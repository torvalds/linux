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
#include <linux/etherdevice.h>
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

#define QEDE_SELFTEST_POLL_COUNT 100

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
	QEDE_PF_STAT(rx_65_to_127_byte_packets),
	QEDE_PF_STAT(rx_128_to_255_byte_packets),
	QEDE_PF_STAT(rx_256_to_511_byte_packets),
	QEDE_PF_STAT(rx_512_to_1023_byte_packets),
	QEDE_PF_STAT(rx_1024_to_1518_byte_packets),
	QEDE_PF_STAT(rx_1519_to_1522_byte_packets),
	QEDE_PF_STAT(rx_1519_to_2047_byte_packets),
	QEDE_PF_STAT(rx_2048_to_4095_byte_packets),
	QEDE_PF_STAT(rx_4096_to_9216_byte_packets),
	QEDE_PF_STAT(rx_9217_to_16383_byte_packets),
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

enum {
	QEDE_PRI_FLAG_CMT,
	QEDE_PRI_FLAG_LEN,
};

static const char qede_private_arr[QEDE_PRI_FLAG_LEN][ETH_GSTRING_LEN] = {
	"Coupled-Function",
};

enum qede_ethtool_tests {
	QEDE_ETHTOOL_INT_LOOPBACK,
	QEDE_ETHTOOL_INTERRUPT_TEST,
	QEDE_ETHTOOL_MEMORY_TEST,
	QEDE_ETHTOOL_REGISTER_TEST,
	QEDE_ETHTOOL_CLOCK_TEST,
	QEDE_ETHTOOL_TEST_MAX
};

static const char qede_tests_str_arr[QEDE_ETHTOOL_TEST_MAX][ETH_GSTRING_LEN] = {
	"Internal loopback (offline)",
	"Interrupt (online)\t",
	"Memory (online)\t\t",
	"Register (online)\t",
	"Clock (online)\t\t",
};

static void qede_get_strings_stats(struct qede_dev *edev, u8 *buf)
{
	int i, j, k;

	for (i = 0, j = 0; i < QEDE_NUM_STATS; i++) {
		if (IS_VF(edev) && qede_stats_arr[i].pf_only)
			continue;
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
	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, qede_private_arr,
		       ETH_GSTRING_LEN * QEDE_PRI_FLAG_LEN);
		break;
	case ETH_SS_TEST:
		memcpy(buf, qede_tests_str_arr,
		       ETH_GSTRING_LEN * QEDE_ETHTOOL_TEST_MAX);
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

	for (sidx = 0; sidx < QEDE_NUM_STATS; sidx++) {
		if (IS_VF(edev) && qede_stats_arr[sidx].pf_only)
			continue;
		buf[cnt++] = QEDE_STATS_DATA(edev, sidx);
	}

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
		if (IS_VF(edev)) {
			int i;

			for (i = 0; i < QEDE_NUM_STATS; i++)
				if (qede_stats_arr[i].pf_only)
					num_stats--;
		}
		return num_stats + QEDE_NUM_RQSTATS;
	case ETH_SS_PRIV_FLAGS:
		return QEDE_PRI_FLAG_LEN;
	case ETH_SS_TEST:
		if (!IS_VF(edev))
			return QEDE_ETHTOOL_TEST_MAX;
		else
			return 0;
	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "Unsupported stringset 0x%08x\n", stringset);
		return -EINVAL;
	}
}

static u32 qede_get_priv_flags(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);

	return (!!(edev->dev_info.common.num_hwfns > 1)) << QEDE_PRI_FLAG_CMT;
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

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev,
			"Link settings are not allowed to be changed\n");
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

static int qede_nway_reset(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;
	struct qed_link_params link_params;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev,
			"Link settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	if (!netif_running(dev))
		return 0;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);
	if (!current_link.link_up)
		return 0;

	/* Toggle the link */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = false;
	edev->ops->common->set_link(edev->cdev, &link_params);
	link_params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &link_params);

	return 0;
}

static u32 qede_get_link(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	return current_link.link_up;
}

static void qede_get_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct qede_dev *edev = netdev_priv(dev);

	ering->rx_max_pending = NUM_RX_BDS_MAX;
	ering->rx_pending = edev->q_num_rx_buffers;
	ering->tx_max_pending = NUM_TX_BDS_MAX;
	ering->tx_pending = edev->q_num_tx_buffers;
}

static int qede_set_ringparam(struct net_device *dev,
			      struct ethtool_ringparam *ering)
{
	struct qede_dev *edev = netdev_priv(dev);

	DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
		   "Set ring params command parameters: rx_pending = %d, tx_pending = %d\n",
		   ering->rx_pending, ering->tx_pending);

	/* Validate legality of configuration */
	if (ering->rx_pending > NUM_RX_BDS_MAX ||
	    ering->rx_pending < NUM_RX_BDS_MIN ||
	    ering->tx_pending > NUM_TX_BDS_MAX ||
	    ering->tx_pending < NUM_TX_BDS_MIN) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Can only support Rx Buffer size [0%08x,...,0x%08x] and Tx Buffer size [0x%08x,...,0x%08x]\n",
			   NUM_RX_BDS_MIN, NUM_RX_BDS_MAX,
			   NUM_TX_BDS_MIN, NUM_TX_BDS_MAX);
		return -EINVAL;
	}

	/* Change ring size and re-load */
	edev->q_num_rx_buffers = ering->rx_pending;
	edev->q_num_tx_buffers = ering->tx_pending;

	if (netif_running(edev->ndev))
		qede_reload(edev, NULL, NULL);

	return 0;
}

static void qede_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_output current_link;

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	if (current_link.pause_config & QED_LINK_PAUSE_AUTONEG_ENABLE)
		epause->autoneg = true;
	if (current_link.pause_config & QED_LINK_PAUSE_RX_ENABLE)
		epause->rx_pause = true;
	if (current_link.pause_config & QED_LINK_PAUSE_TX_ENABLE)
		epause->tx_pause = true;

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "ethtool_pauseparam: cmd %d  autoneg %d  rx_pause %d  tx_pause %d\n",
		   epause->cmd, epause->autoneg, epause->rx_pause,
		   epause->tx_pause);
}

static int qede_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *epause)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qed_link_params params;
	struct qed_link_output current_link;

	if (!edev->ops || !edev->ops->common->can_link_change(edev->cdev)) {
		DP_INFO(edev,
			"Pause settings are not allowed to be changed\n");
		return -EOPNOTSUPP;
	}

	memset(&current_link, 0, sizeof(current_link));
	edev->ops->common->get_link(edev->cdev, &current_link);

	memset(&params, 0, sizeof(params));
	params.override_flags |= QED_LINK_OVERRIDE_PAUSE_CONFIG;
	if (epause->autoneg) {
		if (!(current_link.supported_caps & SUPPORTED_Autoneg)) {
			DP_INFO(edev, "autoneg not supported\n");
			return -EINVAL;
		}
		params.pause_config |= QED_LINK_PAUSE_AUTONEG_ENABLE;
	}
	if (epause->rx_pause)
		params.pause_config |= QED_LINK_PAUSE_RX_ENABLE;
	if (epause->tx_pause)
		params.pause_config |= QED_LINK_PAUSE_TX_ENABLE;

	params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &params);

	return 0;
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

static int qede_set_phys_id(struct net_device *dev,
			    enum ethtool_phys_id_state state)
{
	struct qede_dev *edev = netdev_priv(dev);
	u8 led_state = 0;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		led_state = QED_LED_MODE_ON;
		break;

	case ETHTOOL_ID_OFF:
		led_state = QED_LED_MODE_OFF;
		break;

	case ETHTOOL_ID_INACTIVE:
		led_state = QED_LED_MODE_RESTORE;
		break;
	}

	edev->ops->common->set_led(edev->cdev, led_state);

	return 0;
}

static int qede_get_rss_flags(struct qede_dev *edev, struct ethtool_rxnfc *info)
{
	info->data = RXH_IP_SRC | RXH_IP_DST;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (edev->rss_params.rss_caps & QED_RSS_IPV4_UDP)
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V6_FLOW:
		if (edev->rss_params.rss_caps & QED_RSS_IPV6_UDP)
			info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		info->data = 0;
		break;
	}

	return 0;
}

static int qede_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			  u32 *rules __always_unused)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = edev->num_rss;
		return 0;
	case ETHTOOL_GRXFH:
		return qede_get_rss_flags(edev, info);
	default:
		DP_ERR(edev, "Command parameters not supported\n");
		return -EOPNOTSUPP;
	}
}

static int qede_set_rss_flags(struct qede_dev *edev, struct ethtool_rxnfc *info)
{
	struct qed_update_vport_params vport_update_params;
	u8 set_caps = 0, clr_caps = 0;

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Set rss flags command parameters: flow type = %d, data = %llu\n",
		   info->flow_type, info->data);

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		/* For TCP only 4-tuple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST |
				  RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	case UDP_V4_FLOW:
		/* For UDP either 2-tuple hash or 4-tuple hash is supported */
		if (info->data == (RXH_IP_SRC | RXH_IP_DST |
				   RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			set_caps = QED_RSS_IPV4_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple enabled\n");
		} else if (info->data == (RXH_IP_SRC | RXH_IP_DST)) {
			clr_caps = QED_RSS_IPV4_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple disabled\n");
		} else {
			return -EINVAL;
		}
		break;
	case UDP_V6_FLOW:
		/* For UDP either 2-tuple hash or 4-tuple hash is supported */
		if (info->data == (RXH_IP_SRC | RXH_IP_DST |
				   RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			set_caps = QED_RSS_IPV6_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple enabled\n");
		} else if (info->data == (RXH_IP_SRC | RXH_IP_DST)) {
			clr_caps = QED_RSS_IPV6_UDP;
			DP_VERBOSE(edev, QED_MSG_DEBUG,
				   "UDP 4-tuple disabled\n");
		} else {
			return -EINVAL;
		}
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		/* For IP only 2-tuple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST)) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IP_USER_FLOW:
	case ETHER_FLOW:
		/* RSS is not supported for these protocols */
		if (info->data) {
			DP_INFO(edev, "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;
	default:
		return -EINVAL;
	}

	/* No action is needed if there is no change in the rss capability */
	if (edev->rss_params.rss_caps == ((edev->rss_params.rss_caps &
					   ~clr_caps) | set_caps))
		return 0;

	/* Update internal configuration */
	edev->rss_params.rss_caps = (edev->rss_params.rss_caps & ~clr_caps) |
				    set_caps;
	edev->rss_params_inited |= QEDE_RSS_CAPS_INITED;

	/* Re-configure if possible */
	if (netif_running(edev->ndev)) {
		memset(&vport_update_params, 0, sizeof(vport_update_params));
		vport_update_params.update_rss_flg = 1;
		vport_update_params.vport_id = 0;
		memcpy(&vport_update_params.rss_params, &edev->rss_params,
		       sizeof(vport_update_params.rss_params));
		return edev->ops->vport_update(edev->cdev,
					       &vport_update_params);
	}

	return 0;
}

static int qede_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info)
{
	struct qede_dev *edev = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		return qede_set_rss_flags(edev, info);
	default:
		DP_INFO(edev, "Command parameters not supported\n");
		return -EOPNOTSUPP;
	}
}

static u32 qede_get_rxfh_indir_size(struct net_device *dev)
{
	return QED_RSS_IND_TABLE_SIZE;
}

static u32 qede_get_rxfh_key_size(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);

	return sizeof(edev->rss_params.rss_key);
}

static int qede_get_rxfh(struct net_device *dev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct qede_dev *edev = netdev_priv(dev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++)
		indir[i] = edev->rss_params.rss_ind_table[i];

	if (key)
		memcpy(key, edev->rss_params.rss_key,
		       qede_get_rxfh_key_size(dev));

	return 0;
}

static int qede_set_rxfh(struct net_device *dev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct qed_update_vport_params vport_update_params;
	struct qede_dev *edev = netdev_priv(dev);
	int i;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!indir && !key)
		return 0;

	if (indir) {
		for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++)
			edev->rss_params.rss_ind_table[i] = indir[i];
		edev->rss_params_inited |= QEDE_RSS_INDIR_INITED;
	}

	if (key) {
		memcpy(&edev->rss_params.rss_key, key,
		       qede_get_rxfh_key_size(dev));
		edev->rss_params_inited |= QEDE_RSS_KEY_INITED;
	}

	if (netif_running(edev->ndev)) {
		memset(&vport_update_params, 0, sizeof(vport_update_params));
		vport_update_params.update_rss_flg = 1;
		vport_update_params.vport_id = 0;
		memcpy(&vport_update_params.rss_params, &edev->rss_params,
		       sizeof(vport_update_params.rss_params));
		return edev->ops->vport_update(edev->cdev,
					       &vport_update_params);
	}

	return 0;
}

/* This function enables the interrupt generation and the NAPI on the device */
static void qede_netif_start(struct qede_dev *edev)
{
	int i;

	if (!netif_running(edev->ndev))
		return;

	for_each_rss(i) {
		/* Update and reenable interrupts */
		qed_sb_ack(edev->fp_array[i].sb_info, IGU_INT_ENABLE, 1);
		napi_enable(&edev->fp_array[i].napi);
	}
}

/* This function disables the NAPI and the interrupt generation on the device */
static void qede_netif_stop(struct qede_dev *edev)
{
	int i;

	for_each_rss(i) {
		napi_disable(&edev->fp_array[i].napi);
		/* Disable interrupts */
		qed_sb_ack(edev->fp_array[i].sb_info, IGU_INT_DISABLE, 0);
	}
}

static int qede_selftest_transmit_traffic(struct qede_dev *edev,
					  struct sk_buff *skb)
{
	struct qede_tx_queue *txq = &edev->fp_array[0].txqs[0];
	struct eth_tx_1st_bd *first_bd;
	dma_addr_t mapping;
	int i, idx, val;

	/* Fill the entry in the SW ring and the BDs in the FW ring */
	idx = txq->sw_tx_prod & NUM_TX_BDS_MAX;
	txq->sw_tx_ring[idx].skb = skb;
	first_bd = qed_chain_produce(&txq->tx_pbl);
	memset(first_bd, 0, sizeof(*first_bd));
	val = 1 << ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT;
	first_bd->data.bd_flags.bitfields = val;
	val = skb->len & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK;
	first_bd->data.bitfields |= (val << ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT);

	/* Map skb linear data for DMA and set in the first BD */
	mapping = dma_map_single(&edev->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		DP_NOTICE(edev, "SKB mapping failed\n");
		return -ENOMEM;
	}
	BD_SET_UNMAP_ADDR_LEN(first_bd, mapping, skb_headlen(skb));

	/* update the first BD with the actual num BDs */
	first_bd->data.nbds = 1;
	txq->sw_tx_prod++;
	/* 'next page' entries are counted in the producer value */
	val = cpu_to_le16(qed_chain_get_prod_idx(&txq->tx_pbl));
	txq->tx_db.data.bd_prod = val;

	/* wmb makes sure that the BDs data is updated before updating the
	 * producer, otherwise FW may read old data from the BDs.
	 */
	wmb();
	barrier();
	writel(txq->tx_db.raw, txq->doorbell_addr);

	/* mmiowb is needed to synchronize doorbell writes from more than one
	 * processor. It guarantees that the write arrives to the device before
	 * the queue lock is released and another start_xmit is called (possibly
	 * on another CPU). Without this barrier, the next doorbell can bypass
	 * this doorbell. This is applicable to IA64/Altix systems.
	 */
	mmiowb();

	for (i = 0; i < QEDE_SELFTEST_POLL_COUNT; i++) {
		if (qede_txq_has_work(txq))
			break;
		usleep_range(100, 200);
	}

	if (!qede_txq_has_work(txq)) {
		DP_NOTICE(edev, "Tx completion didn't happen\n");
		return -1;
	}

	first_bd = (struct eth_tx_1st_bd *)qed_chain_consume(&txq->tx_pbl);
	dma_unmap_page(&edev->pdev->dev, BD_UNMAP_ADDR(first_bd),
		       BD_UNMAP_LEN(first_bd), DMA_TO_DEVICE);
	txq->sw_tx_cons++;
	txq->sw_tx_ring[idx].skb = NULL;

	return 0;
}

