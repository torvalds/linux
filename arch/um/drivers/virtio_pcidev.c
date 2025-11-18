// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/logic_iomem.h>
#include <linux/of_platform.h>
#include <linux/irqdomain.h>
#include <linux/virtio_pcidev.h>
#include <linux/virtio-uml.h>
#include <linux/delay.h>
#include <linux/msi.h>
#include <linux/unaligned.h>
#include <irq_kern.h>

#include "virt-pci.h"

#define to_virtio_pcidev(_pdev) \
	container_of(_pdev, struct virtio_pcidev_device, pdev)

/* for MSI-X we have a 32-bit payload */
#define MAX_IRQ_MSG_SIZE (sizeof(struct virtio_pcidev_msg) + sizeof(u32))
#define NUM_IRQ_MSGS	10

struct virtio_pcidev_message_buffer {
	struct virtio_pcidev_msg hdr;
	u8 data[8];
};

struct virtio_pcidev_device {
	struct um_pci_device pdev;
	struct virtio_device *vdev;

	struct virtqueue *cmd_vq, *irq_vq;

#define VIRTIO_PCIDEV_WRITE_BUFS	20
	struct virtio_pcidev_message_buffer bufs[VIRTIO_PCIDEV_WRITE_BUFS + 1];
	void *extra_ptrs[VIRTIO_PCIDEV_WRITE_BUFS + 1];
	DECLARE_BITMAP(used_bufs, VIRTIO_PCIDEV_WRITE_BUFS);

#define VIRTIO_PCIDEV_STAT_WAITING	0
	unsigned long status;

	bool platform;
};

static unsigned int virtio_pcidev_max_delay_us = 40000;
module_param_named(max_delay_us, virtio_pcidev_max_delay_us, uint, 0644);

static int virtio_pcidev_get_buf(struct virtio_pcidev_device *dev, bool *posted)
{
	int i;

	for (i = 0; i < VIRTIO_PCIDEV_WRITE_BUFS; i++) {
		if (!test_and_set_bit(i, dev->used_bufs))
			return i;
	}

	*posted = false;
	return VIRTIO_PCIDEV_WRITE_BUFS;
}

static void virtio_pcidev_free_buf(struct virtio_pcidev_device *dev, void *buf)
{
	int i;

	if (buf == &dev->bufs[VIRTIO_PCIDEV_WRITE_BUFS]) {
		kfree(dev->extra_ptrs[VIRTIO_PCIDEV_WRITE_BUFS]);
		dev->extra_ptrs[VIRTIO_PCIDEV_WRITE_BUFS] = NULL;
		return;
	}

	for (i = 0; i < VIRTIO_PCIDEV_WRITE_BUFS; i++) {
		if (buf == &dev->bufs[i]) {
			kfree(dev->extra_ptrs[i]);
			dev->extra_ptrs[i] = NULL;
			WARN_ON(!test_and_clear_bit(i, dev->used_bufs));
			return;
		}
	}

	WARN_ON(1);
}

