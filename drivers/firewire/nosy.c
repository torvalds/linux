/*
 * nosy - Snoop mode driver for TI PCILynx 1394 controllers
 * Copyright (C) 2002-2007 Kristian Høgsberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/sched.h> /* required for linux/wait.h */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timex.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <asm/atomic.h>
#include <asm/byteorder.h>

#include "nosy.h"
#include "nosy-user.h"

#define TCODE_PHY_PACKET		0x10
#define PCI_DEVICE_ID_TI_PCILYNX	0x8000

static char driver_name[] = KBUILD_MODNAME;

/* this is the physical layout of a PCL, its size is 128 bytes */
struct pcl {
	__le32 next;
	__le32 async_error_next;
	u32 user_data;
	__le32 pcl_status;
	__le32 remaining_transfer_count;
	__le32 next_data_buffer;
	struct {
		__le32 control;
		__le32 pointer;
	} buffer[13];
};

struct packet {
	unsigned int length;
	char data[0];
};

struct packet_buffer {
	char *data;
	size_t capacity;
	long total_packet_count, lost_packet_count;
	atomic_t size;
	struct packet *head, *tail;
	wait_queue_head_t wait;
};

struct pcilynx {
	struct pci_dev *pci_device;
	__iomem char *registers;

	struct pcl *rcv_start_pcl, *rcv_pcl;
	__le32 *rcv_buffer;

	dma_addr_t rcv_start_pcl_bus, rcv_pcl_bus, rcv_buffer_bus;

	spinlock_t client_list_lock;
	struct list_head client_list;

	struct miscdevice misc;
	struct list_head link;
	struct kref kref;
};

static inline struct pcilynx *
lynx_get(struct pcilynx *lynx)
{
	kref_get(&lynx->kref);

	return lynx;
}

static void
lynx_release(struct kref *kref)
{
	kfree(container_of(kref, struct pcilynx, kref));
}

static inline void
lynx_put(struct pcilynx *lynx)
{
	kref_put(&lynx->kref, lynx_release);
}

struct client {
	struct pcilynx *lynx;
	u32 tcode_mask;
	struct packet_buffer buffer;
	struct list_head link;
};

static DEFINE_MUTEX(card_mutex);
static LIST_HEAD(card_list);

static int
packet_buffer_init(struct packet_buffer *buffer, size_t capacity)
{
	buffer->data = kmalloc(capacity, GFP_KERNEL);
	if (buffer->data == NULL)
		return -ENOMEM;
	buffer->head = (struct packet *) buffer->data;
	buffer->tail = (struct packet *) buffer->data;
	buffer->capacity = capacity;
	buffer->lost_packet_count = 0;
	atomic_set(&buffer->size, 0);
	init_waitqueue_head(&buffer->wait);

	return 0;
}

static void
packet_buffer_destroy(struct packet_buffer *buffer)
{
	kfree(buffer->data);
}

static int
packet_buffer_get(struct client *client, char __user *data, size_t user_length)
{
	struct packet_buffer *buffer = &client->buffer;
	size_t length;
	char *end;

	if (wait_event_interruptible(buffer->wait,
				     atomic_read(&buffer->size) > 0) ||
				     list_empty(&client->lynx->link))
		return -ERESTARTSYS;

	if (atomic_read(&buffer->size) == 0)
		return -ENODEV;

	/* FIXME: Check length <= user_length. */

	end = buffer->data + buffer->capacity;
	length = buffer->head->length;

	if (&buffer->head->data[length] < end) {
		if (copy_to_user(data, buffer->head->data, length))
			return -EFAULT;
		buffer->head = (struct packet *) &buffer->head->data[length];
	} else {
		size_t split = end - buffer->head->data;

		if (copy_to_user(data, buffer->head->data, split))
			return -EFAULT;
		if (copy_to_user(data + split, buffer->data, length - split))
			return -EFAULT;
		buffer->head = (struct packet *) &buffer->data[length - split];
	}

	/*
	 * Decrease buffer->size as the last thing, since this is what
	 * keeps the interrupt from overwriting the packet we are
	 * retrieving from the buffer.
	 */
	atomic_sub(sizeof(struct packet) + length, &buffer->size);

	return length;
}

