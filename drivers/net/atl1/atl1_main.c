/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 *
 * Contact Information:
 * Xiong Huang <xiong_huang@attansic.com>
 * Attansic Technology Corp. 3F 147, Xianzheng 9th Road, Zhubei,
 * Xinzhu  302, TAIWAN, REPUBLIC OF CHINA
 *
 * Chris Snook <csnook@redhat.com>
 * Jay Cliburn <jcliburn@gmail.com>
 *
 * This version is adapted from the Attansic reference driver for
 * inclusion in the Linux kernel.  It is currently under heavy development.
 * A very incomplete list of things that need to be dealt with:
 *
 * TODO:
 * Fix TSO; tx performance is horrible with TSO enabled.
 * Wake on LAN.
 * Add more ethtool functions, including set ring parameters.
 * Fix abstruse irq enable/disable condition described here:
 *	http://marc.theaimsgroup.com/?l=linux-netdev&m=116398508500553&w=2
 *
 * NEEDS TESTING:
 * VLAN
 * multicast
 * promiscuous mode
 * interrupt coalescing
 * SMP torture testing
 */

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/irqreturn.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/dma-mapping.h>
#include <linux/net.h>
#include <linux/pm.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <net/checksum.h>

#include <asm/atomic.h>
#include <asm/byteorder.h>

#include "atl1.h"

#define DRIVER_VERSION "2.0.7"

char atl1_driver_name[] = "atl1";
static const char atl1_driver_string[] = "Attansic L1 Ethernet Network Driver";
static const char atl1_copyright[] = "Copyright(c) 2005-2006 Attansic Corporation.";
char atl1_driver_version[] = DRIVER_VERSION;

MODULE_AUTHOR
    ("Attansic Corporation <xiong_huang@attansic.com>, Chris Snook <csnook@redhat.com>, Jay Cliburn <jcliburn@gmail.com>");
MODULE_DESCRIPTION("Attansic 1000M Ethernet Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

/*
 * atl1_pci_tbl - PCI Device ID Table
 */
static const struct pci_device_id atl1_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_ATTANSIC, PCI_DEVICE_ID_ATTANSIC_L1)},
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, atl1_pci_tbl);

/*
 * atl1_sw_init - Initialize general software structures (struct atl1_adapter)
 * @adapter: board private structure to initialize
 *
 * atl1_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int __devinit atl1_sw_init(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	/* PCI config space info */
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);

	hw->max_frame_size = netdev->mtu + ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;
	hw->min_frame_size = MINIMUM_ETHERNET_FRAME_SIZE;

	adapter->wol = 0;
	adapter->rx_buffer_len = (hw->max_frame_size + 7) & ~7;
	adapter->ict = 50000;	/* 100ms */
	adapter->link_speed = SPEED_0;	/* hardware init */
	adapter->link_duplex = FULL_DUPLEX;

	hw->phy_configured = false;
	hw->preamble_len = 7;
	hw->ipgt = 0x60;
	hw->min_ifg = 0x50;
	hw->ipgr1 = 0x40;
	hw->ipgr2 = 0x60;
	hw->max_retry = 0xf;
	hw->lcol = 0x37;
	hw->jam_ipg = 7;
	hw->rfd_burst = 8;
	hw->rrd_burst = 8;
	hw->rfd_fetch_gap = 1;
	hw->rx_jumbo_th = adapter->rx_buffer_len / 8;
	hw->rx_jumbo_lkah = 1;
	hw->rrd_ret_timer = 16;
	hw->tpd_burst = 4;
	hw->tpd_fetch_th = 16;
	hw->txf_burst = 0x100;
	hw->tx_jumbo_task_th = (hw->max_frame_size + 7) >> 3;
	hw->tpd_fetch_gap = 1;
	hw->rcb_value = atl1_rcb_64;
	hw->dma_ord = atl1_dma_ord_enh;
	hw->dmar_block = atl1_dma_req_256;
	hw->dmaw_block = atl1_dma_req_256;
	hw->cmb_rrd = 4;
	hw->cmb_tpd = 4;
	hw->cmb_rx_timer = 1;	/* about 2us */
	hw->cmb_tx_timer = 1;	/* about 2us */
	hw->smb_timer = 100000;	/* about 200ms */

	atomic_set(&adapter->irq_sem, 0);
	spin_lock_init(&adapter->lock);
	spin_lock_init(&adapter->mb_lock);

	return 0;
}

/*
 * atl1_setup_mem_resources - allocate Tx / RX descriptor resources
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
s32 atl1_setup_ring_resources(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_ring_header *ring_header = &adapter->ring_header;
	struct pci_dev *pdev = adapter->pdev;
	int size;
	u8 offset = 0;

	size = sizeof(struct atl1_buffer) * (tpd_ring->count + rfd_ring->count);
	tpd_ring->buffer_info = kzalloc(size, GFP_KERNEL);
	if (unlikely(!tpd_ring->buffer_info)) {
		printk(KERN_WARNING "%s: kzalloc failed , size = D%d\n",
			atl1_driver_name, size);
		goto err_nomem;
	}
	rfd_ring->buffer_info =
	    (struct atl1_buffer *)(tpd_ring->buffer_info + tpd_ring->count);

	/* real ring DMA buffer */
	ring_header->size = size = sizeof(struct tx_packet_desc) *
					tpd_ring->count
	    + sizeof(struct rx_free_desc) * rfd_ring->count
	    + sizeof(struct rx_return_desc) * rrd_ring->count
	    + sizeof(struct coals_msg_block)
	    + sizeof(struct stats_msg_block)
	    + 40;		/* "40: for 8 bytes align" huh? -- CHS */

	ring_header->desc = pci_alloc_consistent(pdev, ring_header->size,
						&ring_header->dma);
	if (unlikely(!ring_header->desc)) {
		printk(KERN_WARNING
			"%s: pci_alloc_consistent failed, size = D%d\n",
			atl1_driver_name, size);
		goto err_nomem;
	}

	memset(ring_header->desc, 0, ring_header->size);

	/* init TPD ring */
	tpd_ring->dma = ring_header->dma;
	offset = (tpd_ring->dma & 0x7) ? (8 - (ring_header->dma & 0x7)) : 0;
	tpd_ring->dma += offset;
	tpd_ring->desc = (u8 *) ring_header->desc + offset;
	tpd_ring->size = sizeof(struct tx_packet_desc) * tpd_ring->count;
	atomic_set(&tpd_ring->next_to_use, 0);
	atomic_set(&tpd_ring->next_to_clean, 0);

	/* init RFD ring */
	rfd_ring->dma = tpd_ring->dma + tpd_ring->size;
	offset = (rfd_ring->dma & 0x7) ? (8 - (rfd_ring->dma & 0x7)) : 0;
	rfd_ring->dma += offset;
	rfd_ring->desc = (u8 *) tpd_ring->desc + (tpd_ring->size + offset);
	rfd_ring->size = sizeof(struct rx_free_desc) * rfd_ring->count;
	rfd_ring->next_to_clean = 0;
	/* rfd_ring->next_to_use = rfd_ring->count - 1; */
	atomic_set(&rfd_ring->next_to_use, 0);

	/* init RRD ring */
	rrd_ring->dma = rfd_ring->dma + rfd_ring->size;
	offset = (rrd_ring->dma & 0x7) ? (8 - (rrd_ring->dma & 0x7)) : 0;
	rrd_ring->dma += offset;
	rrd_ring->desc = (u8 *) rfd_ring->desc + (rfd_ring->size + offset);
	rrd_ring->size = sizeof(struct rx_return_desc) * rrd_ring->count;
	rrd_ring->next_to_use = 0;
	atomic_set(&rrd_ring->next_to_clean, 0);

	/* init CMB */
	adapter->cmb.dma = rrd_ring->dma + rrd_ring->size;
	offset = (adapter->cmb.dma & 0x7) ? (8 - (adapter->cmb.dma & 0x7)) : 0;
	adapter->cmb.dma += offset;
	adapter->cmb.cmb =
	    (struct coals_msg_block *) ((u8 *) rrd_ring->desc +
				   (rrd_ring->size + offset));

	/* init SMB */
	adapter->smb.dma = adapter->cmb.dma + sizeof(struct coals_msg_block);
	offset = (adapter->smb.dma & 0x7) ? (8 - (adapter->smb.dma & 0x7)) : 0;
	adapter->smb.dma += offset;
	adapter->smb.smb = (struct stats_msg_block *)
	    ((u8 *) adapter->cmb.cmb + (sizeof(struct coals_msg_block) + offset));

	return ATL1_SUCCESS;

err_nomem:
	kfree(tpd_ring->buffer_info);
	return -ENOMEM;
}

/*
 * atl1_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static void atl1_irq_enable(struct atl1_adapter *adapter)
{
	if (likely(!atomic_dec_and_test(&adapter->irq_sem)))
		iowrite32(IMR_NORMAL_MASK, adapter->hw.hw_addr + REG_IMR);
}

static void atl1_clear_phy_int(struct atl1_adapter *adapter)
{
	u16 phy_data;
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	atl1_read_phy_reg(&adapter->hw, 19, &phy_data);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

static void atl1_inc_smb(struct atl1_adapter *adapter)
{
	struct stats_msg_block *smb = adapter->smb.smb;

	/* Fill out the OS statistics structure */
	adapter->soft_stats.rx_packets += smb->rx_ok;
	adapter->soft_stats.tx_packets += smb->tx_ok;
	adapter->soft_stats.rx_bytes += smb->rx_byte_cnt;
	adapter->soft_stats.tx_bytes += smb->tx_byte_cnt;
	adapter->soft_stats.multicast += smb->rx_mcast;
	adapter->soft_stats.collisions += (smb->tx_1_col +
					   smb->tx_2_col * 2 +
					   smb->tx_late_col +
					   smb->tx_abort_col *
					   adapter->hw.max_retry);

	/* Rx Errors */
	adapter->soft_stats.rx_errors += (smb->rx_frag +
					  smb->rx_fcs_err +
					  smb->rx_len_err +
					  smb->rx_sz_ov +
					  smb->rx_rxf_ov +
					  smb->rx_rrd_ov + smb->rx_align_err);
	adapter->soft_stats.rx_fifo_errors += smb->rx_rxf_ov;
	adapter->soft_stats.rx_length_errors += smb->rx_len_err;
	adapter->soft_stats.rx_crc_errors += smb->rx_fcs_err;
	adapter->soft_stats.rx_frame_errors += smb->rx_align_err;
	adapter->soft_stats.rx_missed_errors += (smb->rx_rrd_ov +
						 smb->rx_rxf_ov);

	adapter->soft_stats.rx_pause += smb->rx_pause;
	adapter->soft_stats.rx_rrd_ov += smb->rx_rrd_ov;
	adapter->soft_stats.rx_trunc += smb->rx_sz_ov;

	/* Tx Errors */
	adapter->soft_stats.tx_errors += (smb->tx_late_col +
					  smb->tx_abort_col +
					  smb->tx_underrun + smb->tx_trunc);
	adapter->soft_stats.tx_fifo_errors += smb->tx_underrun;
	adapter->soft_stats.tx_aborted_errors += smb->tx_abort_col;
	adapter->soft_stats.tx_window_errors += smb->tx_late_col;

	adapter->soft_stats.excecol += smb->tx_abort_col;
	adapter->soft_stats.deffer += smb->tx_defer;
	adapter->soft_stats.scc += smb->tx_1_col;
	adapter->soft_stats.mcc += smb->tx_2_col;
	adapter->soft_stats.latecol += smb->tx_late_col;
	adapter->soft_stats.tx_underun += smb->tx_underrun;
	adapter->soft_stats.tx_trunc += smb->tx_trunc;
	adapter->soft_stats.tx_pause += smb->tx_pause;

	adapter->net_stats.rx_packets = adapter->soft_stats.rx_packets;
	adapter->net_stats.tx_packets = adapter->soft_stats.tx_packets;
	adapter->net_stats.rx_bytes = adapter->soft_stats.rx_bytes;
	adapter->net_stats.tx_bytes = adapter->soft_stats.tx_bytes;
	adapter->net_stats.multicast = adapter->soft_stats.multicast;
	adapter->net_stats.collisions = adapter->soft_stats.collisions;
	adapter->net_stats.rx_errors = adapter->soft_stats.rx_errors;
	adapter->net_stats.rx_over_errors =
	    adapter->soft_stats.rx_missed_errors;
	adapter->net_stats.rx_length_errors =
	    adapter->soft_stats.rx_length_errors;
	adapter->net_stats.rx_crc_errors = adapter->soft_stats.rx_crc_errors;
	adapter->net_stats.rx_frame_errors =
	    adapter->soft_stats.rx_frame_errors;
	adapter->net_stats.rx_fifo_errors = adapter->soft_stats.rx_fifo_errors;
	adapter->net_stats.rx_missed_errors =
	    adapter->soft_stats.rx_missed_errors;
	adapter->net_stats.tx_errors = adapter->soft_stats.tx_errors;
	adapter->net_stats.tx_fifo_errors = adapter->soft_stats.tx_fifo_errors;
	adapter->net_stats.tx_aborted_errors =
	    adapter->soft_stats.tx_aborted_errors;
	adapter->net_stats.tx_window_errors =
	    adapter->soft_stats.tx_window_errors;
	adapter->net_stats.tx_carrier_errors =
	    adapter->soft_stats.tx_carrier_errors;
}

static void atl1_rx_checksum(struct atl1_adapter *adapter,
					struct rx_return_desc *rrd,
					struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;

	if (unlikely(rrd->pkt_flg & PACKET_FLAG_ERR)) {
		if (rrd->err_flg & (ERR_FLAG_CRC | ERR_FLAG_TRUNC |
					ERR_FLAG_CODE | ERR_FLAG_OV)) {
			adapter->hw_csum_err++;
			printk(KERN_DEBUG "%s: rx checksum error\n",
				atl1_driver_name);
			return;
		}
	}

	/* not IPv4 */
	if (!(rrd->pkt_flg & PACKET_FLAG_IPV4))
		/* checksum is invalid, but it's not an IPv4 pkt, so ok */
		return;

	/* IPv4 packet */
	if (likely(!(rrd->err_flg &
		(ERR_FLAG_IP_CHKSUM | ERR_FLAG_L4_CHKSUM)))) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		adapter->hw_csum_good++;
		return;
	}

	/* IPv4, but hardware thinks its checksum is wrong */
	printk(KERN_DEBUG "%s: hw csum wrong pkt_flag:%x, err_flag:%x\n",
		atl1_driver_name, rrd->pkt_flg, rrd->err_flg);
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb->csum = htons(rrd->xsz.xsum_sz.rx_chksum);
	adapter->hw_csum_err++;
	return;
}

/*
 * atl1_alloc_rx_buffers - Replace used receive buffers
 * @adapter: address of board private structure
 */
static u16 atl1_alloc_rx_buffers(struct atl1_adapter *adapter)
{
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct page *page;
	unsigned long offset;
	struct atl1_buffer *buffer_info, *next_info;
	struct sk_buff *skb;
	u16 num_alloc = 0;
	u16 rfd_next_to_use, next_next;
	struct rx_free_desc *rfd_desc;

	next_next = rfd_next_to_use = atomic_read(&rfd_ring->next_to_use);
	if (++next_next == rfd_ring->count)
		next_next = 0;
	buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
	next_info = &rfd_ring->buffer_info[next_next];

	while (!buffer_info->alloced && !next_info->alloced) {
		if (buffer_info->skb) {
			buffer_info->alloced = 1;
			goto next;
		}

		rfd_desc = ATL1_RFD_DESC(rfd_ring, rfd_next_to_use);

		skb = dev_alloc_skb(adapter->rx_buffer_len + NET_IP_ALIGN);
		if (unlikely(!skb)) {	/* Better luck next round */
			adapter->net_stats.rx_dropped++;
			break;
		}

		/*
		 * Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, NET_IP_ALIGN);

		buffer_info->alloced = 1;
		buffer_info->skb = skb;
		buffer_info->length = (u16) adapter->rx_buffer_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(pdev, page, offset,
						adapter->rx_buffer_len,
						PCI_DMA_FROMDEVICE);
		rfd_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		rfd_desc->buf_len = cpu_to_le16(adapter->rx_buffer_len);
		rfd_desc->coalese = 0;

next:
		rfd_next_to_use = next_next;
		if (unlikely(++next_next == rfd_ring->count))
			next_next = 0;

		buffer_info = &rfd_ring->buffer_info[rfd_next_to_use];
		next_info = &rfd_ring->buffer_info[next_next];
		num_alloc++;
	}

	if (num_alloc) {
		/*
		 * Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		atomic_set(&rfd_ring->next_to_use, (int)rfd_next_to_use);
	}
	return num_alloc;
}

static void atl1_intr_rx(struct atl1_adapter *adapter)
{
	int i, count;
	u16 length;
	u16 rrd_next_to_clean;
	u32 value;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_buffer *buffer_info;
	struct rx_return_desc *rrd;
	struct sk_buff *skb;

	count = 0;

	rrd_next_to_clean = atomic_read(&rrd_ring->next_to_clean);

	while (1) {
		rrd = ATL1_RRD_DESC(rrd_ring, rrd_next_to_clean);
		i = 1;
		if (likely(rrd->xsz.valid)) {	/* packet valid */
chk_rrd:
			/* check rrd status */
			if (likely(rrd->num_buf == 1))
				goto rrd_ok;

			/* rrd seems to be bad */
			if (unlikely(i-- > 0)) {
				/* rrd may not be DMAed completely */
				printk(KERN_DEBUG
					"%s: RRD may not be DMAed completely\n",
					atl1_driver_name);
				udelay(1);
				goto chk_rrd;
			}
			/* bad rrd */
			printk(KERN_DEBUG "%s: bad RRD\n", atl1_driver_name);
			/* see if update RFD index */
			if (rrd->num_buf > 1) {
				u16 num_buf;
				num_buf =
				    (rrd->xsz.xsum_sz.pkt_size +
				     adapter->rx_buffer_len -
				     1) / adapter->rx_buffer_len;
				if (rrd->num_buf == num_buf) {
					/* clean alloc flag for bad rrd */
					while (rfd_ring->next_to_clean !=
					       (rrd->buf_indx + num_buf)) {
						rfd_ring->buffer_info[rfd_ring->
								      next_to_clean].alloced = 0;
						if (++rfd_ring->next_to_clean ==
						    rfd_ring->count) {
							rfd_ring->
							    next_to_clean = 0;
						}
					}
				}
			}

			/* update rrd */
			rrd->xsz.valid = 0;
			if (++rrd_next_to_clean == rrd_ring->count)
				rrd_next_to_clean = 0;
			count++;
			continue;
		} else {	/* current rrd still not be updated */

			break;
		}
rrd_ok:
		/* clean alloc flag for bad rrd */
		while (rfd_ring->next_to_clean != rrd->buf_indx) {
			rfd_ring->buffer_info[rfd_ring->next_to_clean].alloced =
			    0;
			if (++rfd_ring->next_to_clean == rfd_ring->count)
				rfd_ring->next_to_clean = 0;
		}

		buffer_info = &rfd_ring->buffer_info[rrd->buf_indx];
		if (++rfd_ring->next_to_clean == rfd_ring->count)
			rfd_ring->next_to_clean = 0;

		/* update rrd next to clean */
		if (++rrd_next_to_clean == rrd_ring->count)
			rrd_next_to_clean = 0;
		count++;

		if (unlikely(rrd->pkt_flg & PACKET_FLAG_ERR)) {
			if (!(rrd->err_flg &
				(ERR_FLAG_IP_CHKSUM | ERR_FLAG_L4_CHKSUM
				| ERR_FLAG_LEN))) {
				/* packet error, don't need upstream */
				buffer_info->alloced = 0;
				rrd->xsz.valid = 0;
				continue;
			}
		}

		/* Good Receive */
		pci_unmap_page(adapter->pdev, buffer_info->dma,
			       buffer_info->length, PCI_DMA_FROMDEVICE);
		skb = buffer_info->skb;
		length = le16_to_cpu(rrd->xsz.xsum_sz.pkt_size);

		skb_put(skb, length - ETHERNET_FCS_SIZE);

		/* Receive Checksum Offload */
		atl1_rx_checksum(adapter, rrd, skb);
		skb->protocol = eth_type_trans(skb, adapter->netdev);

		if (adapter->vlgrp && (rrd->pkt_flg & PACKET_FLAG_VLAN_INS)) {
			u16 vlan_tag = (rrd->vlan_tag >> 4) |
					((rrd->vlan_tag & 7) << 13) |
					((rrd->vlan_tag & 8) << 9);
			vlan_hwaccel_rx(skb, adapter->vlgrp, vlan_tag);
		} else
			netif_rx(skb);

		/* let protocol layer free skb */
		buffer_info->skb = NULL;
		buffer_info->alloced = 0;
		rrd->xsz.valid = 0;

		adapter->netdev->last_rx = jiffies;
	}

	atomic_set(&rrd_ring->next_to_clean, rrd_next_to_clean);

	atl1_alloc_rx_buffers(adapter);

	/* update mailbox ? */
	if (count) {
		u32 tpd_next_to_use;
		u32 rfd_next_to_use;
		u32 rrd_next_to_clean;

		spin_lock(&adapter->mb_lock);

		tpd_next_to_use = atomic_read(&adapter->tpd_ring.next_to_use);
		rfd_next_to_use =
		    atomic_read(&adapter->rfd_ring.next_to_use);
		rrd_next_to_clean =
		    atomic_read(&adapter->rrd_ring.next_to_clean);
		value = ((rfd_next_to_use & MB_RFD_PROD_INDX_MASK) <<
			MB_RFD_PROD_INDX_SHIFT) |
                        ((rrd_next_to_clean & MB_RRD_CONS_INDX_MASK) <<
			MB_RRD_CONS_INDX_SHIFT) |
                        ((tpd_next_to_use & MB_TPD_PROD_INDX_MASK) <<
			MB_TPD_PROD_INDX_SHIFT);
		iowrite32(value, adapter->hw.hw_addr + REG_MAILBOX);
		spin_unlock(&adapter->mb_lock);
	}
}

