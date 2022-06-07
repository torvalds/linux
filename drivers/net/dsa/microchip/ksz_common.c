// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip switch driver main logic
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "ksz_common.h"

#define MIB_COUNTER_NUM 0x20

struct ksz_stats_raw {
	u64 rx_hi;
	u64 rx_undersize;
	u64 rx_fragments;
	u64 rx_oversize;
	u64 rx_jabbers;
	u64 rx_symbol_err;
	u64 rx_crc_err;
	u64 rx_align_err;
	u64 rx_mac_ctrl;
	u64 rx_pause;
	u64 rx_bcast;
	u64 rx_mcast;
	u64 rx_ucast;
	u64 rx_64_or_less;
	u64 rx_65_127;
	u64 rx_128_255;
	u64 rx_256_511;
	u64 rx_512_1023;
	u64 rx_1024_1522;
	u64 rx_1523_2000;
	u64 rx_2001;
	u64 tx_hi;
	u64 tx_late_col;
	u64 tx_pause;
	u64 tx_bcast;
	u64 tx_mcast;
	u64 tx_ucast;
	u64 tx_deferred;
	u64 tx_total_col;
	u64 tx_exc_col;
	u64 tx_single_col;
	u64 tx_mult_col;
	u64 rx_total;
	u64 tx_total;
	u64 rx_discards;
	u64 tx_discards;
};

static const struct ksz_mib_names ksz88xx_mib_names[] = {
	{ 0x00, "rx" },
	{ 0x01, "rx_hi" },
	{ 0x02, "rx_undersize" },
	{ 0x03, "rx_fragments" },
	{ 0x04, "rx_oversize" },
	{ 0x05, "rx_jabbers" },
	{ 0x06, "rx_symbol_err" },
	{ 0x07, "rx_crc_err" },
	{ 0x08, "rx_align_err" },
	{ 0x09, "rx_mac_ctrl" },
	{ 0x0a, "rx_pause" },
	{ 0x0b, "rx_bcast" },
	{ 0x0c, "rx_mcast" },
	{ 0x0d, "rx_ucast" },
	{ 0x0e, "rx_64_or_less" },
	{ 0x0f, "rx_65_127" },
	{ 0x10, "rx_128_255" },
	{ 0x11, "rx_256_511" },
	{ 0x12, "rx_512_1023" },
	{ 0x13, "rx_1024_1522" },
	{ 0x14, "tx" },
	{ 0x15, "tx_hi" },
	{ 0x16, "tx_late_col" },
	{ 0x17, "tx_pause" },
	{ 0x18, "tx_bcast" },
	{ 0x19, "tx_mcast" },
	{ 0x1a, "tx_ucast" },
	{ 0x1b, "tx_deferred" },
	{ 0x1c, "tx_total_col" },
	{ 0x1d, "tx_exc_col" },
	{ 0x1e, "tx_single_col" },
	{ 0x1f, "tx_mult_col" },
	{ 0x100, "rx_discards" },
	{ 0x101, "tx_discards" },
};

static const struct ksz_mib_names ksz9477_mib_names[] = {
	{ 0x00, "rx_hi" },
	{ 0x01, "rx_undersize" },
	{ 0x02, "rx_fragments" },
	{ 0x03, "rx_oversize" },
	{ 0x04, "rx_jabbers" },
	{ 0x05, "rx_symbol_err" },
	{ 0x06, "rx_crc_err" },
	{ 0x07, "rx_align_err" },
	{ 0x08, "rx_mac_ctrl" },
	{ 0x09, "rx_pause" },
	{ 0x0A, "rx_bcast" },
	{ 0x0B, "rx_mcast" },
	{ 0x0C, "rx_ucast" },
	{ 0x0D, "rx_64_or_less" },
	{ 0x0E, "rx_65_127" },
	{ 0x0F, "rx_128_255" },
	{ 0x10, "rx_256_511" },
	{ 0x11, "rx_512_1023" },
	{ 0x12, "rx_1024_1522" },
	{ 0x13, "rx_1523_2000" },
	{ 0x14, "rx_2001" },
	{ 0x15, "tx_hi" },
	{ 0x16, "tx_late_col" },
	{ 0x17, "tx_pause" },
	{ 0x18, "tx_bcast" },
	{ 0x19, "tx_mcast" },
	{ 0x1A, "tx_ucast" },
	{ 0x1B, "tx_deferred" },
	{ 0x1C, "tx_total_col" },
	{ 0x1D, "tx_exc_col" },
	{ 0x1E, "tx_single_col" },
	{ 0x1F, "tx_mult_col" },
	{ 0x80, "rx_total" },
	{ 0x81, "tx_total" },
	{ 0x82, "rx_discards" },
	{ 0x83, "tx_discards" },
};

const struct ksz_chip_data ksz_switch_chips[] = {
	[KSZ8795] = {
		.chip_id = KSZ8795_CHIP_ID,
		.dev_name = "KSZ8795",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[KSZ8794] = {
		/* WARNING
		 * =======
		 * KSZ8794 is similar to KSZ8795, except the port map
		 * contains a gap between external and CPU ports, the
		 * port map is NOT continuous. The per-port register
		 * map is shifted accordingly too, i.e. registers at
		 * offset 0x40 are NOT used on KSZ8794 and they ARE
		 * used on KSZ8795 for external port 3.
		 *           external  cpu
		 * KSZ8794   0,1,2      4
		 * KSZ8795   0,1,2,3    4
		 * KSZ8765   0,1,2,3    4
		 * port_cnt is configured as 5, even though it is 4
		 */
		.chip_id = KSZ8794_CHIP_ID,
		.dev_name = "KSZ8794",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, false, false},
	},

	[KSZ8765] = {
		.chip_id = KSZ8765_CHIP_ID,
		.dev_name = "KSZ8765",
		.num_vlans = 4096,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total cpu and user ports */
		.ksz87xx_eee_link_erratum = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[KSZ8830] = {
		.chip_id = KSZ8830_CHIP_ID,
		.dev_name = "KSZ8863/KSZ8873",
		.num_vlans = 16,
		.num_alus = 0,
		.num_statics = 8,
		.cpu_ports = 0x4,	/* can be configured as cpu port */
		.port_cnt = 3,
		.mib_names = ksz88xx_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz88xx_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.internal_phy = {true, true, false},
	},

	[KSZ9477] = {
		.chip_id = KSZ9477_CHIP_ID,
		.dev_name = "KSZ9477",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.phy_errata_9477 = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   false, true, false},
		.supports_rmii	= {false, false, false, false,
				   false, true, false},
		.supports_rgmii = {false, false, false, false,
				   false, true, false},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
	},

	[KSZ9897] = {
		.chip_id = KSZ9897_CHIP_ID,
		.dev_name = "KSZ9897",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.phy_errata_9477 = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   false, true, true},
		.supports_rmii	= {false, false, false, false,
				   false, true, true},
		.supports_rgmii = {false, false, false, false,
				   false, true, true},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
	},

	[KSZ9893] = {
		.chip_id = KSZ9893_CHIP_ID,
		.dev_name = "KSZ9893",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x07,	/* can be configured as cpu port */
		.port_cnt = 3,		/* total port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, true},
		.supports_rmii = {false, false, true},
		.supports_rgmii = {false, false, true},
		.internal_phy = {true, true, false},
	},

	[KSZ9567] = {
		.chip_id = KSZ9567_CHIP_ID,
		.dev_name = "KSZ9567",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
		.phy_errata_9477 = true,
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   false, true, true},
		.supports_rmii	= {false, false, false, false,
				   false, true, true},
		.supports_rgmii = {false, false, false, false,
				   false, true, true},
		.internal_phy	= {true, true, true, true,
				   true, false, false},
	},

	[LAN9370] = {
		.chip_id = LAN9370_CHIP_ID,
		.dev_name = "LAN9370",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x10,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total physical port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, false, false, true},
		.supports_rmii = {false, false, false, false, true},
		.supports_rgmii = {false, false, false, false, true},
		.internal_phy = {true, true, true, true, false},
	},

	[LAN9371] = {
		.chip_id = LAN9371_CHIP_ID,
		.dev_name = "LAN9371",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 6,		/* total physical port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii = {false, false, false, false, true, true},
		.supports_rmii = {false, false, false, false, true, true},
		.supports_rgmii = {false, false, false, false, true, true},
		.internal_phy = {true, true, true, true, false, false},
	},

	[LAN9372] = {
		.chip_id = LAN9372_CHIP_ID,
		.dev_name = "LAN9372",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 8,		/* total physical port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, true,
				   false, false, true, true},
	},

	[LAN9373] = {
		.chip_id = LAN9373_CHIP_ID,
		.dev_name = "LAN9373",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x38,	/* can be configured as cpu port */
		.port_cnt = 5,		/* total physical port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, false,
				   false, false, true, true},
	},

	[LAN9374] = {
		.chip_id = LAN9374_CHIP_ID,
		.dev_name = "LAN9374",
		.num_vlans = 4096,
		.num_alus = 1024,
		.num_statics = 256,
		.cpu_ports = 0x30,	/* can be configured as cpu port */
		.port_cnt = 8,		/* total physical port count */
		.mib_names = ksz9477_mib_names,
		.mib_cnt = ARRAY_SIZE(ksz9477_mib_names),
		.reg_mib_cnt = MIB_COUNTER_NUM,
		.supports_mii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rmii	= {false, false, false, false,
				   true, true, false, false},
		.supports_rgmii = {false, false, false, false,
				   true, true, false, false},
		.internal_phy	= {true, true, true, true,
				   false, false, true, true},
	},
};
EXPORT_SYMBOL_GPL(ksz_switch_chips);

static const struct ksz_chip_data *ksz_lookup_info(unsigned int prod_num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ksz_switch_chips); i++) {
		const struct ksz_chip_data *chip = &ksz_switch_chips[i];

		if (chip->chip_id == prod_num)
			return chip;
	}

	return NULL;
}

static int ksz_check_device_id(struct ksz_device *dev)
{
	const struct ksz_chip_data *dt_chip_data;

	dt_chip_data = of_device_get_match_data(dev->dev);

	/* Check for Device Tree and Chip ID */
	if (dt_chip_data->chip_id != dev->chip_id) {
		dev_err(dev->dev,
			"Device tree specifies chip %s but found %s, please fix it!\n",
			dt_chip_data->dev_name, dev->info->dev_name);
		return -ENODEV;
	}

	return 0;
}

void ksz_phylink_get_caps(struct dsa_switch *ds, int port,
			  struct phylink_config *config)
{
	struct ksz_device *dev = ds->priv;

	config->legacy_pre_march2020 = false;

	if (dev->info->supports_mii[port])
		__set_bit(PHY_INTERFACE_MODE_MII, config->supported_interfaces);

	if (dev->info->supports_rmii[port])
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  config->supported_interfaces);

	if (dev->info->supports_rgmii[port])
		phy_interface_set_rgmii(config->supported_interfaces);

	if (dev->info->internal_phy[port])
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
}
EXPORT_SYMBOL_GPL(ksz_phylink_get_caps);

void ksz_r_mib_stats64(struct ksz_device *dev, int port)
{
	struct rtnl_link_stats64 *stats;
	struct ksz_stats_raw *raw;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	stats = &mib->stats64;
	raw = (struct ksz_stats_raw *)mib->counters;

	spin_lock(&mib->stats64_lock);

	stats->rx_packets = raw->rx_bcast + raw->rx_mcast + raw->rx_ucast;
	stats->tx_packets = raw->tx_bcast + raw->tx_mcast + raw->tx_ucast;

	/* HW counters are counting bytes + FCS which is not acceptable
	 * for rtnl_link_stats64 interface
	 */
	stats->rx_bytes = raw->rx_total - stats->rx_packets * ETH_FCS_LEN;
	stats->tx_bytes = raw->tx_total - stats->tx_packets * ETH_FCS_LEN;

	stats->rx_length_errors = raw->rx_undersize + raw->rx_fragments +
		raw->rx_oversize;

	stats->rx_crc_errors = raw->rx_crc_err;
	stats->rx_frame_errors = raw->rx_align_err;
	stats->rx_dropped = raw->rx_discards;
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
		stats->rx_frame_errors  + stats->rx_dropped;

	stats->tx_window_errors = raw->tx_late_col;
	stats->tx_fifo_errors = raw->tx_discards;
	stats->tx_aborted_errors = raw->tx_exc_col;
	stats->tx_errors = stats->tx_window_errors + stats->tx_fifo_errors +
		stats->tx_aborted_errors;

	stats->multicast = raw->rx_mcast;
	stats->collisions = raw->tx_total_col;

	spin_unlock(&mib->stats64_lock);
}
EXPORT_SYMBOL_GPL(ksz_r_mib_stats64);

void ksz_get_stats64(struct dsa_switch *ds, int port,
		     struct rtnl_link_stats64 *s)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;

	spin_lock(&mib->stats64_lock);
	memcpy(s, &mib->stats64, sizeof(*s));
	spin_unlock(&mib->stats64_lock);
}
EXPORT_SYMBOL_GPL(ksz_get_stats64);

void ksz_get_strings(struct dsa_switch *ds, int port,
		     u32 stringset, uint8_t *buf)
{
	struct ksz_device *dev = ds->priv;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < dev->info->mib_cnt; i++) {
		memcpy(buf + i * ETH_GSTRING_LEN,
		       dev->info->mib_names[i].string, ETH_GSTRING_LEN);
	}
}
EXPORT_SYMBOL_GPL(ksz_get_strings);

void ksz_update_port_member(struct ksz_device *dev, int port)
{
	struct ksz_port *p = &dev->ports[port];
	struct dsa_switch *ds = dev->ds;
	u8 port_member = 0, cpu_port;
	const struct dsa_port *dp;
	int i, j;

	if (!dsa_is_user_port(ds, port))
		return;

	dp = dsa_to_port(ds, port);
	cpu_port = BIT(dsa_upstream_port(ds, port));

	for (i = 0; i < ds->num_ports; i++) {
		const struct dsa_port *other_dp = dsa_to_port(ds, i);
		struct ksz_port *other_p = &dev->ports[i];
		u8 val = 0;

		if (!dsa_is_user_port(ds, i))
			continue;
		if (port == i)
			continue;
		if (!dsa_port_bridge_same(dp, other_dp))
			continue;
		if (other_p->stp_state != BR_STATE_FORWARDING)
			continue;

		if (p->stp_state == BR_STATE_FORWARDING) {
			val |= BIT(port);
			port_member |= BIT(i);
		}

		/* Retain port [i]'s relationship to other ports than [port] */
		for (j = 0; j < ds->num_ports; j++) {
			const struct dsa_port *third_dp;
			struct ksz_port *third_p;

			if (j == i)
				continue;
			if (j == port)
				continue;
			if (!dsa_is_user_port(ds, j))
				continue;
			third_p = &dev->ports[j];
			if (third_p->stp_state != BR_STATE_FORWARDING)
				continue;
			third_dp = dsa_to_port(ds, j);
			if (dsa_port_bridge_same(other_dp, third_dp))
				val |= BIT(j);
		}

		dev->dev_ops->cfg_port_member(dev, i, val | cpu_port);
	}

	dev->dev_ops->cfg_port_member(dev, port, port_member | cpu_port);
}
EXPORT_SYMBOL_GPL(ksz_update_port_member);

static void port_r_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;
	u64 *dropped;

	/* Some ports may not have MIB counters before SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->reg_mib_cnt) {
		dev->dev_ops->r_mib_cnt(dev, port, mib->cnt_ptr,
					&mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}

	/* last one in storage */
	dropped = &mib->counters[dev->info->mib_cnt];

	/* Some ports may not have MIB counters after SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->mib_cnt) {
		dev->dev_ops->r_mib_pkt(dev, port, mib->cnt_ptr,
					dropped, &mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}
	mib->cnt_ptr = 0;
}

static void ksz_mib_read_work(struct work_struct *work)
{
	struct ksz_device *dev = container_of(work, struct ksz_device,
					      mib_read.work);
	struct ksz_port_mib *mib;
	struct ksz_port *p;
	int i;

	for (i = 0; i < dev->info->port_cnt; i++) {
		if (dsa_is_unused_port(dev->ds, i))
			continue;

		p = &dev->ports[i];
		mib = &p->mib;
		mutex_lock(&mib->cnt_mutex);

		/* Only read MIB counters when the port is told to do.
		 * If not, read only dropped counters when link is not up.
		 */
		if (!p->read) {
			const struct dsa_port *dp = dsa_to_port(dev->ds, i);

			if (!netif_carrier_ok(dp->slave))
				mib->cnt_ptr = dev->info->reg_mib_cnt;
		}
		port_r_cnt(dev, i);
		p->read = false;

		if (dev->dev_ops->r_mib_stat64)
			dev->dev_ops->r_mib_stat64(dev, i);

		mutex_unlock(&mib->cnt_mutex);
	}

	schedule_delayed_work(&dev->mib_read, dev->mib_read_interval);
}

