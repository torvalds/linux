=======================
SuperH Interfaces Guide
=======================

:Author: Paul Mundt

Memory Management
=================

SH-4
----

Store Queue API
~~~~~~~~~~~~~~~

.. kernel-doc:: arch/sh/kernel/cpu/sh4/sq.c
   :export:

SH-5
----

TLB Interfaces
~~~~~~~~~~~~~~

.. kernel-doc:: arch/sh/mm/tlb-sh5.c
   :internal:

.. kernel-doc:: arch/sh/include/asm/tlb_64.h
   :internal:

Machine Specific Interfaces
===========================

mach-dreamcast
--------------

.. kernel-doc:: arch/sh/boards/mach-dreamcast/rtc.c
   :internal:

mach-x3proto
------------

.. kernel-doc:: arch/sh/boards/mach-x3proto/ilsel.c
   :export:

Busses
======

SuperHyway
----------

.. kernel-doc:: drivers/sh/superhyway/superhyway.c
   :export:

Maple
-----

.. kernel-doc:: drivers/sh/maple/maple.c
   :export:
