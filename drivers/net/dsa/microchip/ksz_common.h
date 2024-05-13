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
#include <linux/irq.h>
#include <linux/platform_data/microchip-ksz.h>

#include "ksz_ptp.h"

#define KSZ_MAX_NUM_PORTS 8
/* all KSZ switches count ports from 1 */
#define KSZ_PORT_1 0
#define KSZ_PORT_2 1

struct ksz_device;
struct ksz_port;
struct phylink_mac_ops;

enum ksz_regmap_width {
	KSZ_REGMAP_8,
	KSZ_REGMAP_16,
	KSZ_REGMAP_32,
	__KSZ_NUM_REGMAPS,
};

struct vlan_table {
	u32 table[3];
};

struct ksz_port_mib {
	struct mutex cnt_mutex;		/* structure access */
	u8 cnt_ptr;
	u64 *counters;
	struct rtnl_link_stats64 stats64;
	struct ethtool_pause_stats pause_stats;
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
	u8 port_nirqs;
	u8 num_tx_queues;
	u8 num_ipms; /* number of Internal Priority Maps */
	bool tc_cbs_supported;
	const struct ksz_dev_ops *ops;
	const struct phylink_mac_ops *phylink_mac_ops;
	bool ksz87xx_eee_link_erratum;
	const struct ksz_mib_names *mib_names;
	int mib_cnt;
	u8 reg_mib_cnt;
	const u16 *regs;
	const u32 *masks;
	const u8 *shifts;
	const u8 *xmii_ctrl0;
	const u8 *xmii_ctrl1;
	int stp_ctrl_reg;
	int broadcast_ctrl_reg;
	int multicast_ctrl_reg;
	int start_ctrl_reg;
	bool supports_mii[KSZ_MAX_NUM_PORTS];
	bool supports_rmii[KSZ_MAX_NUM_PORTS];
	bool supports_rgmii[KSZ_MAX_NUM_PORTS];
	bool internal_phy[KSZ_MAX_NUM_PORTS];
	bool gbit_capable[KSZ_MAX_NUM_PORTS];
	const struct regmap_access_table *wr_table;
	const struct regmap_access_table *rd_table;
};

struct ksz_irq {
	u16 masked;
	u16 reg_mask;
	u16 reg_status;
	struct irq_domain *domain;
	int nirqs;
	int irq_num;
	char name[16];
	struct ksz_device *dev;
};

struct ksz_ptp_irq {
	struct ksz_port *port;
	u16 ts_reg;
	bool ts_en;
	char name[16];
	int num;
};

struct ksz_switch_macaddr {
	unsigned char addr[ETH_ALEN];
	refcount_t refcount;
};

struct ksz_port {
	bool remove_tag;		/* Remove Tag flag set, for ksz8795 only */
	bool learning;
	bool isolated;
	int stp_state;
	struct phy_device phydev;

	u32 fiber:1;			/* port is fiber */
	u32 force:1;
	u32 read:1;			/* read MIB counters in background */
	u32 freeze:1;			/* MIB counter freeze is enabled */

	struct ksz_port_mib mib;
	phy_interface_t interface;
	u32 rgmii_tx_val;
	u32 rgmii_rx_val;
	struct ksz_device *ksz_dev;
	void *acl_priv;
	struct ksz_irq pirq;
	u8 num;
#if IS_ENABLED(CONFIG_NET_DSA_MICROCHIP_KSZ_PTP)
	struct hwtstamp_config tstamp_config;
	bool hwts_tx_en;
	bool hwts_rx_en;
	struct ksz_irq ptpirq;
	struct ksz_ptp_irq ptpmsg_irq[3];
	ktime_t tstamp_msg;
	struct completion tstamp_msg_comp;
#endif
	bool manual_flow;
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
	struct regmap *regmap[__KSZ_NUM_REGMAPS];

	void *priv;
	int irq;

	struct gpio_desc *reset_gpio;	/* Optional reset GPIO */

	/* chip specific data */
	u32 chip_id;
	u8 chip_rev;
	int cpu_port;			/* port connected to CPU */
	int phy_port_cnt;
	phy_interface_t compat_interface;
	bool synclko_125;
	bool synclko_disable;
	bool wakeup_source;

	struct vlan_table *vlan_cache;

	struct ksz_port *ports;
	struct delayed_work mib_read;
	unsigned long mib_read_interval;
	u16 mirror_rx;
	u16 mirror_tx;
	u16 port_mask;
	struct mutex lock_irq;		/* IRQ Access */
	struct ksz_irq girq;
	struct ksz_ptp_data ptp_data;

	struct ksz_switch_macaddr *switch_macaddr;
	struct net_device *hsr_dev;     /* HSR */
	u8 hsr_ports;
};

/* List of supported models */
enum ksz_model {
	KSZ8563,
	KSZ8567,
	KSZ8795,
	KSZ8794,
	KSZ8765,
	KSZ8830,
	KSZ9477,
	KSZ9896,
	KSZ9897,
	KSZ9893,
	KSZ9563,
	KSZ9567,
	LAN9370,
	LAN9371,
	LAN9372,
	LAN9373,
	LAN9374,
};

enum ksz_regs {
	REG_SW_MAC_ADDR,
	REG_IND_CTRL_0,
	REG_IND_DATA_8,
	REG_IND_DATA_CHECK,
	REG_IND_DATA_HI,
	REG_IND_DATA_LO,
	REG_IND_MIB_CHECK,
	REG_IND_BYTE,
	P_FORCE_CTRL,
	P_LINK_STATUS,
	P_LOCAL_CTRL,
	P_NEG_RESTART_CTRL,
	P_REMOTE_STATUS,
	P_SPEED_STATUS,
	S_TAIL_TAG_CTRL,
	P_STP_CTRL,
	S_START_CTRL,
	S_BROADCAST_CTRL,
	S_MULTICAST_CTRL,
	P_XMII_CTRL_0,
	P_XMII_CTRL_1,
};

enum ksz_masks {
	PORT_802_1P_REMAPPING,
	SW_TAIL_TAG_ENABLE,
	MIB_COUNTER_OVERFLOW,
	MIB_COUNTER_VALID,
	VLAN_TABLE_FID,
	VLAN_TABLE_MEMBERSHIP,
	VLAN_TABLE_VALID,
	STATIC_MAC_TABLE_VALID,
	STATIC_MAC_TABLE_USE_FID,
	STATIC_MAC_TABLE_FID,
	STATIC_MAC_TABLE_OVERRIDE,
	STATIC_MAC_TABLE_FWD_PORTS,
	DYNAMIC_MAC_TABLE_ENTRIES_H,
	DYNAMIC_MAC_TABLE_MAC_EMPTY,
	DYNAMIC_MAC_TABLE_NOT_READY,
	DYNAMIC_MAC_TABLE_ENTRIES,
	DYNAMIC_MAC_TABLE_FID,
	DYNAMIC_MAC_TABLE_SRC_PORT,
	DYNAMIC_MAC_TABLE_TIMESTAMP,
	ALU_STAT_WRITE,
	ALU_STAT_READ,
	P_MII_TX_FLOW_CTRL,
	P_MII_RX_FLOW_CTRL,
};

enum ksz_shifts {
	VLAN_TABLE_MEMBERSHIP_S,
	VLAN_TABLE,
	STATIC_MAC_FWD_PORTS,
	STATIC_MAC_FID,
	DYNAMIC_MAC_ENTRIES_H,
	DYNAMIC_MAC_ENTRIES,
	DYNAMIC_MAC_FID,
	DYNAMIC_MAC_TIMESTAMP,
	DYNAMIC_MAC_SRC_PORT,
	ALU_STAT_INDEX,
};

enum ksz_xmii_ctrl0 {
	P_MII_100MBIT,
	P_MII_10MBIT,
	P_MII_FULL_DUPLEX,
	P_MII_HALF_DUPLEX,
};

