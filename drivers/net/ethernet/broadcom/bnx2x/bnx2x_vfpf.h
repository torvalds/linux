/* bnx2x_vfpf.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2011-2012 Broadcom Corporation
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
 * Written by: Ariel Elior <ariele@broadcom.com>
 */
#ifndef VF_PF_IF_H
#define VF_PF_IF_H

/* HW VF-PF channel definitions
 * A.K.A VF-PF mailbox
 */
#define TLV_BUFFER_SIZE			1024

struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

union vfpf_tlvs {
	struct tlv_buffer_size		tlv_buf_size;
};

union pfvf_tlvs {
	struct tlv_buffer_size		tlv_buf_size;
};
#endif /* VF_PF_IF_H */
