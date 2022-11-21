========================================
The Linux driver implementer's API guide
========================================

The kernel offers a wide variety of interfaces to support the development
of device drivers.  This document is an only somewhat organized collection
of some of those interfaces — it will hopefully get better over time!  The
available subsections can be seen below.

.. class:: toc-title

	   Table of contents

.. toctree::
   :maxdepth: 2

   driver-model/index
   basics
   infrastructure
   ioctl
   early-userspace/index
   pm/index
   clk
   device-io
   dma-buf
   device_link
   component
   message-based
   infiniband
   frame-buffer
   regulator
   reset
   iio/index
   input
   usb/index
   firewire
   pci/index
   cxl/index
   spi
   i2c
   ipmb
   ipmi
   i3c/index
   interconnect
   devfreq
   hsi
   edac
   scsi
   libata
   target
   mailbox
   mtdnand
   miscellaneous
   mei/index
   mtd/index
   mmc/index
   nvdimm/index
   w1
   rapidio/index
   s390-drivers
   vme
   80211/index
   uio-howto
   firmware/index
   pin-control
   gpio/index
   md/index
   media/index
   misc_devices
   nfc/index
   dmaengine/index
   slimbus
   soundwire/index
   thermal/index
   fpga/index
   acpi/index
   auxiliary_bus
   backlight/lp855x-driver.rst
   connector
   console
   dcdbas
   eisa
   isa
   isapnp
   io-mapping
   io_ordering
   generic-counter
   memory-devices/index
   men-chameleon-bus
   ntb
   nvmem
   parport-lowlevel
   pps
   ptp
   phy/index
   pwm
   pldmfw/index
   rfkill
   serial/index
   sm501
   surface_aggregator/index
   switchtec
   sync_file
   vfio-mediated-device
   vfio
   vfio-pci-device-specific-driver-acceptance
   xilinx/index
   xillybus
   zorro

.. only::  subproject and html

   Indices
   =======

   * :ref:`genindex`
