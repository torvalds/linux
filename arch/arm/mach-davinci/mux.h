/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pin-multiplex helper macros for TI DaVinci family devices
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc.
 *
 * Copyright (C) 2008 Texas Instruments.
 */
#ifndef _MACH_DAVINCI_MUX_H_
#define _MACH_DAVINCI_MUX_H_

struct mux_config {
	const char *name;
	const char *mux_reg_name;
	const unsigned char mux_reg;
	const unsigned char mask_offset;
	const unsigned char mask;
	const unsigned char mode;
	bool debug;
};

enum davinci_da850_index {
	/* UART0 function */
	DA850_NUART0_CTS,
	DA850_NUART0_RTS,
	DA850_UART0_RXD,
	DA850_UART0_TXD,

	/* UART1 function */
	DA850_NUART1_CTS,
	DA850_NUART1_RTS,
	DA850_UART1_RXD,
	DA850_UART1_TXD,

	/* UART2 function */
	DA850_NUART2_CTS,
	DA850_NUART2_RTS,
	DA850_UART2_RXD,
	DA850_UART2_TXD,

	/* I2C1 function */
	DA850_I2C1_SCL,
	DA850_I2C1_SDA,

	/* I2C0 function */
	DA850_I2C0_SDA,
	DA850_I2C0_SCL,

	/* EMAC function */
	DA850_MII_TXEN,
	DA850_MII_TXCLK,
	DA850_MII_COL,
	DA850_MII_TXD_3,
	DA850_MII_TXD_2,
	DA850_MII_TXD_1,
	DA850_MII_TXD_0,
	DA850_MII_RXER,
	DA850_MII_CRS,
	DA850_MII_RXCLK,
	DA850_MII_RXDV,
	DA850_MII_RXD_3,
	DA850_MII_RXD_2,
	DA850_MII_RXD_1,
	DA850_MII_RXD_0,
	DA850_MDIO_CLK,
	DA850_MDIO_D,
	DA850_RMII_TXD_0,
	DA850_RMII_TXD_1,
	DA850_RMII_TXEN,
	DA850_RMII_CRS_DV,
	DA850_RMII_RXD_0,
	DA850_RMII_RXD_1,
	DA850_RMII_RXER,
	DA850_RMII_MHZ_50_CLK,

	/* McASP function */
	DA850_ACLKR,
	DA850_ACLKX,
	DA850_AFSR,
	DA850_AFSX,
	DA850_AHCLKR,
	DA850_AHCLKX,
	DA850_AMUTE,
	DA850_AXR_15,
	DA850_AXR_14,
	DA850_AXR_13,
	DA850_AXR_12,
	DA850_AXR_11,
	DA850_AXR_10,
	DA850_AXR_9,
	DA850_AXR_8,
	DA850_AXR_7,
	DA850_AXR_6,
	DA850_AXR_5,
	DA850_AXR_4,
	DA850_AXR_3,
	DA850_AXR_2,
	DA850_AXR_1,
	DA850_AXR_0,

	/* LCD function */
	DA850_LCD_D_7,
	DA850_LCD_D_6,
	DA850_LCD_D_5,
	DA850_LCD_D_4,
	DA850_LCD_D_3,
	DA850_LCD_D_2,
	DA850_LCD_D_1,
	DA850_LCD_D_0,
	DA850_LCD_D_15,
	DA850_LCD_D_14,
	DA850_LCD_D_13,
	DA850_LCD_D_12,
	DA850_LCD_D_11,
	DA850_LCD_D_10,
	DA850_LCD_D_9,
	DA850_LCD_D_8,
	DA850_LCD_PCLK,
	DA850_LCD_HSYNC,
	DA850_LCD_VSYNC,
	DA850_NLCD_AC_ENB_CS,

	/* MMC/SD0 function */
	DA850_MMCSD0_DAT_0,
	DA850_MMCSD0_DAT_1,
	DA850_MMCSD0_DAT_2,
	DA850_MMCSD0_DAT_3,
	DA850_MMCSD0_CLK,
	DA850_MMCSD0_CMD,

	/* MMC/SD1 function */
	DA850_MMCSD1_DAT_0,
	DA850_MMCSD1_DAT_1,
	DA850_MMCSD1_DAT_2,
	DA850_MMCSD1_DAT_3,
	DA850_MMCSD1_CLK,
	DA850_MMCSD1_CMD,

	/* EMIF2.5/EMIFA function */
	DA850_EMA_D_7,
	DA850_EMA_D_6,
	DA850_EMA_D_5,
	DA850_EMA_D_4,
	DA850_EMA_D_3,
	DA850_EMA_D_2,
	DA850_EMA_D_1,
	DA850_EMA_D_0,
	DA850_EMA_A_1,
	DA850_EMA_A_2,
	DA850_NEMA_CS_3,
	DA850_NEMA_CS_4,
	DA850_NEMA_WE,
	DA850_NEMA_OE,
	DA850_EMA_D_15,
	DA850_EMA_D_14,
	DA850_EMA_D_13,
	DA850_EMA_D_12,
	DA850_EMA_D_11,
	DA850_EMA_D_10,
	DA850_EMA_D_9,
	DA850_EMA_D_8,
	DA850_EMA_A_0,
	DA850_EMA_A_3,
	DA850_EMA_A_4,
	DA850_EMA_A_5,
	DA850_EMA_A_6,
	DA850_EMA_A_7,
	DA850_EMA_A_8,
	DA850_EMA_A_9,
	DA850_EMA_A_10,
	DA850_EMA_A_11,
	DA850_EMA_A_12,
	DA850_EMA_A_13,
	DA850_EMA_A_14,
	DA850_EMA_A_15,
	DA850_EMA_A_16,
	DA850_EMA_A_17,
	DA850_EMA_A_18,
	DA850_EMA_A_19,
	DA850_EMA_A_20,
	DA850_EMA_A_21,
	DA850_EMA_A_22,
	DA850_EMA_A_23,
	DA850_EMA_BA_1,
	DA850_EMA_CLK,
	DA850_EMA_WAIT_1,
	DA850_NEMA_CS_2,

	/* GPIO function */
	DA850_GPIO2_4,
	DA850_GPIO2_6,
	DA850_GPIO2_8,
	DA850_GPIO2_15,
	DA850_GPIO3_12,
	DA850_GPIO3_13,
	DA850_GPIO4_0,
	DA850_GPIO4_1,
	DA850_GPIO6_9,
	DA850_GPIO6_10,
	DA850_GPIO6_13,
	DA850_RTC_ALARM,

	/* VPIF Capture */
	DA850_VPIF_DIN0,
	DA850_VPIF_DIN1,
	DA850_VPIF_DIN2,
	DA850_VPIF_DIN3,
	DA850_VPIF_DIN4,
	DA850_VPIF_DIN5,
	DA850_VPIF_DIN6,
	DA850_VPIF_DIN7,
	DA850_VPIF_DIN8,
	DA850_VPIF_DIN9,
	DA850_VPIF_DIN10,
	DA850_VPIF_DIN11,
	DA850_VPIF_DIN12,
	DA850_VPIF_DIN13,
	DA850_VPIF_DIN14,
	DA850_VPIF_DIN15,
	DA850_VPIF_CLKIN0,
	DA850_VPIF_CLKIN1,
	DA850_VPIF_CLKIN2,
	DA850_VPIF_CLKIN3,

	/* VPIF Display */
	DA850_VPIF_DOUT0,
	DA850_VPIF_DOUT1,
	DA850_VPIF_DOUT2,
	DA850_VPIF_DOUT3,
	DA850_VPIF_DOUT4,
	DA850_VPIF_DOUT5,
	DA850_VPIF_DOUT6,
	DA850_VPIF_DOUT7,
	DA850_VPIF_DOUT8,
	DA850_VPIF_DOUT9,
	DA850_VPIF_DOUT10,
	DA850_VPIF_DOUT11,
	DA850_VPIF_DOUT12,
	DA850_VPIF_DOUT13,
	DA850_VPIF_DOUT14,
	DA850_VPIF_DOUT15,
	DA850_VPIF_CLKO2,
	DA850_VPIF_CLKO3,
};

#define PINMUX(x)		(4 * (x))

#ifdef CONFIG_DAVINCI_MUX
/* setup pin muxing */
extern int davinci_cfg_reg(unsigned long reg_cfg);
#else
/* boot loader does it all (no warnings from CONFIG_DAVINCI_MUX_WARNINGS) */
static inline int davinci_cfg_reg(unsigned long reg_cfg) { return 0; }
#endif


#define MUX_CFG(soc, desc, muxreg, mode_offset, mode_mask, mux_mode, dbg)\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "PINMUX"#muxreg,		\
			.mux_reg = PINMUX(muxreg),			\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define INT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "INTMUX",			\
			.mux_reg = INTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#define EVT_CFG(soc, desc, mode_offset, mode_mask, mux_mode, dbg)	\
[soc##_##desc] = {							\
			.name =  #desc,					\
			.debug = dbg,					\
			.mux_reg_name = "EVTMUX",			\
			.mux_reg = EVTMUX,				\
			.mask_offset = mode_offset,			\
			.mask = mode_mask,				\
			.mode = mux_mode,				\
		},

#endif /* _MACH_DAVINCI_MUX_H */
