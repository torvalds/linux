=========
Workqueue
=========

:Date: September, 2010
:Author: Tejun Heo <tj@kernel.org>
:Author: Florian Mickler <florian@mickler.org>


Introduction
============

There are many cases where an asynchronous process execution context
is needed and the workqueue (wq) API is the most commonly used
mechanism for such cases.

When such an asynchronous execution context is needed, a work item
describing which function to execute is put on a queue.  An
independent thread serves as the asynchronous execution context.  The
queue is called workqueue and the thread is called worker.

While there are work items on the workqueue the worker executes the
functions associated with the work items one after the other.  When
there is no work item left on the workqueue the worker becomes idle.
When a new work item gets queued, the worker begins executing again.


Why Concurrency Managed Workqueue?
==================================

In the original wq implementation, a multi threaded (MT) wq had one
worker thread per CPU and a single threaded (ST) wq had one worker
thread system-wide.  A single MT wq needed to keep around the same
number of workers as the number of CPUs.  The kernel grew a lot of MT
wq users over the years and with the number of CPU cores continuously
rising, some systems saturated the default 32k PID space just booting
up.

Although MT wq wasted a lot of resource, the level of concurrency
provided was unsatisfactory.  The limitation was common to both ST and
MT wq albeit less severe on MT.  Each wq maintained its own separate
worker pool.  An MT wq could provide only one execution context per CPU
while an ST wq one for the whole system.  Work items had to compete for
those very limited execution contexts leading to various problems
including proneness to deadlocks around the single execution context.

The tension between the provided level of concurrency and resource
usage also forced its users to make unnecessary tradeoffs like libata
choosing to use ST wq for polling PIOs and accepting an unnecessary
limitation that no two polling PIOs can progress at the same time.  As
MT wq don't provide much better concurrency, users which require
higher level of concurrency, like async or fscache, had to implement
their own thread pool.

Concurrency Managed Workqueue (cmwq) is a reimplementation of wq with
focus on the following goals.

* Maintain compatibility with the original workqueue API.

* Use per-CPU unified worker pools shared by all wq to provide
  flexible level of concurrency on demand without wasting a lot of
  resource.

* Automatically regulate worker pool and level of concurrency so that
  the API users don't need to worry about such details.


The Design
==========

In order to ease the asynchronous execution of functions a new
abstraction, the work item, is introduced.

A work item is a simple struct that holds a pointer to the function
that is to be executed asynchronously.  Whenever a driver or subsystem
wants a function to be executed asynchronously it has to set up a work
item pointing to that function and queue that work item on a
workqueue.

A work item can be executed in either a thread or the BH (softirq) context.

For threaded workqueues, special purpose threads, called [k]workers, execute
the functions off of the queue, one after the other. If no work is queued,
the worker threads become idle. These worker threads are managed in
worker-pools.

The cmwq design differentiates between the user-facing workqueues that
subsystems and drivers queue work items on and the backend mechanism
which manages worker-pools and processes the queued work items.

There are two worker-pools, one for normal work items and the other
for high priority ones, for each possible CPU and some extra
worker-pools to serve work items queued on unbound workqueues - the
number of these backing pools is dynamic.

BH workqueues use the same framework. However, as there can only be one
concurrent execution context, there's no need to worry about concurrency.
Each per-CPU BH worker pool contains only one pseudo worker which represents
the BH execution context. A BH workqueue can be considered a convenience
interface to softirq.

Subsystems and drivers can create and queue work items through special
workqueue API functions as they see fit. They can influence some
aspects of the way the work items are executed by setting flags on the
workqueue they are putting the work item on. These flags include
things like CPU locality, concurrency limits, priority and more.  To
get a detailed overview refer to the API description of
``alloc_workqueue()`` below.

When a work item is queued to a workqueue, the target worker-pool is
determined according to the queue parameters and workqueue attributes
and appended on the shared worklist of the worker-pool.  For example,
unless specifically overridden, a work item of a bound workqueue will
be queued on the worklist of either normal or highpri worker-pool that
is associated to the CPU the issuer is running on.

For any thread pool implementation, managing the concurrency level
(how many execution contexts are active) is an important issue.  cmwq
tries to keep the concurrency at a minimal but sufficient level.
Minimal to save resources and sufficient in that the system is used at
its full capacity.

Each worker-pool bound to an actual CPU implements concurrency
management by hooking into the scheduler.  The worker-pool is notified
whenever an active worker wakes up or sleeps and keeps track of the
number of the currently runnable workers.  Generally, work items are
not expected to hog a CPU and consume many cycles.  That means
maintaining just enough concurrency to prevent work processing from
stalling should be optimal.  As long as there are one or more runnable
workers on the CPU, the worker-pool doesn't start execution of a new
work, but, when the last running worker goes to sleep, it immediately
schedules a new worker so that the CPU doesn't sit idle while there
are pending work items.  This allows using a minimal number of workers
without losing execution bandwidth.

Keeping idle workers around doesn't cost other than the memory space
for kthreads, so cmwq holds onto idle ones for a while before killing
them.

For unbound workqueues, the number of backing pools is dynamic.
Unbound workqueue can be assigned custom attributes using
``apply_workqueue_attrs()`` and workqueue will automatically create
backing worker pools matching the attributes.  The responsibility of
regulating concurrency level is on the users.  There is also a flag to
mark a bound wq to ignore the concurrency management.  Please refer to
the API section for details.

Forward progress guarantee relies on that workers can be created when
more execution contexts are necessary, which in turn is guaranteed
through the use of rescue workers.  All work items which might be used
on code paths that handle memory reclaim are required to be queued on
wq's that have a rescue-worker reserved for execution under memory
pressure.  Else it is possible that the worker-pool deadlocks waiting
for execution contexts to free up.


Application Programming Interface (API)
=======================================

``alloc_workqueue()`` allocates a wq.  The original
``create_*workqueue()`` functions are deprecated and scheduled for
removal.  ``alloc_workqueue()`` takes three arguments - ``@name``,
``@flags`` and ``@max_active``.  ``@name`` is the name of the wq and
also used as the name of the rescuer thread if there is one.

A wq no longer manages execution resources but serves as a domain for
forward progress guarantee, flush and work item attributes. ``@flags``
and ``@max_active`` control how work items are assigned execution
resources, scheduled and executed.


``flags``
---------

``WQ_BH``
  BH workqueues can be considered a convenience interface to softirq. BH
  workqueues are always per-CPU and all BH work items are executed in the
  queueing CPU's softirq context in the queueing order.

  All BH workqueues must have 0 ``max_active`` and ``WQ_HIGHPRI`` is the
  only allowed additional flag.

  BH work items cannot sleep. All other features such as delayed queueing,
  flushing and canceling are supported.

``WQ_PERCPU``
  Work items queued to a per-cpu wq are bound to a specific CPU.
  This flag is the right choice when cpu locality is important.

  This flag is the complement of ``WQ_UNBOUND``.

``WQ_UNBOUND``
  Work items queued to an unbound wq are served by the special
  worker-pools which host workers which are not bound to any
  specific CPU.  This makes the wq behave as a simple execution
  context provider without concurrency management.  The unbound
  worker-pools try to start execution of work items as soon as
  possible.  Unbound wq sacrifices locality but is useful for
  the following cases.

  * Wide fluctuation in the concurrency level requirement is
    expected and using bound wq may end up creating large number
    of mostly unused workers across different CPUs as the issuer
    hops through different CPUs.

  * Long running CPU intensive workloads which can be better
    managed by the system scheduler.

``WQ_FREEZABLE``
  A freezable wq participates in the freeze phase of the system
  suspend operations.  Work items on the wq are drained and no
  new work item starts execution until thawed.

``WQ_MEM_RECLAIM``
  All wq which might be used in the memory reclaim paths **MUST**
  have this flag set.  The wq is guaranteed to have at least one
  execution context regardless of memory pressure.

``WQ_HIGHPRI``
  Work items of a highpri wq are queued to the highpri
  worker-pool of the target cpu.  Highpri worker-pools are
  served by worker threads with elevated nice level.

  Note that normal and highpri worker-pools don't interact with
  each other.  Each maintains its separate pool of workers and
  implements concurrency management among its workers.

``WQ_CPU_INTENSIVE``
  Work items of a CPU intensive wq do not contribute to the
  concurrency level.  In other words, runnable CPU intensive
  work items will not prevent other work items in the same
  worker-pool from starting execution.  This is useful for bound
  work items which are expected to hog CPU cycles so that their
  execution is regulated by the system scheduler.

  Although CPU intensive work items don't contribute to the
  concurrency level, start of their executions is still
  regulated by the concurrency management and runnable
  non-CPU-intensive work items can delay execution of CPU
  intensive work items.

  This flag is meaningless for unbound wq.


``max_active``
--------------

