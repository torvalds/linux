**-c**, **--cpus** *cpu-list*

        Set the osnoise tracer to run the sample threads in the cpu-list.

**-H**, **--house-keeping** *cpu-list*

        Run rtla control threads only on the given cpu-list.

**-d**, **--duration** *time[s|m|h|d]*

        Set the duration of the session.

**-D**, **--debug**

        Print debug info.

**-t**, **--trace**\[*=file*]

        Save the stopped trace to [*file|osnoise_trace.txt*].

**-e**, **--event** *sys:event*

        Enable an event in the trace (**-t**) session. The argument can be a specific event, e.g., **-e** *sched:sched_switch*, or all events of a system group, e.g., **-e** *sched*. Multiple **-e** are allowed. It is only active when **-t** or **-a** are set.

**--filter** *<filter>*

        Filter the previous **-e** *sys:event* event with *<filter>*. For further information about event filtering see https://www.kernel.org/doc/html/latest/trace/events.html#event-filtering.

**--trigger** *<trigger>*
        Enable a trace event trigger to the previous **-e** *sys:event*.
        If the *hist:* trigger is activated, the output histogram will be automatically saved to a file named *system_event_hist.txt*.
        For example, the command:

        rtla <command> <mode> -t -e osnoise:irq_noise --trigger="hist:key=desc,duration/1000:sort=desc,duration/1000:vals=hitcount"

        Will automatically save the content of the histogram associated to *osnoise:irq_noise* event in *osnoise_irq_noise_hist.txt*.

        For further information about event trigger see https://www.kernel.org/doc/html/latest/trace/events.html#event-triggers.

**-P**, **--priority** *o:prio|r:prio|f:prio|d:runtime:period*

        Set scheduling parameters to the osnoise tracer threads, the format to set the priority are:

        - *o:prio* - use SCHED_OTHER with *prio*;
        - *r:prio* - use SCHED_RR with *prio*;
        - *f:prio* - use SCHED_FIFO with *prio*;
        - *d:runtime[us|ms|s]:period[us|ms|s]* - use SCHED_DEADLINE with *runtime* and *period* in nanoseconds.

**-C**, **--cgroup**\[*=cgroup*]

        Set a *cgroup* to the tracer's threads. If the **-C** option is passed without arguments, the tracer's thread will inherit **rtla**'s *cgroup*. Otherwise, the threads will be placed on the *cgroup* passed to the option.

**-h**, **--help**

        Print help menu.
