/* bnx2x_vfpf.c: Broadcom Everest network driver.
 *
 * Copyright 2009-2012 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Shmulik Ravid <shmulikr@broadcom.com>
 *	       Ariel Elior <ariele@broadcom.com>
 */

#include "bnx2x.h"
#include "bnx2x_sriov.h"

/* place a given tlv on the tlv buffer at a given offset */
void bnx2x_add_tlv(struct bnx2x *bp, void *tlvs_list, u16 offset, u16 type,
		   u16 length)
{
	struct channel_tlv *tl =
		(struct channel_tlv *)(tlvs_list + offset);

	tl->type = type;
	tl->length = length;
}

/* Clear the mailbox and init the header of the first tlv */
void bnx2x_vfpf_prep(struct bnx2x *bp, struct vfpf_first_tlv *first_tlv,
		     u16 type, u16 length)
{
	DP(BNX2X_MSG_IOV, "preparing to send %d tlv over vf pf channel\n",
	   type);

	/* Clear mailbox */
	memset(bp->vf2pf_mbox, 0, sizeof(struct bnx2x_vf_mbx_msg));

	/* init type and length */
	bnx2x_add_tlv(bp, &first_tlv->tl, 0, type, length);

	/* init first tlv header */
	first_tlv->resp_msg_offset = sizeof(bp->vf2pf_mbox->req);
}

/* list the types and lengths of the tlvs on the buffer */
void bnx2x_dp_tlv_list(struct bnx2x *bp, void *tlvs_list)
{
	int i = 1;
	struct channel_tlv *tlv = (struct channel_tlv *)tlvs_list;

	while (tlv->type != CHANNEL_TLV_LIST_END) {
		/* output tlv */
		DP(BNX2X_MSG_IOV, "TLV number %d: type %d, length %d\n", i,
		   tlv->type, tlv->length);

		/* advance to next tlv */
		tlvs_list += tlv->length;

		/* cast general tlv list pointer to channel tlv header*/
		tlv = (struct channel_tlv *)tlvs_list;

		i++;

		/* break condition for this loop */
		if (i > MAX_TLVS_IN_LIST) {
			WARN(true, "corrupt tlvs");
			return;
		}
	}

	/* output last tlv */
	DP(BNX2X_MSG_IOV, "TLV number %d: type %d, length %d\n", i,
	   tlv->type, tlv->length);
}

/* test whether we support a tlv type */
bool bnx2x_tlv_supported(u16 tlvtype)
{
	return CHANNEL_TLV_NONE < tlvtype && tlvtype < CHANNEL_TLV_MAX;
}

static inline int bnx2x_pfvf_status_codes(int rc)
{
	switch (rc) {
	case 0:
		return PFVF_STATUS_SUCCESS;
	case -ENOMEM:
		return PFVF_STATUS_NO_RESOURCE;
	default:
		return PFVF_STATUS_FAILURE;
	}
}

/* General service functions */
static void storm_memset_vf_mbx_ack(struct bnx2x *bp, u16 abs_fid)
{
	u32 addr = BAR_CSTRORM_INTMEM +
		   CSTORM_VF_PF_CHANNEL_STATE_OFFSET(abs_fid);

	REG_WR8(bp, addr, VF_PF_CHANNEL_STATE_READY);
}

static void storm_memset_vf_mbx_valid(struct bnx2x *bp, u16 abs_fid)
{
	u32 addr = BAR_CSTRORM_INTMEM +
		   CSTORM_VF_PF_CHANNEL_VALID_OFFSET(abs_fid);

	REG_WR8(bp, addr, 1);
}

static inline void bnx2x_set_vf_mbxs_valid(struct bnx2x *bp)
{
	int i;

	for_each_vf(bp, i)
		storm_memset_vf_mbx_valid(bp, bnx2x_vf(bp, i, abs_vfid));
}

/* enable vf_pf mailbox (aka vf-pf-chanell) */
void bnx2x_vf_enable_mbx(struct bnx2x *bp, u8 abs_vfid)
{
	bnx2x_vf_flr_clnup_epilog(bp, abs_vfid);

	/* enable the mailbox in the FW */
	storm_memset_vf_mbx_ack(bp, abs_vfid);
	storm_memset_vf_mbx_valid(bp, abs_vfid);

	/* enable the VF access to the mailbox */
	bnx2x_vf_enable_access(bp, abs_vfid);
}

