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

/* VF Registers */
#define WX_VXMAILBOX         0x600
#define WX_VXMAILBOX_REQ     BIT(0) /* Request for PF Ready bit */
#define WX_VXMAILBOX_ACK     BIT(1) /* Ack PF message received */
#define WX_VXMAILBOX_VFU     BIT(2) /* VF owns the mailbox buffer */
#define WX_VXMAILBOX_PFU     BIT(3) /* PF owns the mailbox buffer */
#define WX_VXMAILBOX_PFSTS   BIT(4) /* PF wrote a message in the MB */
#define WX_VXMAILBOX_PFACK   BIT(5) /* PF ack the previous VF msg */
#define WX_VXMAILBOX_RSTI    BIT(6) /* PF has reset indication */
#define WX_VXMAILBOX_RSTD    BIT(7) /* PF has indicated reset done */
#define WX_VXMAILBOX_R2C_BITS (WX_VXMAILBOX_RSTD | \
	    WX_VXMAILBOX_PFSTS | WX_VXMAILBOX_PFACK)

#define WX_VXMBMEM           0x00C00 /* 16*4B */
#define WX_PXMBMEM(i)        (0x5000 + (64 * (i))) /* i=[0,63] */

#define WX_VFLRE(i)          (0x4A0 + (4 * (i))) /* i=[0,1] */
#define WX_VFLREC(i)         (0x4A8 + (4 * (i))) /* i=[0,1] */

/* SR-IOV specific macros */
#define WX_MBVFICR(i)         (0x480 + (4 * (i))) /* i=[0,3] */
#define WX_MBVFICR_VFREQ_MASK GENMASK(15, 0)
#define WX_MBVFICR_VFACK_MASK GENMASK(31, 16)

#define WX_VT_MSGTYPE_ACK     BIT(31)
#define WX_VT_MSGTYPE_NACK    BIT(30)
#define WX_VT_MSGTYPE_CTS     BIT(29)
#define WX_VT_MSGINFO_SHIFT   16
#define WX_VT_MSGINFO_MASK    GENMASK(23, 16)

enum wx_pfvf_api_rev {
	wx_mbox_api_null,
	wx_mbox_api_13 = 4,      /* API version 1.3 */
	wx_mbox_api_unknown, /* indicates that API version is not known */
};

/* mailbox API */
#define WX_VF_RESET                  0x01 /* VF requests reset */
#define WX_VF_SET_MAC_ADDR           0x02 /* VF requests PF to set MAC addr */
#define WX_VF_SET_MULTICAST          0x03 /* VF requests PF to set MC addr */
#define WX_VF_SET_VLAN               0x04 /* VF requests PF to set VLAN */
#define WX_VF_SET_LPE                0x05 /* VF requests PF to set VMOLR.LPE */
#define WX_VF_SET_MACVLAN            0x06 /* VF requests PF unicast filter */
#define WX_VF_API_NEGOTIATE          0x08 /* negotiate API version */
#define WX_VF_GET_QUEUES             0x09 /* get queue configuration */
#define WX_VF_GET_RETA               0x0a /* VF request for RETA */
#define WX_VF_GET_RSS_KEY            0x0b /* get RSS key */
#define WX_VF_UPDATE_XCAST_MODE      0x0c
#define WX_VF_GET_LINK_STATE         0x10 /* get vf link state */
#define WX_VF_GET_FW_VERSION         0x11 /* get fw version */

#define WX_VF_BACKUP                 0x8001 /* VF requests backup */

#define WX_PF_CONTROL_MSG            BIT(8) /* PF control message */
#define WX_PF_NOFITY_VF_LINK_STATUS  0x1
#define WX_PF_NOFITY_VF_NET_NOT_RUNNING BIT(31)

#define WX_VF_TX_QUEUES              1 /* number of Tx queues supported */
#define WX_VF_RX_QUEUES              2 /* number of Rx queues supported */
#define WX_VF_TRANS_VLAN             3 /* Indication of port vlan */
#define WX_VF_DEF_QUEUE              4 /* Default queue offset */

#define WX_VF_PERMADDR_MSG_LEN       4

enum wxvf_xcast_modes {
	WXVF_XCAST_MODE_NONE = 0,
	WXVF_XCAST_MODE_MULTI,
	WXVF_XCAST_MODE_ALLMULTI,
	WXVF_XCAST_MODE_PROMISC,
};

int wx_write_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf);
int wx_read_mbx_pf(struct wx *wx, u32 *msg, u16 size, u16 vf);
int wx_check_for_rst_pf(struct wx *wx, u16 mbx_id);
int wx_check_for_msg_pf(struct wx *wx, u16 mbx_id);
int wx_check_for_ack_pf(struct wx *wx, u16 mbx_id);

int wx_read_posted_mbx(struct wx *wx, u32 *msg, u16 size);
int wx_write_posted_mbx(struct wx *wx, u32 *msg, u16 size);
int wx_check_for_rst_vf(struct wx *wx);
int wx_check_for_msg_vf(struct wx *wx);
int wx_read_mbx_vf(struct wx *wx, u32 *msg, u16 size);
int wx_write_mbx_vf(struct wx *wx, u32 *msg, u16 size);
int wx_init_mbx_params_vf(struct wx *wx);

#endif /* _WX_MBX_H_ */
