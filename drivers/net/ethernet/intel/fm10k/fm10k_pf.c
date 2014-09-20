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

#include "fm10k_pf.h"

/**
 *  fm10k_reset_hw_pf - PF hardware reset
 *  @hw: pointer to hardware structure
 *
 *  This function should return the hardware to a state similar to the
 *  one it is in after being powered on.
 **/
static s32 fm10k_reset_hw_pf(struct fm10k_hw *hw)
{
	s32 err;
	u32 reg;
	u16 i;

	/* Disable interrupts */
	fm10k_write_reg(hw, FM10K_EIMR, FM10K_EIMR_DISABLE(ALL));

	/* Lock ITR2 reg 0 into itself and disable interrupt moderation */
	fm10k_write_reg(hw, FM10K_ITR2(0), 0);
	fm10k_write_reg(hw, FM10K_INT_CTRL, 0);

	/* We assume here Tx and Rx queue 0 are owned by the PF */

	/* Shut off VF access to their queues forcing them to queue 0 */
	for (i = 0; i < FM10K_TQMAP_TABLE_SIZE; i++) {
		fm10k_write_reg(hw, FM10K_TQMAP(i), 0);
		fm10k_write_reg(hw, FM10K_RQMAP(i), 0);
	}

	/* shut down all rings */
	err = fm10k_disable_queues_generic(hw, FM10K_MAX_QUEUES);
	if (err)
		return err;

	/* Verify that DMA is no longer active */
	reg = fm10k_read_reg(hw, FM10K_DMA_CTRL);
	if (reg & (FM10K_DMA_CTRL_TX_ACTIVE | FM10K_DMA_CTRL_RX_ACTIVE))
		return FM10K_ERR_DMA_PENDING;

	/* Inititate data path reset */
	reg |= FM10K_DMA_CTRL_DATAPATH_RESET;
	fm10k_write_reg(hw, FM10K_DMA_CTRL, reg);

	/* Flush write and allow 100us for reset to complete */
	fm10k_write_flush(hw);
	udelay(FM10K_RESET_TIMEOUT);

	/* Verify we made it out of reset */
	reg = fm10k_read_reg(hw, FM10K_IP);
	if (!(reg & FM10K_IP_NOTINRESET))
		err = FM10K_ERR_RESET_FAILED;

	return err;
}

/**
 *  fm10k_init_hw_pf - PF hardware initialization
 *  @hw: pointer to hardware structure
 *
 **/
