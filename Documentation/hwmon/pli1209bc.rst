.. SPDX-License-Identifier: GPL-2.0

Kernel driver pli1209bc
=======================

Supported chips:

  * Digital Supervisor PLI1209BC

    Prefix: 'pli1209bc'

    Addresses scanned: 0x50 - 0x5F

    Datasheet: https://www.vicorpower.com/documents/datasheets/ds-PLI1209BCxyzz-VICOR.pdf

Authors:
    - Marcello Sylvester Bauer <sylv@sylv.io>

Description
-----------

The Vicor PLI1209BC is an isolated digital power system supervisor that provides
a communication interface between a host processor and one Bus Converter Module
(BCM). The PLI communicates with a system controller via a PMBus compatible
interface over an isolated UART interface. Through the PLI, the host processor
can configure, set protection limits, and monitor the BCM.

Sysfs entries
-------------

======================= ========================================================
in1_label		"vin2"
in1_input		Input voltage.
in1_rated_min		Minimum rated input voltage.
in1_rated_max		Maximum rated input voltage.
in1_max			Maximum input voltage.
in1_max_alarm		Input voltage high alarm.
in1_crit		Critical input voltage.
in1_crit_alarm		Input voltage critical alarm.

in2_label		"vout2"
in2_input		Output voltage.
in2_rated_min		Minimum rated output voltage.
in2_rated_max		Maximum rated output voltage.
in2_alarm		Output voltage alarm

curr1_label		"iin2"
curr1_input		Input current.
curr1_max		Maximum input current.
curr1_max_alarm		Maximum input current high alarm.
curr1_crit		Critical input current.
curr1_crit_alarm	Input current critical alarm.

curr2_label		"iout2"
curr2_input		Output current.
curr2_crit		Critical output current.
curr2_crit_alarm	Output current critical alarm.
curr2_max		Maximum output current.
curr2_max_alarm		Output current high alarm.

power1_label		"pin2"
power1_input		Input power.
power1_alarm		Input power alarm.

power2_label		"pout2"
power2_input		Output power.
power2_rated_max	Maximum rated output power.

temp1_input		Die temperature.
temp1_alarm		Die temperature alarm.
temp1_max		Maximum die temperature.
temp1_max_alarm		Die temperature high alarm.
temp1_crit		Critical die temperature.
temp1_crit_alarm	Die temperature critical alarm.
======================= ========================================================
