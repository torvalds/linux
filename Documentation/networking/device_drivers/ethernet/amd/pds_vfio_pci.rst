.. SPDX-License-Identifier: GPL-2.0+
.. note: can be edited and viewed with /usr/bin/formiko-vim

==========================================================
PCI VFIO driver for the AMD/Pensando(R) DSC adapter family
==========================================================

AMD/Pensando Linux VFIO PCI Device Driver
Copyright(c) 2023 Advanced Micro Devices, Inc.

Overview
========

The ``pds-vfio-pci`` module is a PCI driver that supports Live Migration
capable Virtual Function (VF) devices in the DSC hardware.

Using the device
================

The pds-vfio-pci device is enabled via multiple configuration steps and
depends on the ``pds_core`` driver to create and enable SR-IOV Virtual
Function devices.

Shown below are the steps to bind the driver to a VF and also to the
associated auxiliary device created by the ``pds_core`` driver. This
example assumes the pds_core and pds-vfio-pci modules are already
loaded.

.. code-block:: bash
  :name: example-setup-script

  #!/bin/bash

  PF_BUS="0000:60"
  PF_BDF="0000:60:00.0"
  VF_BDF="0000:60:00.1"

  # Prevent non-vfio VF driver from probing the VF device
  echo 0 > /sys/class/pci_bus/$PF_BUS/device/$PF_BDF/sriov_drivers_autoprobe

  # Create single VF for Live Migration via pds_core
  echo 1 > /sys/bus/pci/drivers/pds_core/$PF_BDF/sriov_numvfs

  # Allow the VF to be bound to the pds-vfio-pci driver
  echo "pds-vfio-pci" > /sys/class/pci_bus/$PF_BUS/device/$VF_BDF/driver_override

  # Bind the VF to the pds-vfio-pci driver
  echo "$VF_BDF" > /sys/bus/pci/drivers/pds-vfio-pci/bind

After performing the steps above, a file in /dev/vfio/<iommu_group>
should have been created.


Enabling the driver
===================

The driver is enabled via the standard kernel configuration system,
using the make command::

  make oldconfig/menuconfig/etc.

The driver is located in the menu structure at:

  -> Device Drivers
    -> VFIO Non-Privileged userspace driver framework
      -> VFIO support for PDS PCI devices

Support
=======

For general Linux networking support, please use the netdev mailing
list, which is monitored by Pensando personnel::

  netdev@vger.kernel.org

For more specific support needs, please use the Pensando driver support
email::

  drivers@pensando.io
