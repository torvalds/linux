.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===================================
Compute Express Link Memory Devices
===================================

A Compute Express Link Memory Device is a CXL component that implements the
CXL.mem protocol. It contains some amount of volatile memory, persistent memory,
or both. It is enumerated as a PCI device for configuration and passing
messages over an MMIO mailbox. Its contribution to the System Physical
Address space is handled via HDM (Host Managed Device Memory) decoders
that optionally define a device's contribution to an interleaved address
range across multiple devices underneath a host-bridge or interleaved
across host-bridges.

Driver Infrastructure
=====================

This section covers the driver infrastructure for a CXL memory device.

CXL Memory Device
-----------------

.. kernel-doc:: drivers/cxl/pci.c
   :doc: cxl pci

.. kernel-doc:: drivers/cxl/pci.c
   :internal:

CXL Core
--------
.. kernel-doc:: drivers/cxl/cxl.h
   :doc: cxl objects

.. kernel-doc:: drivers/cxl/cxl.h
   :internal:

.. kernel-doc:: drivers/cxl/core.c
   :doc: cxl core

External Interfaces
===================

CXL IOCTL Interface
-------------------

.. kernel-doc:: include/uapi/linux/cxl_mem.h
   :doc: UAPI

.. kernel-doc:: include/uapi/linux/cxl_mem.h
   :internal:
