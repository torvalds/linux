.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===============================================
``amd-pstate`` CPU Performance Scaling Driver
===============================================

:Copyright: |copy| 2021 Advanced Micro Devices, Inc.

:Author: Huang Rui <ray.huang@amd.com>


Introduction
===================

``amd-pstate`` is the AMD CPU performance scaling driver that introduces a
new CPU frequency control mechanism on modern AMD APU and CPU series in
Linux kernel. The new mechanism is based on Collaborative Processor
Performance Control (CPPC) which provides finer grain frequency management
than legacy ACPI hardware P-States. Current AMD CPU/APU platforms are using
the ACPI P-states driver to manage CPU frequency and clocks with switching
only in 3 P-states. CPPC replaces the ACPI P-states controls and allows a
flexible, low-latency interface for the Linux kernel to directly
communicate the performance hints to hardware.

``amd-pstate`` leverages the Linux kernel governors such as ``schedutil``,
``ondemand``, etc. to manage the performance hints which are provided by
CPPC hardware functionality that internally follows the hardware
specification (for details refer to AMD64 Architecture Programmer's Manual
Volume 2: System Programming [1]_). Currently, ``amd-pstate`` supports basic
frequency control function according to kernel governors on some of the
Zen2 and Zen3 processors, and we will implement more AMD specific functions
in future after we verify them on the hardware and SBIOS.


AMD CPPC Overview
=======================

Collaborative Processor Performance Control (CPPC) interface enumerates a
continuous, abstract, and unit-less performance value in a scale that is
not tied to a specific performance state / frequency. This is an ACPI
standard [2]_ which software can specify application performance goals and
hints as a relative target to the infrastructure limits. AMD processors
provide the low latency register model (MSR) instead of an AML code
interpreter for performance adjustments. ``amd-pstate`` will initialize a
``struct cpufreq_driver`` instance, ``amd_pstate_driver``, with the callbacks
to manage each performance update behavior. ::

 Highest Perf ------>+-----------------------+                         +-----------------------+
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |          Max Perf  ---->|                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
 Nominal Perf ------>+-----------------------+                         +-----------------------+
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |      Desired Perf  ---->|                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
  Lowest non-        |                       |                         |                       |
  linear perf ------>+-----------------------+                         +-----------------------+
                     |                       |                         |                       |
                     |                       |       Lowest perf  ---->|                       |
                     |                       |                         |                       |
  Lowest perf ------>+-----------------------+                         +-----------------------+
                     |                       |                         |                       |
                     |                       |                         |                       |
                     |                       |                         |                       |
          0   ------>+-----------------------+                         +-----------------------+

                                     AMD P-States Performance Scale


.. _perf_cap:

AMD CPPC Performance Capability
--------------------------------

Highest Performance (RO)
.........................

This is the absolute maximum performance an individual processor may reach,
assuming ideal conditions. This performance level may not be sustainable
for long durations and may only be achievable if other platform components
are in a specific state; for example, it may require other processors to be in
an idle state. This would be equivalent to the highest frequencies
supported by the processor.

Nominal (Guaranteed) Performance (RO)
......................................

This is the maximum sustained performance level of the processor, assuming
ideal operating conditions. In the absence of an external constraint (power,
thermal, etc.), this is the performance level the processor is expected to
be able to maintain continuously. All cores/processors are expected to be
able to sustain their nominal performance state simultaneously.

Lowest non-linear Performance (RO)
...................................

This is the lowest performance level at which nonlinear power savings are
achieved, for example, due to the combined effects of voltage and frequency
scaling. Above this threshold, lower performance levels should be generally
more energy efficient than higher performance levels. This register
effectively conveys the most efficient performance level to ``amd-pstate``.

Lowest Performance (RO)
........................

This is the absolute lowest performance level of the processor. Selecting a
performance level lower than the lowest nonlinear performance level may
cause an efficiency penalty but should reduce the instantaneous power
consumption of the processor.

AMD CPPC Performance Control
------------------------------

``amd-pstate`` passes performance goals through these registers. The
register drives the behavior of the desired performance target.

Minimum requested performance (RW)
...................................

``amd-pstate`` specifies the minimum allowed performance level.

Maximum requested performance (RW)
...................................

``amd-pstate`` specifies a limit the maximum performance that is expected
to be supplied by the hardware.

Desired performance target (RW)
...................................

``amd-pstate`` specifies a desired target in the CPPC performance scale as
a relative number. This can be expressed as percentage of nominal
performance (infrastructure max). Below the nominal sustained performance
level, desired performance expresses the average performance level of the
processor subject to hardware. Above the nominal performance level,
the processor must provide at least nominal performance requested and go higher
if current operating conditions allow.

Energy Performance Preference (EPP) (RW)
.........................................

This attribute provides a hint to the hardware if software wants to bias
toward performance (0x0) or energy efficiency (0xff).


Key Governors Support
=======================

``amd-pstate`` can be used with all the (generic) scaling governors listed
by the ``scaling_available_governors`` policy attribute in ``sysfs``. Then,
it is responsible for the configuration of policy objects corresponding to
CPUs and provides the ``CPUFreq`` core (and the scaling governors attached
to the policy objects) with accurate information on the maximum and minimum
operating frequencies supported by the hardware. Users can check the
``scaling_cur_freq`` information comes from the ``CPUFreq`` core.

``amd-pstate`` mainly supports ``schedutil`` and ``ondemand`` for dynamic
frequency control. It is to fine tune the processor configuration on
``amd-pstate`` to the ``schedutil`` with CPU CFS scheduler. ``amd-pstate``
registers the adjust_perf callback to implement performance update behavior
similar to CPPC. It is initialized by ``sugov_start`` and then populates the
CPU's update_util_data pointer to assign ``sugov_update_single_perf`` as the
utilization update callback function in the CPU scheduler. The CPU scheduler
will call ``cpufreq_update_util`` and assigns the target performance according
to the ``struct sugov_cpu`` that the utilization update belongs to.
Then, ``amd-pstate`` updates the desired performance according to the CPU
scheduler assigned.

.. _processor_support:

Processor Support
=======================

The ``amd-pstate`` initialization will fail if the ``_CPC`` entry in the ACPI
SBIOS does not exist in the detected processor. It uses ``acpi_cpc_valid``
to check the existence of ``_CPC``. All Zen based processors support the legacy
ACPI hardware P-States function, so when ``amd-pstate`` fails initialization,
the kernel will fall back to initialize the ``acpi-cpufreq`` driver.

There are two types of hardware implementations for ``amd-pstate``: one is
`Full MSR Support <perf_cap_>`_ and another is `Shared Memory Support
<perf_cap_>`_. It can use the :c:macro:`X86_FEATURE_CPPC` feature flag to
indicate the different types. (For details, refer to the Processor Programming
Reference (PPR) for AMD Family 19h Model 51h, Revision A1 Processors [3]_.)
``amd-pstate`` is to register different ``static_call`` instances for different
hardware implementations.

Currently, some of the Zen2 and Zen3 processors support ``amd-pstate``. In the
future, it will be supported on more and more AMD processors.

Full MSR Support
-----------------

Some new Zen3 processors such as Cezanne provide the MSR registers directly
while the :c:macro:`X86_FEATURE_CPPC` CPU feature flag is set.
``amd-pstate`` can handle the MSR register to implement the fast switch
function in ``CPUFreq`` that can reduce the latency of frequency control in
interrupt context. The functions with a ``pstate_xxx`` prefix represent the
operations on MSR registers.

Shared Memory Support
----------------------

If the :c:macro:`X86_FEATURE_CPPC` CPU feature flag is not set, the
processor supports the shared memory solution. In this case, ``amd-pstate``
uses the ``cppc_acpi`` helper methods to implement the callback functions
that are defined on ``static_call``. The functions with the ``cppc_xxx`` prefix
represent the operations of ACPI CPPC helpers for the shared memory solution.


AMD P-States and ACPI hardware P-States always can be supported in one
processor. But AMD P-States has the higher priority and if it is enabled
with :c:macro:`MSR_AMD_CPPC_ENABLE` or ``cppc_set_enable``, it will respond
to the request from AMD P-States.


User Space Interface in ``sysfs`` - Per-policy control
======================================================

``amd-pstate`` exposes several global attributes (files) in ``sysfs`` to
control its functionality at the system level. They are located in the
``/sys/devices/system/cpu/cpufreq/policyX/`` directory and affect all CPUs. ::

 root@hr-test1:/home/ray# ls /sys/devices/system/cpu/cpufreq/policy0/*amd*
 /sys/devices/system/cpu/cpufreq/policy0/amd_pstate_highest_perf
 /sys/devices/system/cpu/cpufreq/policy0/amd_pstate_lowest_nonlinear_freq
 /sys/devices/system/cpu/cpufreq/policy0/amd_pstate_max_freq


``amd_pstate_highest_perf / amd_pstate_max_freq``

Maximum CPPC performance and CPU frequency that the driver is allowed to
set, in percent of the maximum supported CPPC performance level (the highest
performance supported in `AMD CPPC Performance Capability <perf_cap_>`_).
In some ASICs, the highest CPPC performance is not the one in the ``_CPC``
table, so we need to expose it to sysfs. If boost is not active, but
still supported, this maximum frequency will be larger than the one in
``cpuinfo``.
This attribute is read-only.

``amd_pstate_lowest_nonlinear_freq``

The lowest non-linear CPPC CPU frequency that the driver is allowed to set,
in percent of the maximum supported CPPC performance level. (Please see the
lowest non-linear performance in `AMD CPPC Performance Capability
<perf_cap_>`_.)
This attribute is read-only.

``energy_performance_available_preferences``

A list of all the supported EPP preferences that could be used for
``energy_performance_preference`` on this system.
These profiles represent different hints that are provided
to the low-level firmware about the user's desired energy vs efficiency
tradeoff.  ``default`` represents the epp value is set by platform
firmware. This attribute is read-only.

``energy_performance_preference``

The current energy performance preference can be read from this attribute.
and user can change current preference according to energy or performance needs
Please get all support profiles list from
``energy_performance_available_preferences`` attribute, all the profiles are
integer values defined between 0 to 255 when EPP feature is enabled by platform
firmware, if EPP feature is disabled, driver will ignore the written value
This attribute is read-write.

Other performance and frequency values can be read back from
``/sys/devices/system/cpu/cpuX/acpi_cppc/``, see :ref:`cppc_sysfs`.


``amd-pstate`` vs ``acpi-cpufreq``
======================================

On the majority of AMD platforms supported by ``acpi-cpufreq``, the ACPI tables
provided by the platform firmware are used for CPU performance scaling, but
only provide 3 P-states on AMD processors.
However, on modern AMD APU and CPU series, hardware provides the Collaborative
Processor Performance Control according to the ACPI protocol and customizes this
for AMD platforms. That is, fine-grained and continuous frequency ranges
instead of the legacy hardware P-states. ``amd-pstate`` is the kernel
module which supports the new AMD P-States mechanism on most of the future AMD
platforms. The AMD P-States mechanism is the more performance and energy
efficiency frequency management method on AMD processors.


AMD Pstate Driver Operation Modes
=================================

``amd_pstate`` CPPC has 3 operation modes: autonomous (active) mode,
non-autonomous (passive) mode and guided autonomous (guided) mode.
Active/passive/guided mode can be chosen by different kernel parameters.

- In autonomous mode, platform ignores the desired performance level request
  and takes into account only the values set to the minimum, maximum and energy
  performance preference registers.
- In non-autonomous mode, platform gets desired performance level
  from OS directly through Desired Performance Register.
- In guided-autonomous mode, platform sets operating performance level
  autonomously according to the current workload and within the limits set by
  OS through min and max performance registers.

Active Mode
------------

``amd_pstate=active``

This is the low-level firmware control mode which is implemented by ``amd_pstate_epp``
driver with ``amd_pstate=active`` passed to the kernel in the command line.
In this mode, ``amd_pstate_epp`` driver provides a hint to the hardware if software
wants to bias toward performance (0x0) or energy efficiency (0xff) to the CPPC firmware.
then CPPC power algorithm will calculate the runtime workload and adjust the realtime
cores frequency according to the power supply and thermal, core voltage and some other
hardware conditions.

Passive Mode
------------

``amd_pstate=passive``

It will be enabled if the ``amd_pstate=passive`` is passed to the kernel in the command line.
In this mode, ``amd_pstate`` driver software specifies a desired QoS target in the CPPC
performance scale as a relative number. This can be expressed as percentage of nominal
performance (infrastructure max). Below the nominal sustained performance level,
desired performance expresses the average performance level of the processor subject
to the Performance Reduction Tolerance register. Above the nominal performance level,
processor must provide at least nominal performance requested and go higher if current
operating conditions allow.

