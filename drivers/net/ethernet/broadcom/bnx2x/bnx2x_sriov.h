/* bnx2x_sriov.h: Broadcom Everest network driver.
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
#ifndef BNX2X_SRIOV_H
#define BNX2X_SRIOV_H

struct bnx2x_vf_mbx_msg {
	union vfpf_tlvs req;
	union pfvf_tlvs resp;
};

void bnx2x_add_tlv(struct bnx2x *bp, void *tlvs_list, u16 offset, u16 type,
		   u16 length);
void bnx2x_vfpf_prep(struct bnx2x *bp, struct vfpf_first_tlv *first_tlv,
		     u16 type, u16 length);
void bnx2x_dp_tlv_list(struct bnx2x *bp, void *tlvs_list);
#endif /* bnx2x_sriov.h */
