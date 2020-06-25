Kernel driver ina2xx
====================

Supported chips:

  * Texas Instruments INA219


    Prefix: 'ina219'
    Addresses: I2C 0x40 - 0x4f

    Datasheet: Publicly available at the Texas Instruments website

	       http://www.ti.com/

  * Texas Instruments INA220

    Prefix: 'ina220'

    Addresses: I2C 0x40 - 0x4f

    Datasheet: Publicly available at the Texas Instruments website

	       http://www.ti.com/

  * Texas Instruments INA226

    Prefix: 'ina226'

    Addresses: I2C 0x40 - 0x4f

    Datasheet: Publicly available at the Texas Instruments website

	       http://www.ti.com/

  * Texas Instruments INA230

    Prefix: 'ina230'

    Addresses: I2C 0x40 - 0x4f

    Datasheet: Publicly available at the Texas Instruments website

	       http://www.ti.com/

  * Texas Instruments INA231

    Prefix: 'ina231'

    Addresses: I2C 0x40 - 0x4f

    Datasheet: Publicly available at the Texas Instruments website

	       http://www.ti.com/

Author: Lothar Felten <lothar.felten@gmail.com>

Description
-----------

The INA219 is a high-side current shunt and power monitor with an I2C
interface. The INA219 monitors both shunt drop and supply voltage, with
programmable conversion times and filtering.

The INA220 is a high or low side current shunt and power monitor with an I2C
interface. The INA220 monitors both shunt drop and supply voltage.

The INA226 is a current shunt and power monitor with an I2C interface.
The INA226 monitors both a shunt voltage drop and bus supply voltage.

INA230 and INA231 are high or low side current shunt and power monitors
with an I2C interface. The chips monitor both a shunt voltage drop and
bus supply voltage.

The shunt value in micro-ohms can be set via platform data or device tree at
compile-time or via the shunt_resistor attribute in sysfs at run-time. Please
refer to the Documentation/devicetree/bindings/hwmon/ina2xx.txt for bindings
if the device tree is used.

Additionally ina226 supports update_interval attribute as described in
Documentation/hwmon/sysfs-interface.rst. Internally the interval is the sum of
bus and shunt voltage conversion times multiplied by the averaging rate. We
don't touch the conversion times and only modify the number of averages. The
lower limit of the update_interval is 2 ms, the upper limit is 2253 ms.
The actual programmed interval may vary from the desired value.

General sysfs entries
---------------------

======================= ===============================
in0_input		Shunt voltage(mV) channel
in1_input		Bus voltage(mV) channel
curr1_input		Current(mA) measurement channel
power1_input		Power(uW) measurement channel
shunt_resistor		Shunt resistance(uOhm) channel
======================= ===============================

Sysfs entries for ina226, ina230 and ina231 only
------------------------------------------------

======================= ====================================================
in0_lcrit		Critical low shunt voltage
in0_crit		Critical high shunt voltage
in0_lcrit_alarm		Shunt voltage critical low alarm
in0_crit_alarm		Shunt voltage critical high alarm
in1_lcrit		Critical low bus voltage
in1_crit		Critical high bus voltage
in1_lcrit_alarm		Bus voltage critical low alarm
in1_crit_alarm		Bus voltage critical high alarm
power1_crit		Critical high power
power1_crit_alarm	Power critical high alarm
update_interval		data conversion time; affects number of samples used
			to average results for shunt and bus voltages.
======================= ====================================================

.. note::

   - Configure `shunt_resistor` before configure `power1_crit`, because power
     value is calculated based on `shunt_resistor` set.
   - Because of the underlying register implementation, only one `*crit` setting
     and its `alarm` can be active. Writing to one `*crit` setting clears other
     `*crit` settings and alarms. Writing 0 to any `*crit` setting clears all
     `*crit` settings and alarms.
