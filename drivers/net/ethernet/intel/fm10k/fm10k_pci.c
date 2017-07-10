/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2017 Intel Corporation.
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
#include <linux/interrupt.h>
#include <linux/aer.h>

#include "fm10k.h"

static const struct fm10k_info *fm10k_info_tbl[] = {
	[fm10k_device_pf] = &fm10k_pf_info,
	[fm10k_device_vf] = &fm10k_vf_info,
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
	{ PCI_VDEVICE(INTEL, FM10K_DEV_ID_VF), fm10k_device_vf },
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
	u32 __iomem *hw_addr = READ_ONCE(hw->hw_addr);
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

void fm10k_service_event_schedule(struct fm10k_intfc *interface)
{
	if (!test_bit(__FM10K_SERVICE_DISABLE, interface->state) &&
	    !test_and_set_bit(__FM10K_SERVICE_SCHED, interface->state)) {
		clear_bit(__FM10K_SERVICE_REQUEST, interface->state);
		queue_work(fm10k_workqueue, &interface->service_task);
	} else {
		set_bit(__FM10K_SERVICE_REQUEST, interface->state);
	}
}

static void fm10k_service_event_complete(struct fm10k_intfc *interface)
{
	WARN_ON(!test_bit(__FM10K_SERVICE_SCHED, interface->state));

	/* flush memory to make sure state is correct before next watchog */
	smp_mb__before_atomic();
	clear_bit(__FM10K_SERVICE_SCHED, interface->state);

	/* If a service event was requested since we started, immediately
	 * re-schedule now. This ensures we don't drop a request until the
	 * next timer event.
	 */
	if (test_bit(__FM10K_SERVICE_REQUEST, interface->state))
		fm10k_service_event_schedule(interface);
}

/**
 * fm10k_service_timer - Timer Call-back
 * @data: pointer to interface cast into an unsigned long
 **/
static void fm10k_service_timer(unsigned long data)
{
	struct fm10k_intfc *interface = (struct fm10k_intfc *)data;

	/* Reset the timer */
	mod_timer(&interface->service_timer, (HZ * 2) + jiffies);

	fm10k_service_event_schedule(interface);
}

static void fm10k_detach_subtask(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	u32 __iomem *hw_addr;
	u32 value;

	/* do nothing if device is still present or hw_addr is set */
	if (netif_device_present(netdev) || interface->hw.hw_addr)
		return;

	/* check the real address space to see if we've recovered */
	hw_addr = READ_ONCE(interface->uc_addr);
	value = readl(hw_addr);
	if (~value) {
		interface->hw.hw_addr = interface->uc_addr;
		netif_device_attach(netdev);
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);
		netdev_warn(netdev, "PCIe link restored, device now attached\n");
		return;
	}

	rtnl_lock();

	if (netif_running(netdev))
		dev_close(netdev);

	rtnl_unlock();
}

static void fm10k_prepare_for_reset(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;

	WARN_ON(in_interrupt());

	/* put off any impending NetWatchDogTimeout */
	netif_trans_update(netdev);

	while (test_and_set_bit(__FM10K_RESETTING, interface->state))
		usleep_range(1000, 2000);

	rtnl_lock();

	fm10k_iov_suspend(interface->pdev);

	if (netif_running(netdev))
		fm10k_close(netdev);

	fm10k_mbx_free_irq(interface);

	/* free interrupts */
	fm10k_clear_queueing_scheme(interface);

	/* delay any future reset requests */
	interface->last_reset = jiffies + (10 * HZ);

	rtnl_unlock();
}

static int fm10k_handle_reset(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int err;

	rtnl_lock();

	pci_set_master(interface->pdev);

	/* reset and initialize the hardware so it is in a known state */
	err = hw->mac.ops.reset_hw(hw);
	if (err) {
		dev_err(&interface->pdev->dev, "reset_hw failed: %d\n", err);
		goto reinit_err;
	}

	err = hw->mac.ops.init_hw(hw);
	if (err) {
		dev_err(&interface->pdev->dev, "init_hw failed: %d\n", err);
		goto reinit_err;
	}

	err = fm10k_init_queueing_scheme(interface);
	if (err) {
		dev_err(&interface->pdev->dev,
			"init_queueing_scheme failed: %d\n", err);
		goto reinit_err;
	}

	/* re-associate interrupts */
	err = fm10k_mbx_request_irq(interface);
	if (err)
		goto err_mbx_irq;

	err = fm10k_hw_ready(interface);
	if (err)
		goto err_open;

	/* update hardware address for VFs if perm_addr has changed */
	if (hw->mac.type == fm10k_mac_vf) {
		if (is_valid_ether_addr(hw->mac.perm_addr)) {
			ether_addr_copy(hw->mac.addr, hw->mac.perm_addr);
			ether_addr_copy(netdev->perm_addr, hw->mac.perm_addr);
			ether_addr_copy(netdev->dev_addr, hw->mac.perm_addr);
			netdev->addr_assign_type &= ~NET_ADDR_RANDOM;
		}

		if (hw->mac.vlan_override)
			netdev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
		else
			netdev->features |= NETIF_F_HW_VLAN_CTAG_RX;
	}

	err = netif_running(netdev) ? fm10k_open(netdev) : 0;
	if (err)
		goto err_open;

	fm10k_iov_resume(interface->pdev);

	rtnl_unlock();

	clear_bit(__FM10K_RESETTING, interface->state);

	return err;
err_open:
	fm10k_mbx_free_irq(interface);
err_mbx_irq:
	fm10k_clear_queueing_scheme(interface);
reinit_err:
	netif_device_detach(netdev);

	rtnl_unlock();

	clear_bit(__FM10K_RESETTING, interface->state);

	return err;
}

static void fm10k_reinit(struct fm10k_intfc *interface)
{
	int err;

	fm10k_prepare_for_reset(interface);

	err = fm10k_handle_reset(interface);
	if (err)
		dev_err(&interface->pdev->dev,
			"fm10k_handle_reset failed: %d\n", err);
}

static void fm10k_reset_subtask(struct fm10k_intfc *interface)
{
	if (!test_and_clear_bit(FM10K_FLAG_RESET_REQUESTED,
				interface->flags))
		return;

	netdev_err(interface->netdev, "Reset interface\n");

	fm10k_reinit(interface);
}

/**
 * fm10k_configure_swpri_map - Configure Receive SWPRI to PC mapping
 * @interface: board private structure
 *
 * Configure the SWPRI to PC mapping for the port.
 **/
static void fm10k_configure_swpri_map(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int i;

	/* clear flag indicating update is needed */
	clear_bit(FM10K_FLAG_SWPRI_CONFIG, interface->flags);

	/* these registers are only available on the PF */
	if (hw->mac.type != fm10k_mac_pf)
		return;

	/* configure SWPRI to PC map */
	for (i = 0; i < FM10K_SWPRI_MAX; i++)
		fm10k_write_reg(hw, FM10K_SWPRI_MAP(i),
				netdev_get_prio_tc_map(netdev, i));
}

/**
 * fm10k_watchdog_update_host_state - Update the link status based on host.
 * @interface: board private structure
 **/
static void fm10k_watchdog_update_host_state(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	s32 err;

	if (test_bit(__FM10K_LINK_DOWN, interface->state)) {
		interface->host_ready = false;
		if (time_is_after_jiffies(interface->link_down_event))
			return;
		clear_bit(__FM10K_LINK_DOWN, interface->state);
	}

	if (test_bit(FM10K_FLAG_SWPRI_CONFIG, interface->flags)) {
		if (rtnl_trylock()) {
			fm10k_configure_swpri_map(interface);
			rtnl_unlock();
		}
	}

	/* lock the mailbox for transmit and receive */
	fm10k_mbx_lock(interface);

	err = hw->mac.ops.get_host_state(hw, &interface->host_ready);
	if (err && time_is_before_jiffies(interface->last_reset))
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	/* free the lock */
	fm10k_mbx_unlock(interface);
}

/**
 * fm10k_mbx_subtask - Process upstream and downstream mailboxes
 * @interface: board private structure
 *
 * This function will process both the upstream and downstream mailboxes.
 **/
static void fm10k_mbx_subtask(struct fm10k_intfc *interface)
{
	/* process upstream mailbox and update device state */
	fm10k_watchdog_update_host_state(interface);

	/* process downstream mailboxes */
	fm10k_iov_mbx(interface);
}

/**
 * fm10k_watchdog_host_is_ready - Update netdev status based on host ready
 * @interface: board private structure
 **/
static void fm10k_watchdog_host_is_ready(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;

	/* only continue if link state is currently down */
	if (netif_carrier_ok(netdev))
		return;

	netif_info(interface, drv, netdev, "NIC Link is up\n");

	netif_carrier_on(netdev);
	netif_tx_wake_all_queues(netdev);
}

/**
 * fm10k_watchdog_host_not_ready - Update netdev status based on host not ready
 * @interface: board private structure
 **/
static void fm10k_watchdog_host_not_ready(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;

	/* only continue if link state is currently up */
	if (!netif_carrier_ok(netdev))
		return;

	netif_info(interface, drv, netdev, "NIC Link is down\n");

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);
}

