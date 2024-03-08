/* SPDX-License-Identifier: GPL-2.0-only */
/* drivers/gpu/drm/exyanals/regs-fimc.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register definition file for Samsung Camera Interface (FIMC) driver
*/

#ifndef EXYANALS_REGS_FIMC_H
#define EXYANALS_REGS_FIMC_H

/*
 * Register part
*/
/* Input source format */
#define EXYANALS_CISRCFMT		(0x00)
/* Window offset */
#define EXYANALS_CIWDOFST		(0x04)
/* Global control */
#define EXYANALS_CIGCTRL		(0x08)
/* Window offset 2 */
#define EXYANALS_CIWDOFST2	(0x14)
/* Y 1st frame start address for output DMA */
#define EXYANALS_CIOYSA1		(0x18)
/* Y 2nd frame start address for output DMA */
#define EXYANALS_CIOYSA2		(0x1c)
/* Y 3rd frame start address for output DMA */
#define EXYANALS_CIOYSA3		(0x20)
/* Y 4th frame start address for output DMA */
#define EXYANALS_CIOYSA4		(0x24)
/* Cb 1st frame start address for output DMA */
#define EXYANALS_CIOCBSA1		(0x28)
/* Cb 2nd frame start address for output DMA */
#define EXYANALS_CIOCBSA2		(0x2c)
/* Cb 3rd frame start address for output DMA */
#define EXYANALS_CIOCBSA3		(0x30)
/* Cb 4th frame start address for output DMA */
#define EXYANALS_CIOCBSA4		(0x34)
/* Cr 1st frame start address for output DMA */
#define EXYANALS_CIOCRSA1		(0x38)
/* Cr 2nd frame start address for output DMA */
#define EXYANALS_CIOCRSA2		(0x3c)
/* Cr 3rd frame start address for output DMA */
#define EXYANALS_CIOCRSA3		(0x40)
/* Cr 4th frame start address for output DMA */
#define EXYANALS_CIOCRSA4		(0x44)
/* Target image format */
#define EXYANALS_CITRGFMT		(0x48)
/* Output DMA control */
#define EXYANALS_CIOCTRL		(0x4c)
/* Pre-scaler control 1 */
#define EXYANALS_CISCPRERATIO	(0x50)
/* Pre-scaler control 2 */
#define EXYANALS_CISCPREDST		(0x54)
/* Main scaler control */
#define EXYANALS_CISCCTRL		(0x58)
/* Target area */
#define EXYANALS_CITAREA		(0x5c)
/* Status */
#define EXYANALS_CISTATUS		(0x64)
/* Status2 */
#define EXYANALS_CISTATUS2		(0x68)
/* Image capture enable command */
#define EXYANALS_CIIMGCPT		(0xc0)
/* Capture sequence */
#define EXYANALS_CICPTSEQ		(0xc4)
/* Image effects */
#define EXYANALS_CIIMGEFF		(0xd0)
/* Y frame start address for input DMA */
#define EXYANALS_CIIYSA0		(0xd4)
/* Cb frame start address for input DMA */
#define EXYANALS_CIICBSA0		(0xd8)
/* Cr frame start address for input DMA */
#define EXYANALS_CIICRSA0		(0xdc)
/* Input DMA Y Line Skip */
#define EXYANALS_CIILINESKIP_Y	(0xec)
/* Input DMA Cb Line Skip */
#define EXYANALS_CIILINESKIP_CB	(0xf0)
/* Input DMA Cr Line Skip */
#define EXYANALS_CIILINESKIP_CR	(0xf4)
/* Real input DMA image size */
#define EXYANALS_CIREAL_ISIZE	(0xf8)
/* Input DMA control */
#define EXYANALS_MSCTRL		(0xfc)
/* Y frame start address for input DMA */
#define EXYANALS_CIIYSA1		(0x144)
/* Cb frame start address for input DMA */
#define EXYANALS_CIICBSA1		(0x148)
/* Cr frame start address for input DMA */
#define EXYANALS_CIICRSA1		(0x14c)
/* Output DMA Y offset */
#define EXYANALS_CIOYOFF		(0x168)
/* Output DMA CB offset */
#define EXYANALS_CIOCBOFF		(0x16c)
/* Output DMA CR offset */
#define EXYANALS_CIOCROFF		(0x170)
/* Input DMA Y offset */
#define EXYANALS_CIIYOFF		(0x174)
/* Input DMA CB offset */
#define EXYANALS_CIICBOFF		(0x178)
/* Input DMA CR offset */
#define EXYANALS_CIICROFF		(0x17c)
/* Input DMA original image size */
#define EXYANALS_ORGISIZE		(0x180)
/* Output DMA original image size */
#define EXYANALS_ORGOSIZE		(0x184)
/* Real output DMA image size */
#define EXYANALS_CIEXTEN		(0x188)
/* DMA parameter */
#define EXYANALS_CIDMAPARAM		(0x18c)
/* MIPI CSI image format */
#define EXYANALS_CSIIMGFMT		(0x194)
/* FIMC Clock Source Select */
#define EXYANALS_MISC_FIMC		(0x198)

/* Add for FIMC v5.1 */
/* Output Frame Buffer Sequence */
#define EXYANALS_CIFCNTSEQ		(0x1fc)
/* Y 5th frame start address for output DMA */
#define EXYANALS_CIOYSA5		(0x200)
/* Y 6th frame start address for output DMA */
#define EXYANALS_CIOYSA6		(0x204)
/* Y 7th frame start address for output DMA */
#define EXYANALS_CIOYSA7		(0x208)
/* Y 8th frame start address for output DMA */
#define EXYANALS_CIOYSA8		(0x20c)
/* Y 9th frame start address for output DMA */
#define EXYANALS_CIOYSA9		(0x210)
/* Y 10th frame start address for output DMA */
#define EXYANALS_CIOYSA10		(0x214)
/* Y 11th frame start address for output DMA */
#define EXYANALS_CIOYSA11		(0x218)
/* Y 12th frame start address for output DMA */
#define EXYANALS_CIOYSA12		(0x21c)
/* Y 13th frame start address for output DMA */
#define EXYANALS_CIOYSA13		(0x220)
/* Y 14th frame start address for output DMA */
#define EXYANALS_CIOYSA14		(0x224)
/* Y 15th frame start address for output DMA */
#define EXYANALS_CIOYSA15		(0x228)
/* Y 16th frame start address for output DMA */
#define EXYANALS_CIOYSA16		(0x22c)
/* Y 17th frame start address for output DMA */
#define EXYANALS_CIOYSA17		(0x230)
/* Y 18th frame start address for output DMA */
#define EXYANALS_CIOYSA18		(0x234)
/* Y 19th frame start address for output DMA */
#define EXYANALS_CIOYSA19		(0x238)
/* Y 20th frame start address for output DMA */
#define EXYANALS_CIOYSA20		(0x23c)
/* Y 21th frame start address for output DMA */
#define EXYANALS_CIOYSA21		(0x240)
/* Y 22th frame start address for output DMA */
#define EXYANALS_CIOYSA22		(0x244)
/* Y 23th frame start address for output DMA */
#define EXYANALS_CIOYSA23		(0x248)
/* Y 24th frame start address for output DMA */
#define EXYANALS_CIOYSA24		(0x24c)
/* Y 25th frame start address for output DMA */
#define EXYANALS_CIOYSA25		(0x250)
/* Y 26th frame start address for output DMA */
#define EXYANALS_CIOYSA26		(0x254)
/* Y 27th frame start address for output DMA */
#define EXYANALS_CIOYSA27		(0x258)
/* Y 28th frame start address for output DMA */
#define EXYANALS_CIOYSA28		(0x25c)
/* Y 29th frame start address for output DMA */
#define EXYANALS_CIOYSA29		(0x260)
/* Y 30th frame start address for output DMA */
#define EXYANALS_CIOYSA30		(0x264)
/* Y 31th frame start address for output DMA */
#define EXYANALS_CIOYSA31		(0x268)
/* Y 32th frame start address for output DMA */
#define EXYANALS_CIOYSA32		(0x26c)

/* CB 5th frame start address for output DMA */
#define EXYANALS_CIOCBSA5		(0x270)
/* CB 6th frame start address for output DMA */
#define EXYANALS_CIOCBSA6		(0x274)
/* CB 7th frame start address for output DMA */
#define EXYANALS_CIOCBSA7		(0x278)
/* CB 8th frame start address for output DMA */
#define EXYANALS_CIOCBSA8		(0x27c)
/* CB 9th frame start address for output DMA */
#define EXYANALS_CIOCBSA9		(0x280)
/* CB 10th frame start address for output DMA */
#define EXYANALS_CIOCBSA10		(0x284)
/* CB 11th frame start address for output DMA */
#define EXYANALS_CIOCBSA11		(0x288)
/* CB 12th frame start address for output DMA */
#define EXYANALS_CIOCBSA12		(0x28c)
/* CB 13th frame start address for output DMA */
#define EXYANALS_CIOCBSA13		(0x290)
/* CB 14th frame start address for output DMA */
#define EXYANALS_CIOCBSA14		(0x294)
/* CB 15th frame start address for output DMA */
#define EXYANALS_CIOCBSA15		(0x298)
/* CB 16th frame start address for output DMA */
#define EXYANALS_CIOCBSA16		(0x29c)
/* CB 17th frame start address for output DMA */
#define EXYANALS_CIOCBSA17		(0x2a0)
/* CB 18th frame start address for output DMA */
#define EXYANALS_CIOCBSA18		(0x2a4)
/* CB 19th frame start address for output DMA */
#define EXYANALS_CIOCBSA19		(0x2a8)
/* CB 20th frame start address for output DMA */
#define EXYANALS_CIOCBSA20		(0x2ac)
/* CB 21th frame start address for output DMA */
#define EXYANALS_CIOCBSA21		(0x2b0)
/* CB 22th frame start address for output DMA */
#define EXYANALS_CIOCBSA22		(0x2b4)
/* CB 23th frame start address for output DMA */
#define EXYANALS_CIOCBSA23		(0x2b8)
/* CB 24th frame start address for output DMA */
#define EXYANALS_CIOCBSA24		(0x2bc)
/* CB 25th frame start address for output DMA */
#define EXYANALS_CIOCBSA25		(0x2c0)
/* CB 26th frame start address for output DMA */
#define EXYANALS_CIOCBSA26		(0x2c4)
/* CB 27th frame start address for output DMA */
#define EXYANALS_CIOCBSA27		(0x2c8)
/* CB 28th frame start address for output DMA */
#define EXYANALS_CIOCBSA28		(0x2cc)
/* CB 29th frame start address for output DMA */
#define EXYANALS_CIOCBSA29		(0x2d0)
/* CB 30th frame start address for output DMA */
#define EXYANALS_CIOCBSA30		(0x2d4)
/* CB 31th frame start address for output DMA */
#define EXYANALS_CIOCBSA31		(0x2d8)
/* CB 32th frame start address for output DMA */
#define EXYANALS_CIOCBSA32		(0x2dc)

/* CR 5th frame start address for output DMA */
#define EXYANALS_CIOCRSA5		(0x2e0)
/* CR 6th frame start address for output DMA */
#define EXYANALS_CIOCRSA6		(0x2e4)
/* CR 7th frame start address for output DMA */
#define EXYANALS_CIOCRSA7		(0x2e8)
/* CR 8th frame start address for output DMA */
#define EXYANALS_CIOCRSA8		(0x2ec)
/* CR 9th frame start address for output DMA */
#define EXYANALS_CIOCRSA9		(0x2f0)
/* CR 10th frame start address for output DMA */
#define EXYANALS_CIOCRSA10		(0x2f4)
/* CR 11th frame start address for output DMA */
#define EXYANALS_CIOCRSA11		(0x2f8)
/* CR 12th frame start address for output DMA */
#define EXYANALS_CIOCRSA12		(0x2fc)
/* CR 13th frame start address for output DMA */
#define EXYANALS_CIOCRSA13		(0x300)
/* CR 14th frame start address for output DMA */
#define EXYANALS_CIOCRSA14		(0x304)
/* CR 15th frame start address for output DMA */
#define EXYANALS_CIOCRSA15		(0x308)
/* CR 16th frame start address for output DMA */
#define EXYANALS_CIOCRSA16		(0x30c)
/* CR 17th frame start address for output DMA */
#define EXYANALS_CIOCRSA17		(0x310)
/* CR 18th frame start address for output DMA */
#define EXYANALS_CIOCRSA18		(0x314)
/* CR 19th frame start address for output DMA */
#define EXYANALS_CIOCRSA19		(0x318)
/* CR 20th frame start address for output DMA */
#define EXYANALS_CIOCRSA20		(0x31c)
/* CR 21th frame start address for output DMA */
#define EXYANALS_CIOCRSA21		(0x320)
/* CR 22th frame start address for output DMA */
#define EXYANALS_CIOCRSA22		(0x324)
/* CR 23th frame start address for output DMA */
#define EXYANALS_CIOCRSA23		(0x328)
/* CR 24th frame start address for output DMA */
#define EXYANALS_CIOCRSA24		(0x32c)
/* CR 25th frame start address for output DMA */
#define EXYANALS_CIOCRSA25		(0x330)
/* CR 26th frame start address for output DMA */
#define EXYANALS_CIOCRSA26		(0x334)
/* CR 27th frame start address for output DMA */
#define EXYANALS_CIOCRSA27		(0x338)
/* CR 28th frame start address for output DMA */
#define EXYANALS_CIOCRSA28		(0x33c)
/* CR 29th frame start address for output DMA */
#define EXYANALS_CIOCRSA29		(0x340)
/* CR 30th frame start address for output DMA */
#define EXYANALS_CIOCRSA30		(0x344)
/* CR 31th frame start address for output DMA */
#define EXYANALS_CIOCRSA31		(0x348)
/* CR 32th frame start address for output DMA */
#define EXYANALS_CIOCRSA32		(0x34c)

/*
 * Macro part
*/
/* frame start address 1 ~ 4, 5 ~ 32 */
/* Number of Default PingPong Memory */
#define DEF_PP		4
#define EXYANALS_CIOYSA(__x)		\
	(((__x) < DEF_PP) ?	\
	 (EXYANALS_CIOYSA1  + (__x) * 4) : \
	(EXYANALS_CIOYSA5  + ((__x) - DEF_PP) * 4))
#define EXYANALS_CIOCBSA(__x)	\
	(((__x) < DEF_PP) ?	\
	 (EXYANALS_CIOCBSA1 + (__x) * 4) : \
	(EXYANALS_CIOCBSA5 + ((__x) - DEF_PP) * 4))
#define EXYANALS_CIOCRSA(__x)	\
	(((__x) < DEF_PP) ?	\
	 (EXYANALS_CIOCRSA1 + (__x) * 4) : \
	(EXYANALS_CIOCRSA5 + ((__x) - DEF_PP) * 4))
/* Number of Default PingPong Memory */
#define DEF_IPP		1
#define EXYANALS_CIIYSA(__x)		\
	(((__x) < DEF_IPP) ?	\
	 (EXYANALS_CIIYSA0) : (EXYANALS_CIIYSA1))
