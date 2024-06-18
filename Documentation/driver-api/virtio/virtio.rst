.. SPDX-License-Identifier: GPL-2.0

.. _virtio:

===============
Virtio on Linux
===============

Introduction
============

Virtio is an open standard that defines a protocol for communication
between drivers and devices of different types, see Chapter 5 ("Device
Types") of the virtio spec (`[1]`_). Originally developed as a standard
for paravirtualized devices implemented by a hypervisor, it can be used
to interface any compliant device (real or emulated) with a driver.

For illustrative purposes, this document will focus on the common case
of a Linux kernel running in a virtual machine and using paravirtualized
devices provided by the hypervisor, which exposes them as virtio devices
via standard mechanisms such as PCI.


Device - Driver communication: virtqueues
=========================================

Although the virtio devices are really an abstraction layer in the
hypervisor, they're exposed to the guest as if they are physical devices
using a specific transport method -- PCI, MMIO or CCW -- that is
orthogonal to the device itself. The virtio spec defines these transport
methods in detail, including device discovery, capabilities and
interrupt handling.

The communication between the driver in the guest OS and the device in
the hypervisor is done through shared memory (that's what makes virtio
devices so efficient) using specialized data structures called
virtqueues, which are actually ring buffers [#f1]_ of buffer descriptors
similar to the ones used in a network device:

.. kernel-doc:: include/uapi/linux/virtio_ring.h
    :identifiers: struct vring_desc

All the buffers the descriptors point to are allocated by the guest and
used by the host either for reading or for writing but not for both.

Refer to Chapter 2.5 ("Virtqueues") of the virtio spec (`[1]`_) for the
reference definitions of virtqueues and "Virtqueues and virtio ring: How
the data travels" blog post (`[2]`_) for an illustrated overview of how
the host device and the guest driver communicate.

The :c:type:`vring_virtqueue` struct models a virtqueue, including the
ring buffers and management data. Embedded in this struct is the
:c:type:`virtqueue` struct, which is the data structure that's
ultimately used by virtio drivers:

.. kernel-doc:: include/linux/virtio.h
    :identifiers: struct virtqueue

The callback function pointed by this struct is triggered when the
device has consumed the buffers provided by the driver. More
specifically, the trigger will be an interrupt issued by the hypervisor
(see vring_interrupt()). Interrupt request handlers are registered for
a virtqueue during the virtqueue setup process (transport-specific).

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: vring_interrupt


Device discovery and probing
============================

In the kernel, the virtio core contains the virtio bus driver and
transport-specific drivers like `virtio-pci` and `virtio-mmio`. Then
there are individual virtio drivers for specific device types that are
registered to the virtio bus driver.

How a virtio device is found and configured by the kernel depends on how
the hypervisor defines it. Taking the `QEMU virtio-console
<https://gitlab.com/qemu-project/qemu/-/blob/master/hw/char/virtio-console.c>`__
device as an example. When using PCI as a transport method, the device
will present itself on the PCI bus with vendor 0x1af4 (Red Hat, Inc.)
and device id 0x1003 (virtio console), as defined in the spec, so the
kernel will detect it as it would do with any other PCI device.

During the PCI enumeration process, if a device is found to match the
virtio-pci driver (according to the virtio-pci device table, any PCI
device with vendor id = 0x1af4)::

	/* Qumranet donated their vendor ID for devices 0x1000 thru 0x10FF. */
	static const struct pci_device_id virtio_pci_id_table[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_REDHAT_QUMRANET, PCI_ANY_ID) },
		{ 0 }
	};

then the virtio-pci driver is probed and, if the probing goes well, the
device is registered to the virtio bus::

	static int virtio_pci_probe(struct pci_dev *pci_dev,
				    const struct pci_device_id *id)
	{
		...

		if (force_legacy) {
			rc = virtio_pci_legacy_probe(vp_dev);
			/* Also try modern mode if we can't map BAR0 (no IO space). */
			if (rc == -ENODEV || rc == -ENOMEM)
				rc = virtio_pci_modern_probe(vp_dev);
			if (rc)
				goto err_probe;
		} else {
			rc = virtio_pci_modern_probe(vp_dev);
			if (rc == -ENODEV)
				rc = virtio_pci_legacy_probe(vp_dev);
			if (rc)
				goto err_probe;
		}

		...

		rc = register_virtio_device(&vp_dev->vdev);

When the device is registered to the virtio bus the kernel will look
for a driver in the bus that can handle the device and call that
driver's ``probe`` method.

At this point, the virtqueues will be allocated and configured by
calling the appropriate ``virtio_find`` helper function, such as
virtio_find_single_vq() or virtio_find_vqs(), which will end up calling
a transport-specific ``find_vqs`` method.


References
==========

_`[1]` Virtio Spec v1.2:
https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html

.. Check for later versions of the spec as well.

_`[2]` Virtqueues and virtio ring: How the data travels
https://www.redhat.com/en/blog/virtqueues-and-virtio-ring-how-data-travels

.. rubric:: Footnotes

.. [#f1] that's why they may be also referred to as virtrings.
