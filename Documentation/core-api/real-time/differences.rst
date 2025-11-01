.. SPDX-License-Identifier: GPL-2.0

===========================
How realtime kernels differ
===========================

:Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>

Preface
=======

With forced-threaded interrupts and sleeping spin locks, code paths that
previously caused long scheduling latencies have been made preemptible and
moved into process context. This allows the scheduler to manage them more
effectively and respond to higher-priority tasks with reduced latency.

The following chapters provide an overview of key differences between a
PREEMPT_RT kernel and a standard, non-PREEMPT_RT kernel.

Locking
=======

Spinning locks such as spinlock_t are used to provide synchronization for data
structures accessed from both interrupt context and process context. For this
reason, locking functions are also available with the _irq() or _irqsave()
suffixes, which disable interrupts before acquiring the lock. This ensures that
the lock can be safely acquired in process context when interrupts are enabled.

However, on a PREEMPT_RT system, interrupts are forced-threaded and no longer
run in hard IRQ context. As a result, there is no need to disable interrupts as
part of the locking procedure when using spinlock_t.

For low-level core components such as interrupt handling, the scheduler, or the
timer subsystem the kernel uses raw_spinlock_t. This lock type preserves
traditional semantics: it disables preemption and, when used with _irq() or
_irqsave(), also disables interrupts. This ensures proper synchronization in
critical sections that must remain non-preemptible or with interrupts disabled.

Execution context
=================

Interrupt handling in a PREEMPT_RT system is invoked in process context through
the use of threaded interrupts. Other parts of the kernel also shift their
execution into threaded context by different mechanisms. The goal is to keep
execution paths preemptible, allowing the scheduler to interrupt them when a
higher-priority task needs to run.

Below is an overview of the kernel subsystems involved in this transition to
threaded, preemptible execution.

Interrupt handling
------------------

All interrupts are forced-threaded in a PREEMPT_RT system. The exceptions are
interrupts that are requested with the IRQF_NO_THREAD, IRQF_PERCPU, or
IRQF_ONESHOT flags.

The IRQF_ONESHOT flag is used together with threaded interrupts, meaning those
registered using request_threaded_irq() and providing only a threaded handler.
Its purpose is to keep the interrupt line masked until the threaded handler has
completed.

If a primary handler is also provided in this case, it is essential that the
handler does not acquire any sleeping locks, as it will not be threaded. The
handler should be minimal and must avoid introducing delays, such as
busy-waiting on hardware registers.


Soft interrupts, bottom half handling
-------------------------------------

Soft interrupts are raised by the interrupt handler and are executed after the
handler returns. Since they run in thread context, they can be preempted by
other threads. Do not assume that softirq context runs with preemption
disabled. This means you must not rely on mechanisms like local_bh_disable() in
process context to protect per-CPU variables. Because softirq handlers are
preemptible under PREEMPT_RT, this approach does not provide reliable
synchronization.

If this kind of protection is required for performance reasons, consider using
local_lock_nested_bh(). On non-PREEMPT_RT kernels, this allows lockdep to
verify that bottom halves are disabled. On PREEMPT_RT systems, it adds the
necessary locking to ensure proper protection.

Using local_lock_nested_bh() also makes the locking scope explicit and easier
for readers and maintainers to understand.


per-CPU variables
-----------------

Protecting access to per-CPU variables solely by using preempt_disable() should
be avoided, especially if the critical section has unbounded runtime or may
call APIs that can sleep.

If using a spinlock_t is considered too costly for performance reasons,
consider using local_lock_t. On non-PREEMPT_RT configurations, this introduces
no runtime overhead when lockdep is disabled. With lockdep enabled, it verifies
that the lock is only acquired in process context and never from softirq or
hard IRQ context.

On a PREEMPT_RT kernel, local_lock_t is implemented using a per-CPU spinlock_t,
which provides safe local protection for per-CPU data while keeping the system
preemptible.

Because spinlock_t on PREEMPT_RT does not disable preemption, it cannot be used
to protect per-CPU data by relying on implicit preemption disabling. If this
inherited preemption disabling is essential and if local_lock_t cannot be used
due to performance constraints, brevity of the code, or abstraction boundaries
within an API then preempt_disable_nested() may be a suitable alternative. On
non-PREEMPT_RT kernels, it verifies with lockdep that preemption is already
disabled. On PREEMPT_RT, it explicitly disables preemption.

Timers
------

By default, an hrtimer is executed in hard interrupt context. The exception is
timers initialized with the HRTIMER_MODE_SOFT flag, which are executed in
softirq context.

On a PREEMPT_RT kernel, this behavior is reversed: hrtimers are executed in
softirq context by default, typically within the ktimersd thread. This thread
runs at the lowest real-time priority, ensuring it executes before any
SCHED_OTHER tasks but does not interfere with higher-priority real-time
threads. To explicitly request execution in hard interrupt context on
PREEMPT_RT, the timer must be marked with the HRTIMER_MODE_HARD flag.

