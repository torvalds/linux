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

#include "fm10k_pf.h"
#include "fm10k_vf.h"

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
	if (err == FM10K_ERR_REQUESTS_PENDING) {
		hw->mac.reset_while_pending++;
		goto force_reset;
	} else if (err) {
		return err;
	}

	/* Verify that DMA is no longer active */
	reg = fm10k_read_reg(hw, FM10K_DMA_CTRL);
	if (reg & (FM10K_DMA_CTRL_TX_ACTIVE | FM10K_DMA_CTRL_RX_ACTIVE))
		return FM10K_ERR_DMA_PENDING;

force_reset:
	/* Inititate data path reset */
	reg = FM10K_DMA_CTRL_DATAPATH_RESET;
	fm10k_write_reg(hw, FM10K_DMA_CTRL, reg);

	/* Flush write and allow 100us for reset to complete */
	fm10k_write_flush(hw);
	udelay(FM10K_RESET_TIMEOUT);

	/* Reset mailbox global interrupts */
	reg = FM10K_MBX_GLOBAL_REQ_INTERRUPT | FM10K_MBX_GLOBAL_ACK_INTERRUPT;
	fm10k_write_reg(hw, FM10K_GMBX, reg);

	/* Verify we made it out of reset */
	reg = fm10k_read_reg(hw, FM10K_IP);
	if (!(reg & FM10K_IP_NOTINRESET))
		return FM10K_ERR_RESET_FAILED;

	return 0;
}

/**
 *  fm10k_is_ari_hierarchy_pf - Indicate ARI hierarchy support
 *  @hw: pointer to hardware structure
 *
 *  Looks at the ARI hierarchy bit to determine whether ARI is supported or not.
 **/
