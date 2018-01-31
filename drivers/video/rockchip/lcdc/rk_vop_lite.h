/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RK_VOPLITE_H_
#define RK_VOPLITE_H_

#include <linux/rk_fb.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define VOP_INPUT_MAX_WIDTH 2048

/*
 * Registers in this file
 * REG_CFG_DONE: Register config done flag
 * VERSION_INFO: Version for vop
 * DSP_BG: Background color
 * MCU_RESERVED: Reversed
 * SYS_CTRL0: System control register0
 * SYS_CTRL1: Axi Bus interface control register
 * SYS_CTRL2: System control register for immediate reg
 * DSP_CTRL0: Display control register0
 * DSP_CTRL2: Display control register2
 * VOP_STATUS: Some vop module status
 * LINE_FLAG: Line flag config register
 * INTR_EN: Interrupt enable register
 * INTR_CLEAR: Interrupt clear register
 * INTR_STATUS: Interrupt raw status and interrupt status
 * WIN0_CTRL0: Win0 ctrl register0
 * WIN0_CTRL1: Win0 ctrl register1
 * WIN0_COLOR_KEY: Win0 color key register
 * WIN0_VIR: Win0 virtual stride
 * WIN0_YRGB_MST: Win0 YRGB memory start address
 * WIN0_CBR_MST: Win0 Cbr memory start address
 * WIN0_ACT_INFO: Win0 active window width/height
 * WIN0_DSP_INFO: Win0 display width/height on panel
 * WIN0_DSP_ST: Win0 display start point on panel
 * WIN0_SCL_FACTOR_YRGB: Win0 YRGB scaling factor
 * WIN0_SCL_FACTOR_CBR: Win0 Cbr scaling factor
 * WIN0_SCL_OFFSET: Win0 scaling start point offset
 * WIN0_ALPHA_CTRL: Win0 Blending control register
 * WIN1_CTRL0: Win1 ctrl register0
 * WIN1_CTRL1: Win1 ctrl register1
 * WIN1_VIR: win1 virtual stride
 * WIN1_YRGB_MST: Win1 frame buffer memory start address
 * WIN1_DSP_INFO: Win1 display width/height on panel
 * WIN1_DSP_ST: Win1 display start point on panel
 * WIN1_COLOR_KEY: Win1 color key register
 * WIN1_ALPHA_CTRL: Win1 Blending control register
 * HWC_CTRL0: Hwc ctrl register0
 * HWC_CTRL1: Hwc ctrl register1
 * HWC_MST: Hwc memory start address
 * HWC_DSP_ST: Hwc display start point on panel
 * HWC_ALPHA_CTRL: Hwc blending control register
 * DSP_HTOTAL_HS_END: Panel scanning horizontal width and hsync pulse end point
 * DSP_HACT_ST_END: Panel active horizontal scanning start point and end point
 * DSP_VTOTAL_VS_END: Panel scanning vertical height and vsync pulse end point
 * DSP_VACT_ST_END: Panel active vertical scanning start point and end point
 * DSP_VS_ST_END_F1: Vertical scanning start point and vsync pulse end point
 *                   of even filed in interlace mode
 * DSP_VACT_ST_END_F1: Vertical scanning active start point and end point of
 *                     even filed in interlace mode
 * BCSH_CTRL: BCSH contrl register
 * BCSH_COLOR_BAR: Color bar config register
 * BCSH_BCS: Brightness contrast saturation*contrast config register
 * BCSH_H: Sin hue and cos hue config register
 * FRC_LOWER01_0: FRC lookup table config register010
 * FRC_LOWER01_1: FRC lookup table config register011
 * FRC_LOWER10_0: FRC lookup table config register100
 * FRC_LOWER10_1: FRC lookup table config register101
 * FRC_LOWER11_0: FRC lookup table config register110
 * FRC_LOWER11_1: FRC lookup table config register111
 * DBG_REG_00:	Current line number of dsp timing
 * BLANKING_VALUE: The value of vsync blanking
 * FLAG_REG_FRM_VALID: Flag reg value after frame valid
 * FLAG_REG: Flag reg value before frame valid
 * HWC_LUT_ADDR: Hwc lut base address
 * GAMMA_LUT_ADDR: GAMMA lut base address
 */

static inline u64 val_mask(int val, u64 msk, int shift)
{
	return (msk << (shift + 32)) | ((msk & val) << shift);
}

#define VAL_MASK(x, width, shift) val_mask(x, (1 << width) - 1, shift)

#define MASK(x) (V_##x(0) >> 32)

