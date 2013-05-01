/*
 *  linux/drivers/pinctrl/pinmux-pxa910.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 *
 *  Copyright (C) 2011, Marvell Technology Group Ltd.
 *
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include "pinctrl-pxa3xx.h"

#define PXA910_DS_MASK		0x1800
#define PXA910_DS_SHIFT		11
#define PXA910_SLEEP_MASK	0x38
#define PXA910_SLEEP_SELECT	(1 << 9)
#define PXA910_SLEEP_DATA	(1 << 8)
#define PXA910_SLEEP_DIR	(1 << 7)

#define MFPR_910(a, r, f0, f1, f2, f3, f4, f5, f6, f7)		\
	{							\
		.name = #a,					\
		.pin = a,					\
		.mfpr = r,					\
		.func = {					\
			PXA910_MUX_##f0,			\
			PXA910_MUX_##f1,			\
			PXA910_MUX_##f2,			\
			PXA910_MUX_##f3,			\
			PXA910_MUX_##f4,			\
			PXA910_MUX_##f5,			\
			PXA910_MUX_##f6,			\
			PXA910_MUX_##f7,			\
		},						\
	}

#define GRP_910(a, m, p)		\
	{ .name = a, .mux = PXA910_MUX_##m, .pins = p, .npins = ARRAY_SIZE(p), }

/* 170 pins */
enum pxa910_pin_list {
	/* 0~127: GPIO0~GPIO127 */
	ND_IO15 = 128,
	ND_IO14,
	ND_IO13, /* 130 */
	ND_IO12,
	ND_IO11,
	ND_IO10,
	ND_IO9,
	ND_IO8,
	ND_IO7,
	ND_IO6,
	ND_IO5,
	ND_IO4,
	ND_IO3, /* 140 */
	ND_IO2,
	ND_IO1,
	ND_IO0,
	ND_NCS0,
	ND_NCS1,
	SM_NCS0,
	SM_NCS1,
	ND_NWE,
	ND_NRE,
	ND_CLE, /* 150 */
	ND_ALE,
	SM_SCLK,
	ND_RDY0,
	SM_ADV,
	ND_RDY1,
	SM_ADVMUX,
	SM_RDY,
	MMC1_DAT7,
	MMC1_DAT6,
	MMC1_DAT5, /* 160 */
	MMC1_DAT4,
	MMC1_DAT3,
	MMC1_DAT2,
	MMC1_DAT1,
	MMC1_DAT0,
	MMC1_CMD,
	MMC1_CLK,
	MMC1_CD,
	VCXO_OUT,
};

enum pxa910_mux {
	/* PXA3xx_MUX_GPIO = 0 (predefined in pinctrl-pxa3xx.h) */
	PXA910_MUX_GPIO = 0,
	PXA910_MUX_NAND,
	PXA910_MUX_USIM2,
	PXA910_MUX_EXT_DMA,
	PXA910_MUX_EXT_INT,
	PXA910_MUX_MMC1,
	PXA910_MUX_MMC2,
	PXA910_MUX_MMC3,
	PXA910_MUX_SM_INT,
	PXA910_MUX_PRI_JTAG,
	PXA910_MUX_SEC1_JTAG,
	PXA910_MUX_SEC2_JTAG,
	PXA910_MUX_RESET,	/* SLAVE RESET OUT */
	PXA910_MUX_CLK_REQ,
	PXA910_MUX_VCXO_REQ,
	PXA910_MUX_VCXO_OUT,
	PXA910_MUX_VCXO_REQ2,
	PXA910_MUX_VCXO_OUT2,
	PXA910_MUX_SPI,
	PXA910_MUX_SPI2,
	PXA910_MUX_GSSP,
	PXA910_MUX_SSP0,
	PXA910_MUX_SSP1,
	PXA910_MUX_SSP2,
	PXA910_MUX_DSSP2,
	PXA910_MUX_DSSP3,
	PXA910_MUX_UART0,
	PXA910_MUX_UART1,
	PXA910_MUX_UART2,
	PXA910_MUX_TWSI,
	PXA910_MUX_CCIC,
	PXA910_MUX_PWM0,
	PXA910_MUX_PWM1,
	PXA910_MUX_PWM2,
	PXA910_MUX_PWM3,
	PXA910_MUX_HSL,
	PXA910_MUX_ONE_WIRE,
	PXA910_MUX_LCD,
	PXA910_MUX_DAC_ST23,
	PXA910_MUX_ULPI,
	PXA910_MUX_TB,
	PXA910_MUX_KP_MK,
	PXA910_MUX_KP_DK,
	PXA910_MUX_TCU_GPOA,
	PXA910_MUX_TCU_GPOB,
	PXA910_MUX_ROT,
	PXA910_MUX_TDS,
	PXA910_MUX_32K_CLK, /* 32KHz CLK OUT */
	PXA910_MUX_MN_CLK, /* MN CLK OUT */
	PXA910_MUX_SMC,
	PXA910_MUX_SM_ADDR18,
	PXA910_MUX_SM_ADDR19,
	PXA910_MUX_SM_ADDR20,
	PXA910_MUX_NONE = 0xffff,
};


static struct pinctrl_pin_desc pxa910_pads[] = {
	PINCTRL_PIN(GPIO0, "GPIO0"),
	PINCTRL_PIN(GPIO1, "GPIO1"),
	PINCTRL_PIN(GPIO2, "GPIO2"),
	PINCTRL_PIN(GPIO3, "GPIO3"),
	PINCTRL_PIN(GPIO4, "GPIO4"),
	PINCTRL_PIN(GPIO5, "GPIO5"),
	PINCTRL_PIN(GPIO6, "GPIO6"),
	PINCTRL_PIN(GPIO7, "GPIO7"),
	PINCTRL_PIN(GPIO8, "GPIO8"),
	PINCTRL_PIN(GPIO9, "GPIO9"),
	PINCTRL_PIN(GPIO10, "GPIO10"),
	PINCTRL_PIN(GPIO11, "GPIO11"),
	PINCTRL_PIN(GPIO12, "GPIO12"),
	PINCTRL_PIN(GPIO13, "GPIO13"),
	PINCTRL_PIN(GPIO14, "GPIO14"),
	PINCTRL_PIN(GPIO15, "GPIO15"),
	PINCTRL_PIN(GPIO16, "GPIO16"),
	PINCTRL_PIN(GPIO17, "GPIO17"),
	PINCTRL_PIN(GPIO18, "GPIO18"),
	PINCTRL_PIN(GPIO19, "GPIO19"),
	PINCTRL_PIN(GPIO20, "GPIO20"),
	PINCTRL_PIN(GPIO21, "GPIO21"),
	PINCTRL_PIN(GPIO22, "GPIO22"),
	PINCTRL_PIN(GPIO23, "GPIO23"),
	PINCTRL_PIN(GPIO24, "GPIO24"),
	PINCTRL_PIN(GPIO25, "GPIO25"),
	PINCTRL_PIN(GPIO26, "GPIO26"),
	PINCTRL_PIN(GPIO27, "GPIO27"),
	PINCTRL_PIN(GPIO28, "GPIO28"),
	PINCTRL_PIN(GPIO29, "GPIO29"),
	PINCTRL_PIN(GPIO30, "GPIO30"),
	PINCTRL_PIN(GPIO31, "GPIO31"),
	PINCTRL_PIN(GPIO32, "GPIO32"),
	PINCTRL_PIN(GPIO33, "GPIO33"),
	PINCTRL_PIN(GPIO34, "GPIO34"),
	PINCTRL_PIN(GPIO35, "GPIO35"),
	PINCTRL_PIN(GPIO36, "GPIO36"),
	PINCTRL_PIN(GPIO37, "GPIO37"),
	PINCTRL_PIN(GPIO38, "GPIO38"),
	PINCTRL_PIN(GPIO39, "GPIO39"),
	PINCTRL_PIN(GPIO40, "GPIO40"),
	PINCTRL_PIN(GPIO41, "GPIO41"),
	PINCTRL_PIN(GPIO42, "GPIO42"),
	PINCTRL_PIN(GPIO43, "GPIO43"),
	PINCTRL_PIN(GPIO44, "GPIO44"),
	PINCTRL_PIN(GPIO45, "GPIO45"),
	PINCTRL_PIN(GPIO46, "GPIO46"),
	PINCTRL_PIN(GPIO47, "GPIO47"),
	PINCTRL_PIN(GPIO48, "GPIO48"),
	PINCTRL_PIN(GPIO49, "GPIO49"),
	PINCTRL_PIN(GPIO50, "GPIO50"),
	PINCTRL_PIN(GPIO51, "GPIO51"),
	PINCTRL_PIN(GPIO52, "GPIO52"),
	PINCTRL_PIN(GPIO53, "GPIO53"),
	PINCTRL_PIN(GPIO54, "GPIO54"),
	PINCTRL_PIN(GPIO55, "GPIO55"),
	PINCTRL_PIN(GPIO56, "GPIO56"),
	PINCTRL_PIN(GPIO57, "GPIO57"),
	PINCTRL_PIN(GPIO58, "GPIO58"),
	PINCTRL_PIN(GPIO59, "GPIO59"),
	PINCTRL_PIN(GPIO60, "GPIO60"),
	PINCTRL_PIN(GPIO61, "GPIO61"),
	PINCTRL_PIN(GPIO62, "GPIO62"),
	PINCTRL_PIN(GPIO63, "GPIO63"),
	PINCTRL_PIN(GPIO64, "GPIO64"),
	PINCTRL_PIN(GPIO65, "GPIO65"),
	PINCTRL_PIN(GPIO66, "GPIO66"),
	PINCTRL_PIN(GPIO67, "GPIO67"),
	PINCTRL_PIN(GPIO68, "GPIO68"),
	PINCTRL_PIN(GPIO69, "GPIO69"),
	PINCTRL_PIN(GPIO70, "GPIO70"),
	PINCTRL_PIN(GPIO71, "GPIO71"),
	PINCTRL_PIN(GPIO72, "GPIO72"),
	PINCTRL_PIN(GPIO73, "GPIO73"),
	PINCTRL_PIN(GPIO74, "GPIO74"),
	PINCTRL_PIN(GPIO75, "GPIO75"),
	PINCTRL_PIN(GPIO76, "GPIO76"),
	PINCTRL_PIN(GPIO77, "GPIO77"),
	PINCTRL_PIN(GPIO78, "GPIO78"),
	PINCTRL_PIN(GPIO79, "GPIO79"),
	PINCTRL_PIN(GPIO80, "GPIO80"),
	PINCTRL_PIN(GPIO81, "GPIO81"),
	PINCTRL_PIN(GPIO82, "GPIO82"),
	PINCTRL_PIN(GPIO83, "GPIO83"),
	PINCTRL_PIN(GPIO84, "GPIO84"),
	PINCTRL_PIN(GPIO85, "GPIO85"),
	PINCTRL_PIN(GPIO86, "GPIO86"),
	PINCTRL_PIN(GPIO87, "GPIO87"),
	PINCTRL_PIN(GPIO88, "GPIO88"),
	PINCTRL_PIN(GPIO89, "GPIO89"),
	PINCTRL_PIN(GPIO90, "GPIO90"),
	PINCTRL_PIN(GPIO91, "GPIO91"),
	PINCTRL_PIN(GPIO92, "GPIO92"),
	PINCTRL_PIN(GPIO93, "GPIO93"),
	PINCTRL_PIN(GPIO94, "GPIO94"),
	PINCTRL_PIN(GPIO95, "GPIO95"),
	PINCTRL_PIN(GPIO96, "GPIO96"),
	PINCTRL_PIN(GPIO97, "GPIO97"),
	PINCTRL_PIN(GPIO98, "GPIO98"),
	PINCTRL_PIN(GPIO99, "GPIO99"),
	PINCTRL_PIN(GPIO100, "GPIO100"),
	PINCTRL_PIN(GPIO101, "GPIO101"),
	PINCTRL_PIN(GPIO102, "GPIO102"),
	PINCTRL_PIN(GPIO103, "GPIO103"),
	PINCTRL_PIN(GPIO104, "GPIO104"),
	PINCTRL_PIN(GPIO105, "GPIO105"),
	PINCTRL_PIN(GPIO106, "GPIO106"),
	PINCTRL_PIN(GPIO107, "GPIO107"),
	PINCTRL_PIN(GPIO108, "GPIO108"),
	PINCTRL_PIN(GPIO109, "GPIO109"),
	PINCTRL_PIN(GPIO110, "GPIO110"),
	PINCTRL_PIN(GPIO111, "GPIO111"),
	PINCTRL_PIN(GPIO112, "GPIO112"),
	PINCTRL_PIN(GPIO113, "GPIO113"),
	PINCTRL_PIN(GPIO114, "GPIO114"),
	PINCTRL_PIN(GPIO115, "GPIO115"),
	PINCTRL_PIN(GPIO116, "GPIO116"),
	PINCTRL_PIN(GPIO117, "GPIO117"),
	PINCTRL_PIN(GPIO118, "GPIO118"),
	PINCTRL_PIN(GPIO119, "GPIO119"),
	PINCTRL_PIN(GPIO120, "GPIO120"),
	PINCTRL_PIN(GPIO121, "GPIO121"),
	PINCTRL_PIN(GPIO122, "GPIO122"),
	PINCTRL_PIN(GPIO123, "GPIO123"),
	PINCTRL_PIN(GPIO124, "GPIO124"),
	PINCTRL_PIN(GPIO125, "GPIO125"),
	PINCTRL_PIN(GPIO126, "GPIO126"),
	PINCTRL_PIN(GPIO127, "GPIO127"),
	PINCTRL_PIN(ND_IO15, "ND_IO15"),
	PINCTRL_PIN(ND_IO14, "ND_IO14"),
	PINCTRL_PIN(ND_IO13, "ND_IO13"),
	PINCTRL_PIN(ND_IO12, "ND_IO12"),
	PINCTRL_PIN(ND_IO11, "ND_IO11"),
	PINCTRL_PIN(ND_IO10, "ND_IO10"),
	PINCTRL_PIN(ND_IO9, "ND_IO9"),
	PINCTRL_PIN(ND_IO8, "ND_IO8"),
	PINCTRL_PIN(ND_IO7, "ND_IO7"),
	PINCTRL_PIN(ND_IO6, "ND_IO6"),
	PINCTRL_PIN(ND_IO5, "ND_IO5"),
	PINCTRL_PIN(ND_IO4, "ND_IO4"),
	PINCTRL_PIN(ND_IO3, "ND_IO3"),
	PINCTRL_PIN(ND_IO2, "ND_IO2"),
	PINCTRL_PIN(ND_IO1, "ND_IO1"),
	PINCTRL_PIN(ND_IO0, "ND_IO0"),
	PINCTRL_PIN(ND_NCS0, "ND_NCS0_SM_NCS2"),
	PINCTRL_PIN(ND_NCS1, "ND_NCS1_SM_NCS3"),
	PINCTRL_PIN(SM_NCS0, "SM_NCS0"),
	PINCTRL_PIN(SM_NCS1, "SM_NCS1"),
	PINCTRL_PIN(ND_NWE, "ND_NWE"),
	PINCTRL_PIN(ND_NRE, "ND_NRE"),
	PINCTRL_PIN(ND_CLE, "ND_CLE_SM_NOE"),
	PINCTRL_PIN(ND_ALE, "ND_ALE_SM_NWE"),
	PINCTRL_PIN(SM_SCLK, "SM_SCLK"),
	PINCTRL_PIN(ND_RDY0, "ND_RDY0"),
	PINCTRL_PIN(SM_ADV, "SM_ADV"),
	PINCTRL_PIN(ND_RDY1, "ND_RDY1"),
	PINCTRL_PIN(SM_RDY, "SM_RDY"),
	PINCTRL_PIN(MMC1_DAT7, "MMC1_DAT7"),
	PINCTRL_PIN(MMC1_DAT6, "MMC1_DAT6"),
	PINCTRL_PIN(MMC1_DAT5, "MMC1_DAT5"),
	PINCTRL_PIN(MMC1_DAT4, "MMC1_DAT4"),
	PINCTRL_PIN(MMC1_DAT3, "MMC1_DAT3"),
	PINCTRL_PIN(MMC1_DAT2, "MMC1_DAT2"),
	PINCTRL_PIN(MMC1_DAT1, "MMC1_DAT1"),
	PINCTRL_PIN(MMC1_DAT0, "MMC1_DAT0"),
	PINCTRL_PIN(MMC1_CMD, "MMC1 CMD"),
	PINCTRL_PIN(MMC1_CLK, "MMC1 CLK"),
	PINCTRL_PIN(MMC1_CD, "MMC1 CD"),
	PINCTRL_PIN(VCXO_OUT, "VCXO_OUT"),
};

