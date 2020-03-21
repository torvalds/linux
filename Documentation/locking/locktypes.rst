.. SPDX-License-Identifier: GPL-2.0

.. _kernel_hacking_locktypes:

==========================
Lock types and their rules
==========================

Introduction
============

The kernel provides a variety of locking primitives which can be divided
into two categories:

 - Sleeping locks
 - Spinning locks

This document conceptually describes these lock types and provides rules
for their nesting, including the rules for use under PREEMPT_RT.


Lock categories
===============

Sleeping locks
--------------

Sleeping locks can only be acquired in preemptible task context.

Although implementations allow try_lock() from other contexts, it is
necessary to carefully evaluate the safety of unlock() as well as of
try_lock().  Furthermore, it is also necessary to evaluate the debugging
versions of these primitives.  In short, don't acquire sleeping locks from
other contexts unless there is no other option.

Sleeping lock types:

 - mutex
 - rt_mutex
 - semaphore
 - rw_semaphore
 - ww_mutex
 - percpu_rw_semaphore

On PREEMPT_RT kernels, these lock types are converted to sleeping locks:

 - spinlock_t
 - rwlock_t

Spinning locks
--------------

 - raw_spinlock_t
 - bit spinlocks

On non-PREEMPT_RT kernels, these lock types are also spinning locks:

 - spinlock_t
 - rwlock_t

Spinning locks implicitly disable preemption and the lock / unlock functions
can have suffixes which apply further protections:

 ===================  ====================================================
 _bh()                Disable / enable bottom halves (soft interrupts)
 _irq()               Disable / enable interrupts
 _irqsave/restore()   Save and disable / restore interrupt disabled state
 ===================  ====================================================


rtmutex
=======

RT-mutexes are mutexes with support for priority inheritance (PI).

PI has limitations on non PREEMPT_RT enabled kernels due to preemption and
interrupt disabled sections.

PI clearly cannot preempt preemption-disabled or interrupt-disabled
regions of code, even on PREEMPT_RT kernels.  Instead, PREEMPT_RT kernels
execute most such regions of code in preemptible task context, especially
interrupt handlers and soft interrupts.  This conversion allows spinlock_t
and rwlock_t to be implemented via RT-mutexes.


raw_spinlock_t and spinlock_t
=============================

raw_spinlock_t
--------------

raw_spinlock_t is a strict spinning lock implementation regardless of the
kernel configuration including PREEMPT_RT enabled kernels.

raw_spinlock_t is a strict spinning lock implementation in all kernels,
including PREEMPT_RT kernels.  Use raw_spinlock_t only in real critical
core code, low level interrupt handling and places where disabling
preemption or interrupts is required, for example, to safely access
hardware state.  raw_spinlock_t can sometimes also be used when the
critical section is tiny, thus avoiding RT-mutex overhead.

spinlock_t
----------

The semantics of spinlock_t change with the state of CONFIG_PREEMPT_RT.

On a non PREEMPT_RT enabled kernel spinlock_t is mapped to raw_spinlock_t
and has exactly the same semantics.

spinlock_t and PREEMPT_RT
-------------------------

On a PREEMPT_RT enabled kernel spinlock_t is mapped to a separate
implementation based on rt_mutex which changes the semantics:

 - Preemption is not disabled

 - The hard interrupt related suffixes for spin_lock / spin_unlock
   operations (_irq, _irqsave / _irqrestore) do not affect the CPUs
   interrupt disabled state

 - The soft interrupt related suffix (_bh()) still disables softirq
   handlers.

   Non-PREEMPT_RT kernels disable preemption to get this effect.

   PREEMPT_RT kernels use a per-CPU lock for serialization which keeps
   preemption disabled. The lock disables softirq handlers and also
   prevents reentrancy due to task preemption.

PREEMPT_RT kernels preserve all other spinlock_t semantics:

 - Tasks holding a spinlock_t do not migrate.  Non-PREEMPT_RT kernels
   avoid migration by disabling preemption.  PREEMPT_RT kernels instead
   disable migration, which ensures that pointers to per-CPU variables
   remain valid even if the task is preempted.

 - Task state is preserved across spinlock acquisition, ensuring that the
   task-state rules apply to all kernel configurations.  Non-PREEMPT_RT
   kernels leave task state untouched.  However, PREEMPT_RT must change
   task state if the task blocks during acquisition.  Therefore, it saves
   the current task state before blocking and the corresponding lock wakeup
   restores it.

   Other types of wakeups would normally unconditionally set the task state
   to RUNNING, but that does not work here because the task must remain
   blocked until the lock becomes available.  Therefore, when a non-lock
   wakeup attempts to awaken a task blocked waiting for a spinlock, it
   instead sets the saved state to RUNNING.  Then, when the lock
   acquisition completes, the lock wakeup sets the task state to the saved
   state, in this case setting it to RUNNING.

rwlock_t
========

