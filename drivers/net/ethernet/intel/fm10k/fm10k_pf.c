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
 *  fm10k_update_vlan_pf - Update status of VLAN ID in VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vid: VLAN ID to add to table
 *  @vsi: Index indicating VF ID or PF ID in table
 *  @set: Indicates if this is a set or clear operation
 *
 *  This function adds or removes the corresponding VLAN ID from the VLAN
 *  filter table for the corresponding function.  In addition to the
 *  standard set/clear that supports one bit a multi-bit write is
 *  supported to set 64 bits at a time.
 **/
static s32 fm10k_update_vlan_pf(struct fm10k_hw *hw, u32 vid, u8 vsi, bool set)
{
	u32 vlan_table, reg, mask, bit, len;

	/* verify the VSI index is valid */
	if (vsi > FM10K_VLAN_TABLE_VSI_MAX)
		return FM10K_ERR_PARAM;

	/* VLAN multi-bit write:
	 * The multi-bit write has several parts to it.
	 *    3			  2		      1			  0
	 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * | RSVD0 |         Length        |C|RSVD0|        VLAN ID        |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *
	 * VLAN ID: Vlan Starting value
	 * RSVD0: Reserved section, must be 0
	 * C: Flag field, 0 is set, 1 is clear (Used in VF VLAN message)
	 * Length: Number of times to repeat the bit being set
	 */
	len = vid >> 16;
	vid = (vid << 17) >> 17;

	/* verify the reserved 0 fields are 0 */
	if (len >= FM10K_VLAN_TABLE_VID_MAX ||
	    vid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	/* Loop through the table updating all required VLANs */
	for (reg = FM10K_VLAN_TABLE(vsi, vid / 32), bit = vid % 32;
	     len < FM10K_VLAN_TABLE_VID_MAX;
	     len -= 32 - bit, reg++, bit = 0) {
		/* record the initial state of the register */
		vlan_table = fm10k_read_reg(hw, reg);

		/* truncate mask if we are at the start or end of the run */
		mask = (~(u32)0 >> ((len < 31) ? 31 - len : 0)) << bit;

		/* make necessary modifications to the register */
		mask &= set ? ~vlan_table : vlan_table;
		if (mask)
			fm10k_write_reg(hw, reg, vlan_table ^ mask);
	}

	return 0;
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
 *  fm10k_glort_valid_pf - Validate that the provided glort is valid
 *  @hw: pointer to the HW structure
 *  @glort: base glort to be validated
 *
 *  This function will return an error if the provided glort is invalid
 **/
bool fm10k_glort_valid_pf(struct fm10k_hw *hw, u16 glort)
{
	glort &= hw->mac.dglort_map >> FM10K_DGLORTMAP_MASK_SHIFT;

	return glort == (hw->mac.dglort_map & FM10K_DGLORTMAP_NONE);
}

/**
 *  fm10k_update_uc_addr_pf - Update device unicast addresss
 *  @hw: pointer to the HW structure
 *  @glort: base resource tag for this request
 *  @mac: MAC address to add/remove from table
 *  @vid: VLAN ID to add/remove from table
 *  @add: Indicates if this is an add or remove operation
 *  @flags: flags field to indicate add and secure
 *
 *  This function generates a message to the Switch API requesting
 *  that the given logical port add/remove the given L2 MAC/VLAN address.
 **/
static s32 fm10k_update_xc_addr_pf(struct fm10k_hw *hw, u16 glort,
				   const u8 *mac, u16 vid, bool add, u8 flags)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	struct fm10k_mac_update mac_update;
	u32 msg[5];

	/* if glort is not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort))
		return FM10K_ERR_PARAM;

	/* drop upper 4 bits of VLAN ID */
	vid = (vid << 4) >> 4;

	/* record fields */
	mac_update.mac_lower = cpu_to_le32(((u32)mac[2] << 24) |
						 ((u32)mac[3] << 16) |
						 ((u32)mac[4] << 8) |
						 ((u32)mac[5]));
	mac_update.mac_upper = cpu_to_le16(((u32)mac[0] << 8) |
						 ((u32)mac[1]));
	mac_update.vlan = cpu_to_le16(vid);
	mac_update.glort = cpu_to_le16(glort);
	mac_update.action = add ? 0 : 1;
	mac_update.flags = flags;

	/* populate mac_update fields */
	fm10k_tlv_msg_init(msg, FM10K_PF_MSG_ID_UPDATE_MAC_FWD_RULE);
	fm10k_tlv_attr_put_le_struct(msg, FM10K_PF_ATTR_ID_MAC_UPDATE,
				     &mac_update, sizeof(mac_update));

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_update_uc_addr_pf - Update device unicast addresss
 *  @hw: pointer to the HW structure
 *  @glort: base resource tag for this request
 *  @mac: MAC address to add/remove from table
 *  @vid: VLAN ID to add/remove from table
 *  @add: Indicates if this is an add or remove operation
 *  @flags: flags field to indicate add and secure
 *
 *  This function is used to add or remove unicast addresses for
 *  the PF.
 **/
static s32 fm10k_update_uc_addr_pf(struct fm10k_hw *hw, u16 glort,
				   const u8 *mac, u16 vid, bool add, u8 flags)
{
	/* verify MAC address is valid */
	if (!is_valid_ether_addr(mac))
		return FM10K_ERR_PARAM;

	return fm10k_update_xc_addr_pf(hw, glort, mac, vid, add, flags);
}

/**
 *  fm10k_update_mc_addr_pf - Update device multicast addresses
 *  @hw: pointer to the HW structure
 *  @glort: base resource tag for this request
 *  @mac: MAC address to add/remove from table
 *  @vid: VLAN ID to add/remove from table
 *  @add: Indicates if this is an add or remove operation
 *
 *  This function is used to add or remove multicast MAC addresses for
 *  the PF.
 **/
static s32 fm10k_update_mc_addr_pf(struct fm10k_hw *hw, u16 glort,
				   const u8 *mac, u16 vid, bool add)
{
	/* verify multicast address is valid */
	if (!is_multicast_ether_addr(mac))
		return FM10K_ERR_PARAM;

	return fm10k_update_xc_addr_pf(hw, glort, mac, vid, add, 0);
}

/**
 *  fm10k_update_xcast_mode_pf - Request update of multicast mode
 *  @hw: pointer to hardware structure
 *  @glort: base resource tag for this request
 *  @mode: integer value indicating mode being requested
 *
 *  This function will attempt to request a higher mode for the port
 *  so that it can enable either multicast, multicast promiscuous, or
 *  promiscuous mode of operation.
 **/
static s32 fm10k_update_xcast_mode_pf(struct fm10k_hw *hw, u16 glort, u8 mode)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[3], xcast_mode;

	if (mode > FM10K_XCAST_MODE_NONE)
		return FM10K_ERR_PARAM;
	/* if glort is not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort))
		return FM10K_ERR_PARAM;

	/* write xcast mode as a single u32 value,
	 * lower 16 bits: glort
	 * upper 16 bits: mode
	 */
	xcast_mode = ((u32)mode << 16) | glort;

	/* generate message requesting to change xcast mode */
	fm10k_tlv_msg_init(msg, FM10K_PF_MSG_ID_XCAST_MODES);
	fm10k_tlv_attr_put_u32(msg, FM10K_PF_ATTR_ID_XCAST_MODE, xcast_mode);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_update_int_moderator_pf - Update interrupt moderator linked list
 *  @hw: pointer to hardware structure
 *
 *  This function walks through the MSI-X vector table to determine the
 *  number of active interrupts and based on that information updates the
 *  interrupt moderator linked list.
 **/
static void fm10k_update_int_moderator_pf(struct fm10k_hw *hw)
{
	u32 i;

	/* Disable interrupt moderator */
	fm10k_write_reg(hw, FM10K_INT_CTRL, 0);

	/* loop through PF from last to first looking enabled vectors */
	for (i = FM10K_ITR_REG_COUNT_PF - 1; i; i--) {
		if (!fm10k_read_reg(hw, FM10K_MSIX_VECTOR_MASK(i)))
			break;
	}

	/* always reset VFITR2[0] to point to last enabled PF vector*/
	fm10k_write_reg(hw, FM10K_ITR2(FM10K_ITR_REG_COUNT_PF), i);

	/* reset ITR2[0] to point to last enabled PF vector */
	fm10k_write_reg(hw, FM10K_ITR2(0), i);

	/* Enable interrupt moderator */
	fm10k_write_reg(hw, FM10K_INT_CTRL, FM10K_INT_CTRL_ENABLEMODERATOR);
}

/**
 *  fm10k_update_lport_state_pf - Notify the switch of a change in port state
 *  @hw: pointer to the HW structure
 *  @glort: base resource tag for this request
 *  @count: number of logical ports being updated
 *  @enable: boolean value indicating enable or disable
 *
 *  This function is used to add/remove a logical port from the switch.
 **/
static s32 fm10k_update_lport_state_pf(struct fm10k_hw *hw, u16 glort,
				       u16 count, bool enable)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[3], lport_msg;

	/* do nothing if we are being asked to create or destroy 0 ports */
	if (!count)
		return 0;

	/* if glort is not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort))
		return FM10K_ERR_PARAM;

	/* construct the lport message from the 2 pieces of data we have */
	lport_msg = ((u32)count << 16) | glort;

	/* generate lport create/delete message */
	fm10k_tlv_msg_init(msg, enable ? FM10K_PF_MSG_ID_LPORT_CREATE :
					 FM10K_PF_MSG_ID_LPORT_DELETE);
	fm10k_tlv_attr_put_u32(msg, FM10K_PF_ATTR_ID_PORT, lport_msg);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_configure_dglort_map_pf - Configures GLORT entry and queues
 *  @hw: pointer to hardware structure
 *  @dglort: pointer to dglort configuration structure
 *
 *  Reads the configuration structure contained in dglort_cfg and uses
 *  that information to then populate a DGLORTMAP/DEC entry and the queues
 *  to which it has been assigned.
 **/
static s32 fm10k_configure_dglort_map_pf(struct fm10k_hw *hw,
					 struct fm10k_dglort_cfg *dglort)
{
	u16 glort, queue_count, vsi_count, pc_count;
	u16 vsi, queue, pc, q_idx;
	u32 txqctl, dglortdec, dglortmap;

	/* verify the dglort pointer */
	if (!dglort)
		return FM10K_ERR_PARAM;

	/* verify the dglort values */
	if ((dglort->idx > 7) || (dglort->rss_l > 7) || (dglort->pc_l > 3) ||
	    (dglort->vsi_l > 6) || (dglort->vsi_b > 64) ||
	    (dglort->queue_l > 8) || (dglort->queue_b >= 256))
		return FM10K_ERR_PARAM;

	/* determine count of VSIs and queues */
	queue_count = 1 << (dglort->rss_l + dglort->pc_l);
	vsi_count = 1 << (dglort->vsi_l + dglort->queue_l);
	glort = dglort->glort;
	q_idx = dglort->queue_b;

	/* configure SGLORT for queues */
	for (vsi = 0; vsi < vsi_count; vsi++, glort++) {
		for (queue = 0; queue < queue_count; queue++, q_idx++) {
			if (q_idx >= FM10K_MAX_QUEUES)
				break;

			fm10k_write_reg(hw, FM10K_TX_SGLORT(q_idx), glort);
			fm10k_write_reg(hw, FM10K_RX_SGLORT(q_idx), glort);
		}
	}

	/* determine count of PCs and queues */
	queue_count = 1 << (dglort->queue_l + dglort->rss_l + dglort->vsi_l);
	pc_count = 1 << dglort->pc_l;

	/* configure PC for Tx queues */
	for (pc = 0; pc < pc_count; pc++) {
		q_idx = pc + dglort->queue_b;
		for (queue = 0; queue < queue_count; queue++) {
			if (q_idx >= FM10K_MAX_QUEUES)
				break;

			txqctl = fm10k_read_reg(hw, FM10K_TXQCTL(q_idx));
			txqctl &= ~FM10K_TXQCTL_PC_MASK;
			txqctl |= pc << FM10K_TXQCTL_PC_SHIFT;
			fm10k_write_reg(hw, FM10K_TXQCTL(q_idx), txqctl);

			q_idx += pc_count;
		}
	}

	/* configure DGLORTDEC */
	dglortdec = ((u32)(dglort->rss_l) << FM10K_DGLORTDEC_RSSLENGTH_SHIFT) |
		    ((u32)(dglort->queue_b) << FM10K_DGLORTDEC_QBASE_SHIFT) |
		    ((u32)(dglort->pc_l) << FM10K_DGLORTDEC_PCLENGTH_SHIFT) |
		    ((u32)(dglort->vsi_b) << FM10K_DGLORTDEC_VSIBASE_SHIFT) |
		    ((u32)(dglort->vsi_l) << FM10K_DGLORTDEC_VSILENGTH_SHIFT) |
		    ((u32)(dglort->queue_l));
	if (dglort->inner_rss)
		dglortdec |=  FM10K_DGLORTDEC_INNERRSS_ENABLE;

	/* configure DGLORTMAP */
	dglortmap = (dglort->idx == fm10k_dglort_default) ?
			FM10K_DGLORTMAP_ANY : FM10K_DGLORTMAP_ZERO;
	dglortmap <<= dglort->vsi_l + dglort->queue_l + dglort->shared_l;
	dglortmap |= dglort->glort;

	/* write values to hardware */
	fm10k_write_reg(hw, FM10K_DGLORTDEC(dglort->idx), dglortdec);
	fm10k_write_reg(hw, FM10K_DGLORTMAP(dglort->idx), dglortmap);

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
 *  fm10k_set_dma_mask_pf - Configures PhyAddrSpace to limit DMA to system
 *  @hw: pointer to hardware structure
 *  @dma_mask: 64 bit DMA mask required for platform
 *
 *  This function sets the PHYADDR.PhyAddrSpace bits for the endpoint in order
 *  to limit the access to memory beyond what is physically in the system.
 **/
static void fm10k_set_dma_mask_pf(struct fm10k_hw *hw, u64 dma_mask)
{
	/* we need to write the upper 32 bits of DMA mask to PhyAddrSpace */
	u32 phyaddr = (u32)(dma_mask >> 32);

	fm10k_write_reg(hw, FM10K_PHYADDR, phyaddr);
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

/**
 *  fm10k_request_lport_map_pf - Request LPORT map from the switch API
 *  @hw: pointer to hardware structure
 *
 **/
static s32 fm10k_request_lport_map_pf(struct fm10k_hw *hw)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[1];

	/* issue request asking for LPORT map */
	fm10k_tlv_msg_init(msg, FM10K_PF_MSG_ID_LPORT_MAP);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_get_host_state_pf - Returns the state of the switch and mailbox
 *  @hw: pointer to hardware structure
 *  @switch_ready: pointer to boolean value that will record switch state
 *
 *  This funciton will check the DMA_CTRL2 register and mailbox in order
 *  to determine if the switch is ready for the PF to begin requesting
 *  addresses and mapping traffic to the local interface.
 **/
static s32 fm10k_get_host_state_pf(struct fm10k_hw *hw, bool *switch_ready)
{
	s32 ret_val = 0;
	u32 dma_ctrl2;

	/* verify the switch is ready for interraction */
	dma_ctrl2 = fm10k_read_reg(hw, FM10K_DMA_CTRL2);
	if (!(dma_ctrl2 & FM10K_DMA_CTRL2_SWITCH_READY))
		goto out;

	/* retrieve generic host state info */
	ret_val = fm10k_get_host_state_generic(hw, switch_ready);
	if (ret_val)
		goto out;

	/* interface cannot receive traffic without logical ports */
	if (hw->mac.dglort_map == FM10K_DGLORTMAP_NONE)
		ret_val = fm10k_request_lport_map_pf(hw);

out:
	return ret_val;
}

/* This structure defines the attibutes to be parsed below */
const struct fm10k_tlv_attr fm10k_lport_map_msg_attr[] = {
	FM10K_TLV_ATTR_U32(FM10K_PF_ATTR_ID_LPORT_MAP),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_msg_lport_map_pf - Message handler for lport_map message from SM
 *  @hw: Pointer to hardware structure
 *  @results: pointer array containing parsed data
 *  @mbx: Pointer to mailbox information structure
 *
 *  This handler configures the lport mapping based on the reply from the
 *  switch API.
 **/
s32 fm10k_msg_lport_map_pf(struct fm10k_hw *hw, u32 **results,
			   struct fm10k_mbx_info *mbx)
{
	u16 glort, mask;
	u32 dglort_map;
	s32 err;

	err = fm10k_tlv_attr_get_u32(results[FM10K_PF_ATTR_ID_LPORT_MAP],
				     &dglort_map);
	if (err)
		return err;

	/* extract values out of the header */
	glort = FM10K_MSG_HDR_FIELD_GET(dglort_map, LPORT_MAP_GLORT);
	mask = FM10K_MSG_HDR_FIELD_GET(dglort_map, LPORT_MAP_MASK);

	/* verify mask is set and none of the masked bits in glort are set */
	if (!mask || (glort & ~mask))
		return FM10K_ERR_PARAM;

	/* verify the mask is contiguous, and that it is 1's followed by 0's */
	if (((~(mask - 1) & mask) + mask) & FM10K_DGLORTMAP_NONE)
		return FM10K_ERR_PARAM;

	/* record the glort, mask, and port count */
	hw->mac.dglort_map = dglort_map;

	return 0;
}

const struct fm10k_tlv_attr fm10k_update_pvid_msg_attr[] = {
	FM10K_TLV_ATTR_U32(FM10K_PF_ATTR_ID_UPDATE_PVID),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_msg_update_pvid_pf - Message handler for port VLAN message from SM
 *  @hw: Pointer to hardware structure
 *  @results: pointer array containing parsed data
 *  @mbx: Pointer to mailbox information structure
 *
 *  This handler configures the default VLAN for the PF
 **/
s32 fm10k_msg_update_pvid_pf(struct fm10k_hw *hw, u32 **results,
			     struct fm10k_mbx_info *mbx)
{
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

	/* record the port VLAN ID value */
	hw->mac.default_vid = pvid;

	return 0;
}

/**
 *  fm10k_record_global_table_data - Move global table data to swapi table info
 *  @from: pointer to source table data structure
 *  @to: pointer to destination table info structure
 *
 *  This function is will copy table_data to the table_info contained in
 *  the hw struct.
 **/
static void fm10k_record_global_table_data(struct fm10k_global_table_data *from,
					   struct fm10k_swapi_table_info *to)
{
	/* convert from le32 struct to CPU byte ordered values */
	to->used = le32_to_cpu(from->used);
	to->avail = le32_to_cpu(from->avail);
}

const struct fm10k_tlv_attr fm10k_err_msg_attr[] = {
	FM10K_TLV_ATTR_LE_STRUCT(FM10K_PF_ATTR_ID_ERR,
				 sizeof(struct fm10k_swapi_error)),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_msg_err_pf - Message handler for error reply
 *  @hw: Pointer to hardware structure
 *  @results: pointer array containing parsed data
 *  @mbx: Pointer to mailbox information structure
 *
 *  This handler will capture the data for any error replies to previous
 *  messages that the PF has sent.
 **/
s32 fm10k_msg_err_pf(struct fm10k_hw *hw, u32 **results,
		     struct fm10k_mbx_info *mbx)
{
	struct fm10k_swapi_error err_msg;
	s32 err;

	/* extract structure from message */
	err = fm10k_tlv_attr_get_le_struct(results[FM10K_PF_ATTR_ID_ERR],
					   &err_msg, sizeof(err_msg));
	if (err)
		return err;

	/* record table status */
	fm10k_record_global_table_data(&err_msg.mac, &hw->swapi.mac);
	fm10k_record_global_table_data(&err_msg.nexthop, &hw->swapi.nexthop);
	fm10k_record_global_table_data(&err_msg.ffu, &hw->swapi.ffu);

	/* record SW API status value */
	hw->swapi.status = le32_to_cpu(err_msg.status);

	return 0;
}

static const struct fm10k_msg_data fm10k_msg_data_pf[] = {
	FM10K_PF_MSG_ERR_HANDLER(XCAST_MODES, fm10k_msg_err_pf),
	FM10K_PF_MSG_ERR_HANDLER(UPDATE_MAC_FWD_RULE, fm10k_msg_err_pf),
	FM10K_PF_MSG_LPORT_MAP_HANDLER(fm10k_msg_lport_map_pf),
	FM10K_PF_MSG_ERR_HANDLER(LPORT_CREATE, fm10k_msg_err_pf),
	FM10K_PF_MSG_ERR_HANDLER(LPORT_DELETE, fm10k_msg_err_pf),
	FM10K_PF_MSG_UPDATE_PVID_HANDLER(fm10k_msg_update_pvid_pf),
	FM10K_TLV_MSG_ERROR_HANDLER(fm10k_tlv_msg_error),
};

static struct fm10k_mac_ops mac_ops_pf = {
	.get_bus_info		= &fm10k_get_bus_info_generic,
	.reset_hw		= &fm10k_reset_hw_pf,
	.init_hw		= &fm10k_init_hw_pf,
	.start_hw		= &fm10k_start_hw_generic,
	.stop_hw		= &fm10k_stop_hw_generic,
	.is_slot_appropriate	= &fm10k_is_slot_appropriate_pf,
	.update_vlan		= &fm10k_update_vlan_pf,
	.read_mac_addr		= &fm10k_read_mac_addr_pf,
	.update_uc_addr		= &fm10k_update_uc_addr_pf,
	.update_mc_addr		= &fm10k_update_mc_addr_pf,
	.update_xcast_mode	= &fm10k_update_xcast_mode_pf,
	.update_int_moderator	= &fm10k_update_int_moderator_pf,
	.update_lport_state	= &fm10k_update_lport_state_pf,
	.update_hw_stats	= &fm10k_update_hw_stats_pf,
	.rebind_hw_stats	= &fm10k_rebind_hw_stats_pf,
	.configure_dglort_map	= &fm10k_configure_dglort_map_pf,
	.set_dma_mask		= &fm10k_set_dma_mask_pf,
	.get_fault		= &fm10k_get_fault_pf,
	.get_host_state		= &fm10k_get_host_state_pf,
};

static s32 fm10k_get_invariants_pf(struct fm10k_hw *hw)
{
	fm10k_get_invariants_generic(hw);

	return fm10k_sm_mbx_init(hw, &hw->mbx, fm10k_msg_data_pf);
}

struct fm10k_info fm10k_pf_info = {
	.mac		= fm10k_mac_pf,
	.get_invariants	= &fm10k_get_invariants_pf,
	.mac_ops	= &mac_ops_pf,
};