static s32 fm10k_init_hw_pf(struct fm10k_hw *hw)
{
	u32 dma_ctrl, txqctl;
	u16 i;

	/* Establish default VSI as valid */
	fm10k_write_reg(hw, FM10K_DGLORTDEC(fm10k_dglort_default), 0);
	fm10k_write_reg(hw, FM10K_DGLORTMAP(fm10k_dglort_default),
			FM10K_DGLORTMAP_ANY);

	/* Invalidate all other GLORT entries */
	for (i = 1; i < FM10K_DGLORT_COUNT; i++)
		fm10k_write_reg(hw, FM10K_DGLORTMAP(i), FM10K_DGLORTMAP_NONE);

	/* reset ITR2(0) to point to itself */
	fm10k_write_reg(hw, FM10K_ITR2(0), 0);

	/* reset VF ITR2(0) to point to 0 avoid PF registers */
	fm10k_write_reg(hw, FM10K_ITR2(FM10K_ITR_REG_COUNT_PF), 0);

	/* loop through all PF ITR2 registers pointing them to the previous */
	for (i = 1; i < FM10K_ITR_REG_COUNT_PF; i++)
		fm10k_write_reg(hw, FM10K_ITR2(i), i - 1);

	/* Enable interrupt moderator if not already enabled */
	fm10k_write_reg(hw, FM10K_INT_CTRL, FM10K_INT_CTRL_ENABLEMODERATOR);

	/* compute the default txqctl configuration */
	txqctl = FM10K_TXQCTL_PF | FM10K_TXQCTL_UNLIMITED_BW |
		 (hw->mac.default_vid << FM10K_TXQCTL_VID_SHIFT);

	for (i = 0; i < FM10K_MAX_QUEUES; i++) {
		/* configure rings for 256 Queue / 32 Descriptor cache mode */
		fm10k_write_reg(hw, FM10K_TQDLOC(i),
				(i * FM10K_TQDLOC_BASE_32_DESC) |
				FM10K_TQDLOC_SIZE_32_DESC);
		fm10k_write_reg(hw, FM10K_TXQCTL(i), txqctl);

		/* configure rings to provide TPH processing hints */
		fm10k_write_reg(hw, FM10K_TPH_TXCTRL(i),
				FM10K_TPH_TXCTRL_DESC_TPHEN |
				FM10K_TPH_TXCTRL_DESC_RROEN |
				FM10K_TPH_TXCTRL_DESC_WROEN |
				FM10K_TPH_TXCTRL_DATA_RROEN);
		fm10k_write_reg(hw, FM10K_TPH_RXCTRL(i),
				FM10K_TPH_RXCTRL_DESC_TPHEN |
				FM10K_TPH_RXCTRL_DESC_RROEN |
				FM10K_TPH_RXCTRL_DATA_WROEN |
				FM10K_TPH_RXCTRL_HDR_WROEN);
	}

	/* set max hold interval to align with 1.024 usec in all modes */
	switch (hw->bus.speed) {
	case fm10k_bus_speed_2500:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN1;
		break;
	case fm10k_bus_speed_5000:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN2;
		break;
	case fm10k_bus_speed_8000:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN3;
		break;
	default:
		dma_ctrl = 0;
		break;
	}

	/* Configure TSO flags */
	fm10k_write_reg(hw, FM10K_DTXTCPFLGL, FM10K_TSO_FLAGS_LOW);
	fm10k_write_reg(hw, FM10K_DTXTCPFLGH, FM10K_TSO_FLAGS_HI);

	/* Enable DMA engine
	 * Set Rx Descriptor size to 32
	 * Set Minimum MSS to 64
	 * Set Maximum number of Rx queues to 256 / 32 Descriptor
	 */
	dma_ctrl |= FM10K_DMA_CTRL_TX_ENABLE | FM10K_DMA_CTRL_RX_ENABLE |
		    FM10K_DMA_CTRL_RX_DESC_SIZE | FM10K_DMA_CTRL_MINMSS_64 |
		    FM10K_DMA_CTRL_32_DESC;

	fm10k_write_reg(hw, FM10K_DMA_CTRL, dma_ctrl);

	/* record maximum queue count, we limit ourselves to 128 */
	hw->mac.max_queues = FM10K_MAX_QUEUES_PF;

	return 0;
}

/**
 *  fm10k_is_slot_appropriate_pf - Indicate appropriate slot for this SKU
 *  @hw: pointer to hardware structure
 *
 *  Looks at the PCIe bus info to confirm whether or not this slot can support
 *  the necessary bandwidth for this device.
 **/
static bool fm10k_is_slot_appropriate_pf(struct fm10k_hw *hw)
{
	return (hw->bus.speed == hw->bus_caps.speed) &&
	       (hw->bus.width == hw->bus_caps.width);
}

/**
 *  fm10k_read_mac_addr_pf - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the SM_AREA and stores the value.
 **/
static s32 fm10k_read_mac_addr_pf(struct fm10k_hw *hw)
{
	u8 perm_addr[ETH_ALEN];
	u32 serial_num;
	int i;

	serial_num = fm10k_read_reg(hw, FM10K_SM_AREA(1));

	/* last byte should be all 1's */
	if ((~serial_num) << 24)
		return  FM10K_ERR_INVALID_MAC_ADDR;

	perm_addr[0] = (u8)(serial_num >> 24);
	perm_addr[1] = (u8)(serial_num >> 16);
	perm_addr[2] = (u8)(serial_num >> 8);

	serial_num = fm10k_read_reg(hw, FM10K_SM_AREA(0));

	/* first byte should be all 1's */
	if ((~serial_num) >> 24)
		return  FM10K_ERR_INVALID_MAC_ADDR;

	perm_addr[3] = (u8)(serial_num >> 16);
	perm_addr[4] = (u8)(serial_num >> 8);
	perm_addr[5] = (u8)(serial_num);

	for (i = 0; i < ETH_ALEN; i++) {
		hw->mac.perm_addr[i] = perm_addr[i];
		hw->mac.addr[i] = perm_addr[i];
	}

	return 0;
}

/**
 *  fm10k_update_stats_hw_pf - Updates hardware related statistics of PF
 *  @hw: pointer to hardware structure
 *  @stats: pointer to the stats structure to update
 *
 *  This function collects and aggregates global and per queue hardware
 *  statistics.
 **/
static void fm10k_update_hw_stats_pf(struct fm10k_hw *hw,
				     struct fm10k_hw_stats *stats)
{
	u32 timeout, ur, ca, um, xec, vlan_drop, loopback_drop, nodesc_drop;
	u32 id, id_prev;

	/* Use Tx queue 0 as a canary to detect a reset */
	id = fm10k_read_reg(hw, FM10K_TXQCTL(0));

	/* Read Global Statistics */
	do {
		timeout = fm10k_read_hw_stats_32b(hw, FM10K_STATS_TIMEOUT,
						  &stats->timeout);
		ur = fm10k_read_hw_stats_32b(hw, FM10K_STATS_UR, &stats->ur);
		ca = fm10k_read_hw_stats_32b(hw, FM10K_STATS_CA, &stats->ca);
		um = fm10k_read_hw_stats_32b(hw, FM10K_STATS_UM, &stats->um);
		xec = fm10k_read_hw_stats_32b(hw, FM10K_STATS_XEC, &stats->xec);
		vlan_drop = fm10k_read_hw_stats_32b(hw, FM10K_STATS_VLAN_DROP,
						    &stats->vlan_drop);
		loopback_drop = fm10k_read_hw_stats_32b(hw,
							FM10K_STATS_LOOPBACK_DROP,
						     &stats->loopback_drop);
		nodesc_drop = fm10k_read_hw_stats_32b(hw,
						      FM10K_STATS_NODESC_DROP,
						      &stats->nodesc_drop);

		/* if value has not changed then we have consistent data */
		id_prev = id;
		id = fm10k_read_reg(hw, FM10K_TXQCTL(0));
	} while ((id ^ id_prev) & FM10K_TXQCTL_ID_MASK);

	/* drop non-ID bits and set VALID ID bit */
	id &= FM10K_TXQCTL_ID_MASK;
	id |= FM10K_STAT_VALID;

	/* Update Global Statistics */
	if (stats->stats_idx == id) {
		stats->timeout.count += timeout;
		stats->ur.count += ur;
		stats->ca.count += ca;
		stats->um.count += um;
		stats->xec.count += xec;
		stats->vlan_drop.count += vlan_drop;
		stats->loopback_drop.count += loopback_drop;
		stats->nodesc_drop.count += nodesc_drop;
	}

	/* Update bases and record current PF id */
	fm10k_update_hw_base_32b(&stats->timeout, timeout);
	fm10k_update_hw_base_32b(&stats->ur, ur);
	fm10k_update_hw_base_32b(&stats->ca, ca);
	fm10k_update_hw_base_32b(&stats->um, um);
	fm10k_update_hw_base_32b(&stats->xec, xec);
	fm10k_update_hw_base_32b(&stats->vlan_drop, vlan_drop);
	fm10k_update_hw_base_32b(&stats->loopback_drop, loopback_drop);
	fm10k_update_hw_base_32b(&stats->nodesc_drop, nodesc_drop);
	stats->stats_idx = id;

	/* Update Queue Statistics */
	fm10k_update_hw_stats_q(hw, stats->q, 0, hw->mac.max_queues);
}

/**
 *  fm10k_rebind_hw_stats_pf - Resets base for hardware statistics of PF
 *  @hw: pointer to hardware structure
 *  @stats: pointer to the stats structure to update
 *
 *  This function resets the base for global and per queue hardware
 *  statistics.
 **/
