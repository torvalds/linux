/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2011 Intel Corporation.

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
 * ixgbe_fcoe_clear_ddp - clear the given ddp context
 * @ddp - ptr to the ixgbe_fcoe_ddp
 *
 * Returns : none
 *
 */
static inline void ixgbe_fcoe_clear_ddp(struct ixgbe_fcoe_ddp *ddp)
{
	ddp->len = 0;
	ddp->err = 1;
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
	u32 fcbuff;

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

		/* guaranteed to be invalidated after 100us */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCDMARW,
				(xid | IXGBE_FCDMARW_RE));
		fcbuff = IXGBE_READ_REG(&adapter->hw, IXGBE_FCBUFF);
		spin_unlock_bh(&fcoe->lock);
		if (fcbuff & IXGBE_FCBUFF_VALID)
			udelay(100);
	}
	if (ddp->sgl)
		pci_unmap_sg(adapter->pdev, ddp->sgl, ddp->sgc,
			     DMA_FROM_DEVICE);
	if (ddp->pool) {
		pci_pool_free(ddp->pool, ddp->udl, ddp->udp);
		ddp->pool = NULL;
	}

	ixgbe_fcoe_clear_ddp(ddp);

out_ddp_put:
	return len;
}

/**
 * ixgbe_fcoe_ddp_setup - called to set up ddp context
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 *
 * Returns : 1 for success and 0 for no ddp
 */
static int ixgbe_fcoe_ddp_setup(struct net_device *netdev, u16 xid,
				struct scatterlist *sgl, unsigned int sgc,
				int target_mode)
{
	struct ixgbe_adapter *adapter;
	struct ixgbe_hw *hw;
	struct ixgbe_fcoe *fcoe;
	struct ixgbe_fcoe_ddp *ddp;
	struct scatterlist *sg;
	unsigned int i, j, dmacount;
	unsigned int len;
	static const unsigned int bufflen = IXGBE_FCBUFF_MIN;
	unsigned int firstoff = 0;
	unsigned int lastsize;
	unsigned int thisoff = 0;
	unsigned int thislen = 0;
	u32 fcbuff, fcdmarw, fcfltrw, fcrxctl;
	dma_addr_t addr = 0;
	struct pci_pool *pool;
	unsigned int cpu;

	if (!netdev || !sgl)
		return 0;

	adapter = netdev_priv(netdev);
	if (xid >= IXGBE_FCOE_DDP_MAX) {
		e_warn(drv, "xid=0x%x out-of-range\n", xid);
		return 0;
	}

	/* no DDP if we are already down or resetting */
	if (test_bit(__IXGBE_DOWN, &adapter->state) ||
	    test_bit(__IXGBE_RESETTING, &adapter->state))
		return 0;

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

	/* alloc the udl from per cpu ddp pool */
	cpu = get_cpu();
	pool = *per_cpu_ptr(fcoe->pool, cpu);
	ddp->udl = pci_pool_alloc(pool, GFP_ATOMIC, &ddp->udp);
	if (!ddp->udl) {
		e_err(drv, "failed allocated ddp context\n");
		goto out_noddp_unmap;
	}
	ddp->pool = pool;
	ddp->sgl = sgl;
	ddp->sgc = sgc;

	j = 0;
	for_each_sg(sgl, sg, dmacount, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
		while (len) {
			/* max number of buffers allowed in one DDP context */
			if (j >= IXGBE_BUFFCNT_MAX) {
				*per_cpu_ptr(fcoe->pcpu_noddp, cpu) += 1;
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

	/*
	 * lastsize can not be buffer len.
	 * If it is then adding another buffer with lastsize = 1.
	 */
	if (lastsize == bufflen) {
		if (j >= IXGBE_BUFFCNT_MAX) {
			*per_cpu_ptr(fcoe->pcpu_noddp_ext_buff, cpu) += 1;
			goto out_noddp_free;
		}

		ddp->udl[j] = (u64)(fcoe->extra_ddp_buffer_dma);
		j++;
		lastsize = 1;
	}
	put_cpu();

	fcbuff = (IXGBE_FCBUFF_4KB << IXGBE_FCBUFF_BUFFSIZE_SHIFT);
	fcbuff |= ((j & 0xff) << IXGBE_FCBUFF_BUFFCNT_SHIFT);
	fcbuff |= (firstoff << IXGBE_FCBUFF_OFFSET_SHIFT);
	/* Set WRCONTX bit to allow DDP for target */
	if (target_mode)
		fcbuff |= (IXGBE_FCBUFF_WRCONTX);
	fcbuff |= (IXGBE_FCBUFF_VALID);

	fcdmarw = xid;
	fcdmarw |= IXGBE_FCDMARW_WE;
	fcdmarw |= (lastsize << IXGBE_FCDMARW_LASTSIZE_SHIFT);

	fcfltrw = xid;
	fcfltrw |= IXGBE_FCFLTRW_WE;

	/* program DMA context */
	hw = &adapter->hw;
	spin_lock_bh(&fcoe->lock);

	/* turn on last frame indication for target mode as FCP_RSPtarget is
	 * supposed to send FCP_RSP when it is done. */
	if (target_mode && !test_bit(__IXGBE_FCOE_TARGET, &fcoe->mode)) {
		set_bit(__IXGBE_FCOE_TARGET, &fcoe->mode);
		fcrxctl = IXGBE_READ_REG(hw, IXGBE_FCRXCTRL);
		fcrxctl |= IXGBE_FCRXCTRL_LASTSEQH;
		IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL, fcrxctl);
	}

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
	pci_pool_free(pool, ddp->udl, ddp->udp);
	ixgbe_fcoe_clear_ddp(ddp);

out_noddp_unmap:
	pci_unmap_sg(adapter->pdev, sgl, sgc, DMA_FROM_DEVICE);
	put_cpu();
	return 0;
}

/**
 * ixgbe_fcoe_ddp_get - called to set up ddp context in initiator mode
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
	return ixgbe_fcoe_ddp_setup(netdev, xid, sgl, sgc, 0);
}

/**
 * ixgbe_fcoe_ddp_target - called to set up ddp context in target mode
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_target
 * and is expected to be called from ULD, e.g., FCP layer of libfc
 * to set up ddp for the corresponding xid of the given sglist for
 * the corresponding I/O. The DDP in target mode is a write I/O request
 * from the initiator.
 *
 * Returns : 1 for success and 0 for no ddp
 */
int ixgbe_fcoe_ddp_target(struct net_device *netdev, u16 xid,
			    struct scatterlist *sgl, unsigned int sgc)
{
	return ixgbe_fcoe_ddp_setup(netdev, xid, sgl, sgc, 1);
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
		   struct sk_buff *skb,
		   u32 staterr)
{
	u16 xid;
	u32 fctl;
	u32 fceofe, fcerr, fcstat;
	int rc = -EINVAL;
	struct ixgbe_fcoe *fcoe;
	struct ixgbe_fcoe_ddp *ddp;
	struct fc_frame_header *fh;
	struct fcoe_crc_eof *crc;

	fcerr = (staterr & IXGBE_RXDADV_ERR_FCERR);
	fceofe = (staterr & IXGBE_RXDADV_ERR_FCEOFE);
	if (fcerr == IXGBE_FCERR_BADCRC)
		skb_checksum_none_assert(skb);
	else
		skb->ip_summed = CHECKSUM_UNNECESSARY;

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

	if (fcerr | fceofe)
		goto ddp_out;

	fcstat = (staterr & IXGBE_RXDADV_STAT_FCSTAT);
	if (fcstat) {
		/* update length of DDPed data */
		ddp->len = le32_to_cpu(rx_desc->wb.lower.hi_dword.rss);
		/* unmap the sg list when FCP_RSP is received */
		if (fcstat == IXGBE_RXDADV_STAT_FCSTAT_FCPRSP) {
			pci_unmap_sg(adapter->pdev, ddp->sgl,
				     ddp->sgc, DMA_FROM_DEVICE);
			ddp->err = (fcerr | fceofe);
			ddp->sgl = NULL;
			ddp->sgc = 0;
		}
		/* return 0 to bypass going to ULD for DDPed data */
		if (fcstat == IXGBE_RXDADV_STAT_FCSTAT_DDP)
			rc = 0;
		else if (ddp->len)
			rc = ddp->len;
	}
	/* In target mode, check the last data frame of the sequence.
	 * For DDP in target mode, data is already DDPed but the header
	 * indication of the last data frame ould allow is to tell if we
	 * got all the data and the ULP can send FCP_RSP back, as this is
	 * not a full fcoe frame, we fill the trailer here so it won't be
	 * dropped by the ULP stack.
	 */
	if ((fh->fh_r_ctl == FC_RCTL_DD_SOL_DATA) &&
	    (fctl & FC_FC_END_SEQ)) {
		crc = (struct fcoe_crc_eof *)skb_put(skb, sizeof(*crc));
		crc->fcoe_eof = FC_EOF_T;
	}
ddp_out:
	return rc;
}

/**
 * ixgbe_fso - ixgbe FCoE Sequence Offload (FSO)
 * @tx_ring: tx desc ring
 * @skb: associated skb
 * @tx_flags: tx flags
 * @hdr_len: hdr_len to be returned
 *
 * This sets up large send offload for FCoE
 *
 * Returns : 0 indicates no FSO, > 0 for FSO, < 0 for error
 */
int ixgbe_fso(struct ixgbe_ring *tx_ring, struct sk_buff *skb,
              u32 tx_flags, u8 *hdr_len)
{
	struct fc_frame_header *fh;
	u32 vlan_macip_lens;
	u32 fcoe_sof_eof = 0;
	u32 mss_l4len_idx;
	u8 sof, eof;

	if (skb_is_gso(skb) && (skb_shinfo(skb)->gso_type != SKB_GSO_FCOE)) {
		dev_err(tx_ring->dev, "Wrong gso type %d:expecting SKB_GSO_FCOE\n",
			skb_shinfo(skb)->gso_type);
		return -EINVAL;
	}

	/* resets the header to point fcoe/fc */
	skb_set_network_header(skb, skb->mac_len);
	skb_set_transport_header(skb, skb->mac_len +
				 sizeof(struct fcoe_hdr));

	/* sets up SOF and ORIS */
	sof = ((struct fcoe_hdr *)skb_network_header(skb))->fcoe_sof;
	switch (sof) {
	case FC_SOF_I2:
		fcoe_sof_eof = IXGBE_ADVTXD_FCOEF_ORIS;
		break;
	case FC_SOF_I3:
		fcoe_sof_eof = IXGBE_ADVTXD_FCOEF_SOF |
			       IXGBE_ADVTXD_FCOEF_ORIS;
		break;
	case FC_SOF_N2:
		break;
	case FC_SOF_N3:
		fcoe_sof_eof = IXGBE_ADVTXD_FCOEF_SOF;
		break;
	default:
		dev_warn(tx_ring->dev, "unknown sof = 0x%x\n", sof);
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
		if (skb_is_gso(skb))
			fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_N |
					IXGBE_ADVTXD_FCOEF_ORIE;
		else
			fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_T;
		break;
	case FC_EOF_NI:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_NI;
		break;
	case FC_EOF_A:
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_EOF_A;
		break;
	default:
		dev_warn(tx_ring->dev, "unknown eof = 0x%x\n", eof);
		return -EINVAL;
	}

	/* sets up PARINC indicating data offset */
	fh = (struct fc_frame_header *)skb_transport_header(skb);
	if (fh->fh_f_ctl[2] & FC_FC_REL_OFF)
		fcoe_sof_eof |= IXGBE_ADVTXD_FCOEF_PARINC;

	/* include trailer in headlen as it is replicated per frame */
	*hdr_len = sizeof(struct fcoe_crc_eof);

	/* hdr_len includes fc_hdr if FCoE LSO is enabled */
	if (skb_is_gso(skb))
		*hdr_len += (skb_transport_offset(skb) +
			     sizeof(struct fc_frame_header));

	/* mss_l4len_id: use 1 for FSO as TSO, no need for L4LEN */
	mss_l4len_idx = skb_shinfo(skb)->gso_size << IXGBE_ADVTXD_MSS_SHIFT;
	mss_l4len_idx |= 1 << IXGBE_ADVTXD_IDX_SHIFT;

	/* vlan_macip_lens: HEADLEN, MACLEN, VLAN tag */
	vlan_macip_lens = skb_transport_offset(skb) +
			  sizeof(struct fc_frame_header);
	vlan_macip_lens |= (skb_transport_offset(skb) - 4)
			   << IXGBE_ADVTXD_MACLEN_SHIFT;
	vlan_macip_lens |= tx_flags & IXGBE_TX_FLAGS_VLAN_MASK;

	/* write context desc */
	ixgbe_tx_ctxtdesc(tx_ring, vlan_macip_lens, fcoe_sof_eof,
			  IXGBE_ADVTXT_TUCMD_FCOE, mss_l4len_idx);

	return skb_is_gso(skb);
}

static void ixgbe_fcoe_ddp_pools_free(struct ixgbe_fcoe *fcoe)
{
	unsigned int cpu;
	struct pci_pool **pool;

	for_each_possible_cpu(cpu) {
		pool = per_cpu_ptr(fcoe->pool, cpu);
		if (*pool)
			pci_pool_destroy(*pool);
	}
	free_percpu(fcoe->pool);
	fcoe->pool = NULL;
}

static void ixgbe_fcoe_ddp_pools_alloc(struct ixgbe_adapter *adapter)
{
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;
	unsigned int cpu;
	struct pci_pool **pool;
	char pool_name[32];

	fcoe->pool = alloc_percpu(struct pci_pool *);
	if (!fcoe->pool)
		return;

	/* allocate pci pool for each cpu */
	for_each_possible_cpu(cpu) {
		snprintf(pool_name, 32, "ixgbe_fcoe_ddp_%d", cpu);
		pool = per_cpu_ptr(fcoe->pool, cpu);
		*pool = pci_pool_create(pool_name,
					adapter->pdev, IXGBE_FCPTR_MAX,
					IXGBE_FCPTR_ALIGN, PAGE_SIZE);
		if (!*pool) {
			e_err(drv, "failed to alloc DDP pool on cpu:%d\n", cpu);
			ixgbe_fcoe_ddp_pools_free(fcoe);
			return;
		}
	}
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
	unsigned int cpu;

	if (!fcoe->pool) {
		spin_lock_init(&fcoe->lock);

		ixgbe_fcoe_ddp_pools_alloc(adapter);
		if (!fcoe->pool) {
			e_err(drv, "failed to alloc percpu fcoe DDP pools\n");
			return;
		}

		/* Extra buffer to be shared by all DDPs for HW work around */
		fcoe->extra_ddp_buffer = kmalloc(IXGBE_FCBUFF_MIN, GFP_ATOMIC);
		if (fcoe->extra_ddp_buffer == NULL) {
			e_err(drv, "failed to allocated extra DDP buffer\n");
			goto out_ddp_pools;
		}

		fcoe->extra_ddp_buffer_dma =
			dma_map_single(&adapter->pdev->dev,
				       fcoe->extra_ddp_buffer,
				       IXGBE_FCBUFF_MIN,
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(&adapter->pdev->dev,
				      fcoe->extra_ddp_buffer_dma)) {
			e_err(drv, "failed to map extra DDP buffer\n");
			goto out_extra_ddp_buffer;
		}

		/* Alloc per cpu mem to count the ddp alloc failure number */
		fcoe->pcpu_noddp = alloc_percpu(u64);
		if (!fcoe->pcpu_noddp) {
			e_err(drv, "failed to alloc noddp counter\n");
			goto out_pcpu_noddp_alloc_fail;
		}

		fcoe->pcpu_noddp_ext_buff = alloc_percpu(u64);
		if (!fcoe->pcpu_noddp_ext_buff) {
			e_err(drv, "failed to alloc noddp extra buff cnt\n");
			goto out_pcpu_noddp_extra_buff_alloc_fail;
		}

		for_each_possible_cpu(cpu) {
			*per_cpu_ptr(fcoe->pcpu_noddp, cpu) = 0;
			*per_cpu_ptr(fcoe->pcpu_noddp_ext_buff, cpu) = 0;
		}
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

	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL, IXGBE_FCRXCTRL_FCCRCBO |
			(FC_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
	return;
out_pcpu_noddp_extra_buff_alloc_fail:
	free_percpu(fcoe->pcpu_noddp);
out_pcpu_noddp_alloc_fail:
	dma_unmap_single(&adapter->pdev->dev,
			 fcoe->extra_ddp_buffer_dma,
			 IXGBE_FCBUFF_MIN,
			 DMA_FROM_DEVICE);
out_extra_ddp_buffer:
	kfree(fcoe->extra_ddp_buffer);
out_ddp_pools:
	ixgbe_fcoe_ddp_pools_free(fcoe);
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

	if (!fcoe->pool)
		return;

	for (i = 0; i < IXGBE_FCOE_DDP_MAX; i++)
		ixgbe_fcoe_ddp_put(adapter->netdev, i);
	dma_unmap_single(&adapter->pdev->dev,
			 fcoe->extra_ddp_buffer_dma,
			 IXGBE_FCBUFF_MIN,
			 DMA_FROM_DEVICE);
	free_percpu(fcoe->pcpu_noddp);
	free_percpu(fcoe->pcpu_noddp_ext_buff);
	kfree(fcoe->extra_ddp_buffer);
	ixgbe_fcoe_ddp_pools_free(fcoe);
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
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;


	if (!(adapter->flags & IXGBE_FLAG_FCOE_CAPABLE))
		goto out_enable;

	atomic_inc(&fcoe->refcnt);
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
	struct ixgbe_fcoe *fcoe = &adapter->fcoe;

	if (!(adapter->flags & IXGBE_FLAG_FCOE_CAPABLE))
		goto out_disable;

	if (!(adapter->flags & IXGBE_FLAG_FCOE_ENABLED))
		goto out_disable;

	if (!atomic_dec_and_test(&fcoe->refcnt))
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

/**
 * ixgbe_fcoe_get_hbainfo - get FCoE HBA information
 * @netdev : ixgbe adapter
 * @info : HBA information
 *
 * Returns ixgbe HBA information
 *
 * Returns : 0 on success
 */
int ixgbe_fcoe_get_hbainfo(struct net_device *netdev,
			   struct netdev_fcoe_hbainfo *info)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	int i, pos;
	u8 buf[8];

	if (!info)
		return -EINVAL;

	/* Don't return information on unsupported devices */
	if (hw->mac.type != ixgbe_mac_82599EB &&
	    hw->mac.type != ixgbe_mac_X540)
		return -EINVAL;

	/* Manufacturer */
	snprintf(info->manufacturer, sizeof(info->manufacturer),
		 "Intel Corporation");

	/* Serial Number */

	/* Get the PCI-e Device Serial Number Capability */
	pos = pci_find_ext_capability(adapter->pdev, PCI_EXT_CAP_ID_DSN);
	if (pos) {
		pos += 4;
		for (i = 0; i < 8; i++)
			pci_read_config_byte(adapter->pdev, pos + i, &buf[i]);

		snprintf(info->serial_number, sizeof(info->serial_number),
			 "%02X%02X%02X%02X%02X%02X%02X%02X",
			 buf[7], buf[6], buf[5], buf[4],
			 buf[3], buf[2], buf[1], buf[0]);
	} else
		snprintf(info->serial_number, sizeof(info->serial_number),
			 "Unknown");

	/* Hardware Version */
	snprintf(info->hardware_version,
		 sizeof(info->hardware_version),
		 "Rev %d", hw->revision_id);
	/* Driver Name/Version */
	snprintf(info->driver_version,
		 sizeof(info->driver_version),
		 "%s v%s",
		 ixgbe_driver_name,
		 ixgbe_driver_version);
	/* Firmware Version */
	snprintf(info->firmware_version,
		 sizeof(info->firmware_version),
		 "0x%08x",
		 (adapter->eeprom_verh << 16) |
		  adapter->eeprom_verl);

	/* Model */
	if (hw->mac.type == ixgbe_mac_82599EB) {
		snprintf(info->model,
			 sizeof(info->model),
			 "Intel 82599");
	} else {
		snprintf(info->model,
			 sizeof(info->model),
			 "Intel X540");
	}

	/* Model Description */
	snprintf(info->model_description,
		 sizeof(info->model_description),
		 "%s",
		 ixgbe_default_device_descr);

	return 0;
}