/**
 * fm10k_update_stats - Update the board statistics counters.
 * @interface: board private structure
 **/
void fm10k_update_stats(struct fm10k_intfc *interface)
{
	struct net_device_stats *net_stats = &interface->netdev->stats;
	struct fm10k_hw *hw = &interface->hw;
	u64 hw_csum_tx_good = 0, hw_csum_rx_good = 0, rx_length_errors = 0;
	u64 rx_switch_errors = 0, rx_drops = 0, rx_pp_errors = 0;
	u64 rx_link_errors = 0;
	u64 rx_errors = 0, rx_csum_errors = 0, tx_csum_errors = 0;
	u64 restart_queue = 0, tx_busy = 0, alloc_failed = 0;
	u64 rx_bytes_nic = 0, rx_pkts_nic = 0, rx_drops_nic = 0;
	u64 tx_bytes_nic = 0, tx_pkts_nic = 0;
	u64 bytes, pkts;
	int i;

	/* ensure only one thread updates stats at a time */
	if (test_and_set_bit(__FM10K_UPDATING_STATS, interface->state))
		return;

	/* do not allow stats update via service task for next second */
	interface->next_stats_update = jiffies + HZ;

	/* gather some stats to the interface struct that are per queue */
	for (bytes = 0, pkts = 0, i = 0; i < interface->num_tx_queues; i++) {
		struct fm10k_ring *tx_ring = READ_ONCE(interface->tx_ring[i]);

		if (!tx_ring)
			continue;

		restart_queue += tx_ring->tx_stats.restart_queue;
		tx_busy += tx_ring->tx_stats.tx_busy;
		tx_csum_errors += tx_ring->tx_stats.csum_err;
		bytes += tx_ring->stats.bytes;
		pkts += tx_ring->stats.packets;
		hw_csum_tx_good += tx_ring->tx_stats.csum_good;
	}

	interface->restart_queue = restart_queue;
	interface->tx_busy = tx_busy;
	net_stats->tx_bytes = bytes;
	net_stats->tx_packets = pkts;
	interface->tx_csum_errors = tx_csum_errors;
	interface->hw_csum_tx_good = hw_csum_tx_good;

	/* gather some stats to the interface struct that are per queue */
	for (bytes = 0, pkts = 0, i = 0; i < interface->num_rx_queues; i++) {
		struct fm10k_ring *rx_ring = READ_ONCE(interface->rx_ring[i]);

		if (!rx_ring)
			continue;

		bytes += rx_ring->stats.bytes;
		pkts += rx_ring->stats.packets;
		alloc_failed += rx_ring->rx_stats.alloc_failed;
		rx_csum_errors += rx_ring->rx_stats.csum_err;
		rx_errors += rx_ring->rx_stats.errors;
		hw_csum_rx_good += rx_ring->rx_stats.csum_good;
		rx_switch_errors += rx_ring->rx_stats.switch_errors;
		rx_drops += rx_ring->rx_stats.drops;
		rx_pp_errors += rx_ring->rx_stats.pp_errors;
		rx_link_errors += rx_ring->rx_stats.link_errors;
		rx_length_errors += rx_ring->rx_stats.length_errors;
	}

	net_stats->rx_bytes = bytes;
	net_stats->rx_packets = pkts;
	interface->alloc_failed = alloc_failed;
	interface->rx_csum_errors = rx_csum_errors;
	interface->hw_csum_rx_good = hw_csum_rx_good;
	interface->rx_switch_errors = rx_switch_errors;
	interface->rx_drops = rx_drops;
	interface->rx_pp_errors = rx_pp_errors;
	interface->rx_link_errors = rx_link_errors;
	interface->rx_length_errors = rx_length_errors;

	hw->mac.ops.update_hw_stats(hw, &interface->stats);

	for (i = 0; i < hw->mac.max_queues; i++) {
		struct fm10k_hw_stats_q *q = &interface->stats.q[i];

		tx_bytes_nic += q->tx_bytes.count;
		tx_pkts_nic += q->tx_packets.count;
		rx_bytes_nic += q->rx_bytes.count;
		rx_pkts_nic += q->rx_packets.count;
		rx_drops_nic += q->rx_drops.count;
	}

	interface->tx_bytes_nic = tx_bytes_nic;
	interface->tx_packets_nic = tx_pkts_nic;
	interface->rx_bytes_nic = rx_bytes_nic;
	interface->rx_packets_nic = rx_pkts_nic;
	interface->rx_drops_nic = rx_drops_nic;

	/* Fill out the OS statistics structure */
	net_stats->rx_errors = rx_errors;
	net_stats->rx_dropped = interface->stats.nodesc_drop.count;

	clear_bit(__FM10K_UPDATING_STATS, interface->state);
}

/**
 * fm10k_watchdog_flush_tx - flush queues on host not ready
 * @interface - pointer to the device interface structure
 **/
static void fm10k_watchdog_flush_tx(struct fm10k_intfc *interface)
{
	int some_tx_pending = 0;
	int i;

	/* nothing to do if carrier is up */
	if (netif_carrier_ok(interface->netdev))
		return;

	for (i = 0; i < interface->num_tx_queues; i++) {
		struct fm10k_ring *tx_ring = interface->tx_ring[i];

		if (tx_ring->next_to_use != tx_ring->next_to_clean) {
			some_tx_pending = 1;
			break;
		}
	}

	/* We've lost link, so the controller stops DMA, but we've got
	 * queued Tx work that's never going to get done, so reset
	 * controller to flush Tx.
	 */
	if (some_tx_pending)
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);
}

/**
 * fm10k_watchdog_subtask - check and bring link up
 * @interface - pointer to the device interface structure
 **/
static void fm10k_watchdog_subtask(struct fm10k_intfc *interface)
{
	/* if interface is down do nothing */
	if (test_bit(__FM10K_DOWN, interface->state) ||
	    test_bit(__FM10K_RESETTING, interface->state))
		return;

	if (interface->host_ready)
		fm10k_watchdog_host_is_ready(interface);
	else
		fm10k_watchdog_host_not_ready(interface);

	/* update stats only once every second */
	if (time_is_before_jiffies(interface->next_stats_update))
		fm10k_update_stats(interface);

	/* flush any uncompleted work */
	fm10k_watchdog_flush_tx(interface);
}

/**
 * fm10k_check_hang_subtask - check for hung queues and dropped interrupts
 * @interface - pointer to the device interface structure
 *
 * This function serves two purposes.  First it strobes the interrupt lines
 * in order to make certain interrupts are occurring.  Secondly it sets the
 * bits needed to check for TX hangs.  As a result we should immediately
 * determine if a hang has occurred.
 */
static void fm10k_check_hang_subtask(struct fm10k_intfc *interface)
{
	int i;

	/* If we're down or resetting, just bail */
	if (test_bit(__FM10K_DOWN, interface->state) ||
	    test_bit(__FM10K_RESETTING, interface->state))
		return;

	/* rate limit tx hang checks to only once every 2 seconds */
	if (time_is_after_eq_jiffies(interface->next_tx_hang_check))
		return;
	interface->next_tx_hang_check = jiffies + (2 * HZ);

	if (netif_carrier_ok(interface->netdev)) {
		/* Force detection of hung controller */
		for (i = 0; i < interface->num_tx_queues; i++)
			set_check_for_tx_hang(interface->tx_ring[i]);

		/* Rearm all in-use q_vectors for immediate firing */
		for (i = 0; i < interface->num_q_vectors; i++) {
			struct fm10k_q_vector *qv = interface->q_vector[i];

			if (!qv->tx.count && !qv->rx.count)
				continue;
			writel(FM10K_ITR_ENABLE | FM10K_ITR_PENDING2, qv->itr);
		}
	}
}

/**
 * fm10k_service_task - manages and runs subtasks
 * @work: pointer to work_struct containing our data
 **/
static void fm10k_service_task(struct work_struct *work)
{
	struct fm10k_intfc *interface;

	interface = container_of(work, struct fm10k_intfc, service_task);

	/* tasks run even when interface is down */
	fm10k_mbx_subtask(interface);
	fm10k_detach_subtask(interface);
	fm10k_reset_subtask(interface);

	/* tasks only run when interface is up */
	fm10k_watchdog_subtask(interface);
	fm10k_check_hang_subtask(interface);

	/* release lock on service events to allow scheduling next event */
	fm10k_service_event_complete(interface);
}

/**
 * fm10k_configure_tx_ring - Configure Tx ring after Reset
 * @interface: board private structure
 * @ring: structure containing ring specific data
 *
 * Configure the Tx descriptor ring after a reset.
 **/
static void fm10k_configure_tx_ring(struct fm10k_intfc *interface,
				    struct fm10k_ring *ring)
{
	struct fm10k_hw *hw = &interface->hw;
	u64 tdba = ring->dma;
	u32 size = ring->count * sizeof(struct fm10k_tx_desc);
	u32 txint = FM10K_INT_MAP_DISABLE;
	u32 txdctl = BIT(FM10K_TXDCTL_MAX_TIME_SHIFT) | FM10K_TXDCTL_ENABLE;
	u8 reg_idx = ring->reg_idx;

	/* disable queue to avoid issues while updating state */
	fm10k_write_reg(hw, FM10K_TXDCTL(reg_idx), 0);
	fm10k_write_flush(hw);

	/* possible poll here to verify ring resources have been cleaned */

	/* set location and size for descriptor ring */
	fm10k_write_reg(hw, FM10K_TDBAL(reg_idx), tdba & DMA_BIT_MASK(32));
	fm10k_write_reg(hw, FM10K_TDBAH(reg_idx), tdba >> 32);
	fm10k_write_reg(hw, FM10K_TDLEN(reg_idx), size);

	/* reset head and tail pointers */
	fm10k_write_reg(hw, FM10K_TDH(reg_idx), 0);
	fm10k_write_reg(hw, FM10K_TDT(reg_idx), 0);

	/* store tail pointer */
	ring->tail = &interface->uc_addr[FM10K_TDT(reg_idx)];

	/* reset ntu and ntc to place SW in sync with hardware */
	ring->next_to_clean = 0;
	ring->next_to_use = 0;

	/* Map interrupt */
	if (ring->q_vector) {
		txint = ring->q_vector->v_idx + NON_Q_VECTORS(hw);
		txint |= FM10K_INT_MAP_TIMER0;
	}

	fm10k_write_reg(hw, FM10K_TXINT(reg_idx), txint);

	/* enable use of FTAG bit in Tx descriptor, register is RO for VF */
	fm10k_write_reg(hw, FM10K_PFVTCTL(reg_idx),
			FM10K_PFVTCTL_FTAG_DESC_ENABLE);

	/* Initialize XPS */
	if (!test_and_set_bit(__FM10K_TX_XPS_INIT_DONE, ring->state) &&
	    ring->q_vector)
		netif_set_xps_queue(ring->netdev,
				    &ring->q_vector->affinity_mask,
				    ring->queue_index);

	/* enable queue */
	fm10k_write_reg(hw, FM10K_TXDCTL(reg_idx), txdctl);
}

/**
 * fm10k_enable_tx_ring - Verify Tx ring is enabled after configuration
 * @interface: board private structure
 * @ring: structure containing ring specific data
 *
 * Verify the Tx descriptor ring is ready for transmit.
 **/
static void fm10k_enable_tx_ring(struct fm10k_intfc *interface,
				 struct fm10k_ring *ring)
{
	struct fm10k_hw *hw = &interface->hw;
	int wait_loop = 10;
	u32 txdctl;
	u8 reg_idx = ring->reg_idx;

	/* if we are already enabled just exit */
	if (fm10k_read_reg(hw, FM10K_TXDCTL(reg_idx)) & FM10K_TXDCTL_ENABLE)
		return;

	/* poll to verify queue is enabled */
	do {
		usleep_range(1000, 2000);
		txdctl = fm10k_read_reg(hw, FM10K_TXDCTL(reg_idx));
	} while (!(txdctl & FM10K_TXDCTL_ENABLE) && --wait_loop);
	if (!wait_loop)
		netif_err(interface, drv, interface->netdev,
			  "Could not enable Tx Queue %d\n", reg_idx);
}

/**
 * fm10k_configure_tx - Configure Transmit Unit after Reset
 * @interface: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void fm10k_configure_tx(struct fm10k_intfc *interface)
{
	int i;

	/* Setup the HW Tx Head and Tail descriptor pointers */
	for (i = 0; i < interface->num_tx_queues; i++)
		fm10k_configure_tx_ring(interface, interface->tx_ring[i]);

	/* poll here to verify that Tx rings are now enabled */
	for (i = 0; i < interface->num_tx_queues; i++)
		fm10k_enable_tx_ring(interface, interface->tx_ring[i]);
}

/**
 * fm10k_configure_rx_ring - Configure Rx ring after Reset
 * @interface: board private structure
 * @ring: structure containing ring specific data
 *
 * Configure the Rx descriptor ring after a reset.
 **/
static void fm10k_configure_rx_ring(struct fm10k_intfc *interface,
				    struct fm10k_ring *ring)
{
	u64 rdba = ring->dma;
	struct fm10k_hw *hw = &interface->hw;
	u32 size = ring->count * sizeof(union fm10k_rx_desc);
	u32 rxqctl, rxdctl = FM10K_RXDCTL_WRITE_BACK_MIN_DELAY;
	u32 srrctl = FM10K_SRRCTL_BUFFER_CHAINING_EN;
	u32 rxint = FM10K_INT_MAP_DISABLE;
	u8 rx_pause = interface->rx_pause;
	u8 reg_idx = ring->reg_idx;

	/* disable queue to avoid issues while updating state */
	rxqctl = fm10k_read_reg(hw, FM10K_RXQCTL(reg_idx));
	rxqctl &= ~FM10K_RXQCTL_ENABLE;
	fm10k_write_reg(hw, FM10K_RXQCTL(reg_idx), rxqctl);
	fm10k_write_flush(hw);

	/* possible poll here to verify ring resources have been cleaned */

	/* set location and size for descriptor ring */
	fm10k_write_reg(hw, FM10K_RDBAL(reg_idx), rdba & DMA_BIT_MASK(32));
	fm10k_write_reg(hw, FM10K_RDBAH(reg_idx), rdba >> 32);
	fm10k_write_reg(hw, FM10K_RDLEN(reg_idx), size);

	/* reset head and tail pointers */
	fm10k_write_reg(hw, FM10K_RDH(reg_idx), 0);
	fm10k_write_reg(hw, FM10K_RDT(reg_idx), 0);

	/* store tail pointer */
	ring->tail = &interface->uc_addr[FM10K_RDT(reg_idx)];

	/* reset ntu and ntc to place SW in sync with hardware */
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
	ring->next_to_alloc = 0;

	/* Configure the Rx buffer size for one buff without split */
	srrctl |= FM10K_RX_BUFSZ >> FM10K_SRRCTL_BSIZEPKT_SHIFT;

	/* Configure the Rx ring to suppress loopback packets */
	srrctl |= FM10K_SRRCTL_LOOPBACK_SUPPRESS;
	fm10k_write_reg(hw, FM10K_SRRCTL(reg_idx), srrctl);

	/* Enable drop on empty */
#ifdef CONFIG_DCB
	if (interface->pfc_en)
		rx_pause = interface->pfc_en;
#endif
	if (!(rx_pause & BIT(ring->qos_pc)))
		rxdctl |= FM10K_RXDCTL_DROP_ON_EMPTY;

	fm10k_write_reg(hw, FM10K_RXDCTL(reg_idx), rxdctl);

	/* assign default VLAN to queue */
	ring->vid = hw->mac.default_vid;

	/* if we have an active VLAN, disable default VLAN ID */
	if (test_bit(hw->mac.default_vid, interface->active_vlans))
		ring->vid |= FM10K_VLAN_CLEAR;

	/* Map interrupt */
	if (ring->q_vector) {
		rxint = ring->q_vector->v_idx + NON_Q_VECTORS(hw);
		rxint |= FM10K_INT_MAP_TIMER1;
	}

	fm10k_write_reg(hw, FM10K_RXINT(reg_idx), rxint);

	/* enable queue */
	rxqctl = fm10k_read_reg(hw, FM10K_RXQCTL(reg_idx));
	rxqctl |= FM10K_RXQCTL_ENABLE;
	fm10k_write_reg(hw, FM10K_RXQCTL(reg_idx), rxqctl);

	/* place buffers on ring for receive data */
	fm10k_alloc_rx_buffers(ring, fm10k_desc_unused(ring));
}

/**
 * fm10k_update_rx_drop_en - Configures the drop enable bits for Rx rings
 * @interface: board private structure
 *
 * Configure the drop enable bits for the Rx rings.
 **/
void fm10k_update_rx_drop_en(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	u8 rx_pause = interface->rx_pause;
	int i;

#ifdef CONFIG_DCB
	if (interface->pfc_en)
		rx_pause = interface->pfc_en;

#endif
	for (i = 0; i < interface->num_rx_queues; i++) {
		struct fm10k_ring *ring = interface->rx_ring[i];
		u32 rxdctl = FM10K_RXDCTL_WRITE_BACK_MIN_DELAY;
		u8 reg_idx = ring->reg_idx;

		if (!(rx_pause & BIT(ring->qos_pc)))
			rxdctl |= FM10K_RXDCTL_DROP_ON_EMPTY;

		fm10k_write_reg(hw, FM10K_RXDCTL(reg_idx), rxdctl);
	}
}

/**
 * fm10k_configure_dglort - Configure Receive DGLORT after reset
 * @interface: board private structure
 *
 * Configure the DGLORT description and RSS tables.
 **/
