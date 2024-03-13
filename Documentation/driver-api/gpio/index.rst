===================================
General Purpose Input/Output (GPIO)
===================================

Contents:

.. toctree::
   :maxdepth: 2

   intro
   using-gpio
   driver
   consumer
   board
   drivers-on-gpio
   legacy
   bt8xxgpio

Core
====

.. kernel-doc:: include/linux/gpio/driver.h
   :internal:

.. kernel-doc:: drivers/gpio/gpiolib.c
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

.. kernel-doc:: drivers/gpio/gpiolib-devres.c
   :export:

sysfs helpers
=============

.. kernel-doc:: drivers/gpio/gpiolib-sysfs.c
   :export:
