/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ATMEL_ISC_REGS_H
#define __ATMEL_ISC_REGS_H

#include <linux/bitops.h>

/* ISC Control Enable Register 0 */
#define ISC_CTRLEN      0x00000000

/* ISC Control Disable Register 0 */
#define ISC_CTRLDIS     0x00000004

/* ISC Control Status Register 0 */
#define ISC_CTRLSR      0x00000008

#define ISC_CTRL_CAPTURE	BIT(0)
#define ISC_CTRL_UPPRO		BIT(1)
#define ISC_CTRL_HISREQ		BIT(2)
#define ISC_CTRL_HISCLR		BIT(3)

/* ISC Parallel Front End Configuration 0 Register */
#define ISC_PFE_CFG0    0x0000000c

#define ISC_PFE_CFG0_HPOL_LOW   BIT(0)
#define ISC_PFE_CFG0_VPOL_LOW   BIT(1)
#define ISC_PFE_CFG0_PPOL_LOW   BIT(2)

#define ISC_PFE_CFG0_MODE_PROGRESSIVE   (0x0 << 4)
#define ISC_PFE_CFG0_MODE_MASK          GENMASK(6, 4)

#define ISC_PFE_CFG0_BPS_EIGHT  (0x4 << 28)
#define ISC_PFG_CFG0_BPS_NINE   (0x3 << 28)
#define ISC_PFG_CFG0_BPS_TEN    (0x2 << 28)
#define ISC_PFG_CFG0_BPS_ELEVEN (0x1 << 28)
#define ISC_PFG_CFG0_BPS_TWELVE (0x0 << 28)
#define ISC_PFE_CFG0_BPS_MASK   GENMASK(30, 28)

/* ISC Clock Enable Register */
#define ISC_CLKEN               0x00000018

/* ISC Clock Disable Register */
#define ISC_CLKDIS              0x0000001c

/* ISC Clock Status Register */
#define ISC_CLKSR               0x00000020

#define ISC_CLK(n)		BIT(n)

/* ISC Clock Configuration Register */
#define ISC_CLKCFG              0x00000024
#define ISC_CLKCFG_DIV_SHIFT(n) ((n)*16)
#define ISC_CLKCFG_DIV_MASK(n)  GENMASK(((n)*16 + 7), (n)*16)
#define ISC_CLKCFG_SEL_SHIFT(n) ((n)*16 + 8)
#define ISC_CLKCFG_SEL_MASK(n)  GENMASK(((n)*17 + 8), ((n)*16 + 8))

/* ISC Interrupt Enable Register */
#define ISC_INTEN       0x00000028

/* ISC Interrupt Disable Register */
#define ISC_INTDIS      0x0000002c

/* ISC Interrupt Mask Register */
#define ISC_INTMASK     0x00000030

/* ISC Interrupt Status Register */
#define ISC_INTSR       0x00000034

#define ISC_INT_DDONE		BIT(8)
#define ISC_INT_HISDONE		BIT(12)

/* ISC White Balance Control Register */
#define ISC_WB_CTRL     0x00000058

/* ISC White Balance Configuration Register */
#define ISC_WB_CFG      0x0000005c

/* ISC White Balance Offset for R, GR Register */
#define ISC_WB_O_RGR	0x00000060

/* ISC White Balance Offset for B, GB Register */
#define ISC_WB_O_BGR	0x00000064

/* ISC White Balance Gain for R, GR Register */
#define ISC_WB_G_RGR	0x00000068

/* ISC White Balance Gain for B, GB Register */
#define ISC_WB_G_BGR	0x0000006c

/* ISC Color Filter Array Control Register */
#define ISC_CFA_CTRL    0x00000070

/* ISC Color Filter Array Configuration Register */
#define ISC_CFA_CFG     0x00000074
#define ISC_CFA_CFG_EITPOL	BIT(4)

#define ISC_BAY_CFG_GRGR	0x0
#define ISC_BAY_CFG_RGRG	0x1
#define ISC_BAY_CFG_GBGB	0x2
#define ISC_BAY_CFG_BGBG	0x3

/* ISC Color Correction Control Register */
#define ISC_CC_CTRL     0x00000078

/* ISC Color Correction RR RG Register */
#define ISC_CC_RR_RG	0x0000007c

/* ISC Color Correction RB OR Register */
#define ISC_CC_RB_OR	0x00000080

/* ISC Color Correction GR GG Register */
#define ISC_CC_GR_GG	0x00000084

/* ISC Color Correction GB OG Register */
#define ISC_CC_GB_OG	0x00000088

/* ISC Color Correction BR BG Register */
#define ISC_CC_BR_BG	0x0000008c

/* ISC Color Correction BB OB Register */
#define ISC_CC_BB_OB	0x00000090

/* ISC Gamma Correction Control Register */
#define ISC_GAM_CTRL    0x00000094

/* ISC_Gamma Correction Blue Entry Register */
#define ISC_GAM_BENTRY	0x00000098

/* ISC_Gamma Correction Green Entry Register */
#define ISC_GAM_GENTRY	0x00000198

/* ISC_Gamma Correction Green Entry Register */
#define ISC_GAM_RENTRY	0x00000298

/* Color Space Conversion Control Register */
#define ISC_CSC_CTRL    0x00000398

/* Color Space Conversion YR YG Register */
#define ISC_CSC_YR_YG	0x0000039c

/* Color Space Conversion YB OY Register */
#define ISC_CSC_YB_OY	0x000003a0

/* Color Space Conversion CBR CBG Register */
#define ISC_CSC_CBR_CBG	0x000003a4

