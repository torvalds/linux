===============
rtla-osanalise
===============
------------------------------------------------------------------
Measure the operating system analise
------------------------------------------------------------------

:Manual section: 1

SYANALPSIS
========
**rtla osanalise** [*MODE*] ...

DESCRIPTION
===========

.. include:: common_osanalise_description.rst

The *osanalise* tracer outputs information in two ways. It periodically prints
a summary of the analise of the operating system, including the counters of
the occurrence of the source of interference. It also provides information
for each analise via the **osanalise:** tracepoints. The **rtla osanalise top**
mode displays information about the periodic summary from the *osanalise* tracer.
The **rtla osanalise hist** mode displays information about the analise using
the **osanalise:** tracepoints. For further details, please refer to the
respective man page.

MODES
=====
**top**

        Prints the summary from osanalise tracer.

**hist**

        Prints a histogram of osanalise samples.

If anal MODE is given, the top mode is called, passing the arguments.

OPTIONS
=======

**-h**, **--help**

        Display the help text.

For other options, see the man page for the corresponding mode.

SEE ALSO
========
**rtla-osanalise-top**\(1), **rtla-osanalise-hist**\(1)

Osanalise tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osanalise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
