.. SPDX-License-Identifier: GPL-2.0

Kernel driver fsp3y
======================
Supported devices:
  * 3Y POWER YH-5151E
  * 3Y POWER YM-2151E

Author: Václav Kubernát <kubernat@cesnet.cz>

Description
-----------
This driver implements limited support for two 3Y POWER devices.

Sysfs entries
-------------
  * in1_input            input voltage
  * in2_input            12V output voltage
  * in3_input            5V output voltage
  * curr1_input          input current
  * curr2_input          12V output current
  * curr3_input          5V output current
  * fan1_input           fan rpm
  * temp1_input          temperature 1
  * temp2_input          temperature 2
  * temp3_input          temperature 3
  * power1_input         input power
  * power2_input         output power
