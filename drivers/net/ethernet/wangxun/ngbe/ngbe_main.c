// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/aer.h>
#include <linux/etherdevice.h>
#include <net/ip.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "ngbe_type.h"
#include "ngbe_hw.h"
#include "ngbe.h"
char ngbe_driver_name[] = "ngbe";

/* ngbe_pci_tbl - PCI Device ID Table
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ngbe_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL_W), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860LC), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A1), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A1L), 0},
	/* required last entry */
	{ .device = 0 }
};

static void ngbe_mac_set_default_filter(struct ngbe_adapter *adapter, u8 *addr)
{
	struct ngbe_hw *hw = &adapter->hw;

	memcpy(&adapter->mac_table[0].addr, addr, ETH_ALEN);
	adapter->mac_table[0].pools = 1ULL;
	adapter->mac_table[0].state = (NGBE_MAC_STATE_DEFAULT |
				       NGBE_MAC_STATE_IN_USE);
	wx_set_rar(&hw->wxhw, 0, adapter->mac_table[0].addr,
		   adapter->mac_table[0].pools,
		   WX_PSR_MAC_SWC_AD_H_AV);
}

/**
 *  ngbe_init_type_code - Initialize the shared code
 *  @hw: pointer to hardware structure
 **/
static void ngbe_init_type_code(struct ngbe_hw *hw)
{
	int wol_mask = 0, ncsi_mask = 0;
	struct wx_hw *wxhw = &hw->wxhw;
	u16 type_mask = 0;

	wxhw->mac.type = wx_mac_em;
	type_mask = (u16)(wxhw->subsystem_device_id & NGBE_OEM_MASK);
	ncsi_mask = wxhw->subsystem_device_id & NGBE_NCSI_MASK;
	wol_mask = wxhw->subsystem_device_id & NGBE_WOL_MASK;

	switch (type_mask) {
	case NGBE_SUBID_M88E1512_SFP:
	case NGBE_SUBID_LY_M88E1512_SFP:
		hw->phy.type = ngbe_phy_m88e1512_sfi;
		break;
	case NGBE_SUBID_M88E1512_RJ45:
		hw->phy.type = ngbe_phy_m88e1512;
		break;
	case NGBE_SUBID_M88E1512_MIX:
		hw->phy.type = ngbe_phy_m88e1512_unknown;
		break;
	case NGBE_SUBID_YT8521S_SFP:
	case NGBE_SUBID_YT8521S_SFP_GPIO:
	case NGBE_SUBID_LY_YT8521S_SFP:
		hw->phy.type = ngbe_phy_yt8521s_sfi;
		break;
	case NGBE_SUBID_INTERNAL_YT8521S_SFP:
	case NGBE_SUBID_INTERNAL_YT8521S_SFP_GPIO:
		hw->phy.type = ngbe_phy_internal_yt8521s_sfi;
		break;
	case NGBE_SUBID_RGMII_FPGA:
	case NGBE_SUBID_OCP_CARD:
		fallthrough;
	default:
		hw->phy.type = ngbe_phy_internal;
		break;
	}

	if (hw->phy.type == ngbe_phy_internal ||
	    hw->phy.type == ngbe_phy_internal_yt8521s_sfi)
		hw->mac_type = ngbe_mac_type_mdi;
	else
		hw->mac_type = ngbe_mac_type_rgmii;

	hw->wol_enabled = (wol_mask == NGBE_WOL_SUP) ? 1 : 0;
	hw->ncsi_enabled = (ncsi_mask == NGBE_NCSI_MASK ||
			   type_mask == NGBE_SUBID_OCP_CARD) ? 1 : 0;

	switch (type_mask) {
	case NGBE_SUBID_LY_YT8521S_SFP:
	case NGBE_SUBID_LY_M88E1512_SFP:
	case NGBE_SUBID_YT8521S_SFP_GPIO:
	case NGBE_SUBID_INTERNAL_YT8521S_SFP_GPIO:
		hw->gpio_ctrl = 1;
		break;
	default:
		hw->gpio_ctrl = 0;
		break;
	}
}

/**
 * ngbe_init_rss_key - Initialize adapter RSS key
 * @adapter: device handle
 *
 * Allocates and initializes the RSS key if it is not allocated.
 **/
static inline int ngbe_init_rss_key(struct ngbe_adapter *adapter)
{
	u32 *rss_key;

	if (!adapter->rss_key) {
		rss_key = kzalloc(NGBE_RSS_KEY_SIZE, GFP_KERNEL);
		if (unlikely(!rss_key))
			return -ENOMEM;

		netdev_rss_key_fill(rss_key, NGBE_RSS_KEY_SIZE);
		adapter->rss_key = rss_key;
	}

	return 0;
}

/**
 * ngbe_sw_init - Initialize general software structures
 * @adapter: board private structure to initialize
 **/
static int ngbe_sw_init(struct ngbe_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ngbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;
	u16 msix_count = 0;
	int err = 0;

	wxhw->hw_addr = adapter->io_addr;
	wxhw->pdev = pdev;

	/* PCI config space info */
	err = wx_sw_init(wxhw);
	if (err < 0) {
		netif_err(adapter, probe, adapter->netdev,
			  "Read of internal subsystem device id failed\n");
		return err;
	}

	/* mac type, phy type , oem type */
	ngbe_init_type_code(hw);

	wxhw->mac.max_rx_queues = NGBE_MAX_RX_QUEUES;
	wxhw->mac.max_tx_queues = NGBE_MAX_TX_QUEUES;
	wxhw->mac.num_rar_entries = NGBE_RAR_ENTRIES;
	/* Set common capability flags and settings */
	adapter->max_q_vectors = NGBE_MAX_MSIX_VECTORS;

	err = wx_get_pcie_msix_counts(wxhw, &msix_count, NGBE_MAX_MSIX_VECTORS);
	if (err)
		dev_err(&pdev->dev, "Do not support MSI-X\n");
	wxhw->mac.max_msix_vectors = msix_count;

	adapter->mac_table = kcalloc(wxhw->mac.num_rar_entries,
				     sizeof(struct ngbe_mac_addr),
				     GFP_KERNEL);
	if (!adapter->mac_table) {
		dev_err(&pdev->dev, "mac_table allocation failed: %d\n", err);
		return -ENOMEM;
	}

	if (ngbe_init_rss_key(adapter))
		return -ENOMEM;

	/* enable itr by default in dynamic mode */
	adapter->rx_itr_setting = 1;
	adapter->tx_itr_setting = 1;

	/* set default ring sizes */
	adapter->tx_ring_count = NGBE_DEFAULT_TXD;
	adapter->rx_ring_count = NGBE_DEFAULT_RXD;

	/* set default work limits */
	adapter->tx_work_limit = NGBE_DEFAULT_TX_WORK;
	adapter->rx_work_limit = NGBE_DEFAULT_RX_WORK;

	return 0;
}

static void ngbe_down(struct ngbe_adapter *adapter)
{
	netif_carrier_off(adapter->netdev);
	netif_tx_disable(adapter->netdev);
};

/**
 * ngbe_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).
 **/
static int ngbe_open(struct net_device *netdev)
{
	struct ngbe_adapter *adapter = netdev_priv(netdev);
	struct ngbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;

	wx_control_hw(wxhw, true);

	return 0;
}