static void
packet_buffer_put(struct packet_buffer *buffer, void *data, size_t length)
{
	char *end;

	buffer->total_packet_count++;

	if (buffer->capacity <
	    atomic_read(&buffer->size) + sizeof(struct packet) + length) {
		buffer->lost_packet_count++;
		return;
	}

	end = buffer->data + buffer->capacity;
	buffer->tail->length = length;

	if (&buffer->tail->data[length] < end) {
		memcpy(buffer->tail->data, data, length);
		buffer->tail = (struct packet *) &buffer->tail->data[length];
	} else {
		size_t split = end - buffer->tail->data;

		memcpy(buffer->tail->data, data, split);
		memcpy(buffer->data, data + split, length - split);
		buffer->tail = (struct packet *) &buffer->data[length - split];
	}

	/* Finally, adjust buffer size and wake up userspace reader. */

	atomic_add(sizeof(struct packet) + length, &buffer->size);
	wake_up_interruptible(&buffer->wait);
}

static inline void
reg_write(struct pcilynx *lynx, int offset, u32 data)
{
	writel(data, lynx->registers + offset);
}

static inline u32
reg_read(struct pcilynx *lynx, int offset)
{
	return readl(lynx->registers + offset);
}

static inline void
reg_set_bits(struct pcilynx *lynx, int offset, u32 mask)
{
	reg_write(lynx, offset, (reg_read(lynx, offset) | mask));
}

/*
 * Maybe the pcl programs could be set up to just append data instead
 * of using a whole packet.
 */
static inline void
run_pcl(struct pcilynx *lynx, dma_addr_t pcl_bus,
			   int dmachan)
{
	reg_write(lynx, DMA0_CURRENT_PCL + dmachan * 0x20, pcl_bus);
	reg_write(lynx, DMA0_CHAN_CTRL + dmachan * 0x20,
		  DMA_CHAN_CTRL_ENABLE | DMA_CHAN_CTRL_LINK);
}

static int
set_phy_reg(struct pcilynx *lynx, int addr, int val)
{
	if (addr > 15) {
		dev_err(&lynx->pci_device->dev,
			"PHY register address %d out of range\n", addr);
		return -1;
	}
	if (val > 0xff) {
		dev_err(&lynx->pci_device->dev,
			"PHY register value %d out of range\n", val);
		return -1;
	}
	reg_write(lynx, LINK_PHY, LINK_PHY_WRITE |
		  LINK_PHY_ADDR(addr) | LINK_PHY_WDATA(val));

	return 0;
}

static int
nosy_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct client *client;
	struct pcilynx *tmp, *lynx = NULL;

	mutex_lock(&card_mutex);
	list_for_each_entry(tmp, &card_list, link)
		if (tmp->misc.minor == minor) {
			lynx = lynx_get(tmp);
			break;
		}
	mutex_unlock(&card_mutex);
	if (lynx == NULL)
		return -ENODEV;

	client = kmalloc(sizeof *client, GFP_KERNEL);
	if (client == NULL)
		goto fail;

	client->tcode_mask = ~0;
	client->lynx = lynx;
	INIT_LIST_HEAD(&client->link);

	if (packet_buffer_init(&client->buffer, 128 * 1024) < 0)
		goto fail;

	file->private_data = client;

	return 0;
fail:
	kfree(client);
	lynx_put(lynx);

	return -ENOMEM;
}

static int
nosy_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct pcilynx *lynx = client->lynx;

	spin_lock_irq(&lynx->client_list_lock);
	list_del_init(&client->link);
	spin_unlock_irq(&lynx->client_list_lock);

	packet_buffer_destroy(&client->buffer);
	kfree(client);
	lynx_put(lynx);

	return 0;
}

static unsigned int
nosy_poll(struct file *file, poll_table *pt)
{
	struct client *client = file->private_data;
	unsigned int ret = 0;

	poll_wait(file, &client->buffer.wait, pt);

	if (atomic_read(&client->buffer.size) > 0)
		ret = POLLIN | POLLRDNORM;

	if (list_empty(&client->lynx->link))
		ret |= POLLHUP;

	return ret;
}

static ssize_t
nosy_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
	struct client *client = file->private_data;

	return packet_buffer_get(client, buffer, count);
}

