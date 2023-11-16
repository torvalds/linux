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
#define AST_IO_VGADRR			(0x47)
#define AST_IO_VGADWR			(0x48)
#define AST_IO_VGAPDR		        (0x49)
#define AST_IO_VGAGRI			(0x4E)

#define AST_IO_VGACRI			(0x54)
#define AST_IO_VGACR80_PASSWORD		(0xa8)
#define AST_IO_VGACRA1_VGAIO_DISABLED	BIT(1)
#define AST_IO_VGACRA1_MMIO_ENABLED	BIT(2)
#define AST_IO_VGACRCB_HWC_16BPP	BIT(0) /* set: ARGB4444, cleared: 2bpp palette */
#define AST_IO_VGACRCB_HWC_ENABLED	BIT(1)

#define AST_IO_VGAIR1_R			(0x5A)
#define AST_IO_VGAIR1_VREFRESH		BIT(3)

/*
 * Display Transmitter Type
 */

#define TX_TYPE_MASK			GENMASK(3, 1)
#define NO_TX				(0 << 1)
#define ITE66121_VBIOS_TX		(1 << 1)
#define SI164_VBIOS_TX			(2 << 1)
#define CH7003_VBIOS_TX			(3 << 1)
#define DP501_VBIOS_TX			(4 << 1)
#define ANX9807_VBIOS_TX		(5 << 1)
#define TX_FW_EMBEDDED_FW_TX		(6 << 1)
#define ASTDP_DPMCU_TX			(7 << 1)

#define AST_VRAM_INIT_STATUS_MASK	GENMASK(7, 6)
//#define AST_VRAM_INIT_BY_BMC		BIT(7)
//#define AST_VRAM_INIT_READY		BIT(6)

/*
 * AST DisplayPort
 */

/* Define for Soc scratched reg used on ASTDP */
#define AST_DP_PHY_SLEEP		BIT(4)
#define AST_DP_VIDEO_ENABLE		BIT(0)

/*
 * CRD1[b5]: DP MCU FW is executing
 * CRDC[b0]: DP link success
 * CRDF[b0]: DP HPD
 * CRE5[b0]: Host reading EDID process is done
 */
#define ASTDP_MCU_FW_EXECUTING		BIT(5)
#define ASTDP_LINK_SUCCESS		BIT(0)
#define ASTDP_HPD			BIT(0)
#define ASTDP_HOST_EDID_READ_DONE	BIT(0)
#define ASTDP_HOST_EDID_READ_DONE_MASK	GENMASK(0, 0)

/*
 * CRB8[b1]: Enable VSYNC off
 * CRB8[b0]: Enable HSYNC off
 */
#define AST_DPMS_VSYNC_OFF		BIT(1)
#define AST_DPMS_HSYNC_OFF		BIT(0)

/*
 * CRDF[b4]: Mirror of AST_DP_VIDEO_ENABLE
 * Precondition:	A. ~AST_DP_PHY_SLEEP  &&
 *			B. DP_HPD &&
 *			C. DP_LINK_SUCCESS
 */
#define ASTDP_MIRROR_VIDEO_ENABLE	BIT(4)

#define ASTDP_EDID_READ_POINTER_MASK	GENMASK(7, 0)
#define ASTDP_EDID_VALID_FLAG_MASK	GENMASK(0, 0)
#define ASTDP_EDID_READ_DATA_MASK	GENMASK(7, 0)

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
