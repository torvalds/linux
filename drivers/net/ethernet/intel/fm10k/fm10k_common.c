/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
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

#include "fm10k_common.h"

/**
 *  fm10k_get_bus_info_generic - Generic set PCI bus info
 *  @hw: pointer to hardware structure
 *
 *  Gets the PCI bus info (speed, width, type) then calls helper function to
 *  store this data within the fm10k_hw structure.
 **/
s32 fm10k_get_bus_info_generic(struct fm10k_hw *hw)
{
	u16 link_cap, link_status, device_cap, device_control;

	/* Get the maximum link width and speed from PCIe config space */
	link_cap = fm10k_read_pci_cfg_word(hw, FM10K_PCIE_LINK_CAP);

	switch (link_cap & FM10K_PCIE_LINK_WIDTH) {
	case FM10K_PCIE_LINK_WIDTH_1:
		hw->bus_caps.width = fm10k_bus_width_pcie_x1;
		break;
	case FM10K_PCIE_LINK_WIDTH_2:
		hw->bus_caps.width = fm10k_bus_width_pcie_x2;
		break;
	case FM10K_PCIE_LINK_WIDTH_4:
		hw->bus_caps.width = fm10k_bus_width_pcie_x4;
		break;
	case FM10K_PCIE_LINK_WIDTH_8:
		hw->bus_caps.width = fm10k_bus_width_pcie_x8;
		break;
	default:
		hw->bus_caps.width = fm10k_bus_width_unknown;
		break;
	}

	switch (link_cap & FM10K_PCIE_LINK_SPEED) {
	case FM10K_PCIE_LINK_SPEED_2500:
		hw->bus_caps.speed = fm10k_bus_speed_2500;
		break;
	case FM10K_PCIE_LINK_SPEED_5000:
		hw->bus_caps.speed = fm10k_bus_speed_5000;
		break;
	case FM10K_PCIE_LINK_SPEED_8000:
		hw->bus_caps.speed = fm10k_bus_speed_8000;
		break;
	default:
		hw->bus_caps.speed = fm10k_bus_speed_unknown;
		break;
	}

	/* Get the PCIe maximum payload size for the PCIe function */
	device_cap = fm10k_read_pci_cfg_word(hw, FM10K_PCIE_DEV_CAP);

	switch (device_cap & FM10K_PCIE_DEV_CAP_PAYLOAD) {
	case FM10K_PCIE_DEV_CAP_PAYLOAD_128:
		hw->bus_caps.payload = fm10k_bus_payload_128;
		break;
	case FM10K_PCIE_DEV_CAP_PAYLOAD_256:
		hw->bus_caps.payload = fm10k_bus_payload_256;
		break;
	case FM10K_PCIE_DEV_CAP_PAYLOAD_512:
		hw->bus_caps.payload = fm10k_bus_payload_512;
		break;
	default:
		hw->bus_caps.payload = fm10k_bus_payload_unknown;
		break;
	}

	/* Get the negotiated link width and speed from PCIe config space */
	link_status = fm10k_read_pci_cfg_word(hw, FM10K_PCIE_LINK_STATUS);

	switch (link_status & FM10K_PCIE_LINK_WIDTH) {
	case FM10K_PCIE_LINK_WIDTH_1:
		hw->bus.width = fm10k_bus_width_pcie_x1;
		break;
	case FM10K_PCIE_LINK_WIDTH_2:
		hw->bus.width = fm10k_bus_width_pcie_x2;
		break;
	case FM10K_PCIE_LINK_WIDTH_4:
		hw->bus.width = fm10k_bus_width_pcie_x4;
		break;
	case FM10K_PCIE_LINK_WIDTH_8:
		hw->bus.width = fm10k_bus_width_pcie_x8;
		break;
	default:
		hw->bus.width = fm10k_bus_width_unknown;
		break;
	}

	switch (link_status & FM10K_PCIE_LINK_SPEED) {
	case FM10K_PCIE_LINK_SPEED_2500:
		hw->bus.speed = fm10k_bus_speed_2500;
		break;
	case FM10K_PCIE_LINK_SPEED_5000:
		hw->bus.speed = fm10k_bus_speed_5000;
		break;
	case FM10K_PCIE_LINK_SPEED_8000:
		hw->bus.speed = fm10k_bus_speed_8000;
		break;
	default:
		hw->bus.speed = fm10k_bus_speed_unknown;
		break;
	}

	/* Get the negotiated PCIe maximum payload size for the PCIe function */
	device_control = fm10k_read_pci_cfg_word(hw, FM10K_PCIE_DEV_CTRL);

	switch (device_control & FM10K_PCIE_DEV_CTRL_PAYLOAD) {
	case FM10K_PCIE_DEV_CTRL_PAYLOAD_128:
		hw->bus.payload = fm10k_bus_payload_128;
		break;
	case FM10K_PCIE_DEV_CTRL_PAYLOAD_256:
		hw->bus.payload = fm10k_bus_payload_256;
		break;
	case FM10K_PCIE_DEV_CTRL_PAYLOAD_512:
		hw->bus.payload = fm10k_bus_payload_512;
		break;
	default:
		hw->bus.payload = fm10k_bus_payload_unknown;
		break;
	}

	return 0;
}

static u16 fm10k_get_pcie_msix_count_generic(struct fm10k_hw *hw)
{
	u16 msix_count;

	/* read in value from MSI-X capability register */
	msix_count = fm10k_read_pci_cfg_word(hw, FM10K_PCI_MSIX_MSG_CTRL);
	msix_count &= FM10K_PCI_MSIX_MSG_CTRL_TBL_SZ_MASK;

	/* MSI-X count is zero-based in HW */
	msix_count++;

	if (msix_count > FM10K_MAX_MSIX_VECTORS)
		msix_count = FM10K_MAX_MSIX_VECTORS;

	return msix_count;
}

/**
 *  fm10k_get_invariants_generic - Inits constant values
 *  @hw: pointer to the hardware structure
 *
 *  Initialize the common invariants for the device.
 **/
s32 fm10k_get_invariants_generic(struct fm10k_hw *hw)
{
	struct fm10k_mac_info *mac = &hw->mac;

	/* initialize GLORT state to avoid any false hits */
	mac->dglort_map = FM10K_DGLORTMAP_NONE;

	/* record maximum number of MSI-X vectors */
	mac->max_msix_vectors = fm10k_get_pcie_msix_count_generic(hw);

	return 0;
}

/**
 *  fm10k_start_hw_generic - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  This function sets the Tx ready flag to indicate that the Tx path has
 *  been initialized.
 **/
s32 fm10k_start_hw_generic(struct fm10k_hw *hw)
{
	/* set flag indicating we are beginning Tx */
	hw->mac.tx_ready = true;

	return 0;
}

/**
 *  fm10k_disable_queues_generic - Stop Tx/Rx queues
 *  @hw: pointer to hardware structure
 *  @q_cnt: number of queues to be disabled
 *
 **/
s32 fm10k_disable_queues_generic(struct fm10k_hw *hw, u16 q_cnt)
{
	u32 reg;
	u16 i, time;

	/* clear tx_ready to prevent any false hits for reset */
	hw->mac.tx_ready = false;

	if (FM10K_REMOVED(hw->hw_addr))
		return 0;

	/* clear the enable bit for all rings */
	for (i = 0; i < q_cnt; i++) {
		reg = fm10k_read_reg(hw, FM10K_TXDCTL(i));
		fm10k_write_reg(hw, FM10K_TXDCTL(i),
				reg & ~FM10K_TXDCTL_ENABLE);
		reg = fm10k_read_reg(hw, FM10K_RXQCTL(i));
		fm10k_write_reg(hw, FM10K_RXQCTL(i),
				reg & ~FM10K_RXQCTL_ENABLE);
	}

	fm10k_write_flush(hw);
	udelay(1);

	/* loop through all queues to verify that they are all disabled */
	for (i = 0, time = FM10K_QUEUE_DISABLE_TIMEOUT; time;) {
		/* if we are at end of rings all rings are disabled */
		if (i == q_cnt)
			return 0;

		/* if queue enables cleared, then move to next ring pair */
		reg = fm10k_read_reg(hw, FM10K_TXDCTL(i));
		if (!~reg || !(reg & FM10K_TXDCTL_ENABLE)) {
			reg = fm10k_read_reg(hw, FM10K_RXQCTL(i));
			if (!~reg || !(reg & FM10K_RXQCTL_ENABLE)) {
				i++;
				continue;
			}
		}

		/* decrement time and wait 1 usec */
		time--;
		if (time)
			udelay(1);
	}

	return FM10K_ERR_REQUESTS_PENDING;
}

/**
 *  fm10k_stop_hw_generic - Stop Tx/Rx units
 *  @hw: pointer to hardware structure
 *
 **/
s32 fm10k_stop_hw_generic(struct fm10k_hw *hw)
{
	return fm10k_disable_queues_generic(hw, hw->mac.max_queues);
}

/**
 *  fm10k_read_hw_stats_32b - Reads value of 32-bit registers
 *  @hw: pointer to the hardware structure
 *  @addr: address of register containing a 32-bit value
 *
 *  Function reads the content of the register and returns the delta
 *  between the base and the current value.
 *  **/
u32 fm10k_read_hw_stats_32b(struct fm10k_hw *hw, u32 addr,
			    struct fm10k_hw_stat *stat)
{
	u32 delta = fm10k_read_reg(hw, addr) - stat->base_l;

	if (FM10K_REMOVED(hw->hw_addr))
		stat->base_h = 0;

	return delta;
}

/**
 *  fm10k_read_hw_stats_48b - Reads value of 48-bit registers
 *  @hw: pointer to the hardware structure
 *  @addr: address of register containing the lower 32-bit value
 *
 *  Function reads the content of 2 registers, combined to represent a 48-bit
 *  statistical value. Extra processing is required to handle overflowing.
 *  Finally, a delta value is returned representing the difference between the
 *  values stored in registers and values stored in the statistic counters.
 *  **/
static u64 fm10k_read_hw_stats_48b(struct fm10k_hw *hw, u32 addr,
				   struct fm10k_hw_stat *stat)
{
	u32 count_l;
	u32 count_h;
	u32 count_tmp;
	u64 delta;

	count_h = fm10k_read_reg(hw, addr + 1);

	/* Check for overflow */
	do {
		count_tmp = count_h;
		count_l = fm10k_read_reg(hw, addr);
		count_h = fm10k_read_reg(hw, addr + 1);
	} while (count_h != count_tmp);

	delta = ((u64)(count_h - stat->base_h) << 32) + count_l;
	delta -= stat->base_l;

	return delta & FM10K_48_BIT_MASK;
}

/**
 *  fm10k_update_hw_base_48b - Updates 48-bit statistic base value
 *  @stat: pointer to the hardware statistic structure
 *  @delta: value to be updated into the hardware statistic structure
 *
 *  Function receives a value and determines if an update is required based on
 *  a delta calculation. Only the base value will be updated.
 **/
static void fm10k_update_hw_base_48b(struct fm10k_hw_stat *stat, u64 delta)
{
	if (!delta)
		return;

	/* update lower 32 bits */
	delta += stat->base_l;
	stat->base_l = (u32)delta;

	/* update upper 32 bits */
	stat->base_h += (u32)(delta >> 32);
}

/**
 *  fm10k_update_hw_stats_tx_q - Updates TX queue statistics counters
 *  @hw: pointer to the hardware structure
 *  @q: pointer to the ring of hardware statistics queue
 *  @idx: index pointing to the start of the ring iteration
 *
 *  Function updates the TX queue statistics counters that are related to the
 *  hardware.
 **/
static void fm10k_update_hw_stats_tx_q(struct fm10k_hw *hw,
				       struct fm10k_hw_stats_q *q,
				       u32 idx)
{
	u32 id_tx, id_tx_prev, tx_packets;
	u64 tx_bytes = 0;

	/* Retrieve TX Owner Data */
	id_tx = fm10k_read_reg(hw, FM10K_TXQCTL(idx));

	/* Process TX Ring */
	do {
		tx_packets = fm10k_read_hw_stats_32b(hw, FM10K_QPTC(idx),
						     &q->tx_packets);

		if (tx_packets)
			tx_bytes = fm10k_read_hw_stats_48b(hw,
							   FM10K_QBTC_L(idx),
							   &q->tx_bytes);

		/* Re-Check Owner Data */
		id_tx_prev = id_tx;
		id_tx = fm10k_read_reg(hw, FM10K_TXQCTL(idx));
	} while ((id_tx ^ id_tx_prev) & FM10K_TXQCTL_ID_MASK);

	/* drop non-ID bits and set VALID ID bit */
	id_tx &= FM10K_TXQCTL_ID_MASK;
	id_tx |= FM10K_STAT_VALID;

	/* update packet counts */
	if (q->tx_stats_idx == id_tx) {
		q->tx_packets.count += tx_packets;
		q->tx_bytes.count += tx_bytes;
	}

	/* update bases and record ID */
	fm10k_update_hw_base_32b(&q->tx_packets, tx_packets);
	fm10k_update_hw_base_48b(&q->tx_bytes, tx_bytes);

	q->tx_stats_idx = id_tx;
}

/**
 *  fm10k_update_hw_stats_rx_q - Updates RX queue statistics counters
 *  @hw: pointer to the hardware structure
 *  @q: pointer to the ring of hardware statistics queue
 *  @idx: index pointing to the start of the ring iteration
 *
 *  Function updates the RX queue statistics counters that are related to the
 *  hardware.
 **/