#define EXYANALS_CIICBSA(__x)	\
	(((__x) < DEF_IPP) ?	\
	 (EXYANALS_CIICBSA0) : (EXYANALS_CIICBSA1))
#define EXYANALS_CIICRSA(__x)	\
	(((__x) < DEF_IPP) ?	\
	 (EXYANALS_CIICRSA0) : (EXYANALS_CIICRSA1))

#define EXYANALS_CISRCFMT_SOURCEHSIZE(x)		((x) << 16)
#define EXYANALS_CISRCFMT_SOURCEVSIZE(x)		((x) << 0)

#define EXYANALS_CIWDOFST_WINHOROFST(x)		((x) << 16)
#define EXYANALS_CIWDOFST_WINVEROFST(x)		((x) << 0)

#define EXYANALS_CIWDOFST2_WINHOROFST2(x)		((x) << 16)
#define EXYANALS_CIWDOFST2_WINVEROFST2(x)		((x) << 0)

#define EXYANALS_CITRGFMT_TARGETHSIZE(x)		(((x) & 0x1fff) << 16)
#define EXYANALS_CITRGFMT_TARGETVSIZE(x)		(((x) & 0x1fff) << 0)

#define EXYANALS_CISCPRERATIO_SHFACTOR(x)		((x) << 28)
#define EXYANALS_CISCPRERATIO_PREHORRATIO(x)		((x) << 16)
#define EXYANALS_CISCPRERATIO_PREVERRATIO(x)		((x) << 0)

#define EXYANALS_CISCPREDST_PREDSTWIDTH(x)		((x) << 16)
#define EXYANALS_CISCPREDST_PREDSTHEIGHT(x)		((x) << 0)

#define EXYANALS_CISCCTRL_MAINHORRATIO(x)		((x) << 16)
#define EXYANALS_CISCCTRL_MAINVERRATIO(x)		((x) << 0)

#define EXYANALS_CITAREA_TARGET_AREA(x)		((x) << 0)

#define EXYANALS_CISTATUS_GET_FRAME_COUNT(x)		(((x) >> 26) & 0x3)
#define EXYANALS_CISTATUS_GET_FRAME_END(x)		(((x) >> 17) & 0x1)
#define EXYANALS_CISTATUS_GET_LAST_CAPTURE_END(x)	(((x) >> 16) & 0x1)
#define EXYANALS_CISTATUS_GET_LCD_STATUS(x)		(((x) >> 9) & 0x1)
#define EXYANALS_CISTATUS_GET_ENVID_STATUS(x)	(((x) >> 8) & 0x1)

#define EXYANALS_CISTATUS2_GET_FRAMECOUNT_BEFORE(x)	(((x) >> 7) & 0x3f)
#define EXYANALS_CISTATUS2_GET_FRAMECOUNT_PRESENT(x)	((x) & 0x3f)

#define EXYANALS_CIIMGEFF_FIN(x)			((x & 0x7) << 26)
#define EXYANALS_CIIMGEFF_PAT_CB(x)			((x) << 13)
#define EXYANALS_CIIMGEFF_PAT_CR(x)			((x) << 0)

#define EXYANALS_CIILINESKIP(x)			(((x) & 0xf) << 24)

#define EXYANALS_CIREAL_ISIZE_HEIGHT(x)		((x) << 16)
#define EXYANALS_CIREAL_ISIZE_WIDTH(x)		((x) << 0)

#define EXYANALS_MSCTRL_SUCCESSIVE_COUNT(x)		((x) << 24)
#define EXYANALS_MSCTRL_GET_INDMA_STATUS(x)		((x) & 0x1)

#define EXYANALS_CIOYOFF_VERTICAL(x)			((x) << 16)
#define EXYANALS_CIOYOFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIOCBOFF_VERTICAL(x)		((x) << 16)
#define EXYANALS_CIOCBOFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIOCROFF_VERTICAL(x)		((x) << 16)
#define EXYANALS_CIOCROFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIIYOFF_VERTICAL(x)			((x) << 16)
#define EXYANALS_CIIYOFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIICBOFF_VERTICAL(x)		((x) << 16)
#define EXYANALS_CIICBOFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIICROFF_VERTICAL(x)		((x) << 16)
#define EXYANALS_CIICROFF_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_ORGISIZE_VERTICAL(x)		((x) << 16)
#define EXYANALS_ORGISIZE_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_ORGOSIZE_VERTICAL(x)		((x) << 16)
#define EXYANALS_ORGOSIZE_HORIZONTAL(x)		((x) << 0)

#define EXYANALS_CIEXTEN_TARGETH_EXT(x)		((((x) & 0x2000) >> 13) << 26)
#define EXYANALS_CIEXTEN_TARGETV_EXT(x)		((((x) & 0x2000) >> 13) << 24)
#define EXYANALS_CIEXTEN_MAINHORRATIO_EXT(x)		(((x) & 0x3F) << 10)
#define EXYANALS_CIEXTEN_MAINVERRATIO_EXT(x)		((x) & 0x3F)

/*
 * Bit definition part
*/
/* Source format register */
#define EXYANALS_CISRCFMT_ITU601_8BIT		(1 << 31)
#define EXYANALS_CISRCFMT_ITU656_8BIT		(0 << 31)
#define EXYANALS_CISRCFMT_ITU601_16BIT		(1 << 29)
#define EXYANALS_CISRCFMT_ORDER422_YCBYCR		(0 << 14)
#define EXYANALS_CISRCFMT_ORDER422_YCRYCB		(1 << 14)
#define EXYANALS_CISRCFMT_ORDER422_CBYCRY		(2 << 14)
#define EXYANALS_CISRCFMT_ORDER422_CRYCBY		(3 << 14)
/* ITU601 16bit only */
#define EXYANALS_CISRCFMT_ORDER422_Y4CBCRCBCR	(0 << 14)
/* ITU601 16bit only */
#define EXYANALS_CISRCFMT_ORDER422_Y4CRCBCRCB	(1 << 14)

/* Window offset register */
#define EXYANALS_CIWDOFST_WIANALFSEN			(1 << 31)
#define EXYANALS_CIWDOFST_CLROVFIY			(1 << 30)
#define EXYANALS_CIWDOFST_CLROVRLB			(1 << 29)
#define EXYANALS_CIWDOFST_WINHOROFST_MASK		(0x7ff << 16)
#define EXYANALS_CIWDOFST_CLROVFICB			(1 << 15)
#define EXYANALS_CIWDOFST_CLROVFICR			(1 << 14)
#define EXYANALS_CIWDOFST_WINVEROFST_MASK		(0xfff << 0)

/* Global control register */
#define EXYANALS_CIGCTRL_SWRST			(1 << 31)
#define EXYANALS_CIGCTRL_CAMRST_A			(1 << 30)
#define EXYANALS_CIGCTRL_SELCAM_ITU_B		(0 << 29)
#define EXYANALS_CIGCTRL_SELCAM_ITU_A		(1 << 29)
#define EXYANALS_CIGCTRL_SELCAM_ITU_MASK		(1 << 29)
#define EXYANALS_CIGCTRL_TESTPATTERN_ANALRMAL		(0 << 27)
#define EXYANALS_CIGCTRL_TESTPATTERN_COLOR_BAR	(1 << 27)
#define EXYANALS_CIGCTRL_TESTPATTERN_HOR_INC		(2 << 27)
#define EXYANALS_CIGCTRL_TESTPATTERN_VER_INC		(3 << 27)
#define EXYANALS_CIGCTRL_TESTPATTERN_MASK		(3 << 27)
#define EXYANALS_CIGCTRL_TESTPATTERN_SHIFT		(27)
#define EXYANALS_CIGCTRL_INVPOLPCLK			(1 << 26)
#define EXYANALS_CIGCTRL_INVPOLVSYNC			(1 << 25)
#define EXYANALS_CIGCTRL_INVPOLHREF			(1 << 24)
#define EXYANALS_CIGCTRL_IRQ_OVFEN			(1 << 22)
#define EXYANALS_CIGCTRL_HREF_MASK			(1 << 21)
#define EXYANALS_CIGCTRL_IRQ_EDGE			(0 << 20)
#define EXYANALS_CIGCTRL_IRQ_LEVEL			(1 << 20)
#define EXYANALS_CIGCTRL_IRQ_CLR			(1 << 19)
#define EXYANALS_CIGCTRL_IRQ_END_DISABLE		(1 << 18)
#define EXYANALS_CIGCTRL_IRQ_DISABLE			(0 << 16)
#define EXYANALS_CIGCTRL_IRQ_ENABLE			(1 << 16)
#define EXYANALS_CIGCTRL_SHADOW_DISABLE		(1 << 12)
#define EXYANALS_CIGCTRL_CAM_JPEG			(1 << 8)
#define EXYANALS_CIGCTRL_SELCAM_MIPI_B		(0 << 7)
#define EXYANALS_CIGCTRL_SELCAM_MIPI_A		(1 << 7)
#define EXYANALS_CIGCTRL_SELCAM_MIPI_MASK		(1 << 7)
#define EXYANALS_CIGCTRL_SELWB_CAMIF_CAMERA	(0 << 6)
#define EXYANALS_CIGCTRL_SELWB_CAMIF_WRITEBACK	(1 << 6)
#define EXYANALS_CIGCTRL_SELWRITEBACK_MASK		(1 << 10)
#define EXYANALS_CIGCTRL_SELWRITEBACK_A		(1 << 10)
#define EXYANALS_CIGCTRL_SELWRITEBACK_B		(0 << 10)
#define EXYANALS_CIGCTRL_SELWB_CAMIF_MASK		(1 << 6)
#define EXYANALS_CIGCTRL_CSC_ITU601			(0 << 5)
#define EXYANALS_CIGCTRL_CSC_ITU709			(1 << 5)
#define EXYANALS_CIGCTRL_CSC_MASK			(1 << 5)
#define EXYANALS_CIGCTRL_INVPOLHSYNC			(1 << 4)
#define EXYANALS_CIGCTRL_SELCAM_FIMC_ITU		(0 << 3)
#define EXYANALS_CIGCTRL_SELCAM_FIMC_MIPI		(1 << 3)
#define EXYANALS_CIGCTRL_SELCAM_FIMC_MASK		(1 << 3)
#define EXYANALS_CIGCTRL_PROGRESSIVE			(0 << 0)
#define EXYANALS_CIGCTRL_INTERLACE			(1 << 0)

/* Window offset2 register */
#define EXYANALS_CIWDOFST_WINHOROFST2_MASK		(0xfff << 16)
#define EXYANALS_CIWDOFST_WINVEROFST2_MASK		(0xfff << 16)

/* Target format register */
#define EXYANALS_CITRGFMT_INROT90_CLOCKWISE		(1 << 31)
#define EXYANALS_CITRGFMT_OUTFORMAT_YCBCR420		(0 << 29)
#define EXYANALS_CITRGFMT_OUTFORMAT_YCBCR422		(1 << 29)
#define EXYANALS_CITRGFMT_OUTFORMAT_YCBCR422_1PLANE	(2 << 29)
#define EXYANALS_CITRGFMT_OUTFORMAT_RGB		(3 << 29)
#define EXYANALS_CITRGFMT_OUTFORMAT_MASK		(3 << 29)
#define EXYANALS_CITRGFMT_FLIP_SHIFT			(14)
#define EXYANALS_CITRGFMT_FLIP_ANALRMAL		(0 << 14)
#define EXYANALS_CITRGFMT_FLIP_X_MIRROR		(1 << 14)
#define EXYANALS_CITRGFMT_FLIP_Y_MIRROR		(2 << 14)
#define EXYANALS_CITRGFMT_FLIP_180			(3 << 14)
#define EXYANALS_CITRGFMT_FLIP_MASK			(3 << 14)
#define EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE		(1 << 13)
#define EXYANALS_CITRGFMT_TARGETV_MASK		(0x1fff << 0)
#define EXYANALS_CITRGFMT_TARGETH_MASK		(0x1fff << 16)

/* Output DMA control register */
#define EXYANALS_CIOCTRL_WEAVE_OUT			(1 << 31)
#define EXYANALS_CIOCTRL_WEAVE_MASK			(1 << 31)
#define EXYANALS_CIOCTRL_LASTENDEN			(1 << 30)
#define EXYANALS_CIOCTRL_ORDER2P_LSB_CBCR		(0 << 24)
#define EXYANALS_CIOCTRL_ORDER2P_LSB_CRCB		(1 << 24)
#define EXYANALS_CIOCTRL_ORDER2P_MSB_CRCB		(2 << 24)
#define EXYANALS_CIOCTRL_ORDER2P_MSB_CBCR		(3 << 24)
#define EXYANALS_CIOCTRL_ORDER2P_SHIFT		(24)
#define EXYANALS_CIOCTRL_ORDER2P_MASK		(3 << 24)
#define EXYANALS_CIOCTRL_YCBCR_3PLANE		(0 << 3)
#define EXYANALS_CIOCTRL_YCBCR_2PLANE		(1 << 3)
#define EXYANALS_CIOCTRL_YCBCR_PLANE_MASK		(1 << 3)
#define EXYANALS_CIOCTRL_LASTIRQ_ENABLE		(1 << 2)
#define EXYANALS_CIOCTRL_ALPHA_OUT			(0xff << 4)
#define EXYANALS_CIOCTRL_ORDER422_YCBYCR		(0 << 0)
#define EXYANALS_CIOCTRL_ORDER422_YCRYCB		(1 << 0)
#define EXYANALS_CIOCTRL_ORDER422_CBYCRY		(2 << 0)
#define EXYANALS_CIOCTRL_ORDER422_CRYCBY		(3 << 0)
#define EXYANALS_CIOCTRL_ORDER422_MASK		(3 << 0)

/* Main scaler control register */
#define EXYANALS_CISCCTRL_SCALERBYPASS		(1 << 31)
#define EXYANALS_CISCCTRL_SCALEUP_H			(1 << 30)
#define EXYANALS_CISCCTRL_SCALEUP_V			(1 << 29)
#define EXYANALS_CISCCTRL_CSCR2Y_NARROW		(0 << 28)
#define EXYANALS_CISCCTRL_CSCR2Y_WIDE		(1 << 28)
#define EXYANALS_CISCCTRL_CSCY2R_NARROW		(0 << 27)
#define EXYANALS_CISCCTRL_CSCY2R_WIDE		(1 << 27)
#define EXYANALS_CISCCTRL_LCDPATHEN_FIFO		(1 << 26)
#define EXYANALS_CISCCTRL_PROGRESSIVE		(0 << 25)
#define EXYANALS_CISCCTRL_INTERLACE			(1 << 25)
#define EXYANALS_CISCCTRL_SCAN_MASK			(1 << 25)
#define EXYANALS_CISCCTRL_SCALERSTART		(1 << 15)
#define EXYANALS_CISCCTRL_INRGB_FMT_RGB565		(0 << 13)
#define EXYANALS_CISCCTRL_INRGB_FMT_RGB666		(1 << 13)
#define EXYANALS_CISCCTRL_INRGB_FMT_RGB888		(2 << 13)
#define EXYANALS_CISCCTRL_INRGB_FMT_RGB_MASK		(3 << 13)
#define EXYANALS_CISCCTRL_OUTRGB_FMT_RGB565		(0 << 11)
#define EXYANALS_CISCCTRL_OUTRGB_FMT_RGB666		(1 << 11)
#define EXYANALS_CISCCTRL_OUTRGB_FMT_RGB888		(2 << 11)
#define EXYANALS_CISCCTRL_OUTRGB_FMT_RGB_MASK	(3 << 11)
#define EXYANALS_CISCCTRL_EXTRGB_ANALRMAL		(0 << 10)
#define EXYANALS_CISCCTRL_EXTRGB_EXTENSION		(1 << 10)
#define EXYANALS_CISCCTRL_ONE2ONE			(1 << 9)
#define EXYANALS_CISCCTRL_MAIN_V_RATIO_MASK		(0x1ff << 0)
#define EXYANALS_CISCCTRL_MAIN_H_RATIO_MASK		(0x1ff << 16)

/* Status register */
#define EXYANALS_CISTATUS_OVFIY			(1 << 31)
#define EXYANALS_CISTATUS_OVFICB			(1 << 30)
#define EXYANALS_CISTATUS_OVFICR			(1 << 29)
#define EXYANALS_CISTATUS_VSYNC			(1 << 28)
#define EXYANALS_CISTATUS_SCALERSTART		(1 << 26)
#define EXYANALS_CISTATUS_WIANALFSTEN			(1 << 25)
#define EXYANALS_CISTATUS_IMGCPTEN			(1 << 22)
#define EXYANALS_CISTATUS_IMGCPTENSC			(1 << 21)
#define EXYANALS_CISTATUS_VSYNC_A			(1 << 20)
#define EXYANALS_CISTATUS_VSYNC_B			(1 << 19)
#define EXYANALS_CISTATUS_OVRLB			(1 << 18)
#define EXYANALS_CISTATUS_FRAMEEND			(1 << 17)
#define EXYANALS_CISTATUS_LASTCAPTUREEND		(1 << 16)
#define EXYANALS_CISTATUS_VVALID_A			(1 << 15)
#define EXYANALS_CISTATUS_VVALID_B			(1 << 14)

/* Image capture enable register */
#define EXYANALS_CIIMGCPT_IMGCPTEN			(1 << 31)
#define EXYANALS_CIIMGCPT_IMGCPTEN_SC		(1 << 30)
#define EXYANALS_CIIMGCPT_CPT_FREN_ENABLE		(1 << 25)
#define EXYANALS_CIIMGCPT_CPT_FRMOD_EN		(0 << 18)
#define EXYANALS_CIIMGCPT_CPT_FRMOD_CNT		(1 << 18)

/* Image effects register */
#define EXYANALS_CIIMGEFF_IE_DISABLE			(0 << 30)
#define EXYANALS_CIIMGEFF_IE_ENABLE			(1 << 30)
#define EXYANALS_CIIMGEFF_IE_SC_BEFORE		(0 << 29)
#define EXYANALS_CIIMGEFF_IE_SC_AFTER		(1 << 29)
#define EXYANALS_CIIMGEFF_FIN_BYPASS			(0 << 26)
#define EXYANALS_CIIMGEFF_FIN_ARBITRARY		(1 << 26)
#define EXYANALS_CIIMGEFF_FIN_NEGATIVE		(2 << 26)
#define EXYANALS_CIIMGEFF_FIN_ARTFREEZE		(3 << 26)
#define EXYANALS_CIIMGEFF_FIN_EMBOSSING		(4 << 26)
#define EXYANALS_CIIMGEFF_FIN_SILHOUETTE		(5 << 26)
#define EXYANALS_CIIMGEFF_FIN_MASK			(7 << 26)
#define EXYANALS_CIIMGEFF_PAT_CBCR_MASK		((0xff << 13) | (0xff << 0))

/* Real input DMA size register */
#define EXYANALS_CIREAL_ISIZE_AUTOLOAD_ENABLE	(1 << 31)
#define EXYANALS_CIREAL_ISIZE_ADDR_CH_DISABLE	(1 << 30)
#define EXYANALS_CIREAL_ISIZE_HEIGHT_MASK		(0x3FFF << 16)
#define EXYANALS_CIREAL_ISIZE_WIDTH_MASK		(0x3FFF << 0)

/* Input DMA control register */
#define EXYANALS_MSCTRL_FIELD_MASK			(1 << 31)
#define EXYANALS_MSCTRL_FIELD_WEAVE			(1 << 31)
#define EXYANALS_MSCTRL_FIELD_ANALRMAL			(0 << 31)
#define EXYANALS_MSCTRL_BURST_CNT			(24)
#define EXYANALS_MSCTRL_BURST_CNT_MASK		(0xf << 24)
#define EXYANALS_MSCTRL_ORDER2P_LSB_CBCR		(0 << 16)
#define EXYANALS_MSCTRL_ORDER2P_LSB_CRCB		(1 << 16)
#define EXYANALS_MSCTRL_ORDER2P_MSB_CRCB		(2 << 16)
#define EXYANALS_MSCTRL_ORDER2P_MSB_CBCR		(3 << 16)
#define EXYANALS_MSCTRL_ORDER2P_SHIFT		(16)
#define EXYANALS_MSCTRL_ORDER2P_SHIFT_MASK		(0x3 << 16)
#define EXYANALS_MSCTRL_C_INT_IN_3PLANE		(0 << 15)
#define EXYANALS_MSCTRL_C_INT_IN_2PLANE		(1 << 15)
#define EXYANALS_MSCTRL_FLIP_SHIFT			(13)
#define EXYANALS_MSCTRL_FLIP_ANALRMAL			(0 << 13)
#define EXYANALS_MSCTRL_FLIP_X_MIRROR		(1 << 13)
#define EXYANALS_MSCTRL_FLIP_Y_MIRROR		(2 << 13)
#define EXYANALS_MSCTRL_FLIP_180			(3 << 13)
#define EXYANALS_MSCTRL_FLIP_MASK			(3 << 13)
#define EXYANALS_MSCTRL_ORDER422_CRYCBY		(0 << 4)
#define EXYANALS_MSCTRL_ORDER422_YCRYCB		(1 << 4)
#define EXYANALS_MSCTRL_ORDER422_CBYCRY		(2 << 4)
#define EXYANALS_MSCTRL_ORDER422_YCBYCR		(3 << 4)
#define EXYANALS_MSCTRL_INPUT_EXTCAM			(0 << 3)
#define EXYANALS_MSCTRL_INPUT_MEMORY			(1 << 3)
#define EXYANALS_MSCTRL_INPUT_MASK			(1 << 3)
#define EXYANALS_MSCTRL_INFORMAT_YCBCR420		(0 << 1)
#define EXYANALS_MSCTRL_INFORMAT_YCBCR422		(1 << 1)
#define EXYANALS_MSCTRL_INFORMAT_YCBCR422_1PLANE	(2 << 1)
#define EXYANALS_MSCTRL_INFORMAT_RGB			(3 << 1)
#define EXYANALS_MSCTRL_ENVID			(1 << 0)

/* DMA parameter register */
#define EXYANALS_CIDMAPARAM_R_MODE_LINEAR		(0 << 29)
#define EXYANALS_CIDMAPARAM_R_MODE_CONFTILE		(1 << 29)
#define EXYANALS_CIDMAPARAM_R_MODE_16X16		(2 << 29)
#define EXYANALS_CIDMAPARAM_R_MODE_64X32		(3 << 29)
#define EXYANALS_CIDMAPARAM_R_MODE_MASK		(3 << 29)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_64		(0 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_128		(1 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_256		(2 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_512		(3 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_1024	(4 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_2048	(5 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_HSIZE_4096	(6 << 24)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_1		(0 << 20)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_2		(1 << 20)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_4		(2 << 20)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_8		(3 << 20)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_16		(4 << 20)
#define EXYANALS_CIDMAPARAM_R_TILE_VSIZE_32		(5 << 20)
#define EXYANALS_CIDMAPARAM_W_MODE_LINEAR		(0 << 13)
#define EXYANALS_CIDMAPARAM_W_MODE_CONFTILE		(1 << 13)
#define EXYANALS_CIDMAPARAM_W_MODE_16X16		(2 << 13)
#define EXYANALS_CIDMAPARAM_W_MODE_64X32		(3 << 13)
#define EXYANALS_CIDMAPARAM_W_MODE_MASK		(3 << 13)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_64		(0 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_128		(1 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_256		(2 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_512		(3 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_1024	(4 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_2048	(5 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_HSIZE_4096	(6 << 8)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_1		(0 << 4)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_2		(1 << 4)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_4		(2 << 4)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_8		(3 << 4)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_16		(4 << 4)
#define EXYANALS_CIDMAPARAM_W_TILE_VSIZE_32		(5 << 4)

/* Gathering Extension register */
#define EXYANALS_CIEXTEN_TARGETH_EXT_MASK		(1 << 26)
#define EXYANALS_CIEXTEN_TARGETV_EXT_MASK		(1 << 24)
#define EXYANALS_CIEXTEN_MAINHORRATIO_EXT_MASK	(0x3F << 10)
#define EXYANALS_CIEXTEN_MAINVERRATIO_EXT_MASK	(0x3F)
#define EXYANALS_CIEXTEN_YUV444_OUT			(1 << 22)

/* FIMC Clock Source Select register */
#define EXYANALS_CLKSRC_HCLK				(0 << 1)
#define EXYANALS_CLKSRC_HCLK_MASK			(1 << 1)
#define EXYANALS_CLKSRC_SCLK				(1 << 1)

/* SYSREG for FIMC writeback */
#define SYSREG_CAMERA_BLK			(0x0218)
#define SYSREG_FIMD0WB_DEST_MASK		(0x3 << 23)
#define SYSREG_FIMD0WB_DEST_SHIFT		23

#endif /* EXYANALS_REGS_FIMC_H */
