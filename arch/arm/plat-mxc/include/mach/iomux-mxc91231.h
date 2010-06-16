/*
 * Copyright 2004-2006 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 * Copyright (C) 2009 by Dmitriy Taychenachev <dimichxp@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_IOMUX_MXC91231_H__
#define __MACH_IOMUX_MXC91231_H__

/*
 * various IOMUX output functions
 */

#define	IOMUX_OCONFIG_GPIO (0 << 4)	/* used as GPIO */
#define	IOMUX_OCONFIG_FUNC (1 << 4)	/* used as function */
#define	IOMUX_OCONFIG_ALT1 (2 << 4)	/* used as alternate function 1 */
#define	IOMUX_OCONFIG_ALT2 (3 << 4)	/* used as alternate function 2 */
#define	IOMUX_OCONFIG_ALT3 (4 << 4)	/* used as alternate function 3 */
#define	IOMUX_OCONFIG_ALT4 (5 << 4)	/* used as alternate function 4 */
#define	IOMUX_OCONFIG_ALT5 (6 << 4)	/* used as alternate function 5 */
#define	IOMUX_OCONFIG_ALT6 (7 << 4)	/* used as alternate function 6 */
#define	IOMUX_ICONFIG_NONE  0	 	/* not configured for input */
#define	IOMUX_ICONFIG_GPIO  1		/* used as GPIO */
#define	IOMUX_ICONFIG_FUNC  2		/* used as function */
#define	IOMUX_ICONFIG_ALT1  4		/* used as alternate function 1 */
#define	IOMUX_ICONFIG_ALT2  8		/* used as alternate function 2 */

#define IOMUX_CONFIG_GPIO (IOMUX_OCONFIG_GPIO | IOMUX_ICONFIG_GPIO)
#define IOMUX_CONFIG_FUNC (IOMUX_OCONFIG_FUNC | IOMUX_ICONFIG_FUNC)
#define IOMUX_CONFIG_ALT1 (IOMUX_OCONFIG_ALT1 | IOMUX_ICONFIG_ALT1)
#define IOMUX_CONFIG_ALT2 (IOMUX_OCONFIG_ALT2 | IOMUX_ICONFIG_ALT2)

/*
 * setups a single pin:
 * 	- reserves the pin so that it is not claimed by another driver
 * 	- setups the iomux according to the configuration
 * 	- if the pin is configured as a GPIO, we claim it through kernel gpiolib
 */
int mxc_iomux_alloc_pin(const unsigned int pin_mode, const char *label);
/*
 * setups mutliple pins
 * convenient way to call the above function with tables
 */
int mxc_iomux_setup_multiple_pins(unsigned int *pin_list, unsigned count,
		const char *label);

/*
 * releases a single pin:
 * 	- make it available for a future use by another driver
 * 	- frees the GPIO if the pin was configured as GPIO
 * 	- DOES NOT reconfigure the IOMUX in its reset state
 */
void mxc_iomux_release_pin(const unsigned int pin_mode);
/*
 * releases multiple pins
 * convenvient way to call the above function with tables
 */
void mxc_iomux_release_multiple_pins(unsigned int *pin_list, int count);

#define MUX_SIDE_AP		(0)
#define MUX_SIDE_SP		(1)

#define MUX_SIDE_SHIFT		(26)
#define MUX_SIDE_MASK		(0x1 << MUX_SIDE_SHIFT)

#define MUX_GPIO_PORT_SHIFT	(23)
#define MUX_GPIO_PORT_MASK	(0x7 << MUX_GPIO_PORT_SHIFT)

#define MUX_GPIO_PIN_SHIFT	(20)
#define MUX_GPIO_PIN_MASK	(0x1f << MUX_GPIO_PIN_SHIFT)

#define MUX_REG_SHIFT		(15)
#define MUX_REG_MASK		(0x1f << MUX_REG_SHIFT)

#define MUX_FIELD_SHIFT		(13)
#define MUX_FIELD_MASK		(0x3 << MUX_FIELD_SHIFT)

#define MUX_PADGRP_SHIFT	(8)
#define MUX_PADGRP_MASK		(0x1f << MUX_PADGRP_SHIFT)

#define MUX_PIN_MASK		(0xffffff << 8)

#define GPIO_PORT_MAX		(3)

#define IOMUX_PIN(side, gport, gpin, ctlreg, ctlfield, padgrp) \
	(((side) << MUX_SIDE_SHIFT) |		  \
	 (gport << MUX_GPIO_PORT_SHIFT) |		\
	 ((gpin) << MUX_GPIO_PIN_SHIFT) |		\
	 ((ctlreg) << MUX_REG_SHIFT) |		\
	 ((ctlfield) << MUX_FIELD_SHIFT) |		\
	 ((padgrp) << MUX_PADGRP_SHIFT))

#define MUX_MODE_OUT_SHIFT	(4)
#define MUX_MODE_IN_SHIFT	(0)
#define MUX_MODE_SHIFT		(0)
#define MUX_MODE_MASK		(0xff << MUX_MODE_SHIFT)

#define IOMUX_MODE(pin, mode) \
	(pin | (mode << MUX_MODE_SHIFT))

enum iomux_pins {
	/* AP Side pins */
	MXC91231_PIN_AP_CLE		= IOMUX_PIN(0, 0,  0,  0, 0, 24),
	MXC91231_PIN_AP_ALE		= IOMUX_PIN(0, 0,  1,  0, 1, 24),
	MXC91231_PIN_AP_CE_B		= IOMUX_PIN(0, 0,  2,  0, 2, 24),
	MXC91231_PIN_AP_RE_B		= IOMUX_PIN(0, 0,  3,  0, 3, 24),
	MXC91231_PIN_AP_WE_B		= IOMUX_PIN(0, 0,  4,  1, 0, 24),
	MXC91231_PIN_AP_WP_B		= IOMUX_PIN(0, 0,  5,  1, 1, 24),
	MXC91231_PIN_AP_BSY_B		= IOMUX_PIN(0, 0,  6,  1, 2, 24),
	MXC91231_PIN_AP_U1_TXD		= IOMUX_PIN(0, 0,  7,  1, 3, 28),
	MXC91231_PIN_AP_U1_RXD		= IOMUX_PIN(0, 0,  8,  2, 0, 28),
	MXC91231_PIN_AP_U1_RTS_B	= IOMUX_PIN(0, 0,  9,  2, 1, 28),
	MXC91231_PIN_AP_U1_CTS_B	= IOMUX_PIN(0, 0, 10,  2, 2, 28),
	MXC91231_PIN_AP_AD1_TXD		= IOMUX_PIN(0, 0, 11,  2, 3,  9),
	MXC91231_PIN_AP_AD1_RXD		= IOMUX_PIN(0, 0, 12,  3, 0,  9),
	MXC91231_PIN_AP_AD1_TXC		= IOMUX_PIN(0, 0, 13,  3, 1,  9),
	MXC91231_PIN_AP_AD1_TXFS	= IOMUX_PIN(0, 0, 14,  3, 2,  9),
	MXC91231_PIN_AP_AD2_TXD		= IOMUX_PIN(0, 0, 15,  3, 3,  9),
	MXC91231_PIN_AP_AD2_RXD		= IOMUX_PIN(0, 0, 16,  4, 0,  9),
	MXC91231_PIN_AP_AD2_TXC		= IOMUX_PIN(0, 0, 17,  4, 1,  9),
	MXC91231_PIN_AP_AD2_TXFS	= IOMUX_PIN(0, 0, 18,  4, 2,  9),
	MXC91231_PIN_AP_OWDAT		= IOMUX_PIN(0, 0, 19,  4, 3, 28),
	MXC91231_PIN_AP_IPU_LD17	= IOMUX_PIN(0, 0, 20,  5, 0, 28),
	MXC91231_PIN_AP_IPU_D3_VSYNC	= IOMUX_PIN(0, 0, 21,  5, 1, 28),
	MXC91231_PIN_AP_IPU_D3_HSYNC	= IOMUX_PIN(0, 0, 22,  5, 2, 28),
	MXC91231_PIN_AP_IPU_D3_CLK	= IOMUX_PIN(0, 0, 23,  5, 3, 28),
	MXC91231_PIN_AP_IPU_D3_DRDY	= IOMUX_PIN(0, 0, 24,  6, 0, 28),
	MXC91231_PIN_AP_IPU_D3_CONTR	= IOMUX_PIN(0, 0, 25,  6, 1, 28),
	MXC91231_PIN_AP_IPU_D0_CS	= IOMUX_PIN(0, 0, 26,  6, 2, 28),
	MXC91231_PIN_AP_IPU_LD16	= IOMUX_PIN(0, 0, 27,  6, 3, 28),
	MXC91231_PIN_AP_IPU_D2_CS	= IOMUX_PIN(0, 0, 28,  7, 0, 28),
	MXC91231_PIN_AP_IPU_PAR_RS	= IOMUX_PIN(0, 0, 29,  7, 1, 28),
	MXC91231_PIN_AP_IPU_D3_PS	= IOMUX_PIN(0, 0, 30,  7, 2, 28),
	MXC91231_PIN_AP_IPU_D3_CLS	= IOMUX_PIN(0, 0, 31,  7, 3, 28),
	MXC91231_PIN_AP_IPU_RD		= IOMUX_PIN(0, 1,  0,  8, 0, 28),
	MXC91231_PIN_AP_IPU_WR		= IOMUX_PIN(0, 1,  1,  8, 1, 28),
	MXC91231_PIN_AP_IPU_LD0		= IOMUX_PIN(0, 7,  0,  8, 2, 28),
	MXC91231_PIN_AP_IPU_LD1		= IOMUX_PIN(0, 7,  0,  8, 3, 28),
	MXC91231_PIN_AP_IPU_LD2		= IOMUX_PIN(0, 7,  0,  9, 0, 28),
	MXC91231_PIN_AP_IPU_LD3		= IOMUX_PIN(0, 1,  2,  9, 1, 28),
	MXC91231_PIN_AP_IPU_LD4		= IOMUX_PIN(0, 1,  3,  9, 2, 28),
	MXC91231_PIN_AP_IPU_LD5		= IOMUX_PIN(0, 1,  4,  9, 3, 28),
	MXC91231_PIN_AP_IPU_LD6		= IOMUX_PIN(0, 1,  5, 10, 0, 28),
	MXC91231_PIN_AP_IPU_LD7		= IOMUX_PIN(0, 1,  6, 10, 1, 28),
	MXC91231_PIN_AP_IPU_LD8		= IOMUX_PIN(0, 1,  7, 10, 2, 28),
	MXC91231_PIN_AP_IPU_LD9		= IOMUX_PIN(0, 1,  8, 10, 3, 28),
	MXC91231_PIN_AP_IPU_LD10	= IOMUX_PIN(0, 1,  9, 11, 0, 28),
	MXC91231_PIN_AP_IPU_LD11	= IOMUX_PIN(0, 1, 10, 11, 1, 28),
	MXC91231_PIN_AP_IPU_LD12	= IOMUX_PIN(0, 1, 11, 11, 2, 28),
	MXC91231_PIN_AP_IPU_LD13	= IOMUX_PIN(0, 1, 12, 11, 3, 28),
	MXC91231_PIN_AP_IPU_LD14	= IOMUX_PIN(0, 1, 13, 12, 0, 28),
	MXC91231_PIN_AP_IPU_LD15	= IOMUX_PIN(0, 1, 14, 12, 1, 28),
	MXC91231_PIN_AP_KPROW4		= IOMUX_PIN(0, 7,  0, 12, 2, 10),
	MXC91231_PIN_AP_KPROW5		= IOMUX_PIN(0, 1, 16, 12, 3, 10),
	MXC91231_PIN_AP_GPIO_AP_B17	= IOMUX_PIN(0, 1, 17, 13, 0, 10),
	MXC91231_PIN_AP_GPIO_AP_B18	= IOMUX_PIN(0, 1, 18, 13, 1, 10),
	MXC91231_PIN_AP_KPCOL3		= IOMUX_PIN(0, 1, 19, 13, 2, 11),
	MXC91231_PIN_AP_KPCOL4		= IOMUX_PIN(0, 1, 20, 13, 3, 11),
	MXC91231_PIN_AP_KPCOL5		= IOMUX_PIN(0, 1, 21, 14, 0, 11),
	MXC91231_PIN_AP_GPIO_AP_B22	= IOMUX_PIN(0, 1, 22, 14, 1, 11),
	MXC91231_PIN_AP_GPIO_AP_B23	= IOMUX_PIN(0, 1, 23, 14, 2, 11),
	MXC91231_PIN_AP_CSI_D0		= IOMUX_PIN(0, 1, 24, 14, 3, 21),
	MXC91231_PIN_AP_CSI_D1		= IOMUX_PIN(0, 1, 25, 15, 0, 21),
	MXC91231_PIN_AP_CSI_D2		= IOMUX_PIN(0, 1, 26, 15, 1, 21),
	MXC91231_PIN_AP_CSI_D3		= IOMUX_PIN(0, 1, 27, 15, 2, 21),
	MXC91231_PIN_AP_CSI_D4		= IOMUX_PIN(0, 1, 28, 15, 3, 21),
	MXC91231_PIN_AP_CSI_D5		= IOMUX_PIN(0, 1, 29, 16, 0, 21),
	MXC91231_PIN_AP_CSI_D6		= IOMUX_PIN(0, 1, 30, 16, 1, 21),
	MXC91231_PIN_AP_CSI_D7		= IOMUX_PIN(0, 1, 31, 16, 2, 21),
	MXC91231_PIN_AP_CSI_D8		= IOMUX_PIN(0, 2,  0, 16, 3, 21),
	MXC91231_PIN_AP_CSI_D9		= IOMUX_PIN(0, 2,  1, 17, 0, 21),
	MXC91231_PIN_AP_CSI_MCLK	= IOMUX_PIN(0, 2,  2, 17, 1, 21),
	MXC91231_PIN_AP_CSI_VSYNC	= IOMUX_PIN(0, 2,  3, 17, 2, 21),
	MXC91231_PIN_AP_CSI_HSYNC	= IOMUX_PIN(0, 2,  4, 17, 3, 21),
	MXC91231_PIN_AP_CSI_PIXCLK	= IOMUX_PIN(0, 2,  5, 18, 0, 21),
	MXC91231_PIN_AP_I2CLK		= IOMUX_PIN(0, 2,  6, 18, 1, 12),
	MXC91231_PIN_AP_I2DAT		= IOMUX_PIN(0, 2,  7, 18, 2, 12),
	MXC91231_PIN_AP_GPIO_AP_C8	= IOMUX_PIN(0, 2,  8, 18, 3,  9),
	MXC91231_PIN_AP_GPIO_AP_C9	= IOMUX_PIN(0, 2,  9, 19, 0,  9),
	MXC91231_PIN_AP_GPIO_AP_C10	= IOMUX_PIN(0, 2, 10, 19, 1,  9),
	MXC91231_PIN_AP_GPIO_AP_C11	= IOMUX_PIN(0, 2, 11, 19, 2,  9),
	MXC91231_PIN_AP_GPIO_AP_C12	= IOMUX_PIN(0, 2, 12, 19, 3,  9),
	MXC91231_PIN_AP_GPIO_AP_C13	= IOMUX_PIN(0, 2, 13, 20, 0, 28),
	MXC91231_PIN_AP_GPIO_AP_C14	= IOMUX_PIN(0, 2, 14, 20, 1, 28),
	MXC91231_PIN_AP_GPIO_AP_C15	= IOMUX_PIN(0, 2, 15, 20, 2,  9),
	MXC91231_PIN_AP_GPIO_AP_C16	= IOMUX_PIN(0, 2, 16, 20, 3,  9),
	MXC91231_PIN_AP_GPIO_AP_C17	= IOMUX_PIN(0, 2, 17, 21, 0,  9),
	MXC91231_PIN_AP_ED_INT0		= IOMUX_PIN(0, 2, 18, 21, 1, 22),
	MXC91231_PIN_AP_ED_INT1		= IOMUX_PIN(0, 2, 19, 21, 2, 22),
	MXC91231_PIN_AP_ED_INT2		= IOMUX_PIN(0, 2, 20, 21, 3, 22),
	MXC91231_PIN_AP_ED_INT3		= IOMUX_PIN(0, 2, 21, 22, 0, 22),
	MXC91231_PIN_AP_ED_INT4		= IOMUX_PIN(0, 2, 22, 22, 1, 23),
	MXC91231_PIN_AP_ED_INT5		= IOMUX_PIN(0, 2, 23, 22, 2, 23),
	MXC91231_PIN_AP_ED_INT6		= IOMUX_PIN(0, 2, 24, 22, 3, 23),
	MXC91231_PIN_AP_ED_INT7		= IOMUX_PIN(0, 2, 25, 23, 0, 23),
	MXC91231_PIN_AP_U2_DSR_B	= IOMUX_PIN(0, 2, 26, 23, 1, 28),
	MXC91231_PIN_AP_U2_RI_B		= IOMUX_PIN(0, 2, 27, 23, 2, 28),
	MXC91231_PIN_AP_U2_CTS_B	= IOMUX_PIN(0, 2, 28, 23, 3, 28),
	MXC91231_PIN_AP_U2_DTR_B	= IOMUX_PIN(0, 2, 29, 24, 0, 28),
	MXC91231_PIN_AP_KPROW0		= IOMUX_PIN(0, 7,  0, 24, 1, 10),
	MXC91231_PIN_AP_KPROW1		= IOMUX_PIN(0, 1, 15, 24, 2, 10),
	MXC91231_PIN_AP_KPROW2		= IOMUX_PIN(0, 7,  0, 24, 3, 10),
	MXC91231_PIN_AP_KPROW3		= IOMUX_PIN(0, 7,  0, 25, 0, 10),
	MXC91231_PIN_AP_KPCOL0		= IOMUX_PIN(0, 7,  0, 25, 1, 11),
	MXC91231_PIN_AP_KPCOL1		= IOMUX_PIN(0, 7,  0, 25, 2, 11),
	MXC91231_PIN_AP_KPCOL2		= IOMUX_PIN(0, 7,  0, 25, 3, 11),

	/* Shared pins */
	MXC91231_PIN_SP_U3_TXD		= IOMUX_PIN(1, 3,  0,  0, 0, 28),
	MXC91231_PIN_SP_U3_RXD		= IOMUX_PIN(1, 3,  1,  0, 1, 28),
	MXC91231_PIN_SP_U3_RTS_B	= IOMUX_PIN(1, 3,  2,  0, 2, 28),
	MXC91231_PIN_SP_U3_CTS_B	= IOMUX_PIN(1, 3,  3,  0, 3, 28),
	MXC91231_PIN_SP_USB_TXOE_B	= IOMUX_PIN(1, 3,  4,  1, 0, 28),
	MXC91231_PIN_SP_USB_DAT_VP	= IOMUX_PIN(1, 3,  5,  1, 1, 28),
	MXC91231_PIN_SP_USB_SE0_VM	= IOMUX_PIN(1, 3,  6,  1, 2, 28),
	MXC91231_PIN_SP_USB_RXD		= IOMUX_PIN(1, 3,  7,  1, 3, 28),
	MXC91231_PIN_SP_UH2_TXOE_B	= IOMUX_PIN(1, 3,  8,  2, 0, 28),
	MXC91231_PIN_SP_UH2_SPEED	= IOMUX_PIN(1, 3,  9,  2, 1, 28),
	MXC91231_PIN_SP_UH2_SUSPEN	= IOMUX_PIN(1, 3, 10,  2, 2, 28),
	MXC91231_PIN_SP_UH2_TXDP	= IOMUX_PIN(1, 3, 11,  2, 3, 28),
	MXC91231_PIN_SP_UH2_RXDP	= IOMUX_PIN(1, 3, 12,  3, 0, 28),
	MXC91231_PIN_SP_UH2_RXDM	= IOMUX_PIN(1, 3, 13,  3, 1, 28),
	MXC91231_PIN_SP_UH2_OVR		= IOMUX_PIN(1, 3, 14,  3, 2, 28),
	MXC91231_PIN_SP_UH2_PWR		= IOMUX_PIN(1, 3, 15,  3, 3, 28),
	MXC91231_PIN_SP_SD1_DAT0	= IOMUX_PIN(1, 3, 16,  4, 0, 25),
	MXC91231_PIN_SP_SD1_DAT1	= IOMUX_PIN(1, 3, 17,  4, 1, 25),
	MXC91231_PIN_SP_SD1_DAT2	= IOMUX_PIN(1, 3, 18,  4, 2, 25),
	MXC91231_PIN_SP_SD1_DAT3	= IOMUX_PIN(1, 3, 19,  4, 3, 25),
	MXC91231_PIN_SP_SD1_CMD		= IOMUX_PIN(1, 3, 20,  5, 0, 25),
	MXC91231_PIN_SP_SD1_CLK		= IOMUX_PIN(1, 3, 21,  5, 1, 25),
	MXC91231_PIN_SP_SD2_DAT0	= IOMUX_PIN(1, 3, 22,  5, 2, 26),
	MXC91231_PIN_SP_SD2_DAT1	= IOMUX_PIN(1, 3, 23,  5, 3, 26),
	MXC91231_PIN_SP_SD2_DAT2	= IOMUX_PIN(1, 3, 24,  6, 0, 26),
	MXC91231_PIN_SP_SD2_DAT3	= IOMUX_PIN(1, 3, 25,  6, 1, 26),
	MXC91231_PIN_SP_GPIO_SP_A26	= IOMUX_PIN(1, 3, 26,  6, 2, 28),
	MXC91231_PIN_SP_SPI1_CLK	= IOMUX_PIN(1, 3, 27,  6, 3, 13),
	MXC91231_PIN_SP_SPI1_MOSI	= IOMUX_PIN(1, 3, 28,  7, 0, 13),
	MXC91231_PIN_SP_SPI1_MISO	= IOMUX_PIN(1, 3, 29,  7, 1, 13),
	MXC91231_PIN_SP_SPI1_SS0	= IOMUX_PIN(1, 3, 30,  7, 2, 13),
	MXC91231_PIN_SP_SPI1_SS1	= IOMUX_PIN(1, 3, 31,  7, 3, 13),
	MXC91231_PIN_SP_SD2_CMD		= IOMUX_PIN(1, 7,  0,  8, 0, 26),
	MXC91231_PIN_SP_SD2_CLK		= IOMUX_PIN(1, 7,  0,  8, 1, 26),
	MXC91231_PIN_SP_SIM1_RST_B	= IOMUX_PIN(1, 2, 30,  8, 2, 28),
	MXC91231_PIN_SP_SIM1_SVEN	= IOMUX_PIN(1, 7,  0,  8, 3, 28),
	MXC91231_PIN_SP_SIM1_CLK	= IOMUX_PIN(1, 7,  0,  9, 0, 28),
	MXC91231_PIN_SP_SIM1_TRXD	= IOMUX_PIN(1, 7,  0,  9, 1, 28),
	MXC91231_PIN_SP_SIM1_PD		= IOMUX_PIN(1, 2, 31,  9, 2, 28),
	MXC91231_PIN_SP_UH2_TXDM	= IOMUX_PIN(1, 7,  0,  9, 3, 28),
	MXC91231_PIN_SP_UH2_RXD		= IOMUX_PIN(1, 7,  0, 10, 0, 28),
};

#define PIN_AP_MAX	(104)
#define PIN_SP_MAX	(41)

#define PIN_MAX		(PIN_AP_MAX + PIN_SP_MAX)

/*
 * Convenience values for use with mxc_iomux_mode()
 *
 * Format here is MXC91231_PIN_(pin name)__(function)
 */

#define MXC91231_PIN_SP_USB_DAT_VP__USB_DAT_VP \
	IOMUX_MODE(MXC91231_PIN_SP_USB_DAT_VP, IOMUX_CONFIG_FUNC)
#define MXC91231_PIN_SP_USB_SE0_VM__USB_SE0_VM \
	IOMUX_MODE(MXC91231_PIN_SP_USB_SE0_VM, IOMUX_CONFIG_FUNC)
#define MXC91231_PIN_SP_USB_DAT_VP__RXD2 \
	IOMUX_MODE(MXC91231_PIN_SP_USB_DAT_VP, IOMUX_CONFIG_ALT1)
#define MXC91231_PIN_SP_USB_SE0_VM__TXD2 \
	IOMUX_MODE(MXC91231_PIN_SP_USB_SE0_VM, IOMUX_CONFIG_ALT1)


#endif /* __MACH_IOMUX_MXC91231_H__ */
