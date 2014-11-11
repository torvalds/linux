/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
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

#ifdef CONFIG_DEBUG_FS

#include <linux/fs.h>
#include <linux/debugfs.h>

#include "i40e.h"

static struct dentry *i40e_dbg_root;

/**
 * i40e_dbg_find_vsi - searches for the vsi with the given seid
 * @pf - the pf structure to search for the vsi
 * @seid - seid of the vsi it is searching for
 **/
static struct i40e_vsi *i40e_dbg_find_vsi(struct i40e_pf *pf, int seid)
{
	int i;

	if (seid < 0)
		dev_info(&pf->pdev->dev, "%d: bad seid\n", seid);
	else
		for (i = 0; i < pf->num_alloc_vsi; i++)
			if (pf->vsi[i] && (pf->vsi[i]->seid == seid))
				return pf->vsi[i];

	return NULL;
}

/**
 * i40e_dbg_find_veb - searches for the veb with the given seid
 * @pf - the pf structure to search for the veb
 * @seid - seid of the veb it is searching for
 **/
static struct i40e_veb *i40e_dbg_find_veb(struct i40e_pf *pf, int seid)
{
	int i;

	if ((seid < I40E_BASE_VEB_SEID) ||
	    (seid > (I40E_BASE_VEB_SEID + I40E_MAX_VEB)))
		dev_info(&pf->pdev->dev, "%d: bad seid\n", seid);
	else
		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i] && pf->veb[i]->seid == seid)
				return pf->veb[i];
	return NULL;
}

/**************************************************************
 * dump
 * The dump entry in debugfs is for getting a data snapshow of
 * the driver's current configuration and runtime details.
 * When the filesystem entry is written, a snapshot is taken.
 * When the entry is read, the most recent snapshot data is dumped.
 **************************************************************/
static char *i40e_dbg_dump_buf;
static ssize_t i40e_dbg_dump_data_len;
static ssize_t i40e_dbg_dump_buffer_len;

/**
 * i40e_dbg_dump_read - read the dump data
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_dump_read(struct file *filp, char __user *buffer,
				  size_t count, loff_t *ppos)
{
	int bytes_not_copied;
	int len;

	/* is *ppos bigger than the available data? */
	if (*ppos >= i40e_dbg_dump_data_len || !i40e_dbg_dump_buf)
		return 0;

	/* be sure to not read beyond the end of available data */
	len = min_t(int, count, (i40e_dbg_dump_data_len - *ppos));

	bytes_not_copied = copy_to_user(buffer, &i40e_dbg_dump_buf[*ppos], len);
	if (bytes_not_copied < 0)
		return bytes_not_copied;

	*ppos += len;
	return len;
}

/**
 * i40e_dbg_prep_dump_buf
 * @pf: the pf we're working with
 * @buflen: the desired buffer length
 *
 * Return positive if success, 0 if failed
 **/
static int i40e_dbg_prep_dump_buf(struct i40e_pf *pf, int buflen)
{
	/* if not already big enough, prep for re alloc */
	if (i40e_dbg_dump_buffer_len && i40e_dbg_dump_buffer_len < buflen) {
		kfree(i40e_dbg_dump_buf);
		i40e_dbg_dump_buffer_len = 0;
		i40e_dbg_dump_buf = NULL;
	}

	/* get a new buffer if needed */
	if (!i40e_dbg_dump_buf) {
		i40e_dbg_dump_buf = kzalloc(buflen, GFP_KERNEL);
		if (i40e_dbg_dump_buf != NULL)
			i40e_dbg_dump_buffer_len = buflen;
	}

	return i40e_dbg_dump_buffer_len;
}

/**
 * i40e_dbg_dump_write - trigger a datadump snapshot
 * @filp: the opened file
 * @buffer: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 *
 * Any write clears the stats
 **/
static ssize_t i40e_dbg_dump_write(struct file *filp,
				   const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	bool seid_found = false;
	long seid = -1;
	int buflen = 0;
	int i, ret;
	int len;
	u8 *p;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	/* decode the SEID given to be dumped */
	ret = kstrtol_from_user(buffer, count, 0, &seid);

	if (ret) {
		dev_info(&pf->pdev->dev, "bad seid value\n");
	} else if (seid == 0) {
		seid_found = true;

		kfree(i40e_dbg_dump_buf);
		i40e_dbg_dump_buffer_len = 0;
		i40e_dbg_dump_data_len = 0;
		i40e_dbg_dump_buf = NULL;
		dev_info(&pf->pdev->dev, "debug buffer freed\n");

	} else if (seid == pf->pf_seid || seid == 1) {
		seid_found = true;

		buflen = sizeof(struct i40e_pf);
		buflen += (sizeof(struct i40e_aq_desc)
		     * (pf->hw.aq.num_arq_entries + pf->hw.aq.num_asq_entries));

		if (i40e_dbg_prep_dump_buf(pf, buflen)) {
			p = i40e_dbg_dump_buf;

			len = sizeof(struct i40e_pf);
			memcpy(p, pf, len);
			p += len;

			len = (sizeof(struct i40e_aq_desc)
					* pf->hw.aq.num_asq_entries);
			memcpy(p, pf->hw.aq.asq.desc_buf.va, len);
			p += len;

			len = (sizeof(struct i40e_aq_desc)
					* pf->hw.aq.num_arq_entries);
			memcpy(p, pf->hw.aq.arq.desc_buf.va, len);
			p += len;

			i40e_dbg_dump_data_len = buflen;
			dev_info(&pf->pdev->dev,
				 "PF seid %ld dumped %d bytes\n",
				 seid, (int)i40e_dbg_dump_data_len);
		}
	} else if (seid >= I40E_BASE_VSI_SEID) {
		struct i40e_vsi *vsi = NULL;
		struct i40e_mac_filter *f;
		int filter_count = 0;

		mutex_lock(&pf->switch_mutex);
		vsi = i40e_dbg_find_vsi(pf, seid);
		if (!vsi) {
			mutex_unlock(&pf->switch_mutex);
			goto write_exit;
		}

		buflen = sizeof(struct i40e_vsi);
		buflen += sizeof(struct i40e_q_vector) * vsi->num_q_vectors;
		buflen += sizeof(struct i40e_ring) * 2 * vsi->num_queue_pairs;
		buflen += sizeof(struct i40e_tx_buffer) * vsi->num_queue_pairs;
		buflen += sizeof(struct i40e_rx_buffer) * vsi->num_queue_pairs;
		list_for_each_entry(f, &vsi->mac_filter_list, list)
			filter_count++;
		buflen += sizeof(struct i40e_mac_filter) * filter_count;

		if (i40e_dbg_prep_dump_buf(pf, buflen)) {
			p = i40e_dbg_dump_buf;
			seid_found = true;

			len = sizeof(struct i40e_vsi);
			memcpy(p, vsi, len);
			p += len;

			if (vsi->num_q_vectors) {
				len = (sizeof(struct i40e_q_vector)
					* vsi->num_q_vectors);
				memcpy(p, vsi->q_vectors, len);
				p += len;
			}

			if (vsi->num_queue_pairs) {
				len = (sizeof(struct i40e_ring) *
				      vsi->num_queue_pairs);
				memcpy(p, vsi->tx_rings, len);
				p += len;
				memcpy(p, vsi->rx_rings, len);
				p += len;
			}

			if (vsi->tx_rings[0]) {
				len = sizeof(struct i40e_tx_buffer);
				for (i = 0; i < vsi->num_queue_pairs; i++) {
					memcpy(p, vsi->tx_rings[i]->tx_bi, len);
					p += len;
				}
				len = sizeof(struct i40e_rx_buffer);
				for (i = 0; i < vsi->num_queue_pairs; i++) {
					memcpy(p, vsi->rx_rings[i]->rx_bi, len);
					p += len;
				}
			}

			/* macvlan filter list */
			len = sizeof(struct i40e_mac_filter);
			list_for_each_entry(f, &vsi->mac_filter_list, list) {
				memcpy(p, f, len);
				p += len;
			}

			i40e_dbg_dump_data_len = buflen;
			dev_info(&pf->pdev->dev,
				 "VSI seid %ld dumped %d bytes\n",
				 seid, (int)i40e_dbg_dump_data_len);
		}
		mutex_unlock(&pf->switch_mutex);
	} else if (seid >= I40E_BASE_VEB_SEID) {
		struct i40e_veb *veb = NULL;

		mutex_lock(&pf->switch_mutex);
		veb = i40e_dbg_find_veb(pf, seid);
		if (!veb) {
			mutex_unlock(&pf->switch_mutex);
			goto write_exit;
		}

		buflen = sizeof(struct i40e_veb);
		if (i40e_dbg_prep_dump_buf(pf, buflen)) {
			seid_found = true;
			memcpy(i40e_dbg_dump_buf, veb, buflen);
			i40e_dbg_dump_data_len = buflen;
			dev_info(&pf->pdev->dev,
				 "VEB seid %ld dumped %d bytes\n",
				 seid, (int)i40e_dbg_dump_data_len);
		}
		mutex_unlock(&pf->switch_mutex);
	}

write_exit:
	if (!seid_found)
		dev_info(&pf->pdev->dev, "unknown seid %ld\n", seid);

	return count;
}

static const struct file_operations i40e_dbg_dump_fops = {
	.owner = THIS_MODULE,
	.open =  simple_open,
	.read =  i40e_dbg_dump_read,
	.write = i40e_dbg_dump_write,
};

/**************************************************************
 * command
 * The command entry in debugfs is for giving the driver commands
 * to be executed - these may be for changing the internal switch
 * setup, adding or removing filters, or other things.  Many of
 * these will be useful for some forms of unit testing.
 **************************************************************/
static char i40e_dbg_command_buf[256] = "hello world";

/**
 * i40e_dbg_command_read - read for command datum
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_command_read(struct file *filp, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	int buf_size = 256;
	char *buf;
	int len;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;
	if (count < buf_size)
		return -ENOSPC;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOSPC;

	len = snprintf(buf, buf_size, "%s: %s\n",
		       pf->vsi[pf->lan_vsi]->netdev->name,
		       i40e_dbg_command_buf);

	bytes_not_copied = copy_to_user(buffer, buf, len);
	kfree(buf);

	if (bytes_not_copied < 0)
		return bytes_not_copied;

	*ppos = len;
	return len;
}

/**
 * i40e_dbg_dump_vsi_seid - handles dump vsi seid write into command datum
 * @pf: the i40e_pf created in command write
 * @seid: the seid the user put in
 **/
static void i40e_dbg_dump_vsi_seid(struct i40e_pf *pf, int seid)
{
	struct rtnl_link_stats64 *nstat;
	struct i40e_mac_filter *f;
	struct i40e_vsi *vsi;
	int i;

	vsi = i40e_dbg_find_vsi(pf, seid);
	if (!vsi) {
		dev_info(&pf->pdev->dev,
			 "dump %d: seid not found\n", seid);
		return;
	}
	dev_info(&pf->pdev->dev, "vsi seid %d\n", seid);
	if (vsi->netdev)
		dev_info(&pf->pdev->dev,
			 "    netdev: name = %s\n",
			 vsi->netdev->name);
	if (vsi->active_vlans)
		dev_info(&pf->pdev->dev,
			 "    vlgrp: & = %p\n", vsi->active_vlans);
	dev_info(&pf->pdev->dev,
		 "    netdev_registered = %i, current_netdev_flags = 0x%04x, state = %li flags = 0x%08lx\n",
		 vsi->netdev_registered,
		 vsi->current_netdev_flags, vsi->state, vsi->flags);
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		dev_info(&pf->pdev->dev,
			 "    mac_filter_list: %pM vid=%d, is_netdev=%d is_vf=%d counter=%d\n",
			 f->macaddr, f->vlan, f->is_netdev, f->is_vf,
			 f->counter);
	}
	nstat = i40e_get_vsi_stats_struct(vsi);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_packets = %lu, rx_bytes = %lu, rx_errors = %lu, rx_dropped = %lu\n",
		 (long unsigned int)nstat->rx_packets,
		 (long unsigned int)nstat->rx_bytes,
		 (long unsigned int)nstat->rx_errors,
		 (long unsigned int)nstat->rx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_packets = %lu, tx_bytes = %lu, tx_errors = %lu, tx_dropped = %lu\n",
		 (long unsigned int)nstat->tx_packets,
		 (long unsigned int)nstat->tx_bytes,
		 (long unsigned int)nstat->tx_errors,
		 (long unsigned int)nstat->tx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats: multicast = %lu, collisions = %lu\n",
		 (long unsigned int)nstat->multicast,
		 (long unsigned int)nstat->collisions);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_length_errors = %lu, rx_over_errors = %lu, rx_crc_errors = %lu\n",
		 (long unsigned int)nstat->rx_length_errors,
		 (long unsigned int)nstat->rx_over_errors,
		 (long unsigned int)nstat->rx_crc_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_frame_errors = %lu, rx_fifo_errors = %lu, rx_missed_errors = %lu\n",
		 (long unsigned int)nstat->rx_frame_errors,
		 (long unsigned int)nstat->rx_fifo_errors,
		 (long unsigned int)nstat->rx_missed_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_aborted_errors = %lu, tx_carrier_errors = %lu, tx_fifo_errors = %lu\n",
		 (long unsigned int)nstat->tx_aborted_errors,
		 (long unsigned int)nstat->tx_carrier_errors,
		 (long unsigned int)nstat->tx_fifo_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_heartbeat_errors = %lu, tx_window_errors = %lu\n",
		 (long unsigned int)nstat->tx_heartbeat_errors,
		 (long unsigned int)nstat->tx_window_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_compressed = %lu, tx_compressed = %lu\n",
		 (long unsigned int)nstat->rx_compressed,
		 (long unsigned int)nstat->tx_compressed);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_packets = %lu, rx_bytes = %lu, rx_errors = %lu, rx_dropped = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.rx_packets,
		 (long unsigned int)vsi->net_stats_offsets.rx_bytes,
		 (long unsigned int)vsi->net_stats_offsets.rx_errors,
		 (long unsigned int)vsi->net_stats_offsets.rx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_packets = %lu, tx_bytes = %lu, tx_errors = %lu, tx_dropped = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.tx_packets,
		 (long unsigned int)vsi->net_stats_offsets.tx_bytes,
		 (long unsigned int)vsi->net_stats_offsets.tx_errors,
		 (long unsigned int)vsi->net_stats_offsets.tx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: multicast = %lu, collisions = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.multicast,
		 (long unsigned int)vsi->net_stats_offsets.collisions);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_length_errors = %lu, rx_over_errors = %lu, rx_crc_errors = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.rx_length_errors,
		 (long unsigned int)vsi->net_stats_offsets.rx_over_errors,
		 (long unsigned int)vsi->net_stats_offsets.rx_crc_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_frame_errors = %lu, rx_fifo_errors = %lu, rx_missed_errors = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.rx_frame_errors,
		 (long unsigned int)vsi->net_stats_offsets.rx_fifo_errors,
		 (long unsigned int)vsi->net_stats_offsets.rx_missed_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_aborted_errors = %lu, tx_carrier_errors = %lu, tx_fifo_errors = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.tx_aborted_errors,
		 (long unsigned int)vsi->net_stats_offsets.tx_carrier_errors,
		 (long unsigned int)vsi->net_stats_offsets.tx_fifo_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_heartbeat_errors = %lu, tx_window_errors = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.tx_heartbeat_errors,
		 (long unsigned int)vsi->net_stats_offsets.tx_window_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_compressed = %lu, tx_compressed = %lu\n",
		 (long unsigned int)vsi->net_stats_offsets.rx_compressed,
		 (long unsigned int)vsi->net_stats_offsets.tx_compressed);
	dev_info(&pf->pdev->dev,
		 "    tx_restart = %d, tx_busy = %d, rx_buf_failed = %d, rx_page_failed = %d\n",
		 vsi->tx_restart, vsi->tx_busy,
		 vsi->rx_buf_failed, vsi->rx_page_failed);
	rcu_read_lock();
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		struct i40e_ring *rx_ring = ACCESS_ONCE(vsi->rx_rings[i]);
		if (!rx_ring)
			continue;

		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: desc = %p\n",
			 i, rx_ring->desc);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: dev = %p, netdev = %p, rx_bi = %p\n",
			 i, rx_ring->dev,
			 rx_ring->netdev,
			 rx_ring->rx_bi);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: state = %li, queue_index = %d, reg_idx = %d\n",
			 i, rx_ring->state,
			 rx_ring->queue_index,
			 rx_ring->reg_idx);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_hdr_len = %d, rx_buf_len = %d, dtype = %d\n",
			 i, rx_ring->rx_hdr_len,
			 rx_ring->rx_buf_len,
			 rx_ring->dtype);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: hsplit = %d, next_to_use = %d, next_to_clean = %d, ring_active = %i\n",
			 i, rx_ring->hsplit,
			 rx_ring->next_to_use,
			 rx_ring->next_to_clean,
			 rx_ring->ring_active);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_stats: packets = %lld, bytes = %lld, non_eop_descs = %lld\n",
			 i, rx_ring->stats.packets,
			 rx_ring->stats.bytes,
			 rx_ring->rx_stats.non_eop_descs);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_stats: alloc_page_failed = %lld, alloc_buff_failed = %lld\n",
			 i,
			 rx_ring->rx_stats.alloc_page_failed,
			 rx_ring->rx_stats.alloc_buff_failed);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: size = %i, dma = 0x%08lx\n",
			 i, rx_ring->size,
			 (long unsigned int)rx_ring->dma);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: vsi = %p, q_vector = %p\n",
			 i, rx_ring->vsi,
			 rx_ring->q_vector);
	}
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		struct i40e_ring *tx_ring = ACCESS_ONCE(vsi->tx_rings[i]);
		if (!tx_ring)
			continue;

		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: desc = %p\n",
			 i, tx_ring->desc);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: dev = %p, netdev = %p, tx_bi = %p\n",
			 i, tx_ring->dev,
			 tx_ring->netdev,
			 tx_ring->tx_bi);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: state = %li, queue_index = %d, reg_idx = %d\n",
			 i, tx_ring->state,
			 tx_ring->queue_index,
			 tx_ring->reg_idx);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: dtype = %d\n",
			 i, tx_ring->dtype);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: hsplit = %d, next_to_use = %d, next_to_clean = %d, ring_active = %i\n",
			 i, tx_ring->hsplit,
			 tx_ring->next_to_use,
			 tx_ring->next_to_clean,
			 tx_ring->ring_active);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: tx_stats: packets = %lld, bytes = %lld, restart_queue = %lld\n",
			 i, tx_ring->stats.packets,
			 tx_ring->stats.bytes,
			 tx_ring->tx_stats.restart_queue);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: tx_stats: tx_busy = %lld, tx_done_old = %lld\n",
			 i,
			 tx_ring->tx_stats.tx_busy,
			 tx_ring->tx_stats.tx_done_old);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: size = %i, dma = 0x%08lx\n",
			 i, tx_ring->size,
			 (long unsigned int)tx_ring->dma);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: vsi = %p, q_vector = %p\n",
			 i, tx_ring->vsi,
			 tx_ring->q_vector);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: DCB tc = %d\n",
			 i, tx_ring->dcb_tc);
	}
	rcu_read_unlock();
	dev_info(&pf->pdev->dev,
		 "    work_limit = %d, rx_itr_setting = %d (%s), tx_itr_setting = %d (%s)\n",
		 vsi->work_limit, vsi->rx_itr_setting,
		 ITR_IS_DYNAMIC(vsi->rx_itr_setting) ? "dynamic" : "fixed",
		 vsi->tx_itr_setting,
		 ITR_IS_DYNAMIC(vsi->tx_itr_setting) ? "dynamic" : "fixed");
	dev_info(&pf->pdev->dev,
		 "    max_frame = %d, rx_hdr_len = %d, rx_buf_len = %d dtype = %d\n",
		 vsi->max_frame, vsi->rx_hdr_len, vsi->rx_buf_len, vsi->dtype);
	dev_info(&pf->pdev->dev,
		 "    num_q_vectors = %i, base_vector = %i\n",
		 vsi->num_q_vectors, vsi->base_vector);
	dev_info(&pf->pdev->dev,
		 "    seid = %d, id = %d, uplink_seid = %d\n",
		 vsi->seid, vsi->id, vsi->uplink_seid);
	dev_info(&pf->pdev->dev,
		 "    base_queue = %d, num_queue_pairs = %d, num_desc = %d\n",
		 vsi->base_queue, vsi->num_queue_pairs, vsi->num_desc);
	dev_info(&pf->pdev->dev, "    type = %i\n", vsi->type);
	dev_info(&pf->pdev->dev,
		 "    info: valid_sections = 0x%04x, switch_id = 0x%04x\n",
		 vsi->info.valid_sections, vsi->info.switch_id);
	dev_info(&pf->pdev->dev,
		 "    info: sw_reserved[] = 0x%02x 0x%02x\n",
		 vsi->info.sw_reserved[0], vsi->info.sw_reserved[1]);
	dev_info(&pf->pdev->dev,
		 "    info: sec_flags = 0x%02x, sec_reserved = 0x%02x\n",
		 vsi->info.sec_flags, vsi->info.sec_reserved);
	dev_info(&pf->pdev->dev,
		 "    info: pvid = 0x%04x, fcoe_pvid = 0x%04x, port_vlan_flags = 0x%02x\n",
		 vsi->info.pvid, vsi->info.fcoe_pvid,
		 vsi->info.port_vlan_flags);
	dev_info(&pf->pdev->dev,
		 "    info: pvlan_reserved[] = 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.pvlan_reserved[0], vsi->info.pvlan_reserved[1],
		 vsi->info.pvlan_reserved[2]);
	dev_info(&pf->pdev->dev,
		 "    info: ingress_table = 0x%08x, egress_table = 0x%08x\n",
		 vsi->info.ingress_table, vsi->info.egress_table);
	dev_info(&pf->pdev->dev,
		 "    info: cas_pv_stag = 0x%04x, cas_pv_flags= 0x%02x, cas_pv_reserved = 0x%02x\n",
		 vsi->info.cas_pv_tag, vsi->info.cas_pv_flags,
		 vsi->info.cas_pv_reserved);
	dev_info(&pf->pdev->dev,
		 "    info: queue_mapping[0..7 ] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.queue_mapping[0], vsi->info.queue_mapping[1],
		 vsi->info.queue_mapping[2], vsi->info.queue_mapping[3],
		 vsi->info.queue_mapping[4], vsi->info.queue_mapping[5],
		 vsi->info.queue_mapping[6], vsi->info.queue_mapping[7]);
	dev_info(&pf->pdev->dev,
		 "    info: queue_mapping[8..15] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.queue_mapping[8], vsi->info.queue_mapping[9],
		 vsi->info.queue_mapping[10], vsi->info.queue_mapping[11],
		 vsi->info.queue_mapping[12], vsi->info.queue_mapping[13],
		 vsi->info.queue_mapping[14], vsi->info.queue_mapping[15]);
	dev_info(&pf->pdev->dev,
		 "    info: tc_mapping[] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.tc_mapping[0], vsi->info.tc_mapping[1],
		 vsi->info.tc_mapping[2], vsi->info.tc_mapping[3],
		 vsi->info.tc_mapping[4], vsi->info.tc_mapping[5],
		 vsi->info.tc_mapping[6], vsi->info.tc_mapping[7]);
	dev_info(&pf->pdev->dev,
		 "    info: queueing_opt_flags = 0x%02x  queueing_opt_reserved[0..2] = 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.queueing_opt_flags,
		 vsi->info.queueing_opt_reserved[0],
		 vsi->info.queueing_opt_reserved[1],
		 vsi->info.queueing_opt_reserved[2]);
	dev_info(&pf->pdev->dev,
		 "    info: up_enable_bits = 0x%02x\n",
		 vsi->info.up_enable_bits);
	dev_info(&pf->pdev->dev,
		 "    info: sched_reserved = 0x%02x, outer_up_table = 0x%04x\n",
		 vsi->info.sched_reserved, vsi->info.outer_up_table);
	dev_info(&pf->pdev->dev,
		 "    info: cmd_reserved[] = 0x%02x 0x%02x 0x%02x 0x0%02x 0x%02x 0x%02x 0x%02x 0x0%02x\n",
		 vsi->info.cmd_reserved[0], vsi->info.cmd_reserved[1],
		 vsi->info.cmd_reserved[2], vsi->info.cmd_reserved[3],
		 vsi->info.cmd_reserved[4], vsi->info.cmd_reserved[5],
		 vsi->info.cmd_reserved[6], vsi->info.cmd_reserved[7]);
	dev_info(&pf->pdev->dev,
		 "    info: qs_handle[] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.qs_handle[0], vsi->info.qs_handle[1],
		 vsi->info.qs_handle[2], vsi->info.qs_handle[3],
		 vsi->info.qs_handle[4], vsi->info.qs_handle[5],
		 vsi->info.qs_handle[6], vsi->info.qs_handle[7]);
	dev_info(&pf->pdev->dev,
		 "    info: stat_counter_idx = 0x%04x, sched_id = 0x%04x\n",
		 vsi->info.stat_counter_idx, vsi->info.sched_id);
	dev_info(&pf->pdev->dev,
		 "    info: resp_reserved[] = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.resp_reserved[0], vsi->info.resp_reserved[1],
		 vsi->info.resp_reserved[2], vsi->info.resp_reserved[3],
		 vsi->info.resp_reserved[4], vsi->info.resp_reserved[5],
		 vsi->info.resp_reserved[6], vsi->info.resp_reserved[7],
		 vsi->info.resp_reserved[8], vsi->info.resp_reserved[9],
		 vsi->info.resp_reserved[10], vsi->info.resp_reserved[11]);
	if (vsi->back)
		dev_info(&pf->pdev->dev, "    pf = %p\n", vsi->back);
	dev_info(&pf->pdev->dev, "    idx = %d\n", vsi->idx);
	dev_info(&pf->pdev->dev,
		 "    tc_config: numtc = %d, enabled_tc = 0x%x\n",
		 vsi->tc_config.numtc, vsi->tc_config.enabled_tc);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev,
			 "    tc_config: tc = %d, qoffset = %d, qcount = %d, netdev_tc = %d\n",
			 i, vsi->tc_config.tc_info[i].qoffset,
			 vsi->tc_config.tc_info[i].qcount,
			 vsi->tc_config.tc_info[i].netdev_tc);
	}
	dev_info(&pf->pdev->dev,
		 "    bw: bw_limit = %d, bw_max_quanta = %d\n",
		 vsi->bw_limit, vsi->bw_max_quanta);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev,
			 "    bw[%d]: ets_share_credits = %d, ets_limit_credits = %d, max_quanta = %d\n",
			 i, vsi->bw_ets_share_credits[i],
			 vsi->bw_ets_limit_credits[i],
			 vsi->bw_ets_max_quanta[i]);
	}
#ifdef I40E_FCOE
	if (vsi->type == I40E_VSI_FCOE) {
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: rx_packets = %llu, rx_dwords = %llu, rx_dropped = %llu\n",
			 vsi->fcoe_stats.rx_fcoe_packets,
			 vsi->fcoe_stats.rx_fcoe_dwords,
			 vsi->fcoe_stats.rx_fcoe_dropped);
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: tx_packets = %llu, tx_dwords = %llu\n",
			 vsi->fcoe_stats.tx_fcoe_packets,
			 vsi->fcoe_stats.tx_fcoe_dwords);
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: bad_crc = %llu, last_error = %llu\n",
			 vsi->fcoe_stats.fcoe_bad_fccrc,
			 vsi->fcoe_stats.fcoe_last_error);
		dev_info(&pf->pdev->dev, "    fcoe_stats: ddp_count = %llu\n",
			 vsi->fcoe_stats.fcoe_ddp_count);
	}
#endif
}

/**
 * i40e_dbg_dump_aq_desc - handles dump aq_desc write into command datum
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_aq_desc(struct i40e_pf *pf)
{
	struct i40e_adminq_ring *ring;
	struct i40e_hw *hw = &pf->hw;
	char hdr[32];
	int i;

	snprintf(hdr, sizeof(hdr), "%s %s:         ",
		 dev_driver_string(&pf->pdev->dev),
		 dev_name(&pf->pdev->dev));

	/* first the send (command) ring, then the receive (event) ring */
	dev_info(&pf->pdev->dev, "AdminQ Tx Ring\n");
	ring = &(hw->aq.asq);
	for (i = 0; i < ring->count; i++) {
		struct i40e_aq_desc *d = I40E_ADMINQ_DESC(*ring, i);
		dev_info(&pf->pdev->dev,
			 "   at[%02d] flags=0x%04x op=0x%04x dlen=0x%04x ret=0x%04x cookie_h=0x%08x cookie_l=0x%08x\n",
			 i, d->flags, d->opcode, d->datalen, d->retval,
			 d->cookie_high, d->cookie_low);
		print_hex_dump(KERN_INFO, hdr, DUMP_PREFIX_NONE,
			       16, 1, d->params.raw, 16, 0);
	}

	dev_info(&pf->pdev->dev, "AdminQ Rx Ring\n");
	ring = &(hw->aq.arq);
	for (i = 0; i < ring->count; i++) {
		struct i40e_aq_desc *d = I40E_ADMINQ_DESC(*ring, i);
		dev_info(&pf->pdev->dev,
			 "   ar[%02d] flags=0x%04x op=0x%04x dlen=0x%04x ret=0x%04x cookie_h=0x%08x cookie_l=0x%08x\n",
			 i, d->flags, d->opcode, d->datalen, d->retval,
			 d->cookie_high, d->cookie_low);
		print_hex_dump(KERN_INFO, hdr, DUMP_PREFIX_NONE,
			       16, 1, d->params.raw, 16, 0);
	}
}

