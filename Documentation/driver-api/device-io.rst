.. Copyright 2001 Matthew Wilcox
..
..     This documentation is free software; you can redistribute
..     it and/or modify it under the terms of the GNU General Public
..     License as published by the Free Software Foundation; either
..     version 2 of the License, or (at your option) any later
..     version.

===============================
Bus-Independent Device Accesses
===============================

:Author: Matthew Wilcox
:Author: Alan Cox

Introduction
============

Linux provides an API which abstracts performing IO across all busses
and devices, allowing device drivers to be written independently of bus
type.

Memory Mapped IO
================

Getting Access to the Device
----------------------------

The most widely supported form of IO is memory mapped IO. That is, a
part of the CPU's address space is interpreted not as accesses to
memory, but as accesses to a device. Some architectures define devices
to be at a fixed address, but most have some method of discovering
devices. The PCI bus walk is a good example of such a scheme. This
document does not cover how to receive such an address, but assumes you
are starting with one. Physical addresses are of type unsigned long.

This address should not be used directly. Instead, to get an address
suitable for passing to the accessor functions described below, you
should call ioremap(). An address suitable for accessing
the device will be returned to you.

After you've finished using the device (say, in your module's exit
routine), call iounmap() in order to return the address
space to the kernel. Most architectures allocate new address space each
time you call ioremap(), and they can run out unless you
call iounmap().

Accessing the device
--------------------

The part of the interface most used by drivers is reading and writing
memory-mapped registers on the device. Linux provides interfaces to read
and write 8-bit, 16-bit, 32-bit and 64-bit quantities. Due to a
historical accident, these are named byte, word, long and quad accesses.
Both read and write accesses are supported; there is no prefetch support
at this time.

The functions are named readb(), readw(), readl(), readq(),
readb_relaxed(), readw_relaxed(), readl_relaxed(), readq_relaxed(),
writeb(), writew(), writel() and writeq().

Some devices (such as framebuffers) would like to use larger transfers than
8 bytes at a time. For these devices, the memcpy_toio(),
memcpy_fromio() and memset_io() functions are
provided. Do not use memset or memcpy on IO addresses; they are not
guaranteed to copy data in order.

The read and write functions are defined to be ordered. That is the
compiler is not permitted to reorder the I/O sequence. When the ordering
can be compiler optimised, you can use __readb() and friends to
indicate the relaxed ordering. Use this with care.

While the basic functions are defined to be synchronous with respect to
each other and ordered with respect to each other the busses the devices
sit on may themselves have asynchronicity. In particular many authors
are burned by the fact that PCI bus writes are posted asynchronously. A
driver author must issue a read from the same device to ensure that
writes have occurred in the specific cases the author cares. This kind
of property cannot be hidden from driver writers in the API. In some
cases, the read used to flush the device may be expected to fail (if the
card is resetting, for example). In that case, the read should be done
from config space, which is guaranteed to soft-fail if the card doesn't
respond.

The following is an example of flushing a write to a device when the
driver would like to ensure the write's effects are visible prior to
continuing execution::

    static inline void
    qla1280_disable_intrs(struct scsi_qla_host *ha)
    {
        struct device_reg *reg;

        reg = ha->iobase;
        /* disable risc and host interrupts */
        WRT_REG_WORD(&reg->ictrl, 0);
        /*
         * The following read will ensure that the above write
         * has been received by the device before we return from this
         * function.
         */
        RD_REG_WORD(&reg->ictrl);
        ha->flags.ints_enabled = 0;
    }

PCI ordering rules also guarantee that PIO read responses arrive after any
outstanding DMA writes from that bus, since for some devices the result of
a readb() call may signal to the driver that a DMA transaction is
complete. In many cases, however, the driver may want to indicate that the
next readb() call has no relation to any previous DMA writes
performed by the device. The driver can use readb_relaxed() for
these cases, although only some platforms will honor the relaxed
semantics. Using the relaxed read functions will provide significant
performance benefits on platforms that support it. The qla2xxx driver
provides examples of how to use readX_relaxed(). In many cases, a majority
of the driver's readX() calls can safely be converted to readX_relaxed()
calls, since only a few will indicate or depend on DMA completion.

