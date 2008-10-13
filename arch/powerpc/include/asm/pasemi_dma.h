/*
 * Copyright (C) 2006-2008 PA Semi, Inc
 *
 * Hardware register layout and descriptor formats for the on-board
 * DMA engine on PA Semi PWRficient. Used by ethernet, function and security
 * drivers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef ASM_PASEMI_DMA_H
#define ASM_PASEMI_DMA_H

/* status register layout in IOB region, at 0xfb800000 */
struct pasdma_status {
	u64 rx_sta[64];		/* RX channel status */
	u64 tx_sta[20];		/* TX channel status */
};


/* All these registers live in the PCI configuration space for the DMA PCI
 * device. Use the normal PCI config access functions for them.
 */
enum {
	PAS_DMA_CAP_TXCH  = 0x44,	/* Transmit Channel Info      */
	PAS_DMA_CAP_RXCH  = 0x48,	/* Transmit Channel Info      */
	PAS_DMA_CAP_IFI	  = 0x4c,	/* Interface Info	      */
	PAS_DMA_COM_TXCMD = 0x100,	/* Transmit Command Register  */
	PAS_DMA_COM_TXSTA = 0x104,	/* Transmit Status Register   */
	PAS_DMA_COM_RXCMD = 0x108,	/* Receive Command Register   */
	PAS_DMA_COM_RXSTA = 0x10c,	/* Receive Status Register    */
	PAS_DMA_COM_CFG   = 0x114,	/* Common config reg	      */
	PAS_DMA_TXF_SFLG0 = 0x140,	/* Set flags                  */
	PAS_DMA_TXF_SFLG1 = 0x144,	/* Set flags                  */
	PAS_DMA_TXF_CFLG0 = 0x148,	/* Set flags                  */
	PAS_DMA_TXF_CFLG1 = 0x14c,	/* Set flags                  */
};


#define PAS_DMA_CAP_TXCH_TCHN_M	0x00ff0000 /* # of TX channels */
#define PAS_DMA_CAP_TXCH_TCHN_S	16

#define PAS_DMA_CAP_RXCH_RCHN_M	0x00ff0000 /* # of RX channels */
#define PAS_DMA_CAP_RXCH_RCHN_S	16

#define PAS_DMA_CAP_IFI_IOFF_M	0xff000000 /* Cfg reg for intf pointers */
#define PAS_DMA_CAP_IFI_IOFF_S	24
#define PAS_DMA_CAP_IFI_NIN_M	0x00ff0000 /* # of interfaces */
#define PAS_DMA_CAP_IFI_NIN_S	16

#define PAS_DMA_COM_TXCMD_EN	0x00000001 /* enable */
#define PAS_DMA_COM_TXSTA_ACT	0x00000001 /* active */
#define PAS_DMA_COM_RXCMD_EN	0x00000001 /* enable */
#define PAS_DMA_COM_RXSTA_ACT	0x00000001 /* active */


/* Per-interface and per-channel registers */
#define _PAS_DMA_RXINT_STRIDE		0x20
#define PAS_DMA_RXINT_RCMDSTA(i)	(0x200+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_RCMDSTA_EN	0x00000001
#define    PAS_DMA_RXINT_RCMDSTA_ST	0x00000002
#define    PAS_DMA_RXINT_RCMDSTA_MBT	0x00000008
#define    PAS_DMA_RXINT_RCMDSTA_MDR	0x00000010
#define    PAS_DMA_RXINT_RCMDSTA_MOO	0x00000020
#define    PAS_DMA_RXINT_RCMDSTA_MBP	0x00000040
#define    PAS_DMA_RXINT_RCMDSTA_BT	0x00000800
#define    PAS_DMA_RXINT_RCMDSTA_DR	0x00001000
#define    PAS_DMA_RXINT_RCMDSTA_OO	0x00002000
#define    PAS_DMA_RXINT_RCMDSTA_BP	0x00004000
#define    PAS_DMA_RXINT_RCMDSTA_TB	0x00008000
#define    PAS_DMA_RXINT_RCMDSTA_ACT	0x00010000
#define    PAS_DMA_RXINT_RCMDSTA_DROPS_M	0xfffe0000
#define    PAS_DMA_RXINT_RCMDSTA_DROPS_S	17
#define PAS_DMA_RXINT_CFG(i)		(0x204+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_CFG_RBP	0x80000000
#define    PAS_DMA_RXINT_CFG_ITRR	0x40000000
#define    PAS_DMA_RXINT_CFG_DHL_M	0x07000000
#define    PAS_DMA_RXINT_CFG_DHL_S	24
#define    PAS_DMA_RXINT_CFG_DHL(x)	(((x) << PAS_DMA_RXINT_CFG_DHL_S) & \
					 PAS_DMA_RXINT_CFG_DHL_M)
#define    PAS_DMA_RXINT_CFG_ITR	0x00400000
#define    PAS_DMA_RXINT_CFG_LW		0x00200000
#define    PAS_DMA_RXINT_CFG_L2		0x00100000
#define    PAS_DMA_RXINT_CFG_HEN	0x00080000
#define    PAS_DMA_RXINT_CFG_WIF	0x00000002
#define    PAS_DMA_RXINT_CFG_WIL	0x00000001

#define PAS_DMA_RXINT_INCR(i)		(0x210+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_INCR_INCR_M	0x0000ffff
#define    PAS_DMA_RXINT_INCR_INCR_S	0
#define    PAS_DMA_RXINT_INCR_INCR(x)	((x) & 0x0000ffff)
#define PAS_DMA_RXINT_BASEL(i)		(0x218+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_BASEL_BRBL(x)	((x) & ~0x3f)
#define PAS_DMA_RXINT_BASEU(i)		(0x21c+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_BASEU_BRBH(x)	((x) & 0xfff)
#define    PAS_DMA_RXINT_BASEU_SIZ_M	0x3fff0000	/* # of cache lines worth of buffer ring */
#define    PAS_DMA_RXINT_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_RXINT_BASEU_SIZ(x)	(((x) << PAS_DMA_RXINT_BASEU_SIZ_S) & \
					 PAS_DMA_RXINT_BASEU_SIZ_M)


#define _PAS_DMA_TXCHAN_STRIDE	0x20    /* Size per channel		*/
#define _PAS_DMA_TXCHAN_TCMDSTA	0x300	/* Command / Status		*/
#define _PAS_DMA_TXCHAN_CFG	0x304	/* Configuration		*/
#define _PAS_DMA_TXCHAN_DSCRBU	0x308	/* Descriptor BU Allocation	*/
#define _PAS_DMA_TXCHAN_INCR	0x310	/* Descriptor increment		*/
#define _PAS_DMA_TXCHAN_CNT	0x314	/* Descriptor count/offset	*/
#define _PAS_DMA_TXCHAN_BASEL	0x318	/* Descriptor ring base (low)	*/
#define _PAS_DMA_TXCHAN_BASEU	0x31c	/*			(high)	*/
#define PAS_DMA_TXCHAN_TCMDSTA(c) (0x300+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_TCMDSTA_EN	0x00000001	/* Enabled */
#define    PAS_DMA_TXCHAN_TCMDSTA_ST	0x00000002	/* Stop interface */
#define    PAS_DMA_TXCHAN_TCMDSTA_ACT	0x00010000	/* Active */
#define    PAS_DMA_TXCHAN_TCMDSTA_SZ	0x00000800
#define    PAS_DMA_TXCHAN_TCMDSTA_DB	0x00000400
#define    PAS_DMA_TXCHAN_TCMDSTA_DE	0x00000200
#define    PAS_DMA_TXCHAN_TCMDSTA_DA	0x00000100
#define PAS_DMA_TXCHAN_CFG(c)     (0x304+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_CFG_TY_IFACE	0x00000000	/* Type = interface */
#define    PAS_DMA_TXCHAN_CFG_TY_COPY	0x00000001	/* Type = copy only */
#define    PAS_DMA_TXCHAN_CFG_TY_FUNC	0x00000002	/* Type = function */
#define    PAS_DMA_TXCHAN_CFG_TY_XOR	0x00000003	/* Type = xor only */
#define    PAS_DMA_TXCHAN_CFG_TATTR_M	0x0000003c
#define    PAS_DMA_TXCHAN_CFG_TATTR_S	2
#define    PAS_DMA_TXCHAN_CFG_TATTR(x)	(((x) << PAS_DMA_TXCHAN_CFG_TATTR_S) & \
					 PAS_DMA_TXCHAN_CFG_TATTR_M)
#define    PAS_DMA_TXCHAN_CFG_LPDQ	0x00000800
#define    PAS_DMA_TXCHAN_CFG_LPSQ	0x00000400
#define    PAS_DMA_TXCHAN_CFG_WT_M	0x000003c0
#define    PAS_DMA_TXCHAN_CFG_WT_S	6
#define    PAS_DMA_TXCHAN_CFG_WT(x)	(((x) << PAS_DMA_TXCHAN_CFG_WT_S) & \
					 PAS_DMA_TXCHAN_CFG_WT_M)
#define    PAS_DMA_TXCHAN_CFG_TRD	0x00010000	/* translate data */
#define    PAS_DMA_TXCHAN_CFG_TRR	0x00008000	/* translate rings */
#define    PAS_DMA_TXCHAN_CFG_UP	0x00004000	/* update tx descr when sent */
#define    PAS_DMA_TXCHAN_CFG_CL	0x00002000	/* Clean last line */
#define    PAS_DMA_TXCHAN_CFG_CF	0x00001000	/* Clean first line */
#define PAS_DMA_TXCHAN_INCR(c)    (0x310+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define PAS_DMA_TXCHAN_BASEL(c)   (0x318+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_BASEL_BRBL_M	0xffffffc0
#define    PAS_DMA_TXCHAN_BASEL_BRBL_S	0
#define    PAS_DMA_TXCHAN_BASEL_BRBL(x)	(((x) << PAS_DMA_TXCHAN_BASEL_BRBL_S) & \
					 PAS_DMA_TXCHAN_BASEL_BRBL_M)
#define PAS_DMA_TXCHAN_BASEU(c)   (0x31c+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_BASEU_BRBH_M	0x00000fff
#define    PAS_DMA_TXCHAN_BASEU_BRBH_S	0
#define    PAS_DMA_TXCHAN_BASEU_BRBH(x)	(((x) << PAS_DMA_TXCHAN_BASEU_BRBH_S) & \
					 PAS_DMA_TXCHAN_BASEU_BRBH_M)
/* # of cache lines worth of buffer ring */
#define    PAS_DMA_TXCHAN_BASEU_SIZ_M	0x3fff0000
#define    PAS_DMA_TXCHAN_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_TXCHAN_BASEU_SIZ(x)	(((x) << PAS_DMA_TXCHAN_BASEU_SIZ_S) & \
					 PAS_DMA_TXCHAN_BASEU_SIZ_M)

#define _PAS_DMA_RXCHAN_STRIDE	0x20    /* Size per channel		*/
#define _PAS_DMA_RXCHAN_CCMDSTA	0x800	/* Command / Status		*/
#define _PAS_DMA_RXCHAN_CFG	0x804	/* Configuration		*/
#define _PAS_DMA_RXCHAN_INCR	0x810	/* Descriptor increment		*/
#define _PAS_DMA_RXCHAN_CNT	0x814	/* Descriptor count/offset	*/
#define _PAS_DMA_RXCHAN_BASEL	0x818	/* Descriptor ring base (low)	*/
#define _PAS_DMA_RXCHAN_BASEU	0x81c	/*			(high)	*/
#define PAS_DMA_RXCHAN_CCMDSTA(c) (0x800+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_CCMDSTA_EN	0x00000001	/* Enabled */
#define    PAS_DMA_RXCHAN_CCMDSTA_ST	0x00000002	/* Stop interface */
#define    PAS_DMA_RXCHAN_CCMDSTA_ACT	0x00010000	/* Active */
#define    PAS_DMA_RXCHAN_CCMDSTA_DU	0x00020000
#define    PAS_DMA_RXCHAN_CCMDSTA_OD	0x00002000
#define    PAS_DMA_RXCHAN_CCMDSTA_FD	0x00001000
#define    PAS_DMA_RXCHAN_CCMDSTA_DT	0x00000800
#define PAS_DMA_RXCHAN_CFG(c)     (0x804+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_CFG_CTR	0x00000400
#define    PAS_DMA_RXCHAN_CFG_HBU_M	0x00000380
#define    PAS_DMA_RXCHAN_CFG_HBU_S	7
#define    PAS_DMA_RXCHAN_CFG_HBU(x)	(((x) << PAS_DMA_RXCHAN_CFG_HBU_S) & \
					 PAS_DMA_RXCHAN_CFG_HBU_M)
#define PAS_DMA_RXCHAN_INCR(c)    (0x810+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define PAS_DMA_RXCHAN_BASEL(c)   (0x818+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_BASEL_BRBL_M	0xffffffc0
#define    PAS_DMA_RXCHAN_BASEL_BRBL_S	0
#define    PAS_DMA_RXCHAN_BASEL_BRBL(x)	(((x) << PAS_DMA_RXCHAN_BASEL_BRBL_S) & \
					 PAS_DMA_RXCHAN_BASEL_BRBL_M)
#define PAS_DMA_RXCHAN_BASEU(c)   (0x81c+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_BASEU_BRBH_M	0x00000fff
#define    PAS_DMA_RXCHAN_BASEU_BRBH_S	0
#define    PAS_DMA_RXCHAN_BASEU_BRBH(x)	(((x) << PAS_DMA_RXCHAN_BASEU_BRBH_S) & \
					 PAS_DMA_RXCHAN_BASEU_BRBH_M)
/* # of cache lines worth of buffer ring */
#define    PAS_DMA_RXCHAN_BASEU_SIZ_M	0x3fff0000
#define    PAS_DMA_RXCHAN_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_RXCHAN_BASEU_SIZ(x)	(((x) << PAS_DMA_RXCHAN_BASEU_SIZ_S) & \
					 PAS_DMA_RXCHAN_BASEU_SIZ_M)

#define    PAS_STATUS_PCNT_M		0x000000000000ffffull
#define    PAS_STATUS_PCNT_S		0
#define    PAS_STATUS_DCNT_M		0x00000000ffff0000ull
#define    PAS_STATUS_DCNT_S		16
#define    PAS_STATUS_BPCNT_M		0x0000ffff00000000ull
#define    PAS_STATUS_BPCNT_S		32
#define    PAS_STATUS_CAUSE_M		0xf000000000000000ull
#define    PAS_STATUS_TIMER		0x1000000000000000ull
#define    PAS_STATUS_ERROR		0x2000000000000000ull
#define    PAS_STATUS_SOFT		0x4000000000000000ull
#define    PAS_STATUS_INT		0x8000000000000000ull

#define PAS_IOB_COM_PKTHDRCNT		0x120
#define    PAS_IOB_COM_PKTHDRCNT_PKTHDR1_M	0x0fff0000
#define    PAS_IOB_COM_PKTHDRCNT_PKTHDR1_S	16
#define    PAS_IOB_COM_PKTHDRCNT_PKTHDR0_M	0x00000fff
#define    PAS_IOB_COM_PKTHDRCNT_PKTHDR0_S	0

#define PAS_IOB_DMA_RXCH_CFG(i)		(0x1100 + (i)*4)
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH_M		0x00000fff
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH_S		0
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH(x)	(((x) << PAS_IOB_DMA_RXCH_CFG_CNTTH_S) & \
						 PAS_IOB_DMA_RXCH_CFG_CNTTH_M)
#define PAS_IOB_DMA_TXCH_CFG(i)		(0x1200 + (i)*4)
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH_M		0x00000fff
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH_S		0
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH(x)	(((x) << PAS_IOB_DMA_TXCH_CFG_CNTTH_S) & \
						 PAS_IOB_DMA_TXCH_CFG_CNTTH_M)
#define PAS_IOB_DMA_RXCH_STAT(i)	(0x1300 + (i)*4)
#define    PAS_IOB_DMA_RXCH_STAT_INTGEN	0x00001000
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL_M	0x00000fff
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL_S	0
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL(x)	(((x) << PAS_IOB_DMA_RXCH_STAT_CNTDEL_S) &\
						 PAS_IOB_DMA_RXCH_STAT_CNTDEL_M)
#define PAS_IOB_DMA_TXCH_STAT(i)	(0x1400 + (i)*4)
#define    PAS_IOB_DMA_TXCH_STAT_INTGEN	0x00001000
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL_M	0x00000fff
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL_S	0
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL(x)	(((x) << PAS_IOB_DMA_TXCH_STAT_CNTDEL_S) &\
						 PAS_IOB_DMA_TXCH_STAT_CNTDEL_M)
#define PAS_IOB_DMA_RXCH_RESET(i)	(0x1500 + (i)*4)
#define    PAS_IOB_DMA_RXCH_RESET_PCNT_M	0xffff0000
#define    PAS_IOB_DMA_RXCH_RESET_PCNT_S	16
#define    PAS_IOB_DMA_RXCH_RESET_PCNT(x)	(((x) << PAS_IOB_DMA_RXCH_RESET_PCNT_S) & \
						 PAS_IOB_DMA_RXCH_RESET_PCNT_M)
#define    PAS_IOB_DMA_RXCH_RESET_PCNTRST	0x00000020
#define    PAS_IOB_DMA_RXCH_RESET_DCNTRST	0x00000010
#define    PAS_IOB_DMA_RXCH_RESET_TINTC		0x00000008
#define    PAS_IOB_DMA_RXCH_RESET_DINTC		0x00000004
#define    PAS_IOB_DMA_RXCH_RESET_SINTC		0x00000002
#define    PAS_IOB_DMA_RXCH_RESET_PINTC		0x00000001
#define PAS_IOB_DMA_TXCH_RESET(i)	(0x1600 + (i)*4)
#define    PAS_IOB_DMA_TXCH_RESET_PCNT_M	0xffff0000
#define    PAS_IOB_DMA_TXCH_RESET_PCNT_S	16
#define    PAS_IOB_DMA_TXCH_RESET_PCNT(x)	(((x) << PAS_IOB_DMA_TXCH_RESET_PCNT_S) & \
						 PAS_IOB_DMA_TXCH_RESET_PCNT_M)
#define    PAS_IOB_DMA_TXCH_RESET_PCNTRST	0x00000020
#define    PAS_IOB_DMA_TXCH_RESET_DCNTRST	0x00000010
#define    PAS_IOB_DMA_TXCH_RESET_TINTC		0x00000008
#define    PAS_IOB_DMA_TXCH_RESET_DINTC		0x00000004
#define    PAS_IOB_DMA_TXCH_RESET_SINTC		0x00000002
#define    PAS_IOB_DMA_TXCH_RESET_PINTC		0x00000001

#define PAS_IOB_DMA_COM_TIMEOUTCFG		0x1700
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_M	0x00ffffff
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_S	0
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(x)	(((x) << PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_S) & \
						 PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_M)

/* Transmit descriptor fields */
#define	XCT_MACTX_T		0x8000000000000000ull
#define	XCT_MACTX_ST		0x4000000000000000ull
#define XCT_MACTX_NORES		0x0000000000000000ull
#define XCT_MACTX_8BRES		0x1000000000000000ull
#define XCT_MACTX_24BRES	0x2000000000000000ull
#define XCT_MACTX_40BRES	0x3000000000000000ull
#define XCT_MACTX_I		0x0800000000000000ull
#define XCT_MACTX_O		0x0400000000000000ull
#define XCT_MACTX_E		0x0200000000000000ull
#define XCT_MACTX_VLAN_M	0x0180000000000000ull
#define XCT_MACTX_VLAN_NOP	0x0000000000000000ull
#define XCT_MACTX_VLAN_REMOVE	0x0080000000000000ull
#define XCT_MACTX_VLAN_INSERT   0x0100000000000000ull
#define XCT_MACTX_VLAN_REPLACE  0x0180000000000000ull
#define XCT_MACTX_CRC_M		0x0060000000000000ull
#define XCT_MACTX_CRC_NOP	0x0000000000000000ull
#define XCT_MACTX_CRC_INSERT	0x0020000000000000ull
#define XCT_MACTX_CRC_PAD	0x0040000000000000ull
#define XCT_MACTX_CRC_REPLACE	0x0060000000000000ull
#define XCT_MACTX_SS		0x0010000000000000ull
#define XCT_MACTX_LLEN_M	0x00007fff00000000ull
#define XCT_MACTX_LLEN_S	32ull
#define XCT_MACTX_LLEN(x)	((((long)(x)) << XCT_MACTX_LLEN_S) & \
				 XCT_MACTX_LLEN_M)
#define XCT_MACTX_IPH_M		0x00000000f8000000ull
#define XCT_MACTX_IPH_S		27ull
#define XCT_MACTX_IPH(x)	((((long)(x)) << XCT_MACTX_IPH_S) & \
				 XCT_MACTX_IPH_M)
#define XCT_MACTX_IPO_M		0x0000000007c00000ull
#define XCT_MACTX_IPO_S		22ull
#define XCT_MACTX_IPO(x)	((((long)(x)) << XCT_MACTX_IPO_S) & \
				 XCT_MACTX_IPO_M)
#define XCT_MACTX_CSUM_M	0x0000000000000060ull
#define XCT_MACTX_CSUM_NOP	0x0000000000000000ull
#define XCT_MACTX_CSUM_TCP	0x0000000000000040ull
#define XCT_MACTX_CSUM_UDP	0x0000000000000060ull
#define XCT_MACTX_V6		0x0000000000000010ull
#define XCT_MACTX_C		0x0000000000000004ull
#define XCT_MACTX_AL2		0x0000000000000002ull

/* Receive descriptor fields */
#define	XCT_MACRX_T		0x8000000000000000ull
#define	XCT_MACRX_ST		0x4000000000000000ull
#define XCT_MACRX_RR_M		0x3000000000000000ull
#define XCT_MACRX_RR_NORES	0x0000000000000000ull
#define XCT_MACRX_RR_8BRES	0x1000000000000000ull
#define XCT_MACRX_O		0x0400000000000000ull
#define XCT_MACRX_E		0x0200000000000000ull
#define XCT_MACRX_FF		0x0100000000000000ull
#define XCT_MACRX_PF		0x0080000000000000ull
#define XCT_MACRX_OB		0x0040000000000000ull
#define XCT_MACRX_OD		0x0020000000000000ull
#define XCT_MACRX_FS		0x0010000000000000ull
#define XCT_MACRX_NB_M		0x000fc00000000000ull
#define XCT_MACRX_NB_S		46ULL
#define XCT_MACRX_NB(x)		((((long)(x)) << XCT_MACRX_NB_S) & \
				 XCT_MACRX_NB_M)
#define XCT_MACRX_LLEN_M	0x00003fff00000000ull
#define XCT_MACRX_LLEN_S	32ULL
#define XCT_MACRX_LLEN(x)	((((long)(x)) << XCT_MACRX_LLEN_S) & \
				 XCT_MACRX_LLEN_M)
#define XCT_MACRX_CRC		0x0000000080000000ull
#define XCT_MACRX_LEN_M		0x0000000060000000ull
#define XCT_MACRX_LEN_TOOSHORT	0x0000000020000000ull
#define XCT_MACRX_LEN_BELOWMIN	0x0000000040000000ull
#define XCT_MACRX_LEN_TRUNC	0x0000000060000000ull
#define XCT_MACRX_CAST_M	0x0000000018000000ull
#define XCT_MACRX_CAST_UNI	0x0000000000000000ull
#define XCT_MACRX_CAST_MULTI	0x0000000008000000ull
#define XCT_MACRX_CAST_BROAD	0x0000000010000000ull
#define XCT_MACRX_CAST_PAUSE	0x0000000018000000ull
#define XCT_MACRX_VLC_M		0x0000000006000000ull
#define XCT_MACRX_FM		0x0000000001000000ull
#define XCT_MACRX_HTY_M		0x0000000000c00000ull
#define XCT_MACRX_HTY_IPV4_OK	0x0000000000000000ull
#define XCT_MACRX_HTY_IPV6 	0x0000000000400000ull
#define XCT_MACRX_HTY_IPV4_BAD	0x0000000000800000ull
#define XCT_MACRX_HTY_NONIP	0x0000000000c00000ull
#define XCT_MACRX_IPP_M		0x00000000003f0000ull
#define XCT_MACRX_IPP_S		16
#define XCT_MACRX_CSUM_M	0x000000000000ffffull
#define XCT_MACRX_CSUM_S	0

#define XCT_PTR_T		0x8000000000000000ull
#define XCT_PTR_LEN_M		0x7ffff00000000000ull
#define XCT_PTR_LEN_S		44
#define XCT_PTR_LEN(x)		((((long)(x)) << XCT_PTR_LEN_S) & \
				 XCT_PTR_LEN_M)
#define XCT_PTR_ADDR_M		0x00000fffffffffffull
#define XCT_PTR_ADDR_S		0
#define XCT_PTR_ADDR(x)		((((long)(x)) << XCT_PTR_ADDR_S) & \
				 XCT_PTR_ADDR_M)

/* Receive interface 8byte result fields */
#define XCT_RXRES_8B_L4O_M	0xff00000000000000ull
#define XCT_RXRES_8B_L4O_S	56
#define XCT_RXRES_8B_RULE_M	0x00ffff0000000000ull
#define XCT_RXRES_8B_RULE_S	40
#define XCT_RXRES_8B_EVAL_M	0x000000ffff000000ull
#define XCT_RXRES_8B_EVAL_S	24
#define XCT_RXRES_8B_HTYPE_M	0x0000000000f00000ull
#define XCT_RXRES_8B_HASH_M	0x00000000000fffffull
#define XCT_RXRES_8B_HASH_S	0

/* Receive interface buffer fields */
#define XCT_RXB_LEN_M		0x0ffff00000000000ull
#define XCT_RXB_LEN_S		44
#define XCT_RXB_LEN(x)		((((long)(x)) << XCT_RXB_LEN_S) & \
				 XCT_RXB_LEN_M)
#define XCT_RXB_ADDR_M		0x00000fffffffffffull
#define XCT_RXB_ADDR_S		0
#define XCT_RXB_ADDR(x)		((((long)(x)) << XCT_RXB_ADDR_S) & \
				 XCT_RXB_ADDR_M)

/* Copy descriptor fields */
#define XCT_COPY_T		0x8000000000000000ull
#define XCT_COPY_ST		0x4000000000000000ull
#define XCT_COPY_RR_M		0x3000000000000000ull
#define XCT_COPY_RR_NORES	0x0000000000000000ull
#define XCT_COPY_RR_8BRES	0x1000000000000000ull
#define XCT_COPY_RR_24BRES	0x2000000000000000ull
#define XCT_COPY_RR_40BRES	0x3000000000000000ull
#define XCT_COPY_I		0x0800000000000000ull
#define XCT_COPY_O		0x0400000000000000ull
#define XCT_COPY_E		0x0200000000000000ull
#define XCT_COPY_STY_ZERO	0x01c0000000000000ull
#define XCT_COPY_DTY_PREF	0x0038000000000000ull
#define XCT_COPY_LLEN_M		0x0007ffff00000000ull
#define XCT_COPY_LLEN_S		32
#define XCT_COPY_LLEN(x)	((((long)(x)) << XCT_COPY_LLEN_S) & \
				 XCT_COPY_LLEN_M)
#define XCT_COPY_SE		0x0000000000000001ull

/* Function descriptor fields */
#define XCT_FUN_T		0x8000000000000000ull
#define XCT_FUN_ST		0x4000000000000000ull
#define XCT_FUN_RR_M		0x3000000000000000ull
#define XCT_FUN_RR_NORES	0x0000000000000000ull
#define XCT_FUN_RR_8BRES	0x1000000000000000ull
#define XCT_FUN_RR_24BRES	0x2000000000000000ull
#define XCT_FUN_RR_40BRES	0x3000000000000000ull
#define XCT_FUN_I		0x0800000000000000ull
#define XCT_FUN_O		0x0400000000000000ull
#define XCT_FUN_E		0x0200000000000000ull
#define XCT_FUN_FUN_M		0x01c0000000000000ull
#define XCT_FUN_FUN_S		54
#define XCT_FUN_FUN(x)		((((long)(x)) << XCT_FUN_FUN_S) & XCT_FUN_FUN_M)
#define XCT_FUN_CRM_M		0x0038000000000000ull
#define XCT_FUN_CRM_NOP		0x0000000000000000ull
#define XCT_FUN_CRM_SIG		0x0008000000000000ull
#define XCT_FUN_LLEN_M		0x0007ffff00000000ull
#define XCT_FUN_LLEN_S		32
#define XCT_FUN_LLEN(x)		((((long)(x)) << XCT_FUN_LLEN_S) & XCT_FUN_LLEN_M)
#define XCT_FUN_SHL_M		0x00000000f8000000ull
#define XCT_FUN_SHL_S		27
#define XCT_FUN_SHL(x)		((((long)(x)) << XCT_FUN_SHL_S) & XCT_FUN_SHL_M)
#define XCT_FUN_CHL_M		0x0000000007c00000ull
#define XCT_FUN_HSZ_M		0x00000000003c0000ull
#define XCT_FUN_ALG_M		0x0000000000038000ull
#define XCT_FUN_HP		0x0000000000004000ull
#define XCT_FUN_BCM_M		0x0000000000003800ull
#define XCT_FUN_BCP_M		0x0000000000000600ull
#define XCT_FUN_SIG_M		0x00000000000001f0ull
#define XCT_FUN_SIG_TCP4	0x0000000000000140ull
#define XCT_FUN_SIG_TCP6	0x0000000000000150ull
#define XCT_FUN_SIG_UDP4	0x0000000000000160ull
#define XCT_FUN_SIG_UDP6	0x0000000000000170ull
#define XCT_FUN_A		0x0000000000000008ull
#define XCT_FUN_C		0x0000000000000004ull
#define XCT_FUN_AL2		0x0000000000000002ull
#define XCT_FUN_SE		0x0000000000000001ull

/* Function descriptor 8byte result fields */
#define XCT_FUNRES_8B_CS_M	0x0000ffff00000000ull
#define XCT_FUNRES_8B_CS_S	32
#define XCT_FUNRES_8B_CRC_M	0x00000000ffffffffull
#define XCT_FUNRES_8B_CRC_S	0

/* Control descriptor fields */
#define CTRL_CMD_T		0x8000000000000000ull
#define CTRL_CMD_META_EVT	0x2000000000000000ull
#define CTRL_CMD_O		0x0400000000000000ull
#define CTRL_CMD_ETYPE_M	0x0038000000000000ull
#define CTRL_CMD_ETYPE_EXT	0x0000000000000000ull
#define CTRL_CMD_ETYPE_WSET	0x0020000000000000ull
#define CTRL_CMD_ETYPE_WCLR	0x0028000000000000ull
#define CTRL_CMD_ETYPE_SET	0x0030000000000000ull
#define CTRL_CMD_ETYPE_CLR	0x0038000000000000ull
#define CTRL_CMD_REG_M		0x000000000000007full
#define CTRL_CMD_REG_S		0
#define CTRL_CMD_REG(x)		((((long)(x)) << CTRL_CMD_REG_S) & \
				 CTRL_CMD_REG_M)



/* Prototypes for the shared DMA functions in the platform code. */

/* DMA TX Channel type. Right now only limitations used are event types 0/1,
 * for event-triggered DMA transactions.
 */

enum pasemi_dmachan_type {
	RXCHAN = 0,		/* Any RX chan */
	TXCHAN = 1,		/* Any TX chan */
	TXCHAN_EVT0 = 0x1001,	/* TX chan in event class 0 (chan 0-9) */
	TXCHAN_EVT1 = 0x2001,	/* TX chan in event class 1 (chan 10-19) */
};

struct pasemi_dmachan {
	int		 chno;		/* Channel number */
	enum pasemi_dmachan_type chan_type;	/* TX / RX */
	u64		*status;	/* Ptr to cacheable status */
	int		 irq;		/* IRQ used by channel */
	unsigned int	 ring_size;	/* size of allocated ring */
	dma_addr_t	 ring_dma;	/* DMA address for ring */
	u64		*ring_virt;	/* Virt address for ring */
	void		*priv;		/* Ptr to start of client struct */
};

/* Read/write the different registers in the I/O Bridge, Ethernet
 * and DMA Controller
 */
extern unsigned int pasemi_read_iob_reg(unsigned int reg);
extern void pasemi_write_iob_reg(unsigned int reg, unsigned int val);

extern unsigned int pasemi_read_mac_reg(int intf, unsigned int reg);
extern void pasemi_write_mac_reg(int intf, unsigned int reg, unsigned int val);

extern unsigned int pasemi_read_dma_reg(unsigned int reg);
extern void pasemi_write_dma_reg(unsigned int reg, unsigned int val);

/* Channel management routines */

extern void *pasemi_dma_alloc_chan(enum pasemi_dmachan_type type,
				   int total_size, int offset);
extern void pasemi_dma_free_chan(struct pasemi_dmachan *chan);

extern void pasemi_dma_start_chan(const struct pasemi_dmachan *chan,
				  const u32 cmdsta);
extern int pasemi_dma_stop_chan(const struct pasemi_dmachan *chan);

/* Common routines to allocate rings and buffers */

extern int pasemi_dma_alloc_ring(struct pasemi_dmachan *chan, int ring_size);
extern void pasemi_dma_free_ring(struct pasemi_dmachan *chan);

extern void *pasemi_dma_alloc_buf(struct pasemi_dmachan *chan, int size,
				  dma_addr_t *handle);
extern void pasemi_dma_free_buf(struct pasemi_dmachan *chan, int size,
				dma_addr_t *handle);

/* Routines to allocate flags (events) for channel syncronization */
extern int  pasemi_dma_alloc_flag(void);
extern void pasemi_dma_free_flag(int flag);
extern void pasemi_dma_set_flag(int flag);
extern void pasemi_dma_clear_flag(int flag);

/* Routines to allocate function engines */
extern int  pasemi_dma_alloc_fun(void);
extern void pasemi_dma_free_fun(int fun);

/* Initialize the library, must be called before any other functions */
extern int pasemi_dma_init(void);

#endif /* ASM_PASEMI_DMA_H */
