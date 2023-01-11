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
#include <linux/phy.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "ngbe_type.h"
#include "ngbe_mdio.h"
#include "ngbe_hw.h"

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

/**
 *  ngbe_init_type_code - Initialize the shared code
 *  @wx: pointer to hardware structure
 **/
static void ngbe_init_type_code(struct wx *wx)
{
	int wol_mask = 0, ncsi_mask = 0;
	u16 type_mask = 0, val;

	wx->mac.type = wx_mac_em;
	type_mask = (u16)(wx->subsystem_device_id & NGBE_OEM_MASK);
	ncsi_mask = wx->subsystem_device_id & NGBE_NCSI_MASK;
	wol_mask = wx->subsystem_device_id & NGBE_WOL_MASK;

	val = rd32(wx, WX_CFG_PORT_ST);
	wx->mac_type = (val & BIT(7)) >> 7 ?
		       em_mac_type_rgmii :
		       em_mac_type_mdi;

	wx->wol_enabled = (wol_mask == NGBE_WOL_SUP) ? 1 : 0;
	wx->ncsi_enabled = (ncsi_mask == NGBE_NCSI_MASK ||
			   type_mask == NGBE_SUBID_OCP_CARD) ? 1 : 0;

	switch (type_mask) {
	case NGBE_SUBID_LY_YT8521S_SFP:
	case NGBE_SUBID_LY_M88E1512_SFP:
	case NGBE_SUBID_YT8521S_SFP_GPIO:
	case NGBE_SUBID_INTERNAL_YT8521S_SFP_GPIO:
		wx->gpio_ctrl = 1;
		break;
	default:
		wx->gpio_ctrl = 0;
		break;
	}
}

/**
 * ngbe_init_rss_key - Initialize wx RSS key
 * @wx: device handle
 *
 * Allocates and initializes the RSS key if it is not allocated.
 **/
static inline int ngbe_init_rss_key(struct wx *wx)
{
	u32 *rss_key;

	if (!wx->rss_key) {
		rss_key = kzalloc(WX_RSS_KEY_SIZE, GFP_KERNEL);
		if (unlikely(!rss_key))
			return -ENOMEM;

		netdev_rss_key_fill(rss_key, WX_RSS_KEY_SIZE);
		wx->rss_key = rss_key;
	}

	return 0;
}

/**
 * ngbe_sw_init - Initialize general software structures
 * @wx: board private structure to initialize
 **/
static int ngbe_sw_init(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	u16 msix_count = 0;
	int err = 0;

	wx->mac.num_rar_entries = NGBE_RAR_ENTRIES;
	wx->mac.max_rx_queues = NGBE_MAX_RX_QUEUES;
	wx->mac.max_tx_queues = NGBE_MAX_TX_QUEUES;

	/* PCI config space info */
	err = wx_sw_init(wx);
	if (err < 0) {
		wx_err(wx, "read of internal subsystem device id failed\n");
		return err;
	}

	/* mac type, phy type , oem type */
	ngbe_init_type_code(wx);

	/* Set common capability flags and settings */
	wx->max_q_vectors = NGBE_MAX_MSIX_VECTORS;
	err = wx_get_pcie_msix_counts(wx, &msix_count, NGBE_MAX_MSIX_VECTORS);
	if (err)
		dev_err(&pdev->dev, "Do not support MSI-X\n");
	wx->mac.max_msix_vectors = msix_count;

	if (ngbe_init_rss_key(wx))
		return -ENOMEM;

	/* enable itr by default in dynamic mode */
	wx->rx_itr_setting = 1;
	wx->tx_itr_setting = 1;

	/* set default ring sizes */
	wx->tx_ring_count = NGBE_DEFAULT_TXD;
	wx->rx_ring_count = NGBE_DEFAULT_RXD;

	/* set default work limits */
	wx->tx_work_limit = NGBE_DEFAULT_TX_WORK;
	wx->rx_work_limit = NGBE_DEFAULT_RX_WORK;

	return 0;
}

static void ngbe_disable_device(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;

	/* disable receives */
	wx_disable_rx(wx);
	netif_tx_disable(netdev);
	if (wx->gpio_ctrl)
		ngbe_sfp_modules_txrx_powerctl(wx, false);
}

static void ngbe_down(struct wx *wx)
{
	phy_stop(wx->phydev);
	ngbe_disable_device(wx);
}

static void ngbe_up(struct wx *wx)
{
	if (wx->gpio_ctrl)
		ngbe_sfp_modules_txrx_powerctl(wx, true);
	phy_start(wx->phydev);
}

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
	struct wx *wx = netdev_priv(netdev);
	int err;

	wx_control_hw(wx, true);
	err = ngbe_phy_connect(wx);
	if (err)
		return err;
	ngbe_up(wx);

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
	struct wx *wx = netdev_priv(netdev);

	ngbe_down(wx);
	phy_disconnect(wx->phydev);
	wx_control_hw(wx, false);

	return 0;
}

static netdev_tx_t ngbe_xmit_frame(struct sk_buff *skb,
				   struct net_device *netdev)
{
	return NETDEV_TX_OK;
}

static void ngbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct wx *wx = pci_get_drvdata(pdev);
	struct net_device *netdev;

	netdev = wx->netdev;
	netif_device_detach(netdev);

	rtnl_lock();
	if (netif_running(netdev))
		ngbe_down(wx);
	rtnl_unlock();
	wx_control_hw(wx, false);

	pci_disable_device(pdev);
}

static void ngbe_shutdown(struct pci_dev *pdev)
{
	struct wx *wx = pci_get_drvdata(pdev);
	bool wake;

	wake = !!wx->wol;

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
	.ndo_set_mac_address    = wx_set_mac,
};

/**
 * ngbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ngbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ngbe_probe initializes an wx identified by a pci_dev structure.
 * The OS initialization, configuring of the wx private structure,
 * and a hardware reset occur.
 **/
static int ngbe_probe(struct pci_dev *pdev,
		      const struct pci_device_id __always_unused *ent)
{
	struct net_device *netdev;
	u32 e2rom_cksum_cap = 0;
	struct wx *wx = NULL;
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
					 sizeof(struct wx),
					 NGBE_MAX_TX_QUEUES,
					 NGBE_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	wx = netdev_priv(netdev);
	wx->netdev = netdev;
	wx->pdev = pdev;
	wx->msg_enable = BIT(3) - 1;

	wx->hw_addr = devm_ioremap(&pdev->dev,
				   pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
	if (!wx->hw_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	netdev->netdev_ops = &ngbe_netdev_ops;

	netdev->features |= NETIF_F_HIGHDMA;

	wx->bd_number = func_nums;
	/* setup the private structure */
	err = ngbe_sw_init(wx);
	if (err)
		goto err_free_mac_table;

	/* check if flash load is done after hw power up */
	err = wx_check_flash_load(wx, NGBE_SPI_ILDR_STATUS_PERST);
	if (err)
		goto err_free_mac_table;
	err = wx_check_flash_load(wx, NGBE_SPI_ILDR_STATUS_PWRRST);
	if (err)
		goto err_free_mac_table;

	err = wx_mng_present(wx);
	if (err) {
		dev_err(&pdev->dev, "Management capability is not present\n");
		goto err_free_mac_table;
	}

	err = ngbe_reset_hw(wx);
	if (err) {
		dev_err(&pdev->dev, "HW Init failed: %d\n", err);
		goto err_free_mac_table;
	}

	if (wx->bus.func == 0) {
		wr32(wx, NGBE_CALSUM_CAP_STATUS, 0x0);
		wr32(wx, NGBE_EEPROM_VERSION_STORE_REG, 0x0);
	} else {
		e2rom_cksum_cap = rd32(wx, NGBE_CALSUM_CAP_STATUS);
		saved_ver = rd32(wx, NGBE_EEPROM_VERSION_STORE_REG);
	}

	wx_init_eeprom_params(wx);
	if (wx->bus.func == 0 || e2rom_cksum_cap == 0) {
		/* make sure the EEPROM is ready */
		err = ngbe_eeprom_chksum_hostif(wx);
		if (err) {
			dev_err(&pdev->dev, "The EEPROM Checksum Is Not Valid\n");
			err = -EIO;
			goto err_free_mac_table;
		}
	}

	wx->wol = 0;
	if (wx->wol_enabled)
		wx->wol = NGBE_PSR_WKUP_CTL_MAG;

	wx->wol_enabled = !!(wx->wol);
	wr32(wx, NGBE_PSR_WKUP_CTL, wx->wol);

	device_set_wakeup_enable(&pdev->dev, wx->wol);

	/* Save off EEPROM version number and Option Rom version which
	 * together make a unique identify for the eeprom
	 */
	if (saved_ver) {
		etrack_id = saved_ver;
	} else {
		wx_read_ee_hostif(wx,
				  wx->eeprom.sw_region_offset + NGBE_EEPROM_VERSION_H,
				  &e2rom_ver);
		etrack_id = e2rom_ver << 16;
		wx_read_ee_hostif(wx,
				  wx->eeprom.sw_region_offset + NGBE_EEPROM_VERSION_L,
				  &e2rom_ver);
		etrack_id |= e2rom_ver;
		wr32(wx, NGBE_EEPROM_VERSION_STORE_REG, etrack_id);
	}

	eth_hw_addr_set(netdev, wx->mac.perm_addr);
	wx_mac_set_default_filter(wx, wx->mac.perm_addr);

	/* phy Interface Configuration */
	err = ngbe_mdio_init(wx);
	if (err)
		goto err_free_mac_table;

	err = register_netdev(netdev);
	if (err)
		goto err_register;

	pci_set_drvdata(pdev, wx);

	netif_info(wx, probe, netdev,
		   "PHY: %s, PBA No: Wang Xun GbE Family Controller\n",
		   wx->mac_type == em_mac_type_mdi ? "Internal" : "External");
	netif_info(wx, probe, netdev, "%pM\n", netdev->dev_addr);

	return 0;

err_register:
	wx_control_hw(wx, false);
err_free_mac_table:
	kfree(wx->mac_table);
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
	struct wx *wx = pci_get_drvdata(pdev);
	struct net_device *netdev;

	netdev = wx->netdev;
	unregister_netdev(netdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	kfree(wx->mac_table);
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
