/* SPDX-License-Identifier: MIT */

#ifndef __AST_REG_H__
#define __AST_REG_H__

#include <linux/bits.h>

/*
 * Modesetting
 */

#define AST_IO_MM_OFFSET		(0x380)
#define AST_IO_MM_LENGTH		(128)

#define AST_IO_VGAARI_W			(0x40)

#define AST_IO_VGAMR_W			(0x42)
#define AST_IO_VGAMR_R			(0x4c)
#define AST_IO_VGAMR_IOSEL		BIT(0)

#define AST_IO_VGAER			(0x43)
#define AST_IO_VGAER_VGA_ENABLE		BIT(0)

#define AST_IO_VGASRI			(0x44)
#define AST_IO_VGASR1_SD		BIT(5)
#define AST_IO_VGADRR			(0x47)
#define AST_IO_VGADWR			(0x48)
#define AST_IO_VGAPDR		        (0x49)
#define AST_IO_VGAGRI			(0x4E)

#define AST_IO_VGACRI			(0x54)
#define AST_IO_VGACR80_PASSWORD		(0xa8)
#define AST_IO_VGACRA1_VGAIO_DISABLED	BIT(1)
#define AST_IO_VGACRA1_MMIO_ENABLED	BIT(2)
#define AST_IO_VGACRB6_HSYNC_OFF	BIT(0)
#define AST_IO_VGACRB6_VSYNC_OFF	BIT(1)
#define AST_IO_VGACRCB_HWC_16BPP	BIT(0) /* set: ARGB4444, cleared: 2bpp palette */
#define AST_IO_VGACRCB_HWC_ENABLED	BIT(1)

#define AST_IO_VGACRD1_MCU_FW_EXECUTING		BIT(5)
/* Display Transmitter Type */
#define AST_IO_VGACRD1_TX_TYPE_MASK		GENMASK(3, 1)
#define AST_IO_VGACRD1_NO_TX			0x00
#define AST_IO_VGACRD1_TX_ITE66121_VBIOS	0x02
#define AST_IO_VGACRD1_TX_SIL164_VBIOS		0x04
#define AST_IO_VGACRD1_TX_CH7003_VBIOS		0x06
#define AST_IO_VGACRD1_TX_DP501_VBIOS		0x08
#define AST_IO_VGACRD1_TX_ANX9807_VBIOS		0x0a
#define AST_IO_VGACRD1_TX_FW_EMBEDDED_FW	0x0c /* special case of DP501 */
#define AST_IO_VGACRD1_TX_ASTDP			0x0e

#define AST_IO_VGACRD7_EDID_VALID_FLAG	BIT(0)
#define AST_IO_VGACRDC_LINK_SUCCESS	BIT(0)
#define AST_IO_VGACRDF_HPD		BIT(0)
#define AST_IO_VGACRDF_DP_VIDEO_ENABLE	BIT(4) /* mirrors AST_IO_VGACRE3_DP_VIDEO_ENABLE */
#define AST_IO_VGACRE3_DP_VIDEO_ENABLE	BIT(0)
#define AST_IO_VGACRE3_DP_PHY_SLEEP	BIT(4)
#define AST_IO_VGACRE5_EDID_READ_DONE	BIT(0)

#define AST_IO_VGAIR1_R			(0x5A)
#define AST_IO_VGAIR1_VREFRESH		BIT(3)


#define AST_VRAM_INIT_STATUS_MASK	GENMASK(7, 6)
//#define AST_VRAM_INIT_BY_BMC		BIT(7)
//#define AST_VRAM_INIT_READY		BIT(6)

/*
 * AST DisplayPort
 */

/*
 * ASTDP setmode registers:
 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
 * CRE1[7:0]: MISC1 (default: 0x00)
 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
 */
#define ASTDP_MISC0_24bpp		BIT(5)
#define ASTDP_MISC1			0
#define ASTDP_AND_CLEAR_MASK		0x00

#endif