Port Space Accesses
===================

Port Space Explained
--------------------

Another form of IO commonly supported is Port Space. This is a range of
addresses separate to the normal memory address space. Access to these
addresses is generally not as fast as accesses to the memory mapped
addresses, and it also has a potentially smaller address space.

Unlike memory mapped IO, no preparation is required to access port
space.

Accessing Port Space
--------------------

Accesses to this space are provided through a set of functions which
allow 8-bit, 16-bit and 32-bit accesses; also known as byte, word and
long. These functions are inb(), inw(),
inl(), outb(), outw() and
outl().

Some variants are provided for these functions. Some devices require
that accesses to their ports are slowed down. This functionality is
provided by appending a ``_p`` to the end of the function.
There are also equivalents to memcpy. The ins() and
outs() functions copy bytes, words or longs to the given
port.

__iomem pointer tokens
======================

The data type for an MMIO address is an ``__iomem`` qualified pointer, such as
``void __iomem *reg``. On most architectures it is a regular pointer that
points to a virtual memory address and can be offset or dereferenced, but in
portable code, it must only be passed from and to functions that explicitly
operated on an ``__iomem`` token, in particular the ioremap() and
readl()/writel() functions. The 'sparse' semantic code checker can be used to
verify that this is done correctly.

While on most architectures, ioremap() creates a page table entry for an
uncached virtual address pointing to the physical MMIO address, some
architectures require special instructions for MMIO, and the ``__iomem`` pointer
just encodes the physical address or an offsettable cookie that is interpreted
by readl()/writel().

Differences between I/O access functions
========================================

readq(), readl(), readw(), readb(), writeq(), writel(), writew(), writeb()

  These are the most generic accessors, providing serialization against other
  MMIO accesses and DMA accesses as well as fixed endianness for accessing
  little-endian PCI devices and on-chip peripherals. Portable device drivers
  should generally use these for any access to ``__iomem`` pointers.

  Note that posted writes are not strictly ordered against a spinlock, see
  Documentation/driver-api/io_ordering.rst.

readq_relaxed(), readl_relaxed(), readw_relaxed(), readb_relaxed(),
writeq_relaxed(), writel_relaxed(), writew_relaxed(), writeb_relaxed()

  On architectures that require an expensive barrier for serializing against
  DMA, these "relaxed" versions of the MMIO accessors only serialize against
  each other, but contain a less expensive barrier operation. A device driver
  might use these in a particularly performance sensitive fast path, with a
  comment that explains why the usage in a specific location is safe without
  the extra barriers.

  See memory-barriers.txt for a more detailed discussion on the precise ordering
  guarantees of the non-relaxed and relaxed versions.

ioread64(), ioread32(), ioread16(), ioread8(),
iowrite64(), iowrite32(), iowrite16(), iowrite8()

  These are an alternative to the normal readl()/writel() functions, with almost
  identical behavior, but they can also operate on ``__iomem`` tokens returned
  for mapping PCI I/O space with pci_iomap() or ioport_map(). On architectures
  that require special instructions for I/O port access, this adds a small
  overhead for an indirect function call implemented in lib/iomap.c, while on
  other architectures, these are simply aliases.

ioread64be(), ioread32be(), ioread16be()
iowrite64be(), iowrite32be(), iowrite16be()

  These behave in the same way as the ioread32()/iowrite32() family, but with
  reversed byte order, for accessing devices with big-endian MMIO registers.
  Device drivers that can operate on either big-endian or little-endian
  registers may have to implement a custom wrapper function that picks one or
  the other depending on which device was found.

  Note: On some architectures, the normal readl()/writel() functions
  traditionally assume that devices are the same endianness as the CPU, while
  using a hardware byte-reverse on the PCI bus when running a big-endian kernel.
  Drivers that use readl()/writel() this way are generally not portable, but
  tend to be limited to a particular SoC.

