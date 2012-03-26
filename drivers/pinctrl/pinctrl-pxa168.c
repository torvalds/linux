/*
 *  linux/drivers/pinctrl/pinmux-pxa168.c
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

#define PXA168_DS_MASK		0x1800
#define PXA168_DS_SHIFT		11
#define PXA168_SLEEP_MASK	0x38
#define PXA168_SLEEP_SELECT	(1 << 9)
#define PXA168_SLEEP_DATA	(1 << 8)
#define PXA168_SLEEP_DIR	(1 << 7)

#define MFPR_168(a, r, f0, f1, f2, f3, f4, f5, f6, f7)		\
	{							\
		.name = #a,					\
		.pin = a,					\
		.mfpr = r,					\
		.func = {					\
			PXA168_MUX_##f0,			\
			PXA168_MUX_##f1,			\
			PXA168_MUX_##f2,			\
			PXA168_MUX_##f3,			\
			PXA168_MUX_##f4,			\
			PXA168_MUX_##f5,			\
			PXA168_MUX_##f6,			\
			PXA168_MUX_##f7,			\
		},						\
	}

#define GRP_168(a, m, p)		\
	{ .name = a, .mux = PXA168_MUX_##m, .pins = p, .npins = ARRAY_SIZE(p), }

/* 131 pins */
enum pxa168_pin_list {
	/* 0~122: GPIO0~GPIO122 */
	PWR_SCL = 123,
	PWR_SDA,
	TDI,
	TMS,
	TCK,
	TDO,
	TRST,
	WAKEUP = 130,
};

enum pxa168_mux {
	/* PXA3xx_MUX_GPIO = 0 (predefined in pinctrl-pxa3xx.h) */
	PXA168_MUX_GPIO = 0,
	PXA168_MUX_DFIO,
	PXA168_MUX_NAND,
	PXA168_MUX_SMC,
	PXA168_MUX_SMC_CS0,
	PXA168_MUX_SMC_CS1,
	PXA168_MUX_SMC_INT,
	PXA168_MUX_SMC_RDY,
	PXA168_MUX_MMC1,
	PXA168_MUX_MMC2,
	PXA168_MUX_MMC2_CMD,
	PXA168_MUX_MMC2_CLK,
	PXA168_MUX_MMC3,
	PXA168_MUX_MMC3_CMD,
	PXA168_MUX_MMC3_CLK,
	PXA168_MUX_MMC4,
	PXA168_MUX_MSP,
	PXA168_MUX_MSP_DAT3,
	PXA168_MUX_MSP_INS,
	PXA168_MUX_I2C,
	PXA168_MUX_PWRI2C,
	PXA168_MUX_AC97,
	PXA168_MUX_AC97_SYSCLK,
	PXA168_MUX_PWM,
	PXA168_MUX_PWM1,
	PXA168_MUX_XD,
	PXA168_MUX_XP,
	PXA168_MUX_LCD,
	PXA168_MUX_CCIC,
	PXA168_MUX_CF,
	PXA168_MUX_CF_RDY,
	PXA168_MUX_CF_nINPACK,
	PXA168_MUX_CF_nWAIT,
	PXA168_MUX_KP_MKOUT,
	PXA168_MUX_KP_MKIN,
	PXA168_MUX_KP_DK,
	PXA168_MUX_ETH,
	PXA168_MUX_ETH_TX,
	PXA168_MUX_ETH_RX,
	PXA168_MUX_ONE_WIRE,
	PXA168_MUX_UART1,
	PXA168_MUX_UART1_TX,
	PXA168_MUX_UART1_CTS,
	PXA168_MUX_UART1_nRI,
	PXA168_MUX_UART1_DTR,
	PXA168_MUX_UART2,
	PXA168_MUX_UART2_TX,
	PXA168_MUX_UART3,
	PXA168_MUX_UART3_TX,
	PXA168_MUX_UART3_CTS,
	PXA168_MUX_SSP1,
	PXA168_MUX_SSP1_TX,
	PXA168_MUX_SSP2,
	PXA168_MUX_SSP2_TX,
	PXA168_MUX_SSP3,
	PXA168_MUX_SSP3_TX,
	PXA168_MUX_SSP4,
	PXA168_MUX_SSP4_TX,
	PXA168_MUX_SSP5,
	PXA168_MUX_SSP5_TX,
	PXA168_MUX_USB,
	PXA168_MUX_JTAG,
	PXA168_MUX_RESET,
	PXA168_MUX_WAKEUP,
	PXA168_MUX_EXT_32K_IN,
	PXA168_MUX_NONE = 0xffff,
};

static struct pinctrl_pin_desc pxa168_pads[] = {
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
	PINCTRL_PIN(PWR_SCL, "PWR_SCL"),
	PINCTRL_PIN(PWR_SDA, "PWR_SDA"),
	PINCTRL_PIN(TDI, "TDI"),
	PINCTRL_PIN(TMS, "TMS"),
	PINCTRL_PIN(TCK, "TCK"),
	PINCTRL_PIN(TDO, "TDO"),
	PINCTRL_PIN(TRST, "TRST"),
	PINCTRL_PIN(WAKEUP, "WAKEUP"),
};

struct pxa3xx_mfp_pin pxa168_mfp[] = {
	/*       pin      offs   f0       f1           f2         f3           f4           f5        f6           f7  */
	MFPR_168(GPIO0,   0x04C, DFIO,    NONE,        NONE,      MSP,         MMC3_CMD,    GPIO,     MMC3,        NONE),
	MFPR_168(GPIO1,   0x050, DFIO,    NONE,        NONE,      MSP,         MMC3_CLK,    GPIO,     MMC3,        NONE),
	MFPR_168(GPIO2,   0x054, DFIO,    NONE,        NONE,      MSP,         NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO3,   0x058, DFIO,    NONE,        NONE,      NONE,        NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO4,   0x05C, DFIO,    NONE,        NONE,      MSP_DAT3,    NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO5,   0x060, DFIO,    NONE,        NONE,      MSP,         NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO6,   0x064, DFIO,    NONE,        NONE,      MSP,         NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO7,   0x068, DFIO,    NONE,        NONE,      MSP,         NONE,        GPIO,     MMC3,        NONE),
	MFPR_168(GPIO8,   0x06C, DFIO,    MMC2,        UART3_TX,  NONE,        MMC2_CMD,    GPIO,     MMC3_CLK,    NONE),
	MFPR_168(GPIO9,   0x070, DFIO,    MMC2,        UART3,     NONE,        MMC2_CLK,    GPIO,     MMC3_CMD,    NONE),
	MFPR_168(GPIO10,  0x074, DFIO,    MMC2,        UART3,     NONE,        NONE,        GPIO,     MSP_DAT3,    NONE),
	MFPR_168(GPIO11,  0x078, DFIO,    MMC2,        UART3,     NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO12,  0x07C, DFIO,    MMC2,        UART3,     NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO13,  0x080, DFIO,    MMC2,        UART3,     NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO14,  0x084, DFIO,    MMC2,        NONE,      NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO15,  0x088, DFIO,    MMC2,        NONE,      NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO16,  0x08C, GPIO,    NAND,        SMC_CS0,   SMC_CS1,     NONE,        NONE,     MMC3,        NONE),
	MFPR_168(GPIO17,  0x090, NAND,    NONE,        NONE,      NONE,        NONE,        GPIO,     MSP,         NONE),
	MFPR_168(GPIO18,  0x094, GPIO,    NAND,        SMC_CS1,   SMC_CS0,     NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO19,  0x098, SMC_CS0, NONE,        NONE,      CF,          NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO20,  0x09C, GPIO,    NONE,        SMC_CS1,   CF,          CF_RDY,      NONE,     NONE,        NONE),
	MFPR_168(GPIO21,  0x0A0, NAND,    MMC2_CLK,    NONE,      NONE,        NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO22,  0x0A4, NAND,    MMC2_CMD,    NONE,      NONE,        NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO23,  0x0A8, SMC,     NAND,        NONE,      CF,          NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO24,  0x0AC, NAND,    NONE,        NONE,      NONE,        NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO25,  0x0B0, SMC,     NAND,        NONE,      CF,          NONE,        GPIO,     NONE,        NONE),
	MFPR_168(GPIO26,  0x0B4, GPIO,    NAND,        NONE,      NONE,        CF,          NONE,     NONE,        NONE),
	MFPR_168(GPIO27,  0x0B8, SMC_INT, NAND,        SMC,       NONE,        SMC_RDY,     GPIO,     NONE,        NONE),
	MFPR_168(GPIO28,  0x0BC, SMC_RDY, MMC4,        SMC,       CF_RDY,      NONE,        GPIO,     MMC2_CMD,    NONE),
	MFPR_168(GPIO29,  0x0C0, SMC,     MMC4,        NONE,      CF,          NONE,        GPIO,     MMC2_CLK,    KP_DK),
	MFPR_168(GPIO30,  0x0C4, SMC,     MMC4,        UART3_TX,  CF,          NONE,        GPIO,     MMC2,        KP_DK),
	MFPR_168(GPIO31,  0x0C8, SMC,     MMC4,        UART3,     CF,          NONE,        GPIO,     MMC2,        KP_DK),
	MFPR_168(GPIO32,  0x0CC, SMC,     MMC4,        UART3,     CF,          NONE,        GPIO,     MMC2,        KP_DK),
	MFPR_168(GPIO33,  0x0D0, SMC,     MMC4,        UART3,     CF,          CF_nINPACK,  GPIO,     MMC2,        KP_DK),
	MFPR_168(GPIO34,  0x0D4, GPIO,    NONE,        SMC_CS1,   CF,          CF_nWAIT,    NONE,     MMC3,        KP_DK),
	MFPR_168(GPIO35,  0x0D8, GPIO,    NONE,        SMC,       CF_nINPACK,  NONE,        NONE,     MMC3_CMD,    KP_DK),
	MFPR_168(GPIO36,  0x0DC, GPIO,    NONE,        SMC,       CF_nWAIT,    NONE,        NONE,     MMC3_CLK,    KP_DK),
	MFPR_168(GPIO37,  0x000, GPIO,    MMC1,        NONE,      KP_MKOUT,    CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO38,  0x004, GPIO,    MMC1,        NONE,      KP_MKOUT,    CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO39,  0x008, GPIO,    NONE,        NONE,      KP_MKOUT,    CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO40,  0x00C, GPIO,    MMC1,        MSP,       KP_MKOUT,    CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO41,  0x010, GPIO,    MMC1,        MSP,       NONE,        CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO42,  0x014, GPIO,    I2C,         NONE,      MSP,         CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO43,  0x018, GPIO,    MMC1,        MSP,       MSP_INS,     NONE,        NONE,     KP_MKIN,     KP_DK),
	MFPR_168(GPIO44,  0x01C, GPIO,    MMC1,        MSP_DAT3,  MSP,         CCIC,        XP,       KP_MKIN,     KP_DK),
	MFPR_168(GPIO45,  0x020, GPIO,    NONE,        NONE,      MSP,         CCIC,        XP,       NONE,        KP_DK),
	MFPR_168(GPIO46,  0x024, GPIO,    MMC1,        MSP_INS,   MSP,         CCIC,        NONE,     KP_MKOUT,    KP_DK),
	MFPR_168(GPIO47,  0x028, GPIO,    NONE,        NONE,      MSP_INS,     NONE,        XP,       NONE,        KP_DK),
	MFPR_168(GPIO48,  0x02C, GPIO,    MMC1,        NONE,      MSP_DAT3,    CCIC,        NONE,     NONE,        KP_DK),
	MFPR_168(GPIO49,  0x030, GPIO,    MMC1,        NONE,      MSP,         NONE,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO50,  0x034, GPIO,    I2C,         NONE,      MSP,         CCIC,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO51,  0x038, GPIO,    MMC1,        NONE,      MSP,         NONE,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO52,  0x03C, GPIO,    MMC1,        NONE,      MSP,         NONE,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO53,  0x040, GPIO,    MMC1,        NONE,      NONE,        NONE,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO54,  0x044, GPIO,    MMC1,        NONE,      NONE,        CCIC,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO55,  0x048, GPIO,    NONE,        NONE,      MSP,         CCIC,        XD,       KP_MKOUT,    NONE),
	MFPR_168(GPIO56,  0x0E0, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO57,  0x0E4, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO58,  0x0E8, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO59,  0x0EC, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO60,  0x0F0, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO61,  0x0F4, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO62,  0x0F8, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO63,  0x0FC, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO64,  0x100, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO65,  0x104, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO66,  0x108, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO67,  0x10C, GPIO,    LCD,         NONE,      NONE,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO68,  0x110, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO69,  0x114, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO70,  0x118, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO71,  0x11C, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO72,  0x120, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO73,  0x124, GPIO,    LCD,         NONE,      XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO74,  0x128, GPIO,    LCD,         PWM,       XD,          NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO75,  0x12C, GPIO,    LCD,         PWM,       XD,          ONE_WIRE,    NONE,     NONE,        NONE),
	MFPR_168(GPIO76,  0x130, GPIO,    LCD,         PWM,       I2C,         NONE,        NONE,     MSP_INS,     NONE),
	MFPR_168(GPIO77,  0x134, GPIO,    LCD,         PWM1,      I2C,         ONE_WIRE,    NONE,     XD,          NONE),
	MFPR_168(GPIO78,  0x138, GPIO,    LCD,         NONE,      NONE,        NONE,        MMC4,     NONE,        NONE),
	MFPR_168(GPIO79,  0x13C, GPIO,    LCD,         NONE,      NONE,        ONE_WIRE,    MMC4,     NONE,        NONE),
	MFPR_168(GPIO80,  0x140, GPIO,    LCD,         NONE,      I2C,         NONE,        MMC4,     NONE,        NONE),
	MFPR_168(GPIO81,  0x144, GPIO,    LCD,         NONE,      I2C,         ONE_WIRE,    MMC4,     NONE,        NONE),
	MFPR_168(GPIO82,  0x148, GPIO,    LCD,         PWM,       NONE,        NONE,        MMC4,     NONE,        NONE),
	MFPR_168(GPIO83,  0x14C, GPIO,    LCD,         PWM,       NONE,        RESET,       MMC4,     NONE,        NONE),
	MFPR_168(GPIO84,  0x150, GPIO,    NONE,        PWM,       ONE_WIRE,    PWM1,        NONE,     NONE,        EXT_32K_IN),
	MFPR_168(GPIO85,  0x154, GPIO,    NONE,        PWM1,      NONE,        NONE,        NONE,     NONE,        USB),
	MFPR_168(GPIO86,  0x158, GPIO,    MMC2,        UART2,     NONE,        JTAG,        ETH_TX,   SSP5_TX,     SSP5),
	MFPR_168(GPIO87,  0x15C, GPIO,    MMC2,        UART2,     NONE,        JTAG,        ETH_TX,   SSP5,        SSP5_TX),
	MFPR_168(GPIO88,  0x160, GPIO,    MMC2,        UART2,     UART2_TX,    JTAG,        ETH_TX,   ETH_RX,      SSP5),
	MFPR_168(GPIO89,  0x164, GPIO,    MMC2,        UART2_TX,  UART2,       JTAG,        ETH_TX,   ETH_RX,      SSP5),
	MFPR_168(GPIO90,  0x168, GPIO,    MMC2,        NONE,      SSP3,        JTAG,        ETH_TX,   ETH_RX,      NONE),
	MFPR_168(GPIO91,  0x16C, GPIO,    MMC2,        NONE,      SSP3,        SSP4,        ETH_TX,   ETH_RX,      NONE),
	MFPR_168(GPIO92,  0x170, GPIO,    MMC2,        NONE,      SSP3,        SSP3_TX,     ETH,      NONE,        NONE),
	MFPR_168(GPIO93,  0x174, GPIO,    MMC2,        NONE,      SSP3_TX,     SSP3,        ETH,      NONE,        NONE),
	MFPR_168(GPIO94,  0x178, GPIO,    MMC2_CMD,    SSP3,      AC97_SYSCLK, AC97,        ETH,      NONE,        NONE),
	MFPR_168(GPIO95,  0x17C, GPIO,    MMC2_CLK,    NONE,      NONE,        AC97,        ETH,      NONE,        NONE),
	MFPR_168(GPIO96,  0x180, GPIO,    PWM,         NONE,      MMC2,        NONE,        ETH_RX,   ETH_TX,      NONE),
	MFPR_168(GPIO97,  0x184, GPIO,    PWM,         ONE_WIRE,  NONE,        NONE,        ETH_RX,   ETH_TX,      NONE),
	MFPR_168(GPIO98,  0x188, GPIO,    PWM1,        UART3_TX,  UART3,       NONE,        ETH_RX,   ETH_TX,      NONE),
	MFPR_168(GPIO99,  0x18C, GPIO,    ONE_WIRE,    UART3,     UART3_TX,    NONE,        ETH_RX,   ETH_TX,      NONE),
	MFPR_168(GPIO100, 0x190, GPIO,    NONE,        UART3_CTS, UART3,       NONE,        ETH,      NONE,        NONE),
	MFPR_168(GPIO101, 0x194, GPIO,    NONE,        UART3,     UART3_CTS,   NONE,        ETH,      NONE,        NONE),
	MFPR_168(GPIO102, 0x198, GPIO,    I2C,         UART3,     SSP4,        NONE,        NONE,     NONE,        NONE),
	MFPR_168(GPIO103, 0x19C, GPIO,    I2C,         UART3,     SSP4,        SSP2,        ETH,      NONE,        NONE),
	MFPR_168(GPIO104, 0x1A0, GPIO,    PWM,         UART1,     SSP4,        SSP4_TX,     AC97,     KP_MKOUT,    NONE),
	MFPR_168(GPIO105, 0x1A4, GPIO,    I2C,         UART1,     SSP4_TX,     SSP4,        AC97,     KP_MKOUT,    NONE),
	MFPR_168(GPIO106, 0x1A8, GPIO,    I2C,         PWM1,      AC97_SYSCLK, MMC2,        NONE,     KP_MKOUT,    NONE),
	MFPR_168(GPIO107, 0x1AC, GPIO,    UART1_TX,    UART1,     NONE,        SSP2,        MSP_DAT3, NONE,        KP_MKIN),
	MFPR_168(GPIO108, 0x1B0, GPIO,    UART1,       UART1_TX,  NONE,        SSP2_TX,     MSP,      NONE,        KP_MKIN),
	MFPR_168(GPIO109, 0x1B4, GPIO,    UART1_CTS,   UART1,     NONE,        AC97_SYSCLK, MSP,      NONE,        KP_MKIN),
	MFPR_168(GPIO110, 0x1B8, GPIO,    UART1,       UART1_CTS, NONE,        SMC_RDY,     MSP,      NONE,        KP_MKIN),
	MFPR_168(GPIO111, 0x1BC, GPIO,    UART1_nRI,   UART1,     SSP3,        SSP2,        MSP,      XD,          KP_MKOUT),
	MFPR_168(GPIO112, 0x1C0, GPIO,    UART1_DTR,   UART1,     ONE_WIRE,    SSP2,        MSP,      XD,          KP_MKOUT),
	MFPR_168(GPIO113, 0x1C4, GPIO,    NONE,        NONE,      NONE,        NONE,        NONE,     AC97_SYSCLK, NONE),
	MFPR_168(GPIO114, 0x1C8, GPIO,    SSP1,        NONE,      NONE,        NONE,        NONE,     AC97,        NONE),
	MFPR_168(GPIO115, 0x1CC, GPIO,    SSP1,        NONE,      NONE,        NONE,        NONE,     AC97,        NONE),
	MFPR_168(GPIO116, 0x1D0, GPIO,    SSP1_TX,     SSP1,      NONE,        NONE,        NONE,     AC97,        NONE),
	MFPR_168(GPIO117, 0x1D4, GPIO,    SSP1,        SSP1_TX,   NONE,        MMC2_CMD,    NONE,     AC97,        NONE),
	MFPR_168(GPIO118, 0x1D8, GPIO,    SSP2,        NONE,      NONE,        MMC2_CLK,    NONE,     AC97,        KP_MKIN),
	MFPR_168(GPIO119, 0x1DC, GPIO,    SSP2,        NONE,      NONE,        MMC2,        NONE,     AC97,        KP_MKIN),
	MFPR_168(GPIO120, 0x1E0, GPIO,    SSP2,        SSP2_TX,   NONE,        MMC2,        NONE,     NONE,        KP_MKIN),
	MFPR_168(GPIO121, 0x1E4, GPIO,    SSP2_TX,     SSP2,      NONE,        MMC2,        NONE,     NONE,        KP_MKIN),
	MFPR_168(GPIO122, 0x1E8, GPIO,    AC97_SYSCLK, SSP2,      PWM,         MMC2,        NONE,     NONE,        NONE),
	MFPR_168(PWR_SCL, 0x1EC, PWRI2C,  NONE,        NONE,      NONE,        NONE,        NONE,     GPIO,        MMC4),
	MFPR_168(PWR_SDA, 0x1F0, PWRI2C,  NONE,        NONE,      NONE,        NONE,        NONE,     GPIO,        NONE),
	MFPR_168(TDI,     0x1F4, JTAG,    PWM1,        UART2,     MMC4,        SSP5,        NONE,     XD,          MMC4),
	MFPR_168(TMS,     0x1F8, JTAG,    PWM,         UART2,     NONE,        SSP5,        NONE,     XD,          MMC4),
	MFPR_168(TCK,     0x1FC, JTAG,    PWM,         UART2,     UART2_TX,    SSP5,        NONE,     XD,          MMC4),
	MFPR_168(TDO,     0x200, JTAG,    PWM,         UART2_TX,  UART2,       SSP5_TX,     NONE,     XD,          MMC4),
	MFPR_168(TRST,    0x204, JTAG,    ONE_WIRE,    SSP2,      SSP3,        AC97_SYSCLK, NONE,     XD,          MMC4),
	MFPR_168(WAKEUP,  0x208, WAKEUP,  ONE_WIRE,    PWM1,      PWM,         SSP2,        NONE,     GPIO,        MMC4),
};

static const unsigned p168_jtag_pin1[] = {TDI, TMS, TCK, TDO, TRST};
static const unsigned p168_wakeup_pin1[] = {WAKEUP};
static const unsigned p168_ssp1rx_pin1[] = {GPIO114, GPIO115, GPIO116};
static const unsigned p168_ssp1tx_pin1[] = {GPIO117};
static const unsigned p168_ssp4rx_pin1[] = {GPIO102, GPIO103, GPIO104};
static const unsigned p168_ssp4tx_pin1[] = {GPIO105};
static const unsigned p168_ssp5rx_pin1[] = {GPIO86, GPIO88, GPIO89};
static const unsigned p168_ssp5tx_pin1[] = {GPIO87};
static const unsigned p168_i2c_pin1[] = {GPIO105, GPIO106};
static const unsigned p168_pwri2c_pin1[] = {PWR_SCL, PWR_SDA};
static const unsigned p168_mmc1_pin1[] = {GPIO40, GPIO41, GPIO43, GPIO46,
	GPIO49, GPIO51, GPIO52, GPIO53};
static const unsigned p168_mmc2_data_pin1[] = {GPIO90, GPIO91, GPIO92, GPIO93};
static const unsigned p168_mmc2_cmd_pin1[] = {GPIO94};
static const unsigned p168_mmc2_clk_pin1[] = {GPIO95};
static const unsigned p168_mmc3_data_pin1[] = {GPIO0, GPIO1, GPIO2, GPIO3,
	GPIO4, GPIO5, GPIO6, GPIO7};
static const unsigned p168_mmc3_cmd_pin1[] = {GPIO9};
static const unsigned p168_mmc3_clk_pin1[] = {GPIO8};
static const unsigned p168_eth_pin1[] = {GPIO92, GPIO93, GPIO100, GPIO101,
	GPIO103};
static const unsigned p168_ethtx_pin1[] = {GPIO86, GPIO87, GPIO88, GPIO89,
	GPIO90, GPIO91};
static const unsigned p168_ethrx_pin1[] = {GPIO94, GPIO95, GPIO96, GPIO97,
	GPIO98, GPIO99};
static const unsigned p168_uart1rx_pin1[] = {GPIO107};
static const unsigned p168_uart1tx_pin1[] = {GPIO108};
static const unsigned p168_uart3rx_pin1[] = {GPIO98, GPIO100, GPIO101};
static const unsigned p168_uart3tx_pin1[] = {GPIO99};
static const unsigned p168_msp_pin1[] = {GPIO40, GPIO41, GPIO42, GPIO43,
	GPIO44, GPIO50};
static const unsigned p168_ccic_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40,
	GPIO41, GPIO42, GPIO44, GPIO45, GPIO46, GPIO48, GPIO54, GPIO55};
static const unsigned p168_xd_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40,
	GPIO41, GPIO42, GPIO44, GPIO45, GPIO47, GPIO48, GPIO49, GPIO50,
	GPIO51, GPIO52};
static const unsigned p168_lcd_pin1[] = {GPIO56, GPIO57, GPIO58, GPIO59,
	GPIO60, GPIO61, GPIO62, GPIO63, GPIO64, GPIO65, GPIO66, GPIO67,
	GPIO68, GPIO69, GPIO70, GPIO71, GPIO72, GPIO73, GPIO74, GPIO75,
	GPIO76, GPIO77, GPIO78, GPIO79, GPIO80, GPIO81, GPIO82, GPIO83};
static const unsigned p168_dfio_pin1[] = {GPIO0, GPIO1, GPIO2, GPIO3,
	GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11, GPIO12,
	GPIO13, GPIO14, GPIO15};
static const unsigned p168_nand_pin1[] = {GPIO16, GPIO17, GPIO21, GPIO22,
	GPIO24, GPIO26};
static const unsigned p168_smc_pin1[] = {GPIO23, GPIO25, GPIO29, GPIO35,
	GPIO36};
static const unsigned p168_smccs0_pin1[] = {GPIO18};
static const unsigned p168_smccs1_pin1[] = {GPIO34};
static const unsigned p168_smcrdy_pin1[] = {GPIO28};
static const unsigned p168_ac97sysclk_pin1[] = {GPIO113};
static const unsigned p168_ac97_pin1[] = {GPIO114, GPIO115, GPIO117, GPIO118,
	GPIO119};
