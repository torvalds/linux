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

#include <asm/firmware.h>
#include <asm/lv1call.h>

#include "platform.h"

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

static int __init_refok ps3_setup_uhc_device(
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

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
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

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
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

	p->dev.match_id = PS3_MATCH_ID_GRAPHICS;
	p->dev.dev_type = PS3_DEVICE_TYPE_IOC0;

	result = ps3_system_bus_device_register(&p->dev);

	if (result)
		pr_debug("%s:%d ps3_system_bus_device_register failed\n",
			__func__, __LINE__);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

/**
 * ps3_register_repository_device - Register a device from the repositiory info.
 *
 */

static int ps3_register_repository_device(
	const struct ps3_repository_device *repo)
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
		result = 0;
		pr_debug("%s:%u: unsupported dev_type %u\n", __func__, __LINE__,
			repo->dev_type);
	}

	return result;
}

/**
 * ps3_probe_thread - Background repository probing at system startup.
 *
 * This implementation only supports background probing on a single bus.
 */

static int ps3_probe_thread(void *data)
{
	struct ps3_repository_device *repo = data;
	int result;
	unsigned int ms = 250;

	pr_debug(" -> %s:%u: kthread started\n", __func__, __LINE__);

	do {
		try_to_freeze();

		pr_debug("%s:%u: probing...\n", __func__, __LINE__);

		do {
			result = ps3_repository_find_device(repo);

			if (result == -ENODEV)
				pr_debug("%s:%u: nothing new\n", __func__,
					__LINE__);
			else if (result)
				pr_debug("%s:%u: find device error.\n",
					__func__, __LINE__);
			else {
				pr_debug("%s:%u: found device\n", __func__,
					__LINE__);
				ps3_register_repository_device(repo);
				ps3_repository_bump_device(repo);
				ms = 250;
			}
		} while (!result);

		pr_debug("%s:%u: ms %u\n", __func__, __LINE__, ms);

		if ( ms > 60000)
			break;

		msleep_interruptible(ms);

		/* An exponential backoff. */
		ms <<= 1;

	} while (!kthread_should_stop());

	pr_debug(" <- %s:%u: kthread finished\n", __func__, __LINE__);

	return 0;
}

/**
 * ps3_start_probe_thread - Starts the background probe thread.
 *
 */

static int __init ps3_start_probe_thread(enum ps3_bus_type bus_type)
{
	int result;
	struct task_struct *task;
	static struct ps3_repository_device repo; /* must be static */

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

	task = kthread_run(ps3_probe_thread, &repo, "ps3-probe-%u", bus_type);

	if (IS_ERR(task)) {
		result = PTR_ERR(task);
		printk(KERN_ERR "%s: kthread_run failed %d\n", __func__,
		       result);
		return result;
	}

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

	ps3_repository_find_devices(PS3_BUS_TYPE_SB,
		ps3_register_repository_device);

	ps3_register_sound_devices();

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;
}

device_initcall(ps3_register_devices);