Memory allocation
-----------------

The memory allocation APIs, such as kmalloc() and alloc_pages(), require a
gfp_t flag to indicate the allocation context. On non-PREEMPT_RT kernels, it is
necessary to use GFP_ATOMIC when allocating memory from interrupt context or
from sections where preemption is disabled. This is because the allocator must
not sleep in these contexts waiting for memory to become available.

However, this approach does not work on PREEMPT_RT kernels. The memory
allocator in PREEMPT_RT uses sleeping locks internally, which cannot be
acquired when preemption is disabled. Fortunately, this is generally not a
problem, because PREEMPT_RT moves most contexts that would traditionally run
with preemption or interrupts disabled into threaded context, where sleeping is
allowed.

What remains problematic is code that explicitly disables preemption or
interrupts. In such cases, memory allocation must be performed outside the
critical section.

This restriction also applies to memory deallocation routines such as kfree()
and free_pages(), which may also involve internal locking and must not be
called from non-preemptible contexts.

IRQ work
--------

The irq_work API provides a mechanism to schedule a callback in interrupt
context. It is designed for use in contexts where traditional scheduling is not
possible, such as from within NMI handlers or from inside the scheduler, where
using a workqueue would be unsafe.

On non-PREEMPT_RT systems, all irq_work items are executed immediately in
interrupt context. Items marked with IRQ_WORK_LAZY are deferred until the next
timer tick but are still executed in interrupt context.

On PREEMPT_RT systems, the execution model changes. Because irq_work callbacks
may acquire sleeping locks or have unbounded execution time, they are handled
in thread context by a per-CPU irq_work kernel thread. This thread runs at the
lowest real-time priority, ensuring it executes before any SCHED_OTHER tasks
but does not interfere with higher-priority real-time threads.

The exception are work items marked with IRQ_WORK_HARD_IRQ, which are still
executed in hard interrupt context. Lazy items (IRQ_WORK_LAZY) continue to be
deferred until the next timer tick and are also executed by the irq_work/
thread.

RCU callbacks
-------------

RCU callbacks are invoked by default in softirq context. Their execution is
important because, depending on the use case, they either free memory or ensure
progress in state transitions. Running these callbacks as part of the softirq
chain can lead to undesired situations, such as contention for CPU resources
with other SCHED_OTHER tasks when executed within ksoftirqd.

To avoid running callbacks in softirq context, the RCU subsystem provides a
mechanism to execute them in process context instead. This behavior can be
enabled by setting the boot command-line parameter rcutree.use_softirq=0. This
setting is enforced in kernels configured with PREEMPT_RT.

Spin until ready
================

The "spin until ready" pattern involves repeatedly checking (spinning on) the
state of a data structure until it becomes available. This pattern assumes that
preemption, soft interrupts, or interrupts are disabled. If the data structure
is marked busy, it is presumed to be in use by another CPU, and spinning should
eventually succeed as that CPU makes progress.

Some examples are hrtimer_cancel() or timer_delete_sync(). These functions
cancel timers that execute with interrupts or soft interrupts disabled. If a
thread attempts to cancel a timer and finds it active, spinning until the
callback completes is safe because the callback can only run on another CPU and
will eventually finish.

On PREEMPT_RT kernels, however, timer callbacks run in thread context. This
introduces a challenge: a higher-priority thread attempting to cancel the timer
may preempt the timer callback thread. Since the scheduler cannot migrate the
callback thread to another CPU due to affinity constraints, spinning can result
in livelock even on multiprocessor systems.

To avoid this, both the canceling and callback sides must use a handshake
mechanism that supports priority inheritance. This allows the canceling thread
to suspend until the callback completes, ensuring forward progress without
risking livelock.

In order to solve the problem at the API level, the sequence locks were extended
to allow a proper handover between the the spinning reader and the maybe
blocked writer.

Sequence locks
--------------

Sequence counters and sequential locks are documented in
Documentation/locking/seqlock.rst.

The interface has been extended to ensure proper preemption states for the
writer and spinning reader contexts. This is achieved by embedding the writer
serialization lock directly into the sequence counter type, resulting in
composite types such as seqcount_spinlock_t or seqcount_mutex_t.

These composite types allow readers to detect an ongoing write and actively
boost the writer’s priority to help it complete its update instead of spinning
and waiting for its completion.

If the plain seqcount_t is used, extra care must be taken to synchronize the
reader with the writer during updates. The writer must ensure its update is
serialized and non-preemptible relative to the reader. This cannot be achieved
using a regular spinlock_t because spinlock_t on PREEMPT_RT does not disable
preemption. In such cases, using seqcount_spinlock_t is the preferred solution.

However, if there is no spinning involved i.e., if the reader only needs to
detect whether a write has started and not serialize against it then using
seqcount_t is reasonable.
