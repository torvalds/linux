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

void fm10k_service_event_schedule(struct fm10k_intfc *interface)
{
	if (!test_bit(__FM10K_SERVICE_DISABLE, &interface->state) &&
	    !test_and_set_bit(__FM10K_SERVICE_SCHED, &interface->state))
		schedule_work(&interface->service_task);
}

static void fm10k_service_event_complete(struct fm10k_intfc *interface)
{
	BUG_ON(!test_bit(__FM10K_SERVICE_SCHED, &interface->state));

	/* flush memory to make sure state is correct before next watchog */
	smp_mb__before_atomic();
	clear_bit(__FM10K_SERVICE_SCHED, &interface->state);
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

	/* do nothing if device is still present or hw_addr is set */
	if (netif_device_present(netdev) || interface->hw.hw_addr)
		return;

	rtnl_lock();

	if (netif_running(netdev))
		dev_close(netdev);

	rtnl_unlock();
}

static void fm10k_reinit(struct fm10k_intfc *interface)
{
	struct net_device *netdev = interface->netdev;
	struct fm10k_hw *hw = &interface->hw;
	int err;

	WARN_ON(in_interrupt());

	/* put off any impending NetWatchDogTimeout */
	netdev->trans_start = jiffies;

	while (test_and_set_bit(__FM10K_RESETTING, &interface->state))
		usleep_range(1000, 2000);

	rtnl_lock();

	if (netif_running(netdev))
		fm10k_close(netdev);

	fm10k_mbx_free_irq(interface);

	/* delay any future reset requests */
	interface->last_reset = jiffies + (10 * HZ);

	/* reset and initialize the hardware so it is in a known state */
	err = hw->mac.ops.reset_hw(hw) ? : hw->mac.ops.init_hw(hw);
	if (err)
		dev_err(&interface->pdev->dev, "init_hw failed: %d\n", err);

	/* reassociate interrupts */
	fm10k_mbx_request_irq(interface);

	if (netif_running(netdev))
		fm10k_open(netdev);

	rtnl_unlock();

	clear_bit(__FM10K_RESETTING, &interface->state);
}

static void fm10k_reset_subtask(struct fm10k_intfc *interface)
{
	if (!(interface->flags & FM10K_FLAG_RESET_REQUESTED))
		return;

	interface->flags &= ~FM10K_FLAG_RESET_REQUESTED;

	netdev_err(interface->netdev, "Reset interface\n");
	interface->tx_timeout_count++;

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
	interface->flags &= ~FM10K_FLAG_SWPRI_CONFIG;

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

	if (test_bit(__FM10K_LINK_DOWN, &interface->state)) {
		interface->host_ready = false;
		if (time_is_after_jiffies(interface->link_down_event))
			return;
		clear_bit(__FM10K_LINK_DOWN, &interface->state);
	}

	if (interface->flags & FM10K_FLAG_SWPRI_CONFIG) {
		if (rtnl_trylock()) {
			fm10k_configure_swpri_map(interface);
			rtnl_unlock();
		}
	}

	/* lock the mailbox for transmit and receive */
	fm10k_mbx_lock(interface);

	err = hw->mac.ops.get_host_state(hw, &interface->host_ready);
	if (err && time_is_before_jiffies(interface->last_reset))
		interface->flags |= FM10K_FLAG_RESET_REQUESTED;

	/* free the lock */
	fm10k_mbx_unlock(interface);
}

/**
 * fm10k_mbx_subtask - Process upstream and downstream mailboxes
 * @interface: board private structure
 *
 * This function will process both the upstream and downstream mailboxes.
 * It is necessary for us to hold the rtnl_lock while doing this as the
 * mailbox accesses are protected by this lock.
 **/
static void fm10k_mbx_subtask(struct fm10k_intfc *interface)
{
	/* process upstream mailbox and update device state */
	fm10k_watchdog_update_host_state(interface);
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
	u64 rx_errors = 0, rx_csum_errors = 0, tx_csum_errors = 0;
	u64 restart_queue = 0, tx_busy = 0, alloc_failed = 0;
	u64 rx_bytes_nic = 0, rx_pkts_nic = 0, rx_drops_nic = 0;
	u64 tx_bytes_nic = 0, tx_pkts_nic = 0;
	u64 bytes, pkts;
	int i;

	/* do not allow stats update via service task for next second */
	interface->next_stats_update = jiffies + HZ;

	/* gather some stats to the interface struct that are per queue */
	for (bytes = 0, pkts = 0, i = 0; i < interface->num_tx_queues; i++) {
		struct fm10k_ring *tx_ring = interface->tx_ring[i];

		restart_queue += tx_ring->tx_stats.restart_queue;
		tx_busy += tx_ring->tx_stats.tx_busy;
		tx_csum_errors += tx_ring->tx_stats.csum_err;
		bytes += tx_ring->stats.bytes;
		pkts += tx_ring->stats.packets;
	}

	interface->restart_queue = restart_queue;
	interface->tx_busy = tx_busy;
	net_stats->tx_bytes = bytes;
	net_stats->tx_packets = pkts;
	interface->tx_csum_errors = tx_csum_errors;
	/* gather some stats to the interface struct that are per queue */
	for (bytes = 0, pkts = 0, i = 0; i < interface->num_rx_queues; i++) {
		struct fm10k_ring *rx_ring = interface->rx_ring[i];

		bytes += rx_ring->stats.bytes;
		pkts += rx_ring->stats.packets;
		alloc_failed += rx_ring->rx_stats.alloc_failed;
		rx_csum_errors += rx_ring->rx_stats.csum_err;
		rx_errors += rx_ring->rx_stats.errors;
	}

	net_stats->rx_bytes = bytes;
	net_stats->rx_packets = pkts;
	interface->alloc_failed = alloc_failed;
	interface->rx_csum_errors = rx_csum_errors;
	interface->rx_errors = rx_errors;

	hw->mac.ops.update_hw_stats(hw, &interface->stats);

	for (i = 0; i < FM10K_MAX_QUEUES_PF; i++) {
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
	net_stats->rx_errors = interface->stats.xec.count;
	net_stats->rx_dropped = interface->stats.nodesc_drop.count;
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
		interface->flags |= FM10K_FLAG_RESET_REQUESTED;
}

/**
 * fm10k_watchdog_subtask - check and bring link up
 * @interface - pointer to the device interface structure
 **/
static void fm10k_watchdog_subtask(struct fm10k_intfc *interface)
{
	/* if interface is down do nothing */
	if (test_bit(__FM10K_DOWN, &interface->state) ||
	    test_bit(__FM10K_RESETTING, &interface->state))
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
	if (test_bit(__FM10K_DOWN, &interface->state) ||
	    test_bit(__FM10K_RESETTING, &interface->state))
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

	/* tasks always capable of running, but must be rtnl protected */
	fm10k_mbx_subtask(interface);
	fm10k_detach_subtask(interface);
	fm10k_reset_subtask(interface);

	/* tasks only run when interface is up */
	fm10k_watchdog_subtask(interface);
	fm10k_check_hang_subtask(interface);

	/* release lock on service events to allow scheduling next event */
	fm10k_service_event_complete(interface);
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

static irqreturn_t fm10k_msix_clean_rings(int irq, void *data)
{
	struct fm10k_q_vector *q_vector = data;

	if (q_vector->rx.count || q_vector->tx.count)
		napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

#define FM10K_ERR_MSG(type) case (type): error = #type; break
static void fm10k_print_fault(struct fm10k_intfc *interface, int type,
			      struct fm10k_fault *fault)
{
	struct pci_dev *pdev = interface->pdev;
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

		fm10k_print_fault(interface, type, &fault);
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
		if (maxholdq & (1 << 31)) {
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

static irqreturn_t fm10k_msix_mbx_pf(int irq, void *data)
{
	struct fm10k_intfc *interface = data;
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 eicr;

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
		mbx->ops.process(hw, mbx);
		fm10k_mbx_unlock(interface);
	}

	/* if switch toggled state we should reset GLORTs */
	if (eicr & FM10K_EICR_SWITCHNOTREADY) {
		/* force link down for at least 4 seconds */
		interface->link_down_event = jiffies + (4 * HZ);
		set_bit(__FM10K_LINK_DOWN, &interface->state);

		/* reset dglort_map back to no config */
		hw->mac.dglort_map = FM10K_DGLORTMAP_NONE;
	}

	/* we should validate host state after interrupt event */
	hw->mac.get_host_state = 1;
	fm10k_service_event_schedule(interface);

	/* re-enable mailbox interrupt and indicate 20us delay */
	fm10k_write_reg(hw, FM10K_ITR(FM10K_MBX_VECTOR),
			FM10K_ITR_ENABLE | FM10K_MBX_INT_DELAY);

	return IRQ_HANDLED;
}

void fm10k_mbx_free_irq(struct fm10k_intfc *interface)
{
	struct msix_entry *entry = &interface->msix_entries[FM10K_MBX_VECTOR];
	struct fm10k_hw *hw = &interface->hw;
	int itr_reg;

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
	}

	fm10k_write_reg(hw, itr_reg, FM10K_ITR_MASK_SET);

	free_irq(entry->vector, interface);
}

/* generic error handler for mailbox issues */
static s32 fm10k_mbx_error(struct fm10k_hw *hw, u32 **results,
			   struct fm10k_mbx_info *mbx)
{
	struct fm10k_intfc *interface;
	struct pci_dev *pdev;

	interface = container_of(hw, struct fm10k_intfc, hw);
	pdev = interface->pdev;

	dev_err(&pdev->dev, "Unknown message ID %u\n",
		**results & FM10K_TLV_ID_MASK);

	return 0;
}

static s32 fm10k_lport_map(struct fm10k_hw *hw, u32 **results,
			   struct fm10k_mbx_info *mbx)
{
	struct fm10k_intfc *interface;
	u32 dglort_map = hw->mac.dglort_map;
	s32 err;

	err = fm10k_msg_lport_map_pf(hw, results, mbx);
	if (err)
		return err;

	interface = container_of(hw, struct fm10k_intfc, hw);

	/* we need to reset if port count was just updated */
	if (dglort_map != hw->mac.dglort_map)
		interface->flags |= FM10K_FLAG_RESET_REQUESTED;

	return 0;
}

static s32 fm10k_update_pvid(struct fm10k_hw *hw, u32 **results,
			     struct fm10k_mbx_info *mbx)
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

	/* verify VID is valid */
	if (pvid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	interface = container_of(hw, struct fm10k_intfc, hw);

	/* we need to reset if default VLAN was just updated */
	if (pvid != hw->mac.default_vid)
		interface->flags |= FM10K_FLAG_RESET_REQUESTED;

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
	u32 mbx_itr = FM10K_INT_MAP_TIMER0 | entry->entry;
	u32 other_itr = FM10K_INT_MAP_IMMEDIATE | entry->entry;

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
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_PCIeFault), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_SwitchUpDown), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_SRAM), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_MaxHoldTime), other_itr);
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_VFLR), other_itr);

	/* Enable interrupts w/ moderation for mailbox */
	fm10k_write_reg(hw, FM10K_INT_MAP(fm10k_int_Mailbox), mbx_itr);

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
	err = fm10k_mbx_request_irq_pf(interface);

	/* connect mailbox */
	if (!err)
		err = hw->mbx.ops.connect(hw, &hw->mbx);

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
	int ri = 0, ti = 0;
	int vector, err;

	entry = &interface->msix_entries[NON_Q_VECTORS(hw)];

	for (vector = 0; vector < interface->num_q_vectors; vector++) {
		struct fm10k_q_vector *q_vector = interface->q_vector[vector];

		/* name the vector */
		if (q_vector->tx.count && q_vector->rx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-TxRx-%d", dev->name, ri++);
			ti++;
		} else if (q_vector->rx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-rx-%d", dev->name, ri++);
		} else if (q_vector->tx.count) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-tx-%d", dev->name, ti++);
		} else {
			/* skip this unused q_vector */
			continue;
		}

		/* Assign ITR register to q_vector */
		q_vector->itr = &interface->uc_addr[FM10K_ITR(entry->entry)];

		/* request the IRQ */
		err = request_irq(entry->vector, &fm10k_msix_clean_rings, 0,
				  q_vector->name, q_vector);
		if (err) {
			netif_err(interface, probe, dev,
				  "request_irq failed for MSIX interrupt Error: %d\n",
				  err);
			goto err_out;
		}

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

	/* configure interrupts */
	hw->mac.ops.update_int_moderator(hw);

	/* clear down bit to indicate we are ready to go */
	clear_bit(__FM10K_DOWN, &interface->state);

	/* enable polling cleanups */
	fm10k_napi_enable_all(interface);

	/* re-establish Rx filters */
	fm10k_restore_rx_state(interface);

	/* enable transmits */
	netif_tx_start_all_queues(interface->netdev);

	/* kick off the service timer */
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

	/* disable polling routines */
	fm10k_napi_disable_all(interface);

	del_timer_sync(&interface->service_timer);

	/* capture stats one last time before stopping interface */
	fm10k_update_stats(interface);

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

	/* delay any future reset requests */
	interface->last_reset = jiffies + (10 * HZ);

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

	/* Initialize service timer and service task */
	set_bit(__FM10K_SERVICE_DISABLE, &interface->state);
	setup_timer(&interface->service_timer, &fm10k_service_timer,
		    (unsigned long)interface);
	INIT_WORK(&interface->service_task, fm10k_service_task);

	/* set default ring sizes */
	interface->tx_ring_count = FM10K_DEFAULT_TXD;
	interface->rx_ring_count = FM10K_DEFAULT_RXD;

	/* set default interrupt moderation */
	interface->tx_itr = FM10K_ITR_10K;
	interface->rx_itr = FM10K_ITR_ADAPTIVE | FM10K_ITR_20K;

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

	err = fm10k_init_queueing_scheme(interface);
	if (err)
		goto err_sw_init;

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

	/* clear the service task disable bit to allow service task to start */
	clear_bit(__FM10K_SERVICE_DISABLE, &interface->state);

	return 0;

err_register:
	fm10k_mbx_free_irq(interface);
err_mbx_interrupt:
	fm10k_clear_queueing_scheme(interface);
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

	set_bit(__FM10K_SERVICE_DISABLE, &interface->state);
	cancel_work_sync(&interface->service_task);

	/* free netdev, this may bounce the interrupts due to setup_tc */
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	/* disable mailbox interrupt */
	fm10k_mbx_free_irq(interface);

	/* free interrupts */
	fm10k_clear_queueing_scheme(interface);

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
