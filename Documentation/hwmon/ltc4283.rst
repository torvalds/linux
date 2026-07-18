.. SPDX-License-Identifier: GPL-2.0-only

Kernel drivers ltc4283
==========================================

Supported chips:

  * Analog Devices LTC4283

    Prefix: 'ltc4283'

    Addresses scanned: -

    Datasheet:

        https://www.analog.com/media/en/technical-documentation/data-sheets/ltc4283.pdf

Author: Nuno Sá <nuno.sa@analog.com>

Description
___________

The LTC4283 negative voltage hot swap controller drives an external N-channel
MOSFET to allow a board to be safely inserted and removed from a live backplane.
The device features programmable current limit with foldback and independently
adjustable inrush current to optimize the MOSFET safe operating area (SOA). The
SOA timer limits MOSFET temperature rise for reliable protection against
overstresses. An I2C interface and onboard gear-shift ADC allow monitoring of
board current, voltage, power, energy, and fault status.  Additional features
respond to input UV/OV, interrupt the host when a fault has occurred, notify
when output power is good, detect insertion of a board, turn off the MOSFET
if an external supply monitor fails to indicate power good within a timeout
period, and auto-reboot after a programmable delay following a host commanded
turn-off.

Sysfs entries
_____________

The following attributes are supported. Limits are read-write and all the other
attributes are read-only. Note that the VADIOx channels might not be available
if the ADIO pins are used as GPIOs (naturally also affects the respective
differential channels).

======================= ==========================================
in0_lcrit_alarm         Critical Undervoltage alarm
in0_crit_alarm          Critical Overvoltage alarm
in0_reset_history       Clears Under and Overvoltage fault logs.
in0_label		Channel label (VIN)

in1_input		Output voltage (mV).
in1_min			Undervoltage threshold
in1_max			Overvoltage threshold
in1_lowest		Lowest measured voltage
in1_highest		Highest measured voltage
in1_reset_history	Write 1 to reset history.
in1_min_alarm		Undervoltage alarm
in1_max_alarm		Overvoltage alarm
in1_label		Channel label (VPWR)

in2_input		Output voltage (mV).
in2_min			Undervoltage threshold
in2_max			Overvoltage threshold
in2_lowest		Lowest measured voltage
in2_highest		Highest measured voltage
in2_reset_history	Write 1 to reset history.
in2_min_alarm		Undervoltage alarm
in2_max_alarm		Overvoltage alarm
in2_enable		Enable/Disable monitoring.
in2_label		Channel label (VADI1)

in3_input		Output voltage (mV).
in3_min			Undervoltage threshold
in3_max			Overvoltage threshold
in3_lowest		Lowest measured voltage
in3_highest		Highest measured voltage
in3_reset_history	Write 1 to reset history.
in3_min_alarm		Undervoltage alarm
in3_max_alarm		Overvoltage alarm
in3_enable		Enable/Disable monitoring.
in3_label		Channel label (VADI2)

in4_input		Output voltage (mV).
in4_min			Undervoltage threshold
in4_max			Overvoltage threshold
in4_lowest		Lowest measured voltage
in4_highest		Highest measured voltage
in4_reset_history	Write 1 to reset history.
in4_min_alarm		Undervoltage alarm
in4_max_alarm		Overvoltage alarm
in4_enable		Enable/Disable monitoring.
in4_label		Channel label (VADI3)

in5_input		Output voltage (mV).
in5_min			Undervoltage threshold
in5_max			Overvoltage threshold
in5_lowest		Lowest measured voltage
in5_highest		Highest measured voltage
in5_reset_history	Write 1 to reset history.
in5_min_alarm		Undervoltage alarm
in5_max_alarm		Overvoltage alarm
in5_enable		Enable/Disable monitoring.
in5_label		Channel label (VADI4)

in6_input		Output voltage (mV).
in6_min			Undervoltage threshold
in6_max			Overvoltage threshold
in6_lowest		Lowest measured voltage
in6_highest		Highest measured voltage
in6_reset_history	Write 1 to reset history.
in6_min_alarm		Undervoltage alarm
in6_max_alarm		Overvoltage alarm
in6_enable		Enable/Disable monitoring.
in6_label		Channel label (VADIO1)

in7_input		Output voltage (mV).
in7_min			Undervoltage threshold
in7_max			Overvoltage threshold
in7_lowest		Lowest measured voltage
in7_highest		Highest measured voltage
in7_reset_history	Write 1 to reset history.
in7_min_alarm		Undervoltage alarm
in7_max_alarm		Overvoltage alarm
in7_enable		Enable/Disable monitoring.
in7_label		Channel label (VADIO2)

in8_input		Output voltage (mV).
in8_min			Undervoltage threshold
in8_max			Overvoltage threshold
in8_lowest		Lowest measured voltage
in8_highest		Highest measured voltage
in8_reset_history	Write 1 to reset history.
in8_min_alarm		Undervoltage alarm
in8_max_alarm		Overvoltage alarm
in8_enable		Enable/Disable monitoring.
in8_label		Channel label (VADIO3)

in9_input		Output voltage (mV).
in9_min			Undervoltage threshold
in9_max			Overvoltage threshold
in9_lowest		Lowest measured voltage
in9_highest		Highest measured voltage
in9_reset_history	Write 1 to reset history.
in9_min_alarm		Undervoltage alarm
in9_max_alarm		Overvoltage alarm
in9_enable		Enable/Disable monitoring.
in9_label		Channel label (VADIO4)

in10_input		Output voltage (mV).
in10_min		Undervoltage threshold
in10_max		Overvoltage threshold
in10_lowest		Lowest measured voltage
in10_highest		Highest measured voltage
in10_reset_history	Write 1 to reset history.
in10_min_alarm		Undervoltage alarm
in10_max_alarm		Overvoltage alarm
in10_enable		Enable/Disable monitoring.
in10_label		Channel label (DRNS)

in11_input		Output voltage (mV).
in11_min		Undervoltage threshold
in11_max		Overvoltage threshold
in11_lowest		Lowest measured voltage
in11_highest		Highest measured voltage
in11_reset_history	Write 1 to reset history.
			Also clears fet bad and short fault logs.
in11_min_alarm		Undervoltage alarm
in11_max_alarm		Overvoltage alarm
in11_enable		Enable/Disable monitoring
in11_fault		Failure in the MOSFET. Either bad or shorted FET.
in11_label		Channel label (DRAIN)

in12_input		Output voltage (mV).
in12_min		Undervoltage threshold
in12_max		Overvoltage threshold
in12_lowest		Lowest measured voltage
in12_highest		Highest measured voltage
in12_reset_history	Write 1 to reset history.
in12_min_alarm		Undervoltage alarm
in12_max_alarm		Overvoltage alarm
in12_enable		Enable/Disable monitoring.
in12_label		Channel label (ADIN2-ADIN1)

in13_input		Output voltage (mV).
in13_min		Undervoltage threshold
in13_max		Overvoltage threshold
in13_lowest		Lowest measured voltage
in13_highest		Highest measured voltage
in13_reset_history	Write 1 to reset history.
in13_min_alarm		Undervoltage alarm
in13_max_alarm		Overvoltage alarm
in13_enable		Enable/Disable monitoring.
in13_label		Channel label (ADIN4-ADIN3)

in14_input		Output voltage (mV).
in14_min		Undervoltage threshold
in14_max		Overvoltage threshold
in14_lowest		Lowest measured voltage
in14_highest		Highest measured voltage
in14_reset_history	Write 1 to reset history.
in14_min_alarm		Undervoltage alarm
in14_max_alarm		Overvoltage alarm
in14_enable		Enable/Disable monitoring.
in14_label		Channel label (ADIO2-ADIO1)

in15_input		Output voltage (mV).
in15_min		Undervoltage threshold
in15_max		Overvoltage threshold
in15_lowest		Lowest measured voltage
in15_highest		Highest measured voltage
in15_reset_history	Write 1 to reset history.
in15_min_alarm		Undervoltage alarm
in15_max_alarm		Overvoltage alarm
in15_enable		Enable/Disable monitoring.
in15_label		Channel label (ADIO4-ADIO3)

curr1_input		Sense current (mA)
curr1_min		Undercurrent threshold
curr1_max		Overcurrent threshold
curr1_lowest		Lowest measured current
curr1_highest		Highest measured current
curr1_reset_history	Write 1 to reset curr1 history.
			Also clears overcurrent fault logs.
curr1_min_alarm		Undercurrent alarm
curr1_max_alarm		Overcurrent alarm
curr1_crit_alarm        Critical Overcurrent alarm
curr1_label		Channel label (ISENSE)

power1_input		Power (in uW)
power1_min		Low power threshold
power1_max		High power threshold
power1_input_lowest	Historical minimum power use
power1_input_highest	Historical maximum power use
power1_reset_history	Write 1 to reset power1 history.
			Also clears power fault logs.
power1_min_alarm	Low power alarm
power1_max_alarm	High power alarm
power1_label		Channel label (Power)

energy1_input		Measured energy over time (in microJoule)
energy1_enable		Enable/Disable Energy accumulation
======================= ==========================================

DebugFs entries
_______________

The chip also has a fault log register where failures can be logged. Hence,
as these are logging events, we give access to them in debugfs. Note that
even if some failure is detected in these logs, it does necessarily mean
that the failure is still present. As mentioned in the proper Sysfs entries,
these logs can be cleared by writing in the proper reset_history attribute.

.. warning:: The debugfs interface is subject to change without notice
             and is only available when the kernel is compiled with
             ``CONFIG_DEBUG_FS`` defined.

``/sys/kernel/debug/i2c/i2c-[X]/[X]-addr/``
contains the following attributes:

==============================  ==========================================================
power1_failed_fault_log		Set to 1 by a power1 fault occurring.
power1_good_input_fault_log	Set to 1 by a power1 good input fault occurring at PGIO3.
in11_fet_short_fault_log	Set to 1 when a FET-short fault occurs.
in11_fet_bad_fault_log		Set to 1 when a FET-BAD fault occurs.
in0_lcrit_fault_log		Set to 1 by a VIN undervoltage fault occurring.
in0_crit_fault_log		Set to 1 by a VIN overvoltage fault occurring.
curr1_crit_fault_log		Set to 1 by an overcurrent fault occurring.
==============================  ==========================================================
