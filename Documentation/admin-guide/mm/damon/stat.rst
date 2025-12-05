.. SPDX-License-Identifier: GPL-2.0

===================================
Data Access Monitoring Results Stat
===================================

Data Access Monitoring Results Stat (DAMON_STAT) is a static kernel module that
is aimed to be used for simple access pattern monitoring.  It monitors accesses
on the system's entire physical memory using DAMON, and provides simplified
access monitoring results statistics, namely idle time percentiles and
estimated memory bandwidth.

.. _damon_stat_monitoring_accuracy_overhead:

Monitoring Accuracy and Overhead
================================

DAMON_STAT uses monitoring intervals :ref:`auto-tuning
<damon_design_monitoring_intervals_autotuning>` to make its accuracy high and
overhead minimum.  It auto-tunes the intervals aiming 4 % of observable access
events to be captured in each snapshot, while limiting the resulting sampling
interval to be 5 milliseconds in minimum and 10 seconds in maximum.  On a few
production server systems, it resulted in consuming only 0.x % single CPU time,
while capturing reasonable quality of access patterns.  The tuning-resulting
intervals can be retrieved via ``aggr_interval_us`` :ref:`parameter
<damon_stat_aggr_interval_us>`.

Interface: Module Parameters
============================

To use this feature, you should first ensure your system is running on a kernel
that is built with ``CONFIG_DAMON_STAT=y``.  The feature can be enabled by
default at build time, by setting ``CONFIG_DAMON_STAT_ENABLED_DEFAULT`` true.

To let sysadmins enable or disable it at boot and/or runtime, and read the
monitoring results, DAMON_STAT provides module parameters.  Following
sections are descriptions of the parameters.

enabled
-------

Enable or disable DAMON_STAT.

You can enable DAMON_STAT by setting the value of this parameter as ``Y``.
Setting it as ``N`` disables DAMON_STAT.  The default value is set by
``CONFIG_DAMON_STAT_ENABLED_DEFAULT`` build config option.

.. _damon_stat_aggr_interval_us:

aggr_interval_us
----------------

Auto-tuned aggregation time interval in microseconds.

Users can read the aggregation interval of DAMON that is being used by the
DAMON instance for DAMON_STAT.  It is :ref:`auto-tuned
<damon_stat_monitoring_accuracy_overhead>` and therefore the value is
dynamically changed.

estimated_memory_bandwidth
--------------------------

Estimated memory bandwidth consumption (bytes per second) of the system.

DAMON_STAT reads observed access events on the current DAMON results snapshot
and converts it to memory bandwidth consumption estimation in bytes per second.
The resulting metric is exposed to user via this read-only parameter.  Because
DAMON uses sampling, this is only an estimation of the access intensity rather
than accurate memory bandwidth.

memory_idle_ms_percentiles
--------------------------

Per-byte idle time (milliseconds) percentiles of the system.

DAMON_STAT calculates how long each byte of the memory was not accessed until
now (idle time), based on the current DAMON results snapshot.  For regions
having access frequency (nr_accesses) larger than zero, how long the current
access frequency level was kept multiplied by ``-1`` becomes the idlee time of
every byte of the region.  If a region has zero access frequency (nr_accesses),
how long the region was keeping the zero access frequency (age) becomes the
idle time of every byte of the region.  Then, DAMON_STAT exposes the
percentiles of the idle time values via this read-only parameter.  Reading the
parameter returns 101 idle time values in milliseconds, separated by comma.
Each value represents 0-th, 1st, 2nd, 3rd, ..., 99th and 100th percentile idle
times.
