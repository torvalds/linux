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

#define REALTEK_HW_STOP_DELAY		25	/* msecs */
#define REALTEK_HW_START_DELAY		100	/* msecs */

struct realtek_ops;
struct dentry;
struct inode;
struct file;

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

struct realtek_priv {
	struct device		*dev;
	struct gpio_desc	*reset;
	struct gpio_desc	*mdc;
	struct gpio_desc	*mdio;
	struct regmap		*map;
	struct regmap		*map_nolock;
	struct mutex		map_lock;
	struct mii_bus		*user_mii_bus;
	struct mii_bus		*bus;
	int			mdio_addr;

	unsigned int		clk_delay;
	u8			cmd_read;
	u8			cmd_write;
	spinlock_t		lock; /* Locks around command writes */
	struct dsa_switch	*ds;
	struct irq_domain	*irqdomain;
	bool			leds_disabled;

	unsigned int		cpu_port;
	unsigned int		num_ports;
	unsigned int		num_vlan_mc;
	unsigned int		num_mib_counters;
	struct rtl8366_mib_counter *mib_counters;

	const struct realtek_ops *ops;
	int			(*setup_interface)(struct dsa_switch *ds);
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
	void	(*cleanup)(struct realtek_priv *priv);
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
	int	(*phy_read)(struct realtek_priv *priv, int phy, int regnum);
	int	(*phy_write)(struct realtek_priv *priv, int phy, int regnum,
			     u16 val);
};

struct realtek_variant {
	const struct dsa_switch_ops *ds_ops_smi;
	const struct dsa_switch_ops *ds_ops_mdio;
	const struct realtek_ops *ops;
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
