**-c**, **--cpus** *cpu-list*

        Set the osnoise tracer to run the sample threads in the cpu-list.

**-H**, **--house-keeping** *cpu-list*

        Run rtla control threads only on the given cpu-list.

**-d**, **--duration** *time[s|m|h|d]*

        Set the duration of the session.

**-D**, **--debug**

        Print debug info.

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

**--warm-up** *s*

        After starting the workload, let it run for *s* seconds before starting collecting the data, allowing the system to warm-up. Statistical data generated during warm-up is discarded.

**--trace-buffer-size** *kB*
        Set the per-cpu trace buffer size in kB for the tracing output.

**--on-threshold** *action*

        Defines an action to be executed when tracing is stopped on a latency threshold
        specified by |threshold|.

        Multiple --on-threshold actions may be specified, and they will be executed in
        the order they are provided. If any action fails, subsequent actions in the list
        will not be executed.

        Supported actions are:

        - *trace[,file=<filename>]*

          Saves trace output, optionally taking a filename. Alternative to -t/--trace.
          Note that nlike -t/--trace, specifying this multiple times will result in
          the trace being saved multiple times.

        - *signal,num=<sig>,pid=<pid>*

          Sends signal to process. "parent" might be specified in place of pid to target
          the parent process of rtla.

        - *shell,command=<command>*

          Execute shell command.

        - *continue*

          Continue tracing after actions are executed instead of stopping.

        Example:

        $ rtla |tool| |thresharg| 20 --on-threshold trace
        --on-threshold shell,command="grep ipi_send |tracer|\_trace.txt"
        --on-threshold signal,num=2,pid=parent

        This will save a trace with the default filename "|tracer|\_trace.txt", print its
        lines that contain the text "ipi_send" on standard output, and send signal 2
        (SIGINT) to the parent process.

        Performance Considerations:

        |actionsperf|

**--on-end** *action*

        Defines an action to be executed at the end of tracing.

        Multiple --on-end actions can be specified, and they will be executed in the order
        they are provided. If any action fails, subsequent actions in the list will not be
        executed.

        See the documentation for **--on-threshold** for the list of supported actions, with
        the exception that *continue* has no effect.

        Example:

        $ rtla |tool| -d 5s --on-end trace

        This runs rtla with the default options, and saves trace output at the end.

**-h**, **--help**

        Print help menu.
