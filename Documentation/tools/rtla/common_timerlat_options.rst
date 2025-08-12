**-a**, **--auto** *us*

        Set the automatic trace mode. This mode sets some commonly used options
        while debugging the system. It is equivalent to use **-T** *us* **-s** *us*
        **-t**. By default, *timerlat* tracer uses FIFO:95 for *timerlat* threads,
        thus equilavent to **-P** *f:95*.

**-p**, **--period** *us*

        Set the *timerlat* tracer period in microseconds.

**-i**, **--irq** *us*

        Stop trace if the *IRQ* latency is higher than the argument in us.

**-T**, **--thread** *us*

        Stop trace if the *Thread* latency is higher than the argument in us.

**-s**, **--stack** *us*

        Save the stack trace at the *IRQ* if a *Thread* latency is higher than the
        argument in us.

**-t**, **--trace** \[*file*]

        Save the stopped trace to [*file|timerlat_trace.txt*].

**--dma-latency** *us*
        Set the /dev/cpu_dma_latency to *us*, aiming to bound exit from idle latencies.
        *cyclictest* sets this value to *0* by default, use **--dma-latency** *0* to have
        similar results.

**--deepest-idle-state** *n*
        Disable idle states higher than *n* for cpus that are running timerlat threads to
        reduce exit from idle latencies. If *n* is -1, all idle states are disabled.
        On exit from timerlat, the idle state setting is restored to its original state
        before running timerlat.

        Requires rtla to be built with libcpupower.

**-k**, **--kernel-threads**

        Use timerlat kernel-space threads, in contrast of **-u**.

**-u**, **--user-threads**

        Set timerlat to run without a workload, and then dispatches user-space workloads
        to wait on the timerlat_fd. Once the workload is awakes, it goes to sleep again
        adding so the measurement for the kernel-to-user and user-to-kernel to the tracer
        output. **--user-threads** will be used unless the user specify **-k**.

**-U**, **--user-load**

        Set timerlat to run without workload, waiting for the user to dispatch a per-cpu
        task that waits for a new period on the tracing/osnoise/per_cpu/cpu$ID/timerlat_fd.
        See linux/tools/rtla/sample/timerlat_load.py for an example of user-load code.

**--on-threshold** *action*

        Defines an action to be executed when tracing is stopped on a latency threshold
        specified by **-i/--irq** or **-T/--thread**.

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

        $ rtla timerlat -T 20 --on-threshold trace
        --on-threshold shell,command="grep ipi_send timerlat_trace.txt"
        --on-threshold signal,num=2,pid=parent

        This will save a trace with the default filename "timerlat_trace.txt", print its
        lines that contain the text "ipi_send" on standard output, and send signal 2
        (SIGINT) to the parent process.

        Performance Considerations:

        For time-sensitive actions, it is recommended to run **rtla timerlat** with BPF
        support and RT priority. Note that due to implementational limitations, actions
        might be delayed up to one second after tracing is stopped if BPF mode is not
        available or disabled.

**--on-end** *action*

        Defines an action to be executed at the end of **rtla timerlat** tracing.

        Multiple --on-end actions can be specified, and they will be executed in the order
        they are provided. If any action fails, subsequent actions in the list will not be
        executed.

        See the documentation for **--on-threshold** for the list of supported actions, with
        the exception that *continue* has no effect.

        Example:

        $ rtla timerlat -d 5s --on-end trace

        This runs rtla timerlat with default options and save trace output at the end.
