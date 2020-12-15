.. SPDX-License-Identifier: GPL-2.0

Kernel driver amd_energy
==========================

Supported chips:

* AMD Family 17h Processors

  Prefix: 'amd_energy'

  Addresses used:  RAPL MSRs

  Datasheets:

  - Processor Programming Reference (PPR) for AMD Family 17h Model 01h, Revision B1 Processors

	https://developer.amd.com/wp-content/resources/55570-B1_PUB.zip

  - Preliminary Processor Programming Reference (PPR) for AMD Family 17h Model 31h, Revision B0 Processors

	https://developer.amd.com/wp-content/resources/56176_ppr_Family_17h_Model_71h_B0_pub_Rev_3.06.zip

Author: Naveen Krishna Chatradhi <nchatrad@amd.com>

Description
-----------

The Energy driver exposes the energy counters that are
reported via the Running Average Power Limit (RAPL)
Model-specific Registers (MSRs) via the hardware monitor
(HWMON) sysfs interface.

1. Power, Energy and Time Units
   MSR_RAPL_POWER_UNIT/ C001_0299:
   shared with all cores in the socket

2. Energy consumed by each Core
   MSR_CORE_ENERGY_STATUS/ C001_029A:
   32-bitRO, Accumulator, core-level power reporting

3. Energy consumed by Socket
   MSR_PACKAGE_ENERGY_STATUS/ C001_029B:
   32-bitRO, Accumulator, socket-level power reporting,
   shared with all cores in socket

These registers are updated every 1ms and cleared on
reset of the system.

Note: If SMT is enabled, Linux enumerates all threads as cpus.
Since, the energy status registers are accessed at core level,
reading those registers from the sibling threads would result
in duplicate values. Hence, energy counter entries are not
populated for the siblings.

Energy Caluclation
------------------

Energy information (in Joules) is based on the multiplier,
1/2^ESU; where ESU is an unsigned integer read from
MSR_RAPL_POWER_UNIT register. Default value is 10000b,
indicating energy status unit is 15.3 micro-Joules increment.

Reported values are scaled as per the formula

scaled value = ((1/2^ESU) * (Raw value) * 1000000UL) in uJoules

Users calculate power for a given domain by calculating
	dEnergy/dTime for that domain.

Energy accumulation
--------------------------

Current, Socket energy status register is 32bit, assuming a 240W
2P system, the register would wrap around in

	2^32*15.3 e-6/240 * 2 = 547.60833024 secs to wrap(~9 mins)

The Core energy register may wrap around after several days.

To improve the wrap around time, a kernel thread is implemented
to accumulate the socket energy counters and one core energy counter
per run to a respective 64-bit counter. The kernel thread starts
running during probe, wakes up every 100secs and stops running
when driver is removed.

A socket and core energy read would return the current register
value added to the respective energy accumulator.

Sysfs attributes
----------------

=============== ========  =====================================
Attribute	Label	  Description
===============	========  =====================================

* For index N between [1] and [nr_cpus]

===============	========  ======================================
energy[N]_input EcoreX	  Core Energy   X = [0] to [nr_cpus - 1]
			  Measured input core energy
===============	========  ======================================

* For N between [nr_cpus] and [nr_cpus + nr_socks]

===============	========  ======================================
energy[N]_input EsocketX  Socket Energy X = [0] to [nr_socks -1]
			  Measured input socket energy
=============== ========  ======================================
