/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include <linux/etherdevice.h>
#include <linux/of_net.h>
#include <linux/pci.h>

/* Local includes */
#include "i40e.h"
#include "i40e_diag.h"
#include <net/udp_tunnel.h>

const char i40e_driver_name[] = "i40e";
static const char i40e_driver_string[] =
			"Intel(R) Ethernet Connection XL710 Network Driver";

#define DRV_KERN "-k"

#define DRV_VERSION_MAJOR 1
#define DRV_VERSION_MINOR 6
#define DRV_VERSION_BUILD 12
#define DRV_VERSION __stringify(DRV_VERSION_MAJOR) "." \
	     __stringify(DRV_VERSION_MINOR) "." \
	     __stringify(DRV_VERSION_BUILD)    DRV_KERN
const char i40e_driver_version_str[] = DRV_VERSION;
static const char i40e_copyright[] = "Copyright (c) 2013 - 2014 Intel Corporation.";

/* a bit of forward declarations */
static void i40e_vsi_reinit_locked(struct i40e_vsi *vsi);
static void i40e_handle_reset_warning(struct i40e_pf *pf);
static int i40e_add_vsi(struct i40e_vsi *vsi);
static int i40e_add_veb(struct i40e_veb *veb, struct i40e_vsi *vsi);
static int i40e_setup_pf_switch(struct i40e_pf *pf, bool reinit);
static int i40e_setup_misc_vector(struct i40e_pf *pf);
static void i40e_determine_queue_usage(struct i40e_pf *pf);
static int i40e_setup_pf_filter_control(struct i40e_pf *pf);
static void i40e_fill_rss_lut(struct i40e_pf *pf, u8 *lut,
			      u16 rss_table_size, u16 rss_size);
static void i40e_fdir_sb_setup(struct i40e_pf *pf);
static int i40e_veb_get_bw_info(struct i40e_veb *veb);

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
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_20G_KR2), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_KX_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_QSFP_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_SFP_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_1G_BASE_T_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_10G_BASE_T_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_SFP_I_X722), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_20G_KR2), 0},
	{PCI_VDEVICE(INTEL, I40E_DEV_ID_20G_KR2_A), 0},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, i40e_pci_tbl);

#define I40E_MAX_VF_COUNT 128
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

MODULE_AUTHOR("Intel Corporation, <e1000-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Ethernet Connection XL710 Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static struct workqueue_struct *i40e_wq;

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
	mem->va = dma_zalloc_coherent(&pf->pdev->dev, mem->size,
				      &mem->pa, GFP_KERNEL);
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
 *
 * The search_hint trick and lack of advanced fit-finding only work
 * because we're highly likely to have all the same size lump requests.
 * Linear search time and any fragmentation should be minimal.
 **/
static int i40e_get_lump(struct i40e_pf *pf, struct i40e_lump_tracking *pile,
			 u16 needed, u16 id)
{
	int ret = -ENOMEM;
	int i, j;

	if (!pile || needed == 0 || id >= I40E_PILE_VALID_BIT) {
		dev_info(&pf->pdev->dev,
			 "param err: pile=%p needed=%d id=0x%04x\n",
			 pile, needed, id);
		return -EINVAL;
	}

	/* start the linear search with an imperfect hint */
	i = pile->search_hint;
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
			pile->search_hint = i + j;
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
	int i;

	if (!pile || index >= pile->num_entries)
		return -EINVAL;

	for (i = index;
	     i < pile->num_entries && pile->list[i] == valid_id;
	     i++) {
		pile->list[i] = 0;
		count++;
	}

	if (count && index < pile->search_hint)
		pile->search_hint = index;

	return count;
}

/**
 * i40e_find_vsi_from_id - searches for the vsi with the given id
 * @pf - the pf structure to search for the vsi
 * @id - id of the vsi it is searching for
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
	if (!test_bit(__I40E_DOWN, &pf->state) &&
	    !test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state) &&
	    !test_and_set_bit(__I40E_SERVICE_SCHED, &pf->state))
		queue_work(i40e_wq, &pf->service_task);
}

/**
 * i40e_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 *
 * If any port has noticed a Tx timeout, it is likely that the whole
 * device is munged, not just the one netdev port, so go for the full
 * reset.
 **/
#ifdef I40E_FCOE
void i40e_tx_timeout(struct net_device *netdev)
#else
static void i40e_tx_timeout(struct net_device *netdev)
#endif
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_ring *tx_ring = NULL;
	unsigned int i, hung_queue = 0;
	u32 head, val;

	pf->tx_timeout_count++;

	/* find the stopped queue the same way the stack does */
	for (i = 0; i < netdev->num_tx_queues; i++) {
		struct netdev_queue *q;
		unsigned long trans_start;

		q = netdev_get_tx_queue(netdev, i);
		trans_start = q->trans_start;
		if (netif_xmit_stopped(q) &&
		    time_after(jiffies,
			       (trans_start + netdev->watchdog_timeo))) {
			hung_queue = i;
			break;
		}
	}

	if (i == netdev->num_tx_queues) {
		netdev_info(netdev, "tx_timeout: no netdev hung queue found\n");
	} else {
		/* now that we have an index, find the tx_ring struct */
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc) {
				if (hung_queue ==
				    vsi->tx_rings[i]->queue_index) {
					tx_ring = vsi->tx_rings[i];
					break;
				}
			}
		}
	}

	if (time_after(jiffies, (pf->tx_timeout_last_recovery + HZ*20)))
		pf->tx_timeout_recovery_level = 1;  /* reset after some time */
	else if (time_before(jiffies,
		      (pf->tx_timeout_last_recovery + netdev->watchdog_timeo)))
		return;   /* don't do any new action before the next timeout */

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
			    vsi->seid, hung_queue, tx_ring->next_to_clean,
			    head, tx_ring->next_to_use,
			    readl(tx_ring->tail), val);
	}

	pf->tx_timeout_last_recovery = jiffies;
	netdev_info(netdev, "tx_timeout recovery level %d, hung_queue %d\n",
		    pf->tx_timeout_recovery_level, hung_queue);

	switch (pf->tx_timeout_recovery_level) {
	case 1:
		set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
		break;
	case 2:
		set_bit(__I40E_CORE_RESET_REQUESTED, &pf->state);
		break;
	case 3:
		set_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state);
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
 * i40e_get_netdev_stats_struct - Get statistics for netdev interface
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the service task.
 **/
#ifdef I40E_FCOE
struct rtnl_link_stats64 *i40e_get_netdev_stats_struct(
					     struct net_device *netdev,
					     struct rtnl_link_stats64 *stats)
#else
static struct rtnl_link_stats64 *i40e_get_netdev_stats_struct(
					     struct net_device *netdev,
					     struct rtnl_link_stats64 *stats)
#endif
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_ring *tx_ring, *rx_ring;
	struct i40e_vsi *vsi = np->vsi;
	struct rtnl_link_stats64 *vsi_stats = i40e_get_vsi_stats_struct(vsi);
	int i;

	if (test_bit(__I40E_DOWN, &vsi->state))
		return stats;

	if (!vsi->tx_rings)
		return stats;

	rcu_read_lock();
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		u64 bytes, packets;
		unsigned int start;

		tx_ring = ACCESS_ONCE(vsi->tx_rings[i]);
		if (!tx_ring)
			continue;

		do {
			start = u64_stats_fetch_begin_irq(&tx_ring->syncp);
			packets = tx_ring->stats.packets;
			bytes   = tx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&tx_ring->syncp, start));

		stats->tx_packets += packets;
		stats->tx_bytes   += bytes;
		rx_ring = &tx_ring[1];

		do {
			start = u64_stats_fetch_begin_irq(&rx_ring->syncp);
			packets = rx_ring->stats.packets;
			bytes   = rx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&rx_ring->syncp, start));

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

	return stats;
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
	i40e_stat_update32(hw, I40E_GLV_TEPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_errors, &es->tx_errors);

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
static void i40e_update_veb_stats(struct i40e_veb *veb)
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

#ifdef I40E_FCOE
/**
 * i40e_update_fcoe_stats - Update FCoE-specific ethernet statistics counters.
 * @vsi: the VSI that is capable of doing FCoE
 **/
static void i40e_update_fcoe_stats(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_fcoe_stats *ofs;
	struct i40e_fcoe_stats *fs;     /* device's eth stats */
	int idx;

	if (vsi->type != I40E_VSI_FCOE)
		return;

	idx = hw->pf_id + I40E_FCOE_PF_STAT_OFFSET;
	fs = &vsi->fcoe_stats;
	ofs = &vsi->fcoe_stats_offsets;

	i40e_stat_update32(hw, I40E_GL_FCOEPRC(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->rx_fcoe_packets, &fs->rx_fcoe_packets);
	i40e_stat_update48(hw, I40E_GL_FCOEDWRCH(idx), I40E_GL_FCOEDWRCL(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->rx_fcoe_dwords, &fs->rx_fcoe_dwords);
	i40e_stat_update32(hw, I40E_GL_FCOERPDC(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->rx_fcoe_dropped, &fs->rx_fcoe_dropped);
	i40e_stat_update32(hw, I40E_GL_FCOEPTC(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->tx_fcoe_packets, &fs->tx_fcoe_packets);
	i40e_stat_update48(hw, I40E_GL_FCOEDWTCH(idx), I40E_GL_FCOEDWTCL(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->tx_fcoe_dwords, &fs->tx_fcoe_dwords);
	i40e_stat_update32(hw, I40E_GL_FCOECRC(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->fcoe_bad_fccrc, &fs->fcoe_bad_fccrc);
	i40e_stat_update32(hw, I40E_GL_FCOELAST(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->fcoe_last_error, &fs->fcoe_last_error);
	i40e_stat_update32(hw, I40E_GL_FCOEDDPC(idx),
			   vsi->fcoe_stat_offsets_loaded,
			   &ofs->fcoe_ddp_count, &fs->fcoe_ddp_count);

	vsi->fcoe_stat_offsets_loaded = true;
}

#endif
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
	u64 tx_lost_interrupt;
	struct i40e_ring *p;
	u32 rx_page, rx_buf;
	u64 bytes, packets;
	unsigned int start;
	u64 tx_linearize;
	u64 tx_force_wb;
	u64 rx_p, rx_b;
	u64 tx_p, tx_b;
	u16 q;

	if (test_bit(__I40E_DOWN, &vsi->state) ||
	    test_bit(__I40E_CONFIG_BUSY, &pf->state))
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
	tx_lost_interrupt = 0;
	rx_page = 0;
	rx_buf = 0;
	rcu_read_lock();
	for (q = 0; q < vsi->num_queue_pairs; q++) {
		/* locate Tx ring */
		p = ACCESS_ONCE(vsi->tx_rings[q]);

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
		tx_lost_interrupt += p->tx_stats.tx_lost_interrupt;

		/* Rx queue is part of the same block as Tx queue */
		p = &p[1];
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			packets = p->stats.packets;
			bytes = p->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));
		rx_b += bytes;
		rx_p += packets;
		rx_buf += p->rx_stats.alloc_buff_failed;
		rx_page += p->rx_stats.alloc_page_failed;
	}
	rcu_read_unlock();
	vsi->tx_restart = tx_restart;
	vsi->tx_busy = tx_busy;
	vsi->tx_linearize = tx_linearize;
	vsi->tx_force_wb = tx_force_wb;
	vsi->tx_lost_interrupt = tx_lost_interrupt;
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
	i40e_stat_update32(hw,
			   I40E_GLQF_PCNT(I40E_FD_ATR_STAT_IDX(pf->hw.pf_id)),
			   pf->stat_offsets_loaded,
			   &osd->fd_atr_match, &nsd->fd_atr_match);
	i40e_stat_update32(hw,
			   I40E_GLQF_PCNT(I40E_FD_SB_STAT_IDX(pf->hw.pf_id)),
			   pf->stat_offsets_loaded,
			   &osd->fd_sb_match, &nsd->fd_sb_match);
	i40e_stat_update32(hw,
		      I40E_GLQF_PCNT(I40E_FD_ATR_TUNNEL_STAT_IDX(pf->hw.pf_id)),
		      pf->stat_offsets_loaded,
		      &osd->fd_atr_tunnel_match, &nsd->fd_atr_tunnel_match);

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
	    !(pf->auto_disable_flags & I40E_FLAG_FD_SB_ENABLED))
		nsd->fd_sb_status = true;
	else
		nsd->fd_sb_status = false;

	if (pf->flags & I40E_FLAG_FD_ATR_ENABLED &&
	    !(pf->auto_disable_flags & I40E_FLAG_FD_ATR_ENABLED))
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
#ifdef I40E_FCOE
	i40e_update_fcoe_stats(vsi);
#endif
}

/**
 * i40e_find_filter - Search VSI filter list for specific mac/vlan filter
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure its a VF filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns ptr to the filter object or NULL
 **/
static struct i40e_mac_filter *i40e_find_filter(struct i40e_vsi *vsi,
						u8 *macaddr, s16 vlan,
						bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return NULL;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (vlan == f->vlan)    &&
		    (!is_vf || f->is_vf) &&
		    (!is_netdev || f->is_netdev))
			return f;
	}
	return NULL;
}

/**
 * i40e_find_mac - Find a mac addr in the macvlan filters list
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address we are searching for
 * @is_vf: make sure its a VF filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns the first filter with the provided MAC address or NULL if
 * MAC address was not found
 **/
struct i40e_mac_filter *i40e_find_mac(struct i40e_vsi *vsi, u8 *macaddr,
				      bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return NULL;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (!is_vf || f->is_vf) &&
		    (!is_netdev || f->is_netdev))
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
	struct i40e_mac_filter *f;

	/* Only -1 for all the filters denotes not in vlan mode
	 * so we have to go through all the list in order to make sure
	 */
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (f->vlan >= 0 || vsi->info.pvid)
			return true;
	}

	return false;
}

/**
 * i40e_put_mac_in_vlan - Make macvlan filters from macaddrs and vlans
 * @vsi: the VSI to be searched
 * @macaddr: the mac address to be filtered
 * @is_vf: true if it is a VF
 * @is_netdev: true if it is a netdev
 *
 * Goes through all the macvlan filters and adds a
 * macvlan filter for each unique vlan that already exists
 *
 * Returns first filter found on success, else NULL
 **/
struct i40e_mac_filter *i40e_put_mac_in_vlan(struct i40e_vsi *vsi, u8 *macaddr,
					     bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (vsi->info.pvid)
			f->vlan = le16_to_cpu(vsi->info.pvid);
		if (!i40e_find_filter(vsi, macaddr, f->vlan,
				      is_vf, is_netdev)) {
			if (!i40e_add_filter(vsi, macaddr, f->vlan,
					     is_vf, is_netdev))
				return NULL;
		}
	}

	return list_first_entry_or_null(&vsi->mac_filter_list,
					struct i40e_mac_filter, list);
}

/**
 * i40e_del_mac_all_vlan - Remove a MAC filter from all VLANS
 * @vsi: the VSI to be searched
 * @macaddr: the mac address to be removed
 * @is_vf: true if it is a VF
 * @is_netdev: true if it is a netdev
 *
 * Removes a given MAC address from a VSI, regardless of VLAN
 *
 * Returns 0 for success, or error
 **/
int i40e_del_mac_all_vlan(struct i40e_vsi *vsi, u8 *macaddr,
			  bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f = NULL;
	int changed = 0;

	WARN(!spin_is_locked(&vsi->mac_filter_list_lock),
	     "Missing mac_filter_list_lock\n");
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (is_vf == f->is_vf) &&
		    (is_netdev == f->is_netdev)) {
			f->counter--;
			changed = 1;
			if (f->counter == 0)
				f->state = I40E_FILTER_REMOVE;
		}
	}
	if (changed) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
		return 0;
	}
	return -ENOENT;
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
			I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
	i40e_aq_remove_macvlan(&pf->hw, vsi->seid, &element, 1, NULL);
}

/**
 * i40e_add_filter - Add a mac/vlan filter to the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure its a VF filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns ptr to the filter object or NULL when no memory available.
 *
 * NOTE: This function is expected to be called with mac_filter_list_lock
 * being held.
 **/
struct i40e_mac_filter *i40e_add_filter(struct i40e_vsi *vsi,
					u8 *macaddr, s16 vlan,
					bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;
	int changed = false;

	if (!vsi || !macaddr)
		return NULL;

	/* Do not allow broadcast filter to be added since broadcast filter
	 * is added as part of add VSI for any newly created VSI except
	 * FDIR VSI
	 */
	if (is_broadcast_ether_addr(macaddr))
		return NULL;

	f = i40e_find_filter(vsi, macaddr, vlan, is_vf, is_netdev);
	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);
		if (!f)
			goto add_filter_out;

		ether_addr_copy(f->macaddr, macaddr);
		f->vlan = vlan;
		/* If we're in overflow promisc mode, set the state directly
		 * to failed, so we don't bother to try sending the filter
		 * to the hardware.
		 */
		if (test_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state))
			f->state = I40E_FILTER_FAILED;
		else
			f->state = I40E_FILTER_NEW;
		changed = true;
		INIT_LIST_HEAD(&f->list);
		list_add_tail(&f->list, &vsi->mac_filter_list);
	}

	/* increment counter and add a new flag if needed */
	if (is_vf) {
		if (!f->is_vf) {
			f->is_vf = true;
			f->counter++;
		}
	} else if (is_netdev) {
		if (!f->is_netdev) {
			f->is_netdev = true;
			f->counter++;
		}
	} else {
		f->counter++;
	}

	if (changed) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
	}

add_filter_out:
	return f;
}

/**
 * i40e_del_filter - Remove a mac/vlan filter from the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure it's a VF filter, else doesn't matter
 * @is_netdev: make sure it's a netdev filter, else doesn't matter
 *
 * NOTE: This function is expected to be called with mac_filter_list_lock
 * being held.
 * ANOTHER NOTE: This function MUST be called from within the context of
 * the "safe" variants of any list iterators, e.g. list_for_each_entry_safe()
 * instead of list_for_each_entry().
 **/
void i40e_del_filter(struct i40e_vsi *vsi,
		     u8 *macaddr, s16 vlan,
		     bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return;

	f = i40e_find_filter(vsi, macaddr, vlan, is_vf, is_netdev);
	if (!f || f->counter == 0)
		return;

	if (is_vf) {
		if (f->is_vf) {
			f->is_vf = false;
			f->counter--;
		}
	} else if (is_netdev) {
		if (f->is_netdev) {
			f->is_netdev = false;
			f->counter--;
		}
	} else {
		/* make sure we don't remove a filter in use by VF or netdev */
		int min_f = 0;

		min_f += (f->is_vf ? 1 : 0);
		min_f += (f->is_netdev ? 1 : 0);

		if (f->counter > min_f)
			f->counter--;
	}

	/* counter == 0 tells sync_filters_subtask to
	 * remove the filter from the firmware's list
	 */
	if (f->counter == 0) {
		if ((f->state == I40E_FILTER_FAILED) ||
		    (f->state == I40E_FILTER_NEW)) {
			/* this one never got added by the FW. Just remove it,
			 * no need to sync anything.
			 */
			list_del(&f->list);
			kfree(f);
		} else {
			f->state = I40E_FILTER_REMOVE;
			vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
			vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
		}
	}
}

/**
 * i40e_set_mac - NDO callback to set mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
#ifdef I40E_FCOE
int i40e_set_mac(struct net_device *netdev, void *p)
#else
static int i40e_set_mac(struct net_device *netdev, void *p)
#endif
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

	if (test_bit(__I40E_DOWN, &vsi->back->state) ||
	    test_bit(__I40E_RESET_RECOVERY_PENDING, &vsi->back->state))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(hw->mac.addr, addr->sa_data))
		netdev_info(netdev, "returning to hw mac address %pM\n",
			    hw->mac.addr);
	else
		netdev_info(netdev, "set new mac address %pM\n", addr->sa_data);

	spin_lock_bh(&vsi->mac_filter_list_lock);
	i40e_del_mac_all_vlan(vsi, netdev->dev_addr, false, true);
	i40e_put_mac_in_vlan(vsi, addr->sa_data, false, true);
	spin_unlock_bh(&vsi->mac_filter_list_lock);
	ether_addr_copy(netdev->dev_addr, addr->sa_data);
	if (vsi->type == I40E_VSI_MAIN) {
		i40e_status ret;

		ret = i40e_aq_mac_address_write(&vsi->back->hw,
						I40E_AQC_WRITE_TYPE_LAA_WOL,
						addr->sa_data, NULL);
		if (ret)
			netdev_info(netdev, "Ignoring error from firmware on LAA update, status %s, AQ ret %s\n",
				    i40e_stat_str(hw, ret),
				    i40e_aq_str(hw, hw->aq.asq_last_status));
	}

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
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
#ifdef I40E_FCOE
void i40e_vsi_setup_queue_map(struct i40e_vsi *vsi,
			      struct i40e_vsi_context *ctxt,
			      u8 enabled_tc,
			      bool is_add)
#else
static void i40e_vsi_setup_queue_map(struct i40e_vsi *vsi,
				     struct i40e_vsi_context *ctxt,
				     u8 enabled_tc,
				     bool is_add)
#endif
{
	struct i40e_pf *pf = vsi->back;
	u16 sections = 0;
	u8 netdev_tc = 0;
	u16 numtc = 0;
	u16 qcount;
	u8 offset;
	u16 qmap;
	int i;
	u16 num_tc_qps = 0;

	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	offset = 0;

	if (enabled_tc && (vsi->back->flags & I40E_FLAG_DCB_ENABLED)) {
		/* Find numtc from enabled TC bitmap */
		for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
			if (enabled_tc & BIT(i)) /* TC is enabled */
				numtc++;
		}
		if (!numtc) {
			dev_warn(&pf->pdev->dev, "DCB is enabled but no TC enabled, forcing TC0\n");
			numtc = 1;
		}
	} else {
		/* At least TC0 is enabled in case of non-DCB case */
		numtc = 1;
	}

	vsi->tc_config.numtc = numtc;
	vsi->tc_config.enabled_tc = enabled_tc ? enabled_tc : 1;
	/* Number of queues per enabled TC */
	qcount = vsi->alloc_queue_pairs;

	num_tc_qps = qcount / numtc;
	num_tc_qps = min_t(int, num_tc_qps, i40e_pf_get_max_q_per_tc(pf));

	/* Setup queue offset/count for all TCs for given VSI */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* See if the given TC is enabled for the given VSI */
		if (vsi->tc_config.enabled_tc & BIT(i)) {
			/* TC is enabled */
			int pow, num_qps;

			switch (vsi->type) {
			case I40E_VSI_MAIN:
				qcount = min_t(int, pf->alloc_rss_size,
					       num_tc_qps);
				break;
#ifdef I40E_FCOE
			case I40E_VSI_FCOE:
				qcount = num_tc_qps;
				break;
#endif
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

	/* Set actual Tx/Rx queue pairs */
	vsi->num_queue_pairs = offset;
	if ((vsi->type == I40E_VSI_MAIN) && (numtc == 1)) {
		if (vsi->req_queue_pairs > 0)
			vsi->num_queue_pairs = vsi->req_queue_pairs;
		else if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			vsi->num_queue_pairs = pf->num_lan_msix;
	}

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
 * i40e_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 **/
#ifdef I40E_FCOE
void i40e_set_rx_mode(struct net_device *netdev)
#else
static void i40e_set_rx_mode(struct net_device *netdev)
#endif
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_vsi *vsi = np->vsi;
	struct netdev_hw_addr *uca;
	struct netdev_hw_addr *mca;
	struct netdev_hw_addr *ha;

	spin_lock_bh(&vsi->mac_filter_list_lock);

	/* add addr if not already in the filter list */
	netdev_for_each_uc_addr(uca, netdev) {
		if (!i40e_find_mac(vsi, uca->addr, false, true)) {
			if (i40e_is_vsi_in_vlan(vsi))
				i40e_put_mac_in_vlan(vsi, uca->addr,
						     false, true);
			else
				i40e_add_filter(vsi, uca->addr, I40E_VLAN_ANY,
						false, true);
		}
	}

	netdev_for_each_mc_addr(mca, netdev) {
		if (!i40e_find_mac(vsi, mca->addr, false, true)) {
			if (i40e_is_vsi_in_vlan(vsi))
				i40e_put_mac_in_vlan(vsi, mca->addr,
						     false, true);
			else
				i40e_add_filter(vsi, mca->addr, I40E_VLAN_ANY,
						false, true);
		}
	}

	/* remove filter if not in netdev list */
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {

		if (!f->is_netdev)
			continue;

		netdev_for_each_mc_addr(mca, netdev)
			if (ether_addr_equal(mca->addr, f->macaddr))
				goto bottom_of_search_loop;

		netdev_for_each_uc_addr(uca, netdev)
			if (ether_addr_equal(uca->addr, f->macaddr))
				goto bottom_of_search_loop;

		for_each_dev_addr(netdev, ha)
			if (ether_addr_equal(ha->addr, f->macaddr))
				goto bottom_of_search_loop;

		/* f->macaddr wasn't found in uc, mc, or ha list so delete it */
		i40e_del_filter(vsi, f->macaddr, I40E_VLAN_ANY, false, true);

bottom_of_search_loop:
		continue;
	}
	spin_unlock_bh(&vsi->mac_filter_list_lock);

	/* check for other flag changes */
	if (vsi->current_netdev_flags != vsi->netdev->flags) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
	}

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
}

/**
 * i40e_undo_del_filter_entries - Undo the changes made to MAC filter entries
 * @vsi: pointer to vsi struct
 * @from: Pointer to list which contains MAC filter entries - changes to
 *        those entries needs to be undone.
 *
 * MAC filter entries from list were slated to be removed from device.
 **/
static void i40e_undo_del_filter_entries(struct i40e_vsi *vsi,
					 struct list_head *from)
{
	struct i40e_mac_filter *f, *ftmp;

	list_for_each_entry_safe(f, ftmp, from, list) {
		/* Move the element back into MAC filter list*/
		list_move_tail(&f->list, &vsi->mac_filter_list);
	}
}

/**
 * i40e_update_filter_state - Update filter state based on return data
 * from firmware
 * @count: Number of filters added
 * @add_list: return data from fw
 * @head: pointer to first filter in current batch
 * @aq_err: status from fw
 *
 * MAC filter entries from list were slated to be added to device. Returns
 * number of successful filters. Note that 0 does NOT mean success!
 **/
static int
i40e_update_filter_state(int count,
			 struct i40e_aqc_add_macvlan_element_data *add_list,
			 struct i40e_mac_filter *add_head, int aq_err)
{
	int retval = 0;
	int i;


	if (!aq_err) {
		retval = count;
		/* Everything's good, mark all filters active. */
		for (i = 0; i < count ; i++) {
			add_head->state = I40E_FILTER_ACTIVE;
			add_head = list_next_entry(add_head, list);
		}
	} else if (aq_err == I40E_AQ_RC_ENOSPC) {
		/* Device ran out of filter space. Check the return value
		 * for each filter to see which ones are active.
		 */
		for (i = 0; i < count ; i++) {
			if (add_list[i].match_method ==
			    I40E_AQC_MM_ERR_NO_RES) {
				add_head->state = I40E_FILTER_FAILED;
			} else {
				add_head->state = I40E_FILTER_ACTIVE;
				retval++;
			}
			add_head = list_next_entry(add_head, list);
		}
	} else {
		/* Some other horrible thing happened, fail all filters */
		retval = 0;
		for (i = 0; i < count ; i++) {
			add_head->state = I40E_FILTER_FAILED;
			add_head = list_next_entry(add_head, list);
		}
	}
	return retval;
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
	struct i40e_mac_filter *f, *ftmp, *add_head = NULL;
	struct list_head tmp_add_list, tmp_del_list;
	struct i40e_hw *hw = &vsi->back->hw;
	bool promisc_changed = false;
	char vsi_name[16] = "PF";
	int filter_list_len = 0;
	u32 changed_flags = 0;
	i40e_status aq_ret = 0;
	int retval = 0;
	struct i40e_pf *pf;
	int num_add = 0;
	int num_del = 0;
	int aq_err = 0;
	u16 cmd_flags;
	int list_size;
	int fcnt;

	/* empty array typed pointers, kcalloc later */
	struct i40e_aqc_add_macvlan_element_data *add_list;
	struct i40e_aqc_remove_macvlan_element_data *del_list;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, &vsi->state))
		usleep_range(1000, 2000);
	pf = vsi->back;

	if (vsi->netdev) {
		changed_flags = vsi->current_netdev_flags ^ vsi->netdev->flags;
		vsi->current_netdev_flags = vsi->netdev->flags;
	}

	INIT_LIST_HEAD(&tmp_add_list);
	INIT_LIST_HEAD(&tmp_del_list);

	if (vsi->type == I40E_VSI_SRIOV)
		snprintf(vsi_name, sizeof(vsi_name) - 1, "VF %d", vsi->vf_id);
	else if (vsi->type != I40E_VSI_MAIN)
		snprintf(vsi_name, sizeof(vsi_name) - 1, "vsi %d", vsi->seid);

	if (vsi->flags & I40E_VSI_FLAG_FILTER_CHANGED) {
		vsi->flags &= ~I40E_VSI_FLAG_FILTER_CHANGED;

		spin_lock_bh(&vsi->mac_filter_list_lock);
		/* Create a list of filters to delete. */
		list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
			if (f->state == I40E_FILTER_REMOVE) {
				WARN_ON(f->counter != 0);
				/* Move the element into temporary del_list */
				list_move_tail(&f->list, &tmp_del_list);
				vsi->active_filters--;
			}
			if (f->state == I40E_FILTER_NEW) {
				WARN_ON(f->counter == 0);
				/* Move the element into temporary add_list */
				list_move_tail(&f->list, &tmp_add_list);
			}
		}
		spin_unlock_bh(&vsi->mac_filter_list_lock);
	}

	/* Now process 'del_list' outside the lock */
	if (!list_empty(&tmp_del_list)) {
		filter_list_len = hw->aq.asq_buf_size /
			    sizeof(struct i40e_aqc_remove_macvlan_element_data);
		list_size = filter_list_len *
			    sizeof(struct i40e_aqc_remove_macvlan_element_data);
		del_list = kzalloc(list_size, GFP_ATOMIC);
		if (!del_list) {
			/* Undo VSI's MAC filter entry element updates */
			spin_lock_bh(&vsi->mac_filter_list_lock);
			i40e_undo_del_filter_entries(vsi, &tmp_del_list);
			spin_unlock_bh(&vsi->mac_filter_list_lock);
			retval = -ENOMEM;
			goto out;
		}

		list_for_each_entry_safe(f, ftmp, &tmp_del_list, list) {
			cmd_flags = 0;

			/* add to delete list */
			ether_addr_copy(del_list[num_del].mac_addr, f->macaddr);
			if (f->vlan == I40E_VLAN_ANY) {
				del_list[num_del].vlan_tag = 0;
				cmd_flags |= I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
			} else {
				del_list[num_del].vlan_tag =
					cpu_to_le16((u16)(f->vlan));
			}

			cmd_flags |= I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
			del_list[num_del].flags = cmd_flags;
			num_del++;

			/* flush a full buffer */
			if (num_del == filter_list_len) {
				aq_ret = i40e_aq_remove_macvlan(hw, vsi->seid,
								del_list,
								num_del, NULL);
				aq_err = hw->aq.asq_last_status;
				num_del = 0;
				memset(del_list, 0, list_size);

				/* Explicitly ignore and do not report when
				 * firmware returns ENOENT.
				 */
				if (aq_ret && !(aq_err == I40E_AQ_RC_ENOENT)) {
					retval = -EIO;
					dev_info(&pf->pdev->dev,
						 "ignoring delete macvlan error on %s, err %s, aq_err %s\n",
						 vsi_name,
						 i40e_stat_str(hw, aq_ret),
						 i40e_aq_str(hw, aq_err));
				}
			}
			/* Release memory for MAC filter entries which were
			 * synced up with HW.
			 */
			list_del(&f->list);
			kfree(f);
		}

		if (num_del) {
			aq_ret = i40e_aq_remove_macvlan(hw, vsi->seid, del_list,
							num_del, NULL);
			aq_err = hw->aq.asq_last_status;
			num_del = 0;

			/* Explicitly ignore and do not report when firmware
			 * returns ENOENT.
			 */
			if (aq_ret && !(aq_err == I40E_AQ_RC_ENOENT)) {
				retval = -EIO;
				dev_info(&pf->pdev->dev,
					 "ignoring delete macvlan error on %s, err %s aq_err %s\n",
					 vsi_name,
					 i40e_stat_str(hw, aq_ret),
					 i40e_aq_str(hw, aq_err));
			}
		}

		kfree(del_list);
		del_list = NULL;
	}

	if (!list_empty(&tmp_add_list)) {
		/* Do all the adds now. */
		filter_list_len = hw->aq.asq_buf_size /
			       sizeof(struct i40e_aqc_add_macvlan_element_data);
		list_size = filter_list_len *
			       sizeof(struct i40e_aqc_add_macvlan_element_data);
		add_list = kzalloc(list_size, GFP_ATOMIC);
		if (!add_list) {
			retval = -ENOMEM;
			goto out;
		}
		num_add = 0;
		list_for_each_entry(f, &tmp_add_list, list) {
			if (test_bit(__I40E_FILTER_OVERFLOW_PROMISC,
				     &vsi->state)) {
				f->state = I40E_FILTER_FAILED;
				continue;
			}
			/* add to add array */
			if (num_add == 0)
				add_head = f;
			cmd_flags = 0;
			ether_addr_copy(add_list[num_add].mac_addr, f->macaddr);
			if (f->vlan == I40E_VLAN_ANY) {
				add_list[num_add].vlan_tag = 0;
				cmd_flags |= I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
			} else {
				add_list[num_add].vlan_tag =
					cpu_to_le16((u16)(f->vlan));
			}
			add_list[num_add].queue_number = 0;
			cmd_flags |= I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;
			add_list[num_add].flags = cpu_to_le16(cmd_flags);
			num_add++;

			/* flush a full buffer */
			if (num_add == filter_list_len) {
				aq_ret = i40e_aq_add_macvlan(hw, vsi->seid,
							     add_list, num_add,
							     NULL);
				aq_err = hw->aq.asq_last_status;
				fcnt = i40e_update_filter_state(num_add,
								add_list,
								add_head,
								aq_ret);
				vsi->active_filters += fcnt;

				if (fcnt != num_add) {
					promisc_changed = true;
					set_bit(__I40E_FILTER_OVERFLOW_PROMISC,
						&vsi->state);
					vsi->promisc_threshold =
						(vsi->active_filters * 3) / 4;
					dev_warn(&pf->pdev->dev,
						 "Error %s adding RX filters on %s, promiscuous mode forced on\n",
						 i40e_aq_str(hw, aq_err),
						 vsi_name);
				}
				memset(add_list, 0, list_size);
				num_add = 0;
			}
		}
		if (num_add) {
			aq_ret = i40e_aq_add_macvlan(hw, vsi->seid,
						     add_list, num_add, NULL);
			aq_err = hw->aq.asq_last_status;
			fcnt = i40e_update_filter_state(num_add, add_list,
							add_head, aq_ret);
			vsi->active_filters += fcnt;
			if (fcnt != num_add) {
				promisc_changed = true;
				set_bit(__I40E_FILTER_OVERFLOW_PROMISC,
					&vsi->state);
				vsi->promisc_threshold =
						(vsi->active_filters * 3) / 4;
				dev_warn(&pf->pdev->dev,
					 "Error %s adding RX filters on %s, promiscuous mode forced on\n",
					 i40e_aq_str(hw, aq_err), vsi_name);
			}
		}
		/* Now move all of the filters from the temp add list back to
		 * the VSI's list.
		 */
		spin_lock_bh(&vsi->mac_filter_list_lock);
		list_for_each_entry_safe(f, ftmp, &tmp_add_list, list) {
			list_move_tail(&f->list, &vsi->mac_filter_list);
		}
		spin_unlock_bh(&vsi->mac_filter_list_lock);
		kfree(add_list);
		add_list = NULL;
	}

	/* Check to see if we can drop out of overflow promiscuous mode. */
	if (test_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state) &&
	    (vsi->active_filters < vsi->promisc_threshold)) {
		int failed_count = 0;
		/* See if we have any failed filters. We can't drop out of
		 * promiscuous until these have all been deleted.
		 */
		spin_lock_bh(&vsi->mac_filter_list_lock);
		list_for_each_entry(f, &vsi->mac_filter_list, list) {
			if (f->state == I40E_FILTER_FAILED)
				failed_count++;
		}
		spin_unlock_bh(&vsi->mac_filter_list_lock);
		if (!failed_count) {
			dev_info(&pf->pdev->dev,
				 "filter logjam cleared on %s, leaving overflow promiscuous mode\n",
				 vsi_name);
			clear_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state);
			promisc_changed = true;
			vsi->promisc_threshold = 0;
		}
	}

	/* if the VF is not trusted do not do promisc */
	if ((vsi->type == I40E_VSI_SRIOV) && !pf->vf[vsi->vf_id].trusted) {
		clear_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state);
		goto out;
	}

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
		}
	}
	if ((changed_flags & IFF_PROMISC) ||
	    (promisc_changed &&
	     test_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state))) {
		bool cur_promisc;

		cur_promisc = (!!(vsi->current_netdev_flags & IFF_PROMISC) ||
			       test_bit(__I40E_FILTER_OVERFLOW_PROMISC,
					&vsi->state));
		if ((vsi->type == I40E_VSI_MAIN) &&
		    (pf->lan_veb != I40E_NO_VEB) &&
		    !(pf->flags & I40E_FLAG_MFP_ENABLED)) {
			/* set defport ON for Main VSI instead of true promisc
			 * this way we will get all unicast/multicast and VLAN
			 * promisc behavior but will not get VF or VMDq traffic
			 * replicated on the Main VSI.
			 */
			if (pf->cur_promisc != cur_promisc) {
				pf->cur_promisc = cur_promisc;
				if (cur_promisc)
					aq_ret =
					      i40e_aq_set_default_vsi(hw,
								      vsi->seid,
								      NULL);
				else
					aq_ret =
					    i40e_aq_clear_default_vsi(hw,
								      vsi->seid,
								      NULL);
				if (aq_ret) {
					retval = i40e_aq_rc_to_posix(aq_ret,
							hw->aq.asq_last_status);
					dev_info(&pf->pdev->dev,
						 "Set default VSI failed on %s, err %s, aq_err %s\n",
						 vsi_name,
						 i40e_stat_str(hw, aq_ret),
						 i40e_aq_str(hw,
						     hw->aq.asq_last_status));
				}
			}
		} else {
			aq_ret = i40e_aq_set_vsi_unicast_promiscuous(
							  hw,
							  vsi->seid,
							  cur_promisc, NULL,
							  true);
			if (aq_ret) {
				retval =
				i40e_aq_rc_to_posix(aq_ret,
						    hw->aq.asq_last_status);
				dev_info(&pf->pdev->dev,
					 "set unicast promisc failed on %s, err %s, aq_err %s\n",
					 vsi_name,
					 i40e_stat_str(hw, aq_ret),
					 i40e_aq_str(hw,
						     hw->aq.asq_last_status));
			}
			aq_ret = i40e_aq_set_vsi_multicast_promiscuous(
							  hw,
							  vsi->seid,
							  cur_promisc, NULL);
			if (aq_ret) {
				retval =
				i40e_aq_rc_to_posix(aq_ret,
						    hw->aq.asq_last_status);
				dev_info(&pf->pdev->dev,
					 "set multicast promisc failed on %s, err %s, aq_err %s\n",
					 vsi_name,
					 i40e_stat_str(hw, aq_ret),
					 i40e_aq_str(hw,
						     hw->aq.asq_last_status));
			}
		}
		aq_ret = i40e_aq_set_vsi_broadcast(&vsi->back->hw,
						   vsi->seid,
						   cur_promisc, NULL);
		if (aq_ret) {
			retval = i40e_aq_rc_to_posix(aq_ret,
						     pf->hw.aq.asq_last_status);
			dev_info(&pf->pdev->dev,
				 "set brdcast promisc failed, err %s, aq_err %s\n",
					 i40e_stat_str(hw, aq_ret),
					 i40e_aq_str(hw,
						     hw->aq.asq_last_status));
		}
	}
out:
	/* if something went wrong then set the changed flag so we try again */
	if (retval)
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;

	clear_bit(__I40E_CONFIG_BUSY, &vsi->state);
	return retval;
}

/**
 * i40e_sync_filters_subtask - Sync the VSI filter list with HW
 * @pf: board private structure
 **/
static void i40e_sync_filters_subtask(struct i40e_pf *pf)
{
	int v;

	if (!pf || !(pf->flags & I40E_FLAG_FILTER_SYNC))
		return;
	pf->flags &= ~I40E_FLAG_FILTER_SYNC;

	for (v = 0; v < pf->num_alloc_vsi; v++) {
		if (pf->vsi[v] &&
		    (pf->vsi[v]->flags & I40E_VSI_FLAG_FILTER_CHANGED)) {
			int ret = i40e_sync_vsi_filters(pf->vsi[v]);

			if (ret) {
				/* come back and try again later */
				pf->flags |= I40E_FLAG_FILTER_SYNC;
				break;
			}
		}
	}
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
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	struct i40e_vsi *vsi = np->vsi;

	/* MTU < 68 is an error and causes problems on some kernels */
	if ((new_mtu < 68) || (max_frame > I40E_MAX_RXBUFFER))
		return -EINVAL;

	netdev_info(netdev, "changing MTU from %d to %d\n",
		    netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	if (netif_running(netdev))
		i40e_vsi_reinit_locked(vsi);
	i40e_notify_client_of_l2_param_changes(vsi);
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
 * i40e_vlan_rx_register - Setup or shutdown vlan offload
 * @netdev: network interface to be adjusted
 * @features: netdev features to test if VLAN offload is enabled or not
 **/
static void i40e_vlan_rx_register(struct net_device *netdev, u32 features)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);
}

/**
 * i40e_vsi_add_vlan - Add vsi membership for given vlan
 * @vsi: the vsi being configured
 * @vid: vlan id to be added (0 = untagged only , -1 = any)
 **/
int i40e_vsi_add_vlan(struct i40e_vsi *vsi, s16 vid)
{
	struct i40e_mac_filter *f, *ftmp, *add_f;
	bool is_netdev, is_vf;

	is_vf = (vsi->type == I40E_VSI_SRIOV);
	is_netdev = !!(vsi->netdev);

	/* Locked once because all functions invoked below iterates list*/
	spin_lock_bh(&vsi->mac_filter_list_lock);

	if (is_netdev) {
		add_f = i40e_add_filter(vsi, vsi->netdev->dev_addr, vid,
					is_vf, is_netdev);
		if (!add_f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add vlan filter %d for %pM\n",
				 vid, vsi->netdev->dev_addr);
			spin_unlock_bh(&vsi->mac_filter_list_lock);
			return -ENOMEM;
		}
	}

	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
		add_f = i40e_add_filter(vsi, f->macaddr, vid, is_vf, is_netdev);
		if (!add_f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add vlan filter %d for %pM\n",
				 vid, f->macaddr);
			spin_unlock_bh(&vsi->mac_filter_list_lock);
			return -ENOMEM;
		}
	}

	/* Now if we add a vlan tag, make sure to check if it is the first
	 * tag (i.e. a "tag" -1 does exist) and if so replace the -1 "tag"
	 * with 0, so we now accept untagged and specified tagged traffic
	 * (and not all tags along with untagged)
	 */
	if (vid > 0) {
		if (is_netdev && i40e_find_filter(vsi, vsi->netdev->dev_addr,
						  I40E_VLAN_ANY,
						  is_vf, is_netdev)) {
			i40e_del_filter(vsi, vsi->netdev->dev_addr,
					I40E_VLAN_ANY, is_vf, is_netdev);
			add_f = i40e_add_filter(vsi, vsi->netdev->dev_addr, 0,
						is_vf, is_netdev);
			if (!add_f) {
				dev_info(&vsi->back->pdev->dev,
					 "Could not add filter 0 for %pM\n",
					 vsi->netdev->dev_addr);
				spin_unlock_bh(&vsi->mac_filter_list_lock);
				return -ENOMEM;
			}
		}
	}

	/* Do not assume that I40E_VLAN_ANY should be reset to VLAN 0 */
	if (vid > 0 && !vsi->info.pvid) {
		list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
			if (!i40e_find_filter(vsi, f->macaddr, I40E_VLAN_ANY,
					      is_vf, is_netdev))
				continue;
			i40e_del_filter(vsi, f->macaddr, I40E_VLAN_ANY,
					is_vf, is_netdev);
			add_f = i40e_add_filter(vsi, f->macaddr,
						0, is_vf, is_netdev);
			if (!add_f) {
				dev_info(&vsi->back->pdev->dev,
					 "Could not add filter 0 for %pM\n",
					f->macaddr);
				spin_unlock_bh(&vsi->mac_filter_list_lock);
				return -ENOMEM;
			}
		}
	}

	spin_unlock_bh(&vsi->mac_filter_list_lock);

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
	return 0;
}