struct pxa3xx_mfp_pin pxa910_mfp[] = {
	/*       pin        offs   f0        f1      f2         f3         f4         f5        f6        f7  */
	MFPR_910(GPIO0,     0x0DC, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO1,     0x0E0, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO2,     0x0E4, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO3,     0x0E8, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO4,     0x0EC, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO5,     0x0F0, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO6,     0x0F4, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO7,     0x0F8, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO8,     0x0FC, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO9,     0x100, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO10,    0x104, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO11,    0x108, GPIO,     KP_MK,  NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO12,    0x10C, GPIO,     KP_MK,  NONE,      NONE,      KP_DK,     NONE,     NONE,     NONE),
	MFPR_910(GPIO13,    0x110, GPIO,     KP_MK,  NONE,      NONE,      KP_DK,     NONE,     NONE,     NONE),
	MFPR_910(GPIO14,    0x114, GPIO,     KP_MK,  NONE,      NONE,      KP_DK,     TB,       NONE,     NONE),
	MFPR_910(GPIO15,    0x118, GPIO,     KP_MK,  NONE,      NONE,      KP_DK,     TB,       NONE,     NONE),
	MFPR_910(GPIO16,    0x11C, GPIO,     KP_DK,  NONE,      NONE,      NONE,      TB,       NONE,     NONE),
	MFPR_910(GPIO17,    0x120, GPIO,     KP_DK,  NONE,      NONE,      NONE,      TB,       NONE,     NONE),
	MFPR_910(GPIO18,    0x124, GPIO,     KP_DK,  NONE,      NONE,      ROT,       NONE,     NONE,     NONE),
	MFPR_910(GPIO19,    0x128, GPIO,     KP_DK,  NONE,      NONE,      ROT,       NONE,     NONE,     NONE),
	MFPR_910(GPIO20,    0x12C, GPIO,     SSP1,   NONE,      NONE,      VCXO_OUT,  NONE,     NONE,     NONE),
	MFPR_910(GPIO21,    0x130, GPIO,     SSP1,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO22,    0x134, GPIO,     SSP1,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO23,    0x138, GPIO,     SSP1,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO24,    0x13C, GPIO,     SSP1,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO25,    0x140, GPIO,     GSSP,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO26,    0x144, GPIO,     GSSP,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO27,    0x148, GPIO,     GSSP,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO28,    0x14C, GPIO,     GSSP,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO29,    0x150, GPIO,     UART0,  NONE,      NONE,      UART1,     NONE,     NONE,     NONE),
	MFPR_910(GPIO30,    0x154, GPIO,     UART0,  NONE,      NONE,      UART1,     NONE,     NONE,     NONE),
	MFPR_910(GPIO31,    0x158, GPIO,     UART0,  NONE,      NONE,      UART1,     NONE,     NONE,     NONE),
	MFPR_910(GPIO32,    0x15C, GPIO,     UART0,  DAC_ST23,  NONE,      UART1,     NONE,     NONE,     NONE),
	MFPR_910(GPIO33,    0x160, GPIO,     MMC2,   SSP0,      SSP2,      NONE,      SPI,      NONE,     MMC3),
	MFPR_910(GPIO34,    0x164, GPIO,     MMC2,   SSP0,      SSP2,      NONE,      SPI,      NONE,     MMC3),
	MFPR_910(GPIO35,    0x168, GPIO,     MMC2,   SSP0,      SSP2,      NONE,      SPI,      NONE,     MMC3),
	MFPR_910(GPIO36,    0x16C, GPIO,     MMC2,   SSP0,      SSP2,      NONE,      SPI,      NONE,     MMC3),
	MFPR_910(GPIO37,    0x170, GPIO,     MMC2,   NONE,      NONE,      NONE,      SPI,      HSL,      NONE),
	MFPR_910(GPIO38,    0x174, GPIO,     MMC2,   NONE,      NONE,      NONE,      NONE,     HSL,      NONE),
	MFPR_910(GPIO39,    0x178, GPIO,     MMC2,   NONE,      NONE,      NONE,      NONE,     HSL,      NONE),
	MFPR_910(GPIO40,    0x17C, GPIO,     MMC2,   NONE,      NONE,      NONE,      NONE,     HSL,      NONE),
	MFPR_910(GPIO41,    0x180, GPIO,     MMC2,   NONE,      NONE,      NONE,      NONE,     HSL,      NONE),
	MFPR_910(GPIO42,    0x184, GPIO,     MMC2,   NONE,      NONE,      NONE,      NONE,     HSL,      NONE),
	MFPR_910(GPIO43,    0x188, GPIO,     UART1,  NONE,      DAC_ST23,  NONE,      DSSP2,    SPI,      UART2),
	MFPR_910(GPIO44,    0x18C, GPIO,     UART1,  NONE,      EXT_INT,   NONE,      DSSP2,    SPI,      UART2),
	MFPR_910(GPIO45,    0x190, GPIO,     UART1,  NONE,      EXT_INT,   NONE,      DSSP2,    SPI,      UART2),
	MFPR_910(GPIO46,    0x194, GPIO,     UART1,  NONE,      EXT_INT,   NONE,      DSSP2,    SPI,      UART2),
	MFPR_910(GPIO47,    0x198, GPIO,     SSP0,   NONE,      NONE,      NONE,      SSP2,     UART1,    NONE),
	MFPR_910(GPIO48,    0x19C, GPIO,     SSP0,   NONE,      NONE,      NONE,      SSP2,     UART1,    NONE),
	MFPR_910(GPIO49,    0x1A0, GPIO,     SSP0,   UART0,     VCXO_REQ,  NONE,      SSP2,     NONE,     MMC3),
	MFPR_910(GPIO50,    0x1A4, GPIO,     SSP0,   UART0,     VCXO_OUT,  NONE,      SSP2,     NONE,     MMC3),
	MFPR_910(GPIO51,    0x1A8, GPIO,     UART2,  PWM1,      TWSI,      SSP0,      NONE,     DSSP3,    NONE),
	MFPR_910(GPIO52,    0x1AC, GPIO,     UART2,  DAC_ST23,  TWSI,      SSP0,      NONE,     DSSP3,    NONE),
	MFPR_910(GPIO53,    0x1B0, GPIO,     UART2,  TWSI,      NONE,      SSP0,      NONE,     DSSP3,    NONE),
	MFPR_910(GPIO54,    0x1B4, GPIO,     UART2,  TWSI,      SSP0,      NONE,      NONE,     DSSP3,    NONE),
	MFPR_910(GPIO55,    0x2F0, TDS,      GPIO,   TB,        NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO56,    0x2F4, TDS,      GPIO,   TB,        NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO57,    0x2F8, TDS,      GPIO,   TB,        NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO58,    0x2FC, TDS,      GPIO,   TB,        NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO59,    0x300, TDS,      GPIO,   TCU_GPOA,  TCU_GPOB,  ONE_WIRE,  NONE,     NONE,     NONE),
	MFPR_910(GPIO60,    0x304, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO61,    0x308, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     HSL),
	MFPR_910(GPIO62,    0x30C, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     HSL),
	MFPR_910(GPIO63,    0x310, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     HSL),
	MFPR_910(GPIO64,    0x314, GPIO,     SPI2,   NONE,      NONE,      NONE,      NONE,     NONE,     HSL),
	MFPR_910(GPIO65,    0x318, GPIO,     SPI2,   NONE,      NONE,      NONE,      NONE,     ONE_WIRE, HSL),
	MFPR_910(GPIO66,    0x31C, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     HSL),
	MFPR_910(GPIO67,    0x1B8, GPIO,     CCIC,   SPI,       NONE,      NONE,      ULPI,     NONE,     USIM2),
	MFPR_910(GPIO68,    0x1BC, GPIO,     CCIC,   SPI,       NONE,      NONE,      ULPI,     NONE,     USIM2),
	MFPR_910(GPIO69,    0x1C0, GPIO,     CCIC,   SPI,       NONE,      NONE,      ULPI,     NONE,     USIM2),
	MFPR_910(GPIO70,    0x1C4, GPIO,     CCIC,   SPI,       NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO71,    0x1C8, GPIO,     CCIC,   SPI,       NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO72,    0x1CC, GPIO,     CCIC,   EXT_DMA,   NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO73,    0x1D0, GPIO,     CCIC,   EXT_DMA,   NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO74,    0x1D4, GPIO,     CCIC,   EXT_DMA,   NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO75,    0x1D8, GPIO,     CCIC,   NONE,      NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO76,    0x1DC, GPIO,     CCIC,   NONE,      NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO77,    0x1E0, GPIO,     CCIC,   NONE,      NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO78,    0x1E4, GPIO,     CCIC,   NONE,      NONE,      NONE,      ULPI,     NONE,     NONE),
	MFPR_910(GPIO79,    0x1E8, GPIO,     TWSI,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO80,    0x1EC, GPIO,     TWSI,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO81,    0x1F0, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO82,    0x1F4, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO83,    0x1F8, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO84,    0x1FC, GPIO,     LCD,    VCXO_REQ2, NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO85,    0x200, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO86,    0x204, GPIO,     LCD,    VCXO_OUT2, NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO87,    0x208, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO88,    0x20C, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO89,    0x210, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO90,    0x214, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO91,    0x218, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO92,    0x21C, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO93,    0x220, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO94,    0x224, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO95,    0x228, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO96,    0x22C, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO97,    0x230, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO98,    0x234, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO99,    0x0B0, MMC1,     GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO100,   0x238, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO101,   0x23C, GPIO,     LCD,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO102,   0x240, GPIO,     LCD,    DSSP2,     SPI,       NONE,      NONE,     NONE,     SPI2),
	MFPR_910(GPIO103,   0x244, GPIO,     LCD,    DSSP2,     SPI,       NONE,      NONE,     NONE,     SPI2),
	MFPR_910(GPIO104,   0x248, GPIO,     LCD,    DSSP2,     SPI,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO105,   0x24C, GPIO,     LCD,    DSSP2,     SPI,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO106,   0x250, GPIO,     LCD,    DSSP3,     ONE_WIRE,  NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO107,   0x254, GPIO,     LCD,    DSSP3,     SPI,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO108,   0x258, GPIO,     LCD,    DSSP3,     SPI,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO109,   0x25C, GPIO,     LCD,    DSSP3,     SPI,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO110,   0x298, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO111,   0x29C, GPIO,     NONE,   DSSP2,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO112,   0x2A0, GPIO,     NONE,   DSSP2,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO113,   0x2A4, GPIO,     NONE,   DSSP2,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO114,   0x2A8, GPIO,     NONE,   DSSP3,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO115,   0x2AC, GPIO,     NONE,   DSSP3,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO116,   0x2B0, GPIO,     NONE,   DSSP3,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO117,   0x0B4, PRI_JTAG, GPIO,   PWM0,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO118,   0x0B8, PRI_JTAG, GPIO,   PWM1,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO119,   0x0BC, PRI_JTAG, GPIO,   PWM2,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO120,   0x0C0, PRI_JTAG, GPIO,   PWM3,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO121,   0x32C, GPIO,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO122,   0x0C8, RESET,    GPIO,   32K_CLK,   NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO123,   0x0CC, CLK_REQ,  GPIO,   ONE_WIRE,  EXT_DMA,   NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO124,   0x0D0, GPIO,     MN_CLK, DAC_ST23,  NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO125,   0x0D4, VCXO_REQ, GPIO,   NONE,      EXT_INT,   NONE,      NONE,     NONE,     NONE),
	MFPR_910(GPIO126,   0x06C, GPIO,     SMC,    NONE,      SM_ADDR18, NONE,      EXT_DMA,  NONE,     NONE),
	MFPR_910(GPIO127,   0x070, GPIO,     SMC,    NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO15,   0x004, NAND,     GPIO,   USIM2,     EXT_DMA,   NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO14,   0x008, NAND,     GPIO,   USIM2,     NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO13,   0x00C, NAND,     GPIO,   USIM2,     EXT_INT,   NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO12,   0x010, NAND,     GPIO,   SSP2,      EXT_INT,   NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO11,   0x014, NAND,     GPIO,   SSP2,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO10,   0x018, NAND,     GPIO,   SSP2,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO9,    0x01C, NAND,     GPIO,   SSP2,      NONE,      VCXO_OUT2, NONE,     NONE,     NONE),
	MFPR_910(ND_IO8,    0x020, NAND,     GPIO,   NONE,      NONE,      PWM3,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO7,    0x024, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO6,    0x028, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO5,    0x02C, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO4,    0x030, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO3,    0x034, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO2,    0x038, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO1,    0x03C, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_IO0,    0x040, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_NCS0,   0x044, NAND,     GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_NCS1,   0x048, NAND,     GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_NCS0,   0x04C, SMC,      GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_NCS1,   0x050, SMC,      GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_NWE,    0x054, GPIO,     NAND,   NONE,      SM_ADDR20, NONE,      SMC,      NONE,     NONE),
	MFPR_910(ND_NRE,    0x058, GPIO,     NAND,   NONE,      SMC,       NONE,      EXT_DMA,  NONE,     NONE),
	MFPR_910(ND_CLE,    0x05C, NAND,     MMC3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_ALE,    0x060, GPIO,     NAND,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_SCLK,   0x064, MMC3,     NONE,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_RDY0,   0x068, NAND,     GPIO,   NONE,      SMC,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_ADV,    0x074, SMC,      GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(ND_RDY1,   0x078, NAND,     GPIO,   NONE,      SMC,       NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_ADVMUX, 0x07C, SMC,      GPIO,   NONE,      SM_ADDR19, NONE,      NONE,     NONE,     NONE),
	MFPR_910(SM_RDY,    0x080, SMC,      GPIO,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT7, 0x084, MMC1,     GPIO,   SEC1_JTAG, TB,        NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT6, 0x088, MMC1,     GPIO,   SEC1_JTAG, TB,        NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT5, 0x08C, MMC1,     GPIO,   SEC1_JTAG, TB,        NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT4, 0x090, MMC1,     GPIO,   NONE,      TB,        NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT3, 0x094, MMC1,     HSL,    SEC2_JTAG, SSP0,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT2, 0x098, MMC1,     HSL,    SEC2_JTAG, SSP2,      SSP0,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT1, 0x09C, MMC1,     HSL,    SEC2_JTAG, SSP2,      SSP0,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_DAT0, 0x0A0, MMC1,     HSL,    SEC2_JTAG, SSP2,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_CMD,  0x0A4, MMC1,     HSL,    SEC1_JTAG, SSP2,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_CLK,  0x0A8, MMC1,     HSL,    SEC2_JTAG, SSP0,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(MMC1_CD,   0x0AC, MMC1,     GPIO,   SEC1_JTAG, NONE,      NONE,      NONE,     NONE,     NONE),
	MFPR_910(VCXO_OUT,  0x0D8, VCXO_OUT, PWM3,   NONE,      NONE,      NONE,      NONE,     NONE,     NONE),
};


static const unsigned p910_usim2_pin1[] = {GPIO67, GPIO68, GPIO69};
static const unsigned p910_usim2_pin2[] = {ND_IO15, ND_IO14, ND_IO13};
static const unsigned p910_mmc1_pin1[] = {MMC1_DAT7, MMC1_DAT6, MMC1_DAT5,
	MMC1_DAT4, MMC1_DAT3, MMC1_DAT2, MMC1_DAT1, MMC1_DAT0, MMC1_CMD,
	MMC1_CLK, MMC1_CD, GPIO99};
static const unsigned p910_mmc2_pin1[] = {GPIO33, GPIO34, GPIO35, GPIO36,
	GPIO37, GPIO38, GPIO39, GPIO40, GPIO41, GPIO42};
static const unsigned p910_mmc3_pin1[] = {GPIO33, GPIO34, GPIO35, GPIO36,
	GPIO49, GPIO50};
static const unsigned p910_mmc3_pin2[] = {ND_IO7, ND_IO6, ND_IO5, ND_IO4,
	ND_IO3, ND_IO2, ND_IO1, ND_IO0, ND_CLE, SM_SCLK};
static const unsigned p910_uart0_pin1[] = {GPIO29, GPIO30, GPIO31, GPIO32};
static const unsigned p910_uart1_pin1[] = {GPIO47, GPIO48};
static const unsigned p910_uart1_pin2[] = {GPIO31, GPIO32};
static const unsigned p910_uart1_pin3[] = {GPIO45, GPIO46};
static const unsigned p910_uart1_pin4[] = {GPIO29, GPIO30, GPIO31, GPIO32};
static const unsigned p910_uart1_pin5[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned p910_uart2_pin1[] = {GPIO43, GPIO44};
static const unsigned p910_uart2_pin2[] = {GPIO51, GPIO52};
static const unsigned p910_uart2_pin3[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned p910_uart2_pin4[] = {GPIO51, GPIO52, GPIO53, GPIO54};
static const unsigned p910_twsi_pin1[] = {GPIO51, GPIO52};
static const unsigned p910_twsi_pin2[] = {GPIO53, GPIO54};
static const unsigned p910_twsi_pin3[] = {GPIO79, GPIO80};
static const unsigned p910_ccic_pin1[] = {GPIO67, GPIO68, GPIO69, GPIO70,
	GPIO71, GPIO72, GPIO73, GPIO74, GPIO75, GPIO76, GPIO77, GPIO78};
static const unsigned p910_lcd_pin1[] = {GPIO81, GPIO82, GPIO83, GPIO84,
	GPIO85, GPIO86, GPIO87, GPIO88, GPIO89, GPIO90, GPIO91, GPIO92,
	GPIO93, GPIO94, GPIO95, GPIO96, GPIO97, GPIO98, GPIO100, GPIO101,
	GPIO102, GPIO103};
static const unsigned p910_spi_pin1[] = {GPIO104, GPIO105, GPIO107, GPIO108};
static const unsigned p910_spi_pin2[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned p910_spi_pin3[] = {GPIO33, GPIO34, GPIO35, GPIO36,
	GPIO37};
static const unsigned p910_spi_pin4[] = {GPIO67, GPIO68, GPIO69, GPIO70,
	GPIO71};
static const unsigned p910_spi2_pin1[] = {GPIO64, GPIO65};
static const unsigned p910_spi2_pin2[] = {GPIO102, GPIO103};
static const unsigned p910_dssp2_pin1[] = {GPIO102, GPIO103, GPIO104, GPIO105};
static const unsigned p910_dssp2_pin2[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned p910_dssp2_pin3[] = {GPIO111, GPIO112, GPIO113};
static const unsigned p910_dssp3_pin1[] = {GPIO106, GPIO107, GPIO108, GPIO109};
static const unsigned p910_dssp3_pin2[] = {GPIO51, GPIO52, GPIO53, GPIO54};
static const unsigned p910_dssp3_pin3[] = {GPIO114, GPIO115, GPIO116};
static const unsigned p910_ssp0_pin1[] = {MMC1_DAT3, MMC1_DAT2, MMC1_DAT1,
	MMC1_CLK};
static const unsigned p910_ssp0_pin2[] = {GPIO33, GPIO34, GPIO35, GPIO36};
static const unsigned p910_ssp0_pin3[] = {GPIO47, GPIO48, GPIO49, GPIO50};
static const unsigned p910_ssp0_pin4[] = {GPIO51, GPIO52, GPIO53, GPIO54};
static const unsigned p910_ssp1_pin1[] = {GPIO21, GPIO22, GPIO23, GPIO24};
static const unsigned p910_ssp1_pin2[] = {GPIO20, GPIO21, GPIO22, GPIO23,
	GPIO24};
static const unsigned p910_ssp2_pin1[] = {MMC1_DAT2, MMC1_DAT1, MMC1_DAT0,
	MMC1_CMD};
static const unsigned p910_ssp2_pin2[] = {GPIO33, GPIO34, GPIO35, GPIO36};
static const unsigned p910_ssp2_pin3[] = {GPIO47, GPIO48, GPIO49, GPIO50};
static const unsigned p910_ssp2_pin4[] = {ND_IO12, ND_IO11, ND_IO10, ND_IO9};
static const unsigned p910_gssp_pin1[] = {GPIO25, GPIO26, GPIO27, GPIO28};
static const unsigned p910_pwm0_pin1[] = {GPIO117};
static const unsigned p910_pwm1_pin1[] = {GPIO118};
static const unsigned p910_pwm1_pin2[] = {GPIO51};
static const unsigned p910_pwm2_pin1[] = {GPIO119};
static const unsigned p910_pwm3_pin1[] = {GPIO120};
static const unsigned p910_pwm3_pin2[] = {ND_IO8};
static const unsigned p910_pwm3_pin3[] = {VCXO_OUT};
static const unsigned p910_pri_jtag_pin1[] = {GPIO117, GPIO118, GPIO119,
	GPIO120};
static const unsigned p910_sec1_jtag_pin1[] = {MMC1_DAT7, MMC1_DAT6, MMC1_DAT5,
	MMC1_CMD, MMC1_CD};
static const unsigned p910_sec2_jtag_pin1[] = {MMC1_DAT3, MMC1_DAT2, MMC1_DAT1,
	MMC1_DAT0, MMC1_CLK};
static const unsigned p910_hsl_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40,
	GPIO41, GPIO42};
static const unsigned p910_hsl_pin2[] = {GPIO61, GPIO62, GPIO63, GPIO64,
	GPIO65, GPIO66};
static const unsigned p910_hsl_pin3[] = {MMC1_DAT3, MMC1_DAT2, MMC1_DAT1,
	MMC1_DAT0, MMC1_CMD, MMC1_CLK};
static const unsigned p910_w1_pin1[] = {GPIO59};
static const unsigned p910_w1_pin2[] = {GPIO65};
static const unsigned p910_w1_pin3[] = {GPIO106};
static const unsigned p910_w1_pin4[] = {GPIO123};
static const unsigned p910_kpmk_pin1[] = {GPIO0, GPIO1, GPIO2, GPIO3, GPIO4,
	GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13,
	GPIO14, GPIO15};
static const unsigned p910_kpmk_pin2[] = {GPIO0, GPIO1, GPIO2, GPIO3, GPIO4,
	GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO12};
static const unsigned p910_kpdk_pin1[] = {GPIO12, GPIO13, GPIO14, GPIO15,
	GPIO16, GPIO17, GPIO18, GPIO19};
static const unsigned p910_tds_pin1[] = {GPIO55, GPIO56, GPIO57, GPIO58,
	GPIO59};
static const unsigned p910_tds_pin2[] = {GPIO55, GPIO57, GPIO58, GPIO59};
static const unsigned p910_tb_pin1[] = {GPIO14, GPIO15, GPIO16, GPIO17};
static const unsigned p910_tb_pin2[] = {GPIO55, GPIO56, GPIO57, GPIO58};
static const unsigned p910_tb_pin3[] = {MMC1_DAT7, MMC1_DAT6, MMC1_DAT5,
	MMC1_DAT4};
static const unsigned p910_ext_dma0_pin1[] = {GPIO72};
static const unsigned p910_ext_dma0_pin2[] = {ND_IO15};
static const unsigned p910_ext_dma0_pin3[] = {ND_NRE};
static const unsigned p910_ext_dma1_pin1[] = {GPIO73};
static const unsigned p910_ext_dma1_pin2[] = {GPIO123};
static const unsigned p910_ext_dma1_pin3[] = {GPIO126};
static const unsigned p910_ext_dma2_pin1[] = {GPIO74};
static const unsigned p910_ext0_int_pin1[] = {GPIO44};
static const unsigned p910_ext0_int_pin2[] = {ND_IO13};
static const unsigned p910_ext1_int_pin1[] = {GPIO45};
static const unsigned p910_ext1_int_pin2[] = {ND_IO12};
static const unsigned p910_ext2_int_pin1[] = {GPIO46};
static const unsigned p910_ext2_int_pin2[] = {GPIO125};
static const unsigned p910_dac_st23_pin1[] = {GPIO32};
static const unsigned p910_dac_st23_pin2[] = {GPIO43};
static const unsigned p910_dac_st23_pin3[] = {GPIO52};
static const unsigned p910_dac_st23_pin4[] = {GPIO124};
static const unsigned p910_vcxo_out_pin1[] = {GPIO50};
static const unsigned p910_vcxo_out_pin2[] = {VCXO_OUT};
static const unsigned p910_vcxo_out_pin3[] = {GPIO20};
static const unsigned p910_vcxo_req_pin1[] = {GPIO49};
static const unsigned p910_vcxo_req_pin2[] = {GPIO125};
static const unsigned p910_vcxo_out2_pin1[] = {GPIO86};
static const unsigned p910_vcxo_out2_pin2[] = {ND_IO9};
static const unsigned p910_vcxo_req2_pin1[] = {GPIO84};
static const unsigned p910_ulpi_pin1[] = {GPIO67, GPIO68, GPIO69, GPIO70,
	GPIO71, GPIO72, GPIO73, GPIO74, GPIO75, GPIO76, GPIO77, GPIO78};
static const unsigned p910_nand_pin1[] = {ND_IO15, ND_IO14, ND_IO13, ND_IO12,
	ND_IO11, ND_IO10, ND_IO9, ND_IO8, ND_IO7, ND_IO6, ND_IO5, ND_IO4,
	ND_IO3, ND_IO2, ND_IO1, ND_IO0, ND_NCS0, ND_NWE, ND_NRE, ND_CLE,
	ND_ALE, ND_RDY0};
static const unsigned p910_gpio0_pin1[] = {GPIO0};
static const unsigned p910_gpio0_pin2[] = {SM_ADV};
static const unsigned p910_gpio1_pin1[] = {GPIO1};
static const unsigned p910_gpio1_pin2[] = {ND_RDY1};
static const unsigned p910_gpio2_pin1[] = {GPIO2};
static const unsigned p910_gpio2_pin2[] = {SM_ADVMUX};
static const unsigned p910_gpio3_pin1[] = {GPIO3};
static const unsigned p910_gpio3_pin2[] = {SM_RDY};
static const unsigned p910_gpio20_pin1[] = {GPIO20};
static const unsigned p910_gpio20_pin2[] = {ND_IO15};
static const unsigned p910_gpio20_pin3[] = {MMC1_DAT6};
static const unsigned p910_gpio21_pin1[] = {GPIO21};
static const unsigned p910_gpio21_pin2[] = {ND_IO14};
static const unsigned p910_gpio21_pin3[] = {MMC1_DAT5};
static const unsigned p910_gpio22_pin1[] = {GPIO22};
static const unsigned p910_gpio22_pin2[] = {ND_IO13};
static const unsigned p910_gpio22_pin3[] = {MMC1_DAT4};
static const unsigned p910_gpio23_pin1[] = {GPIO23};
static const unsigned p910_gpio23_pin2[] = {ND_IO12};
static const unsigned p910_gpio23_pin3[] = {MMC1_CD};
static const unsigned p910_gpio24_pin1[] = {GPIO24};
static const unsigned p910_gpio24_pin2[] = {ND_IO11};
static const unsigned p910_gpio24_pin3[] = {MMC1_DAT7};
static const unsigned p910_gpio25_pin1[] = {GPIO25};
static const unsigned p910_gpio25_pin2[] = {ND_IO10};
static const unsigned p910_gpio26_pin1[] = {GPIO26};
static const unsigned p910_gpio26_pin2[] = {ND_IO9};
static const unsigned p910_gpio27_pin1[] = {GPIO27};
static const unsigned p910_gpio27_pin2[] = {ND_IO8};
static const unsigned p910_gpio85_pin1[] = {GPIO85};
static const unsigned p910_gpio85_pin2[] = {ND_NCS0};
static const unsigned p910_gpio86_pin1[] = {GPIO86};
static const unsigned p910_gpio86_pin2[] = {ND_NCS1};
static const unsigned p910_gpio87_pin1[] = {GPIO87};
static const unsigned p910_gpio87_pin2[] = {SM_NCS0};
static const unsigned p910_gpio88_pin1[] = {GPIO88};
static const unsigned p910_gpio88_pin2[] = {SM_NCS1};
static const unsigned p910_gpio89_pin1[] = {GPIO89};
static const unsigned p910_gpio89_pin2[] = {ND_NWE};
static const unsigned p910_gpio90_pin1[] = {GPIO90};
static const unsigned p910_gpio90_pin2[] = {ND_NRE};
static const unsigned p910_gpio91_pin1[] = {GPIO91};
static const unsigned p910_gpio91_pin2[] = {ND_ALE};
static const unsigned p910_gpio92_pin1[] = {GPIO92};
static const unsigned p910_gpio92_pin2[] = {ND_RDY0};

static struct pxa3xx_pin_group pxa910_grps[] = {
	GRP_910("usim2 3p1", USIM2, p910_usim2_pin1),
	GRP_910("usim2 3p2", USIM2, p910_usim2_pin2),
	GRP_910("mmc1 12p", MMC1, p910_mmc1_pin1),
	GRP_910("mmc2 10p", MMC2, p910_mmc2_pin1),
	GRP_910("mmc3 6p", MMC3, p910_mmc3_pin1),
	GRP_910("mmc3 10p", MMC3, p910_mmc3_pin2),
	GRP_910("uart0 4p", UART0, p910_uart0_pin1),
	GRP_910("uart1 2p1", UART1, p910_uart1_pin1),
	GRP_910("uart1 2p2", UART1, p910_uart1_pin2),
	GRP_910("uart1 2p3", UART1, p910_uart1_pin3),
	GRP_910("uart1 4p4", UART1, p910_uart1_pin4),
	GRP_910("uart1 4p5", UART1, p910_uart1_pin5),
	GRP_910("uart2 2p1", UART2, p910_uart2_pin1),
	GRP_910("uart2 2p2", UART2, p910_uart2_pin2),
	GRP_910("uart2 4p3", UART2, p910_uart2_pin3),
	GRP_910("uart2 4p4", UART2, p910_uart2_pin4),
	GRP_910("twsi 2p1", TWSI, p910_twsi_pin1),
	GRP_910("twsi 2p2", TWSI, p910_twsi_pin2),
	GRP_910("twsi 2p3", TWSI, p910_twsi_pin3),
	GRP_910("ccic", CCIC, p910_ccic_pin1),
	GRP_910("lcd", LCD, p910_lcd_pin1),
	GRP_910("spi 4p1", SPI, p910_spi_pin1),
	GRP_910("spi 4p2", SPI, p910_spi_pin2),
	GRP_910("spi 5p3", SPI, p910_spi_pin3),
	GRP_910("spi 5p4", SPI, p910_spi_pin4),
	GRP_910("dssp2 4p1", DSSP2, p910_dssp2_pin1),
	GRP_910("dssp2 4p2", DSSP2, p910_dssp2_pin2),
	GRP_910("dssp2 3p3", DSSP2, p910_dssp2_pin3),
	GRP_910("dssp3 4p1", DSSP3, p910_dssp3_pin1),
	GRP_910("dssp3 4p2", DSSP3, p910_dssp3_pin2),
	GRP_910("dssp3 3p3", DSSP3, p910_dssp3_pin3),
	GRP_910("ssp0 4p1", SSP0, p910_ssp0_pin1),
	GRP_910("ssp0 4p2", SSP0, p910_ssp0_pin2),
	GRP_910("ssp0 4p3", SSP0, p910_ssp0_pin3),
	GRP_910("ssp0 4p4", SSP0, p910_ssp0_pin4),
	GRP_910("ssp1 4p1", SSP1, p910_ssp1_pin1),
	GRP_910("ssp1 5p2", SSP1, p910_ssp1_pin2),
	GRP_910("ssp2 4p1", SSP2, p910_ssp2_pin1),
	GRP_910("ssp2 4p2", SSP2, p910_ssp2_pin2),
	GRP_910("ssp2 4p3", SSP2, p910_ssp2_pin3),
	GRP_910("ssp2 4p4", SSP2, p910_ssp2_pin4),
	GRP_910("gssp", GSSP, p910_gssp_pin1),
	GRP_910("pwm0", PWM0, p910_pwm0_pin1),
	GRP_910("pwm1-1", PWM1, p910_pwm1_pin1),
	GRP_910("pwm1-2", PWM1, p910_pwm1_pin2),
	GRP_910("pwm2", PWM2, p910_pwm2_pin1),
	GRP_910("pwm3-1", PWM3, p910_pwm3_pin1),
	GRP_910("pwm3-2", PWM3, p910_pwm3_pin2),
	GRP_910("pwm3-3", PWM3, p910_pwm3_pin3),
	GRP_910("pri jtag", PRI_JTAG, p910_pri_jtag_pin1),
	GRP_910("sec1 jtag", SEC1_JTAG, p910_sec1_jtag_pin1),
	GRP_910("sec2 jtag", SEC2_JTAG, p910_sec2_jtag_pin1),
	GRP_910("hsl 6p1", HSL, p910_hsl_pin1),
	GRP_910("hsl 6p2", HSL, p910_hsl_pin2),
	GRP_910("hsl 6p3", HSL, p910_hsl_pin3),
	GRP_910("w1-1", ONE_WIRE, p910_w1_pin1),
	GRP_910("w1-2", ONE_WIRE, p910_w1_pin2),
	GRP_910("w1-3", ONE_WIRE, p910_w1_pin3),
	GRP_910("w1-4", ONE_WIRE, p910_w1_pin4),
	GRP_910("kpmk 16p1", KP_MK, p910_kpmk_pin1),
	GRP_910("kpmk 11p2", KP_MK, p910_kpmk_pin2),
	GRP_910("kpdk 8p1", KP_DK, p910_kpdk_pin1),
	GRP_910("tds 5p1", TDS, p910_tds_pin1),
	GRP_910("tds 4p2", TDS, p910_tds_pin2),
	GRP_910("tb 4p1", TB, p910_tb_pin1),
	GRP_910("tb 4p2", TB, p910_tb_pin2),
	GRP_910("tb 4p3", TB, p910_tb_pin3),
	GRP_910("ext dma0-1", EXT_DMA, p910_ext_dma0_pin1),
	GRP_910("ext dma0-2", EXT_DMA, p910_ext_dma0_pin2),
	GRP_910("ext dma0-3", EXT_DMA, p910_ext_dma0_pin3),
	GRP_910("ext dma1-1", EXT_DMA, p910_ext_dma1_pin1),
	GRP_910("ext dma1-2", EXT_DMA, p910_ext_dma1_pin2),
	GRP_910("ext dma1-3", EXT_DMA, p910_ext_dma1_pin3),
	GRP_910("ext dma2", EXT_DMA, p910_ext_dma2_pin1),
	GRP_910("ext0 int-1", EXT_INT, p910_ext0_int_pin1),
	GRP_910("ext0 int-2", EXT_INT, p910_ext0_int_pin2),
	GRP_910("ext1 int-1", EXT_INT, p910_ext1_int_pin1),
	GRP_910("ext1 int-2", EXT_INT, p910_ext1_int_pin2),
	GRP_910("ext2 int-1", EXT_INT, p910_ext2_int_pin1),
	GRP_910("ext2 int-2", EXT_INT, p910_ext2_int_pin2),
	GRP_910("dac st23-1", DAC_ST23, p910_dac_st23_pin1),
	GRP_910("dac st23-2", DAC_ST23, p910_dac_st23_pin2),
	GRP_910("dac st23-3", DAC_ST23, p910_dac_st23_pin3),
	GRP_910("dac st23-4", DAC_ST23, p910_dac_st23_pin4),
	GRP_910("vcxo out-1", VCXO_OUT, p910_vcxo_out_pin1),
	GRP_910("vcxo out-2", VCXO_OUT, p910_vcxo_out_pin2),
	GRP_910("vcxo out-3", VCXO_OUT, p910_vcxo_out_pin3),
	GRP_910("vcxo req-1", VCXO_REQ, p910_vcxo_req_pin1),
	GRP_910("vcxo req-2", VCXO_REQ, p910_vcxo_req_pin2),
	GRP_910("vcxo out2-1", VCXO_OUT2, p910_vcxo_out2_pin1),
	GRP_910("vcxo out2-2", VCXO_OUT2, p910_vcxo_out2_pin2),
	GRP_910("vcxo req2", VCXO_REQ2, p910_vcxo_req2_pin1),
	GRP_910("ulpi", ULPI, p910_ulpi_pin1),
	GRP_910("nand", NAND, p910_nand_pin1),
	GRP_910("gpio0-1", GPIO, p910_gpio0_pin1),
	GRP_910("gpio0-2", GPIO, p910_gpio0_pin2),
	GRP_910("gpio1-1", GPIO, p910_gpio1_pin1),
	GRP_910("gpio1-2", GPIO, p910_gpio1_pin2),
	GRP_910("gpio2-1", GPIO, p910_gpio2_pin1),
	GRP_910("gpio2-2", GPIO, p910_gpio2_pin2),
	GRP_910("gpio3-1", GPIO, p910_gpio3_pin1),
	GRP_910("gpio3-2", GPIO, p910_gpio3_pin2),
	GRP_910("gpio20-1", GPIO, p910_gpio20_pin1),
	GRP_910("gpio20-2", GPIO, p910_gpio20_pin2),
	GRP_910("gpio21-1", GPIO, p910_gpio21_pin1),
	GRP_910("gpio21-2", GPIO, p910_gpio21_pin2),
	GRP_910("gpio22-1", GPIO, p910_gpio22_pin1),
	GRP_910("gpio22-2", GPIO, p910_gpio22_pin2),
	GRP_910("gpio23-1", GPIO, p910_gpio23_pin1),
	GRP_910("gpio23-2", GPIO, p910_gpio23_pin2),
	GRP_910("gpio24-1", GPIO, p910_gpio24_pin1),
	GRP_910("gpio24-2", GPIO, p910_gpio24_pin2),
	GRP_910("gpio25-1", GPIO, p910_gpio25_pin1),
	GRP_910("gpio25-2", GPIO, p910_gpio25_pin2),
	GRP_910("gpio26-1", GPIO, p910_gpio26_pin1),
	GRP_910("gpio26-2", GPIO, p910_gpio26_pin2),
	GRP_910("gpio27-1", GPIO, p910_gpio27_pin1),
	GRP_910("gpio27-2", GPIO, p910_gpio27_pin2),
	GRP_910("gpio85-1", GPIO, p910_gpio85_pin1),
	GRP_910("gpio85-2", GPIO, p910_gpio85_pin2),
	GRP_910("gpio86-1", GPIO, p910_gpio86_pin1),
	GRP_910("gpio86-2", GPIO, p910_gpio86_pin2),
	GRP_910("gpio87-1", GPIO, p910_gpio87_pin1),
	GRP_910("gpio87-2", GPIO, p910_gpio87_pin2),
	GRP_910("gpio88-1", GPIO, p910_gpio88_pin1),
	GRP_910("gpio88-2", GPIO, p910_gpio88_pin2),
	GRP_910("gpio89-1", GPIO, p910_gpio89_pin1),
	GRP_910("gpio89-2", GPIO, p910_gpio89_pin2),
	GRP_910("gpio90-1", GPIO, p910_gpio90_pin1),
	GRP_910("gpio90-2", GPIO, p910_gpio90_pin2),
	GRP_910("gpio91-1", GPIO, p910_gpio91_pin1),
	GRP_910("gpio91-2", GPIO, p910_gpio91_pin2),
	GRP_910("gpio92-1", GPIO, p910_gpio92_pin1),
	GRP_910("gpio92-2", GPIO, p910_gpio92_pin2),
};

static const char * const p910_usim2_grps[] = {"usim2 3p1", "usim2 3p2"};
static const char * const p910_mmc1_grps[] = {"mmc1 12p"};
static const char * const p910_mmc2_grps[] = {"mmc2 10p"};
static const char * const p910_mmc3_grps[] = {"mmc3 6p", "mmc3 10p"};
static const char * const p910_uart0_grps[] = {"uart0 4p"};
static const char * const p910_uart1_grps[] = {"uart1 2p1", "uart1 2p2",
	"uart1 2p3", "uart1 4p4", "uart1 4p5"};
static const char * const p910_uart2_grps[] = {"uart2 2p1", "uart2 2p2",
	"uart2 4p3", "uart2 4p4"};
static const char * const p910_twsi_grps[] = {"twsi 2p1", "twsi 2p2",
	"twsi 2p3"};
static const char * const p910_ccic_grps[] = {"ccic"};
static const char * const p910_lcd_grps[] = {"lcd"};
static const char * const p910_spi_grps[] = {"spi 4p1", "spi 4p2", "spi 5p3",
	"spi 5p4"};
static const char * const p910_dssp2_grps[] = {"dssp2 4p1", "dssp2 4p2",
	"dssp2 3p3"};
static const char * const p910_dssp3_grps[] = {"dssp3 4p1", "dssp3 4p2",
	"dssp3 3p3"};
static const char * const p910_ssp0_grps[] = {"ssp0 4p1", "ssp0 4p2",
	"ssp0 4p3", "ssp0 4p4"};
static const char * const p910_ssp1_grps[] = {"ssp1 4p1", "ssp1 5p2"};
static const char * const p910_ssp2_grps[] = {"ssp2 4p1", "ssp2 4p2",
	"ssp2 4p3", "ssp2 4p4"};
static const char * const p910_gssp_grps[] = {"gssp"};
static const char * const p910_pwm0_grps[] = {"pwm0"};
static const char * const p910_pwm1_grps[] = {"pwm1-1", "pwm1-2"};
static const char * const p910_pwm2_grps[] = {"pwm2"};
static const char * const p910_pwm3_grps[] = {"pwm3-1", "pwm3-2", "pwm3-3"};
static const char * const p910_pri_jtag_grps[] = {"pri jtag"};
static const char * const p910_sec1_jtag_grps[] = {"sec1 jtag"};
static const char * const p910_sec2_jtag_grps[] = {"sec2 jtag"};
static const char * const p910_hsl_grps[] = {"hsl 6p1", "hsl 6p2", "hsl 6p3"};
static const char * const p910_w1_grps[] = {"w1-1", "w1-2", "w1-3", "w1-4"};
static const char * const p910_kpmk_grps[] = {"kpmk 16p1", "kpmk 11p2"};
static const char * const p910_kpdk_grps[] = {"kpdk 8p1"};
static const char * const p910_tds_grps[] = {"tds 5p1", "tds 4p2"};
static const char * const p910_tb_grps[] = {"tb 4p1", "tb 4p2", "tb 4p3"};
static const char * const p910_dma0_grps[] = {"ext dma0-1", "ext dma0-2",
	"ext dma0-3"};
static const char * const p910_dma1_grps[] = {"ext dma1-1", "ext dma1-2",
	"ext dma1-3"};
static const char * const p910_dma2_grps[] = {"ext dma2"};
static const char * const p910_int0_grps[] = {"ext0 int-1", "ext0 int-2"};
static const char * const p910_int1_grps[] = {"ext1 int-1", "ext1 int-2"};
static const char * const p910_int2_grps[] = {"ext2 int-1", "ext2 int-2"};
static const char * const p910_dac_st23_grps[] = {"dac st23-1", "dac st23-2",
	"dac st23-3", "dac st23-4"};
static const char * const p910_vcxo_out_grps[] = {"vcxo out-1", "vcxo out-2",
	"vcxo out-3"};
static const char * const p910_vcxo_req_grps[] = {"vcxo req-1", "vcxo req-2"};
static const char * const p910_vcxo_out2_grps[] = {"vcxo out2-1",
	"vcxo out2-2"};
static const char * const p910_vcxo_req2_grps[] = {"vcxo req2"};
static const char * const p910_ulpi_grps[] = {"ulpi"};
static const char * const p910_nand_grps[] = {"nand"};
static const char * const p910_gpio0_grps[] = {"gpio0-1", "gpio0-2"};
static const char * const p910_gpio1_grps[] = {"gpio1-1", "gpio1-2"};
static const char * const p910_gpio2_grps[] = {"gpio2-1", "gpio2-2"};
static const char * const p910_gpio3_grps[] = {"gpio3-1", "gpio3-2"};
static const char * const p910_gpio20_grps[] = {"gpio20-1", "gpio20-2"};
static const char * const p910_gpio21_grps[] = {"gpio21-1", "gpio21-2"};
static const char * const p910_gpio22_grps[] = {"gpio22-1", "gpio22-2"};
static const char * const p910_gpio23_grps[] = {"gpio23-1", "gpio23-2"};
static const char * const p910_gpio24_grps[] = {"gpio24-1", "gpio24-2"};
static const char * const p910_gpio25_grps[] = {"gpio25-1", "gpio25-2"};
static const char * const p910_gpio26_grps[] = {"gpio26-1", "gpio26-2"};
static const char * const p910_gpio27_grps[] = {"gpio27-1", "gpio27-2"};
static const char * const p910_gpio85_grps[] = {"gpio85-1", "gpio85-2"};
static const char * const p910_gpio86_grps[] = {"gpio86-1", "gpio86-2"};
static const char * const p910_gpio87_grps[] = {"gpio87-1", "gpio87-2"};
static const char * const p910_gpio88_grps[] = {"gpio88-1", "gpio88-2"};
static const char * const p910_gpio89_grps[] = {"gpio89-1", "gpio89-2"};
static const char * const p910_gpio90_grps[] = {"gpio90-1", "gpio90-2"};
static const char * const p910_gpio91_grps[] = {"gpio91-1", "gpio91-2"};
static const char * const p910_gpio92_grps[] = {"gpio92-1", "gpio92-2"};

static struct pxa3xx_pmx_func pxa910_funcs[] = {
	{"usim2",	ARRAY_AND_SIZE(p910_usim2_grps)},
	{"mmc1",	ARRAY_AND_SIZE(p910_mmc1_grps)},
	{"mmc2",	ARRAY_AND_SIZE(p910_mmc2_grps)},
	{"mmc3",	ARRAY_AND_SIZE(p910_mmc3_grps)},
	{"uart0",	ARRAY_AND_SIZE(p910_uart0_grps)},
	{"uart1",	ARRAY_AND_SIZE(p910_uart1_grps)},
	{"uart2",	ARRAY_AND_SIZE(p910_uart2_grps)},
	{"twsi",	ARRAY_AND_SIZE(p910_twsi_grps)},
	{"ccic",	ARRAY_AND_SIZE(p910_ccic_grps)},
	{"lcd",		ARRAY_AND_SIZE(p910_lcd_grps)},
	{"spi",		ARRAY_AND_SIZE(p910_spi_grps)},
	{"dssp2",	ARRAY_AND_SIZE(p910_dssp2_grps)},
	{"dssp3",	ARRAY_AND_SIZE(p910_dssp3_grps)},
	{"ssp0",	ARRAY_AND_SIZE(p910_ssp0_grps)},
	{"ssp1",	ARRAY_AND_SIZE(p910_ssp1_grps)},
	{"ssp2",	ARRAY_AND_SIZE(p910_ssp2_grps)},
	{"gssp",	ARRAY_AND_SIZE(p910_gssp_grps)},
	{"pwm0",	ARRAY_AND_SIZE(p910_pwm0_grps)},
	{"pwm1",	ARRAY_AND_SIZE(p910_pwm1_grps)},
	{"pwm2",	ARRAY_AND_SIZE(p910_pwm2_grps)},
	{"pwm3",	ARRAY_AND_SIZE(p910_pwm3_grps)},
	{"pri_jtag",	ARRAY_AND_SIZE(p910_pri_jtag_grps)},
	{"sec1_jtag",	ARRAY_AND_SIZE(p910_sec1_jtag_grps)},
	{"sec2_jtag",	ARRAY_AND_SIZE(p910_sec2_jtag_grps)},
	{"hsl",		ARRAY_AND_SIZE(p910_hsl_grps)},
	{"w1",		ARRAY_AND_SIZE(p910_w1_grps)},
	{"kpmk",	ARRAY_AND_SIZE(p910_kpmk_grps)},
	{"kpdk",	ARRAY_AND_SIZE(p910_kpdk_grps)},
	{"tds",		ARRAY_AND_SIZE(p910_tds_grps)},
	{"tb",		ARRAY_AND_SIZE(p910_tb_grps)},
	{"dma0",	ARRAY_AND_SIZE(p910_dma0_grps)},
	{"dma1",	ARRAY_AND_SIZE(p910_dma1_grps)},
	{"dma2",	ARRAY_AND_SIZE(p910_dma2_grps)},
	{"int0",	ARRAY_AND_SIZE(p910_int0_grps)},
	{"int1",	ARRAY_AND_SIZE(p910_int1_grps)},
	{"int2",	ARRAY_AND_SIZE(p910_int2_grps)},
	{"dac_st23",	ARRAY_AND_SIZE(p910_dac_st23_grps)},
	{"vcxo_out",	ARRAY_AND_SIZE(p910_vcxo_out_grps)},
	{"vcxo_req",	ARRAY_AND_SIZE(p910_vcxo_req_grps)},
	{"vcxo_out2",	ARRAY_AND_SIZE(p910_vcxo_out2_grps)},
	{"vcxo_req2",	ARRAY_AND_SIZE(p910_vcxo_req2_grps)},
	{"ulpi",	ARRAY_AND_SIZE(p910_ulpi_grps)},
	{"nand",	ARRAY_AND_SIZE(p910_nand_grps)},
	{"gpio0",	ARRAY_AND_SIZE(p910_gpio0_grps)},
	{"gpio1",	ARRAY_AND_SIZE(p910_gpio1_grps)},
	{"gpio2",	ARRAY_AND_SIZE(p910_gpio2_grps)},
	{"gpio3",	ARRAY_AND_SIZE(p910_gpio3_grps)},
	{"gpio20",	ARRAY_AND_SIZE(p910_gpio20_grps)},
	{"gpio21",	ARRAY_AND_SIZE(p910_gpio21_grps)},
	{"gpio22",	ARRAY_AND_SIZE(p910_gpio22_grps)},
	{"gpio23",	ARRAY_AND_SIZE(p910_gpio23_grps)},
	{"gpio24",	ARRAY_AND_SIZE(p910_gpio24_grps)},
	{"gpio25",	ARRAY_AND_SIZE(p910_gpio25_grps)},
	{"gpio26",	ARRAY_AND_SIZE(p910_gpio26_grps)},
	{"gpio27",	ARRAY_AND_SIZE(p910_gpio27_grps)},
	{"gpio85",	ARRAY_AND_SIZE(p910_gpio85_grps)},
	{"gpio86",	ARRAY_AND_SIZE(p910_gpio86_grps)},
	{"gpio87",	ARRAY_AND_SIZE(p910_gpio87_grps)},
	{"gpio88",	ARRAY_AND_SIZE(p910_gpio88_grps)},
	{"gpio89",	ARRAY_AND_SIZE(p910_gpio89_grps)},
	{"gpio90",	ARRAY_AND_SIZE(p910_gpio90_grps)},
	{"gpio91",	ARRAY_AND_SIZE(p910_gpio91_grps)},
	{"gpio92",	ARRAY_AND_SIZE(p910_gpio92_grps)},
};

static struct pinctrl_desc pxa910_pctrl_desc = {
	.name		= "pxa910-pinctrl",
	.owner		= THIS_MODULE,
};

static struct pxa3xx_pinmux_info pxa910_info = {
	.mfp		= pxa910_mfp,
	.num_mfp	= ARRAY_SIZE(pxa910_mfp),
	.grps		= pxa910_grps,
	.num_grps	= ARRAY_SIZE(pxa910_grps),
	.funcs		= pxa910_funcs,
	.num_funcs	= ARRAY_SIZE(pxa910_funcs),
	.num_gpio	= 128,
	.desc		= &pxa910_pctrl_desc,
	.pads		= pxa910_pads,
	.num_pads	= ARRAY_SIZE(pxa910_pads),

	.cputype	= PINCTRL_PXA910,
	.ds_mask	= PXA910_DS_MASK,
	.ds_shift	= PXA910_DS_SHIFT,
};

static int pxa910_pinmux_probe(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_register(pdev, &pxa910_info);
}

static int pxa910_pinmux_remove(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_unregister(pdev);
}

static struct platform_driver pxa910_pinmux_driver = {
	.driver = {
		.name	= "pxa910-pinmux",
		.owner	= THIS_MODULE,
	},
	.probe	= pxa910_pinmux_probe,
	.remove	= pxa910_pinmux_remove,
};

static int __init pxa910_pinmux_init(void)
{
	return platform_driver_register(&pxa910_pinmux_driver);
}
core_initcall_sync(pxa910_pinmux_init);

static void __exit pxa910_pinmux_exit(void)
{
	platform_driver_unregister(&pxa910_pinmux_driver);
}
module_exit(pxa910_pinmux_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("PXA3xx pin control driver");
MODULE_LICENSE("GPL v2");
