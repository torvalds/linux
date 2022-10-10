.. SPDX-License-Identifier: GPL-2.0

.. _writing_virtio_drivers:

======================
Writing Virtio Drivers
======================

Introduction
============

This document serves as a basic guideline for driver programmers that
need to hack a new virtio driver or understand the essentials of the
existing ones. See :ref:`Virtio on Linux <virtio>` for a general
overview of virtio.


Driver boilerplate
==================

As a bare minimum, a virtio driver needs to register in the virtio bus
and configure the virtqueues for the device according to its spec, the
configuration of the virtqueues in the driver side must match the
virtqueue definitions in the device. A basic driver skeleton could look
like this::

	#include <linux/virtio.h>
	#include <linux/virtio_ids.h>
	#include <linux/virtio_config.h>
	#include <linux/module.h>

	/* device private data (one per device) */
	struct virtio_dummy_dev {
		struct virtqueue *vq;
	};

	static void virtio_dummy_recv_cb(struct virtqueue *vq)
	{
		struct virtio_dummy_dev *dev = vq->vdev->priv;
		char *buf;
		unsigned int len;

		while ((buf = virtqueue_get_buf(dev->vq, &len)) != NULL) {
			/* process the received data */
		}
	}

	static int virtio_dummy_probe(struct virtio_device *vdev)
	{
		struct virtio_dummy_dev *dev = NULL;

		/* initialize device data */
		dev = kzalloc(sizeof(struct virtio_dummy_dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;

		/* the device has a single virtqueue */
		dev->vq = virtio_find_single_vq(vdev, virtio_dummy_recv_cb, "input");
		if (IS_ERR(dev->vq)) {
			kfree(dev);
			return PTR_ERR(dev->vq);

		}
		vdev->priv = dev;

		/* from this point on, the device can notify and get callbacks */
		virtio_device_ready(vdev);

		return 0;
	}

	static void virtio_dummy_remove(struct virtio_device *vdev)
	{
		struct virtio_dummy_dev *dev = vdev->priv;

		/*
		 * disable vq interrupts: equivalent to
		 * vdev->config->reset(vdev)
		 */
		virtio_reset_device(vdev);

		/* detach unused buffers */
		while ((buf = virtqueue_detach_unused_buf(dev->vq)) != NULL) {
			kfree(buf);
		}

		/* remove virtqueues */
		vdev->config->del_vqs(vdev);

		kfree(dev);
	}

	static const struct virtio_device_id id_table[] = {
		{ VIRTIO_ID_DUMMY, VIRTIO_DEV_ANY_ID },
		{ 0 },
	};

	static struct virtio_driver virtio_dummy_driver = {
		.driver.name =  KBUILD_MODNAME,
		.driver.owner = THIS_MODULE,
		.id_table =     id_table,
		.probe =        virtio_dummy_probe,
		.remove =       virtio_dummy_remove,
	};

	module_virtio_driver(virtio_dummy_driver);
	MODULE_DEVICE_TABLE(virtio, id_table);
	MODULE_DESCRIPTION("Dummy virtio driver");
	MODULE_LICENSE("GPL");

The device id ``VIRTIO_ID_DUMMY`` here is a placeholder, virtio drivers
should be added only for devices that are defined in the spec, see
include/uapi/linux/virtio_ids.h. Device ids need to be at least reserved
in the virtio spec before being added to that file.

If your driver doesn't have to do anything special in its ``init`` and
``exit`` methods, you can use the module_virtio_driver() helper to
reduce the amount of boilerplate code.

The ``probe`` method does the minimum driver setup in this case
(memory allocation for the device data) and initializes the
virtqueue. virtio_device_ready() is used to enable the virtqueue and to
notify the device that the driver is ready to manage the device
("DRIVER_OK"). The virtqueues are anyway enabled automatically by the
core after ``probe`` returns.

.. kernel-doc:: include/linux/virtio_config.h
    :identifiers: virtio_device_ready

In any case, the virtqueues need to be enabled before adding buffers to
them.

Sending and receiving data
==========================

The virtio_dummy_recv_cb() callback in the code above will be triggered
when the device notifies the driver after it finishes processing a
descriptor or descriptor chain, either for reading or writing. However,
that's only the second half of the virtio device-driver communication
process, as the communication is always started by the driver regardless
of the direction of the data transfer.

To configure a buffer transfer from the driver to the device, first you
have to add the buffers -- packed as `scatterlists` -- to the
appropriate virtqueue using any of the virtqueue_add_inbuf(),
virtqueue_add_outbuf() or virtqueue_add_sgs(), depending on whether you
need to add one input `scatterlist` (for the device to fill in), one
output `scatterlist` (for the device to consume) or multiple
`scatterlists`, respectively. Then, once the virtqueue is set up, a call
to virtqueue_kick() sends a notification that will be serviced by the
hypervisor that implements the device::

	struct scatterlist sg[1];
	sg_init_one(sg, buffer, BUFLEN);
	virtqueue_add_inbuf(dev->vq, sg, 1, buffer, GFP_ATOMIC);
	virtqueue_kick(dev->vq);

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_add_inbuf

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_add_outbuf

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_add_sgs

Then, after the device has read or written the buffers prepared by the
driver and notifies it back, the driver can call virtqueue_get_buf() to
read the data produced by the device (if the virtqueue was set up with
input buffers) or simply to reclaim the buffers if they were already
consumed by the device:

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_get_buf_ctx

The virtqueue callbacks can be disabled and re-enabled using the
virtqueue_disable_cb() and the family of virtqueue_enable_cb() functions
respectively. See drivers/virtio/virtio_ring.c for more details:

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_disable_cb

.. kernel-doc:: drivers/virtio/virtio_ring.c
    :identifiers: virtqueue_enable_cb

But note that some spurious callbacks can still be triggered under
certain scenarios. The way to disable callbacks reliably is to reset the
device or the virtqueue (virtio_reset_device()).


References
==========

_`[1]` Virtio Spec v1.2:
https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html

Check for later versions of the spec as well.
