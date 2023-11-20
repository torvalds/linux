======================
Memory Management APIs
======================

User Space Memory Access
========================

.. kernel-doc:: arch/x86/include/asm/uaccess.h
   :internal:

.. kernel-doc:: arch/x86/lib/usercopy_32.c
   :export:

.. kernel-doc:: mm/gup.c
   :functions: get_user_pages_fast

.. _mm-api-gfp-flags:

Memory Allocation Controls
==========================

.. kernel-doc:: include/linux/gfp_types.h
   :doc: Page mobility and placement hints

.. kernel-doc:: include/linux/gfp_types.h
   :doc: Watermark modifiers

.. kernel-doc:: include/linux/gfp_types.h
   :doc: Reclaim modifiers

.. kernel-doc:: include/linux/gfp_types.h
   :doc: Useful GFP flag combinations

The Slab Cache
==============

.. kernel-doc:: include/linux/slab.h
   :internal:

.. kernel-doc:: mm/slub.c
   :export:

.. kernel-doc:: mm/slab_common.c
   :export:

.. kernel-doc:: mm/util.c
   :functions: kfree_const kvmalloc_node kvfree

Virtually Contiguous Mappings
=============================

.. kernel-doc:: mm/vmalloc.c
   :export:

File Mapping and Page Cache
===========================

Filemap
-------

.. kernel-doc:: mm/filemap.c
   :export:

Readahead
---------

.. kernel-doc:: mm/readahead.c
   :doc: Readahead Overview

.. kernel-doc:: mm/readahead.c
   :export:

Writeback
---------

.. kernel-doc:: mm/page-writeback.c
   :export:

Truncate
--------

.. kernel-doc:: mm/truncate.c
   :export:

.. kernel-doc:: include/linux/pagemap.h
   :internal:

Memory pools
============

.. kernel-doc:: mm/mempool.c
   :export:

DMA pools
=========

.. kernel-doc:: mm/dmapool.c
   :export:

More Memory Management Functions
================================

.. kernel-doc:: mm/memory.c
   :export:

.. kernel-doc:: mm/page_alloc.c
.. kernel-doc:: mm/mempolicy.c
.. kernel-doc:: include/linux/mm_types.h
   :internal:
.. kernel-doc:: include/linux/mm_inline.h
.. kernel-doc:: include/linux/page-flags.h
.. kernel-doc:: include/linux/mm.h
   :internal:
.. kernel-doc:: include/linux/page_ref.h
.. kernel-doc:: include/linux/mmzone.h
.. kernel-doc:: mm/util.c
   :functions: folio_mapping

.. kernel-doc:: mm/rmap.c
.. kernel-doc:: mm/migrate.c
.. kernel-doc:: mm/mmap.c
.. kernel-doc:: mm/kmemleak.c
.. #kernel-doc:: mm/hmm.c (build warnings)
.. kernel-doc:: mm/memremap.c
.. kernel-doc:: mm/hugetlb.c
.. kernel-doc:: mm/swap.c
.. kernel-doc:: mm/zpool.c
.. kernel-doc:: mm/memcontrol.c
.. #kernel-doc:: mm/memory-tiers.c (build warnings)
.. kernel-doc:: mm/shmem.c
.. kernel-doc:: mm/migrate_device.c
.. #kernel-doc:: mm/nommu.c (duplicates kernel-doc from other files)
.. kernel-doc:: mm/mapping_dirty_helpers.c
.. #kernel-doc:: mm/memory-failure.c (build warnings)
.. kernel-doc:: mm/percpu.c
.. kernel-doc:: mm/maccess.c
.. kernel-doc:: mm/vmscan.c
.. kernel-doc:: mm/memory_hotplug.c
.. kernel-doc:: mm/mmu_notifier.c
.. kernel-doc:: mm/balloon_compaction.c
.. kernel-doc:: mm/huge_memory.c
.. kernel-doc:: mm/io-mapping.c