void ksz_init_mib_timer(struct ksz_device *dev)
{
	int i;

	INIT_DELAYED_WORK(&dev->mib_read, ksz_mib_read_work);

	for (i = 0; i < dev->info->port_cnt; i++) {
		struct ksz_port_mib *mib = &dev->ports[i].mib;

		dev->dev_ops->port_init_cnt(dev, i);

		mib->cnt_ptr = 0;
		memset(mib->counters, 0, dev->info->mib_cnt * sizeof(u64));
	}
}
EXPORT_SYMBOL_GPL(ksz_init_mib_timer);

int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct ksz_device *dev = ds->priv;
	u16 val = 0xffff;

	dev->dev_ops->r_phy(dev, addr, reg, &val);

	return val;
}
EXPORT_SYMBOL_GPL(ksz_phy_read16);

int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct ksz_device *dev = ds->priv;

	dev->dev_ops->w_phy(dev, addr, reg, val);

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_phy_write16);

void ksz_mac_link_down(struct dsa_switch *ds, int port, unsigned int mode,
		       phy_interface_t interface)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p = &dev->ports[port];

	/* Read all MIB counters when the link is going down. */
	p->read = true;
	/* timer started */
	if (dev->mib_read_interval)
		schedule_delayed_work(&dev->mib_read, 0);
}
EXPORT_SYMBOL_GPL(ksz_mac_link_down);

int ksz_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ksz_device *dev = ds->priv;

	if (sset != ETH_SS_STATS)
		return 0;

	return dev->info->mib_cnt;
}
EXPORT_SYMBOL_GPL(ksz_sset_count);

void ksz_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *buf)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	struct ksz_device *dev = ds->priv;
	struct ksz_port_mib *mib;

	mib = &dev->ports[port].mib;
	mutex_lock(&mib->cnt_mutex);

	/* Only read dropped counters if no link. */
	if (!netif_carrier_ok(dp->slave))
		mib->cnt_ptr = dev->info->reg_mib_cnt;
	port_r_cnt(dev, port);
	memcpy(buf, mib->counters, dev->info->mib_cnt * sizeof(u64));
	mutex_unlock(&mib->cnt_mutex);
}
EXPORT_SYMBOL_GPL(ksz_get_ethtool_stats);

int ksz_port_bridge_join(struct dsa_switch *ds, int port,
			 struct dsa_bridge bridge,
			 bool *tx_fwd_offload,
			 struct netlink_ext_ack *extack)
{
	/* port_stp_state_set() will be called after to put the port in
	 * appropriate state so there is no need to do anything.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_bridge_join);

void ksz_port_bridge_leave(struct dsa_switch *ds, int port,
			   struct dsa_bridge bridge)
{
	/* port_stp_state_set() will be called after to put the port in
	 * forwarding state so there is no need to do anything.
	 */
}
EXPORT_SYMBOL_GPL(ksz_port_bridge_leave);

void ksz_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;

	dev->dev_ops->flush_dyn_mac_table(dev, port);
}
EXPORT_SYMBOL_GPL(ksz_port_fast_age);

int ksz_port_fdb_dump(struct dsa_switch *ds, int port, dsa_fdb_dump_cb_t *cb,
		      void *data)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;
	u16 i = 0;
	u16 entries = 0;
	u8 timestamp = 0;
	u8 fid;
	u8 member;
	struct alu_struct alu;

	do {
		alu.is_static = false;
		ret = dev->dev_ops->r_dyn_mac_table(dev, i, alu.mac, &fid,
						    &member, &timestamp,
						    &entries);
		if (!ret && (member & BIT(port))) {
			ret = cb(alu.mac, alu.fid, alu.is_static, data);
			if (ret)
				break;
		}
		i++;
	} while (i < entries);
	if (i >= entries)
		ret = 0;

	return ret;
}
EXPORT_SYMBOL_GPL(ksz_port_fdb_dump);

