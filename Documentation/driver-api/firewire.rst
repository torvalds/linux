===========================================
Firewire (IEEE 1394) driver Interface Guide
===========================================

Introduction and Overview
=========================

TBD

Firewire char device data structures
====================================

.. kernel-doc:: include/uapi/linux/firewire-cdev.h
    :internal:

Firewire device probing and sysfs interfaces
============================================

.. kernel-doc:: drivers/firewire/core-device.c
    :export:

Firewire core transaction interfaces
====================================

.. kernel-doc:: drivers/firewire/core-transaction.c
    :export:

Firewire Isochronous I/O interfaces
===================================

.. kernel-doc:: drivers/firewire/core-iso.c
   :export:

