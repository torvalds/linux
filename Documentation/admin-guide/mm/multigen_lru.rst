.. SPDX-License-Identifier: GPL-2.0

=============
Multi-Gen LRU
=============
The multi-gen LRU is an alternative LRU implementation that optimizes
page reclaim and improves performance under memory pressure. Page
reclaim decides the kernel's caching policy and ability to overcommit
memory. It directly impacts the kswapd CPU usage and RAM efficiency.

Quick start
===========
Build the kernel with the following configurations.

* ``CONFIG_LRU_GEN=y``
* ``CONFIG_LRU_GEN_ENABLED=y``

All set!

Runtime options
===============
``/sys/kernel/mm/lru_gen/`` contains stable ABIs described in the
following subsections.

Kill switch
-----------
``enabled`` accepts different values to enable or disable the
following components. Its default value depends on
``CONFIG_LRU_GEN_ENABLED``. All the components should be enabled
unless some of them have unforeseen side effects. Writing to
``enabled`` has no effect when a component is not supported by the
hardware, and valid values will be accepted even when the main switch
is off.

====== ===============================================================
Values Components
====== ===============================================================
0x0001 The main switch for the multi-gen LRU.
0x0002 Clearing the accessed bit in leaf page table entries in large
       batches, when MMU sets it (e.g., on x86). This behavior can
       theoretically worsen lock contention (mmap_lock). If it is
       disabled, the multi-gen LRU will suffer a minor performance
       degradation for workloads that contiguously map hot pages,
       whose accessed bits can be otherwise cleared by fewer larger
       batches.
0x0004 Clearing the accessed bit in non-leaf page table entries as
       well, when MMU sets it (e.g., on x86). This behavior was not
       verified on x86 varieties other than Intel and AMD. If it is
       disabled, the multi-gen LRU will suffer a negligible
       performance degradation.
[yYnN] Apply to all the components above.
====== ===============================================================

E.g.,
::

    echo y >/sys/kernel/mm/lru_gen/enabled
    cat /sys/kernel/mm/lru_gen/enabled
    0x0007
    echo 5 >/sys/kernel/mm/lru_gen/enabled
    cat /sys/kernel/mm/lru_gen/enabled
    0x0005

Thrashing prevention
--------------------
Personal computers are more sensitive to thrashing because it can
cause janks (lags when rendering UI) and negatively impact user
experience. The multi-gen LRU offers thrashing prevention to the
majority of laptop and desktop users who do not have ``oomd``.

Users can write ``N`` to ``min_ttl_ms`` to prevent the working set of
``N`` milliseconds from getting evicted. The OOM killer is triggered
if this working set cannot be kept in memory. In other words, this
option works as an adjustable pressure relief valve, and when open, it
terminates applications that are hopefully not being used.

Based on the average human detectable lag (~100ms), ``N=1000`` usually
eliminates intolerable janks due to thrashing. Larger values like
``N=3000`` make janks less noticeable at the risk of premature OOM
kills.

The default value ``0`` means disabled.

Experimental features
=====================
``/sys/kernel/debug/lru_gen`` accepts commands described in the
following subsections. Multiple command lines are supported, so does
concatenation with delimiters ``,`` and ``;``.

``/sys/kernel/debug/lru_gen_full`` provides additional stats for
debugging. ``CONFIG_LRU_GEN_STATS=y`` keeps historical stats from
evicted generations in this file.

Working set estimation
----------------------
Working set estimation measures how much memory an application needs
in a given time interval, and it is usually done with little impact on
the performance of the application. E.g., data centers want to
optimize job scheduling (bin packing) to improve memory utilizations.
When a new job comes in, the job scheduler needs to find out whether
each server it manages can allocate a certain amount of memory for
this new job before it can pick a candidate. To do so, the job
scheduler needs to estimate the working sets of the existing jobs.

When it is read, ``lru_gen`` returns a histogram of numbers of pages
accessed over different time intervals for each memcg and node.
``MAX_NR_GENS`` decides the number of bins for each histogram. The
histograms are noncumulative.
::

    memcg  memcg_id  memcg_path
       node  node_id
           min_gen_nr  age_in_ms  nr_anon_pages  nr_file_pages
           ...
           max_gen_nr  age_in_ms  nr_anon_pages  nr_file_pages

Each bin contains an estimated number of pages that have been accessed
within ``age_in_ms``. E.g., ``min_gen_nr`` contains the coldest pages
and ``max_gen_nr`` contains the hottest pages, since ``age_in_ms`` of
the former is the largest and that of the latter is the smallest.

Users can write the following command to ``lru_gen`` to create a new
generation ``max_gen_nr+1``:

    ``+ memcg_id node_id max_gen_nr [can_swap [force_scan]]``

``can_swap`` defaults to the swap setting and, if it is set to ``1``,
it forces the scan of anon pages when swap is off, and vice versa.
``force_scan`` defaults to ``1`` and, if it is set to ``0``, it
employs heuristics to reduce the overhead, which is likely to reduce
the coverage as well.

A typical use case is that a job scheduler runs this command at a
certain time interval to create new generations, and it ranks the
servers it manages based on the sizes of their cold pages defined by
this time interval.

Proactive reclaim
-----------------
Proactive reclaim induces page reclaim when there is no memory
pressure. It usually targets cold pages only. E.g., when a new job
comes in, the job scheduler wants to proactively reclaim cold pages on
the server it selected, to improve the chance of successfully landing
this new job.

Users can write the following command to ``lru_gen`` to evict
generations less than or equal to ``min_gen_nr``.

    ``- memcg_id node_id min_gen_nr [swappiness [nr_to_reclaim]]``

``min_gen_nr`` should be less than ``max_gen_nr-1``, since
``max_gen_nr`` and ``max_gen_nr-1`` are not fully aged (equivalent to
the active list) and therefore cannot be evicted. ``swappiness``
overrides the default value in ``/proc/sys/vm/swappiness`` and the valid
range is [0-200, max], with max being exclusively used for the reclamation
of anonymous memory. ``nr_to_reclaim`` limits the number of pages to evict.

A typical use case is that a job scheduler runs this command before it
tries to land a new job on a server. If it fails to materialize enough
cold pages because of the overestimation, it retries on the next
server according to the ranking result obtained from the working set
estimation step. This less forceful approach limits the impacts on the
existing jobs.
