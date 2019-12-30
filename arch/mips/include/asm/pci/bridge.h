/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * bridge.h - bridge chip header file, derived from IRIX <sys/PCI/bridge.h>,
 * revision 1.76.
 *
 * Copyright (C) 1996, 1999 Silcon Graphics, Inc.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_PCI_BRIDGE_H
#define _ASM_PCI_BRIDGE_H

#include <linux/types.h>
#include <linux/pci.h>
#include <asm/xtalk/xwidget.h>		/* generic widget header */
#include <asm/sn/types.h>

/* I/O page size */

#define IOPFNSHIFT		12	/* 4K per mapped page */

#define IOPGSIZE		(1 << IOPFNSHIFT)
#define IOPG(x)			((x) >> IOPFNSHIFT)
#define IOPGOFF(x)		((x) & (IOPGSIZE-1))

/* Bridge RAM sizes */

#define BRIDGE_ATE_RAM_SIZE	0x00000400	/* 1kB ATE RAM */

#define BRIDGE_CONFIG_BASE	0x20000
#define BRIDGE_CONFIG1_BASE	0x28000
#define BRIDGE_CONFIG_END	0x30000
#define BRIDGE_CONFIG_SLOT_SIZE 0x1000

#define BRIDGE_SSRAM_512K	0x00080000	/* 512kB */
#define BRIDGE_SSRAM_128K	0x00020000	/* 128kB */
#define BRIDGE_SSRAM_64K	0x00010000	/* 64kB */
#define BRIDGE_SSRAM_0K		0x00000000	/* 0kB */

/* ========================================================================
 *    Bridge address map
 */

#ifndef __ASSEMBLY__

#define ATE_V		0x01
#define ATE_CO		0x02
#define ATE_PREC	0x04
#define ATE_PREF	0x08
#define ATE_BAR		0x10

#define ATE_PFNSHIFT		12
#define ATE_TIDSHIFT		8
#define ATE_RMFSHIFT		48

#define mkate(xaddr, xid, attr) (((xaddr) & 0x0000fffffffff000ULL) | \
				 ((xid)<<ATE_TIDSHIFT) | \
				 (attr))

#define BRIDGE_INTERNAL_ATES	128

/*
 * It is generally preferred that hardware registers on the bridge
 * are located from C code via this structure.
 *
 * Generated from Bridge spec dated 04oct95
 */

struct bridge_regs {
	/* Local Registers			       0x000000-0x00FFFF */

	/* standard widget configuration	       0x000000-0x000057 */
	widget_cfg_t	    b_widget;			/* 0x000000 */

	/* helper fieldnames for accessing bridge widget */

#define b_wid_id			b_widget.w_id
#define b_wid_stat			b_widget.w_status
#define b_wid_err_upper			b_widget.w_err_upper_addr
#define b_wid_err_lower			b_widget.w_err_lower_addr
#define b_wid_control			b_widget.w_control
#define b_wid_req_timeout		b_widget.w_req_timeout
#define b_wid_int_upper			b_widget.w_intdest_upper_addr
#define b_wid_int_lower			b_widget.w_intdest_lower_addr
#define b_wid_err_cmdword		b_widget.w_err_cmd_word
#define b_wid_llp			b_widget.w_llp_cfg
#define b_wid_tflush			b_widget.w_tflush

	/* bridge-specific widget configuration 0x000058-0x00007F */
	u32	_pad_000058;
	u32	b_wid_aux_err;		/* 0x00005C */
	u32	_pad_000060;
	u32	b_wid_resp_upper;		/* 0x000064 */
	u32	_pad_000068;
	u32	b_wid_resp_lower;		/* 0x00006C */
	u32	_pad_000070;
	u32	 b_wid_tst_pin_ctrl;		/* 0x000074 */
	u32	_pad_000078[2];

	/* PMU & Map 0x000080-0x00008F */
	u32	_pad_000080;
	u32	b_dir_map;			/* 0x000084 */
	u32	_pad_000088[2];

	/* SSRAM 0x000090-0x00009F */
	u32	_pad_000090;
	u32	b_ram_perr;			/* 0x000094 */
	u32	_pad_000098[2];

	/* Arbitration 0x0000A0-0x0000AF */
	u32	_pad_0000A0;
	u32	b_arb;				/* 0x0000A4 */
	u32	_pad_0000A8[2];

	/* Number In A Can 0x0000B0-0x0000BF */
	u32	_pad_0000B0;
	u32	b_nic;				/* 0x0000B4 */
	u32	_pad_0000B8[2];

	/* PCI/GIO 0x0000C0-0x0000FF */
	u32	_pad_0000C0;
	u32	b_bus_timeout;			/* 0x0000C4 */
#define b_pci_bus_timeout b_bus_timeout

	u32	_pad_0000C8;
	u32	b_pci_cfg;			/* 0x0000CC */
	u32	_pad_0000D0;
	u32	b_pci_err_upper;		/* 0x0000D4 */
	u32	_pad_0000D8;
	u32	b_pci_err_lower;		/* 0x0000DC */
	u32	_pad_0000E0[8];
#define b_gio_err_lower b_pci_err_lower
#define b_gio_err_upper b_pci_err_upper

	/* Interrupt 0x000100-0x0001FF */
	u32	_pad_000100;
	u32	b_int_status;			/* 0x000104 */
	u32	_pad_000108;
	u32	b_int_enable;			/* 0x00010C */
	u32	_pad_000110;
	u32	b_int_rst_stat;			/* 0x000114 */
	u32	_pad_000118;
	u32	b_int_mode;			/* 0x00011C */
	u32	_pad_000120;
	u32	b_int_device;			/* 0x000124 */
	u32	_pad_000128;
	u32	b_int_host_err;			/* 0x00012C */

	struct {
		u32	__pad;			/* 0x0001{30,,,68} */
		u32	addr;			/* 0x0001{34,,,6C} */
	} b_int_addr[8];				/* 0x000130 */

	u32	_pad_000170[36];

	/* Device 0x000200-0x0003FF */
	struct {
		u32	__pad;			/* 0x0002{00,,,38} */
		u32	reg;			/* 0x0002{04,,,3C} */
	} b_device[8];					/* 0x000200 */

	struct {
		u32	__pad;			/* 0x0002{40,,,78} */
		u32	reg;			/* 0x0002{44,,,7C} */
	} b_wr_req_buf[8];				/* 0x000240 */

	struct {
		u32	__pad;			/* 0x0002{80,,,88} */
		u32	reg;			/* 0x0002{84,,,8C} */
	} b_rrb_map[2];					/* 0x000280 */
#define b_even_resp	b_rrb_map[0].reg		/* 0x000284 */
#define b_odd_resp	b_rrb_map[1].reg		/* 0x00028C */

	u32	_pad_000290;
	u32	b_resp_status;			/* 0x000294 */
	u32	_pad_000298;
	u32	b_resp_clear;			/* 0x00029C */

	u32	_pad_0002A0[24];

	char		_pad_000300[0x10000 - 0x000300];

	/* Internal Address Translation Entry RAM 0x010000-0x0103FF */
	union {
		u64	wr;			/* write-only */
		struct {
			u32	_p_pad;
			u32	rd;		/* read-only */
		}			hi;
	}			    b_int_ate_ram[128];

	char	_pad_010400[0x11000 - 0x010400];

	/* Internal Address Translation Entry RAM LOW 0x011000-0x0113FF */
	struct {
		u32	_p_pad;
		u32	rd;		/* read-only */
	} b_int_ate_ram_lo[128];

	char	_pad_011400[0x20000 - 0x011400];

	/* PCI Device Configuration Spaces 0x020000-0x027FFF */
	union {				/* make all access sizes available. */
		u8	c[0x1000 / 1];
		u16	s[0x1000 / 2];
		u32	l[0x1000 / 4];
		u64	d[0x1000 / 8];
		union {
			u8	c[0x100 / 1];
			u16	s[0x100 / 2];
			u32	l[0x100 / 4];
			u64	d[0x100 / 8];
		} f[8];
	} b_type0_cfg_dev[8];					/* 0x020000 */

	/* PCI Type 1 Configuration Space 0x028000-0x028FFF */
	union {				/* make all access sizes available. */
		u8	c[0x1000 / 1];
		u16	s[0x1000 / 2];
		u32	l[0x1000 / 4];
		u64	d[0x1000 / 8];
	} b_type1_cfg;					/* 0x028000-0x029000 */

	char	_pad_029000[0x007000];			/* 0x029000-0x030000 */

	/* PCI Interrupt Acknowledge Cycle 0x030000 */
	union {
		u8	c[8 / 1];
		u16	s[8 / 2];
		u32	l[8 / 4];
		u64	d[8 / 8];
	} b_pci_iack;						/* 0x030000 */

	u8	_pad_030007[0x04fff8];			/* 0x030008-0x07FFFF */

	/* External Address Translation Entry RAM 0x080000-0x0FFFFF */
	u64	b_ext_ate_ram[0x10000];

	/* Reserved 0x100000-0x1FFFFF */
	char	_pad_100000[0x200000-0x100000];

	/* PCI/GIO Device Spaces 0x200000-0xBFFFFF */
	union {				/* make all access sizes available. */
		u8	c[0x100000 / 1];
		u16	s[0x100000 / 2];
		u32	l[0x100000 / 4];
		u64	d[0x100000 / 8];
	} b_devio_raw[10];				/* 0x200000 */

	/* b_devio macro is a bit strange; it reflects the
	 * fact that the Bridge ASIC provides 2M for the
	 * first two DevIO windows and 1M for the other six.
	 */
#define b_devio(n)	b_devio_raw[((n)<2)?(n*2):(n+2)]

	/* External Flash Proms 1,0 0xC00000-0xFFFFFF */
	union {		/* make all access sizes available. */
		u8	c[0x400000 / 1];	/* read-only */
		u16	s[0x400000 / 2];	/* read-write */
		u32	l[0x400000 / 4];	/* read-only */
		u64	d[0x400000 / 8];	/* read-only */
	} b_external_flash;			/* 0xC00000 */
};

/*
 * Field formats for Error Command Word and Auxiliary Error Command Word
 * of bridge.
 */
struct bridge_err_cmdword {
	union {
		u32		cmd_word;
		struct {
			u32	didn:4,		/* Destination ID  */
				sidn:4,		/* Source ID	   */
				pactyp:4,	/* Packet type	   */
				tnum:5,		/* Trans Number	   */
				coh:1,		/* Coh Transaction */
				ds:2,		/* Data size	   */
				gbr:1,		/* GBR enable	   */
				vbpm:1,		/* VBPM message	   */
				error:1,	/* Error occurred  */
				barr:1,		/* Barrier op	   */
				rsvd:8;
		} berr_st;
	} berr_un;
};

#define berr_field	berr_un.berr_st
#endif /* !__ASSEMBLY__ */

/*
 * The values of these macros can and should be crosschecked
 * regularly against the offsets of the like-named fields
 * within the bridge_regs structure above.
 */

/* Byte offset macros for Bridge internal registers */

#define BRIDGE_WID_ID		WIDGET_ID
#define BRIDGE_WID_STAT		WIDGET_STATUS
#define BRIDGE_WID_ERR_UPPER	WIDGET_ERR_UPPER_ADDR
#define BRIDGE_WID_ERR_LOWER	WIDGET_ERR_LOWER_ADDR
#define BRIDGE_WID_CONTROL	WIDGET_CONTROL
#define BRIDGE_WID_REQ_TIMEOUT	WIDGET_REQ_TIMEOUT
#define BRIDGE_WID_INT_UPPER	WIDGET_INTDEST_UPPER_ADDR
#define BRIDGE_WID_INT_LOWER	WIDGET_INTDEST_LOWER_ADDR
#define BRIDGE_WID_ERR_CMDWORD	WIDGET_ERR_CMD_WORD
#define BRIDGE_WID_LLP		WIDGET_LLP_CFG
#define BRIDGE_WID_TFLUSH	WIDGET_TFLUSH

#define BRIDGE_WID_AUX_ERR	0x00005C	/* Aux Error Command Word */
#define BRIDGE_WID_RESP_UPPER	0x000064	/* Response Buf Upper Addr */
#define BRIDGE_WID_RESP_LOWER	0x00006C	/* Response Buf Lower Addr */
#define BRIDGE_WID_TST_PIN_CTRL 0x000074	/* Test pin control */

#define BRIDGE_DIR_MAP		0x000084	/* Direct Map reg */

#define BRIDGE_RAM_PERR		0x000094	/* SSRAM Parity Error */

#define BRIDGE_ARB		0x0000A4	/* Arbitration Priority reg */

#define BRIDGE_NIC		0x0000B4	/* Number In A Can */

#define BRIDGE_BUS_TIMEOUT	0x0000C4	/* Bus Timeout Register */
#define BRIDGE_PCI_BUS_TIMEOUT	BRIDGE_BUS_TIMEOUT
#define BRIDGE_PCI_CFG		0x0000CC	/* PCI Type 1 Config reg */
#define BRIDGE_PCI_ERR_UPPER	0x0000D4	/* PCI error Upper Addr */
#define BRIDGE_PCI_ERR_LOWER	0x0000DC	/* PCI error Lower Addr */

#define BRIDGE_INT_STATUS	0x000104	/* Interrupt Status */
#define BRIDGE_INT_ENABLE	0x00010C	/* Interrupt Enables */
#define BRIDGE_INT_RST_STAT	0x000114	/* Reset Intr Status */
#define BRIDGE_INT_MODE		0x00011C	/* Interrupt Mode */
#define BRIDGE_INT_DEVICE	0x000124	/* Interrupt Device */
#define BRIDGE_INT_HOST_ERR	0x00012C	/* Host Error Field */

#define BRIDGE_INT_ADDR0	0x000134	/* Host Address Reg */
#define BRIDGE_INT_ADDR_OFF	0x000008	/* Host Addr offset (1..7) */
#define BRIDGE_INT_ADDR(x)	(BRIDGE_INT_ADDR0+(x)*BRIDGE_INT_ADDR_OFF)

#define BRIDGE_DEVICE0		0x000204	/* Device 0 */
#define BRIDGE_DEVICE_OFF	0x000008	/* Device offset (1..7) */
#define BRIDGE_DEVICE(x)	(BRIDGE_DEVICE0+(x)*BRIDGE_DEVICE_OFF)

#define BRIDGE_WR_REQ_BUF0	0x000244	/* Write Request Buffer 0 */
#define BRIDGE_WR_REQ_BUF_OFF	0x000008	/* Buffer Offset (1..7) */
#define BRIDGE_WR_REQ_BUF(x)	(BRIDGE_WR_REQ_BUF0+(x)*BRIDGE_WR_REQ_BUF_OFF)

#define BRIDGE_EVEN_RESP	0x000284	/* Even Device Response Buf */
#define BRIDGE_ODD_RESP		0x00028C	/* Odd Device Response Buf */

#define BRIDGE_RESP_STATUS	0x000294	/* Read Response Status reg */
#define BRIDGE_RESP_CLEAR	0x00029C	/* Read Response Clear reg */

/* Byte offset macros for Bridge I/O space */

#define BRIDGE_ATE_RAM		0x00010000	/* Internal Addr Xlat Ram */

#define BRIDGE_TYPE0_CFG_DEV0	0x00020000	/* Type 0 Cfg, Device 0 */
#define BRIDGE_TYPE0_CFG_SLOT_OFF	0x00001000	/* Type 0 Cfg Slot Offset (1..7) */
#define BRIDGE_TYPE0_CFG_FUNC_OFF	0x00000100	/* Type 0 Cfg Func Offset (1..7) */
#define BRIDGE_TYPE0_CFG_DEV(s)		(BRIDGE_TYPE0_CFG_DEV0+\
					 (s)*BRIDGE_TYPE0_CFG_SLOT_OFF)
#define BRIDGE_TYPE0_CFG_DEVF(s, f)	(BRIDGE_TYPE0_CFG_DEV0+\
					 (s)*BRIDGE_TYPE0_CFG_SLOT_OFF+\
					 (f)*BRIDGE_TYPE0_CFG_FUNC_OFF)

#define BRIDGE_TYPE1_CFG	0x00028000	/* Type 1 Cfg space */

#define BRIDGE_PCI_IACK		0x00030000	/* PCI Interrupt Ack */
#define BRIDGE_EXT_SSRAM	0x00080000	/* Extern SSRAM (ATE) */

/* Byte offset macros for Bridge device IO spaces */

#define BRIDGE_DEV_CNT		8	/* Up to 8 devices per bridge */
#define BRIDGE_DEVIO0		0x00200000	/* Device IO 0 Addr */
#define BRIDGE_DEVIO1		0x00400000	/* Device IO 1 Addr */
#define BRIDGE_DEVIO2		0x00600000	/* Device IO 2 Addr */
#define BRIDGE_DEVIO_OFF	0x00100000	/* Device IO Offset (3..7) */

#define BRIDGE_DEVIO_2MB	0x00200000	/* Device IO Offset (0..1) */
#define BRIDGE_DEVIO_1MB	0x00100000	/* Device IO Offset (2..7) */

#define BRIDGE_DEVIO(x)		((x)<=1 ? BRIDGE_DEVIO0+(x)*BRIDGE_DEVIO_2MB : BRIDGE_DEVIO2+((x)-2)*BRIDGE_DEVIO_1MB)

#define BRIDGE_EXTERNAL_FLASH	0x00C00000	/* External Flash PROMS */

/* ========================================================================
 *    Bridge register bit field definitions
 */

/* Widget part number of bridge */
#define BRIDGE_WIDGET_PART_NUM		0xc002
#define XBRIDGE_WIDGET_PART_NUM		0xd002

/* Manufacturer of bridge */
#define BRIDGE_WIDGET_MFGR_NUM		0x036
#define XBRIDGE_WIDGET_MFGR_NUM		0x024

/* Revision numbers for known Bridge revisions */
#define BRIDGE_REV_A			0x1
#define BRIDGE_REV_B			0x2
#define BRIDGE_REV_C			0x3
#define BRIDGE_REV_D			0x4

/* Bridge widget status register bits definition */

#define BRIDGE_STAT_LLP_REC_CNT		(0xFFu << 24)
#define BRIDGE_STAT_LLP_TX_CNT		(0xFF << 16)
#define BRIDGE_STAT_FLASH_SELECT	(0x1 << 6)
#define BRIDGE_STAT_PCI_GIO_N		(0x1 << 5)
#define BRIDGE_STAT_PENDING		(0x1F << 0)

/* Bridge widget control register bits definition */
#define BRIDGE_CTRL_FLASH_WR_EN		(0x1ul << 31)
#define BRIDGE_CTRL_EN_CLK50		(0x1 << 30)
#define BRIDGE_CTRL_EN_CLK40		(0x1 << 29)
#define BRIDGE_CTRL_EN_CLK33		(0x1 << 28)
#define BRIDGE_CTRL_RST(n)		((n) << 24)
#define BRIDGE_CTRL_RST_MASK		(BRIDGE_CTRL_RST(0xF))
#define BRIDGE_CTRL_RST_PIN(x)		(BRIDGE_CTRL_RST(0x1 << (x)))
#define BRIDGE_CTRL_IO_SWAP		(0x1 << 23)
#define BRIDGE_CTRL_MEM_SWAP		(0x1 << 22)
#define BRIDGE_CTRL_PAGE_SIZE		(0x1 << 21)
#define BRIDGE_CTRL_SS_PAR_BAD		(0x1 << 20)
#define BRIDGE_CTRL_SS_PAR_EN		(0x1 << 19)
#define BRIDGE_CTRL_SSRAM_SIZE(n)	((n) << 17)
#define BRIDGE_CTRL_SSRAM_SIZE_MASK	(BRIDGE_CTRL_SSRAM_SIZE(0x3))
#define BRIDGE_CTRL_SSRAM_512K		(BRIDGE_CTRL_SSRAM_SIZE(0x3))
#define BRIDGE_CTRL_SSRAM_128K		(BRIDGE_CTRL_SSRAM_SIZE(0x2))
#define BRIDGE_CTRL_SSRAM_64K		(BRIDGE_CTRL_SSRAM_SIZE(0x1))
#define BRIDGE_CTRL_SSRAM_1K		(BRIDGE_CTRL_SSRAM_SIZE(0x0))
#define BRIDGE_CTRL_F_BAD_PKT		(0x1 << 16)
#define BRIDGE_CTRL_LLP_XBAR_CRD(n)	((n) << 12)
#define BRIDGE_CTRL_LLP_XBAR_CRD_MASK	(BRIDGE_CTRL_LLP_XBAR_CRD(0xf))
#define BRIDGE_CTRL_CLR_RLLP_CNT	(0x1 << 11)
#define BRIDGE_CTRL_CLR_TLLP_CNT	(0x1 << 10)
#define BRIDGE_CTRL_SYS_END		(0x1 << 9)
#define BRIDGE_CTRL_MAX_TRANS(n)	((n) << 4)
#define BRIDGE_CTRL_MAX_TRANS_MASK	(BRIDGE_CTRL_MAX_TRANS(0x1f))
#define BRIDGE_CTRL_WIDGET_ID(n)	((n) << 0)
#define BRIDGE_CTRL_WIDGET_ID_MASK	(BRIDGE_CTRL_WIDGET_ID(0xf))

/* Bridge Response buffer Error Upper Register bit fields definition */
#define BRIDGE_RESP_ERRUPPR_DEVNUM_SHFT (20)
#define BRIDGE_RESP_ERRUPPR_DEVNUM_MASK (0x7 << BRIDGE_RESP_ERRUPPR_DEVNUM_SHFT)
#define BRIDGE_RESP_ERRUPPR_BUFNUM_SHFT (16)
#define BRIDGE_RESP_ERRUPPR_BUFNUM_MASK (0xF << BRIDGE_RESP_ERRUPPR_BUFNUM_SHFT)
#define BRIDGE_RESP_ERRRUPPR_BUFMASK	(0xFFFF)

#define BRIDGE_RESP_ERRUPPR_BUFNUM(x)	\
			(((x) & BRIDGE_RESP_ERRUPPR_BUFNUM_MASK) >> \
				BRIDGE_RESP_ERRUPPR_BUFNUM_SHFT)

#define BRIDGE_RESP_ERRUPPR_DEVICE(x)	\
			(((x) &	 BRIDGE_RESP_ERRUPPR_DEVNUM_MASK) >> \
				 BRIDGE_RESP_ERRUPPR_DEVNUM_SHFT)

/* Bridge direct mapping register bits definition */
#define BRIDGE_DIRMAP_W_ID_SHFT		20
#define BRIDGE_DIRMAP_W_ID		(0xf << BRIDGE_DIRMAP_W_ID_SHFT)
#define BRIDGE_DIRMAP_RMF_64		(0x1 << 18)
#define BRIDGE_DIRMAP_ADD512		(0x1 << 17)
#define BRIDGE_DIRMAP_OFF		(0x1ffff << 0)
#define BRIDGE_DIRMAP_OFF_ADDRSHFT	(31)	/* lsbit of DIRMAP_OFF is xtalk address bit 31 */

/* Bridge Arbitration register bits definition */
#define BRIDGE_ARB_REQ_WAIT_TICK(x)	((x) << 16)
#define BRIDGE_ARB_REQ_WAIT_TICK_MASK	BRIDGE_ARB_REQ_WAIT_TICK(0x3)
#define BRIDGE_ARB_REQ_WAIT_EN(x)	((x) << 8)
#define BRIDGE_ARB_REQ_WAIT_EN_MASK	BRIDGE_ARB_REQ_WAIT_EN(0xff)
#define BRIDGE_ARB_FREEZE_GNT		(1 << 6)
#define BRIDGE_ARB_HPRI_RING_B2		(1 << 5)
#define BRIDGE_ARB_HPRI_RING_B1		(1 << 4)
#define BRIDGE_ARB_HPRI_RING_B0		(1 << 3)
#define BRIDGE_ARB_LPRI_RING_B2		(1 << 2)
#define BRIDGE_ARB_LPRI_RING_B1		(1 << 1)
#define BRIDGE_ARB_LPRI_RING_B0		(1 << 0)

/* Bridge Bus time-out register bits definition */
#define BRIDGE_BUS_PCI_RETRY_HLD(x)	((x) << 16)
#define BRIDGE_BUS_PCI_RETRY_HLD_MASK	BRIDGE_BUS_PCI_RETRY_HLD(0x1f)
#define BRIDGE_BUS_GIO_TIMEOUT		(1 << 12)
#define BRIDGE_BUS_PCI_RETRY_CNT(x)	((x) << 0)
#define BRIDGE_BUS_PCI_RETRY_MASK	BRIDGE_BUS_PCI_RETRY_CNT(0x3ff)

/* Bridge interrupt status register bits definition */
#define BRIDGE_ISR_MULTI_ERR		(0x1u << 31)
#define BRIDGE_ISR_PMU_ESIZE_FAULT	(0x1 << 30)
#define BRIDGE_ISR_UNEXP_RESP		(0x1 << 29)
#define BRIDGE_ISR_BAD_XRESP_PKT	(0x1 << 28)
#define BRIDGE_ISR_BAD_XREQ_PKT		(0x1 << 27)
#define BRIDGE_ISR_RESP_XTLK_ERR	(0x1 << 26)
#define BRIDGE_ISR_REQ_XTLK_ERR		(0x1 << 25)
#define BRIDGE_ISR_INVLD_ADDR		(0x1 << 24)
#define BRIDGE_ISR_UNSUPPORTED_XOP	(0x1 << 23)
#define BRIDGE_ISR_XREQ_FIFO_OFLOW	(0x1 << 22)
#define BRIDGE_ISR_LLP_REC_SNERR	(0x1 << 21)
#define BRIDGE_ISR_LLP_REC_CBERR	(0x1 << 20)
#define BRIDGE_ISR_LLP_RCTY		(0x1 << 19)
#define BRIDGE_ISR_LLP_TX_RETRY		(0x1 << 18)
#define BRIDGE_ISR_LLP_TCTY		(0x1 << 17)
#define BRIDGE_ISR_SSRAM_PERR		(0x1 << 16)
#define BRIDGE_ISR_PCI_ABORT		(0x1 << 15)
#define BRIDGE_ISR_PCI_PARITY		(0x1 << 14)
#define BRIDGE_ISR_PCI_SERR		(0x1 << 13)
#define BRIDGE_ISR_PCI_PERR		(0x1 << 12)
#define BRIDGE_ISR_PCI_MST_TIMEOUT	(0x1 << 11)
#define BRIDGE_ISR_GIO_MST_TIMEOUT	BRIDGE_ISR_PCI_MST_TIMEOUT
#define BRIDGE_ISR_PCI_RETRY_CNT	(0x1 << 10)
#define BRIDGE_ISR_XREAD_REQ_TIMEOUT	(0x1 << 9)
#define BRIDGE_ISR_GIO_B_ENBL_ERR	(0x1 << 8)
#define BRIDGE_ISR_INT_MSK		(0xff << 0)
#define BRIDGE_ISR_INT(x)		(0x1 << (x))

#define BRIDGE_ISR_LINK_ERROR		\
		(BRIDGE_ISR_LLP_REC_SNERR|BRIDGE_ISR_LLP_REC_CBERR|	\
		 BRIDGE_ISR_LLP_RCTY|BRIDGE_ISR_LLP_TX_RETRY|		\
		 BRIDGE_ISR_LLP_TCTY)

#define BRIDGE_ISR_PCIBUS_PIOERR	\
		(BRIDGE_ISR_PCI_MST_TIMEOUT|BRIDGE_ISR_PCI_ABORT)

#define BRIDGE_ISR_PCIBUS_ERROR		\
		(BRIDGE_ISR_PCIBUS_PIOERR|BRIDGE_ISR_PCI_PERR|		\
		 BRIDGE_ISR_PCI_SERR|BRIDGE_ISR_PCI_RETRY_CNT|		\
		 BRIDGE_ISR_PCI_PARITY)

#define BRIDGE_ISR_XTALK_ERROR		\
		(BRIDGE_ISR_XREAD_REQ_TIMEOUT|BRIDGE_ISR_XREQ_FIFO_OFLOW|\
		 BRIDGE_ISR_UNSUPPORTED_XOP|BRIDGE_ISR_INVLD_ADDR|	\
		 BRIDGE_ISR_REQ_XTLK_ERR|BRIDGE_ISR_RESP_XTLK_ERR|	\
		 BRIDGE_ISR_BAD_XREQ_PKT|BRIDGE_ISR_BAD_XRESP_PKT|	\
		 BRIDGE_ISR_UNEXP_RESP)

#define BRIDGE_ISR_ERRORS		\
		(BRIDGE_ISR_LINK_ERROR|BRIDGE_ISR_PCIBUS_ERROR|		\
		 BRIDGE_ISR_XTALK_ERROR|BRIDGE_ISR_SSRAM_PERR|		\
		 BRIDGE_ISR_PMU_ESIZE_FAULT)

/*
 * List of Errors which are fatal and kill the system
 */
#define BRIDGE_ISR_ERROR_FATAL		\
		((BRIDGE_ISR_XTALK_ERROR & ~BRIDGE_ISR_XREAD_REQ_TIMEOUT)|\
		 BRIDGE_ISR_PCI_SERR|BRIDGE_ISR_PCI_PARITY )

#define BRIDGE_ISR_ERROR_DUMP		\
		(BRIDGE_ISR_PCIBUS_ERROR|BRIDGE_ISR_PMU_ESIZE_FAULT|	\
		 BRIDGE_ISR_XTALK_ERROR|BRIDGE_ISR_SSRAM_PERR)

