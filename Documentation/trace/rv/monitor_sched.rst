Scheduler monitors
==================

- Name: sched
- Type: container for multiple monitors
- Author: Gabriele Monaco <gmonaco@redhat.com>, Daniel Bristot de Oliveira <bristot@kernel.org>

Description
-----------

Monitors describing complex systems, such as the scheduler, can easily grow to
the point where they are just hard to understand because of the many possible
state transitions.
Often it is possible to break such descriptions into smaller monitors,
sharing some or all events. Enabling those smaller monitors concurrently is,
in fact, testing the system as if we had one single larger monitor.
Splitting models into multiple specification is not only easier to
understand, but gives some more clues when we see errors.

The sched monitor is a set of specifications to describe the scheduler behaviour.
It includes several per-cpu and per-task monitors that work independently to verify
different specifications the scheduler should follow.

To make this system as straightforward as possible, sched specifications are *nested*
monitors, whereas sched itself is a *container*.
From the interface perspective, sched includes other monitors as sub-directories,
enabling/disabling or setting reactors to sched, propagates the change to all monitors,
however single monitors can be used independently as well.

It is important that future modules are built after their container (sched, in
this case), otherwise the linker would not respect the order and the nesting
wouldn't work as expected.
To do so, simply add them after sched in the Makefile.

Specifications
--------------

The specifications included in sched are currently a work in progress, adapting the ones
defined in by Daniel Bristot in [1].

Currently we included the following:

Monitor sco
~~~~~~~~~~~

The scheduling context operations (sco) monitor ensures changes in a task state
happen only in thread context::


                        |
                        |
                        v
    sched_set_state   +------------------+
  +------------------ |                  |
  |                   |  thread_context  |
  +-----------------> |                  | <+
                      +------------------+  |
                        |                   |
                        | schedule_entry    | schedule_exit
                        v                   |
                                            |
                       scheduling_context  -+

Monitor snroc
~~~~~~~~~~~~~

The set non runnable on its own context (snroc) monitor ensures changes in a
task state happens only in the respective task's context. This is a per-task
monitor::

                        |
                        |
                        v
                      +------------------+
                      |  other_context   | <+
                      +------------------+  |
                        |                   |
                        | sched_switch_in   | sched_switch_out
                        v                   |
    sched_set_state                         |
  +------------------                       |
  |                       own_context       |
  +----------------->                      -+

Monitor scpd
~~~~~~~~~~~~

The schedule called with preemption disabled (scpd) monitor ensures schedule is
called with preemption disabled::

                       |
                       |
                       v
                     +------------------+
                     |    cant_sched    | <+
                     +------------------+  |
                       |                   |
                       | preempt_disable   | preempt_enable
                       v                   |
    schedule_entry                         |
    schedule_exit                          |
  +-----------------      can_sched        |
  |                                        |
  +---------------->                      -+

Monitor snep
~~~~~~~~~~~~

The schedule does not enable preempt (snep) monitor ensures a schedule call
does not enable preemption::

                        |
                        |
                        v
    preempt_disable   +------------------------+
    preempt_enable    |                        |
  +------------------ | non_scheduling_context |
  |                   |                        |
  +-----------------> |                        | <+
                      +------------------------+  |
                        |                         |
                        | schedule_entry          | schedule_exit
                        v                         |
                                                  |
                          scheduling_contex      -+

Monitor sts
~~~~~~~~~~~

The schedule implies task switch (sts) monitor ensures a task switch happens
only in scheduling context and up to once, as well as scheduling occurs with
interrupts enabled but no task switch can happen before interrupts are
disabled. When the next task picked for execution is the same as the previously
running one, no real task switch occurs but interrupts are disabled nonetheless::

    irq_entry                      |
     +----+                        |
     v    |                        v
 +------------+ irq_enable    #===================#   irq_disable
 |            | ------------> H                   H   irq_entry
 | cant_sched | <------------ H                   H   irq_enable
 |            | irq_disable   H     can_sched     H --------------+
 +------------+               H                   H               |
                              H                   H               |
            +---------------> H                   H <-------------+
            |                 #===================#
            |                   |
      schedule_exit             | schedule_entry
            |                   v
            |   +-------------------+     irq_enable
            |   |    scheduling     | <---------------+
            |   +-------------------+                 |
            |     |                                   |
            |     | irq_disable                    +--------+  irq_entry
            |     v                                |        | --------+
            |   +-------------------+  irq_entry   | in_irq |         |
            |   |                   | -----------> |        | <-------+
            |   | disable_to_switch |              +--------+
            |   |                   | --+
            |   +-------------------+   |
            |     |                     |
            |     | sched_switch        |
            |     v                     |
            |   +-------------------+   |
            |   |     switching     |   | irq_enable
            |   +-------------------+   |
            |     |                     |
            |     | irq_enable          |
            |     v                     |
            |   +-------------------+   |
            +-- |  enable_to_exit   | <-+
                +-------------------+
                  ^               | irq_disable
                  |               | irq_entry
                  +---------------+ irq_enable