static int virtio_pcidev_send_cmd(struct virtio_pcidev_device *dev,
				  struct virtio_pcidev_msg *cmd,
				  unsigned int cmd_size,
				  const void *extra, unsigned int extra_size,
				  void *out, unsigned int out_size)
{
	struct scatterlist out_sg, extra_sg, in_sg;
	struct scatterlist *sgs_list[] = {
		[0] = &out_sg,
		[1] = extra ? &extra_sg : &in_sg,
		[2] = extra ? &in_sg : NULL,
	};
	struct virtio_pcidev_message_buffer *buf;
	int delay_count = 0;
	bool bounce_out;
	int ret, len;
	int buf_idx;
	bool posted;

	if (WARN_ON(cmd_size < sizeof(*cmd) || cmd_size > sizeof(*buf)))
		return -EINVAL;

	switch (cmd->op) {
	case VIRTIO_PCIDEV_OP_CFG_WRITE:
	case VIRTIO_PCIDEV_OP_MMIO_WRITE:
	case VIRTIO_PCIDEV_OP_MMIO_MEMSET:
		/* in PCI, writes are posted, so don't wait */
		posted = !out;
		WARN_ON(!posted);
		break;
	default:
		posted = false;
		break;
	}

	bounce_out = !posted && cmd_size <= sizeof(*cmd) &&
		     out && out_size <= sizeof(buf->data);

	buf_idx = virtio_pcidev_get_buf(dev, &posted);
	buf = &dev->bufs[buf_idx];
	memcpy(buf, cmd, cmd_size);

	if (posted && extra && extra_size > sizeof(buf) - cmd_size) {
		dev->extra_ptrs[buf_idx] = kmemdup(extra, extra_size,
						   GFP_ATOMIC);

		if (!dev->extra_ptrs[buf_idx]) {
			virtio_pcidev_free_buf(dev, buf);
			return -ENOMEM;
		}
		extra = dev->extra_ptrs[buf_idx];
	} else if (extra && extra_size <= sizeof(buf) - cmd_size) {
		memcpy((u8 *)buf + cmd_size, extra, extra_size);
		cmd_size += extra_size;
		extra_size = 0;
		extra = NULL;
		cmd = (void *)buf;
	} else {
		cmd = (void *)buf;
	}

	sg_init_one(&out_sg, cmd, cmd_size);
	if (extra)
		sg_init_one(&extra_sg, extra, extra_size);
	/* allow stack for small buffers */
	if (bounce_out)
		sg_init_one(&in_sg, buf->data, out_size);
	else if (out)
		sg_init_one(&in_sg, out, out_size);

	/* add to internal virtio queue */
	ret = virtqueue_add_sgs(dev->cmd_vq, sgs_list,
				extra ? 2 : 1,
				out ? 1 : 0,
				cmd, GFP_ATOMIC);
	if (ret) {
		virtio_pcidev_free_buf(dev, buf);
		return ret;
	}

	if (posted) {
		virtqueue_kick(dev->cmd_vq);
		return 0;
	}

	/* kick and poll for getting a response on the queue */
	set_bit(VIRTIO_PCIDEV_STAT_WAITING, &dev->status);
	virtqueue_kick(dev->cmd_vq);
	ret = 0;

	while (1) {
		void *completed = virtqueue_get_buf(dev->cmd_vq, &len);

		if (completed == buf)
			break;

		if (completed)
			virtio_pcidev_free_buf(dev, completed);

		if (WARN_ONCE(virtqueue_is_broken(dev->cmd_vq) ||
			      ++delay_count > virtio_pcidev_max_delay_us,
			      "um virt-pci delay: %d", delay_count)) {
			ret = -EIO;
			break;
		}
		udelay(1);
	}
	clear_bit(VIRTIO_PCIDEV_STAT_WAITING, &dev->status);

	if (bounce_out)
		memcpy(out, buf->data, out_size);

	virtio_pcidev_free_buf(dev, buf);

	return ret;
}

static unsigned long virtio_pcidev_cfgspace_read(struct um_pci_device *pdev,
						 unsigned int offset, int size)
{
	struct virtio_pcidev_device *dev = to_virtio_pcidev(pdev);
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_CFG_READ,
		.size = size,
		.addr = offset,
	};
	/* max 8, we might not use it all */
	u8 data[8];

	memset(data, 0xff, sizeof(data));

	/* size has been checked in um_pci_cfgspace_read() */
	if (virtio_pcidev_send_cmd(dev, &hdr, sizeof(hdr), NULL, 0, data, size))
		return ULONG_MAX;

	switch (size) {
	case 1:
		return data[0];
	case 2:
		return le16_to_cpup((void *)data);
	case 4:
		return le32_to_cpup((void *)data);
#ifdef CONFIG_64BIT
	case 8:
		return le64_to_cpup((void *)data);
#endif
	default:
		return ULONG_MAX;
	}
}

