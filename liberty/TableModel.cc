// OpenSTA, Static Timing Analyzer
// Copyright (c) 2025, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
// 
// The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software.
// 
// Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 
// This notice may not be removed or altered from any source distribution.

#include "TableModel.hh"

#include <string>

#include "Error.hh"
#include "EnumNameMap.hh"
#include "Units.hh"
#include "Liberty.hh"

namespace sta {

using std::string;
using std::min;
using std::max;
using std::abs;
using std::make_shared;

size_t
findValueIndex(float value,
               const FloatSeq *values);
static void
deleteSigmaModels(TableModel *models[EarlyLate::index_count]);
static string
reportPvt(const LibertyCell *cell,
          const Pvt *pvt,
	  int digits);
static void
appendSpaces(string &result,
	     int count);

TimingModel::TimingModel(LibertyCell *cell) :
  cell_(cell)
{
}

GateTableModel::GateTableModel(LibertyCell *cell,
                               TableModel *delay_model,
			       TableModel *delay_sigma_models[EarlyLate::index_count],
			       TableModel *slew_model,
			       TableModel *slew_sigma_models[EarlyLate::index_count],
                               ReceiverModelPtr receiver_model,
                               OutputWaveforms *output_waveforms) :
  GateTimingModel(cell),
  delay_model_(delay_model),
  slew_model_(slew_model),
  receiver_model_(receiver_model),
  output_waveforms_(output_waveforms)
{
  for (auto el_index : EarlyLate::rangeIndex()) {
    slew_sigma_models_[el_index] = slew_sigma_models
      ? slew_sigma_models[el_index]
      : nullptr;
    delay_sigma_models_[el_index] = delay_sigma_models
      ? delay_sigma_models[el_index]
      : nullptr;
  }
}

GateTableModel::~GateTableModel()
{
  delete delay_model_;
  delete slew_model_;
  delete output_waveforms_;
  deleteSigmaModels(slew_sigma_models_);
  deleteSigmaModels(delay_sigma_models_);
}

static void
deleteSigmaModels(TableModel *models[EarlyLate::index_count])
{
  TableModel *early_model = models[EarlyLate::earlyIndex()];
  TableModel *late_model  = models[EarlyLate::lateIndex()];
  if (early_model == late_model)
    delete early_model;
  else {
    delete early_model;
    delete late_model;
  }
}

void
GateTableModel::setIsScaled(bool is_scaled)
{
  if (delay_model_)
    delay_model_->setIsScaled(is_scaled);
  if (slew_model_)
    slew_model_->setIsScaled(is_scaled);
}

void
GateTableModel::gateDelay(const Pvt *pvt,
			  float in_slew,
			  float load_cap,
			  bool pocv_enabled,
			  // return values
			  ArcDelay &gate_delay,
			  Slew &drvr_slew) const
{
  float delay = findValue(pvt, delay_model_, in_slew, load_cap, 0.0);
  float sigma_early = 0.0;
  float sigma_late = 0.0;
  if (pocv_enabled && delay_sigma_models_[EarlyLate::earlyIndex()])
    sigma_early = findValue(pvt, delay_sigma_models_[EarlyLate::earlyIndex()],
			    in_slew, load_cap, 0.0);
  if (pocv_enabled && delay_sigma_models_[EarlyLate::lateIndex()])
    sigma_late = findValue(pvt, delay_sigma_models_[EarlyLate::lateIndex()],
			   in_slew, load_cap, 0.0);
  gate_delay = makeDelay(delay, sigma_early, sigma_late);

  float slew = findValue(pvt, slew_model_, in_slew, load_cap, 0.0);
  if (pocv_enabled && slew_sigma_models_[EarlyLate::earlyIndex()])
    sigma_early = findValue(pvt, slew_sigma_models_[EarlyLate::earlyIndex()],
			    in_slew, load_cap, 0.0);
  if (pocv_enabled && slew_sigma_models_[EarlyLate::lateIndex()])
    sigma_late = findValue(pvt, slew_sigma_models_[EarlyLate::lateIndex()],
			   in_slew, load_cap, 0.0);
  // Clip negative slews to zero.
  if (slew < 0.0)
    slew = 0.0;
  drvr_slew = makeDelay(slew, sigma_early, sigma_late);
}

void
GateTableModel::gateDelay(const Pvt *pvt,
                          float in_slew,
                          float load_cap,
                          float,
                          bool pocv_enabled,
                          ArcDelay &gate_delay,
                          Slew &drvr_slew) const
{
  gateDelay(pvt, in_slew, load_cap, pocv_enabled, gate_delay, drvr_slew);
}

string
GateTableModel::reportGateDelay(const Pvt *pvt,
				float in_slew,
				float load_cap,
				bool pocv_enabled,
				int digits) const
{
  string result = reportPvt(cell_, pvt, digits);
  result += reportTableLookup("Delay", pvt, delay_model_, in_slew,
                              load_cap, 0.0, digits);
  if (pocv_enabled && delay_sigma_models_[EarlyLate::earlyIndex()])
    result += reportTableLookup("Delay sigma(early)", pvt,
                                delay_sigma_models_[EarlyLate::earlyIndex()],
                                in_slew, load_cap, 0.0, digits);
  if (pocv_enabled && delay_sigma_models_[EarlyLate::lateIndex()])
    result += reportTableLookup("Delay sigma(late)", pvt,
                                delay_sigma_models_[EarlyLate::lateIndex()],
                                in_slew, load_cap, 0.0, digits);
  result += '\n';
  result += reportTableLookup("Slew", pvt, slew_model_, in_slew,
                              load_cap, 9.0, digits);
  if (pocv_enabled && slew_sigma_models_[EarlyLate::earlyIndex()])
    result += reportTableLookup("Slew sigma(early)", pvt,
		      slew_sigma_models_[EarlyLate::earlyIndex()],
		      in_slew, load_cap, 0.0, digits);
  if (pocv_enabled && slew_sigma_models_[EarlyLate::lateIndex()])
    result += reportTableLookup("Slew sigma(late)", pvt,
		      slew_sigma_models_[EarlyLate::lateIndex()],
                                in_slew, load_cap, 0.0, digits);
  float drvr_slew = findValue(pvt, slew_model_, in_slew, load_cap, 0.0);
  if (drvr_slew < 0.0)
    result += "Negative slew clipped to 0.0\n";
  return result;
}

string
GateTableModel::reportTableLookup(const char *result_name,
				  const Pvt *pvt,
				  const TableModel *model,
				  float in_slew,
				  float load_cap,
				  float related_out_cap,
				  int digits) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(model, in_slew, load_cap, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    const LibertyLibrary *library = cell_->libertyLibrary();
    return model->reportValue(result_name, cell_, pvt, axis_value1, nullptr,
                              axis_value2, axis_value3,
                              library->units()->timeUnit(), digits);
  }
  return "";
}

float
GateTableModel::findValue(const Pvt *pvt,
			  const TableModel *model,
			  float in_slew,
			  float load_cap,
			  float related_out_cap) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(model, in_slew, load_cap, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    return model->findValue(cell_, pvt, axis_value1, axis_value2, axis_value3);
  }
  else
    return 0.0;
}

void
GateTableModel::findAxisValues(const TableModel *model,
			       float in_slew,
			       float load_cap,
			       float related_out_cap,
			       // Return values.
			       float &axis_value1,
			       float &axis_value2,
			       float &axis_value3) const
{
  switch (model->order()) {
  case 0:
    axis_value1 = 0.0;
    axis_value2 = 0.0;
    axis_value3 = 0.0;
    break;
  case 1:
    axis_value1 = axisValue(model->axis1(), in_slew, load_cap,
			    related_out_cap);
    axis_value2 = 0.0;
    axis_value3 = 0.0;
    break;
  case 2:
    axis_value1 = axisValue(model->axis1(), in_slew, load_cap,
			    related_out_cap);
    axis_value2 = axisValue(model->axis2(), in_slew, load_cap,
			    related_out_cap);
    axis_value3 = 0.0;
    break;
  case 3:
    axis_value1 = axisValue(model->axis1(), in_slew, load_cap,
			    related_out_cap);
    axis_value2 = axisValue(model->axis2(), in_slew, load_cap,
			    related_out_cap);
    axis_value3 = axisValue(model->axis3(), in_slew, load_cap,
			    related_out_cap);
    break;
  default:
    axis_value1 = 0.0;
    axis_value2 = 0.0;
    axis_value3 = 0.0;
    criticalError(239, "unsupported table order");
  }
}