Guided Mode
-----------

``amd_pstate=guided``

If ``amd_pstate=guided`` is passed to kernel command line option then this mode
is activated.  In this mode, driver requests minimum and maximum performance
level and the platform autonomously selects a performance level in this range
and appropriate to the current workload.

User Space Interface in ``sysfs`` - General
===========================================

Global Attributes
-----------------

``amd-pstate`` exposes several global attributes (files) in ``sysfs`` to
control its functionality at the system level.  They are located in the
``/sys/devices/system/cpu/amd-pstate/`` directory and affect all CPUs.

``status``
	Operation mode of the driver: "active", "passive" or "disable".

	"active"
		The driver is functional and in the ``active mode``

	"passive"
		The driver is functional and in the ``passive mode``

	"guided"
		The driver is functional and in the ``guided mode``

	"disable"
		The driver is unregistered and not functional now.

        This attribute can be written to in order to change the driver's
        operation mode or to unregister it.  The string written to it must be
        one of the possible values of it and, if successful, writing one of
        these values to the sysfs file will cause the driver to switch over
        to the operation mode represented by that string - or to be
        unregistered in the "disable" case.

``cpupower`` tool support for ``amd-pstate``
===============================================

``amd-pstate`` is supported by the ``cpupower`` tool, which can be used to dump
frequency information. Development is in progress to support more and more
operations for the new ``amd-pstate`` module with this tool. ::

 root@hr-test1:/home/ray# cpupower frequency-info
 analyzing CPU 0:
   driver: amd-pstate
   CPUs which run at the same hardware frequency: 0
   CPUs which need to have their frequency coordinated by software: 0
   maximum transition latency: 131 us
   hardware limits: 400 MHz - 4.68 GHz
   available cpufreq governors: ondemand conservative powersave userspace performance schedutil
   current policy: frequency should be within 400 MHz and 4.68 GHz.
                   The governor "schedutil" may decide which speed to use
                   within this range.
   current CPU frequency: Unable to call hardware
   current CPU frequency: 4.02 GHz (asserted by call to kernel)
   boost state support:
     Supported: yes
     Active: yes
     AMD PSTATE Highest Performance: 166. Maximum Frequency: 4.68 GHz.
     AMD PSTATE Nominal Performance: 117. Nominal Frequency: 3.30 GHz.
     AMD PSTATE Lowest Non-linear Performance: 39. Lowest Non-linear Frequency: 1.10 GHz.
     AMD PSTATE Lowest Performance: 15. Lowest Frequency: 400 MHz.


Diagnostics and Tuning
=======================

Trace Events
--------------

There are two static trace events that can be used for ``amd-pstate``
diagnostics. One of them is the ``cpu_frequency`` trace event generally used
by ``CPUFreq``, and the other one is the ``amd_pstate_perf`` trace event
specific to ``amd-pstate``.  The following sequence of shell commands can
be used to enable them and see their output (if the kernel is
configured to support event tracing). ::

 root@hr-test1:/home/ray# cd /sys/kernel/tracing/
 root@hr-test1:/sys/kernel/tracing# echo 1 > events/amd_cpu/enable
 root@hr-test1:/sys/kernel/tracing# cat trace
 # tracer: nop
 #
 # entries-in-buffer/entries-written: 47827/42233061   #P:2
 #
 #                                _-----=> irqs-off
 #                               / _----=> need-resched
 #                              | / _---=> hardirq/softirq
 #                              || / _--=> preempt-depth
 #                              ||| /     delay
 #           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
 #              | |         |   ||||      |         |
          <idle>-0       [015] dN...  4995.979886: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=15 changed=false fast_switch=true
          <idle>-0       [007] d.h..  4995.979893: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=7 changed=false fast_switch=true
             cat-2161    [000] d....  4995.980841: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=0 changed=false fast_switch=true
            sshd-2125    [004] d.s..  4995.980968: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=4 changed=false fast_switch=true
          <idle>-0       [007] d.s..  4995.980968: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=7 changed=false fast_switch=true
          <idle>-0       [003] d.s..  4995.980971: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=3 changed=false fast_switch=true
          <idle>-0       [011] d.s..  4995.980996: amd_pstate_perf: amd_min_perf=85 amd_des_perf=85 amd_max_perf=166 cpu_id=11 changed=false fast_switch=true