static void fm10k_rebind_hw_stats_pf(struct fm10k_hw *hw,
				     struct fm10k_hw_stats *stats)
{
	/* Unbind Global Statistics */
	fm10k_unbind_hw_stats_32b(&stats->timeout);
	fm10k_unbind_hw_stats_32b(&stats->ur);
	fm10k_unbind_hw_stats_32b(&stats->ca);
	fm10k_unbind_hw_stats_32b(&stats->um);
	fm10k_unbind_hw_stats_32b(&stats->xec);
	fm10k_unbind_hw_stats_32b(&stats->vlan_drop);
	fm10k_unbind_hw_stats_32b(&stats->loopback_drop);
	fm10k_unbind_hw_stats_32b(&stats->nodesc_drop);

	/* Unbind Queue Statistics */
	fm10k_unbind_hw_stats_q(stats->q, 0, hw->mac.max_queues);

	/* Reinitialize bases for all stats */
	fm10k_update_hw_stats_pf(hw, stats);
}

/**
 *  fm10k_get_fault_pf - Record a fault in one of the interface units
 *  @hw: pointer to hardware structure
 *  @type: pointer to fault type register offset
 *  @fault: pointer to memory location to record the fault
 *
 *  Record the fault register contents to the fault data structure and
 *  clear the entry from the register.
 *
 *  Returns ERR_PARAM if invalid register is specified or no error is present.
 **/
static s32 fm10k_get_fault_pf(struct fm10k_hw *hw, int type,
			      struct fm10k_fault *fault)
{
	u32 func;

	/* verify the fault register is in range and is aligned */
	switch (type) {
	case FM10K_PCA_FAULT:
	case FM10K_THI_FAULT:
	case FM10K_FUM_FAULT:
		break;
	default:
		return FM10K_ERR_PARAM;
	}

	/* only service faults that are valid */
	func = fm10k_read_reg(hw, type + FM10K_FAULT_FUNC);
	if (!(func & FM10K_FAULT_FUNC_VALID))
		return FM10K_ERR_PARAM;

	/* read remaining fields */
	fault->address = fm10k_read_reg(hw, type + FM10K_FAULT_ADDR_HI);
	fault->address <<= 32;
	fault->address = fm10k_read_reg(hw, type + FM10K_FAULT_ADDR_LO);
	fault->specinfo = fm10k_read_reg(hw, type + FM10K_FAULT_SPECINFO);

	/* clear valid bit to allow for next error */
	fm10k_write_reg(hw, type + FM10K_FAULT_FUNC, FM10K_FAULT_FUNC_VALID);

	/* Record which function triggered the error */
	if (func & FM10K_FAULT_FUNC_PF)
		fault->func = 0;
	else
		fault->func = 1 + ((func & FM10K_FAULT_FUNC_VF_MASK) >>
				   FM10K_FAULT_FUNC_VF_SHIFT);

	/* record fault type */
	fault->type = func & FM10K_FAULT_FUNC_TYPE_MASK;

	return 0;
}

static struct fm10k_mac_ops mac_ops_pf = {
	.get_bus_info		= &fm10k_get_bus_info_generic,
	.reset_hw		= &fm10k_reset_hw_pf,
	.init_hw		= &fm10k_init_hw_pf,
	.start_hw		= &fm10k_start_hw_generic,
	.stop_hw		= &fm10k_stop_hw_generic,
	.is_slot_appropriate	= &fm10k_is_slot_appropriate_pf,
	.read_mac_addr		= &fm10k_read_mac_addr_pf,
	.update_hw_stats	= &fm10k_update_hw_stats_pf,
	.rebind_hw_stats	= &fm10k_rebind_hw_stats_pf,
	.get_fault		= &fm10k_get_fault_pf,
	.get_host_state		= &fm10k_get_host_state_generic,
};

struct fm10k_info fm10k_pf_info = {
	.mac		= fm10k_mac_pf,
	.get_invariants	= &fm10k_get_invariants_generic,
	.mac_ops	= &mac_ops_pf,
};
