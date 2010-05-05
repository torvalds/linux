/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2009 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/


#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/ipv6.h>
#ifdef NETIF_F_HW_VLAN_TX
#include <linux/if_vlan.h>
#endif

#include "ixgbe.h"

#include "ixgbe_sriov.h"

int ixgbe_set_vf_multicasts(struct ixgbe_adapter *adapter,
			    int entries, u16 *hash_list, u32 vf)
{
	struct vf_data_storage *vfinfo = &adapter->vfinfo[vf];
	struct ixgbe_hw *hw = &adapter->hw;
	int i;
	u32 vector_bit;
	u32 vector_reg;
	u32 mta_reg;

	/* only so many hash values supported */
	entries = min(entries, IXGBE_MAX_VF_MC_ENTRIES);

	/*
	 * salt away the number of multi cast addresses assigned
	 * to this VF for later use to restore when the PF multi cast
	 * list changes
	 */
	vfinfo->num_vf_mc_hashes = entries;

	/*
	 * VFs are limited to using the MTA hash table for their multicast
	 * addresses
	 */
	for (i = 0; i < entries; i++) {
		vfinfo->vf_mc_hashes[i] = hash_list[i];;
	}

	for (i = 0; i < vfinfo->num_vf_mc_hashes; i++) {
		vector_reg = (vfinfo->vf_mc_hashes[i] >> 5) & 0x7F;
		vector_bit = vfinfo->vf_mc_hashes[i] & 0x1F;
		mta_reg = IXGBE_READ_REG(hw, IXGBE_MTA(vector_reg));
		mta_reg |= (1 << vector_bit);
		IXGBE_WRITE_REG(hw, IXGBE_MTA(vector_reg), mta_reg);
	}

	return 0;
}

void ixgbe_restore_vf_multicasts(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct vf_data_storage *vfinfo;
	int i, j;
	u32 vector_bit;
	u32 vector_reg;
	u32 mta_reg;

	for (i = 0; i < adapter->num_vfs; i++) {
		vfinfo = &adapter->vfinfo[i];
		for (j = 0; j < vfinfo->num_vf_mc_hashes; j++) {
			hw->addr_ctrl.mta_in_use++;
			vector_reg = (vfinfo->vf_mc_hashes[j] >> 5) & 0x7F;
			vector_bit = vfinfo->vf_mc_hashes[j] & 0x1F;
			mta_reg = IXGBE_READ_REG(hw, IXGBE_MTA(vector_reg));
			mta_reg |= (1 << vector_bit);
			IXGBE_WRITE_REG(hw, IXGBE_MTA(vector_reg), mta_reg);
		}
	}
}

int ixgbe_set_vf_vlan(struct ixgbe_adapter *adapter, int add, int vid, u32 vf)
{
	return adapter->hw.mac.ops.set_vfta(&adapter->hw, vid, vf, (bool)add);
}


void ixgbe_set_vmolr(struct ixgbe_hw *hw, u32 vf, bool aupe)
{
	u32 vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf));
	vmolr |= (IXGBE_VMOLR_ROMPE |
		  IXGBE_VMOLR_ROPE |
		  IXGBE_VMOLR_BAM);
	if (aupe)
		vmolr |= IXGBE_VMOLR_AUPE;
	else
		vmolr &= ~IXGBE_VMOLR_AUPE;
	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);
}

static void ixgbe_set_vmvir(struct ixgbe_adapter *adapter, u32 vid, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;

	if (vid)
		IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf),
				(vid | IXGBE_VMVIR_VLANA_DEFAULT));
	else
		IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf), 0);
}

inline void ixgbe_vf_reset_event(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;

	/* reset offloads to defaults */
	if (adapter->vfinfo[vf].pf_vlan) {
		ixgbe_set_vf_vlan(adapter, true,
				  adapter->vfinfo[vf].pf_vlan, vf);
		ixgbe_set_vmvir(adapter,
				(adapter->vfinfo[vf].pf_vlan |
				 (adapter->vfinfo[vf].pf_qos <<
				  VLAN_PRIO_SHIFT)), vf);
		ixgbe_set_vmolr(hw, vf, false);
	} else {
		ixgbe_set_vmvir(adapter, 0, vf);
		ixgbe_set_vmolr(hw, vf, true);
	}

	/* reset multicast table array for vf */
	adapter->vfinfo[vf].num_vf_mc_hashes = 0;

	/* Flush and reset the mta with the new values */
	ixgbe_set_rx_mode(adapter->netdev);

	if (adapter->vfinfo[vf].rar > 0) {
		adapter->hw.mac.ops.clear_rar(&adapter->hw,
		                              adapter->vfinfo[vf].rar);
		adapter->vfinfo[vf].rar = -1;
	}
}

