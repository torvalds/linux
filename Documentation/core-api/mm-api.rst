======================
Memory Management APIs
======================

User Space Memory Access
========================

.. kernel-doc:: arch/x86/include/asm/uaccess.h
   :internal:

.. kernel-doc:: arch/x86/lib/usercopy_32.c
   :export:

.. kernel-doc:: mm/util.c
   :functions: get_user_pages_fast

The Slab Cache
==============

.. kernel-doc:: include/linux/slab.h
   :internal:

.. kernel-doc:: mm/slab.c
   :export:

.. kernel-doc:: mm/util.c
   :functions: kfree_const kvmalloc_node kvfree

More Memory Management Functions
================================

.. kernel-doc:: mm/readahead.c
   :export:

.. kernel-doc:: mm/filemap.c
   :export:

.. kernel-doc:: mm/memory.c
   :export:

.. kernel-doc:: mm/vmalloc.c
   :export:

.. kernel-doc:: mm/page_alloc.c
   :internal:

.. kernel-doc:: mm/mempool.c
   :export:

.. kernel-doc:: mm/dmapool.c
   :export:

.. kernel-doc:: mm/page-writeback.c
   :export:

.. kernel-doc:: mm/truncate.c
   :export:
