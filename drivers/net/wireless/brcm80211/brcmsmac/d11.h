/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_BRCM_D11_H_
#define	_BRCM_D11_H_

#include <linux/ieee80211.h>

#include <defs.h>
#include "pub.h"
#include "dma.h"

/* RX FIFO numbers */
#define	RX_FIFO			0	/* data and ctl frames */
#define	RX_TXSTATUS_FIFO	3	/* RX fifo for tx status packages */

/* TX FIFO numbers using WME Access Category */
#define	TX_AC_BK_FIFO		0	/* Background TX FIFO */
#define	TX_AC_BE_FIFO		1	/* Best-Effort TX FIFO */
#define	TX_AC_VI_FIFO		2	/* Video TX FIFO */
#define	TX_AC_VO_FIFO		3	/* Voice TX FIFO */
#define	TX_BCMC_FIFO		4	/* Broadcast/Multicast TX FIFO */
#define	TX_ATIM_FIFO		5	/* TX fifo for ATIM window info */

/* Addr is byte address used by SW; offset is word offset used by uCode */

/* Per AC TX limit settings */
#define M_AC_TXLMT_BASE_ADDR         (0x180 * 2)
#define M_AC_TXLMT_ADDR(_ac)         (M_AC_TXLMT_BASE_ADDR + (2 * (_ac)))

/* Legacy TX FIFO numbers */
#define	TX_DATA_FIFO		TX_AC_BE_FIFO
#define	TX_CTL_FIFO		TX_AC_VO_FIFO

#define WL_RSSI_ANT_MAX		4	/* max possible rx antennas */

struct intctrlregs {
	u32 intstatus;
	u32 intmask;
};

/* PIO structure,
 *  support two PIO format: 2 bytes access and 4 bytes access
 *  basic FIFO register set is per channel(transmit or receive)
 *  a pair of channels is defined for convenience
 */
/* 2byte-wide pio register set per channel(xmt or rcv) */
struct pio2regs {
	u16 fifocontrol;
	u16 fifodata;
	u16 fifofree;	/* only valid in xmt channel, not in rcv channel */
	u16 PAD;
};

/* a pair of pio channels(tx and rx) */
struct pio2regp {
	struct pio2regs tx;
	struct pio2regs rx;
};

/* 4byte-wide pio register set per channel(xmt or rcv) */
struct pio4regs {
	u32 fifocontrol;
	u32 fifodata;
};

/* a pair of pio channels(tx and rx) */
struct pio4regp {
	struct pio4regs tx;
	struct pio4regs rx;
};

/* read: 32-bit register that can be read as 32-bit or as 2 16-bit
 * write: only low 16b-it half can be written
 */
union pmqreg {
	u32 pmqhostdata;	/* read only! */
	struct {
		u16 pmqctrlstatus;	/* read/write */
		u16 PAD;
	} w;
};

struct fifo64 {
	struct dma64regs dmaxmt;	/* dma tx */
	struct pio4regs piotx;	/* pio tx */
	struct dma64regs dmarcv;	/* dma rx */
	struct pio4regs piorx;	/* pio rx */
};

/*
 * Host Interface Registers
 */
struct d11regs {
	/* Device Control ("semi-standard host registers") */
	u32 PAD[3];		/* 0x0 - 0x8 */
	u32 biststatus;	/* 0xC */
	u32 biststatus2;	/* 0x10 */
	u32 PAD;		/* 0x14 */
	u32 gptimer;		/* 0x18 */
	u32 usectimer;	/* 0x1c *//* for corerev >= 26 */

	/* Interrupt Control *//* 0x20 */
	struct intctrlregs intctrlregs[8];

	u32 PAD[40];		/* 0x60 - 0xFC */

	u32 intrcvlazy[4];	/* 0x100 - 0x10C */

	u32 PAD[4];		/* 0x110 - 0x11c */

	u32 maccontrol;	/* 0x120 */
	u32 maccommand;	/* 0x124 */
	u32 macintstatus;	/* 0x128 */
	u32 macintmask;	/* 0x12C */

	/* Transmit Template Access */
	u32 tplatewrptr;	/* 0x130 */
	u32 tplatewrdata;	/* 0x134 */
	u32 PAD[2];		/* 0x138 - 0x13C */

	/* PMQ registers */
	union pmqreg pmqreg;	/* 0x140 */
	u32 pmqpatl;		/* 0x144 */
	u32 pmqpath;		/* 0x148 */
	u32 PAD;		/* 0x14C */

	u32 chnstatus;	/* 0x150 */
	u32 psmdebug;	/* 0x154 */
	u32 phydebug;	/* 0x158 */
	u32 machwcap;	/* 0x15C */

	/* Extended Internal Objects */
	u32 objaddr;		/* 0x160 */
	u32 objdata;		/* 0x164 */
	u32 PAD[2];		/* 0x168 - 0x16c */

	u32 frmtxstatus;	/* 0x170 */
	u32 frmtxstatus2;	/* 0x174 */
	u32 PAD[2];		/* 0x178 - 0x17c */

	/* TSF host access */
	u32 tsf_timerlow;	/* 0x180 */
	u32 tsf_timerhigh;	/* 0x184 */
	u32 tsf_cfprep;	/* 0x188 */
	u32 tsf_cfpstart;	/* 0x18c */
	u32 tsf_cfpmaxdur32;	/* 0x190 */
	u32 PAD[3];		/* 0x194 - 0x19c */

	u32 maccontrol1;	/* 0x1a0 */
	u32 machwcap1;	/* 0x1a4 */
	u32 PAD[14];		/* 0x1a8 - 0x1dc */

	/* Clock control and hardware workarounds*/
	u32 clk_ctl_st;	/* 0x1e0 */
	u32 hw_war;
	u32 d11_phypllctl;	/* the phypll request/avail bits are
				 * moved to clk_ctl_st
				 */
	u32 PAD[5];		/* 0x1ec - 0x1fc */

	/* 0x200-0x37F dma/pio registers */
	struct fifo64 fifo64regs[6];

	/* FIFO diagnostic port access */
	struct dma32diag dmafifo;	/* 0x380 - 0x38C */

	u32 aggfifocnt;	/* 0x390 */
	u32 aggfifodata;	/* 0x394 */
	u32 PAD[16];		/* 0x398 - 0x3d4 */
	u16 radioregaddr;	/* 0x3d8 */
	u16 radioregdata;	/* 0x3da */

	/*
	 * time delay between the change on rf disable input and
	 * radio shutdown
	 */
	u32 rfdisabledly;	/* 0x3DC */

	/* PHY register access */
	u16 phyversion;	/* 0x3e0 - 0x0 */
	u16 phybbconfig;	/* 0x3e2 - 0x1 */
	u16 phyadcbias;	/* 0x3e4 - 0x2  Bphy only */
	u16 phyanacore;	/* 0x3e6 - 0x3  pwwrdwn on aphy */
	u16 phyrxstatus0;	/* 0x3e8 - 0x4 */
	u16 phyrxstatus1;	/* 0x3ea - 0x5 */
	u16 phycrsth;	/* 0x3ec - 0x6 */
	u16 phytxerror;	/* 0x3ee - 0x7 */
	u16 phychannel;	/* 0x3f0 - 0x8 */
	u16 PAD[1];		/* 0x3f2 - 0x9 */
	u16 phytest;		/* 0x3f4 - 0xa */
	u16 phy4waddr;	/* 0x3f6 - 0xb */
	u16 phy4wdatahi;	/* 0x3f8 - 0xc */
	u16 phy4wdatalo;	/* 0x3fa - 0xd */
	u16 phyregaddr;	/* 0x3fc - 0xe */
	u16 phyregdata;	/* 0x3fe - 0xf */

	/* IHR *//* 0x400 - 0x7FE */

	/* RXE Block */
	u16 PAD[3];		/* 0x400 - 0x406 */
	u16 rcv_fifo_ctl;	/* 0x406 */
	u16 PAD;		/* 0x408 - 0x40a */
	u16 rcv_frm_cnt;	/* 0x40a */
	u16 PAD[4];		/* 0x40a - 0x414 */
	u16 rssi;		/* 0x414 */
	u16 PAD[5];		/* 0x414 - 0x420 */
	u16 rcm_ctl;		/* 0x420 */
	u16 rcm_mat_data;	/* 0x422 */
	u16 rcm_mat_mask;	/* 0x424 */
	u16 rcm_mat_dly;	/* 0x426 */
	u16 rcm_cond_mask_l;	/* 0x428 */
	u16 rcm_cond_mask_h;	/* 0x42A */
	u16 rcm_cond_dly;	/* 0x42C */
	u16 PAD[1];		/* 0x42E */
	u16 ext_ihr_addr;	/* 0x430 */
	u16 ext_ihr_data;	/* 0x432 */
	u16 rxe_phyrs_2;	/* 0x434 */
	u16 rxe_phyrs_3;	/* 0x436 */
	u16 phy_mode;	/* 0x438 */
	u16 rcmta_ctl;	/* 0x43a */
	u16 rcmta_size;	/* 0x43c */
	u16 rcmta_addr0;	/* 0x43e */
	u16 rcmta_addr1;	/* 0x440 */
	u16 rcmta_addr2;	/* 0x442 */
	u16 PAD[30];		/* 0x444 - 0x480 */

	/* PSM Block *//* 0x480 - 0x500 */

	u16 PAD;		/* 0x480 */
	u16 psm_maccontrol_h;	/* 0x482 */
	u16 psm_macintstatus_l;	/* 0x484 */
	u16 psm_macintstatus_h;	/* 0x486 */
	u16 psm_macintmask_l;	/* 0x488 */
	u16 psm_macintmask_h;	/* 0x48A */
	u16 PAD;		/* 0x48C */
	u16 psm_maccommand;	/* 0x48E */
	u16 psm_brc;		/* 0x490 */
	u16 psm_phy_hdr_param;	/* 0x492 */
	u16 psm_postcard;	/* 0x494 */
	u16 psm_pcard_loc_l;	/* 0x496 */
	u16 psm_pcard_loc_h;	/* 0x498 */
	u16 psm_gpio_in;	/* 0x49A */
	u16 psm_gpio_out;	/* 0x49C */
	u16 psm_gpio_oe;	/* 0x49E */

	u16 psm_bred_0;	/* 0x4A0 */
	u16 psm_bred_1;	/* 0x4A2 */
	u16 psm_bred_2;	/* 0x4A4 */
	u16 psm_bred_3;	/* 0x4A6 */
	u16 psm_brcl_0;	/* 0x4A8 */
	u16 psm_brcl_1;	/* 0x4AA */
	u16 psm_brcl_2;	/* 0x4AC */
	u16 psm_brcl_3;	/* 0x4AE */
	u16 psm_brpo_0;	/* 0x4B0 */
	u16 psm_brpo_1;	/* 0x4B2 */
	u16 psm_brpo_2;	/* 0x4B4 */
	u16 psm_brpo_3;	/* 0x4B6 */
	u16 psm_brwk_0;	/* 0x4B8 */
	u16 psm_brwk_1;	/* 0x4BA */
	u16 psm_brwk_2;	/* 0x4BC */
	u16 psm_brwk_3;	/* 0x4BE */

