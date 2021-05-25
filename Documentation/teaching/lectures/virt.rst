==============
Virtualization
==============

`View slides <virt-slides.html>`_

.. slideconf::
   :autoslides: False
   :theme: single-level

Lecture objectives:
===================

.. slide:: Virtualization
   :inline-contents: True
   :level: 2

   * Emulation basics

   * Virtualization basics

   * Paravirtualization basics

   * Hardware support for virtualization

   * Overview of the Xen hypervisor

   * Overview of the KVM hypervisor


Emulation basics
================

.. slide:: Emulation basics
   :inline-contents: True
   :level: 2

   * Instructions are emulated (each time they are executed)

   * The other system components are also emulated:

     * MMU

     * Physical memory access

     * Peripherals

   * Target architecture - the architecture that it is emulated

   * Host architecture - the architecture that the emulator runs on

   * For emulation target and host architectures can be different


Virtualization basics
=====================

.. slide:: Virtualization basics
   :inline-contents: True
   :level: 2

   * Defined in a paper by Popek & Goldberg in 1974

   * Fidelity

   * Performance

   * Security

   .. ditaa::

      +----+  +----+     +----+
      | VM |  | VM | ... | VM |
      +----+  +----+     +----+

      +-------------------------+
      | Virtual Machine Monitor |
      +-------------------------+

      +-------------------------+
      |         Hardware        |
      +-------------------------+


Classic virtualization
======================

.. slide:: Classic virtualization
   :inline-contents: True
   :level: 2

   * Trap & Emulate

   * Same architecture for host and target

   * Most of the target instructions are natively executed

   * Target OS runs in non-privilege mode on the host

   * Privileged instructions are trapped and emulated

   * Two machine states: host and guest


Software virtualization
=======================

.. slide:: Software virtualization
   :inline-contents: True
   :level: 2

   * Not all architecture can be virtualized; e.g. x86:

     * CS register encodes the CPL

     * Some instructions don't generate a trap (e.g. popf)

   * Solution: emulate instructions using binary translation


MMU virtualization
==================

.. slide:: MMU virtualization
   :inline-contents: True
   :level: 2

   * "Fake" VM physical addresses are translated by the host to actual
     physical addresses

   * Guest virtual address -> Guest physical address -> Host Physical Address

   * The guest page tables are not directly used by the host hardware

   * VM page tables are verified then translated into a new set of page
     tables on the host (shadow page tables)


Shadow page tables
------------------

.. slide:: Shadow page tables
   :inline-contents: True
   :level: 2

   |_|

   .. ditaa::

                          PGD                     PMD                   PT
                      +----------+            +----------+         +----------+
                      |          |            |          |         |          |      Guest Physical Page
                      +----------+            +----------+         +----------+         +----------+
                      |          |            |          |         |          |----+    |          |
      +-----+         +----------+            +----------+         +----------+    |    |          |
      | CR3 |         |          |----+       |          |---+     |          |    |    |          |
      +-----+         +----------+    |       +----------+   |     +----------+    +--->+----------+
         |            |          |    |       |          |   |     |          |
         +--------->  +----------+    +------>+----------+   +---->+----------+
                      Write Protected         Write Protected      Write Protected
                           |
                           |
      Guest (VM)           |
                           | trap access
                           |
      ---------------------+------------------------------------------------------------------------------
                           |
                           | check access, transform GPP to HPP
                           |
                           v

                       Shadow PGD              Shadow PMD            Shadow PT
                      +----------+            +----------+         +----------+
                      |          |            |          |         |          |      Host Physical Page
                      +----------+            +----------+         +----------+         +----------+
                      |          |            |          |         |          |----+    |          |
                      +----------+            +----------+         +----------+    |    |          |
                      |          |----+       |          |---+     |          |    |    |          |
                      +----------+    |       +----------+   |     +----------+    +--->+----------+
                      |          |    |       |          |   |     |          |
                      +----------+    +------>+----------+   +---->+----------+



Lazy shadow sync
----------------

.. slide:: Lazy shadow sync
   :inline-contents: True
   :level: 2

   * Guest page tables changes are typically batched

   * To avoid repeated traps, checks and transformations map guest
     page table entries with write access

   * Update the shadow page table when

     * The TLB is flushed

     * In the host page fault handler


I/O emulation
=============

.. slide:: I/O emulation
   :inline-contents: True
   :level: 2

   |_|

   .. ditaa::

      +---------------------+
      |     Guest OS        |
      |  +---------------+  |
      |  | Guest Driver  |  |
      |  +---------------+  |
      |    |           ^    |
      |    |           |    |
      +----+-----------+----+
           | trap      |
           | access    |
       +---+-----------+----+
       |   |   VMM     |    |
       |   v           |    |
       | +----------------+ |
       | | Virtual Device | |
       | +----------------+ |
       |  |            ^    |
       |  |            |    |
       +--+------------+----+
          |            |
          v            |
        +-----------------+
        | Physical Device |
        +-----------------+


.. slide:: Example: qemu SiFive UART emulation
   :inline-contents: True
   :level: 2

   .. literalinclude:: ../res/sifive_uart.c
      :language: c


Paravirtualization
==================

.. slide:: Paravirtualization
   :inline-contents: True
   :level: 2

   * Change the guest OS so that it cooperates with the VMM

     * CPU paravirtualization

     * MMU paravirtualization

     * I/O paravirtualization

   * VMM exposes hypercalls for:

     * activate / deactivate the interrupts

     * changing page tables

     * accessing virtualized peripherals

   * VMM uses events to trigger interrupts in the VM


Intel VT-x
==========

.. slide:: Intel VT-x
   :inline-contents: True
   :level: 2


   * Hardware extension to transform x86 to the point it can be
     virtualized "classically"

   * New execution mode: non-root mode

   * Each non-root mode instance uses a Virtual Machine Control
     Structure (VMCS) to store its state

   * VMM runs in root mode

   * VM-entry and VM-exit are used to transition between the two modes


Virtual Machine Control Structure
---------------------------------

.. slide:: Virtual Machine Control Structure
   :inline-contents: True
   :level: 2

   * Guest information: state of the virtual CPU

   * Host information: state of the physical CPU

   * Saved information:

     * visible state: segment registers, CR3, IDTR, etc.

     * internal state

   * VMCS can not be accessed directly but certain information can be
     accessed with special instructions

VM entry & exit
---------------

.. slide:: VM entry & exit
   :inline-contents: True
   :level: 2

   * VM entry - new instructions that switches the CPU in non-root
     mode and loads the VM state from a VMCS; host state is saved in
     VMCS

   * Allows injecting interrupts and exceptions in the guest

   * VM exit will be automatically triggered based on the VMCS
     configuration

   * When VM exit occurs host state is loaded from VMCS, guest state
     is saved in VMCS

VM execution control fields
---------------------------

.. slide:: VM execution control fields
   :inline-contents: True
   :level: 2

   * Selects conditions which triggers a VM exit; examples:

     * If an external interrupt is generated

     * If an external interrupt is generated and EFLAGS.IF is set

     * If CR0-CR4 registers are modified

   * Exception bitmap - selects which exceptions will generate a VM
     exit

   * IO bitmap - selects which I/O addresses (IN/OUT accesses)
     generates a VM exit

   * MSR bitmaps - selects which RDMSR or WRMSR instructions will
     generate a VM exit


Extend Page Tables
==================

.. slide:: Extend Page Tables
   :inline-contents: True
   :level: 2

   * Reduces the complexity of MMU virtualization and improves
     performance

   * Access to CR3, INVLPG and page faults do not require VM exit
     anymore

   * The EPT page table is controlled by the VMM

   .. ditaa::

      +-----+                            +-----+
      | CR3 |                            | EPT |
      +-----+                            +-----+
         |          +------------------+     |         +----------------+
         |          |                  |     |         |                |
         +--------> | Guest Page Table |     +-------> | EPT Page Table | --------------->
                    |                  |               |                |
      ------------> +------------------+ ------------> +----------------+

      Guest Virtual                     Guest Physical                      Host Physical
        Address                             Address                           Address


