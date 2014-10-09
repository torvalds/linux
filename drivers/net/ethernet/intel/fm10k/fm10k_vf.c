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

#include "fm10k_vf.h"

/**
 *  fm10k_stop_hw_vf - Stop Tx/Rx units
 *  @hw: pointer to hardware structure
 *
 **/
static s32 fm10k_stop_hw_vf(struct fm10k_hw *hw)
{
	u8 *perm_addr = hw->mac.perm_addr;
	u32 bal = 0, bah = 0;
	s32 err;
	u16 i;

	/* we need to disable the queues before taking further steps */
	err = fm10k_stop_hw_generic(hw);
	if (err)
		return err;

	/* If permenant address is set then we need to restore it */
	if (is_valid_ether_addr(perm_addr)) {
		bal = (((u32)perm_addr[3]) << 24) |
		      (((u32)perm_addr[4]) << 16) |
		      (((u32)perm_addr[5]) << 8);
		bah = (((u32)0xFF)	   << 24) |
		      (((u32)perm_addr[0]) << 16) |
		      (((u32)perm_addr[1]) << 8) |
		       ((u32)perm_addr[2]);
	}

	/* The queues have already been disabled so we just need to
	 * update their base address registers
	 */
	for (i = 0; i < hw->mac.max_queues; i++) {
		fm10k_write_reg(hw, FM10K_TDBAL(i), bal);
		fm10k_write_reg(hw, FM10K_TDBAH(i), bah);
		fm10k_write_reg(hw, FM10K_RDBAL(i), bal);
		fm10k_write_reg(hw, FM10K_RDBAH(i), bah);
	}

	return 0;
}

/**
 *  fm10k_reset_hw_vf - VF hardware reset
 *  @hw: pointer to hardware structure
 *
 *  This function should return the hardare to a state similar to the
 *  one it is in after just being initialized.
 **/
static s32 fm10k_reset_hw_vf(struct fm10k_hw *hw)
{
	s32 err;

	/* shut down queues we own and reset DMA configuration */
	err = fm10k_stop_hw_vf(hw);
	if (err)
		return err;

	/* Inititate VF reset */
	fm10k_write_reg(hw, FM10K_VFCTRL, FM10K_VFCTRL_RST);

	/* Flush write and allow 100us for reset to complete */
	fm10k_write_flush(hw);
	udelay(FM10K_RESET_TIMEOUT);

	/* Clear reset bit and verify it was cleared */
	fm10k_write_reg(hw, FM10K_VFCTRL, 0);
	if (fm10k_read_reg(hw, FM10K_VFCTRL) & FM10K_VFCTRL_RST)
		err = FM10K_ERR_RESET_FAILED;

	return err;
}

/**
 *  fm10k_init_hw_vf - VF hardware initialization
 *  @hw: pointer to hardware structure
 *
 **/
static s32 fm10k_init_hw_vf(struct fm10k_hw *hw)
{
	u32 tqdloc, tqdloc0 = ~fm10k_read_reg(hw, FM10K_TQDLOC(0));
	s32 err;
	u16 i;

	/* assume we always have at least 1 queue */
	for (i = 1; tqdloc0 && (i < FM10K_MAX_QUEUES_POOL); i++) {
		/* verify the Descriptor cache offsets are increasing */
		tqdloc = ~fm10k_read_reg(hw, FM10K_TQDLOC(i));
		if (!tqdloc || (tqdloc == tqdloc0))
			break;

		/* check to verify the PF doesn't own any of our queues */
		if (!~fm10k_read_reg(hw, FM10K_TXQCTL(i)) ||
		    !~fm10k_read_reg(hw, FM10K_RXQCTL(i)))
			break;
	}

	/* shut down queues we own and reset DMA configuration */
	err = fm10k_disable_queues_generic(hw, i);
	if (err)
		return err;

	/* record maximum queue count */
	hw->mac.max_queues = i;

	return 0;
}

/**
 *  fm10k_is_slot_appropriate_vf - Indicate appropriate slot for this SKU
 *  @hw: pointer to hardware structure
 *
 *  Looks at the PCIe bus info to confirm whether or not this slot can support
 *  the necessary bandwidth for this device. Since the VF has no control over
 *  the "slot" it is in, always indicate that the slot is appropriate.
 **/
