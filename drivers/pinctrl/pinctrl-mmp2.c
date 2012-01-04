/*
 *  linux/drivers/pinctrl/pinmux-mmp2.c
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

#define MMP2_DS_MASK		0x1800
#define MMP2_DS_SHIFT		11
#define MMP2_SLEEP_MASK		0x38
#define MMP2_SLEEP_SELECT	(1 << 9)
#define MMP2_SLEEP_DATA		(1 << 8)
#define MMP2_SLEEP_DIR		(1 << 7)

#define MFPR_MMP2(a, r, f0, f1, f2, f3, f4, f5, f6, f7)		\
	{							\
		.name = #a,					\
		.pin = a,					\
		.mfpr = r,					\
		.func = {					\
			MMP2_MUX_##f0,				\
			MMP2_MUX_##f1,				\
			MMP2_MUX_##f2,				\
			MMP2_MUX_##f3,				\
			MMP2_MUX_##f4,				\
			MMP2_MUX_##f5,				\
			MMP2_MUX_##f6,				\
			MMP2_MUX_##f7,				\
		},						\
	}

#define GRP_MMP2(a, m, p)		\
	{ .name = a, .mux = MMP2_MUX_##m, .pins = p, .npins = ARRAY_SIZE(p), }

/* 174 pins */
enum mmp2_pin_list {
	/* 0~168: GPIO0~GPIO168 */
	TWSI4_SCL = 169,
	TWSI4_SDA, /* 170 */
	G_CLKREQ,
	VCXO_REQ,
	VCXO_OUT,
};

enum mmp2_mux {
	/* PXA3xx_MUX_GPIO = 0 (predefined in pinctrl-pxa3xx.h) */
	MMP2_MUX_GPIO = 0,
	MMP2_MUX_G_CLKREQ,
	MMP2_MUX_VCXO_REQ,
	MMP2_MUX_VCXO_OUT,
	MMP2_MUX_KP_MK,
	MMP2_MUX_KP_DK,
	MMP2_MUX_CCIC1,
	MMP2_MUX_CCIC2,
	MMP2_MUX_SPI,
	MMP2_MUX_SSPA2,
	MMP2_MUX_ROT,
	MMP2_MUX_I2S,
	MMP2_MUX_TB,
	MMP2_MUX_CAM2,
	MMP2_MUX_HDMI,
	MMP2_MUX_TWSI2,
	MMP2_MUX_TWSI3,
	MMP2_MUX_TWSI4,
	MMP2_MUX_TWSI5,
	MMP2_MUX_TWSI6,
	MMP2_MUX_UART1,
	MMP2_MUX_UART2,
	MMP2_MUX_UART3,
	MMP2_MUX_UART4,
	MMP2_MUX_SSP1_RX,
	MMP2_MUX_SSP1_FRM,
	MMP2_MUX_SSP1_TXRX,
	MMP2_MUX_SSP2_RX,
	MMP2_MUX_SSP2_FRM,
	MMP2_MUX_SSP1,
	MMP2_MUX_SSP2,
	MMP2_MUX_SSP3,
	MMP2_MUX_SSP4,
	MMP2_MUX_MMC1,
	MMP2_MUX_MMC2,
	MMP2_MUX_MMC3,
	MMP2_MUX_MMC4,
	MMP2_MUX_ULPI,
	MMP2_MUX_AC,
	MMP2_MUX_CA,
	MMP2_MUX_PWM,
	MMP2_MUX_USIM,
	MMP2_MUX_TIPU,
	MMP2_MUX_PLL,
	MMP2_MUX_NAND,
	MMP2_MUX_FSIC,
	MMP2_MUX_SLEEP_IND,
	MMP2_MUX_EXT_DMA,
	MMP2_MUX_ONE_WIRE,
	MMP2_MUX_LCD,
	MMP2_MUX_SMC,
	MMP2_MUX_SMC_INT,
	MMP2_MUX_MSP,
	MMP2_MUX_G_CLKOUT,
	MMP2_MUX_32K_CLKOUT,
	MMP2_MUX_PRI_JTAG,
	MMP2_MUX_AAS_JTAG,
	MMP2_MUX_AAS_GPIO,
	MMP2_MUX_AAS_SPI,
	MMP2_MUX_AAS_TWSI,
	MMP2_MUX_AAS_DEU_EX,
	MMP2_MUX_NONE = 0xffff,
};