#define REG_CFG_DONE			0x00000000
#define  V_REG_LOAD_GLOBAL_EN(x)		VAL_MASK(x, 1, 0)
#define  V_REG_LOAD_WIN0_EN(x)			VAL_MASK(x, 1, 1)
#define  V_REG_LOAD_WIN1_EN(x)			VAL_MASK(x, 1, 2)
#define  V_REG_LOAD_HWC_EN(x)			VAL_MASK(x, 1, 3)
#define  V_REG_LOAD_IEP_EN(x)			VAL_MASK(x, 1, 4)
#define  V_REG_LOAD_SYS_EN(x)			VAL_MASK(x, 1, 5)
#define VERSION				0x00000004
#define  V_BUILD(x)				VAL_MASK(x, 16, 0)
#define  V_MINOR(x)				VAL_MASK(x, 8, 16)
#define  V_MAJOR(x)				VAL_MASK(x, 8, 24)
#define DSP_BG				0x00000008
#define  V_DSP_BG_BLUE(x)			VAL_MASK(x, 8, 0)
#define  V_DSP_BG_GREEN(x)			VAL_MASK(x, 8, 8)
#define  V_DSP_BG_RED(x)			VAL_MASK(x, 8, 16)
#define MCU_RESERVED			0x0000000c
#define SYS_CTRL0			0x00000010
#define  V_DIRECT_PATH_EN(x)			VAL_MASK(x, 1, 0)
#define  V_DIRECT_PATH_LAYER_SEL(x)		VAL_MASK(x, 1, 1)
#define SYS_CTRL1			0x00000014
#define  V_SW_NOC_QOS_EN(x)			VAL_MASK(x, 1, 0)
#define  V_SW_NOC_QOS_VALUE(x)			VAL_MASK(x, 2, 1)
#define  V_SW_NOC_HURRY_EN(x)			VAL_MASK(x, 1, 4)
#define  V_SW_NOC_HURRY_VALUE(x)		VAL_MASK(x, 2, 5)
#define  V_SW_NOC_HURRY_THRESHOLD(x)		VAL_MASK(x, 4, 8)
#define  V_SW_AXI_MAX_OUTSTAND_EN(x)		VAL_MASK(x, 1, 12)
#define  V_SW_AXI_MAX_OUTSTAND_NUM(x)		VAL_MASK(x, 5, 16)
#define SYS_CTRL2			0x00000018
#define  V_IMD_AUTO_GATING_EN(x)		VAL_MASK(x, 1, 0)
#define  V_IMD_VOP_STANDBY_EN(x)		VAL_MASK(x, 1, 1)
#define  V_IMD_VOP_DMA_STOP(x)			VAL_MASK(x, 1, 2)
#define  V_IMD_DSP_OUT_ZERO(x)			VAL_MASK(x, 1, 3)
#define  V_IMD_YUV_CLIP(x)			VAL_MASK(x, 1, 4)
#define  V_IMD_DSP_DATA_OUT_MODE(x)		VAL_MASK(x, 1, 6)
#define  V_SW_IO_PAD_CLK_SEL(x)			VAL_MASK(x, 1, 7)
#define  V_IMD_DSP_TIMING_IMD(x)		VAL_MASK(x, 1, 12)
#define  V_IMD_GLOBAL_REGDONE_EN(x)		VAL_MASK(x, 1, 13)
#define  V_FS_ADDR_MASK_EN(x)			VAL_MASK(x, 1, 14)
#define DSP_CTRL0			0x00000020
#define  V_RGB_DCLK_EN(x)			VAL_MASK(x, 1, 0)
#define  V_RGB_DCLK_POL(x)			VAL_MASK(x, 1, 1)
#define  V_RGB_HSYNC_POL(x)			VAL_MASK(x, 1, 2)
#define  V_RGB_VSYNC_POL(x)			VAL_MASK(x, 1, 3)
#define  V_RGB_DEN_POL(x)			VAL_MASK(x, 1, 4)
#define  V_HDMI_DCLK_EN(x)			VAL_MASK(x, 1, 8)
#define  V_HDMI_DCLK_POL(x)			VAL_MASK(x, 1, 9)
#define  V_HDMI_HSYNC_POL(x)			VAL_MASK(x, 1, 10)
#define  V_HDMI_VSYNC_POL(x)			VAL_MASK(x, 1, 11)
#define  V_HDMI_DEN_POL(x)			VAL_MASK(x, 1, 12)
#define  V_SW_CORE_CLK_SEL(x)			VAL_MASK(x, 1, 13)
#define  V_SW_HDMI_CLK_I_SEL(x)			VAL_MASK(x, 1, 14)
#define  V_LVDS_DCLK_EN(x)			VAL_MASK(x, 1, 16)
#define  V_LVDS_DCLK_POL(x)			VAL_MASK(x, 1, 17)
#define  V_LVDS_HSYNC_POL(x)			VAL_MASK(x, 1, 18)
#define  V_LVDS_VSYNC_POL(x)			VAL_MASK(x, 1, 19)
#define  V_LVDS_DEN_POL(x)			VAL_MASK(x, 1, 20)
#define  V_MIPI_DCLK_EN(x)			VAL_MASK(x, 1, 24)
#define  V_MIPI_DCLK_POL(x)			VAL_MASK(x, 1, 25)
#define  V_MIPI_HSYNC_POL(x)			VAL_MASK(x, 1, 26)
#define  V_MIPI_VSYNC_POL(x)			VAL_MASK(x, 1, 27)
#define  V_MIPI_DEN_POL(x)			VAL_MASK(x, 1, 28)
#define DSP_CTRL2			0x00000028
#define  V_DSP_INTERLACE(x)			VAL_MASK(x, 1, 0)
#define  V_INTERLACE_FIELD_POL(x)		VAL_MASK(x, 1, 1)
#define  V_DITHER_UP(x)				VAL_MASK(x, 1, 2)
#define  V_DSP_WIN0_TOP(x)			VAL_MASK(x, 1, 3)
#define  V_SW_OVERLAY_MODE(x)			VAL_MASK(x, 1, 4)
#define  V_DSP_LUT_EN(x)			VAL_MASK(x, 1, 5)
#define  V_DITHER_DOWN_MODE(x)			VAL_MASK(x, 1, 6)
#define  V_DITHER_DOWN_SEL(x)			VAL_MASK(x, 1, 7)
#define  V_DITHER_DOWN(x)			VAL_MASK(x, 1, 8)
#define  V_DSP_BG_SWAP(x)			VAL_MASK(x, 1, 9)
#define  V_DSP_DELTA_SWAP(x)			VAL_MASK(x, 1, 10)
#define  V_DSP_RB_SWAP(x)			VAL_MASK(x, 1, 11)
#define  V_DSP_RG_SWAP(x)			VAL_MASK(x, 1, 12)
#define  V_DSP_DUMMY_SWAP(x)			VAL_MASK(x, 1, 13)
#define  V_DSP_BLANK_EN(x)			VAL_MASK(x, 1, 14)
#define  V_DSP_BLACK_EN(x)			VAL_MASK(x, 1, 15)
#define  V_DSP_OUT_MODE(x)			VAL_MASK(x, 4, 16)
#define VOP_STATUS			0x0000002c
#define  V_DSP_BLANKING_EN_ASYNC_AFF2(x)	VAL_MASK(x, 1, 0)
#define  V_IDLE_MMU_FF1(x)			VAL_MASK(x, 1, 1)
#define  V_INT_RAW_DMA_FINISH(x)		VAL_MASK(x, 1, 2)
#define  V_DMA_STOP_VALID(x)			VAL_MASK(x, 1, 4)
#define LINE_FLAG			0x00000030
#define  V_DSP_LINE_FLAG0_NUM(x)		VAL_MASK(x, 12, 0)
#define  V_DSP_LINE_FLAG1_NUM(x)		VAL_MASK(x, 12, 16)
#define INTR_EN				0x00000034
#define  V_FS0_INTR_EN(x)			VAL_MASK(x, 1, 0)
#define  V_FS1_INTR_EN(x)			VAL_MASK(x, 1, 1)
#define  V_ADDR_SAME_INTR_EN(x)			VAL_MASK(x, 1, 2)
#define  V_LINE_FLAG0_INTR_EN(x)		VAL_MASK(x, 1, 3)
#define  V_LINE_FLAG1_INTR_EN(x)		VAL_MASK(x, 1, 4)
#define  V_BUS_ERROR_INTR_EN(x)			VAL_MASK(x, 1, 5)
#define  V_WIN0_EMPTY_INTR_EN(x)		VAL_MASK(x, 1, 6)
#define  V_WIN1_EMPTY_INTR_EN(x)		VAL_MASK(x, 1, 7)
#define  V_DSP_HOLD_VALID_INTR_EN(x)		VAL_MASK(x, 1, 8)
#define  V_DMA_FRM_FSH_INTR_EN(x)		VAL_MASK(x, 1, 9)
#define INTR_CLEAR			0x00000038
#define  V_FS0_INTR_CLR(x)			VAL_MASK(x, 1, 0)
#define  V_FS1_INTR_CLR(x)			VAL_MASK(x, 1, 1)
#define  V_ADDR_SAME_INTR_CLR(x)		VAL_MASK(x, 1, 2)
#define  V_LINE_FLAG0_INTR_CLR(x)		VAL_MASK(x, 1, 3)
#define  V_LINE_FLAG1_INTR_CLR(x)		VAL_MASK(x, 1, 4)
#define  V_BUS_ERROR_INTR_CLR(x)		VAL_MASK(x, 1, 5)
#define  V_WIN0_EMPTY_INTR_CLR(x)		VAL_MASK(x, 1, 6)
#define  V_WIN1_EMPTY_INTR_CLR(x)		VAL_MASK(x, 1, 7)
#define  V_DSP_HOLD_VALID_INTR_CLR(x)		VAL_MASK(x, 1, 8)
#define  V_DMA_FRM_FSH_INTR_CLR(x)		VAL_MASK(x, 1, 9)
#define INTR_STATUS			0x0000003c
#define  V_FS0_INTR_STS(x)			VAL_MASK(x, 1, 0)
#define  V_FS1_INTR_STS(x)			VAL_MASK(x, 1, 1)
#define  V_ADDR_SAME_INTR_STS(x)		VAL_MASK(x, 1, 2)
#define  V_LINE_FLAG0_INTR_STS(x)		VAL_MASK(x, 1, 3)
#define  V_LINE_FLAG1_INTR_STS(x)		VAL_MASK(x, 1, 4)
#define  V_BUS_ERROR_INTR_STS(x)		VAL_MASK(x, 1, 5)
#define  V_WIN0_EMPTY_INTR_STS(x)		VAL_MASK(x, 1, 6)
#define  V_WIN1_EMPTY_INTR_STS(x)		VAL_MASK(x, 1, 7)
#define  V_DSP_HOLD_VALID_INTR_STS(x)		VAL_MASK(x, 1, 8)
#define  V_DMA_FRM_FSH_INTR_STS(x)		VAL_MASK(x, 1, 9)
#define  V_MMU_INTR_STATUS(x)			VAL_MASK(x, 1, 15)
#define  V_FS0_INTR_RAW_STS(x)			VAL_MASK(x, 1, 16)
#define  V_FS1_INTR_RAW_STS(x)			VAL_MASK(x, 1, 17)
#define  V_ADDR_SAME_INTR_RAW_STS(x)		VAL_MASK(x, 1, 18)
#define  V_LINE_FLAG0_INTR_RAW_STS(x)		VAL_MASK(x, 1, 19)
#define  V_LINE_FLAG1_INTR_RAW_STS(x)		VAL_MASK(x, 1, 20)
#define  V_BUS_ERROR_INTR_RAW_STS(x)		VAL_MASK(x, 1, 21)
#define  V_WIN0_EMPTY_INTR_RAW_STS(x)		VAL_MASK(x, 1, 22)
#define  V_WIN1_EMPTY_INTR_RAW_STS(x)		VAL_MASK(x, 1, 23)
#define  V_DSP_HOLD_VALID_INTR_RAW_STS(x)	VAL_MASK(x, 1, 24)
#define  V_DMA_FRM_FSH_INTR_RAW_STS(x)		VAL_MASK(x, 1, 25)
#define WIN0_CTRL0			0x00000050
#define  V_WIN0_EN(x)				VAL_MASK(x, 1, 0)
#define  V_WIN0_DATA_FMT(x)			VAL_MASK(x, 3, 1)
#define  V_WIN0_INTERLACE_READ(x)		VAL_MASK(x, 1, 8)
#define  V_WIN0_NO_OUTSTANDING(x)		VAL_MASK(x, 1, 9)
#define  V_WIN0_CSC_MODE(x)			VAL_MASK(x, 2, 10)
#define  V_WIN0_RB_SWAP(x)			VAL_MASK(x, 1, 12)
#define  V_WIN0_ALPHA_SWAP(x)			VAL_MASK(x, 1, 13)
#define  V_WIN0_MID_SWAP(x)			VAL_MASK(x, 1, 14)
#define  V_WIN0_UV_SWAP(x)			VAL_MASK(x, 1, 15)
#define  V_WIN0_YRGB_DEFLICK(x)			VAL_MASK(x, 1, 18)
#define  V_WIN0_CBR_DEFLICK(x)			VAL_MASK(x, 1, 19)
#define WIN0_CTRL1			0x00000054
#define  V_WIN0_YRGB_AXI_GATHER_EN(x)		VAL_MASK(x, 1, 0)
#define  V_WIN0_CBR_AXI_GATHER_EN(x)		VAL_MASK(x, 1, 1)
#define  V_WIN0_DMA_BURST_LENGTH(x)		VAL_MASK(x, 2, 2)
#define  V_WIN0_YRGB_AXI_GATHER_NUM(x)		VAL_MASK(x, 4, 4)
#define  V_WIN0_CBR_AXI_GATHER_NUM(x)		VAL_MASK(x, 3, 8)
#define  V_SW_WIN0_YRGB0_RID(x)			VAL_MASK(x, 4, 12)
#define  V_SW_WIN0_CBR0_RID(x)			VAL_MASK(x, 4, 16)
#define WIN0_COLOR_KEY			0x00000058
#define  V_WIN0_KEY_COLOR(x)			VAL_MASK(x, 24, 0)
#define  V_WIN0_KEY_EN(x)			VAL_MASK(x, 1, 24)
#define WIN0_VIR			0x0000005c
#define  V_WIN0_YRGB_VIR_STRIDE(x)		VAL_MASK(x, 13, 0)
#define  V_WIN0_CBR_VIR_STRIDE(x)		VAL_MASK(x, 13, 16)
#define WIN0_YRGB_MST			0x00000060
#define WIN0_CBR_MST			0x00000064
#define WIN0_ACT_INFO			0x00000068
#define  V_WIN0_ACT_WIDTH(x)			VAL_MASK(x, 13, 0)
#define  V_WIN0_ACT_HEIGHT(x)			VAL_MASK(x, 13, 16)
#define WIN0_DSP_INFO			0x0000006c
#define  V_WIN0_DSP_WIDTH(x)			VAL_MASK(x, 11, 0)
#define  V_WIN0_DSP_HEIGHT(x)			VAL_MASK(x, 11, 16)
#define WIN0_DSP_ST			0x00000070
#define  V_WIN0_DSP_XST(x)			VAL_MASK(x, 12, 0)
#define  V_WIN0_DSP_YST(x)			VAL_MASK(x, 12, 16)
#define WIN0_SCL_FACTOR_YRGB		0x00000074
#define  V_WIN0_HS_FACTOR_YRGB(x)		VAL_MASK(x, 16, 0)
#define  V_WIN0_VS_FACTOR_YRGB(x)		VAL_MASK(x, 16, 16)
#define WIN0_SCL_FACTOR_CBR		0x00000078
#define  V_WIN0_HS_FACTOR_CBR(x)		VAL_MASK(x, 16, 0)
#define  V_WIN0_VS_FACTOR_CBR(x)		VAL_MASK(x, 16, 16)
#define WIN0_SCL_OFFSET			0x0000007c
#define  V_WIN0_HS_OFFSET_YRGB(x)		VAL_MASK(x, 8, 0)
#define  V_WIN0_HS_OFFSET_CBR(x)		VAL_MASK(x, 8, 8)
#define  V_WIN0_VS_OFFSET_YRGB(x)		VAL_MASK(x, 8, 16)
#define  V_WIN0_VS_OFFSET_CBR(x)		VAL_MASK(x, 8, 24)
#define WIN0_ALPHA_CTRL			0x00000080
#define  V_WIN0_ALPHA_EN(x)			VAL_MASK(x, 1, 0)
#define  V_WIN0_ALPHA_MODE(x)			VAL_MASK(x, 1, 1)
#define  V_WIN0_ALPHA_PRE_MUL(x)		VAL_MASK(x, 1, 2)
#define  V_WIN0_ALPHA_SAT_MODE(x)		VAL_MASK(x, 1, 3)
#define  V_WIN0_ALPHA_VALUE(x)			VAL_MASK(x, 8, 4)
#define WIN1_CTRL0			0x00000090
#define  V_WIN1_EN(x)				VAL_MASK(x, 1, 0)
#define  V_WIN1_CSC_MODE(x)			VAL_MASK(x, 1, 2)
#define  V_WIN1_DATA_FMT(x)			VAL_MASK(x, 3, 4)
#define  V_WIN1_INTERLACE_READ(x)		VAL_MASK(x, 1, 8)
#define  V_WIN1_NO_OUTSTANDING(x)		VAL_MASK(x, 1, 9)
#define  V_WIN1_RB_SWAP(x)			VAL_MASK(x, 1, 12)
#define  V_WIN1_ALPHA_SWAP(x)			VAL_MASK(x, 1, 13)
#define  V_WIN1_ENDIAN_SWAP(x)			VAL_MASK(x, 1, 14)
#define WIN1_CTRL1			0x00000094
#define  V_WIN1_AXI_GATHER_EN(x)		VAL_MASK(x, 1, 0)
#define  V_WIN1_DMA_BURST_LENGTH(x)		VAL_MASK(x, 2, 2)
#define  V_WIN1_AXI_GATHER_NUM(x)		VAL_MASK(x, 4, 4)
#define  V_SW_WIN1_RID(x)			VAL_MASK(x, 4, 8)
#define WIN1_VIR			0x00000098
#define  V_WIN1_VIR_STRIDE(x)			VAL_MASK(x, 13, 0)
#define WIN1_YRGB_MST			0x000000a0
#define WIN1_DSP_INFO			0x000000a4
#define  V_WIN1_DSP_WIDTH(x)			VAL_MASK(x, 11, 0)
#define  V_WIN1_DSP_HEIGHT(x)			VAL_MASK(x, 11, 16)
#define WIN1_DSP_ST			0x000000a8
#define  V_WIN1_DSP_XST(x)			VAL_MASK(x, 12, 0)
#define  V_WIN1_DSP_YST(x)			VAL_MASK(x, 12, 16)
#define WIN1_COLOR_KEY			0x000000ac
#define  V_WIN1_KEY_COLOR(x)			VAL_MASK(x, 24, 0)
#define  V_WIN1_KEY_EN(x)			VAL_MASK(x, 1, 24)
#define WIN1_ALPHA_CTRL			0x000000bc
#define  V_WIN1_ALPHA_EN(x)			VAL_MASK(x, 1, 0)
#define  V_WIN1_ALPHA_MODE(x)			VAL_MASK(x, 1, 1)
#define  V_WIN1_ALPHA_PRE_MUL(x)		VAL_MASK(x, 1, 2)
#define  V_WIN1_ALPHA_SAT_MODE(x)		VAL_MASK(x, 1, 3)
#define  V_WIN1_ALPHA_VALUE(x)			VAL_MASK(x, 8, 4)
#define HWC_CTRL0			0x000000e0
#define  V_HWC_EN(x)				VAL_MASK(x, 1, 0)
#define  V_HWC_SIZE(x)				VAL_MASK(x, 1, 1)
#define  V_HWC_LOAD_EN(x)			VAL_MASK(x, 1, 2)
#define  V_HWC_LUT_EN(x)			VAL_MASK(x, 1, 3)
#define  V_SW_HWC_RID(x)			VAL_MASK(x, 4, 4)
#define HWC_CTRL1			0x000000e4
#define HWC_MST				0x000000e8
#define HWC_DSP_ST			0x000000ec
#define  V_HWC_DSP_XST(x)			VAL_MASK(x, 12, 0)
#define  V_HWC_DSP_YST(x)			VAL_MASK(x, 12, 16)
#define HWC_ALPHA_CTRL			0x000000f0
#define  V_HWC_ALPHA_EN(x)			VAL_MASK(x, 1, 0)
#define  V_HWC_ALPHA_MODE(x)			VAL_MASK(x, 1, 1)
#define  V_HWC_ALPHA_PRE_MUL(x)			VAL_MASK(x, 1, 2)
#define  V_HWC_ALPHA_SAT_MODE(x)		VAL_MASK(x, 1, 3)
#define  V_HWC_ALPHA_VALUE(x)			VAL_MASK(x, 8, 4)
#define DSP_HTOTAL_HS_END		0x00000100
#define  V_DSP_HS_END(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_HTOTAL(x)			VAL_MASK(x, 12, 16)
#define DSP_HACT_ST_END			0x00000104
#define  V_DSP_HACT_END(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_HACT_ST(x)			VAL_MASK(x, 12, 16)
#define DSP_VTOTAL_VS_END		0x00000108
#define  V_DSP_VS_END(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_VTOTAL(x)			VAL_MASK(x, 12, 16)
#define DSP_VACT_ST_END			0x0000010c
#define  V_DSP_VACT_END(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_VACT_ST(x)			VAL_MASK(x, 12, 16)
#define DSP_VS_ST_END_F1		0x00000110
#define  V_DSP_VS_END_F1(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_VS_ST_F1(x)			VAL_MASK(x, 12, 16)
#define DSP_VACT_ST_END_F1		0x00000114
#define  V_DSP_VACT_END_F1(x)			VAL_MASK(x, 12, 0)
#define  V_DSP_VACT_ST_F1(x)			VAL_MASK(x, 12, 16)
#define BCSH_CTRL			0x00000160
#define  V_BCSH_EN(x)				VAL_MASK(x, 1, 0)
#define  V_SW_BCSH_R2Y_CSC_MODE(x)		VAL_MASK(x, 1, 1)
#define  V_VIDEO_MODE(x)			VAL_MASK(x, 2, 2)
#define  V_SW_BCSH_Y2R_CSC_MODE(x)		VAL_MASK(x, 2, 4)
#define  V_SW_BCSH_Y2R_EN(x)			VAL_MASK(x, 1, 6)
#define  V_SW_BCSH_R2Y_EN(x)			VAL_MASK(x, 1, 7)
#define BCSH_COL_BAR			0x00000164
#define  V_COLOR_BAR_Y(x)			VAL_MASK(x, 8, 0)
#define  V_COLOR_BAR_U(x)			VAL_MASK(x, 8, 8)
#define  V_COLOR_BAR_V(x)			VAL_MASK(x, 8, 16)
#define BCSH_BCS			0x00000168
#define  V_BRIGHTNESS(x)			VAL_MASK(x, 6, 0)
#define  V_CONTRAST(x)				VAL_MASK(x, 8, 8)
#define  V_SAT_CON(x)				VAL_MASK(x, 9, 16)
#define BCSH_H				0x0000016c
#define  V_SIN_HUE(x)				VAL_MASK(x, 8, 0)
#define  V_COS_HUE(x)				VAL_MASK(x, 8, 8)
#define FRC_LOWER01_0			0x00000170
#define  V_LOWER01_FRM0(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER01_FRM1(x)			VAL_MASK(x, 16, 16)
#define FRC_LOWER01_1			0x00000174
#define  V_LOWER01_FRM2(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER01_FRM3(x)			VAL_MASK(x, 16, 16)
#define FRC_LOWER10_0			0x00000178
#define  V_LOWER10_FRM0(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER10_FRM1(x)			VAL_MASK(x, 16, 16)
#define FRC_LOWER10_1			0x0000017c
#define  V_LOWER10_FRM2(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER10_FRM3(x)			VAL_MASK(x, 16, 16)
#define FRC_LOWER11_0			0x00000180
#define  V_LOWER11_FRM0(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER11_FRM1(x)			VAL_MASK(x, 16, 16)
#define FRC_LOWER11_1			0x00000184
#define  V_LOWER11_FRM2(x)			VAL_MASK(x, 16, 0)
#define  V_LOWER11_FRM3(x)			VAL_MASK(x, 16, 16)
#define DBG_REG_000			0x00000190
#define BLANKING_VALUE			0x000001f4
#define  V_SW_BLANKING_VALUE(x)			VAL_MASK(x, 24, 0)
#define  V_BLANKING_VALUE_CONFIG_EN(x)		VAL_MASK(x, 1, 24)
#define FLAG_REG_FRM_VALID		0x000001f8
#define FLAG_REG			0x000001fc
#define HWC_LUT_ADDR			0x00000600
#define GAMMA_LUT_ADDR			0x00000a00
#define MMU_DTE_ADDR			0x00000f00
#define MMU_STATUS			0x00000f04
#define  V_PAGING_ENABLED(x)			VAL_MASK(x, 1, 0)
#define  V_PAGE_FAULT_ACTIVE(x)			VAL_MASK(x, 1, 1)
#define  V_STAIL_ACTIVE(x)			VAL_MASK(x, 1, 2)
#define  V_MMU_IDLE(x)				VAL_MASK(x, 1, 3)
#define  V_REPLAY_BUFFER_EMPTY(x)		VAL_MASK(x, 1, 4)
#define  V_PAGE_FAULT_IS_WRITE(x)		VAL_MASK(x, 1, 5)
#define MMU_COMMAND			0x00000f08
#define MMU_PAGE_FAULT_ADDR		0x00000f0c
#define MMU_ZAP_ONE_LINE		0x00000f10
#define MMU_INT_RAWSTAT			0x00000f14
#define  V_PAGE_FAULT(x)			VAL_MASK(x, 1, 0)
#define MMU_INT_CLEAR			0x00000f18
#define  V_PAGE_FAULT(x)			VAL_MASK(x, 1, 0)
#define MMU_INT_MASK			0x00000f1c
#define  V_PAGE_FAULT(x)			VAL_MASK(x, 1, 0)
#define MMU_INT_STATUS			0x00000f20
#define  V_PAGE_FAULT(x)			VAL_MASK(x, 1, 0)
#define MMU_AUTO_GATING			0x00000f24
#define  V_MMU_AUTO_GATING(x)			VAL_MASK(x, 1, 0)
#define MMU_CFG_DONE			0x00000f28

#define INTR_FS0		BIT(0)
#define INTR_FS1		BIT(1)
#define INTR_ADDR_SAME		BIT(2)
#define INTR_LINE_FLAG0		BIT(3)
#define INTR_LINE_FLAG1		BIT(4)
#define INTR_BUS_ERROR		BIT(5)
#define INTR_WIN0_EMPTY		BIT(6)
#define INTR_WIN1_EMPTY		BIT(7)
#define INTR_DSP_HOLD_VALID	BIT(8)
#define INTR_DMA_FINISH		BIT(9)
#define INTR_MMU_STATUS		BIT(15)

#define INTR_MASK (INTR_FS0 | INTR_FS1 | INTR_ADDR_SAME | INTR_LINE_FLAG0 | \
			INTR_LINE_FLAG1 | INTR_BUS_ERROR | INTR_WIN0_EMPTY | \
			INTR_WIN1_EMPTY | INTR_DSP_HOLD_VALID | INTR_DMA_FINISH)

/* GRF register for VOP source select */
#define GRF_WEN_SHIFT(x)	(BIT(x) << 16)

#define GRF_SOC_CON0		0x0400
#define V_LVDS_VOP_SEL(x)		(((x) << 0) | GRF_WEN_SHIFT(0))
#define V_HDMI_VOP_SEL(x)		(((x) << 1) | GRF_WEN_SHIFT(1))
#define V_DSI0_VOP_SEL(x)		(((x) << 2) | GRF_WEN_SHIFT(2))

#define GRF_SOC_CON5		0x0414
#define V_RGB_VOP_SEL(x)		(((x) << 4) | GRF_WEN_SHIFT(4))

#define GRF_IO_VSEL		0x0900
#define V_VOP_IOVOL_SEL(x)		(((x) << 0) | GRF_WEN_SHIFT(0))

struct vop_sync_obj_s {
	struct completion stdbyfin;	/* standby finish */
	int stdbyfin_to;
	struct completion frmst;	/* frame start */
	int frmst_to;
};

struct vop_device {
	int id;
	struct rk_lcdc_driver driver;
	struct device *dev;
	struct rk_screen *screen;

	void __iomem *regs;
	void *regsbak;
	u32 reg_phy_base;
	u32 len;
	void __iomem *hwc_lut_addr_base;
	void __iomem *dsp_lut_addr_base;
	struct regmap *grf_base;

	/* one time only one process allowed to config the register */
	spinlock_t reg_lock;

	int prop;	/* used for primary or extended display device */
	bool pre_init;
	bool pwr18;	/* if lcdc use 1.8v power supply */
	/* if aclk or hclk is closed, access to register is not allowed */
	bool clk_on;
	/* active layer counter,when atv_layer_cnt = 0,disable lcdc */
	u8 atv_layer_cnt;

	unsigned int		irq;

	struct clk		*hclk;	/* lcdc AHP clk */
	struct clk		*dclk;	/* lcdc dclk */
	struct clk		*aclk;	/* lcdc share memory frequency */
	u32 pixclock;

	u32 standby;		/* 1:standby,0:wrok */
	u32 iommu_status;
	struct backlight_device *backlight;

	/* lock vop irq reg */
	spinlock_t irq_lock;
	struct vop_sync_obj_s sync;
};

static inline int vop_completion_timeout_ms(struct completion *comp, int to)
{
	long jiffies = msecs_to_jiffies(to);

	return wait_for_completion_timeout(comp, jiffies);
}

static inline void vop_writel(struct vop_device *vop_dev, u32 offset, u32 v)
{
	u32 *_pv = (u32 *)vop_dev->regsbak;

	_pv += (offset >> 2);
	*_pv = v;
	writel_relaxed(v, vop_dev->regs + offset);
}

static inline u32 vop_readl(struct vop_device *vop_dev, u32 offset)
{
	u32 v;

	v = readl_relaxed(vop_dev->regs + offset);
	return v;
}

static inline u32 vop_readl_backup(struct vop_device *vop_dev, u32 offset)
{
	u32 v;
	u32 *_pv = (u32 *)vop_dev->regsbak;

	_pv += (offset >> 2);
	v = readl_relaxed(vop_dev->regs + offset);
	*_pv = v;
	return v;
}

static inline u32 vop_read_bit(struct vop_device *vop_dev, u32 offset, u64 v)
{
	u32 _v = readl_relaxed(vop_dev->regs + offset);

	_v &= v >> 32;
	v = (_v ? 1 : 0);
	return v;
}

static inline void vop_set_bit(struct vop_device *vop_dev, u32 offset, u64 v)
{
	u32 *_pv = (u32 *)vop_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) |= v >> 32;
	writel_relaxed(*_pv, vop_dev->regs + offset);
}

static inline void vop_clr_bit(struct vop_device *vop_dev, u32 offset, u64 v)
{
	u32 *_pv = (u32 *)vop_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) &= (~(v >> 32));
	writel_relaxed(*_pv, vop_dev->regs + offset);
}

static inline void vop_msk_reg(struct vop_device *vop_dev, u32 offset, u64 v)
{
	u32 *_pv = (u32 *)vop_dev->regsbak;

	_pv += (offset >> 2);
	(*_pv) &= (~(v >> 32));
	(*_pv) |= (u32)v;
	writel_relaxed(*_pv, vop_dev->regs + offset);
}

static inline void vop_mask_writel(struct vop_device *vop_dev, u32 offset,
				   u32 mask, u32 v)
{
	v = mask << 16 | v;
	writel_relaxed(v, vop_dev->regs + offset);
}

static inline void vop_cfg_done(struct vop_device *vop_dev)
{
	writel_relaxed(0x001f001f, vop_dev->regs + REG_CFG_DONE);
	dsb(sy);
}

static inline int vop_grf_writel(struct regmap *base, u32 offset, u32 val)
{
	if (base)
		regmap_write(base, offset, val);
	dsb(sy);

	return 0;
}

static inline int vop_cru_writel(struct regmap *base, u32 offset, u32 val)
{
	if (base)
		regmap_write(base, offset, val);
	dsb(sy);

	return 0;
}

static inline int vop_cru_readl(struct regmap *base, u32 offset)
{
	u32 v;

	if (base)
		regmap_read(base, offset, &v);

	return v;
}

enum dither_down_mode {
	DITHER_888_565 = 0x0,
	DITHER_888_666 = 0x1,
};

enum dither_down_sel {
	DITHER_SEL_ALLEGRO = 0x0,
	DITHER_SEL_FRC = 0x1,
};

enum _vop_r2y_csc_mode {
	VOP_R2Y_CSC_BT601 = 0,
	VOP_R2Y_CSC_BT709
};

enum _vop_y2r_csc_mode {
	VOP_Y2R_CSC_MPEG = 0,
	VOP_Y2R_CSC_HD,
	VOP_Y2R_CSC_JPEG,
	VOP_Y2R_CSC_BYPASS
};

enum _vop_format {
	VOP_FORMAT_ARGB888 = 0,
	VOP_FORMAT_RGB888,
	VOP_FORMAT_RGB565,
	VOP_FORMAT_YCBCR420 = 4,
	VOP_FORMAT_YCBCR422,
	VOP_FORMAT_YCBCR444
};

enum _bcsh_video_mode {
	BCSH_MODE_BLACK = 0,
	BCSH_MODE_BLUE,
	BCSH_MODE_COLORBAR,
	BCSH_MODE_VIDEO,
};

#define IS_YUV(x) ((x) >= VOP_FORMAT_YCBCR420)

enum _vop_overlay_mode {
	VOP_RGB_DOMAIN,
	VOP_YUV_DOMAIN
};

/*************************************************************/
#define CALSCALE(x, y)  \
	(1 == (y) ? 0x1000 : ((((u32)((x) - 1)) * 0x1000) / ((y) - 1)))

#endif
