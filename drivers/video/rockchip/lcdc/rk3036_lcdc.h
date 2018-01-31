/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RK3036_LCDC_H_
#define _RK3036_LCDC_H_

#include<linux/rk_fb.h>
#include<linux/io.h>
#include<linux/clk.h>

/*******************register definition**********************/

#define SYS_CTRL		(0x00)
	#define m_WIN0_EN		(1<<0)
	#define m_WIN1_EN		(1<<1)
	#define m_HWC_EN		(1<<2)
	#define m_WIN0_FORMAT		(7<<3)
	#define m_WIN1_FORMAT		(7<<6)
	#define m_HWC_LUT_EN		(1<<9)
	#define m_HWC_SIZE		(1<<10)
	#define m_WIN0_RB_SWAP		(1<<15)
	#define m_WIN0_ALPHA_SWAP	(1<<16)
	#define m_WIN0_Y8_SWAP		(1<<17)
	#define m_WIN0_UV_SWAP		(1<<18)
	#define m_WIN1_RB_SWAP		(1<<19)
	#define m_WIN1_ALPHA_SWAP	(1<<20)
	#define m_WIN0_OTSD_DISABLE	(1<<22)
	#define m_WIN1_OTSD_DISABLE	(1<<23)
	#define m_DMA_BURST_LENGTH	(3<<24)
	#define m_HWC_LODAD_EN		(1<<26)
	#define m_DMA_STOP		(1<<29)
	#define m_LCDC_STANDBY		(1<<30)
	#define m_AUTO_GATING_EN	(1<<31)

	#define v_WIN0_EN(x)		(((x)&1)<<0)
	#define v_WIN1_EN(x)		(((x)&1)<<1)
	#define v_HWC_EN(x)		(((x)&1)<<2)
	#define v_WIN0_FORMAT(x)	(((x)&7)<<3)
	#define v_WIN1_FORMAT(x)	(((x)&7)<<6)
	#define v_HWC_LUT_EN(x)		(((x)&1)<<9)
	#define v_HWC_SIZE(x)		(((x)&1)<<10)
	#define v_WIN0_RB_SWAP(x)	(((x)&1)<<15)
	#define v_WIN0_ALPHA_SWAP(x)	(((x)&1)<<16)
	#define v_WIN0_Y8_SWAP(x)	(((x)&1)<<17)
	#define v_WIN0_UV_SWAP(x)	(((x)&1)<<18)
	#define v_WIN1_RB_SWAP(x)	(((x)&1)<<19)
	#define v_WIN1_ALPHA_SWAP(x)	(((x)&1)<<20)
	#define v_WIN0_OTSD_DISABLE(x)	(((x)&1)<<22)
	#define v_WIN1_OTSD_DISABLE(x)	(((x)&1)<<23)
	#define v_DMA_BURST_LENGTH(x)	(((x)&3)<<24)
	#define v_HWC_LODAD_EN(x)	(((x)&1)<<26)
	#define v_WIN1_LUT_EN(x)	(((x)&1)<<27)
	#define v_DMA_STOP(x)		(((x)&1)<<29)
	#define v_LCDC_STANDBY(x)	(((x)&1)<<30)
	#define v_AUTO_GATING_EN(x)	(((x)&1)<<31)