static void atl1_intr_tx(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	u16 sw_tpd_next_to_clean;
	u16 cmb_tpd_next_to_clean;
	u8 update = 0;

	sw_tpd_next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	cmb_tpd_next_to_clean = le16_to_cpu(adapter->cmb.cmb->tpd_cons_idx);

	while (cmb_tpd_next_to_clean != sw_tpd_next_to_clean) {
		struct tx_packet_desc *tpd;
		update = 1;
		tpd = ATL1_TPD_DESC(tpd_ring, sw_tpd_next_to_clean);
		buffer_info = &tpd_ring->buffer_info[sw_tpd_next_to_clean];
		if (buffer_info->dma) {
			pci_unmap_page(adapter->pdev, buffer_info->dma,
				       buffer_info->length, PCI_DMA_TODEVICE);
			buffer_info->dma = 0;
		}

		if (buffer_info->skb) {
			dev_kfree_skb_irq(buffer_info->skb);
			buffer_info->skb = NULL;
		}
		tpd->buffer_addr = 0;
		tpd->desc.data = 0;

		if (++sw_tpd_next_to_clean == tpd_ring->count)
			sw_tpd_next_to_clean = 0;
	}
	atomic_set(&tpd_ring->next_to_clean, sw_tpd_next_to_clean);

	if (netif_queue_stopped(adapter->netdev)
	    && netif_carrier_ok(adapter->netdev))
		netif_wake_queue(adapter->netdev);
}

