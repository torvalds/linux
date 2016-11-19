/*
 *  PS3 device registration routines.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/reboot.h>

#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/ps3stor.h>

#include "platform.h"

static int __init ps3_register_lpm_devices(void)
{
	int result;
	u64 tmp1;
	u64 tmp2;
	struct ps3_system_bus_device *dev;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->match_id = PS3_MATCH_ID_LPM;
	dev->dev_type = PS3_DEVICE_TYPE_LPM;

	/* The current lpm driver only supports a single BE processor. */

	result = ps3_repository_read_be_node_id(0, &dev->lpm.node_id);

	if (result) {
		pr_debug("%s:%d: ps3_repository_read_be_node_id failed \n",
			__func__, __LINE__);
		goto fail_read_repo;
	}

	result = ps3_repository_read_lpm_privileges(dev->lpm.node_id, &tmp1,
		&dev->lpm.rights);

	if (result) {
		pr_debug("%s:%d: ps3_repository_read_lpm_privileges failed\n",
			__func__, __LINE__);
		goto fail_read_repo;
	}

	lv1_get_logical_partition_id(&tmp2);

	if (tmp1 != tmp2) {
		pr_debug("%s:%d: wrong lpar\n",
			__func__, __LINE__);
		result = -ENODEV;
		goto fail_rights;
	}

	if (!(dev->lpm.rights & PS3_LPM_RIGHTS_USE_LPM)) {
		pr_debug("%s:%d: don't have rights to use lpm\n",
			__func__, __LINE__);
		result = -EPERM;
		goto fail_rights;
	}

	pr_debug("%s:%d: pu_id %llu, rights %llu(%llxh)\n",
		__func__, __LINE__, dev->lpm.pu_id, dev->lpm.rights,
		dev->lpm.rights);

	result = ps3_repository_read_pu_id(0, &dev->lpm.pu_id);

	if (result) {
		pr_debug("%s:%d: ps3_repository_read_pu_id failed \n",
			__func__, __LINE__);
		goto fail_read_repo;
	}

	result = ps3_system_bus_device_register(dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_register;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;


fail_register:
fail_rights:
fail_read_repo:
	kfree(dev);
	pr_debug(" <- %s:%d: failed\n", __func__, __LINE__);
	return result;
}

/**
 * ps3_setup_gelic_device - Setup and register a gelic device instance.
 *
 * Allocates memory for a struct ps3_system_bus_device instance, initialises the
 * structure members, and registers the device instance with the system bus.
 */

static int __init ps3_setup_gelic_device(
	const struct ps3_repository_device *repo)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	BUG_ON(repo->bus_type != PS3_BUS_TYPE_SB);
	BUG_ON(repo->dev_type != PS3_DEV_TYPE_SB_GELIC);

	p = kzalloc(sizeof(struct layout), GFP_KERNEL);

	if (!p) {
		result = -ENOMEM;
		goto fail_malloc;
	}

	p->dev.match_id = PS3_MATCH_ID_GELIC;
	p->dev.dev_type = PS3_DEVICE_TYPE_SB;
	p->dev.bus_id = repo->bus_id;
	p->dev.dev_id = repo->dev_id;
	p->dev.d_region = &p->d_region;

	result = ps3_repository_find_interrupt(repo,
		PS3_INTERRUPT_TYPE_EVENT_PORT, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail_find_interrupt;
	}

	BUG_ON(p->dev.interrupt_id != 0);

	result = ps3_dma_region_init(&p->dev, p->dev.d_region, PS3_DMA_64K,
		PS3_DMA_OTHER, NULL, 0);

	if (result) {
		pr_debug("%s:%d ps3_dma_region_init failed\n",
			__func__, __LINE__);
		goto fail_dma_init;
	}

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail_device_register:
fail_dma_init:
fail_find_interrupt:
	kfree(p);
fail_malloc:
	pr_debug(" <- %s:%d: fail.\n", __func__, __LINE__);
	return result;
}

