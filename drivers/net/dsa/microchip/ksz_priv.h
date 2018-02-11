/*
 * Microchip KSZ series switch common definitions
 *
 * Copyright (C) 2017
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __KSZ_PRIV_H
#define __KSZ_PRIV_H

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <net/dsa.h>

#include "ksz_9477_reg.h"

struct ksz_io_ops;

struct vlan_table {
	u32 table[3];
};

struct ksz_device {
	struct dsa_switch *ds;
	struct ksz_platform_data *pdata;
	const char *name;

	struct mutex reg_mutex;		/* register access */
	struct mutex stats_mutex;	/* status access */
	struct mutex alu_mutex;		/* ALU access */
	struct mutex vlan_mutex;	/* vlan access */
	const struct ksz_io_ops *ops;

	struct device *dev;

	void *priv;

	/* chip specific data */
	u32 chip_id;
	int num_vlans;
	int num_alus;
	int num_statics;
	int cpu_port;			/* port connected to CPU */
	int cpu_ports;			/* port bitmap can be cpu port */
	int port_cnt;

	struct vlan_table *vlan_cache;

	u64 mib_value[TOTAL_SWITCH_COUNTER_NUM];
};

struct ksz_io_ops {
	int (*read8)(struct ksz_device *dev, u32 reg, u8 *value);
	int (*read16)(struct ksz_device *dev, u32 reg, u16 *value);
	int (*read24)(struct ksz_device *dev, u32 reg, u32 *value);
	int (*read32)(struct ksz_device *dev, u32 reg, u32 *value);
	int (*write8)(struct ksz_device *dev, u32 reg, u8 value);
	int (*write16)(struct ksz_device *dev, u32 reg, u16 value);
	int (*write24)(struct ksz_device *dev, u32 reg, u32 value);
	int (*write32)(struct ksz_device *dev, u32 reg, u32 value);
	int (*phy_read16)(struct ksz_device *dev, int addr, int reg,
			  u16 *value);
	int (*phy_write16)(struct ksz_device *dev, int addr, int reg,
			   u16 value);
};

struct ksz_device *ksz_switch_alloc(struct device *base,
				    const struct ksz_io_ops *ops, void *priv);
int ksz_switch_detect(struct ksz_device *dev);
int ksz_switch_register(struct ksz_device *dev);
void ksz_switch_remove(struct ksz_device *dev);

static inline int ksz_read8(struct ksz_device *dev, u32 reg, u8 *val)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->read8(dev, reg, val);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_read16(struct ksz_device *dev, u32 reg, u16 *val)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->read16(dev, reg, val);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_read24(struct ksz_device *dev, u32 reg, u32 *val)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->read24(dev, reg, val);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_read32(struct ksz_device *dev, u32 reg, u32 *val)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->read32(dev, reg, val);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_write8(struct ksz_device *dev, u32 reg, u8 value)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->write8(dev, reg, value);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_write16(struct ksz_device *dev, u32 reg, u16 value)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->write16(dev, reg, value);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_write24(struct ksz_device *dev, u32 reg, u32 value)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->write24(dev, reg, value);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_write32(struct ksz_device *dev, u32 reg, u32 value)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->write32(dev, reg, value);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline void ksz_pread8(struct ksz_device *dev, int port, int offset,
			      u8 *data)
{
	ksz_read8(dev, PORT_CTRL_ADDR(port, offset), data);
}

static inline void ksz_pread16(struct ksz_device *dev, int port, int offset,
			       u16 *data)
{
	ksz_read16(dev, PORT_CTRL_ADDR(port, offset), data);
}

static inline void ksz_pread32(struct ksz_device *dev, int port, int offset,
			       u32 *data)
{
	ksz_read32(dev, PORT_CTRL_ADDR(port, offset), data);
}

static inline void ksz_pwrite8(struct ksz_device *dev, int port, int offset,
			       u8 data)
{
	ksz_write8(dev, PORT_CTRL_ADDR(port, offset), data);
}

static inline void ksz_pwrite16(struct ksz_device *dev, int port, int offset,
				u16 data)
{
	ksz_write16(dev, PORT_CTRL_ADDR(port, offset), data);
}

static inline void ksz_pwrite32(struct ksz_device *dev, int port, int offset,
				u32 data)
{
	ksz_write32(dev, PORT_CTRL_ADDR(port, offset), data);
}

#endif
