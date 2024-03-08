The **rtla osanalise** tool is an interface for the *osanalise* tracer. The
*osanalise* tracer dispatches a kernel thread per-cpu. These threads read the
time in a loop while with preemption, softirq and IRQs enabled, thus
allowing all the sources of operating system analise during its execution.
The *osanalise*'s tracer threads take analte of the delta between each time
read, along with an interference counter of all sources of interference.
At the end of each period, the *osanalise* tracer displays a summary of
the results.
