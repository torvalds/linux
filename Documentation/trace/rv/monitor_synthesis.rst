Runtime Verification Monitor Synthesis
======================================

The starting point for the application of runtime verification (RV) techniques
is the *specification* or *modeling* of the desired (or undesired) behavior
of the system under scrutiny.

The formal representation needs to be then *synthesized* into a *monitor*
that can then be used in the analysis of the trace of the system. The
*monitor* connects to the system via an *instrumentation* that converts
the events from the *system* to the events of the *specification*.


In Linux terms, the runtime verification monitors are encapsulated inside
the *RV monitor* abstraction. The RV monitor includes a set of instances
of the monitor (per-cpu monitor, per-task monitor, and so on), the helper
functions that glue the monitor to the system reference model, and the
trace output as a reaction to event parsing and exceptions, as depicted
below::

 Linux  +----- RV Monitor ----------------------------------+ Formal
  Realm |                                                   |  Realm
  +-------------------+     +----------------+     +-----------------+
  |   Linux kernel    |     |     Monitor    |     |     Reference   |
  |     Tracing       |  -> |   Instance(s)  | <-  |       Model     |
  | (instrumentation) |     | (verification) |     | (specification) |
  +-------------------+     +----------------+     +-----------------+
         |                          |                       |
         |                          V                       |
         |                     +----------+                 |
         |                     | Reaction |                 |
         |                     +--+--+--+-+                 |
         |                        |  |  |                   |
         |                        |  |  +-> trace output ?  |
         +------------------------|--|----------------------+
                                  |  +----> panic ?
                                  +-------> <user-specified>

RV monitor synthesis
--------------------

The synthesis of a specification into the Linux *RV monitor* abstraction is
automated by the rvgen tool and the header file containing common code for
creating monitors. The header files are:

  * rv/da_monitor.h for deterministic automaton monitor.
  * rv/ltl_monitor.h for linear temporal logic monitor.

rvgen
-----

The rvgen utility converts a specification into the C presentation and creating
the skeleton of a kernel monitor in C.

For example, it is possible to transform the wip.dot model present in
[1] into a per-cpu monitor with the following command::

  $ rvgen monitor -c da -s wip.dot -t per_cpu

This will create a directory named wip/ with the following files:

- wip.h: the wip model in C
- wip.c: the RV monitor

The wip.c file contains the monitor declaration and the starting point for
the system instrumentation.

Similarly, a linear temporal logic monitor can be generated with the following
command::

  $ rvgen monitor -c ltl -s pagefault.ltl -t per_task

This generates pagefault/ directory with:

- pagefault.h: The Buchi automaton (the non-deterministic state machine to
  verify the specification)
- pagefault.c: The skeleton for the RV monitor

Monitor header files
--------------------

The header files:

- `rv/da_monitor.h` for deterministic automaton monitor
- `rv/ltl_monitor` for linear temporal logic monitor

include common macros and static functions for implementing *Monitor
Instance(s)*.

