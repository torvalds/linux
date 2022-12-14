// SPDX-License-Identifier: GPL-2.0-only
/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 */

/* ethtool support for fjes */

#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include "fjes.h"

struct fjes_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define FJES_STAT(name, stat) { \
	.stat_string = name, \
	.sizeof_stat = sizeof_field(struct fjes_adapter, stat), \
	.stat_offset = offsetof(struct fjes_adapter, stat) \
}

static const struct fjes_stats fjes_gstrings_stats[] = {
	FJES_STAT("rx_packets", stats64.rx_packets),
	FJES_STAT("tx_packets", stats64.tx_packets),
	FJES_STAT("rx_bytes", stats64.rx_bytes),
	FJES_STAT("tx_bytes", stats64.rx_bytes),
	FJES_STAT("rx_dropped", stats64.rx_dropped),
	FJES_STAT("tx_dropped", stats64.tx_dropped),
};

#define FJES_EP_STATS_LEN 14
#define FJES_STATS_LEN \
	(ARRAY_SIZE(fjes_gstrings_stats) + \
	 ((&((struct fjes_adapter *)netdev_priv(netdev))->hw)->max_epid - 1) * \
	 FJES_EP_STATS_LEN)

static void fjes_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int epidx;
	char *p;
	int i;

	for (i = 0; i < ARRAY_SIZE(fjes_gstrings_stats); i++) {
		p = (char *)adapter + fjes_gstrings_stats[i].stat_offset;
		data[i] = (fjes_gstrings_stats[i].sizeof_stat == sizeof(u64))
			? *(u64 *)p : *(u32 *)p;
	}
	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.com_regist_buf_exec;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.com_unregist_buf_exec;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.send_intr_rx;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.send_intr_unshare;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.send_intr_zoneupdate;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.recv_intr_rx;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.recv_intr_unshare;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.recv_intr_stop;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.recv_intr_zoneupdate;
		data[i++] = hw->ep_shm_info[epidx].ep_stats.tx_buffer_full;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.tx_dropped_not_shared;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.tx_dropped_ver_mismatch;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.tx_dropped_buf_size_mismatch;
		data[i++] = hw->ep_shm_info[epidx].ep_stats
				.tx_dropped_vlanid_mismatch;
	}
}

static void fjes_get_strings(struct net_device *netdev,
			     u32 stringset, u8 *data)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(fjes_gstrings_stats); i++) {
			memcpy(p, fjes_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < hw->max_epid; i++) {
			if (i == hw->my_epid)
				continue;
			sprintf(p, "ep%u_com_regist_buf_exec", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_com_unregist_buf_exec", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_send_intr_rx", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_send_intr_unshare", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_send_intr_zoneupdate", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_recv_intr_rx", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_recv_intr_unshare", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_recv_intr_stop", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_recv_intr_zoneupdate", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_tx_buffer_full", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_tx_dropped_not_shared", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_tx_dropped_ver_mismatch", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_tx_dropped_buf_size_mismatch", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "ep%u_tx_dropped_vlanid_mismatch", i);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int fjes_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return FJES_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void fjes_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct platform_device *plat_dev;

	plat_dev = adapter->plat_dev;

	strscpy(drvinfo->driver, fjes_driver_name, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, fjes_driver_version,
		sizeof(drvinfo->version));

	strscpy(drvinfo->fw_version, "none", sizeof(drvinfo->fw_version));
	snprintf(drvinfo->bus_info, sizeof(drvinfo->bus_info),
		 "platform:%s", plat_dev->name);
}

static int fjes_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *ecmd)
{
	ethtool_link_ksettings_zero_link_mode(ecmd, supported);
	ethtool_link_ksettings_zero_link_mode(ecmd, advertising);
	ecmd->base.duplex = DUPLEX_FULL;
	ecmd->base.autoneg = AUTONEG_DISABLE;
	ecmd->base.port = PORT_NONE;
	ecmd->base.speed = 20000;	/* 20Gb/s */

	return 0;
}