// Use slew/Cload for the highest Cload, which approximates output
// admittance as the "drive".
float
GateTableModel::driveResistance(const Pvt *pvt) const
{
  float slew, cap;
  maxCapSlew(0.0, pvt, slew, cap);
  return slew / cap;
}

void
GateTableModel::maxCapSlew(float in_slew,
			   const Pvt *pvt,
			   float &slew,
			   float &cap) const
{
  const TableAxis *axis1 = slew_model_->axis1();
  const TableAxis *axis2 = slew_model_->axis2();
  const TableAxis *axis3 = slew_model_->axis3();
  if (axis1
      && axis1->variable() == TableAxisVariable::total_output_net_capacitance) {
    cap = axis1->axisValue(axis1->size() - 1);
    slew = findValue(pvt, slew_model_, in_slew, cap, 0.0);
  }
  else if (axis2
	   && axis2->variable()==TableAxisVariable::total_output_net_capacitance) {
    cap = axis2->axisValue(axis2->size() - 1);
    slew = findValue(pvt, slew_model_, in_slew, cap, 0.0);
  }
  else if (axis3
	   && axis3->variable()==TableAxisVariable::total_output_net_capacitance) {
    cap = axis3->axisValue(axis3->size() - 1);
    slew = findValue(pvt, slew_model_, in_slew, cap, 0.0);
  }
  else {
    // Table not dependent on capacitance.
    cap = 1.0;
    slew = 0.0;
  }
  // Clip negative slews to zero.
  if (slew < 0.0)
    slew = 0.0;
}

float
GateTableModel::axisValue(const TableAxis *axis,
			  float in_slew,
			  float load_cap,
			  float related_out_cap) const
{
  TableAxisVariable var = axis->variable();
  if (var == TableAxisVariable::input_transition_time
      || var == TableAxisVariable::input_net_transition)
    return in_slew;
  else if (var == TableAxisVariable::total_output_net_capacitance)
    return load_cap;
  else if (var == TableAxisVariable::related_out_total_output_net_capacitance)
    return related_out_cap;
  else {
    criticalError(240, "unsupported table axes");
    return 0.0;
  }
}

bool
GateTableModel::checkAxes(const TablePtr &table)
{
  const TableAxis *axis1 = table->axis1();
  const TableAxis *axis2 = table->axis2();
  const TableAxis *axis3 = table->axis3();
  bool axis_ok = true;
  if (axis1)
    axis_ok &= checkAxis(axis1);
  if (axis2)
    axis_ok &= checkAxis(axis2);
  if (axis3)
    axis_ok &= checkAxis(axis3);
  return axis_ok;
}

bool
GateTableModel::checkAxis(const TableAxis *axis)
{
  TableAxisVariable var = axis->variable();
  return var == TableAxisVariable::total_output_net_capacitance
    || var == TableAxisVariable::input_transition_time
    || var == TableAxisVariable::input_net_transition
    || var == TableAxisVariable::related_out_total_output_net_capacitance;
}

////////////////////////////////////////////////////////////////

ReceiverModel::~ReceiverModel()
{
  for (TableModel *model : capacitance_models_)
    delete model;
}

void
ReceiverModel::setCapacitanceModel(TableModel *table_model,
                                   size_t segment,
                                   const RiseFall *rf)
{
  if ((segment + 1) * RiseFall::index_count > capacitance_models_.size())
    capacitance_models_.resize((segment + 1) * RiseFall::index_count);
  size_t idx = segment * RiseFall::index_count + rf->index();
  capacitance_models_[idx] = table_model;
}

bool
ReceiverModel::checkAxes(TablePtr table)
{
  const TableAxis *axis1 = table->axis1();
  const TableAxis *axis2 = table->axis2();
  const TableAxis *axis3 = table->axis3();
  return (axis1 && axis1->variable() == TableAxisVariable::input_net_transition
          && axis2 == nullptr
          && axis3 == nullptr)
    || (axis1 && axis1->variable() == TableAxisVariable::input_net_transition
          && axis2 && axis2->variable() == TableAxisVariable::total_output_net_capacitance
          && axis3 == nullptr)
    || (axis1 && axis1->variable() == TableAxisVariable::total_output_net_capacitance
          && axis2 && axis2->variable() == TableAxisVariable::input_net_transition
          && axis3 == nullptr);
}

////////////////////////////////////////////////////////////////

CheckTableModel::CheckTableModel(LibertyCell *cell,
                                 TableModel *model,
				 TableModel *sigma_models[EarlyLate::index_count]) :
  CheckTimingModel(cell),
  model_(model)
{
  for (auto el_index : EarlyLate::rangeIndex())
    sigma_models_[el_index] = sigma_models ? sigma_models[el_index] : nullptr;
}

CheckTableModel::~CheckTableModel()
{
  delete model_;
  deleteSigmaModels(sigma_models_);
}

void
CheckTableModel::setIsScaled(bool is_scaled)
{
  model_->setIsScaled(is_scaled);
}

ArcDelay
CheckTableModel::checkDelay(const Pvt *pvt,
			    float from_slew,
			    float to_slew,
			    float related_out_cap,
			    bool pocv_enabled) const
{
  if (model_) {
    float mean = findValue(pvt, model_, from_slew, to_slew, related_out_cap);
    float sigma_early = 0.0;
    float sigma_late = 0.0;
    if (pocv_enabled && sigma_models_[EarlyLate::earlyIndex()])
      sigma_early = findValue(pvt, sigma_models_[EarlyLate::earlyIndex()],
			      from_slew, to_slew, related_out_cap);
    if (pocv_enabled && sigma_models_[EarlyLate::lateIndex()])
      sigma_late = findValue(pvt, sigma_models_[EarlyLate::lateIndex()],
			     from_slew, to_slew, related_out_cap);
    return makeDelay(mean, sigma_early, sigma_late);  
  }
  else
    return 0.0;
}

float
CheckTableModel::findValue(const Pvt *pvt,
			   const TableModel *model,
			   float from_slew,
			   float to_slew,
			   float related_out_cap) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(from_slew, to_slew, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    return model->findValue(cell_, pvt, axis_value1, axis_value2, axis_value3);
  }
  else
    return 0.0;
}

string
CheckTableModel::reportCheckDelay(const Pvt *pvt,
				  float from_slew,
				  const char *from_slew_annotation,
				  float to_slew,
				  float related_out_cap,
				  bool pocv_enabled,
				  int digits) const
{
  string result = reportTableDelay("Check", pvt, model_,
                                   from_slew, from_slew_annotation, to_slew,
                                   related_out_cap, digits);
  if (pocv_enabled && sigma_models_[EarlyLate::earlyIndex()])
    result += reportTableDelay("Check sigma early", pvt,
                               sigma_models_[EarlyLate::earlyIndex()],
                               from_slew, from_slew_annotation, to_slew,
                               related_out_cap, digits);
  if (pocv_enabled && sigma_models_[EarlyLate::lateIndex()])
    result += reportTableDelay("Check sigma late", pvt,
                               sigma_models_[EarlyLate::lateIndex()],
                               from_slew, from_slew_annotation, to_slew,
                               related_out_cap, digits);
  return result;
}

string
CheckTableModel::reportTableDelay(const char *result_name,
				  const Pvt *pvt,
				  const TableModel *model,
				  float from_slew,
				  const char *from_slew_annotation,
				  float to_slew,
				  float related_out_cap,
				  int digits) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(from_slew, to_slew, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    string result = reportPvt(cell_, pvt, digits);
    result += model_->reportValue(result_name, cell_, pvt,
                                  axis_value1, from_slew_annotation, axis_value2,
                                  axis_value3,
                                  cell_->libertyLibrary()->units()->timeUnit(), digits);
    return result;
  }
  return "";
}

