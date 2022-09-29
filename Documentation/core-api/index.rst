======================
Core API Documentation
======================

This is the beginning of a manual for core kernel APIs.  The conversion
(and writing!) of documents for this manual is much appreciated!

Core utilities
==============

This section has general and "core core" documentation.  The first is a
massive grab-bag of kerneldoc info left over from the docbook days; it
should really be broken up someday when somebody finds the energy to do
it.

.. toctree::
   :maxdepth: 1

   kernel-api
   workqueue
   watch_queue
   printk-basics
   printk-formats
   printk-index
   symbol-namespaces
   asm-annotations

Data structures and low-level utilities
=======================================

Library functionality that is used throughout the kernel.

.. toctree::
   :maxdepth: 1

   kobject
   kref
   assoc_array
   xarray
   idr
   circular-buffers
   rbtree
   generic-radix-tree
   packing
   this_cpu_ops
   timekeeping
   errseq
   wrappers/atomic_t
   wrappers/atomic_bitops

Low level entry and exit
========================

.. toctree::
   :maxdepth: 1

   entry

Concurrency primitives
======================

How Linux keeps everything from happening at the same time.  See
Documentation/locking/index.rst for more related documentation.

.. toctree::
   :maxdepth: 1

   refcount-vs-atomic
   irq/index
   local_ops
   padata
   ../RCU/index
   wrappers/memory-barriers.rst

Low-level hardware management
=============================

Cache management, managing CPU hotplug, etc.

.. toctree::
   :maxdepth: 1

   cachetlb
   cpu_hotplug
   memory-hotplug
   genericirq
   protection-keys

Memory management
=================

How to allocate and use memory in the kernel.  Note that there is a lot
more memory-management documentation in Documentation/mm/index.rst.

.. toctree::
   :maxdepth: 1

   memory-allocation
   unaligned-memory-access
   dma-api
   dma-api-howto
   dma-attributes
   dma-isa-lpc
   mm-api
   genalloc
   pin_user_pages
   boot-time-mm
   gfp_mask-from-fs-io

Interfaces for kernel debugging
===============================

.. toctree::
   :maxdepth: 1

   debug-objects
   tracepoint
   debugging-via-ohci1394

Everything else
===============

Documents that don't fit elsewhere or which have yet to be categorized.

.. toctree::
   :maxdepth: 1

   librs

.. only:: subproject and html

   Indices
   =======

   * :ref:`genindex`
