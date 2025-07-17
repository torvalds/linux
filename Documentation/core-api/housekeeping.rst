======================================
Housekeeping
======================================


CPU Isolation moves away kernel work that may otherwise run on any CPU.
The purpose of its related features is to reduce the OS jitter that some
extreme workloads can't stand, such as in some DPDK usecases.

The kernel work moved away by CPU isolation is commonly described as
"housekeeping" because it includes ground work that performs cleanups,
statistics maintainance and actions relying on them, memory release,
various deferrals etc...

Sometimes housekeeping is just some unbound work (unbound workqueues,
unbound timers, ...) that gets easily assigned to non-isolated CPUs.
But sometimes housekeeping is tied to a specific CPU and requires
elaborated tricks to be offloaded to non-isolated CPUs (RCU_NOCB, remote
scheduler tick, etc...).

Thus, a housekeeping CPU can be considered as the reverse of an isolated
CPU. It is simply a CPU that can execute housekeeping work. There must
always be at least one online housekeeping CPU at any time. The CPUs that
are not	isolated are automatically assigned as housekeeping.

Housekeeping is currently divided in four features described
by the ``enum hk_type type``:

1.	HK_TYPE_DOMAIN matches the work moved away by scheduler domain
	isolation performed through ``isolcpus=domain`` boot parameter or
	isolated cpuset partitions in cgroup v2. This includes scheduler
	load balancing, unbound workqueues and timers.

2.	HK_TYPE_KERNEL_NOISE matches the work moved away by tick isolation
	performed through ``nohz_full=`` or ``isolcpus=nohz`` boot
	parameters. This includes remote scheduler tick, vmstat and lockup
	watchdog.

3.	HK_TYPE_MANAGED_IRQ matches the IRQ handlers moved away by managed
	IRQ isolation performed through ``isolcpus=managed_irq``.

4.	HK_TYPE_DOMAIN_BOOT matches the work moved away by scheduler domain
	isolation performed through ``isolcpus=domain`` only. It is similar
	to HK_TYPE_DOMAIN except it ignores the isolation performed by
	cpusets.


Housekeeping cpumasks
=================================

Housekeeping cpumasks include the CPUs that can execute the work moved
away by the matching isolation feature. These cpumasks are returned by
the following function::

	const struct cpumask *housekeeping_cpumask(enum hk_type type)

By default, if neither ``nohz_full=``, nor ``isolcpus``, nor cpuset's
isolated partitions are used, which covers most usecases, this function
returns the cpu_possible_mask.

Otherwise the function returns the cpumask complement of the isolation
feature. For example:

With isolcpus=domain,7 the following will return a mask with all possible
CPUs except 7::

	housekeeping_cpumask(HK_TYPE_DOMAIN)

Similarly with nohz_full=5,6 the following will return a mask with all
possible CPUs except 5,6::

	housekeeping_cpumask(HK_TYPE_KERNEL_NOISE)


Synchronization against cpusets
=================================

Cpuset can modify the HK_TYPE_DOMAIN housekeeping cpumask while creating,
modifying or deleting an isolated partition.

The users of HK_TYPE_DOMAIN cpumask must then make sure to synchronize
properly against cpuset in order to make sure that:

1.	The cpumask snapshot stays coherent.

2.	No housekeeping work is queued on a newly made isolated CPU.

3.	Pending housekeeping work that was queued to a non isolated
	CPU which just turned isolated through cpuset must be flushed
	before the related created/modified isolated partition is made
	available to userspace.

This synchronization is maintained by an RCU based scheme. The cpuset update
side waits for an RCU grace period after updating the HK_TYPE_DOMAIN
cpumask and before flushing pending works. On the read side, care must be
taken to gather the housekeeping target election and the work enqueue within
the same RCU read side critical section.

A typical layout example would look like this on the update side
(``housekeeping_update()``)::

	rcu_assign_pointer(housekeeping_cpumasks[type], trial);
	synchronize_rcu();
	flush_workqueue(example_workqueue);

And then on the read side::

	rcu_read_lock();
	cpu = housekeeping_any_cpu(HK_TYPE_DOMAIN);
	queue_work_on(cpu, example_workqueue, work);
	rcu_read_unlock();
