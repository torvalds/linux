/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2004 Silicon Graphics, Inc. All Rights Reserved.
 */
#ifndef _ASM_IA64_SN_XTALK_XBOW_H
#define _ASM_IA64_SN_XTALK_XBOW_H

#define XBOW_PORT_8	0x8
#define XBOW_PORT_C	0xc
#define XBOW_PORT_F	0xf

#define MAX_XBOW_PORTS	8	/* number of ports on xbow chip */
#define BASE_XBOW_PORT	XBOW_PORT_8	/* Lowest external port */

#define	XBOW_CREDIT	4

#define MAX_XBOW_NAME 	16

/* Register set for each xbow link */
typedef volatile struct xb_linkregs_s {
/* 
 * we access these through synergy unswizzled space, so the address
 * gets twiddled (i.e. references to 0x4 actually go to 0x0 and vv.)
 * That's why we put the register first and filler second.
 */
    uint32_t               link_ibf;
    uint32_t               filler0;	/* filler for proper alignment */
    uint32_t               link_control;
    uint32_t               filler1;
    uint32_t               link_status;
    uint32_t               filler2;
    uint32_t               link_arb_upper;
    uint32_t               filler3;
    uint32_t               link_arb_lower;
    uint32_t               filler4;
    uint32_t               link_status_clr;
    uint32_t               filler5;
    uint32_t               link_reset;
    uint32_t               filler6;
    uint32_t               link_aux_status;
    uint32_t               filler7;
} xb_linkregs_t;

typedef volatile struct xbow_s {
    /* standard widget configuration                       0x000000-0x000057 */
    struct widget_cfg            xb_widget;  /* 0x000000 */

    /* helper fieldnames for accessing bridge widget */

#define xb_wid_id                       xb_widget.w_id
#define xb_wid_stat                     xb_widget.w_status
#define xb_wid_err_upper                xb_widget.w_err_upper_addr
#define xb_wid_err_lower                xb_widget.w_err_lower_addr
#define xb_wid_control                  xb_widget.w_control
#define xb_wid_req_timeout              xb_widget.w_req_timeout
#define xb_wid_int_upper                xb_widget.w_intdest_upper_addr
#define xb_wid_int_lower                xb_widget.w_intdest_lower_addr
#define xb_wid_err_cmdword              xb_widget.w_err_cmd_word
#define xb_wid_llp                      xb_widget.w_llp_cfg
#define xb_wid_stat_clr                 xb_widget.w_tflush

/* 
 * we access these through synergy unswizzled space, so the address
 * gets twiddled (i.e. references to 0x4 actually go to 0x0 and vv.)
 * That's why we put the register first and filler second.
 */
    /* xbow-specific widget configuration                  0x000058-0x0000FF */
    uint32_t               xb_wid_arb_reload;  /* 0x00005C */
    uint32_t               _pad_000058;
    uint32_t               xb_perf_ctr_a;      /* 0x000064 */
    uint32_t               _pad_000060;
    uint32_t               xb_perf_ctr_b;      /* 0x00006c */
    uint32_t               _pad_000068;
    uint32_t               xb_nic;     /* 0x000074 */
    uint32_t               _pad_000070;

    /* Xbridge only */
    uint32_t               xb_w0_rst_fnc;      /* 0x00007C */
    uint32_t               _pad_000078;
    uint32_t               xb_l8_rst_fnc;      /* 0x000084 */
    uint32_t               _pad_000080;
    uint32_t               xb_l9_rst_fnc;      /* 0x00008c */
    uint32_t               _pad_000088;
    uint32_t               xb_la_rst_fnc;      /* 0x000094 */
    uint32_t               _pad_000090;
    uint32_t               xb_lb_rst_fnc;      /* 0x00009c */
    uint32_t               _pad_000098;
    uint32_t               xb_lc_rst_fnc;      /* 0x0000a4 */
    uint32_t               _pad_0000a0;
    uint32_t               xb_ld_rst_fnc;      /* 0x0000ac */
    uint32_t               _pad_0000a8;
    uint32_t               xb_le_rst_fnc;      /* 0x0000b4 */
    uint32_t               _pad_0000b0;
    uint32_t               xb_lf_rst_fnc;      /* 0x0000bc */
    uint32_t               _pad_0000b8;
    uint32_t               xb_lock;            /* 0x0000c4 */
    uint32_t               _pad_0000c0;
    uint32_t               xb_lock_clr;        /* 0x0000cc */
    uint32_t               _pad_0000c8;
    /* end of Xbridge only */
    uint32_t               _pad_0000d0[12];

    /* Link Specific Registers, port 8..15                 0x000100-0x000300 */
    xb_linkregs_t           xb_link_raw[MAX_XBOW_PORTS];
#define xb_link(p)      xb_link_raw[(p) & (MAX_XBOW_PORTS - 1)]

} xbow_t;

#define XB_FLAGS_EXISTS		0x1	/* device exists */
#define XB_FLAGS_MASTER		0x2
#define XB_FLAGS_SLAVE		0x0
#define XB_FLAGS_GBR		0x4
#define XB_FLAGS_16BIT		0x8
#define XB_FLAGS_8BIT		0x0

/* is widget port number valid?  (based on version 7.0 of xbow spec) */
#define XBOW_WIDGET_IS_VALID(wid) ((wid) >= XBOW_PORT_8 && (wid) <= XBOW_PORT_F)

/* whether to use upper or lower arbitration register, given source widget id */
#define XBOW_ARB_IS_UPPER(wid) 	((wid) >= XBOW_PORT_8 && (wid) <= XBOW_PORT_B)
#define XBOW_ARB_IS_LOWER(wid) 	((wid) >= XBOW_PORT_C && (wid) <= XBOW_PORT_F)

/* offset of arbitration register, given source widget id */
#define XBOW_ARB_OFF(wid) 	(XBOW_ARB_IS_UPPER(wid) ? 0x1c : 0x24)

#define	XBOW_WID_ID		WIDGET_ID
#define	XBOW_WID_STAT		WIDGET_STATUS
#define	XBOW_WID_ERR_UPPER	WIDGET_ERR_UPPER_ADDR
#define	XBOW_WID_ERR_LOWER	WIDGET_ERR_LOWER_ADDR
#define	XBOW_WID_CONTROL	WIDGET_CONTROL
#define	XBOW_WID_REQ_TO		WIDGET_REQ_TIMEOUT
#define	XBOW_WID_INT_UPPER	WIDGET_INTDEST_UPPER_ADDR
#define	XBOW_WID_INT_LOWER	WIDGET_INTDEST_LOWER_ADDR
#define	XBOW_WID_ERR_CMDWORD	WIDGET_ERR_CMD_WORD
#define	XBOW_WID_LLP		WIDGET_LLP_CFG
#define	XBOW_WID_STAT_CLR	WIDGET_TFLUSH
#define XBOW_WID_ARB_RELOAD 	0x5c
#define XBOW_WID_PERF_CTR_A 	0x64
#define XBOW_WID_PERF_CTR_B 	0x6c
#define XBOW_WID_NIC 		0x74

/* Xbridge only */
#define XBOW_W0_RST_FNC		0x00007C
#define	XBOW_L8_RST_FNC		0x000084
#define	XBOW_L9_RST_FNC		0x00008c
#define	XBOW_LA_RST_FNC		0x000094
#define	XBOW_LB_RST_FNC		0x00009c
#define	XBOW_LC_RST_FNC		0x0000a4
#define	XBOW_LD_RST_FNC		0x0000ac
#define	XBOW_LE_RST_FNC		0x0000b4
#define	XBOW_LF_RST_FNC		0x0000bc
#define XBOW_RESET_FENCE(x) ((x) > 7 && (x) < 16) ? \
				(XBOW_W0_RST_FNC + ((x) - 7) * 8) : \
				((x) == 0) ? XBOW_W0_RST_FNC : 0
#define XBOW_LOCK		0x0000c4
#define XBOW_LOCK_CLR		0x0000cc
/* End of Xbridge only */

/* used only in ide, but defined here within the reserved portion */
/*              of the widget0 address space (before 0xf4) */
#define	XBOW_WID_UNDEF		0xe4

/* xbow link register set base, legal value for x is 0x8..0xf */
#define	XB_LINK_BASE		0x100
#define	XB_LINK_OFFSET		0x40
#define	XB_LINK_REG_BASE(x)	(XB_LINK_BASE + ((x) & (MAX_XBOW_PORTS - 1)) * XB_LINK_OFFSET)

#define	XB_LINK_IBUF_FLUSH(x)	(XB_LINK_REG_BASE(x) + 0x4)
#define	XB_LINK_CTRL(x)		(XB_LINK_REG_BASE(x) + 0xc)
#define	XB_LINK_STATUS(x)	(XB_LINK_REG_BASE(x) + 0x14)
#define	XB_LINK_ARB_UPPER(x)	(XB_LINK_REG_BASE(x) + 0x1c)
#define	XB_LINK_ARB_LOWER(x)	(XB_LINK_REG_BASE(x) + 0x24)
#define	XB_LINK_STATUS_CLR(x)	(XB_LINK_REG_BASE(x) + 0x2c)
#define	XB_LINK_RESET(x)	(XB_LINK_REG_BASE(x) + 0x34)
#define	XB_LINK_AUX_STATUS(x)	(XB_LINK_REG_BASE(x) + 0x3c)

/* link_control(x) */
#define	XB_CTRL_LINKALIVE_IE		0x80000000	/* link comes alive */
     /* reserved:			0x40000000 */
#define	XB_CTRL_PERF_CTR_MODE_MSK	0x30000000	/* perf counter mode */
#define	XB_CTRL_IBUF_LEVEL_MSK		0x0e000000	/* input packet buffer level */
#define	XB_CTRL_8BIT_MODE		0x01000000	/* force link into 8 bit mode */
#define XB_CTRL_BAD_LLP_PKT		0x00800000	/* force bad LLP packet */
#define XB_CTRL_WIDGET_CR_MSK		0x007c0000	/* LLP widget credit mask */
#define XB_CTRL_WIDGET_CR_SHFT	18			/* LLP widget credit shift */
#define XB_CTRL_ILLEGAL_DST_IE		0x00020000	/* illegal destination */
#define XB_CTRL_OALLOC_IBUF_IE		0x00010000	/* overallocated input buffer */
     /* reserved:			0x0000fe00 */
#define XB_CTRL_BNDWDTH_ALLOC_IE	0x00000100	/* bandwidth alloc */
#define XB_CTRL_RCV_CNT_OFLOW_IE	0x00000080	/* rcv retry overflow */
#define XB_CTRL_XMT_CNT_OFLOW_IE	0x00000040	/* xmt retry overflow */
#define XB_CTRL_XMT_MAX_RTRY_IE		0x00000020	/* max transmit retry */
#define XB_CTRL_RCV_IE			0x00000010	/* receive */
#define XB_CTRL_XMT_RTRY_IE		0x00000008	/* transmit retry */
     /* reserved:			0x00000004 */
#define	XB_CTRL_MAXREQ_TOUT_IE		0x00000002	/* maximum request timeout */
#define	XB_CTRL_SRC_TOUT_IE		0x00000001	/* source timeout */

/* link_status(x) */
#define	XB_STAT_LINKALIVE		XB_CTRL_LINKALIVE_IE
     /* reserved:			0x7ff80000 */
#define	XB_STAT_MULTI_ERR		0x00040000	/* multi error */
#define	XB_STAT_ILLEGAL_DST_ERR		XB_CTRL_ILLEGAL_DST_IE
#define	XB_STAT_OALLOC_IBUF_ERR		XB_CTRL_OALLOC_IBUF_IE
#define	XB_STAT_BNDWDTH_ALLOC_ID_MSK	0x0000ff00	/* port bitmask */
#define	XB_STAT_RCV_CNT_OFLOW_ERR	XB_CTRL_RCV_CNT_OFLOW_IE
#define	XB_STAT_XMT_CNT_OFLOW_ERR	XB_CTRL_XMT_CNT_OFLOW_IE
#define	XB_STAT_XMT_MAX_RTRY_ERR	XB_CTRL_XMT_MAX_RTRY_IE
#define	XB_STAT_RCV_ERR			XB_CTRL_RCV_IE
#define	XB_STAT_XMT_RTRY_ERR		XB_CTRL_XMT_RTRY_IE
     /* reserved:			0x00000004 */
#define	XB_STAT_MAXREQ_TOUT_ERR		XB_CTRL_MAXREQ_TOUT_IE
#define	XB_STAT_SRC_TOUT_ERR		XB_CTRL_SRC_TOUT_IE

/* link_aux_status(x) */
#define	XB_AUX_STAT_RCV_CNT	0xff000000
#define	XB_AUX_STAT_XMT_CNT	0x00ff0000
#define	XB_AUX_STAT_TOUT_DST	0x0000ff00
#define	XB_AUX_LINKFAIL_RST_BAD	0x00000040
#define	XB_AUX_STAT_PRESENT	0x00000020
#define	XB_AUX_STAT_PORT_WIDTH	0x00000010
     /*	reserved:		0x0000000f */

/*
 * link_arb_upper/link_arb_lower(x), (reg) should be the link_arb_upper
 * register if (x) is 0x8..0xb, link_arb_lower if (x) is 0xc..0xf
 */