#define DSP_CTRL0		(0x04)
	#define m_DSP_OUT_FORMAT	(0x0f<<0)
	#define m_HSYNC_POL		(1<<4)
	#define m_VSYNC_POL		(1<<5)
	#define m_DEN_POL		(1<<6)
	#define m_DCLK_POL		(1<<7)
	#define m_WIN0_TOP		(1<<8)
	#define m_DITHER_UP_EN		(1<<9)
	#define m_INTERLACE_DSP_EN	(1<<12)
	#define m_INTERLACE_DSP_POL	(1<<13)
	#define m_WIN0_INTERLACE_EN	(1<<14)
	#define m_WIN1_INTERLACE_EN	(1<<15)
	#define m_WIN0_YRGB_DEFLICK_EN	(1<<16)
	#define m_WIN0_CBR_DEFLICK_EN	(1<<17)
	#define m_WIN0_ALPHA_MODE	(1<<18)
	#define m_WIN1_ALPHA_MODE	(1<<19)
	#define m_WIN0_CSC_MODE		(3<<20)
	#define m_WIN0_YUV_CLIP		(1<<23)
	#define m_TVE_MODE		(1<<25)
	#define m_HWC_ALPHA_MODE	(1<<28)
	#define m_PREMUL_ALPHA_ENABLE	(1<<29)
	#define m_ALPHA_MODE_SEL1	(1<<30)
	#define m_WIN1_DIFF_DCLK_EN	(1<<31)

	#define v_DSP_OUT_FORMAT(x)	(((x)&0x0f)<<0)
	#define v_HSYNC_POL(x)		(((x)&1)<<4)
	#define v_VSYNC_POL(x)		(((x)&1)<<5)
	#define v_DEN_POL(x)		(((x)&1)<<6)
	#define v_DCLK_POL(x)		(((x)&1)<<7)
	#define v_WIN0_TOP(x)		(((x)&1)<<8)
	#define v_DITHER_UP_EN(x)	(((x)&1)<<9)
	#define v_INTERLACE_DSP_EN(x)	(((x)&1)<<12)
	#define v_INTERLACE_DSP_POL(x)	(((x)&1)<<13)
	#define v_WIN0_INTERLACE_EN(x)	(((x)&1)<<14)
	#define v_WIN1_INTERLACE_EN(x)	(((x)&1)<<15)
	#define v_WIN0_YRGB_DEFLICK_EN(x)	(((x)&1)<<16)
	#define v_WIN0_CBR_DEFLICK_EN(x)	(((x)&1)<<17)
	#define v_WIN0_ALPHA_MODE(x)		(((x)&1)<<18)
	#define v_WIN1_ALPHA_MODE(x)		(((x)&1)<<19)
	#define v_WIN0_CSC_MODE(x)		(((x)&3)<<20)
	#define v_WIN0_YUV_CLIP(x)		(((x)&1)<<23)
	#define v_TVE_MODE(x)			(((x)&1)<<25)
	#define v_HWC_ALPHA_MODE(x)		(((x)&1)<<28)
	#define v_PREMUL_ALPHA_ENABLE(x)	(((x)&1)<<29)
	#define v_ALPHA_MODE_SEL1(x)		(((x)&1)<<30)
	#define v_WIN1_DIFF_DCLK_EN(x)		(((x)&1)<<31)

#define DSP_CTRL1		(0x08)
	#define m_BG_COLOR		(0xffffff<<0)
	#define m_BG_B			(0xff<<0)
	#define m_BG_G			(0xff<<8)
	#define m_BG_R			(0xff<<16)
	#define m_BLANK_EN		(1<<24)
	#define m_BLACK_EN		(1<<25)
	#define m_DSP_BG_SWAP		(1<<26)
	#define m_DSP_RB_SWAP		(1<<27)
	#define m_DSP_RG_SWAP		(1<<28)
	#define m_DSP_DELTA_SWAP	(1<<29)
	#define m_DSP_DUMMY_SWAP	(1<<30)
	#define m_DSP_OUT_ZERO		(1<<31)

	#define v_BG_COLOR(x)		(((x)&0xffffff)<<0)
	#define v_BG_B(x)		(((x)&0xff)<<0)
	#define v_BG_G(x)		(((x)&0xff)<<8)
	#define v_BG_R(x)		(((x)&0xff)<<16)
	#define v_BLANK_EN(x)		(((x)&1)<<24)
	#define v_BLACK_EN(x)		(((x)&1)<<25)
	#define v_DSP_BG_SWAP(x)	(((x)&1)<<26)
	#define v_DSP_RB_SWAP(x)	(((x)&1)<<27)
	#define v_DSP_RG_SWAP(x)	(((x)&1)<<28)
	#define v_DSP_DELTA_SWAP(x)	(((x)&1)<<29)
	#define v_DSP_DUMMY_SWAP(x)	(((x)&1)<<30)
	#define v_DSP_OUT_ZERO(x)	(((x)&1)<<31)

#define INT_STATUS		(0x10)
	#define m_HS_INT_STA		(1<<0) /* status */
	#define m_FS_INT_STA		(1<<1)
	#define m_LF_INT_STA		(1<<2)
	#define m_BUS_ERR_INT_STA	(1<<3)
	#define m_HS_INT_EN		(1<<4) /* enable */
	#define m_FS_INT_EN		(1<<5)
	#define m_LF_INT_EN		(1<<6)
	#define m_BUS_ERR_INT_EN	(1<<7)
	#define m_HS_INT_CLEAR		(1<<8) /* auto clear*/
	#define m_FS_INT_CLEAR		(1<<9)
	#define m_LF_INT_CLEAR		(1<<10)
	#define m_BUS_ERR_INT_CLEAR	(1<<11)
	#define m_LF_INT_NUM		(0xfff<<12)
	#define m_WIN0_EMPTY_INT_EN	(1<<24)
	#define m_WIN1_EMPTY_INT_EN	(1<<25)
	#define m_WIN0_EMPTY_INT_CLEAR	(1<<26)
	#define m_WIN1_EMPTY_INT_CLEAR	(1<<27)
	#define m_WIN0_EMPTY_INT_STA	(1<<28)
	#define m_WIN1_EMPTY_INT_STA	(1<<29)
	#define m_FS_RAW_STA		(1<<30)
	#define m_LF_RAW_STA		(1<<31)

	#define v_HS_INT_EN(x)			(((x)&1)<<4)
	#define v_FS_INT_EN(x)			(((x)&1)<<5)
	#define v_LF_INT_EN(x)			(((x)&1)<<6)
	#define v_BUS_ERR_INT_EN(x)		(((x)&1)<<7)
	#define v_HS_INT_CLEAR(x)		(((x)&1)<<8)
	#define v_FS_INT_CLEAR(x)		(((x)&1)<<9)
	#define v_LF_INT_CLEAR(x)		(((x)&1)<<10)
	#define v_BUS_ERR_INT_CLEAR(x)		(((x)&1)<<11)
	#define v_LF_INT_NUM(x)			(((x)&0xfff)<<12)
	#define v_WIN0_EMPTY_INT_EN(x)		(((x)&1)<<24)
	#define v_WIN1_EMPTY_INT_EN(x)		(((x)&1)<<25)
	#define v_WIN0_EMPTY_INT_CLEAR(x)	(((x)&1)<<26)
	#define v_WIN1_EMPTY_INT_CLEAR(x)	(((x)&1)<<27)


#define ALPHA_CTRL		(0x14)
	#define m_WIN0_ALPHA_EN		(1<<0)
	#define m_WIN1_ALPHA_EN		(1<<1)
	#define m_HWC_ALPAH_EN		(1<<2)
	#define m_WIN1_PREMUL_SCALE	(1<<3)
	#define m_WIN0_ALPHA_VAL	(0xff<<4)
	#define m_WIN1_ALPHA_VAL	(0xff<<12)
	#define m_HWC_ALPAH_VAL		(0xff<<20)

	#define v_WIN0_ALPHA_EN(x)	(((x)&1)<<0)
	#define v_WIN1_ALPHA_EN(x)	(((x)&1)<<1)
	#define v_HWC_ALPAH_EN(x)	(((x)&1)<<2)
	#define v_WIN1_PREMUL_SCALE(x)	(((x)&1)<<3)
	#define v_WIN0_ALPHA_VAL(x)	(((x)&0xff)<<4)
	#define v_WIN1_ALPHA_VAL(x)	(((x)&0xff)<<12)
	#define v_HWC_ALPAH_VAL(x)	(((x)&0xff)<<20)

#define WIN0_COLOR_KEY		(0x18)
#define WIN1_COLOR_KEY		(0x1C)
	#define m_COLOR_KEY_VAL		(0xffffff<<0)
	#define m_COLOR_KEY_EN		(1<<24)
	#define v_COLOR_KEY_VAL(x)	(((x)&0xffffff)<<0)
	#define v_COLOR_KEY_EN(x)	(((x)&1)<<24)

/* Layer Registers */
#define WIN0_YRGB_MST		(0x20)
#define WIN0_CBR_MST		(0x24)
#define WIN1_MST		(0xa0)
#define HWC_MST			(0x58)

#define WIN1_VIR		(0x28)
#define WIN0_VIR		(0x30)
	#define m_YRGB_VIR	(0x1fff << 0)
	#define m_CBBR_VIR	(0x1fff << 16)

	#define v_YRGB_VIR(x)	((x & 0x1fff) << 0)
	#define v_CBBR_VIR(x)	((x & 0x1fff) << 16)

	#define v_ARGB888_VIRWIDTH(x)	(((x) & 0x1fff) << 0)
	#define v_RGB888_VIRWIDTH(x)	(((((x * 3) >> 2)+(x % 3))&0x1fff)<<0)
	#define v_RGB565_VIRWIDTH(x)	((DIV_ROUND_UP(x, 2)&0x1fff)<<0)
	#define v_YUV_VIRWIDTH(x)	((DIV_ROUND_UP(x, 4)&0x1fff)<<0)
	#define v_CBCR_VIR(x)		((x & 0x1fff) << 16)

