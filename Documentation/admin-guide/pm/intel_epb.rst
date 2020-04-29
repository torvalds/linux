.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

======================================
Intel Performance and Energy Bias Hint
======================================

:Copyright: |copy| 2019 Intel Corporation

:Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>


.. kernel-doc:: arch/x86/kernel/cpu/intel_epb.c
   :doc: overview

Intel Performance and Energy Bias Attribute in ``sysfs``
========================================================

The Intel Performance and Energy Bias Hint (EPB) value for a given (logical) CPU
can be checked or updated through a ``sysfs`` attribute (file) under
:file:`/sys/devices/system/cpu/cpu<N>/power/`, where the CPU number ``<N>``
is allocated at the system initialization time:

``energy_perf_bias``
	Shows the current EPB value for the CPU in a sliding scale 0 - 15, where
	a value of 0 corresponds to a hint preference for highest performance
	and a value of 15 corresponds to the maximum energy savings.

	In order to update the EPB value for the CPU, this attribute can be
	written to, either with a number in the 0 - 15 sliding scale above, or
	with one of the strings: "performance", "balance-performance", "normal",
	"balance-power", "power" that represent values reflected by their
	meaning.

	This attribute is present for all online CPUs supporting the EPB
	feature.

Note that while the EPB interface to the processor is defined at the logical CPU
level, the physical register backing it may be shared by multiple CPUs (for
example, SMT siblings or cores in one package).  For this reason, updating the
EPB value for one CPU may cause the EPB values for other CPUs to change.