``@max_active`` determines the maximum number of execution contexts per
CPU which can be assigned to the work items of a wq. For example, with
``@max_active`` of 16, at most 16 work items of the wq can be executing
at the same time per CPU. This is always a per-CPU attribute, even for
unbound workqueues.

The maximum limit for ``@max_active`` is 2048 and the default value used
when 0 is specified is 1024. These values are chosen sufficiently high
such that they are not the limiting factor while providing protection in
runaway cases.

The number of active work items of a wq is usually regulated by the
users of the wq, more specifically, by how many work items the users
may queue at the same time.  Unless there is a specific need for
throttling the number of active work items, specifying '0' is
recommended.

Some users depend on strict execution ordering where only one work item
is in flight at any given time and the work items are processed in
queueing order. While the combination of ``@max_active`` of 1 and
``WQ_UNBOUND`` used to achieve this behavior, this is no longer the
case. Use alloc_ordered_workqueue() instead.


Example Execution Scenarios
===========================

The following example execution scenarios try to illustrate how cmwq
behave under different configurations.

 Work items w0, w1, w2 are queued to a bound wq q0 on the same CPU.
 w0 burns CPU for 5ms then sleeps for 10ms then burns CPU for 5ms
 again before finishing.  w1 and w2 burn CPU for 5ms then sleep for
 10ms.

Ignoring all other tasks, works and processing overhead, and assuming
simple FIFO scheduling, the following is one highly simplified version
of possible sequences of events with the original wq. ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 starts and burns CPU
 25		w1 sleeps
 35		w1 wakes up and finishes
 35		w2 starts and burns CPU
 40		w2 sleeps
 50		w2 wakes up and finishes

And with cmwq with ``@max_active`` >= 3, ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 10		w2 starts and burns CPU
 15		w2 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 25		w2 wakes up and finishes

If ``@max_active`` == 2, ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 20		w2 starts and burns CPU
 25		w2 sleeps
 35		w2 wakes up and finishes

Now, let's assume w1 and w2 are queued to a different wq q1 which has
``WQ_CPU_INTENSIVE`` set, ::

 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 and w2 start and burn CPU
 10		w1 sleeps
 15		w2 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 25		w2 wakes up and finishes


Guidelines
==========

* Do not forget to use ``WQ_MEM_RECLAIM`` if a wq may process work
  items which are used during memory reclaim.  Each wq with
  ``WQ_MEM_RECLAIM`` set has an execution context reserved for it.  If
  there is dependency among multiple work items used during memory
  reclaim, they should be queued to separate wq each with
  ``WQ_MEM_RECLAIM``.

* Unless strict ordering is required, there is no need to use ST wq.

* Unless there is a specific need, using 0 for @max_active is
  recommended.  In most use cases, concurrency level usually stays
  well under the default limit.

