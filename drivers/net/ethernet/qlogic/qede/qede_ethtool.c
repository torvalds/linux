/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*
* This software is available under the terms of the GNU General Public License
* (GPL) Version 2, available from the file COPYING in the main directory of
* this source tree.
*/

#include <linux/version.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/capability.h>
#include "qede.h"

#define QEDE_STAT_OFFSET(stat_name) (offsetof(struct qede_stats, stat_name))
#define QEDE_STAT_STRING(stat_name) (#stat_name)
#define _QEDE_STAT(stat_name, pf_only) \
	 {QEDE_STAT_OFFSET(stat_name), QEDE_STAT_STRING(stat_name), pf_only}
#define QEDE_PF_STAT(stat_name)		_QEDE_STAT(stat_name, true)
#define QEDE_STAT(stat_name)		_QEDE_STAT(stat_name, false)

#define QEDE_RQSTAT_OFFSET(stat_name) \
	 (offsetof(struct qede_rx_queue, stat_name))
#define QEDE_RQSTAT_STRING(stat_name) (#stat_name)
#define QEDE_RQSTAT(stat_name) \
	 {QEDE_RQSTAT_OFFSET(stat_name), QEDE_RQSTAT_STRING(stat_name)}
static const struct {
	u64 offset;
	char string[ETH_GSTRING_LEN];
} qede_rqstats_arr[] = {
	QEDE_RQSTAT(rx_hw_errors),
	QEDE_RQSTAT(rx_alloc_errors),
};

#define QEDE_NUM_RQSTATS ARRAY_SIZE(qede_rqstats_arr)
#define QEDE_RQSTATS_DATA(dev, sindex, rqindex) \
	(*((u64 *)(((char *)(dev->fp_array[(rqindex)].rxq)) +\
		    qede_rqstats_arr[(sindex)].offset)))
static const struct {
	u64 offset;
	char string[ETH_GSTRING_LEN];
	bool pf_only;
} qede_stats_arr[] = {
	QEDE_STAT(rx_ucast_bytes),
	QEDE_STAT(rx_mcast_bytes),
	QEDE_STAT(rx_bcast_bytes),
	QEDE_STAT(rx_ucast_pkts),
	QEDE_STAT(rx_mcast_pkts),
	QEDE_STAT(rx_bcast_pkts),

	QEDE_STAT(tx_ucast_bytes),
	QEDE_STAT(tx_mcast_bytes),
	QEDE_STAT(tx_bcast_bytes),
	QEDE_STAT(tx_ucast_pkts),
	QEDE_STAT(tx_mcast_pkts),
	QEDE_STAT(tx_bcast_pkts),

	QEDE_PF_STAT(rx_64_byte_packets),
	QEDE_PF_STAT(rx_127_byte_packets),
	QEDE_PF_STAT(rx_255_byte_packets),
	QEDE_PF_STAT(rx_511_byte_packets),
	QEDE_PF_STAT(rx_1023_byte_packets),
	QEDE_PF_STAT(rx_1518_byte_packets),
	QEDE_PF_STAT(rx_1522_byte_packets),
	QEDE_PF_STAT(rx_2047_byte_packets),
	QEDE_PF_STAT(rx_4095_byte_packets),
	QEDE_PF_STAT(rx_9216_byte_packets),
	QEDE_PF_STAT(rx_16383_byte_packets),
	QEDE_PF_STAT(tx_64_byte_packets),
	QEDE_PF_STAT(tx_65_to_127_byte_packets),
	QEDE_PF_STAT(tx_128_to_255_byte_packets),
	QEDE_PF_STAT(tx_256_to_511_byte_packets),
	QEDE_PF_STAT(tx_512_to_1023_byte_packets),
	QEDE_PF_STAT(tx_1024_to_1518_byte_packets),
	QEDE_PF_STAT(tx_1519_to_2047_byte_packets),
	QEDE_PF_STAT(tx_2048_to_4095_byte_packets),
	QEDE_PF_STAT(tx_4096_to_9216_byte_packets),
	QEDE_PF_STAT(tx_9217_to_16383_byte_packets),

	QEDE_PF_STAT(rx_mac_crtl_frames),
	QEDE_PF_STAT(tx_mac_ctrl_frames),
	QEDE_PF_STAT(rx_pause_frames),
	QEDE_PF_STAT(tx_pause_frames),
	QEDE_PF_STAT(rx_pfc_frames),
	QEDE_PF_STAT(tx_pfc_frames),

	QEDE_PF_STAT(rx_crc_errors),
	QEDE_PF_STAT(rx_align_errors),
	QEDE_PF_STAT(rx_carrier_errors),
	QEDE_PF_STAT(rx_oversize_packets),
	QEDE_PF_STAT(rx_jabbers),
	QEDE_PF_STAT(rx_undersize_packets),
	QEDE_PF_STAT(rx_fragments),
	QEDE_PF_STAT(tx_lpi_entry_count),
	QEDE_PF_STAT(tx_total_collisions),
	QEDE_PF_STAT(brb_truncates),
	QEDE_PF_STAT(brb_discards),
	QEDE_STAT(no_buff_discards),
	QEDE_PF_STAT(mftag_filter_discards),
	QEDE_PF_STAT(mac_filter_discards),
	QEDE_STAT(tx_err_drop_pkts),

	QEDE_STAT(coalesced_pkts),
	QEDE_STAT(coalesced_events),
	QEDE_STAT(coalesced_aborts_num),
	QEDE_STAT(non_coalesced_pkts),
	QEDE_STAT(coalesced_bytes),
};

