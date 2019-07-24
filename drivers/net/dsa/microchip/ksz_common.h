/* SPDX-License-Identifier: GPL-2.0
 * Microchip switch driver common header
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#ifndef __KSZ_COMMON_H
#define __KSZ_COMMON_H

#include <linux/regmap.h>

void ksz_port_cleanup(struct ksz_device *dev, int port);
void ksz_update_port_member(struct ksz_device *dev, int port);
void ksz_init_mib_timer(struct ksz_device *dev);

/* Common DSA access functions */

int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg);
int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val);
void ksz_adjust_link(struct dsa_switch *ds, int port,
		     struct phy_device *phydev);
int ksz_sset_count(struct dsa_switch *ds, int port, int sset);
void ksz_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *buf);
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
void ksz_disable_port(struct dsa_switch *ds, int port);

/* Common register access functions */

static inline int ksz_read8(struct ksz_device *dev, u32 reg, u8 *val)
{
	unsigned int value;
	int ret = regmap_read(dev->regmap[0], reg, &value);

	*val = value;
	return ret;
}

static inline int ksz_read16(struct ksz_device *dev, u32 reg, u16 *val)
{
	unsigned int value;
	int ret = regmap_read(dev->regmap[1], reg, &value);

	*val = value;
	return ret;
}

static inline int ksz_read32(struct ksz_device *dev, u32 reg, u32 *val)
{
	unsigned int value;
	int ret = regmap_read(dev->regmap[2], reg, &value);

	*val = value;
	return ret;
}

static inline int ksz_write8(struct ksz_device *dev, u32 reg, u8 value)
{
	return regmap_write(dev->regmap[0], reg, value);
}

static inline int ksz_write16(struct ksz_device *dev, u32 reg, u16 value)
{
	return regmap_write(dev->regmap[1], reg, value);
}

static inline int ksz_write32(struct ksz_device *dev, u32 reg, u32 value)
{
	return regmap_write(dev->regmap[2], reg, value);
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

/* Regmap tables generation */
#define KSZ_SPI_OP_RD		3
#define KSZ_SPI_OP_WR		2

#define KSZ_SPI_OP_FLAG_MASK(opcode, swp, regbits, regpad)		\
	swab##swp((opcode) << ((regbits) + (regpad)))

#define KSZ_REGMAP_ENTRY(width, swp, regbits, regpad, regalign)		\
	{								\
		.val_bits = (width),					\
		.reg_stride = (width) / 8,				\
		.reg_bits = (regbits) + (regalign),			\
		.pad_bits = (regpad),					\
		.max_register = BIT(regbits) - 1,			\
		.cache_type = REGCACHE_NONE,				\
		.read_flag_mask =					\
			KSZ_SPI_OP_FLAG_MASK(KSZ_SPI_OP_RD, swp,	\
					     regbits, regpad),		\
		.write_flag_mask =					\
			KSZ_SPI_OP_FLAG_MASK(KSZ_SPI_OP_WR, swp,	\
					     regbits, regpad),		\
		.reg_format_endian = REGMAP_ENDIAN_BIG,			\
		.val_format_endian = REGMAP_ENDIAN_BIG			\
	}

#define KSZ_REGMAP_TABLE(ksz, swp, regbits, regpad, regalign)		\
	static const struct regmap_config ksz##_regmap_config[] = {	\
		KSZ_REGMAP_ENTRY(8, swp, (regbits), (regpad), (regalign)), \
		KSZ_REGMAP_ENTRY(16, swp, (regbits), (regpad), (regalign)), \
		KSZ_REGMAP_ENTRY(32, swp, (regbits), (regpad), (regalign)), \
	}

#endif
