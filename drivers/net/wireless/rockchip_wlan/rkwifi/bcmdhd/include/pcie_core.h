/*
 * BCM43XX PCIE core hardware definitions.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: pcie_core.h 673814 2016-12-05 06:10:24Z $
 */
#ifndef	_PCIE_CORE_H
#define	_PCIE_CORE_H

#include <sbhnddma.h>
#include <siutils.h>

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

/* PCIE Enumeration space offsets */
#define  PCIE_CORE_CONFIG_OFFSET	0x0
#define  PCIE_FUNC0_CONFIG_OFFSET	0x400
#define  PCIE_FUNC1_CONFIG_OFFSET	0x500
#define  PCIE_FUNC2_CONFIG_OFFSET	0x600
#define  PCIE_FUNC3_CONFIG_OFFSET	0x700
#define  PCIE_SPROM_SHADOW_OFFSET	0x800
#define  PCIE_SBCONFIG_OFFSET		0xE00


#define PCIEDEV_MAX_DMAS			4

/* PCIE Bar0 Address Mapping. Each function maps 16KB config space */
#define PCIE_DEV_BAR0_SIZE		0x4000
#define PCIE_BAR0_WINMAPCORE_OFFSET	0x0
#define PCIE_BAR0_EXTSPROM_OFFSET	0x1000
#define PCIE_BAR0_PCIECORE_OFFSET	0x2000
#define PCIE_BAR0_CCCOREREG_OFFSET	0x3000

/* different register spaces to access thr'u pcie indirect access */
#define PCIE_CONFIGREGS		1		/* Access to config space */
#define PCIE_PCIEREGS		2		/* Access to pcie registers */

#define PCIEDEV_HOSTADDR_MAP_BASE     0x8000000
#define PCIEDEV_HOSTADDR_MAP_WIN_MASK 0xFC000000

/* dma regs to control the flow between host2dev and dev2host  */
typedef volatile struct pcie_devdmaregs {
	dma64regs_t	tx;
	uint32		PAD[2];
	dma64regs_t	rx;
	uint32		PAD[2];
} pcie_devdmaregs_t;

#define PCIE_DB_HOST2DEV_0		0x1
#define PCIE_DB_HOST2DEV_1		0x2
#define PCIE_DB_DEV2HOST_0		0x3
#define PCIE_DB_DEV2HOST_1		0x4

/* door bell register sets */
typedef struct pcie_doorbell {
	uint32		host2dev_0;
	uint32		host2dev_1;
	uint32		dev2host_0;
	uint32		dev2host_1;
} pcie_doorbell_t;

/* Flow Ring Manager */
#define IFRM_FR_IDX_MAX	256
#define IFRM_FR_GID_MAX 4
#define IFRM_FR_DEV_MAX 8
#define IFRM_FR_TID_MAX 8
#define IFRM_FR_DEV_VALID 2

#define IFRM_VEC_REG_BITS	32

#define IFRM_FR_PER_VECREG			4
#define IFRM_FR_PER_VECREG_SHIFT	2
#define IFRM_FR_PER_VECREG_MASK		((0x1 << IFRM_FR_PER_VECREG_SHIFT) - 1)

#define IFRM_VEC_BITS_PER_FR	(IFRM_VEC_REG_BITS/IFRM_FR_PER_VECREG)

/* IFRM_DEV_0 : d11AC, IFRM_DEV_1 : d11AD */
#define IFRM_DEV_0	0
#define IFRM_DEV_1	1

#define IFRM_FR_GID_0 0
#define IFRM_FR_GID_1 1
#define IFRM_FR_GID_2 2
#define IFRM_FR_GID_3 3

#define IFRM_TIDMASK 0xffffffff

/* ifrm_ctrlst register */
#define IFRM_EN (1<<0)
#define IFRM_BUFF_INIT_DONE (1<<1)
#define IFRM_COMPARE_EN0 (1<<4)
#define IFRM_COMPARE_EN1 (1<<5)
#define IFRM_COMPARE_EN2 (1<<6)
#define IFRM_COMPARE_EN3 (1<<7)
#define IFRM_INIT_DV0 (1<<8)
#define IFRM_INIT_DV1 (1<<9)
#define IFRM_INIT_DV2 (1<<10)
#define IFRM_INIT_DV3 (1<<11)

/* ifrm_msk_arr.addr, ifrm_tid_arr.addr register */
#define IFRM_ADDR_SHIFT 0
#define IFRM_FRG_ID_SHIFT 8

/* ifrm_vec.diff_lat register */
#define IFRM_DV_LAT			(1<<0)
#define IFRM_DV_LAT_DONE	(1<<1)
#define IFRM_SDV_OFFSET_SHIFT	4
#define IFRM_SDV_FRGID_SHIFT	8
#define IFRM_VECSTAT_MASK		0x3
#define IFRM_VEC_MASK			0xff

/* idma frm array */
typedef struct pcie_ifrm_array {
	uint32		addr;
	uint32		data;
} pcie_ifrm_array_t;

/* idma frm vector */
typedef struct pcie_ifrm_vector {
	uint32		diff_lat;
	uint32		sav_tid;
	uint32		sav_diff;
	uint32		PAD[1];
} pcie_ifrm_vector_t;

/* idma frm interrupt */
typedef struct pcie_ifrm_intr {
	uint32		intstat;
	uint32		intmask;
} pcie_ifrm_intr_t;

/* SB side: PCIE core and host control registers */
typedef volatile struct sbpcieregs {
	uint32 control;		/* host mode only */
	uint32 iocstatus;	/* PCIE2: iostatus */
	uint32 PAD[1];
	uint32 biststatus;	/* bist Status: 0x00C */
	uint32 gpiosel;		/* PCIE gpio sel: 0x010 */
	uint32 gpioouten;	/* PCIE gpio outen: 0x14 */
	uint32 PAD[2];
	uint32 intstatus;	/* Interrupt status: 0x20 */
	uint32 intmask;		/* Interrupt mask: 0x24 */
	uint32 sbtopcimailbox;	/* sb to pcie mailbox: 0x028 */
	uint32 obffcontrol;	/* PCIE2: 0x2C */
	uint32 obffintstatus;	/* PCIE2: 0x30 */
	uint32 obffdatastatus;	/* PCIE2: 0x34 */
	uint32 PAD[1];
	uint32 ctoctrl;		/* PCIE2: 0x3C */
	uint32 errlog;		/* PCIE2: 0x40 */
	uint32 errlogaddr;	/* PCIE2: 0x44 */
	uint32 mailboxint;	/* PCIE2: 0x48 */
	uint32 mailboxintmsk; /* PCIE2: 0x4c */
	uint32 ltrspacing;	/* PCIE2: 0x50 */
	uint32 ltrhysteresiscnt;	/* PCIE2: 0x54 */
	uint32 msivectorassign;	/* PCIE2: 0x58 */
	uint32 intmask2;	/* PCIE2: 0x5C */
	uint32 PAD[40];
	uint32 sbtopcie0;	/* sb to pcie translation 0: 0x100 */
	uint32 sbtopcie1;	/* sb to pcie translation 1: 0x104 */
	uint32 sbtopcie2;	/* sb to pcie translation 2: 0x108 */
	uint32 PAD[5];

	/* pcie core supports in direct access to config space */
	uint32 configaddr;	/* pcie config space access: Address field: 0x120 */
	uint32 configdata;	/* pcie config space access: Data field: 0x124 */
	union {
		struct {
			/* mdio access to serdes */
			uint32 mdiocontrol;	/* controls the mdio access: 0x128 */
			uint32 mdiodata;	/* Data to the mdio access: 0x12c */
			/* pcie protocol phy/dllp/tlp register indirect access mechanism */
			uint32 pcieindaddr; /* indirect access to the internal register: 0x130 */
			uint32 pcieinddata;	/* Data to/from the internal regsiter: 0x134 */
			uint32 clkreqenctrl;	/* >= rev 6, Clkreq rdma control : 0x138 */
			uint32 PAD[177];
			/* 0x400 - 0x7FF, PCIE Cfg Space, note: not used anymore in PcieGen2 */
			uint32 pciecfg[4][64];
		} pcie1;
		struct {
			/* mdio access to serdes */
			uint32 mdiocontrol;	/* controls the mdio access: 0x128 */
			uint32 mdiowrdata;	/* write data to mdio 0x12C */
			uint32 mdiorddata;	/* read data to mdio 0x130 */
			uint32	PAD[3]; 	/* 0x134-0x138-0x13c */
			/* door bell registers available from gen2 rev5 onwards */
			pcie_doorbell_t	   dbls[PCIEDEV_MAX_DMAS]; /* 0x140 - 0x17F */
			uint32	dataintf;	/* 0x180 */
			uint32  PAD[1];		/* 0x184 */
			uint32	d2h_intrlazy_0; /* 0x188 */
			uint32	h2d_intrlazy_0; /* 0x18c */
			uint32  h2d_intstat_0;  /* 0x190 */
			uint32  h2d_intmask_0;	/* 0x194 */
			uint32  d2h_intstat_0;  /* 0x198 */
			uint32  d2h_intmask_0;  /* 0x19c */
			uint32	ltr_state;	/* 0x1A0 */
			uint32	pwr_int_status;	/* 0x1A4 */
			uint32	pwr_int_mask;	/* 0x1A8 */
			uint32	pme_source; /* 0x1AC */
			uint32	err_hdr_logreg1; /* 0x1B0 */
			uint32	err_hdr_logreg2; /* 0x1B4 */
			uint32	err_hdr_logreg3; /* 0x1B8 */
			uint32	err_hdr_logreg4; /* 0x1BC */
			uint32	err_code_logreg; /* 0x1C0 */
			uint32  PAD[7]; /* 0x1C4 - 0x1DF */
			uint32  clk_ctl_st;	/* 0x1E0 */
			uint32  PAD[1];		/* 0x1E4 */
			uint32	powerctl;	/* 0x1E8 */
			uint32  PAD[5];		/* 0x1EC - 0x1FF */
			pcie_devdmaregs_t  h2d0_dmaregs; /* 0x200 - 0x23c */
			pcie_devdmaregs_t  d2h0_dmaregs; /* 0x240 - 0x27c */
			pcie_devdmaregs_t  h2d1_dmaregs; /* 0x280 - 0x2bc */
			pcie_devdmaregs_t  d2h1_dmaregs; /* 0x2c0 - 0x2fc */
			pcie_devdmaregs_t  h2d2_dmaregs; /* 0x300 - 0x33c */
			pcie_devdmaregs_t  d2h2_dmaregs; /* 0x340 - 0x37c */
			pcie_devdmaregs_t  h2d3_dmaregs; /* 0x380 - 0x3bc */
			pcie_devdmaregs_t  d2h3_dmaregs; /* 0x3c0 - 0x3fc */
			uint32	d2h_intrlazy_1; /* 0x400 */
			uint32	h2d_intrlazy_1; /* 0x404 */
			uint32	h2d_intstat_1;	/* 0x408 */
			uint32	h2d_intmask_1;	/* 0x40c */
			uint32	d2h_intstat_1;	/* 0x410 */
			uint32	d2h_intmask_1;	/* 0x414 */
			uint32	PAD[2];			/* 0x418 - 0x41C */
			uint32	d2h_intrlazy_2; /* 0x420 */
			uint32	h2d_intrlazy_2; /* 0x424 */
			uint32	h2d_intstat_2;	/* 0x428 */
			uint32	h2d_intmask_2;	/* 0x42c */
			uint32	d2h_intstat_2;	/* 0x430 */
			uint32	d2h_intmask_2;	/* 0x434 */
			uint32	PAD[10];		/* 0x438 - 0x45F */
			uint32	ifrm_ctrlst;	/* 0x460 */
			uint32	PAD[1];			/* 0x464 */
			pcie_ifrm_array_t	ifrm_msk_arr;		/* 0x468 - 0x46F */
			pcie_ifrm_array_t	ifrm_tid_arr[IFRM_FR_DEV_VALID];
				/* 0x470 - 0x47F */
			pcie_ifrm_vector_t	ifrm_vec[IFRM_FR_DEV_MAX];
				/* 0x480 - 0x4FF */
			pcie_ifrm_intr_t	ifrm_intr[IFRM_FR_DEV_MAX];
				/* 0x500 - 0x53F */
			uint32	PAD[48];		/* 0x540 - 0x5FF */
			uint32	PAD[2][64];		/* 0x600 - 0x7FF */
		} pcie2;
	} u;
	uint16	sprom[64];		/* SPROM shadow Area : 0x800 - 0x880 */
	uint32	PAD[96];		/* 0x880 - 0x9FF */
	/* direct memory access (pcie2 rev19 and after) : 0xA00 - 0xAFF */
	uint32		PAD[16];	/* 0xA00 - 0xA3F */
	uint32		dm_errlog;	/* 0xA40 */
	uint32		dm_erraddr;	/* 0xA44 */
	uint32		PAD[37];	/* 0xA48 - 0xADC */
	uint32		dm_clk_ctl_st;	/* 0xAE0 */
	uint32		PAD[1];		/* 0xAE4 */
	uint32		dm_powerctl;	/* 0xAE8 */
} sbpcieregs_t;

#define PCIE_CFG_DA_OFFSET 0x400	/* direct access register offset for configuration space */

/* PCI control */
#define PCIE_RST_OE	0x01	/* When set, drives PCI_RESET out to pin */
#define PCIE_RST	0x02	/* Value driven out to pin */
#define PCIE_SPERST	0x04	/* SurvivePeRst */
#define PCIE_FORCECFGCLKON_ALP	0x08
#define PCIE_DISABLE_L1CLK_GATING	0x10
#define PCIE_DLYPERST	0x100	/* Delay PeRst to CoE Core */
#define PCIE_DISSPROMLD	0x200	/* DisableSpromLoadOnPerst */
#define PCIE_WakeModeL2	0x1000	/* Wake on L2 */
#define PCIE_MULTIMSI_EN		0x2000	/* enable multi-vector MSI messages */
#define PCIE_PipeIddqDisable0	0x8000	/* Disable assertion of pcie_pipe_iddq during L1.2 and L2 */
#define PCIE_PipeIddqDisable1	0x10000	/* Disable assertion of pcie_pipe_iddq during L2 */
#define PCIE_MSI_B2B_EN		0x100000	/* enable back-to-back MSI messages */
#define PCIE_MSI_FIFO_CLEAR	0x200000	/* reset MSI FIFO */
#define PCIE_IDMA_MODE_EN		0x800000	/* implicit M2M DMA mode */

#define	PCIE_CFGADDR	0x120	/* offsetof(configaddr) */
#define	PCIE_CFGDATA	0x124	/* offsetof(configdata) */
#define PCIE_SWPME_FN0	0x10000
#define PCIE_SWPME_FN0_SHF 16

/* Interrupt status/mask */
#define PCIE_INTA	0x01	/* PCIE INTA message is received */
#define PCIE_INTB	0x02	/* PCIE INTB message is received */
#define PCIE_INTFATAL	0x04	/* PCIE INTFATAL message is received */
#define PCIE_INTNFATAL	0x08	/* PCIE INTNONFATAL message is received */
#define PCIE_INTCORR	0x10	/* PCIE INTCORR message is received */
#define PCIE_INTPME	0x20	/* PCIE INTPME message is received */
#define PCIE_PERST	0x40	/* PCIE Reset Interrupt */

#define PCIE_INT_MB_FN0_0 0x0100 /* PCIE to SB Mailbox int Fn0.0 is received */
#define PCIE_INT_MB_FN0_1 0x0200 /* PCIE to SB Mailbox int Fn0.1 is received */
#define PCIE_INT_MB_FN1_0 0x0400 /* PCIE to SB Mailbox int Fn1.0 is received */
#define PCIE_INT_MB_FN1_1 0x0800 /* PCIE to SB Mailbox int Fn1.1 is received */
#define PCIE_INT_MB_FN2_0 0x1000 /* PCIE to SB Mailbox int Fn2.0 is received */
#define PCIE_INT_MB_FN2_1 0x2000 /* PCIE to SB Mailbox int Fn2.1 is received */
#define PCIE_INT_MB_FN3_0 0x4000 /* PCIE to SB Mailbox int Fn3.0 is received */
#define PCIE_INT_MB_FN3_1 0x8000 /* PCIE to SB Mailbox int Fn3.1 is received */

/* PCIE MSI Vector Assignment register */
#define MSIVEC_MB_0	(0x1 << 1) /* MSI Vector offset for mailbox0 is 2 */
#define MSIVEC_MB_1	(0x1 << 2) /* MSI Vector offset for mailbox1 is 3 */
#define MSIVEC_D2H0_DB0	(0x1 << 3) /* MSI Vector offset for interface0 door bell 0 is 4 */
#define MSIVEC_D2H0_DB1	(0x1 << 4) /* MSI Vector offset for interface0 door bell 1 is 5 */

/* PCIE MailboxInt/MailboxIntMask register */
#define PCIE_MB_TOSB_FN0_0   	0x0001 /* write to assert PCIEtoSB Mailbox interrupt */
#define PCIE_MB_TOSB_FN0_1   	0x0002
#define PCIE_MB_TOSB_FN1_0   	0x0004
#define PCIE_MB_TOSB_FN1_1   	0x0008
#define PCIE_MB_TOSB_FN2_0   	0x0010
#define PCIE_MB_TOSB_FN2_1   	0x0020
#define PCIE_MB_TOSB_FN3_0   	0x0040
#define PCIE_MB_TOSB_FN3_1   	0x0080
#define PCIE_MB_TOPCIE_FN0_0 	0x0100 /* int status/mask for SBtoPCIE Mailbox interrupts */
#define PCIE_MB_TOPCIE_FN0_1 	0x0200
#define PCIE_MB_TOPCIE_FN1_0 	0x0400
#define PCIE_MB_TOPCIE_FN1_1 	0x0800
#define PCIE_MB_TOPCIE_FN2_0 	0x1000
#define PCIE_MB_TOPCIE_FN2_1 	0x2000
#define PCIE_MB_TOPCIE_FN3_0 	0x4000
#define PCIE_MB_TOPCIE_FN3_1 	0x8000
#define	PCIE_MB_TOPCIE_D2H0_DB0	0x10000
#define	PCIE_MB_TOPCIE_D2H0_DB1	0x20000
#define	PCIE_MB_TOPCIE_D2H1_DB0	0x40000
#define	PCIE_MB_TOPCIE_D2H1_DB1	0x80000
#define	PCIE_MB_TOPCIE_D2H2_DB0	0x100000
#define	PCIE_MB_TOPCIE_D2H2_DB1	0x200000
#define	PCIE_MB_TOPCIE_D2H3_DB0	0x400000
#define	PCIE_MB_TOPCIE_D2H3_DB1	0x800000

#define PCIE_MB_D2H_MB_MASK		\
	(PCIE_MB_TOPCIE_D2H0_DB0 | PCIE_MB_TOPCIE_D2H0_DB1 |	\
	PCIE_MB_TOPCIE_D2H1_DB0  | PCIE_MB_TOPCIE_D2H1_DB1 |	\
	PCIE_MB_TOPCIE_D2H2_DB0  | PCIE_MB_TOPCIE_D2H2_DB1 |	\
	PCIE_MB_TOPCIE_D2H3_DB0  | PCIE_MB_TOPCIE_D2H3_DB1)

#define SBTOPCIE0_BASE 0x08000000
#define SBTOPCIE1_BASE 0x0c000000

/* On chips with CCI-400, the small pcie 128 MB region base has shifted */
#define CCI400_SBTOPCIE0_BASE  0x20000000
#define CCI400_SBTOPCIE1_BASE  0x24000000

/* SB to PCIE translation masks */
#define SBTOPCIE0_MASK	0xfc000000
#define SBTOPCIE1_MASK	0xfc000000
#define SBTOPCIE2_MASK	0xc0000000

/* Access type bits (0:1) */
#define SBTOPCIE_MEM	0
#define SBTOPCIE_IO	1
#define SBTOPCIE_CFG0	2
#define SBTOPCIE_CFG1	3

/* Prefetch enable bit 2 */
#define SBTOPCIE_PF		4

/* Write Burst enable for memory write bit 3 */
#define SBTOPCIE_WR_BURST	8

/* config access */
#define CONFIGADDR_FUNC_MASK	0x7000
#define CONFIGADDR_FUNC_SHF	12
#define CONFIGADDR_REG_MASK	0x0FFF
#define CONFIGADDR_REG_SHF	0

#define PCIE_CONFIG_INDADDR(f, r)	((((f) & CONFIGADDR_FUNC_MASK) << CONFIGADDR_FUNC_SHF) | \
			                 (((r) & CONFIGADDR_REG_MASK) << CONFIGADDR_REG_SHF))

/* PCIE protocol regs Indirect Address */
#define PCIEADDR_PROT_MASK	0x300
#define PCIEADDR_PROT_SHF	8
#define PCIEADDR_PL_TLP		0
#define PCIEADDR_PL_DLLP	1
#define PCIEADDR_PL_PLP		2

#define	PCIE_CORE_REG_CONTROL		0x00u   /* Control     */
#define	PCIE_CORE_REG_IOSTATUS		0x04u   /* IO status   */
#define	PCIE_CORE_REG_BITSTATUS		0x0Cu   /* bitstatus   */
#define	PCIE_CORE_REG_GPIO_SEL		0x10u   /* gpio sel    */
#define	PCIE_CORE_REG_GPIO_OUT_EN	0x14u   /* gpio out en */
#define	PCIE_CORE_REG_INT_STATUS	0x20u   /* int status  */
#define	PCIE_CORE_REG_INT_MASK		0x24u   /* int mask    */
#define	PCIE_CORE_REG_SB_PCIE_MB	0x28u   /* sbpcie mb   */
#define	PCIE_CORE_REG_ERRLOG		0x40u   /* errlog      */
#define	PCIE_CORE_REG_ERR_ADDR		0x44u   /* errlog addr */
#define	PCIE_CORE_REG_MB_INTR		0x48u   /* MB intr     */
#define	PCIE_CORE_REG_SB_PCIE_0		0x100u  /* sbpcie0 map */
#define	PCIE_CORE_REG_SB_PCIE_1		0x104u  /* sbpcie1 map */
#define	PCIE_CORE_REG_SB_PCIE_2		0x108u  /* sbpcie2 map */

/* PCIE Config registers */
#define	PCIE_CFG_DEV_STS_CTRL_2		0x0d4u	/* "dev_sts_control_2  */
#define	PCIE_CFG_ADV_ERR_CAP		0x100u	/* adv_err_cap         */
#define	PCIE_CFG_UC_ERR_STS		0x104u	/* uc_err_status       */
#define	PCIE_CFG_UC_ERR_MASK		0x108u	/* ucorr_err_mask      */
#define	PCIE_CFG_UNCOR_ERR_SERV		0x10cu	/* ucorr_err_sevr      */
#define	PCIE_CFG_CORR_ERR_STS		0x110u	/* corr_err_status     */
#define	PCIE_CFG_CORR_ERR_MASK		0x114u	/* corr_err_mask       */
#define	PCIE_CFG_ADV_ERR_CTRL		0x118u	/* adv_err_cap_control */
#define	PCIE_CFG_HDR_LOG1		0x11Cu	/* header_log1         */
#define	PCIE_CFG_HDR_LOG2		0x120u	/* header_log2         */
#define	PCIE_CFG_HDR_LOG3		0x124u	/* header_log3         */
#define	PCIE_CFG_HDR_LOG4		0x128u	/* header_log4         */
#define	PCIE_CFG_PML1_SUB_CAP_ID	0x240u	/* PML1sub_capID       */
#define	PCIE_CFG_PML1_SUB_CAP_REG	0x244u	/* PML1_sub_Cap_reg    */
#define	PCIE_CFG_PML1_SUB_CTRL1		0x248u	/* PML1_sub_control1   */
#define	PCIE_CFG_PML1_SUB_CTRL3		0x24Cu	/* PML1_sub_control2   */
#define	PCIE_CFG_TL_CTRL_5		0x814u	/* tl_control_5        */
#define	PCIE_CFG_PHY_ERR_ATT_VEC	0x1820u	/* phy_err_attn_vec    */
#define	PCIE_CFG_PHY_ERR_ATT_MASK	0x1824u	/* phy_err_attn_mask   */

/* PCIE protocol PHY diagnostic registers */
#define	PCIE_PLP_MODEREG		0x200u /* Mode */
#define	PCIE_PLP_STATUSREG		0x204u /* Status */
#define PCIE_PLP_LTSSMCTRLREG		0x208u /* LTSSM control */
#define PCIE_PLP_LTLINKNUMREG		0x20cu /* Link Training Link number */
#define PCIE_PLP_LTLANENUMREG		0x210u /* Link Training Lane number */
#define PCIE_PLP_LTNFTSREG		0x214u /* Link Training N_FTS */
#define PCIE_PLP_ATTNREG		0x218u /* Attention */
#define PCIE_PLP_ATTNMASKREG		0x21Cu /* Attention Mask */
#define PCIE_PLP_RXERRCTR		0x220u /* Rx Error */
#define PCIE_PLP_RXFRMERRCTR		0x224u /* Rx Framing Error */
#define PCIE_PLP_RXERRTHRESHREG		0x228u /* Rx Error threshold */
#define PCIE_PLP_TESTCTRLREG		0x22Cu /* Test Control reg */
#define PCIE_PLP_SERDESCTRLOVRDREG	0x230u /* SERDES Control Override */
#define PCIE_PLP_TIMINGOVRDREG		0x234u /* Timing param override */
#define PCIE_PLP_RXTXSMDIAGREG		0x238u /* RXTX State Machine Diag */
#define PCIE_PLP_LTSSMDIAGREG		0x23Cu /* LTSSM State Machine Diag */

/* PCIE protocol DLLP diagnostic registers */
#define PCIE_DLLP_LCREG			0x100u /* Link Control */
#define PCIE_DLLP_LSREG			0x104u /* Link Status */
#define PCIE_DLLP_LAREG			0x108u /* Link Attention */
#define PCIE_DLLP_LAMASKREG		0x10Cu /* Link Attention Mask */
#define PCIE_DLLP_NEXTTXSEQNUMREG	0x110u /* Next Tx Seq Num */
#define PCIE_DLLP_ACKEDTXSEQNUMREG	0x114u /* Acked Tx Seq Num */
#define PCIE_DLLP_PURGEDTXSEQNUMREG	0x118u /* Purged Tx Seq Num */
#define PCIE_DLLP_RXSEQNUMREG		0x11Cu /* Rx Sequence Number */
#define PCIE_DLLP_LRREG			0x120u /* Link Replay */
#define PCIE_DLLP_LACKTOREG		0x124u /* Link Ack Timeout */
#define PCIE_DLLP_PMTHRESHREG		0x128u /* Power Management Threshold */
#define PCIE_DLLP_RTRYWPREG		0x12Cu /* Retry buffer write ptr */
#define PCIE_DLLP_RTRYRPREG		0x130u /* Retry buffer Read ptr */
#define PCIE_DLLP_RTRYPPREG		0x134u /* Retry buffer Purged ptr */
#define PCIE_DLLP_RTRRWREG		0x138u /* Retry buffer Read/Write */
#define PCIE_DLLP_ECTHRESHREG		0x13Cu /* Error Count Threshold */
#define PCIE_DLLP_TLPERRCTRREG		0x140u /* TLP Error Counter */
#define PCIE_DLLP_ERRCTRREG		0x144u /* Error Counter */
#define PCIE_DLLP_NAKRXCTRREG		0x148u /* NAK Received Counter */
#define PCIE_DLLP_TESTREG		0x14Cu /* Test */
#define PCIE_DLLP_PKTBIST		0x150u /* Packet BIST */
#define PCIE_DLLP_PCIE11		0x154u /* DLLP PCIE 1.1 reg */

#define PCIE_DLLP_LSREG_LINKUP		(1u << 16u)

/* PCIE protocol TLP diagnostic registers */
#define PCIE_TLP_CONFIGREG		0x000u /* Configuration */
#define PCIE_TLP_WORKAROUNDSREG		0x004u /* TLP Workarounds */
#define PCIE_TLP_WRDMAUPPER		0x010u /* Write DMA Upper Address */
#define PCIE_TLP_WRDMALOWER		0x014u /* Write DMA Lower Address */
#define PCIE_TLP_WRDMAREQ_LBEREG	0x018u /* Write DMA Len/ByteEn Req */
#define PCIE_TLP_RDDMAUPPER		0x01Cu /* Read DMA Upper Address */
#define PCIE_TLP_RDDMALOWER		0x020u /* Read DMA Lower Address */
#define PCIE_TLP_RDDMALENREG		0x024u /* Read DMA Len Req */
#define PCIE_TLP_MSIDMAUPPER		0x028u /* MSI DMA Upper Address */
#define PCIE_TLP_MSIDMALOWER		0x02Cu /* MSI DMA Lower Address */
#define PCIE_TLP_MSIDMALENREG		0x030u /* MSI DMA Len Req */
#define PCIE_TLP_SLVREQLENREG		0x034u /* Slave Request Len */
#define PCIE_TLP_FCINPUTSREQ		0x038u /* Flow Control Inputs */
#define PCIE_TLP_TXSMGRSREQ		0x03Cu /* Tx StateMachine and Gated Req */
#define PCIE_TLP_ADRACKCNTARBLEN	0x040u /* Address Ack XferCnt and ARB Len */
#define PCIE_TLP_DMACPLHDR0		0x044u /* DMA Completion Hdr 0 */
#define PCIE_TLP_DMACPLHDR1		0x048u /* DMA Completion Hdr 1 */
#define PCIE_TLP_DMACPLHDR2		0x04Cu /* DMA Completion Hdr 2 */
#define PCIE_TLP_DMACPLMISC0		0x050u /* DMA Completion Misc0 */
#define PCIE_TLP_DMACPLMISC1		0x054u /* DMA Completion Misc1 */
#define PCIE_TLP_DMACPLMISC2		0x058u /* DMA Completion Misc2 */
#define PCIE_TLP_SPTCTRLLEN		0x05Cu /* Split Controller Req len */
#define PCIE_TLP_SPTCTRLMSIC0		0x060u /* Split Controller Misc 0 */
#define PCIE_TLP_SPTCTRLMSIC1		0x064u /* Split Controller Misc 1 */
#define PCIE_TLP_BUSDEVFUNC		0x068u /* Bus/Device/Func */
#define PCIE_TLP_RESETCTR		0x06Cu /* Reset Counter */
#define PCIE_TLP_RTRYBUF		0x070u /* Retry Buffer value */
#define PCIE_TLP_TGTDEBUG1		0x074u /* Target Debug Reg1 */
#define PCIE_TLP_TGTDEBUG2		0x078u /* Target Debug Reg2 */
#define PCIE_TLP_TGTDEBUG3		0x07Cu /* Target Debug Reg3 */
#define PCIE_TLP_TGTDEBUG4		0x080u /* Target Debug Reg4 */