static bool fm10k_is_ari_hierarchy_pf(struct fm10k_hw *hw)
{
	u16 sriov_ctrl = fm10k_read_pci_cfg_word(hw, FM10K_PCIE_SRIOV_CTRL);

	return !!(sriov_ctrl & FM10K_PCIE_SRIOV_CTRL_VFARI);
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

	/* set max hold interval to align with 1.024 usec in all modes and
	 * store ITR scale
	 */
	switch (hw->bus.speed) {
	case fm10k_bus_speed_2500:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN1;
		hw->mac.itr_scale = FM10K_TDLEN_ITR_SCALE_GEN1;
		break;
	case fm10k_bus_speed_5000:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN2;
		hw->mac.itr_scale = FM10K_TDLEN_ITR_SCALE_GEN2;
		break;
	case fm10k_bus_speed_8000:
		dma_ctrl = FM10K_DMA_CTRL_MAX_HOLD_1US_GEN3;
		hw->mac.itr_scale = FM10K_TDLEN_ITR_SCALE_GEN3;
		break;
	default:
		dma_ctrl = 0;
		/* just in case, assume Gen3 ITR scale */
		hw->mac.itr_scale = FM10K_TDLEN_ITR_SCALE_GEN3;
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

	/* We support either 64 VFs or 7 VFs depending on if we have ARI */
	hw->iov.total_vfs = fm10k_is_ari_hierarchy_pf(hw) ? 64 : 7;

	return 0;
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
	 *               24              16               8               0
	 *  7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0 7 6 5 4 3 2 1 0
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
	if (len >= FM10K_VLAN_TABLE_VID_MAX || vid >= FM10K_VLAN_TABLE_VID_MAX)
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

	ether_addr_copy(hw->mac.perm_addr, perm_addr);
	ether_addr_copy(hw->mac.addr, perm_addr);

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
 *  fm10k_update_xc_addr_pf - Update device addresses
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

	/* clear set bit from VLAN ID */
	vid &= ~FM10K_VLAN_CLEAR;

	/* if glort or VLAN are not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort) || vid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	/* record fields */
	mac_update.mac_lower = cpu_to_le32(((u32)mac[2] << 24) |
						 ((u32)mac[3] << 16) |
						 ((u32)mac[4] << 8) |
						 ((u32)mac[5]));
	mac_update.mac_upper = cpu_to_le16(((u16)mac[0] << 8) |
					   ((u16)mac[1]));
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
 *  fm10k_update_uc_addr_pf - Update device unicast addresses
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

	/* always reset VFITR2[0] to point to last enabled PF vector */
	fm10k_write_reg(hw, FM10K_ITR2(FM10K_ITR_REG_COUNT_PF), i);

	/* reset ITR2[0] to point to last enabled PF vector */
	if (!hw->iov.num_vfs)
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

	/* reset multicast mode if deleting lport */
	if (!enable)
		fm10k_update_xcast_mode_pf(hw, glort, FM10K_XCAST_MODE_NONE);

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
	queue_count = BIT(dglort->rss_l + dglort->pc_l);
	vsi_count = BIT(dglort->vsi_l + dglort->queue_l);
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
	queue_count = BIT(dglort->queue_l + dglort->rss_l + dglort->vsi_l);
	pc_count = BIT(dglort->pc_l);

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

u16 fm10k_queues_per_pool(struct fm10k_hw *hw)
{
	u16 num_pools = hw->iov.num_pools;

	return (num_pools > 32) ? 2 : (num_pools > 16) ? 4 : (num_pools > 8) ?
	       8 : FM10K_MAX_QUEUES_POOL;
}

u16 fm10k_vf_queue_index(struct fm10k_hw *hw, u16 vf_idx)
{
	u16 num_vfs = hw->iov.num_vfs;
	u16 vf_q_idx = FM10K_MAX_QUEUES;

	vf_q_idx -= fm10k_queues_per_pool(hw) * (num_vfs - vf_idx);

	return vf_q_idx;
}

static u16 fm10k_vectors_per_pool(struct fm10k_hw *hw)
{
	u16 num_pools = hw->iov.num_pools;

	return (num_pools > 32) ? 8 : (num_pools > 16) ? 16 :
	       FM10K_MAX_VECTORS_POOL;
}

static u16 fm10k_vf_vector_index(struct fm10k_hw *hw, u16 vf_idx)
{
	u16 vf_v_idx = FM10K_MAX_VECTORS_PF;

	vf_v_idx += fm10k_vectors_per_pool(hw) * vf_idx;

	return vf_v_idx;
}

/**
 *  fm10k_iov_assign_resources_pf - Assign pool resources for virtualization
 *  @hw: pointer to the HW structure
 *  @num_vfs: number of VFs to be allocated
 *  @num_pools: number of virtualization pools to be allocated
 *
 *  Allocates queues and traffic classes to virtualization entities to prepare
 *  the PF for SR-IOV and VMDq
 **/
static s32 fm10k_iov_assign_resources_pf(struct fm10k_hw *hw, u16 num_vfs,
					 u16 num_pools)
{
	u16 qmap_stride, qpp, vpp, vf_q_idx, vf_q_idx0, qmap_idx;
	u32 vid = hw->mac.default_vid << FM10K_TXQCTL_VID_SHIFT;
	int i, j;

	/* hardware only supports up to 64 pools */
	if (num_pools > 64)
		return FM10K_ERR_PARAM;

	/* the number of VFs cannot exceed the number of pools */
	if ((num_vfs > num_pools) || (num_vfs > hw->iov.total_vfs))
		return FM10K_ERR_PARAM;

	/* record number of virtualization entities */
	hw->iov.num_vfs = num_vfs;
	hw->iov.num_pools = num_pools;

	/* determine qmap offsets and counts */
	qmap_stride = (num_vfs > 8) ? 32 : 256;
	qpp = fm10k_queues_per_pool(hw);
	vpp = fm10k_vectors_per_pool(hw);

	/* calculate starting index for queues */
	vf_q_idx = fm10k_vf_queue_index(hw, 0);
	qmap_idx = 0;

	/* establish TCs with -1 credits and no quanta to prevent transmit */
	for (i = 0; i < num_vfs; i++) {
		fm10k_write_reg(hw, FM10K_TC_MAXCREDIT(i), 0);
		fm10k_write_reg(hw, FM10K_TC_RATE(i), 0);
		fm10k_write_reg(hw, FM10K_TC_CREDIT(i),
				FM10K_TC_CREDIT_CREDIT_MASK);
	}

	/* zero out all mbmem registers */
	for (i = FM10K_VFMBMEM_LEN * num_vfs; i--;)
		fm10k_write_reg(hw, FM10K_MBMEM(i), 0);

	/* clear event notification of VF FLR */
	fm10k_write_reg(hw, FM10K_PFVFLREC(0), ~0);
	fm10k_write_reg(hw, FM10K_PFVFLREC(1), ~0);

	/* loop through unallocated rings assigning them back to PF */
	for (i = FM10K_MAX_QUEUES_PF; i < vf_q_idx; i++) {
		fm10k_write_reg(hw, FM10K_TXDCTL(i), 0);
		fm10k_write_reg(hw, FM10K_TXQCTL(i), FM10K_TXQCTL_PF |
				FM10K_TXQCTL_UNLIMITED_BW | vid);
		fm10k_write_reg(hw, FM10K_RXQCTL(i), FM10K_RXQCTL_PF);
	}

	/* PF should have already updated VFITR2[0] */

	/* update all ITR registers to flow to VFITR2[0] */
	for (i = FM10K_ITR_REG_COUNT_PF + 1; i < FM10K_ITR_REG_COUNT; i++) {
		if (!(i & (vpp - 1)))
			fm10k_write_reg(hw, FM10K_ITR2(i), i - vpp);
		else
			fm10k_write_reg(hw, FM10K_ITR2(i), i - 1);
	}

	/* update PF ITR2[0] to reference the last vector */
	fm10k_write_reg(hw, FM10K_ITR2(0),
			fm10k_vf_vector_index(hw, num_vfs - 1));

	/* loop through rings populating rings and TCs */
	for (i = 0; i < num_vfs; i++) {
		/* record index for VF queue 0 for use in end of loop */
		vf_q_idx0 = vf_q_idx;

		for (j = 0; j < qpp; j++, qmap_idx++, vf_q_idx++) {
			/* assign VF and locked TC to queues */
			fm10k_write_reg(hw, FM10K_TXDCTL(vf_q_idx), 0);
			fm10k_write_reg(hw, FM10K_TXQCTL(vf_q_idx),
					(i << FM10K_TXQCTL_TC_SHIFT) | i |
					FM10K_TXQCTL_VF | vid);
			fm10k_write_reg(hw, FM10K_RXDCTL(vf_q_idx),
					FM10K_RXDCTL_WRITE_BACK_MIN_DELAY |
					FM10K_RXDCTL_DROP_ON_EMPTY);
			fm10k_write_reg(hw, FM10K_RXQCTL(vf_q_idx),
					(i << FM10K_RXQCTL_VF_SHIFT) |
					FM10K_RXQCTL_VF);

			/* map queue pair to VF */
			fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx), vf_q_idx);
			fm10k_write_reg(hw, FM10K_RQMAP(qmap_idx), vf_q_idx);
		}

		/* repeat the first ring for all of the remaining VF rings */
		for (; j < qmap_stride; j++, qmap_idx++) {
			fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx), vf_q_idx0);
			fm10k_write_reg(hw, FM10K_RQMAP(qmap_idx), vf_q_idx0);
		}
	}

	/* loop through remaining indexes assigning all to queue 0 */
	while (qmap_idx < FM10K_TQMAP_TABLE_SIZE) {
		fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx), 0);
		fm10k_write_reg(hw, FM10K_RQMAP(qmap_idx), 0);
		qmap_idx++;
	}

	return 0;
}

/**
 *  fm10k_iov_configure_tc_pf - Configure the shaping group for VF
 *  @hw: pointer to the HW structure
 *  @vf_idx: index of VF receiving GLORT
 *  @rate: Rate indicated in Mb/s
 *
 *  Configured the TC for a given VF to allow only up to a given number
 *  of Mb/s of outgoing Tx throughput.
 **/
static s32 fm10k_iov_configure_tc_pf(struct fm10k_hw *hw, u16 vf_idx, int rate)
{
	/* configure defaults */
	u32 interval = FM10K_TC_RATE_INTERVAL_4US_GEN3;
	u32 tc_rate = FM10K_TC_RATE_QUANTA_MASK;

	/* verify vf is in range */
	if (vf_idx >= hw->iov.num_vfs)
		return FM10K_ERR_PARAM;

	/* set interval to align with 4.096 usec in all modes */
	switch (hw->bus.speed) {
	case fm10k_bus_speed_2500:
		interval = FM10K_TC_RATE_INTERVAL_4US_GEN1;
		break;
	case fm10k_bus_speed_5000:
		interval = FM10K_TC_RATE_INTERVAL_4US_GEN2;
		break;
	default:
		break;
	}

	if (rate) {
		if (rate > FM10K_VF_TC_MAX || rate < FM10K_VF_TC_MIN)
			return FM10K_ERR_PARAM;

		/* The quanta is measured in Bytes per 4.096 or 8.192 usec
		 * The rate is provided in Mbits per second
		 * To tralslate from rate to quanta we need to multiply the
		 * rate by 8.192 usec and divide by 8 bits/byte.  To avoid
		 * dealing with floating point we can round the values up
		 * to the nearest whole number ratio which gives us 128 / 125.
		 */
		tc_rate = (rate * 128) / 125;

		/* try to keep the rate limiting accurate by increasing
		 * the number of credits and interval for rates less than 4Gb/s
		 */
		if (rate < 4000)
			interval <<= 1;
		else
			tc_rate >>= 1;
	}

	/* update rate limiter with new values */
	fm10k_write_reg(hw, FM10K_TC_RATE(vf_idx), tc_rate | interval);
	fm10k_write_reg(hw, FM10K_TC_MAXCREDIT(vf_idx), FM10K_TC_MAXCREDIT_64K);
	fm10k_write_reg(hw, FM10K_TC_CREDIT(vf_idx), FM10K_TC_MAXCREDIT_64K);

	return 0;
}

/**
 *  fm10k_iov_assign_int_moderator_pf - Add VF interrupts to moderator list
 *  @hw: pointer to the HW structure
 *  @vf_idx: index of VF receiving GLORT
 *
 *  Update the interrupt moderator linked list to include any MSI-X
 *  interrupts which the VF has enabled in the MSI-X vector table.
 **/
static s32 fm10k_iov_assign_int_moderator_pf(struct fm10k_hw *hw, u16 vf_idx)
{
	u16 vf_v_idx, vf_v_limit, i;

	/* verify vf is in range */
	if (vf_idx >= hw->iov.num_vfs)
		return FM10K_ERR_PARAM;

	/* determine vector offset and count */
	vf_v_idx = fm10k_vf_vector_index(hw, vf_idx);
	vf_v_limit = vf_v_idx + fm10k_vectors_per_pool(hw);

	/* search for first vector that is not masked */
	for (i = vf_v_limit - 1; i > vf_v_idx; i--) {
		if (!fm10k_read_reg(hw, FM10K_MSIX_VECTOR_MASK(i)))
			break;
	}

	/* reset linked list so it now includes our active vectors */
	if (vf_idx == (hw->iov.num_vfs - 1))
		fm10k_write_reg(hw, FM10K_ITR2(0), i);
	else
		fm10k_write_reg(hw, FM10K_ITR2(vf_v_limit), i);

	return 0;
}

/**
 *  fm10k_iov_assign_default_mac_vlan_pf - Assign a MAC and VLAN to VF
 *  @hw: pointer to the HW structure
 *  @vf_info: pointer to VF information structure
 *
 *  Assign a MAC address and default VLAN to a VF and notify it of the update
 **/
static s32 fm10k_iov_assign_default_mac_vlan_pf(struct fm10k_hw *hw,
						struct fm10k_vf_info *vf_info)
{
	u16 qmap_stride, queues_per_pool, vf_q_idx, timeout, qmap_idx, i;
	u32 msg[4], txdctl, txqctl, tdbal = 0, tdbah = 0;
	s32 err = 0;
	u16 vf_idx, vf_vid;

	/* verify vf is in range */
	if (!vf_info || vf_info->vf_idx >= hw->iov.num_vfs)
		return FM10K_ERR_PARAM;

	/* determine qmap offsets and counts */
	qmap_stride = (hw->iov.num_vfs > 8) ? 32 : 256;
	queues_per_pool = fm10k_queues_per_pool(hw);

	/* calculate starting index for queues */
	vf_idx = vf_info->vf_idx;
	vf_q_idx = fm10k_vf_queue_index(hw, vf_idx);
	qmap_idx = qmap_stride * vf_idx;

	/* Determine correct default VLAN ID. The FM10K_VLAN_OVERRIDE bit is
	 * used here to indicate to the VF that it will not have privilege to
	 * write VLAN_TABLE. All policy is enforced on the PF but this allows
	 * the VF to correctly report errors to userspace rqeuests.
	 */
	if (vf_info->pf_vid)
		vf_vid = vf_info->pf_vid | FM10K_VLAN_OVERRIDE;
	else
		vf_vid = vf_info->sw_vid;

	/* generate MAC_ADDR request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_MAC_VLAN);
	fm10k_tlv_attr_put_mac_vlan(msg, FM10K_MAC_VLAN_MSG_DEFAULT_MAC,
				    vf_info->mac, vf_vid);

	/* Configure Queue control register with new VLAN ID. The TXQCTL
	 * register is RO from the VF, so the PF must do this even in the
	 * case of notifying the VF of a new VID via the mailbox.
	 */
	txqctl = ((u32)vf_vid << FM10K_TXQCTL_VID_SHIFT) &
		 FM10K_TXQCTL_VID_MASK;
	txqctl |= (vf_idx << FM10K_TXQCTL_TC_SHIFT) |
		  FM10K_TXQCTL_VF | vf_idx;

	for (i = 0; i < queues_per_pool; i++)
		fm10k_write_reg(hw, FM10K_TXQCTL(vf_q_idx + i), txqctl);

	/* try loading a message onto outgoing mailbox first */
	if (vf_info->mbx.ops.enqueue_tx) {
		err = vf_info->mbx.ops.enqueue_tx(hw, &vf_info->mbx, msg);
		if (err != FM10K_MBX_ERR_NO_MBX)
			return err;
		err = 0;
	}

	/* If we aren't connected to a mailbox, this is most likely because
	 * the VF driver is not running. It should thus be safe to re-map
	 * queues and use the registers to pass the MAC address so that the VF
	 * driver gets correct information during its initialization.
	 */

	/* MAP Tx queue back to 0 temporarily, and disable it */
	fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx), 0);
	fm10k_write_reg(hw, FM10K_TXDCTL(vf_q_idx), 0);

	/* verify ring has disabled before modifying base address registers */
	txdctl = fm10k_read_reg(hw, FM10K_TXDCTL(vf_q_idx));
	for (timeout = 0; txdctl & FM10K_TXDCTL_ENABLE; timeout++) {
		/* limit ourselves to a 1ms timeout */
		if (timeout == 10) {
			err = FM10K_ERR_DMA_PENDING;
			goto err_out;
		}

		usleep_range(100, 200);
		txdctl = fm10k_read_reg(hw, FM10K_TXDCTL(vf_q_idx));
	}

	/* Update base address registers to contain MAC address */
	if (is_valid_ether_addr(vf_info->mac)) {
		tdbal = (((u32)vf_info->mac[3]) << 24) |
			(((u32)vf_info->mac[4]) << 16) |
			(((u32)vf_info->mac[5]) << 8);

		tdbah = (((u32)0xFF)	        << 24) |
			(((u32)vf_info->mac[0]) << 16) |
			(((u32)vf_info->mac[1]) << 8) |
			((u32)vf_info->mac[2]);
	}

	/* Record the base address into queue 0 */
	fm10k_write_reg(hw, FM10K_TDBAL(vf_q_idx), tdbal);
	fm10k_write_reg(hw, FM10K_TDBAH(vf_q_idx), tdbah);

	/* Provide the VF the ITR scale, using software-defined fields in TDLEN
	 * to pass the information during VF initialization. See definition of
	 * FM10K_TDLEN_ITR_SCALE_SHIFT for more details.
	 */
	fm10k_write_reg(hw, FM10K_TDLEN(vf_q_idx), hw->mac.itr_scale <<
						   FM10K_TDLEN_ITR_SCALE_SHIFT);

err_out:
	/* restore the queue back to VF ownership */
	fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx), vf_q_idx);
	return err;
}

/**
 *  fm10k_iov_reset_resources_pf - Reassign queues and interrupts to a VF
 *  @hw: pointer to the HW structure
 *  @vf_info: pointer to VF information structure
 *
 *  Reassign the interrupts and queues to a VF following an FLR
 **/
static s32 fm10k_iov_reset_resources_pf(struct fm10k_hw *hw,
					struct fm10k_vf_info *vf_info)
{
	u16 qmap_stride, queues_per_pool, vf_q_idx, qmap_idx;
	u32 tdbal = 0, tdbah = 0, txqctl, rxqctl;
	u16 vf_v_idx, vf_v_limit, vf_vid;
	u8 vf_idx = vf_info->vf_idx;
	int i;

	/* verify vf is in range */
	if (vf_idx >= hw->iov.num_vfs)
		return FM10K_ERR_PARAM;

	/* clear event notification of VF FLR */
	fm10k_write_reg(hw, FM10K_PFVFLREC(vf_idx / 32), BIT(vf_idx % 32));

	/* force timeout and then disconnect the mailbox */
	vf_info->mbx.timeout = 0;
	if (vf_info->mbx.ops.disconnect)
		vf_info->mbx.ops.disconnect(hw, &vf_info->mbx);

	/* determine vector offset and count */
	vf_v_idx = fm10k_vf_vector_index(hw, vf_idx);
	vf_v_limit = vf_v_idx + fm10k_vectors_per_pool(hw);

	/* determine qmap offsets and counts */
	qmap_stride = (hw->iov.num_vfs > 8) ? 32 : 256;
	queues_per_pool = fm10k_queues_per_pool(hw);
	qmap_idx = qmap_stride * vf_idx;

	/* make all the queues inaccessible to the VF */
	for (i = qmap_idx; i < (qmap_idx + qmap_stride); i++) {
		fm10k_write_reg(hw, FM10K_TQMAP(i), 0);
		fm10k_write_reg(hw, FM10K_RQMAP(i), 0);
	}

	/* calculate starting index for queues */
	vf_q_idx = fm10k_vf_queue_index(hw, vf_idx);

	/* determine correct default VLAN ID */
	if (vf_info->pf_vid)
		vf_vid = vf_info->pf_vid;
	else
		vf_vid = vf_info->sw_vid;

	/* configure Queue control register */
	txqctl = ((u32)vf_vid << FM10K_TXQCTL_VID_SHIFT) |
		 (vf_idx << FM10K_TXQCTL_TC_SHIFT) |
		 FM10K_TXQCTL_VF | vf_idx;
	rxqctl = (vf_idx << FM10K_RXQCTL_VF_SHIFT) | FM10K_RXQCTL_VF;

	/* stop further DMA and reset queue ownership back to VF */
	for (i = vf_q_idx; i < (queues_per_pool + vf_q_idx); i++) {
		fm10k_write_reg(hw, FM10K_TXDCTL(i), 0);
		fm10k_write_reg(hw, FM10K_TXQCTL(i), txqctl);
		fm10k_write_reg(hw, FM10K_RXDCTL(i),
				FM10K_RXDCTL_WRITE_BACK_MIN_DELAY |
				FM10K_RXDCTL_DROP_ON_EMPTY);
		fm10k_write_reg(hw, FM10K_RXQCTL(i), rxqctl);
	}

	/* reset TC with -1 credits and no quanta to prevent transmit */
	fm10k_write_reg(hw, FM10K_TC_MAXCREDIT(vf_idx), 0);
	fm10k_write_reg(hw, FM10K_TC_RATE(vf_idx), 0);
	fm10k_write_reg(hw, FM10K_TC_CREDIT(vf_idx),
			FM10K_TC_CREDIT_CREDIT_MASK);

	/* update our first entry in the table based on previous VF */
	if (!vf_idx)
		hw->mac.ops.update_int_moderator(hw);
	else
		hw->iov.ops.assign_int_moderator(hw, vf_idx - 1);

	/* reset linked list so it now includes our active vectors */
	if (vf_idx == (hw->iov.num_vfs - 1))
		fm10k_write_reg(hw, FM10K_ITR2(0), vf_v_idx);
	else
		fm10k_write_reg(hw, FM10K_ITR2(vf_v_limit), vf_v_idx);

	/* link remaining vectors so that next points to previous */
	for (vf_v_idx++; vf_v_idx < vf_v_limit; vf_v_idx++)
		fm10k_write_reg(hw, FM10K_ITR2(vf_v_idx), vf_v_idx - 1);

	/* zero out MBMEM, VLAN_TABLE, RETA, RSSRK, and MRQC registers */
	for (i = FM10K_VFMBMEM_LEN; i--;)
		fm10k_write_reg(hw, FM10K_MBMEM_VF(vf_idx, i), 0);
	for (i = FM10K_VLAN_TABLE_SIZE; i--;)
		fm10k_write_reg(hw, FM10K_VLAN_TABLE(vf_info->vsi, i), 0);
	for (i = FM10K_RETA_SIZE; i--;)
		fm10k_write_reg(hw, FM10K_RETA(vf_info->vsi, i), 0);
	for (i = FM10K_RSSRK_SIZE; i--;)
		fm10k_write_reg(hw, FM10K_RSSRK(vf_info->vsi, i), 0);
	fm10k_write_reg(hw, FM10K_MRQC(vf_info->vsi), 0);

	/* Update base address registers to contain MAC address */
	if (is_valid_ether_addr(vf_info->mac)) {
		tdbal = (((u32)vf_info->mac[3]) << 24) |
			(((u32)vf_info->mac[4]) << 16) |
			(((u32)vf_info->mac[5]) << 8);
		tdbah = (((u32)0xFF)	   << 24) |
			(((u32)vf_info->mac[0]) << 16) |
			(((u32)vf_info->mac[1]) << 8) |
			((u32)vf_info->mac[2]);
	}

	/* map queue pairs back to VF from last to first */
	for (i = queues_per_pool; i--;) {
		fm10k_write_reg(hw, FM10K_TDBAL(vf_q_idx + i), tdbal);
		fm10k_write_reg(hw, FM10K_TDBAH(vf_q_idx + i), tdbah);
		/* See definition of FM10K_TDLEN_ITR_SCALE_SHIFT for an
		 * explanation of how TDLEN is used.
		 */
		fm10k_write_reg(hw, FM10K_TDLEN(vf_q_idx + i),
				hw->mac.itr_scale <<
				FM10K_TDLEN_ITR_SCALE_SHIFT);
		fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx + i), vf_q_idx + i);
		fm10k_write_reg(hw, FM10K_RQMAP(qmap_idx + i), vf_q_idx + i);
	}

	/* repeat the first ring for all the remaining VF rings */
	for (i = queues_per_pool; i < qmap_stride; i++) {
		fm10k_write_reg(hw, FM10K_TQMAP(qmap_idx + i), vf_q_idx);
		fm10k_write_reg(hw, FM10K_RQMAP(qmap_idx + i), vf_q_idx);
	}

	return 0;
}

/**
 *  fm10k_iov_set_lport_pf - Assign and enable a logical port for a given VF
 *  @hw: pointer to hardware structure
 *  @vf_info: pointer to VF information structure
 *  @lport_idx: Logical port offset from the hardware glort
 *  @flags: Set of capability flags to extend port beyond basic functionality
 *
 *  This function allows enabling a VF port by assigning it a GLORT and
 *  setting the flags so that it can enable an Rx mode.
 **/
static s32 fm10k_iov_set_lport_pf(struct fm10k_hw *hw,
				  struct fm10k_vf_info *vf_info,
				  u16 lport_idx, u8 flags)
{
	u16 glort = (hw->mac.dglort_map + lport_idx) & FM10K_DGLORTMAP_NONE;

	/* if glort is not valid return error */
	if (!fm10k_glort_valid_pf(hw, glort))
		return FM10K_ERR_PARAM;

	vf_info->vf_flags = flags | FM10K_VF_FLAG_NONE_CAPABLE;
	vf_info->glort = glort;

	return 0;
}

/**
 *  fm10k_iov_reset_lport_pf - Disable a logical port for a given VF
 *  @hw: pointer to hardware structure
 *  @vf_info: pointer to VF information structure
 *
 *  This function disables a VF port by stripping it of a GLORT and
 *  setting the flags so that it cannot enable any Rx mode.
 **/
static void fm10k_iov_reset_lport_pf(struct fm10k_hw *hw,
				     struct fm10k_vf_info *vf_info)
{
	u32 msg[1];

	/* need to disable the port if it is already enabled */
	if (FM10K_VF_FLAG_ENABLED(vf_info)) {
		/* notify switch that this port has been disabled */
		fm10k_update_lport_state_pf(hw, vf_info->glort, 1, false);

		/* generate port state response to notify VF it is not ready */
		fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_LPORT_STATE);
		vf_info->mbx.ops.enqueue_tx(hw, &vf_info->mbx, msg);
	}

	/* clear flags and glort if it exists */
	vf_info->vf_flags = 0;
	vf_info->glort = 0;
}

/**
 *  fm10k_iov_update_stats_pf - Updates hardware related statistics for VFs
 *  @hw: pointer to hardware structure
 *  @q: stats for all queues of a VF
 *  @vf_idx: index of VF
 *
 *  This function collects queue stats for VFs.
 **/
static void fm10k_iov_update_stats_pf(struct fm10k_hw *hw,
				      struct fm10k_hw_stats_q *q,
				      u16 vf_idx)
{
	u32 idx, qpp;

	/* get stats for all of the queues */
	qpp = fm10k_queues_per_pool(hw);
	idx = fm10k_vf_queue_index(hw, vf_idx);
	fm10k_update_hw_stats_q(hw, q, idx, qpp);
}

/**
 *  fm10k_iov_msg_msix_pf - Message handler for MSI-X request from VF
 *  @hw: Pointer to hardware structure
 *  @results: Pointer array to message, results[0] is pointer to message
 *  @mbx: Pointer to mailbox information structure
 *
 *  This function is a default handler for MSI-X requests from the VF.  The
 *  assumption is that in this case it is acceptable to just directly
 *  hand off the message from the VF to the underlying shared code.
 **/
s32 fm10k_iov_msg_msix_pf(struct fm10k_hw *hw, u32 **results,
			  struct fm10k_mbx_info *mbx)
{
	struct fm10k_vf_info *vf_info = (struct fm10k_vf_info *)mbx;
	u8 vf_idx = vf_info->vf_idx;

	return hw->iov.ops.assign_int_moderator(hw, vf_idx);
}

/**
 * fm10k_iov_select_vid - Select correct default VLAN ID
 * @hw: Pointer to hardware structure
 * @vid: VLAN ID to correct
 *
 * Will report an error if the VLAN ID is out of range. For VID = 0, it will
 * return either the pf_vid or sw_vid depending on which one is set.
 */
static s32 fm10k_iov_select_vid(struct fm10k_vf_info *vf_info, u16 vid)
{
	if (!vid)
		return vf_info->pf_vid ? vf_info->pf_vid : vf_info->sw_vid;
	else if (vf_info->pf_vid && vid != vf_info->pf_vid)
		return FM10K_ERR_PARAM;
	else
		return vid;
}

/**
 *  fm10k_iov_msg_mac_vlan_pf - Message handler for MAC/VLAN request from VF
 *  @hw: Pointer to hardware structure
 *  @results: Pointer array to message, results[0] is pointer to message
 *  @mbx: Pointer to mailbox information structure
 *
 *  This function is a default handler for MAC/VLAN requests from the VF.
 *  The assumption is that in this case it is acceptable to just directly
 *  hand off the message from the VF to the underlying shared code.
 **/
s32 fm10k_iov_msg_mac_vlan_pf(struct fm10k_hw *hw, u32 **results,
			      struct fm10k_mbx_info *mbx)
{
	struct fm10k_vf_info *vf_info = (struct fm10k_vf_info *)mbx;
	u8 mac[ETH_ALEN];
	u32 *result;
	int err = 0;
	bool set;
	u16 vlan;
	u32 vid;

	/* we shouldn't be updating rules on a disabled interface */
	if (!FM10K_VF_FLAG_ENABLED(vf_info))
		err = FM10K_ERR_PARAM;

