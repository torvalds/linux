.. SPDX-License-Identifier: GPL-2.0-only

.. _auxiliary_bus:

=============
Auxiliary Bus
=============

.. kernel-doc:: drivers/base/auxiliary.c
   :doc: PURPOSE

When Should the Auxiliary Bus Be Used
=====================================

.. kernel-doc:: drivers/base/auxiliary.c
   :doc: USAGE


Auxiliary Device Creation
=========================

.. kernel-doc:: include/linux/auxiliary_bus.h
   :identifiers: auxiliary_device

.. kernel-doc:: drivers/base/auxiliary.c
   :identifiers: auxiliary_device_init __auxiliary_device_add
                 auxiliary_find_device

Auxiliary Device Memory Model and Lifespan
------------------------------------------

.. kernel-doc:: include/linux/auxiliary_bus.h
   :doc: DEVICE_LIFESPAN


Auxiliary Drivers
=================

.. kernel-doc:: include/linux/auxiliary_bus.h
   :identifiers: auxiliary_driver module_auxiliary_driver

.. kernel-doc:: drivers/base/auxiliary.c
   :identifiers: __auxiliary_driver_register auxiliary_driver_unregister

Example Usage
=============

.. kernel-doc:: drivers/base/auxiliary.c
   :doc: EXAMPLE

