/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/module.h>

#include "fm10k.h"

static const struct fm10k_info *fm10k_info_tbl[] = {
	[fm10k_device_pf] = &fm10k_pf_info,
};

/**
 * fm10k_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id fm10k_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, FM10K_DEV_ID_PF), fm10k_device_pf },
	/* required last entry */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, fm10k_pci_tbl);

u16 fm10k_read_pci_cfg_word(struct fm10k_hw *hw, u32 reg)
{
	struct fm10k_intfc *interface = hw->back;
	u16 value = 0;

	if (FM10K_REMOVED(hw->hw_addr))
		return ~value;

	pci_read_config_word(interface->pdev, reg, &value);
	if (value == 0xFFFF)
		fm10k_write_flush(hw);

	return value;
}

u32 fm10k_read_reg(struct fm10k_hw *hw, int reg)
{
	u32 __iomem *hw_addr = ACCESS_ONCE(hw->hw_addr);
	u32 value = 0;

	if (FM10K_REMOVED(hw_addr))
		return ~value;

	value = readl(&hw_addr[reg]);
	if (!(~value) && (!reg || !(~readl(hw_addr)))) {
		struct fm10k_intfc *interface = hw->back;
		struct net_device *netdev = interface->netdev;

		hw->hw_addr = NULL;
		netif_device_detach(netdev);
		netdev_err(netdev, "PCIe link lost, device now detached\n");
	}

	return value;
}

static int fm10k_hw_ready(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;

	fm10k_write_flush(hw);

	return FM10K_REMOVED(hw->hw_addr) ? -ENODEV : 0;
}

void fm10k_up(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;

	/* Enable Tx/Rx DMA */
	hw->mac.ops.start_hw(hw);

	/* configure interrupts */
	hw->mac.ops.update_int_moderator(hw);

	/* clear down bit to indicate we are ready to go */
	clear_bit(__FM10K_DOWN, &interface->state);

	/* re-establish Rx filters */
	fm10k_restore_rx_state(interface);

	/* enable transmits */
	netif_tx_start_all_queues(interface->netdev);
}

void fm10k_down(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;

	/* signal that we are down to the interrupt handler and service task */
	set_bit(__FM10K_DOWN, &interface->state);

	/* call carrier off first to avoid false dev_watchdog timeouts */
	netif_carrier_off(netdev);

	/* disable transmits */
	netif_tx_stop_all_queues(netdev);
	netif_tx_disable(netdev);

	/* reset Rx filters */
	fm10k_reset_rx_state(interface);

	/* allow 10ms for device to quiesce */
	usleep_range(10000, 20000);

	/* Disable DMA engine for Tx/Rx */
	hw->mac.ops.stop_hw(hw);
}

/**
 * fm10k_sw_init - Initialize general software structures
 * @interface: host interface private structure to initialize
 *
 * fm10k_sw_init initializes the interface private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int fm10k_sw_init(struct fm10k_intfc *interface,
			 const struct pci_device_id *ent)
{
	static const u32 seed[FM10K_RSSRK_SIZE] = { 0xda565a6d, 0xc20e5b25,
						    0x3d256741, 0xb08fa343,
						    0xcb2bcad0, 0xb4307bae,
						    0xa32dcb77, 0x0cf23080,
						    0x3bb7426a, 0xfa01acbe };
	const struct fm10k_info *fi = fm10k_info_tbl[ent->driver_data];
	struct fm10k_hw *hw = &interface->hw;
	struct pci_dev *pdev = interface->pdev;
	struct net_device *netdev = interface->netdev;
	unsigned int rss;
	int err;

	/* initialize back pointer */
	hw->back = interface;
	hw->hw_addr = interface->uc_addr;

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* Setup hw api */
	memcpy(&hw->mac.ops, fi->mac_ops, sizeof(hw->mac.ops));
	hw->mac.type = fi->mac;

	/* Set common capability flags and settings */
	rss = min_t(int, FM10K_MAX_RSS_INDICES, num_online_cpus());
	interface->ring_feature[RING_F_RSS].limit = rss;
	fi->get_invariants(hw);

	/* pick up the PCIe bus settings for reporting later */
	if (hw->mac.ops.get_bus_info)
		hw->mac.ops.get_bus_info(hw);

	/* limit the usable DMA range */
	if (hw->mac.ops.set_dma_mask)
		hw->mac.ops.set_dma_mask(hw, dma_get_mask(&pdev->dev));

	/* update netdev with DMA restrictions */
	if (dma_get_mask(&pdev->dev) > DMA_BIT_MASK(32)) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	/* reset and initialize the hardware so it is in a known state */
	err = hw->mac.ops.reset_hw(hw) ? : hw->mac.ops.init_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "init_hw failed: %d\n", err);
		return err;
	}

	/* initialize hardware statistics */
	hw->mac.ops.update_hw_stats(hw, &interface->stats);

	/* Start with random Ethernet address */
	eth_random_addr(hw->mac.addr);

	/* Initialize MAC address from hardware */
	err = hw->mac.ops.read_mac_addr(hw);
	if (err) {
		dev_warn(&pdev->dev,
			 "Failed to obtain MAC address defaulting to random\n");
		/* tag address assignment as random */
		netdev->addr_assign_type |= NET_ADDR_RANDOM;
	}

	memcpy(netdev->dev_addr, hw->mac.addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac.addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->perm_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		return -EIO;
	}

	/* Only the PF can support VXLAN and NVGRE offloads */
	if (hw->mac.type != fm10k_mac_pf) {
		netdev->hw_enc_features = 0;
		netdev->features &= ~NETIF_F_GSO_UDP_TUNNEL;
		netdev->hw_features &= ~NETIF_F_GSO_UDP_TUNNEL;
	}

	/* initialize vxlan_port list */
	INIT_LIST_HEAD(&interface->vxlan_port);

	/* initialize RSS key */
	memcpy(interface->rssrk, seed, sizeof(seed));

	/* Start off interface as being down */
	set_bit(__FM10K_DOWN, &interface->state);

	return 0;
}

static void fm10k_slot_warn(struct fm10k_intfc *interface)
{
	struct device *dev = &interface->pdev->dev;
	struct fm10k_hw *hw = &interface->hw;

	if (hw->mac.ops.is_slot_appropriate(hw))
		return;

	dev_warn(dev,
		 "For optimal performance, a %s %s slot is recommended.\n",
		 (hw->bus_caps.width == fm10k_bus_width_pcie_x1 ? "x1" :
		  hw->bus_caps.width == fm10k_bus_width_pcie_x4 ? "x4" :
		  "x8"),
		 (hw->bus_caps.speed == fm10k_bus_speed_2500 ? "2.5GT/s" :
		  hw->bus_caps.speed == fm10k_bus_speed_5000 ? "5.0GT/s" :
		  "8.0GT/s"));
	dev_warn(dev,
		 "A slot with more lanes and/or higher speed is suggested.\n");
}

/**
 * fm10k_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in fm10k_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * fm10k_probe initializes an interface identified by a pci_dev structure.
 * The OS initialization, configuring of the interface private structure,
 * and a hardware reset occur.
 **/
static int fm10k_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct fm10k_intfc *interface;
	struct fm10k_hw *hw;
	int err;
	u64 dma_mask;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* By default fm10k only supports a 48 bit DMA mask */
	dma_mask = DMA_BIT_MASK(48) | dma_get_required_mask(&pdev->dev);

	if ((dma_mask <= DMA_BIT_MASK(32)) ||
	    dma_set_mask_and_coherent(&pdev->dev, dma_mask)) {
		dma_mask &= DMA_BIT_MASK(32);

		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
			if (err) {
				dev_err(&pdev->dev,
					"No usable DMA configuration, aborting\n");
				goto err_dma;
			}
		}
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev,
							   IORESOURCE_MEM),
					   fm10k_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	netdev = fm10k_alloc_netdev();
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	interface = netdev_priv(netdev);
	pci_set_drvdata(pdev, interface);

	interface->netdev = netdev;
	interface->pdev = pdev;
	hw = &interface->hw;

	interface->uc_addr = ioremap(pci_resource_start(pdev, 0),
				     FM10K_UC_ADDR_SIZE);
	if (!interface->uc_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	err = fm10k_sw_init(interface, ent);
	if (err)
		goto err_sw_init;

	/* final check of hardware state before registering the interface */
	err = fm10k_hw_ready(interface);
	if (err)
		goto err_register;

	err = register_netdev(netdev);
	if (err)
		goto err_register;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	/* stop all the transmit queues from transmitting until link is up */
	netif_tx_stop_all_queues(netdev);

	/* print bus type/speed/width info */
	dev_info(&pdev->dev, "(PCI Express:%s Width: %s Payload: %s)\n",
		 (hw->bus.speed == fm10k_bus_speed_8000 ? "8.0GT/s" :
		  hw->bus.speed == fm10k_bus_speed_5000 ? "5.0GT/s" :
		  hw->bus.speed == fm10k_bus_speed_2500 ? "2.5GT/s" :
		  "Unknown"),
		 (hw->bus.width == fm10k_bus_width_pcie_x8 ? "x8" :
		  hw->bus.width == fm10k_bus_width_pcie_x4 ? "x4" :
		  hw->bus.width == fm10k_bus_width_pcie_x1 ? "x1" :
		  "Unknown"),
		 (hw->bus.payload == fm10k_bus_payload_128 ? "128B" :
		  hw->bus.payload == fm10k_bus_payload_256 ? "256B" :
		  hw->bus.payload == fm10k_bus_payload_512 ? "512B" :
		  "Unknown"));

	/* print warning for non-optimal configurations */
	fm10k_slot_warn(interface);

	return 0;

err_register:
err_sw_init:
	iounmap(interface->uc_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_netdev:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * fm10k_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * fm10k_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void fm10k_remove(struct pci_dev *pdev)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	struct net_device *netdev = interface->netdev;

	/* free netdev, this may bounce the interrupts due to setup_tc */
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	iounmap(interface->uc_addr);

	free_netdev(netdev);

	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_device(pdev);
}

static struct pci_driver fm10k_driver = {
	.name			= fm10k_driver_name,
	.id_table		= fm10k_pci_tbl,
	.probe			= fm10k_probe,
	.remove			= fm10k_remove,
};

/**
 * fm10k_register_pci_driver - register driver interface
 *
 * This funciton is called on module load in order to register the driver.
 **/
int fm10k_register_pci_driver(void)
{
	return pci_register_driver(&fm10k_driver);
}

/**
 * fm10k_unregister_pci_driver - unregister driver interface
 *
 * This funciton is called on module unload in order to remove the driver.
 **/
void fm10k_unregister_pci_driver(void)
{
	pci_unregister_driver(&fm10k_driver);
}
