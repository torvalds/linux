/*
 * Copyright (C) 2008 Google, Inc.
 * Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/dma-mapping.h>
#include <mach/irqs.h>
#include <mach/msm_iomap.h>
#include <mach/dma.h>
#include <mach/board.h>

#include "devices.h"
#include "smd_private.h"

#include <asm/mach/flash.h>

#include "clock-pcom.h"

#include <mach/mmc.h>

static struct resource resources_uart2[] = {
	{
		.start	= INT_UART2,
		.end	= INT_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART2_PHYS,
		.end	= MSM_UART2_PHYS + MSM_UART2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device msm_device_uart2 = {
	.name	= "msm_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};

struct platform_device msm_device_smd = {
	.name   = "msm_smd",
	.id     = -1,
};

static struct resource resources_otg[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_otg = {
	.name		= "msm_otg",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_otg),
	.resource	= resources_otg,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

static struct resource resources_hsusb[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_hsusb = {
	.name		= "msm_hsusb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_hsusb),
	.resource	= resources_hsusb,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

static u64 dma_mask = 0xffffffffULL;
static struct resource resources_hsusb_host[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_hsusb_host = {
	.name		= "msm_hsusb_host",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_hsusb_host),
	.resource	= resources_hsusb_host,
	.dev		= {
		.dma_mask               = &dma_mask,
		.coherent_dma_mask      = 0xffffffffULL,
	},
};

struct clk msm_clocks_7x30[] = {
	CLK_PCOM("adm_clk",	ADM_CLK,	NULL, 0),
	CLK_PCOM("adsp_clk",	ADSP_CLK,	NULL, 0),
	CLK_PCOM("cam_m_clk",	CAM_M_CLK,	NULL, 0),
	CLK_PCOM("camif_pad_pclk",	CAMIF_PAD_P_CLK,	NULL, OFF),
	CLK_PCOM("ebi1_clk",	EBI1_CLK,	NULL, CLK_MIN),
	CLK_PCOM("ecodec_clk",	ECODEC_CLK,	NULL, 0),
	CLK_PCOM("emdh_clk",	EMDH_CLK,	NULL, OFF | CLK_MINMAX),
	CLK_PCOM("emdh_pclk",	EMDH_P_CLK,	NULL, OFF),
	CLK_PCOM("gp_clk",	GP_CLK,		NULL, 0),
	CLK_PCOM("grp_2d_clk",	GRP_2D_CLK,	NULL, 0),
	CLK_PCOM("grp_2d_pclk",	GRP_2D_P_CLK,	NULL, 0),
	CLK_PCOM("grp_clk",	GRP_3D_CLK,	NULL, 0),
	CLK_PCOM("grp_pclk",	GRP_3D_P_CLK,	NULL, 0),
	CLK_7X30S("grp_src_clk", GRP_3D_SRC_CLK, GRP_3D_CLK,	NULL, 0),
	CLK_PCOM("hdmi_clk",	HDMI_CLK,	NULL, 0),
	CLK_PCOM("imem_clk",	IMEM_CLK,	NULL, OFF),
	CLK_PCOM("jpeg_clk",	JPEG_CLK,	NULL, OFF),
	CLK_PCOM("jpeg_pclk",	JPEG_P_CLK,	NULL, OFF),
	CLK_PCOM("lpa_codec_clk",	LPA_CODEC_CLK,		NULL, 0),
	CLK_PCOM("lpa_core_clk",	LPA_CORE_CLK,		NULL, 0),
	CLK_PCOM("lpa_pclk",		LPA_P_CLK,		NULL, 0),
	CLK_PCOM("mdc_clk",	MDC_CLK,	NULL, 0),
	CLK_PCOM("mddi_clk",	PMDH_CLK,	NULL, OFF | CLK_MINMAX),
	CLK_PCOM("mddi_pclk",	PMDH_P_CLK,	NULL, 0),
	CLK_PCOM("mdp_clk",	MDP_CLK,	NULL, OFF),
	CLK_PCOM("mdp_pclk",	MDP_P_CLK,	NULL, 0),
	CLK_PCOM("mdp_lcdc_pclk_clk", MDP_LCDC_PCLK_CLK, NULL, 0),
	CLK_PCOM("mdp_lcdc_pad_pclk_clk", MDP_LCDC_PAD_PCLK_CLK, NULL, 0),
	CLK_PCOM("mdp_vsync_clk",	MDP_VSYNC_CLK,  NULL, 0),
	CLK_PCOM("mfc_clk",		MFC_CLK,		NULL, 0),
	CLK_PCOM("mfc_div2_clk",	MFC_DIV2_CLK,		NULL, 0),
	CLK_PCOM("mfc_pclk",		MFC_P_CLK,		NULL, 0),
	CLK_PCOM("mi2s_m_clk",		MI2S_M_CLK,  		NULL, 0),
	CLK_PCOM("mi2s_s_clk",		MI2S_S_CLK,  		NULL, 0),
	CLK_PCOM("mi2s_codec_rx_m_clk",	MI2S_CODEC_RX_M_CLK,  NULL, 0),
	CLK_PCOM("mi2s_codec_rx_s_clk",	MI2S_CODEC_RX_S_CLK,  NULL, 0),
	CLK_PCOM("mi2s_codec_tx_m_clk",	MI2S_CODEC_TX_M_CLK,  NULL, 0),
	CLK_PCOM("mi2s_codec_tx_s_clk",	MI2S_CODEC_TX_S_CLK,  NULL, 0),
	CLK_PCOM("pbus_clk",	PBUS_CLK,	NULL, CLK_MIN),
	CLK_PCOM("pcm_clk",	PCM_CLK,	NULL, 0),
	CLK_PCOM("rotator_clk",	AXI_ROTATOR_CLK,		NULL, 0),
	CLK_PCOM("rotator_imem_clk",	ROTATOR_IMEM_CLK,	NULL, OFF),
	CLK_PCOM("rotator_pclk",	ROTATOR_P_CLK,		NULL, OFF),
	CLK_PCOM("sdac_clk",	SDAC_CLK,	NULL, OFF),
	CLK_PCOM("spi_clk",	SPI_CLK,	NULL, 0),
	CLK_PCOM("spi_pclk",	SPI_P_CLK,	NULL, 0),
	CLK_7X30S("tv_src_clk",	TV_CLK, 	TV_ENC_CLK,	NULL, 0),
	CLK_PCOM("tv_dac_clk",	TV_DAC_CLK,	NULL, 0),
	CLK_PCOM("tv_enc_clk",	TV_ENC_CLK,	NULL, 0),
	CLK_PCOM("uart_clk",	UART2_CLK,	&msm_device_uart2.dev, 0),
	CLK_PCOM("usb_phy_clk",	USB_PHY_CLK,	NULL, 0),
	CLK_PCOM("usb_hs_clk",		USB_HS_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs_pclk",		USB_HS_P_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs_core_clk",	USB_HS_CORE_CLK,	NULL, OFF),
	CLK_PCOM("usb_hs2_clk",		USB_HS2_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs2_pclk",	USB_HS2_P_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs2_core_clk",	USB_HS2_CORE_CLK,	NULL, OFF),
	CLK_PCOM("usb_hs3_clk",		USB_HS3_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs3_pclk",	USB_HS3_P_CLK,		NULL, OFF),
	CLK_PCOM("usb_hs3_core_clk",	USB_HS3_CORE_CLK,	NULL, OFF),
	CLK_PCOM("vdc_clk",	VDC_CLK,	NULL, OFF | CLK_MIN),
	CLK_PCOM("vfe_camif_clk",	VFE_CAMIF_CLK, 	NULL, 0),
	CLK_PCOM("vfe_clk",	VFE_CLK,	NULL, 0),
	CLK_PCOM("vfe_mdc_clk",	VFE_MDC_CLK,	NULL, 0),
	CLK_PCOM("vfe_pclk",	VFE_P_CLK,	NULL, OFF),
	CLK_PCOM("vpe_clk",	VPE_CLK,	NULL, 0),

	/* 7x30 v2 hardware only. */
	CLK_PCOM("csi_clk",	CSI0_CLK,	NULL, 0),
	CLK_PCOM("csi_pclk",	CSI0_P_CLK,	NULL, 0),
	CLK_PCOM("csi_vfe_clk",	CSI0_VFE_CLK,	NULL, 0),
};

unsigned msm_num_clocks_7x30 = ARRAY_SIZE(msm_clocks_7x30);