static long
nosy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;
	spinlock_t *client_list_lock = &client->lynx->client_list_lock;
	struct nosy_stats stats;

	switch (cmd) {
	case NOSY_IOC_GET_STATS:
		spin_lock_irq(client_list_lock);
		stats.total_packet_count = client->buffer.total_packet_count;
		stats.lost_packet_count  = client->buffer.lost_packet_count;
		spin_unlock_irq(client_list_lock);

		if (copy_to_user((void __user *) arg, &stats, sizeof stats))
			return -EFAULT;
		else
			return 0;

	case NOSY_IOC_START:
		spin_lock_irq(client_list_lock);
		list_add_tail(&client->link, &client->lynx->client_list);
		spin_unlock_irq(client_list_lock);

		return 0;

	case NOSY_IOC_STOP:
		spin_lock_irq(client_list_lock);
		list_del_init(&client->link);
		spin_unlock_irq(client_list_lock);

		return 0;

	case NOSY_IOC_FILTER:
		spin_lock_irq(client_list_lock);
		client->tcode_mask = arg;
		spin_unlock_irq(client_list_lock);

		return 0;

	default:
		return -EINVAL;
		/* Flush buffer, configure filter. */
	}
}

static const struct file_operations nosy_ops = {
	.owner =		THIS_MODULE,
	.read =			nosy_read,
	.unlocked_ioctl =	nosy_ioctl,
	.poll =			nosy_poll,
	.open =			nosy_open,
	.release =		nosy_release,
};

#define PHY_PACKET_SIZE 12 /* 1 payload, 1 inverse, 1 ack = 3 quadlets */

static void
packet_irq_handler(struct pcilynx *lynx)
{
	struct client *client;
	u32 tcode_mask, tcode;
	size_t length;
	struct timeval tv;

	/* FIXME: Also report rcv_speed. */

	length = __le32_to_cpu(lynx->rcv_pcl->pcl_status) & 0x00001fff;
	tcode  = __le32_to_cpu(lynx->rcv_buffer[1]) >> 4 & 0xf;

	do_gettimeofday(&tv);
	lynx->rcv_buffer[0] = (__force __le32)tv.tv_usec;

	if (length == PHY_PACKET_SIZE)
		tcode_mask = 1 << TCODE_PHY_PACKET;
	else
		tcode_mask = 1 << tcode;

	spin_lock(&lynx->client_list_lock);

	list_for_each_entry(client, &lynx->client_list, link)
		if (client->tcode_mask & tcode_mask)
			packet_buffer_put(&client->buffer,
					  lynx->rcv_buffer, length + 4);

	spin_unlock(&lynx->client_list_lock);
}

static void
bus_reset_irq_handler(struct pcilynx *lynx)
{
	struct client *client;
	struct timeval tv;

	do_gettimeofday(&tv);

	spin_lock(&lynx->client_list_lock);

	list_for_each_entry(client, &lynx->client_list, link)
		packet_buffer_put(&client->buffer, &tv.tv_usec, 4);

	spin_unlock(&lynx->client_list_lock);
}

static irqreturn_t
irq_handler(int irq, void *device)
{
	struct pcilynx *lynx = device;
	u32 pci_int_status;

	pci_int_status = reg_read(lynx, PCI_INT_STATUS);

	if (pci_int_status == ~0)
		/* Card was ejected. */
		return IRQ_NONE;

	if ((pci_int_status & PCI_INT_INT_PEND) == 0)
		/* Not our interrupt, bail out quickly. */
		return IRQ_NONE;

	if ((pci_int_status & PCI_INT_P1394_INT) != 0) {
		u32 link_int_status;

		link_int_status = reg_read(lynx, LINK_INT_STATUS);
		reg_write(lynx, LINK_INT_STATUS, link_int_status);

		if ((link_int_status & LINK_INT_PHY_BUSRESET) > 0)
			bus_reset_irq_handler(lynx);
	}

	/* Clear the PCI_INT_STATUS register only after clearing the
	 * LINK_INT_STATUS register; otherwise the PCI_INT_P1394 will
	 * be set again immediately. */

	reg_write(lynx, PCI_INT_STATUS, pci_int_status);

	if ((pci_int_status & PCI_INT_DMA0_HLT) > 0) {
		packet_irq_handler(lynx);
		run_pcl(lynx, lynx->rcv_start_pcl_bus, 0);
	}

	return IRQ_HANDLED;
}

