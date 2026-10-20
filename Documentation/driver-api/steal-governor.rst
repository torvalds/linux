.. SPDX-License-Identifier: GPL-2.0

Steal Governor
==============

:Author: Shrikanth Hegde <sshegde@linux.ibm.com>

Introduction
============

The steal governor is aimed at mitigating the Noisy Neighbour problem
which occurs in paravirtualized environments with CPU overcommit.
The performance of a workload running in one VM gets degraded by
the activity of other VMs on the same host. As a result, all VMs
collectively make slower forward progress.

In such systems, high utilization in all VMs causes the hypervisor to
frequently preempt vCPUs. This vCPU preemption is expensive.
To mitigate this, the kernel aims to restrict workloads to a subset of
Preferred CPUs to reduce physical CPU contention.
A detailed explanation of Preferred CPUs is available in
``Documentation/scheduler/sched-paravirt.rst``.

The steal governor selects ``CONFIG_PREFERRED_CPU=y`` which enables the
scheduler core infrastructure to move the tasks to Preferred CPUs where
possible. The driver controls the policy decisions regarding the state of
preferred CPUs. That is, this driver decides which CPUs are preferred
and which CPUs are non-preferred.

The driver code is available at ``drivers/virt/steal_governor.c``.

Core idea
=========

steal time is an indication available today in Guest which shows contention
for underlying physical CPU. Use it as a hint in the guest to fold the
workload to a reduced set of vCPUs. When there is contention, steal time
will show up in all the guests. When each guest honors the hint and folds
the workload to a smaller set of vCPUs (Preferred CPUs), it reduces the
contention and thereby reduces vCPU preemption.
This is achieved without any cross-guest communication.

Steal governor driver effectively does:

1. Periodically computes the steal ratio using accumulated steal time
   across possible CPUs, normalized by the number of active CPUs.

2. If steal ratio is greater than high threshold, reduce the number of
   preferred CPUs by 1 core. Ensure at least one core is left always.
   Skip changing the state of offline CPUs in that core.

3. If steal ratio is less than or equal to low threshold, increase the
   number of preferred CPUs by 1 core. If preferred is same as active,
   nothing to be done. Skip changing the state of offline CPUs.
   This helps to handle cases where few CPUs are offline in a core and
   those offline CPUs will not be marked as preferred.

4. Ensure preferred CPUs is always subset of active CPUs.
   On feature disable it is same as active CPUs.

This feature works best only when all the VMs enable the feature as
it is a co-operative scheme. If a specific VM doesn't enable this feature
it may end up with more CPUs than others, still should lead to better
performance when seen from system view. Those who enable this driver must
ensure it is enabled in all VMs.

Note that this driver is strictly intended for actual guests; for example,
loading this module in a privileged VM like Xen Dom0 is blocked.

Workload considerations
=======================

The steal governor is useful for workloads where vCPU preemption has
costs beyond the lost CPU time, such as lock-holder preemption, critical
sections, communicating threads, and cache or TLB disruption.

Pure CPU-time workloads with independent workers may not benefit and
could see a small regression due to additional guest scheduling overhead.

Module Parameters
=================

interval_ms
-----------

How often steal governor checks for steal time.
Default: 1000 i.e. 1 second. Value should be in between 100ms to 100sec.

This controls how fast steal governor driver reacts to changes to the
contention of physical CPUs. Since it does a fair amount of work, setting
too low may have overhead. Setting it too high might render it ineffective.

low_threshold
-------------

lower threshold value in percentage * 100.
Default: 200, i.e. 2% steal is considered as low threshold.
Can't be higher than high_threshold.

This determines what values should be considered as nil/no steal values.
When steal governor sees steal ratio is less than or equal to this value,
it will increase the preferred CPUs by 1 core.
Using zero might cause oscillations.

high_threshold
--------------

higher threshold value in percentage * 100
Default: 500, i.e. 5% steal is considered as high threshold.
Can't be lower than low_threshold. Must be less than 10000.

This determines what values should be considered as high steal values.
When steal governor sees steal ratio is higher than this value, it will
reduce the preferred CPUs by 1 core.

Limitations of default values
-----------------------------

Because of the vast diversity in VM configurations and different
architectures, the default thresholds may not be optimal for all systems.
Users may need to tune these parameters based on the system under
test to achieve the best results.

The governor sums the steal time across all possible CPUs, which ensures
the accumulated steal time remains a monotonically increasing value.
However, to calculate the effective steal ratio, it divides this sum
by the number of active CPUs. Because only active CPUs contribute to
the steal time delta, this prevents threshold dilution on sparsely
populated systems.

The driver reduces/increases preferred CPUs by core-level. This could provide
faster convergence for hypervisors such as powerVM. But on KVM and Xen
convergence could be slower depending on the configuration.
Using a smaller interval_ms could help one to expedite it.

Reasons for CONFIG_STEAL_GOVERNOR=m
===================================

Selecting this driver makes CONFIG_PREFERRED_CPU=y. That makes configs
driven by user preference. Though one can have CONFIG_STEAL_GOVERNOR=y,
It is recommended to build CONFIG_STEAL_GOVERNOR=m due to below reasons:

1. Doing periodic work has additional overheads. Enabling this driver
   in systems where steal time cannot happen is of no use. There is no
   benefit with additional overheads in such systems.

2. This works well when all VMs work in a co-operative manner. When an
   administrative user enables it in one VM, he/she will likely enable
   it in all VMs.

3. User can tweak the module parameters by reloading the module.
