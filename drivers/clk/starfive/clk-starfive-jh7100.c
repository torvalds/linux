// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7100 Clock Generator Driver
 *
 * Copyright 2021 Ahmad Fatoum, Pengutronix
 * Copyright (C) 2021 Glider bv
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/starfive-jh7100.h>

/* external clocks */
#define JH7100_CLK_OSC_SYS		(JH7100_CLK_END + 0)
#define JH7100_CLK_OSC_AUD		(JH7100_CLK_END + 1)
#define JH7100_CLK_GMAC_RMII_REF	(JH7100_CLK_END + 2)
#define JH7100_CLK_GMAC_GR_MII_RX	(JH7100_CLK_END + 3)

/* register fields */
#define JH7100_CLK_ENABLE	BIT(31)
#define JH7100_CLK_INVERT	BIT(30)
#define JH7100_CLK_MUX_MASK	GENMASK(27, 24)
#define JH7100_CLK_MUX_SHIFT	24
#define JH7100_CLK_DIV_MASK	GENMASK(23, 0)
#define JH7100_CLK_FRAC_MASK	GENMASK(15, 8)
#define JH7100_CLK_FRAC_SHIFT	8
#define JH7100_CLK_INT_MASK	GENMASK(7, 0)

/* fractional divider min/max */
#define JH7100_CLK_FRAC_MIN	100UL
#define JH7100_CLK_FRAC_MAX	25599UL

/* clock data */
#define JH7100_GATE(_idx, _name, _flags, _parent) [_idx] = {		\
	.name = _name,							\
	.flags = CLK_SET_RATE_PARENT | (_flags),			\
	.max = JH7100_CLK_ENABLE,					\
	.parents = { [0] = _parent },					\
}

#define JH7100__DIV(_idx, _name, _max, _parent) [_idx] = {		\
	.name = _name,							\
	.flags = 0,							\
	.max = _max,							\
	.parents = { [0] = _parent },					\
}

#define JH7100_GDIV(_idx, _name, _flags, _max, _parent) [_idx] = {	\
	.name = _name,							\
	.flags = _flags,						\
	.max = JH7100_CLK_ENABLE | (_max),				\
	.parents = { [0] = _parent },					\
}

#define JH7100_FDIV(_idx, _name, _parent) [_idx] = {			\
	.name = _name,							\
	.flags = 0,							\
	.max = JH7100_CLK_FRAC_MAX,					\
	.parents = { [0] = _parent },					\
}

#define JH7100__MUX(_idx, _name, _nparents, ...) [_idx] = {		\
	.name = _name,							\
	.flags = 0,							\
	.max = ((_nparents) - 1) << JH7100_CLK_MUX_SHIFT,		\
	.parents = { __VA_ARGS__ },					\
}

#define JH7100_GMUX(_idx, _name, _flags, _nparents, ...) [_idx] = {	\
	.name = _name,							\
	.flags = _flags,						\
	.max = JH7100_CLK_ENABLE |					\
		(((_nparents) - 1) << JH7100_CLK_MUX_SHIFT),		\
	.parents = { __VA_ARGS__ },					\
}

#define JH7100__INV(_idx, _name, _parent) [_idx] = {			\
	.name = _name,							\
	.flags = CLK_SET_RATE_PARENT,					\
	.max = JH7100_CLK_INVERT,					\
	.parents = { [0] = _parent },					\
}

