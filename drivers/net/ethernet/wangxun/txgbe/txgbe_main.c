// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

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
#include "txgbe_type.h"
#include "txgbe_hw.h"
#include "txgbe.h"

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
	/* required last entry */
	{ .device = 0 }
};

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

static void txgbe_check_minimum_link(struct txgbe_adapter *adapter)
{
	struct pci_dev *pdev;

	pdev = adapter->pdev;
	pcie_print_link_status(pdev);
}

/**
 * txgbe_enumerate_functions - Get the number of ports this device has
 * @adapter: adapter structure
 *
 * This function enumerates the phsyical functions co-located on a single slot,
 * in order to determine how many ports a device has. This is most useful in
 * determining the required GT/s of PCIe bandwidth necessary for optimal
 * performance.
 **/
static int txgbe_enumerate_functions(struct txgbe_adapter *adapter)
{
	struct pci_dev *entry, *pdev = adapter->pdev;
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

static void txgbe_sync_mac_table(struct txgbe_adapter *adapter)
{
	struct txgbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;
	int i;

	for (i = 0; i < wxhw->mac.num_rar_entries; i++) {
		if (adapter->mac_table[i].state & TXGBE_MAC_STATE_MODIFIED) {
			if (adapter->mac_table[i].state & TXGBE_MAC_STATE_IN_USE) {
				wx_set_rar(wxhw, i,
					   adapter->mac_table[i].addr,
					   adapter->mac_table[i].pools,
					   WX_PSR_MAC_SWC_AD_H_AV);
			} else {
				wx_clear_rar(wxhw, i);
			}
			adapter->mac_table[i].state &= ~(TXGBE_MAC_STATE_MODIFIED);
		}
	}
}

/* this function destroys the first RAR entry */
static void txgbe_mac_set_default_filter(struct txgbe_adapter *adapter,
					 u8 *addr)
{
	struct wx_hw *wxhw = &adapter->hw.wxhw;

	memcpy(&adapter->mac_table[0].addr, addr, ETH_ALEN);
	adapter->mac_table[0].pools = 1ULL;
	adapter->mac_table[0].state = (TXGBE_MAC_STATE_DEFAULT |
				       TXGBE_MAC_STATE_IN_USE);
	wx_set_rar(wxhw, 0, adapter->mac_table[0].addr,
		   adapter->mac_table[0].pools,
		   WX_PSR_MAC_SWC_AD_H_AV);
}

static void txgbe_flush_sw_mac_table(struct txgbe_adapter *adapter)
{
	struct wx_hw *wxhw = &adapter->hw.wxhw;
	u32 i;

	for (i = 0; i < wxhw->mac.num_rar_entries; i++) {
		adapter->mac_table[i].state |= TXGBE_MAC_STATE_MODIFIED;
		adapter->mac_table[i].state &= ~TXGBE_MAC_STATE_IN_USE;
		memset(adapter->mac_table[i].addr, 0, ETH_ALEN);
		adapter->mac_table[i].pools = 0;
	}
	txgbe_sync_mac_table(adapter);
}

static int txgbe_del_mac_filter(struct txgbe_adapter *adapter, u8 *addr, u16 pool)
{
	struct wx_hw *wxhw = &adapter->hw.wxhw;
	u32 i;

	if (is_zero_ether_addr(addr))
		return -EINVAL;

	/* search table for addr, if found, set to 0 and sync */
	for (i = 0; i < wxhw->mac.num_rar_entries; i++) {
		if (ether_addr_equal(addr, adapter->mac_table[i].addr)) {
			if (adapter->mac_table[i].pools & (1ULL << pool)) {
				adapter->mac_table[i].state |= TXGBE_MAC_STATE_MODIFIED;
				adapter->mac_table[i].state &= ~TXGBE_MAC_STATE_IN_USE;
				adapter->mac_table[i].pools &= ~(1ULL << pool);
				txgbe_sync_mac_table(adapter);
			}
			return 0;
		}

		if (adapter->mac_table[i].pools != (1 << pool))
			continue;
		if (!ether_addr_equal(addr, adapter->mac_table[i].addr))
			continue;

		adapter->mac_table[i].state |= TXGBE_MAC_STATE_MODIFIED;
		adapter->mac_table[i].state &= ~TXGBE_MAC_STATE_IN_USE;
		memset(adapter->mac_table[i].addr, 0, ETH_ALEN);
		adapter->mac_table[i].pools = 0;
		txgbe_sync_mac_table(adapter);
		return 0;
	}
	return -ENOMEM;
}

static void txgbe_up_complete(struct txgbe_adapter *adapter)
{
	struct txgbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;

	wx_control_hw(wxhw, true);
}

static void txgbe_reset(struct txgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct txgbe_hw *hw = &adapter->hw;
	u8 old_addr[ETH_ALEN];
	int err;

	err = txgbe_reset_hw(hw);
	if (err != 0)
		dev_err(&adapter->pdev->dev, "Hardware Error: %d\n", err);

	/* do not flush user set addresses */
	memcpy(old_addr, &adapter->mac_table[0].addr, netdev->addr_len);
	txgbe_flush_sw_mac_table(adapter);
	txgbe_mac_set_default_filter(adapter, old_addr);
}

static void txgbe_disable_device(struct txgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct wx_hw *wxhw = &adapter->hw.wxhw;

	wx_disable_pcie_master(wxhw);
	/* disable receives */
	wx_disable_rx(wxhw);

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	if (wxhw->bus.func < 2)
		wr32m(wxhw, TXGBE_MIS_PRB_CTL, TXGBE_MIS_PRB_CTL_LAN_UP(wxhw->bus.func), 0);
	else
		dev_err(&adapter->pdev->dev,
			"%s: invalid bus lan id %d\n",
			__func__, wxhw->bus.func);

	if (!(((wxhw->subsystem_device_id & WX_NCSI_MASK) == WX_NCSI_SUP) ||
	      ((wxhw->subsystem_device_id & WX_WOL_MASK) == WX_WOL_SUP))) {
		/* disable mac transmiter */
		wr32m(wxhw, WX_MAC_TX_CFG, WX_MAC_TX_CFG_TE, 0);
	}

	/* Disable the Tx DMA engine */
	wr32m(wxhw, WX_TDM_CTL, WX_TDM_CTL_TE, 0);
}

static void txgbe_down(struct txgbe_adapter *adapter)
{
	txgbe_disable_device(adapter);
	txgbe_reset(adapter);
}

/**
 * txgbe_sw_init - Initialize general software structures (struct txgbe_adapter)
 * @adapter: board private structure to initialize
 **/
static int txgbe_sw_init(struct txgbe_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct txgbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;
	int err;

	wxhw->hw_addr = adapter->io_addr;
	wxhw->pdev = pdev;

	/* PCI config space info */
	err = wx_sw_init(wxhw);
	if (err < 0) {
		netif_err(adapter, probe, adapter->netdev,
			  "read of internal subsystem device id failed\n");
		return err;
	}

	switch (wxhw->device_id) {
	case TXGBE_DEV_ID_SP1000:
	case TXGBE_DEV_ID_WX1820:
		wxhw->mac.type = wx_mac_sp;
		break;
	default:
		wxhw->mac.type = wx_mac_unknown;
		break;
	}

	wxhw->mac.num_rar_entries = TXGBE_SP_RAR_ENTRIES;
	wxhw->mac.max_tx_queues = TXGBE_SP_MAX_TX_QUEUES;
	wxhw->mac.max_rx_queues = TXGBE_SP_MAX_RX_QUEUES;
	wxhw->mac.mcft_size = TXGBE_SP_MC_TBL_SIZE;

	adapter->mac_table = kcalloc(wxhw->mac.num_rar_entries,
				     sizeof(struct txgbe_mac_addr),
				     GFP_KERNEL);
	if (!adapter->mac_table) {
		netif_err(adapter, probe, adapter->netdev,
			  "mac_table allocation failed\n");
		return -ENOMEM;
	}

	return 0;
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
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	txgbe_up_complete(adapter);

	return 0;
}

/**
 * txgbe_close_suspend - actions necessary to both suspend and close flows
 * @adapter: the private adapter struct
 *
 * This function should contain the necessary work common to both suspending
 * and closing of the device.
 */
static void txgbe_close_suspend(struct txgbe_adapter *adapter)
{
	txgbe_disable_device(adapter);
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
	struct txgbe_adapter *adapter = netdev_priv(netdev);

	txgbe_down(adapter);
	wx_control_hw(&adapter->hw.wxhw, false);

	return 0;
}

static void txgbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct txgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	struct txgbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;

	netif_device_detach(netdev);

	rtnl_lock();
	if (netif_running(netdev))
		txgbe_close_suspend(adapter);
	rtnl_unlock();

	wx_control_hw(wxhw, false);

	pci_disable_device(pdev);
}

static void txgbe_shutdown(struct pci_dev *pdev)
{
	bool wake;

	txgbe_dev_shutdown(pdev, &wake);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static netdev_tx_t txgbe_xmit_frame(struct sk_buff *skb,
				    struct net_device *netdev)
{
	return NETDEV_TX_OK;
}

/**
 * txgbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int txgbe_set_mac(struct net_device *netdev, void *p)
{
	struct txgbe_adapter *adapter = netdev_priv(netdev);
	struct wx_hw *wxhw = &adapter->hw.wxhw;
	struct sockaddr *addr = p;
	int retval;

	retval = eth_prepare_mac_addr_change(netdev, addr);
	if (retval)
		return retval;

	txgbe_del_mac_filter(adapter, wxhw->mac.addr, 0);
	eth_hw_addr_set(netdev, addr->sa_data);
	memcpy(wxhw->mac.addr, addr->sa_data, netdev->addr_len);

	txgbe_mac_set_default_filter(adapter, wxhw->mac.addr);

	return 0;
}

static const struct net_device_ops txgbe_netdev_ops = {
	.ndo_open               = txgbe_open,
	.ndo_stop               = txgbe_close,
	.ndo_start_xmit         = txgbe_xmit_frame,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = txgbe_set_mac,
};

/**
 * txgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in txgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * txgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int txgbe_probe(struct pci_dev *pdev,
		       const struct pci_device_id __always_unused *ent)
{
	struct txgbe_adapter *adapter = NULL;
	struct txgbe_hw *hw = NULL;
	struct wx_hw *wxhw = NULL;
	struct net_device *netdev;
	int err, expected_gts;

	u16 eeprom_verh = 0, eeprom_verl = 0, offset = 0;
	u16 eeprom_cfg_blkh = 0, eeprom_cfg_blkl = 0;
	u16 build = 0, major = 0, patch = 0;
	u8 part_str[TXGBE_PBANUM_LENGTH];
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

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct txgbe_adapter),
					 TXGBE_MAX_TX_QUEUES,
					 TXGBE_MAX_RX_QUEUES);
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
	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	adapter->io_addr = devm_ioremap(&pdev->dev,
					pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!adapter->io_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	netdev->netdev_ops = &txgbe_netdev_ops;

	/* setup the private structure */
	err = txgbe_sw_init(adapter);
	if (err)
		goto err_free_mac_table;

	/* check if flash load is done after hw power up */
	err = wx_check_flash_load(wxhw, TXGBE_SPI_ILDR_STATUS_PERST);
	if (err)
		goto err_free_mac_table;
	err = wx_check_flash_load(wxhw, TXGBE_SPI_ILDR_STATUS_PWRRST);
	if (err)
		goto err_free_mac_table;

	err = wx_mng_present(wxhw);
	if (err) {
		dev_err(&pdev->dev, "Management capability is not present\n");
		goto err_free_mac_table;
	}

	err = txgbe_reset_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "HW Init failed: %d\n", err);
		goto err_free_mac_table;
	}

	netdev->features |= NETIF_F_HIGHDMA;

	/* make sure the EEPROM is good */
	err = txgbe_validate_eeprom_checksum(hw, NULL);
	if (err != 0) {
		dev_err(&pdev->dev, "The EEPROM Checksum Is Not Valid\n");
		wr32(wxhw, WX_MIS_RST, WX_MIS_RST_SW_RST);
		err = -EIO;
		goto err_free_mac_table;
	}

	eth_hw_addr_set(netdev, wxhw->mac.perm_addr);
	txgbe_mac_set_default_filter(adapter, wxhw->mac.perm_addr);

	/* Save off EEPROM version number and Option Rom version which
	 * together make a unique identify for the eeprom
	 */
	wx_read_ee_hostif(wxhw,
			  wxhw->eeprom.sw_region_offset + TXGBE_EEPROM_VERSION_H,
			  &eeprom_verh);
	wx_read_ee_hostif(wxhw,
			  wxhw->eeprom.sw_region_offset + TXGBE_EEPROM_VERSION_L,
			  &eeprom_verl);
	etrack_id = (eeprom_verh << 16) | eeprom_verl;

	wx_read_ee_hostif(wxhw,
			  wxhw->eeprom.sw_region_offset + TXGBE_ISCSI_BOOT_CONFIG,
			  &offset);

	/* Make sure offset to SCSI block is valid */
	if (!(offset == 0x0) && !(offset == 0xffff)) {
		wx_read_ee_hostif(wxhw, offset + 0x84, &eeprom_cfg_blkh);
		wx_read_ee_hostif(wxhw, offset + 0x83, &eeprom_cfg_blkl);

		/* Only display Option Rom if exist */
		if (eeprom_cfg_blkl && eeprom_cfg_blkh) {
			major = eeprom_cfg_blkl >> 8;
			build = (eeprom_cfg_blkl << 8) | (eeprom_cfg_blkh >> 8);
			patch = eeprom_cfg_blkh & 0x00ff;

			snprintf(adapter->eeprom_id, sizeof(adapter->eeprom_id),
				 "0x%08x, %d.%d.%d", etrack_id, major, build,
				 patch);
		} else {
			snprintf(adapter->eeprom_id, sizeof(adapter->eeprom_id),
				 "0x%08x", etrack_id);
		}
	} else {
		snprintf(adapter->eeprom_id, sizeof(adapter->eeprom_id),
			 "0x%08x", etrack_id);
	}

	err = register_netdev(netdev);
	if (err)
		goto err_release_hw;

	pci_set_drvdata(pdev, adapter);

	/* calculate the expected PCIe bandwidth required for optimal
	 * performance. Note that some older parts will never have enough
	 * bandwidth due to being older generation PCIe parts. We clamp these
	 * parts to ensure that no warning is displayed, as this could confuse
	 * users otherwise.
	 */
	expected_gts = txgbe_enumerate_functions(adapter) * 10;

	/* don't check link if we failed to enumerate functions */
	if (expected_gts > 0)
		txgbe_check_minimum_link(adapter);
	else
		dev_warn(&pdev->dev, "Failed to enumerate PF devices.\n");

	/* First try to read PBA as a string */
	err = txgbe_read_pba_string(hw, part_str, TXGBE_PBANUM_LENGTH);
	if (err)
		strncpy(part_str, "Unknown", TXGBE_PBANUM_LENGTH);

	netif_info(adapter, probe, netdev, "%pM\n", netdev->dev_addr);

	return 0;

err_release_hw:
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
	struct txgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev;

	netdev = adapter->netdev;
	unregister_netdev(netdev);

	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	kfree(adapter->mac_table);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static struct pci_driver txgbe_driver = {
	.name     = txgbe_driver_name,
	.id_table = txgbe_pci_tbl,
	.probe    = txgbe_probe,
	.remove   = txgbe_remove,
	.shutdown = txgbe_shutdown,
};

module_pci_driver(txgbe_driver);

MODULE_DEVICE_TABLE(pci, txgbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@trustnetic.com>");
MODULE_DESCRIPTION("WangXun(R) 10 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
