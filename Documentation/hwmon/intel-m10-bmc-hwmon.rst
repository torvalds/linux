.. SPDX-License-Identifier: GPL-2.0

Kernel driver intel-m10-bmc-hwmon
=================================

Supported chips:

 * Intel MAX 10 BMC for Intel PAC N3000

   Prefix: 'n3000bmc-hwmon'

Author: Xu Yilun <yilun.xu@intel.com>


Description
-----------

This driver adds the temperature, voltage, current and power reading
support for the Intel MAX 10 Board Management Controller (BMC) chip.
The BMC chip is integrated in some Intel Programmable Acceleration
Cards (PAC). It connects to a set of sensor chips to monitor the
sensor data of different components on the board. The BMC firmware is
responsible for sensor data sampling and recording in shared
registers. The host driver reads the sensor data from these shared
registers and exposes them to users as hwmon interfaces.

The BMC chip is implemented using the Intel MAX 10 CPLD. It could be
reprogramed to some variants in order to support different Intel
PACs. The driver is designed to be able to distinguish between the
variants, but now it only supports the BMC for Intel PAC N3000.


Sysfs attributes
----------------

The following attributes are supported:

- Intel MAX 10 BMC for Intel PAC N3000:

======================= =======================================================
tempX_input             Temperature of the component (specified by tempX_label)
tempX_max               Temperature maximum setpoint of the component
tempX_crit              Temperature critical setpoint of the component
tempX_max_hyst          Hysteresis for temperature maximum of the component
tempX_crit_hyst         Hysteresis for temperature critical of the component
temp1_label             "Board Temperature"
temp2_label             "FPGA Die Temperature"
temp3_label             "QSFP0 Temperature"
temp4_label             "QSFP1 Temperature"
temp5_label             "Retimer A Temperature"
temp6_label             "Retimer A SerDes Temperature"
temp7_label             "Retimer B Temperature"
temp8_label             "Retimer B SerDes Temperature"

inX_input               Measured voltage of the component (specified by
                        inX_label)
in0_label               "QSFP0 Supply Voltage"
in1_label               "QSFP1 Supply Voltage"
in2_label               "FPGA Core Voltage"
in3_label               "12V Backplane Voltage"
in4_label               "1.2V Voltage"
in5_label               "12V AUX Voltage"
in6_label               "1.8V Voltage"
in7_label               "3.3V Voltage"

currX_input             Measured current of the component (specified by
                        currX_label)
curr1_label             "FPGA Core Current"
curr2_label             "12V Backplane Current"
curr3_label             "12V AUX Current"

powerX_input            Measured power of the component (specified by
                        powerX_label)
power1_label            "Board Power"

======================= =======================================================

All the attributes are read-only.
