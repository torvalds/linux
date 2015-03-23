/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2014 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
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
#ifdef NETIF_F_HW_VLAN_CTAG_TX
#include <linux/if_vlan.h>
#endif

#include "ixgbe.h"
#include "ixgbe_type.h"
#include "ixgbe_sriov.h"

#ifdef CONFIG_PCI_IOV
static int __ixgbe_enable_sriov(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int num_vf_macvlans, i;
	struct vf_macvlans *mv_list;

	adapter->flags |= IXGBE_FLAG_SRIOV_ENABLED;
	e_info(probe, "SR-IOV enabled with %d VFs\n", adapter->num_vfs);

	/* Enable VMDq flag so device will be set in VM mode */
	adapter->flags |= IXGBE_FLAG_VMDQ_ENABLED;
	if (!adapter->ring_feature[RING_F_VMDQ].limit)
		adapter->ring_feature[RING_F_VMDQ].limit = 1;
	adapter->ring_feature[RING_F_VMDQ].offset = adapter->num_vfs;

	num_vf_macvlans = hw->mac.num_rar_entries -
	(IXGBE_MAX_PF_MACVLANS + 1 + adapter->num_vfs);

	adapter->mv_list = mv_list = kcalloc(num_vf_macvlans,
					     sizeof(struct vf_macvlans),
					     GFP_KERNEL);
	if (mv_list) {
		/* Initialize list of VF macvlans */
		INIT_LIST_HEAD(&adapter->vf_mvs.l);
		for (i = 0; i < num_vf_macvlans; i++) {
			mv_list->vf = -1;
			mv_list->free = true;
			list_add(&mv_list->l, &adapter->vf_mvs.l);
			mv_list++;
		}
	}

	/* Initialize default switching mode VEB */
	IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);
	adapter->flags2 |= IXGBE_FLAG2_BRIDGE_MODE_VEB;

	/* If call to enable VFs succeeded then allocate memory
	 * for per VF control structures.
	 */
	adapter->vfinfo =
		kcalloc(adapter->num_vfs,
			sizeof(struct vf_data_storage), GFP_KERNEL);
	if (adapter->vfinfo) {
		/* limit trafffic classes based on VFs enabled */
		if ((adapter->hw.mac.type == ixgbe_mac_82599EB) &&
		    (adapter->num_vfs < 16)) {
			adapter->dcb_cfg.num_tcs.pg_tcs = MAX_TRAFFIC_CLASS;
			adapter->dcb_cfg.num_tcs.pfc_tcs = MAX_TRAFFIC_CLASS;
		} else if (adapter->num_vfs < 32) {
			adapter->dcb_cfg.num_tcs.pg_tcs = 4;
			adapter->dcb_cfg.num_tcs.pfc_tcs = 4;
		} else {
			adapter->dcb_cfg.num_tcs.pg_tcs = 1;
			adapter->dcb_cfg.num_tcs.pfc_tcs = 1;
		}

		/* Disable RSC when in SR-IOV mode */
		adapter->flags2 &= ~(IXGBE_FLAG2_RSC_CAPABLE |
				     IXGBE_FLAG2_RSC_ENABLED);

		/* enable spoof checking for all VFs */
		for (i = 0; i < adapter->num_vfs; i++)
			adapter->vfinfo[i].spoofchk_enabled = true;
		return 0;
	}

	return -ENOMEM;
}

/* Note this function is called when the user wants to enable SR-IOV
 * VFs using the now deprecated module parameter
 */
void ixgbe_enable_sriov(struct ixgbe_adapter *adapter)
{
	int pre_existing_vfs = 0;

	pre_existing_vfs = pci_num_vf(adapter->pdev);
	if (!pre_existing_vfs && !adapter->num_vfs)
		return;

	/* If there are pre-existing VFs then we have to force
	 * use of that many - over ride any module parameter value.
	 * This may result from the user unloading the PF driver
	 * while VFs were assigned to guest VMs or because the VFs
	 * have been created via the new PCI SR-IOV sysfs interface.
	 */
	if (pre_existing_vfs) {
		adapter->num_vfs = pre_existing_vfs;
		dev_warn(&adapter->pdev->dev,
			 "Virtual Functions already enabled for this device - Please reload all VF drivers to avoid spoofed packet errors\n");
	} else {
		int err;
		/*
		 * The 82599 supports up to 64 VFs per physical function
		 * but this implementation limits allocation to 63 so that
		 * basic networking resources are still available to the
		 * physical function.  If the user requests greater thn
		 * 63 VFs then it is an error - reset to default of zero.
		 */
		adapter->num_vfs = min_t(unsigned int, adapter->num_vfs, IXGBE_MAX_VFS_DRV_LIMIT);

		err = pci_enable_sriov(adapter->pdev, adapter->num_vfs);
		if (err) {
			e_err(probe, "Failed to enable PCI sriov: %d\n", err);
			adapter->num_vfs = 0;
			return;
		}
	}

	if (!__ixgbe_enable_sriov(adapter))
		return;

	/* If we have gotten to this point then there is no memory available
	 * to manage the VF devices - print message and bail.
	 */
	e_err(probe, "Unable to allocate memory for VF Data Storage - "
	      "SRIOV disabled\n");
	ixgbe_disable_sriov(adapter);
}

#endif /* #ifdef CONFIG_PCI_IOV */
int ixgbe_disable_sriov(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 gpie;
	u32 vmdctl;
	int rss;

	/* set num VFs to 0 to prevent access to vfinfo */
	adapter->num_vfs = 0;

	/* free VF control structures */
	kfree(adapter->vfinfo);
	adapter->vfinfo = NULL;

	/* free macvlan list */
	kfree(adapter->mv_list);
	adapter->mv_list = NULL;

	/* if SR-IOV is already disabled then there is nothing to do */
	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return 0;

#ifdef CONFIG_PCI_IOV
	/*
	 * If our VFs are assigned we cannot shut down SR-IOV
	 * without causing issues, so just leave the hardware
	 * available but disabled
	 */
	if (pci_vfs_assigned(adapter->pdev)) {
		e_dev_warn("Unloading driver while VFs are assigned - VFs will not be deallocated\n");
		return -EPERM;
	}
	/* disable iov and allow time for transactions to clear */
	pci_disable_sriov(adapter->pdev);
#endif

	/* turn off device IOV mode */
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT, 0);
	gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);
	gpie &= ~IXGBE_GPIE_VTMODE_MASK;
	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);

	/* set default pool back to 0 */
	vmdctl = IXGBE_READ_REG(hw, IXGBE_VT_CTL);
	vmdctl &= ~IXGBE_VT_CTL_POOL_MASK;
	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, vmdctl);
	IXGBE_WRITE_FLUSH(hw);

	/* Disable VMDq flag so device will be set in VM mode */
	if (adapter->ring_feature[RING_F_VMDQ].limit == 1) {
		adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
		adapter->flags &= ~IXGBE_FLAG_SRIOV_ENABLED;
		rss = min_t(int, ixgbe_max_rss_indices(adapter),
			    num_online_cpus());
	} else {
		rss = min_t(int, IXGBE_MAX_L2A_QUEUES, num_online_cpus());
	}

	adapter->ring_feature[RING_F_VMDQ].offset = 0;
	adapter->ring_feature[RING_F_RSS].limit = rss;

	/* take a breather then clean up driver data */
	msleep(100);
	return 0;
}

static int ixgbe_pci_sriov_enable(struct pci_dev *dev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct ixgbe_adapter *adapter = pci_get_drvdata(dev);
	int err = 0;
	int i;
	int pre_existing_vfs = pci_num_vf(dev);

	if (pre_existing_vfs && pre_existing_vfs != num_vfs)
		err = ixgbe_disable_sriov(adapter);
	else if (pre_existing_vfs && pre_existing_vfs == num_vfs)
		return num_vfs;

	if (err)
		return err;

	/* While the SR-IOV capability structure reports total VFs to be 64,
	 * we have to limit the actual number allocated based on two factors.
	 * First, we reserve some transmit/receive resources for the PF.
	 * Second, VMDQ also uses the same pools that SR-IOV does. We need to
	 * account for this, so that we don't accidentally allocate more VFs
	 * than we have available pools. The PCI bus driver already checks for
	 * other values out of range.
	 */
	if ((num_vfs + adapter->num_rx_pools) > IXGBE_MAX_VF_FUNCTIONS)
		return -EPERM;

	adapter->num_vfs = num_vfs;

	err = __ixgbe_enable_sriov(adapter);
	if (err)
		return  err;

	for (i = 0; i < adapter->num_vfs; i++)
		ixgbe_vf_configuration(dev, (i | 0x10000000));

	err = pci_enable_sriov(dev, num_vfs);
	if (err) {
		e_dev_warn("Failed to enable PCI sriov: %d\n", err);
		return err;
	}
	ixgbe_sriov_reinit(adapter);

	return num_vfs;
#else
	return 0;
#endif
}

static int ixgbe_pci_sriov_disable(struct pci_dev *dev)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(dev);
	int err;
#ifdef CONFIG_PCI_IOV
	u32 current_flags = adapter->flags;
#endif

	err = ixgbe_disable_sriov(adapter);

	/* Only reinit if no error and state changed */
#ifdef CONFIG_PCI_IOV
	if (!err && current_flags != adapter->flags)
		ixgbe_sriov_reinit(adapter);
#endif

	return err;
}

int ixgbe_pci_sriov_configure(struct pci_dev *dev, int num_vfs)
{
	if (num_vfs == 0)
		return ixgbe_pci_sriov_disable(dev);
	else
		return ixgbe_pci_sriov_enable(dev, num_vfs);
}

static int ixgbe_set_vf_multicasts(struct ixgbe_adapter *adapter,
				   u32 *msgbuf, u32 vf)
{
	int entries = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK)
		       >> IXGBE_VT_MSGINFO_SHIFT;
	u16 *hash_list = (u16 *)&msgbuf[1];
	struct vf_data_storage *vfinfo = &adapter->vfinfo[vf];
	struct ixgbe_hw *hw = &adapter->hw;
	int i;
	u32 vector_bit;
	u32 vector_reg;
	u32 mta_reg;
	u32 vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf));

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
		vfinfo->vf_mc_hashes[i] = hash_list[i];
	}

	for (i = 0; i < vfinfo->num_vf_mc_hashes; i++) {
		vector_reg = (vfinfo->vf_mc_hashes[i] >> 5) & 0x7F;
		vector_bit = vfinfo->vf_mc_hashes[i] & 0x1F;
		mta_reg = IXGBE_READ_REG(hw, IXGBE_MTA(vector_reg));
		mta_reg |= (1 << vector_bit);
		IXGBE_WRITE_REG(hw, IXGBE_MTA(vector_reg), mta_reg);
	}
	vmolr |= IXGBE_VMOLR_ROMPE;
	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);

	return 0;
}

#ifdef CONFIG_PCI_IOV
void ixgbe_restore_vf_multicasts(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct vf_data_storage *vfinfo;
	int i, j;
	u32 vector_bit;
	u32 vector_reg;
	u32 mta_reg;

	for (i = 0; i < adapter->num_vfs; i++) {
		u32 vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(i));
		vfinfo = &adapter->vfinfo[i];
		for (j = 0; j < vfinfo->num_vf_mc_hashes; j++) {
			hw->addr_ctrl.mta_in_use++;
			vector_reg = (vfinfo->vf_mc_hashes[j] >> 5) & 0x7F;
			vector_bit = vfinfo->vf_mc_hashes[j] & 0x1F;
			mta_reg = IXGBE_READ_REG(hw, IXGBE_MTA(vector_reg));
			mta_reg |= (1 << vector_bit);
			IXGBE_WRITE_REG(hw, IXGBE_MTA(vector_reg), mta_reg);
		}

		if (vfinfo->num_vf_mc_hashes)
			vmolr |= IXGBE_VMOLR_ROMPE;
		else
			vmolr &= ~IXGBE_VMOLR_ROMPE;
		IXGBE_WRITE_REG(hw, IXGBE_VMOLR(i), vmolr);
	}

	/* Restore any VF macvlans */
	ixgbe_full_sync_mac_table(adapter);
}
#endif

static int ixgbe_set_vf_vlan(struct ixgbe_adapter *adapter, int add, int vid,
			     u32 vf)
{
	/* VLAN 0 is a special case, don't allow it to be removed */
	if (!vid && !add)
		return 0;

	return adapter->hw.mac.ops.set_vfta(&adapter->hw, vid, vf, (bool)add);
}

static s32 ixgbe_set_vf_lpe(struct ixgbe_adapter *adapter, u32 *msgbuf, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int max_frame = msgbuf[1];
	u32 max_frs;

	/*
	 * For 82599EB we have to keep all PFs and VFs operating with
	 * the same max_frame value in order to avoid sending an oversize
	 * frame to a VF.  In order to guarantee this is handled correctly
	 * for all cases we have several special exceptions to take into
	 * account before we can enable the VF for receive
	 */
	if (adapter->hw.mac.type == ixgbe_mac_82599EB) {
		struct net_device *dev = adapter->netdev;
		int pf_max_frame = dev->mtu + ETH_HLEN;
		u32 reg_offset, vf_shift, vfre;
		s32 err = 0;

#ifdef CONFIG_FCOE
		if (dev->features & NETIF_F_FCOE_MTU)
			pf_max_frame = max_t(int, pf_max_frame,
					     IXGBE_FCOE_JUMBO_FRAME_SIZE);

#endif /* CONFIG_FCOE */
		switch (adapter->vfinfo[vf].vf_api) {
		case ixgbe_mbox_api_11:
			/*
			 * Version 1.1 supports jumbo frames on VFs if PF has
			 * jumbo frames enabled which means legacy VFs are
			 * disabled
			 */
			if (pf_max_frame > ETH_FRAME_LEN)
				break;
		default:
			/*
			 * If the PF or VF are running w/ jumbo frames enabled
			 * we need to shut down the VF Rx path as we cannot
			 * support jumbo frames on legacy VFs
			 */
			if ((pf_max_frame > ETH_FRAME_LEN) ||
			    (max_frame > (ETH_FRAME_LEN + ETH_FCS_LEN)))
				err = -EINVAL;
			break;
		}

		/* determine VF receive enable location */
		vf_shift = vf % 32;
		reg_offset = vf / 32;

		/* enable or disable receive depending on error */
		vfre = IXGBE_READ_REG(hw, IXGBE_VFRE(reg_offset));
		if (err)
			vfre &= ~(1 << vf_shift);
		else
			vfre |= 1 << vf_shift;
		IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_offset), vfre);

		if (err) {
			e_err(drv, "VF max_frame %d out of range\n", max_frame);
			return err;
		}
	}

	/* MTU < 68 is an error and causes problems on some kernels */
	if (max_frame > IXGBE_MAX_JUMBO_FRAME_SIZE) {
		e_err(drv, "VF max_frame %d out of range\n", max_frame);
		return -EINVAL;
	}

	/* pull current max frame size from hardware */
	max_frs = IXGBE_READ_REG(hw, IXGBE_MAXFRS);
	max_frs &= IXGBE_MHADD_MFS_MASK;
	max_frs >>= IXGBE_MHADD_MFS_SHIFT;

	if (max_frs < max_frame) {
		max_frs = max_frame << IXGBE_MHADD_MFS_SHIFT;
		IXGBE_WRITE_REG(hw, IXGBE_MAXFRS, max_frs);
	}

	e_info(hw, "VF requests change max MTU to %d\n", max_frame);

	return 0;
}

static void ixgbe_set_vmolr(struct ixgbe_hw *hw, u32 vf, bool aupe)
{
	u32 vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf));
	vmolr |= IXGBE_VMOLR_BAM;
	if (aupe)
		vmolr |= IXGBE_VMOLR_AUPE;
	else
		vmolr &= ~IXGBE_VMOLR_AUPE;
	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf), vmolr);
}

static void ixgbe_clear_vmvir(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;

	IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf), 0);
}
static inline void ixgbe_vf_reset_event(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct vf_data_storage *vfinfo = &adapter->vfinfo[vf];
	u8 num_tcs = netdev_get_num_tc(adapter->netdev);

	/* add PF assigned VLAN or VLAN 0 */
	ixgbe_set_vf_vlan(adapter, true, vfinfo->pf_vlan, vf);

	/* reset offloads to defaults */
	ixgbe_set_vmolr(hw, vf, !vfinfo->pf_vlan);

	/* set outgoing tags for VFs */
	if (!vfinfo->pf_vlan && !vfinfo->pf_qos && !num_tcs) {
		ixgbe_clear_vmvir(adapter, vf);
	} else {
		if (vfinfo->pf_qos || !num_tcs)
			ixgbe_set_vmvir(adapter, vfinfo->pf_vlan,
					vfinfo->pf_qos, vf);
		else
			ixgbe_set_vmvir(adapter, vfinfo->pf_vlan,
					adapter->default_up, vf);

		if (vfinfo->spoofchk_enabled)
			hw->mac.ops.set_vlan_anti_spoofing(hw, true, vf);
	}

	/* reset multicast table array for vf */
	adapter->vfinfo[vf].num_vf_mc_hashes = 0;

	/* Flush and reset the mta with the new values */
	ixgbe_set_rx_mode(adapter->netdev);

	ixgbe_del_mac_filter(adapter, adapter->vfinfo[vf].vf_mac_addresses, vf);

	/* reset VF api back to unknown */
	adapter->vfinfo[vf].vf_api = ixgbe_mbox_api_10;
}

static int ixgbe_set_vf_mac(struct ixgbe_adapter *adapter,
			    int vf, unsigned char *mac_addr)
{
	ixgbe_del_mac_filter(adapter, adapter->vfinfo[vf].vf_mac_addresses, vf);
	memcpy(adapter->vfinfo[vf].vf_mac_addresses, mac_addr, ETH_ALEN);
	ixgbe_add_mac_filter(adapter, adapter->vfinfo[vf].vf_mac_addresses, vf);

	return 0;
}

static int ixgbe_set_vf_macvlan(struct ixgbe_adapter *adapter,
				int vf, int index, unsigned char *mac_addr)
{
	struct list_head *pos;
	struct vf_macvlans *entry;

	if (index <= 1) {
		list_for_each(pos, &adapter->vf_mvs.l) {
			entry = list_entry(pos, struct vf_macvlans, l);
			if (entry->vf == vf) {
				entry->vf = -1;
				entry->free = true;
				entry->is_macvlan = false;
				ixgbe_del_mac_filter(adapter,
						     entry->vf_macvlan, vf);
			}
		}
	}

	/*
	 * If index was zero then we were asked to clear the uc list
	 * for the VF.  We're done.
	 */
	if (!index)
		return 0;

	entry = NULL;

	list_for_each(pos, &adapter->vf_mvs.l) {
		entry = list_entry(pos, struct vf_macvlans, l);
		if (entry->free)
			break;
	}

	/*
	 * If we traversed the entire list and didn't find a free entry
	 * then we're out of space on the RAR table.  Also entry may
	 * be NULL because the original memory allocation for the list
	 * failed, which is not fatal but does mean we can't support
	 * VF requests for MACVLAN because we couldn't allocate
	 * memory for the list management required.
	 */
	if (!entry || !entry->free)
		return -ENOSPC;

	entry->free = false;
	entry->is_macvlan = true;
	entry->vf = vf;
	memcpy(entry->vf_macvlan, mac_addr, ETH_ALEN);

	ixgbe_add_mac_filter(adapter, mac_addr, vf);

	return 0;
}

int ixgbe_vf_configuration(struct pci_dev *pdev, unsigned int event_mask)
{
	struct ixgbe_adapter *adapter = pci_get_drvdata(pdev);
	unsigned int vfn = (event_mask & 0x3f);

	bool enable = ((event_mask & 0x10000000U) != 0);

	if (enable)
		eth_zero_addr(adapter->vfinfo[vfn].vf_mac_addresses);

	return 0;
}

static inline void ixgbe_write_qde(struct ixgbe_adapter *adapter, u32 vf,
				   u32 qde)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	u32 q_per_pool = __ALIGN_MASK(1, ~vmdq->mask);
	int i;

	for (i = vf * q_per_pool; i < ((vf + 1) * q_per_pool); i++) {
		u32 reg;

		/* flush previous write */
		IXGBE_WRITE_FLUSH(hw);

		/* indicate to hardware that we want to set drop enable */
		reg = IXGBE_QDE_WRITE | IXGBE_QDE_ENABLE;
		reg |= i <<  IXGBE_QDE_IDX_SHIFT;
		IXGBE_WRITE_REG(hw, IXGBE_QDE, reg);
	}
}