static const unsigned p168_cf_pin1[] = {GPIO19, GPIO20, GPIO23, GPIO25,
	GPIO28, GPIO29, GPIO30, GPIO31, GPIO32, GPIO33, GPIO34, GPIO35,
	GPIO36};
static const unsigned p168_kpmkin_pin1[] = {GPIO109, GPIO110, GPIO121};
static const unsigned p168_kpmkout_pin1[] = {GPIO111, GPIO112};
static const unsigned p168_gpio86_pin1[] = {WAKEUP};
static const unsigned p168_gpio86_pin2[] = {GPIO86};
static const unsigned p168_gpio87_pin1[] = {GPIO87};
static const unsigned p168_gpio87_pin2[] = {PWR_SDA};
static const unsigned p168_gpio88_pin1[] = {GPIO88};
static const unsigned p168_gpio88_pin2[] = {PWR_SCL};

static struct pxa3xx_pin_group pxa168_grps[] = {
	GRP_168("uart1rx-1", UART1, p168_uart1rx_pin1),
	GRP_168("uart1tx-1", UART1_TX, p168_uart1tx_pin1),
	GRP_168("uart3rx-1", UART3, p168_uart3rx_pin1),
	GRP_168("uart3tx-1", UART3_TX, p168_uart3tx_pin1),
	GRP_168("ssp1rx-1", SSP1, p168_ssp1rx_pin1),
	GRP_168("ssp1tx-1", SSP1_TX, p168_ssp1tx_pin1),
	GRP_168("ssp4rx-1", SSP4, p168_ssp4rx_pin1),
	GRP_168("ssp4tx-1", SSP4_TX, p168_ssp4tx_pin1),
	GRP_168("ssp5rx-1", SSP5, p168_ssp5rx_pin1),
	GRP_168("ssp5tx-1", SSP5_TX, p168_ssp5tx_pin1),
	GRP_168("jtag", JTAG, p168_jtag_pin1),
	GRP_168("wakeup", WAKEUP, p168_wakeup_pin1),
	GRP_168("i2c", I2C, p168_i2c_pin1),
	GRP_168("pwri2c", PWRI2C, p168_pwri2c_pin1),
	GRP_168("mmc1 8p1", MMC1, p168_mmc1_pin1),
	GRP_168("mmc2 4p1", MMC2, p168_mmc2_data_pin1),
	GRP_168("mmc2 cmd1", MMC2_CMD, p168_mmc2_cmd_pin1),
	GRP_168("mmc2 clk1", MMC2_CLK, p168_mmc2_clk_pin1),
	GRP_168("mmc3 8p1", MMC3, p168_mmc3_data_pin1),
	GRP_168("mmc3 cmd1", MMC3_CMD, p168_mmc3_cmd_pin1),
	GRP_168("mmc3 clk1", MMC3_CLK, p168_mmc3_clk_pin1),
	GRP_168("eth", ETH, p168_eth_pin1),
	GRP_168("eth rx", ETH_RX, p168_ethrx_pin1),
	GRP_168("eth tx", ETH_TX, p168_ethtx_pin1),
	GRP_168("msp", MSP, p168_msp_pin1),
	GRP_168("ccic", CCIC, p168_ccic_pin1),
	GRP_168("xd", XD, p168_xd_pin1),
	GRP_168("lcd", LCD, p168_lcd_pin1),
	GRP_168("dfio", DFIO, p168_dfio_pin1),
	GRP_168("nand", NAND, p168_nand_pin1),
	GRP_168("smc", SMC, p168_smc_pin1),
	GRP_168("smc cs0", SMC_CS0, p168_smccs0_pin1),
	GRP_168("smc cs1", SMC_CS1, p168_smccs1_pin1),
	GRP_168("smc rdy", SMC_RDY, p168_smcrdy_pin1),
	GRP_168("ac97 sysclk", AC97_SYSCLK, p168_ac97sysclk_pin1),
	GRP_168("ac97", AC97, p168_ac97_pin1),
	GRP_168("cf", CF, p168_cf_pin1),
	GRP_168("kp mkin 3p1", KP_MKIN, p168_kpmkin_pin1),
	GRP_168("kp mkout 2p1", KP_MKOUT, p168_kpmkout_pin1),
	GRP_168("gpio86-1", GPIO, p168_gpio86_pin1),
	GRP_168("gpio86-2", GPIO, p168_gpio86_pin2),
	GRP_168("gpio87-1", GPIO, p168_gpio87_pin1),
	GRP_168("gpio87-2", GPIO, p168_gpio87_pin2),
	GRP_168("gpio88-1", GPIO, p168_gpio88_pin1),
	GRP_168("gpio88-2", GPIO, p168_gpio88_pin2),
};