enum ksz_xmii_ctrl1 {
	P_RGMII_SEL,
	P_RMII_SEL,
	P_GMII_SEL,
	P_MII_SEL,
	P_GMII_1GBIT,
	P_GMII_NOT_1GBIT,
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
	int (*setup)(struct dsa_switch *ds);
	void (*teardown)(struct dsa_switch *ds);
	u32 (*get_port_addr)(int port, int offset);
	void (*cfg_port_member)(struct ksz_device *dev, int port, u8 member);
	void (*flush_dyn_mac_table)(struct ksz_device *dev, int port);
	void (*port_cleanup)(struct ksz_device *dev, int port);
	void (*port_setup)(struct ksz_device *dev, int port, bool cpu_port);
	int (*set_ageing_time)(struct ksz_device *dev, unsigned int msecs);
	int (*r_phy)(struct ksz_device *dev, u16 phy, u16 reg, u16 *val);
	int (*w_phy)(struct ksz_device *dev, u16 phy, u16 reg, u16 val);
	void (*r_mib_cnt)(struct ksz_device *dev, int port, u16 addr,
			  u64 *cnt);
	void (*r_mib_pkt)(struct ksz_device *dev, int port, u16 addr,
			  u64 *dropped, u64 *cnt);
	void (*r_mib_stat64)(struct ksz_device *dev, int port);
	int  (*vlan_filtering)(struct ksz_device *dev, int port,
			       bool flag, struct netlink_ext_ack *extack);
	int  (*vlan_add)(struct ksz_device *dev, int port,
			 const struct switchdev_obj_port_vlan *vlan,
			 struct netlink_ext_ack *extack);
	int  (*vlan_del)(struct ksz_device *dev, int port,
			 const struct switchdev_obj_port_vlan *vlan);
	int (*mirror_add)(struct ksz_device *dev, int port,
			  struct dsa_mall_mirror_tc_entry *mirror,
			  bool ingress, struct netlink_ext_ack *extack);
	void (*mirror_del)(struct ksz_device *dev, int port,
			   struct dsa_mall_mirror_tc_entry *mirror);
	int (*fdb_add)(struct ksz_device *dev, int port,
		       const unsigned char *addr, u16 vid, struct dsa_db db);
	int (*fdb_del)(struct ksz_device *dev, int port,
		       const unsigned char *addr, u16 vid, struct dsa_db db);
	int (*fdb_dump)(struct ksz_device *dev, int port,
			dsa_fdb_dump_cb_t *cb, void *data);
	int (*mdb_add)(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_mdb *mdb,
		       struct dsa_db db);
	int (*mdb_del)(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_mdb *mdb,
		       struct dsa_db db);
	void (*get_caps)(struct ksz_device *dev, int port,
			 struct phylink_config *config);
	int (*change_mtu)(struct ksz_device *dev, int port, int mtu);
	void (*freeze_mib)(struct ksz_device *dev, int port, bool freeze);
	void (*port_init_cnt)(struct ksz_device *dev, int port);
	void (*phylink_mac_link_up)(struct ksz_device *dev, int port,
				    unsigned int mode,
				    phy_interface_t interface,
				    struct phy_device *phydev, int speed,
				    int duplex, bool tx_pause, bool rx_pause);
	void (*setup_rgmii_delay)(struct ksz_device *dev, int port);
	int (*tc_cbs_set_cinc)(struct ksz_device *dev, int port, u32 val);
	void (*get_wol)(struct ksz_device *dev, int port,
			struct ethtool_wolinfo *wol);
	int (*set_wol)(struct ksz_device *dev, int port,
		       struct ethtool_wolinfo *wol);
	void (*wol_pre_shutdown)(struct ksz_device *dev, bool *wol_enabled);
	void (*config_cpu_port)(struct dsa_switch *ds);
	int (*enable_stp_addr)(struct ksz_device *dev);
	int (*reset)(struct ksz_device *dev);
	int (*init)(struct ksz_device *dev);
	void (*exit)(struct ksz_device *dev);
};

struct ksz_device *ksz_switch_alloc(struct device *base, void *priv);
int ksz_switch_register(struct ksz_device *dev);
void ksz_switch_remove(struct ksz_device *dev);

void ksz_init_mib_timer(struct ksz_device *dev);
bool ksz_is_port_mac_global_usable(struct dsa_switch *ds, int port);
void ksz_r_mib_stats64(struct ksz_device *dev, int port);
void ksz88xx_r_mib_stats64(struct ksz_device *dev, int port);
void ksz_port_stp_state_set(struct dsa_switch *ds, int port, u8 state);
bool ksz_get_gbit(struct ksz_device *dev, int port);
phy_interface_t ksz_get_xmii(struct ksz_device *dev, int port, bool gbit);
extern const struct ksz_chip_data ksz_switch_chips[];
int ksz_switch_macaddr_get(struct dsa_switch *ds, int port,
			   struct netlink_ext_ack *extack);
void ksz_switch_macaddr_put(struct dsa_switch *ds);
void ksz_switch_shutdown(struct ksz_device *dev);

/* Common register access functions */
static inline struct regmap *ksz_regmap_8(struct ksz_device *dev)
{
	return dev->regmap[KSZ_REGMAP_8];
}

static inline struct regmap *ksz_regmap_16(struct ksz_device *dev)
{
	return dev->regmap[KSZ_REGMAP_16];
}

static inline struct regmap *ksz_regmap_32(struct ksz_device *dev)
{
	return dev->regmap[KSZ_REGMAP_32];
}

static inline int ksz_read8(struct ksz_device *dev, u32 reg, u8 *val)
{
	unsigned int value;
	int ret = regmap_read(ksz_regmap_8(dev), reg, &value);

	if (ret)
		dev_err(dev->dev, "can't read 8bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	*val = value;
	return ret;
}

static inline int ksz_read16(struct ksz_device *dev, u32 reg, u16 *val)
{
	unsigned int value;
	int ret = regmap_read(ksz_regmap_16(dev), reg, &value);

	if (ret)
		dev_err(dev->dev, "can't read 16bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	*val = value;
	return ret;
}

static inline int ksz_read32(struct ksz_device *dev, u32 reg, u32 *val)
{
	unsigned int value;
	int ret = regmap_read(ksz_regmap_32(dev), reg, &value);

	if (ret)
		dev_err(dev->dev, "can't read 32bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	*val = value;
	return ret;
}

static inline int ksz_read64(struct ksz_device *dev, u32 reg, u64 *val)
{
	u32 value[2];
	int ret;

	ret = regmap_bulk_read(ksz_regmap_32(dev), reg, value, 2);
	if (ret)
		dev_err(dev->dev, "can't read 64bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));
	else
		*val = (u64)value[0] << 32 | value[1];

	return ret;
}

static inline int ksz_write8(struct ksz_device *dev, u32 reg, u8 value)
{
	int ret;

	ret = regmap_write(ksz_regmap_8(dev), reg, value);
	if (ret)
		dev_err(dev->dev, "can't write 8bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_write16(struct ksz_device *dev, u32 reg, u16 value)
{
	int ret;

	ret = regmap_write(ksz_regmap_16(dev), reg, value);
	if (ret)
		dev_err(dev->dev, "can't write 16bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_write32(struct ksz_device *dev, u32 reg, u32 value)
{
	int ret;

	ret = regmap_write(ksz_regmap_32(dev), reg, value);
	if (ret)
		dev_err(dev->dev, "can't write 32bit reg: 0x%x %pe\n", reg,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_rmw16(struct ksz_device *dev, u32 reg, u16 mask,
			    u16 value)
{
	int ret;

	ret = regmap_update_bits(ksz_regmap_16(dev), reg, mask, value);
	if (ret)
		dev_err(dev->dev, "can't rmw 16bit reg 0x%x: %pe\n", reg,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_rmw32(struct ksz_device *dev, u32 reg, u32 mask,
			    u32 value)
{
	int ret;

	ret = regmap_update_bits(ksz_regmap_32(dev), reg, mask, value);
	if (ret)
		dev_err(dev->dev, "can't rmw 32bit reg 0x%x: %pe\n", reg,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_write64(struct ksz_device *dev, u32 reg, u64 value)
{
	u32 val[2];

	/* Ick! ToDo: Add 64bit R/W to regmap on 32bit systems */
	value = swab64(value);
	val[0] = swab32(value & 0xffffffffULL);
	val[1] = swab32(value >> 32ULL);

	return regmap_bulk_write(ksz_regmap_32(dev), reg, val, 2);
}

static inline int ksz_rmw8(struct ksz_device *dev, int offset, u8 mask, u8 val)
{
	int ret;

	ret = regmap_update_bits(ksz_regmap_8(dev), offset, mask, val);
	if (ret)
		dev_err(dev->dev, "can't rmw 8bit reg 0x%x: %pe\n", offset,
			ERR_PTR(ret));

	return ret;
}

static inline int ksz_pread8(struct ksz_device *dev, int port, int offset,
			     u8 *data)
{
	return ksz_read8(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline int ksz_pread16(struct ksz_device *dev, int port, int offset,
			      u16 *data)
{
	return ksz_read16(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline int ksz_pread32(struct ksz_device *dev, int port, int offset,
			      u32 *data)
{
	return ksz_read32(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline int ksz_pwrite8(struct ksz_device *dev, int port, int offset,
			      u8 data)
{
	return ksz_write8(dev, dev->dev_ops->get_port_addr(port, offset), data);
}

static inline int ksz_pwrite16(struct ksz_device *dev, int port, int offset,
			       u16 data)
{
	return ksz_write16(dev, dev->dev_ops->get_port_addr(port, offset),
			   data);
}

static inline int ksz_pwrite32(struct ksz_device *dev, int port, int offset,
			       u32 data)
{
	return ksz_write32(dev, dev->dev_ops->get_port_addr(port, offset),
			   data);
}

static inline int ksz_prmw8(struct ksz_device *dev, int port, int offset,
			    u8 mask, u8 val)
{
	return ksz_rmw8(dev, dev->dev_ops->get_port_addr(port, offset),
			mask, val);
}

static inline int ksz_prmw32(struct ksz_device *dev, int port, int offset,
			     u32 mask, u32 val)
{
	return ksz_rmw32(dev, dev->dev_ops->get_port_addr(port, offset),
			 mask, val);
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

static inline bool ksz_is_ksz87xx(struct ksz_device *dev)
{
	return dev->chip_id == KSZ8795_CHIP_ID ||
	       dev->chip_id == KSZ8794_CHIP_ID ||
	       dev->chip_id == KSZ8765_CHIP_ID;
}

static inline bool ksz_is_ksz88x3(struct ksz_device *dev)
{
	return dev->chip_id == KSZ8830_CHIP_ID;
}

static inline bool is_ksz8(struct ksz_device *dev)
{
	return ksz_is_ksz87xx(dev) || ksz_is_ksz88x3(dev);
}

static inline int is_lan937x(struct ksz_device *dev)
{
	return dev->chip_id == LAN9370_CHIP_ID ||
		dev->chip_id == LAN9371_CHIP_ID ||
		dev->chip_id == LAN9372_CHIP_ID ||
		dev->chip_id == LAN9373_CHIP_ID ||
		dev->chip_id == LAN9374_CHIP_ID;
}

/* STP State Defines */
#define PORT_TX_ENABLE			BIT(2)
#define PORT_RX_ENABLE			BIT(1)
#define PORT_LEARN_DISABLE		BIT(0)

/* Switch ID Defines */
#define REG_CHIP_ID0			0x00

#define SW_FAMILY_ID_M			GENMASK(15, 8)
#define KSZ87_FAMILY_ID			0x87
#define KSZ88_FAMILY_ID			0x88

#define KSZ8_PORT_STATUS_0		0x08
#define KSZ8_PORT_FIBER_MODE		BIT(7)

#define SW_CHIP_ID_M			GENMASK(7, 4)
#define KSZ87_CHIP_ID_94		0x6
#define KSZ87_CHIP_ID_95		0x9
#define KSZ88_CHIP_ID_63		0x3

#define SW_REV_ID_M			GENMASK(7, 4)

/* KSZ9893, KSZ9563, KSZ8563 specific register  */
#define REG_CHIP_ID4			0x0f
#define SKU_ID_KSZ8563			0x3c
#define SKU_ID_KSZ9563			0x1c

/* Driver set switch broadcast storm protection at 10% rate. */
#define BROADCAST_STORM_PROT_RATE	10

/* 148,800 frames * 67 ms / 100 */
#define BROADCAST_STORM_VALUE		9969

#define BROADCAST_STORM_RATE_HI		0x07
#define BROADCAST_STORM_RATE_LO		0xFF
#define BROADCAST_STORM_RATE		0x07FF

#define MULTICAST_STORM_DISABLE		BIT(6)

#define SW_START			0x01

/* xMII configuration */
#define P_MII_DUPLEX_M			BIT(6)
#define P_MII_100MBIT_M			BIT(4)

#define P_GMII_1GBIT_M			BIT(6)
#define P_RGMII_ID_IG_ENABLE		BIT(4)
#define P_RGMII_ID_EG_ENABLE		BIT(3)
#define P_MII_MAC_MODE			BIT(2)
#define P_MII_SEL_M			0x3

/* Interrupt */
#define REG_SW_PORT_INT_STATUS__1	0x001B
#define REG_SW_PORT_INT_MASK__1		0x001F

#define REG_PORT_INT_STATUS		0x001B
#define REG_PORT_INT_MASK		0x001F

#define PORT_SRC_PHY_INT		1
#define PORT_SRC_PTP_INT		2

#define KSZ8795_HUGE_PACKET_SIZE	2000
#define KSZ8863_HUGE_PACKET_SIZE	1916
#define KSZ8863_NORMAL_PACKET_SIZE	1536
#define KSZ8_LEGAL_PACKET_SIZE		1518
#define KSZ9477_MAX_FRAME_SIZE		9000

#define KSZ8873_REG_GLOBAL_CTRL_12	0x0e
/* Drive Strength of I/O Pad
 * 0: 8mA, 1: 16mA
 */
#define KSZ8873_DRIVE_STRENGTH_16MA	BIT(6)

#define KSZ8795_REG_SW_CTRL_20		0xa3
#define KSZ9477_REG_SW_IO_STRENGTH	0x010d
#define SW_DRIVE_STRENGTH_M		0x7
#define SW_DRIVE_STRENGTH_2MA		0
#define SW_DRIVE_STRENGTH_4MA		1
#define SW_DRIVE_STRENGTH_8MA		2
#define SW_DRIVE_STRENGTH_12MA		3
#define SW_DRIVE_STRENGTH_16MA		4
#define SW_DRIVE_STRENGTH_20MA		5
#define SW_DRIVE_STRENGTH_24MA		6
#define SW_DRIVE_STRENGTH_28MA		7
#define SW_HI_SPEED_DRIVE_STRENGTH_S	4
#define SW_LO_SPEED_DRIVE_STRENGTH_S	0

#define KSZ9477_REG_PORT_OUT_RATE_0	0x0420
#define KSZ9477_OUT_RATE_NO_LIMIT	0

#define KSZ9477_PORT_MRI_TC_MAP__4	0x0808

#define KSZ9477_PORT_TC_MAP_S		4

/* CBS related registers */
#define REG_PORT_MTI_QUEUE_INDEX__4	0x0900

#define REG_PORT_MTI_QUEUE_CTRL_0	0x0914

#define MTI_SCHEDULE_MODE_M		GENMASK(7, 6)
#define MTI_SCHEDULE_STRICT_PRIO	0
#define MTI_SCHEDULE_WRR		2
#define MTI_SHAPING_M			GENMASK(5, 4)
#define MTI_SHAPING_OFF			0
#define MTI_SHAPING_SRP			1
#define MTI_SHAPING_TIME_AWARE		2

#define KSZ9477_PORT_MTI_QUEUE_CTRL_1	0x0915
#define KSZ9477_DEFAULT_WRR_WEIGHT	1

#define REG_PORT_MTI_HI_WATER_MARK	0x0916
#define REG_PORT_MTI_LO_WATER_MARK	0x0918

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
		[KSZ_REGMAP_8] = KSZ_REGMAP_ENTRY(8, swp, (regbits), (regpad), (regalign)), \
		[KSZ_REGMAP_16] = KSZ_REGMAP_ENTRY(16, swp, (regbits), (regpad), (regalign)), \
		[KSZ_REGMAP_32] = KSZ_REGMAP_ENTRY(32, swp, (regbits), (regpad), (regalign)), \
	}

#endif