int ksz_port_mdb_add(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;
	struct alu_struct alu;
	int index;
	int empty = 0;

	alu.port_forward = 0;
	for (index = 0; index < dev->info->num_statics; index++) {
		if (!dev->dev_ops->r_sta_mac_table(dev, index, &alu)) {
			/* Found one already in static MAC table. */
			if (!memcmp(alu.mac, mdb->addr, ETH_ALEN) &&
			    alu.fid == mdb->vid)
				break;
		/* Remember the first empty entry. */
		} else if (!empty) {
			empty = index + 1;
		}
	}

	/* no available entry */
	if (index == dev->info->num_statics && !empty)
		return -ENOSPC;

	/* add entry */
	if (index == dev->info->num_statics) {
		index = empty - 1;
		memset(&alu, 0, sizeof(alu));
		memcpy(alu.mac, mdb->addr, ETH_ALEN);
		alu.is_static = true;
	}
	alu.port_forward |= BIT(port);
	if (mdb->vid) {
		alu.is_use_fid = true;

		/* Need a way to map VID to FID. */
		alu.fid = mdb->vid;
	}
	dev->dev_ops->w_sta_mac_table(dev, index, &alu);

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_mdb_add);

int ksz_port_mdb_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct dsa_db db)
{
	struct ksz_device *dev = ds->priv;
	struct alu_struct alu;
	int index;

	for (index = 0; index < dev->info->num_statics; index++) {
		if (!dev->dev_ops->r_sta_mac_table(dev, index, &alu)) {
			/* Found one already in static MAC table. */
			if (!memcmp(alu.mac, mdb->addr, ETH_ALEN) &&
			    alu.fid == mdb->vid)
				break;
		}
	}

	/* no available entry */
	if (index == dev->info->num_statics)
		goto exit;

	/* clear port */
	alu.port_forward &= ~BIT(port);
	if (!alu.port_forward)
		alu.is_static = false;
	dev->dev_ops->w_sta_mac_table(dev, index, &alu);

exit:
	return 0;
}
EXPORT_SYMBOL_GPL(ksz_port_mdb_del);

