/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */
#ifndef _WX_MBX_H_
#define _WX_MBX_H_

#define WX_VXMAILBOX_SIZE    15

/* PF Registers */
#define WX_PXMAILBOX(i)      (0x600 + (4 * (i))) /* i=[0,63] */
#define WX_PXMAILBOX_STS     BIT(0) /* Initiate message send to VF */
#define WX_PXMAILBOX_ACK     BIT(1) /* Ack message recv'd from VF */
#define WX_PXMAILBOX_PFU     BIT(3) /* PF owns the mailbox buffer */

#define WX_PXMBMEM(i)        (0x5000 + (64 * (i))) /* i=[0,63] */

#define WX_VFLRE(i)          (0x4A0 + (4 * (i))) /* i=[0,1] */
#define WX_VFLREC(i)         (0x4A8 + (4 * (i))) /* i=[0,1] */

/* SR-IOV specific macros */
#define WX_MBVFICR(i)         (0x480 + (4 * (i))) /* i=[0,3] */
#define WX_MBVFICR_VFREQ_MASK GENMASK(15, 0)
#define WX_MBVFICR_VFACK_MASK GENMASK(31, 16)

#define WX_VT_MSGINFO_MASK    GENMASK(23, 16)

enum wxvf_xcast_modes {
	WXVF_XCAST_MODE_NONE = 0,
};

int wx_write_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf);
int wx_read_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf);
int wx_check_for_rst_pf(struct wx *wx, u16 mbx_id);
int wx_check_for_msg_pf(struct wx *wx, u16 mbx_id);
int wx_check_for_ack_pf(struct wx *wx, u16 mbx_id);

#endif /* _WX_MBX_H_ */
