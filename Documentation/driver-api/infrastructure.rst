Device drivers infrastructure
=============================

The Basic Device Driver-Model Structures
----------------------------------------

.. kernel-doc:: include/linux/device.h
   :internal:

Device Drivers Base
-------------------

.. kernel-doc:: drivers/base/init.c
   :internal:

.. kernel-doc:: drivers/base/driver.c
   :export:

.. kernel-doc:: drivers/base/core.c
   :export:

.. kernel-doc:: drivers/base/syscore.c
   :export:

.. kernel-doc:: drivers/base/class.c
   :export:

.. kernel-doc:: drivers/base/node.c
   :internal:

.. kernel-doc:: drivers/base/firmware_class.c
   :export:

.. kernel-doc:: drivers/base/transport_class.c
   :export:

.. kernel-doc:: drivers/base/dd.c
   :export:

.. kernel-doc:: include/linux/platform_device.h
   :internal:

.. kernel-doc:: drivers/base/platform.c
   :export:

.. kernel-doc:: drivers/base/bus.c
   :export:

Buffer Sharing and Synchronization
----------------------------------

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

dma-buf
~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-buf.c
   :export:

.. kernel-doc:: include/linux/dma-buf.h
   :internal:

reservation
~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/reservation.c
   :doc: Reservation Object Overview

.. kernel-doc:: drivers/dma-buf/reservation.c
   :export:

.. kernel-doc:: include/linux/reservation.h
   :internal:

fence
~~~~~

.. kernel-doc:: drivers/dma-buf/dma-fence.c
   :export:

.. kernel-doc:: include/linux/dma-fence.h
   :internal:

.. kernel-doc:: drivers/dma-buf/seqno-fence.c
   :export:

.. kernel-doc:: include/linux/seqno-fence.h
   :internal:

.. kernel-doc:: drivers/dma-buf/dma-fence-array.c
   :export:

.. kernel-doc:: include/linux/dma-fence-array.h
   :internal:

.. kernel-doc:: drivers/dma-buf/reservation.c
   :export:

.. kernel-doc:: include/linux/reservation.h
   :internal:

.. kernel-doc:: drivers/dma-buf/sync_file.c
   :export:

.. kernel-doc:: include/linux/sync_file.h
   :internal:

Device Drivers DMA Management
-----------------------------

.. kernel-doc:: drivers/base/dma-coherent.c
   :export:

.. kernel-doc:: drivers/base/dma-mapping.c
   :export:

Device Drivers Power Management
-------------------------------

.. kernel-doc:: drivers/base/power/main.c
   :export:

Device Drivers ACPI Support
---------------------------

.. kernel-doc:: drivers/acpi/scan.c
   :export:

.. kernel-doc:: drivers/acpi/scan.c
   :internal:

Device drivers PnP support
--------------------------

.. kernel-doc:: drivers/pnp/core.c
   :internal:

.. kernel-doc:: drivers/pnp/card.c
   :export:

.. kernel-doc:: drivers/pnp/driver.c
   :internal:

.. kernel-doc:: drivers/pnp/manager.c
   :export:

.. kernel-doc:: drivers/pnp/support.c
   :export:

Userspace IO devices
--------------------

.. kernel-doc:: drivers/uio/uio.c
   :export:

.. kernel-doc:: include/linux/uio_driver.h
   :internal:

