/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip switch driver common header
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#ifndef __KSZ_COMMON_H
#define __KSZ_COMMON_H

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <net/dsa.h>

#define KSZ_MAX_NUM_PORTS 8

struct vlan_table {
	u32 table[3];
};

struct ksz_port_mib {
	struct mutex cnt_mutex;		/* structure access */
	u8 cnt_ptr;
	u64 *counters;
	struct rtnl_link_stats64 stats64;
	struct spinlock stats64_lock;
};

struct ksz_mib_names {
	int index;
	char string[ETH_GSTRING_LEN];
};

struct ksz_chip_data {
	u32 chip_id;
	const char *dev_name;
	int num_vlans;
	int num_alus;
	int num_statics;
	int cpu_ports;
	int port_cnt;
	bool phy_errata_9477;
	bool ksz87xx_eee_link_erratum;
	const struct ksz_mib_names *mib_names;
	int mib_cnt;
	u8 reg_mib_cnt;
	bool supports_mii[KSZ_MAX_NUM_PORTS];
	bool supports_rmii[KSZ_MAX_NUM_PORTS];
	bool supports_rgmii[KSZ_MAX_NUM_PORTS];
	bool internal_phy[KSZ_MAX_NUM_PORTS];
};

struct ksz_port {
	bool remove_tag;		/* Remove Tag flag set, for ksz8795 only */
	int stp_state;
	struct phy_device phydev;

	u32 on:1;			/* port is not disabled by hardware */
	u32 phy:1;			/* port has a PHY */
	u32 fiber:1;			/* port is fiber */
	u32 sgmii:1;			/* port is SGMII */
	u32 force:1;
	u32 read:1;			/* read MIB counters in background */
	u32 freeze:1;			/* MIB counter freeze is enabled */

	struct ksz_port_mib mib;
	phy_interface_t interface;
	u16 max_frame;
};

struct ksz_device {
	struct dsa_switch *ds;
	struct ksz_platform_data *pdata;
	const struct ksz_chip_data *info;

	struct mutex dev_mutex;		/* device access */
	struct mutex regmap_mutex;	/* regmap access */
	struct mutex alu_mutex;		/* ALU access */
	struct mutex vlan_mutex;	/* vlan access */
	const struct ksz_dev_ops *dev_ops;

	struct device *dev;
	struct regmap *regmap[3];

	void *priv;

	struct gpio_desc *reset_gpio;	/* Optional reset GPIO */

	/* chip specific data */
	u32 chip_id;
	int cpu_port;			/* port connected to CPU */
	int phy_port_cnt;
	phy_interface_t compat_interface;
	bool synclko_125;
	bool synclko_disable;

	struct vlan_table *vlan_cache;

	struct ksz_port *ports;
	struct delayed_work mib_read;
	unsigned long mib_read_interval;
	u16 mirror_rx;
	u16 mirror_tx;
	u32 features;			/* chip specific features */
	u16 port_mask;
};

/* List of supported models */
enum ksz_model {
	KSZ8795,
	KSZ8794,
	KSZ8765,
	KSZ8830,
	KSZ9477,
	KSZ9897,
	KSZ9893,
	KSZ9567,
	LAN9370,
	LAN9371,
	LAN9372,
	LAN9373,
	LAN9374,
};

enum ksz_chip_id {
	KSZ8795_CHIP_ID = 0x8795,
	KSZ8794_CHIP_ID = 0x8794,
	KSZ8765_CHIP_ID = 0x8765,
	KSZ8830_CHIP_ID = 0x8830,
	KSZ9477_CHIP_ID = 0x00947700,
	KSZ9897_CHIP_ID = 0x00989700,
	KSZ9893_CHIP_ID = 0x00989300,
	KSZ9567_CHIP_ID = 0x00956700,
	LAN9370_CHIP_ID = 0x00937000,
	LAN9371_CHIP_ID = 0x00937100,
	LAN9372_CHIP_ID = 0x00937200,
	LAN9373_CHIP_ID = 0x00937300,
	LAN9374_CHIP_ID = 0x00937400,
};

struct alu_struct {
	/* entry 1 */
	u8	is_static:1;
	u8	is_src_filter:1;
	u8	is_dst_filter:1;
	u8	prio_age:3;
	u32	_reserv_0_1:23;
	u8	mstp:3;
	/* entry 2 */
	u8	is_override:1;
	u8	is_use_fid:1;
	u32	_reserv_1_1:23;
	u8	port_forward:7;
	/* entry 3 & 4*/
	u32	_reserv_2_1:9;
	u8	fid:7;
	u8	mac[ETH_ALEN];
};

struct ksz_dev_ops {
	u32 (*get_port_addr)(int port, int offset);
	void (*cfg_port_member)(struct ksz_device *dev, int port, u8 member);
	void (*flush_dyn_mac_table)(struct ksz_device *dev, int port);
	void (*port_cleanup)(struct ksz_device *dev, int port);
	void (*port_setup)(struct ksz_device *dev, int port, bool cpu_port);
	void (*r_phy)(struct ksz_device *dev, u16 phy, u16 reg, u16 *val);
	void (*w_phy)(struct ksz_device *dev, u16 phy, u16 reg, u16 val);
	int (*r_dyn_mac_table)(struct ksz_device *dev, u16 addr, u8 *mac_addr,
			       u8 *fid, u8 *src_port, u8 *timestamp,
			       u16 *entries);
	int (*r_sta_mac_table)(struct ksz_device *dev, u16 addr,
			       struct alu_struct *alu);
	void (*w_sta_mac_table)(struct ksz_device *dev, u16 addr,
				struct alu_struct *alu);
	void (*r_mib_cnt)(struct ksz_device *dev, int port, u16 addr,
			  u64 *cnt);
	void (*r_mib_pkt)(struct ksz_device *dev, int port, u16 addr,
			  u64 *dropped, u64 *cnt);
	void (*r_mib_stat64)(struct ksz_device *dev, int port);
	void (*freeze_mib)(struct ksz_device *dev, int port, bool freeze);
	void (*port_init_cnt)(struct ksz_device *dev, int port);
	int (*shutdown)(struct ksz_device *dev);
	int (*detect)(struct ksz_device *dev);
	int (*init)(struct ksz_device *dev);
	void (*exit)(struct ksz_device *dev);
};

struct ksz_device *ksz_switch_alloc(struct device *base, void *priv);
int ksz_switch_register(struct ksz_device *dev,
			const struct ksz_dev_ops *ops);
void ksz_switch_remove(struct ksz_device *dev);

int ksz8_switch_register(struct ksz_device *dev);
int ksz9477_switch_register(struct ksz_device *dev);

void ksz_update_port_member(struct ksz_device *dev, int port);
void ksz_init_mib_timer(struct ksz_device *dev);
void ksz_r_mib_stats64(struct ksz_device *dev, int port);
void ksz_get_stats64(struct dsa_switch *ds, int port,
		     struct rtnl_link_stats64 *s);
void ksz_phylink_get_caps(struct dsa_switch *ds, int port,
			  struct phylink_config *config);
extern const struct ksz_chip_data ksz_switch_chips[];

/* Common DSA access functions */

int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg);
int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val);
void ksz_mac_link_down(struct dsa_switch *ds, int port, unsigned int mode,
		       phy_interface_t interface);
int ksz_sset_count(struct dsa_switch *ds, int port, int sset);
void ksz_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *buf);
int ksz_port_bridge_join(struct dsa_switch *ds, int port,
			 struct dsa_bridge bridge, bool *tx_fwd_offload,
			 struct netlink_ext_ack *extack);
void ksz_port_bridge_leave(struct dsa_switch *ds, int port,
			   struct dsa_bridge bridge);
void ksz_port_stp_state_set(struct dsa_switch *ds, int port,
			    u8 state, int reg);
void ksz_port_fast_age(struct dsa_switch *ds, int port);
int ksz_port_fdb_dump(struct dsa_switch *ds, int port, dsa_fdb_dump_cb_t *cb,
		      void *data);
int ksz_port_mdb_add(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct dsa_db db);
int ksz_port_mdb_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct dsa_db db);
int ksz_enable_port(struct dsa_switch *ds, int port, struct phy_device *phy);
void ksz_get_strings(struct dsa_switch *ds, int port,
		     u32 stringset, uint8_t *buf);

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

static inline int ksz_read64(struct ksz_device *dev, u32 reg, u64 *val)
{
	u32 value[2];
	int ret;

	ret = regmap_bulk_read(dev->regmap[2], reg, value, 2);
	if (!ret)
		*val = (u64)value[0] << 32 | value[1];

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

static inline int ksz_write64(struct ksz_device *dev, u32 reg, u64 value)
{
	u32 val[2];

	/* Ick! ToDo: Add 64bit R/W to regmap on 32bit systems */
	value = swab64(value);
	val[0] = swab32(value & 0xffffffffULL);
	val[1] = swab32(value >> 32ULL);

	return regmap_bulk_write(dev->regmap[2], reg, val, 2);
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

static inline void ksz_regmap_lock(void *__mtx)
{
	struct mutex *mtx = __mtx;
	mutex_lock(mtx);
}

static inline void ksz_regmap_unlock(void *__mtx)
{
	struct mutex *mtx = __mtx;
	mutex_unlock(mtx);
}

/* STP State Defines */
#define PORT_TX_ENABLE			BIT(2)
#define PORT_RX_ENABLE			BIT(1)
#define PORT_LEARN_DISABLE		BIT(0)

/* Regmap tables generation */
#define KSZ_SPI_OP_RD		3
#define KSZ_SPI_OP_WR		2

#define swabnot_used(x)		0

#define KSZ_SPI_OP_FLAG_MASK(opcode, swp, regbits, regpad)		\
	swab##swp((opcode) << ((regbits) + (regpad)))

#define KSZ_REGMAP_ENTRY(width, swp, regbits, regpad, regalign)		\
	{								\
		.name = #width,						\
		.val_bits = (width),					\
		.reg_stride = 1,					\
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
		.lock = ksz_regmap_lock,				\
		.unlock = ksz_regmap_unlock,				\
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