VPID
----

.. slide:: VPID
   :inline-contents: True
   :level: 2

   * VM entry and VM exit forces a TLB flush - loses VMM / VM translations

   * To avoid this issue a VPID (Virtual Processor ID) tag is
     associated with each VM (VPID 0 is reserved for the VMM)

   * All TLB entries are tagged

   * At VM entry and exit just the entries associated with the tags
     are flushed

   * When searching the TLB just the current VPID is used


I/O virtualization
==================

   * Direct access to hardware from a VM - in a controlled fashion

     * Map the MMIO host directly to the guest

     * Forward interrupts

.. slide:: I/O virtualization
   :inline-contents: True
   :level: 2

   .. ditaa::

      +---------------------+     +---------------------+
      |     Guest OS        |	  |     Guest OS        |
      |  +---------------+  |	  |  +---------------+  |
      |  | Guest Driver  |  |	  |  | Guest Driver  |  |
      |  +---------------+  |	  |  +---------------+  |
      |    |           ^    |	  |    |           ^    |
      |    |           |    |	  |    |           |    |
      +----+-----------+----+	  +----+-----------+----+
           | traped    | 	       | mapped    |
           | access    |	       | access    |
       +---+-----------+----+	   +---+-----------+-----+     But how do we deal with DMA?
       |   |   VMM     |    |	   |   |   VMM     |     |
       |   v           |    |	   |   |           |     |
       | +----------------+ |	   |   |     +---------+ |
       | | Virtual Device | |	   |   |     | IRQ     | |
       | +----------------+ |	   |   |     | Mapping | |
       |  |            ^    |	   |   |     +---------+ |
       |  |            |    |	   |   |           |     |
       +--+------------+----+	   +---+-----------+-----+
          |            |	       |           |
          v            |	       v           |
        +-----------------+	    +-----------------+
        | Physical Device |	    | Physical Device |
        +-----------------+    	    +-----------------+

Instead of trapping MMIO as with emulated devices we can allow the
guest to access the MMIO directly by mapping through its page tables.

Interrupts from the device are handled by the host kernel and a signal
is send to the VMM which injects the interrupt to the guest just as
for the emulated devices.


.. slide:: I/O MMU
   :inline-contents: True
   :level: 2

   VT-d protects and translates VM physical addresses using an I/O
   MMU (DMA remaping)

   .. ditaa::

	 +------+                           +------+
	 |      |			    |      |
	 | CPU  |			    | DMA  |
	 |      |			    |      |
	 +------+			    +------+
                                               |
                                               |
                                               v
	 +-----+                            +-----+
	 | CR3 |                            | EPT |
	 +-----+                            +-----+
           |          +------------------+     |         +----------------+
           |          |                  |     |         |                |
           +--------> | Guest Page Table |     +-------> | EPT Page Table | --------------->
                      |                  |               |                |
        ------------> +------------------+ ------------> +----------------+

        Guest Virtual                     Guest Physical                      Host Physical
          Address                             Address                           Address


.. slide:: Interrupt posting
   :inline-contents: True
   :level: 2

   * Messsage Signaled Interrupts (MSI) = DMA writes to the host
     address range of the IRQ controller (e.g. 0xFEExxxxx)

   * Low bits of the address and the data indicate which interrupt
     vector to deliver to which CPU

   * Interrupt remapping table points to the virtual CPU (VMCS) that
     should receive the interrupt

   * I/O MMU will trap the IRQ controller write and look it up in the
     interrupt remmaping table

     * if that virtual CPU is currently running it will take the
       interrupt directly

     * otherwise a bit is set in a table (Posted Interrupt Descriptor
       table) and the interrupt will be inject next time that vCPU is
       run


.. slide:: I/O virtualization
   :inline-contents: True
   :level: 2

   .. ditaa::

      +---------------------+     +---------------------+    +---------------------+
      |     Guest OS        |	  |     Guest OS        |    |     Guest OS        |
      |  +---------------+  |	  |  +---------------+  |    |  +---------------+  |
      |  | Guest Driver  |  |	  |  | Guest Driver  |  |    |  | Guest Driver  |  |
      |  +---------------+  |	  |  +---------------+  |    |  +---------------+  |
      |    |           ^    |	  |    |           ^    |    |    |           ^    |
      |    |           |    |	  |    |           |    |    |    |           |    |
      +----+-----------+----+	  +----+-----------+----+    +----+-----------+----+
           | traped    | 	       | mapped    |	          | mapped    | interrupt
           | access    |	       | access    |	          | access    | posting
       +---+-----------+----+	   +---+-----------+-----+    +---+-----------+-----+
       |   |   VMM     |    |	   |   |   VMM     |     |    |   |   VMM     |     |
       |   v           |    |	   |   |           |     |    |   |           |     |
       | +----------------+ |	   |   |     +---------+ |    |   |           |     |
       | | Virtual Device | |	   |   |     | IRQ     | |    |   |           |     |
       | +----------------+ |	   |   |     | Mapping | |    |   |           |     |
       |  |            ^    |	   |   |     +---------+ |    |   |           |     |
       |  |            |    |	   |   |           |     |    |   |           |     |
       +--+------------+----+	   +---+-----------+-----+    +---+-----------+-----+
          |            |	       |           |	          |           |
          v            |	       v           |	          v           |
        +-----------------+	    +-----------------+	       +-----------------+
        | Physical Device |	    | Physical Device |	       | Physical Device |
        +-----------------+    	    +-----------------+        +-----------------+



.. slide:: SR-IOV
   :inline-contents: True
   :level: 2

   * Single Root - Input Output Virtualization

   * Physical device with multiple Ethernet ports will be shown as
     multiple device on the PCI bus

   * Physical Function is used for the control and can be configured

     * to present itself as a new PCI device

     * which VLAN to use

   * The new virtual function is enumerated on the bus and can be
     assigned to a particular guest


qemu
====

.. slide:: qemu
   :inline-contents: True
   :level: 2

   * Uses binary translation via Tiny Code Generator (TCG) for
     efficient emulation

   * Supports different target and host architectures (e.g. running
     ARM VMs on x86)

   * Both process and full system level emulation

   * MMU emulation

   * I/O emulation

   * Can be used with KVM for accelerated virtualization

KVM
===

.. slide:: KVM
   :inline-contents: True
   :level: 2

   .. ditaa::

             VM1 (qemu)                     VM2 (qemu)
      +---------------------+        +---------------------+
      | +------+   +------+ |        | +------+   +------+ |
      | | App1 |   | App2 | |        | | App1 |   | App2 | |
      | +------+   +------+ |        | +------+   +------+ |
      | +-----------------+ |        | +-----------------+ |
      | |  Guest Kernel   | |        | |  Guest Kernel   | |
      | +-----------------+ |        | +-----------------+ |
      +---------------------+        +---------------------+

      +----------------------------------------------------+
      | +-----+                                            |
      | | KVM |      Host Linux Kernel                     |
      | +-----+                                            |
      +----------------------------------------------------+

      +----------------------------------------------------+
      |        Hardware with virtualization support        |
      +----------------------------------------------------+


.. slide:: KVM
   :inline-contents: True
   :level: 2

   * Linux device driver for hardware virtualization (e.g. Intel VT-x, SVM)

   * IOCTL based interface for managing and running virtual CPUs

   * VMM components implemented inside the Linux kernel
     (e.g. interrupt controller, timers)

   * Shadow page tables or EPT if present

   * Uses qemu or virtio for I/O virtualization



Type 1 vs Type 2 Hypervisors
============================

.. slide:: Xen
   :inline-contents: True
   :level: 2

   * Type 1 = Bare Metal Hypervisor

   * Type 2 = Hypervisor embedded in an exist kernel / OS


Xen
===

.. slide:: Xen
   :inline-contents: True
   :level: 2

   .. image::  ../res/xen-overview.png
