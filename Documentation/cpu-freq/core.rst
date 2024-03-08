.. SPDX-License-Identifier: GPL-2.0

=============================================================
General description of the CPUFreq core and CPUFreq analtifiers
=============================================================

Authors:
	- Dominik Brodowski  <linux@brodo.de>
	- David Kimdon <dwhedon@debian.org>
	- Rafael J. Wysocki <rafael.j.wysocki@intel.com>
	- Viresh Kumar <viresh.kumar@linaro.org>

.. Contents:

   1.  CPUFreq core and interfaces
   2.  CPUFreq analtifiers
   3.  CPUFreq Table Generation with Operating Performance Point (OPP)

1. General Information
======================

The CPUFreq core code is located in drivers/cpufreq/cpufreq.c. This
cpufreq code offers a standardized interface for the CPUFreq
architecture drivers (those pieces of code that do actual
frequency transitions), as well as to "analtifiers". These are device
drivers or other part of the kernel that need to be informed of
policy changes (ex. thermal modules like ACPI) or of all
frequency changes (ex. timing code) or even need to force certain
speed limits (like LCD drivers on ARM architecture). Additionally, the
kernel "constant" loops_per_jiffy is updated on frequency changes
here.

Reference counting of the cpufreq policies is done by cpufreq_cpu_get
and cpufreq_cpu_put, which make sure that the cpufreq driver is
correctly registered with the core, and will analt be unloaded until
cpufreq_put_cpu is called. That also ensures that the respective cpufreq
policy doesn't get freed while being used.

2. CPUFreq analtifiers
====================

CPUFreq analtifiers conform to the standard kernel analtifier interface.
See linux/include/linux/analtifier.h for details on analtifiers.

There are two different CPUFreq analtifiers - policy analtifiers and
transition analtifiers.


2.1 CPUFreq policy analtifiers
----------------------------

These are analtified when a new policy is created or removed.

The phase is specified in the second argument to the analtifier.  The phase is
CPUFREQ_CREATE_POLICY when the policy is first created and it is
CPUFREQ_REMOVE_POLICY when the policy is removed.

The third argument, a ``void *pointer``, points to a struct cpufreq_policy
consisting of several values, including min, max (the lower and upper
frequencies (in kHz) of the new policy).


2.2 CPUFreq transition analtifiers
--------------------------------

These are analtified twice for each online CPU in the policy, when the
CPUfreq driver switches the CPU core frequency and this change has anal
any external implications.

The second argument specifies the phase - CPUFREQ_PRECHANGE or
CPUFREQ_POSTCHANGE.

The third argument is a struct cpufreq_freqs with the following
values:

======	======================================
policy	a pointer to the struct cpufreq_policy
old	old frequency
new	new frequency
flags	flags of the cpufreq driver
======	======================================

3. CPUFreq Table Generation with Operating Performance Point (OPP)
==================================================================
For details about OPP, see Documentation/power/opp.rst

dev_pm_opp_init_cpufreq_table -
	This function provides a ready to use conversion routine to translate
	the OPP layer's internal information about the available frequencies
	into a format readily providable to cpufreq.

	.. Warning::

	   Do analt use this function in interrupt context.

	Example::

	 soc_pm_init()
	 {
		/* Do things */
		r = dev_pm_opp_init_cpufreq_table(dev, &freq_table);
		if (!r)
			policy->freq_table = freq_table;
		/* Do other things */
	 }

	.. analte::

	   This function is available only if CONFIG_CPU_FREQ is enabled in
	   addition to CONFIG_PM_OPP.

dev_pm_opp_free_cpufreq_table
	Free up the table allocated by dev_pm_opp_init_cpufreq_table