int ixgbe_set_vf_mac(struct ixgbe_adapter *adapter,
                          int vf, unsigned char *mac_addr)
{
	struct ixgbe_hw *hw = &adapter->hw;

	adapter->vfinfo[vf].rar = hw->mac.ops.set_rar(hw, vf + 1, mac_addr,
	                                              vf, IXGBE_RAH_AV);
	if (adapter->vfinfo[vf].rar < 0) {
		DPRINTK(DRV, ERR, "Could not set MAC Filter for VF %d\n", vf);
		return -1;
	}

	memcpy(adapter->vfinfo[vf].vf_mac_addresses, mac_addr, 6);

	return 0;
}

int ixgbe_vf_configuration(struct pci_dev *pdev, unsigned int event_mask)
{
	unsigned char vf_mac_addr[6];
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	unsigned int vfn = (event_mask & 0x3f);

	bool enable = ((event_mask & 0x10000000U) != 0);

	if (enable) {
		random_ether_addr(vf_mac_addr);
		DPRINTK(PROBE, INFO, "IOV: VF %d is enabled "
		       "mac %02X:%02X:%02X:%02X:%02X:%02X\n",
		       vfn,
		       vf_mac_addr[0], vf_mac_addr[1], vf_mac_addr[2],
		       vf_mac_addr[3], vf_mac_addr[4], vf_mac_addr[5]);
		/*
		 * Store away the VF "permananet" MAC address, it will ask
		 * for it later.
		 */
		memcpy(adapter->vfinfo[vfn].vf_mac_addresses, vf_mac_addr, 6);
	}

	return 0;
}

inline void ixgbe_vf_reset_msg(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 reg;
	u32 reg_offset, vf_shift;

	vf_shift = vf % 32;
	reg_offset = vf / 32;

	/* enable transmit and receive for vf */
	reg = IXGBE_READ_REG(hw, IXGBE_VFTE(reg_offset));
	reg |= (reg | (1 << vf_shift));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_offset), reg);

	reg = IXGBE_READ_REG(hw, IXGBE_VFRE(reg_offset));
	reg |= (reg | (1 << vf_shift));
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_offset), reg);

	ixgbe_vf_reset_event(adapter, vf);
}

static int ixgbe_rcv_msg_from_vf(struct ixgbe_adapter *adapter, u32 vf)
{
	u32 mbx_size = IXGBE_VFMAILBOX_SIZE;
	u32 msgbuf[mbx_size];
	struct ixgbe_hw *hw = &adapter->hw;
	s32 retval;
	int entries;
	u16 *hash_list;
	int add, vid;

	retval = ixgbe_read_mbx(hw, msgbuf, mbx_size, vf);

	if (retval)
		printk(KERN_ERR "Error receiving message from VF\n");

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (IXGBE_VT_MSGTYPE_ACK | IXGBE_VT_MSGTYPE_NACK))
		return retval;

	/*
	 * until the vf completes a virtual function reset it should not be
	 * allowed to start any configuration.
	 */

	if (msgbuf[0] == IXGBE_VF_RESET) {
		unsigned char *vf_mac = adapter->vfinfo[vf].vf_mac_addresses;
		u8 *addr = (u8 *)(&msgbuf[1]);
		DPRINTK(PROBE, INFO, "VF Reset msg received from vf %d\n", vf);
		adapter->vfinfo[vf].clear_to_send = false;
		ixgbe_vf_reset_msg(adapter, vf);
		adapter->vfinfo[vf].clear_to_send = true;

		/* reply to reset with ack and vf mac address */
		msgbuf[0] = IXGBE_VF_RESET | IXGBE_VT_MSGTYPE_ACK;
		memcpy(addr, vf_mac, IXGBE_ETH_LENGTH_OF_ADDRESS);
		/*
		 * Piggyback the multicast filter type so VF can compute the
		 * correct vectors
		 */
		msgbuf[3] = hw->mac.mc_filter_type;
		ixgbe_write_mbx(hw, msgbuf, IXGBE_VF_PERMADDR_MSG_LEN, vf);

		return retval;
	}

	if (!adapter->vfinfo[vf].clear_to_send) {
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
		ixgbe_write_mbx(hw, msgbuf, 1, vf);
		return retval;
	}

	switch ((msgbuf[0] & 0xFFFF)) {
	case IXGBE_VF_SET_MAC_ADDR:
		{
			u8 *new_mac = ((u8 *)(&msgbuf[1]));
			if (is_valid_ether_addr(new_mac) &&
			    !adapter->vfinfo[vf].pf_set_mac)
				ixgbe_set_vf_mac(adapter, vf, new_mac);
			else
				ixgbe_set_vf_mac(adapter,
				  vf, adapter->vfinfo[vf].vf_mac_addresses);
		}
		break;
	case IXGBE_VF_SET_MULTICAST:
		entries = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK)
		          >> IXGBE_VT_MSGINFO_SHIFT;
		hash_list = (u16 *)&msgbuf[1];
		retval = ixgbe_set_vf_multicasts(adapter, entries,
		                                 hash_list, vf);
		break;
	case IXGBE_VF_SET_LPE:
		WARN_ON((msgbuf[0] & 0xFFFF) == IXGBE_VF_SET_LPE);
		break;
	case IXGBE_VF_SET_VLAN:
		add = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK)
		      >> IXGBE_VT_MSGINFO_SHIFT;
		vid = (msgbuf[1] & IXGBE_VLVF_VLANID_MASK);
		retval = ixgbe_set_vf_vlan(adapter, add, vid, vf);
		break;
	default:
		DPRINTK(DRV, ERR, "Unhandled Msg %8.8x\n", msgbuf[0]);
		retval = IXGBE_ERR_MBX;
		break;
	}

	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= IXGBE_VT_MSGTYPE_ACK;

	msgbuf[0] |= IXGBE_VT_MSGTYPE_CTS;

	ixgbe_write_mbx(hw, msgbuf, 1, vf);

	return retval;
}

static void ixgbe_rcv_ack_from_vf(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 msg = IXGBE_VT_MSGTYPE_NACK;

	/* if device isn't clear to send it shouldn't be reading either */
	if (!adapter->vfinfo[vf].clear_to_send)
		ixgbe_write_mbx(hw, &msg, 1, vf);
}

void ixgbe_msg_task(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 vf;

	for (vf = 0; vf < adapter->num_vfs; vf++) {
		/* process any reset requests */
		if (!ixgbe_check_for_rst(hw, vf))
			ixgbe_vf_reset_event(adapter, vf);

		/* process any messages pending */
		if (!ixgbe_check_for_msg(hw, vf))
			ixgbe_rcv_msg_from_vf(adapter, vf);

		/* process any acks */
		if (!ixgbe_check_for_ack(hw, vf))
			ixgbe_rcv_ack_from_vf(adapter, vf);
	}
}

void ixgbe_disable_tx_rx(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	/* disable transmit and receive for all vfs */
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(0), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(1), 0);

	IXGBE_WRITE_REG(hw, IXGBE_VFRE(0), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(1), 0);
}

void ixgbe_ping_all_vfs(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 ping;
	int i;

	for (i = 0 ; i < adapter->num_vfs; i++) {
		ping = IXGBE_PF_CONTROL_MSG;
		if (adapter->vfinfo[i].clear_to_send)
			ping |= IXGBE_VT_MSGTYPE_CTS;
		ixgbe_write_mbx(hw, &ping, 1, i);
	}
}

int ixgbe_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	if (!is_valid_ether_addr(mac) || (vf >= adapter->num_vfs))
		return -EINVAL;
	adapter->vfinfo[vf].pf_set_mac = true;
	dev_info(&adapter->pdev->dev, "setting MAC %pM on VF %d\n", mac, vf);
	dev_info(&adapter->pdev->dev, "Reload the VF driver to make this"
				      " change effective.");
	if (test_bit(__IXGBE_DOWN, &adapter->state)) {
		dev_warn(&adapter->pdev->dev, "The VF MAC address has been set,"
			 " but the PF device is not up.\n");
		dev_warn(&adapter->pdev->dev, "Bring the PF device up before"
			 " attempting to use the VF device.\n");
	}
	return ixgbe_set_vf_mac(adapter, vf, mac);
}

int ixgbe_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos)
{
	int err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if ((vf >= adapter->num_vfs) || (vlan > 4095) || (qos > 7))
		return -EINVAL;
	if (vlan || qos) {
		err = ixgbe_set_vf_vlan(adapter, true, vlan, vf);
		if (err)
			goto out;
		ixgbe_set_vmvir(adapter, vlan | (qos << VLAN_PRIO_SHIFT), vf);
		ixgbe_set_vmolr(&adapter->hw, vf, false);
		adapter->vfinfo[vf].pf_vlan = vlan;
		adapter->vfinfo[vf].pf_qos = qos;
		dev_info(&adapter->pdev->dev,
			 "Setting VLAN %d, QOS 0x%x on VF %d\n", vlan, qos, vf);
		if (test_bit(__IXGBE_DOWN, &adapter->state)) {
			dev_warn(&adapter->pdev->dev,
				 "The VF VLAN has been set,"
				 " but the PF device is not up.\n");
			dev_warn(&adapter->pdev->dev,
				 "Bring the PF device up before"
				 " attempting to use the VF device.\n");
		}
	} else {
		err = ixgbe_set_vf_vlan(adapter, false,
					adapter->vfinfo[vf].pf_vlan, vf);
		ixgbe_set_vmvir(adapter, vlan, vf);
		ixgbe_set_vmolr(&adapter->hw, vf, true);
		adapter->vfinfo[vf].pf_vlan = 0;
		adapter->vfinfo[vf].pf_qos = 0;
       }
out:
       return err;
}

int ixgbe_ndo_set_vf_bw(struct net_device *netdev, int vf, int tx_rate)
{
	return -EOPNOTSUPP;
}

int ixgbe_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	if (vf >= adapter->num_vfs)
		return -EINVAL;
	ivi->vf = vf;
	memcpy(&ivi->mac, adapter->vfinfo[vf].vf_mac_addresses, ETH_ALEN);
	ivi->tx_rate = 0;
	ivi->vlan = adapter->vfinfo[vf].pf_vlan;
	ivi->qos = adapter->vfinfo[vf].pf_qos;
	return 0;
}