The ``cpu_frequency`` trace event will be triggered either by the ``schedutil`` scaling
governor (for the policies it is attached to), or by the ``CPUFreq`` core (for the
policies with other scaling governors).


Tracer Tool
-------------

``amd_pstate_tracer.py`` can record and parse ``amd-pstate`` trace log, then
generate performance plots. This utility can be used to debug and tune the
performance of ``amd-pstate`` driver. The tracer tool needs to import intel
pstate tracer.

Tracer tool located in ``linux/tools/power/x86/amd_pstate_tracer``. It can be
used in two ways. If trace file is available, then directly parse the file
with command ::

 ./amd_pstate_trace.py [-c cpus] -t <trace_file> -n <test_name>

Or generate trace file with root privilege, then parse and plot with command ::

 sudo ./amd_pstate_trace.py [-c cpus] -n <test_name> -i <interval> [-m kbytes]

The test result can be found in ``results/test_name``. Following is the example
about part of the output. ::

 common_cpu  common_secs  common_usecs  min_perf  des_perf  max_perf  freq    mperf   apef    tsc       load   duration_ms  sample_num  elapsed_time  common_comm
 CPU_005     712          116384        39        49        166       0.7565  9645075 2214891 38431470  25.1   11.646       469         2.496         kworker/5:0-40
 CPU_006     712          116408        39        49        166       0.6769  8950227 1839034 37192089  24.06  11.272       470         2.496         kworker/6:0-1264

Unit Tests for amd-pstate
-------------------------

``amd-pstate-ut`` is a test module for testing the ``amd-pstate`` driver.

 * It can help all users to verify their processor support (SBIOS/Firmware or Hardware).

 * Kernel can have a basic function test to avoid the kernel regression during the update.

 * We can introduce more functional or performance tests to align the result together, it will benefit power and performance scale optimization.

1. Test case descriptions

    1). Basic tests

        Test prerequisite and basic functions for the ``amd-pstate`` driver.

        +---------+--------------------------------+------------------------------------------------------------------------------------+
        | Index   | Functions                      | Description                                                                        |
        +=========+================================+====================================================================================+
        | 1       | amd_pstate_ut_acpi_cpc_valid   || Check whether the _CPC object is present in SBIOS.                                |
        |         |                                ||                                                                                   |
        |         |                                || The detail refer to `Processor Support <processor_support_>`_.                    |
        +---------+--------------------------------+------------------------------------------------------------------------------------+
        | 2       | amd_pstate_ut_check_enabled    || Check whether AMD P-State is enabled.                                             |
        |         |                                ||                                                                                   |
        |         |                                || AMD P-States and ACPI hardware P-States always can be supported in one processor. |
        |         |                                | But AMD P-States has the higher priority and if it is enabled with                 |
        |         |                                | :c:macro:`MSR_AMD_CPPC_ENABLE` or ``cppc_set_enable``, it will respond to the      |
        |         |                                | request from AMD P-States.                                                         |
        +---------+--------------------------------+------------------------------------------------------------------------------------+
        | 3       | amd_pstate_ut_check_perf       || Check if the each performance values are reasonable.                              |
        |         |                                || highest_perf >= nominal_perf > lowest_nonlinear_perf > lowest_perf > 0.           |
        +---------+--------------------------------+------------------------------------------------------------------------------------+
        | 4       | amd_pstate_ut_check_freq       || Check if the each frequency values and max freq when set support boost mode       |
        |         |                                | are reasonable.                                                                    |
        |         |                                || max_freq >= nominal_freq > lowest_nonlinear_freq > min_freq > 0                   |
        |         |                                || If boost is not active but supported, this maximum frequency will be larger than  |
        |         |                                | the one in ``cpuinfo``.                                                            |
        +---------+--------------------------------+------------------------------------------------------------------------------------+

    2). Tbench test

        Test and monitor the cpu changes when running tbench benchmark under the specified governor.
        These changes include desire performance, frequency, load, performance, energy etc.
        The specified governor is ondemand or schedutil.
        Tbench can also be tested on the ``acpi-cpufreq`` kernel driver for comparison.

    3). Gitsource test

        Test and monitor the cpu changes when running gitsource benchmark under the specified governor.
        These changes include desire performance, frequency, load, time, energy etc.
        The specified governor is ondemand or schedutil.
        Gitsource can also be tested on the ``acpi-cpufreq`` kernel driver for comparison.