hi_lo_readq(), lo_hi_readq(), hi_lo_readq_relaxed(), lo_hi_readq_relaxed(),
ioread64_lo_hi(), ioread64_hi_lo(), ioread64be_lo_hi(), ioread64be_hi_lo(),
hi_lo_writeq(), lo_hi_writeq(), hi_lo_writeq_relaxed(), lo_hi_writeq_relaxed(),
iowrite64_lo_hi(), iowrite64_hi_lo(), iowrite64be_lo_hi(), iowrite64be_hi_lo()

  Some device drivers have 64-bit registers that cannot be accessed atomically
  on 32-bit architectures but allow two consecutive 32-bit accesses instead.
  Since it depends on the particular device which of the two halves has to be
  accessed first, a helper is provided for each combination of 64-bit accessors
  with either low/high or high/low word ordering. A device driver must include
  either <linux/io-64-nonatomic-lo-hi.h> or <linux/io-64-nonatomic-hi-lo.h> to
  get the function definitions along with helpers that redirect the normal
  readq()/writeq() to them on architectures that do not provide 64-bit access
  natively.

__raw_readq(), __raw_readl(), __raw_readw(), __raw_readb(),
__raw_writeq(), __raw_writel(), __raw_writew(), __raw_writeb()

  These are low-level MMIO accessors without barriers or byteorder changes and
  architecture specific behavior. Accesses are usually atomic in the sense that
  a four-byte __raw_readl() does not get split into individual byte loads, but
  multiple consecutive accesses can be combined on the bus. In portable code, it
  is only safe to use these to access memory behind a device bus but not MMIO
  registers, as there are no ordering guarantees with regard to other MMIO
  accesses or even spinlocks. The byte order is generally the same as for normal
  memory, so unlike the other functions, these can be used to copy data between
  kernel memory and device memory.

inl(), inw(), inb(), outl(), outw(), outb()

  PCI I/O port resources traditionally require separate helpers as they are
  implemented using special instructions on the x86 architecture. On most other
  architectures, these are mapped to readl()/writel() style accessors
  internally, usually pointing to a fixed area in virtual memory. Instead of an
  ``__iomem`` pointer, the address is a 32-bit integer token to identify a port
  number. PCI requires I/O port access to be non-posted, meaning that an outb()
  must complete before the following code executes, while a normal writeb() may
  still be in progress. On architectures that correctly implement this, I/O port
  access is therefore ordered against spinlocks. Many non-x86 PCI host bridge
  implementations and CPU architectures however fail to implement non-posted I/O
  space on PCI, so they can end up being posted on such hardware.

  In some architectures, the I/O port number space has a 1:1 mapping to
  ``__iomem`` pointers, but this is not recommended and device drivers should
  not rely on that for portability. Similarly, an I/O port number as described
  in a PCI base address register may not correspond to the port number as seen
  by a device driver. Portable drivers need to read the port number for the
  resource provided by the kernel.

  There are no direct 64-bit I/O port accessors, but pci_iomap() in combination
  with ioread64/iowrite64 can be used instead.

inl_p(), inw_p(), inb_p(), outl_p(), outw_p(), outb_p()

  On ISA devices that require specific timing, the _p versions of the I/O
  accessors add a small delay. On architectures that do not have ISA buses,
  these are aliases to the normal inb/outb helpers.

readsq, readsl, readsw, readsb
writesq, writesl, writesw, writesb
ioread64_rep, ioread32_rep, ioread16_rep, ioread8_rep
iowrite64_rep, iowrite32_rep, iowrite16_rep, iowrite8_rep
insl, insw, insb, outsl, outsw, outsb

  These are helpers that access the same address multiple times, usually to copy
  data between kernel memory byte stream and a FIFO buffer. Unlike the normal
  MMIO accessors, these do not perform a byteswap on big-endian kernels, so the
  first byte in the FIFO register corresponds to the first byte in the memory
  buffer regardless of the architecture.

Public Functions Provided
=========================

.. kernel-doc:: arch/x86/include/asm/io.h
   :internal:

.. kernel-doc:: lib/pci_iomap.c
   :export:
