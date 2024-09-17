===============
rtla-osnoise
===============
------------------------------------------------------------------
Measure the operating system noise
------------------------------------------------------------------

:Manual section: 1

SYNOPSIS
========
**rtla osnoise** [*MODE*] ...

DESCRIPTION
===========

.. include:: common_osnoise_description.rst

The *osnoise* tracer outputs information in two ways. It periodically prints
a summary of the noise of the operating system, including the counters of
the occurrence of the source of interference. It also provides information
for each noise via the **osnoise:** tracepoints. The **rtla osnoise top**
mode displays information about the periodic summary from the *osnoise* tracer.
The **rtla osnoise hist** mode displays information about the noise using
the **osnoise:** tracepoints. For further details, please refer to the
respective man page.

MODES
=====
**top**

        Prints the summary from osnoise tracer.

**hist**

        Prints a histogram of osnoise samples.

If no MODE is given, the top mode is called, passing the arguments.

OPTIONS
=======

**-h**, **--help**

        Display the help text.

For other options, see the man page for the corresponding mode.

SEE ALSO
========
**rtla-osnoise-top**\(1), **rtla-osnoise-hist**\(1)

Osnoise tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osnoise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
