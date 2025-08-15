.. SPDX-License-Identifier: GPL-2.0

=====================
Theory of operation
=====================

:Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>

Preface
=======

PREEMPT_RT transforms the Linux kernel into a real-time kernel. It achieves
this by replacing locking primitives, such as spinlock_t, with a preemptible
and priority-inheritance aware implementation known as rtmutex, and by enforcing
the use of threaded interrupts. As a result, the kernel becomes fully
preemptible, with the exception of a few critical code paths, including entry
code, the scheduler, and low-level interrupt handling routines.

This transformation places the majority of kernel execution contexts under the
control of the scheduler and significantly increasing the number of preemption
points. Consequently, it reduces the latency between a high-priority task
becoming runnable and its actual execution on the CPU.

Scheduling
==========

The core principles of Linux scheduling and the associated user-space API are
documented in the man page sched(7)
`sched(7) <https://man7.org/linux/man-pages/man7/sched.7.html>`_.
By default, the Linux kernel uses the SCHED_OTHER scheduling policy. Under
this policy, a task is preempted when the scheduler determines that it has
consumed a fair share of CPU time relative to other runnable tasks. However,
the policy does not guarantee immediate preemption when a new SCHED_OTHER task
becomes runnable. The currently running task may continue executing.

This behavior differs from that of real-time scheduling policies such as
SCHED_FIFO. When a task with a real-time policy becomes runnable, the
scheduler immediately selects it for execution if it has a higher priority than
the currently running task. The task continues to run until it voluntarily
yields the CPU, typically by blocking on an event.

Sleeping spin locks
===================

The various lock types and their behavior under real-time configurations are
described in detail in Documentation/locking/locktypes.rst.
In a non-PREEMPT_RT configuration, a spinlock_t is acquired by first disabling
preemption and then actively spinning until the lock becomes available. Once
the lock is released, preemption is enabled. From a real-time perspective,
this approach is undesirable because disabling preemption prevents the
scheduler from switching to a higher-priority task, potentially increasing
latency.

To address this, PREEMPT_RT replaces spinning locks with sleeping spin locks
that do not disable preemption. On PREEMPT_RT, spinlock_t is implemented using
rtmutex. Instead of spinning, a task attempting to acquire a contended lock
disables CPU migration, donates its priority to the lock owner (priority
inheritance), and voluntarily schedules out while waiting for the lock to
become available.

Disabling CPU migration provides the same effect as disabling preemption, while
still allowing preemption and ensuring that the task continues to run on the
same CPU while holding a sleeping lock.

Priority inheritance
====================

Lock types such as spinlock_t and mutex_t in a PREEMPT_RT enabled kernel are
implemented on top of rtmutex, which provides support for priority inheritance
(PI). When a task blocks on such a lock, the PI mechanism temporarily
propagates the blocked task’s scheduling parameters to the lock owner.

For example, if a SCHED_FIFO task A blocks on a lock currently held by a
SCHED_OTHER task B, task A’s scheduling policy and priority are temporarily
inherited by task B. After this inheritance, task A is put to sleep while
waiting for the lock, and task B effectively becomes the highest-priority task
in the system. This allows B to continue executing, make progress, and
eventually release the lock.

Once B releases the lock, it reverts to its original scheduling parameters, and
task A can resume execution.

Threaded interrupts
===================

Interrupt handlers are another source of code that executes with preemption
disabled and outside the control of the scheduler. To bring interrupt handling
under scheduler control, PREEMPT_RT enforces threaded interrupt handlers.

With forced threading, interrupt handling is split into two stages. The first
stage, the primary handler, is executed in IRQ context with interrupts disabled.
Its sole responsibility is to wake the associated threaded handler. The second
stage, the threaded handler, is the function passed to request_irq() as the
interrupt handler. It runs in process context, scheduled by the kernel.

From waking the interrupt thread until threaded handling is completed, the
interrupt source is masked in the interrupt controller. This ensures that the
device interrupt remains pending but does not retrigger the CPU, allowing the
system to exit IRQ context and handle the interrupt in a scheduled thread.

By default, the threaded handler executes with the SCHED_FIFO scheduling policy
and a priority of 50 (MAX_RT_PRIO / 2), which is midway between the minimum and
maximum real-time priorities.

If the threaded interrupt handler raises any soft interrupts during its
execution, those soft interrupt routines are invoked after the threaded handler
completes, within the same thread. Preemption remains enabled during the
execution of the soft interrupt handler.

Summary
=======

By using sleeping locks and forced-threaded interrupts, PREEMPT_RT
significantly reduces sections of code where interrupts or preemption is
disabled, allowing the scheduler to preempt the current execution context and
switch to a higher-priority task.
