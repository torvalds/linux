.. SPDX-License-Identifier: GPL-2.0
.. _devicetree:

======================================
DeviceTree Kernel API
======================================

Core functions
--------------

.. kernel-doc:: drivers/of/base.c
   :export:

.. kernel-doc:: include/linux/of.h
   :internal:

.. kernel-doc:: drivers/of/property.c
   :export:

.. kernel-doc:: include/linux/of_graph.h
   :internal:

.. kernel-doc:: drivers/of/address.c
   :export:

.. kernel-doc:: drivers/of/irq.c
   :export:

.. kernel-doc:: drivers/of/fdt.c
   :export:

Driver model functions
----------------------

.. kernel-doc:: include/linux/of_device.h
   :internal:

.. kernel-doc:: drivers/of/device.c
   :export:

.. kernel-doc:: include/linux/of_platform.h
   :internal:

.. kernel-doc:: drivers/of/platform.c
   :export:

Overlay and Dynamic DT functions
--------------------------------

.. kernel-doc:: drivers/of/resolver.c
   :export:

.. kernel-doc:: drivers/of/dynamic.c
   :export:

.. kernel-doc:: drivers/of/overlay.c
   :export:
