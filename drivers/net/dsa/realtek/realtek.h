/* SPDX-License-Identifier: GPL-2.0+ */
/* Realtek SMI interface driver defines
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 */

#ifndef _REALTEK_H
#define _REALTEK_H

#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <net/dsa.h>
#include <linux/reset.h>

#define REALTEK_HW_STOP_DELAY		25	/* msecs */
#define REALTEK_HW_START_DELAY		100	/* msecs */

struct phylink_mac_ops;
struct realtek_ops;

struct rtl8366_mib_counter {
	unsigned int	base;
	unsigned int	offset;
	unsigned int	length;
	const char	*name;
};

/*
 * struct rtl8366_vlan_mc - Virtual LAN member configuration
 */
struct rtl8366_vlan_mc {
	u16	vid;
	u16	untag;
	u16	member;
	u8	fid;
	u8	priority;
};

struct rtl8366_vlan_4k {
	u16	vid;
	u16	untag;
	u16	member;
	u8	fid;
};

struct realtek_fdb_entry {
	u8 mac_addr[ETH_ALEN];
	u16 vid;
	bool is_static;
};

struct realtek_priv {
	struct device		*dev;
	struct reset_control    *reset_ctl;
	struct gpio_desc	*reset;
	struct gpio_desc	*mdc;
	struct gpio_desc	*mdio;
	struct regmap		*map;
	struct regmap		*map_nolock;
	struct mutex		map_lock;
	/* vlan_lock protects against concurrent Read-Modify-Write operations
	 * on the global VLAN 4K and VLANMC tables, such as when adding or
	 * deleting port VLAN memberships and PVID configurations.
	 */
	struct mutex		vlan_lock;
	/* l2_lock is used to prevent concurrent modifications of L2 table
	 * entries while another function is reading it. l2_(add,del)_mc
	 * is an example that first read current table entry and then
	 * create/update it. l2_(add|del)_uc uses a single table op and,
	 * internally, it might not need this lock. However, altering FDB
	 * may still collide, as well as l2_flush, with fdb_dump iterating
	 * over FDB.
	 */
	struct mutex		l2_lock;
	struct mii_bus		*user_mii_bus;
	struct mii_bus		*bus;
	int			mdio_addr;

	const struct realtek_variant *variant;

	spinlock_t		lock; /* Locks around command writes */
	struct dsa_switch	ds;
	struct irq_domain	*irqdomain;
	bool			leds_disabled;

	unsigned int		cpu_port;
	unsigned int		num_ports;
	unsigned int		num_vlan_mc;
	unsigned int		num_mib_counters;
	struct rtl8366_mib_counter *mib_counters;

	const struct realtek_ops *ops;
	int			(*write_reg_noack)(void *ctx, u32 addr, u32 data);

	int			vlan_enabled;
	int			vlan4k_enabled;

	char			buf[4096];
	void			*chip_data; /* Per-chip extra variant data */
};

/*
 * struct realtek_ops - vtable for the per-SMI-chiptype operations
 * @detect: detects the chiptype
 */
struct realtek_ops {
	int	(*detect)(struct realtek_priv *priv);
	int	(*reset_chip)(struct realtek_priv *priv);
	int	(*setup)(struct realtek_priv *priv);
	int	(*get_mib_counter)(struct realtek_priv *priv,
				   int port,
				   struct rtl8366_mib_counter *mib,
				   u64 *mibvalue);
	int	(*get_vlan_mc)(struct realtek_priv *priv, u32 index,
			       struct rtl8366_vlan_mc *vlanmc);
	int	(*set_vlan_mc)(struct realtek_priv *priv, u32 index,
			       const struct rtl8366_vlan_mc *vlanmc);
	int	(*get_vlan_4k)(struct realtek_priv *priv, u32 vid,
			       struct rtl8366_vlan_4k *vlan4k);
	int	(*set_vlan_4k)(struct realtek_priv *priv,
			       const struct rtl8366_vlan_4k *vlan4k);
	int	(*get_mc_index)(struct realtek_priv *priv, int port, int *val);
	int	(*set_mc_index)(struct realtek_priv *priv, int port, int index);
	bool	(*is_vlan_valid)(struct realtek_priv *priv, unsigned int vlan);
	int	(*enable_vlan)(struct realtek_priv *priv, bool enable);
	int	(*enable_vlan4k)(struct realtek_priv *priv, bool enable);
	int	(*enable_port)(struct realtek_priv *priv, int port, bool enable);
	int	(*port_add_isolation)(struct realtek_priv *priv, int port,
				      u32 mask);
	int	(*port_remove_isolation)(struct realtek_priv *priv, int port,
					 u32 mask);
	int	(*port_set_efid)(struct realtek_priv *priv, int port, u32 efid);
	int	(*port_set_learning)(struct realtek_priv *priv, int port,
				     bool enable);
	int	(*port_set_ucast_flood)(struct realtek_priv *priv, int port,
					bool enable);
	int	(*port_set_mcast_flood)(struct realtek_priv *priv, int port,
					bool enable);
	int	(*port_set_bcast_flood)(struct realtek_priv *priv, int port,
					bool enable);
	int	(*l2_add_uc)(struct realtek_priv *priv, int port,
			     const unsigned char addr[ETH_ALEN],
			     u16 efid, u16 vid);
	int	(*l2_del_uc)(struct realtek_priv *priv, int port,
			     const unsigned char addr[ETH_ALEN],
			     u16 efid, u16 vid);
	int	(*l2_get_next_uc)(struct realtek_priv *priv, u16 *addr,
				  int port, struct realtek_fdb_entry *entry);
	int	(*l2_add_mc)(struct realtek_priv *priv, int port,
			     const unsigned char addr[ETH_ALEN], u16 vid);
	int	(*l2_del_mc)(struct realtek_priv *priv, int port,
			     const unsigned char addr[ETH_ALEN], u16 vid);
	int	(*l2_flush)(struct realtek_priv *priv, int port, u16 vid);
	int	(*phy_read)(struct realtek_priv *priv, int phy, int regnum);
	int	(*phy_write)(struct realtek_priv *priv, int phy, int regnum,
			     u16 val);
};

struct realtek_variant {
	const struct dsa_switch_ops *ds_ops;
	const struct realtek_ops *ops;
	const struct phylink_mac_ops *phylink_mac_ops;
	unsigned int clk_delay;
	u8 cmd_read;
	u8 cmd_write;
	size_t chip_data_sz;
};

/* RTL8366 library helpers */
int rtl8366_mc_is_used(struct realtek_priv *priv, int mc_index, int *used);
int rtl8366_set_vlan(struct realtek_priv *priv, int vid, u32 member,
		     u32 untag, u32 fid);
int rtl8366_set_pvid(struct realtek_priv *priv, unsigned int port,
		     unsigned int vid);
int rtl8366_enable_vlan4k(struct realtek_priv *priv, bool enable);
int rtl8366_enable_vlan(struct realtek_priv *priv, bool enable);
int rtl8366_reset_vlan(struct realtek_priv *priv);
int rtl8366_vlan_add(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan,
		     struct netlink_ext_ack *extack);
int rtl8366_vlan_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan);
void rtl8366_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			 uint8_t *data);
int rtl8366_get_sset_count(struct dsa_switch *ds, int port, int sset);
void rtl8366_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data);

extern const struct realtek_variant rtl8366rb_variant;
extern const struct realtek_variant rtl8365mb_variant;

#endif /*  _REALTEK_H */