/* Bridge interrupt enable register bits definition */
#define BRIDGE_IMR_UNEXP_RESP		BRIDGE_ISR_UNEXP_RESP
#define BRIDGE_IMR_PMU_ESIZE_FAULT	BRIDGE_ISR_PMU_ESIZE_FAULT
#define BRIDGE_IMR_BAD_XRESP_PKT	BRIDGE_ISR_BAD_XRESP_PKT
#define BRIDGE_IMR_BAD_XREQ_PKT		BRIDGE_ISR_BAD_XREQ_PKT
#define BRIDGE_IMR_RESP_XTLK_ERR	BRIDGE_ISR_RESP_XTLK_ERR
#define BRIDGE_IMR_REQ_XTLK_ERR		BRIDGE_ISR_REQ_XTLK_ERR
#define BRIDGE_IMR_INVLD_ADDR		BRIDGE_ISR_INVLD_ADDR
#define BRIDGE_IMR_UNSUPPORTED_XOP	BRIDGE_ISR_UNSUPPORTED_XOP
#define BRIDGE_IMR_XREQ_FIFO_OFLOW	BRIDGE_ISR_XREQ_FIFO_OFLOW
#define BRIDGE_IMR_LLP_REC_SNERR	BRIDGE_ISR_LLP_REC_SNERR
#define BRIDGE_IMR_LLP_REC_CBERR	BRIDGE_ISR_LLP_REC_CBERR
#define BRIDGE_IMR_LLP_RCTY		BRIDGE_ISR_LLP_RCTY
#define BRIDGE_IMR_LLP_TX_RETRY		BRIDGE_ISR_LLP_TX_RETRY
#define BRIDGE_IMR_LLP_TCTY		BRIDGE_ISR_LLP_TCTY
#define BRIDGE_IMR_SSRAM_PERR		BRIDGE_ISR_SSRAM_PERR
#define BRIDGE_IMR_PCI_ABORT		BRIDGE_ISR_PCI_ABORT
#define BRIDGE_IMR_PCI_PARITY		BRIDGE_ISR_PCI_PARITY
#define BRIDGE_IMR_PCI_SERR		BRIDGE_ISR_PCI_SERR
#define BRIDGE_IMR_PCI_PERR		BRIDGE_ISR_PCI_PERR
#define BRIDGE_IMR_PCI_MST_TIMEOUT	BRIDGE_ISR_PCI_MST_TIMEOUT
#define BRIDGE_IMR_GIO_MST_TIMEOUT	BRIDGE_ISR_GIO_MST_TIMEOUT
#define BRIDGE_IMR_PCI_RETRY_CNT	BRIDGE_ISR_PCI_RETRY_CNT
#define BRIDGE_IMR_XREAD_REQ_TIMEOUT	BRIDGE_ISR_XREAD_REQ_TIMEOUT
#define BRIDGE_IMR_GIO_B_ENBL_ERR	BRIDGE_ISR_GIO_B_ENBL_ERR
#define BRIDGE_IMR_INT_MSK		BRIDGE_ISR_INT_MSK
#define BRIDGE_IMR_INT(x)		BRIDGE_ISR_INT(x)

/* Bridge interrupt reset register bits definition */
#define BRIDGE_IRR_MULTI_CLR		(0x1 << 6)
#define BRIDGE_IRR_CRP_GRP_CLR		(0x1 << 5)
#define BRIDGE_IRR_RESP_BUF_GRP_CLR	(0x1 << 4)
#define BRIDGE_IRR_REQ_DSP_GRP_CLR	(0x1 << 3)
#define BRIDGE_IRR_LLP_GRP_CLR		(0x1 << 2)
#define BRIDGE_IRR_SSRAM_GRP_CLR	(0x1 << 1)
#define BRIDGE_IRR_PCI_GRP_CLR		(0x1 << 0)
#define BRIDGE_IRR_GIO_GRP_CLR		(0x1 << 0)
#define BRIDGE_IRR_ALL_CLR		0x7f

#define BRIDGE_IRR_CRP_GRP		(BRIDGE_ISR_UNEXP_RESP | \
					 BRIDGE_ISR_XREQ_FIFO_OFLOW)
#define BRIDGE_IRR_RESP_BUF_GRP		(BRIDGE_ISR_BAD_XRESP_PKT | \
					 BRIDGE_ISR_RESP_XTLK_ERR | \
					 BRIDGE_ISR_XREAD_REQ_TIMEOUT)
#define BRIDGE_IRR_REQ_DSP_GRP		(BRIDGE_ISR_UNSUPPORTED_XOP | \
					 BRIDGE_ISR_BAD_XREQ_PKT | \
					 BRIDGE_ISR_REQ_XTLK_ERR | \
					 BRIDGE_ISR_INVLD_ADDR)
#define BRIDGE_IRR_LLP_GRP		(BRIDGE_ISR_LLP_REC_SNERR | \
					 BRIDGE_ISR_LLP_REC_CBERR | \
					 BRIDGE_ISR_LLP_RCTY | \
					 BRIDGE_ISR_LLP_TX_RETRY | \
					 BRIDGE_ISR_LLP_TCTY)
#define BRIDGE_IRR_SSRAM_GRP		(BRIDGE_ISR_SSRAM_PERR | \
					 BRIDGE_ISR_PMU_ESIZE_FAULT)
#define BRIDGE_IRR_PCI_GRP		(BRIDGE_ISR_PCI_ABORT | \
					 BRIDGE_ISR_PCI_PARITY | \
					 BRIDGE_ISR_PCI_SERR | \
					 BRIDGE_ISR_PCI_PERR | \
					 BRIDGE_ISR_PCI_MST_TIMEOUT | \
					 BRIDGE_ISR_PCI_RETRY_CNT)

#define BRIDGE_IRR_GIO_GRP		(BRIDGE_ISR_GIO_B_ENBL_ERR | \
					 BRIDGE_ISR_GIO_MST_TIMEOUT)

/* Bridge INT_DEV register bits definition */
#define BRIDGE_INT_DEV_SHFT(n)		((n)*3)
#define BRIDGE_INT_DEV_MASK(n)		(0x7 << BRIDGE_INT_DEV_SHFT(n))
#define BRIDGE_INT_DEV_SET(_dev, _line) (_dev << BRIDGE_INT_DEV_SHFT(_line))

/* Bridge interrupt(x) register bits definition */
#define BRIDGE_INT_ADDR_HOST		0x0003FF00
#define BRIDGE_INT_ADDR_FLD		0x000000FF

#define BRIDGE_TMO_PCI_RETRY_HLD_MASK	0x1f0000
#define BRIDGE_TMO_GIO_TIMEOUT_MASK	0x001000
#define BRIDGE_TMO_PCI_RETRY_CNT_MASK	0x0003ff

#define BRIDGE_TMO_PCI_RETRY_CNT_MAX	0x3ff

/*
 * The NASID should be shifted by this amount and stored into the
 * interrupt(x) register.
 */
#define BRIDGE_INT_ADDR_NASID_SHFT	8

/*
 * The BRIDGE_INT_ADDR_DEST_IO bit should be set to send an interrupt to
 * memory.
 */
#define BRIDGE_INT_ADDR_DEST_IO		(1 << 17)
#define BRIDGE_INT_ADDR_DEST_MEM	0
#define BRIDGE_INT_ADDR_MASK		(1 << 17)

/* Bridge device(x) register bits definition */
#define BRIDGE_DEV_ERR_LOCK_EN		0x10000000
#define BRIDGE_DEV_PAGE_CHK_DIS		0x08000000
#define BRIDGE_DEV_FORCE_PCI_PAR	0x04000000
#define BRIDGE_DEV_VIRTUAL_EN		0x02000000
#define BRIDGE_DEV_PMU_WRGA_EN		0x01000000
#define BRIDGE_DEV_DIR_WRGA_EN		0x00800000
#define BRIDGE_DEV_DEV_SIZE		0x00400000
#define BRIDGE_DEV_RT			0x00200000
#define BRIDGE_DEV_SWAP_PMU		0x00100000
#define BRIDGE_DEV_SWAP_DIR		0x00080000
#define BRIDGE_DEV_PREF			0x00040000
#define BRIDGE_DEV_PRECISE		0x00020000
#define BRIDGE_DEV_COH			0x00010000
#define BRIDGE_DEV_BARRIER		0x00008000
#define BRIDGE_DEV_GBR			0x00004000
#define BRIDGE_DEV_DEV_SWAP		0x00002000
#define BRIDGE_DEV_DEV_IO_MEM		0x00001000
#define BRIDGE_DEV_OFF_MASK		0x00000fff
#define BRIDGE_DEV_OFF_ADDR_SHFT	20

#define BRIDGE_DEV_PMU_BITS		(BRIDGE_DEV_PMU_WRGA_EN		| \
					 BRIDGE_DEV_SWAP_PMU)
#define BRIDGE_DEV_D32_BITS		(BRIDGE_DEV_DIR_WRGA_EN		| \
					 BRIDGE_DEV_SWAP_DIR		| \
					 BRIDGE_DEV_PREF		| \
					 BRIDGE_DEV_PRECISE		| \
					 BRIDGE_DEV_COH			| \
					 BRIDGE_DEV_BARRIER)
#define BRIDGE_DEV_D64_BITS		(BRIDGE_DEV_DIR_WRGA_EN		| \
					 BRIDGE_DEV_SWAP_DIR		| \
					 BRIDGE_DEV_COH			| \
					 BRIDGE_DEV_BARRIER)

/* Bridge Error Upper register bit field definition */
#define BRIDGE_ERRUPPR_DEVMASTER	(0x1 << 20)	/* Device was master */
#define BRIDGE_ERRUPPR_PCIVDEV		(0x1 << 19)	/* Virtual Req value */
#define BRIDGE_ERRUPPR_DEVNUM_SHFT	(16)
#define BRIDGE_ERRUPPR_DEVNUM_MASK	(0x7 << BRIDGE_ERRUPPR_DEVNUM_SHFT)
#define BRIDGE_ERRUPPR_DEVICE(err)	(((err) >> BRIDGE_ERRUPPR_DEVNUM_SHFT) & 0x7)
#define BRIDGE_ERRUPPR_ADDRMASK		(0xFFFF)

/* Bridge interrupt mode register bits definition */
#define BRIDGE_INTMODE_CLR_PKT_EN(x)	(0x1 << (x))

/* this should be written to the xbow's link_control(x) register */
#define BRIDGE_CREDIT	3

/* RRB assignment register */
#define BRIDGE_RRB_EN	0x8	/* after shifting down */
#define BRIDGE_RRB_DEV	0x7	/* after shifting down */
#define BRIDGE_RRB_VDEV 0x4	/* after shifting down */
#define BRIDGE_RRB_PDEV 0x3	/* after shifting down */

/* RRB status register */
#define BRIDGE_RRB_VALID(r)	(0x00010000<<(r))
#define BRIDGE_RRB_INUSE(r)	(0x00000001<<(r))

/* RRB clear register */
#define BRIDGE_RRB_CLEAR(r)	(0x00000001<<(r))

/* xbox system controller declarations */
#define XBOX_BRIDGE_WID		8
#define FLASH_PROM1_BASE	0xE00000 /* To read the xbox sysctlr status */
#define XBOX_RPS_EXISTS		1 << 6	 /* RPS bit in status register */
#define XBOX_RPS_FAIL		1 << 4	 /* RPS status bit in register */

/* ========================================================================
 */
/*
 * Macros for Xtalk to Bridge bus (PCI/GIO) PIO
 * refer to section 4.2.1 of Bridge Spec for xtalk to PCI/GIO PIO mappings
 */
/* XTALK addresses that map into Bridge Bus addr space */
#define BRIDGE_PIO32_XTALK_ALIAS_BASE	0x000040000000L
#define BRIDGE_PIO32_XTALK_ALIAS_LIMIT	0x00007FFFFFFFL
#define BRIDGE_PIO64_XTALK_ALIAS_BASE	0x000080000000L
#define BRIDGE_PIO64_XTALK_ALIAS_LIMIT	0x0000BFFFFFFFL
#define BRIDGE_PCIIO_XTALK_ALIAS_BASE	0x000100000000L
#define BRIDGE_PCIIO_XTALK_ALIAS_LIMIT	0x0001FFFFFFFFL

/* Ranges of PCI bus space that can be accessed via PIO from xtalk */
#define BRIDGE_MIN_PIO_ADDR_MEM		0x00000000	/* 1G PCI memory space */
#define BRIDGE_MAX_PIO_ADDR_MEM		0x3fffffff
#define BRIDGE_MIN_PIO_ADDR_IO		0x00000000	/* 4G PCI IO space */
#define BRIDGE_MAX_PIO_ADDR_IO		0xffffffff

/* XTALK addresses that map into PCI addresses */
#define BRIDGE_PCI_MEM32_BASE		BRIDGE_PIO32_XTALK_ALIAS_BASE
#define BRIDGE_PCI_MEM32_LIMIT		BRIDGE_PIO32_XTALK_ALIAS_LIMIT
#define BRIDGE_PCI_MEM64_BASE		BRIDGE_PIO64_XTALK_ALIAS_BASE
#define BRIDGE_PCI_MEM64_LIMIT		BRIDGE_PIO64_XTALK_ALIAS_LIMIT
#define BRIDGE_PCI_IO_BASE		BRIDGE_PCIIO_XTALK_ALIAS_BASE
#define BRIDGE_PCI_IO_LIMIT		BRIDGE_PCIIO_XTALK_ALIAS_LIMIT

/*
 * Macros for Bridge bus (PCI/GIO) to Xtalk DMA
 */
/* Bridge Bus DMA addresses */
#define BRIDGE_LOCAL_BASE		0
#define BRIDGE_DMA_MAPPED_BASE		0x40000000
#define BRIDGE_DMA_MAPPED_SIZE		0x40000000	/* 1G Bytes */
#define BRIDGE_DMA_DIRECT_BASE		0x80000000
#define BRIDGE_DMA_DIRECT_SIZE		0x80000000	/* 2G Bytes */

#define PCI32_LOCAL_BASE		BRIDGE_LOCAL_BASE

/* PCI addresses of regions decoded by Bridge for DMA */
#define PCI32_MAPPED_BASE		BRIDGE_DMA_MAPPED_BASE
#define PCI32_DIRECT_BASE		BRIDGE_DMA_DIRECT_BASE

#define IS_PCI32_LOCAL(x)	((ulong_t)(x) < PCI32_MAPPED_BASE)
#define IS_PCI32_MAPPED(x)	((ulong_t)(x) < PCI32_DIRECT_BASE && \
					(ulong_t)(x) >= PCI32_MAPPED_BASE)
#define IS_PCI32_DIRECT(x)	((ulong_t)(x) >= PCI32_MAPPED_BASE)
#define IS_PCI64(x)		((ulong_t)(x) >= PCI64_BASE)

/*
 * The GIO address space.
 */
/* Xtalk to GIO PIO */
#define BRIDGE_GIO_MEM32_BASE		BRIDGE_PIO32_XTALK_ALIAS_BASE
#define BRIDGE_GIO_MEM32_LIMIT		BRIDGE_PIO32_XTALK_ALIAS_LIMIT

#define GIO_LOCAL_BASE			BRIDGE_LOCAL_BASE

/* GIO addresses of regions decoded by Bridge for DMA */
#define GIO_MAPPED_BASE			BRIDGE_DMA_MAPPED_BASE
#define GIO_DIRECT_BASE			BRIDGE_DMA_DIRECT_BASE

#define IS_GIO_LOCAL(x)		((ulong_t)(x) < GIO_MAPPED_BASE)
#define IS_GIO_MAPPED(x)	((ulong_t)(x) < GIO_DIRECT_BASE && \
					(ulong_t)(x) >= GIO_MAPPED_BASE)
#define IS_GIO_DIRECT(x)	((ulong_t)(x) >= GIO_MAPPED_BASE)

/* PCI to xtalk mapping */

/* given a DIR_OFF value and a pci/gio 32 bits direct address, determine
 * which xtalk address is accessed
 */
#define BRIDGE_DIRECT_32_SEG_SIZE	BRIDGE_DMA_DIRECT_SIZE
#define BRIDGE_DIRECT_32_TO_XTALK(dir_off,adr)		\
	((dir_off) * BRIDGE_DIRECT_32_SEG_SIZE +	\
		((adr) & (BRIDGE_DIRECT_32_SEG_SIZE - 1)) + PHYS_RAMBASE)

/* 64-bit address attribute masks */
#define PCI64_ATTR_TARG_MASK	0xf000000000000000
#define PCI64_ATTR_TARG_SHFT	60
#define PCI64_ATTR_PREF		0x0800000000000000
#define PCI64_ATTR_PREC		0x0400000000000000
#define PCI64_ATTR_VIRTUAL	0x0200000000000000
#define PCI64_ATTR_BAR		0x0100000000000000
#define PCI64_ATTR_RMF_MASK	0x00ff000000000000
#define PCI64_ATTR_RMF_SHFT	48

struct bridge_controller {
	struct resource		busn;
	struct bridge_regs	*base;
	unsigned long		baddr;
	unsigned long		intr_addr;
	struct irq_domain	*domain;
	unsigned int		pci_int[8];
	u32			ioc3_sid[8];
	nasid_t			nasid;
};

#define BRIDGE_CONTROLLER(bus) \
	((struct bridge_controller *)((bus)->sysdata))

#define bridge_read(bc, reg)		__raw_readl(&bc->base->reg)
#define bridge_write(bc, reg, val)	__raw_writel(val, &bc->base->reg)
#define bridge_set(bc, reg, val)	\
	__raw_writel(__raw_readl(&bc->base->reg) | (val), &bc->base->reg)
#define bridge_clr(bc, reg, val)	\
	__raw_writel(__raw_readl(&bc->base->reg) & ~(val), &bc->base->reg)

#endif /* _ASM_PCI_BRIDGE_H */
