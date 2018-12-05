// OpenSTA, Static Timing Analyzer
// Copyright (c) 2018, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Machine.hh"
#include "Report.hh"
#include "Error.hh"
#include "Units.hh"
#include "Liberty.hh"
#include "TableModel.hh"

namespace sta {

static void
reportPvt(const LibertyLibrary *library,
	  const Pvt *pvt,
	  int digits,
	  string *result);
static void
appendSpaces(string *result,
	     int count);

GateTableModel::GateTableModel(TableModel *delay_model,
			       TableModel *delay_sigma_models[EarlyLate::index_count],
			       TableModel *slew_model,
			       TableModel *slew_sigma_models[EarlyLate::index_count]) :
  delay_model_(delay_model),
  slew_model_(slew_model)
{
  MinMaxIterator el_iter;
  while (el_iter.hasNext()) {
    EarlyLate *early_late = el_iter.next();
    int el_index = early_late->index();
    slew_sigma_models_[el_index] = slew_sigma_models[el_index];
    delay_sigma_models_[el_index] = delay_sigma_models[el_index];
  }
}

GateTableModel::~GateTableModel()
{
  delete delay_model_;
  delete slew_model_;
  MinMaxIterator el_iter;
  while (el_iter.hasNext()) {
    EarlyLate *early_late = el_iter.next();
    int el_index = early_late->index();
    delete slew_sigma_models_[el_index];
    delete delay_sigma_models_[el_index];
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
GateTableModel::gateDelay(const LibertyCell *cell,
			  const Pvt *pvt,
			  float in_slew,
			  float load_cap,
			  float related_out_cap,
			  // return values
			  ArcDelay &gate_delay,
			  Slew &drvr_slew) const
{
  const LibertyLibrary *library = cell->libertyLibrary();
  float delay = findValue(library, cell, pvt, delay_model_, in_slew,
			  load_cap, related_out_cap);
  float sigma_early = 0.0;
  float sigma_late = 0.0;
  if (delay_sigma_models_[EarlyLate::earlyIndex()])
    sigma_early = findValue(library, cell, pvt,
			    delay_sigma_models_[EarlyLate::earlyIndex()],
			    in_slew, load_cap, related_out_cap);
  if (delay_sigma_models_[EarlyLate::lateIndex()])
    sigma_late = findValue(library, cell, pvt,
			   delay_sigma_models_[EarlyLate::earlyIndex()],
			   in_slew, load_cap, related_out_cap);
  gate_delay = makeDelay(delay, sigma_early, sigma_late);

  float slew = findValue(library, cell, pvt, slew_model_, in_slew,
			 load_cap, related_out_cap);
  if (slew_sigma_models_[EarlyLate::earlyIndex()])
    sigma_early = findValue(library, cell, pvt,
			    slew_sigma_models_[EarlyLate::earlyIndex()],
			    in_slew, load_cap, related_out_cap);
  if (slew_sigma_models_[EarlyLate::lateIndex()])
    sigma_late = findValue(library, cell, pvt,
			   slew_sigma_models_[EarlyLate::earlyIndex()],
			   in_slew, load_cap, related_out_cap);
  sigma_early = 0.0;
  sigma_late = 0.0;
  // Clip negative slews to zero.
  if (slew < 0.0)
    slew = 0.0;
  drvr_slew = makeDelay(slew, sigma_early, sigma_late);
}

void
GateTableModel::reportGateDelay(const LibertyCell *cell,
				const Pvt *pvt,
				float in_slew,
				float load_cap,
				float related_out_cap,
				int digits,
				string *result) const
{
  const LibertyLibrary *library = cell->libertyLibrary();
  reportPvt(library, pvt, digits, result);
  reportTableLookup("Delay", library, cell, pvt, delay_model_, in_slew,
		    load_cap, related_out_cap, digits, result);
  if (delay_sigma_models_[EarlyLate::earlyIndex()])
    reportTableLookup("Delay sigma(early)", library, cell, pvt,
		      delay_sigma_models_[EarlyLate::earlyIndex()],
		      in_slew, load_cap, related_out_cap, digits, result);
  if (delay_sigma_models_[EarlyLate::lateIndex()])
    reportTableLookup("Delay sigma(late)", library, cell, pvt,
		      delay_sigma_models_[EarlyLate::lateIndex()],
		      in_slew, load_cap, related_out_cap, digits, result);
  *result += '\n';
  reportTableLookup("Slew", library, cell, pvt, slew_model_, in_slew,
		    load_cap, related_out_cap, digits, result);
  if (slew_sigma_models_[EarlyLate::earlyIndex()])
    reportTableLookup("Slew sigma(early)", library, cell, pvt,
		      slew_sigma_models_[EarlyLate::earlyIndex()],
		      in_slew, load_cap, related_out_cap, digits, result);
  if (slew_sigma_models_[EarlyLate::lateIndex()])
    reportTableLookup("Slew sigma(late)", library, cell, pvt,
		      slew_sigma_models_[EarlyLate::lateIndex()],
		      in_slew, load_cap, related_out_cap, digits, result);
  float drvr_slew = findValue(library, cell, pvt, slew_model_, in_slew,
			      load_cap, related_out_cap);
  if (drvr_slew < 0.0)
    *result += "Negative slew clipped to 0.0\n";
}

void
GateTableModel::reportTableLookup(const char *result_name,
				  const LibertyLibrary *library,
				  const LibertyCell *cell,
				  const Pvt *pvt,
				  const TableModel *model,
				  float in_slew,
				  float load_cap,
				  float related_out_cap,
				  int digits,
				  string *result) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(model, in_slew, load_cap, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    model->reportValue(result_name, library, cell, pvt,
		       axis_value1, NULL, axis_value2, axis_value3,
		       digits, result);
  }
}

float
GateTableModel::findValue(const LibertyLibrary *library,
			  const LibertyCell *cell,
			  const Pvt *pvt,
			  const TableModel *model,
			  float in_slew,
			  float load_cap,
			  float related_out_cap) const
{
  if (model) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(model, in_slew, load_cap, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    return model->findValue(library, cell, pvt,
			    axis_value1, axis_value2, axis_value3);
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
    internalError("unsupported table order");
  }
}

// Use slew/Cload for the highest Cload, which approximates output
// admittance as the "drive".
float
GateTableModel::driveResistance(const LibertyCell *cell,
				const Pvt *pvt) const
{
  float slew, cap;
  maxCapSlew(cell, 0.0, pvt, slew, cap);
  return slew / cap;
}

void
GateTableModel::maxCapSlew(const LibertyCell *cell,
			   float in_slew,
			   const Pvt *pvt,
			   float &slew,
			   float &cap) const
{
  const LibertyLibrary *library = cell->libertyLibrary();
  TableAxis *axis1 = slew_model_->axis1();
  TableAxis *axis2 = slew_model_->axis2();
  TableAxis *axis3 = slew_model_->axis3();
  if (axis1
      && axis1->variable() == table_axis_total_output_net_capacitance) {
    cap = axis1->axisValue(axis1->size() - 1);
    slew = findValue(library, cell, pvt, slew_model_,
		     in_slew, cap, 0.0);
  }
  else if (axis2
	   && axis2->variable()==table_axis_total_output_net_capacitance) {
    cap = axis2->axisValue(axis2->size() - 1);
    slew = findValue(library, cell, pvt, slew_model_,
		     in_slew, cap, 0.0);
  }
  else if (axis3
	   && axis3->variable()==table_axis_total_output_net_capacitance) {
    cap = axis3->axisValue(axis3->size() - 1);
    slew = findValue(library, cell, pvt, slew_model_,
		     in_slew, cap, 0.0);
  }
  else {
    // Table not dependent on capacitance.
    cap = 0.0;
    slew = 1.0;
  }
  // Clip negative slews to zero.
  if (slew < 0.0)
    slew = 0.0;
}

float
GateTableModel::axisValue(TableAxis *axis,
			  float in_slew,
			  float load_cap,
			  float related_out_cap) const
{
  TableAxisVariable var = axis->variable();
  if (var == table_axis_input_transition_time
      || var == table_axis_input_net_transition)
    return in_slew;
  else if (var == table_axis_total_output_net_capacitance)
    return load_cap;
  else if (var == table_axis_related_out_total_output_net_capacitance)
    return related_out_cap;
  else {
    internalError("unsupported table axes");
    return 0.0;
  }
}

bool
GateTableModel::checkAxes(const Table *table)
{
  TableAxis *axis1 = table->axis1();
  TableAxis *axis2 = table->axis2();
  TableAxis *axis3 = table->axis3();
  bool axis_ok = true;
  if (axis1)
    axis_ok &= checkAxis(table->axis1());
  if (axis2)
    axis_ok &= checkAxis(table->axis2());
  if (axis3)
    axis_ok &= checkAxis(table->axis3());
  return axis_ok;
}

bool
GateTableModel::checkAxis(TableAxis *axis)
{
  TableAxisVariable var = axis->variable();
  return var == table_axis_total_output_net_capacitance
    || var == table_axis_input_transition_time
    || var == table_axis_input_net_transition
    || var == table_axis_related_out_total_output_net_capacitance;
}

////////////////////////////////////////////////////////////////

CheckTableModel::CheckTableModel(TableModel *model) :
  model_(model)
{
}

CheckTableModel::~CheckTableModel()
{
  delete model_;
}

void
CheckTableModel::setIsScaled(bool is_scaled)
{
  model_->setIsScaled(is_scaled);
}

void
CheckTableModel::checkDelay(const LibertyCell *cell,
			    const Pvt *pvt,
			    float from_slew,
			    float to_slew,
			    float related_out_cap,
			    // Return values.
			    ArcDelay &margin) const
{
  if (model_) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(from_slew, to_slew, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    const LibertyLibrary *library = cell->libertyLibrary();
    margin = model_->findValue(library, cell, pvt,
			       axis_value1, axis_value2, axis_value3);
  }
  else
    margin = 0.0;
}

void
CheckTableModel::reportCheckDelay(const LibertyCell *cell,
				  const Pvt *pvt,
				  float from_slew,
				  const char *from_slew_annotation,
				  float to_slew,
				  float related_out_cap,
				  int digits,
				  string *result) const
{
  if (model_) {
    float axis_value1, axis_value2, axis_value3;
    findAxisValues(from_slew, to_slew, related_out_cap,
		   axis_value1, axis_value2, axis_value3);
    const LibertyLibrary *library = cell->libertyLibrary();
    reportPvt(library, pvt, digits, result);
    model_->reportValue("Check", library, cell, pvt,
			axis_value1, from_slew_annotation, axis_value2,
			axis_value3, digits, result);
  }
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
    internalError("unsupported table order");
  }
}

float
CheckTableModel::axisValue(TableAxis *axis,
			   float from_slew,
			   float to_slew,
			   float related_out_cap) const
{
  TableAxisVariable var = axis->variable();
  if (var == table_axis_related_pin_transition)
    return from_slew;
  else if (var == table_axis_constrained_pin_transition)
    return to_slew;
  else if (var == table_axis_related_out_total_output_net_capacitance)
    return related_out_cap;
  else {
    internalError("unsupported table axes");
    return 0.0;
  }
}

bool
CheckTableModel::checkAxes(const Table *table)
{
  TableAxis *axis1 = table->axis1();
  TableAxis *axis2 = table->axis2();
  TableAxis *axis3 = table->axis3();
  bool axis_ok = true;
  if (axis1)
    axis_ok &= checkAxis(table->axis1());
  if (axis2)
    axis_ok &= checkAxis(table->axis2());
  if (axis3)
    axis_ok &= checkAxis(table->axis3());
  return axis_ok;
}

bool
CheckTableModel::checkAxis(TableAxis *axis)
{
  TableAxisVariable var = axis->variable();
  return var == table_axis_constrained_pin_transition
    || var == table_axis_related_pin_transition
    || var == table_axis_related_out_total_output_net_capacitance;
}

////////////////////////////////////////////////////////////////

TableModel::TableModel(Table *table,
		       ScaleFactorType scale_factor_type,
		       TransRiseFall *tr) :
  table_(table),
  scale_factor_type_(scale_factor_type),
  tr_index_(tr->index()),
  is_scaled_(false)
{
}

TableModel::~TableModel()
{
  delete table_;
}

int
TableModel::order() const
{
  return table_->order();
}

void
TableModel::setScaleFactorType(ScaleFactorType type)
{
  scale_factor_type_ = type;
}

void
TableModel::setIsScaled(bool is_scaled)
{
  is_scaled_ = is_scaled;
}

TableAxis *
TableModel::axis1() const
{
  return table_->axis1();
}

TableAxis *
TableModel::axis2() const
{
  return table_->axis2();
}

TableAxis *
TableModel::axis3() const
{
  return table_->axis3();
}

float
TableModel::findValue(const LibertyLibrary *library,
		      const LibertyCell *cell,
		      const Pvt *pvt,
		      float value1,
		      float value2,
		      float value3) const
{
  return table_->findValue(value1, value2, value3)
    * scaleFactor(library, cell, pvt);
}

float
TableModel::scaleFactor(const LibertyLibrary *library,
			const LibertyCell *cell,
			const Pvt *pvt) const
{
  if (is_scaled_)
    // Scaled tables are not derated because scale factors are wrt
    // nominal pvt.
    return 1.0F;
  else {
    ScaleFactorType type = static_cast<ScaleFactorType>(scale_factor_type_);
    return library->scaleFactor(type, tr_index_, cell, pvt);
  }
}

void
TableModel::reportValue(const char *result_name,
			const LibertyLibrary *library,
			const LibertyCell *cell,
			const Pvt *pvt,
			float value1,
			const char *comment1,
			float value2,
			float value3,
			int digits,
			string *result) const
{
  table_->reportValue("Table value", library, cell, pvt, value1,
		      comment1, value2, value3, digits, result);

  reportPvtScaleFactor(library, cell, pvt, digits, result);

  const Units *units = library->units();
  const Unit *table_unit = units->timeUnit();
  *result += result_name;
  *result += " = ";
  *result += table_unit->asString(findValue(library, cell, pvt,
					    value1, value2, value3), digits);
  *result += '\n';
}

static void
reportPvt(const LibertyLibrary *library,
	  const Pvt *pvt,
	  int digits,
	  string *result)
{
  if (pvt == NULL)
    pvt = library->defaultOperatingConditions();
  if (pvt) {
    const char *pvt_str = stringPrint(strlen("P = %.*f V = %.*f T = %.*f\n")
				      + (digits + 10) * 3 + 1,
				      "P = %.*f V = %.*f T = %.*f\n",
				      digits, pvt->process(),
				      digits, pvt->voltage(),
				      digits, pvt->temperature());
    *result += pvt_str;
    stringDelete(pvt_str);
  }
}

void
TableModel::reportPvtScaleFactor(const LibertyLibrary *library,
				 const LibertyCell *cell,
				 const Pvt *pvt,
				 int digits,
				 string *result) const
{
  if (pvt == NULL)
    pvt = library->defaultOperatingConditions();
  if (pvt) {
    const char *scale_str = stringPrint(strlen("PVT scale factor = %.*f\n")
					+ digits + 10 + 1,
					"PVT scale factor = %.*f\n",
					digits,
					scaleFactor(library, cell, pvt));
    *result += scale_str;
    stringDelete(scale_str);
  }
}

////////////////////////////////////////////////////////////////

Table0::Table0(float value) :
  Table(),
  value_(value)
{
}

float
Table0::findValue(float,
		  float,
		  float) const
{
  return value_;
}

void
Table0::reportValue(const char *result_name,
		    const LibertyLibrary *library,
		    const LibertyCell *,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
		    int digits,
		    string *result) const
{
  const Units *units = library->units();
  const Unit *table_unit = units->timeUnit();
  *result += result_name;
  *result += " constant = ";
  *result += table_unit->asString(findValue(value1, value2, value3), digits);
  if (comment1)
    *result += comment1;
  *result += '\n';
}

void
Table0::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  report->print("%s\n", table_unit->asString(value_, digits));
}

////////////////////////////////////////////////////////////////

Table1::Table1(FloatSeq *values,
	       TableAxis *axis1,
	       bool own_axis1) :
  Table(),
  values_(values),
  axis1_(axis1),
  own_axis1_(own_axis1)
{
}

Table1::~Table1()
{
  delete values_;
  if (own_axis1_)
    delete axis1_;
}

float
Table1::tableValue(size_t index1) const
{
  return (*values_)[index1];
}

float
Table1::findValue(float value1,
		  float,
		  float) const
{
  if (axis1_->size() == 1)
    return tableValue(value1);
  else {
    size_t index1 = axis1_->findAxisIndex(value1);
    float x1 = value1;
    float x1l = axis1_->axisValue(index1);
    float x1u = axis1_->axisValue(index1 + 1);
    float y1 = tableValue(index1);
    float y2 = tableValue(index1 + 1);
    float dx1 = (x1 - x1l) / (x1u - x1l);
    return (1 - dx1) * y1 + dx1 * y2;
  }
}

void
Table1::reportValue(const char *result_name, const
		    LibertyLibrary *library,
		    const LibertyCell *,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
		    int digits,
		    string *result) const
{
  TableAxisVariable var1 = axis1_->variable();
  const Units *units = library->units();
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = tableVariableUnit(var1, units);
  *result += "Table is indexed by\n";
  *result += "  ";
  *result += tableVariableString(var1);
  *result += " = ";
  *result += unit1->asString(value1, digits);
  if (comment1)
    *result += comment1;
  *result += '\n';

  if (axis1_->size() != 1) {
    size_t index1 = axis1_->findAxisIndex(value1);
    *result += "  ";
    *result += unit1->asString(axis1_->axisValue(index1), digits);
    *result += "      ";
    *result += unit1->asString(axis1_->axisValue(index1 + 1), digits);
    *result += '\n';

    *result += "    --------------------\n";

    *result += "| ";
    *result += table_unit->asString(tableValue(index1), digits);
    *result += "     ";
    *result += table_unit->asString(tableValue(index1 + 1),
				    digits);
    *result += '\n';
  }

  *result += result_name;
  *result += " = ";
  *result += table_unit->asString(findValue(value1, value2, value3), digits);
  *result += '\n';
}

void
Table1::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *unit1 = tableVariableUnit(axis1_->variable(), units);
  const Unit *table_unit = units->timeUnit();
  report->print("%s\n", tableVariableString(axis1_->variable()));
  report->print("------------------------------\n");
  for (size_t index1 = 0; index1 < axis1_->size(); index1++)
    report->print("%s ", unit1->asString(axis1_->axisValue(index1), digits));
  report->print("\n");

  for (size_t index1 = 0; index1 < axis1_->size(); index1++)
    report->print("%s ", table_unit->asString(tableValue(index1),digits));
  report->print("\n");
}

////////////////////////////////////////////////////////////////

Table2::Table2(FloatTable *values,
	       TableAxis *axis1,
	       bool own_axis1,
	       TableAxis *axis2,
	       bool own_axis2) :
  Table(),
  values_(values),
  axis1_(axis1),
  own_axis1_(own_axis1),
  axis2_(axis2),
  own_axis2_(own_axis2)
{
}

Table2::~Table2()
{
  values_->deleteContents();
  delete values_;
  if (own_axis1_)
    delete axis1_;
  if (own_axis2_)
    delete axis2_;
}

float
Table2::tableValue(size_t index1,
		   size_t index2) const
{
  FloatSeq *row = (*values_)[index1];
  return (*row)[index2];
}