/* Color Space Conversion CBB OCB Register */
#define ISC_CSC_CBB_OCB	0x000003a8

/* Color Space Conversion CRR CRG Register */
#define ISC_CSC_CRR_CRG	0x000003ac

/* Color Space Conversion CRB OCR Register */
#define ISC_CSC_CRB_OCR	0x000003b0

/* Contrast And Brightness Control Register */
#define ISC_CBC_CTRL    0x000003b4

/* Contrast And Brightness Configuration Register */
#define ISC_CBC_CFG	0x000003b8

/* Brightness Register */
#define ISC_CBC_BRIGHT	0x000003bc
#define ISC_CBC_BRIGHT_MASK	GENMASK(10, 0)

/* Contrast Register */
#define ISC_CBC_CONTRAST	0x000003c0
#define ISC_CBC_CONTRAST_MASK	GENMASK(11, 0)

/* Subsampling 4:4:4 to 4:2:2 Control Register */
#define ISC_SUB422_CTRL 0x000003c4

/* Subsampling 4:2:2 to 4:2:0 Control Register */
#define ISC_SUB420_CTRL 0x000003cc

/* Rounding, Limiting and Packing Configuration Register */
#define ISC_RLP_CFG     0x000003d0

#define ISC_RLP_CFG_MODE_DAT8           0x0
#define ISC_RLP_CFG_MODE_DAT9           0x1
#define ISC_RLP_CFG_MODE_DAT10          0x2
#define ISC_RLP_CFG_MODE_DAT11          0x3
#define ISC_RLP_CFG_MODE_DAT12          0x4
#define ISC_RLP_CFG_MODE_DATY8          0x5
#define ISC_RLP_CFG_MODE_DATY10         0x6
#define ISC_RLP_CFG_MODE_ARGB444        0x7
#define ISC_RLP_CFG_MODE_ARGB555        0x8
#define ISC_RLP_CFG_MODE_RGB565         0x9
#define ISC_RLP_CFG_MODE_ARGB32         0xa
#define ISC_RLP_CFG_MODE_YYCC           0xb
#define ISC_RLP_CFG_MODE_YYCC_LIMITED   0xc
#define ISC_RLP_CFG_MODE_MASK           GENMASK(3, 0)

/* Histogram Control Register */
#define ISC_HIS_CTRL	0x000003d4

#define ISC_HIS_CTRL_EN			BIT(0)
#define ISC_HIS_CTRL_DIS		0x0

/* Histogram Configuration Register */
#define ISC_HIS_CFG	0x000003d8

#define ISC_HIS_CFG_MODE_GR		0x0
#define ISC_HIS_CFG_MODE_R		0x1
#define ISC_HIS_CFG_MODE_GB		0x2
#define ISC_HIS_CFG_MODE_B		0x3
#define ISC_HIS_CFG_MODE_Y		0x4
#define ISC_HIS_CFG_MODE_RAW		0x5
#define ISC_HIS_CFG_MODE_YCCIR656	0x6

#define ISC_HIS_CFG_BAYSEL_SHIFT	4

#define ISC_HIS_CFG_RAR			BIT(8)

/* DMA Configuration Register */
#define ISC_DCFG        0x000003e0
#define ISC_DCFG_IMODE_PACKED8          0x0
#define ISC_DCFG_IMODE_PACKED16         0x1
#define ISC_DCFG_IMODE_PACKED32         0x2
#define ISC_DCFG_IMODE_YC422SP          0x3
#define ISC_DCFG_IMODE_YC422P           0x4
#define ISC_DCFG_IMODE_YC420SP          0x5
#define ISC_DCFG_IMODE_YC420P           0x6
#define ISC_DCFG_IMODE_MASK             GENMASK(2, 0)

#define ISC_DCFG_YMBSIZE_SINGLE         (0x0 << 4)
#define ISC_DCFG_YMBSIZE_BEATS4         (0x1 << 4)
#define ISC_DCFG_YMBSIZE_BEATS8         (0x2 << 4)
#define ISC_DCFG_YMBSIZE_BEATS16        (0x3 << 4)
#define ISC_DCFG_YMBSIZE_MASK           GENMASK(5, 4)

#define ISC_DCFG_CMBSIZE_SINGLE         (0x0 << 8)
#define ISC_DCFG_CMBSIZE_BEATS4         (0x1 << 8)
#define ISC_DCFG_CMBSIZE_BEATS8         (0x2 << 8)
#define ISC_DCFG_CMBSIZE_BEATS16        (0x3 << 8)
#define ISC_DCFG_CMBSIZE_MASK           GENMASK(9, 8)

/* DMA Control Register */
#define ISC_DCTRL       0x000003e4

#define ISC_DCTRL_DVIEW_PACKED          (0x0 << 1)
#define ISC_DCTRL_DVIEW_SEMIPLANAR      (0x1 << 1)
#define ISC_DCTRL_DVIEW_PLANAR          (0x2 << 1)
#define ISC_DCTRL_DVIEW_MASK            GENMASK(2, 1)

#define ISC_DCTRL_IE_IS			(0x0 << 4)

/* DMA Descriptor Address Register */
#define ISC_DNDA        0x000003e8

/* DMA Address 0 Register */
#define ISC_DAD0        0x000003ec

/* DMA Address 1 Register */
#define ISC_DAD1        0x000003f4

/* DMA Address 2 Register */
#define ISC_DAD2        0x000003fc

/* Histogram Entry */
#define ISC_HIS_ENTRY	0x00000410

#endif