static int ixgbe_vf_reset_msg(struct ixgbe_adapter *adapter, u32 vf)
{
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	struct ixgbe_hw *hw = &adapter->hw;
	unsigned char *vf_mac = adapter->vfinfo[vf].vf_mac_addresses;
	u32 reg, reg_offset, vf_shift;
	u32 msgbuf[4] = {0, 0, 0, 0};
	u8 *addr = (u8 *)(&msgbuf[1]);
	u32 q_per_pool = __ALIGN_MASK(1, ~vmdq->mask);
	int i;

	e_info(probe, "VF Reset msg received from vf %d\n", vf);

	/* reset the filters for the device */
	ixgbe_vf_reset_event(adapter, vf);

	/* set vf mac address */
	if (!is_zero_ether_addr(vf_mac))
		ixgbe_set_vf_mac(adapter, vf, vf_mac);

	vf_shift = vf % 32;
	reg_offset = vf / 32;

	/* enable transmit for vf */
	reg = IXGBE_READ_REG(hw, IXGBE_VFTE(reg_offset));
	reg |= 1 << vf_shift;
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(reg_offset), reg);

	/* force drop enable for all VF Rx queues */
	ixgbe_write_qde(adapter, vf, IXGBE_QDE_ENABLE);

	/* enable receive for vf */
	reg = IXGBE_READ_REG(hw, IXGBE_VFRE(reg_offset));
	reg |= 1 << vf_shift;
	/*
	 * The 82599 cannot support a mix of jumbo and non-jumbo PF/VFs.
	 * For more info take a look at ixgbe_set_vf_lpe
	 */
	if (adapter->hw.mac.type == ixgbe_mac_82599EB) {
		struct net_device *dev = adapter->netdev;
		int pf_max_frame = dev->mtu + ETH_HLEN;

#ifdef CONFIG_FCOE
		if (dev->features & NETIF_F_FCOE_MTU)
			pf_max_frame = max_t(int, pf_max_frame,
					     IXGBE_FCOE_JUMBO_FRAME_SIZE);

#endif /* CONFIG_FCOE */
		if (pf_max_frame > ETH_FRAME_LEN)
			reg &= ~(1 << vf_shift);
	}
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(reg_offset), reg);

	/* enable VF mailbox for further messages */
	adapter->vfinfo[vf].clear_to_send = true;

	/* Enable counting of spoofed packets in the SSVPC register */
	reg = IXGBE_READ_REG(hw, IXGBE_VMECM(reg_offset));
	reg |= (1 << vf_shift);
	IXGBE_WRITE_REG(hw, IXGBE_VMECM(reg_offset), reg);

	/*
	 * Reset the VFs TDWBAL and TDWBAH registers
	 * which are not cleared by an FLR
	 */
	for (i = 0; i < q_per_pool; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_PVFTDWBAHn(q_per_pool, vf, i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_PVFTDWBALn(q_per_pool, vf, i), 0);
	}

	/* reply to reset with ack and vf mac address */
	msgbuf[0] = IXGBE_VF_RESET;
	if (!is_zero_ether_addr(vf_mac)) {
		msgbuf[0] |= IXGBE_VT_MSGTYPE_ACK;
		memcpy(addr, vf_mac, ETH_ALEN);
	} else {
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
		dev_warn(&adapter->pdev->dev,
			 "VF %d has no MAC address assigned, you may have to assign one manually\n",
			 vf);
	}

	/*
	 * Piggyback the multicast filter type so VF can compute the
	 * correct vectors
	 */
	msgbuf[3] = hw->mac.mc_filter_type;
	ixgbe_write_mbx(hw, msgbuf, IXGBE_VF_PERMADDR_MSG_LEN, vf);

	return 0;
}

static int ixgbe_set_vf_mac_addr(struct ixgbe_adapter *adapter,
				 u32 *msgbuf, u32 vf)
{
	u8 *new_mac = ((u8 *)(&msgbuf[1]));

	if (!is_valid_ether_addr(new_mac)) {
		e_warn(drv, "VF %d attempted to set invalid mac\n", vf);
		return -1;
	}

	if (adapter->vfinfo[vf].pf_set_mac &&
	    !ether_addr_equal(adapter->vfinfo[vf].vf_mac_addresses, new_mac)) {
		e_warn(drv,
		       "VF %d attempted to override administratively set MAC address\n"
		       "Reload the VF driver to resume operations\n",
		       vf);
		return -1;
	}

	return ixgbe_set_vf_mac(adapter, vf, new_mac) < 0;
}

static int ixgbe_find_vlvf_entry(struct ixgbe_hw *hw, u32 vlan)
{
	u32 vlvf;
	s32 regindex;

	/* short cut the special case */
	if (vlan == 0)
		return 0;

	/* Search for the vlan id in the VLVF entries */
	for (regindex = 1; regindex < IXGBE_VLVF_ENTRIES; regindex++) {
		vlvf = IXGBE_READ_REG(hw, IXGBE_VLVF(regindex));
		if ((vlvf & VLAN_VID_MASK) == vlan)
			break;
	}

	/* Return a negative value if not found */
	if (regindex >= IXGBE_VLVF_ENTRIES)
		regindex = -1;

	return regindex;
}

static int ixgbe_set_vf_vlan_msg(struct ixgbe_adapter *adapter,
				 u32 *msgbuf, u32 vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int add = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK) >> IXGBE_VT_MSGINFO_SHIFT;
	int vid = (msgbuf[1] & IXGBE_VLVF_VLANID_MASK);
	int err;
	s32 reg_ndx;
	u32 vlvf;
	u32 bits;
	u8 tcs = netdev_get_num_tc(adapter->netdev);

	if (adapter->vfinfo[vf].pf_vlan || tcs) {
		e_warn(drv,
		       "VF %d attempted to override administratively set VLAN configuration\n"
		       "Reload the VF driver to resume operations\n",
		       vf);
		return -1;
	}

	if (add)
		adapter->vfinfo[vf].vlan_count++;
	else if (adapter->vfinfo[vf].vlan_count)
		adapter->vfinfo[vf].vlan_count--;

	/* in case of promiscuous mode any VLAN filter set for a VF must
	 * also have the PF pool added to it.
	 */
	if (add && adapter->netdev->flags & IFF_PROMISC)
		err = ixgbe_set_vf_vlan(adapter, add, vid, VMDQ_P(0));

	err = ixgbe_set_vf_vlan(adapter, add, vid, vf);
	if (!err && adapter->vfinfo[vf].spoofchk_enabled)
		hw->mac.ops.set_vlan_anti_spoofing(hw, true, vf);

	/* Go through all the checks to see if the VLAN filter should
	 * be wiped completely.
	 */
	if (!add && adapter->netdev->flags & IFF_PROMISC) {
		reg_ndx = ixgbe_find_vlvf_entry(hw, vid);
		if (reg_ndx < 0)
			return err;
		vlvf = IXGBE_READ_REG(hw, IXGBE_VLVF(reg_ndx));
		/* See if any other pools are set for this VLAN filter
		 * entry other than the PF.
		 */
		if (VMDQ_P(0) < 32) {
			bits = IXGBE_READ_REG(hw, IXGBE_VLVFB(reg_ndx * 2));
			bits &= ~(1 << VMDQ_P(0));
			bits |= IXGBE_READ_REG(hw,
					       IXGBE_VLVFB(reg_ndx * 2) + 1);
		} else {
			bits = IXGBE_READ_REG(hw,
					      IXGBE_VLVFB(reg_ndx * 2) + 1);
			bits &= ~(1 << (VMDQ_P(0) - 32));
			bits |= IXGBE_READ_REG(hw, IXGBE_VLVFB(reg_ndx * 2));
		}

		/* If the filter was removed then ensure PF pool bit
		 * is cleared if the PF only added itself to the pool
		 * because the PF is in promiscuous mode.
		 */
		if ((vlvf & VLAN_VID_MASK) == vid &&
		    !test_bit(vid, adapter->active_vlans) && !bits)
			ixgbe_set_vf_vlan(adapter, add, vid, VMDQ_P(0));
	}

	return err;
}

static int ixgbe_set_vf_macvlan_msg(struct ixgbe_adapter *adapter,
				    u32 *msgbuf, u32 vf)
{
	u8 *new_mac = ((u8 *)(&msgbuf[1]));
	int index = (msgbuf[0] & IXGBE_VT_MSGINFO_MASK) >>
		    IXGBE_VT_MSGINFO_SHIFT;
	int err;

	if (adapter->vfinfo[vf].pf_set_mac && index > 0) {
		e_warn(drv,
		       "VF %d requested MACVLAN filter but is administratively denied\n",
		       vf);
		return -1;
	}

	/* An non-zero index indicates the VF is setting a filter */
	if (index) {
		if (!is_valid_ether_addr(new_mac)) {
			e_warn(drv, "VF %d attempted to set invalid mac\n", vf);
			return -1;
		}

		/*
		 * If the VF is allowed to set MAC filters then turn off
		 * anti-spoofing to avoid false positives.
		 */
		if (adapter->vfinfo[vf].spoofchk_enabled)
			ixgbe_ndo_set_vf_spoofchk(adapter->netdev, vf, false);
	}

	err = ixgbe_set_vf_macvlan(adapter, vf, index, new_mac);
	if (err == -ENOSPC)
		e_warn(drv,
		       "VF %d has requested a MACVLAN filter but there is no space for it\n",
		       vf);

	return err < 0;
}

static int ixgbe_negotiate_vf_api(struct ixgbe_adapter *adapter,
				  u32 *msgbuf, u32 vf)
{
	int api = msgbuf[1];

	switch (api) {
	case ixgbe_mbox_api_10:
	case ixgbe_mbox_api_11:
		adapter->vfinfo[vf].vf_api = api;
		return 0;
	default:
		break;
	}

	e_info(drv, "VF %d requested invalid api version %u\n", vf, api);

	return -1;
}

static int ixgbe_get_vf_queues(struct ixgbe_adapter *adapter,
			       u32 *msgbuf, u32 vf)
{
	struct net_device *dev = adapter->netdev;
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	unsigned int default_tc = 0;
	u8 num_tcs = netdev_get_num_tc(dev);

	/* verify the PF is supporting the correct APIs */
	switch (adapter->vfinfo[vf].vf_api) {
	case ixgbe_mbox_api_20:
	case ixgbe_mbox_api_11:
		break;
	default:
		return -1;
	}

	/* only allow 1 Tx queue for bandwidth limiting */
	msgbuf[IXGBE_VF_TX_QUEUES] = __ALIGN_MASK(1, ~vmdq->mask);
	msgbuf[IXGBE_VF_RX_QUEUES] = __ALIGN_MASK(1, ~vmdq->mask);

	/* if TCs > 1 determine which TC belongs to default user priority */
	if (num_tcs > 1)
		default_tc = netdev_get_prio_tc_map(dev, adapter->default_up);

	/* notify VF of need for VLAN tag stripping, and correct queue */
	if (num_tcs)
		msgbuf[IXGBE_VF_TRANS_VLAN] = num_tcs;
	else if (adapter->vfinfo[vf].pf_vlan || adapter->vfinfo[vf].pf_qos)
		msgbuf[IXGBE_VF_TRANS_VLAN] = 1;
	else
		msgbuf[IXGBE_VF_TRANS_VLAN] = 0;

	/* notify VF of default queue */
	msgbuf[IXGBE_VF_DEF_QUEUE] = default_tc;

	return 0;
}

