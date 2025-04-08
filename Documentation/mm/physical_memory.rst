.. SPDX-License-Identifier: GPL-2.0

===============
Physical Memory
===============

Linux is available for a wide range of architectures so there is a need for an
architecture-independent abstraction to represent the physical memory. This
chapter describes the structures used to manage physical memory in a running
system.

The first principal concept prevalent in the memory management is
`Non-Uniform Memory Access (NUMA)
<https://en.wikipedia.org/wiki/Non-uniform_memory_access>`_.
With multi-core and multi-socket machines, memory may be arranged into banks
that incur a different cost to access depending on the “distance” from the
processor. For example, there might be a bank of memory assigned to each CPU or
a bank of memory very suitable for DMA near peripheral devices.

Each bank is called a node and the concept is represented under Linux by a
``struct pglist_data`` even if the architecture is UMA. This structure is
always referenced by its typedef ``pg_data_t``. A ``pg_data_t`` structure
for a particular node can be referenced by ``NODE_DATA(nid)`` macro where
``nid`` is the ID of that node.

For NUMA architectures, the node structures are allocated by the architecture
specific code early during boot. Usually, these structures are allocated
locally on the memory bank they represent. For UMA architectures, only one
static ``pg_data_t`` structure called ``contig_page_data`` is used. Nodes will
be discussed further in Section :ref:`Nodes <nodes>`

The entire physical address space is partitioned into one or more blocks
called zones which represent ranges within memory. These ranges are usually
determined by architectural constraints for accessing the physical memory.
The memory range within a node that corresponds to a particular zone is
described by a ``struct zone``. Each zone has
one of the types described below.

* ``ZONE_DMA`` and ``ZONE_DMA32`` historically represented memory suitable for
  DMA by peripheral devices that cannot access all of the addressable
  memory. For many years there are better more and robust interfaces to get
  memory with DMA specific requirements (Documentation/core-api/dma-api.rst),
  but ``ZONE_DMA`` and ``ZONE_DMA32`` still represent memory ranges that have
  restrictions on how they can be accessed.
  Depending on the architecture, either of these zone types or even they both
  can be disabled at build time using ``CONFIG_ZONE_DMA`` and
  ``CONFIG_ZONE_DMA32`` configuration options. Some 64-bit platforms may need
  both zones as they support peripherals with different DMA addressing
  limitations.

* ``ZONE_NORMAL`` is for normal memory that can be accessed by the kernel all
  the time. DMA operations can be performed on pages in this zone if the DMA
  devices support transfers to all addressable memory. ``ZONE_NORMAL`` is
  always enabled.

* ``ZONE_HIGHMEM`` is the part of the physical memory that is not covered by a
  permanent mapping in the kernel page tables. The memory in this zone is only
  accessible to the kernel using temporary mappings. This zone is available
  only on some 32-bit architectures and is enabled with ``CONFIG_HIGHMEM``.

* ``ZONE_MOVABLE`` is for normal accessible memory, just like ``ZONE_NORMAL``.
  The difference is that the contents of most pages in ``ZONE_MOVABLE`` is
  movable. That means that while virtual addresses of these pages do not
  change, their content may move between different physical pages. Often
  ``ZONE_MOVABLE`` is populated during memory hotplug, but it may be
  also populated on boot using one of ``kernelcore``, ``movablecore`` and
  ``movable_node`` kernel command line parameters. See
  Documentation/mm/page_migration.rst and
  Documentation/admin-guide/mm/memory-hotplug.rst for additional details.

* ``ZONE_DEVICE`` represents memory residing on devices such as PMEM and GPU.
  It has different characteristics than RAM zone types and it exists to provide
  :ref:`struct page <Pages>` and memory map services for device driver
  identified physical address ranges. ``ZONE_DEVICE`` is enabled with
  configuration option ``CONFIG_ZONE_DEVICE``.

It is important to note that many kernel operations can only take place using
``ZONE_NORMAL`` so it is the most performance critical zone. Zones are
discussed further in Section :ref:`Zones <zones>`.

The relation between node and zone extents is determined by the physical memory
map reported by the firmware, architectural constraints for memory addressing
and certain parameters in the kernel command line.

For example, with 32-bit kernel on an x86 UMA machine with 2 Gbytes of RAM the
entire memory will be on node 0 and there will be three zones: ``ZONE_DMA``,
``ZONE_NORMAL`` and ``ZONE_HIGHMEM``::

  0                                                            2G
  +-------------------------------------------------------------+
  |                            node 0                           |
  +-------------------------------------------------------------+

  0         16M                    896M                        2G
  +----------+-----------------------+--------------------------+
  | ZONE_DMA |      ZONE_NORMAL      |       ZONE_HIGHMEM       |
  +----------+-----------------------+--------------------------+


With a kernel built with ``ZONE_DMA`` disabled and ``ZONE_DMA32`` enabled and
booted with ``movablecore=80%`` parameter on an arm64 machine with 16 Gbytes of
RAM equally split between two nodes, there will be ``ZONE_DMA32``,
``ZONE_NORMAL`` and ``ZONE_MOVABLE`` on node 0, and ``ZONE_NORMAL`` and
``ZONE_MOVABLE`` on node 1::


  1G                                9G                         17G
  +--------------------------------+ +--------------------------+
  |              node 0            | |          node 1          |
  +--------------------------------+ +--------------------------+

  1G       4G        4200M          9G          9320M          17G
  +---------+----------+-----------+ +------------+-------------+
  |  DMA32  |  NORMAL  |  MOVABLE  | |   NORMAL   |   MOVABLE   |
  +---------+----------+-----------+ +------------+-------------+


Memory banks may belong to interleaving nodes. In the example below an x86
machine has 16 Gbytes of RAM in 4 memory banks, even banks belong to node 0
and odd banks belong to node 1::


  0              4G              8G             12G            16G
  +-------------+ +-------------+ +-------------+ +-------------+
  |    node 0   | |    node 1   | |    node 0   | |    node 1   |
  +-------------+ +-------------+ +-------------+ +-------------+

  0   16M      4G
  +-----+-------+ +-------------+ +-------------+ +-------------+
  | DMA | DMA32 | |    NORMAL   | |    NORMAL   | |    NORMAL   |
  +-----+-------+ +-------------+ +-------------+ +-------------+

In this case node 0 will span from 0 to 12 Gbytes and node 1 will span from
4 to 16 Gbytes.

.. _nodes:

Nodes
=====

As we have mentioned, each node in memory is described by a ``pg_data_t`` which
is a typedef for a ``struct pglist_data``. When allocating a page, by default
Linux uses a node-local allocation policy to allocate memory from the node
closest to the running CPU. As processes tend to run on the same CPU, it is
likely the memory from the current node will be used. The allocation policy can
be controlled by users as described in
Documentation/admin-guide/mm/numa_memory_policy.rst.

Most NUMA architectures maintain an array of pointers to the node
structures. The actual structures are allocated early during boot when
architecture specific code parses the physical memory map reported by the
firmware. The bulk of the node initialization happens slightly later in the
boot process by free_area_init() function, described later in Section
:ref:`Initialization <initialization>`.


Along with the node structures, kernel maintains an array of ``nodemask_t``
bitmasks called ``node_states``. Each bitmask in this array represents a set of
nodes with particular properties as defined by ``enum node_states``:

``N_POSSIBLE``
  The node could become online at some point.
``N_ONLINE``
  The node is online.
``N_NORMAL_MEMORY``
  The node has regular memory.
``N_HIGH_MEMORY``
  The node has regular or high memory. When ``CONFIG_HIGHMEM`` is disabled
  aliased to ``N_NORMAL_MEMORY``.
``N_MEMORY``
  The node has memory(regular, high, movable)
``N_CPU``
  The node has one or more CPUs

For each node that has a property described above, the bit corresponding to the
node ID in the ``node_states[<property>]`` bitmask is set.

For example, for node 2 with normal memory and CPUs, bit 2 will be set in ::

  node_states[N_POSSIBLE]
  node_states[N_ONLINE]
  node_states[N_NORMAL_MEMORY]
  node_states[N_HIGH_MEMORY]
  node_states[N_MEMORY]
  node_states[N_CPU]

For various operations possible with nodemasks please refer to
``include/linux/nodemask.h``.

Among other things, nodemasks are used to provide macros for node traversal,
namely ``for_each_node()`` and ``for_each_online_node()``.

For instance, to call a function foo() for each online node::

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);

		foo(pgdat);
	}

Node structure
--------------

The nodes structure ``struct pglist_data`` is declared in
``include/linux/mmzone.h``. Here we briefly describe fields of this
structure:

General
~~~~~~~

``node_zones``
  The zones for this node.  Not all of the zones may be populated, but it is
  the full list. It is referenced by this node's node_zonelists as well as
  other node's node_zonelists.

``node_zonelists``
  The list of all zones in all nodes. This list defines the order of zones
  that allocations are preferred from. The ``node_zonelists`` is set up by
  ``build_zonelists()`` in ``mm/page_alloc.c`` during the initialization of
  core memory management structures.

``nr_zones``
  Number of populated zones in this node.

``node_mem_map``
  For UMA systems that use FLATMEM memory model the 0's node
  ``node_mem_map`` is array of struct pages representing each physical frame.

``node_page_ext``
  For UMA systems that use FLATMEM memory model the 0's node
  ``node_page_ext`` is array of extensions of struct pages. Available only
  in the kernels built with ``CONFIG_PAGE_EXTENSION`` enabled.

``node_start_pfn``
  The page frame number of the starting page frame in this node.

``node_present_pages``
  Total number of physical pages present in this node.

``node_spanned_pages``
  Total size of physical page range, including holes.

``node_size_lock``
  A lock that protects the fields defining the node extents. Only defined when
  at least one of ``CONFIG_MEMORY_HOTPLUG`` or
  ``CONFIG_DEFERRED_STRUCT_PAGE_INIT`` configuration options are enabled.
  ``pgdat_resize_lock()`` and ``pgdat_resize_unlock()`` are provided to
  manipulate ``node_size_lock`` without checking for ``CONFIG_MEMORY_HOTPLUG``
  or ``CONFIG_DEFERRED_STRUCT_PAGE_INIT``.

``node_id``
  The Node ID (NID) of the node, starts at 0.

``totalreserve_pages``
  This is a per-node reserve of pages that are not available to userspace
  allocations.

``first_deferred_pfn``
  If memory initialization on large machines is deferred then this is the first
  PFN that needs to be initialized. Defined only when
  ``CONFIG_DEFERRED_STRUCT_PAGE_INIT`` is enabled

``deferred_split_queue``
  Per-node queue of huge pages that their split was deferred. Defined only when ``CONFIG_TRANSPARENT_HUGEPAGE`` is enabled.

``__lruvec``
  Per-node lruvec holding LRU lists and related parameters. Used only when
  memory cgroups are disabled. It should not be accessed directly, use
  ``mem_cgroup_lruvec()`` to look up lruvecs instead.

Reclaim control
~~~~~~~~~~~~~~~

See also Documentation/mm/page_reclaim.rst.

``kswapd``
  Per-node instance of kswapd kernel thread.

``kswapd_wait``, ``pfmemalloc_wait``, ``reclaim_wait``
  Workqueues used to synchronize memory reclaim tasks

``nr_writeback_throttled``
  Number of tasks that are throttled waiting on dirty pages to clean.

``nr_reclaim_start``
  Number of pages written while reclaim is throttled waiting for writeback.

``kswapd_order``
  Controls the order kswapd tries to reclaim

``kswapd_highest_zoneidx``
  The highest zone index to be reclaimed by kswapd

``kswapd_failures``
  Number of runs kswapd was unable to reclaim any pages

``min_unmapped_pages``
  Minimal number of unmapped file backed pages that cannot be reclaimed.
  Determined by ``vm.min_unmapped_ratio`` sysctl. Only defined when
  ``CONFIG_NUMA`` is enabled.

``min_slab_pages``
  Minimal number of SLAB pages that cannot be reclaimed. Determined by
  ``vm.min_slab_ratio sysctl``. Only defined when ``CONFIG_NUMA`` is enabled

``flags``
  Flags controlling reclaim behavior.

Compaction control
~~~~~~~~~~~~~~~~~~

``kcompactd_max_order``
  Page order that kcompactd should try to achieve.

``kcompactd_highest_zoneidx``
  The highest zone index to be compacted by kcompactd.

``kcompactd_wait``
  Workqueue used to synchronize memory compaction tasks.

``kcompactd``
  Per-node instance of kcompactd kernel thread.

``proactive_compact_trigger``
  Determines if proactive compaction is enabled. Controlled by
  ``vm.compaction_proactiveness`` sysctl.

Statistics
~~~~~~~~~~

``per_cpu_nodestats``
  Per-CPU VM statistics for the node

``vm_stat``
  VM statistics for the node.

.. _zones:

Zones
=====
As we have mentioned, each zone in memory is described by a ``struct zone``
which is an element of the ``node_zones`` array of the node it belongs to.
``struct zone`` is the core data structure of the page allocator. A zone
represents a range of physical memory and may have holes.

The page allocator uses the GFP flags, see :ref:`mm-api-gfp-flags`, specified by
a memory allocation to determine the highest zone in a node from which the
memory allocation can allocate memory. The page allocator first allocates memory
from that zone, if the page allocator can't allocate the requested amount of
memory from the zone, it will allocate memory from the next lower zone in the
node, the process continues up to and including the lowest zone. For example, if
a node contains ``ZONE_DMA32``, ``ZONE_NORMAL`` and ``ZONE_MOVABLE`` and the
highest zone of a memory allocation is ``ZONE_MOVABLE``, the order of the zones
from which the page allocator allocates memory is ``ZONE_MOVABLE`` >
``ZONE_NORMAL`` > ``ZONE_DMA32``.

At runtime, free pages in a zone are in the Per-CPU Pagesets (PCP) or free areas
of the zone. The Per-CPU Pagesets are a vital mechanism in the kernel's memory
management system. By handling most frequent allocations and frees locally on
each CPU, the Per-CPU Pagesets improve performance and scalability, especially
on systems with many cores. The page allocator in the kernel employs a two-step
strategy for memory allocation, starting with the Per-CPU Pagesets before
falling back to the buddy allocator. Pages are transferred between the Per-CPU
Pagesets and the global free areas (managed by the buddy allocator) in batches.
This minimizes the overhead of frequent interactions with the global buddy
allocator.

Architecture specific code calls free_area_init() to initializes zones.

Zone structure
--------------
The zones structure ``struct zone`` is defined in ``include/linux/mmzone.h``.
Here we briefly describe fields of this structure:

General
~~~~~~~

``_watermark``
  The watermarks for this zone. When the amount of free pages in a zone is below
  the min watermark, boosting is ignored, an allocation may trigger direct
  reclaim and direct compaction, it is also used to throttle direct reclaim.
  When the amount of free pages in a zone is below the low watermark, kswapd is
  woken up. When the amount of free pages in a zone is above the high watermark,
  kswapd stops reclaiming (a zone is balanced) when the
  ``NUMA_BALANCING_MEMORY_TIERING`` bit of ``sysctl_numa_balancing_mode`` is not
  set. The promo watermark is used for memory tiering and NUMA balancing. When
  the amount of free pages in a zone is above the promo watermark, kswapd stops
  reclaiming when the ``NUMA_BALANCING_MEMORY_TIERING`` bit of
  ``sysctl_numa_balancing_mode`` is set. The watermarks are set by
  ``__setup_per_zone_wmarks()``. The min watermark is calculated according to
  ``vm.min_free_kbytes`` sysctl. The other three watermarks are set according
  to the distance between two watermarks. The distance itself is calculated
  taking ``vm.watermark_scale_factor`` sysctl into account.

``watermark_boost``
  The number of pages which are used to boost watermarks to increase reclaim
  pressure to reduce the likelihood of future fallbacks and wake kswapd now
  as the node may be balanced overall and kswapd will not wake naturally.

``nr_reserved_highatomic``
  The number of pages which are reserved for high-order atomic allocations.

``nr_free_highatomic``
  The number of free pages in reserved highatomic pageblocks

``lowmem_reserve``
  The array of the amounts of the memory reserved in this zone for memory
  allocations. For example, if the highest zone a memory allocation can
  allocate memory from is ``ZONE_MOVABLE``, the amount of memory reserved in
  this zone for this allocation is ``lowmem_reserve[ZONE_MOVABLE]`` when
  attempting to allocate memory from this zone. This is a mechanism the page
  allocator uses to prevent allocations which could use ``highmem`` from using
  too much ``lowmem``. For some specialised workloads on ``highmem`` machines,
  it is dangerous for the kernel to allow process memory to be allocated from
  the ``lowmem`` zone. This is because that memory could then be pinned via the
  ``mlock()`` system call, or by unavailability of swapspace.
  ``vm.lowmem_reserve_ratio`` sysctl determines how aggressive the kernel is in
  defending these lower zones. This array is recalculated by
  ``setup_per_zone_lowmem_reserve()`` at runtime if ``vm.lowmem_reserve_ratio``
  sysctl changes.

``node``
  The index of the node this zone belongs to. Available only when
  ``CONFIG_NUMA`` is enabled because there is only one zone in a UMA system.

``zone_pgdat``
  Pointer to the ``struct pglist_data`` of the node this zone belongs to.

``per_cpu_pageset``
  Pointer to the Per-CPU Pagesets (PCP) allocated and initialized by
  ``setup_zone_pageset()``. By handling most frequent allocations and frees
  locally on each CPU, PCP improves performance and scalability on systems with
  many cores.

``pageset_high_min``
  Copied to the ``high_min`` of the Per-CPU Pagesets for faster access.

``pageset_high_max``
  Copied to the ``high_max`` of the Per-CPU Pagesets for faster access.

``pageset_batch``
  Copied to the ``batch`` of the Per-CPU Pagesets for faster access. The
  ``batch``, ``high_min`` and ``high_max`` of the Per-CPU Pagesets are used to
  calculate the number of elements the Per-CPU Pagesets obtain from the buddy
  allocator under a single hold of the lock for efficiency. They are also used
  to decide if the Per-CPU Pagesets return pages to the buddy allocator in page
  free process.

``pageblock_flags``
  The pointer to the flags for the pageblocks in the zone (see
  ``include/linux/pageblock-flags.h`` for flags list). The memory is allocated
  in ``setup_usemap()``. Each pageblock occupies ``NR_PAGEBLOCK_BITS`` bits.
  Defined only when ``CONFIG_FLATMEM`` is enabled. The flags is stored in
  ``mem_section`` when ``CONFIG_SPARSEMEM`` is enabled.

``zone_start_pfn``
  The start pfn of the zone. It is initialized by
  ``calculate_node_totalpages()``.

``managed_pages``
  The present pages managed by the buddy system, which is calculated as:
  ``managed_pages`` = ``present_pages`` - ``reserved_pages``, ``reserved_pages``
  includes pages allocated by the memblock allocator. It should be used by page
  allocator and vm scanner to calculate all kinds of watermarks and thresholds.
  It is accessed using ``atomic_long_xxx()`` functions. It is initialized in
  ``free_area_init_core()`` and then is reinitialized when memblock allocator
  frees pages into buddy system.

``spanned_pages``
  The total pages spanned by the zone, including holes, which is calculated as:
  ``spanned_pages`` = ``zone_end_pfn`` - ``zone_start_pfn``. It is initialized
  by ``calculate_node_totalpages()``.

``present_pages``
  The physical pages existing within the zone, which is calculated as:
  ``present_pages`` = ``spanned_pages`` - ``absent_pages`` (pages in holes). It
  may be used by memory hotplug or memory power management logic to figure out
  unmanaged pages by checking (``present_pages`` - ``managed_pages``). Write
  access to ``present_pages`` at runtime should be protected by
  ``mem_hotplug_begin/done()``. Any reader who can't tolerant drift of
  ``present_pages`` should use ``get_online_mems()`` to get a stable value. It
  is initialized by ``calculate_node_totalpages()``.

``present_early_pages``
  The present pages existing within the zone located on memory available since
  early boot, excluding hotplugged memory. Defined only when
  ``CONFIG_MEMORY_HOTPLUG`` is enabled and initialized by
  ``calculate_node_totalpages()``.

``cma_pages``
  The pages reserved for CMA use. These pages behave like ``ZONE_MOVABLE`` when
  they are not used for CMA. Defined only when ``CONFIG_CMA`` is enabled.

``name``
  The name of the zone. It is a pointer to the corresponding element of
  the ``zone_names`` array.

``nr_isolate_pageblock``
  Number of isolated pageblocks. It is used to solve incorrect freepage counting
  problem due to racy retrieving migratetype of pageblock. Protected by
  ``zone->lock``. Defined only when ``CONFIG_MEMORY_ISOLATION`` is enabled.

``span_seqlock``
  The seqlock to protect ``zone_start_pfn`` and ``spanned_pages``. It is a
  seqlock because it has to be read outside of ``zone->lock``, and it is done in
  the main allocator path. However, the seqlock is written quite infrequently.
  Defined only when ``CONFIG_MEMORY_HOTPLUG`` is enabled.

``initialized``
  The flag indicating if the zone is initialized. Set by
  ``init_currently_empty_zone()`` during boot.

``free_area``
  The array of free areas, where each element corresponds to a specific order
  which is a power of two. The buddy allocator uses this structure to manage
  free memory efficiently. When allocating, it tries to find the smallest
  sufficient block, if the smallest sufficient block is larger than the
  requested size, it will be recursively split into the next smaller blocks
  until the required size is reached. When a page is freed, it may be merged
  with its buddy to form a larger block. It is initialized by
  ``zone_init_free_lists()``.

``unaccepted_pages``
  The list of pages to be accepted. All pages on the list are ``MAX_PAGE_ORDER``.
  Defined only when ``CONFIG_UNACCEPTED_MEMORY`` is enabled.

``flags``
  The zone flags. The least three bits are used and defined by
  ``enum zone_flags``. ``ZONE_BOOSTED_WATERMARK`` (bit 0): zone recently boosted
  watermarks. Cleared when kswapd is woken. ``ZONE_RECLAIM_ACTIVE`` (bit 1):
  kswapd may be scanning the zone. ``ZONE_BELOW_HIGH`` (bit 2): zone is below
  high watermark.

``lock``
  The main lock that protects the internal data structures of the page allocator
  specific to the zone, especially protects ``free_area``.

``percpu_drift_mark``
  When free pages are below this point, additional steps are taken when reading
  the number of free pages to avoid per-cpu counter drift allowing watermarks
  to be breached. It is updated in ``refresh_zone_stat_thresholds()``.

Compaction control
~~~~~~~~~~~~~~~~~~

``compact_cached_free_pfn``
  The PFN where compaction free scanner should start in the next scan.

``compact_cached_migrate_pfn``
  The PFNs where compaction migration scanner should start in the next scan.
  This array has two elements: the first one is used in ``MIGRATE_ASYNC`` mode,
  and the other one is used in ``MIGRATE_SYNC`` mode.

``compact_init_migrate_pfn``
  The initial migration PFN which is initialized to 0 at boot time, and to the
  first pageblock with migratable pages in the zone after a full compaction
  finishes. It is used to check if a scan is a whole zone scan or not.

``compact_init_free_pfn``
  The initial free PFN which is initialized to 0 at boot time and to the last
  pageblock with free ``MIGRATE_MOVABLE`` pages in the zone. It is used to check
  if it is the start of a scan.

``compact_considered``
  The number of compactions attempted since last failure. It is reset in
  ``defer_compaction()`` when a compaction fails to result in a page allocation
  success. It is increased by 1 in ``compaction_deferred()`` when a compaction
  should be skipped. ``compaction_deferred()`` is called before
  ``compact_zone()`` is called, ``compaction_defer_reset()`` is called when
  ``compact_zone()`` returns ``COMPACT_SUCCESS``, ``defer_compaction()`` is
  called when ``compact_zone()`` returns ``COMPACT_PARTIAL_SKIPPED`` or
  ``COMPACT_COMPLETE``.

``compact_defer_shift``
  The number of compactions skipped before trying again is
  ``1<<compact_defer_shift``. It is increased by 1 in ``defer_compaction()``.
  It is reset in ``compaction_defer_reset()`` when a direct compaction results
  in a page allocation success. Its maximum value is ``COMPACT_MAX_DEFER_SHIFT``.

``compact_order_failed``
  The minimum compaction failed order. It is set in ``compaction_defer_reset()``
  when a compaction succeeds and in ``defer_compaction()`` when a compaction
  fails to result in a page allocation success.

``compact_blockskip_flush``
  Set to true when compaction migration scanner and free scanner meet, which
  means the ``PB_migrate_skip`` bits should be cleared.

``contiguous``
  Set to true when the zone is contiguous (in other words, no hole).

Statistics
~~~~~~~~~~

``vm_stat``
  VM statistics for the zone. The items tracked are defined by
  ``enum zone_stat_item``.

``vm_numa_event``
  VM NUMA event statistics for the zone. The items tracked are defined by
  ``enum numa_stat_item``.

``per_cpu_zonestats``
  Per-CPU VM statistics for the zone. It records VM statistics and VM NUMA event
  statistics on a per-CPU basis. It reduces updates to the global ``vm_stat``
  and ``vm_numa_event`` fields of the zone to improve performance.

.. _pages:

Pages
=====

.. admonition:: Stub

   This section is incomplete. Please list and describe the appropriate fields.

.. _folios:

Folios
======

.. admonition:: Stub

   This section is incomplete. Please list and describe the appropriate fields.

.. _initialization:

Initialization
==============

.. admonition:: Stub

   This section is incomplete. Please list and describe the appropriate fields.