static void fm10k_configure_dglort(struct fm10k_intfc *interface)
{
	struct fm10k_dglort_cfg dglort = { 0 };
	struct fm10k_hw *hw = &interface->hw;
	int i;
	u32 mrqc;

	/* Fill out hash function seeds */
	for (i = 0; i < FM10K_RSSRK_SIZE; i++)
		fm10k_write_reg(hw, FM10K_RSSRK(0, i), interface->rssrk[i]);

	/* Write RETA table to hardware */
	for (i = 0; i < FM10K_RETA_SIZE; i++)
		fm10k_write_reg(hw, FM10K_RETA(0, i), interface->reta[i]);

	/* Generate RSS hash based on packet types, TCP/UDP
	 * port numbers and/or IPv4/v6 src and dst addresses
	 */
	mrqc = FM10K_MRQC_IPV4 |
	       FM10K_MRQC_TCP_IPV4 |
	       FM10K_MRQC_IPV6 |
	       FM10K_MRQC_TCP_IPV6;

	if (test_bit(FM10K_FLAG_RSS_FIELD_IPV4_UDP, interface->flags))
		mrqc |= FM10K_MRQC_UDP_IPV4;
	if (test_bit(FM10K_FLAG_RSS_FIELD_IPV6_UDP, interface->flags))
		mrqc |= FM10K_MRQC_UDP_IPV6;

	fm10k_write_reg(hw, FM10K_MRQC(0), mrqc);

	/* configure default DGLORT mapping for RSS/DCB */
	dglort.inner_rss = 1;
	dglort.rss_l = fls(interface->ring_feature[RING_F_RSS].mask);
	dglort.pc_l = fls(interface->ring_feature[RING_F_QOS].mask);
	hw->mac.ops.configure_dglort_map(hw, &dglort);

	/* assign GLORT per queue for queue mapped testing */
	if (interface->glort_count > 64) {
		memset(&dglort, 0, sizeof(dglort));
		dglort.inner_rss = 1;
		dglort.glort = interface->glort + 64;
		dglort.idx = fm10k_dglort_pf_queue;
		dglort.queue_l = fls(interface->num_rx_queues - 1);
		hw->mac.ops.configure_dglort_map(hw, &dglort);
	}

	/* assign glort value for RSS/DCB specific to this interface */
	memset(&dglort, 0, sizeof(dglort));
	dglort.inner_rss = 1;
	dglort.glort = interface->glort;
	dglort.rss_l = fls(interface->ring_feature[RING_F_RSS].mask);
	dglort.pc_l = fls(interface->ring_feature[RING_F_QOS].mask);
	/* configure DGLORT mapping for RSS/DCB */
	dglort.idx = fm10k_dglort_pf_rss;
	if (interface->l2_accel)
		dglort.shared_l = fls(interface->l2_accel->size);
	hw->mac.ops.configure_dglort_map(hw, &dglort);
}

/**
 * fm10k_configure_rx - Configure Receive Unit after Reset
 * @interface: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void fm10k_configure_rx(struct fm10k_intfc *interface)
{
	int i;

	/* Configure SWPRI to PC map */
	fm10k_configure_swpri_map(interface);

	/* Configure RSS and DGLORT map */
	fm10k_configure_dglort(interface);

	/* Setup the HW Rx Head and Tail descriptor pointers */
	for (i = 0; i < interface->num_rx_queues; i++)
		fm10k_configure_rx_ring(interface, interface->rx_ring[i]);

	/* possible poll here to verify that Rx rings are now enabled */
}

static void fm10k_napi_enable_all(struct fm10k_intfc *interface)
{
	struct fm10k_q_vector *q_vector;
	int q_idx;

	for (q_idx = 0; q_idx < interface->num_q_vectors; q_idx++) {
		q_vector = interface->q_vector[q_idx];
		napi_enable(&q_vector->napi);
	}
}

static irqreturn_t fm10k_msix_clean_rings(int __always_unused irq, void *data)
{
	struct fm10k_q_vector *q_vector = data;

	if (q_vector->rx.count || q_vector->tx.count)
		napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}

static irqreturn_t fm10k_msix_mbx_vf(int __always_unused irq, void *data)
{
	struct fm10k_intfc *interface = data;
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_mbx_info *mbx = &hw->mbx;

	/* re-enable mailbox interrupt and indicate 20us delay */
	fm10k_write_reg(hw, FM10K_VFITR(FM10K_MBX_VECTOR),
			(FM10K_MBX_INT_DELAY >> hw->mac.itr_scale) |
			FM10K_ITR_ENABLE);

	/* service upstream mailbox */
	if (fm10k_mbx_trylock(interface)) {
		mbx->ops.process(hw, mbx);
		fm10k_mbx_unlock(interface);
	}

	hw->mac.get_host_state = true;
	fm10k_service_event_schedule(interface);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 *  fm10k_netpoll - A Polling 'interrupt' handler
 *  @netdev: network interface device structure
 *
 *  This is used by netconsole to send skbs without having to re-enable
 *  interrupts. It's not called while the normal interrupt routine is executing.
 **/
void fm10k_netpoll(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	int i;

	/* if interface is down do nothing */
	if (test_bit(__FM10K_DOWN, interface->state))
		return;

	for (i = 0; i < interface->num_q_vectors; i++)
		fm10k_msix_clean_rings(0, interface->q_vector[i]);
}

#endif
#define FM10K_ERR_MSG(type) case (type): error = #type; break
static void fm10k_handle_fault(struct fm10k_intfc *interface, int type,
			       struct fm10k_fault *fault)
{
	struct pci_dev *pdev = interface->pdev;
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_iov_data *iov_data = interface->iov_data;
	char *error;

	switch (type) {
	case FM10K_PCA_FAULT:
		switch (fault->type) {
		default:
			error = "Unknown PCA error";
			break;
		FM10K_ERR_MSG(PCA_NO_FAULT);
		FM10K_ERR_MSG(PCA_UNMAPPED_ADDR);
		FM10K_ERR_MSG(PCA_BAD_QACCESS_PF);
		FM10K_ERR_MSG(PCA_BAD_QACCESS_VF);
		FM10K_ERR_MSG(PCA_MALICIOUS_REQ);
		FM10K_ERR_MSG(PCA_POISONED_TLP);
		FM10K_ERR_MSG(PCA_TLP_ABORT);
		}
		break;
	case FM10K_THI_FAULT:
		switch (fault->type) {
		default:
			error = "Unknown THI error";
			break;
		FM10K_ERR_MSG(THI_NO_FAULT);
		FM10K_ERR_MSG(THI_MAL_DIS_Q_FAULT);
		}
		break;
	case FM10K_FUM_FAULT:
		switch (fault->type) {
		default:
			error = "Unknown FUM error";
			break;
		FM10K_ERR_MSG(FUM_NO_FAULT);
		FM10K_ERR_MSG(FUM_UNMAPPED_ADDR);
		FM10K_ERR_MSG(FUM_BAD_VF_QACCESS);
		FM10K_ERR_MSG(FUM_ADD_DECODE_ERR);
		FM10K_ERR_MSG(FUM_RO_ERROR);
		FM10K_ERR_MSG(FUM_QPRC_CRC_ERROR);
		FM10K_ERR_MSG(FUM_CSR_TIMEOUT);
		FM10K_ERR_MSG(FUM_INVALID_TYPE);
		FM10K_ERR_MSG(FUM_INVALID_LENGTH);
		FM10K_ERR_MSG(FUM_INVALID_BE);
		FM10K_ERR_MSG(FUM_INVALID_ALIGN);
		}
		break;
	default:
		error = "Undocumented fault";
		break;
	}

	dev_warn(&pdev->dev,
		 "%s Address: 0x%llx SpecInfo: 0x%x Func: %02x.%0x\n",
		 error, fault->address, fault->specinfo,
		 PCI_SLOT(fault->func), PCI_FUNC(fault->func));

	/* For VF faults, clear out the respective LPORT, reset the queue
	 * resources, and then reconnect to the mailbox. This allows the
	 * VF in question to resume behavior. For transient faults that are
	 * the result of non-malicious behavior this will log the fault and
	 * allow the VF to resume functionality. Obviously for malicious VFs
	 * they will be able to attempt malicious behavior again. In this
	 * case, the system administrator will need to step in and manually
	 * remove or disable the VF in question.
	 */
	if (fault->func && iov_data) {
		int vf = fault->func - 1;
		struct fm10k_vf_info *vf_info = &iov_data->vf_info[vf];

		hw->iov.ops.reset_lport(hw, vf_info);
		hw->iov.ops.reset_resources(hw, vf_info);

		/* reset_lport disables the VF, so re-enable it */
		hw->iov.ops.set_lport(hw, vf_info, vf,
				      FM10K_VF_FLAG_MULTI_CAPABLE);

		/* reset_resources will disconnect from the mbx  */
		vf_info->mbx.ops.connect(hw, &vf_info->mbx);
	}
}

static void fm10k_report_fault(struct fm10k_intfc *interface, u32 eicr)
{
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_fault fault = { 0 };
	int type, err;

	for (eicr &= FM10K_EICR_FAULT_MASK, type = FM10K_PCA_FAULT;
	     eicr;
	     eicr >>= 1, type += FM10K_FAULT_SIZE) {
		/* only check if there is an error reported */
		if (!(eicr & 0x1))
			continue;

		/* retrieve fault info */
		err = hw->mac.ops.get_fault(hw, type, &fault);
		if (err) {
			dev_err(&interface->pdev->dev,
				"error reading fault\n");
			continue;
		}

		fm10k_handle_fault(interface, type, &fault);
	}
}

static void fm10k_reset_drop_on_empty(struct fm10k_intfc *interface, u32 eicr)
{
	struct fm10k_hw *hw = &interface->hw;
	const u32 rxdctl = FM10K_RXDCTL_WRITE_BACK_MIN_DELAY;
	u32 maxholdq;
	int q;

	if (!(eicr & FM10K_EICR_MAXHOLDTIME))
		return;

	maxholdq = fm10k_read_reg(hw, FM10K_MAXHOLDQ(7));
	if (maxholdq)
		fm10k_write_reg(hw, FM10K_MAXHOLDQ(7), maxholdq);
	for (q = 255;;) {
		if (maxholdq & BIT(31)) {
			if (q < FM10K_MAX_QUEUES_PF) {
				interface->rx_overrun_pf++;
				fm10k_write_reg(hw, FM10K_RXDCTL(q), rxdctl);
			} else {
				interface->rx_overrun_vf++;
			}
		}

		maxholdq *= 2;
		if (!maxholdq)
			q &= ~(32 - 1);

		if (!q)
			break;

		if (q-- % 32)
			continue;

		maxholdq = fm10k_read_reg(hw, FM10K_MAXHOLDQ(q / 32));
		if (maxholdq)
			fm10k_write_reg(hw, FM10K_MAXHOLDQ(q / 32), maxholdq);
	}
}

static irqreturn_t fm10k_msix_mbx_pf(int __always_unused irq, void *data)
{
	struct fm10k_intfc *interface = data;
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 eicr;
	s32 err = 0;

	/* unmask any set bits related to this interrupt */
	eicr = fm10k_read_reg(hw, FM10K_EICR);
	fm10k_write_reg(hw, FM10K_EICR, eicr & (FM10K_EICR_MAILBOX |
						FM10K_EICR_SWITCHREADY |
						FM10K_EICR_SWITCHNOTREADY));

	/* report any faults found to the message log */
	fm10k_report_fault(interface, eicr);

	/* reset any queues disabled due to receiver overrun */
	fm10k_reset_drop_on_empty(interface, eicr);

	/* service mailboxes */
	if (fm10k_mbx_trylock(interface)) {
		err = mbx->ops.process(hw, mbx);
		/* handle VFLRE events */
		fm10k_iov_event(interface);
		fm10k_mbx_unlock(interface);
	}

	if (err == FM10K_ERR_RESET_REQUESTED)
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	/* if switch toggled state we should reset GLORTs */
	if (eicr & FM10K_EICR_SWITCHNOTREADY) {
		/* force link down for at least 4 seconds */
		interface->link_down_event = jiffies + (4 * HZ);
		set_bit(__FM10K_LINK_DOWN, interface->state);

		/* reset dglort_map back to no config */
		hw->mac.dglort_map = FM10K_DGLORTMAP_NONE;
	}

	/* we should validate host state after interrupt event */
	hw->mac.get_host_state = true;

	/* validate host state, and handle VF mailboxes in the service task */
	fm10k_service_event_schedule(interface);

	/* re-enable mailbox interrupt and indicate 20us delay */
	fm10k_write_reg(hw, FM10K_ITR(FM10K_MBX_VECTOR),
			(FM10K_MBX_INT_DELAY >> hw->mac.itr_scale) |
			FM10K_ITR_ENABLE);

	return IRQ_HANDLED;
}

void fm10k_mbx_free_irq(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	struct msix_entry *entry;
	int itr_reg;

	/* no mailbox IRQ to free if MSI-X is not enabled */
	if (!interface->msix_entries)
		return;

	entry = &interface->msix_entries[FM10K_MBX_VECTOR];

	/* disconnect the mailbox */
	hw->mbx.ops.disconnect(hw, &hw->mbx);

	/* disable Mailbox cause */
	if (hw->mac.type == fm10k_mac_pf) {
		fm10k_write_reg(hw, FM10K_EIMR,
				FM10K_EIMR_DISABLE(PCA_FAULT) |
				FM10K_EIMR_DISABLE(FUM_FAULT) |
				FM10K_EIMR_DISABLE(MAILBOX) |
				FM10K_EIMR_DISABLE(SWITCHREADY) |
				FM10K_EIMR_DISABLE(SWITCHNOTREADY) |
				FM10K_EIMR_DISABLE(SRAMERROR) |
				FM10K_EIMR_DISABLE(VFLR) |
				FM10K_EIMR_DISABLE(MAXHOLDTIME));
		itr_reg = FM10K_ITR(FM10K_MBX_VECTOR);
	} else {
		itr_reg = FM10K_VFITR(FM10K_MBX_VECTOR);
	}

	fm10k_write_reg(hw, itr_reg, FM10K_ITR_MASK_SET);

	free_irq(entry->vector, interface);
}

static s32 fm10k_mbx_mac_addr(struct fm10k_hw *hw, u32 **results,
			      struct fm10k_mbx_info *mbx)
{
	bool vlan_override = hw->mac.vlan_override;
	u16 default_vid = hw->mac.default_vid;
	struct fm10k_intfc *interface;
	s32 err;

	err = fm10k_msg_mac_vlan_vf(hw, results, mbx);
	if (err)
		return err;

	interface = container_of(hw, struct fm10k_intfc, hw);

	/* MAC was changed so we need reset */
	if (is_valid_ether_addr(hw->mac.perm_addr) &&
	    !ether_addr_equal(hw->mac.perm_addr, hw->mac.addr))
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	/* VLAN override was changed, or default VLAN changed */
	if ((vlan_override != hw->mac.vlan_override) ||
	    (default_vid != hw->mac.default_vid))
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	return 0;
}

/* generic error handler for mailbox issues */
static s32 fm10k_mbx_error(struct fm10k_hw *hw, u32 **results,
			   struct fm10k_mbx_info __always_unused *mbx)
{
	struct fm10k_intfc *interface;
	struct pci_dev *pdev;

	interface = container_of(hw, struct fm10k_intfc, hw);
	pdev = interface->pdev;

	dev_err(&pdev->dev, "Unknown message ID %u\n",
		**results & FM10K_TLV_ID_MASK);

	return 0;
}

static const struct fm10k_msg_data vf_mbx_data[] = {
	FM10K_TLV_MSG_TEST_HANDLER(fm10k_tlv_msg_test),
	FM10K_VF_MSG_MAC_VLAN_HANDLER(fm10k_mbx_mac_addr),
	FM10K_VF_MSG_LPORT_STATE_HANDLER(fm10k_msg_lport_state_vf),
	FM10K_TLV_MSG_ERROR_HANDLER(fm10k_mbx_error),
};

static int fm10k_mbx_request_irq_vf(struct fm10k_intfc *interface)
{
	struct msix_entry *entry = &interface->msix_entries[FM10K_MBX_VECTOR];
	struct net_device *dev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int err;

	/* Use timer0 for interrupt moderation on the mailbox */
	u32 itr = entry->entry | FM10K_INT_MAP_TIMER0;

	/* register mailbox handlers */
	err = hw->mbx.ops.register_handlers(&hw->mbx, vf_mbx_data);
	if (err)
		return err;

	/* request the IRQ */
	err = request_irq(entry->vector, fm10k_msix_mbx_vf, 0,
			  dev->name, interface);
	if (err) {
		netif_err(interface, probe, dev,
			  "request_irq for msix_mbx failed: %d\n", err);
		return err;
	}

	/* map all of the interrupt sources */
	fm10k_write_reg(hw, FM10K_VFINT_MAP, itr);

	/* enable interrupt */
	fm10k_write_reg(hw, FM10K_VFITR(entry->entry), FM10K_ITR_ENABLE);

	return 0;
}

static s32 fm10k_lport_map(struct fm10k_hw *hw, u32 **results,
			   struct fm10k_mbx_info *mbx)
{
	struct fm10k_intfc *interface;
	u32 dglort_map = hw->mac.dglort_map;
	s32 err;

	interface = container_of(hw, struct fm10k_intfc, hw);

	err = fm10k_msg_err_pf(hw, results, mbx);
	if (!err && hw->swapi.status) {
		/* force link down for a reasonable delay */
		interface->link_down_event = jiffies + (2 * HZ);
		set_bit(__FM10K_LINK_DOWN, interface->state);

		/* reset dglort_map back to no config */
		hw->mac.dglort_map = FM10K_DGLORTMAP_NONE;

		fm10k_service_event_schedule(interface);

		/* prevent overloading kernel message buffer */
		if (interface->lport_map_failed)
			return 0;

		interface->lport_map_failed = true;

		if (hw->swapi.status == FM10K_MSG_ERR_PEP_NOT_SCHEDULED)
			dev_warn(&interface->pdev->dev,
				 "cannot obtain link because the host interface is configured for a PCIe host interface bandwidth of zero\n");
		dev_warn(&interface->pdev->dev,
			 "request logical port map failed: %d\n",
			 hw->swapi.status);

		return 0;
	}

	err = fm10k_msg_lport_map_pf(hw, results, mbx);
	if (err)
		return err;

	interface->lport_map_failed = false;

	/* we need to reset if port count was just updated */
	if (dglort_map != hw->mac.dglort_map)
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	return 0;
}

static s32 fm10k_update_pvid(struct fm10k_hw *hw, u32 **results,
			     struct fm10k_mbx_info __always_unused *mbx)
{
	struct fm10k_intfc *interface;
	u16 glort, pvid;
	u32 pvid_update;
	s32 err;

	err = fm10k_tlv_attr_get_u32(results[FM10K_PF_ATTR_ID_UPDATE_PVID],
				     &pvid_update);
	if (err)
		return err;

	/* extract values from the pvid update */
	glort = FM10K_MSG_HDR_FIELD_GET(pvid_update, UPDATE_PVID_GLORT);
	pvid = FM10K_MSG_HDR_FIELD_GET(pvid_update, UPDATE_PVID_PVID);

	/* if glort is not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort))
		return FM10K_ERR_PARAM;

	/* verify VLAN ID is valid */
	if (pvid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	interface = container_of(hw, struct fm10k_intfc, hw);

	/* check to see if this belongs to one of the VFs */
	err = fm10k_iov_update_pvid(interface, glort, pvid);
	if (!err)
		return 0;

	/* we need to reset if default VLAN was just updated */
	if (pvid != hw->mac.default_vid)
		set_bit(FM10K_FLAG_RESET_REQUESTED, interface->flags);

	hw->mac.default_vid = pvid;

	return 0;
}

static const struct fm10k_msg_data pf_mbx_data[] = {
	FM10K_PF_MSG_ERR_HANDLER(XCAST_MODES, fm10k_msg_err_pf),
	FM10K_PF_MSG_ERR_HANDLER(UPDATE_MAC_FWD_RULE, fm10k_msg_err_pf),
	FM10K_PF_MSG_LPORT_MAP_HANDLER(fm10k_lport_map),
	FM10K_PF_MSG_ERR_HANDLER(LPORT_CREATE, fm10k_msg_err_pf),
	FM10K_PF_MSG_ERR_HANDLER(LPORT_DELETE, fm10k_msg_err_pf),
	FM10K_PF_MSG_UPDATE_PVID_HANDLER(fm10k_update_pvid),
	FM10K_TLV_MSG_ERROR_HANDLER(fm10k_mbx_error),
};

static int fm10k_mbx_request_irq_pf(struct fm10k_intfc *interface)
{
	struct msix_entry *entry = &interface->msix_entries[FM10K_MBX_VECTOR];
	struct net_device *dev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int err;

	/* Use timer0 for interrupt moderation on the mailbox */
	u32 mbx_itr = entry->entry | FM10K_INT_MAP_TIMER0;
	u32 other_itr = entry->entry | FM10K_INT_MAP_IMMEDIATE;

	/* register mailbox handlers */
	err = hw->mbx.ops.register_handlers(&hw->mbx, pf_mbx_data);
	if (err)
		return err;

	/* request the IRQ */
	err = request_irq(entry->vector, fm10k_msix_mbx_pf, 0,
			  dev->name, interface);
	if (err) {
		netif_err(interface, probe, dev,
			  "request_irq for msix_mbx failed: %d\n", err);
		return err;
	}

	/* Enable interrupts w/ no moderation for "other" interrupts */
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_pcie_fault), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_switch_up_down), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_sram), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_max_hold_time), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_vflr), other_itr);

	/* Enable interrupts w/ moderation for mailbox */
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_mailbox), mbx_itr);

	/* Enable individual interrupt causes */
	fm10k_write_reg(hw, FM10K_EIMR, FM10K_EIMR_ENABLE(PCA_FAULT) |
					FM10K_EIMR_ENABLE(FUM_FAULT) |
					FM10K_EIMR_ENABLE(MAILBOX) |
					FM10K_EIMR_ENABLE(SWITCHREADY) |
					FM10K_EIMR_ENABLE(SWITCHNOTREADY) |
					FM10K_EIMR_ENABLE(SRAMERROR) |
					FM10K_EIMR_ENABLE(VFLR) |
					FM10K_EIMR_ENABLE(MAXHOLDTIME));

	/* enable interrupt */
	fm10k_write_reg(hw, FM10K_ITR(entry->entry), FM10K_ITR_ENABLE);

	return 0;
}

