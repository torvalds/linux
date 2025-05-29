// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/phylink.h>
#include <net/udp_tunnel.h>
#include <net/ip.h>
#include <linux/if_vlan.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_lib.h"
#include "../libwx/wx_ptp.h"
#include "../libwx/wx_hw.h"
#include "../libwx/wx_mbx.h"
#include "../libwx/wx_sriov.h"
#include "txgbe_type.h"
#include "txgbe_hw.h"
#include "txgbe_phy.h"
#include "txgbe_aml.h"
#include "txgbe_irq.h"
#include "txgbe_fdir.h"
#include "txgbe_ethtool.h"

char txgbe_driver_name[] = "txgbe";

/* txgbe_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id txgbe_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_SP1000), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_WX1820), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5010), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5110), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5025), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5125), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5040), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_AML5140), 0},
	/* required last entry */
	{ .device = 0 }
};

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

static void txgbe_check_minimum_link(struct wx *wx)
{
	struct pci_dev *pdev;

	pdev = wx->pdev;
	pcie_print_link_status(pdev);
}

/**
 * txgbe_enumerate_functions - Get the number of ports this device has
 * @wx: wx structure
 *
 * This function enumerates the phsyical functions co-located on a single slot,
 * in order to determine how many ports a device has. This is most useful in
 * determining the required GT/s of PCIe bandwidth necessary for optimal
 * performance.
 **/
static int txgbe_enumerate_functions(struct wx *wx)
{
	struct pci_dev *entry, *pdev = wx->pdev;
	int physfns = 0;

	list_for_each_entry(entry, &pdev->bus->devices, bus_list) {
		/* When the devices on the bus don't all match our device ID,
		 * we can't reliably determine the correct number of
		 * functions. This can occur if a function has been direct
		 * attached to a virtual machine using VT-d.
		 */
		if (entry->vendor != pdev->vendor ||
		    entry->device != pdev->device)
			return -EINVAL;

		physfns++;
	}

	return physfns;
}

static void txgbe_sfp_detection_subtask(struct wx *wx)
{
	int err;

	if (!test_bit(WX_FLAG_NEED_SFP_RESET, wx->flags))
		return;

	/* wait for SFP module ready */
	msleep(200);

	err = txgbe_identify_sfp(wx);
	if (err)
		return;

	clear_bit(WX_FLAG_NEED_SFP_RESET, wx->flags);
}

static void txgbe_link_config_subtask(struct wx *wx)
{
	int err;

	if (!test_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags))
		return;

	err = txgbe_set_phy_link(wx);
	if (err)
		return;

	clear_bit(WX_FLAG_NEED_LINK_CONFIG, wx->flags);
}

/**
 * txgbe_service_task - manages and runs subtasks
 * @work: pointer to work_struct containing our data
 **/
static void txgbe_service_task(struct work_struct *work)
{
	struct wx *wx = container_of(work, struct wx, service_task);

	txgbe_sfp_detection_subtask(wx);
	txgbe_link_config_subtask(wx);

	wx_service_event_complete(wx);
}

static void txgbe_init_service(struct wx *wx)
{
	timer_setup(&wx->service_timer, wx_service_timer, 0);
	INIT_WORK(&wx->service_task, txgbe_service_task);
	clear_bit(WX_STATE_SERVICE_SCHED, wx->state);
}

static void txgbe_up_complete(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	u32 reg;

	wx_control_hw(wx, true);
	wx_configure_vectors(wx);

	/* make sure to complete pre-operations */
	smp_mb__before_atomic();
	wx_napi_enable_all(wx);

	switch (wx->mac.type) {
	case wx_mac_aml40:
		reg = rd32(wx, TXGBE_AML_MAC_TX_CFG);
		reg &= ~TXGBE_AML_MAC_TX_CFG_SPEED_MASK;
		reg |= TXGBE_AML_MAC_TX_CFG_SPEED_40G;
		wr32(wx, WX_MAC_TX_CFG, reg);
		txgbe_enable_sec_tx_path(wx);
		netif_carrier_on(wx->netdev);
		break;
	case wx_mac_aml:
		/* Enable TX laser */
		wr32m(wx, WX_GPIO_DR, TXGBE_GPIOBIT_1, 0);
		txgbe_setup_link(wx);
		phylink_start(wx->phylink);
		break;
	case wx_mac_sp:
		phylink_start(wx->phylink);
		break;
	default:
		break;
	}

	/* clear any pending interrupts, may auto mask */
	rd32(wx, WX_PX_IC(0));
	rd32(wx, WX_PX_IC(1));
	rd32(wx, WX_PX_MISC_IC);
	txgbe_irq_enable(wx, true);

	/* enable transmits */
	netif_tx_start_all_queues(netdev);
	mod_timer(&wx->service_timer, jiffies);

	/* Set PF Reset Done bit so PF/VF Mail Ops can work */
	wr32m(wx, WX_CFG_PORT_CTL, WX_CFG_PORT_CTL_PFRSTD,
	      WX_CFG_PORT_CTL_PFRSTD);
	/* update setting rx tx for all active vfs */
	wx_set_all_vfs(wx);
}

static void txgbe_reset(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	u8 old_addr[ETH_ALEN];
	int err;

	err = txgbe_reset_hw(wx);
	if (err != 0)
		wx_err(wx, "Hardware Error: %d\n", err);

	wx_start_hw(wx);
	/* do not flush user set addresses */
	memcpy(old_addr, &wx->mac_table[0].addr, netdev->addr_len);
	wx_flush_sw_mac_table(wx);
	wx_mac_set_default_filter(wx, old_addr);

	if (test_bit(WX_STATE_PTP_RUNNING, wx->state))
		wx_ptp_reset(wx);
}

static void txgbe_disable_device(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	u32 i;

	wx_disable_pcie_master(wx);
	/* disable receives */
	wx_disable_rx(wx);

	/* disable all enabled rx queues */
	for (i = 0; i < wx->num_rx_queues; i++)
		/* this call also flushes the previous write */
		wx_disable_rx_queue(wx, wx->rx_ring[i]);

	netif_tx_stop_all_queues(netdev);
	netif_tx_disable(netdev);

	wx_irq_disable(wx);
	wx_napi_disable_all(wx);

	timer_delete_sync(&wx->service_timer);

	if (wx->bus.func < 2)
		wr32m(wx, TXGBE_MIS_PRB_CTL, TXGBE_MIS_PRB_CTL_LAN_UP(wx->bus.func), 0);
	else
		wx_err(wx, "%s: invalid bus lan id %d\n",
		       __func__, wx->bus.func);

	if (wx->num_vfs) {
		/* Clear EITR Select mapping */
		wr32(wx, WX_PX_ITRSEL, 0);
		/* Mark all the VFs as inactive */
		for (i = 0; i < wx->num_vfs; i++)
			wx->vfinfo[i].clear_to_send = 0;
		/* update setting rx tx for all active vfs */
		wx_set_all_vfs(wx);
	}

	if (!(((wx->subsystem_device_id & WX_NCSI_MASK) == WX_NCSI_SUP) ||
	      ((wx->subsystem_device_id & WX_WOL_MASK) == WX_WOL_SUP))) {
		/* disable mac transmiter */
		wr32m(wx, WX_MAC_TX_CFG, WX_MAC_TX_CFG_TE, 0);
	}

	/* disable transmits in the hardware now that interrupts are off */
	for (i = 0; i < wx->num_tx_queues; i++) {
		u8 reg_idx = wx->tx_ring[i]->reg_idx;

		wr32(wx, WX_PX_TR_CFG(reg_idx), WX_PX_TR_CFG_SWFLSH);
	}

	/* Disable the Tx DMA engine */
	wr32m(wx, WX_TDM_CTL, WX_TDM_CTL_TE, 0);

	wx_update_stats(wx);
}

