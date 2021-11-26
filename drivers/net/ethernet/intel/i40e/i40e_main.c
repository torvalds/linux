// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/etherdevice.h>
#include <linux/of_net.h>
#include <linux/pci.h>
#include <linux/bpf.h>
#include <generated/utsrelease.h>

/* Local includes */
#include "i40e.h"
#include "i40e_diag.h"
#include "i40e_xsk.h"
#include <net/udp_tunnel.h>
#include <net/xdp_sock_drv.h>
/* All i40e tracepoints are defined by the include below, which
 * must be included exactly once across the whole kernel with
 * CREATE_TRACE_POINTS defined
 */
#define CREATE_TRACE_POINTS
#include "i40e_trace.h"

const char i40e_driver_name[] = "i40e";
static const char i40e_driver_string[] =
			"Intel(R) Ethernet Connection XL710 Network Driver";

static const char i40e_copyright[] = "Copyright (c) 2013 - 2019 Intel Corporation.";

/* a bit of forward declarations */
static void i40e_vsi_reinit_locked(struct i40e_vsi *vsi);
static void i40e_handle_reset_warning(struct i40e_pf *pf, bool lock_acquired);
static int i40e_add_vsi(struct i40e_vsi *vsi);
static int i40e_add_veb(struct i40e_veb *veb, struct i40e_vsi *vsi);
static int i40e_setup_pf_switch(struct i40e_pf *pf, bool reinit, bool lock_acquired);
static int i40e_setup_misc_vector(struct i40e_pf *pf);
static void i40e_determine_queue_usage(struct i40e_pf *pf);
static int i40e_setup_pf_filter_control(struct i40e_pf *pf);
static void i40e_prep_for_reset(struct i40e_pf *pf, bool lock_acquired);
static void i40e_reset_and_rebuild(struct i40e_pf *pf, bool reinit,
				   bool lock_acquired);
static int i40e_reset(struct i40e_pf *pf);
static void i40e_rebuild(struct i40e_pf *pf, bool reinit, bool lock_acquired);
static int i40e_setup_misc_vector_for_recovery_mode(struct i40e_pf *pf);
static int i40e_restore_interrupt_scheme(struct i40e_pf *pf);
static bool i40e_check_recovery_mode(struct i40e_pf *pf);
static int i40e_init_recovery_mode(struct i40e_pf *pf, struct i40e_hw *hw);
static void i40e_fdir_sb_setup(struct i40e_pf *pf);
static int i40e_veb_get_bw_info(struct i40e_veb *veb);
static int i40e_get_capabilities(struct i40e_pf *pf,
				 enum i40e_admin_queue_opc list_type);
static bool i40e_is_total_port_shutdown_enabled(struct i40e_pf *pf);

/* i40e_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id i40e_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_SFP_XL710), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QEMU), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_KX_B), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_KX_C), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QSFP_A), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QSFP_B), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QSFP_C), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_BASE_T), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_BASE_T4), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_BASE_T_BC), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_SFP), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_B), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_KX_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QSFP_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_SFP_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_1G_BASE_T_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_BASE_T_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_SFP_I_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_20G_KR2), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_20G_KR2_A), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_X710_N3000), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_XXV710_N3000), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_25G_B), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_25G_SFP28), 0},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, i40e_pci_tbl);

#define I40E_MAX_VF_COUNT 128
static int debug = -1;
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all), Debug mask (0x8XXXXXXX)");

MODULE_AUTHOR("Intel Corporation, <e1000-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Ethernet Connection XL710 Network Driver");
MODULE_LICENSE("GPL v2");

static struct workqueue_struct *i40e_wq;

static void netdev_hw_addr_refcnt(struct i40e_mac_filter *f,
				  struct net_device *netdev, int delta)
{
	struct netdev_hw_addr *ha;

	if (!f || !netdev)
		return;

	netdev_for_each_mc_addr(ha, netdev) {
		if (ether_addr_equal(ha->addr, f->macaddr)) {
			ha->refcount += delta;
			if (ha->refcount <= 0)
				ha->refcount = 1;
			break;
		}
	}
}

/**
 * i40e_allocate_dma_mem_d - OS specific memory alloc for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 * @alignment: what to align the allocation to
 **/
int i40e_allocate_dma_mem_d(struct i40e_hw *hw, struct i40e_dma_mem *mem,
			    u64 size, u32 alignment)
{
	struct i40e_pf *pf = (struct i40e_pf *)hw->back;

	mem->size = ALIGN(size, alignment);
	mem->va = dma_alloc_coherent(&pf->pdev->dev, mem->size, &mem->pa,
				     GFP_KERNEL);
	if (!mem->va)
		return -ENOMEM;

	return 0;
}

/**
 * i40e_free_dma_mem_d - OS specific memory free for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 **/
int i40e_free_dma_mem_d(struct i40e_hw *hw, struct i40e_dma_mem *mem)
{
	struct i40e_pf *pf = (struct i40e_pf *)hw->back;

	dma_free_coherent(&pf->pdev->dev, mem->size, mem->va, mem->pa);
	mem->va = NULL;
	mem->pa = 0;
	mem->size = 0;

	return 0;
}

/**
 * i40e_allocate_virt_mem_d - OS specific memory alloc for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 **/
int i40e_allocate_virt_mem_d(struct i40e_hw *hw, struct i40e_virt_mem *mem,
			     u32 size)
{
	mem->size = size;
	mem->va = kzalloc(size, GFP_KERNEL);

	if (!mem->va)
		return -ENOMEM;

	return 0;
}

/**
 * i40e_free_virt_mem_d - OS specific memory free for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 **/
int i40e_free_virt_mem_d(struct i40e_hw *hw, struct i40e_virt_mem *mem)
{
	/* it's ok to kfree a NULL pointer */
	kfree(mem->va);
	mem->va = NULL;
	mem->size = 0;

	return 0;
}

/**
 * i40e_get_lump - find a lump of free generic resource
 * @pf: board private structure
 * @pile: the pile of resource to search
 * @needed: the number of items needed
 * @id: an owner id to stick on the items assigned
 *
 * Returns the base item index of the lump, or negative for error
 **/
static int i40e_get_lump(struct i40e_pf *pf, struct i40e_lump_tracking *pile,
			 u16 needed, u16 id)
{
	int ret = -ENOMEM;
	int i, j;

	if (!pile || needed == 0 || id >= I40E_PILE_VALID_BIT) {
		dev_info(&pf->pdev->dev,
			 "param err: pile=%s needed=%d id=0x%04x\n",
			 pile ? "<valid>" : "<null>", needed, id);
		return -EINVAL;
	}

	/* Allocate last queue in the pile for FDIR VSI queue
	 * so it doesn't fragment the qp_pile
	 */
	if (pile == pf->qp_pile && pf->vsi[id]->type == I40E_VSI_FDIR) {
		if (pile->list[pile->num_entries - 1] & I40E_PILE_VALID_BIT) {
			dev_err(&pf->pdev->dev,
				"Cannot allocate queue %d for I40E_VSI_FDIR\n",
				pile->num_entries - 1);
			return -ENOMEM;
		}
		pile->list[pile->num_entries - 1] = id | I40E_PILE_VALID_BIT;
		return pile->num_entries - 1;
	}

	i = 0;
	while (i < pile->num_entries) {
		/* skip already allocated entries */
		if (pile->list[i] & I40E_PILE_VALID_BIT) {
			i++;
			continue;
		}

		/* do we have enough in this lump? */
		for (j = 0; (j < needed) && ((i+j) < pile->num_entries); j++) {
			if (pile->list[i+j] & I40E_PILE_VALID_BIT)
				break;
		}

		if (j == needed) {
			/* there was enough, so assign it to the requestor */
			for (j = 0; j < needed; j++)
				pile->list[i+j] = id | I40E_PILE_VALID_BIT;
			ret = i;
			break;
		}

		/* not enough, so skip over it and continue looking */
		i += j;
	}

	return ret;
}

/**
 * i40e_put_lump - return a lump of generic resource
 * @pile: the pile of resource to search
 * @index: the base item index
 * @id: the owner id of the items assigned
 *
 * Returns the count of items in the lump
 **/
static int i40e_put_lump(struct i40e_lump_tracking *pile, u16 index, u16 id)
{
	int valid_id = (id | I40E_PILE_VALID_BIT);
	int count = 0;
	u16 i;

	if (!pile || index >= pile->num_entries)
		return -EINVAL;

	for (i = index;
	     i < pile->num_entries && pile->list[i] == valid_id;
	     i++) {
		pile->list[i] = 0;
		count++;
	}


	return count;
}

/**
 * i40e_find_vsi_from_id - searches for the vsi with the given id
 * @pf: the pf structure to search for the vsi
 * @id: id of the vsi it is searching for
 **/
struct i40e_vsi *i40e_find_vsi_from_id(struct i40e_pf *pf, u16 id)
{
	int i;

	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i] && (pf->vsi[i]->id == id))
			return pf->vsi[i];

	return NULL;
}

/**
 * i40e_service_event_schedule - Schedule the service task to wake up
 * @pf: board private structure
 *
 * If not already scheduled, this puts the task into the work queue
 **/
void i40e_service_event_schedule(struct i40e_pf *pf)
{
	if ((!test_bit(__I40E_DOWN, pf->state) &&
	     !test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state)) ||
	      test_bit(__I40E_RECOVERY_MODE, pf->state))
		queue_work(i40e_wq, &pf->service_task);
}

/**
 * i40e_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 * @txqueue: queue number timing out
 *
 * If any port has noticed a Tx timeout, it is likely that the whole
 * device is munged, not just the one netdev port, so go for the full
 * reset.
 **/
static void i40e_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_ring *tx_ring = NULL;
	unsigned int i;
	u32 head, val;

	pf->tx_timeout_count++;

	/* with txqueue index, find the tx_ring struct */
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc) {
			if (txqueue ==
			    vsi->tx_rings[i]->queue_index) {
				tx_ring = vsi->tx_rings[i];
				break;
			}
		}
	}

	if (time_after(jiffies, (pf->tx_timeout_last_recovery + HZ*20)))
		pf->tx_timeout_recovery_level = 1;  /* reset after some time */
	else if (time_before(jiffies,
		      (pf->tx_timeout_last_recovery + netdev->watchdog_timeo)))
		return;   /* don't do any new action before the next timeout */

	/* don't kick off another recovery if one is already pending */
	if (test_and_set_bit(__I40E_TIMEOUT_RECOVERY_PENDING, pf->state))
		return;

	if (tx_ring) {
		head = i40e_get_head(tx_ring);
		/* Read interrupt register */
		if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			val = rd32(&pf->hw,
			     I40E_PFINT_DYN_CTLN(tx_ring->q_vector->v_idx +
						tx_ring->vsi->base_vector - 1));
		else
			val = rd32(&pf->hw, I40E_PFINT_DYN_CTL0);

		netdev_info(netdev, "tx_timeout: VSI_seid: %d, Q %d, NTC: 0x%x, HWB: 0x%x, NTU: 0x%x, TAIL: 0x%x, INT: 0x%x\n",
			    vsi->seid, txqueue, tx_ring->next_to_clean,
			    head, tx_ring->next_to_use,
			    readl(tx_ring->tail), val);
	}

	pf->tx_timeout_last_recovery = jiffies;
	netdev_info(netdev, "tx_timeout recovery level %d, txqueue %d\n",
		    pf->tx_timeout_recovery_level, txqueue);

	switch (pf->tx_timeout_recovery_level) {
	case 1:
		set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
		break;
	case 2:
		set_bit(__I40E_CORE_RESET_REQUESTED, pf->state);
		break;
	case 3:
		set_bit(__I40E_GLOBAL_RESET_REQUESTED, pf->state);
		break;
	default:
		netdev_err(netdev, "tx_timeout recovery unsuccessful\n");
		break;
	}

	i40e_service_event_schedule(pf);
	pf->tx_timeout_recovery_level++;
}

/**
 * i40e_get_vsi_stats_struct - Get System Network Statistics
 * @vsi: the VSI we care about
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the service task.
 **/
struct rtnl_link_stats64 *i40e_get_vsi_stats_struct(struct i40e_vsi *vsi)
{
	return &vsi->net_stats;
}

/**
 * i40e_get_netdev_stats_struct_tx - populate stats from a Tx ring
 * @ring: Tx ring to get statistics from
 * @stats: statistics entry to be updated
 **/
static void i40e_get_netdev_stats_struct_tx(struct i40e_ring *ring,
					    struct rtnl_link_stats64 *stats)
{
	u64 bytes, packets;
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(&ring->syncp);
		packets = ring->stats.packets;
		bytes   = ring->stats.bytes;
	} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

	stats->tx_packets += packets;
	stats->tx_bytes   += bytes;
}

/**
 * i40e_get_netdev_stats_struct - Get statistics for netdev interface
 * @netdev: network interface device structure
 * @stats: data structure to store statistics
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the service task.
 **/
static void i40e_get_netdev_stats_struct(struct net_device *netdev,
				  struct rtnl_link_stats64 *stats)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct rtnl_link_stats64 *vsi_stats = i40e_get_vsi_stats_struct(vsi);
	struct i40e_ring *ring;
	int i;

	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return;

	if (!vsi->tx_rings)
		return;

	rcu_read_lock();
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		u64 bytes, packets;
		unsigned int start;

		ring = READ_ONCE(vsi->tx_rings[i]);
		if (!ring)
			continue;
		i40e_get_netdev_stats_struct_tx(ring, stats);

		if (i40e_enabled_xdp_vsi(vsi)) {
			ring = READ_ONCE(vsi->xdp_rings[i]);
			if (!ring)
				continue;
			i40e_get_netdev_stats_struct_tx(ring, stats);
		}

		ring = READ_ONCE(vsi->rx_rings[i]);
		if (!ring)
			continue;
		do {
			start   = u64_stats_fetch_begin_irq(&ring->syncp);
			packets = ring->stats.packets;
			bytes   = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));

		stats->rx_packets += packets;
		stats->rx_bytes   += bytes;

	}
	rcu_read_unlock();

	/* following stats updated by i40e_watchdog_subtask() */
	stats->multicast	= vsi_stats->multicast;
	stats->tx_errors	= vsi_stats->tx_errors;
	stats->tx_dropped	= vsi_stats->tx_dropped;
	stats->rx_errors	= vsi_stats->rx_errors;
	stats->rx_dropped	= vsi_stats->rx_dropped;
	stats->rx_crc_errors	= vsi_stats->rx_crc_errors;
	stats->rx_length_errors	= vsi_stats->rx_length_errors;
}

/**
 * i40e_vsi_reset_stats - Resets all stats of the given vsi
 * @vsi: the VSI to have its stats reset
 **/
void i40e_vsi_reset_stats(struct i40e_vsi *vsi)
{
	struct rtnl_link_stats64 *ns;
	int i;

	if (!vsi)
		return;

	ns = i40e_get_vsi_stats_struct(vsi);
	memset(ns, 0, sizeof(*ns));
	memset(&vsi->net_stats_offsets, 0, sizeof(vsi->net_stats_offsets));
	memset(&vsi->eth_stats, 0, sizeof(vsi->eth_stats));
	memset(&vsi->eth_stats_offsets, 0, sizeof(vsi->eth_stats_offsets));
	if (vsi->rx_rings && vsi->rx_rings[0]) {
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			memset(&vsi->rx_rings[i]->stats, 0,
			       sizeof(vsi->rx_rings[i]->stats));
			memset(&vsi->rx_rings[i]->rx_stats, 0,
			       sizeof(vsi->rx_rings[i]->rx_stats));
			memset(&vsi->tx_rings[i]->stats, 0,
			       sizeof(vsi->tx_rings[i]->stats));
			memset(&vsi->tx_rings[i]->tx_stats, 0,
			       sizeof(vsi->tx_rings[i]->tx_stats));
		}
	}
	vsi->stat_offsets_loaded = false;
}

/**
 * i40e_pf_reset_stats - Reset all of the stats for the given PF
 * @pf: the PF to be reset
 **/
void i40e_pf_reset_stats(struct i40e_pf *pf)
{
	int i;

	memset(&pf->stats, 0, sizeof(pf->stats));
	memset(&pf->stats_offsets, 0, sizeof(pf->stats_offsets));
	pf->stat_offsets_loaded = false;

	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (pf->veb[i]) {
			memset(&pf->veb[i]->stats, 0,
			       sizeof(pf->veb[i]->stats));
			memset(&pf->veb[i]->stats_offsets, 0,
			       sizeof(pf->veb[i]->stats_offsets));
			memset(&pf->veb[i]->tc_stats, 0,
			       sizeof(pf->veb[i]->tc_stats));
			memset(&pf->veb[i]->tc_stats_offsets, 0,
			       sizeof(pf->veb[i]->tc_stats_offsets));
			pf->veb[i]->stat_offsets_loaded = false;
		}
	}
	pf->hw_csum_rx_error = 0;
}

/**
 * i40e_stat_update48 - read and update a 48 bit stat from the chip
 * @hw: ptr to the hardware info
 * @hireg: the high 32 bit reg to read
 * @loreg: the low 32 bit reg to read
 * @offset_loaded: has the initial offset been loaded yet
 * @offset: ptr to current offset value
 * @stat: ptr to the stat
 *
 * Since the device stats are not reset at PFReset, they likely will not
 * be zeroed when the driver starts.  We'll save the first values read
 * and use them as offsets to be subtracted from the raw values in order
 * to report stats that count from zero.  In the process, we also manage
 * the potential roll-over.
 **/
static void i40e_stat_update48(struct i40e_hw *hw, u32 hireg, u32 loreg,
			       bool offset_loaded, u64 *offset, u64 *stat)
{
	u64 new_data;

	if (hw->device_id == I40E_DEV_ID_QEMU) {
		new_data = rd32(hw, loreg);
		new_data |= ((u64)(rd32(hw, hireg) & 0xFFFF)) << 32;
	} else {
		new_data = rd64(hw, loreg);
	}
	if (!offset_loaded)
		*offset = new_data;
	if (likely(new_data >= *offset))
		*stat = new_data - *offset;
	else
		*stat = (new_data + BIT_ULL(48)) - *offset;
	*stat &= 0xFFFFFFFFFFFFULL;
}

/**
 * i40e_stat_update32 - read and update a 32 bit stat from the chip
 * @hw: ptr to the hardware info
 * @reg: the hw reg to read
 * @offset_loaded: has the initial offset been loaded yet
 * @offset: ptr to current offset value
 * @stat: ptr to the stat
 **/
static void i40e_stat_update32(struct i40e_hw *hw, u32 reg,
			       bool offset_loaded, u64 *offset, u64 *stat)
{
	u32 new_data;

	new_data = rd32(hw, reg);
	if (!offset_loaded)
		*offset = new_data;
	if (likely(new_data >= *offset))
		*stat = (u32)(new_data - *offset);
	else
		*stat = (u32)((new_data + BIT_ULL(32)) - *offset);
}

/**
 * i40e_stat_update_and_clear32 - read and clear hw reg, update a 32 bit stat
 * @hw: ptr to the hardware info
 * @reg: the hw reg to read and clear
 * @stat: ptr to the stat
 **/
static void i40e_stat_update_and_clear32(struct i40e_hw *hw, u32 reg, u64 *stat)
{
	u32 new_data = rd32(hw, reg);

	wr32(hw, reg, 1); /* must write a nonzero value to clear register */
	*stat += new_data;
}

/**
 * i40e_update_eth_stats - Update VSI-specific ethernet statistics counters.
 * @vsi: the VSI to be updated
 **/
void i40e_update_eth_stats(struct i40e_vsi *vsi)
{
	int stat_idx = le16_to_cpu(vsi->info.stat_counter_idx);
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */

	es = &vsi->eth_stats;
	oes = &vsi->eth_stats_offsets;

	/* Gather up the stats that the hw collects */
	i40e_stat_update32(hw, I40E_GLV_TEPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_errors, &es->tx_errors);
	i40e_stat_update32(hw, I40E_GLV_RDPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_discards, &es->rx_discards);
	i40e_stat_update32(hw, I40E_GLV_RUPP(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_unknown_protocol, &es->rx_unknown_protocol);

	i40e_stat_update48(hw, I40E_GLV_GORCH(stat_idx),
			   I40E_GLV_GORCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_bytes, &es->rx_bytes);
	i40e_stat_update48(hw, I40E_GLV_UPRCH(stat_idx),
			   I40E_GLV_UPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_unicast, &es->rx_unicast);
	i40e_stat_update48(hw, I40E_GLV_MPRCH(stat_idx),
			   I40E_GLV_MPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_multicast, &es->rx_multicast);
	i40e_stat_update48(hw, I40E_GLV_BPRCH(stat_idx),
			   I40E_GLV_BPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_broadcast, &es->rx_broadcast);

	i40e_stat_update48(hw, I40E_GLV_GOTCH(stat_idx),
			   I40E_GLV_GOTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_bytes, &es->tx_bytes);
	i40e_stat_update48(hw, I40E_GLV_UPTCH(stat_idx),
			   I40E_GLV_UPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_unicast, &es->tx_unicast);
	i40e_stat_update48(hw, I40E_GLV_MPTCH(stat_idx),
			   I40E_GLV_MPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_multicast, &es->tx_multicast);
	i40e_stat_update48(hw, I40E_GLV_BPTCH(stat_idx),
			   I40E_GLV_BPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_broadcast, &es->tx_broadcast);
	vsi->stat_offsets_loaded = true;
}

/**
 * i40e_update_veb_stats - Update Switch component statistics
 * @veb: the VEB being updated
 **/
void i40e_update_veb_stats(struct i40e_veb *veb)
{
	struct i40e_pf *pf = veb->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */
	struct i40e_veb_tc_stats *veb_oes;
	struct i40e_veb_tc_stats *veb_es;
	int i, idx = 0;

	idx = veb->stats_idx;
	es = &veb->stats;
	oes = &veb->stats_offsets;
	veb_es = &veb->tc_stats;
	veb_oes = &veb->tc_stats_offsets;

	/* Gather up the stats that the hw collects */
	i40e_stat_update32(hw, I40E_GLSW_TDPC(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_discards, &es->tx_discards);
	if (hw->revision_id > 0)
		i40e_stat_update32(hw, I40E_GLSW_RUPP(idx),
				   veb->stat_offsets_loaded,
				   &oes->rx_unknown_protocol,
				   &es->rx_unknown_protocol);
	i40e_stat_update48(hw, I40E_GLSW_GORCH(idx), I40E_GLSW_GORCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_bytes, &es->rx_bytes);
	i40e_stat_update48(hw, I40E_GLSW_UPRCH(idx), I40E_GLSW_UPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_unicast, &es->rx_unicast);
	i40e_stat_update48(hw, I40E_GLSW_MPRCH(idx), I40E_GLSW_MPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_multicast, &es->rx_multicast);
	i40e_stat_update48(hw, I40E_GLSW_BPRCH(idx), I40E_GLSW_BPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_broadcast, &es->rx_broadcast);

	i40e_stat_update48(hw, I40E_GLSW_GOTCH(idx), I40E_GLSW_GOTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_bytes, &es->tx_bytes);
	i40e_stat_update48(hw, I40E_GLSW_UPTCH(idx), I40E_GLSW_UPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_unicast, &es->tx_unicast);
	i40e_stat_update48(hw, I40E_GLSW_MPTCH(idx), I40E_GLSW_MPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_multicast, &es->tx_multicast);
	i40e_stat_update48(hw, I40E_GLSW_BPTCH(idx), I40E_GLSW_BPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_broadcast, &es->tx_broadcast);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		i40e_stat_update48(hw, I40E_GLVEBTC_RPCH(i, idx),
				   I40E_GLVEBTC_RPCL(i, idx),
				   veb->stat_offsets_loaded,
				   &veb_oes->tc_rx_packets[i],
				   &veb_es->tc_rx_packets[i]);
		i40e_stat_update48(hw, I40E_GLVEBTC_RBCH(i, idx),
				   I40E_GLVEBTC_RBCL(i, idx),
				   veb->stat_offsets_loaded,
				   &veb_oes->tc_rx_bytes[i],
				   &veb_es->tc_rx_bytes[i]);
		i40e_stat_update48(hw, I40E_GLVEBTC_TPCH(i, idx),
				   I40E_GLVEBTC_TPCL(i, idx),
				   veb->stat_offsets_loaded,
				   &veb_oes->tc_tx_packets[i],
				   &veb_es->tc_tx_packets[i]);
		i40e_stat_update48(hw, I40E_GLVEBTC_TBCH(i, idx),
				   I40E_GLVEBTC_TBCL(i, idx),
				   veb->stat_offsets_loaded,
				   &veb_oes->tc_tx_bytes[i],
				   &veb_es->tc_tx_bytes[i]);
	}
	veb->stat_offsets_loaded = true;
}

/**
 * i40e_update_vsi_stats - Update the vsi statistics counters.
 * @vsi: the VSI to be updated
 *
 * There are a few instances where we store the same stat in a
 * couple of different structs.  This is partly because we have
 * the netdev stats that need to be filled out, which is slightly
 * different from the "eth_stats" defined by the chip and used in
 * VF communications.  We sort it out here.
 **/
static void i40e_update_vsi_stats(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct rtnl_link_stats64 *ons;
	struct rtnl_link_stats64 *ns;   /* netdev stats */
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */
	u32 tx_restart, tx_busy;
	struct i40e_ring *p;
	u32 rx_page, rx_buf;
	u64 bytes, packets;
	unsigned int start;
	u64 tx_linearize;
	u64 tx_force_wb;
	u64 rx_p, rx_b;
	u64 tx_p, tx_b;
	u16 q;

	if (test_bit(__I40E_VSI_DOWN, vsi->state) ||
	    test_bit(__I40E_CONFIG_BUSY, pf->state))
		return;

	ns = i40e_get_vsi_stats_struct(vsi);
	ons = &vsi->net_stats_offsets;
	es = &vsi->eth_stats;
	oes = &vsi->eth_stats_offsets;

	/* Gather up the netdev and vsi stats that the driver collects
	 * on the fly during packet processing
	 */
	rx_b = rx_p = 0;
	tx_b = tx_p = 0;
	tx_restart = tx_busy = tx_linearize = tx_force_wb = 0;
	rx_page = 0;
	rx_buf = 0;
	rcu_read_lock();
	for (q = 0; q < vsi->num_queue_pairs; q++) {
		/* locate Tx ring */
		p = READ_ONCE(vsi->tx_rings[q]);
		if (!p)
			continue;

		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			packets = p->stats.packets;
			bytes = p->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));
		tx_b += bytes;
		tx_p += packets;
		tx_restart += p->tx_stats.restart_queue;
		tx_busy += p->tx_stats.tx_busy;
		tx_linearize += p->tx_stats.tx_linearize;
		tx_force_wb += p->tx_stats.tx_force_wb;

		/* locate Rx ring */
		p = READ_ONCE(vsi->rx_rings[q]);
		if (!p)
			continue;

		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			packets = p->stats.packets;
			bytes = p->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));
		rx_b += bytes;
		rx_p += packets;
		rx_buf += p->rx_stats.alloc_buff_failed;
		rx_page += p->rx_stats.alloc_page_failed;

		if (i40e_enabled_xdp_vsi(vsi)) {
			/* locate XDP ring */
			p = READ_ONCE(vsi->xdp_rings[q]);
			if (!p)
				continue;

			do {
				start = u64_stats_fetch_begin_irq(&p->syncp);
				packets = p->stats.packets;
				bytes = p->stats.bytes;
			} while (u64_stats_fetch_retry_irq(&p->syncp, start));
			tx_b += bytes;
			tx_p += packets;
			tx_restart += p->tx_stats.restart_queue;
			tx_busy += p->tx_stats.tx_busy;
			tx_linearize += p->tx_stats.tx_linearize;
			tx_force_wb += p->tx_stats.tx_force_wb;
		}
	}
	rcu_read_unlock();
	vsi->tx_restart = tx_restart;
	vsi->tx_busy = tx_busy;
	vsi->tx_linearize = tx_linearize;
	vsi->tx_force_wb = tx_force_wb;
	vsi->rx_page_failed = rx_page;
	vsi->rx_buf_failed = rx_buf;

	ns->rx_packets = rx_p;
	ns->rx_bytes = rx_b;
	ns->tx_packets = tx_p;
	ns->tx_bytes = tx_b;

	/* update netdev stats from eth stats */
	i40e_update_eth_stats(vsi);
	ons->tx_errors = oes->tx_errors;
	ns->tx_errors = es->tx_errors;
	ons->multicast = oes->rx_multicast;
	ns->multicast = es->rx_multicast;
	ons->rx_dropped = oes->rx_discards;
	ns->rx_dropped = es->rx_discards;
	ons->tx_dropped = oes->tx_discards;
	ns->tx_dropped = es->tx_discards;

	/* pull in a couple PF stats if this is the main vsi */
	if (vsi == pf->vsi[pf->lan_vsi]) {
		ns->rx_crc_errors = pf->stats.crc_errors;
		ns->rx_errors = pf->stats.crc_errors + pf->stats.illegal_bytes;
		ns->rx_length_errors = pf->stats.rx_length_errors;
	}
}

/**
 * i40e_update_pf_stats - Update the PF statistics counters.
 * @pf: the PF to be updated
 **/
static void i40e_update_pf_stats(struct i40e_pf *pf)
{
	struct i40e_hw_port_stats *osd = &pf->stats_offsets;
	struct i40e_hw_port_stats *nsd = &pf->stats;
	struct i40e_hw *hw = &pf->hw;
	u32 val;
	int i;

	i40e_stat_update48(hw, I40E_GLPRT_GORCH(hw->port),
			   I40E_GLPRT_GORCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_bytes, &nsd->eth.rx_bytes);
	i40e_stat_update48(hw, I40E_GLPRT_GOTCH(hw->port),
			   I40E_GLPRT_GOTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_bytes, &nsd->eth.tx_bytes);
	i40e_stat_update32(hw, I40E_GLPRT_RDPC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_discards,
			   &nsd->eth.rx_discards);
	i40e_stat_update48(hw, I40E_GLPRT_UPRCH(hw->port),
			   I40E_GLPRT_UPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_unicast,
			   &nsd->eth.rx_unicast);
	i40e_stat_update48(hw, I40E_GLPRT_MPRCH(hw->port),
			   I40E_GLPRT_MPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_multicast,
			   &nsd->eth.rx_multicast);
	i40e_stat_update48(hw, I40E_GLPRT_BPRCH(hw->port),
			   I40E_GLPRT_BPRCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.rx_broadcast,
			   &nsd->eth.rx_broadcast);
	i40e_stat_update48(hw, I40E_GLPRT_UPTCH(hw->port),
			   I40E_GLPRT_UPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_unicast,
			   &nsd->eth.tx_unicast);
	i40e_stat_update48(hw, I40E_GLPRT_MPTCH(hw->port),
			   I40E_GLPRT_MPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_multicast,
			   &nsd->eth.tx_multicast);
	i40e_stat_update48(hw, I40E_GLPRT_BPTCH(hw->port),
			   I40E_GLPRT_BPTCL(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->eth.tx_broadcast,
			   &nsd->eth.tx_broadcast);

	i40e_stat_update32(hw, I40E_GLPRT_TDOLD(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_dropped_link_down,
			   &nsd->tx_dropped_link_down);

	i40e_stat_update32(hw, I40E_GLPRT_CRCERRS(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->crc_errors, &nsd->crc_errors);

	i40e_stat_update32(hw, I40E_GLPRT_ILLERRC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->illegal_bytes, &nsd->illegal_bytes);

	i40e_stat_update32(hw, I40E_GLPRT_MLFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->mac_local_faults,
			   &nsd->mac_local_faults);
	i40e_stat_update32(hw, I40E_GLPRT_MRFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->mac_remote_faults,
			   &nsd->mac_remote_faults);

	i40e_stat_update32(hw, I40E_GLPRT_RLEC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_length_errors,
			   &nsd->rx_length_errors);

	i40e_stat_update32(hw, I40E_GLPRT_LXONRXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xon_rx, &nsd->link_xon_rx);
	i40e_stat_update32(hw, I40E_GLPRT_LXONTXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xon_tx, &nsd->link_xon_tx);
	i40e_stat_update32(hw, I40E_GLPRT_LXOFFRXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xoff_rx, &nsd->link_xoff_rx);
	i40e_stat_update32(hw, I40E_GLPRT_LXOFFTXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xoff_tx, &nsd->link_xoff_tx);

	for (i = 0; i < 8; i++) {
		i40e_stat_update32(hw, I40E_GLPRT_PXOFFRXC(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xoff_rx[i],
				   &nsd->priority_xoff_rx[i]);
		i40e_stat_update32(hw, I40E_GLPRT_PXONRXC(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xon_rx[i],
				   &nsd->priority_xon_rx[i]);
		i40e_stat_update32(hw, I40E_GLPRT_PXONTXC(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xon_tx[i],
				   &nsd->priority_xon_tx[i]);
		i40e_stat_update32(hw, I40E_GLPRT_PXOFFTXC(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xoff_tx[i],
				   &nsd->priority_xoff_tx[i]);
		i40e_stat_update32(hw,
				   I40E_GLPRT_RXON2OFFCNT(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xon_2_xoff[i],
				   &nsd->priority_xon_2_xoff[i]);
	}

	i40e_stat_update48(hw, I40E_GLPRT_PRC64H(hw->port),
			   I40E_GLPRT_PRC64L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_64, &nsd->rx_size_64);
	i40e_stat_update48(hw, I40E_GLPRT_PRC127H(hw->port),
			   I40E_GLPRT_PRC127L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_127, &nsd->rx_size_127);
	i40e_stat_update48(hw, I40E_GLPRT_PRC255H(hw->port),
			   I40E_GLPRT_PRC255L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_255, &nsd->rx_size_255);
	i40e_stat_update48(hw, I40E_GLPRT_PRC511H(hw->port),
			   I40E_GLPRT_PRC511L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_511, &nsd->rx_size_511);
	i40e_stat_update48(hw, I40E_GLPRT_PRC1023H(hw->port),
			   I40E_GLPRT_PRC1023L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_1023, &nsd->rx_size_1023);
	i40e_stat_update48(hw, I40E_GLPRT_PRC1522H(hw->port),
			   I40E_GLPRT_PRC1522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_1522, &nsd->rx_size_1522);
	i40e_stat_update48(hw, I40E_GLPRT_PRC9522H(hw->port),
			   I40E_GLPRT_PRC9522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_size_big, &nsd->rx_size_big);

	i40e_stat_update48(hw, I40E_GLPRT_PTC64H(hw->port),
			   I40E_GLPRT_PTC64L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_64, &nsd->tx_size_64);
	i40e_stat_update48(hw, I40E_GLPRT_PTC127H(hw->port),
			   I40E_GLPRT_PTC127L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_127, &nsd->tx_size_127);
	i40e_stat_update48(hw, I40E_GLPRT_PTC255H(hw->port),
			   I40E_GLPRT_PTC255L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_255, &nsd->tx_size_255);
	i40e_stat_update48(hw, I40E_GLPRT_PTC511H(hw->port),
			   I40E_GLPRT_PTC511L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_511, &nsd->tx_size_511);
	i40e_stat_update48(hw, I40E_GLPRT_PTC1023H(hw->port),
			   I40E_GLPRT_PTC1023L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_1023, &nsd->tx_size_1023);
	i40e_stat_update48(hw, I40E_GLPRT_PTC1522H(hw->port),
			   I40E_GLPRT_PTC1522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_1522, &nsd->tx_size_1522);
	i40e_stat_update48(hw, I40E_GLPRT_PTC9522H(hw->port),
			   I40E_GLPRT_PTC9522L(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->tx_size_big, &nsd->tx_size_big);

	i40e_stat_update32(hw, I40E_GLPRT_RUC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_undersize, &nsd->rx_undersize);
	i40e_stat_update32(hw, I40E_GLPRT_RFC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_fragments, &nsd->rx_fragments);
	i40e_stat_update32(hw, I40E_GLPRT_ROC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_oversize, &nsd->rx_oversize);
	i40e_stat_update32(hw, I40E_GLPRT_RJC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->rx_jabber, &nsd->rx_jabber);

	/* FDIR stats */
	i40e_stat_update_and_clear32(hw,
			I40E_GLQF_PCNT(I40E_FD_ATR_STAT_IDX(hw->pf_id)),
			&nsd->fd_atr_match);
	i40e_stat_update_and_clear32(hw,
			I40E_GLQF_PCNT(I40E_FD_SB_STAT_IDX(hw->pf_id)),
			&nsd->fd_sb_match);
	i40e_stat_update_and_clear32(hw,
			I40E_GLQF_PCNT(I40E_FD_ATR_TUNNEL_STAT_IDX(hw->pf_id)),
			&nsd->fd_atr_tunnel_match);

	val = rd32(hw, I40E_PRTPM_EEE_STAT);
	nsd->tx_lpi_status =
		       (val & I40E_PRTPM_EEE_STAT_TX_LPI_STATUS_MASK) >>
			I40E_PRTPM_EEE_STAT_TX_LPI_STATUS_SHIFT;
	nsd->rx_lpi_status =
		       (val & I40E_PRTPM_EEE_STAT_RX_LPI_STATUS_MASK) >>
			I40E_PRTPM_EEE_STAT_RX_LPI_STATUS_SHIFT;
	i40e_stat_update32(hw, I40E_PRTPM_TLPIC,
			   pf->stat_offsets_loaded,
			   &osd->tx_lpi_count, &nsd->tx_lpi_count);
	i40e_stat_update32(hw, I40E_PRTPM_RLPIC,
			   pf->stat_offsets_loaded,
			   &osd->rx_lpi_count, &nsd->rx_lpi_count);

	if (pf->flags & I40E_FLAG_FD_SB_ENABLED &&
	    !test_bit(__I40E_FD_SB_AUTO_DISABLED, pf->state))
		nsd->fd_sb_status = true;
	else
		nsd->fd_sb_status = false;

	if (pf->flags & I40E_FLAG_FD_ATR_ENABLED &&
	    !test_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state))
		nsd->fd_atr_status = true;
	else
		nsd->fd_atr_status = false;

	pf->stat_offsets_loaded = true;
}

/**
 * i40e_update_stats - Update the various statistics counters.
 * @vsi: the VSI to be updated
 *
 * Update the various stats for this VSI and its related entities.
 **/
void i40e_update_stats(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	if (vsi == pf->vsi[pf->lan_vsi])
		i40e_update_pf_stats(pf);

	i40e_update_vsi_stats(vsi);
}

/**
 * i40e_count_filters - counts VSI mac filters
 * @vsi: the VSI to be searched
 *
 * Returns count of mac filters
 **/
int i40e_count_filters(struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f;
	struct hlist_node *h;
	int bkt;
	int cnt = 0;

	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist)
		++cnt;

	return cnt;
}

/**
 * i40e_find_filter - Search VSI filter list for specific mac/vlan filter
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 *
 * Returns ptr to the filter object or NULL
 **/
static struct i40e_mac_filter *i40e_find_filter(struct i40e_vsi *vsi,
						const u8 *macaddr, s16 vlan)
{
	struct i40e_mac_filter *f;
	u64 key;

	if (!vsi || !macaddr)
		return NULL;

	key = i40e_addr_to_hkey(macaddr);
	hash_for_each_possible(vsi->mac_filter_hash, f, hlist, key) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (vlan == f->vlan))
			return f;
	}
	return NULL;
}

/**
 * i40e_find_mac - Find a mac addr in the macvlan filters list
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address we are searching for
 *
 * Returns the first filter with the provided MAC address or NULL if
 * MAC address was not found
 **/
struct i40e_mac_filter *i40e_find_mac(struct i40e_vsi *vsi, const u8 *macaddr)
{
	struct i40e_mac_filter *f;
	u64 key;

	if (!vsi || !macaddr)
		return NULL;

	key = i40e_addr_to_hkey(macaddr);
	hash_for_each_possible(vsi->mac_filter_hash, f, hlist, key) {
		if ((ether_addr_equal(macaddr, f->macaddr)))
			return f;
	}
	return NULL;
}

/**
 * i40e_is_vsi_in_vlan - Check if VSI is in vlan mode
 * @vsi: the VSI to be searched
 *
 * Returns true if VSI is in vlan mode or false otherwise
 **/
bool i40e_is_vsi_in_vlan(struct i40e_vsi *vsi)
{
	/* If we have a PVID, always operate in VLAN mode */
	if (vsi->info.pvid)
		return true;

	/* We need to operate in VLAN mode whenever we have any filters with
	 * a VLAN other than I40E_VLAN_ALL. We could check the table each
	 * time, incurring search cost repeatedly. However, we can notice two
	 * things:
	 *
	 * 1) the only place where we can gain a VLAN filter is in
	 *    i40e_add_filter.
	 *
	 * 2) the only place where filters are actually removed is in
	 *    i40e_sync_filters_subtask.
	 *
	 * Thus, we can simply use a boolean value, has_vlan_filters which we
	 * will set to true when we add a VLAN filter in i40e_add_filter. Then
	 * we have to perform the full search after deleting filters in
	 * i40e_sync_filters_subtask, but we already have to search
	 * filters here and can perform the check at the same time. This
	 * results in avoiding embedding a loop for VLAN mode inside another
	 * loop over all the filters, and should maintain correctness as noted
	 * above.
	 */
	return vsi->has_vlan_filter;
}

/**
 * i40e_correct_mac_vlan_filters - Correct non-VLAN filters if necessary
 * @vsi: the VSI to configure
 * @tmp_add_list: list of filters ready to be added
 * @tmp_del_list: list of filters ready to be deleted
 * @vlan_filters: the number of active VLAN filters
 *
 * Update VLAN=0 and VLAN=-1 (I40E_VLAN_ANY) filters properly so that they
 * behave as expected. If we have any active VLAN filters remaining or about
 * to be added then we need to update non-VLAN filters to be marked as VLAN=0
 * so that they only match against untagged traffic. If we no longer have any
 * active VLAN filters, we need to make all non-VLAN filters marked as VLAN=-1
 * so that they match against both tagged and untagged traffic. In this way,
 * we ensure that we correctly receive the desired traffic. This ensures that
 * when we have an active VLAN we will receive only untagged traffic and
 * traffic matching active VLANs. If we have no active VLANs then we will
 * operate in non-VLAN mode and receive all traffic, tagged or untagged.
 *
 * Finally, in a similar fashion, this function also corrects filters when
 * there is an active PVID assigned to this VSI.
 *
 * In case of memory allocation failure return -ENOMEM. Otherwise, return 0.
 *
 * This function is only expected to be called from within
 * i40e_sync_vsi_filters.
 *
 * NOTE: This function expects to be called while under the
 * mac_filter_hash_lock
 */
static int i40e_correct_mac_vlan_filters(struct i40e_vsi *vsi,
					 struct hlist_head *tmp_add_list,
					 struct hlist_head *tmp_del_list,
					 int vlan_filters)
{
	s16 pvid = le16_to_cpu(vsi->info.pvid);
	struct i40e_mac_filter *f, *add_head;
	struct i40e_new_mac_filter *new;
	struct hlist_node *h;
	int bkt, new_vlan;

	/* To determine if a particular filter needs to be replaced we
	 * have the three following conditions:
	 *
	 * a) if we have a PVID assigned, then all filters which are
	 *    not marked as VLAN=PVID must be replaced with filters that
	 *    are.
	 * b) otherwise, if we have any active VLANS, all filters
	 *    which are marked as VLAN=-1 must be replaced with
	 *    filters marked as VLAN=0
	 * c) finally, if we do not have any active VLANS, all filters
	 *    which are marked as VLAN=0 must be replaced with filters
	 *    marked as VLAN=-1
	 */

	/* Update the filters about to be added in place */
	hlist_for_each_entry(new, tmp_add_list, hlist) {
		if (pvid && new->f->vlan != pvid)
			new->f->vlan = pvid;
		else if (vlan_filters && new->f->vlan == I40E_VLAN_ANY)
			new->f->vlan = 0;
		else if (!vlan_filters && new->f->vlan == 0)
			new->f->vlan = I40E_VLAN_ANY;
	}

	/* Update the remaining active filters */
	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		/* Combine the checks for whether a filter needs to be changed
		 * and then determine the new VLAN inside the if block, in
		 * order to avoid duplicating code for adding the new filter
		 * then deleting the old filter.
		 */
		if ((pvid && f->vlan != pvid) ||
		    (vlan_filters && f->vlan == I40E_VLAN_ANY) ||
		    (!vlan_filters && f->vlan == 0)) {
			/* Determine the new vlan we will be adding */
			if (pvid)
				new_vlan = pvid;
			else if (vlan_filters)
				new_vlan = 0;
			else
				new_vlan = I40E_VLAN_ANY;

			/* Create the new filter */
			add_head = i40e_add_filter(vsi, f->macaddr, new_vlan);
			if (!add_head)
				return -ENOMEM;

			/* Create a temporary i40e_new_mac_filter */
			new = kzalloc(sizeof(*new), GFP_ATOMIC);
			if (!new)
				return -ENOMEM;

			new->f = add_head;
			new->state = add_head->state;

			/* Add the new filter to the tmp list */
			hlist_add_head(&new->hlist, tmp_add_list);

			/* Put the original filter into the delete list */
			f->state = I40E_FILTER_REMOVE;
			hash_del(&f->hlist);
			hlist_add_head(&f->hlist, tmp_del_list);
		}
	}

	vsi->has_vlan_filter = !!vlan_filters;

	return 0;
}

/**
 * i40e_rm_default_mac_filter - Remove the default MAC filter set by NVM
 * @vsi: the PF Main VSI - inappropriate for any other VSI
 * @macaddr: the MAC address
 *
 * Remove whatever filter the firmware set up so the driver can manage
 * its own filtering intelligently.
 **/
static void i40e_rm_default_mac_filter(struct i40e_vsi *vsi, u8 *macaddr)
{
	struct i40e_aqc_remove_macvlan_element_data element;
	struct i40e_pf *pf = vsi->back;

	/* Only appropriate for the PF main VSI */
	if (vsi->type != I40E_VSI_MAIN)
		return;

	memset(&element, 0, sizeof(element));
	ether_addr_copy(element.mac_addr, macaddr);
	element.vlan_tag = 0;
	/* Ignore error returns, some firmware does it this way... */
	element.flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
	i40e_aq_remove_macvlan(&pf->hw, vsi->seid, &element, 1, NULL);

	memset(&element, 0, sizeof(element));
	ether_addr_copy(element.mac_addr, macaddr);
	element.vlan_tag = 0;
	/* ...and some firmware does it this way. */
	element.flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH |
			I40E_AQC_MACVLAN_DEL_IGNORE_VLAN;
	i40e_aq_remove_macvlan(&pf->hw, vsi->seid, &element, 1, NULL);
}

/**
 * i40e_add_filter - Add a mac/vlan filter to the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 *
 * Returns ptr to the filter object or NULL when no memory available.
 *
 * NOTE: This function is expected to be called with mac_filter_hash_lock
 * being held.
 **/
struct i40e_mac_filter *i40e_add_filter(struct i40e_vsi *vsi,
					const u8 *macaddr, s16 vlan)
{
	struct i40e_mac_filter *f;
	u64 key;

	if (!vsi || !macaddr)
		return NULL;

	f = i40e_find_filter(vsi, macaddr, vlan);
	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);
		if (!f)
			return NULL;

		/* Update the boolean indicating if we need to function in
		 * VLAN mode.
		 */
		if (vlan >= 0)
			vsi->has_vlan_filter = true;

		ether_addr_copy(f->macaddr, macaddr);
		f->vlan = vlan;
		f->state = I40E_FILTER_NEW;
		INIT_HLIST_NODE(&f->hlist);

		key = i40e_addr_to_hkey(macaddr);
		hash_add(vsi->mac_filter_hash, &f->hlist, key);

		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		set_bit(__I40E_MACVLAN_SYNC_PENDING, vsi->back->state);
	}

	/* If we're asked to add a filter that has been marked for removal, it
	 * is safe to simply restore it to active state. __i40e_del_filter
	 * will have simply deleted any filters which were previously marked
	 * NEW or FAILED, so if it is currently marked REMOVE it must have
	 * previously been ACTIVE. Since we haven't yet run the sync filters
	 * task, just restore this filter to the ACTIVE state so that the
	 * sync task leaves it in place
	 */
	if (f->state == I40E_FILTER_REMOVE)
		f->state = I40E_FILTER_ACTIVE;

	return f;
}

/**
 * __i40e_del_filter - Remove a specific filter from the VSI
 * @vsi: VSI to remove from
 * @f: the filter to remove from the list
 *
 * This function should be called instead of i40e_del_filter only if you know
 * the exact filter you will remove already, such as via i40e_find_filter or
 * i40e_find_mac.
 *
 * NOTE: This function is expected to be called with mac_filter_hash_lock
 * being held.
 * ANOTHER NOTE: This function MUST be called from within the context of
 * the "safe" variants of any list iterators, e.g. list_for_each_entry_safe()
 * instead of list_for_each_entry().
 **/
void __i40e_del_filter(struct i40e_vsi *vsi, struct i40e_mac_filter *f)
{
	if (!f)
		return;

	/* If the filter was never added to firmware then we can just delete it
	 * directly and we don't want to set the status to remove or else an
	 * admin queue command will unnecessarily fire.
	 */
	if ((f->state == I40E_FILTER_FAILED) ||
	    (f->state == I40E_FILTER_NEW)) {
		hash_del(&f->hlist);
		kfree(f);
	} else {
		f->state = I40E_FILTER_REMOVE;
	}

	vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
	set_bit(__I40E_MACVLAN_SYNC_PENDING, vsi->back->state);
}

/**
 * i40e_del_filter - Remove a MAC/VLAN filter from the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the VLAN
 *
 * NOTE: This function is expected to be called with mac_filter_hash_lock
 * being held.
 * ANOTHER NOTE: This function MUST be called from within the context of
 * the "safe" variants of any list iterators, e.g. list_for_each_entry_safe()
 * instead of list_for_each_entry().
 **/
void i40e_del_filter(struct i40e_vsi *vsi, const u8 *macaddr, s16 vlan)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return;

	f = i40e_find_filter(vsi, macaddr, vlan);
	__i40e_del_filter(vsi, f);
}

/**
 * i40e_add_mac_filter - Add a MAC filter for all active VLANs
 * @vsi: the VSI to be searched
 * @macaddr: the mac address to be filtered
 *
 * If we're not in VLAN mode, just add the filter to I40E_VLAN_ANY. Otherwise,
 * go through all the macvlan filters and add a macvlan filter for each
 * unique vlan that already exists. If a PVID has been assigned, instead only
 * add the macaddr to that VLAN.
 *
 * Returns last filter added on success, else NULL
 **/
struct i40e_mac_filter *i40e_add_mac_filter(struct i40e_vsi *vsi,
					    const u8 *macaddr)
{
	struct i40e_mac_filter *f, *add = NULL;
	struct hlist_node *h;
	int bkt;

	if (vsi->info.pvid)
		return i40e_add_filter(vsi, macaddr,
				       le16_to_cpu(vsi->info.pvid));

	if (!i40e_is_vsi_in_vlan(vsi))
		return i40e_add_filter(vsi, macaddr, I40E_VLAN_ANY);

	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		if (f->state == I40E_FILTER_REMOVE)
			continue;
		add = i40e_add_filter(vsi, macaddr, f->vlan);
		if (!add)
			return NULL;
	}

	return add;
}

/**
 * i40e_del_mac_filter - Remove a MAC filter from all VLANs
 * @vsi: the VSI to be searched
 * @macaddr: the mac address to be removed
 *
 * Removes a given MAC address from a VSI regardless of what VLAN it has been
 * associated with.
 *
 * Returns 0 for success, or error
 **/
int i40e_del_mac_filter(struct i40e_vsi *vsi, const u8 *macaddr)
{
	struct i40e_mac_filter *f;
	struct hlist_node *h;
	bool found = false;
	int bkt;

	lockdep_assert_held(&vsi->mac_filter_hash_lock);
	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		if (ether_addr_equal(macaddr, f->macaddr)) {
			__i40e_del_filter(vsi, f);
			found = true;
		}
	}

	if (found)
		return 0;
	else
		return -ENOENT;
}

/**
 * i40e_set_mac - NDO callback to set mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_set_mac(struct net_device *netdev, void *p)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, addr->sa_data)) {
		netdev_info(netdev, "already using mac address %pM\n",
			    addr->sa_data);
		return 0;
	}

	if (test_bit(__I40E_DOWN, pf->state) ||
	    test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(hw->mac.addr, addr->sa_data))
		netdev_info(netdev, "returning to hw mac address %pM\n",
			    hw->mac.addr);
	else
		netdev_info(netdev, "set new mac address %pM\n", addr->sa_data);

	/* Copy the address first, so that we avoid a possible race with
	 * .set_rx_mode().
	 * - Remove old address from MAC filter
	 * - Copy new address
	 * - Add new address to MAC filter
	 */
	spin_lock_bh(&vsi->mac_filter_hash_lock);
	i40e_del_mac_filter(vsi, netdev->dev_addr);
	ether_addr_copy(netdev->dev_addr, addr->sa_data);
	i40e_add_mac_filter(vsi, netdev->dev_addr);
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	if (vsi->type == I40E_VSI_MAIN) {
		i40e_status ret;

		ret = i40e_aq_mac_address_write(hw, I40E_AQC_WRITE_TYPE_LAA_WOL,
						addr->sa_data, NULL);
		if (ret)
			netdev_info(netdev, "Ignoring error from firmware on LAA update, status %s, AQ ret %s\n",
				    i40e_stat_str(hw, ret),
				    i40e_aq_str(hw, hw->aq.asq_last_status));
	}

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(pf);
	return 0;
}

/**
 * i40e_config_rss_aq - Prepare for RSS using AQ commands
 * @vsi: vsi structure
 * @seed: RSS hash seed
 * @lut: pointer to lookup table of lut_size
 * @lut_size: size of the lookup table
 **/
static int i40e_config_rss_aq(struct i40e_vsi *vsi, const u8 *seed,
			      u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int ret = 0;

	if (seed) {
		struct i40e_aqc_get_set_rss_key_data *seed_dw =
			(struct i40e_aqc_get_set_rss_key_data *)seed;
		ret = i40e_aq_set_rss_key(hw, vsi->id, seed_dw);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Cannot set RSS key, err %s aq_err %s\n",
				 i40e_stat_str(hw, ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
			return ret;
		}
	}
	if (lut) {
		bool pf_lut = vsi->type == I40E_VSI_MAIN;

		ret = i40e_aq_set_rss_lut(hw, vsi->id, pf_lut, lut, lut_size);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Cannot set RSS lut, err %s aq_err %s\n",
				 i40e_stat_str(hw, ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
			return ret;
		}
	}
	return ret;
}

/**
 * i40e_vsi_config_rss - Prepare for VSI(VMDq) RSS if used
 * @vsi: VSI structure
 **/
static int i40e_vsi_config_rss(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	u8 seed[I40E_HKEY_ARRAY_SIZE];
	u8 *lut;
	int ret;

	if (!(pf->hw_features & I40E_HW_RSS_AQ_CAPABLE))
		return 0;
	if (!vsi->rss_size)
		vsi->rss_size = min_t(int, pf->alloc_rss_size,
				      vsi->num_queue_pairs);
	if (!vsi->rss_size)
		return -EINVAL;
	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	/* Use the user configured hash keys and lookup table if there is one,
	 * otherwise use default
	 */
	if (vsi->rss_lut_user)
		memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
	else
		i40e_fill_rss_lut(pf, lut, vsi->rss_table_size, vsi->rss_size);
	if (vsi->rss_hkey_user)
		memcpy(seed, vsi->rss_hkey_user, I40E_HKEY_ARRAY_SIZE);
	else
		netdev_rss_key_fill((void *)seed, I40E_HKEY_ARRAY_SIZE);
	ret = i40e_config_rss_aq(vsi, seed, lut, vsi->rss_table_size);
	kfree(lut);
	return ret;
}

/**
 * i40e_vsi_setup_queue_map_mqprio - Prepares mqprio based tc_config
 * @vsi: the VSI being configured,
 * @ctxt: VSI context structure
 * @enabled_tc: number of traffic classes to enable
 *
 * Prepares VSI tc_config to have queue configurations based on MQPRIO options.
 **/
static int i40e_vsi_setup_queue_map_mqprio(struct i40e_vsi *vsi,
					   struct i40e_vsi_context *ctxt,
					   u8 enabled_tc)
{
	u16 qcount = 0, max_qcount, qmap, sections = 0;
	int i, override_q, pow, num_qps, ret;
	u8 netdev_tc = 0, offset = 0;

	if (vsi->type != I40E_VSI_MAIN)
		return -EINVAL;
	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	sections |= I40E_AQ_VSI_PROP_SCHED_VALID;
	vsi->tc_config.numtc = vsi->mqprio_qopt.qopt.num_tc;
	vsi->tc_config.enabled_tc = enabled_tc ? enabled_tc : 1;
	num_qps = vsi->mqprio_qopt.qopt.count[0];

	/* find the next higher power-of-2 of num queue pairs */
	pow = ilog2(num_qps);
	if (!is_power_of_2(num_qps))
		pow++;
	qmap = (offset << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
		(pow << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT);

	/* Setup queue offset/count for all TCs for given VSI */
	max_qcount = vsi->mqprio_qopt.qopt.count[0];
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* See if the given TC is enabled for the given VSI */
		if (vsi->tc_config.enabled_tc & BIT(i)) {
			offset = vsi->mqprio_qopt.qopt.offset[i];
			qcount = vsi->mqprio_qopt.qopt.count[i];
			if (qcount > max_qcount)
				max_qcount = qcount;
			vsi->tc_config.tc_info[i].qoffset = offset;
			vsi->tc_config.tc_info[i].qcount = qcount;
			vsi->tc_config.tc_info[i].netdev_tc = netdev_tc++;
		} else {
			/* TC is not enabled so set the offset to
			 * default queue and allocate one queue
			 * for the given TC.
			 */
			vsi->tc_config.tc_info[i].qoffset = 0;
			vsi->tc_config.tc_info[i].qcount = 1;
			vsi->tc_config.tc_info[i].netdev_tc = 0;
		}
	}

	/* Set actual Tx/Rx queue pairs */
	vsi->num_queue_pairs = offset + qcount;

	/* Setup queue TC[0].qmap for given VSI context */
	ctxt->info.tc_mapping[0] = cpu_to_le16(qmap);
	ctxt->info.mapping_flags |= cpu_to_le16(I40E_AQ_VSI_QUE_MAP_CONTIG);
	ctxt->info.queue_mapping[0] = cpu_to_le16(vsi->base_queue);
	ctxt->info.valid_sections |= cpu_to_le16(sections);

	/* Reconfigure RSS for main VSI with max queue count */
	vsi->rss_size = max_qcount;
	ret = i40e_vsi_config_rss(vsi);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to reconfig rss for num_queues (%u)\n",
			 max_qcount);
		return ret;
	}
	vsi->reconfig_rss = true;
	dev_dbg(&vsi->back->pdev->dev,
		"Reconfigured rss with num_queues (%u)\n", max_qcount);

	/* Find queue count available for channel VSIs and starting offset
	 * for channel VSIs
	 */
	override_q = vsi->mqprio_qopt.qopt.count[0];
	if (override_q && override_q < vsi->num_queue_pairs) {
		vsi->cnt_q_avail = vsi->num_queue_pairs - override_q;
		vsi->next_base_queue = override_q;
	}
	return 0;
}

/**
 * i40e_vsi_setup_queue_map - Setup a VSI queue map based on enabled_tc
 * @vsi: the VSI being setup
 * @ctxt: VSI context structure
 * @enabled_tc: Enabled TCs bitmap
 * @is_add: True if called before Add VSI
 *
 * Setup VSI queue mapping for enabled traffic classes.
 **/
static void i40e_vsi_setup_queue_map(struct i40e_vsi *vsi,
				     struct i40e_vsi_context *ctxt,
				     u8 enabled_tc,
				     bool is_add)
{
	struct i40e_pf *pf = vsi->back;
	u16 num_tc_qps = 0;
	u16 sections = 0;
	u8 netdev_tc = 0;
	u16 numtc = 1;
	u16 qcount;
	u8 offset;
	u16 qmap;
	int i;

	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	offset = 0;
	/* zero out queue mapping, it will get updated on the end of the function */
	memset(ctxt->info.queue_mapping, 0, sizeof(ctxt->info.queue_mapping));

	if (vsi->type == I40E_VSI_MAIN) {
		/* This code helps add more queue to the VSI if we have
		 * more cores than RSS can support, the higher cores will
		 * be served by ATR or other filters. Furthermore, the
		 * non-zero req_queue_pairs says that user requested a new
		 * queue count via ethtool's set_channels, so use this
		 * value for queues distribution across traffic classes
		 */
		if (vsi->req_queue_pairs > 0)
			vsi->num_queue_pairs = vsi->req_queue_pairs;
		else if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			vsi->num_queue_pairs = pf->num_lan_msix;
	}

	/* Number of queues per enabled TC */
	if (vsi->type == I40E_VSI_MAIN ||
	    (vsi->type == I40E_VSI_SRIOV && vsi->num_queue_pairs != 0))
		num_tc_qps = vsi->num_queue_pairs;
	else
		num_tc_qps = vsi->alloc_queue_pairs;

	if (enabled_tc && (vsi->back->flags & I40E_FLAG_DCB_ENABLED)) {
		/* Find numtc from enabled TC bitmap */
		for (i = 0, numtc = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
			if (enabled_tc & BIT(i)) /* TC is enabled */
				numtc++;
		}
		if (!numtc) {
			dev_warn(&pf->pdev->dev, "DCB is enabled but no TC enabled, forcing TC0\n");
			numtc = 1;
		}
		num_tc_qps = num_tc_qps / numtc;
		num_tc_qps = min_t(int, num_tc_qps,
				   i40e_pf_get_max_q_per_tc(pf));
	}

	vsi->tc_config.numtc = numtc;
	vsi->tc_config.enabled_tc = enabled_tc ? enabled_tc : 1;

	/* Do not allow use more TC queue pairs than MSI-X vectors exist */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		num_tc_qps = min_t(int, num_tc_qps, pf->num_lan_msix);

	/* Setup queue offset/count for all TCs for given VSI */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* See if the given TC is enabled for the given VSI */
		if (vsi->tc_config.enabled_tc & BIT(i)) {
			/* TC is enabled */
			int pow, num_qps;

			switch (vsi->type) {
			case I40E_VSI_MAIN:
				if (!(pf->flags & (I40E_FLAG_FD_SB_ENABLED |
				    I40E_FLAG_FD_ATR_ENABLED)) ||
				    vsi->tc_config.enabled_tc != 1) {
					qcount = min_t(int, pf->alloc_rss_size,
						       num_tc_qps);
					break;
				}
				fallthrough;
			case I40E_VSI_FDIR:
			case I40E_VSI_SRIOV:
			case I40E_VSI_VMDQ2:
			default:
				qcount = num_tc_qps;
				WARN_ON(i != 0);
				break;
			}
			vsi->tc_config.tc_info[i].qoffset = offset;
			vsi->tc_config.tc_info[i].qcount = qcount;

			/* find the next higher power-of-2 of num queue pairs */
			num_qps = qcount;
			pow = 0;
			while (num_qps && (BIT_ULL(pow) < qcount)) {
				pow++;
				num_qps >>= 1;
			}

			vsi->tc_config.tc_info[i].netdev_tc = netdev_tc++;
			qmap =
			    (offset << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
			    (pow << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT);

			offset += qcount;
		} else {
			/* TC is not enabled so set the offset to
			 * default queue and allocate one queue
			 * for the given TC.
			 */
			vsi->tc_config.tc_info[i].qoffset = 0;
			vsi->tc_config.tc_info[i].qcount = 1;
			vsi->tc_config.tc_info[i].netdev_tc = 0;

			qmap = 0;
		}
		ctxt->info.tc_mapping[i] = cpu_to_le16(qmap);
	}
	/* Do not change previously set num_queue_pairs for PFs and VFs*/
	if ((vsi->type == I40E_VSI_MAIN && numtc != 1) ||
	    (vsi->type == I40E_VSI_SRIOV && vsi->num_queue_pairs == 0) ||
	    (vsi->type != I40E_VSI_MAIN && vsi->type != I40E_VSI_SRIOV))
		vsi->num_queue_pairs = offset;

	/* Scheduler section valid can only be set for ADD VSI */
	if (is_add) {
		sections |= I40E_AQ_VSI_PROP_SCHED_VALID;

		ctxt->info.up_enable_bits = enabled_tc;
	}
	if (vsi->type == I40E_VSI_SRIOV) {
		ctxt->info.mapping_flags |=
				     cpu_to_le16(I40E_AQ_VSI_QUE_MAP_NONCONTIG);
		for (i = 0; i < vsi->num_queue_pairs; i++)
			ctxt->info.queue_mapping[i] =
					       cpu_to_le16(vsi->base_queue + i);
	} else {
		ctxt->info.mapping_flags |=
					cpu_to_le16(I40E_AQ_VSI_QUE_MAP_CONTIG);
		ctxt->info.queue_mapping[0] = cpu_to_le16(vsi->base_queue);
	}
	ctxt->info.valid_sections |= cpu_to_le16(sections);
}

/**
 * i40e_addr_sync - Callback for dev_(mc|uc)_sync to add address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be added. We call
 * __dev_(uc|mc)_sync from .set_rx_mode and guarantee to hold the hash lock.
 */
static int i40e_addr_sync(struct net_device *netdev, const u8 *addr)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (i40e_add_mac_filter(vsi, addr))
		return 0;
	else
		return -ENOMEM;
}

/**
 * i40e_addr_unsync - Callback for dev_(mc|uc)_sync to remove address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be removed. We call
 * __dev_(uc|mc)_sync from .set_rx_mode and guarantee to hold the hash lock.
 */
static int i40e_addr_unsync(struct net_device *netdev, const u8 *addr)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	/* Under some circumstances, we might receive a request to delete
	 * our own device address from our uc list. Because we store the
	 * device address in the VSI's MAC/VLAN filter list, we need to ignore
	 * such requests and not delete our device address from this list.
	 */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	i40e_del_mac_filter(vsi, addr);

	return 0;
}

/**
 * i40e_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 **/
static void i40e_set_rx_mode(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	spin_lock_bh(&vsi->mac_filter_hash_lock);

	__dev_uc_sync(netdev, i40e_addr_sync, i40e_addr_unsync);
	__dev_mc_sync(netdev, i40e_addr_sync, i40e_addr_unsync);

	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	/* check for other flag changes */
	if (vsi->current_netdev_flags != vsi->netdev->flags) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		set_bit(__I40E_MACVLAN_SYNC_PENDING, vsi->back->state);
	}
}

/**
 * i40e_undo_del_filter_entries - Undo the changes made to MAC filter entries
 * @vsi: Pointer to VSI struct
 * @from: Pointer to list which contains MAC filter entries - changes to
 *        those entries needs to be undone.
 *
 * MAC filter entries from this list were slated for deletion.
 **/
static void i40e_undo_del_filter_entries(struct i40e_vsi *vsi,
					 struct hlist_head *from)
{
	struct i40e_mac_filter *f;
	struct hlist_node *h;

	hlist_for_each_entry_safe(f, h, from, hlist) {
		u64 key = i40e_addr_to_hkey(f->macaddr);

		/* Move the element back into MAC filter list*/
		hlist_del(&f->hlist);
		hash_add(vsi->mac_filter_hash, &f->hlist, key);
	}
}

/**
 * i40e_undo_add_filter_entries - Undo the changes made to MAC filter entries
 * @vsi: Pointer to vsi struct
 * @from: Pointer to list which contains MAC filter entries - changes to
 *        those entries needs to be undone.
 *
 * MAC filter entries from this list were slated for addition.
 **/
static void i40e_undo_add_filter_entries(struct i40e_vsi *vsi,
					 struct hlist_head *from)
{
	struct i40e_new_mac_filter *new;
	struct hlist_node *h;

	hlist_for_each_entry_safe(new, h, from, hlist) {
		/* We can simply free the wrapper structure */
		hlist_del(&new->hlist);
		netdev_hw_addr_refcnt(new->f, vsi->netdev, -1);
		kfree(new);
	}
}

/**
 * i40e_next_entry - Get the next non-broadcast filter from a list
 * @next: pointer to filter in list
 *
 * Returns the next non-broadcast filter in the list. Required so that we
 * ignore broadcast filters within the list, since these are not handled via
 * the normal firmware update path.
 */
static
struct i40e_new_mac_filter *i40e_next_filter(struct i40e_new_mac_filter *next)
{
	hlist_for_each_entry_continue(next, hlist) {
		if (!is_broadcast_ether_addr(next->f->macaddr))
			return next;
	}

	return NULL;
}

/**
 * i40e_update_filter_state - Update filter state based on return data
 * from firmware
 * @count: Number of filters added
 * @add_list: return data from fw
 * @add_head: pointer to first filter in current batch
 *
 * MAC filter entries from list were slated to be added to device. Returns
 * number of successful filters. Note that 0 does NOT mean success!
 **/
static int
i40e_update_filter_state(int count,
			 struct i40e_aqc_add_macvlan_element_data *add_list,
			 struct i40e_new_mac_filter *add_head)
{
	int retval = 0;
	int i;

	for (i = 0; i < count; i++) {
		/* Always check status of each filter. We don't need to check
		 * the firmware return status because we pre-set the filter
		 * status to I40E_AQC_MM_ERR_NO_RES when sending the filter
		 * request to the adminq. Thus, if it no longer matches then
		 * we know the filter is active.
		 */
		if (add_list[i].match_method == I40E_AQC_MM_ERR_NO_RES) {
			add_head->state = I40E_FILTER_FAILED;
		} else {
			add_head->state = I40E_FILTER_ACTIVE;
			retval++;
		}

		add_head = i40e_next_filter(add_head);
		if (!add_head)
			break;
	}

	return retval;
}

/**
 * i40e_aqc_del_filters - Request firmware to delete a set of filters
 * @vsi: ptr to the VSI
 * @vsi_name: name to display in messages
 * @list: the list of filters to send to firmware
 * @num_del: the number of filters to delete
 * @retval: Set to -EIO on failure to delete
 *
 * Send a request to firmware via AdminQ to delete a set of filters. Uses
 * *retval instead of a return value so that success does not force ret_val to
 * be set to 0. This ensures that a sequence of calls to this function
 * preserve the previous value of *retval on successful delete.
 */
static
void i40e_aqc_del_filters(struct i40e_vsi *vsi, const char *vsi_name,
			  struct i40e_aqc_remove_macvlan_element_data *list,
			  int num_del, int *retval)
{
	struct i40e_hw *hw = &vsi->back->hw;
	i40e_status aq_ret;
	int aq_err;

	aq_ret = i40e_aq_remove_macvlan(hw, vsi->seid, list, num_del, NULL);
	aq_err = hw->aq.asq_last_status;

	/* Explicitly ignore and do not report when firmware returns ENOENT */
	if (aq_ret && !(aq_err == I40E_AQ_RC_ENOENT)) {
		*retval = -EIO;
		dev_info(&vsi->back->pdev->dev,
			 "ignoring delete macvlan error on %s, err %s, aq_err %s\n",
			 vsi_name, i40e_stat_str(hw, aq_ret),
			 i40e_aq_str(hw, aq_err));
	}
}

/**
 * i40e_aqc_add_filters - Request firmware to add a set of filters
 * @vsi: ptr to the VSI
 * @vsi_name: name to display in messages
 * @list: the list of filters to send to firmware
 * @add_head: Position in the add hlist
 * @num_add: the number of filters to add
 *
 * Send a request to firmware via AdminQ to add a chunk of filters. Will set
 * __I40E_VSI_OVERFLOW_PROMISC bit in vsi->state if the firmware has run out of
 * space for more filters.
 */
static
void i40e_aqc_add_filters(struct i40e_vsi *vsi, const char *vsi_name,
			  struct i40e_aqc_add_macvlan_element_data *list,
			  struct i40e_new_mac_filter *add_head,
			  int num_add)
{
	struct i40e_hw *hw = &vsi->back->hw;
	int aq_err, fcnt;

	i40e_aq_add_macvlan(hw, vsi->seid, list, num_add, NULL);
	aq_err = hw->aq.asq_last_status;
	fcnt = i40e_update_filter_state(num_add, list, add_head);

	if (fcnt != num_add) {
		if (vsi->type == I40E_VSI_MAIN) {
			set_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);
			dev_warn(&vsi->back->pdev->dev,
				 "Error %s adding RX filters on %s, promiscuous mode forced on\n",
				 i40e_aq_str(hw, aq_err), vsi_name);
		} else if (vsi->type == I40E_VSI_SRIOV ||
			   vsi->type == I40E_VSI_VMDQ1 ||
			   vsi->type == I40E_VSI_VMDQ2) {
			dev_warn(&vsi->back->pdev->dev,
				 "Error %s adding RX filters on %s, please set promiscuous on manually for %s\n",
				 i40e_aq_str(hw, aq_err), vsi_name, vsi_name);
		} else {
			dev_warn(&vsi->back->pdev->dev,
				 "Error %s adding RX filters on %s, incorrect VSI type: %i.\n",
				 i40e_aq_str(hw, aq_err), vsi_name, vsi->type);
		}
	}
}

/**
 * i40e_aqc_broadcast_filter - Set promiscuous broadcast flags
 * @vsi: pointer to the VSI
 * @vsi_name: the VSI name
 * @f: filter data
 *
 * This function sets or clears the promiscuous broadcast flags for VLAN
 * filters in order to properly receive broadcast frames. Assumes that only
 * broadcast filters are passed.
 *
 * Returns status indicating success or failure;
 **/
static i40e_status
i40e_aqc_broadcast_filter(struct i40e_vsi *vsi, const char *vsi_name,
			  struct i40e_mac_filter *f)
{
	bool enable = f->state == I40E_FILTER_NEW;
	struct i40e_hw *hw = &vsi->back->hw;
	i40e_status aq_ret;

	if (f->vlan == I40E_VLAN_ANY) {
		aq_ret = i40e_aq_set_vsi_broadcast(hw,
						   vsi->seid,
						   enable,
						   NULL);
	} else {
		aq_ret = i40e_aq_set_vsi_bc_promisc_on_vlan(hw,
							    vsi->seid,
							    enable,
							    f->vlan,
							    NULL);
	}

	if (aq_ret) {
		set_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);
		dev_warn(&vsi->back->pdev->dev,
			 "Error %s, forcing overflow promiscuous on %s\n",
			 i40e_aq_str(hw, hw->aq.asq_last_status),
			 vsi_name);
	}

	return aq_ret;
}

/**
 * i40e_set_promiscuous - set promiscuous mode
 * @pf: board private structure
 * @promisc: promisc on or off
 *
 * There are different ways of setting promiscuous mode on a PF depending on
 * what state/environment we're in.  This identifies and sets it appropriately.
 * Returns 0 on success.
 **/
static int i40e_set_promiscuous(struct i40e_pf *pf, bool promisc)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	struct i40e_hw *hw = &pf->hw;
	i40e_status aq_ret;

	if (vsi->type == I40E_VSI_MAIN &&
	    pf->lan_veb != I40E_NO_VEB &&
	    !(pf->flags & I40E_FLAG_MFP_ENABLED)) {
		/* set defport ON for Main VSI instead of true promisc
		 * this way we will get all unicast/multicast and VLAN
		 * promisc behavior but will not get VF or VMDq traffic
		 * replicated on the Main VSI.
		 */
		if (promisc)
			aq_ret = i40e_aq_set_default_vsi(hw,
							 vsi->seid,
							 NULL);
		else
			aq_ret = i40e_aq_clear_default_vsi(hw,
							   vsi->seid,
							   NULL);
		if (aq_ret) {
			dev_info(&pf->pdev->dev,
				 "Set default VSI failed, err %s, aq_err %s\n",
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		}
	} else {
		aq_ret = i40e_aq_set_vsi_unicast_promiscuous(
						  hw,
						  vsi->seid,
						  promisc, NULL,
						  true);
		if (aq_ret) {
			dev_info(&pf->pdev->dev,
				 "set unicast promisc failed, err %s, aq_err %s\n",
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		}
		aq_ret = i40e_aq_set_vsi_multicast_promiscuous(
						  hw,
						  vsi->seid,
						  promisc, NULL);
		if (aq_ret) {
			dev_info(&pf->pdev->dev,
				 "set multicast promisc failed, err %s, aq_err %s\n",
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		}
	}

	if (!aq_ret)
		pf->cur_promisc = promisc;

	return aq_ret;
}

/**
 * i40e_sync_vsi_filters - Update the VSI filter list to the HW
 * @vsi: ptr to the VSI
 *
 * Push any outstanding VSI filter changes through the AdminQ.
 *
 * Returns 0 or error value
 **/
int i40e_sync_vsi_filters(struct i40e_vsi *vsi)
{
	struct hlist_head tmp_add_list, tmp_del_list;
	struct i40e_mac_filter *f;
	struct i40e_new_mac_filter *new, *add_head = NULL;
	struct i40e_hw *hw = &vsi->back->hw;
	bool old_overflow, new_overflow;
	unsigned int failed_filters = 0;
	unsigned int vlan_filters = 0;
	char vsi_name[16] = "PF";
	int filter_list_len = 0;
	i40e_status aq_ret = 0;
	u32 changed_flags = 0;
	struct hlist_node *h;
	struct i40e_pf *pf;
	int num_add = 0;
	int num_del = 0;
	int retval = 0;
	u16 cmd_flags;
	int list_size;
	int bkt;

	/* empty array typed pointers, kcalloc later */
	struct i40e_aqc_add_macvlan_element_data *add_list;
	struct i40e_aqc_remove_macvlan_element_data *del_list;

	while (test_and_set_bit(__I40E_VSI_SYNCING_FILTERS, vsi->state))
		usleep_range(1000, 2000);
	pf = vsi->back;

	old_overflow = test_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);

	if (vsi->netdev) {
		changed_flags = vsi->current_netdev_flags ^ vsi->netdev->flags;
		vsi->current_netdev_flags = vsi->netdev->flags;
	}

	INIT_HLIST_HEAD(&tmp_add_list);
	INIT_HLIST_HEAD(&tmp_del_list);

	if (vsi->type == I40E_VSI_SRIOV)
		snprintf(vsi_name, sizeof(vsi_name) - 1, "VF %d", vsi->vf_id);
	else if (vsi->type != I40E_VSI_MAIN)
		snprintf(vsi_name, sizeof(vsi_name) - 1, "vsi %d", vsi->seid);

	if (vsi->flags & I40E_VSI_FLAG_FILTER_CHANGED) {
		vsi->flags &= ~I40E_VSI_FLAG_FILTER_CHANGED;

		spin_lock_bh(&vsi->mac_filter_hash_lock);
		/* Create a list of filters to delete. */
		hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
			if (f->state == I40E_FILTER_REMOVE) {
				/* Move the element into temporary del_list */
				hash_del(&f->hlist);
				hlist_add_head(&f->hlist, &tmp_del_list);

				/* Avoid counting removed filters */
				continue;
			}
			if (f->state == I40E_FILTER_NEW) {
				/* Create a temporary i40e_new_mac_filter */
				new = kzalloc(sizeof(*new), GFP_ATOMIC);
				if (!new)
					goto err_no_memory_locked;

				/* Store pointer to the real filter */
				new->f = f;
				new->state = f->state;

				/* Add it to the hash list */
				hlist_add_head(&new->hlist, &tmp_add_list);
			}

			/* Count the number of active (current and new) VLAN
			 * filters we have now. Does not count filters which
			 * are marked for deletion.
			 */
			if (f->vlan > 0)
				vlan_filters++;
		}

		retval = i40e_correct_mac_vlan_filters(vsi,
						       &tmp_add_list,
						       &tmp_del_list,
						       vlan_filters);

		hlist_for_each_entry(new, &tmp_add_list, hlist)
			netdev_hw_addr_refcnt(new->f, vsi->netdev, 1);

		if (retval)
			goto err_no_memory_locked;

		spin_unlock_bh(&vsi->mac_filter_hash_lock);
	}

	/* Now process 'del_list' outside the lock */
	if (!hlist_empty(&tmp_del_list)) {
		filter_list_len = hw->aq.asq_buf_size /
			    sizeof(struct i40e_aqc_remove_macvlan_element_data);
		list_size = filter_list_len *
			    sizeof(struct i40e_aqc_remove_macvlan_element_data);
		del_list = kzalloc(list_size, GFP_ATOMIC);
		if (!del_list)
			goto err_no_memory;

		hlist_for_each_entry_safe(f, h, &tmp_del_list, hlist) {
			cmd_flags = 0;

			/* handle broadcast filters by updating the broadcast
			 * promiscuous flag and release filter list.
			 */
			if (is_broadcast_ether_addr(f->macaddr)) {
				i40e_aqc_broadcast_filter(vsi, vsi_name, f);

				hlist_del(&f->hlist);
				kfree(f);
				continue;
			}

			/* add to delete list */
			ether_addr_copy(del_list[num_del].mac_addr, f->macaddr);
			if (f->vlan == I40E_VLAN_ANY) {
				del_list[num_del].vlan_tag = 0;
				cmd_flags |= I40E_AQC_MACVLAN_DEL_IGNORE_VLAN;
			} else {
				del_list[num_del].vlan_tag =
					cpu_to_le16((u16)(f->vlan));
			}

			cmd_flags |= I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
			del_list[num_del].flags = cmd_flags;
			num_del++;

			/* flush a full buffer */
			if (num_del == filter_list_len) {
				i40e_aqc_del_filters(vsi, vsi_name, del_list,
						     num_del, &retval);
				memset(del_list, 0, list_size);
				num_del = 0;
			}
			/* Release memory for MAC filter entries which were
			 * synced up with HW.
			 */
			hlist_del(&f->hlist);
			kfree(f);
		}

		if (num_del) {
			i40e_aqc_del_filters(vsi, vsi_name, del_list,
					     num_del, &retval);
		}

		kfree(del_list);
		del_list = NULL;
	}

	if (!hlist_empty(&tmp_add_list)) {
		/* Do all the adds now. */
		filter_list_len = hw->aq.asq_buf_size /
			       sizeof(struct i40e_aqc_add_macvlan_element_data);
		list_size = filter_list_len *
			       sizeof(struct i40e_aqc_add_macvlan_element_data);
		add_list = kzalloc(list_size, GFP_ATOMIC);
		if (!add_list)
			goto err_no_memory;

		num_add = 0;
		hlist_for_each_entry_safe(new, h, &tmp_add_list, hlist) {
			/* handle broadcast filters by updating the broadcast
			 * promiscuous flag instead of adding a MAC filter.
			 */
			if (is_broadcast_ether_addr(new->f->macaddr)) {
				if (i40e_aqc_broadcast_filter(vsi, vsi_name,
							      new->f))
					new->state = I40E_FILTER_FAILED;
				else
					new->state = I40E_FILTER_ACTIVE;
				continue;
			}

			/* add to add array */
			if (num_add == 0)
				add_head = new;
			cmd_flags = 0;
			ether_addr_copy(add_list[num_add].mac_addr,
					new->f->macaddr);
			if (new->f->vlan == I40E_VLAN_ANY) {
				add_list[num_add].vlan_tag = 0;
				cmd_flags |= I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
			} else {
				add_list[num_add].vlan_tag =
					cpu_to_le16((u16)(new->f->vlan));
			}
			add_list[num_add].queue_number = 0;
			/* set invalid match method for later detection */
			add_list[num_add].match_method = I40E_AQC_MM_ERR_NO_RES;
			cmd_flags |= I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
			add_list[num_add].flags = cpu_to_le16(cmd_flags);
			num_add++;

			/* flush a full buffer */
			if (num_add == filter_list_len) {
				i40e_aqc_add_filters(vsi, vsi_name, add_list,
						     add_head, num_add);
				memset(add_list, 0, list_size);
				num_add = 0;
			}
		}
		if (num_add) {
			i40e_aqc_add_filters(vsi, vsi_name, add_list, add_head,
					     num_add);
		}
		/* Now move all of the filters from the temp add list back to
		 * the VSI's list.
		 */
		spin_lock_bh(&vsi->mac_filter_hash_lock);
		hlist_for_each_entry_safe(new, h, &tmp_add_list, hlist) {
			/* Only update the state if we're still NEW */
			if (new->f->state == I40E_FILTER_NEW)
				new->f->state = new->state;
			hlist_del(&new->hlist);
			netdev_hw_addr_refcnt(new->f, vsi->netdev, -1);
			kfree(new);
		}
		spin_unlock_bh(&vsi->mac_filter_hash_lock);
		kfree(add_list);
		add_list = NULL;
	}

	/* Determine the number of active and failed filters. */
	spin_lock_bh(&vsi->mac_filter_hash_lock);
	vsi->active_filters = 0;
	hash_for_each(vsi->mac_filter_hash, bkt, f, hlist) {
		if (f->state == I40E_FILTER_ACTIVE)
			vsi->active_filters++;
		else if (f->state == I40E_FILTER_FAILED)
			failed_filters++;
	}
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	/* Check if we are able to exit overflow promiscuous mode. We can
	 * safely exit if we didn't just enter, we no longer have any failed
	 * filters, and we have reduced filters below the threshold value.
	 */
	if (old_overflow && !failed_filters &&
	    vsi->active_filters < vsi->promisc_threshold) {
		dev_info(&pf->pdev->dev,
			 "filter logjam cleared on %s, leaving overflow promiscuous mode\n",
			 vsi_name);
		clear_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);
		vsi->promisc_threshold = 0;
	}

	/* if the VF is not trusted do not do promisc */
	if ((vsi->type == I40E_VSI_SRIOV) && !pf->vf[vsi->vf_id].trusted) {
		clear_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);
		goto out;
	}

	new_overflow = test_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);

	/* If we are entering overflow promiscuous, we need to calculate a new
	 * threshold for when we are safe to exit
	 */
	if (!old_overflow && new_overflow)
		vsi->promisc_threshold = (vsi->active_filters * 3) / 4;

	/* check for changes in promiscuous modes */
	if (changed_flags & IFF_ALLMULTI) {
		bool cur_multipromisc;

		cur_multipromisc = !!(vsi->current_netdev_flags & IFF_ALLMULTI);
		aq_ret = i40e_aq_set_vsi_multicast_promiscuous(&vsi->back->hw,
							       vsi->seid,
							       cur_multipromisc,
							       NULL);
		if (aq_ret) {
			retval = i40e_aq_rc_to_posix(aq_ret,
						     hw->aq.asq_last_status);
			dev_info(&pf->pdev->dev,
				 "set multi promisc failed on %s, err %s aq_err %s\n",
				 vsi_name,
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		} else {
			dev_info(&pf->pdev->dev, "%s allmulti mode.\n",
				 cur_multipromisc ? "entering" : "leaving");
		}
	}

	if ((changed_flags & IFF_PROMISC) || old_overflow != new_overflow) {
		bool cur_promisc;

		cur_promisc = (!!(vsi->current_netdev_flags & IFF_PROMISC) ||
			       new_overflow);
		aq_ret = i40e_set_promiscuous(pf, cur_promisc);
		if (aq_ret) {
			retval = i40e_aq_rc_to_posix(aq_ret,
						     hw->aq.asq_last_status);
			dev_info(&pf->pdev->dev,
				 "Setting promiscuous %s failed on %s, err %s aq_err %s\n",
				 cur_promisc ? "on" : "off",
				 vsi_name,
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		}
	}
out:
	/* if something went wrong then set the changed flag so we try again */
	if (retval)
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;

	clear_bit(__I40E_VSI_SYNCING_FILTERS, vsi->state);
	return retval;

err_no_memory:
	/* Restore elements on the temporary add and delete lists */
	spin_lock_bh(&vsi->mac_filter_hash_lock);
err_no_memory_locked:
	i40e_undo_del_filter_entries(vsi, &tmp_del_list);
	i40e_undo_add_filter_entries(vsi, &tmp_add_list);
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
	clear_bit(__I40E_VSI_SYNCING_FILTERS, vsi->state);
	return -ENOMEM;
}

/**
 * i40e_sync_filters_subtask - Sync the VSI filter list with HW
 * @pf: board private structure
 **/
static void i40e_sync_filters_subtask(struct i40e_pf *pf)
{
	int v;

	if (!pf)
		return;
	if (!test_and_clear_bit(__I40E_MACVLAN_SYNC_PENDING, pf->state))
		return;
	if (test_bit(__I40E_VF_DISABLE, pf->state)) {
		set_bit(__I40E_MACVLAN_SYNC_PENDING, pf->state);
		return;
	}

	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v] &&
		    (pf->vsi[v]->flags & I40E_VSI_FLAG_FILTER_CHANGED) &&
		    !test_bit(__I40E_VSI_RELEASING, pf->vsi[v]->state)) {
			int ret = i40e_sync_vsi_filters(pf->vsi[v]);

			if (ret) {
				/* come back and try again later */
				set_bit(__I40E_MACVLAN_SYNC_PENDING,
					pf->state);
				break;
			}
		}
	}
}

/**
 * i40e_max_xdp_frame_size - returns the maximum allowed frame size for XDP
 * @vsi: the vsi
 **/
static int i40e_max_xdp_frame_size(struct i40e_vsi *vsi)
{
	if (PAGE_SIZE >= 8192 || (vsi->back->flags & I40E_FLAG_LEGACY_RX))
		return I40E_RXBUFFER_2048;
	else
		return I40E_RXBUFFER_3072;
}

/**
 * i40e_change_mtu - NDO callback to change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	if (i40e_enabled_xdp_vsi(vsi)) {
		int frame_size = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

		if (frame_size > i40e_max_xdp_frame_size(vsi))
			return -EINVAL;
	}

	netdev_dbg(netdev, "changing MTU from %d to %d\n",
		   netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	if (netif_running(netdev))
		i40e_vsi_reinit_locked(vsi);
	set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
	set_bit(__I40E_CLIENT_L2_CHANGE, pf->state);
	return 0;
}

/**
 * i40e_ioctl - Access the hwtstamp interface
 * @netdev: network interface device structure
 * @ifr: interface request data
 * @cmd: ioctl command
 **/
int i40e_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return i40e_ptp_get_ts_config(pf, ifr);
	case SIOCSHWTSTAMP:
		return i40e_ptp_set_ts_config(pf, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * i40e_vlan_stripping_enable - Turn on vlan stripping for the VSI
 * @vsi: the vsi being adjusted
 **/
void i40e_vlan_stripping_enable(struct i40e_vsi *vsi)
{
	struct i40e_vsi_context ctxt;
	i40e_status ret;

	/* Don't modify stripping options if a port VLAN is active */
	if (vsi->info.pvid)
		return;

	if ((vsi->info.valid_sections &
	     cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID)) &&
	    ((vsi->info.port_vlan_flags & I40E_AQ_VSI_PVLAN_MODE_MASK) == 0))
		return;  /* already enabled */

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
				    I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH;

	ctxt.seid = vsi->seid;
	ctxt.info = vsi->info;
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "update vlan stripping failed, err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
	}
}

/**
 * i40e_vlan_stripping_disable - Turn off vlan stripping for the VSI
 * @vsi: the vsi being adjusted
 **/
void i40e_vlan_stripping_disable(struct i40e_vsi *vsi)
{
	struct i40e_vsi_context ctxt;
	i40e_status ret;

	/* Don't modify stripping options if a port VLAN is active */
	if (vsi->info.pvid)
		return;

	if ((vsi->info.valid_sections &
	     cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID)) &&
	    ((vsi->info.port_vlan_flags & I40E_AQ_VSI_PVLAN_EMOD_MASK) ==
	     I40E_AQ_VSI_PVLAN_EMOD_MASK))
		return;  /* already disabled */

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
				    I40E_AQ_VSI_PVLAN_EMOD_NOTHING;

	ctxt.seid = vsi->seid;
	ctxt.info = vsi->info;
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "update vlan stripping failed, err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
	}
}

/**
 * i40e_add_vlan_all_mac - Add a MAC/VLAN filter for each existing MAC address
 * @vsi: the vsi being configured
 * @vid: vlan id to be added (0 = untagged only , -1 = any)
 *
 * This is a helper function for adding a new MAC/VLAN filter with the
 * specified VLAN for each existing MAC address already in the hash table.
 * This function does *not* perform any accounting to update filters based on
 * VLAN mode.
 *
 * NOTE: this function expects to be called while under the
 * mac_filter_hash_lock
 **/
int i40e_add_vlan_all_mac(struct i40e_vsi *vsi, s16 vid)
{
	struct i40e_mac_filter *f, *add_f;
	struct hlist_node *h;
	int bkt;

	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		if (f->state == I40E_FILTER_REMOVE)
			continue;
		add_f = i40e_add_filter(vsi, f->macaddr, vid);
		if (!add_f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add vlan filter %d for %pM\n",
				 vid, f->macaddr);
			return -ENOMEM;
		}
	}

	return 0;
}

/**
 * i40e_vsi_add_vlan - Add VSI membership for given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN id to be added
 **/
int i40e_vsi_add_vlan(struct i40e_vsi *vsi, u16 vid)
{
	int err;

	if (vsi->info.pvid)
		return -EINVAL;

	/* The network stack will attempt to add VID=0, with the intention to
	 * receive priority tagged packets with a VLAN of 0. Our HW receives
	 * these packets by default when configured to receive untagged
	 * packets, so we don't need to add a filter for this case.
	 * Additionally, HW interprets adding a VID=0 filter as meaning to
	 * receive *only* tagged traffic and stops receiving untagged traffic.
	 * Thus, we do not want to actually add a filter for VID=0
	 */
	if (!vid)
		return 0;

	/* Locked once because all functions invoked below iterates list*/
	spin_lock_bh(&vsi->mac_filter_hash_lock);
	err = i40e_add_vlan_all_mac(vsi, vid);
	spin_unlock_bh(&vsi->mac_filter_hash_lock);
	if (err)
		return err;

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
	return 0;
}

/**
 * i40e_rm_vlan_all_mac - Remove MAC/VLAN pair for all MAC with the given VLAN
 * @vsi: the vsi being configured
 * @vid: vlan id to be removed (0 = untagged only , -1 = any)
 *
 * This function should be used to remove all VLAN filters which match the
 * given VID. It does not schedule the service event and does not take the
 * mac_filter_hash_lock so it may be combined with other operations under
 * a single invocation of the mac_filter_hash_lock.
 *
 * NOTE: this function expects to be called while under the
 * mac_filter_hash_lock
 */
void i40e_rm_vlan_all_mac(struct i40e_vsi *vsi, s16 vid)
{
	struct i40e_mac_filter *f;
	struct hlist_node *h;
	int bkt;

	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		if (f->vlan == vid)
			__i40e_del_filter(vsi, f);
	}
}

/**
 * i40e_vsi_kill_vlan - Remove VSI membership for given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN id to be removed
 **/
void i40e_vsi_kill_vlan(struct i40e_vsi *vsi, u16 vid)
{
	if (!vid || vsi->info.pvid)
		return;

	spin_lock_bh(&vsi->mac_filter_hash_lock);
	i40e_rm_vlan_all_mac(vsi, vid);
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
}

/**
 * i40e_vlan_rx_add_vid - Add a vlan id filter to HW offload
 * @netdev: network interface to be adjusted
 * @proto: unused protocol value
 * @vid: vlan id to be added
 *
 * net_device_ops implementation for adding vlan ids
 **/
static int i40e_vlan_rx_add_vid(struct net_device *netdev,
				__always_unused __be16 proto, u16 vid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	int ret = 0;

	if (vid >= VLAN_N_VID)
		return -EINVAL;

	ret = i40e_vsi_add_vlan(vsi, vid);
	if (!ret)
		set_bit(vid, vsi->active_vlans);

	return ret;
}

/**
 * i40e_vlan_rx_add_vid_up - Add a vlan id filter to HW offload in UP path
 * @netdev: network interface to be adjusted
 * @proto: unused protocol value
 * @vid: vlan id to be added
 **/
static void i40e_vlan_rx_add_vid_up(struct net_device *netdev,
				    __always_unused __be16 proto, u16 vid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (vid >= VLAN_N_VID)
		return;
	set_bit(vid, vsi->active_vlans);
}

/**
 * i40e_vlan_rx_kill_vid - Remove a vlan id filter from HW offload
 * @netdev: network interface to be adjusted
 * @proto: unused protocol value
 * @vid: vlan id to be removed
 *
 * net_device_ops implementation for removing vlan ids
 **/
static int i40e_vlan_rx_kill_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	/* return code is ignored as there is nothing a user
	 * can do about failure to remove and a log message was
	 * already printed from the other function
	 */
	i40e_vsi_kill_vlan(vsi, vid);

	clear_bit(vid, vsi->active_vlans);

	return 0;
}

/**
 * i40e_restore_vlan - Reinstate vlans when vsi/netdev comes back up
 * @vsi: the vsi being brought back up
 **/
static void i40e_restore_vlan(struct i40e_vsi *vsi)
{
	u16 vid;

	if (!vsi->netdev)
		return;

	if (vsi->netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);

	for_each_set_bit(vid, vsi->active_vlans, VLAN_N_VID)
		i40e_vlan_rx_add_vid_up(vsi->netdev, htons(ETH_P_8021Q),
					vid);
}

/**
 * i40e_vsi_add_pvid - Add pvid for the VSI
 * @vsi: the vsi being adjusted
 * @vid: the vlan id to set as a PVID
 **/
int i40e_vsi_add_pvid(struct i40e_vsi *vsi, u16 vid)
{
	struct i40e_vsi_context ctxt;
	i40e_status ret;

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.pvid = cpu_to_le16(vid);
	vsi->info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_TAGGED |
				    I40E_AQ_VSI_PVLAN_INSERT_PVID |
				    I40E_AQ_VSI_PVLAN_EMOD_STR;

	ctxt.seid = vsi->seid;
	ctxt.info = vsi->info;
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "add pvid failed, err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
		return -ENOENT;
	}

	return 0;
}

/**
 * i40e_vsi_remove_pvid - Remove the pvid from the VSI
 * @vsi: the vsi being adjusted
 *
 * Just use the vlan_rx_register() service to put it back to normal
 **/
void i40e_vsi_remove_pvid(struct i40e_vsi *vsi)
{
	vsi->info.pvid = 0;

	i40e_vlan_stripping_disable(vsi);
}

/**
 * i40e_vsi_setup_tx_resources - Allocate VSI Tx queue resources
 * @vsi: ptr to the VSI
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40e_vsi_setup_tx_resources(struct i40e_vsi *vsi)
{
	int i, err = 0;

	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_setup_tx_descriptors(vsi->tx_rings[i]);

	if (!i40e_enabled_xdp_vsi(vsi))
		return err;

	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_setup_tx_descriptors(vsi->xdp_rings[i]);

	return err;
}

/**
 * i40e_vsi_free_tx_resources - Free Tx resources for VSI queues
 * @vsi: ptr to the VSI
 *
 * Free VSI's transmit software resources
 **/
static void i40e_vsi_free_tx_resources(struct i40e_vsi *vsi)
{
	int i;

	if (vsi->tx_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++)
			if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc)
				i40e_free_tx_resources(vsi->tx_rings[i]);
	}

	if (vsi->xdp_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++)
			if (vsi->xdp_rings[i] && vsi->xdp_rings[i]->desc)
				i40e_free_tx_resources(vsi->xdp_rings[i]);
	}
}

/**
 * i40e_vsi_setup_rx_resources - Allocate VSI queues Rx resources
 * @vsi: ptr to the VSI
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40e_vsi_setup_rx_resources(struct i40e_vsi *vsi)
{
	int i, err = 0;

	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_setup_rx_descriptors(vsi->rx_rings[i]);
	return err;
}

/**
 * i40e_vsi_free_rx_resources - Free Rx Resources for VSI queues
 * @vsi: ptr to the VSI
 *
 * Free all receive software resources
 **/
static void i40e_vsi_free_rx_resources(struct i40e_vsi *vsi)
{
	int i;

	if (!vsi->rx_rings)
		return;

	for (i = 0; i < vsi->num_queue_pairs; i++)
		if (vsi->rx_rings[i] && vsi->rx_rings[i]->desc)
			i40e_free_rx_resources(vsi->rx_rings[i]);
}

/**
 * i40e_config_xps_tx_ring - Configure XPS for a Tx ring
 * @ring: The Tx ring to configure
 *
 * This enables/disables XPS for a given Tx descriptor ring
 * based on the TCs enabled for the VSI that ring belongs to.
 **/
static void i40e_config_xps_tx_ring(struct i40e_ring *ring)
{
	int cpu;

	if (!ring->q_vector || !ring->netdev || ring->ch)
		return;

	/* We only initialize XPS once, so as not to overwrite user settings */
	if (test_and_set_bit(__I40E_TX_XPS_INIT_DONE, ring->state))
		return;

	cpu = cpumask_local_spread(ring->q_vector->v_idx, -1);
	netif_set_xps_queue(ring->netdev, get_cpu_mask(cpu),
			    ring->queue_index);
}

/**
 * i40e_xsk_pool - Retrieve the AF_XDP buffer pool if XDP and ZC is enabled
 * @ring: The Tx or Rx ring
 *
 * Returns the AF_XDP buffer pool or NULL.
 **/
static struct xsk_buff_pool *i40e_xsk_pool(struct i40e_ring *ring)
{
	bool xdp_on = i40e_enabled_xdp_vsi(ring->vsi);
	int qid = ring->queue_index;

	if (ring_is_xdp(ring))
		qid -= ring->vsi->alloc_queue_pairs;

	if (!xdp_on || !test_bit(qid, ring->vsi->af_xdp_zc_qps))
		return NULL;

	return xsk_get_pool_from_qid(ring->vsi->netdev, qid);
}

/**
 * i40e_configure_tx_ring - Configure a transmit ring context and rest
 * @ring: The Tx ring to configure
 *
 * Configure the Tx descriptor ring in the HMC context.
 **/
static int i40e_configure_tx_ring(struct i40e_ring *ring)
{
	struct i40e_vsi *vsi = ring->vsi;
	u16 pf_q = vsi->base_queue + ring->queue_index;
	struct i40e_hw *hw = &vsi->back->hw;
	struct i40e_hmc_obj_txq tx_ctx;
	i40e_status err = 0;
	u32 qtx_ctl = 0;

	if (ring_is_xdp(ring))
		ring->xsk_pool = i40e_xsk_pool(ring);

	/* some ATR related tx ring init */
	if (vsi->back->flags & I40E_FLAG_FD_ATR_ENABLED) {
		ring->atr_sample_rate = vsi->back->atr_sample_rate;
		ring->atr_count = 0;
	} else {
		ring->atr_sample_rate = 0;
	}

	/* configure XPS */
	i40e_config_xps_tx_ring(ring);

	/* clear the context structure first */
	memset(&tx_ctx, 0, sizeof(tx_ctx));

	tx_ctx.new_context = 1;
	tx_ctx.base = (ring->dma / 128);
	tx_ctx.qlen = ring->count;
	tx_ctx.fd_ena = !!(vsi->back->flags & (I40E_FLAG_FD_SB_ENABLED |
					       I40E_FLAG_FD_ATR_ENABLED));
	tx_ctx.timesync_ena = !!(vsi->back->flags & I40E_FLAG_PTP);
	/* FDIR VSI tx ring can still use RS bit and writebacks */
	if (vsi->type != I40E_VSI_FDIR)
		tx_ctx.head_wb_ena = 1;
	tx_ctx.head_wb_addr = ring->dma +
			      (ring->count * sizeof(struct i40e_tx_desc));

	/* As part of VSI creation/update, FW allocates certain
	 * Tx arbitration queue sets for each TC enabled for
	 * the VSI. The FW returns the handles to these queue
	 * sets as part of the response buffer to Add VSI,
	 * Update VSI, etc. AQ commands. It is expected that
	 * these queue set handles be associated with the Tx
	 * queues by the driver as part of the TX queue context
	 * initialization. This has to be done regardless of
	 * DCB as by default everything is mapped to TC0.
	 */

	if (ring->ch)
		tx_ctx.rdylist =
			le16_to_cpu(ring->ch->info.qs_handle[ring->dcb_tc]);

	else
		tx_ctx.rdylist = le16_to_cpu(vsi->info.qs_handle[ring->dcb_tc]);

	tx_ctx.rdylist_act = 0;

	/* clear the context in the HMC */
	err = i40e_clear_lan_tx_queue_context(hw, pf_q);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to clear LAN Tx queue context on Tx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* set the context in the HMC */
	err = i40e_set_lan_tx_queue_context(hw, pf_q, &tx_ctx);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to set LAN Tx queue context on Tx ring %d (pf_q %d, error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* Now associate this queue with this PCI function */
	if (ring->ch) {
		if (ring->ch->type == I40E_VSI_VMDQ2)
			qtx_ctl = I40E_QTX_CTL_VM_QUEUE;
		else
			return -EINVAL;

		qtx_ctl |= (ring->ch->vsi_number <<
			    I40E_QTX_CTL_VFVM_INDX_SHIFT) &
			    I40E_QTX_CTL_VFVM_INDX_MASK;
	} else {
		if (vsi->type == I40E_VSI_VMDQ2) {
			qtx_ctl = I40E_QTX_CTL_VM_QUEUE;
			qtx_ctl |= ((vsi->id) << I40E_QTX_CTL_VFVM_INDX_SHIFT) &
				    I40E_QTX_CTL_VFVM_INDX_MASK;
		} else {
			qtx_ctl = I40E_QTX_CTL_PF_QUEUE;
		}
	}

	qtx_ctl |= ((hw->pf_id << I40E_QTX_CTL_PF_INDX_SHIFT) &
		    I40E_QTX_CTL_PF_INDX_MASK);
	wr32(hw, I40E_QTX_CTL(pf_q), qtx_ctl);
	i40e_flush(hw);

	/* cache tail off for easier writes later */
	ring->tail = hw->hw_addr + I40E_QTX_TAIL(pf_q);

	return 0;
}

/**
 * i40e_configure_rx_ring - Configure a receive ring context
 * @ring: The Rx ring to configure
 *
 * Configure the Rx descriptor ring in the HMC context.
 **/
static int i40e_configure_rx_ring(struct i40e_ring *ring)
{
	struct i40e_vsi *vsi = ring->vsi;
	u32 chain_len = vsi->back->hw.func_caps.rx_buf_chain_len;
	u16 pf_q = vsi->base_queue + ring->queue_index;
	struct i40e_hw *hw = &vsi->back->hw;
	struct i40e_hmc_obj_rxq rx_ctx;
	i40e_status err = 0;
	bool ok;
	int ret;

	bitmap_zero(ring->state, __I40E_RING_STATE_NBITS);

	/* clear the context structure first */
	memset(&rx_ctx, 0, sizeof(rx_ctx));

	if (ring->vsi->type == I40E_VSI_MAIN)
		xdp_rxq_info_unreg_mem_model(&ring->xdp_rxq);

	kfree(ring->rx_bi);
	ring->xsk_pool = i40e_xsk_pool(ring);
	if (ring->xsk_pool) {
		ret = i40e_alloc_rx_bi_zc(ring);
		if (ret)
			return ret;
		ring->rx_buf_len =
		  xsk_pool_get_rx_frame_size(ring->xsk_pool);
		/* For AF_XDP ZC, we disallow packets to span on
		 * multiple buffers, thus letting us skip that
		 * handling in the fast-path.
		 */
		chain_len = 1;
		ret = xdp_rxq_info_reg_mem_model(&ring->xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL,
						 NULL);
		if (ret)
			return ret;
		dev_info(&vsi->back->pdev->dev,
			 "Registered XDP mem model MEM_TYPE_XSK_BUFF_POOL on Rx ring %d\n",
			 ring->queue_index);

	} else {
		ret = i40e_alloc_rx_bi(ring);
		if (ret)
			return ret;
		ring->rx_buf_len = vsi->rx_buf_len;
		if (ring->vsi->type == I40E_VSI_MAIN) {
			ret = xdp_rxq_info_reg_mem_model(&ring->xdp_rxq,
							 MEM_TYPE_PAGE_SHARED,
							 NULL);
			if (ret)
				return ret;
		}
	}

	rx_ctx.dbuff = DIV_ROUND_UP(ring->rx_buf_len,
				    BIT_ULL(I40E_RXQ_CTX_DBUFF_SHIFT));

	rx_ctx.base = (ring->dma / 128);
	rx_ctx.qlen = ring->count;

	/* use 16 byte descriptors */
	rx_ctx.dsize = 0;

	/* descriptor type is always zero
	 * rx_ctx.dtype = 0;
	 */
	rx_ctx.hsplit_0 = 0;

	rx_ctx.rxmax = min_t(u16, vsi->max_frame, chain_len * ring->rx_buf_len);
	if (hw->revision_id == 0)
		rx_ctx.lrxqthresh = 0;
	else
		rx_ctx.lrxqthresh = 1;
	rx_ctx.crcstrip = 1;
	rx_ctx.l2tsel = 1;
	/* this controls whether VLAN is stripped from inner headers */
	rx_ctx.showiv = 0;
	/* set the prefena field to 1 because the manual says to */
	rx_ctx.prefena = 1;

	/* clear the context in the HMC */
	err = i40e_clear_lan_rx_queue_context(hw, pf_q);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to clear LAN Rx queue context on Rx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* set the context in the HMC */
	err = i40e_set_lan_rx_queue_context(hw, pf_q, &rx_ctx);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to set LAN Rx queue context on Rx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* configure Rx buffer alignment */
	if (!vsi->netdev || (vsi->back->flags & I40E_FLAG_LEGACY_RX))
		clear_ring_build_skb_enabled(ring);
	else
		set_ring_build_skb_enabled(ring);

	/* cache tail for quicker writes, and clear the reg before use */
	ring->tail = hw->hw_addr + I40E_QRX_TAIL(pf_q);
	writel(0, ring->tail);

	if (ring->xsk_pool) {
		xsk_pool_set_rxq_info(ring->xsk_pool, &ring->xdp_rxq);
		ok = i40e_alloc_rx_buffers_zc(ring, I40E_DESC_UNUSED(ring));
	} else {
		ok = !i40e_alloc_rx_buffers(ring, I40E_DESC_UNUSED(ring));
	}
	if (!ok) {
		/* Log this in case the user has forgotten to give the kernel
		 * any buffers, even later in the application.
		 */
		dev_info(&vsi->back->pdev->dev,
			 "Failed to allocate some buffers on %sRx ring %d (pf_q %d)\n",
			 ring->xsk_pool ? "AF_XDP ZC enabled " : "",
			 ring->queue_index, pf_q);
	}

	return 0;
}

/**
 * i40e_vsi_configure_tx - Configure the VSI for Tx
 * @vsi: VSI structure describing this set of rings and resources
 *
 * Configure the Tx VSI for operation.
 **/
static int i40e_vsi_configure_tx(struct i40e_vsi *vsi)
{
	int err = 0;
	u16 i;

	for (i = 0; (i < vsi->num_queue_pairs) && !err; i++)
		err = i40e_configure_tx_ring(vsi->tx_rings[i]);

	if (err || !i40e_enabled_xdp_vsi(vsi))
		return err;

	for (i = 0; (i < vsi->num_queue_pairs) && !err; i++)
		err = i40e_configure_tx_ring(vsi->xdp_rings[i]);

	return err;
}

/**
 * i40e_vsi_configure_rx - Configure the VSI for Rx
 * @vsi: the VSI being configured
 *
 * Configure the Rx VSI for operation.
 **/
static int i40e_vsi_configure_rx(struct i40e_vsi *vsi)
{
	int err = 0;
	u16 i;

	if (!vsi->netdev || (vsi->back->flags & I40E_FLAG_LEGACY_RX)) {
		vsi->max_frame = I40E_MAX_RXBUFFER;
		vsi->rx_buf_len = I40E_RXBUFFER_2048;
#if (PAGE_SIZE < 8192)
	} else if (!I40E_2K_TOO_SMALL_WITH_PADDING &&
		   (vsi->netdev->mtu <= ETH_DATA_LEN)) {
		vsi->max_frame = I40E_RXBUFFER_1536 - NET_IP_ALIGN;
		vsi->rx_buf_len = I40E_RXBUFFER_1536 - NET_IP_ALIGN;
#endif
	} else {
		vsi->max_frame = I40E_MAX_RXBUFFER;
		vsi->rx_buf_len = (PAGE_SIZE < 8192) ? I40E_RXBUFFER_3072 :
						       I40E_RXBUFFER_2048;
	}

	/* set up individual rings */
	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_configure_rx_ring(vsi->rx_rings[i]);

	return err;
}

/**
 * i40e_vsi_config_dcb_rings - Update rings to reflect DCB TC
 * @vsi: ptr to the VSI
 **/
static void i40e_vsi_config_dcb_rings(struct i40e_vsi *vsi)
{
	struct i40e_ring *tx_ring, *rx_ring;
	u16 qoffset, qcount;
	int i, n;

	if (!(vsi->back->flags & I40E_FLAG_DCB_ENABLED)) {
		/* Reset the TC information */
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			rx_ring = vsi->rx_rings[i];
			tx_ring = vsi->tx_rings[i];
			rx_ring->dcb_tc = 0;
			tx_ring->dcb_tc = 0;
		}
		return;
	}

	for (n = 0; n < I40E_MAX_TRAFFIC_CLASS; n++) {
		if (!(vsi->tc_config.enabled_tc & BIT_ULL(n)))
			continue;

		qoffset = vsi->tc_config.tc_info[n].qoffset;
		qcount = vsi->tc_config.tc_info[n].qcount;
		for (i = qoffset; i < (qoffset + qcount); i++) {
			rx_ring = vsi->rx_rings[i];
			tx_ring = vsi->tx_rings[i];
			rx_ring->dcb_tc = n;
			tx_ring->dcb_tc = n;
		}
	}
}

/**
 * i40e_set_vsi_rx_mode - Call set_rx_mode on a VSI
 * @vsi: ptr to the VSI
 **/
static void i40e_set_vsi_rx_mode(struct i40e_vsi *vsi)
{
	if (vsi->netdev)
		i40e_set_rx_mode(vsi->netdev);
}

/**
 * i40e_fdir_filter_restore - Restore the Sideband Flow Director filters
 * @vsi: Pointer to the targeted VSI
 *
 * This function replays the hlist on the hw where all the SB Flow Director
 * filters were saved.
 **/
static void i40e_fdir_filter_restore(struct i40e_vsi *vsi)
{
	struct i40e_fdir_filter *filter;
	struct i40e_pf *pf = vsi->back;
	struct hlist_node *node;

	if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
		return;

	/* Reset FDir counters as we're replaying all existing filters */
	pf->fd_tcp4_filter_cnt = 0;
	pf->fd_udp4_filter_cnt = 0;
	pf->fd_sctp4_filter_cnt = 0;
	pf->fd_ip4_filter_cnt = 0;

	hlist_for_each_entry_safe(filter, node,
				  &pf->fdir_filter_list, fdir_node) {
		i40e_add_del_fdir(vsi, filter, true);
	}
}

/**
 * i40e_vsi_configure - Set up the VSI for action
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_configure(struct i40e_vsi *vsi)
{
	int err;

	i40e_set_vsi_rx_mode(vsi);
	i40e_restore_vlan(vsi);
	i40e_vsi_config_dcb_rings(vsi);
	err = i40e_vsi_configure_tx(vsi);
	if (!err)
		err = i40e_vsi_configure_rx(vsi);

	return err;
}

/**
 * i40e_vsi_configure_msix - MSIX mode Interrupt Config in the HW
 * @vsi: the VSI being configured
 **/
static void i40e_vsi_configure_msix(struct i40e_vsi *vsi)
{
	bool has_xdp = i40e_enabled_xdp_vsi(vsi);
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 vector;
	int i, q;
	u32 qp;

	/* The interrupt indexing is offset by 1 in the PFINT_ITRn
	 * and PFINT_LNKLSTn registers, e.g.:
	 *   PFINT_ITRn[0..n-1] gets msix-1..msix-n  (qpair interrupts)
	 */
	qp = vsi->base_queue;
	vector = vsi->base_vector;
	for (i = 0; i < vsi->num_q_vectors; i++, vector++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[i];

		q_vector->rx.next_update = jiffies + 1;
		q_vector->rx.target_itr =
			ITR_TO_REG(vsi->rx_rings[i]->itr_setting);
		wr32(hw, I40E_PFINT_ITRN(I40E_RX_ITR, vector - 1),
		     q_vector->rx.target_itr >> 1);
		q_vector->rx.current_itr = q_vector->rx.target_itr;

		q_vector->tx.next_update = jiffies + 1;
		q_vector->tx.target_itr =
			ITR_TO_REG(vsi->tx_rings[i]->itr_setting);
		wr32(hw, I40E_PFINT_ITRN(I40E_TX_ITR, vector - 1),
		     q_vector->tx.target_itr >> 1);
		q_vector->tx.current_itr = q_vector->tx.target_itr;

		wr32(hw, I40E_PFINT_RATEN(vector - 1),
		     i40e_intrl_usec_to_reg(vsi->int_rate_limit));

		/* Linked list for the queuepairs assigned to this vector */
		wr32(hw, I40E_PFINT_LNKLSTN(vector - 1), qp);
		for (q = 0; q < q_vector->num_ringpairs; q++) {
			u32 nextqp = has_xdp ? qp + vsi->alloc_queue_pairs : qp;
			u32 val;

			val = I40E_QINT_RQCTL_CAUSE_ENA_MASK |
			      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
			      (vector << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
			      (nextqp << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
			      (I40E_QUEUE_TYPE_TX <<
			       I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT);

			wr32(hw, I40E_QINT_RQCTL(qp), val);

			if (has_xdp) {
				val = I40E_QINT_TQCTL_CAUSE_ENA_MASK |
				      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
				      (vector << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
				      (qp << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT) |
				      (I40E_QUEUE_TYPE_TX <<
				       I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

				wr32(hw, I40E_QINT_TQCTL(nextqp), val);
			}

			val = I40E_QINT_TQCTL_CAUSE_ENA_MASK |
			      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
			      (vector << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
			      ((qp + 1) << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT) |
			      (I40E_QUEUE_TYPE_RX <<
			       I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

			/* Terminate the linked list */
			if (q == (q_vector->num_ringpairs - 1))
				val |= (I40E_QUEUE_END_OF_LIST <<
					I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);

			wr32(hw, I40E_QINT_TQCTL(qp), val);
			qp++;
		}
	}

	i40e_flush(hw);
}

/**
 * i40e_enable_misc_int_causes - enable the non-queue interrupts
 * @pf: pointer to private device data structure
 **/
static void i40e_enable_misc_int_causes(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	/* clear things first */
	wr32(hw, I40E_PFINT_ICR0_ENA, 0);  /* disable all */
	rd32(hw, I40E_PFINT_ICR0);         /* read to clear */

	val = I40E_PFINT_ICR0_ENA_ECC_ERR_MASK       |
	      I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK    |
	      I40E_PFINT_ICR0_ENA_GRST_MASK          |
	      I40E_PFINT_ICR0_ENA_PCI_EXCEPTION_MASK |
	      I40E_PFINT_ICR0_ENA_GPIO_MASK          |
	      I40E_PFINT_ICR0_ENA_HMC_ERR_MASK       |
	      I40E_PFINT_ICR0_ENA_VFLR_MASK          |
	      I40E_PFINT_ICR0_ENA_ADMINQ_MASK;

	if (pf->flags & I40E_FLAG_IWARP_ENABLED)
		val |= I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK;

	if (pf->flags & I40E_FLAG_PTP)
		val |= I40E_PFINT_ICR0_ENA_TIMESYNC_MASK;

	wr32(hw, I40E_PFINT_ICR0_ENA, val);

	/* SW_ITR_IDX = 0, but don't change INTENA */
	wr32(hw, I40E_PFINT_DYN_CTL0, I40E_PFINT_DYN_CTL0_SW_ITR_INDX_MASK |
					I40E_PFINT_DYN_CTL0_INTENA_MSK_MASK);

	/* OTHER_ITR_IDX = 0 */
	wr32(hw, I40E_PFINT_STAT_CTL0, 0);
}

/**
 * i40e_configure_msi_and_legacy - Legacy mode interrupt config in the HW
 * @vsi: the VSI being configured
 **/
static void i40e_configure_msi_and_legacy(struct i40e_vsi *vsi)
{
	u32 nextqp = i40e_enabled_xdp_vsi(vsi) ? vsi->alloc_queue_pairs : 0;
	struct i40e_q_vector *q_vector = vsi->q_vectors[0];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	/* set the ITR configuration */
	q_vector->rx.next_update = jiffies + 1;
	q_vector->rx.target_itr = ITR_TO_REG(vsi->rx_rings[0]->itr_setting);
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), q_vector->rx.target_itr >> 1);
	q_vector->rx.current_itr = q_vector->rx.target_itr;
	q_vector->tx.next_update = jiffies + 1;
	q_vector->tx.target_itr = ITR_TO_REG(vsi->tx_rings[0]->itr_setting);
	wr32(hw, I40E_PFINT_ITR0(I40E_TX_ITR), q_vector->tx.target_itr >> 1);
	q_vector->tx.current_itr = q_vector->tx.target_itr;

	i40e_enable_misc_int_causes(pf);

	/* FIRSTQ_INDX = 0, FIRSTQ_TYPE = 0 (rx) */
	wr32(hw, I40E_PFINT_LNKLST0, 0);

	/* Associate the queue pair to the vector and enable the queue int */
	val = I40E_QINT_RQCTL_CAUSE_ENA_MASK		       |
	      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT)  |
	      (nextqp	   << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT)|
	      (I40E_QUEUE_TYPE_TX << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

	wr32(hw, I40E_QINT_RQCTL(0), val);

	if (i40e_enabled_xdp_vsi(vsi)) {
		val = I40E_QINT_TQCTL_CAUSE_ENA_MASK		     |
		      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT)|
		      (I40E_QUEUE_TYPE_TX
		       << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

		wr32(hw, I40E_QINT_TQCTL(nextqp), val);
	}

	val = I40E_QINT_TQCTL_CAUSE_ENA_MASK		      |
	      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
	      (I40E_QUEUE_END_OF_LIST << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);

	wr32(hw, I40E_QINT_TQCTL(0), val);
	i40e_flush(hw);
}

/**
 * i40e_irq_dynamic_disable_icr0 - Disable default interrupt generation for icr0
 * @pf: board private structure
 **/
void i40e_irq_dynamic_disable_icr0(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;

	wr32(hw, I40E_PFINT_DYN_CTL0,
	     I40E_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT);
	i40e_flush(hw);
}

/**
 * i40e_irq_dynamic_enable_icr0 - Enable default interrupt generation for icr0
 * @pf: board private structure
 **/
void i40e_irq_dynamic_enable_icr0(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	val = I40E_PFINT_DYN_CTL0_INTENA_MASK   |
	      I40E_PFINT_DYN_CTL0_CLEARPBA_MASK |
	      (I40E_ITR_NONE << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT);

	wr32(hw, I40E_PFINT_DYN_CTL0, val);
	i40e_flush(hw);
}

/**
 * i40e_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 **/
static irqreturn_t i40e_msix_clean_rings(int irq, void *data)
{
	struct i40e_q_vector *q_vector = data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

	napi_schedule_irqoff(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * i40e_irq_affinity_notify - Callback for affinity changes
 * @notify: context as to what irq was changed
 * @mask: the new affinity mask
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * so that we may register to receive changes to the irq affinity masks.
 **/
static void i40e_irq_affinity_notify(struct irq_affinity_notify *notify,
				     const cpumask_t *mask)
{
	struct i40e_q_vector *q_vector =
		container_of(notify, struct i40e_q_vector, affinity_notify);

	cpumask_copy(&q_vector->affinity_mask, mask);
}

/**
 * i40e_irq_affinity_release - Callback for affinity notifier release
 * @ref: internal core kernel usage
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * to inform the current notification subscriber that they will no longer
 * receive notifications.
 **/
static void i40e_irq_affinity_release(struct kref *ref) {}

/**
 * i40e_vsi_request_irq_msix - Initialize MSI-X interrupts
 * @vsi: the VSI being configured
 * @basename: name for the vector
 *
 * Allocates MSI-X vectors and requests interrupts from the kernel.
 **/
static int i40e_vsi_request_irq_msix(struct i40e_vsi *vsi, char *basename)
{
	int q_vectors = vsi->num_q_vectors;
	struct i40e_pf *pf = vsi->back;
	int base = vsi->base_vector;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	int vector, err;
	int irq_num;
	int cpu;

	for (vector = 0; vector < q_vectors; vector++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[vector];

		irq_num = pf->msix_entries[base + vector].vector;

		if (q_vector->tx.ring && q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "TxRx", rx_int_idx++);
			tx_int_idx++;
		} else if (q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "rx", rx_int_idx++);
		} else if (q_vector->tx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "tx", tx_int_idx++);
		} else {
			/* skip this unused q_vector */
			continue;
		}
		err = request_irq(irq_num,
				  vsi->irq_handler,
				  0,
				  q_vector->name,
				  q_vector);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "MSIX request_irq failed, error: %d\n", err);
			goto free_queue_irqs;
		}

		/* register for affinity change notifications */
		q_vector->affinity_notify.notify = i40e_irq_affinity_notify;
		q_vector->affinity_notify.release = i40e_irq_affinity_release;
		irq_set_affinity_notifier(irq_num, &q_vector->affinity_notify);
		/* Spread affinity hints out across online CPUs.
		 *
		 * get_cpu_mask returns a static constant mask with
		 * a permanent lifetime so it's ok to pass to
		 * irq_set_affinity_hint without making a copy.
		 */
		cpu = cpumask_local_spread(q_vector->v_idx, -1);
		irq_set_affinity_hint(irq_num, get_cpu_mask(cpu));
	}

	vsi->irqs_ready = true;
	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		irq_num = pf->msix_entries[base + vector].vector;
		irq_set_affinity_notifier(irq_num, NULL);
		irq_set_affinity_hint(irq_num, NULL);
		free_irq(irq_num, &vsi->q_vectors[vector]);
	}
	return err;
}

/**
 * i40e_vsi_disable_irq - Mask off queue interrupt generation on the VSI
 * @vsi: the VSI being un-configured
 **/
static void i40e_vsi_disable_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int base = vsi->base_vector;
	int i;

	/* disable interrupt causation from each queue */
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		u32 val;

		val = rd32(hw, I40E_QINT_TQCTL(vsi->tx_rings[i]->reg_idx));
		val &= ~I40E_QINT_TQCTL_CAUSE_ENA_MASK;
		wr32(hw, I40E_QINT_TQCTL(vsi->tx_rings[i]->reg_idx), val);

		val = rd32(hw, I40E_QINT_RQCTL(vsi->rx_rings[i]->reg_idx));
		val &= ~I40E_QINT_RQCTL_CAUSE_ENA_MASK;
		wr32(hw, I40E_QINT_RQCTL(vsi->rx_rings[i]->reg_idx), val);

		if (!i40e_enabled_xdp_vsi(vsi))
			continue;
		wr32(hw, I40E_QINT_TQCTL(vsi->xdp_rings[i]->reg_idx), 0);
	}

	/* disable each interrupt */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = vsi->base_vector;
		     i < (vsi->num_q_vectors + vsi->base_vector); i++)
			wr32(hw, I40E_PFINT_DYN_CTLN(i - 1), 0);

		i40e_flush(hw);
		for (i = 0; i < vsi->num_q_vectors; i++)
			synchronize_irq(pf->msix_entries[i + base].vector);
	} else {
		/* Legacy and MSI mode - this stops all interrupt handling */
		wr32(hw, I40E_PFINT_ICR0_ENA, 0);
		wr32(hw, I40E_PFINT_DYN_CTL0, 0);
		i40e_flush(hw);
		synchronize_irq(pf->pdev->irq);
	}
}

/**
 * i40e_vsi_enable_irq - Enable IRQ for the given VSI
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_enable_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = 0; i < vsi->num_q_vectors; i++)
			i40e_irq_dynamic_enable(vsi, i);
	} else {
		i40e_irq_dynamic_enable_icr0(pf);
	}

	i40e_flush(&pf->hw);
	return 0;
}

/**
 * i40e_free_misc_vector - Free the vector that handles non-queue events
 * @pf: board private structure
 **/
static void i40e_free_misc_vector(struct i40e_pf *pf)
{
	/* Disable ICR 0 */
	wr32(&pf->hw, I40E_PFINT_ICR0_ENA, 0);
	i40e_flush(&pf->hw);

	if (pf->flags & I40E_FLAG_MSIX_ENABLED && pf->msix_entries) {
		synchronize_irq(pf->msix_entries[0].vector);
		free_irq(pf->msix_entries[0].vector, pf);
		clear_bit(__I40E_MISC_IRQ_REQUESTED, pf->state);
	}
}

/**
 * i40e_intr - MSI/Legacy and non-queue interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 *
 * This is the handler used for all MSI/Legacy interrupts, and deals
 * with both queue and non-queue interrupts.  This is also used in
 * MSIX mode to handle the non-queue interrupts.
 **/
static irqreturn_t i40e_intr(int irq, void *data)
{
	struct i40e_pf *pf = (struct i40e_pf *)data;
	struct i40e_hw *hw = &pf->hw;
	irqreturn_t ret = IRQ_NONE;
	u32 icr0, icr0_remaining;
	u32 val, ena_mask;

	icr0 = rd32(hw, I40E_PFINT_ICR0);
	ena_mask = rd32(hw, I40E_PFINT_ICR0_ENA);

	/* if sharing a legacy IRQ, we might get called w/o an intr pending */
	if ((icr0 & I40E_PFINT_ICR0_INTEVENT_MASK) == 0)
		goto enable_intr;

	/* if interrupt but no bits showing, must be SWINT */
	if (((icr0 & ~I40E_PFINT_ICR0_INTEVENT_MASK) == 0) ||
	    (icr0 & I40E_PFINT_ICR0_SWINT_MASK))
		pf->sw_int_count++;

	if ((pf->flags & I40E_FLAG_IWARP_ENABLED) &&
	    (icr0 & I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK)) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK;
		dev_dbg(&pf->pdev->dev, "cleared PE_CRITERR\n");
		set_bit(__I40E_CORE_RESET_REQUESTED, pf->state);
	}

	/* only q0 is used in MSI/Legacy mode, and none are used in MSIX */
	if (icr0 & I40E_PFINT_ICR0_QUEUE_0_MASK) {
		struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
		struct i40e_q_vector *q_vector = vsi->q_vectors[0];

		/* We do not have a way to disarm Queue causes while leaving
		 * interrupt enabled for all other causes, ideally
		 * interrupt should be disabled while we are in NAPI but
		 * this is not a performance path and napi_schedule()
		 * can deal with rescheduling.
		 */
		if (!test_bit(__I40E_DOWN, pf->state))
			napi_schedule_irqoff(&q_vector->napi);
	}

	if (icr0 & I40E_PFINT_ICR0_ADMINQ_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
		set_bit(__I40E_ADMINQ_EVENT_PENDING, pf->state);
		i40e_debug(&pf->hw, I40E_DEBUG_NVM, "AdminQ event\n");
	}

	if (icr0 & I40E_PFINT_ICR0_MAL_DETECT_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
		set_bit(__I40E_MDD_EVENT_PENDING, pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_VFLR_MASK) {
		/* disable any further VFLR event notifications */
		if (test_bit(__I40E_VF_RESETS_DISABLED, pf->state)) {
			u32 reg = rd32(hw, I40E_PFINT_ICR0_ENA);

			reg &= ~I40E_PFINT_ICR0_VFLR_MASK;
			wr32(hw, I40E_PFINT_ICR0_ENA, reg);
		} else {
			ena_mask &= ~I40E_PFINT_ICR0_ENA_VFLR_MASK;
			set_bit(__I40E_VFLR_EVENT_PENDING, pf->state);
		}
	}

	if (icr0 & I40E_PFINT_ICR0_GRST_MASK) {
		if (!test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state))
			set_bit(__I40E_RESET_INTR_RECEIVED, pf->state);
		ena_mask &= ~I40E_PFINT_ICR0_ENA_GRST_MASK;
		val = rd32(hw, I40E_GLGEN_RSTAT);
		val = (val & I40E_GLGEN_RSTAT_RESET_TYPE_MASK)
		       >> I40E_GLGEN_RSTAT_RESET_TYPE_SHIFT;
		if (val == I40E_RESET_CORER) {
			pf->corer_count++;
		} else if (val == I40E_RESET_GLOBR) {
			pf->globr_count++;
		} else if (val == I40E_RESET_EMPR) {
			pf->empr_count++;
			set_bit(__I40E_EMP_RESET_INTR_RECEIVED, pf->state);
		}
	}

	if (icr0 & I40E_PFINT_ICR0_HMC_ERR_MASK) {
		icr0 &= ~I40E_PFINT_ICR0_HMC_ERR_MASK;
		dev_info(&pf->pdev->dev, "HMC error interrupt\n");
		dev_info(&pf->pdev->dev, "HMC error info 0x%x, HMC error data 0x%x\n",
			 rd32(hw, I40E_PFHMC_ERRORINFO),
			 rd32(hw, I40E_PFHMC_ERRORDATA));
	}

	if (icr0 & I40E_PFINT_ICR0_TIMESYNC_MASK) {
		u32 prttsyn_stat = rd32(hw, I40E_PRTTSYN_STAT_0);

		if (prttsyn_stat & I40E_PRTTSYN_STAT_0_TXTIME_MASK) {
			icr0 &= ~I40E_PFINT_ICR0_ENA_TIMESYNC_MASK;
			i40e_ptp_tx_hwtstamp(pf);
		}
	}

	/* If a critical error is pending we have no choice but to reset the
	 * device.
	 * Report and mask out any remaining unexpected interrupts.
	 */
	icr0_remaining = icr0 & ena_mask;
	if (icr0_remaining) {
		dev_info(&pf->pdev->dev, "unhandled interrupt icr0=0x%08x\n",
			 icr0_remaining);
		if ((icr0_remaining & I40E_PFINT_ICR0_PE_CRITERR_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_PCI_EXCEPTION_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_ECC_ERR_MASK)) {
			dev_info(&pf->pdev->dev, "device will be reset\n");
			set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
			i40e_service_event_schedule(pf);
		}
		ena_mask &= ~icr0_remaining;
	}
	ret = IRQ_HANDLED;

enable_intr:
	/* re-enable interrupt causes */
	wr32(hw, I40E_PFINT_ICR0_ENA, ena_mask);
	if (!test_bit(__I40E_DOWN, pf->state) ||
	    test_bit(__I40E_RECOVERY_MODE, pf->state)) {
		i40e_service_event_schedule(pf);
		i40e_irq_dynamic_enable_icr0(pf);
	}

	return ret;
}

/**
 * i40e_clean_fdir_tx_irq - Reclaim resources after transmit completes
 * @tx_ring:  tx ring to clean
 * @budget:   how many cleans we're allowed
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 **/
static bool i40e_clean_fdir_tx_irq(struct i40e_ring *tx_ring, int budget)
{
	struct i40e_vsi *vsi = tx_ring->vsi;
	u16 i = tx_ring->next_to_clean;
	struct i40e_tx_buffer *tx_buf;
	struct i40e_tx_desc *tx_desc;

	tx_buf = &tx_ring->tx_bi[i];
	tx_desc = I40E_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		struct i40e_tx_desc *eop_desc = tx_buf->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if the descriptor isn't done, no work yet to do */
		if (!(eop_desc->cmd_type_offset_bsz &
		      cpu_to_le64(I40E_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;

		tx_desc->buffer_addr = 0;
		tx_desc->cmd_type_offset_bsz = 0;
		/* move past filter desc */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_bi;
			tx_desc = I40E_TX_DESC(tx_ring, 0);
		}
		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buf, dma),
				 dma_unmap_len(tx_buf, len),
				 DMA_TO_DEVICE);
		if (tx_buf->tx_flags & I40E_TX_FLAGS_FD_SB)
			kfree(tx_buf->raw_buf);

		tx_buf->raw_buf = NULL;
		tx_buf->tx_flags = 0;
		tx_buf->next_to_watch = NULL;
		dma_unmap_len_set(tx_buf, len, 0);
		tx_desc->buffer_addr = 0;
		tx_desc->cmd_type_offset_bsz = 0;

		/* move us past the eop_desc for start of next FD desc */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_bi;
			tx_desc = I40E_TX_DESC(tx_ring, 0);
		}

		/* update budget accounting */
		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;

	if (vsi->back->flags & I40E_FLAG_MSIX_ENABLED)
		i40e_irq_dynamic_enable(vsi, tx_ring->q_vector->v_idx);

	return budget > 0;
}

/**
 * i40e_fdir_clean_ring - Interrupt Handler for FDIR SB ring
 * @irq: interrupt number
 * @data: pointer to a q_vector
 **/
static irqreturn_t i40e_fdir_clean_ring(int irq, void *data)
{
	struct i40e_q_vector *q_vector = data;
	struct i40e_vsi *vsi;

	if (!q_vector->tx.ring)
		return IRQ_HANDLED;

	vsi = q_vector->tx.ring->vsi;
	i40e_clean_fdir_tx_irq(q_vector->tx.ring, vsi->work_limit);

	return IRQ_HANDLED;
}

/**
 * i40e_map_vector_to_qp - Assigns the queue pair to the vector
 * @vsi: the VSI being configured
 * @v_idx: vector index
 * @qp_idx: queue pair index
 **/
static void i40e_map_vector_to_qp(struct i40e_vsi *vsi, int v_idx, int qp_idx)
{
	struct i40e_q_vector *q_vector = vsi->q_vectors[v_idx];
	struct i40e_ring *tx_ring = vsi->tx_rings[qp_idx];
	struct i40e_ring *rx_ring = vsi->rx_rings[qp_idx];

	tx_ring->q_vector = q_vector;
	tx_ring->next = q_vector->tx.ring;
	q_vector->tx.ring = tx_ring;
	q_vector->tx.count++;

	/* Place XDP Tx ring in the same q_vector ring list as regular Tx */
	if (i40e_enabled_xdp_vsi(vsi)) {
		struct i40e_ring *xdp_ring = vsi->xdp_rings[qp_idx];

		xdp_ring->q_vector = q_vector;
		xdp_ring->next = q_vector->tx.ring;
		q_vector->tx.ring = xdp_ring;
		q_vector->tx.count++;
	}

	rx_ring->q_vector = q_vector;
	rx_ring->next = q_vector->rx.ring;
	q_vector->rx.ring = rx_ring;
	q_vector->rx.count++;
}

/**
 * i40e_vsi_map_rings_to_vectors - Maps descriptor rings to vectors
 * @vsi: the VSI being configured
 *
 * This function maps descriptor rings to the queue-specific vectors
 * we were allotted through the MSI-X enabling code.  Ideally, we'd have
 * one vector per queue pair, but on a constrained vector budget, we
 * group the queue pairs as "efficiently" as possible.
 **/
static void i40e_vsi_map_rings_to_vectors(struct i40e_vsi *vsi)
{
	int qp_remaining = vsi->num_queue_pairs;
	int q_vectors = vsi->num_q_vectors;
	int num_ringpairs;
	int v_start = 0;
	int qp_idx = 0;

	/* If we don't have enough vectors for a 1-to-1 mapping, we'll have to
	 * group them so there are multiple queues per vector.
	 * It is also important to go through all the vectors available to be
	 * sure that if we don't use all the vectors, that the remaining vectors
	 * are cleared. This is especially important when decreasing the
	 * number of queues in use.
	 */
	for (; v_start < q_vectors; v_start++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[v_start];

		num_ringpairs = DIV_ROUND_UP(qp_remaining, q_vectors - v_start);

		q_vector->num_ringpairs = num_ringpairs;
		q_vector->reg_idx = q_vector->v_idx + vsi->base_vector - 1;

		q_vector->rx.count = 0;
		q_vector->tx.count = 0;
		q_vector->rx.ring = NULL;
		q_vector->tx.ring = NULL;

		while (num_ringpairs--) {
			i40e_map_vector_to_qp(vsi, v_start, qp_idx);
			qp_idx++;
			qp_remaining--;
		}
	}
}

/**
 * i40e_vsi_request_irq - Request IRQ from the OS
 * @vsi: the VSI being configured
 * @basename: name for the vector
 **/
static int i40e_vsi_request_irq(struct i40e_vsi *vsi, char *basename)
{
	struct i40e_pf *pf = vsi->back;
	int err;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		err = i40e_vsi_request_irq_msix(vsi, basename);
	else if (pf->flags & I40E_FLAG_MSI_ENABLED)
		err = request_irq(pf->pdev->irq, i40e_intr, 0,
				  pf->int_name, pf);
	else
		err = request_irq(pf->pdev->irq, i40e_intr, IRQF_SHARED,
				  pf->int_name, pf);

	if (err)
		dev_info(&pf->pdev->dev, "request_irq failed, Error %d\n", err);

	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * i40e_netpoll - A Polling 'interrupt' handler
 * @netdev: network interface device structure
 *
 * This is used by netconsole to send skbs without having to re-enable
 * interrupts.  It's not called while the normal interrupt routine is executing.
 **/
static void i40e_netpoll(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int i;

	/* if interface is down do nothing */
	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = 0; i < vsi->num_q_vectors; i++)
			i40e_msix_clean_rings(0, vsi->q_vectors[i]);
	} else {
		i40e_intr(pf->pdev->irq, netdev);
	}
}
#endif

#define I40E_QTX_ENA_WAIT_COUNT 50

/**
 * i40e_pf_txq_wait - Wait for a PF's Tx queue to be enabled or disabled
 * @pf: the PF being configured
 * @pf_q: the PF queue
 * @enable: enable or disable state of the queue
 *
 * This routine will wait for the given Tx queue of the PF to reach the
 * enabled or disabled state.
 * Returns -ETIMEDOUT in case of failing to reach the requested state after
 * multiple retries; else will return 0 in case of success.
 **/
static int i40e_pf_txq_wait(struct i40e_pf *pf, int pf_q, bool enable)
{
	int i;
	u32 tx_reg;

	for (i = 0; i < I40E_QUEUE_WAIT_RETRY_LIMIT; i++) {
		tx_reg = rd32(&pf->hw, I40E_QTX_ENA(pf_q));
		if (enable == !!(tx_reg & I40E_QTX_ENA_QENA_STAT_MASK))
			break;

		usleep_range(10, 20);
	}
	if (i >= I40E_QUEUE_WAIT_RETRY_LIMIT)
		return -ETIMEDOUT;

	return 0;
}

/**
 * i40e_control_tx_q - Start or stop a particular Tx queue
 * @pf: the PF structure
 * @pf_q: the PF queue to configure
 * @enable: start or stop the queue
 *
 * This function enables or disables a single queue. Note that any delay
 * required after the operation is expected to be handled by the caller of
 * this function.
 **/
static void i40e_control_tx_q(struct i40e_pf *pf, int pf_q, bool enable)
{
	struct i40e_hw *hw = &pf->hw;
	u32 tx_reg;
	int i;

	/* warn the TX unit of coming changes */
	i40e_pre_tx_queue_cfg(&pf->hw, pf_q, enable);
	if (!enable)
		usleep_range(10, 20);

	for (i = 0; i < I40E_QTX_ENA_WAIT_COUNT; i++) {
		tx_reg = rd32(hw, I40E_QTX_ENA(pf_q));
		if (((tx_reg >> I40E_QTX_ENA_QENA_REQ_SHIFT) & 1) ==
		    ((tx_reg >> I40E_QTX_ENA_QENA_STAT_SHIFT) & 1))
			break;
		usleep_range(1000, 2000);
	}

	/* Skip if the queue is already in the requested state */
	if (enable == !!(tx_reg & I40E_QTX_ENA_QENA_STAT_MASK))
		return;

	/* turn on/off the queue */
	if (enable) {
		wr32(hw, I40E_QTX_HEAD(pf_q), 0);
		tx_reg |= I40E_QTX_ENA_QENA_REQ_MASK;
	} else {
		tx_reg &= ~I40E_QTX_ENA_QENA_REQ_MASK;
	}

	wr32(hw, I40E_QTX_ENA(pf_q), tx_reg);
}

/**
 * i40e_control_wait_tx_q - Start/stop Tx queue and wait for completion
 * @seid: VSI SEID
 * @pf: the PF structure
 * @pf_q: the PF queue to configure
 * @is_xdp: true if the queue is used for XDP
 * @enable: start or stop the queue
 **/
int i40e_control_wait_tx_q(int seid, struct i40e_pf *pf, int pf_q,
			   bool is_xdp, bool enable)
{
	int ret;

	i40e_control_tx_q(pf, pf_q, enable);

	/* wait for the change to finish */
	ret = i40e_pf_txq_wait(pf, pf_q, enable);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "VSI seid %d %sTx ring %d %sable timeout\n",
			 seid, (is_xdp ? "XDP " : ""), pf_q,
			 (enable ? "en" : "dis"));
	}

	return ret;
}

/**
 * i40e_vsi_enable_tx - Start a VSI's rings
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_enable_tx(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i, pf_q, ret = 0;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		ret = i40e_control_wait_tx_q(vsi->seid, pf,
					     pf_q,
					     false /*is xdp*/, true);
		if (ret)
			break;

		if (!i40e_enabled_xdp_vsi(vsi))
			continue;

		ret = i40e_control_wait_tx_q(vsi->seid, pf,
					     pf_q + vsi->alloc_queue_pairs,
					     true /*is xdp*/, true);
		if (ret)
			break;
	}
	return ret;
}

/**
 * i40e_pf_rxq_wait - Wait for a PF's Rx queue to be enabled or disabled
 * @pf: the PF being configured
 * @pf_q: the PF queue
 * @enable: enable or disable state of the queue
 *
 * This routine will wait for the given Rx queue of the PF to reach the
 * enabled or disabled state.
 * Returns -ETIMEDOUT in case of failing to reach the requested state after
 * multiple retries; else will return 0 in case of success.
 **/
static int i40e_pf_rxq_wait(struct i40e_pf *pf, int pf_q, bool enable)
{
	int i;
	u32 rx_reg;

	for (i = 0; i < I40E_QUEUE_WAIT_RETRY_LIMIT; i++) {
		rx_reg = rd32(&pf->hw, I40E_QRX_ENA(pf_q));
		if (enable == !!(rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
			break;

		usleep_range(10, 20);
	}
	if (i >= I40E_QUEUE_WAIT_RETRY_LIMIT)
		return -ETIMEDOUT;

	return 0;
}

/**
 * i40e_control_rx_q - Start or stop a particular Rx queue
 * @pf: the PF structure
 * @pf_q: the PF queue to configure
 * @enable: start or stop the queue
 *
 * This function enables or disables a single queue. Note that
 * any delay required after the operation is expected to be
 * handled by the caller of this function.
 **/
static void i40e_control_rx_q(struct i40e_pf *pf, int pf_q, bool enable)
{
	struct i40e_hw *hw = &pf->hw;
	u32 rx_reg;
	int i;

	for (i = 0; i < I40E_QTX_ENA_WAIT_COUNT; i++) {
		rx_reg = rd32(hw, I40E_QRX_ENA(pf_q));
		if (((rx_reg >> I40E_QRX_ENA_QENA_REQ_SHIFT) & 1) ==
		    ((rx_reg >> I40E_QRX_ENA_QENA_STAT_SHIFT) & 1))
			break;
		usleep_range(1000, 2000);
	}

	/* Skip if the queue is already in the requested state */
	if (enable == !!(rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
		return;

	/* turn on/off the queue */
	if (enable)
		rx_reg |= I40E_QRX_ENA_QENA_REQ_MASK;
	else
		rx_reg &= ~I40E_QRX_ENA_QENA_REQ_MASK;

	wr32(hw, I40E_QRX_ENA(pf_q), rx_reg);
}

/**
 * i40e_control_wait_rx_q
 * @pf: the PF structure
 * @pf_q: queue being configured
 * @enable: start or stop the rings
 *
 * This function enables or disables a single queue along with waiting
 * for the change to finish. The caller of this function should handle
 * the delays needed in the case of disabling queues.
 **/
int i40e_control_wait_rx_q(struct i40e_pf *pf, int pf_q, bool enable)
{
	int ret = 0;

	i40e_control_rx_q(pf, pf_q, enable);

	/* wait for the change to finish */
	ret = i40e_pf_rxq_wait(pf, pf_q, enable);
	if (ret)
		return ret;

	return ret;
}

/**
 * i40e_vsi_enable_rx - Start a VSI's rings
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_enable_rx(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i, pf_q, ret = 0;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		ret = i40e_control_wait_rx_q(pf, pf_q, true);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Rx ring %d enable timeout\n",
				 vsi->seid, pf_q);
			break;
		}
	}

	return ret;
}

/**
 * i40e_vsi_start_rings - Start a VSI's rings
 * @vsi: the VSI being configured
 **/
int i40e_vsi_start_rings(struct i40e_vsi *vsi)
{
	int ret = 0;

	/* do rx first for enable and last for disable */
	ret = i40e_vsi_enable_rx(vsi);
	if (ret)
		return ret;
	ret = i40e_vsi_enable_tx(vsi);

	return ret;
}

#define I40E_DISABLE_TX_GAP_MSEC	50

/**
 * i40e_vsi_stop_rings - Stop a VSI's rings
 * @vsi: the VSI being configured
 **/
void i40e_vsi_stop_rings(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int pf_q, err, q_end;

	/* When port TX is suspended, don't wait */
	if (test_bit(__I40E_PORT_SUSPENDED, vsi->back->state))
		return i40e_vsi_stop_rings_no_wait(vsi);

	q_end = vsi->base_queue + vsi->num_queue_pairs;
	for (pf_q = vsi->base_queue; pf_q < q_end; pf_q++)
		i40e_pre_tx_queue_cfg(&pf->hw, (u32)pf_q, false);

	for (pf_q = vsi->base_queue; pf_q < q_end; pf_q++) {
		err = i40e_control_wait_rx_q(pf, pf_q, false);
		if (err)
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Rx ring %d dissable timeout\n",
				 vsi->seid, pf_q);
	}

	msleep(I40E_DISABLE_TX_GAP_MSEC);
	pf_q = vsi->base_queue;
	for (pf_q = vsi->base_queue; pf_q < q_end; pf_q++)
		wr32(&pf->hw, I40E_QTX_ENA(pf_q), 0);

	i40e_vsi_wait_queues_disabled(vsi);
}

/**
 * i40e_vsi_stop_rings_no_wait - Stop a VSI's rings and do not delay
 * @vsi: the VSI being shutdown
 *
 * This function stops all the rings for a VSI but does not delay to verify
 * that rings have been disabled. It is expected that the caller is shutting
 * down multiple VSIs at once and will delay together for all the VSIs after
 * initiating the shutdown. This is particularly useful for shutting down lots
 * of VFs together. Otherwise, a large delay can be incurred while configuring
 * each VSI in serial.
 **/
void i40e_vsi_stop_rings_no_wait(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i, pf_q;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		i40e_control_tx_q(pf, pf_q, false);
		i40e_control_rx_q(pf, pf_q, false);
	}
}

/**
 * i40e_vsi_free_irq - Free the irq association with the OS
 * @vsi: the VSI being configured
 **/
static void i40e_vsi_free_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int base = vsi->base_vector;
	u32 val, qp;
	int i;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		if (!vsi->q_vectors)
			return;

		if (!vsi->irqs_ready)
			return;

		vsi->irqs_ready = false;
		for (i = 0; i < vsi->num_q_vectors; i++) {
			int irq_num;
			u16 vector;

			vector = i + base;
			irq_num = pf->msix_entries[vector].vector;

			/* free only the irqs that were actually requested */
			if (!vsi->q_vectors[i] ||
			    !vsi->q_vectors[i]->num_ringpairs)
				continue;

			/* clear the affinity notifier in the IRQ descriptor */
			irq_set_affinity_notifier(irq_num, NULL);
			/* remove our suggested affinity mask for this IRQ */
			irq_set_affinity_hint(irq_num, NULL);
			synchronize_irq(irq_num);
			free_irq(irq_num, vsi->q_vectors[i]);

			/* Tear down the interrupt queue link list
			 *
			 * We know that they come in pairs and always
			 * the Rx first, then the Tx.  To clear the
			 * link list, stick the EOL value into the
			 * next_q field of the registers.
			 */
			val = rd32(hw, I40E_PFINT_LNKLSTN(vector - 1));
			qp = (val & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK)
				>> I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
			val |= I40E_QUEUE_END_OF_LIST
				<< I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
			wr32(hw, I40E_PFINT_LNKLSTN(vector - 1), val);

			while (qp != I40E_QUEUE_END_OF_LIST) {
				u32 next;

				val = rd32(hw, I40E_QINT_RQCTL(qp));

				val &= ~(I40E_QINT_RQCTL_MSIX_INDX_MASK  |
					 I40E_QINT_RQCTL_MSIX0_INDX_MASK |
					 I40E_QINT_RQCTL_CAUSE_ENA_MASK  |
					 I40E_QINT_RQCTL_INTEVENT_MASK);

				val |= (I40E_QINT_RQCTL_ITR_INDX_MASK |
					 I40E_QINT_RQCTL_NEXTQ_INDX_MASK);

				wr32(hw, I40E_QINT_RQCTL(qp), val);

				val = rd32(hw, I40E_QINT_TQCTL(qp));

				next = (val & I40E_QINT_TQCTL_NEXTQ_INDX_MASK)
					>> I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT;

				val &= ~(I40E_QINT_TQCTL_MSIX_INDX_MASK  |
					 I40E_QINT_TQCTL_MSIX0_INDX_MASK |
					 I40E_QINT_TQCTL_CAUSE_ENA_MASK  |
					 I40E_QINT_TQCTL_INTEVENT_MASK);

				val |= (I40E_QINT_TQCTL_ITR_INDX_MASK |
					 I40E_QINT_TQCTL_NEXTQ_INDX_MASK);

				wr32(hw, I40E_QINT_TQCTL(qp), val);
				qp = next;
			}
		}
	} else {
		free_irq(pf->pdev->irq, pf);

		val = rd32(hw, I40E_PFINT_LNKLST0);
		qp = (val & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK)
			>> I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
		val |= I40E_QUEUE_END_OF_LIST
			<< I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT;
		wr32(hw, I40E_PFINT_LNKLST0, val);

		val = rd32(hw, I40E_QINT_RQCTL(qp));
		val &= ~(I40E_QINT_RQCTL_MSIX_INDX_MASK  |
			 I40E_QINT_RQCTL_MSIX0_INDX_MASK |
			 I40E_QINT_RQCTL_CAUSE_ENA_MASK  |
			 I40E_QINT_RQCTL_INTEVENT_MASK);

		val |= (I40E_QINT_RQCTL_ITR_INDX_MASK |
			I40E_QINT_RQCTL_NEXTQ_INDX_MASK);

		wr32(hw, I40E_QINT_RQCTL(qp), val);

		val = rd32(hw, I40E_QINT_TQCTL(qp));

		val &= ~(I40E_QINT_TQCTL_MSIX_INDX_MASK  |
			 I40E_QINT_TQCTL_MSIX0_INDX_MASK |
			 I40E_QINT_TQCTL_CAUSE_ENA_MASK  |
			 I40E_QINT_TQCTL_INTEVENT_MASK);

		val |= (I40E_QINT_TQCTL_ITR_INDX_MASK |
			I40E_QINT_TQCTL_NEXTQ_INDX_MASK);

		wr32(hw, I40E_QINT_TQCTL(qp), val);
	}
}

/**
 * i40e_free_q_vector - Free memory allocated for specific interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void i40e_free_q_vector(struct i40e_vsi *vsi, int v_idx)
{
	struct i40e_q_vector *q_vector = vsi->q_vectors[v_idx];
	struct i40e_ring *ring;

	if (!q_vector)
		return;

	/* disassociate q_vector from rings */
	i40e_for_each_ring(ring, q_vector->tx)
		ring->q_vector = NULL;

	i40e_for_each_ring(ring, q_vector->rx)
		ring->q_vector = NULL;

	/* only VSI w/ an associated netdev is set up w/ NAPI */
	if (vsi->netdev)
		netif_napi_del(&q_vector->napi);

	vsi->q_vectors[v_idx] = NULL;

	kfree_rcu(q_vector, rcu);
}

/**
 * i40e_vsi_free_q_vectors - Free memory allocated for interrupt vectors
 * @vsi: the VSI being un-configured
 *
 * This frees the memory allocated to the q_vectors and
 * deletes references to the NAPI struct.
 **/
static void i40e_vsi_free_q_vectors(struct i40e_vsi *vsi)
{
	int v_idx;

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++)
		i40e_free_q_vector(vsi, v_idx);
}

/**
 * i40e_reset_interrupt_capability - Disable interrupt setup in OS
 * @pf: board private structure
 **/
static void i40e_reset_interrupt_capability(struct i40e_pf *pf)
{
	/* If we're in Legacy mode, the interrupt was cleaned in vsi_close */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		pci_disable_msix(pf->pdev);
		kfree(pf->msix_entries);
		pf->msix_entries = NULL;
		kfree(pf->irq_pile);
		pf->irq_pile = NULL;
	} else if (pf->flags & I40E_FLAG_MSI_ENABLED) {
		pci_disable_msi(pf->pdev);
	}
	pf->flags &= ~(I40E_FLAG_MSIX_ENABLED | I40E_FLAG_MSI_ENABLED);
}

/**
 * i40e_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @pf: board private structure
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
static void i40e_clear_interrupt_scheme(struct i40e_pf *pf)
{
	int i;

	if (test_bit(__I40E_MISC_IRQ_REQUESTED, pf->state))
		i40e_free_misc_vector(pf);

	i40e_put_lump(pf->irq_pile, pf->iwarp_base_vector,
		      I40E_IWARP_IRQ_PILE_ID);

	i40e_put_lump(pf->irq_pile, 0, I40E_PILE_VALID_BIT-1);
	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i])
			i40e_vsi_free_q_vectors(pf->vsi[i]);
	i40e_reset_interrupt_capability(pf);
}

/**
 * i40e_napi_enable_all - Enable NAPI for all q_vectors in the VSI
 * @vsi: the VSI being configured
 **/
static void i40e_napi_enable_all(struct i40e_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[q_idx];

		if (q_vector->rx.ring || q_vector->tx.ring)
			napi_enable(&q_vector->napi);
	}
}

/**
 * i40e_napi_disable_all - Disable NAPI for all q_vectors in the VSI
 * @vsi: the VSI being configured
 **/
static void i40e_napi_disable_all(struct i40e_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[q_idx];

		if (q_vector->rx.ring || q_vector->tx.ring)
			napi_disable(&q_vector->napi);
	}
}

/**
 * i40e_vsi_close - Shut down a VSI
 * @vsi: the vsi to be quelled
 **/
static void i40e_vsi_close(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	if (!test_and_set_bit(__I40E_VSI_DOWN, vsi->state))
		i40e_down(vsi);
	i40e_vsi_free_irq(vsi);
	i40e_vsi_free_tx_resources(vsi);
	i40e_vsi_free_rx_resources(vsi);
	vsi->current_netdev_flags = 0;
	set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state))
		set_bit(__I40E_CLIENT_RESET, pf->state);
}

/**
 * i40e_quiesce_vsi - Pause a given VSI
 * @vsi: the VSI being paused
 **/
static void i40e_quiesce_vsi(struct i40e_vsi *vsi)
{
	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return;

	set_bit(__I40E_VSI_NEEDS_RESTART, vsi->state);
	if (vsi->netdev && netif_running(vsi->netdev))
		vsi->netdev->netdev_ops->ndo_stop(vsi->netdev);
	else
		i40e_vsi_close(vsi);
}

/**
 * i40e_unquiesce_vsi - Resume a given VSI
 * @vsi: the VSI being resumed
 **/
static void i40e_unquiesce_vsi(struct i40e_vsi *vsi)
{
	if (!test_and_clear_bit(__I40E_VSI_NEEDS_RESTART, vsi->state))
		return;

	if (vsi->netdev && netif_running(vsi->netdev))
		vsi->netdev->netdev_ops->ndo_open(vsi->netdev);
	else
		i40e_vsi_open(vsi);   /* this clears the DOWN bit */
}

/**
 * i40e_pf_quiesce_all_vsi - Pause all VSIs on a PF
 * @pf: the PF
 **/
static void i40e_pf_quiesce_all_vsi(struct i40e_pf *pf)
{
	int v;

	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v])
			i40e_quiesce_vsi(pf->vsi[v]);
	}
}

/**
 * i40e_pf_unquiesce_all_vsi - Resume all VSIs on a PF
 * @pf: the PF
 **/
static void i40e_pf_unquiesce_all_vsi(struct i40e_pf *pf)
{
	int v;

	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v])
			i40e_unquiesce_vsi(pf->vsi[v]);
	}
}

/**
 * i40e_vsi_wait_queues_disabled - Wait for VSI's queues to be disabled
 * @vsi: the VSI being configured
 *
 * Wait until all queues on a given VSI have been disabled.
 **/
int i40e_vsi_wait_queues_disabled(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i, pf_q, ret;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		/* Check and wait for the Tx queue */
		ret = i40e_pf_txq_wait(pf, pf_q, false);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Tx ring %d disable timeout\n",
				 vsi->seid, pf_q);
			return ret;
		}

		if (!i40e_enabled_xdp_vsi(vsi))
			goto wait_rx;

		/* Check and wait for the XDP Tx queue */
		ret = i40e_pf_txq_wait(pf, pf_q + vsi->alloc_queue_pairs,
				       false);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d XDP Tx ring %d disable timeout\n",
				 vsi->seid, pf_q);
			return ret;
		}
wait_rx:
		/* Check and wait for the Rx queue */
		ret = i40e_pf_rxq_wait(pf, pf_q, false);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Rx ring %d disable timeout\n",
				 vsi->seid, pf_q);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_I40E_DCB
/**
 * i40e_pf_wait_queues_disabled - Wait for all queues of PF VSIs to be disabled
 * @pf: the PF
 *
 * This function waits for the queues to be in disabled state for all the
 * VSIs that are managed by this PF.
 **/
static int i40e_pf_wait_queues_disabled(struct i40e_pf *pf)
{
	int v, ret = 0;

	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (pf->vsi[v]) {
			ret = i40e_vsi_wait_queues_disabled(pf->vsi[v]);
			if (ret)
				break;
		}
	}

	return ret;
}

#endif

/**
 * i40e_get_iscsi_tc_map - Return TC map for iSCSI APP
 * @pf: pointer to PF
 *
 * Get TC map for ISCSI PF type that will include iSCSI TC
 * and LAN TC.
 **/
static u8 i40e_get_iscsi_tc_map(struct i40e_pf *pf)
{
	struct i40e_dcb_app_priority_table app;
	struct i40e_hw *hw = &pf->hw;
	u8 enabled_tc = 1; /* TC0 is always enabled */
	u8 tc, i;
	/* Get the iSCSI APP TLV */
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	for (i = 0; i < dcbcfg->numapps; i++) {
		app = dcbcfg->app[i];
		if (app.selector == I40E_APP_SEL_TCPIP &&
		    app.protocolid == I40E_APP_PROTOID_ISCSI) {
			tc = dcbcfg->etscfg.prioritytable[app.priority];
			enabled_tc |= BIT(tc);
			break;
		}
	}

	return enabled_tc;
}

/**
 * i40e_dcb_get_num_tc -  Get the number of TCs from DCBx config
 * @dcbcfg: the corresponding DCBx configuration structure
 *
 * Return the number of TCs from given DCBx configuration
 **/
static u8 i40e_dcb_get_num_tc(struct i40e_dcbx_config *dcbcfg)
{
	int i, tc_unused = 0;
	u8 num_tc = 0;
	u8 ret = 0;

	/* Scan the ETS Config Priority Table to find
	 * traffic class enabled for a given priority
	 * and create a bitmask of enabled TCs
	 */
	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++)
		num_tc |= BIT(dcbcfg->etscfg.prioritytable[i]);

	/* Now scan the bitmask to check for
	 * contiguous TCs starting with TC0
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (num_tc & BIT(i)) {
			if (!tc_unused) {
				ret++;
			} else {
				pr_err("Non-contiguous TC - Disabling DCB\n");
				return 1;
			}
		} else {
			tc_unused = 1;
		}
	}

	/* There is always at least TC0 */
	if (!ret)
		ret = 1;

	return ret;
}

/**
 * i40e_dcb_get_enabled_tc - Get enabled traffic classes
 * @dcbcfg: the corresponding DCBx configuration structure
 *
 * Query the current DCB configuration and return the number of
 * traffic classes enabled from the given DCBX config
 **/
static u8 i40e_dcb_get_enabled_tc(struct i40e_dcbx_config *dcbcfg)
{
	u8 num_tc = i40e_dcb_get_num_tc(dcbcfg);
	u8 enabled_tc = 1;
	u8 i;

	for (i = 0; i < num_tc; i++)
		enabled_tc |= BIT(i);

	return enabled_tc;
}

/**
 * i40e_mqprio_get_enabled_tc - Get enabled traffic classes
 * @pf: PF being queried
 *
 * Query the current MQPRIO configuration and return the number of
 * traffic classes enabled.
 **/
static u8 i40e_mqprio_get_enabled_tc(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	u8 num_tc = vsi->mqprio_qopt.qopt.num_tc;
	u8 enabled_tc = 1, i;

	for (i = 1; i < num_tc; i++)
		enabled_tc |= BIT(i);
	return enabled_tc;
}

/**
 * i40e_pf_get_num_tc - Get enabled traffic classes for PF
 * @pf: PF being queried
 *
 * Return number of traffic classes enabled for the given PF
 **/
static u8 i40e_pf_get_num_tc(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u8 i, enabled_tc = 1;
	u8 num_tc = 0;
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	if (pf->flags & I40E_FLAG_TC_MQPRIO)
		return pf->vsi[pf->lan_vsi]->mqprio_qopt.qopt.num_tc;

	/* If neither MQPRIO nor DCB is enabled, then always use single TC */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return 1;

	/* SFP mode will be enabled for all TCs on port */
	if (!(pf->flags & I40E_FLAG_MFP_ENABLED))
		return i40e_dcb_get_num_tc(dcbcfg);

	/* MFP mode return count of enabled TCs for this PF */
	if (pf->hw.func_caps.iscsi)
		enabled_tc =  i40e_get_iscsi_tc_map(pf);
	else
		return 1; /* Only TC0 */

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & BIT(i))
			num_tc++;
	}
	return num_tc;
}

/**
 * i40e_pf_get_pf_tc_map - Get bitmap for enabled traffic classes
 * @pf: PF being queried
 *
 * Return a bitmap for enabled traffic classes for this PF.
 **/
static u8 i40e_pf_get_tc_map(struct i40e_pf *pf)
{
	if (pf->flags & I40E_FLAG_TC_MQPRIO)
		return i40e_mqprio_get_enabled_tc(pf);

	/* If neither MQPRIO nor DCB is enabled for this PF then just return
	 * default TC
	 */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return I40E_DEFAULT_TRAFFIC_CLASS;

	/* SFP mode we want PF to be enabled for all TCs */
	if (!(pf->flags & I40E_FLAG_MFP_ENABLED))
		return i40e_dcb_get_enabled_tc(&pf->hw.local_dcbx_config);

	/* MFP enabled and iSCSI PF type */
	if (pf->hw.func_caps.iscsi)
		return i40e_get_iscsi_tc_map(pf);
	else
		return I40E_DEFAULT_TRAFFIC_CLASS;
}

/**
 * i40e_vsi_get_bw_info - Query VSI BW Information
 * @vsi: the VSI being queried
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_vsi_get_bw_info(struct i40e_vsi *vsi)
{
	struct i40e_aqc_query_vsi_ets_sla_config_resp bw_ets_config = {0};
	struct i40e_aqc_query_vsi_bw_config_resp bw_config = {0};
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;
	u32 tc_bw_max;
	int i;

	/* Get the VSI level BW configuration */
	ret = i40e_aq_query_vsi_bw_config(hw, vsi->seid, &bw_config, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get PF vsi bw config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return -EINVAL;
	}

	/* Get the VSI level BW configuration per TC */
	ret = i40e_aq_query_vsi_ets_sla_config(hw, vsi->seid, &bw_ets_config,
					       NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get PF vsi ets bw config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return -EINVAL;
	}

	if (bw_config.tc_valid_bits != bw_ets_config.tc_valid_bits) {
		dev_info(&pf->pdev->dev,
			 "Enabled TCs mismatch from querying VSI BW info 0x%08x 0x%08x\n",
			 bw_config.tc_valid_bits,
			 bw_ets_config.tc_valid_bits);
		/* Still continuing */
	}

	vsi->bw_limit = le16_to_cpu(bw_config.port_bw_limit);
	vsi->bw_max_quanta = bw_config.max_bw;
	tc_bw_max = le16_to_cpu(bw_ets_config.tc_bw_max[0]) |
		    (le16_to_cpu(bw_ets_config.tc_bw_max[1]) << 16);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		vsi->bw_ets_share_credits[i] = bw_ets_config.share_credits[i];
		vsi->bw_ets_limit_credits[i] =
					le16_to_cpu(bw_ets_config.credits[i]);
		/* 3 bits out of 4 for each TC */
		vsi->bw_ets_max_quanta[i] = (u8)((tc_bw_max >> (i*4)) & 0x7);
	}

	return 0;
}

/**
 * i40e_vsi_configure_bw_alloc - Configure VSI BW allocation per TC
 * @vsi: the VSI being configured
 * @enabled_tc: TC bitmap
 * @bw_share: BW shared credits per TC
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_vsi_configure_bw_alloc(struct i40e_vsi *vsi, u8 enabled_tc,
				       u8 *bw_share)
{
	struct i40e_aqc_configure_vsi_tc_bw_data bw_data;
	struct i40e_pf *pf = vsi->back;
	i40e_status ret;
	int i;

	/* There is no need to reset BW when mqprio mode is on.  */
	if (pf->flags & I40E_FLAG_TC_MQPRIO)
		return 0;
	if (!vsi->mqprio_qopt.qopt.hw && !(pf->flags & I40E_FLAG_DCB_ENABLED)) {
		ret = i40e_set_bw_limit(vsi, vsi->seid, 0);
		if (ret)
			dev_info(&pf->pdev->dev,
				 "Failed to reset tx rate for vsi->seid %u\n",
				 vsi->seid);
		return ret;
	}
	bw_data.tc_valid_bits = enabled_tc;
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		bw_data.tc_bw_credits[i] = bw_share[i];

	ret = i40e_aq_config_vsi_tc_bw(&pf->hw, vsi->seid, &bw_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "AQ command Config VSI BW allocation per TC failed = %d\n",
			 pf->hw.aq.asq_last_status);
		return -EINVAL;
	}

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		vsi->info.qs_handle[i] = bw_data.qs_handles[i];

	return 0;
}

/**
 * i40e_vsi_config_netdev_tc - Setup the netdev TC configuration
 * @vsi: the VSI being configured
 * @enabled_tc: TC map to be enabled
 *
 **/
static void i40e_vsi_config_netdev_tc(struct i40e_vsi *vsi, u8 enabled_tc)
{
	struct net_device *netdev = vsi->netdev;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u8 netdev_tc = 0;
	int i;
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	if (!netdev)
		return;

	if (!enabled_tc) {
		netdev_reset_tc(netdev);
		return;
	}

	/* Set up actual enabled TCs on the VSI */
	if (netdev_set_num_tc(netdev, vsi->tc_config.numtc))
		return;

	/* set per TC queues for the VSI */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* Only set TC queues for enabled tcs
		 *
		 * e.g. For a VSI that has TC0 and TC3 enabled the
		 * enabled_tc bitmap would be 0x00001001; the driver
		 * will set the numtc for netdev as 2 that will be
		 * referenced by the netdev layer as TC 0 and 1.
		 */
		if (vsi->tc_config.enabled_tc & BIT(i))
			netdev_set_tc_queue(netdev,
					vsi->tc_config.tc_info[i].netdev_tc,
					vsi->tc_config.tc_info[i].qcount,
					vsi->tc_config.tc_info[i].qoffset);
	}

	if (pf->flags & I40E_FLAG_TC_MQPRIO)
		return;

	/* Assign UP2TC map for the VSI */
	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		/* Get the actual TC# for the UP */
		u8 ets_tc = dcbcfg->etscfg.prioritytable[i];
		/* Get the mapped netdev TC# for the UP */
		netdev_tc =  vsi->tc_config.tc_info[ets_tc].netdev_tc;
		netdev_set_prio_tc_map(netdev, i, netdev_tc);
	}
}

/**
 * i40e_vsi_update_queue_map - Update our copy of VSi info with new queue map
 * @vsi: the VSI being configured
 * @ctxt: the ctxt buffer returned from AQ VSI update param command
 **/
static void i40e_vsi_update_queue_map(struct i40e_vsi *vsi,
				      struct i40e_vsi_context *ctxt)
{
	/* copy just the sections touched not the entire info
	 * since not all sections are valid as returned by
	 * update vsi params
	 */
	vsi->info.mapping_flags = ctxt->info.mapping_flags;
	memcpy(&vsi->info.queue_mapping,
	       &ctxt->info.queue_mapping, sizeof(vsi->info.queue_mapping));
	memcpy(&vsi->info.tc_mapping, ctxt->info.tc_mapping,
	       sizeof(vsi->info.tc_mapping));
}

/**
 * i40e_update_adq_vsi_queues - update queue mapping for ADq VSI
 * @vsi: the VSI being reconfigured
 * @vsi_offset: offset from main VF VSI
 */
int i40e_update_adq_vsi_queues(struct i40e_vsi *vsi, int vsi_offset)
{
	struct i40e_vsi_context ctxt = {};
	struct i40e_pf *pf;
	struct i40e_hw *hw;
	int ret;

	if (!vsi)
		return I40E_ERR_PARAM;
	pf = vsi->back;
	hw = &pf->hw;

	ctxt.seid = vsi->seid;
	ctxt.pf_num = hw->pf_id;
	ctxt.vf_num = vsi->vf_id + hw->func_caps.vf_base_id + vsi_offset;
	ctxt.uplink_seid = vsi->uplink_seid;
	ctxt.connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
	ctxt.flags = I40E_AQ_VSI_TYPE_VF;
	ctxt.info = vsi->info;

	i40e_vsi_setup_queue_map(vsi, &ctxt, vsi->tc_config.enabled_tc,
				 false);
	if (vsi->reconfig_rss) {
		vsi->rss_size = min_t(int, pf->alloc_rss_size,
				      vsi->num_queue_pairs);
		ret = i40e_vsi_config_rss(vsi);
		if (ret) {
			dev_info(&pf->pdev->dev, "Failed to reconfig rss for num_queues\n");
			return ret;
		}
		vsi->reconfig_rss = false;
	}

	ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev, "Update vsi config failed, err %s aq_err %s\n",
			 i40e_stat_str(hw, ret),
			 i40e_aq_str(hw, hw->aq.asq_last_status));
		return ret;
	}
	/* update the local VSI info with updated queue map */
	i40e_vsi_update_queue_map(vsi, &ctxt);
	vsi->info.valid_sections = 0;

	return ret;
}

/**
 * i40e_vsi_config_tc - Configure VSI Tx Scheduler for given TC map
 * @vsi: VSI to be configured
 * @enabled_tc: TC bitmap
 *
 * This configures a particular VSI for TCs that are mapped to the
 * given TC bitmap. It uses default bandwidth share for TCs across
 * VSIs to configure TC for a particular VSI.
 *
 * NOTE:
 * It is expected that the VSI queues have been quisced before calling
 * this function.
 **/
static int i40e_vsi_config_tc(struct i40e_vsi *vsi, u8 enabled_tc)
{
	u8 bw_share[I40E_MAX_TRAFFIC_CLASS] = {0};
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	int ret = 0;
	int i;

	/* Check if enabled_tc is same as existing or new TCs */
	if (vsi->tc_config.enabled_tc == enabled_tc &&
	    vsi->mqprio_qopt.mode != TC_MQPRIO_MODE_CHANNEL)
		return ret;

	/* Enable ETS TCs with equal BW Share for now across all VSIs */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & BIT(i))
			bw_share[i] = 1;
	}

	ret = i40e_vsi_configure_bw_alloc(vsi, enabled_tc, bw_share);
	if (ret) {
		struct i40e_aqc_query_vsi_bw_config_resp bw_config = {0};

		dev_info(&pf->pdev->dev,
			 "Failed configuring TC map %d for VSI %d\n",
			 enabled_tc, vsi->seid);
		ret = i40e_aq_query_vsi_bw_config(hw, vsi->seid,
						  &bw_config, NULL);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Failed querying vsi bw info, err %s aq_err %s\n",
				 i40e_stat_str(hw, ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
			goto out;
		}
		if ((bw_config.tc_valid_bits & enabled_tc) != enabled_tc) {
			u8 valid_tc = bw_config.tc_valid_bits & enabled_tc;

			if (!valid_tc)
				valid_tc = bw_config.tc_valid_bits;
			/* Always enable TC0, no matter what */
			valid_tc |= 1;
			dev_info(&pf->pdev->dev,
				 "Requested tc 0x%x, but FW reports 0x%x as valid. Attempting to use 0x%x.\n",
				 enabled_tc, bw_config.tc_valid_bits, valid_tc);
			enabled_tc = valid_tc;
		}

		ret = i40e_vsi_configure_bw_alloc(vsi, enabled_tc, bw_share);
		if (ret) {
			dev_err(&pf->pdev->dev,
				"Unable to  configure TC map %d for VSI %d\n",
				enabled_tc, vsi->seid);
			goto out;
		}
	}

	/* Update Queue Pairs Mapping for currently enabled UPs */
	ctxt.seid = vsi->seid;
	ctxt.pf_num = vsi->back->hw.pf_id;
	ctxt.vf_num = 0;
	ctxt.uplink_seid = vsi->uplink_seid;
	ctxt.info = vsi->info;
	if (vsi->back->flags & I40E_FLAG_TC_MQPRIO) {
		ret = i40e_vsi_setup_queue_map_mqprio(vsi, &ctxt, enabled_tc);
		if (ret)
			goto out;
	} else {
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, false);
	}

	/* On destroying the qdisc, reset vsi->rss_size, as number of enabled
	 * queues changed.
	 */
	if (!vsi->mqprio_qopt.qopt.hw && vsi->reconfig_rss) {
		vsi->rss_size = min_t(int, vsi->back->alloc_rss_size,
				      vsi->num_queue_pairs);
		ret = i40e_vsi_config_rss(vsi);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "Failed to reconfig rss for num_queues\n");
			return ret;
		}
		vsi->reconfig_rss = false;
	}
	if (vsi->back->flags & I40E_FLAG_IWARP_ENABLED) {
		ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
		ctxt.info.queueing_opt_flags |= I40E_AQ_VSI_QUE_OPT_TCP_ENA;
	}

	/* Update the VSI after updating the VSI queue-mapping
	 * information
	 */
	ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Update vsi tc config failed, err %s aq_err %s\n",
			 i40e_stat_str(hw, ret),
			 i40e_aq_str(hw, hw->aq.asq_last_status));
		goto out;
	}
	/* update the local VSI info with updated queue map */
	i40e_vsi_update_queue_map(vsi, &ctxt);
	vsi->info.valid_sections = 0;

	/* Update current VSI BW information */
	ret = i40e_vsi_get_bw_info(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Failed updating vsi bw info, err %s aq_err %s\n",
			 i40e_stat_str(hw, ret),
			 i40e_aq_str(hw, hw->aq.asq_last_status));
		goto out;
	}

	/* Update the netdev TC setup */
	i40e_vsi_config_netdev_tc(vsi, enabled_tc);
out:
	return ret;
}

/**
 * i40e_get_link_speed - Returns link speed for the interface
 * @vsi: VSI to be configured
 *
 **/
static int i40e_get_link_speed(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	switch (pf->hw.phy.link_info.link_speed) {
	case I40E_LINK_SPEED_40GB:
		return 40000;
	case I40E_LINK_SPEED_25GB:
		return 25000;
	case I40E_LINK_SPEED_20GB:
		return 20000;
	case I40E_LINK_SPEED_10GB:
		return 10000;
	case I40E_LINK_SPEED_1GB:
		return 1000;
	default:
		return -EINVAL;
	}
}

/**
 * i40e_set_bw_limit - setup BW limit for Tx traffic based on max_tx_rate
 * @vsi: VSI to be configured
 * @seid: seid of the channel/VSI
 * @max_tx_rate: max TX rate to be configured as BW limit
 *
 * Helper function to set BW limit for a given VSI
 **/
int i40e_set_bw_limit(struct i40e_vsi *vsi, u16 seid, u64 max_tx_rate)
{
	struct i40e_pf *pf = vsi->back;
	u64 credits = 0;
	int speed = 0;
	int ret = 0;

	speed = i40e_get_link_speed(vsi);
	if (max_tx_rate > speed) {
		dev_err(&pf->pdev->dev,
			"Invalid max tx rate %llu specified for VSI seid %d.",
			max_tx_rate, seid);
		return -EINVAL;
	}
	if (max_tx_rate && max_tx_rate < 50) {
		dev_warn(&pf->pdev->dev,
			 "Setting max tx rate to minimum usable value of 50Mbps.\n");
		max_tx_rate = 50;
	}

	/* Tx rate credits are in values of 50Mbps, 0 is disabled */
	credits = max_tx_rate;
	do_div(credits, I40E_BW_CREDIT_DIVISOR);
	ret = i40e_aq_config_vsi_bw_limit(&pf->hw, seid, credits,
					  I40E_MAX_BW_INACTIVE_ACCUM, NULL);
	if (ret)
		dev_err(&pf->pdev->dev,
			"Failed set tx rate (%llu Mbps) for vsi->seid %u, err %s aq_err %s\n",
			max_tx_rate, seid, i40e_stat_str(&pf->hw, ret),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	return ret;
}

/**
 * i40e_remove_queue_channels - Remove queue channels for the TCs
 * @vsi: VSI to be configured
 *
 * Remove queue channels for the TCs
 **/
static void i40e_remove_queue_channels(struct i40e_vsi *vsi)
{
	enum i40e_admin_queue_err last_aq_status;
	struct i40e_cloud_filter *cfilter;
	struct i40e_channel *ch, *ch_tmp;
	struct i40e_pf *pf = vsi->back;
	struct hlist_node *node;
	int ret, i;

	/* Reset rss size that was stored when reconfiguring rss for
	 * channel VSIs with non-power-of-2 queue count.
	 */
	vsi->current_rss_size = 0;

	/* perform cleanup for channels if they exist */
	if (list_empty(&vsi->ch_list))
		return;

	list_for_each_entry_safe(ch, ch_tmp, &vsi->ch_list, list) {
		struct i40e_vsi *p_vsi;

		list_del(&ch->list);
		p_vsi = ch->parent_vsi;
		if (!p_vsi || !ch->initialized) {
			kfree(ch);
			continue;
		}
		/* Reset queue contexts */
		for (i = 0; i < ch->num_queue_pairs; i++) {
			struct i40e_ring *tx_ring, *rx_ring;
			u16 pf_q;

			pf_q = ch->base_queue + i;
			tx_ring = vsi->tx_rings[pf_q];
			tx_ring->ch = NULL;

			rx_ring = vsi->rx_rings[pf_q];
			rx_ring->ch = NULL;
		}

		/* Reset BW configured for this VSI via mqprio */
		ret = i40e_set_bw_limit(vsi, ch->seid, 0);
		if (ret)
			dev_info(&vsi->back->pdev->dev,
				 "Failed to reset tx rate for ch->seid %u\n",
				 ch->seid);

		/* delete cloud filters associated with this channel */
		hlist_for_each_entry_safe(cfilter, node,
					  &pf->cloud_filter_list, cloud_node) {
			if (cfilter->seid != ch->seid)
				continue;

			hash_del(&cfilter->cloud_node);
			if (cfilter->dst_port)
				ret = i40e_add_del_cloud_filter_big_buf(vsi,
									cfilter,
									false);
			else
				ret = i40e_add_del_cloud_filter(vsi, cfilter,
								false);
			last_aq_status = pf->hw.aq.asq_last_status;
			if (ret)
				dev_info(&pf->pdev->dev,
					 "Failed to delete cloud filter, err %s aq_err %s\n",
					 i40e_stat_str(&pf->hw, ret),
					 i40e_aq_str(&pf->hw, last_aq_status));
			kfree(cfilter);
		}

		/* delete VSI from FW */
		ret = i40e_aq_delete_element(&vsi->back->hw, ch->seid,
					     NULL);
		if (ret)
			dev_err(&vsi->back->pdev->dev,
				"unable to remove channel (%d) for parent VSI(%d)\n",
				ch->seid, p_vsi->seid);
		kfree(ch);
	}
	INIT_LIST_HEAD(&vsi->ch_list);
}

/**
 * i40e_get_max_queues_for_channel
 * @vsi: ptr to VSI to which channels are associated with
 *
 * Helper function which returns max value among the queue counts set on the
 * channels/TCs created.
 **/
static int i40e_get_max_queues_for_channel(struct i40e_vsi *vsi)
{
	struct i40e_channel *ch, *ch_tmp;
	int max = 0;

	list_for_each_entry_safe(ch, ch_tmp, &vsi->ch_list, list) {
		if (!ch->initialized)
			continue;
		if (ch->num_queue_pairs > max)
			max = ch->num_queue_pairs;
	}

	return max;
}

/**
 * i40e_validate_num_queues - validate num_queues w.r.t channel
 * @pf: ptr to PF device
 * @num_queues: number of queues
 * @vsi: the parent VSI
 * @reconfig_rss: indicates should the RSS be reconfigured or not
 *
 * This function validates number of queues in the context of new channel
 * which is being established and determines if RSS should be reconfigured
 * or not for parent VSI.
 **/
static int i40e_validate_num_queues(struct i40e_pf *pf, int num_queues,
				    struct i40e_vsi *vsi, bool *reconfig_rss)
{
	int max_ch_queues;

	if (!reconfig_rss)
		return -EINVAL;

	*reconfig_rss = false;
	if (vsi->current_rss_size) {
		if (num_queues > vsi->current_rss_size) {
			dev_dbg(&pf->pdev->dev,
				"Error: num_queues (%d) > vsi's current_size(%d)\n",
				num_queues, vsi->current_rss_size);
			return -EINVAL;
		} else if ((num_queues < vsi->current_rss_size) &&
			   (!is_power_of_2(num_queues))) {
			dev_dbg(&pf->pdev->dev,
				"Error: num_queues (%d) < vsi's current_size(%d), but not power of 2\n",
				num_queues, vsi->current_rss_size);
			return -EINVAL;
		}
	}

	if (!is_power_of_2(num_queues)) {
		/* Find the max num_queues configured for channel if channel
		 * exist.
		 * if channel exist, then enforce 'num_queues' to be more than
		 * max ever queues configured for channel.
		 */
		max_ch_queues = i40e_get_max_queues_for_channel(vsi);
		if (num_queues < max_ch_queues) {
			dev_dbg(&pf->pdev->dev,
				"Error: num_queues (%d) < max queues configured for channel(%d)\n",
				num_queues, max_ch_queues);
			return -EINVAL;
		}
		*reconfig_rss = true;
	}

	return 0;
}

/**
 * i40e_vsi_reconfig_rss - reconfig RSS based on specified rss_size
 * @vsi: the VSI being setup
 * @rss_size: size of RSS, accordingly LUT gets reprogrammed
 *
 * This function reconfigures RSS by reprogramming LUTs using 'rss_size'
 **/
static int i40e_vsi_reconfig_rss(struct i40e_vsi *vsi, u16 rss_size)
{
	struct i40e_pf *pf = vsi->back;
	u8 seed[I40E_HKEY_ARRAY_SIZE];
	struct i40e_hw *hw = &pf->hw;
	int local_rss_size;
	u8 *lut;
	int ret;

	if (!vsi->rss_size)
		return -EINVAL;

	if (rss_size > vsi->rss_size)
		return -EINVAL;

	local_rss_size = min_t(int, vsi->rss_size, rss_size);
	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	/* Ignoring user configured lut if there is one */
	i40e_fill_rss_lut(pf, lut, vsi->rss_table_size, local_rss_size);

	/* Use user configured hash key if there is one, otherwise
	 * use default.
	 */
	if (vsi->rss_hkey_user)
		memcpy(seed, vsi->rss_hkey_user, I40E_HKEY_ARRAY_SIZE);
	else
		netdev_rss_key_fill((void *)seed, I40E_HKEY_ARRAY_SIZE);

	ret = i40e_config_rss(vsi, seed, lut, vsi->rss_table_size);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Cannot set RSS lut, err %s aq_err %s\n",
			 i40e_stat_str(hw, ret),
			 i40e_aq_str(hw, hw->aq.asq_last_status));
		kfree(lut);
		return ret;
	}
	kfree(lut);

	/* Do the update w.r.t. storing rss_size */
	if (!vsi->orig_rss_size)
		vsi->orig_rss_size = vsi->rss_size;
	vsi->current_rss_size = local_rss_size;

	return ret;
}

/**
 * i40e_channel_setup_queue_map - Setup a channel queue map
 * @pf: ptr to PF device
 * @ctxt: VSI context structure
 * @ch: ptr to channel structure
 *
 * Setup queue map for a specific channel
 **/
static void i40e_channel_setup_queue_map(struct i40e_pf *pf,
					 struct i40e_vsi_context *ctxt,
					 struct i40e_channel *ch)
{
	u16 qcount, qmap, sections = 0;
	u8 offset = 0;
	int pow;

	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	sections |= I40E_AQ_VSI_PROP_SCHED_VALID;

	qcount = min_t(int, ch->num_queue_pairs, pf->num_lan_msix);
	ch->num_queue_pairs = qcount;

	/* find the next higher power-of-2 of num queue pairs */
	pow = ilog2(qcount);
	if (!is_power_of_2(qcount))
		pow++;

	qmap = (offset << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
		(pow << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT);

	/* Setup queue TC[0].qmap for given VSI context */
	ctxt->info.tc_mapping[0] = cpu_to_le16(qmap);

	ctxt->info.up_enable_bits = 0x1; /* TC0 enabled */
	ctxt->info.mapping_flags |= cpu_to_le16(I40E_AQ_VSI_QUE_MAP_CONTIG);
	ctxt->info.queue_mapping[0] = cpu_to_le16(ch->base_queue);
	ctxt->info.valid_sections |= cpu_to_le16(sections);
}

/**
 * i40e_add_channel - add a channel by adding VSI
 * @pf: ptr to PF device
 * @uplink_seid: underlying HW switching element (VEB) ID
 * @ch: ptr to channel structure
 *
 * Add a channel (VSI) using add_vsi and queue_map
 **/
static int i40e_add_channel(struct i40e_pf *pf, u16 uplink_seid,
			    struct i40e_channel *ch)
{
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	u8 enabled_tc = 0x1; /* TC0 enabled */
	int ret;

	if (ch->type != I40E_VSI_VMDQ2) {
		dev_info(&pf->pdev->dev,
			 "add new vsi failed, ch->type %d\n", ch->type);
		return -EINVAL;
	}

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.pf_num = hw->pf_id;
	ctxt.vf_num = 0;
	ctxt.uplink_seid = uplink_seid;
	ctxt.connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
	if (ch->type == I40E_VSI_VMDQ2)
		ctxt.flags = I40E_AQ_VSI_TYPE_VMDQ2;

	if (pf->flags & I40E_FLAG_VEB_MODE_ENABLED) {
		ctxt.info.valid_sections |=
		     cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
		ctxt.info.switch_id =
		   cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);
	}

	/* Set queue map for a given VSI context */
	i40e_channel_setup_queue_map(pf, &ctxt, ch);

	/* Now time to create VSI */
	ret = i40e_aq_add_vsi(hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "add new vsi failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw,
				     pf->hw.aq.asq_last_status));
		return -ENOENT;
	}

	/* Success, update channel, set enabled_tc only if the channel
	 * is not a macvlan
	 */
	ch->enabled_tc = !i40e_is_channel_macvlan(ch) && enabled_tc;
	ch->seid = ctxt.seid;
	ch->vsi_number = ctxt.vsi_number;
	ch->stat_counter_idx = le16_to_cpu(ctxt.info.stat_counter_idx);

	/* copy just the sections touched not the entire info
	 * since not all sections are valid as returned by
	 * update vsi params
	 */
	ch->info.mapping_flags = ctxt.info.mapping_flags;
	memcpy(&ch->info.queue_mapping,
	       &ctxt.info.queue_mapping, sizeof(ctxt.info.queue_mapping));
	memcpy(&ch->info.tc_mapping, ctxt.info.tc_mapping,
	       sizeof(ctxt.info.tc_mapping));

	return 0;
}

static int i40e_channel_config_bw(struct i40e_vsi *vsi, struct i40e_channel *ch,
				  u8 *bw_share)
{
	struct i40e_aqc_configure_vsi_tc_bw_data bw_data;
	i40e_status ret;
	int i;

	bw_data.tc_valid_bits = ch->enabled_tc;
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		bw_data.tc_bw_credits[i] = bw_share[i];

	ret = i40e_aq_config_vsi_tc_bw(&vsi->back->hw, ch->seid,
				       &bw_data, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Config VSI BW allocation per TC failed, aq_err: %d for new_vsi->seid %u\n",
			 vsi->back->hw.aq.asq_last_status, ch->seid);
		return -EINVAL;
	}

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		ch->info.qs_handle[i] = bw_data.qs_handles[i];

	return 0;
}

/**
 * i40e_channel_config_tx_ring - config TX ring associated with new channel
 * @pf: ptr to PF device
 * @vsi: the VSI being setup
 * @ch: ptr to channel structure
 *
 * Configure TX rings associated with channel (VSI) since queues are being
 * from parent VSI.
 **/
static int i40e_channel_config_tx_ring(struct i40e_pf *pf,
				       struct i40e_vsi *vsi,
				       struct i40e_channel *ch)
{
	i40e_status ret;
	int i;
	u8 bw_share[I40E_MAX_TRAFFIC_CLASS] = {0};

	/* Enable ETS TCs with equal BW Share for now across all VSIs */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (ch->enabled_tc & BIT(i))
			bw_share[i] = 1;
	}

	/* configure BW for new VSI */
	ret = i40e_channel_config_bw(vsi, ch, bw_share);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed configuring TC map %d for channel (seid %u)\n",
			 ch->enabled_tc, ch->seid);
		return ret;
	}

	for (i = 0; i < ch->num_queue_pairs; i++) {
		struct i40e_ring *tx_ring, *rx_ring;
		u16 pf_q;

		pf_q = ch->base_queue + i;

		/* Get to TX ring ptr of main VSI, for re-setup TX queue
		 * context
		 */
		tx_ring = vsi->tx_rings[pf_q];
		tx_ring->ch = ch;

		/* Get the RX ring ptr */
		rx_ring = vsi->rx_rings[pf_q];
		rx_ring->ch = ch;
	}

	return 0;
}

/**
 * i40e_setup_hw_channel - setup new channel
 * @pf: ptr to PF device
 * @vsi: the VSI being setup
 * @ch: ptr to channel structure
 * @uplink_seid: underlying HW switching element (VEB) ID
 * @type: type of channel to be created (VMDq2/VF)
 *
 * Setup new channel (VSI) based on specified type (VMDq2/VF)
 * and configures TX rings accordingly
 **/
static inline int i40e_setup_hw_channel(struct i40e_pf *pf,
					struct i40e_vsi *vsi,
					struct i40e_channel *ch,
					u16 uplink_seid, u8 type)
{
	int ret;

	ch->initialized = false;
	ch->base_queue = vsi->next_base_queue;
	ch->type = type;

	/* Proceed with creation of channel (VMDq2) VSI */
	ret = i40e_add_channel(pf, uplink_seid, ch);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "failed to add_channel using uplink_seid %u\n",
			 uplink_seid);
		return ret;
	}

	/* Mark the successful creation of channel */
	ch->initialized = true;

	/* Reconfigure TX queues using QTX_CTL register */
	ret = i40e_channel_config_tx_ring(pf, vsi, ch);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "failed to configure TX rings for channel %u\n",
			 ch->seid);
		return ret;
	}

	/* update 'next_base_queue' */
	vsi->next_base_queue = vsi->next_base_queue + ch->num_queue_pairs;
	dev_dbg(&pf->pdev->dev,
		"Added channel: vsi_seid %u, vsi_number %u, stat_counter_idx %u, num_queue_pairs %u, pf->next_base_queue %d\n",
		ch->seid, ch->vsi_number, ch->stat_counter_idx,
		ch->num_queue_pairs,
		vsi->next_base_queue);
	return ret;
}

/**
 * i40e_setup_channel - setup new channel using uplink element
 * @pf: ptr to PF device
 * @vsi: pointer to the VSI to set up the channel within
 * @ch: ptr to channel structure
 *
 * Setup new channel (VSI) based on specified type (VMDq2/VF)
 * and uplink switching element (uplink_seid)
 **/
static bool i40e_setup_channel(struct i40e_pf *pf, struct i40e_vsi *vsi,
			       struct i40e_channel *ch)
{
	u8 vsi_type;
	u16 seid;
	int ret;

	if (vsi->type == I40E_VSI_MAIN) {
		vsi_type = I40E_VSI_VMDQ2;
	} else {
		dev_err(&pf->pdev->dev, "unsupported parent vsi type(%d)\n",
			vsi->type);
		return false;
	}

	/* underlying switching element */
	seid = pf->vsi[pf->lan_vsi]->uplink_seid;

	/* create channel (VSI), configure TX rings */
	ret = i40e_setup_hw_channel(pf, vsi, ch, seid, vsi_type);
	if (ret) {
		dev_err(&pf->pdev->dev, "failed to setup hw_channel\n");
		return false;
	}

	return ch->initialized ? true : false;
}

/**
 * i40e_validate_and_set_switch_mode - sets up switch mode correctly
 * @vsi: ptr to VSI which has PF backing
 *
 * Sets up switch mode correctly if it needs to be changed and perform
 * what are allowed modes.
 **/
static int i40e_validate_and_set_switch_mode(struct i40e_vsi *vsi)
{
	u8 mode;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int ret;

	ret = i40e_get_capabilities(pf, i40e_aqc_opc_list_dev_capabilities);
	if (ret)
		return -EINVAL;

	if (hw->dev_caps.switch_mode) {
		/* if switch mode is set, support mode2 (non-tunneled for
		 * cloud filter) for now
		 */
		u32 switch_mode = hw->dev_caps.switch_mode &
				  I40E_SWITCH_MODE_MASK;
		if (switch_mode >= I40E_CLOUD_FILTER_MODE1) {
			if (switch_mode == I40E_CLOUD_FILTER_MODE2)
				return 0;
			dev_err(&pf->pdev->dev,
				"Invalid switch_mode (%d), only non-tunneled mode for cloud filter is supported\n",
				hw->dev_caps.switch_mode);
			return -EINVAL;
		}
	}

	/* Set Bit 7 to be valid */
	mode = I40E_AQ_SET_SWITCH_BIT7_VALID;

	/* Set L4type for TCP support */
	mode |= I40E_AQ_SET_SWITCH_L4_TYPE_TCP;

	/* Set cloud filter mode */
	mode |= I40E_AQ_SET_SWITCH_MODE_NON_TUNNEL;

	/* Prep mode field for set_switch_config */
	ret = i40e_aq_set_switch_config(hw, pf->last_sw_conf_flags,
					pf->last_sw_conf_valid_flags,
					mode, NULL);
	if (ret && hw->aq.asq_last_status != I40E_AQ_RC_ESRCH)
		dev_err(&pf->pdev->dev,
			"couldn't set switch config bits, err %s aq_err %s\n",
			i40e_stat_str(hw, ret),
			i40e_aq_str(hw,
				    hw->aq.asq_last_status));

	return ret;
}

/**
 * i40e_create_queue_channel - function to create channel
 * @vsi: VSI to be configured
 * @ch: ptr to channel (it contains channel specific params)
 *
 * This function creates channel (VSI) using num_queues specified by user,
 * reconfigs RSS if needed.
 **/
int i40e_create_queue_channel(struct i40e_vsi *vsi,
			      struct i40e_channel *ch)
{
	struct i40e_pf *pf = vsi->back;
	bool reconfig_rss;
	int err;

	if (!ch)
		return -EINVAL;

	if (!ch->num_queue_pairs) {
		dev_err(&pf->pdev->dev, "Invalid num_queues requested: %d\n",
			ch->num_queue_pairs);
		return -EINVAL;
	}

	/* validate user requested num_queues for channel */
	err = i40e_validate_num_queues(pf, ch->num_queue_pairs, vsi,
				       &reconfig_rss);
	if (err) {
		dev_info(&pf->pdev->dev, "Failed to validate num_queues (%d)\n",
			 ch->num_queue_pairs);
		return -EINVAL;
	}

	/* By default we are in VEPA mode, if this is the first VF/VMDq
	 * VSI to be added switch to VEB mode.
	 */

	if (!(pf->flags & I40E_FLAG_VEB_MODE_ENABLED)) {
		pf->flags |= I40E_FLAG_VEB_MODE_ENABLED;

		if (vsi->type == I40E_VSI_MAIN) {
			if (pf->flags & I40E_FLAG_TC_MQPRIO)
				i40e_do_reset(pf, I40E_PF_RESET_FLAG, true);
			else
				i40e_do_reset_safe(pf, I40E_PF_RESET_FLAG);
		}
		/* now onwards for main VSI, number of queues will be value
		 * of TC0's queue count
		 */
	}

	/* By this time, vsi->cnt_q_avail shall be set to non-zero and
	 * it should be more than num_queues
	 */
	if (!vsi->cnt_q_avail || vsi->cnt_q_avail < ch->num_queue_pairs) {
		dev_dbg(&pf->pdev->dev,
			"Error: cnt_q_avail (%u) less than num_queues %d\n",
			vsi->cnt_q_avail, ch->num_queue_pairs);
		return -EINVAL;
	}

	/* reconfig_rss only if vsi type is MAIN_VSI */
	if (reconfig_rss && (vsi->type == I40E_VSI_MAIN)) {
		err = i40e_vsi_reconfig_rss(vsi, ch->num_queue_pairs);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "Error: unable to reconfig rss for num_queues (%u)\n",
				 ch->num_queue_pairs);
			return -EINVAL;
		}
	}

	if (!i40e_setup_channel(pf, vsi, ch)) {
		dev_info(&pf->pdev->dev, "Failed to setup channel\n");
		return -EINVAL;
	}

	dev_info(&pf->pdev->dev,
		 "Setup channel (id:%u) utilizing num_queues %d\n",
		 ch->seid, ch->num_queue_pairs);

	/* configure VSI for BW limit */
	if (ch->max_tx_rate) {
		u64 credits = ch->max_tx_rate;

		if (i40e_set_bw_limit(vsi, ch->seid, ch->max_tx_rate))
			return -EINVAL;

		do_div(credits, I40E_BW_CREDIT_DIVISOR);
		dev_dbg(&pf->pdev->dev,
			"Set tx rate of %llu Mbps (count of 50Mbps %llu) for vsi->seid %u\n",
			ch->max_tx_rate,
			credits,
			ch->seid);
	}

	/* in case of VF, this will be main SRIOV VSI */
	ch->parent_vsi = vsi;

	/* and update main_vsi's count for queue_available to use */
	vsi->cnt_q_avail -= ch->num_queue_pairs;

	return 0;
}

/**
 * i40e_configure_queue_channels - Add queue channel for the given TCs
 * @vsi: VSI to be configured
 *
 * Configures queue channel mapping to the given TCs
 **/
static int i40e_configure_queue_channels(struct i40e_vsi *vsi)
{
	struct i40e_channel *ch;
	u64 max_rate = 0;
	int ret = 0, i;

	/* Create app vsi with the TCs. Main VSI with TC0 is already set up */
	vsi->tc_seid_map[0] = vsi->seid;
	for (i = 1; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (vsi->tc_config.enabled_tc & BIT(i)) {
			ch = kzalloc(sizeof(*ch), GFP_KERNEL);
			if (!ch) {
				ret = -ENOMEM;
				goto err_free;
			}

			INIT_LIST_HEAD(&ch->list);
			ch->num_queue_pairs =
				vsi->tc_config.tc_info[i].qcount;
			ch->base_queue =
				vsi->tc_config.tc_info[i].qoffset;

			/* Bandwidth limit through tc interface is in bytes/s,
			 * change to Mbit/s
			 */
			max_rate = vsi->mqprio_qopt.max_rate[i];
			do_div(max_rate, I40E_BW_MBPS_DIVISOR);
			ch->max_tx_rate = max_rate;

			list_add_tail(&ch->list, &vsi->ch_list);

			ret = i40e_create_queue_channel(vsi, ch);
			if (ret) {
				dev_err(&vsi->back->pdev->dev,
					"Failed creating queue channel with TC%d: queues %d\n",
					i, ch->num_queue_pairs);
				goto err_free;
			}
			vsi->tc_seid_map[i] = ch->seid;
		}
	}
	return ret;

err_free:
	i40e_remove_queue_channels(vsi);
	return ret;
}

/**
 * i40e_veb_config_tc - Configure TCs for given VEB
 * @veb: given VEB
 * @enabled_tc: TC bitmap
 *
 * Configures given TC bitmap for VEB (switching) element
 **/
int i40e_veb_config_tc(struct i40e_veb *veb, u8 enabled_tc)
{
	struct i40e_aqc_configure_switching_comp_bw_config_data bw_data = {0};
	struct i40e_pf *pf = veb->pf;
	int ret = 0;
	int i;

	/* No TCs or already enabled TCs just return */
	if (!enabled_tc || veb->enabled_tc == enabled_tc)
		return ret;

	bw_data.tc_valid_bits = enabled_tc;
	/* bw_data.absolute_credits is not set (relative) */

	/* Enable ETS TCs with equal BW Share for now */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & BIT(i))
			bw_data.tc_bw_share_credits[i] = 1;
	}

	ret = i40e_aq_config_switch_comp_bw_config(&pf->hw, veb->seid,
						   &bw_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "VEB bw config failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		goto out;
	}

	/* Update the BW information */
	ret = i40e_veb_get_bw_info(veb);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Failed getting veb bw config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	}

out:
	return ret;
}

#ifdef CONFIG_I40E_DCB
/**
 * i40e_dcb_reconfigure - Reconfigure all VEBs and VSIs
 * @pf: PF struct
 *
 * Reconfigure VEB/VSIs on a given PF; it is assumed that
 * the caller would've quiesce all the VSIs before calling
 * this function
 **/
static void i40e_dcb_reconfigure(struct i40e_pf *pf)
{
	u8 tc_map = 0;
	int ret;
	u8 v;

	/* Enable the TCs available on PF to all VEBs */
	tc_map = i40e_pf_get_tc_map(pf);
	for (v = 0; v < I40E_MAX_VEB; v++) {
		if (!pf->veb[v])
			continue;
		ret = i40e_veb_config_tc(pf->veb[v], tc_map);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Failed configuring TC for VEB seid=%d\n",
				 pf->veb[v]->seid);
			/* Will try to configure as many components */
		}
	}

	/* Update each VSI */
	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (!pf->vsi[v])
			continue;

		/* - Enable all TCs for the LAN VSI
		 * - For all others keep them at TC0 for now
		 */
		if (v == pf->lan_vsi)
			tc_map = i40e_pf_get_tc_map(pf);
		else
			tc_map = I40E_DEFAULT_TRAFFIC_CLASS;

		ret = i40e_vsi_config_tc(pf->vsi[v], tc_map);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Failed configuring TC for VSI seid=%d\n",
				 pf->vsi[v]->seid);
			/* Will try to configure as many components */
		} else {
			/* Re-configure VSI vectors based on updated TC map */
			i40e_vsi_map_rings_to_vectors(pf->vsi[v]);
			if (pf->vsi[v]->netdev)
				i40e_dcbnl_set_all(pf->vsi[v]);
		}
	}
}

/**
 * i40e_resume_port_tx - Resume port Tx
 * @pf: PF struct
 *
 * Resume a port's Tx and issue a PF reset in case of failure to
 * resume.
 **/
static int i40e_resume_port_tx(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int ret;

	ret = i40e_aq_resume_port_tx(hw, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Resume Port Tx failed, err %s aq_err %s\n",
			  i40e_stat_str(&pf->hw, ret),
			  i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		/* Schedule PF reset to recover */
		set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
		i40e_service_event_schedule(pf);
	}

	return ret;
}

/**
 * i40e_init_pf_dcb - Initialize DCB configuration
 * @pf: PF being configured
 *
 * Query the current DCB configuration and cache it
 * in the hardware structure
 **/
static int i40e_init_pf_dcb(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int err = 0;

	/* Do not enable DCB for SW1 and SW2 images even if the FW is capable
	 * Also do not enable DCBx if FW LLDP agent is disabled
	 */
	if ((pf->hw_features & I40E_HW_NO_DCB_SUPPORT) ||
	    (pf->flags & I40E_FLAG_DISABLE_FW_LLDP)) {
		dev_info(&pf->pdev->dev, "DCB is not supported or FW LLDP is disabled\n");
		err = I40E_NOT_SUPPORTED;
		goto out;
	}

	err = i40e_init_dcb(hw, true);
	if (!err) {
		/* Device/Function is not DCBX capable */
		if ((!hw->func_caps.dcb) ||
		    (hw->dcbx_status == I40E_DCBX_STATUS_DISABLED)) {
			dev_info(&pf->pdev->dev,
				 "DCBX offload is not supported or is disabled for this PF.\n");
		} else {
			/* When status is not DISABLED then DCBX in FW */
			pf->dcbx_cap = DCB_CAP_DCBX_LLD_MANAGED |
				       DCB_CAP_DCBX_VER_IEEE;

			pf->flags |= I40E_FLAG_DCB_CAPABLE;
			/* Enable DCB tagging only when more than one TC
			 * or explicitly disable if only one TC
			 */
			if (i40e_dcb_get_num_tc(&hw->local_dcbx_config) > 1)
				pf->flags |= I40E_FLAG_DCB_ENABLED;
			else
				pf->flags &= ~I40E_FLAG_DCB_ENABLED;
			dev_dbg(&pf->pdev->dev,
				"DCBX offload is supported for this PF.\n");
		}
	} else if (pf->hw.aq.asq_last_status == I40E_AQ_RC_EPERM) {
		dev_info(&pf->pdev->dev, "FW LLDP disabled for this PF.\n");
		pf->flags |= I40E_FLAG_DISABLE_FW_LLDP;
	} else {
		dev_info(&pf->pdev->dev,
			 "Query for DCB configuration failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, err),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	}

out:
	return err;
}
#endif /* CONFIG_I40E_DCB */

/**
 * i40e_print_link_message - print link up or down
 * @vsi: the VSI for which link needs a message
 * @isup: true of link is up, false otherwise
 */
void i40e_print_link_message(struct i40e_vsi *vsi, bool isup)
{
	enum i40e_aq_link_speed new_speed;
	struct i40e_pf *pf = vsi->back;
	char *speed = "Unknown";
	char *fc = "Unknown";
	char *fec = "";
	char *req_fec = "";
	char *an = "";

	if (isup)
		new_speed = pf->hw.phy.link_info.link_speed;
	else
		new_speed = I40E_LINK_SPEED_UNKNOWN;

	if ((vsi->current_isup == isup) && (vsi->current_speed == new_speed))
		return;
	vsi->current_isup = isup;
	vsi->current_speed = new_speed;
	if (!isup) {
		netdev_info(vsi->netdev, "NIC Link is Down\n");
		return;
	}

	/* Warn user if link speed on NPAR enabled partition is not at
	 * least 10GB
	 */
	if (pf->hw.func_caps.npar_enable &&
	    (pf->hw.phy.link_info.link_speed == I40E_LINK_SPEED_1GB ||
	     pf->hw.phy.link_info.link_speed == I40E_LINK_SPEED_100MB))
		netdev_warn(vsi->netdev,
			    "The partition detected link speed that is less than 10Gbps\n");

	switch (pf->hw.phy.link_info.link_speed) {
	case I40E_LINK_SPEED_40GB:
		speed = "40 G";
		break;
	case I40E_LINK_SPEED_20GB:
		speed = "20 G";
		break;
	case I40E_LINK_SPEED_25GB:
		speed = "25 G";
		break;
	case I40E_LINK_SPEED_10GB:
		speed = "10 G";
		break;
	case I40E_LINK_SPEED_5GB:
		speed = "5 G";
		break;
	case I40E_LINK_SPEED_2_5GB:
		speed = "2.5 G";
		break;
	case I40E_LINK_SPEED_1GB:
		speed = "1000 M";
		break;
	case I40E_LINK_SPEED_100MB:
		speed = "100 M";
		break;
	default:
		break;
	}

	switch (pf->hw.fc.current_mode) {
	case I40E_FC_FULL:
		fc = "RX/TX";
		break;
	case I40E_FC_TX_PAUSE:
		fc = "TX";
		break;
	case I40E_FC_RX_PAUSE:
		fc = "RX";
		break;
	default:
		fc = "None";
		break;
	}

	if (pf->hw.phy.link_info.link_speed == I40E_LINK_SPEED_25GB) {
		req_fec = "None";
		fec = "None";
		an = "False";

		if (pf->hw.phy.link_info.an_info & I40E_AQ_AN_COMPLETED)
			an = "True";

		if (pf->hw.phy.link_info.fec_info &
		    I40E_AQ_CONFIG_FEC_KR_ENA)
			fec = "CL74 FC-FEC/BASE-R";
		else if (pf->hw.phy.link_info.fec_info &
			 I40E_AQ_CONFIG_FEC_RS_ENA)
			fec = "CL108 RS-FEC";

		/* 'CL108 RS-FEC' should be displayed when RS is requested, or
		 * both RS and FC are requested
		 */
		if (vsi->back->hw.phy.link_info.req_fec_info &
		    (I40E_AQ_REQUEST_FEC_KR | I40E_AQ_REQUEST_FEC_RS)) {
			if (vsi->back->hw.phy.link_info.req_fec_info &
			    I40E_AQ_REQUEST_FEC_RS)
				req_fec = "CL108 RS-FEC";
			else
				req_fec = "CL74 FC-FEC/BASE-R";
		}
		netdev_info(vsi->netdev,
			    "NIC Link is Up, %sbps Full Duplex, Requested FEC: %s, Negotiated FEC: %s, Autoneg: %s, Flow Control: %s\n",
			    speed, req_fec, fec, an, fc);
	} else if (pf->hw.device_id == I40E_DEV_ID_KX_X722) {
		req_fec = "None";
		fec = "None";
		an = "False";

		if (pf->hw.phy.link_info.an_info & I40E_AQ_AN_COMPLETED)
			an = "True";

		if (pf->hw.phy.link_info.fec_info &
		    I40E_AQ_CONFIG_FEC_KR_ENA)
			fec = "CL74 FC-FEC/BASE-R";

		if (pf->hw.phy.link_info.req_fec_info &
		    I40E_AQ_REQUEST_FEC_KR)
			req_fec = "CL74 FC-FEC/BASE-R";

		netdev_info(vsi->netdev,
			    "NIC Link is Up, %sbps Full Duplex, Requested FEC: %s, Negotiated FEC: %s, Autoneg: %s, Flow Control: %s\n",
			    speed, req_fec, fec, an, fc);
	} else {
		netdev_info(vsi->netdev,
			    "NIC Link is Up, %sbps Full Duplex, Flow Control: %s\n",
			    speed, fc);
	}

}

/**
 * i40e_up_complete - Finish the last steps of bringing up a connection
 * @vsi: the VSI being configured
 **/
static int i40e_up_complete(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int err;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		i40e_vsi_configure_msix(vsi);
	else
		i40e_configure_msi_and_legacy(vsi);

	/* start rings */
	err = i40e_vsi_start_rings(vsi);
	if (err)
		return err;

	clear_bit(__I40E_VSI_DOWN, vsi->state);
	i40e_napi_enable_all(vsi);
	i40e_vsi_enable_irq(vsi);

	if ((pf->hw.phy.link_info.link_info & I40E_AQ_LINK_UP) &&
	    (vsi->netdev)) {
		i40e_print_link_message(vsi, true);
		netif_tx_start_all_queues(vsi->netdev);
		netif_carrier_on(vsi->netdev);
	}

	/* replay FDIR SB filters */
	if (vsi->type == I40E_VSI_FDIR) {
		/* reset fd counters */
		pf->fd_add_err = 0;
		pf->fd_atr_cnt = 0;
		i40e_fdir_filter_restore(vsi);
	}

	/* On the next run of the service_task, notify any clients of the new
	 * opened netdev
	 */
	set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
	i40e_service_event_schedule(pf);

	return 0;
}

/**
 * i40e_vsi_reinit_locked - Reset the VSI
 * @vsi: the VSI being configured
 *
 * Rebuild the ring structs after some configuration
 * has changed, e.g. MTU size.
 **/
static void i40e_vsi_reinit_locked(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state))
		usleep_range(1000, 2000);
	i40e_down(vsi);

	i40e_up(vsi);
	clear_bit(__I40E_CONFIG_BUSY, pf->state);
}

/**
 * i40e_force_link_state - Force the link status
 * @pf: board private structure
 * @is_up: whether the link state should be forced up or down
 **/
static i40e_status i40e_force_link_state(struct i40e_pf *pf, bool is_up)
{
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_aq_set_phy_config config = {0};
	bool non_zero_phy_type = is_up;
	struct i40e_hw *hw = &pf->hw;
	i40e_status err;
	u64 mask;
	u8 speed;

	/* Card might've been put in an unstable state by other drivers
	 * and applications, which causes incorrect speed values being
	 * set on startup. In order to clear speed registers, we call
	 * get_phy_capabilities twice, once to get initial state of
	 * available speeds, and once to get current PHY config.
	 */
	err = i40e_aq_get_phy_capabilities(hw, false, true, &abilities,
					   NULL);
	if (err) {
		dev_err(&pf->pdev->dev,
			"failed to get phy cap., ret =  %s last_status =  %s\n",
			i40e_stat_str(hw, err),
			i40e_aq_str(hw, hw->aq.asq_last_status));
		return err;
	}
	speed = abilities.link_speed;

	/* Get the current phy config */
	err = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					   NULL);
	if (err) {
		dev_err(&pf->pdev->dev,
			"failed to get phy cap., ret =  %s last_status =  %s\n",
			i40e_stat_str(hw, err),
			i40e_aq_str(hw, hw->aq.asq_last_status));
		return err;
	}

	/* If link needs to go up, but was not forced to go down,
	 * and its speed values are OK, no need for a flap
	 * if non_zero_phy_type was set, still need to force up
	 */
	if (pf->flags & I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED)
		non_zero_phy_type = true;
	else if (is_up && abilities.phy_type != 0 && abilities.link_speed != 0)
		return I40E_SUCCESS;

	/* To force link we need to set bits for all supported PHY types,
	 * but there are now more than 32, so we need to split the bitmap
	 * across two fields.
	 */
	mask = I40E_PHY_TYPES_BITMASK;
	config.phy_type =
		non_zero_phy_type ? cpu_to_le32((u32)(mask & 0xffffffff)) : 0;
	config.phy_type_ext =
		non_zero_phy_type ? (u8)((mask >> 32) & 0xff) : 0;
	/* Copy the old settings, except of phy_type */
	config.abilities = abilities.abilities;
	if (pf->flags & I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED) {
		if (is_up)
			config.abilities |= I40E_AQ_PHY_ENABLE_LINK;
		else
			config.abilities &= ~(I40E_AQ_PHY_ENABLE_LINK);
	}
	if (abilities.link_speed != 0)
		config.link_speed = abilities.link_speed;
	else
		config.link_speed = speed;
	config.eee_capability = abilities.eee_capability;
	config.eeer = abilities.eeer_val;
	config.low_power_ctrl = abilities.d3_lpan;
	config.fec_config = abilities.fec_cfg_curr_mod_ext_info &
			    I40E_AQ_PHY_FEC_CONFIG_MASK;
	err = i40e_aq_set_phy_config(hw, &config, NULL);

	if (err) {
		dev_err(&pf->pdev->dev,
			"set phy config ret =  %s last_status =  %s\n",
			i40e_stat_str(&pf->hw, err),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return err;
	}

	/* Update the link info */
	err = i40e_update_link_info(hw);
	if (err) {
		/* Wait a little bit (on 40G cards it sometimes takes a really
		 * long time for link to come back from the atomic reset)
		 * and try once more
		 */
		msleep(1000);
		i40e_update_link_info(hw);
	}

	i40e_aq_set_link_restart_an(hw, is_up, NULL);

	return I40E_SUCCESS;
}

/**
 * i40e_up - Bring the connection back up after being down
 * @vsi: the VSI being configured
 **/
int i40e_up(struct i40e_vsi *vsi)
{
	int err;

	if (vsi->type == I40E_VSI_MAIN &&
	    (vsi->back->flags & I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED ||
	     vsi->back->flags & I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED))
		i40e_force_link_state(vsi->back, true);

	err = i40e_vsi_configure(vsi);
	if (!err)
		err = i40e_up_complete(vsi);

	return err;
}

/**
 * i40e_down - Shutdown the connection processing
 * @vsi: the VSI being stopped
 **/
void i40e_down(struct i40e_vsi *vsi)
{
	int i;

	/* It is assumed that the caller of this function
	 * sets the vsi->state __I40E_VSI_DOWN bit.
	 */
	if (vsi->netdev) {
		netif_carrier_off(vsi->netdev);
		netif_tx_disable(vsi->netdev);
	}
	i40e_vsi_disable_irq(vsi);
	i40e_vsi_stop_rings(vsi);
	if (vsi->type == I40E_VSI_MAIN &&
	   (vsi->back->flags & I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED ||
	    vsi->back->flags & I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED))
		i40e_force_link_state(vsi->back, false);
	i40e_napi_disable_all(vsi);

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		i40e_clean_tx_ring(vsi->tx_rings[i]);
		if (i40e_enabled_xdp_vsi(vsi)) {
			/* Make sure that in-progress ndo_xdp_xmit and
			 * ndo_xsk_wakeup calls are completed.
			 */
			synchronize_rcu();
			i40e_clean_tx_ring(vsi->xdp_rings[i]);
		}
		i40e_clean_rx_ring(vsi->rx_rings[i]);
	}

}

/**
 * i40e_validate_mqprio_qopt- validate queue mapping info
 * @vsi: the VSI being configured
 * @mqprio_qopt: queue parametrs
 **/
static int i40e_validate_mqprio_qopt(struct i40e_vsi *vsi,
				     struct tc_mqprio_qopt_offload *mqprio_qopt)
{
	u64 sum_max_rate = 0;
	u64 max_rate = 0;
	int i;

	if (mqprio_qopt->qopt.offset[0] != 0 ||
	    mqprio_qopt->qopt.num_tc < 1 ||
	    mqprio_qopt->qopt.num_tc > I40E_MAX_TRAFFIC_CLASS)
		return -EINVAL;
	for (i = 0; ; i++) {
		if (!mqprio_qopt->qopt.count[i])
			return -EINVAL;
		if (mqprio_qopt->min_rate[i]) {
			dev_err(&vsi->back->pdev->dev,
				"Invalid min tx rate (greater than 0) specified\n");
			return -EINVAL;
		}
		max_rate = mqprio_qopt->max_rate[i];
		do_div(max_rate, I40E_BW_MBPS_DIVISOR);
		sum_max_rate += max_rate;

		if (i >= mqprio_qopt->qopt.num_tc - 1)
			break;
		if (mqprio_qopt->qopt.offset[i + 1] !=
		    (mqprio_qopt->qopt.offset[i] + mqprio_qopt->qopt.count[i]))
			return -EINVAL;
	}
	if (vsi->num_queue_pairs <
	    (mqprio_qopt->qopt.offset[i] + mqprio_qopt->qopt.count[i])) {
		dev_err(&vsi->back->pdev->dev,
			"Failed to create traffic channel, insufficient number of queues.\n");
		return -EINVAL;
	}
	if (sum_max_rate > i40e_get_link_speed(vsi)) {
		dev_err(&vsi->back->pdev->dev,
			"Invalid max tx rate specified\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * i40e_vsi_set_default_tc_config - set default values for tc configuration
 * @vsi: the VSI being configured
 **/
static void i40e_vsi_set_default_tc_config(struct i40e_vsi *vsi)
{
	u16 qcount;
	int i;

	/* Only TC0 is enabled */
	vsi->tc_config.numtc = 1;
	vsi->tc_config.enabled_tc = 1;
	qcount = min_t(int, vsi->alloc_queue_pairs,
		       i40e_pf_get_max_q_per_tc(vsi->back));
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* For the TC that is not enabled set the offset to to default
		 * queue and allocate one queue for the given TC.
		 */
		vsi->tc_config.tc_info[i].qoffset = 0;
		if (i == 0)
			vsi->tc_config.tc_info[i].qcount = qcount;
		else
			vsi->tc_config.tc_info[i].qcount = 1;
		vsi->tc_config.tc_info[i].netdev_tc = 0;
	}
}

/**
 * i40e_del_macvlan_filter
 * @hw: pointer to the HW structure
 * @seid: seid of the channel VSI
 * @macaddr: the mac address to apply as a filter
 * @aq_err: store the admin Q error
 *
 * This function deletes a mac filter on the channel VSI which serves as the
 * macvlan. Returns 0 on success.
 **/
static i40e_status i40e_del_macvlan_filter(struct i40e_hw *hw, u16 seid,
					   const u8 *macaddr, int *aq_err)
{
	struct i40e_aqc_remove_macvlan_element_data element;
	i40e_status status;

	memset(&element, 0, sizeof(element));
	ether_addr_copy(element.mac_addr, macaddr);
	element.vlan_tag = 0;
	element.flags = I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
	status = i40e_aq_remove_macvlan(hw, seid, &element, 1, NULL);
	*aq_err = hw->aq.asq_last_status;

	return status;
}

/**
 * i40e_add_macvlan_filter
 * @hw: pointer to the HW structure
 * @seid: seid of the channel VSI
 * @macaddr: the mac address to apply as a filter
 * @aq_err: store the admin Q error
 *
 * This function adds a mac filter on the channel VSI which serves as the
 * macvlan. Returns 0 on success.
 **/
static i40e_status i40e_add_macvlan_filter(struct i40e_hw *hw, u16 seid,
					   const u8 *macaddr, int *aq_err)
{
	struct i40e_aqc_add_macvlan_element_data element;
	i40e_status status;
	u16 cmd_flags = 0;

	ether_addr_copy(element.mac_addr, macaddr);
	element.vlan_tag = 0;
	element.queue_number = 0;
	element.match_method = I40E_AQC_MM_ERR_NO_RES;
	cmd_flags |= I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
	element.flags = cpu_to_le16(cmd_flags);
	status = i40e_aq_add_macvlan(hw, seid, &element, 1, NULL);
	*aq_err = hw->aq.asq_last_status;

	return status;
}

/**
 * i40e_reset_ch_rings - Reset the queue contexts in a channel
 * @vsi: the VSI we want to access
 * @ch: the channel we want to access
 */
static void i40e_reset_ch_rings(struct i40e_vsi *vsi, struct i40e_channel *ch)
{
	struct i40e_ring *tx_ring, *rx_ring;
	u16 pf_q;
	int i;

	for (i = 0; i < ch->num_queue_pairs; i++) {
		pf_q = ch->base_queue + i;
		tx_ring = vsi->tx_rings[pf_q];
		tx_ring->ch = NULL;
		rx_ring = vsi->rx_rings[pf_q];
		rx_ring->ch = NULL;
	}
}

/**
 * i40e_free_macvlan_channels
 * @vsi: the VSI we want to access
 *
 * This function frees the Qs of the channel VSI from
 * the stack and also deletes the channel VSIs which
 * serve as macvlans.
 */
static void i40e_free_macvlan_channels(struct i40e_vsi *vsi)
{
	struct i40e_channel *ch, *ch_tmp;
	int ret;

	if (list_empty(&vsi->macvlan_list))
		return;

	list_for_each_entry_safe(ch, ch_tmp, &vsi->macvlan_list, list) {
		struct i40e_vsi *parent_vsi;

		if (i40e_is_channel_macvlan(ch)) {
			i40e_reset_ch_rings(vsi, ch);
			clear_bit(ch->fwd->bit_no, vsi->fwd_bitmask);
			netdev_unbind_sb_channel(vsi->netdev, ch->fwd->netdev);
			netdev_set_sb_channel(ch->fwd->netdev, 0);
			kfree(ch->fwd);
			ch->fwd = NULL;
		}

		list_del(&ch->list);
		parent_vsi = ch->parent_vsi;
		if (!parent_vsi || !ch->initialized) {
			kfree(ch);
			continue;
		}

		/* remove the VSI */
		ret = i40e_aq_delete_element(&vsi->back->hw, ch->seid,
					     NULL);
		if (ret)
			dev_err(&vsi->back->pdev->dev,
				"unable to remove channel (%d) for parent VSI(%d)\n",
				ch->seid, parent_vsi->seid);
		kfree(ch);
	}
	vsi->macvlan_cnt = 0;
}

/**
 * i40e_fwd_ring_up - bring the macvlan device up
 * @vsi: the VSI we want to access
 * @vdev: macvlan netdevice
 * @fwd: the private fwd structure
 */
static int i40e_fwd_ring_up(struct i40e_vsi *vsi, struct net_device *vdev,
			    struct i40e_fwd_adapter *fwd)
{
	int ret = 0, num_tc = 1,  i, aq_err;
	struct i40e_channel *ch, *ch_tmp;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;

	if (list_empty(&vsi->macvlan_list))
		return -EINVAL;

	/* Go through the list and find an available channel */
	list_for_each_entry_safe(ch, ch_tmp, &vsi->macvlan_list, list) {
		if (!i40e_is_channel_macvlan(ch)) {
			ch->fwd = fwd;
			/* record configuration for macvlan interface in vdev */
			for (i = 0; i < num_tc; i++)
				netdev_bind_sb_channel_queue(vsi->netdev, vdev,
							     i,
							     ch->num_queue_pairs,
							     ch->base_queue);
			for (i = 0; i < ch->num_queue_pairs; i++) {
				struct i40e_ring *tx_ring, *rx_ring;
				u16 pf_q;

				pf_q = ch->base_queue + i;

				/* Get to TX ring ptr */
				tx_ring = vsi->tx_rings[pf_q];
				tx_ring->ch = ch;

				/* Get the RX ring ptr */
				rx_ring = vsi->rx_rings[pf_q];
				rx_ring->ch = ch;
			}
			break;
		}
	}

	/* Guarantee all rings are updated before we update the
	 * MAC address filter.
	 */
	wmb();

	/* Add a mac filter */
	ret = i40e_add_macvlan_filter(hw, ch->seid, vdev->dev_addr, &aq_err);
	if (ret) {
		/* if we cannot add the MAC rule then disable the offload */
		macvlan_release_l2fw_offload(vdev);
		for (i = 0; i < ch->num_queue_pairs; i++) {
			struct i40e_ring *rx_ring;
			u16 pf_q;

			pf_q = ch->base_queue + i;
			rx_ring = vsi->rx_rings[pf_q];
			rx_ring->netdev = NULL;
		}
		dev_info(&pf->pdev->dev,
			 "Error adding mac filter on macvlan err %s, aq_err %s\n",
			  i40e_stat_str(hw, ret),
			  i40e_aq_str(hw, aq_err));
		netdev_err(vdev, "L2fwd offload disabled to L2 filter error\n");
	}

	return ret;
}

/**
 * i40e_setup_macvlans - create the channels which will be macvlans
 * @vsi: the VSI we want to access
 * @macvlan_cnt: no. of macvlans to be setup
 * @qcnt: no. of Qs per macvlan
 * @vdev: macvlan netdevice
 */
static int i40e_setup_macvlans(struct i40e_vsi *vsi, u16 macvlan_cnt, u16 qcnt,
			       struct net_device *vdev)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	u16 sections, qmap, num_qps;
	struct i40e_channel *ch;
	int i, pow, ret = 0;
	u8 offset = 0;

	if (vsi->type != I40E_VSI_MAIN || !macvlan_cnt)
		return -EINVAL;

	num_qps = vsi->num_queue_pairs - (macvlan_cnt * qcnt);

	/* find the next higher power-of-2 of num queue pairs */
	pow = fls(roundup_pow_of_two(num_qps) - 1);

	qmap = (offset << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
		(pow << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT);

	/* Setup context bits for the main VSI */
	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	sections |= I40E_AQ_VSI_PROP_SCHED_VALID;
	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.seid = vsi->seid;
	ctxt.pf_num = vsi->back->hw.pf_id;
	ctxt.vf_num = 0;
	ctxt.uplink_seid = vsi->uplink_seid;
	ctxt.info = vsi->info;
	ctxt.info.tc_mapping[0] = cpu_to_le16(qmap);
	ctxt.info.mapping_flags |= cpu_to_le16(I40E_AQ_VSI_QUE_MAP_CONTIG);
	ctxt.info.queue_mapping[0] = cpu_to_le16(vsi->base_queue);
	ctxt.info.valid_sections |= cpu_to_le16(sections);

	/* Reconfigure RSS for main VSI with new max queue count */
	vsi->rss_size = max_t(u16, num_qps, qcnt);
	ret = i40e_vsi_config_rss(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Failed to reconfig RSS for num_queues (%u)\n",
			 vsi->rss_size);
		return ret;
	}
	vsi->reconfig_rss = true;
	dev_dbg(&vsi->back->pdev->dev,
		"Reconfigured RSS with num_queues (%u)\n", vsi->rss_size);
	vsi->next_base_queue = num_qps;
	vsi->cnt_q_avail = vsi->num_queue_pairs - num_qps;

	/* Update the VSI after updating the VSI queue-mapping
	 * information
	 */
	ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Update vsi tc config failed, err %s aq_err %s\n",
			 i40e_stat_str(hw, ret),
			 i40e_aq_str(hw, hw->aq.asq_last_status));
		return ret;
	}
	/* update the local VSI info with updated queue map */
	i40e_vsi_update_queue_map(vsi, &ctxt);
	vsi->info.valid_sections = 0;

	/* Create channels for macvlans */
	INIT_LIST_HEAD(&vsi->macvlan_list);
	for (i = 0; i < macvlan_cnt; i++) {
		ch = kzalloc(sizeof(*ch), GFP_KERNEL);
		if (!ch) {
			ret = -ENOMEM;
			goto err_free;
		}
		INIT_LIST_HEAD(&ch->list);
		ch->num_queue_pairs = qcnt;
		if (!i40e_setup_channel(pf, vsi, ch)) {
			ret = -EINVAL;
			kfree(ch);
			goto err_free;
		}
		ch->parent_vsi = vsi;
		vsi->cnt_q_avail -= ch->num_queue_pairs;
		vsi->macvlan_cnt++;
		list_add_tail(&ch->list, &vsi->macvlan_list);
	}

	return ret;

err_free:
	dev_info(&pf->pdev->dev, "Failed to setup macvlans\n");
	i40e_free_macvlan_channels(vsi);

	return ret;
}

/**
 * i40e_fwd_add - configure macvlans
 * @netdev: net device to configure
 * @vdev: macvlan netdevice
 **/
static void *i40e_fwd_add(struct net_device *netdev, struct net_device *vdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	u16 q_per_macvlan = 0, macvlan_cnt = 0, vectors;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_fwd_adapter *fwd;
	int avail_macvlan, ret;

	if ((pf->flags & I40E_FLAG_DCB_ENABLED)) {
		netdev_info(netdev, "Macvlans are not supported when DCB is enabled\n");
		return ERR_PTR(-EINVAL);
	}
	if ((pf->flags & I40E_FLAG_TC_MQPRIO)) {
		netdev_info(netdev, "Macvlans are not supported when HW TC offload is on\n");
		return ERR_PTR(-EINVAL);
	}
	if (pf->num_lan_msix < I40E_MIN_MACVLAN_VECTORS) {
		netdev_info(netdev, "Not enough vectors available to support macvlans\n");
		return ERR_PTR(-EINVAL);
	}

	/* The macvlan device has to be a single Q device so that the
	 * tc_to_txq field can be reused to pick the tx queue.
	 */
	if (netif_is_multiqueue(vdev))
		return ERR_PTR(-ERANGE);

	if (!vsi->macvlan_cnt) {
		/* reserve bit 0 for the pf device */
		set_bit(0, vsi->fwd_bitmask);

		/* Try to reserve as many queues as possible for macvlans. First
		 * reserve 3/4th of max vectors, then half, then quarter and
		 * calculate Qs per macvlan as you go
		 */
		vectors = pf->num_lan_msix;
		if (vectors <= I40E_MAX_MACVLANS && vectors > 64) {
			/* allocate 4 Qs per macvlan and 32 Qs to the PF*/
			q_per_macvlan = 4;
			macvlan_cnt = (vectors - 32) / 4;
		} else if (vectors <= 64 && vectors > 32) {
			/* allocate 2 Qs per macvlan and 16 Qs to the PF*/
			q_per_macvlan = 2;
			macvlan_cnt = (vectors - 16) / 2;
		} else if (vectors <= 32 && vectors > 16) {
			/* allocate 1 Q per macvlan and 16 Qs to the PF*/
			q_per_macvlan = 1;
			macvlan_cnt = vectors - 16;
		} else if (vectors <= 16 && vectors > 8) {
			/* allocate 1 Q per macvlan and 8 Qs to the PF */
			q_per_macvlan = 1;
			macvlan_cnt = vectors - 8;
		} else {
			/* allocate 1 Q per macvlan and 1 Q to the PF */
			q_per_macvlan = 1;
			macvlan_cnt = vectors - 1;
		}

		if (macvlan_cnt == 0)
			return ERR_PTR(-EBUSY);

		/* Quiesce VSI queues */
		i40e_quiesce_vsi(vsi);

		/* sets up the macvlans but does not "enable" them */
		ret = i40e_setup_macvlans(vsi, macvlan_cnt, q_per_macvlan,
					  vdev);
		if (ret)
			return ERR_PTR(ret);

		/* Unquiesce VSI */
		i40e_unquiesce_vsi(vsi);
	}
	avail_macvlan = find_first_zero_bit(vsi->fwd_bitmask,
					    vsi->macvlan_cnt);
	if (avail_macvlan >= I40E_MAX_MACVLANS)
		return ERR_PTR(-EBUSY);

	/* create the fwd struct */
	fwd = kzalloc(sizeof(*fwd), GFP_KERNEL);
	if (!fwd)
		return ERR_PTR(-ENOMEM);

	set_bit(avail_macvlan, vsi->fwd_bitmask);
	fwd->bit_no = avail_macvlan;
	netdev_set_sb_channel(vdev, avail_macvlan);
	fwd->netdev = vdev;

	if (!netif_running(netdev))
		return fwd;

	/* Set fwd ring up */
	ret = i40e_fwd_ring_up(vsi, vdev, fwd);
	if (ret) {
		/* unbind the queues and drop the subordinate channel config */
		netdev_unbind_sb_channel(netdev, vdev);
		netdev_set_sb_channel(vdev, 0);

		kfree(fwd);
		return ERR_PTR(-EINVAL);
	}

	return fwd;
}

/**
 * i40e_del_all_macvlans - Delete all the mac filters on the channels
 * @vsi: the VSI we want to access
 */
static void i40e_del_all_macvlans(struct i40e_vsi *vsi)
{
	struct i40e_channel *ch, *ch_tmp;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int aq_err, ret = 0;

	if (list_empty(&vsi->macvlan_list))
		return;

	list_for_each_entry_safe(ch, ch_tmp, &vsi->macvlan_list, list) {
		if (i40e_is_channel_macvlan(ch)) {
			ret = i40e_del_macvlan_filter(hw, ch->seid,
						      i40e_channel_mac(ch),
						      &aq_err);
			if (!ret) {
				/* Reset queue contexts */
				i40e_reset_ch_rings(vsi, ch);
				clear_bit(ch->fwd->bit_no, vsi->fwd_bitmask);
				netdev_unbind_sb_channel(vsi->netdev,
							 ch->fwd->netdev);
				netdev_set_sb_channel(ch->fwd->netdev, 0);
				kfree(ch->fwd);
				ch->fwd = NULL;
			}
		}
	}
}

/**
 * i40e_fwd_del - delete macvlan interfaces
 * @netdev: net device to configure
 * @vdev: macvlan netdevice
 */
static void i40e_fwd_del(struct net_device *netdev, void *vdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_fwd_adapter *fwd = vdev;
	struct i40e_channel *ch, *ch_tmp;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int aq_err, ret = 0;

	/* Find the channel associated with the macvlan and del mac filter */
	list_for_each_entry_safe(ch, ch_tmp, &vsi->macvlan_list, list) {
		if (i40e_is_channel_macvlan(ch) &&
		    ether_addr_equal(i40e_channel_mac(ch),
				     fwd->netdev->dev_addr)) {
			ret = i40e_del_macvlan_filter(hw, ch->seid,
						      i40e_channel_mac(ch),
						      &aq_err);
			if (!ret) {
				/* Reset queue contexts */
				i40e_reset_ch_rings(vsi, ch);
				clear_bit(ch->fwd->bit_no, vsi->fwd_bitmask);
				netdev_unbind_sb_channel(netdev, fwd->netdev);
				netdev_set_sb_channel(fwd->netdev, 0);
				kfree(ch->fwd);
				ch->fwd = NULL;
			} else {
				dev_info(&pf->pdev->dev,
					 "Error deleting mac filter on macvlan err %s, aq_err %s\n",
					  i40e_stat_str(hw, ret),
					  i40e_aq_str(hw, aq_err));
			}
			break;
		}
	}
}

/**
 * i40e_setup_tc - configure multiple traffic classes
 * @netdev: net device to configure
 * @type_data: tc offload data
 **/
static int i40e_setup_tc(struct net_device *netdev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio_qopt = type_data;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u8 enabled_tc = 0, num_tc, hw;
	bool need_reset = false;
	int old_queue_pairs;
	int ret = -EINVAL;
	u16 mode;
	int i;

	old_queue_pairs = vsi->num_queue_pairs;
	num_tc = mqprio_qopt->qopt.num_tc;
	hw = mqprio_qopt->qopt.hw;
	mode = mqprio_qopt->mode;
	if (!hw) {
		pf->flags &= ~I40E_FLAG_TC_MQPRIO;
		memcpy(&vsi->mqprio_qopt, mqprio_qopt, sizeof(*mqprio_qopt));
		goto config_tc;
	}

	/* Check if MFP enabled */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		netdev_info(netdev,
			    "Configuring TC not supported in MFP mode\n");
		return ret;
	}
	switch (mode) {
	case TC_MQPRIO_MODE_DCB:
		pf->flags &= ~I40E_FLAG_TC_MQPRIO;

		/* Check if DCB enabled to continue */
		if (!(pf->flags & I40E_FLAG_DCB_ENABLED)) {
			netdev_info(netdev,
				    "DCB is not enabled for adapter\n");
			return ret;
		}

		/* Check whether tc count is within enabled limit */
		if (num_tc > i40e_pf_get_num_tc(pf)) {
			netdev_info(netdev,
				    "TC count greater than enabled on link for adapter\n");
			return ret;
		}
		break;
	case TC_MQPRIO_MODE_CHANNEL:
		if (pf->flags & I40E_FLAG_DCB_ENABLED) {
			netdev_info(netdev,
				    "Full offload of TC Mqprio options is not supported when DCB is enabled\n");
			return ret;
		}
		if (!(pf->flags & I40E_FLAG_MSIX_ENABLED))
			return ret;
		ret = i40e_validate_mqprio_qopt(vsi, mqprio_qopt);
		if (ret)
			return ret;
		memcpy(&vsi->mqprio_qopt, mqprio_qopt,
		       sizeof(*mqprio_qopt));
		pf->flags |= I40E_FLAG_TC_MQPRIO;
		pf->flags &= ~I40E_FLAG_DCB_ENABLED;
		break;
	default:
		return -EINVAL;
	}

config_tc:
	/* Generate TC map for number of tc requested */
	for (i = 0; i < num_tc; i++)
		enabled_tc |= BIT(i);

	/* Requesting same TC configuration as already enabled */
	if (enabled_tc == vsi->tc_config.enabled_tc &&
	    mode != TC_MQPRIO_MODE_CHANNEL)
		return 0;

	/* Quiesce VSI queues */
	i40e_quiesce_vsi(vsi);

	if (!hw && !(pf->flags & I40E_FLAG_TC_MQPRIO))
		i40e_remove_queue_channels(vsi);

	/* Configure VSI for enabled TCs */
	ret = i40e_vsi_config_tc(vsi, enabled_tc);
	if (ret) {
		netdev_info(netdev, "Failed configuring TC for VSI seid=%d\n",
			    vsi->seid);
		need_reset = true;
		goto exit;
	} else if (enabled_tc &&
		   (!is_power_of_2(vsi->tc_config.tc_info[0].qcount))) {
		netdev_info(netdev,
			    "Failed to create channel. Override queues (%u) not power of 2\n",
			    vsi->tc_config.tc_info[0].qcount);
		ret = -EINVAL;
		need_reset = true;
		goto exit;
	}

	dev_info(&vsi->back->pdev->dev,
		 "Setup channel (id:%u) utilizing num_queues %d\n",
		 vsi->seid, vsi->tc_config.tc_info[0].qcount);

	if (pf->flags & I40E_FLAG_TC_MQPRIO) {
		if (vsi->mqprio_qopt.max_rate[0]) {
			u64 max_tx_rate = vsi->mqprio_qopt.max_rate[0];

			do_div(max_tx_rate, I40E_BW_MBPS_DIVISOR);
			ret = i40e_set_bw_limit(vsi, vsi->seid, max_tx_rate);
			if (!ret) {
				u64 credits = max_tx_rate;

				do_div(credits, I40E_BW_CREDIT_DIVISOR);
				dev_dbg(&vsi->back->pdev->dev,
					"Set tx rate of %llu Mbps (count of 50Mbps %llu) for vsi->seid %u\n",
					max_tx_rate,
					credits,
					vsi->seid);
			} else {
				need_reset = true;
				goto exit;
			}
		}
		ret = i40e_configure_queue_channels(vsi);
		if (ret) {
			vsi->num_queue_pairs = old_queue_pairs;
			netdev_info(netdev,
				    "Failed configuring queue channels\n");
			need_reset = true;
			goto exit;
		}
	}

exit:
	/* Reset the configuration data to defaults, only TC0 is enabled */
	if (need_reset) {
		i40e_vsi_set_default_tc_config(vsi);
		need_reset = false;
	}

	/* Unquiesce VSI */
	i40e_unquiesce_vsi(vsi);
	return ret;
}

/**
 * i40e_set_cld_element - sets cloud filter element data
 * @filter: cloud filter rule
 * @cld: ptr to cloud filter element data
 *
 * This is helper function to copy data into cloud filter element
 **/
static inline void
i40e_set_cld_element(struct i40e_cloud_filter *filter,
		     struct i40e_aqc_cloud_filters_element_data *cld)
{
	u32 ipa;
	int i;

	memset(cld, 0, sizeof(*cld));
	ether_addr_copy(cld->outer_mac, filter->dst_mac);
	ether_addr_copy(cld->inner_mac, filter->src_mac);

	if (filter->n_proto != ETH_P_IP && filter->n_proto != ETH_P_IPV6)
		return;

	if (filter->n_proto == ETH_P_IPV6) {
#define IPV6_MAX_INDEX	(ARRAY_SIZE(filter->dst_ipv6) - 1)
		for (i = 0; i < ARRAY_SIZE(filter->dst_ipv6); i++) {
			ipa = be32_to_cpu(filter->dst_ipv6[IPV6_MAX_INDEX - i]);

			*(__le32 *)&cld->ipaddr.raw_v6.data[i * 2] = cpu_to_le32(ipa);
		}
	} else {
		ipa = be32_to_cpu(filter->dst_ipv4);

		memcpy(&cld->ipaddr.v4.data, &ipa, sizeof(ipa));
	}

	cld->inner_vlan = cpu_to_le16(ntohs(filter->vlan_id));

	/* tenant_id is not supported by FW now, once the support is enabled
	 * fill the cld->tenant_id with cpu_to_le32(filter->tenant_id)
	 */
	if (filter->tenant_id)
		return;
}

/**
 * i40e_add_del_cloud_filter - Add/del cloud filter
 * @vsi: pointer to VSI
 * @filter: cloud filter rule
 * @add: if true, add, if false, delete
 *
 * Add or delete a cloud filter for a specific flow spec.
 * Returns 0 if the filter were successfully added.
 **/
int i40e_add_del_cloud_filter(struct i40e_vsi *vsi,
			      struct i40e_cloud_filter *filter, bool add)
{
	struct i40e_aqc_cloud_filters_element_data cld_filter;
	struct i40e_pf *pf = vsi->back;
	int ret;
	static const u16 flag_table[128] = {
		[I40E_CLOUD_FILTER_FLAGS_OMAC]  =
			I40E_AQC_ADD_CLOUD_FILTER_OMAC,
		[I40E_CLOUD_FILTER_FLAGS_IMAC]  =
			I40E_AQC_ADD_CLOUD_FILTER_IMAC,
		[I40E_CLOUD_FILTER_FLAGS_IMAC_IVLAN]  =
			I40E_AQC_ADD_CLOUD_FILTER_IMAC_IVLAN,
		[I40E_CLOUD_FILTER_FLAGS_IMAC_TEN_ID] =
			I40E_AQC_ADD_CLOUD_FILTER_IMAC_TEN_ID,
		[I40E_CLOUD_FILTER_FLAGS_OMAC_TEN_ID_IMAC] =
			I40E_AQC_ADD_CLOUD_FILTER_OMAC_TEN_ID_IMAC,
		[I40E_CLOUD_FILTER_FLAGS_IMAC_IVLAN_TEN_ID] =
			I40E_AQC_ADD_CLOUD_FILTER_IMAC_IVLAN_TEN_ID,
		[I40E_CLOUD_FILTER_FLAGS_IIP] =
			I40E_AQC_ADD_CLOUD_FILTER_IIP,
	};

	if (filter->flags >= ARRAY_SIZE(flag_table))
		return I40E_ERR_CONFIG;

	memset(&cld_filter, 0, sizeof(cld_filter));

	/* copy element needed to add cloud filter from filter */
	i40e_set_cld_element(filter, &cld_filter);

	if (filter->tunnel_type != I40E_CLOUD_TNL_TYPE_NONE)
		cld_filter.flags = cpu_to_le16(filter->tunnel_type <<
					     I40E_AQC_ADD_CLOUD_TNL_TYPE_SHIFT);

	if (filter->n_proto == ETH_P_IPV6)
		cld_filter.flags |= cpu_to_le16(flag_table[filter->flags] |
						I40E_AQC_ADD_CLOUD_FLAGS_IPV6);
	else
		cld_filter.flags |= cpu_to_le16(flag_table[filter->flags] |
						I40E_AQC_ADD_CLOUD_FLAGS_IPV4);

	if (add)
		ret = i40e_aq_add_cloud_filters(&pf->hw, filter->seid,
						&cld_filter, 1);
	else
		ret = i40e_aq_rem_cloud_filters(&pf->hw, filter->seid,
						&cld_filter, 1);
	if (ret)
		dev_dbg(&pf->pdev->dev,
			"Failed to %s cloud filter using l4 port %u, err %d aq_err %d\n",
			add ? "add" : "delete", filter->dst_port, ret,
			pf->hw.aq.asq_last_status);
	else
		dev_info(&pf->pdev->dev,
			 "%s cloud filter for VSI: %d\n",
			 add ? "Added" : "Deleted", filter->seid);
	return ret;
}

/**
 * i40e_add_del_cloud_filter_big_buf - Add/del cloud filter using big_buf
 * @vsi: pointer to VSI
 * @filter: cloud filter rule
 * @add: if true, add, if false, delete
 *
 * Add or delete a cloud filter for a specific flow spec using big buffer.
 * Returns 0 if the filter were successfully added.
 **/
int i40e_add_del_cloud_filter_big_buf(struct i40e_vsi *vsi,
				      struct i40e_cloud_filter *filter,
				      bool add)
{
	struct i40e_aqc_cloud_filters_element_bb cld_filter;
	struct i40e_pf *pf = vsi->back;
	int ret;

	/* Both (src/dst) valid mac_addr are not supported */
	if ((is_valid_ether_addr(filter->dst_mac) &&
	     is_valid_ether_addr(filter->src_mac)) ||
	    (is_multicast_ether_addr(filter->dst_mac) &&
	     is_multicast_ether_addr(filter->src_mac)))
		return -EOPNOTSUPP;

	/* Big buffer cloud filter needs 'L4 port' to be non-zero. Also, UDP
	 * ports are not supported via big buffer now.
	 */
	if (!filter->dst_port || filter->ip_proto == IPPROTO_UDP)
		return -EOPNOTSUPP;

	/* adding filter using src_port/src_ip is not supported at this stage */
	if (filter->src_port ||
	    (filter->src_ipv4 && filter->n_proto != ETH_P_IPV6) ||
	    !ipv6_addr_any(&filter->ip.v6.src_ip6))
		return -EOPNOTSUPP;

	memset(&cld_filter, 0, sizeof(cld_filter));

	/* copy element needed to add cloud filter from filter */
	i40e_set_cld_element(filter, &cld_filter.element);

	if (is_valid_ether_addr(filter->dst_mac) ||
	    is_valid_ether_addr(filter->src_mac) ||
	    is_multicast_ether_addr(filter->dst_mac) ||
	    is_multicast_ether_addr(filter->src_mac)) {
		/* MAC + IP : unsupported mode */
		if (filter->dst_ipv4)
			return -EOPNOTSUPP;

		/* since we validated that L4 port must be valid before
		 * we get here, start with respective "flags" value
		 * and update if vlan is present or not
		 */
		cld_filter.element.flags =
			cpu_to_le16(I40E_AQC_ADD_CLOUD_FILTER_MAC_PORT);

		if (filter->vlan_id) {
			cld_filter.element.flags =
			cpu_to_le16(I40E_AQC_ADD_CLOUD_FILTER_MAC_VLAN_PORT);
		}

	} else if ((filter->dst_ipv4 && filter->n_proto != ETH_P_IPV6) ||
		   !ipv6_addr_any(&filter->ip.v6.dst_ip6)) {
		cld_filter.element.flags =
				cpu_to_le16(I40E_AQC_ADD_CLOUD_FILTER_IP_PORT);
		if (filter->n_proto == ETH_P_IPV6)
			cld_filter.element.flags |=
				cpu_to_le16(I40E_AQC_ADD_CLOUD_FLAGS_IPV6);
		else
			cld_filter.element.flags |=
				cpu_to_le16(I40E_AQC_ADD_CLOUD_FLAGS_IPV4);
	} else {
		dev_err(&pf->pdev->dev,
			"either mac or ip has to be valid for cloud filter\n");
		return -EINVAL;
	}

	/* Now copy L4 port in Byte 6..7 in general fields */
	cld_filter.general_fields[I40E_AQC_ADD_CLOUD_FV_FLU_0X16_WORD0] =
						be16_to_cpu(filter->dst_port);

	if (add) {
		/* Validate current device switch mode, change if necessary */
		ret = i40e_validate_and_set_switch_mode(vsi);
		if (ret) {
			dev_err(&pf->pdev->dev,
				"failed to set switch mode, ret %d\n",
				ret);
			return ret;
		}

		ret = i40e_aq_add_cloud_filters_bb(&pf->hw, filter->seid,
						   &cld_filter, 1);
	} else {
		ret = i40e_aq_rem_cloud_filters_bb(&pf->hw, filter->seid,
						   &cld_filter, 1);
	}

	if (ret)
		dev_dbg(&pf->pdev->dev,
			"Failed to %s cloud filter(big buffer) err %d aq_err %d\n",
			add ? "add" : "delete", ret, pf->hw.aq.asq_last_status);
	else
		dev_info(&pf->pdev->dev,
			 "%s cloud filter for VSI: %d, L4 port: %d\n",
			 add ? "add" : "delete", filter->seid,
			 ntohs(filter->dst_port));
	return ret;
}

/**
 * i40e_parse_cls_flower - Parse tc flower filters provided by kernel
 * @vsi: Pointer to VSI
 * @f: Pointer to struct flow_cls_offload
 * @filter: Pointer to cloud filter structure
 *
 **/
static int i40e_parse_cls_flower(struct i40e_vsi *vsi,
				 struct flow_cls_offload *f,
				 struct i40e_cloud_filter *filter)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;
	u16 n_proto_mask = 0, n_proto_key = 0, addr_type = 0;
	struct i40e_pf *pf = vsi->back;
	u8 field_flags = 0;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID))) {
		dev_err(&pf->pdev->dev, "Unsupported key used: 0x%x\n",
			dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(rule, &match);
		if (match.mask->keyid != 0)
			field_flags |= I40E_CLOUD_FIELD_TEN_ID;

		filter->tenant_id = be32_to_cpu(match.key->keyid);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		n_proto_key = ntohs(match.key->n_proto);
		n_proto_mask = ntohs(match.mask->n_proto);

		if (n_proto_key == ETH_P_ALL) {
			n_proto_key = 0;
			n_proto_mask = 0;
		}
		filter->n_proto = n_proto_key & n_proto_mask;
		filter->ip_proto = match.key->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);

		/* use is_broadcast and is_zero to check for all 0xf or 0 */
		if (!is_zero_ether_addr(match.mask->dst)) {
			if (is_broadcast_ether_addr(match.mask->dst)) {
				field_flags |= I40E_CLOUD_FIELD_OMAC;
			} else {
				dev_err(&pf->pdev->dev, "Bad ether dest mask %pM\n",
					match.mask->dst);
				return I40E_ERR_CONFIG;
			}
		}

		if (!is_zero_ether_addr(match.mask->src)) {
			if (is_broadcast_ether_addr(match.mask->src)) {
				field_flags |= I40E_CLOUD_FIELD_IMAC;
			} else {
				dev_err(&pf->pdev->dev, "Bad ether src mask %pM\n",
					match.mask->src);
				return I40E_ERR_CONFIG;
			}
		}
		ether_addr_copy(filter->dst_mac, match.key->dst);
		ether_addr_copy(filter->src_mac, match.key->src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		if (match.mask->vlan_id) {
			if (match.mask->vlan_id == VLAN_VID_MASK) {
				field_flags |= I40E_CLOUD_FIELD_IVLAN;

			} else {
				dev_err(&pf->pdev->dev, "Bad vlan mask 0x%04x\n",
					match.mask->vlan_id);
				return I40E_ERR_CONFIG;
			}
		}

		filter->vlan_id = cpu_to_be16(match.key->vlan_id);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		if (match.mask->dst) {
			if (match.mask->dst == cpu_to_be32(0xffffffff)) {
				field_flags |= I40E_CLOUD_FIELD_IIP;
			} else {
				dev_err(&pf->pdev->dev, "Bad ip dst mask %pI4b\n",
					&match.mask->dst);
				return I40E_ERR_CONFIG;
			}
		}

		if (match.mask->src) {
			if (match.mask->src == cpu_to_be32(0xffffffff)) {
				field_flags |= I40E_CLOUD_FIELD_IIP;
			} else {
				dev_err(&pf->pdev->dev, "Bad ip src mask %pI4b\n",
					&match.mask->src);
				return I40E_ERR_CONFIG;
			}
		}

		if (field_flags & I40E_CLOUD_FIELD_TEN_ID) {
			dev_err(&pf->pdev->dev, "Tenant id not allowed for ip filter\n");
			return I40E_ERR_CONFIG;
		}
		filter->dst_ipv4 = match.key->dst;
		filter->src_ipv4 = match.key->src;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);

		/* src and dest IPV6 address should not be LOOPBACK
		 * (0:0:0:0:0:0:0:1), which can be represented as ::1
		 */
		if (ipv6_addr_loopback(&match.key->dst) ||
		    ipv6_addr_loopback(&match.key->src)) {
			dev_err(&pf->pdev->dev,
				"Bad ipv6, addr is LOOPBACK\n");
			return I40E_ERR_CONFIG;
		}
		if (!ipv6_addr_any(&match.mask->dst) ||
		    !ipv6_addr_any(&match.mask->src))
			field_flags |= I40E_CLOUD_FIELD_IIP;

		memcpy(&filter->src_ipv6, &match.key->src.s6_addr32,
		       sizeof(filter->src_ipv6));
		memcpy(&filter->dst_ipv6, &match.key->dst.s6_addr32,
		       sizeof(filter->dst_ipv6));
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		if (match.mask->src) {
			if (match.mask->src == cpu_to_be16(0xffff)) {
				field_flags |= I40E_CLOUD_FIELD_IIP;
			} else {
				dev_err(&pf->pdev->dev, "Bad src port mask 0x%04x\n",
					be16_to_cpu(match.mask->src));
				return I40E_ERR_CONFIG;
			}
		}

		if (match.mask->dst) {
			if (match.mask->dst == cpu_to_be16(0xffff)) {
				field_flags |= I40E_CLOUD_FIELD_IIP;
			} else {
				dev_err(&pf->pdev->dev, "Bad dst port mask 0x%04x\n",
					be16_to_cpu(match.mask->dst));
				return I40E_ERR_CONFIG;
			}
		}

		filter->dst_port = match.key->dst;
		filter->src_port = match.key->src;

		switch (filter->ip_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			break;
		default:
			dev_err(&pf->pdev->dev,
				"Only UDP and TCP transport are supported\n");
			return -EINVAL;
		}
	}
	filter->flags = field_flags;
	return 0;
}

/**
 * i40e_handle_tclass: Forward to a traffic class on the device
 * @vsi: Pointer to VSI
 * @tc: traffic class index on the device
 * @filter: Pointer to cloud filter structure
 *
 **/
static int i40e_handle_tclass(struct i40e_vsi *vsi, u32 tc,
			      struct i40e_cloud_filter *filter)
{
	struct i40e_channel *ch, *ch_tmp;

	/* direct to a traffic class on the same device */
	if (tc == 0) {
		filter->seid = vsi->seid;
		return 0;
	} else if (vsi->tc_config.enabled_tc & BIT(tc)) {
		if (!filter->dst_port) {
			dev_err(&vsi->back->pdev->dev,
				"Specify destination port to direct to traffic class that is not default\n");
			return -EINVAL;
		}
		if (list_empty(&vsi->ch_list))
			return -EINVAL;
		list_for_each_entry_safe(ch, ch_tmp, &vsi->ch_list,
					 list) {
			if (ch->seid == vsi->tc_seid_map[tc])
				filter->seid = ch->seid;
		}
		return 0;
	}
	dev_err(&vsi->back->pdev->dev, "TC is not enabled\n");
	return -EINVAL;
}

/**
 * i40e_configure_clsflower - Configure tc flower filters
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to struct flow_cls_offload
 *
 **/
static int i40e_configure_clsflower(struct i40e_vsi *vsi,
				    struct flow_cls_offload *cls_flower)
{
	int tc = tc_classid_to_hwtc(vsi->netdev, cls_flower->classid);
	struct i40e_cloud_filter *filter = NULL;
	struct i40e_pf *pf = vsi->back;
	int err = 0;

	if (tc < 0) {
		dev_err(&vsi->back->pdev->dev, "Invalid traffic class\n");
		return -EOPNOTSUPP;
	}

	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		return -EBUSY;

	if (pf->fdir_pf_active_filters ||
	    (!hlist_empty(&pf->fdir_filter_list))) {
		dev_err(&vsi->back->pdev->dev,
			"Flow Director Sideband filters exists, turn ntuple off to configure cloud filters\n");
		return -EINVAL;
	}

	if (vsi->back->flags & I40E_FLAG_FD_SB_ENABLED) {
		dev_err(&vsi->back->pdev->dev,
			"Disable Flow Director Sideband, configuring Cloud filters via tc-flower\n");
		vsi->back->flags &= ~I40E_FLAG_FD_SB_ENABLED;
		vsi->back->flags |= I40E_FLAG_FD_SB_TO_CLOUD_FILTER;
	}

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	filter->cookie = cls_flower->cookie;

	err = i40e_parse_cls_flower(vsi, cls_flower, filter);
	if (err < 0)
		goto err;

	err = i40e_handle_tclass(vsi, tc, filter);
	if (err < 0)
		goto err;

	/* Add cloud filter */
	if (filter->dst_port)
		err = i40e_add_del_cloud_filter_big_buf(vsi, filter, true);
	else
		err = i40e_add_del_cloud_filter(vsi, filter, true);

	if (err) {
		dev_err(&pf->pdev->dev, "Failed to add cloud filter, err %d\n",
			err);
		goto err;
	}

	/* add filter to the ordered list */
	INIT_HLIST_NODE(&filter->cloud_node);

	hlist_add_head(&filter->cloud_node, &pf->cloud_filter_list);

	pf->num_cloud_filters++;

	return err;
err:
	kfree(filter);
	return err;
}

/**
 * i40e_find_cloud_filter - Find the could filter in the list
 * @vsi: Pointer to VSI
 * @cookie: filter specific cookie
 *
 **/
static struct i40e_cloud_filter *i40e_find_cloud_filter(struct i40e_vsi *vsi,
							unsigned long *cookie)
{
	struct i40e_cloud_filter *filter = NULL;
	struct hlist_node *node2;

	hlist_for_each_entry_safe(filter, node2,
				  &vsi->back->cloud_filter_list, cloud_node)
		if (!memcmp(cookie, &filter->cookie, sizeof(filter->cookie)))
			return filter;
	return NULL;
}

/**
 * i40e_delete_clsflower - Remove tc flower filters
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to struct flow_cls_offload
 *
 **/
static int i40e_delete_clsflower(struct i40e_vsi *vsi,
				 struct flow_cls_offload *cls_flower)
{
	struct i40e_cloud_filter *filter = NULL;
	struct i40e_pf *pf = vsi->back;
	int err = 0;

	filter = i40e_find_cloud_filter(vsi, &cls_flower->cookie);

	if (!filter)
		return -EINVAL;

	hash_del(&filter->cloud_node);

	if (filter->dst_port)
		err = i40e_add_del_cloud_filter_big_buf(vsi, filter, false);
	else
		err = i40e_add_del_cloud_filter(vsi, filter, false);

	kfree(filter);
	if (err) {
		dev_err(&pf->pdev->dev,
			"Failed to delete cloud filter, err %s\n",
			i40e_stat_str(&pf->hw, err));
		return i40e_aq_rc_to_posix(err, pf->hw.aq.asq_last_status);
	}

	pf->num_cloud_filters--;
	if (!pf->num_cloud_filters)
		if ((pf->flags & I40E_FLAG_FD_SB_TO_CLOUD_FILTER) &&
		    !(pf->flags & I40E_FLAG_FD_SB_INACTIVE)) {
			pf->flags |= I40E_FLAG_FD_SB_ENABLED;
			pf->flags &= ~I40E_FLAG_FD_SB_TO_CLOUD_FILTER;
			pf->flags &= ~I40E_FLAG_FD_SB_INACTIVE;
		}
	return 0;
}

/**
 * i40e_setup_tc_cls_flower - flower classifier offloads
 * @np: net device to configure
 * @cls_flower: offload data
 **/
static int i40e_setup_tc_cls_flower(struct i40e_netdev_priv *np,
				    struct flow_cls_offload *cls_flower)
{
	struct i40e_vsi *vsi = np->vsi;

	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return i40e_configure_clsflower(vsi, cls_flower);
	case FLOW_CLS_DESTROY:
		return i40e_delete_clsflower(vsi, cls_flower);
	case FLOW_CLS_STATS:
		return -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}
}

static int i40e_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				  void *cb_priv)
{
	struct i40e_netdev_priv *np = cb_priv;

	if (!tc_cls_can_offload_and_chain0(np->vsi->netdev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return i40e_setup_tc_cls_flower(np, type_data);

	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(i40e_block_cb_list);

static int __i40e_setup_tc(struct net_device *netdev, enum tc_setup_type type,
			   void *type_data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);

	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return i40e_setup_tc(netdev, type_data);
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &i40e_block_cb_list,
						  i40e_setup_tc_block_cb,
						  np, np, true);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * i40e_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog subtask is
 * enabled, and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 **/
int i40e_open(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int err;

	/* disallow open during test or if eeprom is broken */
	if (test_bit(__I40E_TESTING, pf->state) ||
	    test_bit(__I40E_BAD_EEPROM, pf->state))
		return -EBUSY;

	netif_carrier_off(netdev);

	if (i40e_force_link_state(pf, true))
		return -EAGAIN;

	err = i40e_vsi_open(vsi);
	if (err)
		return err;

	/* configure global TSO hardware offload settings */
	wr32(&pf->hw, I40E_GLLAN_TSOMSK_F, be32_to_cpu(TCP_FLAG_PSH |
						       TCP_FLAG_FIN) >> 16);
	wr32(&pf->hw, I40E_GLLAN_TSOMSK_M, be32_to_cpu(TCP_FLAG_PSH |
						       TCP_FLAG_FIN |
						       TCP_FLAG_CWR) >> 16);
	wr32(&pf->hw, I40E_GLLAN_TSOMSK_L, be32_to_cpu(TCP_FLAG_CWR) >> 16);

	udp_tunnel_get_rx_info(netdev);

	return 0;
}

/**
 * i40e_netif_set_realnum_tx_rx_queues - Update number of tx/rx queues
 * @vsi: vsi structure
 *
 * This updates netdev's number of tx/rx queues
 *
 * Returns status of setting tx/rx queues
 **/
static int i40e_netif_set_realnum_tx_rx_queues(struct i40e_vsi *vsi)
{
	int ret;

	ret = netif_set_real_num_rx_queues(vsi->netdev,
					   vsi->num_queue_pairs);
	if (ret)
		return ret;

	return netif_set_real_num_tx_queues(vsi->netdev,
					    vsi->num_queue_pairs);
}

/**
 * i40e_vsi_open -
 * @vsi: the VSI to open
 *
 * Finish initialization of the VSI.
 *
 * Returns 0 on success, negative value on failure
 *
 * Note: expects to be called while under rtnl_lock()
 **/
int i40e_vsi_open(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	char int_name[I40E_INT_NAME_STR_LEN];
	int err;

	/* allocate descriptors */
	err = i40e_vsi_setup_tx_resources(vsi);
	if (err)
		goto err_setup_tx;
	err = i40e_vsi_setup_rx_resources(vsi);
	if (err)
		goto err_setup_rx;

	err = i40e_vsi_configure(vsi);
	if (err)
		goto err_setup_rx;

	if (vsi->netdev) {
		snprintf(int_name, sizeof(int_name) - 1, "%s-%s",
			 dev_driver_string(&pf->pdev->dev), vsi->netdev->name);
		err = i40e_vsi_request_irq(vsi, int_name);
		if (err)
			goto err_setup_rx;

		/* Notify the stack of the actual queue counts. */
		err = i40e_netif_set_realnum_tx_rx_queues(vsi);
		if (err)
			goto err_set_queues;

	} else if (vsi->type == I40E_VSI_FDIR) {
		snprintf(int_name, sizeof(int_name) - 1, "%s-%s:fdir",
			 dev_driver_string(&pf->pdev->dev),
			 dev_name(&pf->pdev->dev));
		err = i40e_vsi_request_irq(vsi, int_name);
		if (err)
			goto err_setup_rx;

	} else {
		err = -EINVAL;
		goto err_setup_rx;
	}

	err = i40e_up_complete(vsi);
	if (err)
		goto err_up_complete;

	return 0;

err_up_complete:
	i40e_down(vsi);
err_set_queues:
	i40e_vsi_free_irq(vsi);
err_setup_rx:
	i40e_vsi_free_rx_resources(vsi);
err_setup_tx:
	i40e_vsi_free_tx_resources(vsi);
	if (vsi == pf->vsi[pf->lan_vsi])
		i40e_do_reset(pf, I40E_PF_RESET_FLAG, true);

	return err;
}

/**
 * i40e_fdir_filter_exit - Cleans up the Flow Director accounting
 * @pf: Pointer to PF
 *
 * This function destroys the hlist where all the Flow Director
 * filters were saved.
 **/
static void i40e_fdir_filter_exit(struct i40e_pf *pf)
{
	struct i40e_fdir_filter *filter;
	struct i40e_flex_pit *pit_entry, *tmp;
	struct hlist_node *node2;

	hlist_for_each_entry_safe(filter, node2,
				  &pf->fdir_filter_list, fdir_node) {
		hlist_del(&filter->fdir_node);
		kfree(filter);
	}

	list_for_each_entry_safe(pit_entry, tmp, &pf->l3_flex_pit_list, list) {
		list_del(&pit_entry->list);
		kfree(pit_entry);
	}
	INIT_LIST_HEAD(&pf->l3_flex_pit_list);

	list_for_each_entry_safe(pit_entry, tmp, &pf->l4_flex_pit_list, list) {
		list_del(&pit_entry->list);
		kfree(pit_entry);
	}
	INIT_LIST_HEAD(&pf->l4_flex_pit_list);

	pf->fdir_pf_active_filters = 0;
	pf->fd_tcp4_filter_cnt = 0;
	pf->fd_udp4_filter_cnt = 0;
	pf->fd_sctp4_filter_cnt = 0;
	pf->fd_ip4_filter_cnt = 0;

	/* Reprogram the default input set for TCP/IPv4 */
	i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_NONF_IPV4_TCP,
				I40E_L3_SRC_MASK | I40E_L3_DST_MASK |
				I40E_L4_SRC_MASK | I40E_L4_DST_MASK);

	/* Reprogram the default input set for UDP/IPv4 */
	i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_NONF_IPV4_UDP,
				I40E_L3_SRC_MASK | I40E_L3_DST_MASK |
				I40E_L4_SRC_MASK | I40E_L4_DST_MASK);

	/* Reprogram the default input set for SCTP/IPv4 */
	i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_NONF_IPV4_SCTP,
				I40E_L3_SRC_MASK | I40E_L3_DST_MASK |
				I40E_L4_SRC_MASK | I40E_L4_DST_MASK);

	/* Reprogram the default input set for Other/IPv4 */
	i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_NONF_IPV4_OTHER,
				I40E_L3_SRC_MASK | I40E_L3_DST_MASK);

	i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_FRAG_IPV4,
				I40E_L3_SRC_MASK | I40E_L3_DST_MASK);
}

/**
 * i40e_cloud_filter_exit - Cleans up the cloud filters
 * @pf: Pointer to PF
 *
 * This function destroys the hlist where all the cloud filters
 * were saved.
 **/
static void i40e_cloud_filter_exit(struct i40e_pf *pf)
{
	struct i40e_cloud_filter *cfilter;
	struct hlist_node *node;

	hlist_for_each_entry_safe(cfilter, node,
				  &pf->cloud_filter_list, cloud_node) {
		hlist_del(&cfilter->cloud_node);
		kfree(cfilter);
	}
	pf->num_cloud_filters = 0;

	if ((pf->flags & I40E_FLAG_FD_SB_TO_CLOUD_FILTER) &&
	    !(pf->flags & I40E_FLAG_FD_SB_INACTIVE)) {
		pf->flags |= I40E_FLAG_FD_SB_ENABLED;
		pf->flags &= ~I40E_FLAG_FD_SB_TO_CLOUD_FILTER;
		pf->flags &= ~I40E_FLAG_FD_SB_INACTIVE;
	}
}

/**
 * i40e_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the driver's control, but
 * this netdev interface is disabled.
 *
 * Returns 0, this is not allowed to fail
 **/
int i40e_close(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	i40e_vsi_close(vsi);

	return 0;
}

/**
 * i40e_do_reset - Start a PF or Core Reset sequence
 * @pf: board private structure
 * @reset_flags: which reset is requested
 * @lock_acquired: indicates whether or not the lock has been acquired
 * before this function was called.
 *
 * The essential difference in resets is that the PF Reset
 * doesn't clear the packet buffers, doesn't reset the PE
 * firmware, and doesn't bother the other PFs on the chip.
 **/
void i40e_do_reset(struct i40e_pf *pf, u32 reset_flags, bool lock_acquired)
{
	u32 val;

	/* do the biggest reset indicated */
	if (reset_flags & BIT_ULL(__I40E_GLOBAL_RESET_REQUESTED)) {

		/* Request a Global Reset
		 *
		 * This will start the chip's countdown to the actual full
		 * chip reset event, and a warning interrupt to be sent
		 * to all PFs, including the requestor.  Our handler
		 * for the warning interrupt will deal with the shutdown
		 * and recovery of the switch setup.
		 */
		dev_dbg(&pf->pdev->dev, "GlobalR requested\n");
		val = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		val |= I40E_GLGEN_RTRIG_GLOBR_MASK;
		wr32(&pf->hw, I40E_GLGEN_RTRIG, val);

	} else if (reset_flags & BIT_ULL(__I40E_CORE_RESET_REQUESTED)) {

		/* Request a Core Reset
		 *
		 * Same as Global Reset, except does *not* include the MAC/PHY
		 */
		dev_dbg(&pf->pdev->dev, "CoreR requested\n");
		val = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		val |= I40E_GLGEN_RTRIG_CORER_MASK;
		wr32(&pf->hw, I40E_GLGEN_RTRIG, val);
		i40e_flush(&pf->hw);

	} else if (reset_flags & I40E_PF_RESET_FLAG) {

		/* Request a PF Reset
		 *
		 * Resets only the PF-specific registers
		 *
		 * This goes directly to the tear-down and rebuild of
		 * the switch, since we need to do all the recovery as
		 * for the Core Reset.
		 */
		dev_dbg(&pf->pdev->dev, "PFR requested\n");
		i40e_handle_reset_warning(pf, lock_acquired);

	} else if (reset_flags & I40E_PF_RESET_AND_REBUILD_FLAG) {
		/* Request a PF Reset
		 *
		 * Resets PF and reinitializes PFs VSI.
		 */
		i40e_prep_for_reset(pf, lock_acquired);
		i40e_reset_and_rebuild(pf, true, lock_acquired);
		dev_info(&pf->pdev->dev,
			 pf->flags & I40E_FLAG_DISABLE_FW_LLDP ?
			 "FW LLDP is disabled\n" :
			 "FW LLDP is enabled\n");

	} else if (reset_flags & BIT_ULL(__I40E_REINIT_REQUESTED)) {
		int v;

		/* Find the VSI(s) that requested a re-init */
		dev_info(&pf->pdev->dev,
			 "VSI reinit requested\n");
		for (v = 0; v < pf->num_alloc_vsi; v++) {
			struct i40e_vsi *vsi = pf->vsi[v];

			if (vsi != NULL &&
			    test_and_clear_bit(__I40E_VSI_REINIT_REQUESTED,
					       vsi->state))
				i40e_vsi_reinit_locked(pf->vsi[v]);
		}
	} else if (reset_flags & BIT_ULL(__I40E_DOWN_REQUESTED)) {
		int v;

		/* Find the VSI(s) that needs to be brought down */
		dev_info(&pf->pdev->dev, "VSI down requested\n");
		for (v = 0; v < pf->num_alloc_vsi; v++) {
			struct i40e_vsi *vsi = pf->vsi[v];

			if (vsi != NULL &&
			    test_and_clear_bit(__I40E_VSI_DOWN_REQUESTED,
					       vsi->state)) {
				set_bit(__I40E_VSI_DOWN, vsi->state);
				i40e_down(vsi);
			}
		}
	} else {
		dev_info(&pf->pdev->dev,
			 "bad reset request 0x%08x\n", reset_flags);
	}
}

#ifdef CONFIG_I40E_DCB
/**
 * i40e_dcb_need_reconfig - Check if DCB needs reconfig
 * @pf: board private structure
 * @old_cfg: current DCB config
 * @new_cfg: new DCB config
 **/
bool i40e_dcb_need_reconfig(struct i40e_pf *pf,
			    struct i40e_dcbx_config *old_cfg,
			    struct i40e_dcbx_config *new_cfg)
{
	bool need_reconfig = false;

	/* Check if ETS configuration has changed */
	if (memcmp(&new_cfg->etscfg,
		   &old_cfg->etscfg,
		   sizeof(new_cfg->etscfg))) {
		/* If Priority Table has changed reconfig is needed */
		if (memcmp(&new_cfg->etscfg.prioritytable,
			   &old_cfg->etscfg.prioritytable,
			   sizeof(new_cfg->etscfg.prioritytable))) {
			need_reconfig = true;
			dev_dbg(&pf->pdev->dev, "ETS UP2TC changed.\n");
		}

		if (memcmp(&new_cfg->etscfg.tcbwtable,
			   &old_cfg->etscfg.tcbwtable,
			   sizeof(new_cfg->etscfg.tcbwtable)))
			dev_dbg(&pf->pdev->dev, "ETS TC BW Table changed.\n");

		if (memcmp(&new_cfg->etscfg.tsatable,
			   &old_cfg->etscfg.tsatable,
			   sizeof(new_cfg->etscfg.tsatable)))
			dev_dbg(&pf->pdev->dev, "ETS TSA Table changed.\n");
	}

	/* Check if PFC configuration has changed */
	if (memcmp(&new_cfg->pfc,
		   &old_cfg->pfc,
		   sizeof(new_cfg->pfc))) {
		need_reconfig = true;
		dev_dbg(&pf->pdev->dev, "PFC config change detected.\n");
	}

	/* Check if APP Table has changed */
	if (memcmp(&new_cfg->app,
		   &old_cfg->app,
		   sizeof(new_cfg->app))) {
		need_reconfig = true;
		dev_dbg(&pf->pdev->dev, "APP Table change detected.\n");
	}

	dev_dbg(&pf->pdev->dev, "dcb need_reconfig=%d\n", need_reconfig);
	return need_reconfig;
}

/**
 * i40e_handle_lldp_event - Handle LLDP Change MIB event
 * @pf: board private structure
 * @e: event info posted on ARQ
 **/
static int i40e_handle_lldp_event(struct i40e_pf *pf,
				  struct i40e_arq_event_info *e)
{
	struct i40e_aqc_lldp_get_mib *mib =
		(struct i40e_aqc_lldp_get_mib *)&e->desc.params.raw;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_dcbx_config tmp_dcbx_cfg;
	bool need_reconfig = false;
	int ret = 0;
	u8 type;

	/* Not DCB capable or capability disabled */
	if (!(pf->flags & I40E_FLAG_DCB_CAPABLE))
		return ret;

	/* Ignore if event is not for Nearest Bridge */
	type = ((mib->type >> I40E_AQ_LLDP_BRIDGE_TYPE_SHIFT)
		& I40E_AQ_LLDP_BRIDGE_TYPE_MASK);
	dev_dbg(&pf->pdev->dev, "LLDP event mib bridge type 0x%x\n", type);
	if (type != I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE)
		return ret;

	/* Check MIB Type and return if event for Remote MIB update */
	type = mib->type & I40E_AQ_LLDP_MIB_TYPE_MASK;
	dev_dbg(&pf->pdev->dev,
		"LLDP event mib type %s\n", type ? "remote" : "local");
	if (type == I40E_AQ_LLDP_MIB_REMOTE) {
		/* Update the remote cached instance and return */
		ret = i40e_aq_get_dcb_config(hw, I40E_AQ_LLDP_MIB_REMOTE,
				I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE,
				&hw->remote_dcbx_config);
		goto exit;
	}

	/* Store the old configuration */
	tmp_dcbx_cfg = hw->local_dcbx_config;

	/* Reset the old DCBx configuration data */
	memset(&hw->local_dcbx_config, 0, sizeof(hw->local_dcbx_config));
	/* Get updated DCBX data from firmware */
	ret = i40e_get_dcb_config(&pf->hw);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Failed querying DCB configuration data from firmware, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		goto exit;
	}

	/* No change detected in DCBX configs */
	if (!memcmp(&tmp_dcbx_cfg, &hw->local_dcbx_config,
		    sizeof(tmp_dcbx_cfg))) {
		dev_dbg(&pf->pdev->dev, "No change detected in DCBX configuration.\n");
		goto exit;
	}

	need_reconfig = i40e_dcb_need_reconfig(pf, &tmp_dcbx_cfg,
					       &hw->local_dcbx_config);

	i40e_dcbnl_flush_apps(pf, &tmp_dcbx_cfg, &hw->local_dcbx_config);

	if (!need_reconfig)
		goto exit;

	/* Enable DCB tagging only when more than one TC */
	if (i40e_dcb_get_num_tc(&hw->local_dcbx_config) > 1)
		pf->flags |= I40E_FLAG_DCB_ENABLED;
	else
		pf->flags &= ~I40E_FLAG_DCB_ENABLED;

	set_bit(__I40E_PORT_SUSPENDED, pf->state);
	/* Reconfiguration needed quiesce all VSIs */
	i40e_pf_quiesce_all_vsi(pf);

	/* Changes in configuration update VEB/VSI */
	i40e_dcb_reconfigure(pf);

	ret = i40e_resume_port_tx(pf);

	clear_bit(__I40E_PORT_SUSPENDED, pf->state);
	/* In case of error no point in resuming VSIs */
	if (ret)
		goto exit;

	/* Wait for the PF's queues to be disabled */
	ret = i40e_pf_wait_queues_disabled(pf);
	if (ret) {
		/* Schedule PF reset to recover */
		set_bit(__I40E_PF_RESET_REQUESTED, pf->state);
		i40e_service_event_schedule(pf);
	} else {
		i40e_pf_unquiesce_all_vsi(pf);
		set_bit(__I40E_CLIENT_SERVICE_REQUESTED, pf->state);
		set_bit(__I40E_CLIENT_L2_CHANGE, pf->state);
	}

exit:
	return ret;
}
#endif /* CONFIG_I40E_DCB */

/**
 * i40e_do_reset_safe - Protected reset path for userland calls.
 * @pf: board private structure
 * @reset_flags: which reset is requested
 *
 **/
void i40e_do_reset_safe(struct i40e_pf *pf, u32 reset_flags)
{
	rtnl_lock();
	i40e_do_reset(pf, reset_flags, true);
	rtnl_unlock();
}

/**
 * i40e_handle_lan_overflow_event - Handler for LAN queue overflow event
 * @pf: board private structure
 * @e: event info posted on ARQ
 *
 * Handler for LAN Queue Overflow Event generated by the firmware for PF
 * and VF queues
 **/
static void i40e_handle_lan_overflow_event(struct i40e_pf *pf,
					   struct i40e_arq_event_info *e)
{
	struct i40e_aqc_lan_overflow *data =
		(struct i40e_aqc_lan_overflow *)&e->desc.params.raw;
	u32 queue = le32_to_cpu(data->prtdcb_rupto);
	u32 qtx_ctl = le32_to_cpu(data->otx_ctl);
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vf *vf;
	u16 vf_id;

	dev_dbg(&pf->pdev->dev, "overflow Rx Queue Number = %d QTX_CTL=0x%08x\n",
		queue, qtx_ctl);

	/* Queue belongs to VF, find the VF and issue VF reset */
	if (((qtx_ctl & I40E_QTX_CTL_PFVF_Q_MASK)
	    >> I40E_QTX_CTL_PFVF_Q_SHIFT) == I40E_QTX_CTL_VF_QUEUE) {
		vf_id = (u16)((qtx_ctl & I40E_QTX_CTL_VFVM_INDX_MASK)
			 >> I40E_QTX_CTL_VFVM_INDX_SHIFT);
		vf_id -= hw->func_caps.vf_base_id;
		vf = &pf->vf[vf_id];
		i40e_vc_notify_vf_reset(vf);
		/* Allow VF to process pending reset notification */
		msleep(20);
		i40e_reset_vf(vf, false);
	}
}

/**
 * i40e_get_cur_guaranteed_fd_count - Get the consumed guaranteed FD filters
 * @pf: board private structure
 **/
u32 i40e_get_cur_guaranteed_fd_count(struct i40e_pf *pf)
{
	u32 val, fcnt_prog;

	val = rd32(&pf->hw, I40E_PFQF_FDSTAT);
	fcnt_prog = (val & I40E_PFQF_FDSTAT_GUARANT_CNT_MASK);
	return fcnt_prog;
}

/**
 * i40e_get_current_fd_count - Get total FD filters programmed for this PF
 * @pf: board private structure
 **/
u32 i40e_get_current_fd_count(struct i40e_pf *pf)
{
	u32 val, fcnt_prog;

	val = rd32(&pf->hw, I40E_PFQF_FDSTAT);
	fcnt_prog = (val & I40E_PFQF_FDSTAT_GUARANT_CNT_MASK) +
		    ((val & I40E_PFQF_FDSTAT_BEST_CNT_MASK) >>
		      I40E_PFQF_FDSTAT_BEST_CNT_SHIFT);
	return fcnt_prog;
}

/**
 * i40e_get_global_fd_count - Get total FD filters programmed on device
 * @pf: board private structure
 **/
u32 i40e_get_global_fd_count(struct i40e_pf *pf)
{
	u32 val, fcnt_prog;

	val = rd32(&pf->hw, I40E_GLQF_FDCNT_0);
	fcnt_prog = (val & I40E_GLQF_FDCNT_0_GUARANT_CNT_MASK) +
		    ((val & I40E_GLQF_FDCNT_0_BESTCNT_MASK) >>
		     I40E_GLQF_FDCNT_0_BESTCNT_SHIFT);
	return fcnt_prog;
}

/**
 * i40e_reenable_fdir_sb - Restore FDir SB capability
 * @pf: board private structure
 **/
static void i40e_reenable_fdir_sb(struct i40e_pf *pf)
{
	if (test_and_clear_bit(__I40E_FD_SB_AUTO_DISABLED, pf->state))
		if ((pf->flags & I40E_FLAG_FD_SB_ENABLED) &&
		    (I40E_DEBUG_FD & pf->hw.debug_mask))
			dev_info(&pf->pdev->dev, "FD Sideband/ntuple is being enabled since we have space in the table now\n");
}

/**
 * i40e_reenable_fdir_atr - Restore FDir ATR capability
 * @pf: board private structure
 **/
static void i40e_reenable_fdir_atr(struct i40e_pf *pf)
{
	if (test_and_clear_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state)) {
		/* ATR uses the same filtering logic as SB rules. It only
		 * functions properly if the input set mask is at the default
		 * settings. It is safe to restore the default input set
		 * because there are no active TCPv4 filter rules.
		 */
		i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_NONF_IPV4_TCP,
					I40E_L3_SRC_MASK | I40E_L3_DST_MASK |
					I40E_L4_SRC_MASK | I40E_L4_DST_MASK);

		if ((pf->flags & I40E_FLAG_FD_ATR_ENABLED) &&
		    (I40E_DEBUG_FD & pf->hw.debug_mask))
			dev_info(&pf->pdev->dev, "ATR is being enabled since we have space in the table and there are no conflicting ntuple rules\n");
	}
}

/**
 * i40e_delete_invalid_filter - Delete an invalid FDIR filter
 * @pf: board private structure
 * @filter: FDir filter to remove
 */
static void i40e_delete_invalid_filter(struct i40e_pf *pf,
				       struct i40e_fdir_filter *filter)
{
	/* Update counters */
	pf->fdir_pf_active_filters--;
	pf->fd_inv = 0;

	switch (filter->flow_type) {
	case TCP_V4_FLOW:
		pf->fd_tcp4_filter_cnt--;
		break;
	case UDP_V4_FLOW:
		pf->fd_udp4_filter_cnt--;
		break;
	case SCTP_V4_FLOW:
		pf->fd_sctp4_filter_cnt--;
		break;
	case IP_USER_FLOW:
		switch (filter->ip4_proto) {
		case IPPROTO_TCP:
			pf->fd_tcp4_filter_cnt--;
			break;
		case IPPROTO_UDP:
			pf->fd_udp4_filter_cnt--;
			break;
		case IPPROTO_SCTP:
			pf->fd_sctp4_filter_cnt--;
			break;
		case IPPROTO_IP:
			pf->fd_ip4_filter_cnt--;
			break;
		}
		break;
	}

	/* Remove the filter from the list and free memory */
	hlist_del(&filter->fdir_node);
	kfree(filter);
}

/**
 * i40e_fdir_check_and_reenable - Function to reenabe FD ATR or SB if disabled
 * @pf: board private structure
 **/
void i40e_fdir_check_and_reenable(struct i40e_pf *pf)
{
	struct i40e_fdir_filter *filter;
	u32 fcnt_prog, fcnt_avail;
	struct hlist_node *node;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		return;

	/* Check if we have enough room to re-enable FDir SB capability. */
	fcnt_prog = i40e_get_global_fd_count(pf);
	fcnt_avail = pf->fdir_pf_filter_count;
	if ((fcnt_prog < (fcnt_avail - I40E_FDIR_BUFFER_HEAD_ROOM)) ||
	    (pf->fd_add_err == 0) ||
	    (i40e_get_current_atr_cnt(pf) < pf->fd_atr_cnt))
		i40e_reenable_fdir_sb(pf);

	/* We should wait for even more space before re-enabling ATR.
	 * Additionally, we cannot enable ATR as long as we still have TCP SB
	 * rules active.
	 */
	if ((fcnt_prog < (fcnt_avail - I40E_FDIR_BUFFER_HEAD_ROOM_FOR_ATR)) &&
	    (pf->fd_tcp4_filter_cnt == 0))
		i40e_reenable_fdir_atr(pf);

	/* if hw had a problem adding a filter, delete it */
	if (pf->fd_inv > 0) {
		hlist_for_each_entry_safe(filter, node,
					  &pf->fdir_filter_list, fdir_node)
			if (filter->fd_id == pf->fd_inv)
				i40e_delete_invalid_filter(pf, filter);
	}
}

#define I40E_MIN_FD_FLUSH_INTERVAL 10
#define I40E_MIN_FD_FLUSH_SB_ATR_UNSTABLE 30
/**
 * i40e_fdir_flush_and_replay - Function to flush all FD filters and replay SB
 * @pf: board private structure
 **/
static void i40e_fdir_flush_and_replay(struct i40e_pf *pf)
{
	unsigned long min_flush_time;
	int flush_wait_retry = 50;
	bool disable_atr = false;
	int fd_room;
	int reg;

	if (!time_after(jiffies, pf->fd_flush_timestamp +
				 (I40E_MIN_FD_FLUSH_INTERVAL * HZ)))
		return;

	/* If the flush is happening too quick and we have mostly SB rules we
	 * should not re-enable ATR for some time.
	 */
	min_flush_time = pf->fd_flush_timestamp +
			 (I40E_MIN_FD_FLUSH_SB_ATR_UNSTABLE * HZ);
	fd_room = pf->fdir_pf_filter_count - pf->fdir_pf_active_filters;

	if (!(time_after(jiffies, min_flush_time)) &&
	    (fd_room < I40E_FDIR_BUFFER_HEAD_ROOM_FOR_ATR)) {
		if (I40E_DEBUG_FD & pf->hw.debug_mask)
			dev_info(&pf->pdev->dev, "ATR disabled, not enough FD filter space.\n");
		disable_atr = true;
	}

	pf->fd_flush_timestamp = jiffies;
	set_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state);
	/* flush all filters */
	wr32(&pf->hw, I40E_PFQF_CTL_1,
	     I40E_PFQF_CTL_1_CLEARFDTABLE_MASK);
	i40e_flush(&pf->hw);
	pf->fd_flush_cnt++;
	pf->fd_add_err = 0;
	do {
		/* Check FD flush status every 5-6msec */
		usleep_range(5000, 6000);
		reg = rd32(&pf->hw, I40E_PFQF_CTL_1);
		if (!(reg & I40E_PFQF_CTL_1_CLEARFDTABLE_MASK))
			break;
	} while (flush_wait_retry--);
	if (reg & I40E_PFQF_CTL_1_CLEARFDTABLE_MASK) {
		dev_warn(&pf->pdev->dev, "FD table did not flush, needs more time\n");
	} else {
		/* replay sideband filters */
		i40e_fdir_filter_restore(pf->vsi[pf->lan_vsi]);
		if (!disable_atr && !pf->fd_tcp4_filter_cnt)
			clear_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state);
		clear_bit(__I40E_FD_FLUSH_REQUESTED, pf->state);
		if (I40E_DEBUG_FD & pf->hw.debug_mask)
			dev_info(&pf->pdev->dev, "FD Filter table flushed and FD-SB replayed.\n");
	}
}

/**
 * i40e_get_current_atr_count - Get the count of total FD ATR filters programmed
 * @pf: board private structure
 **/
u32 i40e_get_current_atr_cnt(struct i40e_pf *pf)
{
	return i40e_get_current_fd_count(pf) - pf->fdir_pf_active_filters;
}

/**
 * i40e_fdir_reinit_subtask - Worker thread to reinit FDIR filter table
 * @pf: board private structure
 **/
static void i40e_fdir_reinit_subtask(struct i40e_pf *pf)
{

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, pf->state))
		return;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		i40e_fdir_flush_and_replay(pf);

	i40e_fdir_check_and_reenable(pf);

}

/**
 * i40e_vsi_link_event - notify VSI of a link event
 * @vsi: vsi to be notified
 * @link_up: link up or down
 **/
static void i40e_vsi_link_event(struct i40e_vsi *vsi, bool link_up)
{
	if (!vsi || test_bit(__I40E_VSI_DOWN, vsi->state))
		return;

	switch (vsi->type) {
	case I40E_VSI_MAIN:
		if (!vsi->netdev || !vsi->netdev_registered)
			break;

		if (link_up) {
			netif_carrier_on(vsi->netdev);
			netif_tx_wake_all_queues(vsi->netdev);
		} else {
			netif_carrier_off(vsi->netdev);
			netif_tx_stop_all_queues(vsi->netdev);
		}
		break;

	case I40E_VSI_SRIOV:
	case I40E_VSI_VMDQ2:
	case I40E_VSI_CTRL:
	case I40E_VSI_IWARP:
	case I40E_VSI_MIRROR:
	default:
		/* there is no notification for other VSIs */
		break;
	}
}

/**
 * i40e_veb_link_event - notify elements on the veb of a link event
 * @veb: veb to be notified
 * @link_up: link up or down
 **/
static void i40e_veb_link_event(struct i40e_veb *veb, bool link_up)
{
	struct i40e_pf *pf;
	int i;

	if (!veb || !veb->pf)
		return;
	pf = veb->pf;

	/* depth first... */
	for (i = 0; i < I40E_MAX_VEB; i++)
		if (pf->veb[i] && (pf->veb[i]->uplink_seid == veb->seid))
			i40e_veb_link_event(pf->veb[i], link_up);

	/* ... now the local VSIs */
	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i] && (pf->vsi[i]->uplink_seid == veb->seid))
			i40e_vsi_link_event(pf->vsi[i], link_up);
}

/**
 * i40e_link_event - Update netif_carrier status
 * @pf: board private structure
 **/
static void i40e_link_event(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	u8 new_link_speed, old_link_speed;
	i40e_status status;
	bool new_link, old_link;

	/* set this to force the get_link_status call to refresh state */
	pf->hw.phy.get_link_info = true;
	old_link = (pf->hw.phy.link_info_old.link_info & I40E_AQ_LINK_UP);
	status = i40e_get_link_status(&pf->hw, &new_link);

	/* On success, disable temp link polling */
	if (status == I40E_SUCCESS) {
		clear_bit(__I40E_TEMP_LINK_POLLING, pf->state);
	} else {
		/* Enable link polling temporarily until i40e_get_link_status
		 * returns I40E_SUCCESS
		 */
		set_bit(__I40E_TEMP_LINK_POLLING, pf->state);
		dev_dbg(&pf->pdev->dev, "couldn't get link state, status: %d\n",
			status);
		return;
	}

	old_link_speed = pf->hw.phy.link_info_old.link_speed;
	new_link_speed = pf->hw.phy.link_info.link_speed;

	if (new_link == old_link &&
	    new_link_speed == old_link_speed &&
	    (test_bit(__I40E_VSI_DOWN, vsi->state) ||
	     new_link == netif_carrier_ok(vsi->netdev)))
		return;

	i40e_print_link_message(vsi, new_link);

	/* Notify the base of the switch tree connected to
	 * the link.  Floating VEBs are not notified.
	 */
	if (pf->lan_veb < I40E_MAX_VEB && pf->veb[pf->lan_veb])
		i40e_veb_link_event(pf->veb[pf->lan_veb], new_link);
	else
		i40e_vsi_link_event(vsi, new_link);

	if (pf->vf)
		i40e_vc_notify_link_state(pf);

	if (pf->flags & I40E_FLAG_PTP)
		i40e_ptp_set_increment(pf);
}

/**
 * i40e_watchdog_subtask - periodic checks not using event driven response
 * @pf: board private structure
 **/
static void i40e_watchdog_subtask(struct i40e_pf *pf)
{
	int i;

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, pf->state) ||
	    test_bit(__I40E_CONFIG_BUSY, pf->state))
		return;

	/* make sure we don't do these things too often */
	if (time_before(jiffies, (pf->service_timer_previous +
				  pf->service_timer_period)))
		return;
	pf->service_timer_previous = jiffies;

	if ((pf->flags & I40E_FLAG_LINK_POLLING_ENABLED) ||
	    test_bit(__I40E_TEMP_LINK_POLLING, pf->state))
		i40e_link_event(pf);

	/* Update the stats for active netdevs so the network stack
	 * can look at updated numbers whenever it cares to
	 */
	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i] && pf->vsi[i]->netdev)
			i40e_update_stats(pf->vsi[i]);

	if (pf->flags & I40E_FLAG_VEB_STATS_ENABLED) {
		/* Update the stats for the active switching components */
		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i])
				i40e_update_veb_stats(pf->veb[i]);
	}

	i40e_ptp_rx_hang(pf);
	i40e_ptp_tx_hang(pf);
}

/**
 * i40e_reset_subtask - Set up for resetting the device and driver
 * @pf: board private structure
 **/
static void i40e_reset_subtask(struct i40e_pf *pf)
{
	u32 reset_flags = 0;

	if (test_bit(__I40E_REINIT_REQUESTED, pf->state)) {
		reset_flags |= BIT(__I40E_REINIT_REQUESTED);
		clear_bit(__I40E_REINIT_REQUESTED, pf->state);
	}
	if (test_bit(__I40E_PF_RESET_REQUESTED, pf->state)) {
		reset_flags |= BIT(__I40E_PF_RESET_REQUESTED);
		clear_bit(__I40E_PF_RESET_REQUESTED, pf->state);
	}
	if (test_bit(__I40E_CORE_RESET_REQUESTED, pf->state)) {
		reset_flags |= BIT(__I40E_CORE_RESET_REQUESTED);
		clear_bit(__I40E_CORE_RESET_REQUESTED, pf->state);
	}
	if (test_bit(__I40E_GLOBAL_RESET_REQUESTED, pf->state)) {
		reset_flags |= BIT(__I40E_GLOBAL_RESET_REQUESTED);
		clear_bit(__I40E_GLOBAL_RESET_REQUESTED, pf->state);
	}
	if (test_bit(__I40E_DOWN_REQUESTED, pf->state)) {
		reset_flags |= BIT(__I40E_DOWN_REQUESTED);
		clear_bit(__I40E_DOWN_REQUESTED, pf->state);
	}

	/* If there's a recovery already waiting, it takes
	 * precedence before starting a new reset sequence.
	 */
	if (test_bit(__I40E_RESET_INTR_RECEIVED, pf->state)) {
		i40e_prep_for_reset(pf, false);
		i40e_reset(pf);
		i40e_rebuild(pf, false, false);
	}

	/* If we're already down or resetting, just bail */
	if (reset_flags &&
	    !test_bit(__I40E_DOWN, pf->state) &&
	    !test_bit(__I40E_CONFIG_BUSY, pf->state)) {
		i40e_do_reset(pf, reset_flags, false);
	}
}

/**
 * i40e_handle_link_event - Handle link event
 * @pf: board private structure
 * @e: event info posted on ARQ
 **/
static void i40e_handle_link_event(struct i40e_pf *pf,
				   struct i40e_arq_event_info *e)
{
	struct i40e_aqc_get_link_status *status =
		(struct i40e_aqc_get_link_status *)&e->desc.params.raw;

	/* Do a new status request to re-enable LSE reporting
	 * and load new status information into the hw struct
	 * This completely ignores any state information
	 * in the ARQ event info, instead choosing to always
	 * issue the AQ update link status command.
	 */
	i40e_link_event(pf);

	/* Check if module meets thermal requirements */
	if (status->phy_type == I40E_PHY_TYPE_NOT_SUPPORTED_HIGH_TEMP) {
		dev_err(&pf->pdev->dev,
			"Rx/Tx is disabled on this device because the module does not meet thermal requirements.\n");
		dev_err(&pf->pdev->dev,
			"Refer to the Intel(R) Ethernet Adapters and Devices User Guide for a list of supported modules.\n");
	} else {
		/* check for unqualified module, if link is down, suppress
		 * the message if link was forced to be down.
		 */
		if ((status->link_info & I40E_AQ_MEDIA_AVAILABLE) &&
		    (!(status->an_info & I40E_AQ_QUALIFIED_MODULE)) &&
		    (!(status->link_info & I40E_AQ_LINK_UP)) &&
		    (!(pf->flags & I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED))) {
			dev_err(&pf->pdev->dev,
				"Rx/Tx is disabled on this device because an unsupported SFP module type was detected.\n");
			dev_err(&pf->pdev->dev,
				"Refer to the Intel(R) Ethernet Adapters and Devices User Guide for a list of supported modules.\n");
		}
	}
}

/**
 * i40e_clean_adminq_subtask - Clean the AdminQ rings
 * @pf: board private structure
 **/
static void i40e_clean_adminq_subtask(struct i40e_pf *pf)
{
	struct i40e_arq_event_info event;
	struct i40e_hw *hw = &pf->hw;
	u16 pending, i = 0;
	i40e_status ret;
	u16 opcode;
	u32 oldval;
	u32 val;

	/* Do not run clean AQ when PF reset fails */
	if (test_bit(__I40E_RESET_FAILED, pf->state))
		return;

	/* check for error indications */
	val = rd32(&pf->hw, pf->hw.aq.arq.len);
	oldval = val;
	if (val & I40E_PF_ARQLEN_ARQVFE_MASK) {
		if (hw->debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ARQ VF Error detected\n");
		val &= ~I40E_PF_ARQLEN_ARQVFE_MASK;
	}
	if (val & I40E_PF_ARQLEN_ARQOVFL_MASK) {
		if (hw->debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ARQ Overflow Error detected\n");
		val &= ~I40E_PF_ARQLEN_ARQOVFL_MASK;
		pf->arq_overflows++;
	}
	if (val & I40E_PF_ARQLEN_ARQCRIT_MASK) {
		if (hw->debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ARQ Critical Error detected\n");
		val &= ~I40E_PF_ARQLEN_ARQCRIT_MASK;
	}
	if (oldval != val)
		wr32(&pf->hw, pf->hw.aq.arq.len, val);

	val = rd32(&pf->hw, pf->hw.aq.asq.len);
	oldval = val;
	if (val & I40E_PF_ATQLEN_ATQVFE_MASK) {
		if (pf->hw.debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ASQ VF Error detected\n");
		val &= ~I40E_PF_ATQLEN_ATQVFE_MASK;
	}
	if (val & I40E_PF_ATQLEN_ATQOVFL_MASK) {
		if (pf->hw.debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ASQ Overflow Error detected\n");
		val &= ~I40E_PF_ATQLEN_ATQOVFL_MASK;
	}
	if (val & I40E_PF_ATQLEN_ATQCRIT_MASK) {
		if (pf->hw.debug_mask & I40E_DEBUG_AQ)
			dev_info(&pf->pdev->dev, "ASQ Critical Error detected\n");
		val &= ~I40E_PF_ATQLEN_ATQCRIT_MASK;
	}
	if (oldval != val)
		wr32(&pf->hw, pf->hw.aq.asq.len, val);

	event.buf_len = I40E_MAX_AQ_BUF_SIZE;
	event.msg_buf = kzalloc(event.buf_len, GFP_KERNEL);
	if (!event.msg_buf)
		return;

	do {
		ret = i40e_clean_arq_element(hw, &event, &pending);
		if (ret == I40E_ERR_ADMIN_QUEUE_NO_WORK)
			break;
		else if (ret) {
			dev_info(&pf->pdev->dev, "ARQ event error %d\n", ret);
			break;
		}

		opcode = le16_to_cpu(event.desc.opcode);
		switch (opcode) {

		case i40e_aqc_opc_get_link_status:
			i40e_handle_link_event(pf, &event);
			break;
		case i40e_aqc_opc_send_msg_to_pf:
			ret = i40e_vc_process_vf_msg(pf,
					le16_to_cpu(event.desc.retval),
					le32_to_cpu(event.desc.cookie_high),
					le32_to_cpu(event.desc.cookie_low),
					event.msg_buf,
					event.msg_len);
			break;
		case i40e_aqc_opc_lldp_update_mib:
			dev_dbg(&pf->pdev->dev, "ARQ: Update LLDP MIB event received\n");
#ifdef CONFIG_I40E_DCB
			rtnl_lock();
			ret = i40e_handle_lldp_event(pf, &event);
			rtnl_unlock();
#endif /* CONFIG_I40E_DCB */
			break;
		case i40e_aqc_opc_event_lan_overflow:
			dev_dbg(&pf->pdev->dev, "ARQ LAN queue overflow event received\n");
			i40e_handle_lan_overflow_event(pf, &event);
			break;
		case i40e_aqc_opc_send_msg_to_peer:
			dev_info(&pf->pdev->dev, "ARQ: Msg from other pf\n");
			break;
		case i40e_aqc_opc_nvm_erase:
		case i40e_aqc_opc_nvm_update:
		case i40e_aqc_opc_oem_post_update:
			i40e_debug(&pf->hw, I40E_DEBUG_NVM,
				   "ARQ NVM operation 0x%04x completed\n",
				   opcode);
			break;
		default:
			dev_info(&pf->pdev->dev,
				 "ARQ: Unknown event 0x%04x ignored\n",
				 opcode);
			break;
		}
	} while (i++ < pf->adminq_work_limit);

	if (i < pf->adminq_work_limit)
		clear_bit(__I40E_ADMINQ_EVENT_PENDING, pf->state);

	/* re-enable Admin queue interrupt cause */
	val = rd32(hw, I40E_PFINT_ICR0_ENA);
	val |=  I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, val);
	i40e_flush(hw);

	kfree(event.msg_buf);
}

/**
 * i40e_verify_eeprom - make sure eeprom is good to use
 * @pf: board private structure
 **/
static void i40e_verify_eeprom(struct i40e_pf *pf)
{
	int err;

	err = i40e_diag_eeprom_test(&pf->hw);
	if (err) {
		/* retry in case of garbage read */
		err = i40e_diag_eeprom_test(&pf->hw);
		if (err) {
			dev_info(&pf->pdev->dev, "eeprom check failed (%d), Tx/Rx traffic disabled\n",
				 err);
			set_bit(__I40E_BAD_EEPROM, pf->state);
		}
	}

	if (!err && test_bit(__I40E_BAD_EEPROM, pf->state)) {
		dev_info(&pf->pdev->dev, "eeprom check passed, Tx/Rx traffic enabled\n");
		clear_bit(__I40E_BAD_EEPROM, pf->state);
	}
}

/**
 * i40e_enable_pf_switch_lb
 * @pf: pointer to the PF structure
 *
 * enable switch loop back or die - no point in a return value
 **/
static void i40e_enable_pf_switch_lb(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	struct i40e_vsi_context ctxt;
	int ret;

	ctxt.seid = pf->main_vsi_seid;
	ctxt.pf_num = pf->hw.pf_id;
	ctxt.vf_num = 0;
	ret = i40e_aq_get_vsi_params(&pf->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get PF vsi config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return;
	}
	ctxt.flags = I40E_AQ_VSI_TYPE_PF;
	ctxt.info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
	ctxt.info.switch_id |= cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);

	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "update vsi switch failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	}
}

/**
 * i40e_disable_pf_switch_lb
 * @pf: pointer to the PF structure
 *
 * disable switch loop back or die - no point in a return value
 **/
static void i40e_disable_pf_switch_lb(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	struct i40e_vsi_context ctxt;
	int ret;

	ctxt.seid = pf->main_vsi_seid;
	ctxt.pf_num = pf->hw.pf_id;
	ctxt.vf_num = 0;
	ret = i40e_aq_get_vsi_params(&pf->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get PF vsi config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return;
	}
	ctxt.flags = I40E_AQ_VSI_TYPE_PF;
	ctxt.info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
	ctxt.info.switch_id &= ~cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);

	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "update vsi switch failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	}
}

/**
 * i40e_config_bridge_mode - Configure the HW bridge mode
 * @veb: pointer to the bridge instance
 *
 * Configure the loop back mode for the LAN VSI that is downlink to the
 * specified HW bridge instance. It is expected this function is called
 * when a new HW bridge is instantiated.
 **/
static void i40e_config_bridge_mode(struct i40e_veb *veb)
{
	struct i40e_pf *pf = veb->pf;

	if (pf->hw.debug_mask & I40E_DEBUG_LAN)
		dev_info(&pf->pdev->dev, "enabling bridge mode: %s\n",
			 veb->bridge_mode == BRIDGE_MODE_VEPA ? "VEPA" : "VEB");
	if (veb->bridge_mode & BRIDGE_MODE_VEPA)
		i40e_disable_pf_switch_lb(pf);
	else
		i40e_enable_pf_switch_lb(pf);
}

/**
 * i40e_reconstitute_veb - rebuild the VEB and anything connected to it
 * @veb: pointer to the VEB instance
 *
 * This is a recursive function that first builds the attached VSIs then
 * recurses in to build the next layer of VEB.  We track the connections
 * through our own index numbers because the seid's from the HW could
 * change across the reset.
 **/
static int i40e_reconstitute_veb(struct i40e_veb *veb)
{
	struct i40e_vsi *ctl_vsi = NULL;
	struct i40e_pf *pf = veb->pf;
	int v, veb_idx;
	int ret;

	/* build VSI that owns this VEB, temporarily attached to base VEB */
	for (v = 0; v < pf->num_alloc_vsi && !ctl_vsi; v++) {
		if (pf->vsi[v] &&
		    pf->vsi[v]->veb_idx == veb->idx &&
		    pf->vsi[v]->flags & I40E_VSI_FLAG_VEB_OWNER) {
			ctl_vsi = pf->vsi[v];
			break;
		}
	}
	if (!ctl_vsi) {
		dev_info(&pf->pdev->dev,
			 "missing owner VSI for veb_idx %d\n", veb->idx);
		ret = -ENOENT;
		goto end_reconstitute;
	}
	if (ctl_vsi != pf->vsi[pf->lan_vsi])
		ctl_vsi->uplink_seid = pf->vsi[pf->lan_vsi]->uplink_seid;
	ret = i40e_add_vsi(ctl_vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "rebuild of veb_idx %d owner VSI failed: %d\n",
			 veb->idx, ret);
		goto end_reconstitute;
	}
	i40e_vsi_reset_stats(ctl_vsi);

	/* create the VEB in the switch and move the VSI onto the VEB */
	ret = i40e_add_veb(veb, ctl_vsi);
	if (ret)
		goto end_reconstitute;

	if (pf->flags & I40E_FLAG_VEB_MODE_ENABLED)
		veb->bridge_mode = BRIDGE_MODE_VEB;
	else
		veb->bridge_mode = BRIDGE_MODE_VEPA;
	i40e_config_bridge_mode(veb);

	/* create the remaining VSIs attached to this VEB */
	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (!pf->vsi[v] || pf->vsi[v] == ctl_vsi)
			continue;

		if (pf->vsi[v]->veb_idx == veb->idx) {
			struct i40e_vsi *vsi = pf->vsi[v];

			vsi->uplink_seid = veb->seid;
			ret = i40e_add_vsi(vsi);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "rebuild of vsi_idx %d failed: %d\n",
					 v, ret);
				goto end_reconstitute;
			}
			i40e_vsi_reset_stats(vsi);
		}
	}

	/* create any VEBs attached to this VEB - RECURSION */
	for (veb_idx = 0; veb_idx < I40E_MAX_VEB; veb_idx++) {
		if (pf->veb[veb_idx] && pf->veb[veb_idx]->veb_idx == veb->idx) {
			pf->veb[veb_idx]->uplink_seid = veb->seid;
			ret = i40e_reconstitute_veb(pf->veb[veb_idx]);
			if (ret)
				break;
		}
	}

end_reconstitute:
	return ret;
}

/**
 * i40e_get_capabilities - get info about the HW
 * @pf: the PF struct
 * @list_type: AQ capability to be queried
 **/
static int i40e_get_capabilities(struct i40e_pf *pf,
				 enum i40e_admin_queue_opc list_type)
{
	struct i40e_aqc_list_capabilities_element_resp *cap_buf;
	u16 data_size;
	int buf_len;
	int err;

	buf_len = 40 * sizeof(struct i40e_aqc_list_capabilities_element_resp);
	do {
		cap_buf = kzalloc(buf_len, GFP_KERNEL);
		if (!cap_buf)
			return -ENOMEM;

		/* this loads the data into the hw struct for us */
		err = i40e_aq_discover_capabilities(&pf->hw, cap_buf, buf_len,
						    &data_size, list_type,
						    NULL);
		/* data loaded, buffer no longer needed */
		kfree(cap_buf);

		if (pf->hw.aq.asq_last_status == I40E_AQ_RC_ENOMEM) {
			/* retry with a larger buffer */
			buf_len = data_size;
		} else if (pf->hw.aq.asq_last_status != I40E_AQ_RC_OK || err) {
			dev_info(&pf->pdev->dev,
				 "capability discovery failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, err),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			return -ENODEV;
		}
	} while (err);

	if (pf->hw.debug_mask & I40E_DEBUG_USER) {
		if (list_type == i40e_aqc_opc_list_func_capabilities) {
			dev_info(&pf->pdev->dev,
				 "pf=%d, num_vfs=%d, msix_pf=%d, msix_vf=%d, fd_g=%d, fd_b=%d, pf_max_q=%d num_vsi=%d\n",
				 pf->hw.pf_id, pf->hw.func_caps.num_vfs,
				 pf->hw.func_caps.num_msix_vectors,
				 pf->hw.func_caps.num_msix_vectors_vf,
				 pf->hw.func_caps.fd_filters_guaranteed,
				 pf->hw.func_caps.fd_filters_best_effort,
				 pf->hw.func_caps.num_tx_qp,
				 pf->hw.func_caps.num_vsis);
		} else if (list_type == i40e_aqc_opc_list_dev_capabilities) {
			dev_info(&pf->pdev->dev,
				 "switch_mode=0x%04x, function_valid=0x%08x\n",
				 pf->hw.dev_caps.switch_mode,
				 pf->hw.dev_caps.valid_functions);
			dev_info(&pf->pdev->dev,
				 "SR-IOV=%d, num_vfs for all function=%u\n",
				 pf->hw.dev_caps.sr_iov_1_1,
				 pf->hw.dev_caps.num_vfs);
			dev_info(&pf->pdev->dev,
				 "num_vsis=%u, num_rx:%u, num_tx=%u\n",
				 pf->hw.dev_caps.num_vsis,
				 pf->hw.dev_caps.num_rx_qp,
				 pf->hw.dev_caps.num_tx_qp);
		}
	}
	if (list_type == i40e_aqc_opc_list_func_capabilities) {
#define DEF_NUM_VSI (1 + (pf->hw.func_caps.fcoe ? 1 : 0) \
		       + pf->hw.func_caps.num_vfs)
		if (pf->hw.revision_id == 0 &&
		    pf->hw.func_caps.num_vsis < DEF_NUM_VSI) {
			dev_info(&pf->pdev->dev,
				 "got num_vsis %d, setting num_vsis to %d\n",
				 pf->hw.func_caps.num_vsis, DEF_NUM_VSI);
			pf->hw.func_caps.num_vsis = DEF_NUM_VSI;
		}
	}
	return 0;
}

static int i40e_vsi_clear(struct i40e_vsi *vsi);

/**
 * i40e_fdir_sb_setup - initialize the Flow Director resources for Sideband
 * @pf: board private structure
 **/
static void i40e_fdir_sb_setup(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi;

	/* quick workaround for an NVM issue that leaves a critical register
	 * uninitialized
	 */
	if (!rd32(&pf->hw, I40E_GLQF_HKEY(0))) {
		static const u32 hkey[] = {
			0xe640d33f, 0xcdfe98ab, 0x73fa7161, 0x0d7a7d36,
			0xeacb7d61, 0xaa4f05b6, 0x9c5c89ed, 0xfc425ddb,
			0xa4654832, 0xfc7461d4, 0x8f827619, 0xf5c63c21,
			0x95b3a76d};
		int i;

		for (i = 0; i <= I40E_GLQF_HKEY_MAX_INDEX; i++)
			wr32(&pf->hw, I40E_GLQF_HKEY(i), hkey[i]);
	}

	if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
		return;

	/* find existing VSI and see if it needs configuring */
	vsi = i40e_find_vsi_by_type(pf, I40E_VSI_FDIR);

	/* create a new VSI if none exists */
	if (!vsi) {
		vsi = i40e_vsi_setup(pf, I40E_VSI_FDIR,
				     pf->vsi[pf->lan_vsi]->seid, 0);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "Couldn't create FDir VSI\n");
			pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
			pf->flags |= I40E_FLAG_FD_SB_INACTIVE;
			return;
		}
	}

	i40e_vsi_setup_irqhandler(vsi, i40e_fdir_clean_ring);
}

/**
 * i40e_fdir_teardown - release the Flow Director resources
 * @pf: board private structure
 **/
static void i40e_fdir_teardown(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi;

	i40e_fdir_filter_exit(pf);
	vsi = i40e_find_vsi_by_type(pf, I40E_VSI_FDIR);
	if (vsi)
		i40e_vsi_release(vsi);
}

/**
 * i40e_rebuild_cloud_filters - Rebuilds cloud filters for VSIs
 * @vsi: PF main vsi
 * @seid: seid of main or channel VSIs
 *
 * Rebuilds cloud filters associated with main VSI and channel VSIs if they
 * existed before reset
 **/
static int i40e_rebuild_cloud_filters(struct i40e_vsi *vsi, u16 seid)
{
	struct i40e_cloud_filter *cfilter;
	struct i40e_pf *pf = vsi->back;
	struct hlist_node *node;
	i40e_status ret;

	/* Add cloud filters back if they exist */
	hlist_for_each_entry_safe(cfilter, node, &pf->cloud_filter_list,
				  cloud_node) {
		if (cfilter->seid != seid)
			continue;

		if (cfilter->dst_port)
			ret = i40e_add_del_cloud_filter_big_buf(vsi, cfilter,
								true);
		else
			ret = i40e_add_del_cloud_filter(vsi, cfilter, true);

		if (ret) {
			dev_dbg(&pf->pdev->dev,
				"Failed to rebuild cloud filter, err %s aq_err %s\n",
				i40e_stat_str(&pf->hw, ret),
				i40e_aq_str(&pf->hw,
					    pf->hw.aq.asq_last_status));
			return ret;
		}
	}
	return 0;
}

/**
 * i40e_rebuild_channels - Rebuilds channel VSIs if they existed before reset
 * @vsi: PF main vsi
 *
 * Rebuilds channel VSIs if they existed before reset
 **/
static int i40e_rebuild_channels(struct i40e_vsi *vsi)
{
	struct i40e_channel *ch, *ch_tmp;
	i40e_status ret;

	if (list_empty(&vsi->ch_list))
		return 0;

	list_for_each_entry_safe(ch, ch_tmp, &vsi->ch_list, list) {
		if (!ch->initialized)
			break;
		/* Proceed with creation of channel (VMDq2) VSI */
		ret = i40e_add_channel(vsi->back, vsi->uplink_seid, ch);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "failed to rebuild channels using uplink_seid %u\n",
				 vsi->uplink_seid);
			return ret;
		}
		/* Reconfigure TX queues using QTX_CTL register */
		ret = i40e_channel_config_tx_ring(vsi->back, vsi, ch);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "failed to configure TX rings for channel %u\n",
				 ch->seid);
			return ret;
		}
		/* update 'next_base_queue' */
		vsi->next_base_queue = vsi->next_base_queue +
							ch->num_queue_pairs;
		if (ch->max_tx_rate) {
			u64 credits = ch->max_tx_rate;

			if (i40e_set_bw_limit(vsi, ch->seid,
					      ch->max_tx_rate))
				return -EINVAL;

			do_div(credits, I40E_BW_CREDIT_DIVISOR);
			dev_dbg(&vsi->back->pdev->dev,
				"Set tx rate of %llu Mbps (count of 50Mbps %llu) for vsi->seid %u\n",
				ch->max_tx_rate,
				credits,
				ch->seid);
		}
		ret = i40e_rebuild_cloud_filters(vsi, ch->seid);
		if (ret) {
			dev_dbg(&vsi->back->pdev->dev,
				"Failed to rebuild cloud filters for channel VSI %u\n",
				ch->seid);
			return ret;
		}
	}
	return 0;
}

/**
 * i40e_prep_for_reset - prep for the core to reset
 * @pf: board private structure
 * @lock_acquired: indicates whether or not the lock has been acquired
 * before this function was called.
 *
 * Close up the VFs and other things in prep for PF Reset.
  **/
static void i40e_prep_for_reset(struct i40e_pf *pf, bool lock_acquired)
{
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret = 0;
	u32 v;

	clear_bit(__I40E_RESET_INTR_RECEIVED, pf->state);
	if (test_and_set_bit(__I40E_RESET_RECOVERY_PENDING, pf->state))
		return;
	if (i40e_check_asq_alive(&pf->hw))
		i40e_vc_notify_reset(pf);

	dev_dbg(&pf->pdev->dev, "Tearing down internal switch for reset\n");

	/* quiesce the VSIs and their queues that are not already DOWN */
	/* pf_quiesce_all_vsi modifies netdev structures -rtnl_lock needed */
	if (!lock_acquired)
		rtnl_lock();
	i40e_pf_quiesce_all_vsi(pf);
	if (!lock_acquired)
		rtnl_unlock();

	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v])
			pf->vsi[v]->seid = 0;
	}

	i40e_shutdown_adminq(&pf->hw);

	/* call shutdown HMC */
	if (hw->hmc.hmc_obj) {
		ret = i40e_shutdown_lan_hmc(hw);
		if (ret)
			dev_warn(&pf->pdev->dev,
				 "shutdown_lan_hmc failed: %d\n", ret);
	}

	/* Save the current PTP time so that we can restore the time after the
	 * reset completes.
	 */
	i40e_ptp_save_hw_time(pf);
}

/**
 * i40e_send_version - update firmware with driver version
 * @pf: PF struct
 */
static void i40e_send_version(struct i40e_pf *pf)
{
	struct i40e_driver_version dv;

	dv.major_version = 0xff;
	dv.minor_version = 0xff;
	dv.build_version = 0xff;
	dv.subbuild_version = 0;
	strlcpy(dv.driver_string, UTS_RELEASE, sizeof(dv.driver_string));
	i40e_aq_send_driver_version(&pf->hw, &dv, NULL);
}

/**
 * i40e_get_oem_version - get OEM specific version information
 * @hw: pointer to the hardware structure
 **/
static void i40e_get_oem_version(struct i40e_hw *hw)
{
	u16 block_offset = 0xffff;
	u16 block_length = 0;
	u16 capabilities = 0;
	u16 gen_snap = 0;
	u16 release = 0;

#define I40E_SR_NVM_OEM_VERSION_PTR		0x1B
#define I40E_NVM_OEM_LENGTH_OFFSET		0x00
#define I40E_NVM_OEM_CAPABILITIES_OFFSET	0x01
#define I40E_NVM_OEM_GEN_OFFSET			0x02
#define I40E_NVM_OEM_RELEASE_OFFSET		0x03
#define I40E_NVM_OEM_CAPABILITIES_MASK		0x000F
#define I40E_NVM_OEM_LENGTH			3

	/* Check if pointer to OEM version block is valid. */
	i40e_read_nvm_word(hw, I40E_SR_NVM_OEM_VERSION_PTR, &block_offset);
	if (block_offset == 0xffff)
		return;

	/* Check if OEM version block has correct length. */
	i40e_read_nvm_word(hw, block_offset + I40E_NVM_OEM_LENGTH_OFFSET,
			   &block_length);
	if (block_length < I40E_NVM_OEM_LENGTH)
		return;

	/* Check if OEM version format is as expected. */
	i40e_read_nvm_word(hw, block_offset + I40E_NVM_OEM_CAPABILITIES_OFFSET,
			   &capabilities);
	if ((capabilities & I40E_NVM_OEM_CAPABILITIES_MASK) != 0)
		return;

	i40e_read_nvm_word(hw, block_offset + I40E_NVM_OEM_GEN_OFFSET,
			   &gen_snap);
	i40e_read_nvm_word(hw, block_offset + I40E_NVM_OEM_RELEASE_OFFSET,
			   &release);
	hw->nvm.oem_ver = (gen_snap << I40E_OEM_SNAP_SHIFT) | release;
	hw->nvm.eetrack = I40E_OEM_EETRACK_ID;
}

/**
 * i40e_reset - wait for core reset to finish reset, reset pf if corer not seen
 * @pf: board private structure
 **/
static int i40e_reset(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;

	ret = i40e_pf_reset(hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "PF reset failed, %d\n", ret);
		set_bit(__I40E_RESET_FAILED, pf->state);
		clear_bit(__I40E_RESET_RECOVERY_PENDING, pf->state);
	} else {
		pf->pfr_count++;
	}
	return ret;
}

/**
 * i40e_rebuild - rebuild using a saved config
 * @pf: board private structure
 * @reinit: if the Main VSI needs to re-initialized.
 * @lock_acquired: indicates whether or not the lock has been acquired
 * before this function was called.
 **/
static void i40e_rebuild(struct i40e_pf *pf, bool reinit, bool lock_acquired)
{
	int old_recovery_mode_bit = test_bit(__I40E_RECOVERY_MODE, pf->state);
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;
	u32 val;
	int v;

	if (test_bit(__I40E_EMP_RESET_INTR_RECEIVED, pf->state) &&
	    i40e_check_recovery_mode(pf)) {
		i40e_set_ethtool_ops(pf->vsi[pf->lan_vsi]->netdev);
	}

	if (test_bit(__I40E_DOWN, pf->state) &&
	    !test_bit(__I40E_RECOVERY_MODE, pf->state) &&
	    !old_recovery_mode_bit)
		goto clear_recovery;
	dev_dbg(&pf->pdev->dev, "Rebuilding internal switch\n");

	/* rebuild the basics for the AdminQ, HMC, and initial HW switch */
	ret = i40e_init_adminq(&pf->hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "Rebuild AdminQ failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		goto clear_recovery;
	}
	i40e_get_oem_version(&pf->hw);

	if (test_and_clear_bit(__I40E_EMP_RESET_INTR_RECEIVED, pf->state)) {
		/* The following delay is necessary for firmware update. */
		mdelay(1000);
	}

	/* re-verify the eeprom if we just had an EMP reset */
	if (test_and_clear_bit(__I40E_EMP_RESET_INTR_RECEIVED, pf->state))
		i40e_verify_eeprom(pf);

	/* if we are going out of or into recovery mode we have to act
	 * accordingly with regard to resources initialization
	 * and deinitialization
	 */
	if (test_bit(__I40E_RECOVERY_MODE, pf->state) ||
	    old_recovery_mode_bit) {
		if (i40e_get_capabilities(pf,
					  i40e_aqc_opc_list_func_capabilities))
			goto end_unlock;

		if (test_bit(__I40E_RECOVERY_MODE, pf->state)) {
			/* we're staying in recovery mode so we'll reinitialize
			 * misc vector here
			 */
			if (i40e_setup_misc_vector_for_recovery_mode(pf))
				goto end_unlock;
		} else {
			if (!lock_acquired)
				rtnl_lock();
			/* we're going out of recovery mode so we'll free
			 * the IRQ allocated specifically for recovery mode
			 * and restore the interrupt scheme
			 */
			free_irq(pf->pdev->irq, pf);
			i40e_clear_interrupt_scheme(pf);
			if (i40e_restore_interrupt_scheme(pf))
				goto end_unlock;
		}

		/* tell the firmware that we're starting */
		i40e_send_version(pf);

		/* bail out in case recovery mode was detected, as there is
		 * no need for further configuration.
		 */
		goto end_unlock;
	}

	i40e_clear_pxe_mode(hw);
	ret = i40e_get_capabilities(pf, i40e_aqc_opc_list_func_capabilities);
	if (ret)
		goto end_core_reset;

	ret = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp, 0, 0);
	if (ret) {
		dev_info(&pf->pdev->dev, "init_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}
	ret = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (ret) {
		dev_info(&pf->pdev->dev, "configure_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}

	/* Enable FW to write a default DCB config on link-up */
	i40e_aq_set_dcb_parameters(hw, true, NULL);

#ifdef CONFIG_I40E_DCB
	ret = i40e_init_pf_dcb(pf);
	if (ret) {
		dev_info(&pf->pdev->dev, "DCB init failed %d, disabled\n", ret);
		pf->flags &= ~I40E_FLAG_DCB_CAPABLE;
		/* Continue without DCB enabled */
	}
#endif /* CONFIG_I40E_DCB */
	/* do basic switch setup */
	if (!lock_acquired)
		rtnl_lock();
	ret = i40e_setup_pf_switch(pf, reinit, true);
	if (ret)
		goto end_unlock;

	/* The driver only wants link up/down and module qualification
	 * reports from firmware.  Note the negative logic.
	 */
	ret = i40e_aq_set_phy_int_mask(&pf->hw,
				       ~(I40E_AQ_EVENT_LINK_UPDOWN |
					 I40E_AQ_EVENT_MEDIA_NA |
					 I40E_AQ_EVENT_MODULE_QUAL_FAIL), NULL);
	if (ret)
		dev_info(&pf->pdev->dev, "set phy mask fail, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

	/* Rebuild the VSIs and VEBs that existed before reset.
	 * They are still in our local switch element arrays, so only
	 * need to rebuild the switch model in the HW.
	 *
	 * If there were VEBs but the reconstitution failed, we'll try
	 * try to recover minimal use by getting the basic PF VSI working.
	 */
	if (vsi->uplink_seid != pf->mac_seid) {
		dev_dbg(&pf->pdev->dev, "attempting to rebuild switch\n");
		/* find the one VEB connected to the MAC, and find orphans */
		for (v = 0; v < I40E_MAX_VEB; v++) {
			if (!pf->veb[v])
				continue;

			if (pf->veb[v]->uplink_seid == pf->mac_seid ||
			    pf->veb[v]->uplink_seid == 0) {
				ret = i40e_reconstitute_veb(pf->veb[v]);

				if (!ret)
					continue;

				/* If Main VEB failed, we're in deep doodoo,
				 * so give up rebuilding the switch and set up
				 * for minimal rebuild of PF VSI.
				 * If orphan failed, we'll report the error
				 * but try to keep going.
				 */
				if (pf->veb[v]->uplink_seid == pf->mac_seid) {
					dev_info(&pf->pdev->dev,
						 "rebuild of switch failed: %d, will try to set up simple PF connection\n",
						 ret);
					vsi->uplink_seid = pf->mac_seid;
					break;
				} else if (pf->veb[v]->uplink_seid == 0) {
					dev_info(&pf->pdev->dev,
						 "rebuild of orphan VEB failed: %d\n",
						 ret);
				}
			}
		}
	}

	if (vsi->uplink_seid == pf->mac_seid) {
		dev_dbg(&pf->pdev->dev, "attempting to rebuild PF VSI\n");
		/* no VEB, so rebuild only the Main VSI */
		ret = i40e_add_vsi(vsi);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "rebuild of Main VSI failed: %d\n", ret);
			goto end_unlock;
		}
	}

	if (vsi->mqprio_qopt.max_rate[0]) {
		u64 max_tx_rate = vsi->mqprio_qopt.max_rate[0];
		u64 credits = 0;

		do_div(max_tx_rate, I40E_BW_MBPS_DIVISOR);
		ret = i40e_set_bw_limit(vsi, vsi->seid, max_tx_rate);
		if (ret)
			goto end_unlock;

		credits = max_tx_rate;
		do_div(credits, I40E_BW_CREDIT_DIVISOR);
		dev_dbg(&vsi->back->pdev->dev,
			"Set tx rate of %llu Mbps (count of 50Mbps %llu) for vsi->seid %u\n",
			max_tx_rate,
			credits,
			vsi->seid);
	}

	ret = i40e_rebuild_cloud_filters(vsi, vsi->seid);
	if (ret)
		goto end_unlock;

	/* PF Main VSI is rebuild by now, go ahead and rebuild channel VSIs
	 * for this main VSI if they exist
	 */
	ret = i40e_rebuild_channels(vsi);
	if (ret)
		goto end_unlock;

	/* Reconfigure hardware for allowing smaller MSS in the case
	 * of TSO, so that we avoid the MDD being fired and causing
	 * a reset in the case of small MSS+TSO.
	 */
#define I40E_REG_MSS          0x000E64DC
#define I40E_REG_MSS_MIN_MASK 0x3FF0000
#define I40E_64BYTE_MSS       0x400000
	val = rd32(hw, I40E_REG_MSS);
	if ((val & I40E_REG_MSS_MIN_MASK) > I40E_64BYTE_MSS) {
		val &= ~I40E_REG_MSS_MIN_MASK;
		val |= I40E_64BYTE_MSS;
		wr32(hw, I40E_REG_MSS, val);
	}

	if (pf->hw_features & I40E_HW_RESTART_AUTONEG) {
		msleep(75);
		ret = i40e_aq_set_link_restart_an(&pf->hw, true, NULL);
		if (ret)
			dev_info(&pf->pdev->dev, "link restart failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
	}
	/* reinit the misc interrupt */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		ret = i40e_setup_misc_vector(pf);

	/* Add a filter to drop all Flow control frames from any VSI from being
	 * transmitted. By doing so we stop a malicious VF from sending out
	 * PAUSE or PFC frames and potentially controlling traffic for other
	 * PF/VF VSIs.
	 * The FW can still send Flow control frames if enabled.
	 */
	i40e_add_filter_to_drop_tx_flow_control_frames(&pf->hw,
						       pf->main_vsi_seid);

	/* restart the VSIs that were rebuilt and running before the reset */
	i40e_pf_unquiesce_all_vsi(pf);

	/* Release the RTNL lock before we start resetting VFs */
	if (!lock_acquired)
		rtnl_unlock();

	/* Restore promiscuous settings */
	ret = i40e_set_promiscuous(pf, pf->cur_promisc);
	if (ret)
		dev_warn(&pf->pdev->dev,
			 "Failed to restore promiscuous setting: %s, err %s aq_err %s\n",
			 pf->cur_promisc ? "on" : "off",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

	i40e_reset_all_vfs(pf, true);

	/* tell the firmware that we're starting */
	i40e_send_version(pf);

	/* We've already released the lock, so don't do it again */
	goto end_core_reset;

end_unlock:
	if (!lock_acquired)
		rtnl_unlock();
end_core_reset:
	clear_bit(__I40E_RESET_FAILED, pf->state);
clear_recovery:
	clear_bit(__I40E_RESET_RECOVERY_PENDING, pf->state);
	clear_bit(__I40E_TIMEOUT_RECOVERY_PENDING, pf->state);
}

/**
 * i40e_reset_and_rebuild - reset and rebuild using a saved config
 * @pf: board private structure
 * @reinit: if the Main VSI needs to re-initialized.
 * @lock_acquired: indicates whether or not the lock has been acquired
 * before this function was called.
 **/
static void i40e_reset_and_rebuild(struct i40e_pf *pf, bool reinit,
				   bool lock_acquired)
{
	int ret;
	/* Now we wait for GRST to settle out.
	 * We don't have to delete the VEBs or VSIs from the hw switch
	 * because the reset will make them disappear.
	 */
	ret = i40e_reset(pf);
	if (!ret)
		i40e_rebuild(pf, reinit, lock_acquired);
}

/**
 * i40e_handle_reset_warning - prep for the PF to reset, reset and rebuild
 * @pf: board private structure
 *
 * Close up the VFs and other things in prep for a Core Reset,
 * then get ready to rebuild the world.
 * @lock_acquired: indicates whether or not the lock has been acquired
 * before this function was called.
 **/
static void i40e_handle_reset_warning(struct i40e_pf *pf, bool lock_acquired)
{
	i40e_prep_for_reset(pf, lock_acquired);
	i40e_reset_and_rebuild(pf, false, lock_acquired);
}

/**
 * i40e_handle_mdd_event
 * @pf: pointer to the PF structure
 *
 * Called from the MDD irq handler to identify possibly malicious vfs
 **/
static void i40e_handle_mdd_event(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	bool mdd_detected = false;
	struct i40e_vf *vf;
	u32 reg;
	int i;

	if (!test_bit(__I40E_MDD_EVENT_PENDING, pf->state))
		return;

	/* find what triggered the MDD event */
	reg = rd32(hw, I40E_GL_MDET_TX);
	if (reg & I40E_GL_MDET_TX_VALID_MASK) {
		u8 pf_num = (reg & I40E_GL_MDET_TX_PF_NUM_MASK) >>
				I40E_GL_MDET_TX_PF_NUM_SHIFT;
		u16 vf_num = (reg & I40E_GL_MDET_TX_VF_NUM_MASK) >>
				I40E_GL_MDET_TX_VF_NUM_SHIFT;
		u8 event = (reg & I40E_GL_MDET_TX_EVENT_MASK) >>
				I40E_GL_MDET_TX_EVENT_SHIFT;
		u16 queue = ((reg & I40E_GL_MDET_TX_QUEUE_MASK) >>
				I40E_GL_MDET_TX_QUEUE_SHIFT) -
				pf->hw.func_caps.base_queue;
		if (netif_msg_tx_err(pf))
			dev_info(&pf->pdev->dev, "Malicious Driver Detection event 0x%02x on TX queue %d PF number 0x%02x VF number 0x%02x\n",
				 event, queue, pf_num, vf_num);
		wr32(hw, I40E_GL_MDET_TX, 0xffffffff);
		mdd_detected = true;
	}
	reg = rd32(hw, I40E_GL_MDET_RX);
	if (reg & I40E_GL_MDET_RX_VALID_MASK) {
		u8 func = (reg & I40E_GL_MDET_RX_FUNCTION_MASK) >>
				I40E_GL_MDET_RX_FUNCTION_SHIFT;
		u8 event = (reg & I40E_GL_MDET_RX_EVENT_MASK) >>
				I40E_GL_MDET_RX_EVENT_SHIFT;
		u16 queue = ((reg & I40E_GL_MDET_RX_QUEUE_MASK) >>
				I40E_GL_MDET_RX_QUEUE_SHIFT) -
				pf->hw.func_caps.base_queue;
		if (netif_msg_rx_err(pf))
			dev_info(&pf->pdev->dev, "Malicious Driver Detection event 0x%02x on RX queue %d of function 0x%02x\n",
				 event, queue, func);
		wr32(hw, I40E_GL_MDET_RX, 0xffffffff);
		mdd_detected = true;
	}

	if (mdd_detected) {
		reg = rd32(hw, I40E_PF_MDET_TX);
		if (reg & I40E_PF_MDET_TX_VALID_MASK) {
			wr32(hw, I40E_PF_MDET_TX, 0xFFFF);
			dev_dbg(&pf->pdev->dev, "TX driver issue detected on PF\n");
		}
		reg = rd32(hw, I40E_PF_MDET_RX);
		if (reg & I40E_PF_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_PF_MDET_RX, 0xFFFF);
			dev_dbg(&pf->pdev->dev, "RX driver issue detected on PF\n");
		}
	}

	/* see if one of the VFs needs its hand slapped */
	for (i = 0; i < pf->num_alloc_vfs && mdd_detected; i++) {
		vf = &(pf->vf[i]);
		reg = rd32(hw, I40E_VP_MDET_TX(i));
		if (reg & I40E_VP_MDET_TX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_TX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "TX driver issue detected on VF %d\n",
				 i);
			dev_info(&pf->pdev->dev,
				 "Use PF Control I/F to re-enable the VF\n");
			set_bit(I40E_VF_STATE_DISABLED, &vf->vf_states);
		}

		reg = rd32(hw, I40E_VP_MDET_RX(i));
		if (reg & I40E_VP_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_RX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "RX driver issue detected on VF %d\n",
				 i);
			dev_info(&pf->pdev->dev,
				 "Use PF Control I/F to re-enable the VF\n");
			set_bit(I40E_VF_STATE_DISABLED, &vf->vf_states);
		}
	}

	/* re-enable mdd interrupt cause */
	clear_bit(__I40E_MDD_EVENT_PENDING, pf->state);
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |=  I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);
	i40e_flush(hw);
}

/**
 * i40e_service_task - Run the driver's async subtasks
 * @work: pointer to work_struct containing our data
 **/
static void i40e_service_task(struct work_struct *work)
{
	struct i40e_pf *pf = container_of(work,
					  struct i40e_pf,
					  service_task);
	unsigned long start_time = jiffies;

	/* don't bother with service tasks if a reset is in progress */
	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_SUSPENDED, pf->state))
		return;

	if (test_and_set_bit(__I40E_SERVICE_SCHED, pf->state))
		return;

	if (!test_bit(__I40E_RECOVERY_MODE, pf->state)) {
		i40e_detect_recover_hung(pf->vsi[pf->lan_vsi]);
		i40e_sync_filters_subtask(pf);
		i40e_reset_subtask(pf);
		i40e_handle_mdd_event(pf);
		i40e_vc_process_vflr_event(pf);
		i40e_watchdog_subtask(pf);
		i40e_fdir_reinit_subtask(pf);
		if (test_and_clear_bit(__I40E_CLIENT_RESET, pf->state)) {
			/* Client subtask will reopen next time through. */
			i40e_notify_client_of_netdev_close(pf->vsi[pf->lan_vsi],
							   true);
		} else {
			i40e_client_subtask(pf);
			if (test_and_clear_bit(__I40E_CLIENT_L2_CHANGE,
					       pf->state))
				i40e_notify_client_of_l2_param_changes(
								pf->vsi[pf->lan_vsi]);
		}
		i40e_sync_filters_subtask(pf);
	} else {
		i40e_reset_subtask(pf);
	}

	i40e_clean_adminq_subtask(pf);

	/* flush memory to make sure state is correct before next watchdog */
	smp_mb__before_atomic();
	clear_bit(__I40E_SERVICE_SCHED, pf->state);

	/* If the tasks have taken longer than one timer cycle or there
	 * is more work to be done, reschedule the service task now
	 * rather than wait for the timer to tick again.
	 */
	if (time_after(jiffies, (start_time + pf->service_timer_period)) ||
	    test_bit(__I40E_ADMINQ_EVENT_PENDING, pf->state)		 ||
	    test_bit(__I40E_MDD_EVENT_PENDING, pf->state)		 ||
	    test_bit(__I40E_VFLR_EVENT_PENDING, pf->state))
		i40e_service_event_schedule(pf);
}

/**
 * i40e_service_timer - timer callback
 * @t: timer list pointer
 **/
static void i40e_service_timer(struct timer_list *t)
{
	struct i40e_pf *pf = from_timer(pf, t, service_timer);

	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));
	i40e_service_event_schedule(pf);
}

/**
 * i40e_set_num_rings_in_vsi - Determine number of rings in the VSI
 * @vsi: the VSI being configured
 **/
static int i40e_set_num_rings_in_vsi(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	switch (vsi->type) {
	case I40E_VSI_MAIN:
		vsi->alloc_queue_pairs = pf->num_lan_qps;
		if (!vsi->num_tx_desc)
			vsi->num_tx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (!vsi->num_rx_desc)
			vsi->num_rx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			vsi->num_q_vectors = pf->num_lan_msix;
		else
			vsi->num_q_vectors = 1;

		break;

	case I40E_VSI_FDIR:
		vsi->alloc_queue_pairs = 1;
		vsi->num_tx_desc = ALIGN(I40E_FDIR_RING_COUNT,
					 I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_rx_desc = ALIGN(I40E_FDIR_RING_COUNT,
					 I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_fdsb_msix;
		break;

	case I40E_VSI_VMDQ2:
		vsi->alloc_queue_pairs = pf->num_vmdq_qps;
		if (!vsi->num_tx_desc)
			vsi->num_tx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (!vsi->num_rx_desc)
			vsi->num_rx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_vmdq_msix;
		break;

	case I40E_VSI_SRIOV:
		vsi->alloc_queue_pairs = pf->num_vf_qps;
		if (!vsi->num_tx_desc)
			vsi->num_tx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (!vsi->num_rx_desc)
			vsi->num_rx_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
						 I40E_REQ_DESCRIPTOR_MULTIPLE);
		break;

	default:
		WARN_ON(1);
		return -ENODATA;
	}

	return 0;
}

/**
 * i40e_vsi_alloc_arrays - Allocate queue and vector pointer arrays for the vsi
 * @vsi: VSI pointer
 * @alloc_qvectors: a bool to specify if q_vectors need to be allocated.
 *
 * On error: returns error code (negative)
 * On success: returns 0
 **/
static int i40e_vsi_alloc_arrays(struct i40e_vsi *vsi, bool alloc_qvectors)
{
	struct i40e_ring **next_rings;
	int size;
	int ret = 0;

	/* allocate memory for both Tx, XDP Tx and Rx ring pointers */
	size = sizeof(struct i40e_ring *) * vsi->alloc_queue_pairs *
	       (i40e_enabled_xdp_vsi(vsi) ? 3 : 2);
	vsi->tx_rings = kzalloc(size, GFP_KERNEL);
	if (!vsi->tx_rings)
		return -ENOMEM;
	next_rings = vsi->tx_rings + vsi->alloc_queue_pairs;
	if (i40e_enabled_xdp_vsi(vsi)) {
		vsi->xdp_rings = next_rings;
		next_rings += vsi->alloc_queue_pairs;
	}
	vsi->rx_rings = next_rings;

	if (alloc_qvectors) {
		/* allocate memory for q_vector pointers */
		size = sizeof(struct i40e_q_vector *) * vsi->num_q_vectors;
		vsi->q_vectors = kzalloc(size, GFP_KERNEL);
		if (!vsi->q_vectors) {
			ret = -ENOMEM;
			goto err_vectors;
		}
	}
	return ret;

err_vectors:
	kfree(vsi->tx_rings);
	return ret;
}

/**
 * i40e_vsi_mem_alloc - Allocates the next available struct vsi in the PF
 * @pf: board private structure
 * @type: type of VSI
 *
 * On error: returns error code (negative)
 * On success: returns vsi index in PF (positive)
 **/
static int i40e_vsi_mem_alloc(struct i40e_pf *pf, enum i40e_vsi_type type)
{
	int ret = -ENODEV;
	struct i40e_vsi *vsi;
	int vsi_idx;
	int i;

	/* Need to protect the allocation of the VSIs at the PF level */
	mutex_lock(&pf->switch_mutex);

	/* VSI list may be fragmented if VSI creation/destruction has
	 * been happening.  We can afford to do a quick scan to look
	 * for any free VSIs in the list.
	 *
	 * find next empty vsi slot, looping back around if necessary
	 */
	i = pf->next_vsi;
	while (i < pf->num_alloc_vsi && pf->vsi[i])
		i++;
	if (i >= pf->num_alloc_vsi) {
		i = 0;
		while (i < pf->next_vsi && pf->vsi[i])
			i++;
	}

	if (i < pf->num_alloc_vsi && !pf->vsi[i]) {
		vsi_idx = i;             /* Found one! */
	} else {
		ret = -ENODEV;
		goto unlock_pf;  /* out of VSI slots! */
	}
	pf->next_vsi = ++i;

	vsi = kzalloc(sizeof(*vsi), GFP_KERNEL);
	if (!vsi) {
		ret = -ENOMEM;
		goto unlock_pf;
	}
	vsi->type = type;
	vsi->back = pf;
	set_bit(__I40E_VSI_DOWN, vsi->state);
	vsi->flags = 0;
	vsi->idx = vsi_idx;
	vsi->int_rate_limit = 0;
	vsi->rss_table_size = (vsi->type == I40E_VSI_MAIN) ?
				pf->rss_table_size : 64;
	vsi->netdev_registered = false;
	vsi->work_limit = I40E_DEFAULT_IRQ_WORK;
	hash_init(vsi->mac_filter_hash);
	vsi->irqs_ready = false;

	if (type == I40E_VSI_MAIN) {
		vsi->af_xdp_zc_qps = bitmap_zalloc(pf->num_lan_qps, GFP_KERNEL);
		if (!vsi->af_xdp_zc_qps)
			goto err_rings;
	}

	ret = i40e_set_num_rings_in_vsi(vsi);
	if (ret)
		goto err_rings;

	ret = i40e_vsi_alloc_arrays(vsi, true);
	if (ret)
		goto err_rings;

	/* Setup default MSIX irq handler for VSI */
	i40e_vsi_setup_irqhandler(vsi, i40e_msix_clean_rings);

	/* Initialize VSI lock */
	spin_lock_init(&vsi->mac_filter_hash_lock);
	pf->vsi[vsi_idx] = vsi;
	ret = vsi_idx;
	goto unlock_pf;

err_rings:
	bitmap_free(vsi->af_xdp_zc_qps);
	pf->next_vsi = i - 1;
	kfree(vsi);
unlock_pf:
	mutex_unlock(&pf->switch_mutex);
	return ret;
}

/**
 * i40e_vsi_free_arrays - Free queue and vector pointer arrays for the VSI
 * @vsi: VSI pointer
 * @free_qvectors: a bool to specify if q_vectors need to be freed.
 *
 * On error: returns error code (negative)
 * On success: returns 0
 **/
static void i40e_vsi_free_arrays(struct i40e_vsi *vsi, bool free_qvectors)
{
	/* free the ring and vector containers */
	if (free_qvectors) {
		kfree(vsi->q_vectors);
		vsi->q_vectors = NULL;
	}
	kfree(vsi->tx_rings);
	vsi->tx_rings = NULL;
	vsi->rx_rings = NULL;
	vsi->xdp_rings = NULL;
}

/**
 * i40e_clear_rss_config_user - clear the user configured RSS hash keys
 * and lookup table
 * @vsi: Pointer to VSI structure
 */
static void i40e_clear_rss_config_user(struct i40e_vsi *vsi)
{
	if (!vsi)
		return;

	kfree(vsi->rss_hkey_user);
	vsi->rss_hkey_user = NULL;

	kfree(vsi->rss_lut_user);
	vsi->rss_lut_user = NULL;
}

/**
 * i40e_vsi_clear - Deallocate the VSI provided
 * @vsi: the VSI being un-configured
 **/
static int i40e_vsi_clear(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf;

	if (!vsi)
		return 0;

	if (!vsi->back)
		goto free_vsi;
	pf = vsi->back;

	mutex_lock(&pf->switch_mutex);
	if (!pf->vsi[vsi->idx]) {
		dev_err(&pf->pdev->dev, "pf->vsi[%d] is NULL, just free vsi[%d](type %d)\n",
			vsi->idx, vsi->idx, vsi->type);
		goto unlock_vsi;
	}

	if (pf->vsi[vsi->idx] != vsi) {
		dev_err(&pf->pdev->dev,
			"pf->vsi[%d](type %d) != vsi[%d](type %d): no free!\n",
			pf->vsi[vsi->idx]->idx,
			pf->vsi[vsi->idx]->type,
			vsi->idx, vsi->type);
		goto unlock_vsi;
	}

	/* updates the PF for this cleared vsi */
	i40e_put_lump(pf->qp_pile, vsi->base_queue, vsi->idx);
	i40e_put_lump(pf->irq_pile, vsi->base_vector, vsi->idx);

	bitmap_free(vsi->af_xdp_zc_qps);
	i40e_vsi_free_arrays(vsi, true);
	i40e_clear_rss_config_user(vsi);

	pf->vsi[vsi->idx] = NULL;
	if (vsi->idx < pf->next_vsi)
		pf->next_vsi = vsi->idx;

unlock_vsi:
	mutex_unlock(&pf->switch_mutex);
free_vsi:
	kfree(vsi);

	return 0;
}

/**
 * i40e_vsi_clear_rings - Deallocates the Rx and Tx rings for the provided VSI
 * @vsi: the VSI being cleaned
 **/
static void i40e_vsi_clear_rings(struct i40e_vsi *vsi)
{
	int i;

	if (vsi->tx_rings && vsi->tx_rings[0]) {
		for (i = 0; i < vsi->alloc_queue_pairs; i++) {
			kfree_rcu(vsi->tx_rings[i], rcu);
			WRITE_ONCE(vsi->tx_rings[i], NULL);
			WRITE_ONCE(vsi->rx_rings[i], NULL);
			if (vsi->xdp_rings)
				WRITE_ONCE(vsi->xdp_rings[i], NULL);
		}
	}
}

/**
 * i40e_alloc_rings - Allocates the Rx and Tx rings for the provided VSI
 * @vsi: the VSI being configured
 **/
static int i40e_alloc_rings(struct i40e_vsi *vsi)
{
	int i, qpv = i40e_enabled_xdp_vsi(vsi) ? 3 : 2;
	struct i40e_pf *pf = vsi->back;
	struct i40e_ring *ring;

	/* Set basic values in the rings to be used later during open() */
	for (i = 0; i < vsi->alloc_queue_pairs; i++) {
		/* allocate space for both Tx and Rx in one shot */
		ring = kcalloc(qpv, sizeof(struct i40e_ring), GFP_KERNEL);
		if (!ring)
			goto err_out;

		ring->queue_index = i;
		ring->reg_idx = vsi->base_queue + i;
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_tx_desc;
		ring->size = 0;
		ring->dcb_tc = 0;
		if (vsi->back->hw_features & I40E_HW_WB_ON_ITR_CAPABLE)
			ring->flags = I40E_TXR_FLAGS_WB_ON_ITR;
		ring->itr_setting = pf->tx_itr_default;
		WRITE_ONCE(vsi->tx_rings[i], ring++);

		if (!i40e_enabled_xdp_vsi(vsi))
			goto setup_rx;

		ring->queue_index = vsi->alloc_queue_pairs + i;
		ring->reg_idx = vsi->base_queue + ring->queue_index;
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = NULL;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_tx_desc;
		ring->size = 0;
		ring->dcb_tc = 0;
		if (vsi->back->hw_features & I40E_HW_WB_ON_ITR_CAPABLE)
			ring->flags = I40E_TXR_FLAGS_WB_ON_ITR;
		set_ring_xdp(ring);
		ring->itr_setting = pf->tx_itr_default;
		WRITE_ONCE(vsi->xdp_rings[i], ring++);

setup_rx:
		ring->queue_index = i;
		ring->reg_idx = vsi->base_queue + i;
		ring->ring_active = false;
		ring->vsi = vsi;
		ring->netdev = vsi->netdev;
		ring->dev = &pf->pdev->dev;
		ring->count = vsi->num_rx_desc;
		ring->size = 0;
		ring->dcb_tc = 0;
		ring->itr_setting = pf->rx_itr_default;
		WRITE_ONCE(vsi->rx_rings[i], ring);
	}

	return 0;

err_out:
	i40e_vsi_clear_rings(vsi);
	return -ENOMEM;
}

/**
 * i40e_reserve_msix_vectors - Reserve MSI-X vectors in the kernel
 * @pf: board private structure
 * @vectors: the number of MSI-X vectors to request
 *
 * Returns the number of vectors reserved, or error
 **/
static int i40e_reserve_msix_vectors(struct i40e_pf *pf, int vectors)
{
	vectors = pci_enable_msix_range(pf->pdev, pf->msix_entries,
					I40E_MIN_MSIX, vectors);
	if (vectors < 0) {
		dev_info(&pf->pdev->dev,
			 "MSI-X vector reservation failed: %d\n", vectors);
		vectors = 0;
	}

	return vectors;
}

/**
 * i40e_init_msix - Setup the MSIX capability
 * @pf: board private structure
 *
 * Work with the OS to set up the MSIX vectors needed.
 *
 * Returns the number of vectors reserved or negative on failure
 **/
static int i40e_init_msix(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int cpus, extra_vectors;
	int vectors_left;
	int v_budget, i;
	int v_actual;
	int iwarp_requested = 0;

	if (!(pf->flags & I40E_FLAG_MSIX_ENABLED))
		return -ENODEV;

	/* The number of vectors we'll request will be comprised of:
	 *   - Add 1 for "other" cause for Admin Queue events, etc.
	 *   - The number of LAN queue pairs
	 *	- Queues being used for RSS.
	 *		We don't need as many as max_rss_size vectors.
	 *		use rss_size instead in the calculation since that
	 *		is governed by number of cpus in the system.
	 *	- assumes symmetric Tx/Rx pairing
	 *   - The number of VMDq pairs
	 *   - The CPU count within the NUMA node if iWARP is enabled
	 * Once we count this up, try the request.
	 *
	 * If we can't get what we want, we'll simplify to nearly nothing
	 * and try again.  If that still fails, we punt.
	 */
	vectors_left = hw->func_caps.num_msix_vectors;
	v_budget = 0;

	/* reserve one vector for miscellaneous handler */
	if (vectors_left) {
		v_budget++;
		vectors_left--;
	}

	/* reserve some vectors for the main PF traffic queues. Initially we
	 * only reserve at most 50% of the available vectors, in the case that
	 * the number of online CPUs is large. This ensures that we can enable
	 * extra features as well. Once we've enabled the other features, we
	 * will use any remaining vectors to reach as close as we can to the
	 * number of online CPUs.
	 */
	cpus = num_online_cpus();
	pf->num_lan_msix = min_t(int, cpus, vectors_left / 2);
	vectors_left -= pf->num_lan_msix;

	/* reserve one vector for sideband flow director */
	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		if (vectors_left) {
			pf->num_fdsb_msix = 1;
			v_budget++;
			vectors_left--;
		} else {
			pf->num_fdsb_msix = 0;
		}
	}

	/* can we reserve enough for iWARP? */
	if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
		iwarp_requested = pf->num_iwarp_msix;

		if (!vectors_left)
			pf->num_iwarp_msix = 0;
		else if (vectors_left < pf->num_iwarp_msix)
			pf->num_iwarp_msix = 1;
		v_budget += pf->num_iwarp_msix;
		vectors_left -= pf->num_iwarp_msix;
	}

	/* any vectors left over go for VMDq support */
	if (pf->flags & I40E_FLAG_VMDQ_ENABLED) {
		if (!vectors_left) {
			pf->num_vmdq_msix = 0;
			pf->num_vmdq_qps = 0;
		} else {
			int vmdq_vecs_wanted =
				pf->num_vmdq_vsis * pf->num_vmdq_qps;
			int vmdq_vecs =
				min_t(int, vectors_left, vmdq_vecs_wanted);

			/* if we're short on vectors for what's desired, we limit
			 * the queues per vmdq.  If this is still more than are
			 * available, the user will need to change the number of
			 * queues/vectors used by the PF later with the ethtool
			 * channels command
			 */
			if (vectors_left < vmdq_vecs_wanted) {
				pf->num_vmdq_qps = 1;
				vmdq_vecs_wanted = pf->num_vmdq_vsis;
				vmdq_vecs = min_t(int,
						  vectors_left,
						  vmdq_vecs_wanted);
			}
			pf->num_vmdq_msix = pf->num_vmdq_qps;

			v_budget += vmdq_vecs;
			vectors_left -= vmdq_vecs;
		}
	}

	/* On systems with a large number of SMP cores, we previously limited
	 * the number of vectors for num_lan_msix to be at most 50% of the
	 * available vectors, to allow for other features. Now, we add back
	 * the remaining vectors. However, we ensure that the total
	 * num_lan_msix will not exceed num_online_cpus(). To do this, we
	 * calculate the number of vectors we can add without going over the
	 * cap of CPUs. For systems with a small number of CPUs this will be
	 * zero.
	 */
	extra_vectors = min_t(int, cpus - pf->num_lan_msix, vectors_left);
	pf->num_lan_msix += extra_vectors;
	vectors_left -= extra_vectors;

	WARN(vectors_left < 0,
	     "Calculation of remaining vectors underflowed. This is an accounting bug when determining total MSI-X vectors.\n");

	v_budget += pf->num_lan_msix;
	pf->msix_entries = kcalloc(v_budget, sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!pf->msix_entries)
		return -ENOMEM;

	for (i = 0; i < v_budget; i++)
		pf->msix_entries[i].entry = i;
	v_actual = i40e_reserve_msix_vectors(pf, v_budget);

	if (v_actual < I40E_MIN_MSIX) {
		pf->flags &= ~I40E_FLAG_MSIX_ENABLED;
		kfree(pf->msix_entries);
		pf->msix_entries = NULL;
		pci_disable_msix(pf->pdev);
		return -ENODEV;

	} else if (v_actual == I40E_MIN_MSIX) {
		/* Adjust for minimal MSIX use */
		pf->num_vmdq_vsis = 0;
		pf->num_vmdq_qps = 0;
		pf->num_lan_qps = 1;
		pf->num_lan_msix = 1;

	} else if (v_actual != v_budget) {
		/* If we have limited resources, we will start with no vectors
		 * for the special features and then allocate vectors to some
		 * of these features based on the policy and at the end disable
		 * the features that did not get any vectors.
		 */
		int vec;

		dev_info(&pf->pdev->dev,
			 "MSI-X vector limit reached with %d, wanted %d, attempting to redistribute vectors\n",
			 v_actual, v_budget);
		/* reserve the misc vector */
		vec = v_actual - 1;

		/* Scale vector usage down */
		pf->num_vmdq_msix = 1;    /* force VMDqs to only one vector */
		pf->num_vmdq_vsis = 1;
		pf->num_vmdq_qps = 1;

		/* partition out the remaining vectors */
		switch (vec) {
		case 2:
			pf->num_lan_msix = 1;
			break;
		case 3:
			if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
				pf->num_lan_msix = 1;
				pf->num_iwarp_msix = 1;
			} else {
				pf->num_lan_msix = 2;
			}
			break;
		default:
			if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
				pf->num_iwarp_msix = min_t(int, (vec / 3),
						 iwarp_requested);
				pf->num_vmdq_vsis = min_t(int, (vec / 3),
						  I40E_DEFAULT_NUM_VMDQ_VSI);
			} else {
				pf->num_vmdq_vsis = min_t(int, (vec / 2),
						  I40E_DEFAULT_NUM_VMDQ_VSI);
			}
			if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
				pf->num_fdsb_msix = 1;
				vec--;
			}
			pf->num_lan_msix = min_t(int,
			       (vec - (pf->num_iwarp_msix + pf->num_vmdq_vsis)),
							      pf->num_lan_msix);
			pf->num_lan_qps = pf->num_lan_msix;
			break;
		}
	}

	if ((pf->flags & I40E_FLAG_FD_SB_ENABLED) &&
	    (pf->num_fdsb_msix == 0)) {
		dev_info(&pf->pdev->dev, "Sideband Flowdir disabled, not enough MSI-X vectors\n");
		pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
		pf->flags |= I40E_FLAG_FD_SB_INACTIVE;
	}
	if ((pf->flags & I40E_FLAG_VMDQ_ENABLED) &&
	    (pf->num_vmdq_msix == 0)) {
		dev_info(&pf->pdev->dev, "VMDq disabled, not enough MSI-X vectors\n");
		pf->flags &= ~I40E_FLAG_VMDQ_ENABLED;
	}

	if ((pf->flags & I40E_FLAG_IWARP_ENABLED) &&
	    (pf->num_iwarp_msix == 0)) {
		dev_info(&pf->pdev->dev, "IWARP disabled, not enough MSI-X vectors\n");
		pf->flags &= ~I40E_FLAG_IWARP_ENABLED;
	}
	i40e_debug(&pf->hw, I40E_DEBUG_INIT,
		   "MSI-X vector distribution: PF %d, VMDq %d, FDSB %d, iWARP %d\n",
		   pf->num_lan_msix,
		   pf->num_vmdq_msix * pf->num_vmdq_vsis,
		   pf->num_fdsb_msix,
		   pf->num_iwarp_msix);

	return v_actual;
}

/**
 * i40e_vsi_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the vsi struct
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int i40e_vsi_alloc_q_vector(struct i40e_vsi *vsi, int v_idx)
{
	struct i40e_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = kzalloc(sizeof(struct i40e_q_vector), GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
	cpumask_copy(&q_vector->affinity_mask, cpu_possible_mask);

	if (vsi->netdev)
		netif_napi_add(vsi->netdev, &q_vector->napi,
			       i40e_napi_poll, NAPI_POLL_WEIGHT);

	/* tie q_vector and vsi together */
	vsi->q_vectors[v_idx] = q_vector;

	return 0;
}

/**
 * i40e_vsi_alloc_q_vectors - Allocate memory for interrupt vectors
 * @vsi: the VSI being configured
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int i40e_vsi_alloc_q_vectors(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int err, v_idx, num_q_vectors;

	/* if not MSIX, give the one vector only to the LAN VSI */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		num_q_vectors = vsi->num_q_vectors;
	else if (vsi == pf->vsi[pf->lan_vsi])
		num_q_vectors = 1;
	else
		return -EINVAL;

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		err = i40e_vsi_alloc_q_vector(vsi, v_idx);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (v_idx--)
		i40e_free_q_vector(vsi, v_idx);

	return err;
}

/**
 * i40e_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 **/
static int i40e_init_interrupt_scheme(struct i40e_pf *pf)
{
	int vectors = 0;
	ssize_t size;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		vectors = i40e_init_msix(pf);
		if (vectors < 0) {
			pf->flags &= ~(I40E_FLAG_MSIX_ENABLED	|
				       I40E_FLAG_IWARP_ENABLED	|
				       I40E_FLAG_RSS_ENABLED	|
				       I40E_FLAG_DCB_CAPABLE	|
				       I40E_FLAG_DCB_ENABLED	|
				       I40E_FLAG_SRIOV_ENABLED	|
				       I40E_FLAG_FD_SB_ENABLED	|
				       I40E_FLAG_FD_ATR_ENABLED	|
				       I40E_FLAG_VMDQ_ENABLED);
			pf->flags |= I40E_FLAG_FD_SB_INACTIVE;

			/* rework the queue expectations without MSIX */
			i40e_determine_queue_usage(pf);
		}
	}

	if (!(pf->flags & I40E_FLAG_MSIX_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSI_ENABLED)) {
		dev_info(&pf->pdev->dev, "MSI-X not available, trying MSI\n");
		vectors = pci_enable_msi(pf->pdev);
		if (vectors < 0) {
			dev_info(&pf->pdev->dev, "MSI init failed - %d\n",
				 vectors);
			pf->flags &= ~I40E_FLAG_MSI_ENABLED;
		}
		vectors = 1;  /* one MSI or Legacy vector */
	}

	if (!(pf->flags & (I40E_FLAG_MSIX_ENABLED | I40E_FLAG_MSI_ENABLED)))
		dev_info(&pf->pdev->dev, "MSI-X and MSI not available, falling back to Legacy IRQ\n");

	/* set up vector assignment tracking */
	size = sizeof(struct i40e_lump_tracking) + (sizeof(u16) * vectors);
	pf->irq_pile = kzalloc(size, GFP_KERNEL);
	if (!pf->irq_pile)
		return -ENOMEM;

	pf->irq_pile->num_entries = vectors;

	/* track first vector for misc interrupts, ignore return */
	(void)i40e_get_lump(pf, pf->irq_pile, 1, I40E_PILE_VALID_BIT - 1);

	return 0;
}

/**
 * i40e_restore_interrupt_scheme - Restore the interrupt scheme
 * @pf: private board data structure
 *
 * Restore the interrupt scheme that was cleared when we suspended the
 * device. This should be called during resume to re-allocate the q_vectors
 * and reacquire IRQs.
 */
static int i40e_restore_interrupt_scheme(struct i40e_pf *pf)
{
	int err, i;

	/* We cleared the MSI and MSI-X flags when disabling the old interrupt
	 * scheme. We need to re-enabled them here in order to attempt to
	 * re-acquire the MSI or MSI-X vectors
	 */
	pf->flags |= (I40E_FLAG_MSIX_ENABLED | I40E_FLAG_MSI_ENABLED);

	err = i40e_init_interrupt_scheme(pf);
	if (err)
		return err;

	/* Now that we've re-acquired IRQs, we need to remap the vectors and
	 * rings together again.
	 */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i]) {
			err = i40e_vsi_alloc_q_vectors(pf->vsi[i]);
			if (err)
				goto err_unwind;
			i40e_vsi_map_rings_to_vectors(pf->vsi[i]);
		}
	}

	err = i40e_setup_misc_vector(pf);
	if (err)
		goto err_unwind;

	if (pf->flags & I40E_FLAG_IWARP_ENABLED)
		i40e_client_update_msix_info(pf);

	return 0;

err_unwind:
	while (i--) {
		if (pf->vsi[i])
			i40e_vsi_free_q_vectors(pf->vsi[i]);
	}

	return err;
}

/**
 * i40e_setup_misc_vector_for_recovery_mode - Setup the misc vector to handle
 * non queue events in recovery mode
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0 or MSI/legacy, which is used to manage
 * the non-queue interrupts, e.g. AdminQ and errors in recovery mode.
 * This is handled differently than in recovery mode since no Tx/Rx resources
 * are being allocated.
 **/
static int i40e_setup_misc_vector_for_recovery_mode(struct i40e_pf *pf)
{
	int err;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		err = i40e_setup_misc_vector(pf);

		if (err) {
			dev_info(&pf->pdev->dev,
				 "MSI-X misc vector request failed, error %d\n",
				 err);
			return err;
		}
	} else {
		u32 flags = pf->flags & I40E_FLAG_MSI_ENABLED ? 0 : IRQF_SHARED;

		err = request_irq(pf->pdev->irq, i40e_intr, flags,
				  pf->int_name, pf);

		if (err) {
			dev_info(&pf->pdev->dev,
				 "MSI/legacy misc vector request failed, error %d\n",
				 err);
			return err;
		}
		i40e_enable_misc_int_causes(pf);
		i40e_irq_dynamic_enable_icr0(pf);
	}

	return 0;
}

/**
 * i40e_setup_misc_vector - Setup the misc vector to handle non queue events
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0, which is used to manage the
 * non-queue interrupts, e.g. AdminQ and errors.  This is not used
 * when in MSI or Legacy interrupt mode.
 **/
static int i40e_setup_misc_vector(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int err = 0;

	/* Only request the IRQ once, the first time through. */
	if (!test_and_set_bit(__I40E_MISC_IRQ_REQUESTED, pf->state)) {
		err = request_irq(pf->msix_entries[0].vector,
				  i40e_intr, 0, pf->int_name, pf);
		if (err) {
			clear_bit(__I40E_MISC_IRQ_REQUESTED, pf->state);
			dev_info(&pf->pdev->dev,
				 "request_irq for %s failed: %d\n",
				 pf->int_name, err);
			return -EFAULT;
		}
	}

	i40e_enable_misc_int_causes(pf);

	/* associate no queues to the misc vector */
	wr32(hw, I40E_PFINT_LNKLST0, I40E_QUEUE_END_OF_LIST);
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), I40E_ITR_8K >> 1);

	i40e_flush(hw);

	i40e_irq_dynamic_enable_icr0(pf);

	return err;
}

/**
 * i40e_get_rss_aq - Get RSS keys and lut by using AQ commands
 * @vsi: Pointer to vsi structure
 * @seed: Buffter to store the hash keys
 * @lut: Buffer to store the lookup table entries
 * @lut_size: Size of buffer to store the lookup table entries
 *
 * Return 0 on success, negative on failure
 */
static int i40e_get_rss_aq(struct i40e_vsi *vsi, const u8 *seed,
			   u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int ret = 0;

	if (seed) {
		ret = i40e_aq_get_rss_key(hw, vsi->id,
			(struct i40e_aqc_get_set_rss_key_data *)seed);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Cannot get RSS key, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			return ret;
		}
	}

	if (lut) {
		bool pf_lut = vsi->type == I40E_VSI_MAIN;

		ret = i40e_aq_get_rss_lut(hw, vsi->id, pf_lut, lut, lut_size);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Cannot get RSS lut, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			return ret;
		}
	}

	return ret;
}

/**
 * i40e_config_rss_reg - Configure RSS keys and lut by writing registers
 * @vsi: Pointer to vsi structure
 * @seed: RSS hash seed
 * @lut: Lookup table
 * @lut_size: Lookup table size
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_config_rss_reg(struct i40e_vsi *vsi, const u8 *seed,
			       const u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 vf_id = vsi->vf_id;
	u8 i;

	/* Fill out hash function seed */
	if (seed) {
		u32 *seed_dw = (u32 *)seed;

		if (vsi->type == I40E_VSI_MAIN) {
			for (i = 0; i <= I40E_PFQF_HKEY_MAX_INDEX; i++)
				wr32(hw, I40E_PFQF_HKEY(i), seed_dw[i]);
		} else if (vsi->type == I40E_VSI_SRIOV) {
			for (i = 0; i <= I40E_VFQF_HKEY1_MAX_INDEX; i++)
				wr32(hw, I40E_VFQF_HKEY1(i, vf_id), seed_dw[i]);
		} else {
			dev_err(&pf->pdev->dev, "Cannot set RSS seed - invalid VSI type\n");
		}
	}

	if (lut) {
		u32 *lut_dw = (u32 *)lut;

		if (vsi->type == I40E_VSI_MAIN) {
			if (lut_size != I40E_HLUT_ARRAY_SIZE)
				return -EINVAL;
			for (i = 0; i <= I40E_PFQF_HLUT_MAX_INDEX; i++)
				wr32(hw, I40E_PFQF_HLUT(i), lut_dw[i]);
		} else if (vsi->type == I40E_VSI_SRIOV) {
			if (lut_size != I40E_VF_HLUT_ARRAY_SIZE)
				return -EINVAL;
			for (i = 0; i <= I40E_VFQF_HLUT_MAX_INDEX; i++)
				wr32(hw, I40E_VFQF_HLUT1(i, vf_id), lut_dw[i]);
		} else {
			dev_err(&pf->pdev->dev, "Cannot set RSS LUT - invalid VSI type\n");
		}
	}
	i40e_flush(hw);

	return 0;
}

/**
 * i40e_get_rss_reg - Get the RSS keys and lut by reading registers
 * @vsi: Pointer to VSI structure
 * @seed: Buffer to store the keys
 * @lut: Buffer to store the lookup table entries
 * @lut_size: Size of buffer to store the lookup table entries
 *
 * Returns 0 on success, negative on failure
 */
static int i40e_get_rss_reg(struct i40e_vsi *vsi, u8 *seed,
			    u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 i;

	if (seed) {
		u32 *seed_dw = (u32 *)seed;

		for (i = 0; i <= I40E_PFQF_HKEY_MAX_INDEX; i++)
			seed_dw[i] = i40e_read_rx_ctl(hw, I40E_PFQF_HKEY(i));
	}
	if (lut) {
		u32 *lut_dw = (u32 *)lut;

		if (lut_size != I40E_HLUT_ARRAY_SIZE)
			return -EINVAL;
		for (i = 0; i <= I40E_PFQF_HLUT_MAX_INDEX; i++)
			lut_dw[i] = rd32(hw, I40E_PFQF_HLUT(i));
	}

	return 0;
}

/**
 * i40e_config_rss - Configure RSS keys and lut
 * @vsi: Pointer to VSI structure
 * @seed: RSS hash seed
 * @lut: Lookup table
 * @lut_size: Lookup table size
 *
 * Returns 0 on success, negative on failure
 */
int i40e_config_rss(struct i40e_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;

	if (pf->hw_features & I40E_HW_RSS_AQ_CAPABLE)
		return i40e_config_rss_aq(vsi, seed, lut, lut_size);
	else
		return i40e_config_rss_reg(vsi, seed, lut, lut_size);
}

/**
 * i40e_get_rss - Get RSS keys and lut
 * @vsi: Pointer to VSI structure
 * @seed: Buffer to store the keys
 * @lut: Buffer to store the lookup table entries
 * @lut_size: Size of buffer to store the lookup table entries
 *
 * Returns 0 on success, negative on failure
 */
int i40e_get_rss(struct i40e_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;

	if (pf->hw_features & I40E_HW_RSS_AQ_CAPABLE)
		return i40e_get_rss_aq(vsi, seed, lut, lut_size);
	else
		return i40e_get_rss_reg(vsi, seed, lut, lut_size);
}

/**
 * i40e_fill_rss_lut - Fill the RSS lookup table with default values
 * @pf: Pointer to board private structure
 * @lut: Lookup table
 * @rss_table_size: Lookup table size
 * @rss_size: Range of queue number for hashing
 */
void i40e_fill_rss_lut(struct i40e_pf *pf, u8 *lut,
		       u16 rss_table_size, u16 rss_size)
{
	u16 i;

	for (i = 0; i < rss_table_size; i++)
		lut[i] = i % rss_size;
}

/**
 * i40e_pf_config_rss - Prepare for RSS if used
 * @pf: board private structure
 **/
static int i40e_pf_config_rss(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	u8 seed[I40E_HKEY_ARRAY_SIZE];
	u8 *lut;
	struct i40e_hw *hw = &pf->hw;
	u32 reg_val;
	u64 hena;
	int ret;

	/* By default we enable TCP/UDP with IPv4/IPv6 ptypes */
	hena = (u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(0)) |
		((u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(1)) << 32);
	hena |= i40e_pf_get_default_rss_hena(pf);

	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(0), (u32)hena);
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(1), (u32)(hena >> 32));

	/* Determine the RSS table size based on the hardware capabilities */
	reg_val = i40e_read_rx_ctl(hw, I40E_PFQF_CTL_0);
	reg_val = (pf->rss_table_size == 512) ?
			(reg_val | I40E_PFQF_CTL_0_HASHLUTSIZE_512) :
			(reg_val & ~I40E_PFQF_CTL_0_HASHLUTSIZE_512);
	i40e_write_rx_ctl(hw, I40E_PFQF_CTL_0, reg_val);

	/* Determine the RSS size of the VSI */
	if (!vsi->rss_size) {
		u16 qcount;
		/* If the firmware does something weird during VSI init, we
		 * could end up with zero TCs. Check for that to avoid
		 * divide-by-zero. It probably won't pass traffic, but it also
		 * won't panic.
		 */
		qcount = vsi->num_queue_pairs /
			 (vsi->tc_config.numtc ? vsi->tc_config.numtc : 1);
		vsi->rss_size = min_t(int, pf->alloc_rss_size, qcount);
	}
	if (!vsi->rss_size)
		return -EINVAL;

	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	/* Use user configured lut if there is one, otherwise use default */
	if (vsi->rss_lut_user)
		memcpy(lut, vsi->rss_lut_user, vsi->rss_table_size);
	else
		i40e_fill_rss_lut(pf, lut, vsi->rss_table_size, vsi->rss_size);

	/* Use user configured hash key if there is one, otherwise
	 * use default.
	 */
	if (vsi->rss_hkey_user)
		memcpy(seed, vsi->rss_hkey_user, I40E_HKEY_ARRAY_SIZE);
	else
		netdev_rss_key_fill((void *)seed, I40E_HKEY_ARRAY_SIZE);
	ret = i40e_config_rss(vsi, seed, lut, vsi->rss_table_size);
	kfree(lut);

	return ret;
}

/**
 * i40e_reconfig_rss_queues - change number of queues for rss and rebuild
 * @pf: board private structure
 * @queue_count: the requested queue count for rss.
 *
 * returns 0 if rss is not enabled, if enabled returns the final rss queue
 * count which may be different from the requested queue count.
 * Note: expects to be called while under rtnl_lock()
 **/
int i40e_reconfig_rss_queues(struct i40e_pf *pf, int queue_count)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	int new_rss_size;

	if (!(pf->flags & I40E_FLAG_RSS_ENABLED))
		return 0;

	queue_count = min_t(int, queue_count, num_online_cpus());
	new_rss_size = min_t(int, queue_count, pf->rss_size_max);

	if (queue_count != vsi->num_queue_pairs) {
		u16 qcount;

		vsi->req_queue_pairs = queue_count;
		i40e_prep_for_reset(pf, true);

		pf->alloc_rss_size = new_rss_size;

		i40e_reset_and_rebuild(pf, true, true);

		/* Discard the user configured hash keys and lut, if less
		 * queues are enabled.
		 */
		if (queue_count < vsi->rss_size) {
			i40e_clear_rss_config_user(vsi);
			dev_dbg(&pf->pdev->dev,
				"discard user configured hash keys and lut\n");
		}

		/* Reset vsi->rss_size, as number of enabled queues changed */
		qcount = vsi->num_queue_pairs / vsi->tc_config.numtc;
		vsi->rss_size = min_t(int, pf->alloc_rss_size, qcount);

		i40e_pf_config_rss(pf);
	}
	dev_info(&pf->pdev->dev, "User requested queue count/HW max RSS count:  %d/%d\n",
		 vsi->req_queue_pairs, pf->rss_size_max);
	return pf->alloc_rss_size;
}

/**
 * i40e_get_partition_bw_setting - Retrieve BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_get_partition_bw_setting(struct i40e_pf *pf)
{
	i40e_status status;
	bool min_valid, max_valid;
	u32 max_bw, min_bw;

	status = i40e_read_bw_from_alt_ram(&pf->hw, &max_bw, &min_bw,
					   &min_valid, &max_valid);

	if (!status) {
		if (min_valid)
			pf->min_bw = min_bw;
		if (max_valid)
			pf->max_bw = max_bw;
	}

	return status;
}

/**
 * i40e_set_partition_bw_setting - Set BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_set_partition_bw_setting(struct i40e_pf *pf)
{
	struct i40e_aqc_configure_partition_bw_data bw_data;
	i40e_status status;

	memset(&bw_data, 0, sizeof(bw_data));

	/* Set the valid bit for this PF */
	bw_data.pf_valid_bits = cpu_to_le16(BIT(pf->hw.pf_id));
	bw_data.max_bw[pf->hw.pf_id] = pf->max_bw & I40E_ALT_BW_VALUE_MASK;
	bw_data.min_bw[pf->hw.pf_id] = pf->min_bw & I40E_ALT_BW_VALUE_MASK;

	/* Set the new bandwidths */
	status = i40e_aq_configure_partition_bw(&pf->hw, &bw_data, NULL);

	return status;
}

/**
 * i40e_commit_partition_bw_setting - Commit BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_commit_partition_bw_setting(struct i40e_pf *pf)
{
	/* Commit temporary BW setting to permanent NVM image */
	enum i40e_admin_queue_err last_aq_status;
	i40e_status ret;
	u16 nvm_word;

	if (pf->hw.partition_id != 1) {
		dev_info(&pf->pdev->dev,
			 "Commit BW only works on partition 1! This is partition %d",
			 pf->hw.partition_id);
		ret = I40E_NOT_SUPPORTED;
		goto bw_commit_out;
	}

	/* Acquire NVM for read access */
	ret = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_READ);
	last_aq_status = pf->hw.aq.asq_last_status;
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Cannot acquire NVM for read access, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, last_aq_status));
		goto bw_commit_out;
	}

	/* Read word 0x10 of NVM - SW compatibility word 1 */
	ret = i40e_aq_read_nvm(&pf->hw,
			       I40E_SR_NVM_CONTROL_WORD,
			       0x10, sizeof(nvm_word), &nvm_word,
			       false, NULL);
	/* Save off last admin queue command status before releasing
	 * the NVM
	 */
	last_aq_status = pf->hw.aq.asq_last_status;
	i40e_release_nvm(&pf->hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "NVM read error, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, last_aq_status));
		goto bw_commit_out;
	}

	/* Wait a bit for NVM release to complete */
	msleep(50);

	/* Acquire NVM for write access */
	ret = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_WRITE);
	last_aq_status = pf->hw.aq.asq_last_status;
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Cannot acquire NVM for write access, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, last_aq_status));
		goto bw_commit_out;
	}
	/* Write it back out unchanged to initiate update NVM,
	 * which will force a write of the shadow (alt) RAM to
	 * the NVM - thus storing the bandwidth values permanently.
	 */
	ret = i40e_aq_update_nvm(&pf->hw,
				 I40E_SR_NVM_CONTROL_WORD,
				 0x10, sizeof(nvm_word),
				 &nvm_word, true, 0, NULL);
	/* Save off last admin queue command status before releasing
	 * the NVM
	 */
	last_aq_status = pf->hw.aq.asq_last_status;
	i40e_release_nvm(&pf->hw);
	if (ret)
		dev_info(&pf->pdev->dev,
			 "BW settings NOT SAVED, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, last_aq_status));
bw_commit_out:

	return ret;
}

/**
 * i40e_is_total_port_shutdown_enabled - read NVM and return value
 * if total port shutdown feature is enabled for this PF
 * @pf: board private structure
 **/
static bool i40e_is_total_port_shutdown_enabled(struct i40e_pf *pf)
{
#define I40E_TOTAL_PORT_SHUTDOWN_ENABLED	BIT(4)
#define I40E_FEATURES_ENABLE_PTR		0x2A
#define I40E_CURRENT_SETTING_PTR		0x2B
#define I40E_LINK_BEHAVIOR_WORD_OFFSET		0x2D
#define I40E_LINK_BEHAVIOR_WORD_LENGTH		0x1
#define I40E_LINK_BEHAVIOR_OS_FORCED_ENABLED	BIT(0)
#define I40E_LINK_BEHAVIOR_PORT_BIT_LENGTH	4
	i40e_status read_status = I40E_SUCCESS;
	u16 sr_emp_sr_settings_ptr = 0;
	u16 features_enable = 0;
	u16 link_behavior = 0;
	bool ret = false;

	read_status = i40e_read_nvm_word(&pf->hw,
					 I40E_SR_EMP_SR_SETTINGS_PTR,
					 &sr_emp_sr_settings_ptr);
	if (read_status)
		goto err_nvm;
	read_status = i40e_read_nvm_word(&pf->hw,
					 sr_emp_sr_settings_ptr +
					 I40E_FEATURES_ENABLE_PTR,
					 &features_enable);
	if (read_status)
		goto err_nvm;
	if (I40E_TOTAL_PORT_SHUTDOWN_ENABLED & features_enable) {
		read_status = i40e_read_nvm_module_data(&pf->hw,
							I40E_SR_EMP_SR_SETTINGS_PTR,
							I40E_CURRENT_SETTING_PTR,
							I40E_LINK_BEHAVIOR_WORD_OFFSET,
							I40E_LINK_BEHAVIOR_WORD_LENGTH,
							&link_behavior);
		if (read_status)
			goto err_nvm;
		link_behavior >>= (pf->hw.port * I40E_LINK_BEHAVIOR_PORT_BIT_LENGTH);
		ret = I40E_LINK_BEHAVIOR_OS_FORCED_ENABLED & link_behavior;
	}
	return ret;

err_nvm:
	dev_warn(&pf->pdev->dev,
		 "total-port-shutdown feature is off due to read nvm error: %s\n",
		 i40e_stat_str(&pf->hw, read_status));
	return ret;
}

/**
 * i40e_sw_init - Initialize general software structures (struct i40e_pf)
 * @pf: board private structure to initialize
 *
 * i40e_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int i40e_sw_init(struct i40e_pf *pf)
{
	int err = 0;
	int size;
	u16 pow;

	/* Set default capability flags */
	pf->flags = I40E_FLAG_RX_CSUM_ENABLED |
		    I40E_FLAG_MSI_ENABLED     |
		    I40E_FLAG_MSIX_ENABLED;

	/* Set default ITR */
	pf->rx_itr_default = I40E_ITR_RX_DEF;
	pf->tx_itr_default = I40E_ITR_TX_DEF;

	/* Depending on PF configurations, it is possible that the RSS
	 * maximum might end up larger than the available queues
	 */
	pf->rss_size_max = BIT(pf->hw.func_caps.rss_table_entry_width);
	pf->alloc_rss_size = 1;
	pf->rss_table_size = pf->hw.func_caps.rss_table_size;
	pf->rss_size_max = min_t(int, pf->rss_size_max,
				 pf->hw.func_caps.num_tx_qp);

	/* find the next higher power-of-2 of num cpus */
	pow = roundup_pow_of_two(num_online_cpus());
	pf->rss_size_max = min_t(int, pf->rss_size_max, pow);

	if (pf->hw.func_caps.rss) {
		pf->flags |= I40E_FLAG_RSS_ENABLED;
		pf->alloc_rss_size = min_t(int, pf->rss_size_max,
					   num_online_cpus());
	}

	/* MFP mode enabled */
	if (pf->hw.func_caps.npar_enable || pf->hw.func_caps.flex10_enable) {
		pf->flags |= I40E_FLAG_MFP_ENABLED;
		dev_info(&pf->pdev->dev, "MFP mode Enabled\n");
		if (i40e_get_partition_bw_setting(pf)) {
			dev_warn(&pf->pdev->dev,
				 "Could not get partition bw settings\n");
		} else {
			dev_info(&pf->pdev->dev,
				 "Partition BW Min = %8.8x, Max = %8.8x\n",
				 pf->min_bw, pf->max_bw);

			/* nudge the Tx scheduler */
			i40e_set_partition_bw_setting(pf);
		}
	}

	if ((pf->hw.func_caps.fd_filters_guaranteed > 0) ||
	    (pf->hw.func_caps.fd_filters_best_effort > 0)) {
		pf->flags |= I40E_FLAG_FD_ATR_ENABLED;
		pf->atr_sample_rate = I40E_DEFAULT_ATR_SAMPLE_RATE;
		if (pf->flags & I40E_FLAG_MFP_ENABLED &&
		    pf->hw.num_partitions > 1)
			dev_info(&pf->pdev->dev,
				 "Flow Director Sideband mode Disabled in MFP mode\n");
		else
			pf->flags |= I40E_FLAG_FD_SB_ENABLED;
		pf->fdir_pf_filter_count =
				 pf->hw.func_caps.fd_filters_guaranteed;
		pf->hw.fdir_shared_filter_count =
				 pf->hw.func_caps.fd_filters_best_effort;
	}

	if (pf->hw.mac.type == I40E_MAC_X722) {
		pf->hw_features |= (I40E_HW_RSS_AQ_CAPABLE |
				    I40E_HW_128_QP_RSS_CAPABLE |
				    I40E_HW_ATR_EVICT_CAPABLE |
				    I40E_HW_WB_ON_ITR_CAPABLE |
				    I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE |
				    I40E_HW_NO_PCI_LINK_CHECK |
				    I40E_HW_USE_SET_LLDP_MIB |
				    I40E_HW_GENEVE_OFFLOAD_CAPABLE |
				    I40E_HW_PTP_L4_CAPABLE |
				    I40E_HW_WOL_MC_MAGIC_PKT_WAKE |
				    I40E_HW_OUTER_UDP_CSUM_CAPABLE);

#define I40E_FDEVICT_PCTYPE_DEFAULT 0xc03
		if (rd32(&pf->hw, I40E_GLQF_FDEVICTENA(1)) !=
		    I40E_FDEVICT_PCTYPE_DEFAULT) {
			dev_warn(&pf->pdev->dev,
				 "FD EVICT PCTYPES are not right, disable FD HW EVICT\n");
			pf->hw_features &= ~I40E_HW_ATR_EVICT_CAPABLE;
		}
	} else if ((pf->hw.aq.api_maj_ver > 1) ||
		   ((pf->hw.aq.api_maj_ver == 1) &&
		    (pf->hw.aq.api_min_ver > 4))) {
		/* Supported in FW API version higher than 1.4 */
		pf->hw_features |= I40E_HW_GENEVE_OFFLOAD_CAPABLE;
	}

	/* Enable HW ATR eviction if possible */
	if (pf->hw_features & I40E_HW_ATR_EVICT_CAPABLE)
		pf->flags |= I40E_FLAG_HW_ATR_EVICT_ENABLED;

	if ((pf->hw.mac.type == I40E_MAC_XL710) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver < 33)) ||
	    (pf->hw.aq.fw_maj_ver < 4))) {
		pf->hw_features |= I40E_HW_RESTART_AUTONEG;
		/* No DCB support  for FW < v4.33 */
		pf->hw_features |= I40E_HW_NO_DCB_SUPPORT;
	}

	/* Disable FW LLDP if FW < v4.3 */
	if ((pf->hw.mac.type == I40E_MAC_XL710) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver < 3)) ||
	    (pf->hw.aq.fw_maj_ver < 4)))
		pf->hw_features |= I40E_HW_STOP_FW_LLDP;

	/* Use the FW Set LLDP MIB API if FW > v4.40 */
	if ((pf->hw.mac.type == I40E_MAC_XL710) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver >= 40)) ||
	    (pf->hw.aq.fw_maj_ver >= 5)))
		pf->hw_features |= I40E_HW_USE_SET_LLDP_MIB;

	/* Enable PTP L4 if FW > v6.0 */
	if (pf->hw.mac.type == I40E_MAC_XL710 &&
	    pf->hw.aq.fw_maj_ver >= 6)
		pf->hw_features |= I40E_HW_PTP_L4_CAPABLE;

	if (pf->hw.func_caps.vmdq && num_online_cpus() != 1) {
		pf->num_vmdq_vsis = I40E_DEFAULT_NUM_VMDQ_VSI;
		pf->flags |= I40E_FLAG_VMDQ_ENABLED;
		pf->num_vmdq_qps = i40e_default_queues_per_vmdq(pf);
	}

	if (pf->hw.func_caps.iwarp && num_online_cpus() != 1) {
		pf->flags |= I40E_FLAG_IWARP_ENABLED;
		/* IWARP needs one extra vector for CQP just like MISC.*/
		pf->num_iwarp_msix = (int)num_online_cpus() + 1;
	}
	/* Stopping FW LLDP engine is supported on XL710 and X722
	 * starting from FW versions determined in i40e_init_adminq.
	 * Stopping the FW LLDP engine is not supported on XL710
	 * if NPAR is functioning so unset this hw flag in this case.
	 */
	if (pf->hw.mac.type == I40E_MAC_XL710 &&
	    pf->hw.func_caps.npar_enable &&
	    (pf->hw.flags & I40E_HW_FLAG_FW_LLDP_STOPPABLE))
		pf->hw.flags &= ~I40E_HW_FLAG_FW_LLDP_STOPPABLE;

#ifdef CONFIG_PCI_IOV
	if (pf->hw.func_caps.num_vfs && pf->hw.partition_id == 1) {
		pf->num_vf_qps = I40E_DEFAULT_QUEUES_PER_VF;
		pf->flags |= I40E_FLAG_SRIOV_ENABLED;
		pf->num_req_vfs = min_t(int,
					pf->hw.func_caps.num_vfs,
					I40E_MAX_VF_COUNT);
	}
#endif /* CONFIG_PCI_IOV */
	pf->eeprom_version = 0xDEAD;
	pf->lan_veb = I40E_NO_VEB;
	pf->lan_vsi = I40E_NO_VSI;

	/* By default FW has this off for performance reasons */
	pf->flags &= ~I40E_FLAG_VEB_STATS_ENABLED;

	/* set up queue assignment tracking */
	size = sizeof(struct i40e_lump_tracking)
		+ (sizeof(u16) * pf->hw.func_caps.num_tx_qp);
	pf->qp_pile = kzalloc(size, GFP_KERNEL);
	if (!pf->qp_pile) {
		err = -ENOMEM;
		goto sw_init_done;
	}
	pf->qp_pile->num_entries = pf->hw.func_caps.num_tx_qp;

	pf->tx_timeout_recovery_level = 1;

	if (pf->hw.mac.type != I40E_MAC_X722 &&
	    i40e_is_total_port_shutdown_enabled(pf)) {
		/* Link down on close must be on when total port shutdown
		 * is enabled for a given port
		 */
		pf->flags |= (I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED |
			      I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED);
		dev_info(&pf->pdev->dev,
			 "total-port-shutdown was enabled, link-down-on-close is forced on\n");
	}
	mutex_init(&pf->switch_mutex);

sw_init_done:
	return err;
}

/**
 * i40e_set_ntuple - set the ntuple feature flag and take action
 * @pf: board private structure to initialize
 * @features: the feature set that the stack is suggesting
 *
 * returns a bool to indicate if reset needs to happen
 **/
bool i40e_set_ntuple(struct i40e_pf *pf, netdev_features_t features)
{
	bool need_reset = false;

	/* Check if Flow Director n-tuple support was enabled or disabled.  If
	 * the state changed, we need to reset.
	 */
	if (features & NETIF_F_NTUPLE) {
		/* Enable filters and mark for reset */
		if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
			need_reset = true;
		/* enable FD_SB only if there is MSI-X vector and no cloud
		 * filters exist
		 */
		if (pf->num_fdsb_msix > 0 && !pf->num_cloud_filters) {
			pf->flags |= I40E_FLAG_FD_SB_ENABLED;
			pf->flags &= ~I40E_FLAG_FD_SB_INACTIVE;
		}
	} else {
		/* turn off filters, mark for reset and clear SW filter list */
		if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
			need_reset = true;
			i40e_fdir_filter_exit(pf);
		}
		pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
		clear_bit(__I40E_FD_SB_AUTO_DISABLED, pf->state);
		pf->flags |= I40E_FLAG_FD_SB_INACTIVE;

		/* reset fd counters */
		pf->fd_add_err = 0;
		pf->fd_atr_cnt = 0;
		/* if ATR was auto disabled it can be re-enabled. */
		if (test_and_clear_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state))
			if ((pf->flags & I40E_FLAG_FD_ATR_ENABLED) &&
			    (I40E_DEBUG_FD & pf->hw.debug_mask))
				dev_info(&pf->pdev->dev, "ATR re-enabled.\n");
	}
	return need_reset;
}

/**
 * i40e_clear_rss_lut - clear the rx hash lookup table
 * @vsi: the VSI being configured
 **/
static void i40e_clear_rss_lut(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 vf_id = vsi->vf_id;
	u8 i;

	if (vsi->type == I40E_VSI_MAIN) {
		for (i = 0; i <= I40E_PFQF_HLUT_MAX_INDEX; i++)
			wr32(hw, I40E_PFQF_HLUT(i), 0);
	} else if (vsi->type == I40E_VSI_SRIOV) {
		for (i = 0; i <= I40E_VFQF_HLUT_MAX_INDEX; i++)
			i40e_write_rx_ctl(hw, I40E_VFQF_HLUT1(i, vf_id), 0);
	} else {
		dev_err(&pf->pdev->dev, "Cannot set RSS LUT - invalid VSI type\n");
	}
}

/**
 * i40e_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 * Note: expects to be called while under rtnl_lock()
 **/
static int i40e_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	bool need_reset;

	if (features & NETIF_F_RXHASH && !(netdev->features & NETIF_F_RXHASH))
		i40e_pf_config_rss(pf);
	else if (!(features & NETIF_F_RXHASH) &&
		 netdev->features & NETIF_F_RXHASH)
		i40e_clear_rss_lut(vsi);

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);

	if (!(features & NETIF_F_HW_TC) && pf->num_cloud_filters) {
		dev_err(&pf->pdev->dev,
			"Offloaded tc filters active, can't turn hw_tc_offload off");
		return -EINVAL;
	}

	if (!(features & NETIF_F_HW_L2FW_DOFFLOAD) && vsi->macvlan_cnt)
		i40e_del_all_macvlans(vsi);

	need_reset = i40e_set_ntuple(pf, features);

	if (need_reset)
		i40e_do_reset(pf, I40E_PF_RESET_FLAG, true);

	return 0;
}

static int i40e_udp_tunnel_set_port(struct net_device *netdev,
				    unsigned int table, unsigned int idx,
				    struct udp_tunnel_info *ti)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	u8 type, filter_index;
	i40e_status ret;

	type = ti->type == UDP_TUNNEL_TYPE_VXLAN ? I40E_AQC_TUNNEL_TYPE_VXLAN :
						   I40E_AQC_TUNNEL_TYPE_NGE;

	ret = i40e_aq_add_udp_tunnel(hw, ntohs(ti->port), type, &filter_index,
				     NULL);
	if (ret) {
		netdev_info(netdev, "add UDP port failed, err %s aq_err %s\n",
			    i40e_stat_str(hw, ret),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		return -EIO;
	}

	udp_tunnel_nic_set_port_priv(netdev, table, idx, filter_index);
	return 0;
}

static int i40e_udp_tunnel_unset_port(struct net_device *netdev,
				      unsigned int table, unsigned int idx,
				      struct udp_tunnel_info *ti)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	i40e_status ret;

	ret = i40e_aq_del_udp_tunnel(hw, ti->hw_priv, NULL);
	if (ret) {
		netdev_info(netdev, "delete UDP port failed, err %s aq_err %s\n",
			    i40e_stat_str(hw, ret),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		return -EIO;
	}

	return 0;
}

static int i40e_get_phys_port_id(struct net_device *netdev,
				 struct netdev_phys_item_id *ppid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;

	if (!(pf->hw_features & I40E_HW_PORT_ID_VALID))
		return -EOPNOTSUPP;

	ppid->id_len = min_t(int, sizeof(hw->mac.port_addr), sizeof(ppid->id));
	memcpy(ppid->id, hw->mac.port_addr, ppid->id_len);

	return 0;
}

/**
 * i40e_ndo_fdb_add - add an entry to the hardware database
 * @ndm: the input from the stack
 * @tb: pointer to array of nladdr (unused)
 * @dev: the net device pointer
 * @addr: the MAC address entry being added
 * @vid: VLAN ID
 * @flags: instructions from stack about fdb operation
 * @extack: netlink extended ack, unused currently
 */
static int i40e_ndo_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			    struct net_device *dev,
			    const unsigned char *addr, u16 vid,
			    u16 flags,
			    struct netlink_ext_ack *extack)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_pf *pf = np->vsi->back;
	int err = 0;

	if (!(pf->flags & I40E_FLAG_SRIOV_ENABLED))
		return -EOPNOTSUPP;

	if (vid) {
		pr_info("%s: vlans aren't supported yet for dev_uc|mc_add()\n", dev->name);
		return -EINVAL;
	}

	/* Hardware does not support aging addresses so if a
	 * ndm_state is given only allow permanent addresses
	 */
	if (ndm->ndm_state && !(ndm->ndm_state & NUD_PERMANENT)) {
		netdev_info(dev, "FDB only supports static addresses\n");
		return -EINVAL;
	}

	if (is_unicast_ether_addr(addr) || is_link_local_ether_addr(addr))
		err = dev_uc_add_excl(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_add_excl(dev, addr);
	else
		err = -EINVAL;

	/* Only return duplicate errors if NLM_F_EXCL is set */
	if (err == -EEXIST && !(flags & NLM_F_EXCL))
		err = 0;

	return err;
}

/**
 * i40e_ndo_bridge_setlink - Set the hardware bridge mode
 * @dev: the netdev being configured
 * @nlh: RTNL message
 * @flags: bridge flags
 * @extack: netlink extended ack
 *
 * Inserts a new hardware bridge if not already created and
 * enables the bridging mode requested (VEB or VEPA). If the
 * hardware bridge has already been inserted and the request
 * is to change the mode then that requires a PF reset to
 * allow rebuild of the components with required hardware
 * bridge mode enabled.
 *
 * Note: expects to be called while under rtnl_lock()
 **/
static int i40e_ndo_bridge_setlink(struct net_device *dev,
				   struct nlmsghdr *nlh,
				   u16 flags,
				   struct netlink_ext_ack *extack)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_veb *veb = NULL;
	struct nlattr *attr, *br_spec;
	int i, rem;

	/* Only for PF VSI for now */
	if (vsi->seid != pf->vsi[pf->lan_vsi]->seid)
		return -EOPNOTSUPP;

	/* Find the HW bridge for PF VSI */
	for (i = 0; i < I40E_MAX_VEB && !veb; i++) {
		if (pf->veb[i] && pf->veb[i]->seid == vsi->uplink_seid)
			veb = pf->veb[i];
	}

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);

	nla_for_each_nested(attr, br_spec, rem) {
		__u16 mode;

		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		mode = nla_get_u16(attr);
		if ((mode != BRIDGE_MODE_VEPA) &&
		    (mode != BRIDGE_MODE_VEB))
			return -EINVAL;

		/* Insert a new HW bridge */
		if (!veb) {
			veb = i40e_veb_setup(pf, 0, vsi->uplink_seid, vsi->seid,
					     vsi->tc_config.enabled_tc);
			if (veb) {
				veb->bridge_mode = mode;
				i40e_config_bridge_mode(veb);
			} else {
				/* No Bridge HW offload available */
				return -ENOENT;
			}
			break;
		} else if (mode != veb->bridge_mode) {
			/* Existing HW bridge but different mode needs reset */
			veb->bridge_mode = mode;
			/* TODO: If no VFs or VMDq VSIs, disallow VEB mode */
			if (mode == BRIDGE_MODE_VEB)
				pf->flags |= I40E_FLAG_VEB_MODE_ENABLED;
			else
				pf->flags &= ~I40E_FLAG_VEB_MODE_ENABLED;
			i40e_do_reset(pf, I40E_PF_RESET_FLAG, true);
			break;
		}
	}

	return 0;
}

/**
 * i40e_ndo_bridge_getlink - Get the hardware bridge mode
 * @skb: skb buff
 * @pid: process id
 * @seq: RTNL message seq #
 * @dev: the netdev being configured
 * @filter_mask: unused
 * @nlflags: netlink flags passed in
 *
 * Return the mode in which the hardware bridge is operating in
 * i.e VEB or VEPA.
 **/
static int i40e_ndo_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				   struct net_device *dev,
				   u32 __always_unused filter_mask,
				   int nlflags)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_veb *veb = NULL;
	int i;

	/* Only for PF VSI for now */
	if (vsi->seid != pf->vsi[pf->lan_vsi]->seid)
		return -EOPNOTSUPP;

	/* Find the HW bridge for the PF VSI */
	for (i = 0; i < I40E_MAX_VEB && !veb; i++) {
		if (pf->veb[i] && pf->veb[i]->seid == vsi->uplink_seid)
			veb = pf->veb[i];
	}

	if (!veb)
		return 0;

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, veb->bridge_mode,
				       0, 0, nlflags, filter_mask, NULL);
}

/**
 * i40e_features_check - Validate encapsulated packet conforms to limits
 * @skb: skb buff
 * @dev: This physical port's netdev
 * @features: Offload features that the stack believes apply
 **/
static netdev_features_t i40e_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	size_t len;

	/* No point in doing any of this if neither checksum nor GSO are
	 * being requested for this frame.  We can rule out both by just
	 * checking for CHECKSUM_PARTIAL
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return features;

	/* We cannot support GSO if the MSS is going to be less than
	 * 64 bytes.  If it is then we need to drop support for GSO.
	 */
	if (skb_is_gso(skb) && (skb_shinfo(skb)->gso_size < 64))
		features &= ~NETIF_F_GSO_MASK;

	/* MACLEN can support at most 63 words */
	len = skb_network_header(skb) - skb->data;
	if (len & ~(63 * 2))
		goto out_err;

	/* IPLEN and EIPLEN can support at most 127 dwords */
	len = skb_transport_header(skb) - skb_network_header(skb);
	if (len & ~(127 * 4))
		goto out_err;

	if (skb->encapsulation) {
		/* L4TUNLEN can support 127 words */
		len = skb_inner_network_header(skb) - skb_transport_header(skb);
		if (len & ~(127 * 2))
			goto out_err;

		/* IPLEN can support at most 127 dwords */
		len = skb_inner_transport_header(skb) -
		      skb_inner_network_header(skb);
		if (len & ~(127 * 4))
			goto out_err;
	}

	/* No need to validate L4LEN as TCP is the only protocol with a
	 * a flexible value and we support all possible values supported
	 * by TCP, which is at most 15 dwords
	 */

	return features;
out_err:
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

/**
 * i40e_xdp_setup - add/remove an XDP program
 * @vsi: VSI to changed
 * @prog: XDP program
 **/
static int i40e_xdp_setup(struct i40e_vsi *vsi,
			  struct bpf_prog *prog)
{
	int frame_size = vsi->netdev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	struct i40e_pf *pf = vsi->back;
	struct bpf_prog *old_prog;
	bool need_reset;
	int i;

	/* Don't allow frames that span over multiple buffers */
	if (frame_size > vsi->rx_buf_len)
		return -EINVAL;

	if (!i40e_enabled_xdp_vsi(vsi) && !prog)
		return 0;

	/* When turning XDP on->off/off->on we reset and rebuild the rings. */
	need_reset = (i40e_enabled_xdp_vsi(vsi) != !!prog);

	if (need_reset)
		i40e_prep_for_reset(pf, true);

	old_prog = xchg(&vsi->xdp_prog, prog);

	if (need_reset) {
		if (!prog)
			/* Wait until ndo_xsk_wakeup completes. */
			synchronize_rcu();
		i40e_reset_and_rebuild(pf, true, true);
	}

	for (i = 0; i < vsi->num_queue_pairs; i++)
		WRITE_ONCE(vsi->rx_rings[i]->xdp_prog, vsi->xdp_prog);

	if (old_prog)
		bpf_prog_put(old_prog);

	/* Kick start the NAPI context if there is an AF_XDP socket open
	 * on that queue id. This so that receiving will start.
	 */
	if (need_reset && prog)
		for (i = 0; i < vsi->num_queue_pairs; i++)
			if (vsi->xdp_rings[i]->xsk_pool)
				(void)i40e_xsk_wakeup(vsi->netdev, i,
						      XDP_WAKEUP_RX);

	return 0;
}

/**
 * i40e_enter_busy_conf - Enters busy config state
 * @vsi: vsi
 *
 * Returns 0 on success, <0 for failure.
 **/
static int i40e_enter_busy_conf(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int timeout = 50;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	return 0;
}

/**
 * i40e_exit_busy_conf - Exits busy config state
 * @vsi: vsi
 **/
static void i40e_exit_busy_conf(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	clear_bit(__I40E_CONFIG_BUSY, pf->state);
}

/**
 * i40e_queue_pair_reset_stats - Resets all statistics for a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 **/
static void i40e_queue_pair_reset_stats(struct i40e_vsi *vsi, int queue_pair)
{
	memset(&vsi->rx_rings[queue_pair]->rx_stats, 0,
	       sizeof(vsi->rx_rings[queue_pair]->rx_stats));
	memset(&vsi->tx_rings[queue_pair]->stats, 0,
	       sizeof(vsi->tx_rings[queue_pair]->stats));
	if (i40e_enabled_xdp_vsi(vsi)) {
		memset(&vsi->xdp_rings[queue_pair]->stats, 0,
		       sizeof(vsi->xdp_rings[queue_pair]->stats));
	}
}

/**
 * i40e_queue_pair_clean_rings - Cleans all the rings of a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 **/
static void i40e_queue_pair_clean_rings(struct i40e_vsi *vsi, int queue_pair)
{
	i40e_clean_tx_ring(vsi->tx_rings[queue_pair]);
	if (i40e_enabled_xdp_vsi(vsi)) {
		/* Make sure that in-progress ndo_xdp_xmit calls are
		 * completed.
		 */
		synchronize_rcu();
		i40e_clean_tx_ring(vsi->xdp_rings[queue_pair]);
	}
	i40e_clean_rx_ring(vsi->rx_rings[queue_pair]);
}

/**
 * i40e_queue_pair_toggle_napi - Enables/disables NAPI for a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 * @enable: true for enable, false for disable
 **/
static void i40e_queue_pair_toggle_napi(struct i40e_vsi *vsi, int queue_pair,
					bool enable)
{
	struct i40e_ring *rxr = vsi->rx_rings[queue_pair];
	struct i40e_q_vector *q_vector = rxr->q_vector;

	if (!vsi->netdev)
		return;

	/* All rings in a qp belong to the same qvector. */
	if (q_vector->rx.ring || q_vector->tx.ring) {
		if (enable)
			napi_enable(&q_vector->napi);
		else
			napi_disable(&q_vector->napi);
	}
}

/**
 * i40e_queue_pair_toggle_rings - Enables/disables all rings for a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 * @enable: true for enable, false for disable
 *
 * Returns 0 on success, <0 on failure.
 **/
static int i40e_queue_pair_toggle_rings(struct i40e_vsi *vsi, int queue_pair,
					bool enable)
{
	struct i40e_pf *pf = vsi->back;
	int pf_q, ret = 0;

	pf_q = vsi->base_queue + queue_pair;
	ret = i40e_control_wait_tx_q(vsi->seid, pf, pf_q,
				     false /*is xdp*/, enable);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "VSI seid %d Tx ring %d %sable timeout\n",
			 vsi->seid, pf_q, (enable ? "en" : "dis"));
		return ret;
	}

	i40e_control_rx_q(pf, pf_q, enable);
	ret = i40e_pf_rxq_wait(pf, pf_q, enable);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "VSI seid %d Rx ring %d %sable timeout\n",
			 vsi->seid, pf_q, (enable ? "en" : "dis"));
		return ret;
	}

	/* Due to HW errata, on Rx disable only, the register can
	 * indicate done before it really is. Needs 50ms to be sure
	 */
	if (!enable)
		mdelay(50);

	if (!i40e_enabled_xdp_vsi(vsi))
		return ret;

	ret = i40e_control_wait_tx_q(vsi->seid, pf,
				     pf_q + vsi->alloc_queue_pairs,
				     true /*is xdp*/, enable);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "VSI seid %d XDP Tx ring %d %sable timeout\n",
			 vsi->seid, pf_q, (enable ? "en" : "dis"));
	}

	return ret;
}

/**
 * i40e_queue_pair_enable_irq - Enables interrupts for a queue pair
 * @vsi: vsi
 * @queue_pair: queue_pair
 **/
static void i40e_queue_pair_enable_irq(struct i40e_vsi *vsi, int queue_pair)
{
	struct i40e_ring *rxr = vsi->rx_rings[queue_pair];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;

	/* All rings in a qp belong to the same qvector. */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		i40e_irq_dynamic_enable(vsi, rxr->q_vector->v_idx);
	else
		i40e_irq_dynamic_enable_icr0(pf);

	i40e_flush(hw);
}

/**
 * i40e_queue_pair_disable_irq - Disables interrupts for a queue pair
 * @vsi: vsi
 * @queue_pair: queue_pair
 **/
static void i40e_queue_pair_disable_irq(struct i40e_vsi *vsi, int queue_pair)
{
	struct i40e_ring *rxr = vsi->rx_rings[queue_pair];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;

	/* For simplicity, instead of removing the qp interrupt causes
	 * from the interrupt linked list, we simply disable the interrupt, and
	 * leave the list intact.
	 *
	 * All rings in a qp belong to the same qvector.
	 */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		u32 intpf = vsi->base_vector + rxr->q_vector->v_idx;

		wr32(hw, I40E_PFINT_DYN_CTLN(intpf - 1), 0);
		i40e_flush(hw);
		synchronize_irq(pf->msix_entries[intpf].vector);
	} else {
		/* Legacy and MSI mode - this stops all interrupt handling */
		wr32(hw, I40E_PFINT_ICR0_ENA, 0);
		wr32(hw, I40E_PFINT_DYN_CTL0, 0);
		i40e_flush(hw);
		synchronize_irq(pf->pdev->irq);
	}
}

/**
 * i40e_queue_pair_disable - Disables a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 *
 * Returns 0 on success, <0 on failure.
 **/
int i40e_queue_pair_disable(struct i40e_vsi *vsi, int queue_pair)
{
	int err;

	err = i40e_enter_busy_conf(vsi);
	if (err)
		return err;

	i40e_queue_pair_disable_irq(vsi, queue_pair);
	err = i40e_queue_pair_toggle_rings(vsi, queue_pair, false /* off */);
	i40e_queue_pair_toggle_napi(vsi, queue_pair, false /* off */);
	i40e_queue_pair_clean_rings(vsi, queue_pair);
	i40e_queue_pair_reset_stats(vsi, queue_pair);

	return err;
}

/**
 * i40e_queue_pair_enable - Enables a queue pair
 * @vsi: vsi
 * @queue_pair: queue pair
 *
 * Returns 0 on success, <0 on failure.
 **/
int i40e_queue_pair_enable(struct i40e_vsi *vsi, int queue_pair)
{
	int err;

	err = i40e_configure_tx_ring(vsi->tx_rings[queue_pair]);
	if (err)
		return err;

	if (i40e_enabled_xdp_vsi(vsi)) {
		err = i40e_configure_tx_ring(vsi->xdp_rings[queue_pair]);
		if (err)
			return err;
	}

	err = i40e_configure_rx_ring(vsi->rx_rings[queue_pair]);
	if (err)
		return err;

	err = i40e_queue_pair_toggle_rings(vsi, queue_pair, true /* on */);
	i40e_queue_pair_toggle_napi(vsi, queue_pair, true /* on */);
	i40e_queue_pair_enable_irq(vsi, queue_pair);

	i40e_exit_busy_conf(vsi);

	return err;
}

/**
 * i40e_xdp - implements ndo_bpf for i40e
 * @dev: netdevice
 * @xdp: XDP command
 **/
static int i40e_xdp(struct net_device *dev,
		    struct netdev_bpf *xdp)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;

	if (vsi->type != I40E_VSI_MAIN)
		return -EINVAL;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return i40e_xdp_setup(vsi, xdp->prog);
	case XDP_SETUP_XSK_POOL:
		return i40e_xsk_pool_setup(vsi, xdp->xsk.pool,
					   xdp->xsk.queue_id);
	default:
		return -EINVAL;
	}
}

static const struct net_device_ops i40e_netdev_ops = {
	.ndo_open		= i40e_open,
	.ndo_stop		= i40e_close,
	.ndo_start_xmit		= i40e_lan_xmit_frame,
	.ndo_get_stats64	= i40e_get_netdev_stats_struct,
	.ndo_set_rx_mode	= i40e_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= i40e_set_mac,
	.ndo_change_mtu		= i40e_change_mtu,
	.ndo_do_ioctl		= i40e_ioctl,
	.ndo_tx_timeout		= i40e_tx_timeout,
	.ndo_vlan_rx_add_vid	= i40e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= i40e_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= i40e_netpoll,
#endif
	.ndo_setup_tc		= __i40e_setup_tc,
	.ndo_select_queue	= i40e_lan_select_queue,
	.ndo_set_features	= i40e_set_features,
	.ndo_set_vf_mac		= i40e_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= i40e_ndo_set_vf_port_vlan,
	.ndo_get_vf_stats	= i40e_get_vf_stats,
	.ndo_set_vf_rate	= i40e_ndo_set_vf_bw,
	.ndo_get_vf_config	= i40e_ndo_get_vf_config,
	.ndo_set_vf_link_state	= i40e_ndo_set_vf_link_state,
	.ndo_set_vf_spoofchk	= i40e_ndo_set_vf_spoofchk,
	.ndo_set_vf_trust	= i40e_ndo_set_vf_trust,
	.ndo_udp_tunnel_add	= udp_tunnel_nic_add_port,
	.ndo_udp_tunnel_del	= udp_tunnel_nic_del_port,
	.ndo_get_phys_port_id	= i40e_get_phys_port_id,
	.ndo_fdb_add		= i40e_ndo_fdb_add,
	.ndo_features_check	= i40e_features_check,
	.ndo_bridge_getlink	= i40e_ndo_bridge_getlink,
	.ndo_bridge_setlink	= i40e_ndo_bridge_setlink,
	.ndo_bpf		= i40e_xdp,
	.ndo_xdp_xmit		= i40e_xdp_xmit,
	.ndo_xsk_wakeup	        = i40e_xsk_wakeup,
	.ndo_dfwd_add_station	= i40e_fwd_add,
	.ndo_dfwd_del_station	= i40e_fwd_del,
};

/**
 * i40e_config_netdev - Setup the netdev flags
 * @vsi: the VSI being configured
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_config_netdev(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_netdev_priv *np;
	struct net_device *netdev;
	u8 broadcast[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
	int etherdev_size;
	netdev_features_t hw_enc_features;
	netdev_features_t hw_features;

	etherdev_size = sizeof(struct i40e_netdev_priv);
	netdev = alloc_etherdev_mq(etherdev_size, vsi->alloc_queue_pairs);
	if (!netdev)
		return -ENOMEM;

	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	hw_enc_features = NETIF_F_SG			|
			  NETIF_F_IP_CSUM		|
			  NETIF_F_IPV6_CSUM		|
			  NETIF_F_HIGHDMA		|
			  NETIF_F_SOFT_FEATURES		|
			  NETIF_F_TSO			|
			  NETIF_F_TSO_ECN		|
			  NETIF_F_TSO6			|
			  NETIF_F_GSO_GRE		|
			  NETIF_F_GSO_GRE_CSUM		|
			  NETIF_F_GSO_PARTIAL		|
			  NETIF_F_GSO_IPXIP4		|
			  NETIF_F_GSO_IPXIP6		|
			  NETIF_F_GSO_UDP_TUNNEL	|
			  NETIF_F_GSO_UDP_TUNNEL_CSUM	|
			  NETIF_F_GSO_UDP_L4		|
			  NETIF_F_SCTP_CRC		|
			  NETIF_F_RXHASH		|
			  NETIF_F_RXCSUM		|
			  0;

	if (!(pf->hw_features & I40E_HW_OUTER_UDP_CSUM_CAPABLE))
		netdev->gso_partial_features |= NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->udp_tunnel_nic_info = &pf->udp_tunnel_nic;

	netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;

	netdev->hw_enc_features |= hw_enc_features;

	/* record features VLANs can make use of */
	netdev->vlan_features |= hw_enc_features | NETIF_F_TSO_MANGLEID;

	/* enable macvlan offloads */
	netdev->hw_features |= NETIF_F_HW_L2FW_DOFFLOAD;

	hw_features = hw_enc_features		|
		      NETIF_F_HW_VLAN_CTAG_TX	|
		      NETIF_F_HW_VLAN_CTAG_RX;

	if (!(pf->flags & I40E_FLAG_MFP_ENABLED))
		hw_features |= NETIF_F_NTUPLE | NETIF_F_HW_TC;

	netdev->hw_features |= hw_features;

	netdev->features |= hw_features | NETIF_F_HW_VLAN_CTAG_FILTER;
	netdev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	if (vsi->type == I40E_VSI_MAIN) {
		SET_NETDEV_DEV(netdev, &pf->pdev->dev);
		ether_addr_copy(mac_addr, hw->mac.perm_addr);
		/* The following steps are necessary for two reasons. First,
		 * some older NVM configurations load a default MAC-VLAN
		 * filter that will accept any tagged packet, and we want to
		 * replace this with a normal filter. Additionally, it is
		 * possible our MAC address was provided by the platform using
		 * Open Firmware or similar.
		 *
		 * Thus, we need to remove the default filter and install one
		 * specific to the MAC address.
		 */
		i40e_rm_default_mac_filter(vsi, mac_addr);
		spin_lock_bh(&vsi->mac_filter_hash_lock);
		i40e_add_mac_filter(vsi, mac_addr);
		spin_unlock_bh(&vsi->mac_filter_hash_lock);
	} else {
		/* Relate the VSI_VMDQ name to the VSI_MAIN name. Note that we
		 * are still limited by IFNAMSIZ, but we're adding 'v%d\0' to
		 * the end, which is 4 bytes long, so force truncation of the
		 * original name by IFNAMSIZ - 4
		 */
		snprintf(netdev->name, IFNAMSIZ, "%.*sv%%d",
			 IFNAMSIZ - 4,
			 pf->vsi[pf->lan_vsi]->netdev->name);
		eth_random_addr(mac_addr);

		spin_lock_bh(&vsi->mac_filter_hash_lock);
		i40e_add_mac_filter(vsi, mac_addr);
		spin_unlock_bh(&vsi->mac_filter_hash_lock);
	}

	/* Add the broadcast filter so that we initially will receive
	 * broadcast packets. Note that when a new VLAN is first added the
	 * driver will convert all filters marked I40E_VLAN_ANY into VLAN
	 * specific filters as part of transitioning into "vlan" operation.
	 * When more VLANs are added, the driver will copy each existing MAC
	 * filter and add it for the new VLAN.
	 *
	 * Broadcast filters are handled specially by
	 * i40e_sync_filters_subtask, as the driver must to set the broadcast
	 * promiscuous bit instead of adding this directly as a MAC/VLAN
	 * filter. The subtask will update the correct broadcast promiscuous
	 * bits as VLANs become active or inactive.
	 */
	eth_broadcast_addr(broadcast);
	spin_lock_bh(&vsi->mac_filter_hash_lock);
	i40e_add_mac_filter(vsi, broadcast);
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	ether_addr_copy(netdev->dev_addr, mac_addr);
	ether_addr_copy(netdev->perm_addr, mac_addr);

	/* i40iw_net_event() reads 16 bytes from neigh->primary_key */
	netdev->neigh_priv_len = sizeof(u32) * 4;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	/* Setup netdev TC information */
	i40e_vsi_config_netdev_tc(vsi, vsi->tc_config.enabled_tc);

	netdev->netdev_ops = &i40e_netdev_ops;
	netdev->watchdog_timeo = 5 * HZ;
	i40e_set_ethtool_ops(netdev);

	/* MTU range: 68 - 9706 */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = I40E_MAX_RXBUFFER - I40E_PACKET_HDR_PAD;

	return 0;
}

/**
 * i40e_vsi_delete - Delete a VSI from the switch
 * @vsi: the VSI being removed
 *
 * Returns 0 on success, negative value on failure
 **/
static void i40e_vsi_delete(struct i40e_vsi *vsi)
{
	/* remove default VSI is not allowed */
	if (vsi == vsi->back->vsi[vsi->back->lan_vsi])
		return;

	i40e_aq_delete_element(&vsi->back->hw, vsi->seid, NULL);
}

/**
 * i40e_is_vsi_uplink_mode_veb - Check if the VSI's uplink bridge mode is VEB
 * @vsi: the VSI being queried
 *
 * Returns 1 if HW bridge mode is VEB and return 0 in case of VEPA mode
 **/
int i40e_is_vsi_uplink_mode_veb(struct i40e_vsi *vsi)
{
	struct i40e_veb *veb;
	struct i40e_pf *pf = vsi->back;

	/* Uplink is not a bridge so default to VEB */
	if (vsi->veb_idx >= I40E_MAX_VEB)
		return 1;

	veb = pf->veb[vsi->veb_idx];
	if (!veb) {
		dev_info(&pf->pdev->dev,
			 "There is no veb associated with the bridge\n");
		return -ENOENT;
	}

	/* Uplink is a bridge in VEPA mode */
	if (veb->bridge_mode & BRIDGE_MODE_VEPA) {
		return 0;
	} else {
		/* Uplink is a bridge in VEB mode */
		return 1;
	}

	/* VEPA is now default bridge, so return 0 */
	return 0;
}

/**
 * i40e_add_vsi - Add a VSI to the switch
 * @vsi: the VSI being configured
 *
 * This initializes a VSI context depending on the VSI type to be added and
 * passes it down to the add_vsi aq command.
 **/
static int i40e_add_vsi(struct i40e_vsi *vsi)
{
	int ret = -ENODEV;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	struct i40e_mac_filter *f;
	struct hlist_node *h;
	int bkt;

	u8 enabled_tc = 0x1; /* TC0 enabled */
	int f_count = 0;

	memset(&ctxt, 0, sizeof(ctxt));
	switch (vsi->type) {
	case I40E_VSI_MAIN:
		/* The PF's main VSI is already setup as part of the
		 * device initialization, so we'll not bother with
		 * the add_vsi call, but we will retrieve the current
		 * VSI context.
		 */
		ctxt.seid = pf->main_vsi_seid;
		ctxt.pf_num = pf->hw.pf_id;
		ctxt.vf_num = 0;
		ret = i40e_aq_get_vsi_params(&pf->hw, &ctxt, NULL);
		ctxt.flags = I40E_AQ_VSI_TYPE_PF;
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "couldn't get PF vsi config, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			return -ENOENT;
		}
		vsi->info = ctxt.info;
		vsi->info.valid_sections = 0;

		vsi->seid = ctxt.seid;
		vsi->id = ctxt.vsi_number;

		enabled_tc = i40e_pf_get_tc_map(pf);

		/* Source pruning is enabled by default, so the flag is
		 * negative logic - if it's set, we need to fiddle with
		 * the VSI to disable source pruning.
		 */
		if (pf->flags & I40E_FLAG_SOURCE_PRUNING_DISABLED) {
			memset(&ctxt, 0, sizeof(ctxt));
			ctxt.seid = pf->main_vsi_seid;
			ctxt.pf_num = pf->hw.pf_id;
			ctxt.vf_num = 0;
			ctxt.info.valid_sections |=
				     cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
			ctxt.info.switch_id =
				   cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_LOCAL_LB);
			ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "update vsi failed, err %s aq_err %s\n",
					 i40e_stat_str(&pf->hw, ret),
					 i40e_aq_str(&pf->hw,
						     pf->hw.aq.asq_last_status));
				ret = -ENOENT;
				goto err;
			}
		}

		/* MFP mode setup queue map and update VSI */
		if ((pf->flags & I40E_FLAG_MFP_ENABLED) &&
		    !(pf->hw.func_caps.iscsi)) { /* NIC type PF */
			memset(&ctxt, 0, sizeof(ctxt));
			ctxt.seid = pf->main_vsi_seid;
			ctxt.pf_num = pf->hw.pf_id;
			ctxt.vf_num = 0;
			i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, false);
			ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "update vsi failed, err %s aq_err %s\n",
					 i40e_stat_str(&pf->hw, ret),
					 i40e_aq_str(&pf->hw,
						    pf->hw.aq.asq_last_status));
				ret = -ENOENT;
				goto err;
			}
			/* update the local VSI info queue map */
			i40e_vsi_update_queue_map(vsi, &ctxt);
			vsi->info.valid_sections = 0;
		} else {
			/* Default/Main VSI is only enabled for TC0
			 * reconfigure it to enable all TCs that are
			 * available on the port in SFP mode.
			 * For MFP case the iSCSI PF would use this
			 * flow to enable LAN+iSCSI TC.
			 */
			ret = i40e_vsi_config_tc(vsi, enabled_tc);
			if (ret) {
				/* Single TC condition is not fatal,
				 * message and continue
				 */
				dev_info(&pf->pdev->dev,
					 "failed to configure TCs for main VSI tc_map 0x%08x, err %s aq_err %s\n",
					 enabled_tc,
					 i40e_stat_str(&pf->hw, ret),
					 i40e_aq_str(&pf->hw,
						    pf->hw.aq.asq_last_status));
			}
		}
		break;

	case I40E_VSI_FDIR:
		ctxt.pf_num = hw->pf_id;
		ctxt.vf_num = 0;
		ctxt.uplink_seid = vsi->uplink_seid;
		ctxt.connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
		ctxt.flags = I40E_AQ_VSI_TYPE_PF;
		if ((pf->flags & I40E_FLAG_VEB_MODE_ENABLED) &&
		    (i40e_is_vsi_uplink_mode_veb(vsi))) {
			ctxt.info.valid_sections |=
			     cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
			ctxt.info.switch_id =
			   cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);
		}
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		break;

	case I40E_VSI_VMDQ2:
		ctxt.pf_num = hw->pf_id;
		ctxt.vf_num = 0;
		ctxt.uplink_seid = vsi->uplink_seid;
		ctxt.connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
		ctxt.flags = I40E_AQ_VSI_TYPE_VMDQ2;

		/* This VSI is connected to VEB so the switch_id
		 * should be set to zero by default.
		 */
		if (i40e_is_vsi_uplink_mode_veb(vsi)) {
			ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
			ctxt.info.switch_id =
				cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);
		}

		/* Setup the VSI tx/rx queue map for TC0 only for now */
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		break;

	case I40E_VSI_SRIOV:
		ctxt.pf_num = hw->pf_id;
		ctxt.vf_num = vsi->vf_id + hw->func_caps.vf_base_id;
		ctxt.uplink_seid = vsi->uplink_seid;
		ctxt.connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
		ctxt.flags = I40E_AQ_VSI_TYPE_VF;

		/* This VSI is connected to VEB so the switch_id
		 * should be set to zero by default.
		 */
		if (i40e_is_vsi_uplink_mode_veb(vsi)) {
			ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
			ctxt.info.switch_id =
				cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);
		}

		if (vsi->back->flags & I40E_FLAG_IWARP_ENABLED) {
			ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
			ctxt.info.queueing_opt_flags |=
				(I40E_AQ_VSI_QUE_OPT_TCP_ENA |
				 I40E_AQ_VSI_QUE_OPT_RSS_LUT_VSI);
		}

		ctxt.info.valid_sections |= cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
		ctxt.info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_MODE_ALL;
		if (pf->vf[vsi->vf_id].spoofchk) {
			ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_SECURITY_VALID);
			ctxt.info.sec_flags |=
				(I40E_AQ_VSI_SEC_FLAG_ENABLE_VLAN_CHK |
				 I40E_AQ_VSI_SEC_FLAG_ENABLE_MAC_CHK);
		}
		/* Setup the VSI tx/rx queue map for TC0 only for now */
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		break;

	case I40E_VSI_IWARP:
		/* send down message to iWARP */
		break;

	default:
		return -ENODEV;
	}

	if (vsi->type != I40E_VSI_MAIN) {
		ret = i40e_aq_add_vsi(hw, &ctxt, NULL);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "add vsi failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			ret = -ENOENT;
			goto err;
		}
		vsi->info = ctxt.info;
		vsi->info.valid_sections = 0;
		vsi->seid = ctxt.seid;
		vsi->id = ctxt.vsi_number;
	}

	vsi->active_filters = 0;
	clear_bit(__I40E_VSI_OVERFLOW_PROMISC, vsi->state);
	spin_lock_bh(&vsi->mac_filter_hash_lock);
	/* If macvlan filters already exist, force them to get loaded */
	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist) {
		f->state = I40E_FILTER_NEW;
		f_count++;
	}
	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	if (f_count) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		set_bit(__I40E_MACVLAN_SYNC_PENDING, pf->state);
	}

	/* Update VSI BW information */
	ret = i40e_vsi_get_bw_info(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get vsi bw info, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		/* VSI is already added so not tearing that up */
		ret = 0;
	}

err:
	return ret;
}

/**
 * i40e_vsi_release - Delete a VSI and free its resources
 * @vsi: the VSI being removed
 *
 * Returns 0 on success or < 0 on error
 **/
int i40e_vsi_release(struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f;
	struct hlist_node *h;
	struct i40e_veb *veb = NULL;
	struct i40e_pf *pf;
	u16 uplink_seid;
	int i, n, bkt;

	pf = vsi->back;

	/* release of a VEB-owner or last VSI is not allowed */
	if (vsi->flags & I40E_VSI_FLAG_VEB_OWNER) {
		dev_info(&pf->pdev->dev, "VSI %d has existing VEB %d\n",
			 vsi->seid, vsi->uplink_seid);
		return -ENODEV;
	}
	if (vsi == pf->vsi[pf->lan_vsi] &&
	    !test_bit(__I40E_DOWN, pf->state)) {
		dev_info(&pf->pdev->dev, "Can't remove PF VSI\n");
		return -ENODEV;
	}
	set_bit(__I40E_VSI_RELEASING, vsi->state);
	uplink_seid = vsi->uplink_seid;
	if (vsi->type != I40E_VSI_SRIOV) {
		if (vsi->netdev_registered) {
			vsi->netdev_registered = false;
			if (vsi->netdev) {
				/* results in a call to i40e_close() */
				unregister_netdev(vsi->netdev);
			}
		} else {
			i40e_vsi_close(vsi);
		}
		i40e_vsi_disable_irq(vsi);
	}

	spin_lock_bh(&vsi->mac_filter_hash_lock);

	/* clear the sync flag on all filters */
	if (vsi->netdev) {
		__dev_uc_unsync(vsi->netdev, NULL);
		__dev_mc_unsync(vsi->netdev, NULL);
	}

	/* make sure any remaining filters are marked for deletion */
	hash_for_each_safe(vsi->mac_filter_hash, bkt, h, f, hlist)
		__i40e_del_filter(vsi, f);

	spin_unlock_bh(&vsi->mac_filter_hash_lock);

	i40e_sync_vsi_filters(vsi);

	i40e_vsi_delete(vsi);
	i40e_vsi_free_q_vectors(vsi);
	if (vsi->netdev) {
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
	i40e_vsi_clear_rings(vsi);
	i40e_vsi_clear(vsi);

	/* If this was the last thing on the VEB, except for the
	 * controlling VSI, remove the VEB, which puts the controlling
	 * VSI onto the next level down in the switch.
	 *
	 * Well, okay, there's one more exception here: don't remove
	 * the orphan VEBs yet.  We'll wait for an explicit remove request
	 * from up the network stack.
	 */
	for (n = 0, i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i] &&
		    pf->vsi[i]->uplink_seid == uplink_seid &&
		    (pf->vsi[i]->flags & I40E_VSI_FLAG_VEB_OWNER) == 0) {
			n++;      /* count the VSIs */
		}
	}
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;
		if (pf->veb[i]->uplink_seid == uplink_seid)
			n++;     /* count the VEBs */
		if (pf->veb[i]->seid == uplink_seid)
			veb = pf->veb[i];
	}
	if (n == 0 && veb && veb->uplink_seid != 0)
		i40e_veb_release(veb);

	return 0;
}

/**
 * i40e_vsi_setup_vectors - Set up the q_vectors for the given VSI
 * @vsi: ptr to the VSI
 *
 * This should only be called after i40e_vsi_mem_alloc() which allocates the
 * corresponding SW VSI structure and initializes num_queue_pairs for the
 * newly allocated VSI.
 *
 * Returns 0 on success or negative on failure
 **/
static int i40e_vsi_setup_vectors(struct i40e_vsi *vsi)
{
	int ret = -ENOENT;
	struct i40e_pf *pf = vsi->back;

	if (vsi->q_vectors[0]) {
		dev_info(&pf->pdev->dev, "VSI %d has existing q_vectors\n",
			 vsi->seid);
		return -EEXIST;
	}

	if (vsi->base_vector) {
		dev_info(&pf->pdev->dev, "VSI %d has non-zero base vector %d\n",
			 vsi->seid, vsi->base_vector);
		return -EEXIST;
	}

	ret = i40e_vsi_alloc_q_vectors(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "failed to allocate %d q_vector for VSI %d, ret=%d\n",
			 vsi->num_q_vectors, vsi->seid, ret);
		vsi->num_q_vectors = 0;
		goto vector_setup_out;
	}

	/* In Legacy mode, we do not have to get any other vector since we
	 * piggyback on the misc/ICR0 for queue interrupts.
	*/
	if (!(pf->flags & I40E_FLAG_MSIX_ENABLED))
		return ret;
	if (vsi->num_q_vectors)
		vsi->base_vector = i40e_get_lump(pf, pf->irq_pile,
						 vsi->num_q_vectors, vsi->idx);
	if (vsi->base_vector < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get tracking for %d vectors for VSI %d, err=%d\n",
			 vsi->num_q_vectors, vsi->seid, vsi->base_vector);
		i40e_vsi_free_q_vectors(vsi);
		ret = -ENOENT;
		goto vector_setup_out;
	}

vector_setup_out:
	return ret;
}

/**
 * i40e_vsi_reinit_setup - return and reallocate resources for a VSI
 * @vsi: pointer to the vsi.
 *
 * This re-allocates a vsi's queue resources.
 *
 * Returns pointer to the successfully allocated and configured VSI sw struct
 * on success, otherwise returns NULL on failure.
 **/
static struct i40e_vsi *i40e_vsi_reinit_setup(struct i40e_vsi *vsi)
{
	u16 alloc_queue_pairs;
	struct i40e_pf *pf;
	u8 enabled_tc;
	int ret;

	if (!vsi)
		return NULL;

	pf = vsi->back;

	i40e_put_lump(pf->qp_pile, vsi->base_queue, vsi->idx);
	i40e_vsi_clear_rings(vsi);

	i40e_vsi_free_arrays(vsi, false);
	i40e_set_num_rings_in_vsi(vsi);
	ret = i40e_vsi_alloc_arrays(vsi, false);
	if (ret)
		goto err_vsi;

	alloc_queue_pairs = vsi->alloc_queue_pairs *
			    (i40e_enabled_xdp_vsi(vsi) ? 2 : 1);

	ret = i40e_get_lump(pf, pf->qp_pile, alloc_queue_pairs, vsi->idx);
	if (ret < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get tracking for %d queues for VSI %d err %d\n",
			 alloc_queue_pairs, vsi->seid, ret);
		goto err_vsi;
	}
	vsi->base_queue = ret;

	/* Update the FW view of the VSI. Force a reset of TC and queue
	 * layout configurations.
	 */
	enabled_tc = pf->vsi[pf->lan_vsi]->tc_config.enabled_tc;
	pf->vsi[pf->lan_vsi]->tc_config.enabled_tc = 0;
	pf->vsi[pf->lan_vsi]->seid = pf->main_vsi_seid;
	i40e_vsi_config_tc(pf->vsi[pf->lan_vsi], enabled_tc);
	if (vsi->type == I40E_VSI_MAIN)
		i40e_rm_default_mac_filter(vsi, pf->hw.mac.perm_addr);

	/* assign it some queues */
	ret = i40e_alloc_rings(vsi);
	if (ret)
		goto err_rings;

	/* map all of the rings to the q_vectors */
	i40e_vsi_map_rings_to_vectors(vsi);
	return vsi;

err_rings:
	i40e_vsi_free_q_vectors(vsi);
	if (vsi->netdev_registered) {
		vsi->netdev_registered = false;
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
	i40e_aq_delete_element(&pf->hw, vsi->seid, NULL);
err_vsi:
	i40e_vsi_clear(vsi);
	return NULL;
}

/**
 * i40e_vsi_setup - Set up a VSI by a given type
 * @pf: board private structure
 * @type: VSI type
 * @uplink_seid: the switch element to link to
 * @param1: usage depends upon VSI type. For VF types, indicates VF id
 *
 * This allocates the sw VSI structure and its queue resources, then add a VSI
 * to the identified VEB.
 *
 * Returns pointer to the successfully allocated and configure VSI sw struct on
 * success, otherwise returns NULL on failure.
 **/
struct i40e_vsi *i40e_vsi_setup(struct i40e_pf *pf, u8 type,
				u16 uplink_seid, u32 param1)
{
	struct i40e_vsi *vsi = NULL;
	struct i40e_veb *veb = NULL;
	u16 alloc_queue_pairs;
	int ret, i;
	int v_idx;

	/* The requested uplink_seid must be either
	 *     - the PF's port seid
	 *              no VEB is needed because this is the PF
	 *              or this is a Flow Director special case VSI
	 *     - seid of an existing VEB
	 *     - seid of a VSI that owns an existing VEB
	 *     - seid of a VSI that doesn't own a VEB
	 *              a new VEB is created and the VSI becomes the owner
	 *     - seid of the PF VSI, which is what creates the first VEB
	 *              this is a special case of the previous
	 *
	 * Find which uplink_seid we were given and create a new VEB if needed
	 */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (pf->veb[i] && pf->veb[i]->seid == uplink_seid) {
			veb = pf->veb[i];
			break;
		}
	}

	if (!veb && uplink_seid != pf->mac_seid) {

		for (i = 0; i < pf->num_alloc_vsi; i++) {
			if (pf->vsi[i] && pf->vsi[i]->seid == uplink_seid) {
				vsi = pf->vsi[i];
				break;
			}
		}
		if (!vsi) {
			dev_info(&pf->pdev->dev, "no such uplink_seid %d\n",
				 uplink_seid);
			return NULL;
		}

		if (vsi->uplink_seid == pf->mac_seid)
			veb = i40e_veb_setup(pf, 0, pf->mac_seid, vsi->seid,
					     vsi->tc_config.enabled_tc);
		else if ((vsi->flags & I40E_VSI_FLAG_VEB_OWNER) == 0)
			veb = i40e_veb_setup(pf, 0, vsi->uplink_seid, vsi->seid,
					     vsi->tc_config.enabled_tc);
		if (veb) {
			if (vsi->seid != pf->vsi[pf->lan_vsi]->seid) {
				dev_info(&vsi->back->pdev->dev,
					 "New VSI creation error, uplink seid of LAN VSI expected.\n");
				return NULL;
			}
			/* We come up by default in VEPA mode if SRIOV is not
			 * already enabled, in which case we can't force VEPA
			 * mode.
			 */
			if (!(pf->flags & I40E_FLAG_VEB_MODE_ENABLED)) {
				veb->bridge_mode = BRIDGE_MODE_VEPA;
				pf->flags &= ~I40E_FLAG_VEB_MODE_ENABLED;
			}
			i40e_config_bridge_mode(veb);
		}
		for (i = 0; i < I40E_MAX_VEB && !veb; i++) {
			if (pf->veb[i] && pf->veb[i]->seid == vsi->uplink_seid)
				veb = pf->veb[i];
		}
		if (!veb) {
			dev_info(&pf->pdev->dev, "couldn't add VEB\n");
			return NULL;
		}

		vsi->flags |= I40E_VSI_FLAG_VEB_OWNER;
		uplink_seid = veb->seid;
	}

	/* get vsi sw struct */
	v_idx = i40e_vsi_mem_alloc(pf, type);
	if (v_idx < 0)
		goto err_alloc;
	vsi = pf->vsi[v_idx];
	if (!vsi)
		goto err_alloc;
	vsi->type = type;
	vsi->veb_idx = (veb ? veb->idx : I40E_NO_VEB);

	if (type == I40E_VSI_MAIN)
		pf->lan_vsi = v_idx;
	else if (type == I40E_VSI_SRIOV)
		vsi->vf_id = param1;
	/* assign it some queues */
	alloc_queue_pairs = vsi->alloc_queue_pairs *
			    (i40e_enabled_xdp_vsi(vsi) ? 2 : 1);

	ret = i40e_get_lump(pf, pf->qp_pile, alloc_queue_pairs, vsi->idx);
	if (ret < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get tracking for %d queues for VSI %d err=%d\n",
			 alloc_queue_pairs, vsi->seid, ret);
		goto err_vsi;
	}
	vsi->base_queue = ret;

	/* get a VSI from the hardware */
	vsi->uplink_seid = uplink_seid;
	ret = i40e_add_vsi(vsi);
	if (ret)
		goto err_vsi;

	switch (vsi->type) {
	/* setup the netdev if needed */
	case I40E_VSI_MAIN:
	case I40E_VSI_VMDQ2:
		ret = i40e_config_netdev(vsi);
		if (ret)
			goto err_netdev;
		ret = i40e_netif_set_realnum_tx_rx_queues(vsi);
		if (ret)
			goto err_netdev;
		ret = register_netdev(vsi->netdev);
		if (ret)
			goto err_netdev;
		vsi->netdev_registered = true;
		netif_carrier_off(vsi->netdev);
#ifdef CONFIG_I40E_DCB
		/* Setup DCB netlink interface */
		i40e_dcbnl_setup(vsi);
#endif /* CONFIG_I40E_DCB */
		fallthrough;
	case I40E_VSI_FDIR:
		/* set up vectors and rings if needed */
		ret = i40e_vsi_setup_vectors(vsi);
		if (ret)
			goto err_msix;

		ret = i40e_alloc_rings(vsi);
		if (ret)
			goto err_rings;

		/* map all of the rings to the q_vectors */
		i40e_vsi_map_rings_to_vectors(vsi);

		i40e_vsi_reset_stats(vsi);
		break;
	default:
		/* no netdev or rings for the other VSI types */
		break;
	}

	if ((pf->hw_features & I40E_HW_RSS_AQ_CAPABLE) &&
	    (vsi->type == I40E_VSI_VMDQ2)) {
		ret = i40e_vsi_config_rss(vsi);
	}
	return vsi;

err_rings:
	i40e_vsi_free_q_vectors(vsi);
err_msix:
	if (vsi->netdev_registered) {
		vsi->netdev_registered = false;
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
err_netdev:
	i40e_aq_delete_element(&pf->hw, vsi->seid, NULL);
err_vsi:
	i40e_vsi_clear(vsi);
err_alloc:
	return NULL;
}

/**
 * i40e_veb_get_bw_info - Query VEB BW information
 * @veb: the veb to query
 *
 * Query the Tx scheduler BW configuration data for given VEB
 **/
static int i40e_veb_get_bw_info(struct i40e_veb *veb)
{
	struct i40e_aqc_query_switching_comp_ets_config_resp ets_data;
	struct i40e_aqc_query_switching_comp_bw_config_resp bw_data;
	struct i40e_pf *pf = veb->pf;
	struct i40e_hw *hw = &pf->hw;
	u32 tc_bw_max;
	int ret = 0;
	int i;

	ret = i40e_aq_query_switch_comp_bw_config(hw, veb->seid,
						  &bw_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "query veb bw config failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, hw->aq.asq_last_status));
		goto out;
	}

	ret = i40e_aq_query_switch_comp_ets_config(hw, veb->seid,
						   &ets_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "query veb bw ets config failed, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, hw->aq.asq_last_status));
		goto out;
	}

	veb->bw_limit = le16_to_cpu(ets_data.port_bw_limit);
	veb->bw_max_quanta = ets_data.tc_bw_max;
	veb->is_abs_credits = bw_data.absolute_credits_enable;
	veb->enabled_tc = ets_data.tc_valid_bits;
	tc_bw_max = le16_to_cpu(bw_data.tc_bw_max[0]) |
		    (le16_to_cpu(bw_data.tc_bw_max[1]) << 16);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		veb->bw_tc_share_credits[i] = bw_data.tc_bw_share_credits[i];
		veb->bw_tc_limit_credits[i] =
					le16_to_cpu(bw_data.tc_bw_limits[i]);
		veb->bw_tc_max_quanta[i] = ((tc_bw_max >> (i*4)) & 0x7);
	}

out:
	return ret;
}

/**
 * i40e_veb_mem_alloc - Allocates the next available struct veb in the PF
 * @pf: board private structure
 *
 * On error: returns error code (negative)
 * On success: returns vsi index in PF (positive)
 **/
static int i40e_veb_mem_alloc(struct i40e_pf *pf)
{
	int ret = -ENOENT;
	struct i40e_veb *veb;
	int i;

	/* Need to protect the allocation of switch elements at the PF level */
	mutex_lock(&pf->switch_mutex);

	/* VEB list may be fragmented if VEB creation/destruction has
	 * been happening.  We can afford to do a quick scan to look
	 * for any free slots in the list.
	 *
	 * find next empty veb slot, looping back around if necessary
	 */
	i = 0;
	while ((i < I40E_MAX_VEB) && (pf->veb[i] != NULL))
		i++;
	if (i >= I40E_MAX_VEB) {
		ret = -ENOMEM;
		goto err_alloc_veb;  /* out of VEB slots! */
	}

	veb = kzalloc(sizeof(*veb), GFP_KERNEL);
	if (!veb) {
		ret = -ENOMEM;
		goto err_alloc_veb;
	}
	veb->pf = pf;
	veb->idx = i;
	veb->enabled_tc = 1;

	pf->veb[i] = veb;
	ret = i;
err_alloc_veb:
	mutex_unlock(&pf->switch_mutex);
	return ret;
}

/**
 * i40e_switch_branch_release - Delete a branch of the switch tree
 * @branch: where to start deleting
 *
 * This uses recursion to find the tips of the branch to be
 * removed, deleting until we get back to and can delete this VEB.
 **/
static void i40e_switch_branch_release(struct i40e_veb *branch)
{
	struct i40e_pf *pf = branch->pf;
	u16 branch_seid = branch->seid;
	u16 veb_idx = branch->idx;
	int i;

	/* release any VEBs on this VEB - RECURSION */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;
		if (pf->veb[i]->uplink_seid == branch->seid)
			i40e_switch_branch_release(pf->veb[i]);
	}

	/* Release the VSIs on this VEB, but not the owner VSI.
	 *
	 * NOTE: Removing the last VSI on a VEB has the SIDE EFFECT of removing
	 *       the VEB itself, so don't use (*branch) after this loop.
	 */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (!pf->vsi[i])
			continue;
		if (pf->vsi[i]->uplink_seid == branch_seid &&
		   (pf->vsi[i]->flags & I40E_VSI_FLAG_VEB_OWNER) == 0) {
			i40e_vsi_release(pf->vsi[i]);
		}
	}

	/* There's one corner case where the VEB might not have been
	 * removed, so double check it here and remove it if needed.
	 * This case happens if the veb was created from the debugfs
	 * commands and no VSIs were added to it.
	 */
	if (pf->veb[veb_idx])
		i40e_veb_release(pf->veb[veb_idx]);
}

/**
 * i40e_veb_clear - remove veb struct
 * @veb: the veb to remove
 **/
static void i40e_veb_clear(struct i40e_veb *veb)
{
	if (!veb)
		return;

	if (veb->pf) {
		struct i40e_pf *pf = veb->pf;

		mutex_lock(&pf->switch_mutex);
		if (pf->veb[veb->idx] == veb)
			pf->veb[veb->idx] = NULL;
		mutex_unlock(&pf->switch_mutex);
	}

	kfree(veb);
}

/**
 * i40e_veb_release - Delete a VEB and free its resources
 * @veb: the VEB being removed
 **/
void i40e_veb_release(struct i40e_veb *veb)
{
	struct i40e_vsi *vsi = NULL;
	struct i40e_pf *pf;
	int i, n = 0;

	pf = veb->pf;

	/* find the remaining VSI and check for extras */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i] && pf->vsi[i]->uplink_seid == veb->seid) {
			n++;
			vsi = pf->vsi[i];
		}
	}
	if (n != 1) {
		dev_info(&pf->pdev->dev,
			 "can't remove VEB %d with %d VSIs left\n",
			 veb->seid, n);
		return;
	}

	/* move the remaining VSI to uplink veb */
	vsi->flags &= ~I40E_VSI_FLAG_VEB_OWNER;
	if (veb->uplink_seid) {
		vsi->uplink_seid = veb->uplink_seid;
		if (veb->uplink_seid == pf->mac_seid)
			vsi->veb_idx = I40E_NO_VEB;
		else
			vsi->veb_idx = veb->veb_idx;
	} else {
		/* floating VEB */
		vsi->uplink_seid = pf->vsi[pf->lan_vsi]->uplink_seid;
		vsi->veb_idx = pf->vsi[pf->lan_vsi]->veb_idx;
	}

	i40e_aq_delete_element(&pf->hw, veb->seid, NULL);
	i40e_veb_clear(veb);
}

/**
 * i40e_add_veb - create the VEB in the switch
 * @veb: the VEB to be instantiated
 * @vsi: the controlling VSI
 **/
static int i40e_add_veb(struct i40e_veb *veb, struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = veb->pf;
	bool enable_stats = !!(pf->flags & I40E_FLAG_VEB_STATS_ENABLED);
	int ret;

	ret = i40e_aq_add_veb(&pf->hw, veb->uplink_seid, vsi->seid,
			      veb->enabled_tc, false,
			      &veb->seid, enable_stats, NULL);

	/* get a VEB from the hardware */
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't add VEB, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return -EPERM;
	}

	/* get statistics counter */
	ret = i40e_aq_get_veb_parameters(&pf->hw, veb->seid, NULL, NULL,
					 &veb->stats_idx, NULL, NULL, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get VEB statistics idx, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return -EPERM;
	}
	ret = i40e_veb_get_bw_info(veb);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get VEB bw info, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		i40e_aq_delete_element(&pf->hw, veb->seid, NULL);
		return -ENOENT;
	}

	vsi->uplink_seid = veb->seid;
	vsi->veb_idx = veb->idx;
	vsi->flags |= I40E_VSI_FLAG_VEB_OWNER;

	return 0;
}

/**
 * i40e_veb_setup - Set up a VEB
 * @pf: board private structure
 * @flags: VEB setup flags
 * @uplink_seid: the switch element to link to
 * @vsi_seid: the initial VSI seid
 * @enabled_tc: Enabled TC bit-map
 *
 * This allocates the sw VEB structure and links it into the switch
 * It is possible and legal for this to be a duplicate of an already
 * existing VEB.  It is also possible for both uplink and vsi seids
 * to be zero, in order to create a floating VEB.
 *
 * Returns pointer to the successfully allocated VEB sw struct on
 * success, otherwise returns NULL on failure.
 **/
struct i40e_veb *i40e_veb_setup(struct i40e_pf *pf, u16 flags,
				u16 uplink_seid, u16 vsi_seid,
				u8 enabled_tc)
{
	struct i40e_veb *veb, *uplink_veb = NULL;
	int vsi_idx, veb_idx;
	int ret;

	/* if one seid is 0, the other must be 0 to create a floating relay */
	if ((uplink_seid == 0 || vsi_seid == 0) &&
	    (uplink_seid + vsi_seid != 0)) {
		dev_info(&pf->pdev->dev,
			 "one, not both seid's are 0: uplink=%d vsi=%d\n",
			 uplink_seid, vsi_seid);
		return NULL;
	}

	/* make sure there is such a vsi and uplink */
	for (vsi_idx = 0; vsi_idx < pf->num_alloc_vsi; vsi_idx++)
		if (pf->vsi[vsi_idx] && pf->vsi[vsi_idx]->seid == vsi_seid)
			break;
	if (vsi_idx == pf->num_alloc_vsi && vsi_seid != 0) {
		dev_info(&pf->pdev->dev, "vsi seid %d not found\n",
			 vsi_seid);
		return NULL;
	}

	if (uplink_seid && uplink_seid != pf->mac_seid) {
		for (veb_idx = 0; veb_idx < I40E_MAX_VEB; veb_idx++) {
			if (pf->veb[veb_idx] &&
			    pf->veb[veb_idx]->seid == uplink_seid) {
				uplink_veb = pf->veb[veb_idx];
				break;
			}
		}
		if (!uplink_veb) {
			dev_info(&pf->pdev->dev,
				 "uplink seid %d not found\n", uplink_seid);
			return NULL;
		}
	}

	/* get veb sw struct */
	veb_idx = i40e_veb_mem_alloc(pf);
	if (veb_idx < 0)
		goto err_alloc;
	veb = pf->veb[veb_idx];
	veb->flags = flags;
	veb->uplink_seid = uplink_seid;
	veb->veb_idx = (uplink_veb ? uplink_veb->idx : I40E_NO_VEB);
	veb->enabled_tc = (enabled_tc ? enabled_tc : 0x1);

	/* create the VEB in the switch */
	ret = i40e_add_veb(veb, pf->vsi[vsi_idx]);
	if (ret)
		goto err_veb;
	if (vsi_idx == pf->lan_vsi)
		pf->lan_veb = veb->idx;

	return veb;

err_veb:
	i40e_veb_clear(veb);
err_alloc:
	return NULL;
}

/**
 * i40e_setup_pf_switch_element - set PF vars based on switch type
 * @pf: board private structure
 * @ele: element we are building info from
 * @num_reported: total number of elements
 * @printconfig: should we print the contents
 *
 * helper function to assist in extracting a few useful SEID values.
 **/
static void i40e_setup_pf_switch_element(struct i40e_pf *pf,
				struct i40e_aqc_switch_config_element_resp *ele,
				u16 num_reported, bool printconfig)
{
	u16 downlink_seid = le16_to_cpu(ele->downlink_seid);
	u16 uplink_seid = le16_to_cpu(ele->uplink_seid);
	u8 element_type = ele->element_type;
	u16 seid = le16_to_cpu(ele->seid);

	if (printconfig)
		dev_info(&pf->pdev->dev,
			 "type=%d seid=%d uplink=%d downlink=%d\n",
			 element_type, seid, uplink_seid, downlink_seid);

	switch (element_type) {
	case I40E_SWITCH_ELEMENT_TYPE_MAC:
		pf->mac_seid = seid;
		break;
	case I40E_SWITCH_ELEMENT_TYPE_VEB:
		/* Main VEB? */
		if (uplink_seid != pf->mac_seid)
			break;
		if (pf->lan_veb >= I40E_MAX_VEB) {
			int v;

			/* find existing or else empty VEB */
			for (v = 0; v < I40E_MAX_VEB; v++) {
				if (pf->veb[v] && (pf->veb[v]->seid == seid)) {
					pf->lan_veb = v;
					break;
				}
			}
			if (pf->lan_veb >= I40E_MAX_VEB) {
				v = i40e_veb_mem_alloc(pf);
				if (v < 0)
					break;
				pf->lan_veb = v;
			}
		}
		if (pf->lan_veb >= I40E_MAX_VEB)
			break;

		pf->veb[pf->lan_veb]->seid = seid;
		pf->veb[pf->lan_veb]->uplink_seid = pf->mac_seid;
		pf->veb[pf->lan_veb]->pf = pf;
		pf->veb[pf->lan_veb]->veb_idx = I40E_NO_VEB;
		break;
	case I40E_SWITCH_ELEMENT_TYPE_VSI:
		if (num_reported != 1)
			break;
		/* This is immediately after a reset so we can assume this is
		 * the PF's VSI
		 */
		pf->mac_seid = uplink_seid;
		pf->pf_seid = downlink_seid;
		pf->main_vsi_seid = seid;
		if (printconfig)
			dev_info(&pf->pdev->dev,
				 "pf_seid=%d main_vsi_seid=%d\n",
				 pf->pf_seid, pf->main_vsi_seid);
		break;
	case I40E_SWITCH_ELEMENT_TYPE_PF:
	case I40E_SWITCH_ELEMENT_TYPE_VF:
	case I40E_SWITCH_ELEMENT_TYPE_EMP:
	case I40E_SWITCH_ELEMENT_TYPE_BMC:
	case I40E_SWITCH_ELEMENT_TYPE_PE:
	case I40E_SWITCH_ELEMENT_TYPE_PA:
		/* ignore these for now */
		break;
	default:
		dev_info(&pf->pdev->dev, "unknown element type=%d seid=%d\n",
			 element_type, seid);
		break;
	}
}

/**
 * i40e_fetch_switch_configuration - Get switch config from firmware
 * @pf: board private structure
 * @printconfig: should we print the contents
 *
 * Get the current switch configuration from the device and
 * extract a few useful SEID values.
 **/
int i40e_fetch_switch_configuration(struct i40e_pf *pf, bool printconfig)
{
	struct i40e_aqc_get_switch_config_resp *sw_config;
	u16 next_seid = 0;
	int ret = 0;
	u8 *aq_buf;
	int i;

	aq_buf = kzalloc(I40E_AQ_LARGE_BUF, GFP_KERNEL);
	if (!aq_buf)
		return -ENOMEM;

	sw_config = (struct i40e_aqc_get_switch_config_resp *)aq_buf;
	do {
		u16 num_reported, num_total;

		ret = i40e_aq_get_switch_config(&pf->hw, sw_config,
						I40E_AQ_LARGE_BUF,
						&next_seid, NULL);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "get switch config failed err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			kfree(aq_buf);
			return -ENOENT;
		}

		num_reported = le16_to_cpu(sw_config->header.num_reported);
		num_total = le16_to_cpu(sw_config->header.num_total);

		if (printconfig)
			dev_info(&pf->pdev->dev,
				 "header: %d reported %d total\n",
				 num_reported, num_total);

		for (i = 0; i < num_reported; i++) {
			struct i40e_aqc_switch_config_element_resp *ele =
				&sw_config->element[i];

			i40e_setup_pf_switch_element(pf, ele, num_reported,
						     printconfig);
		}
	} while (next_seid != 0);

	kfree(aq_buf);
	return ret;
}

/**
 * i40e_setup_pf_switch - Setup the HW switch on startup or after reset
 * @pf: board private structure
 * @reinit: if the Main VSI needs to re-initialized.
 * @lock_acquired: indicates whether or not the lock has been acquired
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_setup_pf_switch(struct i40e_pf *pf, bool reinit, bool lock_acquired)
{
	u16 flags = 0;
	int ret;

	/* find out what's out there already */
	ret = i40e_fetch_switch_configuration(pf, false);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't fetch switch config, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		return ret;
	}
	i40e_pf_reset_stats(pf);

	/* set the switch config bit for the whole device to
	 * support limited promisc or true promisc
	 * when user requests promisc. The default is limited
	 * promisc.
	*/

	if ((pf->hw.pf_id == 0) &&
	    !(pf->flags & I40E_FLAG_TRUE_PROMISC_SUPPORT)) {
		flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		pf->last_sw_conf_flags = flags;
	}

	if (pf->hw.pf_id == 0) {
		u16 valid_flags;

		valid_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		ret = i40e_aq_set_switch_config(&pf->hw, flags, valid_flags, 0,
						NULL);
		if (ret && pf->hw.aq.asq_last_status != I40E_AQ_RC_ESRCH) {
			dev_info(&pf->pdev->dev,
				 "couldn't set switch config bits, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			/* not a fatal problem, just keep going */
		}
		pf->last_sw_conf_valid_flags = valid_flags;
	}

	/* first time setup */
	if (pf->lan_vsi == I40E_NO_VSI || reinit) {
		struct i40e_vsi *vsi = NULL;
		u16 uplink_seid;

		/* Set up the PF VSI associated with the PF's main VSI
		 * that is already in the HW switch
		 */
		if (pf->lan_veb < I40E_MAX_VEB && pf->veb[pf->lan_veb])
			uplink_seid = pf->veb[pf->lan_veb]->seid;
		else
			uplink_seid = pf->mac_seid;
		if (pf->lan_vsi == I40E_NO_VSI)
			vsi = i40e_vsi_setup(pf, I40E_VSI_MAIN, uplink_seid, 0);
		else if (reinit)
			vsi = i40e_vsi_reinit_setup(pf->vsi[pf->lan_vsi]);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "setup of MAIN VSI failed\n");
			i40e_cloud_filter_exit(pf);
			i40e_fdir_teardown(pf);
			return -EAGAIN;
		}
	} else {
		/* force a reset of TC and queue layout configurations */
		u8 enabled_tc = pf->vsi[pf->lan_vsi]->tc_config.enabled_tc;

		pf->vsi[pf->lan_vsi]->tc_config.enabled_tc = 0;
		pf->vsi[pf->lan_vsi]->seid = pf->main_vsi_seid;
		i40e_vsi_config_tc(pf->vsi[pf->lan_vsi], enabled_tc);
	}
	i40e_vlan_stripping_disable(pf->vsi[pf->lan_vsi]);

	i40e_fdir_sb_setup(pf);

	/* Setup static PF queue filter control settings */
	ret = i40e_setup_pf_filter_control(pf);
	if (ret) {
		dev_info(&pf->pdev->dev, "setup_pf_filter_control failed: %d\n",
			 ret);
		/* Failure here should not stop continuing other steps */
	}

	/* enable RSS in the HW, even for only one queue, as the stack can use
	 * the hash
	 */
	if ((pf->flags & I40E_FLAG_RSS_ENABLED))
		i40e_pf_config_rss(pf);

	/* fill in link information and enable LSE reporting */
	i40e_link_event(pf);

	/* Initialize user-specific link properties */
	pf->fc_autoneg_status = ((pf->hw.phy.link_info.an_info &
				  I40E_AQ_AN_COMPLETED) ? true : false);

	i40e_ptp_init(pf);

	if (!lock_acquired)
		rtnl_lock();

	/* repopulate tunnel port filters */
	udp_tunnel_nic_reset_ntf(pf->vsi[pf->lan_vsi]->netdev);

	if (!lock_acquired)
		rtnl_unlock();

	return ret;
}

/**
 * i40e_determine_queue_usage - Work out queue distribution
 * @pf: board private structure
 **/
static void i40e_determine_queue_usage(struct i40e_pf *pf)
{
	int queues_left;
	int q_max;

	pf->num_lan_qps = 0;

	/* Find the max queues to be put into basic use.  We'll always be
	 * using TC0, whether or not DCB is running, and TC0 will get the
	 * big RSS set.
	 */
	queues_left = pf->hw.func_caps.num_tx_qp;

	if ((queues_left == 1) ||
	    !(pf->flags & I40E_FLAG_MSIX_ENABLED)) {
		/* one qp for PF, no queues for anything else */
		queues_left = 0;
		pf->alloc_rss_size = pf->num_lan_qps = 1;

		/* make sure all the fancies are disabled */
		pf->flags &= ~(I40E_FLAG_RSS_ENABLED	|
			       I40E_FLAG_IWARP_ENABLED	|
			       I40E_FLAG_FD_SB_ENABLED	|
			       I40E_FLAG_FD_ATR_ENABLED	|
			       I40E_FLAG_DCB_CAPABLE	|
			       I40E_FLAG_DCB_ENABLED	|
			       I40E_FLAG_SRIOV_ENABLED	|
			       I40E_FLAG_VMDQ_ENABLED);
		pf->flags |= I40E_FLAG_FD_SB_INACTIVE;
	} else if (!(pf->flags & (I40E_FLAG_RSS_ENABLED |
				  I40E_FLAG_FD_SB_ENABLED |
				  I40E_FLAG_FD_ATR_ENABLED |
				  I40E_FLAG_DCB_CAPABLE))) {
		/* one qp for PF */
		pf->alloc_rss_size = pf->num_lan_qps = 1;
		queues_left -= pf->num_lan_qps;

		pf->flags &= ~(I40E_FLAG_RSS_ENABLED	|
			       I40E_FLAG_IWARP_ENABLED	|
			       I40E_FLAG_FD_SB_ENABLED	|
			       I40E_FLAG_FD_ATR_ENABLED	|
			       I40E_FLAG_DCB_ENABLED	|
			       I40E_FLAG_VMDQ_ENABLED);
		pf->flags |= I40E_FLAG_FD_SB_INACTIVE;
	} else {
		/* Not enough queues for all TCs */
		if ((pf->flags & I40E_FLAG_DCB_CAPABLE) &&
		    (queues_left < I40E_MAX_TRAFFIC_CLASS)) {
			pf->flags &= ~(I40E_FLAG_DCB_CAPABLE |
					I40E_FLAG_DCB_ENABLED);
			dev_info(&pf->pdev->dev, "not enough queues for DCB. DCB is disabled.\n");
		}

		/* limit lan qps to the smaller of qps, cpus or msix */
		q_max = max_t(int, pf->rss_size_max, num_online_cpus());
		q_max = min_t(int, q_max, pf->hw.func_caps.num_tx_qp);
		q_max = min_t(int, q_max, pf->hw.func_caps.num_msix_vectors);
		pf->num_lan_qps = q_max;

		queues_left -= pf->num_lan_qps;
	}

	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		if (queues_left > 1) {
			queues_left -= 1; /* save 1 queue for FD */
		} else {
			pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
			pf->flags |= I40E_FLAG_FD_SB_INACTIVE;
			dev_info(&pf->pdev->dev, "not enough queues for Flow Director. Flow Director feature is disabled\n");
		}
	}

	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    pf->num_vf_qps && pf->num_req_vfs && queues_left) {
		pf->num_req_vfs = min_t(int, pf->num_req_vfs,
					(queues_left / pf->num_vf_qps));
		queues_left -= (pf->num_req_vfs * pf->num_vf_qps);
	}

	if ((pf->flags & I40E_FLAG_VMDQ_ENABLED) &&
	    pf->num_vmdq_vsis && pf->num_vmdq_qps && queues_left) {
		pf->num_vmdq_vsis = min_t(int, pf->num_vmdq_vsis,
					  (queues_left / pf->num_vmdq_qps));
		queues_left -= (pf->num_vmdq_vsis * pf->num_vmdq_qps);
	}

	pf->queues_left = queues_left;
	dev_dbg(&pf->pdev->dev,
		"qs_avail=%d FD SB=%d lan_qs=%d lan_tc0=%d vf=%d*%d vmdq=%d*%d, remaining=%d\n",
		pf->hw.func_caps.num_tx_qp,
		!!(pf->flags & I40E_FLAG_FD_SB_ENABLED),
		pf->num_lan_qps, pf->alloc_rss_size, pf->num_req_vfs,
		pf->num_vf_qps, pf->num_vmdq_vsis, pf->num_vmdq_qps,
		queues_left);
}

/**
 * i40e_setup_pf_filter_control - Setup PF static filter control
 * @pf: PF to be setup
 *
 * i40e_setup_pf_filter_control sets up a PF's initial filter control
 * settings. If PE/FCoE are enabled then it will also set the per PF
 * based filter sizes required for them. It also enables Flow director,
 * ethertype and macvlan type filter settings for the pf.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_setup_pf_filter_control(struct i40e_pf *pf)
{
	struct i40e_filter_control_settings *settings = &pf->filter_settings;

	settings->hash_lut_size = I40E_HASH_LUT_SIZE_128;

	/* Flow Director is enabled */
	if (pf->flags & (I40E_FLAG_FD_SB_ENABLED | I40E_FLAG_FD_ATR_ENABLED))
		settings->enable_fdir = true;

	/* Ethtype and MACVLAN filters enabled for PF */
	settings->enable_ethtype = true;
	settings->enable_macvlan = true;

	if (i40e_set_filter_control(&pf->hw, settings))
		return -ENOENT;

	return 0;
}

#define INFO_STRING_LEN 255
#define REMAIN(__x) (INFO_STRING_LEN - (__x))
static void i40e_print_features(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	char *buf;
	int i;

	buf = kmalloc(INFO_STRING_LEN, GFP_KERNEL);
	if (!buf)
		return;

	i = snprintf(buf, INFO_STRING_LEN, "Features: PF-id[%d]", hw->pf_id);
#ifdef CONFIG_PCI_IOV
	i += scnprintf(&buf[i], REMAIN(i), " VFs: %d", pf->num_req_vfs);
#endif
	i += scnprintf(&buf[i], REMAIN(i), " VSIs: %d QP: %d",
		      pf->hw.func_caps.num_vsis,
		      pf->vsi[pf->lan_vsi]->num_queue_pairs);
	if (pf->flags & I40E_FLAG_RSS_ENABLED)
		i += scnprintf(&buf[i], REMAIN(i), " RSS");
	if (pf->flags & I40E_FLAG_FD_ATR_ENABLED)
		i += scnprintf(&buf[i], REMAIN(i), " FD_ATR");
	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		i += scnprintf(&buf[i], REMAIN(i), " FD_SB");
		i += scnprintf(&buf[i], REMAIN(i), " NTUPLE");
	}
	if (pf->flags & I40E_FLAG_DCB_CAPABLE)
		i += scnprintf(&buf[i], REMAIN(i), " DCB");
	i += scnprintf(&buf[i], REMAIN(i), " VxLAN");
	i += scnprintf(&buf[i], REMAIN(i), " Geneve");
	if (pf->flags & I40E_FLAG_PTP)
		i += scnprintf(&buf[i], REMAIN(i), " PTP");
	if (pf->flags & I40E_FLAG_VEB_MODE_ENABLED)
		i += scnprintf(&buf[i], REMAIN(i), " VEB");
	else
		i += scnprintf(&buf[i], REMAIN(i), " VEPA");

	dev_info(&pf->pdev->dev, "%s\n", buf);
	kfree(buf);
	WARN_ON(i > INFO_STRING_LEN);
}

/**
 * i40e_get_platform_mac_addr - get platform-specific MAC address
 * @pdev: PCI device information struct
 * @pf: board private structure
 *
 * Look up the MAC address for the device. First we'll try
 * eth_platform_get_mac_address, which will check Open Firmware, or arch
 * specific fallback. Otherwise, we'll default to the stored value in
 * firmware.
 **/
static void i40e_get_platform_mac_addr(struct pci_dev *pdev, struct i40e_pf *pf)
{
	if (eth_platform_get_mac_address(&pdev->dev, pf->hw.mac.addr))
		i40e_get_mac_addr(&pf->hw, pf->hw.mac.addr);
}

/**
 * i40e_set_fec_in_flags - helper function for setting FEC options in flags
 * @fec_cfg: FEC option to set in flags
 * @flags: ptr to flags in which we set FEC option
 **/
void i40e_set_fec_in_flags(u8 fec_cfg, u32 *flags)
{
	if (fec_cfg & I40E_AQ_SET_FEC_AUTO)
		*flags |= I40E_FLAG_RS_FEC | I40E_FLAG_BASE_R_FEC;
	if ((fec_cfg & I40E_AQ_SET_FEC_REQUEST_RS) ||
	    (fec_cfg & I40E_AQ_SET_FEC_ABILITY_RS)) {
		*flags |= I40E_FLAG_RS_FEC;
		*flags &= ~I40E_FLAG_BASE_R_FEC;
	}
	if ((fec_cfg & I40E_AQ_SET_FEC_REQUEST_KR) ||
	    (fec_cfg & I40E_AQ_SET_FEC_ABILITY_KR)) {
		*flags |= I40E_FLAG_BASE_R_FEC;
		*flags &= ~I40E_FLAG_RS_FEC;
	}
	if (fec_cfg == 0)
		*flags &= ~(I40E_FLAG_RS_FEC | I40E_FLAG_BASE_R_FEC);
}

/**
 * i40e_check_recovery_mode - check if we are running transition firmware
 * @pf: board private structure
 *
 * Check registers indicating the firmware runs in recovery mode. Sets the
 * appropriate driver state.
 *
 * Returns true if the recovery mode was detected, false otherwise
 **/
static bool i40e_check_recovery_mode(struct i40e_pf *pf)
{
	u32 val = rd32(&pf->hw, I40E_GL_FWSTS);

	if (val & I40E_GL_FWSTS_FWS1B_MASK) {
		dev_crit(&pf->pdev->dev, "Firmware recovery mode detected. Limiting functionality.\n");
		dev_crit(&pf->pdev->dev, "Refer to the Intel(R) Ethernet Adapters and Devices User Guide for details on firmware recovery mode.\n");
		set_bit(__I40E_RECOVERY_MODE, pf->state);

		return true;
	}
	if (test_bit(__I40E_RECOVERY_MODE, pf->state))
		dev_info(&pf->pdev->dev, "Please do Power-On Reset to initialize adapter in normal mode with full functionality.\n");

	return false;
}

/**
 * i40e_pf_loop_reset - perform reset in a loop.
 * @pf: board private structure
 *
 * This function is useful when a NIC is about to enter recovery mode.
 * When a NIC's internal data structures are corrupted the NIC's
 * firmware is going to enter recovery mode.
 * Right after a POR it takes about 7 minutes for firmware to enter
 * recovery mode. Until that time a NIC is in some kind of intermediate
 * state. After that time period the NIC almost surely enters
 * recovery mode. The only way for a driver to detect intermediate
 * state is to issue a series of pf-resets and check a return value.
 * If a PF reset returns success then the firmware could be in recovery
 * mode so the caller of this code needs to check for recovery mode
 * if this function returns success. There is a little chance that
 * firmware will hang in intermediate state forever.
 * Since waiting 7 minutes is quite a lot of time this function waits
 * 10 seconds and then gives up by returning an error.
 *
 * Return 0 on success, negative on failure.
 **/
static i40e_status i40e_pf_loop_reset(struct i40e_pf *pf)
{
	/* wait max 10 seconds for PF reset to succeed */
	const unsigned long time_end = jiffies + 10 * HZ;

	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;

	ret = i40e_pf_reset(hw);
	while (ret != I40E_SUCCESS && time_before(jiffies, time_end)) {
		usleep_range(10000, 20000);
		ret = i40e_pf_reset(hw);
	}

	if (ret == I40E_SUCCESS)
		pf->pfr_count++;
	else
		dev_info(&pf->pdev->dev, "PF reset failed: %d\n", ret);

	return ret;
}

/**
 * i40e_check_fw_empr - check if FW issued unexpected EMP Reset
 * @pf: board private structure
 *
 * Check FW registers to determine if FW issued unexpected EMP Reset.
 * Every time when unexpected EMP Reset occurs the FW increments
 * a counter of unexpected EMP Resets. When the counter reaches 10
 * the FW should enter the Recovery mode
 *
 * Returns true if FW issued unexpected EMP Reset
 **/
static bool i40e_check_fw_empr(struct i40e_pf *pf)
{
	const u32 fw_sts = rd32(&pf->hw, I40E_GL_FWSTS) &
			   I40E_GL_FWSTS_FWS1B_MASK;
	return (fw_sts > I40E_GL_FWSTS_FWS1B_EMPR_0) &&
	       (fw_sts <= I40E_GL_FWSTS_FWS1B_EMPR_10);
}

/**
 * i40e_handle_resets - handle EMP resets and PF resets
 * @pf: board private structure
 *
 * Handle both EMP resets and PF resets and conclude whether there are
 * any issues regarding these resets. If there are any issues then
 * generate log entry.
 *
 * Return 0 if NIC is healthy or negative value when there are issues
 * with resets
 **/
static i40e_status i40e_handle_resets(struct i40e_pf *pf)
{
	const i40e_status pfr = i40e_pf_loop_reset(pf);
	const bool is_empr = i40e_check_fw_empr(pf);

	if (is_empr || pfr != I40E_SUCCESS)
		dev_crit(&pf->pdev->dev, "Entering recovery mode due to repeated FW resets. This may take several minutes. Refer to the Intel(R) Ethernet Adapters and Devices User Guide.\n");

	return is_empr ? I40E_ERR_RESET_FAILED : pfr;
}

/**
 * i40e_init_recovery_mode - initialize subsystems needed in recovery mode
 * @pf: board private structure
 * @hw: ptr to the hardware info
 *
 * This function does a minimal setup of all subsystems needed for running
 * recovery mode.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_init_recovery_mode(struct i40e_pf *pf, struct i40e_hw *hw)
{
	struct i40e_vsi *vsi;
	int err;
	int v_idx;

	pci_save_state(pf->pdev);

	/* set up periodic task facility */
	timer_setup(&pf->service_timer, i40e_service_timer, 0);
	pf->service_timer_period = HZ;

	INIT_WORK(&pf->service_task, i40e_service_task);
	clear_bit(__I40E_SERVICE_SCHED, pf->state);

	err = i40e_init_interrupt_scheme(pf);
	if (err)
		goto err_switch_setup;

	/* The number of VSIs reported by the FW is the minimum guaranteed
	 * to us; HW supports far more and we share the remaining pool with
	 * the other PFs. We allocate space for more than the guarantee with
	 * the understanding that we might not get them all later.
	 */
	if (pf->hw.func_caps.num_vsis < I40E_MIN_VSI_ALLOC)
		pf->num_alloc_vsi = I40E_MIN_VSI_ALLOC;
	else
		pf->num_alloc_vsi = pf->hw.func_caps.num_vsis;

	/* Set up the vsi struct and our local tracking of the MAIN PF vsi. */
	pf->vsi = kcalloc(pf->num_alloc_vsi, sizeof(struct i40e_vsi *),
			  GFP_KERNEL);
	if (!pf->vsi) {
		err = -ENOMEM;
		goto err_switch_setup;
	}

	/* We allocate one VSI which is needed as absolute minimum
	 * in order to register the netdev
	 */
	v_idx = i40e_vsi_mem_alloc(pf, I40E_VSI_MAIN);
	if (v_idx < 0) {
		err = v_idx;
		goto err_switch_setup;
	}
	pf->lan_vsi = v_idx;
	vsi = pf->vsi[v_idx];
	if (!vsi) {
		err = -EFAULT;
		goto err_switch_setup;
	}
	vsi->alloc_queue_pairs = 1;
	err = i40e_config_netdev(vsi);
	if (err)
		goto err_switch_setup;
	err = register_netdev(vsi->netdev);
	if (err)
		goto err_switch_setup;
	vsi->netdev_registered = true;
	i40e_dbg_pf_init(pf);

	err = i40e_setup_misc_vector_for_recovery_mode(pf);
	if (err)
		goto err_switch_setup;

	/* tell the firmware that we're starting */
	i40e_send_version(pf);

	/* since everything's happy, start the service_task timer */
	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));

	return 0;

err_switch_setup:
	i40e_reset_interrupt_capability(pf);
	del_timer_sync(&pf->service_timer);
	i40e_shutdown_adminq(hw);
	iounmap(hw->hw_addr);
	pci_disable_pcie_error_reporting(pf->pdev);
	pci_release_mem_regions(pf->pdev);
	pci_disable_device(pf->pdev);
	kfree(pf);

	return err;
}

/**
 * i40e_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in i40e_pci_tbl
 *
 * i40e_probe initializes a PF identified by a pci_dev structure.
 * The OS initialization, configuring of the PF private structure,
 * and a hardware reset occur.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_pf *pf;
	struct i40e_hw *hw;
	static u16 pfs_found;
	u16 wol_nvm_bits;
	u16 link_status;
	int err;
	u32 val;
	u32 i;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"DMA configuration failed: 0x%x\n", err);
			goto err_dma;
		}
	}

	/* set up pci connections */
	err = pci_request_mem_regions(pdev, i40e_driver_name);
	if (err) {
		dev_info(&pdev->dev,
			 "pci_request_selected_regions failed %d\n", err);
		goto err_pci_reg;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	/* Now that we have a PCI connection, we need to do the
	 * low level device setup.  This is primarily setting up
	 * the Admin Queue structures and then querying for the
	 * device's current profile information.
	 */
	pf = kzalloc(sizeof(*pf), GFP_KERNEL);
	if (!pf) {
		err = -ENOMEM;
		goto err_pf_alloc;
	}
	pf->next_vsi = 0;
	pf->pdev = pdev;
	set_bit(__I40E_DOWN, pf->state);

	hw = &pf->hw;
	hw->back = pf;

	pf->ioremap_len = min_t(int, pci_resource_len(pdev, 0),
				I40E_MAX_CSR_SPACE);
	/* We believe that the highest register to read is
	 * I40E_GLGEN_STAT_CLEAR, so we check if the BAR size
	 * is not less than that before mapping to prevent a
	 * kernel panic.
	 */
	if (pf->ioremap_len < I40E_GLGEN_STAT_CLEAR) {
		dev_err(&pdev->dev, "Cannot map registers, bar size 0x%X too small, aborting\n",
			pf->ioremap_len);
		err = -ENOMEM;
		goto err_ioremap;
	}
	hw->hw_addr = ioremap(pci_resource_start(pdev, 0), pf->ioremap_len);
	if (!hw->hw_addr) {
		err = -EIO;
		dev_info(&pdev->dev, "ioremap(0x%04x, 0x%04x) failed: 0x%x\n",
			 (unsigned int)pci_resource_start(pdev, 0),
			 pf->ioremap_len, err);
		goto err_ioremap;
	}
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);
	hw->bus.bus_id = pdev->bus->number;
	pf->instance = pfs_found;

	/* Select something other than the 802.1ad ethertype for the
	 * switch to use internally and drop on ingress.
	 */
	hw->switch_tag = 0xffff;
	hw->first_tag = ETH_P_8021AD;
	hw->second_tag = ETH_P_8021Q;

	INIT_LIST_HEAD(&pf->l3_flex_pit_list);
	INIT_LIST_HEAD(&pf->l4_flex_pit_list);
	INIT_LIST_HEAD(&pf->ddp_old_prof);

	/* set up the locks for the AQ, do this only once in probe
	 * and destroy them only once in remove
	 */
	mutex_init(&hw->aq.asq_mutex);
	mutex_init(&hw->aq.arq_mutex);

	pf->msg_enable = netif_msg_init(debug,
					NETIF_MSG_DRV |
					NETIF_MSG_PROBE |
					NETIF_MSG_LINK);
	if (debug < -1)
		pf->hw.debug_mask = debug;

	/* do a special CORER for clearing PXE mode once at init */
	if (hw->revision_id == 0 &&
	    (rd32(hw, I40E_GLLAN_RCTL_0) & I40E_GLLAN_RCTL_0_PXE_MODE_MASK)) {
		wr32(hw, I40E_GLGEN_RTRIG, I40E_GLGEN_RTRIG_CORER_MASK);
		i40e_flush(hw);
		msleep(200);
		pf->corer_count++;

		i40e_clear_pxe_mode(hw);
	}

	/* Reset here to make sure all is clean and to define PF 'n' */
	i40e_clear_hw(hw);

	err = i40e_set_mac_type(hw);
	if (err) {
		dev_warn(&pdev->dev, "unidentified MAC or BLANK NVM: %d\n",
			 err);
		goto err_pf_reset;
	}

	err = i40e_handle_resets(pf);
	if (err)
		goto err_pf_reset;

	i40e_check_recovery_mode(pf);

	hw->aq.num_arq_entries = I40E_AQ_LEN;
	hw->aq.num_asq_entries = I40E_AQ_LEN;
	hw->aq.arq_buf_size = I40E_MAX_AQ_BUF_SIZE;
	hw->aq.asq_buf_size = I40E_MAX_AQ_BUF_SIZE;
	pf->adminq_work_limit = I40E_AQ_WORK_LIMIT;

	snprintf(pf->int_name, sizeof(pf->int_name) - 1,
		 "%s-%s:misc",
		 dev_driver_string(&pf->pdev->dev), dev_name(&pdev->dev));

	err = i40e_init_shared_code(hw);
	if (err) {
		dev_warn(&pdev->dev, "unidentified MAC or BLANK NVM: %d\n",
			 err);
		goto err_pf_reset;
	}

	/* set up a default setting for link flow control */
	pf->hw.fc.requested_mode = I40E_FC_NONE;

	err = i40e_init_adminq(hw);
	if (err) {
		if (err == I40E_ERR_FIRMWARE_API_VERSION)
			dev_info(&pdev->dev,
				 "The driver for the device stopped because the NVM image v%u.%u is newer than expected v%u.%u. You must install the most recent version of the network driver.\n",
				 hw->aq.api_maj_ver,
				 hw->aq.api_min_ver,
				 I40E_FW_API_VERSION_MAJOR,
				 I40E_FW_MINOR_VERSION(hw));
		else
			dev_info(&pdev->dev,
				 "The driver for the device stopped because the device firmware failed to init. Try updating your NVM image.\n");

		goto err_pf_reset;
	}
	i40e_get_oem_version(hw);

	/* provide nvm, fw, api versions, vendor:device id, subsys vendor:device id */
	dev_info(&pdev->dev, "fw %d.%d.%05d api %d.%d nvm %s [%04x:%04x] [%04x:%04x]\n",
		 hw->aq.fw_maj_ver, hw->aq.fw_min_ver, hw->aq.fw_build,
		 hw->aq.api_maj_ver, hw->aq.api_min_ver,
		 i40e_nvm_version_str(hw), hw->vendor_id, hw->device_id,
		 hw->subsystem_vendor_id, hw->subsystem_device_id);

	if (hw->aq.api_maj_ver == I40E_FW_API_VERSION_MAJOR &&
	    hw->aq.api_min_ver > I40E_FW_MINOR_VERSION(hw))
		dev_dbg(&pdev->dev,
			"The driver for the device detected a newer version of the NVM image v%u.%u than v%u.%u.\n",
			 hw->aq.api_maj_ver,
			 hw->aq.api_min_ver,
			 I40E_FW_API_VERSION_MAJOR,
			 I40E_FW_MINOR_VERSION(hw));
	else if (hw->aq.api_maj_ver == 1 && hw->aq.api_min_ver < 4)
		dev_info(&pdev->dev,
			 "The driver for the device detected an older version of the NVM image v%u.%u than expected v%u.%u. Please update the NVM image.\n",
			 hw->aq.api_maj_ver,
			 hw->aq.api_min_ver,
			 I40E_FW_API_VERSION_MAJOR,
			 I40E_FW_MINOR_VERSION(hw));

	i40e_verify_eeprom(pf);

	/* Rev 0 hardware was never productized */
	if (hw->revision_id < 1)
		dev_warn(&pdev->dev, "This device is a pre-production adapter/LOM. Please be aware there may be issues with your hardware. If you are experiencing problems please contact your Intel or hardware representative who provided you with this hardware.\n");

	i40e_clear_pxe_mode(hw);

	err = i40e_get_capabilities(pf, i40e_aqc_opc_list_func_capabilities);
	if (err)
		goto err_adminq_setup;

	err = i40e_sw_init(pf);
	if (err) {
		dev_info(&pdev->dev, "sw_init failed: %d\n", err);
		goto err_sw_init;
	}

	if (test_bit(__I40E_RECOVERY_MODE, pf->state))
		return i40e_init_recovery_mode(pf, hw);

	err = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp, 0, 0);
	if (err) {
		dev_info(&pdev->dev, "init_lan_hmc failed: %d\n", err);
		goto err_init_lan_hmc;
	}

	err = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (err) {
		dev_info(&pdev->dev, "configure_lan_hmc failed: %d\n", err);
		err = -ENOENT;
		goto err_configure_lan_hmc;
	}

	/* Disable LLDP for NICs that have firmware versions lower than v4.3.
	 * Ignore error return codes because if it was already disabled via
	 * hardware settings this will fail
	 */
	if (pf->hw_features & I40E_HW_STOP_FW_LLDP) {
		dev_info(&pdev->dev, "Stopping firmware LLDP agent.\n");
		i40e_aq_stop_lldp(hw, true, false, NULL);
	}

	/* allow a platform config to override the HW addr */
	i40e_get_platform_mac_addr(pdev, pf);

	if (!is_valid_ether_addr(hw->mac.addr)) {
		dev_info(&pdev->dev, "invalid MAC address %pM\n", hw->mac.addr);
		err = -EIO;
		goto err_mac_addr;
	}
	dev_info(&pdev->dev, "MAC address: %pM\n", hw->mac.addr);
	ether_addr_copy(hw->mac.perm_addr, hw->mac.addr);
	i40e_get_port_mac_addr(hw, hw->mac.port_addr);
	if (is_valid_ether_addr(hw->mac.port_addr))
		pf->hw_features |= I40E_HW_PORT_ID_VALID;

	pci_set_drvdata(pdev, pf);
	pci_save_state(pdev);

	dev_info(&pdev->dev,
		 (pf->flags & I40E_FLAG_DISABLE_FW_LLDP) ?
			"FW LLDP is disabled\n" :
			"FW LLDP is enabled\n");

	/* Enable FW to write default DCB config on link-up */
	i40e_aq_set_dcb_parameters(hw, true, NULL);

#ifdef CONFIG_I40E_DCB
	err = i40e_init_pf_dcb(pf);
	if (err) {
		dev_info(&pdev->dev, "DCB init failed %d, disabled\n", err);
		pf->flags &= ~(I40E_FLAG_DCB_CAPABLE | I40E_FLAG_DCB_ENABLED);
		/* Continue without DCB enabled */
	}
#endif /* CONFIG_I40E_DCB */

	/* set up periodic task facility */
	timer_setup(&pf->service_timer, i40e_service_timer, 0);
	pf->service_timer_period = HZ;

	INIT_WORK(&pf->service_task, i40e_service_task);
	clear_bit(__I40E_SERVICE_SCHED, pf->state);

	/* NVM bit on means WoL disabled for the port */
	i40e_read_nvm_word(hw, I40E_SR_NVM_WAKE_ON_LAN, &wol_nvm_bits);
	if (BIT (hw->port) & wol_nvm_bits || hw->partition_id != 1)
		pf->wol_en = false;
	else
		pf->wol_en = true;
	device_set_wakeup_enable(&pf->pdev->dev, pf->wol_en);

	/* set up the main switch operations */
	i40e_determine_queue_usage(pf);
	err = i40e_init_interrupt_scheme(pf);
	if (err)
		goto err_switch_setup;

	pf->udp_tunnel_nic.set_port = i40e_udp_tunnel_set_port;
	pf->udp_tunnel_nic.unset_port = i40e_udp_tunnel_unset_port;
	pf->udp_tunnel_nic.flags = UDP_TUNNEL_NIC_INFO_MAY_SLEEP;
	pf->udp_tunnel_nic.shared = &pf->udp_tunnel_shared;
	pf->udp_tunnel_nic.tables[0].n_entries = I40E_MAX_PF_UDP_OFFLOAD_PORTS;
	pf->udp_tunnel_nic.tables[0].tunnel_types = UDP_TUNNEL_TYPE_VXLAN |
						    UDP_TUNNEL_TYPE_GENEVE;

	/* The number of VSIs reported by the FW is the minimum guaranteed
	 * to us; HW supports far more and we share the remaining pool with
	 * the other PFs. We allocate space for more than the guarantee with
	 * the understanding that we might not get them all later.
	 */
	if (pf->hw.func_caps.num_vsis < I40E_MIN_VSI_ALLOC)
		pf->num_alloc_vsi = I40E_MIN_VSI_ALLOC;
	else
		pf->num_alloc_vsi = pf->hw.func_caps.num_vsis;
	if (pf->num_alloc_vsi > UDP_TUNNEL_NIC_MAX_SHARING_DEVICES) {
		dev_warn(&pf->pdev->dev,
			 "limiting the VSI count due to UDP tunnel limitation %d > %d\n",
			 pf->num_alloc_vsi, UDP_TUNNEL_NIC_MAX_SHARING_DEVICES);
		pf->num_alloc_vsi = UDP_TUNNEL_NIC_MAX_SHARING_DEVICES;
	}

	/* Set up the *vsi struct and our local tracking of the MAIN PF vsi. */
	pf->vsi = kcalloc(pf->num_alloc_vsi, sizeof(struct i40e_vsi *),
			  GFP_KERNEL);
	if (!pf->vsi) {
		err = -ENOMEM;
		goto err_switch_setup;
	}

#ifdef CONFIG_PCI_IOV
	/* prep for VF support */
	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSIX_ENABLED) &&
	    !test_bit(__I40E_BAD_EEPROM, pf->state)) {
		if (pci_num_vf(pdev))
			pf->flags |= I40E_FLAG_VEB_MODE_ENABLED;
	}
#endif
	err = i40e_setup_pf_switch(pf, false, false);
	if (err) {
		dev_info(&pdev->dev, "setup_pf_switch failed: %d\n", err);
		goto err_vsis;
	}
	INIT_LIST_HEAD(&pf->vsi[pf->lan_vsi]->ch_list);

	/* if FDIR VSI was set up, start it now */
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR) {
			i40e_vsi_open(pf->vsi[i]);
			break;
		}
	}

	/* The driver only wants link up/down and module qualification
	 * reports from firmware.  Note the negative logic.
	 */
	err = i40e_aq_set_phy_int_mask(&pf->hw,
				       ~(I40E_AQ_EVENT_LINK_UPDOWN |
					 I40E_AQ_EVENT_MEDIA_NA |
					 I40E_AQ_EVENT_MODULE_QUAL_FAIL), NULL);
	if (err)
		dev_info(&pf->pdev->dev, "set phy mask fail, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, err),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

	/* Reconfigure hardware for allowing smaller MSS in the case
	 * of TSO, so that we avoid the MDD being fired and causing
	 * a reset in the case of small MSS+TSO.
	 */
	val = rd32(hw, I40E_REG_MSS);
	if ((val & I40E_REG_MSS_MIN_MASK) > I40E_64BYTE_MSS) {
		val &= ~I40E_REG_MSS_MIN_MASK;
		val |= I40E_64BYTE_MSS;
		wr32(hw, I40E_REG_MSS, val);
	}

	if (pf->hw_features & I40E_HW_RESTART_AUTONEG) {
		msleep(75);
		err = i40e_aq_set_link_restart_an(&pf->hw, true, NULL);
		if (err)
			dev_info(&pf->pdev->dev, "link restart failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, err),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
	}
	/* The main driver is (mostly) up and happy. We need to set this state
	 * before setting up the misc vector or we get a race and the vector
	 * ends up disabled forever.
	 */
	clear_bit(__I40E_DOWN, pf->state);

	/* In case of MSIX we are going to setup the misc vector right here
	 * to handle admin queue events etc. In case of legacy and MSI
	 * the misc functionality and queue processing is combined in
	 * the same vector and that gets setup at open.
	 */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		err = i40e_setup_misc_vector(pf);
		if (err) {
			dev_info(&pdev->dev,
				 "setup of misc vector failed: %d\n", err);
			i40e_cloud_filter_exit(pf);
			i40e_fdir_teardown(pf);
			goto err_vsis;
		}
	}

#ifdef CONFIG_PCI_IOV
	/* prep for VF support */
	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSIX_ENABLED) &&
	    !test_bit(__I40E_BAD_EEPROM, pf->state)) {
		/* disable link interrupts for VFs */
		val = rd32(hw, I40E_PFGEN_PORTMDIO_NUM);
		val &= ~I40E_PFGEN_PORTMDIO_NUM_VFLINK_STAT_ENA_MASK;
		wr32(hw, I40E_PFGEN_PORTMDIO_NUM, val);
		i40e_flush(hw);

		if (pci_num_vf(pdev)) {
			dev_info(&pdev->dev,
				 "Active VFs found, allocating resources.\n");
			err = i40e_alloc_vfs(pf, pci_num_vf(pdev));
			if (err)
				dev_info(&pdev->dev,
					 "Error %d allocating resources for existing VFs\n",
					 err);
		}
	}
#endif /* CONFIG_PCI_IOV */

	if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
		pf->iwarp_base_vector = i40e_get_lump(pf, pf->irq_pile,
						      pf->num_iwarp_msix,
						      I40E_IWARP_IRQ_PILE_ID);
		if (pf->iwarp_base_vector < 0) {
			dev_info(&pdev->dev,
				 "failed to get tracking for %d vectors for IWARP err=%d\n",
				 pf->num_iwarp_msix, pf->iwarp_base_vector);
			pf->flags &= ~I40E_FLAG_IWARP_ENABLED;
		}
	}

	i40e_dbg_pf_init(pf);

	/* tell the firmware that we're starting */
	i40e_send_version(pf);

	/* since everything's happy, start the service_task timer */
	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));

	/* add this PF to client device list and launch a client service task */
	if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
		err = i40e_lan_add_device(pf);
		if (err)
			dev_info(&pdev->dev, "Failed to add PF to client API service list: %d\n",
				 err);
	}

#define PCI_SPEED_SIZE 8
#define PCI_WIDTH_SIZE 8
	/* Devices on the IOSF bus do not have this information
	 * and will report PCI Gen 1 x 1 by default so don't bother
	 * checking them.
	 */
	if (!(pf->hw_features & I40E_HW_NO_PCI_LINK_CHECK)) {
		char speed[PCI_SPEED_SIZE] = "Unknown";
		char width[PCI_WIDTH_SIZE] = "Unknown";

		/* Get the negotiated link width and speed from PCI config
		 * space
		 */
		pcie_capability_read_word(pf->pdev, PCI_EXP_LNKSTA,
					  &link_status);

		i40e_set_pci_config_data(hw, link_status);

		switch (hw->bus.speed) {
		case i40e_bus_speed_8000:
			strlcpy(speed, "8.0", PCI_SPEED_SIZE); break;
		case i40e_bus_speed_5000:
			strlcpy(speed, "5.0", PCI_SPEED_SIZE); break;
		case i40e_bus_speed_2500:
			strlcpy(speed, "2.5", PCI_SPEED_SIZE); break;
		default:
			break;
		}
		switch (hw->bus.width) {
		case i40e_bus_width_pcie_x8:
			strlcpy(width, "8", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x4:
			strlcpy(width, "4", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x2:
			strlcpy(width, "2", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x1:
			strlcpy(width, "1", PCI_WIDTH_SIZE); break;
		default:
			break;
		}

		dev_info(&pdev->dev, "PCI-Express: Speed %sGT/s Width x%s\n",
			 speed, width);

		if (hw->bus.width < i40e_bus_width_pcie_x8 ||
		    hw->bus.speed < i40e_bus_speed_8000) {
			dev_warn(&pdev->dev, "PCI-Express bandwidth available for this device may be insufficient for optimal performance.\n");
			dev_warn(&pdev->dev, "Please move the device to a different PCI-e link with more lanes and/or higher transfer rate.\n");
		}
	}

	/* get the requested speeds from the fw */
	err = i40e_aq_get_phy_capabilities(hw, false, false, &abilities, NULL);
	if (err)
		dev_dbg(&pf->pdev->dev, "get requested speeds ret =  %s last_status =  %s\n",
			i40e_stat_str(&pf->hw, err),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	pf->hw.phy.link_info.requested_speeds = abilities.link_speed;

	/* set the FEC config due to the board capabilities */
	i40e_set_fec_in_flags(abilities.fec_cfg_curr_mod_ext_info, &pf->flags);

	/* get the supported phy types from the fw */
	err = i40e_aq_get_phy_capabilities(hw, false, true, &abilities, NULL);
	if (err)
		dev_dbg(&pf->pdev->dev, "get supported phy types ret =  %s last_status =  %s\n",
			i40e_stat_str(&pf->hw, err),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

	/* make sure the MFS hasn't been set lower than the default */
#define MAX_FRAME_SIZE_DEFAULT 0x2600
	val = (rd32(&pf->hw, I40E_PRTGL_SAH) &
	       I40E_PRTGL_SAH_MFS_MASK) >> I40E_PRTGL_SAH_MFS_SHIFT;
	if (val < MAX_FRAME_SIZE_DEFAULT)
		dev_warn(&pdev->dev, "MFS for port %x has been set below the default: %x\n",
			 i, val);

	/* Add a filter to drop all Flow control frames from any VSI from being
	 * transmitted. By doing so we stop a malicious VF from sending out
	 * PAUSE or PFC frames and potentially controlling traffic for other
	 * PF/VF VSIs.
	 * The FW can still send Flow control frames if enabled.
	 */
	i40e_add_filter_to_drop_tx_flow_control_frames(&pf->hw,
						       pf->main_vsi_seid);

	if ((pf->hw.device_id == I40E_DEV_ID_10G_BASE_T) ||
		(pf->hw.device_id == I40E_DEV_ID_10G_BASE_T4))
		pf->hw_features |= I40E_HW_PHY_CONTROLS_LEDS;
	if (pf->hw.device_id == I40E_DEV_ID_SFP_I_X722)
		pf->hw_features |= I40E_HW_HAVE_CRT_RETIMER;
	/* print a string summarizing features */
	i40e_print_features(pf);

	return 0;

	/* Unwind what we've done if something failed in the setup */
err_vsis:
	set_bit(__I40E_DOWN, pf->state);
	i40e_clear_interrupt_scheme(pf);
	kfree(pf->vsi);
err_switch_setup:
	i40e_reset_interrupt_capability(pf);
	del_timer_sync(&pf->service_timer);
err_mac_addr:
err_configure_lan_hmc:
	(void)i40e_shutdown_lan_hmc(hw);
err_init_lan_hmc:
	kfree(pf->qp_pile);
err_sw_init:
err_adminq_setup:
err_pf_reset:
	iounmap(hw->hw_addr);
err_ioremap:
	kfree(pf);
err_pf_alloc:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_mem_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * i40e_remove - Device removal routine
 * @pdev: PCI device information struct
 *
 * i40e_remove is called by the PCI subsystem to alert the driver
 * that is should release a PCI device.  This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void i40e_remove(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret_code;
	int i;

	i40e_dbg_pf_exit(pf);

	i40e_ptp_stop(pf);

	/* Disable RSS in hw */
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(0), 0);
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(1), 0);

	while (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state))
		usleep_range(1000, 2000);

	if (pf->flags & I40E_FLAG_SRIOV_ENABLED) {
		set_bit(__I40E_VF_RESETS_DISABLED, pf->state);
		i40e_free_vfs(pf);
		pf->flags &= ~I40E_FLAG_SRIOV_ENABLED;
	}
	/* no more scheduling of any task */
	set_bit(__I40E_SUSPENDED, pf->state);
	set_bit(__I40E_DOWN, pf->state);
	if (pf->service_timer.function)
		del_timer_sync(&pf->service_timer);
	if (pf->service_task.func)
		cancel_work_sync(&pf->service_task);

	if (test_bit(__I40E_RECOVERY_MODE, pf->state)) {
		struct i40e_vsi *vsi = pf->vsi[0];

		/* We know that we have allocated only one vsi for this PF,
		 * it was just for registering netdevice, so the interface
		 * could be visible in the 'ifconfig' output
		 */
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);

		goto unmap;
	}

	/* Client close must be called explicitly here because the timer
	 * has been stopped.
	 */
	i40e_notify_client_of_netdev_close(pf->vsi[pf->lan_vsi], false);

	i40e_fdir_teardown(pf);

	/* If there is a switch structure or any orphans, remove them.
	 * This will leave only the PF's VSI remaining.
	 */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;

		if (pf->veb[i]->uplink_seid == pf->mac_seid ||
		    pf->veb[i]->uplink_seid == 0)
			i40e_switch_branch_release(pf->veb[i]);
	}

	/* Now we can shutdown the PF's VSI, just before we kill
	 * adminq and hmc.
	 */
	if (pf->vsi[pf->lan_vsi])
		i40e_vsi_release(pf->vsi[pf->lan_vsi]);

	i40e_cloud_filter_exit(pf);

	/* remove attached clients */
	if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
		ret_code = i40e_lan_del_device(pf);
		if (ret_code)
			dev_warn(&pdev->dev, "Failed to delete client device: %d\n",
				 ret_code);
	}

	/* shutdown and destroy the HMC */
	if (hw->hmc.hmc_obj) {
		ret_code = i40e_shutdown_lan_hmc(hw);
		if (ret_code)
			dev_warn(&pdev->dev,
				 "Failed to destroy the HMC resources: %d\n",
				 ret_code);
	}

unmap:
	/* Free MSI/legacy interrupt 0 when in recovery mode. */
	if (test_bit(__I40E_RECOVERY_MODE, pf->state) &&
	    !(pf->flags & I40E_FLAG_MSIX_ENABLED))
		free_irq(pf->pdev->irq, pf);

	/* shutdown the adminq */
	i40e_shutdown_adminq(hw);

	/* destroy the locks only once, here */
	mutex_destroy(&hw->aq.arq_mutex);
	mutex_destroy(&hw->aq.asq_mutex);

	/* Clear all dynamic memory lists of rings, q_vectors, and VSIs */
	rtnl_lock();
	i40e_clear_interrupt_scheme(pf);
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i]) {
			if (!test_bit(__I40E_RECOVERY_MODE, pf->state))
				i40e_vsi_clear_rings(pf->vsi[i]);
			i40e_vsi_clear(pf->vsi[i]);
			pf->vsi[i] = NULL;
		}
	}
	rtnl_unlock();

	for (i = 0; i < I40E_MAX_VEB; i++) {
		kfree(pf->veb[i]);
		pf->veb[i] = NULL;
	}

	kfree(pf->qp_pile);
	kfree(pf->vsi);

	iounmap(hw->hw_addr);
	kfree(pf);
	pci_release_mem_regions(pdev);

	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

/**
 * i40e_pci_error_detected - warning that something funky happened in PCI land
 * @pdev: PCI device information struct
 * @error: the type of PCI error
 *
 * Called to warn that something happened and the error handling steps
 * are in progress.  Allows the driver to quiesce things, be ready for
 * remediation.
 **/
static pci_ers_result_t i40e_pci_error_detected(struct pci_dev *pdev,
						pci_channel_state_t error)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s: error %d\n", __func__, error);

	if (!pf) {
		dev_info(&pdev->dev,
			 "Cannot recover - error happened during device probe\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/* shutdown all operations */
	if (!test_bit(__I40E_SUSPENDED, pf->state))
		i40e_prep_for_reset(pf, false);

	/* Request a slot reset */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * i40e_pci_error_slot_reset - a PCI slot reset just happened
 * @pdev: PCI device information struct
 *
 * Called to find if the driver can work with the device now that
 * the pci slot has been reset.  If a basic connection seems good
 * (registers are readable and have sane content) then return a
 * happy little PCI_ERS_RESULT_xxx.
 **/
static pci_ers_result_t i40e_pci_error_slot_reset(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	pci_ers_result_t result;
	u32 reg;

	dev_dbg(&pdev->dev, "%s\n", __func__);
	if (pci_enable_device_mem(pdev)) {
		dev_info(&pdev->dev,
			 "Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);
		pci_wake_from_d3(pdev, false);

		reg = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		if (reg == 0)
			result = PCI_ERS_RESULT_RECOVERED;
		else
			result = PCI_ERS_RESULT_DISCONNECT;
	}

	return result;
}

/**
 * i40e_pci_error_reset_prepare - prepare device driver for pci reset
 * @pdev: PCI device information struct
 */
static void i40e_pci_error_reset_prepare(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	i40e_prep_for_reset(pf, false);
}

/**
 * i40e_pci_error_reset_done - pci reset done, device driver reset can begin
 * @pdev: PCI device information struct
 */
static void i40e_pci_error_reset_done(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	i40e_reset_and_rebuild(pf, false, false);
}

/**
 * i40e_pci_error_resume - restart operations after PCI error recovery
 * @pdev: PCI device information struct
 *
 * Called to allow the driver to bring things back up after PCI error
 * and/or reset recovery has finished.
 **/
static void i40e_pci_error_resume(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);
	if (test_bit(__I40E_SUSPENDED, pf->state))
		return;

	i40e_handle_reset_warning(pf, false);
}

/**
 * i40e_enable_mc_magic_wake - enable multicast magic packet wake up
 * using the mac_address_write admin q function
 * @pf: pointer to i40e_pf struct
 **/
static void i40e_enable_mc_magic_wake(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;
	u8 mac_addr[6];
	u16 flags = 0;

	/* Get current MAC address in case it's an LAA */
	if (pf->vsi[pf->lan_vsi] && pf->vsi[pf->lan_vsi]->netdev) {
		ether_addr_copy(mac_addr,
				pf->vsi[pf->lan_vsi]->netdev->dev_addr);
	} else {
		dev_err(&pf->pdev->dev,
			"Failed to retrieve MAC address; using default\n");
		ether_addr_copy(mac_addr, hw->mac.addr);
	}

	/* The FW expects the mac address write cmd to first be called with
	 * one of these flags before calling it again with the multicast
	 * enable flags.
	 */
	flags = I40E_AQC_WRITE_TYPE_LAA_WOL;

	if (hw->func_caps.flex10_enable && hw->partition_id != 1)
		flags = I40E_AQC_WRITE_TYPE_LAA_ONLY;

	ret = i40e_aq_mac_address_write(hw, flags, mac_addr, NULL);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"Failed to update MAC address registers; cannot enable Multicast Magic packet wake up");
		return;
	}

	flags = I40E_AQC_MC_MAG_EN
			| I40E_AQC_WOL_PRESERVE_ON_PFR
			| I40E_AQC_WRITE_TYPE_UPDATE_MC_MAG;
	ret = i40e_aq_mac_address_write(hw, flags, mac_addr, NULL);
	if (ret)
		dev_err(&pf->pdev->dev,
			"Failed to enable Multicast Magic Packet wake up\n");
}

/**
 * i40e_shutdown - PCI callback for shutting down
 * @pdev: PCI device information struct
 **/
static void i40e_shutdown(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	struct i40e_hw *hw = &pf->hw;

	set_bit(__I40E_SUSPENDED, pf->state);
	set_bit(__I40E_DOWN, pf->state);

	del_timer_sync(&pf->service_timer);
	cancel_work_sync(&pf->service_task);
	i40e_cloud_filter_exit(pf);
	i40e_fdir_teardown(pf);

	/* Client close must be called explicitly here because the timer
	 * has been stopped.
	 */
	i40e_notify_client_of_netdev_close(pf->vsi[pf->lan_vsi], false);

	if (pf->wol_en && (pf->hw_features & I40E_HW_WOL_MC_MAGIC_PKT_WAKE))
		i40e_enable_mc_magic_wake(pf);

	i40e_prep_for_reset(pf, false);

	wr32(hw, I40E_PFPM_APM,
	     (pf->wol_en ? I40E_PFPM_APM_APME_MASK : 0));
	wr32(hw, I40E_PFPM_WUFC,
	     (pf->wol_en ? I40E_PFPM_WUFC_MAG_MASK : 0));

	/* Free MSI/legacy interrupt 0 when in recovery mode. */
	if (test_bit(__I40E_RECOVERY_MODE, pf->state) &&
	    !(pf->flags & I40E_FLAG_MSIX_ENABLED))
		free_irq(pf->pdev->irq, pf);

	/* Since we're going to destroy queues during the
	 * i40e_clear_interrupt_scheme() we should hold the RTNL lock for this
	 * whole section
	 */
	rtnl_lock();
	i40e_clear_interrupt_scheme(pf);
	rtnl_unlock();

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, pf->wol_en);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * i40e_suspend - PM callback for moving to D3
 * @dev: generic device information structure
 **/
static int __maybe_unused i40e_suspend(struct device *dev)
{
	struct i40e_pf *pf = dev_get_drvdata(dev);
	struct i40e_hw *hw = &pf->hw;

	/* If we're already suspended, then there is nothing to do */
	if (test_and_set_bit(__I40E_SUSPENDED, pf->state))
		return 0;

	set_bit(__I40E_DOWN, pf->state);

	/* Ensure service task will not be running */
	del_timer_sync(&pf->service_timer);
	cancel_work_sync(&pf->service_task);

	/* Client close must be called explicitly here because the timer
	 * has been stopped.
	 */
	i40e_notify_client_of_netdev_close(pf->vsi[pf->lan_vsi], false);

	if (pf->wol_en && (pf->hw_features & I40E_HW_WOL_MC_MAGIC_PKT_WAKE))
		i40e_enable_mc_magic_wake(pf);

	/* Since we're going to destroy queues during the
	 * i40e_clear_interrupt_scheme() we should hold the RTNL lock for this
	 * whole section
	 */
	rtnl_lock();

	i40e_prep_for_reset(pf, true);

	wr32(hw, I40E_PFPM_APM, (pf->wol_en ? I40E_PFPM_APM_APME_MASK : 0));
	wr32(hw, I40E_PFPM_WUFC, (pf->wol_en ? I40E_PFPM_WUFC_MAG_MASK : 0));

	/* Clear the interrupt scheme and release our IRQs so that the system
	 * can safely hibernate even when there are a large number of CPUs.
	 * Otherwise hibernation might fail when mapping all the vectors back
	 * to CPU0.
	 */
	i40e_clear_interrupt_scheme(pf);

	rtnl_unlock();

	return 0;
}

/**
 * i40e_resume - PM callback for waking up from D3
 * @dev: generic device information structure
 **/
static int __maybe_unused i40e_resume(struct device *dev)
{
	struct i40e_pf *pf = dev_get_drvdata(dev);
	int err;

	/* If we're not suspended, then there is nothing to do */
	if (!test_bit(__I40E_SUSPENDED, pf->state))
		return 0;

	/* We need to hold the RTNL lock prior to restoring interrupt schemes,
	 * since we're going to be restoring queues
	 */
	rtnl_lock();

	/* We cleared the interrupt scheme when we suspended, so we need to
	 * restore it now to resume device functionality.
	 */
	err = i40e_restore_interrupt_scheme(pf);
	if (err) {
		dev_err(dev, "Cannot restore interrupt scheme: %d\n",
			err);
	}

	clear_bit(__I40E_DOWN, pf->state);
	i40e_reset_and_rebuild(pf, false, true);

	rtnl_unlock();

	/* Clear suspended state last after everything is recovered */
	clear_bit(__I40E_SUSPENDED, pf->state);

	/* Restart the service task */
	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));

	return 0;
}

static const struct pci_error_handlers i40e_err_handler = {
	.error_detected = i40e_pci_error_detected,
	.slot_reset = i40e_pci_error_slot_reset,
	.reset_prepare = i40e_pci_error_reset_prepare,
	.reset_done = i40e_pci_error_reset_done,
	.resume = i40e_pci_error_resume,
};

static SIMPLE_DEV_PM_OPS(i40e_pm_ops, i40e_suspend, i40e_resume);

static struct pci_driver i40e_driver = {
	.name     = i40e_driver_name,
	.id_table = i40e_pci_tbl,
	.probe    = i40e_probe,
	.remove   = i40e_remove,
	.driver   = {
		.pm = &i40e_pm_ops,
	},
	.shutdown = i40e_shutdown,
	.err_handler = &i40e_err_handler,
	.sriov_configure = i40e_pci_sriov_configure,
};

/**
 * i40e_init_module - Driver registration routine
 *
 * i40e_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init i40e_init_module(void)
{
	pr_info("%s: %s\n", i40e_driver_name, i40e_driver_string);
	pr_info("%s: %s\n", i40e_driver_name, i40e_copyright);

	/* There is no need to throttle the number of active tasks because
	 * each device limits its own task using a state bit for scheduling
	 * the service task, and the device tasks do not interfere with each
	 * other, so we don't set a max task limit. We must set WQ_MEM_RECLAIM
	 * since we need to be able to guarantee forward progress even under
	 * memory pressure.
	 */
	i40e_wq = alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, i40e_driver_name);
	if (!i40e_wq) {
		pr_err("%s: Failed to create workqueue\n", i40e_driver_name);
		return -ENOMEM;
	}

	i40e_dbg_init();
	return pci_register_driver(&i40e_driver);
}
module_init(i40e_init_module);

/**
 * i40e_exit_module - Driver exit cleanup routine
 *
 * i40e_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit i40e_exit_module(void)
{
	pci_unregister_driver(&i40e_driver);
	destroy_workqueue(i40e_wq);
	i40e_dbg_exit();
}
module_exit(i40e_exit_module);