/**
 * i40e_dbg_dump_desc - handles dump desc write into command datum
 * @cnt: number of arguments that the user supplied
 * @vsi_seid: vsi id entered by user
 * @ring_id: ring id entered by user
 * @desc_n: descriptor number entered by user
 * @pf: the i40e_pf created in command write
 * @is_rx_ring: true if rx, false if tx
 **/
static void i40e_dbg_dump_desc(int cnt, int vsi_seid, int ring_id, int desc_n,
			       struct i40e_pf *pf, bool is_rx_ring)
{
	struct i40e_tx_desc *txd;
	union i40e_rx_desc *rxd;
	struct i40e_ring ring;
	struct i40e_vsi *vsi;
	int i;

	vsi = i40e_dbg_find_vsi(pf, vsi_seid);
	if (!vsi) {
		dev_info(&pf->pdev->dev, "vsi %d not found\n", vsi_seid);
		return;
	}
	if (ring_id >= vsi->num_queue_pairs || ring_id < 0) {
		dev_info(&pf->pdev->dev, "ring %d not found\n", ring_id);
		return;
	}
	if (!vsi->tx_rings || !vsi->tx_rings[0]->desc) {
		dev_info(&pf->pdev->dev,
			 "descriptor rings have not been allocated for vsi %d\n",
			 vsi_seid);
		return;
	}
	if (is_rx_ring)
		ring = *vsi->rx_rings[ring_id];
	else
		ring = *vsi->tx_rings[ring_id];
	if (cnt == 2) {
		dev_info(&pf->pdev->dev, "vsi = %02i %s ring = %02i\n",
			 vsi_seid, is_rx_ring ? "rx" : "tx", ring_id);
		for (i = 0; i < ring.count; i++) {
			if (!is_rx_ring) {
				txd = I40E_TX_DESC(&ring, i);
				dev_info(&pf->pdev->dev,
					 "   d[%03i] = 0x%016llx 0x%016llx\n",
					 i, txd->buffer_addr,
					 txd->cmd_type_offset_bsz);
			} else if (sizeof(union i40e_rx_desc) ==
				   sizeof(union i40e_16byte_rx_desc)) {
				rxd = I40E_RX_DESC(&ring, i);
				dev_info(&pf->pdev->dev,
					 "   d[%03i] = 0x%016llx 0x%016llx\n",
					 i, rxd->read.pkt_addr,
					 rxd->read.hdr_addr);
			} else {
				rxd = I40E_RX_DESC(&ring, i);
				dev_info(&pf->pdev->dev,
					 "   d[%03i] = 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
					 i, rxd->read.pkt_addr,
					 rxd->read.hdr_addr,
					 rxd->read.rsvd1, rxd->read.rsvd2);
			}
		}
	} else if (cnt == 3) {
		if (desc_n >= ring.count || desc_n < 0) {
			dev_info(&pf->pdev->dev,
				 "descriptor %d not found\n", desc_n);
			return;
		}
		if (!is_rx_ring) {
			txd = I40E_TX_DESC(&ring, desc_n);
			dev_info(&pf->pdev->dev,
				 "vsi = %02i tx ring = %02i d[%03i] = 0x%016llx 0x%016llx\n",
				 vsi_seid, ring_id, desc_n,
				 txd->buffer_addr, txd->cmd_type_offset_bsz);
		} else if (sizeof(union i40e_rx_desc) ==
			   sizeof(union i40e_16byte_rx_desc)) {
			rxd = I40E_RX_DESC(&ring, desc_n);
			dev_info(&pf->pdev->dev,
				 "vsi = %02i rx ring = %02i d[%03i] = 0x%016llx 0x%016llx\n",
				 vsi_seid, ring_id, desc_n,
				 rxd->read.pkt_addr, rxd->read.hdr_addr);
		} else {
			rxd = I40E_RX_DESC(&ring, desc_n);
			dev_info(&pf->pdev->dev,
				 "vsi = %02i rx ring = %02i d[%03i] = 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
				 vsi_seid, ring_id, desc_n,
				 rxd->read.pkt_addr, rxd->read.hdr_addr,
				 rxd->read.rsvd1, rxd->read.rsvd2);
		}
	} else {
		dev_info(&pf->pdev->dev, "dump desc rx/tx <vsi_seid> <ring_id> [<desc_n>]\n");
	}
}

/**
 * i40e_dbg_dump_vsi_no_seid - handles dump vsi write into command datum
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_vsi_no_seid(struct i40e_pf *pf)
{
	int i;

	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i])
			dev_info(&pf->pdev->dev, "dump vsi[%d]: %d\n",
				 i, pf->vsi[i]->seid);
}

/**
 * i40e_dbg_dump_stats - handles dump stats write into command datum
 * @pf: the i40e_pf created in command write
 * @estats: the eth stats structure to be dumped
 **/
static void i40e_dbg_dump_eth_stats(struct i40e_pf *pf,
				    struct i40e_eth_stats *estats)
{
	dev_info(&pf->pdev->dev, "  ethstats:\n");
	dev_info(&pf->pdev->dev,
		 "    rx_bytes = \t%lld \trx_unicast = \t\t%lld \trx_multicast = \t%lld\n",
		estats->rx_bytes, estats->rx_unicast, estats->rx_multicast);
	dev_info(&pf->pdev->dev,
		 "    rx_broadcast = \t%lld \trx_discards = \t\t%lld\n",
		 estats->rx_broadcast, estats->rx_discards);
	dev_info(&pf->pdev->dev,
		 "    rx_unknown_protocol = \t%lld \ttx_bytes = \t%lld\n",
		 estats->rx_unknown_protocol, estats->tx_bytes);
	dev_info(&pf->pdev->dev,
		 "    tx_unicast = \t%lld \ttx_multicast = \t\t%lld \ttx_broadcast = \t%lld\n",
		 estats->tx_unicast, estats->tx_multicast, estats->tx_broadcast);
	dev_info(&pf->pdev->dev,
		 "    tx_discards = \t%lld \ttx_errors = \t\t%lld\n",
		 estats->tx_discards, estats->tx_errors);
}

/**
 * i40e_dbg_dump_veb_seid - handles dump stats of a single given veb
 * @pf: the i40e_pf created in command write
 * @seid: the seid the user put in
 **/
static void i40e_dbg_dump_veb_seid(struct i40e_pf *pf, int seid)
{
	struct i40e_veb *veb;

	if ((seid < I40E_BASE_VEB_SEID) ||
	    (seid >= (I40E_MAX_VEB + I40E_BASE_VEB_SEID))) {
		dev_info(&pf->pdev->dev, "%d: bad seid\n", seid);
		return;
	}

	veb = i40e_dbg_find_veb(pf, seid);
	if (!veb) {
		dev_info(&pf->pdev->dev, "can't find veb %d\n", seid);
		return;
	}
	dev_info(&pf->pdev->dev,
		 "veb idx=%d,%d stats_ic=%d  seid=%d uplink=%d\n",
		 veb->idx, veb->veb_idx, veb->stats_idx, veb->seid,
		 veb->uplink_seid);
	i40e_dbg_dump_eth_stats(pf, &veb->stats);
}

/**
 * i40e_dbg_dump_veb_all - dumps all known veb's stats
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_veb_all(struct i40e_pf *pf)
{
	struct i40e_veb *veb;
	int i;

	for (i = 0; i < I40E_MAX_VEB; i++) {
		veb = pf->veb[i];
		if (veb)
			i40e_dbg_dump_veb_seid(pf, veb->seid);
	}
}

/**
 * i40e_dbg_cmd_fd_ctrl - Enable/disable FD sideband/ATR
 * @pf: the pf that would be altered
 * @flag: flag that needs enabling or disabling
 * @enable: Enable/disable FD SD/ATR
 **/
static void i40e_dbg_cmd_fd_ctrl(struct i40e_pf *pf, u64 flag, bool enable)
{
	if (enable) {
		pf->flags |= flag;
	} else {
		pf->flags &= ~flag;
		pf->auto_disable_flags |= flag;
	}
	dev_info(&pf->pdev->dev, "requesting a pf reset\n");
	i40e_do_reset_safe(pf, (1 << __I40E_PF_RESET_REQUESTED));
}

#define I40E_MAX_DEBUG_OUT_BUFFER (4096*4)
/**
 * i40e_dbg_command_write - write into command datum
 * @filp: the opened file
 * @buffer: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_command_write(struct file *filp,
				      const char __user *buffer,
				      size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	char *cmd_buf, *cmd_buf_tmp;
	int bytes_not_copied;
	struct i40e_vsi *vsi;
	int vsi_seid;
	int veb_seid;
	int cnt;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!cmd_buf)
		return count;
	bytes_not_copied = copy_from_user(cmd_buf, buffer, count);
	if (bytes_not_copied < 0)
		return bytes_not_copied;
	if (bytes_not_copied > 0)
		count -= bytes_not_copied;
	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	if (strncmp(cmd_buf, "add vsi", 7) == 0) {
		vsi_seid = -1;
		cnt = sscanf(&cmd_buf[7], "%i", &vsi_seid);
		if (cnt == 0) {
			/* default to PF VSI */
			vsi_seid = pf->vsi[pf->lan_vsi]->seid;
		} else if (vsi_seid < 0) {
			dev_info(&pf->pdev->dev, "add VSI %d: bad vsi seid\n",
				 vsi_seid);
			goto command_write_done;
		}

		vsi = i40e_vsi_setup(pf, I40E_VSI_VMDQ2, vsi_seid, 0);
		if (vsi)
			dev_info(&pf->pdev->dev, "added VSI %d to relay %d\n",
				 vsi->seid, vsi->uplink_seid);
		else
			dev_info(&pf->pdev->dev, "'%s' failed\n", cmd_buf);

	} else if (strncmp(cmd_buf, "del vsi", 7) == 0) {
		sscanf(&cmd_buf[7], "%i", &vsi_seid);
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "del VSI %d: seid not found\n",
				 vsi_seid);
			goto command_write_done;
		}

		dev_info(&pf->pdev->dev, "deleting VSI %d\n", vsi_seid);
		i40e_vsi_release(vsi);

	} else if (strncmp(cmd_buf, "add relay", 9) == 0) {
		struct i40e_veb *veb;
		int uplink_seid, i;

		cnt = sscanf(&cmd_buf[9], "%i %i", &uplink_seid, &vsi_seid);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add relay: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		} else if (uplink_seid < 0) {
			dev_info(&pf->pdev->dev,
				 "add relay %d: bad uplink seid\n",
				 uplink_seid);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "add relay: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i] && pf->veb[i]->seid == uplink_seid)
				break;
		if (i >= I40E_MAX_VEB && uplink_seid != 0 &&
		    uplink_seid != pf->mac_seid) {
			dev_info(&pf->pdev->dev,
				 "add relay: relay uplink %d not found\n",
				 uplink_seid);
			goto command_write_done;
		}

		veb = i40e_veb_setup(pf, 0, uplink_seid, vsi_seid,
				     vsi->tc_config.enabled_tc);
		if (veb)
			dev_info(&pf->pdev->dev, "added relay %d\n", veb->seid);
		else
			dev_info(&pf->pdev->dev, "add relay failed\n");

	} else if (strncmp(cmd_buf, "del relay", 9) == 0) {
		int i;
		cnt = sscanf(&cmd_buf[9], "%i", &veb_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "del relay: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		} else if (veb_seid < 0) {
			dev_info(&pf->pdev->dev,
				 "del relay %d: bad relay seid\n", veb_seid);
			goto command_write_done;
		}

		/* find the veb */
		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i] && pf->veb[i]->seid == veb_seid)
				break;
		if (i >= I40E_MAX_VEB) {
			dev_info(&pf->pdev->dev,
				 "del relay: relay %d not found\n", veb_seid);
			goto command_write_done;
		}

		dev_info(&pf->pdev->dev, "deleting relay %d\n", veb_seid);
		i40e_veb_release(pf->veb[i]);

	} else if (strncmp(cmd_buf, "add macaddr", 11) == 0) {
		struct i40e_mac_filter *f;
		int vlan = 0;
		u8 ma[6];
		int ret;

		cnt = sscanf(&cmd_buf[11],
			     "%i %hhx:%hhx:%hhx:%hhx:%hhx:%hhx %i",
			     &vsi_seid,
			     &ma[0], &ma[1], &ma[2], &ma[3], &ma[4], &ma[5],
			     &vlan);
		if (cnt == 7) {
			vlan = 0;
		} else if (cnt != 8) {
			dev_info(&pf->pdev->dev,
				 "add macaddr: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "add macaddr: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		f = i40e_add_filter(vsi, ma, vlan, false, false);
		ret = i40e_sync_vsi_filters(vsi);
		if (f && !ret)
			dev_info(&pf->pdev->dev,
				 "add macaddr: %pM vlan=%d added to VSI %d\n",
				 ma, vlan, vsi_seid);
		else
			dev_info(&pf->pdev->dev,
				 "add macaddr: %pM vlan=%d to VSI %d failed, f=%p ret=%d\n",
				 ma, vlan, vsi_seid, f, ret);

	} else if (strncmp(cmd_buf, "del macaddr", 11) == 0) {
		int vlan = 0;
		u8 ma[6];
		int ret;

		cnt = sscanf(&cmd_buf[11],
			     "%i %hhx:%hhx:%hhx:%hhx:%hhx:%hhx %i",
			     &vsi_seid,
			     &ma[0], &ma[1], &ma[2], &ma[3], &ma[4], &ma[5],
			     &vlan);
		if (cnt == 7) {
			vlan = 0;
		} else if (cnt != 8) {
			dev_info(&pf->pdev->dev,
				 "del macaddr: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "del macaddr: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		i40e_del_filter(vsi, ma, vlan, false, false);
		ret = i40e_sync_vsi_filters(vsi);
		if (!ret)
			dev_info(&pf->pdev->dev,
				 "del macaddr: %pM vlan=%d removed from VSI %d\n",
				 ma, vlan, vsi_seid);
		else
			dev_info(&pf->pdev->dev,
				 "del macaddr: %pM vlan=%d from VSI %d failed, ret=%d\n",
				 ma, vlan, vsi_seid, ret);

	} else if (strncmp(cmd_buf, "add pvid", 8) == 0) {
		i40e_status ret;
		u16 vid;
		unsigned int v;

		cnt = sscanf(&cmd_buf[8], "%i %u", &vsi_seid, &v);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add pvid: bad command string, cnt=%d\n", cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "add pvid: VSI %d not found\n",
				 vsi_seid);
			goto command_write_done;
		}

		vid = v;
		ret = i40e_vsi_add_pvid(vsi, vid);
		if (!ret)
			dev_info(&pf->pdev->dev,
				 "add pvid: %d added to VSI %d\n",
				 vid, vsi_seid);
		else
			dev_info(&pf->pdev->dev,
				 "add pvid: %d to VSI %d failed, ret=%d\n",
				 vid, vsi_seid, ret);

	} else if (strncmp(cmd_buf, "del pvid", 8) == 0) {

		cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "del pvid: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "del pvid: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		i40e_vsi_remove_pvid(vsi);
		dev_info(&pf->pdev->dev,
			 "del pvid: removed from VSI %d\n", vsi_seid);

	} else if (strncmp(cmd_buf, "dump", 4) == 0) {
		if (strncmp(&cmd_buf[5], "switch", 6) == 0) {
			i40e_fetch_switch_configuration(pf, true);
		} else if (strncmp(&cmd_buf[5], "vsi", 3) == 0) {
			cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
			if (cnt > 0)
				i40e_dbg_dump_vsi_seid(pf, vsi_seid);
			else
				i40e_dbg_dump_vsi_no_seid(pf);
		} else if (strncmp(&cmd_buf[5], "veb", 3) == 0) {
			cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
			if (cnt > 0)
				i40e_dbg_dump_veb_seid(pf, vsi_seid);
			else
				i40e_dbg_dump_veb_all(pf);
		} else if (strncmp(&cmd_buf[5], "desc", 4) == 0) {
			int ring_id, desc_n;
			if (strncmp(&cmd_buf[10], "rx", 2) == 0) {
				cnt = sscanf(&cmd_buf[12], "%i %i %i",
					     &vsi_seid, &ring_id, &desc_n);
				i40e_dbg_dump_desc(cnt, vsi_seid, ring_id,
						   desc_n, pf, true);
			} else if (strncmp(&cmd_buf[10], "tx", 2)
					== 0) {
				cnt = sscanf(&cmd_buf[12], "%i %i %i",
					     &vsi_seid, &ring_id, &desc_n);
				i40e_dbg_dump_desc(cnt, vsi_seid, ring_id,
						   desc_n, pf, false);
			} else if (strncmp(&cmd_buf[10], "aq", 2) == 0) {
				i40e_dbg_dump_aq_desc(pf);
			} else {
				dev_info(&pf->pdev->dev,
					 "dump desc tx <vsi_seid> <ring_id> [<desc_n>]\n");
				dev_info(&pf->pdev->dev,
					 "dump desc rx <vsi_seid> <ring_id> [<desc_n>]\n");
				dev_info(&pf->pdev->dev, "dump desc aq\n");
			}
		} else if (strncmp(&cmd_buf[5], "reset stats", 11) == 0) {
			dev_info(&pf->pdev->dev,
				 "core reset count: %d\n", pf->corer_count);
			dev_info(&pf->pdev->dev,
				 "global reset count: %d\n", pf->globr_count);
			dev_info(&pf->pdev->dev,
				 "emp reset count: %d\n", pf->empr_count);
			dev_info(&pf->pdev->dev,
				 "pf reset count: %d\n", pf->pfr_count);
			dev_info(&pf->pdev->dev,
				 "pf tx sluggish count: %d\n",
				 pf->tx_sluggish_count);
		} else if (strncmp(&cmd_buf[5], "port", 4) == 0) {
			struct i40e_aqc_query_port_ets_config_resp *bw_data;
			struct i40e_dcbx_config *cfg =
						&pf->hw.local_dcbx_config;
			struct i40e_dcbx_config *r_cfg =
						&pf->hw.remote_dcbx_config;
			int i, ret;

			bw_data = kzalloc(sizeof(
				    struct i40e_aqc_query_port_ets_config_resp),
					  GFP_KERNEL);
			if (!bw_data) {
				ret = -ENOMEM;
				goto command_write_done;
			}

			ret = i40e_aq_query_port_ets_config(&pf->hw,
							    pf->mac_seid,
							    bw_data, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Query Port ETS Config AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(bw_data);
				bw_data = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev,
				 "port bw: tc_valid=0x%x tc_strict_prio=0x%x, tc_bw_max=0x%04x,0x%04x\n",
				 bw_data->tc_valid_bits,
				 bw_data->tc_strict_priority_bits,
				 le16_to_cpu(bw_data->tc_bw_max[0]),
				 le16_to_cpu(bw_data->tc_bw_max[1]));
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "port bw: tc_bw_share=%d tc_bw_limit=%d\n",
					 bw_data->tc_bw_share_credits[i],
					 le16_to_cpu(bw_data->tc_bw_limits[i]));
			}

			kfree(bw_data);
			bw_data = NULL;

			dev_info(&pf->pdev->dev,
				 "port dcbx_mode=%d\n", cfg->dcbx_mode);
			dev_info(&pf->pdev->dev,
				 "port ets_cfg: willing=%d cbs=%d, maxtcs=%d\n",
				 cfg->etscfg.willing, cfg->etscfg.cbs,
				 cfg->etscfg.maxtcs);
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "port ets_cfg: %d prio_tc=%d tcbw=%d tctsa=%d\n",
					 i, cfg->etscfg.prioritytable[i],
					 cfg->etscfg.tcbwtable[i],
					 cfg->etscfg.tsatable[i]);
			}
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "port ets_rec: %d prio_tc=%d tcbw=%d tctsa=%d\n",
					 i, cfg->etsrec.prioritytable[i],
					 cfg->etsrec.tcbwtable[i],
					 cfg->etsrec.tsatable[i]);
			}
			dev_info(&pf->pdev->dev,
				 "port pfc_cfg: willing=%d mbc=%d, pfccap=%d pfcenable=0x%x\n",
				 cfg->pfc.willing, cfg->pfc.mbc,
				 cfg->pfc.pfccap, cfg->pfc.pfcenable);
			dev_info(&pf->pdev->dev,
				 "port app_table: num_apps=%d\n", cfg->numapps);
			for (i = 0; i < cfg->numapps; i++) {
				dev_info(&pf->pdev->dev, "port app_table: %d prio=%d selector=%d protocol=0x%x\n",
					 i, cfg->app[i].priority,
					 cfg->app[i].selector,
					 cfg->app[i].protocolid);
			}
			/* Peer TLV DCBX data */
			dev_info(&pf->pdev->dev,
				 "remote port ets_cfg: willing=%d cbs=%d, maxtcs=%d\n",
				 r_cfg->etscfg.willing,
				 r_cfg->etscfg.cbs, r_cfg->etscfg.maxtcs);
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "remote port ets_cfg: %d prio_tc=%d tcbw=%d tctsa=%d\n",
					 i, r_cfg->etscfg.prioritytable[i],
					 r_cfg->etscfg.tcbwtable[i],
					 r_cfg->etscfg.tsatable[i]);
			}
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "remote port ets_rec: %d prio_tc=%d tcbw=%d tctsa=%d\n",
					 i, r_cfg->etsrec.prioritytable[i],
					 r_cfg->etsrec.tcbwtable[i],
					 r_cfg->etsrec.tsatable[i]);
			}
			dev_info(&pf->pdev->dev,
				 "remote port pfc_cfg: willing=%d mbc=%d, pfccap=%d pfcenable=0x%x\n",
				 r_cfg->pfc.willing,
				 r_cfg->pfc.mbc,
				 r_cfg->pfc.pfccap,
				 r_cfg->pfc.pfcenable);
			dev_info(&pf->pdev->dev,
				 "remote port app_table: num_apps=%d\n",
				 r_cfg->numapps);
			for (i = 0; i < r_cfg->numapps; i++) {
				dev_info(&pf->pdev->dev, "remote port app_table: %d prio=%d selector=%d protocol=0x%x\n",
					 i, r_cfg->app[i].priority,
					 r_cfg->app[i].selector,
					 r_cfg->app[i].protocolid);
			}
		} else {
			dev_info(&pf->pdev->dev,
				 "dump desc tx <vsi_seid> <ring_id> [<desc_n>], dump desc rx <vsi_seid> <ring_id> [<desc_n>],\n");
			dev_info(&pf->pdev->dev, "dump switch\n");
			dev_info(&pf->pdev->dev, "dump vsi [seid]\n");
			dev_info(&pf->pdev->dev, "dump reset stats\n");
			dev_info(&pf->pdev->dev, "dump port\n");
			dev_info(&pf->pdev->dev,
				 "dump debug fwdata <cluster_id> <table_id> <index>\n");
		}

	} else if (strncmp(cmd_buf, "msg_enable", 10) == 0) {
		u32 level;
		cnt = sscanf(&cmd_buf[10], "%i", &level);
		if (cnt) {
			if (I40E_DEBUG_USER & level) {
				pf->hw.debug_mask = level;
				dev_info(&pf->pdev->dev,
					 "set hw.debug_mask = 0x%08x\n",
					 pf->hw.debug_mask);
			}
			pf->msg_enable = level;
			dev_info(&pf->pdev->dev, "set msg_enable = 0x%08x\n",
				 pf->msg_enable);
		} else {
			dev_info(&pf->pdev->dev, "msg_enable = 0x%08x\n",
				 pf->msg_enable);
		}
	} else if (strncmp(cmd_buf, "pfr", 3) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing PFR\n");
		i40e_do_reset_safe(pf, (1 << __I40E_PF_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "corer", 5) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing CoreR\n");
		i40e_do_reset_safe(pf, (1 << __I40E_CORE_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "globr", 5) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing GlobR\n");
		i40e_do_reset_safe(pf, (1 << __I40E_GLOBAL_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "empr", 4) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing EMPR\n");
		i40e_do_reset_safe(pf, (1 << __I40E_EMP_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "read", 4) == 0) {
		u32 address;
		u32 value;
		cnt = sscanf(&cmd_buf[4], "%i", &address);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "read <reg>\n");
			goto command_write_done;
		}

		/* check the range on address */
		if (address >= I40E_MAX_REGISTER) {
			dev_info(&pf->pdev->dev, "read reg address 0x%08x too large\n",
				 address);
			goto command_write_done;
		}

		value = rd32(&pf->hw, address);
		dev_info(&pf->pdev->dev, "read: 0x%08x = 0x%08x\n",
			 address, value);

	} else if (strncmp(cmd_buf, "write", 5) == 0) {
		u32 address, value;
		cnt = sscanf(&cmd_buf[5], "%i %i", &address, &value);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev, "write <reg> <value>\n");
			goto command_write_done;
		}

		/* check the range on address */
		if (address >= I40E_MAX_REGISTER) {
			dev_info(&pf->pdev->dev, "write reg address 0x%08x too large\n",
				 address);
			goto command_write_done;
		}
		wr32(&pf->hw, address, value);
		value = rd32(&pf->hw, address);
		dev_info(&pf->pdev->dev, "write: 0x%08x = 0x%08x\n",
			 address, value);
	} else if (strncmp(cmd_buf, "clear_stats", 11) == 0) {
		if (strncmp(&cmd_buf[12], "vsi", 3) == 0) {
			cnt = sscanf(&cmd_buf[15], "%i", &vsi_seid);
			if (cnt == 0) {
				int i;
				for (i = 0; i < pf->num_alloc_vsi; i++)
					i40e_vsi_reset_stats(pf->vsi[i]);
				dev_info(&pf->pdev->dev, "vsi clear stats called for all vsi's\n");
			} else if (cnt == 1) {
				vsi = i40e_dbg_find_vsi(pf, vsi_seid);
				if (!vsi) {
					dev_info(&pf->pdev->dev,
						 "clear_stats vsi: bad vsi %d\n",
						 vsi_seid);
					goto command_write_done;
				}
				i40e_vsi_reset_stats(vsi);
				dev_info(&pf->pdev->dev,
					 "vsi clear stats called for vsi %d\n",
					 vsi_seid);
			} else {
				dev_info(&pf->pdev->dev, "clear_stats vsi [seid]\n");
			}
		} else if (strncmp(&cmd_buf[12], "pf", 2) == 0) {
			i40e_pf_reset_stats(pf);
			dev_info(&pf->pdev->dev, "pf clear stats called\n");
		} else {
			dev_info(&pf->pdev->dev, "clear_stats vsi [seid] or clear_stats pf\n");
		}
	} else if (strncmp(cmd_buf, "send aq_cmd", 11) == 0) {
		struct i40e_aq_desc *desc;
		i40e_status ret;

		desc = kzalloc(sizeof(struct i40e_aq_desc), GFP_KERNEL);
		if (!desc)
			goto command_write_done;
		cnt = sscanf(&cmd_buf[11],
			     "%hi %hi %hi %hi %i %i %i %i %i %i",
			     &desc->flags,
			     &desc->opcode, &desc->datalen, &desc->retval,
			     &desc->cookie_high, &desc->cookie_low,
			     &desc->params.internal.param0,
			     &desc->params.internal.param1,
			     &desc->params.internal.param2,
			     &desc->params.internal.param3);
		if (cnt != 10) {
			dev_info(&pf->pdev->dev,
				 "send aq_cmd: bad command string, cnt=%d\n",
				 cnt);
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		ret = i40e_asq_send_command(&pf->hw, desc, NULL, 0, NULL);
		if (!ret) {
			dev_info(&pf->pdev->dev, "AQ command sent Status : Success\n");
		} else if (ret == I40E_ERR_ADMIN_QUEUE_ERROR) {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x AQ Error: %d\n",
				 desc->opcode, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x Status: %d\n",
				 desc->opcode, ret);
		}
		dev_info(&pf->pdev->dev,
			 "AQ desc WB 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 desc->flags, desc->opcode, desc->datalen, desc->retval,
			 desc->cookie_high, desc->cookie_low,
			 desc->params.internal.param0,
			 desc->params.internal.param1,
			 desc->params.internal.param2,
			 desc->params.internal.param3);
		kfree(desc);
		desc = NULL;
	} else if (strncmp(cmd_buf, "send indirect aq_cmd", 20) == 0) {
		struct i40e_aq_desc *desc;
		i40e_status ret;
		u16 buffer_len;
		u8 *buff;

		desc = kzalloc(sizeof(struct i40e_aq_desc), GFP_KERNEL);
		if (!desc)
			goto command_write_done;
		cnt = sscanf(&cmd_buf[20],
			     "%hi %hi %hi %hi %i %i %i %i %i %i %hi",
			     &desc->flags,
			     &desc->opcode, &desc->datalen, &desc->retval,
			     &desc->cookie_high, &desc->cookie_low,
			     &desc->params.internal.param0,
			     &desc->params.internal.param1,
			     &desc->params.internal.param2,
			     &desc->params.internal.param3,
			     &buffer_len);
		if (cnt != 11) {
			dev_info(&pf->pdev->dev,
				 "send indirect aq_cmd: bad command string, cnt=%d\n",
				 cnt);
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		/* Just stub a buffer big enough in case user messed up */
		if (buffer_len == 0)
			buffer_len = 1280;

		buff = kzalloc(buffer_len, GFP_KERNEL);
		if (!buff) {
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		desc->flags |= cpu_to_le16((u16)I40E_AQ_FLAG_BUF);
		ret = i40e_asq_send_command(&pf->hw, desc, buff,
					    buffer_len, NULL);
		if (!ret) {
			dev_info(&pf->pdev->dev, "AQ command sent Status : Success\n");
		} else if (ret == I40E_ERR_ADMIN_QUEUE_ERROR) {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x AQ Error: %d\n",
				 desc->opcode, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x Status: %d\n",
				 desc->opcode, ret);
		}
		dev_info(&pf->pdev->dev,
			 "AQ desc WB 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 desc->flags, desc->opcode, desc->datalen, desc->retval,
			 desc->cookie_high, desc->cookie_low,
			 desc->params.internal.param0,
			 desc->params.internal.param1,
			 desc->params.internal.param2,
			 desc->params.internal.param3);
		print_hex_dump(KERN_INFO, "AQ buffer WB: ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buff, buffer_len, true);
		kfree(buff);
		buff = NULL;
		kfree(desc);
		desc = NULL;
	} else if ((strncmp(cmd_buf, "add fd_filter", 13) == 0) ||
		   (strncmp(cmd_buf, "rem fd_filter", 13) == 0)) {
		struct i40e_fdir_filter fd_data;
		u16 packet_len, i, j = 0;
		char *asc_packet;
		u8 *raw_packet;
		bool add = false;
		int ret;

		if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
			goto command_write_done;

		if (strncmp(cmd_buf, "add", 3) == 0)
			add = true;

		if (add && (pf->auto_disable_flags & I40E_FLAG_FD_SB_ENABLED))
			goto command_write_done;

		asc_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE,
				     GFP_KERNEL);
		if (!asc_packet)
			goto command_write_done;

		raw_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE,
				     GFP_KERNEL);

		if (!raw_packet) {
			kfree(asc_packet);
			asc_packet = NULL;
			goto command_write_done;
		}

		cnt = sscanf(&cmd_buf[13],
			     "%hx %2hhx %2hhx %hx %2hhx %2hhx %hx %x %hd %511s",
			     &fd_data.q_index,
			     &fd_data.flex_off, &fd_data.pctype,
			     &fd_data.dest_vsi, &fd_data.dest_ctl,
			     &fd_data.fd_status, &fd_data.cnt_index,
			     &fd_data.fd_id, &packet_len, asc_packet);
		if (cnt != 10) {
			dev_info(&pf->pdev->dev,
				 "program fd_filter: bad command string, cnt=%d\n",
				 cnt);
			kfree(asc_packet);
			asc_packet = NULL;
			kfree(raw_packet);
			goto command_write_done;
		}

		/* fix packet length if user entered 0 */
		if (packet_len == 0)
			packet_len = I40E_FDIR_MAX_RAW_PACKET_SIZE;

		/* make sure to check the max as well */
		packet_len = min_t(u16,
				   packet_len, I40E_FDIR_MAX_RAW_PACKET_SIZE);

		for (i = 0; i < packet_len; i++) {
			sscanf(&asc_packet[j], "%2hhx ",
			       &raw_packet[i]);
			j += 3;
		}
		dev_info(&pf->pdev->dev, "FD raw packet dump\n");
		print_hex_dump(KERN_INFO, "FD raw packet: ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       raw_packet, packet_len, true);
		ret = i40e_program_fdir_filter(&fd_data, raw_packet, pf, add);
		if (!ret) {
			dev_info(&pf->pdev->dev, "Filter command send Status : Success\n");
		} else {
			dev_info(&pf->pdev->dev,
				 "Filter command send failed %d\n", ret);
		}
		kfree(raw_packet);
		raw_packet = NULL;
		kfree(asc_packet);
		asc_packet = NULL;
	} else if (strncmp(cmd_buf, "fd-atr off", 10) == 0) {
		i40e_dbg_cmd_fd_ctrl(pf, I40E_FLAG_FD_ATR_ENABLED, false);
	} else if (strncmp(cmd_buf, "fd-atr on", 9) == 0) {
		i40e_dbg_cmd_fd_ctrl(pf, I40E_FLAG_FD_ATR_ENABLED, true);
	} else if (strncmp(cmd_buf, "fd current cnt", 14) == 0) {
		dev_info(&pf->pdev->dev, "FD current total filter count for this interface: %d\n",
			 i40e_get_current_fd_count(pf));
	} else if (strncmp(cmd_buf, "lldp", 4) == 0) {
		if (strncmp(&cmd_buf[5], "stop", 4) == 0) {
			int ret;
			ret = i40e_aq_stop_lldp(&pf->hw, false, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Stop LLDP AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
			ret = i40e_aq_add_rem_control_packet_filter(&pf->hw,
						pf->hw.mac.addr,
						I40E_ETH_P_LLDP, 0,
						pf->vsi[pf->lan_vsi]->seid,
						0, true, NULL, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					"%s: Add Control Packet Filter AQ command failed =0x%x\n",
					__func__, pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
#ifdef CONFIG_I40E_DCB
			pf->dcbx_cap = DCB_CAP_DCBX_HOST |
				       DCB_CAP_DCBX_VER_IEEE;
#endif /* CONFIG_I40E_DCB */
		} else if (strncmp(&cmd_buf[5], "start", 5) == 0) {
			int ret;
			ret = i40e_aq_add_rem_control_packet_filter(&pf->hw,
						pf->hw.mac.addr,
						I40E_ETH_P_LLDP, 0,
						pf->vsi[pf->lan_vsi]->seid,
						0, false, NULL, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					"%s: Remove Control Packet Filter AQ command failed =0x%x\n",
					__func__, pf->hw.aq.asq_last_status);
				/* Continue and start FW LLDP anyways */
			}

			ret = i40e_aq_start_lldp(&pf->hw, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Start LLDP AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
#ifdef CONFIG_I40E_DCB
			pf->dcbx_cap = DCB_CAP_DCBX_LLD_MANAGED |
				       DCB_CAP_DCBX_VER_IEEE;
#endif /* CONFIG_I40E_DCB */
		} else if (strncmp(&cmd_buf[5],
			   "get local", 9) == 0) {
			u16 llen, rlen;
			int ret;
			u8 *buff;
			buff = kzalloc(I40E_LLDPDU_SIZE, GFP_KERNEL);
			if (!buff)
				goto command_write_done;

			ret = i40e_aq_get_lldp_mib(&pf->hw, 0,
						   I40E_AQ_LLDP_MIB_LOCAL,
						   buff, I40E_LLDPDU_SIZE,
						   &llen, &rlen, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Get LLDP MIB (local) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(buff);
				buff = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev, "LLDP MIB (local)\n");
			print_hex_dump(KERN_INFO, "LLDP MIB (local): ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       buff, I40E_LLDPDU_SIZE, true);
			kfree(buff);
			buff = NULL;
		} else if (strncmp(&cmd_buf[5], "get remote", 10) == 0) {
			u16 llen, rlen;
			int ret;
			u8 *buff;
			buff = kzalloc(I40E_LLDPDU_SIZE, GFP_KERNEL);
			if (!buff)
				goto command_write_done;

			ret = i40e_aq_get_lldp_mib(&pf->hw,
					I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE,
					I40E_AQ_LLDP_MIB_REMOTE,
					buff, I40E_LLDPDU_SIZE,
					&llen, &rlen, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Get LLDP MIB (remote) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(buff);
				buff = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev, "LLDP MIB (remote)\n");
			print_hex_dump(KERN_INFO, "LLDP MIB (remote): ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       buff, I40E_LLDPDU_SIZE, true);
			kfree(buff);
			buff = NULL;
		} else if (strncmp(&cmd_buf[5], "event on", 8) == 0) {
			int ret;
			ret = i40e_aq_cfg_lldp_mib_change_event(&pf->hw,
								true, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Config LLDP MIB Change Event (on) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
		} else if (strncmp(&cmd_buf[5], "event off", 9) == 0) {
			int ret;
			ret = i40e_aq_cfg_lldp_mib_change_event(&pf->hw,
								false, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Config LLDP MIB Change Event (off) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
		}
	} else if (strncmp(cmd_buf, "nvm read", 8) == 0) {
		u16 buffer_len, bytes;
		u16 module;
		u32 offset;
		u16 *buff;
		int ret;

		cnt = sscanf(&cmd_buf[8], "%hx %x %hx",
			     &module, &offset, &buffer_len);
		if (cnt == 0) {
			module = 0;
			offset = 0;
			buffer_len = 0;
		} else if (cnt == 1) {
			offset = 0;
			buffer_len = 0;
		} else if (cnt == 2) {
			buffer_len = 0;
		} else if (cnt > 3) {
			dev_info(&pf->pdev->dev,
				 "nvm read: bad command string, cnt=%d\n", cnt);
			goto command_write_done;
		}

		/* set the max length */
		buffer_len = min_t(u16, buffer_len, I40E_MAX_AQ_BUF_SIZE/2);

		bytes = 2 * buffer_len;

		/* read at least 1k bytes, no more than 4kB */
		bytes = clamp(bytes, (u16)1024, (u16)I40E_MAX_AQ_BUF_SIZE);
		buff = kzalloc(bytes, GFP_KERNEL);
		if (!buff)
			goto command_write_done;

		ret = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_READ);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Failed Acquiring NVM resource for read err=%d status=0x%x\n",
				 ret, pf->hw.aq.asq_last_status);
			kfree(buff);
			goto command_write_done;
		}

		ret = i40e_aq_read_nvm(&pf->hw, module, (2 * offset),
				       bytes, (u8 *)buff, true, NULL);
		i40e_release_nvm(&pf->hw);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Read NVM AQ failed err=%d status=0x%x\n",
				 ret, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "Read NVM module=0x%x offset=0x%x words=%d\n",
				 module, offset, buffer_len);
			if (bytes)
				print_hex_dump(KERN_INFO, "NVM Dump: ",
					DUMP_PREFIX_OFFSET, 16, 2,
					buff, bytes, true);
		}
		kfree(buff);
		buff = NULL;
	} else {
		dev_info(&pf->pdev->dev, "unknown command '%s'\n", cmd_buf);
		dev_info(&pf->pdev->dev, "available commands\n");
		dev_info(&pf->pdev->dev, "  add vsi [relay_seid]\n");
		dev_info(&pf->pdev->dev, "  del vsi [vsi_seid]\n");
		dev_info(&pf->pdev->dev, "  add relay <uplink_seid> <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  del relay <relay_seid>\n");
		dev_info(&pf->pdev->dev, "  add macaddr <vsi_seid> <aa:bb:cc:dd:ee:ff> [vlan]\n");
		dev_info(&pf->pdev->dev, "  del macaddr <vsi_seid> <aa:bb:cc:dd:ee:ff> [vlan]\n");
		dev_info(&pf->pdev->dev, "  add pvid <vsi_seid> <vid>\n");
		dev_info(&pf->pdev->dev, "  del pvid <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  dump switch\n");
		dev_info(&pf->pdev->dev, "  dump vsi [seid]\n");
		dev_info(&pf->pdev->dev, "  dump desc tx <vsi_seid> <ring_id> [<desc_n>]\n");
		dev_info(&pf->pdev->dev, "  dump desc rx <vsi_seid> <ring_id> [<desc_n>]\n");
		dev_info(&pf->pdev->dev, "  dump desc aq\n");
		dev_info(&pf->pdev->dev, "  dump stats\n");
		dev_info(&pf->pdev->dev, "  dump reset stats\n");
		dev_info(&pf->pdev->dev, "  msg_enable [level]\n");
		dev_info(&pf->pdev->dev, "  read <reg>\n");
		dev_info(&pf->pdev->dev, "  write <reg> <value>\n");
		dev_info(&pf->pdev->dev, "  clear_stats vsi [seid]\n");
		dev_info(&pf->pdev->dev, "  clear_stats pf\n");
		dev_info(&pf->pdev->dev, "  pfr\n");
		dev_info(&pf->pdev->dev, "  corer\n");
		dev_info(&pf->pdev->dev, "  globr\n");
		dev_info(&pf->pdev->dev, "  send aq_cmd <flags> <opcode> <datalen> <retval> <cookie_h> <cookie_l> <param0> <param1> <param2> <param3>\n");
		dev_info(&pf->pdev->dev, "  send indirect aq_cmd <flags> <opcode> <datalen> <retval> <cookie_h> <cookie_l> <param0> <param1> <param2> <param3> <buffer_len>\n");
		dev_info(&pf->pdev->dev, "  add fd_filter <dest q_index> <flex_off> <pctype> <dest_vsi> <dest_ctl> <fd_status> <cnt_index> <fd_id> <packet_len> <packet>\n");
		dev_info(&pf->pdev->dev, "  rem fd_filter <dest q_index> <flex_off> <pctype> <dest_vsi> <dest_ctl> <fd_status> <cnt_index> <fd_id> <packet_len> <packet>\n");
		dev_info(&pf->pdev->dev, "  fd-atr off\n");
		dev_info(&pf->pdev->dev, "  fd-atr on\n");
		dev_info(&pf->pdev->dev, "  fd current cnt");
		dev_info(&pf->pdev->dev, "  lldp start\n");
		dev_info(&pf->pdev->dev, "  lldp stop\n");
		dev_info(&pf->pdev->dev, "  lldp get local\n");
		dev_info(&pf->pdev->dev, "  lldp get remote\n");
		dev_info(&pf->pdev->dev, "  lldp event on\n");
		dev_info(&pf->pdev->dev, "  lldp event off\n");
		dev_info(&pf->pdev->dev, "  nvm read [module] [word_offset] [word_count]\n");
	}

command_write_done:
	kfree(cmd_buf);
	cmd_buf = NULL;
	return count;
}

static const struct file_operations i40e_dbg_command_fops = {
	.owner = THIS_MODULE,
	.open =  simple_open,
	.read =  i40e_dbg_command_read,
	.write = i40e_dbg_command_write,
};

/**************************************************************
 * netdev_ops
 * The netdev_ops entry in debugfs is for giving the driver commands
 * to be executed from the netdev operations.
 **************************************************************/
static char i40e_dbg_netdev_ops_buf[256] = "hello world";

/**
 * i40e_dbg_netdev_ops - read for netdev_ops datum
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_netdev_ops_read(struct file *filp, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	int buf_size = 256;
	char *buf;
	int len;

	/* don't allow partal reads */
	if (*ppos != 0)
		return 0;
	if (count < buf_size)
		return -ENOSPC;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOSPC;

	len = snprintf(buf, buf_size, "%s: %s\n",
		       pf->vsi[pf->lan_vsi]->netdev->name,
		       i40e_dbg_netdev_ops_buf);

	bytes_not_copied = copy_to_user(buffer, buf, len);
	kfree(buf);

	if (bytes_not_copied < 0)
		return bytes_not_copied;

	*ppos = len;
	return len;
}

