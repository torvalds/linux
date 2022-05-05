/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

struct bdisp_node {
	/* 0 - General */
	u32 nip;
	u32 cic;
	u32 ins;
	u32 ack;
	/* 1 - Target */
	u32 tba;
	u32 tty;
	u32 txy;
	u32 tsz;
	/* 2 - Color Fill */
	u32 s1cf;
	u32 s2cf;
	/* 3 - Source 1 */
	u32 s1ba;
	u32 s1ty;
	u32 s1xy;
	u32 s1sz_tsz;
	/* 4 - Source 2 */
	u32 s2ba;
	u32 s2ty;
	u32 s2xy;
	u32 s2sz;
	/* 5 - Source 3 */
	u32 s3ba;
	u32 s3ty;
	u32 s3xy;
	u32 s3sz;
	/* 6 - Clipping */
	u32 cwo;
	u32 cws;
	/* 7 - CLUT */
	u32 cco;
	u32 cml;
	/* 8 - Filter & Mask */
	u32 fctl;
	u32 pmk;
	/* 9 - Chroma Filter */
	u32 rsf;
	u32 rzi;
	u32 hfp;
	u32 vfp;
	/* 10 - Luma Filter */
	u32 y_rsf;
	u32 y_rzi;
	u32 y_hfp;
	u32 y_vfp;
	/* 11 - Flicker */
	u32 ff0;
	u32 ff1;
	u32 ff2;
	u32 ff3;
	/* 12 - Color Key */
	u32 key1;
	u32 key2;
	/* 14 - Static Address & User */
	u32 sar;
	u32 usr;
	/* 15 - Input Versatile Matrix */
	u32 ivmx0;
	u32 ivmx1;
	u32 ivmx2;
	u32 ivmx3;
	/* 16 - Output Versatile Matrix */
	u32 ovmx0;
	u32 ovmx1;
	u32 ovmx2;
	u32 ovmx3;
	/* 17 - Pace */
	u32 pace;
	/* 18 - VC1R & DEI */
	u32 vc1r;
	u32 dei;
	/* 19 - Gradient Fill */
	u32 hgf;
	u32 vgf;
};

/* HW registers : static */
#define BLT_CTL                 0x0A00
#define BLT_ITS                 0x0A04
#define BLT_STA1                0x0A08
#define BLT_AQ1_CTL             0x0A60
#define BLT_AQ1_IP              0x0A64
#define BLT_AQ1_LNA             0x0A68
#define BLT_AQ1_STA             0x0A6C
#define BLT_ITM0                0x0AD0
/* HW registers : plugs */
#define BLT_PLUGS1_OP2          0x0B04
#define BLT_PLUGS1_CHZ          0x0B08
#define BLT_PLUGS1_MSZ          0x0B0C
#define BLT_PLUGS1_PGZ          0x0B10
#define BLT_PLUGS2_OP2          0x0B24
#define BLT_PLUGS2_CHZ          0x0B28
#define BLT_PLUGS2_MSZ          0x0B2C
#define BLT_PLUGS2_PGZ          0x0B30
#define BLT_PLUGS3_OP2          0x0B44
#define BLT_PLUGS3_CHZ          0x0B48
#define BLT_PLUGS3_MSZ          0x0B4C
#define BLT_PLUGS3_PGZ          0x0B50
#define BLT_PLUGT_OP2           0x0B84
#define BLT_PLUGT_CHZ           0x0B88
#define BLT_PLUGT_MSZ           0x0B8C
#define BLT_PLUGT_PGZ           0x0B90
/* HW registers : node */
#define BLT_NIP                 0x0C00
#define BLT_CIC                 0x0C04
#define BLT_INS                 0x0C08
#define BLT_ACK                 0x0C0C
#define BLT_TBA                 0x0C10
#define BLT_TTY                 0x0C14
#define BLT_TXY                 0x0C18
#define BLT_TSZ                 0x0C1C
#define BLT_S1BA                0x0C28
#define BLT_S1TY                0x0C2C
#define BLT_S1XY                0x0C30
#define BLT_S2BA                0x0C38
#define BLT_S2TY                0x0C3C
#define BLT_S2XY                0x0C40
#define BLT_S2SZ                0x0C44
#define BLT_S3BA                0x0C48
#define BLT_S3TY                0x0C4C
#define BLT_S3XY                0x0C50
#define BLT_S3SZ                0x0C54
#define BLT_FCTL                0x0C68
#define BLT_RSF                 0x0C70
#define BLT_RZI                 0x0C74
#define BLT_HFP                 0x0C78
#define BLT_VFP                 0x0C7C
#define BLT_Y_RSF               0x0C80
#define BLT_Y_RZI               0x0C84
#define BLT_Y_HFP               0x0C88
#define BLT_Y_VFP               0x0C8C
#define BLT_IVMX0               0x0CC0
#define BLT_IVMX1               0x0CC4
#define BLT_IVMX2               0x0CC8
#define BLT_IVMX3               0x0CCC
#define BLT_OVMX0               0x0CD0
#define BLT_OVMX1               0x0CD4
#define BLT_OVMX2               0x0CD8
#define BLT_OVMX3               0x0CDC
#define BLT_DEI                 0x0CEC
/* HW registers : filters */
#define BLT_HFC_N               0x0D00
#define BLT_VFC_N               0x0D90
#define BLT_Y_HFC_N             0x0E00
#define BLT_Y_VFC_N             0x0E90
#define BLT_NB_H_COEF           16
#define BLT_NB_V_COEF           10

/* Registers values */
#define BLT_CTL_RESET           BIT(31)         /* Global soft reset */

#define BLT_ITS_AQ1_LNA         BIT(12)         /* AQ1 LNA reached */

#define BLT_STA1_IDLE           BIT(0)          /* BDISP idle */

#define BLT_AQ1_CTL_CFG         0x80400003      /* Enable, P3, LNA reached */

#define BLT_INS_S1_MASK         (BIT(0) | BIT(1) | BIT(2))
#define BLT_INS_S1_OFF          0x00000000      /* src1 disabled */
#define BLT_INS_S1_MEM          0x00000001      /* src1 fetched from memory */
#define BLT_INS_S1_CF           0x00000003      /* src1 color fill */
#define BLT_INS_S1_COPY         0x00000004      /* src1 direct copy */
#define BLT_INS_S1_FILL         0x00000007      /* src1 firect fill */
#define BLT_INS_S2_MASK         (BIT(3) | BIT(4))
#define BLT_INS_S2_OFF          0x00000000      /* src2 disabled */
#define BLT_INS_S2_MEM          0x00000008      /* src2 fetched from memory */
#define BLT_INS_S2_CF           0x00000018      /* src2 color fill */
#define BLT_INS_S3_MASK         BIT(5)
#define BLT_INS_S3_OFF          0x00000000      /* src3 disabled */
#define BLT_INS_S3_MEM          0x00000020      /* src3 fetched from memory */
#define BLT_INS_IVMX            BIT(6)          /* Input versatile matrix */
#define BLT_INS_CLUT            BIT(7)          /* Color Look Up Table */
#define BLT_INS_SCALE           BIT(8)          /* Scaling */
#define BLT_INS_FLICK           BIT(9)          /* Flicker filter */
#define BLT_INS_CLIP            BIT(10)         /* Clipping */
#define BLT_INS_CKEY            BIT(11)         /* Color key */
#define BLT_INS_OVMX            BIT(12)         /* Output versatile matrix */
#define BLT_INS_DEI             BIT(13)         /* Deinterlace */
#define BLT_INS_PMASK           BIT(14)         /* Plane mask */
#define BLT_INS_VC1R            BIT(17)         /* VC1 Range mapping */
#define BLT_INS_ROTATE          BIT(18)         /* Rotation */
#define BLT_INS_GRAD            BIT(19)         /* Gradient fill */
#define BLT_INS_AQLOCK          BIT(29)         /* AQ lock */
#define BLT_INS_PACE            BIT(30)         /* Pace down */
#define BLT_INS_IRQ             BIT(31)         /* Raise IRQ when node done */
#define BLT_CIC_ALL_GRP         0x000FDFFC      /* all valid groups present */
#define BLT_ACK_BYPASS_S2S3     0x00000007      /* Bypass src2 and src3 */

#define BLT_TTY_COL_SHIFT       16              /* Color format */
#define BLT_TTY_COL_MASK        0x001F0000      /* Color format mask */
#define BLT_TTY_ALPHA_R         BIT(21)         /* Alpha range */
#define BLT_TTY_CR_NOT_CB       BIT(22)         /* CR not Cb */
#define BLT_TTY_MB              BIT(23)         /* MB frame / field*/
#define BLT_TTY_HSO             BIT(24)         /* H scan order */
#define BLT_TTY_VSO             BIT(25)         /* V scan order */
#define BLT_TTY_DITHER          BIT(26)         /* Dithering */
#define BLT_TTY_CHROMA          BIT(27)         /* Write chroma / luma */
#define BLT_TTY_BIG_END         BIT(30)         /* Big endianness */

#define BLT_S1TY_A1_SUBSET      BIT(22)         /* A1 subset */
#define BLT_S1TY_CHROMA_EXT     BIT(26)         /* Chroma Extended */
#define BTL_S1TY_SUBBYTE        BIT(28)         /* Sub-byte fmt, pixel order */
#define BLT_S1TY_RGB_EXP        BIT(29)         /* RGB expansion mode */

#define BLT_S2TY_A1_SUBSET      BIT(22)         /* A1 subset */
#define BLT_S2TY_CHROMA_EXT     BIT(26)         /* Chroma Extended */
#define BTL_S2TY_SUBBYTE        BIT(28)         /* Sub-byte fmt, pixel order */
#define BLT_S2TY_RGB_EXP        BIT(29)         /* RGB expansion mode */

#define BLT_S3TY_BLANK_ACC      BIT(26)         /* Blank access */

#define BLT_FCTL_HV_SCALE       0x00000055      /* H/V resize + color filter */
#define BLT_FCTL_Y_HV_SCALE     0x33000000      /* Luma version */

#define BLT_FCTL_HV_SAMPLE      0x00000044      /* H/V resize */
#define BLT_FCTL_Y_HV_SAMPLE    0x22000000      /* Luma version */

#define BLT_RZI_DEFAULT         0x20003000      /* H/VNB_repeat = 3/2 */

/* Color format */
#define BDISP_RGB565            0x00            /* RGB565 */
#define BDISP_RGB888            0x01            /* RGB888 */
#define BDISP_XRGB8888          0x02            /* RGB888_32 */
#define BDISP_ARGB8888          0x05            /* ARGB888 */
#define BDISP_NV12              0x16            /* YCbCr42x R2B */
#define BDISP_YUV_3B            0x1E            /* YUV (3 buffer) */