// Bilinear Interpolation.
float
Table2::findValue(float value1,
		  float value2,
		  float) const
{
  size_t size1 = axis1_->size();
  size_t size2 = axis2_->size();
  if (size1 == 1) {
    if (size2 == 1)
      return tableValue(0, 0);
    else {
      size_t index2 = axis2_->findAxisIndex(value2);
      float x2 = value2;
      float y00 = tableValue(0, index2);
      float x2l = axis2_->axisValue(index2);
      float x2u = axis2_->axisValue(index2 + 1);
      float dx2 = (x2 - x2l) / (x2u - x2l);
      float y01 = tableValue(0, index2 + 1);
      float tbl_value
	= (1 - dx2) * y00
	+      dx2  * y01;
      return tbl_value;
    }
  }
  else if (size2 == 1) {
    size_t index1 = axis1_->findAxisIndex(value1);
    float x1 = value1;
    float y00 = tableValue(index1, 0);
    float x1l = axis1_->axisValue(index1);
    float x1u = axis1_->axisValue(index1 + 1);
    float dx1 = (x1 - x1l) / (x1u - x1l);
    float y10 = tableValue(index1 + 1, 0);
    float tbl_value
      = (1 - dx1) * y00
      +      dx1  * y10;
    return tbl_value;
  }
  else {
    size_t index1 = axis1_->findAxisIndex(value1);
    size_t index2 = axis2_->findAxisIndex(value2);
    float x1 = value1;
    float x2 = value2;
    float y00 = tableValue(index1, index2);
    float x1l = axis1_->axisValue(index1);
    float x1u = axis1_->axisValue(index1 + 1);
    float dx1 = (x1 - x1l) / (x1u - x1l);
    float y10 = tableValue(index1 + 1, index2);
    float y11 = tableValue(index1 + 1, index2 + 1);
    float x2l = axis2_->axisValue(index2);
    float x2u = axis2_->axisValue(index2 + 1);
    float dx2 = (x2 - x2l) / (x2u - x2l);
    float y01 = tableValue(index1, index2 + 1);
    float tbl_value
      = (1 - dx1) * (1 - dx2) * y00
      +      dx1  * (1 - dx2) * y10
      +      dx1  *      dx2  * y11
      + (1 - dx1) *      dx2  * y01;
    return tbl_value;
  }
}

