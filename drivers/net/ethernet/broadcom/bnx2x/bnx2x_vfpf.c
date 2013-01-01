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
