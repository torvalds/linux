/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-ethtool.c: Driver for Neterion Inc's X3100 Series 10GbE PCIe I/O
 *                 Virtualized Server Adapter.
 * Copyright(c) 2002-2009 Neterion Inc.
 ******************************************************************************/
#include<linux/ethtool.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "vxge-ethtool.h"

/**
 * vxge_ethtool_sset - Sets different link parameters.
 * @dev: device pointer.
 * @info: pointer to the structure with parameters given by ethtool to set
 * link information.
 *
 * The function sets different link parameters provided by the user onto
 * the NIC.
 * Return value:
 * 0 on success.
 */

static int vxge_ethtool_sset(struct net_device *dev, struct ethtool_cmd *info)
{
	/* We currently only support 10Gb/FULL */
	if ((info->autoneg == AUTONEG_ENABLE) ||
	    (info->speed != SPEED_10000) || (info->duplex != DUPLEX_FULL))
		return -EINVAL;

	return 0;
}

/**
 * vxge_ethtool_gset - Return link specific information.
 * @dev: device pointer.
 * @info: pointer to the structure with parameters given by ethtool
 * to return link information.
 *
 * Returns link specific information like speed, duplex etc.. to ethtool.
 * Return value :
 * return 0 on success.
 */
static int vxge_ethtool_gset(struct net_device *dev, struct ethtool_cmd *info)
{
	info->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	info->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	info->port = PORT_FIBRE;

	info->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(dev)) {
		info->speed = SPEED_10000;
		info->duplex = DUPLEX_FULL;
	} else {
		info->speed = -1;
		info->duplex = -1;
	}

	info->autoneg = AUTONEG_DISABLE;
	return 0;
}

/**
 * vxge_ethtool_gdrvinfo - Returns driver specific information.
 * @dev: device pointer.
 * @info: pointer to the structure with parameters given by ethtool to
 * return driver information.
 *
 * Returns driver specefic information like name, version etc.. to ethtool.
 */
static void vxge_ethtool_gdrvinfo(struct net_device *dev,
			struct ethtool_drvinfo *info)
{
	struct vxgedev *vdev;
	vdev = (struct vxgedev *)netdev_priv(dev);
	strlcpy(info->driver, VXGE_DRIVER_NAME, sizeof(VXGE_DRIVER_NAME));
	strlcpy(info->version, DRV_VERSION, sizeof(DRV_VERSION));
	strlcpy(info->fw_version, vdev->fw_version, VXGE_HW_FW_STRLEN);
	strlcpy(info->bus_info, pci_name(vdev->pdev), sizeof(info->bus_info));
	info->regdump_len = sizeof(struct vxge_hw_vpath_reg)
				* vdev->no_of_vpath;

	info->n_stats = STAT_LEN;
}

/**
 * vxge_ethtool_gregs - dumps the entire space of Titan into the buffer.
 * @dev: device pointer.
 * @regs: pointer to the structure with parameters given by ethtool for
 * dumping the registers.
 * @reg_space: The input argumnet into which all the registers are dumped.
 *
 * Dumps the vpath register space of Titan NIC into the user given
 * buffer area.
 */
static void vxge_ethtool_gregs(struct net_device *dev,
			struct ethtool_regs *regs, void *space)
{
	int index, offset;
	enum vxge_hw_status status;
	u64 reg;
	u8 *reg_space = (u8 *) space;
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	struct __vxge_hw_device  *hldev = (struct __vxge_hw_device *)
					pci_get_drvdata(vdev->pdev);

	regs->len = sizeof(struct vxge_hw_vpath_reg) * vdev->no_of_vpath;
	regs->version = vdev->pdev->subsystem_device;
	for (index = 0; index < vdev->no_of_vpath; index++) {
		for (offset = 0; offset < sizeof(struct vxge_hw_vpath_reg);
				offset += 8) {
			status = vxge_hw_mgmt_reg_read(hldev,
					vxge_hw_mgmt_reg_type_vpath,
					vdev->vpaths[index].device_id,
					offset, &reg);
			if (status != VXGE_HW_OK) {
				vxge_debug_init(VXGE_ERR,
					"%s:%d Getting reg dump Failed",
						__func__, __LINE__);
				return;
			}

			memcpy((reg_space + offset), &reg, 8);
		}
	}
}

/**
 * vxge_ethtool_idnic - To physically identify the nic on the system.
 * @dev : device pointer.
 * @id : pointer to the structure with identification parameters given by
 * ethtool.
 *
 * Used to physically identify the NIC on the system.
 * The Link LED will blink for a time specified by the user.
 * Return value:
 * 0 on success
 */
static int vxge_ethtool_idnic(struct net_device *dev, u32 data)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	struct __vxge_hw_device  *hldev = (struct __vxge_hw_device  *)
			pci_get_drvdata(vdev->pdev);

	vxge_hw_device_flick_link_led(hldev, VXGE_FLICKER_ON);
	msleep_interruptible(data ? (data * HZ) : VXGE_MAX_FLICKER_TIME);
	vxge_hw_device_flick_link_led(hldev, VXGE_FLICKER_OFF);

	return 0;
}

/**
 * vxge_ethtool_getpause_data - Pause frame frame generation and reception.
 * @dev : device pointer.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * Returns the Pause frame generation and reception capability of the NIC.
 * Return value:
 *  void
 */
static void vxge_ethtool_getpause_data(struct net_device *dev,
					struct ethtool_pauseparam *ep)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	struct __vxge_hw_device  *hldev = (struct __vxge_hw_device  *)
			pci_get_drvdata(vdev->pdev);

	vxge_hw_device_getpause_data(hldev, 0, &ep->tx_pause, &ep->rx_pause);
}

/**
 * vxge_ethtool_setpause_data -  set/reset pause frame generation.
 * @dev : device pointer.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * It can be used to set or reset Pause frame generation or reception
 * support of the NIC.
 * Return value:
 * int, returns 0 on Success
 */
static int vxge_ethtool_setpause_data(struct net_device *dev,
					struct ethtool_pauseparam *ep)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	struct __vxge_hw_device  *hldev = (struct __vxge_hw_device  *)
			pci_get_drvdata(vdev->pdev);

	vxge_hw_device_setpause_data(hldev, 0, ep->tx_pause, ep->rx_pause);

	vdev->config.tx_pause_enable = ep->tx_pause;
	vdev->config.rx_pause_enable = ep->rx_pause;

	return 0;
}