/* this works only on !E1h */
static int bnx2x_copy32_vf_dmae(struct bnx2x *bp, u8 from_vf,
				dma_addr_t pf_addr, u8 vfid, u32 vf_addr_hi,
				u32 vf_addr_lo, u32 len32)
{
	struct dmae_command dmae;

	if (CHIP_IS_E1x(bp)) {
		BNX2X_ERR("Chip revision does not support VFs\n");
		return DMAE_NOT_RDY;
	}

	if (!bp->dmae_ready) {
		BNX2X_ERR("DMAE is not ready, can not copy\n");
		return DMAE_NOT_RDY;
	}

	/* set opcode and fixed command fields */
	bnx2x_prep_dmae_with_comp(bp, &dmae, DMAE_SRC_PCI, DMAE_DST_PCI);

	if (from_vf) {
		dmae.opcode_iov = (vfid << DMAE_COMMAND_SRC_VFID_SHIFT) |
			(DMAE_SRC_VF << DMAE_COMMAND_SRC_VFPF_SHIFT) |
			(DMAE_DST_PF << DMAE_COMMAND_DST_VFPF_SHIFT);

		dmae.opcode |= (DMAE_C_DST << DMAE_COMMAND_C_FUNC_SHIFT);

		dmae.src_addr_lo = vf_addr_lo;
		dmae.src_addr_hi = vf_addr_hi;
		dmae.dst_addr_lo = U64_LO(pf_addr);
		dmae.dst_addr_hi = U64_HI(pf_addr);
	} else {
		dmae.opcode_iov = (vfid << DMAE_COMMAND_DST_VFID_SHIFT) |
			(DMAE_DST_VF << DMAE_COMMAND_DST_VFPF_SHIFT) |
			(DMAE_SRC_PF << DMAE_COMMAND_SRC_VFPF_SHIFT);

		dmae.opcode |= (DMAE_C_SRC << DMAE_COMMAND_C_FUNC_SHIFT);

		dmae.src_addr_lo = U64_LO(pf_addr);
		dmae.src_addr_hi = U64_HI(pf_addr);
		dmae.dst_addr_lo = vf_addr_lo;
		dmae.dst_addr_hi = vf_addr_hi;
	}
	dmae.len = len32;
	bnx2x_dp_dmae(bp, &dmae, BNX2X_MSG_DMAE);

	/* issue the command and wait for completion */
	return bnx2x_issue_dmae_with_comp(bp, &dmae);
}

/* dispatch request */
static void bnx2x_vf_mbx_request(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  struct bnx2x_vf_mbx *mbx)
{
	int i;

	/* check if tlv type is known */
	if (bnx2x_tlv_supported(mbx->first_tlv.tl.type)) {
		/* switch on the opcode */
		switch (mbx->first_tlv.tl.type) {
		}
	} else {
		/* unknown TLV - this may belong to a VF driver from the future
		 * - a version written after this PF driver was written, which
		 * supports features unknown as of yet. Too bad since we don't
		 * support them. Or this may be because someone wrote a crappy
		 * VF driver and is sending garbage over the channel.
		 */
		BNX2X_ERR("unknown TLV. type %d length %d. first 20 bytes of mailbox buffer:\n",
			  mbx->first_tlv.tl.type, mbx->first_tlv.tl.length);
		for (i = 0; i < 20; i++)
			DP_CONT(BNX2X_MSG_IOV, "%x ",
				mbx->msg->req.tlv_buf_size.tlv_buffer[i]);
	}
}

/* handle new vf-pf message */
void bnx2x_vf_mbx(struct bnx2x *bp, struct vf_pf_event_data *vfpf_event)
{
	struct bnx2x_virtf *vf;
	struct bnx2x_vf_mbx *mbx;
	u8 vf_idx;
	int rc;

	DP(BNX2X_MSG_IOV,
	   "vf pf event received: vfid %d, address_hi %x, address lo %x",
	   vfpf_event->vf_id, vfpf_event->msg_addr_hi, vfpf_event->msg_addr_lo);
	/* Sanity checks consider removing later */

	/* check if the vf_id is valid */
	if (vfpf_event->vf_id - BP_VFDB(bp)->sriov.first_vf_in_pf >
	    BNX2X_NR_VIRTFN(bp)) {
		BNX2X_ERR("Illegal vf_id %d max allowed: %d\n",
			  vfpf_event->vf_id, BNX2X_NR_VIRTFN(bp));
		goto mbx_done;
	}
	vf_idx = bnx2x_vf_idx_by_abs_fid(bp, vfpf_event->vf_id);
	mbx = BP_VF_MBX(bp, vf_idx);

	/* verify an event is not currently being processed -
	 * debug failsafe only
	 */
	if (mbx->flags & VF_MSG_INPROCESS) {
		BNX2X_ERR("Previous message is still being processed, vf_id %d\n",
			  vfpf_event->vf_id);
		goto mbx_done;
	}
	vf = BP_VF(bp, vf_idx);

	/* save the VF message address */
	mbx->vf_addr_hi = vfpf_event->msg_addr_hi;
	mbx->vf_addr_lo = vfpf_event->msg_addr_lo;
	DP(BNX2X_MSG_IOV, "mailbox vf address hi 0x%x, lo 0x%x, offset 0x%x\n",
	   mbx->vf_addr_hi, mbx->vf_addr_lo, mbx->first_tlv.resp_msg_offset);

	/* dmae to get the VF request */
	rc = bnx2x_copy32_vf_dmae(bp, true, mbx->msg_mapping, vf->abs_vfid,
				  mbx->vf_addr_hi, mbx->vf_addr_lo,
				  sizeof(union vfpf_tlvs)/4);
	if (rc) {
		BNX2X_ERR("Failed to copy request VF %d\n", vf->abs_vfid);
		goto mbx_error;
	}

	/* process the VF message header */
	mbx->first_tlv = mbx->msg->req.first_tlv;

	/* dispatch the request (will prepare the response) */
	bnx2x_vf_mbx_request(bp, vf, mbx);
	goto mbx_done;

mbx_error:
mbx_done:
	return;
}