static void
remove_card(struct pci_dev *dev)
{
	struct pcilynx *lynx = pci_get_drvdata(dev);
	struct client *client;

	mutex_lock(&card_mutex);
	list_del_init(&lynx->link);
	misc_deregister(&lynx->misc);
	mutex_unlock(&card_mutex);

	reg_write(lynx, PCI_INT_ENABLE, 0);
	free_irq(lynx->pci_device->irq, lynx);

	spin_lock_irq(&lynx->client_list_lock);
	list_for_each_entry(client, &lynx->client_list, link)
		wake_up_interruptible(&client->buffer.wait);
	spin_unlock_irq(&lynx->client_list_lock);

	pci_free_consistent(lynx->pci_device, sizeof(struct pcl),
			    lynx->rcv_start_pcl, lynx->rcv_start_pcl_bus);
	pci_free_consistent(lynx->pci_device, sizeof(struct pcl),
			    lynx->rcv_pcl, lynx->rcv_pcl_bus);
	pci_free_consistent(lynx->pci_device, PAGE_SIZE,
			    lynx->rcv_buffer, lynx->rcv_buffer_bus);

	iounmap(lynx->registers);
	pci_disable_device(dev);
	lynx_put(lynx);
}

#define RCV_BUFFER_SIZE (16 * 1024)