void
Table2::reportValue(const char *result_name,
		    const LibertyLibrary *library,
		    const LibertyCell *,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
		    int digits,
		    string *result) const
{
  TableAxisVariable var1 = axis1_->variable();
  TableAxisVariable var2 = axis2_->variable();
  const Units *units = library->units();
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = tableVariableUnit(var1, units);
  const Unit *unit2 = tableVariableUnit(var2, units);
  *result += "------- ";
  *result += tableVariableString(var1),
  *result += " = ";
  *result += unit1->asString(value1, digits);
  if (comment1)
    *result += comment1;
  *result += '\n';

  *result += "|       ";
  *result += tableVariableString(var2);
  *result += " = ";
  *result += unit2->asString(value2, digits);
  *result += '\n';

  size_t index1 = axis1_->findAxisIndex(value1);
  size_t index2 = axis2_->findAxisIndex(value2);
  *result += "|        ";
  *result += unit2->asString(axis2_->axisValue(index2), digits);
  if (axis2_->size() != 1) {
    *result += "     ";
    *result += unit2->asString(axis2_->axisValue(index2 + 1), digits);
  }
  *result += '\n';

  *result += "v      --------------------\n";
  *result += unit1->asString(axis1_->axisValue(index1), digits);
  *result += " | ";

  *result += table_unit->asString(tableValue(index1, index2), digits);
  if (axis2_->size() != 1) {
    *result += "     ";
    *result += table_unit->asString(tableValue(index1, index2 + 1), digits);
  }
  *result += '\n';

  if (axis1_->size() != 1) {
    *result += unit1->asString(axis1_->axisValue(index1 + 1), digits);
    *result += " | ";
    *result += table_unit->asString(tableValue(index1 + 1, index2), digits);
    if (axis2_->size() != 1) {
      *result += "     ";
      *result +=table_unit->asString(tableValue(index1 + 1, index2 + 1),digits);
    }
  }
  *result += '\n';

  *result += result_name;
  *result += " = ";
  *result += table_unit->asString(findValue(value1, value2, value3), digits);
  *result += '\n';
}

