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