static int __devinit
add_card(struct pci_dev *dev, const struct pci_device_id *unused)
{
	struct pcilynx *lynx;
	u32 p, end;
	int ret, i;

	if (pci_set_dma_mask(dev, 0xffffffff)) {
		dev_err(&dev->dev,
		    "DMA address limits not supported for PCILynx hardware\n");
		return -ENXIO;
	}
	if (pci_enable_device(dev)) {
		dev_err(&dev->dev, "Failed to enable PCILynx hardware\n");
		return -ENXIO;
	}
	pci_set_master(dev);

	lynx = kzalloc(sizeof *lynx, GFP_KERNEL);
	if (lynx == NULL) {
		dev_err(&dev->dev, "Failed to allocate control structure\n");
		ret = -ENOMEM;
		goto fail_disable;
	}
	lynx->pci_device = dev;
	pci_set_drvdata(dev, lynx);

	spin_lock_init(&lynx->client_list_lock);
	INIT_LIST_HEAD(&lynx->client_list);
	kref_init(&lynx->kref);

	lynx->registers = ioremap_nocache(pci_resource_start(dev, 0),
					  PCILYNX_MAX_REGISTER);

	lynx->rcv_start_pcl = pci_alloc_consistent(lynx->pci_device,
				sizeof(struct pcl), &lynx->rcv_start_pcl_bus);
	lynx->rcv_pcl = pci_alloc_consistent(lynx->pci_device,
				sizeof(struct pcl), &lynx->rcv_pcl_bus);
	lynx->rcv_buffer = pci_alloc_consistent(lynx->pci_device,
				RCV_BUFFER_SIZE, &lynx->rcv_buffer_bus);
	if (lynx->rcv_start_pcl == NULL ||
	    lynx->rcv_pcl == NULL ||
	    lynx->rcv_buffer == NULL) {
		dev_err(&dev->dev, "Failed to allocate receive buffer\n");
		ret = -ENOMEM;
		goto fail_deallocate;
	}
	lynx->rcv_start_pcl->next	= cpu_to_le32(lynx->rcv_pcl_bus);
	lynx->rcv_pcl->next		= cpu_to_le32(PCL_NEXT_INVALID);
	lynx->rcv_pcl->async_error_next	= cpu_to_le32(PCL_NEXT_INVALID);

	lynx->rcv_pcl->buffer[0].control =
			cpu_to_le32(PCL_CMD_RCV | PCL_BIGENDIAN | 2044);
	lynx->rcv_pcl->buffer[0].pointer =
			cpu_to_le32(lynx->rcv_buffer_bus + 4);
	p = lynx->rcv_buffer_bus + 2048;
	end = lynx->rcv_buffer_bus + RCV_BUFFER_SIZE;
	for (i = 1; p < end; i++, p += 2048) {
		lynx->rcv_pcl->buffer[i].control =
			cpu_to_le32(PCL_CMD_RCV | PCL_BIGENDIAN | 2048);
		lynx->rcv_pcl->buffer[i].pointer = cpu_to_le32(p);
	}
	lynx->rcv_pcl->buffer[i - 1].control |= cpu_to_le32(PCL_LAST_BUFF);

	reg_set_bits(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);
	/* Fix buggy cards with autoboot pin not tied low: */
	reg_write(lynx, DMA0_CHAN_CTRL, 0);
	reg_write(lynx, DMA_GLOBAL_REGISTER, 0x00 << 24);

#if 0
	/* now, looking for PHY register set */
	if ((get_phy_reg(lynx, 2) & 0xe0) == 0xe0) {
		lynx->phyic.reg_1394a = 1;
		PRINT(KERN_INFO, lynx->id,
		      "found 1394a conform PHY (using extended register set)");
		lynx->phyic.vendor = get_phy_vendorid(lynx);
		lynx->phyic.product = get_phy_productid(lynx);
	} else {
		lynx->phyic.reg_1394a = 0;
		PRINT(KERN_INFO, lynx->id, "found old 1394 PHY");
	}
#endif

	/* Setup the general receive FIFO max size. */
	reg_write(lynx, FIFO_SIZES, 255);

	reg_set_bits(lynx, PCI_INT_ENABLE, PCI_INT_DMA_ALL);

	reg_write(lynx, LINK_INT_ENABLE,
		  LINK_INT_PHY_TIME_OUT | LINK_INT_PHY_REG_RCVD |
		  LINK_INT_PHY_BUSRESET | LINK_INT_IT_STUCK |
		  LINK_INT_AT_STUCK | LINK_INT_SNTRJ |
		  LINK_INT_TC_ERR | LINK_INT_GRF_OVER_FLOW |
		  LINK_INT_ITF_UNDER_FLOW | LINK_INT_ATF_UNDER_FLOW);

	/* Disable the L flag in self ID packets. */
	set_phy_reg(lynx, 4, 0);

	/* Put this baby into snoop mode */
	reg_set_bits(lynx, LINK_CONTROL, LINK_CONTROL_SNOOP_ENABLE);

	run_pcl(lynx, lynx->rcv_start_pcl_bus, 0);

	if (request_irq(dev->irq, irq_handler, IRQF_SHARED,
			driver_name, lynx)) {
		dev_err(&dev->dev,
			"Failed to allocate shared interrupt %d\n", dev->irq);
		ret = -EIO;
		goto fail_deallocate;
	}

	lynx->misc.parent = &dev->dev;
	lynx->misc.minor = MISC_DYNAMIC_MINOR;
	lynx->misc.name = "nosy";
	lynx->misc.fops = &nosy_ops;

	mutex_lock(&card_mutex);
	ret = misc_register(&lynx->misc);
	if (ret) {
		dev_err(&dev->dev, "Failed to register misc char device\n");
		mutex_unlock(&card_mutex);
		goto fail_free_irq;
	}
	list_add_tail(&lynx->link, &card_list);
	mutex_unlock(&card_mutex);

	dev_info(&dev->dev,
		 "Initialized PCILynx IEEE1394 card, irq=%d\n", dev->irq);

	return 0;

fail_free_irq:
	reg_write(lynx, PCI_INT_ENABLE, 0);
	free_irq(lynx->pci_device->irq, lynx);

fail_deallocate:
	if (lynx->rcv_start_pcl)
		pci_free_consistent(lynx->pci_device, sizeof(struct pcl),
				lynx->rcv_start_pcl, lynx->rcv_start_pcl_bus);
	if (lynx->rcv_pcl)
		pci_free_consistent(lynx->pci_device, sizeof(struct pcl),
				lynx->rcv_pcl, lynx->rcv_pcl_bus);
	if (lynx->rcv_buffer)
		pci_free_consistent(lynx->pci_device, PAGE_SIZE,
				lynx->rcv_buffer, lynx->rcv_buffer_bus);
	iounmap(lynx->registers);
	kfree(lynx);

fail_disable:
	pci_disable_device(dev);

	return ret;
}

static struct pci_device_id pci_table[] __devinitdata = {
	{
		.vendor =    PCI_VENDOR_ID_TI,
		.device =    PCI_DEVICE_ID_TI_PCILYNX,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{ }	/* Terminating entry */
};

static struct pci_driver lynx_pci_driver = {
	.name =		driver_name,
	.id_table =	pci_table,
	.probe =	add_card,
	.remove =	remove_card,
};

MODULE_AUTHOR("Kristian Hoegsberg");
MODULE_DESCRIPTION("Snoop mode driver for TI pcilynx 1394 controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pci_table);

static int __init nosy_init(void)
{
	return pci_register_driver(&lynx_pci_driver);
}

static void __exit nosy_cleanup(void)
{
	pci_unregister_driver(&lynx_pci_driver);

	pr_info("Unloaded %s\n", driver_name);
}

module_init(nosy_init);
module_exit(nosy_cleanup);
