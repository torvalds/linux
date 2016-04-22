/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2015 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "vf.h"
#include "ixgbevf.h"

/* On Hyper-V, to reset, we need to read from this offset
 * from the PCI config space. This is the mechanism used on
 * Hyper-V to support PF/VF communication.
 */
#define IXGBE_HV_RESET_OFFSET           0x201

/**
 *  ixgbevf_start_hw_vf - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware by filling the bus info structure and media type, clears
 *  all on chip counters, initializes receive address registers, multicast
 *  table, VLAN filter table, calls routine to set up link and flow control
 *  settings, and leaves transmit and receive units disabled and uninitialized
 **/
static s32 ixgbevf_start_hw_vf(struct ixgbe_hw *hw)
{
	/* Clear adapter stopped flag */
	hw->adapter_stopped = false;

	return 0;
}

/**
 *  ixgbevf_init_hw_vf - virtual function hardware initialization
 *  @hw: pointer to hardware structure
 *
 *  Initialize the hardware by resetting the hardware and then starting
 *  the hardware
 **/
static s32 ixgbevf_init_hw_vf(struct ixgbe_hw *hw)
{
	s32 status = hw->mac.ops.start_hw(hw);

	hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

	return status;
}

/**
 *  ixgbevf_reset_hw_vf - Performs hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks and
 *  clears all interrupts.
 **/
static s32 ixgbevf_reset_hw_vf(struct ixgbe_hw *hw)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 timeout = IXGBE_VF_INIT_TIMEOUT;
	s32 ret_val = IXGBE_ERR_INVALID_MAC_ADDR;
	u32 msgbuf[IXGBE_VF_PERMADDR_MSG_LEN];
	u8 *addr = (u8 *)(&msgbuf[1]);

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/* reset the api version */
	hw->api_version = ixgbe_mbox_api_10;

	IXGBE_WRITE_REG(hw, IXGBE_VFCTRL, IXGBE_CTRL_RST);
	IXGBE_WRITE_FLUSH(hw);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!mbx->ops.check_for_rst(hw) && timeout) {
		timeout--;
		udelay(5);
	}

	if (!timeout)
		return IXGBE_ERR_RESET_FAILED;

	/* mailbox timeout can now become active */
	mbx->timeout = IXGBE_VF_MBX_INIT_TIMEOUT;

	msgbuf[0] = IXGBE_VF_RESET;
	mbx->ops.write_posted(hw, msgbuf, 1);

	mdelay(10);

	/* set our "perm_addr" based on info provided by PF
	 * also set up the mc_filter_type which is piggy backed
	 * on the mac address in word 3
	 */
	ret_val = mbx->ops.read_posted(hw, msgbuf, IXGBE_VF_PERMADDR_MSG_LEN);
	if (ret_val)
		return ret_val;

	/* New versions of the PF may NACK the reset return message
	 * to indicate that no MAC address has yet been assigned for
	 * the VF.
	 */
	if (msgbuf[0] != (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_ACK) &&
	    msgbuf[0] != (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_NACK))
		return IXGBE_ERR_INVALID_MAC_ADDR;

	if (msgbuf[0] == (IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_ACK))
		ether_addr_copy(hw->mac.perm_addr, addr);

	hw->mac.mc_filter_type = msgbuf[IXGBE_VF_MC_TYPE_WORD];

	return 0;
}

/**
 * Hyper-V variant; the VF/PF communication is through the PCI
 * config space.
 */
static s32 ixgbevf_hv_reset_hw_vf(struct ixgbe_hw *hw)
{
#if IS_ENABLED(CONFIG_PCI_MMCONFIG)
	struct ixgbevf_adapter *adapter = hw->back;
	int i;

	for (i = 0; i < 6; i++)
		pci_read_config_byte(adapter->pdev,
				     (i + IXGBE_HV_RESET_OFFSET),
				     &hw->mac.perm_addr[i]);
	return 0;
#else
	pr_err("PCI_MMCONFIG needs to be enabled for Hyper-V\n");
	return -EOPNOTSUPP;
#endif
}

/**
 *  ixgbevf_stop_hw_vf - Generic stop Tx/Rx units
 *  @hw: pointer to hardware structure
 *
 *  Sets the adapter_stopped flag within ixgbe_hw struct. Clears interrupts,
 *  disables transmit and receive units. The adapter_stopped flag is used by
 *  the shared code and drivers to determine if the adapter is in a stopped
 *  state and should not touch the hardware.
 **/
static s32 ixgbevf_stop_hw_vf(struct ixgbe_hw *hw)
{
	u32 number_of_queues;
	u32 reg_val;
	u16 i;

	/* Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	hw->adapter_stopped = true;

	/* Disable the receive unit by stopped each queue */
	number_of_queues = hw->mac.max_rx_queues;
	for (i = 0; i < number_of_queues; i++) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(i));
		if (reg_val & IXGBE_RXDCTL_ENABLE) {
			reg_val &= ~IXGBE_RXDCTL_ENABLE;
			IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(i), reg_val);
		}
	}

	IXGBE_WRITE_FLUSH(hw);

	/* Clear interrupt mask to stop from interrupts being generated */
	IXGBE_WRITE_REG(hw, IXGBE_VTEIMC, IXGBE_VF_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts */
	IXGBE_READ_REG(hw, IXGBE_VTEICR);

	/* Disable the transmit unit.  Each queue must be disabled. */
	number_of_queues = hw->mac.max_tx_queues;
	for (i = 0; i < number_of_queues; i++) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_VFTXDCTL(i));
		if (reg_val & IXGBE_TXDCTL_ENABLE) {
			reg_val &= ~IXGBE_TXDCTL_ENABLE;
			IXGBE_WRITE_REG(hw, IXGBE_VFTXDCTL(i), reg_val);
		}
	}

	return 0;
}

/**
 *  ixgbevf_mta_vector - Determines bit-vector in multicast table to set
 *  @hw: pointer to hardware structure
 *  @mc_addr: the multicast address
 *
 *  Extracts the 12 bits, from a multicast address, to determine which
 *  bit-vector to set in the multicast table. The hardware uses 12 bits, from
 *  incoming Rx multicast addresses, to determine the bit-vector to check in
 *  the MTA. Which of the 4 combination, of 12-bits, the hardware uses is set
 *  by the MO field of the MCSTCTRL. The MO field is set during initialization
 *  to mc_filter_type.
 **/
static s32 ixgbevf_mta_vector(struct ixgbe_hw *hw, u8 *mc_addr)
{
	u32 vector = 0;

	switch (hw->mac.mc_filter_type) {
	case 0:   /* use bits [47:36] of the address */
		vector = ((mc_addr[4] >> 4) | (((u16)mc_addr[5]) << 4));
		break;
	case 1:   /* use bits [46:35] of the address */
		vector = ((mc_addr[4] >> 3) | (((u16)mc_addr[5]) << 5));
		break;
	case 2:   /* use bits [45:34] of the address */
		vector = ((mc_addr[4] >> 2) | (((u16)mc_addr[5]) << 6));
		break;
	case 3:   /* use bits [43:32] of the address */
		vector = ((mc_addr[4]) | (((u16)mc_addr[5]) << 8));
		break;
	default:  /* Invalid mc_filter_type */
		break;
	}

	/* vector can only be 12-bits or boundary will be exceeded */
	vector &= 0xFFF;
	return vector;
}

/**
 *  ixgbevf_get_mac_addr_vf - Read device MAC address
 *  @hw: pointer to the HW structure
 *  @mac_addr: pointer to storage for retrieved MAC address
 **/
static s32 ixgbevf_get_mac_addr_vf(struct ixgbe_hw *hw, u8 *mac_addr)
{
	ether_addr_copy(mac_addr, hw->mac.perm_addr);

	return 0;
}

static s32 ixgbevf_set_uc_addr_vf(struct ixgbe_hw *hw, u32 index, u8 *addr)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 ret_val;

	memset(msgbuf, 0, sizeof(msgbuf));
	/* If index is one then this is the start of a new list and needs
	 * indication to the PF so it can do it's own list management.
	 * If it is zero then that tells the PF to just clear all of
	 * this VF's macvlans and there is no new list.
	 */
	msgbuf[0] |= index << IXGBE_VT_MSGINFO_SHIFT;
	msgbuf[0] |= IXGBE_VF_SET_MACVLAN;
	if (addr)
		ether_addr_copy(msg_addr, addr);
	ret_val = mbx->ops.write_posted(hw, msgbuf, 3);

	if (!ret_val)
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3);

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

	if (!ret_val)
		if (msgbuf[0] ==
		    (IXGBE_VF_SET_MACVLAN | IXGBE_VT_MSGTYPE_NACK))
			ret_val = -ENOMEM;

	return ret_val;
}

static s32 ixgbevf_hv_set_uc_addr_vf(struct ixgbe_hw *hw, u32 index, u8 *addr)
{
	return -EOPNOTSUPP;
}

/**
 * ixgbevf_get_reta_locked - get the RSS redirection table (RETA) contents.
 * @adapter: pointer to the port handle
 * @reta: buffer to fill with RETA contents.
 * @num_rx_queues: Number of Rx queues configured for this port
 *
 * The "reta" buffer should be big enough to contain 32 registers.
 *
 * Returns: 0 on success.
 *          if API doesn't support this operation - (-EOPNOTSUPP).
 */
int ixgbevf_get_reta_locked(struct ixgbe_hw *hw, u32 *reta, int num_rx_queues)
{
	int err, i, j;
	u32 msgbuf[IXGBE_VFMAILBOX_SIZE];
	u32 *hw_reta = &msgbuf[1];
	u32 mask = 0;

	/* We have to use a mailbox for 82599 and x540 devices only.
	 * For these devices RETA has 128 entries.
	 * Also these VFs support up to 4 RSS queues. Therefore PF will compress
	 * 16 RETA entries in each DWORD giving 2 bits to each entry.
	 */
	int dwords = IXGBEVF_82599_RETA_SIZE / 16;

	/* We support the RSS querying for 82599 and x540 devices only.
	 * Thus return an error if API doesn't support RETA querying or querying
	 * is not supported for this device type.
	 */
	if (hw->api_version != ixgbe_mbox_api_12 ||
	    hw->mac.type >= ixgbe_mac_X550_vf)
		return -EOPNOTSUPP;

	msgbuf[0] = IXGBE_VF_GET_RETA;

	err = hw->mbx.ops.write_posted(hw, msgbuf, 1);

	if (err)
		return err;

	err = hw->mbx.ops.read_posted(hw, msgbuf, dwords + 1);

	if (err)
		return err;

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

	/* If the operation has been refused by a PF return -EPERM */
	if (msgbuf[0] == (IXGBE_VF_GET_RETA | IXGBE_VT_MSGTYPE_NACK))
		return -EPERM;

	/* If we didn't get an ACK there must have been
	 * some sort of mailbox error so we should treat it
	 * as such.
	 */
	if (msgbuf[0] != (IXGBE_VF_GET_RETA | IXGBE_VT_MSGTYPE_ACK))
		return IXGBE_ERR_MBX;

	/* ixgbevf doesn't support more than 2 queues at the moment */
	if (num_rx_queues > 1)
		mask = 0x1;

	for (i = 0; i < dwords; i++)
		for (j = 0; j < 16; j++)
			reta[i * 16 + j] = (hw_reta[i] >> (2 * j)) & mask;

	return 0;
}

/**
 * ixgbevf_get_rss_key_locked - get the RSS Random Key
 * @hw: pointer to the HW structure
 * @rss_key: buffer to fill with RSS Hash Key contents.
 *
 * The "rss_key" buffer should be big enough to contain 10 registers.
 *
 * Returns: 0 on success.
 *          if API doesn't support this operation - (-EOPNOTSUPP).
 */
int ixgbevf_get_rss_key_locked(struct ixgbe_hw *hw, u8 *rss_key)
{
	int err;
	u32 msgbuf[IXGBE_VFMAILBOX_SIZE];

	/* We currently support the RSS Random Key retrieval for 82599 and x540
	 * devices only.
	 *
	 * Thus return an error if API doesn't support RSS Random Key retrieval
	 * or if the operation is not supported for this device type.
	 */
	if (hw->api_version != ixgbe_mbox_api_12 ||
	    hw->mac.type >= ixgbe_mac_X550_vf)
		return -EOPNOTSUPP;

	msgbuf[0] = IXGBE_VF_GET_RSS_KEY;
	err = hw->mbx.ops.write_posted(hw, msgbuf, 1);

	if (err)
		return err;

	err = hw->mbx.ops.read_posted(hw, msgbuf, 11);

	if (err)
		return err;

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

	/* If the operation has been refused by a PF return -EPERM */
	if (msgbuf[0] == (IXGBE_VF_GET_RETA | IXGBE_VT_MSGTYPE_NACK))
		return -EPERM;

	/* If we didn't get an ACK there must have been
	 * some sort of mailbox error so we should treat it
	 * as such.
	 */
	if (msgbuf[0] != (IXGBE_VF_GET_RSS_KEY | IXGBE_VT_MSGTYPE_ACK))
		return IXGBE_ERR_MBX;

	memcpy(rss_key, msgbuf + 1, IXGBEVF_RSS_HASH_KEY_SIZE);

	return 0;
}

/**
 *  ixgbevf_set_rar_vf - set device MAC address
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: Unused in this implementation
 **/
static s32 ixgbevf_set_rar_vf(struct ixgbe_hw *hw, u32 index, u8 *addr,
			      u32 vmdq)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[3];
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	s32 ret_val;

	memset(msgbuf, 0, sizeof(msgbuf));
	msgbuf[0] = IXGBE_VF_SET_MAC_ADDR;
	ether_addr_copy(msg_addr, addr);
	ret_val = mbx->ops.write_posted(hw, msgbuf, 3);

	if (!ret_val)
		ret_val = mbx->ops.read_posted(hw, msgbuf, 3);

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (!ret_val &&
	    (msgbuf[0] == (IXGBE_VF_SET_MAC_ADDR | IXGBE_VT_MSGTYPE_NACK))) {
		ixgbevf_get_mac_addr_vf(hw, hw->mac.addr);
		return IXGBE_ERR_MBX;
	}

	return ret_val;
}

/**
 *  ixgbevf_hv_set_rar_vf - set device MAC address Hyper-V variant
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: Unused in this implementation
 *
 * We don't really allow setting the device MAC address. However,
 * if the address being set is the permanent MAC address we will
 * permit that.
 **/
static s32 ixgbevf_hv_set_rar_vf(struct ixgbe_hw *hw, u32 index, u8 *addr,
				 u32 vmdq)
{
	if (ether_addr_equal(addr, hw->mac.perm_addr))
		return 0;

	return -EOPNOTSUPP;
}

static void ixgbevf_write_msg_read_ack(struct ixgbe_hw *hw,
				       u32 *msg, u16 size)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 retmsg[IXGBE_VFMAILBOX_SIZE];
	s32 retval = mbx->ops.write_posted(hw, msg, size);

	if (!retval)
		mbx->ops.read_posted(hw, retmsg, size);
}

/**
 *  ixgbevf_update_mc_addr_list_vf - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @netdev: pointer to net device structure
 *
 *  Updates the Multicast Table Array.
 **/
static s32 ixgbevf_update_mc_addr_list_vf(struct ixgbe_hw *hw,
					  struct net_device *netdev)
{
	struct netdev_hw_addr *ha;
	u32 msgbuf[IXGBE_VFMAILBOX_SIZE];
	u16 *vector_list = (u16 *)&msgbuf[1];
	u32 cnt, i;

	/* Each entry in the list uses 1 16 bit word.  We have 30
	 * 16 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 30 hash values if we pack 'em right.  If
	 * there are more than 30 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 30 later.
	 * It would be unusual for a server to request that many multi-cast
	 * addresses except for in large enterprise network environments.
	 */

	cnt = netdev_mc_count(netdev);
	if (cnt > 30)
		cnt = 30;
	msgbuf[0] = IXGBE_VF_SET_MULTICAST;
	msgbuf[0] |= cnt << IXGBE_VT_MSGINFO_SHIFT;

	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == cnt)
			break;
		if (is_link_local_ether_addr(ha->addr))
			continue;

		vector_list[i++] = ixgbevf_mta_vector(hw, ha->addr);
	}

	ixgbevf_write_msg_read_ack(hw, msgbuf, IXGBE_VFMAILBOX_SIZE);

	return 0;
}

