Deterministic Automata Monitor Synthesis
========================================

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

DA monitor synthesis
--------------------

The synthesis of automata-based models into the Linux *RV monitor* abstraction
is automated by the dot2k tool and the rv/da_monitor.h header file that
contains a set of macros that automatically generate the monitor's code.

dot2k
-----

The dot2k utility leverages dot2c by converting an automaton model in
the DOT format into the C representation [1] and creating the skeleton of
a kernel monitor in C.

For example, it is possible to transform the wip.dot model present in
[1] into a per-cpu monitor with the following command::

  $ dot2k -d wip.dot -t per_cpu

This will create a directory named wip/ with the following files:

- wip.h: the wip model in C
- wip.c: the RV monitor

The wip.c file contains the monitor declaration and the starting point for
the system instrumentation.

Monitor macros
--------------

The rv/da_monitor.h enables automatic code generation for the *Monitor
Instance(s)* using C macros.

The benefits of the usage of macro for monitor synthesis are 3-fold as it:

- Reduces the code duplication;
- Facilitates the bug fix/improvement;
- Avoids the case of developers changing the core of the monitor code
  to manipulate the model in a (let's say) non-standard way.

This initial implementation presents three different types of monitor instances:

- ``#define DECLARE_DA_MON_GLOBAL(name, type)``
- ``#define DECLARE_DA_MON_PER_CPU(name, type)``
- ``#define DECLARE_DA_MON_PER_TASK(name, type)``

The first declares the functions for a global deterministic automata monitor,
the second for monitors with per-cpu instances, and the third with per-task
instances.

In all cases, the 'name' argument is a string that identifies the monitor, and
the 'type' argument is the data type used by dot2k on the representation of
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

Final remarks
-------------

With the monitor synthesis in place using the rv/da_monitor.h and
dot2k, the developer's work should be limited to the instrumentation
of the system, increasing the confidence in the overall approach.

[1] For details about deterministic automata format and the translation
from one representation to another, see::

  Documentation/trace/rv/deterministic_automata.rst

[2] dot2k appends the monitor's name suffix to the events enums to
avoid conflicting variables when exporting the global vmlinux.h
use by BPF programs.
