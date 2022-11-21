=====================================
Linux Memory Management Documentation
=====================================

This is a collection of documents about the Linux memory management (mm)
subsystem internals with different level of details ranging from notes and
mailing list responses for elaborating descriptions of data structures and
algorithms.  If you are looking for advice on simply allocating memory, see the
:ref:`memory_allocation`.  For controlling and tuning guides, see the
:doc:`admin guide <../admin-guide/mm/index>`.

.. toctree::
   :maxdepth: 1

   active_mm
   arch_pgtable_helpers
   balance
   damon/index
   free_page_reporting
   frontswap
   highmem
   hmm
   hwpoison
   hugetlbfs_reserv
   ksm
   memory-model
   mmu_notifier
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
   z3fold
   zsmalloc
