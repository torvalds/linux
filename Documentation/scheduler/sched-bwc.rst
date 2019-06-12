=====================
CFS Bandwidth Control
=====================

[ This document only discusses CPU bandwidth control for SCHED_NORMAL.
  The SCHED_RT case is covered in Documentation/scheduler/sched-rt-group.rst ]

CFS bandwidth control is a CONFIG_FAIR_GROUP_SCHED extension which allows the
specification of the maximum CPU bandwidth available to a group or hierarchy.

The bandwidth allowed for a group is specified using a quota and period. Within
each given "period" (microseconds), a group is allowed to consume only up to
"quota" microseconds of CPU time.  When the CPU bandwidth consumption of a
group exceeds this limit (for that period), the tasks belonging to its
hierarchy will be throttled and are not allowed to run again until the next
period.

A group's unused runtime is globally tracked, being refreshed with quota units
above at each period boundary.  As threads consume this bandwidth it is
transferred to cpu-local "silos" on a demand basis.  The amount transferred
within each of these updates is tunable and described as the "slice".

Management
----------
Quota and period are managed within the cpu subsystem via cgroupfs.

cpu.cfs_quota_us: the total available run-time within a period (in microseconds)
cpu.cfs_period_us: the length of a period (in microseconds)
cpu.stat: exports throttling statistics [explained further below]

The default values are::

	cpu.cfs_period_us=100ms
	cpu.cfs_quota=-1

A value of -1 for cpu.cfs_quota_us indicates that the group does not have any
bandwidth restriction in place, such a group is described as an unconstrained
bandwidth group.  This represents the traditional work-conserving behavior for
CFS.

Writing any (valid) positive value(s) will enact the specified bandwidth limit.
The minimum quota allowed for the quota or period is 1ms.  There is also an
upper bound on the period length of 1s.  Additional restrictions exist when
bandwidth limits are used in a hierarchical fashion, these are explained in
more detail below.

Writing any negative value to cpu.cfs_quota_us will remove the bandwidth limit
and return the group to an unconstrained state once more.

Any updates to a group's bandwidth specification will result in it becoming
unthrottled if it is in a constrained state.

System wide settings
--------------------
For efficiency run-time is transferred between the global pool and CPU local
"silos" in a batch fashion.  This greatly reduces global accounting pressure
on large systems.  The amount transferred each time such an update is required
is described as the "slice".

This is tunable via procfs::

	/proc/sys/kernel/sched_cfs_bandwidth_slice_us (default=5ms)

Larger slice values will reduce transfer overheads, while smaller values allow
for more fine-grained consumption.

Statistics
----------
A group's bandwidth statistics are exported via 3 fields in cpu.stat.

cpu.stat:

- nr_periods: Number of enforcement intervals that have elapsed.
- nr_throttled: Number of times the group has been throttled/limited.
- throttled_time: The total time duration (in nanoseconds) for which entities
  of the group have been throttled.

This interface is read-only.

Hierarchical considerations
---------------------------
The interface enforces that an individual entity's bandwidth is always
attainable, that is: max(c_i) <= C. However, over-subscription in the
aggregate case is explicitly allowed to enable work-conserving semantics
within a hierarchy:

  e.g. \Sum (c_i) may exceed C

[ Where C is the parent's bandwidth, and c_i its children ]


There are two ways in which a group may become throttled:

	a. it fully consumes its own quota within a period
	b. a parent's quota is fully consumed within its period

In case b) above, even though the child may have runtime remaining it will not
be allowed to until the parent's runtime is refreshed.

Examples
--------
1. Limit a group to 1 CPU worth of runtime::

	If period is 250ms and quota is also 250ms, the group will get
	1 CPU worth of runtime every 250ms.

	# echo 250000 > cpu.cfs_quota_us /* quota = 250ms */
	# echo 250000 > cpu.cfs_period_us /* period = 250ms */

2. Limit a group to 2 CPUs worth of runtime on a multi-CPU machine

   With 500ms period and 1000ms quota, the group can get 2 CPUs worth of
   runtime every 500ms::

	# echo 1000000 > cpu.cfs_quota_us /* quota = 1000ms */
	# echo 500000 > cpu.cfs_period_us /* period = 500ms */

	The larger period here allows for increased burst capacity.

3. Limit a group to 20% of 1 CPU.

   With 50ms period, 10ms quota will be equivalent to 20% of 1 CPU::

	# echo 10000 > cpu.cfs_quota_us /* quota = 10ms */
	# echo 50000 > cpu.cfs_period_us /* period = 50ms */

   By using a small period here we are ensuring a consistent latency
   response at the expense of burst capacity.