static int ixgbe_rcv_msg_from_vf(struct ixgbe_adapter *adapter, u32 vf)
{
	u32 mbx_size = IXGBE_VFMAILBOX_SIZE;
	u32 msgbuf[IXGBE_VFMAILBOX_SIZE];
	struct ixgbe_hw *hw = &adapter->hw;
	s32 retval;

	retval = ixgbe_read_mbx(hw, msgbuf, mbx_size, vf);

	if (retval) {
		pr_err("Error receiving message from VF\n");
		return retval;
	}

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (IXGBE_VT_MSGTYPE_ACK | IXGBE_VT_MSGTYPE_NACK))
		return 0;

	/* flush the ack before we write any messages back */
	IXGBE_WRITE_FLUSH(hw);

	if (msgbuf[0] == IXGBE_VF_RESET)
		return ixgbe_vf_reset_msg(adapter, vf);

	/*
	 * until the vf completes a virtual function reset it should not be
	 * allowed to start any configuration.
	 */
	if (!adapter->vfinfo[vf].clear_to_send) {
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
		ixgbe_write_mbx(hw, msgbuf, 1, vf);
		return 0;
	}

	switch ((msgbuf[0] & 0xFFFF)) {
	case IXGBE_VF_SET_MAC_ADDR:
		retval = ixgbe_set_vf_mac_addr(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_SET_MULTICAST:
		retval = ixgbe_set_vf_multicasts(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_SET_VLAN:
		retval = ixgbe_set_vf_vlan_msg(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_SET_LPE:
		retval = ixgbe_set_vf_lpe(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_SET_MACVLAN:
		retval = ixgbe_set_vf_macvlan_msg(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_API_NEGOTIATE:
		retval = ixgbe_negotiate_vf_api(adapter, msgbuf, vf);
		break;
	case IXGBE_VF_GET_QUEUES:
		retval = ixgbe_get_vf_queues(adapter, msgbuf, vf);
		break;
	default:
		e_err(drv, "Unhandled Msg %8.8x\n", msgbuf[0]);
		retval = IXGBE_ERR_MBX;
		break;
	}

	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= IXGBE_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= IXGBE_VT_MSGTYPE_ACK;

	msgbuf[0] |= IXGBE_VT_MSGTYPE_CTS;

	ixgbe_write_mbx(hw, msgbuf, mbx_size, vf);

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

static int ixgbe_enable_port_vlan(struct ixgbe_adapter *adapter, int vf,
				  u16 vlan, u8 qos)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	err = ixgbe_set_vf_vlan(adapter, true, vlan, vf);
	if (err)
		goto out;

	ixgbe_set_vmvir(adapter, vlan, qos, vf);
	ixgbe_set_vmolr(hw, vf, false);
	if (adapter->vfinfo[vf].spoofchk_enabled)
		hw->mac.ops.set_vlan_anti_spoofing(hw, true, vf);
	adapter->vfinfo[vf].vlan_count++;

	/* enable hide vlan on X550 */
	if (hw->mac.type >= ixgbe_mac_X550)
		ixgbe_write_qde(adapter, vf, IXGBE_QDE_ENABLE |
				IXGBE_QDE_HIDE_VLAN);

	adapter->vfinfo[vf].pf_vlan = vlan;
	adapter->vfinfo[vf].pf_qos = qos;
	dev_info(&adapter->pdev->dev,
		 "Setting VLAN %d, QOS 0x%x on VF %d\n", vlan, qos, vf);
	if (test_bit(__IXGBE_DOWN, &adapter->state)) {
		dev_warn(&adapter->pdev->dev,
			 "The VF VLAN has been set, but the PF device is not up.\n");
		dev_warn(&adapter->pdev->dev,
			 "Bring the PF device up before attempting to use the VF device.\n");
	}

out:
	return err;
}

static int ixgbe_disable_port_vlan(struct ixgbe_adapter *adapter, int vf)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	err = ixgbe_set_vf_vlan(adapter, false,
				adapter->vfinfo[vf].pf_vlan, vf);
	ixgbe_clear_vmvir(adapter, vf);
	ixgbe_set_vmolr(hw, vf, true);
	hw->mac.ops.set_vlan_anti_spoofing(hw, false, vf);
	if (adapter->vfinfo[vf].vlan_count)
		adapter->vfinfo[vf].vlan_count--;

	/* disable hide VLAN on X550 */
	if (hw->mac.type >= ixgbe_mac_X550)
		ixgbe_write_qde(adapter, vf, IXGBE_QDE_ENABLE);

	adapter->vfinfo[vf].pf_vlan = 0;
	adapter->vfinfo[vf].pf_qos = 0;

	return err;
}

int ixgbe_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos)
{
	int err = 0;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if ((vf >= adapter->num_vfs) || (vlan > 4095) || (qos > 7))
		return -EINVAL;
	if (vlan || qos) {
		/* Check if there is already a port VLAN set, if so
		 * we have to delete the old one first before we
		 * can set the new one.  The usage model had
		 * previously assumed the user would delete the
		 * old port VLAN before setting a new one but this
		 * is not necessarily the case.
		 */
		if (adapter->vfinfo[vf].pf_vlan)
			err = ixgbe_disable_port_vlan(adapter, vf);
		if (err)
			goto out;
		err = ixgbe_enable_port_vlan(adapter, vf, vlan, qos);
	} else {
		err = ixgbe_disable_port_vlan(adapter, vf);
	}

out:
	return err;
}

static int ixgbe_link_mbps(struct ixgbe_adapter *adapter)
{
	switch (adapter->link_speed) {
	case IXGBE_LINK_SPEED_100_FULL:
		return 100;
	case IXGBE_LINK_SPEED_1GB_FULL:
		return 1000;
	case IXGBE_LINK_SPEED_10GB_FULL:
		return 10000;
	default:
		return 0;
	}
}

static void ixgbe_set_vf_rate_limit(struct ixgbe_adapter *adapter, int vf)
{
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	struct ixgbe_hw *hw = &adapter->hw;
	u32 bcnrc_val = 0;
	u16 queue, queues_per_pool;
	u16 tx_rate = adapter->vfinfo[vf].tx_rate;

	if (tx_rate) {
		/* start with base link speed value */
		bcnrc_val = adapter->vf_rate_link_speed;

		/* Calculate the rate factor values to set */
		bcnrc_val <<= IXGBE_RTTBCNRC_RF_INT_SHIFT;
		bcnrc_val /= tx_rate;

		/* clear everything but the rate factor */
		bcnrc_val &= IXGBE_RTTBCNRC_RF_INT_MASK |
			     IXGBE_RTTBCNRC_RF_DEC_MASK;

		/* enable the rate scheduler */
		bcnrc_val |= IXGBE_RTTBCNRC_RS_ENA;
	}

	/*
	 * Set global transmit compensation time to the MMW_SIZE in RTTBCNRM
	 * register. Typically MMW_SIZE=0x014 if 9728-byte jumbo is supported
	 * and 0x004 otherwise.
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRM, 0x4);
		break;
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRM, 0x14);
		break;
	default:
		break;
	}

	/* determine how many queues per pool based on VMDq mask */
	queues_per_pool = __ALIGN_MASK(1, ~vmdq->mask);

	/* write value for all Tx queues belonging to VF */
	for (queue = 0; queue < queues_per_pool; queue++) {
		unsigned int reg_idx = (vf * queues_per_pool) + queue;

		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, reg_idx);
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRC, bcnrc_val);
	}
}

void ixgbe_check_vf_rate_limit(struct ixgbe_adapter *adapter)
{
	int i;

	/* VF Tx rate limit was not set */
	if (!adapter->vf_rate_link_speed)
		return;

	if (ixgbe_link_mbps(adapter) != adapter->vf_rate_link_speed) {
		adapter->vf_rate_link_speed = 0;
		dev_info(&adapter->pdev->dev,
			 "Link speed has been changed. VF Transmit rate is disabled\n");
	}

	for (i = 0; i < adapter->num_vfs; i++) {
		if (!adapter->vf_rate_link_speed)
			adapter->vfinfo[i].tx_rate = 0;

		ixgbe_set_vf_rate_limit(adapter, i);
	}
}

int ixgbe_ndo_set_vf_bw(struct net_device *netdev, int vf, int min_tx_rate,
			int max_tx_rate)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int link_speed;

	/* verify VF is active */
	if (vf >= adapter->num_vfs)
		return -EINVAL;

	/* verify link is up */
	if (!adapter->link_up)
		return -EINVAL;

	/* verify we are linked at 10Gbps */
	link_speed = ixgbe_link_mbps(adapter);
	if (link_speed != 10000)
		return -EINVAL;

	if (min_tx_rate)
		return -EINVAL;

	/* rate limit cannot be less than 10Mbs or greater than link speed */
	if (max_tx_rate && ((max_tx_rate <= 10) || (max_tx_rate > link_speed)))
		return -EINVAL;

	/* store values */
	adapter->vf_rate_link_speed = link_speed;
	adapter->vfinfo[vf].tx_rate = max_tx_rate;

	/* update hardware configuration */
	ixgbe_set_vf_rate_limit(adapter, vf);

	return 0;
}

int ixgbe_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int vf_target_reg = vf >> 3;
	int vf_target_shift = vf % 8;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 regval;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	adapter->vfinfo[vf].spoofchk_enabled = setting;

	regval = IXGBE_READ_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg));
	regval &= ~(1 << vf_target_shift);
	regval |= (setting << vf_target_shift);
	IXGBE_WRITE_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg), regval);

	if (adapter->vfinfo[vf].vlan_count) {
		vf_target_shift += IXGBE_SPOOF_VLANAS_SHIFT;
		regval = IXGBE_READ_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg));
		regval &= ~(1 << vf_target_shift);
		regval |= (setting << vf_target_shift);
		IXGBE_WRITE_REG(hw, IXGBE_PFVFSPOOF(vf_target_reg), regval);
	}

	return 0;
}

int ixgbe_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	if (vf >= adapter->num_vfs)
		return -EINVAL;
	ivi->vf = vf;
	memcpy(&ivi->mac, adapter->vfinfo[vf].vf_mac_addresses, ETH_ALEN);
	ivi->max_tx_rate = adapter->vfinfo[vf].tx_rate;
	ivi->min_tx_rate = 0;
	ivi->vlan = adapter->vfinfo[vf].pf_vlan;
	ivi->qos = adapter->vfinfo[vf].pf_qos;
	ivi->spoofchk = adapter->vfinfo[vf].spoofchk_enabled;
	return 0;
}
