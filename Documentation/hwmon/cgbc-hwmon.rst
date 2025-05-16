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
temp2_input   Box temperature
temp3_input   Ambient temperature
temp4_input   Board temperature
temp5_input   Carrier temperature
temp6_input   Chipset temperature
temp7_input   Video temperature
temp8_input   Other temperature
temp9_input   TOPDIM temperature
temp10_input  BOTTOMDIM temperature
in0_input     CPU voltage
in1_input     DC Runtime voltage
in2_input     DC Standby voltage
in3_input     CMOS Battery voltage
in4_input     Battery voltage
in5_input     AC voltage
in6_input     Other voltage
in7_input     5V voltage
in8_input     5V Standby voltage
in9_input     3V3 voltage
in10_input    3V3 Standby voltage
in11_input    VCore A voltage
in12_input    VCore B voltage
in13_input    12V voltage
curr1_input   DC current
curr2_input   5V current
curr3_input   12V current
fan1_input    CPU fan
fan2_input    Box fan
fan3_input    Ambient fan
fan4_input    Chiptset fan
fan5_input    Video fan
fan6_input    Other fan
============= ======================
