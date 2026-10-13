// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/skbuff.h>
#include <net/rtnetlink.h>
#include <linux/etherdevice.h>

#include "rnpgbe.h"
#include "rnpgbe_hw.h"
#include "rnpgbe_lib.h"
#include "rnpgbe_mbx_fw.h"

static const char rnpgbe_driver_name[] = "rnpgbe";

/* rnpgbe_pci_tbl - PCI Device ID Table
 *
 * { PCI_VDEVICE(Vendor ID, Device ID),
 *   private_data (used for different hw chip) }
 */
static struct pci_device_id rnpgbe_pci_tbl[] = {
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N210), .driver_data = board_n210 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N210L), .driver_data = board_n210 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N500_DUAL_PORT), .driver_data = board_n500 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N500_QUAD_PORT), .driver_data = board_n500 },
	/* required last entry */
	{ },
};

/**
 * rnpgbe_configure - Configure the hardware
 * @mucse: pointer to private structure
 *
 * Configure Tx and Rx registers in hardware.
 *
 * Return: 0 on success, negative errno if hardware configuration fails
 **/
static int rnpgbe_configure(struct mucse *mucse)
{
	int err;

	err = rnpgbe_configure_tx(mucse);
	if (err)
		return err;

	netif_addr_lock_bh(mucse->netdev);
	err = rnpgbe_set_rx_mode(mucse->netdev, &mucse->netdev->uc,
				 &mucse->netdev->mc);
	netif_addr_unlock_bh(mucse->netdev);
	if (err)
		return err;

	return rnpgbe_configure_rx(mucse);
}

/**
 * rnpgbe_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).
 *
 * Return: 0 on success, negative value on failure
 **/
static int rnpgbe_open(struct net_device *netdev)
{
	struct mucse *mucse = netdev_priv(netdev);
	int err;

	if (test_bit(__MUCSE_AXI_FAULT, &mucse->state))
		return -EIO;

	err = rnpgbe_request_irq(mucse);
	if (err)
		return err;

	err = netif_set_real_num_queues(netdev, mucse->num_tx_queues,
					mucse->num_rx_queues);
	if (err)
		goto err_free_irqs;

	err = rnpgbe_setup_all_tx_resources(mucse);
	if (err)
		goto err_free_irqs;
	err = rnpgbe_setup_all_rx_resources(mucse);
	if (err)
		goto err_free_tx;

	err = rnpgbe_configure(mucse);
	if (err)
		goto err_free_rx;
	rnpgbe_up_complete(mucse);

	return 0;
err_free_rx:
	rnpgbe_free_all_rx_resources(mucse);
err_free_tx:
	rnpgbe_clean_all_tx_rings(mucse);
	rnpgbe_free_all_tx_resources(mucse);
err_free_irqs:
	rnpgbe_free_irq(mucse);
	return err;
}

/**
 * rnpgbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.
 *
 * Return: 0, this is not allowed to fail
 **/
static int rnpgbe_close(struct net_device *netdev)
{
	struct mucse *mucse = netdev_priv(netdev);

	rnpgbe_down(mucse);
	rnpgbe_free_irq(mucse);
	rnpgbe_free_all_tx_resources(mucse);
	rnpgbe_free_all_rx_resources(mucse);

	return 0;
}

/**
 * rnpgbe_xmit_frame - Send a skb to driver
 * @skb: skb structure to be sent
 * @netdev: network interface device structure
 *
 * Return: NETDEV_TX_OK or NETDEV_TX_BUSY when insufficient descriptors
 **/
static netdev_tx_t rnpgbe_xmit_frame(struct sk_buff *skb,
				     struct net_device *netdev)
{
	struct mucse *mucse = netdev_priv(netdev);
	struct mucse_ring *tx_ring;

	tx_ring = mucse->tx_ring[skb_get_queue_mapping(skb)];

	if (unlikely(skb_put_padto(skb, RNPGBE_TX_MIN_PKT_LEN))) {
		atomic64_inc(&tx_ring->stats->dropped);
		return NETDEV_TX_OK;
	}

	return rnpgbe_xmit_frame_ring(skb, tx_ring);
}

static const struct net_device_ops rnpgbe_netdev_ops = {
	.ndo_open		 = rnpgbe_open,
	.ndo_stop		 = rnpgbe_close,
	.ndo_start_xmit		 = rnpgbe_xmit_frame,
	.ndo_set_rx_mode_async = rnpgbe_set_rx_mode,
	.ndo_get_stats64	 = rnpgbe_get_stats64,
};

static void rnpgbe_sw_init(struct mucse *mucse)
{
	int i;

	mucse->tx_ring_item_count = M_DEFAULT_TXD;
	mucse->rx_ring_item_count = M_DEFAULT_RXD;
	mucse->tx_work_limit = M_DEFAULT_TX_WORK;

	for (i = 0; i < RNPGBE_MAX_QUEUES; i++) {
		u64_stats_init(&mucse->tx_stats[i].syncp);
		atomic64_set(&mucse->tx_stats[i].dropped, 0);
		u64_stats_init(&mucse->rx_stats[i].syncp);
		atomic64_set(&mucse->rx_stats[i].dropped, 0);
	}
}

/**
 * rnpgbe_add_adapter - Add netdev for this pci_dev
 * @pdev: PCI device information structure
 * @board_type: board type
 *
 * rnpgbe_add_adapter initializes a netdev for this pci_dev
 * structure. Initializes Bar map, private structure, and a
 * hardware reset occur.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int rnpgbe_add_adapter(struct pci_dev *pdev,
			      int board_type)
{
	struct net_device *netdev;
	u8 perm_addr[ETH_ALEN];
	void __iomem *hw_addr;
	struct mucse *mucse;
	struct mucse_hw *hw;
	int err, err_notify;

	netdev = alloc_etherdev_mq(sizeof(struct mucse), RNPGBE_MAX_QUEUES);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	mucse = netdev_priv(netdev);
	mucse->netdev = netdev;
	mucse->pdev = pdev;
	pci_set_drvdata(pdev, mucse);

	hw = &mucse->hw;
	hw_addr = devm_ioremap(&pdev->dev,
			       pci_resource_start(pdev, 2),
			       pci_resource_len(pdev, 2));
	if (!hw_addr) {
		err = -EIO;
		goto err_free_net;
	}

	hw->hw_addr = hw_addr;
	hw->pdev = pdev;

	err = rnpgbe_init_hw(hw, board_type);
	if (err) {
		dev_err(&pdev->dev, "Init hw err %d\n", err);
		goto err_free_net;
	}
	/* Step 1: Send power-up notification to firmware (no response expected)
	 * This informs firmware to initialize hardware power state, but
	 * firmware only acknowledges receipt without returning data. Must be
	 * done before synchronization as firmware may be in low-power idle
	 * state initially.
	 */
	err_notify = rnpgbe_send_notify(hw, true, mucse_fw_powerup);
	if (err_notify) {
		dev_warn(&pdev->dev, "Send powerup to hw failed %d\n",
			 err_notify);
		dev_warn(&pdev->dev, "Maybe low performance\n");
	}
	/* Step 2: Synchronize mailbox communication with firmware (requires
	 * response) After power-up, confirm firmware is ready to process
	 * requests with responses. This ensures subsequent request/response
	 * interactions work reliably.
	 */
	err = mucse_mbx_sync_fw(hw);
	if (err) {
		dev_err(&pdev->dev, "Sync fw failed! %d\n", err);
		goto err_powerdown;
	}

	netdev->netdev_ops = &rnpgbe_netdev_ops;
	netdev->priv_flags |= IFF_UNICAST_FLT;
	rnpgbe_sw_init(mucse);
	err = rnpgbe_reset_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "Hw reset failed %d\n", err);
		goto err_powerdown;
	}

	err = rnpgbe_get_permanent_mac(hw, perm_addr);
	if (!err) {
		eth_hw_addr_set(netdev, perm_addr);
	} else if (err == -EINVAL) {
		dev_warn(&pdev->dev, "Using random MAC\n");
		eth_hw_addr_random(netdev);
	} else if (err) {
		dev_err(&pdev->dev, "get perm_addr failed %d\n", err);
		goto err_powerdown;
	}

	err = rnpgbe_init_interrupt_scheme(mucse);
	if (err) {
		dev_err(&pdev->dev, "init interrupt failed %d\n", err);
		goto err_powerdown;
	}

	err = netif_set_real_num_queues(netdev, mucse->num_tx_queues,
					mucse->num_rx_queues);
	if (err)
		goto err_clear_interrupt;

	err = rnpgbe_request_mbx_irq(mucse);
	if (err) {
		dev_err(&pdev->dev, "register mbx irq failed %d\n", err);
		goto err_clear_interrupt;
	}

	netdev->features |= NETIF_F_SG;
	netdev->hw_features |= NETIF_F_SG;
	if (dma_get_mask(&pdev->dev) > DMA_BIT_MASK(32)) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->hw_features |= NETIF_F_HIGHDMA;
	}

	netif_carrier_off(netdev);
	err = register_netdev(netdev);
	if (err)
		goto err_remove_mbx;

	return 0;