#define QEDE_STATS_DATA(dev, index) \
	(*((u64 *)(((char *)(dev)) + offsetof(struct qede_dev, stats) \
			+ qede_stats_arr[(index)].offset)))

#define QEDE_NUM_STATS	ARRAY_SIZE(qede_stats_arr)

static void qede_get_strings_stats(struct qede_dev *edev, u8 *buf)
{
	int i, j, k;

	for (i = 0, j = 0; i < QEDE_NUM_STATS; i++) {
		strcpy(buf + j * ETH_GSTRING_LEN,
		       qede_stats_arr[i].string);
		j++;
	}

	for (k = 0; k < QEDE_NUM_RQSTATS; k++, j++)
		strcpy(buf + j * ETH_GSTRING_LEN,
		       qede_rqstats_arr[k].string);
}

static void qede_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_STATS:
		qede_get_strings_stats(edev, buf);
		break;
	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Unsupported stringset 0x%08x\n", stringset);
	}
}

static void qede_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);
	int sidx, cnt = 0;
	int qid;

	qede_fill_by_demand_stats(edev);

	mutex_lock(&edev->qede_lock);

	for (sidx = 0; sidx < QEDE_NUM_STATS; sidx++)
		buf[cnt++] = QEDE_STATS_DATA(edev, sidx);

	for (sidx = 0; sidx < QEDE_NUM_RQSTATS; sidx++) {
		buf[cnt] = 0;
		for (qid = 0; qid < edev->num_rss; qid++)
			buf[cnt] += QEDE_RQSTATS_DATA(edev, sidx, qid);
		cnt++;
	}

	mutex_unlock(&edev->qede_lock);
}

static int qede_get_sset_count(struct net_device *dev, int stringset)
{
	struct qede_dev *edev = netdev_priv(dev);
	int num_stats = QEDE_NUM_STATS;

	switch (stringset) {
	case ETH_SS_STATS:
		return num_stats + QEDE_NUM_RQSTATS;

	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Unsupported stringset 0x%08x\n", stringset);
		return -EINVAL;
	}
}

static int qede_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	cmd->supported = current_link.supported_caps;
	cmd->advertising = current_link.advertised_caps;
	if ((edev->state == QEDE_STATE_OPEN) && (current_link.link_up)) {
		ethtool_cmd_speed_set(cmd, current_link.speed);
		cmd->duplex = current_link.duplex;
	} else {
		cmd->duplex = DUPLEX_UNKNOWN;
		ethtool_cmd_speed_set(cmd, SPEED_UNKNOWN);
	}
	cmd->port = current_link.port;
	cmd->autoneg = (current_link.autoneg) ? AUTONEG_ENABLE :
						AUTONEG_DISABLE;
	cmd->lp_advertising = current_link.lp_caps;

	return 0;
}

static int qede_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;
	struct qed_link_params params;
	u32 speed;

	if (edev->dev_info.common.is_mf) {
		DP_INFO(edev,
			"Link parameters can not be changed in MF mode\n");
		return -EOPNOTSUPP;
	}

	memset(&current_link, 0, sizeof(current_link));
	memset(&params, 0, sizeof(params));
	edev->ops->common->get_link(edev->cdev, &current_link);

	speed = ethtool_cmd_speed(cmd);
	params.override_flags |= QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS;
	params.override_flags |= QED_LINK_OVERRIDE_SPEED_AUTONEG;
	if (cmd->autoneg == AUTONEG_ENABLE) {
		params.autoneg = true;
		params.forced_speed = 0;
		params.adv_speeds = cmd->advertising;
	} else { /* forced speed */
		params.override_flags |= QED_LINK_OVERRIDE_SPEED_FORCED_SPEED;
		params.autoneg = false;
		params.forced_speed = speed;
		switch (speed) {
		case SPEED_10000:
			if (!(current_link.supported_caps &
			    SUPPORTED_10000baseKR_Full)) {
				DP_INFO(edev, "10G speed not supported\n");
				return -EINVAL;
			}
			params.adv_speeds = SUPPORTED_10000baseKR_Full;
			break;
		case SPEED_40000:
			if (!(current_link.supported_caps &
			    SUPPORTED_40000baseLR4_Full)) {
				DP_INFO(edev, "40G speed not supported\n");
				return -EINVAL;
			}
			params.adv_speeds = SUPPORTED_40000baseLR4_Full;
			break;
		default:
			DP_INFO(edev, "Unsupported speed %u\n", speed);
			return -EINVAL;
		}
	}

	params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
}

static void qede_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	char mfw[ETHTOOL_FWVERS_LEN], storm[ETHTOOL_FWVERS_LEN];
	struct qede_dev *edev = netdev_priv(ndev);

	strlcpy(info->driver, "qede", sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));

	snprintf(storm, ETHTOOL_FWVERS_LEN, "%d.%d.%d.%d",
		 edev->dev_info.common.fw_major,
		 edev->dev_info.common.fw_minor,
		 edev->dev_info.common.fw_rev,
		 edev->dev_info.common.fw_eng);

	snprintf(mfw, ETHTOOL_FWVERS_LEN, "%d.%d.%d.%d",
		 (edev->dev_info.common.mfw_rev >> 24) & 0xFF,
		 (edev->dev_info.common.mfw_rev >> 16) & 0xFF,
		 (edev->dev_info.common.mfw_rev >> 8) & 0xFF,
		 edev->dev_info.common.mfw_rev & 0xFF);

	if ((strlen(storm) + strlen(mfw) + strlen("mfw storm  ")) <
	    sizeof(info->fw_version)) {
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "mfw %s storm %s", mfw, storm);
	} else {
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "%s %s", mfw, storm);
	}

	strlcpy(info->bus_info, pci_name(edev->pdev), sizeof(info->bus_info));
}

static u32 qede_get_msglevel(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	return ((u32)edev->dp_level << QED_LOG_LEVEL_SHIFT) |
	       edev->dp_module;
}

static void qede_set_msglevel(struct net_device *ndev, u32 level)
{
	struct qede_dev *edev = netdev_priv(ndev);
	u32 dp_module = 0;
	u8 dp_level = 0;

	qede_config_debug(level, &dp_module, &dp_level);

	edev->dp_level = dp_level;
	edev->dp_module = dp_module;
	edev->ops->common->update_msglvl(edev->cdev,
					 dp_module, dp_level);
}

static u32 qede_get_link(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	return current_link.link_up;
}

static void qede_update_mtu(struct qede_dev *edev, union qede_reload_args *args)
{
	edev->ndev->mtu = args->mtu;
}

/* Netdevice NDOs */
#define ETH_MAX_JUMBO_PACKET_SIZE	9600
#define ETH_MIN_PACKET_SIZE		60
int qede_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct qede_dev *edev = netdev_priv(ndev);
	union qede_reload_args args;

	if ((new_mtu > ETH_MAX_JUMBO_PACKET_SIZE) ||
	    ((new_mtu + ETH_HLEN) < ETH_MIN_PACKET_SIZE)) {
		DP_ERR(edev, "Can't support requested MTU size\n");
		return -EINVAL;
	}

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "Configuring MTU size of %d\n", new_mtu);

	/* Set the mtu field and re-start the interface if needed*/
	args.mtu = new_mtu;

	if (netif_running(edev->ndev))
		qede_reload(edev, &qede_update_mtu, &args);

	qede_update_mtu(edev, &args);

	return 0;
}

static void qede_get_channels(struct net_device *dev,
			      struct ethtool_channels *channels)
{
	struct qede_dev *edev = netdev_priv(dev);

	channels->max_combined = QEDE_MAX_RSS_CNT(edev);
	channels->combined_count = QEDE_RSS_CNT(edev);
}

static int qede_set_channels(struct net_device *dev,
			     struct ethtool_channels *channels)
{
	struct qede_dev *edev = netdev_priv(dev);

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "set-channels command parameters: rx = %d, tx = %d, other = %d, combined = %d\n",
		   channels->rx_count, channels->tx_count,
		   channels->other_count, channels->combined_count);

	/* We don't support separate rx / tx, nor `other' channels. */
	if (channels->rx_count || channels->tx_count ||
	    channels->other_count || (channels->combined_count == 0) ||
	    (channels->combined_count > QEDE_MAX_RSS_CNT(edev))) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "command parameters not supported\n");
		return -EINVAL;
	}

	/* Check if there was a change in the active parameters */
	if (channels->combined_count == QEDE_RSS_CNT(edev)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "No change in active parameters\n");
		return 0;
	}

	/* We need the number of queues to be divisible between the hwfns */
	if (channels->combined_count % edev->dev_info.common.num_hwfns) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Number of channels must be divisable by %04x\n",
			   edev->dev_info.common.num_hwfns);
		return -EINVAL;
	}

	/* Set number of queues and reload if necessary */
	edev->req_rss = channels->combined_count;
	if (netif_running(dev))
		qede_reload(edev, NULL, NULL);

	return 0;
}

static const struct ethtool_ops qede_ethtool_ops = {
	.get_settings = qede_get_settings,
	.set_settings = qede_set_settings,
	.get_drvinfo = qede_get_drvinfo,
	.get_msglevel = qede_get_msglevel,
	.set_msglevel = qede_set_msglevel,
	.get_link = qede_get_link,
	.get_strings = qede_get_strings,
	.get_ethtool_stats = qede_get_ethtool_stats,
	.get_sset_count = qede_get_sset_count,

	.get_channels = qede_get_channels,
	.set_channels = qede_set_channels,
};

void qede_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &qede_ethtool_ops;
}
