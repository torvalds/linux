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


#include "ixgbe.h"
#include <linux/if_ether.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>
#include <scsi/libfcoe.h>

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
		DPRINTK(DRV, ERR, "Wrong gso type %d:expecting SKB_GSO_FCOE\n",
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
		DPRINTK(DRV, WARNING, "unknown sof = 0x%x\n", sof);
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
		DPRINTK(DRV, WARNING, "unknown eof = 0x%x\n", eof);
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
	struct ixgbe_hw *hw = &adapter->hw;

	/* L2 filter for FCoE: default to queue 0 */
	IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_FCOE),
			(ETH_P_FCOE | IXGBE_ETQF_FCOE | IXGBE_ETQF_FILTER_EN));
	IXGBE_WRITE_REG(hw, IXGBE_FCRECTL, 0);
	IXGBE_WRITE_REG(hw, IXGBE_ETQS(IXGBE_ETQF_FILTER_FCOE),
			IXGBE_ETQS_QUEUE_EN);
	IXGBE_WRITE_REG(hw, IXGBE_FCRXCTRL,
			IXGBE_FCRXCTRL_FCOELLI |
			IXGBE_FCRXCTRL_FCCRCBO |
			(FC_FCOE_VER << IXGBE_FCRXCTRL_FCOEVER_SHIFT));
}
