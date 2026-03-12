.. SPDX-License-Identifier: GPL-2.0

==========================
Link Power Managent Policy
==========================

This parameter allows the user to set the link (interface) power management.
There are 6 possible options:

======================  =====================================================
Value			Effect
======================  =====================================================
min_power		Enable slumber mode(no partial mode) for the link to
			use the least possible power when possible.  This may
			sacrifice some performance due to increased latency
			when coming out of lower power states.

max_performance		Generally, this means no power management.  Tell
			the controller to have performance be a priority
			over power management.

medium_power		Tell the controller to enter a lower power state
			when possible, but do not enter the lowest power
			state, thus improving latency over min_power setting.

keep_firmware_settings	Do not change the current firmware settings for
			Power management. This is the default setting.

med_power_with_dipm	Same as medium_power, but additionally with
			Device-initiated power management(DIPM) enabled,
			as Intel Rapid Storage Technology(IRST) does.

min_power_with_partial	Same as min_power, but additionally with partial
			power state enabled, which may improve performance
			over min_power setting.
======================  =====================================================
