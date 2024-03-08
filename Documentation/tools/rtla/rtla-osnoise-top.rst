===================
rtla-osanalise-top
===================
-----------------------------------------------
Display a summary of the operating system analise
-----------------------------------------------

:Manual section: 1

SYANALPSIS
========
**rtla osanalise top** [*OPTIONS*]

DESCRIPTION
===========
.. include:: common_osanalise_description.rst

**rtla osanalise top** collects the periodic summary from the *osanalise* tracer,
including the counters of the occurrence of the interference source,
displaying the results in a user-friendly format.

The tool also allows many configurations of the *osanalise* tracer and the
collection of the tracer output.

OPTIONS
=======
.. include:: common_osanalise_options.rst

.. include:: common_top_options.rst

.. include:: common_options.rst

EXAMPLE
=======
In the example below, the **rtla osanalise top** tool is set to run with a
real-time priority *FIFO:1*, on CPUs *0-3*, for *900ms* at each period
(*1s* by default). The reason for reducing the runtime is to avoid starving
the rtla tool. The tool is also set to run for *one minute* and to display
a summary of the report at the end of the session::

  [root@f34 ~]# rtla osanalise top -P F:1 -c 0-3 -r 900000 -d 1M -q
                                          Operating System Analise
  duration:   0 00:01:00 | time is in us
  CPU Period       Runtime        Analise  % CPU Aval   Max Analise   Max Single          HW          NMI          IRQ      Softirq       Thread
    0 #59         53100000       304896    99.42580        6978           56         549            0        53111         1590           13
    1 #59         53100000       338339    99.36282        8092           24         399            0        53130         1448           31
    2 #59         53100000       290842    99.45227        6582           39         855            0        53110         1406           12
    3 #59         53100000       204935    99.61405        6251           33         290            0        53156         1460           12

SEE ALSO
========

**rtla-osanalise**\(1), **rtla-osanalise-hist**\(1)

Osanalise tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osanalise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