static int qede_selftest_receive_traffic(struct qede_dev *edev)
{
	struct qede_rx_queue *rxq = edev->fp_array[0].rxq;
	u16 hw_comp_cons, sw_comp_cons, sw_rx_index, len;
	struct eth_fast_path_rx_reg_cqe *fp_cqe;
	struct sw_rx_data *sw_rx_data;
	union eth_rx_cqe *cqe;
	u8 *data_ptr;
	int i;

	/* The packet is expected to receive on rx-queue 0 even though RSS is
	 * enabled. This is because the queue 0 is configured as the default
	 * queue and that the loopback traffic is not IP.
	 */
	for (i = 0; i < QEDE_SELFTEST_POLL_COUNT; i++) {
		if (qede_has_rx_work(rxq))
			break;
		usleep_range(100, 200);
	}

	if (!qede_has_rx_work(rxq)) {
		DP_NOTICE(edev, "Failed to receive the traffic\n");
		return -1;
	}

	hw_comp_cons = le16_to_cpu(*rxq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);

	/* Memory barrier to prevent the CPU from doing speculative reads of CQE
	 * / BD before reading hw_comp_cons. If the CQE is read before it is
	 * written by FW, then FW writes CQE and SB, and then the CPU reads the
	 * hw_comp_cons, it will use an old CQE.
	 */
	rmb();

	/* Get the CQE from the completion ring */
	cqe = (union eth_rx_cqe *)qed_chain_consume(&rxq->rx_comp_ring);

	/* Get the data from the SW ring */
	sw_rx_index = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
	sw_rx_data = &rxq->sw_rx_ring[sw_rx_index];
	fp_cqe = &cqe->fast_path_regular;
	len =  le16_to_cpu(fp_cqe->len_on_first_bd);
	data_ptr = (u8 *)(page_address(sw_rx_data->data) +
		     fp_cqe->placement_offset + sw_rx_data->page_offset);
	for (i = ETH_HLEN; i < len; i++)
		if (data_ptr[i] != (unsigned char)(i & 0xff)) {
			DP_NOTICE(edev, "Loopback test failed\n");
			qede_recycle_rx_bd_ring(rxq, edev, 1);
			return -1;
		}

	qede_recycle_rx_bd_ring(rxq, edev, 1);

	return 0;
}

static int qede_selftest_run_loopback(struct qede_dev *edev, u32 loopback_mode)
{
	struct qed_link_params link_params;
	struct sk_buff *skb = NULL;
	int rc = 0, i;
	u32 pkt_size;
	u8 *packet;

	if (!netif_running(edev->ndev)) {
		DP_NOTICE(edev, "Interface is down\n");
		return -EINVAL;
	}

	qede_netif_stop(edev);

	/* Bring up the link in Loopback mode */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	link_params.override_flags = QED_LINK_OVERRIDE_LOOPBACK_MODE;
	link_params.loopback_mode = loopback_mode;
	edev->ops->common->set_link(edev->cdev, &link_params);

	/* Wait for loopback configuration to apply */
	msleep_interruptible(500);

	/* prepare the loopback packet */
	pkt_size = edev->ndev->mtu + ETH_HLEN;

	skb = netdev_alloc_skb(edev->ndev, pkt_size);
	if (!skb) {
		DP_INFO(edev, "Can't allocate skb\n");
		rc = -ENOMEM;
		goto test_loopback_exit;
	}
	packet = skb_put(skb, pkt_size);
	ether_addr_copy(packet, edev->ndev->dev_addr);
	ether_addr_copy(packet + ETH_ALEN, edev->ndev->dev_addr);
	memset(packet + (2 * ETH_ALEN), 0x77, (ETH_HLEN - (2 * ETH_ALEN)));
	for (i = ETH_HLEN; i < pkt_size; i++)
		packet[i] = (unsigned char)(i & 0xff);

	rc = qede_selftest_transmit_traffic(edev, skb);
	if (rc)
		goto test_loopback_exit;

	rc = qede_selftest_receive_traffic(edev);
	if (rc)
		goto test_loopback_exit;

	DP_VERBOSE(edev, NETIF_MSG_RX_STATUS, "Loopback test successful\n");

test_loopback_exit:
	dev_kfree_skb(skb);

	/* Bring up the link in Normal mode */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	link_params.override_flags = QED_LINK_OVERRIDE_LOOPBACK_MODE;
	link_params.loopback_mode = QED_LINK_LOOPBACK_NONE;
	edev->ops->common->set_link(edev->cdev, &link_params);

	/* Wait for loopback configuration to apply */
	msleep_interruptible(500);

	qede_netif_start(edev);

	return rc;
}