/**
 * i40e_vsi_kill_vlan - Remove vsi membership for given vlan
 * @vsi: the vsi being configured
 * @vid: vlan id to be removed (0 = untagged only , -1 = any)
 *
 * Return: 0 on success or negative otherwise
 **/
int i40e_vsi_kill_vlan(struct i40e_vsi *vsi, s16 vid)
{
	struct net_device *netdev = vsi->netdev;
	struct i40e_mac_filter *f, *ftmp, *add_f;
	bool is_vf, is_netdev;
	int filter_count = 0;

	is_vf = (vsi->type == I40E_VSI_SRIOV);
	is_netdev = !!(netdev);

	/* Locked once because all functions invoked below iterates list */
	spin_lock_bh(&vsi->mac_filter_list_lock);

	if (is_netdev)
		i40e_del_filter(vsi, netdev->dev_addr, vid, is_vf, is_netdev);

	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list)
		i40e_del_filter(vsi, f->macaddr, vid, is_vf, is_netdev);

	/* go through all the filters for this VSI and if there is only
	 * vid == 0 it means there are no other filters, so vid 0 must
	 * be replaced with -1. This signifies that we should from now
	 * on accept any traffic (with any tag present, or untagged)
	 */
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (is_netdev) {
			if (f->vlan &&
			    ether_addr_equal(netdev->dev_addr, f->macaddr))
				filter_count++;
		}

		if (f->vlan)
			filter_count++;
	}

	if (!filter_count && is_netdev) {
		i40e_del_filter(vsi, netdev->dev_addr, 0, is_vf, is_netdev);
		f = i40e_add_filter(vsi, netdev->dev_addr, I40E_VLAN_ANY,
				    is_vf, is_netdev);
		if (!f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add filter %d for %pM\n",
				 I40E_VLAN_ANY, netdev->dev_addr);
			spin_unlock_bh(&vsi->mac_filter_list_lock);
			return -ENOMEM;
		}
	}

	if (!filter_count) {
		list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
			i40e_del_filter(vsi, f->macaddr, 0, is_vf, is_netdev);
			add_f = i40e_add_filter(vsi, f->macaddr, I40E_VLAN_ANY,
						is_vf, is_netdev);
			if (!add_f) {
				dev_info(&vsi->back->pdev->dev,
					 "Could not add filter %d for %pM\n",
					 I40E_VLAN_ANY, f->macaddr);
				spin_unlock_bh(&vsi->mac_filter_list_lock);
				return -ENOMEM;
			}
		}
	}

	spin_unlock_bh(&vsi->mac_filter_list_lock);

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
	return 0;
}

/**
 * i40e_vlan_rx_add_vid - Add a vlan id filter to HW offload
 * @netdev: network interface to be adjusted
 * @vid: vlan id to be added
 *
 * net_device_ops implementation for adding vlan ids
 **/
#ifdef I40E_FCOE
int i40e_vlan_rx_add_vid(struct net_device *netdev,
			 __always_unused __be16 proto, u16 vid)
#else
static int i40e_vlan_rx_add_vid(struct net_device *netdev,
				__always_unused __be16 proto, u16 vid)
#endif
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	int ret = 0;

	if (vid > 4095)
		return -EINVAL;

	/* If the network stack called us with vid = 0 then
	 * it is asking to receive priority tagged packets with
	 * vlan id 0.  Our HW receives them by default when configured
	 * to receive untagged packets so there is no need to add an
	 * extra filter for vlan 0 tagged packets.
	 */
	if (vid)
		ret = i40e_vsi_add_vlan(vsi, vid);

	if (!ret && (vid < VLAN_N_VID))
		set_bit(vid, vsi->active_vlans);

	return ret;
}

/**
 * i40e_vlan_rx_kill_vid - Remove a vlan id filter from HW offload
 * @netdev: network interface to be adjusted
 * @vid: vlan id to be removed
 *
 * net_device_ops implementation for removing vlan ids
 **/
#ifdef I40E_FCOE
int i40e_vlan_rx_kill_vid(struct net_device *netdev,
			  __always_unused __be16 proto, u16 vid)
#else
static int i40e_vlan_rx_kill_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
#endif
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
 * i40e_macaddr_init - explicitly write the mac address filters
 *
 * @vsi: pointer to the vsi
 * @macaddr: the MAC address
 *
 * This is needed when the macaddr has been obtained by other
 * means than the default, e.g., from Open Firmware or IDPROM.
 * Returns 0 on success, negative on failure
 **/
static int i40e_macaddr_init(struct i40e_vsi *vsi, u8 *macaddr)
{
	int ret;
	struct i40e_aqc_add_macvlan_element_data element;

	ret = i40e_aq_mac_address_write(&vsi->back->hw,
					I40E_AQC_WRITE_TYPE_LAA_WOL,
					macaddr, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Addr change for VSI failed: %d\n", ret);
		return -EADDRNOTAVAIL;
	}

	memset(&element, 0, sizeof(element));
	ether_addr_copy(element.mac_addr, macaddr);
	element.flags = cpu_to_le16(I40E_AQC_MACVLAN_ADD_PERFECT_MATCH);
	ret = i40e_aq_add_macvlan(&vsi->back->hw, vsi->seid, &element, 1, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "add filter failed err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
	}
	return ret;
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

	i40e_vlan_rx_register(vsi->netdev, vsi->netdev->features);

	for_each_set_bit(vid, vsi->active_vlans, VLAN_N_VID)
		i40e_vlan_rx_add_vid(vsi->netdev, htons(ETH_P_8021Q),
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
	i40e_vlan_stripping_disable(vsi);

	vsi->info.pvid = 0;
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

	if (!vsi->tx_rings)
		return;

	for (i = 0; i < vsi->num_queue_pairs; i++)
		if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc)
			i40e_free_tx_resources(vsi->tx_rings[i]);
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
#ifdef I40E_FCOE
	i40e_fcoe_setup_ddp_resources(vsi);
#endif
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
#ifdef I40E_FCOE
	i40e_fcoe_free_ddp_resources(vsi);
#endif
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
	struct i40e_vsi *vsi = ring->vsi;
	cpumask_var_t mask;

	if (!ring->q_vector || !ring->netdev)
		return;

	/* Single TC mode enable XPS */
	if (vsi->tc_config.numtc <= 1) {
		if (!test_and_set_bit(__I40E_TX_XPS_INIT_DONE, &ring->state))
			netif_set_xps_queue(ring->netdev,
					    &ring->q_vector->affinity_mask,
					    ring->queue_index);
	} else if (alloc_cpumask_var(&mask, GFP_KERNEL)) {
		/* Disable XPS to allow selection based on TC */
		bitmap_zero(cpumask_bits(mask), nr_cpumask_bits);
		netif_set_xps_queue(ring->netdev, mask, ring->queue_index);
		free_cpumask_var(mask);
	}

	/* schedule our worker thread which will take care of
	 * applying the new filter changes
	 */
	i40e_service_event_schedule(vsi->back);
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
#ifdef I40E_FCOE
	tx_ctx.fc_ena = (vsi->type == I40E_VSI_FCOE);
#endif
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
	if (vsi->type == I40E_VSI_VMDQ2) {
		qtx_ctl = I40E_QTX_CTL_VM_QUEUE;
		qtx_ctl |= ((vsi->id) << I40E_QTX_CTL_VFVM_INDX_SHIFT) &
			   I40E_QTX_CTL_VFVM_INDX_MASK;
	} else {
		qtx_ctl = I40E_QTX_CTL_PF_QUEUE;
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

	ring->state = 0;

	/* clear the context structure first */
	memset(&rx_ctx, 0, sizeof(rx_ctx));

	ring->rx_buf_len = vsi->rx_buf_len;

	rx_ctx.dbuff = ring->rx_buf_len >> I40E_RXQ_CTX_DBUFF_SHIFT;

	rx_ctx.base = (ring->dma / 128);
	rx_ctx.qlen = ring->count;

	/* use 32 byte descriptors */
	rx_ctx.dsize = 1;

	/* descriptor type is always zero
	 * rx_ctx.dtype = 0;
	 */
	rx_ctx.hsplit_0 = 0;

	rx_ctx.rxmax = min_t(u16, vsi->max_frame, chain_len * ring->rx_buf_len);
	if (hw->revision_id == 0)
		rx_ctx.lrxqthresh = 0;
	else
		rx_ctx.lrxqthresh = 2;
	rx_ctx.crcstrip = 1;
	rx_ctx.l2tsel = 1;
	/* this controls whether VLAN is stripped from inner headers */
	rx_ctx.showiv = 0;
#ifdef I40E_FCOE
	rx_ctx.fc_ena = (vsi->type == I40E_VSI_FCOE);
#endif
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

	/* cache tail for quicker writes, and clear the reg before use */
	ring->tail = hw->hw_addr + I40E_QRX_TAIL(pf_q);
	writel(0, ring->tail);

	i40e_alloc_rx_buffers(ring, I40E_DESC_UNUSED(ring));

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

	if (vsi->netdev && (vsi->netdev->mtu > ETH_DATA_LEN))
		vsi->max_frame = vsi->netdev->mtu + ETH_HLEN
			       + ETH_FCS_LEN + VLAN_HLEN;
	else
		vsi->max_frame = I40E_RXBUFFER_2048;

	vsi->rx_buf_len = I40E_RXBUFFER_2048;

#ifdef I40E_FCOE
	/* setup rx buffer for FCoE */
	if ((vsi->type == I40E_VSI_FCOE) &&
	    (vsi->back->flags & I40E_FLAG_FCOE_ENABLED)) {
		vsi->rx_buf_len = I40E_RXBUFFER_3072;
		vsi->max_frame = I40E_RXBUFFER_3072;
	}

#endif /* I40E_FCOE */
	/* round up for the chip's needs */
	vsi->rx_buf_len = ALIGN(vsi->rx_buf_len,
				BIT_ULL(I40E_RXQ_CTX_DBUFF_SHIFT));

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
	struct i40e_pf *pf = vsi->back;
	int err;

	if (vsi->netdev)
		i40e_set_rx_mode(vsi->netdev);

	if (!!(pf->flags & I40E_FLAG_PF_MAC)) {
		err = i40e_macaddr_init(vsi, pf->hw.mac.addr);
		if (err) {
			dev_warn(&pf->pdev->dev,
				 "could not set up macaddr; err %d\n", err);
		}
	}
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

		q_vector->itr_countdown = ITR_COUNTDOWN_START;
		q_vector->rx.itr = ITR_TO_REG(vsi->rx_rings[i]->rx_itr_setting);
		q_vector->rx.latency_range = I40E_LOW_LATENCY;
		wr32(hw, I40E_PFINT_ITRN(I40E_RX_ITR, vector - 1),
		     q_vector->rx.itr);
		q_vector->tx.itr = ITR_TO_REG(vsi->tx_rings[i]->tx_itr_setting);
		q_vector->tx.latency_range = I40E_LOW_LATENCY;
		wr32(hw, I40E_PFINT_ITRN(I40E_TX_ITR, vector - 1),
		     q_vector->tx.itr);
		wr32(hw, I40E_PFINT_RATEN(vector - 1),
		     INTRL_USEC_TO_REG(vsi->int_rate_limit));

		/* Linked list for the queuepairs assigned to this vector */
		wr32(hw, I40E_PFINT_LNKLSTN(vector - 1), qp);
		for (q = 0; q < q_vector->num_ringpairs; q++) {
			u32 val;

			val = I40E_QINT_RQCTL_CAUSE_ENA_MASK |
			      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT)  |
			      (vector      << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
			      (qp          << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT)|
			      (I40E_QUEUE_TYPE_TX
				      << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT);

			wr32(hw, I40E_QINT_RQCTL(qp), val);

			val = I40E_QINT_TQCTL_CAUSE_ENA_MASK |
			      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT)  |
			      (vector      << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
			      ((qp+1)      << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT)|
			      (I40E_QUEUE_TYPE_RX
				      << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

			/* Terminate the linked list */
			if (q == (q_vector->num_ringpairs - 1))
				val |= (I40E_QUEUE_END_OF_LIST
					   << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);

			wr32(hw, I40E_QINT_TQCTL(qp), val);
			qp++;
		}
	}

	i40e_flush(hw);
}

/**
 * i40e_enable_misc_int_causes - enable the non-queue interrupts
 * @hw: ptr to the hardware info
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
	struct i40e_q_vector *q_vector = vsi->q_vectors[0];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	/* set the ITR configuration */
	q_vector->itr_countdown = ITR_COUNTDOWN_START;
	q_vector->rx.itr = ITR_TO_REG(vsi->rx_rings[0]->rx_itr_setting);
	q_vector->rx.latency_range = I40E_LOW_LATENCY;
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), q_vector->rx.itr);
	q_vector->tx.itr = ITR_TO_REG(vsi->tx_rings[0]->tx_itr_setting);
	q_vector->tx.latency_range = I40E_LOW_LATENCY;
	wr32(hw, I40E_PFINT_ITR0(I40E_TX_ITR), q_vector->tx.itr);

	i40e_enable_misc_int_causes(pf);

	/* FIRSTQ_INDX = 0, FIRSTQ_TYPE = 0 (rx) */
	wr32(hw, I40E_PFINT_LNKLST0, 0);

	/* Associate the queue pair to the vector and enable the queue int */
	val = I40E_QINT_RQCTL_CAUSE_ENA_MASK		      |
	      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
	      (I40E_QUEUE_TYPE_TX << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

	wr32(hw, I40E_QINT_RQCTL(0), val);

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
 * @clearpba: true when all pending interrupt events should be cleared
 **/
void i40e_irq_dynamic_enable_icr0(struct i40e_pf *pf, bool clearpba)
{
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	val = I40E_PFINT_DYN_CTL0_INTENA_MASK   |
	      (clearpba ? I40E_PFINT_DYN_CTL0_CLEARPBA_MASK : 0) |
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

	for (vector = 0; vector < q_vectors; vector++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[vector];

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
		err = request_irq(pf->msix_entries[base + vector].vector,
				  vsi->irq_handler,
				  0,
				  q_vector->name,
				  q_vector);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "MSIX request_irq failed, error: %d\n", err);
			goto free_queue_irqs;
		}
		/* assign the mask for this irq */
		irq_set_affinity_hint(pf->msix_entries[base + vector].vector,
				      &q_vector->affinity_mask);
	}

	vsi->irqs_ready = true;
	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		irq_set_affinity_hint(pf->msix_entries[base + vector].vector,
				      NULL);
		free_irq(pf->msix_entries[base + vector].vector,
			 &(vsi->q_vectors[vector]));
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

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		wr32(hw, I40E_QINT_TQCTL(vsi->tx_rings[i]->reg_idx), 0);
		wr32(hw, I40E_QINT_RQCTL(vsi->rx_rings[i]->reg_idx), 0);
	}

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
		i40e_irq_dynamic_enable_icr0(pf, true);
	}

	i40e_flush(&pf->hw);
	return 0;
}

/**
 * i40e_stop_misc_vector - Stop the vector that handles non-queue events
 * @pf: board private structure
 **/
static void i40e_stop_misc_vector(struct i40e_pf *pf)
{
	/* Disable ICR 0 */
	wr32(&pf->hw, I40E_PFINT_ICR0_ENA, 0);
	i40e_flush(&pf->hw);
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
	    (ena_mask & I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK)) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK;
		icr0 &= ~I40E_PFINT_ICR0_ENA_PE_CRITERR_MASK;
		dev_info(&pf->pdev->dev, "cleared PE_CRITERR\n");
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
		if (!test_bit(__I40E_DOWN, &pf->state))
			napi_schedule_irqoff(&q_vector->napi);
	}

	if (icr0 & I40E_PFINT_ICR0_ADMINQ_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
		set_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state);
		i40e_debug(&pf->hw, I40E_DEBUG_NVM, "AdminQ event\n");
	}

	if (icr0 & I40E_PFINT_ICR0_MAL_DETECT_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
		set_bit(__I40E_MDD_EVENT_PENDING, &pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_VFLR_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_VFLR_MASK;
		set_bit(__I40E_VFLR_EVENT_PENDING, &pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_GRST_MASK) {
		if (!test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state))
			set_bit(__I40E_RESET_INTR_RECEIVED, &pf->state);
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
			set_bit(__I40E_EMP_RESET_INTR_RECEIVED, &pf->state);
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
			set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
			i40e_service_event_schedule(pf);
		}
		ena_mask &= ~icr0_remaining;
	}
	ret = IRQ_HANDLED;

enable_intr:
	/* re-enable interrupt causes */
	wr32(hw, I40E_PFINT_ICR0_ENA, ena_mask);
	if (!test_bit(__I40E_DOWN, &pf->state)) {
		i40e_service_event_schedule(pf);
		i40e_irq_dynamic_enable_icr0(pf, false);
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
		read_barrier_depends();

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
#ifdef I40E_FCOE
void i40e_netpoll(struct net_device *netdev)
#else
static void i40e_netpoll(struct net_device *netdev)
#endif
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int i;

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, &vsi->state))
		return;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = 0; i < vsi->num_q_vectors; i++)
			i40e_msix_clean_rings(0, vsi->q_vectors[i]);
	} else {
		i40e_intr(pf->pdev->irq, netdev);
	}
}
#endif

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
 * i40e_vsi_control_tx - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
static int i40e_vsi_control_tx(struct i40e_vsi *vsi, bool enable)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int i, j, pf_q, ret = 0;
	u32 tx_reg;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {

		/* warn the TX unit of coming changes */
		i40e_pre_tx_queue_cfg(&pf->hw, pf_q, enable);
		if (!enable)
			usleep_range(10, 20);

		for (j = 0; j < 50; j++) {
			tx_reg = rd32(hw, I40E_QTX_ENA(pf_q));
			if (((tx_reg >> I40E_QTX_ENA_QENA_REQ_SHIFT) & 1) ==
			    ((tx_reg >> I40E_QTX_ENA_QENA_STAT_SHIFT) & 1))
				break;
			usleep_range(1000, 2000);
		}
		/* Skip if the queue is already in the requested state */
		if (enable == !!(tx_reg & I40E_QTX_ENA_QENA_STAT_MASK))
			continue;

		/* turn on/off the queue */
		if (enable) {
			wr32(hw, I40E_QTX_HEAD(pf_q), 0);
			tx_reg |= I40E_QTX_ENA_QENA_REQ_MASK;
		} else {
			tx_reg &= ~I40E_QTX_ENA_QENA_REQ_MASK;
		}

		wr32(hw, I40E_QTX_ENA(pf_q), tx_reg);
		/* No waiting for the Tx queue to disable */
		if (!enable && test_bit(__I40E_PORT_TX_SUSPENDED, &pf->state))
			continue;

		/* wait for the change to finish */
		ret = i40e_pf_txq_wait(pf, pf_q, enable);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Tx ring %d %sable timeout\n",
				 vsi->seid, pf_q, (enable ? "en" : "dis"));
			break;
		}
	}

	if (hw->revision_id == 0)
		mdelay(50);
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
 * i40e_vsi_control_rx - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
static int i40e_vsi_control_rx(struct i40e_vsi *vsi, bool enable)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int i, j, pf_q, ret = 0;
	u32 rx_reg;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		for (j = 0; j < 50; j++) {
			rx_reg = rd32(hw, I40E_QRX_ENA(pf_q));
			if (((rx_reg >> I40E_QRX_ENA_QENA_REQ_SHIFT) & 1) ==
			    ((rx_reg >> I40E_QRX_ENA_QENA_STAT_SHIFT) & 1))
				break;
			usleep_range(1000, 2000);
		}

		/* Skip if the queue is already in the requested state */
		if (enable == !!(rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
			continue;

		/* turn on/off the queue */
		if (enable)
			rx_reg |= I40E_QRX_ENA_QENA_REQ_MASK;
		else
			rx_reg &= ~I40E_QRX_ENA_QENA_REQ_MASK;
		wr32(hw, I40E_QRX_ENA(pf_q), rx_reg);
		/* No waiting for the Tx queue to disable */
		if (!enable && test_bit(__I40E_PORT_TX_SUSPENDED, &pf->state))
			continue;

		/* wait for the change to finish */
		ret = i40e_pf_rxq_wait(pf, pf_q, enable);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Rx ring %d %sable timeout\n",
				 vsi->seid, pf_q, (enable ? "en" : "dis"));
			break;
		}
	}

	return ret;
}

/**
 * i40e_vsi_control_rings - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
int i40e_vsi_control_rings(struct i40e_vsi *vsi, bool request)
{
	int ret = 0;

	/* do rx first for enable and last for disable */
	if (request) {
		ret = i40e_vsi_control_rx(vsi, request);
		if (ret)
			return ret;
		ret = i40e_vsi_control_tx(vsi, request);
	} else {
		/* Ignore return value, we need to shutdown whatever we can */
		i40e_vsi_control_tx(vsi, request);
		i40e_vsi_control_rx(vsi, request);
	}

	return ret;
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
			u16 vector = i + base;

			/* free only the irqs that were actually requested */
			if (!vsi->q_vectors[i] ||
			    !vsi->q_vectors[i]->num_ringpairs)
				continue;

			/* clear the affinity_mask in the IRQ descriptor */
			irq_set_affinity_hint(pf->msix_entries[vector].vector,
					      NULL);
			synchronize_irq(pf->msix_entries[vector].vector);
			free_irq(pf->msix_entries[vector].vector,
				 vsi->q_vectors[i]);

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

	i40e_stop_misc_vector(pf);
	if (pf->flags & I40E_FLAG_MSIX_ENABLED && pf->msix_entries) {
		synchronize_irq(pf->msix_entries[0].vector);
		free_irq(pf->msix_entries[0].vector, pf);
	}

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

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_enable(&vsi->q_vectors[q_idx]->napi);
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

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_disable(&vsi->q_vectors[q_idx]->napi);
}

/**
 * i40e_vsi_close - Shut down a VSI
 * @vsi: the vsi to be quelled
 **/
static void i40e_vsi_close(struct i40e_vsi *vsi)
{
	bool reset = false;

	if (!test_and_set_bit(__I40E_DOWN, &vsi->state))
		i40e_down(vsi);
	i40e_vsi_free_irq(vsi);
	i40e_vsi_free_tx_resources(vsi);
	i40e_vsi_free_rx_resources(vsi);
	vsi->current_netdev_flags = 0;
	if (test_bit(__I40E_RESET_RECOVERY_PENDING, &vsi->back->state))
		reset = true;
	i40e_notify_client_of_netdev_close(vsi, reset);
}

/**
 * i40e_quiesce_vsi - Pause a given VSI
 * @vsi: the VSI being paused
 **/
static void i40e_quiesce_vsi(struct i40e_vsi *vsi)
{
	if (test_bit(__I40E_DOWN, &vsi->state))
		return;

	/* No need to disable FCoE VSI when Tx suspended */
	if ((test_bit(__I40E_PORT_TX_SUSPENDED, &vsi->back->state)) &&
	    vsi->type == I40E_VSI_FCOE) {
		dev_dbg(&vsi->back->pdev->dev,
			 "VSI seid %d skipping FCoE VSI disable\n", vsi->seid);
		return;
	}

	set_bit(__I40E_NEEDS_RESTART, &vsi->state);
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
	if (!test_bit(__I40E_NEEDS_RESTART, &vsi->state))
		return;

	clear_bit(__I40E_NEEDS_RESTART, &vsi->state);
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

#ifdef CONFIG_I40E_DCB
/**
 * i40e_vsi_wait_queues_disabled - Wait for VSI's queues to be disabled
 * @vsi: the VSI being configured
 *
 * This function waits for the given VSI's queues to be disabled.
 **/
static int i40e_vsi_wait_queues_disabled(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i, pf_q, ret;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		/* Check and wait for the disable status of the queue */
		ret = i40e_pf_txq_wait(pf, pf_q, false);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "VSI seid %d Tx ring %d disable timeout\n",
				 vsi->seid, pf_q);
			return ret;
		}
	}

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		/* Check and wait for the disable status of the queue */
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
		/* No need to wait for FCoE VSI queues */
		if (pf->vsi[v] && pf->vsi[v]->type != I40E_VSI_FCOE) {
			ret = i40e_vsi_wait_queues_disabled(pf->vsi[v]);
			if (ret)
				break;
		}
	}

	return ret;
}

#endif

/**
 * i40e_detect_recover_hung_queue - Function to detect and recover hung_queue
 * @q_idx: TX queue number
 * @vsi: Pointer to VSI struct
 *
 * This function checks specified queue for given VSI. Detects hung condition.
 * Sets hung bit since it is two step process. Before next run of service task
 * if napi_poll runs, it reset 'hung' bit for respective q_vector. If not,
 * hung condition remain unchanged and during subsequent run, this function
 * issues SW interrupt to recover from hung condition.
 **/
static void i40e_detect_recover_hung_queue(int q_idx, struct i40e_vsi *vsi)
{
	struct i40e_ring *tx_ring = NULL;
	struct i40e_pf	*pf;
	u32 head, val, tx_pending_hw;
	int i;

	pf = vsi->back;

	/* now that we have an index, find the tx_ring struct */
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		if (vsi->tx_rings[i] && vsi->tx_rings[i]->desc) {
			if (q_idx == vsi->tx_rings[i]->queue_index) {
				tx_ring = vsi->tx_rings[i];
				break;
			}
		}
	}

	if (!tx_ring)
		return;

	/* Read interrupt register */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		val = rd32(&pf->hw,
			   I40E_PFINT_DYN_CTLN(tx_ring->q_vector->v_idx +
					       tx_ring->vsi->base_vector - 1));
	else
		val = rd32(&pf->hw, I40E_PFINT_DYN_CTL0);

	head = i40e_get_head(tx_ring);

	tx_pending_hw = i40e_get_tx_pending(tx_ring, false);

	/* HW is done executing descriptors, updated HEAD write back,
	 * but SW hasn't processed those descriptors. If interrupt is
	 * not generated from this point ON, it could result into
	 * dev_watchdog detecting timeout on those netdev_queue,
	 * hence proactively trigger SW interrupt.
	 */
	if (tx_pending_hw && (!(val & I40E_PFINT_DYN_CTLN_INTENA_MASK))) {
		/* NAPI Poll didn't run and clear since it was set */
		if (test_and_clear_bit(I40E_Q_VECTOR_HUNG_DETECT,
				       &tx_ring->q_vector->hung_detected)) {
			netdev_info(vsi->netdev, "VSI_seid %d, Hung TX queue %d, tx_pending_hw: %d, NTC:0x%x, HWB: 0x%x, NTU: 0x%x, TAIL: 0x%x\n",
				    vsi->seid, q_idx, tx_pending_hw,
				    tx_ring->next_to_clean, head,
				    tx_ring->next_to_use,
				    readl(tx_ring->tail));
			netdev_info(vsi->netdev, "VSI_seid %d, Issuing force_wb for TX queue %d, Interrupt Reg: 0x%x\n",
				    vsi->seid, q_idx, val);
			i40e_force_wb(vsi, tx_ring->q_vector);
		} else {
			/* First Chance - detected possible hung */
			set_bit(I40E_Q_VECTOR_HUNG_DETECT,
				&tx_ring->q_vector->hung_detected);
		}
	}

	/* This is the case where we have interrupts missing,
	 * so the tx_pending in HW will most likely be 0, but we
	 * will have tx_pending in SW since the WB happened but the
	 * interrupt got lost.
	 */
	if ((!tx_pending_hw) && i40e_get_tx_pending(tx_ring, true) &&
	    (!(val & I40E_PFINT_DYN_CTLN_INTENA_MASK))) {
		if (napi_reschedule(&tx_ring->q_vector->napi))
			tx_ring->tx_stats.tx_lost_interrupt++;
	}
}

/**
 * i40e_detect_recover_hung - Function to detect and recover hung_queues
 * @pf:  pointer to PF struct
 *
 * LAN VSI has netdev and netdev has TX queues. This function is to check
 * each of those TX queues if they are hung, trigger recovery by issuing
 * SW interrupt.
 **/
static void i40e_detect_recover_hung(struct i40e_pf *pf)
{
	struct net_device *netdev;
	struct i40e_vsi *vsi;
	int i;

	/* Only for LAN VSI */
	vsi = pf->vsi[pf->lan_vsi];

	if (!vsi)
		return;

	/* Make sure, VSI state is not DOWN/RECOVERY_PENDING */
	if (test_bit(__I40E_DOWN, &vsi->back->state) ||
	    test_bit(__I40E_RESET_RECOVERY_PENDING, &vsi->back->state))
		return;

	/* Make sure type is MAIN VSI */
	if (vsi->type != I40E_VSI_MAIN)
		return;

	netdev = vsi->netdev;
	if (!netdev)
		return;

	/* Bail out if netif_carrier is not OK */
	if (!netif_carrier_ok(netdev))
		return;

	/* Go thru' TX queues for netdev */
	for (i = 0; i < netdev->num_tx_queues; i++) {
		struct netdev_queue *q;

		q = netdev_get_tx_queue(netdev, i);
		if (q)
			i40e_detect_recover_hung_queue(i, vsi);
	}
}

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

	/* If DCB is not enabled then always in single TC */
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
 * i40e_pf_get_default_tc - Get bitmap for first enabled TC
 * @pf: PF being queried
 *
 * Return a bitmap for first enabled traffic class for this PF.
 **/
static u8 i40e_pf_get_default_tc(struct i40e_pf *pf)
{
	u8 enabled_tc = pf->hw.func_caps.enabled_tcmap;
	u8 i = 0;

	if (!enabled_tc)
		return 0x1; /* TC0 */

	/* Find the first enabled TC */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & BIT(i))
			break;
	}

	return BIT(i);
}

/**
 * i40e_pf_get_pf_tc_map - Get bitmap for enabled traffic classes
 * @pf: PF being queried
 *
 * Return a bitmap for enabled traffic classes for this PF.
 **/
static u8 i40e_pf_get_tc_map(struct i40e_pf *pf)
{
	/* If DCB is not enabled for this PF then just return default TC */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return i40e_pf_get_default_tc(pf);

	/* SFP mode we want PF to be enabled for all TCs */
	if (!(pf->flags & I40E_FLAG_MFP_ENABLED))
		return i40e_dcb_get_enabled_tc(&pf->hw.local_dcbx_config);

	/* MFP enabled and iSCSI PF type */
	if (pf->hw.func_caps.iscsi)
		return i40e_get_iscsi_tc_map(pf);
	else
		return i40e_pf_get_default_tc(pf);
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
 * @bw_credits: BW shared credits per TC
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_vsi_configure_bw_alloc(struct i40e_vsi *vsi, u8 enabled_tc,
				       u8 *bw_share)
{
	struct i40e_aqc_configure_vsi_tc_bw_data bw_data;
	i40e_status ret;
	int i;

	bw_data.tc_valid_bits = enabled_tc;
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		bw_data.tc_bw_credits[i] = bw_share[i];

	ret = i40e_aq_config_vsi_tc_bw(&vsi->back->hw, vsi->seid, &bw_data,
				       NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "AQ command Config VSI BW allocation per TC failed = %d\n",
			 vsi->back->hw.aq.asq_last_status);
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
	struct i40e_vsi_context ctxt;
	int ret = 0;
	int i;

	/* Check if enabled_tc is same as existing or new TCs */
	if (vsi->tc_config.enabled_tc == enabled_tc)
		return ret;

	/* Enable ETS TCs with equal BW Share for now across all VSIs */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & BIT(i))
			bw_share[i] = 1;
	}

	ret = i40e_vsi_configure_bw_alloc(vsi, enabled_tc, bw_share);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed configuring TC map %d for VSI %d\n",
			 enabled_tc, vsi->seid);
		goto out;
	}

	/* Update Queue Pairs Mapping for currently enabled UPs */
	ctxt.seid = vsi->seid;
	ctxt.pf_num = vsi->back->hw.pf_id;
	ctxt.vf_num = 0;
	ctxt.uplink_seid = vsi->uplink_seid;
	ctxt.info = vsi->info;
	i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, false);

	if (vsi->back->flags & I40E_FLAG_IWARP_ENABLED) {
		ctxt.info.valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);
		ctxt.info.queueing_opt_flags |= I40E_AQ_VSI_QUE_OPT_TCP_ENA;
	}

	/* Update the VSI after updating the VSI queue-mapping information */
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Update vsi tc config failed, err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
		goto out;
	}
	/* update the local VSI info with updated queue map */
	i40e_vsi_update_queue_map(vsi, &ctxt);
	vsi->info.valid_sections = 0;

	/* Update current VSI BW information */
	ret = i40e_vsi_get_bw_info(vsi);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed updating vsi bw info, err %s aq_err %s\n",
			 i40e_stat_str(&vsi->back->hw, ret),
			 i40e_aq_str(&vsi->back->hw,
				     vsi->back->hw.aq.asq_last_status));
		goto out;
	}

	/* Update the netdev TC setup */
	i40e_vsi_config_netdev_tc(vsi, enabled_tc);
out:
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
#ifdef I40E_FCOE
		 * - For FCoE VSI only enable the TC configured
		 *   as per the APP TLV
#endif
		 * - For all others keep them at TC0 for now
		 */
		if (v == pf->lan_vsi)
			tc_map = i40e_pf_get_tc_map(pf);
		else
			tc_map = i40e_pf_get_default_tc(pf);
#ifdef I40E_FCOE
		if (pf->vsi[v]->type == I40E_VSI_FCOE)
			tc_map = i40e_get_fcoe_tc_map(pf);
#endif /* #ifdef I40E_FCOE */

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
		set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
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

	/* Do not enable DCB for SW1 and SW2 images even if the FW is capable */
	if (pf->flags & I40E_FLAG_NO_DCB_SUPPORT)
		goto out;

	/* Get the initial DCB configuration */
	err = i40e_init_dcb(hw);
	if (!err) {
		/* Device/Function is not DCBX capable */
		if ((!hw->func_caps.dcb) ||
		    (hw->dcbx_status == I40E_DCBX_STATUS_DISABLED)) {
			dev_info(&pf->pdev->dev,
				 "DCBX offload is not supported or is disabled for this PF.\n");

			if (pf->flags & I40E_FLAG_MFP_ENABLED)
				goto out;

		} else {
			/* When status is not DISABLED then DCBX in FW */
			pf->dcbx_cap = DCB_CAP_DCBX_LLD_MANAGED |
				       DCB_CAP_DCBX_VER_IEEE;

			pf->flags |= I40E_FLAG_DCB_CAPABLE;
			/* Enable DCB tagging only when more than one TC */
			if (i40e_dcb_get_num_tc(&hw->local_dcbx_config) > 1)
				pf->flags |= I40E_FLAG_DCB_ENABLED;
			dev_dbg(&pf->pdev->dev,
				"DCBX offload is supported for this PF.\n");
		}
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
#define SPEED_SIZE 14
#define FC_SIZE 8
/**
 * i40e_print_link_message - print link up or down
 * @vsi: the VSI for which link needs a message
 */
void i40e_print_link_message(struct i40e_vsi *vsi, bool isup)
{
	char *speed = "Unknown";
	char *fc = "Unknown";

	if (vsi->current_isup == isup)
		return;
	vsi->current_isup = isup;
	if (!isup) {
		netdev_info(vsi->netdev, "NIC Link is Down\n");
		return;
	}

	/* Warn user if link speed on NPAR enabled partition is not at
	 * least 10GB
	 */
	if (vsi->back->hw.func_caps.npar_enable &&
	    (vsi->back->hw.phy.link_info.link_speed == I40E_LINK_SPEED_1GB ||
	     vsi->back->hw.phy.link_info.link_speed == I40E_LINK_SPEED_100MB))
		netdev_warn(vsi->netdev,
			    "The partition detected link speed that is less than 10Gbps\n");

	switch (vsi->back->hw.phy.link_info.link_speed) {
	case I40E_LINK_SPEED_40GB:
		speed = "40 G";
		break;
	case I40E_LINK_SPEED_20GB:
		speed = "20 G";
		break;
	case I40E_LINK_SPEED_10GB:
		speed = "10 G";
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

	switch (vsi->back->hw.fc.current_mode) {
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

	netdev_info(vsi->netdev, "NIC Link is Up %sbps Full Duplex, Flow Control: %s\n",
		    speed, fc);
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
	err = i40e_vsi_control_rings(vsi, true);
	if (err)
		return err;

	clear_bit(__I40E_DOWN, &vsi->state);
	i40e_napi_enable_all(vsi);
	i40e_vsi_enable_irq(vsi);

	if ((pf->hw.phy.link_info.link_info & I40E_AQ_LINK_UP) &&
	    (vsi->netdev)) {
		i40e_print_link_message(vsi, true);
		netif_tx_start_all_queues(vsi->netdev);
		netif_carrier_on(vsi->netdev);
	} else if (vsi->netdev) {
		i40e_print_link_message(vsi, false);
		/* need to check for qualified module here*/
		if ((pf->hw.phy.link_info.link_info &
			I40E_AQ_MEDIA_AVAILABLE) &&
		    (!(pf->hw.phy.link_info.an_info &
			I40E_AQ_QUALIFIED_MODULE)))
			netdev_err(vsi->netdev,
				   "the driver failed to link because an unqualified module was detected.");
	}

	/* replay FDIR SB filters */
	if (vsi->type == I40E_VSI_FDIR) {
		/* reset fd counters */
		pf->fd_add_err = pf->fd_atr_cnt = 0;
		if (pf->fd_tcp_rule > 0) {
			pf->flags &= ~I40E_FLAG_FD_ATR_ENABLED;
			if (I40E_DEBUG_FD & pf->hw.debug_mask)
				dev_info(&pf->pdev->dev, "Forcing ATR off, sideband rules for TCP/IPv4 exist\n");
			pf->fd_tcp_rule = 0;
		}
		i40e_fdir_filter_restore(vsi);
	}

	/* On the next run of the service_task, notify any clients of the new
	 * opened netdev
	 */
	pf->flags |= I40E_FLAG_SERVICE_CLIENT_REQUESTED;
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

	WARN_ON(in_interrupt());
	while (test_and_set_bit(__I40E_CONFIG_BUSY, &pf->state))
		usleep_range(1000, 2000);
	i40e_down(vsi);

	i40e_up(vsi);
	clear_bit(__I40E_CONFIG_BUSY, &pf->state);
}

/**
 * i40e_up - Bring the connection back up after being down
 * @vsi: the VSI being configured
 **/
int i40e_up(struct i40e_vsi *vsi)
{
	int err;

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
	 * sets the vsi->state __I40E_DOWN bit.
	 */
	if (vsi->netdev) {
		netif_carrier_off(vsi->netdev);
		netif_tx_disable(vsi->netdev);
	}
	i40e_vsi_disable_irq(vsi);
	i40e_vsi_control_rings(vsi, false);
	i40e_napi_disable_all(vsi);

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		i40e_clean_tx_ring(vsi->tx_rings[i]);
		i40e_clean_rx_ring(vsi->rx_rings[i]);
	}

	i40e_notify_client_of_netdev_close(vsi, false);

}

/**
 * i40e_setup_tc - configure multiple traffic classes
 * @netdev: net device to configure
 * @tc: number of traffic classes to enable
 **/
static int i40e_setup_tc(struct net_device *netdev, u8 tc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u8 enabled_tc = 0;
	int ret = -EINVAL;
	int i;

	/* Check if DCB enabled to continue */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED)) {
		netdev_info(netdev, "DCB is not enabled for adapter\n");
		goto exit;
	}

	/* Check if MFP enabled */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		netdev_info(netdev, "Configuring TC not supported in MFP mode\n");
		goto exit;
	}

	/* Check whether tc count is within enabled limit */
	if (tc > i40e_pf_get_num_tc(pf)) {
		netdev_info(netdev, "TC count greater than enabled on link for adapter\n");
		goto exit;
	}

	/* Generate TC map for number of tc requested */
	for (i = 0; i < tc; i++)
		enabled_tc |= BIT(i);

	/* Requesting same TC configuration as already enabled */
	if (enabled_tc == vsi->tc_config.enabled_tc)
		return 0;

	/* Quiesce VSI queues */
	i40e_quiesce_vsi(vsi);

	/* Configure VSI for enabled TCs */
	ret = i40e_vsi_config_tc(vsi, enabled_tc);
	if (ret) {
		netdev_info(netdev, "Failed configuring TC for VSI seid=%d\n",
			    vsi->seid);
		goto exit;
	}

	/* Unquiesce VSI */
	i40e_unquiesce_vsi(vsi);

exit:
	return ret;
}

#ifdef I40E_FCOE
int __i40e_setup_tc(struct net_device *netdev, u32 handle, __be16 proto,
		    struct tc_to_netdev *tc)
#else
static int __i40e_setup_tc(struct net_device *netdev, u32 handle, __be16 proto,
			   struct tc_to_netdev *tc)
#endif
{
	if (handle != TC_H_ROOT || tc->type != TC_SETUP_MQPRIO)
		return -EINVAL;
	return i40e_setup_tc(netdev, tc->tc);
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
	if (test_bit(__I40E_TESTING, &pf->state) ||
	    test_bit(__I40E_BAD_EEPROM, &pf->state))
		return -EBUSY;

	netif_carrier_off(netdev);

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
	i40e_notify_client_of_netdev_open(vsi);

	return 0;
}

/**
 * i40e_vsi_open -
 * @vsi: the VSI to open
 *
 * Finish initialization of the VSI.
 *
 * Returns 0 on success, negative value on failure
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
		err = netif_set_real_num_tx_queues(vsi->netdev,
						   vsi->num_queue_pairs);
		if (err)
			goto err_set_queues;

		err = netif_set_real_num_rx_queues(vsi->netdev,
						   vsi->num_queue_pairs);
		if (err)
			goto err_set_queues;

	} else if (vsi->type == I40E_VSI_FDIR) {
		snprintf(int_name, sizeof(int_name) - 1, "%s-%s:fdir",
			 dev_driver_string(&pf->pdev->dev),
			 dev_name(&pf->pdev->dev));
		err = i40e_vsi_request_irq(vsi, int_name);

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
		i40e_do_reset(pf, BIT_ULL(__I40E_PF_RESET_REQUESTED));

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
	struct hlist_node *node2;

	hlist_for_each_entry_safe(filter, node2,
				  &pf->fdir_filter_list, fdir_node) {
		hlist_del(&filter->fdir_node);
		kfree(filter);
	}
	pf->fdir_pf_active_filters = 0;
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
 *
 * The essential difference in resets is that the PF Reset
 * doesn't clear the packet buffers, doesn't reset the PE
 * firmware, and doesn't bother the other PFs on the chip.
 **/
void i40e_do_reset(struct i40e_pf *pf, u32 reset_flags)
{
	u32 val;

	WARN_ON(in_interrupt());


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

	} else if (reset_flags & BIT_ULL(__I40E_PF_RESET_REQUESTED)) {

		/* Request a PF Reset
		 *
		 * Resets only the PF-specific registers
		 *
		 * This goes directly to the tear-down and rebuild of
		 * the switch, since we need to do all the recovery as
		 * for the Core Reset.
		 */
		dev_dbg(&pf->pdev->dev, "PFR requested\n");
		i40e_handle_reset_warning(pf);

	} else if (reset_flags & BIT_ULL(__I40E_REINIT_REQUESTED)) {
		int v;

		/* Find the VSI(s) that requested a re-init */
		dev_info(&pf->pdev->dev,
			 "VSI reinit requested\n");
		for (v = 0; v < pf->num_alloc_vsi; v++) {
			struct i40e_vsi *vsi = pf->vsi[v];

			if (vsi != NULL &&
			    test_bit(__I40E_REINIT_REQUESTED, &vsi->state)) {
				i40e_vsi_reinit_locked(pf->vsi[v]);
				clear_bit(__I40E_REINIT_REQUESTED, &vsi->state);
			}
		}
	} else if (reset_flags & BIT_ULL(__I40E_DOWN_REQUESTED)) {
		int v;

		/* Find the VSI(s) that needs to be brought down */
		dev_info(&pf->pdev->dev, "VSI down requested\n");
		for (v = 0; v < pf->num_alloc_vsi; v++) {
			struct i40e_vsi *vsi = pf->vsi[v];

			if (vsi != NULL &&
			    test_bit(__I40E_DOWN_REQUESTED, &vsi->state)) {
				set_bit(__I40E_DOWN, &vsi->state);
				i40e_down(vsi);
				clear_bit(__I40E_DOWN_REQUESTED, &vsi->state);
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

	set_bit(__I40E_PORT_TX_SUSPENDED, &pf->state);
	/* Reconfiguration needed quiesce all VSIs */
	i40e_pf_quiesce_all_vsi(pf);

	/* Changes in configuration update VEB/VSI */
	i40e_dcb_reconfigure(pf);

	ret = i40e_resume_port_tx(pf);

	clear_bit(__I40E_PORT_TX_SUSPENDED, &pf->state);
	/* In case of error no point in resuming VSIs */
	if (ret)
		goto exit;

	/* Wait for the PF's queues to be disabled */
	ret = i40e_pf_wait_queues_disabled(pf);
	if (ret) {
		/* Schedule PF reset to recover */
		set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
		i40e_service_event_schedule(pf);
	} else {
		i40e_pf_unquiesce_all_vsi(pf);
		/* Notify the client for the DCB changes */
		i40e_notify_client_of_l2_param_changes(pf->vsi[pf->lan_vsi]);
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
	i40e_do_reset(pf, reset_flags);
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
 * i40e_service_event_complete - Finish up the service event
 * @pf: board private structure
 **/
static void i40e_service_event_complete(struct i40e_pf *pf)
{
	WARN_ON(!test_bit(__I40E_SERVICE_SCHED, &pf->state));

	/* flush memory to make sure state is correct before next watchog */
	smp_mb__before_atomic();
	clear_bit(__I40E_SERVICE_SCHED, &pf->state);
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
 * i40e_fdir_check_and_reenable - Function to reenabe FD ATR or SB if disabled
 * @pf: board private structure
 **/
void i40e_fdir_check_and_reenable(struct i40e_pf *pf)
{
	struct i40e_fdir_filter *filter;
	u32 fcnt_prog, fcnt_avail;
	struct hlist_node *node;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, &pf->state))
		return;

	/* Check if, FD SB or ATR was auto disabled and if there is enough room
	 * to re-enable
	 */
	fcnt_prog = i40e_get_global_fd_count(pf);
	fcnt_avail = pf->fdir_pf_filter_count;
	if ((fcnt_prog < (fcnt_avail - I40E_FDIR_BUFFER_HEAD_ROOM)) ||
	    (pf->fd_add_err == 0) ||
	    (i40e_get_current_atr_cnt(pf) < pf->fd_atr_cnt)) {
		if ((pf->flags & I40E_FLAG_FD_SB_ENABLED) &&
		    (pf->auto_disable_flags & I40E_FLAG_FD_SB_ENABLED)) {
			pf->auto_disable_flags &= ~I40E_FLAG_FD_SB_ENABLED;
			if (I40E_DEBUG_FD & pf->hw.debug_mask)
				dev_info(&pf->pdev->dev, "FD Sideband/ntuple is being enabled since we have space in the table now\n");
		}
	}
	/* Wait for some more space to be available to turn on ATR */
	if (fcnt_prog < (fcnt_avail - I40E_FDIR_BUFFER_HEAD_ROOM * 2)) {
		if ((pf->flags & I40E_FLAG_FD_ATR_ENABLED) &&
		    (pf->auto_disable_flags & I40E_FLAG_FD_ATR_ENABLED)) {
			pf->auto_disable_flags &= ~I40E_FLAG_FD_ATR_ENABLED;
			if (I40E_DEBUG_FD & pf->hw.debug_mask)
				dev_info(&pf->pdev->dev, "ATR is being enabled since we have space in the table now\n");
		}
	}

	/* if hw had a problem adding a filter, delete it */
	if (pf->fd_inv > 0) {
		hlist_for_each_entry_safe(filter, node,
					  &pf->fdir_filter_list, fdir_node) {
			if (filter->fd_id == pf->fd_inv) {
				hlist_del(&filter->fdir_node);
				kfree(filter);
				pf->fdir_pf_active_filters--;
			}
		}
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

	if (!(pf->flags & (I40E_FLAG_FD_SB_ENABLED | I40E_FLAG_FD_ATR_ENABLED)))
		return;

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
	pf->flags &= ~I40E_FLAG_FD_ATR_ENABLED;
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
		if (!disable_atr)
			pf->flags |= I40E_FLAG_FD_ATR_ENABLED;
		clear_bit(__I40E_FD_FLUSH_REQUESTED, &pf->state);
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

/* We can see up to 256 filter programming desc in transit if the filters are
 * being applied really fast; before we see the first
 * filter miss error on Rx queue 0. Accumulating enough error messages before
 * reacting will make sure we don't cause flush too often.
 */
#define I40E_MAX_FD_PROGRAM_ERROR 256

/**
 * i40e_fdir_reinit_subtask - Worker thread to reinit FDIR filter table
 * @pf: board private structure
 **/
static void i40e_fdir_reinit_subtask(struct i40e_pf *pf)
{

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, &pf->state))
		return;

	if (!(pf->flags & (I40E_FLAG_FD_SB_ENABLED | I40E_FLAG_FD_ATR_ENABLED)))
		return;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, &pf->state))
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
	if (!vsi || test_bit(__I40E_DOWN, &vsi->state))
		return;

	switch (vsi->type) {
	case I40E_VSI_MAIN:
#ifdef I40E_FCOE
	case I40E_VSI_FCOE:
#endif
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

	/* save off old link status information */
	pf->hw.phy.link_info_old = pf->hw.phy.link_info;

	/* set this to force the get_link_status call to refresh state */
	pf->hw.phy.get_link_info = true;

	old_link = (pf->hw.phy.link_info_old.link_info & I40E_AQ_LINK_UP);

	status = i40e_get_link_status(&pf->hw, &new_link);
	if (status) {
		dev_dbg(&pf->pdev->dev, "couldn't get link state, status: %d\n",
			status);
		return;
	}

	old_link_speed = pf->hw.phy.link_info_old.link_speed;
	new_link_speed = pf->hw.phy.link_info.link_speed;

	if (new_link == old_link &&
	    new_link_speed == old_link_speed &&
	    (test_bit(__I40E_DOWN, &vsi->state) ||
	     new_link == netif_carrier_ok(vsi->netdev)))
		return;

	if (!test_bit(__I40E_DOWN, &vsi->state))
		i40e_print_link_message(vsi, new_link);

	/* Notify the base of the switch tree connected to
	 * the link.  Floating VEBs are not notified.
	 */
	if (pf->lan_veb != I40E_NO_VEB && pf->veb[pf->lan_veb])
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
	if (test_bit(__I40E_DOWN, &pf->state) ||
	    test_bit(__I40E_CONFIG_BUSY, &pf->state))
		return;

	/* make sure we don't do these things too often */
	if (time_before(jiffies, (pf->service_timer_previous +
				  pf->service_timer_period)))
		return;
	pf->service_timer_previous = jiffies;

	if (pf->flags & I40E_FLAG_LINK_POLLING_ENABLED)
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

	i40e_ptp_rx_hang(pf->vsi[pf->lan_vsi]);
}

/**
 * i40e_reset_subtask - Set up for resetting the device and driver
 * @pf: board private structure
 **/
static void i40e_reset_subtask(struct i40e_pf *pf)
{
	u32 reset_flags = 0;

	rtnl_lock();
	if (test_bit(__I40E_REINIT_REQUESTED, &pf->state)) {
		reset_flags |= BIT(__I40E_REINIT_REQUESTED);
		clear_bit(__I40E_REINIT_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_PF_RESET_REQUESTED, &pf->state)) {
		reset_flags |= BIT(__I40E_PF_RESET_REQUESTED);
		clear_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_CORE_RESET_REQUESTED, &pf->state)) {
		reset_flags |= BIT(__I40E_CORE_RESET_REQUESTED);
		clear_bit(__I40E_CORE_RESET_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state)) {
		reset_flags |= BIT(__I40E_GLOBAL_RESET_REQUESTED);
		clear_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_DOWN_REQUESTED, &pf->state)) {
		reset_flags |= BIT(__I40E_DOWN_REQUESTED);
		clear_bit(__I40E_DOWN_REQUESTED, &pf->state);
	}

	/* If there's a recovery already waiting, it takes
	 * precedence before starting a new reset sequence.
	 */
	if (test_bit(__I40E_RESET_INTR_RECEIVED, &pf->state)) {
		i40e_handle_reset_warning(pf);
		goto unlock;
	}

	/* If we're already down or resetting, just bail */
	if (reset_flags &&
	    !test_bit(__I40E_DOWN, &pf->state) &&
	    !test_bit(__I40E_CONFIG_BUSY, &pf->state))
		i40e_do_reset(pf, reset_flags);

unlock:
	rtnl_unlock();
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

	/* check for unqualified module, if link is down */
	if ((status->link_info & I40E_AQ_MEDIA_AVAILABLE) &&
	    (!(status->an_info & I40E_AQ_QUALIFIED_MODULE)) &&
	    (!(status->link_info & I40E_AQ_LINK_UP)))
		dev_err(&pf->pdev->dev,
			"The driver failed to link because an unqualified module was detected.\n");
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
	if (test_bit(__I40E_RESET_FAILED, &pf->state))
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
	} while (pending && (i++ < pf->adminq_work_limit));

	clear_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state);
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
			set_bit(__I40E_BAD_EEPROM, &pf->state);
		}
	}

	if (!err && test_bit(__I40E_BAD_EEPROM, &pf->state)) {
		dev_info(&pf->pdev->dev, "eeprom check passed, Tx/Rx traffic enabled\n");
		clear_bit(__I40E_BAD_EEPROM, &pf->state);
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
 **/
static int i40e_get_capabilities(struct i40e_pf *pf)
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
					    &data_size,
					    i40e_aqc_opc_list_func_capabilities,
					    NULL);
		/* data loaded, buffer no longer needed */
		kfree(cap_buf);

		if (pf->hw.aq.asq_last_status == I40E_AQ_RC_ENOMEM) {
			/* retry with a larger buffer */
			buf_len = data_size;
		} else if (pf->hw.aq.asq_last_status != I40E_AQ_RC_OK) {
			dev_info(&pf->pdev->dev,
				 "capability discovery failed, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, err),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			return -ENODEV;
		}
	} while (err);

	if (pf->hw.debug_mask & I40E_DEBUG_USER)
		dev_info(&pf->pdev->dev,
			 "pf=%d, num_vfs=%d, msix_pf=%d, msix_vf=%d, fd_g=%d, fd_b=%d, pf_max_q=%d num_vsi=%d\n",
			 pf->hw.pf_id, pf->hw.func_caps.num_vfs,
			 pf->hw.func_caps.num_msix_vectors,
			 pf->hw.func_caps.num_msix_vectors_vf,
			 pf->hw.func_caps.fd_filters_guaranteed,
			 pf->hw.func_caps.fd_filters_best_effort,
			 pf->hw.func_caps.num_tx_qp,
			 pf->hw.func_caps.num_vsis);

#define DEF_NUM_VSI (1 + (pf->hw.func_caps.fcoe ? 1 : 0) \
		       + pf->hw.func_caps.num_vfs)
	if (pf->hw.revision_id == 0 && (DEF_NUM_VSI > pf->hw.func_caps.num_vsis)) {
		dev_info(&pf->pdev->dev,
			 "got num_vsis %d, setting num_vsis to %d\n",
			 pf->hw.func_caps.num_vsis, DEF_NUM_VSI);
		pf->hw.func_caps.num_vsis = DEF_NUM_VSI;
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
	int i;

	/* quick workaround for an NVM issue that leaves a critical register
	 * uninitialized
	 */
	if (!rd32(&pf->hw, I40E_GLQF_HKEY(0))) {
		static const u32 hkey[] = {
			0xe640d33f, 0xcdfe98ab, 0x73fa7161, 0x0d7a7d36,
			0xeacb7d61, 0xaa4f05b6, 0x9c5c89ed, 0xfc425ddb,
			0xa4654832, 0xfc7461d4, 0x8f827619, 0xf5c63c21,
			0x95b3a76d};

		for (i = 0; i <= I40E_GLQF_HKEY_MAX_INDEX; i++)
			wr32(&pf->hw, I40E_GLQF_HKEY(i), hkey[i]);
	}

	if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
		return;

	/* find existing VSI and see if it needs configuring */
	vsi = NULL;
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR) {
			vsi = pf->vsi[i];
			break;
		}
	}

	/* create a new VSI if none exists */
	if (!vsi) {
		vsi = i40e_vsi_setup(pf, I40E_VSI_FDIR,
				     pf->vsi[pf->lan_vsi]->seid, 0);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "Couldn't create FDir VSI\n");
			pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
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
	int i;

	i40e_fdir_filter_exit(pf);
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR) {
			i40e_vsi_release(pf->vsi[i]);
			break;
		}
	}
}

/**
 * i40e_prep_for_reset - prep for the core to reset
 * @pf: board private structure
 *
 * Close up the VFs and other things in prep for PF Reset.
  **/
static void i40e_prep_for_reset(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret = 0;
	u32 v;

	clear_bit(__I40E_RESET_INTR_RECEIVED, &pf->state);
	if (test_and_set_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state))
		return;
	if (i40e_check_asq_alive(&pf->hw))
		i40e_vc_notify_reset(pf);

	dev_dbg(&pf->pdev->dev, "Tearing down internal switch for reset\n");

	/* quiesce the VSIs and their queues that are not already DOWN */
	i40e_pf_quiesce_all_vsi(pf);

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
}

/**
 * i40e_send_version - update firmware with driver version
 * @pf: PF struct
 */
static void i40e_send_version(struct i40e_pf *pf)
{
	struct i40e_driver_version dv;

	dv.major_version = DRV_VERSION_MAJOR;
	dv.minor_version = DRV_VERSION_MINOR;
	dv.build_version = DRV_VERSION_BUILD;
	dv.subbuild_version = 0;
	strlcpy(dv.driver_string, DRV_VERSION, sizeof(dv.driver_string));
	i40e_aq_send_driver_version(&pf->hw, &dv, NULL);
}

/**
 * i40e_reset_and_rebuild - reset and rebuild using a saved config
 * @pf: board private structure
 * @reinit: if the Main VSI needs to re-initialized.
 **/
static void i40e_reset_and_rebuild(struct i40e_pf *pf, bool reinit)
{
	struct i40e_hw *hw = &pf->hw;
	u8 set_fc_aq_fail = 0;
	i40e_status ret;
	u32 val;
	u32 v;

	/* Now we wait for GRST to settle out.
	 * We don't have to delete the VEBs or VSIs from the hw switch
	 * because the reset will make them disappear.
	 */
	ret = i40e_pf_reset(hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "PF reset failed, %d\n", ret);
		set_bit(__I40E_RESET_FAILED, &pf->state);
		goto clear_recovery;
	}
	pf->pfr_count++;

	if (test_bit(__I40E_DOWN, &pf->state))
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

	/* re-verify the eeprom if we just had an EMP reset */
	if (test_and_clear_bit(__I40E_EMP_RESET_INTR_RECEIVED, &pf->state))
		i40e_verify_eeprom(pf);

	i40e_clear_pxe_mode(hw);
	ret = i40e_get_capabilities(pf);
	if (ret)
		goto end_core_reset;

	ret = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp,
				pf->fcoe_hmc_cntx_num, pf->fcoe_hmc_filt_num);
	if (ret) {
		dev_info(&pf->pdev->dev, "init_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}
	ret = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (ret) {
		dev_info(&pf->pdev->dev, "configure_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}

#ifdef CONFIG_I40E_DCB
	ret = i40e_init_pf_dcb(pf);
	if (ret) {
		dev_info(&pf->pdev->dev, "DCB init failed %d, disabled\n", ret);
		pf->flags &= ~I40E_FLAG_DCB_CAPABLE;
		/* Continue without DCB enabled */
	}
#endif /* CONFIG_I40E_DCB */
#ifdef I40E_FCOE
	i40e_init_pf_fcoe(pf);

#endif
	/* do basic switch setup */
	ret = i40e_setup_pf_switch(pf, reinit);
	if (ret)
		goto end_core_reset;

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

	/* make sure our flow control settings are restored */
	ret = i40e_set_fc(&pf->hw, &set_fc_aq_fail, true);
	if (ret)
		dev_dbg(&pf->pdev->dev, "setting flow control: ret = %s last_status = %s\n",
			i40e_stat_str(&pf->hw, ret),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

	/* Rebuild the VSIs and VEBs that existed before reset.
	 * They are still in our local switch element arrays, so only
	 * need to rebuild the switch model in the HW.
	 *
	 * If there were VEBs but the reconstitution failed, we'll try
	 * try to recover minimal use by getting the basic PF VSI working.
	 */
	if (pf->vsi[pf->lan_vsi]->uplink_seid != pf->mac_seid) {
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
					pf->vsi[pf->lan_vsi]->uplink_seid
								= pf->mac_seid;
					break;
				} else if (pf->veb[v]->uplink_seid == 0) {
					dev_info(&pf->pdev->dev,
						 "rebuild of orphan VEB failed: %d\n",
						 ret);
				}
			}
		}
	}

	if (pf->vsi[pf->lan_vsi]->uplink_seid == pf->mac_seid) {
		dev_dbg(&pf->pdev->dev, "attempting to rebuild PF VSI\n");
		/* no VEB, so rebuild only the Main VSI */
		ret = i40e_add_vsi(pf->vsi[pf->lan_vsi]);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "rebuild of Main VSI failed: %d\n", ret);
			goto end_core_reset;
		}
	}

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

	if (pf->flags & I40E_FLAG_RESTART_AUTONEG) {
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

	if (pf->num_alloc_vfs) {
		for (v = 0; v < pf->num_alloc_vfs; v++)
			i40e_reset_vf(&pf->vf[v], true);
	}

	/* tell the firmware that we're starting */
	i40e_send_version(pf);

end_core_reset:
	clear_bit(__I40E_RESET_FAILED, &pf->state);
clear_recovery:
	clear_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state);
}

/**
 * i40e_handle_reset_warning - prep for the PF to reset, reset and rebuild
 * @pf: board private structure
 *
 * Close up the VFs and other things in prep for a Core Reset,
 * then get ready to rebuild the world.
 **/
static void i40e_handle_reset_warning(struct i40e_pf *pf)
{
	i40e_prep_for_reset(pf);
	i40e_reset_and_rebuild(pf, false);
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
	bool pf_mdd_detected = false;
	struct i40e_vf *vf;
	u32 reg;
	int i;

	if (!test_bit(__I40E_MDD_EVENT_PENDING, &pf->state))
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
			dev_info(&pf->pdev->dev, "TX driver issue detected, PF reset issued\n");
			pf_mdd_detected = true;
		}
		reg = rd32(hw, I40E_PF_MDET_RX);
		if (reg & I40E_PF_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_PF_MDET_RX, 0xFFFF);
			dev_info(&pf->pdev->dev, "RX driver issue detected, PF reset issued\n");
			pf_mdd_detected = true;
		}
		/* Queue belongs to the PF, initiate a reset */
		if (pf_mdd_detected) {
			set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
			i40e_service_event_schedule(pf);
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
		}

		reg = rd32(hw, I40E_VP_MDET_RX(i));
		if (reg & I40E_VP_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_RX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "RX driver issue detected on VF %d\n",
				 i);
		}

		if (vf->num_mdd_events > I40E_DEFAULT_NUM_MDD_EVENTS_ALLOWED) {
			dev_info(&pf->pdev->dev,
				 "Too many MDD events on VF %d, disabled\n", i);
			dev_info(&pf->pdev->dev,
				 "Use PF Control I/F to re-enable the VF\n");
			set_bit(I40E_VF_STAT_DISABLED, &vf->vf_states);
		}
	}

	/* re-enable mdd interrupt cause */
	clear_bit(__I40E_MDD_EVENT_PENDING, &pf->state);
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |=  I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);
	i40e_flush(hw);
}

/**
 * i40e_sync_udp_filters_subtask - Sync the VSI filter list with HW
 * @pf: board private structure
 **/
static void i40e_sync_udp_filters_subtask(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;
	__be16 port;
	int i;

	if (!(pf->flags & I40E_FLAG_UDP_FILTER_SYNC))
		return;

	pf->flags &= ~I40E_FLAG_UDP_FILTER_SYNC;

	for (i = 0; i < I40E_MAX_PF_UDP_OFFLOAD_PORTS; i++) {
		if (pf->pending_udp_bitmap & BIT_ULL(i)) {
			pf->pending_udp_bitmap &= ~BIT_ULL(i);
			port = pf->udp_ports[i].index;
			if (port)
				ret = i40e_aq_add_udp_tunnel(hw, ntohs(port),
						     pf->udp_ports[i].type,
						     NULL, NULL);
			else
				ret = i40e_aq_del_udp_tunnel(hw, i, NULL);

			if (ret) {
				dev_dbg(&pf->pdev->dev,
					"%s %s port %d, index %d failed, err %s aq_err %s\n",
					pf->udp_ports[i].type ? "vxlan" : "geneve",
					port ? "add" : "delete",
					ntohs(port), i,
					i40e_stat_str(&pf->hw, ret),
					i40e_aq_str(&pf->hw,
						    pf->hw.aq.asq_last_status));
				pf->udp_ports[i].index = 0;
			}
		}
	}
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
	if (test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state)) {
		i40e_service_event_complete(pf);
		return;
	}

	i40e_detect_recover_hung(pf);
	i40e_sync_filters_subtask(pf);
	i40e_reset_subtask(pf);
	i40e_handle_mdd_event(pf);
	i40e_vc_process_vflr_event(pf);
	i40e_watchdog_subtask(pf);
	i40e_fdir_reinit_subtask(pf);
	i40e_client_subtask(pf);
	i40e_sync_filters_subtask(pf);
	i40e_sync_udp_filters_subtask(pf);
	i40e_clean_adminq_subtask(pf);

	i40e_service_event_complete(pf);

	/* If the tasks have taken longer than one timer cycle or there
	 * is more work to be done, reschedule the service task now
	 * rather than wait for the timer to tick again.
	 */
	if (time_after(jiffies, (start_time + pf->service_timer_period)) ||
	    test_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state)		 ||
	    test_bit(__I40E_MDD_EVENT_PENDING, &pf->state)		 ||
	    test_bit(__I40E_VFLR_EVENT_PENDING, &pf->state))
		i40e_service_event_schedule(pf);
}

