===================
rtla-osnoise-hist
===================
------------------------------------------------------
Display a histogram of the osnoise tracer samples
------------------------------------------------------

:Manual section: 1

SYNOPSIS
========
**rtla osnoise hist** [*OPTIONS*]

DESCRIPTION
===========
.. include:: common_osnoise_description.rst

The **rtla osnoise hist** tool collects all **osnoise:sample_threshold**
occurrence in a histogram, displaying the results in a user-friendly way.
The tool also allows many configurations of the *osnoise* tracer and the
collection of the tracer output.

OPTIONS
=======
.. include:: common_osnoise_options.rst

.. include:: common_hist_options.rst

.. include:: common_options.rst

EXAMPLE
=======
In the example below, *osnoise* tracer threads are set to run with real-time
priority *FIFO:1*, on CPUs *0-11*, for *900ms* at each period (*1s* by
default). The reason for reducing the runtime is to avoid starving the
**rtla** tool. The tool is also set to run for *one minute*. The output
histogram is set to group outputs in buckets of *10us* and *25* entries::

  [root@f34 ~/]# rtla osnoise hist -P F:1 -c 0-11 -r 900000 -d 1M -b 10 -E 25
  # RTLA osnoise histogram
  # Time unit is microseconds (us)
  # Duration:   0 00:01:00
  Index   CPU-000   CPU-001   CPU-002   CPU-003   CPU-004   CPU-005   CPU-006   CPU-007   CPU-008   CPU-009   CPU-010   CPU-011
  0         42982     46287     51779     53740     52024     44817     49898     36500     50408     50128     49523     52377
  10        12224      8356      2912       878      2667     10155      4573     18894      4214      4836      5708      2413
  20            8         5        12         2        13        24        20        41        29        53        39        39
  30            1         1         0         0        10         3         6        19        15        31        30        38
  40            0         0         0         0         0         4         2         7         2         3         8        11
  50            0         0         0         0         0         0         0         0         0         1         1         2
  over:         0         0         0         0         0         0         0         0         0         0         0         0
  count:    55215     54649     54703     54620     54714     55003     54499     55461     54668     55052     55309     54880
  min:          0         0         0         0         0         0         0         0         0         0         0         0
  avg:          0         0         0         0         0         0         0         0         0         0         0         0
  max:         30        30        20        20        30        40        40        40        40        50        50        50

SEE ALSO
========
**rtla-osnoise**\(1), **rtla-osnoise-top**\(1)

*osnoise* tracer documentation: <https://www.kernel.org/doc/html/latest/trace/osnoise-tracer.html>

AUTHOR
======
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
