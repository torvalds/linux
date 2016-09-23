/*
 * Fake VME bridge support.
 *
 * This drive provides a fake VME bridge chip, this enables debugging of the
 * VME framework in the absence of a VME system.
 *
 * This driver has to do a number of things in software that would be driven
 * by hardware if it was available, it will also result in extra overhead at
 * times when compared with driving actual hardware.
 *
 * Author: Martyn Welch <martyn@welches.me.uk>
 * Copyright (c) 2014 Martyn Welch
 *
 * Based on vme_tsi148.c:
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by Tom Armistead and Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/vme.h>

#include "../vme_bridge.h"

/*
 *  Define the number of each that the fake driver supports.
 */
#define FAKE_MAX_MASTER		8	/* Max Master Windows */
#define FAKE_MAX_SLAVE		8	/* Max Slave Windows */

/* Structures to hold information normally held in device registers */
struct fake_slave_window {
	int enabled;
	unsigned long long vme_base;
	unsigned long long size;
	void *buf_base;
	u32 aspace;
	u32 cycle;
};

struct fake_master_window {
	int enabled;
	unsigned long long vme_base;
	unsigned long long size;
	u32 aspace;
	u32 cycle;
	u32 dwidth;
};

/* Structure used to hold driver specific information */
struct fake_driver {
	struct vme_bridge *parent;
	struct fake_slave_window slaves[FAKE_MAX_SLAVE];
	struct fake_master_window masters[FAKE_MAX_MASTER];
	u32 lm_enabled;
	unsigned long long lm_base;
	u32 lm_aspace;
	u32 lm_cycle;
	void (*lm_callback[4])(void *);
	void *lm_data[4];
	struct tasklet_struct int_tasklet;
	int int_level;
	int int_statid;
	void *crcsr_kernel;
	dma_addr_t crcsr_bus;
	/* Only one VME interrupt can be generated at a time, provide locking */
	struct mutex vme_int;
};

/* Module parameter */
static int geoid;

static const char driver_name[] = "vme_fake";

static struct vme_bridge *exit_pointer;

static struct device *vme_root;

/*
 * Calling VME bus interrupt callback if provided.
 */
static void fake_VIRQ_tasklet(unsigned long data)
{
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = (struct vme_bridge *) data;
	bridge = fake_bridge->driver_priv;

	vme_irq_handler(fake_bridge, bridge->int_level, bridge->int_statid);
}

/*
 * Configure VME interrupt
 */
static void fake_irq_set(struct vme_bridge *fake_bridge, int level,
		int state, int sync)
{
	/* Nothing to do */
}

static void *fake_pci_to_ptr(dma_addr_t addr)
{
	return (void *)(uintptr_t)addr;
}

static dma_addr_t fake_ptr_to_pci(void *addr)
{
	return (dma_addr_t)(uintptr_t)addr;
}

/*
 * Generate a VME bus interrupt at the requested level & vector. Wait for
 * interrupt to be acked.
 */
static int fake_irq_generate(struct vme_bridge *fake_bridge, int level,
		int statid)
{
	struct fake_driver *bridge;

	bridge = fake_bridge->driver_priv;

	mutex_lock(&bridge->vme_int);

	bridge->int_level = level;

	bridge->int_statid = statid;

	/*
	 * Schedule tasklet to run VME handler to emulate normal VME interrupt
	 * handler behaviour.
	 */
	tasklet_schedule(&bridge->int_tasklet);

	mutex_unlock(&bridge->vme_int);

	return 0;
}

/*
 * Initialize a slave window with the requested attributes.
 */
static int fake_slave_set(struct vme_slave_resource *image, int enabled,
		unsigned long long vme_base, unsigned long long size,
		dma_addr_t buf_base, u32 aspace, u32 cycle)
{
	unsigned int i, granularity = 0;
	unsigned long long vme_bound;
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = image->parent;
	bridge = fake_bridge->driver_priv;

	i = image->number;

	switch (aspace) {
	case VME_A16:
		granularity = 0x10;
		break;
	case VME_A24:
		granularity = 0x1000;
		break;
	case VME_A32:
		granularity = 0x10000;
		break;
	case VME_A64:
		granularity = 0x10000;
		break;
	case VME_CRCSR:
	case VME_USER1:
	case VME_USER2:
	case VME_USER3:
	case VME_USER4:
	default:
		pr_err("Invalid address space\n");
		return -EINVAL;
	}

	/*
	 * Bound address is a valid address for the window, adjust
	 * accordingly
	 */
	vme_bound = vme_base + size - granularity;

	if (vme_base & (granularity - 1)) {
		pr_err("Invalid VME base alignment\n");
		return -EINVAL;
	}
	if (vme_bound & (granularity - 1)) {
		pr_err("Invalid VME bound alignment\n");
		return -EINVAL;
	}

	mutex_lock(&image->mtx);

	bridge->slaves[i].enabled = enabled;
	bridge->slaves[i].vme_base = vme_base;
	bridge->slaves[i].size = size;
	bridge->slaves[i].buf_base = fake_pci_to_ptr(buf_base);
	bridge->slaves[i].aspace = aspace;
	bridge->slaves[i].cycle = cycle;

	mutex_unlock(&image->mtx);

	return 0;
}

/*
 * Get slave window configuration.
 */
static int fake_slave_get(struct vme_slave_resource *image, int *enabled,
		unsigned long long *vme_base, unsigned long long *size,
		dma_addr_t *buf_base, u32 *aspace, u32 *cycle)
{
	unsigned int i;
	struct fake_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	mutex_lock(&image->mtx);

	*enabled = bridge->slaves[i].enabled;
	*vme_base = bridge->slaves[i].vme_base;
	*size = bridge->slaves[i].size;
	*buf_base = fake_ptr_to_pci(bridge->slaves[i].buf_base);
	*aspace = bridge->slaves[i].aspace;
	*cycle = bridge->slaves[i].cycle;

	mutex_unlock(&image->mtx);

	return 0;
}

/*
 * Set the attributes of an outbound window.
 */
static int fake_master_set(struct vme_master_resource *image, int enabled,
		unsigned long long vme_base, unsigned long long size,
		u32 aspace, u32 cycle, u32 dwidth)
{
	int retval = 0;
	unsigned int i;
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = image->parent;

	bridge = fake_bridge->driver_priv;

	/* Verify input data */
	if (vme_base & 0xFFFF) {
		pr_err("Invalid VME Window alignment\n");
		retval = -EINVAL;
		goto err_window;
	}

	if (size & 0xFFFF) {
		spin_unlock(&image->lock);
		pr_err("Invalid size alignment\n");
		retval = -EINVAL;
		goto err_window;
	}

	if ((size == 0) && (enabled != 0)) {
		pr_err("Size must be non-zero for enabled windows\n");
		retval = -EINVAL;
		goto err_window;
	}

	/* Setup data width */
	switch (dwidth) {
	case VME_D8:
	case VME_D16:
	case VME_D32:
		break;
	default:
		spin_unlock(&image->lock);
		pr_err("Invalid data width\n");
		retval = -EINVAL;
		goto err_dwidth;
	}

	/* Setup address space */
	switch (aspace) {
	case VME_A16:
	case VME_A24:
	case VME_A32:
	case VME_A64:
	case VME_CRCSR:
	case VME_USER1:
	case VME_USER2:
	case VME_USER3:
	case VME_USER4:
		break;
	default:
		spin_unlock(&image->lock);
		pr_err("Invalid address space\n");
		retval = -EINVAL;
		goto err_aspace;
	}

	spin_lock(&image->lock);

	i = image->number;

	bridge->masters[i].enabled = enabled;
	bridge->masters[i].vme_base = vme_base;
	bridge->masters[i].size = size;
	bridge->masters[i].aspace = aspace;
	bridge->masters[i].cycle = cycle;
	bridge->masters[i].dwidth = dwidth;

	spin_unlock(&image->lock);

	return 0;

err_aspace:
err_dwidth:
err_window:
	return retval;

}

/*
 * Set the attributes of an outbound window.
 */
