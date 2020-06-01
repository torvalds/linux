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
   printk-formats
   symbol-namespaces

Data structures and low-level utilities
=======================================

Library functionality that is used throughout the kernel.

.. toctree::
   :maxdepth: 1

   kobject
   assoc_array
   xarray
   idr
   circular-buffers
   generic-radix-tree
   packing
   timekeeping
   errseq

Concurrency primitives
======================

How Linux keeps everything from happening at the same time.  See
:doc:`/locking/index` for more related documentation.

.. toctree::
   :maxdepth: 1

   atomic_ops
   refcount-vs-atomic
   local_ops
   padata
   ../RCU/index

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
more memory-management documentation in :doc:`/vm/index`.

.. toctree::
   :maxdepth: 1

   memory-allocation
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
