===============================
Memory Management Documentation
===============================

This is a guide to understanding the memory management subsystem
of Linux.  If you are looking for advice on simply allocating memory,
see the :ref:`memory_allocation`.  For controlling and tuning guides,
see the :doc:`admin guide <../admin-guide/mm/index>`.

.. note::

  Unfortunately, parts of this guide are still incomplete or missing.
  While we appreciate contributions, documentation in this area is hard
  to get right and requires a lot of attention to detail.  New contributors
  should reach out to the relevant maintainers early.

  This guide is expected to reflect reality, which requires contributors
  to have a detailed understanding.  Documentation generated with LLMs
  by contributors unfamiliar with these details shifts the real work onto
  reviewers, which is why such contributions will be rejected without
  further comment.

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
   swap-table
   page_cache
   shmfs
   oom

Unsorted Documentation
======================

This is a collection of unsorted documents about the Linux memory management
(MM) subsystem internals with different level of details ranging from notes and
mailing list responses for elaborating descriptions of data structures and
algorithms.  It should all be integrated nicely into the above structured
documentation, or deleted if it has served its purpose.

.. toctree::
   :maxdepth: 1

   active_mm
   allocation-profiling
   arch_pgtable_helpers
   balance
   damon/index
   free_page_reporting
   hmm
   hwpoison
   hugetlbfs_reserv
   ksm
   memory-model
   memfd_preservation
   mmu_notifier
   multigen_lru
   numa
   overcommit-accounting
   page_migration
   page_frags
   page_owner
   page_table_check
   remap_file_pages
   split_page_table_lock
   transhuge
   unevictable-lru
   vmalloced-kernel-stacks
   vmemmap_dedup
   zsmalloc