	if (!err && !!results[FM10K_MAC_VLAN_MSG_VLAN]) {
		result = results[FM10K_MAC_VLAN_MSG_VLAN];

		/* record VLAN id requested */
		err = fm10k_tlv_attr_get_u32(result, &vid);
		if (err)
			return err;

		set = !(vid & FM10K_VLAN_CLEAR);
		vid &= ~FM10K_VLAN_CLEAR;

		/* if the length field has been set, this is a multi-bit
		 * update request. For multi-bit requests, simply disallow
		 * them when the pf_vid has been set. In this case, the PF
		 * should have already cleared the VLAN_TABLE, and if we
		 * allowed them, it could allow a rogue VF to receive traffic
		 * on a VLAN it was not assigned. In the single-bit case, we
		 * need to modify requests for VLAN 0 to use the default PF or
		 * SW vid when assigned.
		 */

		if (vid >> 16) {
			/* prevent multi-bit requests when PF has
			 * administratively set the VLAN for this VF
			 */
			if (vf_info->pf_vid)
				return FM10K_ERR_PARAM;
		} else {
			err = fm10k_iov_select_vid(vf_info, (u16)vid);
			if (err < 0)
				return err;

			vid = err;
		}

		/* update VSI info for VF in regards to VLAN table */
		err = hw->mac.ops.update_vlan(hw, vid, vf_info->vsi, set);
	}

	if (!err && !!results[FM10K_MAC_VLAN_MSG_MAC]) {
		result = results[FM10K_MAC_VLAN_MSG_MAC];

		/* record unicast MAC address requested */
		err = fm10k_tlv_attr_get_mac_vlan(result, mac, &vlan);
		if (err)
			return err;

		/* block attempts to set MAC for a locked device */
		if (is_valid_ether_addr(vf_info->mac) &&
		    !ether_addr_equal(mac, vf_info->mac))
			return FM10K_ERR_PARAM;

		set = !(vlan & FM10K_VLAN_CLEAR);
		vlan &= ~FM10K_VLAN_CLEAR;

		err = fm10k_iov_select_vid(vf_info, vlan);
		if (err < 0)
			return err;

		vlan = (u16)err;

		/* notify switch of request for new unicast address */
		err = hw->mac.ops.update_uc_addr(hw, vf_info->glort,
						 mac, vlan, set, 0);
	}

	if (!err && !!results[FM10K_MAC_VLAN_MSG_MULTICAST]) {
		result = results[FM10K_MAC_VLAN_MSG_MULTICAST];

		/* record multicast MAC address requested */
		err = fm10k_tlv_attr_get_mac_vlan(result, mac, &vlan);
		if (err)
			return err;

		/* verify that the VF is allowed to request multicast */
		if (!(vf_info->vf_flags & FM10K_VF_FLAG_MULTI_ENABLED))
			return FM10K_ERR_PARAM;

		set = !(vlan & FM10K_VLAN_CLEAR);
		vlan &= ~FM10K_VLAN_CLEAR;

		err = fm10k_iov_select_vid(vf_info, vlan);
		if (err < 0)
			return err;

		vlan = (u16)err;

		/* notify switch of request for new multicast address */
		err = hw->mac.ops.update_mc_addr(hw, vf_info->glort,
						 mac, vlan, set);
	}

	return err;
}

/**
 *  fm10k_iov_supported_xcast_mode_pf - Determine best match for xcast mode
 *  @vf_info: VF info structure containing capability flags
 *  @mode: Requested xcast mode
 *
 *  This function outputs the mode that most closely matches the requested
 *  mode.  If not modes match it will request we disable the port
 **/
static u8 fm10k_iov_supported_xcast_mode_pf(struct fm10k_vf_info *vf_info,
					    u8 mode)
{
	u8 vf_flags = vf_info->vf_flags;

	/* match up mode to capabilities as best as possible */
	switch (mode) {
	case FM10K_XCAST_MODE_PROMISC:
		if (vf_flags & FM10K_VF_FLAG_PROMISC_CAPABLE)
			return FM10K_XCAST_MODE_PROMISC;
		/* fallthough */
	case FM10K_XCAST_MODE_ALLMULTI:
		if (vf_flags & FM10K_VF_FLAG_ALLMULTI_CAPABLE)
			return FM10K_XCAST_MODE_ALLMULTI;
		/* fallthough */
	case FM10K_XCAST_MODE_MULTI:
		if (vf_flags & FM10K_VF_FLAG_MULTI_CAPABLE)
			return FM10K_XCAST_MODE_MULTI;
		/* fallthough */
	case FM10K_XCAST_MODE_NONE:
		if (vf_flags & FM10K_VF_FLAG_NONE_CAPABLE)
			return FM10K_XCAST_MODE_NONE;
		/* fallthough */
	default:
		break;
	}

	/* disable interface as it should not be able to request any */
	return FM10K_XCAST_MODE_DISABLE;
}

/**
 *  fm10k_iov_msg_lport_state_pf - Message handler for port state requests
 *  @hw: Pointer to hardware structure
 *  @results: Pointer array to message, results[0] is pointer to message
 *  @mbx: Pointer to mailbox information structure
 *
 *  This function is a default handler for port state requests.  The port
 *  state requests for now are basic and consist of enabling or disabling
 *  the port.
 **/
s32 fm10k_iov_msg_lport_state_pf(struct fm10k_hw *hw, u32 **results,
				 struct fm10k_mbx_info *mbx)
{
	struct fm10k_vf_info *vf_info = (struct fm10k_vf_info *)mbx;
	u32 *result;
	s32 err = 0;
	u32 msg[2];
	u8 mode = 0;

	/* verify VF is allowed to enable even minimal mode */
	if (!(vf_info->vf_flags & FM10K_VF_FLAG_NONE_CAPABLE))
		return FM10K_ERR_PARAM;

