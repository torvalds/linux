===========================
Boot time memory management
===========================

Early system initialization cannot use "normal" memory management
simply because it is not set up yet. But there is still need to
allocate memory for various data structures, for instance for the
physical page allocator.

A specialized allocator called ``memblock`` performs the
boot time memory management. The architecture specific initialization
must set it up in :c:func:`setup_arch` and tear it down in
:c:func:`mem_init` functions.

Once the early memory management is available it offers a variety of
functions and macros for memory allocations. The allocation request
may be directed to the first (and probably the only) node or to a
particular node in a NUMA system. There are API variants that panic
when an allocation fails and those that don't.

Memblock also offers a variety of APIs that control its own behaviour.

Memblock Overview
=================

.. kernel-doc:: mm/memblock.c
   :doc: memblock overview


Functions and structures
========================

Here is the description of memblock data structures, functions and
macros. Some of them are actually internal, but since they are
documented it would be silly to omit them. Besides, reading the
descriptions for the internal functions can help to understand what
really happens under the hood.

.. kernel-doc:: include/linux/memblock.h
.. kernel-doc:: mm/memblock.c
   :functions:
