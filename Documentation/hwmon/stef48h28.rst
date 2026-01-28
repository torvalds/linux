.. SPDX-License-Identifier: GPL-2.0

Kernel driver stef48h28
=======================

Supported chips:

  * Analog Devices STEF48H28

    Prefix: 'stef48h28'

    Addresses scanned: -

    Datasheet: https://www.st.com/resource/en/data_brief/stef48h28.pdf

Author:

  - Charles Hsu <hsu.yungteng@gmail.com>


Description
-----------

The STEF48H28 is a 30 A integrated e-fuse for 9-80 V DC power rails.
It provides inrush control, undervoltage/overvoltage lockout and
overcurrent protection using an adaptive (I x t) scheme that permits
short high-current pulses typical of CPU/GPU loads.

The device offers an analog current-monitor output and an on-chip
temperature-monitor signal for system supervision. Startup behavior is
programmable through insertion-delay and soft-start settings.

Additional features include power-good indication, self-diagnostics,
thermal shutdown and a PMBus interface for telemetry and status
reporting.

Platform data support
---------------------

The driver supports standard PMBus driver platform data.

Sysfs entries
-------------

======================  ========================================================
in1_label		"vin".
in1_input		Measured voltage. From READ_VIN register.
in1_min			Minimum Voltage. From VIN_UV_WARN_LIMIT register.
in1_max			Maximum voltage. From VIN_OV_WARN_LIMIT register.

in2_label		"vout1".
in2_input		Measured voltage. From READ_VOUT register.
in2_min			Minimum Voltage. From VOUT_UV_WARN_LIMIT register.
in2_max			Maximum voltage. From VOUT_OV_WARN_LIMIT register.

curr1_label "iin".      curr1_input Measured current. From READ_IIN register.

curr2_label "iout1".    curr2_input Measured current. From READ_IOUT register.

power1_label		"pin"
power1_input		Measured input power. From READ_PIN register.

power2_label		"pout1"
power2_input		Measured output power. From READ_POUT register.

temp1_input		Measured temperature. From READ_TEMPERATURE_1 register.
temp1_max		Maximum temperature. From OT_WARN_LIMIT register.
temp1_crit		Critical high temperature. From OT_FAULT_LIMIT register.

temp2_input		Measured temperature. From READ_TEMPERATURE_2 register.
======================  ========================================================