Monitor nrp
-----------

The need resched preempts (nrp) monitor ensures preemption requires
``need_resched``. Only kernel preemption is considered, since preemption
while returning to userspace, for this monitor, is indistinguishable from
``sched_switch_yield`` (described in the sssw monitor).
A kernel preemption is whenever ``__schedule`` is called with the preemption
flag set to true (e.g. from preempt_enable or exiting from interrupts). This
type of preemption occurs after the need for ``rescheduling`` has been set.
This is not valid for the *lazy* variant of the flag, which causes only
userspace preemption.
A ``schedule_entry_preempt`` may involve a task switch or not, in the latter
case, a task goes through the scheduler from a preemption context but it is
picked as the next task to run. Since the scheduler runs, this clears the need
to reschedule. The ``any_thread_running`` state does not imply the monitored
task is not running as this monitor does not track the outcome of scheduling.

In theory, a preemption can only occur after the ``need_resched`` flag is set. In
practice, however, it is possible to see a preemption where the flag is not
set. This can happen in one specific condition::

  need_resched
                   preempt_schedule()
                                           preempt_schedule_irq()
                                                   __schedule()
  !need_resched
                           __schedule()

In the situation above, standard preemption starts (e.g. from preempt_enable
when the flag is set), an interrupt occurs before scheduling and, on its exit
path, it schedules, which clears the ``need_resched`` flag.
When the preempted task runs again, the standard preemption started earlier
resumes, although the flag is no longer set. The monitor considers this a
``nested_preemption``, this allows another preemption without re-setting the
flag. This condition relaxes the monitor constraints and may catch false
negatives (i.e. no real ``nested_preemptions``) but makes the monitor more
robust and able to validate other scenarios.
For simplicity, the monitor starts in ``preempt_irq``, although no interrupt
occurred, as the situation above is hard to pinpoint::

    schedule_entry
    irq_entry                 #===========================================#
  +-------------------------- H                                           H
  |                           H                                           H
  +-------------------------> H             any_thread_running            H
                              H                                           H
  +-------------------------> H                                           H
  |                           #===========================================#
  | schedule_entry              |                       ^
  | schedule_entry_preempt      | sched_need_resched    | schedule_entry
  |                             |                      schedule_entry_preempt
  |                             v                       |
  |                           +----------------------+  |
  |                      +--- |                      |  |
  |   sched_need_resched |    |     rescheduling     | -+
  |                      +--> |                      |
  |                           +----------------------+
  |                             | irq_entry
  |                             v
  |                           +----------------------+
  |                           |                      | ---+
  |                      ---> |                      |    | sched_need_resched
  |                           |      preempt_irq     |    | irq_entry
  |                           |                      | <--+
  |                           |                      | <--+
  |                           +----------------------+    |
  |                             | schedule_entry          | sched_need_resched
  |                             | schedule_entry_preempt  |
  |                             v                         |
  |                           +-----------------------+   |
  +-------------------------- |    nested_preempt     | --+
                              +-----------------------+
                                ^ irq_entry         |
                                +-------------------+

Due to how the ``need_resched`` flag on the preemption count works on arm64,
this monitor is unstable on that architecture, as it often records preemption
when the flag is not set, even in presence of the workaround above.
For the time being, the monitor is disabled by default on arm64.

Monitor sssw
------------

The set state sleep and wakeup (sssw) monitor ensures ``set_state`` to
sleepable leads to sleeping and sleeping tasks require wakeup. It includes the
following types of switch:

* ``switch_suspend``:
  a task puts itself to sleep, this can happen only after explicitly setting
  the task to ``sleepable``. After a task is suspended, it needs to be woken up
  (``waking`` state) before being switched in again.
  Setting the task's state to ``sleepable`` can be reverted before switching if it
  is woken up or set to ``runnable``.
* ``switch_blocking``:
  a special case of a ``switch_suspend`` where the task is waiting on a
  sleeping RT lock (``PREEMPT_RT`` only), it is common to see wakeup and set
  state events racing with each other and this leads the model to perceive this
  type of switch when the task is not set to sleepable. This is a limitation of
  the model in SMP system and workarounds may slow down the system.
* ``switch_preempt``:
  a task switch as a result of kernel preemption (``schedule_entry_preempt`` in
  the nrp model).
