===================================
General Purpose Input/Output (GPIO)
===================================

Core
====

.. kernel-doc:: include/linux/gpio/driver.h
   :internal:

.. kernel-doc:: drivers/gpio/gpiolib.c
   :export:

Legacy API
==========

The functions listed in this section are deprecated. The GPIO descriptor based
API described above should be used in new code.

.. kernel-doc:: drivers/gpio/gpiolib-legacy.c
   :export:

ACPI support
============

.. kernel-doc:: drivers/gpio/gpiolib-acpi.c
   :export:

Device tree support
===================

.. kernel-doc:: drivers/gpio/gpiolib-of.c
   :export:

Device-managed API
==================

.. kernel-doc:: drivers/gpio/devres.c
   :export:

sysfs helpers
=============

.. kernel-doc:: drivers/gpio/gpiolib-sysfs.c
   :export:
