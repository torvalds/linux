/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2010 Intel Corporation.

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

#include "ixgbe.h"
#ifdef CONFIG_IXGBE_DCB
#include "ixgbe_dcb_82599.h"
#endif /* CONFIG_IXGBE_DCB */
#include <linux/if_ether.h>
#include <linux/gfp.h>
#include <linux/if_vlan.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>
#include <scsi/libfcoe.h>

/**
 * ixgbe_rx_is_fcoe - check the rx desc for incoming pkt type
 * @rx_desc: advanced rx descriptor
 *
 * Returns : true if it is FCoE pkt
 */
static inline bool ixgbe_rx_is_fcoe(union ixgbe_adv_rx_desc *rx_desc)
{
	u16 p;

	p = le16_to_cpu(rx_desc->wb.lower.lo_dword.hs_rss.pkt_info);
	if (p & IXGBE_RXDADV_PKTTYPE_ETQF) {
		p &= IXGBE_RXDADV_PKTTYPE_ETQF_MASK;
		p >>= IXGBE_RXDADV_PKTTYPE_ETQF_SHIFT;
		return p == IXGBE_ETQF_FILTER_FCOE;
	}
	return false;
}

/**
 * ixgbe_fcoe_clear_ddp - clear the given ddp context
 * @ddp - ptr to the ixgbe_fcoe_ddp
 *
 * Returns : none
 *
 */
static inline void ixgbe_fcoe_clear_ddp(struct ixgbe_fcoe_ddp *ddp)
{
	ddp->len = 0;
	ddp->err = 0;
	ddp->udl = NULL;
	ddp->udp = 0UL;
	ddp->sgl = NULL;
	ddp->sgc = 0;
}

/**
 * ixgbe_fcoe_ddp_put - free the ddp context for a given xid
 * @netdev: the corresponding net_device
 * @xid: the xid that corresponding ddp will be freed
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_done
 * and it is expected to be called by ULD, i.e., FCP layer of libfc
 * to release the corresponding ddp context when the I/O is done.
 *
 * Returns : data length already ddp-ed in bytes
 */
int ixgbe_fcoe_ddp_put(struct net_device *netdev, u16 xid)
{
	int len = 0;
	struct ixgbe_fcoe *fcoe;
	struct ixgbe_adapter *adapter;
	struct ixgbe_fcoe_ddp *ddp;

	if (!netdev)
		goto out_ddp_put;

	if (xid >= IXGBE_FCOE_DDP_MAX)
		goto out_ddp_put;

	adapter = netdev_priv(netdev);
	fcoe = &adapter->fcoe;
	ddp = &fcoe->ddp[xid];
	if (!ddp->udl)
		goto out_ddp_put;

	len = ddp->len;
	/* if there an error, force to invalidate ddp context */
	if (ddp->err) {
		spin_lock_bh(&fcoe->lock);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCFLT, 0);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCFLTRW,
				(xid | IXGBE_FCFLTRW_WE));
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCBUFF, 0);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCDMARW,
				(xid | IXGBE_FCDMARW_WE));
		spin_unlock_bh(&fcoe->lock);
	}
	if (ddp->sgl)
		pci_unmap_sg(adapter->pdev, ddp->sgl, ddp->sgc,
			     DMA_FROM_DEVICE);
	pci_pool_free(fcoe->pool, ddp->udl, ddp->udp);
	ixgbe_fcoe_clear_ddp(ddp);

out_ddp_put:
	return len;
}

/**
 * ixgbe_fcoe_ddp_get - called to set up ddp context
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_setup
 * and is expected to be called from ULD, e.g., FCP layer of libfc
 * to set up ddp for the corresponding xid of the given sglist for
 * the corresponding I/O.
 *
 * Returns : 1 for success and 0 for no ddp
 */
int ixgbe_fcoe_ddp_get(struct net_device *netdev, u16 xid,
		       struct scatterlist *sgl, unsigned int sgc)
{
	struct ixgbe_adapter *adapter;
	struct ixgbe_hw *hw;
	struct ixgbe_fcoe *fcoe;
	struct ixgbe_fcoe_ddp *ddp;
	struct scatterlist *sg;
	unsigned int i, j, dmacount;
	unsigned int len;
	static const unsigned int bufflen = 4096;
	unsigned int firstoff = 0;
	unsigned int lastsize;
	unsigned int thisoff = 0;
	unsigned int thislen = 0;
	u32 fcbuff, fcdmarw, fcfltrw;
	dma_addr_t addr;

	if (!netdev || !sgl)
		return 0;

	adapter = netdev_priv(netdev);
	if (xid >= IXGBE_FCOE_DDP_MAX) {
		e_warn(drv, "xid=0x%x out-of-range\n", xid);
		return 0;
	}

	fcoe = &adapter->fcoe;
	if (!fcoe->pool) {
		e_warn(drv, "xid=0x%x no ddp pool for fcoe\n", xid);
		return 0;
	}

	ddp = &fcoe->ddp[xid];
	if (ddp->sgl) {
		e_err(drv, "xid 0x%x w/ non-null sgl=%p nents=%d\n",
		      xid, ddp->sgl, ddp->sgc);
		return 0;
	}
	ixgbe_fcoe_clear_ddp(ddp);

	/* setup dma from scsi command sgl */
	dmacount = pci_map_sg(adapter->pdev, sgl, sgc, DMA_FROM_DEVICE);
	if (dmacount == 0) {
		e_err(drv, "xid 0x%x DMA map error\n", xid);
		return 0;
	}

	/* alloc the udl from our ddp pool */
	ddp->udl = pci_pool_alloc(fcoe->pool, GFP_ATOMIC, &ddp->udp);
	if (!ddp->udl) {
		e_err(drv, "failed allocated ddp context\n");
		goto out_noddp_unmap;
	}
	ddp->sgl = sgl;
	ddp->sgc = sgc;

	j = 0;
	for_each_sg(sgl, sg, dmacount, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
		while (len) {
			/* max number of buffers allowed in one DDP context */
			if (j >= IXGBE_BUFFCNT_MAX) {
				e_err(drv, "xid=%x:%d,%d,%d:addr=%llx "
				      "not enough descriptors\n",
				      xid, i, j, dmacount, (u64)addr);
				goto out_noddp_free;
			}

			/* get the offset of length of current buffer */
			thisoff = addr & ((dma_addr_t)bufflen - 1);
			thislen = min((bufflen - thisoff), len);
			/*
			 * all but the 1st buffer (j == 0)
			 * must be aligned on bufflen
			 */
			if ((j != 0) && (thisoff))
				goto out_noddp_free;
			/*
			 * all but the last buffer
			 * ((i == (dmacount - 1)) && (thislen == len))
			 * must end at bufflen
			 */
			if (((i != (dmacount - 1)) || (thislen != len))
			    && ((thislen + thisoff) != bufflen))
				goto out_noddp_free;

			ddp->udl[j] = (u64)(addr - thisoff);
			/* only the first buffer may have none-zero offset */
			if (j == 0)
				firstoff = thisoff;
			len -= thislen;
			addr += thislen;
			j++;
		}
	}
	/* only the last buffer may have non-full bufflen */
	lastsize = thisoff + thislen;

	fcbuff = (IXGBE_FCBUFF_4KB << IXGBE_FCBUFF_BUFFSIZE_SHIFT);
	fcbuff |= ((j & 0xff) << IXGBE_FCBUFF_BUFFCNT_SHIFT);
	fcbuff |= (firstoff << IXGBE_FCBUFF_OFFSET_SHIFT);
	fcbuff |= (IXGBE_FCBUFF_VALID);

	fcdmarw = xid;
	fcdmarw |= IXGBE_FCDMARW_WE;
	fcdmarw |= (lastsize << IXGBE_FCDMARW_LASTSIZE_SHIFT);

	fcfltrw = xid;
	fcfltrw |= IXGBE_FCFLTRW_WE;

	/* program DMA context */
	hw = &adapter->hw;
	spin_lock_bh(&fcoe->lock);
	IXGBE_WRITE_REG(hw, IXGBE_FCPTRL, ddp->udp & DMA_BIT_MASK(32));
	IXGBE_WRITE_REG(hw, IXGBE_FCPTRH, (u64)ddp->udp >> 32);
	IXGBE_WRITE_REG(hw, IXGBE_FCBUFF, fcbuff);
	IXGBE_WRITE_REG(hw, IXGBE_FCDMARW, fcdmarw);
	/* program filter context */
	IXGBE_WRITE_REG(hw, IXGBE_FCPARAM, 0);
	IXGBE_WRITE_REG(hw, IXGBE_FCFLT, IXGBE_FCFLT_VALID);
	IXGBE_WRITE_REG(hw, IXGBE_FCFLTRW, fcfltrw);
	spin_unlock_bh(&fcoe->lock);

	return 1;

out_noddp_free:
	pci_pool_free(fcoe->pool, ddp->udl, ddp->udp);
	ixgbe_fcoe_clear_ddp(ddp);

out_noddp_unmap:
	pci_unmap_sg(adapter->pdev, sgl, sgc, DMA_FROM_DEVICE);
	return 0;
}

/**
 * ixgbe_fcoe_ddp - check ddp status and mark it done
 * @adapter: ixgbe adapter
 * @rx_desc: advanced rx descriptor
 * @skb: the skb holding the received data
 *
 * This checks ddp status.
 *
 * Returns : < 0 indicates an error or not a FCiE ddp, 0 indicates
 * not passing the skb to ULD, > 0 indicates is the length of data
 * being ddped.
 */
int ixgbe_fcoe_ddp(struct ixgbe_adapter *adapter,
		   union ixgbe_adv_rx_desc *rx_desc,
		   struct sk_buff *skb)
{
	u16 xid;
	u32 fctl;
	u32 sterr, fceofe, fcerr, fcstat;
	int rc = -EINVAL;
	struct ixgbe_fcoe *fcoe;
	struct ixgbe_fcoe_ddp *ddp;
	struct fc_frame_header *fh;

	if (!ixgbe_rx_is_fcoe(rx_desc))
		goto ddp_out;

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	sterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	fcerr = (sterr & IXGBE_RXDADV_ERR_FCERR);
	fceofe = (sterr & IXGBE_RXDADV_ERR_FCEOFE);
	if (fcerr == IXGBE_FCERR_BADCRC)
		skb->ip_summed = CHECKSUM_NONE;

	if (eth_hdr(skb)->h_proto == htons(ETH_P_8021Q))
		fh = (struct fc_frame_header *)(skb->data +
			sizeof(struct vlan_hdr) + sizeof(struct fcoe_hdr));
	else
		fh = (struct fc_frame_header *)(skb->data +
			sizeof(struct fcoe_hdr));
	fctl = ntoh24(fh->fh_f_ctl);
	if (fctl & FC_FC_EX_CTX)
		xid =  be16_to_cpu(fh->fh_ox_id);
	else
		xid =  be16_to_cpu(fh->fh_rx_id);

	if (xid >= IXGBE_FCOE_DDP_MAX)
		goto ddp_out;

	fcoe = &adapter->fcoe;
	ddp = &fcoe->ddp[xid];
	if (!ddp->udl)
		goto ddp_out;

	ddp->err = (fcerr | fceofe);
	if (ddp->err)
		goto ddp_out;

	fcstat = (sterr & IXGBE_RXDADV_STAT_FCSTAT);
	if (fcstat) {
		/* update length of DDPed data */
		ddp->len = le32_to_cpu(rx_desc->wb.lower.hi_dword.rss);
		/* unmap the sg list when FCP_RSP is received */
		if (fcstat == IXGBE_RXDADV_STAT_FCSTAT_FCPRSP) {
			pci_unmap_sg(adapter->pdev, ddp->sgl,
				     ddp->sgc, DMA_FROM_DEVICE);
			ddp->sgl = NULL;
			ddp->sgc = 0;
		}
		/* return 0 to bypass going to ULD for DDPed data */
		if (fcstat == IXGBE_RXDADV_STAT_FCSTAT_DDP)
			rc = 0;
		else if (ddp->len)
			rc = ddp->len;
	}

ddp_out:
	return rc;
}

/**
 * ixgbe_fso - ixgbe FCoE Sequence Offload (FSO)
 * @adapter: ixgbe adapter
 * @tx_ring: tx desc ring
 * @skb: associated skb
 * @tx_flags: tx flags
 * @hdr_len: hdr_len to be returned
 *
 * This sets up large send offload for FCoE
 *
 * Returns : 0 indicates no FSO, > 0 for FSO, < 0 for error
 */
int ixgbe_fso(struct ixgbe_adapter *adapter,
              struct ixgbe_ring *tx_ring, struct sk_buff *skb,
              u32 tx_flags, u8 *hdr_len)
{
	u8 sof, eof;
	u32 vlan_macip_lens;
	u32 fcoe_sof_eof;
	u32 type_tucmd;
	u32 mss_l4len_idx;
	int mss = 0;
	unsigned int i;
	struct ixgbe_tx_buffer *tx_buffer_info;
	struct ixgbe_adv_tx_context_desc *context_desc;
	struct fc_frame_header *fh;

	if (skb_is_gso(skb) && (skb_shinfo(skb)->gso_type != SKB_GSO_FCOE)) {
		e_err(drv, "Wrong gso type %d:expecting SKB_GSO_FCOE\n",
		      skb_shinfo(skb)->gso_type);
		return -EINVAL;
	}

	/* resets the header to point fcoe/fc */
	skb_set_network_header(skb, skb->mac_len);
	skb_set_transport_header(skb, skb->mac_len +
				 sizeof(struct fcoe_hdr));

	/* sets up SOF and ORIS */
	fcoe_sof_eof = 0;
	sof = ((struct fcoe_hdr *)skb_network_header(skb))->fcoe_sof;
	switch (sof) {
	case FC_SOF_I2:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_ORIS;
		break;
	case FC_SOF_I3:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_SOF;
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_ORIS;
		break;
	case FC_SOF_N2:
		break;
	case FC_SOF_N3:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_SOF;
		break;
	default:
		e_warn(drv, "unknown sof = 0x%x\n", sof);
		return -EINVAL;
	}

	/* the first byte of the last dword is EOF */
	skb_copy_bits(skb, skb->len - 4, &eof, 1);
	/* sets up EOF and ORIE */
	switch (eof) {
	case FC_EOF_N:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_N;
		break;
	case FC_EOF_T:
		/* lso needs ORIE */
		if (skb_is_gso(skb)) {
			fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_N;
			fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_ORIE;
		} else {
			fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_T;
		}
		break;
	case FC_EOF_NI:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_NI;
		break;
	case FC_EOF_A:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_A;
		break;
	default:
		e_warn(drv, "unknown eof = 0x%x\n", eof);
		return -EINVAL;
	}

	/* sets up PARINC indicating data offset */
	fh = (struct fc_frame_header *)skb_transport_header(skb);
	if (fh->fh_f_ctl[2] & FC_FC_REL_OFF)
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_PARINC;

	/* hdr_len includes fc_hdr if FCoE lso is enabled */
	*hdr_len = sizeof(struct fcoe_crc_eof);
	if (skb_is_gso(skb))
		*hdr_len += (skb_transport_offset(skb) +
			     sizeof(struct fc_frame_header));
	/* vlan_macip_lens: HEADLEN, MACLEN, VLAN tag */
	vlan_macip_lens = (skb_transport_offset(skb) +
			  sizeof(struct fc_frame_header));
	vlan_macip_lens |= ((skb_transport_offset(skb) - 4)
			   << IXGBE_ADVTXD_MACLEN_SHIFT);
	vlan_macip_lens |= (tx_flags & IXGBE_TX_FLAGS_VLAN_MASK);

	/* type_tycmd and mss: set TUCMD.FCoE to enable offload */
	type_tucmd = IXGBE_TXD_CMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT |
		     IXGBE_ADVTXT_TUCMD_FCOE;
	if (skb_is_gso(skb))
		mss = skb_shinfo(skb)->gso_size;
	/* mss_l4len_id: use 1 for FSO as TSO, no need for L4LEN */
	mss_l4len_idx = (mss << IXGBE_ADVTXD_MSS_SHIFT) |
			(1 << IXGBE_ADVTXD_IDX_SHIFT);

	/* write context desc */
	i = tx_ring->next_to_use;
	context_desc = IXGBE_TX_CTXTDESC_ADV(*tx_ring, i);
	context_desc->vlan_macip_lens	= cpu_to_le32(vlan_macip_lens);
	context_desc->seqnum_seed	= cpu_to_le32(fcoe_sof_eof);
	context_desc->type_tucmd_mlhl	= cpu_to_le32(type_tucmd);
	context_desc->mss_l4len_idx	= cpu_to_le32(mss_l4len_idx);

	tx_buffer_info = &tx_ring->tx_buffer_info[i];
	tx_buffer_info->time_stamp = jiffies;
	tx_buffer_info->next_to_watch = i;

	i++;
	if (i == tx_ring->count)
		i = 0;
	tx_ring->next_to_use = i;

	return skb_is_gso(skb);
}

/**
 * ixgbe_configure_fcoe - configures registers for fcoe at start
 * @adapter: ptr to ixgbe adapter
 *
 * This sets up FCoE related registers
 *
 * Returns : none
 */
void ixgbe_configure_fcoe(struct ixgbe_adapter *adapter)
{
	int i, fcoe_q, fcoe_i;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;
	struct ixgbe_ring_feature *f = &adapter->ring_feature[RING_F_FCOE];
#ifdef CONFIG_IXGBE_DCB
	u8 tc;
	u32 up2tc;
#endif

	/* create the pool for ddp if not created yet */
	if (!fcoe->pool) {
		/* allocate ddp pool */
		fcoe->pool = pci_pool_create("ixgbe_fcoe_ddp",
					     adapter->pdev, IXGBE_FCPTR_MAX,
					     IXGBE_FCPTR_ALIGN, PAGE_SIZE);
		if (!fcoe->pool)
			e_err(drv, "failed to allocated FCoE DDP pool\n");

		spin_lock_init(&fcoe->lock);
	}

	/* Enable L2 eth type filter for FCoE */
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE),
			(ETH_P_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN));
	/* Enable L2 eth type filter for FIP */
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FIP),
			(ETH_P_FIP | IXGBE_ETQF_FILTER_EN));
	if (adapter->ring_feature[RING_F_FCOE].indices) {
		/* Use multiple rx queues for FCoE by redirection table */
		for (i = 0; i < IXGBE_FCRETA_SIZE; i++) {
			fcoe_i = f->mask + i % f->indices;
			fcoe_i &= IXGBE_FCRETA_ENTRY_MASK;
			fcoe_q = adapter->rx_ring[fcoe_i]->reg_idx;
			IXGBE_WRITE_REG(hw, IXGBE_FCRETA(i), fcoe_q);
		}
		IXGBE_WRITE_REG(hw, IXGBE_FCRECTL, IXGBE_FCRECTL_ENA);
		IXGBE_WRITE_REG(hw, IXGBE_ETQS(IXGBE_ETQF_FILTER_FCOE), 0);
	} else  {
		/* Use single rx queue for FCoE */
		fcoe_i = f->mask;
		fcoe_q = adapter->rx_ring[fcoe_i]->reg_idx;
		IXGBE_WRITE_REG(hw, IXGBE_FCRECTL, 0);
		IXGBE_WRITE_REG(hw, IXGBE_ETQS(IXGBE_ETQF_FILTER_FCOE),
				IXGBE_ETQS_QUEUE_EN |
				(fcoe_q << IXGBE_ETQS_RX_QUEUE_SHIFT));
	}
	/* send FIP frames to the first FCoE queue */
	fcoe_i = f->mask;
	fcoe_q = adapter->rx_ring[fcoe_i]->reg_idx;
	IXGBE_WRITE_REG(hw, IXGBE_ETQS(IXGBE_ETQF_FILTER_FIP),
			IXGBE_ETQS_QUEUE_EN |
			(fcoe_q << IXGBE_ETQS_RX_QUEUE_SHIFT));

	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL,
			IXGBE_FCRXCTRL_FCOELLI |
			IXGBE_FCRXCTRL_FCCRCBO |
			(FC_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
#ifdef CONFIG_IXGBE_DCB
	up2tc = IXGBE_READ_REG(&adapter->hw, IXGBE_RTTUP2TC);
	for (i = 0; i < MAX_USER_PRIORITY; i++) {
		tc = (u8)(up2tc >> (i * IXGBE_RTTUP2TC_UP_SHIFT));
		tc &= (MAX_TRAFFIC_CLASS - 1);
		if (fcoe->tc == tc) {
			fcoe->up = i;
			break;
		}
	}
#endif
}

