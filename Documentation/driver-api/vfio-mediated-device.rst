.. SPDX-License-Identifier: GPL-2.0-only
.. include:: <isonum.txt>

=====================
VFIO Mediated devices
=====================

:Copyright: |copy| 2016, NVIDIA CORPORATION. All rights reserved.
:Author: Neo Jia <cjia@nvidia.com>
:Author: Kirti Wankhede <kwankhede@nvidia.com>



Virtual Function I/O (VFIO) Mediated devices[1]
===============================================

The number of use cases for virtualizing DMA devices that do not have built-in
SR_IOV capability is increasing. Previously, to virtualize such devices,
developers had to create their own management interfaces and APIs, and then
integrate them with user space software. To simplify integration with user space
software, we have identified common requirements and a unified management
interface for such devices.

The VFIO driver framework provides unified APIs for direct device access. It is
an IOMMU/device-agnostic framework for exposing direct device access to user
space in a secure, IOMMU-protected environment. This framework is used for
multiple devices, such as GPUs, network adapters, and compute accelerators. With
direct device access, virtual machines or user space applications have direct
access to the physical device. This framework is reused for mediated devices.

The mediated core driver provides a common interface for mediated device
management that can be used by drivers of different devices. This module
provides a generic interface to perform these operations:

* Create and destroy a mediated device
* Add a mediated device to and remove it from a mediated bus driver
* Add a mediated device to and remove it from an IOMMU group

The mediated core driver also provides an interface to register a bus driver.
For example, the mediated VFIO mdev driver is designed for mediated devices and
supports VFIO APIs. The mediated bus driver adds a mediated device to and
removes it from a VFIO group.

The following high-level block diagram shows the main components and interfaces
in the VFIO mediated driver framework. The diagram shows NVIDIA, Intel, and IBM
devices as examples, as these devices are the first devices to use this module::

     +---------------+
     |               |
     | +-----------+ |  mdev_register_driver() +--------------+
     | |           | +<------------------------+              |
     | |  mdev     | |                         |              |
     | |  bus      | +------------------------>+ vfio_mdev.ko |<-> VFIO user
     | |  driver   | |     probe()/remove()    |              |    APIs
     | |           | |                         +--------------+
     | +-----------+ |
     |               |
     |  MDEV CORE    |
     |   MODULE      |
     |   mdev.ko     |
     | +-----------+ |  mdev_register_parent() +--------------+
     | |           | +<------------------------+              |
     | |           | |                         | ccw_device.ko|<-> physical
     | |           | +------------------------>+              |    device
     | |           | |        callbacks        +--------------+
     | | Physical  | |
     | |  device   | |  mdev_register_parent() +--------------+
     | | interface | |<------------------------+              |
     | |           | |                         |  i915.ko     |<-> physical
     | |           | +------------------------>+              |    device
     | |           | |        callbacks        +--------------+
     | +-----------+ |
     +---------------+


Registration Interfaces
=======================

The mediated core driver provides the following types of registration
interfaces:

* Registration interface for a mediated bus driver
* Physical device driver interface

Registration Interface for a Mediated Bus Driver
------------------------------------------------

The registration interface for a mediated device driver provides the following
structure to represent a mediated device's driver::

     /*
      * struct mdev_driver [2] - Mediated device's driver
      * @probe: called when new device created
      * @remove: called when device removed
      * @driver: device driver structure
      */
     struct mdev_driver {
	     int  (*probe)  (struct mdev_device *dev);
	     void (*remove) (struct mdev_device *dev);
	     unsigned int (*get_available)(struct mdev_type *mtype);
	     ssize_t (*show_description)(struct mdev_type *mtype, char *buf);
	     struct device_driver    driver;
     };

A mediated bus driver for mdev should use this structure in the function calls
to register and unregister itself with the core driver:

* Register::

    int mdev_register_driver(struct mdev_driver *drv);

* Unregister::

    void mdev_unregister_driver(struct mdev_driver *drv);

The mediated bus driver's probe function should create a vfio_device on top of
the mdev_device and connect it to an appropriate implementation of
vfio_device_ops.

When a driver wants to add the GUID creation sysfs to an existing device it has
probe'd to then it should call::

    int mdev_register_parent(struct mdev_parent *parent, struct device *dev,
			struct mdev_driver *mdev_driver);