void
CheckTableModel::findAxisValues(float from_slew,
				float to_slew,
				float related_out_cap,
				// Return values.
				float &axis_value1,
				float &axis_value2,
				float &axis_value3) const
{
  switch (model_->order()) {
  case 0:
    axis_value1 = 0.0;
    axis_value2 = 0.0;
    axis_value3 = 0.0;
    break;
  case 1:
    axis_value1 = axisValue(model_->axis1(), from_slew, to_slew,
			    related_out_cap);
    axis_value2 = 0.0;
    axis_value3 = 0.0;
    break;
  case 2:
    axis_value1 = axisValue(model_->axis1(), from_slew, to_slew,
			    related_out_cap);
    axis_value2 = axisValue(model_->axis2(), from_slew, to_slew,
			    related_out_cap);
    axis_value3 = 0.0;
    break;
  case 3:
    axis_value1 = axisValue(model_->axis1(), from_slew, to_slew,
			    related_out_cap);
    axis_value2 = axisValue(model_->axis2(), from_slew, to_slew,
			    related_out_cap);
    axis_value3 = axisValue(model_->axis3(), from_slew, to_slew,
			    related_out_cap);
    break;
  default:
    criticalError(241, "unsupported table order");
  }
}

float
CheckTableModel::axisValue(const TableAxis *axis,
			   float from_slew,
			   float to_slew,
			   float related_out_cap) const
{
  TableAxisVariable var = axis->variable();
  if (var == TableAxisVariable::related_pin_transition)
    return from_slew;
  else if (var == TableAxisVariable::constrained_pin_transition)
    return to_slew;
  else if (var == TableAxisVariable::related_out_total_output_net_capacitance)
    return related_out_cap;
  else {
    criticalError(242, "unsupported table axes");
    return 0.0;
  }
}

bool
CheckTableModel::checkAxes(const TablePtr table)
{
  const TableAxis *axis1 = table->axis1();
  const TableAxis *axis2 = table->axis2();
  const TableAxis *axis3 = table->axis3();
  bool axis_ok = true;
  if (axis1)
    axis_ok &= checkAxis(axis1);
  if (axis2)
    axis_ok &= checkAxis(axis2);
  if (axis3)
    axis_ok &= checkAxis(axis3);
  return axis_ok;
}

bool
CheckTableModel::checkAxis(const TableAxis *axis)
{
  TableAxisVariable var = axis->variable();
  return var == TableAxisVariable::constrained_pin_transition
    || var == TableAxisVariable::related_pin_transition
    || var == TableAxisVariable::related_out_total_output_net_capacitance;
}

////////////////////////////////////////////////////////////////

TableModel::TableModel(TablePtr table,
                       TableTemplate *tbl_template,
		       ScaleFactorType scale_factor_type,
		       const RiseFall *rf) :
  table_(table),
  tbl_template_(tbl_template),
  scale_factor_type_(int(scale_factor_type)),
  rf_index_(rf->index()),
  is_scaled_(false)
{
}

int
TableModel::order() const
{
  return table_->order();
}

void
TableModel::setScaleFactorType(ScaleFactorType type)
{
  scale_factor_type_ = int(type);
}

void
TableModel::setIsScaled(bool is_scaled)
{
  is_scaled_ = is_scaled;
}

const TableAxis *
TableModel::axis1() const
{
  return table_->axis1();
}

const TableAxis *
TableModel::axis2() const
{
  return table_->axis2();
}

const TableAxis *
TableModel::axis3() const
{
  return table_->axis3();
}

float
TableModel::value(size_t axis_index1,
                  size_t axis_index2,
                  size_t axis_index3) const
{
  return table_->value(axis_index1, axis_index2, axis_index3);
}

float
TableModel::findValue(float axis_value1,
		      float axis_value2,
		      float axis_value3) const
{
  return table_->findValue(axis_value1, axis_value2, axis_value3);
}

float
TableModel::findValue(const LibertyCell *cell,
		      const Pvt *pvt,
		      float axis_value1,
		      float axis_value2,
		      float axis_value3) const
{
  return table_->findValue(axis_value1, axis_value2, axis_value3)
    * scaleFactor(cell, pvt);
}

float
TableModel::scaleFactor(const LibertyCell *cell,
			const Pvt *pvt) const
{
  if (is_scaled_)
    // Scaled tables are not derated because scale factors are wrt
    // nominal pvt.
    return 1.0F;
  else
    return cell->libertyLibrary()->scaleFactor(static_cast<ScaleFactorType>(scale_factor_type_),
                                               rf_index_, cell, pvt);
}

string
TableModel::reportValue(const char *result_name,
			const LibertyCell *cell,
			const Pvt *pvt,
			float value1,
			const char *comment1,
			float value2,
			float value3,
                        const Unit *table_unit,
			int digits) const
{
  string result = table_->reportValue("Table value", cell, pvt, value1,
                                      comment1, value2, value3, table_unit, digits);

  result += reportPvtScaleFactor(cell, pvt, digits);

  result += result_name;
  result += " = ";
  result += table_unit->asString(findValue(cell, pvt, value1, value2, value3), digits);
  result += '\n';
  return result;
}

static string
reportPvt(const LibertyCell *cell,
	  const Pvt *pvt,
	  int digits)
{
  const LibertyLibrary *library = cell->libertyLibrary();
  if (pvt == nullptr)
    pvt = library->defaultOperatingConditions();
  if (pvt) {
    string result;
    stringPrint(result, "P = %.*f V = %.*f T = %.*f\n",
		digits, pvt->process(),
		digits, pvt->voltage(),
		digits, pvt->temperature());
    return result;
  }
  return "";
}

string
TableModel::reportPvtScaleFactor(const LibertyCell *cell,
				 const Pvt *pvt,
				 int digits) const
{
  if (pvt == nullptr)
    pvt = cell->libertyLibrary()->defaultOperatingConditions();
  if (pvt) {
    string result;
    stringPrint(result, "PVT scale factor = %.*f\n",
		digits,
		scaleFactor(cell, pvt));
    return result;
  }
  return "";
}

////////////////////////////////////////////////////////////////

Table0::Table0(float value) :
  Table(),
  value_(value)
{
}

float
Table0::value(size_t,
              size_t,
              size_t) const
{
  return value_;
}

float
Table0::findValue(float,
		  float,
		  float) const
{
  return value_;
}

string
Table0::reportValue(const char *result_name,
		    const LibertyCell *,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
                    const Unit *table_unit,
		    int digits) const
{
  string result = result_name;
  result += " constant = ";
  result += table_unit->asString(findValue(value1, value2, value3), digits);
  if (comment1)
    result += comment1;
  result += '\n';
  return result;
}

void
Table0::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  report->reportLine("%s", table_unit->asString(value_, digits));
}

////////////////////////////////////////////////////////////////

Table1::Table1() :
  Table(),
  values_(nullptr),
  axis1_(nullptr)
{
}

Table1::Table1(FloatSeq *values,
	       TableAxisPtr axis1) :
  Table(),
  values_(values),
  axis1_(axis1)
{
}

Table1::Table1(Table1 &&table) :
  Table(),
  values_(table.values_),
  axis1_(table.axis1_)
{
  table.values_ = nullptr;
  table.axis1_ = nullptr;
}

Table1::Table1(const Table1 &table) :
  Table(),
  values_(new FloatSeq(*table.values_)),
  axis1_(table.axis1_)
{
}

Table1::~Table1()
{
  delete values_;
}

Table1 &
Table1::operator=(Table1 &&table)
{
  values_ = table.values_;
  axis1_ = table.axis1_;
  table.values_ = nullptr;
  table.axis1_ = nullptr;
  return *this;
}

float
Table1::value(size_t axis_index1,
              size_t,
              size_t) const
{
  return value(axis_index1);
}

float
Table1::value(size_t axis_index1) const
{
  return (*values_)[axis_index1];
}

float
Table1::findValue(float axis_value1,
		  float,
		  float) const
{
  return findValue(axis_value1);
}

float
Table1::findValue(float axis_value1) const
{
  if (axis1_->size() == 1)
    return this->value(axis_value1);
  else {
    size_t axis_index1 = axis1_->findAxisIndex(axis_value1);
    double x1 = axis_value1;
    double x1l = axis1_->axisValue(axis_index1);
    double x1u = axis1_->axisValue(axis_index1 + 1);
    double y1 = this->value(axis_index1);
    double y2 = this->value(axis_index1 + 1);
    double dx1 = (x1 - x1l) / (x1u - x1l);
    return (1 - dx1) * y1 + dx1 * y2;
  }
}

float
Table1::findValueClip(float axis_value1) const
{
  if (axis1_->size() == 1)
    return this->value(axis_value1);
  else {
    size_t axis_index1 = axis1_->findAxisIndex(axis_value1);
    double x1 = axis_value1;
    double x1l = axis1_->axisValue(axis_index1);
    double x1u = axis1_->axisValue(axis_index1 + 1);
    if (x1 < x1l)
      return 0.0;
    else if (x1 > x1u)
      return this->value(axis1_->size() - 1);
    else {
      double y1 = this->value(axis_index1);
      double y2 = this->value(axis_index1 + 1);
      double dx1 = (x1 - x1l) / (x1u - x1l);
      return (1 - dx1) * y1 + dx1 * y2;
    }
  }
}

string
Table1::reportValue(const char *result_name,
		    const LibertyCell *cell,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
                    const Unit *table_unit,
		    int digits) const
{
  const Units *units = cell->libertyLibrary()->units();
  const Unit *unit1 = axis1_->unit(units);
  string result = "Table is indexed by\n  ";
  result += axis1_->variableString();
  result += " = ";
  result += unit1->asString(value1, digits);
  if (comment1)
    result += comment1;
  result += '\n';

  if (axis1_->size() != 1) {
    size_t index1 = axis1_->findAxisIndex(value1);
    result += "  ";
    result += unit1->asString(axis1_->axisValue(index1), digits);
    result += "      ";
    result += unit1->asString(axis1_->axisValue(index1 + 1), digits);
    result += '\n';

    result += "    --------------------\n";

    result += "| ";
    result += table_unit->asString(value(index1), digits);
    result += "     ";
    result += table_unit->asString(value(index1 + 1),
				    digits);
    result += '\n';
  }

  result += result_name;
  result += " = ";
  result += table_unit->asString(findValue(value1, value2, value3), digits);
  result += '\n';
  return result;
}

void
Table1::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *unit1 = axis1_->unit(units);
  const Unit *table_unit = units->timeUnit();
  report->reportLine("%s", tableVariableString(axis1_->variable()));
  report->reportLine("------------------------------");
  string line;
  for (size_t index1 = 0; index1 < axis1_->size(); index1++) {
    line += unit1->asString(axis1_->axisValue(index1), digits);
    line += " ";
  }
  report->reportLineString(line);

  line.clear();
  for (size_t index1 = 0; index1 < axis1_->size(); index1++) {
    line += table_unit->asString(value(index1), digits);
    line += " ";
  }
  report->reportLineString(line);
}

////////////////////////////////////////////////////////////////

Table2::Table2(FloatTable *values,
	       TableAxisPtr axis1,
	       TableAxisPtr axis2) :
  Table(),
  values_(values),
  axis1_(axis1),
  axis2_(axis2)
{
}

Table2::~Table2()
{
  values_->deleteContents();
  delete values_;
}

float
Table2::value(size_t axis_index1,
              size_t axis_index2,
              size_t) const
{
  return value(axis_index1, axis_index2);
}

float
Table2::value(size_t axis_index1,
              size_t axis_index2) const
{
  FloatSeq *row = (*values_)[axis_index1];
  return (*row)[axis_index2];
}

// Bilinear Interpolation.
float
Table2::findValue(float axis_value1,
		  float axis_value2,
		  float) const
{
  size_t size1 = axis1_->size();
  size_t size2 = axis2_->size();
  if (size1 == 1) {
    if (size2 == 1)
      return value(0, 0);
    else {
      size_t axis_index2 = axis2_->findAxisIndex(axis_value2);
      double x2 = axis_value2;
      double y00 = value(0, axis_index2);
      double x2l = axis2_->axisValue(axis_index2);
      double x2u = axis2_->axisValue(axis_index2 + 1);
      double dx2 = (x2 - x2l) / (x2u - x2l);
      double y01 = value(0, axis_index2 + 1);
      double tbl_value
	= (1 - dx2) * y00
	+      dx2  * y01;
      return tbl_value;
    }
  }
  else if (size2 == 1) {
    size_t axis_index1 = axis1_->findAxisIndex(axis_value1);
    double x1 = axis_value1;
    double y00 = value(axis_index1, 0);
    double x1l = axis1_->axisValue(axis_index1);
    double x1u = axis1_->axisValue(axis_index1 + 1);
    double dx1 = (x1 - x1l) / (x1u - x1l);
    double y10 = value(axis_index1 + 1, 0);
    double tbl_value
      = (1 - dx1) * y00
      +      dx1  * y10;
    return tbl_value;
  }
  else {
    size_t axis_index1 = axis1_->findAxisIndex(axis_value1);
    size_t axis_index2 = axis2_->findAxisIndex(axis_value2);
    double x1 = axis_value1;
    double x2 = axis_value2;
    double y00 = value(axis_index1, axis_index2);
    double x1l = axis1_->axisValue(axis_index1);
    double x1u = axis1_->axisValue(axis_index1 + 1);
    double dx1 = (x1 - x1l) / (x1u - x1l);
    double y10 = value(axis_index1 + 1, axis_index2);
    double y11 = value(axis_index1 + 1, axis_index2 + 1);
    double x2l = axis2_->axisValue(axis_index2);
    double x2u = axis2_->axisValue(axis_index2 + 1);
    double dx2 = (x2 - x2l) / (x2u - x2l);
    double y01 = value(axis_index1, axis_index2 + 1);
    double tbl_value
      = (1 - dx1) * (1 - dx2) * y00
      +      dx1  * (1 - dx2) * y10
      +      dx1  *      dx2  * y11
      + (1 - dx1) *      dx2  * y01;
    return tbl_value;
  }
}

string
Table2::reportValue(const char *result_name,
		    const LibertyCell *cell,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
                    const Unit *table_unit,
		    int digits) const
{
  const Units *units = cell->libertyLibrary()->units();
  const Unit *unit1 = axis1_->unit(units);
  const Unit *unit2 = axis2_->unit(units);
  string result = "------- ";
  result += axis1_->variableString(),
  result += " = ";
  result += unit1->asString(value1, digits);
  if (comment1)
    result += comment1;
  result += '\n';

  result += "|       ";
  result += axis2_->variableString();
  result += " = ";
  result += unit2->asString(value2, digits);
  result += '\n';

  size_t index1 = axis1_->findAxisIndex(value1);
  size_t index2 = axis2_->findAxisIndex(value2);
  result += "|        ";
  result += unit2->asString(axis2_->axisValue(index2), digits);
  if (axis2_->size() != 1) {
    result += "     ";
    result += unit2->asString(axis2_->axisValue(index2 + 1), digits);
  }
  result += '\n';

  result += "v      --------------------\n";
  result += unit1->asString(axis1_->axisValue(index1), digits);
  result += " | ";

  result += table_unit->asString(value(index1, index2), digits);
  if (axis2_->size() != 1) {
    result += "     ";
    result += table_unit->asString(value(index1, index2 + 1), digits);
  }
  result += '\n';

  if (axis1_->size() != 1) {
    result += unit1->asString(axis1_->axisValue(index1 + 1), digits);
    result += " | ";
    result += table_unit->asString(value(index1 + 1, index2), digits);
    if (axis2_->size() != 1) {
      result += "     ";
      result +=table_unit->asString(value(index1 + 1, index2 + 1),digits);
    }
  }
  result += '\n';

  result += result_name;
  result += " = ";
  result += table_unit->asString(findValue(value1, value2, value3), digits);
  result += '\n';
  return result;
}

void
Table2::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = axis1_->unit(units);
  const Unit *unit2 = axis2_->unit(units);
  report->reportLine("%s", tableVariableString(axis2_->variable()));
  report->reportLine("     ------------------------------");
  string line = "     ";
  for (size_t index2 = 0; index2 < axis2_->size(); index2++) {
    line += unit2->asString(axis2_->axisValue(index2), digits);
    line += " ";
  }
  report->reportLineString(line);

  for (size_t index1 = 0; index1 < axis1_->size(); index1++) {
    line = unit1->asString(axis1_->axisValue(index1), digits);
    line += " |";
    for (size_t index2 = 0; index2 < axis2_->size(); index2++) {
      line += table_unit->asString(value(index1, index2), digits);
      line += " ";
    }
    report->reportLineString(line);
  }
}

////////////////////////////////////////////////////////////////

Table3::Table3(FloatTable *values,
	       TableAxisPtr axis1,
	       TableAxisPtr axis2,
	       TableAxisPtr axis3) :
  Table2(values, axis1, axis2),
  axis3_(axis3)
{
}

float
Table3::value(size_t axis_index1,
              size_t axis_index2,
              size_t axis_index3) const
{
  size_t row = axis_index1 * axis2_->size() + axis_index2;
  return values_->operator[](row)->operator[](axis_index3);
}

// Bilinear Interpolation.
float
Table3::findValue(float axis_value1,
		  float axis_value2,
		  float axis_value3) const
{
  size_t axis_index1 = axis1_->findAxisIndex(axis_value1);
  size_t axis_index2 = axis2_->findAxisIndex(axis_value2);
  size_t axis_index3 = axis3_->findAxisIndex(axis_value3);
  double x1 = axis_value1;
  double x2 = axis_value2;
  double x3 = axis_value3;
  double dx1 = 0.0;
  double dx2 = 0.0;
  double dx3 = 0.0;
  double y000 = value(axis_index1, axis_index2, axis_index3);
  double y001 = 0.0;
  double y010 = 0.0;
  double y011 = 0.0;
  double y100 = 0.0;
  double y101 = 0.0;
  double y110 = 0.0;
  double y111 = 0.0;

  if (axis1_->size() != 1) {
    double x1l = axis1_->axisValue(axis_index1);
    double x1u = axis1_->axisValue(axis_index1 + 1);
    dx1 = (x1 - x1l) / (x1u - x1l);
    y100 = value(axis_index1 + 1, axis_index2, axis_index3);
    if (axis3_->size() != 1)
      y101 = value(axis_index1 + 1, axis_index2, axis_index3 + 1);
    if (axis2_->size() != 1) {
      y110 = value(axis_index1 + 1, axis_index2 + 1, axis_index3);
      if (axis3_->size() != 1)
	y111 = value(axis_index1 + 1, axis_index2 + 1, axis_index3 + 1);
    }
  }
  if (axis2_->size() != 1) {
    double x2l = axis2_->axisValue(axis_index2);
    double x2u = axis2_->axisValue(axis_index2 + 1);
    dx2 = (x2 - x2l) / (x2u - x2l);
    y010 = value(axis_index1, axis_index2 + 1, axis_index3);
    if (axis3_->size() != 1)
      y011 = value(axis_index1, axis_index2 + 1, axis_index3 + 1);
  }
  if (axis3_->size() != 1) {
    double x3l = axis3_->axisValue(axis_index3);
    double x3u = axis3_->axisValue(axis_index3 + 1);
    dx3 = (x3 - x3l) / (x3u - x3l);
    y001 = value(axis_index1, axis_index2, axis_index3 + 1);
  }

  double tbl_value
    = (1 - dx1) * (1 - dx2) * (1 - dx3) * y000
    + (1 - dx1) * (1 - dx2) *      dx3  * y001
    + (1 - dx1) *      dx2  * (1 - dx3) * y010
    + (1 - dx1) *      dx2  *      dx3  * y011
    +      dx1  * (1 - dx2) * (1 - dx3) * y100
    +      dx1  * (1 - dx2) *      dx3  * y101
    +      dx1  *      dx2  * (1 - dx3) * y110
    +      dx1  *      dx2  *      dx3  * y111;
  return tbl_value;
}

// Sample output.
//
//    --------- input_net_transition = 0.00
//    |    ---- total_output_net_capacitance = 0.20
//    |    |    related_out_total_output_net_capacitance = 0.10
//    |    |    0.00     0.30
//    v    |    --------------------
//  0.01   v   / 0.23     0.25
// 0.00  0.20 | 0.10     0.20
//            |/ 0.30     0.32
//       0.40 | 0.20     0.30
string
Table3::reportValue(const char *result_name,
		    const LibertyCell *cell,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
                    const Unit *table_unit,
		    int digits) const
{
  const Units *units = cell->libertyLibrary()->units();
  const Unit *unit1 = axis1_->unit(units);
  const Unit *unit2 = axis2_->unit(units);
  const Unit *unit3 = axis3_->unit(units);

  string result = "   --------- ";
  result += axis1_->variableString(),
  result += " = ";
  result += unit1->asString(value1, digits);
  if (comment1)
    result += comment1;
  result += '\n';

  result += "   |    ---- ";
  result += axis2_->variableString(),
  result += " = ";
  result += unit2->asString(value2, digits);
  result += '\n';

  result += "   |    |    ";
  result += axis3_->variableString();
  result += " = ";
  result += unit3->asString(value3, digits);
  result += '\n';

  size_t axis_index1 = axis1_->findAxisIndex(value1);
  size_t axis_index2 = axis2_->findAxisIndex(value2);
  size_t axis_index3 = axis3_->findAxisIndex(value3);

  result += "   |    |    ";
  result += unit3->asString(axis3_->axisValue(axis_index3), digits);
  if (axis3_->size() != 1) {
    result += "     ";
    result += unit3->asString(axis3_->axisValue(axis_index3 + 1), digits);
  }
  result += '\n';

  result += "   v    |    --------------------\n";

  if (axis1_->size() != 1) {
    result += " ";
    result += unit1->asString(axis1_->axisValue(axis_index1+1), digits);
    result += "   v   / ";
    result += table_unit->asString(value(axis_index1+1,axis_index2,axis_index3),
				    digits);
    if (axis3_->size() != 1) {
      result += "     ";
      result += table_unit->asString(value(axis_index1+1,axis_index2,axis_index3+1),
				      digits);
    }
  }
  else {
    appendSpaces(result, digits+3);
    result += "   v   / ";
  }
  result += '\n';

  result += unit1->asString(axis1_->axisValue(axis_index1), digits);
  result += "  ";
  result += unit2->asString(axis2_->axisValue(axis_index2), digits);
  result += " | ";
  result += table_unit->asString(value(axis_index1, axis_index2, axis_index3), digits);
  if (axis3_->size() != 1) {
    result += "     ";
    result += table_unit->asString(value(axis_index1, axis_index2, axis_index3+1),
				    digits);
  }
  result += '\n';

  result += "           |/ ";
  if (axis1_->size() != 1
      && axis2_->size() != 1) {
    result += table_unit->asString(value(axis_index1+1,axis_index2+1,axis_index3),
				    digits);
    if (axis3_->size() != 1) {
      result += "     ";
      result +=table_unit->asString(value(axis_index1+1,axis_index2+1,axis_index3+1),
				     digits);
    }
  }
  result += '\n';

  result += "      ";
  result += unit2->asString(axis2_->axisValue(axis_index2 + 1), digits);
  result += " | ";
  if (axis2_->size() != 1) {
    result += table_unit->asString(value(axis_index1, axis_index2+1, axis_index3),
                                   digits);
    if (axis3_->size() != 1) {
      result += "     ";
      result +=table_unit->asString(value(axis_index1, axis_index2+1,axis_index3+1),
                                    digits);
    }
  }
  result += '\n';

  result += result_name;
  result += " = ";
  result += table_unit->asString(findValue(value1, value2, value3), digits);
  result += '\n';
  return result;
}

static void
appendSpaces(string &result,
	     int count)
{
  while (count--)
    result += ' ';
}

void
Table3::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = axis1_->unit(units);
  const Unit *unit2 = axis2_->unit(units);
  const Unit *unit3 = axis3_->unit(units);
  for (size_t axis_index1 = 0; axis_index1 < axis1_->size(); axis_index1++) {
    report->reportLine("%s %s", tableVariableString(axis1_->variable()),
                       unit1->asString(axis1_->axisValue(axis_index1), digits));

    report->reportLine("%s", tableVariableString(axis3_->variable()));
    report->reportLine("     ------------------------------");
    string line = "     ";
    for (size_t axis_index3 = 0; axis_index3 < axis3_->size(); axis_index3++) {
      line += unit3->asString(axis3_->axisValue(axis_index3), digits);
      line += " ";
    }
    report->reportLineString(line);

    for (size_t axis_index2 = 0; axis_index2 < axis2_->size(); axis_index2++) {
      line = unit2->asString(axis2_->axisValue(axis_index2),digits);
      line += " |";
      for (size_t axis_index3 = 0; axis_index3 < axis3_->size(); axis_index3++) {
        line += table_unit->asString(value(axis_index1, axis_index2, axis_index3),digits);
        line += " ";
      }
      report->reportLineString(line);
    }
  }
}

////////////////////////////////////////////////////////////////

TableAxis::TableAxis(TableAxisVariable variable,
		     FloatSeq *values) :
  variable_(variable),
  values_(values)
{
}

TableAxis::~TableAxis()
{
  delete values_;
}

float
TableAxis::min() const
{
  if (!values_->empty())
    return (*values_)[0];
  else
    return 0.0;
}

float
TableAxis::max() const
{
  size_t size = values_->size();
  if (size > 0)
    return (*values_)[values_->size() - 1];
  else
    return 0.0;
}

bool
TableAxis::inBounds(float value) const
{
  size_t size = values_->size();
  return size > 1
    && value >= (*values_)[0]
    && value <= (*values_)[size - 1];
}

size_t
TableAxis::findAxisIndex(float value) const
{
  return findValueIndex(value, values_);
}

// Bisection search.
// Assumes values are monotonically increasing.
size_t
findValueIndex(float value,
               const FloatSeq *values)
{
  size_t size = values->size();
  if (size <= 1 || value <= (*values)[0])
    return 0;
  else if (value >= (*values)[size - 1])
    // Return max_index-1 for value too large so interpolation pts are index,index+1.
    return size - 2;
  else {
    int lower = -1;
    int upper = size;
    while (upper - lower > 1) {
      int mid = (upper + lower) >> 1;
      if (value >= (*values)[mid])
	lower = mid;
      else
	upper = mid;
    }
    return lower;
  }
}

void
TableAxis::findAxisIndex(float value,
                         // Return values.
                         size_t &index,
                         bool &exists) const
{
  size_t size = values_->size();
  if (size != 0
      && value >= (*values_)[0]
      && value <= (*values_)[size - 1]) {
    int lower = -1;
    int upper = size;
    while (upper - lower > 1) {
      int mid = (upper + lower) >> 1;
      if (value == (*values_)[mid]) {
        index = mid;
        exists = true;
        return;
      }
      if (value > (*values_)[mid])
	lower = mid;
      else
	upper = mid;
    }
  }
  exists = false;
}

size_t
TableAxis::findAxisClosestIndex(float value) const
{
  size_t size = values_->size();
  if (size <= 1 || value <= (*values_)[0])
    return 0;
  else if (value >= (*values_)[size - 1])
    return size - 1;
  else {
    int lower = -1;
    int upper = size;
    while (upper - lower > 1) {
      int mid = (upper + lower) >> 1;
      if (value >= (*values_)[mid])
	lower = mid;
      else
	upper = mid;
    }
    if ((value - (*values_)[lower]) < ((*values_)[upper] - value))
      return lower;
    else
      return upper;
  }
}

const char *
TableAxis::variableString() const
{
  return tableVariableString(variable_);
}

const Unit *
TableAxis::unit(const Units *units)
{
  return tableVariableUnit(variable_, units);
}

////////////////////////////////////////////////////////////////

static EnumNameMap<TableAxisVariable> table_axis_variable_map =
  {{TableAxisVariable::total_output_net_capacitance, "total_output_net_capacitance"},
   {TableAxisVariable::equal_or_opposite_output_net_capacitance, "equal_or_opposite_output_net_capacitance"},
   {TableAxisVariable::input_net_transition, "input_net_transition"},
   {TableAxisVariable::input_transition_time, "input_transition_time"},
   {TableAxisVariable::related_pin_transition, "related_pin_transition"},
   {TableAxisVariable::constrained_pin_transition, "constrained_pin_transition"},
   {TableAxisVariable::output_pin_transition, "output_pin_transition"},
   {TableAxisVariable::connect_delay, "connect_delay"},
   {TableAxisVariable::related_out_total_output_net_capacitance, "related_out_total_output_net_capacitance"},
   {TableAxisVariable::time, "time"},
   {TableAxisVariable::iv_output_voltage, "iv_output_voltage"},
   {TableAxisVariable::input_noise_width, "input_noise_width"},
   {TableAxisVariable::input_noise_height, "input_noise_height"},
   {TableAxisVariable::input_voltage, "input_voltage"},
   {TableAxisVariable::output_voltage, "output_voltage"},
   {TableAxisVariable::path_depth, "path_depth"},
   {TableAxisVariable::path_distance, "path_distance"},
   {TableAxisVariable::normalized_voltage, "normalized_voltage"}
  };

TableAxisVariable
stringTableAxisVariable(const char *variable)
{
  return table_axis_variable_map.find(variable, TableAxisVariable::unknown);
}

const char *
tableVariableString(TableAxisVariable variable)
{
  return table_axis_variable_map.find(variable);
}

const Unit *
tableVariableUnit(TableAxisVariable variable,
		  const Units *units)
{
  switch (variable) {
  case TableAxisVariable::total_output_net_capacitance:
  case TableAxisVariable::related_out_total_output_net_capacitance:
  case TableAxisVariable::equal_or_opposite_output_net_capacitance:
    return units->capacitanceUnit();
  case TableAxisVariable::input_net_transition:
  case TableAxisVariable::input_transition_time:
  case TableAxisVariable::related_pin_transition:
  case TableAxisVariable::constrained_pin_transition:
  case TableAxisVariable::output_pin_transition:
  case TableAxisVariable::connect_delay:
  case TableAxisVariable::time:
  case TableAxisVariable::input_noise_height:
    return units->timeUnit();
  case TableAxisVariable::input_voltage:
  case TableAxisVariable::output_voltage:
  case TableAxisVariable::iv_output_voltage:
  case TableAxisVariable::input_noise_width:
    return units->voltageUnit();
  case TableAxisVariable::path_distance:
    return units->distanceUnit();
  case TableAxisVariable::path_depth:
  case TableAxisVariable::normalized_voltage:
  case TableAxisVariable::unknown:
    return units->scalarUnit();
  }
  // Prevent warnings from lame compilers.
  return nullptr;
}

////////////////////////////////////////////////////////////////

OutputWaveforms::OutputWaveforms(TableAxisPtr slew_axis,
                                 TableAxisPtr cap_axis,
                                 const RiseFall *rf,
                                 Table1Seq &current_waveforms,
                                 Table1 *ref_times) :
  slew_axis_(slew_axis),
  cap_axis_(cap_axis),
  rf_(rf),
  current_waveforms_(current_waveforms),
  ref_times_(ref_times),
  vdd_(0.0)
{
}

OutputWaveforms::~OutputWaveforms()
{
  current_waveforms_.deleteContents();
  voltage_waveforms_.deleteContents();
  voltage_currents_.deleteContents();
  delete ref_times_;
}

bool
OutputWaveforms::checkAxes(const TableTemplate *tbl_template)
{
  const TableAxis *axis1 = tbl_template->axis1();
  const TableAxis *axis2 = tbl_template->axis2();
  const TableAxis *axis3 = tbl_template->axis3();
  return (axis1 && axis1->variable() == TableAxisVariable::input_net_transition
          && axis2->variable() == TableAxisVariable::time
          && axis3 == nullptr)
    || (axis1 && axis1->variable() == TableAxisVariable::input_net_transition
          && axis2 && axis2->variable() == TableAxisVariable::total_output_net_capacitance
          && axis3->variable() == TableAxisVariable::time)
    || (axis1 && axis1->variable() == TableAxisVariable::total_output_net_capacitance
          && axis2 && axis2->variable() == TableAxisVariable::input_net_transition
          && axis3->variable() == TableAxisVariable::time);
}

void
OutputWaveforms::ensureVoltageWaveforms(float vdd)
{
  if (voltage_waveforms_.empty()) {
    vdd_ = vdd;
    size_t size = current_waveforms_.size();
    voltage_waveforms_.resize(size);
    voltage_currents_.resize(size);
    size_t cap_count = cap_axis_->size();
    for (size_t slew_index = 0; slew_index < slew_axis_->size(); slew_index++) {
      for (size_t cap_index = 0; cap_index < cap_count; cap_index++) {
        size_t wave_index = slew_index * cap_count + cap_index;
        findVoltages(wave_index, cap_axis_->axisValue(cap_index));
      }
    }
  }
}

void
OutputWaveforms::findVoltages(size_t wave_index,
                              float cap)
{
  // Integrate current waveform to find voltage waveform.
  // i = C dv/dt
  FloatSeq *volts = new FloatSeq;
  Table1 *currents = current_waveforms_[wave_index];
  const TableAxis *time_axis = currents->axis1();
  float prev_time = time_axis->axisValue(0);
  float prev_current = currents->value(0);
  float voltage = 0.0;
  volts->push_back(voltage);
  bool always_rise = true;
  bool invert = (always_rise && rf_ == RiseFall::fall());
  for (size_t i = 1; i < time_axis->size(); i++) {
    float time = time_axis->axisValue(i);
    float current = currents->value(i);
    float dv = (current + prev_current) / 2.0 * (time - prev_time) / cap;
    voltage += invert ? -dv : dv;
    volts->push_back(voltage);
    prev_time = time;
    prev_current = current;
  }
  (*volts)[volts->size() - 1] = vdd_;
  Table1 *volt_table = new Table1(volts, currents->axis1ptr());
  voltage_waveforms_[wave_index] = volt_table;

  // Make voltage -> current table.
  FloatSeq *axis_volts = new FloatSeq(*volts);
  TableAxisPtr volt_axis =
    make_shared<TableAxis>(TableAxisVariable::input_voltage, axis_volts);
  FloatSeq *currents1 = new FloatSeq(*currents->values());
  Table1 *volt_currents = new Table1(currents1, volt_axis);
  voltage_currents_[wave_index] = volt_currents;
}

Table1
OutputWaveforms::currentWaveform(float slew,
                                 float cap)
{
  FloatSeq *times = new FloatSeq;
  FloatSeq *currents = new FloatSeq;
  for (size_t i = 0; i <= voltage_waveform_step_count_; i++) {
    float volt = i * vdd_ / voltage_waveform_step_count_;
    float time = voltageTime(slew, cap, volt);
    float current = voltageCurrent(slew, cap, volt);
    times->push_back(time);
    currents->push_back(current);
  }
  TableAxisPtr time_axis = make_shared<TableAxis>(TableAxisVariable::time, times);
  return Table1(currents, time_axis);
}

const Table1 *
OutputWaveforms::currentWaveformRaw(float slew,
                                    float cap)
{
  size_t slew_index = slew_axis_->findAxisClosestIndex(slew);
  size_t cap_index = cap_axis_->findAxisClosestIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index = slew_index * cap_count + cap_index;
  return current_waveforms_[wave_index];
}

float
OutputWaveforms::timeCurrent(float slew,
                             float cap,
                             float time)
{
  // Current waveform is not monotonic, so use volt/current correspondence.
  float volt = timeVoltage(slew, cap, time);
  return voltageCurrent(slew, cap, volt);
}

float
OutputWaveforms::timeVoltage(float slew,
                             float cap,
                             float time)
{
  size_t slew_index = slew_axis_->findAxisIndex(slew);
  size_t cap_index = cap_axis_->findAxisIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index00 = slew_index * cap_count + cap_index;
  size_t wave_index01 = slew_index * cap_count + (cap_index + 1);
  size_t wave_index10 = (slew_index + 1) * cap_count + cap_index;
  size_t wave_index11 = (slew_index + 1) * cap_count + (cap_index + 1);

  size_t index1 = slew_index;
  size_t index2 = cap_index;
  double x1 = slew;
  double x2 = cap;
  double x1l = slew_axis_->axisValue(index1);
  double x1u = slew_axis_->axisValue(index1 + 1);
  double dx1 = (x1 - x1l) / (x1u - x1l);
  double x2l = cap_axis_->axisValue(index2);
  double x2u = cap_axis_->axisValue(index2 + 1);
  double dx2 = (x2 - x2l) / (x2u - x2l);

  double v_lo = 0.0;
  double v_hi = vdd_;
  double v_mid = (v_hi + v_lo) * 0.5;
  double time_mid;
  while (v_hi - v_lo > .001) {
    time_mid = voltageTime1(v_mid, dx1, dx2, wave_index00, wave_index01,
                            wave_index10, wave_index11);
    if (time > time_mid) {
      v_lo = v_mid;
      v_mid = (v_hi + v_lo) * 0.5;
    }
    else {
      v_hi = v_mid;
      v_mid = (v_hi + v_lo) * 0.5;
    }
  }
  return v_mid;
}

double
OutputWaveforms::voltageTime1(double volt,
                              double dx1,
                              double dx2,
                              size_t wave_index00,
                              size_t wave_index01,
                              size_t wave_index10,
                              size_t wave_index11)
{
  double y00 = voltageTime2(volt, wave_index00);
  double y01 = voltageTime2(volt, wave_index01);
  double y10 = voltageTime2(volt, wave_index10);
  double y11 = voltageTime2(volt, wave_index11);
  double time
    =   (1 - dx1) * (1 - dx2) * y00
      +      dx1  * (1 - dx2) * y10
      +      dx1  *      dx2  * y11
      + (1 - dx1) *      dx2  * y01;
  return time;
}

float
OutputWaveforms::voltageTime2(float volt,
                              size_t wave_index)
{
  const Table1 *voltage_waveform = voltage_waveforms_[wave_index];
  const FloatSeq *voltages = voltage_waveform->values();
  size_t index1 = findValueIndex(volt, voltages);
  float volt_lo = (*voltages)[index1];
  float volt_hi = (*voltages)[index1 + 1];
  float dv = volt_hi - volt_lo;

  const TableAxis *time_axis = voltage_waveform->axis1();
  float time_lo = time_axis->axisValue(index1);
  float time_hi = time_axis->axisValue(index1 + 1);
  float dt = time_hi - time_lo;
  return time_lo + dt * (volt - volt_lo) / dv;
}

float
OutputWaveforms::voltageCurrent(float slew,
                                float cap,
                                float volt)
{
  return waveformValue(slew, cap, volt, voltage_currents_);
}

float
OutputWaveforms::waveformValue(float slew,
                               float cap,
                               float axis_value,
                               Table1Seq &waveforms)
{
  size_t slew_index = slew_axis_->findAxisIndex(slew);
  size_t cap_index = cap_axis_->findAxisIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index00 = slew_index * cap_count + cap_index;
  size_t wave_index01 = slew_index * cap_count + (cap_index + 1);
  size_t wave_index10 = (slew_index + 1) * cap_count + cap_index;
  size_t wave_index11 = (slew_index + 1) * cap_count + (cap_index + 1);

  const Table1 *waveform00 = waveforms[wave_index00];
  const Table1 *waveform01 = waveforms[wave_index01];
  const Table1 *waveform10 = waveforms[wave_index10];
  const Table1 *waveform11 = waveforms[wave_index11];

  // Interpolate waveform samples at voltage steps.
  size_t index1 = slew_index;
  size_t index2 = cap_index;
  double x1 = slew;
  double x2 = cap;
  double x1l = slew_axis_->axisValue(index1);
  double x1u = slew_axis_->axisValue(index1 + 1);
  double dx1 = (x1 - x1l) / (x1u - x1l);
  double x2l = cap_axis_->axisValue(index2);
  double x2u = cap_axis_->axisValue(index2 + 1);
  double dx2 = (x2 - x2l) / (x2u - x2l);

  double y00 = waveform00->findValueClip(axis_value);
  double y01 = waveform01->findValueClip(axis_value);
  double y10 = waveform10->findValueClip(axis_value);
  double y11 = waveform11->findValueClip(axis_value);
  double wave_value
    =   (1 - dx1) * (1 - dx2) * y00
      +      dx1  * (1 - dx2) * y10
      +      dx1  *      dx2  * y11
      + (1 - dx1) *      dx2  * y01;
  return wave_value;
}

float
OutputWaveforms::referenceTime(float slew)
{
  return ref_times_->findValue(slew);
}

Table1
OutputWaveforms::voltageWaveform(float slew,
                                 float cap)
{
  FloatSeq *times = new FloatSeq;
  FloatSeq *volts = new FloatSeq;
  for (size_t i = 0; i <= voltage_waveform_step_count_; i++) {
    float volt = i * vdd_ / voltage_waveform_step_count_;
    float time = voltageTime(slew, cap, volt);
    times->push_back(time);
    volts->push_back(volt);
  }
  TableAxisPtr time_axis = make_shared<TableAxis>(TableAxisVariable::time, times);
  return Table1(volts, time_axis);
}

const Table1 *
OutputWaveforms::voltageWaveformRaw(float slew,
                                    float cap)
{
  size_t slew_index = slew_axis_->findAxisClosestIndex(slew);
  size_t cap_index = cap_axis_->findAxisClosestIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index = slew_index * cap_count + cap_index;
  return voltage_waveforms_[wave_index];
}

float
OutputWaveforms::voltageTime(float slew,
                              float cap,
                              float volt)
{
  size_t slew_index = slew_axis_->findAxisIndex(slew);
  size_t cap_index = cap_axis_->findAxisIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index00 = slew_index * cap_count + cap_index;
  size_t wave_index01 = slew_index * cap_count + (cap_index + 1);
  size_t wave_index10 = (slew_index + 1) * cap_count + cap_index;
  size_t wave_index11 = (slew_index + 1) * cap_count + (cap_index + 1);

  // Interpolate waveform samples at voltage steps.
  size_t index1 = slew_index;
  size_t index2 = cap_index;
  double x1 = slew;
  double x2 = cap;
  double x1l = slew_axis_->axisValue(index1);
  double x1u = slew_axis_->axisValue(index1 + 1);
  double dx1 = (x1 - x1l) / (x1u - x1l);
  double x2l = cap_axis_->axisValue(index2);
  double x2u = cap_axis_->axisValue(index2 + 1);
  double dx2 = (x2 - x2l) / (x2u - x2l);

  double time = voltageTime1(volt, dx1, dx2, wave_index00, wave_index01,
                             wave_index10, wave_index11);
  return time;
}

float
OutputWaveforms::beginTime(float slew,
                           float cap)
{
  return beginEndTime(slew, cap, true);
}

float
OutputWaveforms::endTime(float slew,
                         float cap)
{
  return beginEndTime(slew, cap, false);
}

float
OutputWaveforms::beginEndTime(float slew,
                              float cap,
                              bool begin)
{
  size_t slew_index = slew_axis_->findAxisIndex(slew);
  size_t cap_index = cap_axis_->findAxisIndex(cap);
  size_t cap_count = cap_axis_->size();
  size_t wave_index00 = slew_index * cap_count + cap_index;
  size_t wave_index01 = slew_index * cap_count + (cap_index + 1);
  size_t wave_index10 = (slew_index + 1) * cap_count + cap_index;
  size_t wave_index11 = (slew_index + 1) * cap_count + (cap_index + 1);

  const Table1 *waveform00 = current_waveforms_[wave_index00];
  const Table1 *waveform01 = current_waveforms_[wave_index01];
  const Table1 *waveform10 = current_waveforms_[wave_index10];
  const Table1 *waveform11 = current_waveforms_[wave_index11];

  // Interpolate waveform samples at voltage steps.
  size_t index1 = slew_index;
  size_t index2 = cap_index;
  float x1 = slew;
  float x2 = cap;
  float x1l = slew_axis_->axisValue(index1);
  float x1u = slew_axis_->axisValue(index1 + 1);
  float dx1 = (x1 - x1l) / (x1u - x1l);
  float x2l = cap_axis_->axisValue(index2);
  float x2u = cap_axis_->axisValue(index2 + 1);
  float dx2 = (x2 - x2l) / (x2u - x2l);

  float y00, y01, y10, y11;
  if (begin) {
    y00 = waveform00->axis1()->min();
    y01 = waveform01->axis1()->min();
    y10 = waveform10->axis1()->min();
    y11 = waveform11->axis1()->min();
  }
  else {
    y00 = waveform00->axis1()->max();
    y01 = waveform01->axis1()->max();
    y10 = waveform10->axis1()->max();
    y11 = waveform11->axis1()->max();
  }

  float wave_value
    =   (1 - dx1) * (1 - dx2) * y00
      +      dx1  * (1 - dx2) * y10
      +      dx1  *      dx2  * y11
      + (1 - dx1) *      dx2  * y01;
  return wave_value;
}

Table1
OutputWaveforms::voltageCurrentWaveform(float slew,
                                        float cap)
{
  FloatSeq *volts = new FloatSeq;
  FloatSeq *currents = new FloatSeq;
  for (size_t i = 0; i < voltage_waveform_step_count_; i++) {
    float volt = i * vdd_ / voltage_waveform_step_count_;
    float current = voltageCurrent(slew, cap, volt);
    volts->push_back(volt);
    currents->push_back(current);
  }
  TableAxisPtr volt_axis =
    make_shared<TableAxis>(TableAxisVariable::input_voltage, volts);
  return Table1(currents, volt_axis);
}

// Incremental resistance at final value of waveform.
// This corresponds to the pulldown/pullup that holds the output to the rail
// after the waveform has transitioned to the final value.
float
OutputWaveforms::finalResistance()
{
  size_t slew_index = 0;
  size_t cap_count = cap_axis_->size();
  size_t cap_index = cap_count - 1;
  size_t wave_index = slew_index * cap_count + cap_index;
  const Table1 *voltage_currents = voltage_currents_[wave_index];
  FloatSeq *voltages = voltage_currents->axis1()->values();
  FloatSeq *currents = voltage_currents->values();
  size_t idx_last1 = voltages->size() - 2;
  return (vdd_ - (*voltages)[idx_last1]) / abs((*currents)[idx_last1]);
}

////////////////////////////////////////////////////////////////

DriverWaveform::DriverWaveform(const string &name,
                               TablePtr waveforms) :
  name_(name),
  waveforms_(waveforms)
{
}

Table1
DriverWaveform::waveform(float slew)
{
  const TableAxis *volt_axis = waveforms_->axis2();
  FloatSeq *time_values = new FloatSeq;
  FloatSeq *volt_values = new FloatSeq;
  for (float volt : *volt_axis->values()) {
    float time = waveforms_->findValue(slew, volt, 0.0);
    time_values->push_back(time);
    volt_values->push_back(volt);
  }
  TableAxisPtr time_axis = make_shared<TableAxis>(TableAxisVariable::time,
                                                  time_values);
  Table1 waveform(volt_values, time_axis);
  return waveform;
}

} // namespace
