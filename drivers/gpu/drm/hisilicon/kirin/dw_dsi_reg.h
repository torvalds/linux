/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 HiSilicon Limited.
 */

#ifndef __DW_DSI_REG_H__
#define __DW_DSI_REG_H__

#define MASK(x)				(BIT(x) - 1)

/*
 * regs
 */
#define PWR_UP                  0x04  /* Core power-up */
#define RESET                   0
#define POWERUP                 BIT(0)
#define PHY_IF_CFG              0xA4  /* D-PHY interface configuration */
#define CLKMGR_CFG              0x08  /* the internal clock dividers */
#define PHY_RSTZ                0xA0  /* D-PHY reset control */
#define PHY_ENABLECLK           BIT(2)
#define PHY_UNRSTZ              BIT(1)
#define PHY_UNSHUTDOWNZ         BIT(0)
#define PHY_TST_CTRL0           0xB4  /* D-PHY test interface control 0 */
#define PHY_TST_CTRL1           0xB8  /* D-PHY test interface control 1 */
#define CLK_TLPX                0x10
#define CLK_THS_PREPARE         0x11
#define CLK_THS_ZERO            0x12
#define CLK_THS_TRAIL           0x13
#define CLK_TWAKEUP             0x14
#define DATA_TLPX(x)            (0x20 + ((x) << 4))
#define DATA_THS_PREPARE(x)     (0x21 + ((x) << 4))
#define DATA_THS_ZERO(x)        (0x22 + ((x) << 4))
#define DATA_THS_TRAIL(x)       (0x23 + ((x) << 4))
#define DATA_TTA_GO(x)          (0x24 + ((x) << 4))
#define DATA_TTA_GET(x)         (0x25 + ((x) << 4))
#define DATA_TWAKEUP(x)         (0x26 + ((x) << 4))
#define PHY_CFG_I               0x60
#define PHY_CFG_PLL_I           0x63
#define PHY_CFG_PLL_II          0x64
#define PHY_CFG_PLL_III         0x65
#define PHY_CFG_PLL_IV          0x66
#define PHY_CFG_PLL_V           0x67
#define DPI_COLOR_CODING        0x10  /* DPI color coding */
#define DPI_CFG_POL             0x14  /* DPI polarity configuration */
#define VID_HSA_TIME            0x48  /* Horizontal Sync Active time */
#define VID_HBP_TIME            0x4C  /* Horizontal Back Porch time */
#define VID_HLINE_TIME          0x50  /* Line time */
#define VID_VSA_LINES           0x54  /* Vertical Sync Active period */
#define VID_VBP_LINES           0x58  /* Vertical Back Porch period */
#define VID_VFP_LINES           0x5C  /* Vertical Front Porch period */
#define VID_VACTIVE_LINES       0x60  /* Vertical resolution */
#define VID_PKT_SIZE            0x3C  /* Video packet size */
#define VID_MODE_CFG            0x38  /* Video mode configuration */
#define PHY_TMR_CFG             0x9C  /* Data lanes timing configuration */
#define BTA_TO_CNT              0x8C  /* Response timeout definition */
#define PHY_TMR_LPCLK_CFG       0x98  /* clock lane timing configuration */
#define CLK_DATA_TMR_CFG        0xCC
#define LPCLK_CTRL              0x94  /* Low-power in clock lane */
#define PHY_TXREQUESTCLKHS      BIT(0)
#define MODE_CFG                0x34  /* Video or Command mode selection */
#define PHY_STATUS              0xB0  /* D-PHY PPI status interface */

#define	PHY_STOP_WAIT_TIME      0x30

/*
 * regs relevant enum
 */
enum dpi_color_coding {
	DSI_24BITS_1 = 5,
};

enum dsi_video_mode_type {
	DSI_NON_BURST_SYNC_PULSES = 0,
	DSI_NON_BURST_SYNC_EVENTS,
	DSI_BURST_SYNC_PULSES_1,
	DSI_BURST_SYNC_PULSES_2
};

enum dsi_work_mode {
	DSI_VIDEO_MODE = 0,
	DSI_COMMAND_MODE
};

/*
 * Register Write/Read Helper functions
 */
static inline void dw_update_bits(void __iomem *addr, u32 bit_start,
				  u32 mask, u32 val)
{
	u32 tmp, orig;

	orig = readl(addr);
	tmp = orig & ~(mask << bit_start);
	tmp |= (val & mask) << bit_start;
	writel(tmp, addr);
}

#endif /* __DW_DRM_DSI_H__ */