/**
 * Hyper-V variant - just a stub.
 */
static s32 ixgbevf_hv_update_mc_addr_list_vf(struct ixgbe_hw *hw,
					     struct net_device *netdev)
{
	return -EOPNOTSUPP;
}

/**
 *  ixgbevf_update_xcast_mode - Update Multicast mode
 *  @hw: pointer to the HW structure
 *  @netdev: pointer to net device structure
 *  @xcast_mode: new multicast mode
 *
 *  Updates the Multicast Mode of VF.
 **/
static s32 ixgbevf_update_xcast_mode(struct ixgbe_hw *hw,
				     struct net_device *netdev, int xcast_mode)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;

	switch (hw->api_version) {
	case ixgbe_mbox_api_12:
		break;
	default:
		return -EOPNOTSUPP;
	}

	msgbuf[0] = IXGBE_VF_UPDATE_XCAST_MODE;
	msgbuf[1] = xcast_mode;

	err = mbx->ops.write_posted(hw, msgbuf, 2);
	if (err)
		return err;

	err = mbx->ops.read_posted(hw, msgbuf, 2);
	if (err)
		return err;

	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;
	if (msgbuf[0] == (IXGBE_VF_UPDATE_XCAST_MODE | IXGBE_VT_MSGTYPE_NACK))
		return -EPERM;

	return 0;
}

/**
 * Hyper-V variant - just a stub.
 */
static s32 ixgbevf_hv_update_xcast_mode(struct ixgbe_hw *hw,
					struct net_device *netdev,
					int xcast_mode)
{
	return -EOPNOTSUPP;
}

/**
 *  ixgbevf_set_vfta_vf - Set/Unset VLAN filter table address
 *  @hw: pointer to the HW structure
 *  @vlan: 12 bit VLAN ID
 *  @vind: unused by VF drivers
 *  @vlan_on: if true then set bit, else clear bit
 **/
static s32 ixgbevf_set_vfta_vf(struct ixgbe_hw *hw, u32 vlan, u32 vind,
			       bool vlan_on)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	u32 msgbuf[2];
	s32 err;

	msgbuf[0] = IXGBE_VF_SET_VLAN;
	msgbuf[1] = vlan;
	/* Setting the 8 bit field MSG INFO to TRUE indicates "add" */
	msgbuf[0] |= vlan_on << IXGBE_VT_MSGINFO_SHIFT;

	err = mbx->ops.write_posted(hw, msgbuf, 2);
	if (err)
		goto mbx_err;

	err = mbx->ops.read_posted(hw, msgbuf, 2);
	if (err)
		goto mbx_err;

	/* remove extra bits from the message */
	msgbuf[0] &= ~IXGBE_VT_MSGTYPE_CTS;
	msgbuf[0] &= ~(0xFF << IXGBE_VT_MSGINFO_SHIFT);

	if (msgbuf[0] != (IXGBE_VF_SET_VLAN | IXGBE_VT_MSGTYPE_ACK))
		err = IXGBE_ERR_INVALID_ARGUMENT;

mbx_err:
	return err;
}

/**
 * Hyper-V variant - just a stub.
 */
static s32 ixgbevf_hv_set_vfta_vf(struct ixgbe_hw *hw, u32 vlan, u32 vind,
				  bool vlan_on)
{
	return -EOPNOTSUPP;
}

/**
 *  ixgbevf_setup_mac_link_vf - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *  @speed: Unused in this implementation
 *  @autoneg: Unused in this implementation
 *  @autoneg_wait_to_complete: Unused in this implementation
 *
 *  Do nothing and return success.  VF drivers are not allowed to change
 *  global settings.  Maintained for driver compatibility.
 **/
static s32 ixgbevf_setup_mac_link_vf(struct ixgbe_hw *hw,
				     ixgbe_link_speed speed, bool autoneg,
				     bool autoneg_wait_to_complete)
{
	return 0;
}