static int __ref ps3_setup_uhc_device(
	const struct ps3_repository_device *repo, enum ps3_match_id match_id,
	enum ps3_interrupt_type interrupt_type, enum ps3_reg_type reg_type)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;
	u64 bus_addr;
	u64 len;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	BUG_ON(repo->bus_type != PS3_BUS_TYPE_SB);
	BUG_ON(repo->dev_type != PS3_DEV_TYPE_SB_USB);

	p = kzalloc(sizeof(struct layout), GFP_KERNEL);

	if (!p) {
		result = -ENOMEM;
		goto fail_malloc;
	}

	p->dev.match_id = match_id;
	p->dev.dev_type = PS3_DEVICE_TYPE_SB;
	p->dev.bus_id = repo->bus_id;
	p->dev.dev_id = repo->dev_id;
	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;

	result = ps3_repository_find_interrupt(repo,
		interrupt_type, &p->dev.interrupt_id);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_interrupt failed\n",
			__func__, __LINE__);
		goto fail_find_interrupt;
	}

	result = ps3_repository_find_reg(repo, reg_type,
		&bus_addr, &len);

	if (result) {
		pr_debug("%s:%d ps3_repository_find_reg failed\n",
			__func__, __LINE__);
		goto fail_find_reg;
	}

	result = ps3_dma_region_init(&p->dev, p->dev.d_region, PS3_DMA_64K,
		PS3_DMA_INTERNAL, NULL, 0);

	if (result) {
		pr_debug("%s:%d ps3_dma_region_init failed\n",
			__func__, __LINE__);
		goto fail_dma_init;
	}

	result = ps3_mmio_region_init(&p->dev, p->dev.m_region, bus_addr, len,
		PS3_MMIO_4K);

	if (result) {
		pr_debug("%s:%d ps3_mmio_region_init failed\n",
			__func__, __LINE__);
		goto fail_mmio_init;
	}

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;

fail_device_register:
fail_mmio_init:
fail_dma_init:
fail_find_reg:
fail_find_interrupt:
	kfree(p);
fail_malloc:
	pr_debug(" <- %s:%d: fail.\n", __func__, __LINE__);
	return result;
}

static int __init ps3_setup_ehci_device(
	const struct ps3_repository_device *repo)
{
	return ps3_setup_uhc_device(repo, PS3_MATCH_ID_EHCI,
		PS3_INTERRUPT_TYPE_SB_EHCI, PS3_REG_TYPE_SB_EHCI);
}

static int __init ps3_setup_ohci_device(
	const struct ps3_repository_device *repo)
{
	return ps3_setup_uhc_device(repo, PS3_MATCH_ID_OHCI,
		PS3_INTERRUPT_TYPE_SB_OHCI, PS3_REG_TYPE_SB_OHCI);
}

static int __init ps3_setup_vuart_device(enum ps3_match_id match_id,
	unsigned int port_number)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
	} *p;

	pr_debug(" -> %s:%d: match_id %u, port %u\n", __func__, __LINE__,
		match_id, port_number);

	p = kzalloc(sizeof(struct layout), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	p->dev.match_id = match_id;
	p->dev.dev_type = PS3_DEVICE_TYPE_VUART;
	p->dev.port_number = port_number;

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;

fail_device_register:
	kfree(p);
	pr_debug(" <- %s:%d fail\n", __func__, __LINE__);
	return result;
}

static int ps3_setup_storage_dev(const struct ps3_repository_device *repo,
				 enum ps3_match_id match_id)
{
	int result;
	struct ps3_storage_device *p;
	u64 port, blk_size, num_blocks;
	unsigned int num_regions, i;

	pr_debug(" -> %s:%u: match_id %u\n", __func__, __LINE__, match_id);

	result = ps3_repository_read_stor_dev_info(repo->bus_index,
						   repo->dev_index, &port,
						   &blk_size, &num_blocks,
						   &num_regions);
	if (result) {
		printk(KERN_ERR "%s:%u: _read_stor_dev_info failed %d\n",
		       __func__, __LINE__, result);
		return -ENODEV;
	}

	pr_debug("%s:%u: (%u:%u:%u): port %llu blk_size %llu num_blocks %llu "
		 "num_regions %u\n", __func__, __LINE__, repo->bus_index,
		 repo->dev_index, repo->dev_type, port, blk_size, num_blocks,
		 num_regions);

	p = kzalloc(sizeof(struct ps3_storage_device) +
		    num_regions * sizeof(struct ps3_storage_region),
		    GFP_KERNEL);
	if (!p) {
		result = -ENOMEM;
		goto fail_malloc;
	}