/* PCIE2 MDIO register offsets */
#define PCIE2_MDIO_CONTROL    0x128
#define PCIE2_MDIO_WR_DATA    0x12C
#define PCIE2_MDIO_RD_DATA    0x130


/* MDIO control */
#define MDIOCTL_DIVISOR_MASK		0x7fu	/* clock to be used on MDIO */
#define MDIOCTL_DIVISOR_VAL		0x2u
#define MDIOCTL_PREAM_EN		0x80u	/* Enable preamble sequnce */
#define MDIOCTL_ACCESS_DONE		0x100u   /* Tranaction complete */

/* MDIO Data */
#define MDIODATA_MASK			0x0000ffff	/* data 2 bytes */
#define MDIODATA_TA			0x00020000	/* Turnaround */
#define MDIODATA_REGADDR_SHF_OLD	18		/* Regaddr shift (rev < 10) */
#define MDIODATA_REGADDR_MASK_OLD	0x003c0000	/* Regaddr Mask (rev < 10) */
#define MDIODATA_DEVADDR_SHF_OLD	22		/* Physmedia devaddr shift (rev < 10) */
#define MDIODATA_DEVADDR_MASK_OLD	0x0fc00000	/* Physmedia devaddr Mask (rev < 10) */
#define MDIODATA_REGADDR_SHF		18		/* Regaddr shift */
#define MDIODATA_REGADDR_MASK		0x007c0000	/* Regaddr Mask */
#define MDIODATA_DEVADDR_SHF		23		/* Physmedia devaddr shift */
#define MDIODATA_DEVADDR_MASK		0x0f800000	/* Physmedia devaddr Mask */
#define MDIODATA_WRITE			0x10000000	/* write Transaction */
#define MDIODATA_READ			0x20000000	/* Read Transaction */
#define MDIODATA_START			0x40000000	/* start of Transaction */

#define MDIODATA_DEV_ADDR		0x0		/* dev address for serdes */
#define	MDIODATA_BLK_ADDR		0x1F		/* blk address for serdes */

/* MDIO control/wrData/rdData register defines for PCIE Gen 2 */
#define MDIOCTL2_DIVISOR_MASK		0x7f	/* clock to be used on MDIO */
#define MDIOCTL2_DIVISOR_VAL		0x2
#define MDIOCTL2_REGADDR_SHF		8		/* Regaddr shift */
#define MDIOCTL2_REGADDR_MASK		0x00FFFF00	/* Regaddr Mask */
#define MDIOCTL2_DEVADDR_SHF		24		/* Physmedia devaddr shift */
#define MDIOCTL2_DEVADDR_MASK		0x0f000000	/* Physmedia devaddr Mask */
#define MDIOCTL2_SLAVE_BYPASS		0x10000000	/* IP slave bypass */
#define MDIOCTL2_READ			0x20000000	/* IP slave bypass */

#define MDIODATA2_DONE			0x80000000u	/* rd/wr transaction done */
#define MDIODATA2_MASK			0x7FFFFFFF	/* rd/wr transaction data */
#define MDIODATA2_DEVADDR_SHF		4		/* Physmedia devaddr shift */


/* MDIO devices (SERDES modules)
 *  unlike old pcie cores (rev < 10), rev10 pcie serde organizes registers into a few blocks.
 *  two layers mapping (blockidx, register offset) is required
 */
#define MDIO_DEV_IEEE0		0x000
#define MDIO_DEV_IEEE1		0x001
#define MDIO_DEV_BLK0		0x800
#define MDIO_DEV_BLK1		0x801
#define MDIO_DEV_BLK2		0x802
#define MDIO_DEV_BLK3		0x803
#define MDIO_DEV_BLK4		0x804
#define MDIO_DEV_TXPLL		0x808	/* TXPLL register block idx */
#define MDIO_DEV_TXCTRL0	0x820
#define MDIO_DEV_SERDESID	0x831
#define MDIO_DEV_RXCTRL0	0x840


/* XgxsBlk1_A Register Offsets */
#define BLK1_PWR_MGMT0		0x16
#define BLK1_PWR_MGMT1		0x17
#define BLK1_PWR_MGMT2		0x18
#define BLK1_PWR_MGMT3		0x19
#define BLK1_PWR_MGMT4		0x1A

/* serdes regs (rev < 10) */
#define MDIODATA_DEV_PLL       		0x1d	/* SERDES PLL Dev */
#define MDIODATA_DEV_TX        		0x1e	/* SERDES TX Dev */
#define MDIODATA_DEV_RX        		0x1f	/* SERDES RX Dev */
	/* SERDES RX registers */
#define SERDES_RX_CTRL			1	/* Rx cntrl */
#define SERDES_RX_TIMER1		2	/* Rx Timer1 */
#define SERDES_RX_CDR			6	/* CDR */
#define SERDES_RX_CDRBW			7	/* CDR BW */

	/* SERDES RX control register */
#define SERDES_RX_CTRL_FORCE		0x80	/* rxpolarity_force */
#define SERDES_RX_CTRL_POLARITY		0x40	/* rxpolarity_value */

	/* SERDES PLL registers */
#define SERDES_PLL_CTRL                 1       /* PLL control reg */
#define PLL_CTRL_FREQDET_EN             0x4000  /* bit 14 is FREQDET on */

/* Power management threshold */
#define PCIE_L0THRESHOLDTIME_MASK       0xFF00u	/* bits 0 - 7 */
#define PCIE_L1THRESHOLDTIME_MASK       0xFF00u	/* bits 8 - 15 */
#define PCIE_L1THRESHOLDTIME_SHIFT      8	/* PCIE_L1THRESHOLDTIME_SHIFT */
#define PCIE_L1THRESHOLD_WARVAL         0x72	/* WAR value */
#define PCIE_ASPMTIMER_EXTEND		0x01000000	/* > rev7: enable extend ASPM timer */

/* SPROM offsets */
#define SRSH_ASPM_OFFSET		4	/* word 4 */
#define SRSH_ASPM_ENB			0x18	/* bit 3, 4 */
#define SRSH_ASPM_L1_ENB		0x10	/* bit 4 */
#define SRSH_ASPM_L0s_ENB		0x8	/* bit 3 */
#define SRSH_PCIE_MISC_CONFIG		5	/* word 5 */
#define SRSH_L23READY_EXIT_NOPERST	0x8000u	/* bit 15 */
#define SRSH_CLKREQ_OFFSET_REV5		20	/* word 20 for srom rev <= 5 */
#define SRSH_CLKREQ_OFFSET_REV8		52	/* word 52 for srom rev 8 */
#define SRSH_CLKREQ_ENB			0x0800	/* bit 11 */
#define SRSH_BD_OFFSET                  6       /* word 6 */
#define SRSH_AUTOINIT_OFFSET            18      /* auto initialization enable */

/* PCI Capability ID's
 * Reference include/linux/pci_regs.h
 * #define  PCI_CAP_LIST_ID	0       // Capability ID
 * #define  PCI_CAP_ID_PM		0x01    // Power Management
 * #define  PCI_CAP_ID_AGP		0x02    // Accelerated Graphics Port
 * #define  PCI_CAP_ID_VPD		0x03    // Vital Product Data
 * #define  PCI_CAP_ID_SLOTID	0x04    // Slot Identification
 * #define  PCI_CAP_ID_MSI		0x05    // Message Signalled Interrupts
 * #define  PCI_CAP_ID_CHSWP       0x06    // CompactPCI HotSwap
 * #define  PCI_CAP_ID_PCIX        0x07    // PCI-X
 * #define  PCI_CAP_ID_HT          0x08    // HyperTransport
 * #define  PCI_CAP_ID_VNDR        0x09    // Vendor-Specific
 * #define  PCI_CAP_ID_DBG         0x0A    // Debug port
 * #define  PCI_CAP_ID_CCRC        0x0B    // CompactPCI Central Resource Control
 * #define  PCI_CAP_ID_SHPC        0x0C    // PCI Standard Hot-Plug Controller
 * #define  PCI_CAP_ID_SSVID       0x0D    // Bridge subsystem vendor/device ID
 * #define  PCI_CAP_ID_AGP3        0x0E    // AGP Target PCI-PCI bridge
 * #define  PCI_CAP_ID_SECDEV      0x0F    // Secure Device
 * #define  PCI_CAP_ID_MSIX        0x11    // MSI-X
 * #define  PCI_CAP_ID_SATA        0x12    // SATA Data/Index Conf.
 * #define  PCI_CAP_ID_AF          0x13    // PCI Advanced Features
 * #define  PCI_CAP_ID_EA          0x14    // PCI Enhanced Allocation
 * #define  PCI_CAP_ID_MAX         PCI_CAP_ID_EA
 */

#define  PCIE_CAP_ID_EXP         0x10    // PCI Express

/* PCIe Capabilities Offsets
 * Reference include/linux/pci_regs.h
 * #define PCIE_CAP_FLAGS           2       // Capabilities register
 * #define PCIE_CAP_DEVCAP          4       // Device capabilities
 * #define PCIE_CAP_DEVCTL          8       // Device Control
 * #define PCIE_CAP_DEVSTA          10      // Device Status
 * #define PCIE_CAP_LNKCAP          12      // Link Capabilities
 * #define PCIE_CAP_LNKCTL          16      // Link Control
 * #define PCIE_CAP_LNKSTA          18      // Link Status
 * #define PCI_CAP_EXP_ENDPOINT_SIZEOF_V1  20      // v1 endpoints end here
 * #define PCIE_CAP_SLTCAP          20      // Slot Capabilities
 * #define PCIE_CAP_SLTCTL          24      // Slot Control
 * #define PCIE_CAP_SLTSTA          26      // Slot Status
 * #define PCIE_CAP_RTCTL           28      // Root Control
 * #define PCIE_CAP_RTCAP           30      // Root Capabilities
 * #define PCIE_CAP_RTSTA           32      // Root Status
 */


/* Linkcapability reg offset in PCIE Cap */
#define PCIE_CAP_LINKCAP_OFFSET         12      /* linkcap offset in pcie cap */
#define PCIE_CAP_LINKCAP_LNKSPEED_MASK	0xf     /* Supported Link Speeds */
#define PCIE_CAP_LINKCAP_GEN2           0x2     /* Value for GEN2 */

/* Uc_Err reg offset in AER Cap */
#define PCIE_EXTCAP_ID_ERR		0x01	/* Advanced Error Reporting */
#define PCIE_EXTCAP_AER_UCERR_OFFSET	4	/* Uc_Err reg offset in AER Cap */

/* Linkcontrol reg offset in PCIE Cap */
#define PCIE_CAP_LINKCTRL_OFFSET	16	/* linkctrl offset in pcie cap */
#define PCIE_CAP_LCREG_ASPML0s		0x01	/* ASPM L0s in linkctrl */
#define PCIE_CAP_LCREG_ASPML1		0x02	/* ASPM L1 in linkctrl */
#define PCIE_CLKREQ_ENAB		0x100	/* CLKREQ Enab in linkctrl */
#define PCIE_LINKSPEED_MASK		0xF0000u	/* bits 0 - 3 of high word */
#define PCIE_LINKSPEED_SHIFT		16	/* PCIE_LINKSPEED_SHIFT */

/* Devcontrol reg offset in PCIE Cap */
#define PCIE_CAP_DEVCTRL_OFFSET		8	/* devctrl offset in pcie cap */
#define PCIE_CAP_DEVCTRL_MRRS_MASK	0x7000	/* Max read request size mask */
#define PCIE_CAP_DEVCTRL_MRRS_SHIFT	12	/* Max read request size shift */
#define PCIE_CAP_DEVCTRL_MRRS_128B	0	/* 128 Byte */
#define PCIE_CAP_DEVCTRL_MRRS_256B	1	/* 256 Byte */
#define PCIE_CAP_DEVCTRL_MRRS_512B	2	/* 512 Byte */
#define PCIE_CAP_DEVCTRL_MRRS_1024B	3	/* 1024 Byte */
#define PCIE_CAP_DEVCTRL_MPS_MASK	0x00e0	/* Max payload size mask */
#define PCIE_CAP_DEVCTRL_MPS_SHIFT	5	/* Max payload size shift */
#define PCIE_CAP_DEVCTRL_MPS_128B	0	/* 128 Byte */
#define PCIE_CAP_DEVCTRL_MPS_256B	1	/* 256 Byte */
#define PCIE_CAP_DEVCTRL_MPS_512B	2	/* 512 Byte */
#define PCIE_CAP_DEVCTRL_MPS_1024B	3	/* 1024 Byte */

#define PCIE_ASPM_ENAB			3	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L1_ENAB		2	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_L0s_ENAB		1	/* ASPM L0s & L1 in linkctrl */
#define PCIE_ASPM_DISAB			0	/* ASPM L0s & L1 in linkctrl */

#define PCIE_ASPM_L11_ENAB		8	/* ASPM L1.1 in PML1_sub_control2 */
#define PCIE_ASPM_L12_ENAB		4	/* ASPM L1.2 in PML1_sub_control2 */

/* NumMsg and NumMsgEn in PCIE MSI Cap */
#define MSICAP_NUM_MSG_SHF		17
#define MSICAP_NUM_MSG_MASK		(0x7 << MSICAP_NUM_MSG_SHF)
#define MSICAP_NUM_MSG_EN_SHF	20
#define MSICAP_NUM_MSG_EN_MASK	(0x7 << MSICAP_NUM_MSG_EN_SHF)

/* Devcontrol2 reg offset in PCIE Cap */
#define PCIE_CAP_DEVCTRL2_OFFSET	0x28	/* devctrl2 offset in pcie cap */
#define PCIE_CAP_DEVCTRL2_LTR_ENAB_MASK	0x400	/* Latency Tolerance Reporting Enable */
#define PCIE_CAP_DEVCTRL2_OBFF_ENAB_SHIFT 13	/* Enable OBFF mechanism, select signaling method */
#define PCIE_CAP_DEVCTRL2_OBFF_ENAB_MASK 0x6000	/* Enable OBFF mechanism, select signaling method */

/* LTR registers in PCIE Cap */
#define PCIE_LTR0_REG_OFFSET	0x844u	/* ltr0_reg offset in pcie cap */
#define PCIE_LTR1_REG_OFFSET	0x848u	/* ltr1_reg offset in pcie cap */
#define PCIE_LTR2_REG_OFFSET	0x84cu	/* ltr2_reg offset in pcie cap */
#define PCIE_LTR0_REG_DEFAULT_60	0x883c883cu	/* active latency default to 60usec */
#define PCIE_LTR0_REG_DEFAULT_150	0x88968896u	/* active latency default to 150usec */
#define PCIE_LTR1_REG_DEFAULT		0x88648864u	/* idle latency default to 100usec */
#define PCIE_LTR2_REG_DEFAULT		0x90039003u	/* sleep latency default to 3msec */

/* Status reg PCIE_PLP_STATUSREG */
#define PCIE_PLP_POLARITYINV_STAT	0x10


/* PCIE BRCM Vendor CAP REVID reg  bits */
#define BRCMCAP_PCIEREV_CT_MASK			0xF00u
#define BRCMCAP_PCIEREV_CT_SHIFT		8u
#define BRCMCAP_PCIEREV_REVID_MASK		0xFFu
#define BRCMCAP_PCIEREV_REVID_SHIFT		0

#define PCIE_REVREG_CT_PCIE1		0
#define PCIE_REVREG_CT_PCIE2		1

/* PCIE GEN2 specific defines */
/* PCIE BRCM Vendor Cap offsets w.r.t to vendor cap ptr */
#define PCIE2R0_BRCMCAP_REVID_OFFSET		4
#define PCIE2R0_BRCMCAP_BAR0_WIN0_WRAP_OFFSET	8
#define PCIE2R0_BRCMCAP_BAR0_WIN2_OFFSET	12
#define PCIE2R0_BRCMCAP_BAR0_WIN2_WRAP_OFFSET	16
#define PCIE2R0_BRCMCAP_BAR0_WIN_OFFSET		20
#define PCIE2R0_BRCMCAP_BAR1_WIN_OFFSET		24
#define PCIE2R0_BRCMCAP_SPROM_CTRL_OFFSET	28
#define PCIE2R0_BRCMCAP_BAR2_WIN_OFFSET		32
#define PCIE2R0_BRCMCAP_INTSTATUS_OFFSET	36
#define PCIE2R0_BRCMCAP_INTMASK_OFFSET		40
#define PCIE2R0_BRCMCAP_PCIE2SB_MB_OFFSET	44
#define PCIE2R0_BRCMCAP_BPADDR_OFFSET		48
#define PCIE2R0_BRCMCAP_BPDATA_OFFSET		52
#define PCIE2R0_BRCMCAP_CLKCTLSTS_OFFSET	56

/* definition of configuration space registers of PCIe gen2
 * http://hwnbu-twiki.sj.broadcom.com/twiki/pub/Mwgroup/CurrentPcieGen2ProgramGuide/pcie_ep.htm
 */
#define PCIECFGREG_STATUS_CMD		0x4
#define PCIECFGREG_PM_CSR		0x4C
#define PCIECFGREG_MSI_CAP		0x58
#define PCIECFGREG_MSI_ADDR_L		0x5C
#define PCIECFGREG_MSI_ADDR_H		0x60
#define PCIECFGREG_MSI_DATA		0x64
#define PCIECFGREG_LINK_STATUS_CTRL	0xBCu
#define PCIECFGREG_DEV_STATUS_CTRL 0xB4u
#define PCIECFGGEN_DEV_STATUS_CTRL2	0xD4
#define PCIECFGREG_LINK_STATUS_CTRL2	0xDCu
#define PCIECFGREG_RBAR_CTRL		0x228
#define PCIECFGREG_PML1_SUB_CTRL1	0x248
#define PCIECFGREG_PML1_SUB_CTRL2	0x24C
#define PCIECFGREG_REG_BAR2_CONFIG	0x4E0
#define PCIECFGREG_REG_BAR3_CONFIG	0x4F4
#define PCIECFGREG_PDL_CTRL1		0x1004
#define PCIECFGREG_PDL_IDDQ		0x1814
#define PCIECFGREG_REG_PHY_CTL7		0x181c
#define PCIECFGREG_PHY_DBG_CLKREQ0		0x1E10
#define PCIECFGREG_PHY_DBG_CLKREQ1		0x1E14
#define PCIECFGREG_PHY_DBG_CLKREQ2		0x1E18
#define PCIECFGREG_PHY_DBG_CLKREQ3		0x1E1C

/* PCIECFGREG_PML1_SUB_CTRL1 Bit Definition */
#define PCI_PM_L1_2_ENA_MASK		0x00000001	/* PCI-PM L1.2 Enabled */
#define PCI_PM_L1_1_ENA_MASK		0x00000002	/* PCI-PM L1.1 Enabled */
#define ASPM_L1_2_ENA_MASK		0x00000004	/* ASPM L1.2 Enabled */
#define ASPM_L1_1_ENA_MASK		0x00000008	/* ASPM L1.1 Enabled */

/* PCIe gen2 mailbox interrupt masks */
#define I_MB    0x3
#define I_BIT0  0x1
#define I_BIT1  0x2

/* PCIE gen2 config regs */
#define PCIIntstatus	0x090
#define PCIIntmask	0x094
#define PCISBMbx	0x98

/* enumeration Core regs */
#define PCIH2D_MailBox  0x140
#define PCIH2D_DB1		0x144
#define PCID2H_MailBox  0x148
#define PCIH2D_MailBox_1	0x150  /* for dma channel1 */
#define PCIH2D_DB1_1		0x154
#define PCID2H_MailBox_1	0x158
#define PCIH2D_MailBox_2	0x160  /* for dma channel2 which will be used for Implicit DMA */
#define PCIH2D_DB1_2		0x164
#define PCID2H_MailBox_2	0x168

#define PCIMailBoxInt	0x48
#define PCIMailBoxMask	0x4C
#define PCIMSIVecAssign	0x58

#define I_F0_B0         (0x1 << 8) /* Mail box interrupt Function 0 interrupt, bit 0 */
#define I_F0_B1         (0x1 << 9) /* Mail box interrupt Function 0 interrupt, bit 1 */

#define PCIECFGREG_DEVCONTROL	0xB4
#define PCIECFGREG_BASEADDR0	0x10
#define PCIECFGREG_DEVCONTROL_MRRS_SHFT	12
#define PCIECFGREG_DEVCONTROL_MRRS_MASK	(0x7 << PCIECFGREG_DEVCONTROL_MRRS_SHFT)
#define PCIECFGREG_DEVCTRL_MPS_SHFT	5
#define PCIECFGREG_DEVCTRL_MPS_MASK (0x7 << PCIECFGREG_DEVCTRL_MPS_SHFT)
#define PCIECFGREG_PM_CSR_STATE_MASK 0x00000003
#define PCIECFGREG_PM_CSR_STATE_D0 0
#define PCIECFGREG_PM_CSR_STATE_D1 1
#define PCIECFGREG_PM_CSR_STATE_D2 2
#define PCIECFGREG_PM_CSR_STATE_D3_HOT 3
#define PCIECFGREG_PM_CSR_STATE_D3_COLD 4


/* SROM hardware region */
#define SROM_OFFSET_BAR1_CTRL  52

#define BAR1_ENC_SIZE_MASK	0x000e
#define BAR1_ENC_SIZE_SHIFT	1

#define BAR1_ENC_SIZE_1M	0
#define BAR1_ENC_SIZE_2M	1
#define BAR1_ENC_SIZE_4M	2

#define PCIEGEN2_CAP_DEVSTSCTRL2_OFFSET		0xD4
#define PCIEGEN2_CAP_DEVSTSCTRL2_LTRENAB	0x400

/*
 * Latency Tolerance Reporting (LTR) states
 * Active has the least tolerant latency requirement
 * Sleep is most tolerant
 */
#define LTR_ACTIVE				2
#define LTR_ACTIVE_IDLE				1
#define LTR_SLEEP				0
#define LTR_FINAL_MASK				0x300
#define LTR_FINAL_SHIFT				8

/* pwrinstatus, pwrintmask regs */
#define PCIEGEN2_PWRINT_D0_STATE_SHIFT		0
#define PCIEGEN2_PWRINT_D1_STATE_SHIFT		1
#define PCIEGEN2_PWRINT_D2_STATE_SHIFT		2
#define PCIEGEN2_PWRINT_D3_STATE_SHIFT		3
#define PCIEGEN2_PWRINT_L0_LINK_SHIFT		4
#define PCIEGEN2_PWRINT_L0s_LINK_SHIFT		5
#define PCIEGEN2_PWRINT_L1_LINK_SHIFT		6
#define PCIEGEN2_PWRINT_L2_L3_LINK_SHIFT	7
#define PCIEGEN2_PWRINT_OBFF_CHANGE_SHIFT	8

#define PCIEGEN2_PWRINT_D0_STATE_MASK		(1 << PCIEGEN2_PWRINT_D0_STATE_SHIFT)
#define PCIEGEN2_PWRINT_D1_STATE_MASK		(1 << PCIEGEN2_PWRINT_D1_STATE_SHIFT)
#define PCIEGEN2_PWRINT_D2_STATE_MASK		(1 << PCIEGEN2_PWRINT_D2_STATE_SHIFT)
#define PCIEGEN2_PWRINT_D3_STATE_MASK		(1 << PCIEGEN2_PWRINT_D3_STATE_SHIFT)
#define PCIEGEN2_PWRINT_L0_LINK_MASK		(1 << PCIEGEN2_PWRINT_L0_LINK_SHIFT)
#define PCIEGEN2_PWRINT_L0s_LINK_MASK		(1 << PCIEGEN2_PWRINT_L0s_LINK_SHIFT)
#define PCIEGEN2_PWRINT_L1_LINK_MASK		(1 << PCIEGEN2_PWRINT_L1_LINK_SHIFT)
#define PCIEGEN2_PWRINT_L2_L3_LINK_MASK		(1 << PCIEGEN2_PWRINT_L2_L3_LINK_SHIFT)
#define PCIEGEN2_PWRINT_OBFF_CHANGE_MASK	(1 << PCIEGEN2_PWRINT_OBFF_CHANGE_SHIFT)

/* sbtopcie mail box */
#define SBTOPCIE_MB_FUNC0_SHIFT 8
#define SBTOPCIE_MB_FUNC1_SHIFT 10
#define SBTOPCIE_MB_FUNC2_SHIFT 12
#define SBTOPCIE_MB_FUNC3_SHIFT 14

#define SBTOPCIE_MB1_FUNC0_SHIFT 9
#define SBTOPCIE_MB1_FUNC1_SHIFT 11
#define SBTOPCIE_MB1_FUNC2_SHIFT 13
#define SBTOPCIE_MB1_FUNC3_SHIFT 15

/* pcieiocstatus */
#define PCIEGEN2_IOC_D0_STATE_SHIFT		8
#define PCIEGEN2_IOC_D1_STATE_SHIFT		9
#define PCIEGEN2_IOC_D2_STATE_SHIFT		10
#define PCIEGEN2_IOC_D3_STATE_SHIFT		11
#define PCIEGEN2_IOC_L0_LINK_SHIFT		12
#define PCIEGEN2_IOC_L1_LINK_SHIFT		13
#define PCIEGEN2_IOC_L1L2_LINK_SHIFT		14
#define PCIEGEN2_IOC_L2_L3_LINK_SHIFT		15

#define PCIEGEN2_IOC_D0_STATE_MASK		(1 << PCIEGEN2_IOC_D0_STATE_SHIFT)
#define PCIEGEN2_IOC_D1_STATE_MASK		(1 << PCIEGEN2_IOC_D1_STATE_SHIFT)
#define PCIEGEN2_IOC_D2_STATE_MASK		(1 << PCIEGEN2_IOC_D2_STATE_SHIFT)
#define PCIEGEN2_IOC_D3_STATE_MASK		(1 << PCIEGEN2_IOC_D3_STATE_SHIFT)
#define PCIEGEN2_IOC_L0_LINK_MASK		(1 << PCIEGEN2_IOC_L0_LINK_SHIFT)
#define PCIEGEN2_IOC_L1_LINK_MASK		(1 << PCIEGEN2_IOC_L1_LINK_SHIFT)
#define PCIEGEN2_IOC_L1L2_LINK_MASK		(1 << PCIEGEN2_IOC_L1L2_LINK_SHIFT)
#define PCIEGEN2_IOC_L2_L3_LINK_MASK		(1 << PCIEGEN2_IOC_L2_L3_LINK_SHIFT)

/* stat_ctrl */
#define PCIE_STAT_CTRL_RESET		0x1
#define PCIE_STAT_CTRL_ENABLE		0x2
#define PCIE_STAT_CTRL_INTENABLE	0x4
#define PCIE_STAT_CTRL_INTSTATUS	0x8

/* cpl_timeout_ctrl_reg */
#define PCIE_CTO_TO_THRESHOLD_SHIFT	0
#define PCIE_CTO_TO_THRESHHOLD_MASK	(0xfffff << PCIE_CTO_TO_THRESHOLD_SHIFT)

#define PCIE_CTO_CLKCHKCNT_SHIFT		24
#define PCIE_CTO_CLKCHKCNT_MASK		(0xf << PCIE_CTO_CLKCHKCNT_SHIFT)

#define PCIE_CTO_ENAB_SHIFT			31
#define PCIE_CTO_ENAB_MASK			(0x1 << PCIE_CTO_ENAB_SHIFT)

#define PCIE_CTO_TO_THRESH_DEFAULT	0x58000
#define PCIE_CTO_CLKCHKCNT_VAL		0xA

/* ErrLog */
#define PCIE_SROMRD_ERR_SHIFT			5
#define PCIE_SROMRD_ERR_MASK			(0x1 << PCIE_SROMRD_ERR_SHIFT)

#define PCIE_CTO_ERR_SHIFT			8
#define PCIE_CTO_ERR_MASK				(0x1 << PCIE_CTO_ERR_SHIFT)

#define PCIE_CTO_ERR_CODE_SHIFT		9
#define PCIE_CTO_ERR_CODE_MASK		(0x3 << PCIE_CTO_ERR_CODE_SHIFT)

#define PCIE_BP_CLK_OFF_ERR_SHIFT		12
#define PCIE_BP_CLK_OFF_ERR_MASK		(0x1 << PCIE_BP_CLK_OFF_ERR_SHIFT)

#define PCIE_BP_IN_RESET_ERR_SHIFT	13
#define PCIE_BP_IN_RESET_ERR_MASK		(0x1 << PCIE_BP_IN_RESET_ERR_SHIFT)

#ifdef BCMDRIVER
void pcie_watchdog_reset(osl_t *osh, si_t *sih, sbpcieregs_t *sbpcieregs);
void pcie_serdes_iddqdisable(osl_t *osh, si_t *sih, sbpcieregs_t *sbpcieregs);
void pcie_set_trefup_time_100us(si_t *sih);
#endif /* BCMDRIVER */

/* DMA intstatus and intmask */
#define	I_PC		(1 << 10)	/* pci descriptor error */
#define	I_PD		(1 << 11)	/* pci data error */
#define	I_DE		(1 << 12)	/* descriptor protocol error */
#define	I_RU		(1 << 13)	/* receive descriptor underflow */
#define	I_RO		(1 << 14)	/* receive fifo overflow */
#define	I_XU		(1 << 15)	/* transmit fifo underflow */
#define	I_RI		(1 << 16)	/* receive interrupt */
#define	I_XI		(1 << 24)	/* transmit interrupt */

#endif	/* _PCIE_CORE_H */