static void virtio_pcidev_cfgspace_write(struct um_pci_device *pdev,
					 unsigned int offset, int size,
					 unsigned long val)
{
	struct virtio_pcidev_device *dev = to_virtio_pcidev(pdev);
	struct {
		struct virtio_pcidev_msg hdr;
		/* maximum size - we may only use parts of it */
		u8 data[8];
	} msg = {
		.hdr = {
			.op = VIRTIO_PCIDEV_OP_CFG_WRITE,
			.size = size,
			.addr = offset,
		},
	};

	/* size has been checked in um_pci_cfgspace_write() */
	switch (size) {
	case 1:
		msg.data[0] = (u8)val;
		break;
	case 2:
		put_unaligned_le16(val, (void *)msg.data);
		break;
	case 4:
		put_unaligned_le32(val, (void *)msg.data);
		break;
#ifdef CONFIG_64BIT
	case 8:
		put_unaligned_le64(val, (void *)msg.data);
		break;
#endif
	}

	WARN_ON(virtio_pcidev_send_cmd(dev, &msg.hdr, sizeof(msg), NULL, 0, NULL, 0));
}

static void virtio_pcidev_bar_copy_from(struct um_pci_device *pdev,
					int bar, void *buffer,
					unsigned int offset, int size)
{
	struct virtio_pcidev_device *dev = to_virtio_pcidev(pdev);
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_MMIO_READ,
		.bar = bar,
		.size = size,
		.addr = offset,
	};

	memset(buffer, 0xff, size);

	virtio_pcidev_send_cmd(dev, &hdr, sizeof(hdr), NULL, 0, buffer, size);
}

static unsigned long virtio_pcidev_bar_read(struct um_pci_device *pdev, int bar,
					    unsigned int offset, int size)
{
	/* 8 is maximum size - we may only use parts of it */
	u8 data[8];

	/* size has been checked in um_pci_bar_read() */
	virtio_pcidev_bar_copy_from(pdev, bar, data, offset, size);

	switch (size) {
	case 1:
		return data[0];
	case 2:
		return le16_to_cpup((void *)data);
	case 4:
		return le32_to_cpup((void *)data);
#ifdef CONFIG_64BIT
	case 8:
		return le64_to_cpup((void *)data);
#endif
	default:
		return ULONG_MAX;
	}
}

static void virtio_pcidev_bar_copy_to(struct um_pci_device *pdev,
				      int bar, unsigned int offset,
				      const void *buffer, int size)
{
	struct virtio_pcidev_device *dev = to_virtio_pcidev(pdev);
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_MMIO_WRITE,
		.bar = bar,
		.size = size,
		.addr = offset,
	};

	virtio_pcidev_send_cmd(dev, &hdr, sizeof(hdr), buffer, size, NULL, 0);
}

static void virtio_pcidev_bar_write(struct um_pci_device *pdev, int bar,
				    unsigned int offset, int size,
				    unsigned long val)
{
	/* maximum size - we may only use parts of it */
	u8 data[8];

	/* size has been checked in um_pci_bar_write() */
	switch (size) {
	case 1:
		data[0] = (u8)val;
		break;
	case 2:
		put_unaligned_le16(val, (void *)data);
		break;
	case 4:
		put_unaligned_le32(val, (void *)data);
		break;
#ifdef CONFIG_64BIT
	case 8:
		put_unaligned_le64(val, (void *)data);
		break;
#endif
	}

	virtio_pcidev_bar_copy_to(pdev, bar, offset, data, size);
}

static void virtio_pcidev_bar_set(struct um_pci_device *pdev, int bar,
				  unsigned int offset, u8 value, int size)
{
	struct virtio_pcidev_device *dev = to_virtio_pcidev(pdev);
	struct {
		struct virtio_pcidev_msg hdr;
		u8 data;
	} msg = {
		.hdr = {
			.op = VIRTIO_PCIDEV_OP_CFG_WRITE,
			.bar = bar,
			.size = size,
			.addr = offset,
		},
		.data = value,
	};

	virtio_pcidev_send_cmd(dev, &msg.hdr, sizeof(msg), NULL, 0, NULL, 0);
}

static const struct um_pci_ops virtio_pcidev_um_pci_ops = {
	.cfgspace_read	= virtio_pcidev_cfgspace_read,
	.cfgspace_write	= virtio_pcidev_cfgspace_write,
	.bar_read	= virtio_pcidev_bar_read,
	.bar_write	= virtio_pcidev_bar_write,
	.bar_copy_from	= virtio_pcidev_bar_copy_from,
	.bar_copy_to	= virtio_pcidev_bar_copy_to,
	.bar_set	= virtio_pcidev_bar_set,
};

static void virtio_pcidev_irq_vq_addbuf(struct virtqueue *vq, void *buf, bool kick)
{
	struct scatterlist sg[1];

	sg_init_one(sg, buf, MAX_IRQ_MSG_SIZE);
	if (virtqueue_add_inbuf(vq, sg, 1, buf, GFP_ATOMIC))
		kfree(buf);
	else if (kick)
		virtqueue_kick(vq);
}

static void virtio_pcidev_handle_irq_message(struct virtqueue *vq,
					     struct virtio_pcidev_msg *msg)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pcidev_device *dev = vdev->priv;

	if (!dev->pdev.irq)
		return;

	/* we should properly chain interrupts, but on ARCH=um we don't care */

	switch (msg->op) {
	case VIRTIO_PCIDEV_OP_INT:
		generic_handle_irq(dev->pdev.irq);
		break;
	case VIRTIO_PCIDEV_OP_MSI:
		/* our MSI message is just the interrupt number */
		if (msg->size == sizeof(u32))
			generic_handle_irq(le32_to_cpup((void *)msg->data));
		else
			generic_handle_irq(le16_to_cpup((void *)msg->data));
		break;
	case VIRTIO_PCIDEV_OP_PME:
		/* nothing to do - we already woke up due to the message */
		break;
	default:
		dev_err(&vdev->dev, "unexpected virt-pci message %d\n", msg->op);
		break;
	}
}

static void virtio_pcidev_cmd_vq_cb(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pcidev_device *dev = vdev->priv;
	void *cmd;
	int len;

	if (test_bit(VIRTIO_PCIDEV_STAT_WAITING, &dev->status))
		return;

	while ((cmd = virtqueue_get_buf(vq, &len)))
		virtio_pcidev_free_buf(dev, cmd);
}

static void virtio_pcidev_irq_vq_cb(struct virtqueue *vq)
{
	struct virtio_pcidev_msg *msg;
	int len;

	while ((msg = virtqueue_get_buf(vq, &len))) {
		if (len >= sizeof(*msg))
			virtio_pcidev_handle_irq_message(vq, msg);

		/* recycle the message buffer */
		virtio_pcidev_irq_vq_addbuf(vq, msg, true);
	}
}

static int virtio_pcidev_init_vqs(struct virtio_pcidev_device *dev)
{
	struct virtqueue_info vqs_info[] = {
		{ "cmd", virtio_pcidev_cmd_vq_cb },
		{ "irq", virtio_pcidev_irq_vq_cb },
	};
	struct virtqueue *vqs[2];
	int err, i;

	err = virtio_find_vqs(dev->vdev, 2, vqs, vqs_info, NULL);
	if (err)
		return err;

	dev->cmd_vq = vqs[0];
	dev->irq_vq = vqs[1];

	virtio_device_ready(dev->vdev);

	for (i = 0; i < NUM_IRQ_MSGS; i++) {
		void *msg = kzalloc(MAX_IRQ_MSG_SIZE, GFP_KERNEL);

		if (msg)
			virtio_pcidev_irq_vq_addbuf(dev->irq_vq, msg, false);
	}

	virtqueue_kick(dev->irq_vq);

	return 0;
}