static const char * const p168_uart1rx_grps[] = {"uart1rx-1"};
static const char * const p168_uart1tx_grps[] = {"uart1tx-1"};
static const char * const p168_uart3rx_grps[] = {"uart3rx-1"};
static const char * const p168_uart3tx_grps[] = {"uart3tx-1"};
static const char * const p168_ssp1rx_grps[] = {"ssp1rx-1"};
static const char * const p168_ssp1tx_grps[] = {"ssp1tx-1"};
static const char * const p168_ssp4rx_grps[] = {"ssp4rx-1"};
static const char * const p168_ssp4tx_grps[] = {"ssp4tx-1"};
static const char * const p168_ssp5rx_grps[] = {"ssp5rx-1"};
static const char * const p168_ssp5tx_grps[] = {"ssp5tx-1"};
static const char * const p168_i2c_grps[] = {"i2c"};
static const char * const p168_pwri2c_grps[] = {"pwri2c"};
static const char * const p168_mmc1_grps[] = {"mmc1 8p1"};
static const char * const p168_mmc2_data_grps[] = {"mmc2 4p1"};
static const char * const p168_mmc2_cmd_grps[] = {"mmc2 cmd1"};
static const char * const p168_mmc2_clk_grps[] = {"mmc2 clk1"};
static const char * const p168_mmc3_data_grps[] = {"mmc3 8p1"};
static const char * const p168_mmc3_cmd_grps[] = {"mmc3 cmd1"};
static const char * const p168_mmc3_clk_grps[] = {"mmc3 clk1"};
static const char * const p168_eth_grps[] = {"eth"};
static const char * const p168_ethrx_grps[] = {"eth rx"};
static const char * const p168_ethtx_grps[] = {"eth tx"};
static const char * const p168_msp_grps[] = {"msp"};
static const char * const p168_ccic_grps[] = {"ccic"};
static const char * const p168_xd_grps[] = {"xd"};
static const char * const p168_lcd_grps[] = {"lcd"};
static const char * const p168_dfio_grps[] = {"dfio"};
static const char * const p168_nand_grps[] = {"nand"};
static const char * const p168_smc_grps[] = {"smc"};
static const char * const p168_smccs0_grps[] = {"smc cs0"};
static const char * const p168_smccs1_grps[] = {"smc cs1"};
static const char * const p168_smcrdy_grps[] = {"smc rdy"};
static const char * const p168_ac97sysclk_grps[] = {"ac97 sysclk"};
static const char * const p168_ac97_grps[] = {"ac97"};
static const char * const p168_cf_grps[] = {"cf"};
static const char * const p168_kpmkin_grps[] = {"kp mkin 3p1"};
static const char * const p168_kpmkout_grps[] = {"kp mkout 2p1"};
static const char * const p168_gpio86_grps[] = {"gpio86-1", "gpio86-2"};
static const char * const p168_gpio87_grps[] = {"gpio87-1", "gpio87-2"};
static const char * const p168_gpio88_grps[] = {"gpio88-1", "gpio88-2"};