	p->sbd.match_id = match_id;
	p->sbd.dev_type = PS3_DEVICE_TYPE_SB;
	p->sbd.bus_id = repo->bus_id;
	p->sbd.dev_id = repo->dev_id;
	p->sbd.d_region = &p->dma_region;
	p->blk_size = blk_size;
	p->num_regions = num_regions;

	result = ps3_repository_find_interrupt(repo,
					       PS3_INTERRUPT_TYPE_EVENT_PORT,
					       &p->sbd.interrupt_id);
	if (result) {
		printk(KERN_ERR "%s:%u: find_interrupt failed %d\n", __func__,
		       __LINE__, result);
		result = -ENODEV;
		goto fail_find_interrupt;
	}

	for (i = 0; i < num_regions; i++) {
		unsigned int id;
		u64 start, size;

		result = ps3_repository_read_stor_dev_region(repo->bus_index,
							     repo->dev_index,
							     i, &id, &start,
							     &size);
		if (result) {
			printk(KERN_ERR
			       "%s:%u: read_stor_dev_region failed %d\n",
			       __func__, __LINE__, result);
			result = -ENODEV;
			goto fail_read_region;
		}
		pr_debug("%s:%u: region %u: id %u start %llu size %llu\n",
			 __func__, __LINE__, i, id, start, size);

		p->regions[i].id = id;
		p->regions[i].start = start;
		p->regions[i].size = size;
	}

	result = ps3_system_bus_device_register(&p->sbd);
	if (result) {
		pr_debug("%s:%u ps3_system_bus_device_register failed\n",
			 __func__, __LINE__);
		goto fail_device_register;
	}

	pr_debug(" <- %s:%u\n", __func__, __LINE__);
	return 0;

fail_device_register:
fail_read_region:
fail_find_interrupt:
	kfree(p);
fail_malloc:
	pr_debug(" <- %s:%u: fail.\n", __func__, __LINE__);
	return result;
}

static int __init ps3_register_vuart_devices(void)
{
	int result;
	unsigned int port_number;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	result = ps3_repository_read_vuart_av_port(&port_number);
	if (result)
		port_number = 0; /* av default */

	result = ps3_setup_vuart_device(PS3_MATCH_ID_AV_SETTINGS, port_number);
	WARN_ON(result);

	result = ps3_repository_read_vuart_sysmgr_port(&port_number);
	if (result)
		port_number = 2; /* sysmgr default */

	result = ps3_setup_vuart_device(PS3_MATCH_ID_SYSTEM_MANAGER,
		port_number);
	WARN_ON(result);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int __init ps3_register_sound_devices(void)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
		struct ps3_dma_region d_region;
		struct ps3_mmio_region m_region;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev.match_id = PS3_MATCH_ID_SOUND;
	p->dev.dev_type = PS3_DEVICE_TYPE_IOC0;
	p->dev.d_region = &p->d_region;
	p->dev.m_region = &p->m_region;

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;

fail_device_register:
	kfree(p);
	pr_debug(" <- %s:%d failed\n", __func__, __LINE__);
	return result;
}

static int __init ps3_register_graphics_devices(void)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct layout), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	p->dev.match_id = PS3_MATCH_ID_GPU;
	p->dev.match_sub_id = PS3_MATCH_SUB_ID_GPU_FB;
	p->dev.dev_type = PS3_DEVICE_TYPE_IOC0;

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;

fail_device_register:
	kfree(p);
	pr_debug(" <- %s:%d failed\n", __func__, __LINE__);
	return result;
}

