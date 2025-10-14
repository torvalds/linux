.. SPDX-License-Identifier: GPL-2.0

.. |tool| replace:: hwnoise

============
rtla-hwnoise
============
------------------------------------------
Detect and quantify hardware-related noise
------------------------------------------

:Manual section: 1

SYNOPSIS
========

**rtla hwnoise** [*OPTIONS*]

DESCRIPTION
===========

**rtla hwnoise** collects the periodic summary from the *osnoise* tracer
running with *interrupts disabled*. By disabling interrupts, and the scheduling
of threads as a consequence, only non-maskable interrupts and hardware-related
noise is allowed.

The tool also allows the configurations of the *osnoise* tracer and the
collection of the tracer output.

OPTIONS
=======
.. include:: common_osnoise_options.rst

.. include:: common_top_options.rst

.. include:: common_options.rst

EXAMPLE
=======
In the example below, the **rtla hwnoise** tool is set to run on CPUs *1-7*
on a system with 8 cores/16 threads with hyper-threading enabled.

The tool is set to detect any noise higher than *one microsecond*,
to run for *ten minutes*, displaying a summary of the report at the
end of the session::

  # rtla hwnoise -c 1-7 -T 1 -d 10m -q
                                          Hardware-related Noise
  duration:   0 00:10:00 | time is in us
  CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI
    1 #599       599000000          138    99.99997           3            3           4           74
    2 #599       599000000           85    99.99998           3            3           4           75
    3 #599       599000000           86    99.99998           4            3           6           75
    4 #599       599000000           81    99.99998           4            4           2           75
    5 #599       599000000           85    99.99998           2            2           2           75
    6 #599       599000000           76    99.99998           2            2           0           75
    7 #599       599000000           77    99.99998           3            3           0           75


The first column shows the *CPU*, and the second column shows how many
*Periods* the tool ran during the session. The *Runtime* is the time
the tool effectively runs on the CPU. The *Noise* column is the sum of
all noise that the tool observed, and the *% CPU Aval* is the relation
between the *Runtime* and *Noise*.

The *Max Noise* column is the maximum hardware noise the tool detected in a
single period, and the *Max Single* is the maximum single noise seen.

The *HW* and *NMI* columns show the total number of *hardware* and *NMI* noise
occurrence observed by the tool.

For example, *CPU 3* ran *599* periods of *1 second Runtime*. The CPU received
*86 us* of noise during the entire execution, leaving *99.99997 %* of CPU time
for the application. In the worst single period, the CPU caused *4 us* of
noise to the application, but it was certainly caused by more than one single
noise, as the *Max Single* noise was of *3 us*. The CPU has *HW noise,* at a
rate of *six occurrences*/*ten minutes*. The CPU also has *NMIs*, at a higher
frequency: around *seven per second*.

The tool should report *0* hardware-related noise in the ideal situation.
For example, by disabling hyper-threading to remove the hardware noise,
and disabling the TSC watchdog to remove the NMI (it is possible to identify
this using tracing options of **rtla hwnoise**), it was possible to reach
the ideal situation in the same hardware::

  # rtla hwnoise -c 1-7 -T 1 -d 10m -q
                                          Hardware-related Noise
  duration:   0 00:10:00 | time is in us
  CPU Period       Runtime        Noise  % CPU Aval   Max Noise   Max Single          HW          NMI
    1 #599       599000000            0   100.00000           0            0           0            0
    2 #599       599000000            0   100.00000           0            0           0            0
    3 #599       599000000            0   100.00000           0            0           0            0
    4 #599       599000000            0   100.00000           0            0           0            0
    5 #599       599000000            0   100.00000           0            0           0            0
    6 #599       599000000            0   100.00000           0            0           0            0
    7 #599       599000000            0   100.00000           0            0           0            0

SEE ALSO
========

**rtla-osnoise**\(1)

Osnoise tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osnoise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