static int __fake_master_get(struct vme_master_resource *image, int *enabled,
		unsigned long long *vme_base, unsigned long long *size,
		u32 *aspace, u32 *cycle, u32 *dwidth)
{
	unsigned int i;
	struct fake_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	*enabled = bridge->masters[i].enabled;
	*vme_base = bridge->masters[i].vme_base;
	*size = bridge->masters[i].size;
	*aspace = bridge->masters[i].aspace;
	*cycle = bridge->masters[i].cycle;
	*dwidth = bridge->masters[i].dwidth;

	return 0;
}


static int fake_master_get(struct vme_master_resource *image, int *enabled,
		unsigned long long *vme_base, unsigned long long *size,
		u32 *aspace, u32 *cycle, u32 *dwidth)
{
	int retval;

	spin_lock(&image->lock);

	retval = __fake_master_get(image, enabled, vme_base, size, aspace,
			cycle, dwidth);

	spin_unlock(&image->lock);

	return retval;
}


static void fake_lm_check(struct fake_driver *bridge, unsigned long long addr,
			  u32 aspace, u32 cycle)
{
	struct vme_bridge *fake_bridge;
	unsigned long long lm_base;
	u32 lm_aspace, lm_cycle;
	int i;
	struct vme_lm_resource *lm;
	struct list_head *pos = NULL, *n;

	/* Get vme_bridge */
	fake_bridge = bridge->parent;

	/* Loop through each location monitor resource */
	list_for_each_safe(pos, n, &fake_bridge->lm_resources) {
		lm = list_entry(pos, struct vme_lm_resource, list);

		/* If disabled, we're done */
		if (bridge->lm_enabled == 0)
			return;

		lm_base = bridge->lm_base;
		lm_aspace = bridge->lm_aspace;
		lm_cycle = bridge->lm_cycle;

		/* First make sure that the cycle and address space match */
		if ((lm_aspace == aspace) && (lm_cycle == cycle)) {
			for (i = 0; i < lm->monitors; i++) {
				/* Each location monitor covers 8 bytes */
				if (((lm_base + (8 * i)) <= addr) &&
				    ((lm_base + (8 * i) + 8) > addr)) {
					if (bridge->lm_callback[i] != NULL)
						bridge->lm_callback[i](
							bridge->lm_data[i]);
				}
			}
		}
	}
}