	u16 psm_base_0;	/* 0x4C0 */
	u16 psm_base_1;	/* 0x4C2 */
	u16 psm_base_2;	/* 0x4C4 */
	u16 psm_base_3;	/* 0x4C6 */
	u16 psm_base_4;	/* 0x4C8 */
	u16 psm_base_5;	/* 0x4CA */
	u16 psm_base_6;	/* 0x4CC */
	u16 psm_pc_reg_0;	/* 0x4CE */
	u16 psm_pc_reg_1;	/* 0x4D0 */
	u16 psm_pc_reg_2;	/* 0x4D2 */
	u16 psm_pc_reg_3;	/* 0x4D4 */
	u16 PAD[0xD];	/* 0x4D6 - 0x4DE */
	u16 psm_corectlsts;	/* 0x4f0 *//* Corerev >= 13 */
	u16 PAD[0x7];	/* 0x4f2 - 0x4fE */

	/* TXE0 Block *//* 0x500 - 0x580 */
	u16 txe_ctl;		/* 0x500 */
	u16 txe_aux;		/* 0x502 */
	u16 txe_ts_loc;	/* 0x504 */
	u16 txe_time_out;	/* 0x506 */
	u16 txe_wm_0;	/* 0x508 */
	u16 txe_wm_1;	/* 0x50A */
	u16 txe_phyctl;	/* 0x50C */
	u16 txe_status;	/* 0x50E */
	u16 txe_mmplcp0;	/* 0x510 */
	u16 txe_mmplcp1;	/* 0x512 */
	u16 txe_phyctl1;	/* 0x514 */

	u16 PAD[0x05];	/* 0x510 - 0x51E */

	/* Transmit control */
	u16 xmtfifodef;	/* 0x520 */
	u16 xmtfifo_frame_cnt;	/* 0x522 *//* Corerev >= 16 */
	u16 xmtfifo_byte_cnt;	/* 0x524 *//* Corerev >= 16 */
	u16 xmtfifo_head;	/* 0x526 *//* Corerev >= 16 */
	u16 xmtfifo_rd_ptr;	/* 0x528 *//* Corerev >= 16 */
	u16 xmtfifo_wr_ptr;	/* 0x52A *//* Corerev >= 16 */
	u16 xmtfifodef1;	/* 0x52C *//* Corerev >= 16 */

	u16 PAD[0x09];	/* 0x52E - 0x53E */

	u16 xmtfifocmd;	/* 0x540 */
	u16 xmtfifoflush;	/* 0x542 */
	u16 xmtfifothresh;	/* 0x544 */
	u16 xmtfifordy;	/* 0x546 */
	u16 xmtfifoprirdy;	/* 0x548 */
	u16 xmtfiforqpri;	/* 0x54A */
	u16 xmttplatetxptr;	/* 0x54C */
	u16 PAD;		/* 0x54E */
	u16 xmttplateptr;	/* 0x550 */
	u16 smpl_clct_strptr;	/* 0x552 *//* Corerev >= 22 */
	u16 smpl_clct_stpptr;	/* 0x554 *//* Corerev >= 22 */
	u16 smpl_clct_curptr;	/* 0x556 *//* Corerev >= 22 */
	u16 PAD[0x04];	/* 0x558 - 0x55E */
	u16 xmttplatedatalo;	/* 0x560 */
	u16 xmttplatedatahi;	/* 0x562 */

	u16 PAD[2];		/* 0x564 - 0x566 */

	u16 xmtsel;		/* 0x568 */
	u16 xmttxcnt;	/* 0x56A */
	u16 xmttxshmaddr;	/* 0x56C */

	u16 PAD[0x09];	/* 0x56E - 0x57E */

	/* TXE1 Block */
	u16 PAD[0x40];	/* 0x580 - 0x5FE */

	/* TSF Block */
	u16 PAD[0X02];	/* 0x600 - 0x602 */
	u16 tsf_cfpstrt_l;	/* 0x604 */
	u16 tsf_cfpstrt_h;	/* 0x606 */
	u16 PAD[0X05];	/* 0x608 - 0x610 */
	u16 tsf_cfppretbtt;	/* 0x612 */
	u16 PAD[0XD];	/* 0x614 - 0x62C */
	u16 tsf_clk_frac_l;	/* 0x62E */
	u16 tsf_clk_frac_h;	/* 0x630 */
	u16 PAD[0X14];	/* 0x632 - 0x658 */
	u16 tsf_random;	/* 0x65A */
	u16 PAD[0x05];	/* 0x65C - 0x664 */
	/* GPTimer 2 registers */
	u16 tsf_gpt2_stat;	/* 0x666 */
	u16 tsf_gpt2_ctr_l;	/* 0x668 */
	u16 tsf_gpt2_ctr_h;	/* 0x66A */
	u16 tsf_gpt2_val_l;	/* 0x66C */
	u16 tsf_gpt2_val_h;	/* 0x66E */
	u16 tsf_gptall_stat;	/* 0x670 */
	u16 PAD[0x07];	/* 0x672 - 0x67E */

	/* IFS Block */
	u16 ifs_sifs_rx_tx_tx;	/* 0x680 */
	u16 ifs_sifs_nav_tx;	/* 0x682 */
	u16 ifs_slot;	/* 0x684 */
	u16 PAD;		/* 0x686 */
	u16 ifs_ctl;		/* 0x688 */
	u16 PAD[0x3];	/* 0x68a - 0x68F */
	u16 ifsstat;		/* 0x690 */
	u16 ifsmedbusyctl;	/* 0x692 */
	u16 iftxdur;		/* 0x694 */
	u16 PAD[0x3];	/* 0x696 - 0x69b */
	/* EDCF support in dot11macs */
	u16 ifs_aifsn;	/* 0x69c */
	u16 ifs_ctl1;	/* 0x69e */

	/* slow clock registers */
	u16 scc_ctl;		/* 0x6a0 */
	u16 scc_timer_l;	/* 0x6a2 */
	u16 scc_timer_h;	/* 0x6a4 */
	u16 scc_frac;	/* 0x6a6 */
	u16 scc_fastpwrup_dly;	/* 0x6a8 */
	u16 scc_per;		/* 0x6aa */
	u16 scc_per_frac;	/* 0x6ac */
	u16 scc_cal_timer_l;	/* 0x6ae */
	u16 scc_cal_timer_h;	/* 0x6b0 */
	u16 PAD;		/* 0x6b2 */

	u16 PAD[0x26];

	/* NAV Block */
	u16 nav_ctl;		/* 0x700 */
	u16 navstat;		/* 0x702 */
	u16 PAD[0x3e];	/* 0x702 - 0x77E */

	/* WEP/PMQ Block *//* 0x780 - 0x7FE */
	u16 PAD[0x20];	/* 0x780 - 0x7BE */

	u16 wepctl;		/* 0x7C0 */
	u16 wepivloc;	/* 0x7C2 */
	u16 wepivkey;	/* 0x7C4 */
	u16 wepwkey;		/* 0x7C6 */

	u16 PAD[4];		/* 0x7C8 - 0x7CE */
	u16 pcmctl;		/* 0X7D0 */
	u16 pcmstat;		/* 0X7D2 */
	u16 PAD[6];		/* 0x7D4 - 0x7DE */

	u16 pmqctl;		/* 0x7E0 */
	u16 pmqstatus;	/* 0x7E2 */
	u16 pmqpat0;		/* 0x7E4 */
	u16 pmqpat1;		/* 0x7E6 */
	u16 pmqpat2;		/* 0x7E8 */

	u16 pmqdat;		/* 0x7EA */
	u16 pmqdator;	/* 0x7EC */
	u16 pmqhst;		/* 0x7EE */
	u16 pmqpath0;	/* 0x7F0 */
	u16 pmqpath1;	/* 0x7F2 */
	u16 pmqpath2;	/* 0x7F4 */
	u16 pmqdath;		/* 0x7F6 */

	u16 PAD[0x04];	/* 0x7F8 - 0x7FE */

	/* SHM *//* 0x800 - 0xEFE */
	u16 PAD[0x380];	/* 0x800 - 0xEFE */
};

/* d11 register field offset */
#define D11REGOFFS(field)	offsetof(struct d11regs, field)

#define	PIHR_BASE	0x0400	/* byte address of packed IHR region */

/* biststatus */
#define	BT_DONE		(1U << 31)	/* bist done */
#define	BT_B2S		(1 << 30)	/* bist2 ram summary bit */

/* intstatus and intmask */
#define	I_PC		(1 << 10)	/* pci descriptor error */
#define	I_PD		(1 << 11)	/* pci data error */
#define	I_DE		(1 << 12)	/* descriptor protocol error */
#define	I_RU		(1 << 13)	/* receive descriptor underflow */
#define	I_RO		(1 << 14)	/* receive fifo overflow */
#define	I_XU		(1 << 15)	/* transmit fifo underflow */
#define	I_RI		(1 << 16)	/* receive interrupt */
#define	I_XI		(1 << 24)	/* transmit interrupt */

/* interrupt receive lazy */
#define	IRL_TO_MASK		0x00ffffff	/* timeout */
#define	IRL_FC_MASK		0xff000000	/* frame count */
#define	IRL_FC_SHIFT		24	/* frame count */

/*== maccontrol register ==*/
#define	MCTL_GMODE		(1U << 31)
#define	MCTL_DISCARD_PMQ	(1 << 30)
#define	MCTL_WAKE		(1 << 26)
#define	MCTL_HPS		(1 << 25)
#define	MCTL_PROMISC		(1 << 24)
#define	MCTL_KEEPBADFCS		(1 << 23)
#define	MCTL_KEEPCONTROL	(1 << 22)
#define	MCTL_PHYLOCK		(1 << 21)
#define	MCTL_BCNS_PROMISC	(1 << 20)
#define	MCTL_LOCK_RADIO		(1 << 19)
#define	MCTL_AP			(1 << 18)
#define	MCTL_INFRA		(1 << 17)
#define	MCTL_BIGEND		(1 << 16)
#define	MCTL_GPOUT_SEL_MASK	(3 << 14)
#define	MCTL_GPOUT_SEL_SHIFT	14
#define	MCTL_EN_PSMDBG		(1 << 13)
#define	MCTL_IHR_EN		(1 << 10)
#define	MCTL_SHM_UPPER		(1 <<  9)
#define	MCTL_SHM_EN		(1 <<  8)
#define	MCTL_PSM_JMP_0		(1 <<  2)
#define	MCTL_PSM_RUN		(1 <<  1)
#define	MCTL_EN_MAC		(1 <<  0)

/*== maccommand register ==*/
#define	MCMD_BCN0VLD		(1 <<  0)
#define	MCMD_BCN1VLD		(1 <<  1)
#define	MCMD_DIRFRMQVAL		(1 <<  2)
#define	MCMD_CCA		(1 <<  3)
#define	MCMD_BG_NOISE		(1 <<  4)
#define	MCMD_SKIP_SHMINIT	(1 <<  5)	/* only used for simulation */
#define MCMD_SAMPLECOLL		MCMD_SKIP_SHMINIT /* reuse for sample collect */

/*== macintstatus/macintmask ==*/
/* gracefully suspended */
#define	MI_MACSSPNDD		(1 <<  0)
/* beacon template available */
#define	MI_BCNTPL		(1 <<  1)
/* TBTT indication */
#define	MI_TBTT			(1 <<  2)
/* beacon successfully tx'd */
#define	MI_BCNSUCCESS		(1 <<  3)
/* beacon canceled (IBSS) */
#define	MI_BCNCANCLD		(1 <<  4)
/* end of ATIM-window (IBSS) */
#define	MI_ATIMWINEND		(1 <<  5)
/* PMQ entries available */
#define	MI_PMQ			(1 <<  6)
/* non-specific gen-stat bits that are set by PSM */
#define	MI_NSPECGEN_0		(1 <<  7)
/* non-specific gen-stat bits that are set by PSM */
#define	MI_NSPECGEN_1		(1 <<  8)
/* MAC level Tx error */
#define	MI_MACTXERR		(1 <<  9)
/* non-specific gen-stat bits that are set by PSM */
#define	MI_NSPECGEN_3		(1 << 10)
/* PHY Tx error */
#define	MI_PHYTXERR		(1 << 11)
/* Power Management Event */
#define	MI_PME			(1 << 12)
/* General-purpose timer0 */
#define	MI_GP0			(1 << 13)
/* General-purpose timer1 */
#define	MI_GP1			(1 << 14)
/* (ORed) DMA-interrupts */
#define	MI_DMAINT		(1 << 15)
/* MAC has completed a TX FIFO Suspend/Flush */
#define	MI_TXSTOP		(1 << 16)
/* MAC has completed a CCA measurement */
#define	MI_CCA			(1 << 17)
/* MAC has collected background noise samples */
#define	MI_BG_NOISE		(1 << 18)
/* MBSS DTIM TBTT indication */
#define	MI_DTIM_TBTT		(1 << 19)
/* Probe response queue needs attention */
#define MI_PRQ			(1 << 20)
/* Radio/PHY has been powered back up. */
#define	MI_PWRUP		(1 << 21)
#define	MI_RESERVED3		(1 << 22)
#define	MI_RESERVED2		(1 << 23)
#define MI_RESERVED1		(1 << 25)
/* MAC detected change on RF Disable input*/
#define MI_RFDISABLE		(1 << 28)
/* MAC has completed a TX */
#define	MI_TFS			(1 << 29)
/* A phy status change wrt G mode */
#define	MI_PHYCHANGED		(1 << 30)
/* general purpose timeout */
#define	MI_TO			(1U << 31)

/* Mac capabilities registers */
/*== machwcap ==*/
#define	MCAP_TKIPMIC		0x80000000	/* TKIP MIC hardware present */

/*== pmqhost data ==*/
/* data entry of head pmq entry */
#define	PMQH_DATA_MASK		0xffff0000
/* PM entry for BSS config */
#define	PMQH_BSSCFG		0x00100000
/* PM Mode OFF: power save off */
#define	PMQH_PMOFF		0x00010000
/* PM Mode ON: power save on */
#define	PMQH_PMON		0x00020000
/* Dis-associated or De-authenticated */
#define	PMQH_DASAT		0x00040000
/* ATIM not acknowledged */
#define	PMQH_ATIMFAIL		0x00080000
/* delete head entry */
#define	PMQH_DEL_ENTRY		0x00000001
/* delete head entry to cur read pointer -1 */
#define	PMQH_DEL_MULT		0x00000002
/* pmq overflow indication */
#define	PMQH_OFLO		0x00000004
/* entries are present in pmq */
#define	PMQH_NOT_EMPTY		0x00000008

/*== phydebug ==*/
/* phy is asserting carrier sense */
#define	PDBG_CRS		(1 << 0)
/* phy is taking xmit byte from mac this cycle */
#define	PDBG_TXA		(1 << 1)
/* mac is instructing the phy to transmit a frame */
#define	PDBG_TXF		(1 << 2)
/* phy is signalling a transmit Error to the mac */
#define	PDBG_TXE		(1 << 3)
/* phy detected the end of a valid frame preamble */
#define	PDBG_RXF		(1 << 4)
/* phy detected the end of a valid PLCP header */
#define	PDBG_RXS		(1 << 5)
/* rx start not asserted */
#define	PDBG_RXFRG		(1 << 6)
/* mac is taking receive byte from phy this cycle */
#define	PDBG_RXV		(1 << 7)
/* RF portion of the radio is disabled */
#define	PDBG_RFD		(1 << 16)

/*== objaddr register ==*/
#define	OBJADDR_SEL_MASK	0x000F0000
#define	OBJADDR_UCM_SEL		0x00000000
#define	OBJADDR_SHM_SEL		0x00010000
#define	OBJADDR_SCR_SEL		0x00020000
#define	OBJADDR_IHR_SEL		0x00030000
#define	OBJADDR_RCMTA_SEL	0x00040000
#define	OBJADDR_SRCHM_SEL	0x00060000
#define	OBJADDR_WINC		0x01000000
#define	OBJADDR_RINC		0x02000000
#define	OBJADDR_AUTO_INC	0x03000000

#define	WEP_PCMADDR		0x07d4
#define	WEP_PCMDATA		0x07d6

/*== frmtxstatus ==*/
#define	TXS_V			(1 << 0)	/* valid bit */
#define	TXS_STATUS_MASK		0xffff
#define	TXS_FID_MASK		0xffff0000
#define	TXS_FID_SHIFT		16

/*== frmtxstatus2 ==*/
#define	TXS_SEQ_MASK		0xffff
#define	TXS_PTX_MASK		0xff0000
#define	TXS_PTX_SHIFT		16
#define	TXS_MU_MASK		0x01000000
#define	TXS_MU_SHIFT		24

/*== clk_ctl_st ==*/
#define CCS_ERSRC_REQ_D11PLL	0x00000100	/* d11 core pll request */
#define CCS_ERSRC_REQ_PHYPLL	0x00000200	/* PHY pll request */
#define CCS_ERSRC_AVAIL_D11PLL	0x01000000	/* d11 core pll available */
#define CCS_ERSRC_AVAIL_PHYPLL	0x02000000	/* PHY pll available */

/* HT Cloclk Ctrl and Clock Avail for 4313 */
#define CCS_ERSRC_REQ_HT    0x00000010	/* HT avail request */
#define CCS_ERSRC_AVAIL_HT  0x00020000	/* HT clock available */

/* tsf_cfprep register */
#define	CFPREP_CBI_MASK		0xffffffc0
#define	CFPREP_CBI_SHIFT	6
#define	CFPREP_CFPP		0x00000001

/* tx fifo sizes values are in terms of 256 byte blocks */
#define TXFIFOCMD_RESET_MASK	(1 << 15)	/* reset */
#define TXFIFOCMD_FIFOSEL_SHIFT	8	/* fifo */
#define TXFIFO_FIFOTOP_SHIFT	8	/* fifo start */

#define TXFIFO_START_BLK16	 65	/* Base address + 32 * 512 B/P */
#define TXFIFO_START_BLK	 6	/* Base address + 6 * 256 B */
#define TXFIFO_SIZE_UNIT	256	/* one unit corresponds to 256 bytes */
#define MBSS16_TEMPLMEM_MINBLKS	65	/* one unit corresponds to 256 bytes */

/*== phy versions (PhyVersion:Revision field) ==*/
/* analog block version */
#define	PV_AV_MASK		0xf000
/* analog block version bitfield offset */
#define	PV_AV_SHIFT		12
/* phy type */
#define	PV_PT_MASK		0x0f00
/* phy type bitfield offset */
#define	PV_PT_SHIFT		8
/* phy version */
#define	PV_PV_MASK		0x000f
#define	PHY_TYPE(v)		((v & PV_PT_MASK) >> PV_PT_SHIFT)

/*== phy types (PhyVersion:PhyType field) ==*/
#define	PHY_TYPE_N		4	/* N-Phy value */
#define	PHY_TYPE_SSN		6	/* SSLPN-Phy value */
#define	PHY_TYPE_LCN		8	/* LCN-Phy value */
#define	PHY_TYPE_LCNXN		9	/* LCNXN-Phy value */
#define	PHY_TYPE_NULL		0xf	/* Invalid Phy value */

/*== analog types (PhyVersion:AnalogType field) ==*/
#define	ANA_11N_013		5

/* 802.11a PLCP header def */
struct ofdm_phy_hdr {
	u8 rlpt[3];		/* rate, length, parity, tail */
	u16 service;
	u8 pad;
} __packed;

#define	D11A_PHY_HDR_GRATE(phdr)	((phdr)->rlpt[0] & 0x0f)
#define	D11A_PHY_HDR_GRES(phdr)		(((phdr)->rlpt[0] >> 4) & 0x01)
#define	D11A_PHY_HDR_GLENGTH(phdr)	(((u32 *)((phdr)->rlpt) >> 5) & 0x0fff)
#define	D11A_PHY_HDR_GPARITY(phdr)	(((phdr)->rlpt[3] >> 1) & 0x01)
#define	D11A_PHY_HDR_GTAIL(phdr)	(((phdr)->rlpt[3] >> 2) & 0x3f)

/* rate encoded per 802.11a-1999 sec 17.3.4.1 */
#define	D11A_PHY_HDR_SRATE(phdr, rate)		\
	((phdr)->rlpt[0] = ((phdr)->rlpt[0] & 0xf0) | ((rate) & 0xf))
/* set reserved field to zero */
#define	D11A_PHY_HDR_SRES(phdr)		((phdr)->rlpt[0] &= 0xef)
/* length is number of octets in PSDU */
#define	D11A_PHY_HDR_SLENGTH(phdr, length)	\
	(*(u32 *)((phdr)->rlpt) = *(u32 *)((phdr)->rlpt) | \
	(((length) & 0x0fff) << 5))
/* set the tail to all zeros */
#define	D11A_PHY_HDR_STAIL(phdr)	((phdr)->rlpt[3] &= 0x03)

#define	D11A_PHY_HDR_LEN_L	3	/* low-rate part of PLCP header */
#define	D11A_PHY_HDR_LEN_R	2	/* high-rate part of PLCP header */

#define	D11A_PHY_TX_DELAY	(2)	/* 2.1 usec */

#define	D11A_PHY_HDR_TIME	(4)	/* low-rate part of PLCP header */
#define	D11A_PHY_PRE_TIME	(16)
#define	D11A_PHY_PREHDR_TIME	(D11A_PHY_PRE_TIME + D11A_PHY_HDR_TIME)

/* 802.11b PLCP header def */
struct cck_phy_hdr {
	u8 signal;
	u8 service;
	u16 length;
	u16 crc;
} __packed;

#define	D11B_PHY_HDR_LEN	6

#define	D11B_PHY_TX_DELAY	(3)	/* 3.4 usec */

#define	D11B_PHY_LHDR_TIME	(D11B_PHY_HDR_LEN << 3)
#define	D11B_PHY_LPRE_TIME	(144)
#define	D11B_PHY_LPREHDR_TIME	(D11B_PHY_LPRE_TIME + D11B_PHY_LHDR_TIME)

#define	D11B_PHY_SHDR_TIME	(D11B_PHY_LHDR_TIME >> 1)
#define	D11B_PHY_SPRE_TIME	(D11B_PHY_LPRE_TIME >> 1)
#define	D11B_PHY_SPREHDR_TIME	(D11B_PHY_SPRE_TIME + D11B_PHY_SHDR_TIME)

#define	D11B_PLCP_SIGNAL_LOCKED	(1 << 2)
#define	D11B_PLCP_SIGNAL_LE	(1 << 7)

#define MIMO_PLCP_MCS_MASK	0x7f	/* mcs index */
#define MIMO_PLCP_40MHZ		0x80	/* 40 Hz frame */
#define MIMO_PLCP_AMPDU		0x08	/* ampdu */

#define BRCMS_GET_CCK_PLCP_LEN(plcp) (plcp[4] + (plcp[5] << 8))
#define BRCMS_GET_MIMO_PLCP_LEN(plcp) (plcp[1] + (plcp[2] << 8))
#define BRCMS_SET_MIMO_PLCP_LEN(plcp, len) \
	do { \
		plcp[1] = len & 0xff; \
		plcp[2] = ((len >> 8) & 0xff); \
	} while (0)

#define BRCMS_SET_MIMO_PLCP_AMPDU(plcp) (plcp[3] |= MIMO_PLCP_AMPDU)
#define BRCMS_CLR_MIMO_PLCP_AMPDU(plcp) (plcp[3] &= ~MIMO_PLCP_AMPDU)
#define BRCMS_IS_MIMO_PLCP_AMPDU(plcp) (plcp[3] & MIMO_PLCP_AMPDU)

/*
 * The dot11a PLCP header is 5 bytes.  To simplify the software (so that we
 * don't need e.g. different tx DMA headers for 11a and 11b), the PLCP header
 * has padding added in the ucode.
 */
#define	D11_PHY_HDR_LEN	6

/* TX DMA buffer header */
struct d11txh {
	__le16 MacTxControlLow;	/* 0x0 */
	__le16 MacTxControlHigh;	/* 0x1 */
	__le16 MacFrameControl;	/* 0x2 */
	__le16 TxFesTimeNormal;	/* 0x3 */
	__le16 PhyTxControlWord;	/* 0x4 */
	__le16 PhyTxControlWord_1;	/* 0x5 */
	__le16 PhyTxControlWord_1_Fbr;	/* 0x6 */
	__le16 PhyTxControlWord_1_Rts;	/* 0x7 */
	__le16 PhyTxControlWord_1_FbrRts;	/* 0x8 */
	__le16 MainRates;	/* 0x9 */
	__le16 XtraFrameTypes;	/* 0xa */
	u8 IV[16];		/* 0x0b - 0x12 */
	u8 TxFrameRA[6];	/* 0x13 - 0x15 */
	__le16 TxFesTimeFallback;	/* 0x16 */
	u8 RTSPLCPFallback[6];	/* 0x17 - 0x19 */
	__le16 RTSDurFallback;	/* 0x1a */
	u8 FragPLCPFallback[6];	/* 0x1b - 1d */
	__le16 FragDurFallback;	/* 0x1e */
	__le16 MModeLen;	/* 0x1f */
	__le16 MModeFbrLen;	/* 0x20 */
	__le16 TstampLow;	/* 0x21 */
	__le16 TstampHigh;	/* 0x22 */
	__le16 ABI_MimoAntSel;	/* 0x23 */
	__le16 PreloadSize;	/* 0x24 */
	__le16 AmpduSeqCtl;	/* 0x25 */
	__le16 TxFrameID;	/* 0x26 */
	__le16 TxStatus;	/* 0x27 */
	__le16 MaxNMpdus;	/* 0x28 */
	__le16 MaxABytes_MRT;	/* 0x29 */
	__le16 MaxABytes_FBR;	/* 0x2a */
	__le16 MinMBytes;	/* 0x2b */
	u8 RTSPhyHeader[D11_PHY_HDR_LEN];	/* 0x2c - 0x2e */
	struct ieee80211_rts rts_frame;	/* 0x2f - 0x36 */
	u16 PAD;		/* 0x37 */
} __packed;

#define	D11_TXH_LEN		112	/* bytes */

/* Frame Types */
#define FT_CCK	0
#define FT_OFDM	1
#define FT_HT	2
#define FT_N	3

/*
 * Position of MPDU inside A-MPDU; indicated with bits 10:9
 * of MacTxControlLow
 */
#define TXC_AMPDU_SHIFT		9	/* shift for ampdu settings */
#define TXC_AMPDU_NONE		0	/* Regular MPDU, not an A-MPDU */
#define TXC_AMPDU_FIRST		1	/* first MPDU of an A-MPDU */
#define TXC_AMPDU_MIDDLE	2	/* intermediate MPDU of an A-MPDU */
#define TXC_AMPDU_LAST		3	/* last (or single) MPDU of an A-MPDU */

/*== MacTxControlLow ==*/
#define TXC_AMIC		0x8000
#define	TXC_SENDCTS		0x0800
#define TXC_AMPDU_MASK		0x0600
#define TXC_BW_40		0x0100
#define TXC_FREQBAND_5G		0x0080
#define	TXC_DFCS		0x0040
#define	TXC_IGNOREPMQ		0x0020
#define	TXC_HWSEQ		0x0010
#define	TXC_STARTMSDU		0x0008
#define	TXC_SENDRTS		0x0004
#define	TXC_LONGFRAME		0x0002
#define	TXC_IMMEDACK		0x0001

/*== MacTxControlHigh ==*/
/* RTS fallback preamble type 1 = SHORT 0 = LONG */
#define TXC_PREAMBLE_RTS_FB_SHORT	0x8000
/* RTS main rate preamble type 1 = SHORT 0 = LONG */
#define TXC_PREAMBLE_RTS_MAIN_SHORT	0x4000
/*
 * Main fallback rate preamble type
 *   1 = SHORT for OFDM/GF for MIMO
 *   0 = LONG for CCK/MM for MIMO
 */
#define TXC_PREAMBLE_DATA_FB_SHORT	0x2000

/* TXC_PREAMBLE_DATA_MAIN is in PhyTxControl bit 5 */
/* use fallback rate for this AMPDU */
#define	TXC_AMPDU_FBR		0x1000
#define	TXC_SECKEY_MASK		0x0FF0
#define	TXC_SECKEY_SHIFT	4
/* Use alternate txpwr defined at loc. M_ALT_TXPWR_IDX */
#define	TXC_ALT_TXPWR		0x0008
#define	TXC_SECTYPE_MASK	0x0007
#define	TXC_SECTYPE_SHIFT	0

/* Null delimiter for Fallback rate */
#define AMPDU_FBR_NULL_DELIM  5	/* Location of Null delimiter count for AMPDU */

/* PhyTxControl for Mimophy */
#define	PHY_TXC_PWR_MASK	0xFC00
#define	PHY_TXC_PWR_SHIFT	10
#define	PHY_TXC_ANT_MASK	0x03C0	/* bit 6, 7, 8, 9 */
#define	PHY_TXC_ANT_SHIFT	6
#define	PHY_TXC_ANT_0_1		0x00C0	/* auto, last rx */
#define	PHY_TXC_LCNPHY_ANT_LAST	0x0000
#define	PHY_TXC_ANT_3		0x0200	/* virtual antenna 3 */
#define	PHY_TXC_ANT_2		0x0100	/* virtual antenna 2 */
#define	PHY_TXC_ANT_1		0x0080	/* virtual antenna 1 */
#define	PHY_TXC_ANT_0		0x0040	/* virtual antenna 0 */
#define	PHY_TXC_SHORT_HDR	0x0010

#define	PHY_TXC_OLD_ANT_0	0x0000
#define	PHY_TXC_OLD_ANT_1	0x0100
#define	PHY_TXC_OLD_ANT_LAST	0x0300

/* PhyTxControl_1 for Mimophy */
#define PHY_TXC1_BW_MASK		0x0007
#define PHY_TXC1_BW_10MHZ		0
#define PHY_TXC1_BW_10MHZ_UP		1
#define PHY_TXC1_BW_20MHZ		2
#define PHY_TXC1_BW_20MHZ_UP		3
#define PHY_TXC1_BW_40MHZ		4
#define PHY_TXC1_BW_40MHZ_DUP		5
#define PHY_TXC1_MODE_SHIFT		3
#define PHY_TXC1_MODE_MASK		0x0038
#define PHY_TXC1_MODE_SISO		0
#define PHY_TXC1_MODE_CDD		1
#define PHY_TXC1_MODE_STBC		2
#define PHY_TXC1_MODE_SDM		3

/* PhyTxControl for HTphy that are different from Mimophy */
#define	PHY_TXC_HTANT_MASK		0x3fC0	/* bits 6-13 */

/* XtraFrameTypes */
#define XFTS_RTS_FT_SHIFT	2
#define XFTS_FBRRTS_FT_SHIFT	4
#define XFTS_CHANNEL_SHIFT	8

/* Antenna diversity bit in ant_wr_settle */
#define	PHY_AWS_ANTDIV		0x2000

/* IFS ctl */
#define IFS_USEEDCF	(1 << 2)

/* IFS ctl1 */
#define IFS_CTL1_EDCRS	(1 << 3)
#define IFS_CTL1_EDCRS_20L (1 << 4)
#define IFS_CTL1_EDCRS_40 (1 << 5)

/* ABI_MimoAntSel */
#define ABI_MAS_ADDR_BMP_IDX_MASK	0x0f00
#define ABI_MAS_ADDR_BMP_IDX_SHIFT	8
#define ABI_MAS_FBR_ANT_PTN_MASK	0x00f0
#define ABI_MAS_FBR_ANT_PTN_SHIFT	4
#define ABI_MAS_MRT_ANT_PTN_MASK	0x000f

/* tx status packet */
struct tx_status {
	u16 framelen;
	u16 PAD;
	u16 frameid;
	u16 status;
	u16 lasttxtime;
	u16 sequence;
	u16 phyerr;
	u16 ackphyrxsh;
} __packed;

#define	TXSTATUS_LEN	16

/* status field bit definitions */
#define	TX_STATUS_FRM_RTX_MASK	0xF000
#define	TX_STATUS_FRM_RTX_SHIFT	12
#define	TX_STATUS_RTS_RTX_MASK	0x0F00
#define	TX_STATUS_RTS_RTX_SHIFT	8
#define TX_STATUS_MASK		0x00FE
#define	TX_STATUS_PMINDCTD	(1 << 7) /* PM mode indicated to AP */
#define	TX_STATUS_INTERMEDIATE	(1 << 6) /* intermediate or 1st ampdu pkg */
#define	TX_STATUS_AMPDU		(1 << 5) /* AMPDU status */
#define TX_STATUS_SUPR_MASK	0x1C	 /* suppress status bits (4:2) */
#define TX_STATUS_SUPR_SHIFT	2
#define	TX_STATUS_ACK_RCV	(1 << 1) /* ACK received */
#define	TX_STATUS_VALID		(1 << 0) /* Tx status valid */
#define	TX_STATUS_NO_ACK	0

/* suppress status reason codes */
#define	TX_STATUS_SUPR_PMQ	(1 << 2) /* PMQ entry */
#define	TX_STATUS_SUPR_FLUSH	(2 << 2) /* flush request */
#define	TX_STATUS_SUPR_FRAG	(3 << 2) /* previous frag failure */
#define	TX_STATUS_SUPR_TBTT	(3 << 2) /* SHARED: Probe resp supr for TBTT */
#define	TX_STATUS_SUPR_BADCH	(4 << 2) /* channel mismatch */
#define	TX_STATUS_SUPR_EXPTIME	(5 << 2) /* lifetime expiry */
#define	TX_STATUS_SUPR_UF	(6 << 2) /* underflow */

/* Unexpected tx status for rate update */
#define TX_STATUS_UNEXP(status) \
	((((status) & TX_STATUS_INTERMEDIATE) != 0) && \
	 TX_STATUS_UNEXP_AMPDU(status))

/* Unexpected tx status for A-MPDU rate update */
#define TX_STATUS_UNEXP_AMPDU(status) \
	((((status) & TX_STATUS_SUPR_MASK) != 0) && \
	 (((status) & TX_STATUS_SUPR_MASK) != TX_STATUS_SUPR_EXPTIME))

#define TX_STATUS_BA_BMAP03_MASK	0xF000	/* ba bitmap 0:3 in 1st pkg */
#define TX_STATUS_BA_BMAP03_SHIFT	12	/* ba bitmap 0:3 in 1st pkg */
#define TX_STATUS_BA_BMAP47_MASK	0x001E	/* ba bitmap 4:7 in 2nd pkg */
#define TX_STATUS_BA_BMAP47_SHIFT	3	/* ba bitmap 4:7 in 2nd pkg */

/* RXE (Receive Engine) */

/* RCM_CTL */
#define	RCM_INC_MASK_H		0x0080
#define	RCM_INC_MASK_L		0x0040
#define	RCM_INC_DATA		0x0020
#define	RCM_INDEX_MASK		0x001F
#define	RCM_SIZE		15

#define	RCM_MAC_OFFSET		0	/* current MAC address */
#define	RCM_BSSID_OFFSET	3	/* current BSSID address */
#define	RCM_F_BSSID_0_OFFSET	6	/* foreign BSS CFP tracking */
#define	RCM_F_BSSID_1_OFFSET	9	/* foreign BSS CFP tracking */
#define	RCM_F_BSSID_2_OFFSET	12	/* foreign BSS CFP tracking */

#define RCM_WEP_TA0_OFFSET	16
#define RCM_WEP_TA1_OFFSET	19
#define RCM_WEP_TA2_OFFSET	22
#define RCM_WEP_TA3_OFFSET	25

/* PSM Block */

/* psm_phy_hdr_param bits */
#define MAC_PHY_RESET		1
#define MAC_PHY_CLOCK_EN	2
#define MAC_PHY_FORCE_CLK	4

/* WEP Block */

/* WEP_WKEY */
#define	WKEY_START		(1 << 8)
#define	WKEY_SEL_MASK		0x1F

/* WEP data formats */

/* the number of RCMTA entries */
#define RCMTA_SIZE 50

#define M_ADDR_BMP_BLK		(0x37e * 2)
#define M_ADDR_BMP_BLK_SZ	12

#define ADDR_BMP_RA		(1 << 0)	/* Receiver Address (RA) */
#define ADDR_BMP_TA		(1 << 1)	/* Transmitter Address (TA) */
#define ADDR_BMP_BSSID		(1 << 2)	/* BSSID */
#define ADDR_BMP_AP		(1 << 3)	/* Infra-BSS Access Point */
#define ADDR_BMP_STA		(1 << 4)	/* Infra-BSS Station */
#define ADDR_BMP_RESERVED1	(1 << 5)
#define ADDR_BMP_RESERVED2	(1 << 6)
#define ADDR_BMP_RESERVED3	(1 << 7)
#define ADDR_BMP_BSS_IDX_MASK	(3 << 8)	/* BSS control block index */
#define ADDR_BMP_BSS_IDX_SHIFT	8

#define	WSEC_MAX_RCMTA_KEYS	54

/* max keys in M_TKMICKEYS_BLK */
#define	WSEC_MAX_TKMIC_ENGINE_KEYS		12	/* 8 + 4 default */

/* max RXE match registers */
#define WSEC_MAX_RXE_KEYS	4

/* SECKINDXALGO (Security Key Index & Algorithm Block) word format */
/* SKL (Security Key Lookup) */
#define	SKL_ALGO_MASK		0x0007
#define	SKL_ALGO_SHIFT		0
#define	SKL_KEYID_MASK		0x0008
#define	SKL_KEYID_SHIFT		3
#define	SKL_INDEX_MASK		0x03F0
#define	SKL_INDEX_SHIFT		4
#define	SKL_GRP_ALGO_MASK	0x1c00
#define	SKL_GRP_ALGO_SHIFT	10

/* additional bits defined for IBSS group key support */
#define	SKL_IBSS_INDEX_MASK	0x01F0
#define	SKL_IBSS_INDEX_SHIFT	4
#define	SKL_IBSS_KEYID1_MASK	0x0600
#define	SKL_IBSS_KEYID1_SHIFT	9
#define	SKL_IBSS_KEYID2_MASK	0x1800
#define	SKL_IBSS_KEYID2_SHIFT	11
#define	SKL_IBSS_KEYALGO_MASK	0xE000
#define	SKL_IBSS_KEYALGO_SHIFT	13

#define	WSEC_MODE_OFF		0
#define	WSEC_MODE_HW		1
#define	WSEC_MODE_SW		2

#define	WSEC_ALGO_OFF		0
#define	WSEC_ALGO_WEP1		1
#define	WSEC_ALGO_TKIP		2
#define	WSEC_ALGO_AES		3
#define	WSEC_ALGO_WEP128	4
#define	WSEC_ALGO_AES_LEGACY	5
#define	WSEC_ALGO_NALG		6

#define	AES_MODE_NONE		0
#define	AES_MODE_CCM		1

/* WEP_CTL (Rev 0) */
#define	WECR0_KEYREG_SHIFT	0
#define	WECR0_KEYREG_MASK	0x7
#define	WECR0_DECRYPT		(1 << 3)
#define	WECR0_IVINLINE		(1 << 4)
#define	WECR0_WEPALG_SHIFT	5
#define	WECR0_WEPALG_MASK	(0x7 << 5)
#define	WECR0_WKEYSEL_SHIFT	8
#define	WECR0_WKEYSEL_MASK	(0x7 << 8)
#define	WECR0_WKEYSTART		(1 << 11)
#define	WECR0_WEPINIT		(1 << 14)
#define	WECR0_ICVERR		(1 << 15)

/* Frame template map byte offsets */
#define	T_ACTS_TPL_BASE		(0)
#define	T_NULL_TPL_BASE		(0xc * 2)
#define	T_QNULL_TPL_BASE	(0x1c * 2)
#define	T_RR_TPL_BASE		(0x2c * 2)
#define	T_BCN0_TPL_BASE		(0x34 * 2)
#define	T_PRS_TPL_BASE		(0x134 * 2)
#define	T_BCN1_TPL_BASE		(0x234 * 2)
#define T_TX_FIFO_TXRAM_BASE	(T_ACTS_TPL_BASE + \
				 (TXFIFO_START_BLK * TXFIFO_SIZE_UNIT))

#define T_BA_TPL_BASE		T_QNULL_TPL_BASE /* template area for BA */

#define T_RAM_ACCESS_SZ		4	/* template ram is 4 byte access only */

/* Shared Mem byte offsets */

/* Location where the ucode expects the corerev */
#define	M_MACHW_VER		(0x00b * 2)

/* Location where the ucode expects the MAC capabilities */
#define	M_MACHW_CAP_L		(0x060 * 2)
#define	M_MACHW_CAP_H	(0x061 * 2)

/* WME shared memory */
#define M_EDCF_STATUS_OFF	(0x007 * 2)
#define M_TXF_CUR_INDEX		(0x018 * 2)
#define M_EDCF_QINFO		(0x120 * 2)

/* PS-mode related parameters */
#define	M_DOT11_SLOT		(0x008 * 2)
#define	M_DOT11_DTIMPERIOD	(0x009 * 2)
#define	M_NOSLPZNATDTIM		(0x026 * 2)

/* Beacon-related parameters */
#define	M_BCN0_FRM_BYTESZ	(0x00c * 2)	/* Bcn 0 template length */
#define	M_BCN1_FRM_BYTESZ	(0x00d * 2)	/* Bcn 1 template length */
#define	M_BCN_TXTSF_OFFSET	(0x00e * 2)
#define	M_TIMBPOS_INBEACON	(0x00f * 2)
#define	M_SFRMTXCNTFBRTHSD	(0x022 * 2)
#define	M_LFRMTXCNTFBRTHSD	(0x023 * 2)
#define	M_BCN_PCTLWD		(0x02a * 2)
#define M_BCN_LI		(0x05b * 2)	/* beacon listen interval */

/* MAX Rx Frame len */
#define M_MAXRXFRM_LEN		(0x010 * 2)

/* ACK/CTS related params */
#define	M_RSP_PCTLWD		(0x011 * 2)

/* Hardware Power Control */
#define M_TXPWR_N		(0x012 * 2)
#define M_TXPWR_TARGET		(0x013 * 2)
#define M_TXPWR_MAX		(0x014 * 2)
#define M_TXPWR_CUR		(0x019 * 2)

/* Rx-related parameters */
#define	M_RX_PAD_DATA_OFFSET	(0x01a * 2)

/* WEP Shared mem data */
#define	M_SEC_DEFIVLOC		(0x01e * 2)
#define	M_SEC_VALNUMSOFTMCHTA	(0x01f * 2)
#define	M_PHYVER		(0x028 * 2)
#define	M_PHYTYPE		(0x029 * 2)
#define	M_SECRXKEYS_PTR		(0x02b * 2)
#define	M_TKMICKEYS_PTR		(0x059 * 2)
#define	M_SECKINDXALGO_BLK	(0x2ea * 2)
#define M_SECKINDXALGO_BLK_SZ	54
#define	M_SECPSMRXTAMCH_BLK	(0x2fa * 2)
#define	M_TKIP_TSC_TTAK		(0x18c * 2)
#define	D11_MAX_KEY_SIZE	16

#define	M_MAX_ANTCNT		(0x02e * 2)	/* antenna swap threshold */

/* Probe response related parameters */
#define	M_SSIDLEN		(0x024 * 2)
#define	M_PRB_RESP_FRM_LEN	(0x025 * 2)
#define	M_PRS_MAXTIME		(0x03a * 2)
#define	M_SSID			(0xb0 * 2)
#define	M_CTXPRS_BLK		(0xc0 * 2)
#define	C_CTX_PCTLWD_POS	(0x4 * 2)

/* Delta between OFDM and CCK power in CCK power boost mode */
#define M_OFDM_OFFSET		(0x027 * 2)

/* TSSI for last 4 11b/g CCK packets transmitted */
#define	M_B_TSSI_0		(0x02c * 2)
#define	M_B_TSSI_1		(0x02d * 2)

/* Host flags to turn on ucode options */
#define	M_HOST_FLAGS1		(0x02f * 2)
#define	M_HOST_FLAGS2		(0x030 * 2)
#define	M_HOST_FLAGS3		(0x031 * 2)
#define	M_HOST_FLAGS4		(0x03c * 2)
#define	M_HOST_FLAGS5		(0x06a * 2)
#define	M_HOST_FLAGS_SZ		16

#define M_RADAR_REG		(0x033 * 2)

/* TSSI for last 4 11a OFDM packets transmitted */
#define	M_A_TSSI_0		(0x034 * 2)
#define	M_A_TSSI_1		(0x035 * 2)

/* noise interference measurement */
#define M_NOISE_IF_COUNT	(0x034 * 2)
#define M_NOISE_IF_TIMEOUT	(0x035 * 2)

#define	M_RF_RX_SP_REG1		(0x036 * 2)

/* TSSI for last 4 11g OFDM packets transmitted */
#define	M_G_TSSI_0		(0x038 * 2)
#define	M_G_TSSI_1		(0x039 * 2)

/* Background noise measure */
#define	M_JSSI_0		(0x44 * 2)
#define	M_JSSI_1		(0x45 * 2)
#define	M_JSSI_AUX		(0x46 * 2)

#define	M_CUR_2050_RADIOCODE	(0x47 * 2)

/* TX fifo sizes */
#define M_FIFOSIZE0		(0x4c * 2)
#define M_FIFOSIZE1		(0x4d * 2)
#define M_FIFOSIZE2		(0x4e * 2)
#define M_FIFOSIZE3		(0x4f * 2)
#define D11_MAX_TX_FRMS		32	/* max frames allowed in tx fifo */

/* Current channel number plus upper bits */
#define M_CURCHANNEL		(0x50 * 2)
#define D11_CURCHANNEL_5G	0x0100;
#define D11_CURCHANNEL_40	0x0200;
#define D11_CURCHANNEL_MAX	0x00FF;

/* last posted frameid on the bcmc fifo */
#define M_BCMC_FID		(0x54 * 2)
#define INVALIDFID		0xffff

/* extended beacon phyctl bytes for 11N */
#define	M_BCN_PCTL1WD		(0x058 * 2)

/* idle busy ratio to duty_cycle requirement  */
#define M_TX_IDLE_BUSY_RATIO_X_16_CCK  (0x52 * 2)
#define M_TX_IDLE_BUSY_RATIO_X_16_OFDM (0x5A * 2)

/* CW RSSI for LCNPHY */
#define M_LCN_RSSI_0		0x1332
#define M_LCN_RSSI_1		0x1338
#define M_LCN_RSSI_2		0x133e
#define M_LCN_RSSI_3		0x1344

/* SNR for LCNPHY */
#define M_LCN_SNR_A_0	0x1334
#define M_LCN_SNR_B_0	0x1336

#define M_LCN_SNR_A_1	0x133a
#define M_LCN_SNR_B_1	0x133c

#define M_LCN_SNR_A_2	0x1340
#define M_LCN_SNR_B_2	0x1342

#define M_LCN_SNR_A_3	0x1346
#define M_LCN_SNR_B_3	0x1348

#define M_LCN_LAST_RESET	(81*2)
#define M_LCN_LAST_LOC	(63*2)
#define M_LCNPHY_RESET_STATUS (4902)
#define M_LCNPHY_DSC_TIME	(0x98d*2)
#define M_LCNPHY_RESET_CNT_DSC (0x98b*2)
#define M_LCNPHY_RESET_CNT	(0x98c*2)

/* Rate table offsets */
#define	M_RT_DIRMAP_A		(0xe0 * 2)
#define	M_RT_BBRSMAP_A		(0xf0 * 2)
#define	M_RT_DIRMAP_B		(0x100 * 2)
#define	M_RT_BBRSMAP_B		(0x110 * 2)

/* Rate table entry offsets */
#define	M_RT_PRS_PLCP_POS	10
#define	M_RT_PRS_DUR_POS	16
#define	M_RT_OFDM_PCTL1_POS	18

#define M_20IN40_IQ			(0x380 * 2)

/* SHM locations where ucode stores the current power index */
#define M_CURR_IDX1		(0x384 * 2)
#define M_CURR_IDX2		(0x387 * 2)

#define M_BSCALE_ANT0	(0x5e * 2)
#define M_BSCALE_ANT1	(0x5f * 2)

/* Antenna Diversity Testing */
#define M_MIMO_ANTSEL_RXDFLT	(0x63 * 2)
#define M_ANTSEL_CLKDIV	(0x61 * 2)
#define M_MIMO_ANTSEL_TXDFLT	(0x64 * 2)

#define M_MIMO_MAXSYM	(0x5d * 2)
#define MIMO_MAXSYM_DEF		0x8000	/* 32k */
#define MIMO_MAXSYM_MAX		0xffff	/* 64k */

#define M_WATCHDOG_8TU		(0x1e * 2)
#define WATCHDOG_8TU_DEF	5
#define WATCHDOG_8TU_MAX	10

/* Manufacturing Test Variables */
/* PER test mode */
#define M_PKTENG_CTRL		(0x6c * 2)
/* IFS for TX mode */
#define M_PKTENG_IFS		(0x6d * 2)
/* Lower word of tx frmcnt/rx lostcnt */
#define M_PKTENG_FRMCNT_LO	(0x6e * 2)
/* Upper word of tx frmcnt/rx lostcnt */
#define M_PKTENG_FRMCNT_HI	(0x6f * 2)

/* Index variation in vbat ripple */
#define M_LCN_PWR_IDX_MAX	(0x67 * 2) /* highest index read by ucode */
#define M_LCN_PWR_IDX_MIN	(0x66 * 2) /* lowest index read by ucode */

/* M_PKTENG_CTRL bit definitions */
#define M_PKTENG_MODE_TX		0x0001
#define M_PKTENG_MODE_TX_RIFS	        0x0004
#define M_PKTENG_MODE_TX_CTS            0x0008
#define M_PKTENG_MODE_RX		0x0002
#define M_PKTENG_MODE_RX_WITH_ACK	0x0402
#define M_PKTENG_MODE_MASK		0x0003
/* TX frames indicated in the frmcnt reg */
#define M_PKTENG_FRMCNT_VLD		0x0100

/* Sample Collect parameters (bitmap and type) */
/* Trigger bitmap for sample collect */
#define M_SMPL_COL_BMP		(0x37d * 2)
/* Sample collect type */
#define M_SMPL_COL_CTL		(0x3b2 * 2)

#define ANTSEL_CLKDIV_4MHZ	6
#define MIMO_ANTSEL_BUSY	0x4000	/* bit 14 (busy) */
#define MIMO_ANTSEL_SEL		0x8000	/* bit 15 write the value */
#define MIMO_ANTSEL_WAIT	50	/* 50us wait */
#define MIMO_ANTSEL_OVERRIDE	0x8000	/* flag */

struct shm_acparams {
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u16 cwcur;
	u16 aifs;
	u16 bslots;
	u16 reggap;
	u16 status;
	u16 rsvd[8];
} __packed;
#define M_EDCF_QLEN	(16 * 2)

#define WME_STATUS_NEWAC	(1 << 8)

/* M_HOST_FLAGS */
#define MHFMAX		5	/* Number of valid hostflag half-word (u16) */
#define MHF1		0	/* Hostflag 1 index */
#define MHF2		1	/* Hostflag 2 index */
#define MHF3		2	/* Hostflag 3 index */
#define MHF4		3	/* Hostflag 4 index */
#define MHF5		4	/* Hostflag 5 index */

/* Flags in M_HOST_FLAGS */
/* Enable ucode antenna diversity help */
#define	MHF1_ANTDIV		0x0001
/* Enable EDCF access control */
#define	MHF1_EDCF		0x0100
#define MHF1_IQSWAP_WAR		0x0200
/* Disable Slow clock request, for corerev < 11 */
#define	MHF1_FORCEFASTCLK	0x0400

/* Flags in M_HOST_FLAGS2 */

/* Flush BCMC FIFO immediately */
#define MHF2_TXBCMC_NOW		0x0040
/* Enable ucode/hw power control */
#define MHF2_HWPWRCTL		0x0080
#define MHF2_NPHY40MHZ_WAR	0x0800

/* Flags in M_HOST_FLAGS3 */
/* enabled mimo antenna selection */
#define MHF3_ANTSEL_EN		0x0001
/* antenna selection mode: 0: 2x3, 1: 2x4 */
#define MHF3_ANTSEL_MODE	0x0002
#define MHF3_RESERVED1		0x0004
#define MHF3_RESERVED2		0x0008
#define MHF3_NPHY_MLADV_WAR	0x0010

/* Flags in M_HOST_FLAGS4 */
/* force bphy Tx on core 0 (board level WAR) */
#define MHF4_BPHY_TXCORE0	0x0080
/* for 4313A0 FEM boards */
#define MHF4_EXTPA_ENABLE	0x4000

/* Flags in M_HOST_FLAGS5 */
#define MHF5_4313_GPIOCTRL	0x0001
#define MHF5_RESERVED1		0x0002
#define MHF5_RESERVED2		0x0004
/* Radio power setting for ucode */
#define	M_RADIO_PWR		(0x32 * 2)

/* phy noise recorded by ucode right after tx */
#define	M_PHY_NOISE		(0x037 * 2)
#define	PHY_NOISE_MASK		0x00ff

/*
 * Receive Frame Data Header for 802.11b DCF-only frames
 *
 * RxFrameSize: Actual byte length of the frame data received
 * PAD: padding (not used)
 * PhyRxStatus_0: PhyRxStatus 15:0
 * PhyRxStatus_1: PhyRxStatus 31:16
 * PhyRxStatus_2: PhyRxStatus 47:32
 * PhyRxStatus_3: PhyRxStatus 63:48
 * PhyRxStatus_4: PhyRxStatus 79:64
 * PhyRxStatus_5: PhyRxStatus 95:80
 * RxStatus1: MAC Rx Status
 * RxStatus2: extended MAC Rx status
 * RxTSFTime: RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY
 * RxChan: gain code, channel radio code, and phy type
 */
struct d11rxhdr_le {
	__le16 RxFrameSize;
	u16 PAD;
	__le16 PhyRxStatus_0;
	__le16 PhyRxStatus_1;
	__le16 PhyRxStatus_2;
	__le16 PhyRxStatus_3;
	__le16 PhyRxStatus_4;
	__le16 PhyRxStatus_5;
	__le16 RxStatus1;
	__le16 RxStatus2;
	__le16 RxTSFTime;
	__le16 RxChan;
} __packed;

struct d11rxhdr {
	u16 RxFrameSize;
	u16 PAD;
	u16 PhyRxStatus_0;
	u16 PhyRxStatus_1;
	u16 PhyRxStatus_2;
	u16 PhyRxStatus_3;
	u16 PhyRxStatus_4;
	u16 PhyRxStatus_5;
	u16 RxStatus1;
	u16 RxStatus2;
	u16 RxTSFTime;
	u16 RxChan;
} __packed;

/* PhyRxStatus_0: */
/* NPHY only: CCK, OFDM, preN, N */
#define	PRXS0_FT_MASK		0x0003
/* NPHY only: clip count adjustment steps by AGC */
#define	PRXS0_CLIP_MASK		0x000C
#define	PRXS0_CLIP_SHIFT	2
/* PHY received a frame with unsupported rate */
#define	PRXS0_UNSRATE		0x0010
/* GPHY: rx ant, NPHY: upper sideband */
#define	PRXS0_RXANT_UPSUBBAND	0x0020
/* CCK frame only: lost crs during cck frame reception */
#define	PRXS0_LCRS		0x0040
/* Short Preamble */
#define	PRXS0_SHORTH		0x0080
/* PLCP violation */
#define	PRXS0_PLCPFV		0x0100
/* PLCP header integrity check failed */
#define	PRXS0_PLCPHCF		0x0200
/* legacy PHY gain control */
#define	PRXS0_GAIN_CTL		0x4000
/* NPHY: Antennas used for received frame, bitmask */
#define PRXS0_ANTSEL_MASK	0xF000
#define PRXS0_ANTSEL_SHIFT	0x12

/* subfield PRXS0_FT_MASK */
#define	PRXS0_CCK		0x0000
/* valid only for G phy, use rxh->RxChan for A phy */
#define	PRXS0_OFDM		0x0001
#define	PRXS0_PREN		0x0002
#define	PRXS0_STDN		0x0003

/* subfield PRXS0_ANTSEL_MASK */
#define PRXS0_ANTSEL_0		0x0	/* antenna 0 is used */
#define PRXS0_ANTSEL_1		0x2	/* antenna 1 is used */
#define PRXS0_ANTSEL_2		0x4	/* antenna 2 is used */
#define PRXS0_ANTSEL_3		0x8	/* antenna 3 is used */

/* PhyRxStatus_1: */
#define	PRXS1_JSSI_MASK		0x00FF
#define	PRXS1_JSSI_SHIFT	0
#define	PRXS1_SQ_MASK		0xFF00
#define	PRXS1_SQ_SHIFT		8

/* nphy PhyRxStatus_1: */
#define PRXS1_nphy_PWR0_MASK	0x00FF
#define PRXS1_nphy_PWR1_MASK	0xFF00

/* HTPHY Rx Status defines */
/* htphy PhyRxStatus_0: those bit are overlapped with PhyRxStatus_0 */
#define PRXS0_BAND	        0x0400	/* 0 = 2.4G, 1 = 5G */
#define PRXS0_RSVD	        0x0800	/* reserved; set to 0 */
#define PRXS0_UNUSED	        0xF000	/* unused and not defined; set to 0 */

/* htphy PhyRxStatus_1: */
/* core enables for {3..0}, 0=disabled, 1=enabled */
#define PRXS1_HTPHY_CORE_MASK	0x000F
/* antenna configation */
#define PRXS1_HTPHY_ANTCFG_MASK	0x00F0
/* Mixmode PLCP Length low byte mask */
#define PRXS1_HTPHY_MMPLCPLenL_MASK	0xFF00

/* htphy PhyRxStatus_2: */
/* Mixmode PLCP Length high byte maskw */
#define PRXS2_HTPHY_MMPLCPLenH_MASK	0x000F
/* Mixmode PLCP rate mask */
#define PRXS2_HTPHY_MMPLCH_RATE_MASK	0x00F0
/* Rx power on core 0 */
#define PRXS2_HTPHY_RXPWR_ANT0	0xFF00

/* htphy PhyRxStatus_3: */
/* Rx power on core 1 */
#define PRXS3_HTPHY_RXPWR_ANT1	0x00FF
/* Rx power on core 2 */
#define PRXS3_HTPHY_RXPWR_ANT2	0xFF00

/* htphy PhyRxStatus_4: */
/* Rx power on core 3 */
#define PRXS4_HTPHY_RXPWR_ANT3	0x00FF
/* Coarse frequency offset */
#define PRXS4_HTPHY_CFO		0xFF00

/* htphy PhyRxStatus_5: */
/* Fine frequency offset */
#define PRXS5_HTPHY_FFO	        0x00FF
/* Advance Retard */
#define PRXS5_HTPHY_AR	        0xFF00

#define HTPHY_MMPLCPLen(rxs) \
	((((rxs)->PhyRxStatus_1 & PRXS1_HTPHY_MMPLCPLenL_MASK) >> 8) | \
	(((rxs)->PhyRxStatus_2 & PRXS2_HTPHY_MMPLCPLenH_MASK) << 8))
/* Get Rx power on core 0 */
#define HTPHY_RXPWR_ANT0(rxs) \
	((((rxs)->PhyRxStatus_2) & PRXS2_HTPHY_RXPWR_ANT0) >> 8)
/* Get Rx power on core 1 */
#define HTPHY_RXPWR_ANT1(rxs) \
	(((rxs)->PhyRxStatus_3) & PRXS3_HTPHY_RXPWR_ANT1)
/* Get Rx power on core 2 */
#define HTPHY_RXPWR_ANT2(rxs) \
	((((rxs)->PhyRxStatus_3) & PRXS3_HTPHY_RXPWR_ANT2) >> 8)

/* ucode RxStatus1: */
#define	RXS_BCNSENT		0x8000
#define	RXS_SECKINDX_MASK	0x07e0
#define	RXS_SECKINDX_SHIFT	5
#define	RXS_DECERR		(1 << 4)
#define	RXS_DECATMPT		(1 << 3)
/* PAD bytes to make IP data 4 bytes aligned */
#define	RXS_PBPRES		(1 << 2)
#define	RXS_RESPFRAMETX		(1 << 1)
#define	RXS_FCSERR		(1 << 0)

/* ucode RxStatus2: */
#define RXS_AMSDU_MASK		1
#define	RXS_AGGTYPE_MASK	0x6
#define	RXS_AGGTYPE_SHIFT	1
#define	RXS_PHYRXST_VALID	(1 << 8)
#define RXS_RXANT_MASK		0x3
#define RXS_RXANT_SHIFT		12

/* RxChan */
#define RXS_CHAN_40		0x1000
#define RXS_CHAN_5G		0x0800
#define	RXS_CHAN_ID_MASK	0x07f8
#define	RXS_CHAN_ID_SHIFT	3
#define	RXS_CHAN_PHYTYPE_MASK	0x0007
#define	RXS_CHAN_PHYTYPE_SHIFT	0

/* Index of attenuations used during ucode power control. */
#define M_PWRIND_BLKS	(0x184 * 2)
#define M_PWRIND_MAP0	(M_PWRIND_BLKS + 0x0)
#define M_PWRIND_MAP1	(M_PWRIND_BLKS + 0x2)
#define M_PWRIND_MAP2	(M_PWRIND_BLKS + 0x4)
#define M_PWRIND_MAP3	(M_PWRIND_BLKS + 0x6)
/* M_PWRIND_MAP(core) macro */
#define M_PWRIND_MAP(core)  (M_PWRIND_BLKS + ((core)<<1))

/* PSM SHM variable offsets */
#define	M_PSM_SOFT_REGS	0x0
#define	M_BOM_REV_MAJOR	(M_PSM_SOFT_REGS + 0x0)
#define	M_BOM_REV_MINOR	(M_PSM_SOFT_REGS + 0x2)
#define	M_UCODE_DBGST	(M_PSM_SOFT_REGS + 0x40) /* ucode debug status code */
#define	M_UCODE_MACSTAT	(M_PSM_SOFT_REGS + 0xE0) /* macstat counters */

#define M_AGING_THRSH	(0x3e * 2) /* max time waiting for medium before tx */
#define	M_MBURST_SIZE	(0x40 * 2) /* max frames in a frameburst */
#define	M_MBURST_TXOP	(0x41 * 2) /* max frameburst TXOP in unit of us */
#define M_SYNTHPU_DLY	(0x4a * 2) /* pre-wakeup for synthpu, default: 500 */
#define	M_PRETBTT	(0x4b * 2)

/* offset to the target txpwr */
#define M_ALT_TXPWR_IDX		(M_PSM_SOFT_REGS + (0x3b * 2))
#define M_PHY_TX_FLT_PTR	(M_PSM_SOFT_REGS + (0x3d * 2))
#define M_CTS_DURATION		(M_PSM_SOFT_REGS + (0x5c * 2))
#define M_LP_RCCAL_OVR		(M_PSM_SOFT_REGS + (0x6b * 2))

/* PKTENG Rx Stats Block */
#define M_RXSTATS_BLK_PTR	(M_PSM_SOFT_REGS + (0x65 * 2))

/* ucode debug status codes */
/* not valid really */
#define	DBGST_INACTIVE		0
/* after zeroing SHM, before suspending at init */
#define	DBGST_INIT		1
/* "normal" state */
#define	DBGST_ACTIVE		2
/* suspended */
#define	DBGST_SUSPENDED		3
/* asleep (PS mode) */
#define	DBGST_ASLEEP		4

/* Scratch Reg defs */
enum _ePsmScratchPadRegDefinitions {
	S_RSV0 = 0,
	S_RSV1,
	S_RSV2,

	/* offset 0x03: scratch registers for Dot11-contants */
	S_DOT11_CWMIN,		/* CW-minimum */
	S_DOT11_CWMAX,		/* CW-maximum */
	S_DOT11_CWCUR,		/* CW-current */
	S_DOT11_SRC_LMT,	/* short retry count limit */
	S_DOT11_LRC_LMT,	/* long retry count limit */
	S_DOT11_DTIMCOUNT,	/* DTIM-count */

	/* offset 0x09: Tx-side scratch registers */
	S_SEQ_NUM,		/* hardware sequence number reg */
	S_SEQ_NUM_FRAG,		/* seq num for frags (at the start of MSDU) */
	S_FRMRETX_CNT,		/* frame retx count */
	S_SSRC,			/* Station short retry count */
	S_SLRC,			/* Station long retry count */
	S_EXP_RSP,		/* Expected response frame */
	S_OLD_BREM,		/* Remaining backoff ctr */
	S_OLD_CWWIN,		/* saved-off CW-cur */
	S_TXECTL,		/* TXE-Ctl word constructed in scr-pad */
	S_CTXTST,		/* frm type-subtype as read from Tx-descr */

	/* offset 0x13: Rx-side scratch registers */
	S_RXTST,		/* Type and subtype in Rxframe */

	/* Global state register */
	S_STREG,		/* state storage actual bit maps below */

	S_TXPWR_SUM,		/* Tx power control: accumulator */
	S_TXPWR_ITER,		/* Tx power control: iteration */
	S_RX_FRMTYPE,		/* Rate and PHY type for frames */
	S_THIS_AGG,		/* Size of this AGG (A-MSDU) */

	S_KEYINDX,
	S_RXFRMLEN,		/* Receive MPDU length in bytes */

	/* offset 0x1B: Receive TSF time stored in SCR */
	S_RXTSFTMRVAL_WD3,	/* TSF value at the start of rx */
	S_RXTSFTMRVAL_WD2,	/* TSF value at the start of rx */
	S_RXTSFTMRVAL_WD1,	/* TSF value at the start of rx */
	S_RXTSFTMRVAL_WD0,	/* TSF value at the start of rx */
	S_RXSSN,		/* Received start seq number for A-MPDU BA */
	S_RXQOSFLD,		/* Rx-QoS field (if present) */

	/* offset 0x21: Scratch pad regs used in microcode as temp storage */
	S_TMP0,			/* stmp0 */
	S_TMP1,			/* stmp1 */
	S_TMP2,			/* stmp2 */
	S_TMP3,			/* stmp3 */
	S_TMP4,			/* stmp4 */
	S_TMP5,			/* stmp5 */
	S_PRQPENALTY_CTR,	/* Probe response queue penalty counter */
	S_ANTCNT,		/* unsuccessful attempts on current ant. */
	S_SYMBOL,		/* flag for possible symbol ctl frames */
	S_RXTP,			/* rx frame type */
	S_STREG2,		/* extra state storage */
	S_STREG3,		/* even more extra state storage */
	S_STREG4,		/* ... */
	S_STREG5,		/* remember to initialize it to zero */

	S_ADJPWR_IDX,
	S_CUR_PTR,		/* Temp pointer for A-MPDU re-Tx SHM table */
	S_REVID4,		/* 0x33 */
	S_INDX,			/* 0x34 */
	S_ADDR0,		/* 0x35 */
	S_ADDR1,		/* 0x36 */
	S_ADDR2,		/* 0x37 */
	S_ADDR3,		/* 0x38 */
	S_ADDR4,		/* 0x39 */
	S_ADDR5,		/* 0x3A */
	S_TMP6,			/* 0x3B */
	S_KEYINDX_BU,		/* Backup for Key index */
	S_MFGTEST_TMP0,		/* Temp regs used for RX test calculations */
	S_RXESN,		/* Received end sequence number for A-MPDU BA */
	S_STREG6,		/* 0x3F */
};

#define S_BEACON_INDX	S_OLD_BREM
#define S_PRS_INDX	S_OLD_CWWIN
#define S_PHYTYPE	S_SSRC
#define S_PHYVER	S_SLRC

/* IHR SLOW_CTRL values */
#define SLOW_CTRL_PDE		(1 << 0)
#define SLOW_CTRL_FD		(1 << 8)

/* ucode mac statistic counters in shared memory */
struct macstat {
	u16 txallfrm;	/* 0x80 */
	u16 txrtsfrm;	/* 0x82 */
	u16 txctsfrm;	/* 0x84 */
	u16 txackfrm;	/* 0x86 */
	u16 txdnlfrm;	/* 0x88 */
	u16 txbcnfrm;	/* 0x8a */
	u16 txfunfl[8];	/* 0x8c - 0x9b */
	u16 txtplunfl;	/* 0x9c */
	u16 txphyerr;	/* 0x9e */
	u16 pktengrxducast;	/* 0xa0 */
	u16 pktengrxdmcast;	/* 0xa2 */
	u16 rxfrmtoolong;	/* 0xa4 */
	u16 rxfrmtooshrt;	/* 0xa6 */
	u16 rxinvmachdr;	/* 0xa8 */
	u16 rxbadfcs;	/* 0xaa */
	u16 rxbadplcp;	/* 0xac */
	u16 rxcrsglitch;	/* 0xae */
	u16 rxstrt;		/* 0xb0 */
	u16 rxdfrmucastmbss;	/* 0xb2 */
	u16 rxmfrmucastmbss;	/* 0xb4 */
	u16 rxcfrmucast;	/* 0xb6 */
	u16 rxrtsucast;	/* 0xb8 */
	u16 rxctsucast;	/* 0xba */
	u16 rxackucast;	/* 0xbc */
	u16 rxdfrmocast;	/* 0xbe */
	u16 rxmfrmocast;	/* 0xc0 */
	u16 rxcfrmocast;	/* 0xc2 */
	u16 rxrtsocast;	/* 0xc4 */
	u16 rxctsocast;	/* 0xc6 */
	u16 rxdfrmmcast;	/* 0xc8 */
	u16 rxmfrmmcast;	/* 0xca */
	u16 rxcfrmmcast;	/* 0xcc */
	u16 rxbeaconmbss;	/* 0xce */
	u16 rxdfrmucastobss;	/* 0xd0 */
	u16 rxbeaconobss;	/* 0xd2 */
	u16 rxrsptmout;	/* 0xd4 */
	u16 bcntxcancl;	/* 0xd6 */
	u16 PAD;
	u16 rxf0ovfl;	/* 0xda */
	u16 rxf1ovfl;	/* 0xdc */
	u16 rxf2ovfl;	/* 0xde */
	u16 txsfovfl;	/* 0xe0 */
	u16 pmqovfl;		/* 0xe2 */
	u16 rxcgprqfrm;	/* 0xe4 */
	u16 rxcgprsqovfl;	/* 0xe6 */
	u16 txcgprsfail;	/* 0xe8 */
	u16 txcgprssuc;	/* 0xea */
	u16 prs_timeout;	/* 0xec */
	u16 rxnack;
	u16 frmscons;
	u16 txnack;
	u16 txglitch_nack;
	u16 txburst;		/* 0xf6 # tx bursts */
	u16 bphy_rxcrsglitch;	/* bphy rx crs glitch */
	u16 phywatchdog;	/* 0xfa # of phy watchdog events */
	u16 PAD;
	u16 bphy_badplcp;	/* bphy bad plcp */
};

/* dot11 core-specific control flags */
#define	SICF_PCLKE		0x0004	/* PHY clock enable */
#define	SICF_PRST		0x0008	/* PHY reset */
#define	SICF_MPCLKE		0x0010	/* MAC PHY clockcontrol enable */
#define	SICF_FREF		0x0020	/* PLL FreqRefSelect */
/* NOTE: the following bw bits only apply when the core is attached
 * to a NPHY
 */
#define	SICF_BWMASK		0x00c0	/* phy clock mask (b6 & b7) */
#define	SICF_BW40		0x0080	/* 40MHz BW (160MHz phyclk) */
#define	SICF_BW20		0x0040	/* 20MHz BW (80MHz phyclk) */
#define	SICF_BW10		0x0000	/* 10MHz BW (40MHz phyclk) */
#define	SICF_GMODE		0x2000	/* gmode enable */

/* dot11 core-specific status flags */
#define	SISF_2G_PHY		0x0001	/* 2.4G capable phy */
#define	SISF_5G_PHY		0x0002	/* 5G capable phy */
#define	SISF_FCLKA		0x0004	/* FastClkAvailable */
#define	SISF_DB_PHY		0x0008	/* Dualband phy */

/* === End of MAC reg, Beginning of PHY(b/a/g/n) reg === */
/* radio and LPPHY regs are separated */

#define	BPHY_REG_OFT_BASE	0x0
/* offsets for indirect access to bphy registers */
#define	BPHY_BB_CONFIG		0x01
#define	BPHY_ADCBIAS		0x02
#define	BPHY_ANACORE		0x03
#define	BPHY_PHYCRSTH		0x06
#define	BPHY_TEST		0x0a
#define	BPHY_PA_TX_TO		0x10
#define	BPHY_SYNTH_DC_TO	0x11
#define	BPHY_PA_TX_TIME_UP	0x12
#define	BPHY_RX_FLTR_TIME_UP	0x13
#define	BPHY_TX_POWER_OVERRIDE	0x14
#define	BPHY_RF_OVERRIDE	0x15
#define	BPHY_RF_TR_LOOKUP1	0x16
#define	BPHY_RF_TR_LOOKUP2	0x17
#define	BPHY_COEFFS		0x18
#define	BPHY_PLL_OUT		0x19
#define	BPHY_REFRESH_MAIN	0x1a
#define	BPHY_REFRESH_TO0	0x1b
#define	BPHY_REFRESH_TO1	0x1c
#define	BPHY_RSSI_TRESH		0x20
#define	BPHY_IQ_TRESH_HH	0x21
#define	BPHY_IQ_TRESH_H		0x22
#define	BPHY_IQ_TRESH_L		0x23
#define	BPHY_IQ_TRESH_LL	0x24
#define	BPHY_GAIN		0x25
#define	BPHY_LNA_GAIN_RANGE	0x26
#define	BPHY_JSSI		0x27
#define	BPHY_TSSI_CTL		0x28
#define	BPHY_TSSI		0x29
#define	BPHY_TR_LOSS_CTL	0x2a
#define	BPHY_LO_LEAKAGE		0x2b
#define	BPHY_LO_RSSI_ACC	0x2c
#define	BPHY_LO_IQMAG_ACC	0x2d
#define	BPHY_TX_DC_OFF1		0x2e
#define	BPHY_TX_DC_OFF2		0x2f
#define	BPHY_PEAK_CNT_THRESH	0x30
#define	BPHY_FREQ_OFFSET	0x31
#define	BPHY_DIVERSITY_CTL	0x32
#define	BPHY_PEAK_ENERGY_LO	0x33
#define	BPHY_PEAK_ENERGY_HI	0x34
#define	BPHY_SYNC_CTL		0x35
#define	BPHY_TX_PWR_CTRL	0x36
#define BPHY_TX_EST_PWR		0x37
#define	BPHY_STEP		0x38
#define	BPHY_WARMUP		0x39
#define	BPHY_LMS_CFF_READ	0x3a
#define	BPHY_LMS_COEFF_I	0x3b
#define	BPHY_LMS_COEFF_Q	0x3c
#define	BPHY_SIG_POW		0x3d
#define	BPHY_RFDC_CANCEL_CTL	0x3e
#define	BPHY_HDR_TYPE		0x40
#define	BPHY_SFD_TO		0x41
#define	BPHY_SFD_CTL		0x42
#define	BPHY_DEBUG		0x43
#define	BPHY_RX_DELAY_COMP	0x44
#define	BPHY_CRS_DROP_TO	0x45
#define	BPHY_SHORT_SFD_NZEROS	0x46
#define	BPHY_DSSS_COEFF1	0x48
#define	BPHY_DSSS_COEFF2	0x49
#define	BPHY_CCK_COEFF1		0x4a
#define	BPHY_CCK_COEFF2		0x4b
#define	BPHY_TR_CORR		0x4c
#define	BPHY_ANGLE_SCALE	0x4d
#define	BPHY_TX_PWR_BASE_IDX	0x4e
#define	BPHY_OPTIONAL_MODES2	0x4f
#define	BPHY_CCK_LMS_STEP	0x50
#define	BPHY_BYPASS		0x51
#define	BPHY_CCK_DELAY_LONG	0x52
#define	BPHY_CCK_DELAY_SHORT	0x53
#define	BPHY_PPROC_CHAN_DELAY	0x54
#define	BPHY_DDFS_ENABLE	0x58
#define	BPHY_PHASE_SCALE	0x59
#define	BPHY_FREQ_CONTROL	0x5a
#define	BPHY_LNA_GAIN_RANGE_10	0x5b
#define	BPHY_LNA_GAIN_RANGE_32	0x5c
#define	BPHY_OPTIONAL_MODES	0x5d
#define	BPHY_RX_STATUS2		0x5e
#define	BPHY_RX_STATUS3		0x5f
#define	BPHY_DAC_CONTROL	0x60
#define	BPHY_ANA11G_FILT_CTRL	0x62
#define	BPHY_REFRESH_CTRL	0x64
#define	BPHY_RF_OVERRIDE2	0x65
#define	BPHY_SPUR_CANCEL_CTRL	0x66
#define	BPHY_FINE_DIGIGAIN_CTRL	0x67
#define	BPHY_RSSI_LUT		0x88
#define	BPHY_RSSI_LUT_END	0xa7
#define	BPHY_TSSI_LUT		0xa8
#define	BPHY_TSSI_LUT_END	0xc7
#define	BPHY_TSSI2PWR_LUT	0x380
#define	BPHY_TSSI2PWR_LUT_END	0x39f
#define	BPHY_LOCOMP_LUT		0x3a0
#define	BPHY_LOCOMP_LUT_END	0x3bf
#define	BPHY_TXGAIN_LUT		0x3c0
#define	BPHY_TXGAIN_LUT_END	0x3ff

/* Bits in BB_CONFIG: */
#define	PHY_BBC_ANT_MASK	0x0180
#define	PHY_BBC_ANT_SHIFT	7
#define	BB_DARWIN		0x1000
#define BBCFG_RESETCCA		0x4000
#define BBCFG_RESETRX		0x8000

/* Bits in phytest(0x0a): */
#define	TST_DDFS		0x2000
#define	TST_TXFILT1		0x0800
#define	TST_UNSCRAM		0x0400
#define	TST_CARR_SUPP		0x0200
#define	TST_DC_COMP_LOOP	0x0100
#define	TST_LOOPBACK		0x0080
#define	TST_TXFILT0		0x0040
#define	TST_TXTEST_ENABLE	0x0020
#define	TST_TXTEST_RATE		0x0018
#define	TST_TXTEST_PHASE	0x0007

/* phytest txTestRate values */
#define	TST_TXTEST_RATE_1MBPS	0
#define	TST_TXTEST_RATE_2MBPS	1
#define	TST_TXTEST_RATE_5_5MBPS	2
#define	TST_TXTEST_RATE_11MBPS	3
#define	TST_TXTEST_RATE_SHIFT	3

#define SHM_BYT_CNT	0x2	/* IHR location */
#define MAX_BYT_CNT	0x600	/* Maximum frame len */

struct d11cnt {
	u32 txfrag;
	u32 txmulti;
	u32 txfail;
	u32 txretry;
	u32 txretrie;
	u32 rxdup;
	u32 txrts;
	u32 txnocts;
	u32 txnoack;
	u32 rxfrag;
	u32 rxmulti;
	u32 rxcrc;
	u32 txfrmsnt;
	u32 rxundec;
};

#endif				/* _BRCM_D11_H_ */