void
Table2::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = tableVariableUnit(axis1_->variable(), units);
  const Unit *unit2 = tableVariableUnit(axis2_->variable(), units);
  report->print("%s\n", tableVariableString(axis2_->variable()));
  report->print("     ------------------------------\n");
  report->print("     ");
  for (size_t index2 = 0; index2 < axis2_->size(); index2++)
    report->print("%s ", unit2->asString(axis2_->axisValue(index2), digits));
  report->print("\n");

  for (size_t index1 = 0; index1 < axis1_->size(); index1++) {
    report->print("%s |", unit1->asString(axis1_->axisValue(index1), digits));
    for (size_t index2 = 0; index2 < axis2_->size(); index2++)
      report->print("%s ", table_unit->asString(tableValue(index1, index2),
						digits));
    report->print("\n");
  }
}

////////////////////////////////////////////////////////////////

Table3::Table3(FloatTable *values,
	       TableAxis *axis1,
	       bool own_axis1,
	       TableAxis *axis2,
	       bool own_axis2,
	       TableAxis *axis3,
	       bool own_axis3) :
  Table2(values, axis1, own_axis1, axis2, own_axis2),
  axis3_(axis3),
  own_axis3_(own_axis3)
{
}

Table3::~Table3()
{
  if (own_axis3_)
    delete axis3_;
}

float
Table3::tableValue(size_t index1,
		   size_t index2,
		   size_t index3) const
{
  size_t row = index1 * axis2_->size() + index2;
  return values_->operator[](row)->operator[](index3);
}

