.. SPDX-License-Identifier: GPL-2.0

==============================
Driver implementer's API guide
==============================

The kernel offers a wide variety of interfaces to support the development
of device drivers.  This document is an only somewhat organized collection
of some of those interfaces — it will hopefully get better over time!  The
available subsections can be seen below.


General information for driver authors
======================================

This section contains documentation that should, at some point or other, be
of interest to most developers working on device drivers.

.. toctree::
   :maxdepth: 1

   basics
   driver-model/index
   device_link
   infrastructure
   ioctl
   pm/index

Useful support libraries
========================

This section contains documentation that should, at some point or other, be
of interest to most developers working on device drivers.

.. toctree::
   :maxdepth: 1

   early-userspace/index
   connector
   device-io
   devfreq
   dma-buf
   component
   io-mapping
   io_ordering
   uio-howto
   vfio-mediated-device
   vfio
   vfio-pci-device-specific-driver-acceptance

Bus-level documentation
=======================

.. toctree::
   :maxdepth: 1

   auxiliary_bus
   cxl/index
   eisa
   firewire
   i3c/index
   isa
   men-chameleon-bus
   pci/index
   rapidio/index
   slimbus
   usb/index
   virtio/index
   vme
   w1
   xillybus


Subsystem-specific APIs
=======================

.. toctree::
   :maxdepth: 1

   80211/index
   acpi/index
   backlight/lp855x-driver.rst
   clk
   console
   crypto/index
   dmaengine/index
   dpll
   edac
   firmware/index
   fpga/index
   frame-buffer
   aperture
   generic-counter
   gpio/index
   hsi
   hte/index
   i2c
   iio/index
   infiniband
   input
   interconnect
   ipmb
   ipmi
   libata
   mailbox
   md/index
   media/index
   mei/index
   memory-devices/index
   message-based
   misc_devices
   miscellaneous
   mmc/index
   mtd/index
   mtdnand
   nfc/index
   ntb
   nvdimm/index
   nvmem
   parport-lowlevel
   phy/index
   pin-control
   pldmfw/index
   pps
   ptp
   pwm
   regulator
   reset
   rfkill
   s390-drivers
   scsi
   serial/index
   sm501
   soundwire/index
   spi
   surface_aggregator/index
   switchtec
   sync_file
   target
   tee
   thermal/index
   tty/index
   wbrf
   wmi
   xilinx/index
   zorro

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`
