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
#include <linux/irqdomain.h>
#include <linux/virtio_pcidev.h>
#include <linux/virtio-uml.h>
#include <linux/delay.h>
#include <linux/msi.h>
#include <asm/unaligned.h>
#include <irq_kern.h>

#define MAX_DEVICES 8
#define MAX_MSI_VECTORS 32
#define CFG_SPACE_SIZE 4096

/* for MSI-X we have a 32-bit payload */
#define MAX_IRQ_MSG_SIZE (sizeof(struct virtio_pcidev_msg) + sizeof(u32))
#define NUM_IRQ_MSGS	10

#define HANDLE_NO_FREE(ptr) ((void *)((unsigned long)(ptr) | 1))
#define HANDLE_IS_NO_FREE(ptr) ((unsigned long)(ptr) & 1)

struct um_pci_device {
	struct virtio_device *vdev;

	/* for now just standard BARs */
	u8 resptr[PCI_STD_NUM_BARS];

	struct virtqueue *cmd_vq, *irq_vq;

#define UM_PCI_STAT_WAITING	0
	unsigned long status;

	int irq;
};

struct um_pci_device_reg {
	struct um_pci_device *dev;
	void __iomem *iomem;
};

static struct pci_host_bridge *bridge;
static DEFINE_MUTEX(um_pci_mtx);
static struct um_pci_device_reg um_pci_devices[MAX_DEVICES];
static struct fwnode_handle *um_pci_fwnode;
static struct irq_domain *um_pci_inner_domain;
static struct irq_domain *um_pci_msi_domain;
static unsigned long um_pci_msi_used[BITS_TO_LONGS(MAX_MSI_VECTORS)];

#define UM_VIRT_PCI_MAXDELAY 40000

static int um_pci_send_cmd(struct um_pci_device *dev,
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
	int delay_count = 0;
	int ret, len;
	bool posted;

	if (WARN_ON(cmd_size < sizeof(*cmd)))
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

	if (posted) {
		u8 *ncmd = kmalloc(cmd_size + extra_size, GFP_ATOMIC);

		if (ncmd) {
			memcpy(ncmd, cmd, cmd_size);
			if (extra)
				memcpy(ncmd + cmd_size, extra, extra_size);
			cmd = (void *)ncmd;
			cmd_size += extra_size;
			extra = NULL;
			extra_size = 0;
		} else {
			/* try without allocating memory */
			posted = false;
		}
	}

	sg_init_one(&out_sg, cmd, cmd_size);
	if (extra)
		sg_init_one(&extra_sg, extra, extra_size);
	if (out)
		sg_init_one(&in_sg, out, out_size);

	/* add to internal virtio queue */
	ret = virtqueue_add_sgs(dev->cmd_vq, sgs_list,
				extra ? 2 : 1,
				out ? 1 : 0,
				posted ? cmd : HANDLE_NO_FREE(cmd),
				GFP_ATOMIC);
	if (ret)
		return ret;

	if (posted) {
		virtqueue_kick(dev->cmd_vq);
		return 0;
	}

	/* kick and poll for getting a response on the queue */
	set_bit(UM_PCI_STAT_WAITING, &dev->status);
	virtqueue_kick(dev->cmd_vq);

	while (1) {
		void *completed = virtqueue_get_buf(dev->cmd_vq, &len);

		if (completed == HANDLE_NO_FREE(cmd))
			break;

		if (completed && !HANDLE_IS_NO_FREE(completed))
			kfree(completed);

		if (WARN_ONCE(virtqueue_is_broken(dev->cmd_vq) ||
			      ++delay_count > UM_VIRT_PCI_MAXDELAY,
			      "um virt-pci delay: %d", delay_count)) {
			ret = -EIO;
			break;
		}
		udelay(1);
	}
	clear_bit(UM_PCI_STAT_WAITING, &dev->status);

	return ret;
}

static unsigned long um_pci_cfgspace_read(void *priv, unsigned int offset,
					  int size)
{
	struct um_pci_device_reg *reg = priv;
	struct um_pci_device *dev = reg->dev;
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_CFG_READ,
		.size = size,
		.addr = offset,
	};
	/* maximum size - we may only use parts of it */
	u8 data[8];

	if (!dev)
		return ~0ULL;

	memset(data, 0xff, sizeof(data));

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid config space read size %d\n", size);
		return ~0ULL;
	}

	if (um_pci_send_cmd(dev, &hdr, sizeof(hdr), NULL, 0,
			    data, sizeof(data)))
		return ~0ULL;

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
		return ~0ULL;
	}
}

static void um_pci_cfgspace_write(void *priv, unsigned int offset, int size,
				  unsigned long val)
{
	struct um_pci_device_reg *reg = priv;
	struct um_pci_device *dev = reg->dev;
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

	if (!dev)
		return;

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
	default:
		WARN(1, "invalid config space write size %d\n", size);
		return;
	}

	WARN_ON(um_pci_send_cmd(dev, &msg.hdr, sizeof(msg), NULL, 0, NULL, 0));
}

static const struct logic_iomem_ops um_pci_device_cfgspace_ops = {
	.read = um_pci_cfgspace_read,
	.write = um_pci_cfgspace_write,
};

static void um_pci_bar_copy_from(void *priv, void *buffer,
				 unsigned int offset, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_MMIO_READ,
		.bar = *resptr,
		.size = size,
		.addr = offset,
	};

	memset(buffer, 0xff, size);

	um_pci_send_cmd(dev, &hdr, sizeof(hdr), NULL, 0, buffer, size);
}

static unsigned long um_pci_bar_read(void *priv, unsigned int offset,
				     int size)
{
	/* maximum size - we may only use parts of it */
	u8 data[8];

	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		break;
	default:
		WARN(1, "invalid config space read size %d\n", size);
		return ~0ULL;
	}

	um_pci_bar_copy_from(priv, data, offset, size);

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
		return ~0ULL;
	}
}

static void um_pci_bar_copy_to(void *priv, unsigned int offset,
			       const void *buffer, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	struct virtio_pcidev_msg hdr = {
		.op = VIRTIO_PCIDEV_OP_MMIO_WRITE,
		.bar = *resptr,
		.size = size,
		.addr = offset,
	};

	um_pci_send_cmd(dev, &hdr, sizeof(hdr), buffer, size, NULL, 0);
}

static void um_pci_bar_write(void *priv, unsigned int offset, int size,
			     unsigned long val)
{
	/* maximum size - we may only use parts of it */
	u8 data[8];

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
	default:
		WARN(1, "invalid config space write size %d\n", size);
		return;
	}

	um_pci_bar_copy_to(priv, offset, data, size);
}

static void um_pci_bar_set(void *priv, unsigned int offset, u8 value, int size)
{
	u8 *resptr = priv;
	struct um_pci_device *dev = container_of(resptr - *resptr,
						 struct um_pci_device,
						 resptr[0]);
	struct {
		struct virtio_pcidev_msg hdr;
		u8 data;
	} msg = {
		.hdr = {
			.op = VIRTIO_PCIDEV_OP_CFG_WRITE,
			.bar = *resptr,
			.size = size,
			.addr = offset,
		},
		.data = value,
	};

	um_pci_send_cmd(dev, &msg.hdr, sizeof(msg), NULL, 0, NULL, 0);
}

static const struct logic_iomem_ops um_pci_device_bar_ops = {
	.read = um_pci_bar_read,
	.write = um_pci_bar_write,
	.set = um_pci_bar_set,
	.copy_from = um_pci_bar_copy_from,
	.copy_to = um_pci_bar_copy_to,
};

static void __iomem *um_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
				    int where)
{
	struct um_pci_device_reg *dev;
	unsigned int busn = bus->number;

	if (busn > 0)
		return NULL;

	/* not allowing functions for now ... */
	if (devfn % 8)
		return NULL;

	if (devfn / 8 >= ARRAY_SIZE(um_pci_devices))
		return NULL;

	dev = &um_pci_devices[devfn / 8];
	if (!dev)
		return NULL;

	return (void __iomem *)((unsigned long)dev->iomem + where);
}

