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
evict or protect.

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

Summary
-------
The multi-gen LRU can be disassembled into the following parts:

* Generations
* Rmap walks
* Page table walks
* Bloom filters
* PID controller

The aging and the eviction form a producer-consumer model;
specifically, the latter drives the former by the sliding window over
generations. Within the aging, rmap walks drive page table walks by
inserting hot densely populated page tables to the Bloom filters.
Within the eviction, the PID controller uses refaults as the feedback
to select types to evict and tiers to protect.
