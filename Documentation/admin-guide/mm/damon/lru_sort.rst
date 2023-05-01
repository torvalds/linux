.. SPDX-License-Identifier: GPL-2.0

=============================
DAMON-based LRU-lists Sorting
=============================

DAMON-based LRU-lists Sorting (DAMON_LRU_SORT) is a static kernel module that
aimed to be used for proactive and lightweight data access pattern based
(de)prioritization of pages on their LRU-lists for making LRU-lists a more
trusworthy data access pattern source.

Where Proactive LRU-lists Sorting is Required?
==============================================

As page-granularity access checking overhead could be significant on huge
systems, LRU lists are normally not proactively sorted but partially and
reactively sorted for special events including specific user requests, system
calls and memory pressure.  As a result, LRU lists are sometimes not so
perfectly prepared to be used as a trustworthy access pattern source for some
situations including reclamation target pages selection under sudden memory
pressure.

Because DAMON can identify access patterns of best-effort accuracy while
inducing only user-specified range of overhead, proactively running
DAMON_LRU_SORT could be helpful for making LRU lists more trustworthy access
pattern source with low and controlled overhead.

How It Works?
=============

DAMON_LRU_SORT finds hot pages (pages of memory regions that showing access
rates that higher than a user-specified threshold) and cold pages (pages of
memory regions that showing no access for a time that longer than a
user-specified threshold) using DAMON, and prioritizes hot pages while
deprioritizing cold pages on their LRU-lists.  To avoid it consuming too much
CPU for the prioritizations, a CPU time usage limit can be configured.  Under
the limit, it prioritizes and deprioritizes more hot and cold pages first,
respectively.  System administrators can also configure under what situation
this scheme should automatically activated and deactivated with three memory
pressure watermarks.

Its default parameters for hotness/coldness thresholds and CPU quota limit are
conservatively chosen.  That is, the module under its default parameters could
be widely used without harm for common situations while providing a level of
benefits for systems having clear hot/cold access patterns under memory
pressure while consuming only a limited small portion of CPU time.

Interface: Module Parameters
============================

To use this feature, you should first ensure your system is running on a kernel
that is built with ``CONFIG_DAMON_LRU_SORT=y``.

To let sysadmins enable or disable it and tune for the given system,
DAMON_LRU_SORT utilizes module parameters.  That is, you can put
``damon_lru_sort.<parameter>=<value>`` on the kernel boot command line or write
proper values to ``/sys/module/damon_lru_sort/parameters/<parameter>`` files.

Below are the description of each parameter.

enabled
-------

Enable or disable DAMON_LRU_SORT.

You can enable DAMON_LRU_SORT by setting the value of this parameter as ``Y``.
Setting it as ``N`` disables DAMON_LRU_SORT.  Note that DAMON_LRU_SORT could do
no real monitoring and LRU-lists sorting due to the watermarks-based activation
condition.  Refer to below descriptions for the watermarks parameter for this.

commit_inputs
-------------

Make DAMON_LRU_SORT reads the input parameters again, except ``enabled``.

Input parameters that updated while DAMON_LRU_SORT is running are not applied
by default.  Once this parameter is set as ``Y``, DAMON_LRU_SORT reads values
of parametrs except ``enabled`` again.  Once the re-reading is done, this
parameter is set as ``N``.  If invalid parameters are found while the
re-reading, DAMON_LRU_SORT will be disabled.

hot_thres_access_freq
---------------------

Access frequency threshold for hot memory regions identification in permil.

If a memory region is accessed in frequency of this or higher, DAMON_LRU_SORT
identifies the region as hot, and mark it as accessed on the LRU list, so that
it could not be reclaimed under memory pressure.  50% by default.

cold_min_age
------------

Time threshold for cold memory regions identification in microseconds.

If a memory region is not accessed for this or longer time, DAMON_LRU_SORT
identifies the region as cold, and mark it as unaccessed on the LRU list, so
that it could be reclaimed first under memory pressure.  120 seconds by
default.

quota_ms
--------

Limit of time for trying the LRU lists sorting in milliseconds.

DAMON_LRU_SORT tries to use only up to this time within a time window
(quota_reset_interval_ms) for trying LRU lists sorting.  This can be used
for limiting CPU consumption of DAMON_LRU_SORT.  If the value is zero, the
limit is disabled.

10 ms by default.

quota_reset_interval_ms
-----------------------

The time quota charge reset interval in milliseconds.

The charge reset interval for the quota of time (quota_ms).  That is,
DAMON_LRU_SORT does not try LRU-lists sorting for more than quota_ms
milliseconds or quota_sz bytes within quota_reset_interval_ms milliseconds.

1 second by default.

wmarks_interval
---------------

The watermarks check time interval in microseconds.

Minimal time to wait before checking the watermarks, when DAMON_LRU_SORT is
enabled but inactive due to its watermarks rule.  5 seconds by default.

wmarks_high
-----------

Free memory rate (per thousand) for the high watermark.

If free memory of the system in bytes per thousand bytes is higher than this,
DAMON_LRU_SORT becomes inactive, so it does nothing but periodically checks the
watermarks.  200 (20%) by default.

wmarks_mid
----------

Free memory rate (per thousand) for the middle watermark.

If free memory of the system in bytes per thousand bytes is between this and
the low watermark, DAMON_LRU_SORT becomes active, so starts the monitoring and
the LRU-lists sorting.  150 (15%) by default.

wmarks_low
----------

Free memory rate (per thousand) for the low watermark.

If free memory of the system in bytes per thousand bytes is lower than this,
DAMON_LRU_SORT becomes inactive, so it does nothing but periodically checks the
watermarks.  50 (5%) by default.

sample_interval
---------------

Sampling interval for the monitoring in microseconds.

The sampling interval of DAMON for the cold memory monitoring.  Please refer to
the DAMON documentation (:doc:`usage`) for more detail.  5ms by default.

aggr_interval
-------------

Aggregation interval for the monitoring in microseconds.

The aggregation interval of DAMON for the cold memory monitoring.  Please
refer to the DAMON documentation (:doc:`usage`) for more detail.  100ms by
default.

min_nr_regions
--------------

Minimum number of monitoring regions.

The minimal number of monitoring regions of DAMON for the cold memory
monitoring.  This can be used to set lower-bound of the monitoring quality.
But, setting this too high could result in increased monitoring overhead.
Please refer to the DAMON documentation (:doc:`usage`) for more detail.  10 by
default.

max_nr_regions
--------------

Maximum number of monitoring regions.

The maximum number of monitoring regions of DAMON for the cold memory
monitoring.  This can be used to set upper-bound of the monitoring overhead.
However, setting this too low could result in bad monitoring quality.  Please
refer to the DAMON documentation (:doc:`usage`) for more detail.  1000 by
defaults.

monitor_region_start
--------------------

Start of target memory region in physical address.

The start physical address of memory region that DAMON_LRU_SORT will do work
against.  By default, biggest System RAM is used as the region.

monitor_region_end
------------------

End of target memory region in physical address.

The end physical address of memory region that DAMON_LRU_SORT will do work
against.  By default, biggest System RAM is used as the region.

kdamond_pid
-----------

PID of the DAMON thread.

If DAMON_LRU_SORT is enabled, this becomes the PID of the worker thread.  Else,
-1.

nr_lru_sort_tried_hot_regions
-----------------------------

Number of hot memory regions that tried to be LRU-sorted.

bytes_lru_sort_tried_hot_regions
--------------------------------

Total bytes of hot memory regions that tried to be LRU-sorted.

nr_lru_sorted_hot_regions
-------------------------

Number of hot memory regions that successfully be LRU-sorted.

bytes_lru_sorted_hot_regions
----------------------------

Total bytes of hot memory regions that successfully be LRU-sorted.

nr_hot_quota_exceeds
--------------------

Number of times that the time quota limit for hot regions have exceeded.

nr_lru_sort_tried_cold_regions
------------------------------

Number of cold memory regions that tried to be LRU-sorted.

bytes_lru_sort_tried_cold_regions
---------------------------------

Total bytes of cold memory regions that tried to be LRU-sorted.

nr_lru_sorted_cold_regions
--------------------------

Number of cold memory regions that successfully be LRU-sorted.

bytes_lru_sorted_cold_regions
-----------------------------

Total bytes of cold memory regions that successfully be LRU-sorted.

nr_cold_quota_exceeds
---------------------

Number of times that the time quota limit for cold regions have exceeded.

Example
=======

Below runtime example commands make DAMON_LRU_SORT to find memory regions
having >=50% access frequency and LRU-prioritize while LRU-deprioritizing
memory regions that not accessed for 120 seconds.  The prioritization and
deprioritization is limited to be done using only up to 1% CPU time to avoid
DAMON_LRU_SORT consuming too much CPU time for the (de)prioritization.  It also
asks DAMON_LRU_SORT to do nothing if the system's free memory rate is more than
50%, but start the real works if it becomes lower than 40%.  If DAMON_RECLAIM
doesn't make progress and therefore the free memory rate becomes lower than
20%, it asks DAMON_LRU_SORT to do nothing again, so that we can fall back to
the LRU-list based page granularity reclamation. ::

    # cd /sys/module/damon_lru_sort/parameters
    # echo 500 > hot_thres_access_freq
    # echo 120000000 > cold_min_age
    # echo 10 > quota_ms
    # echo 1000 > quota_reset_interval_ms
    # echo 500 > wmarks_high
    # echo 400 > wmarks_mid
    # echo 200 > wmarks_low
    # echo Y > enabled