rwlock_t is a multiple readers and single writer lock mechanism.

Non-PREEMPT_RT kernels implement rwlock_t as a spinning lock and the
suffix rules of spinlock_t apply accordingly. The implementation is fair,
thus preventing writer starvation.

rwlock_t and PREEMPT_RT
-----------------------

PREEMPT_RT kernels map rwlock_t to a separate rt_mutex-based
implementation, thus changing semantics:

 - All the spinlock_t changes also apply to rwlock_t.

 - Because an rwlock_t writer cannot grant its priority to multiple
   readers, a preempted low-priority reader will continue holding its lock,
   thus starving even high-priority writers.  In contrast, because readers
   can grant their priority to a writer, a preempted low-priority writer
   will have its priority boosted until it releases the lock, thus
   preventing that writer from starving readers.


PREEMPT_RT caveats
==================

spinlock_t and rwlock_t
-----------------------

These changes in spinlock_t and rwlock_t semantics on PREEMPT_RT kernels
have a few implications.  For example, on a non-PREEMPT_RT kernel the
following code sequence works as expected::

   local_irq_disable();
   spin_lock(&lock);

and is fully equivalent to::

   spin_lock_irq(&lock);

Same applies to rwlock_t and the _irqsave() suffix variants.

On PREEMPT_RT kernel this code sequence breaks because RT-mutex requires a
fully preemptible context.  Instead, use spin_lock_irq() or
spin_lock_irqsave() and their unlock counterparts.  In cases where the
interrupt disabling and locking must remain separate, PREEMPT_RT offers a
local_lock mechanism.  Acquiring the local_lock pins the task to a CPU,
allowing things like per-CPU irq-disabled locks to be acquired.  However,
this approach should be used only where absolutely necessary.


raw_spinlock_t
--------------

Acquiring a raw_spinlock_t disables preemption and possibly also
interrupts, so the critical section must avoid acquiring a regular
spinlock_t or rwlock_t, for example, the critical section must avoid
allocating memory.  Thus, on a non-PREEMPT_RT kernel the following code
works perfectly::

  raw_spin_lock(&lock);
  p = kmalloc(sizeof(*p), GFP_ATOMIC);

But this code fails on PREEMPT_RT kernels because the memory allocator is
fully preemptible and therefore cannot be invoked from truly atomic
contexts.  However, it is perfectly fine to invoke the memory allocator
while holding normal non-raw spinlocks because they do not disable
preemption on PREEMPT_RT kernels::

  spin_lock(&lock);
  p = kmalloc(sizeof(*p), GFP_ATOMIC);


bit spinlocks
-------------

Bit spinlocks are problematic for PREEMPT_RT as they cannot be easily
substituted by an RT-mutex based implementation for obvious reasons.

The semantics of bit spinlocks are preserved on PREEMPT_RT kernels and the
caveats vs. raw_spinlock_t apply.

Some bit spinlocks are substituted by regular spinlock_t for PREEMPT_RT but
this requires conditional (#ifdef'ed) code changes at the usage site while
the spinlock_t substitution is simply done by the compiler and the
conditionals are restricted to header files and core implementation of the
locking primitives and the usage sites do not require any changes.


Lock type nesting rules
=======================

The most basic rules are:

  - Lock types of the same lock category (sleeping, spinning) can nest
    arbitrarily as long as they respect the general lock ordering rules to
    prevent deadlocks.

  - Sleeping lock types cannot nest inside spinning lock types.

  - Spinning lock types can nest inside sleeping lock types.

These rules apply in general independent of CONFIG_PREEMPT_RT.

As PREEMPT_RT changes the lock category of spinlock_t and rwlock_t from
spinning to sleeping this has obviously restrictions how they can nest with
raw_spinlock_t.

This results in the following nest ordering:

  1) Sleeping locks
  2) spinlock_t and rwlock_t
  3) raw_spinlock_t and bit spinlocks

Lockdep is aware of these constraints to ensure that they are respected.


Owner semantics
===============

Most lock types in the Linux kernel have strict owner semantics, i.e. the
context (task) which acquires a lock has to release it.

There are two exceptions:

  - semaphores
  - rwsems

semaphores have no owner semantics for historical reason, and as such
trylock and release operations can be called from any context. They are
often used for both serialization and waiting purposes. That's generally
discouraged and should be replaced by separate serialization and wait
mechanisms, such as mutexes and completions.

rwsems have grown interfaces which allow non owner release for special
purposes. This usage is problematic on PREEMPT_RT because PREEMPT_RT
substitutes all locking primitives except semaphores with RT-mutex based
implementations to provide priority inheritance for all lock types except
the truly spinning ones. Priority inheritance on ownerless locks is
obviously impossible.

For now the rwsem non-owner release excludes code which utilizes it from
being used on PREEMPT_RT enabled kernels. In same cases this can be
mitigated by disabling portions of the code, in other cases the complete
functionality has to be disabled until a workable solution has been found.
