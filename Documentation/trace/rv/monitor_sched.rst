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

Monitor tss
~~~~~~~~~~~

The task switch while scheduling (tss) monitor ensures a task switch happens
only in scheduling context, that is inside a call to `__schedule`::

                     |
                     |
                     v
                   +-----------------+
                   |     thread      | <+
                   +-----------------+  |
                     |                  |
                     | schedule_entry   | schedule_exit
                     v                  |
    sched_switch                        |
  +---------------                      |
  |                       sched         |
  +-------------->                     -+

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

Monitor sncid
~~~~~~~~~~~~~

The schedule not called with interrupt disabled (sncid) monitor ensures
schedule is not called with interrupt disabled::

                       |
                       |
                       v
    schedule_entry   +--------------+
    schedule_exit    |              |
  +----------------- |  can_sched   |
  |                  |              |
  +----------------> |              | <+
                     +--------------+  |
                       |               |
                       | irq_disable   | irq_enable
                       v               |
                                       |
                        cant_sched    -+

References
----------

[1] - https://bristot.me/linux-task-model