static int __init ps3_register_ramdisk_device(void)
{
	int result;
	struct layout {
		struct ps3_system_bus_device dev;
	} *p;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	p = kzalloc(sizeof(struct layout), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	p->dev.match_id = PS3_MATCH_ID_GPU;
	p->dev.match_sub_id = PS3_MATCH_SUB_ID_GPU_RAMDISK;
	p->dev.dev_type = PS3_DEVICE_TYPE_IOC0;

	result = ps3_system_bus_device_register(&p->dev);

	if (result) {
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);
		goto fail_device_register;
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;

fail_device_register:
	kfree(p);
	pr_debug(" <- %s:%d failed\n", __func__, __LINE__);
	return result;
}

/**
 * ps3_setup_dynamic_device - Setup a dynamic device from the repository
 */

static int ps3_setup_dynamic_device(const struct ps3_repository_device *repo)
{
	int result;

	switch (repo->dev_type) {
	case PS3_DEV_TYPE_STOR_DISK:
		result = ps3_setup_storage_dev(repo, PS3_MATCH_ID_STOR_DISK);

		/* Some devices are not accessible from the Other OS lpar. */
		if (result == -ENODEV) {
			result = 0;
			pr_debug("%s:%u: not accessible\n", __func__,
				 __LINE__);
		}

		if (result)
			pr_debug("%s:%u ps3_setup_storage_dev failed\n",
				 __func__, __LINE__);
		break;

	case PS3_DEV_TYPE_STOR_ROM:
		result = ps3_setup_storage_dev(repo, PS3_MATCH_ID_STOR_ROM);
		if (result)
			pr_debug("%s:%u ps3_setup_storage_dev failed\n",
				 __func__, __LINE__);
		break;

	case PS3_DEV_TYPE_STOR_FLASH:
		result = ps3_setup_storage_dev(repo, PS3_MATCH_ID_STOR_FLASH);
		if (result)
			pr_debug("%s:%u ps3_setup_storage_dev failed\n",
				 __func__, __LINE__);
		break;

	default:
		result = 0;
		pr_debug("%s:%u: unsupported dev_type %u\n", __func__, __LINE__,
			repo->dev_type);
	}

	return result;
}

/**
 * ps3_setup_static_device - Setup a static device from the repository
 */

static int __init ps3_setup_static_device(const struct ps3_repository_device *repo)
{
	int result;

	switch (repo->dev_type) {
	case PS3_DEV_TYPE_SB_GELIC:
		result = ps3_setup_gelic_device(repo);
		if (result) {
			pr_debug("%s:%d ps3_setup_gelic_device failed\n",
				__func__, __LINE__);
		}
		break;
	case PS3_DEV_TYPE_SB_USB:

		/* Each USB device has both an EHCI and an OHCI HC */

		result = ps3_setup_ehci_device(repo);

		if (result) {
			pr_debug("%s:%d ps3_setup_ehci_device failed\n",
				__func__, __LINE__);
		}

		result = ps3_setup_ohci_device(repo);

		if (result) {
			pr_debug("%s:%d ps3_setup_ohci_device failed\n",
				__func__, __LINE__);
		}
		break;

	default:
		return ps3_setup_dynamic_device(repo);
	}

	return result;
}

static void ps3_find_and_add_device(u64 bus_id, u64 dev_id)
{
	struct ps3_repository_device repo;
	int res;
	unsigned int retries;
	unsigned long rem;

	/*
	 * On some firmware versions (e.g. 1.90), the device may not show up
	 * in the repository immediately
	 */
	for (retries = 0; retries < 10; retries++) {
		res = ps3_repository_find_device_by_id(&repo, bus_id, dev_id);
		if (!res)
			goto found;

		rem = msleep_interruptible(100);
		if (rem)
			break;
	}
	pr_warning("%s:%u: device %llu:%llu not found\n", __func__, __LINE__,
		   bus_id, dev_id);
	return;

found:
	if (retries)
		pr_debug("%s:%u: device %llu:%llu found after %u retries\n",
			 __func__, __LINE__, bus_id, dev_id, retries);

	ps3_setup_dynamic_device(&repo);
	return;
}

#define PS3_NOTIFICATION_DEV_ID		ULONG_MAX
#define PS3_NOTIFICATION_INTERRUPT_ID	0

struct ps3_notification_device {
	struct ps3_system_bus_device sbd;
	spinlock_t lock;
	u64 tag;
	u64 lv1_status;
	struct completion done;
};

enum ps3_notify_type {
	notify_device_ready = 0,
	notify_region_probe = 1,
	notify_region_update = 2,
};

struct ps3_notify_cmd {
	u64 operation_code;		/* must be zero */
	u64 event_mask;			/* OR of 1UL << enum ps3_notify_type */
};

struct ps3_notify_event {
	u64 event_type;			/* enum ps3_notify_type */
	u64 bus_id;
	u64 dev_id;
	u64 dev_type;
	u64 dev_port;
};

static irqreturn_t ps3_notification_interrupt(int irq, void *data)
{
	struct ps3_notification_device *dev = data;
	int res;
	u64 tag, status;

	spin_lock(&dev->lock);
	res = lv1_storage_get_async_status(PS3_NOTIFICATION_DEV_ID, &tag,
					   &status);
	if (tag != dev->tag)
		pr_err("%s:%u: tag mismatch, got %llx, expected %llx\n",
		       __func__, __LINE__, tag, dev->tag);

	if (res) {
		pr_err("%s:%u: res %d status 0x%llx\n", __func__, __LINE__, res,
		       status);
	} else {
		pr_debug("%s:%u: completed, status 0x%llx\n", __func__,
			 __LINE__, status);
		dev->lv1_status = status;
		complete(&dev->done);
	}
	spin_unlock(&dev->lock);
	return IRQ_HANDLED;
}

static int ps3_notification_read_write(struct ps3_notification_device *dev,
				       u64 lpar, int write)
{
	const char *op = write ? "write" : "read";
	unsigned long flags;
	int res;

	init_completion(&dev->done);
	spin_lock_irqsave(&dev->lock, flags);
	res = write ? lv1_storage_write(dev->sbd.dev_id, 0, 0, 1, 0, lpar,
					&dev->tag)
		    : lv1_storage_read(dev->sbd.dev_id, 0, 0, 1, 0, lpar,
				       &dev->tag);
	spin_unlock_irqrestore(&dev->lock, flags);
	if (res) {
		pr_err("%s:%u: %s failed %d\n", __func__, __LINE__, op, res);
		return -EPERM;
	}
	pr_debug("%s:%u: notification %s issued\n", __func__, __LINE__, op);

	res = wait_event_interruptible(dev->done.wait,
				       dev->done.done || kthread_should_stop());
	if (kthread_should_stop())
		res = -EINTR;
	if (res) {
		pr_debug("%s:%u: interrupted %s\n", __func__, __LINE__, op);
		return res;
	}

	if (dev->lv1_status) {
		pr_err("%s:%u: %s not completed, status 0x%llx\n", __func__,
		       __LINE__, op, dev->lv1_status);
		return -EIO;
	}
	pr_debug("%s:%u: notification %s completed\n", __func__, __LINE__, op);

	return 0;
}

static struct task_struct *probe_task;

/**
 * ps3_probe_thread - Background repository probing at system startup.
 *
 * This implementation only supports background probing on a single bus.
 * It uses the hypervisor's storage device notification mechanism to wait until
 * a storage device is ready.  The device notification mechanism uses a
 * pseudo device to asynchronously notify the guest when storage devices become
 * ready.  The notification device has a block size of 512 bytes.
 */

static int ps3_probe_thread(void *data)
{
	struct ps3_notification_device dev;
	int res;
	unsigned int irq;
	u64 lpar;
	void *buf;
	struct ps3_notify_cmd *notify_cmd;
	struct ps3_notify_event *notify_event;

	pr_debug(" -> %s:%u: kthread started\n", __func__, __LINE__);

	buf = kzalloc(512, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	lpar = ps3_mm_phys_to_lpar(__pa(buf));
	notify_cmd = buf;
	notify_event = buf;

	/* dummy system bus device */
	dev.sbd.bus_id = (u64)data;
	dev.sbd.dev_id = PS3_NOTIFICATION_DEV_ID;
	dev.sbd.interrupt_id = PS3_NOTIFICATION_INTERRUPT_ID;

	res = lv1_open_device(dev.sbd.bus_id, dev.sbd.dev_id, 0);
	if (res) {
		pr_err("%s:%u: lv1_open_device failed %s\n", __func__,
		       __LINE__, ps3_result(res));
		goto fail_free;
	}

	res = ps3_sb_event_receive_port_setup(&dev.sbd, PS3_BINDING_CPU_ANY,
					      &irq);
	if (res) {
		pr_err("%s:%u: ps3_sb_event_receive_port_setup failed %d\n",
		       __func__, __LINE__, res);
	       goto fail_close_device;
	}

	spin_lock_init(&dev.lock);

	res = request_irq(irq, ps3_notification_interrupt, 0,
			  "ps3_notification", &dev);
	if (res) {
		pr_err("%s:%u: request_irq failed %d\n", __func__, __LINE__,
		       res);
		goto fail_sb_event_receive_port_destroy;
	}

	/* Setup and write the request for device notification. */
	notify_cmd->operation_code = 0; /* must be zero */
	notify_cmd->event_mask = 1UL << notify_region_probe;

	res = ps3_notification_read_write(&dev, lpar, 1);
	if (res)
		goto fail_free_irq;

	/* Loop here processing the requested notification events. */
	do {
		try_to_freeze();

		memset(notify_event, 0, sizeof(*notify_event));

		res = ps3_notification_read_write(&dev, lpar, 0);
		if (res)
			break;

		pr_debug("%s:%u: notify event type 0x%llx bus id %llu dev id %llu"
			 " type %llu port %llu\n", __func__, __LINE__,
			 notify_event->event_type, notify_event->bus_id,
			 notify_event->dev_id, notify_event->dev_type,
			 notify_event->dev_port);

		if (notify_event->event_type != notify_region_probe ||
		    notify_event->bus_id != dev.sbd.bus_id) {
			pr_warning("%s:%u: bad notify_event: event %llu, "
				   "dev_id %llu, dev_type %llu\n",
				   __func__, __LINE__, notify_event->event_type,
				   notify_event->dev_id,
				   notify_event->dev_type);
			continue;
		}

		ps3_find_and_add_device(dev.sbd.bus_id, notify_event->dev_id);

	} while (!kthread_should_stop());

fail_free_irq:
	free_irq(irq, &dev);
fail_sb_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&dev.sbd, irq);
fail_close_device:
	lv1_close_device(dev.sbd.bus_id, dev.sbd.dev_id);
fail_free:
	kfree(buf);

	probe_task = NULL;

	pr_debug(" <- %s:%u: kthread finished\n", __func__, __LINE__);

	return 0;
}

/**
 * ps3_stop_probe_thread - Stops the background probe thread.
 *
 */

static int ps3_stop_probe_thread(struct notifier_block *nb, unsigned long code,
				 void *data)
{
	if (probe_task)
		kthread_stop(probe_task);
	return 0;
}

static struct notifier_block nb = {
	.notifier_call = ps3_stop_probe_thread
};

/**
 * ps3_start_probe_thread - Starts the background probe thread.
 *
 */

static int __init ps3_start_probe_thread(enum ps3_bus_type bus_type)
{
	int result;
	struct task_struct *task;
	struct ps3_repository_device repo;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	memset(&repo, 0, sizeof(repo));

	repo.bus_type = bus_type;

	result = ps3_repository_find_bus(repo.bus_type, 0, &repo.bus_index);

	if (result) {
		printk(KERN_ERR "%s: Cannot find bus (%d)\n", __func__, result);
		return -ENODEV;
	}

	result = ps3_repository_read_bus_id(repo.bus_index, &repo.bus_id);

	if (result) {
		printk(KERN_ERR "%s: read_bus_id failed %d\n", __func__,
			result);
		return -ENODEV;
	}

	task = kthread_run(ps3_probe_thread, (void *)repo.bus_id,
			   "ps3-probe-%u", bus_type);

	if (IS_ERR(task)) {
		result = PTR_ERR(task);
		printk(KERN_ERR "%s: kthread_run failed %d\n", __func__,
		       result);
		return result;
	}

	probe_task = task;
	register_reboot_notifier(&nb);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;
}

/**
 * ps3_register_devices - Probe the system and register devices found.
 *
 * A device_initcall() routine.
 */

static int __init ps3_register_devices(void)
{
	int result;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	/* ps3_repository_dump_bus_info(); */

	result = ps3_start_probe_thread(PS3_BUS_TYPE_STORAGE);

	ps3_register_vuart_devices();

	ps3_register_graphics_devices();

	ps3_repository_find_devices(PS3_BUS_TYPE_SB, ps3_setup_static_device);

	ps3_register_sound_devices();

	ps3_register_lpm_devices();

	ps3_register_ramdisk_device();

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;
}

device_initcall(ps3_register_devices);