/**
 * i40e_service_timer - timer callback
 * @data: pointer to PF struct
 **/
static void i40e_service_timer(unsigned long data)
{
	struct i40e_pf *pf = (struct i40e_pf *)data;

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
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			vsi->num_q_vectors = pf->num_lan_msix;
		else
			vsi->num_q_vectors = 1;

		break;

	case I40E_VSI_FDIR:
		vsi->alloc_queue_pairs = 1;
		vsi->num_desc = ALIGN(I40E_FDIR_RING_COUNT,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_fdsb_msix;
		break;

	case I40E_VSI_VMDQ2:
		vsi->alloc_queue_pairs = pf->num_vmdq_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_vmdq_msix;
		break;

	case I40E_VSI_SRIOV:
		vsi->alloc_queue_pairs = pf->num_vf_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		break;

#ifdef I40E_FCOE
	case I40E_VSI_FCOE:
		vsi->alloc_queue_pairs = pf->num_fcoe_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_fcoe_msix;
		break;

#endif /* I40E_FCOE */
	default:
		WARN_ON(1);
		return -ENODATA;
	}

	return 0;
}

/**
 * i40e_vsi_alloc_arrays - Allocate queue and vector pointer arrays for the vsi
 * @type: VSI pointer
 * @alloc_qvectors: a bool to specify if q_vectors need to be allocated.
 *
 * On error: returns error code (negative)
 * On success: returns 0
 **/
static int i40e_vsi_alloc_arrays(struct i40e_vsi *vsi, bool alloc_qvectors)
{
	int size;
	int ret = 0;

	/* allocate memory for both Tx and Rx ring pointers */
	size = sizeof(struct i40e_ring *) * vsi->alloc_queue_pairs * 2;
	vsi->tx_rings = kzalloc(size, GFP_KERNEL);
	if (!vsi->tx_rings)
		return -ENOMEM;
	vsi->rx_rings = &vsi->tx_rings[vsi->alloc_queue_pairs];

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
	set_bit(__I40E_DOWN, &vsi->state);
	vsi->flags = 0;
	vsi->idx = vsi_idx;
	vsi->int_rate_limit = 0;
	vsi->rss_table_size = (vsi->type == I40E_VSI_MAIN) ?
				pf->rss_table_size : 64;
	vsi->netdev_registered = false;
	vsi->work_limit = I40E_DEFAULT_IRQ_WORK;
	INIT_LIST_HEAD(&vsi->mac_filter_list);
	vsi->irqs_ready = false;

	ret = i40e_set_num_rings_in_vsi(vsi);
	if (ret)
		goto err_rings;

	ret = i40e_vsi_alloc_arrays(vsi, true);
	if (ret)
		goto err_rings;

	/* Setup default MSIX irq handler for VSI */
	i40e_vsi_setup_irqhandler(vsi, i40e_msix_clean_rings);

	/* Initialize VSI lock */
	spin_lock_init(&vsi->mac_filter_list_lock);
	pf->vsi[vsi_idx] = vsi;
	ret = vsi_idx;
	goto unlock_pf;

err_rings:
	pf->next_vsi = i - 1;
	kfree(vsi);
unlock_pf:
	mutex_unlock(&pf->switch_mutex);
	return ret;
}

/**
 * i40e_vsi_free_arrays - Free queue and vector pointer arrays for the VSI
 * @type: VSI pointer
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
		dev_err(&pf->pdev->dev, "pf->vsi[%d] is NULL, just free vsi[%d](%p,type %d)\n",
			vsi->idx, vsi->idx, vsi, vsi->type);
		goto unlock_vsi;
	}

	if (pf->vsi[vsi->idx] != vsi) {
		dev_err(&pf->pdev->dev,
			"pf->vsi[%d](%p, type %d) != vsi[%d](%p,type %d): no free!\n",
			pf->vsi[vsi->idx]->idx,
			pf->vsi[vsi->idx],
			pf->vsi[vsi->idx]->type,
			vsi->idx, vsi, vsi->type);
		goto unlock_vsi;
	}

	/* updates the PF for this cleared vsi */
	i40e_put_lump(pf->qp_pile, vsi->base_queue, vsi->idx);
	i40e_put_lump(pf->irq_pile, vsi->base_vector, vsi->idx);

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
			vsi->tx_rings[i] = NULL;
			vsi->rx_rings[i] = NULL;
		}
	}
}

/**
 * i40e_alloc_rings - Allocates the Rx and Tx rings for the provided VSI
 * @vsi: the VSI being configured
 **/
static int i40e_alloc_rings(struct i40e_vsi *vsi)
{
	struct i40e_ring *tx_ring, *rx_ring;
	struct i40e_pf *pf = vsi->back;
	int i;

	/* Set basic values in the rings to be used later during open() */
	for (i = 0; i < vsi->alloc_queue_pairs; i++) {
		/* allocate space for both Tx and Rx in one shot */
		tx_ring = kzalloc(sizeof(struct i40e_ring) * 2, GFP_KERNEL);
		if (!tx_ring)
			goto err_out;

		tx_ring->queue_index = i;
		tx_ring->reg_idx = vsi->base_queue + i;
		tx_ring->ring_active = false;
		tx_ring->vsi = vsi;
		tx_ring->netdev = vsi->netdev;
		tx_ring->dev = &pf->pdev->dev;
		tx_ring->count = vsi->num_desc;
		tx_ring->size = 0;
		tx_ring->dcb_tc = 0;
		if (vsi->back->flags & I40E_FLAG_WB_ON_ITR_CAPABLE)
			tx_ring->flags = I40E_TXR_FLAGS_WB_ON_ITR;
		tx_ring->tx_itr_setting = pf->tx_itr_default;
		vsi->tx_rings[i] = tx_ring;

		rx_ring = &tx_ring[1];
		rx_ring->queue_index = i;
		rx_ring->reg_idx = vsi->base_queue + i;
		rx_ring->ring_active = false;
		rx_ring->vsi = vsi;
		rx_ring->netdev = vsi->netdev;
		rx_ring->dev = &pf->pdev->dev;
		rx_ring->count = vsi->num_desc;
		rx_ring->size = 0;
		rx_ring->dcb_tc = 0;
		rx_ring->rx_itr_setting = pf->rx_itr_default;
		vsi->rx_rings[i] = rx_ring;
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
#ifdef I40E_FCOE
	 *   - The number of FCOE qps.
#endif
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

	/* reserve vectors for the main PF traffic queues */
	pf->num_lan_msix = min_t(int, num_online_cpus(), vectors_left);
	vectors_left -= pf->num_lan_msix;
	v_budget += pf->num_lan_msix;

	/* reserve one vector for sideband flow director */
	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		if (vectors_left) {
			pf->num_fdsb_msix = 1;
			v_budget++;
			vectors_left--;
		} else {
			pf->num_fdsb_msix = 0;
			pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
		}
	}

#ifdef I40E_FCOE
	/* can we reserve enough for FCoE? */
	if (pf->flags & I40E_FLAG_FCOE_ENABLED) {
		if (!vectors_left)
			pf->num_fcoe_msix = 0;
		else if (vectors_left >= pf->num_fcoe_qps)
			pf->num_fcoe_msix = pf->num_fcoe_qps;
		else
			pf->num_fcoe_msix = 1;
		v_budget += pf->num_fcoe_msix;
		vectors_left -= pf->num_fcoe_msix;
	}

#endif
	/* can we reserve enough for iWARP? */
	if (pf->flags & I40E_FLAG_IWARP_ENABLED) {
		if (!vectors_left)
			pf->num_iwarp_msix = 0;
		else if (vectors_left < pf->num_iwarp_msix)
			pf->num_iwarp_msix = 1;
		v_budget += pf->num_iwarp_msix;
		vectors_left -= pf->num_iwarp_msix;
	}

	/* any vectors left over go for VMDq support */
	if (pf->flags & I40E_FLAG_VMDQ_ENABLED) {
		int vmdq_vecs_wanted = pf->num_vmdq_vsis * pf->num_vmdq_qps;
		int vmdq_vecs = min_t(int, vectors_left, vmdq_vecs_wanted);

		/* if we're short on vectors for what's desired, we limit
		 * the queues per vmdq.  If this is still more than are
		 * available, the user will need to change the number of
		 * queues/vectors used by the PF later with the ethtool
		 * channels command
		 */
		if (vmdq_vecs < vmdq_vecs_wanted)
			pf->num_vmdq_qps = 1;
		pf->num_vmdq_msix = pf->num_vmdq_qps;

		v_budget += vmdq_vecs;
		vectors_left -= vmdq_vecs;
	}

	pf->msix_entries = kcalloc(v_budget, sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!pf->msix_entries)
		return -ENOMEM;

	for (i = 0; i < v_budget; i++)
		pf->msix_entries[i].entry = i;
	v_actual = i40e_reserve_msix_vectors(pf, v_budget);

	if (v_actual != v_budget) {
		/* If we have limited resources, we will start with no vectors
		 * for the special features and then allocate vectors to some
		 * of these features based on the policy and at the end disable
		 * the features that did not get any vectors.
		 */
		iwarp_requested = pf->num_iwarp_msix;
		pf->num_iwarp_msix = 0;
#ifdef I40E_FCOE
		pf->num_fcoe_qps = 0;
		pf->num_fcoe_msix = 0;
#endif
		pf->num_vmdq_msix = 0;
	}

	if (v_actual < I40E_MIN_MSIX) {
		pf->flags &= ~I40E_FLAG_MSIX_ENABLED;
		kfree(pf->msix_entries);
		pf->msix_entries = NULL;
		return -ENODEV;

	} else if (v_actual == I40E_MIN_MSIX) {
		/* Adjust for minimal MSIX use */
		pf->num_vmdq_vsis = 0;
		pf->num_vmdq_qps = 0;
		pf->num_lan_qps = 1;
		pf->num_lan_msix = 1;

	} else if (v_actual != v_budget) {
		int vec;

		/* reserve the misc vector */
		vec = v_actual - 1;

		/* Scale vector usage down */
		pf->num_vmdq_msix = 1;    /* force VMDqs to only one vector */
		pf->num_vmdq_vsis = 1;
		pf->num_vmdq_qps = 1;
		pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;

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
#ifdef I40E_FCOE
			/* give one vector to FCoE */
			if (pf->flags & I40E_FLAG_FCOE_ENABLED) {
				pf->num_lan_msix = 1;
				pf->num_fcoe_msix = 1;
			}
#endif
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
			pf->num_lan_msix = min_t(int,
			       (vec - (pf->num_iwarp_msix + pf->num_vmdq_vsis)),
							      pf->num_lan_msix);
#ifdef I40E_FCOE
			/* give one vector to FCoE */
			if (pf->flags & I40E_FLAG_FCOE_ENABLED) {
				pf->num_fcoe_msix = 1;
				vec--;
			}
#endif
			break;
		}
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
#ifdef I40E_FCOE

	if ((pf->flags & I40E_FLAG_FCOE_ENABLED) && (pf->num_fcoe_msix == 0)) {
		dev_info(&pf->pdev->dev, "FCOE disabled, not enough MSI-X vectors\n");
		pf->flags &= ~I40E_FLAG_FCOE_ENABLED;
	}
#endif
	return v_actual;
}

/**
 * i40e_vsi_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the vsi struct
 * @cpu: cpu to be used on affinity_mask
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int i40e_vsi_alloc_q_vector(struct i40e_vsi *vsi, int v_idx, int cpu)
{
	struct i40e_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = kzalloc(sizeof(struct i40e_q_vector), GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
	cpumask_set_cpu(cpu, &q_vector->affinity_mask);

	if (vsi->netdev)
		netif_napi_add(vsi->netdev, &q_vector->napi,
			       i40e_napi_poll, NAPI_POLL_WEIGHT);

	q_vector->rx.latency_range = I40E_LOW_LATENCY;
	q_vector->tx.latency_range = I40E_LOW_LATENCY;

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
	int err, v_idx, num_q_vectors, current_cpu;

	/* if not MSIX, give the one vector only to the LAN VSI */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		num_q_vectors = vsi->num_q_vectors;
	else if (vsi == pf->vsi[pf->lan_vsi])
		num_q_vectors = 1;
	else
		return -EINVAL;

	current_cpu = cpumask_first(cpu_online_mask);

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		err = i40e_vsi_alloc_q_vector(vsi, v_idx, current_cpu);
		if (err)
			goto err_out;
		current_cpu = cpumask_next(current_cpu, cpu_online_mask);
		if (unlikely(current_cpu >= nr_cpu_ids))
			current_cpu = cpumask_first(cpu_online_mask);
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
#ifdef I40E_FCOE
				       I40E_FLAG_FCOE_ENABLED	|
#endif
				       I40E_FLAG_RSS_ENABLED	|
				       I40E_FLAG_DCB_CAPABLE	|
				       I40E_FLAG_SRIOV_ENABLED	|
				       I40E_FLAG_FD_SB_ENABLED	|
				       I40E_FLAG_FD_ATR_ENABLED	|
				       I40E_FLAG_VMDQ_ENABLED);

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
	if (!pf->irq_pile) {
		dev_err(&pf->pdev->dev, "error allocating irq_pile memory\n");
		return -ENOMEM;
	}
	pf->irq_pile->num_entries = vectors;
	pf->irq_pile->search_hint = 0;

	/* track first vector for misc interrupts, ignore return */
	(void)i40e_get_lump(pf, pf->irq_pile, 1, I40E_PILE_VALID_BIT - 1);

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

	/* Only request the irq if this is the first time through, and
	 * not when we're rebuilding after a Reset
	 */
	if (!test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state)) {
		err = request_irq(pf->msix_entries[0].vector,
				  i40e_intr, 0, pf->int_name, pf);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "request_irq for %s failed: %d\n",
				 pf->int_name, err);
			return -EFAULT;
		}
	}

	i40e_enable_misc_int_causes(pf);

	/* associate no queues to the misc vector */
	wr32(hw, I40E_PFINT_LNKLST0, I40E_QUEUE_END_OF_LIST);
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), I40E_ITR_8K);

	i40e_flush(hw);

	i40e_irq_dynamic_enable_icr0(pf, true);

	return err;
}

/**
 * i40e_config_rss_aq - Prepare for RSS using AQ commands
 * @vsi: vsi structure
 * @seed: RSS hash seed
 **/
static int i40e_config_rss_aq(struct i40e_vsi *vsi, const u8 *seed,
			      u8 *lut, u16 lut_size)
{
	struct i40e_aqc_get_set_rss_key_data rss_key;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	bool pf_lut = false;
	u8 *rss_lut;
	int ret, i;

	memcpy(&rss_key, seed, sizeof(rss_key));

	rss_lut = kzalloc(pf->rss_table_size, GFP_KERNEL);
	if (!rss_lut)
		return -ENOMEM;

	/* Populate the LUT with max no. of queues in round robin fashion */
	for (i = 0; i < vsi->rss_table_size; i++)
		rss_lut[i] = i % vsi->rss_size;

	ret = i40e_aq_set_rss_key(hw, vsi->id, &rss_key);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "Cannot set RSS key, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		goto config_rss_aq_out;
	}

	if (vsi->type == I40E_VSI_MAIN)
		pf_lut = true;

	ret = i40e_aq_set_rss_lut(hw, vsi->id, pf_lut, rss_lut,
				  vsi->rss_table_size);
	if (ret)
		dev_info(&pf->pdev->dev,
			 "Cannot set RSS lut, err %s aq_err %s\n",
			 i40e_stat_str(&pf->hw, ret),
			 i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));

config_rss_aq_out:
	kfree(rss_lut);
	return ret;
}

/**
 * i40e_vsi_config_rss - Prepare for VSI(VMDq) RSS if used
 * @vsi: VSI structure
 **/
static int i40e_vsi_config_rss(struct i40e_vsi *vsi)
{
	u8 seed[I40E_HKEY_ARRAY_SIZE];
	struct i40e_pf *pf = vsi->back;
	u8 *lut;
	int ret;

	if (!(pf->flags & I40E_FLAG_RSS_AQ_CAPABLE))
		return 0;

	lut = kzalloc(vsi->rss_table_size, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;

	i40e_fill_rss_lut(pf, lut, vsi->rss_table_size, vsi->rss_size);
	netdev_rss_key_fill((void *)seed, I40E_HKEY_ARRAY_SIZE);
	vsi->rss_size = min_t(int, pf->alloc_rss_size, vsi->num_queue_pairs);
	ret = i40e_config_rss_aq(vsi, seed, lut, vsi->rss_table_size);
	kfree(lut);

	return ret;
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
		bool pf_lut = vsi->type == I40E_VSI_MAIN ? true : false;

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
				i40e_write_rx_ctl(hw, I40E_PFQF_HKEY(i),
						  seed_dw[i]);
		} else if (vsi->type == I40E_VSI_SRIOV) {
			for (i = 0; i <= I40E_VFQF_HKEY1_MAX_INDEX; i++)
				i40e_write_rx_ctl(hw,
						  I40E_VFQF_HKEY1(i, vf_id),
						  seed_dw[i]);
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
				i40e_write_rx_ctl(hw,
						  I40E_VFQF_HLUT1(i, vf_id),
						  lut_dw[i]);
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

	if (pf->flags & I40E_FLAG_RSS_AQ_CAPABLE)
		return i40e_config_rss_aq(vsi, seed, lut, lut_size);
	else
		return i40e_config_rss_reg(vsi, seed, lut, lut_size);
}

/**
 * i40e_get_rss - Get RSS keys and lut
 * @vsi: Pointer to VSI structure
 * @seed: Buffer to store the keys
 * @lut: Buffer to store the lookup table entries
 * lut_size: Size of buffer to store the lookup table entries
 *
 * Returns 0 on success, negative on failure
 */
int i40e_get_rss(struct i40e_vsi *vsi, u8 *seed, u8 *lut, u16 lut_size)
{
	struct i40e_pf *pf = vsi->back;

	if (pf->flags & I40E_FLAG_RSS_AQ_CAPABLE)
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
static void i40e_fill_rss_lut(struct i40e_pf *pf, u8 *lut,
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
	if (!vsi->rss_size)
		vsi->rss_size = min_t(int, pf->alloc_rss_size,
				      vsi->num_queue_pairs);

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
 **/
int i40e_reconfig_rss_queues(struct i40e_pf *pf, int queue_count)
{
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];
	int new_rss_size;

	if (!(pf->flags & I40E_FLAG_RSS_ENABLED))
		return 0;

	new_rss_size = min_t(int, queue_count, pf->rss_size_max);

	if (queue_count != vsi->num_queue_pairs) {
		vsi->req_queue_pairs = queue_count;
		i40e_prep_for_reset(pf);

		pf->alloc_rss_size = new_rss_size;

		i40e_reset_and_rebuild(pf, true);

		/* Discard the user configured hash keys and lut, if less
		 * queues are enabled.
		 */
		if (queue_count < vsi->rss_size) {
			i40e_clear_rss_config_user(vsi);
			dev_dbg(&pf->pdev->dev,
				"discard user configured hash keys and lut\n");
		}

		/* Reset vsi->rss_size, as number of enabled queues changed */
		vsi->rss_size = min_t(int, pf->alloc_rss_size,
				      vsi->num_queue_pairs);

		i40e_pf_config_rss(pf);
	}
	dev_info(&pf->pdev->dev, "RSS count/HW max RSS count:  %d/%d\n",
		 pf->alloc_rss_size, pf->rss_size_max);
	return pf->alloc_rss_size;
}

/**
 * i40e_get_npar_bw_setting - Retrieve BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_get_npar_bw_setting(struct i40e_pf *pf)
{
	i40e_status status;
	bool min_valid, max_valid;
	u32 max_bw, min_bw;

	status = i40e_read_bw_from_alt_ram(&pf->hw, &max_bw, &min_bw,
					   &min_valid, &max_valid);

	if (!status) {
		if (min_valid)
			pf->npar_min_bw = min_bw;
		if (max_valid)
			pf->npar_max_bw = max_bw;
	}

	return status;
}

/**
 * i40e_set_npar_bw_setting - Set BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_set_npar_bw_setting(struct i40e_pf *pf)
{
	struct i40e_aqc_configure_partition_bw_data bw_data;
	i40e_status status;

	/* Set the valid bit for this PF */
	bw_data.pf_valid_bits = cpu_to_le16(BIT(pf->hw.pf_id));
	bw_data.max_bw[pf->hw.pf_id] = pf->npar_max_bw & I40E_ALT_BW_VALUE_MASK;
	bw_data.min_bw[pf->hw.pf_id] = pf->npar_min_bw & I40E_ALT_BW_VALUE_MASK;

	/* Set the new bandwidths */
	status = i40e_aq_configure_partition_bw(&pf->hw, &bw_data, NULL);

	return status;
}

/**
 * i40e_commit_npar_bw_setting - Commit BW settings for this PF partition
 * @pf: board private structure
 **/
i40e_status i40e_commit_npar_bw_setting(struct i40e_pf *pf)
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
				 &nvm_word, true, NULL);
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

	pf->msg_enable = netif_msg_init(I40E_DEFAULT_MSG_ENABLE,
				(NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK));
	if (debug != -1 && debug != I40E_DEFAULT_MSG_ENABLE) {
		if (I40E_DEBUG_USER & debug)
			pf->hw.debug_mask = debug;
		pf->msg_enable = netif_msg_init((debug & ~I40E_DEBUG_USER),
						I40E_DEFAULT_MSG_ENABLE);
	}

	/* Set default capability flags */
	pf->flags = I40E_FLAG_RX_CSUM_ENABLED |
		    I40E_FLAG_MSI_ENABLED     |
		    I40E_FLAG_MSIX_ENABLED;

	/* Set default ITR */
	pf->rx_itr_default = I40E_ITR_DYNAMIC | I40E_ITR_RX_DEF;
	pf->tx_itr_default = I40E_ITR_DYNAMIC | I40E_ITR_TX_DEF;

	/* Depending on PF configurations, it is possible that the RSS
	 * maximum might end up larger than the available queues
	 */
	pf->rss_size_max = BIT(pf->hw.func_caps.rss_table_entry_width);
	pf->alloc_rss_size = 1;
	pf->rss_table_size = pf->hw.func_caps.rss_table_size;
	pf->rss_size_max = min_t(int, pf->rss_size_max,
				 pf->hw.func_caps.num_tx_qp);
	if (pf->hw.func_caps.rss) {
		pf->flags |= I40E_FLAG_RSS_ENABLED;
		pf->alloc_rss_size = min_t(int, pf->rss_size_max,
					   num_online_cpus());
	}

	/* MFP mode enabled */
	if (pf->hw.func_caps.npar_enable || pf->hw.func_caps.flex10_enable) {
		pf->flags |= I40E_FLAG_MFP_ENABLED;
		dev_info(&pf->pdev->dev, "MFP mode Enabled\n");
		if (i40e_get_npar_bw_setting(pf))
			dev_warn(&pf->pdev->dev,
				 "Could not get NPAR bw settings\n");
		else
			dev_info(&pf->pdev->dev,
				 "Min BW = %8.8x, Max BW = %8.8x\n",
				 pf->npar_min_bw, pf->npar_max_bw);
	}

	/* FW/NVM is not yet fixed in this regard */
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

	if (i40e_is_mac_710(&pf->hw) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver < 33)) ||
	    (pf->hw.aq.fw_maj_ver < 4))) {
		pf->flags |= I40E_FLAG_RESTART_AUTONEG;
		/* No DCB support  for FW < v4.33 */
		pf->flags |= I40E_FLAG_NO_DCB_SUPPORT;
	}

	/* Disable FW LLDP if FW < v4.3 */
	if (i40e_is_mac_710(&pf->hw) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver < 3)) ||
	    (pf->hw.aq.fw_maj_ver < 4)))
		pf->flags |= I40E_FLAG_STOP_FW_LLDP;

	/* Use the FW Set LLDP MIB API if FW > v4.40 */
	if (i40e_is_mac_710(&pf->hw) &&
	    (((pf->hw.aq.fw_maj_ver == 4) && (pf->hw.aq.fw_min_ver >= 40)) ||
	    (pf->hw.aq.fw_maj_ver >= 5)))
		pf->flags |= I40E_FLAG_USE_SET_LLDP_MIB;

	if (pf->hw.func_caps.vmdq) {
		pf->num_vmdq_vsis = I40E_DEFAULT_NUM_VMDQ_VSI;
		pf->flags |= I40E_FLAG_VMDQ_ENABLED;
		pf->num_vmdq_qps = i40e_default_queues_per_vmdq(pf);
	}

	if (pf->hw.func_caps.iwarp) {
		pf->flags |= I40E_FLAG_IWARP_ENABLED;
		/* IWARP needs one extra vector for CQP just like MISC.*/
		pf->num_iwarp_msix = (int)num_online_cpus() + 1;
	}

