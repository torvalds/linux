.. |tool| replace:: timerlat top

====================
rtla-timerlat-top
====================
-------------------------------------------
Measures the operating system timer latency
-------------------------------------------

:Manual section: 1

SYNOPSIS
========
**rtla timerlat top** [*OPTIONS*] ...

DESCRIPTION
===========

.. include:: common_timerlat_description.rst

The **rtla timerlat top** displays a summary of the periodic output
from the *timerlat* tracer. It also provides information for each
operating system noise via the **osnoise:** tracepoints that can be
seem with the option **-T**.

OPTIONS
=======

.. include:: common_timerlat_options.rst

.. include:: common_top_options.rst

.. include:: common_options.rst

.. include:: common_timerlat_aa.rst

**--aa-only** *us*

        Set stop tracing conditions and run without collecting and displaying statistics.
        Print the auto-analysis if the system hits the stop tracing condition. This option
        is useful to reduce rtla timerlat CPU, enabling the debug without the overhead of
        collecting the statistics.

EXAMPLE
=======

In the example below, the timerlat tracer is dispatched in cpus *1-23* in the
automatic trace mode, instructing the tracer to stop if a *40 us* latency or
higher is found::

  # timerlat -a 40 -c 1-23 -q
                                     Timer Latency
    0 00:00:12   |          IRQ Timer Latency (us)        |         Thread Timer Latency (us)
  CPU COUNT      |      cur       min       avg       max |      cur       min       avg       max
    1 #12322     |        0         0         1        15 |       10         3         9        31
    2 #12322     |        3         0         1        12 |       10         3         9        23
    3 #12322     |        1         0         1        21 |        8         2         8        34
    4 #12322     |        1         0         1        17 |       10         2        11        33
    5 #12322     |        0         0         1        12 |        8         3         8        25
    6 #12322     |        1         0         1        14 |       16         3        11        35
    7 #12322     |        0         0         1        14 |        9         2         8        29
    8 #12322     |        1         0         1        22 |        9         3         9        34
    9 #12322     |        0         0         1        14 |        8         2         8        24
   10 #12322     |        1         0         0        12 |        9         3         8        24
   11 #12322     |        0         0         0        15 |        6         2         7        29
   12 #12321     |        1         0         0        13 |        5         3         8        23
   13 #12319     |        0         0         1        14 |        9         3         9        26
   14 #12321     |        1         0         0        13 |        6         2         8        24
   15 #12321     |        1         0         1        15 |       12         3        11        27
   16 #12318     |        0         0         1        13 |        7         3        10        24
   17 #12319     |        0         0         1        13 |       11         3         9        25
   18 #12318     |        0         0         0        12 |        8         2         8        20
   19 #12319     |        0         0         1        18 |       10         2         9        28
   20 #12317     |        0         0         0        20 |        9         3         8        34
   21 #12318     |        0         0         0        13 |        8         3         8        28
   22 #12319     |        0         0         1        11 |        8         3        10        22
   23 #12320     |       28         0         1        28 |       41         3        11        41
  rtla timerlat hit stop tracing
  ## CPU 23 hit stop tracing, analyzing it ##
  IRQ handler delay:                                        27.49 us (65.52 %)
  IRQ latency:                                              28.13 us
  Timerlat IRQ duration:                                     9.59 us (22.85 %)
  Blocking thread:                                           3.79 us (9.03 %)
                         objtool:49256                       3.79 us
    Blocking thread stacktrace
                -> timerlat_irq
                -> __hrtimer_run_queues
                -> hrtimer_interrupt
                -> __sysvec_apic_timer_interrupt
                -> sysvec_apic_timer_interrupt
                -> asm_sysvec_apic_timer_interrupt
                -> _raw_spin_unlock_irqrestore
                -> cgroup_rstat_flush_locked
                -> cgroup_rstat_flush_irqsafe
                -> mem_cgroup_flush_stats
                -> mem_cgroup_wb_stats
                -> balance_dirty_pages
                -> balance_dirty_pages_ratelimited_flags
                -> btrfs_buffered_write
                -> btrfs_do_write_iter
                -> vfs_write
                -> __x64_sys_pwrite64
                -> do_syscall_64
                -> entry_SYSCALL_64_after_hwframe
  ------------------------------------------------------------------------
    Thread latency:                                          41.96 us (100%)

  The system has exit from idle latency!
    Max timerlat IRQ latency from idle: 17.48 us in cpu 4
  Saving trace to timerlat_trace.txt

In this case, the major factor was the delay suffered by the *IRQ handler*
that handles **timerlat** wakeup: *65.52%*. This can be caused by the
current thread masking interrupts, which can be seen in the blocking
thread stacktrace: the current thread (*objtool:49256*) disabled interrupts
via *raw spin lock* operations inside mem cgroup, while doing write
syscall in a btrfs file system.

The raw trace is saved in the **timerlat_trace.txt** file for further analysis.

Note that **rtla timerlat** was dispatched without changing *timerlat* tracer
threads' priority. That is generally not needed because these threads have
priority *FIFO:95* by default, which is a common priority used by real-time
kernel developers to analyze scheduling delays.

SEE ALSO
--------
**rtla-timerlat**\(1), **rtla-timerlat-hist**\(1)

*timerlat* tracer documentation: <https://www.kernel.org/doc/html/latest/trace/timerlat-tracer.html>

AUTHOR
------
Written by Daniel Bristot de Oliveira <bristot@kernel.org>

.. include:: common_appendix.rst