static bool fm10k_is_slot_appropriate_vf(struct fm10k_hw *hw)
{
	return true;
}

/* This structure defines the attibutes to be parsed below */
const struct fm10k_tlv_attr fm10k_mac_vlan_msg_attr[] = {
	FM10K_TLV_ATTR_U32(FM10K_MAC_VLAN_MSG_VLAN),
	FM10K_TLV_ATTR_BOOL(FM10K_MAC_VLAN_MSG_SET),
	FM10K_TLV_ATTR_MAC_ADDR(FM10K_MAC_VLAN_MSG_MAC),
	FM10K_TLV_ATTR_MAC_ADDR(FM10K_MAC_VLAN_MSG_DEFAULT_MAC),
	FM10K_TLV_ATTR_MAC_ADDR(FM10K_MAC_VLAN_MSG_MULTICAST),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_update_vlan_vf - Update status of VLAN ID in VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vid: VLAN ID to add to table
 *  @vsi: Reserved, should always be 0
 *  @set: Indicates if this is a set or clear operation
 *
 *  This function adds or removes the corresponding VLAN ID from the VLAN
 *  filter table for this VF.
 **/
static s32 fm10k_update_vlan_vf(struct fm10k_hw *hw, u32 vid, u8 vsi, bool set)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[4];

	/* verify the index is not set */
	if (vsi)
		return FM10K_ERR_PARAM;

	/* verify upper 4 bits of vid and length are 0 */
	if ((vid << 16 | vid) >> 28)
		return FM10K_ERR_PARAM;

	/* encode set bit into the VLAN ID */
	if (!set)
		vid |= FM10K_VLAN_CLEAR;

	/* generate VLAN request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_MAC_VLAN);
	fm10k_tlv_attr_put_u32(msg, FM10K_MAC_VLAN_MSG_VLAN, vid);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_msg_mac_vlan_vf - Read device MAC address from mailbox message
 *  @hw: pointer to the HW structure
 *  @results: Attributes for message
 *  @mbx: unused mailbox data
 *
 *  This function should determine the MAC address for the VF
 **/
s32 fm10k_msg_mac_vlan_vf(struct fm10k_hw *hw, u32 **results,
			  struct fm10k_mbx_info *mbx)
{
	u8 perm_addr[ETH_ALEN];
	u16 vid;
	s32 err;

	/* record MAC address requested */
	err = fm10k_tlv_attr_get_mac_vlan(
					results[FM10K_MAC_VLAN_MSG_DEFAULT_MAC],
					perm_addr, &vid);
	if (err)
		return err;

	ether_addr_copy(hw->mac.perm_addr, perm_addr);
	hw->mac.default_vid = vid & (FM10K_VLAN_TABLE_VID_MAX - 1);
	hw->mac.vlan_override = !!(vid & FM10K_VLAN_CLEAR);

	return 0;
}

/**
 *  fm10k_read_mac_addr_vf - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  This function should determine the MAC address for the VF
 **/
static s32 fm10k_read_mac_addr_vf(struct fm10k_hw *hw)
{
	u8 perm_addr[ETH_ALEN];
	u32 base_addr;

	base_addr = fm10k_read_reg(hw, FM10K_TDBAL(0));

	/* last byte should be 0 */
	if (base_addr << 24)
		return  FM10K_ERR_INVALID_MAC_ADDR;

	perm_addr[3] = (u8)(base_addr >> 24);
	perm_addr[4] = (u8)(base_addr >> 16);
	perm_addr[5] = (u8)(base_addr >> 8);

	base_addr = fm10k_read_reg(hw, FM10K_TDBAH(0));

	/* first byte should be all 1's */
	if ((~base_addr) >> 24)
		return  FM10K_ERR_INVALID_MAC_ADDR;

	perm_addr[0] = (u8)(base_addr >> 16);
	perm_addr[1] = (u8)(base_addr >> 8);
	perm_addr[2] = (u8)(base_addr);

	ether_addr_copy(hw->mac.perm_addr, perm_addr);
	ether_addr_copy(hw->mac.addr, perm_addr);

	return 0;
}

/**
 *  fm10k_update_uc_addr_vf - Update device unicast address
 *  @hw: pointer to the HW structure
 *  @glort: unused
 *  @mac: MAC address to add/remove from table
 *  @vid: VLAN ID to add/remove from table
 *  @add: Indicates if this is an add or remove operation
 *  @flags: flags field to indicate add and secure - unused
 *
 *  This function is used to add or remove unicast MAC addresses for
 *  the VF.
 **/
static s32 fm10k_update_uc_addr_vf(struct fm10k_hw *hw, u16 glort,
				   const u8 *mac, u16 vid, bool add, u8 flags)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[7];

	/* verify VLAN ID is valid */
	if (vid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	/* verify MAC address is valid */
	if (!is_valid_ether_addr(mac))
		return FM10K_ERR_PARAM;

	/* verify we are not locked down on the MAC address */
	if (is_valid_ether_addr(hw->mac.perm_addr) &&
	    memcmp(hw->mac.perm_addr, mac, ETH_ALEN))
		return FM10K_ERR_PARAM;

	/* add bit to notify us if this is a set of clear operation */
	if (!add)
		vid |= FM10K_VLAN_CLEAR;

	/* generate VLAN request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_MAC_VLAN);
	fm10k_tlv_attr_put_mac_vlan(msg, FM10K_MAC_VLAN_MSG_MAC, mac, vid);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_update_mc_addr_vf - Update device multicast address
 *  @hw: pointer to the HW structure
 *  @glort: unused
 *  @mac: MAC address to add/remove from table
 *  @vid: VLAN ID to add/remove from table
 *  @add: Indicates if this is an add or remove operation
 *
 *  This function is used to add or remove multicast MAC addresses for
 *  the VF.
 **/
static s32 fm10k_update_mc_addr_vf(struct fm10k_hw *hw, u16 glort,
				   const u8 *mac, u16 vid, bool add)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[7];

	/* verify VLAN ID is valid */
	if (vid >= FM10K_VLAN_TABLE_VID_MAX)
		return FM10K_ERR_PARAM;

	/* verify multicast address is valid */
	if (!is_multicast_ether_addr(mac))
		return FM10K_ERR_PARAM;

	/* add bit to notify us if this is a set of clear operation */
	if (!add)
		vid |= FM10K_VLAN_CLEAR;

	/* generate VLAN request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_MAC_VLAN);
	fm10k_tlv_attr_put_mac_vlan(msg, FM10K_MAC_VLAN_MSG_MULTICAST,
				    mac, vid);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_update_int_moderator_vf - Request update of interrupt moderator list
 *  @hw: pointer to hardware structure
 *
 *  This function will issue a request to the PF to rescan our MSI-X table
 *  and to update the interrupt moderator linked list.
 **/
static void fm10k_update_int_moderator_vf(struct fm10k_hw *hw)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[1];

	/* generate MSI-X request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_MSIX);

	/* load onto outgoing mailbox */
	mbx->ops.enqueue_tx(hw, mbx, msg);
}

/* This structure defines the attibutes to be parsed below */
const struct fm10k_tlv_attr fm10k_lport_state_msg_attr[] = {
	FM10K_TLV_ATTR_BOOL(FM10K_LPORT_STATE_MSG_DISABLE),
	FM10K_TLV_ATTR_U8(FM10K_LPORT_STATE_MSG_XCAST_MODE),
	FM10K_TLV_ATTR_BOOL(FM10K_LPORT_STATE_MSG_READY),
	FM10K_TLV_ATTR_LAST
};

/**
 *  fm10k_msg_lport_state_vf - Message handler for lport_state message from PF
 *  @hw: Pointer to hardware structure
 *  @results: pointer array containing parsed data
 *  @mbx: Pointer to mailbox information structure
 *
 *  This handler is meant to capture the indication from the PF that we
 *  are ready to bring up the interface.
 **/
s32 fm10k_msg_lport_state_vf(struct fm10k_hw *hw, u32 **results,
			     struct fm10k_mbx_info *mbx)
{
	hw->mac.dglort_map = !results[FM10K_LPORT_STATE_MSG_READY] ?
			     FM10K_DGLORTMAP_NONE : FM10K_DGLORTMAP_ZERO;

	return 0;
}

/**
 *  fm10k_update_lport_state_vf - Update device state in lower device
 *  @hw: pointer to the HW structure
 *  @glort: unused
 *  @count: number of logical ports to enable - unused (always 1)
 *  @enable: boolean value indicating if this is an enable or disable request
 *
 *  Notify the lower device of a state change.  If the lower device is
 *  enabled we can add filters, if it is disabled all filters for this
 *  logical port are flushed.
 **/
static s32 fm10k_update_lport_state_vf(struct fm10k_hw *hw, u16 glort,
				       u16 count, bool enable)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[2];

	/* reset glort mask 0 as we have to wait to be enabled */
	hw->mac.dglort_map = FM10K_DGLORTMAP_NONE;

	/* generate port state request */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_LPORT_STATE);
	if (!enable)
		fm10k_tlv_attr_put_bool(msg, FM10K_LPORT_STATE_MSG_DISABLE);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

/**
 *  fm10k_update_xcast_mode_vf - Request update of multicast mode
 *  @hw: pointer to hardware structure
 *  @glort: unused
 *  @mode: integer value indicating mode being requested
 *
 *  This function will attempt to request a higher mode for the port
 *  so that it can enable either multicast, multicast promiscuous, or
 *  promiscuous mode of operation.
 **/
static s32 fm10k_update_xcast_mode_vf(struct fm10k_hw *hw, u16 glort, u8 mode)
{
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 msg[3];

	if (mode > FM10K_XCAST_MODE_NONE)
		return FM10K_ERR_PARAM;
	/* generate message requesting to change xcast mode */
	fm10k_tlv_msg_init(msg, FM10K_VF_MSG_ID_LPORT_STATE);
	fm10k_tlv_attr_put_u8(msg, FM10K_LPORT_STATE_MSG_XCAST_MODE, mode);

	/* load onto outgoing mailbox */
	return mbx->ops.enqueue_tx(hw, mbx, msg);
}

const struct fm10k_tlv_attr fm10k_1588_msg_attr[] = {
	FM10K_TLV_ATTR_U64(FM10K_1588_MSG_TIMESTAMP),
	FM10K_TLV_ATTR_LAST
};

/* currently there is no shared 1588 timestamp handler */