static struct pinctrl_pin_desc mmp2_pads[] = {
	/*
	 * The name indicates function 0 of this pin.
	 * After reset, function 0 is the default function of pin.
	 */
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
	PINCTRL_PIN(GPIO128, "GPIO128"),
	PINCTRL_PIN(GPIO129, "GPIO129"),
	PINCTRL_PIN(GPIO130, "GPIO130"),
	PINCTRL_PIN(GPIO131, "GPIO131"),
	PINCTRL_PIN(GPIO132, "GPIO132"),
	PINCTRL_PIN(GPIO133, "GPIO133"),
	PINCTRL_PIN(GPIO134, "GPIO134"),
	PINCTRL_PIN(GPIO135, "GPIO135"),
	PINCTRL_PIN(GPIO136, "GPIO136"),
	PINCTRL_PIN(GPIO137, "GPIO137"),
	PINCTRL_PIN(GPIO138, "GPIO138"),
	PINCTRL_PIN(GPIO139, "GPIO139"),
	PINCTRL_PIN(GPIO140, "GPIO140"),
	PINCTRL_PIN(GPIO141, "GPIO141"),
	PINCTRL_PIN(GPIO142, "GPIO142"),
	PINCTRL_PIN(GPIO143, "GPIO143"),
	PINCTRL_PIN(GPIO144, "GPIO144"),
	PINCTRL_PIN(GPIO145, "GPIO145"),
	PINCTRL_PIN(GPIO146, "GPIO146"),
	PINCTRL_PIN(GPIO147, "GPIO147"),
	PINCTRL_PIN(GPIO148, "GPIO148"),
	PINCTRL_PIN(GPIO149, "GPIO149"),
	PINCTRL_PIN(GPIO150, "GPIO150"),
	PINCTRL_PIN(GPIO151, "GPIO151"),
	PINCTRL_PIN(GPIO152, "GPIO152"),
	PINCTRL_PIN(GPIO153, "GPIO153"),
	PINCTRL_PIN(GPIO154, "GPIO154"),
	PINCTRL_PIN(GPIO155, "GPIO155"),
	PINCTRL_PIN(GPIO156, "GPIO156"),
	PINCTRL_PIN(GPIO157, "GPIO157"),
	PINCTRL_PIN(GPIO158, "GPIO158"),
	PINCTRL_PIN(GPIO159, "GPIO159"),
	PINCTRL_PIN(GPIO160, "GPIO160"),
	PINCTRL_PIN(GPIO161, "GPIO161"),
	PINCTRL_PIN(GPIO162, "GPIO162"),
	PINCTRL_PIN(GPIO163, "GPIO163"),
	PINCTRL_PIN(GPIO164, "GPIO164"),
	PINCTRL_PIN(GPIO165, "GPIO165"),
	PINCTRL_PIN(GPIO166, "GPIO166"),
	PINCTRL_PIN(GPIO167, "GPIO167"),
	PINCTRL_PIN(GPIO168, "GPIO168"),
	PINCTRL_PIN(TWSI4_SCL, "TWSI4_SCL"),
	PINCTRL_PIN(TWSI4_SDA, "TWSI4_SDA"),
	PINCTRL_PIN(G_CLKREQ, "G_CLKREQ"),
	PINCTRL_PIN(VCXO_REQ, "VCXO_REQ"),
	PINCTRL_PIN(VCXO_OUT, "VCXO_OUT"),
};