/**
 * ixgbe_cleanup_fcoe - release all fcoe ddp context resources
 * @adapter : ixgbe adapter
 *
 * Cleans up outstanding ddp context resources
 *
 * Returns : none
 */
void ixgbe_cleanup_fcoe(struct ixgbe_adapter *adapter)
{
	int i;
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;

	/* release ddp resource */
	if (fcoe->pool) {
		for (i = 0; i < IXGBE_FCOE_DDP_MAX; i++)
			ixgbe_fcoe_ddp_put(adapter->netdev, i);
		pci_pool_destroy(fcoe->pool);
		fcoe->pool = NULL;
	}
}

/**
 * ixgbe_fcoe_enable - turn on FCoE offload feature
 * @netdev: the corresponding netdev
 *
 * Turns on FCoE offload feature in 82599.
 *
 * Returns : 0 indicates success or -EINVAL on failure
 */
int ixgbe_fcoe_enable(struct net_device *netdev)
{
	int rc = -EINVAL;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);


	if (!(adapter->flags & IXGBE_FLAG_FCOE_CAPABLE))
		goto out_enable;

	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED)
		goto out_enable;

	e_info(drv, "Enabling FCoE offload features.\n");
	if (netif_running(netdev))
		netdev->netdev_ops->ndo_stop(netdev);

	ixgbe_clear_interrupt_scheme(adapter);

	adapter->flags |= IXGBE_FLAG_FCOE_ENABLED;
	adapter->ring_feature[RING_F_FCOE].indices = IXGBE_FCRETA_SIZE;
	netdev->features |= NETIF_F_FCOE_CRC;
	netdev->features |= NETIF_F_FSO;
	netdev->features |= NETIF_F_FCOE_MTU;
	netdev->fcoe_ddp_xid = IXGBE_FCOE_DDP_MAX - 1;

	ixgbe_init_interrupt_scheme(adapter);
	netdev_features_change(netdev);

	if (netif_running(netdev))
		netdev->netdev_ops->ndo_open(netdev);
	rc = 0;

out_enable:
	return rc;
}

/**
 * ixgbe_fcoe_disable - turn off FCoE offload feature
 * @netdev: the corresponding netdev
 *
 * Turns off FCoE offload feature in 82599.
 *
 * Returns : 0 indicates success or -EINVAL on failure
 */
int ixgbe_fcoe_disable(struct net_device *netdev)
{
	int rc = -EINVAL;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (!(adapter->flags & IXGBE_FLAG_FCOE_CAPABLE))
		goto out_disable;

	if (!(adapter->flags & IXGBE_FLAG_FCOE_ENABLED))
		goto out_disable;

	e_info(drv, "Disabling FCoE offload features.\n");
	netdev->features &= ~NETIF_F_FCOE_CRC;
	netdev->features &= ~NETIF_F_FSO;
	netdev->features &= ~NETIF_F_FCOE_MTU;
	netdev->fcoe_ddp_xid = 0;
	netdev_features_change(netdev);

	if (netif_running(netdev))
		netdev->netdev_ops->ndo_stop(netdev);

	ixgbe_clear_interrupt_scheme(adapter);
	adapter->flags &= ~IXGBE_FLAG_FCOE_ENABLED;
	adapter->ring_feature[RING_F_FCOE].indices = 0;
	ixgbe_cleanup_fcoe(adapter);
	ixgbe_init_interrupt_scheme(adapter);

	if (netif_running(netdev))
		netdev->netdev_ops->ndo_open(netdev);
	rc = 0;

out_disable:
	return rc;
}

#ifdef CONFIG_IXGBE_DCB
/**
 * ixgbe_fcoe_getapp - retrieves current user priority bitmap for FCoE
 * @adapter : ixgbe adapter
 *
 * Finds out the corresponding user priority bitmap from the current
 * traffic class that FCoE belongs to. Returns 0 as the invalid user
 * priority bitmap to indicate an error.
 *
 * Returns : 802.1p user priority bitmap for FCoE
 */
u8 ixgbe_fcoe_getapp(struct ixgbe_adapter *adapter)
{
	return 1 << adapter->fcoe.up;
}

/**
 * ixgbe_fcoe_setapp - sets the user priority bitmap for FCoE
 * @adapter : ixgbe adapter
 * @up : 802.1p user priority bitmap
 *
 * Finds out the traffic class from the input user priority
 * bitmap for FCoE.
 *
 * Returns : 0 on success otherwise returns 1 on error
 */
u8 ixgbe_fcoe_setapp(struct ixgbe_adapter *adapter, u8 up)
{
	int i;
	u32 up2tc;

	/* valid user priority bitmap must not be 0 */
	if (up) {
		/* from user priority to the corresponding traffic class */
		up2tc = IXGBE_READ_REG(&adapter->hw, IXGBE_RTTUP2TC);
		for (i = 0; i < MAX_USER_PRIORITY; i++) {
			if (up & (1 << i)) {
				up2tc >>= (i * IXGBE_RTTUP2TC_UP_SHIFT);
				up2tc &= (MAX_TRAFFIC_CLASS - 1);
				adapter->fcoe.tc = (u8)up2tc;
				adapter->fcoe.up = i;
				return 0;
			}
		}
	}

	return 1;
}
#endif /* CONFIG_IXGBE_DCB */

/**
 * ixgbe_fcoe_get_wwn - get world wide name for the node or the port
 * @netdev : ixgbe adapter
 * @wwn : the world wide name
 * @type: the type of world wide name
 *
 * Returns the node or port world wide name if both the prefix and the san
 * mac address are valid, then the wwn is formed based on the NAA-2 for
 * IEEE Extended name identifier (ref. to T10 FC-LS Spec., Sec. 15.3).
 *
 * Returns : 0 on success
 */
int ixgbe_fcoe_get_wwn(struct net_device *netdev, u64 *wwn, int type)
{
	int rc = -EINVAL;
	u16 prefix = 0xffff;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_mac_info *mac = &adapter->hw.mac;

	switch (type) {
	case NETDEV_FCOE_WWNN:
		prefix = mac->wwnn_prefix;
		break;
	case NETDEV_FCOE_WWPN:
		prefix = mac->wwpn_prefix;
		break;
	default:
		break;
	}

	if ((prefix != 0xffff) &&
	    is_valid_ether_addr(mac->san_addr)) {
		*wwn = ((u64) prefix << 48) |
		       ((u64) mac->san_addr[0] << 40) |
		       ((u64) mac->san_addr[1] << 32) |
		       ((u64) mac->san_addr[2] << 24) |
		       ((u64) mac->san_addr[3] << 16) |
		       ((u64) mac->san_addr[4] << 8)  |
		       ((u64) mac->san_addr[5]);
		rc = 0;
	}
	return rc;
}


