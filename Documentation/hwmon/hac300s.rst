.. SPDX-License-Identifier: GPL-2.0

Kernel driver hac300s
=====================

Supported chips:

   * HiTRON HAC300S

     Prefix: 'hac300s'

     Datasheet: Publicly available at HiTRON website.

Author:

  - Vasileios Amoiridis <vasileios.amoiridis@cern.ch>

Description
-----------

This driver supports the HiTRON HAC300S PSU. It is a Universal AC input
harmonic correction AC-DC hot-swappable CompactPCI Serial Dual output
(with 5V standby) 312 Watts active current sharing switching power supply.

The device has an input of 90-264VAC and 2 nominal output voltaged at 12V and
5V which they can supplu up to 25A and 2.5A respectively.

Sysfs entries
-------------

======= ==========================================
curr1   Output current
in1     Output voltage
power1  Output power
temp1   Ambient temperature inside the module
temp2   Internal secondary component's temperature
======= ==========================================