static void atl1_check_for_link(struct atl1_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u16 phy_data = 0;

	spin_lock(&adapter->lock);
	adapter->phy_timer_pending = false;
	atl1_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	atl1_read_phy_reg(&adapter->hw, MII_BMSR, &phy_data);
	spin_unlock(&adapter->lock);

	/* notify upper layer link down ASAP */
	if (!(phy_data & BMSR_LSTATUS)) {	/* Link Down */
		if (netif_carrier_ok(netdev)) {	/* old link state: Up */
			printk(KERN_INFO "%s: %s link is down\n",
			       atl1_driver_name, netdev->name);
			adapter->link_speed = SPEED_0;
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	}
	schedule_work(&adapter->link_chg_task);
}

/*
 * atl1_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 */
static irqreturn_t atl1_intr(int irq, void *data)
{
	/*struct atl1_adapter *adapter = ((struct net_device *)data)->priv;*/
	struct atl1_adapter *adapter = netdev_priv(data);
	u32 status;
	u8 update_rx;
	int max_ints = 10;

	status = adapter->cmb.cmb->int_stats;
	if (!status)
		return IRQ_NONE;

	update_rx = 0;

	do {
		/* clear CMB interrupt status at once */
		adapter->cmb.cmb->int_stats = 0;

		if (status & ISR_GPHY)	/* clear phy status */
			atl1_clear_phy_int(adapter);

		/* clear ISR status, and Enable CMB DMA/Disable Interrupt */
		iowrite32(status | ISR_DIS_INT, adapter->hw.hw_addr + REG_ISR);

		/* check if SMB intr */
		if (status & ISR_SMB)
			atl1_inc_smb(adapter);

		/* check if PCIE PHY Link down */
		if (status & ISR_PHY_LINKDOWN) {
			printk(KERN_DEBUG "%s: pcie phy link down %x\n",
				atl1_driver_name, status);
			if (netif_running(adapter->netdev)) {	/* reset MAC */
				iowrite32(0, adapter->hw.hw_addr + REG_IMR);
				schedule_work(&adapter->pcie_dma_to_rst_task);
				return IRQ_HANDLED;
			}
		}

		/* check if DMA read/write error ? */
		if (status & (ISR_DMAR_TO_RST | ISR_DMAW_TO_RST)) {
			printk(KERN_DEBUG
				"%s: pcie DMA r/w error (status = 0x%x)\n",
				atl1_driver_name, status);
			iowrite32(0, adapter->hw.hw_addr + REG_IMR);
			schedule_work(&adapter->pcie_dma_to_rst_task);
			return IRQ_HANDLED;
		}

		/* link event */
		if (status & ISR_GPHY) {
			adapter->soft_stats.tx_carrier_errors++;
			atl1_check_for_link(adapter);
		}

		/* transmit event */
		if (status & ISR_CMB_TX)
			atl1_intr_tx(adapter);

		/* rx exception */
		if (unlikely(status & (ISR_RXF_OV | ISR_RFD_UNRUN |
				ISR_RRD_OV | ISR_HOST_RFD_UNRUN |
				ISR_HOST_RRD_OV | ISR_CMB_RX))) {
			if (status &
			    (ISR_RXF_OV | ISR_RFD_UNRUN | ISR_RRD_OV |
			     ISR_HOST_RFD_UNRUN | ISR_HOST_RRD_OV))
				printk(KERN_INFO
					"%s: rx exception: status = 0x%x\n",
					atl1_driver_name, status);
			atl1_intr_rx(adapter);
		}

		if (--max_ints < 0)
			break;

	} while ((status = adapter->cmb.cmb->int_stats));

	/* re-enable Interrupt */
	iowrite32(ISR_DIS_SMB | ISR_DIS_DMA, adapter->hw.hw_addr + REG_ISR);
	return IRQ_HANDLED;
}

/*
 * atl1_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 */
static void atl1_set_multi(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	struct dev_mc_list *mc_ptr;
	u32 rctl;
	u32 hash_value;

	/* Check for Promiscuous and All Multicast modes */
	rctl = ioread32(hw->hw_addr + REG_MAC_CTRL);
	if (netdev->flags & IFF_PROMISC)
		rctl |= MAC_CTRL_PROMIS_EN;
	else if (netdev->flags & IFF_ALLMULTI) {
		rctl |= MAC_CTRL_MC_ALL_EN;
		rctl &= ~MAC_CTRL_PROMIS_EN;
	} else
		rctl &= ~(MAC_CTRL_PROMIS_EN | MAC_CTRL_MC_ALL_EN);

	iowrite32(rctl, hw->hw_addr + REG_MAC_CTRL);

	/* clear the old settings from the multicast hash table */
	iowrite32(0, hw->hw_addr + REG_RX_HASH_TABLE);
	iowrite32(0, (hw->hw_addr + REG_RX_HASH_TABLE) + (1 << 2));

	/* compute mc addresses' hash value ,and put it into hash table */
	for (mc_ptr = netdev->mc_list; mc_ptr; mc_ptr = mc_ptr->next) {
		hash_value = atl1_hash_mc_addr(hw, mc_ptr->dmi_addr);
		atl1_hash_set(hw, hash_value);
	}
}

static void atl1_setup_mac_ctrl(struct atl1_adapter *adapter)
{
	u32 value;
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	/* Config MAC CTRL Register */
	value = MAC_CTRL_TX_EN | MAC_CTRL_RX_EN;
	/* duplex */
	if (FULL_DUPLEX == adapter->link_duplex)
		value |= MAC_CTRL_DUPLX;
	/* speed */
	value |= ((u32) ((SPEED_1000 == adapter->link_speed) ?
			 MAC_CTRL_SPEED_1000 : MAC_CTRL_SPEED_10_100) <<
		  MAC_CTRL_SPEED_SHIFT);
	/* flow control */
	value |= (MAC_CTRL_TX_FLOW | MAC_CTRL_RX_FLOW);
	/* PAD & CRC */
	value |= (MAC_CTRL_ADD_CRC | MAC_CTRL_PAD);
	/* preamble length */
	value |= (((u32) adapter->hw.preamble_len
		   & MAC_CTRL_PRMLEN_MASK) << MAC_CTRL_PRMLEN_SHIFT);
	/* vlan */
	if (adapter->vlgrp)
		value |= MAC_CTRL_RMV_VLAN;
	/* rx checksum
	   if (adapter->rx_csum)
	   value |= MAC_CTRL_RX_CHKSUM_EN;
	 */
	/* filter mode */
	value |= MAC_CTRL_BC_EN;
	if (netdev->flags & IFF_PROMISC)
		value |= MAC_CTRL_PROMIS_EN;
	else if (netdev->flags & IFF_ALLMULTI)
		value |= MAC_CTRL_MC_ALL_EN;
	/* value |= MAC_CTRL_LOOPBACK; */
	iowrite32(value, hw->hw_addr + REG_MAC_CTRL);
}

static u32 atl1_check_link(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 ret_val;
	u16 speed, duplex, phy_data;
	int reconfig = 0;

	/* MII_BMSR must read twice */
	atl1_read_phy_reg(hw, MII_BMSR, &phy_data);
	atl1_read_phy_reg(hw, MII_BMSR, &phy_data);
	if (!(phy_data & BMSR_LSTATUS)) {	/* link down */
		if (netif_carrier_ok(netdev)) {	/* old link state: Up */
			printk(KERN_INFO "%s: link is down\n",
				atl1_driver_name);
			adapter->link_speed = SPEED_0;
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
		return ATL1_SUCCESS;
	}

	/* Link Up */
	ret_val = atl1_get_speed_and_duplex(hw, &speed, &duplex);
	if (ret_val)
		return ret_val;

	switch (hw->media_type) {
	case MEDIA_TYPE_1000M_FULL:
		if (speed != SPEED_1000 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_100M_FULL:
		if (speed != SPEED_100 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_100M_HALF:
		if (speed != SPEED_100 || duplex != HALF_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_10M_FULL:
		if (speed != SPEED_10 || duplex != FULL_DUPLEX)
			reconfig = 1;
		break;
	case MEDIA_TYPE_10M_HALF:
		if (speed != SPEED_10 || duplex != HALF_DUPLEX)
			reconfig = 1;
		break;
	}

	/* link result is our setting */
	if (!reconfig) {
		if (adapter->link_speed != speed
		    || adapter->link_duplex != duplex) {
			adapter->link_speed = speed;
			adapter->link_duplex = duplex;
			atl1_setup_mac_ctrl(adapter);
			printk(KERN_INFO "%s: %s link is up %d Mbps %s\n",
			       atl1_driver_name, netdev->name,
			       adapter->link_speed,
			       adapter->link_duplex ==
			       FULL_DUPLEX ? "full duplex" : "half duplex");
		}
		if (!netif_carrier_ok(netdev)) {	/* Link down -> Up */
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
		return ATL1_SUCCESS;
	}

	/* change orignal link status */
	if (netif_carrier_ok(netdev)) {
		adapter->link_speed = SPEED_0;
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}

	if (hw->media_type != MEDIA_TYPE_AUTO_SENSOR &&
	    hw->media_type != MEDIA_TYPE_1000M_FULL) {
		switch (hw->media_type) {
		case MEDIA_TYPE_100M_FULL:
			phy_data = MII_CR_FULL_DUPLEX | MII_CR_SPEED_100 |
			           MII_CR_RESET;
			break;
		case MEDIA_TYPE_100M_HALF:
			phy_data = MII_CR_SPEED_100 | MII_CR_RESET;
			break;
		case MEDIA_TYPE_10M_FULL:
			phy_data =
			    MII_CR_FULL_DUPLEX | MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		default:	/* MEDIA_TYPE_10M_HALF: */
			phy_data = MII_CR_SPEED_10 | MII_CR_RESET;
			break;
		}
		atl1_write_phy_reg(hw, MII_BMCR, phy_data);
		return ATL1_SUCCESS;
	}

	/* auto-neg, insert timer to re-config phy */
	if (!adapter->phy_timer_pending) {
		adapter->phy_timer_pending = true;
		mod_timer(&adapter->phy_config_timer, jiffies + 3 * HZ);
	}

	return ATL1_SUCCESS;
}

static void set_flow_ctrl_old(struct atl1_adapter *adapter)
{
	u32 hi, lo, value;

	/* RFD Flow Control */
	value = adapter->rfd_ring.count;
	hi = value / 16;
	if (hi < 2)
		hi = 2;
	lo = value * 7 / 8;

	value = ((hi & RXQ_RXF_PAUSE_TH_HI_MASK) << RXQ_RXF_PAUSE_TH_HI_SHIFT) |
	    ((lo & RXQ_RXF_PAUSE_TH_LO_MASK) << RXQ_RXF_PAUSE_TH_LO_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_RXQ_RXF_PAUSE_THRESH);

	/* RRD Flow Control */
	value = adapter->rrd_ring.count;
	lo = value / 16;
	hi = value * 7 / 8;
	if (lo < 2)
		lo = 2;
	value = ((hi & RXQ_RRD_PAUSE_TH_HI_MASK) << RXQ_RRD_PAUSE_TH_HI_SHIFT) |
	    ((lo & RXQ_RRD_PAUSE_TH_LO_MASK) << RXQ_RRD_PAUSE_TH_LO_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_RXQ_RRD_PAUSE_THRESH);
}

static void set_flow_ctrl_new(struct atl1_hw *hw)
{
	u32 hi, lo, value;

	/* RXF Flow Control */
	value = ioread32(hw->hw_addr + REG_SRAM_RXF_LEN);
	lo = value / 16;
	if (lo < 192)
		lo = 192;
	hi = value * 7 / 8;
	if (hi < lo)
		hi = lo + 16;
	value = ((hi & RXQ_RXF_PAUSE_TH_HI_MASK) << RXQ_RXF_PAUSE_TH_HI_SHIFT) |
	    ((lo & RXQ_RXF_PAUSE_TH_LO_MASK) << RXQ_RXF_PAUSE_TH_LO_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_RXF_PAUSE_THRESH);

	/* RRD Flow Control */
	value = ioread32(hw->hw_addr + REG_SRAM_RRD_LEN);
	lo = value / 8;
	hi = value * 7 / 8;
	if (lo < 2)
		lo = 2;
	if (hi < lo)
		hi = lo + 3;
	value = ((hi & RXQ_RRD_PAUSE_TH_HI_MASK) << RXQ_RRD_PAUSE_TH_HI_SHIFT) |
	    ((lo & RXQ_RRD_PAUSE_TH_LO_MASK) << RXQ_RRD_PAUSE_TH_LO_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_RRD_PAUSE_THRESH);
}

/*
 * atl1_configure - Configure Transmit&Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx /Rx unit of the MAC after a reset.
 */
static u32 atl1_configure(struct atl1_adapter *adapter)
{
	struct atl1_hw *hw = &adapter->hw;
	u32 value;

	/* clear interrupt status */
	iowrite32(0xffffffff, adapter->hw.hw_addr + REG_ISR);

	/* set MAC Address */
	value = (((u32) hw->mac_addr[2]) << 24) |
		(((u32) hw->mac_addr[3]) << 16) |
		(((u32) hw->mac_addr[4]) << 8) |
		(((u32) hw->mac_addr[5]));
	iowrite32(value, hw->hw_addr + REG_MAC_STA_ADDR);
	value = (((u32) hw->mac_addr[0]) << 8) | (((u32) hw->mac_addr[1]));
	iowrite32(value, hw->hw_addr + (REG_MAC_STA_ADDR + 4));

	/* tx / rx ring */

	/* HI base address */
	iowrite32((u32) ((adapter->tpd_ring.dma & 0xffffffff00000000ULL) >> 32),
		hw->hw_addr + REG_DESC_BASE_ADDR_HI);
	/* LO base address */
	iowrite32((u32) (adapter->rfd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_RFD_ADDR_LO);
	iowrite32((u32) (adapter->rrd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_RRD_ADDR_LO);
	iowrite32((u32) (adapter->tpd_ring.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_TPD_ADDR_LO);
	iowrite32((u32) (adapter->cmb.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_CMB_ADDR_LO);
	iowrite32((u32) (adapter->smb.dma & 0x00000000ffffffffULL),
		hw->hw_addr + REG_DESC_SMB_ADDR_LO);

	/* element count */
	value = adapter->rrd_ring.count;
	value <<= 16;
	value += adapter->rfd_ring.count;
	iowrite32(value, hw->hw_addr + REG_DESC_RFD_RRD_RING_SIZE);
	iowrite32(adapter->tpd_ring.count, hw->hw_addr + REG_DESC_TPD_RING_SIZE);

	/* Load Ptr */
	iowrite32(1, hw->hw_addr + REG_LOAD_PTR);

	/* config Mailbox */
	value = ((atomic_read(&adapter->tpd_ring.next_to_use)
		  & MB_TPD_PROD_INDX_MASK) << MB_TPD_PROD_INDX_SHIFT) |
	    ((atomic_read(&adapter->rrd_ring.next_to_clean)
	      & MB_RRD_CONS_INDX_MASK) << MB_RRD_CONS_INDX_SHIFT) |
	    ((atomic_read(&adapter->rfd_ring.next_to_use)
	      & MB_RFD_PROD_INDX_MASK) << MB_RFD_PROD_INDX_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAILBOX);

	/* config IPG/IFG */
	value = (((u32) hw->ipgt & MAC_IPG_IFG_IPGT_MASK)
		 << MAC_IPG_IFG_IPGT_SHIFT) |
	    (((u32) hw->min_ifg & MAC_IPG_IFG_MIFG_MASK)
	     << MAC_IPG_IFG_MIFG_SHIFT) |
	    (((u32) hw->ipgr1 & MAC_IPG_IFG_IPGR1_MASK)
	     << MAC_IPG_IFG_IPGR1_SHIFT) |
	    (((u32) hw->ipgr2 & MAC_IPG_IFG_IPGR2_MASK)
	     << MAC_IPG_IFG_IPGR2_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAC_IPG_IFG);

	/* config  Half-Duplex Control */
	value = ((u32) hw->lcol & MAC_HALF_DUPLX_CTRL_LCOL_MASK) |
	    (((u32) hw->max_retry & MAC_HALF_DUPLX_CTRL_RETRY_MASK)
	     << MAC_HALF_DUPLX_CTRL_RETRY_SHIFT) |
	    MAC_HALF_DUPLX_CTRL_EXC_DEF_EN |
	    (0xa << MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT) |
	    (((u32) hw->jam_ipg & MAC_HALF_DUPLX_CTRL_JAMIPG_MASK)
	     << MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT);
	iowrite32(value, hw->hw_addr + REG_MAC_HALF_DUPLX_CTRL);

	/* set Interrupt Moderator Timer */
	iowrite16(adapter->imt, hw->hw_addr + REG_IRQ_MODU_TIMER_INIT);
	iowrite32(MASTER_CTRL_ITIMER_EN, hw->hw_addr + REG_MASTER_CTRL);

	/* set Interrupt Clear Timer */
	iowrite16(adapter->ict, hw->hw_addr + REG_CMBDISDMA_TIMER);

	/* set MTU, 4 : VLAN */
	iowrite32(hw->max_frame_size + 4, hw->hw_addr + REG_MTU);

	/* jumbo size & rrd retirement timer */
	value = (((u32) hw->rx_jumbo_th & RXQ_JMBOSZ_TH_MASK)
		 << RXQ_JMBOSZ_TH_SHIFT) |
	    (((u32) hw->rx_jumbo_lkah & RXQ_JMBO_LKAH_MASK)
	     << RXQ_JMBO_LKAH_SHIFT) |
	    (((u32) hw->rrd_ret_timer & RXQ_RRD_TIMER_MASK)
	     << RXQ_RRD_TIMER_SHIFT);
	iowrite32(value, hw->hw_addr + REG_RXQ_JMBOSZ_RRDTIM);

	/* Flow Control */
	switch (hw->dev_rev) {
	case 0x8001:
	case 0x9001:
	case 0x9002:
	case 0x9003:
		set_flow_ctrl_old(adapter);
		break;
	default:
		set_flow_ctrl_new(hw);
		break;
	}

	/* config TXQ */
	value = (((u32) hw->tpd_burst & TXQ_CTRL_TPD_BURST_NUM_MASK)
		 << TXQ_CTRL_TPD_BURST_NUM_SHIFT) |
	    (((u32) hw->txf_burst & TXQ_CTRL_TXF_BURST_NUM_MASK)
	     << TXQ_CTRL_TXF_BURST_NUM_SHIFT) |
	    (((u32) hw->tpd_fetch_th & TXQ_CTRL_TPD_FETCH_TH_MASK)
	     << TXQ_CTRL_TPD_FETCH_TH_SHIFT) | TXQ_CTRL_ENH_MODE | TXQ_CTRL_EN;
	iowrite32(value, hw->hw_addr + REG_TXQ_CTRL);

	/* min tpd fetch gap & tx jumbo packet size threshold for taskoffload */
	value = (((u32) hw->tx_jumbo_task_th & TX_JUMBO_TASK_TH_MASK)
		 << TX_JUMBO_TASK_TH_SHIFT) |
	    (((u32) hw->tpd_fetch_gap & TX_TPD_MIN_IPG_MASK)
	     << TX_TPD_MIN_IPG_SHIFT);
	iowrite32(value, hw->hw_addr + REG_TX_JUMBO_TASK_TH_TPD_IPG);

	/* config RXQ */
	value = (((u32) hw->rfd_burst & RXQ_CTRL_RFD_BURST_NUM_MASK)
		 << RXQ_CTRL_RFD_BURST_NUM_SHIFT) |
	    (((u32) hw->rrd_burst & RXQ_CTRL_RRD_BURST_THRESH_MASK)
	     << RXQ_CTRL_RRD_BURST_THRESH_SHIFT) |
	    (((u32) hw->rfd_fetch_gap & RXQ_CTRL_RFD_PREF_MIN_IPG_MASK)
	     << RXQ_CTRL_RFD_PREF_MIN_IPG_SHIFT) |
	    RXQ_CTRL_CUT_THRU_EN | RXQ_CTRL_EN;
	iowrite32(value, hw->hw_addr + REG_RXQ_CTRL);

	/* config DMA Engine */
	value = ((((u32) hw->dmar_block) & DMA_CTRL_DMAR_BURST_LEN_MASK)
		 << DMA_CTRL_DMAR_BURST_LEN_SHIFT) |
	    ((((u32) hw->dmaw_block) & DMA_CTRL_DMAR_BURST_LEN_MASK)
	     << DMA_CTRL_DMAR_BURST_LEN_SHIFT) |
	    DMA_CTRL_DMAR_EN | DMA_CTRL_DMAW_EN;
	value |= (u32) hw->dma_ord;
	if (atl1_rcb_128 == hw->rcb_value)
		value |= DMA_CTRL_RCB_VALUE;
	iowrite32(value, hw->hw_addr + REG_DMA_CTRL);

	/* config CMB / SMB */
	value = hw->cmb_rrd | ((u32) hw->cmb_tpd << 16);
	iowrite32(value, hw->hw_addr + REG_CMB_WRITE_TH);
	value = hw->cmb_rx_timer | ((u32) hw->cmb_tx_timer << 16);
	iowrite32(value, hw->hw_addr + REG_CMB_WRITE_TIMER);
	iowrite32(hw->smb_timer, hw->hw_addr + REG_SMB_TIMER);

	/* --- enable CMB / SMB */
	value = CSMB_CTRL_CMB_EN | CSMB_CTRL_SMB_EN;
	iowrite32(value, hw->hw_addr + REG_CSMB_CTRL);

	value = ioread32(adapter->hw.hw_addr + REG_ISR);
	if (unlikely((value & ISR_PHY_LINKDOWN) != 0))
		value = 1;	/* config failed */
	else
		value = 0;

	/* clear all interrupt status */
	iowrite32(0x3fffffff, adapter->hw.hw_addr + REG_ISR);
	iowrite32(0, adapter->hw.hw_addr + REG_ISR);
	return value;
}

/*
 * atl1_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static void atl1_irq_disable(struct atl1_adapter *adapter)
{
	atomic_inc(&adapter->irq_sem);
	iowrite32(0, adapter->hw.hw_addr + REG_IMR);
	ioread32(adapter->hw.hw_addr + REG_IMR);
	synchronize_irq(adapter->pdev->irq);
}

static void atl1_vlan_rx_register(struct net_device *netdev,
				struct vlan_group *grp)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&adapter->lock, flags);
	/* atl1_irq_disable(adapter); */
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = ioread32(adapter->hw.hw_addr + REG_MAC_CTRL);
		ctrl |= MAC_CTRL_RMV_VLAN;
		iowrite32(ctrl, adapter->hw.hw_addr + REG_MAC_CTRL);
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = ioread32(adapter->hw.hw_addr + REG_MAC_CTRL);
		ctrl &= ~MAC_CTRL_RMV_VLAN;
		iowrite32(ctrl, adapter->hw.hw_addr + REG_MAC_CTRL);
	}

	/* atl1_irq_enable(adapter); */
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/* FIXME: justify or remove -- CHS */
static void atl1_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	/* We don't do Vlan filtering */
	return;
}

/* FIXME: this looks wrong too -- CHS */
static void atl1_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	/* atl1_irq_disable(adapter); */
	vlan_group_set_device(adapter->vlgrp, vid, NULL);
	/* atl1_irq_enable(adapter); */
	spin_unlock_irqrestore(&adapter->lock, flags);
	/* We don't do Vlan filtering */
	return;
}

static void atl1_restore_vlan(struct atl1_adapter *adapter)
{
	atl1_vlan_rx_register(adapter->netdev, adapter->vlgrp);
	if (adapter->vlgrp) {
		u16 vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!vlan_group_get_device(adapter->vlgrp, vid))
				continue;
			atl1_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}

static u16 tpd_avail(struct atl1_tpd_ring *tpd_ring)
{
	u16 next_to_clean = atomic_read(&tpd_ring->next_to_clean);
	u16 next_to_use = atomic_read(&tpd_ring->next_to_use);
	return ((next_to_clean >
		 next_to_use) ? next_to_clean - next_to_use -
		1 : tpd_ring->count + next_to_clean - next_to_use - 1);
}

static int atl1_tso(struct atl1_adapter *adapter, struct sk_buff *skb,
			 struct tso_param *tso)
{
	/* We enter this function holding a spinlock. */
	u8 ipofst;
	int err;

	if (skb_shinfo(skb)->gso_size) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (unlikely(err))
				return err;
		}

		if (skb->protocol == ntohs(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);

			iph->tot_len = 0;
			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								 iph->daddr, 0,
								 IPPROTO_TCP,
								 0);
			ipofst = skb_network_offset(skb);
			if (ipofst != ENET_HEADER_SIZE) /* 802.3 frame */
				tso->tsopl |= 1 << TSO_PARAM_ETHTYPE_SHIFT;

			tso->tsopl |= (iph->ihl &
				CSUM_PARAM_IPHL_MASK) << CSUM_PARAM_IPHL_SHIFT;
			tso->tsopl |= (tcp_hdrlen(skb) &
				TSO_PARAM_TCPHDRLEN_MASK) << TSO_PARAM_TCPHDRLEN_SHIFT;
			tso->tsopl |= (skb_shinfo(skb)->gso_size &
				TSO_PARAM_MSS_MASK) << TSO_PARAM_MSS_SHIFT;
			tso->tsopl |= 1 << TSO_PARAM_IPCKSUM_SHIFT;
			tso->tsopl |= 1 << TSO_PARAM_TCPCKSUM_SHIFT;
			tso->tsopl |= 1 << TSO_PARAM_SEGMENT_SHIFT;
			return true;
		}
	}
	return false;
}

static int atl1_tx_csum(struct atl1_adapter *adapter, struct sk_buff *skb,
			struct csum_param *csum)
{
	u8 css, cso;

	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		cso = skb_transport_offset(skb);
		css = cso + skb->csum_offset;
		if (unlikely(cso & 0x1)) {
			printk(KERN_DEBUG "%s: payload offset != even number\n",
				atl1_driver_name);
			return -1;
		}
		csum->csumpl |= (cso & CSUM_PARAM_PLOADOFFSET_MASK) <<
			CSUM_PARAM_PLOADOFFSET_SHIFT;
		csum->csumpl |= (css & CSUM_PARAM_XSUMOFFSET_MASK) <<
			CSUM_PARAM_XSUMOFFSET_SHIFT;
		csum->csumpl |= 1 << CSUM_PARAM_CUSTOMCKSUM_SHIFT;
		return true;
	}

	return true;
}

static void atl1_tx_map(struct atl1_adapter *adapter,
				struct sk_buff *skb, bool tcp_seg)
{
	/* We enter this function holding a spinlock. */
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	struct page *page;
	int first_buf_len = skb->len;
	unsigned long offset;
	unsigned int nr_frags;
	unsigned int f;
	u16 tpd_next_to_use;
	u16 proto_hdr_len;
	u16 i, m, len12;

	first_buf_len -= skb->data_len;
	nr_frags = skb_shinfo(skb)->nr_frags;
	tpd_next_to_use = atomic_read(&tpd_ring->next_to_use);
	buffer_info = &tpd_ring->buffer_info[tpd_next_to_use];
	if (unlikely(buffer_info->skb))
		BUG();
	buffer_info->skb = NULL;	/* put skb in last TPD */

	if (tcp_seg) {
		/* TSO/GSO */
		proto_hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		buffer_info->length = proto_hdr_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(adapter->pdev, page,
						offset, proto_hdr_len,
						PCI_DMA_TODEVICE);

		if (++tpd_next_to_use == tpd_ring->count)
			tpd_next_to_use = 0;

		if (first_buf_len > proto_hdr_len) {
			len12 = first_buf_len - proto_hdr_len;
			m = (len12 + MAX_TX_BUF_LEN - 1) / MAX_TX_BUF_LEN;
			for (i = 0; i < m; i++) {
				buffer_info =
				    &tpd_ring->buffer_info[tpd_next_to_use];
				buffer_info->skb = NULL;
				buffer_info->length =
				    (MAX_TX_BUF_LEN >=
				     len12) ? MAX_TX_BUF_LEN : len12;
				len12 -= buffer_info->length;
				page = virt_to_page(skb->data +
						 (proto_hdr_len +
						  i * MAX_TX_BUF_LEN));
				offset = (unsigned long)(skb->data +
							(proto_hdr_len +
							i * MAX_TX_BUF_LEN)) &
							~PAGE_MASK;
				buffer_info->dma =
				    pci_map_page(adapter->pdev, page, offset,
						 buffer_info->length,
						 PCI_DMA_TODEVICE);
				if (++tpd_next_to_use == tpd_ring->count)
					tpd_next_to_use = 0;
			}
		}
	} else {
		/* not TSO/GSO */
		buffer_info->length = first_buf_len;
		page = virt_to_page(skb->data);
		offset = (unsigned long)skb->data & ~PAGE_MASK;
		buffer_info->dma = pci_map_page(adapter->pdev, page,
						offset, first_buf_len,
						PCI_DMA_TODEVICE);
		if (++tpd_next_to_use == tpd_ring->count)
			tpd_next_to_use = 0;
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;
		u16 lenf, i, m;

		frag = &skb_shinfo(skb)->frags[f];
		lenf = frag->size;

		m = (lenf + MAX_TX_BUF_LEN - 1) / MAX_TX_BUF_LEN;
		for (i = 0; i < m; i++) {
			buffer_info = &tpd_ring->buffer_info[tpd_next_to_use];
			if (unlikely(buffer_info->skb))
				BUG();
			buffer_info->skb = NULL;
			buffer_info->length =
			    (lenf > MAX_TX_BUF_LEN) ? MAX_TX_BUF_LEN : lenf;
			lenf -= buffer_info->length;
			buffer_info->dma =
			    pci_map_page(adapter->pdev, frag->page,
					 frag->page_offset + i * MAX_TX_BUF_LEN,
					 buffer_info->length, PCI_DMA_TODEVICE);

			if (++tpd_next_to_use == tpd_ring->count)
				tpd_next_to_use = 0;
		}
	}

	/* last tpd's buffer-info */
	buffer_info->skb = skb;
}

static void atl1_tx_queue(struct atl1_adapter *adapter, int count,
			       union tpd_descr *descr)
{
	/* We enter this function holding a spinlock. */
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	int j;
	u32 val;
	struct atl1_buffer *buffer_info;
	struct tx_packet_desc *tpd;
	u16 tpd_next_to_use = atomic_read(&tpd_ring->next_to_use);

	for (j = 0; j < count; j++) {
		buffer_info = &tpd_ring->buffer_info[tpd_next_to_use];
		tpd = ATL1_TPD_DESC(&adapter->tpd_ring, tpd_next_to_use);
		tpd->desc.csum.csumpu = descr->csum.csumpu;
		tpd->desc.csum.csumpl = descr->csum.csumpl;
		tpd->desc.tso.tsopu = descr->tso.tsopu;
		tpd->desc.tso.tsopl = descr->tso.tsopl;
		tpd->buffer_addr = cpu_to_le64(buffer_info->dma);
		tpd->desc.data = descr->data;
		tpd->desc.csum.csumpu |= (cpu_to_le16(buffer_info->length) &
			CSUM_PARAM_BUFLEN_MASK) << CSUM_PARAM_BUFLEN_SHIFT;

		val = (descr->tso.tsopl >> TSO_PARAM_SEGMENT_SHIFT) &
			TSO_PARAM_SEGMENT_MASK;
		if (val && !j)
			tpd->desc.tso.tsopl |= 1 << TSO_PARAM_HDRFLAG_SHIFT;

		if (j == (count - 1))
			tpd->desc.csum.csumpl |= 1 << CSUM_PARAM_EOP_SHIFT;

		if (++tpd_next_to_use == tpd_ring->count)
			tpd_next_to_use = 0;
	}
	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	atomic_set(&tpd_ring->next_to_use, (int)tpd_next_to_use);
}

static void atl1_update_mailbox(struct atl1_adapter *adapter)
{
	unsigned long flags;
	u32 tpd_next_to_use;
	u32 rfd_next_to_use;
	u32 rrd_next_to_clean;
	u32 value;

	spin_lock_irqsave(&adapter->mb_lock, flags);

	tpd_next_to_use = atomic_read(&adapter->tpd_ring.next_to_use);
	rfd_next_to_use = atomic_read(&adapter->rfd_ring.next_to_use);
	rrd_next_to_clean = atomic_read(&adapter->rrd_ring.next_to_clean);

	value = ((rfd_next_to_use & MB_RFD_PROD_INDX_MASK) <<
		MB_RFD_PROD_INDX_SHIFT) |
		((rrd_next_to_clean & MB_RRD_CONS_INDX_MASK) <<
		MB_RRD_CONS_INDX_SHIFT) |
		((tpd_next_to_use & MB_TPD_PROD_INDX_MASK) <<
		MB_TPD_PROD_INDX_SHIFT);
	iowrite32(value, adapter->hw.hw_addr + REG_MAILBOX);

	spin_unlock_irqrestore(&adapter->mb_lock, flags);
}

static int atl1_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int len = skb->len;
	int tso;
	int count = 1;
	int ret_val;
	u32 val;
	union tpd_descr param;
	u16 frag_size;
	u16 vlan_tag;
	unsigned long flags;
	unsigned int nr_frags = 0;
	unsigned int mss = 0;
	unsigned int f;
	unsigned int proto_hdr_len;

	len -= skb->data_len;

	if (unlikely(skb->len == 0)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	param.data = 0;
	param.tso.tsopu = 0;
	param.tso.tsopl = 0;
	param.csum.csumpu = 0;
	param.csum.csumpl = 0;

	/* nr_frags will be nonzero if we're doing scatter/gather (SG) */
	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++) {
		frag_size = skb_shinfo(skb)->frags[f].size;
		if (frag_size)
			count +=
			    (frag_size + MAX_TX_BUF_LEN - 1) / MAX_TX_BUF_LEN;
	}

	/* mss will be nonzero if we're doing segment offload (TSO/GSO) */
	mss = skb_shinfo(skb)->gso_size;
	if (mss) {
		if (skb->protocol == htons(ETH_P_IP)) {
			proto_hdr_len = (skb_transport_offset(skb) +
					 tcp_hdrlen(skb));
			if (unlikely(proto_hdr_len > len)) {
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
			/* need additional TPD ? */
			if (proto_hdr_len != len)
				count += (len - proto_hdr_len +
					MAX_TX_BUF_LEN - 1) / MAX_TX_BUF_LEN;
		}
	}

	local_irq_save(flags);
	if (!spin_trylock(&adapter->lock)) {
		/* Can't get lock - tell upper layer to requeue */
		local_irq_restore(flags);
		printk(KERN_DEBUG "%s: TX locked\n", atl1_driver_name);
		return NETDEV_TX_LOCKED;
	}

	if (tpd_avail(&adapter->tpd_ring) < count) {
		/* not enough descriptors */
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&adapter->lock, flags);
		printk(KERN_DEBUG "%s: TX busy\n", atl1_driver_name);
		return NETDEV_TX_BUSY;
	}

	param.data = 0;

	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		vlan_tag = vlan_tx_tag_get(skb);
		vlan_tag = (vlan_tag << 4) | (vlan_tag >> 13) |
			((vlan_tag >> 9) & 0x8);
		param.csum.csumpl |= 1 << CSUM_PARAM_INSVLAG_SHIFT;
		param.csum.csumpu |= (vlan_tag & CSUM_PARAM_VALANTAG_MASK) <<
			CSUM_PARAM_VALAN_SHIFT;
	}

	tso = atl1_tso(adapter, skb, &param.tso);
	if (tso < 0) {
		spin_unlock_irqrestore(&adapter->lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (!tso) {
		ret_val = atl1_tx_csum(adapter, skb, &param.csum);
		if (ret_val < 0) {
			spin_unlock_irqrestore(&adapter->lock, flags);
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}

	val = (param.csum.csumpl >> CSUM_PARAM_SEGMENT_SHIFT) &
		CSUM_PARAM_SEGMENT_MASK;
	atl1_tx_map(adapter, skb, 1 == val);
	atl1_tx_queue(adapter, count, &param);
	netdev->trans_start = jiffies;
	spin_unlock_irqrestore(&adapter->lock, flags);
	atl1_update_mailbox(adapter);
	return NETDEV_TX_OK;
}

/*
 * atl1_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 */
static struct net_device_stats *atl1_get_stats(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	return &adapter->net_stats;
}

/*
 * atl1_clean_rx_ring - Free RFD Buffers
 * @adapter: board private structure
 */
static void atl1_clean_rx_ring(struct atl1_adapter *adapter)
{
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rfd_ring->count; i++) {
		buffer_info = &rfd_ring->buffer_info[i];
		if (buffer_info->dma) {
			pci_unmap_page(pdev,
					buffer_info->dma,
					buffer_info->length,
					PCI_DMA_FROMDEVICE);
			buffer_info->dma = 0;
		}
		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	size = sizeof(struct atl1_buffer) * rfd_ring->count;
	memset(rfd_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rfd_ring->desc, 0, rfd_ring->size);

	rfd_ring->next_to_clean = 0;
	atomic_set(&rfd_ring->next_to_use, 0);

	rrd_ring->next_to_use = 0;
	atomic_set(&rrd_ring->next_to_clean, 0);
}

/*
 * atl1_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 */
static void atl1_clean_tx_ring(struct atl1_adapter *adapter)
{
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tpd_ring->count; i++) {
		buffer_info = &tpd_ring->buffer_info[i];
		if (buffer_info->dma) {
			pci_unmap_page(pdev, buffer_info->dma,
				       buffer_info->length, PCI_DMA_TODEVICE);
			buffer_info->dma = 0;
		}
	}

	for (i = 0; i < tpd_ring->count; i++) {
		buffer_info = &tpd_ring->buffer_info[i];
		if (buffer_info->skb) {
			dev_kfree_skb_any(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	size = sizeof(struct atl1_buffer) * tpd_ring->count;
	memset(tpd_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tpd_ring->desc, 0, tpd_ring->size);

	atomic_set(&tpd_ring->next_to_use, 0);
	atomic_set(&tpd_ring->next_to_clean, 0);
}

/*
 * atl1_free_ring_resources - Free Tx / RX descriptor Resources
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
void atl1_free_ring_resources(struct atl1_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct atl1_tpd_ring *tpd_ring = &adapter->tpd_ring;
	struct atl1_rfd_ring *rfd_ring = &adapter->rfd_ring;
	struct atl1_rrd_ring *rrd_ring = &adapter->rrd_ring;
	struct atl1_ring_header *ring_header = &adapter->ring_header;

	atl1_clean_tx_ring(adapter);
	atl1_clean_rx_ring(adapter);

	kfree(tpd_ring->buffer_info);
	pci_free_consistent(pdev, ring_header->size, ring_header->desc,
			    ring_header->dma);

	tpd_ring->buffer_info = NULL;
	tpd_ring->desc = NULL;
	tpd_ring->dma = 0;

	rfd_ring->buffer_info = NULL;
	rfd_ring->desc = NULL;
	rfd_ring->dma = 0;

	rrd_ring->desc = NULL;
	rrd_ring->dma = 0;
}

s32 atl1_up(struct atl1_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;
	int irq_flags = IRQF_SAMPLE_RANDOM;

	/* hardware has been reset, we need to reload some things */
	atl1_set_multi(netdev);
	atl1_restore_vlan(adapter);
	err = atl1_alloc_rx_buffers(adapter);
	if (unlikely(!err))		/* no RX BUFFER allocated */
		return -ENOMEM;

	if (unlikely(atl1_configure(adapter))) {
		err = -EIO;
		goto err_up;
	}

	err = pci_enable_msi(adapter->pdev);
	if (err) {
		dev_info(&adapter->pdev->dev,
			"Unable to enable MSI: %d\n", err);
		irq_flags |= IRQF_SHARED;
	}

	err = request_irq(adapter->pdev->irq, &atl1_intr, irq_flags,
			netdev->name, netdev);
	if (unlikely(err))
		goto err_up;

	mod_timer(&adapter->watchdog_timer, jiffies);
	atl1_irq_enable(adapter);
	atl1_check_link(adapter);
	return 0;

	/* FIXME: unreachable code! -- CHS */
	/* free irq disable any interrupt */
	iowrite32(0, adapter->hw.hw_addr + REG_IMR);
	free_irq(adapter->pdev->irq, netdev);

err_up:
	pci_disable_msi(adapter->pdev);
	/* free rx_buffers */
	atl1_clean_rx_ring(adapter);
	return err;
}

void atl1_down(struct atl1_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_config_timer);
	adapter->phy_timer_pending = false;

	atl1_irq_disable(adapter);
	free_irq(adapter->pdev->irq, netdev);
	pci_disable_msi(adapter->pdev);
	atl1_reset_hw(&adapter->hw);
	adapter->cmb.cmb->int_stats = 0;

	adapter->link_speed = SPEED_0;
	adapter->link_duplex = -1;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	atl1_clean_tx_ring(adapter);
	atl1_clean_rx_ring(adapter);
}

/*
 * atl1_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int atl1_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int old_mtu = netdev->mtu;
	int max_frame = new_mtu + ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;

	if ((max_frame < MINIMUM_ETHERNET_FRAME_SIZE) ||
	    (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		printk(KERN_WARNING "%s: invalid MTU setting\n",
			atl1_driver_name);
		return -EINVAL;
	}

	adapter->hw.max_frame_size = max_frame;
	adapter->hw.tx_jumbo_task_th = (max_frame + 7) >> 3;
	adapter->rx_buffer_len = (max_frame + 7) & ~7;
	adapter->hw.rx_jumbo_th = adapter->rx_buffer_len / 8;

	netdev->mtu = new_mtu;
	if ((old_mtu != new_mtu) && netif_running(netdev)) {
		atl1_down(adapter);
		atl1_up(adapter);
	}

	return 0;
}

/*
 * atl1_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 */
static int atl1_set_mac(struct net_device *netdev, void *p)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac_addr, addr->sa_data, netdev->addr_len);

	atl1_set_mac_addr(&adapter->hw);
	return 0;
}

/*
 * atl1_watchdog - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1_watchdog(unsigned long data)
{
	struct atl1_adapter *adapter = (struct atl1_adapter *)data;

	/* Reset the timer */
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

static int mdio_read(struct net_device *netdev, int phy_id, int reg_num)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	u16 result;

	atl1_read_phy_reg(&adapter->hw, reg_num & 0x1f, &result);

	return result;
}

static void mdio_write(struct net_device *netdev, int phy_id, int reg_num, int val)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);

	atl1_write_phy_reg(&adapter->hw, reg_num, val);
}

/*
 * atl1_mii_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 */
static int atl1_mii_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	unsigned long flags;
	int retval;

	if (!netif_running(netdev))
		return -EINVAL;

	spin_lock_irqsave(&adapter->lock, flags);
	retval = generic_mii_ioctl(&adapter->mii, if_mii(ifr), cmd, NULL);
	spin_unlock_irqrestore(&adapter->lock, flags);

	return retval;
}

/*
 * atl1_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 */
static int atl1_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return atl1_mii_ioctl(netdev, ifr, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

/*
 * atl1_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 */
static void atl1_tx_timeout(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->tx_timeout_task);
}

/*
 * atl1_phy_config - Timer Call-back
 * @data: pointer to netdev cast into an unsigned long
 */
static void atl1_phy_config(unsigned long data)
{
	struct atl1_adapter *adapter = (struct atl1_adapter *)data;
	struct atl1_hw *hw = &adapter->hw;
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	adapter->phy_timer_pending = false;
	atl1_write_phy_reg(hw, MII_ADVERTISE, hw->mii_autoneg_adv_reg);
	atl1_write_phy_reg(hw, MII_AT001_CR, hw->mii_1000t_ctrl_reg);
	atl1_write_phy_reg(hw, MII_BMCR, MII_CR_RESET | MII_CR_AUTO_NEG_EN);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

int atl1_reset(struct atl1_adapter *adapter)
{
	int ret;

	ret = atl1_reset_hw(&adapter->hw);
	if (ret != ATL1_SUCCESS)
		return ret;
	return atl1_init_hw(&adapter->hw);
}

/*
 * atl1_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 */
static int atl1_open(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	int err;

	/* allocate transmit descriptors */
	err = atl1_setup_ring_resources(adapter);
	if (err)
		return err;

	err = atl1_up(adapter);
	if (err)
		goto err_up;

	return 0;

err_up:
	atl1_reset(adapter);
	return err;
}

/*
 * atl1_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 */
static int atl1_close(struct net_device *netdev)
{
	struct atl1_adapter *adapter = netdev_priv(netdev);
	atl1_down(adapter);
	atl1_free_ring_resources(adapter);
	return 0;
}

/*
 * If TPD Buffer size equal to 0, PCIE DMAR_TO_INT
 * will assert. We do soft reset <0x1400=1> according
 * with the SPEC. BUT, it seemes that PCIE or DMA
 * state-machine will not be reset. DMAR_TO_INT will
 * assert again and again.
 */
static void atl1_tx_timeout_task(struct work_struct *work)
{
	struct atl1_adapter *adapter =
		container_of(work, struct atl1_adapter, tx_timeout_task);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);
	atl1_down(adapter);
	atl1_up(adapter);
	netif_device_attach(netdev);
}

/*
 * atl1_link_chg_task - deal with link change event Out of interrupt context
 */
static void atl1_link_chg_task(struct work_struct *work)
{
	struct atl1_adapter *adapter =
               container_of(work, struct atl1_adapter, link_chg_task);
	unsigned long flags;

	spin_lock_irqsave(&adapter->lock, flags);
	atl1_check_link(adapter);
	spin_unlock_irqrestore(&adapter->lock, flags);
}

/*
 * atl1_pcie_patch - Patch for PCIE module
 */
static void atl1_pcie_patch(struct atl1_adapter *adapter)
{
	u32 value;
	value = 0x6500;
	iowrite32(value, adapter->hw.hw_addr + 0x12FC);
	/* pcie flow control mode change */
	value = ioread32(adapter->hw.hw_addr + 0x1008);
	value |= 0x8000;
	iowrite32(value, adapter->hw.hw_addr + 0x1008);
}

/*
 * When ACPI resume on some VIA MotherBoard, the Interrupt Disable bit/0x400
 * on PCI Command register is disable.
 * The function enable this bit.
 * Brackett, 2006/03/15
 */
static void atl1_via_workaround(struct atl1_adapter *adapter)
{
	unsigned long value;

	value = ioread16(adapter->hw.hw_addr + PCI_COMMAND);
	if (value & PCI_COMMAND_INTX_DISABLE)
		value &= ~PCI_COMMAND_INTX_DISABLE;
	iowrite32(value, adapter->hw.hw_addr + PCI_COMMAND);
}