static u8 fake_vmeread8(struct fake_driver *bridge, unsigned long long addr,
		u32 aspace, u32 cycle)
{
	u8 retval = 0xff;
	int i;
	unsigned long long start, end, offset;
	u8 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		if ((addr >= start) && (addr < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u8 *)(bridge->slaves[i].buf_base + offset);
			retval = *loc;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

	return retval;
}

static u16 fake_vmeread16(struct fake_driver *bridge, unsigned long long addr,
		u32 aspace, u32 cycle)
{
	u16 retval = 0xffff;
	int i;
	unsigned long long start, end, offset;
	u16 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if ((addr >= start) && ((addr + 1) < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u16 *)(bridge->slaves[i].buf_base + offset);
			retval = *loc;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

	return retval;
}

static u32 fake_vmeread32(struct fake_driver *bridge, unsigned long long addr,
		u32 aspace, u32 cycle)
{
	u32 retval = 0xffffffff;
	int i;
	unsigned long long start, end, offset;
	u32 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if ((addr >= start) && ((addr + 3) < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u32 *)(bridge->slaves[i].buf_base + offset);
			retval = *loc;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

	return retval;
}

static ssize_t fake_master_read(struct vme_master_resource *image, void *buf,
		size_t count, loff_t offset)
{
	int retval;
	u32 aspace, cycle, dwidth;
	struct vme_bridge *fake_bridge;
	struct fake_driver *priv;
	int i;
	unsigned long long addr;
	unsigned int done = 0;
	unsigned int count32;

	fake_bridge = image->parent;

	priv = fake_bridge->driver_priv;

	i = image->number;

	addr = (unsigned long long)priv->masters[i].vme_base + offset;
	aspace = priv->masters[i].aspace;
	cycle = priv->masters[i].cycle;
	dwidth = priv->masters[i].dwidth;

	spin_lock(&image->lock);

	/* The following code handles VME address alignment. We cannot use
	 * memcpy_xxx here because it may cut data transfers in to 8-bit
	 * cycles when D16 or D32 cycles are required on the VME bus.
	 * On the other hand, the bridge itself assures that the maximum data
	 * cycle configured for the transfer is used and splits it
	 * automatically for non-aligned addresses, so we don't want the
	 * overhead of needlessly forcing small transfers for the entire cycle.
	 */
	if (addr & 0x1) {
		*(u8 *)buf = fake_vmeread8(priv, addr, aspace, cycle);
		done += 1;
		if (done == count)
			goto out;
	}
	if ((dwidth == VME_D16) || (dwidth == VME_D32)) {
		if ((addr + done) & 0x2) {
			if ((count - done) < 2) {
				*(u8 *)(buf + done) = fake_vmeread8(priv,
						addr + done, aspace, cycle);
				done += 1;
				goto out;
			} else {
				*(u16 *)(buf + done) = fake_vmeread16(priv,
						addr + done, aspace, cycle);
				done += 2;
			}
		}
	}

	if (dwidth == VME_D32) {
		count32 = (count - done) & ~0x3;
		while (done < count32) {
			*(u32 *)(buf + done) = fake_vmeread32(priv, addr + done,
					aspace, cycle);
			done += 4;
		}
	} else if (dwidth == VME_D16) {
		count32 = (count - done) & ~0x3;
		while (done < count32) {
			*(u16 *)(buf + done) = fake_vmeread16(priv, addr + done,
					aspace, cycle);
			done += 2;
		}
	} else if (dwidth == VME_D8) {
		count32 = (count - done);
		while (done < count32) {
			*(u8 *)(buf + done) = fake_vmeread8(priv, addr + done,
					aspace, cycle);
			done += 1;
		}

	}

	if ((dwidth == VME_D16) || (dwidth == VME_D32)) {
		if ((count - done) & 0x2) {
			*(u16 *)(buf + done) = fake_vmeread16(priv, addr + done,
					aspace, cycle);
			done += 2;
		}
	}
	if ((count - done) & 0x1) {
		*(u8 *)(buf + done) = fake_vmeread8(priv, addr + done, aspace,
				cycle);
		done += 1;
	}

out:
	retval = count;

	spin_unlock(&image->lock);

	return retval;
}

static void fake_vmewrite8(struct fake_driver *bridge, u8 *buf,
			   unsigned long long addr, u32 aspace, u32 cycle)
{
	int i;
	unsigned long long start, end, offset;
	u8 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if ((addr >= start) && (addr < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u8 *)((void *)bridge->slaves[i].buf_base + offset);
			*loc = *buf;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

}

static void fake_vmewrite16(struct fake_driver *bridge, u16 *buf,
			    unsigned long long addr, u32 aspace, u32 cycle)
{
	int i;
	unsigned long long start, end, offset;
	u16 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if ((addr >= start) && ((addr + 1) < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u16 *)((void *)bridge->slaves[i].buf_base + offset);
			*loc = *buf;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

}

static void fake_vmewrite32(struct fake_driver *bridge, u32 *buf,
			    unsigned long long addr, u32 aspace, u32 cycle)
{
	int i;
	unsigned long long start, end, offset;
	u32 *loc;

	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		if (aspace != bridge->slaves[i].aspace)
			continue;

		if (cycle != bridge->slaves[i].cycle)
			continue;

		start = bridge->slaves[i].vme_base;
		end = bridge->slaves[i].vme_base + bridge->slaves[i].size;

		if ((addr >= start) && ((addr + 3) < end)) {
			offset = addr - bridge->slaves[i].vme_base;
			loc = (u32 *)((void *)bridge->slaves[i].buf_base + offset);
			*loc = *buf;

			break;
		}
	}

	fake_lm_check(bridge, addr, aspace, cycle);

}

static ssize_t fake_master_write(struct vme_master_resource *image, void *buf,
		size_t count, loff_t offset)
{
	int retval = 0;
	u32 aspace, cycle, dwidth;
	unsigned long long addr;
	int i;
	unsigned int done = 0;
	unsigned int count32;

	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = image->parent;

	bridge = fake_bridge->driver_priv;

	i = image->number;

	addr = bridge->masters[i].vme_base + offset;
	aspace = bridge->masters[i].aspace;
	cycle = bridge->masters[i].cycle;
	dwidth = bridge->masters[i].dwidth;

	spin_lock(&image->lock);

	/* Here we apply for the same strategy we do in master_read
	 * function in order to assure the correct cycles.
	 */
	if (addr & 0x1) {
		fake_vmewrite8(bridge, (u8 *)buf, addr, aspace, cycle);
		done += 1;
		if (done == count)
			goto out;
	}

	if ((dwidth == VME_D16) || (dwidth == VME_D32)) {
		if ((addr + done) & 0x2) {
			if ((count - done) < 2) {
				fake_vmewrite8(bridge, (u8 *)(buf + done),
						addr + done, aspace, cycle);
				done += 1;
				goto out;
			} else {
				fake_vmewrite16(bridge, (u16 *)(buf + done),
						addr + done, aspace, cycle);
				done += 2;
			}
		}
	}

	if (dwidth == VME_D32) {
		count32 = (count - done) & ~0x3;
		while (done < count32) {
			fake_vmewrite32(bridge, (u32 *)(buf + done),
					addr + done, aspace, cycle);
			done += 4;
		}
	} else if (dwidth == VME_D16) {
		count32 = (count - done) & ~0x3;
		while (done < count32) {
			fake_vmewrite16(bridge, (u16 *)(buf + done),
					addr + done, aspace, cycle);
			done += 2;
		}
	} else if (dwidth == VME_D8) {
		count32 = (count - done);
		while (done < count32) {
			fake_vmewrite8(bridge, (u8 *)(buf + done), addr + done,
					aspace, cycle);
			done += 1;
		}

	}

	if ((dwidth == VME_D16) || (dwidth == VME_D32)) {
		if ((count - done) & 0x2) {
			fake_vmewrite16(bridge, (u16 *)(buf + done),
					addr + done, aspace, cycle);
			done += 2;
		}
	}

	if ((count - done) & 0x1) {
		fake_vmewrite8(bridge, (u8 *)(buf + done), addr + done, aspace,
				cycle);
		done += 1;
	}

out:
	retval = count;

	spin_unlock(&image->lock);

	return retval;
}

/*
 * Perform an RMW cycle on the VME bus.
 *
 * Requires a previously configured master window, returns final value.
 */
static unsigned int fake_master_rmw(struct vme_master_resource *image,
		unsigned int mask, unsigned int compare, unsigned int swap,
		loff_t offset)
{
	u32 tmp, base;
	u32 aspace, cycle;
	int i;
	struct fake_driver *bridge;

	bridge = image->parent->driver_priv;

	/* Find the PCI address that maps to the desired VME address */
	i = image->number;

	base = bridge->masters[i].vme_base;
	aspace = bridge->masters[i].aspace;
	cycle = bridge->masters[i].cycle;

	/* Lock image */
	spin_lock(&image->lock);

	/* Read existing value */
	tmp = fake_vmeread32(bridge, base + offset, aspace, cycle);

	/* Perform check */
	if ((tmp && mask) == (compare && mask)) {
		tmp = tmp | (mask | swap);
		tmp = tmp & (~mask | swap);

		/* Write back */
		fake_vmewrite32(bridge, &tmp, base + offset, aspace, cycle);
	}

	/* Unlock image */
	spin_unlock(&image->lock);

	return tmp;
}

/*
 * All 4 location monitors reside at the same base - this is therefore a
 * system wide configuration.
 *
 * This does not enable the LM monitor - that should be done when the first
 * callback is attached and disabled when the last callback is removed.
 */
static int fake_lm_set(struct vme_lm_resource *lm, unsigned long long lm_base,
		u32 aspace, u32 cycle)
{
	int i;
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = lm->parent;

	bridge = fake_bridge->driver_priv;

	mutex_lock(&lm->mtx);

	/* If we already have a callback attached, we can't move it! */
	for (i = 0; i < lm->monitors; i++) {
		if (bridge->lm_callback[i] != NULL) {
			mutex_unlock(&lm->mtx);
			pr_err("Location monitor callback attached, can't reset\n");
			return -EBUSY;
		}
	}

	switch (aspace) {
	case VME_A16:
	case VME_A24:
	case VME_A32:
	case VME_A64:
		break;
	default:
		mutex_unlock(&lm->mtx);
		pr_err("Invalid address space\n");
		return -EINVAL;
	}

	bridge->lm_base = lm_base;
	bridge->lm_aspace = aspace;
	bridge->lm_cycle = cycle;

	mutex_unlock(&lm->mtx);

	return 0;
}

/* Get configuration of the callback monitor and return whether it is enabled
 * or disabled.
 */
static int fake_lm_get(struct vme_lm_resource *lm,
		unsigned long long *lm_base, u32 *aspace, u32 *cycle)
{
	struct fake_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	*lm_base = bridge->lm_base;
	*aspace = bridge->lm_aspace;
	*cycle = bridge->lm_cycle;

	mutex_unlock(&lm->mtx);

	return bridge->lm_enabled;
}

/*
 * Attach a callback to a specific location monitor.
 *
 * Callback will be passed the monitor triggered.
 */
static int fake_lm_attach(struct vme_lm_resource *lm, int monitor,
		void (*callback)(void *), void *data)
{
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = lm->parent;

	bridge = fake_bridge->driver_priv;

	mutex_lock(&lm->mtx);

	/* Ensure that the location monitor is configured - need PGM or DATA */
	if (bridge->lm_cycle == 0) {
		mutex_unlock(&lm->mtx);
		pr_err("Location monitor not properly configured\n");
		return -EINVAL;
	}

	/* Check that a callback isn't already attached */
	if (bridge->lm_callback[monitor] != NULL) {
		mutex_unlock(&lm->mtx);
		pr_err("Existing callback attached\n");
		return -EBUSY;
	}

	/* Attach callback */
	bridge->lm_callback[monitor] = callback;
	bridge->lm_data[monitor] = data;

	/* Ensure that global Location Monitor Enable set */
	bridge->lm_enabled = 1;

	mutex_unlock(&lm->mtx);

	return 0;
}

/*
 * Detach a callback function forn a specific location monitor.
 */
static int fake_lm_detach(struct vme_lm_resource *lm, int monitor)
{
	u32 tmp;
	int i;
	struct fake_driver *bridge;

	bridge = lm->parent->driver_priv;

	mutex_lock(&lm->mtx);

	/* Detach callback */
	bridge->lm_callback[monitor] = NULL;
	bridge->lm_data[monitor] = NULL;

	/* If all location monitors disabled, disable global Location Monitor */
	tmp = 0;
	for (i = 0; i < lm->monitors; i++) {
		if (bridge->lm_callback[i] != NULL)
			tmp = 1;
	}

	if (tmp == 0)
		bridge->lm_enabled = 0;

	mutex_unlock(&lm->mtx);

	return 0;
}

/*
 * Determine Geographical Addressing
 */
static int fake_slot_get(struct vme_bridge *fake_bridge)
{
	return geoid;
}

static void *fake_alloc_consistent(struct device *parent, size_t size,
		dma_addr_t *dma)
{
	void *alloc = kmalloc(size, GFP_KERNEL);

	if (alloc != NULL)
		*dma = fake_ptr_to_pci(alloc);

	return alloc;
}

static void fake_free_consistent(struct device *parent, size_t size,
		void *vaddr, dma_addr_t dma)
{
	kfree(vaddr);
/*
	dma_free_coherent(parent, size, vaddr, dma);
*/
}

/*
 * Configure CR/CSR space
 *
 * Access to the CR/CSR can be configured at power-up. The location of the
 * CR/CSR registers in the CR/CSR address space is determined by the boards
 * Geographic address.
 *
 * Each board has a 512kB window, with the highest 4kB being used for the
 * boards registers, this means there is a fix length 508kB window which must
 * be mapped onto PCI memory.
 */
static int fake_crcsr_init(struct vme_bridge *fake_bridge)
{
	u32 vstat;
	struct fake_driver *bridge;

	bridge = fake_bridge->driver_priv;

	/* Allocate mem for CR/CSR image */
	bridge->crcsr_kernel = kzalloc(VME_CRCSR_BUF_SIZE, GFP_KERNEL);
	bridge->crcsr_bus = fake_ptr_to_pci(bridge->crcsr_kernel);
	if (bridge->crcsr_kernel == NULL)
		return -ENOMEM;

	vstat = fake_slot_get(fake_bridge);

	pr_info("CR/CSR Offset: %d\n", vstat);

	return 0;
}

static void fake_crcsr_exit(struct vme_bridge *fake_bridge)
{
	struct fake_driver *bridge;

	bridge = fake_bridge->driver_priv;

	kfree(bridge->crcsr_kernel);
}


static int __init fake_init(void)
{
	int retval, i;
	struct list_head *pos = NULL, *n;
	struct vme_bridge *fake_bridge;
	struct fake_driver *fake_device;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct vme_lm_resource *lm;

	/* We need a fake parent device */
	vme_root = __root_device_register("vme", THIS_MODULE);

	/* If we want to support more than one bridge at some point, we need to
	 * dynamically allocate this so we get one per device.
	 */
	fake_bridge = kzalloc(sizeof(struct vme_bridge), GFP_KERNEL);
	if (fake_bridge == NULL) {
		retval = -ENOMEM;
		goto err_struct;
	}

	fake_device = kzalloc(sizeof(struct fake_driver), GFP_KERNEL);
	if (fake_device == NULL) {
		retval = -ENOMEM;
		goto err_driver;
	}

	fake_bridge->driver_priv = fake_device;

	fake_bridge->parent = vme_root;

	fake_device->parent = fake_bridge;

	/* Initialize wait queues & mutual exclusion flags */
	mutex_init(&fake_device->vme_int);
	mutex_init(&fake_bridge->irq_mtx);
	tasklet_init(&fake_device->int_tasklet, fake_VIRQ_tasklet,
			(unsigned long) fake_bridge);

	strcpy(fake_bridge->name, driver_name);

	/* Add master windows to list */
	INIT_LIST_HEAD(&fake_bridge->master_resources);
	for (i = 0; i < FAKE_MAX_MASTER; i++) {
		master_image = kmalloc(sizeof(struct vme_master_resource),
				GFP_KERNEL);
		if (master_image == NULL) {
			retval = -ENOMEM;
			goto err_master;
		}
		master_image->parent = fake_bridge;
		spin_lock_init(&master_image->lock);
		master_image->locked = 0;
		master_image->number = i;
		master_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_A64;
		master_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_2eVME | VME_2eSST | VME_2eSSTB | VME_2eSST160 |
			VME_2eSST267 | VME_2eSST320 | VME_SUPER | VME_USER |
			VME_PROG | VME_DATA;
		master_image->width_attr = VME_D16 | VME_D32;
		memset(&master_image->bus_resource, 0,
				sizeof(struct resource));
		master_image->kern_base  = NULL;
		list_add_tail(&master_image->list,
				&fake_bridge->master_resources);
	}

	/* Add slave windows to list */
	INIT_LIST_HEAD(&fake_bridge->slave_resources);
	for (i = 0; i < FAKE_MAX_SLAVE; i++) {
		slave_image = kmalloc(sizeof(struct vme_slave_resource),
				GFP_KERNEL);
		if (slave_image == NULL) {
			retval = -ENOMEM;
			goto err_slave;
		}
		slave_image->parent = fake_bridge;
		mutex_init(&slave_image->mtx);
		slave_image->locked = 0;
		slave_image->number = i;
		slave_image->address_attr = VME_A16 | VME_A24 | VME_A32 |
			VME_A64 | VME_CRCSR | VME_USER1 | VME_USER2 |
			VME_USER3 | VME_USER4;
		slave_image->cycle_attr = VME_SCT | VME_BLT | VME_MBLT |
			VME_2eVME | VME_2eSST | VME_2eSSTB | VME_2eSST160 |
			VME_2eSST267 | VME_2eSST320 | VME_SUPER | VME_USER |
			VME_PROG | VME_DATA;
		list_add_tail(&slave_image->list,
				&fake_bridge->slave_resources);
	}

	/* Add location monitor to list */
	INIT_LIST_HEAD(&fake_bridge->lm_resources);
	lm = kmalloc(sizeof(struct vme_lm_resource), GFP_KERNEL);
	if (lm == NULL) {
		pr_err("Failed to allocate memory for location monitor resource structure\n");
		retval = -ENOMEM;
		goto err_lm;
	}
	lm->parent = fake_bridge;
	mutex_init(&lm->mtx);
	lm->locked = 0;
	lm->number = 1;
	lm->monitors = 4;
	list_add_tail(&lm->list, &fake_bridge->lm_resources);

	fake_bridge->slave_get = fake_slave_get;
	fake_bridge->slave_set = fake_slave_set;
	fake_bridge->master_get = fake_master_get;
	fake_bridge->master_set = fake_master_set;
	fake_bridge->master_read = fake_master_read;
	fake_bridge->master_write = fake_master_write;
	fake_bridge->master_rmw = fake_master_rmw;
	fake_bridge->irq_set = fake_irq_set;
	fake_bridge->irq_generate = fake_irq_generate;
	fake_bridge->lm_set = fake_lm_set;
	fake_bridge->lm_get = fake_lm_get;
	fake_bridge->lm_attach = fake_lm_attach;
	fake_bridge->lm_detach = fake_lm_detach;
	fake_bridge->slot_get = fake_slot_get;
	fake_bridge->alloc_consistent = fake_alloc_consistent;
	fake_bridge->free_consistent = fake_free_consistent;

	pr_info("Board is%s the VME system controller\n",
			(geoid == 1) ? "" : " not");

	pr_info("VME geographical address is set to %d\n", geoid);

	retval = fake_crcsr_init(fake_bridge);
	if (retval) {
		pr_err("CR/CSR configuration failed.\n");
		goto err_crcsr;
	}

	retval = vme_register_bridge(fake_bridge);
	if (retval != 0) {
		pr_err("Chip Registration failed.\n");
		goto err_reg;
	}

	exit_pointer = fake_bridge;

	return 0;

err_reg:
	fake_crcsr_exit(fake_bridge);
err_crcsr:
err_lm:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &fake_bridge->lm_resources) {
		lm = list_entry(pos, struct vme_lm_resource, list);
		list_del(pos);
		kfree(lm);
	}
err_slave:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &fake_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}
err_master:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &fake_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
				list);
		list_del(pos);
		kfree(master_image);
	}

	kfree(fake_device);
err_driver:
	kfree(fake_bridge);
err_struct:
	return retval;

}