	if (!!results[FM10K_LPORT_STATE_MSG_XCAST_MODE]) {
		result = results[FM10K_LPORT_STATE_MSG_XCAST_MODE];

		/* XCAST mode update requested */
		err = fm10k_tlv_attr_get_u8(result, &mode);
		if (err)
			return FM10K_ERR_PARAM;

		/* prep for possible demotion depending on capabilities */
		mode = fm10k_iov_supported_xcast_mode_pf(vf_info, mode);

		/* if mode is not currently enabled, enable it */
		if (!(FM10K_VF_FLAG_ENABLED(vf_info) & BIT(mode)))
			fm10k_update_xcast_mode_pf(hw, vf_info->glort, mode);

		/* swap mode back to a bit flag */
		mode = FM10K_VF_FLAG_SET_MODE(mode);
	} else if (!results[FM10K_LPORT_STATE_MSG_DISABLE]) {
		/* need to disable the port if it is already enabled */
		if (FM10K_VF_FLAG_ENABLED(vf_info))
			err = fm10k_update_lport_state_pf(hw, vf_info->glort,
							  1, false);

		/* we need to clear VF_FLAG_ENABLED flags in order to ensure
		 * that we actually re-enable the LPORT state below. Note that
		 * this has no impact if the VF is already disabled, as the
		 * flags are already cleared.
		 */
		if (!err)
			vf_info->vf_flags = FM10K_VF_FLAG_CAPABLE(vf_info);

		/* when enabling the port we should reset the rate limiters */
		hw->iov.ops.configure_tc(hw, vf_info->vf_idx, vf_info->rate);

		/* set mode for minimal functionality */
		mode = FM10K_VF_FLAG_SET_MODE_NONE;

		/* generate port state response to notify VF it is ready */
		fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_LPORT_STATE);
		fm10k_tlv_attr_put_bool(msg, FM10K_LPORT_STATE_MSG_READY);
		mbx->ops.enqueue_tx(hw, mbx, msg);
	}

	/* if enable state toggled note the update */
	if (!err && (!FM10K_VF_FLAG_ENABLED(vf_info) != !mode))
		err = fm10k_update_lport_state_pf(hw, vf_info->glort, 1,
						  !!mode);

	/* if state change succeeded, then update our stored state */
	mode |= FM10K_VF_FLAG_CAPABLE(vf_info);
	if (!err)
		vf_info->vf_flags = mode;

	return err;
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
		loopback_drop =
			fm10k_read_hw_stats_32b(hw,
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
 *  This function will check the DMA_CTRL2 register and mailbox in order
 *  to determine if the switch is ready for the PF to begin requesting
 *  addresses and mapping traffic to the local interface.
 **/
static s32 fm10k_get_host_state_pf(struct fm10k_hw *hw, bool *switch_ready)
{
	u32 dma_ctrl2;

	/* verify the switch is ready for interaction */
	dma_ctrl2 = fm10k_read_reg(hw, FM10K_DMA_CTRL2);
	if (!(dma_ctrl2 & FM10K_DMA_CTRL2_SWITCH_READY))
		return 0;

	/* retrieve generic host state info */
	return fm10k_get_host_state_generic(hw, switch_ready);
}

/* This structure defines the attibutes to be parsed below */
const struct fm10k_tlv_attr fm10k_lport_map_msg_attr[] = {
	FM10K_TLV_ATTR_LE_STRUCT(FM10K_PF_ATTR_ID_ERR,
				 sizeof(struct fm10k_swapi_error)),
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
static s32 fm10k_msg_update_pvid_pf(struct fm10k_hw *hw, u32 **results,
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

	/* verify VLAN ID is valid */
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

static const struct fm10k_mac_ops mac_ops_pf = {
	.get_bus_info		= fm10k_get_bus_info_generic,
	.reset_hw		= fm10k_reset_hw_pf,
	.init_hw		= fm10k_init_hw_pf,
	.start_hw		= fm10k_start_hw_generic,
	.stop_hw		= fm10k_stop_hw_generic,
	.update_vlan		= fm10k_update_vlan_pf,
	.read_mac_addr		= fm10k_read_mac_addr_pf,
	.update_uc_addr		= fm10k_update_uc_addr_pf,
	.update_mc_addr		= fm10k_update_mc_addr_pf,
	.update_xcast_mode	= fm10k_update_xcast_mode_pf,
	.update_int_moderator	= fm10k_update_int_moderator_pf,
	.update_lport_state	= fm10k_update_lport_state_pf,
	.update_hw_stats	= fm10k_update_hw_stats_pf,
	.rebind_hw_stats	= fm10k_rebind_hw_stats_pf,
	.configure_dglort_map	= fm10k_configure_dglort_map_pf,
	.set_dma_mask		= fm10k_set_dma_mask_pf,
	.get_fault		= fm10k_get_fault_pf,
	.get_host_state		= fm10k_get_host_state_pf,
	.request_lport_map	= fm10k_request_lport_map_pf,
};

static const struct fm10k_iov_ops iov_ops_pf = {
	.assign_resources		= fm10k_iov_assign_resources_pf,
	.configure_tc			= fm10k_iov_configure_tc_pf,
	.assign_int_moderator		= fm10k_iov_assign_int_moderator_pf,
	.assign_default_mac_vlan	= fm10k_iov_assign_default_mac_vlan_pf,
	.reset_resources		= fm10k_iov_reset_resources_pf,
	.set_lport			= fm10k_iov_set_lport_pf,
	.reset_lport			= fm10k_iov_reset_lport_pf,
	.update_stats			= fm10k_iov_update_stats_pf,
};

static s32 fm10k_get_invariants_pf(struct fm10k_hw *hw)
{
	fm10k_get_invariants_generic(hw);

	return fm10k_sm_mbx_init(hw, &hw->mbx, fm10k_msg_data_pf);
}

const struct fm10k_info fm10k_pf_info = {
	.mac		= fm10k_mac_pf,
	.get_invariants	= fm10k_get_invariants_pf,
	.mac_ops	= &mac_ops_pf,
	.iov_ops	= &iov_ops_pf,
};
