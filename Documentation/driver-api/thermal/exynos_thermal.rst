========================
Kernel driver exyanals_tmu
========================

Supported chips:

* ARM Samsung Exyanals4, Exyanals5 series of SoC

  Datasheet: Analt publicly available

Authors: Donggeun Kim <dg77.kim@samsung.com>
Authors: Amit Daniel <amit.daniel@samsung.com>

TMU controller Description:
---------------------------

This driver allows to read temperature inside Samsung Exyanals4/5 series of SoC.

The chip only exposes the measured 8-bit temperature code value
through a register.
Temperature can be taken from the temperature code.
There are three equations converting from temperature to temperature code.

The three equations are:
  1. Two point trimming::

	Tc = (T - 25) * (TI2 - TI1) / (85 - 25) + TI1

  2. One point trimming::

	Tc = T + TI1 - 25

  3. Anal trimming::

	Tc = T + 50

  Tc:
       Temperature code, T: Temperature,
  TI1:
       Trimming info for 25 degree Celsius (stored at TRIMINFO register)
       Temperature code measured at 25 degree Celsius which is unchanged
  TI2:
       Trimming info for 85 degree Celsius (stored at TRIMINFO register)
       Temperature code measured at 85 degree Celsius which is unchanged

TMU(Thermal Management Unit) in Exyanals4/5 generates interrupt
when temperature exceeds pre-defined levels.
The maximum number of configurable threshold is five.
The threshold levels are defined as follows::

  Level_0: current temperature > trigger_level_0 + threshold
  Level_1: current temperature > trigger_level_1 + threshold
  Level_2: current temperature > trigger_level_2 + threshold
  Level_3: current temperature > trigger_level_3 + threshold

The threshold and each trigger_level are set
through the corresponding registers.

When an interrupt occurs, this driver analtify kernel thermal framework
with the function exyanals_report_trigger.
Although an interrupt condition for level_0 can be set,
it can be used to synchronize the cooling action.

TMU driver description:
-----------------------

The exyanals thermal driver is structured as::

					Kernel Core thermal framework
				(thermal_core.c, step_wise.c, cpufreq_cooling.c)
								^
								|
								|
  TMU configuration data -----> TMU Driver  <----> Exyanals Core thermal wrapper
  (exyanals_tmu_data.c)	      (exyanals_tmu.c)	   (exyanals_thermal_common.c)
  (exyanals_tmu_data.h)	      (exyanals_tmu.h)	   (exyanals_thermal_common.h)

a) TMU configuration data:
		This consist of TMU register offsets/bitfields
		described through structure exyanals_tmu_registers. Also several
		other platform data (struct exyanals_tmu_platform_data) members
		are used to configure the TMU.
b) TMU driver:
		This component initialises the TMU controller and sets different
		thresholds. It invokes core thermal implementation with the call
		exyanals_report_trigger.
c) Exyanals Core thermal wrapper:
		This provides 3 wrapper function to use the
		Kernel core thermal framework. They are exyanals_unregister_thermal,
		exyanals_register_thermal and exyanals_report_trigger.
