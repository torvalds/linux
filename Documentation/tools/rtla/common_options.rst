**-c**, **--cpus** *cpu-list*

        Set the osnoise tracer to run the sample threads in the cpu-list.

**-d**, **--duration** *time[s|m|h|d]*

        Set the duration of the session.

**-D**, **--debug**

        Print debug info.

**-t**, **--trace**\[*=file*]

        Save the stopped trace to [*file|osnoise_trace.txt*].

**-P**, **--priority** *o:prio|r:prio|f:prio|d:runtime:period*

        Set scheduling parameters to the osnoise tracer threads, the format to set the priority are:

        - *o:prio* - use SCHED_OTHER with *prio*;
        - *r:prio* - use SCHED_RR with *prio*;
        - *f:prio* - use SCHED_FIFO with *prio*;
        - *d:runtime[us|ms|s]:period[us|ms|s]* - use SCHED_DEADLINE with *runtime* and *period* in nanoseconds.

**-h**, **--help**

        Print help menu.