static void __virtio_pcidev_virtio_platform_remove(struct virtio_device *vdev,
						   struct virtio_pcidev_device *dev)
{
	um_pci_platform_device_unregister(&dev->pdev);

	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);

	kfree(dev);
}

static int virtio_pcidev_virtio_platform_probe(struct virtio_device *vdev,
					       struct virtio_pcidev_device *dev)
{
	int err;

	dev->platform = true;

	err = virtio_pcidev_init_vqs(dev);
	if (err)
		goto err_free;

	err = um_pci_platform_device_register(&dev->pdev);
	if (err)
		goto err_reset;

	err = of_platform_default_populate(vdev->dev.of_node, NULL, &vdev->dev);
	if (err)
		goto err_unregister;

	return 0;

err_unregister:
	um_pci_platform_device_unregister(&dev->pdev);
err_reset:
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
err_free:
	kfree(dev);
	return err;
}

static int virtio_pcidev_virtio_probe(struct virtio_device *vdev)
{
	struct virtio_pcidev_device *dev;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vdev = vdev;
	vdev->priv = dev;

	dev->pdev.ops = &virtio_pcidev_um_pci_ops;

	if (of_device_is_compatible(vdev->dev.of_node, "simple-bus"))
		return virtio_pcidev_virtio_platform_probe(vdev, dev);

	err = virtio_pcidev_init_vqs(dev);
	if (err)
		goto err_free;

	err = um_pci_device_register(&dev->pdev);
	if (err)
		goto err_reset;

	device_set_wakeup_enable(&vdev->dev, true);

	/*
	 * In order to do suspend-resume properly, don't allow VQs
	 * to be suspended.
	 */
	virtio_uml_set_no_vq_suspend(vdev, true);

	return 0;

err_reset:
	virtio_reset_device(vdev);
	vdev->config->del_vqs(vdev);
err_free:
	kfree(dev);
	return err;
}

static void virtio_pcidev_virtio_remove(struct virtio_device *vdev)
{
	struct virtio_pcidev_device *dev = vdev->priv;

	if (dev->platform) {
		of_platform_depopulate(&vdev->dev);
		__virtio_pcidev_virtio_platform_remove(vdev, dev);
		return;
	}

	device_set_wakeup_enable(&vdev->dev, false);

	um_pci_device_unregister(&dev->pdev);

	/* Stop all virtqueues */
	virtio_reset_device(vdev);
	dev->cmd_vq = NULL;
	dev->irq_vq = NULL;
	vdev->config->del_vqs(vdev);

	kfree(dev);
}

static void virtio_pcidev_virtio_shutdown(struct virtio_device *vdev)
{
	/* nothing to do, we just don't want queue shutdown */
}

static struct virtio_device_id id_table[] = {
	{ CONFIG_UML_PCI_OVER_VIRTIO_DEVICE_ID, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_pcidev_virtio_driver = {
	.driver.name = "virtio-pci",
	.id_table = id_table,
	.probe = virtio_pcidev_virtio_probe,
	.remove = virtio_pcidev_virtio_remove,
	.shutdown = virtio_pcidev_virtio_shutdown,
};

static int __init virtio_pcidev_init(void)
{
	if (WARN(CONFIG_UML_PCI_OVER_VIRTIO_DEVICE_ID < 0,
		 "No virtio device ID configured for PCI - no PCI support\n"))
		return 0;

	return register_virtio_driver(&virtio_pcidev_virtio_driver);
}
late_initcall(virtio_pcidev_init);

static void __exit virtio_pcidev_exit(void)
{
	unregister_virtio_driver(&virtio_pcidev_virtio_driver);
}
module_exit(virtio_pcidev_exit);