static void qede_self_test(struct net_device *dev,
			   struct ethtool_test *etest, u64 *buf)
{
	struct qede_dev *edev = netdev_priv(dev);

	DP_VERBOSE(edev, QED_MSG_DEBUG,
		   "Self-test command parameters: offline = %d, external_lb = %d\n",
		   (etest->flags & ETH_TEST_FL_OFFLINE),
		   (etest->flags & ETH_TEST_FL_EXTERNAL_LB) >> 2);

	memset(buf, 0, sizeof(u64) * QEDE_ETHTOOL_TEST_MAX);

	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		if (qede_selftest_run_loopback(edev,
					       QED_LINK_LOOPBACK_INT_PHY)) {
			buf[QEDE_ETHTOOL_INT_LOOPBACK] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
	}

	if (edev->ops->common->selftest->selftest_interrupt(edev->cdev)) {
		buf[QEDE_ETHTOOL_INTERRUPT_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_memory(edev->cdev)) {
		buf[QEDE_ETHTOOL_MEMORY_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_register(edev->cdev)) {
		buf[QEDE_ETHTOOL_REGISTER_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (edev->ops->common->selftest->selftest_clock(edev->cdev)) {
		buf[QEDE_ETHTOOL_CLOCK_TEST] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
}

static const struct ethtool_ops qede_ethtool_ops = {
	.get_settings = qede_get_settings,
	.set_settings = qede_set_settings,
	.get_drvinfo = qede_get_drvinfo,
	.get_msglevel = qede_get_msglevel,
	.set_msglevel = qede_set_msglevel,
	.nway_reset = qede_nway_reset,
	.get_link = qede_get_link,
	.get_ringparam = qede_get_ringparam,
	.set_ringparam = qede_set_ringparam,
	.get_pauseparam = qede_get_pauseparam,
	.set_pauseparam = qede_set_pauseparam,
	.get_strings = qede_get_strings,
	.set_phys_id = qede_set_phys_id,
	.get_ethtool_stats = qede_get_ethtool_stats,
	.get_priv_flags = qede_get_priv_flags,
	.get_sset_count = qede_get_sset_count,
	.get_rxnfc = qede_get_rxnfc,
	.set_rxnfc = qede_set_rxnfc,
	.get_rxfh_indir_size = qede_get_rxfh_indir_size,
	.get_rxfh_key_size = qede_get_rxfh_key_size,
	.get_rxfh = qede_get_rxfh,
	.set_rxfh = qede_set_rxfh,
	.get_channels = qede_get_channels,
	.set_channels = qede_set_channels,
	.self_test = qede_self_test,
};

static const struct ethtool_ops qede_vf_ethtool_ops = {
	.get_settings = qede_get_settings,
	.get_drvinfo = qede_get_drvinfo,
	.get_msglevel = qede_get_msglevel,
	.set_msglevel = qede_set_msglevel,
	.get_link = qede_get_link,
	.get_ringparam = qede_get_ringparam,
	.set_ringparam = qede_set_ringparam,
	.get_strings = qede_get_strings,
	.get_ethtool_stats = qede_get_ethtool_stats,
	.get_priv_flags = qede_get_priv_flags,
	.get_sset_count = qede_get_sset_count,
	.get_rxnfc = qede_get_rxnfc,
	.set_rxnfc = qede_set_rxnfc,
	.get_rxfh_indir_size = qede_get_rxfh_indir_size,
	.get_rxfh_key_size = qede_get_rxfh_key_size,
	.get_rxfh = qede_get_rxfh,
	.set_rxfh = qede_set_rxfh,
	.get_channels = qede_get_channels,
	.set_channels = qede_set_channels,
};

void qede_set_ethtool_ops(struct net_device *dev)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (IS_VF(edev))
		dev->ethtool_ops = &qede_vf_ethtool_ops;
	else
		dev->ethtool_ops = &qede_ethtool_ops;
}
