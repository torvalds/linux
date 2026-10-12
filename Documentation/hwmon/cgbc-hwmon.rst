.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver cgbc-hwmon
========================

Supported chips:

 * Congatec Board Controller.

   Prefix: 'cgbc-hwmon'

Author: Thomas Richard <thomas.richard@bootlin.com>

Description
-----------

This driver enables monitoring support for the Congatec Board Controller.
This controller is embedded on the x86 SoMs of Congatec.

Sysfs entries
-------------

The following sysfs entries list contains all sensors defined in the Board
Controller. The available sensors in sysfs depend on the SoM and the
system.

============= ======================
Name          Description
============= ======================
temp1_input   CPU temperature
temp2_input   Case temperature
temp3_input   Ambient temperature
temp4_input   CPU Board temperature
temp5_input   Carrier Board temperature
temp6_input   System Chipset temperature
temp7_input   Video Controller/Board temperature
temp8_input   Other temperature
temp9_input   Top DIMM 0 temperature
temp10_input  Bottom DIMM 0 temperature
temp11_input  Alternate Board temperature
temp12_input  Top DIMM 1 temperature
temp13_input  Top DIMM 2 temperature
temp14_input  Top DIMM 3 temperature
temp15_input  Top DIMM 4 temperature
temp16_input  Top DIMM 5 temperature
temp17_input  Top DIMM 6 temperature
temp18_input  Top DIMM 7 temperature
temp19_input  Bottom DIMM 1 temperature
in0_input     CPU Core voltage
in1_input     DC Runtime voltage
in2_input     DC Standby voltage
in3_input     CMOS Battery voltage
in4_input     Battery Supply voltage
in5_input     AC voltage
in6_input     Other voltage
in7_input     5V Runtime voltage
in8_input     5V Standby voltage
in9_input     3V3 Runtime voltage
in10_input    3V3 Standby voltage
in11_input    VCore A voltage
in12_input    VCore B voltage
in13_input    12V Runtime voltage
in14_input    12V Standby voltage
curr1_input   DC Runtime current
curr2_input   5V Runtime current
curr3_input   12V Runtime current
fan1_input    CPU fan
fan2_input    Case fan
fan3_input    Ambient fan
fan4_input    Chiptset fan
fan5_input    Video fan
fan6_input    Other fan
============= ======================