struct pxa3xx_mfp_pin mmp2_mfp[] = {
	/*       pin         offs   f0        f1          f2          f3          f4          f5        f6        f7  */
	MFPR_MMP2(GPIO0,     0x054, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO1,     0x058, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO2,     0x05C, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO3,     0x060, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO4,     0x064, GPIO,     KP_MK,      NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO5,     0x068, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO6,     0x06C, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO7,     0x070, GPIO,     KP_MK,      NONE,       SPI,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO8,     0x074, GPIO,     KP_MK,      NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO9,     0x078, GPIO,     KP_MK,      NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO10,    0x07C, GPIO,     KP_MK,      NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO11,    0x080, GPIO,     KP_MK,      NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO12,    0x084, GPIO,     KP_MK,      NONE,       CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO13,    0x088, GPIO,     KP_MK,      NONE,       CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO14,    0x08C, GPIO,     KP_MK,      NONE,       CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO15,    0x090, GPIO,     KP_MK,      KP_DK,      CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO16,    0x094, GPIO,     KP_DK,      ROT,        CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO17,    0x098, GPIO,     KP_DK,      ROT,        CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO18,    0x09C, GPIO,     KP_DK,      ROT,        CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO19,    0x0A0, GPIO,     KP_DK,      ROT,        CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO20,    0x0A4, GPIO,     KP_DK,      TB,         CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO21,    0x0A8, GPIO,     KP_DK,      TB,         CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO22,    0x0AC, GPIO,     KP_DK,      TB,         CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO23,    0x0B0, GPIO,     KP_DK,      TB,         CCIC1,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO24,    0x0B4, GPIO,     I2S,        VCXO_OUT,   NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO25,    0x0B8, GPIO,     I2S,        HDMI,       SSPA2,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO26,    0x0BC, GPIO,     I2S,        HDMI,       SSPA2,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO27,    0x0C0, GPIO,     I2S,        HDMI,       SSPA2,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO28,    0x0C4, GPIO,     I2S,        NONE,       SSPA2,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO29,    0x0C8, GPIO,     UART1,      KP_MK,      NONE,       NONE,       NONE,     AAS_SPI,  NONE),
	MFPR_MMP2(GPIO30,    0x0CC, GPIO,     UART1,      KP_MK,      NONE,       NONE,       NONE,     AAS_SPI,  NONE),
	MFPR_MMP2(GPIO31,    0x0D0, GPIO,     UART1,      KP_MK,      NONE,       NONE,       NONE,     AAS_SPI,  NONE),
	MFPR_MMP2(GPIO32,    0x0D4, GPIO,     UART1,      KP_MK,      NONE,       NONE,       NONE,     AAS_SPI,  NONE),
	MFPR_MMP2(GPIO33,    0x0D8, GPIO,     SSPA2,      I2S,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO34,    0x0DC, GPIO,     SSPA2,      I2S,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO35,    0x0E0, GPIO,     SSPA2,      I2S,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO36,    0x0E4, GPIO,     SSPA2,      I2S,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO37,    0x0E8, GPIO,     MMC2,       SSP1,       TWSI2,      UART2,      UART3,    AAS_SPI,  AAS_TWSI),
	MFPR_MMP2(GPIO38,    0x0EC, GPIO,     MMC2,       SSP1,       TWSI2,      UART2,      UART3,    AAS_SPI,  AAS_TWSI),
	MFPR_MMP2(GPIO39,    0x0F0, GPIO,     MMC2,       SSP1,       TWSI2,      UART2,      UART3,    AAS_SPI,  AAS_TWSI),
	MFPR_MMP2(GPIO40,    0x0F4, GPIO,     MMC2,       SSP1,       TWSI2,      UART2,      UART3,    AAS_SPI,  AAS_TWSI),
	MFPR_MMP2(GPIO41,    0x0F8, GPIO,     MMC2,       TWSI5,      NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO42,    0x0FC, GPIO,     MMC2,       TWSI5,      NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO43,    0x100, GPIO,     TWSI2,      UART4,      SSP1,       UART2,      UART3,    NONE,     AAS_TWSI),
	MFPR_MMP2(GPIO44,    0x104, GPIO,     TWSI2,      UART4,      SSP1,       UART2,      UART3,    NONE,     AAS_TWSI),
	MFPR_MMP2(GPIO45,    0x108, GPIO,     UART1,      UART4,      SSP1,       UART2,      UART3,    NONE,     NONE),
	MFPR_MMP2(GPIO46,    0x10C, GPIO,     UART1,      UART4,      SSP1,       UART2,      UART3,    NONE,     NONE),
	MFPR_MMP2(GPIO47,    0x110, GPIO,     UART2,      SSP2,       TWSI6,      CAM2,       AAS_SPI,  AAS_GPIO, NONE),
	MFPR_MMP2(GPIO48,    0x114, GPIO,     UART2,      SSP2,       TWSI6,      CAM2,       AAS_SPI,  AAS_GPIO, NONE),
	MFPR_MMP2(GPIO49,    0x118, GPIO,     UART2,      SSP2,       PWM,        CCIC2,      AAS_SPI,  NONE,     NONE),
	MFPR_MMP2(GPIO50,    0x11C, GPIO,     UART2,      SSP2,       PWM,        CCIC2,      AAS_SPI,  NONE,     NONE),
	MFPR_MMP2(GPIO51,    0x120, GPIO,     UART3,      ROT,        AAS_GPIO,   PWM,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO52,    0x124, GPIO,     UART3,      ROT,        AAS_GPIO,   PWM,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO53,    0x128, GPIO,     UART3,      TWSI2,      VCXO_REQ,   NONE,       PWM,      NONE,     AAS_TWSI),
	MFPR_MMP2(GPIO54,    0x12C, GPIO,     UART3,      TWSI2,      VCXO_OUT,   HDMI,       PWM,      NONE,     AAS_TWSI),
	MFPR_MMP2(GPIO55,    0x130, GPIO,     SSP2,       SSP1,       UART2,      ROT,        TWSI2,    SSP3,     AAS_TWSI),
	MFPR_MMP2(GPIO56,    0x134, GPIO,     SSP2,       SSP1,       UART2,      ROT,        TWSI2,    KP_DK,    AAS_TWSI),
	MFPR_MMP2(GPIO57,    0x138, GPIO,     SSP2_RX,    SSP1_TXRX,  SSP2_FRM,   SSP1_RX,    VCXO_REQ, KP_DK,    NONE),
	MFPR_MMP2(GPIO58,    0x13C, GPIO,     SSP2,       SSP1_RX,    SSP1_FRM,   SSP1_TXRX,  VCXO_REQ, KP_DK,    NONE),
	MFPR_MMP2(GPIO59,    0x280, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      UART3,    UART4,    NONE),
	MFPR_MMP2(GPIO60,    0x284, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      UART3,    UART4,    NONE),
	MFPR_MMP2(GPIO61,    0x288, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      UART3,    HDMI,     NONE),
	MFPR_MMP2(GPIO62,    0x28C, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      UART3,    NONE,     NONE),
	MFPR_MMP2(GPIO63,    0x290, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      UART4,    NONE),
	MFPR_MMP2(GPIO64,    0x294, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      UART4,    NONE),
	MFPR_MMP2(GPIO65,    0x298, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      UART4,    NONE),
	MFPR_MMP2(GPIO66,    0x29C, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      UART4,    NONE),
	MFPR_MMP2(GPIO67,    0x2A0, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      NONE,     NONE),
	MFPR_MMP2(GPIO68,    0x2A4, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      LCD,      NONE),
	MFPR_MMP2(GPIO69,    0x2A8, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      NONE,     LCD,      NONE),
	MFPR_MMP2(GPIO70,    0x2AC, GPIO,     CCIC1,      ULPI,       MMC3,       CCIC2,      MSP,      LCD,      NONE),
	MFPR_MMP2(GPIO71,    0x2B0, GPIO,     TWSI3,      NONE,       PWM,        NONE,       NONE,     LCD,      AAS_TWSI),
	MFPR_MMP2(GPIO72,    0x2B4, GPIO,     TWSI3,      HDMI,       PWM,        NONE,       NONE,     LCD,      AAS_TWSI),
	MFPR_MMP2(GPIO73,    0x2B8, GPIO,     VCXO_REQ,   32K_CLKOUT, PWM,        VCXO_OUT,   NONE,     LCD,      NONE),
	MFPR_MMP2(GPIO74,    0x170, GPIO,     LCD,        SMC,        MMC4,       SSP3,       UART2,    UART4,    TIPU),
	MFPR_MMP2(GPIO75,    0x174, GPIO,     LCD,        SMC,        MMC4,       SSP3,       UART2,    UART4,    TIPU),
	MFPR_MMP2(GPIO76,    0x178, GPIO,     LCD,        SMC,        MMC4,       SSP3,       UART2,    UART4,    TIPU),
	MFPR_MMP2(GPIO77,    0x17C, GPIO,     LCD,        SMC,        MMC4,       SSP3,       UART2,    UART4,    TIPU),
	MFPR_MMP2(GPIO78,    0x180, GPIO,     LCD,        HDMI,       MMC4,       NONE,       SSP4,     AAS_SPI,  TIPU),
	MFPR_MMP2(GPIO79,    0x184, GPIO,     LCD,        AAS_GPIO,   MMC4,       NONE,       SSP4,     AAS_SPI,  TIPU),
	MFPR_MMP2(GPIO80,    0x188, GPIO,     LCD,        AAS_GPIO,   MMC4,       NONE,       SSP4,     AAS_SPI,  TIPU),
	MFPR_MMP2(GPIO81,    0x18C, GPIO,     LCD,        AAS_GPIO,   MMC4,       NONE,       SSP4,     AAS_SPI,  TIPU),
	MFPR_MMP2(GPIO82,    0x190, GPIO,     LCD,        NONE,       MMC4,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO83,    0x194, GPIO,     LCD,        NONE,       MMC4,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO84,    0x198, GPIO,     LCD,        SMC,        MMC2,       NONE,       TWSI5,    AAS_TWSI, TIPU),
	MFPR_MMP2(GPIO85,    0x19C, GPIO,     LCD,        SMC,        MMC2,       NONE,       TWSI5,    AAS_TWSI, TIPU),
	MFPR_MMP2(GPIO86,    0x1A0, GPIO,     LCD,        SMC,        MMC2,       NONE,       TWSI6,    CCIC2,    TIPU),
	MFPR_MMP2(GPIO87,    0x1A4, GPIO,     LCD,        SMC,        MMC2,       NONE,       TWSI6,    CCIC2,    TIPU),
	MFPR_MMP2(GPIO88,    0x1A8, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO89,    0x1AC, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO90,    0x1B0, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO91,    0x1B4, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO92,    0x1B8, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO93,    0x1BC, GPIO,     LCD,        AAS_GPIO,   MMC2,       NONE,       NONE,     CCIC2,    TIPU),
	MFPR_MMP2(GPIO94,    0x1C0, GPIO,     LCD,        AAS_GPIO,   SPI,        NONE,       AAS_SPI,  CCIC2,    TIPU),
	MFPR_MMP2(GPIO95,    0x1C4, GPIO,     LCD,        TWSI3,      SPI,        AAS_DEU_EX, AAS_SPI,  CCIC2,    TIPU),
	MFPR_MMP2(GPIO96,    0x1C8, GPIO,     LCD,        TWSI3,      SPI,        AAS_DEU_EX, AAS_SPI,  NONE,     TIPU),
	MFPR_MMP2(GPIO97,    0x1CC, GPIO,     LCD,        TWSI6,      SPI,        AAS_DEU_EX, AAS_SPI,  NONE,     TIPU),
	MFPR_MMP2(GPIO98,    0x1D0, GPIO,     LCD,        TWSI6,      SPI,        ONE_WIRE,   NONE,     NONE,     TIPU),
	MFPR_MMP2(GPIO99,    0x1D4, GPIO,     LCD,        SMC,        SPI,        TWSI5,      NONE,     NONE,     TIPU),
	MFPR_MMP2(GPIO100,   0x1D8, GPIO,     LCD,        SMC,        SPI,        TWSI5,      NONE,     NONE,     TIPU),
	MFPR_MMP2(GPIO101,   0x1DC, GPIO,     LCD,        SMC,        SPI,        NONE,       NONE,     NONE,     TIPU),
	MFPR_MMP2(GPIO102,   0x000, USIM,     GPIO,       FSIC,       KP_DK,      LCD,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO103,   0x004, USIM,     GPIO,       FSIC,       KP_DK,      LCD,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO104,   0x1FC, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO105,   0x1F8, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO106,   0x1F4, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO107,   0x1F0, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO108,   0x21C, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO109,   0x218, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO110,   0x214, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO111,   0x200, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO112,   0x244, NAND,     GPIO,       MMC3,       SMC,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO113,   0x25C, SMC,      GPIO,       EXT_DMA,    MMC3,       SMC,        HDMI,     NONE,     NONE),
	MFPR_MMP2(GPIO114,   0x164, G_CLKOUT, 32K_CLKOUT, HDMI,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO115,   0x260, GPIO,     NONE,       AC,         UART4,      UART3,      SSP1,     NONE,     NONE),
	MFPR_MMP2(GPIO116,   0x264, GPIO,     NONE,       AC,         UART4,      UART3,      SSP1,     NONE,     NONE),
	MFPR_MMP2(GPIO117,   0x268, GPIO,     NONE,       AC,         UART4,      UART3,      SSP1,     NONE,     NONE),
	MFPR_MMP2(GPIO118,   0x26C, GPIO,     NONE,       AC,         UART4,      UART3,      SSP1,     NONE,     NONE),
	MFPR_MMP2(GPIO119,   0x270, GPIO,     NONE,       CA,         SSP3,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO120,   0x274, GPIO,     NONE,       CA,         SSP3,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO121,   0x278, GPIO,     NONE,       CA,         SSP3,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO122,   0x27C, GPIO,     NONE,       CA,         SSP3,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO123,   0x148, GPIO,     SLEEP_IND,  ONE_WIRE,   32K_CLKOUT, NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO124,   0x00C, GPIO,     MMC1,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO125,   0x010, GPIO,     MMC1,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO126,   0x014, GPIO,     MMC1,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO127,   0x018, GPIO,     NONE,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO128,   0x01C, GPIO,     NONE,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO129,   0x020, GPIO,     MMC1,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO130,   0x024, GPIO,     MMC1,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO131,   0x028, GPIO,     MMC1,       NONE,       MSP,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO132,   0x02C, GPIO,     MMC1,       PRI_JTAG,   MSP,        SSP3,       AAS_JTAG, NONE,     NONE),
	MFPR_MMP2(GPIO133,   0x030, GPIO,     MMC1,       PRI_JTAG,   MSP,        SSP3,       AAS_JTAG, NONE,     NONE),
	MFPR_MMP2(GPIO134,   0x034, GPIO,     MMC1,       PRI_JTAG,   MSP,        SSP3,       AAS_JTAG, NONE,     NONE),
	MFPR_MMP2(GPIO135,   0x038, GPIO,     NONE,       LCD,        MMC3,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO136,   0x03C, GPIO,     MMC1,       PRI_JTAG,   MSP,        SSP3,       AAS_JTAG, NONE,     NONE),
	MFPR_MMP2(GPIO137,   0x040, GPIO,     HDMI,       LCD,        MSP,        NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO138,   0x044, GPIO,     NONE,       LCD,        MMC3,       SMC,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO139,   0x048, GPIO,     MMC1,       PRI_JTAG,   MSP,        NONE,       AAS_JTAG, NONE,     NONE),
	MFPR_MMP2(GPIO140,   0x04C, GPIO,     MMC1,       LCD,        NONE,       NONE,       UART2,    UART1,    NONE),
	MFPR_MMP2(GPIO141,   0x050, GPIO,     MMC1,       LCD,        NONE,       NONE,       UART2,    UART1,    NONE),
	MFPR_MMP2(GPIO142,   0x008, USIM,     GPIO,       FSIC,       KP_DK,      NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO143,   0x220, NAND,     GPIO,       SMC,        NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO144,   0x224, NAND,     GPIO,       SMC_INT,    SMC,        NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO145,   0x228, SMC,      GPIO,       NONE,       NONE,       SMC,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO146,   0x22C, SMC,      GPIO,       NONE,       NONE,       SMC,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO147,   0x230, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO148,   0x234, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO149,   0x238, NAND,     GPIO,       NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO150,   0x23C, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO151,   0x240, SMC,      GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO152,   0x248, SMC,      GPIO,       NONE,       NONE,       SMC,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO153,   0x24C, SMC,      GPIO,       NONE,       NONE,       SMC,        NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO154,   0x254, SMC_INT,  GPIO,       SMC,        NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO155,   0x258, EXT_DMA,  GPIO,       SMC,        NONE,       EXT_DMA,    NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO156,   0x14C, PRI_JTAG, GPIO,       PWM,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO157,   0x150, PRI_JTAG, GPIO,       PWM,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO158,   0x154, PRI_JTAG, GPIO,       PWM,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO159,   0x158, PRI_JTAG, GPIO,       PWM,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO160,   0x250, NAND,     GPIO,       SMC,        NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO161,   0x210, NAND,     GPIO,       NONE,       NONE,       NAND,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO162,   0x20C, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO163,   0x208, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO164,   0x204, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO165,   0x1EC, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO166,   0x1E8, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO167,   0x1E4, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(GPIO168,   0x1E0, NAND,     GPIO,       MMC3,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(TWSI4_SCL, 0x2BC, TWSI4,    LCD,        NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(TWSI4_SDA, 0x2C0, TWSI4,    LCD,        NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(G_CLKREQ,  0x160, G_CLKREQ, ONE_WIRE,   NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(VCXO_REQ,  0x168, VCXO_REQ, ONE_WIRE,   PLL,        NONE,       NONE,       NONE,     NONE,     NONE),
	MFPR_MMP2(VCXO_OUT,  0x16C, VCXO_OUT, 32K_CLKOUT, NONE,       NONE,       NONE,       NONE,     NONE,     NONE),
};

static const unsigned mmp2_uart1_pin1[] = {GPIO29, GPIO30, GPIO31, GPIO32};
static const unsigned mmp2_uart1_pin2[] = {GPIO45, GPIO46};
static const unsigned mmp2_uart1_pin3[] = {GPIO140, GPIO141};
static const unsigned mmp2_uart2_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40};
static const unsigned mmp2_uart2_pin2[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned mmp2_uart2_pin3[] = {GPIO47, GPIO48, GPIO49, GPIO50};
static const unsigned mmp2_uart2_pin4[] = {GPIO74, GPIO75, GPIO76, GPIO77};
static const unsigned mmp2_uart2_pin5[] = {GPIO55, GPIO56};
static const unsigned mmp2_uart2_pin6[] = {GPIO140, GPIO141};
static const unsigned mmp2_uart3_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40};
static const unsigned mmp2_uart3_pin2[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned mmp2_uart3_pin3[] = {GPIO51, GPIO52, GPIO53, GPIO54};
static const unsigned mmp2_uart3_pin4[] = {GPIO59, GPIO60, GPIO61, GPIO62};
static const unsigned mmp2_uart3_pin5[] = {GPIO115, GPIO116, GPIO117, GPIO118};
static const unsigned mmp2_uart3_pin6[] = {GPIO51, GPIO52};
static const unsigned mmp2_uart4_pin1[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned mmp2_uart4_pin2[] = {GPIO63, GPIO64, GPIO65, GPIO66};
static const unsigned mmp2_uart4_pin3[] = {GPIO74, GPIO75, GPIO76, GPIO77};
static const unsigned mmp2_uart4_pin4[] = {GPIO115, GPIO116, GPIO117, GPIO118};
static const unsigned mmp2_uart4_pin5[] = {GPIO59, GPIO60};
static const unsigned mmp2_kpdk_pin1[] = {GPIO16, GPIO17, GPIO18, GPIO19};
static const unsigned mmp2_kpdk_pin2[] = {GPIO16, GPIO17};
static const unsigned mmp2_twsi2_pin1[] = {GPIO37, GPIO38};
static const unsigned mmp2_twsi2_pin2[] = {GPIO39, GPIO40};
static const unsigned mmp2_twsi2_pin3[] = {GPIO43, GPIO44};
static const unsigned mmp2_twsi2_pin4[] = {GPIO53, GPIO54};
static const unsigned mmp2_twsi2_pin5[] = {GPIO55, GPIO56};
static const unsigned mmp2_twsi3_pin1[] = {GPIO71, GPIO72};
static const unsigned mmp2_twsi3_pin2[] = {GPIO95, GPIO96};
static const unsigned mmp2_twsi4_pin1[] = {TWSI4_SCL, TWSI4_SDA};
static const unsigned mmp2_twsi5_pin1[] = {GPIO41, GPIO42};
static const unsigned mmp2_twsi5_pin2[] = {GPIO84, GPIO85};
static const unsigned mmp2_twsi5_pin3[] = {GPIO99, GPIO100};
static const unsigned mmp2_twsi6_pin1[] = {GPIO47, GPIO48};
static const unsigned mmp2_twsi6_pin2[] = {GPIO86, GPIO87};
static const unsigned mmp2_twsi6_pin3[] = {GPIO97, GPIO98};
static const unsigned mmp2_ccic1_pin1[] = {GPIO12, GPIO13, GPIO14, GPIO15,
	GPIO16, GPIO17, GPIO18, GPIO19, GPIO20, GPIO21, GPIO22, GPIO23};
static const unsigned mmp2_ccic1_pin2[] = {GPIO59, GPIO60, GPIO61, GPIO62,
	GPIO63, GPIO64, GPIO65, GPIO66, GPIO67, GPIO68, GPIO69, GPIO70};
static const unsigned mmp2_ccic2_pin1[] = {GPIO59, GPIO60, GPIO61, GPIO62,
	GPIO63, GPIO64, GPIO65, GPIO66, GPIO67, GPIO68, GPIO69, GPIO70};
static const unsigned mmp2_ccic2_pin2[] = {GPIO82, GPIO83, GPIO86, GPIO87,
	GPIO88, GPIO89, GPIO90, GPIO91, GPIO92, GPIO93, GPIO94, GPIO95};
static const unsigned mmp2_ulpi_pin1[] = {GPIO59, GPIO60, GPIO61, GPIO62,
	GPIO63, GPIO64, GPIO65, GPIO66, GPIO67, GPIO68, GPIO69, GPIO70};
static const unsigned mmp2_ro_pin1[] = {GPIO16, GPIO17};
static const unsigned mmp2_ro_pin2[] = {GPIO18, GPIO19};
static const unsigned mmp2_ro_pin3[] = {GPIO51, GPIO52};
static const unsigned mmp2_ro_pin4[] = {GPIO55, GPIO56};
static const unsigned mmp2_i2s_pin1[] = {GPIO24, GPIO25, GPIO26, GPIO27,
	GPIO28};
static const unsigned mmp2_i2s_pin2[] = {GPIO33, GPIO34, GPIO35, GPIO36};
static const unsigned mmp2_ssp1_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40};
static const unsigned mmp2_ssp1_pin2[] = {GPIO43, GPIO44, GPIO45, GPIO46};
static const unsigned mmp2_ssp1_pin3[] = {GPIO115, GPIO116, GPIO117, GPIO118};
static const unsigned mmp2_ssp2_pin1[] = {GPIO47, GPIO48, GPIO49, GPIO50};
static const unsigned mmp2_ssp3_pin1[] = {GPIO119, GPIO120, GPIO121, GPIO122};
static const unsigned mmp2_ssp3_pin2[] = {GPIO132, GPIO133, GPIO133, GPIO136};
static const unsigned mmp2_sspa2_pin1[] = {GPIO25, GPIO26, GPIO27, GPIO28};
static const unsigned mmp2_sspa2_pin2[] = {GPIO33, GPIO34, GPIO35, GPIO36};
static const unsigned mmp2_mmc1_pin1[] = {GPIO131, GPIO132, GPIO133, GPIO134,
	GPIO136, GPIO139, GPIO140, GPIO141};
static const unsigned mmp2_mmc2_pin1[] = {GPIO37, GPIO38, GPIO39, GPIO40,
	GPIO41, GPIO42};
static const unsigned mmp2_mmc3_pin1[] = {GPIO111, GPIO112, GPIO151, GPIO162,
	GPIO163, GPIO164, GPIO165, GPIO166, GPIO167, GPIO168};

static struct pxa3xx_pin_group mmp2_grps[] = {
	GRP_MMP2("uart1 4p1", UART1, mmp2_uart1_pin1),
	GRP_MMP2("uart1 2p2", UART1, mmp2_uart1_pin2),
	GRP_MMP2("uart1 2p3", UART1, mmp2_uart1_pin3),
	GRP_MMP2("uart2 4p1", UART2, mmp2_uart2_pin1),
	GRP_MMP2("uart2 4p2", UART2, mmp2_uart2_pin2),
	GRP_MMP2("uart2 4p3", UART2, mmp2_uart2_pin3),
	GRP_MMP2("uart2 4p4", UART2, mmp2_uart2_pin4),
	GRP_MMP2("uart2 2p5", UART2, mmp2_uart2_pin5),
	GRP_MMP2("uart2 2p6", UART2, mmp2_uart2_pin6),
	GRP_MMP2("uart3 4p1", UART3, mmp2_uart3_pin1),
	GRP_MMP2("uart3 4p2", UART3, mmp2_uart3_pin2),
	GRP_MMP2("uart3 4p3", UART3, mmp2_uart3_pin3),
	GRP_MMP2("uart3 4p4", UART3, mmp2_uart3_pin4),
	GRP_MMP2("uart3 4p5", UART3, mmp2_uart3_pin5),
	GRP_MMP2("uart3 2p6", UART3, mmp2_uart3_pin6),
	GRP_MMP2("uart4 4p1", UART4, mmp2_uart4_pin1),
	GRP_MMP2("uart4 4p2", UART4, mmp2_uart4_pin2),
	GRP_MMP2("uart4 4p3", UART4, mmp2_uart4_pin3),
	GRP_MMP2("uart4 4p4", UART4, mmp2_uart4_pin4),
	GRP_MMP2("uart4 2p5", UART4, mmp2_uart4_pin5),
	GRP_MMP2("kpdk 4p1", KP_DK, mmp2_kpdk_pin1),
	GRP_MMP2("kpdk 4p2", KP_DK, mmp2_kpdk_pin2),
	GRP_MMP2("twsi2-1", TWSI2, mmp2_twsi2_pin1),
	GRP_MMP2("twsi2-2", TWSI2, mmp2_twsi2_pin2),
	GRP_MMP2("twsi2-3", TWSI2, mmp2_twsi2_pin3),
	GRP_MMP2("twsi2-4", TWSI2, mmp2_twsi2_pin4),
	GRP_MMP2("twsi2-5", TWSI2, mmp2_twsi2_pin5),
	GRP_MMP2("twsi3-1", TWSI3, mmp2_twsi3_pin1),
	GRP_MMP2("twsi3-2", TWSI3, mmp2_twsi3_pin2),
	GRP_MMP2("twsi4", TWSI4, mmp2_twsi4_pin1),
	GRP_MMP2("twsi5-1", TWSI5, mmp2_twsi5_pin1),
	GRP_MMP2("twsi5-2", TWSI5, mmp2_twsi5_pin2),
	GRP_MMP2("twsi5-3", TWSI5, mmp2_twsi5_pin3),
	GRP_MMP2("twsi6-1", TWSI6, mmp2_twsi6_pin1),
	GRP_MMP2("twsi6-2", TWSI6, mmp2_twsi6_pin2),
	GRP_MMP2("twsi6-3", TWSI6, mmp2_twsi6_pin3),
	GRP_MMP2("ccic1-1", CCIC1, mmp2_ccic1_pin1),
	GRP_MMP2("ccic1-2", CCIC1, mmp2_ccic1_pin2),
	GRP_MMP2("ccic2-1", CCIC2, mmp2_ccic2_pin1),
	GRP_MMP2("ccic2-1", CCIC2, mmp2_ccic2_pin2),
	GRP_MMP2("ulpi", ULPI, mmp2_ulpi_pin1),
	GRP_MMP2("ro-1", ROT, mmp2_ro_pin1),
	GRP_MMP2("ro-2", ROT, mmp2_ro_pin2),
	GRP_MMP2("ro-3", ROT, mmp2_ro_pin3),
	GRP_MMP2("ro-4", ROT, mmp2_ro_pin4),
	GRP_MMP2("i2s 5p1", I2S, mmp2_i2s_pin1),
	GRP_MMP2("i2s 4p2", I2S, mmp2_i2s_pin2),
	GRP_MMP2("ssp1 4p1", SSP1, mmp2_ssp1_pin1),
	GRP_MMP2("ssp1 4p2", SSP1, mmp2_ssp1_pin2),
	GRP_MMP2("ssp1 4p3", SSP1, mmp2_ssp1_pin3),
	GRP_MMP2("ssp2 4p1", SSP2, mmp2_ssp2_pin1),
	GRP_MMP2("ssp3 4p1", SSP3, mmp2_ssp3_pin1),
	GRP_MMP2("ssp3 4p2", SSP3, mmp2_ssp3_pin2),
	GRP_MMP2("sspa2 4p1", SSPA2, mmp2_sspa2_pin1),
	GRP_MMP2("sspa2 4p2", SSPA2, mmp2_sspa2_pin2),
	GRP_MMP2("mmc1 8p1", MMC1, mmp2_mmc1_pin1),
	GRP_MMP2("mmc2 6p1", MMC2, mmp2_mmc2_pin1),
	GRP_MMP2("mmc3 10p1", MMC3, mmp2_mmc3_pin1),
};

static const char * const mmp2_uart1_grps[] = {"uart1 4p1", "uart1 2p2",
	"uart1 2p3"};
static const char * const mmp2_uart2_grps[] = {"uart2 4p1", "uart2 4p2",
	"uart2 4p3", "uart2 4p4", "uart2 4p5", "uart2 4p6"};
static const char * const mmp2_uart3_grps[] = {"uart3 4p1", "uart3 4p2",
	"uart3 4p3", "uart3 4p4", "uart3 4p5", "uart3 2p6"};
static const char * const mmp2_uart4_grps[] = {"uart4 4p1", "uart4 4p2",
	"uart4 4p3", "uart4 4p4", "uart4 2p5"};
static const char * const mmp2_kpdk_grps[] = {"kpdk 4p1", "kpdk 4p2"};
static const char * const mmp2_twsi2_grps[] = {"twsi2-1", "twsi2-2",
	"twsi2-3", "twsi2-4", "twsi2-5"};
static const char * const mmp2_twsi3_grps[] = {"twsi3-1", "twsi3-2"};
static const char * const mmp2_twsi4_grps[] = {"twsi4"};
static const char * const mmp2_twsi5_grps[] = {"twsi5-1", "twsi5-2",
	"twsi5-3"};
static const char * const mmp2_twsi6_grps[] = {"twsi6-1", "twsi6-2",
	"twsi6-3"};
static const char * const mmp2_ccic1_grps[] = {"ccic1-1", "ccic1-2"};
static const char * const mmp2_ccic2_grps[] = {"ccic2-1", "ccic2-2"};
static const char * const mmp2_ulpi_grps[] = {"ulpi"};
static const char * const mmp2_ro_grps[] = {"ro-1", "ro-2", "ro-3", "ro-4"};
static const char * const mmp2_i2s_grps[] = {"i2s 5p1", "i2s 4p2"};
static const char * const mmp2_ssp1_grps[] = {"ssp1 4p1", "ssp1 4p2",
	"ssp1 4p3"};
static const char * const mmp2_ssp2_grps[] = {"ssp2 4p1"};
static const char * const mmp2_ssp3_grps[] = {"ssp3 4p1", "ssp3 4p2"};
static const char * const mmp2_sspa2_grps[] = {"sspa2 4p1", "sspa2 4p2"};
static const char * const mmp2_mmc1_grps[] = {"mmc1 8p1"};
static const char * const mmp2_mmc2_grps[] = {"mmc2 6p1"};
static const char * const mmp2_mmc3_grps[] = {"mmc3 10p1"};

static struct pxa3xx_pmx_func mmp2_funcs[] = {
	{"uart1",	ARRAY_AND_SIZE(mmp2_uart1_grps)},
	{"uart2",	ARRAY_AND_SIZE(mmp2_uart2_grps)},
	{"uart3",	ARRAY_AND_SIZE(mmp2_uart3_grps)},
	{"uart4",	ARRAY_AND_SIZE(mmp2_uart4_grps)},
	{"kpdk",	ARRAY_AND_SIZE(mmp2_kpdk_grps)},
	{"twsi2",	ARRAY_AND_SIZE(mmp2_twsi2_grps)},
	{"twsi3",	ARRAY_AND_SIZE(mmp2_twsi3_grps)},
	{"twsi4",	ARRAY_AND_SIZE(mmp2_twsi4_grps)},
	{"twsi5",	ARRAY_AND_SIZE(mmp2_twsi5_grps)},
	{"twsi6",	ARRAY_AND_SIZE(mmp2_twsi6_grps)},
	{"ccic1",	ARRAY_AND_SIZE(mmp2_ccic1_grps)},
	{"ccic2",	ARRAY_AND_SIZE(mmp2_ccic2_grps)},
	{"ulpi",	ARRAY_AND_SIZE(mmp2_ulpi_grps)},
	{"ro",		ARRAY_AND_SIZE(mmp2_ro_grps)},
	{"i2s",		ARRAY_AND_SIZE(mmp2_i2s_grps)},
	{"ssp1",	ARRAY_AND_SIZE(mmp2_ssp1_grps)},
	{"ssp2",	ARRAY_AND_SIZE(mmp2_ssp2_grps)},
	{"ssp3",	ARRAY_AND_SIZE(mmp2_ssp3_grps)},
	{"sspa2",	ARRAY_AND_SIZE(mmp2_sspa2_grps)},
	{"mmc1",	ARRAY_AND_SIZE(mmp2_mmc1_grps)},
	{"mmc2",	ARRAY_AND_SIZE(mmp2_mmc2_grps)},
	{"mmc3",	ARRAY_AND_SIZE(mmp2_mmc3_grps)},
};

static struct pinctrl_desc mmp2_pctrl_desc = {
	.name		= "mmp2-pinctrl",
	.owner		= THIS_MODULE,
};

static struct pxa3xx_pinmux_info mmp2_info = {
	.mfp		= mmp2_mfp,
	.num_mfp	= ARRAY_SIZE(mmp2_mfp),
	.grps		= mmp2_grps,
	.num_grps	= ARRAY_SIZE(mmp2_grps),
	.funcs		= mmp2_funcs,
	.num_funcs	= ARRAY_SIZE(mmp2_funcs),
	.num_gpio	= 169,
	.desc		= &mmp2_pctrl_desc,
	.pads		= mmp2_pads,
	.num_pads	= ARRAY_SIZE(mmp2_pads),

	.cputype	= PINCTRL_MMP2,
	.ds_mask	= MMP2_DS_MASK,
	.ds_shift	= MMP2_DS_SHIFT,
};

static int __devinit mmp2_pinmux_probe(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_register(pdev, &mmp2_info);
}

static int __devexit mmp2_pinmux_remove(struct platform_device *pdev)
{
	return pxa3xx_pinctrl_unregister(pdev);
}

static struct platform_driver mmp2_pinmux_driver = {
	.driver = {
		.name	= "mmp2-pinmux",
		.owner	= THIS_MODULE,
	},
	.probe	= mmp2_pinmux_probe,
	.remove	= __devexit_p(mmp2_pinmux_remove),
};

static int __init mmp2_pinmux_init(void)
{
	return platform_driver_register(&mmp2_pinmux_driver);
}
core_initcall_sync(mmp2_pinmux_init);

static void __exit mmp2_pinmux_exit(void)
{
	platform_driver_unregister(&mmp2_pinmux_driver);
}
module_exit(mmp2_pinmux_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("PXA3xx pin control driver");
MODULE_LICENSE("GPL v2");