#define WIN0_ACT_INFO		(0x34)
#define WIN1_ACT_INFO		(0xB4)
	#define m_ACT_WIDTH		(0x1fff << 0)
	#define m_ACT_HEIGHT		(0x1fff << 16)
	#define v_ACT_WIDTH(x)		(((x-1) & 0x1fff)<<0)
	#define v_ACT_HEIGHT(x)		(((x-1) & 0x1fff)<<16)

#define WIN0_DSP_INFO		(0x38)
#define WIN1_DSP_INFO		(0xB8)
	#define v_DSP_WIDTH(x)		(((x-1)&0x7ff)<<0)
	#define v_DSP_HEIGHT(x)		(((x-1)&0x7ff)<<16)

#define WIN0_DSP_ST		(0x3C)
#define WIN1_DSP_ST		(0xBC)
#define HWC_DSP_ST		(0x5C)
	#define v_DSP_STX(x)		(((x)&0xfff)<<0)
	#define v_DSP_STY(x)		(((x)&0xfff)<<16)

#define WIN0_SCL_FACTOR_YRGB	(0x40)
#define WIN0_SCL_FACTOR_CBR	(0x44)
#define WIN1_SCL_FACTOR_YRGB	(0xC0)
	#define v_X_SCL_FACTOR(x)	(((x)&0xffff)<<0)
	#define v_Y_SCL_FACTOR(x)	(((x)&0xffff)<<16)

#define WIN0_SCL_OFFSET		(0x48)
#define WIN1_SCL_OFFSET		(0xC8)

/* LUT Registers */
#define WIN1_LUT_ADDR			(0x0400)
#define HWC_LUT_ADDR			(0x0800)

/* Display Infomation Registers */
#define DSP_HTOTAL_HS_END	(0x6C)
	/*hsync pulse width*/
	#define v_HSYNC(x)		(((x)&0xfff)<<0)
	/*horizontal period*/
	#define v_HORPRD(x)		(((x)&0xfff)<<16)

#define DSP_HACT_ST_END		(0x70)
	/*horizontal active end point*/
	#define v_HAEP(x)		(((x)&0xfff)<<0)
	/*horizontal active start point*/
	#define v_HASP(x)		(((x)&0xfff)<<16)

#define DSP_VTOTAL_VS_END	(0x74)
	#define v_VSYNC(x)		(((x)&0xfff)<<0)
	#define v_VERPRD(x)		(((x)&0xfff)<<16)

#define DSP_VACT_ST_END		(0x78)
	#define v_VAEP(x)		(((x)&0xfff)<<0)
	#define v_VASP(x)		(((x)&0xfff)<<16)

#define DSP_VS_ST_END_F1	(0x7C)
	#define v_VSYNC_END_F1(x)	(((x)&0xfff)<<0)
	#define v_VSYNC_ST_F1(x)	(((x)&0xfff)<<16)
#define DSP_VACT_ST_END_F1	(0x80)

/*BCSH Registers*/
#define BCSH_CTRL			(0xD0)
	#define m_BCSH_EN		(1 << 0)
	#define m_BCSH_OUT_MODE		(3 << 2)
	#define m_BCSH_CSC_MODE		(3 << 4)

	#define v_BCSH_EN(x)		((1 & x) << 0)
	#define v_BCSH_OUT_MODE(x)	((3 & x) << 2)
	#define v_BCSH_CSC_MODE(x)	((3 & x) << 4)

#define BCSH_COLOR_BAR			(0xD4)
	#define v_BCSH_COLOR_BAR_Y(x)		(((x)&0xf) << 0)
	#define v_BCSH_COLOR_BAR_U(x)		(((x)&0xf) << 8)
	#define v_BCSH_COLOR_BAR_V(x)		(((x)&0xf) << 16)

	#define m_BCSH_COLOR_BAR_Y		(0xf << 0)
	#define m_BCSH_COLOR_BAR_U		(0xf << 8)
	#define m_BCSH_COLOR_BAR_V		(0xf << 16)

#define BCSH_BCS			(0xD8)
	#define v_BCSH_BRIGHTNESS(x)		(((x)&0x3f) << 0)
	#define v_BCSH_CONTRAST(x)		(((x)&0xff) << 8)
	#define v_BCSH_SAT_CON(x)		(((x)&0x1ff) << 16)

	#define m_BCSH_BRIGHTNESS		(0x3f << 0)
	#define m_BCSH_CONTRAST			(0xff << 8)
	#define m_BCSH_SAT_CON			(0x1ff << 16)

#define BCSH_H				(0xDC)
	#define v_BCSH_SIN_HUE(x)		(((x)&0xff) << 0)
	#define v_BCSH_COS_HUE(x)		(((x)&0xff) << 8)

	#define m_BCSH_SIN_HUE			(0xff << 0)
	#define m_BCSH_COS_HUE			(0xff << 8)

/* Bus Register */
#define AXI_BUS_CTRL		(0x2C)
	#define m_IO_PAD_CLK			(1 << 31)
	#define m_CORE_CLK_DIV_EN		(1 << 30)
	#define m_HDMI_DCLK_INVERT		(1 << 23)
	#define m_HDMI_DCLK_EN			(1 << 22)
	#define m_TVE_DAC_DCLK_INVERT		(1 << 21)
	#define m_TVE_DAC_DCLK_EN		(1 << 20)
	#define m_HDMI_DCLK_DIV_EN		(1 << 19)
	#define m_AXI_OUTSTANDING_MAX_NUM	(0x1f << 12)
	#define m_AXI_MAX_OUTSTANDING_EN	(1 << 11)
	#define m_MMU_EN			(1 << 10)
	#define m_NOC_HURRY_THRESHOLD		(0xf << 6)
	#define m_NOC_HURRY_VALUE		(3 << 4)
	#define m_NOC_HURRY_EN			(1 << 3)
	#define m_NOC_QOS_VALUE			(3 << 1)
	#define m_NOC_QOS_EN			(1 << 0)

	#define v_IO_PAD_CLK(x)			((x&1) << 31)
	#define v_CORE_CLK_DIV_EN(x)		((x&1) << 30)
	#define v_HDMI_DCLK_INVERT(x)		((x&1) << 23)
	#define v_HDMI_DCLK_EN(x)		((x&1) << 22)
	#define v_TVE_DAC_DCLK_INVERT(x)	((x&1) << 21)
	#define v_TVE_DAC_DCLK_EN(x)		((x&1) << 20)
	#define v_HDMI_DCLK_DIV_EN(x)		((x&1) << 19)
	#define v_AXI_OUTSTANDING_MAX_NUM(x)	((x&0x1f) << 12)
	#define v_AXI_MAX_OUTSTANDING_EN(x)	((x&1) << 11)
	#define v_MMU_EN(x)			((x&1) << 10)
	#define v_NOC_HURRY_THRESHOLD(x)	((x&0xf) << 6)
	#define v_NOC_HURRY_VALUE(x)		((x&3) << 4)
	#define v_NOC_HURRY_EN(x)		((x&1) << 3)
	#define v_NOC_QOS_VALUE(x)		((x&3) << 1)
	#define v_NOC_QOS_EN(x)			((x&1) << 0)

#define GATHER_TRANSFER		(0x84)
	#define m_WIN1_AXI_GATHER_NUM		(0xf << 12)
	#define m_WIN0_CBCR_AXI_GATHER_NUM	(0x7 << 8)
	#define m_WIN0_YRGB_AXI_GATHER_NUM	(0xf << 4)
	#define m_WIN1_AXI_GAHTER_EN		(1 << 2)
	#define m_WIN0_CBCR_AXI_GATHER_EN	(1 << 1)
	#define m_WIN0_YRGB_AXI_GATHER_EN	(1 << 0)

	#define v_WIN1_AXI_GATHER_NUM(x)	((x & 0xf) << 12)
	#define v_WIN0_CBCR_AXI_GATHER_NUM(x)	((x & 0x7) << 8)
	#define v_WIN0_YRGB_AXI_GATHER_NUM(x)	((x & 0xf) << 4)
	#define v_WIN1_AXI_GAHTER_EN(x)		((x & 1) << 2)
	#define v_WIN0_CBCR_AXI_GATHER_EN(x)	((x & 1) << 1)
	#define v_WIN0_YRGB_AXI_GATHER_EN(x)	((x & 1) << 0)

