Buffer Sharing and Synchronization
==================================

The dma-buf subsystem provides the framework for sharing buffers for
hardware (DMA) access across multiple device drivers and subsystems, and
for synchronizing asynchronous hardware access.

This is used, for example, by drm "prime" multi-GPU support, but is of
course not limited to GPU use cases.

The three main components of this are: (1) dma-buf, representing a
sg_table and exposed to userspace as a file descriptor to allow passing
between devices, (2) fence, which provides a mechanism to signal when
one device as finished access, and (3) reservation, which manages the
shared or exclusive fence(s) associated with the buffer.

Shared DMA Buffers
------------------

This document serves as a guide to device-driver writers on what is the dma-buf
buffer sharing API, how to use it for exporting and using shared buffers.

Any device driver which wishes to be a part of DMA buffer sharing, can do so as
either the 'exporter' of buffers, or the 'user' or 'importer' of buffers.

Say a driver A wants to use buffers created by driver B, then we call B as the
exporter, and A as buffer-user/importer.

The exporter

 - implements and manages operations in :c:type:`struct dma_buf_ops
   <dma_buf_ops>` for the buffer,
 - allows other users to share the buffer by using dma_buf sharing APIs,
 - manages the details of buffer allocation, wrapped int a :c:type:`struct
   dma_buf <dma_buf>`,
 - decides about the actual backing storage where this allocation happens,
 - and takes care of any migration of scatterlist - for all (shared) users of
   this buffer.

The buffer-user

 - is one of (many) sharing users of the buffer.
 - doesn't need to worry about how the buffer is allocated, or where.
 - and needs a mechanism to get access to the scatterlist that makes up this
   buffer in memory, mapped into its own address space, so it can access the
   same area of memory. This interface is provided by :c:type:`struct
   dma_buf_attachment <dma_buf_attachment>`.

Basic Operation and Device DMA Access
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-buf.c
   :doc: dma buf device access

CPU Access to DMA Buffer Objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-buf.c
   :doc: cpu access

Kernel Functions and Structures Reference
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-buf.c
   :export:

.. kernel-doc:: include/linux/dma-buf.h
   :internal:

Reservation Objects
-------------------

.. kernel-doc:: drivers/dma-buf/reservation.c
   :doc: Reservation Object Overview

.. kernel-doc:: drivers/dma-buf/reservation.c
   :export:

.. kernel-doc:: include/linux/reservation.h
   :internal:

DMA Fences
----------

.. kernel-doc:: drivers/dma-buf/dma-fence.c
   :export:

.. kernel-doc:: include/linux/dma-fence.h
   :internal:

Seqno Hardware Fences
~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/seqno-fence.c
   :export:

.. kernel-doc:: include/linux/seqno-fence.h
   :internal:

DMA Fence Array
~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-fence-array.c
   :export:

.. kernel-doc:: include/linux/dma-fence-array.h
   :internal:

DMA Fence uABI/Sync File
~~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/sync_file.c
   :export:

.. kernel-doc:: include/linux/sync_file.h
   :internal:

