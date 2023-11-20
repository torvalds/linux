// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <net/ip.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "../libwx/wx_lib.h"
#include "ngbe_type.h"
#include "ngbe_mdio.h"
#include "ngbe_hw.h"
#include "ngbe_ethtool.h"

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

	wx->wol_hw_supported = (wol_mask == NGBE_WOL_SUP) ? 1 : 0;
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
	wx->mac.mcft_size = NGBE_MC_TBL_SIZE;
	wx->mac.vft_size = NGBE_SP_VFT_TBL_SIZE;
	wx->mac.rx_pb_size = NGBE_RX_PB_SIZE;
	wx->mac.tx_pb_size = NGBE_TDB_PB_SZ;

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

/**
 * ngbe_irq_enable - Enable default interrupt generation settings
 * @wx: board private structure
 * @queues: enable all queues interrupts
 **/
static void ngbe_irq_enable(struct wx *wx, bool queues)
{
	u32 mask;

	/* enable misc interrupt */
	mask = NGBE_PX_MISC_IEN_MASK;

	wr32(wx, WX_GPIO_DDR, WX_GPIO_DDR_0);
	wr32(wx, WX_GPIO_INTEN, WX_GPIO_INTEN_0 | WX_GPIO_INTEN_1);
	wr32(wx, WX_GPIO_INTTYPE_LEVEL, 0x0);
	wr32(wx, WX_GPIO_POLARITY, wx->gpio_ctrl ? 0 : 0x3);

	wr32(wx, WX_PX_MISC_IEN, mask);

	/* mask interrupt */
	if (queues)
		wx_intr_enable(wx, NGBE_INTR_ALL);
	else
		wx_intr_enable(wx, NGBE_INTR_MISC(wx));
}

/**
 * ngbe_intr - msi/legacy mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t ngbe_intr(int __always_unused irq, void *data)
{
	struct wx_q_vector *q_vector;
	struct wx *wx  = data;
	struct pci_dev *pdev;
	u32 eicr;

	q_vector = wx->q_vector[0];
	pdev = wx->pdev;

	eicr = wx_misc_isb(wx, WX_ISB_VEC0);
	if (!eicr) {
		/* shared interrupt alert!
		 * the interrupt that we masked before the EICR read.
		 */
		if (netif_running(wx->netdev))
			ngbe_irq_enable(wx, true);
		return IRQ_NONE;        /* Not our interrupt */
	}
	wx->isb_mem[WX_ISB_VEC0] = 0;
	if (!(pdev->msi_enabled))
		wr32(wx, WX_PX_INTA, 1);

	wx->isb_mem[WX_ISB_MISC] = 0;
	/* would disable interrupts here but it is auto disabled */
	napi_schedule_irqoff(&q_vector->napi);

	if (netif_running(wx->netdev))
		ngbe_irq_enable(wx, false);

	return IRQ_HANDLED;
}

static irqreturn_t ngbe_msix_other(int __always_unused irq, void *data)
{
	struct wx *wx = data;

	/* re-enable the original interrupt state, no lsc, no queues */
	if (netif_running(wx->netdev))
		ngbe_irq_enable(wx, false);

	return IRQ_HANDLED;
}

/**
 * ngbe_request_msix_irqs - Initialize MSI-X interrupts
 * @wx: board private structure
 *
 * ngbe_request_msix_irqs allocates MSI-X vectors and requests
 * interrupts from the kernel.
 **/
static int ngbe_request_msix_irqs(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	int vector, err;

	for (vector = 0; vector < wx->num_q_vectors; vector++) {
		struct wx_q_vector *q_vector = wx->q_vector[vector];
		struct msix_entry *entry = &wx->msix_entries[vector];

		if (q_vector->tx.ring && q_vector->rx.ring)
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-TxRx-%d", netdev->name, entry->entry);
		else
			/* skip this unused q_vector */
			continue;

		err = request_irq(entry->vector, wx_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			wx_err(wx, "request_irq failed for MSIX interrupt %s Error: %d\n",
			       q_vector->name, err);
			goto free_queue_irqs;
		}
	}

	err = request_irq(wx->msix_entries[vector].vector,
			  ngbe_msix_other, 0, netdev->name, wx);

	if (err) {
		wx_err(wx, "request_irq for msix_other failed: %d\n", err);
		goto free_queue_irqs;
	}

	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		free_irq(wx->msix_entries[vector].vector,
			 wx->q_vector[vector]);
	}
	wx_reset_interrupt_capability(wx);
	return err;
}

