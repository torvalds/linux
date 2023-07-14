.. SPDX-License-Identifier: GPL-2.0

=============
Multi-Gen LRU
=============
The multi-gen LRU is an alternative LRU implementation that optimizes
page reclaim and improves performance under memory pressure. Page
reclaim decides the kernel's caching policy and ability to overcommit
memory. It directly impacts the kswapd CPU usage and RAM efficiency.

Design overview
===============
Objectives
----------
The design objectives are:

* Good representation of access recency
* Try to profit from spatial locality
* Fast paths to make obvious choices
* Simple self-correcting heuristics

The representation of access recency is at the core of all LRU
implementations. In the multi-gen LRU, each generation represents a
group of pages with similar access recency. Generations establish a
(time-based) common frame of reference and therefore help make better
choices, e.g., between different memcgs on a computer or different
computers in a data center (for job scheduling).

Exploiting spatial locality improves efficiency when gathering the
accessed bit. A rmap walk targets a single page and does not try to
profit from discovering a young PTE. A page table walk can sweep all
the young PTEs in an address space, but the address space can be too
sparse to make a profit. The key is to optimize both methods and use
them in combination.

Fast paths reduce code complexity and runtime overhead. Unmapped pages
do not require TLB flushes; clean pages do not require writeback.
These facts are only helpful when other conditions, e.g., access
recency, are similar. With generations as a common frame of reference,
additional factors stand out. But obvious choices might not be good
choices; thus self-correction is necessary.

The benefits of simple self-correcting heuristics are self-evident.
Again, with generations as a common frame of reference, this becomes
attainable. Specifically, pages in the same generation can be
categorized based on additional factors, and a feedback loop can
statistically compare the refault percentages across those categories
and infer which of them are better choices.

Assumptions
-----------
The protection of hot pages and the selection of cold pages are based
on page access channels and patterns. There are two access channels:

* Accesses through page tables
* Accesses through file descriptors

The protection of the former channel is by design stronger because:

1. The uncertainty in determining the access patterns of the former
   channel is higher due to the approximation of the accessed bit.
2. The cost of evicting the former channel is higher due to the TLB
   flushes required and the likelihood of encountering the dirty bit.
3. The penalty of underprotecting the former channel is higher because
   applications usually do not prepare themselves for major page
   faults like they do for blocked I/O. E.g., GUI applications
   commonly use dedicated I/O threads to avoid blocking rendering
   threads.

There are also two access patterns:

* Accesses exhibiting temporal locality
* Accesses not exhibiting temporal locality

For the reasons listed above, the former channel is assumed to follow
the former pattern unless ``VM_SEQ_READ`` or ``VM_RAND_READ`` is
present, and the latter channel is assumed to follow the latter
pattern unless outlying refaults have been observed.

Workflow overview
=================
Evictable pages are divided into multiple generations for each
``lruvec``. The youngest generation number is stored in
``lrugen->max_seq`` for both anon and file types as they are aged on
an equal footing. The oldest generation numbers are stored in
``lrugen->min_seq[]`` separately for anon and file types as clean file
pages can be evicted regardless of swap constraints. These three
variables are monotonically increasing.

Generation numbers are truncated into ``order_base_2(MAX_NR_GENS+1)``
bits in order to fit into the gen counter in ``folio->flags``. Each
truncated generation number is an index to ``lrugen->folios[]``. The
sliding window technique is used to track at least ``MIN_NR_GENS`` and
at most ``MAX_NR_GENS`` generations. The gen counter stores a value
within ``[1, MAX_NR_GENS]`` while a page is on one of
``lrugen->folios[]``; otherwise it stores zero.

Each generation is divided into multiple tiers. A page accessed ``N``
times through file descriptors is in tier ``order_base_2(N)``. Unlike
generations, tiers do not have dedicated ``lrugen->folios[]``. In
contrast to moving across generations, which requires the LRU lock,
moving across tiers only involves atomic operations on
``folio->flags`` and therefore has a negligible cost. A feedback loop
modeled after the PID controller monitors refaults over all the tiers
from anon and file types and decides which tiers from which types to
evict or protect. The desired effect is to balance refault percentages
between anon and file types proportional to the swappiness level.

There are two conceptually independent procedures: the aging and the
eviction. They form a closed-loop system, i.e., the page reclaim.

Aging
-----
The aging produces young generations. Given an ``lruvec``, it
increments ``max_seq`` when ``max_seq-min_seq+1`` approaches
``MIN_NR_GENS``. The aging promotes hot pages to the youngest
generation when it finds them accessed through page tables; the
demotion of cold pages happens consequently when it increments
``max_seq``. The aging uses page table walks and rmap walks to find
young PTEs. For the former, it iterates ``lruvec_memcg()->mm_list``
and calls ``walk_page_range()`` with each ``mm_struct`` on this list
to scan PTEs, and after each iteration, it increments ``max_seq``. For
the latter, when the eviction walks the rmap and finds a young PTE,
the aging scans the adjacent PTEs. For both, on finding a young PTE,
the aging clears the accessed bit and updates the gen counter of the
page mapped by this PTE to ``(max_seq%MAX_NR_GENS)+1``.

Eviction
--------
The eviction consumes old generations. Given an ``lruvec``, it
increments ``min_seq`` when ``lrugen->folios[]`` indexed by
``min_seq%MAX_NR_GENS`` becomes empty. To select a type and a tier to
evict from, it first compares ``min_seq[]`` to select the older type.
If both types are equally old, it selects the one whose first tier has
a lower refault percentage. The first tier contains single-use
unmapped clean pages, which are the best bet. The eviction sorts a
page according to its gen counter if the aging has found this page
accessed through page tables and updated its gen counter. It also
moves a page to the next generation, i.e., ``min_seq+1``, if this page
was accessed multiple times through file descriptors and the feedback
loop has detected outlying refaults from the tier this page is in. To
this end, the feedback loop uses the first tier as the baseline, for
the reason stated earlier.

Working set protection
----------------------
Each generation is timestamped at birth. If ``lru_gen_min_ttl`` is
set, an ``lruvec`` is protected from the eviction when its oldest
generation was born within ``lru_gen_min_ttl`` milliseconds. In other
words, it prevents the working set of ``lru_gen_min_ttl`` milliseconds
from getting evicted. The OOM killer is triggered if this working set
cannot be kept in memory.

This time-based approach has the following advantages:

1. It is easier to configure because it is agnostic to applications
   and memory sizes.
2. It is more reliable because it is directly wired to the OOM killer.

``mm_struct`` list
------------------
An ``mm_struct`` list is maintained for each memcg, and an
``mm_struct`` follows its owner task to the new memcg when this task
is migrated.

A page table walker iterates ``lruvec_memcg()->mm_list`` and calls
``walk_page_range()`` with each ``mm_struct`` on this list to scan
PTEs. When multiple page table walkers iterate the same list, each of
them gets a unique ``mm_struct``, and therefore they can run in
parallel.

Page table walkers ignore any misplaced pages, e.g., if an
``mm_struct`` was migrated, pages left in the previous memcg will be
ignored when the current memcg is under reclaim. Similarly, page table
walkers will ignore pages from nodes other than the one under reclaim.

This infrastructure also tracks the usage of ``mm_struct`` between
context switches so that page table walkers can skip processes that
have been sleeping since the last iteration.

Rmap/PT walk feedback
---------------------
Searching the rmap for PTEs mapping each page on an LRU list (to test
and clear the accessed bit) can be expensive because pages from
different VMAs (PA space) are not cache friendly to the rmap (VA
space). For workloads mostly using mapped pages, searching the rmap
can incur the highest CPU cost in the reclaim path.

``lru_gen_look_around()`` exploits spatial locality to reduce the
trips into the rmap. It scans the adjacent PTEs of a young PTE and
promotes hot pages. If the scan was done cacheline efficiently, it
adds the PMD entry pointing to the PTE table to the Bloom filter. This
forms a feedback loop between the eviction and the aging.

Bloom filters
-------------
Bloom filters are a space and memory efficient data structure for set
membership test, i.e., test if an element is not in the set or may be
in the set.

In the eviction path, specifically, in ``lru_gen_look_around()``, if a
PMD has a sufficient number of hot pages, its address is placed in the
filter. In the aging path, set membership means that the PTE range
will be scanned for young pages.

Note that Bloom filters are probabilistic on set membership. If a test
is false positive, the cost is an additional scan of a range of PTEs,
which may yield hot pages anyway. Parameters of the filter itself can
control the false positive rate in the limit.

PID controller
--------------
A feedback loop modeled after the Proportional-Integral-Derivative
(PID) controller monitors refaults over anon and file types and
decides which type to evict when both types are available from the
same generation.

The PID controller uses generations rather than the wall clock as the
time domain because a CPU can scan pages at different rates under
varying memory pressure. It calculates a moving average for each new
generation to avoid being permanently locked in a suboptimal state.

Memcg LRU
---------
An memcg LRU is a per-node LRU of memcgs. It is also an LRU of LRUs,
since each node and memcg combination has an LRU of folios (see
``mem_cgroup_lruvec()``). Its goal is to improve the scalability of
global reclaim, which is critical to system-wide memory overcommit in
data centers. Note that memcg LRU only applies to global reclaim.

The basic structure of an memcg LRU can be understood by an analogy to
the active/inactive LRU (of folios):

1. It has the young and the old (generations), i.e., the counterparts
   to the active and the inactive;
2. The increment of ``max_seq`` triggers promotion, i.e., the
   counterpart to activation;
3. Other events trigger similar operations, e.g., offlining an memcg
   triggers demotion, i.e., the counterpart to deactivation.

In terms of global reclaim, it has two distinct features:

1. Sharding, which allows each thread to start at a random memcg (in
   the old generation) and improves parallelism;
2. Eventual fairness, which allows direct reclaim to bail out at will
   and reduces latency without affecting fairness over some time.

In terms of traversing memcgs during global reclaim, it improves the
best-case complexity from O(n) to O(1) and does not affect the
worst-case complexity O(n). Therefore, on average, it has a sublinear
complexity.

Summary
-------
The multi-gen LRU (of folios) can be disassembled into the following
parts:

* Generations
* Rmap walks
* Page table walks via ``mm_struct`` list
* Bloom filters for rmap/PT walk feedback
* PID controller for refault feedback

The aging and the eviction form a producer-consumer model;
specifically, the latter drives the former by the sliding window over
generations. Within the aging, rmap walks drive page table walks by
inserting hot densely populated page tables to the Bloom filters.
Within the eviction, the PID controller uses refaults as the feedback
to select types to evict and tiers to protect.
