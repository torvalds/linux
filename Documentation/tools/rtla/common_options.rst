**-c**, **--cpus** *cpu-list*

        Set the osnoise tracer to run the sample threads in the cpu-list.

**-d**, **--duration** *time[s|m|h|d]*

        Set the duration of the session.

**-D**, **--debug**

        Print debug info.

**-t**, **--trace**\[*=file*]

        Save the stopped trace to [*file|osnoise_trace.txt*].

**-e**, **--event** *sys:event*

        Enable an event in the trace (**-t**) session. The argument can be a specific event, e.g., **-e** *sched:sched_switch*, or all events of a system group, e.g., **-e** *sched*. Multiple **-e** are allowed. It is only active when **-t** or **-a** are set.

**--trigger** *<trigger>*
        Enable a trace event trigger to the previous **-e** *sys:event*. For further information about event trigger see https://www.kernel.org/doc/html/latest/trace/events.html#event-triggers.

**-P**, **--priority** *o:prio|r:prio|f:prio|d:runtime:period*

        Set scheduling parameters to the osnoise tracer threads, the format to set the priority are:

        - *o:prio* - use SCHED_OTHER with *prio*;
        - *r:prio* - use SCHED_RR with *prio*;
        - *f:prio* - use SCHED_FIFO with *prio*;
        - *d:runtime[us|ms|s]:period[us|ms|s]* - use SCHED_DEADLINE with *runtime* and *period* in nanoseconds.

**-h**, **--help**

        Print help menu.
