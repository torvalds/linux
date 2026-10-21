.. SPDX-License-Identifier: GPL-2.0

=====================
Scheduler preemption
=====================

The kernel can be built to run kernel code either uninterruptibly, or with
varying degrees of preemptibility.  These are the *preemption models*.
They apply to kernel code only: a task running in user space is always
preempted by the scheduler, whichever model is selected.

When CONFIG_PREEMPT_DYNAMIC is enabled the preemption model can additionally
be selected at boot time with the ``preempt=`` command line parameter, without
rebuilding the kernel.  See
Documentation/admin-guide/kernel-parameters.txt for the parameter itself.
The active model can also be read and changed through
/sys/kernel/debug/sched/preempt, which exists for debugging purposes and may
change.

The models selectable at runtime are:

	=========	====================================================
	none		No preemption of kernel code other than at explicit
			``cond_resched()`` / blocking points.
	voluntary	As ``none``, plus ``might_sleep()`` sites.
	full		Any section that is not explicitly preempt disabled
			may be preempted at any time.
	lazy		As ``full``, except that the scheduling request is
			delayed until the return to user space or the next
			tick, whichever comes first.  This delay does not
			apply to real-time tasks.
	=========	====================================================

Not every model is available on every kernel.  An architecture that provides
lazy-preempt support offers only ``full`` and ``lazy``, and ``none`` and
``voluntary`` are rejected there.  A PREEMPT_RT kernel always offers ``full``,
and ``lazy`` as well if the architecture provides it.

The model that is actually active is reported in the boot log::

	Dynamic Preempt: full

Honouring a scheduling request
==============================

A wakeup makes a task runnable, and the scheduler then decides whether it
should run.  If there is an idle CPU which is suitable then the task is moved
there.  Otherwise the scheduler has to decide which task to preempt, and the
task that has to leave the CPU gets a reschedule flag.  In the tracing output
this is the ``need-resched`` column, ``N`` for TIF_NEED_RESCHED and ``l`` for
TIF_NEED_RESCHED_LAZY.

The kernel then has to honour that request.  A task executing in user space
can always be preempted; a task executing in kernel space can only be
preempted where it is safe to do so, and how that is decided is what
separates the models:

``none``
	Code paths with long loops contain explicit ``cond_resched()`` calls,
	which perform the scheduling.

``voluntary``
	As ``none``, and functions which are known to be able to block gain
	a ``cond_resched()``-style scheduling point as well, through
	``might_sleep()``.

``full``
	Explicit preemption points are no longer involved.  The kernel tracks
	whether it may be preempted and schedules once the request can be
	honoured.

``lazy``
	As ``full``, with one difference: the request the fair scheduler sets
	is TIF_NEED_RESCHED_LAZY, which is not honoured immediately even where
	it could be.  It is delayed until the task returns to user space, so
	that the in-kernel work runs to completion and every lock has been
	dropped before the CPU is given up.  If the task has not scheduled on
	its own by then, the request is turned into a full TIF_NEED_RESCHED on
	the next HZ tick; this is visible as ``B`` in the ``need-resched``
	column.

Real-time tasks always use TIF_NEED_RESCHED, so the delay above does not
apply to them.  ``lazy`` is therefore not a replacement for PREEMPT_RT: it
keeps most of the responsiveness of ``full`` for SCHED_NORMAL tasks while
letting them run to completion, which avoids leaving a lock behind that
another task will immediately ask for, and avoids pushing cache-hot data out
that would have to be pulled back in.