#define VERSION_INFO		(0x94)
	#define m_MAJOR		(0xff << 24)
	#define m_MINOR		(0xff << 16)
	#define m_BUILD		(0xffff)

#define REG_CFG_DONE		(0x90)

/* TV Control Registers */
#define TV_CTRL			(0x200)
#define TV_SYNC_TIMING		(0x204)
#define TV_ACT_TIMING		(0x208)
#define TV_ADJ_TIMING		(0x20c)
#define TV_FREQ_SC		(0x210)
#define TV_FILTER0		(0x214)
#define TV_FILTER1		(0x218)
#define TV_FILTER2		(0x21C)
#define TV_ACT_ST		(0x234)
#define TV_ROUTING		(0x238)
#define TV_SYNC_ADJUST		(0x250)
#define TV_STATUS		(0x254)
#define TV_RESET		(0x268)
#define TV_SATURATION		(0x278)
#define TV_BW_CTRL		(0x28C)
#define TV_BRIGHTNESS_CONTRAST	(0x290)


/* MMU registers */
#define MMU_DTE_ADDR			(0x0300)
	#define v_MMU_DTE_ADDR(x)		(((x)&0xffffffff)<<0)
	#define m_MMU_DTE_ADDR			(0xffffffff<<0)

#define MMU_STATUS			(0x0304)
	#define v_PAGING_ENABLED(x)		(((x)&1)<<0)
	#define v_PAGE_FAULT_ACTIVE(x)		(((x)&1)<<1)
	#define v_STAIL_ACTIVE(x)		(((x)&1)<<2)
	#define v_MMU_IDLE(x)			(((x)&1)<<3)
	#define v_REPLAY_BUFFER_EMPTY(x)	(((x)&1)<<4)
	#define v_PAGE_FAULT_IS_WRITE(x)	(((x)&1)<<5)
	#define v_PAGE_FAULT_BUS_ID(x)		(((x)&0x1f)<<6)
	#define m_PAGING_ENABLED		(1<<0)
	#define m_PAGE_FAULT_ACTIVE		(1<<1)
	#define m_STAIL_ACTIVE			(1<<2)
	#define m_MMU_IDLE			(1<<3)
	#define m_REPLAY_BUFFER_EMPTY		(1<<4)
	#define m_PAGE_FAULT_IS_WRITE		(1<<5)
	#define m_PAGE_FAULT_BUS_ID		(0x1f<<6)

#define MMU_COMMAND			(0x0308)
	#define v_MMU_CMD(x)			(((x)&0x3)<<0)
	#define m_MMU_CMD			(0x3<<0)

#define MMU_PAGE_FAULT_ADDR		(0x030c)
	#define v_PAGE_FAULT_ADDR(x)		(((x)&0xffffffff)<<0)
	#define m_PAGE_FAULT_ADDR		(0xffffffff<<0)

#define MMU_ZAP_ONE_LINE		(0x0310)
	#define v_MMU_ZAP_ONE_LINE(x)		(((x)&0xffffffff)<<0)
	#define m_MMU_ZAP_ONE_LINE		(0xffffffff<<0)

#define MMU_INT_RAWSTAT			(0x0314)
	#define v_PAGE_FAULT_RAWSTAT(x)		(((x)&1)<<0)
	#define v_READ_BUS_ERROR_RAWSTAT(x)	(((x)&1)<<1)
	#define m_PAGE_FAULT_RAWSTAT		(1<<0)
	#define m_READ_BUS_ERROR_RAWSTAT	(1<<1)

#define MMU_INT_CLEAR			(0x0318)
	#define v_PAGE_FAULT_CLEAR(x)		(((x)&1)<<0)
	#define v_READ_BUS_ERROR_CLEAR(x)	(((x)&1)<<1)
	#define m_PAGE_FAULT_CLEAR		(1<<0)
	#define m_READ_BUS_ERROR_CLEAR		(1<<1)

#define MMU_INT_MASK			(0x031c)
	#define v_PAGE_FAULT_MASK(x)		(((x)&1)<<0)
	#define v_READ_BUS_ERROR_MASK(x)	(((x)&1)<<1)
	#define m_PAGE_FAULT_MASK		(1<<0)
	#define m_READ_BUS_ERROR_MASK		(1<<1)

#define MMU_INT_STATUS			(0x0320)
	#define v_PAGE_FAULT_STATUS(x)		(((x)&1)<<0)
	#define v_READ_BUS_ERROR_STATUS(x)	(((x)&1)<<1)
	#define m_PAGE_FAULT_STATUS		(1<<0)
	#define m_READ_BUS_ERROR_STATUS		(1<<1)

#define MMU_AUTO_GATING			(0x0324)
	#define v_MMU_AUTO_GATING(x)		(((x)&1)<<0)
	#define m_MMU_AUTO_GATING		(1<<0)

enum _vop_dma_burst {
	DMA_BURST_16 = 0,
	DMA_BURST_8,
	DMA_BURST_4
};

enum _vop_format_e {
	VOP_FORMAT_ARGB888 = 0,
	VOP_FORMAT_RGB888,
	VOP_FORMAT_RGB565,
	VOP_FORMAT_YCBCR420 = 4,
	VOP_FORMAT_YCBCR422,
	VOP_FORMAT_YCBCR444
};

enum _vop_tv_mode {
	TV_NTSC,
	TV_PAL,
};

enum _vop_csc_mode {
	VOP_CSC_BT601 = 0,
	VOP_CSC_JPEG,
	VOP_CSC_BT709
};

enum _vop_hwc_size {
	VOP_HWC_SIZE_32,
	VOP_HWC_SIZE_64
};

#define calscale(x, y)		((((u32)(x-1))*0x1000)/(y-1))

struct lcdc_device {
	int id;
	struct rk_lcdc_driver driver;
	struct device *dev;
	struct rk_screen *screen;

	void __iomem *regs;
	void *regsbak;		/* back up reg */
	u32 reg_phy_base;	/* physical basic address of lcdc register*/
	u32 len;		/* physical map length of lcdc register*/
	spinlock_t  reg_lock;	/* one time only one process allowed to
				   config the register*/

	int __iomem *hwc_lut_addr_base;
	int __iomem *dsp_lut_addr_base;


	int prop;			/*used for primary or */
					/*extended display device*/
	bool pre_init;
	bool pwr18;			/*if lcdc use 1.8v power supply*/
	bool clk_on;			/*if aclk or hclk is closed,
					  acess to register is not allowed*/
	u8 atv_layer_cnt;		/*active layer counter, when
					  atv_layer_cnt = 0,disable lcdc*/

	unsigned int		irq;

	struct clk		*pd;	/*lcdc power domain*/
	struct clk		*hclk;	/*lcdc AHP clk*/
	struct clk		*dclk;	/*lcdc dclk*/
	struct clk		*aclk;	/*lcdc share memory frequency*/
	u32 pixclock;

	u32 standby;			/*1:standby,0:work*/
	u32 iommu_status;
};

static inline
void lcdc_writel(struct lcdc_device *lcdc_dev, u32 offset, u32 v)
{
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (offset >> 2);
	*_pv = v;
	writel_relaxed(v, lcdc_dev->regs + offset);
}

static inline
u32 lcdc_readl(struct lcdc_device *lcdc_dev, u32 offset)
{
	u32 v;
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (offset >> 2);
	v = readl_relaxed(lcdc_dev->regs + offset);
	*_pv = v;
	return v;
}

static inline
u32 lcdc_read_bit(struct lcdc_device *lcdc_dev, u32 offset, u32 msk)
{
	u32 _v = readl_relaxed(lcdc_dev->regs + offset);

	_v &= msk;
	return _v ? 1 : 0;
}

static inline
void  lcdc_set_bit(struct lcdc_device *lcdc_dev, u32 offset, u32 msk)
{
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) |= msk;
	writel_relaxed(*_pv, lcdc_dev->regs + offset);
}

static inline
void lcdc_clr_bit(struct lcdc_device *lcdc_dev, u32 offset, u32 msk)
{
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) &= (~msk);
	writel_relaxed(*_pv, lcdc_dev->regs + offset);
}

static inline
void  lcdc_msk_reg(struct lcdc_device *lcdc_dev, u32 offset, u32 msk, u32 v)
{
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) &= (~msk);
	(*_pv) |= v;
	writel_relaxed(*_pv, lcdc_dev->regs + offset);
}

static inline void lcdc_cfg_done(struct lcdc_device *lcdc_dev)
{
	writel_relaxed(0x01, lcdc_dev->regs + REG_CFG_DONE);
	dsb();
}

#endif /* _RK3036_LCDC_H_ */