The benefits of having all common functionalities in a single header file are
3-fold:

  - Reduce the code duplication;
  - Facilitate the bug fix/improvement;
  - Avoid the case of developers changing the core of the monitor code to
    manipulate the model in a (let's say) non-standard way.

rv/da_monitor.h
+++++++++++++++

This initial implementation presents three different types of monitor instances:

- ``#define DECLARE_DA_MON_GLOBAL(name, type)``
- ``#define DECLARE_DA_MON_PER_CPU(name, type)``
- ``#define DECLARE_DA_MON_PER_TASK(name, type)``

The first declares the functions for a global deterministic automata monitor,
the second for monitors with per-cpu instances, and the third with per-task
instances.

In all cases, the 'name' argument is a string that identifies the monitor, and
the 'type' argument is the data type used by rvgen on the representation of
the model in C.

For example, the wip model with two states and three events can be
stored in an 'unsigned char' type. Considering that the preemption control
is a per-cpu behavior, the monitor declaration in the 'wip.c' file is::

  DECLARE_DA_MON_PER_CPU(wip, unsigned char);

The monitor is executed by sending events to be processed via the functions
presented below::

  da_handle_event_$(MONITOR_NAME)($(event from event enum));
  da_handle_start_event_$(MONITOR_NAME)($(event from event enum));
  da_handle_start_run_event_$(MONITOR_NAME)($(event from event enum));

The function ``da_handle_event_$(MONITOR_NAME)()`` is the regular case where
the event will be processed if the monitor is processing events.

When a monitor is enabled, it is placed in the initial state of the automata.
However, the monitor does not know if the system is in the *initial state*.

The ``da_handle_start_event_$(MONITOR_NAME)()`` function is used to notify the
monitor that the system is returning to the initial state, so the monitor can
start monitoring the next event.

The ``da_handle_start_run_event_$(MONITOR_NAME)()`` function is used to notify
the monitor that the system is known to be in the initial state, so the
monitor can start monitoring and monitor the current event.

Using the wip model as example, the events "preempt_disable" and
"sched_waking" should be sent to monitor, respectively, via [2]::

  da_handle_event_wip(preempt_disable_wip);
  da_handle_event_wip(sched_waking_wip);

While the event "preempt_enabled" will use::

  da_handle_start_event_wip(preempt_enable_wip);

To notify the monitor that the system will be returning to the initial state,
so the system and the monitor should be in sync.

rv/ltl_monitor.h
++++++++++++++++
This file must be combined with the $(MODEL_NAME).h file (generated by `rvgen`)
to be complete. For example, for the `pagefault` monitor, the `pagefault.c`
source file must include::

  #include "pagefault.h"
  #include <rv/ltl_monitor.h>

(the skeleton monitor file generated by `rvgen` already does this).

`$(MODEL_NAME).h` (`pagefault.h` in the above example) includes the
implementation of the Buchi automaton - a non-deterministic state machine that
verifies the LTL specification. While `rv/ltl_monitor.h` includes the common
helper functions to interact with the Buchi automaton and to implement an RV
monitor. An important definition in `$(MODEL_NAME).h` is::

  enum ltl_atom {
      LTL_$(FIRST_ATOMIC_PROPOSITION),
      LTL_$(SECOND_ATOMIC_PROPOSITION),
      ...
      LTL_NUM_ATOM
  };

which is the list of atomic propositions present in the LTL specification
(prefixed with "LTL\_" to avoid name collision). This `enum` is passed to the
functions interacting with the Buchi automaton.

While generating code, `rvgen` cannot understand the meaning of the atomic
propositions. Thus, that task is left for manual work. The recommended practice
is adding tracepoints to places where the atomic propositions change; and in the
tracepoints' handlers: the Buchi automaton is executed using::

  void ltl_atom_update(struct task_struct *task, enum ltl_atom atom, bool value)

which tells the Buchi automaton that the atomic proposition `atom` is now
`value`. The Buchi automaton checks whether the LTL specification is still
satisfied, and invokes the monitor's error tracepoint and the reactor if
violation is detected.

Tracepoints and `ltl_atom_update()` should be used whenever possible. However,
it is sometimes not the most convenient. For some atomic propositions which are
changed in multiple places in the kernel, it is cumbersome to trace all those
places. Furthermore, it may not be important that the atomic propositions are
updated at precise times. For example, considering the following linear temporal
logic::

  RULE = always (RT imply not PAGEFAULT)

This LTL states that a real-time task does not raise page faults. For this
specification, it is not important when `RT` changes, as long as it has the
correct value when `PAGEFAULT` is true.  Motivated by this case, another
function is introduced::

  void ltl_atom_fetch(struct task_struct *task, struct ltl_monitor *mon)

This function is called whenever the Buchi automaton is triggered. Therefore, it
can be manually implemented to "fetch" `RT`::

  void ltl_atom_fetch(struct task_struct *task, struct ltl_monitor *mon)
  {
      ltl_atom_set(mon, LTL_RT, rt_task(task));
  }

Effectively, whenever `PAGEFAULT` is updated with a call to `ltl_atom_update()`,
`RT` is also fetched. Thus, the LTL specification can be verified without
tracing `RT` everywhere.

For atomic propositions which act like events, they usually need to be set (or
cleared) and then immediately cleared (or set). A convenient function is
provided::

  void ltl_atom_pulse(struct task_struct *task, enum ltl_atom atom, bool value)

which is equivalent to::

  ltl_atom_update(task, atom, value);
  ltl_atom_update(task, atom, !value);

To initialize the atomic propositions, the following function must be
implemented::

  ltl_atoms_init(struct task_struct *task, struct ltl_monitor *mon, bool task_creation)

This function is called for all running tasks when the monitor is enabled. It is
also called for new tasks created after the enabling the monitor. It should
initialize as many atomic propositions as possible, for example::

  void ltl_atom_init(struct task_struct *task, struct ltl_monitor *mon, bool task_creation)
  {
      ltl_atom_set(mon, LTL_RT, rt_task(task));
      if (task_creation)
          ltl_atom_set(mon, LTL_PAGEFAULT, false);
  }

Atomic propositions not initialized by `ltl_atom_init()` will stay in the
unknown state until relevant tracepoints are hit, which can take some time. As
monitoring for a task cannot be done until all atomic propositions is known for
the task, the monitor may need some time to start validating tasks which have
been running before the monitor is enabled. Therefore, it is recommended to
start the tasks of interest after enabling the monitor.

Final remarks
-------------

With the monitor synthesis in place using the header files and
rvgen, the developer's work should be limited to the instrumentation
of the system, increasing the confidence in the overall approach.

[1] For details about deterministic automata format and the translation
from one representation to another, see::

  Documentation/trace/rv/deterministic_automata.rst

[2] rvgen appends the monitor's name suffix to the events enums to
avoid conflicting variables when exporting the global vmlinux.h
use by BPF programs.