#define	XB_ARB_GBR_MSK		0x1f
#define	XB_ARB_RR_MSK		0x7
#define	XB_ARB_GBR_SHFT(x)	(((x) & 0x3) * 8)
#define	XB_ARB_RR_SHFT(x)	(((x) & 0x3) * 8 + 5)
#define	XB_ARB_GBR_CNT(reg,x)	((reg) >> XB_ARB_GBR_SHFT(x) & XB_ARB_GBR_MSK)
#define	XB_ARB_RR_CNT(reg,x)	((reg) >> XB_ARB_RR_SHFT(x) & XB_ARB_RR_MSK)

/* XBOW_WID_STAT */
#define	XB_WID_STAT_LINK_INTR_SHFT	(24)
#define	XB_WID_STAT_LINK_INTR_MASK	(0xFF << XB_WID_STAT_LINK_INTR_SHFT)
#define	XB_WID_STAT_LINK_INTR(x)	(0x1 << (((x)&7) + XB_WID_STAT_LINK_INTR_SHFT))
#define	XB_WID_STAT_WIDGET0_INTR	0x00800000
#define XB_WID_STAT_SRCID_MASK		0x000003c0	/* Xbridge only */
#define	XB_WID_STAT_REG_ACC_ERR		0x00000020
#define XB_WID_STAT_RECV_TOUT		0x00000010	/* Xbridge only */
#define XB_WID_STAT_ARB_TOUT		0x00000008	/* Xbridge only */
#define	XB_WID_STAT_XTALK_ERR		0x00000004
#define XB_WID_STAT_DST_TOUT		0x00000002	/* Xbridge only */
#define	XB_WID_STAT_MULTI_ERR		0x00000001

#define XB_WID_STAT_SRCID_SHFT		6

/* XBOW_WID_CONTROL */
#define XB_WID_CTRL_REG_ACC_IE		XB_WID_STAT_REG_ACC_ERR
#define XB_WID_CTRL_RECV_TOUT		XB_WID_STAT_RECV_TOUT
#define XB_WID_CTRL_ARB_TOUT		XB_WID_STAT_ARB_TOUT
#define XB_WID_CTRL_XTALK_IE		XB_WID_STAT_XTALK_ERR

/* XBOW_WID_INT_UPPER */
/* defined in xwidget.h for WIDGET_INTDEST_UPPER_ADDR */

/* XBOW WIDGET part number, in the ID register */
#define XBOW_WIDGET_PART_NUM	0x0		/* crossbow */
#define XXBOW_WIDGET_PART_NUM	0xd000		/* Xbridge */
#define	XBOW_WIDGET_MFGR_NUM	0x0
#define	XXBOW_WIDGET_MFGR_NUM	0x0
#define PXBOW_WIDGET_PART_NUM   0xd100          /* PIC */

#define	XBOW_REV_1_0		0x1	/* xbow rev 1.0 is "1" */
#define	XBOW_REV_1_1		0x2	/* xbow rev 1.1 is "2" */
#define XBOW_REV_1_2		0x3	/* xbow rev 1.2 is "3" */
#define XBOW_REV_1_3		0x4	/* xbow rev 1.3 is "4" */
#define XBOW_REV_2_0		0x5	/* xbow rev 2.0 is "5" */

#define XXBOW_PART_REV_1_0		(XXBOW_WIDGET_PART_NUM << 4 | 0x1 )
#define XXBOW_PART_REV_2_0		(XXBOW_WIDGET_PART_NUM << 4 | 0x2 )

/* XBOW_WID_ARB_RELOAD */
#define	XBOW_WID_ARB_RELOAD_INT	0x3f	/* GBR reload interval */

#define IS_XBRIDGE_XBOW(wid) \
        (XWIDGET_PART_NUM(wid) == XXBOW_WIDGET_PART_NUM && \
                        XWIDGET_MFG_NUM(wid) == XXBOW_WIDGET_MFGR_NUM)

#define IS_PIC_XBOW(wid) \
        (XWIDGET_PART_NUM(wid) == PXBOW_WIDGET_PART_NUM && \
                        XWIDGET_MFG_NUM(wid) == XXBOW_WIDGET_MFGR_NUM)

#define XBOW_WAR_ENABLED(pv, widid) ((1 << XWIDGET_REV_NUM(widid)) & pv)

#endif                          /* _ASM_IA64_SN_XTALK_XBOW_H */