static void vxge_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *estats, u64 *tmp_stats)
{
	int j, k;
	enum vxge_hw_status status;
	enum vxge_hw_status swstatus;
	struct vxge_vpath *vpath = NULL;

	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	struct __vxge_hw_device  *hldev = vdev->devh;
	struct vxge_hw_xmac_stats *xmac_stats;
	struct vxge_hw_device_stats_sw_info *sw_stats;
	struct vxge_hw_device_stats_hw_info *hw_stats;

	u64 *ptr = tmp_stats;

	memset(tmp_stats, 0,
		vxge_ethtool_get_sset_count(dev, ETH_SS_STATS) * sizeof(u64));

	xmac_stats = kzalloc(sizeof(struct vxge_hw_xmac_stats), GFP_KERNEL);
	if (xmac_stats == NULL) {
		vxge_debug_init(VXGE_ERR,
			"%s : %d Memory Allocation failed for xmac_stats",
				 __func__, __LINE__);
		return;
	}

	sw_stats = kzalloc(sizeof(struct vxge_hw_device_stats_sw_info),
				GFP_KERNEL);
	if (sw_stats == NULL) {
		kfree(xmac_stats);
		vxge_debug_init(VXGE_ERR,
			"%s : %d Memory Allocation failed for sw_stats",
			__func__, __LINE__);
		return;
	}

	hw_stats = kzalloc(sizeof(struct vxge_hw_device_stats_hw_info),
				GFP_KERNEL);
	if (hw_stats == NULL) {
		kfree(xmac_stats);
		kfree(sw_stats);
		vxge_debug_init(VXGE_ERR,
			"%s : %d Memory Allocation failed for hw_stats",
			__func__, __LINE__);
		return;
	}

	*ptr++ = 0;
	status = vxge_hw_device_xmac_stats_get(hldev, xmac_stats);
	if (status != VXGE_HW_OK) {
		if (status != VXGE_HW_ERR_PRIVILAGED_OPEARATION) {
			vxge_debug_init(VXGE_ERR,
				"%s : %d Failure in getting xmac stats",
				__func__, __LINE__);
		}
	}
	swstatus = vxge_hw_driver_stats_get(hldev, sw_stats);
	if (swstatus != VXGE_HW_OK) {
		vxge_debug_init(VXGE_ERR,
			"%s : %d Failure in getting sw stats",
			__func__, __LINE__);
	}

	status = vxge_hw_device_stats_get(hldev, hw_stats);
	if (status != VXGE_HW_OK) {
		vxge_debug_init(VXGE_ERR,
			"%s : %d hw_stats_get error", __func__, __LINE__);
	}

	for (k = 0; k < vdev->no_of_vpath; k++) {
		struct vxge_hw_vpath_stats_hw_info *vpath_info;

		vpath = &vdev->vpaths[k];
		j = vpath->device_id;
		vpath_info = hw_stats->vpath_info[j];
		if (!vpath_info) {
			memset(ptr, 0, (VXGE_HW_VPATH_TX_STATS_LEN +
				VXGE_HW_VPATH_RX_STATS_LEN) * sizeof(u64));
			ptr += (VXGE_HW_VPATH_TX_STATS_LEN +
				VXGE_HW_VPATH_RX_STATS_LEN);
			continue;
		}

		*ptr++ = vpath_info->tx_stats.tx_ttl_eth_frms;
		*ptr++ = vpath_info->tx_stats.tx_ttl_eth_octets;
		*ptr++ = vpath_info->tx_stats.tx_data_octets;
		*ptr++ = vpath_info->tx_stats.tx_mcast_frms;
		*ptr++ = vpath_info->tx_stats.tx_bcast_frms;
		*ptr++ = vpath_info->tx_stats.tx_ucast_frms;
		*ptr++ = vpath_info->tx_stats.tx_tagged_frms;
		*ptr++ = vpath_info->tx_stats.tx_vld_ip;
		*ptr++ = vpath_info->tx_stats.tx_vld_ip_octets;
		*ptr++ = vpath_info->tx_stats.tx_icmp;
		*ptr++ = vpath_info->tx_stats.tx_tcp;
		*ptr++ = vpath_info->tx_stats.tx_rst_tcp;
		*ptr++ = vpath_info->tx_stats.tx_udp;
		*ptr++ = vpath_info->tx_stats.tx_unknown_protocol;
		*ptr++ = vpath_info->tx_stats.tx_lost_ip;
		*ptr++ = vpath_info->tx_stats.tx_parse_error;
		*ptr++ = vpath_info->tx_stats.tx_tcp_offload;
		*ptr++ = vpath_info->tx_stats.tx_retx_tcp_offload;
		*ptr++ = vpath_info->tx_stats.tx_lost_ip_offload;
		*ptr++ = vpath_info->rx_stats.rx_ttl_eth_frms;
		*ptr++ = vpath_info->rx_stats.rx_vld_frms;
		*ptr++ = vpath_info->rx_stats.rx_offload_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_eth_octets;
		*ptr++ = vpath_info->rx_stats.rx_data_octets;
		*ptr++ = vpath_info->rx_stats.rx_offload_octets;
		*ptr++ = vpath_info->rx_stats.rx_vld_mcast_frms;
		*ptr++ = vpath_info->rx_stats.rx_vld_bcast_frms;
		*ptr++ = vpath_info->rx_stats.rx_accepted_ucast_frms;
		*ptr++ = vpath_info->rx_stats.rx_accepted_nucast_frms;
		*ptr++ = vpath_info->rx_stats.rx_tagged_frms;
		*ptr++ = vpath_info->rx_stats.rx_long_frms;
		*ptr++ = vpath_info->rx_stats.rx_usized_frms;
		*ptr++ = vpath_info->rx_stats.rx_osized_frms;
		*ptr++ = vpath_info->rx_stats.rx_frag_frms;
		*ptr++ = vpath_info->rx_stats.rx_jabber_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_64_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_65_127_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_128_255_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_256_511_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_512_1023_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_1024_1518_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_1519_4095_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_4096_8191_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_8192_max_frms;
		*ptr++ = vpath_info->rx_stats.rx_ttl_gt_max_frms;
		*ptr++ = vpath_info->rx_stats.rx_ip;
		*ptr++ = vpath_info->rx_stats.rx_accepted_ip;
		*ptr++ = vpath_info->rx_stats.rx_ip_octets;
		*ptr++ = vpath_info->rx_stats.rx_err_ip;
		*ptr++ = vpath_info->rx_stats.rx_icmp;
		*ptr++ = vpath_info->rx_stats.rx_tcp;
		*ptr++ = vpath_info->rx_stats.rx_udp;
		*ptr++ = vpath_info->rx_stats.rx_err_tcp;
		*ptr++ = vpath_info->rx_stats.rx_lost_frms;
		*ptr++ = vpath_info->rx_stats.rx_lost_ip;
		*ptr++ = vpath_info->rx_stats.rx_lost_ip_offload;
		*ptr++ = vpath_info->rx_stats.rx_various_discard;
		*ptr++ = vpath_info->rx_stats.rx_sleep_discard;
		*ptr++ = vpath_info->rx_stats.rx_red_discard;
		*ptr++ = vpath_info->rx_stats.rx_queue_full_discard;
		*ptr++ = vpath_info->rx_stats.rx_mpa_ok_frms;
	}
	*ptr++ = 0;
	for (k = 0; k < vdev->max_config_port; k++) {
		*ptr++ = xmac_stats->aggr_stats[k].tx_frms;
		*ptr++ = xmac_stats->aggr_stats[k].tx_data_octets;
		*ptr++ = xmac_stats->aggr_stats[k].tx_mcast_frms;
		*ptr++ = xmac_stats->aggr_stats[k].tx_bcast_frms;
		*ptr++ = xmac_stats->aggr_stats[k].tx_discarded_frms;
		*ptr++ = xmac_stats->aggr_stats[k].tx_errored_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_data_octets;
		*ptr++ = xmac_stats->aggr_stats[k].rx_mcast_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_bcast_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_discarded_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_errored_frms;
		*ptr++ = xmac_stats->aggr_stats[k].rx_unknown_slow_proto_frms;
	}
	*ptr++ = 0;
	for (k = 0; k < vdev->max_config_port; k++) {
		*ptr++ = xmac_stats->port_stats[k].tx_ttl_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_ttl_octets;
		*ptr++ = xmac_stats->port_stats[k].tx_data_octets;
		*ptr++ = xmac_stats->port_stats[k].tx_mcast_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_bcast_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_ucast_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_tagged_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_vld_ip;
		*ptr++ = xmac_stats->port_stats[k].tx_vld_ip_octets;
		*ptr++ = xmac_stats->port_stats[k].tx_icmp;
		*ptr++ = xmac_stats->port_stats[k].tx_tcp;
		*ptr++ = xmac_stats->port_stats[k].tx_rst_tcp;
		*ptr++ = xmac_stats->port_stats[k].tx_udp;
		*ptr++ = xmac_stats->port_stats[k].tx_parse_error;
		*ptr++ = xmac_stats->port_stats[k].tx_unknown_protocol;
		*ptr++ = xmac_stats->port_stats[k].tx_pause_ctrl_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_marker_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_lacpdu_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_drop_ip;
		*ptr++ = xmac_stats->port_stats[k].tx_marker_resp_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_xgmii_char2_match;
		*ptr++ = xmac_stats->port_stats[k].tx_xgmii_char1_match;
		*ptr++ = xmac_stats->port_stats[k].tx_xgmii_column2_match;
		*ptr++ = xmac_stats->port_stats[k].tx_xgmii_column1_match;
		*ptr++ = xmac_stats->port_stats[k].tx_any_err_frms;
		*ptr++ = xmac_stats->port_stats[k].tx_drop_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_vld_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_offload_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_octets;
		*ptr++ = xmac_stats->port_stats[k].rx_data_octets;
		*ptr++ = xmac_stats->port_stats[k].rx_offload_octets;
		*ptr++ = xmac_stats->port_stats[k].rx_vld_mcast_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_vld_bcast_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_accepted_ucast_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_accepted_nucast_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_tagged_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_long_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_usized_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_osized_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_frag_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_jabber_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_64_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_65_127_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_128_255_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_256_511_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_512_1023_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_1024_1518_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_1519_4095_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_4096_8191_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_8192_max_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ttl_gt_max_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_ip;
		*ptr++ = xmac_stats->port_stats[k].rx_accepted_ip;
		*ptr++ = xmac_stats->port_stats[k].rx_ip_octets;
		*ptr++ = xmac_stats->port_stats[k].rx_err_ip;
		*ptr++ = xmac_stats->port_stats[k].rx_icmp;
		*ptr++ = xmac_stats->port_stats[k].rx_tcp;
		*ptr++ = xmac_stats->port_stats[k].rx_udp;
		*ptr++ = xmac_stats->port_stats[k].rx_err_tcp;
		*ptr++ = xmac_stats->port_stats[k].rx_pause_count;
		*ptr++ = xmac_stats->port_stats[k].rx_pause_ctrl_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_unsup_ctrl_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_fcs_err_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_in_rng_len_err_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_out_rng_len_err_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_drop_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_discarded_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_drop_ip;
		*ptr++ = xmac_stats->port_stats[k].rx_drop_udp;
		*ptr++ = xmac_stats->port_stats[k].rx_marker_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_lacpdu_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_unknown_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_marker_resp_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_fcs_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_illegal_pdu_frms;
		*ptr++ = xmac_stats->port_stats[k].rx_switch_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_len_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_rpa_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_l2_mgmt_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_rts_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_trash_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_buff_full_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_red_discard;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_ctrl_err_cnt;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_data_err_cnt;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_char1_match;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_err_sym;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_column1_match;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_char2_match;
		*ptr++ = xmac_stats->port_stats[k].rx_local_fault;
		*ptr++ = xmac_stats->port_stats[k].rx_xgmii_column2_match;
		*ptr++ = xmac_stats->port_stats[k].rx_jettison;
		*ptr++ = xmac_stats->port_stats[k].rx_remote_fault;
	}

	*ptr++ = 0;
	for (k = 0; k < vdev->no_of_vpath; k++) {
		struct vxge_hw_vpath_stats_sw_info *vpath_info;

		vpath = &vdev->vpaths[k];
		j = vpath->device_id;
		vpath_info = (struct vxge_hw_vpath_stats_sw_info *)
				&sw_stats->vpath_info[j];
		*ptr++ = vpath_info->soft_reset_cnt;
		*ptr++ = vpath_info->error_stats.unknown_alarms;
		*ptr++ = vpath_info->error_stats.network_sustained_fault;
		*ptr++ = vpath_info->error_stats.network_sustained_ok;
		*ptr++ = vpath_info->error_stats.kdfcctl_fifo0_overwrite;
		*ptr++ = vpath_info->error_stats.kdfcctl_fifo0_poison;
		*ptr++ = vpath_info->error_stats.kdfcctl_fifo0_dma_error;
		*ptr++ = vpath_info->error_stats.dblgen_fifo0_overflow;
		*ptr++ = vpath_info->error_stats.statsb_pif_chain_error;
		*ptr++ = vpath_info->error_stats.statsb_drop_timeout;
		*ptr++ = vpath_info->error_stats.target_illegal_access;
		*ptr++ = vpath_info->error_stats.ini_serr_det;
		*ptr++ = vpath_info->error_stats.prc_ring_bumps;
		*ptr++ = vpath_info->error_stats.prc_rxdcm_sc_err;
		*ptr++ = vpath_info->error_stats.prc_rxdcm_sc_abort;
		*ptr++ = vpath_info->error_stats.prc_quanta_size_err;
		*ptr++ = vpath_info->ring_stats.common_stats.full_cnt;
		*ptr++ = vpath_info->ring_stats.common_stats.usage_cnt;
		*ptr++ = vpath_info->ring_stats.common_stats.usage_max;
		*ptr++ = vpath_info->ring_stats.common_stats.
					reserve_free_swaps_cnt;
		*ptr++ = vpath_info->ring_stats.common_stats.total_compl_cnt;
		for (j = 0; j < VXGE_HW_DTR_MAX_T_CODE; j++)
			*ptr++ = vpath_info->ring_stats.rxd_t_code_err_cnt[j];
		*ptr++ = vpath_info->fifo_stats.common_stats.full_cnt;
		*ptr++ = vpath_info->fifo_stats.common_stats.usage_cnt;
		*ptr++ = vpath_info->fifo_stats.common_stats.usage_max;
		*ptr++ = vpath_info->fifo_stats.common_stats.
						reserve_free_swaps_cnt;
		*ptr++ = vpath_info->fifo_stats.common_stats.total_compl_cnt;
		*ptr++ = vpath_info->fifo_stats.total_posts;
		*ptr++ = vpath_info->fifo_stats.total_buffers;
		for (j = 0; j < VXGE_HW_DTR_MAX_T_CODE; j++)
			*ptr++ = vpath_info->fifo_stats.txd_t_code_err_cnt[j];
	}

	*ptr++ = 0;
	for (k = 0; k < vdev->no_of_vpath; k++) {
		struct vxge_hw_vpath_stats_hw_info *vpath_info;
		vpath = &vdev->vpaths[k];
		j = vpath->device_id;
		vpath_info = hw_stats->vpath_info[j];
		if (!vpath_info) {
			memset(ptr, 0, VXGE_HW_VPATH_STATS_LEN * sizeof(u64));
			ptr += VXGE_HW_VPATH_STATS_LEN;
			continue;
		}
		*ptr++ = vpath_info->ini_num_mwr_sent;
		*ptr++ = vpath_info->ini_num_mrd_sent;
		*ptr++ = vpath_info->ini_num_cpl_rcvd;
		*ptr++ = vpath_info->ini_num_mwr_byte_sent;
		*ptr++ = vpath_info->ini_num_cpl_byte_rcvd;
		*ptr++ = vpath_info->wrcrdtarb_xoff;
		*ptr++ = vpath_info->rdcrdtarb_xoff;
		*ptr++ = vpath_info->vpath_genstats_count0;
		*ptr++ = vpath_info->vpath_genstats_count1;
		*ptr++ = vpath_info->vpath_genstats_count2;
		*ptr++ = vpath_info->vpath_genstats_count3;
		*ptr++ = vpath_info->vpath_genstats_count4;
		*ptr++ = vpath_info->vpath_genstats_count5;
		*ptr++ = vpath_info->prog_event_vnum0;
		*ptr++ = vpath_info->prog_event_vnum1;
		*ptr++ = vpath_info->prog_event_vnum2;
		*ptr++ = vpath_info->prog_event_vnum3;
		*ptr++ = vpath_info->rx_multi_cast_frame_discard;
		*ptr++ = vpath_info->rx_frm_transferred;
		*ptr++ = vpath_info->rxd_returned;
		*ptr++ = vpath_info->rx_mpa_len_fail_frms;
		*ptr++ = vpath_info->rx_mpa_mrk_fail_frms;
		*ptr++ = vpath_info->rx_mpa_crc_fail_frms;
		*ptr++ = vpath_info->rx_permitted_frms;
		*ptr++ = vpath_info->rx_vp_reset_discarded_frms;
		*ptr++ = vpath_info->rx_wol_frms;
		*ptr++ = vpath_info->tx_vp_reset_discarded_frms;
	}

	*ptr++ = 0;
	*ptr++ = vdev->stats.vpaths_open;
	*ptr++ = vdev->stats.vpath_open_fail;
	*ptr++ = vdev->stats.link_up;
	*ptr++ = vdev->stats.link_down;

	for (k = 0; k < vdev->no_of_vpath; k++) {
		*ptr += vdev->vpaths[k].fifo.stats.tx_frms;
		*(ptr + 1) += vdev->vpaths[k].fifo.stats.tx_errors;
		*(ptr + 2) += vdev->vpaths[k].fifo.stats.tx_bytes;
		*(ptr + 3) += vdev->vpaths[k].fifo.stats.txd_not_free;
		*(ptr + 4) += vdev->vpaths[k].fifo.stats.txd_out_of_desc;
		*(ptr + 5) += vdev->vpaths[k].ring.stats.rx_frms;
		*(ptr + 6) += vdev->vpaths[k].ring.stats.rx_errors;
		*(ptr + 7) += vdev->vpaths[k].ring.stats.rx_bytes;
		*(ptr + 8) += vdev->vpaths[k].ring.stats.rx_mcast;
		*(ptr + 9) += vdev->vpaths[k].fifo.stats.pci_map_fail +
				vdev->vpaths[k].ring.stats.pci_map_fail;
		*(ptr + 10) += vdev->vpaths[k].ring.stats.skb_alloc_fail;
	}

	ptr += 12;

	kfree(xmac_stats);
	kfree(sw_stats);
	kfree(hw_stats);
}