* A wq serves as a domain for forward progress guarantee
  (``WQ_MEM_RECLAIM``, flush and work item attributes.  Work items
  which are not involved in memory reclaim and don't need to be
  flushed as a part of a group of work items, and don't require any
  special attribute, can use one of the system wq.  There is no
  difference in execution characteristics between using a dedicated wq
  and a system wq.

  Note: If something may generate more than @max_active outstanding
  work items (do stress test your producers), it may saturate a system
  wq and potentially lead to deadlock. It should utilize its own
  dedicated workqueue rather than the system wq.

* Unless work items are expected to consume a huge amount of CPU
  cycles, using a bound wq is usually beneficial due to the increased
  level of locality in wq operations and work item execution.


Affinity Scopes
===============

An unbound workqueue groups CPUs according to its affinity scope to improve
cache locality. For example, if a workqueue is using the default affinity
scope of "cache", it will group CPUs according to last level cache
boundaries. A work item queued on the workqueue will be assigned to a worker
on one of the CPUs which share the last level cache with the issuing CPU.
Once started, the worker may or may not be allowed to move outside the scope
depending on the ``affinity_strict`` setting of the scope.

Workqueue currently supports the following affinity scopes.

``default``
  Use the scope in module parameter ``workqueue.default_affinity_scope``
  which is always set to one of the scopes below.

``cpu``
  CPUs are not grouped. A work item issued on one CPU is processed by a
  worker on the same CPU. This makes unbound workqueues behave as per-cpu
  workqueues without concurrency management.

``smt``
  CPUs are grouped according to SMT boundaries. This usually means that the
  logical threads of each physical CPU core are grouped together.

``cache``
  CPUs are grouped according to cache boundaries. Which specific cache
  boundary is used is determined by the arch code. L3 is used in a lot of
  cases. This is the default affinity scope.

``numa``
  CPUs are grouped according to NUMA boundaries.

``system``
  All CPUs are put in the same group. Workqueue makes no effort to process a
  work item on a CPU close to the issuing CPU.

The default affinity scope can be changed with the module parameter
``workqueue.default_affinity_scope`` and a specific workqueue's affinity
scope can be changed using ``apply_workqueue_attrs()``.

If ``WQ_SYSFS`` is set, the workqueue will have the following affinity scope
related interface files under its ``/sys/devices/virtual/workqueue/WQ_NAME/``
directory.

``affinity_scope``
  Read to see the current affinity scope. Write to change.

  When default is the current scope, reading this file will also show the
  current effective scope in parentheses, for example, ``default (cache)``.

``affinity_strict``
  0 by default indicating that affinity scopes are not strict. When a work
  item starts execution, workqueue makes a best-effort attempt to ensure
  that the worker is inside its affinity scope, which is called
  repatriation. Once started, the scheduler is free to move the worker
  anywhere in the system as it sees fit. This enables benefiting from scope
  locality while still being able to utilize other CPUs if necessary and
  available.

  If set to 1, all workers of the scope are guaranteed always to be in the
  scope. This may be useful when crossing affinity scopes has other
  implications, for example, in terms of power consumption or workload
  isolation. Strict NUMA scope can also be used to match the workqueue
  behavior of older kernels.


Affinity Scopes and Performance
===============================

It'd be ideal if an unbound workqueue's behavior is optimal for vast
majority of use cases without further tuning. Unfortunately, in the current
kernel, there exists a pronounced trade-off between locality and utilization
necessitating explicit configurations when workqueues are heavily used.

Higher locality leads to higher efficiency where more work is performed for
the same number of consumed CPU cycles. However, higher locality may also
cause lower overall system utilization if the work items are not spread
enough across the affinity scopes by the issuers. The following performance
testing with dm-crypt clearly illustrates this trade-off.

The tests are run on a CPU with 12-cores/24-threads split across four L3
caches (AMD Ryzen 9 3900x). CPU clock boost is turned off for consistency.
``/dev/dm-0`` is a dm-crypt device created on NVME SSD (Samsung 990 PRO) and
opened with ``cryptsetup`` with default settings.


Scenario 1: Enough issuers and work spread across the machine
-------------------------------------------------------------

The command used: ::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k --ioengine=libaio \
    --iodepth=64 --runtime=60 --numjobs=24 --time_based --group_reporting \
    --name=iops-test-job --verify=sha512

There are 24 issuers, each issuing 64 IOs concurrently. ``--verify=sha512``
makes ``fio`` generate and read back the content each time which makes
execution locality matter between the issuer and ``kcryptd``. The following
are the read bandwidths and CPU utilizations depending on different affinity
scope settings on ``kcryptd`` measured over five runs. Bandwidths are in
MiBps, and CPU util in percents.

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - Affinity
     - Bandwidth (MiBps)
     - CPU util (%)

   * - system
     - 1159.40 ±1.34
     - 99.31 ±0.02

   * - cache
     - 1166.40 ±0.89
     - 99.34 ±0.01

   * - cache (strict)
     - 1166.00 ±0.71
     - 99.35 ±0.01

With enough issuers spread across the system, there is no downside to
"cache", strict or otherwise. All three configurations saturate the whole
machine but the cache-affine ones outperform by 0.6% thanks to improved
locality.


Scenario 2: Fewer issuers, enough work for saturation
-----------------------------------------------------

The command used: ::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k \
    --ioengine=libaio --iodepth=64 --runtime=60 --numjobs=8 \
    --time_based --group_reporting --name=iops-test-job --verify=sha512

The only difference from the previous scenario is ``--numjobs=8``. There are
a third of the issuers but is still enough total work to saturate the
system.

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - Affinity
     - Bandwidth (MiBps)
     - CPU util (%)

   * - system
     - 1155.40 ±0.89
     - 97.41 ±0.05

   * - cache
     - 1154.40 ±1.14
     - 96.15 ±0.09

   * - cache (strict)
     - 1112.00 ±4.64
     - 93.26 ±0.35

This is more than enough work to saturate the system. Both "system" and
"cache" are nearly saturating the machine but not fully. "cache" is using
less CPU but the better efficiency puts it at the same bandwidth as
"system".

Eight issuers moving around over four L3 cache scope still allow "cache
(strict)" to mostly saturate the machine but the loss of work conservation
is now starting to hurt with 3.7% bandwidth loss.


Scenario 3: Even fewer issuers, not enough work to saturate
-----------------------------------------------------------

The command used: ::

  $ fio --filename=/dev/dm-0 --direct=1 --rw=randrw --bs=32k \
    --ioengine=libaio --iodepth=64 --runtime=60 --numjobs=4 \
    --time_based --group_reporting --name=iops-test-job --verify=sha512

Again, the only difference is ``--numjobs=4``. With the number of issuers
reduced to four, there now isn't enough work to saturate the whole system
and the bandwidth becomes dependent on completion latencies.

.. list-table::
   :widths: 16 20 20
   :header-rows: 1

   * - Affinity
     - Bandwidth (MiBps)
     - CPU util (%)

   * - system
     - 993.60 ±1.82
     - 75.49 ±0.06

   * - cache
     - 973.40 ±1.52
     - 74.90 ±0.07

   * - cache (strict)
     - 828.20 ±4.49
     - 66.84 ±0.29

Now, the tradeoff between locality and utilization is clearer. "cache" shows
2% bandwidth loss compared to "system" and "cache (struct)" whopping 20%.


Conclusion and Recommendations
------------------------------

In the above experiments, the efficiency advantage of the "cache" affinity
scope over "system" is, while consistent and noticeable, small. However, the
impact is dependent on the distances between the scopes and may be more
pronounced in processors with more complex topologies.

While the loss of work-conservation in certain scenarios hurts, it is a lot
better than "cache (strict)" and maximizing workqueue utilization is
unlikely to be the common case anyway. As such, "cache" is the default
affinity scope for unbound pools.

* As there is no one option which is great for most cases, workqueue usages
  that may consume a significant amount of CPU are recommended to configure
  the workqueues using ``apply_workqueue_attrs()`` and/or enable
  ``WQ_SYSFS``.

* An unbound workqueue with strict "cpu" affinity scope behaves the same as
  ``WQ_CPU_INTENSIVE`` per-cpu workqueue. There is no real advanage to the
  latter and an unbound workqueue provides a lot more flexibility.

* Affinity scopes are introduced in Linux v6.5. To emulate the previous
  behavior, use strict "numa" affinity scope.

* The loss of work-conservation in non-strict affinity scopes is likely
  originating from the scheduler. There is no theoretical reason why the
  kernel wouldn't be able to do the right thing and maintain
  work-conservation in most cases. As such, it is possible that future
  scheduler improvements may make most of these tunables unnecessary.


Examining Configuration
=======================

Use tools/workqueue/wq_dump.py to examine unbound CPU affinity
configuration, worker pools and how workqueues map to the pools: ::

  $ tools/workqueue/wq_dump.py
  Affinity Scopes
  ===============
  wq_unbound_cpumask=0000000f

  CPU
    nr_pods  4
    pod_cpus [0]=00000001 [1]=00000002 [2]=00000004 [3]=00000008
    pod_node [0]=0 [1]=0 [2]=1 [3]=1
    cpu_pod  [0]=0 [1]=1 [2]=2 [3]=3

  SMT
    nr_pods  4
    pod_cpus [0]=00000001 [1]=00000002 [2]=00000004 [3]=00000008
    pod_node [0]=0 [1]=0 [2]=1 [3]=1
    cpu_pod  [0]=0 [1]=1 [2]=2 [3]=3

  CACHE (default)
    nr_pods  2
    pod_cpus [0]=00000003 [1]=0000000c
    pod_node [0]=0 [1]=1
    cpu_pod  [0]=0 [1]=0 [2]=1 [3]=1

  NUMA
    nr_pods  2
    pod_cpus [0]=00000003 [1]=0000000c
    pod_node [0]=0 [1]=1
    cpu_pod  [0]=0 [1]=0 [2]=1 [3]=1

  SYSTEM
    nr_pods  1
    pod_cpus [0]=0000000f
    pod_node [0]=-1
    cpu_pod  [0]=0 [1]=0 [2]=0 [3]=0

  Worker Pools
  ============
  pool[00] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  0
  pool[01] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  0
  pool[02] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  1
  pool[03] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  1
  pool[04] ref= 1 nice=  0 idle/workers=  4/  4 cpu=  2
  pool[05] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  2
  pool[06] ref= 1 nice=  0 idle/workers=  3/  3 cpu=  3
  pool[07] ref= 1 nice=-20 idle/workers=  2/  2 cpu=  3
  pool[08] ref=42 nice=  0 idle/workers=  6/  6 cpus=0000000f
  pool[09] ref=28 nice=  0 idle/workers=  3/  3 cpus=00000003
  pool[10] ref=28 nice=  0 idle/workers= 17/ 17 cpus=0000000c
  pool[11] ref= 1 nice=-20 idle/workers=  1/  1 cpus=0000000f
  pool[12] ref= 2 nice=-20 idle/workers=  1/  1 cpus=00000003
  pool[13] ref= 2 nice=-20 idle/workers=  1/  1 cpus=0000000c

  Workqueue CPU -> pool
  =====================
  [    workqueue \ CPU              0  1  2  3 dfl]
  events                   percpu   0  2  4  6
  events_highpri           percpu   1  3  5  7
  events_long              percpu   0  2  4  6
  events_unbound           unbound  9  9 10 10  8
  events_freezable         percpu   0  2  4  6
  events_power_efficient   percpu   0  2  4  6
  events_freezable_pwr_ef  percpu   0  2  4  6
  rcu_gp                   percpu   0  2  4  6
  rcu_par_gp               percpu   0  2  4  6
  slub_flushwq             percpu   0  2  4  6
  netns                    ordered  8  8  8  8  8
  ...

See the command's help message for more info.


Monitoring
==========

Use tools/workqueue/wq_monitor.py to monitor workqueue operations: ::

  $ tools/workqueue/wq_monitor.py events
                              total  infl  CPUtime  CPUhog CMW/RPR  mayday rescued
  events                      18545     0      6.1       0       5       -       -
  events_highpri                  8     0      0.0       0       0       -       -
  events_long                     3     0      0.0       0       0       -       -
  events_unbound              38306     0      0.1       -       7       -       -
  events_freezable                0     0      0.0       0       0       -       -
  events_power_efficient      29598     0      0.2       0       0       -       -
  events_freezable_pwr_ef        10     0      0.0       0       0       -       -
  sock_diag_events                0     0      0.0       0       0       -       -

                              total  infl  CPUtime  CPUhog CMW/RPR  mayday rescued
  events                      18548     0      6.1       0       5       -       -
  events_highpri                  8     0      0.0       0       0       -       -
  events_long                     3     0      0.0       0       0       -       -
  events_unbound              38322     0      0.1       -       7       -       -
  events_freezable                0     0      0.0       0       0       -       -
  events_power_efficient      29603     0      0.2       0       0       -       -
  events_freezable_pwr_ef        10     0      0.0       0       0       -       -
  sock_diag_events                0     0      0.0       0       0       -       -

  ...

See the command's help message for more info.


Debugging
=========

Because the work functions are executed by generic worker threads
there are a few tricks needed to shed some light on misbehaving
workqueue users.

Worker threads show up in the process list as: ::

  root      5671  0.0  0.0      0     0 ?        S    12:07   0:00 [kworker/0:1]
  root      5672  0.0  0.0      0     0 ?        S    12:07   0:00 [kworker/1:2]
  root      5673  0.0  0.0      0     0 ?        S    12:12   0:00 [kworker/0:0]
  root      5674  0.0  0.0      0     0 ?        S    12:13   0:00 [kworker/1:0]

If kworkers are going crazy (using too much cpu), there are two types
of possible problems:

	1. Something being scheduled in rapid succession
	2. A single work item that consumes lots of cpu cycles

The first one can be tracked using tracing: ::

	$ echo workqueue:workqueue_queue_work > /sys/kernel/tracing/set_event
	$ cat /sys/kernel/tracing/trace_pipe > out.txt
	(wait a few secs)
	^C

If something is busy looping on work queueing, it would be dominating
the output and the offender can be determined with the work item
function.

For the second type of problems it should be possible to just check
the stack trace of the offending worker thread. ::

	$ cat /proc/THE_OFFENDING_KWORKER/stack

The work item's function should be trivially visible in the stack
trace.


Non-reentrance Conditions
=========================

Workqueue guarantees that a work item cannot be re-entrant if the following
conditions hold after a work item gets queued:

        1. The work function hasn't been changed.
        2. No one queues the work item to another workqueue.
        3. The work item hasn't been reinitiated.

In other words, if the above conditions hold, the work item is guaranteed to be
executed by at most one worker system-wide at any given time.

Note that requeuing the work item (to the same queue) in the self function
doesn't break these conditions, so it's safe to do. Otherwise, caution is
required when breaking the conditions inside a work function.


Kernel Inline Documentations Reference
======================================

.. kernel-doc:: include/linux/workqueue.h

.. kernel-doc:: kernel/workqueue.c