This will provide the 'mdev_supported_types/XX/create' files which can then be
used to trigger the creation of a mdev_device. The created mdev_device will be
attached to the specified driver.

When the driver needs to remove itself it calls::

    void mdev_unregister_parent(struct mdev_parent *parent);

Which will unbind and destroy all the created mdevs and remove the sysfs files.

Mediated Device Management Interface Through sysfs
==================================================

The management interface through sysfs enables user space software, such as
libvirt, to query and configure mediated devices in a hardware-agnostic fashion.
This management interface provides flexibility to the underlying physical
device's driver to support features such as:

* Mediated device hot plug
* Multiple mediated devices in a single virtual machine
* Multiple mediated devices from different physical devices

Links in the mdev_bus Class Directory
-------------------------------------
The /sys/class/mdev_bus/ directory contains links to devices that are registered
with the mdev core driver.

Directories and files under the sysfs for Each Physical Device
--------------------------------------------------------------

::

  |- [parent physical device]
  |--- Vendor-specific-attributes [optional]
  |--- [mdev_supported_types]
  |     |--- [<type-id>]
  |     |   |--- create
  |     |   |--- name
  |     |   |--- available_instances
  |     |   |--- device_api
  |     |   |--- description
  |     |   |--- [devices]
  |     |--- [<type-id>]
  |     |   |--- create
  |     |   |--- name
  |     |   |--- available_instances
  |     |   |--- device_api
  |     |   |--- description
  |     |   |--- [devices]
  |     |--- [<type-id>]
  |          |--- create
  |          |--- name
  |          |--- available_instances
  |          |--- device_api
  |          |--- description
  |          |--- [devices]

* [mdev_supported_types]

  The list of currently supported mediated device types and their details.

  [<type-id>], device_api, and available_instances are mandatory attributes
  that should be provided by vendor driver.

* [<type-id>]

  The [<type-id>] name is created by adding the device driver string as a prefix
  to the string provided by the vendor driver. This format of this name is as
  follows::

	sprintf(buf, "%s-%s", dev_driver_string(parent->dev), group->name);

* device_api

  This attribute shows which device API is being created, for example,
  "vfio-pci" for a PCI device.

* available_instances

  This attribute shows the number of devices of type <type-id> that can be
  created.

* [device]

  This directory contains links to the devices of type <type-id> that have been
  created.

* name

  This attribute shows a human readable name.

* description

  This attribute can show brief features/description of the type. This is an
  optional attribute.

Directories and Files Under the sysfs for Each mdev Device
----------------------------------------------------------

::

  |- [parent phy device]
  |--- [$MDEV_UUID]
         |--- remove
         |--- mdev_type {link to its type}
         |--- vendor-specific-attributes [optional]

* remove (write only)

Writing '1' to the 'remove' file destroys the mdev device. The vendor driver can
fail the remove() callback if that device is active and the vendor driver
doesn't support hot unplug.

Example::

	# echo 1 > /sys/bus/mdev/devices/$mdev_UUID/remove

Mediated device Hot plug
------------------------

Mediated devices can be created and assigned at runtime. The procedure to hot
plug a mediated device is the same as the procedure to hot plug a PCI device.

Translation APIs for Mediated Devices
=====================================

The following APIs are provided for translating user pfn to host pfn in a VFIO
driver::

	int vfio_pin_pages(struct vfio_device *device, dma_addr_t iova,
				  int npage, int prot, struct page **pages);

	void vfio_unpin_pages(struct vfio_device *device, dma_addr_t iova,
				    int npage);

These functions call back into the back-end IOMMU module by using the pin_pages
and unpin_pages callbacks of the struct vfio_iommu_driver_ops[4]. Currently
these callbacks are supported in the TYPE1 IOMMU module. To enable them for
other IOMMU backend modules, such as PPC64 sPAPR module, they need to provide
these two callback functions.

References
==========

1. See Documentation/driver-api/vfio.rst for more information on VFIO.
2. struct mdev_driver in include/linux/mdev.h
3. struct mdev_parent_ops in include/linux/mdev.h
4. struct vfio_iommu_driver_ops in include/linux/vfio.h