int ksz_enable_port(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	struct ksz_device *dev = ds->priv;

	if (!dsa_is_user_port(ds, port))
		return 0;

	/* setup slave port */
	dev->dev_ops->port_setup(dev, port, false);

	/* port_stp_state_set() will be called after to enable the port so
	 * there is no need to do anything.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(ksz_enable_port);

void ksz_port_stp_state_set(struct dsa_switch *ds, int port,
			    u8 state, int reg)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p;
	u8 data;

	ksz_pread8(dev, port, reg, &data);
	data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE | PORT_LEARN_DISABLE);

	switch (state) {
	case BR_STATE_DISABLED:
		data |= PORT_LEARN_DISABLE;
		break;
	case BR_STATE_LISTENING:
		data |= (PORT_RX_ENABLE | PORT_LEARN_DISABLE);
		break;
	case BR_STATE_LEARNING:
		data |= PORT_RX_ENABLE;
		break;
	case BR_STATE_FORWARDING:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);
		break;
	case BR_STATE_BLOCKING:
		data |= PORT_LEARN_DISABLE;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ksz_pwrite8(dev, port, reg, data);

	p = &dev->ports[port];
	p->stp_state = state;

	ksz_update_port_member(dev, port);
}
EXPORT_SYMBOL_GPL(ksz_port_stp_state_set);

struct ksz_device *ksz_switch_alloc(struct device *base, void *priv)
{
	struct dsa_switch *ds;
	struct ksz_device *swdev;

	ds = devm_kzalloc(base, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = base;
	ds->num_ports = DSA_MAX_PORTS;

	swdev = devm_kzalloc(base, sizeof(*swdev), GFP_KERNEL);
	if (!swdev)
		return NULL;

	ds->priv = swdev;
	swdev->dev = base;

	swdev->ds = ds;
	swdev->priv = priv;

	return swdev;
}
EXPORT_SYMBOL(ksz_switch_alloc);

int ksz_switch_register(struct ksz_device *dev,
			const struct ksz_dev_ops *ops)
{
	const struct ksz_chip_data *info;
	struct device_node *port, *ports;
	phy_interface_t interface;
	unsigned int port_num;
	int ret;
	int i;

	if (dev->pdata)
		dev->chip_id = dev->pdata->chip_id;

	dev->reset_gpio = devm_gpiod_get_optional(dev->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(dev->reset_gpio))
		return PTR_ERR(dev->reset_gpio);

	if (dev->reset_gpio) {
		gpiod_set_value_cansleep(dev->reset_gpio, 1);
		usleep_range(10000, 12000);
		gpiod_set_value_cansleep(dev->reset_gpio, 0);
		msleep(100);
	}

	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->regmap_mutex);
	mutex_init(&dev->alu_mutex);
	mutex_init(&dev->vlan_mutex);

	dev->dev_ops = ops;

	if (dev->dev_ops->detect(dev))
		return -EINVAL;

	info = ksz_lookup_info(dev->chip_id);
	if (!info)
		return -ENODEV;

	/* Update the compatible info with the probed one */
	dev->info = info;

	ret = ksz_check_device_id(dev);
	if (ret)
		return ret;

	ret = dev->dev_ops->init(dev);
	if (ret)
		return ret;

	dev->ports = devm_kzalloc(dev->dev,
				  dev->info->port_cnt * sizeof(struct ksz_port),
				  GFP_KERNEL);
	if (!dev->ports)
		return -ENOMEM;

	for (i = 0; i < dev->info->port_cnt; i++) {
		spin_lock_init(&dev->ports[i].mib.stats64_lock);
		mutex_init(&dev->ports[i].mib.cnt_mutex);
		dev->ports[i].mib.counters =
			devm_kzalloc(dev->dev,
				     sizeof(u64) * (dev->info->mib_cnt + 1),
				     GFP_KERNEL);
		if (!dev->ports[i].mib.counters)
			return -ENOMEM;
	}

	/* set the real number of ports */
	dev->ds->num_ports = dev->info->port_cnt;

	/* Host port interface will be self detected, or specifically set in
	 * device tree.
	 */
	for (port_num = 0; port_num < dev->info->port_cnt; ++port_num)
		dev->ports[port_num].interface = PHY_INTERFACE_MODE_NA;
	if (dev->dev->of_node) {
		ret = of_get_phy_mode(dev->dev->of_node, &interface);
		if (ret == 0)
			dev->compat_interface = interface;
		ports = of_get_child_by_name(dev->dev->of_node, "ethernet-ports");
		if (!ports)
			ports = of_get_child_by_name(dev->dev->of_node, "ports");
		if (ports)
			for_each_available_child_of_node(ports, port) {
				if (of_property_read_u32(port, "reg",
							 &port_num))
					continue;
				if (!(dev->port_mask & BIT(port_num))) {
					of_node_put(port);
					return -EINVAL;
				}
				of_get_phy_mode(port,
						&dev->ports[port_num].interface);
			}
		dev->synclko_125 = of_property_read_bool(dev->dev->of_node,
							 "microchip,synclko-125");
		dev->synclko_disable = of_property_read_bool(dev->dev->of_node,
							     "microchip,synclko-disable");
		if (dev->synclko_125 && dev->synclko_disable) {
			dev_err(dev->dev, "inconsistent synclko settings\n");
			return -EINVAL;
		}
	}

	ret = dsa_register_switch(dev->ds);
	if (ret) {
		dev->dev_ops->exit(dev);
		return ret;
	}

	/* Read MIB counters every 30 seconds to avoid overflow. */
	dev->mib_read_interval = msecs_to_jiffies(5000);

	/* Start the MIB timer. */
	schedule_delayed_work(&dev->mib_read, 0);

	return 0;
}
EXPORT_SYMBOL(ksz_switch_register);

void ksz_switch_remove(struct ksz_device *dev)
{
	/* timer started */
	if (dev->mib_read_interval) {
		dev->mib_read_interval = 0;
		cancel_delayed_work_sync(&dev->mib_read);
	}

	dev->dev_ops->exit(dev);
	dsa_unregister_switch(dev->ds);

	if (dev->reset_gpio)
		gpiod_set_value_cansleep(dev->reset_gpio, 1);

}
EXPORT_SYMBOL(ksz_switch_remove);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ Series Switch DSA Driver");
MODULE_LICENSE("GPL");