#ifdef I40E_FCOE
	i40e_init_pf_fcoe(pf);

#endif /* I40E_FCOE */
#ifdef CONFIG_PCI_IOV
	if (pf->hw.func_caps.num_vfs && pf->hw.partition_id == 1) {
		pf->num_vf_qps = I40E_DEFAULT_QUEUES_PER_VF;
		pf->flags |= I40E_FLAG_SRIOV_ENABLED;
		pf->num_req_vfs = min_t(int,
					pf->hw.func_caps.num_vfs,
					I40E_MAX_VF_COUNT);
	}
#endif /* CONFIG_PCI_IOV */
	if (pf->hw.mac.type == I40E_MAC_X722) {
		pf->flags |= I40E_FLAG_RSS_AQ_CAPABLE |
			     I40E_FLAG_128_QP_RSS_CAPABLE |
			     I40E_FLAG_HW_ATR_EVICT_CAPABLE |
			     I40E_FLAG_OUTER_UDP_CSUM_CAPABLE |
			     I40E_FLAG_WB_ON_ITR_CAPABLE |
			     I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE |
			     I40E_FLAG_NO_PCI_LINK_CHECK |
			     I40E_FLAG_100M_SGMII_CAPABLE |
			     I40E_FLAG_USE_SET_LLDP_MIB |
			     I40E_FLAG_GENEVE_OFFLOAD_CAPABLE;
	} else if ((pf->hw.aq.api_maj_ver > 1) ||
		   ((pf->hw.aq.api_maj_ver == 1) &&
		    (pf->hw.aq.api_min_ver > 4))) {
		/* Supported in FW API version higher than 1.4 */
		pf->flags |= I40E_FLAG_GENEVE_OFFLOAD_CAPABLE;
		pf->auto_disable_flags = I40E_FLAG_HW_ATR_EVICT_CAPABLE;
	} else {
		pf->auto_disable_flags = I40E_FLAG_HW_ATR_EVICT_CAPABLE;
	}

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
	pf->qp_pile->search_hint = 0;

	pf->tx_timeout_recovery_level = 1;

	mutex_init(&pf->switch_mutex);

	/* If NPAR is enabled nudge the Tx scheduler */
	if (pf->hw.func_caps.npar_enable && (!i40e_get_npar_bw_setting(pf)))
		i40e_set_npar_bw_setting(pf);

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
		/* enable FD_SB only if there is MSI-X vector */
		if (pf->num_fdsb_msix > 0)
			pf->flags |= I40E_FLAG_FD_SB_ENABLED;
	} else {
		/* turn off filters, mark for reset and clear SW filter list */
		if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
			need_reset = true;
			i40e_fdir_filter_exit(pf);
		}
		pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
		pf->auto_disable_flags &= ~I40E_FLAG_FD_SB_ENABLED;
		/* reset fd counters */
		pf->fd_add_err = pf->fd_atr_cnt = pf->fd_tcp_rule = 0;
		pf->fdir_pf_active_filters = 0;
		pf->flags |= I40E_FLAG_FD_ATR_ENABLED;
		if (I40E_DEBUG_FD & pf->hw.debug_mask)
			dev_info(&pf->pdev->dev, "ATR re-enabled.\n");
		/* if ATR was auto disabled it can be re-enabled. */
		if ((pf->flags & I40E_FLAG_FD_ATR_ENABLED) &&
		    (pf->auto_disable_flags & I40E_FLAG_FD_ATR_ENABLED))
			pf->auto_disable_flags &= ~I40E_FLAG_FD_ATR_ENABLED;
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

	need_reset = i40e_set_ntuple(pf, features);

	if (need_reset)
		i40e_do_reset(pf, BIT_ULL(__I40E_PF_RESET_REQUESTED));

	return 0;
}

/**
 * i40e_get_udp_port_idx - Lookup a possibly offloaded for Rx UDP port
 * @pf: board private structure
 * @port: The UDP port to look up
 *
 * Returns the index number or I40E_MAX_PF_UDP_OFFLOAD_PORTS if port not found
 **/
static u8 i40e_get_udp_port_idx(struct i40e_pf *pf, __be16 port)
{
	u8 i;

	for (i = 0; i < I40E_MAX_PF_UDP_OFFLOAD_PORTS; i++) {
		if (pf->udp_ports[i].index == port)
			return i;
	}

	return i;
}

/**
 * i40e_udp_tunnel_add - Get notifications about UDP tunnel ports that come up
 * @netdev: This physical port's netdev
 * @ti: Tunnel endpoint information
 **/
static void i40e_udp_tunnel_add(struct net_device *netdev,
				struct udp_tunnel_info *ti)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	__be16 port = ti->port;
	u8 next_idx;
	u8 idx;

	idx = i40e_get_udp_port_idx(pf, port);

	/* Check if port already exists */
	if (idx < I40E_MAX_PF_UDP_OFFLOAD_PORTS) {
		netdev_info(netdev, "port %d already offloaded\n",
			    ntohs(port));
		return;
	}

	/* Now check if there is space to add the new port */
	next_idx = i40e_get_udp_port_idx(pf, 0);

	if (next_idx == I40E_MAX_PF_UDP_OFFLOAD_PORTS) {
		netdev_info(netdev, "maximum number of offloaded UDP ports reached, not adding port %d\n",
			    ntohs(port));
		return;
	}

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		pf->udp_ports[next_idx].type = I40E_AQC_TUNNEL_TYPE_VXLAN;
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (!(pf->flags & I40E_FLAG_GENEVE_OFFLOAD_CAPABLE))
			return;
		pf->udp_ports[next_idx].type = I40E_AQC_TUNNEL_TYPE_NGE;
		break;
	default:
		return;
	}

	/* New port: add it and mark its index in the bitmap */
	pf->udp_ports[next_idx].index = port;
	pf->pending_udp_bitmap |= BIT_ULL(next_idx);
	pf->flags |= I40E_FLAG_UDP_FILTER_SYNC;
}

/**
 * i40e_udp_tunnel_del - Get notifications about UDP tunnel ports that go away
 * @netdev: This physical port's netdev
 * @ti: Tunnel endpoint information
 **/
static void i40e_udp_tunnel_del(struct net_device *netdev,
				struct udp_tunnel_info *ti)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	__be16 port = ti->port;
	u8 idx;

	idx = i40e_get_udp_port_idx(pf, port);

	/* Check if port already exists */
	if (idx >= I40E_MAX_PF_UDP_OFFLOAD_PORTS)
		goto not_found;

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (pf->udp_ports[idx].type != I40E_AQC_TUNNEL_TYPE_VXLAN)
			goto not_found;
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (pf->udp_ports[idx].type != I40E_AQC_TUNNEL_TYPE_NGE)
			goto not_found;
		break;
	default:
		goto not_found;
	}

	/* if port exists, set it to 0 (mark for deletion)
	 * and make it pending
	 */
	pf->udp_ports[idx].index = 0;
	pf->pending_udp_bitmap |= BIT_ULL(idx);
	pf->flags |= I40E_FLAG_UDP_FILTER_SYNC;

	return;
not_found:
	netdev_warn(netdev, "UDP port %d was not found, not deleting\n",
		    ntohs(port));
}

static int i40e_get_phys_port_id(struct net_device *netdev,
				 struct netdev_phys_item_id *ppid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;

	if (!(pf->flags & I40E_FLAG_PORT_ID_VALID))
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
 * @flags: instructions from stack about fdb operation
 */
static int i40e_ndo_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			    struct net_device *dev,
			    const unsigned char *addr, u16 vid,
			    u16 flags)
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
 *
 * Inserts a new hardware bridge if not already created and
 * enables the bridging mode requested (VEB or VEPA). If the
 * hardware bridge has already been inserted and the request
 * is to change the mode then that requires a PF reset to
 * allow rebuild of the components with required hardware
 * bridge mode enabled.
 **/
static int i40e_ndo_bridge_setlink(struct net_device *dev,
				   struct nlmsghdr *nlh,
				   u16 flags)
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
			i40e_do_reset(pf, BIT_ULL(__I40E_PF_RESET_REQUESTED));
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
				       nlflags, 0, 0, filter_mask, NULL);
}

/* Hardware supports L4 tunnel length of 128B (=2^7) which includes
 * inner mac plus all inner ethertypes.
 */
#define I40E_MAX_TUNNEL_HDR_LEN 128
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
	if (skb->encapsulation &&
	    ((skb_inner_network_header(skb) - skb_transport_header(skb)) >
	     I40E_MAX_TUNNEL_HDR_LEN))
		return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
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
#ifdef I40E_FCOE
	.ndo_fcoe_enable	= i40e_fcoe_enable,
	.ndo_fcoe_disable	= i40e_fcoe_disable,
#endif
	.ndo_set_features	= i40e_set_features,
	.ndo_set_vf_mac		= i40e_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= i40e_ndo_set_vf_port_vlan,
	.ndo_set_vf_rate	= i40e_ndo_set_vf_bw,
	.ndo_get_vf_config	= i40e_ndo_get_vf_config,
	.ndo_set_vf_link_state	= i40e_ndo_set_vf_link_state,
	.ndo_set_vf_spoofchk	= i40e_ndo_set_vf_spoofchk,
	.ndo_set_vf_trust	= i40e_ndo_set_vf_trust,
	.ndo_udp_tunnel_add	= i40e_udp_tunnel_add,
	.ndo_udp_tunnel_del	= i40e_udp_tunnel_del,
	.ndo_get_phys_port_id	= i40e_get_phys_port_id,
	.ndo_fdb_add		= i40e_ndo_fdb_add,
	.ndo_features_check	= i40e_features_check,
	.ndo_bridge_getlink	= i40e_ndo_bridge_getlink,
	.ndo_bridge_setlink	= i40e_ndo_bridge_setlink,
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
	u8 mac_addr[ETH_ALEN];
	int etherdev_size;

	etherdev_size = sizeof(struct i40e_netdev_priv);
	netdev = alloc_etherdev_mq(etherdev_size, vsi->alloc_queue_pairs);
	if (!netdev)
		return -ENOMEM;

	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	netdev->hw_enc_features |= NETIF_F_SG			|
				   NETIF_F_IP_CSUM		|
				   NETIF_F_IPV6_CSUM		|
				   NETIF_F_HIGHDMA		|
				   NETIF_F_SOFT_FEATURES	|
				   NETIF_F_TSO			|
				   NETIF_F_TSO_ECN		|
				   NETIF_F_TSO6			|
				   NETIF_F_GSO_GRE		|
				   NETIF_F_GSO_GRE_CSUM		|
				   NETIF_F_GSO_IPXIP4		|
				   NETIF_F_GSO_IPXIP6		|
				   NETIF_F_GSO_UDP_TUNNEL	|
				   NETIF_F_GSO_UDP_TUNNEL_CSUM	|
				   NETIF_F_GSO_PARTIAL		|
				   NETIF_F_SCTP_CRC		|
				   NETIF_F_RXHASH		|
				   NETIF_F_RXCSUM		|
				   0;

	if (!(pf->flags & I40E_FLAG_OUTER_UDP_CSUM_CAPABLE))
		netdev->gso_partial_features |= NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->gso_partial_features |= NETIF_F_GSO_GRE_CSUM;

	/* record features VLANs can make use of */
	netdev->vlan_features |= netdev->hw_enc_features |
				 NETIF_F_TSO_MANGLEID;

	if (!(pf->flags & I40E_FLAG_MFP_ENABLED))
		netdev->hw_features |= NETIF_F_NTUPLE;

	netdev->hw_features |= netdev->hw_enc_features	|
			       NETIF_F_HW_VLAN_CTAG_TX	|
			       NETIF_F_HW_VLAN_CTAG_RX;

	netdev->features |= netdev->hw_features | NETIF_F_HW_VLAN_CTAG_FILTER;
	netdev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	if (vsi->type == I40E_VSI_MAIN) {
		SET_NETDEV_DEV(netdev, &pf->pdev->dev);
		ether_addr_copy(mac_addr, hw->mac.perm_addr);
		/* The following steps are necessary to prevent reception
		 * of tagged packets - some older NVM configurations load a
		 * default a MAC-VLAN filter that accepts any tagged packet
		 * which must be replaced by a normal filter.
		 */
		i40e_rm_default_mac_filter(vsi, mac_addr);
		spin_lock_bh(&vsi->mac_filter_list_lock);
		i40e_add_filter(vsi, mac_addr, I40E_VLAN_ANY, false, true);
		spin_unlock_bh(&vsi->mac_filter_list_lock);
	} else {
		/* relate the VSI_VMDQ name to the VSI_MAIN name */
		snprintf(netdev->name, IFNAMSIZ, "%sv%%d",
			 pf->vsi[pf->lan_vsi]->netdev->name);
		random_ether_addr(mac_addr);

		spin_lock_bh(&vsi->mac_filter_list_lock);
		i40e_add_filter(vsi, mac_addr, I40E_VLAN_ANY, false, false);
		spin_unlock_bh(&vsi->mac_filter_list_lock);
	}

	ether_addr_copy(netdev->dev_addr, mac_addr);
	ether_addr_copy(netdev->perm_addr, mac_addr);

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	/* Setup netdev TC information */
	i40e_vsi_config_netdev_tc(vsi, vsi->tc_config.enabled_tc);

	netdev->netdev_ops = &i40e_netdev_ops;
	netdev->watchdog_timeo = 5 * HZ;
	i40e_set_ethtool_ops(netdev);
#ifdef I40E_FCOE
	i40e_fcoe_config_netdev(netdev, vsi);
#endif

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
	if (vsi->veb_idx == I40E_NO_VEB)
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
	i40e_status aq_ret = 0;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	struct i40e_mac_filter *f, *ftmp;

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
				dev_info(&pf->pdev->dev,
					 "failed to configure TCs for main VSI tc_map 0x%08x, err %s aq_err %s\n",
					 enabled_tc,
					 i40e_stat_str(&pf->hw, ret),
					 i40e_aq_str(&pf->hw,
						    pf->hw.aq.asq_last_status));
				ret = -ENOENT;
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

#ifdef I40E_FCOE
	case I40E_VSI_FCOE:
		ret = i40e_fcoe_vsi_init(vsi, &ctxt);
		if (ret) {
			dev_info(&pf->pdev->dev, "failed to initialize FCoE VSI\n");
			return ret;
		}
		break;

#endif /* I40E_FCOE */
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
	/* Except FDIR VSI, for all othet VSI set the broadcast filter */
	if (vsi->type != I40E_VSI_FDIR) {
		aq_ret = i40e_aq_set_vsi_broadcast(hw, vsi->seid, true, NULL);
		if (aq_ret) {
			ret = i40e_aq_rc_to_posix(aq_ret,
						  hw->aq.asq_last_status);
			dev_info(&pf->pdev->dev,
				 "set brdcast promisc failed, err %s, aq_err %s\n",
				 i40e_stat_str(hw, aq_ret),
				 i40e_aq_str(hw, hw->aq.asq_last_status));
		}
	}

	vsi->active_filters = 0;
	clear_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state);
	spin_lock_bh(&vsi->mac_filter_list_lock);
	/* If macvlan filters already exist, force them to get loaded */
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
		f->state = I40E_FILTER_NEW;
		f_count++;
	}
	spin_unlock_bh(&vsi->mac_filter_list_lock);

	if (f_count) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		pf->flags |= I40E_FLAG_FILTER_SYNC;
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
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_veb *veb = NULL;
	struct i40e_pf *pf;
	u16 uplink_seid;
	int i, n;

	pf = vsi->back;

	/* release of a VEB-owner or last VSI is not allowed */
	if (vsi->flags & I40E_VSI_FLAG_VEB_OWNER) {
		dev_info(&pf->pdev->dev, "VSI %d has existing VEB %d\n",
			 vsi->seid, vsi->uplink_seid);
		return -ENODEV;
	}
	if (vsi == pf->vsi[pf->lan_vsi] &&
	    !test_bit(__I40E_DOWN, &pf->state)) {
		dev_info(&pf->pdev->dev, "Can't remove PF VSI\n");
		return -ENODEV;
	}

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

	spin_lock_bh(&vsi->mac_filter_list_lock);
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list)
		i40e_del_filter(vsi, f->macaddr, f->vlan,
				f->is_vf, f->is_netdev);
	spin_unlock_bh(&vsi->mac_filter_list_lock);

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

	ret = i40e_get_lump(pf, pf->qp_pile, vsi->alloc_queue_pairs, vsi->idx);
	if (ret < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get tracking for %d queues for VSI %d err %d\n",
			 vsi->alloc_queue_pairs, vsi->seid, ret);
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
	ret = i40e_get_lump(pf, pf->qp_pile, vsi->alloc_queue_pairs,
				vsi->idx);
	if (ret < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get tracking for %d queues for VSI %d err=%d\n",
			 vsi->alloc_queue_pairs, vsi->seid, ret);
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
		/* Apply relevant filters if a platform-specific mac
		 * address was selected.
		 */
		if (!!(pf->flags & I40E_FLAG_PF_MAC)) {
			ret = i40e_macaddr_init(vsi, pf->hw.mac.addr);
			if (ret) {
				dev_warn(&pf->pdev->dev,
					 "could not set up macaddr; err %d\n",
					 ret);
			}
		}
	case I40E_VSI_VMDQ2:
	case I40E_VSI_FCOE:
		ret = i40e_config_netdev(vsi);
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
		/* fall through */

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

	if ((pf->flags & I40E_FLAG_RSS_AQ_CAPABLE) &&
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
	if (vsi_idx >= pf->num_alloc_vsi && vsi_seid != 0) {
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
		if (pf->lan_veb == I40E_NO_VEB) {
			int v;

			/* find existing or else empty VEB */
			for (v = 0; v < I40E_MAX_VEB; v++) {
				if (pf->veb[v] && (pf->veb[v]->seid == seid)) {
					pf->lan_veb = v;
					break;
				}
			}
			if (pf->lan_veb == I40E_NO_VEB) {
				v = i40e_veb_mem_alloc(pf);
				if (v < 0)
					break;
				pf->lan_veb = v;
			}
		}

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
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_setup_pf_switch(struct i40e_pf *pf, bool reinit)
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
	    !(pf->flags & I40E_FLAG_TRUE_PROMISC_SUPPORT))
		flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;

	if (pf->hw.pf_id == 0) {
		u16 valid_flags;

		valid_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		ret = i40e_aq_set_switch_config(&pf->hw, flags, valid_flags,
						NULL);
		if (ret && pf->hw.aq.asq_last_status != I40E_AQ_RC_ESRCH) {
			dev_info(&pf->pdev->dev,
				 "couldn't set switch config bits, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			/* not a fatal problem, just keep going */
		}
	}

	/* first time setup */
	if (pf->lan_vsi == I40E_NO_VSI || reinit) {
		struct i40e_vsi *vsi = NULL;
		u16 uplink_seid;

		/* Set up the PF VSI associated with the PF's main VSI
		 * that is already in the HW switch
		 */
		if (pf->lan_veb != I40E_NO_VEB && pf->veb[pf->lan_veb])
			uplink_seid = pf->veb[pf->lan_veb]->seid;
		else
			uplink_seid = pf->mac_seid;
		if (pf->lan_vsi == I40E_NO_VSI)
			vsi = i40e_vsi_setup(pf, I40E_VSI_MAIN, uplink_seid, 0);
		else if (reinit)
			vsi = i40e_vsi_reinit_setup(pf->vsi[pf->lan_vsi]);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "setup of MAIN VSI failed\n");
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
	i40e_update_link_info(&pf->hw);
	i40e_link_event(pf);

	/* Initialize user-specific link properties */
	pf->fc_autoneg_status = ((pf->hw.phy.link_info.an_info &
				  I40E_AQ_AN_COMPLETED) ? true : false);

	i40e_ptp_init(pf);

	return ret;
}

/**
 * i40e_determine_queue_usage - Work out queue distribution
 * @pf: board private structure
 **/
static void i40e_determine_queue_usage(struct i40e_pf *pf)
{
	int queues_left;

	pf->num_lan_qps = 0;
#ifdef I40E_FCOE
	pf->num_fcoe_qps = 0;
#endif

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
#ifdef I40E_FCOE
			       I40E_FLAG_FCOE_ENABLED	|
#endif
			       I40E_FLAG_FD_SB_ENABLED	|
			       I40E_FLAG_FD_ATR_ENABLED	|
			       I40E_FLAG_DCB_CAPABLE	|
			       I40E_FLAG_SRIOV_ENABLED	|
			       I40E_FLAG_VMDQ_ENABLED);
	} else if (!(pf->flags & (I40E_FLAG_RSS_ENABLED |
				  I40E_FLAG_FD_SB_ENABLED |
				  I40E_FLAG_FD_ATR_ENABLED |
				  I40E_FLAG_DCB_CAPABLE))) {
		/* one qp for PF */
		pf->alloc_rss_size = pf->num_lan_qps = 1;
		queues_left -= pf->num_lan_qps;

		pf->flags &= ~(I40E_FLAG_RSS_ENABLED	|
			       I40E_FLAG_IWARP_ENABLED	|
#ifdef I40E_FCOE
			       I40E_FLAG_FCOE_ENABLED	|
#endif
			       I40E_FLAG_FD_SB_ENABLED	|
			       I40E_FLAG_FD_ATR_ENABLED	|
			       I40E_FLAG_DCB_ENABLED	|
			       I40E_FLAG_VMDQ_ENABLED);
	} else {
		/* Not enough queues for all TCs */
		if ((pf->flags & I40E_FLAG_DCB_CAPABLE) &&
		    (queues_left < I40E_MAX_TRAFFIC_CLASS)) {
			pf->flags &= ~I40E_FLAG_DCB_CAPABLE;
			dev_info(&pf->pdev->dev, "not enough queues for DCB. DCB is disabled.\n");
		}
		pf->num_lan_qps = max_t(int, pf->rss_size_max,
					num_online_cpus());
		pf->num_lan_qps = min_t(int, pf->num_lan_qps,
					pf->hw.func_caps.num_tx_qp);

		queues_left -= pf->num_lan_qps;
	}

#ifdef I40E_FCOE
	if (pf->flags & I40E_FLAG_FCOE_ENABLED) {
		if (I40E_DEFAULT_FCOE <= queues_left) {
			pf->num_fcoe_qps = I40E_DEFAULT_FCOE;
		} else if (I40E_MINIMUM_FCOE <= queues_left) {
			pf->num_fcoe_qps = I40E_MINIMUM_FCOE;
		} else {
			pf->num_fcoe_qps = 0;
			pf->flags &= ~I40E_FLAG_FCOE_ENABLED;
			dev_info(&pf->pdev->dev, "not enough queues for FCoE. FCoE feature will be disabled\n");
		}

		queues_left -= pf->num_fcoe_qps;
	}

#endif
	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		if (queues_left > 1) {
			queues_left -= 1; /* save 1 queue for FD */
		} else {
			pf->flags &= ~I40E_FLAG_FD_SB_ENABLED;
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
#ifdef I40E_FCOE
	dev_dbg(&pf->pdev->dev, "fcoe queues = %d\n", pf->num_fcoe_qps);
#endif
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
	i += snprintf(&buf[i], REMAIN(i), " VFs: %d", pf->num_req_vfs);
#endif
	i += snprintf(&buf[i], REMAIN(i), " VSIs: %d QP: %d",
		      pf->hw.func_caps.num_vsis,
		      pf->vsi[pf->lan_vsi]->num_queue_pairs);
	if (pf->flags & I40E_FLAG_RSS_ENABLED)
		i += snprintf(&buf[i], REMAIN(i), " RSS");
	if (pf->flags & I40E_FLAG_FD_ATR_ENABLED)
		i += snprintf(&buf[i], REMAIN(i), " FD_ATR");
	if (pf->flags & I40E_FLAG_FD_SB_ENABLED) {
		i += snprintf(&buf[i], REMAIN(i), " FD_SB");
		i += snprintf(&buf[i], REMAIN(i), " NTUPLE");
	}
	if (pf->flags & I40E_FLAG_DCB_CAPABLE)
		i += snprintf(&buf[i], REMAIN(i), " DCB");
	i += snprintf(&buf[i], REMAIN(i), " VxLAN");
	i += snprintf(&buf[i], REMAIN(i), " Geneve");
	if (pf->flags & I40E_FLAG_PTP)
		i += snprintf(&buf[i], REMAIN(i), " PTP");
#ifdef I40E_FCOE
	if (pf->flags & I40E_FLAG_FCOE_ENABLED)
		i += snprintf(&buf[i], REMAIN(i), " FCOE");
#endif
	if (pf->flags & I40E_FLAG_VEB_MODE_ENABLED)
		i += snprintf(&buf[i], REMAIN(i), " VEB");
	else
		i += snprintf(&buf[i], REMAIN(i), " VEPA");

	dev_info(&pf->pdev->dev, "%s\n", buf);
	kfree(buf);
	WARN_ON(i > INFO_STRING_LEN);
}

/**
 * i40e_get_platform_mac_addr - get platform-specific MAC address
 *
 * @pdev: PCI device information struct
 * @pf: board private structure
 *
 * Look up the MAC address in Open Firmware  on systems that support it,
 * and use IDPROM on SPARC if no OF address is found. On return, the
 * I40E_FLAG_PF_MAC will be wset in pf->flags if a platform-specific value
 * has been selected.
 **/
static void i40e_get_platform_mac_addr(struct pci_dev *pdev, struct i40e_pf *pf)
{
	pf->flags &= ~I40E_FLAG_PF_MAC;
	if (!eth_platform_get_mac_address(&pdev->dev, pf->hw.mac.addr))
		pf->flags |= I40E_FLAG_PF_MAC;
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
	u8 set_fc_aq_fail;

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
	set_bit(__I40E_DOWN, &pf->state);

	hw = &pf->hw;
	hw->back = pf;

	pf->ioremap_len = min_t(int, pci_resource_len(pdev, 0),
				I40E_MAX_CSR_SPACE);

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
	pf->instance = pfs_found;

	/* set up the locks for the AQ, do this only once in probe
	 * and destroy them only once in remove
	 */
	mutex_init(&hw->aq.asq_mutex);
	mutex_init(&hw->aq.arq_mutex);

	if (debug != -1) {
		pf->msg_enable = pf->hw.debug_mask;
		pf->msg_enable = debug;
	}

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
	err = i40e_pf_reset(hw);
	if (err) {
		dev_info(&pdev->dev, "Initial pf_reset failed: %d\n", err);
		goto err_pf_reset;
	}
	pf->pfr_count++;

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
				 "The driver for the device stopped because the NVM image is newer than expected. You must install the most recent version of the network driver.\n");
		else
			dev_info(&pdev->dev,
				 "The driver for the device stopped because the device firmware failed to init. Try updating your NVM image.\n");

		goto err_pf_reset;
	}

	/* provide nvm, fw, api versions */
	dev_info(&pdev->dev, "fw %d.%d.%05d api %d.%d nvm %s\n",
		 hw->aq.fw_maj_ver, hw->aq.fw_min_ver, hw->aq.fw_build,
		 hw->aq.api_maj_ver, hw->aq.api_min_ver,
		 i40e_nvm_version_str(hw));

	if (hw->aq.api_maj_ver == I40E_FW_API_VERSION_MAJOR &&
	    hw->aq.api_min_ver > I40E_FW_API_VERSION_MINOR)
		dev_info(&pdev->dev,
			 "The driver for the device detected a newer version of the NVM image than expected. Please install the most recent version of the network driver.\n");
	else if (hw->aq.api_maj_ver < I40E_FW_API_VERSION_MAJOR ||
		 hw->aq.api_min_ver < (I40E_FW_API_VERSION_MINOR - 1))
		dev_info(&pdev->dev,
			 "The driver for the device detected an older version of the NVM image than expected. Please update the NVM image.\n");

	i40e_verify_eeprom(pf);

	/* Rev 0 hardware was never productized */
	if (hw->revision_id < 1)
		dev_warn(&pdev->dev, "This device is a pre-production adapter/LOM. Please be aware there may be issues with your hardware. If you are experiencing problems please contact your Intel or hardware representative who provided you with this hardware.\n");

	i40e_clear_pxe_mode(hw);
	err = i40e_get_capabilities(pf);
	if (err)
		goto err_adminq_setup;

	err = i40e_sw_init(pf);
	if (err) {
		dev_info(&pdev->dev, "sw_init failed: %d\n", err);
		goto err_sw_init;
	}

	err = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp,
				pf->fcoe_hmc_cntx_num, pf->fcoe_hmc_filt_num);
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
	if (pf->flags & I40E_FLAG_STOP_FW_LLDP) {
		dev_info(&pdev->dev, "Stopping firmware LLDP agent.\n");
		i40e_aq_stop_lldp(hw, true, NULL);
	}

	i40e_get_mac_addr(hw, hw->mac.addr);
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
		pf->flags |= I40E_FLAG_PORT_ID_VALID;
#ifdef I40E_FCOE
	err = i40e_get_san_mac_addr(hw, hw->mac.san_addr);
	if (err)
		dev_info(&pdev->dev,
			 "(non-fatal) SAN MAC retrieval failed: %d\n", err);
	if (!is_valid_ether_addr(hw->mac.san_addr)) {
		dev_warn(&pdev->dev, "invalid SAN MAC address %pM, falling back to LAN MAC\n",
			 hw->mac.san_addr);
		ether_addr_copy(hw->mac.san_addr, hw->mac.addr);
	}
	dev_info(&pf->pdev->dev, "SAN MAC: %pM\n", hw->mac.san_addr);
#endif /* I40E_FCOE */

	pci_set_drvdata(pdev, pf);
	pci_save_state(pdev);
#ifdef CONFIG_I40E_DCB
	err = i40e_init_pf_dcb(pf);
	if (err) {
		dev_info(&pdev->dev, "DCB init failed %d, disabled\n", err);
		pf->flags &= ~I40E_FLAG_DCB_CAPABLE;
		/* Continue without DCB enabled */
	}
#endif /* CONFIG_I40E_DCB */

	/* set up periodic task facility */
	setup_timer(&pf->service_timer, i40e_service_timer, (unsigned long)pf);
	pf->service_timer_period = HZ;

	INIT_WORK(&pf->service_task, i40e_service_task);
	clear_bit(__I40E_SERVICE_SCHED, &pf->state);
	pf->flags |= I40E_FLAG_NEED_LINK_UPDATE;

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

	/* The number of VSIs reported by the FW is the minimum guaranteed
	 * to us; HW supports far more and we share the remaining pool with
	 * the other PFs. We allocate space for more than the guarantee with
	 * the understanding that we might not get them all later.
	 */
	if (pf->hw.func_caps.num_vsis < I40E_MIN_VSI_ALLOC)
		pf->num_alloc_vsi = I40E_MIN_VSI_ALLOC;
	else
		pf->num_alloc_vsi = pf->hw.func_caps.num_vsis;

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
	    !test_bit(__I40E_BAD_EEPROM, &pf->state)) {
		if (pci_num_vf(pdev))
			pf->flags |= I40E_FLAG_VEB_MODE_ENABLED;
	}
#endif
	err = i40e_setup_pf_switch(pf, false);
	if (err) {
		dev_info(&pdev->dev, "setup_pf_switch failed: %d\n", err);
		goto err_vsis;
	}

	/* Make sure flow control is set according to current settings */
	err = i40e_set_fc(hw, &set_fc_aq_fail, true);
	if (set_fc_aq_fail & I40E_SET_FC_AQ_FAIL_GET)
		dev_dbg(&pf->pdev->dev,
			"Set fc with err %s aq_err %s on get_phy_cap\n",
			i40e_stat_str(hw, err),
			i40e_aq_str(hw, hw->aq.asq_last_status));
	if (set_fc_aq_fail & I40E_SET_FC_AQ_FAIL_SET)
		dev_dbg(&pf->pdev->dev,
			"Set fc with err %s aq_err %s on set_phy_config\n",
			i40e_stat_str(hw, err),
			i40e_aq_str(hw, hw->aq.asq_last_status));
	if (set_fc_aq_fail & I40E_SET_FC_AQ_FAIL_UPDATE)
		dev_dbg(&pf->pdev->dev,
			"Set fc with err %s aq_err %s on get_link_info\n",
			i40e_stat_str(hw, err),
			i40e_aq_str(hw, hw->aq.asq_last_status));

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

	if (pf->flags & I40E_FLAG_RESTART_AUTONEG) {
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
	clear_bit(__I40E_DOWN, &pf->state);

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
			goto err_vsis;
		}
	}

#ifdef CONFIG_PCI_IOV
	/* prep for VF support */
	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSIX_ENABLED) &&
	    !test_bit(__I40E_BAD_EEPROM, &pf->state)) {
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
	err = i40e_lan_add_device(pf);
	if (err)
		dev_info(&pdev->dev, "Failed to add PF to client API service list: %d\n",
			 err);

#ifdef I40E_FCOE
	/* create FCoE interface */
	i40e_fcoe_vsi_setup(pf);

#endif
#define PCI_SPEED_SIZE 8
#define PCI_WIDTH_SIZE 8
	/* Devices on the IOSF bus do not have this information
	 * and will report PCI Gen 1 x 1 by default so don't bother
	 * checking them.
	 */
	if (!(pf->flags & I40E_FLAG_NO_PCI_LINK_CHECK)) {
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
			strncpy(speed, "8.0", PCI_SPEED_SIZE); break;
		case i40e_bus_speed_5000:
			strncpy(speed, "5.0", PCI_SPEED_SIZE); break;
		case i40e_bus_speed_2500:
			strncpy(speed, "2.5", PCI_SPEED_SIZE); break;
		default:
			break;
		}
		switch (hw->bus.width) {
		case i40e_bus_width_pcie_x8:
			strncpy(width, "8", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x4:
			strncpy(width, "4", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x2:
			strncpy(width, "2", PCI_WIDTH_SIZE); break;
		case i40e_bus_width_pcie_x1:
			strncpy(width, "1", PCI_WIDTH_SIZE); break;
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

	/* get the supported phy types from the fw */
	err = i40e_aq_get_phy_capabilities(hw, false, true, &abilities, NULL);
	if (err)
		dev_dbg(&pf->pdev->dev, "get supported phy types ret =  %s last_status =  %s\n",
			i40e_stat_str(&pf->hw, err),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
	pf->hw.phy.phy_types = le32_to_cpu(abilities.phy_type);

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
		pf->flags |= I40E_FLAG_HAVE_10GBASET_PHY;

	/* print a string summarizing features */
	i40e_print_features(pf);

	return 0;

	/* Unwind what we've done if something failed in the setup */
err_vsis:
	set_bit(__I40E_DOWN, &pf->state);
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

	/* no more scheduling of any task */
	set_bit(__I40E_SUSPENDED, &pf->state);
	set_bit(__I40E_DOWN, &pf->state);
	if (pf->service_timer.data)
		del_timer_sync(&pf->service_timer);
	if (pf->service_task.func)
		cancel_work_sync(&pf->service_task);

	if (pf->flags & I40E_FLAG_SRIOV_ENABLED) {
		i40e_free_vfs(pf);
		pf->flags &= ~I40E_FLAG_SRIOV_ENABLED;
	}

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

	/* remove attached clients */
	ret_code = i40e_lan_del_device(pf);
	if (ret_code) {
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

	/* shutdown the adminq */
	ret_code = i40e_shutdown_adminq(hw);
	if (ret_code)
		dev_warn(&pdev->dev,
			 "Failed to destroy the Admin Queue resources: %d\n",
			 ret_code);

	/* destroy the locks only once, here */
	mutex_destroy(&hw->aq.arq_mutex);
	mutex_destroy(&hw->aq.asq_mutex);

	/* Clear all dynamic memory lists of rings, q_vectors, and VSIs */
	i40e_clear_interrupt_scheme(pf);
	for (i = 0; i < pf->num_alloc_vsi; i++) {
		if (pf->vsi[i]) {
			i40e_vsi_clear_rings(pf->vsi[i]);
			i40e_vsi_clear(pf->vsi[i]);
			pf->vsi[i] = NULL;
		}
	}

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
 *
 * Called to warn that something happened and the error handling steps
 * are in progress.  Allows the driver to quiesce things, be ready for
 * remediation.
 **/
static pci_ers_result_t i40e_pci_error_detected(struct pci_dev *pdev,
						enum pci_channel_state error)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s: error %d\n", __func__, error);

	/* shutdown all operations */
	if (!test_bit(__I40E_SUSPENDED, &pf->state)) {
		rtnl_lock();
		i40e_prep_for_reset(pf);
		rtnl_unlock();
	}

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
	int err;
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

	err = pci_cleanup_aer_uncorrect_error_status(pdev);
	if (err) {
		dev_info(&pdev->dev,
			 "pci_cleanup_aer_uncorrect_error_status failed 0x%0x\n",
			 err);
		/* non-fatal, continue */
	}

	return result;
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
	if (test_bit(__I40E_SUSPENDED, &pf->state))
		return;

	rtnl_lock();
	i40e_handle_reset_warning(pf);
	rtnl_unlock();
}

/**
 * i40e_shutdown - PCI callback for shutting down
 * @pdev: PCI device information struct
 **/
static void i40e_shutdown(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	struct i40e_hw *hw = &pf->hw;

	set_bit(__I40E_SUSPENDED, &pf->state);
	set_bit(__I40E_DOWN, &pf->state);
	rtnl_lock();
	i40e_prep_for_reset(pf);
	rtnl_unlock();

	wr32(hw, I40E_PFPM_APM, (pf->wol_en ? I40E_PFPM_APM_APME_MASK : 0));
	wr32(hw, I40E_PFPM_WUFC, (pf->wol_en ? I40E_PFPM_WUFC_MAG_MASK : 0));

	del_timer_sync(&pf->service_timer);
	cancel_work_sync(&pf->service_task);
	i40e_fdir_teardown(pf);

	rtnl_lock();
	i40e_prep_for_reset(pf);
	rtnl_unlock();

	wr32(hw, I40E_PFPM_APM,
	     (pf->wol_en ? I40E_PFPM_APM_APME_MASK : 0));
	wr32(hw, I40E_PFPM_WUFC,
	     (pf->wol_en ? I40E_PFPM_WUFC_MAG_MASK : 0));

	i40e_clear_interrupt_scheme(pf);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, pf->wol_en);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

#ifdef CONFIG_PM
/**
 * i40e_suspend - PCI callback for moving to D3
 * @pdev: PCI device information struct
 **/
static int i40e_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	struct i40e_hw *hw = &pf->hw;
	int retval = 0;

	set_bit(__I40E_SUSPENDED, &pf->state);
	set_bit(__I40E_DOWN, &pf->state);

	rtnl_lock();
	i40e_prep_for_reset(pf);
	rtnl_unlock();

	wr32(hw, I40E_PFPM_APM, (pf->wol_en ? I40E_PFPM_APM_APME_MASK : 0));
	wr32(hw, I40E_PFPM_WUFC, (pf->wol_en ? I40E_PFPM_WUFC_MAG_MASK : 0));

	i40e_stop_misc_vector(pf);

	retval = pci_save_state(pdev);
	if (retval)
		return retval;

	pci_wake_from_d3(pdev, pf->wol_en);
	pci_set_power_state(pdev, PCI_D3hot);

	return retval;
}

/**
 * i40e_resume - PCI callback for waking up from D3
 * @pdev: PCI device information struct
 **/
static int i40e_resume(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	/* pci_restore_state() clears dev->state_saves, so
	 * call pci_save_state() again to restore it.
	 */
	pci_save_state(pdev);

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	/* no wakeup events while running */
	pci_wake_from_d3(pdev, false);

	/* handling the reset will rebuild the device state */
	if (test_and_clear_bit(__I40E_SUSPENDED, &pf->state)) {
		clear_bit(__I40E_DOWN, &pf->state);
		rtnl_lock();
		i40e_reset_and_rebuild(pf, false);
		rtnl_unlock();
	}

	return 0;
}

#endif
static const struct pci_error_handlers i40e_err_handler = {
	.error_detected = i40e_pci_error_detected,
	.slot_reset = i40e_pci_error_slot_reset,
	.resume = i40e_pci_error_resume,
};

static struct pci_driver i40e_driver = {
	.name     = i40e_driver_name,
	.id_table = i40e_pci_tbl,
	.probe    = i40e_probe,
	.remove   = i40e_remove,
#ifdef CONFIG_PM
	.suspend  = i40e_suspend,
	.resume   = i40e_resume,
#endif
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
	pr_info("%s: %s - version %s\n", i40e_driver_name,
		i40e_driver_string, i40e_driver_version_str);
	pr_info("%s: %s\n", i40e_driver_name, i40e_copyright);

	/* we will see if single thread per module is enough for now,
	 * it can't be any worse than using the system workqueue which
	 * was already single threaded
	 */
	i40e_wq = create_singlethread_workqueue(i40e_driver_name);
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