static int fjes_get_regs_len(struct net_device *netdev)
{
#define FJES_REGS_LEN	37
	return FJES_REGS_LEN * sizeof(u32);
}

static void fjes_get_regs(struct net_device *netdev,
			  struct ethtool_regs *regs, void *p)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	u32 *regs_buff = p;

	memset(p, 0, FJES_REGS_LEN * sizeof(u32));

	regs->version = 1;

	/* Information registers */
	regs_buff[0] = rd32(XSCT_OWNER_EPID);
	regs_buff[1] = rd32(XSCT_MAX_EP);

	/* Device Control registers */
	regs_buff[4] = rd32(XSCT_DCTL);

	/* Command Control registers */
	regs_buff[8] = rd32(XSCT_CR);
	regs_buff[9] = rd32(XSCT_CS);
	regs_buff[10] = rd32(XSCT_SHSTSAL);
	regs_buff[11] = rd32(XSCT_SHSTSAH);

	regs_buff[13] = rd32(XSCT_REQBL);
	regs_buff[14] = rd32(XSCT_REQBAL);
	regs_buff[15] = rd32(XSCT_REQBAH);

	regs_buff[17] = rd32(XSCT_RESPBL);
	regs_buff[18] = rd32(XSCT_RESPBAL);
	regs_buff[19] = rd32(XSCT_RESPBAH);

	/* Interrupt Control registers */
	regs_buff[32] = rd32(XSCT_IS);
	regs_buff[33] = rd32(XSCT_IMS);
	regs_buff[34] = rd32(XSCT_IMC);
	regs_buff[35] = rd32(XSCT_IG);
	regs_buff[36] = rd32(XSCT_ICTL);
}

static int fjes_set_dump(struct net_device *netdev, struct ethtool_dump *dump)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int ret = 0;

	if (dump->flag) {
		if (hw->debug_mode)
			return -EPERM;

		hw->debug_mode = dump->flag;

		/* enable debug mode */
		mutex_lock(&hw->hw_info.lock);
		ret = fjes_hw_start_debug(hw);
		mutex_unlock(&hw->hw_info.lock);

		if (ret)
			hw->debug_mode = 0;
	} else {
		if (!hw->debug_mode)
			return -EPERM;

		/* disable debug mode */
		mutex_lock(&hw->hw_info.lock);
		ret = fjes_hw_stop_debug(hw);
		mutex_unlock(&hw->hw_info.lock);
	}

	return ret;
}

static int fjes_get_dump_flag(struct net_device *netdev,
			      struct ethtool_dump *dump)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;

	dump->len = hw->hw_info.trace_size;
	dump->version = 1;
	dump->flag = hw->debug_mode;

	return 0;
}

static int fjes_get_dump_data(struct net_device *netdev,
			      struct ethtool_dump *dump, void *buf)
{
	struct fjes_adapter *adapter = netdev_priv(netdev);
	struct fjes_hw *hw = &adapter->hw;
	int ret = 0;

	if (hw->hw_info.trace)
		memcpy(buf, hw->hw_info.trace, hw->hw_info.trace_size);
	else
		ret = -EPERM;

	return ret;
}

static const struct ethtool_ops fjes_ethtool_ops = {
		.get_drvinfo		= fjes_get_drvinfo,
		.get_ethtool_stats = fjes_get_ethtool_stats,
		.get_strings      = fjes_get_strings,
		.get_sset_count   = fjes_get_sset_count,
		.get_regs		= fjes_get_regs,
		.get_regs_len		= fjes_get_regs_len,
		.set_dump		= fjes_set_dump,
		.get_dump_flag		= fjes_get_dump_flag,
		.get_dump_data		= fjes_get_dump_data,
		.get_link_ksettings	= fjes_get_link_ksettings,
};

void fjes_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &fjes_ethtool_ops;
}