/**
 *  ixgbevf_check_mac_link_vf - Get link/speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: true is link is up, false otherwise
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
static s32 ixgbevf_check_mac_link_vf(struct ixgbe_hw *hw,
				     ixgbe_link_speed *speed,
				     bool *link_up,
				     bool autoneg_wait_to_complete)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	struct ixgbe_mac_info *mac = &hw->mac;
	s32 ret_val = 0;
	u32 links_reg;
	u32 in_msg = 0;

	/* If we were hit with a reset drop the link */
	if (!mbx->ops.check_for_rst(hw) || !mbx->timeout)
		mac->get_link_status = true;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);
	if (!(links_reg & IXGBE_LINKS_UP))
		goto out;

	/* for SFP+ modules and DA cables on 82599 it can take up to 500usecs
	 * before the link status is correct
	 */
	if (mac->type == ixgbe_mac_82599_vf) {
		int i;

		for (i = 0; i < 5; i++) {
			udelay(100);
			links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);

			if (!(links_reg & IXGBE_LINKS_UP))
				goto out;
		}
	}

	switch (links_reg & IXGBE_LINKS_SPEED_82599) {
	case IXGBE_LINKS_SPEED_10G_82599:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_1G_82599:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_100_82599:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		break;
	}

	/* if the read failed it could just be a mailbox collision, best wait
	 * until we are called again and don't report an error
	 */
	if (mbx->ops.read(hw, &in_msg, 1))
		goto out;

	if (!(in_msg & IXGBE_VT_MSGTYPE_CTS)) {
		/* msg is not CTS and is NACK we must have lost CTS status */
		if (in_msg & IXGBE_VT_MSGTYPE_NACK)
			ret_val = -1;
		goto out;
	}

	/* the pf is talking, if we timed out in the past we reinit */
	if (!mbx->timeout) {
		ret_val = -1;
		goto out;
	}

	/* if we passed all the tests above then the link is up and we no
	 * longer need to check for link
	 */
	mac->get_link_status = false;

out:
	*link_up = !mac->get_link_status;
	return ret_val;
}

/**
 * Hyper-V variant; there is no mailbox communication.
 */
static s32 ixgbevf_hv_check_mac_link_vf(struct ixgbe_hw *hw,
					ixgbe_link_speed *speed,
					bool *link_up,
					bool autoneg_wait_to_complete)
{
	struct ixgbe_mbx_info *mbx = &hw->mbx;
	struct ixgbe_mac_info *mac = &hw->mac;
	u32 links_reg;

	/* If we were hit with a reset drop the link */
	if (!mbx->ops.check_for_rst(hw) || !mbx->timeout)
		mac->get_link_status = true;

	if (!mac->get_link_status)
		goto out;

	/* if link status is down no point in checking to see if pf is up */
	links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);
	if (!(links_reg & IXGBE_LINKS_UP))
		goto out;

	/* for SFP+ modules and DA cables on 82599 it can take up to 500usecs
	 * before the link status is correct
	 */
	if (mac->type == ixgbe_mac_82599_vf) {
		int i;

		for (i = 0; i < 5; i++) {
			udelay(100);
			links_reg = IXGBE_READ_REG(hw, IXGBE_VFLINKS);

			if (!(links_reg & IXGBE_LINKS_UP))
				goto out;
		}
	}

	switch (links_reg & IXGBE_LINKS_SPEED_82599) {
	case IXGBE_LINKS_SPEED_10G_82599:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_1G_82599:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		break;
	case IXGBE_LINKS_SPEED_100_82599:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		break;
	}

	/* if we passed all the tests above then the link is up and we no
	 * longer need to check for link
	 */
	mac->get_link_status = false;

out:
	*link_up = !mac->get_link_status;
	return 0;
}

/**
 *  ixgbevf_set_rlpml_vf - Set the maximum receive packet length
 *  @hw: pointer to the HW structure
 *  @max_size: value to assign to max frame size
 **/
static void ixgbevf_set_rlpml_vf(struct ixgbe_hw *hw, u16 max_size)
{
	u32 msgbuf[2];

	msgbuf[0] = IXGBE_VF_SET_LPE;
	msgbuf[1] = max_size;
	ixgbevf_write_msg_read_ack(hw, msgbuf, 2);
}

/**
 * ixgbevf_hv_set_rlpml_vf - Set the maximum receive packet length
 * @hw: pointer to the HW structure
 * @max_size: value to assign to max frame size
 * Hyper-V variant.
 **/
static void ixgbevf_hv_set_rlpml_vf(struct ixgbe_hw *hw, u16 max_size)
{
	u32 reg;

	/* If we are on Hyper-V, we implement this functionality
	 * differently.
	 */
	reg =  IXGBE_READ_REG(hw, IXGBE_VFRXDCTL(0));
	/* CRC == 4 */
	reg |= ((max_size + 4) | IXGBE_RXDCTL_RLPML_EN);
	IXGBE_WRITE_REG(hw, IXGBE_VFRXDCTL(0), reg);
}

/**
 *  ixgbevf_negotiate_api_version_vf - Negotiate supported API version
 *  @hw: pointer to the HW structure
 *  @api: integer containing requested API version
 **/
static int ixgbevf_negotiate_api_version_vf(struct ixgbe_hw *hw, int api)
{
	int err;
	u32 msg[3];

	/* Negotiate the mailbox API version */
	msg[0] = IXGBE_VF_API_NEGOTIATE;
	msg[1] = api;
	msg[2] = 0;
	err = hw->mbx.ops.write_posted(hw, msg, 3);

	if (!err)
		err = hw->mbx.ops.read_posted(hw, msg, 3);

	if (!err) {
		msg[0] &= ~IXGBE_VT_MSGTYPE_CTS;

		/* Store value and return 0 on success */
		if (msg[0] == (IXGBE_VF_API_NEGOTIATE | IXGBE_VT_MSGTYPE_ACK)) {
			hw->api_version = api;
			return 0;
		}

		err = IXGBE_ERR_INVALID_ARGUMENT;
	}

	return err;
}

/**
 *  ixgbevf_hv_negotiate_api_version_vf - Negotiate supported API version
 *  @hw: pointer to the HW structure
 *  @api: integer containing requested API version
 *  Hyper-V version - only ixgbe_mbox_api_10 supported.
 **/
static int ixgbevf_hv_negotiate_api_version_vf(struct ixgbe_hw *hw, int api)
{
	/* Hyper-V only supports api version ixgbe_mbox_api_10 */
	if (api != ixgbe_mbox_api_10)
		return IXGBE_ERR_INVALID_ARGUMENT;

	return 0;
}

int ixgbevf_get_queues(struct ixgbe_hw *hw, unsigned int *num_tcs,
		       unsigned int *default_tc)
{
	int err;
	u32 msg[5];

	/* do nothing if API doesn't support ixgbevf_get_queues */
	switch (hw->api_version) {
	case ixgbe_mbox_api_11:
	case ixgbe_mbox_api_12:
		break;
	default:
		return 0;
	}

	/* Fetch queue configuration from the PF */
	msg[0] = IXGBE_VF_GET_QUEUE;
	msg[1] = msg[2] = msg[3] = msg[4] = 0;
	err = hw->mbx.ops.write_posted(hw, msg, 5);

	if (!err)
		err = hw->mbx.ops.read_posted(hw, msg, 5);

	if (!err) {
		msg[0] &= ~IXGBE_VT_MSGTYPE_CTS;

		/* if we we didn't get an ACK there must have been
		 * some sort of mailbox error so we should treat it
		 * as such
		 */
		if (msg[0] != (IXGBE_VF_GET_QUEUE | IXGBE_VT_MSGTYPE_ACK))
			return IXGBE_ERR_MBX;

		/* record and validate values from message */
		hw->mac.max_tx_queues = msg[IXGBE_VF_TX_QUEUES];
		if (hw->mac.max_tx_queues == 0 ||
		    hw->mac.max_tx_queues > IXGBE_VF_MAX_TX_QUEUES)
			hw->mac.max_tx_queues = IXGBE_VF_MAX_TX_QUEUES;

		hw->mac.max_rx_queues = msg[IXGBE_VF_RX_QUEUES];
		if (hw->mac.max_rx_queues == 0 ||
		    hw->mac.max_rx_queues > IXGBE_VF_MAX_RX_QUEUES)
			hw->mac.max_rx_queues = IXGBE_VF_MAX_RX_QUEUES;

		*num_tcs = msg[IXGBE_VF_TRANS_VLAN];
		/* in case of unknown state assume we cannot tag frames */
		if (*num_tcs > hw->mac.max_rx_queues)
			*num_tcs = 1;

		*default_tc = msg[IXGBE_VF_DEF_QUEUE];
		/* default to queue 0 on out-of-bounds queue number */
		if (*default_tc >= hw->mac.max_tx_queues)
			*default_tc = 0;
	}

	return err;
}

static const struct ixgbe_mac_operations ixgbevf_mac_ops = {
	.init_hw		= ixgbevf_init_hw_vf,
	.reset_hw		= ixgbevf_reset_hw_vf,
	.start_hw		= ixgbevf_start_hw_vf,
	.get_mac_addr		= ixgbevf_get_mac_addr_vf,
	.stop_adapter		= ixgbevf_stop_hw_vf,
	.setup_link		= ixgbevf_setup_mac_link_vf,
	.check_link		= ixgbevf_check_mac_link_vf,
	.negotiate_api_version	= ixgbevf_negotiate_api_version_vf,
	.set_rar		= ixgbevf_set_rar_vf,
	.update_mc_addr_list	= ixgbevf_update_mc_addr_list_vf,
	.update_xcast_mode	= ixgbevf_update_xcast_mode,
	.set_uc_addr		= ixgbevf_set_uc_addr_vf,
	.set_vfta		= ixgbevf_set_vfta_vf,
	.set_rlpml		= ixgbevf_set_rlpml_vf,
};

static const struct ixgbe_mac_operations ixgbevf_hv_mac_ops = {
	.init_hw		= ixgbevf_init_hw_vf,
	.reset_hw		= ixgbevf_hv_reset_hw_vf,
	.start_hw		= ixgbevf_start_hw_vf,
	.get_mac_addr		= ixgbevf_get_mac_addr_vf,
	.stop_adapter		= ixgbevf_stop_hw_vf,
	.setup_link		= ixgbevf_setup_mac_link_vf,
	.check_link		= ixgbevf_hv_check_mac_link_vf,
	.negotiate_api_version	= ixgbevf_hv_negotiate_api_version_vf,
	.set_rar		= ixgbevf_hv_set_rar_vf,
	.update_mc_addr_list	= ixgbevf_hv_update_mc_addr_list_vf,
	.update_xcast_mode	= ixgbevf_hv_update_xcast_mode,
	.set_uc_addr		= ixgbevf_hv_set_uc_addr_vf,
	.set_vfta		= ixgbevf_hv_set_vfta_vf,
	.set_rlpml		= ixgbevf_hv_set_rlpml_vf,
};

const struct ixgbevf_info ixgbevf_82599_vf_info = {
	.mac = ixgbe_mac_82599_vf,
	.mac_ops = &ixgbevf_mac_ops,
};

const struct ixgbevf_info ixgbevf_82599_vf_hv_info = {
	.mac = ixgbe_mac_82599_vf,
	.mac_ops = &ixgbevf_hv_mac_ops,
};

const struct ixgbevf_info ixgbevf_X540_vf_info = {
	.mac = ixgbe_mac_X540_vf,
	.mac_ops = &ixgbevf_mac_ops,
};

const struct ixgbevf_info ixgbevf_X540_vf_hv_info = {
	.mac = ixgbe_mac_X540_vf,
	.mac_ops = &ixgbevf_hv_mac_ops,
};

const struct ixgbevf_info ixgbevf_X550_vf_info = {
	.mac = ixgbe_mac_X550_vf,
	.mac_ops = &ixgbevf_mac_ops,
};

const struct ixgbevf_info ixgbevf_X550_vf_hv_info = {
	.mac = ixgbe_mac_X550_vf,
	.mac_ops = &ixgbevf_hv_mac_ops,
};

const struct ixgbevf_info ixgbevf_X550EM_x_vf_info = {
	.mac = ixgbe_mac_X550EM_x_vf,
	.mac_ops = &ixgbevf_mac_ops,
};

const struct ixgbevf_info ixgbevf_X550EM_x_vf_hv_info = {
	.mac = ixgbe_mac_X550EM_x_vf,
	.mac_ops = &ixgbevf_hv_mac_ops,
};
