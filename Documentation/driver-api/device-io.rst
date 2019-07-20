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
should call :c:func:`ioremap()`. An address suitable for accessing
the device will be returned to you.

After you've finished using the device (say, in your module's exit
routine), call :c:func:`iounmap()` in order to return the address
space to the kernel. Most architectures allocate new address space each
time you call :c:func:`ioremap()`, and they can run out unless you
call :c:func:`iounmap()`.

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
8 bytes at a time. For these devices, the :c:func:`memcpy_toio()`,
:c:func:`memcpy_fromio()` and :c:func:`memset_io()` functions are
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
long. These functions are :c:func:`inb()`, :c:func:`inw()`,
:c:func:`inl()`, :c:func:`outb()`, :c:func:`outw()` and
:c:func:`outl()`.

Some variants are provided for these functions. Some devices require
that accesses to their ports are slowed down. This functionality is
provided by appending a ``_p`` to the end of the function.
There are also equivalents to memcpy. The :c:func:`ins()` and
:c:func:`outs()` functions copy bytes, words or longs to the given
port.

Public Functions Provided
=========================

.. kernel-doc:: arch/x86/include/asm/io.h
   :internal:

.. kernel-doc:: lib/pci_iomap.c
   :export:
