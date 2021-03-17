/* SPDX-License-Identifier: GPL-2.0+
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __KIRIN_DPE_REG_H__
#define __KIRIN_DPE_REG_H__

#define BIT_MMU_IRPT_NS                  BIT(28)
#define BIT_ITF0_INTS                    BIT(16)
#define BIT_DPP_INTS                     BIT(15)
#define BIT_VACTIVE0_END                 BIT(8)
#define BIT_VACTIVE0_START               BIT(7)
#define BIT_VSYNC                        BIT(4)
#define BIT_LDI_UNFLOW                   BIT(2)

#define DFS_TIME                         (80)
#define DFS_TIME_MIN                     (50)
#define DFS_TIME_MIN_4K                  (10)
#define DBUF0_DEPTH                      (1408)
#define DBUF_WIDTH_BIT                   (144)
#define PERRSTDIS3                       (0x088)

#define DPE_GLB0_OFFSET                  (0x12000)
#define DPE_DBG_OFFSET                   (0x11000)
#define DPE_CMDLIST_OFFSET               (0x02000)
#define DPE_SMMU_OFFSET                  (0x08000)
#define DPE_MIF_OFFSET                   (0x0A000)
#define DPE_MCTRL_SYS_OFFSET             (0x10000)
#define DPE_MCTRL_CTL0_OFFSET            (0x10800)
#define DPE_RCH_VG0_DMA_OFFSET           (0x20000)
#define DPE_RCH_VG0_SCL_OFFSET           (0x20200)
#define DPE_RCH_VG0_ARSR_OFFSET          (0x20300)
#define DPE_RCH_VG1_DMA_OFFSET           (0x28000)
#define DPE_RCH_VG1_SCL_OFFSET           (0x28200)
#define DPE_RCH_VG2_DMA_OFFSET           (0x30000)
#define DPE_RCH_VG2_SCL_OFFSET           (0x30200)
#define DPE_RCH_G0_DMA_OFFSET            (0x38000)
#define DPE_RCH_G0_SCL_OFFSET            (0x38200)
#define DPE_RCH_G1_DMA_OFFSET            (0x40000)
#define DPE_RCH_G1_SCL_OFFSET            (0x40200)
#define DPE_RCH_D2_DMA_OFFSET            (0x50000)
#define DPE_RCH_D3_DMA_OFFSET            (0x51000)
#define DPE_RCH_D0_DMA_OFFSET            (0x52000)
#define DPE_RCH_D0_DFC_OFFSET            (0x52100)
#define DPE_RCH_D1_DMA_OFFSET            (0x53000)
#define DPE_WCH0_DMA_OFFSET              (0x5A000)
#define DPE_WCH1_DMA_OFFSET              (0x5C000)
#define DPE_WCH2_DMA_OFFSET              (0x5E000)
#define DPE_WCH2_DFC_OFFSET              (0x5E100)
#define DPE_OVL0_OFFSET                  (0x60000)
#define DPE_DBUF0_OFFSET                 (0x6D000)
#define DPE_DPP_OFFSET                   (0x70000)
#define DPE_DPP_DITHER_OFFSET            (0x70200)
#define DPE_LDI0_OFFSET                  (0x7D000)
#define DPE_IFBC_OFFSET                  (0x7D800)
#define DPE_DSC_OFFSET                   (0x7DC00)

#define GLB_CPU_PDP_INTS                 (DPE_GLB0_OFFSET + 0x224)
#define GLB_CPU_PDP_INT_MSK              (DPE_GLB0_OFFSET + 0x228)
#define GLB_CPU_SDP_INTS                 (DPE_GLB0_OFFSET + 0x22C)
#define GLB_CPU_SDP_INT_MSK              (DPE_GLB0_OFFSET + 0x230)

#define DBG_MCTL_INTS                    (0x023C)
#define DBG_MCTL_INT_MSK                 (0x0240)
#define DBG_WCH0_INTS                    (0x0244)
#define DBG_WCH0_INT_MSK                 (0x0248)
#define DBG_WCH1_INTS                    (0x024C)
#define DBG_WCH1_INT_MSK                 (0x0250)
#define DBG_RCH0_INTS                    (0x0254)
#define DBG_RCH0_INT_MSK                 (0x0258)
#define DBG_RCH1_INTS                    (0x025C)
#define DBG_RCH1_INT_MSK                 (0x0260)
#define DBG_RCH2_INTS                    (0x0264)
#define DBG_RCH2_INT_MSK                 (0x0268)
#define DBG_RCH3_INTS                    (0x026C)
#define DBG_RCH3_INT_MSK                 (0x0270)
#define DBG_RCH4_INTS                    (0x0274)
#define DBG_RCH4_INT_MSK                 (0x0278)
#define DBG_RCH5_INTS                    (0x027C)
#define DBG_RCH5_INT_MSK                 (0x0280)
#define DBG_RCH6_INTS                    (0x0284)
#define DBG_RCH6_INT_MSK                 (0x0288)
#define DBG_RCH7_INTS                    (0x028C)
#define DBG_RCH7_INT_MSK                 (0x0290)
#define DBG_DPE_GLB_INTS                 (0x0294)
#define DBG_DPE_GLB_INT_MSK              (0x0298)

#define AIF0_CH0_OFFSET                  (0x7000)
#define AIF0_CH0_ADD_OFFSET              (0x7004)

#define MIF_ENABLE                       (0x0000)
#define MIF_MEM_CTRL                     (0x0004)
#define MIF_CTRL0                        (0x0000)
#define MIF_CTRL1                        (0x0004)
#define MIF_CTRL2                        (0x0008)
#define MIF_CTRL3                        (0x000C)
#define MIF_CTRL4                        (0x0010)
#define MIF_CTRL5                        (0x0014)
#define MIF_CTRL_OFFSET                  (0x0020)
#define MIF_CH0_OFFSET                   (DPE_MIF_OFFSET + MIF_CTRL_OFFSET * 1)

#define SMMU_SCR                         (0x0000)
#define SMMU_MEMCTRL                     (0x0004)
#define SMMU_LP_CTRL                     (0x0008)
#define SMMU_INTMASK_NS                  (0x0010)
#define SMMU_INTRAW_NS                   (0x0014)
#define SMMU_INTSTAT_NS                  (0x0018)
#define SMMU_INTCLR_NS                   (0x001C)
#define SMMU_SMRx_NS                     (0x0020)

#define DMA_OFT_X0                       (0x0000)
#define DMA_OFT_Y0                       (0x0004)
#define DMA_OFT_X1                       (0x0008)
#define DMA_OFT_Y1                       (0x000C)
#define DMA_MASK0                        (0x0010)
#define DMA_MASK1                        (0x0014)
#define DMA_STRETCH_SIZE_VRT             (0x0018)
#define DMA_CTRL                         (0x001C)
#define DMA_TILE_SCRAM                   (0x0020)
#define DMA_PULSE                        (0x0028)
#define DMA_CORE_GT                      (0x002C)
#define DMA_DATA_ADDR0                   (0x0060)
#define DMA_STRIDE0                      (0x0064)
#define DMA_STRETCH_STRIDE0              (0x0068)
#define DMA_DATA_NUM0                    (0x006C)
#define DMA_CH_CTL                       (0x00D4)
#define DMA_CH_REG_DEFAULT               (0x0A00)
#define DMA_ALIGN_BYTES                  (128 / BITS_PER_BYTE)
#define DMA_ADDR_ALIGN                   (128 / BITS_PER_BYTE)
#define DMA_STRIDE_ALIGN                 (128 / BITS_PER_BYTE)

#define DFC_DISP_SIZE                    (0x0000)
#define DFC_PIX_IN_NUM                   (0x0004)
#define DFC_GLB_ALPHA                    (0x0008)
#define DFC_DISP_FMT                     (0x000C)
#define DFC_CLIP_CTL_HRZ                 (0x0010)
#define DFC_CLIP_CTL_VRZ                 (0x0014)
#define DFC_CTL_CLIP_EN                  (0x0018)
#define DFC_ICG_MODULE                   (0x001C)
#define DFC_DITHER_ENABLE                (0x0020)
#define DFC_PADDING_CTL                  (0x0024)

#define MCTL_CTL_EN                      (0x0000)
#define MCTL_CTL_MUTEX                   (0x0004)
#define MCTL_CTL_MUTEX_STATUS            (0x0008)
#define MCTL_CTL_MUTEX_ITF               (0x000C)
#define MCTL_CTL_MUTEX_DBUF              (0x0010)
#define MCTL_CTL_MUTEX_SCF               (0x0014)
#define MCTL_CTL_MUTEX_OV                (0x0018)
#define MCTL_CTL_MUTEX_WCH0              (0x0020)
#define MCTL_CTL_MUTEX_RCH0              (0x0030)
#define MCTL_CTL_TOP                     (0x0050)
#define MCTL_CTL_DBG                     (0x00E0)
#define MCTL_RCH0_FLUSH_EN               (0x0100)
#define MCTL_OV0_FLUSH_EN                (0x0128)
#define MCTL_RCH0_OV_OEN                 (0x0160)
#define MCTL_RCH_OV0_SEL                 (0x0180)

#define OVL_SIZE                         (0x0000)
#define OVL_BG_COLOR                     (0x0004)
#define OVL_DST_STARTPOS                 (0x0008)
#define OVL_DST_ENDPOS                   (0x000C)
#define OVL_GCFG                         (0x0010)
#define OVL_LAYER0_POS                   (0x0014)
#define OVL_LAYER0_SIZE                  (0x0018)
#define OVL_LAYER0_ALPHA                 (0x0030)
#define OVL_LAYER0_CFG                   (0x0034)
#define OVL6_REG_DEFAULT                 (0x01A8)

#define DBUF_FRM_SIZE                    (0x0000)
#define DBUF_FRM_HSIZE                   (0x0004)
#define DBUF_SRAM_VALID_NUM              (0x0008)
#define DBUF_WBE_EN                      (0x000C)
#define DBUF_THD_FILL_LEV0               (0x0010)
#define DBUF_DFS_FILL_LEV1               (0x0014)
#define DBUF_THD_RQOS                    (0x0018)
#define DBUF_THD_WQOS                    (0x001C)
#define DBUF_THD_CG                      (0x0020)
#define DBUF_THD_OTHER                   (0x0024)
#define DBUF_ONLINE_FILL_LEVEL           (0x003C)
#define DBUF_WB_FILL_LEVEL               (0x0040)
#define DBUF_DFS_STATUS                  (0x0044)
#define DBUF_THD_FLUX_REQ_BEF            (0x0048)
#define DBUF_DFS_LP_CTRL                 (0x004C)
#define DBUF_RD_SHADOW_SEL               (0x0050)
#define DBUF_MEM_CTRL                    (0x0054)
#define DBUF_THD_FLUX_REQ_AFT            (0x0064)
#define DBUF_THD_DFS_OK                  (0x0068)
#define DBUF_FLUX_REQ_CTRL               (0x006C)
#define DBUF_REG_DEFAULT                 (0x00A4)

#define DPP_IMG_SIZE_BEF_SR              (0x000C)
#define DPP_IMG_SIZE_AFT_SR              (0x0010)
#define DPP_INTS                         (0x0040)
#define DPP_INT_MSK                      (0x0044)

#define SCF_COEF_MEM_CTRL                (0x0018)
#define IFBC_MEM_CTRL                    (0x001C)
#define DITHER_MEM_CTRL                  (0x002C)
#define DSC_MEM_CTRL                     (0x0084)
#define ARSR2P_LB_MEM_CTRL               (0x0084)
#define SCF_LB_MEM_CTRL                  (0x0090)
#define ROT_MEM_CTRL                     (0x0538)
#define VPP_MEM_CTRL                     (0x0704)
#define CMD_MEM_CTRL                     (0x073C)
#define DMA_BUF_MEM_CTRL                 (0x0854)
#define AFBCD_MEM_CTRL                   (0x093C)
#define AFBCE_MEM_CTRL                   (0x0924)

#define LDI_DPI0_HRZ_CTRL0               (0x0000)
#define LDI_DPI0_HRZ_CTRL1               (0x0004)
#define LDI_DPI0_HRZ_CTRL2               (0x0008)
#define LDI_VRT_CTRL0                    (0x000C)
#define LDI_VRT_CTRL1                    (0x0010)
#define LDI_VRT_CTRL2                    (0x0014)
#define LDI_PLR_CTRL                     (0x0018)
#define LDI_CTRL                         (0x0024)
#define LDI_WORK_MODE                    (0x0028)
#define LDI_DSI_CMD_MOD_CTRL             (0x0030)
#define LDI_VINACT_MSK_LEN               (0x0050)
#define LDI_CMD_EVENT_SEL                (0x0060)
#define LDI_MEM_CTRL                     (0x0100)
#define LDI_PXL0_DIV2_GT_EN              (0x0210)
#define LDI_PXL0_DIV4_GT_EN              (0x0214)
#define LDI_PXL0_GT_EN                   (0x0218)
#define LDI_PXL0_DSI_GT_EN               (0x021C)
#define LDI_PXL0_DIVXCFG                 (0x0220)
#define LDI_VESA_CLK_SEL                 (0x0228)
#define LDI_CPU_ITF_INTS                 (0x0248)
#define LDI_CPU_ITF_INT_MSK              (0x024C)

#define MIPIDSI_VERSION_OFFSET           (0x0000)
#define MIPIDSI_PWR_UP_OFFSET            (0x0004)
#define MIPIDSI_CLKMGR_CFG_OFFSET        (0x0008)
#define MIPIDSI_DPI_VCID_OFFSET          (0x000c)
#define MIPIDSI_DPI_COLOR_CODING_OFFSET  (0x0010)
#define MIPIDSI_DPI_CFG_POL_OFFSET       (0x0014)
#define MIPIDSI_DPI_LP_CMD_TIM_OFFSET    (0x0018)
#define MIPIDSI_PCKHDL_CFG_OFFSET        (0x002c)
#define MIPIDSI_GEN_VCID_OFFSET          (0x0030)
#define MIPIDSI_MODE_CFG_OFFSET          (0x0034)
#define MIPIDSI_VID_MODE_CFG_OFFSET      (0x0038)
#define MIPIDSI_VID_PKT_SIZE_OFFSET      (0x003c)
#define MIPIDSI_VID_NUM_CHUNKS_OFFSET    (0x0040)
#define MIPIDSI_VID_NULL_SIZE_OFFSET     (0x0044)
#define MIPIDSI_VID_HSA_TIME_OFFSET      (0x0048)
#define MIPIDSI_VID_HBP_TIME_OFFSET      (0x004c)
#define MIPIDSI_VID_HLINE_TIME_OFFSET    (0x0050)
#define MIPIDSI_VID_VSA_LINES_OFFSET     (0x0054)
#define MIPIDSI_VID_VBP_LINES_OFFSET     (0x0058)
#define MIPIDSI_VID_VFP_LINES_OFFSET     (0x005c)
#define MIPIDSI_VID_VACTIVE_LINES_OFFSET (0x0060)
#define MIPIDSI_EDPI_CMD_SIZE_OFFSET     (0x0064)
#define MIPIDSI_CMD_MODE_CFG_OFFSET      (0x0068)
#define MIPIDSI_GEN_HDR_OFFSET           (0x006c)
#define MIPIDSI_GEN_PLD_DATA_OFFSET      (0x0070)
#define MIPIDSI_CMD_PKT_STATUS_OFFSET    (0x0074)
#define MIPIDSI_TO_CNT_CFG_OFFSET        (0x0078)
#define MIPIDSI_BTA_TO_CNT_OFFSET        (0x008C)
#define MIPIDSI_SDF_3D_OFFSET            (0x0090)
#define MIPIDSI_LPCLK_CTRL_OFFSET        (0x0094)
#define MIPIDSI_PHY_TMR_LPCLK_CFG_OFFSET (0x0098)
#define MIPIDSI_PHY_TMR_CFG_OFFSET       (0x009c)
#define MIPIDSI_PHY_RSTZ_OFFSET          (0x00a0)
#define MIPIDSI_PHY_IF_CFG_OFFSET        (0x00a4)
#define MIPIDSI_PHY_ULPS_CTRL_OFFSET     (0x00a8)
#define MIPIDSI_PHY_TX_TRIGGERS_OFFSET   (0x00ac)
#define MIPIDSI_PHY_STATUS_OFFSET        (0x00b0)
#define MIPIDSI_PHY_TST_CTRL0_OFFSET     (0x00b4)
#define MIPIDSI_PHY_TST_CTRL1_OFFSET     (0x00b8)
#define MIPIDSI_PHY_TMR_RD_CFG_OFFSET    (0x00f4)

enum XRES_DIV {
	XRES_DIV_1 = 1,
	XRES_DIV_2,
};

enum YRES_DIV {
	YRES_DIV_1 = 1,
	YRES_DIV_2,
};

enum PXL0_DIVCFG {
	PXL0_DIVCFG_0 = 0,
	PXL0_DIVCFG_1,
};

enum PXL0_DIV2_GT_EN {
	PXL0_DIV2_GT_EN_CLOSE = 0,
	PXL0_DIV2_GT_EN_OPEN,
};

enum PXL0_DIV4_GT_EN {
	PXL0_DIV4_GT_EN_CLOSE = 0,
	PXL0_DIV4_GT_EN_OPEN,
};

enum PXL0_DSI_GT_EN {
	PXL0_DSI_GT_EN_0 = 0,
	PXL0_DSI_GT_EN_1,
};

enum lcd_format {
	LCD_RGB888 = 0,
	LCD_RGB101010,
	LCD_RGB565,
};

enum lcd_rgb_order {
	LCD_RGB = 0,
	LCD_BGR,
};

enum dpe_dfc_format {
	DFC_PIXEL_FORMAT_RGB_565 = 0,
	DFC_PIXEL_FORMAT_XRGB_4444,
	DFC_PIXEL_FORMAT_ARGB_4444,
	DFC_PIXEL_FORMAT_XRGB_5551,
	DFC_PIXEL_FORMAT_ARGB_5551,
	DFC_PIXEL_FORMAT_XRGB_8888,
	DFC_PIXEL_FORMAT_ARGB_8888,
	DFC_PIXEL_FORMAT_BGR_565,
	DFC_PIXEL_FORMAT_XBGR_4444,
	DFC_PIXEL_FORMAT_ABGR_4444,
	DFC_PIXEL_FORMAT_XBGR_5551,
	DFC_PIXEL_FORMAT_ABGR_5551,
	DFC_PIXEL_FORMAT_XBGR_8888,
	DFC_PIXEL_FORMAT_ABGR_8888,
	DFC_PIXEL_FORMAT_YUV444,
	DFC_PIXEL_FORMAT_YVU444,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_VYUY422,
	DFC_PIXEL_FORMAT_UYVY422,
};

enum dpe_dma_format {
	DMA_PIXEL_FORMAT_RGB_565 = 0,
	DMA_PIXEL_FORMAT_ARGB_4444,
	DMA_PIXEL_FORMAT_XRGB_4444,
	DMA_PIXEL_FORMAT_ARGB_5551,
	DMA_PIXEL_FORMAT_XRGB_5551,
	DMA_PIXEL_FORMAT_ARGB_8888,
	DMA_PIXEL_FORMAT_XRGB_8888,
	DMA_PIXEL_FORMAT_RESERVED0,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
	DMA_PIXEL_FORMAT_YUV_420_SP_HP,
	DMA_PIXEL_FORMAT_YUV_420_P_HP,
	DMA_PIXEL_FORMAT_YUV_422_SP_HP,
	DMA_PIXEL_FORMAT_YUV_422_P_HP,
	DMA_PIXEL_FORMAT_AYUV_4444,
};

enum dpe_fb_format {
	DPE_RGB_565 = 0,
	DPE_RGBX_4444,
	DPE_RGBA_4444,
	DPE_RGBX_5551,
	DPE_RGBA_5551,
	DPE_RGBX_8888,
	DPE_RGBA_8888,
	DPE_BGR_565,
	DPE_BGRX_4444,
	DPE_BGRA_4444,
	DPE_BGRX_5551,
	DPE_BGRA_5551,
	DPE_BGRX_8888,
	DPE_BGRA_8888,
	DPE_YUV_422_I,
	/* YUV Semi-planar */
	DPE_YCbCr_422_SP,
	DPE_YCrCb_422_SP,
	DPE_YCbCr_420_SP,
	DPE_YCrCb_420_SP,
	/* YUV Planar */
	DPE_YCbCr_422_P,
	DPE_YCrCb_422_P,
	DPE_YCbCr_420_P,
	DPE_YCrCb_420_P,
	/* YUV Package */
	DPE_YUYV_422_Pkg,
	DPE_UYVY_422_Pkg,
	DPE_YVYU_422_Pkg,
	DPE_VYUY_422_Pkg,
};

#endif