static const struct {
	const char *name;
	unsigned long flags;
	u32 max;
	u8 parents[4];
} jh7100_clk_data[] __initconst = {
	JH7100__MUX(JH7100_CLK_CPUNDBUS_ROOT, "cpundbus_root", 4,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL1_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_DLA_ROOT, "dla_root", 3,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL1_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_DSP_ROOT, "dsp_root", 4,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL1_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_GMACUSB_ROOT, "gmacusb_root", 3,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_PERH0_ROOT, "perh0_root", 2,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL0_OUT),
	JH7100__MUX(JH7100_CLK_PERH1_ROOT, "perh1_root", 2,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_VIN_ROOT, "vin_root", 3,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL1_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_VOUT_ROOT, "vout_root", 3,
		    JH7100_CLK_OSC_AUD,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100_GDIV(JH7100_CLK_AUDIO_ROOT, "audio_root", 0, 8, JH7100_CLK_PLL0_OUT),
	JH7100__MUX(JH7100_CLK_CDECHIFI4_ROOT, "cdechifi4_root", 3,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL1_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__MUX(JH7100_CLK_CDEC_ROOT, "cdec_root", 3,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL1_OUT),
	JH7100__MUX(JH7100_CLK_VOUTBUS_ROOT, "voutbus_root", 3,
		    JH7100_CLK_OSC_AUD,
		    JH7100_CLK_PLL0_OUT,
		    JH7100_CLK_PLL2_OUT),
	JH7100__DIV(JH7100_CLK_CPUNBUS_ROOT_DIV, "cpunbus_root_div", 2, JH7100_CLK_CPUNDBUS_ROOT),
	JH7100__DIV(JH7100_CLK_DSP_ROOT_DIV, "dsp_root_div", 4, JH7100_CLK_DSP_ROOT),
	JH7100__DIV(JH7100_CLK_PERH0_SRC, "perh0_src", 4, JH7100_CLK_PERH0_ROOT),
	JH7100__DIV(JH7100_CLK_PERH1_SRC, "perh1_src", 4, JH7100_CLK_PERH1_ROOT),
	JH7100_GDIV(JH7100_CLK_PLL0_TESTOUT, "pll0_testout", 0, 31, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_PLL1_TESTOUT, "pll1_testout", 0, 31, JH7100_CLK_DLA_ROOT),
	JH7100_GDIV(JH7100_CLK_PLL2_TESTOUT, "pll2_testout", 0, 31, JH7100_CLK_PERH1_SRC),
	JH7100__MUX(JH7100_CLK_PLL2_REF, "pll2_refclk", 2,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_OSC_AUD),
	JH7100__DIV(JH7100_CLK_CPU_CORE, "cpu_core", 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100__DIV(JH7100_CLK_CPU_AXI, "cpu_axi", 8, JH7100_CLK_CPU_CORE),
	JH7100__DIV(JH7100_CLK_AHB_BUS, "ahb_bus", 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100__DIV(JH7100_CLK_APB1_BUS, "apb1_bus", 8, JH7100_CLK_AHB_BUS),
	JH7100__DIV(JH7100_CLK_APB2_BUS, "apb2_bus", 8, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_DOM3AHB_BUS, "dom3ahb_bus", CLK_IS_CRITICAL, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_DOM7AHB_BUS, "dom7ahb_bus", CLK_IS_CRITICAL, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_U74_CORE0, "u74_core0", CLK_IS_CRITICAL, JH7100_CLK_CPU_CORE),
	JH7100_GDIV(JH7100_CLK_U74_CORE1, "u74_core1", CLK_IS_CRITICAL, 8, JH7100_CLK_CPU_CORE),
	JH7100_GATE(JH7100_CLK_U74_AXI, "u74_axi", CLK_IS_CRITICAL, JH7100_CLK_CPU_AXI),
	JH7100_GATE(JH7100_CLK_U74RTC_TOGGLE, "u74rtc_toggle", CLK_IS_CRITICAL, JH7100_CLK_OSC_SYS),
	JH7100_GATE(JH7100_CLK_SGDMA2P_AXI, "sgdma2p_axi", 0, JH7100_CLK_CPU_AXI),
	JH7100_GATE(JH7100_CLK_DMA2PNOC_AXI, "dma2pnoc_axi", 0, JH7100_CLK_CPU_AXI),
	JH7100_GATE(JH7100_CLK_SGDMA2P_AHB, "sgdma2p_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100__DIV(JH7100_CLK_DLA_BUS, "dla_bus", 4, JH7100_CLK_DLA_ROOT),
	JH7100_GATE(JH7100_CLK_DLA_AXI, "dla_axi", 0, JH7100_CLK_DLA_BUS),
	JH7100_GATE(JH7100_CLK_DLANOC_AXI, "dlanoc_axi", 0, JH7100_CLK_DLA_BUS),
	JH7100_GATE(JH7100_CLK_DLA_APB, "dla_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_VP6_CORE, "vp6_core", 0, 4, JH7100_CLK_DSP_ROOT_DIV),
	JH7100__DIV(JH7100_CLK_VP6BUS_SRC, "vp6bus_src", 4, JH7100_CLK_DSP_ROOT),
	JH7100_GDIV(JH7100_CLK_VP6_AXI, "vp6_axi", 0, 4, JH7100_CLK_VP6BUS_SRC),
	JH7100__DIV(JH7100_CLK_VCDECBUS_SRC, "vcdecbus_src", 4, JH7100_CLK_CDECHIFI4_ROOT),
	JH7100__DIV(JH7100_CLK_VDEC_BUS, "vdec_bus", 8, JH7100_CLK_VCDECBUS_SRC),
	JH7100_GATE(JH7100_CLK_VDEC_AXI, "vdec_axi", 0, JH7100_CLK_VDEC_BUS),
	JH7100_GATE(JH7100_CLK_VDECBRG_MAIN, "vdecbrg_mainclk", 0, JH7100_CLK_VDEC_BUS),
	JH7100_GDIV(JH7100_CLK_VDEC_BCLK, "vdec_bclk", 0, 8, JH7100_CLK_VCDECBUS_SRC),
	JH7100_GDIV(JH7100_CLK_VDEC_CCLK, "vdec_cclk", 0, 8, JH7100_CLK_CDEC_ROOT),
	JH7100_GATE(JH7100_CLK_VDEC_APB, "vdec_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_JPEG_AXI, "jpeg_axi", 0, 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100_GDIV(JH7100_CLK_JPEG_CCLK, "jpeg_cclk", 0, 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100_GATE(JH7100_CLK_JPEG_APB, "jpeg_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_GC300_2X, "gc300_2x", 0, 8, JH7100_CLK_CDECHIFI4_ROOT),
	JH7100_GATE(JH7100_CLK_GC300_AHB, "gc300_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100__DIV(JH7100_CLK_JPCGC300_AXIBUS, "jpcgc300_axibus", 8, JH7100_CLK_VCDECBUS_SRC),
	JH7100_GATE(JH7100_CLK_GC300_AXI, "gc300_axi", 0, JH7100_CLK_JPCGC300_AXIBUS),
	JH7100_GATE(JH7100_CLK_JPCGC300_MAIN, "jpcgc300_mainclk", 0, JH7100_CLK_JPCGC300_AXIBUS),
	JH7100__DIV(JH7100_CLK_VENC_BUS, "venc_bus", 8, JH7100_CLK_VCDECBUS_SRC),
	JH7100_GATE(JH7100_CLK_VENC_AXI, "venc_axi", 0, JH7100_CLK_VENC_BUS),
	JH7100_GATE(JH7100_CLK_VENCBRG_MAIN, "vencbrg_mainclk", 0, JH7100_CLK_VENC_BUS),
	JH7100_GDIV(JH7100_CLK_VENC_BCLK, "venc_bclk", 0, 8, JH7100_CLK_VCDECBUS_SRC),
	JH7100_GDIV(JH7100_CLK_VENC_CCLK, "venc_cclk", 0, 8, JH7100_CLK_CDEC_ROOT),
	JH7100_GATE(JH7100_CLK_VENC_APB, "venc_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_DDRPLL_DIV2, "ddrpll_div2", CLK_IS_CRITICAL, 2, JH7100_CLK_PLL1_OUT),
	JH7100_GDIV(JH7100_CLK_DDRPLL_DIV4, "ddrpll_div4", CLK_IS_CRITICAL, 2, JH7100_CLK_DDRPLL_DIV2),
	JH7100_GDIV(JH7100_CLK_DDRPLL_DIV8, "ddrpll_div8", CLK_IS_CRITICAL, 2, JH7100_CLK_DDRPLL_DIV4),
	JH7100_GDIV(JH7100_CLK_DDROSC_DIV2, "ddrosc_div2", CLK_IS_CRITICAL, 2, JH7100_CLK_OSC_SYS),
	JH7100_GMUX(JH7100_CLK_DDRC0, "ddrc0", CLK_IS_CRITICAL, 4,
		    JH7100_CLK_DDROSC_DIV2,
		    JH7100_CLK_DDRPLL_DIV2,
		    JH7100_CLK_DDRPLL_DIV4,
		    JH7100_CLK_DDRPLL_DIV8),
	JH7100_GMUX(JH7100_CLK_DDRC1, "ddrc1", CLK_IS_CRITICAL, 4,
		    JH7100_CLK_DDROSC_DIV2,
		    JH7100_CLK_DDRPLL_DIV2,
		    JH7100_CLK_DDRPLL_DIV4,
		    JH7100_CLK_DDRPLL_DIV8),
	JH7100_GATE(JH7100_CLK_DDRPHY_APB, "ddrphy_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100__DIV(JH7100_CLK_NOC_ROB, "noc_rob", 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100__DIV(JH7100_CLK_NOC_COG, "noc_cog", 8, JH7100_CLK_DLA_ROOT),
	JH7100_GATE(JH7100_CLK_NNE_AHB, "nne_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100__DIV(JH7100_CLK_NNEBUS_SRC1, "nnebus_src1", 4, JH7100_CLK_DSP_ROOT),
	JH7100__MUX(JH7100_CLK_NNE_BUS, "nne_bus", 2,
		    JH7100_CLK_CPU_AXI,
		    JH7100_CLK_NNEBUS_SRC1),
	JH7100_GATE(JH7100_CLK_NNE_AXI, "nne_axi", 0, JH7100_CLK_NNE_BUS),
	JH7100_GATE(JH7100_CLK_NNENOC_AXI, "nnenoc_axi", 0, JH7100_CLK_NNE_BUS),
	JH7100_GATE(JH7100_CLK_DLASLV_AXI, "dlaslv_axi", 0, JH7100_CLK_NNE_BUS),
	JH7100_GATE(JH7100_CLK_DSPX2C_AXI, "dspx2c_axi", CLK_IS_CRITICAL, JH7100_CLK_NNE_BUS),
	JH7100__DIV(JH7100_CLK_HIFI4_SRC, "hifi4_src", 4, JH7100_CLK_CDECHIFI4_ROOT),
	JH7100__DIV(JH7100_CLK_HIFI4_COREFREE, "hifi4_corefree", 8, JH7100_CLK_HIFI4_SRC),
	JH7100_GATE(JH7100_CLK_HIFI4_CORE, "hifi4_core", 0, JH7100_CLK_HIFI4_COREFREE),
	JH7100__DIV(JH7100_CLK_HIFI4_BUS, "hifi4_bus", 8, JH7100_CLK_HIFI4_COREFREE),
	JH7100_GATE(JH7100_CLK_HIFI4_AXI, "hifi4_axi", 0, JH7100_CLK_HIFI4_BUS),
	JH7100_GATE(JH7100_CLK_HIFI4NOC_AXI, "hifi4noc_axi", 0, JH7100_CLK_HIFI4_BUS),
	JH7100__DIV(JH7100_CLK_SGDMA1P_BUS, "sgdma1p_bus", 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100_GATE(JH7100_CLK_SGDMA1P_AXI, "sgdma1p_axi", 0, JH7100_CLK_SGDMA1P_BUS),
	JH7100_GATE(JH7100_CLK_DMA1P_AXI, "dma1p_axi", 0, JH7100_CLK_SGDMA1P_BUS),
	JH7100_GDIV(JH7100_CLK_X2C_AXI, "x2c_axi", CLK_IS_CRITICAL, 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100__DIV(JH7100_CLK_USB_BUS, "usb_bus", 8, JH7100_CLK_CPUNBUS_ROOT_DIV),
	JH7100_GATE(JH7100_CLK_USB_AXI, "usb_axi", 0, JH7100_CLK_USB_BUS),
	JH7100_GATE(JH7100_CLK_USBNOC_AXI, "usbnoc_axi", 0, JH7100_CLK_USB_BUS),
	JH7100__DIV(JH7100_CLK_USBPHY_ROOTDIV, "usbphy_rootdiv", 4, JH7100_CLK_GMACUSB_ROOT),
	JH7100_GDIV(JH7100_CLK_USBPHY_125M, "usbphy_125m", 0, 8, JH7100_CLK_USBPHY_ROOTDIV),
	JH7100_GDIV(JH7100_CLK_USBPHY_PLLDIV25M, "usbphy_plldiv25m", 0, 32, JH7100_CLK_USBPHY_ROOTDIV),
	JH7100__MUX(JH7100_CLK_USBPHY_25M, "usbphy_25m", 2,
		    JH7100_CLK_OSC_SYS,
		    JH7100_CLK_USBPHY_PLLDIV25M),
	JH7100_FDIV(JH7100_CLK_AUDIO_DIV, "audio_div", JH7100_CLK_AUDIO_ROOT),
	JH7100_GATE(JH7100_CLK_AUDIO_SRC, "audio_src", 0, JH7100_CLK_AUDIO_DIV),
	JH7100_GATE(JH7100_CLK_AUDIO_12288, "audio_12288", 0, JH7100_CLK_OSC_AUD),
	JH7100_GDIV(JH7100_CLK_VIN_SRC, "vin_src", 0, 4, JH7100_CLK_VIN_ROOT),
	JH7100__DIV(JH7100_CLK_ISP0_BUS, "isp0_bus", 8, JH7100_CLK_VIN_SRC),
	JH7100_GATE(JH7100_CLK_ISP0_AXI, "isp0_axi", 0, JH7100_CLK_ISP0_BUS),
	JH7100_GATE(JH7100_CLK_ISP0NOC_AXI, "isp0noc_axi", 0, JH7100_CLK_ISP0_BUS),
	JH7100_GATE(JH7100_CLK_ISPSLV_AXI, "ispslv_axi", 0, JH7100_CLK_ISP0_BUS),
	JH7100__DIV(JH7100_CLK_ISP1_BUS, "isp1_bus", 8, JH7100_CLK_VIN_SRC),
	JH7100_GATE(JH7100_CLK_ISP1_AXI, "isp1_axi", 0, JH7100_CLK_ISP1_BUS),
	JH7100_GATE(JH7100_CLK_ISP1NOC_AXI, "isp1noc_axi", 0, JH7100_CLK_ISP1_BUS),
	JH7100__DIV(JH7100_CLK_VIN_BUS, "vin_bus", 8, JH7100_CLK_VIN_SRC),
	JH7100_GATE(JH7100_CLK_VIN_AXI, "vin_axi", 0, JH7100_CLK_VIN_BUS),
	JH7100_GATE(JH7100_CLK_VINNOC_AXI, "vinnoc_axi", 0, JH7100_CLK_VIN_BUS),
	JH7100_GDIV(JH7100_CLK_VOUT_SRC, "vout_src", 0, 4, JH7100_CLK_VOUT_ROOT),
	JH7100__DIV(JH7100_CLK_DISPBUS_SRC, "dispbus_src", 4, JH7100_CLK_VOUTBUS_ROOT),
	JH7100__DIV(JH7100_CLK_DISP_BUS, "disp_bus", 4, JH7100_CLK_DISPBUS_SRC),
	JH7100_GATE(JH7100_CLK_DISP_AXI, "disp_axi", 0, JH7100_CLK_DISP_BUS),
	JH7100_GATE(JH7100_CLK_DISPNOC_AXI, "dispnoc_axi", 0, JH7100_CLK_DISP_BUS),
	JH7100_GATE(JH7100_CLK_SDIO0_AHB, "sdio0_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GDIV(JH7100_CLK_SDIO0_CCLKINT, "sdio0_cclkint", 0, 24, JH7100_CLK_PERH0_SRC),
	JH7100__INV(JH7100_CLK_SDIO0_CCLKINT_INV, "sdio0_cclkint_inv", JH7100_CLK_SDIO0_CCLKINT),
	JH7100_GATE(JH7100_CLK_SDIO1_AHB, "sdio1_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GDIV(JH7100_CLK_SDIO1_CCLKINT, "sdio1_cclkint", 0, 24, JH7100_CLK_PERH1_SRC),
	JH7100__INV(JH7100_CLK_SDIO1_CCLKINT_INV, "sdio1_cclkint_inv", JH7100_CLK_SDIO1_CCLKINT),
	JH7100_GATE(JH7100_CLK_GMAC_AHB, "gmac_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100__DIV(JH7100_CLK_GMAC_ROOT_DIV, "gmac_root_div", 8, JH7100_CLK_GMACUSB_ROOT),
	JH7100_GDIV(JH7100_CLK_GMAC_PTP_REF, "gmac_ptp_refclk", 0, 31, JH7100_CLK_GMAC_ROOT_DIV),
	JH7100_GDIV(JH7100_CLK_GMAC_GTX, "gmac_gtxclk", 0, 255, JH7100_CLK_GMAC_ROOT_DIV),
	JH7100_GDIV(JH7100_CLK_GMAC_RMII_TX, "gmac_rmii_txclk", 0, 8, JH7100_CLK_GMAC_RMII_REF),
	JH7100_GDIV(JH7100_CLK_GMAC_RMII_RX, "gmac_rmii_rxclk", 0, 8, JH7100_CLK_GMAC_RMII_REF),
	JH7100__MUX(JH7100_CLK_GMAC_TX, "gmac_tx", 3,
		    JH7100_CLK_GMAC_GTX,
		    JH7100_CLK_GMAC_TX_INV,
		    JH7100_CLK_GMAC_RMII_TX),
	JH7100__INV(JH7100_CLK_GMAC_TX_INV, "gmac_tx_inv", JH7100_CLK_GMAC_TX),
	JH7100__MUX(JH7100_CLK_GMAC_RX_PRE, "gmac_rx_pre", 2,
		    JH7100_CLK_GMAC_GR_MII_RX,
		    JH7100_CLK_GMAC_RMII_RX),
	JH7100__INV(JH7100_CLK_GMAC_RX_INV, "gmac_rx_inv", JH7100_CLK_GMAC_RX_PRE),
	JH7100_GATE(JH7100_CLK_GMAC_RMII, "gmac_rmii", 0, JH7100_CLK_GMAC_RMII_REF),
	JH7100_GDIV(JH7100_CLK_GMAC_TOPHYREF, "gmac_tophyref", 0, 127, JH7100_CLK_GMAC_ROOT_DIV),
	JH7100_GATE(JH7100_CLK_SPI2AHB_AHB, "spi2ahb_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GDIV(JH7100_CLK_SPI2AHB_CORE, "spi2ahb_core", 0, 31, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_EZMASTER_AHB, "ezmaster_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_E24_AHB, "e24_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_E24RTC_TOGGLE, "e24rtc_toggle", 0, JH7100_CLK_OSC_SYS),
	JH7100_GATE(JH7100_CLK_QSPI_AHB, "qspi_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_QSPI_APB, "qspi_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_QSPI_REF, "qspi_refclk", 0, 31, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_SEC_AHB, "sec_ahb", 0, JH7100_CLK_AHB_BUS),
	JH7100_GATE(JH7100_CLK_AES, "aes_clk", 0, JH7100_CLK_SEC_AHB),
	JH7100_GATE(JH7100_CLK_SHA, "sha_clk", 0, JH7100_CLK_SEC_AHB),
	JH7100_GATE(JH7100_CLK_PKA, "pka_clk", 0, JH7100_CLK_SEC_AHB),
	JH7100_GATE(JH7100_CLK_TRNG_APB, "trng_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GATE(JH7100_CLK_OTP_APB, "otp_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GATE(JH7100_CLK_UART0_APB, "uart0_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_UART0_CORE, "uart0_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_UART1_APB, "uart1_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_UART1_CORE, "uart1_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_SPI0_APB, "spi0_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_SPI0_CORE, "spi0_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_SPI1_APB, "spi1_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_SPI1_CORE, "spi1_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_I2C0_APB, "i2c0_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_I2C0_CORE, "i2c0_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_I2C1_APB, "i2c1_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GDIV(JH7100_CLK_I2C1_CORE, "i2c1_core", 0, 63, JH7100_CLK_PERH1_SRC),
	JH7100_GATE(JH7100_CLK_GPIO_APB, "gpio_apb", 0, JH7100_CLK_APB1_BUS),
	JH7100_GATE(JH7100_CLK_UART2_APB, "uart2_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_UART2_CORE, "uart2_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_UART3_APB, "uart3_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_UART3_CORE, "uart3_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_SPI2_APB, "spi2_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_SPI2_CORE, "spi2_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_SPI3_APB, "spi3_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_SPI3_CORE, "spi3_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_I2C2_APB, "i2c2_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_I2C2_CORE, "i2c2_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_I2C3_APB, "i2c3_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_I2C3_CORE, "i2c3_core", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_WDTIMER_APB, "wdtimer_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_WDT_CORE, "wdt_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER0_CORE, "timer0_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER1_CORE, "timer1_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER2_CORE, "timer2_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER3_CORE, "timer3_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER4_CORE, "timer4_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER5_CORE, "timer5_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GDIV(JH7100_CLK_TIMER6_CORE, "timer6_coreclk", 0, 63, JH7100_CLK_PERH0_SRC),
	JH7100_GATE(JH7100_CLK_VP6INTC_APB, "vp6intc_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GATE(JH7100_CLK_PWM_APB, "pwm_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GATE(JH7100_CLK_MSI_APB, "msi_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GATE(JH7100_CLK_TEMP_APB, "temp_apb", 0, JH7100_CLK_APB2_BUS),
	JH7100_GDIV(JH7100_CLK_TEMP_SENSE, "temp_sense", 0, 31, JH7100_CLK_OSC_SYS),
	JH7100_GATE(JH7100_CLK_SYSERR_APB, "syserr_apb", 0, JH7100_CLK_APB2_BUS),
};

struct jh7100_clk {
	struct clk_hw hw;
	unsigned int idx;
	unsigned int max_div;
};

struct jh7100_clk_priv {
	/* protect clk enable and set rate/parent from happening at the same time */
	spinlock_t rmw_lock;
	struct device *dev;
	void __iomem *base;
	struct clk_hw *pll[3];
	struct jh7100_clk reg[JH7100_CLK_PLL0_OUT];
};

static struct jh7100_clk *jh7100_clk_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh7100_clk, hw);
}

static struct jh7100_clk_priv *jh7100_priv_from(struct jh7100_clk *clk)
{
	return container_of(clk, struct jh7100_clk_priv, reg[clk->idx]);
}

static u32 jh7100_clk_reg_get(struct jh7100_clk *clk)
{
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;

	return readl_relaxed(reg);
}

static void jh7100_clk_reg_rmw(struct jh7100_clk *clk, u32 mask, u32 value)
{
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;
	unsigned long flags;

	spin_lock_irqsave(&priv->rmw_lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int jh7100_clk_enable(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_ENABLE, JH7100_CLK_ENABLE);
	return 0;
}

static void jh7100_clk_disable(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_ENABLE, 0);
}

static int jh7100_clk_is_enabled(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	return !!(jh7100_clk_reg_get(clk) & JH7100_CLK_ENABLE);
}

static unsigned long jh7100_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 div = jh7100_clk_reg_get(clk) & JH7100_CLK_DIV_MASK;

	return div ? parent_rate / div : 0;
}

static int jh7100_clk_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long parent = req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div = min_t(unsigned long, DIV_ROUND_UP(parent, rate), clk->max_div);
	unsigned long result = parent / div;

	/*
	 * we want the result clamped by min_rate and max_rate if possible:
	 * case 1: div hits the max divider value, which means it's less than
	 * parent / rate, so the result is greater than rate and min_rate in
	 * particular. we can't do anything about result > max_rate because the
	 * divider doesn't go any further.
	 * case 2: div = DIV_ROUND_UP(parent, rate) which means the result is
	 * always lower or equal to rate and max_rate. however the result may
	 * turn out lower than min_rate, but then the next higher rate is fine:
	 *   div - 1 = ceil(parent / rate) - 1 < parent / rate
	 * and thus
	 *   min_rate <= rate < parent / (div - 1)
	 */
	if (result < req->min_rate && div > 1)
		result = parent / (div - 1);

	req->rate = result;
	return 0;
}

static int jh7100_clk_set_rate(struct clk_hw *hw,
			       unsigned long rate,
			       unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long div = clamp(DIV_ROUND_CLOSEST(parent_rate, rate),
				  1UL, (unsigned long)clk->max_div);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_DIV_MASK, div);
	return 0;
}

static unsigned long jh7100_clk_frac_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 reg = jh7100_clk_reg_get(clk);
	unsigned long div100 = 100 * (reg & JH7100_CLK_INT_MASK) +
			       ((reg & JH7100_CLK_FRAC_MASK) >> JH7100_CLK_FRAC_SHIFT);

	return (div100 >= JH7100_CLK_FRAC_MIN) ? 100 * parent_rate / div100 : 0;
}

static int jh7100_clk_frac_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	unsigned long parent100 = 100 * req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(parent100, rate),
				     JH7100_CLK_FRAC_MIN, JH7100_CLK_FRAC_MAX);
	unsigned long result = parent100 / div100;

	/* clamp the result as in jh7100_clk_determine_rate() above */
	if (result > req->max_rate && div100 < JH7100_CLK_FRAC_MAX)
		result = parent100 / (div100 + 1);
	if (result < req->min_rate && div100 > JH7100_CLK_FRAC_MIN)
		result = parent100 / (div100 - 1);

	req->rate = result;
	return 0;
}

static int jh7100_clk_frac_set_rate(struct clk_hw *hw,
				    unsigned long rate,
				    unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(100 * parent_rate, rate),
				     JH7100_CLK_FRAC_MIN, JH7100_CLK_FRAC_MAX);
	u32 value = ((div100 % 100) << JH7100_CLK_FRAC_SHIFT) | (div100 / 100);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_DIV_MASK, value);
	return 0;
}

static u8 jh7100_clk_get_parent(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = jh7100_clk_reg_get(clk);

	return (value & JH7100_CLK_MUX_MASK) >> JH7100_CLK_MUX_SHIFT;
}

static int jh7100_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = (u32)index << JH7100_CLK_MUX_SHIFT;

	jh7100_clk_reg_rmw(clk, JH7100_CLK_MUX_MASK, value);
	return 0;
}

static int jh7100_clk_mux_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, 0);
}

static int jh7100_clk_get_phase(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = jh7100_clk_reg_get(clk);

	return (value & JH7100_CLK_INVERT) ? 180 : 0;
}

static int jh7100_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value;

	if (degrees == 0)
		value = 0;
	else if (degrees == 180)
		value = JH7100_CLK_INVERT;
	else
		return -EINVAL;

	jh7100_clk_reg_rmw(clk, JH7100_CLK_INVERT, value);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void jh7100_clk_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	static const struct debugfs_reg32 jh7100_clk_reg = {
		.name = "CTRL",
		.offset = 0,
	};
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(priv->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = &jh7100_clk_reg;
	regset->nregs = 1;
	regset->base = priv->base + 4 * clk->idx;

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#else
#define jh7100_clk_debug_init NULL
#endif

static const struct clk_ops jh7100_clk_gate_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_div_ops = {
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_fdiv_ops = {
	.recalc_rate = jh7100_clk_frac_recalc_rate,
	.determine_rate = jh7100_clk_frac_determine_rate,
	.set_rate = jh7100_clk_frac_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_gdiv_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_mux_ops = {
	.determine_rate = jh7100_clk_mux_determine_rate,
	.set_parent = jh7100_clk_set_parent,
	.get_parent = jh7100_clk_get_parent,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_gmux_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.determine_rate = jh7100_clk_mux_determine_rate,
	.set_parent = jh7100_clk_set_parent,
	.get_parent = jh7100_clk_get_parent,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_inv_ops = {
	.get_phase = jh7100_clk_get_phase,
	.set_phase = jh7100_clk_set_phase,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops *__init jh7100_clk_ops(u32 max)
{
	if (max & JH7100_CLK_DIV_MASK) {
		if (max & JH7100_CLK_ENABLE)
			return &jh7100_clk_gdiv_ops;
		if (max == JH7100_CLK_FRAC_MAX)
			return &jh7100_clk_fdiv_ops;
		return &jh7100_clk_div_ops;
	}

	if (max & JH7100_CLK_MUX_MASK) {
		if (max & JH7100_CLK_ENABLE)
			return &jh7100_clk_gmux_ops;
		return &jh7100_clk_mux_ops;
	}

	if (max & JH7100_CLK_ENABLE)
		return &jh7100_clk_gate_ops;

	return &jh7100_clk_inv_ops;
}

static struct clk_hw *jh7100_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct jh7100_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx < JH7100_CLK_PLL0_OUT)
		return &priv->reg[idx].hw;

	if (idx < JH7100_CLK_END)
		return priv->pll[idx - JH7100_CLK_PLL0_OUT];

	return ERR_PTR(-EINVAL);
}

static int __init clk_starfive_jh7100_probe(struct platform_device *pdev)
{
	struct jh7100_clk_priv *priv;
	unsigned int idx;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->dev = &pdev->dev;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->pll[0] = devm_clk_hw_register_fixed_factor(priv->dev, "pll0_out",
							 "osc_sys", 0, 40, 1);
	if (IS_ERR(priv->pll[0]))
		return PTR_ERR(priv->pll[0]);

	priv->pll[1] = devm_clk_hw_register_fixed_factor(priv->dev, "pll1_out",
							 "osc_sys", 0, 64, 1);
	if (IS_ERR(priv->pll[1]))
		return PTR_ERR(priv->pll[1]);

	priv->pll[2] = devm_clk_hw_register_fixed_factor(priv->dev, "pll2_out",
							 "pll2_refclk", 0, 55, 1);
	if (IS_ERR(priv->pll[2]))
		return PTR_ERR(priv->pll[2]);

	for (idx = 0; idx < JH7100_CLK_PLL0_OUT; idx++) {
		u32 max = jh7100_clk_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7100_clk_data[idx].name,
			.ops = jh7100_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7100_CLK_MUX_MASK) >> JH7100_CLK_MUX_SHIFT) + 1,
			.flags = jh7100_clk_data[idx].flags,
		};
		struct jh7100_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7100_clk_data[idx].parents[i];

			if (pidx < JH7100_CLK_PLL0_OUT)
				parents[i].hw = &priv->reg[pidx].hw;
			else if (pidx < JH7100_CLK_END)
				parents[i].hw = priv->pll[pidx - JH7100_CLK_PLL0_OUT];
			else if (pidx == JH7100_CLK_OSC_SYS)
				parents[i].fw_name = "osc_sys";
			else if (pidx == JH7100_CLK_OSC_AUD)
				parents[i].fw_name = "osc_aud";
			else if (pidx == JH7100_CLK_GMAC_RMII_REF)
				parents[i].fw_name = "gmac_rmii_ref";
			else if (pidx == JH7100_CLK_GMAC_GR_MII_RX)
				parents[i].fw_name = "gmac_gr_mii_rxclk";
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7100_CLK_DIV_MASK;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(priv->dev, jh7100_clk_get, priv);
}

static const struct of_device_id clk_starfive_jh7100_match[] = {
	{ .compatible = "starfive,jh7100-clkgen" },
	{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7100_driver = {
	.driver = {
		.name = "clk-starfive-jh7100",
		.of_match_table = clk_starfive_jh7100_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(clk_starfive_jh7100_driver, clk_starfive_jh7100_probe);
