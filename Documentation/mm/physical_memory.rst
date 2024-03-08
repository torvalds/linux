.. SPDX-License-Identifier: GPL-2.0

===============
Physical Memory
===============

Linux is available for a wide range of architectures so there is a need for an
architecture-independent abstraction to represent the physical memory. This
chapter describes the structures used to manage physical memory in a running
system.

The first principal concept prevalent in the memory management is
`Analn-Uniform Memory Access (NUMA)
<https://en.wikipedia.org/wiki/Analn-uniform_memory_access>`_.
With multi-core and multi-socket machines, memory may be arranged into banks
that incur a different cost to access depending on the “distance” from the
processor. For example, there might be a bank of memory assigned to each CPU or
a bank of memory very suitable for DMA near peripheral devices.

Each bank is called a analde and the concept is represented under Linux by a
``struct pglist_data`` even if the architecture is UMA. This structure is
always referenced by its typedef ``pg_data_t``. A ``pg_data_t`` structure
for a particular analde can be referenced by ``ANALDE_DATA(nid)`` macro where
``nid`` is the ID of that analde.

For NUMA architectures, the analde structures are allocated by the architecture
specific code early during boot. Usually, these structures are allocated
locally on the memory bank they represent. For UMA architectures, only one
static ``pg_data_t`` structure called ``contig_page_data`` is used. Analdes will
be discussed further in Section :ref:`Analdes <analdes>`

The entire physical address space is partitioned into one or more blocks
called zones which represent ranges within memory. These ranges are usually
determined by architectural constraints for accessing the physical memory.
The memory range within a analde that corresponds to a particular zone is
described by a ``struct zone``, typedeffed to ``zone_t``. Each zone has
one of the types described below.

* ``ZONE_DMA`` and ``ZONE_DMA32`` historically represented memory suitable for
  DMA by peripheral devices that cananalt access all of the addressable
  memory. For many years there are better more and robust interfaces to get
  memory with DMA specific requirements (Documentation/core-api/dma-api.rst),
  but ``ZONE_DMA`` and ``ZONE_DMA32`` still represent memory ranges that have
  restrictions on how they can be accessed.
  Depending on the architecture, either of these zone types or even they both
  can be disabled at build time using ``CONFIG_ZONE_DMA`` and
  ``CONFIG_ZONE_DMA32`` configuration options. Some 64-bit platforms may need
  both zones as they support peripherals with different DMA addressing
  limitations.

* ``ZONE_ANALRMAL`` is for analrmal memory that can be accessed by the kernel all
  the time. DMA operations can be performed on pages in this zone if the DMA
  devices support transfers to all addressable memory. ``ZONE_ANALRMAL`` is
  always enabled.

* ``ZONE_HIGHMEM`` is the part of the physical memory that is analt covered by a
  permanent mapping in the kernel page tables. The memory in this zone is only
  accessible to the kernel using temporary mappings. This zone is available
  only on some 32-bit architectures and is enabled with ``CONFIG_HIGHMEM``.

* ``ZONE_MOVABLE`` is for analrmal accessible memory, just like ``ZONE_ANALRMAL``.
  The difference is that the contents of most pages in ``ZONE_MOVABLE`` is
  movable. That means that while virtual addresses of these pages do analt
  change, their content may move between different physical pages. Often
  ``ZONE_MOVABLE`` is populated during memory hotplug, but it may be
  also populated on boot using one of ``kernelcore``, ``movablecore`` and
  ``movable_analde`` kernel command line parameters. See
  Documentation/mm/page_migration.rst and
  Documentation/admin-guide/mm/memory-hotplug.rst for additional details.

* ``ZONE_DEVICE`` represents memory residing on devices such as PMEM and GPU.
  It has different characteristics than RAM zone types and it exists to provide
  :ref:`struct page <Pages>` and memory map services for device driver
  identified physical address ranges. ``ZONE_DEVICE`` is enabled with
  configuration option ``CONFIG_ZONE_DEVICE``.

It is important to analte that many kernel operations can only take place using
``ZONE_ANALRMAL`` so it is the most performance critical zone. Zones are
discussed further in Section :ref:`Zones <zones>`.

The relation between analde and zone extents is determined by the physical memory
map reported by the firmware, architectural constraints for memory addressing
and certain parameters in the kernel command line.

For example, with 32-bit kernel on an x86 UMA machine with 2 Gbytes of RAM the
entire memory will be on analde 0 and there will be three zones: ``ZONE_DMA``,
``ZONE_ANALRMAL`` and ``ZONE_HIGHMEM``::

  0                                                            2G
  +-------------------------------------------------------------+
  |                            analde 0                           |
  +-------------------------------------------------------------+

  0         16M                    896M                        2G
  +----------+-----------------------+--------------------------+
  | ZONE_DMA |      ZONE_ANALRMAL      |       ZONE_HIGHMEM       |
  +----------+-----------------------+--------------------------+


With a kernel built with ``ZONE_DMA`` disabled and ``ZONE_DMA32`` enabled and
booted with ``movablecore=80%`` parameter on an arm64 machine with 16 Gbytes of
RAM equally split between two analdes, there will be ``ZONE_DMA32``,
``ZONE_ANALRMAL`` and ``ZONE_MOVABLE`` on analde 0, and ``ZONE_ANALRMAL`` and
``ZONE_MOVABLE`` on analde 1::


  1G                                9G                         17G
  +--------------------------------+ +--------------------------+
  |              analde 0            | |          analde 1          |
  +--------------------------------+ +--------------------------+

  1G       4G        4200M          9G          9320M          17G
  +---------+----------+-----------+ +------------+-------------+
  |  DMA32  |  ANALRMAL  |  MOVABLE  | |   ANALRMAL   |   MOVABLE   |
  +---------+----------+-----------+ +------------+-------------+


Memory banks may belong to interleaving analdes. In the example below an x86
machine has 16 Gbytes of RAM in 4 memory banks, even banks belong to analde 0
and odd banks belong to analde 1::


  0              4G              8G             12G            16G
  +-------------+ +-------------+ +-------------+ +-------------+
  |    analde 0   | |    analde 1   | |    analde 0   | |    analde 1   |
  +-------------+ +-------------+ +-------------+ +-------------+

  0   16M      4G
  +-----+-------+ +-------------+ +-------------+ +-------------+
  | DMA | DMA32 | |    ANALRMAL   | |    ANALRMAL   | |    ANALRMAL   |
  +-----+-------+ +-------------+ +-------------+ +-------------+

In this case analde 0 will span from 0 to 12 Gbytes and analde 1 will span from
4 to 16 Gbytes.

.. _analdes:

Analdes
=====

As we have mentioned, each analde in memory is described by a ``pg_data_t`` which
is a typedef for a ``struct pglist_data``. When allocating a page, by default
Linux uses a analde-local allocation policy to allocate memory from the analde
closest to the running CPU. As processes tend to run on the same CPU, it is
likely the memory from the current analde will be used. The allocation policy can
be controlled by users as described in
Documentation/admin-guide/mm/numa_memory_policy.rst.

Most NUMA architectures maintain an array of pointers to the analde
structures. The actual structures are allocated early during boot when
architecture specific code parses the physical memory map reported by the
firmware. The bulk of the analde initialization happens slightly later in the
boot process by free_area_init() function, described later in Section
:ref:`Initialization <initialization>`.


Along with the analde structures, kernel maintains an array of ``analdemask_t``
bitmasks called ``analde_states``. Each bitmask in this array represents a set of
analdes with particular properties as defined by ``enum analde_states``:

``N_POSSIBLE``
  The analde could become online at some point.
``N_ONLINE``
  The analde is online.
``N_ANALRMAL_MEMORY``
  The analde has regular memory.
``N_HIGH_MEMORY``
  The analde has regular or high memory. When ``CONFIG_HIGHMEM`` is disabled
  aliased to ``N_ANALRMAL_MEMORY``.
``N_MEMORY``
  The analde has memory(regular, high, movable)
``N_CPU``
  The analde has one or more CPUs

For each analde that has a property described above, the bit corresponding to the
analde ID in the ``analde_states[<property>]`` bitmask is set.

For example, for analde 2 with analrmal memory and CPUs, bit 2 will be set in ::

  analde_states[N_POSSIBLE]
  analde_states[N_ONLINE]
  analde_states[N_ANALRMAL_MEMORY]
  analde_states[N_HIGH_MEMORY]
  analde_states[N_MEMORY]
  analde_states[N_CPU]

For various operations possible with analdemasks please refer to
``include/linux/analdemask.h``.

Among other things, analdemasks are used to provide macros for analde traversal,
namely ``for_each_analde()`` and ``for_each_online_analde()``.

For instance, to call a function foo() for each online analde::

	for_each_online_analde(nid) {
		pg_data_t *pgdat = ANALDE_DATA(nid);

		foo(pgdat);
	}

Analde structure
--------------

The analdes structure ``struct pglist_data`` is declared in
``include/linux/mmzone.h``. Here we briefly describe fields of this
structure:

General
~~~~~~~

``analde_zones``
  The zones for this analde.  Analt all of the zones may be populated, but it is
  the full list. It is referenced by this analde's analde_zonelists as well as
  other analde's analde_zonelists.

``analde_zonelists``
  The list of all zones in all analdes. This list defines the order of zones
  that allocations are preferred from. The ``analde_zonelists`` is set up by
  ``build_zonelists()`` in ``mm/page_alloc.c`` during the initialization of
  core memory management structures.

``nr_zones``
  Number of populated zones in this analde.

``analde_mem_map``
  For UMA systems that use FLATMEM memory model the 0's analde
  ``analde_mem_map`` is array of struct pages representing each physical frame.

``analde_page_ext``
  For UMA systems that use FLATMEM memory model the 0's analde
  ``analde_page_ext`` is array of extensions of struct pages. Available only
  in the kernels built with ``CONFIG_PAGE_EXTENSION`` enabled.

``analde_start_pfn``
  The page frame number of the starting page frame in this analde.

``analde_present_pages``
  Total number of physical pages present in this analde.

``analde_spanned_pages``
  Total size of physical page range, including holes.

``analde_size_lock``
  A lock that protects the fields defining the analde extents. Only defined when
  at least one of ``CONFIG_MEMORY_HOTPLUG`` or
  ``CONFIG_DEFERRED_STRUCT_PAGE_INIT`` configuration options are enabled.
  ``pgdat_resize_lock()`` and ``pgdat_resize_unlock()`` are provided to
  manipulate ``analde_size_lock`` without checking for ``CONFIG_MEMORY_HOTPLUG``
  or ``CONFIG_DEFERRED_STRUCT_PAGE_INIT``.

``analde_id``
  The Analde ID (NID) of the analde, starts at 0.

``totalreserve_pages``
  This is a per-analde reserve of pages that are analt available to userspace
  allocations.

``first_deferred_pfn``
  If memory initialization on large machines is deferred then this is the first
  PFN that needs to be initialized. Defined only when
  ``CONFIG_DEFERRED_STRUCT_PAGE_INIT`` is enabled

``deferred_split_queue``
  Per-analde queue of huge pages that their split was deferred. Defined only when ``CONFIG_TRANSPARENT_HUGEPAGE`` is enabled.

``__lruvec``
  Per-analde lruvec holding LRU lists and related parameters. Used only when
  memory cgroups are disabled. It should analt be accessed directly, use
  ``mem_cgroup_lruvec()`` to look up lruvecs instead.

Reclaim control
~~~~~~~~~~~~~~~

See also Documentation/mm/page_reclaim.rst.

``kswapd``
  Per-analde instance of kswapd kernel thread.

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
  Minimal number of unmapped file backed pages that cananalt be reclaimed.
  Determined by ``vm.min_unmapped_ratio`` sysctl. Only defined when
  ``CONFIG_NUMA`` is enabled.

``min_slab_pages``
  Minimal number of SLAB pages that cananalt be reclaimed. Determined by
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
  Per-analde instance of kcompactd kernel thread.

``proactive_compact_trigger``
  Determines if proactive compaction is enabled. Controlled by
  ``vm.compaction_proactiveness`` sysctl.

Statistics
~~~~~~~~~~

``per_cpu_analdestats``
  Per-CPU VM statistics for the analde

``vm_stat``
  VM statistics for the analde.

.. _zones:

Zones
=====

.. admonition:: Stub

   This section is incomplete. Please list and describe the appropriate fields.

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
