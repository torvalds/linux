/*
 * arch/arm/mach-pxa/include/mach/mfp.h
 *
 * Multi-Function Pin Definitions
 *
 * Copyright (C) 2007 Marvell International Ltd.
 *
 * 2007-8-21: eric miao <eric.miao@marvell.com>
 *            initial version
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MFP_H
#define __ASM_ARCH_MFP_H

#define mfp_to_gpio(m)	((m) % 128)

/* list of all the configurable MFP pins */
enum {
	MFP_PIN_INVALID = -1,

	MFP_PIN_GPIO0 = 0,
	MFP_PIN_GPIO1,
	MFP_PIN_GPIO2,
	MFP_PIN_GPIO3,
	MFP_PIN_GPIO4,
	MFP_PIN_GPIO5,
	MFP_PIN_GPIO6,
	MFP_PIN_GPIO7,
	MFP_PIN_GPIO8,
	MFP_PIN_GPIO9,
	MFP_PIN_GPIO10,
	MFP_PIN_GPIO11,
	MFP_PIN_GPIO12,
	MFP_PIN_GPIO13,
	MFP_PIN_GPIO14,
	MFP_PIN_GPIO15,
	MFP_PIN_GPIO16,
	MFP_PIN_GPIO17,
	MFP_PIN_GPIO18,
	MFP_PIN_GPIO19,
	MFP_PIN_GPIO20,
	MFP_PIN_GPIO21,
	MFP_PIN_GPIO22,
	MFP_PIN_GPIO23,
	MFP_PIN_GPIO24,
	MFP_PIN_GPIO25,
	MFP_PIN_GPIO26,
	MFP_PIN_GPIO27,
	MFP_PIN_GPIO28,
	MFP_PIN_GPIO29,
	MFP_PIN_GPIO30,
	MFP_PIN_GPIO31,
	MFP_PIN_GPIO32,
	MFP_PIN_GPIO33,
	MFP_PIN_GPIO34,
	MFP_PIN_GPIO35,
	MFP_PIN_GPIO36,
	MFP_PIN_GPIO37,
	MFP_PIN_GPIO38,
	MFP_PIN_GPIO39,
	MFP_PIN_GPIO40,
	MFP_PIN_GPIO41,
	MFP_PIN_GPIO42,
	MFP_PIN_GPIO43,
	MFP_PIN_GPIO44,
	MFP_PIN_GPIO45,
	MFP_PIN_GPIO46,
	MFP_PIN_GPIO47,
	MFP_PIN_GPIO48,
	MFP_PIN_GPIO49,
	MFP_PIN_GPIO50,
	MFP_PIN_GPIO51,
	MFP_PIN_GPIO52,
	MFP_PIN_GPIO53,
	MFP_PIN_GPIO54,
	MFP_PIN_GPIO55,
	MFP_PIN_GPIO56,
	MFP_PIN_GPIO57,
	MFP_PIN_GPIO58,
	MFP_PIN_GPIO59,
	MFP_PIN_GPIO60,
	MFP_PIN_GPIO61,
	MFP_PIN_GPIO62,
	MFP_PIN_GPIO63,
	MFP_PIN_GPIO64,
	MFP_PIN_GPIO65,
	MFP_PIN_GPIO66,
	MFP_PIN_GPIO67,
	MFP_PIN_GPIO68,
	MFP_PIN_GPIO69,
	MFP_PIN_GPIO70,
	MFP_PIN_GPIO71,
	MFP_PIN_GPIO72,
	MFP_PIN_GPIO73,
	MFP_PIN_GPIO74,
	MFP_PIN_GPIO75,
	MFP_PIN_GPIO76,
	MFP_PIN_GPIO77,
	MFP_PIN_GPIO78,
	MFP_PIN_GPIO79,
	MFP_PIN_GPIO80,
	MFP_PIN_GPIO81,
	MFP_PIN_GPIO82,
	MFP_PIN_GPIO83,
	MFP_PIN_GPIO84,
	MFP_PIN_GPIO85,
	MFP_PIN_GPIO86,
	MFP_PIN_GPIO87,
	MFP_PIN_GPIO88,
	MFP_PIN_GPIO89,
	MFP_PIN_GPIO90,
	MFP_PIN_GPIO91,
	MFP_PIN_GPIO92,
	MFP_PIN_GPIO93,
	MFP_PIN_GPIO94,
	MFP_PIN_GPIO95,
	MFP_PIN_GPIO96,
	MFP_PIN_GPIO97,
	MFP_PIN_GPIO98,
	MFP_PIN_GPIO99,
	MFP_PIN_GPIO100,
	MFP_PIN_GPIO101,
	MFP_PIN_GPIO102,
	MFP_PIN_GPIO103,
	MFP_PIN_GPIO104,
	MFP_PIN_GPIO105,
	MFP_PIN_GPIO106,
	MFP_PIN_GPIO107,
	MFP_PIN_GPIO108,
	MFP_PIN_GPIO109,
	MFP_PIN_GPIO110,
	MFP_PIN_GPIO111,
	MFP_PIN_GPIO112,
	MFP_PIN_GPIO113,
	MFP_PIN_GPIO114,
	MFP_PIN_GPIO115,
	MFP_PIN_GPIO116,
	MFP_PIN_GPIO117,
	MFP_PIN_GPIO118,
	MFP_PIN_GPIO119,
	MFP_PIN_GPIO120,
	MFP_PIN_GPIO121,
	MFP_PIN_GPIO122,
	MFP_PIN_GPIO123,
	MFP_PIN_GPIO124,
	MFP_PIN_GPIO125,
	MFP_PIN_GPIO126,
	MFP_PIN_GPIO127,
	MFP_PIN_GPIO0_2,
	MFP_PIN_GPIO1_2,
	MFP_PIN_GPIO2_2,
	MFP_PIN_GPIO3_2,
	MFP_PIN_GPIO4_2,
	MFP_PIN_GPIO5_2,
	MFP_PIN_GPIO6_2,
	MFP_PIN_GPIO7_2,
	MFP_PIN_GPIO8_2,
	MFP_PIN_GPIO9_2,
	MFP_PIN_GPIO10_2,
	MFP_PIN_GPIO11_2,
	MFP_PIN_GPIO12_2,
	MFP_PIN_GPIO13_2,
	MFP_PIN_GPIO14_2,
	MFP_PIN_GPIO15_2,
	MFP_PIN_GPIO16_2,
	MFP_PIN_GPIO17_2,

	MFP_PIN_ULPI_STP,
	MFP_PIN_ULPI_NXT,
	MFP_PIN_ULPI_DIR,

	MFP_PIN_nXCVREN,
	MFP_PIN_DF_CLE_nOE,
	MFP_PIN_DF_nADV1_ALE,
	MFP_PIN_DF_SCLK_E,
	MFP_PIN_DF_SCLK_S,
	MFP_PIN_nBE0,
	MFP_PIN_nBE1,
	MFP_PIN_DF_nADV2_ALE,
	MFP_PIN_DF_INT_RnB,
	MFP_PIN_DF_nCS0,
	MFP_PIN_DF_nCS1,
	MFP_PIN_nLUA,
	MFP_PIN_nLLA,
	MFP_PIN_DF_nWE,
	MFP_PIN_DF_ALE_nWE,
	MFP_PIN_DF_nRE_nOE,
	MFP_PIN_DF_ADDR0,
	MFP_PIN_DF_ADDR1,
	MFP_PIN_DF_ADDR2,
	MFP_PIN_DF_ADDR3,
	MFP_PIN_DF_IO0,
	MFP_PIN_DF_IO1,
	MFP_PIN_DF_IO2,
	MFP_PIN_DF_IO3,
	MFP_PIN_DF_IO4,
	MFP_PIN_DF_IO5,
	MFP_PIN_DF_IO6,
	MFP_PIN_DF_IO7,
	MFP_PIN_DF_IO8,
	MFP_PIN_DF_IO9,
	MFP_PIN_DF_IO10,
	MFP_PIN_DF_IO11,
	MFP_PIN_DF_IO12,
	MFP_PIN_DF_IO13,
	MFP_PIN_DF_IO14,
	MFP_PIN_DF_IO15,

	/* additional pins on PXA930 */
	MFP_PIN_GSIM_UIO,
	MFP_PIN_GSIM_UCLK,
	MFP_PIN_GSIM_UDET,
	MFP_PIN_GSIM_nURST,
	MFP_PIN_PMIC_INT,
	MFP_PIN_RDY,

	MFP_PIN_MAX,
};

/*
 * a possible MFP configuration is represented by a 32-bit integer
 *
 * bit  0.. 9 - MFP Pin Number (1024 Pins Maximum)
 * bit 10..12 - Alternate Function Selection
 * bit 13..15 - Drive Strength
 * bit 16..18 - Low Power Mode State
 * bit 19..20 - Low Power Mode Edge Detection
 * bit 21..22 - Run Mode Pull State
 *
 * to facilitate the definition, the following macros are provided
 *
 * MFP_CFG_DEFAULT - default MFP configuration value, with
 * 		  alternate function = 0,
 * 		  drive strength = fast 3mA (MFP_DS03X)
 * 		  low power mode = default
 * 		  edge detection = none
 *
 * MFP_CFG	- default MFPR value with alternate function
 * MFP_CFG_DRV	- default MFPR value with alternate function and
 * 		  pin drive strength
 * MFP_CFG_LPM	- default MFPR value with alternate function and
 * 		  low power mode
 * MFP_CFG_X	- default MFPR value with alternate function,
 * 		  pin drive strength and low power mode
 */

typedef unsigned long mfp_cfg_t;

#define MFP_PIN(x)		((x) & 0x3ff)

#define MFP_AF0			(0x0 << 10)
#define MFP_AF1			(0x1 << 10)
#define MFP_AF2			(0x2 << 10)
#define MFP_AF3			(0x3 << 10)
#define MFP_AF4			(0x4 << 10)
#define MFP_AF5			(0x5 << 10)
#define MFP_AF6			(0x6 << 10)
#define MFP_AF7			(0x7 << 10)
#define MFP_AF_MASK		(0x7 << 10)
#define MFP_AF(x)		(((x) >> 10) & 0x7)

#define MFP_DS01X		(0x0 << 13)
#define MFP_DS02X		(0x1 << 13)
#define MFP_DS03X		(0x2 << 13)
#define MFP_DS04X		(0x3 << 13)
#define MFP_DS06X		(0x4 << 13)
#define MFP_DS08X		(0x5 << 13)
#define MFP_DS10X		(0x6 << 13)
#define MFP_DS13X		(0x7 << 13)
#define MFP_DS_MASK		(0x7 << 13)
#define MFP_DS(x)		(((x) >> 13) & 0x7)

#define MFP_LPM_DEFAULT		(0x0 << 16)
#define MFP_LPM_DRIVE_LOW	(0x1 << 16)
#define MFP_LPM_DRIVE_HIGH	(0x2 << 16)
#define MFP_LPM_PULL_LOW	(0x3 << 16)
#define MFP_LPM_PULL_HIGH	(0x4 << 16)
#define MFP_LPM_FLOAT		(0x5 << 16)
#define MFP_LPM_INPUT		(0x6 << 16)
#define MFP_LPM_STATE_MASK	(0x7 << 16)
#define MFP_LPM_STATE(x)	(((x) >> 16) & 0x7)

#define MFP_LPM_EDGE_NONE	(0x0 << 19)
#define MFP_LPM_EDGE_RISE	(0x1 << 19)
#define MFP_LPM_EDGE_FALL	(0x2 << 19)
#define MFP_LPM_EDGE_BOTH	(0x3 << 19)
#define MFP_LPM_EDGE_MASK	(0x3 << 19)
#define MFP_LPM_EDGE(x)		(((x) >> 19) & 0x3)

#define MFP_PULL_NONE		(0x0 << 21)
#define MFP_PULL_LOW		(0x1 << 21)
#define MFP_PULL_HIGH		(0x2 << 21)
#define MFP_PULL_BOTH		(0x3 << 21)
#define MFP_PULL_MASK		(0x3 << 21)
#define MFP_PULL(x)		(((x) >> 21) & 0x3)

#define MFP_CFG_DEFAULT		(MFP_AF0 | MFP_DS03X | MFP_LPM_DEFAULT |\
				 MFP_LPM_EDGE_NONE | MFP_PULL_NONE)

#define MFP_CFG(pin, af)		\
	((MFP_CFG_DEFAULT & ~MFP_AF_MASK) |\
	 (MFP_PIN(MFP_PIN_##pin) | MFP_##af))

#define MFP_CFG_DRV(pin, af, drv)	\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DS_MASK)) |\
	 (MFP_PIN(MFP_PIN_##pin) | MFP_##af | MFP_##drv))

#define MFP_CFG_LPM(pin, af, lpm)	\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_LPM_STATE_MASK)) |\
	 (MFP_PIN(MFP_PIN_##pin) | MFP_##af | MFP_LPM_##lpm))

#define MFP_CFG_X(pin, af, drv, lpm)	\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DS_MASK | MFP_LPM_STATE_MASK)) |\
	 (MFP_PIN(MFP_PIN_##pin) | MFP_##af | MFP_##drv | MFP_LPM_##lpm))

#endif /* __ASM_ARCH_MFP_H */
