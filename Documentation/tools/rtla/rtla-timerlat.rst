================
rtla-timerlat
================
-------------------------------------------
Measures the operating system timer latency
-------------------------------------------

:Manual section: 1

SYNOPSIS
========
**rtla timerlat** [*MODE*] ...

DESCRIPTION
===========

.. include:: common_timerlat_description.rst

The *timerlat* tracer outputs information in two ways. It periodically
prints the timer latency at the timer *IRQ* handler and the *Thread* handler.
It also provides information for each noise via the **osnoise:** tracepoints.
The **rtla timerlat top** mode displays a summary of the periodic output
from the *timerlat* tracer. The **rtla hist hist** mode displays a histogram
of each tracer event occurrence. For further details, please refer to the
respective man page.

MODES
=====
**top**

        Prints the summary from *timerlat* tracer.

**hist**

        Prints a histogram of timerlat samples.

If no *MODE* is given, the top mode is called, passing the arguments.

OPTIONS
=======
**-h**, **--help**

        Display the help text.

For other options, see the man page for the corresponding mode.

SEE ALSO
========
**rtla-timerlat-top**\(1), **rtla-timerlat-hist**\(1)

*timerlat* tracer documentation: <https://www.kernel.org/doc/html/latest/trace/timerlat-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
