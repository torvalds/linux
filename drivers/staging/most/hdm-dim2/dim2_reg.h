/*
 * dim2_reg.h - Definitions for registers of DIM2
 * (MediaLB, Device Interface Macro IP, OS62420)
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#ifndef DIM2_OS62420_H
#define	DIM2_OS62420_H

#include <linux/types.h>

struct dim2_regs {
	/* 0x00 */ u32 MLBC0;
	/* 0x01 */ u32 rsvd0[1];
	/* 0x02 */ u32 MLBPC0;
	/* 0x03 */ u32 MS0;
	/* 0x04 */ u32 rsvd1[1];
	/* 0x05 */ u32 MS1;
	/* 0x06 */ u32 rsvd2[2];
	/* 0x08 */ u32 MSS;
	/* 0x09 */ u32 MSD;
	/* 0x0A */ u32 rsvd3[1];
	/* 0x0B */ u32 MIEN;
	/* 0x0C */ u32 rsvd4[1];
	/* 0x0D */ u32 MLBPC2;
	/* 0x0E */ u32 MLBPC1;
	/* 0x0F */ u32 MLBC1;
	/* 0x10 */ u32 rsvd5[0x10];
	/* 0x20 */ u32 HCTL;
	/* 0x21 */ u32 rsvd6[1];
	/* 0x22 */ u32 HCMR0;
	/* 0x23 */ u32 HCMR1;
	/* 0x24 */ u32 HCER0;
	/* 0x25 */ u32 HCER1;
	/* 0x26 */ u32 HCBR0;
	/* 0x27 */ u32 HCBR1;
	/* 0x28 */ u32 rsvd7[8];
	/* 0x30 */ u32 MDAT0;
	/* 0x31 */ u32 MDAT1;
	/* 0x32 */ u32 MDAT2;
	/* 0x33 */ u32 MDAT3;
	/* 0x34 */ u32 MDWE0;
	/* 0x35 */ u32 MDWE1;
	/* 0x36 */ u32 MDWE2;
	/* 0x37 */ u32 MDWE3;
	/* 0x38 */ u32 MCTL;
	/* 0x39 */ u32 MADR;
	/* 0x3A */ u32 rsvd8[0xB6];
	/* 0xF0 */ u32 ACTL;
	/* 0xF1 */ u32 rsvd9[3];
	/* 0xF4 */ u32 ACSR0;
	/* 0xF5 */ u32 ACSR1;
	/* 0xF6 */ u32 ACMR0;
	/* 0xF7 */ u32 ACMR1;
};

#define DIM2_MASK(n)  (~((~(u32)0) << (n)))

enum {
	MLBC0_MLBLK_BIT = 7,

	MLBC0_MLBPEN_BIT = 5,

	MLBC0_MLBCLK_SHIFT = 2,
	MLBC0_MLBCLK_VAL_256FS = 0,
	MLBC0_MLBCLK_VAL_512FS = 1,
	MLBC0_MLBCLK_VAL_1024FS = 2,
	MLBC0_MLBCLK_VAL_2048FS = 3,

	MLBC0_FCNT_SHIFT = 15,
	MLBC0_FCNT_MASK = 7,
	MLBC0_FCNT_VAL_1FPSB = 0,
	MLBC0_FCNT_VAL_2FPSB = 1,
	MLBC0_FCNT_VAL_4FPSB = 2,
	MLBC0_FCNT_VAL_8FPSB = 3,
	MLBC0_FCNT_VAL_16FPSB = 4,
	MLBC0_FCNT_VAL_32FPSB = 5,
	MLBC0_FCNT_VAL_64FPSB = 6,

	MLBC0_MLBEN_BIT = 0,

	MIEN_CTX_BREAK_BIT = 29,
	MIEN_CTX_PE_BIT = 28,
	MIEN_CTX_DONE_BIT = 27,

	MIEN_CRX_BREAK_BIT = 26,
	MIEN_CRX_PE_BIT = 25,
	MIEN_CRX_DONE_BIT = 24,

	MIEN_ATX_BREAK_BIT = 22,
	MIEN_ATX_PE_BIT = 21,
	MIEN_ATX_DONE_BIT = 20,

	MIEN_ARX_BREAK_BIT = 19,
	MIEN_ARX_PE_BIT = 18,
	MIEN_ARX_DONE_BIT = 17,

	MIEN_SYNC_PE_BIT = 16,

	MIEN_ISOC_BUFO_BIT = 1,
	MIEN_ISOC_PE_BIT = 0,

	MLBC1_NDA_SHIFT = 8,
	MLBC1_NDA_MASK = 0xFF,

	MLBC1_CLKMERR_BIT = 7,
	MLBC1_LOCKERR_BIT = 6,

	ACTL_DMA_MODE_BIT = 2,
	ACTL_DMA_MODE_VAL_DMA_MODE_0 = 0,
	ACTL_DMA_MODE_VAL_DMA_MODE_1 = 1,
	ACTL_SCE_BIT = 0,

	HCTL_EN_BIT = 15
};

enum {
	CDT1_BS_ISOC_SHIFT = 0,
	CDT1_BS_ISOC_MASK = DIM2_MASK(9),

	CDT3_BD_SHIFT = 0,
	CDT3_BD_MASK = DIM2_MASK(12),
	CDT3_BD_ISOC_MASK = DIM2_MASK(13),
	CDT3_BA_SHIFT = 16,

	ADT0_CE_BIT = 15,
	ADT0_LE_BIT = 14,
	ADT0_PG_BIT = 13,

	ADT1_RDY_BIT = 15,
	ADT1_DNE_BIT = 14,
	ADT1_ERR_BIT = 13,
	ADT1_PS_BIT = 12,
	ADT1_MEP_BIT = 11,
	ADT1_BD_SHIFT = 0,
	ADT1_CTRL_ASYNC_BD_MASK = DIM2_MASK(11),
	ADT1_ISOC_SYNC_BD_MASK = DIM2_MASK(13),

	CAT_MFE_BIT = 14,

	CAT_MT_BIT = 13,

	CAT_RNW_BIT = 12,

	CAT_CE_BIT = 11,

	CAT_CT_SHIFT = 8,
	CAT_CT_VAL_SYNC = 0,
	CAT_CT_VAL_CONTROL = 1,
	CAT_CT_VAL_ASYNC = 2,
	CAT_CT_VAL_ISOC = 3,

	CAT_CL_SHIFT = 0,
	CAT_CL_MASK = DIM2_MASK(6)
};

#endif	/* DIM2_OS62420_H */
