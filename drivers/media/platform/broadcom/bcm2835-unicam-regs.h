/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (C) 2017-2020 Raspberry Pi Trading.
 * Dave Stevenson <dave.stevenson@raspberrypi.com>
 */

#ifndef VC4_REGS_UNICAM_H
#define VC4_REGS_UNICAM_H

#include <linux/bits.h>

/*
 * The following values are taken from files found within the code drop
 * made by Broadcom for the BCM21553 Graphics Driver, predominantly in
 * brcm_usrlib/dag/vmcsx/vcinclude/hardware_vc4.h.
 * They have been modified to be only the register offset.
 */
#define UNICAM_CTRL		0x000
#define UNICAM_STA		0x004
#define UNICAM_ANA		0x008
#define UNICAM_PRI		0x00c
#define UNICAM_CLK		0x010
#define UNICAM_CLT		0x014
#define UNICAM_DAT0		0x018
#define UNICAM_DAT1		0x01c
#define UNICAM_DAT2		0x020
#define UNICAM_DAT3		0x024
#define UNICAM_DLT		0x028
#define UNICAM_CMP0		0x02c
#define UNICAM_CMP1		0x030
#define UNICAM_CAP0		0x034
#define UNICAM_CAP1		0x038
#define UNICAM_ICTL		0x100
#define UNICAM_ISTA		0x104
#define UNICAM_IDI0		0x108
#define UNICAM_IPIPE		0x10c
#define UNICAM_IBSA0		0x110
#define UNICAM_IBEA0		0x114
#define UNICAM_IBLS		0x118
#define UNICAM_IBWP		0x11c
#define UNICAM_IHWIN		0x120
#define UNICAM_IHSTA		0x124
#define UNICAM_IVWIN		0x128
#define UNICAM_IVSTA		0x12c
#define UNICAM_ICC		0x130
#define UNICAM_ICS		0x134
#define UNICAM_IDC		0x138
#define UNICAM_IDPO		0x13c
#define UNICAM_IDCA		0x140
#define UNICAM_IDCD		0x144
#define UNICAM_IDS		0x148
#define UNICAM_DCS		0x200
#define UNICAM_DBSA0		0x204
#define UNICAM_DBEA0		0x208
#define UNICAM_DBWP		0x20c
#define UNICAM_DBCTL		0x300
#define UNICAM_IBSA1		0x304
#define UNICAM_IBEA1		0x308
#define UNICAM_IDI1		0x30c
#define UNICAM_DBSA1		0x310
#define UNICAM_DBEA1		0x314
#define UNICAM_MISC		0x400

/*
 * The following bitmasks are from the kernel released by Broadcom
 * for Android - https://android.googlesource.com/kernel/bcm/
 * The Rhea, Hawaii, and Java chips all contain the same VideoCore4
 * Unicam block as BCM2835, as defined in eg
 * arch/arm/mach-rhea/include/mach/rdb_A0/brcm_rdb_cam.h and similar.
 * Values reworked to use the kernel BIT and GENMASK macros.
 *
 * Some of the bit mnenomics have been amended to match the datasheet.
 */
/* UNICAM_CTRL Register */
#define UNICAM_CPE		BIT(0)
#define UNICAM_MEM		BIT(1)
#define UNICAM_CPR		BIT(2)
#define UNICAM_CPM_MASK		GENMASK(3, 3)
#define UNICAM_CPM_CSI2		0
#define UNICAM_CPM_CCP2		1
#define UNICAM_SOE		BIT(4)
#define UNICAM_DCM_MASK		GENMASK(5, 5)
#define UNICAM_DCM_STROBE	0
#define UNICAM_DCM_DATA		1
#define UNICAM_SLS		BIT(6)
#define UNICAM_PFT_MASK		GENMASK(11, 8)
#define UNICAM_OET_MASK		GENMASK(20, 12)

/* UNICAM_STA Register */
#define UNICAM_SYN		BIT(0)
#define UNICAM_CS		BIT(1)
#define UNICAM_SBE		BIT(2)
#define UNICAM_PBE		BIT(3)
#define UNICAM_HOE		BIT(4)
#define UNICAM_PLE		BIT(5)
#define UNICAM_SSC		BIT(6)
#define UNICAM_CRCE		BIT(7)
#define UNICAM_OES		BIT(8)
#define UNICAM_IFO		BIT(9)
#define UNICAM_OFO		BIT(10)
#define UNICAM_BFO		BIT(11)
#define UNICAM_DL		BIT(12)
#define UNICAM_PS		BIT(13)
#define UNICAM_IS		BIT(14)
#define UNICAM_PI0		BIT(15)
#define UNICAM_PI1		BIT(16)
#define UNICAM_FSI_S		BIT(17)
#define UNICAM_FEI_S		BIT(18)
#define UNICAM_LCI_S		BIT(19)
#define UNICAM_BUF0_RDY		BIT(20)
#define UNICAM_BUF0_NO		BIT(21)
#define UNICAM_BUF1_RDY		BIT(22)
#define UNICAM_BUF1_NO		BIT(23)
#define UNICAM_DI		BIT(24)

#define UNICAM_STA_MASK_ALL \
	(UNICAM_SBE  | UNICAM_PBE | UNICAM_HOE | UNICAM_PLE | UNICAM_SSC | \
	 UNICAM_CRCE | UNICAM_IFO | UNICAM_OFO | UNICAM_DL  | UNICAM_PS  | \
	 UNICAM_PI0  | UNICAM_PI1)

/* UNICAM_ANA Register */
#define UNICAM_APD		BIT(0)
#define UNICAM_BPD		BIT(1)
#define UNICAM_AR		BIT(2)
#define UNICAM_DDL		BIT(3)
#define UNICAM_CTATADJ_MASK	GENMASK(7, 4)
#define UNICAM_PTATADJ_MASK	GENMASK(11, 8)

/* UNICAM_PRI Register */
#define UNICAM_PE		BIT(0)
#define UNICAM_PT_MASK		GENMASK(2, 1)
#define UNICAM_NP_MASK		GENMASK(7, 4)
#define UNICAM_PP_MASK		GENMASK(11, 8)
#define UNICAM_BS_MASK		GENMASK(15, 12)
#define UNICAM_BL_MASK		GENMASK(17, 16)

/* UNICAM_CLK Register */
#define UNICAM_CLE		BIT(0)
#define UNICAM_CLPD		BIT(1)
#define UNICAM_CLLPE		BIT(2)
#define UNICAM_CLHSE		BIT(3)
#define UNICAM_CLTRE		BIT(4)
#define UNICAM_CLAC_MASK	GENMASK(8, 5)
#define UNICAM_CLSTE		BIT(29)

/* UNICAM_CLT Register */
#define UNICAM_CLT1_MASK	GENMASK(7, 0)
#define UNICAM_CLT2_MASK	GENMASK(15, 8)

/* UNICAM_DATn Registers */
#define UNICAM_DLE		BIT(0)
#define UNICAM_DLPD		BIT(1)
#define UNICAM_DLLPE		BIT(2)
#define UNICAM_DLHSE		BIT(3)
#define UNICAM_DLTRE		BIT(4)
#define UNICAM_DLSM		BIT(5)
#define UNICAM_DLFO		BIT(28)
#define UNICAM_DLSTE		BIT(29)

#define UNICAM_DAT_MASK_ALL	(UNICAM_DLSTE | UNICAM_DLFO)

/* UNICAM_DLT Register */
#define UNICAM_DLT1_MASK	GENMASK(7, 0)
#define UNICAM_DLT2_MASK	GENMASK(15, 8)
#define UNICAM_DLT3_MASK	GENMASK(23, 16)

/* UNICAM_ICTL Register */
#define UNICAM_FSIE		BIT(0)
#define UNICAM_FEIE		BIT(1)
#define UNICAM_IBOB		BIT(2)
#define UNICAM_FCM		BIT(3)
#define UNICAM_TFC		BIT(4)
#define UNICAM_LIP_MASK		GENMASK(6, 5)
#define UNICAM_LCIE_MASK	GENMASK(28, 16)

/* UNICAM_IDI0/1 Register */
#define UNICAM_ID0_MASK		GENMASK(7, 0)
#define UNICAM_ID1_MASK		GENMASK(15, 8)
#define UNICAM_ID2_MASK		GENMASK(23, 16)
#define UNICAM_ID3_MASK		GENMASK(31, 24)

/* UNICAM_ISTA Register */
#define UNICAM_FSI		BIT(0)
#define UNICAM_FEI		BIT(1)
#define UNICAM_LCI		BIT(2)

#define UNICAM_ISTA_MASK_ALL	(UNICAM_FSI | UNICAM_FEI | UNICAM_LCI)

/* UNICAM_IPIPE Register */
#define UNICAM_PUM_MASK		GENMASK(2, 0)
/* Unpacking modes */
#define UNICAM_PUM_NONE		0
#define UNICAM_PUM_UNPACK6	1
#define UNICAM_PUM_UNPACK7	2
#define UNICAM_PUM_UNPACK8	3
#define UNICAM_PUM_UNPACK10	4
#define UNICAM_PUM_UNPACK12	5
#define UNICAM_PUM_UNPACK14	6
#define UNICAM_PUM_UNPACK16	7
#define UNICAM_DDM_MASK		GENMASK(6, 3)
#define UNICAM_PPM_MASK		GENMASK(9, 7)
/* Packing modes */
#define UNICAM_PPM_NONE		0
#define UNICAM_PPM_PACK8	1
#define UNICAM_PPM_PACK10	2
#define UNICAM_PPM_PACK12	3
#define UNICAM_PPM_PACK14	4
#define UNICAM_PPM_PACK16	5
#define UNICAM_DEM_MASK		GENMASK(11, 10)
#define UNICAM_DEBL_MASK	GENMASK(14, 12)
#define UNICAM_ICM_MASK		GENMASK(16, 15)
#define UNICAM_IDM_MASK		GENMASK(17, 17)

/* UNICAM_ICC Register */
#define UNICAM_ICFL_MASK	GENMASK(4, 0)
#define UNICAM_ICFH_MASK	GENMASK(9, 5)
#define UNICAM_ICST_MASK	GENMASK(12, 10)
#define UNICAM_ICLT_MASK	GENMASK(15, 13)
#define UNICAM_ICLL_MASK	GENMASK(31, 16)

/* UNICAM_DCS Register */
#define UNICAM_DIE		BIT(0)
#define UNICAM_DIM		BIT(1)
#define UNICAM_DBOB		BIT(3)
#define UNICAM_FDE		BIT(4)
#define UNICAM_LDP		BIT(5)
#define UNICAM_EDL_MASK		GENMASK(15, 8)

/* UNICAM_DBCTL Register */
#define UNICAM_DBEN		BIT(0)
#define UNICAM_BUF0_IE		BIT(1)
#define UNICAM_BUF1_IE		BIT(2)

/* UNICAM_CMP[0,1] register */
#define UNICAM_PCE		BIT(31)
#define UNICAM_GI		BIT(9)
#define UNICAM_CPH		BIT(8)
#define UNICAM_PCVC_MASK	GENMASK(7, 6)
#define UNICAM_PCDT_MASK	GENMASK(5, 0)

/* UNICAM_MISC register */
#define UNICAM_FL0		BIT(6)
#define UNICAM_FL1		BIT(9)

#endif