err_remove_mbx:
	rnpgbe_free_mbx_irq(mucse);
err_clear_interrupt:
	rnpgbe_clear_interrupt_scheme(mucse);
err_powerdown:
	/* notify powerdown only powerup ok */
	if (!err_notify) {
		err_notify = rnpgbe_send_notify(hw, false, mucse_fw_powerup);
		if (err_notify)
			dev_warn(&pdev->dev, "Send powerdown to hw failed %d\n",
				 err_notify);
	}
err_free_net:
	free_netdev(netdev);
	return err;
}

/**
 * rnpgbe_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @id: entry in rnpgbe_pci_tbl
 *
 * rnpgbe_probe initializes a PF adapter identified by a pci_dev
 * structure.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int rnpgbe_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int board_type = id->driver_data;
	int err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(56));

	err = pci_request_mem_regions(pdev, rnpgbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed %d\n", err);
		goto err_disable_dev;
	}

	pci_set_master(pdev);
	err = pci_save_state(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_save_state failed %d\n", err);
		goto err_free_regions;
	}

	err = rnpgbe_add_adapter(pdev, board_type);
	if (err)
		goto err_free_regions;

	return 0;
err_free_regions:
	pci_release_mem_regions(pdev);
err_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * rnpgbe_notify_powerdown - Notify firmware that the driver is going down
 * @mucse: pointer to private structure
 *
 * Skip the mailbox access if an earlier shutdown has already disabled the
 * PCI device.
 **/
static void rnpgbe_notify_powerdown(struct mucse *mucse)
{
	int err;

	if (!pci_is_enabled(mucse->pdev))
		return;

	err = rnpgbe_send_notify(&mucse->hw, false, mucse_fw_powerup);
	if (err)
		dev_warn(&mucse->pdev->dev,
			 "Send powerdown to hw failed %d\n", err);
}

/**
 * rnpgbe_rm_adapter - Remove netdev for this mucse structure
 * @pdev: PCI device information struct
 *
 * rnpgbe_rm_adapter remove a netdev for this mucse structure
 **/
static void rnpgbe_rm_adapter(struct pci_dev *pdev)
{
	struct mucse *mucse = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!mucse)
		return;
	netdev = mucse->netdev;
	unregister_netdev(netdev);
	rnpgbe_free_mbx_irq(mucse);
	rnpgbe_notify_powerdown(mucse);
	rnpgbe_clear_interrupt_scheme(mucse);
	free_netdev(netdev);
}

/**
 * rnpgbe_remove - Device removal routine
 * @pdev: PCI device information struct
 *
 * rnpgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device. This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void rnpgbe_remove(struct pci_dev *pdev)
{
	rnpgbe_rm_adapter(pdev);
	pci_release_mem_regions(pdev);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
}

/**
 * rnpgbe_dev_shutdown - Device shutdown routine
 * @pdev: PCI device information struct
 **/
static void rnpgbe_dev_shutdown(struct pci_dev *pdev)
{
	struct mucse *mucse = pci_get_drvdata(pdev);
	struct net_device *netdev = mucse->netdev;

	rtnl_lock();
	netif_device_detach(netdev);
	if (netif_running(netdev))
		dev_close(netdev);
	rtnl_unlock();

	rnpgbe_free_mbx_irq(mucse);
	rnpgbe_notify_powerdown(mucse);
	rnpgbe_clear_interrupt_scheme(mucse);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
}

/**
 * rnpgbe_shutdown - Device shutdown routine
 * @pdev: PCI device information struct
 *
 * rnpgbe_shutdown is called by the PCI subsystem to alert the driver
 * that os shutdown. Device should setup wakeup state here.
 **/
static void rnpgbe_shutdown(struct pci_dev *pdev)
{
	rnpgbe_dev_shutdown(pdev);
}

static struct pci_driver rnpgbe_driver = {
	.name     = rnpgbe_driver_name,
	.id_table = rnpgbe_pci_tbl,
	.probe    = rnpgbe_probe,
	.remove   = rnpgbe_remove,
	.shutdown = rnpgbe_shutdown,
};

module_pci_driver(rnpgbe_driver);

MODULE_DEVICE_TABLE(pci, rnpgbe_pci_tbl);
MODULE_AUTHOR("Yibo Dong, <dong100@mucse.com>");
MODULE_DESCRIPTION("Mucse(R) 1 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
