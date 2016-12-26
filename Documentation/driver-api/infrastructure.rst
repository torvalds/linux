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

