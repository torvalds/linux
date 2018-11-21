/* SPDX-License-Identifier: GPL-2.0
 * Microchip switch driver common header
 *
 * Copyright (C) 2017-2018 Microchip Technology Inc.
 */

#ifndef __KSZ_COMMON_H
#define __KSZ_COMMON_H

void ksz_update_port_member(struct ksz_device *dev, int port);

/* Common DSA access functions */

int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg);
int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val);
int ksz_sset_count(struct dsa_switch *ds, int port, int sset);
int ksz_port_bridge_join(struct dsa_switch *ds, int port,
			 struct net_device *br);
void ksz_port_bridge_leave(struct dsa_switch *ds, int port,
			   struct net_device *br);
void ksz_port_fast_age(struct dsa_switch *ds, int port);
int ksz_port_vlan_prepare(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan);
int ksz_port_fdb_dump(struct dsa_switch *ds, int port, dsa_fdb_dump_cb_t *cb,
		      void *data);
int ksz_port_mdb_prepare(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb);
void ksz_port_mdb_add(struct dsa_switch *ds, int port,
		      const struct switchdev_obj_port_mdb *mdb);
int ksz_port_mdb_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb);
int ksz_enable_port(struct dsa_switch *ds, int port, struct phy_device *phy);
void ksz_disable_port(struct dsa_switch *ds, int port, struct phy_device *phy);

/* Common register access functions */

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

static inline int ksz_get(struct ksz_device *dev, u32 reg, void *data,
			  size_t len)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->get(dev, reg, data, len);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline int ksz_set(struct ksz_device *dev, u32 reg, void *data,
			  size_t len)
{
	int ret;

	mutex_lock(&dev->reg_mutex);
	ret = dev->ops->set(dev, reg, data, len);
	mutex_unlock(&dev->reg_mutex);

	return ret;
}

static inline void ksz_pread8(struct ksz_device *dev, int port, int offset,
			      u8 *data)
{
	ksz_read8(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline void ksz_pread16(struct ksz_device *dev, int port, int offset,
			       u16 *data)
{
	ksz_read16(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline void ksz_pread32(struct ksz_device *dev, int port, int offset,
			       u32 *data)
{
	ksz_read32(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline void ksz_pwrite8(struct ksz_device *dev, int port, int offset,
			       u8 data)
{
	ksz_write8(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline void ksz_pwrite16(struct ksz_device *dev, int port, int offset,
				u16 data)
{
	ksz_write16(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline void ksz_pwrite32(struct ksz_device *dev, int port, int offset,
				u32 data)
{
	ksz_write32(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static void ksz_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	u8 data;

	ksz_read8(dev, addr, &data);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	ksz_write8(dev, addr, data);
}

static void ksz_port_cfg(struct ksz_device *dev, int port, int offset, u8 bits,
			 bool set)
{
	u32 addr;
	u8 data;

	addr = dev->dev_ops->get_port_addr(port, offset);
	ksz_read8(dev, addr, &data);

	if (set)
		data |= bits;
	else
		data &= ~bits;

	ksz_write8(dev, addr, data);
}

#endif