static struct pxa3xx_pmx_func pxa168_funcs[] = {
	{"uart1 rx",	ARRAY_AND_SIZE(p168_uart1rx_grps)},
	{"uart1 tx",	ARRAY_AND_SIZE(p168_uart1tx_grps)},
	{"uart3 rx",	ARRAY_AND_SIZE(p168_uart3rx_grps)},
	{"uart3 tx",	ARRAY_AND_SIZE(p168_uart3tx_grps)},
	{"ssp1 rx",	ARRAY_AND_SIZE(p168_ssp1rx_grps)},
	{"ssp1 tx",	ARRAY_AND_SIZE(p168_ssp1tx_grps)},
	{"ssp4 rx",	ARRAY_AND_SIZE(p168_ssp4rx_grps)},
	{"ssp4 tx",	ARRAY_AND_SIZE(p168_ssp4tx_grps)},
	{"ssp5 rx",	ARRAY_AND_SIZE(p168_ssp5rx_grps)},
	{"ssp5 tx",	ARRAY_AND_SIZE(p168_ssp5tx_grps)},
	{"i2c",		ARRAY_AND_SIZE(p168_i2c_grps)},
	{"pwri2c",	ARRAY_AND_SIZE(p168_pwri2c_grps)},
	{"mmc1",	ARRAY_AND_SIZE(p168_mmc1_grps)},
	{"mmc2",	ARRAY_AND_SIZE(p168_mmc2_data_grps)},
	{"mmc2 cmd",	ARRAY_AND_SIZE(p168_mmc2_cmd_grps)},
	{"mmc2 clk",	ARRAY_AND_SIZE(p168_mmc2_clk_grps)},
	{"mmc3",	ARRAY_AND_SIZE(p168_mmc3_data_grps)},
	{"mmc3 cmd",	ARRAY_AND_SIZE(p168_mmc3_cmd_grps)},
	{"mmc3 clk",	ARRAY_AND_SIZE(p168_mmc3_clk_grps)},
	{"eth",		ARRAY_AND_SIZE(p168_eth_grps)},
	{"eth rx",	ARRAY_AND_SIZE(p168_ethrx_grps)},
	{"eth tx",	ARRAY_AND_SIZE(p168_ethtx_grps)},
	{"msp",		ARRAY_AND_SIZE(p168_msp_grps)},
	{"ccic",	ARRAY_AND_SIZE(p168_ccic_grps)},
	{"xd",		ARRAY_AND_SIZE(p168_xd_grps)},
	{"lcd",		ARRAY_AND_SIZE(p168_lcd_grps)},
	{"dfio",	ARRAY_AND_SIZE(p168_dfio_grps)},
	{"nand",	ARRAY_AND_SIZE(p168_nand_grps)},
	{"smc",		ARRAY_AND_SIZE(p168_smc_grps)},
	{"smc cs0",	ARRAY_AND_SIZE(p168_smccs0_grps)},
	{"smc cs1",	ARRAY_AND_SIZE(p168_smccs1_grps)},
	{"smc rdy",	ARRAY_AND_SIZE(p168_smcrdy_grps)},
	{"ac97",	ARRAY_AND_SIZE(p168_ac97_grps)},
	{"ac97 sysclk",	ARRAY_AND_SIZE(p168_ac97sysclk_grps)},
	{"cf",		ARRAY_AND_SIZE(p168_cf_grps)},
	{"kpmkin",	ARRAY_AND_SIZE(p168_kpmkin_grps)},
	{"kpmkout",	ARRAY_AND_SIZE(p168_kpmkout_grps)},
	{"gpio86",	ARRAY_AND_SIZE(p168_gpio86_grps)},
	{"gpio87",	ARRAY_AND_SIZE(p168_gpio87_grps)},
	{"gpio88",	ARRAY_AND_SIZE(p168_gpio88_grps)},
};

static struct pinctrl_desc pxa168_pctrl_desc = {
	.name		= "pxa168-pinctrl",
	.owner		= THIS_MODULE,
};

static struct pxa3xx_pinmux_info pxa168_info = {
	.mfp		= pxa168_mfp,
	.num_mfp	= ARRAY_SIZE(pxa168_mfp),
	.grps		= pxa168_grps,
	.num_grps	= ARRAY_SIZE(pxa168_grps),
	.funcs		= pxa168_funcs,
	.num_funcs	= ARRAY_SIZE(pxa168_funcs),
	.num_gpio	= 128,
	.desc		= &pxa168_pctrl_desc,
	.pads		= pxa168_pads,
	.num_pads	= ARRAY_SIZE(pxa168_pads),

	.cputype	= PINCTRL_PXA168,
	.ds_mask	= PXA168_DS_MASK,
	.ds_shift	= PXA168_DS_SHIFT,
};

static int __devinit pxa168_pinmux_probe(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_register(pdev, &pxa168_info);
}

static int __devexit pxa168_pinmux_remove(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_unregister(pdev);
}

static struct platform_driver pxa168_pinmux_driver = {
	.driver = {
		.name	= "pxa168-pinmux",
		.owner	= THIS_MODULE,
	},
	.probe	= pxa168_pinmux_probe,
	.remove	= __devexit_p(pxa168_pinmux_remove),
};

static int __init pxa168_pinmux_init(void)
{
	return platform_driver_register(&pxa168_pinmux_driver);
}
core_initcall_sync(pxa168_pinmux_init);

static void __exit pxa168_pinmux_exit(void)
{
	platform_driver_unregister(&pxa168_pinmux_driver);
}
module_exit(pxa168_pinmux_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("PXA3xx pin control driver");
MODULE_LICENSE("GPL v2");
