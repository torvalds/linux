The **rtla timerlat** tool is an interface for the *timerlat* tracer. The
*timerlat* tracer dispatches a kernel thread per-cpu. These threads
set a periodic timer to wake themselves up and go back to sleep. After
the wakeup, they collect and generate useful information for the
debugging of operating system timer latency.

The *timerlat* tracer outputs information in two ways. It periodically
prints the timer latency at the timer *IRQ* handler and the *Thread*
handler. It also enables the trace of the most relevant information via
**osnoise:** tracepoints.

The **rtla timerlat** tool sets the options of the *timerlat* tracer
and collects and displays a summary of the results. By default,
the collection is done synchronously in kernel space using a dedicated
BPF program attached to the *timerlat* tracer. If either BPF or
the **osnoise:timerlat_sample** tracepoint it attaches to is
unavailable, the **rtla timerlat** tool falls back to using tracefs to
process the data asynchronously in user space.