/**
 *  fm10k_update_hw_stats_vf - Updates hardware related statistics of VF
 *  @hw: pointer to hardware structure
 *  @stats: pointer to statistics structure
 *
 *  This function collects and aggregates per queue hardware statistics.
 **/
static void fm10k_update_hw_stats_vf(struct fm10k_hw *hw,
				     struct fm10k_hw_stats *stats)
{
	fm10k_update_hw_stats_q(hw, stats->q, 0, hw->mac.max_queues);
}

/**
 *  fm10k_rebind_hw_stats_vf - Resets base for hardware statistics of VF
 *  @hw: pointer to hardware structure
 *  @stats: pointer to the stats structure to update
 *
 *  This function resets the base for queue hardware statistics.
 **/
static void fm10k_rebind_hw_stats_vf(struct fm10k_hw *hw,
				     struct fm10k_hw_stats *stats)
{
	/* Unbind Queue Statistics */
	fm10k_unbind_hw_stats_q(stats->q, 0, hw->mac.max_queues);

	/* Reinitialize bases for all stats */
	fm10k_update_hw_stats_vf(hw, stats);
}

/**
 *  fm10k_configure_dglort_map_vf - Configures GLORT entry and queues
 *  @hw: pointer to hardware structure
 *  @dglort: pointer to dglort configuration structure
 *
 *  Reads the configuration structure contained in dglort_cfg and uses
 *  that information to then populate a DGLORTMAP/DEC entry and the queues
 *  to which it has been assigned.
 **/
static s32 fm10k_configure_dglort_map_vf(struct fm10k_hw *hw,
					 struct fm10k_dglort_cfg *dglort)
{
	/* verify the dglort pointer */
	if (!dglort)
		return FM10K_ERR_PARAM;

	/* stub for now until we determine correct message for this */

	return 0;
}

/**
 *  fm10k_adjust_systime_vf - Adjust systime frequency
 *  @hw: pointer to hardware structure
 *  @ppb: adjustment rate in parts per billion
 *
 *  This function takes an adjustment rate in parts per billion and will
 *  verify that this value is 0 as the VF cannot support adjusting the
 *  systime clock.
 *
 *  If the ppb value is non-zero the return is ERR_PARAM else success
 **/
static s32 fm10k_adjust_systime_vf(struct fm10k_hw *hw, s32 ppb)
{
	/* The VF cannot adjust the clock frequency, however it should
	 * already have a syntonic clock with whichever host interface is
	 * running as the master for the host interface clock domain so
	 * there should be not frequency adjustment necessary.
	 */
	return ppb ? FM10K_ERR_PARAM : 0;
}

/**
 *  fm10k_read_systime_vf - Reads value of systime registers
 *  @hw: pointer to the hardware structure
 *
 *  Function reads the content of 2 registers, combined to represent a 64 bit
 *  value measured in nanosecods.  In order to guarantee the value is accurate
 *  we check the 32 most significant bits both before and after reading the
 *  32 least significant bits to verify they didn't change as we were reading
 *  the registers.
 **/
static u64 fm10k_read_systime_vf(struct fm10k_hw *hw)
{
	u32 systime_l, systime_h, systime_tmp;

	systime_h = fm10k_read_reg(hw, FM10K_VFSYSTIME + 1);

	do {
		systime_tmp = systime_h;
		systime_l = fm10k_read_reg(hw, FM10K_VFSYSTIME);
		systime_h = fm10k_read_reg(hw, FM10K_VFSYSTIME + 1);
	} while (systime_tmp != systime_h);

	return ((u64)systime_h << 32) | systime_l;
}

static const struct fm10k_msg_data fm10k_msg_data_vf[] = {
	FM10K_TLV_MSG_TEST_HANDLER(fm10k_tlv_msg_test),
	FM10K_VF_MSG_MAC_VLAN_HANDLER(fm10k_msg_mac_vlan_vf),
	FM10K_VF_MSG_LPORT_STATE_HANDLER(fm10k_msg_lport_state_vf),
	FM10K_TLV_MSG_ERROR_HANDLER(fm10k_tlv_msg_error),
};

static struct fm10k_mac_ops mac_ops_vf = {
	.get_bus_info		= &fm10k_get_bus_info_generic,
	.reset_hw		= &fm10k_reset_hw_vf,
	.init_hw		= &fm10k_init_hw_vf,
	.start_hw		= &fm10k_start_hw_generic,
	.stop_hw		= &fm10k_stop_hw_vf,
	.is_slot_appropriate	= &fm10k_is_slot_appropriate_vf,
	.update_vlan		= &fm10k_update_vlan_vf,
	.read_mac_addr		= &fm10k_read_mac_addr_vf,
	.update_uc_addr		= &fm10k_update_uc_addr_vf,
	.update_mc_addr		= &fm10k_update_mc_addr_vf,
	.update_xcast_mode	= &fm10k_update_xcast_mode_vf,
	.update_int_moderator	= &fm10k_update_int_moderator_vf,
	.update_lport_state	= &fm10k_update_lport_state_vf,
	.update_hw_stats	= &fm10k_update_hw_stats_vf,
	.rebind_hw_stats	= &fm10k_rebind_hw_stats_vf,
	.configure_dglort_map	= &fm10k_configure_dglort_map_vf,
	.get_host_state		= &fm10k_get_host_state_generic,
	.adjust_systime		= &fm10k_adjust_systime_vf,
	.read_systime		= &fm10k_read_systime_vf,
};

static s32 fm10k_get_invariants_vf(struct fm10k_hw *hw)
{
	fm10k_get_invariants_generic(hw);

	return fm10k_pfvf_mbx_init(hw, &hw->mbx, fm10k_msg_data_vf, 0);
}

struct fm10k_info fm10k_vf_info = {
	.mac		= fm10k_mac_vf,
	.get_invariants	= &fm10k_get_invariants_vf,
	.mac_ops	= &mac_ops_vf,
};