void txgbe_down(struct wx *wx)
{
	txgbe_disable_device(wx);
	txgbe_reset(wx);

	switch (wx->mac.type) {
	case wx_mac_aml40:
		netif_carrier_off(wx->netdev);
		break;
	case wx_mac_aml:
		phylink_stop(wx->phylink);
		/* Disable TX laser */
		wr32m(wx, WX_GPIO_DR, TXGBE_GPIOBIT_1, TXGBE_GPIOBIT_1);
		break;
	case wx_mac_sp:
		phylink_stop(wx->phylink);
		break;
	default:
		break;
	}

	wx_clean_all_tx_rings(wx);
	wx_clean_all_rx_rings(wx);
}

void txgbe_up(struct wx *wx)
{
	wx_configure(wx);
	wx_ptp_init(wx);
	txgbe_up_complete(wx);
}

/**
 *  txgbe_init_type_code - Initialize the shared code
 *  @wx: pointer to hardware structure
 **/
static void txgbe_init_type_code(struct wx *wx)
{
	u8 device_type = wx->subsystem_device_id & 0xF0;

	switch (wx->device_id) {
	case TXGBE_DEV_ID_SP1000:
	case TXGBE_DEV_ID_WX1820:
		wx->mac.type = wx_mac_sp;
		break;
	case TXGBE_DEV_ID_AML5010:
	case TXGBE_DEV_ID_AML5110:
	case TXGBE_DEV_ID_AML5025:
	case TXGBE_DEV_ID_AML5125:
		wx->mac.type = wx_mac_aml;
		break;
	case TXGBE_DEV_ID_AML5040:
	case TXGBE_DEV_ID_AML5140:
		wx->mac.type = wx_mac_aml40;
		break;
	default:
		wx->mac.type = wx_mac_unknown;
		break;
	}

	switch (device_type) {
	case TXGBE_ID_SFP:
		wx->media_type = wx_media_fiber;
		break;
	case TXGBE_ID_XAUI:
	case TXGBE_ID_SGMII:
		wx->media_type = wx_media_copper;
		break;
	case TXGBE_ID_KR_KX_KX4:
	case TXGBE_ID_MAC_XAUI:
	case TXGBE_ID_MAC_SGMII:
		wx->media_type = wx_media_backplane;
		break;
	case TXGBE_ID_SFI_XAUI:
		if (wx->bus.func == 0)
			wx->media_type = wx_media_fiber;
		else
			wx->media_type = wx_media_copper;
		break;
	default:
		wx->media_type = wx_media_unknown;
		break;
	}
}

/**
 * txgbe_sw_init - Initialize general software structures (struct wx)
 * @wx: board private structure to initialize
 **/
static int txgbe_sw_init(struct wx *wx)
{
	u16 msix_count = 0;
	int err;

	wx->mac.num_rar_entries = TXGBE_RAR_ENTRIES;
	wx->mac.max_tx_queues = TXGBE_MAX_TXQ;
	wx->mac.max_rx_queues = TXGBE_MAX_RXQ;
	wx->mac.mcft_size = TXGBE_MC_TBL_SIZE;
	wx->mac.vft_size = TXGBE_VFT_TBL_SIZE;
	wx->mac.rx_pb_size = TXGBE_RX_PB_SIZE;
	wx->mac.tx_pb_size = TXGBE_TDB_PB_SZ;

	/* PCI config space info */
	err = wx_sw_init(wx);
	if (err < 0)
		return err;

	txgbe_init_type_code(wx);

	/* Set common capability flags and settings */
	wx->max_q_vectors = TXGBE_MAX_MSIX_VECTORS;
	err = wx_get_pcie_msix_counts(wx, &msix_count, TXGBE_MAX_MSIX_VECTORS);
	if (err)
		wx_err(wx, "Do not support MSI-X\n");
	wx->mac.max_msix_vectors = msix_count;

	wx->ring_feature[RING_F_RSS].limit = min_t(int, TXGBE_MAX_RSS_INDICES,
						   num_online_cpus());
	wx->rss_enabled = true;

	wx->ring_feature[RING_F_FDIR].limit = min_t(int, TXGBE_MAX_FDIR_INDICES,
						    num_online_cpus());
	set_bit(WX_FLAG_FDIR_CAPABLE, wx->flags);
	set_bit(WX_FLAG_FDIR_HASH, wx->flags);
	wx->atr_sample_rate = TXGBE_DEFAULT_ATR_SAMPLE_RATE;
	wx->atr = txgbe_atr;
	wx->configure_fdir = txgbe_configure_fdir;

	set_bit(WX_FLAG_RSC_CAPABLE, wx->flags);
	set_bit(WX_FLAG_MULTI_64_FUNC, wx->flags);

	/* enable itr by default in dynamic mode */
	wx->rx_itr_setting = 1;
	wx->tx_itr_setting = 1;

	/* set default ring sizes */
	wx->tx_ring_count = TXGBE_DEFAULT_TXD;
	wx->rx_ring_count = TXGBE_DEFAULT_RXD;
	wx->mbx.size = WX_VXMAILBOX_SIZE;

	/* set default work limits */
	wx->tx_work_limit = TXGBE_DEFAULT_TX_WORK;
	wx->rx_work_limit = TXGBE_DEFAULT_RX_WORK;

	wx->setup_tc = txgbe_setup_tc;
	wx->do_reset = txgbe_do_reset;
	set_bit(0, &wx->fwd_bitmask);

	switch (wx->mac.type) {
	case wx_mac_sp:
		break;
	case wx_mac_aml:
	case wx_mac_aml40:
		set_bit(WX_FLAG_SWFW_RING, wx->flags);
		wx->swfw_index = 0;
		break;
	default:
		break;
	}

	return 0;
}

static void txgbe_init_fdir(struct txgbe *txgbe)
{
	txgbe->fdir_filter_count = 0;
	spin_lock_init(&txgbe->fdir_perfect_lock);
}

/**
 * txgbe_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).
 **/
static int txgbe_open(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);
	int err;

	err = wx_setup_resources(wx);
	if (err)
		goto err_reset;

	wx_configure(wx);

	err = txgbe_request_queue_irqs(wx);
	if (err)
		goto err_free_resources;

	/* Notify the stack of the actual queue counts. */
	err = netif_set_real_num_tx_queues(netdev, wx->num_tx_queues);
	if (err)
		goto err_free_irq;

	err = netif_set_real_num_rx_queues(netdev, wx->num_rx_queues);
	if (err)
		goto err_free_irq;

	wx_ptp_init(wx);

	txgbe_up_complete(wx);

	return 0;

err_free_irq:
	wx_free_irq(wx);
err_free_resources:
	wx_free_resources(wx);
err_reset:
	txgbe_reset(wx);

	return err;
}

/**
 * txgbe_close_suspend - actions necessary to both suspend and close flows
 * @wx: the private wx struct
 *
 * This function should contain the necessary work common to both suspending
 * and closing of the device.
 */
static void txgbe_close_suspend(struct wx *wx)
{
	wx_ptp_suspend(wx);
	txgbe_disable_device(wx);
	wx_free_resources(wx);
}

/**
 * txgbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int txgbe_close(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	wx_ptp_stop(wx);
	txgbe_down(wx);
	wx_free_irq(wx);
	wx_free_resources(wx);
	txgbe_fdir_filter_exit(wx);
	wx_control_hw(wx, false);

	return 0;
}

static void txgbe_dev_shutdown(struct pci_dev *pdev)
{
	struct wx *wx = pci_get_drvdata(pdev);
	struct net_device *netdev;

	netdev = wx->netdev;
	netif_device_detach(netdev);

	rtnl_lock();
	if (netif_running(netdev))
		txgbe_close_suspend(wx);
	rtnl_unlock();

	wx_control_hw(wx, false);

	pci_disable_device(pdev);
}

static void txgbe_shutdown(struct pci_dev *pdev)
{
	txgbe_dev_shutdown(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, false);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * txgbe_setup_tc - routine to configure net_device for multiple traffic
 * classes.
 *
 * @dev: net device to configure
 * @tc: number of traffic classes to enable
 */
int txgbe_setup_tc(struct net_device *dev, u8 tc)
{
	struct wx *wx = netdev_priv(dev);
	struct txgbe *txgbe = wx->priv;

	/* Hardware has to reinitialize queues and interrupts to
	 * match packet buffer alignment. Unfortunately, the
	 * hardware is not flexible enough to do this dynamically.
	 */
	if (netif_running(dev))
		txgbe_close(dev);
	else
		txgbe_reset(wx);

	txgbe_free_misc_irq(txgbe);
	wx_clear_interrupt_scheme(wx);

	if (tc)
		netdev_set_num_tc(dev, tc);
	else
		netdev_reset_tc(dev);

	wx_init_interrupt_scheme(wx);
	txgbe_setup_misc_irq(txgbe);

	if (netif_running(dev))
		txgbe_open(dev);

	return 0;
}

static void txgbe_reinit_locked(struct wx *wx)
{
	int err = 0;

	netif_trans_update(wx->netdev);

	err = wx_set_state_reset(wx);
	if (err) {
		wx_err(wx, "wait device reset timeout\n");
		return;
	}

	txgbe_down(wx);
	txgbe_up(wx);

	clear_bit(WX_STATE_RESETTING, wx->state);
}

void txgbe_do_reset(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	if (netif_running(netdev))
		txgbe_reinit_locked(wx);
	else
		txgbe_reset(wx);
}

static int txgbe_udp_tunnel_sync(struct net_device *dev, unsigned int table)
{
	struct wx *wx = netdev_priv(dev);
	struct udp_tunnel_info ti;

	udp_tunnel_nic_get_port(dev, table, 0, &ti);
	switch (ti.type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		wr32(wx, TXGBE_CFG_VXLAN, ntohs(ti.port));
		break;
	case UDP_TUNNEL_TYPE_VXLAN_GPE:
		wr32(wx, TXGBE_CFG_VXLAN_GPE, ntohs(ti.port));
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		wr32(wx, TXGBE_CFG_GENEVE, ntohs(ti.port));
		break;
	default:
		break;
	}

	return 0;
}

static const struct udp_tunnel_nic_info txgbe_udp_tunnels = {
	.sync_table	= txgbe_udp_tunnel_sync,
	.flags		= UDP_TUNNEL_NIC_INFO_OPEN_ONLY,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN, },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN_GPE, },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
	},
};

static const struct net_device_ops txgbe_netdev_ops = {
	.ndo_open               = txgbe_open,
	.ndo_stop               = txgbe_close,
	.ndo_change_mtu         = wx_change_mtu,
	.ndo_start_xmit         = wx_xmit_frame,
	.ndo_set_rx_mode        = wx_set_rx_mode,
	.ndo_set_features       = wx_set_features,
	.ndo_fix_features       = wx_fix_features,
	.ndo_features_check     = wx_features_check,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = wx_set_mac,
	.ndo_get_stats64        = wx_get_stats64,
	.ndo_vlan_rx_add_vid    = wx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = wx_vlan_rx_kill_vid,
	.ndo_hwtstamp_set       = wx_hwtstamp_set,
	.ndo_hwtstamp_get       = wx_hwtstamp_get,
};

/**
 * txgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in txgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * txgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the wx private structure,
 * and a hardware reset occur.
 **/