static struct pci_ops um_pci_ops = {
	.map_bus = um_pci_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void um_pci_rescan(void)
{
	pci_lock_rescan_remove();
	pci_rescan_bus(bridge->bus);
	pci_unlock_rescan_remove();
}

static void um_pci_irq_vq_addbuf(struct virtqueue *vq, void *buf, bool kick)
{
	struct scatterlist sg[1];

	sg_init_one(sg, buf, MAX_IRQ_MSG_SIZE);
	if (virtqueue_add_inbuf(vq, sg, 1, buf, GFP_ATOMIC))
		kfree(buf);
	else if (kick)
		virtqueue_kick(vq);
}

static void um_pci_handle_irq_message(struct virtqueue *vq,
				      struct virtio_pcidev_msg *msg)
{
	struct virtio_device *vdev = vq->vdev;
	struct um_pci_device *dev = vdev->priv;

	/* we should properly chain interrupts, but on ARCH=um we don't care */

	switch (msg->op) {
	case VIRTIO_PCIDEV_OP_INT:
		generic_handle_irq(dev->irq);
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

static void um_pci_cmd_vq_cb(struct virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;
	struct um_pci_device *dev = vdev->priv;
	void *cmd;
	int len;

	if (test_bit(UM_PCI_STAT_WAITING, &dev->status))
		return;

	while ((cmd = virtqueue_get_buf(vq, &len))) {
		if (WARN_ON(HANDLE_IS_NO_FREE(cmd)))
			continue;
		kfree(cmd);
	}
}

static void um_pci_irq_vq_cb(struct virtqueue *vq)
{
	struct virtio_pcidev_msg *msg;
	int len;

	while ((msg = virtqueue_get_buf(vq, &len))) {
		if (len >= sizeof(*msg))
			um_pci_handle_irq_message(vq, msg);

		/* recycle the message buffer */
		um_pci_irq_vq_addbuf(vq, msg, true);
	}
}

static int um_pci_init_vqs(struct um_pci_device *dev)
{
	struct virtqueue *vqs[2];
	static const char *const names[2] = { "cmd", "irq" };
	vq_callback_t *cbs[2] = { um_pci_cmd_vq_cb, um_pci_irq_vq_cb };
	int err, i;

	err = virtio_find_vqs(dev->vdev, 2, vqs, cbs, names, NULL);
	if (err)
		return err;

	dev->cmd_vq = vqs[0];
	dev->irq_vq = vqs[1];

	for (i = 0; i < NUM_IRQ_MSGS; i++) {
		void *msg = kzalloc(MAX_IRQ_MSG_SIZE, GFP_KERNEL);

		if (msg)
			um_pci_irq_vq_addbuf(dev->irq_vq, msg, false);
	}

	virtqueue_kick(dev->irq_vq);

	return 0;
}

static int um_pci_virtio_probe(struct virtio_device *vdev)
{
	struct um_pci_device *dev;
	int i, free = -1;
	int err = -ENOSPC;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vdev = vdev;
	vdev->priv = dev;

	mutex_lock(&um_pci_mtx);
	for (i = 0; i < MAX_DEVICES; i++) {
		if (um_pci_devices[i].dev)
			continue;
		free = i;
		break;
	}

	if (free < 0)
		goto error;

	err = um_pci_init_vqs(dev);
	if (err)
		goto error;

	dev->irq = irq_alloc_desc(numa_node_id());
	if (dev->irq < 0) {
		err = dev->irq;
		goto error;
	}
	um_pci_devices[free].dev = dev;
	vdev->priv = dev;

	mutex_unlock(&um_pci_mtx);

	device_set_wakeup_enable(&vdev->dev, true);

	/*
	 * In order to do suspend-resume properly, don't allow VQs
	 * to be suspended.
	 */
	virtio_uml_set_no_vq_suspend(vdev, true);

	um_pci_rescan();
	return 0;
error:
	mutex_unlock(&um_pci_mtx);
	kfree(dev);
	return err;
}

static void um_pci_virtio_remove(struct virtio_device *vdev)
{
	struct um_pci_device *dev = vdev->priv;
	int i;

        /* Stop all virtqueues */
        vdev->config->reset(vdev);
        vdev->config->del_vqs(vdev);

	device_set_wakeup_enable(&vdev->dev, false);

	mutex_lock(&um_pci_mtx);
	for (i = 0; i < MAX_DEVICES; i++) {
		if (um_pci_devices[i].dev != dev)
			continue;
		um_pci_devices[i].dev = NULL;
		irq_free_desc(dev->irq);
	}
	mutex_unlock(&um_pci_mtx);

	um_pci_rescan();

	kfree(dev);
}

static struct virtio_device_id id_table[] = {
	{ CONFIG_UML_PCI_OVER_VIRTIO_DEVICE_ID, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver um_pci_virtio_driver = {
	.driver.name = "virtio-pci",
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = um_pci_virtio_probe,
	.remove = um_pci_virtio_remove,
};

static struct resource virt_cfgspace_resource = {
	.name = "PCI config space",
	.start = 0xf0000000 - MAX_DEVICES * CFG_SPACE_SIZE,
	.end = 0xf0000000 - 1,
	.flags = IORESOURCE_MEM,
};

static long um_pci_map_cfgspace(unsigned long offset, size_t size,
				const struct logic_iomem_ops **ops,
				void **priv)
{
	if (WARN_ON(size > CFG_SPACE_SIZE || offset % CFG_SPACE_SIZE))
		return -EINVAL;

	if (offset / CFG_SPACE_SIZE < MAX_DEVICES) {
		*ops = &um_pci_device_cfgspace_ops;
		*priv = &um_pci_devices[offset / CFG_SPACE_SIZE];
		return 0;
	}

	WARN(1, "cannot map offset 0x%lx/0x%zx\n", offset, size);
	return -ENOENT;
}

static const struct logic_iomem_region_ops um_pci_cfgspace_ops = {
	.map = um_pci_map_cfgspace,
};

static struct resource virt_iomem_resource = {
	.name = "PCI iomem",
	.start = 0xf0000000,
	.end = 0xffffffff,
	.flags = IORESOURCE_MEM,
};

struct um_pci_map_iomem_data {
	unsigned long offset;
	size_t size;
	const struct logic_iomem_ops **ops;
	void **priv;
	long ret;
};

static int um_pci_map_iomem_walk(struct pci_dev *pdev, void *_data)
{
	struct um_pci_map_iomem_data *data = _data;
	struct um_pci_device_reg *reg = &um_pci_devices[pdev->devfn / 8];
	struct um_pci_device *dev;
	int i;

	if (!reg->dev)
		return 0;

	for (i = 0; i < ARRAY_SIZE(dev->resptr); i++) {
		struct resource *r = &pdev->resource[i];

		if ((r->flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM)
			continue;

		/*
		 * must be the whole or part of the resource,
		 * not allowed to only overlap
		 */
		if (data->offset < r->start || data->offset > r->end)
			continue;
		if (data->offset + data->size - 1 > r->end)
			continue;

		dev = reg->dev;
		*data->ops = &um_pci_device_bar_ops;
		dev->resptr[i] = i;
		*data->priv = &dev->resptr[i];
		data->ret = data->offset - r->start;

		/* no need to continue */
		return 1;
	}

	return 0;
}

static long um_pci_map_iomem(unsigned long offset, size_t size,
			     const struct logic_iomem_ops **ops,
			     void **priv)
{
	struct um_pci_map_iomem_data data = {
		/* we want the full address here */
		.offset = offset + virt_iomem_resource.start,
		.size = size,
		.ops = ops,
		.priv = priv,
		.ret = -ENOENT,
	};

	pci_walk_bus(bridge->bus, um_pci_map_iomem_walk, &data);
	return data.ret;
}

static const struct logic_iomem_region_ops um_pci_iomem_ops = {
	.map = um_pci_map_iomem,
};

static void um_pci_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	/*
	 * This is a very low address and not actually valid 'physical' memory
	 * in UML, so we can simply map MSI(-X) vectors to there, it cannot be
	 * legitimately written to by the device in any other way.
	 * We use the (virtual) IRQ number here as the message to simplify the
	 * code that receives the message, where for now we simply trust the
	 * device to send the correct message.
	 */
	msg->address_hi = 0;
	msg->address_lo = 0xa0000;
	msg->data = data->irq;
}

static struct irq_chip um_pci_msi_bottom_irq_chip = {
	.name = "UM virtio MSI",
	.irq_compose_msi_msg = um_pci_compose_msi_msg,
};

static int um_pci_inner_domain_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *args)
{
	unsigned long bit;

	WARN_ON(nr_irqs != 1);

	mutex_lock(&um_pci_mtx);
	bit = find_first_zero_bit(um_pci_msi_used, MAX_MSI_VECTORS);
	if (bit >= MAX_MSI_VECTORS) {
		mutex_unlock(&um_pci_mtx);
		return -ENOSPC;
	}

	set_bit(bit, um_pci_msi_used);
	mutex_unlock(&um_pci_mtx);

	irq_domain_set_info(domain, virq, bit, &um_pci_msi_bottom_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);

	return 0;
}

static void um_pci_inner_domain_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	mutex_lock(&um_pci_mtx);

	if (!test_bit(d->hwirq, um_pci_msi_used))
		pr_err("trying to free unused MSI#%lu\n", d->hwirq);
	else
		__clear_bit(d->hwirq, um_pci_msi_used);

	mutex_unlock(&um_pci_mtx);
}

static const struct irq_domain_ops um_pci_inner_domain_ops = {
	.alloc = um_pci_inner_domain_alloc,
	.free = um_pci_inner_domain_free,
};

static struct irq_chip um_pci_msi_irq_chip = {
	.name = "UM virtio PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info um_pci_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS |
		  MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX,
	.chip	= &um_pci_msi_irq_chip,
};

static struct resource busn_resource = {
	.name	= "PCI busn",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUS,
};

static int um_pci_map_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
	struct um_pci_device_reg *reg = &um_pci_devices[pdev->devfn / 8];

	if (WARN_ON(!reg->dev))
		return -EINVAL;

	/* Yes, we map all pins to the same IRQ ... doesn't matter for now. */
	return reg->dev->irq;
}

void *pci_root_bus_fwnode(struct pci_bus *bus)
{
	return um_pci_fwnode;
}

int um_pci_init(void)
{
	int err, i;

	WARN_ON(logic_iomem_add_region(&virt_cfgspace_resource,
				       &um_pci_cfgspace_ops));
	WARN_ON(logic_iomem_add_region(&virt_iomem_resource,
				       &um_pci_iomem_ops));

	if (WARN(CONFIG_UML_PCI_OVER_VIRTIO_DEVICE_ID < 0,
		 "No virtio device ID configured for PCI - no PCI support\n"))
		return 0;

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return -ENOMEM;

	um_pci_fwnode = irq_domain_alloc_named_fwnode("um-pci");
	if (!um_pci_fwnode) {
		err = -ENOMEM;
		goto free;
	}

	um_pci_inner_domain = __irq_domain_add(um_pci_fwnode, MAX_MSI_VECTORS,
					       MAX_MSI_VECTORS, 0,
					       &um_pci_inner_domain_ops, NULL);
	if (!um_pci_inner_domain) {
		err = -ENOMEM;
		goto free;
	}

	um_pci_msi_domain = pci_msi_create_irq_domain(um_pci_fwnode,
						      &um_pci_msi_domain_info,
						      um_pci_inner_domain);
	if (!um_pci_msi_domain) {
		err = -ENOMEM;
		goto free;
	}

	pci_add_resource(&bridge->windows, &virt_iomem_resource);
	pci_add_resource(&bridge->windows, &busn_resource);
	bridge->ops = &um_pci_ops;
	bridge->map_irq = um_pci_map_irq;

	for (i = 0; i < MAX_DEVICES; i++) {
		resource_size_t start;

		start = virt_cfgspace_resource.start + i * CFG_SPACE_SIZE;
		um_pci_devices[i].iomem = ioremap(start, CFG_SPACE_SIZE);
		if (WARN(!um_pci_devices[i].iomem, "failed to map %d\n", i)) {
			err = -ENOMEM;
			goto free;
		}
	}

	err = pci_host_probe(bridge);
	if (err)
		goto free;

	err = register_virtio_driver(&um_pci_virtio_driver);
	if (err)
		goto free;
	return 0;
free:
	if (um_pci_inner_domain)
		irq_domain_remove(um_pci_inner_domain);
	if (um_pci_fwnode)
		irq_domain_free_fwnode(um_pci_fwnode);
	pci_free_resource_list(&bridge->windows);
	pci_free_host_bridge(bridge);
	return err;
}
module_init(um_pci_init);

void um_pci_exit(void)
{
	unregister_virtio_driver(&um_pci_virtio_driver);
	irq_domain_remove(um_pci_msi_domain);
	irq_domain_remove(um_pci_inner_domain);
	pci_free_resource_list(&bridge->windows);
	pci_free_host_bridge(bridge);
}
module_exit(um_pci_exit);