/**
 * ngbe_request_irq - initialize interrupts
 * @wx: board private structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int ngbe_request_irq(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	struct pci_dev *pdev = wx->pdev;
	int err;

	if (pdev->msix_enabled)
		err = ngbe_request_msix_irqs(wx);
	else if (pdev->msi_enabled)
		err = request_irq(pdev->irq, ngbe_intr, 0,
				  netdev->name, wx);
	else
		err = request_irq(pdev->irq, ngbe_intr, IRQF_SHARED,
				  netdev->name, wx);

	if (err)
		wx_err(wx, "request_irq failed, Error %d\n", err);

	return err;
}

static void ngbe_disable_device(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	u32 i;

	/* disable all enabled rx queues */
	for (i = 0; i < wx->num_rx_queues; i++)
		/* this call also flushes the previous write */
		wx_disable_rx_queue(wx, wx->rx_ring[i]);
	/* disable receives */
	wx_disable_rx(wx);
	wx_napi_disable_all(wx);
	netif_tx_stop_all_queues(netdev);
	netif_tx_disable(netdev);
	if (wx->gpio_ctrl)
		ngbe_sfp_modules_txrx_powerctl(wx, false);
	wx_irq_disable(wx);
	/* disable transmits in the hardware now that interrupts are off */
	for (i = 0; i < wx->num_tx_queues; i++) {
		u8 reg_idx = wx->tx_ring[i]->reg_idx;

		wr32(wx, WX_PX_TR_CFG(reg_idx), WX_PX_TR_CFG_SWFLSH);
	}

	wx_update_stats(wx);
}

static void ngbe_down(struct wx *wx)
{
	phy_stop(wx->phydev);
	ngbe_disable_device(wx);
	wx_clean_all_tx_rings(wx);
	wx_clean_all_rx_rings(wx);
}

static void ngbe_up(struct wx *wx)
{
	wx_configure_vectors(wx);

	/* make sure to complete pre-operations */
	smp_mb__before_atomic();
	wx_napi_enable_all(wx);
	/* enable transmits */
	netif_tx_start_all_queues(wx->netdev);

	/* clear any pending interrupts, may auto mask */
	rd32(wx, WX_PX_IC(0));
	rd32(wx, WX_PX_MISC_IC);
	ngbe_irq_enable(wx, true);
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

	err = wx_setup_resources(wx);
	if (err)
		return err;

	wx_configure(wx);

	err = ngbe_request_irq(wx);
	if (err)
		goto err_free_resources;

	err = ngbe_phy_connect(wx);
	if (err)
		goto err_free_irq;

	err = netif_set_real_num_tx_queues(netdev, wx->num_tx_queues);
	if (err)
		goto err_dis_phy;

	err = netif_set_real_num_rx_queues(netdev, wx->num_rx_queues);
	if (err)
		goto err_dis_phy;

	ngbe_up(wx);

	return 0;
err_dis_phy:
	phy_disconnect(wx->phydev);
err_free_irq:
	wx_free_irq(wx);
err_free_resources:
	wx_free_resources(wx);
	return err;
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
	wx_free_irq(wx);
	wx_free_resources(wx);
	phy_disconnect(wx->phydev);
	wx_control_hw(wx, false);

	return 0;
}

static void ngbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct wx *wx = pci_get_drvdata(pdev);
	struct net_device *netdev;
	u32 wufc = wx->wol;

	netdev = wx->netdev;
	rtnl_lock();
	netif_device_detach(netdev);

	if (netif_running(netdev))
		ngbe_close(netdev);
	wx_clear_interrupt_scheme(wx);
	rtnl_unlock();

	if (wufc) {
		wx_set_rx_mode(netdev);
		wx_configure_rx(wx);
		wr32(wx, NGBE_PSR_WKUP_CTL, wufc);
	} else {
		wr32(wx, NGBE_PSR_WKUP_CTL, 0);
	}
	pci_wake_from_d3(pdev, !!wufc);
	*enable_wake = !!wufc;
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
	.ndo_change_mtu         = wx_change_mtu,
	.ndo_start_xmit         = wx_xmit_frame,
	.ndo_set_rx_mode        = wx_set_rx_mode,
	.ndo_set_features       = wx_set_features,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = wx_set_mac,
	.ndo_get_stats64        = wx_get_stats64,
	.ndo_vlan_rx_add_vid    = wx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = wx_vlan_rx_kill_vid,
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

	wx->driver_name = ngbe_driver_name;
	ngbe_set_ethtool_ops(netdev);
	netdev->netdev_ops = &ngbe_netdev_ops;

	netdev->features = NETIF_F_SG | NETIF_F_IP_CSUM |
			   NETIF_F_TSO | NETIF_F_TSO6 |
			   NETIF_F_RXHASH | NETIF_F_RXCSUM;
	netdev->features |= NETIF_F_SCTP_CRC | NETIF_F_TSO_MANGLEID;
	netdev->vlan_features |= netdev->features;
	netdev->features |= NETIF_F_IPV6_CSUM | NETIF_F_VLAN_FEATURES;
	/* copy netdev features into list of user selectable features */
	netdev->hw_features |= netdev->features | NETIF_F_RXALL;
	netdev->hw_features |= NETIF_F_NTUPLE | NETIF_F_HW_TC;
	netdev->features |= NETIF_F_HIGHDMA;
	netdev->hw_features |= NETIF_F_GRO;
	netdev->features |= NETIF_F_GRO;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = WX_MAX_JUMBO_FRAME_SIZE -
			  (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

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
	if (wx->wol_hw_supported)
		wx->wol = NGBE_PSR_WKUP_CTL_MAG;

	netdev->wol_enabled = !!(wx->wol);
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
	snprintf(wx->eeprom_id, sizeof(wx->eeprom_id),
		 "0x%08x", etrack_id);

	eth_hw_addr_set(netdev, wx->mac.perm_addr);
	wx_mac_set_default_filter(wx, wx->mac.perm_addr);

	err = wx_init_interrupt_scheme(wx);
	if (err)
		goto err_free_mac_table;

	/* phy Interface Configuration */
	err = ngbe_mdio_init(wx);
	if (err)
		goto err_clear_interrupt_scheme;

	err = register_netdev(netdev);
	if (err)
		goto err_register;

	pci_set_drvdata(pdev, wx);

	return 0;

err_register:
	wx_control_hw(wx, false);
err_clear_interrupt_scheme:
	wx_clear_interrupt_scheme(wx);
err_free_mac_table:
	kfree(wx->mac_table);
err_pci_release_regions:
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
	wx_clear_interrupt_scheme(wx);

	pci_disable_device(pdev);
}

static int ngbe_suspend(struct pci_dev *pdev, pm_message_t state)
{
	bool wake;

	ngbe_dev_shutdown(pdev, &wake);
	device_set_wakeup_enable(&pdev->dev, wake);

	return 0;
}

static int ngbe_resume(struct pci_dev *pdev)
{
	struct net_device *netdev;
	struct wx *wx;
	u32 err;

	wx = pci_get_drvdata(pdev);
	netdev = wx->netdev;

	err = pci_enable_device_mem(pdev);
	if (err) {
		wx_err(wx, "Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);
	device_wakeup_disable(&pdev->dev);

	ngbe_reset_hw(wx);
	rtnl_lock();
	err = wx_init_interrupt_scheme(wx);
	if (!err && netif_running(netdev))
		err = ngbe_open(netdev);
	if (!err)
		netif_device_attach(netdev);
	rtnl_unlock();

	return 0;
}

static struct pci_driver ngbe_driver = {
	.name     = ngbe_driver_name,
	.id_table = ngbe_pci_tbl,
	.probe    = ngbe_probe,
	.remove   = ngbe_remove,
	.suspend  = ngbe_suspend,
	.resume   = ngbe_resume,
	.shutdown = ngbe_shutdown,
};

module_pci_driver(ngbe_driver);

MODULE_DEVICE_TABLE(pci, ngbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@net-swift.com>");
MODULE_DESCRIPTION("WangXun(R) Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