static int txgbe_probe(struct pci_dev *pdev,
		       const struct pci_device_id __always_unused *ent)
{
	struct net_device *netdev;
	int err, expected_gts;
	struct wx *wx = NULL;
	struct txgbe *txgbe;

	u16 eeprom_verh = 0, eeprom_verl = 0, offset = 0;
	u16 eeprom_cfg_blkh = 0, eeprom_cfg_blkl = 0;
	u16 build = 0, major = 0, patch = 0;
	u32 etrack_id = 0;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev,
			"No usable DMA configuration, aborting\n");
		goto err_pci_disable_dev;
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev, IORESOURCE_MEM),
					   txgbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_disable_dev;
	}

	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct wx),
					 TXGBE_MAX_TX_QUEUES,
					 TXGBE_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	wx = netdev_priv(netdev);
	wx->netdev = netdev;
	wx->pdev = pdev;

	wx->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	wx->hw_addr = devm_ioremap(&pdev->dev,
				   pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
	if (!wx->hw_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	/* The sapphire supports up to 63 VFs per pf, but physical
	 * function also need one pool for basic networking.
	 */
	pci_sriov_set_totalvfs(pdev, TXGBE_MAX_VFS_DRV_LIMIT);
	wx->driver_name = txgbe_driver_name;
	txgbe_set_ethtool_ops(netdev);
	netdev->netdev_ops = &txgbe_netdev_ops;
	netdev->udp_tunnel_nic_info = &txgbe_udp_tunnels;

	/* setup the private structure */
	err = txgbe_sw_init(wx);
	if (err)
		goto err_pci_release_regions;

	/* check if flash load is done after hw power up */
	err = wx_check_flash_load(wx, TXGBE_SPI_ILDR_STATUS_PERST);
	if (err)
		goto err_free_mac_table;
	err = wx_check_flash_load(wx, TXGBE_SPI_ILDR_STATUS_PWRRST);
	if (err)
		goto err_free_mac_table;

	err = wx_mng_present(wx);
	if (err) {
		dev_err(&pdev->dev, "Management capability is not present\n");
		goto err_free_mac_table;
	}

	err = txgbe_reset_hw(wx);
	if (err) {
		dev_err(&pdev->dev, "HW Init failed: %d\n", err);
		goto err_free_mac_table;
	}

	netdev->features = NETIF_F_SG |
			   NETIF_F_TSO |
			   NETIF_F_TSO6 |
			   NETIF_F_RXHASH |
			   NETIF_F_RXCSUM |
			   NETIF_F_HW_CSUM;

	netdev->gso_partial_features =  NETIF_F_GSO_ENCAP_ALL;
	netdev->features |= netdev->gso_partial_features;
	netdev->features |= NETIF_F_SCTP_CRC;
	netdev->vlan_features |= netdev->features | NETIF_F_TSO_MANGLEID;
	netdev->hw_enc_features |= netdev->vlan_features;
	netdev->features |= NETIF_F_VLAN_FEATURES;
	/* copy netdev features into list of user selectable features */
	netdev->hw_features |= netdev->features | NETIF_F_RXALL;
	netdev->hw_features |= NETIF_F_NTUPLE | NETIF_F_HW_TC;
	netdev->features |= NETIF_F_HIGHDMA;
	netdev->hw_features |= NETIF_F_GRO;
	netdev->features |= NETIF_F_GRO;
	netdev->features |= NETIF_F_RX_UDP_TUNNEL_PORT;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = WX_MAX_JUMBO_FRAME_SIZE -
			  (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

	/* make sure the EEPROM is good */
	err = txgbe_validate_eeprom_checksum(wx, NULL);
	if (err != 0) {
		dev_err(&pdev->dev, "The EEPROM Checksum Is Not Valid\n");
		wr32(wx, WX_MIS_RST, WX_MIS_RST_SW_RST);
		err = -EIO;
		goto err_free_mac_table;
	}

	eth_hw_addr_set(netdev, wx->mac.perm_addr);
	wx_mac_set_default_filter(wx, wx->mac.perm_addr);

	txgbe_init_service(wx);

	err = wx_init_interrupt_scheme(wx);
	if (err)
		goto err_cancel_service;

	/* Save off EEPROM version number and Option Rom version which
	 * together make a unique identify for the eeprom
	 */
	wx_read_ee_hostif(wx,
			  wx->eeprom.sw_region_offset + TXGBE_EEPROM_VERSION_H,
			  &eeprom_verh);
	wx_read_ee_hostif(wx,
			  wx->eeprom.sw_region_offset + TXGBE_EEPROM_VERSION_L,
			  &eeprom_verl);
	etrack_id = (eeprom_verh << 16) | eeprom_verl;

	wx_read_ee_hostif(wx,
			  wx->eeprom.sw_region_offset + TXGBE_ISCSI_BOOT_CONFIG,
			  &offset);

	/* Make sure offset to SCSI block is valid */
	if (!(offset == 0x0) && !(offset == 0xffff)) {
		wx_read_ee_hostif(wx, offset + 0x84, &eeprom_cfg_blkh);
		wx_read_ee_hostif(wx, offset + 0x83, &eeprom_cfg_blkl);

		/* Only display Option Rom if exist */
		if (eeprom_cfg_blkl && eeprom_cfg_blkh) {
			major = eeprom_cfg_blkl >> 8;
			build = (eeprom_cfg_blkl << 8) | (eeprom_cfg_blkh >> 8);
			patch = eeprom_cfg_blkh & 0x00ff;

			snprintf(wx->eeprom_id, sizeof(wx->eeprom_id),
				 "0x%08x, %d.%d.%d", etrack_id, major, build,
				 patch);
		} else {
			snprintf(wx->eeprom_id, sizeof(wx->eeprom_id),
				 "0x%08x", etrack_id);
		}
	} else {
		snprintf(wx->eeprom_id, sizeof(wx->eeprom_id),
			 "0x%08x", etrack_id);
	}

	if (etrack_id < 0x20010)
		dev_warn(&pdev->dev, "Please upgrade the firmware to 0x20010 or above.\n");

	err = txgbe_test_hostif(wx);
	if (err != 0) {
		dev_err(&pdev->dev, "Mismatched Firmware version\n");
		err = -EIO;
		goto err_release_hw;
	}

	txgbe = devm_kzalloc(&pdev->dev, sizeof(*txgbe), GFP_KERNEL);
	if (!txgbe) {
		err = -ENOMEM;
		goto err_release_hw;
	}

	txgbe->wx = wx;
	wx->priv = txgbe;

	txgbe_init_fdir(txgbe);

	err = txgbe_setup_misc_irq(txgbe);
	if (err)
		goto err_release_hw;

	err = txgbe_init_phy(txgbe);
	if (err)
		goto err_free_misc_irq;

	err = register_netdev(netdev);
	if (err)
		goto err_remove_phy;

	pci_set_drvdata(pdev, wx);

	netif_tx_stop_all_queues(netdev);

	/* calculate the expected PCIe bandwidth required for optimal
	 * performance. Note that some older parts will never have enough
	 * bandwidth due to being older generation PCIe parts. We clamp these
	 * parts to ensure that no warning is displayed, as this could confuse
	 * users otherwise.
	 */
	expected_gts = txgbe_enumerate_functions(wx) * 10;

	/* don't check link if we failed to enumerate functions */
	if (expected_gts > 0)
		txgbe_check_minimum_link(wx);
	else
		dev_warn(&pdev->dev, "Failed to enumerate PF devices.\n");

	return 0;

err_remove_phy:
	txgbe_remove_phy(txgbe);
err_free_misc_irq:
	txgbe_free_misc_irq(txgbe);
err_release_hw:
	wx_clear_interrupt_scheme(wx);
	wx_control_hw(wx, false);
err_cancel_service:
	timer_delete_sync(&wx->service_timer);
	cancel_work_sync(&wx->service_task);
err_free_mac_table:
	kfree(wx->rss_key);
	kfree(wx->mac_table);
err_pci_release_regions:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * txgbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * txgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void txgbe_remove(struct pci_dev *pdev)
{
	struct wx *wx = pci_get_drvdata(pdev);
	struct txgbe *txgbe = wx->priv;
	struct net_device *netdev;

	cancel_work_sync(&wx->service_task);

	netdev = wx->netdev;
	wx_disable_sriov(wx);
	unregister_netdev(netdev);

	txgbe_remove_phy(txgbe);
	txgbe_free_misc_irq(txgbe);
	wx_free_isb_resources(wx);

	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	kfree(wx->rss_key);
	kfree(wx->mac_table);
	wx_clear_interrupt_scheme(wx);

	pci_disable_device(pdev);
}

static struct pci_driver txgbe_driver = {
	.name     = txgbe_driver_name,
	.id_table = txgbe_pci_tbl,
	.probe    = txgbe_probe,
	.remove   = txgbe_remove,
	.shutdown = txgbe_shutdown,
	.sriov_configure = wx_pci_sriov_configure,
};

module_pci_driver(txgbe_driver);

MODULE_DEVICE_TABLE(pci, txgbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@trustnetic.com>");
MODULE_DESCRIPTION("WangXun(R) 10/25/40 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
