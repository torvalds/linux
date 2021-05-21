==================
Architecture Layer
==================

`View slides <arch-slides.html>`_

.. slideconf::
   :autoslides: False
   :theme: single-level

Lecture objectives:
===================

.. slide:: Introduction
   :inline-contents: True
   :level: 2

   * Overview of the arch layer

   * Overview of the boot process


Overview of the arch layer
==========================

.. slide:: Overview of the arch layer
   :level: 2
   :inline-contents: True

   .. ditaa::
      :height: 100%

      +---------------+  +--------------+      +---------------+
      | Application 1 |  | Application2 | ...  | Application n |
      +---------------+  +--------------+      +---------------+
              |                 |                      |
              v                 v                      v
      +--------------------------------+------------------------+
      |   Kernel core & subsystems     |    Generic Drivers     |
      +--------------------------------+------------------------+
      |             Generic Architecture Code                   |
      +---------------------------------------------------------+
      |              Architecture Specific Code                 |
      |                                                         |
      | +-----------+  +--------+  +---------+  +--------+      |
      | | Bootstrap |  | Memory |  | Threads |  | Timers |      |
      | +-----------+  +--------+  +---------+  +--------+      |
      | +------+ +----------+ +------------------+              |
      | | IRQs | | Syscalls | | Platform Drivers |              |
      | +------+ +----------+ +------------------+              |
      | +------------------+  +---------+     +---------+       |
      |	| Platform Drivers |  | machine | ... | machine |       |
      | +------------------+  +---------+     +---------+       |
      +---------------------------------------------------------+
              |                 |                      |
              v                 v                      v
      +--------------------------------------------------------+
      |                         Hardware                       |
      +--------------------------------------------------------+


Boot strap
----------

.. slide:: Bootstrap
   :level: 2
   :inline-contents: True

   * The first kernel code that runs

   * Typically runs with the MMU disabled

   * Move / Relocate kernel code


Boot strap
----------

.. slide:: Bootstrap
   :level: 2
   :inline-contents: True

   * The first kernel code that runs

   * Typically runs with the MMU disabled

   * Copy bootloader arguments and determine kernel run location

   * Move / relocate kernel code to final location

   * Initial MMU setup - map the kernel



Memory setup
------------

.. slide:: Memory Setup
   :level: 2
   :inline-contents: True

   * Determine available memory and setup the boot memory allocator

   * Manages memory regions before the page allocator is setup

   * Bootmem - used a bitmap to track free blocks

   * Memblock - deprecates bootmem and adds support for memory ranges

     * Supports both physical and virtual addresses

     * support NUMA architectures


MMU management
--------------

.. slide:: MMU management
   :level: 2
   :inline-contents: True

   * Implements the generic page table manipulation APIs: types,
     accessors, flags

   * Implement TLB management APIs: flush, invalidate


Thread Management
-----------------

.. slide:: Thread Management
   :level: 2
   :inline-contents: True

   * Defines the thread type (struct thread_info) and implements
     functions for allocating threads (if needed)

   * Implement :c:func:`copy_thread` and :c:func:`switch_context`


Time Management
----------------

.. slide:: Timer Management
   :level: 2
   :inline-contents: True

   * Setup the timer tick and provide a time source

   * Mostly transitioned to platform drivers

     * clock_event_device - for scheduling timers

     * clocksource - for reading the time


IRQs and exception management
-----------------------------

.. slide:: IRQs and exception management
   :level: 2
   :inline-contents: True

   * Define interrupt and exception handlers / entry points

   * Setup priorities

   * Platform drivers for interrupt controllers


System calls
------------

.. slide:: System calls
   :level: 2
   :inline-contents: True

   * Define system call entry point(s)

   * Implement user-space access primitives (e.g. copy_to_user)


Platform Drivers
----------------

.. slide:: Platform Drivers
   :level: 2
   :inline-contents: True

   * Platform and architecture specific drivers

   * Bindings to platform device enumeration methods (e.g. device tree
     or ACPI)

Machine specific code
---------------------

.. slide:: Machine specific code
   :level: 2
   :inline-contents: True

   * Some architectures use a "machine" / "platform" abstraction

   * Typical for architecture used in embedded systems with a lot of
     variety (e.g. ARM, powerPC)


Overview of the boot process
============================


.. slide:: Boot flow inspection
   :level: 2
   :inline-contents: True


   .. asciicast:: ../res/boot.cast
