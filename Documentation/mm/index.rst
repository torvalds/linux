===============================
Memory Management Documentation
===============================

Memory Management Guide
=======================

This is a guide to understanding the memory management subsystem
of Linux.  If you are looking for advice on simply allocating memory,
see the :ref:`memory_allocation`.  For controlling and tuning guides,
see the :doc:`admin guide <../admin-guide/mm/index>`.

.. toctree::
   :maxdepth: 1

   physical_memory
   page_tables
   process_addrs
   bootmem
   page_allocation
   vmalloc
   slab
   highmem
   page_reclaim
   swap
   page_cache
   shmfs
   oom
   allocation-profiling

Legacy Documentation
====================

This is a collection of older documents about the Linux memory management
(MM) subsystem internals with different level of details ranging from
notes and mailing list responses for elaborating descriptions of data
structures and algorithms.  It should all be integrated nicely into the
above structured documentation, or deleted if it has served its purpose.

.. toctree::
   :maxdepth: 1

   active_mm
   arch_pgtable_helpers
   balance
   damon/index
   free_page_reporting
   hmm
   hwpoison
   hugetlbfs_reserv
   ksm
   memory-model
   mmu_notifier
   multigen_lru
   numa
   overcommit-accounting
   page_migration
   page_frags
   page_owner
   page_table_check
   remap_file_pages
   slub
   split_page_table_lock
   transhuge
   unevictable-lru
   vmalloced-kernel-stacks
   vmemmap_dedup
   z3fold
   zsmalloc