static void vxge_ethtool_get_strings(struct net_device *dev,
			      u32 stringset, u8 *data)
{
	int stat_size = 0;
	int i, j;
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);
	switch (stringset) {
	case ETH_SS_STATS:
		vxge_add_string("VPATH STATISTICS%s\t\t\t",
			&stat_size, data, "");
		for (i = 0; i < vdev->no_of_vpath; i++) {
			vxge_add_string("tx_ttl_eth_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_ttl_eth_octects_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_data_octects_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_mcast_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_bcast_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_ucast_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_tagged_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_vld_ip_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_vld_ip_octects_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_icmp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_tcp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_rst_tcp_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_udp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_unknown_proto_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_lost_ip_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_parse_error_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_tcp_offload_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_retx_tcp_offload_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_lost_ip_offload_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_eth_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_vld_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_offload_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_eth_octects_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_data_octects_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_offload_octects_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_vld_mcast_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_vld_bcast_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_accepted_ucast_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_accepted_nucast_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_tagged_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_long_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_usized_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_osized_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_frag_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_jabber_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_64_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_65_127_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_128_255_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_256_511_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_512_1023_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_1024_1518_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_1519_4095_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_4096_8191_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_8192_max_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ttl_gt_max_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ip%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_accepted_ip_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_ip_octects_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_err_ip_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_icmp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_tcp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_udp_%d\t\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_err_tcp_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_lost_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_lost_ip_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_lost_ip_offload_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_various_discard_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_sleep_discard_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_red_discard_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_queue_full_discard_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_mpa_ok_frms_%d\t\t\t",
					&stat_size, data, i);
		}

		vxge_add_string("\nAGGR STATISTICS%s\t\t\t\t",
			&stat_size, data, "");
		for (i = 0; i < vdev->max_config_port; i++) {
			vxge_add_string("tx_frms_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_data_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_mcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_bcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_discarded_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_errored_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_frms_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_data_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_mcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_bcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_discarded_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_errored_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_unknown_slow_proto_frms_%d\t",
				&stat_size, data, i);
		}

		vxge_add_string("\nPORT STATISTICS%s\t\t\t\t",
			&stat_size, data, "");
		for (i = 0; i < vdev->max_config_port; i++) {
			vxge_add_string("tx_ttl_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_ttl_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_data_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_mcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_bcast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_ucast_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_tagged_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_vld_ip_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_vld_ip_octects_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_icmp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_tcp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_rst_tcp_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_udp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_parse_error_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_unknown_protocol_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_pause_ctrl_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_marker_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_lacpdu_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_drop_ip_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_marker_resp_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_xgmii_char2_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_xgmii_char1_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_xgmii_column2_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_xgmii_column1_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_any_err_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("tx_drop_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_vld_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_offload_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_data_octects_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_offload_octects_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_vld_mcast_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_vld_bcast_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_accepted_ucast_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_accepted_nucast_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_tagged_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_long_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_usized_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_osized_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_frag_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_jabber_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_64_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_65_127_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_128_255_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_256_511_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_512_1023_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_1024_1518_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_1519_4095_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_4096_8191_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_8192_max_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ttl_gt_max_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ip_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_accepted_ip_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_ip_octets_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_err_ip_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_icmp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_tcp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_udp_%d\t\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_err_tcp_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_pause_count_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_pause_ctrl_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_unsup_ctrl_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_fcs_err_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_in_rng_len_err_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_out_rng_len_err_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_drop_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_discard_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_drop_ip_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_drop_udp_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_marker_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_lacpdu_frms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_unknown_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_marker_resp_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_fcs_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_illegal_pdu_frms_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_switch_discard_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_len_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_rpa_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_l2_mgmt_discard_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_rts_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_trash_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_buff_full_discard_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_red_discard_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_ctrl_err_cnt_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_data_err_cnt_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_char1_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_err_sym_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_column1_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_char2_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_local_fault_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_xgmii_column2_match_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_jettison_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("rx_remote_fault_%d\t\t\t",
				&stat_size, data, i);
		}

		vxge_add_string("\n SOFTWARE STATISTICS%s\t\t\t",
			&stat_size, data, "");
		for (i = 0; i < vdev->no_of_vpath; i++) {
			vxge_add_string("soft_reset_cnt_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("unknown_alarms_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("network_sustained_fault_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("network_sustained_ok_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("kdfcctl_fifo0_overwrite_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("kdfcctl_fifo0_poison_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("kdfcctl_fifo0_dma_error_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("dblgen_fifo0_overflow_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("statsb_pif_chain_error_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("statsb_drop_timeout_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("target_illegal_access_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("ini_serr_det_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("prc_ring_bumps_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("prc_rxdcm_sc_err_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("prc_rxdcm_sc_abort_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("prc_quanta_size_err_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("ring_full_cnt_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("ring_usage_cnt_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("ring_usage_max_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("ring_reserve_free_swaps_cnt_%d\t",
				&stat_size, data, i);
			vxge_add_string("ring_total_compl_cnt_%d\t\t",
				&stat_size, data, i);
			for (j = 0; j < VXGE_HW_DTR_MAX_T_CODE; j++)
				vxge_add_string("rxd_t_code_err_cnt%d_%d\t\t",
					&stat_size, data, j, i);
			vxge_add_string("fifo_full_cnt_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("fifo_usage_cnt_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("fifo_usage_max_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("fifo_reserve_free_swaps_cnt_%d\t",
				&stat_size, data, i);
			vxge_add_string("fifo_total_compl_cnt_%d\t\t",
				&stat_size, data, i);
			vxge_add_string("fifo_total_posts_%d\t\t\t",
				&stat_size, data, i);
			vxge_add_string("fifo_total_buffers_%d\t\t",
				&stat_size, data, i);
			for (j = 0; j < VXGE_HW_DTR_MAX_T_CODE; j++)
				vxge_add_string("txd_t_code_err_cnt%d_%d\t\t",
					&stat_size, data, j, i);
		}

		vxge_add_string("\n HARDWARE STATISTICS%s\t\t\t",
				&stat_size, data, "");
		for (i = 0; i < vdev->no_of_vpath; i++) {
			vxge_add_string("ini_num_mwr_sent_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("ini_num_mrd_sent_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("ini_num_cpl_rcvd_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("ini_num_mwr_byte_sent_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("ini_num_cpl_byte_rcvd_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("wrcrdtarb_xoff_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rdcrdtarb_xoff_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count0_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count1_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count2_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count3_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count4_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("vpath_genstats_count5_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("prog_event_vnum0_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("prog_event_vnum1_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("prog_event_vnum2_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("prog_event_vnum3_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_multi_cast_frame_discard_%d\t",
					&stat_size, data, i);
			vxge_add_string("rx_frm_transferred_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rxd_returned_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_mpa_len_fail_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_mpa_mrk_fail_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_mpa_crc_fail_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_permitted_frms_%d\t\t",
					&stat_size, data, i);
			vxge_add_string("rx_vp_reset_discarded_frms_%d\t",
					&stat_size, data, i);
			vxge_add_string("rx_wol_frms_%d\t\t\t",
					&stat_size, data, i);
			vxge_add_string("tx_vp_reset_discarded_frms_%d\t",
					&stat_size, data, i);
		}

		memcpy(data + stat_size, &ethtool_driver_stats_keys,
			sizeof(ethtool_driver_stats_keys));
	}
}

static int vxge_ethtool_get_regs_len(struct net_device *dev)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);

	return sizeof(struct vxge_hw_vpath_reg) * vdev->no_of_vpath;
}

static u32 vxge_get_rx_csum(struct net_device *dev)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);

	return vdev->rx_csum;
}

static int vxge_set_rx_csum(struct net_device *dev, u32 data)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);

	if (data)
		vdev->rx_csum = 1;
	else
		vdev->rx_csum = 0;

	return 0;
}

static int vxge_ethtool_op_set_tso(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= (NETIF_F_TSO | NETIF_F_TSO6);
	else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6);

	return 0;
}

static int vxge_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	struct vxgedev *vdev = (struct vxgedev *)netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return VXGE_TITLE_LEN +
			(vdev->no_of_vpath * VXGE_HW_VPATH_STATS_LEN) +
			(vdev->max_config_port * VXGE_HW_AGGR_STATS_LEN) +
			(vdev->max_config_port * VXGE_HW_PORT_STATS_LEN) +
			(vdev->no_of_vpath * VXGE_HW_VPATH_TX_STATS_LEN) +
			(vdev->no_of_vpath * VXGE_HW_VPATH_RX_STATS_LEN) +
			(vdev->no_of_vpath * VXGE_SW_STATS_LEN) +
			DRIVER_STAT_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops vxge_ethtool_ops = {
	.get_settings		= vxge_ethtool_gset,
	.set_settings		= vxge_ethtool_sset,
	.get_drvinfo		= vxge_ethtool_gdrvinfo,
	.get_regs_len		= vxge_ethtool_get_regs_len,
	.get_regs		= vxge_ethtool_gregs,
	.get_link		= ethtool_op_get_link,
	.get_pauseparam		= vxge_ethtool_getpause_data,
	.set_pauseparam		= vxge_ethtool_setpause_data,
	.get_rx_csum		= vxge_get_rx_csum,
	.set_rx_csum		= vxge_set_rx_csum,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= ethtool_op_set_tx_hw_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= vxge_ethtool_op_set_tso,
	.get_strings		= vxge_ethtool_get_strings,
	.phys_id		= vxge_ethtool_idnic,
	.get_sset_count		= vxge_ethtool_get_sset_count,
	.get_ethtool_stats	= vxge_get_ethtool_stats,
};

void initialize_ethtool_ops(struct net_device *ndev)
{
	SET_ETHTOOL_OPS(ndev, &vxge_ethtool_ops);
}