/**
 * i40e_dbg_netdev_ops_write - write into netdev_ops datum
 * @filp: the opened file
 * @buffer: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_netdev_ops_write(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	struct i40e_vsi *vsi;
	char *buf_tmp;
	int vsi_seid;
	int i, cnt;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;
	if (count >= sizeof(i40e_dbg_netdev_ops_buf))
		return -ENOSPC;

	memset(i40e_dbg_netdev_ops_buf, 0, sizeof(i40e_dbg_netdev_ops_buf));
	bytes_not_copied = copy_from_user(i40e_dbg_netdev_ops_buf,
					  buffer, count);
	if (bytes_not_copied < 0)
		return bytes_not_copied;
	else if (bytes_not_copied > 0)
		count -= bytes_not_copied;
	i40e_dbg_netdev_ops_buf[count] = '\0';

	buf_tmp = strchr(i40e_dbg_netdev_ops_buf, '\n');
	if (buf_tmp) {
		*buf_tmp = '\0';
		count = buf_tmp - i40e_dbg_netdev_ops_buf + 1;
	}

	if (strncmp(i40e_dbg_netdev_ops_buf, "tx_timeout", 10) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "tx_timeout <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "tx_timeout: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "tx_timeout: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (test_bit(__I40E_DOWN, &vsi->state)) {
			dev_info(&pf->pdev->dev, "tx_timeout: VSI %d not UP\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_tx_timeout(vsi->netdev);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "tx_timeout called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}
	} else if (strncmp(i40e_dbg_netdev_ops_buf, "change_mtu", 10) == 0) {
		int mtu;
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i %i",
			     &vsi_seid, &mtu);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev, "change_mtu <vsi_seid> <mtu>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "change_mtu: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "change_mtu: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_change_mtu(vsi->netdev,
								mtu);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "change_mtu called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}

	} else if (strncmp(i40e_dbg_netdev_ops_buf, "set_rx_mode", 11) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "set_rx_mode <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "set_rx_mode: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "set_rx_mode: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_set_rx_mode(vsi->netdev);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "set_rx_mode called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}

	} else if (strncmp(i40e_dbg_netdev_ops_buf, "napi", 4) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[4], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "napi <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "napi: VSI %d not found\n",
				 vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "napi: no netdev for VSI %d\n",
				 vsi_seid);
		} else {
			for (i = 0; i < vsi->num_q_vectors; i++)
				napi_schedule(&vsi->q_vectors[i]->napi);
			dev_info(&pf->pdev->dev, "napi called\n");
		}
	} else {
		dev_info(&pf->pdev->dev, "unknown command '%s'\n",
			 i40e_dbg_netdev_ops_buf);
		dev_info(&pf->pdev->dev, "available commands\n");
		dev_info(&pf->pdev->dev, "  tx_timeout <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  change_mtu <vsi_seid> <mtu>\n");
		dev_info(&pf->pdev->dev, "  set_rx_mode <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  napi <vsi_seid>\n");
	}
netdev_ops_write_done:
	return count;
}

static const struct file_operations i40e_dbg_netdev_ops_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = i40e_dbg_netdev_ops_read,
	.write = i40e_dbg_netdev_ops_write,
};

/**
 * i40e_dbg_pf_init - setup the debugfs directory for the pf
 * @pf: the pf that is starting up
 **/
void i40e_dbg_pf_init(struct i40e_pf *pf)
{
	struct dentry *pfile;
	const char *name = pci_name(pf->pdev);
	const struct device *dev = &pf->pdev->dev;

	pf->i40e_dbg_pf = debugfs_create_dir(name, i40e_dbg_root);
	if (!pf->i40e_dbg_pf)
		return;

	pfile = debugfs_create_file("command", 0600, pf->i40e_dbg_pf, pf,
				    &i40e_dbg_command_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("dump", 0600, pf->i40e_dbg_pf, pf,
				    &i40e_dbg_dump_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("netdev_ops", 0600, pf->i40e_dbg_pf, pf,
				    &i40e_dbg_netdev_ops_fops);
	if (!pfile)
		goto create_failed;

	return;

create_failed:
	dev_info(dev, "debugfs dir/file for %s failed\n", name);
	debugfs_remove_recursive(pf->i40e_dbg_pf);
	return;
}

/**
 * i40e_dbg_pf_exit - clear out the pf's debugfs entries
 * @pf: the pf that is stopping
 **/
void i40e_dbg_pf_exit(struct i40e_pf *pf)
{
	debugfs_remove_recursive(pf->i40e_dbg_pf);
	pf->i40e_dbg_pf = NULL;

	kfree(i40e_dbg_dump_buf);
	i40e_dbg_dump_buf = NULL;
}

/**
 * i40e_dbg_init - start up debugfs for the driver
 **/
void i40e_dbg_init(void)
{
	i40e_dbg_root = debugfs_create_dir(i40e_driver_name, NULL);
	if (!i40e_dbg_root)
		pr_info("init of debugfs failed\n");
}

/**
 * i40e_dbg_exit - clean out the driver's debugfs entries
 **/
void i40e_dbg_exit(void)
{
	debugfs_remove_recursive(i40e_dbg_root);
	i40e_dbg_root = NULL;
}

#endif /* CONFIG_DEBUG_FS */