/**
 * ngbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int ngbe_close(struct net_device *netdev)
{
	struct ngbe_adapter *adapter = netdev_priv(netdev);

	ngbe_down(adapter);
	wx_control_hw(&adapter->hw.wxhw, false);

	return 0;
}

static netdev_tx_t ngbe_xmit_frame(struct sk_buff *skb,
				   struct net_device *netdev)
{
	return NETDEV_TX_OK;
}

/**
 * ngbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int ngbe_set_mac(struct net_device *netdev, void *p)
{
	struct ngbe_adapter *adapter = netdev_priv(netdev);
	struct wx_hw *wxhw = &adapter->hw.wxhw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, addr->sa_data);
	memcpy(wxhw->mac.addr, addr->sa_data, netdev->addr_len);

	ngbe_mac_set_default_filter(adapter, wxhw->mac.addr);

	return 0;
}

static void ngbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct ngbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	rtnl_lock();
	if (netif_running(netdev))
		ngbe_down(adapter);
	rtnl_unlock();
	wx_control_hw(&adapter->hw.wxhw, false);

	pci_disable_device(pdev);
}

static void ngbe_shutdown(struct pci_dev *pdev)
{
	struct ngbe_adapter *adapter = pci_get_drvdata(pdev);
	bool wake;

	wake = !!adapter->wol;

	ngbe_dev_shutdown(pdev, &wake);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static const struct net_device_ops ngbe_netdev_ops = {
	.ndo_open               = ngbe_open,
	.ndo_stop               = ngbe_close,
	.ndo_start_xmit         = ngbe_xmit_frame,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = ngbe_set_mac,
};

/**
 * ngbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ngbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ngbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int ngbe_probe(struct pci_dev *pdev,
		      const struct pci_device_id __always_unused *ent)
{
	struct ngbe_adapter *adapter = NULL;
	struct ngbe_hw *hw = NULL;
	struct wx_hw *wxhw = NULL;
	struct net_device *netdev;
	u32 e2rom_cksum_cap = 0;
	static int func_nums;
	u16 e2rom_ver = 0;
	u32 etrack_id = 0;
	u32 saved_ver = 0;
	int err;

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
					   ngbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed %d\n", err);
		goto err_pci_disable_dev;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct ngbe_adapter),
					 NGBE_MAX_TX_QUEUES,
					 NGBE_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	wxhw = &hw->wxhw;
	adapter->msg_enable = BIT(3) - 1;

	adapter->io_addr = devm_ioremap(&pdev->dev,
					pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!adapter->io_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	netdev->netdev_ops = &ngbe_netdev_ops;

	netdev->features |= NETIF_F_HIGHDMA;

	adapter->bd_number = func_nums;
	/* setup the private structure */
	err = ngbe_sw_init(adapter);
	if (err)
		goto err_free_mac_table;

	/* check if flash load is done after hw power up */
	err = wx_check_flash_load(wxhw, NGBE_SPI_ILDR_STATUS_PERST);
	if (err)
		goto err_free_mac_table;
	err = wx_check_flash_load(wxhw, NGBE_SPI_ILDR_STATUS_PWRRST);
	if (err)
		goto err_free_mac_table;

	err = wx_mng_present(wxhw);
	if (err) {
		dev_err(&pdev->dev, "Management capability is not present\n");
		goto err_free_mac_table;
	}

	err = ngbe_reset_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "HW Init failed: %d\n", err);
		goto err_free_mac_table;
	}

	if (wxhw->bus.func == 0) {
		wr32(wxhw, NGBE_CALSUM_CAP_STATUS, 0x0);
		wr32(wxhw, NGBE_EEPROM_VERSION_STORE_REG, 0x0);
	} else {
		e2rom_cksum_cap = rd32(wxhw, NGBE_CALSUM_CAP_STATUS);
		saved_ver = rd32(wxhw, NGBE_EEPROM_VERSION_STORE_REG);
	}

	wx_init_eeprom_params(wxhw);
	if (wxhw->bus.func == 0 || e2rom_cksum_cap == 0) {
		/* make sure the EEPROM is ready */
		err = ngbe_eeprom_chksum_hostif(hw);
		if (err) {
			dev_err(&pdev->dev, "The EEPROM Checksum Is Not Valid\n");
			err = -EIO;
			goto err_free_mac_table;
		}
	}

	adapter->wol = 0;
	if (hw->wol_enabled)
		adapter->wol = NGBE_PSR_WKUP_CTL_MAG;

	hw->wol_enabled = !!(adapter->wol);
	wr32(wxhw, NGBE_PSR_WKUP_CTL, adapter->wol);

	device_set_wakeup_enable(&pdev->dev, adapter->wol);

	/* Save off EEPROM version number and Option Rom version which
	 * together make a unique identify for the eeprom
	 */
	if (saved_ver) {
		etrack_id = saved_ver;
	} else {
		wx_read_ee_hostif(wxhw,
				  wxhw->eeprom.sw_region_offset + NGBE_EEPROM_VERSION_H,
				  &e2rom_ver);
		etrack_id = e2rom_ver << 16;
		wx_read_ee_hostif(wxhw,
				  wxhw->eeprom.sw_region_offset + NGBE_EEPROM_VERSION_L,
				  &e2rom_ver);
		etrack_id |= e2rom_ver;
		wr32(wxhw, NGBE_EEPROM_VERSION_STORE_REG, etrack_id);
	}

	eth_hw_addr_set(netdev, wxhw->mac.perm_addr);
	ngbe_mac_set_default_filter(adapter, wxhw->mac.perm_addr);

	err = register_netdev(netdev);
	if (err)
		goto err_register;

	pci_set_drvdata(pdev, adapter);

	netif_info(adapter, probe, netdev,
		   "PHY: %s, PBA No: Wang Xun GbE Family Controller\n",
		   hw->phy.type == ngbe_phy_internal ? "Internal" : "External");
	netif_info(adapter, probe, netdev, "%pM\n", netdev->dev_addr);

	return 0;

err_register:
	wx_control_hw(wxhw, false);
err_free_mac_table:
	kfree(adapter->mac_table);
err_pci_release_regions:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * ngbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ngbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void ngbe_remove(struct pci_dev *pdev)
{
	struct ngbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev;

	netdev = adapter->netdev;
	unregister_netdev(netdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	kfree(adapter->mac_table);
	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static struct pci_driver ngbe_driver = {
	.name     = ngbe_driver_name,
	.id_table = ngbe_pci_tbl,
	.probe    = ngbe_probe,
	.remove   = ngbe_remove,
	.shutdown = ngbe_shutdown,
};

module_pci_driver(ngbe_driver);

MODULE_DEVICE_TABLE(pci, ngbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@net-swift.com>");
MODULE_DESCRIPTION("WangXun(R) Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