static void fm10k_update_hw_stats_rx_q(struct fm10k_hw *hw,
				       struct fm10k_hw_stats_q *q,
				       u32 idx)
{
	u32 id_rx, id_rx_prev, rx_packets, rx_drops;
	u64 rx_bytes = 0;

	/* Retrieve RX Owner Data */
	id_rx = fm10k_read_reg(hw, FM10K_RXQCTL(idx));

	/* Process RX Ring */
	do {
		rx_drops = fm10k_read_hw_stats_32b(hw, FM10K_QPRDC(idx),
						   &q->rx_drops);

		rx_packets = fm10k_read_hw_stats_32b(hw, FM10K_QPRC(idx),
						     &q->rx_packets);

		if (rx_packets)
			rx_bytes = fm10k_read_hw_stats_48b(hw,
							   FM10K_QBRC_L(idx),
							   &q->rx_bytes);

		/* Re-Check Owner Data */
		id_rx_prev = id_rx;
		id_rx = fm10k_read_reg(hw, FM10K_RXQCTL(idx));
	} while ((id_rx ^ id_rx_prev) & FM10K_RXQCTL_ID_MASK);

	/* drop non-ID bits and set VALID ID bit */
	id_rx &= FM10K_RXQCTL_ID_MASK;
	id_rx |= FM10K_STAT_VALID;

	/* update packet counts */
	if (q->rx_stats_idx == id_rx) {
		q->rx_drops.count += rx_drops;
		q->rx_packets.count += rx_packets;
		q->rx_bytes.count += rx_bytes;
	}

	/* update bases and record ID */
	fm10k_update_hw_base_32b(&q->rx_drops, rx_drops);
	fm10k_update_hw_base_32b(&q->rx_packets, rx_packets);
	fm10k_update_hw_base_48b(&q->rx_bytes, rx_bytes);

	q->rx_stats_idx = id_rx;
}

/**
 *  fm10k_update_hw_stats_q - Updates queue statistics counters
 *  @hw: pointer to the hardware structure
 *  @q: pointer to the ring of hardware statistics queue
 *  @idx: index pointing to the start of the ring iteration
 *  @count: number of queues to iterate over
 *
 *  Function updates the queue statistics counters that are related to the
 *  hardware.
 **/
void fm10k_update_hw_stats_q(struct fm10k_hw *hw, struct fm10k_hw_stats_q *q,
			     u32 idx, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++, idx++, q++) {
		fm10k_update_hw_stats_tx_q(hw, q, idx);
		fm10k_update_hw_stats_rx_q(hw, q, idx);
	}
}

/**
 *  fm10k_unbind_hw_stats_q - Unbind the queue counters from their queues
 *  @hw: pointer to the hardware structure
 *  @q: pointer to the ring of hardware statistics queue
 *  @idx: index pointing to the start of the ring iteration
 *  @count: number of queues to iterate over
 *
 *  Function invalidates the index values for the queues so any updates that
 *  may have happened are ignored and the base for the queue stats is reset.
 **/
void fm10k_unbind_hw_stats_q(struct fm10k_hw_stats_q *q, u32 idx, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++, idx++, q++) {
		q->rx_stats_idx = 0;
		q->tx_stats_idx = 0;
	}
}

/**
 *  fm10k_get_host_state_generic - Returns the state of the host
 *  @hw: pointer to hardware structure
 *  @host_ready: pointer to boolean value that will record host state
 *
 *  This function will check the health of the mailbox and Tx queue 0
 *  in order to determine if we should report that the link is up or not.
 **/
s32 fm10k_get_host_state_generic(struct fm10k_hw *hw, bool *host_ready)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	struct fm10k_mac_info *mac = &hw->mac;
	s32 ret_val = 0;
	u32 txdctl = fm10k_read_reg(hw, FM10K_TXDCTL(0));

	/* process upstream mailbox in case interrupts were disabled */
	mbx->ops.process(hw, mbx);

	/* If Tx is no longer enabled link should come down */
	if (!(~txdctl) || !(txdctl & FM10K_TXDCTL_ENABLE))
		mac->get_host_state = true;

	/* exit if not checking for link, or link cannot be changed */
	if (!mac->get_host_state || !(~txdctl))
		goto out;

	/* if we somehow dropped the Tx enable we should reset */
	if (mac->tx_ready && !(txdctl & FM10K_TXDCTL_ENABLE)) {
		ret_val = FM10K_ERR_RESET_REQUESTED;
		goto out;
	}

	/* if Mailbox timed out we should request reset */
	if (!mbx->timeout) {
		ret_val = FM10K_ERR_RESET_REQUESTED;
		goto out;
	}

	/* verify Mailbox is still valid */
	if (!mbx->ops.tx_ready(mbx, FM10K_VFMBX_MSG_MTU))
		goto out;

	/* interface cannot receive traffic without logical ports */
	if (mac->dglort_map == FM10K_DGLORTMAP_NONE) {
		if (mac->ops.request_lport_map)
			ret_val = mac->ops.request_lport_map(hw);

		goto out;
	}

	/* if we passed all the tests above then the switch is ready and we no
	 * longer need to check for link
	 */
	mac->get_host_state = false;

out:
	*host_ready = !mac->get_host_state;
	return ret_val;
}
