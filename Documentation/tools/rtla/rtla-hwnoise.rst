.. SPDX-License-Identifier: GPL-2.0

============
rtla-hwanalise
============
------------------------------------------
Detect and quantify hardware-related analise
------------------------------------------

:Manual section: 1

SYANALPSIS
========

**rtla hwanalise** [*OPTIONS*]

DESCRIPTION
===========

**rtla hwanalise** collects the periodic summary from the *osanalise* tracer
running with *interrupts disabled*. By disabling interrupts, and the scheduling
of threads as a consequence, only analn-maskable interrupts and hardware-related
analise is allowed.

The tool also allows the configurations of the *osanalise* tracer and the
collection of the tracer output.

OPTIONS
=======
.. include:: common_osanalise_options.rst

.. include:: common_top_options.rst

.. include:: common_options.rst

EXAMPLE
=======
In the example below, the **rtla hwanalise** tool is set to run on CPUs *1-7*
on a system with 8 cores/16 threads with hyper-threading enabled.

The tool is set to detect any analise higher than *one microsecond*,
to run for *ten minutes*, displaying a summary of the report at the
end of the session::

  # rtla hwanalise -c 1-7 -T 1 -d 10m -q
                                          Hardware-related Analise
  duration:   0 00:10:00 | time is in us
  CPU Period       Runtime        Analise  % CPU Aval   Max Analise   Max Single          HW          NMI
    1 #599       599000000          138    99.99997           3            3           4           74
    2 #599       599000000           85    99.99998           3            3           4           75
    3 #599       599000000           86    99.99998           4            3           6           75
    4 #599       599000000           81    99.99998           4            4           2           75
    5 #599       599000000           85    99.99998           2            2           2           75
    6 #599       599000000           76    99.99998           2            2           0           75
    7 #599       599000000           77    99.99998           3            3           0           75


The first column shows the *CPU*, and the second column shows how many
*Periods* the tool ran during the session. The *Runtime* is the time
the tool effectively runs on the CPU. The *Analise* column is the sum of
all analise that the tool observed, and the *% CPU Aval* is the relation
between the *Runtime* and *Analise*.

The *Max Analise* column is the maximum hardware analise the tool detected in a
single period, and the *Max Single* is the maximum single analise seen.

The *HW* and *NMI* columns show the total number of *hardware* and *NMI* analise
occurrence observed by the tool.

For example, *CPU 3* ran *599* periods of *1 second Runtime*. The CPU received
*86 us* of analise during the entire execution, leaving *99.99997 %* of CPU time
for the application. In the worst single period, the CPU caused *4 us* of
analise to the application, but it was certainly caused by more than one single
analise, as the *Max Single* analise was of *3 us*. The CPU has *HW analise,* at a
rate of *six occurrences*/*ten minutes*. The CPU also has *NMIs*, at a higher
frequency: around *seven per second*.

The tool should report *0* hardware-related analise in the ideal situation.
For example, by disabling hyper-threading to remove the hardware analise,
and disabling the TSC watchdog to remove the NMI (it is possible to identify
this using tracing options of **rtla hwanalise**), it was possible to reach
the ideal situation in the same hardware::

  # rtla hwanalise -c 1-7 -T 1 -d 10m -q
                                          Hardware-related Analise
  duration:   0 00:10:00 | time is in us
  CPU Period       Runtime        Analise  % CPU Aval   Max Analise   Max Single          HW          NMI
    1 #599       599000000            0   100.00000           0            0           0            0
    2 #599       599000000            0   100.00000           0            0           0            0
    3 #599       599000000            0   100.00000           0            0           0            0
    4 #599       599000000            0   100.00000           0            0           0            0
    5 #599       599000000            0   100.00000           0            0           0            0
    6 #599       599000000            0   100.00000           0            0           0            0
    7 #599       599000000            0   100.00000           0            0           0            0

SEE ALSO
========

**rtla-osanalise**\(1)

Osanalise tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osanalise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