// Bilinear Interpolation.
float
Table3::findValue(float value1,
		  float value2,
		  float value3) const
{
  size_t index1 = axis1_->findAxisIndex(value1);
  size_t index2 = axis2_->findAxisIndex(value2);
  size_t index3 = axis3_->findAxisIndex(value3);
  float x1 = value1;
  float x2 = value2;
  float x3 = value3;
  float dx1 = 0.0;
  float dx2 = 0.0;
  float dx3 = 0.0;
  float y000 = tableValue(index1, index2, index3);
  float y001 = 0.0;
  float y010 = 0.0;
  float y011 = 0.0;
  float y100 = 0.0;
  float y101 = 0.0;
  float y110 = 0.0;
  float y111 = 0.0;

  if (axis1_->size() != 1) {
    float x1l = axis1_->axisValue(index1);
    float x1u = axis1_->axisValue(index1 + 1);
    dx1 = (x1 - x1l) / (x1u - x1l);
    y100 = tableValue(index1 + 1, index2, index3);
    if (axis3_->size() != 1)
      y101 = tableValue(index1 + 1, index2, index3 + 1);
    if (axis2_->size() != 1) {
      y110 = tableValue(index1 + 1, index2 + 1, index3);
      if (axis3_->size() != 1)
	y111 = tableValue(index1 + 1, index2 + 1, index3 + 1);
    }
  }
  if (axis2_->size() != 1) {
    float x2l = axis2_->axisValue(index2);
    float x2u = axis2_->axisValue(index2 + 1);
    dx2 = (x2 - x2l) / (x2u - x2l);
    y010 = tableValue(index1, index2 + 1, index3);
    if (axis3_->size() != 1)
      y011 = tableValue(index1, index2 + 1, index3 + 1);
  }
  if (axis3_->size() != 1) {
    float x3l = axis3_->axisValue(index3);
    float x3u = axis3_->axisValue(index3 + 1);
    dx3 = (x3 - x3l) / (x3u - x3l);
    y001 = tableValue(index1, index2, index3 + 1);
  }

  float tbl_value
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
void
Table3::reportValue(const char *result_name,
		    const LibertyLibrary *library,
		    const LibertyCell *,
		    const Pvt *,
		    float value1,
		    const char *comment1,
		    float value2,
		    float value3,
		    int digits,
		    string *result) const
{
  TableAxisVariable var1 = axis1_->variable();
  TableAxisVariable var2 = axis2_->variable();
  TableAxisVariable var3 = axis3_->variable();
  const Units *units = library->units();
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = tableVariableUnit(var1, units);
  const Unit *unit2 = tableVariableUnit(var2, units);
  const Unit *unit3 = tableVariableUnit(var3, units);

  *result += "   --------- ";
  *result += tableVariableString(var1),
  *result += " = ";
  *result += unit1->asString(value1, digits);
  if (comment1)
    *result += comment1;
  *result += '\n';

  *result += "   |    ---- ";
  *result += tableVariableString(var2),
  *result += " = ";
  *result += unit2->asString(value2, digits);
  *result += '\n';

  *result += "   |    |    ";
  *result += tableVariableString(var3);
  *result += " = ";
  *result += unit3->asString(value3, digits);
  *result += '\n';

  size_t index1 = axis1_->findAxisIndex(value1);
  size_t index2 = axis2_->findAxisIndex(value2);
  size_t index3 = axis3_->findAxisIndex(value3);

  *result += "   |    |    ";
  *result += unit3->asString(axis3_->axisValue(index3), digits);
  if (axis3_->size() != 1) {
    *result += "     ";
    *result += unit3->asString(axis3_->axisValue(index3 + 1), digits);
  }
  *result += '\n';

  *result += "   v    |    --------------------\n";

  if (axis1_->size() != 1) {
    *result += " ";
    *result += unit1->asString(axis1_->axisValue(index1+1), digits);
    *result += "   v   / ";
    *result += table_unit->asString(tableValue(index1+1,index2,index3),
				    digits);
    if (axis3_->size() != 1) {
      *result += "     ";
      *result += table_unit->asString(tableValue(index1+1,index2,index3+1),
				      digits);
    }
  }
  else {
    appendSpaces(result, digits+3);
    *result += "   v   / ";
  }
  *result += '\n';

  *result += unit1->asString(axis1_->axisValue(index1), digits);
  *result += "  ";
  *result += unit2->asString(axis2_->axisValue(index2), digits);
  *result += " | ";
  *result += table_unit->asString(tableValue(index1, index2, index3), digits);
  if (axis3_->size() != 1) {
    *result += "     ";
    *result += table_unit->asString(tableValue(index1, index2, index3+1),
				    digits);
  }
  *result += '\n';

  *result += "           |/ ";
  if (axis1_->size() != 1
      && axis2_->size() != 1) {
    *result += table_unit->asString(tableValue(index1+1,index2+1,index3),
				    digits);
    if (axis3_->size() != 1) {
      *result += "     ";
      *result +=table_unit->asString(tableValue(index1+1,index2+1,index3+1),
				     digits);
    }
  }
  *result += '\n';

  *result += "      ";
  *result += unit2->asString(axis2_->axisValue(index2 + 1), digits);
  *result += " | ";
  if (axis2_->size() != 1) {
    *result += table_unit->asString(tableValue(index1, index2+1, index3),
				    digits);
    if (axis3_->size() != 1) {
      *result += "     ";
      *result +=table_unit->asString(tableValue(index1, index2+1,index3+1),
				     digits);
    }
  }
  *result += '\n';

  *result += result_name;
  *result += " = ";
  *result += table_unit->asString(findValue(value1, value2, value3), digits);
  *result += '\n';
}

static void
appendSpaces(string *result,
	     int count)
{
  while (count--)
    *result += ' ';
}

void
Table3::report(const Units *units,
	       Report *report) const
{
  int digits = 4;
  const Unit *table_unit = units->timeUnit();
  const Unit *unit1 = tableVariableUnit(axis1_->variable(), units);
  const Unit *unit2 = tableVariableUnit(axis2_->variable(), units);
  const Unit *unit3 = tableVariableUnit(axis3_->variable(), units);
  for (size_t index1 = 0; index1 < axis1_->size(); index1++) {
    report->print("%s %s\n", tableVariableString(axis1_->variable()),
		  unit1->asString(axis1_->axisValue(index1), digits));

    report->print("%s\n", tableVariableString(axis3_->variable()));
    report->print("     ------------------------------\n");
    report->print("     ");
    for (size_t index3 = 0; index3 < axis3_->size(); index3++)
      report->print("%s ", unit3->asString(axis3_->axisValue(index3), digits));
    report->print("\n");

    for (size_t index2 = 0; index2 < axis2_->size(); index2++) {
      report->print("%s |", unit2->asString(axis2_->axisValue(index2),digits));
      for (size_t index3 = 0; index3 < axis3_->size(); index3++)
	report->print("%s ", table_unit->asString(tableValue(index1, index2,
							     index3),
						  digits));
      report->print("\n");
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

// Bisection search.
size_t
TableAxis::findAxisIndex(float value) const
{
  int max = static_cast<int>(values_->size()) - 1;
  if (value <= (*values_)[0] || max == 0)
    return 0;
  else if (value >= (*values_)[max])
    // Return max-1 for value too large so interpolation pts are index,index+1.
    return max - 1;
  else {
    int lower = -1;
    int upper = max + 1;
    bool ascend = ((*values_)[max] >= (*values_)[0]);
    while (upper - lower > 1) {
      int mid = (upper + lower) >> 1;
      if ((value >= (*values_)[mid]) == ascend)
	lower = mid;
      else
	upper = mid;
    }
    return lower;
  }
}

////////////////////////////////////////////////////////////////

TableAxisVariable
stringTableAxisVariable(const char *variable)
{
  if (stringEq(variable, "total_output_net_capacitance"))
    return table_axis_total_output_net_capacitance;
  if (stringEq(variable, "equal_or_opposite_output_net_capacitance"))
    return table_axis_equal_or_opposite_output_net_capacitance;
  else if (stringEq(variable, "input_net_transition"))
    return table_axis_input_net_transition;
  else if (stringEq(variable, "input_transition_time"))
    return table_axis_input_transition_time;
  else if (stringEq(variable, "related_pin_transition"))
    return table_axis_related_pin_transition;
  else if (stringEq(variable, "constrained_pin_transition"))
    return table_axis_constrained_pin_transition;
  else if (stringEq(variable, "output_pin_transition"))
    return table_axis_output_pin_transition;
  else if (stringEq(variable, "connect_delay"))
    return table_axis_connect_delay;
  else if (stringEq(variable,"related_out_total_output_net_capacitance"))
    return table_axis_related_out_total_output_net_capacitance;
  else if (stringEq(variable, "time"))
    return table_axis_time;
  else if (stringEq(variable, "iv_output_voltage"))
    return table_axis_iv_output_voltage;
  else if (stringEq(variable, "input_noise_width"))
    return table_axis_input_noise_width;
  else if (stringEq(variable, "input_noise_height"))
    return table_axis_input_noise_height;
  else if (stringEq(variable, "input_voltage"))
    return table_axis_input_voltage;
  else if (stringEq(variable, "output_voltage"))
    return table_axis_output_voltage;
  else if (stringEq(variable, "path_depth"))
    return table_axis_path_depth;
  else if (stringEq(variable, "path_distance"))
    return table_axis_path_distance;
  else
    return table_axis_unknown;
}

const char *
tableVariableString(TableAxisVariable variable)
{
  switch (variable) {
  case table_axis_total_output_net_capacitance:
    return "total_output_net_capacitance";
  case table_axis_equal_or_opposite_output_net_capacitance:
    return "equal_or_opposite_output_net_capacitance";
  case table_axis_input_net_transition:
    return "input_net_transition";
  case table_axis_input_transition_time:
    return "input_transition_time";
  case table_axis_related_pin_transition:
    return "related_pin_transition";
  case table_axis_constrained_pin_transition:
    return "constrained_pin_transition";
  case table_axis_output_pin_transition:
    return "output_pin_transition";
  case table_axis_connect_delay:
    return "connect_delay";
  case table_axis_related_out_total_output_net_capacitance:
    return "related_out_total_output_net_capacitance";
  case table_axis_time:
    return "time";
  case table_axis_iv_output_voltage:
    return "iv_output_voltage";
  case table_axis_input_noise_width:
    return "input_noise_width";
  case table_axis_input_noise_height:
    return "input_noise_height";
  default:
    return "unknown";
  }
}

const Unit *
tableVariableUnit(TableAxisVariable variable,
		  const Units *units)
{
  switch (variable) {
  case table_axis_total_output_net_capacitance:
  case table_axis_related_out_total_output_net_capacitance:
  case table_axis_equal_or_opposite_output_net_capacitance:
    return units->capacitanceUnit();
  case table_axis_input_net_transition:
  case table_axis_input_transition_time:
  case table_axis_related_pin_transition:
  case table_axis_constrained_pin_transition:
  case table_axis_output_pin_transition:
  case table_axis_connect_delay:
  case table_axis_time:
  case table_axis_input_noise_height:
    return units->timeUnit();
  case table_axis_input_voltage:
  case table_axis_output_voltage:
  case table_axis_iv_output_voltage:
  case table_axis_input_noise_width:
    return units->voltageUnit();
  case table_axis_path_distance:
    return units->distanceUnit();
  case table_axis_path_depth:
  case table_axis_unknown:
    return units->scalarUnit();
  }
  // Prevent warnings from lame compilers.
  return NULL;
}

} // namespace