int fm10k_mbx_request_irq(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	int err;

	/* enable Mailbox cause */
	if (hw->mac.type == fm10k_mac_pf)
		err = fm10k_mbx_request_irq_pf(interface);
	else
		err = fm10k_mbx_request_irq_vf(interface);
	if (err)
		return err;

	/* connect mailbox */
	err = hw->mbx.ops.connect(hw, &hw->mbx);

	/* if the mailbox failed to connect, then free IRQ */
	if (err)
		fm10k_mbx_free_irq(interface);

	return err;
}

/**
 * fm10k_qv_free_irq - release interrupts associated with queue vectors
 * @interface: board private structure
 *
 * Release all interrupts associated with this interface
 **/
void fm10k_qv_free_irq(struct fm10k_intfc *interface)
{
	int vector = interface->num_q_vectors;
	struct fm10k_hw *hw = &interface->hw;
	struct msix_entry *entry;

	entry = &interface->msix_entries[NON_Q_VECTORS(hw) + vector];

	while (vector) {
		struct fm10k_q_vector *q_vector;

		vector--;
		entry--;
		q_vector = interface->q_vector[vector];

		if (!q_vector->tx.count && !q_vector->rx.count)
			continue;

		/* clear the affinity_mask in the IRQ descriptor */
		irq_set_affinity_hint(entry->vector, NULL);

		/* disable interrupts */
		writel(FM10K_ITR_MASK_SET, q_vector->itr);

		free_irq(entry->vector, q_vector);
	}
}

/**
 * fm10k_qv_request_irq - initialize interrupts for queue vectors
 * @interface: board private structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
int fm10k_qv_request_irq(struct fm10k_intfc *interface)
{
	struct net_device *dev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	struct msix_entry *entry;
	unsigned int ri = 0, ti = 0;
	int vector, err;

	entry = &interface->msix_entries[NON_Q_VECTORS(hw)];

	for (vector = 0; vector < interface->num_q_vectors; vector++) {
		struct fm10k_q_vector *q_vector = interface->q_vector[vector];

		/* name the vector */
		if (q_vector->tx.count && q_vector->rx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name),
				 "%s-TxRx-%u", dev->name, ri++);
			ti++;
		} else if (q_vector->rx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name),
				 "%s-rx-%u", dev->name, ri++);
		} else if (q_vector->tx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name),
				 "%s-tx-%u", dev->name, ti++);
		} else {
			/* skip this unused q_vector */
			continue;
		}

		/* Assign ITR register to q_vector */
		q_vector->itr = (hw->mac.type == fm10k_mac_pf) ?
				&interface->uc_addr[FM10K_ITR(entry->entry)] :
				&interface->uc_addr[FM10K_VFITR(entry->entry)];

		/* request the IRQ */
		err = request_irq(entry->vector, &fm10k_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			netif_err(interface, probe, dev,
				  "request_irq failed for MSIX interrupt Error: %d\n",
				  err);
			goto err_out;
		}

		/* assign the mask for this irq */
		irq_set_affinity_hint(entry->vector, &q_vector->affinity_mask);

		/* Enable q_vector */
		writel(FM10K_ITR_ENABLE, q_vector->itr);

		entry++;
	}

	return 0;

err_out:
	/* wind through the ring freeing all entries and vectors */
	while (vector) {
		struct fm10k_q_vector *q_vector;

		entry--;
		vector--;
		q_vector = interface->q_vector[vector];

		if (!q_vector->tx.count && !q_vector->rx.count)
			continue;

		/* clear the affinity_mask in the IRQ descriptor */
		irq_set_affinity_hint(entry->vector, NULL);

		/* disable interrupts */
		writel(FM10K_ITR_MASK_SET, q_vector->itr);

		free_irq(entry->vector, q_vector);
	}

	return err;
}

void fm10k_up(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;

	/* Enable Tx/Rx DMA */
	hw->mac.ops.start_hw(hw);

	/* configure Tx descriptor rings */
	fm10k_configure_tx(interface);

	/* configure Rx descriptor rings */
	fm10k_configure_rx(interface);

	/* configure interrupts */
	hw->mac.ops.update_int_moderator(hw);

	/* enable statistics capture again */
	clear_bit(__FM10K_UPDATING_STATS, interface->state);

	/* clear down bit to indicate we are ready to go */
	clear_bit(__FM10K_DOWN, interface->state);

	/* enable polling cleanups */
	fm10k_napi_enable_all(interface);

	/* re-establish Rx filters */
	fm10k_restore_rx_state(interface);

	/* enable transmits */
	netif_tx_start_all_queues(interface->netdev);

	/* kick off the service timer now */
	hw->mac.get_host_state = true;
	mod_timer(&interface->service_timer, jiffies);
}

static void fm10k_napi_disable_all(struct fm10k_intfc *interface)
{
	struct fm10k_q_vector *q_vector;
	int q_idx;

	for (q_idx = 0; q_idx < interface->num_q_vectors; q_idx++) {
		q_vector = interface->q_vector[q_idx];
		napi_disable(&q_vector->napi);
	}
}

void fm10k_down(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int err, i = 0, count = 0;

	/* signal that we are down to the interrupt handler and service task */
	if (test_and_set_bit(__FM10K_DOWN, interface->state))
		return;

	/* call carrier off first to avoid false dev_watchdog timeouts */
	netif_carrier_off(netdev);

	/* disable transmits */
	netif_tx_stop_all_queues(netdev);
	netif_tx_disable(netdev);

	/* reset Rx filters */
	fm10k_reset_rx_state(interface);

	/* disable polling routines */
	fm10k_napi_disable_all(interface);

	/* capture stats one last time before stopping interface */
	fm10k_update_stats(interface);

	/* prevent updating statistics while we're down */
	while (test_and_set_bit(__FM10K_UPDATING_STATS, interface->state))
		usleep_range(1000, 2000);

	/* skip waiting for TX DMA if we lost PCIe link */
	if (FM10K_REMOVED(hw->hw_addr))
		goto skip_tx_dma_drain;

	/* In some rare circumstances it can take a while for Tx queues to
	 * quiesce and be fully disabled. Attempt to .stop_hw() first, and
	 * then if we get ERR_REQUESTS_PENDING, go ahead and wait in a loop
	 * until the Tx queues have emptied, or until a number of retries. If
	 * we fail to clear within the retry loop, we will issue a warning
	 * indicating that Tx DMA is probably hung. Note this means we call
	 * .stop_hw() twice but this shouldn't cause any problems.
	 */
	err = hw->mac.ops.stop_hw(hw);
	if (err != FM10K_ERR_REQUESTS_PENDING)
		goto skip_tx_dma_drain;

#define TX_DMA_DRAIN_RETRIES 25
	for (count = 0; count < TX_DMA_DRAIN_RETRIES; count++) {
		usleep_range(10000, 20000);

		/* start checking at the last ring to have pending Tx */
		for (; i < interface->num_tx_queues; i++)
			if (fm10k_get_tx_pending(interface->tx_ring[i], false))
				break;

		/* if all the queues are drained, we can break now */
		if (i == interface->num_tx_queues)
			break;
	}

	if (count >= TX_DMA_DRAIN_RETRIES)
		dev_err(&interface->pdev->dev,
			"Tx queues failed to drain after %d tries. Tx DMA is probably hung.\n",
			count);
skip_tx_dma_drain:
	/* Disable DMA engine for Tx/Rx */
	err = hw->mac.ops.stop_hw(hw);
	if (err == FM10K_ERR_REQUESTS_PENDING)
		dev_err(&interface->pdev->dev,
			"due to pending requests hw was not shut down gracefully\n");
	else if (err)
		dev_err(&interface->pdev->dev, "stop_hw failed: %d\n", err);

	/* free any buffers still on the rings */
	fm10k_clean_all_tx_rings(interface);
	fm10k_clean_all_rx_rings(interface);
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
	const struct fm10k_info *fi = fm10k_info_tbl[ent->driver_data];
	struct fm10k_hw *hw = &interface->hw;
	struct pci_dev *pdev = interface->pdev;
	struct net_device *netdev = interface->netdev;
	u32 rss_key[FM10K_RSSRK_SIZE];
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

	/* Setup IOV handlers */
	if (fi->iov_ops)
		memcpy(&hw->iov.ops, fi->iov_ops, sizeof(hw->iov.ops));

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

	/* delay any future reset requests */
	interface->last_reset = jiffies + (10 * HZ);

	/* reset and initialize the hardware so it is in a known state */
	err = hw->mac.ops.reset_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "reset_hw failed: %d\n", err);
		return err;
	}

	err = hw->mac.ops.init_hw(hw);
	if (err) {
		dev_err(&pdev->dev, "init_hw failed: %d\n", err);
		return err;
	}

	/* initialize hardware statistics */
	hw->mac.ops.update_hw_stats(hw, &interface->stats);

	/* Set upper limit on IOV VFs that can be allocated */
	pci_sriov_set_totalvfs(pdev, hw->iov.total_vfs);

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

	ether_addr_copy(netdev->dev_addr, hw->mac.addr);
	ether_addr_copy(netdev->perm_addr, hw->mac.addr);

	if (!is_valid_ether_addr(netdev->perm_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		return -EIO;
	}

	/* initialize DCBNL interface */
	fm10k_dcbnl_set_ops(netdev);

	/* set default ring sizes */
	interface->tx_ring_count = FM10K_DEFAULT_TXD;
	interface->rx_ring_count = FM10K_DEFAULT_RXD;

	/* set default interrupt moderation */
	interface->tx_itr = FM10K_TX_ITR_DEFAULT;
	interface->rx_itr = FM10K_ITR_ADAPTIVE | FM10K_RX_ITR_DEFAULT;

	/* initialize udp port lists */
	INIT_LIST_HEAD(&interface->vxlan_port);
	INIT_LIST_HEAD(&interface->geneve_port);

	netdev_rss_key_fill(rss_key, sizeof(rss_key));
	memcpy(interface->rssrk, rss_key, sizeof(rss_key));

	/* Start off interface as being down */
	set_bit(__FM10K_DOWN, interface->state);
	set_bit(__FM10K_UPDATING_STATS, interface->state);

	return 0;
}

static void fm10k_slot_warn(struct fm10k_intfc *interface)
{
	enum pcie_link_width width = PCIE_LNK_WIDTH_UNKNOWN;
	enum pci_bus_speed speed = PCI_SPEED_UNKNOWN;
	struct fm10k_hw *hw = &interface->hw;
	int max_gts = 0, expected_gts = 0;

	if (pcie_get_minimum_link(interface->pdev, &speed, &width) ||
	    speed == PCI_SPEED_UNKNOWN || width == PCIE_LNK_WIDTH_UNKNOWN) {
		dev_warn(&interface->pdev->dev,
			 "Unable to determine PCI Express bandwidth.\n");
		return;
	}

	switch (speed) {
	case PCIE_SPEED_2_5GT:
		/* 8b/10b encoding reduces max throughput by 20% */
		max_gts = 2 * width;
		break;
	case PCIE_SPEED_5_0GT:
		/* 8b/10b encoding reduces max throughput by 20% */
		max_gts = 4 * width;
		break;
	case PCIE_SPEED_8_0GT:
		/* 128b/130b encoding has less than 2% impact on throughput */
		max_gts = 8 * width;
		break;
	default:
		dev_warn(&interface->pdev->dev,
			 "Unable to determine PCI Express bandwidth.\n");
		return;
	}

	dev_info(&interface->pdev->dev,
		 "PCI Express bandwidth of %dGT/s available\n",
		 max_gts);
	dev_info(&interface->pdev->dev,
		 "(Speed:%s, Width: x%d, Encoding Loss:%s, Payload:%s)\n",
		 (speed == PCIE_SPEED_8_0GT ? "8.0GT/s" :
		  speed == PCIE_SPEED_5_0GT ? "5.0GT/s" :
		  speed == PCIE_SPEED_2_5GT ? "2.5GT/s" :
		  "Unknown"),
		 hw->bus.width,
		 (speed == PCIE_SPEED_2_5GT ? "20%" :
		  speed == PCIE_SPEED_5_0GT ? "20%" :
		  speed == PCIE_SPEED_8_0GT ? "<2%" :
		  "Unknown"),
		 (hw->bus.payload == fm10k_bus_payload_128 ? "128B" :
		  hw->bus.payload == fm10k_bus_payload_256 ? "256B" :
		  hw->bus.payload == fm10k_bus_payload_512 ? "512B" :
		  "Unknown"));

	switch (hw->bus_caps.speed) {
	case fm10k_bus_speed_2500:
		/* 8b/10b encoding reduces max throughput by 20% */
		expected_gts = 2 * hw->bus_caps.width;
		break;
	case fm10k_bus_speed_5000:
		/* 8b/10b encoding reduces max throughput by 20% */
		expected_gts = 4 * hw->bus_caps.width;
		break;
	case fm10k_bus_speed_8000:
		/* 128b/130b encoding has less than 2% impact on throughput */
		expected_gts = 8 * hw->bus_caps.width;
		break;
	default:
		dev_warn(&interface->pdev->dev,
			 "Unable to determine expected PCI Express bandwidth.\n");
		return;
	}

	if (max_gts >= expected_gts)
		return;

	dev_warn(&interface->pdev->dev,
		 "This device requires %dGT/s of bandwidth for optimal performance.\n",
		 expected_gts);
	dev_warn(&interface->pdev->dev,
		 "A %sslot with x%d lanes is suggested.\n",
		 (hw->bus_caps.speed == fm10k_bus_speed_2500 ? "2.5GT/s " :
		  hw->bus_caps.speed == fm10k_bus_speed_5000 ? "5.0GT/s " :
		  hw->bus_caps.speed == fm10k_bus_speed_8000 ? "8.0GT/s " : ""),
		 hw->bus_caps.width);
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
static int fm10k_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct fm10k_intfc *interface;
	int err;

	if (pdev->error_state != pci_channel_io_normal) {
		dev_err(&pdev->dev,
			"PCI device still in an error state. Unable to load...\n");
		return -EIO;
	}

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"PCI enable device failed: %d\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev,
			"DMA configuration failed: %d\n", err);
		goto err_dma;
	}

	err = pci_request_mem_regions(pdev, fm10k_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed: %d\n", err);
		goto err_pci_reg;
	}

	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);
	pci_save_state(pdev);

	netdev = fm10k_alloc_netdev(fm10k_info_tbl[ent->driver_data]);
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	interface = netdev_priv(netdev);
	pci_set_drvdata(pdev, interface);

	interface->netdev = netdev;
	interface->pdev = pdev;

	interface->uc_addr = ioremap(pci_resource_start(pdev, 0),
				     FM10K_UC_ADDR_SIZE);
	if (!interface->uc_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	err = fm10k_sw_init(interface, ent);
	if (err)
		goto err_sw_init;

	/* enable debugfs support */
	fm10k_dbg_intfc_init(interface);

	err = fm10k_init_queueing_scheme(interface);
	if (err)
		goto err_sw_init;

	/* the mbx interrupt might attempt to schedule the service task, so we
	 * must ensure it is disabled since we haven't yet requested the timer
	 * or work item.
	 */
	set_bit(__FM10K_SERVICE_DISABLE, interface->state);

	err = fm10k_mbx_request_irq(interface);
	if (err)
		goto err_mbx_interrupt;

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

	/* Initialize service timer and service task late in order to avoid
	 * cleanup issues.
	 */
	setup_timer(&interface->service_timer, &fm10k_service_timer,
		    (unsigned long)interface);
	INIT_WORK(&interface->service_task, fm10k_service_task);

	/* kick off service timer now, even when interface is down */
	mod_timer(&interface->service_timer, (HZ * 2) + jiffies);

	/* print warning for non-optimal configurations */
	fm10k_slot_warn(interface);

	/* report MAC address for logging */
	dev_info(&pdev->dev, "%pM\n", netdev->dev_addr);

	/* enable SR-IOV after registering netdev to enforce PF/VF ordering */
	fm10k_iov_configure(pdev, 0);

	/* clear the service task disable bit to allow service task to start */
	clear_bit(__FM10K_SERVICE_DISABLE, interface->state);

	return 0;

err_register:
	fm10k_mbx_free_irq(interface);
err_mbx_interrupt:
	fm10k_clear_queueing_scheme(interface);
err_sw_init:
	if (interface->sw_addr)
		iounmap(interface->sw_addr);
	iounmap(interface->uc_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_netdev:
	pci_release_mem_regions(pdev);
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

	del_timer_sync(&interface->service_timer);

	set_bit(__FM10K_SERVICE_DISABLE, interface->state);
	cancel_work_sync(&interface->service_task);

	/* free netdev, this may bounce the interrupts due to setup_tc */
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	/* release VFs */
	fm10k_iov_disable(pdev);

	/* disable mailbox interrupt */
	fm10k_mbx_free_irq(interface);

	/* free interrupts */
	fm10k_clear_queueing_scheme(interface);

	/* remove any debugfs interfaces */
	fm10k_dbg_intfc_exit(interface);

	if (interface->sw_addr)
		iounmap(interface->sw_addr);
	iounmap(interface->uc_addr);

	free_netdev(netdev);

	pci_release_mem_regions(pdev);

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static void fm10k_prepare_suspend(struct fm10k_intfc *interface)
{
	/* the watchdog task reads from registers, which might appear like
	 * a surprise remove if the PCIe device is disabled while we're
	 * stopped. We stop the watchdog task until after we resume software
	 * activity.
	 */
	set_bit(__FM10K_SERVICE_DISABLE, interface->state);
	cancel_work_sync(&interface->service_task);

	fm10k_prepare_for_reset(interface);
}

static int fm10k_handle_resume(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;
	int err;

	/* reset statistics starting values */
	hw->mac.ops.rebind_hw_stats(hw, &interface->stats);

	err = fm10k_handle_reset(interface);
	if (err)
		return err;

	/* assume host is not ready, to prevent race with watchdog in case we
	 * actually don't have connection to the switch
	 */
	interface->host_ready = false;
	fm10k_watchdog_host_not_ready(interface);

	/* force link to stay down for a second to prevent link flutter */
	interface->link_down_event = jiffies + (HZ);
	set_bit(__FM10K_LINK_DOWN, interface->state);

	/* clear the service task disable bit to allow service task to start */
	clear_bit(__FM10K_SERVICE_DISABLE, interface->state);
	fm10k_service_event_schedule(interface);

	return err;
}

#ifdef CONFIG_PM
/**
 * fm10k_resume - Restore device to pre-sleep state
 * @pdev: PCI device information struct
 *
 * fm10k_resume is called after the system has powered back up from a sleep
 * state and is ready to resume operation.  This function is meant to restore
 * the device back to its pre-sleep state.
 **/
static int fm10k_resume(struct pci_dev *pdev)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	/* pci_restore_state clears dev->state_saved so call
	 * pci_save_state to restore it.
	 */
	pci_save_state(pdev);

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_wake_from_d3(pdev, false);

	/* refresh hw_addr in case it was dropped */
	hw->hw_addr = interface->uc_addr;

	err = fm10k_handle_resume(interface);
	if (err)
		return err;

	netif_device_attach(netdev);

	return 0;
}

/**
 * fm10k_suspend - Prepare the device for a system sleep state
 * @pdev: PCI device information struct
 *
 * fm10k_suspend is meant to shutdown the device prior to the system entering
 * a sleep state.  The fm10k hardware does not support wake on lan so the
 * driver simply needs to shut down the device so it is in a low power state.
 **/
static int fm10k_suspend(struct pci_dev *pdev,
			 pm_message_t __always_unused state)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	struct net_device *netdev = interface->netdev;
	int err = 0;

	netif_device_detach(netdev);

	fm10k_prepare_suspend(interface);

	err = pci_save_state(pdev);
	if (err)
		return err;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, false);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

#endif /* CONFIG_PM */
/**
 * fm10k_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t fm10k_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	struct net_device *netdev = interface->netdev;

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	fm10k_prepare_suspend(interface);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * fm10k_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t fm10k_io_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t result;

	if (pci_reenable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);

		/* After second error pci->state_saved is false, this
		 * resets it so EEH doesn't break.
		 */
		pci_save_state(pdev);

		pci_wake_from_d3(pdev, false);

		result = PCI_ERS_RESULT_RECOVERED;
	}

	pci_cleanup_aer_uncorrect_error_status(pdev);

	return result;
}

/**
 * fm10k_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void fm10k_io_resume(struct pci_dev *pdev)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	struct net_device *netdev = interface->netdev;
	int err;

	err = fm10k_handle_resume(interface);

	if (err)
		dev_warn(&pdev->dev,
			 "fm10k_io_resume failed: %d\n", err);
	else
		netif_device_attach(netdev);
}

static void fm10k_io_reset_prepare(struct pci_dev *pdev)
{
	/* warn incase we have any active VF devices */
	if (pci_num_vf(pdev))
		dev_warn(&pdev->dev,
			 "PCIe FLR may cause issues for any active VF devices\n");
	fm10k_prepare_suspend(pci_get_drvdata(pdev));
}

static void fm10k_io_reset_done(struct pci_dev *pdev)
{
	struct fm10k_intfc *interface = pci_get_drvdata(pdev);
	int err = fm10k_handle_resume(interface);

	if (err) {
		dev_warn(&pdev->dev,
			 "fm10k_io_reset_notify failed: %d\n", err);
		netif_device_detach(interface->netdev);
	}
}

static const struct pci_error_handlers fm10k_err_handler = {
	.error_detected = fm10k_io_error_detected,
	.slot_reset = fm10k_io_slot_reset,
	.resume = fm10k_io_resume,
	.reset_prepare = fm10k_io_reset_prepare,
	.reset_done = fm10k_io_reset_done,
};

static struct pci_driver fm10k_driver = {
	.name			= fm10k_driver_name,
	.id_table		= fm10k_pci_tbl,
	.probe			= fm10k_probe,
	.remove			= fm10k_remove,
#ifdef CONFIG_PM
	.suspend		= fm10k_suspend,
	.resume			= fm10k_resume,
#endif
	.sriov_configure	= fm10k_iov_configure,
	.err_handler		= &fm10k_err_handler
};

/**
 * fm10k_register_pci_driver - register driver interface
 *
 * This function is called on module load in order to register the driver.
 **/
int fm10k_register_pci_driver(void)
{
	return pci_register_driver(&fm10k_driver);
}

/**
 * fm10k_unregister_pci_driver - unregister driver interface
 *
 * This function is called on module unload in order to remove the driver.
 **/
void fm10k_unregister_pci_driver(void)
{
	pci_unregister_driver(&fm10k_driver);
}