/*
 * atl1_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in atl1_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * atl1_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 */
static int __devinit atl1_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct atl1_adapter *adapter;
	static int cards_found = 0;
	bool pci_using_64 = true;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	err = pci_set_dma_mask(pdev, DMA_64BIT_MASK);
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			printk(KERN_DEBUG
				"%s: no usable DMA configuration, aborting\n",
				atl1_driver_name);
			goto err_dma;
		}
		pci_using_64 = false;
	}
	/* Mark all PCI regions associated with PCI device
	 * pdev as being reserved by owner atl1_driver_name
	 */
	err = pci_request_regions(pdev, atl1_driver_name);
	if (err)
		goto err_request_regions;

	/* Enables bus-mastering on the device and calls
	 * pcibios_set_master to do the needed arch specific settings
	 */
	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct atl1_adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}
	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;

	adapter->hw.hw_addr = pci_iomap(pdev, 0, 0);
	if (!adapter->hw.hw_addr) {
		err = -EIO;
		goto err_pci_iomap;
	}
	/* get device revision number */
	adapter->hw.dev_rev = ioread16(adapter->hw.hw_addr + (REG_MASTER_CTRL + 2));

	/* set default ring resource counts */
	adapter->rfd_ring.count = adapter->rrd_ring.count = ATL1_DEFAULT_RFD;
	adapter->tpd_ring.count = ATL1_DEFAULT_TPD;

	adapter->mii.dev = netdev;
	adapter->mii.mdio_read = mdio_read;
	adapter->mii.mdio_write = mdio_write;
	adapter->mii.phy_id_mask = 0x1f;
	adapter->mii.reg_num_mask = 0x1f;

	netdev->open = &atl1_open;
	netdev->stop = &atl1_close;
	netdev->hard_start_xmit = &atl1_xmit_frame;
	netdev->get_stats = &atl1_get_stats;
	netdev->set_multicast_list = &atl1_set_multi;
	netdev->set_mac_address = &atl1_set_mac;
	netdev->change_mtu = &atl1_change_mtu;
	netdev->do_ioctl = &atl1_ioctl;
	netdev->tx_timeout = &atl1_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
	netdev->vlan_rx_register = atl1_vlan_rx_register;
	netdev->vlan_rx_add_vid = atl1_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = atl1_vlan_rx_kill_vid;
	netdev->ethtool_ops = &atl1_ethtool_ops;
	adapter->bd_number = cards_found;
	adapter->pci_using_64 = pci_using_64;

	/* setup the private structure */
	err = atl1_sw_init(adapter);
	if (err)
		goto err_common;

	netdev->features = NETIF_F_HW_CSUM;
	netdev->features |= NETIF_F_SG;
	netdev->features |= (NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX);

	/*
	 * FIXME - Until tso performance gets fixed, disable the feature.
	 * Enable it with ethtool -K if desired.
	 */
	/* netdev->features |= NETIF_F_TSO; */

	if (pci_using_64)
		netdev->features |= NETIF_F_HIGHDMA;

	netdev->features |= NETIF_F_LLTX;

	/*
	 * patch for some L1 of old version,
	 * the final version of L1 may not need these
	 * patches
	 */
	/* atl1_pcie_patch(adapter); */

	/* really reset GPHY core */
	iowrite16(0, adapter->hw.hw_addr + REG_GPHY_ENABLE);

	/*
	 * reset the controller to
	 * put the device in a known good starting state
	 */
	if (atl1_reset_hw(&adapter->hw)) {
		err = -EIO;
		goto err_common;
	}

	/* copy the MAC address out of the EEPROM */
	atl1_read_mac_addr(&adapter->hw);
	memcpy(netdev->dev_addr, adapter->hw.mac_addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		err = -EIO;
		goto err_common;
	}

	atl1_check_options(adapter);

	/* pre-init the MAC, and setup link */
	err = atl1_init_hw(&adapter->hw);
	if (err) {
		err = -EIO;
		goto err_common;
	}

	atl1_pcie_patch(adapter);
	/* assume we have no link for now */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &atl1_watchdog;
	adapter->watchdog_timer.data = (unsigned long)adapter;

	init_timer(&adapter->phy_config_timer);
	adapter->phy_config_timer.function = &atl1_phy_config;
	adapter->phy_config_timer.data = (unsigned long)adapter;
	adapter->phy_timer_pending = false;

	INIT_WORK(&adapter->tx_timeout_task, atl1_tx_timeout_task);

	INIT_WORK(&adapter->link_chg_task, atl1_link_chg_task);

	INIT_WORK(&adapter->pcie_dma_to_rst_task, atl1_tx_timeout_task);

	err = register_netdev(netdev);
	if (err)
		goto err_common;

	cards_found++;
	atl1_via_workaround(adapter);
	return 0;

err_common:
	pci_iounmap(pdev, adapter->hw.hw_addr);
err_pci_iomap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_dma:
err_request_regions:
	pci_disable_device(pdev);
	return err;
}

/*
 * atl1_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * atl1_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void __devexit atl1_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter;
	/* Device not available. Return. */
	if (!netdev)
		return;

	adapter = netdev_priv(netdev);

	/* Some atl1 boards lack persistent storage for their MAC, and get it
	 * from the BIOS during POST.  If we've been messing with the MAC
	 * address, we need to save the permanent one.
	 */
	if (memcmp(adapter->hw.mac_addr, adapter->hw.perm_mac_addr, ETH_ALEN)) {
		memcpy(adapter->hw.mac_addr, adapter->hw.perm_mac_addr, ETH_ALEN);
		atl1_set_mac_addr(&adapter->hw);
	}

	iowrite16(0, adapter->hw.hw_addr + REG_GPHY_ENABLE);
	unregister_netdev(netdev);
	pci_iounmap(pdev, adapter->hw.hw_addr);
	pci_release_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
static int atl1_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter = netdev_priv(netdev);
	struct atl1_hw *hw = &adapter->hw;
	u32 ctrl = 0;
	u32 wufc = adapter->wol;

	netif_device_detach(netdev);
	if (netif_running(netdev))
		atl1_down(adapter);

	atl1_read_phy_reg(hw, MII_BMSR, (u16 *) & ctrl);
	atl1_read_phy_reg(hw, MII_BMSR, (u16 *) & ctrl);
	if (ctrl & BMSR_LSTATUS)
		wufc &= ~ATL1_WUFC_LNKC;

	/* reduce speed to 10/100M */
	if (wufc) {
		atl1_phy_enter_power_saving(hw);
		/* if resume, let driver to re- setup link */
		hw->phy_configured = false;
		atl1_set_mac_addr(hw);
		atl1_set_multi(netdev);

		ctrl = 0;
		/* turn on magic packet wol */
		if (wufc & ATL1_WUFC_MAG)
			ctrl = WOL_MAGIC_EN | WOL_MAGIC_PME_EN;

		/* turn on Link change WOL */
		if (wufc & ATL1_WUFC_LNKC)
			ctrl |= (WOL_LINK_CHG_EN | WOL_LINK_CHG_PME_EN);
		iowrite32(ctrl, hw->hw_addr + REG_WOL_CTRL);

		/* turn on all-multi mode if wake on multicast is enabled */
		ctrl = ioread32(hw->hw_addr + REG_MAC_CTRL);
		ctrl &= ~MAC_CTRL_DBG;
		ctrl &= ~MAC_CTRL_PROMIS_EN;
		if (wufc & ATL1_WUFC_MC)
			ctrl |= MAC_CTRL_MC_ALL_EN;
		else
			ctrl &= ~MAC_CTRL_MC_ALL_EN;

		/* turn on broadcast mode if wake on-BC is enabled */
		if (wufc & ATL1_WUFC_BC)
			ctrl |= MAC_CTRL_BC_EN;
		else
			ctrl &= ~MAC_CTRL_BC_EN;

		/* enable RX */
		ctrl |= MAC_CTRL_RX_EN;
		iowrite32(ctrl, hw->hw_addr + REG_MAC_CTRL);
		pci_enable_wake(pdev, PCI_D3hot, 1);
		pci_enable_wake(pdev, PCI_D3cold, 1);	/* 4 == D3 cold */
	} else {
		iowrite32(0, hw->hw_addr + REG_WOL_CTRL);
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);	/* 4 == D3 cold */
	}

	pci_save_state(pdev);
	pci_disable_device(pdev);

	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int atl1_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct atl1_adapter *adapter = netdev_priv(netdev);
	u32 ret_val;

	pci_set_power_state(pdev, 0);
	pci_restore_state(pdev);

	ret_val = pci_enable_device(pdev);
	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	iowrite32(0, adapter->hw.hw_addr + REG_WOL_CTRL);
	atl1_reset(adapter);

	if (netif_running(netdev))
		atl1_up(adapter);
	netif_device_attach(netdev);

	atl1_via_workaround(adapter);

	return 0;
}
#else
#define atl1_suspend NULL
#define atl1_resume NULL
#endif

static struct pci_driver atl1_driver = {
	.name = atl1_driver_name,
	.id_table = atl1_pci_tbl,
	.probe = atl1_probe,
	.remove = __devexit_p(atl1_remove),
	/* Power Managment Hooks */
	/* probably broken right now -- CHS */
	.suspend = atl1_suspend,
	.resume = atl1_resume
};

/*
 * atl1_exit_module - Driver Exit Cleanup Routine
 *
 * atl1_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit atl1_exit_module(void)
{
	pci_unregister_driver(&atl1_driver);
}

/*
 * atl1_init_module - Driver Registration Routine
 *
 * atl1_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init atl1_init_module(void)
{
	printk(KERN_INFO "%s - version %s\n", atl1_driver_string, DRIVER_VERSION);
	printk(KERN_INFO "%s\n", atl1_copyright);
	return pci_register_driver(&atl1_driver);
}

module_init(atl1_init_module);
module_exit(atl1_exit_module);