#. How to execute the tests

   We use test module in the kselftest frameworks to implement it.
   We create ``amd-pstate-ut`` module and tie it into kselftest.(for
   details refer to Linux Kernel Selftests [4]_).

    1). Build

        + open the :c:macro:`CONFIG_X86_AMD_PSTATE` configuration option.
        + set the :c:macro:`CONFIG_X86_AMD_PSTATE_UT` configuration option to M.
        + make project
        + make selftest ::

            $ cd linux
            $ make -C tools/testing/selftests

        + make perf ::

            $ cd tools/perf/
            $ make


    2). Installation & Steps ::

        $ make -C tools/testing/selftests install INSTALL_PATH=~/kselftest
        $ cp tools/perf/perf /usr/bin/perf
        $ sudo ./kselftest/run_kselftest.sh -c amd-pstate

    3). Specified test case ::

        $ cd ~/kselftest/amd-pstate
        $ sudo ./run.sh -t basic
        $ sudo ./run.sh -t tbench
        $ sudo ./run.sh -t tbench -m acpi-cpufreq
        $ sudo ./run.sh -t gitsource
        $ sudo ./run.sh -t gitsource -m acpi-cpufreq
        $ ./run.sh --help
        ./run.sh: illegal option -- -
        Usage: ./run.sh [OPTION...]
                [-h <help>]
                [-o <output-file-for-dump>]
                [-c <all: All testing,
                     basic: Basic testing,
                     tbench: Tbench testing,
                     gitsource: Gitsource testing.>]
                [-t <tbench time limit>]
                [-p <tbench process number>]
                [-l <loop times for tbench>]
                [-i <amd tracer interval>]
                [-m <comparative test: acpi-cpufreq>]


    4). Results

        + basic

         When you finish test, you will get the following log info ::

          $ dmesg | grep "amd_pstate_ut" | tee log.txt
          [12977.570663] amd_pstate_ut: 1    amd_pstate_ut_acpi_cpc_valid  success!
          [12977.570673] amd_pstate_ut: 2    amd_pstate_ut_check_enabled   success!
          [12977.571207] amd_pstate_ut: 3    amd_pstate_ut_check_perf      success!
          [12977.571212] amd_pstate_ut: 4    amd_pstate_ut_check_freq      success!

        + tbench

         When you finish test, you will get selftest.tbench.csv and png images.
         The selftest.tbench.csv file contains the raw data and the drop of the comparative test.
         The png images shows the performance, energy and performan per watt of each test.
         Open selftest.tbench.csv :

         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + Governor                                        | Round        | Des-perf | Freq    | Load     | Performance | Energy  | Performance Per Watt |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + Unit                                            |              |          | GHz     |          | MB/s        | J       | MB/J                 |
         +=================================================+==============+==========+=========+==========+=============+=========+======================+
         + amd-pstate-ondemand                             | 1            |          |         |          | 2504.05     | 1563.67 | 158.5378             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | 2            |          |         |          | 2243.64     | 1430.32 | 155.2941             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | 3            |          |         |          | 2183.88     | 1401.32 | 154.2860             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | Average      |          |         |          | 2310.52     | 1465.1  | 156.1268             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 1            | 165.329  | 1.62257 | 99.798   | 2136.54     | 1395.26 | 151.5971             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 2            | 166      | 1.49761 | 99.9993  | 2100.56     | 1380.5  | 150.6377             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 3            | 166      | 1.47806 | 99.9993  | 2084.12     | 1375.76 | 149.9737             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | Average      | 165.776  | 1.53275 | 99.9322  | 2107.07     | 1383.84 | 150.7399             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 1            |          |         |          | 2529.9      | 1564.4  | 160.0997             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 2            |          |         |          | 2249.76     | 1432.97 | 155.4297             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 3            |          |         |          | 2181.46     | 1406.88 | 153.5060             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | Average      |          |         |          | 2320.37     | 1468.08 | 156.4741             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 1            |          |         |          | 2137.64     | 1385.24 | 152.7723             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 2            |          |         |          | 2107.05     | 1372.23 | 152.0138             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 3            |          |         |          | 2085.86     | 1365.35 | 151.2433             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | Average      |          |         |          | 2110.18     | 1374.27 | 152.0136             |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand VS acpi-cpufreq-schedutil | Comprison(%) |          |         |          | -9.0584     | -6.3899 | -2.8506              |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand VS amd-pstate-schedutil     | Comprison(%) |          |         |          | 8.8053      | -5.5463 | -3.4503              |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand VS amd-pstate-ondemand    | Comprison(%) |          |         |          | -0.4245     | -0.2029 | -0.2219              |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil VS amd-pstate-schedutil  | Comprison(%) |          |         |          | -0.1473     | 0.6963  | -0.8378              |
         +-------------------------------------------------+--------------+----------+---------+----------+-------------+---------+----------------------+

        + gitsource

         When you finish test, you will get selftest.gitsource.csv and png images.
         The selftest.gitsource.csv file contains the raw data and the drop of the comparative test.
         The png images shows the performance, energy and performan per watt of each test.
         Open selftest.gitsource.csv :

         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + Governor                                        | Round        | Des-perf | Freq     | Load     | Time        | Energy  | Performance Per Watt |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + Unit                                            |              |          | GHz      |          | s           | J       | 1/J                  |
         +=================================================+==============+==========+==========+==========+=============+=========+======================+
         + amd-pstate-ondemand                             | 1            | 50.119   | 2.10509  | 23.3076  | 475.69      | 865.78  | 0.001155027          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | 2            | 94.8006  | 1.98771  | 56.6533  | 467.1       | 839.67  | 0.001190944          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | 3            | 76.6091  | 2.53251  | 43.7791  | 467.69      | 855.85  | 0.001168429          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand                             | Average      | 73.8429  | 2.20844  | 41.2467  | 470.16      | 853.767 | 0.001171279          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 1            | 165.919  | 1.62319  | 98.3868  | 464.17      | 866.8   | 0.001153668          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 2            | 165.97   | 1.31309  | 99.5712  | 480.15      | 880.4   | 0.001135847          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | 3            | 165.973  | 1.28448  | 99.9252  | 481.79      | 867.02  | 0.001153375          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-schedutil                            | Average      | 165.954  | 1.40692  | 99.2944  | 475.37      | 871.407 | 0.001147569          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 1            |          |          |          | 2379.62     | 742.96  | 0.001345967          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 2            |          |          |          | 441.74      | 817.49  | 0.001223256          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | 3            |          |          |          | 455.48      | 820.01  | 0.001219497          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand                           | Average      |          |          |          | 425.613     | 793.487 | 0.001260260          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 1            |          |          |          | 459.69      | 838.54  | 0.001192548          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 2            |          |          |          | 466.55      | 830.89  | 0.001203528          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | 3            |          |          |          | 470.38      | 837.32  | 0.001194286          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil                          | Average      |          |          |          | 465.54      | 835.583 | 0.001196769          |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand VS acpi-cpufreq-schedutil | Comprison(%) |          |          |          | 9.3810      | 5.3051  | -5.0379              |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + amd-pstate-ondemand VS amd-pstate-schedutil     | Comprison(%) | 124.7392 | -36.2934 | 140.7329 | 1.1081      | 2.0661  | -2.0242              |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-ondemand VS amd-pstate-ondemand    | Comprison(%) |          |          |          | 10.4665     | 7.5968  | -7.0605              |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+
         + acpi-cpufreq-schedutil VS amd-pstate-schedutil  | Comprison(%) |          |          |          | 2.1115      | 4.2873  | -4.1110              |
         +-------------------------------------------------+--------------+----------+----------+----------+-------------+---------+----------------------+

Reference
===========

.. [1] AMD64 Architecture Programmer's Manual Volume 2: System Programming,
       https://www.amd.com/system/files/TechDocs/24593.pdf

.. [2] Advanced Configuration and Power Interface Specification,
       https://uefi.org/sites/default/files/resources/ACPI_Spec_6_4_Jan22.pdf

.. [3] Processor Programming Reference (PPR) for AMD Family 19h Model 51h, Revision A1 Processors
       https://www.amd.com/system/files/TechDocs/56569-A1-PUB.zip

.. [4] Linux Kernel Selftests,
       https://www.kernel.org/doc/html/latest/dev-tools/kselftest.html
