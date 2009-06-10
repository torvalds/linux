/*
 * Table of the DAVINCI register configurations for the PINMUX combinations
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * Based on linux/include/asm-arm/arch-omap/mux.h:
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright (C) 2008 Texas Instruments.
 */

#ifndef __INC_MACH_MUX_H
#define __INC_MACH_MUX_H

/* System module registers */
#define PINMUX0			0x00
#define PINMUX1			0x04
/* dm355 only */
#define PINMUX2			0x08
#define PINMUX3			0x0c
#define PINMUX4			0x10
#define INTMUX			0x18
#define EVTMUX			0x1c

struct mux_config {
	const char *name;
	const char *mux_reg_name;
	const unsigned char mux_reg;
	const unsigned char mask_offset;
	const unsigned char mask;
	const unsigned char mode;
	bool debug;
};

enum davinci_dm644x_index {
	/* ATA and HDDIR functions */
	DM644X_HDIREN,
	DM644X_ATAEN,
	DM644X_ATAEN_DISABLE,

	/* HPI functions */
	DM644X_HPIEN_DISABLE,

	/* AEAW functions */
	DM644X_AEAW,

	/* Memory Stick */
	DM644X_MSTK,

	/* I2C */
	DM644X_I2C,

	/* ASP function */
	DM644X_MCBSP,

	/* UART1 */
	DM644X_UART1,

	/* UART2 */
	DM644X_UART2,

	/* PWM0 */
	DM644X_PWM0,

	/* PWM1 */
	DM644X_PWM1,

	/* PWM2 */
	DM644X_PWM2,

	/* VLYNQ function */
	DM644X_VLYNQEN,
	DM644X_VLSCREN,
	DM644X_VLYNQWD,

	/* EMAC and MDIO function */
	DM644X_EMACEN,

	/* GPIO3V[0:16] pins */
	DM644X_GPIO3V,

	/* GPIO pins */
	DM644X_GPIO0,
	DM644X_GPIO3,
	DM644X_GPIO43_44,
	DM644X_GPIO46_47,

	/* VPBE */
	DM644X_RGB666,

	/* LCD */
	DM644X_LOEEN,
	DM644X_LFLDEN,
};

enum davinci_dm646x_index {
	/* ATA function */
	DM646X_ATAEN,

	/* AUDIO Clock */
	DM646X_AUDCK1,
	DM646X_AUDCK0,

	/* CRGEN Control */
	DM646X_CRGMUX,

	/* VPIF Control */
	DM646X_STSOMUX_DISABLE,
	DM646X_STSIMUX_DISABLE,
	DM646X_PTSOMUX_DISABLE,
	DM646X_PTSIMUX_DISABLE,

	/* TSIF Control */
	DM646X_STSOMUX,
	DM646X_STSIMUX,
	DM646X_PTSOMUX_PARALLEL,
	DM646X_PTSIMUX_PARALLEL,
	DM646X_PTSOMUX_SERIAL,
	DM646X_PTSIMUX_SERIAL,
};

enum davinci_dm355_index {
	/* MMC/SD 0 */
	DM355_MMCSD0,

	/* MMC/SD 1 */
	DM355_SD1_CLK,
	DM355_SD1_CMD,
	DM355_SD1_DATA3,
	DM355_SD1_DATA2,
	DM355_SD1_DATA1,
	DM355_SD1_DATA0,

	/* I2C */
	DM355_I2C_SDA,
	DM355_I2C_SCL,

	/* ASP0 function */
	DM355_MCBSP0_BDX,
	DM355_MCBSP0_X,
	DM355_MCBSP0_BFSX,
	DM355_MCBSP0_BDR,
	DM355_MCBSP0_R,
	DM355_MCBSP0_BFSR,

	/* SPI0 */
	DM355_SPI0_SDI,
	DM355_SPI0_SDENA0,
	DM355_SPI0_SDENA1,

	/* IRQ muxing */
	DM355_INT_EDMA_CC,
	DM355_INT_EDMA_TC0_ERR,
	DM355_INT_EDMA_TC1_ERR,

	/* EDMA event muxing */
	DM355_EVT8_ASP1_TX,
	DM355_EVT9_ASP1_RX,
	DM355_EVT26_MMC0_RX,
};

#ifdef CONFIG_DAVINCI_MUX
/* setup pin muxing */
extern void davinci_mux_init(void);
extern int davinci_mux_register(const struct mux_config *pins,
				unsigned long size);
extern int davinci_cfg_reg(unsigned long reg_cfg);
#else
/* boot loader does it all (no warnings from CONFIG_DAVINCI_MUX_WARNINGS) */
static inline void davinci_mux_init(void) {}
static inline int davinci_mux_register(const struct mux_config *pins,
				       unsigned long size) { return 0; }
static inline int davinci_cfg_reg(unsigned long reg_cfg) { return 0; }
#endif

#endif /* __INC_MACH_MUX_H */