static void __exit fake_exit(void)
{
	struct list_head *pos = NULL;
	struct list_head *tmplist;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	int i;
	struct vme_bridge *fake_bridge;
	struct fake_driver *bridge;

	fake_bridge = exit_pointer;

	bridge = fake_bridge->driver_priv;

	pr_debug("Driver is being unloaded.\n");

	/*
	 *  Shutdown all inbound and outbound windows.
	 */
	for (i = 0; i < FAKE_MAX_MASTER; i++)
		bridge->masters[i].enabled = 0;

	for (i = 0; i < FAKE_MAX_SLAVE; i++)
		bridge->slaves[i].enabled = 0;

	/*
	 *  Shutdown Location monitor.
	 */
	bridge->lm_enabled = 0;

	vme_unregister_bridge(fake_bridge);

	fake_crcsr_exit(fake_bridge);
	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &fake_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &fake_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
				list);
		list_del(pos);
		kfree(master_image);
	}

	kfree(fake_bridge->driver_priv);

	kfree(fake_bridge);

	root_device_unregister(vme_root);
}


MODULE_PARM_DESC(geoid, "Set geographical addressing");
module_param(geoid, int, 0);

MODULE_DESCRIPTION("Fake VME bridge driver");
MODULE_LICENSE("GPL");

module_init(fake_init);
module_exit(fake_exit);
