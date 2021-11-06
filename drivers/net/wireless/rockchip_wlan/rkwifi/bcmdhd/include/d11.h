/*
 * Chip-specific hardware definitions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#ifndef	_D11_H
#define	_D11_H

/*
 * Notes:
 * 1. pre40/pre rev40: corerev < 40
 * 2. pre80/pre rev80: 40 <= corerev < 80
 * 3. rev40/D11AC: 80 > corerev >= 40
 * 4. rev80: corerev >= 80
 */

#include <typedefs.h>
#include <hndsoc.h>
#include <sbhnddma.h>
#include <802.11.h>

#if defined(BCMDONGLEHOST) || defined(WL_UNITTEST)
typedef struct {
	uint32 pad;
} shmdefs_t;
#else	/* defined(BCMDONGLEHOST)|| defined(WL_UNITTEST) */
#include <d11shm.h>
#ifdef USE_BCMCONF_H
#include <bcmconf.h>
#else
#include <wlc_cfg.h>
#endif
#endif /* !defined(BCMDONGLEHOST)|| !defined(WL_UNITTEST) */

#include <d11regs.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* cpp contortions to concatenate w/arg prescan */
#ifndef	PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

#define	D11AC_BCN_TMPL_LEN	640	/**< length of the BCN template area for 11AC */

#define LPRS_TMPL_LEN		512	/**< length of the legacy PRS template area */

/* RX FIFO numbers */
#define	RX_FIFO			0	/**< data and ctl frames */
#define	RX_FIFO1		1	/**< ctl frames */
#define RX_FIFO2		2	/**< ctl frames */
#define RX_FIFO_NUMBER		3

/* TX FIFO numbers using WME Access Classes */
#define	TX_AC_BK_FIFO		0	/**< Access Category Background TX FIFO */
#define	TX_AC_BE_FIFO		1	/**< Access Category Best-Effort TX FIFO */
#define	TX_AC_VI_FIFO		2	/**< Access Class Video TX FIFO */
#define	TX_AC_VO_FIFO		3	/**< Access Class Voice TX FIFO */
#define	TX_BCMC_FIFO		4	/**< Broadcast/Multicast TX FIFO */
#define	TX_ATIM_FIFO		5	/**< TX fifo for ATIM window info */
#define	TX_AC_N_DATA_FIFO	4	/**< Number of legacy Data Fifos (BK, BE, VI, VO) */

/* TX FIFO numbers for trigger queues for HE STA only chips (i.e
 * This is valid only for 4369 or similar STA chips that supports
 * a single HE STA connection.
 */
#define	TX_TRIG_BK_FIFO		6	/**< Access Category Background TX FIFO */
#define	TX_TRIG_BE_FIFO		7	/**< Access Category Best-Effort TX FIFO */
#define	TX_TRIG_VI_FIFO		8	/**< Access Class Video TX FIFO */
#define	TX_TRIG_VO_FIFO		9	/**< Access Class Voice TX FIFO */
#define	TX_TRIG_HP_FIFO		10	/**< Access High Priority TX FIFO */
#define	TX_TRIG_N_DATA_FIFO	4	/**< Number of Trigger Data Fifos (BK, BE, VI, VO) */

#if defined(WL11AX_TRIGGERQ) && !defined(WL11AX_TRIGGERQ_DISABLED)
#define IS_TRIG_FIFO(fifo) \
	(((fifo) >= TX_TRIG_BK_FIFO) && ((fifo) < (TX_TRIG_BK_FIFO + TX_TRIG_N_DATA_FIFO)))
#else
#define IS_TRIG_FIFO(fifo) FALSE
#endif /* defined(WL11AX_TRIGGERQ) && !defined(WL11AX_TRIGGERQ_DISABLED) */

#define IS_AC_FIFO(fifo) \
	((fifo) < (TX_AC_BK_FIFO + TX_AC_N_DATA_FIFO))

/** Legacy TX FIFO numbers */
#define	TX_DATA_FIFO		TX_AC_BE_FIFO
#define	TX_CTL_FIFO		TX_AC_VO_FIFO

/** Trig TX FIFO numbers */
#define	TX_TRIG_DATA_FIFO	TX_TRIG_BE_FIFO
#define	TX_TRIG_CTL_FIFO	TX_TRIG_VO_FIFO

/* Extended FIFOs for corerev >= 64 */
#define TX_FIFO_6		6
#define TX_FIFO_7		7
#define TX_FIFO_16		16
#define TX_FIFO_23		23
#define TX_FIFO_25		25

#define TX_FIFO_EXT_START	TX_FIFO_6	/* Starting index of extendied HW TX FIFOs */
#define TX_FIFO_MU_START	8		/* index at which MU TX FIFOs start */

#define D11REG_IHR_WBASE	0x200
#define D11REG_IHR_BASE		(D11REG_IHR_WBASE << 1)

#define	PIHR_BASE	0x0400		/**< byte address of packed IHR region */

/* biststatus */
#define	BT_DONE		(1U << 31)	/**< bist done */
#define	BT_B2S		(1 << 30)	/**< bist2 ram summary bit */

/* DMA intstatus and intmask */
#define	I_PC		(1 << 10)	/**< pci descriptor error */
#define	I_PD		(1 << 11)	/**< pci data error */
#define	I_DE		(1 << 12)	/**< descriptor protocol error */
#define	I_RU		(1 << 13)	/**< receive descriptor underflow */
#define	I_RO		(1 << 14)	/**< receive fifo overflow */
#define	I_XU		(1 << 15)	/**< transmit fifo underflow */
#define	I_RI		(1 << 16)	/**< receive interrupt */
#define	I_XI		(1 << 24)	/**< transmit interrupt */

/* interrupt receive lazy */
#define	IRL_TO_MASK		0x00ffffff	/**< timeout */
#define	IRL_FC_MASK		0xff000000	/**< frame count */
#define	IRL_FC_SHIFT		24		/**< frame count */
#define	IRL_DISABLE		0x01000000	/**< Disabled value: int on 1 frame, zero time */

/** for correv >= 80. prev rev uses bit 21 */
#define	MCTL_BCNS_PROMISC_SHIFT	21
/** for correv < 80. prev rev uses bit 20 */
#define	MCTL_BCNS_PROMISC_SHIFT_LT80	20

/* maccontrol register */
#define	MCTL_GMODE		(1U << 31)
#define	MCTL_DISCARD_PMQ	(1 << 30)
#define	MCTL_DISCARD_TXSTATUS	(1 << 29)
#define	MCTL_TBTT_HOLD		(1 << 28)
#define	MCTL_CLOSED_NETWORK	(1 << 27)
#define	MCTL_WAKE		(1 << 26)
#define	MCTL_HPS		(1 << 25)
#define	MCTL_PROMISC		(1 << 24)
#define	MCTL_KEEPBADFCS		(1 << 23)
#define	MCTL_KEEPCONTROL	(1 << 22)
#define	MCTL_BCNS_PROMISC	(1 << MCTL_BCNS_PROMISC_SHIFT)
#define	MCTL_BCNS_PROMISC_LT80	(1 << MCTL_BCNS_PROMISC_SHIFT_LT80)
#define	MCTL_NO_TXDMA_LAST_PTR	(1 << 20)	/** for correv >= 85 */
#define	MCTL_LOCK_RADIO		(1 << 19)
#define	MCTL_AP			(1 << 18)
#define	MCTL_INFRA		(1 << 17)
#define	MCTL_BIGEND		(1 << 16)
#define	MCTL_DISABLE_CT		(1 << 14)   /** for corerev >= 83.1 */
#define	MCTL_GPOUT_SEL_MASK	(3 << 14)
#define	MCTL_GPOUT_SEL_SHIFT	14
#define	MCTL_EN_PSMDBG		(1 << 13)
#define	MCTL_IHR_EN		(1 << 10)
#define	MCTL_SHM_UPPER		(1 <<  9)
#define	MCTL_SHM_EN		(1 <<  8)
#define	MCTL_PSM_JMP_0		(1 <<  2)
#define	MCTL_PSM_RUN		(1 <<  1)
#define	MCTL_EN_MAC		(1 <<  0)

/* maccontrol1 register */
#define MCTL1_GCPS			(1u << 0u)
#define MCTL1_EGS_MASK			0x0000c000
#define MCTL1_EGS_SHIFT			14u
#define MCTL1_AVB_ENABLE		(1u << 1u)
#define MCTL1_GPIOSEL_SHIFT		8u
#define MCTL1_GPIOSEL			(0x3F)
#define MCTL1_GPIOSEL_MASK		(MCTL1_GPIOSEL << MCTL1_GPIOSEL_SHIFT)
/* Select MAC_SMPL_CPTR debug data that is placed in pc<7:1> & ifs_gpio_out<8:0> GPIOs */
#define MCTL1_GPIOSEL_TSF_PC_IFS(_corerev)	(D11REV_GE(_corerev, 85) ? 0x3b : 0x36)
#define MCTL1_AVB_TRIGGER		(1u << 2u)
#define MCTL1_THIRD_AXI1_FOR_PSM	(1u << 3u)
#define MCTL1_AXI1_FOR_RX		(1u << 4u)
#define MCTL1_TXDMA_ENABLE_PASS		(1u << 5u)
/* SampleCollectPlayCtrl */
#define SC_PLAYCTRL_MASK_ENABLE		(1u << 8u)
#define SC_PLAYCTRL_TRANS_MODE		(1u << 6u)
#define SC_PLAYCTRL_SRC_SHIFT		3u
#define SC_PLAYCTRL_SRC_MASK		(3u << SC_PLAYCTRL_SRC_SHIFT)
#define SC_PLAYCTRL_SRC_PHY_DBG		(3u << SC_PLAYCTRL_SRC_SHIFT)
#define SC_PLAYCTRL_SRC_GPIO_OUT	(2u << SC_PLAYCTRL_SRC_SHIFT)
#define SC_PLAYCTRL_SRC_GPIO_IN		(1u << SC_PLAYCTRL_SRC_SHIFT)
#define SC_PLAYCTRL_SRC_PHY_SMPL	(0u << SC_PLAYCTRL_SRC_SHIFT)
#define SC_PLAYCTRL_STOP		(1u << 2u)
#define SC_PLAYCTRL_PAUSE		(1u << 1u)
#define SC_PLAYCTRL_START		(1u << 0u)
/* SCPortalSel fields */
#define SC_PORTAL_SEL_AUTO_INCR		(1u << 15u)	/* Autoincr */
#define SC_PORTAL_SEL_STORE_MASK	(0u << 5u)	/* Bits 14:5 SCStoreMask15to0 */
#define SC_PORTAL_SEL_MATCH_MASK	(4u << 5u)	/* Bits 14:5 SCMatchMask15to0 */
#define SC_PORTAL_SEL_MATCH_VALUE	(8u << 5u)	/* Bits 14:5 SCMatchValue15to0 */
#define SC_PORTAL_SEL_TRIGGER_MASK	(12u << 0u)	/* Bits 4:0 SCTriggerMask15to0 */
#define SC_PORTAL_SEL_TRIGGER_VALUE	(16u << 0u)	/* Bits 4:0 SCTriggerValue15to0 */
#define SC_PORTAL_SEL_TRANS_MASK	(20u << 0u)	/* Bits 4:0 SCTransMask15to0 */

/* GpioOut register */
#define MGPIO_OUT_RXQ1_IFIFO_CNT_MASK	0x1fc0u
#define MGPIO_OUT_RXQ1_IFIFO_CNT_SHIFT	6u

#define MAC_RXQ1_IFIFO_CNT_ADDR	  0x26u
#define MAC_RXQ1_IFIFO_MAXLEN	  3u

/* maccommand register */
#define	MCMD_BCN0VLD		(1 <<  0)
#define	MCMD_BCN1VLD		(1 <<  1)
#define	MCMD_DIRFRMQVAL		(1 <<  2)
#define	MCMD_CCA		(1 <<  3)
#define	MCMD_BG_NOISE		(1 <<  4)
#define	MCMD_SKIP_SHMINIT	(1 <<  5)	/**< only used for simulation */
#define MCMD_SLOWCAL		(1 <<  6)
#define MCMD_SAMPLECOLL		MCMD_SKIP_SHMINIT	/**< reuse for sample collect */
#define MCMD_IF_DOWN		(1 << 8 )	/**< indicate interface is going down  */
#define MCMD_TOF		(1 << 9) /**< wifi ranging processing in ucode for rxd frames */
#define MCMD_TSYNC		(1 << 10) /**< start timestamp sync process in ucode */
#define MCMD_RADIO_DOWN		(1 << 11) /**< radio down by ucode */
#define MCMD_RADIO_UP		(1 << 12) /**< radio up by ucode */
#define MCMD_TXPU		(1 << 13) /**< txpu control by ucode */

/* macintstatus/macintmask */
#define	MI_MACSSPNDD     (1 <<  0)	/**< MAC has gracefully suspended */
#define	MI_BCNTPL        (1 <<  1)	/**< beacon template available */
#define	MI_TBTT          (1 <<  2)	/**< TBTT indication */
#define	MI_BCNSUCCESS    (1 <<  3)	/**< beacon successfully tx'd */
#define	MI_BCNCANCLD     (1 <<  4)	/**< beacon canceled (IBSS) */
#define	MI_ATIMWINEND    (1 <<  5)	/**< end of ATIM-window (IBSS) */
#define	MI_PMQ           (1 <<  6)	/**< PMQ entries available */
#define	MI_ALTTFS        (1 <<  7)	/**< TX status interrupt for ARM offloads */
#define	MI_NSPECGEN_1    (1 <<  8)	/**< non-specific gen-stat bits that are set by PSM */
#define	MI_MACTXERR      (1 <<  9)	/**< MAC level Tx error */
#define MI_PMQERR        (1 << 10)
#define	MI_PHYTXERR      (1 << 11)	/**< PHY Tx error */
#define	MI_PME           (1 << 12)	/**< Power Management Event */
#define	MI_GP0           (1 << 13)	/**< General-purpose timer0 */
#define	MI_GP1           (1 << 14)	/**< General-purpose timer1 */
#define	MI_DMAINT        (1 << 15)	/**< (ORed) DMA-interrupts */
#define	MI_TXSTOP        (1 << 16)	/**< MAC has completed a TX FIFO Suspend/Flush */
#define	MI_CCA           (1 << 17)	/**< MAC has completed a CCA measurement */
#define	MI_BG_NOISE      (1 << 18)	/**< MAC has collected background noise samples */
#define	MI_DTIM_TBTT     (1 << 19)	/**< MBSS DTIM TBTT indication */
#define MI_PRQ           (1 << 20)	/**< Probe response queue needs attention */
#define	MI_HEB           (1 << 21)	/**< HEB (Hardware Event Block) interrupt - 11ax cores */
#define	MI_BT_RFACT_STUCK	(1 << 22)	/**< MAC has detected invalid BT_RFACT pin,
						 * valid when rev < 15
						 */
#define MI_TTTT          (1 << 22)	/**< Target TIM Transmission Time,
						 * valid in rev = 26/29, or rev >= 42
						 */
#define	MI_BT_PRED_REQ   (1 << 23)	/**< MAC requested driver BTCX predictor calc */
#define	MI_BCNTRIM_RX	 (1 << 24)	/**< PSM received a partial beacon */
#define MI_P2P           (1 << 25)	/**< WiFi P2P interrupt */
#define MI_DMATX         (1 << 26)	/**< MAC new frame ready */
#define MI_TSSI_LIMIT    (1 << 27)	/**< Tssi Limit Reach, TxIdx=0/127 Interrupt */
#define MI_HWACI_NOTIFY  (1 << 27)	/**< HWACI detects ACI, Apply Mitigation settings */
#define MI_RFDISABLE     (1 << 28)	/**< MAC detected a change on RF Disable input
						 * (corerev >= 10)
						 */
#define	MI_TFS           (1 << 29)	/**< MAC has completed a TX (corerev >= 5) */
#define	MI_LEGACY_BUS_ERROR	(1 << 30)	/**< uCode indicated bus error */
#define	MI_TO            (1U << 31)	/**< general purpose timeout (corerev >= 3) */

#define MI_RXOV                 MI_NSPECGEN_1   /**< rxfifo overflow interrupt */

/* macintstatus_ext/macintmask_ext */
#define	MI_BUS_ERROR		(1U << 0u)	/**< uCode indicated bus error */
#define	MI_VCOPLL		(1U << 1u)	/**< uCode indicated PLL lock issue */
#define	MI_EXT_PS_CHG		(1U << 2u)	/**< Power state is changing (PS 0 <-> 1) */
#define MI_DIS_ULOFDMA		(1U << 3u)	/**< ucode indicated disabling ULOFDMA request */
#define	MI_EXT_PM_OFFLOAD	(1U << 4u)	/**< PM offload */
#define MI_OBSS_INTR		(1U << 5u)	/**< OBSS detection interrupt */
#define MI_SENSORC_CX_REQ	(1U << 6u)	/**< SensorC Mitigation Request interrupt */
#define MI_RLL_NAV_HOF		(1U << 7u)	/**< RLLW Switch */

#define MI_EXT_TXE_SHARED_ERR  (1U << 28u)     /* Error event in blocks inside TXE shared
						* (BMC/AQM/AQM-DMA/MIF)
						*/

/* Mac capabilities registers */
#define	MCAP_TKIPMIC		0x80000000	/**< TKIP MIC hardware present */
#define	MCAP_TKIPPH2KEY		0x40000000	/**< TKIP phase 2 key hardware present */
#define	MCAP_BTCX		0x20000000	/**< BT coexistence hardware and pins present */
#define	MCAP_MBSS		0x10000000	/**< Multi-BSS hardware present */
#define	MCAP_RXFSZ_MASK		0x0ff80000	/**< Rx fifo size in blocks (revid >= 16) */
#define	MCAP_RXFSZ_SHIFT	19
#define	MCAP_NRXQ_MASK		0x00070000	/**< Max Rx queues supported - 1 */
#define	MCAP_NRXQ_SHIFT		16
#define	MCAP_UCMSZ_MASK		0x0000e000	/**< Ucode memory size */
#define	MCAP_UCMSZ_3K3		0		/**< 3328 Words Ucode memory, in unit of 50-bit */
#define	MCAP_UCMSZ_4K		1		/**< 4096 Words Ucode memory */
#define	MCAP_UCMSZ_5K		2		/**< 5120 Words Ucode memory */
#define	MCAP_UCMSZ_6K		3		/**< 6144 Words Ucode memory */
#define	MCAP_UCMSZ_8K		4		/**< 8192 Words Ucode memory */
#define	MCAP_UCMSZ_SHIFT	13
#define	MCAP_TXFSZ_MASK		0x00000ff8	/**< Tx fifo size (* 512 bytes) */
#define	MCAP_TXFSZ_SHIFT	3
#define	MCAP_NTXQ_MASK		0x00000007	/**< Max Tx queues supported - 1 */
#define	MCAP_NTXQ_SHIFT		0

#define	MCAP_BTCX_SUP(corerev)	(MCAP_BTCX)

#define	MCAP_UCMSZ_TYPES	8		/**< different Ucode memory size types */

/* machwcap1 */
#define	MCAP1_ERC_MASK		0x00000001	/**< external radio coexistence */
#define	MCAP1_ERC_SHIFT		0
#define	MCAP1_SHMSZ_MASK	0x0000000e	/**< shm size (corerev >= 16) */
#define	MCAP1_SHMSZ_SHIFT	1
#define MCAP1_SHMSZ_1K		0		/**< 1024 words in unit of 32-bit */
#define MCAP1_SHMSZ_2K		1		/**< 1536 words in unit of 32-bit */
#define MCAP1_NUMMACCHAINS	0x00003000	/**< Indicates one less than the
							number of MAC Chains in the MAC.
							*/
#define MCAP1_NUMMACCHAINS_SHIFT	12
#define MCAP1_RXBLMAX_MASK	0x1800000u
#define MCAP1_RXBLMAX_SHIFT	23u
#define MCAP1_NUM_HEB_MASK	0xE0000000u
#define MCAP1_NUM_HEB_SHIFT	29u
#define MCAP1_NUM_HEB_FACTOR	3u
#define MCAP1_CT_CAPABLE_SHIFT	17

/* BTCX control */
#define BTCX_CTRL_EN		0x0001	/**< Enable BTCX module */
#define BTCX_CTRL_SW		0x0002	/**< Enable software override */
#define BTCX_CTRL_DSBLBTCXOUT	0x8000 /* Disable txconf/prisel signal output from btcx module */

#define BTCX_CTRL_PRI_POL	0x0080  /* Invert prisel polarity */
#define BTCX_CTRL_TXC_POL	0x0020  /* Invert txconf polarity */

#define SW_PRI_ON		1	/* switch prisel polarity */
#define SW_TXC_ON		2	/* switch txconf polarity */

/* BTCX status */
#define BTCX_STAT_RA		0x0001	/**< RF_ACTIVE state */

/* BTCX transaction control */
#define BTCX_TRANS_ANTSEL	0x0040	/**< ANTSEL output */
#define BTCX_TRANS_TXCONF	0x0080	/**< TX_CONF output */

/* pmqhost data */
#define	PMQH_DATA_MASK		0xffff0000	/**< data entry of head pmq entry */
#define	PMQH_BSSCFG		0x00100000	/**< PM entry for BSS config */
#define	PMQH_PMOFF		0x00010000	/**< PM Mode OFF: power save off */
#define	PMQH_PMON		0x00020000	/**< PM Mode ON: power save on */
#define	PMQH_PMPS		0x00200000	/**< PM Mode PRETEND */
#define	PMQH_DASAT		0x00040000	/**< Dis-associated or De-authenticated */
#define	PMQH_ATIMFAIL		0x00080000	/**< ATIM not acknowledged */
#define	PMQH_DEL_ENTRY		0x00000001	/**< delete head entry */
#define	PMQH_DEL_MULT		0x00000002	/**< delete head entry to cur read pointer -1 */
#define	PMQH_OFLO		0x00000004	/**< pmq overflow indication */
#define	PMQH_NOT_EMPTY		0x00000008	/**< entries are present in pmq */

/* phydebug (corerev >= 3) */
#define	PDBG_CRS		(1 << 0)  /**< phy is asserting carrier sense */
#define	PDBG_TXA		(1 << 1)  /**< phy is taking xmit byte from mac this cycle */
#define	PDBG_TXF		(1 << 2)  /**< mac is instructing the phy to transmit a frame */
#define	PDBG_TXE		(1 << 3)  /**< phy is signaling a transmit Error to the mac */
#define	PDBG_RXF		(1 << 4)  /**< phy detected the end of a valid frame preamble */
#define	PDBG_RXS		(1 << 5)  /**< phy detected the end of a valid PLCP header */
#define	PDBG_RXFRG		(1 << 6)  /**< rx start not asserted */
#define	PDBG_RXV		(1 << 7)  /**< mac is taking receive byte from phy this cycle */
#define	PDBG_RFD		(1 << 16) /**< RF portion of the radio is disabled */

/* objaddr register */
#define	OBJADDR_UCM_SEL		0x00000000
#define	OBJADDR_SHM_SEL		0x00010000
#define	OBJADDR_SCR_SEL		0x00020000
#define	OBJADDR_IHR_SEL		0x00030000
#define	OBJADDR_RCMTA_SEL	0x00040000
#define	OBJADDR_AMT_SEL		0x00040000
#define	OBJADDR_SRCHM_SEL	0x00060000
#define	OBJADDR_KEYTBL_SEL	0x000c0000
#define	OBJADDR_HEB_SEL		0x00120000
#define	OBJADDR_TXDC_TBL_SEL	0x00140000
#define	OBJADDR_TXDC_RIB_SEL	0x00150000
#define	OBJADDR_FCBS_SEL	0x00160000
#define	OBJADDR_LIT_SEL		0x00170000
#define	OBJADDR_LIB_SEL		0x00180000
#define	OBJADDR_WINC		0x01000000
#define	OBJADDR_RINC		0x02000000
#define	OBJADDR_AUTO_INC	0x03000000
/* SHM/SCR/IHR/SHMX/SCRX/IHRX allow 2 bytes read/write, else only 4 bytes */
#define	OBJADDR_2BYTES_ACCESS(sel)	\
	(((sel & 0x70000) == OBJADDR_SHM_SEL) || \
	((sel & 0x70000) == OBJADDR_SCR_SEL) || \
	((sel & 0x70000) == OBJADDR_IHR_SEL))

/* objdata register */
#define	OBJDATA_WR_COMPLT	0x00000001

/* frmtxstatus */
#define	TXS_V			(1 << 0)	/**< valid bit */

#define	TXS_STATUS_MASK		0xffff
/* sw mask to map txstatus for corerevs <= 4 to be the same as for corerev > 4 */
#define	TXS_COMPAT_MASK		0x3
#define	TXS_COMPAT_SHIFT	1
#define	TXS_FID_MASK		0xffff0000
#define	TXS_FID_SHIFT		16

/* frmtxstatus2 */
#define	TXS_SEQ_MASK		0xffff
#define	TXS_PTX_MASK		0xff0000
#define	TXS_PTX_SHIFT		16
#define	TXS_MU_MASK		0x01000000
#define	TXS_MU_SHIFT		24

/* clk_ctl_st, corerev >= 17 */
#define CCS_ERSRC_REQ_D11PLL	0x00000100	/**< d11 core pll request */
#define CCS_ERSRC_REQ_PHYPLL	0x00000200	/**< PHY pll request */
#define CCS_ERSRC_REQ_PTMPLL	0x00001000	/* PTM clock request */
#define CCS_ERSRC_AVAIL_D11PLL	0x01000000	/**< d11 core pll available */
#define CCS_ERSRC_AVAIL_PHYPLL	0x02000000	/**< PHY pll available */
#define CCS_ERSRC_AVAIL_PTMPLL	0x10000000	/**< PHY pll available */

/* tsf_cfprep register */
#define	CFPREP_CBI_MASK		0xffffffc0
#define	CFPREP_CBI_SHIFT	6
#define	CFPREP_CFPP		0x00000001

/* receive fifo control */
#define	RFC_FR			(1 << 0)	/**< frame ready */
#define	RFC_DR			(1 << 1)	/**< data ready */

/* tx fifo sizes for corerev >= 9 */
/* tx fifo sizes values are in terms of 256 byte blocks */
#define TXFIFOCMD_RESET_MASK	(1 << 15)	/**< reset */
#define TXFIFOCMD_FIFOSEL_SHIFT	8		/**< fifo */
#define TXFIFOCMD_FIFOSEL_SET(val)	((val & 0x7) << TXFIFOCMD_FIFOSEL_SHIFT)	/* fifo */
#define TXFIFOCMD_FIFOSEL_GET(val)	((val >> TXFIFOCMD_FIFOSEL_SHIFT) & 0x7)	/* fifo */
#define TXFIFO_FIFOTOP_SHIFT	8		/**< fifo start */

#define TXFIFO_FIFO_START(def, def1)	((def & 0xFF) | ((def1 & 0xFF) << 8))
#define TXFIFO_FIFO_END(def, def1)	(((def & 0xFF00) >> 8) | (def1 & 0xFF00))

/* Must redefine to 65 for 16 MBSS */
#ifdef WLLPRS
#define TXFIFO_START_BLK16	(65+16)	/**< Base address + 32 * 512 B/P + 8 * 512 11g P */
#else /* WLLPRS */
#define TXFIFO_START_BLK16	65	/**< Base address + 32 * 512 B/P */
#endif /* WLLPRS */
#define TXFIFO_START_BLK	6	/**< Base address + 6 * 256 B */
#define TXFIFO_START_BLK_NIN	7	/**< Base address + 6 * 256 B */

#define TXFIFO_AC_SIZE_PER_UNIT	512	/**< one unit corresponds to 512 bytes */

#define MBSS16_TEMPLMEM_MINBLKS	65	/**< one unit corresponds to 256 bytes */

/* phy versions, PhyVersion:Revision field */
#define	PV_AV_MASK		0xf000		/**< analog block version */
#define	PV_AV_SHIFT		12		/**< analog block version bitfield offset */
#define	PV_PT_MASK		0x0f00		/**< phy type */
#define	PV_PT_SHIFT		8		/**< phy type bitfield offset */
#define	PV_PV_MASK		0x00ff		/**< phy version */
#define	PHY_TYPE(v)		((v & PV_PT_MASK) >> PV_PT_SHIFT)

/* phy types, PhyVersion:PhyType field */
#ifndef USE_BCMCONF_H
#define	PHY_TYPE_A		0	/**< A-Phy value */
#define	PHY_TYPE_B		1	/**< B-Phy value */
#define	PHY_TYPE_G		2	/**< G-Phy value */
#define	PHY_TYPE_N		4	/**< N-Phy value */
/* #define	PHY_TYPE_LP		5 */	/**< LP-Phy value */
/* #define	PHY_TYPE_SSN		6 */	/**< SSLPN-Phy value */
#define	PHY_TYPE_HT		7	/**< 3x3 HTPhy value */
#define	PHY_TYPE_LCN		8	/**< LCN-Phy value */
#define	PHY_TYPE_LCNXN		9	/**< LCNXN-Phy value */
#define	PHY_TYPE_LCN40		10	/**< LCN40-Phy value */
#define	PHY_TYPE_AC		11	/**< AC-Phy value */
#define	PHY_TYPE_LCN20		12	/**< LCN20-Phy value */
#define	PHY_TYPE_HE		13	/**< HE-Phy value */
#define	PHY_TYPE_NULL		0xf	/**< Invalid Phy value */
#endif /* USE_BCMCONF_H */

/* analog types, PhyVersion:AnalogType field */
#define	ANA_11G_018		1
#define	ANA_11G_018_ALL		2
#define	ANA_11G_018_ALLI	3
#define	ANA_11G_013		4
#define	ANA_11N_013		5
#define	ANA_11LP_013		6

/** 802.11a PLCP header def */
typedef struct ofdm_phy_hdr ofdm_phy_hdr_t;
BWL_PRE_PACKED_STRUCT struct ofdm_phy_hdr {
	uint8	rlpt[3];	/**< rate, length, parity, tail */
	uint16	service;
	uint8	pad;
} BWL_POST_PACKED_STRUCT;

#define	D11A_PHY_HDR_GRATE(phdr)	((phdr)->rlpt[0] & 0x0f)
#define	D11A_PHY_HDR_GRES(phdr)		(((phdr)->rlpt[0] >> 4) & 0x01)
#define	D11A_PHY_HDR_GLENGTH(phdr)	(((*((uint32 *)((phdr)->rlpt))) >> 5) & 0x0fff)
#define	D11A_PHY_HDR_GPARITY(phdr)	(((phdr)->rlpt[3] >> 1) & 0x01)
#define	D11A_PHY_HDR_GTAIL(phdr)	(((phdr)->rlpt[3] >> 2) & 0x3f)

/** rate encoded per 802.11a-1999 sec 17.3.4.1 */
#define	D11A_PHY_HDR_SRATE(phdr, rate)		\
	((phdr)->rlpt[0] = ((phdr)->rlpt[0] & 0xf0) | ((rate) & 0xf))
/** set reserved field to zero */
#define	D11A_PHY_HDR_SRES(phdr)		((phdr)->rlpt[0] &= 0xef)
/** length is number of octets in PSDU */
#define	D11A_PHY_HDR_SLENGTH(phdr, length)	\
	(*(uint32 *)((phdr)->rlpt) = *(uint32 *)((phdr)->rlpt) | \
	(((length) & 0x0fff) << 5))
/** set the tail to all zeros */
#define	D11A_PHY_HDR_STAIL(phdr)	((phdr)->rlpt[3] &= 0x03)

#define	D11A_PHY_HDR_LEN_L	3	/**< low-rate part of PLCP header */
#define	D11A_PHY_HDR_LEN_R	2	/**< high-rate part of PLCP header */

#define	D11A_PHY_TX_DELAY	(2)	/**< 2.1 usec */

#define	D11A_PHY_HDR_TIME	(4)	/**< low-rate part of PLCP header */
#define	D11A_PHY_PRE_TIME	(16)
#define	D11A_PHY_PREHDR_TIME	(D11A_PHY_PRE_TIME + D11A_PHY_HDR_TIME)

/** 802.11b PLCP header def */
typedef struct cck_phy_hdr cck_phy_hdr_t;
BWL_PRE_PACKED_STRUCT struct cck_phy_hdr {
	uint8	signal;
	uint8	service;
	uint16	length;
	uint16	crc;
} BWL_POST_PACKED_STRUCT;

#define	D11B_PHY_HDR_LEN	6

#define	D11B_PHY_TX_DELAY	(3)	/**< 3.4 usec */

#define	D11B_PHY_LHDR_TIME	(D11B_PHY_HDR_LEN << 3)
#define	D11B_PHY_LPRE_TIME	(144)
#define	D11B_PHY_LPREHDR_TIME	(D11B_PHY_LPRE_TIME + D11B_PHY_LHDR_TIME)

#define	D11B_PHY_SHDR_TIME	(D11B_PHY_LHDR_TIME >> 1)
#define	D11B_PHY_SPRE_TIME	(D11B_PHY_LPRE_TIME >> 1)
#define	D11B_PHY_SPREHDR_TIME	(D11B_PHY_SPRE_TIME + D11B_PHY_SHDR_TIME)

#define	D11B_PLCP_SIGNAL_LOCKED	(1 << 2)
#define	D11B_PLCP_SIGNAL_LE	(1 << 7)

/* AMPDUXXX: move to ht header file once it is ready: Mimo PLCP */
#define MIMO_PLCP_MCS_MASK	0x7f	/**< mcs index */
#define MIMO_PLCP_40MHZ		0x80	/**< 40 Hz frame */
#define MIMO_PLCP_AMPDU		0x08	/**< ampdu */

#define WLC_GET_CCK_PLCP_LEN(plcp) (plcp[4] + (plcp[5] << 8))
#define WLC_GET_MIMO_PLCP_LEN(plcp) (plcp[1] + (plcp[2] << 8))
#define WLC_SET_MIMO_PLCP_LEN(plcp, len) \
	plcp[1] = len & 0xff; plcp[2] = ((len >> 8) & 0xff);

#define WLC_SET_MIMO_PLCP_AMPDU(plcp) (plcp[3] |= MIMO_PLCP_AMPDU)
#define WLC_CLR_MIMO_PLCP_AMPDU(plcp) (plcp[3] &= ~MIMO_PLCP_AMPDU)
#define WLC_IS_MIMO_PLCP_AMPDU(plcp) (plcp[3] & MIMO_PLCP_AMPDU)

/**
 * The dot11a PLCP header is 5 bytes.  To simplify the software (so that we don't need eg different
 * tx DMA headers for 11a and 11b), the PLCP header has padding added in the ucode.
 */
#define	D11_PHY_HDR_LEN	6u

/** For the AC phy PLCP is 12 bytes and not all bytes are used for all the modulations */
#define D11AC_PHY_HDR_LEN	12
#define D11AC_PHY_VHT_PLCP_OFFSET	0
#define D11AC_PHY_HTMM_PLCP_OFFSET	0
#define D11AC_PHY_HTGF_PLCP_OFFSET	3
#define D11AC_PHY_OFDM_PLCP_OFFSET	3
#define D11AC_PHY_CCK_PLCP_OFFSET	6
#define D11AC_PHY_BEACON_PLCP_OFFSET	0

#define D11_PHY_RXPLCP_LEN(rev)		(D11_PHY_HDR_LEN)
#define D11_PHY_RXPLCP_OFF(rev)		(0)

/** TX descriptor - pre40 */
typedef struct d11txh_pre40 d11txh_pre40_t;
BWL_PRE_PACKED_STRUCT struct d11txh_pre40 {
	uint16	MacTxControlLow;		/* 0x0 */
	uint16	MacTxControlHigh;		/* 0x1 */
	uint16	MacFrameControl;		/* 0x2 */
	uint16	TxFesTimeNormal;		/* 0x3 */
	uint16	PhyTxControlWord;		/* 0x4 */
	uint16	PhyTxControlWord_1;		/* 0x5 */
	uint16	PhyTxControlWord_1_Fbr;		/* 0x6 */
	uint16	PhyTxControlWord_1_Rts;		/* 0x7 */
	uint16	PhyTxControlWord_1_FbrRts;	/* 0x8 */
	uint16	MainRates;			/* 0x9 */
	uint16	XtraFrameTypes;			/* 0xa */
	uint8	IV[16];				/* 0x0b - 0x12 */
	uint8	TxFrameRA[6];			/* 0x13 - 0x15 */
	uint16	TxFesTimeFallback;		/* 0x16 */
	uint8	RTSPLCPFallback[6];		/* 0x17 - 0x19 */
	uint16	RTSDurFallback;			/* 0x1a */
	uint8	FragPLCPFallback[6];		/* 0x1b - 1d */
	uint16	FragDurFallback;		/* 0x1e */
	uint16	MModeLen;			/* 0x1f */
	uint16	MModeFbrLen;			/* 0x20 */
	uint16	TstampLow;			/* 0x21 */
	uint16	TstampHigh;			/* 0x22 */
	uint16	ABI_MimoAntSel;			/* 0x23 */
	uint16	PreloadSize;			/* 0x24 */
	uint16	AmpduSeqCtl;			/* 0x25 */
	uint16	TxFrameID;			/* 0x26 */
	uint16	TxStatus;			/* 0x27 */
	uint16	MaxNMpdus;			/* 0x28 corerev >=16 */
	BWL_PRE_PACKED_STRUCT union {
		uint16 MaxAggDur;		/* 0x29 corerev >=16 */
		uint16 MaxAggLen;
	} BWL_POST_PACKED_STRUCT u1;
	BWL_PRE_PACKED_STRUCT union {
		BWL_PRE_PACKED_STRUCT struct {	/* 0x29 corerev >=16 */
			uint8 MaxRNum;
			uint8 MaxAggBytes;	/* Max Agg Bytes in power of 2 */
		} BWL_POST_PACKED_STRUCT s1;
		uint16	MaxAggLen_FBR;
	} BWL_POST_PACKED_STRUCT u2;
	uint16	MinMBytes;			/* 0x2b corerev >=16 */
	uint8	RTSPhyHeader[D11_PHY_HDR_LEN];	/* 0x2c - 0x2e */
	struct	dot11_rts_frame rts_frame;	/* 0x2f - 0x36 */
	uint16	pad;				/* 0x37 */
} BWL_POST_PACKED_STRUCT;

#define	D11_TXH_LEN		112	/**< bytes */

/* Frame Types */
#define FT_LEGACY	(-1)
#define FT_CCK		0
#define FT_OFDM		1
#define FT_HT		2
#define FT_VHT		3
#define FT_HE		4
#define FT_EHT		6

/* HE PPDU type */
#define HE_SU_PPDU              0
#define HE_SU_RE_PPDU           1
#define HE_MU_PPDU              2
#define HE_TRIG_PPDU            3

/* Position of MPDU inside A-MPDU; indicated with bits 10:9 of MacTxControlLow */
#define TXC_AMPDU_SHIFT		9	/**< shift for ampdu settings */
#define TXC_AMPDU_NONE		0	/**< Regular MPDU, not an A-MPDU */
#define TXC_AMPDU_FIRST		1	/**< first MPDU of an A-MPDU */
#define TXC_AMPDU_MIDDLE	2	/**< intermediate MPDU of an A-MPDU */
#define TXC_AMPDU_LAST		3	/**< last (or single) MPDU of an A-MPDU */

/* MacTxControlLow */
#define TXC_AMIC		0x8000
#define TXC_USERIFS		0x4000
#define TXC_LIFETIME		0x2000
#define	TXC_FRAMEBURST		0x1000
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

/* MacTxControlHigh */
#define TXC_PREAMBLE_RTS_FB_SHORT	0x8000	/* RTS fallback preamble type 1 = SHORT 0 = LONG */
#define TXC_PREAMBLE_RTS_MAIN_SHORT	0x4000	/* RTS main rate preamble type 1 = SHORT 0 = LONG */
#define TXC_PREAMBLE_DATA_FB_SHORT	0x2000	/**< Main fallback rate preamble type
					 * 1 = SHORT for OFDM/GF for MIMO
					 * 0 = LONG for CCK/MM for MIMO
					 */
/* TXC_PREAMBLE_DATA_MAIN is in PhyTxControl bit 5 */
#define	TXC_AMPDU_FBR		0x1000	/**< use fallback rate for this AMPDU */
#define	TXC_SECKEY_MASK		0x0FF0
#define	TXC_SECKEY_SHIFT	4
#define	TXC_ALT_TXPWR		0x0008	/**< Use alternate txpwr defined at loc. M_ALT_TXPWR_IDX */
#define	TXC_SECTYPE_MASK	0x0007
#define	TXC_SECTYPE_SHIFT	0

/* Null delimiter for Fallback rate */
#define AMPDU_FBR_NULL_DELIM  5		/**< Location of Null delimiter count for AMPDU */

/* PhyTxControl for Mimophy */
#define	PHY_TXC_PWR_MASK	0xFC00
#define	PHY_TXC_PWR_SHIFT	10
#define	PHY_TXC_ANT_MASK	0x03C0	/**< bit 6, 7, 8, 9 */
#define	PHY_TXC_ANT_SHIFT	6
#define	PHY_TXC_ANT_0_1		0x00C0	/**< auto, last rx */
#define	PHY_TXC_LPPHY_ANT_LAST	0x0000
#define	PHY_TXC_ANT_3		0x0200	/**< virtual antenna 3 */
#define	PHY_TXC_ANT_2		0x0100	/**< virtual antenna 2 */
#define	PHY_TXC_ANT_1		0x0080	/**< virtual antenna 1 */
#define	PHY_TXC_ANT_0		0x0040	/**< virtual antenna 0 */

#define	PHY_TXC_SHORT_HDR	0x0010
#define PHY_TXC_FT_MASK		0x0003

#define	PHY_TXC_FT_CCK		0x0000
#define	PHY_TXC_FT_OFDM		0x0001
#define	PHY_TXC_FT_HT		0x0002
#define	PHY_TXC_FT_VHT		0x0003
#define PHY_TXC_FT_HE		0x0004
#define PHY_TXC_FT_EHT		0x0006

#define	PHY_TXC_OLD_ANT_0	0x0000
#define	PHY_TXC_OLD_ANT_1	0x0100
#define	PHY_TXC_OLD_ANT_LAST	0x0300

/** PhyTxControl_1 for Mimophy */
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
#define PHY_TXC1_CODE_RATE_SHIFT	8
#define PHY_TXC1_CODE_RATE_MASK		0x0700
#define PHY_TXC1_CODE_RATE_1_2		0
#define PHY_TXC1_CODE_RATE_2_3		1
#define PHY_TXC1_CODE_RATE_3_4		2
#define PHY_TXC1_CODE_RATE_4_5		3
#define PHY_TXC1_CODE_RATE_5_6		4
#define PHY_TXC1_CODE_RATE_7_8		6
#define PHY_TXC1_MOD_SCHEME_SHIFT	11
#define PHY_TXC1_MOD_SCHEME_MASK	0x3800
#define PHY_TXC1_MOD_SCHEME_BPSK	0
#define PHY_TXC1_MOD_SCHEME_QPSK	1
#define PHY_TXC1_MOD_SCHEME_QAM16	2
#define PHY_TXC1_MOD_SCHEME_QAM64	3
#define PHY_TXC1_MOD_SCHEME_QAM256	4

/* PhyTxControl for HTphy that are different from Mimophy */
#define	PHY_TXC_HTANT_MASK		0x3fC0	/**< bit 6, 7, 8, 9, 10, 11, 12, 13 */
#define	PHY_TXC_HTCORE_MASK		0x03C0	/**< core enable core3:core0, 1=enable, 0=disable */
#define	PHY_TXC_HTCORE_SHIFT		6	/**< bit 6, 7, 8, 9 */
#define	PHY_TXC_HTANT_IDX_MASK		0x3C00	/**< 4-bit, 16 possible antenna configuration */
#define	PHY_TXC_HTANT_IDX_SHIFT		10
#define	PHY_TXC_HTANT_IDX0		0
#define	PHY_TXC_HTANT_IDX1		1
#define	PHY_TXC_HTANT_IDX2		2
#define	PHY_TXC_HTANT_IDX3		3

/* PhyTxControl_1 for HTphy that are different from Mimophy */
#define PHY_TXC1_HTSPARTIAL_MAP_MASK	0x7C00	/**< bit 14:10 */
#define PHY_TXC1_HTSPARTIAL_MAP_SHIFT	10
#define PHY_TXC1_HTTXPWR_OFFSET_MASK	0x01f8	/**< bit 8:3 */
#define PHY_TXC1_HTTXPWR_OFFSET_SHIFT	3

/* TxControl word follows new interface for AX */
/* PhyTxControl_6 for AXphy */
#define PHY_TXC5_AXTXPWR_OFFSET_C0_MASK 0xff00 /**< bit 15:8 */
#define PHY_TXC5_AXTXPWR_OFFSET_C0_SHIFT 8
#define PHY_TXC6_AXTXPWR_OFFSET_C1_MASK 0x00ff /**< bit 7:0 */
#define PHY_TXC6_AXTXPWR_OFFSET_C1_SHIFT 0
#define PHY_TXC5_AXTXPWR_OFFSET_C2_MASK 0x00ff /**< bit 7:0 */
#define PHY_TXC5_AXTXPWR_OFFSET_C2_SHIFT 0

/* XtraFrameTypes */
#define XFTS_RTS_FT_SHIFT	2
#define XFTS_FBRRTS_FT_SHIFT	4
#define XFTS_CHANNEL_SHIFT	8

/** Antenna diversity bit in ant_wr_settle */
#define	PHY_AWS_ANTDIV		0x2000

/* IFS ctl */
#define IFS_USEEDCF	(1 << 2)

/* IFS ctl1 */
#define IFS_CTL1_EDCRS	(1 << 3)
#define IFS_CTL1_EDCRS_20L (1 << 4)
#define IFS_CTL1_EDCRS_40 (1 << 5)
#define IFS_EDCRS_MASK	(IFS_CTL1_EDCRS | IFS_CTL1_EDCRS_20L | IFS_CTL1_EDCRS_40)
#define IFS_EDCRS_SHIFT	3

/* IFS ctl sel pricrs  */
#define IFS_CTL_CRS_SEL_20LL    1
#define IFS_CTL_CRS_SEL_20LU    2
#define IFS_CTL_CRS_SEL_20UL    4
#define IFS_CTL_CRS_SEL_20UU    8
#define IFS_CTL_CRS_SEL_MASK    (IFS_CTL_CRS_SEL_20LL | IFS_CTL_CRS_SEL_20LU | \
				IFS_CTL_CRS_SEL_20UL | IFS_CTL_CRS_SEL_20UU)
#define IFS_CTL_ED_SEL_20LL     (1 << 8)
#define IFS_CTL_ED_SEL_20LU     (1 << 9)
#define IFS_CTL_ED_SEL_20UL     (1 << 10)
#define IFS_CTL_ED_SEL_20UU     (1 << 11)
#define IFS_CTL_ED_SEL_MASK     (IFS_CTL_ED_SEL_20LL | IFS_CTL_ED_SEL_20LU | \
				IFS_CTL_ED_SEL_20UL | IFS_CTL_ED_SEL_20UU)

/* ABI_MimoAntSel */
#define ABI_MAS_ADDR_BMP_IDX_MASK	0x0f00
#define ABI_MAS_ADDR_BMP_IDX_SHIFT	8
#define ABI_MAS_FBR_ANT_PTN_MASK	0x00f0
#define ABI_MAS_FBR_ANT_PTN_SHIFT	4
#define ABI_MAS_MRT_ANT_PTN_MASK	0x000f

#ifdef WLAWDL
#define ABI_MAS_AWDL_TS_INSERT		0x1000	/**< bit 12 */
#endif

#define ABI_MAS_TIMBC_TSF		0x2000	/**< Enable TIMBC tsf field present */

/* MinMBytes */
#define MINMBYTES_PKT_LEN_MASK                  0x0300
#define MINMBYTES_FBRATE_PWROFFSET_MASK         0xFC00
#define MINMBYTES_FBRATE_PWROFFSET_SHIFT        10

/* Rev40 template constants */

/** templates include a longer PLCP header that matches the MAC / PHY interface */
#define	D11_VHT_PLCP_LEN	12

/* 11AC TX DMA buffer header */

#define D11AC_TXH_NUM_RATES			4

/** per rate info - rev40 */
typedef struct d11actxh_rate d11actxh_rate_t;
BWL_PRE_PACKED_STRUCT struct d11actxh_rate {
	uint16  PhyTxControlWord_0;             /* 0 - 1 */
	uint16  PhyTxControlWord_1;             /* 2 - 3 */
	uint16  PhyTxControlWord_2;             /* 4 - 5 */
	uint8   plcp[D11_PHY_HDR_LEN];          /* 6 - 11 */
	uint16  FbwInfo;                        /* 12 -13, fall back bandwidth info */
	uint16  TxRate;                         /* 14 */
	uint16  RtsCtsControl;                  /* 16 */
	uint16  Bfm0;                           /* 18 */
} BWL_POST_PACKED_STRUCT;

/* Bit definition for FbwInfo field */
#define FBW_BW_MASK             3
#define FBW_BW_SHIFT            0
#define FBW_TXBF                4
#define FBW_TXBF_SHIFT          2
/* this needs to be re-visited if we want to use this feature */
#define FBW_BFM0_TXPWR_MASK     0x1F8
#define FBW_BFM0_TXPWR_SHIFT    3
#define FBW_BFM_TXPWR_MASK      0x7E00
#define FBW_BFM_TXPWR_SHIFT     9

/* Bit definition for Bfm0 field */
#define BFM0_TXPWR_MASK         0x3f
#define BFM0_STBC_SHIFT         6
#define BFM0_STBC               (1 << BFM0_STBC_SHIFT)
/* should find a chance to converge the two */
#define D11AC2_BFM0_TXPWR_MASK  0x7f
#define D11AC2_BFM0_STBC_SHIFT  7
#define D11AC2_BFM0_STBC        (1 << D11AC2_BFM0_STBC_SHIFT)

/* per packet info */
typedef struct d11pktinfo_common d11pktinfo_common_t;
typedef struct d11pktinfo_common d11actxh_pkt_t;
BWL_PRE_PACKED_STRUCT struct d11pktinfo_common {
	/* Per pkt info */
	uint16  TSOInfo;                        /* 0 */
	uint16  MacTxControlLow;                /* 2 */
	uint16  MacTxControlHigh;               /* 4 */
	uint16  Chanspec;                       /* 6 */
	uint8   IVOffset;                       /* 8 */
	uint8   PktCacheLen;                    /* 9 */
	uint16  FrameLen;                       /* 10. In [bytes] units. */
	uint16  TxFrameID;                      /* 12 */
	uint16  Seq;                            /* 14 */
	uint16  Tstamp;                         /* 16 */
	uint16  TxStatus;                       /* 18 */
} BWL_POST_PACKED_STRUCT;

/* common cache info between rev40 and rev80 formats */
typedef struct d11txh_cache_common d11txh_cache_common_t;
BWL_PRE_PACKED_STRUCT struct d11txh_cache_common {
	uint8   BssIdEncAlg;                    /* 0 */
	uint8   KeyIdx;                         /* 1 */
	uint8   PrimeMpduMax;                   /* 2 */
	uint8   FallbackMpduMax;                /* 3 */
	uint16  AmpduDur;                       /* 4 - 5 */
	uint8   BAWin;                          /* 6 */
	uint8   MaxAggLen;                      /* 7 */
} BWL_POST_PACKED_STRUCT;

/** Per cache info - rev40 */
typedef struct d11actxh_cache d11actxh_cache_t;
BWL_PRE_PACKED_STRUCT struct d11actxh_cache {
	d11txh_cache_common_t common;		/*  0 -  7 */
	uint8   TkipPH1Key[10];                 /*  8 - 17 */
	uint8   TSCPN[6];                       /* 18 - 23 */
} BWL_POST_PACKED_STRUCT;

/** Long format tx descriptor - rev40 */
typedef struct d11actxh d11actxh_t;
BWL_PRE_PACKED_STRUCT struct d11actxh {
	/* Per pkt info */
	d11actxh_pkt_t	PktInfo;			/* 0 - 19 */

	union {

		/** Rev 40 to rev 63 layout */
		struct {
			/** Per rate info */
			d11actxh_rate_t RateInfo[D11AC_TXH_NUM_RATES];  /* 20 - 99 */

			/** Per cache info */
			d11actxh_cache_t	CacheInfo;                    /* 100 - 123 */
		} rev40;

		/** Rev >= 64 layout */
		struct {
			/** Per cache info */
			d11actxh_cache_t	CacheInfo;                    /* 20 - 43 */

			/** Per rate info */
			d11actxh_rate_t RateInfo[D11AC_TXH_NUM_RATES];  /* 44 - 123 */
		} rev64;

	};
} BWL_POST_PACKED_STRUCT;

#define D11AC_TXH_LEN		sizeof(d11actxh_t)	/* 124 bytes */

/* Short format tx descriptor only has per packet info */
#define D11AC_TXH_SHORT_LEN	sizeof(d11actxh_pkt_t)	/* 20 bytes */

/* -TXDC-TxH Excluding Rate Info 41 bytes (Note 1 byte of RATEINFO is removed */
#define D11AC_TXH_SHORT_EXT_LEN		(sizeof(d11txh_rev80_t) - 1)

/* Retry limit regs */
/* Current retries for the fallback rates are hardcoded */
#define D11AC_TXDC_SRL_FB	(3u)	/* Short Retry Limit - Fallback */
#define D11AC_TXDC_LRL_FB	(2u)	/* Long Retry Limit - Fallback */

#define D11AC_TXDC_RET_LIM_MASK (0x000Fu)
#define D11AC_TXDC_SRL_SHIFT	(0u)	/* Short Retry Limit */
#define D11AC_TXDC_SRL_FB_SHIFT (4u)	/* Short Retry Limit - Fallback */
#define D11AC_TXDC_LRL_SHIFT	(8u)	/* Long Retry Limit */
#define D11AC_TXDC_LRL_FB_SHIFT (12u)	/* Long Retry Limit - Fallback */

/* MacTxControlLow */
#define D11AC_TXC_HDR_FMT_SHORT		0x0001	/**< 0: long format, 1: short format */
#define D11AC_TXC_UPD_CACHE		0x0002
#define D11AC_TXC_CACHE_IDX_MASK	0x003C	/**< Cache index 0 .. 15 */
#define D11AC_TXC_CACHE_IDX_SHIFT	2

#define D11AC_TXDC_IDX_SHIFT		1
#define D11AC_TXDC_CPG_SHIFT		5
#define D11REV80_TXDC_RIB_CPG		0x0020  /**< Cache Index CPG (Bit 5)   -TXDC- */
#define D11REV80_TXDC_RIB_DEL_MASK	0x001E  /**< Cache index CIPX 0 .. 15 (Bit 1-4 -TXDC- */
#define D11REV80_TXDC_RIB_IMM_MASK	0x003E	/**< Cache index CIPX 0 .. 31 (Bit 1-5) -TXDC- */
#define D11AC_TXC_AMPDU			0x0040	/**< Is aggregate-able */
#define D11AC_TXC_IACK			0x0080	/**< Expect immediate ACK */
#define D11AC_TXC_LFRM			0x0100	/**< Use long/short retry frame count/limit */
#define D11AC_TXC_IPMQ			0x0200	/**< Ignore PMQ */
#define D11AC_TXC_MBURST		0x0400	/**< Burst mode */
#define D11AC_TXC_ASEQ			0x0800	/**< Add ucode generated seq num */
#define D11AC_TXC_AGING			0x1000	/**< Use lifetime */
#define D11AC_TXC_AMIC			0x2000	/**< Compute and add TKIP MIC */
#define D11AC_TXC_STMSDU		0x4000	/**< First MSDU */
#define D11AC_TXC_URIFS			0x8000	/**< Use RIFS */

/* MacTxControlHigh */
#define D11AC_TXC_DISFCS		0x0001	/**< Discard FCS */
#define D11AC_TXC_FIX_RATE		0x0002	/**< Use primary rate only */
#define D11AC_TXC_SVHT			0x0004	/**< Single VHT mpdu ampdu */
#define D11AC_TXC_PPS			0x0008	/**< Enable PS Pretend feature */
#define D11AC_TXC_UCODE_SEQ		0x0010	/* Sequence counter for BK traffic, for offloads */
#define D11AC_TXC_TIMBC_TSF		0x0020	/**< Enable TIMBC tsf field present */
#define D11AC_TXC_TCPACK		0x0040
#define D11AC_TXC_AWDL_PHYTT 	0x0080 /**< Fill in PHY Transmission Time for AWDL action frames */
#define D11AC_TXC_TOF			0x0100 /**< Enable wifi ranging processing for rxd frames */
#define D11AC_TXC_MU			0x0200 /**< MU Tx data */
#define D11AC_TXC_BFIX			0x0800 /**< BFI from SHMx */
#define D11AC_TXC_NORETRY		0x0800 /**< Disable retry for tsync frames */
#define D11AC_TXC_UFP			0x1000	/**< UFP */
#define D11AC_TXC_OVERRIDE_NAV		0x1000 /**< if set, ucode will tx without honoring NAV */
#define D11AC_TXC_DYNBW			0x2000	/**< Dynamic BW */
#define D11AC_TXC_TXPROF_EN		0x8000	/**< TxProfile Enable TODO: support multiple idx */
#define D11AC_TXC_SLTF			0x8000	/**< 11az Secure Ranging frame */

#define D11AC_TSTAMP_SHIFT		8	/**< Tstamp in 256us units */

/* PhyTxControlWord_0 */
#define D11AC_PHY_TXC_FT_MASK		0x0003

/* vht txctl0 */
#define D11AC_PHY_TXC_NON_SOUNDING	0x0004
#define D11AC_PHY_TXC_BFM			0x0008
#define D11AC_PHY_TXC_SHORT_PREAMBLE	0x0010
#define D11AC2_PHY_TXC_STBC		0x0020
#define D11AC_PHY_TXC_ANT_MASK		0x3FC0
#define D11AC_PHY_TXC_CORE_MASK		0x03C0
#define D11AC_PHY_TXC_CORE_SHIFT	6
#define D11AC_PHY_TXC_ANT_IDX_MASK	0x3C00
#define D11AC_PHY_TXC_ANT_IDX_SHIFT	10
#define D11AC_PHY_TXC_BW_MASK		0xC000
#define D11AC_PHY_TXC_BW_SHIFT		14
#define D11AC_PHY_TXC_BW_20MHZ		0x0000
#define D11AC_PHY_TXC_BW_40MHZ		0x4000
#define D11AC_PHY_TXC_BW_80MHZ		0x8000
#define D11AC_PHY_TXC_BW_160MHZ		0xC000

/* PhyTxControlWord_1 */
#define D11AC_PHY_TXC_PRIM_SUBBAND_MASK		0x0007
#define D11AC_PHY_TXC_PRIM_SUBBAND_LLL		0x0000
#define D11AC_PHY_TXC_PRIM_SUBBAND_LLU		0x0001
#define D11AC_PHY_TXC_PRIM_SUBBAND_LUL		0x0002
#define D11AC_PHY_TXC_PRIM_SUBBAND_LUU		0x0003
#define D11AC_PHY_TXC_PRIM_SUBBAND_ULL		0x0004
#define D11AC_PHY_TXC_PRIM_SUBBAND_ULU		0x0005
#define D11AC_PHY_TXC_PRIM_SUBBAND_UUL		0x0006
#define D11AC_PHY_TXC_PRIM_SUBBAND_UUU		0x0007
#define D11AC_PHY_TXC_TXPWR_OFFSET_MASK 	0x01F8
#define D11AC_PHY_TXC_TXPWR_OFFSET_SHIFT	3
#define D11AC2_PHY_TXC_TXPWR_OFFSET_MASK 	0x03F8
#define D11AC2_PHY_TXC_TXPWR_OFFSET_SHIFT	3
#define D11AC_PHY_TXC_TXBF_USER_IDX_MASK	0x7C00
#define D11AC_PHY_TXC_TXBF_USER_IDX_SHIFT	10
#define D11AC2_PHY_TXC_DELTA_TXPWR_OFFSET_MASK 	0x7C00
#define D11AC2_PHY_TXC_DELTA_TXPWR_OFFSET_SHIFT	10
/* Rather awkward bit mapping to keep pctl1 word same as legacy, for proprietary 11n rate support */
#define D11AC_PHY_TXC_11N_PROP_MCS		0x8000 /* this represents bit mcs[6] */
#define D11AC2_PHY_TXC_MU			0x8000

/* PhyTxControlWord_2 phy rate */
#define D11AC_PHY_TXC_PHY_RATE_MASK		0x003F
#define D11AC2_PHY_TXC_PHY_RATE_MASK		0x007F

/* 11b phy rate */
#define D11AC_PHY_TXC_11B_PHY_RATE_MASK		0x0003
#define D11AC_PHY_TXC_11B_PHY_RATE_1		0x0000
#define D11AC_PHY_TXC_11B_PHY_RATE_2		0x0001
#define D11AC_PHY_TXC_11B_PHY_RATE_5_5		0x0002
#define D11AC_PHY_TXC_11B_PHY_RATE_11		0x0003

/* 11a/g phy rate */
#define D11AC_PHY_TXC_11AG_PHY_RATE_MASK	0x0007
#define D11AC_PHY_TXC_11AG_PHY_RATE_6		0x0000
#define D11AC_PHY_TXC_11AG_PHY_RATE_9		0x0001
#define D11AC_PHY_TXC_11AG_PHY_RATE_12		0x0002
#define D11AC_PHY_TXC_11AG_PHY_RATE_18		0x0003
#define D11AC_PHY_TXC_11AG_PHY_RATE_24		0x0004
#define D11AC_PHY_TXC_11AG_PHY_RATE_36		0x0005
#define D11AC_PHY_TXC_11AG_PHY_RATE_48		0x0006
#define D11AC_PHY_TXC_11AG_PHY_RATE_54		0x0007

/* 11ac phy rate */
#define D11AC_PHY_TXC_11AC_MCS_MASK		0x000F
#define D11AC_PHY_TXC_11AC_NSS_MASK		0x0030
#define D11AC_PHY_TXC_11AC_NSS_SHIFT		4

/* 11n phy rate */
#define D11AC_PHY_TXC_11N_MCS_MASK		0x003F
#define D11AC2_PHY_TXC_11N_MCS_MASK		0x007F
#define D11AC2_PHY_TXC_11N_PROP_MCS		0x0040 /* this represents bit mcs[6] */

/* PhyTxControlWord_2 rest */
#define D11AC_PHY_TXC_STBC			0x0040
#define D11AC_PHY_TXC_DYN_BW_IN_NON_HT_PRESENT	0x0080
#define D11AC_PHY_TXC_DYN_BW_IN_NON_HT_DYNAMIC	0x0100
#define D11AC2_PHY_TXC_TXBF_USER_IDX_MASK	0xFE00
#define D11AC2_PHY_TXC_TXBF_USER_IDX_SHIFT	9

/* RtsCtsControl */
#define D11AC_RTSCTS_FRM_TYPE_MASK	0x0001	/**< frame type */
#define D11AC_RTSCTS_FRM_TYPE_11B	0x0000	/**< 11b */
#define D11AC_RTSCTS_FRM_TYPE_11AG	0x0001	/**< 11a/g */
#define D11AC_RTSCTS_USE_RTS		0x0004	/**< Use RTS */
#define D11AC_RTSCTS_USE_CTS		0x0008	/**< Use CTS */
#define D11AC_RTSCTS_SHORT_PREAMBLE	0x0010	/**< Long/short preamble: 0 - long, 1 - short? */
#define D11AC_RTSCTS_LAST_RATE		0x0020	/**< this is last rate */
#define D11AC_RTSCTS_IMBF		0x0040	/**< Implicit TxBF */
#define D11AC_RTSCTS_MIMOPS_RTS		0x8000	/**< Use RTS for mimops */
#define D11AC_RTSCTS_DPCU_VALID		0x0080	/**< DPCU Valid : Same bitfield as above */
#define D11AC_RTSCTS_BF_IDX_MASK	0xF000	/**< 4-bit index to the beamforming block */
#define D11AC_RTSCTS_BF_IDX_SHIFT	12
#define D11AC_RTSCTS_RATE_MASK		0x0F00	/**< Rate table offset: bit 3-0 of PLCP byte 0 */
#define D11AC_RTSCTS_USE_RATE_SHIFT	8

/* BssIdEncAlg */
#define D11AC_BSSID_MASK		0x000F	/**< BSS index */
#define D11AC_BSSID_SHIFT		0
#define D11AC_ENCRYPT_ALG_MASK		0x00F0	/**< Encryption algoritm */
#define D11AC_ENCRYPT_ALG_SHIFT		4
#define D11AC_ENCRYPT_ALG_NOSEC		0x0000	/**< No security */
#define D11AC_ENCRYPT_ALG_WEP		0x0010	/**< WEP */
#define D11AC_ENCRYPT_ALG_TKIP		0x0020	/**< TKIP */
#define D11AC_ENCRYPT_ALG_AES		0x0030	/**< AES */
#define D11AC_ENCRYPT_ALG_WEP128	0x0040	/**< WEP128 */
#define D11AC_ENCRYPT_ALG_NA		0x0050	/**< N/A */
#define D11AC_ENCRYPT_ALG_WAPI		0x0060	/**< WAPI */

/* AmpduDur */
#define D11AC_AMPDU_MIN_DUR_IDX_MASK	0x000F	/**< AMPDU minimum duration index */
#define D11AC_AMPDU_MIN_DUR_IDX_SHIFT	0
#define D11AC_AMPDU_MAX_DUR_MASK	0xFFF0	/**< AMPDU maximum duration in unit 16 usec */
#define D11AC_AMPDU_MAX_DUR_SHIFT	4

/**
 * TX Descriptor definitions for supporting rev80 (HE)
 */
/* Maximum number of TX fallback rates per packet */
#define D11_REV80_TXH_NUM_RATES			4
#define D11_REV80_TXH_PHYTXCTL_MIN_LENGTH	1

/** per rate info - fixed portion - rev80 */
typedef struct d11txh_rev80_rate_fixed d11txh_rev80_rate_fixed_t;
BWL_PRE_PACKED_STRUCT struct d11txh_rev80_rate_fixed {
	uint16	TxRate;			/* rate in 500Kbps */
	uint16	RtsCtsControl;		/* RTS - CTS control */
	uint8	plcp[D11_PHY_HDR_LEN];	/* 6 bytes */
} BWL_POST_PACKED_STRUCT;

/* rev80 specific per packet info fields */
typedef struct d11pktinfo_rev80 d11pktinfo_rev80_t;
BWL_PRE_PACKED_STRUCT struct d11pktinfo_rev80 {
	uint16	HEModeControl;			/* 20 */
	uint16  length;				/* 22 - length of txd in bytes */
} BWL_POST_PACKED_STRUCT;

#define D11_REV80_TXH_TX_MODE_SHIFT		0	/* Bits 2:0 of HeModeControl */
#define D11_REV80_TXH_TX_MODE_MASK		0x3
#define D11_REV80_TXH_HTC_OFFSET_SHIFT		4	/* Bits 8:4 of HeModeControl */
#define D11_REV80_TXH_HTC_OFFSET_MASK		0x01F0u
#define D11_REV80_TXH_TWT_EOSP			0x0200u	/* bit 9 indicate TWT EOSP */
#define D11_REV80_TXH_QSZ_QOS_CTL_IND_SHIFT	10	/* Bit 10 of HeModeControl */
#define D11_REV80_TXH_QSZ_QOS_CTL_IND_MASK	(1 << D11_REV80_TXH_QSZ_QOS_CTL_IND_SHIFT)
#define D11_REV80_TXH_USE_BSSCOLOR_SHM_SHIFT	15	/* Bit 15 of HEModeControl */
#define D11_REV80_TXH_USE_BSSCOLOR_SHM_MASK	(1 << D11_REV80_TXH_USE_BSSCOLOR_SHM_SHIFT)

/* Calculate Length for short format TXD */
#define D11_TXH_SHORT_LEN(__corerev__)		(D11REV_GE(__corerev__, 80) ? \
						 D11_REV80_TXH_SHORT_LEN :    \
						 D11AC_TXH_SHORT_LEN)

/* Calculate Length for short format TXD  (TXDC and/or FMF) */
#define D11_TXH_SHORT_EX_LEN(__corerev__)	(D11REV_GE(__corerev__, 80) ? \
						 D11_REV80_TXH_SHORT_EX_LEN : \
						 D11AC_TXH_SHORT_LEN)

#define D11_REV80_TXH_IS_HE_AMPDU_SHIFT		11	/* Bit 11 of HeModeControl */
#define D11_REV80_TXH_IS_HE_AMPDU_MASK		(1 << D11_REV80_TXH_IS_HE_AMPDU_SHIFT)

#define D11_REV80_PHY_TXC_EDCA			0x00
#define D11_REV80_PHY_TXC_OFDMA_RA		0x01	/* Use Random Access Trigger for Tx */
#define D11_REV80_PHY_TXC_OFDMA_DT		0x02	/* Use Directed Trigger for Tx */
#define D11_REV80_PHY_TXC_OFDMA_ET		0x03	/* Use earliest Trigger Opportunity */

/** Per cache info - rev80 */
typedef struct d11txh_rev80_cache d11txh_rev80_cache_t;
BWL_PRE_PACKED_STRUCT struct d11txh_rev80_cache {
	d11txh_cache_common_t common;		/* 0 - 7 */
	uint16	ampdu_mpdu_all;			/* 8 - 9 */
	uint16	aggid;				/* 10 - 11 */
	uint8	tkipph1_index;			/* 12 */
	uint8	pktext;				/* 13 */
	uint16	hebid_map;			/* 14 -15: HEB ID bitmap */
} BWL_POST_PACKED_STRUCT;

/** Fixed size portion of TX descriptor - rev80 */
typedef struct d11txh_rev80 d11txh_rev80_t;
BWL_PRE_PACKED_STRUCT struct d11txh_rev80 {
	/**
	 * Per pkt info fields (common + rev80 specific)
	 *
	 * Note : Ensure that PktInfo field is always the first member
	 * of the d11txh_rev80 struct (that is at OFFSET - 0)
	 */
	d11pktinfo_common_t PktInfo;	/* 0 - 19 */
	d11pktinfo_rev80_t PktInfoExt;	/* 20 - 23 */

	/** Per cache info */
	d11txh_rev80_cache_t CacheInfo;	/* 24 - 39 */

	/**
	 * D11_REV80_TXH_NUM_RATES number of Rate Info blocks
	 * contribute to the variable size portion of the TXD.
	 * Each Rate Info element (block) is a funtion of
	 * (N_PwrOffset, N_RU, N_User).
	 */
	uint8 RateInfoBlock[1];
} BWL_POST_PACKED_STRUCT;

/* Size of fixed portion in TX descriptor (without CacheInfo(Link info) and RateInfoBlock)
 * this portion never change regardless of TXDC/FMF support.
 */
/* OFFSETOF() is available in bcmutils.h but including it will cause
 * recursive inclusion of d11.h specifically on NDIS platforms.
 */
#ifdef BCMFUZZ
	/* use 0x10 offset to avoid undefined behavior error due to NULL access */
#define D11_REV80_TXH_FIXED_LEN	(((uint)(uintptr)&((d11txh_rev80_t *)0x10)->CacheInfo) - 0x10)
#else
#define D11_REV80_TXH_FIXED_LEN	((uint)(uintptr)&((d11txh_rev80_t *)0)->CacheInfo)
#endif /* BCMFUZZ */

/* Short format tx descriptor only has per packet info (24 bytes) */
#define D11_REV80_TXH_SHORT_LEN	(sizeof(d11pktinfo_common_t) + sizeof(d11pktinfo_rev80_t))

/* Size of CacheInfo(Link info) in TX descriptor */
#define D11_REV80_TXH_LINK_INFO_LEN	(sizeof(d11txh_rev80_cache_t))

/* Size of Short format TX descriptor
 * with TXDC - Short TXD(40 bytes) shall include PktInfo and Cache info without Rate info
 * with TXDC+FMF - Short TXD(24 bytes) shall include PktInfo only without Link info and Rate info
 * do NOT use D11_REV80_TXH_SHORT_EX_LEN to calculate long TXD length, value depends on FMF feature
 */
#if defined(FMF_LIT) && !defined(FMF_LIT_DISABLED)
#define D11_REV80_TXH_SHORT_EX_LEN	D11_REV80_TXH_FIXED_LEN
#else
#define D11_REV80_TXH_SHORT_EX_LEN	(D11_REV80_TXH_FIXED_LEN + D11_REV80_TXH_LINK_INFO_LEN)
#endif /* FMF_LIT && !FMF_LIT_DISABLED */

/* Length of BFM0 field in RateInfo Blk */
#define	D11_REV80_TXH_BFM0_FIXED_LEN(pwr_offs)		2u

/**
 * Length of FBWInfo field in RateInfo Blk
 *
 * Note : for now return fixed length of 1 word
 */
#define	D11_REV80_TXH_FBWINFO_FIXED_LEN(pwr_offs)	2

#define D11_REV80_TXH_FIXED_RATEINFO_LEN	sizeof(d11txh_rev80_rate_fixed_t)

/**
 * Macros to find size of N-RUs field in the PhyTxCtlWord.
 */
#define D11_REV80_TXH_TXC_N_RUs_FIELD_SIZE		1
#define D11_REV80_TXH_TXC_PER_RU_INFO_SIZE		4
#define D11_REV80_TXH_TXC_PER_RU_MIN_SIZE		2

#define D11_REV80_TXH_TXC_RU_FIELD_SIZE(n_rus)	((n_rus == 1) ? \
						(D11_REV80_TXH_TXC_PER_RU_MIN_SIZE) : \
						((D11_REV80_TXH_TXC_N_RUs_FIELD_SIZE) + \
						((n_rus) * D11_REV80_TXH_TXC_PER_RU_INFO_SIZE)))

/**
 * Macros to find size of N-Users field in the TXCTL_EXT
 */
#define D11_REV80_TXH_TXC_EXT_N_USERs_FIELD_SIZE	1
#define D11_REV80_TXH_TXC_EXT_PER_USER_INFO_SIZE	4

#define D11_REV80_TXH_TXC_N_USERs_FIELD_SIZE(n_users) \
	((n_users) ? \
	 (((n_users) * \
	   (D11_REV80_TXH_TXC_EXT_PER_USER_INFO_SIZE)) + \
	  (D11_REV80_TXH_TXC_EXT_N_USERs_FIELD_SIZE)) :	\
	 (n_users))

/**
 * Size of each Tx Power Offset field in PhyTxCtlWord.
 */
#define D11_REV80_TXH_TXC_PWR_OFFSET_SIZE		1u

/**
 * Size of fixed / static fields in PhyTxCtlWord (all fields except N-RUs, N-Users and Pwr offsets)
 */
#define D11_REV80_TXH_TXC_CONST_FIELDS_SIZE		6u

/**
 * Macros used for filling PhyTxCtlWord
 */

/* PhyTxCtl Byte 0 */
#define D11_REV80_PHY_TXC_FT_MASK		0x0007u
#define D11_REV80_PHY_TXC_HE_FMT_MASK		0x0018u
#define D11_REV80_PHY_TXC_SOFT_AP_MODE		0x0020u
#define D11_REV80_PHY_TXC_NON_SOUNDING		0x0040u
#define D11_REV80_PHY_TXC_SHORT_PREAMBLE	0x0080u
#define D11_REV80_PHY_TXC_FRAME_TYPE_VHT	0X0003u
#define D11_REV80_PHY_TXC_FRAME_TYPE_HT		0X0002u
#define D11_REV80_PHY_TXC_FRAME_TYPE_LEG	0X0001u

#define D11_REV80_PHY_TXC_HE_FMT_SHIFT		3u

/* PhyTxCtl Byte 1 */
#define D11_REV80_PHY_TXC_STBC			0x0080u

/* PhyTxCtl Word 1 (Bytes 2 - 3) */
#define D11_REV80_PHY_TXC_DPCU_SUBBAND_SHIFT	5u
#define D11_REV80_PHY_TXC_DYNBW_PRESENT		0x2000u
#define D11_REV80_PHY_TXC_DYNBW_MODE		0x4000u
#define D11_REV80_PHY_TXC_MU			0x8000u
#define D11_REV80_PHY_TXC_BW_MASK		0x0003u
#define D11_REV80_PHY_TXC_BW_20MHZ		0x0000u
#define D11_REV80_PHY_TXC_BW_40MHZ		0x0001u
#define D11_REV80_PHY_TXC_BW_80MHZ		0x0002u
#define D11_REV80_PHY_TXC_BW_160MHZ		0x0003u
/* PhyTxCtl Word 2 (Bytes 4 -5) */
/* Though the width antennacfg, coremask fields are 8-bits,
 * only 4 bits is valid for 4369a0, hence masking only 4 bits
 */
#define D11_REV80_PHY_TXC_ANT_CONFIG_MASK		0x00F0u
#define D11_REV80_PHY_TXC_CORE_MASK			0x000Fu
#define D11_REV80_PHY_TXC_ANT_CONFIG_SHIFT		4u
/* upper byte- Ant. cfg, lower byte - Core  */
#define D11_REV80_PHY_TXC_ANT_CORE_MASK		0x0F0Fu

/* PhyTxCtl BFM field */
#define D11_REV80_PHY_TXC_BFM			0x80u

/* PhyTxCtl power offsets */
#define D11_REV80_PHY_TXC_PWROFS0_BYTE_POS	6u

/* Phytx Ctl Sub band location */
#define D11_REV80_PHY_TXC_SB_SHIFT		2u
#define D11_REV80_PHY_TXC_SB_MASK		0x001Cu

/* 11n phy rate */
#define D11_REV80_PHY_TXC_11N_MCS_MASK		0x003Fu
#define D11_REV80_PHY_TXC_11N_PROP_MCS		0x0040u /* this represents bit mcs[6] */

/* 11ac phy rate */
#define D11_REV80_PHY_TXC_11AC_NSS_SHIFT	4u

/* PhyTxCtl Word0  */
#define D11_REV80_PHY_TXC_MCS_NSS_MASK		0x7F00u
#define D11_REV80_PHY_TXC_MCS_MASK		0xF00u
#define D11_REV80_PHY_TXC_MCS_NSS_SHIFT		8u

/* 11ax phy rate */
#define D11_REV80_PHY_TXC_11AX_NSS_SHIFT	4u

#define D11_PHY_TXC_FT_MASK(corerev)	((D11REV_GE(corerev, 80)) ? D11_REV80_PHY_TXC_FT_MASK : \
					D11AC_PHY_TXC_FT_MASK)

/* PhyTxCtl Word 4 */
#define D11_REV80_PHY_TXC_HEHL_ENABLE              0x2000u

/* PhyTxCtl Word 5 */
#define D11_REV80_PHY_TXC_CORE0_PWR_OFFSET_SHIFT   8u
#define D11_REV80_PHY_TXC_CORE0_PWR_OFFSET_MASK    0xFF00u
/* PhyTxCtl Word 6 */
#define D11_REV80_PHY_TXC_CORE1_PWR_OFFSET_MASK    0x00FFu
/* Number of RU assigned */
#define D11_REV80_PHY_TXC_NRU                      0x0100u

/* A wrapper structure for all versions of TxD/d11txh structures */
typedef union d11txhdr {
	d11txh_pre40_t pre40;
	d11actxh_t rev40;
	d11txh_rev80_t rev80;
} d11txhdr_t;

/**
 * Generic tx status packet for software use. This is independent of hardware
 * structure for a particular core. Hardware structure should be read and converted
 * to this structure before being sent for the sofware consumption.
 */
typedef struct tx_status tx_status_t;
typedef struct tx_status_macinfo tx_status_macinfo_t;

BWL_PRE_PACKED_STRUCT struct tx_status_macinfo {
	int8 pad0;
	int8 is_intermediate;
	int8 pm_indicated;
	int8 pad1;
	uint8 suppr_ind;
	int8 was_acked;
	uint16 rts_tx_cnt;
	uint16 frag_tx_cnt;
	uint16 cts_rx_cnt;
	uint16 raw_bits;
	uint32 s3;
	uint32 s4;
	uint32 s5;
	uint32 s8;
	uint32 s9;
	uint32 s10;
	uint32 s11;
	uint32 s12;
	uint32 s13;
	uint32 s14;
	/* 128BA support */
	uint16 ncons_ext;
	uint16 s15;
	uint32 ack_map[8];
	/* pktlat */
	uint16 pkt_fetch_ts;	/* PSM Packet Fetch Time */
	uint16 med_acc_dly;	/* Medium Access Delay */
	uint16 rx_dur;		/* Rx duration */
	uint16 mac_susp_dur;	/* Mac Suspend Duration */
	uint16 txstatus_ts;	/* TxStatus Time */
	uint16 tx_en_cnt;	/* Number of times Tx was enabled */
	uint16 oac_txs_cnt;	/* Other AC TxStatus count */
	uint16 data_retx_cnt;	/* DataRetry count */
	uint16 pktlat_rsvd;	/* reserved */
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct tx_status {
	uint16 framelen;
	uint16 frameid;
	uint16 sequence;
	uint16 phyerr;
	uint32 lasttxtime;
	uint16 ackphyrxsh;
	uint16 procflags;	/* tx status processing flags */
	uint32 dequeuetime;
	tx_status_macinfo_t status;
} BWL_POST_PACKED_STRUCT;

/* Bits in struct tx_status procflags */
#define TXS_PROCFLAG_AMPDU_BA_PKG2_READ_REQD	0x1	/* AMPDU BA txs pkg2 read required */

/* status field bit definitions */
#define	TX_STATUS_FRM_RTX_MASK	0xF000
#define	TX_STATUS_FRM_RTX_SHIFT	12
#define	TX_STATUS_RTS_RTX_MASK	0x0F00
#define	TX_STATUS_RTS_RTX_SHIFT	8
#define TX_STATUS_MASK		0x00FE
#define	TX_STATUS_PMINDCTD	(1 << 7)	/**< PM mode indicated to AP */
#define	TX_STATUS_INTERMEDIATE	(1 << 6)	/**< intermediate or 1st ampdu pkg */
#define	TX_STATUS_AMPDU		(1 << 5)	/**< AMPDU status */
#define TX_STATUS_SUPR_MASK	0x1C		/**< suppress status bits (4:2) */
#define TX_STATUS_SUPR_SHIFT	2
#define	TX_STATUS_ACK_RCV	(1 << 1)	/**< ACK received */
#define	TX_STATUS_VALID		(1 << 0)	/**< Tx status valid (corerev >= 5) */
#define	TX_STATUS_NO_ACK	0
#define TX_STATUS_BE		(TX_STATUS_ACK_RCV | TX_STATUS_PMINDCTD)

/* TX_STATUS for fw initiated pktfree event */
#define TX_STATUS_SW_Q_FLUSH	0x10000

/* status field bit definitions phy rev > 40 */
#define TX_STATUS40_FIRST		0x0002
#define TX_STATUS40_INTERMEDIATE	0x0004
#define TX_STATUS40_PMINDCTD		0x0008

#define TX_STATUS40_SUPR		0x00f0
#define TX_STATUS40_SUPR_SHIFT		4

#define TX_STATUS40_NCONS		0x7f00

#define TX_STATUS40_NCONS_SHIFT		8

#define TX_STATUS40_ACK_RCV		0x8000

/* tx status bytes 8-16 */
#define TX_STATUS40_TXCNT_RATE0_MASK	0x000000ff
#define TX_STATUS40_TXCNT_RATE0_SHIFT	0

#define TX_STATUS40_TXCNT_RATE1_MASK	0x00ff0000
#define TX_STATUS40_TXCNT_RATE1_SHIFT	16

#define TX_STATUS40_MEDIUM_DELAY_MASK   0xFFFF

#define TX_STATUS40_TXCNT(s3, s4) \
	(((s3 & TX_STATUS40_TXCNT_RATE0_MASK) >> TX_STATUS40_TXCNT_RATE0_SHIFT) + \
	((s3 & TX_STATUS40_TXCNT_RATE1_MASK) >> TX_STATUS40_TXCNT_RATE1_SHIFT) + \
	((s4 & TX_STATUS40_TXCNT_RATE0_MASK) >> TX_STATUS40_TXCNT_RATE0_SHIFT) + \
	((s4 & TX_STATUS40_TXCNT_RATE1_MASK) >> TX_STATUS40_TXCNT_RATE1_SHIFT))

#define TX_STATUS40_TXCNT_RT0(s3) \
	((s3 & TX_STATUS40_TXCNT_RATE0_MASK) >> TX_STATUS40_TXCNT_RATE0_SHIFT)

#define TX_STATUS_EXTBA_TXCNT_BITS	0x3u
#define TX_STATUS_EXTBA_TXSUCCNT_BITS	0x1u
#define TX_STATUS_EXTBA_TXSIZE_RT	0x4u

#define TX_STATUS_EXTBA_TXCNT_RATE_MASK		0x7u
#define TX_STATUS_EXTBA_TXSUCCNT_RATE_MASK	0x8u

#define TX_STATUS_EXTBA_TXCNT_RATE_SHIFT	0x8u
#define TX_STATUS_EXTBA_TXSUCCNT_RATE_SHIFT	0x8u

#define TX_STATUS_EXTBA_TXCNT_RT(s15, rt) \
	((((s15) & (TX_STATUS_EXTBA_TXCNT_RATE_MASK << ((rt) * TX_STATUS_EXTBA_TXSIZE_RT))) >> \
	((rt) * TX_STATUS_EXTBA_TXSIZE_RT)) << TX_STATUS_EXTBA_TXCNT_RATE_SHIFT)

#define TX_STATUS_EXTBA_TXSUCCNT_RT(s15, rt) \
	((((s15) & (TX_STATUS_EXTBA_TXSUCCNT_RATE_MASK << ((rt) * TX_STATUS_EXTBA_TXSIZE_RT))) >> \
	(((rt) * TX_STATUS_EXTBA_TXSIZE_RT))) << TX_STATUS_EXTBA_TXSUCCNT_RATE_SHIFT)

#define TX_STATUS40_TX_MEDIUM_DELAY(txs)    ((txs)->status.s8 & TX_STATUS40_MEDIUM_DELAY_MASK)

/* chip rev 40 pkg 2 fields */
#define TX_STATUS40_IMPBF_MASK		0x0000000Cu	/* implicit bf applied */
#define TX_STATUS40_IMPBF_BAD_MASK	0x00000010u	/* impl bf applied but ack frm has no bfm */
#define TX_STATUS40_IMPBF_LOW_MASK	0x00000020u	/* ack received with low rssi */
#define TX_STATUS40_BFTX		0x00000040u	/* Beamformed pkt TXed */
/* pkt two status field bit definitions mac rev > 64 */
#define TX_STATUS64_MUTX		0x00000080u	/* Not used in STA-dongle chips */

/* pkt two status field bit definitions mac rev > 80 */

/* TXS rate cookie contains
 * mac rev 81/82 : RIT idx in bit[4:0] of RIB CtrlStat[0]
 * mac rev >= 83 : RIB version in bit[4:0] of RIB CtrlStat[1]
 */
#define TX_STATUS80_RATE_COOKIE_MASK	0x00003E00u
#define TX_STATUS80_RATE_COOKIE_SHIFT	9u
#define TX_STATUS80_NAV_HDR		0x00004000u /* NAV Overriden */

#define TX_STATUS80_TBPPDU_MASK		0x00000040u /* Indicates TBPPDU TX */
#define TX_STATUS80_TBPPDU_SHIFT	6u
#define TX_STATUS40_RTS_RTX_MASK	0x00ff0000u
#define TX_STATUS40_RTS_RTX_SHIFT	16u
#define TX_STATUS40_CTS_RRX_MASK	0xff000000u
#define TX_STATUS40_CTS_RRX_SHIFT	24u

/*
 * Intermediate status for TBPPDU (for stats purposes)
 * First uint16 word (word0 - status): VALID, !FIRST, INTERMEDIATE
 * Remaining word0 bits (3 - 15) are unasisgned
 */
#define TX_ITBSTATUS(status)		\
	(((status) & (TX_STATUS40_FIRST | TX_STATUS40_INTERMEDIATE)) == TX_STATUS40_INTERMEDIATE)
/* Remainder of first uint32 (words 0 and  1) */
#define TX_ITBSTATUS_LSIG_MASK		0x0000fff0u
#define TX_ITBSTATUS_LSIG_SHIFT		4u
#define TX_ITBSTATUS_TXPOWER_MASK	0xffff0000u
#define TX_ITBSTATUS_TXPOWER_SHIFT	16u
/* Second uint32  (words 2 and 3) */
#define TX_ITBSTATUS_NULL_DELIMS_MASK	0x0007ffffu /* 19 bits * 4B => ~2M bytes */
#define TX_ITBSTATUS_NULL_DELIMS_SHIFT	0u
#define TX_ITBSTATUS_ACKED_MPDUS_MASK	0x3ff80000u /* 11 bits: 0-2047 */
#define TX_ITBSTATUS_ACKED_MPDUS_SHIFT	19u
/* Third uint32 (words 4 and 5) */
#define TX_ITBSTATUS_SENT_MPDUS_MASK	0x0000ffe0u /* 11 bits: 0-2047 */
#define TX_ITBSTATUS_SENT_MPDUS_SHIFT	5u
#define TX_ITBSTATUS_APTXPWR_MASK	0x003f0000u /* 0-60 => -20 - 40 */
#define TX_ITBSTATUS_APTXPWR_SHIFT	16u
#define TX_ITBSTATUS_ULPKTEXT_MASK	0x01c00000u
#define TX_ITBSTATUS_ULPKTEXT_SHIFT	22u
#define TX_ITBSTATUS_MORETF_MASK	0x02000000u
#define TX_ITBSTATUS_MORETF_SHIFT	25u
#define TX_ITBSTATUS_CSREQ_MASK		0x04000000u
#define TX_ITBSTATUS_CSREQ_SHIFT	26u
#define TX_ITBSTATUS_ULBW_MASK		0x18000000u
#define TX_ITBSTATUS_ULBW_SHIFT		27u
#define TX_ITBSTATUS_GI_LTF_MASK	0x60000000u
#define TX_ITBSTATUS_GI_LTF_SHIFT	29u
#define TX_ITBSTATUS_MUMIMO_LTF_MASK	0x80000000u
#define TX_ITBSTATUS_MUMIMO_LTF_SHIFT	30u
/* Fourth uint32 (words 6 and 7) */
#define TX_ITBSTATUS_CODING_TYPE_MASK	0x00000001u
#define TX_ITBSTATUS_CODING_TYPE_SHIFT	0u
#define TX_ITBSTATUS_MCS_MASK		0x0000001eu
#define TX_ITBSTATUS_MCS_SHIFT		1u
#define TX_ITBSTATUS_DCM_MASK		0x00000020u
#define TX_ITBSTATUS_DCM_SHIFT		5u
#define TX_ITBSTATUS_RU_ALLOC_MASK	0x00003fc0u
#define TX_ITBSTATUS_RU_ALLOC_SHIFT	6u
/* Bits 14 and 15 unassigned */
#define TX_ITBSTATUS_NSS_MASK		0x00030000u
#define TX_ITBSTATUS_NSS_SHIFT		16u
#define TX_ITBSTATUS_TARGET_RSSI_MASK	0x03fc0000u
#define TX_ITBSTATUS_TARGET_RSSI_SHIFT	18u
#define TX_ITBSTATUS_RA_RU_MASK		0x04000000u
#define TX_ITBSTATUS_RA_RU_SHIFT	26u
/* Bits 27 through 31 unassigned */
/* End of intermediate TBPPDU txstatus definitions */

/* MU group info txstatus field (s3 b[31:16]) */
#define TX_STATUS64_MU_GID_MASK		0x003f0000u
#define TX_STATUS64_MU_GID_SHIFT	16u
#define TX_STATUS64_MU_BW_MASK		0x00c00000u
#define TX_STATUS64_MU_BW_SHIFT		22u
#define TX_STATUS64_MU_TXPWR_MASK	0x7f000000u
#define TX_STATUS64_MU_TXPWR_SHIFT	24u
#define TX_STATUS64_MU_SGI_MASK		0x80000080u
#define TX_STATUS64_MU_SGI_SHIFT	31u
#define TX_STATUS64_INTERM_MUTXCNT(s3) \
	((s3 & TX_STATUS40_TXCNT_RATE0_MASK) >> TX_STATUS40_TXCNT_RATE0_SHIFT)

#define TX_STATUS64_MU_GID(s3) ((s3 & TX_STATUS64_MU_GID_MASK) >> TX_STATUS64_MU_GID_SHIFT)
#define TX_STATUS64_MU_BW(s3) ((s3 & TX_STATUS64_MU_BW_MASK) >> TX_STATUS64_MU_BW_SHIFT)
#define TX_STATUS64_MU_TXPWR(s3) ((s3 & TX_STATUS64_MU_TXPWR_MASK) >> TX_STATUS64_MU_TXPWR_SHIFT)
#define TX_STATUS64_MU_SGI(s3) ((s3 & TX_STATUS64_MU_SGI_MASK) >> TX_STATUS64_MU_SGI_SHIFT)

/* MU user info0 txstatus field (s4 b[15:0]) */
#define TX_STATUS64_MU_MCS_MASK		0x0000000f
#define TX_STATUS64_MU_MCS_SHIFT	0
#define TX_STATUS64_MU_NSS_MASK		0x00000070
#define TX_STATUS64_MU_NSS_SHIFT	4
#define TX_STATUS64_MU_SNR_MASK		0x0000ff00
#define TX_STATUS64_MU_SNR_SHIFT	8

#define TX_STATUS64_MU_MCS(s4) ((s4 & TX_STATUS64_MU_MCS_MASK) >> TX_STATUS64_MU_MCS_SHIFT)
#define TX_STATUS64_MU_NSS(s4) ((s4 & TX_STATUS64_MU_NSS_MASK) >> TX_STATUS64_MU_NSS_SHIFT)
#define TX_STATUS64_MU_SNR(s4) ((s4 & TX_STATUS64_MU_SNR_MASK) >> TX_STATUS64_MU_SNR_SHIFT)

/* MU txstatus rspec field (NSS | MCS) */
#define TX_STATUS64_MU_RSPEC_MASK	(TX_STATUS64_MU_NSS_MASK | TX_STATUS64_MU_MCS_MASK)
#define TX_STATUS64_MU_RSPEC_SHIFT	0

#define TX_STATUS64_MU_RSPEC(s4) ((s4 & TX_STATUS64_MU_RSPEC_MASK) >> TX_STATUS64_MU_RSPEC_SHIFT)

/* MU user info0 txstatus field (s4 b[31:16]) */
#define TX_STATUS64_MU_GBMP_MASK	0x000f0000
#define TX_STATUS64_MU_GBMP_SHIFT	16
#define TX_STATUS64_MU_GPOS_MASK	0x00300000
#define TX_STATUS64_MU_GPOS_SHIFT	20
#define TX_STATUS64_MU_TXCNT_MASK	0x0fc00000
#define TX_STATUS64_MU_TXCNT_SHIFT	22

#define TX_STATUS64_MU_GBMP(s4) ((s4 & TX_STATUS64_MU_GBMP_MASK) >> TX_STATUS64_MU_GBMP_SHIFT)
#define TX_STATUS64_MU_GPOS(s4) ((s4 & TX_STATUS64_MU_GPOS_MASK) >> TX_STATUS64_MU_GPOS_SHIFT)
#define TX_STATUS64_MU_TXCNT(s4) ((s4 & TX_STATUS64_MU_TXCNT_MASK) >> TX_STATUS64_MU_TXCNT_SHIFT)

#define HE_MU_APTX_PWR_MAX			60u
#define HE_TXS_MU_APTX_PWR_DBM(aptx_pwr)	((aptx_pwr) - 20u)

#define HE_TXS_MU_TARGET_RSSI_RANG		90
#define HE_TXS_MU_TARGET_RSSI_MAX_PWR		127
#define HE_TXS_MU_TARGET_RSSI_DBM(rssi)		((rssi) - 110)

#define HE_TXS_W4_MU_GET_RU_INDEX(index)	((index <= HE_MAX_26_TONE_RU_INDX) ? 0u : \
						((index) <= HE_MAX_52_TONE_RU_INDX) ? 1u : \
						((index) <= HE_MAX_106_TONE_RU_INDX) ? 2u : \
						((index) <= HE_MAX_242_TONE_RU_INDX) ? 3u : \
						((index) <= HE_MAX_484_TONE_RU_INDX) ? 4u :\
						((index) <= HE_MAX_996_TONE_RU_INDX) ? 5u : 6u)

/* Bit 8 indicates upper 80 MHz */
#define HE_TXS_W4_MU_RU_INDEX_RU_INDEX_MASK	0x7Fu
#define HE_TXS_W4_MU_RU_INDEX_TONE(index)	HE_TXS_W4_MU_GET_RU_INDEX(((index) & \
						HE_TXS_W4_MU_RU_INDEX_RU_INDEX_MASK))

#define HE_TXS_W3_MU_APTX_PWR_MASK		0x003F0000u
#define HE_TXS_W3_MU_APTX_PWR_SHIFT		16u
#define HE_TXS_W3_MU_PKT_EXT_MASK		0x01C00000u
#define HE_TXS_W3_MU_PKT_EXT_SHIFT		22u
#define HE_TXS_W3_MU_MORE_TF_MASK		0x02000000u
#define HE_TXS_W3_MU_MORE_TF_SHIFT		25u
#define HE_TXS_W3_MU_CS_REQ_MASK		0x04000000u
#define HE_TXS_W3_MU_CS_REQ_SHIFT		26u
#define HE_TXS_W3_MU_UL_BW_MASK			0x18000000u
#define HE_TXS_W3_MU_UL_BW_SHIFT		27u
#define HE_TXS_W3_MU_GI_LTF_MASK		0x60000000u
#define HE_TXS_W3_MU_GI_LTF_SHIFT		29u
#define HE_TXS_W3_MU_MIMO_LTF_MASK		0x80000000u
#define HE_TXS_W3_MU_MIMO_LTF_SHIFT		31u

#define HE_TXS_W3_MU_APTX_PWR(s3)		(((s3) & HE_TXS_W3_MU_APTX_PWR_MASK) >> \
						HE_TXS_W3_MU_APTX_PWR_SHIFT)
#define HE_TXS_W3_MU_PKT_EXT(s3)		(((s3) & HE_TXS_W3_MU_PKT_EXT_MASK) >> \
						HE_TXS_W3_MU_PKT_EXT_SHIFT)
#define HE_TXS_W3_MU_MORE_TF(s3)		(((s3) & HE_TXS_W3_MU_MORE_TF_MASK) >> \
						HE_TXS_W3_MU_MORE_TF_SHIFT)
#define HE_TXS_W3_MU_CS_REQ(s3)			(((s3) & HE_TXS_W3_MU_CS_REQ_MASK) >> \
						HE_TXS_W3_MU_CS_REQ_SHIFT)
#define HE_TXS_W3_MU_UL_BW(s3)			(((s3) & HE_TXS_W3_MU_UL_BW_MASK) >> \
						HE_TXS_W3_MU_UL_BW_SHIFT)
#define HE_TXS_W3_MU_GI_LTF(s3)			(((s3) & HE_TXS_W3_MU_GI_LTF_MASK) >> \
						HE_TXS_W3_MU_GI_LTF_SHIFT)
#define HE_TXS_W3_MU_MIMO_LT(s3)		(((s3) & HE_TXS_W3_MU_MIMO_LTF_MASK) >> \
						HE_TXS_W3_MU_MIMO_LTF_SHIFT)

#define HE_TXS_W4_MU_CODINF_TYPE_MASK		0x00000001u
#define HE_TXS_W4_MU_CODINF_TYPE_SHIFT		0u
#define HE_TXS_W4_MU_MCS_MASK			0x0000001Eu
#define HE_TXS_W4_MU_MCS_SHIFT			1u
#define HE_TXS_W4_MU_DCM_MASK			0x00000020u
#define HE_TXS_W4_MU_DCM_SHIFT			5u
#define HE_TXS_W4_RU_ALLOCATION_MASK		0x00003FC0u
#define HE_TXS_W4_RU_ALLOCATION_SHIFT		6u

#define HE_TXS_W4_MU_CODINF_TYPE(s4)		(((s4) & HE_TXS_W4_MU_CODINF_TYPE_MASK) >> \
						HE_TXS_W4_MU_CODINF_TYPE_SHIFT)
#define HE_TXS_W4_MU_MCS(s4)			(((s4) & HE_TXS_W4_MU_MCS_MASK) >> \
						HE_TXS_W4_MU_MCS_SHIFT)
#define HE_TXS_W4_MU_DCM(s4)			(((s4) & HE_TXS_W4_MU_DCM_MASK) >> \
						HE_TXS_W4_MU_DCM_SHIFT)
#define HE_TXS_W4_RU_ALLOCATION(s4)		(((s4) & HE_TXS_W4_RU_ALLOCATION_MASK) >> \
						HE_TXS_W4_RU_ALLOCATION_SHIFT)

#define HE_TXS_W4_MU_NSS_MASK			0x00030000u
#define HE_TXS_W4_MU_NSS_SHIFT			16u
#define HE_TXS_W4_MU_TARGET_RSSI_MASK		0x03FC0000u
#define HE_TXS_W4_MU_TARGET_RSSI_SHIFT		18u

#define HE_TXS_W4_MU_NSS(s4)			(((s4) & HE_TXS_W4_MU_NSS_MASK) >> \
						HE_TXS_W4_MU_NSS_SHIFT)
#define HE_TXS_W4_MU_TARGET_RSSI(s4)		(((s4) & HE_TXS_W4_MU_TARGET_RSSI_MASK) >> \
						HE_TXS_W4_MU_TARGET_RSSI_SHIFT)

/* WARNING: Modifying suppress reason codes?
 * Update wlc_tx_status_t and TX_STS_REASON_STRINGS and
 * wlc_tx_status_map_hw_to_sw_supr_code() also
 */
/* status field bit definitions */
/** suppress status reason codes */
enum  {
	TX_STATUS_SUPR_NONE =       0,
	TX_STATUS_SUPR_PMQ =        1,	/**< PMQ entry */
	TX_STATUS_SUPR_FLUSH =      2,	/**< flush request */
	TX_STATUS_SUPR_FRAG =       3,	/**< previous frag failure */
	TX_STATUS_SUPR_TBTT =       3,	/**< SHARED: Probe response supr for TBTT */
	TX_STATUS_SUPR_BADCH =      4,	/**< channel mismatch */
	TX_STATUS_SUPR_EXPTIME =    5,	/**< lifetime expiry */
	TX_STATUS_SUPR_UF =         6,	/**< underflow */
#ifdef WLP2P_UCODE
	TX_STATUS_SUPR_NACK_ABS =   7,	/**< BSS entered ABSENCE period */
#endif
	TX_STATUS_SUPR_PPS   =      8,	/**< Pretend PS */
	TX_STATUS_SUPR_PHASE1_KEY = 9,	/**< Request new TKIP phase-1 key */
	TX_STATUS_UNUSED =          10,	/**< Unused in trunk */
	TX_STATUS_INT_XFER_ERR =    11, /**< Internal DMA xfer error */
	TX_STATUS_SUPR_TWT_SP_OUT = 12, /**< Suppress Tx outside TWTSP */
	NUM_TX_STATUS_SUPR
};

/** Unexpected tx status for rate update */
#define TX_STATUS_UNEXP(status) \
	((((status.is_intermediate))) && \
	 TX_STATUS_UNEXP_AMPDU(status))

/** Unexpected tx status for A-MPDU rate update */
#ifdef WLP2P_UCODE
#define TX_STATUS_UNEXP_AMPDU(status) \
	((((status.suppr_ind)) != TX_STATUS_SUPR_NONE) && \
	 (((status.suppr_ind)) != TX_STATUS_SUPR_EXPTIME) && \
	 (((status.suppr_ind)) != TX_STATUS_SUPR_NACK_ABS))
#else
#define TX_STATUS_UNEXP_AMPDU(status) \
	((((status.suppr_ind)) != TX_STATUS_SUPR_NONE) && \
	 (((status.suppr_ind)) != TX_STATUS_SUPR_EXPTIME))
#endif

/**
 * This defines the collection of supp reasons (including none)
 * for which mac has done its (re-)transmission in any of ucode retx schemes
 * which include ucode/hw/aqm agg
 */
#define TXS_SUPR_MAGG_DONE_MASK ((1 << TX_STATUS_SUPR_NONE) | \
		(1 << TX_STATUS_SUPR_UF) |   \
		(1 << TX_STATUS_SUPR_FRAG) | \
		(1 << TX_STATUS_SUPR_EXPTIME))
#define TXS_SUPR_MAGG_DONE(suppr_ind) \
		((1 << (suppr_ind)) & TXS_SUPR_MAGG_DONE_MASK)

#define TX_STATUS_BA_BMAP03_MASK	0xF000	/**< ba bitmap 0:3 in 1st pkg */
#define TX_STATUS_BA_BMAP03_SHIFT	12	/**< ba bitmap 0:3 in 1st pkg */
#define TX_STATUS_BA_BMAP47_MASK	0x001E	/**< ba bitmap 4:7 in 2nd pkg */
#define TX_STATUS_BA_BMAP47_SHIFT	3	/**< ba bitmap 4:7 in 2nd pkg */

/* RXE (Receive Engine) */

/* RCM_CTL */
#define	RCM_INC_MASK_H		0x0080
#define	RCM_INC_MASK_L		0x0040
#define	RCM_INC_DATA		0x0020
#define	RCM_INDEX_MASK		0x001F
#define	RCM_SIZE		15

#define	RCM_MAC_OFFSET		0	/**< current MAC address */
#define	RCM_BSSID_OFFSET	3	/**< current BSSID address */
#define	RCM_F_BSSID_0_OFFSET	6	/**< foreign BSS CFP tracking */
#define	RCM_F_BSSID_1_OFFSET	9	/**< foreign BSS CFP tracking */
#define	RCM_F_BSSID_2_OFFSET	12	/**< foreign BSS CFP tracking */

#define RCM_WEP_TA0_OFFSET	16
#define RCM_WEP_TA1_OFFSET	19
#define RCM_WEP_TA2_OFFSET	22
#define RCM_WEP_TA3_OFFSET	25

/* AMT - Address Match Table */

/* AMT Attribute bits */
#define AMT_ATTR_VALID          0x8000	/**< Mark the table entry valid */
#define AMT_ATTR_A1             0x0008	/**< Match for A1 */
#define AMT_ATTR_A2             0x0004	/**< Match for A2 */
#define AMT_ATTR_A3             0x0002	/**< Match for A3 */

/* AMT Index defines */
#define AMT_SIZE_64		64  /* number of AMT entries */
#define AMT_SIZE_128		128 /* number of AMT entries for corerev >= 64 */
#define AMT_IDX_MAC		63	/**< device MAC */
#define AMT_IDX_BSSID		62	/**< BSSID match */
#define AMT_IDX_TRANSMITTED_BSSID      60 /**< transmitted BSSID in multiple BSSID set */
#define AMT_WORD_CNT		2	/* Number of word count per AMT entry */

#define AMT_SIZE(_corerev)	(D11REV_GE(_corerev, 64) ? \
	(D11REV_GE(_corerev, 80) ? AMT_SIZE_64 : AMT_SIZE_128) : \
	AMT_SIZE_64)

/* RMC entries */
#define AMT_IDX_MCAST_ADDR	61	/**< MCAST address for Reliable Mcast feature */
#define AMT_IDX_MCAST_ADDR1	59	/**< MCAST address for Reliable Mcast feature */
#define AMT_IDX_MCAST_ADDR2	58	/**< MCAST address for Reliable Mcast feature */
#define AMT_IDX_MCAST_ADDR3	57	/**< MCAST address for Reliable Mcast feature */

#ifdef WLMESH
/* note: this is max supported by ucode. But ARM-driver can
 * only mesh_info->mesh_max_peers which should be <= this value.
 */

#define AMT_MAX_MESH_PEER		10
#define AMT_MAXIDX_MESH_PEER            60
#define AMT_MAXIDX_P2P_USE	\
	(AMT_MAXIDX_MESH_PEER - AMT_MAX_MESH_PEER)
#else
#define AMT_MAXIDX_P2P_USE	60	/**< Max P2P entry to use */
#endif /* WL_STA_MONITOR */
#define AMT_MAX_TXBF_ENTRIES	7	/**< Max tx beamforming entry */
/* PSTA AWARE AP: Max PSTA Tx beamforming entry */
#define AMT_MAX_TXBF_PSTA_ENTRIES	20

/* M_AMT_INFO SHM bit field definition */
#define AMTINFO_BMP_IBSS	(1u << 0u)	/* IBSS Station */
#define AMTINFO_BMP_MESH	(1u << 1u)	/* MESH Station */
#define AMTINFO_BMP_BSSID	(1u << 2u)	/* BSSID-only */
#define AMTINFO_BMP_IS_WAPI	(1u << 3u)	/* For WAPI keyid extraction */
#define AMTINFO_BMP_IS_HE	(1u << 13u)	/* For HE peer indication */

#define AUXPMQ_ENTRIES		64  /* number of AUX PMQ entries */
#define AUXPMQ_ENTRY_SIZE       8

/* PSM Block */

/* psm_phy_hdr_param bits */
#define MAC_PHY_RESET		1
#define MAC_PHY_CLOCK_EN	2
#define MAC_PHY_FORCE_CLK	4
#define MAC_IHRP_CLOCK_EN	15

/* PSMCoreControlStatus (IHR Address 0x078) bit definitions */
#define PSM_CORE_CTL_AR		(1 << 0)
#define PSM_CORE_CTL_HR		(1 << 1)
#define PSM_CORE_CTL_IR		(1 << 2)
#define PSM_CORE_CTL_AAR	(1 << 3)
#define PSM_CORE_CTL_HAR	(1 << 4)
#define PSM_CORE_CTL_PPAR	(1 << 5)
#define PSM_CORE_CTL_SS		(1 << 6)
#define PSM_CORE_CTL_REHE	(1 << 7)
#define PSM_CORE_CTL_PPAS	(1 << 13)
#define PSM_CORE_CTL_AAS	(1 << 14)
#define PSM_CORE_CTL_HAS	(1 << 15)

#define PSM_CORE_CTL_LTR_BIT	9
#define PSM_CORE_CTL_LTR_MASK	0x3

#define PSM_SBACCESS_FIFO_MODE	(1 << 1)
#define PSM_SBACCESS_EXT_ERR	(1 << 11)

/* WEP Block */

/* WEP_WKEY */
#define	WKEY_START		(1 << 8)
#define	WKEY_SEL_MASK		0x1F

/* WEP data formats */

/* the number of RCMTA entries */
#define RCMTA_SIZE 50

/* max keys in M_TKMICKEYS_BLK - 96 * sizeof(uint16) */
#define	WSEC_MAX_TKMIC_ENGINE_KEYS(_corerev) ((D11REV_GE(_corerev, 64)) ? \
	AMT_SIZE(_corerev) : 12) /* 8 + 4 default - 2 mic keys 8 bytes each */

/* max keys in M_WAPIMICKEYS_BLK - 64 * sizeof(uint16) */
#define	WSEC_MAX_SMS4MIC_ENGINE_KEYS(_corerev) ((D11REV_GE(_corerev, 64)) ? \
	AMT_SIZE(_corerev) : 8)  /* 4 + 4 default  - 16 bytes each */

/* max RXE match registers */
#define WSEC_MAX_RXE_KEYS	4

/* SECKINDXALGO (Security Key Index & Algorithm Block) word format */
/* SKL (Security Key Lookup) */
#define	SKL_POST80_ALGO_MASK	0x000F
#define	SKL_PRE80_ALGO_MASK	0x0007
#define	SKL_ALGO_SHIFT		0

#define	SKL_ALGO_MASK(_corerev)	(D11REV_GE(_corerev, 80) ? SKL_POST80_ALGO_MASK : \
				SKL_PRE80_ALGO_MASK)

#define	SKL_WAPI_KEYID_MASK	0x8000
#define	SKL_WAPI_KEYID_SHIFT	15
#define	SKL_INDEX_SHIFT		4

#define	SKL_PRE80_WAPI_KEYID_MASK	0x0008
#define	SKL_PRE80_WAPI_KEYID_SHIFT	3

#define SKL_INDEX_MASK(_corerev)   ((D11REV_GE(_corerev, 64)) ? \
	(0x0FF0) : (0x03F0))
#define SKL_GRP_ALGO_MASK(_corerev)   ((D11REV_GE(_corerev, 64)) ? \
	((D11REV_GE(_corerev, 80)) ? (0xE000) : (0x7000)) : (0x1c00))
#define SKL_GRP_ALGO_SHIFT(_corerev)   ((D11REV_GE(_corerev, 64)) ? \
	((D11REV_GE(_corerev, 80)) ? (13) : (12)) : (10))

#define	SKL_STAMON_NBIT		0x8000 /* STA monitor bit */

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

/* Mapped as per HW_ALGO */
#define	WSEC_ALGO_OFF			0
#define	WSEC_ALGO_WEP1			1
#define	WSEC_ALGO_TKIP			2
#define	WSEC_ALGO_WEP128		3
#define	WSEC_ALGO_AES_LEGACY		4
#define	WSEC_ALGO_AES			5
#define	WSEC_ALGO_SMS4			6
#define	WSEC_ALGO_SMS4_DFT_2005_09_07	7	/**< Not used right now */
#define	WSEC_ALGO_NALG			8

/* For CORE_REV 80 */
#define	WSEC_ALGO_AES_GCM		8
#define	WSEC_ALGO_AES_GCM256		9

/* For CORE_REV Less than 80 and */
#define	WSEC_ALGO_AES_PRE80_GCM		6
#define	WSEC_ALGO_AES_PRE80_GCM256	8

/* D11 MAX TTAK INDEX */
#define TSC_TTAK_PRE80_MAX_INDEX 50
#define TSC_TTAK_MAX_INDEX 8
/* D11 COREREV 80 TTAK KEY INDEX SHIFT */
#define	SKL_TTAK_INDEX_SHIFT		13
#define	SKL_TTAK_INDEX_MASK	0xE000

/* D11 PRECOREREV 40 Hw algos...changed from corerev 40 */
#define	D11_PRE40_WSEC_ALGO_AES		3
#define	D11_PRE40_WSEC_ALGO_WEP128	4
#define	D11_PRE40_WSEC_ALGO_AES_LEGACY	5
#define	D11_PRE40_WSEC_ALGO_SMS4	6
#define	D11_PRE40_WSEC_ALGO_NALG	7

#define D11_WSEC_ALGO_AES(_corerev)	WSEC_ALGO_AES

#define	AES_MODE_NONE		0
#define	AES_MODE_CCM		1
#define	AES_MODE_OCB_MSDU	2
#define	AES_MODE_OCB_MPDU	3
#define	AES_MODE_CMAC		4
#define	AES_MODE_GCM		5
#define	AES_MODE_GMAC		6

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
#define	T_P2P_NULL_TPL_BASE	(0x340 * 2)
#define	T_P2P_NULL_TPL_SIZE	(32)
#define T_TRIG_TPL_BASE		(0x90 * 2)

/* FCBS base addresses and sizes in BM */

#define FCBS_DS0_BM_CMD_SZ_CORE0	0x0200	/* 512 bytes */
#define FCBS_DS0_BM_DAT_SZ_CORE0	0x0200	/* 512 bytes */

#ifndef FCBS_DS0_BM_CMDPTR_BASE_CORE0
#define FCBS_DS0_BM_CMDPTR_BASE_CORE0	0x3000
#endif
#define FCBS_DS0_BM_DATPTR_BASE_CORE0	(FCBS_DS0_BM_CMDPTR_BASE_CORE0 + FCBS_DS0_BM_CMD_SZ_CORE0)

#define FCBS_DS0_BM_CMD_SZ_CORE1	0x0200	/* 512 bytes */
#define FCBS_DS0_BM_DAT_SZ_CORE1	0x0200	/* 512 bytes */

#ifndef FCBS_DS0_BM_CMDPTR_BASE_CORE1
#define FCBS_DS0_BM_CMDPTR_BASE_CORE1	0x2400
#endif
#define FCBS_DS0_BM_DATPTR_BASE_CORE1	(FCBS_DS0_BM_CMDPTR_BASE_CORE1 + FCBS_DS0_BM_CMD_SZ_CORE1)

#define FCBS_DS0_BM_CMD_SZ_CORE2	0x0200	/* 512 bytes */
#define FCBS_DS0_BM_DAT_SZ_CORE2	0x0200	/* 512 bytes */

#define FCBS_DS1_BM_CMD_SZ_CORE0	0x2000	/* Not used */
#define FCBS_DS1_BM_DAT_SZ_CORE0	0x2000	/* Not used */

#define FCBS_DS1_BM_CMDPTR_BASE_CORE0	0x17B4
#define FCBS_DS1_BM_DATPTR_BASE_CORE0	(FCBS_DS1_BM_CMDPTR_BASE_CORE0 + FCBS_DS1_BM_CMD_SZ_CORE0)

#define FCBS_DS1_BM_CMD_SZ_CORE1	0x2000	/* Not used */
#define FCBS_DS1_BM_DAT_SZ_CORE1	0x2000	/* Not used */

#define FCBS_DS1_BM_CMDPTR_BASE_CORE1	0x17B4
#define FCBS_DS1_BM_DATPTR_BASE_CORE1	(FCBS_DS1_BM_CMDPTR_BASE_CORE1 + FCBS_DS1_BM_CMD_SZ_CORE1)

#define T_BA_TPL_BASE		T_QNULL_TPL_BASE	/**< template area for BA */

#define T_RAM_ACCESS_SZ		4	/**< template ram is 4 byte access only */

#define TPLBLKS_PER_BCN_NUM	2
#define TPLBLKS_AC_PER_BCN_NUM	1

#if defined(WLLPRS) && defined(MBSS)
#define TPLBLKS_PER_PRS_NUM	4
#define TPLBLKS_AC_PER_PRS_NUM	2
#else
#define TPLBLKS_PER_PRS_NUM	2
#define TPLBLKS_AC_PER_PRS_NUM	1
#endif /* WLLPRS && MBSS */

/* MAC Sample Collect Params */

/* SampleCapture set-up options in
 * different registers based on CoreRev
 */
/* CoreRev >= 50, use SMP_CTRL in TXE_IHR */
#define SC_SRC_MAC		2 /* MAC as Sample Collect Src */
#define SC_SRC_SHIFT		3 /* SC_SRC bits [3:4] */
#define	SC_TRIG_SHIFT		5
#define SC_TRANS_SHIFT		6
#define SC_MATCH_SHIFT		7
#define SC_STORE_SHIFT		8

#define SC_STRT		1
#define SC_TRIG_EN	(1 << SC_TRIG_SHIFT)
#define SC_TRANS_EN	(1 << SC_TRANS_SHIFT)
#define SC_MATCH_EN	(1 << SC_MATCH_SHIFT)
#define SC_STORE_EN	(1 << SC_STORE_SHIFT)

/* CoreRev < 50, use PHY_CTL in PSM_IHR */
#define PHYCTL_PHYCLKEN		(1 << 1)
#define PHYCTL_FORCE_GATED_CLK_ON		(1 << 2)
#define PHYCTL_SC_STRT		(1 << 4)
#define PHYCTL_SC_SRC_LB	(1 << 7)
#define PHYCTL_SC_TRIG_EN	(1 << 8)
#define PHYCTL_SC_TRANS_EN	(1 << 9)
#define PHYCTL_SC_STR_EN	(1 << 10)
#define PHYCTL_IHRP_CLK_EN	(1 << 15)
/* End MAC Sample Collect Params */

#define ANTSEL_CLKDIV_4MHZ	6
#define MIMO_ANTSEL_BUSY	0x4000		/**< bit 14 (busy) */
#define MIMO_ANTSEL_SEL		0x8000		/**< bit 15 write the value */
#define MIMO_ANTSEL_WAIT	50		/**< 50us wait */
#define MIMO_ANTSEL_OVERRIDE	0x8000		/**< flag */

typedef struct shm_acparams shm_acparams_t;
BWL_PRE_PACKED_STRUCT struct shm_acparams {
	uint16	txop;
	uint16	cwmin;
	uint16	cwmax;
	uint16	cwcur;
	uint16	aifs;
	uint16	bslots;
	uint16	reggap;
	uint16	status;
	uint16  txcnt;
	uint16	rsvd[7];
} BWL_POST_PACKED_STRUCT;

#define WME_STATUS_NEWAC	(1 << 8)

/* M_HOST_FLAGS */
#define MHFMAX		5 /* Number of valid hostflag half-word (uint16) */
#define MHF1		0 /* Hostflag 1 index */
#define MHF2		1 /* Hostflag 2 index */
#define MHF3		2 /* Hostflag 3 index */
#define MHF4		3 /* Hostflag 4 index */
#define MHF5		4 /* Hostflag 5 index */

#define MXHFMAX		1 /* Number of valid PSMx hostflag half-word (uint16) */
#define MXHF0		64 /* PSMx Hostflag 0 index */

/* Flags in M_HOST_FLAGS */
#define	MHF1_D11AC_DYNBW	0x0001	/**< dynamic bw */
#define MHF1_WLAN_CRITICAL	0x0002	/**< WLAN is in critical state */
#define	MHF1_MBSS_EN		0x0004	/**< Enable MBSS: RXPUWAR deprecated for rev >= 9 */
#define	MHF1_BTCOEXIST		0x0010	/**< Enable Bluetooth / WLAN coexistence */
#define	MHF1_P2P_SKIP_TIME_UPD	0x0020	/**< Skip P2P SHM updates and P2P event generations */
#define	MHF1_TXMUTE_WAR		0x0040	/**< ucode based Tx mute */
#define	MHF1_RXFIFO1		0x0080	/**< Switch data reception from RX fifo 0 to fifo 1 */
#define	MHF1_EDCF		0x0100	/**< Enable EDCF access control */
#define MHF1_ULP		0x0200	/**< Force Ucode to put chip in low power state */
#define	MHF1_FORCE_SEND_BCN	0x0800	/**< Force send bcn, even if rcvd from peer STA (IBSS) */
#define	MHF1_TIMBC_EN		0x1000	/**< Enable Target TIM Transmission Time function */
#define MHF1_RADARWAR		0x2000	/**< Enable Radar Detect WAR PR 16559 */
#define MHF1_DEFKEYVALID	0x4000	/**< Enable use of the default keys */
#define	MHF1_CTS2SELF		0x8000	/**< Enable CTS to self full phy bw protection */

/* Flags in M_HOST_FLAGS2 */
#define MHF2_DISABLE_PRB_RESP	0x0001	/**< disable Probe Response in ucode */
#define MHF2_HIB_FEATURE_ENABLE	0x0008	/* Enable HIB feature in ucode (60<=rev<80) */
#define MHF2_SKIP_ADJTSF	0x0010	/**< skip TSF update when receiving bcn/probeRsp */
#define MHF2_RSPBW20		0x0020	/**< Uses bw20 for response frames ack/ba/cts */
#define MHF2_TXBCMC_NOW		0x0040	/**< Flush BCMC FIFO immediately */
#define MHF2_PPR_HWPWRCTL	0x0080	/**< TSSI_DIV WAR (rev<80) */
#define MHF2_BTC2WIRE_ALTGPIO	0x0100	/**< BTC 2wire in alternate pins */
#define MHF2_BTCPREMPT		0x0200	/**< BTC enable bluetooth check during tx */
#define MHF2_SKIP_CFP_UPDATE	0x0400	/**< Skip CFP update ; for d11 rev <= 80 */
#define MHF2_TX_TMSTMP		0x0800	/**< Enable passing tx-timestamps in tx-status */
#define MHF2_UFC_GE84		0x2000	/**< Enable UFC in CT mode */
#define MHF2_NAV_NORST_WAR	0x4000	/**< WAR to use rogue NAV duration */
#define MHF2_BTCANTMODE		0x4000	// OBSOLETE (TO BE REMOVED)

/* Flags in M_HOST_FLAGS3 */
#define MHF3_ANTSEL_EN		0x0001	/**< enabled mimo antenna selection (REV<80) */
#define MHF3_TKIP_FRAG_WAR	0x0001	/**< TKIP fragment corrupt WAR (REV>=80) */
#define MHF3_TXSHAPER_EN	0x0002	/** enable tx shaper for non-OFDM-A frames */
#define MHF3_ANTSEL_MODE	0x0002	/**< antenna selection mode: 0: 2x3, 1: 2x4 (REV<80) */
#define MHF3_BTCX_DEF_BT	0x0004	/**< corerev >= 13 BT Coex. */
#define MHF3_BTCX_ACTIVE_PROT	0x0008	/**< corerev >= 13 BT Coex. */
#define MHF3_PKTENG_PROMISC	0x0010	/**< pass frames to driver in packet engine Rx mode */
#define MHF3_SCANCORE_PM_EN	0x0040	/**< enable ScanCore PM from ucode */
#define MHF3_PM_BCNRX		0x0080	/**< PM single core beacon RX for power reduction */
#define MHF3_BTCX_SIM_RSP	0x0100	/**< allow limited lwo power tx when BT is active */
#define MHF3_BTCX_PS_PROTECT	0x0200	/**< use PS mode to protect BT activity */
#define MHF3_BTCX_SIM_TX_LP	0x0400	/**< use low power for simultaneous tx responses */
#define MHF3_SELECT_RXF1	0x0800	/**< enable frame classification in pcie FD */
#define MHF3_BTCX_ECI		0x1000	/**< Enable BTCX ECI interface */
#define MHF3_NOISECAL_ENHANCE   0x2000

/* Flags in M_HOST_FLAGS4 */
#define MHF4_RCMTA_BSSID_EN	0x0002  /**< BTAMP: multiSta BSSIDs matching in RCMTA area */
#define MHF4_SC_MIX_EN		0x0002  /**< set to enable 4389a0 specific changes */
#define	MHF4_BCN_ROT_RR		0x0004	/**< MBSSID: beacon rotate in round-robin fashion */
#define	MHF4_OPT_SLEEP		0x0008	/**< enable opportunistic sleep (REV<80) */
#define MHF4_PM_OFFLOAD		0x0008	/**< enable PM offload */
#define	MHF4_PROXY_STA		0x0010	/**< enable proxy-STA feature */
#define MHF4_AGING		0x0020	/**< Enable aging threshold for RF awareness */
#define MHF4_STOP_BA_ON_NDP	0x0080	/**< Stop BlockAck to AP to get chance to send NULL data */
#define MHF4_NOPHYHANGWAR	0x0100  /**< disable ucode WAR for idletssi cal (rev=61) */
#define MHF4_WMAC_ACKTMOUT	0x0200	/**< reserved for WMAC testing */
#define MHF4_NAPPING_ENABLE	0x0400	/**< Napping enable (REV<80) */
#define MHF4_IBSS_SEC		0x0800	/**< IBSS WPA2-PSK operating mode */
#define MHF4_SISO_BCMC_RX	0x1000	/* Disable switch to MIMO on recving multicast TIM */
#define MHF4_RSDB_CR1_MINIPMU_CAL_EN	0x8000		/* for 4349B0. JIRA:SW4349-1469 */

/* Flags in M_HOST_FLAGS5 */
#define MHF5_BTCX_LIGHT         0x0002	/**< light coex mode, off txpu only for critical BT */
#define MHF5_BTCX_PARALLEL      0x0004	/**< BT and WLAN run in parallel. */
#define MHF5_BTCX_DEFANT        0x0008	/**< default position for shared antenna */
#define MHF5_P2P_MODE		0x0010	/**< Enable P2P mode */
#define MHF5_LEGACY_PRS		0x0020	/**< Enable legacy probe resp support */
#define MHF5_HWRSSI_EN		0x0800	/**< Enable HW RSSI (ac) */
#define MHF5_HIBERNATE		0x1000	/**< Force ucode to power save until wake-bit */
#define MHF5_BTCX_GPIO_DEBUG	0x4000	/**< Enable gpio pins for btcoex ECI signals */
#define MHF5_SUPPRESS_PRB_REQ	0x8000	/**< Suppress probe requests at ucode level */

/* Flags in M_HOST_FLAGS6 */
#define MHF6_TXPWRCAP_RST_EN    0x0001 /** < Ucode clear phyreg after each tx */
#define MHF6_TXPWRCAP_EN        0x0002 /** < Enable TX power capping in ucode */
#define MHF6_TSYNC_AVB          0x0004  /** Enable AVB for timestamping */
#define MHF6_TSYNC_3PKG		0x0020 /** < Enable 3rd txstatus package */
#define MHF6_TDMTX		0x0040 /** < Enable SDB TDM in ucode */
#define MHF6_TSYNC_NODEEPSLP	0x0080 /** < Disable deep sleep to keep AVB clock */
#define MHF6_TSYNC_CAL          0x0100 /** < Enable Tsync cal in ucode */
#define MHF6_TXPWRCAP_IOS_NBIT  0x0200 /** < Enable IOS mode of operation for Txpwrcap (REV>=80) */
#define MHF6_MULBSSID_NBIT      0x0400 /** < associated to AP belonging to a multiple BSSID set */
#define MHF6_HEBCN_TX_NBIT      0x0800 /** < HE BCN-TX */
#define MHF6_LATENCY_EN		0x2000 /** < Enable Latency instrumentation in ucode */
#define MHF6_PTMSTS_EN          0x4000 /** < Enable PTM Status */

/* MX_HOST_FLAGS */
/* Flags for MX_HOST_FLAGS0 */
#define MXHF0_RSV0		0x0001		/* ucode internal, not exposed yet */
#define MXHF0_TXDRATE		0x0002		/* mu txrate to use rate from txd */
#define MXHF0_CHKFID		0x0004		/* check if frameid->fifo matches hw txfifo idx */
#define MXHF0_DISWAR		0x0008		/* disable some WAR. */

/* M_AXC_HOST_FLAGS0 */
#define MAXCHF0_WAIT_TRIG	0x0001		/* Hold frames till trigger frame is rxed */
#define MAXCHF0_HTC_SUPPORT	0x0002		/* 11AX HTC field support */
#define MAXCHF0_AX_ASSOC_SHIFT	0x0003		/* 11AX association indicator */
#define MAXCHF0_HEB_CONFIG	0x0004		/* HEB configuration */
#define MAXCHF0_ACI_DET		0x0008		/* ACI detect soft enable */
#define MAXCHF0_TRIGRES_LP	0x0010		/* Lite-Point testing */
#define MAXCHF0_HDRCONV_SHIFT	5u		/* Enable header conversion */
#define MAXCHF0_HDRCONV		(1 << MAXCHF0_HDRCONV_SHIFT)
#define MAXCHF0_FORCE_ZERO_PPR_SHIFT	6u	/* Force PPR value to 0 for ULTPC */
#define MAXCHF0_FORCE_ZERO_PPR	(1 << MAXCHF0_FORCE_ZERO_PPR_SHIFT)
#define MAXCHF0_DISABLE_PYLDECWAR_SHIFT	7u	/* Disable WAR for Paydecode issue */
#define MAXCHF0_DISABLE_PYLDECWAR (1 << MAXCHF0_DISABLE_PYLDECWAR_SHIFT)
#define MAXCHF0_BSR_SUPPORT_SHIFT	8u	/* BSR is supported */
#define MAXCHF0_BSR_SUPPORT (1 << MAXCHF0_BSR_SUPPORT_SHIFT)
#define MAXCHF0_MUEDCA_VALID_SHIFT	9u	/* MUEDCA information is valid */
#define MAXCHF0_MUEDCA_VALID (1 << MAXCHF0_MUEDCA_VALID_SHIFT)
/* Bit 10 definition missing? */
#define MAXCHF0_TWT_PKTSUPP_SHIFT	11u	/* Enable pkt suppress outside TWT SP */
#define MAXCHF0_TWT_PKTSUPP_EN	(1 << MAXCHF0_TWT_PKTSUPP_SHIFT)
#define MAXCHF0_TBPPDU_STATUS_SHIFT	12u
#define MAXCHF0_TBPPDU_STATUS_EN	(1 << MAXCHF0_TBPPDU_STATUS_SHIFT)
#define MAXCHF0_11AX_TXSTATUS_EXT_SHIFT 13u     /* Enable 128 BA pkg in TX status */
#define MAXCHF0_11AX_TXSTATUS_EXT_EN    (1u << MAXCHF0_11AX_TXSTATUS_EXT_SHIFT)
#define MAXCHF1_11AX_TXSTATUS_EXT_SHIFT 0u  /* Enable 256 BA pkg in TX status */
#define MAXCHF1_11AX_TXSTATUS_EXT_EN    (1u << MAXCHF1_11AX_TXSTATUS_EXT_SHIFT)
/* Bit 14 for UORA_EN */
#define MAXCHF0_11AX_UORA_SHIFT		14u	/* Enable UORA support */
#define MAXCHF0_11AX_UORA_EN		(1u << MAXCHF0_11AX_UORA_SHIFT)

/* M_AXC_HOST_FLAGS1 */
#define MAXCHF1_ITXSTATUS_EN		0x0004u          /* Enable intermediate txs for TB PPDU */
#define MAXCHF1_OBSSHWSTATS_EN		0x0008u	/* Enable ucode OBSS stats monitoring */

/* M_SC_HOST_FLAGS */
#define C_SCCX_STATS_EN			0x0001u		/* Enable SC stats */
#define C_SC_BTMC_COEX_EN		0x0002u		/* Enable WLSC-BTMC coex */

/** Short version of receive frame status. Only used for non-last MSDU of AMSDU - rev61.1 */
typedef struct d11rxhdrshort_rev61_1 d11rxhdrshort_rev61_1_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrshort_rev61_1 {
	uint16 RxFrameSize;	/**< Actual byte length of the frame data received */

	/* These two 8-bit fields remain in the same order regardless of
	 * processor byte order.
	 */
	uint8 dma_flags;    /**< bit 0 indicates short or long rx status. 1 == short. */
	uint8 fifo;         /**< rx fifo number */
	uint16 mrxs;        /**< MAC Rx Status */
	uint16 RxFrameSize0;	/**< rxframesize for fifo-0 (in bytes). */
	uint16 HdrConvSt;   /**< hdr conversion status. Copy of ihr(RCV_HDR_CTLSTS). */
	uint16 RxTSFTimeL;  /**< RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY */
	uint16 RxTSFTimeH;  /**< RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY */
	uint16 aux_status;  /**< DMA writes into this field. ucode treats as reserved. */
} BWL_POST_PACKED_STRUCT;

/** Short version of receive frame status. Only used for non-last MSDU of AMSDU - pre80 */
typedef struct d11rxhdrshort_lt80 d11rxhdrshort_lt80_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrshort_lt80 {
	uint16 RxFrameSize;	/**< Actual byte length of the frame data received */

	/* These two 8-bit fields remain in the same order regardless of
	 * processor byte order.
	 */
	uint8 dma_flags;    /**< bit 0 indicates short or long rx status. 1 == short. */
	uint8 fifo;         /**< rx fifo number */
	uint16 mrxs;        /**< MAC Rx Status */
	uint16 RxTSFTime;   /**< RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY */
	uint16 HdrConvSt;   /**< hdr conversion status. Copy of ihr(RCV_HDR_CTLSTS). */
	uint16 aux_status;  /**< DMA writes into this field. ucode treats as reserved. */
} BWL_POST_PACKED_STRUCT;

/* Errflag bits for ge80 */
#define ERRFLAGS_ERR_STATE           0x0003u
#define ERRFLAGS_GREATER_MSDU_LEN    0x0001u
#define ERRFLAGS_AMSDU_TRUNCATED     0x0002u
#define ERRFLAGS_HDRCONV_MASK        0x00F0u
#define ERRFLAGS_HDRCONV_SHIFT            4u
#define ERRFLAGS_CSI_LEN_64K         0x0100u
#define ERRFLAGS_MESH_FMT_ERR        0x0200u

/* Register 'D11_RXE_ERRVAL' bits for ge80 */
#define RXEERR_GREATER_MSDU_LEN     (1u << 6)

/* 128 BA configuration */
/* Register D11_TXBA_DataSel bits for ge80 */
#define TXBA_DATASEL_WSIZE_BITMAP_LEN_ENC_SEL   (1u << 0u)

/* Register D11_TXBA_Data bits (ge80) */
#define TXBA_DATA_WSIZE_256             (0x100u)
#define TXBA_DATA_WSIZE_128             (0x80u)
#define TXBA_DATA_WSIZE_64              (0x40u)

/* HW optimisation to generate bitmap based on start SSN & max SSN */
#define TXBA_DATA_HW_CONST              (0xfu << 12)

/* Register D11_RXE_BA_LEN bits (ge80) */
#define RXE_BA_LEN_RXBA_64              (0x0u)
#define RXE_BA_LEN_RXBA_128             (0x1u)
#define RXE_BA_LEN_RXBA_256             (0x2u)
#define RXE_BA_LEN_TID0_SHIFT           (0u)
#define RXE_BA_LEN_TID1_SHIFT           (2u)
#define RXE_BA_LEN_TID2_SHIFT           (4u)
#define RXE_BA_LEN_TID3_SHIFT           (6u)
#define RXE_BA_LEN_TID4_SHIFT           (8u)
#define RXE_BA_LEN_TID5_SHIFT           (10u)
#define RXE_BA_LEN_TID6_SHIFT           (12u)
#define RXE_BA_LEN_TID7_SHIFT           (14u)

/* Register D11_RXE_BA_LEN_ENC bits (ge80) */
#define RXE_BA_LEN_ENC_BA32_VAL         (0x3u << 0u)
#define RXE_BA_LEN_ENC_BA64_VAL         (0x0u << 2u)
#define RXE_BA_LEN_ENC_BA128_VAL        (0x1u << 4u)
#define RXE_BA_LEN_ENC_BA256_VAL        (0x2u << 6u)

/* Register D11_RXE_TXBA_CTL2 (ge80) */
#define RXE_TXBA_CTL2_CONIG_SINGLE_TID  (0x0u << 0u)
#define RXE_TXBA_CTL2_CONIG_ALL_TID     (0x1u << 0u)
#define RXE_TXBA_CTL2_SEL_TID0          (0x0u << 12u)
#define RXE_TXBA_CTL2_SEL_TID1          (0x1u << 12u)
#define RXE_TXBA_CTL2_SEL_TID2          (0x2u << 12u)
#define RXE_TXBA_CTL2_SEL_TID3          (0x3u << 12u)
#define RXE_TXBA_CTL2_SEL_TID4          (0x4u << 12u)
#define RXE_TXBA_CTL2_SEL_TID5          (0x5u << 12u)
#define RXE_TXBA_CTL2_SEL_TID6          (0x6u << 12u)
#define RXE_TXBA_CTL2_SEL_TID7          (0x7u << 12u)

/**
 * Special Notes
 * #1: dma_flags, fifo
 * These two 8-bit fields remain in the same order regardless of
 * processor byte order.
 * #2: pktclass
 * 16 bit bitmap is a result of Packet (or Flow ) Classification.
 *
 *	0	:	Flow ID Different
 *	1,2,3	:	A1, A2, A3 Different
 *	4	:	TID Different
 *	5, 6	:	DA, SA from AMSDU SubFrame Different
 *	7	:	FC Different
 *	8	:	AMPDU boundary
 *	9 - 15	:	Reserved
 * #3: errflags
 * These bits indicate specific errors detected by the HW on the Rx Path.
 * However, these will be relevant for Last MSDU Status only.
 *
 * Whenever there is an error at any MSDU, HW treats it as last
 * MSDU and send out last MSDU status.
 */

#define D11RXHDR_HW_STATUS_GE80 \
	uint16 RxFrameSize;	/**< Actual byte length of the frame data received */ \
	/* For comments see special note #1 above */\
	uint8 dma_flags;	/**< bit 0 indicates short or long rx status. 1 == short. */ \
	uint8 fifo;		/**< rx fifo number */ \
	\
	uint16 mrxs;		/**< MAC Rx Status */ \
	uint16 RxFrameSize0;	/**< rxframesize for fifo-0 (in bytes). */ \
	uint16 HdrConvSt;	/**< hdr conversion status. Copy of ihr(RCV_HDR_CTLSTS). */ \
	uint16 pktclass; \
	uint32 filtermap;	/**< 32 bit bitmap indicates which "Filters" have matched. */ \
	/* For comments see special note #2 above */ \
	uint16 flowid;		/**< result of Flow ID Look Up performed by the HW. */ \
	/* For comments see special note #3 above */\
	uint16 errflags;

#define D11RXHDR_UCODE_STATUS_GE80 \
	/**< Ucode Generated Status (16 Bytes) */ \
	uint16 RxStatus1;		/**< MAC Rx Status */ \
	uint16 RxStatus2;		/**< extended MAC Rx status */ \
	uint16 RxChan;			/**< Rx channel info or chanspec */ \
	uint16 AvbRxTimeL;		/**< AVB RX timestamp low16 */ \
	uint16 AvbRxTimeH;		/**< AVB RX timestamp high16 */ \
	uint16 RxTSFTime;		/**< Lower 16 bits of Rx timestamp */ \
	uint16 RxTsfTimeH;		/**< Higher 16 bits of Rx timestamp */ \
	uint16 MuRate;			/**< MU rate info (bit3:0 MCS, bit6:4 NSTS) */

#define D11RXHDR_HW_STATUS_GE87_1       /**< HW Generated 24 bytes RX Status           */ \
	D11RXHDR_HW_STATUS_GE80		/**< First 20 bytes are same as mac rev >= 80  */ \
	uint16 roe_hw_sts;		/**< ROE HW status                             */ \
	uint16 roe_err_flags;		/**< ROE error flags                           */

#define D11RXHDR_UCODE_STATUS_GE87_1    /**< Ucode Generated Status (22 Bytes)         */ \
	uint16 RxStatus1;		/**< MAC Rx Status                             */ \
	uint16 RxStatus2;		/**< extended MAC Rx status                    */ \
	uint16 RxChan;			/**< Rx channel info or chanspec               */ \
	uint16 MuRate;			/**< MU rate info (bit3:0 MCS, bit6:4 NSTS)    */ \
	uint32 AVBRxTime;		/**< 32 bit AVB timestamp                      */ \
	uint32 TSFRxTime;		/**< 32 bit TSF timestamp                      */ \
	uint64 PTMRxTime;		/**< 64 bit PTM timestamp                      */

	/**< HW Generated Status (20 Bytes) */
/** Short version of receive frame status. Only used for non-last MSDU of AMSDU - rev80 */
typedef struct d11rxhdrshort_ge87_1 d11rxhdrshort_ge87_1_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrshort_ge87_1 {

	D11RXHDR_HW_STATUS_GE87_1

} BWL_POST_PACKED_STRUCT;

/** Mid version of receive frame status. Only used for MPDU of AMPDU - rev80 */
typedef struct d11rxhdrmid_ge87_1 d11rxhdrmid_ge87_1_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrmid_ge87_1 {

	D11RXHDR_HW_STATUS_GE87_1
	D11RXHDR_UCODE_STATUS_GE87_1
} BWL_POST_PACKED_STRUCT;

/** Short version of receive frame status. Only used for non-last MSDU of AMSDU - rev80 */
typedef struct d11rxhdrshort_ge80 d11rxhdrshort_ge80_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrshort_ge80 {

	D11RXHDR_HW_STATUS_GE80

} BWL_POST_PACKED_STRUCT;

/** Mid version of receive frame status. Only used for MPDU of AMPDU - rev80 */
typedef struct d11rxhdrmid_ge80 d11rxhdrmid_ge80_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdrmid_ge80 {

	D11RXHDR_HW_STATUS_GE80
	D11RXHDR_UCODE_STATUS_GE80

} BWL_POST_PACKED_STRUCT;

/** Receive Frame Data Header - pre80 */
typedef struct d11rxhdr_lt80 d11rxhdr_lt80_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdr_lt80 {
	uint16 RxFrameSize;	/**< Actual byte length of the frame data received */

	/**
	 * These two 8-bit fields remain in the same order regardless of
	 * processor byte order.
	 */
	uint8 dma_flags;    /* bit 0 indicates short or long rx status. 1 == short. */
	uint8 fifo;         /* rx fifo number */

	uint16 PhyRxStatus_0;	/**< PhyRxStatus 15:0 */
	uint16 PhyRxStatus_1;	/**< PhyRxStatus 31:16 */
	uint16 PhyRxStatus_2;	/**< PhyRxStatus 47:32 */
	uint16 PhyRxStatus_3;	/**< PhyRxStatus 63:48 */
	uint16 PhyRxStatus_4;	/**< PhyRxStatus 79:64 */
	uint16 PhyRxStatus_5;	/**< PhyRxStatus 95:80 */
	uint16 RxStatus1;	/**< MAC Rx Status */
	uint16 RxStatus2;	/**< extended MAC Rx status */

	/**
	 * - RxTSFTime time of first MAC symbol + M_PHY_PLCPRX_DLY
	 */
	uint16 RxTSFTime;

	uint16 RxChan;		/**< Rx channel info or chanspec */
	uint16 RxFrameSize0;	/**< size of rx-frame in fifo-0 in case frame is copied to fifo-1 */
	uint16 HdrConvSt;	/**< hdr conversion status. Copy of ihr(RCV_HDR_CTLSTS). */
	uint16 AvbRxTimeL;	/**< AVB RX timestamp low16 */
	uint16 AvbRxTimeH;	/**< AVB RX timestamp high16 */
	uint16 MuRate;		/**< MU rate info (bit3:0 MCS, bit6:4 NSTS) */
	/**
	 * These bits indicate specific errors detected by the HW on the Rx Path.
	 * However, these will be relevant for Last MSDU Status only.
	 *
	 * Whenever there is an error at any MSDU, HW treats it as last
	 * MSDU and send out last MSDU status.
	 */
	uint16 errflags;
} BWL_POST_PACKED_STRUCT;

#define N_PRXS_GE80	16		/* Total number of PhyRx status words for corerev >= 80 */
#define N_PRXS_LT80	6		/* Total number of PhyRx status words for corerev < 80 */

/* number of PhyRx status words newly added for (corerev >= 80) */
#define N_PRXS_REM_GE80	(N_PRXS_GE80 - N_PRXS_LT80)

/** RX Hdr definition - rev80 */
typedef struct d11rxhdr_ge80 d11rxhdr_ge80_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdr_ge80 {
	/**
	 * Even though rxhdr can be in short or long format, always declare it here
	 * to be in long format. So the offsets for the other fields are always the same.
	 */

	/**< HW Generated Status (20 Bytes) */
	D11RXHDR_HW_STATUS_GE80
	D11RXHDR_UCODE_STATUS_GE80

	/**< PHY Generated Status (32 Bytes) */
	uint16 PhyRxStatus_0;		/**< PhyRxStatus 15:0 */
	uint16 PhyRxStatus_1;		/**< PhyRxStatus 31:16 */
	uint16 PhyRxStatus_2;		/**< PhyRxStatus 47:32 */
	uint16 PhyRxStatus_3;		/**< PhyRxStatus 63:48 */
	uint16 PhyRxStatus_4;		/**< PhyRxStatus 79:64 */
	uint16 PhyRxStatus_5;		/**< PhyRxStatus 95:80 */
	uint16 phyrxs_rem[N_PRXS_REM_GE80];	/**< 20 bytes of remaining prxs (corerev >= 80) */
	/* Currently only 6 words are being pushed out of uCode: 6, 9, 16, 17, 21, 23 */
} BWL_POST_PACKED_STRUCT;

#define N_PRXS_GE85	32u	// total number of PhyRxStatus BYTEs for rev >= 85

typedef struct d11rxhdr_ge87_1 d11rxhdr_ge87_1_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdr_ge87_1 {
	/**
	 * Even though rxhdr can be in short or long format, always declare it here
	 * to be in long format. So the offsets for the other fields are always the same.
	 */

	D11RXHDR_HW_STATUS_GE87_1       /**< HW Generated Status (24 Bytes)    */
	D11RXHDR_UCODE_STATUS_GE87_1    /**< uCode Generated Status (24 Bytes) */
	uint8 PHYRXSTATUS[N_PRXS_GE85]; /**< PHY Generated Status (32 Bytes)   */
} BWL_POST_PACKED_STRUCT;

/* A wrapper structure for all versions of d11rxh short structures */
typedef struct d11rxhdr_ge85 d11rxhdr_ge85_t;
BWL_PRE_PACKED_STRUCT struct d11rxhdr_ge85 {
	/**
	 * Even though rxhdr can be in short or long format, always declare it here
	 * to be in long format. So the offsets for the other fields are always the same.
	 */

	/**< HW Generated Status (20 Bytes) */
	D11RXHDR_HW_STATUS_GE80
	D11RXHDR_UCODE_STATUS_GE80

	/**< PHY Generated Status (32 Bytes) */
	uint8 PHYRXSTATUS[N_PRXS_GE85];
} BWL_POST_PACKED_STRUCT;

/* A wrapper structure for all versions of d11rxh short structures */
typedef union d11rxhdrshort {
	d11rxhdrshort_rev61_1_t rev61_1;
	d11rxhdrshort_lt80_t lt80;
	d11rxhdrshort_ge80_t ge80;
	d11rxhdrshort_ge87_1_t ge87_1;
} d11rxhdrshort_t;

/* A wrapper structure for all versions of d11rxh mid structures */
typedef union d11rxhdrmid {
	d11rxhdrmid_ge80_t ge80;
	d11rxhdrmid_ge87_1_t ge87_1;
} d11rxhdrmid_t;

/* A wrapper structure for all versions of d11rxh structures */
typedef union d11rxhdr {
	d11rxhdr_lt80_t lt80;
	d11rxhdr_ge80_t ge80;
	d11rxhdr_ge85_t ge85;
	d11rxhdr_ge87_1_t ge87_1;
} d11rxhdr_t;

#define D11RXHDRSHORT_GE87_1_ACCESS_REF(srxh, member) \
	(&((((d11rxhdrshort_t *)(srxh))->ge87_1).member))

#define D11RXHDRMID_GE87_1_ACCESS_REF(mrxh, member) \
	(&((((d11rxhdrmid_t *)(mrxh))->ge87_1).member))

#define D11RXHDRSHORT_GE87_1_ACCESS_VAL(srxh, member) \
	((((d11rxhdrshort_t *)(srxh))->ge87_1).member)

#define D11RXHDRMID_GE87_1_ACCESS_VAL(mrxh, member) \
	((((d11rxhdrmid_t *)(mrxh))->ge87_1).member)

#define D11RXHDR_GE87_1_ACCESS_REF(rxh, member) \
	(&((rxh)->ge87_1).member)

#define D11RXHDR_GE87_1_ACCESS_VAL(rxh, member) \
	(((rxh)->ge87_1).member)

#define D11RXHDR_GE87_1_SET_VAL(rxh, member, value) \
	(((rxh)->ge87_1).member = value)

#define D11RXHDRSHORT_GE80_ACCESS_REF(srxh, member) \
	(&((((d11rxhdrshort_t *)(srxh))->ge80).member))

#define D11RXHDRMID_GE80_ACCESS_REF(mrxh, member) \
	(&((((d11rxhdrmid_t *)(mrxh))->ge80).member))

#define D11RXHDRSHORT_LT80_ACCESS_REF(srxh, member) \
	(&((((d11rxhdrshort_t *)(srxh))->lt80).member))

#define D11RXHDRSHORT_GE80_ACCESS_VAL(srxh, member) \
	((((d11rxhdrshort_t *)(srxh))->ge80).member)

#define D11RXHDRMID_GE80_ACCESS_VAL(mrxh, member) \
	((((d11rxhdrmid_t *)(mrxh))->ge80).member)

#define D11RXHDRSHORT_LT80_ACCESS_VAL(srxh, member) \
	((((d11rxhdrshort_t *)(srxh))->lt80).member)

#define D11RXHDR_GE80_ACCESS_REF(rxh, member) \
	(&((rxh)->ge80).member)

#define D11RXHDR_LT80_ACCESS_REF(rxh, member) \
	(&((rxh)->lt80).member)

#define D11RXHDR_GE80_ACCESS_VAL(rxh, member) \
	(((rxh)->ge80).member)

#define D11RXHDR_GE80_SET_VAL(rxh, member, value) \
	(((rxh)->ge80).member = value)

#define D11RXHDR_LT80_ACCESS_VAL(rxh, member) \
	(((rxh)->lt80).member)

#define D11RXHDR_LT80_SET_VAL(rxh, member, value) \
	(((rxh)->lt80).member = value)

/** For accessing members of d11rxhdrshort_t by reference (address of members) */
#define D11RXHDRSHORT_ACCESS_REF(srxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDRSHORT_GE87_1_ACCESS_REF(srxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDRSHORT_GE80_ACCESS_REF(srxh, member) : \
	D11RXHDRSHORT_LT80_ACCESS_REF(srxh, member))

/** For accessing members of d11rxhdrshort_t by value (only value stored inside members accessed) */
#define D11RXHDRSHORT_ACCESS_VAL(srxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDRSHORT_GE87_1_ACCESS_VAL(srxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDRSHORT_GE80_ACCESS_VAL(srxh, member) : \
	D11RXHDRSHORT_LT80_ACCESS_VAL(srxh, member))

/** For accessing members of d11rxhdrmid_t by reference (address of members) */
#define D11RXHDRMID_ACCESS_REF(mrxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDRMID_GE87_1_ACCESS_REF(mrxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDRMID_GE80_ACCESS_REF(mrxh, member) : NULL)

/** For accessing members of d11rxhdrmid_t by value (only value stored inside members accessed) */
#define D11RXHDRMID_ACCESS_VAL(mrxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDRMID_GE87_1_ACCESS_VAL(mrxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDRMID_GE80_ACCESS_VAL(mrxh, member) : NULL)

/** For accessing members of d11rxhdr_t by reference (address of members) */
#define D11RXHDR_ACCESS_REF(rxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_REF(rxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDR_GE80_ACCESS_REF(rxh, member) : \
	D11RXHDR_LT80_ACCESS_REF(rxh, member))

/** For accessing members of d11rxhdr_t by value (only value stored inside members accessed) */
#define D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, member) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, member) : \
	D11REV_GE(corerev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, member) : \
	D11RXHDR_LT80_ACCESS_VAL(rxh, member))

/** For accessing members of d11rxhdr_t by value (only value stored inside members accessed) */
#define D11RXHDR_SET_VAL(rxh, corerev, corerev_minor, member, value) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDR_GE87_1_SET_VAL(rxh, member, value) : \
	D11REV_GE(corerev, 80) ? D11RXHDR_GE80_SET_VAL(rxh, member, value) : \
	D11RXHDR_LT80_SET_VAL(rxh, member, value))

#define D11RXHDR_PTM(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDR_GE87_1_ACCESS_VAL(rxh, PTMRxTime) : 0)

#define D11RXHDR_AVB(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		(uint32)D11RXHDR_GE87_1_ACCESS_VAL(rxh, AVBRxTime) : \
	D11REV_GE(corerev, 80) ? ((uint32)D11RXHDR_GE80_ACCESS_VAL(rxh, AvbRxTimeL) | \
		((uint32)D11RXHDR_GE80_ACCESS_VAL(rxh, AvbRxTimeH) << 16u)) : \
		((uint32)D11RXHDR_LT80_ACCESS_VAL(rxh, AvbRxTimeL) | \
		((uint32)D11RXHDR_LT80_ACCESS_VAL(rxh, AvbRxTimeH) << 16u)))

#define D11RXHDR_TSF_REF(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDR_GE87_1_ACCESS_REF(rxh, TSFRxTime) : \
	D11REV_GE(corerev, 80) ? (uint32*)D11RXHDR_GE80_ACCESS_REF(rxh, RxTSFTime) : \
		(uint32*)D11RXHDR_LT80_ACCESS_REF(rxh, RxTSFTime))

#define D11RXHDR_TSF(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
		D11RXHDR_GE87_1_ACCESS_VAL(rxh, TSFRxTime) : \
	D11REV_GE(corerev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, RxTSFTime) : \
		D11RXHDR_LT80_ACCESS_VAL(rxh, RxTSFTime))

#define RXS_SHORT_ENAB(rev)	(D11REV_GE(rev, 64) || \
				D11REV_IS(rev, 60) || \
				D11REV_IS(rev, 62))

#define RXS_MID_ENAB(rev)	(D11REV_GE(rev, 80))
#define RXS_LONG_ENAB(rev)	(D11REV_GE(rev, 80))

#define IS_D11RXHDRSHORT(rxh, rev, rev_min) ((RXS_SHORT_ENAB(rev) && \
	((D11RXHDR_ACCESS_VAL((rxh), (rev), (rev_min), dma_flags)) & RXS_SHORT_MASK)) != 0)

#define IS_D11RXHDRMID(rxh, rev, rev_min) ((RXS_MID_ENAB(rev) && \
	((D11RXHDR_ACCESS_VAL((rxh), (rev), (rev_min), dma_flags)) == 0)))

#define IS_D11RXHDRLONG(rxh, rev, rev_min) \
		((!(IS_D11RXHDRSHORT((rxh), (rev), (rev_min)))) && \
			(!(IS_D11RXHDRMID((rxh), (rev), (rev_min)))))

#define D11RXHDR_HAS_UCODE_STATUS(rxhdr, corerev, corerev_minor) \
		((!IS_D11RXHDRSHORT((rxhdr), (corerev), (corerev_minor))) || \
			(IS_D11RXHDRMID((rxhdr), (corerev), (corerev_minor))))

#define IS_PHYRXHDR_VALID(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	(D11RXHDR_GE87_1_ACCESS_VAL(rxh, dma_flags) == RXS_PHYRXST_VALID_REV_GE80) : \
	D11REV_GE(corerev, 80) ? \
	(D11RXHDR_GE80_ACCESS_VAL(rxh, dma_flags) == RXS_PHYRXST_VALID_REV_GE80) : \
	(D11RXHDR_LT80_ACCESS_VAL(rxh, RxStatus2) & RXS_PHYRXST_VALID))

#define RXHDR_GET_PAD_LEN(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	((((D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_VAL(rxh, mrxs)) & RXSS_PBPRES) != 0) ? HDRCONV_PAD : 0) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	(((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, mrxs) & \
	RXSS_PBPRES) != 0) ? HDRCONV_PAD : 0) : \
	(((D11RXHDR_LT80_ACCESS_VAL(rxh, RxStatus1) & RXS_PBPRES) != 0) ? HDRCONV_PAD : 0)))

#define RXHDR_GET_PAD_PRES(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	(((D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_VAL(rxh, mrxs)) & RXSS_PBPRES) != 0) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, mrxs) & \
	RXSS_PBPRES) != 0) : \
	(((D11RXHDR_LT80_ACCESS_VAL(rxh, RxStatus1) & RXS_PBPRES) != 0))))

#define RXHDR_GET_CONV_TYPE(rxh, corerev, corerev_minor) \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, \
	HdrConvSt) & HDRCONV_ETH_FRAME) != 0) : ((D11RXHDR_ACCESS_VAL(rxh, \
	corerev, corerev_minor, HdrConvSt) & HDRCONV_ETH_FRAME) != 0))

#define RXHDR_GET_ROE_ERR_STS(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	((D11RXHDR_GE87_1_ACCESS_VAL(rxh, roe_err_flags))) : 0)

#define RXHDR_GET_ROE_L3_TYPE(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	((D11RXHDR_GE87_1_ACCESS_VAL(rxh, roe_hw_sts)) & ROE_L3_PROT_TYPE_MASK) : 0)

#define RXHDR_GET_ROE_L4_TYPE(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	((D11RXHDR_GE87_1_ACCESS_VAL(rxh, roe_hw_sts)) & ROE_L4_PROT_TYPE_MASK) : 0)

#define RXHDR_GET_ROE_L3_STATUS(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	((D11RXHDR_GE87_1_ACCESS_VAL(rxh, roe_hw_sts)) & ROE_L3_CHKSUM_STATUS_MASK) : 0)

#define RXHDR_GET_ROE_L4_STATUS(rxh, corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	((D11RXHDR_GE87_1_ACCESS_VAL(rxh, roe_hw_sts)) & ROE_L4_CHKSUM_STATUS_MASK) : 0)

#define RXHDR_GET_AGG_TYPE(rxh, corerev, corerev_minor) \
	(D11REV_GE(corerev, 80) ? \
	(((D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_VAL(rxh, mrxs)) & RXSS_AGGTYPE_MASK) >> RXSS_AGGTYPE_SHIFT) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, mrxs) \
	 & RXSS_AGGTYPE_MASK) >> RXSS_AGGTYPE_SHIFT) : \
	((D11RXHDR_LT80_ACCESS_VAL(rxh, RxStatus2) & RXS_AGGTYPE_MASK) >> RXS_AGGTYPE_SHIFT)))

#define RXHDR_GET_PBPRS_REF(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_REF(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_REF(rxh, mrxs)) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	((D11RXHDRSHORT_ACCESS_REF(rxh, corerev, corerev_minor, mrxs))) : \
	(D11RXHDR_LT80_ACCESS_REF(rxh, RxStatus1))))

#define RXHDR_GET_IS_DEFRAG(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	(D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, RxStatus1) & RXS_IS_DEFRAG) : 0)

#define SET_RXHDR_PBPRS_REF_VAL(rxh, corerev, corerev_minor, val) \
	(D11REV_GE(corerev, 80) ? \
	(*val |= RXSS_PBPRES) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? (*val |= RXSS_PBPRES) : \
	(*val |= RXS_PBPRES)))

#define CLEAR_RXHDR_PBPRS_REF_VAL(rxh, corerev, corerev_minor, val) \
	(D11REV_GE(corerev, 80) ? \
	(*val &= ~RXSS_PBPRES) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? (*val &= ~RXSS_PBPRES) : \
	(*val &= ~RXS_PBPRES)))

#define RXHDR_GET_AMSDU(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	(((D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_VAL(rxh, mrxs)) & RXSS_AMSDU_MASK) != 0) : \
	(IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, \
	mrxs) & RXSS_AMSDU_MASK) != 0) : \
	((D11RXHDR_LT80_ACCESS_VAL(rxh, RxStatus2) & RXS_AMSDU_MASK) != 0)))

#ifdef BCMDBG
#define RXHDR_GET_MSDU_COUNT(rxh, corerev, corerev_minor) (D11REV_GE(corerev, 80) ? \
	(((D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? \
	D11RXHDR_GE87_1_ACCESS_VAL(rxh, mrxs) : \
	D11RXHDR_GE80_ACCESS_VAL(rxh, mrxs)) & RXSS_MSDU_CNT_MASK) >> RXSS_MSDU_CNT_SHIFT) : \
	IS_D11RXHDRSHORT(rxh, corerev, corerev_minor) ? \
	(((D11RXHDRSHORT_ACCESS_VAL(rxh, corerev, corerev_minor, mrxs)) & \
	RXSS_MSDU_CNT_MASK) >> RXSS_MSDU_CNT_SHIFT) : 0)

#endif /* BCMDBG */

/** Length of HW RX status in RxStatus */
#define HW_RXHDR_LEN_REV_GE87_1	(sizeof(d11rxhdrshort_ge87_1_t))	/* 24 bytes */
#define HW_RXHDR_LEN_REV_GE80	(sizeof(d11rxhdrshort_ge80_t))		/* 20 bytes */
#define HW_RXHDR_LEN_REV_LT80	(sizeof(d11rxhdrshort_lt80_t))		/* 12 bytes */
#define HW_RXHDR_LEN_REV_61_1	(sizeof(d11rxhdrshort_rev61_1_t))	/* 16 bytes */

/** Length of HW RX status + ucode Rx status in RxStatus */
#define MID_RXHDR_LEN_REV_GE87_1 (sizeof(d11rxhdrmid_ge87_1_t))		/* 48 bytes */
#define MID_RXHDR_LEN_REV_GE80   (sizeof(d11rxhdrmid_ge80_t))		/* 36 bytes */

/** Length of HW RX status + ucode RX status + PHY RX status + padding(if need align) */
#define D11_RXHDR_LEN_REV_GE87_1 (sizeof(d11rxhdr_ge87_1_t))		/* 80 bytes */
#define D11_RXHDR_LEN_REV_GE80   (sizeof(d11rxhdr_ge80_t))		/* 68 bytes */
#define D11_RXHDR_LEN_REV_LT80   (sizeof(d11rxhdr_lt80_t))		/* 36 bytes */

#define HW_RXHDR_LEN(corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? HW_RXHDR_LEN_REV_GE87_1 : \
	D11REV_GE(corerev, 80) ? HW_RXHDR_LEN_REV_GE80 : HW_RXHDR_LEN_REV_LT80)

#define MID_RXHDR_LEN(corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? MID_RXHDR_LEN_REV_GE87_1 : \
	D11REV_GE(corerev, 80) ? \
		MID_RXHDR_LEN_REV_GE80 : NULL)

#define D11_RXHDR_LEN(corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? D11_RXHDR_LEN_REV_GE87_1 : \
	D11REV_GE(corerev, 80) ? D11_RXHDR_LEN_REV_GE80 : \
	D11_RXHDR_LEN_REV_LT80)

#define	FRAMELEN(corerev, corerev_minor, rxh) \
	D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, RxFrameSize)

#define RXS_SHORT_MASK  0x01	/**< Short vs full rx status in dma_flags field of d11rxhdr */

/** validate chip specific phychain info for MCSSQ snr.
 *  should sync with uCode reporting.
 *  please add a condition with decending order to avoid any wrong skip
 *  Note: this macro can be removed once NEWT no longer needs 4368a0.
 */
#define IS_MCSSQ_ANT3_VALID_GE80(corerev, corerev_minor)    \
	(D11REV_IS(corerev, 83) && (D11MINORREV_IS(corerev_minor, 1)))

/* Header conversion status register bit fields */
#define HDRCONV_USR_ENAB	0x0001
#define HDRCONV_ENAB		0x0100
#define HDRCONV_ETH_FRAME	0x0200
#define HDRCONV_STATUS_VALID	0x8000

#define ROE_L3_PROT_TYPE_IPV4   (0x10u)
#define ROE_L3_PROT_TYPE_IPV6	(0x20u)
#define ROE_L3_PROT_TYPE_MASK	(0x30u)
#define ROE_L3_PROT_TYPE_SHIFT	(4u)

#define ROE_L4_PROT_TYPE_TCP	(0x40u)
#define ROE_L4_PROT_TYPE_UDP	(0x80u)
#define ROE_L4_PROT_TYPE_MASK	(0xC0u)
#define ROE_L4_PROT_TYPE_SHIFT	(6u)

#define ROE_L3_CHKSUM_STATUS_FAIL	(0x100u)
#define ROE_L3_CHKSUM_STATUS_SUCCESS	(0x200u)
#define ROE_L3_CHKSUM_STATUS_MASK	(0x300u)
#define ROE_L3_CHKSUM_STATUS_SHIFT	(8u)

#define ROE_L4_CHKSUM_STATUS_FAIL	(0x400u)
#define ROE_L4_CHKSUM_STATUS_SUCCESS	(0x800u)
#define ROE_L4_CHKSUM_STATUS_MASK	(0xC00u)
#define ROE_L4_CHKSUM_STATUS_SHIFT	(10u)

/** NOTE: Due to precommit issue, _d11_autophyrxsts_ will be moved
 *        to a separated file when 4387 trunk build is stable
 */
#ifndef _d11_autophyrxsts_
#define _d11_autophyrxsts_

#define APRXS_WD0_L_EN_GE85		1u
#define APRXS_WD0_H_EN_GE85		1u
#define APRXS_WD1_L_EN_GE85		1u
#define APRXS_WD1_H_EN_GE85		1u
#define APRXS_WD2_L_EN_GE85		1u
#define APRXS_WD2_H_EN_GE85		1u
#define APRXS_WD3_L_EN_GE85		1u
#define APRXS_WD3_H_EN_GE85		0u // DO NOT ENABLE WD3_H
#define APRXS_WD4_L_EN_GE85		1u
#define APRXS_WD4_H_EN_GE85		1u
#define APRXS_WD5_L_EN_GE85		1u
#define APRXS_WD5_H_EN_GE85		1u
#define APRXS_WD6_L_EN_GE85		0u
#define APRXS_WD6_H_EN_GE85		0u
#define APRXS_WD7_L_EN_GE85		0u
#define APRXS_WD7_H_EN_GE85		0u
#define APRXS_WD8_L_EN_GE85		0u
#define APRXS_WD8_H_EN_GE85		1u
#define APRXS_WD9_L_EN_GE85		0u
#define APRXS_WD9_H_EN_GE85		0u
#define APRXS_WD10_L_EN_GE85		0u
#define APRXS_WD10_H_EN_GE85		0u
#define APRXS_WD11_L_EN_GE85		0u
#define APRXS_WD11_H_EN_GE85		0u
#define APRXS_WD12_L_EN_GE85		0u
#define APRXS_WD12_H_EN_GE85		0u
#define APRXS_WD13_L_EN_GE85		0u
#define APRXS_WD13_H_EN_GE85		0u
#define APRXS_WD14_L_EN_GE85		0u
#define APRXS_WD14_H_EN_GE85		0u
#define APRXS_WD15_L_EN_GE85		0u
#define APRXS_WD15_H_EN_GE85		0u
#define APRXS_WD16_L_EN_GE85		1u
#define APRXS_WD16_H_EN_GE85		0u
#define APRXS_WD17_L_EN_GE85		0u
#define APRXS_WD17_H_EN_GE85		0u
#define APRXS_WD18_L_EN_GE85		1u
#define APRXS_WD18_H_EN_GE85		0u
#define APRXS_WD19_L_EN_GE85		0u
#define APRXS_WD19_H_EN_GE85		0u
#define APRXS_WD20_L_EN_GE85		1u
#define APRXS_WD20_H_EN_GE85		1u
#define APRXS_WD21_L_EN_GE85		0u
#define APRXS_WD21_H_EN_GE85		1u
#define APRXS_WD22_L_EN_GE85		1u
#define APRXS_WD22_H_EN_GE85		1u
#define APRXS_WD23_L_EN_GE85		1u
#define APRXS_WD23_H_EN_GE85		1u
#define APRXS_WD24_L_EN_GE85		0u
#define APRXS_WD24_H_EN_GE85		0u
#define APRXS_WD25_L_EN_GE85		0u
#define APRXS_WD25_H_EN_GE85		0u

enum {
	APRXS_WD0_L_SHIFT = 0,	// frameType, unsupportedRate, band, lostCRS, shortPreamble
	APRXS_WD0_H_SHIFT,	// PLCPViolation, MFCRSFired, ACCRSFired, MUPPDU, OBSSStat
	APRXS_WD1_L_SHIFT,	// coremask, antcfg,
	APRXS_WD1_H_SHIFT,	// BWclassification
	APRXS_WD2_L_SHIFT,	// RxPwrAnt0
	APRXS_WD2_H_SHIFT,	// RxPwrAnt1
	APRXS_WD3_L_SHIFT,	// RxPwrAnt2
	APRXS_WD3_H_SHIFT,	// RxPwrAnt3, OCL
	APRXS_WD4_L_SHIFT,	// RSSI factional bit
	APRXS_WD4_H_SHIFT,	// AGC type, ACI mitigation state, ClipCount, DynBWInNonHT
	APRXS_WD5_L_SHIFT,	// MCSSQSNRCore0
	APRXS_WD5_H_SHIFT,	// MCSSQSNRCore1
	APRXS_WD6_L_SHIFT,	// MCSSQSNRCore2
	APRXS_WD6_H_SHIFT,	// MCSSQSNRCore3, OCL 1
	APRXS_WD7_L_SHIFT,	// MUIntProcessType,
	APRXS_WD7_H_SHIFT,	// coarse freq_offset, packet abort
	APRXS_WD8_L_SHIFT = 0,	// fine freq offset
	APRXS_WD8_H_SHIFT,	// ChBWInNonHT, MLUsed, SINRBasedACIDet
	APRXS_WD9_L_SHIFT,	// SpatialSQCnt
	APRXS_WD9_H_SHIFT,	// packet gain
	APRXS_WD10_L_SHIFT,	// RxPwrAntExt
	APRXS_WD10_H_SHIFT,	// coarse freq_offset of 2nd 80mhz
	APRXS_WD11_L_SHIFT,	// fine freq_offset of 2nd 80mhz
	APRXS_WD11_H_SHIFT,
	APRXS_WD12_L_SHIFT,
	APRXS_WD12_H_SHIFT,
	APRXS_WD13_L_SHIFT,
	APRXS_WD13_H_SHIFT,
	APRXS_WD14_L_SHIFT,
	APRXS_WD14_H_SHIFT,
	APRXS_WD15_L_SHIFT,
	APRXS_WD15_H_SHIFT,
	APRXS_WD16_L_SHIFT = 0,
	APRXS_WD16_H_SHIFT,
	APRXS_WD17_L_SHIFT,
	APRXS_WD17_H_SHIFT,
	APRXS_WD18_L_SHIFT,
	APRXS_WD18_H_SHIFT,
	APRXS_WD19_L_SHIFT,
	APRXS_WD19_H_SHIFT,
	APRXS_WD20_L_SHIFT,
	APRXS_WD20_H_SHIFT,
	APRXS_WD21_L_SHIFT,
	APRXS_WD21_H_SHIFT,
	APRXS_WD22_L_SHIFT,	// STA ID
	APRXS_WD22_H_SHIFT,	// STA ID, NSTS, TXBF, DCM
	APRXS_WD23_L_SHIFT,
	APRXS_WD23_H_SHIFT,
	APRXS_WD24_L_SHIFT = 0,
	APRXS_WD24_H_SHIFT,
	APRXS_WD25_L_SHIFT,
	APRXS_WD25_H_SHIFT
};

#define APRXS_WD0_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD0_L_EN_GE85 : 0)
#define APRXS_WD0_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD0_H_EN_GE85 : 0)
#define APRXS_WD1_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD1_L_EN_GE85 : 0)
#define APRXS_WD1_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD1_H_EN_GE85 : 0)
#define APRXS_WD2_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD2_L_EN_GE85 : 0)
#define APRXS_WD2_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD2_H_EN_GE85 : 0)
#define APRXS_WD3_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD3_L_EN_GE85 : 0)
#define APRXS_WD3_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD3_H_EN_GE85 : 0)
#define APRXS_WD4_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD4_L_EN_GE85 : 0)
#define APRXS_WD4_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD4_H_EN_GE85 : 0)
#define APRXS_WD5_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD5_L_EN_GE85 : 0)
#define APRXS_WD5_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD5_H_EN_GE85 : 0)
#define APRXS_WD6_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD6_L_EN_GE85 : 0)
#define APRXS_WD6_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD6_H_EN_GE85 : 0)
#define APRXS_WD7_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD7_L_EN_GE85 : 0)
#define APRXS_WD7_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD7_H_EN_GE85 : 0)
#define APRXS_WD8_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD8_L_EN_GE85 : 0)
#define APRXS_WD8_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD8_H_EN_GE85 : 0)
#define APRXS_WD9_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD9_L_EN_GE85 : 0)
#define APRXS_WD9_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD9_H_EN_GE85 : 0)
#define APRXS_WD10_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD10_L_EN_GE85 : 0)
#define APRXS_WD10_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD10_H_EN_GE85 : 0)
#define APRXS_WD11_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD11_L_EN_GE85 : 0)
#define APRXS_WD11_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD11_H_EN_GE85 : 0)
#define APRXS_WD12_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD12_L_EN_GE85 : 0)
#define APRXS_WD12_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD12_H_EN_GE85 : 0)
#define APRXS_WD13_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD13_L_EN_GE85 : 0)
#define APRXS_WD13_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD13_H_EN_GE85 : 0)
#define APRXS_WD14_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD14_L_EN_GE85 : 0)
#define APRXS_WD14_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD14_H_EN_GE85 : 0)
#define APRXS_WD15_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD15_L_EN_GE85 : 0)
#define APRXS_WD15_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD15_H_EN_GE85 : 0)
#define APRXS_WD16_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD16_L_EN_GE85 : 0)
#define APRXS_WD16_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD16_H_EN_GE85 : 0)
#define APRXS_WD17_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD17_L_EN_GE85 : 0)
#define APRXS_WD17_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD17_H_EN_GE85 : 0)
#define APRXS_WD18_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD18_L_EN_GE85 : 0)
#define APRXS_WD18_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD18_H_EN_GE85 : 0)
#define APRXS_WD19_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD19_L_EN_GE85 : 0)
#define APRXS_WD19_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD19_H_EN_GE85 : 0)
#define APRXS_WD20_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD20_L_EN_GE85 : 0)
#define APRXS_WD20_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD20_H_EN_GE85 : 0)
#define APRXS_WD21_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD21_L_EN_GE85 : 0)
#define APRXS_WD21_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD21_H_EN_GE85 : 0)
#define APRXS_WD22_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD22_L_EN_GE85 : 0)
#define APRXS_WD22_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD22_H_EN_GE85 : 0)
#define APRXS_WD23_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD23_L_EN_GE85 : 0)
#define APRXS_WD23_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD23_H_EN_GE85 : 0)
#define APRXS_WD24_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD24_L_EN_GE85 : 0)
#define APRXS_WD24_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD24_H_EN_GE85 : 0)
#define APRXS_WD25_L_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD25_L_EN_GE85 : 0)
#define APRXS_WD25_H_EN(rev)	((D11REV_GE(rev, 85)) ? \
				 APRXS_WD25_H_EN_GE85 : 0)

#define APRXS_BMAP0(rev)	((APRXS_WD0_L_EN(rev) << APRXS_WD0_L_SHIFT) | \
				(APRXS_WD0_H_EN(rev) << APRXS_WD0_H_SHIFT) |\
				(APRXS_WD1_L_EN(rev) << APRXS_WD1_L_SHIFT) |\
				(APRXS_WD1_H_EN(rev) << APRXS_WD1_H_SHIFT) |\
				(APRXS_WD2_L_EN(rev) << APRXS_WD2_L_SHIFT) |\
				(APRXS_WD2_H_EN(rev) << APRXS_WD2_H_SHIFT) |\
				(APRXS_WD3_L_EN(rev) << APRXS_WD3_L_SHIFT) |\
				(APRXS_WD3_H_EN(rev) << APRXS_WD3_H_SHIFT) |\
				(APRXS_WD4_L_EN(rev) << APRXS_WD4_L_SHIFT) |\
				(APRXS_WD4_H_EN(rev) << APRXS_WD4_H_SHIFT) |\
				(APRXS_WD5_L_EN(rev) << APRXS_WD5_L_SHIFT) |\
				(APRXS_WD5_H_EN(rev) << APRXS_WD5_H_SHIFT) |\
				(APRXS_WD6_L_EN(rev) << APRXS_WD6_L_SHIFT) |\
				(APRXS_WD6_H_EN(rev) << APRXS_WD6_H_SHIFT) |\
				(APRXS_WD7_L_EN(rev) << APRXS_WD7_L_SHIFT) |\
				(APRXS_WD7_H_EN(rev) << APRXS_WD7_H_SHIFT))

#define APRXS_BMAP1(rev)	((APRXS_WD8_L_EN(rev) << APRXS_WD8_L_SHIFT) | \
				(APRXS_WD8_H_EN(rev) << APRXS_WD8_H_SHIFT) |\
				(APRXS_WD9_L_EN(rev) << APRXS_WD9_L_SHIFT) |\
				(APRXS_WD9_H_EN(rev) << APRXS_WD9_H_SHIFT) |\
				(APRXS_WD10_L_EN(rev) << APRXS_WD10_L_SHIFT) |\
				(APRXS_WD10_H_EN(rev) << APRXS_WD10_H_SHIFT) |\
				(APRXS_WD11_L_EN(rev) << APRXS_WD11_L_SHIFT) |\
				(APRXS_WD11_H_EN(rev) << APRXS_WD11_H_SHIFT) |\
				(APRXS_WD12_L_EN(rev) << APRXS_WD12_L_SHIFT) |\
				(APRXS_WD12_H_EN(rev) << APRXS_WD12_H_SHIFT) |\
				(APRXS_WD13_L_EN(rev) << APRXS_WD13_L_SHIFT) |\
				(APRXS_WD13_H_EN(rev) << APRXS_WD13_H_SHIFT) |\
				(APRXS_WD14_L_EN(rev) << APRXS_WD14_L_SHIFT) |\
				(APRXS_WD14_H_EN(rev) << APRXS_WD14_H_SHIFT) |\
				(APRXS_WD15_L_EN(rev) << APRXS_WD15_L_SHIFT) |\
				(APRXS_WD15_H_EN(rev) << APRXS_WD15_H_SHIFT))

#define APRXS_BMAP2(rev)	((APRXS_WD16_L_EN(rev) << APRXS_WD16_L_SHIFT) | \
				(APRXS_WD16_H_EN(rev) << APRXS_WD16_H_SHIFT) |\
				(APRXS_WD17_L_EN(rev) << APRXS_WD17_L_SHIFT) |\
				(APRXS_WD17_H_EN(rev) << APRXS_WD17_H_SHIFT) |\
				(APRXS_WD18_L_EN(rev) << APRXS_WD18_L_SHIFT) |\
				(APRXS_WD18_H_EN(rev) << APRXS_WD18_H_SHIFT) |\
				(APRXS_WD19_L_EN(rev) << APRXS_WD19_L_SHIFT) |\
				(APRXS_WD19_H_EN(rev) << APRXS_WD19_H_SHIFT) |\
				(APRXS_WD20_L_EN(rev) << APRXS_WD20_L_SHIFT) |\
				(APRXS_WD20_H_EN(rev) << APRXS_WD20_H_SHIFT) |\
				(APRXS_WD21_L_EN(rev) << APRXS_WD21_L_SHIFT) |\
				(APRXS_WD21_H_EN(rev) << APRXS_WD21_H_SHIFT) |\
				(APRXS_WD22_L_EN(rev) << APRXS_WD22_L_SHIFT) |\
				(APRXS_WD22_H_EN(rev) << APRXS_WD22_H_SHIFT) |\
				(APRXS_WD23_L_EN(rev) << APRXS_WD23_L_SHIFT) |\
				(APRXS_WD23_H_EN(rev) << APRXS_WD23_H_SHIFT))

#define APRXS_BMAP3(rev)	((APRXS_WD24_L_EN(rev) << APRXS_WD24_L_SHIFT) | \
				(APRXS_WD24_H_EN(rev) << APRXS_WD24_H_SHIFT) |\
				(APRXS_WD25_L_EN(rev) << APRXS_WD25_L_SHIFT) |\
				(APRXS_WD25_H_EN(rev) << APRXS_WD25_H_SHIFT))
/* byte position */
#define APRXS_WD0_L_POS(rev)	0u
#define APRXS_WD0_H_POS(rev)	(APRXS_WD0_L_POS(rev) + APRXS_WD0_L_EN(rev))	/*  1 */
#define APRXS_WD1_L_POS(rev)	(APRXS_WD0_H_POS(rev) + APRXS_WD0_H_EN(rev))	/*  2 */
#define APRXS_WD1_H_POS(rev)	(APRXS_WD1_L_POS(rev) + APRXS_WD1_L_EN(rev))	/*  3 */
#define APRXS_WD2_L_POS(rev)	(APRXS_WD1_H_POS(rev) + APRXS_WD1_H_EN(rev))	/*  4 */
#define APRXS_WD2_H_POS(rev)	(APRXS_WD2_L_POS(rev) + APRXS_WD2_L_EN(rev))	/*  5 */
#define APRXS_WD3_L_POS(rev)	(APRXS_WD2_H_POS(rev) + APRXS_WD2_H_EN(rev))	/*  6 */
#define APRXS_WD3_H_POS(rev)	(APRXS_WD3_L_POS(rev) + APRXS_WD3_L_EN(rev))	/*  7 */
#define APRXS_WD4_L_POS(rev)	(APRXS_WD3_H_POS(rev) + APRXS_WD3_H_EN(rev))	/*  7 */
#define APRXS_WD4_H_POS(rev)	(APRXS_WD4_L_POS(rev) + APRXS_WD4_L_EN(rev))	/*  8 */
#define APRXS_WD5_L_POS(rev)	(APRXS_WD4_H_POS(rev) + APRXS_WD4_H_EN(rev))	/*  9 */
#define APRXS_WD5_H_POS(rev)	(APRXS_WD5_L_POS(rev) + APRXS_WD5_L_EN(rev))	/* 10 */
#define APRXS_WD6_L_POS(rev)	(APRXS_WD5_H_POS(rev) + APRXS_WD5_H_EN(rev))	/* 11 */
#define APRXS_WD6_H_POS(rev)	(APRXS_WD6_L_POS(rev) + APRXS_WD6_L_EN(rev))	/* 11 */
#define APRXS_WD7_L_POS(rev)	(APRXS_WD6_H_POS(rev) + APRXS_WD6_H_EN(rev))	/* 11 */
#define APRXS_WD7_H_POS(rev)	(APRXS_WD7_L_POS(rev) + APRXS_WD7_L_EN(rev))	/* 11 */
#define APRXS_WD8_L_POS(rev)	(APRXS_WD7_H_POS(rev) + APRXS_WD7_H_EN(rev))	/* 11 */
#define APRXS_WD8_H_POS(rev)	(APRXS_WD8_L_POS(rev) + APRXS_WD8_L_EN(rev))	/* 11 */
#define APRXS_WD9_L_POS(rev)	(APRXS_WD8_H_POS(rev) + APRXS_WD8_H_EN(rev))	/* 12 */
#define APRXS_WD9_H_POS(rev)	(APRXS_WD9_L_POS(rev) + APRXS_WD9_L_EN(rev))	/* 12 */
#define APRXS_WD10_L_POS(rev)	(APRXS_WD9_H_POS(rev) + APRXS_WD9_H_EN(rev))	/* 12 */
#define APRXS_WD10_H_POS(rev)	(APRXS_WD10_L_POS(rev) + APRXS_WD10_L_EN(rev))	/* 12 */
#define APRXS_WD11_L_POS(rev)	(APRXS_WD10_H_POS(rev) + APRXS_WD10_H_EN(rev))	/* 12 */
#define APRXS_WD11_H_POS(rev)	(APRXS_WD11_L_POS(rev) + APRXS_WD11_L_EN(rev))	/* 12 */
#define APRXS_WD12_L_POS(rev)	(APRXS_WD11_H_POS(rev) + APRXS_WD11_H_EN(rev))	/* 12 */
#define APRXS_WD12_H_POS(rev)	(APRXS_WD12_L_POS(rev) + APRXS_WD12_L_EN(rev))	/* 12 */
#define APRXS_WD13_L_POS(rev)	(APRXS_WD12_H_POS(rev) + APRXS_WD12_H_EN(rev))	/* 12 */
#define APRXS_WD13_H_POS(rev)	(APRXS_WD13_L_POS(rev) + APRXS_WD13_L_EN(rev))	/* 12 */
#define APRXS_WD14_L_POS(rev)	(APRXS_WD13_H_POS(rev) + APRXS_WD13_H_EN(rev))	/* 12 */
#define APRXS_WD14_H_POS(rev)	(APRXS_WD14_L_POS(rev) + APRXS_WD14_L_EN(rev))	/* 12 */
#define APRXS_WD15_L_POS(rev)	(APRXS_WD14_H_POS(rev) + APRXS_WD14_H_EN(rev))	/* 12 */
#define APRXS_WD15_H_POS(rev)	(APRXS_WD15_L_POS(rev) + APRXS_WD15_L_EN(rev))	/* 12 */
#define APRXS_WD16_L_POS(rev)	(APRXS_WD15_H_POS(rev) + APRXS_WD15_H_EN(rev))	/* 12 */
#define APRXS_WD16_H_POS(rev)	(APRXS_WD16_L_POS(rev) + APRXS_WD16_L_EN(rev))	/* 13 */
#define APRXS_WD17_L_POS(rev)	(APRXS_WD16_H_POS(rev) + APRXS_WD16_H_EN(rev))	/* 13 */
#define APRXS_WD17_H_POS(rev)	(APRXS_WD17_L_POS(rev) + APRXS_WD17_L_EN(rev))	/* 13 */
#define APRXS_WD18_L_POS(rev)	(APRXS_WD17_H_POS(rev) + APRXS_WD17_H_EN(rev))	/* 13 */
#define APRXS_WD18_H_POS(rev)	(APRXS_WD18_L_POS(rev) + APRXS_WD18_L_EN(rev))	/* 14 */
#define APRXS_WD19_L_POS(rev)	(APRXS_WD18_H_POS(rev) + APRXS_WD18_H_EN(rev))	/* 14 */
#define APRXS_WD19_H_POS(rev)	(APRXS_WD19_L_POS(rev) + APRXS_WD19_L_EN(rev))	/* 14 */
#define APRXS_WD20_L_POS(rev)	(APRXS_WD19_H_POS(rev) + APRXS_WD19_H_EN(rev))	/* 14 */
#define APRXS_WD20_H_POS(rev)	(APRXS_WD20_L_POS(rev) + APRXS_WD20_L_EN(rev))	/* 15 */
#define APRXS_WD21_L_POS(rev)	(APRXS_WD20_H_POS(rev) + APRXS_WD20_H_EN(rev))	/* 16 */
#define APRXS_WD21_H_POS(rev)	(APRXS_WD21_L_POS(rev) + APRXS_WD21_L_EN(rev))	/* 16 */
#define APRXS_WD22_L_POS(rev)	(APRXS_WD21_H_POS(rev) + APRXS_WD21_H_EN(rev))	/* 17 */
#define APRXS_WD22_H_POS(rev)	(APRXS_WD22_L_POS(rev) + APRXS_WD22_L_EN(rev))	/* 18 */
#define APRXS_WD23_L_POS(rev)	(APRXS_WD22_H_POS(rev) + APRXS_WD22_H_EN(rev))	/* 19 */
#define APRXS_WD23_H_POS(rev)	(APRXS_WD23_L_POS(rev) + APRXS_WD23_L_EN(rev))	/* 20 */
#define APRXS_WD24_L_POS(rev)	(APRXS_WD23_H_POS(rev) + APRXS_WD23_H_EN(rev))	/* 21 */
#define APRXS_WD24_H_POS(rev)	(APRXS_WD24_L_POS(rev) + APRXS_WD24_L_EN(rev))	/* 21 */
#define APRXS_WD25_L_POS(rev)	(APRXS_WD24_H_POS(rev) + APRXS_WD24_H_EN(rev))	/* 22 */
#define APRXS_WD25_H_POS(rev)	(APRXS_WD25_L_POS(rev) + APRXS_WD25_L_EN(rev))	/* 23 */

#define APRXS_NBYTES(rev)	(APRXS_WD25_H_POS(rev)) // total number of bytes enabled

// frame type
#define APRXS_FT_POS(rev)		APRXS_WD0_L_POS(rev)
#define APRXS_FT_MASK			0xFu
#define APRXS_FT(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_FT_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_FT_POS(rev)]) & \
		APRXS_FT_MASK)

// unsupported rate
#define APRXS_UNSRATE_POS(rev)		APRXS_WD0_L_POS(rev)
#define APRXS_UNSRATE_MASK		0x10u
#define APRXS_UNSRATE_SHIFT		4u
#define APRXS_UNSRATE(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_UNSRATE_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_UNSRATE_POS(rev)]) & \
		APRXS_UNSRATE_MASK) >> APRXS_UNSRATE_SHIFT)

// band
#define APRXS_BAND_POS(rev)		APRXS_WD0_L_POS(rev)
#define APRXS_BAND_MASK			0x20u
#define APRXS_BAND_SHIFT		5u
#define APRXS_BAND(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_BAND_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_BAND_POS(rev)]) & \
		APRXS_BAND_MASK) >> APRXS_BAND_SHIFT)

// lost CRS
#define APRXS_LOSTCRS_POS(rev)		APRXS_WD0_L_POS(rev)
#define APRXS_LOSTCRS_MASK		0x40u
#define APRXS_LOSTCRS_SHIFT		6u
#define APRXS_LOSTCRS(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_LOSTCRS_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_LOSTCRS_POS(rev)]) & \
		APRXS_LOSTCRS_MASK) >> APRXS_LOSTCRS_SHIFT)

// short preamble
#define APRXS_SHORTH_POS(rev)		APRXS_WD0_L_POS(rev)
#define APRXS_SHORTH_MASK		0x80u
#define APRXS_SHORTH_SHIFT		7u
#define APRXS_SHORTH(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_SHORTH_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_SHORTH_POS(rev)]) & \
		APRXS_SHORTH_MASK) >> APRXS_SHORTH_SHIFT)

// plcp format violation
#define APRXS_PLCPFV_POS(rev)		APRXS_WD0_H_POS(rev)
#define APRXS_PLCPFV_MASK		0x1u
#define APRXS_PLCPFV(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_PLCPFV_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_PLCPFV_POS(rev)]) & \
		APRXS_PLCPFV_MASK)

// plcp header CRC failed
#define APRXS_PLCPHCF_POS(rev)		APRXS_WD0_H_POS(rev)
#define APRXS_PLCPHCF_MASK		0x2u
#define APRXS_PLCPHCF_SHIFT		1u
#define APRXS_PLCPHCF(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_PLCPHCF_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_PLCPHCF_POS(rev)]) & \
		APRXS_PLCPHCF_MASK) >> APRXS_PLCPHCF_SHIFT)

// MFCRS fired
#define APRXS_MFCRS_FIRED_POS(rev)	APRXS_WD0_H_POS(rev)
#define APRXS_MFCRS_FIRED_MASK		0x4u
#define APRXS_MFCRS_FIRED_SHIFT		2u
#define APRXS_MFCRS_FIRED(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_MFCRS_FIRED_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_MFCRS_FIRED_POS(rev)]) & \
		APRXS_MFCRS_FIRED_MASK) >> APRXS_MFCRS_FIRED_SHIFT)

// ACCRS fired
#define APRXS_ACCRS_FIRED_POS(rev)	APRXS_WD0_H_POS(rev)
#define APRXS_ACCRS_FIRED_MASK		0x8u
#define APRXS_ACCRS_FIRED_SHIFT		3u
#define APRXS_ACCRS_FIRED(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_ACCRS_FIRED_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_ACCRS_FIRED_POS(rev)]) & \
		APRXS_ACCRS_FIRED_MASK) >> APRXS_ACCRS_FIRED_SHIFT)

// MU PPDU
#define APRXS_MUPPDU_POS(rev)		APRXS_WD0_H_POS(rev)
#define APRXS_MUPPDU_MASK		0x10u
#define APRXS_MUPPDU_SHIFT		4u
#define APRXS_MUPPDU(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_MUPPDU_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_MUPPDU_POS(rev)]) & \
		APRXS_MUPPDU_MASK) >> APRXS_MUPPDU_SHIFT)

// OBSS status
#define APRXS_OBSS_STS_POS(rev)		APRXS_WD0_H_POS(rev)
#define APRXS_OBSS_STS_MASK		0xE0u
#define APRXS_OBSS_STS_SHIFT		5u
#define APRXS_OBSS_STS(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_OBSS_STS_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_OBSS_STS_POS(rev)]) & \
		APRXS_OBSS_STS_MASK) >> APRXS_OBSS_STS_SHIFT)

// coremask
#define APRXS_COREMASK_POS(rev)		APRXS_WD1_L_POS(rev)
#define APRXS_COREMASK_MASK		0xFu
#define APRXS_COREMASK(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_COREMASK_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_COREMASK_POS(rev)]) & \
		APRXS_COREMASK_MASK)

// antcfg
#define APRXS_ANTCFG_POS(rev)		APRXS_WD1_L_POS(rev)
#define APRXS_ANTCFG_MASK		0xF0u
#define APRXS_ANTCFG_SHIFT		4u
#define APRXS_ANTCFG(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_ANTCFG_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_ANTCFG_POS(rev)]) & \
		APRXS_ANTCFG_MASK) >> APRXS_ANTCFG_SHIFT)

// final BW classification
#define APRXS_SUBBAND_POS(rev)		APRXS_WD1_H_POS(rev)
#define APRXS_SUBBAND_MASK		0xFFu
#define APRXS_SUBBAND(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_SUBBAND_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_SUBBAND_POS(rev)]) & \
		APRXS_SUBBAND_MASK)

// Rx power Antenna0
#define APRXS_RXPWR_ANT0_POS(rev)	APRXS_WD2_L_POS(rev)
#define APRXS_RXPWR_ANT0_MASK		0xFFu
#define APRXS_RXPWR_ANT0(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RXPWR_ANT0_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RXPWR_ANT0_POS(rev)]) & \
		APRXS_RXPWR_ANT0_MASK)

// Rx power Antenna1
#define APRXS_RXPWR_ANT1_POS(rev)	APRXS_WD2_H_POS(rev)
#define APRXS_RXPWR_ANT1_MASK		0xFFu
#define APRXS_RXPWR_ANT1(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RXPWR_ANT1_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RXPWR_ANT1_POS(rev)]) & \
		APRXS_RXPWR_ANT1_MASK)

// Rx power Antenna2
#define APRXS_RXPWR_ANT2_POS(rev)	APRXS_WD3_L_POS(rev)
#define APRXS_RXPWR_ANT2_MASK		0xFFu
#define APRXS_RXPWR_ANT2(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RXPWR_ANT2_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RXPWR_ANT2_POS(rev)]) & \
		APRXS_RXPWR_ANT2_MASK)

// Rx power Antenna3
#define APRXS_RXPWR_ANT3_POS(rev)	APRXS_WD3_H_POS(rev)
#define APRXS_RXPWR_ANT3_MASK		0xFFu
#define APRXS_RXPWR_ANT3(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RXPWR_ANT3_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RXPWR_ANT3_POS(rev)]) & \
		APRXS_RXPWR_ANT3_MASK)

// RX ELNA INDEX ANT0
#define APRXS_ELNA_IDX_ANT0_POS(rev)	APRXS_WD20_L_POS(rev)
#define APRXS_ELNA_IDX_ANT0_MASK		0x2u
#define APRXS_ELNA_IDX_ANT0_SHIFT		1u
#define APRXS_ELNA_IDX_ANT0(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_ELNA_IDX_ANT0_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_ELNA_IDX_ANT0_POS(rev)]) & \
		APRXS_ELNA_IDX_ANT0_MASK) >> APRXS_ELNA_IDX_ANT0_SHIFT)

// RX ELNA INDEX ANT1
#define APRXS_ELNA_IDX_ANT1_POS(rev)	APRXS_WD20_L_POS(rev)
#define APRXS_ELNA_IDX_ANT1_MASK		0x20u
#define APRXS_ELNA_IDX_ANT1_SHIFT		5u
#define APRXS_ELNA_IDX_ANT1(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_ELNA_IDX_ANT1_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_ELNA_IDX_ANT1_POS(rev)]) & \
		APRXS_ELNA_IDX_ANT1_MASK) >> APRXS_ELNA_IDX_ANT1_SHIFT)

// RX TIA INDEX ANT0 LO
#define APRXS_TIA_IDX_ANT0_POS(rev)	APRXS_WD16_L_POS(rev)
#define APRXS_TIA_IDX_ANT0_MASK		0x1Cu
#define APRXS_TIA_IDX_ANT0_SHIFT	2u
#define APRXS_TIA_IDX_ANT0(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_TIA_IDX_ANT0_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_TIA_IDX_ANT0_POS(rev)]) & \
		APRXS_TIA_IDX_ANT0_MASK) >> APRXS_TIA_IDX_ANT0_SHIFT)

// RX TIA INDEX ANT1 LO
#define APRXS_TIA_IDX_ANT1_POS(rev)	APRXS_WD18_L_POS(rev)
#define APRXS_TIA_IDX_ANT1_MASK		0x1Cu
#define APRXS_TIA_IDX_ANT1_SHIFT		2u
#define APRXS_TIA_IDX_ANT1(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_TIA_IDX_ANT1_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_TIA_IDX_ANT1_POS(rev)]) & \
		APRXS_TIA_IDX_ANT1_MASK) >> APRXS_TIA_IDX_ANT1_SHIFT)

// RX VSW INDEX ANT0
#define APRXS_VSW_IDX_ANT0_POS(rev)	APRXS_WD20_L_POS(rev)
#define APRXS_VSW_IDX_ANT0_MASK		0x8u
#define APRXS_VSW_IDX_ANT0_SHIFT	3u
#define APRXS_VSW_IDX_ANT0(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_VSW_IDX_ANT0_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_VSW_IDX_ANT0_POS(rev)]) & \
		APRXS_VSW_IDX_ANT0_MASK) >> APRXS_VSW_IDX_ANT0_SHIFT)

// RX VSW INDEX ANT1
#define APRXS_VSW_IDX_ANT1_POS(rev)	APRXS_WD20_L_POS(rev)
#define APRXS_VSW_IDX_ANT1_MASK		0x80u
#define APRXS_VSW_IDX_ANT1_SHIFT	7u
#define APRXS_VSW_IDX_ANT1(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_VSW_IDX_ANT1_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_VSW_IDX_ANT1_POS(rev)]) & \
		APRXS_VSW_IDX_ANT1_MASK) >> APRXS_VSW_IDX_ANT1_SHIFT)

// RSSI fractional bits
#define APRXS_RXPWR_FRAC_POS(rev)	APRXS_WD4_L_POS(rev)
#define APRXS_RXPWR_FRAC_MASK		0xFFu
#define APRXS_RXPWR_FRAC(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RXPWR_FRAC_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RXPWR_FRAC_POS(rev)]) & \
		APRXS_RXPWR_FRAC_MASK)

// Ucode overwrites ClipCount with GILTF
#define APRXS_GILTF_POS(rev)		APRXS_WD4_H_POS(rev)
#define APRXS_GILTF_MASK		0x18u
#define APRXS_GILTF_SHIFT		3u
#define APRXS_GILTF(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_GILTF_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_GILTF_POS(rev)]) & \
		APRXS_GILTF_MASK) >> APRXS_GILTF_SHIFT)

#define APRXS_DYNBWINNONHT_POS(rev)	APRXS_WD4_H_POS(rev)
#define APRXS_DYNBWINNONHT_MASK		0x20u
#define APRXS_DYNBWINNONHT_SHIFT	5u
#define APRXS_DYNBWINNONHT(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_DYNBWINNONHT_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_DYNBWINNONHT_POS(rev)]) & \
		APRXS_DYNBWINNONHT_MASK) >> APRXS_DYNBWINNONHT_SHIFT)

#define APRXS_MCSSQSNR0_POS(rev)	APRXS_WD5_L_POS(rev)
#define APRXS_MCSSQSNR0_MASK		0xFFu
#define APRXS_MCSSQSNR0(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_MCSSQSNR0_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_MCSSQSNR0_POS(rev)]) & \
		APRXS_MCSSQSNR0_MASK)

#define APRXS_MCSSQSNR1_POS(rev)	APRXS_WD5_H_POS(rev)
#define APRXS_MCSSQSNR1_MASK		0xFFu
#define APRXS_MCSSQSNR1(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_MCSSQSNR1_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_MCSSQSNR1_POS(rev)]) & \
		APRXS_MCSSQSNR1_MASK)

#define APRXS_MCSSQSNR2_POS(rev)	APRXS_WD6_L_POS(rev)
#define APRXS_MCSSQSNR2_MASK		0xFFu
#define APRXS_MCSSQSNR2(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_MCSSQSNR2_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_MCSSQSNR2_POS(rev)]) & \
		APRXS_MCSSQSNR2_MASK)

#define APRXS_CHBWINNONHT_POS(rev)	APRXS_WD8_H_POS(rev)
#define APRXS_CHBWINNONHT_MASK		0x3u
#define APRXS_CHBWINNONHT(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_CHBWINNONHT_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_CHBWINNONHT_POS(rev)]) & \
		APRXS_CHBWINNONHT_MASK)

// User type
#define APRXS_USTY_POS(rev)		APRXS_WD23_H_POS(rev)
#define APRXS_USTY_MASK			0xE0u
#define APRXS_USTY_SHIFT		0x5u
#define APRXS_USTY(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_USTY_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_USTY_POS(rev)]) & \
		APRXS_USTY_MASK) >> APRXS_USTY_SHIFT)

// 11ax frame format
#define APRXS_AXFF_POS(rev)		APRXS_WD20_H_POS(rev)
#define APRXS_AXFF_MASK			0x7u
#define APRXS_AXFF(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_AXFF_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_AXFF_POS(rev)]) & \
		APRXS_AXFF_MASK)

// MCS
#define APRXS_AXMCS_POS(rev)		APRXS_WD21_H_POS(rev)
#define APRXS_AXMCS_MASK		0xFu
#define APRXS_AXMCS(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_AXMCS_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_AXMCS_POS(rev)]) & \
		APRXS_AXMCS_MASK)

// Coding
#define APRXS_CODING_POS(rev)		APRXS_WD21_H_POS(rev)
#define APRXS_CODING_MASK		0x10u
#define APRXS_CODING_SHIFT		4u
#define APRXS_CODING(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_CODING_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_CODING_POS(rev)]) & \
		APRXS_CODING_MASK) >> APRXS_CODING_SHIFT)

// STAID
#define APRXS_AX_STAID_L_POS(rev)		APRXS_WD22_L_POS(rev)
#define APRXS_AX_STAID_L_MASK		0xFFu
#define APRXS_AX_STAID_L(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_AX_STAID_L_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_AX_STAID_L_POS(rev)]) & \
		APRXS_AX_STAID_L_MASK)

#define APRXS_AX_STAID_H_POS(rev)		APRXS_WD22_H_POS(rev)
#define APRXS_AX_STAID_H_MASK		0x03u
#define APRXS_AX_STAID_H(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_AX_STAID_H_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_AX_STAID_H_POS(rev)]) & \
		APRXS_AX_STAID_H_MASK)

#define APRXS_AX_STAID(rxh, rev, min_rev)	((APRXS_AX_STAID_H(rxh, rev, min_rev) << 1) |\
		APRXS_AX_STAID_L(rxh, rev, min_rev))

// NSTS
#define APRXS_NSTS_POS(rev)		APRXS_WD22_H_POS(rev)
#define APRXS_NSTS_MASK			0x38u
#define APRXS_NSTS_SHIFT		3u
#define APRXS_NSTS(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_DCM_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_DCM_POS(rev)]) & \
		APRXS_NSTS_MASK) >> APRXS_NSTS_SHIFT)

// TXBF
#define APRXS_TXBF_POS(rev)		APRXS_WD22_H_POS(rev)
#define APRXS_TXBF_MASK			0x40u
#define APRXS_TXBF_SHIFT		6u
#define APRXS_TXBF(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_TXBF_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_TXBF_POS(rev)]) & \
			APRXS_TXBF_MASK) >> APRXS_TXBF_SHIFT)

//DCM
#define APRXS_DCM_POS(rev)		APRXS_WD22_H_POS(rev)
#define APRXS_DCM_MASK			0x80u
#define APRXS_DCM_SHIFT			7u
#define APRXS_DCM(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_DCM_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_DCM_POS(rev)]) & \
		APRXS_DCM_MASK) >> APRXS_DCM_SHIFT)

// RU Offset
#define APRXS_AX_RUALLOC_POS(rev)	APRXS_WD23_L_POS(rev)
#define APRXS_AX_RUALLOC_MASK		0x7Fu
#define APRXS_AX_RUALLOC_SHIFT		0u
#define APRXS_AX_RUALLOC(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_AX_RUALLOC_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_AX_RUALLOC_POS(rev)]) & \
		APRXS_AX_RUALLOC_MASK) >> APRXS_AX_RUALLOC_SHIFT)

#define APRXS_PE_L_POS(rev)		APRXS_WD23_L_POS(rev)
#define APRXS_PE_L_MASK			0x80u
#define APRXS_PE_L_SHIFT		7u
#define APRXS_PE_L(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_PE_L_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_PE_L_POS(rev)]) & \
			APRXS_PE_L_MASK) >> APRXS_PE_L_SHIFT)

#define APRXS_PE_H_POS(rev)		APRXS_WD23_H_POS(rev)
#define APRXS_PE_H_MASK			0x3u
#define APRXS_PE_H(rxh, rev, min_rev) \
	((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_PE_H_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_PE_H_POS(rev)]) & \
		APRXS_PE_H_MASK)

#define APRXS_PE(rxh, rev, rev_min) \
	((APRXS_PE_H(rxh, rev, rev_min) << 1) | APRXS_PE_L(rxh, rev, rev_min))

#define APRXS_RU_POS(rev)		APRXS_WD23_H_POS(rev)
#define APRXS_RU_MASK			0x1Cu
#define APRXS_RU_SHIFT			2u
#define APRXS_RU(rxh, rev, min_rev) \
	(((D11REV_MAJ_MIN_GE(rev, min_rev, 87, 1) ? \
		(rxh)->ge87_1.PHYRXSTATUS[APRXS_RU_POS(rev)] : \
		(rxh)->ge85.PHYRXSTATUS[APRXS_RU_POS(rev)]) & \
		APRXS_RU_MASK) >> APRXS_RU_SHIFT)

#endif /* _d11_autophyrxsts_ */

#if defined(AUTO_PHYRXSTS)
#define AUTO_PHYRXSTS_ENAB()		1u
#else
#define AUTO_PHYRXSTS_ENAB()		0u
#endif /* AUTO_PHYRXSTS */

/* PhyRxStatus_0: */
#define	PRXS0_FT_MASK		0x0003u	/**< [PRE-HE] NPHY only: CCK, OFDM, HT, VHT */
#define	PRXS0_CLIP_MASK		0x000Cu	/**< NPHY only: clip count adjustment steps by AGC */
#define	PRXS0_CLIP_SHIFT	2u	/**< SHIFT bits for clip count adjustment */
#define	PRXS0_UNSRATE		0x0010u	/**< PHY received a frame with unsupported rate */
#define PRXS0_UNSRATE_SHIFT	4u
#define	PRXS0_RXANT_UPSUBBAND	0x0020u	/**< GPHY: rx ant, NPHY: upper sideband */
#define	PRXS0_LCRS		0x0040u	/**< CCK frame only: lost crs during cck frame reception */
#define	PRXS0_SHORTH		0x0080u	/**< Short Preamble */
#define PRXS0_SHORTH_SHIFT	7u
#define	PRXS0_PLCPFV		0x0100u	/**< PLCP violation */
#define	PRXS0_PLCPFV_SHIFT	8u
#define	PRXS0_PLCPHCF		0x0200u	/**< PLCP header integrity check failed */
#define	PRXS0_PLCPHCF_SHIFT	9u
#define	PRXS0_GAIN_CTL		0x4000u	/**< legacy PHY gain control */
#define PRXS0_ANTSEL_MASK	0xF000u	/**< NPHY: Antennas used for received frame, bitmask */
#define PRXS0_ANTSEL_SHIFT	12u	/**< SHIFT bits for Antennas used for received frame */
#define PRXS0_PPDU_MASK         0x1000u  /**< PPDU type SU/MU */

/* subfield PRXS0_FT_MASK [PRXS0_PRE_HE_FT_MASK] */
#define	PRXS0_CCK		0x0000u
#define	PRXS0_OFDM		0x0001u	/**< valid only for G phy, use rxh->RxChan for A phy */
#define	PRXS0_PREN		0x0002u
#define	PRXS0_STDN		0x0003u

/* subfield PRXS0_ANTSEL_MASK */
#define PRXS0_ANTSEL_0		0x0u	/**< antenna 0 is used */
#define PRXS0_ANTSEL_1		0x2u	/**< antenna 1 is used */
#define PRXS0_ANTSEL_2		0x4u	/**< antenna 2 is used */
#define PRXS0_ANTSEL_3		0x8u	/**< antenna 3 is used */

/* PhyRxStatus_1: */
#define PRXS1_JSSI_MASK         0x00FFu
#define PRXS1_JSSI_SHIFT        0u
#define PRXS1_SQ_MASK           0xFF00u
#define PRXS1_SQ_SHIFT          8u
#define PRXS1_COREMAP           0x000Fu  /**< core enable bits for core 0/1/2/3 */
#define PRXS1_ANTCFG            0x00F0u  /**< anttenna configuration bits */

#define PHY_COREMAP_LT85(rxh, rev) \
	((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_1) : \
		D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_1)) & \
		PRXS1_COREMAP)
#define PHY_COREMAP(rev, rev_min, rxh)		(AUTO_PHYRXSTS_ENAB() ?		\
		APRXS_COREMASK(rxh, rev, rev_min) : PHY_COREMAP_LT85(rxh, rev))

#define PHY_ANTMAP_LT85(rxh, corerev) \
	(((D11REV_GE(corerev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_1) : \
		D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_1)) & \
		PRXS1_ANTCFG) >> 4)
#define PHY_ANTMAP(rev, rev_min, rxh)		(AUTO_PHYRXSTS_ENAB() ?		\
		APRXS_ANTCFG(rxh, rev, rev_min) : PHY_ANTMAP_LT85(rxh, rev))

/* nphy PhyRxStatus_1: */
#define PRXS1_nphy_PWR0_MASK	0x00FF
#define PRXS1_nphy_PWR1_MASK	0xFF00

/* PhyRxStatus_2: */
#define	PRXS2_LNAGN_MASK	0xC000
#define	PRXS2_LNAGN_SHIFT	14
#define	PRXS2_PGAGN_MASK	0x3C00
#define	PRXS2_PGAGN_SHIFT	10
#define	PRXS2_FOFF_MASK		0x03FF

/* nphy PhyRxStatus_2: */
#define PRXS2_nphy_SQ_ANT0	0x000F	/**< nphy overall signal quality for antenna 0 */
#define PRXS2_nphy_SQ_ANT1	0x00F0	/**< nphy overall signal quality for antenna 0 */
#define PRXS2_nphy_cck_SQ	0x00FF	/**< bphy signal quality(when FT field is 0) */
#define PRXS3_nphy_SSQ_MASK	0xFF00	/**< spatial conditioning of the two receive channels */
#define PRXS3_nphy_SSQ_SHIFT	8

/* PhyRxStatus_3: */
#define	PRXS3_DIGGN_MASK	0x1800
#define	PRXS3_DIGGN_SHIFT	11
#define	PRXS3_TRSTATE		0x0400

/* nphy PhyRxStatus_3: */
#define PRXS3_nphy_MMPLCPLen_MASK	0x0FFF	/**< Mixed-mode preamble PLCP length */
#define PRXS3_nphy_MMPLCP_RATE_MASK	0xF000	/**< Mixed-mode preamble rate field */
#define PRXS3_nphy_MMPLCP_RATE_SHIFT	12

/* HTPHY Rx Status defines */
/* htphy PhyRxStatus_0: those bit are overlapped with PhyRxStatus_0 */
#define PRXS0_BAND	        0x0400	/**< 0 = 2.4G, 1 = 5G */
#define PRXS0_RSVD	        0x0800	/**< reserved; set to 0 */
#define PRXS0_UNUSED	        0xF000	/**< unused and not defined; set to 0 */

/* htphy PhyRxStatus_1: */
#define PRXS1_HTPHY_MMPLCPLenL_MASK	0xFF00	/**< Mixmode PLCP Length low byte mask */

/* htphy PhyRxStatus_2: */
#define PRXS2_HTPHY_MMPLCPLenH_MASK	0x000F	/**< Mixmode PLCP Length high byte maskw */
#define PRXS2_HTPHY_MMPLCH_RATE_MASK	0x00F0	/**< Mixmode PLCP rate mask */
#define PRXS2_HTPHY_RXPWR_ANT0	0xFF00	/**< Rx power on core 0 */

/* htphy PhyRxStatus_3: */
#define PRXS3_HTPHY_RXPWR_ANT1	0x00FF	/**< Rx power on core 1 */
#define PRXS3_HTPHY_RXPWR_ANT2	0xFF00	/**< Rx power on core 2 */

/* htphy PhyRxStatus_4: */
#define PRXS4_HTPHY_RXPWR_ANT3	0x00FF	/**< Rx power on core 3 */
#define PRXS4_HTPHY_CFO		0xFF00	/**< Coarse frequency offset */

/* htphy PhyRxStatus_5: */
#define PRXS5_HTPHY_FFO	        0x00FF	/**< Fine frequency offset */
#define PRXS5_HTPHY_AR	        0xFF00	/**< Advance Retard */

/* ACPHY RxStatus defs */

/* ACPHY PhyRxStatus_0: */
#define PRXS0_ACPHY_FT_MASK      0x0003  /**< CCK, OFDM, HT, VHT */
#define PRXS0_ACPHY_CLIP_MASK    0x000C  /**< clip count adjustment steps by AGC */
#define PRXS0_ACPHY_CLIP_SHIFT        2
#define PRXS0_ACPHY_UNSRATE      0x0010  /**< PHY received a frame with unsupported rate */
#define PRXS0_ACPHY_BAND5G       0x0020  /**< Rx Band indication: 0 -> 2G, 1 -> 5G */
#define PRXS0_ACPHY_LCRS         0x0040  /**< CCK frame only: lost crs during cck frame reception */
#define PRXS0_ACPHY_SHORTH       0x0080  /**< Short Preamble (CCK), GF preamble (HT) */
#define PRXS0_ACPHY_PLCPFV       0x0100  /**< PLCP violation */
#define PRXS0_ACPHY_PLCPHCF      0x0200  /**< PLCP header integrity check failed */
#define PRXS0_ACPHY_MFCRS        0x0400  /**< Matched Filter CRS fired */
#define PRXS0_ACPHY_ACCRS        0x0800  /**< Autocorrelation CRS fired */
#define PRXS0_ACPHY_SUBBAND_MASK 0xF000  /**< FinalBWClassification:
	                                  * lower nibble Bitfield of sub-bands occupied by Rx frame
	                                  */
/* ACPHY PhyRxStatus_1: */
#define PRXS1_ACPHY_ANT_CORE0	0x0001	/* Antenna Config for core 0 */
#define PRXS1_ACPHY_SUBBAND_MASK_GEN2 0xFF00  /**< FinalBWClassification:
					 * lower byte Bitfield of sub-bands occupied by Rx frame
					 */
#define PRXS0_ACPHY_SUBBAND_SHIFT    12
#define PRXS1_ACPHY_SUBBAND_SHIFT_GEN2    8

/* acphy PhyRxStatus_3: */
#define PRXS2_ACPHY_RXPWR_ANT0	0xFF00	/**< Rx power on core 1 */
#define PRXS3_ACPHY_RXPWR_ANT1	0x00FF	/**< Rx power on core 1 */
#define PRXS3_ACPHY_RXPWR_ANT2	0xFF00	/**< Rx power on core 2 */
#define PRXS3_ACPHY_SNR_ANT0 0xFF00     /* SNR on core 0 */

/* acphy PhyRxStatus_4: */
/** FinalBWClassification:upper nibble of sub-bands occupied by Rx frame */
#define PRXS4_ACPHY_SUBBAND_MASK 0x000F
#define PRXS4_ACPHY_RXPWR_ANT3	0x00FF	/**< Rx power on core 3 */
#define PRXS4_ACPHY_SNR_ANT1 0xFF00     /* SNR on core 1 */

#define PRXS5_ACPHY_CHBWINNONHT_MASK 0x0003
#define PRXS5_ACPHY_CHBWINNONHT_20MHZ	0
#define PRXS5_ACPHY_CHBWINNONHT_40MHZ	1
#define PRXS5_ACPHY_CHBWINNONHT_80MHZ	2
#define PRXS5_ACPHY_CHBWINNONHT_160MHZ	3 /* includes 80+80 */
#define PRXS5_ACPHY_DYNBWINNONHT_MASK 0x0004

/** Get Rx power on core 0 */
#define ACPHY_RXPWR_ANT0(rxs)	(((rxs)->lt80.PhyRxStatus_2 & PRXS2_ACPHY_RXPWR_ANT0) >> 8)
/** Get Rx power on core 1 */
#define ACPHY_RXPWR_ANT1(rxs)	((rxs)->lt80.PhyRxStatus_3 & PRXS3_ACPHY_RXPWR_ANT1)
/** Get Rx power on core 2 */
#define ACPHY_RXPWR_ANT2(rxs)	(((rxs)->lt80.PhyRxStatus_3 & PRXS3_ACPHY_RXPWR_ANT2) >> 8)
/** Get Rx power on core 3 */
#define ACPHY_RXPWR_ANT3(rxs)	((rxs)->lt80.PhyRxStatus_4 & PRXS4_ACPHY_RXPWR_ANT3)

/** MCSSQSNR location access. MCSSQ usage is limited by chip specific impl,
 * and there is no way to commonize these status location yet.
 * TODO: When the storage locations are settled we need to revisit
 * this defs controls.
 */

/* exception handling */
#ifdef PHY_CORE_MAX
#if PHY_CORE_MAX > 4
#error "PHY_CORE_MAX is exceeded more than MCSSQSNR defs (4)"
#endif
#endif /* PHY_CORE_MAX */

/* rev 48/55/59 are obsoleted for SNR in trunk */
#define D11_PRXS_MCSSQ_SNR_SUPPORT(corerev)	(D11REV_GE((corerev), 80))

#define ACPHY_SNR_MASK	(0xFF)
#define ACPHY_SNR_SHIFT	(8)

#define PRXS5_ACPHY_DYNBWINNONHT(rxs) ((rxs)->lt80.PhyRxStatus_5 & PRXS5_ACPHY_DYNBWINNONHT_MASK)
#define PRXS5_ACPHY_CHBWINNONHT(rxs) ((rxs)->lt80.PhyRxStatus_5 & PRXS5_ACPHY_CHBWINNONHT_MASK)

#define D11N_MMPLCPLen(rxs)	((rxs)->lt80.PhyRxStatus_3 & PRXS3_nphy_MMPLCPLen_MASK)
#define D11HT_MMPLCPLen(rxs) ((((rxs)->lt80.PhyRxStatus_1 & PRXS1_HTPHY_MMPLCPLenL_MASK) >> 8) | \
			      (((rxs)->lt80.PhyRxStatus_2 & PRXS2_HTPHY_MMPLCPLenH_MASK) << 8))

/* REV80 Defintions (corerev >= 80) */

/** Dma_flags Masks */
#define RXS_PHYRXST_VALID_REV_GE80	0x02

/** Get RxStatus1 */
#define RXSTATUS1_REV_GE87_1(rxs)	((rxs)->ge87_1.RxStatus1)
#define RXSTATUS1_REV_GE80(rxs)		((rxs)->ge80.RxStatus1)
#define RXSTATUS1_REV_LT80(rxs)		((rxs)->lt80.RxStatus1)

#define PHY_RXSTATUS1(corerev, corerev_minor, rxs) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? RXSTATUS1_REV_GE87_1(rxs) : \
	D11REV_GE(corerev, 80) ? RXSTATUS1_REV_GE80(rxs) : \
	RXSTATUS1_REV_LT80(rxs))

/* (FT Mask) PhyRxStatus_0: */
#define PRXS0_FT_MASK_REV_LT80		PRXS0_FT_MASK	/**< (corerev < 80) frame type field mask */

#define	PRXS0_FT_SHIFT_REV_GE80		8
#define	PRXS0_FT_MASK_REV_GE80		0x0700		/**
							 * (corerev >= 80) frame type field mask.
							 *
							 * 0 = CCK, 1 = 11a/g legacy OFDM,
							 * 2 = HT, 3 = VHT, 4 = 11ah, 5 = HE,
							 * 6-15 Rsvd.
							 */

/* *
* Macro to find Frame type from RX Hdr based on corerev.
*
* Note: From rev80 onwards frame type is indicated only
* in the phyrxstatus, which is valid only for the last
* MPDU of an AMPDU. Since FT is required for every MPDU,
* frametype for core-revs >= 80, shall be
* provided in bits (8:10) of MuRate field in RXH.
*
*/
#define D11PPDU_FT(rxh, rev) (\
	(D11REV_GE(rev, 80) ? \
	((D11RXHDR_ACCESS_VAL(rxh, rev, 0, MuRate) & PRXS_FT_MASK(rev)) >>	\
	(PRXS0_FT_SHIFT_REV_GE80)) : \
	(D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0) & PRXS_FT_MASK(rev))))

#define PRXS_UNSRATE_LT85(rxh, rev) \
	(((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_0) : \
	D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0)) & \
		PRXS0_UNSRATE) >> PRXS0_UNSRATE_SHIFT)

#define PRXS_UNSRATE(rxh, rev, min_rev)		(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_UNSRATE(rxh, rev, min_rev) : PRXS_UNSRATE_LT85(rxh, rev))

// 1: short (or GF) preamble, 0: long (or MM) preamble
#define PRXS_SHORTH_LT85(rxh, rev)	\
		(((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_0) : \
			D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0)) & \
		PRXS0_SHORTH) >> PRXS0_SHORTH_SHIFT)
#define PRXS_SHORTH(rxh, rev, min_rev)	\
		(AUTO_PHYRXSTS_ENAB() ? APRXS_SHORTH(rxh, rev, min_rev) : \
			PRXS_SHORTH_LT85(rxh, rev))

#define PRXS_PLCPFV_LT85(rxh, rev) \
	(((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_0) : \
	D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0)) & \
		PRXS0_PLCPFV) >> PRXS0_PLCPFV_SHIFT)
#define PRXS_PLCPFV(rxh, rev, rev_min)		(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_PLCPFV(rxh, rev, rev_min) : PRXS_PLCPFV_LT85(rxh, rev))

#define PRXS_PLCPHCF_LT85(rxh, rev) \
	(((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_0) : \
	D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0)) & \
		PRXS0_PLCPHCF) >> PRXS0_PLCPHCF_SHIFT)
#define PRXS_PLCPHCF(rxh, rev, rev_min)		(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_PLCPHCF(rxh, rev, rev_min) : PRXS_PLCPHCF_LT85(rxh, rev))

// final BW classification
#define PRXS_SUBBAND_ACPHY(rxh, rev, rev_min) \
	(((D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_0) & \
		PRXS0_ACPHY_SUBBAND_MASK) >> PRXS0_ACPHY_SUBBAND_SHIFT) | \
		((D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_4) & \
		PRXS4_ACPHY_SUBBAND_MASK) << 4))
#define PRXS_SUBBAND_ACPHY2(rxh, rev, rev_min)	\
		(((D11REV_GE(rev, 80) ? D11RXHDR_GE80_ACCESS_VAL(rxh, PhyRxStatus_1) : \
		D11RXHDR_LT80_ACCESS_VAL(rxh, PhyRxStatus_1)) & PRXS1_ACPHY2_SUBBAND_MASK) >> \
		PRXS1_ACPHY2_SUBBAND_SHIFT)

#define PRXS_SUBBAND(rxh, rev, rev_min, phyrev)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_SUBBAND(rxh, rev, rev_min) : (ACREV_GE(phyrev, 32) ? \
		PRXS_SUBBAND_ACPHY2(rxh, rev, rev_min) : \
		PRXS_SUBBAND_ACPHY(rxh, rev, rev_min)))

/* Macros to access MCS, NSTS and MU valididity from MuRate field in corerev > 80 RXH */
#define RXS_MU_VALID_MASK_REV80		0x0080
#define RXS_MU_VALID_SHIFT_REV80	7
#define RXS_MCS_MASK_REV80		0x000F
#define RXS_MCS_SHIFT_REV80		0
#define RXS_NSTS_MASK_REV80		0x0070
#define RXS_NSTS_SHIFT_REV80		4

#define D11PPDU_ISMU_REV80(rxh, corerev, corerev_minor) \
	((D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, MuRate) & \
	(RXS_MU_VALID_MASK_REV80)) >> RXS_MU_VALID_SHIFT_REV80)
#define D11RXHDR_GE80_GET_MCS(rxh, corerev, corerev_minor) \
	((D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, MuRate) & \
	(RXS_MCS_MASK_REV80)) >> RXS_MCS_SHIFT_REV80)
#define D11RXHDR_GE80_GET_NSTS(rxh, corerev, corerev_minor) \
	((D11RXHDR_ACCESS_VAL(rxh, corerev, corerev_minor, MuRate) & \
	(RXS_NSTS_MASK_REV80)) >> RXS_NSTS_SHIFT_REV80)

/* subfield PRXS0_FT_MASK_REV_GE80 */
#define	PRXS0_HE			0x0004	/**< HE frame type */

/* (Corerev >= 80) PhyRxStatus_2: */
#define PRXS2_RXPWR_ANT0_REV_GE80	0x00FF	/**< (corerev >= 80) Rx power on first antenna */
#define PRXS2_RXPWR_ANT1_REV_GE80	0xFF00	/**< (corerev >= 80) Rx power on second antenna */

/* (Corerev >= 80) PhyRxStatus_3: */
#define PRXS3_RXPWR_ANT2_REV_GE80	0x00FF	/**< (corerev >= 80) Rx power on third antenna */
#define PRXS3_RXPWR_ANT3_REV_GE80	0xFF00	/**
						 * (corerev >= 80) Rx power on fourth antenna.
						 *
						 * Note: For PHY revs 3 and > 4, OCL Status
						 * byte 0 will be reported if PHY register
						 * OCL_RxStatus_Ctrl is set to 0x2 or 0x6.
						 */
#define PRXS3_RXPWR_FRAC_REV_GE80	0xFFu

/** Get Rx power on ANT 0 */
#define RXPWR_ANT0_REV_GE80(rxs)	((rxs)->ge80.PhyRxStatus_2 & \
		(PRXS2_RXPWR_ANT0_REV_GE80))

#define PHY_RXPWR_ANT0(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_RXPWR_ANT0(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		RXPWR_ANT0_REV_GE80(rxs) : ACPHY_RXPWR_ANT0(rxs)))

/** Get Rx power on ANT 1 */
#define RXPWR_ANT1_REV_GE80(rxs)	(((rxs)->ge80.PhyRxStatus_2 & \
		(PRXS2_RXPWR_ANT1_REV_GE80)) >> 8)

#define PHY_RXPWR_ANT1(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_RXPWR_ANT1(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		RXPWR_ANT1_REV_GE80(rxs) : ACPHY_RXPWR_ANT1(rxs)))

/** Get Rx power on ANT 2 */
#define RXPWR_ANT2_REV_GE80(rxs)	((rxs)->ge80.PhyRxStatus_3 & \
		(PRXS3_RXPWR_ANT2_REV_GE80))

#define PHY_RXPWR_ANT2(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_RXPWR_ANT2(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		RXPWR_ANT2_REV_GE80(rxs) : ACPHY_RXPWR_ANT2(rxs)))

/** Get Rx power on ANT 3 */
#define RXPWR_ANT3_REV_GE80(rxs)	(((rxs)->ge80.PhyRxStatus_3 & \
		(PRXS3_RXPWR_ANT3_REV_GE80)) >> 8)

#define PHY_RXPWR_ANT3(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_RXPWR_ANT3(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		RXPWR_ANT3_REV_GE80(rxs) : ACPHY_RXPWR_ANT3(rxs)))

/*	Get the following entries from RXStatus bytes
*	for RSSI compensation
*	based on factory calibration
*	TIA Index
*	eLNA Index
*	V_path Switch
*/
#define PHY_ELNA_IDX_ANT0_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_ELNA_IDX_ANT0(rxs, corerev, corerev_min)
#define PHY_ELNA_IDX_ANT1_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_ELNA_IDX_ANT1(rxs, corerev, corerev_min)
#define PHY_TIA_IDX_ANT0_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_TIA_IDX_ANT0(rxs, corerev, corerev_min)
#define PHY_TIA_IDX_ANT1_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_TIA_IDX_ANT1(rxs, corerev, corerev_min)
#define PHY_VSW_IDX_ANT0_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_VSW_IDX_ANT0(rxs, corerev, corerev_min)
#define PHY_VSW_IDX_ANT1_REV_GE85(corerev, corerev_min, rxs) \
		APRXS_VSW_IDX_ANT1(rxs, corerev, corerev_min)

/** Get RSSI fractional bits */
#define RXPWR_FRAC_REV_GE80(rxs)	((rxs)->ge80.PhyRxStatus_4 & \
		(PRXS3_RXPWR_FRAC_REV_GE80))

#define RXPWR_FRAC(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_RXPWR_FRAC(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		RXPWR_FRAC_REV_GE80(rxs) : 0))

/* HECAPPHY PhyRxStatus_4: */
#define PRXS4_DYNBWINNONHT_MASK_REV_GE80	0x1000
#define PRXS4_DYNBWINNONHT_REV_GE80(rxs)	((rxs)->ge80.PhyRxStatus_4 & \
						PRXS4_DYNBWINNONHT_MASK_REV_GE80)

#define PRXS_PHY_DYNBWINNONHT(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_DYNBWINNONHT(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		PRXS4_DYNBWINNONHT_REV_GE80(rxs) : PRXS5_ACPHY_DYNBWINNONHT(rxs)))

/** (corerev >= 80) PhyRxStatus_5: MCSSQ SNR for core 0 and 1 */
#define PRXS5_MCSSQ_SHIFT           (8u)
#define PRXS5_MCSSQ_CORE0_REV_GE80  (0x00FF)
#define PRXS5_MCSSQ_CORE1_REV_GE80  (0xFF00)

#define MCSSQ_SNR_ANT0_GE80(rxs)    ((rxs)->ge80.PhyRxStatus_5 & PRXS5_MCSSQ_CORE0_REV_GE80)
#define MCSSQ_SNR_ANT0(rxs, rev, rev_min)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_MCSSQSNR0(rxs, rev, rev_min) : \
		((rxs)->ge80.PhyRxStatus_5 & PRXS5_MCSSQ_CORE0_REV_GE80))

#define MCSSQ_SNR_ANT1_GE80(rxs)    (((rxs)->ge80.PhyRxStatus_5 & PRXS5_MCSSQ_CORE1_REV_GE80) \
	>> PRXS5_MCSSQ_SHIFT)
#define MCSSQ_SNR_ANT1(rxs, rev, rev_min)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_MCSSQSNR1(rxs, rev, rev_min) : \
		(((rxs)->ge80.PhyRxStatus_5 & PRXS5_MCSSQ_CORE1_REV_GE80) \
			>> PRXS5_MCSSQ_SHIFT))

/** (corerev >= 80) PhyRxStatus_6: MCSSQ SNR for core 2 and 3 */
#define PRXS6_MCSSQ_SHIFT           (8u)
#define PRXS6_MCSSQ_CORE2_REV_GE80  (0x00FF)
#define PRXS6_MCSSQ_CORE3_REV_GE80  (0xFF00)

#define MCSSQ_SNR_ANT2_GE80(rxs)           (((rxs)->ge80.phyrxs_rem[0] &  \
	PRXS6_MCSSQ_CORE2_REV_GE80))
#define MCSSQ_SNR_ANT2(rxs, rev, rev_min)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_MCSSQSNR2(rxs, rev, rev_min) : \
		(((rxs)->ge80.phyrxs_rem[0] & PRXS6_MCSSQ_CORE2_REV_GE80)))

/* HECAPPHY PhyRxStatus_8 (part of phyrxs_rem[2]) : */
#define PRXS8_CHBWINNONHT_MASK_REV_GE80		0x0100
#define PRXS8_CHBWINNONHT_REV_GE80(rxs)		((rxs)->ge80.phyrxs_rem[2] & \
						PRXS8_CHBWINNONHT_MASK_REV_GE80)

#define PRXS_PHY_CHBWINNONHT(corerev, corerev_minor, rxs)	(AUTO_PHYRXSTS_ENAB() ? \
		APRXS_CHBWINNONHT(rxs, corerev, corerev_minor) : (D11REV_GE(corerev, 80) ? \
		PRXS8_CHBWINNONHT_REV_GE80(rxs) : PRXS5_ACPHY_CHBWINNONHT(rxs)))

/* HE phyrxs_rem[4] */
#define PRXS_REM4_PE_MASK_REV80			0x0380
#define PRXS_REM4_PE_SHIFT_REV80		7u
#define PRXS_REM4_RU_TYPE_MASK_REV80		0x1c00
#define PRXS_REM4_RU_TYPE_SHIFT_REV80		10u
#define PRXS_REM4_NUM_USER_SHIFT_REV80          13u
#define PRXS_REM4_NUM_USER_BIT_MASK_REV80       0xe000

/* HE phyrxs_rem[5] */
#define PRXS_REM5_GI_LTF_MASK_REV80		0x0003
#define PRXS_REM5_GI_LTF_SHIFT_REV80		0u
#define PRXS_REM5_11AX_FF_MASK_REV80		0x0700
#define PRXS_REM5_11AX_FF_SHIFT_REV80		8u

/* HE phyrxs_rem[6] */
#define PRXS_REM6_MCS_MASK_REV80		0x0f00
#define PRXS_REM6_MCS_SHIFT_REV80		8u
#define PRXS_REM6_CODING_MASK_REV80		0x1000
#define PRXS_REM6_CODING_SHIFT_REV80		12u

/* HE phyrxs_rem[7] */
#define PRXS_REM7_DCM_MASK_REV80		0x8000
#define PRXS_REM7_DCM_SHIFT_REV80		15u
#define PRXS_REM7_TXBF_MASK_REV80		0x4000
#define PRXS_REM7_TXBF_SHIFT_REV80		14u
#define PRXS_REM7_NSTS_MASK_REV80		0x3800
#define PRXS_REM7_NSTS_SHIFT_REV80		11u
#define PRXS_REM7_RU_ALLOC_MASK_REV80		0x007f
#define PRXS_REM7_RU_ALLOC_SHIFT_REV80		0u

#define PRXS_STAID_MASK				0x07ff
#define PRXS_STAID_SHIFT			0u

enum {
	HE_RU_TYPE_26T     = 0, /* 26 tone RU, 0 - 36 */
	HE_RU_TYPE_52T     = 1, /* 52 tone RU, 37 - 52 */
	HE_RU_TYPE_106T    = 2, /* 106 tone RU, 53 - 60 */
	HE_RU_TYPE_242T    = 3, /* 242 tone RU, 61 - 64 */
	HE_RU_TYPE_484T    = 4, /* 484 tone RU, 65 - 66 */
	HE_RU_TYPE_996T    = 5, /* 996 tone RU, 67 - 68 */
	HE_RU_TYPE_2x996T  = 6,	/* 2x996 tone RU, 69 */
	HE_RU_TYPE_LAST    = 7  /* Reserved, Invalid */
};

#define HE_RU_TYPE_MAX				6

/* received PE duration is present in phyrxs_rem[4] bit position [7-9] */
#define D11PPDU_PE_GE80(rxh, corerev)	((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[4]) &         \
		(PRXS_REM4_PE_MASK_REV80)) >> PRXS_REM4_PE_SHIFT_REV80)

#define D11PPDU_PE(rxh, corerev, corerev_minor)	(AUTO_PHYRXSTS_ENAB() ?                          \
		APRXS_PE(rxh, corerev, corerev_minor) : D11PPDU_PE_GE80(rxh, corerev))

/* received RU type is present in phyrxs_rem[4] bit position [10-11] */
#define D11PPDU_RU_TYPE(rxh, corerev, corerev_minor)                                              \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_RU(rxh, corerev, corerev_minor) :                           \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[4]) &                \
	(PRXS_REM4_RU_TYPE_MASK_REV80)) >> PRXS_REM4_RU_TYPE_SHIFT_REV80) : 0))

/* received he num of user type is present in phyrxs_rem[4] bit position [13-15] */
#define D11PPDU_HE_NUM_USER_TYPE(rxh, corerev, corerev_min)                                       \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_USTY(rxh, corerev, corerev_min) :                           \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[4]) &                \
	(PRXS_REM4_NUM_USER_BIT_MASK_REV80)) >> PRXS_REM4_NUM_USER_SHIFT_REV80) : 0))

#define D11PPDU_FF_TYPE(rxh, corerev, corerev_minor)                                              \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_AXFF(rxh, corerev, corerev_minor) :                         \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[5]) &                \
	(PRXS_REM5_11AX_FF_MASK_REV80)) >> PRXS_REM5_11AX_FF_SHIFT_REV80) : 0))

/* DCM is present in phyrxs_rem[7] byte 27, bit position [7] */
#define D11PPDU_DCM(rxh, corerev, corerev_minor)                                                  \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_DCM(rxh, corerev, corerev_minor) :                          \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[7]) &                \
	(PRXS_REM7_DCM_MASK_REV80)) >> PRXS_REM7_DCM_SHIFT_REV80) : 0))

/* coding used is present in phyrxs_rem[6] byte:25, bit position [12] */
#define D11PPDU_CODING(rxh, corerev, corerev_minor)                                               \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_CODING(rxh, corerev, corerev_minor) :                       \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[6]) &                \
	(PRXS_REM6_CODING_MASK_REV80)) >> PRXS_REM6_CODING_SHIFT_REV80) : 0))

/* spatial reuse 2 / STA-ID */
#define D11PPDU_STAID(rxh, corerev, corerev_minor)                                                \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_AX_STAID(rxh, corerev, corerev_minor) :                     \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[7]) &                \
	(PRXS_STAID_MASK)) >> PRXS_STAID_SHIFT) : 0))

#define D11PPDU_TXBF(rxh, corerev, corerev_minor)                                                 \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_TXBF(rxh, corerev, corerev_minor) :                         \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[7]) &                \
	(PRXS_REM7_TXBF_MASK_REV80)) >> PRXS_REM7_TXBF_SHIFT_REV80) : 0))

/* GI_LTF is present in phyrxs_rem[5] bit position [0-1] */
#define D11PPDU_GI_LTF(rxh, corerev, corerev_minor)                                               \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_GILTF(rxh, corerev, corerev_minor) :                        \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[5]) &                \
	(PRXS_REM5_GI_LTF_MASK_REV80)) >> PRXS_REM5_GI_LTF_SHIFT_REV80) : 0))

/* MCS is present in phyrxs_rem[6] - byte 25, bit position [8-11] */
#define D11PPDU_MCS(rxh, corerev, corerev_minor)                                                  \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_AXMCS(rxh, corerev, corerev_minor) :                        \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[6]) &                \
	(PRXS_REM6_MCS_MASK_REV80)) >> PRXS_REM6_MCS_SHIFT_REV80) : 0))

/* NSTS present in phyrxs_rem[7] bit position [11-13] */
#define D11PPDU_NSTS(rxh, corerev, corerev_minor)                                                 \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_NSTS(rxh, corerev, corerev_minor) :                         \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[7]) &                \
	(PRXS_REM7_NSTS_MASK_REV80)) >> PRXS_REM7_NSTS_SHIFT_REV80) : 0))

/* RU ALLOC present in phyrxs_rem[7]- byte 26; bit position [6:0] */
#define D11PPDU_RU_ALLOC(rxh, corerev, corerev_minor)                                             \
	(AUTO_PHYRXSTS_ENAB() ? APRXS_AX_RUALLOC(rxh, corerev, corerev_minor) :                   \
	(D11REV_GE(corerev, 80) ? ((D11RXHDR_GE80_ACCESS_VAL(rxh, phyrxs_rem[7]) &                \
	(PRXS_REM7_RU_ALLOC_MASK_REV80)) >> PRXS_REM7_RU_ALLOC_SHIFT_REV80) : 0)

/* PHY RX status "Frame Type" field mask. */
#define PRXS_FT_MASK(corerev)                                                                     \
	(D11REV_GE(corerev, 80) ? (PRXS0_FT_MASK_REV_GE80) :                                      \
	(PRXS0_FT_MASK_REV_LT80))

/**
 * ACPHY PhyRxStatus0 SubBand (FinalBWClassification) bit defs
 * FinalBWClassification is a 4 bit field, each bit representing one 20MHz sub-band
 * of a channel.
 */
enum prxs_subband {
	PRXS_SUBBAND_20LL = 0x0001,
	PRXS_SUBBAND_20LU = 0x0002,
	PRXS_SUBBAND_20UL = 0x0004,
	PRXS_SUBBAND_20UU = 0x0008,
	PRXS_SUBBAND_40L  = 0x0003,
	PRXS_SUBBAND_40U  = 0x000C,
	PRXS_SUBBAND_80   = 0x000F,
	PRXS_SUBBAND_20LLL = 0x0001,
	PRXS_SUBBAND_20LLU = 0x0002,
	PRXS_SUBBAND_20LUL = 0x0004,
	PRXS_SUBBAND_20LUU = 0x0008,
	PRXS_SUBBAND_20ULL = 0x0010,
	PRXS_SUBBAND_20ULU = 0x0020,
	PRXS_SUBBAND_20UUL = 0x0040,
	PRXS_SUBBAND_20UUU = 0x0080,
	PRXS_SUBBAND_40LL = 0x0003,
	PRXS_SUBBAND_40LU = 0x000c,
	PRXS_SUBBAND_40UL = 0x0030,
	PRXS_SUBBAND_40UU = 0x00c0,
	PRXS_SUBBAND_80L = 0x000f,
	PRXS_SUBBAND_80U = 0x00f0,
	PRXS_SUBBAND_160 = 0x00ff
};

enum prxs_subband_bphy {
	PRXS_SUBBAND_BPHY_20L = 0x0000,
	PRXS_SUBBAND_BPHY_20U = 0x0001
};

/* ACPHY Gen2 RxStatus defs */

/* ACPHY Gen2 PhyRxStatus_0: */
#define PRXS0_ACPHY2_MUPPDU     0x1000	/**< 0: SU PPDU; 1: MU PPDU */
#define PRXS0_ACPHY2_OBSS       0xE000	/**< OBSS mitigation state */

/* ACPHY Gen2 PhyRxStatus_1: */
#define PRXS1_ACPHY2_SUBBAND_MASK 0xFF00  /**< FinalBWClassification:
	                                   * 8-bit bitfield of sub-bands occupied by Rx frame
	                                   */
#define PRXS1_ACPHY2_SUBBAND_SHIFT     8

/* ACPHY Gen2 PhyRxStatus_2: */
#define PRXS2_ACPHY2_MU_INT     0x003F	/**< MU interference processing type */

/* ACPHY Gen2 PhyRxStatus_5: */
#define PRXS5_ACPHY2_RSSI_FRAC  0xFF00	/**< RSSI fractional bits */

/* ucode RxStatus1: */
#define	RXS_BCNSENT		0x8000
#define	RXS_TOFINFO		0x4000		/**< Rxed measurement frame processed by ucode */
#define	RXS_GRANTBT		0x2000		/* Indicate medium given to BT */
#define	RXS_SECKINDX_MASK_GE64	0x1fe0
#define	RXS_SECKINDX_MASK	0x07e0
#define RXS_IS_DEFRAG		0x4
#define RXS_DEFRAG_SHIFT	2
#define	RXS_SECKINDX_SHIFT	5
#define	RXS_DECERR		(1 << 4)
#define	RXS_DECATMPT		(1 << 3)
#define	RXS_PBPRES		(1 << 2)	/**< PAD bytes to make IP data 4 bytes aligned */
#define	RXS_RESPFRAMETX		(1 << 1)
#define	RXS_FCSERR		(1 << 0)

/* ucode RxStatus2: */
#define RXS_AMSDU_MASK		1
#define RXS_AGGTYPE_MASK	0x6
#define RXS_AGGTYPE_SHIFT	1
#define RXS_AMSDU_FIRST		1
#define RXS_AMSDU_INTERMEDIATE	0
#define RXS_AMSDU_LAST		2
#define RXS_AMSDU_N_ONE		3
#define RXS_TKMICATMPT		(1 << 3)
#define RXS_TKMICERR		(1 << 4)
#define RXS_PHYRXST_PRISEL_CLR	(1 << 5)	/**< PR113291: When '1', Indicates that the Rx	*/
						/* packet was received while the antenna	*/
						/* (prisel) had been granted to BT.		*/
#define RXS_PHYRXST_VALID	(1 << 8)
#define RXS_BCNCLSG		(1 << 9)	/**< Coleasced beacon packet */
#define RXS_RXANT_MASK		0x3
#define RXS_RXANT_SHIFT_LT80	12
#define RXS_RXANT_SHIFT_GE80	5
#define RXS_LOOPBACK_MODE	4

/* Bit definitions for MRXS word for short rx status. */
/* RXSS = RX Status Short */
#define RXSS_AMSDU_MASK         1	/**< 1: AMSDU */
#define RXSS_AGGTYPE_MASK     0x6	/**< 0 intermed, 1 first, 2 last, 3 single/non-AMSDU */
#define	RXSS_AGGTYPE_SHIFT      1
#define RXSS_PBPRES       (1 << 3)	/**< two-byte PAD prior to plcp */
#define RXSS_HDRSTS       (1 << 4)	/**< header conversion status. 1 enabled, 0 disabled */
#define RXSS_RES_MASK        0xE0	/**< reserved */
#define RXSS_MSDU_CNT_MASK 0xFF00	/**< index of this AMSDU sub-frame in the AMSDU */
#define RXSS_MSDU_CNT_SHIFT     8

/* RX signal control definitions */
/** PHYRXSTAUS validity checker; in-between ampdu, or rxs status isn't valid */
#define PRXS_IS_VALID(rxh, rev, rev_min)                                       \
	((D11REV_GE(rev, 80) && \
		(D11RXHDR_ACCESS_VAL(rxh, rev, rev_min, dma_flags) &           \
			RXS_PHYRXST_VALID_REV_GE80)) || \
	(D11REV_GE(rev, 64) && !(D11RXHDR_ACCESS_VAL(rxh,                      \
			rev, rev_min, dma_flags) & RXS_SHORT_MASK)) || \
	(D11RXHDR_ACCESS_VAL(rxh, rev, rev_min, RxStatus2) & RXS_PHYRXST_VALID))

/* RxChan */
#define RXS_CHAN_40		0x1000
#define RXS_CHAN_5G		0x0800
#define	RXS_CHAN_ID_MASK	0x07f8
#define	RXS_CHAN_ID_SHIFT	3

#define C_BTCX_AGGOFF_BLE		(1 << 0)
#define C_BTCX_AGGOFF_A2DP		(1 << 1)
#define C_BTCX_AGGOFF_PER		(1 << 2)
#define C_BTCX_AGGOFF_MULTIHID		(1 << 3)
#define C_BTCX_AGG_LMT_SET_HIGH		(1 << 4)
#define C_BTCX_AGGOFF_ESCO_SLAVE	(1 << 5)

#define BTCX_HFLG_NO_A2DP_BFR		(1 << 0) /**< no check a2dp buffer */
#define BTCX_HFLG_NO_CCK		(1 << 1) /**< no cck rate for null or cts2self */
#define BTCX_HFLG_NO_OFDM_FBR		(1 << 2) /**< no ofdm fbr for null or cts2self */
#define	BTCX_HFLG_NO_INQ_DEF		(1 << 3) /**< no defer inquery */
#define	BTCX_HFLG_GRANT_BT		(1 << 4) /**< always grant bt */
#define BTCX_HFLG_ANT2WL		(1 << 5) /**< force prisel to wl */
#define BTCX_HFLG_PS4ACL		(1 << 7) /**< use ps null for unsniff acl */
#define BTCX_HFLG_DYAGG			(1 << 8) /**< dynamic tx aggregation */
#define BTCX_HFLG_SKIPLMP		(1 << 10) /**< no LMP check for 4331 (w 20702 A1/A3) */
#define BTCX_HFLG_ACL_BSD_BLE_SCAN_GRNT	(1 << 14) /**< ACL based grant for BLE scan */
						/* indication to ucode */
#define BTCX_HFLG2_TRAP_RFACTIVE		(1 << 0) /* trap when RfActive too long */
#define BTCX_HFLG2_TRAP_TXCONF		(1 << 1) /* trap when coex grants txconf late */
#define BTCX_HFLG2_TRAP_ANTDLY		(1 << 2) /* trap when coex grants antdly late */
#define BTCX_HFLG2_TRAP_BTTYPE		(1 << 3) /* trap when illegal BT tasktype receive */
/* Bit definitions for M_BTCX_CONFIG */
#define BTCX_CONFIG_FORCE_TRAP		(1 << 13) /* Force a specific BTCoex TRAP when set */

/* BTCX_CONFIG bits */
#define C_BTCX_CONFIG_SLOTTED_STATE_1	(1 << 3)
#define C_BTCX_CONFIG_SLOTTED_STATE_2	(1 << 4)
#define C_BTCX_CONFIG_SLOTTED_STATE_3	(1 << 5)
#define	C_BTCX_CONFIG_LOW_RSSI		(1 << 7)
#define C_BTCX_CONFIG_BT_STROBE		(1 << 9)
#define C_BTCX_CONFIG_SCO_PROT		(1 << 10)
#define C_BTCX_CFG_CMN_CTS2SELF		(1 << 11)
#define C_BTCX_CONFIG_HPP_STATE		(1 << 15)

#define BTC_PARAMS_FW_START_IDX		1000	/**< starting index of FW only btc params */
/** BTC_PARAMS_FW definitions */
typedef enum
{
	// allow rx-agg to be re-enabled after SCO session completes
	BTC_FW_RX_REAGG_AFTER_SCO	= BTC_PARAMS_FW_START_IDX,
	// RSSI threshold at which SCO grant/deny limits are changed dynamically
	BTC_FW_RSSI_THRESH_SCO		= BTC_PARAMS_FW_START_IDX + 1,
	// Enable the dynamic LE scan priority
	BTC_FW_ENABLE_DYN_LESCAN_PRI	= BTC_PARAMS_FW_START_IDX + 2,
	// If Tput(mbps) is above this, then share antenna with BT's LE_SCAN packet type.
	BTC_FW_LESCAN_LO_TPUT_THRESH	= BTC_PARAMS_FW_START_IDX + 3,
	// If Tput(mbps) is below this, then share antenna with BT's LE_SCAN packet type.
	// sampled once a second.
	BTC_FW_LESCAN_HI_TPUT_THRESH	= BTC_PARAMS_FW_START_IDX + 4,
	// Numbers of denials before granting LS scans
	BTC_FW_LESCAN_GRANT_INT		= BTC_PARAMS_FW_START_IDX + 5,
	// number of times algorighm changes lescn pri
	BTC_FW_LESCAN_ALG_CNT		= BTC_PARAMS_FW_START_IDX + 6,
	// RSSI threshold at which aggregation will be disabled during frequent BLE activity
	BTC_FW_RSSI_THRESH_BLE		= BTC_PARAMS_FW_START_IDX + 7,
	// AMPDU Aggregation state requested by BTC
	BTC_FW_AGG_STATE_REQ		= BTC_PARAMS_FW_START_IDX + 8,
	// Reserving space for parameters used in other projects
	BTC_FW_RSVD_1			= BTC_PARAMS_FW_START_IDX + 9,
	BTC_FW_HOLDSCO_LIMIT		= BTC_PARAMS_FW_START_IDX + 10,	// Lower Limit
	BTC_FW_HOLDSCO_LIMIT_HI		= BTC_PARAMS_FW_START_IDX + 11,	// Higher Limit
	BTC_FW_SCO_GRANT_HOLD_RATIO	= BTC_PARAMS_FW_START_IDX + 12,	// Low Ratio
	BTC_FW_SCO_GRANT_HOLD_RATIO_HI	= BTC_PARAMS_FW_START_IDX + 13,	// High Ratio
	BTC_FW_HOLDSCO_HI_THRESH	= BTC_PARAMS_FW_START_IDX + 14,	// BT Period Threshold
	BTC_FW_MOD_RXAGG_PKT_SZ_FOR_SCO	= BTC_PARAMS_FW_START_IDX + 15,
	/* Modify Rx Aggregation size when SCO/eSCO detected */
	BTC_FW_AGG_SIZE_LOW	= BTC_PARAMS_FW_START_IDX + 16,
	/* Agg size when BT period < 7500 ms */
	BTC_FW_AGG_SIZE_HIGH	= BTC_PARAMS_FW_START_IDX + 17,
	/* Agg size when BT period >= 7500 ms */
	BTC_FW_MOD_RXAGG_PKT_SZ_FOR_A2DP = BTC_PARAMS_FW_START_IDX + 18,
	/* Enable COEX constraints for TWT scheduling */
	BTC_FW_TWT_COEX_CONSTRAINTS_EN = BTC_PARAMS_FW_START_IDX + 19,
	/* Enable Rx Aggregation for P2P_GO and SOFTAP when ACL/A2DP detected */
	BTC_FW_MOD_RXAGG_PKT_SZ_FOR_APMODE_ACL_A2DP = BTC_PARAMS_FW_START_IDX + 20,
	/* Disable amsdu dynamicaly during Rx limited aggregation */
	BTC_FW_DISABLE_AMSDU_DURING_LIM_AGG = BTC_PARAMS_FW_START_IDX + 21,
	/* Enable acl based grant for ble scan based on number of 2G slots */
	BTC_FW_ENABLE_ACL_GRNT_FOR_BLE_SCAN = BTC_PARAMS_FW_START_IDX + 22,
	/* Threshold slot count for 2g band to Enable acl based grant for ble scan during NAN */
	BTC_FW_NAN_THRESHOLD_SLOTS_FOR_2G = BTC_PARAMS_FW_START_IDX + 23,
	/*  BT task bm override for critical chansw slots */
	BTC_FW_CHANSW_CRT_OVR_BTTASK_BM_L	= BTC_PARAMS_FW_START_IDX + 24,
	BTC_FW_CHANSW_CRT_OVR_BTTASK_BM_H	= BTC_PARAMS_FW_START_IDX + 25,
	/* Limited Aggr AP check grace period, # of BTC watchdog timeout */
	BTC_FW_AGG_AP_GRACE_PERIOD		= BTC_PARAMS_FW_START_IDX + 26,
	/* Limited Aggr AP check buffer limit, sample interval, # of BTC watchdog timeout */
	BTC_FW_AGG_AP_BUFLIM_SMPLINTV		= BTC_PARAMS_FW_START_IDX + 27,
	/* Limited Aggr AP check excessive DELBA, sample interval, # of BTC watchdog timeout */
	BTC_FW_AGG_AP_DELBA_SMPLINTV		= BTC_PARAMS_FW_START_IDX + 28,
	/* Limited Aggr AP check excessive DELBA, threshold, # of DELBA */
	BTC_FW_AGG_AP_DELBA_THRESHOLD		= BTC_PARAMS_FW_START_IDX + 29,
	BTC_FW_MAX_INDICES			// Maximum number of btc_fw sw registers
} btcParamsFirmwareDefinitions;

#define BTC_FW_NUM_INDICES		(BTC_FW_MAX_INDICES - BTC_PARAMS_FW_START_IDX)

// 1: Re-enable aggregation after SCO
#define BTC_FW_RX_REAGG_AFTER_SCO_INIT_VAL	1

// 1: Enable limited aggregation for SCO
#define BTC_FW_MOD_RXAGG_PKT_SZ_FOR_SCO_INIT_VAL	0

/* Enable Limited aggregation for HI interval BT periodic task only (>=7.5ms) */
#ifdef WL_BTC_LIMAGG_HI_INT
/* RX aggregation packet size when SCO */
#define BTC_FW_AGG_SIZE_LOW_INIT_VAL			0
#else
/* RX aggregation packet size when SCO */
#define BTC_FW_AGG_SIZE_LOW_INIT_VAL			1
#endif

/* aggregation size when BT period < BT_AMPDU_RESIZE_THRESH */
#define BTC_FW_AGG_SIZE_HIGH_INIT_VAL			2
/* aggregation size when BT period > BT_AMPDU_RESIZE_THRESH */
// 0: disable weak-rssi SCO coex feature. If > 0, adjust SCO COEX algorithm for weak RSSI scenario.
#define BTC_FW_RSSI_THRESH_SCO_INIT_VAL			0

// 1: Enable limited aggregation for A2DP
#define BTC_FW_MOD_RXAGG_PKT_SZ_FOR_A2DP_INIT_VAL	0

// Enable LE Scan Priority Algorithm  0: Disable, 1: Enable
#define BTC_FW_ENABLE_DYN_LESCAN_PRI_INIT_VAL	0
// If WL Tput below 7 mbps, don't grant background LE Scans
#define BTC_FW_LESCAN_LO_TPUT_THRESH_INIT_VAL	7
// If WL Tput above 30 mbps, don't grant background LE Scans
#define BTC_FW_LESCAN_HI_TPUT_THRESH_INIT_VAL	30
// If LE Priority algorithm is triggered, grant one out of 2 LE_SCAN requests
#define BTC_FW_LESCAN_GRANT_INT_INIT_VAL	2
// If RSSI is weaker than -70 dBm and BLE activity is frequent, then disable
// RX aggregation, and clamp TX aggregation.
#ifdef WL_BTCX_UDM
#define	BTC_FW_RSSI_THRESH_BLE_INIT_VAL		100
#else
#define	BTC_FW_RSSI_THRESH_BLE_INIT_VAL		70
#endif
#define	BTC_FW_HOLDSCO_LIMIT_INIT_VAL		100
#define	BTC_FW_HOLDSCO_LIMIT_HI_INIT_VAL	10
#define	BTC_FW_SCO_GRANT_HOLD_RATIO_INIT_VAL	1500
#define	BTC_FW_SCO_GRANT_HOLD_RATIO_HI_INIT_VAL	1000
#define	BTC_FW_HOLDSCO_HI_THRESH_INIT_VAL	7400
#define BTC_FW_TWT_COEX_CONSTRAINTS_EN_INIT_VAL	1
/* Aggregation in AP mode (P2P_GO and SOFTAP) when ACL and A2DP  */
#define BTC_FW_MOD_RXAGG_PKT_SZ_FOR_APMODE_ACL_A2DP_INIT_VAL 16
/* Disable amsdu dynamicaly during Rx limited aggregation */
#define BTC_FW_DISABLE_AMSDU_DURING_LIM_AGG_INIT_VAL 1
/* Enable acl based grant for ble scan based on number of 2G slots during NAN */
#define BTC_FW_ENABLE_ACL_GRNT_FOR_BLE_SCAN_INIT_VAL 0
/* Threshold slot count for 2g band to Enable acl based grant for ble
 * scan during NAN. Setting current value to 8, considering time line is 512ms
 * Threshold changes dynamically based on different time line
 */
#define BTC_FW_NAN_THRESHOLD_SLOTS_FOR_2G_INIT_VAL 8
/* BT task bm override for critical chansw slots -initval */
#define BTC_FW_CHANSW_CRT_OVR_BTTASK_BM_L_INIT_VAL 0x0000
#define BTC_FW_CHANSW_CRT_OVR_BTTASK_BM_H_INIT_VAL 0x0020
#define BTC_FW_AGG_AP_GRACE_PERIOD_VAL		1
#define BTC_FW_AGG_AP_BUFLIM_SMPLINTV_VAL	1
#define BTC_FW_AGG_AP_DELBA_SMPLINTV_VAL	5
#define BTC_FW_AGG_AP_DELBA_THRESHOLD_VAL	3

/* NR Coex Params Set/Get via wl btc_params, starting index */
#define NR5GCX_PARAMS_FW_START_IDX		1200

typedef enum NR5GCX_Params {
	// Min # of PPDU to be tracked for hysteresis
	NR5GCX_FW_MIN_NUM_PPDU		= NR5GCX_PARAMS_FW_START_IDX,
	// Threshold for data stall detection, percentage
	NR5GCX_FW_DATA_STALL_TH		= NR5GCX_PARAMS_FW_START_IDX + 1,
	// max number of rate recovery attempts
	NR5GCX_FW_MAX_NUM_ATTEMPTS	= NR5GCX_PARAMS_FW_START_IDX + 2,
	// Rate recovery rate check duration
	NR5GCX_FW_RR_RATE_CHK_DUR	= NR5GCX_PARAMS_FW_START_IDX + 3,
	// Rate recovery attempt duration
	NR5GCX_FW_RR_ATTEMPT_DUR	= NR5GCX_PARAMS_FW_START_IDX + 4,
	// NR grant duration after a unsuccessful rate recovery
	NR5GCX_FW_RR_UNSC_DUR		= NR5GCX_PARAMS_FW_START_IDX + 5,
	// Threshold for rate recovery, percentage
	NR5GCX_FW_RECOVERY_TH		= NR5GCX_PARAMS_FW_START_IDX + 6,
	// Threshold for low RSSI
	NR5GCX_FW_LOWRSSI_TH		= NR5GCX_PARAMS_FW_START_IDX + 7,
	// Maximum number of nr5gcx fw params
	NR5GCX_FW_MAX_INDICES
} NR5GCXParamsFirmwareDefinitions;

#define NR5GCX_FW_NUM_INDICES		(NR5GCX_FW_MAX_INDICES - NR5GCX_PARAMS_FW_START_IDX)

#define NR5GCX_FW_MIN_NUM_PPDU_INIT		10u
#define NR5GCX_FW_DATA_STALL_TH_INIT		75u
#define NR5GCX_FW_MAX_NUM_ATTEMPTS_INIT		5u
#define NR5GCX_FW_RR_RATE_CHK_DUR_INIT_MS	60u	/* ms */
#define NR5GCX_FW_RR_ATTEMPT_DUR_INIT_MS	60u	/* ms */
#define NR5GCX_FW_RR_UNSC_DUR_INIT_MS		10000u	/* ms */
#define NR5GCX_FW_RECOVERY_TH_INIT		50u
#define NR5GCX_FW_LOWRSSI_TH_INIT		85u	/* dBm */

/* RC1 Coex Params Set/Get via wl btc_params, starting index */
#define RC1CX_PARAMS_FW_START_IDX		1200

typedef enum RC1CX_Params {
	// Min # of PPDU to be tracked for hysteresis
	RC1CX_FW_MIN_NUM_PPDU		= RC1CX_PARAMS_FW_START_IDX,
	// Threshold for data stall detection, percentage
	RC1CX_FW_DATA_STALL_TH		= RC1CX_PARAMS_FW_START_IDX + 1,
	// max number of rate recovery attempts
	RC1CX_FW_MAX_NUM_ATTEMPTS	= RC1CX_PARAMS_FW_START_IDX + 2,
	// Rate recovery rate check duration
	RC1CX_FW_RR_RATE_CHK_DUR	= RC1CX_PARAMS_FW_START_IDX + 3,
	// Rate recovery attempt duration
	RC1CX_FW_RR_ATTEMPT_DUR	= RC1CX_PARAMS_FW_START_IDX + 4,
	// NR grant duration after a unsuccessful rate recovery
	RC1CX_FW_RR_UNSC_DUR		= RC1CX_PARAMS_FW_START_IDX + 5,
	// Threshold for rate recovery, percentage
	RC1CX_FW_RECOVERY_TH		= RC1CX_PARAMS_FW_START_IDX + 6,
	// Threshold for low RSSI
	RC1CX_FW_LOWRSSI_TH		= RC1CX_PARAMS_FW_START_IDX + 7,
	// Maximum number of rc1cx fw params
	RC1CX_FW_MAX_INDICES
} RC1CXParamsFirmwareDefinitions;

#define RC1CX_FW_NUM_INDICES		(RC1CX_FW_MAX_INDICES - RC1CX_PARAMS_FW_START_IDX)

#define RC1CX_FW_MIN_NUM_PPDU_INIT		10u
#define RC1CX_FW_DATA_STALL_TH_INIT		75u
#define RC1CX_FW_MAX_NUM_ATTEMPTS_INIT		5u
#define RC1CX_FW_RR_RATE_CHK_DUR_INIT_MS	60u	/* ms */
#define RC1CX_FW_RR_ATTEMPT_DUR_INIT_MS	60u	/* ms */
#define RC1CX_FW_RR_UNSC_DUR_INIT_MS		10000u	/* ms */
#define RC1CX_FW_RECOVERY_TH_INIT		50u
#define RC1CX_FW_LOWRSSI_TH_INIT		85u	/* dBm */

#ifdef GPIO_TXINHIBIT
/* GPIO based TX_INHIBIT:SWWLAN-109270 */
typedef enum shm_macintstatus_ext_e {
	C_MISE_GPIO_TXINHIBIT_VAL_NBIT	= 0,
	C_MISE_GPIO_TXINHIBIT_INT_NBIT	= 1
} shm_macintstatus_ext_t;
#define C_MISE_GPIO_TXINHIBIT_VAL_MASK (1 << C_MISE_GPIO_TXINHIBIT_VAL_NBIT)
#define C_MISE_GPIO_TXINHIBIT_INT_MASK (1 << C_MISE_GPIO_TXINHIBIT_INT_NBIT)
#endif
#define M_PSM_SOFT_REGS 0x0

/** Scratch Reg defs */
typedef enum
{
	S_RSV0 = 0,
	S_RSV1,
	S_RSV2,

	/* scratch registers for Dot11-constants */
	S_DOT11_CWMIN,		/**< CW-minimum					0x03 */
	S_DOT11_CWMAX,		/**< CW-maximum					0x04 */
	S_DOT11_CWCUR,		/**< CW-current					0x05 */
	S_DOT11_SRC_LMT,	/**< short retry count limit			0x06 */
	S_DOT11_LRC_LMT,	/**< long retry count limit			0x07 */
	S_DOT11_DTIMCOUNT,	/**< DTIM-count					0x08 */

	/* Tx-side scratch registers */
	S_SEQ_NUM,		/**< hardware sequence number reg			0x09 */
	S_SEQ_NUM_FRAG,		/**< seq-num for frags (Set at the start os MSDU	0x0A */
	S_FRMRETX_CNT,		/**< frame retx count				0x0B */
	S_SSRC,			/**< Station short retry count			0x0C */
	S_SLRC,			/**< Station long retry count			0x0D */
	S_EXP_RSP,		/**< Expected response frame			0x0E */
	S_OLD_BREM,		/**< Remaining backoff ctr			0x0F */
	S_OLD_CWWIN,		/**< saved-off CW-cur				0x10 */
	S_TXECTL,		/**< TXE-Ctl word constructed in scr-pad		0x11 */
	S_CTXTST,		/**< frm type-subtype as read from Tx-descr	0x12 */

	/* Rx-side scratch registers */
	S_RXTST,		/**< Type and subtype in Rxframe			0x13 */

	/* Global state register */
	S_STREG,		/**< state storage actual bit maps below		0x14 */

	S_TXPWR_SUM,		/**< Tx power control: accumulator		0x15 */
	S_TXPWR_ITER,		/**< Tx power control: iteration			0x16 */
	S_RX_FRMTYPE,		/**< Rate and PHY type for frames			0x17 */
	S_THIS_AGG,		/**< Size of this AGG (A-MSDU)			0x18 */

	S_KEYINDX,		/*						0x19 */
	S_RXFRMLEN,		/**< Receive MPDU length in bytes			0x1A */

	/* Receive TSF time stored in SCR */
	S_RXTSFTMRVAL_WD3,	/**< TSF value at the start of rx			0x1B */
	S_RXTSFTMRVAL_WD2,	/**< TSF value at the start of rx			0x1C */
	S_RXTSFTMRVAL_WD1,	/**< TSF value at the start of rx			0x1D */
	S_RXTSFTMRVAL_WD0,	/**< TSF value at the start of rx			0x1E */
	S_RXSSN,		/**< Received start seq number for A-MPDU BA	0x1F */
	S_RXQOSFLD,		/**< Rx-QoS field (if present)			0x20 */

	/* Scratch pad regs used in microcode as temp storage */
	S_TMP0,			/**< stmp0					0x21 */
	S_TMP1,			/**< stmp1					0x22 */
	S_TMP2,			/**< stmp2					0x23 */
	S_TMP3,			/**< stmp3					0x24 */
	S_TMP4,			/**< stmp4					0x25 */
	S_TMP5,			/**< stmp5					0x26 */
	S_PRQPENALTY_CTR,	/**< Probe response queue penalty counter		0x27 */
	S_ANTCNT,		/**< unsuccessful attempts on current ant.	0x28 */
	S_SYMBOL,		/**< flag for possible symbol ctl frames		0x29 */
	S_RXTP,			/**< rx frame type				0x2A */
	S_STREG2,		/**< extra state storage				0x2B */
	S_STREG3,		/**< even more extra state storage		0x2C */
	S_STREG4,		/**< ...						0x2D */
	S_STREG5,		/**< remember to initialize it to zero		0x2E */

	S_UNUSED_0X2F,		/**< No longer used				0x2F */
	S_UPTR,			/* Use this to initialize utrace                0x30 */
	S_ADJPWR_IDX,		/**< PR 37101 WAR, adj_pwr_idx			0x31 */
	S_CUR_PTR,		/**< Temp pointer for A-MPDU re-Tx SHM table	0x32 */
	S_REVID4,		/**< 0x33 */
	S_INDX,			/**< 0x34 */
	S_ADDR0,		/**< 0x35 */
	S_ADDR1,		/**< 0x36 */
	S_ADDR2,		/**< 0x37 */
	S_ADDR3,		/**< 0x38 */
	S_ADDR4,		/**< 0x39 */
	S_ADDR5,		/**< 0x3A */
	S_TMP6,			/**< 0x3B */
	S_KEYINDX_BU,		/**< Backup for Key index 			0x3C */
	S_MFGTEST_TMP0,		/**< Temp register used for RX test calculations	0x3D */
	S_RXESN,		/**< Received end sequence number for A-MPDU BA	0x3E */
	S_STREG6,		/**< 0x3F */
} ePsmScratchPadRegDefinitions;

#define C_STREG_SLOWCAL_PD_NBIT 0x00000004        /* BIT 2 slow clock cal is pending */
#define C_STREG_SLOWCAL_DN_NBIT 0x00000008        /* BIT 3 slow clock cal is done */

#define S_BEACON_INDX	S_OLD_BREM
#define S_PRS_INDX	S_OLD_CWWIN
#define S_BTCX_BT_DUR	S_REVID4
#define S_PHYTYPE	S_SSRC
#define S_PHYVER	S_SLRC

/* IHR GPT_2 is corerev >= 3 */
#define TSF_GPT_2_STAT		0x133
#define TSF_GPT_2_CTR_L		0x134
#define TSF_GPT_2_CTR_H		0x135
#define TSF_GPT_2_VAL_L		0x136
#define TSF_GPT_2_VAL_H		0x137

/* IHR TSF_GPT STAT values */
#define TSF_GPT_PERIODIC	(1 << 12)
#define TSF_GPT_ADJTSF		(1 << 13)
#define TSF_GPT_USETSF		(1 << 14)
#define TSF_GPT_ENABLE		(1 << 15)

/** ucode mac statistic counters in shared memory */
#define MACSTAT_OFFSET_SZ 64
#define MACSTAT_REV80_OFFSET_SZ 118

/* ucode macstat txfunflw offset */
#define UCODEMSTAT_TXFUNFL_BLK	((0x70 * 2) + (0x76 * 2))

/* MACSTAT offset to SHM address */
#define MACSTAT_ADDR(x, offset) (M_PSM2HOST_STATS(x) + (offset))

/** ucode mac statistic counters in shared memory, base addr defined in M_UCODE_MACSTAT1 */
typedef struct macstat1 {
	uint16 txndpa;                  /* + 0 (0x0) */
	uint16 txndp;                   /* + 1*2 (0x2) */
	uint16 txsf;                    /* + 2*2 (0x4) */
	uint16 txcwrts;                 /* + 3*2 (0x6) */
	uint16 txcwcts;                 /* + 4*2 (0x8) */
	uint16 txbfm;                   /* + 5*2 (0xa) */
	uint16 rxndpaucast;             /* + 6*2 (0xc) */
	uint16 bferptrdy;               /* + 7*2 (0xe) */
	uint16 rxsfucast;               /* + 8*2 (0x10) */
	uint16 rxcwrtsucast;            /* + 9*2 (0x12) */
	uint16 rxcwctsucast;            /* +10*2 (0x14) */
	uint16 rx20s;                  /* +11*2 (0x16) */
	uint16 bcntrim;                  /* +12*2 (0x18) */
	uint16 btc_rfact_l;             /* +13*2 (0x1a) */
	uint16 btc_rfact_h;             /* +14*2 (0x1c) */
	uint16 btc_txconf_l;            /* +15*2 (0x1e) : cnt */
	uint16 btc_txconf_h;            /* +16*2 (0x20) : cnt */
	uint16 btc_txconf_durl;         /* +17*2 (0x22) : dur */
	uint16 btc_txconf_durh;         /* +18*2 (0x24) : dur */
	uint16 rxsecrssi0;              /* +19*2 (0x26) : high bin */
	uint16 rxsecrssi1;              /* +20*2 (0x28) : med bin */
	uint16 rxsecrssi2;              /* +21*2 (0x2a) : low bin */
	uint16 rxpri_durl;              /* +22*2 (0x2c) : dur */
	uint16 rxpri_durh;              /* +23*2 (0x2e) : dur */
	uint16 rxsec20_durl;            /* +24*2 (0x30) : dur */
	uint16 rxsec20_durh;            /* +25*2 (0x32) : dur */
	uint16 rxsec40_durl;            /* +26*2 (0x34) : dur */
	uint16 rxsec40_durh;            /* +27*2 (0x36) : dur */
} macstat1_t;

#define MX_UCODEX_MACSTAT (0x40 * 2)
/* ucodex mac statistic counters in shared memory */
#define MACXSTAT_OFFSET_SZ 6

/* psm2 statistic counters in shared memory, base addr defined in MX_PSM2HOST_STATS */
typedef enum {
	MCXSTOFF_MACXSUSP = 0,
	MCXSTOFF_M2VMSG = 1,
	MCXSTOFF_V2MMSG = 2,
	MCXSTOFF_MBOXOUT = 3,
	MCXSTOFF_MUSND = 4,
	MCXSTOFF_SFB2V = 5
} macxstat_offset_t;

/* dot11 core-specific control flags */
#define SICF_MCLKE		0x0001          /* Mac core clock Enable */
#define SICF_FCLKON		0x0002          /* Force clocks On */
#define	SICF_PCLKE		0x0004		/**< PHY clock enable */
#define	SICF_PRST		0x0008		/**< PHY reset */
#define	SICF_MPCLKE		0x0010		/**< MAC PHY clockcontrol enable */
#define	SICF_FREF		0x0020		/**< PLL FreqRefSelect (corerev >= 5) */
/* NOTE: the following bw bits only apply when the core is attached
 * to a NPHY (and corerev >= 11 which it will always be for NPHYs).
 */
#ifdef SICF_160M_BWMASK_DEF
#define	SICF_BWMASK(macrev)	(D11REV_GE(macrev, 86) ? 0x00e0 : 0x00c0)	/**< phy clkmsk */
#define	SICF_BW160(macrev)	(D11REV_GE(macrev, 86) ? 0x0080 : 0x00c0)	/**< 160MHz BW */
#define	SICF_BW80(macrev)	(D11REV_GE(macrev, 86) ? 0x0060 : 0x00c0)	/**< 80MHz BW */
#define	SICF_BW40(macrev)	(D11REV_GE(macrev, 86) ? 0x0040 : 0x0080)	/**< 40MHz BW */
#define	SICF_BW20(macrev)	(D11REV_GE(macrev, 86) ? 0x0020 : 0x0040)	/**< 20MHz BW */
#define	SICF_BW10(macrev)	(D11REV_GE(macrev, 86) ? 0x0000 : 0x0000)	/**< 10MHz BW */
#else
#define	SICF_BWMASK		0x00c0		/**< phy clock mask (b6 & b7) */
#define	SICF_BW160		0x00c0		/**< 160MHz BW */
#define	SICF_BW80		0x00c0		/**< 80MHz BW */
#define	SICF_BW40		0x0080		/**< 40MHz BW (160MHz phyclk) */
#define	SICF_BW20		0x0040		/**< 20MHz BW (80MHz phyclk) */
#define	SICF_BW10		0x0000		/**< 10MHz BW (40MHz phyclk) */
#endif
#define	SICF_DAC		0x0300		/**< Highspeed DAC mode control field */
#define	SICF_GMODE		0x2000		/**< gmode enable */

/* Macmode / Phymode / Opmode are used interchangebly sometimes
 * even though they all mean the same. Going ahead with the HW
 * signal name - using phymode here on (even though we know its
 * a misnomer). Applicable to d11 corerev >= 50 ---- ACPHY only
 */
#define SICF_PHYMODE_SHIFT	16
#define	SICF_PHYMODE		0xf0000		/**< mask */

#define SICF_160CLKSEL		0x100000u	/* main phy clock speed selection */

/* dot11 core-specific status flags */
#define	SISF_2G_PHY		0x0001		/**< 2.4G capable phy (corerev >= 5) */
#define	SISF_5G_PHY		0x0002		/**< 5G capable phy (corerev >= 5) */
#define	SISF_FCLKA		0x0004		/**< FastClkAvailable (corerev >= 5) */
#define	SISF_DB_PHY		0x0008		/**< Dualband phy (corerev >= 11) */

/* === End of MAC reg, Beginning of PHY(b/a/g/n) reg, radio and LPPHY regs are separated === */

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

typedef struct shm_mbss_prq_entry_s shm_mbss_prq_entry_t;
BWL_PRE_PACKED_STRUCT struct shm_mbss_prq_entry_s {
	struct ether_addr ta;
	uint8 prq_info[2];
	uint8 time_stamp;
	uint8 flags;	/**< bit 0 HT STA Indication, bit 7:1 Reserved */
} BWL_POST_PACKED_STRUCT;

typedef enum shm_mbss_prq_ft_e {
	SHM_MBSS_PRQ_FT_CCK,
	SHM_MBSS_PRQ_FT_OFDM,
	SHM_MBSS_PRQ_FT_MIMO,
	SHM_MBSS_PRQ_FT_RESERVED
} shm_mbss_prq_ft_t;

#define SHM_MBSS_PRQ_FT_COUNT SHM_MBSS_PRQ_FT_RESERVED

#define SHM_MBSS_PRQ_ENT_FRAMETYPE(entry)      ((entry)->prq_info[0] & 0x3)
#define SHM_MBSS_PRQ_ENT_UPBAND(entry)         ((((entry)->prq_info[0] >> 2) & 0x1) != 0)

/** What was the index matched? */
#define SHM_MBSS_PRQ_ENT_UC_BSS_IDX(entry)     (((entry)->prq_info[0] >> 2) & 0x3)
#define SHM_MBSS_PRQ_ENT_PLCP0(entry)          ((entry)->prq_info[1])

/** Was this directed to a specific SSID or BSSID? If bit clear, quantity known */
#define SHM_MBSS_PRQ_ENT_DIR_SSID(entry) \
	((((entry)->prq_info[0] >> 6) == 0) || ((entry)->prq_info[0] >> 6) == 1)
#define SHM_MBSS_PRQ_ENT_DIR_BSSID(entry) \
	((((entry)->prq_info[0] >> 6) == 0) || ((entry)->prq_info[0] >> 6) == 2)

#define SHM_MBSS_PRQ_ENT_TIMESTAMP(entry)	((entry)->time_stamp)
/** Was the probe request from a ht STA or a legacy STA */
#define SHM_MBSS_PRQ_ENT_HTSTA(entry)		((entry)->flags & 0x1)

typedef struct d11ac_tso_s d11ac_tso_t;

BWL_PRE_PACKED_STRUCT struct d11ac_tso_s {
	uint8 flag[3];
	uint8 sfh_hdr_offset;
	uint16 tso_mss;		/**< tso segment size */
	uint16 msdu_siz;	/**< msdu size */
	uint32 tso_payload_siz;	/**< total byte cnt in tcp payload */
	uint16 ip_hdr_offset;	/**< relative to the start of txd header */
	uint16 tcp_hdr_offset;	/**< relative to start of txd header */
} BWL_POST_PACKED_STRUCT;

/* toe_ctl TCP offload engine register definitions */
#define TOE_CTL_DISAB		(1u << 0)
#define TOE_CTL_MASK		(1u << 0)
#define TOE_CTL_ENAB		(0xFFFEu)
#define TOE_CLK_GATING_DISAB	(1u << 1)

#define TSO_HDR_TOE_FLAG_OFFSET	(0u)

#define TOE_F0_HDRSIZ_NORMAL	(1u << 0)
#define TOE_F0_PASSTHROUGH	(1u << 1)
#define TOE_F0_TCPSEG_EN	(1u << 3)
#define TOE_F0_IPV4		(1u << 4)
#define TOE_F0_IPV6		(1u << 5)
#define TOE_F0_TCP		(1u << 6)
#define TOE_F0_UDP		(1u << 7)

#define TOE_F1_IPV4_CSUM_EN	(1u << 0)
#define TOE_F1_TCPUDP_CSUM_EN	(1u << 1)
#define TOE_F1_PSEUDO_CSUM_EN	(1u << 2)
#define TOE_F1_FRAG_ALLOW	(1u << 5)
#define TOE_F1_FRAMETYPE_1	(1u << 6)
#define TOE_F1_FRAMETYPE_2	(1u << 7)
#define TOE_F1_FT_MASK		(TOE_F1_FRAMETYPE_1 | TOE_F1_FRAMETYPE_2)
#define TOE_F1_FT_SHIFT		(6u)

#define TOE_F2_TXD_HEAD_SHORT	(1u << 0)
#define TOE_F2_EPOCH_SHIFT	(1u)
#define TOE_F2_EPOCH		(1u << TOE_F2_EPOCH_SHIFT)
#define TOE_F2_EPOCH_EXT	(1u << 2)
#define TOE_F2_EPOCH_EXT_MASK	(TOE_F2_EPOCH | TOE_F2_EPOCH_EXT)
#define TOE_F2_AMSDU_AGGR_EN	(1u << 4)
#define TOE_F2_AMSDU_CSUM_EN	(1u << 5)
#define TOE_F2_AMSDU_FS_MID	(1u << 6)
#define TOE_F2_AMSDU_FS_LAST	(1u << 7)

#define TOE_TXDMA_FLAGS_AMSDU_FIRST	(0x14u)
#define TOE_TXDMA_FLAGS_AMSDU_MID	(0x24u)
#define TOE_TXDMA_FLAGS_AMSDU_LAST	(0x34u)

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#define SHM_BYT_CNT	0x2			/**< IHR location */
#define MAX_BYT_CNT	0x600			/**< Maximum frame len */

/* WOWL Template Regions */
#define WOWL_NS_CHKSUM		 (0x57 * 2)
#define WOWL_PSP_TPL_BASE   (0x334 * 2)
#define WOWL_GTK_MSG2             (0x434 * 2)
#define WOWL_NS_OFFLOAD     (0x634 * 2)
#define T_KEEPALIVE_0       (0x6b4 * 2)
#define T_KEEPALIVE_1       ((0x6b4 + 0x40) * 2)
#define WOWL_ARP_OFFLOAD    (0x734 * 2)
#define WOWL_TX_FIFO_TXRAM_BASE (0x774 * 2)	/**< conservative, leave 1KB for GTKM2 */

/* template regions for 11ac */
#define D11AC_WOWL_PSP_TPL_BASE   (0x4c0 * 2)
#define D11AC_WOWL_GTK_MSG2       (0x5c0 * 2)	/**< for core rev >= 42 */
#define WOWL_NS_OFFLOAD_GE42	 (0x7c0 * 2)
#define T_KEEPALIVE_0_GE42       (0x840 * 2)
#define T_KEEPALIVE_1_GE42       ((0x840 + 0x40) * 2)
#define WOWL_ARP_OFFLOAD_GE42    (0x8c0 * 2)
#define D11AC_WOWL_TX_FIFO_TXRAM_BASE   (0x900 * 2)	/**< GTKM2 for core rev >= 42 */

/* Event definitions */
#define WOWL_MAGIC       (1 << 0)	/**< Wakeup on Magic packet */
#define WOWL_NET         (1 << 1)	/**< Wakeup on Netpattern */
#define WOWL_DIS         (1 << 2)	/**< Wakeup on loss-of-link due to Disassoc/Deauth */
#define WOWL_RETR        (1 << 3)	/**< Wakeup on retrograde TSF */
#define WOWL_BCN         (1 << 4)	/**< Wakeup on loss of beacon */
#define WOWL_TST         (1 << 5)	/**< Wakeup after test */
#define WOWL_M1          (1 << 6)	/**< Wakeup after PTK refresh */
#define WOWL_EAPID       (1 << 7)	/**< Wakeup after receipt of EAP-Identity Req */
#define WOWL_PME_GPIO    (1 << 8)	/**< Wakeind via PME(0) or GPIO(1) */
#define WOWL_NEEDTKIP1   (1 << 9)	/**< need tkip phase 1 key to be updated by the driver */
#define WOWL_GTK_FAILURE (1 << 10)	/**< enable wakeup if GTK fails */
#define WOWL_EXTMAGPAT   (1 << 11)	/**< support extended magic packets */
#define WOWL_ARPOFFLOAD  (1 << 12)	/**< support ARP/NS offloading */
#define WOWL_WPA2        (1 << 13)	/**< read protocol version for EAPOL frames */
#define WOWL_KEYROT      (1 << 14)	/**< If the bit is set, use key rotaton */
#define WOWL_BCAST       (1 << 15)	/**< If the bit is set, frm received was bcast frame */

#define MAXBCNLOSS (1 << 13) - 1	/**< max 12-bit value for bcn loss */

/* UCODE shm view:
 * typedef struct {
 *         uint16 offset; // byte offset
 *         uint16 patternsize; // the length of value[.] in bytes
 *         uchar bitmask[MAXPATTERNSIZE/8]; // 16 bytes, the effect length is (patternsize+7)/8
 *         uchar value[MAXPATTERNSIZE]; // 128 bytes, the effect length is patternsize.
 *   } netpattern_t;
 */
#define NETPATTERNSIZE	(148) /* 128 value + 16 mask + 4 offset + 4 patternsize */
#define MAXPATTERNSIZE 128
#define MAXMASKSIZE	MAXPATTERNSIZE/8

/** Security Algorithm defines */
#define WOWL_TSCPN_SIZE 6
#define WOWL_TSCPN_COUNT  4			/**< 4 ACs */
#define WOWL_TSCPN_BLK_SIZE	(WOWL_TSCPN_SIZE * WOWL_TSCPN_COUNT)

#define	WOWL_SECSUITE_GRP_ALGO_MASK		0x0007
#define	WOWL_SECSUITE_GRP_ALGO_SHIFT	0
#define	WOWL_SECSUITE_ALGO_MASK			0x0700
#define	WOWL_SECSUITE_ALGO_SHIFT		8

#define EXPANDED_KEY_RNDS 10
#define EXPANDED_KEY_LEN  176 /* the expanded key from KEK (4*11*4, 16-byte state, 11 rounds) */

/* Organization of Template RAM is as follows
 *   typedef struct {
 *      uint8 AES_XTIME9DBE[1024];
 *	uint8 AES_INVSBOX[256];
 *	uint8 AES_KEYW[176];
 * } AES_TABLES_t;
 */
/* See dot11_firmware/diag/wmac_tcl/wmac_762_wowl_gtk_aes: proc write_aes_tables,
 *  for an example of writing those tables into the tx fifo buffer.
 */

typedef struct {
	uint16 MacTxControlLow;		/**< mac-tx-ctl-low word */
	uint16 MacTxControlHigh;	/**< mac-tx-ctl-high word */
	uint16 PhyTxControlWord;	/**< phy control word */
	uint16 PhyTxControlWord_1;	/**< extra phy control word for mimophy */
	union {
		uint16 XtraFrameTypes;	/**< frame type for RTS/FRAG fallback (used only for AES) */
		uint16 bssenc_pos;	/**< BssEnc includes key ID , for corerev >= 42 */
	} u1;
	uint8 plcp[6];			/**< plcp of template */

	uint16 mac_frmtype; /**< MAC frame type for GTK MSG2, can be
			     * dot11_data frame (0x20) or dot11_QoS_Data frame (0x22).
			     */
	uint16 frm_bytesize; /**< number of bytes in the template, it includes:
			      * PLCP, MAC header, IV/EIV, the data payload
			      * (eth-hdr and EAPOL-Key), TKIP MIC
			      */
	uint16 payload_wordoffset;	/**< the word offset of the data payload */

	/* ALIGN */
	uint16 seqnum;		/**< Sequence number for this frame */
	uint8  seciv[18]; /**< 10-byte TTAK used for TKIP, 8-byte IV/EIV.
			   * See <SecurityInitVector> in the general tx descriptor.
			   */
} wowl_templ_ctxt_t;

#define WOWL_TEMPL_CTXT_LEN 42	/**< For making sure that no PADs are needed */
#define WOWL_TEMPL_CTXT_FRMTYPE_DATA    0x2
#define WOWL_TEMPL_CTXT_FRMTYPE_QOS     0x22

/** constant tables required for AES key unwrapping for key rotation */
extern uint16 aes_invsbox[128];
extern uint16 aes_xtime9dbe[512];

#define MAX_MPDU_SPACE           (D11_TXH_LEN + 1538)

/* Bits in TXE_BMCCTL */
#define BMCCTL_INITREQ_SHIFT	0
#define BMC_CTL_DONE		(1 << BMCCTL_INITREQ_SHIFT)
#define BMCCTL_RESETSTATS_SHIFT	1
#define BMCCTL_TXBUFSIZE_SHIFT	2
#define BMCCTL_LOOPBACK_SHIFT	5
#define BMCCTL_TXBUFSZ_MASK	((1 << BMCCTL_LOOPBACK_SHIFT) - (1 << BMCCTL_TXBUFSIZE_SHIFT))
#define BMCCTL_CLKGATEEN_SHIFT  8

/* Bits in TXE_BMCConfig */
#define BMCCONFIG_BUFCNT_SHIFT		0
#define BMCCONFIG_DISCLKGATE_SHIFT	13
#define BMCCONFIG_BUFCNT_MASK	((1 << BMCCONFIG_DISCLKGATE_SHIFT) - (1 << BMCCONFIG_BUFCNT_SHIFT))

/* Bits in TXE_BMCStartAddr */
#define BMCSTARTADDR_STRTADDR_MASK	0x3ff

/* Bits in TXE_BMCDescrLen */
#define BMCDescrLen_ShortLen_SHIFT	0
#define BMCDescrLen_LongLen_SHIFT	8

/* Bits in TXE_BMCAllocCtl */
#define BMCAllocCtl_AllocCount_SHIFT		0
/* Rev==50 || Rev>52
*	BMCAllocCtl.AllocCount [0:10]
*	BMCAllocCtl.AllocThreshold [11:14]
* !Rev50
*	BMCAllocCtl.AllocCount [0:7]
*	BMCAllocCtl.AllocThreshold [8:15]
*/
#define BMCAllocCtl_AllocThreshold_SHIFT_Rev50	11
#define BMCAllocCtl_AllocThreshold_SHIFT	8

/* Bits in TXE_BMCCmd1 */
#define BMCCMD1_TIDSEL_SHIFT		1
#define BMCCMD1_RDSRC_SHIFT		6
#define BMCCmd1_RXMapPassThru_SHIFT	12
#define BMCCMD1_BQSelNum_SHIFT		1u
#define BMCCMD1_BQSelType_SHIFT		7u
#define BMCCMD1_RDSRC_Group0		0u	/* register itself */
#define BMCCMD1_RDSRC_Group1		1u	/* staged max/min */
#define BMCCMD1_RDSRC_Group2		2u	/* staged max/previous min */
#define BMCCMD1_RDSRC_Group3		3u	/* active max/min */
#define BMCCMD1_RDSRC_SHIFT_rev80	10u
#define BMCCMD1_CoreSel_SHIFT		13u
#define BMCCMD1_CoreSel_SHIFT_rev80	15u

/* Bits in TXE_BMCCmd */
#define BMCCmd_TIDSel_SHIFT		0
#define BMCCmd_Enable_SHIFT		4
#define BMCCmd_ReleasePreAlloc_SHIFT	5
#define BMCCmd_ReleasePreAllocAll_SHIFT	6
#define BMCCmd_UpdateBA_SHIFT		7
#define BMCCmd_Consume_SHIFT		8
#define BMCCmd_Aggregate_SHIFT		9
#define BMCCmd_UpdateRetryCount_SHIFT	10
#define BMCCmd_DisableTID_SHIFT		11

#define BMCCmd_BQSelType_TX	0
#define BMCCmd_BQSelType_RX	1
#define BMCCmd_BQSelType_Templ	2

/* Bits in TXE_BMCCMD for rev >= 80 */
#define BMCCmd_BQSelType_MASK_Rev80	0x00c0
#define BMCCmd_BQSelType_SHIFT_Rev80	6
#define BMCCmd_Enable_SHIFT_rev80	8
#define BMCCmd_ReleasePreAllocAll_SHIFT_rev80	10

/* Bits in TXE_BMCCmd1 */
#define BMCCmd1_Minmaxappall_SHIFT	0
#define BMCCmd1_Minmaxlden_SHIFT	5
#define BMCCmd1_Minmaxffszlden_SHIFT	8
#define BMCCmd_Core1_Sel_MASK		0x2000

/* Bits in TXE_BMCStatCtl */
#define BMCStatCtl_TIDSel_SHIFT		0u
#define BMCStatCtl_STATSel_SHIFT	4u
#define BMCStatCtl_BQSelNum_SHIFT	0u
#define BMCStatCtl_BQSelType_SHIFT	6u
#define BMCStatCtl_STATSel_SHIFT_rev80	8u

/* Bits in BMVpConfig */
#define BMCVPConfig_SingleVpModePortA_SHIFT	4

/* Bits in TXE_PsmMSDUAccess */
#define PsmMSDUAccess_TIDSel_SHIFT	0
#define PsmMSDUAccess_MSDUIdx_SHIFT	4
#define PsmMSDUAccess_ReadBusy_SHIFT	14
#define PsmMSDUAccess_WriteBusy_SHIFT	15

/* Bits in TXE_PsmMSDUAccess for rev >= 80 */
#define PsmMSDUAccess_BQSelType_SHIFT	5
#define PsmMSDUAccess_MSDUIdx_SHIFT_rev80	7
#define PsmMSDUAccess_BQSelType_Templ	2
#define PsmMSDUAccess_BQSelType_TX	0

#ifdef WLRSDB
#define MAX_RSDB_MAC_NUM 2
#else
#define MAX_RSDB_MAC_NUM 1
#endif
#define MAX_MIMO_MAC_NUM 1

#ifdef WL_SCAN_CORE
#define MAX_MAC_CORE_NUM	(MAX_RSDB_MAC_NUM + 1)
#else
#define MAX_MAC_CORE_NUM	(MAX_RSDB_MAC_NUM)
#endif /* WL_SCAN_CORE */

#define MAC_CORE_UNIT_0				0x0u /**< First mac core unit */
#define MAC_CORE_UNIT_1				0x1u /**< Second mac core unit */

/* HW unit of scan core.
 * This is used to overwrite the tunables specific to scan core
 */
#define SCAN_CORE_UNIT				0x2u

/* Supported phymodes / macmodes / opmodes */
#define SINGLE_MAC_MODE				0x0 /**< only single mac is enabled */
#define DUAL_MAC_MODE				0x1 /**< enables dual mac */
/* (JIRA: CRDOT11ACPHY-652) Following two #defines support
 * exclusive reg access to core 0/1 in MIMO mode
 */
#define SUPPORT_EXCLUSIVE_REG_ACCESS_CORE0	0x2
#define SUPPORT_EXCLUSIVE_REG_ACCESS_CORE1	0x4 /**< not functional in 4349A0 */
#define SUPPORT_CHANNEL_BONDING			0x8 /**< enables channel bonding,
						     * supported in single mac mode only
						     */
#define SCAN_CORE_ACTIVE			0x10 /* scan core enabled for background DFS */

#define PHYMODE_MIMO		(SINGLE_MAC_MODE)
#define PHYMODE_80P80		(SINGLE_MAC_MODE | SUPPORT_CHANNEL_BONDING)
#define PHYMODE_RSDB_SISO_0	(DUAL_MAC_MODE | SUPPORT_EXCLUSIVE_REG_ACCESS_CORE0)
#define PHYMODE_RSDB_SISO_1	(DUAL_MAC_MODE | SUPPORT_EXCLUSIVE_REG_ACCESS_CORE1)
#define PHYMODE_RSDB		(PHYMODE_RSDB_SISO_0 | PHYMODE_RSDB_SISO_1)
#define PHYMODE_BGDFS		31
#define PHYMODE_3x3_1x1		31

#define RX_INTR_FIFO_0		0x1		/**< FIFO-0 interrupt */
#define RX_INTR_FIFO_1		0x2		/**< FIFO-1 interrupt */
#define RX_INTR_FIFO_2		0x4		/**< FIFO-2 interrupt */

#define MAX_RX_FIFO		3

#define RX_CTL_FIFOSEL_SHIFT	8
#define RX_CTL_FIFOSEL_MASK	(0x3 << RX_CTL_FIFOSEL_SHIFT)

#define RCO_EN				(0x1u)  /**< Receive checksum offload */

/* MAC_PTM_CTRL1 bit definitions */
#define PTM_RX_TMSTMP_CAPTURE_EN	0x0001u
#define PTM_TX_TMSTMP_CAPTURE_EN	0x0001u
#define PTM_TMSTMP_OVERRIDE_EN		0x1000u

/* For corerev >= 64
 * Additional DMA descriptor flags for AQM Descriptor. These are used in
 * conjunction with the descriptor control flags defined in sbhnddma.h
 */
/* AQM DMA Descriptor control flags 1 */
#define D64_AQM_CTRL1_SOFPTR		0x0000FFFF	/* index of the descr which
							 * is SOF decriptor in DMA table
							 */
#define D64_AQM_CTRL1_EPOCH		0x00010000	/* Epoch bit for the frame */
#define D64_AQM_CTRL1_NUMD_MASK		0x00F00000	/* NumberofDescriptors(NUMD) */
#define D64_AQM_CTRL1_NUMD_SHIFT	20
#define D64_AQM_CTRL1_AC_MASK		0x0F000000	/* AC of the current frame */
#define D64_AQM_CTRL1_AC_SHIFT		24

/* AQM DMA Descriptor control flags 2 */
#define D64_AQM_CTRL2_MPDULEN_MASK	0x00003FFF	/* Length of the entire MPDU */
#define D64_AQM_CTRL2_TXDTYPE		0x00080000	/* When set to 1 the long form of the
							 * TXD is used for the frame.
							 */
/* For corerev >= 83
 * DMA descriptor flags for AQM Descriptor. These are used in
 * conjunction with the descriptor control flags defined in sbhnddma.h
 */
/* AQM DMA Descriptor control flags 1 */
#define D11_REV83_AQM_DESC_CTRL1_SOFPTR		0x0000FFFFu	/* index of the descr which
								 * is SOF decriptor in DMA table
								 */
#define D11_REV83_AQM_DESC_CTRL1_EPOCH_SHIFT		16u
#define D11_REV83_AQM_DESC_CTRL1_EPOCH			(1u << D11_REV83_AQM_DESC_CTRL1_EPOCH_SHIFT)
#define D11_REV83_AQM_DESC_CTRL1_EPOCH_EXT_SHIFT	17u
#define D11_REV83_AQM_DESC_CTRL1_EPOCH_EXT		(1u << \
							D11_REV83_AQM_DESC_CTRL1_EPOCH_EXT_SHIFT)
#define D11_REV83_AQM_DESC_CTRL1_EPOCH_MASK	(D11_REV83_AQM_DESC_CTRL1_EPOCH | \
							D11_REV83_AQM_DESC_CTRL1_EPOCH_EXT)
#define D11_REV83_AQM_DESC_CTRL1_RESV1		0x00040000u	/* RESERVED */
#define D11_REV83_AQM_DESC_CTRL1_FRAGALLOW_SHIFT	19u	/* Fragmentation allowance flag
								 * shift.
								 */
#define D11_REV83_AQM_DESC_CTRL1_FRAGALLOW	(1u << D11_REV83_AQM_DESC_CTRL1_FRAGALLOW_SHIFT)
								/* Fragmentation allowance flag
								 * of the frame
								 */
#define D11_REV83_AQM_DESC_CTRL1_NUMD_SHIFT	20u		/* NumberofDescriptors(NUMD) */
#define D11_REV83_AQM_DESC_CTRL1_NUMD_MASK	(0xFu << D11_REV83_AQM_DESC_CTRL1_NUMD_SHIFT)
#define D11_REV83_AQM_DESC_CTRL1_AC_SHIFT	24u		/* AC of the current frame */
#define D11_REV83_AQM_DESC_CTRL1_AC_MASK	(0xFu << D11_REV83_AQM_DESC_CTRL1_AC_SHIFT)
#define D11_REV83_AQM_DESC_CTRL1_ET		0x10000000u	/* End of table */
#define D11_REV83_AQM_DESC_CTRL1_IC		0x20000000u	/* Interrupt on Completion */
#define D11_REV83_AQM_DESC_CTRL1_RESV2		0x40000000u	/* Used to be EF: End of frame,
								 * and would have been set to 1.
								 */
#define D11_REV83_AQM_DESC_CTRL1_RESV3		0x80000000u	/* Used to be SF: Start of Frame,
								 * and would have been set to 1
								 */

/* AQM DMA Descriptor control flags 2 */
#define D11_REV83_AQM_DESC_CTRL2_MPDULEN_MASK	0x00003FFFu	/* Length of the entire MPDU */
#define D11_REV83_AQM_DESC_CTRL2_FTYPE_SHIFT	14u		/* Frame Type, Indicate whether
								 * frame is Data, Management or
								 * Control Frame. 2 bits:
								 * 2'b00=Data, 2'b01=Management,
								 * 2'b10=Control, 2'b11=Invalid
								 * value
								 */
#define D11_REV83_AQM_DESC_CTRL2_FTYPE_MASK	(0x3u << D11_REV83_AQM_DESC_CTRL2_FTYPE_SHIFT)
#define D11_REV83_AQM_DESC_CTRL2_PTXDLENIDX_SHIFT	16u /* pTxD length index in 4-deep table */
#define D11_REV83_AQM_DESC_CTRL2_PTXDLENIDX_MASK	(0x3u << \
							D11_REV83_AQM_DESC_CTRL2_PTXDLENIDX_SHIFT)
#define D11_REV83_AQM_DESC_CTRL2_PT		0x00040000u	/* Parity bit. Choose a
								 * value such that the entire
								 * descriptor haseven parity
								 */
#define D11_REV83_AQM_DESC_CTRL2_USERIT		0x00080000u	/* If set, the Rate Table Index and
								 * RIT entry are fetched into SHM by
								 * hardware. Otherwise, software
								 * uses pTxD to convey this
								 * information to ucode
								 */
#define D11_REV83_AQM_DESC_CTRL2_USELIT		0x00100000u	/* If set, the Link Info Table Index
								 * and LIT entry are fetched into
								 * SHM by hardware. Otherwise,
								 * software uses pTxD to convey this
								 * information to ucode
								 */
#define D11_REV83_AQM_DESC_CTRL2_LIT_SHIFT	21u	/* LTI(Link info Table Index) */
#define D11_REV83_AQM_DESC_CTRL2_LIT_MASK	(0x3Fu << D11_REV83_AQM_DESC_CTRL2_LIT_SHIFT)
#define D11_REV83_AQM_DESC_CTRL2_RIT_SHIFT	27u	/* bit[4:0] of RTI(Rate info Table Index) */
#define D11_REV83_AQM_DESC_CTRL2_RIT_MASK	(0x1Fu << D11_REV83_AQM_DESC_CTRL2_RIT_SHIFT)

/* AQM DMA Descriptor control flags 3 */
#define D11_REV86_AQM_DESC_CTRL3_RTI_BIT5	0x00000001u /* bit[5] of RTI (cont'd from ctrl2) */
#define D11_REV86_AQM_DESC_CTRL3_RTI_BIT5_MASK	1u          /* bit[5] of RTI (cont'd from ctrl2) */
#define D11_REV86_AQM_DESC_CTRL3_RTI_BIT5_SHIFT	0u
#define D11_REV83_AQM_DESC_CTRL3_AGGR_ID	0x0000000Eu	/* Aggregation ID */
#define D11_REV83_AQM_DESC_CTRL3_CO		0x00000010u	/* Coherency */
#define D11_REV84_AQM_DESC_CTRL3_TXDPTR_SHIFT	5u		/* TxD ptr */
#define D11_REV84_AQM_DESC_CTRL3_TXDPTR_MASK	0xFFFFFFu	/* bit[23:0] of TxD addr */
#define D11_REV86_AQM_DESC_CTRL3_TID_SHIFT	29u		/* TID for BSR */
#define D11_REV86_AQM_DESC_CTRL3_TID_MASK	(0x7u << D11_REV86_AQM_DESC_CTRL3_TID_SHIFT)

/* values for psm_patchcopy_ctrl (0x1AC) post corerev 60 */
#define PSM_PATCHCC_PMODE_MASK		(0x3)
#define PSM_PATCHCC_PMODE_RAM		(0)	/* default */
#define PSM_PATCHCC_PMODE_ROM_RO	(1)
#define PSM_PATCHCC_PMODE_ROM_PATCH	(2)

#define PSM_PATCHCC_PENG_TRIGGER_SHIFT	(2)
#define PSM_PATCHCC_PENG_TRIGGER_MASK	(1 << PSM_PATCHCC_PENG_TRIGGER_SHIFT)
#define PSM_PATCHCC_PENG_TRIGGER	(1 << PSM_PATCHCC_PENG_TRIGGER_SHIFT)

#define PSM_PATCHCC_PCTRL_RST_SHIFT	(3)
#define PSM_PATCHCC_PCTRL_RST_MASK	(0x3 << PSM_PATCHCC_PCTRL_RST_SHIFT)
#define PSM_PATCHCC_PCTRL_RST_RESET	(0x0 << PSM_PATCHCC_PCTRL_RST_SHIFT)
#define PSM_PATCHCC_PCTRL_RST_HW	(0x1 << PSM_PATCHCC_PCTRL_RST_SHIFT)

#define PSM_PATCHCC_COPYEN_SHIFT	(5)
#define PSM_PATCHCC_COPYEN_MASK		(1 << PSM_PATCHCC_COPYEN_SHIFT)
#define PSM_PATCHCC_COPYEN		(1 << PSM_PATCHCC_COPYEN_SHIFT)

#define PSM_PATCHCC_UCIMGSEL_SHIFT	(16)
#define PSM_PATCHCC_UCIMGSEL_MASK	(0x30000)
#define PSM_PATCHCC_UCIMGSEL_DS0	(0x00000)	/* default image */
#define PSM_PATCHCC_UCIMGSEL_DS1	(0x10000)	/* image 1 */

/* patch copy delay for psm: 2millisec */
#define PSM_PATCHCOPY_DELAY		(2000)

/* START-below WAS in d11_if_shm.h which we can move to auto shm.
 * Some of them are offsets, but some of them are not given by ucode [possibly legacy]
 * so, not taken care by autoshm.
 */

/* Addr is byte address used by SW; offset is word offset used by uCode */

/** Per AC TX limit settings */
#define M_AC_TXLMT_ADDR(x, _ac)         (M_AC_TXLMT_BLK(x) + (2 * (_ac)))

/** delay from end of PLCP reception to RxTSFTime */
#define	M_APHY_PLCPRX_DLY	3
#define	M_BPHY_PLCPRX_DLY	4

/* btcx debug shmem size */
#define C_BTCX_DBGBLK_SZ	6	/**< Number of 16bit words */
#define C_BTCX_DBGBLK2_SZ	11	/* size of statistics at 2nd SHM segment */

#define C_BTCX_STATS_DBGBLK_SZ	18 /* total size of statistics at A2DP stats */
#define C_BTCX_A2DP_PRI_SZ	6	/* size of a2dp priority counters stats */
#define C_BTCX_A2DP_BUFCNT_SZ	8	/* size of a2dp buffer counters stats */
#define C_BTCX_ANT_GRANT_SZ	4	/* size of ant granted duration to BT */
#define C_BTCX_STATS_ECNTR_BLK_SZ	C_BTCX_STATS_DBGBLK_SZ /* blk size for btcx ecounters */

#define D11_DMA_CHANNELS	6

/* WME shared memory */
#define M_EDCF_STATUS_OFF(x)	(0x007 * 2)

/* Beacon-related parameters */
#define M_BCN_LI(x)		M_PS_MORE_DTIM_TBTT(x)	/**< beacon listen interval */

/* prerev 40 defines */
#define	D11_PRE40_M_SECKINDXALGO_BLK(x)	(0x2ea * 2)

/* corerev 40 defines */
/* BLK SIZE needs to change for GE64 */
#define	D11_POST80_MAX_KEY_SIZE		32
#define	D11_PRE80_MAX_KEY_SIZE		16

#define D11_MAX_KEY_SIZE(_corerev) ((D11REV_GE(_corerev, 80)) ? \
		D11_POST80_MAX_KEY_SIZE : D11_PRE80_MAX_KEY_SIZE)

#define M_SECKINDXALGO_BLK_SZ(_corerev)   (AMT_SIZE(_corerev) + 4 /* default keys */)

#define	C_CTX_PCTLWD_POS	(0x4 * 2)

#define D11_MAX_TX_FRMS		32		/**< max frames allowed in tx fifo */

/* Current channel number plus upper bits */
#define D11_CURCHANNEL_5G	0x0100;
#define D11_CURCHANNEL_40	0x0200;
#define D11_CURCHANNEL_MAX	0x00FF;

#define INVALIDFID		0xffff

#define	D11_RT_DIRMAP_SIZE	16

/** Rate table entry offsets */
#define	M_RT_PRS_PLCP_POS(x)	10
#define	M_RT_PRS_DUR_POS(x)	16
#define	M_RT_OFDM_PCTL1_POS(x)	18
#define	M_RT_TXPWROFF_POS(x)	20
#define	M_REV40_RT_TXPWROFF_POS(x)	14

#define MIMO_MAXSYM_DEF		0x8000 /* 32k */
#define MIMO_MAXSYM_MAX		0xffff /* 64k */

#define WATCHDOG_8TU_DEF_LT42	5
#define WATCHDOG_8TU_MAX_LT42	10
#define WATCHDOG_8TU_DEF	3
#define WATCHDOG_8TU_MAX	4

#define M_PKTENG_RXAVGPWR_ANT(x, w)            (M_MFGTEST_RXAVGPWR_ANT0(x) + (w) * 2)

/* M_MFGTEST_NUM (pkt eng) bit definitions */
#define MFGTEST_TXMODE			0x0001 /* TX frames indefinitely */
#define MFGTEST_RXMODE			0x0002 /* RX frames */
#define MFGTEST_RXMODE_ACK		0x0402 /* RX frames with sending ACKs back */
#define MFGTEST_RXMODE_FWD2FW		0x8000 /* RX frames - forward packet to the fw */
#define MFGTEST_TXMODE_FRMCNT		0x0101 /* TX frames by frmcnt */
#define MFGTEST_RU_TXMODE		0x0011	/* RU frames TX indefinetly */
#define MFGTEST_RU_TXMODE_FRMCNT	0x0111 /* RU TX frames by frmcnt */

/* UOTA interface bit definitions */
enum {
	C_UOTA_CNTSRT_NBIT = 0,	 /* 0 OTA rx frame count start bit (14 LSB's) */
	C_UOTA_RXFST_NBIT = 14,	 /* 14 indicating first frame */
	C_UOTA_RSSION_NBIT = 15, /* 15 OTA rx ON bit position */
};

#define M_EDCF_QLEN(x)	(M_EDCF_QINFO1_OFFSET(x))
#define M_PWRIND_MAP(x, core)		(M_PWRIND_BLKS(x) + ((core)<<1))

#define M_BTCX_MAX_INDEX		320u
#define M_BTCX_BACKUP_SIZE		130
#define BTCX_AMPDU_MAX_DUR		2500

#define ADDR_STAMON_NBIT	(1 << 10) /* STA monitor bit in AMT_INFO_BLK entity */

#ifdef WLP2P_UCODE

/** The number of scheduling blocks */
#ifdef BCMFUZZ /* need more for fuzzing */
#define M_P2P_BSS_MAX		8
#else
#define M_P2P_BSS_MAX		4
#endif /* BCMFUZZ */

/** WiFi P2P interrupt block positions */
#define M_P2P_I_BLK_SZ		4
#define M_P2P_I_BLK_OFFSET(x)	(M_P2P_INTR_BLK(x) - M_P2P_INTF_BLK(x))
#define M_P2P_I_BLK(x, b)		(M_P2P_I_BLK_OFFSET(x) + (M_P2P_I_BLK_SZ * (b) * 2))
#define M_P2P_I(x, b, i)		(M_P2P_I_BLK(x, b) + ((i) * 2))

#define M_P2P_I_PRE_TBTT	0	/**< pretbtt, wake up just before beacon reception */
#define M_P2P_I_CTW_END		1	/**< CTWindow ends */
#define M_P2P_I_ABS		2	/**< absence period start, trigger for switching channels */
#define M_P2P_I_PRS		3	/**< presence period starts */

/** P2P hps flags */
#define M_P2P_HPS_CTW(b)	(1 << (b))
#define M_P2P_HPS_NOA(b)	(1 << ((b) + M_P2P_BSS_MAX))

/** WiFi P2P address attribute block */
#define M_ADDR_BMP_BLK_SZ		12
#define M_ADDR_RANDMAC_BMP_BLK_SZ	40u

#define M_ADDR_BMP_BLK(x, b)	(M_ADDR_BMP_BLK_OFFSET(x) + ((b) * 2))

#define ADDR_BMP_RA		(1 << 0)	/**< Receiver Address (RA) */
#define ADDR_BMP_TA		(1 << 1)	/**< Transmitter Address (TA) */
#define ADDR_BMP_BSSID		(1 << 2)	/**< BSSID */
#define ADDR_BMP_AP		(1 << 3)	/**< Infra-BSS Access Point (AP) */
#define ADDR_BMP_STA		(1 << 4)	/**< Infra-BSS Station (STA) */
#define ADDR_BMP_P2P_DISC	(1 << 5)	/**< P2P Device */
#define ADDR_BMP_P2P_GO		(1 << 6)	/**< P2P Group Owner */
#define ADDR_BMP_P2P_GC		(1 << 7)	/**< P2P Client */
#define ADDR_BMP_BSS_IDX_MASK	(3 << 8)	/**< BSS control block index */
#define ADDR_BMP_BSS_IDX_SHIFT	8

/** WiFi P2P address starts from this entry in RCMTA */
#define P2P_ADDR_STRT_INDX	(RCMTA_SIZE - M_ADDR_BMP_BLK_SZ)

/* WiFi P2P per BSS control block positions.
 * all time related fields are in units of (1<<P2P_UCODE_TIME_SHIFT)us unless noted otherwise.
 */

#define P2P_UCODE_TIME_SHIFT		7
#define M_P2P_BSS_BLK_SZ		12
#define M_P2P_BSS_BLK_OFFSET(x)		(M_P2P_PERBSS_BLK(x) - M_P2P_INTF_BLK(x))
#define M_P2P_BSS_BLK(x, b)		(M_P2P_BSS_BLK_OFFSET(x) + (M_P2P_BSS_BLK_SZ * (b) * 2))
#define M_P2P_BSS(x, b, p)		(M_P2P_BSS_BLK(x, b) + (p) * 2)
#define M_P2P_BSS_BCN_INT(x, b)		(M_P2P_BSS_BLK(x, b) + (0 * 2))	/**< beacon interval */
#define M_P2P_BSS_DTIM_PRD(x, b)	(M_P2P_BSS_BLK(x, b) + (1 * 2))	/**< DTIM period */
#define M_P2P_BSS_ST(x, b)		(M_P2P_BSS_BLK(x, b) + (2 * 2))	/**< current state */
#define M_P2P_BSS_N_PRE_TBTT(x, b)	(M_P2P_BSS_BLK(x, b) + (3 * 2))	/**< next pretbtt time */
#define M_P2P_BSS_CTW(x, b)		(M_P2P_BSS_BLK(x, b) + (4 * 2))	/**< CTWindow duration */
#define M_P2P_BSS_N_CTW_END(x, b)	(M_P2P_BSS_BLK(x, b) + (5 * 2))	/**< next CTWindow end */
#define M_P2P_BSS_NOA_CNT(x, b)		(M_P2P_BSS_BLK(x, b) + (6 * 2))	/**< NoA count */
#define M_P2P_BSS_N_NOA(x, b)		(M_P2P_BSS_BLK(x, b) + (7 * 2))	/**< next absence time */
#define M_P2P_BSS_NOA_DUR(x, b)		(M_P2P_BSS_BLK(x, b) + (8 * 2))	/**< absence period */
#define M_P2P_BSS_NOA_TD(x, b)		(M_P2P_BSS_BLK(x, b) + (9 * 2))
								/**< presence period (int - dur) */
#define M_P2P_BSS_NOA_OFS(x, b)		(M_P2P_BSS_BLK(x, b) + (10 * 2))
								/* last 7 bits of interval in us */
#define M_P2P_BSS_DTIM_CNT(x, b)	(M_P2P_BSS_BLK(x, b) + (11 * 2))
								/**< DTIM count */

/* M_P2P_BSS_ST word positions. */
#define M_P2P_BSS_ST_CTW	(1 << 0)	/**< BSS is in CTWindow */
#define M_P2P_BSS_ST_SUPR	(1 << 1)	/**< BSS is suppressing frames */
#define M_P2P_BSS_ST_ABS	(1 << 2)	/**< BSS is in absence period */
#define M_P2P_BSS_ST_WAKE	(1 << 3)
#define M_P2P_BSS_ST_AP		(1 << 4)	/**< BSS is Infra-BSS AP */
#define M_P2P_BSS_ST_STA	(1 << 5)	/**< BSS is Infra-BSS STA */
#define M_P2P_BSS_ST_GO		(1 << 6)	/**< BSS is P2P Group Owner */
#define M_P2P_BSS_ST_GC		(1 << 7)	/**< BSS is P2P Client */
#define M_P2P_BSS_ST_IBSS	(1 << 8)	/**< BSS is an IBSS */
#define M_P2P_BSS_ST_AWDL	(1 << 9)	/* BSS is AWDL */
#define M_P2P_BSS_ST_NAN	(1 << 10)	/**< BSS is NAN */
#define M_P2P_BSS_ST_MULTIDTIM	(1 << 11)	/* BSS is Muti-DTIM enabled */

/** WiFi P2P TSF block positions */
#define M_P2P_TSF_BLK_SZ		4
#define M_P2P_TSF_BLK_OFFSET(x)		(M_P2P_TSF_OFFSET_BLK(x) - M_P2P_INTF_BLK(x))
#define M_P2P_TSF_BLK(x, b)		(M_P2P_TSF_BLK_OFFSET(x) + (M_P2P_TSF_BLK_SZ * (b) * 2))
#define M_P2P_TSF(x, b, w)		(M_P2P_TSF_BLK(x, b) + (w) * 2)

#define M_P2P_TSF_DRIFT_OFFSET(x)	(M_P2P_TSF_DRIFT_WD0(x) - M_P2P_INTF_BLK(x))
#define M_P2P_TSF_DRIFT(x, w)		(M_P2P_TSF_DRIFT_OFFSET(x) + (w) * 2)

#define M_P2P_GO_CHANNEL_OFFSET(x)	(M_P2P_GO_CHANNEL(x) - M_P2P_INTF_BLK(x))
#define M_P2P_GO_IND_BMP_OFFSET(x)	(M_P2P_GO_IND_BMP(x) - M_P2P_INTF_BLK(x))

/**
 * M_P2P_GO_IND_BMP now has multiple fields:
 *	7:0	- GO_IND_BMP
 *	10:8	- BSS Index
 *	15:11	- Reserved
*/
#define M_P2P_GO_IND_BMP_MASK		(0xFF)
#define M_P2P_BSS_INDEX_MASK		(0x700)
#define M_P2P_BSS_INDEX_SHIFT_BITS	(8)

/* per BSS PreTBTT */
/* BOM 768.0 and above */
#define M_P2P_PRE_TBTT_OFFSET(x)	(M_P2P_PRETBTT_BLK(x) - M_P2P_INTF_BLK(x))
#define M_P2P_PRE_TBTT(x, b)		(M_P2P_PRE_TBTT_OFFSET(x) + ((b) * 2))	/**< in us */

/* Reserve bottom of RCMTA for P2P Addresses */
#define	WSEC_MAX_RCMTA_KEYS	(54 - M_ADDR_BMP_BLK_SZ)
#else
#define	WSEC_MAX_RCMTA_KEYS	54
#endif	/* WLP2P_UCODE */

#define TXCOREMASK		0x0F
#define SPATIAL_SHIFT		8
#define MAX_COREMASK_BLK	5
#define COREMASK_BLK_TRIG_FRAMES	(MAX_COREMASK_BLK + 1)

#define BPHY_ONE_CORE_TX	(1 << 15)	/**< enable TX ant diversity for 11b frames */

#define M_WLCX_CONFIG_EN(x)	0x1				/**< 1: enable wifi coex */
#define M_WLCX_CONFIG_MASTER(x)	0x2				/**< 1: Coex Master(5357) */

/* ucode debug status codes */
#define	DBGST_INACTIVE		0		/**< not valid really */
#define	DBGST_INIT		1		/**< after zeroing SHM, before suspending at init */
#define	DBGST_ACTIVE		2		/**< "normal" state */
#define	DBGST_SUSPENDED		3		/**< suspended */
#define	DBGST_ASLEEP		4		/**< asleep (PS mode) */
#define DBGST_SLP2WAKE          7               /* On wake up path. */

/**
 * Defines for Self Mac address (used currently for CTS2SELF frames
 * generated by BTCX ucode for protection purposes) in SHM. GE40 only.
 */
#define M_MYMAC_ADDR_L(x)                (M_MYMAC_ADDR(x))
#define M_MYMAC_ADDR_M(x)                (M_MYMAC_ADDR(x) + (1*2))
#define M_MYMAC_ADDR_H(x)                (M_MYMAC_ADDR(x) + (2*2))

/* Re-uses M_SSID */
#define SHM_MBSS_BCNLEN0(x)		M_SSID(x)

#define SHM_MBSS_CLOSED_NET(x)		(0x80)	/**< indicates closed network */

/** SSID Search Engine entries */
#define SHM_MBSS_SSIDSE_BASE_ADDR(x)	(0)
#define SHM_MBSS_SSIDSE_BLKSZ(x)		(36)
#define SHM_MBSS_SSIDLEN_BLKSZ		(4)
#define SHM_MBSS_SSID_BLKSZ			(32)

/* END New for ucode template based mbss */

/** Definitions for PRQ fifo data */

#define SHM_MBSS_PRQ_ENTRY_BYTES 10	/**< Size of each PRQ entry */
#define SHM_MBSS_PRQ_ENTRY_COUNT 12	/**< Number of PRQ entries */
#define SHM_MBSS_PRQ_TOT_BYTES   (SHM_MBSS_PRQ_ENTRY_BYTES * SHM_MBSS_PRQ_ENTRY_COUNT)

#define M_WOWL_NOBCN	(0x06c * 2)		/**< loss of bcn value */

#define M_KEK(x)		M_EAPOLMICKEY_BLK(x) + (0x10 * 2) /* < KEK for WEP/TKIP */

#define M_ARPRESP_BYTESZ_OFFSET		0	/**< 2 bytes; ARP resp pkt size */
#define M_NA_BYTESZ_0_OFFSET		2	/**< 2 bytes ; NA pkt size */
#define M_NA_BYTESZ_1_OFFSET		4	/**< 2 bytes ; NA pkt size */
#define M_KEEPALIVE_BYTESZ_0_OFFSET	6	/**< 2 bytes; size of first keepalive */
#define M_KEEPALIVE_BYTESZ_1_OFFSET	8	/**< 2 bytes; size of second keepalive */
#define M_NPAT_ARPIDX_OFFSET		10	/**< 2 bytes; net pattern index of ARP */
#define M_NPAT_NS0IDX_OFFSET		12	/**< 2 bytes; net pattern index of NS 0 */
#define M_NPAT_NS1IDX_OFFSET		14	/**< 2 bytes; net pattern index of NS 1 */
#define M_EXTWAKEPATTERN_0_OFFSET	16	/**< 6 bytes; ext magic pattern */
#define M_EXTWAKEPATTERN_U0_OFFSET	22	/**< 8 bytes; unaligned ext magic pattern */
#define M_KEEPALIVE_INTVL_0_OFFSET	30	/**< 2 bytes; in no of beacon intervals */
#define M_KEEPALIVE_INTVL_1_OFFSET	32	/**< 2 bytes; in no of beacon intervals */

#define M_COREMASK_BLK_WOWL_L30     (0x298 * 2)

/* corerev > 29 && corerev < 40 */
#define M_COREMASK_BLK_WOWL         (0x7e8 *2)

/* corerev >= 42 */
#define D11AC_M_COREMASK_BLK_WOWL       (0x1b0*2)

#define	M_EXTLNA_PWRSAVE(x)	M_RADIO_PWR(x)	/**< External LNA power control support */

/* D11AC shm location changes */
#define	D11AC_T_NULL_TPL_BASE		(0x16 * 2)
#define D11AC_T_NULL_TPL_SIZE_BYTES	(24)
#define D11_T_BCN0_TPL_BASE	T_BCN0_TPL_BASE
#define D11AC_T_BCN0_TPL_BASE	(0x100 * 2)
#define D11_T_BCN1_TPL_BASE	T_BCN1_TPL_BASE
#define D11AC_T_BCN1_TPL_BASE	(0x240 * 2)
#define D11AC_T_GACT_TWT_INFO_TPL_BASE	(0xB0 * 2)
#define D11AC_T_GACT_TWT_INFO_TPL_SIZE_BYTES	(36)

/* The response (ACK/BA) phyctrl words */
#define D11AC_RSP_TXPCTL0      (0x4c * 2)
#define D11AC_RSP_TXPCTL1      (0x4d * 2)

#define D11AC_T_PRS_TPL_BASE    (0x380 * 2)

#define	D11_M_RT_PRS_PLCP_POS(x) M_RT_PRS_PLCP_POS(x)
#define	D11_M_RT_PRS_DUR_POS(x) M_RT_PRS_DUR_POS(x)
#define D11AC_M_RT_PRS_PLCP_POS 8
#define D11AC_M_RT_PRS_DUR_POS 12

/* Field definitions for M_REV40_RT_TXPWROFF_POS */
#define M_REV40_RT_HTTXPWR_OFFSET_MASK	0x01f8	/**< bit 8:3 */
#define M_REV40_RT_HTTXPWR_OFFSET_SHIFT	3

/* for axphy */
#define M_REV80_RT_TXPWR_OFFSET_MASK	0xff00	/* bit 15:8 */
#define M_REV80_RT_TXPWR_OFFSET_SHIFT	9	/* 8 (byte align) + 1 (convert from S5.1 to S5.2) */

/* shmem locations for Beamforming */
/* shmem defined with prefix M_ are in shmem */
#define shm_addr(base, offset)  (((base)+(offset))*2)

#define C_BFI_REFRESH_THR_OFFSET  (1u)
#define C_BFI_NDPA_TXLMT_OFFSET   (2u)
#define C_BFI_NRXC_OFFSET         (3u)
#define C_BFI_MLBF_LUT_OFFSET     (4u)  // for corerev < 64 only

#define C_BFI_BLK_SIZE(corerev)	 ((D11REV_GE(corerev, 86) ? 18u: 16u))

/* BFI block definitions (Beamforming) */
#define C_BFI_BFRIDX_POS          (0)
#define	C_BFI_NDPA_TST_POS        (1)
#define	C_BFI_NDPA_TXCNT_POS      (2)
#define C_BFI_NDPA_SEQ_POS        (3)
#define C_BFI_NDPA_FCTST_POS      (4)
#define C_BFI_BFRCTL_POS          (5)
#define C_BFI_BFR_CONFIG0_POS     (6)
#define C_BFI_BFE_CONFIG0_POS     (7)
#define C_BFI_BFE_MIMOCTL_POS     (8)
#define C_BFI_BSSID0_POS          (9)
#define C_BFI_BSSID1_POS          (10)
#define C_BFI_BSSID2_POS          (11)
#define C_BFI_STAINFO_POS         (12)
#define C_BFI_STAINFO1_POS        (13)
#define C_BFI_BFE_MYAID_POS       (13) /* stainfo1 is mutually exclusive */
#define C_BFI_BFMSTAT_POS         (14)
#define C_BFI_BFE_MIMOCTL_EXT_POS (15)
/* below SHMs for rev >= 86 */
#define C_BFI_BFE_11AXMIMOCTL_POS (16) /* phyreg bfeMimoCtlReg for 11AX */
#define C_BFI_BFE_NDPNR_POS       (17)
/* used by BFR */
#define C_BFI_STA_ADDR_POS C_BFI_BSSID0_POS

/* to be removed -start */
#define M_BFI_BLK_SIZE            (16u)
#define BFI_BLK_SIZE  18
/* to be removed -end */

/* Phy cache index Bit<8> indicates the validity. Cleared during TxBf link Init
 * to trigger a new sounding sequence.
 */
#define C_BFRIDX_VLD_NBIT	8 /* valid */
#define C_BFRIDX_EN_NBIT	7 /* BFI block is enabled (has valid info),
				   * applicable only for MU BFI block in shmemx
				   */
#define C_BFRIDX_BW_NBIT	12

#define C_STAINFO_FBT_NBIT   12   /* 0: SU; 1: MU */
#define C_STAINFO_NCIDX_NBIT 13 /* Bits13-15: NC IDX; Reserved if Feedback Type is SU */

/* NDP control blk */
#define C_BFI_BFRCTL_POS_NDP_TYPE_SHIFT  (0)   /* 0: HT NDP; 1: VHT NDP; HE no need */
#define C_BFI_BFRCTL_POS_NSTS_SHIFT      (1)   /* 0: 2ss; 1: 3ss; 2: 4ss */
#define C_BFI_BFRCTL_POS_MLBF_SHIFT      (4)   /* 1  enable MLBF(used for corerev < 64) */
#define C_BFI_BFRCTL_POS_BFM_SHIFT       (8)   /* Bits15-8: BFM mask for BFM frame tx */

/** dynamic rflo ucode WAR defines */
#define UCODE_WAR_EN		1
#define UCODE_WAR_DIS		0

/** LTE coex definitions */
#define LTECX_FLAGS_LPBK_OFF 0

/** LTECX shares BTCX shmem block */
#define M_LTECX_BLK_PTR(x)				M_BTCX_BLK_PTR(x)

/** NR5GCX shares BTCX shmem block */
#define M_NR5GCX_BLK_PTR(x)				M_BTCX_BLK_PTR(x)

/** RC1CX shares BTCX shmem block */
#define M_RC1CX_BLK_PTR(x)				M_BTCX_BLK_PTR(x)

/** RC2CX shares BTCX shmem block */
#define M_RC2CX_BLK_PTR(x)				M_BTCX_BLK_PTR(x)

/* CORE0 MODE */
#define CORE0_MODE_RSDB		0x0
#define CORE0_MODE_MIMO		0x1
#define CORE0_MODE_80P80	0x2

#define CORE1_MODE_RSDB		0x100

#define HWACI_HOST_FLAG_ADDR		(0x186)
#define HWACI_SET_SW_MITIGATION_MODE	(0x0008)

/* split RX war shm locations  */
#define RXFIFO_0_OFFSET 0x1A0
#define RXFIFO_1_OFFSET 0x19E
#define HDRCONV_FIFO0_STSLEN    0x4	/* status length in header conversion mode */

/* GE80:
 * [15:8]: Phy status length
 *  [7:0]: Ucode status length
 */
#define DEFAULT_FIFO0_STSLEN(corerev, corerev_minor) \
	(D11REV_MAJ_MIN_GE(corerev, corerev_minor, 87, 1) ? 0x2018 : \
	D11REV_GE(corerev, 80) ? 0x2010: 0x24)

/* M_ULP_WAKEIND bits */
#define	C_WATCHDOG_EXPIRY	(1 << 0)
#define	C_FCBS_ERROR		(1 << 1)
#define	C_RETX_FAILURE		(1 << 2)
#define	C_HOST_WAKEUP		(1 << 3)
#define	C_INVALID_FCBS_BLOCK	(1 << 4)
#define	C_HUDI_DS1_EXIT		(1 << 5)
#define	C_LOB_SLEEP		(1 << 6)

/* values for M_ULP_FEATURES */
#define C_P2P_NOA			(0x0001)
#define C_INFINITE_NOA			(0x0002)
#define C_P2P_CTWIN			(0x0004)
#define C_P2P_GC			(0x0008)
#define C_BCN_TRIM			(0x0010)
#define C_BT_COEX			(0x0020)
#define C_LTE_COEX			(0x0040)
#define C_ADS1				(0x0080)
#define C_LTECX_PSPOLL_PRIO_EN		(0x0100)
#define C_ULP_SLOWCAL_SKIP		(0x0200)
#define C_HUDI_ENABLE			(0x0400)

#define M_WOWL_ULP_SW_DAT_BLK	(0xBFF * 2)	/* (0xFFF * 2) - 1024 */
#define M_WOWL_ULP_SW_DAT_BLK_MAX_SZ	(0x400)	/* 1024 bytes */

#define RX_INTR_FIFO_0		0x1		/* FIFO-0 interrupt */
#define RX_INTR_FIFO_1		0x2		/* FIFO-1 interrupt */
#define RX_INTR_FIFO_2		0x4		/* FIFO-2 interrupt */

/* M_TOF_FLAG bits */
typedef enum {
	TOF_RX_FTM_NBIT = 0,
	TOF_SEQ_DISRXENTX_RFCTL = 1,
	TOF_IS_TARGET = 2,
	TOF_TPC_FREEZE = 3
} eTOFFlags;

/* TOF feature flags */
#define M_UCODE_F2_TOF_BIT	7 /* part of features_2 shm */
#define M_UCODE_F3_AVB_BIT	2 /* part of features_3 shm */
#define M_UCODE_F3_SEQ_BIT	3 /* part of features_3 shm */

/* New SHM definitions required for tsync based time stamping of FTM frames.
* More details in below conf
* http://confluence.broadcom.com/display/WLAN/NewUcodeInterfaceForProxdFeature
*/
#define FTM_TIMESTAMP_SHIFT		16
#define TXS_ACK_INDEX_SHIFT		3
#define FTM_ACK_TS_BLOCK_SIZE		3
#define RXH_ACK_SHIFT(corerev)	(D11REV_GE((corerev), 80) ? 12u:8u)
#define FTM_INVALID_SHM_INDEX(corerev)	(D11REV_GE((corerev), 80) ? 0x04u:0x0Fu)
#define FTM_ACK_INDEX_MASK		0x0F
#define NUM_UCODE_ACK_TS_BLKS		4

#define FTM_TXSTATUS_ACK_RSPEC_BLOCK_MASK	0xFF
#define FTM_TXSTATUS_ACK_RSPEC_BW_MASK		0x3
#define FTM_TXSTATUS_ACK_RSPEC_BW_SHIFT		2
#define FTM_TXSTATUS_ACK_RSPEC_BW_20		0
#define FTM_TXSTATUS_ACK_RSPEC_BW_40		1
#define FTM_TXSTATUS_ACK_RSPEC_BW_80		2
#define FTM_TXSTATUS_ACK_RSPEC_BW_160		3
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_SHIFT	4
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_MASK	0x7
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_CCK		0
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_LEG		1 /* Legacy */
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_HT		2
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_VHT		3
#define FTM_TXSTATUS_ACK_RSPEC_TYPE_HE		4
#define FTM_TXSTATUS_ACK_RSPEC_RATE_6M(ackword)	(ackword >> 7)
/* Following are the offsets in M_DRVR_UCODE_IF_PTR block. Start address of
 * M_DRVR_UCODE_IF_PTR block is present in M_DRVR_UCODE_IF_PTR.
 */
#define M_ULP_FEATURES			(0x0 * 2)

/* M_HOST_FLAGS5 offset changed in ULP ucode */
#define M_ULP_HOST_FLAGS5   (0x3d * 2)

#define M_RADAR_REG_TMP			(0x033 * 2)

/* Bit masks for ClkGateUcodeReq2: Ucode MAC Clock Request2 (IHR Address 0x375)  register */
#define D11_FUNC16_MAC_CLOCKREQ_MASK (0x3)

/*
 * Clock gating registers
 */
#define CLKREQ_BLOCK	0
#define CLKREQ_MAC_ILP	1
#define CLKREQ_MAC_ALP	2
#define CLKREQ_MAC_HT	3

/* ClkGateSts */
#define CLKGTE_FORCE_MAC_CLK_REQ_SHIFT			0
#define CLKGTE_MAC_PHY_CLK_REQ_SHIFT			4

/* ClkGateReqCtrl0 */
#define CLKGTE_PSM_PATCHCOPY_CLK_REQ_SHIFT		0
#define CLKGTE_RXKEEP_OCP_CLK_REQ_SHIFT			2
#define CLKGTE_PSM_MAC_CLK_REQ_SHIFT			4
#define CLKGTE_TSF_CLK_REQ_SHIFT			6
#define CLKGTE_AQM_CLK_REQ_SHIFT			8
#define CLKGTE_SERIAL_CLK_REQ_SHIFT			10
#define CLKGTE_TX_CLK_REQ_SHIFT				12
#define CLKGTE_POSTTX_CLK_REQ_SHIFT			14

/* ClkGateReqCtrl1 */
#define CLKGTE_RX_CLK_REQ_SHIFT				0
#define CLKGTE_TXKEEP_OCP_CLK_REQ_SHIFT			2
#define CLKGTE_HOST_RW_CLK_REQ_SHIFT			4
#define CLKGTE_IHR_WR_CLK_REQ_SHIFT			6
#define CLKGTE_TKIP_KEY_CLK_REQ_SHIFT			8
#define CLKGTE_TKIP_MISC_CLK_REQ_SHIFT			10
#define CLKGTE_AES_CLK_REQ_SHIFT			12
#define CLKGTE_WAPI_CLK_REQ_SHIFT			14

/* ClkGateReqCtrl2 */
#define CLKGTE_WEP_CLK_REQ_SHIFT			0
#define CLKGTE_PSM_CLK_REQ_SHIFT			2
#define CLKGTE_MACPHY_CLK_REQ_BY_PHY_SHIFT		4
#define CLKGTE_FCBS_CLK_REQ_SHIFT			6
#define CLKGTE_HIN_AXI_MAC_CLK_REQ_SHIFT		8

/* ClkGateStretch0 */
#define CLKGTE_MAC_HT_CLOCK_STRETCH_SHIFT		0
#define CLKGTE_MAC_ALP_CLOCK_STRETCH_SHIFT		8
#define CLKGTE_MAC_HT_CLOCK_STRETCH_VAL			0x4

/* ClkGateStretch1 */
#define CLKGTE_MAC_PHY_CLOCK_STRETCH_SHIFT		13

/* ClkGateMisc */
#define CLKGTE_TPF_CLK_REQTHRESH			0xF
#define CLKGTE_AQM_CLK_REQEXT				0x70

/* ClkGateDivCtrl */
#define CLKGTE_MAC_ILP_OFF_COUNT_MASK			0x0007
#define CLKGTE_MAC_ILP_OFF_COUNT_SHIFT			0
#define CLKGTE_MAC_ILP_ON_COUNT_MASK			0x0020
#define CLKGTE_MAC_ILP_ON_COUNT_MASK_GE_REV80		0x0030
#define CLKGTE_MAC_ALP_OFF_COUNT_MASK			0x03C0
#define CLKGTE_MAC_ALP_OFF_COUNT_SHIFT			6

/* ClkGatePhyClkCtrl */
#define CLKGTE_PHY_MAC_PHY_CLK_REQ_EN_SHIFT		0
#define CLKGTE_O2C_HIN_PHY_CLK_EN_SHIFT			1
#define CLKGTE_HIN_PHY_CLK_EN_SHIFT			2
#define CLKGTE_IHRP_PHY_CLK_EN_SHIFT			3
#define CLKGTE_CCA_MAC_PHY_CLK_REQ_EN_SHIFT		4
#define CLKGTE_TX_MAC_PHY_CLK_REQ_EN_SHIFT		5
#define CLKGTE_HRP_MAC_PHY_CLK_REQ_EN_SHIFT		6
#define CLKGTE_SYNC_MAC_PHY_CLK_REQ_EN_SHIFT		7
#define CLKGTE_RX_FRAME_MAC_PHY_CLK_REQ_EN_SHIFT	8
#define CLKGTE_RX_START_MAC_PHY_CLK_REQ_EN_SHIFT	9
#define CLKGTE_FCBS_MAC_PHY_CLK_REQ_SHIFT		10
#define CLKGTE_POSTRX_MAC_PHY_CLK_REQ_EN_SHIFT		11
#define CLKGTE_DOT11_MAC_PHY_RXVALID_SHIFT		12
#define CLKGTE_NOT_PHY_FIFO_EMPTY_SHIFT			13
#define CLKGTE_DOT11_MAC_PHY_BFE_REPORT_DATA_READY	14
#define CLKGTE_DOT11_MAC_PHY_CLK_BIT15			15

/* ClkGateExtReq0 */
#define CLKGTE_TOE_SYNC_MAC_CLK_REQ_SHIFT		0
#define CLKGTE_TXBF_SYNC_MAC_CLK_REQ_SHIFT		2
#define CLKGTE_HIN_SYNC_MAC_CLK_REQ_SHIFT		4
#define CLKGTE_SLOW_SYNC_CLK_REQ_SHIFT			6
#define CLKGTE_ERCX_SYNC_CLK_REQ_SHIFT			8
#define CLKGTE_BTCX_SYNC_CLK_REQ_SHIFT			10
#define CLKGTE_IFS_CRS_SYNC_CLK_REQ_SHIFT		12
#define CLKGTE_IFS_GCI_SYNC_CLK_REQ_SHIFT		14

#define CLKGTE_TOE_SYNC_MAC_CLK_REQ_80_SHIFT		2
#define CLKGTE_TXBF_SYNC_MAC_CLK_REQ_80_SHIFT		4
#define CLKGTE_HIN_SYNC_MAC_CLK_REQ_80_SHIFT		6
#define CLKGTE_SLOW_SYNC_CLK_REQ_80_SHIFT		8
#define CLKGTE_ERCX_SYNC_CLK_REQ_80_SHIFT		10
#define CLKGTE_BTCX_SYNC_CLK_REQ_80_SHIFT		12
#define CLKGTE_IFS_CRS_SYNC_CLK_REQ_80_SHIFT		14

#define CLKGTE_TOE_SYNC_MAC_CLK_REQ_83_SHIFT		2
#define CLKGTE_TXBF_SYNC_MAC_CLK_REQ_83_SHIFT		4
#define CLKGTE_HIN_SYNC_MAC_CLK_REQ_83_SHIFT		6
#define CLKGTE_SLOW_SYNC_CLK_REQ_83_SHIFT		8
#define CLKGTE_ERCX_SYNC_CLK_REQ_83_SHIFT		10
#define CLKGTE_BTCX2_SYNC_CLK_REQ_83_SHIFT		12
#define CLKGTE_BTCX_SYNC_CLK_REQ_83_SHIFT		14

/* ClkGateExtReq1 */
#define CLKGTE_PHY_FIFO_SYNC_CLK_REQ_SHIFT		0
#define CLKGTE_RXE_CHAN_SYNC_CLK_REQ_SHIFT		2
#define CLKGTE_PMU_MDIS_SYNC_MAC_CLK_REQ_SHIFT		4
#define CLKGTE_PSM_IPC_SYNC_CLK_REQ_SHIFT		6

#define CLKGTE_IFS_GCI_SYNC_CLK_REQ_80_SHIFT		0
#define CLKGTE_PHY_FIFO_SYNC_CLK_REQ_80_SHIFT		2
#define CLKGTE_RXE_CHAN_SYNC_CLK_REQ_80_SHIFT		4
#define CLKGTE_PMU_MDIS_SYNC_MAC_CLK_REQ_80_SHIFT	6
#define CLKGTE_PSM_IPC_SYNC_CLK_REQ_80_SHIFT		8

#define CLKGTE_IFS_CRS_SYNC_CLK_REQ_83_SHIFT		0
#define CLKGTE_IFS_GCI_SYNC_CLK_REQ_83_SHIFT		2
#define CLKGTE_PHY_FIFO_SYNC_CLK_REQ_83_SHIFT		4
#define CLKGTE_RXE_CHAN_SYNC_CLK_REQ_83_SHIFT		6
#define CLKGTE_PMU_MDIS_SYNC_MAC_CLK_REQ_83_SHIFT	8
#define CLKGTE_PSM_IPC_SYNC_CLK_REQ_83_SHIFT		10

/* PFE CtlStat1 register */
#define PFE_CTLSTAT1_ROUTE_PFE_TO_BMSTAT	(1u << 15u)
#define PFE_CTLSTAT1_PFE_ENABLE			(1u << 0u)

/* PPR Ctrl1 register */
#define PPR_CTMODE_SHIFT			8u
#define PPR_CTMODE_MASK				(3u << PPR_CTMODE_SHIFT)

#define PPR_CTMODE_A				(0u << PPR_CTMODE_SHIFT)
#define PPR_CTMODE_B				(1u << PPR_CTMODE_SHIFT)
#define PPR_CTMODE_C				(2u << PPR_CTMODE_SHIFT)

/* Ptxd Len */
#define PTXD_LEN0_SHIFT				(0u)
#define PTXD_LEN1_SHIFT				(8u)
#define PTXD_LEN2_SHIFT				(0u)
#define PTXD_LEN3_SHIFT				(8u)
/* =========== LHL regs =========== */
/* WL ARM Timer0 Interrupt Status (lhl_wl_armtim0_st_adr) */
#define LHL_WL_ARMTIM0_ST_WL_ARMTIM_INT_ST	0x00000001

#define D11_AUTO_MEM_STBY_RET_SHIFT		(4u)
#define D11_AUTO_MEM_STBY_RET_83_SHIFT		(5u)
#define D11_AUTO_MEM_STBY_NON_RET_SHIFT		(6u)
#define D11_AUTO_MEM_STBY_BM_SHIFT		(9u)

#define D11_AUTO_MEM_STBY_RET_SHIFT_REV(d11rev) \
	(((d11rev) >= 83) ? D11_AUTO_MEM_STBY_RET_83_SHIFT : D11_AUTO_MEM_STBY_RET_SHIFT)

/* WiFi P2P TX stop timestamp block (only applicable with AC ucode) */
#define P2P_TXSTOP_SHMPERBSS		2u	/* 2 shmems per BSS */
#define M_P2P_TXSTOP_TS(x, b, w)	(M_P2P_TXSTOP_T_BLK(x) +\
			(P2P_TXSTOP_SHMPERBSS * (b) + (w)) * 2)

#define D11TXHDR_RATEINFO_ACCESS_VAL(txh, corerev, member) \
	((((txh)->corerev).RateInfo[3]).member)

/* QoS + BSR information */
#define D11_QOS_BSR_TIDQS_SHIFT	0u
#define D11_QOS_BSR_TIDQS_SZ	8u
#define D11_QOS_BSR_TIDQS_MASK	(((1 << D11_QOS_BSR_TIDQS_SZ) - 1) << D11_QOS_BSR_TIDQS_SHIFT)

#define D11_QOS_BSR_UV_SHIFT	8u
#define D11_QOS_BSR_UV_SZ	6u
#define D11_QOS_BSR_UV_MASK	(((1 << D11_QOS_BSR_UV_SZ) - 1) << D11_QOS_BSR_UV_SHIFT)

#define D11_QOS_BSR_SF_SHIFT	14u
#define D11_QOS_BSR_SF_SZ	2u
#define D11_QOS_BSR_SF_MASK	(((1 << D11_QOS_BSR_SF_SZ) - 1) << D11_QOS_BSR_SF_SHIFT)

/* Queue size in QoS control */
#define D11_QOS_BSR_SF_0	0u
#define D11_QOS_BSR_SF_1	1u
#define D11_QOS_BSR_SF_2	2u
#define D11_QOS_BSR_SF_3	3u

#define D11_QS_OFFSET_SF_0	0u
#define D11_QS_OFFSET_SF_1	1024u
#define D11_QS_OFFSET_SF_2	17408u
#define D11_QS_OFFSET_SF_3	148480u

#define D11_QOS_BSR_SF_0_SHIFT	4u	/* Scale: 16 bytes */
#define D11_QOS_BSR_SF_1_SHIFT	8u	/* Scale: 256 bytes */
#define D11_QOS_BSR_SF_2_SHIFT	11u	/* Scale: 2048 bytes */
#define D11_QOS_BSR_SF_3_SHIFT	15u	/* Scale: 32768 bytes */

#define D11_MIN_QS_UV		0u
#define D11_MAX_QS_UV		63u
#define D11_MAX_QS_UV_SF3	((D11_MAX_QS_UV) - 1)

/* 1008: 16 * UV when the Scaling Factor subfield is 0 */
#define D11_MAX_QS_SF_0	(D11_QS_OFFSET_SF_0 + (D11_MAX_QS_UV << D11_QOS_BSR_SF_0_SHIFT))
/* 17152: 1024 + 256 * UV when the Scaling Factor subfield is 1 */
#define D11_MAX_QS_SF_1	(D11_QS_OFFSET_SF_1 + (D11_MAX_QS_UV << D11_QOS_BSR_SF_1_SHIFT))
/* 146432: 17408 + 2048 * UV when the Scaling Factor subfield is 2 */
#define D11_MAX_QS_SF_2	(D11_QS_OFFSET_SF_2 + (D11_MAX_QS_UV << D11_QOS_BSR_SF_2_SHIFT))
/* 2147328: 148480 + 32768 * UV when the Scaling Factor subfield is 3 */
#define D11_MAX_QS_SF_3	(D11_QS_OFFSET_SF_3 + ((D11_MAX_QS_UV_SF3-1) << D11_QOS_BSR_SF_3_SHIFT))

/* 2 bits for HE signature and 4 bits for control ID */
#define D11_BSR_HE_SIG_SHIFT		6u
/* HE Variant with BSR control ID */
#define D11_BSR_HE_SIG			(0xf)
#define D11_BSR_ACI_BMAP_SHIFT		(0 + D11_BSR_HE_SIG_SHIFT)
#define D11_BSR_DELTA_TID_SHIFT		(4 + D11_BSR_HE_SIG_SHIFT)
#define D11_BSR_SF_SHIFT		(8 + D11_BSR_HE_SIG_SHIFT)
#define D11_BSR_QUEUE_SIZE_HIGH_SHIFT	(10 + D11_BSR_HE_SIG_SHIFT)
#define D11_BSR_QUEUE_SIZE_ALL_SHIFT	(18 + D11_BSR_HE_SIG_SHIFT)

#define D11_BSR_DELTA_TID_ALLTID_SIGNATURE	3u

#define D11_BSR_QUEUE_SIZE_WIDTH	8u
#define D11_BSR_QUEUE_SIZE_WIDTH_VAL	((1 << D11_BSR_QUEUE_SIZE_WIDTH) - 1)
#define D11_BSR_QUEUE_SIZE_UNKNOWN	(255u)
#define D11_BSR_QUEUE_SIZE_MAX		(254u)
#define D11_BSR_QUEUE_SIZE_HIGH_MASK		(D11_BSR_QUEUE_SIZE_WIDTH_VAL <<\
		D11_BSR_QUEUE_SIZE_HIGH_SHIFT)
#define D11_BSR_QUEUE_SIZE_ALL_MASK		(D11_BSR_QUEUE_SIZE_WIDTH_VAL <<\
		D11_BSR_QUEUE_SIZE_ALL_SHIFT)

#define D11_BSR_WD1_SHIFT			16u

enum {
	D11_BSR_SF_ID_16 = 0,	/* 0 */
	D11_BSR_SF_ID_256 = 1,	/* 1 */
	D11_BSR_SF_ID_2048 = 2,	/* 2 */
	D11_BSR_SF_ID_32768 = 3	/* 3 */
};

enum {
	D11_PING_BLOCK_VALID = 0,		/* 0 */
	D11_PONG_BLOCK_VALID = 1,		/* 1 */
	D11_UC_READING_PING_BLOCK = 2,	/* 2 */
	D11_UC_READING_PONG_BLOCK = 3	/* 3 */
};

enum {
	D11_BSR_TID0_POS = 0,	/* 0  */
	D11_BSR_TID1_POS = 1,	/* 1 */
	D11_BSR_TID2_POS = 2,	/* 2 */
	D11_BSR_TID3_POS = 3,	/* 3 */
	D11_BSR_TID4_POS = 4,	/* 4 */
	D11_BSR_TID5_POS = 5,	/* 5 */
	D11_BSR_TID6_POS = 6,	/* 6 */
	D11_BSR_TID7_POS = 7,	/* 7 */
	D11_BSR_WD0_POS = 8,	/* 8 */
	D11_BSR_WD1_POS = 9,	/* 9 */
};

#define D11_IS_PING_PONG_IN_RESET(i)	(((i) & ((1 << D11_PING_BLOCK_VALID) |\
	(1 << D11_UC_READING_PING_BLOCK) | (1 << D11_PONG_BLOCK_VALID) |\
	(1 << D11_UC_READING_PONG_BLOCK))) == 0)
#define D11_PING_BLOCK_VALID_MASK		((1 << D11_PONG_BLOCK_VALID) |\
		(1 << D11_UC_READING_PING_BLOCK))
#define D11_PONG_BLOCK_VALID_MASK		((1 << D11_PING_BLOCK_VALID) |\
		(1 << D11_UC_READING_PONG_BLOCK))
#define D11_PING_PONG_UPDATE_MASK		((1 << D11_PING_BLOCK_VALID) |\
		(1 << D11_PONG_BLOCK_VALID))
#define D11_IS_PING_BLOCK_WRITABLE(i)	(((i) & D11_PING_BLOCK_VALID_MASK) == \
		(1 << D11_PONG_BLOCK_VALID))
#define D11_IS_PONG_BLOCK_WRITABLE(i)	(((i) & D11_PONG_BLOCK_VALID_MASK) == \
		(1 << D11_PING_BLOCK_VALID))
#define D11_SET_PING_BLOCK_VALID(i)		(((i) & ~(1 << D11_PONG_BLOCK_VALID)) |\
		(1 << D11_PING_BLOCK_VALID))
#define D11_SET_PONG_BLOCK_VALID(i)		(((i) & ~(1 << D11_PING_BLOCK_VALID)) |\
		(1 << D11_PONG_BLOCK_VALID))
#define D11_SET_PING_PONG_INVALID(i)		(((i) & ~(1 << D11_PING_BLOCK_VALID)) |\
		((i) & ~(1 << D11_PONG_BLOCK_VALID)))

/* valid rx plcp check */
#define PLCP_VALID(plcp) (((plcp)[0] | (plcp)[1] | (plcp)[2]) != 0)
enum {
	D11_TXTRIG_EN = 0, /* 0 */
	D11_TXTRIG_PROG = 1, /* 1 */
	D11_TXTRIG_DONE = 2, /* 2 */
	D11_TXTRIG_TYPE = 4, /* 4 */
};

#define D11_SET_TXTRIG_EN	(1 << D11_TXTRIG_EN)
#define D11_TXTRIG_TYPE_MASK	((1 << D11_TXTRIG_TYPE) | (1 << (D11_TXTRIG_TYPE+1)))
#define D11_SET_TXTRIG_TYPE(i)	(((i) << D11_TXTRIG_TYPE) & D11_TXTRIG_TYPE_MASK)

enum {
	D11_MUEDCA_AIFSN = 0, /* 0 */
	D11_MUEDCA_CWMIN = 1, /* 1 */
	D11_MUEDCA_CWMAX = 2, /* 2 */
	D11_MUEDCA_TIMER = 3, /* 3 */
	D11_MUEDCA_SU_AIFSN = 4, /* 4 */
	D11_MUEDCA_SU_CWMIN = 5, /* 5 */
	D11_MUEDCA_SU_CWMAX = 6,  /* 6 */
	D11_MUEDCA_EXPIRY_TSF = 7, /* 7 */
	D11_MUEDCA_QINFO = 8, /* 8 */
	D11_MUEDCA_STAT = 9, /* 9 */
	D11_MUEDCA_BLK_SIZE = 10 /* 10 */
};
#define D11_MUEDCA_BLK(x, idx, offset) (M_MUEDCA_BLK((x)) +\
	(idx * (D11_MUEDCA_BLK_SIZE << 1)) + (offset << 1))

#define D11_BSSCOLOR_VALID_SHIFT	15u
#define D11_BSSCOLOR_VALID_MASK		(1 << D11_BSSCOLOR_VALID_SHIFT)

#ifdef BCMPCIE_HP2P
/* HP2P (High Priority P2P) shared memory EDCA parameters */
typedef struct shm_hp2p_edca_params {
	uint16	txop;
	uint16	cwmin;
	uint16	cwmax;
	uint16	cwcur;
	uint16	aifs;
	uint16	bslots;
	uint16	reggap;
	uint16	status;
} shm_hp2p_edca_params_t;

#define HP2P_STATUS_NEWPARAMS	(1u << 8u)
#endif /* BCMPCIE_HP2P */

#define MAX_D11_GPIOS			16

/* Workaround register */
#define WAR_TXDMA_NONMODIFIABLE_EN	0x00000010 /* For TxDMA initiated AXI reads */
#define WAR_AQMDMA_NONMODIFIABLE_EN	0x00000020 /* For AQMDMA initiated AXI reads */

/* noise cal timeout when NAN is enabled.
* 54 * 256 = ~14ms .
* smallest NAN CRB possible is 16ms..choose 14ms
* as timeout to ensure noise cal happens within this 16ms
*/
#define M_NOISE_CALTIMEOUT_FOR_NAN			54u

#define TXPU_CMD_SET		1u /**< txpu set command */

#endif	/* _D11_H */
