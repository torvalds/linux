.. SPDX-License-Identifier: GPL-2.0
.. _sched-paravirt:

Preferred CPUs
==============

In paravirtualized environments CPU overcommit is a common scenario.
i.e. the sum of virtual CPUs (vCPUs) of all VMs is greater than number of
physical CPUs (pCPUs). Under such conditions when all or many VMs have
high utilization, hypervisor won't be able to satisfy the CPU requirement
and has to context switch within or across VMs. The hypervisor needs to
preempt one vCPU to run another. This is called vCPU preemption.
This is more expensive compared to task context switch within a vCPU, since
hypervisor lacks vCPU context and could preempt a critical section which
slows forward progress.

In such cases it is better that combined vCPU demand from all VMs is reduced
by not using some of the vCPUs in each VM. vCPUs where workload can be safely
scheduled which won't increase any contention for pCPU are called
"Preferred CPUs".

One of the main design constructs is that preferred CPUs are always
a subset of active CPUs. In most cases preferred CPUs will be same as
active CPUs. When there is pCPU contention, Preferred CPUs will reduce
based on the steal time. When the pCPU contention goes away as indicated
by steal time, Preferred CPUs could become same as active CPUs again.
The policy decisions are to be taken by driver.
For example, steal_governor. Look at its documentation for more
details. (``drivers/virt/steal_governor.c``)

Scheduling decisions such as wakeup, pushing the task etc, need this
CPU state info. This is maintained in ``cpu_preferred_mask``.
vCPUs which are not in ``cpu_preferred_mask`` should be treated as vCPUs which
should not be used at this moment provided it doesn't break user affinity.

This is achieved by:

1. Selecting a preferred CPU at wakeup using fallback mechanism.
2. Pushing the task away from non-preferred CPU at tick.
3. Selecting only preferred CPUs for load balance.

``/sys/devices/system/cpu/preferred`` prints the current ``cpu_preferred_mask``
in cpulist format.

Notes:

1. This feature is available under ``CONFIG_PREFERRED_CPU``. Driver which
   makes decisions should enable it. For example, steal_governor driver
   (``CONFIG_STEAL_GOVERNOR``). On enabling the driver, CPU preferred state
   can change based on steal time. Without the driver, preferred CPUs is
   same as active CPUs.

2. This feature works for the FAIR class only.

3. A pinned task, which can't be moved to preferred CPUs will continue
   to run based on its affinity. But no load balancing happens if it is affined
   only on non-preferred CPUs.

4. Decision to change the preferred CPU state is driven by the kernel.
   Hence it shouldn't break user affinities. One of the main reasons why
   CPU hotplug or Isolated cpuset partitions was not a solution.

5. This feature works best only when all the Guest VMs enable the feature as
   it is a co-operative scheme. If a specific VM doesn't enable this feature
   it may end up with more CPUs than others, still should lead to better
   performance when seen from system view.
   Users who enable this driver must ensure it is enabled in all Guest VMs.
