.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver q54sj108a2
========================

Supported chips:

  * DELTA Q54SJ108A2NCAH, Q54SJ108A2NCDH, Q54SJ108A2NCPG, Q54SJ108A2NCPH

    Prefix: 'q54sj108a2'

    Addresses scanned: -

    Datasheet: https://filecenter.delta-china.com.cn/products/download/01/0102/datasheet/DS_Q54SJ108A2.pdf

Authors:
    Xiao.ma <xiao.mx.ma@deltaww.com>


Description
-----------

This driver implements support for DELTA Q54SJ108A2NCAH, Q54SJ108A2NCDH,
Q54SJ108A2NCPG, and Q54SJ108A2NCPH 1/4 Brick DC/DC Regulated Power Module
with PMBus support.

The driver is a client driver to the core PMBus driver.
Please see Documentation/hwmon/pmbus.rst for details on PMBus client drivers.


Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.


Sysfs entries
-------------

===================== ===== ==================================================
curr1_alarm           RO    Output current alarm
curr1_input           RO    Output current
curr1_label           RO    'iout1'
in1_alarm             RO    Input voltage alarm
in1_input             RO    Input voltage
in1_label             RO    'vin'
in2_alarm             RO    Output voltage alarm
in2_input             RO    Output voltage
in2_label             RO    'vout1'
temp1_alarm           RO    Temperature alarm
temp1_input           RO    Chip temperature
===================== ===== ==================================================
