================
NMI Trace Events
================

These events normally show up here:

	/sys/kernel/debug/tracing/events/nmi


nmi_handler
-----------

You might want to use this tracepoint if you suspect that your
NMI handlers are hogging large amounts of CPU time.  The kernel
will warn if it sees long-running handlers::

	INFO: NMI handler took too long to run: 9.207 msecs

and this tracepoint will allow you to drill down and get some
more details.

Let's say you suspect that perf_event_nmi_handler() is causing
you some problems and you only want to trace that handler
specifically.  You need to find its address::

	$ grep perf_event_nmi_handler /proc/kallsyms
	ffffffff81625600 t perf_event_nmi_handler

Let's also say you are only interested in when that function is
really hogging a lot of CPU time, like a millisecond at a time.
Note that the kernel's output is in milliseconds, but the input
to the filter is in nanoseconds!  You can filter on 'delta_ns'::

	cd /sys/kernel/debug/tracing/events/nmi/nmi_handler
	echo 'handler==0xffffffff81625600 && delta_ns>1000000' > filter
	echo 1 > enable

Your output would then look like::

	$ cat /sys/kernel/debug/tracing/trace_pipe
	<idle>-0     [000] d.h3   505.397558: nmi_handler: perf_event_nmi_handler() delta_ns: 3236765 handled: 1
	<idle>-0     [000] d.h3   505.805893: nmi_handler: perf_event_nmi_handler() delta_ns: 3174234 handled: 1
	<idle>-0     [000] d.h3   506.158206: nmi_handler: perf_event_nmi_handler() delta_ns: 3084642 handled: 1
	<idle>-0     [000] d.h3   506.334346: nmi_handler: perf_event_nmi_handler() delta_ns: 3080351 handled: 1