* ``switch_yield``:
  a task explicitly calls the scheduler or is preempted while returning to
  userspace. It can happen after a ``yield`` system call, from the idle task or
  if the ``need_resched`` flag is set. By definition, a task cannot yield while
  ``sleepable`` as that would be a suspension. A special case of a yield occurs
  when a task in ``TASK_INTERRUPTIBLE`` calls the scheduler while a signal is
  pending. The task doesn't go through the usual blocking/waking and is set
  back to runnable, the resulting switch (if there) looks like a yield to the
  ``signal_wakeup`` state and is followed by the signal delivery. From this
  state, the monitor expects a signal even if it sees a wakeup event, although
  not necessary, to rule out false negatives.

This monitor doesn't include a running state, ``sleepable`` and ``runnable``
are only referring to the task's desired state, which could be scheduled out
(e.g. due to preemption). However, it does include the event
``sched_switch_in`` to represent when a task is allowed to become running. This
can be triggered also by preemption, but cannot occur after the task got to
``sleeping`` before a ``wakeup`` occurs::

   +--------------------------------------------------------------------------+
   |                                                                          |
   |                                                                          |
   | switch_suspend           |                                               |
   | switch_blocking          |                                               |
   v                          v                                               |
 +----------+              #==========================#   set_state_runnable  |
 |          |              H                          H   wakeup              |
 |          |              H                          H   switch_in           |
 |          |              H                          H   switch_yield        |
 | sleeping |              H                          H   switch_preempt      |
 |          |              H                          H   signal_deliver      |
 |          |  switch_     H                          H ------+               |
 |          |  _blocking   H         runnable         H       |               |
 |          | <----------- H                          H <-----+               |
 +----------+              H                          H                       |
   |   wakeup              H                          H                       |
   +---------------------> H                          H                       |
                           H                          H                       |
               +---------> H                          H                       |
               |           #==========================#                       |
               |             |                ^                               |
               |             |                | set_state_runnable            |
               |             |                | wakeup                        |
               |    set_state_sleepable       |      +------------------------+
               |             v                |      |
               |           +--------------------------+  set_state_sleepable
               |           |                          |  switch_in
               |           |                          |  switch_preempt
   signal_deliver          |        sleepable         |  signal_deliver
               |           |                          | ------+
               |           |                          |       |
               |           |                          | <-----+
               |           +--------------------------+
               |             |                ^
               |        switch_yield          | set_state_sleepable
               |             v                |
               |           +---------------+  |
               +---------- | signal_wakeup | -+
                           +---------------+
                             ^           | switch_in
                             |           | switch_preempt
                             |           | switch_yield
                             +-----------+ wakeup

Monitor opid
------------

The operations with preemption and irq disabled (opid) monitor ensures
operations like ``wakeup`` and ``need_resched`` occur with interrupts and
preemption disabled or during interrupt context, in such case preemption may
not be disabled explicitly.
``need_resched`` can be set by some RCU internals functions, in which case it
doesn't match a task wakeup and might occur with only interrupts disabled::

                 |                     sched_need_resched
                 |                     sched_waking
                 |                     irq_entry
                 |                   +--------------------+
                 v                   v                    |
               +------------------------------------------------------+
  +----------- |                     disabled                         | <+
  |            +------------------------------------------------------+  |
  |              |                 ^                                     |
  |              |          preempt_disable      sched_need_resched      |
  |       preempt_enable           |           +--------------------+    |
  |              v                 |           v                    |    |
  |            +------------------------------------------------------+  |
  |            |                   irq_disabled                       |  |
  |            +------------------------------------------------------+  |
  |                              |             |        ^                |
  |     irq_entry            irq_entry         |        |                |
  |     sched_need_resched       v             |   irq_disable           |
  |     sched_waking +--------------+          |        |                |
  |           +----- |              |     irq_enable    |                |
  |           |      |    in_irq    |          |        |                |
  |           +----> |              |          |        |                |
  |                  +--------------+          |        |          irq_disable
  |                     |                      |        |                |
  | irq_enable          | irq_enable           |        |                |
  |                     v                      v        |                |
  |            #======================================================#  |
  |            H                     enabled                          H  |
  |            #======================================================#  |
  |              |                   ^         ^ preempt_enable     |    |
  |       preempt_disable     preempt_enable   +--------------------+    |
  |              v                   |                                   |
  |            +------------------+  |                                   |
  +----------> | preempt_disabled | -+                                   |
               +------------------+                                      |
                 |                                                       |
                 +-------------------------------------------------------+

This monitor is designed to work on ``PREEMPT_RT`` kernels, the special case of
events occurring in interrupt context is a shortcut to identify valid scenarios
where the preemption tracepoints might not be visible, during interrupts
preemption is always disabled. On non- ``PREEMPT_RT`` kernels, the interrupts
might invoke a softirq to set ``need_resched`` and wake up a task. This is
another special case that is currently not supported by the monitor.

References
----------

[1] - https://bristot.me/linux-task-model
