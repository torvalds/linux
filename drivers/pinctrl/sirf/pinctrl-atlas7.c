// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pinctrl pads, groups, functions for CSR SiRFatlasVII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/gpio/driver.h>

/* Definition of Pad&Mux Properties */
#define N 0

/* The Bank contains input-disable regisgers */
#define BANK_DS	0

/* Clear Register offset */
#define CLR_REG(r)	((r) + 0x04)

/* Definition of multiple function select register */
#define FUNC_CLEAR_MASK		0x7
#define FUNC_GPIO		0
#define FUNC_ANALOGUE		0x8
#define ANA_CLEAR_MASK		0x1

/* The Atlas7's Pad Type List */
enum altas7_pad_type {
	PAD_T_4WE_PD = 0,	/* ZIO_PAD3V_4WE_PD */
	PAD_T_4WE_PU,		/* ZIO_PAD3V_4WE_PD */
	PAD_T_16ST,		/* ZIO_PAD3V_SDCLK_PD */
	PAD_T_M31_0204_PD,	/* PRDW0204SDGZ_M311311_PD */
	PAD_T_M31_0204_PU,	/* PRDW0204SDGZ_M311311_PU */
	PAD_T_M31_0610_PD,	/* PRUW0610SDGZ_M311311_PD */
	PAD_T_M31_0610_PU,	/* PRUW0610SDGZ_M311311_PU */
	PAD_T_AD,		/* PRDWUWHW08SCDG_HZ */
};

/* Raw value of Driver-Strength Bits */
#define DS3	BIT(3)
#define DS2	BIT(2)
#define DS1	BIT(1)
#define DS0	BIT(0)
#define DSZ	0

/* Drive-Strength Intermediate Values */
#define DS_NULL		-1
#define DS_1BIT_IM_VAL  DS0
#define DS_1BIT_MASK	0x1
#define DS_2BIT_IM_VAL  (DS1 | DS0)
#define DS_2BIT_MASK	0x3
#define DS_4BIT_IM_VAL	(DS3 | DS2 | DS1 | DS0)
#define DS_4BIT_MASK	0xf

/* The Drive-Strength of 4WE Pad		 DS1  0  CO */
#define DS_4WE_3   (DS1 | DS0)			/* 1  1  3  */
#define DS_4WE_2   (DS1)			/* 1  0  2  */
#define DS_4WE_1   (DS0)			/* 0  1  1  */
#define DS_4WE_0   (DSZ)			/* 0  0  0  */

/* The Drive-Strength of 16st Pad		 DS3  2  1  0  CO */
#define DS_16ST_15  (DS3 | DS2 | DS1 | DS0)	/* 1  1  1  1  15 */
#define DS_16ST_14  (DS3 | DS2 | DS0)		/* 1  1  0  1  13 */
#define DS_16ST_13  (DS3 | DS2 | DS1)		/* 1  1  1  0  14 */
#define DS_16ST_12  (DS2 | DS1 | DS0)		/* 0  1  1  1  7  */
#define DS_16ST_11  (DS2 | DS0)			/* 0  1  0  1  5  */
#define DS_16ST_10  (DS3 | DS1 | DS0)		/* 1  0  1  1  11 */
#define DS_16ST_9   (DS3 | DS0)			/* 1  0  0  1  9  */
#define DS_16ST_8   (DS1 | DS0)			/* 0  0  1  1  3  */
#define DS_16ST_7   (DS2 | DS1)			/* 0  1  1  0  6  */
#define DS_16ST_6   (DS3 | DS2)			/* 1  1  0  0  12 */
#define DS_16ST_5   (DS2)			/* 0  1  0  0  4  */
#define DS_16ST_4   (DS3 | DS1)			/* 1  0  1  0  10 */
#define DS_16ST_3   (DS1)			/* 0  0  1  0  2  */
#define DS_16ST_2   (DS0)			/* 0  0  0  1  1  */
#define DS_16ST_1   (DSZ)			/* 0  0  0  0  0  */
#define DS_16ST_0   (DS3)			/* 1  0  0  0  8  */

/* The Drive-Strength of M31 Pad		 DS0  CO */
#define DS_M31_0   (DSZ)			/* 0  0  */
#define DS_M31_1   (DS0)			/* 1  1  */

/* Raw values of Pull Option Bits */
#define PUN	BIT(1)
#define PD	BIT(0)
#define PE	BIT(0)
#define PZ	0

/* Definition of Pull Types */
#define PULL_UP		0
#define HIGH_HYSTERESIS 1
#define HIGH_Z		2
#define PULL_DOWN	3
#define PULL_DISABLE	4
#define PULL_ENABLE	5
#define PULL_UNKNOWN	-1

/* Pull Options for 4WE Pad			  PUN  PD  CO */
#define P4WE_PULL_MASK		0x3
#define P4WE_PULL_DOWN		(PUN | PD)	/* 1   1   3  */
#define P4WE_HIGH_Z		(PUN)		/* 1   0   2  */
#define P4WE_HIGH_HYSTERESIS	(PD)		/* 0   1   1  */
#define P4WE_PULL_UP		(PZ)		/* 0   0   0  */

/* Pull Options for 16ST Pad			  PUN  PD  CO */
#define P16ST_PULL_MASK		0x3
#define P16ST_PULL_DOWN		(PUN | PD)	/* 1   1   3  */
#define P16ST_HIGH_Z		(PUN)		/* 1   0   2  */
#define P16ST_PULL_UP		(PZ)		/* 0   0   0  */

/* Pull Options for M31 Pad			  PE */
#define PM31_PULL_MASK		0x1
#define PM31_PULL_ENABLED	(PE)		/* 1 */
#define PM31_PULL_DISABLED	(PZ)		/* 0 */

/* Pull Options for A/D Pad			  PUN  PD  CO */
#define PANGD_PULL_MASK		0x3
#define PANGD_PULL_DOWN		(PUN | PD)	/* 1   1   3  */
#define PANGD_HIGH_Z		(PUN)		/* 1   0   2  */
#define PANGD_PULL_UP		(PZ)		/* 0   0   0  */

/* Definition of Input Disable */
#define DI_MASK		0x1
#define DI_DISABLE	0x1
#define DI_ENABLE	0x0

/* Definition of Input Disable Value */
#define DIV_MASK	0x1
#define DIV_DISABLE	0x1
#define DIV_ENABLE	0x0

/* Number of Function input disable registers */
#define NUM_OF_IN_DISABLE_REG	0x2

/* Offset of Function input disable registers */
#define IN_DISABLE_0_REG_SET		0x0A00
#define IN_DISABLE_0_REG_CLR		0x0A04
#define IN_DISABLE_1_REG_SET		0x0A08
#define IN_DISABLE_1_REG_CLR		0x0A0C
#define IN_DISABLE_VAL_0_REG_SET	0x0A80
#define IN_DISABLE_VAL_0_REG_CLR	0x0A84
#define IN_DISABLE_VAL_1_REG_SET	0x0A88
#define IN_DISABLE_VAL_1_REG_CLR	0x0A8C

/* Offset of the SDIO9SEL*/
#define SYS2PCI_SDIO9SEL 0x14

struct dt_params {
	const char *property;
	int value;
};

/**
 * struct atlas7_pad_conf - Atlas7 Pad Configuration
 * @id			The ID of this Pad.
 * @type:		The type of this Pad.
 * @mux_reg:		The mux register offset.
 *			This register contains the mux.
 * @pupd_reg:		The pull-up/down register offset.
 * @drvstr_reg:		The drive-strength register offset.
 * @ad_ctrl_reg:	The Analogue/Digital Control register.
 *
 * @mux_bit:		The start bit of mux register.
 * @pupd_bit:		The start bit of pull-up/down register.
 * @drvstr_bit:		The start bit of drive-strength register.
 * @ad_ctrl_bit:	The start bit of analogue/digital register.
 */
struct atlas7_pad_config {
	const u32 id;
	u32 type;
	u32 mux_reg;
	u32 pupd_reg;
	u32 drvstr_reg;
	u32 ad_ctrl_reg;
	/* bits in register */
	u8 mux_bit;
	u8 pupd_bit;
	u8 drvstr_bit;
	u8 ad_ctrl_bit;
};

#define PADCONF(pad, t, mr, pr, dsr, adr, mb, pb, dsb, adb)	\
	{							\
		.id = pad,					\
		.type = t,					\
		.mux_reg = mr,					\
		.pupd_reg = pr,					\
		.drvstr_reg = dsr,				\
		.ad_ctrl_reg = adr,				\
		.mux_bit = mb,					\
		.pupd_bit = pb,					\
		.drvstr_bit = dsb,				\
		.ad_ctrl_bit = adb,				\
	}

/**
 * struct atlas7_pad_status - Atlas7 Pad status
 */
struct atlas7_pad_status {
	u8 func;
	u8 pull;
	u8 dstr;
	u8 reserved;
};

/**
 * struct atlas7_pad_mux - Atlas7 mux
 * @bank:		The bank of this pad's registers on.
 * @pin	:		The ID of this Pad.
 * @func:		The mux func on this Pad.
 * @dinput_reg:		The Input-Disable register offset.
 * @dinput_bit:		The start bit of Input-Disable register.
 * @dinput_val_reg:	The Input-Disable-value register offset.
 *			This register is used to set the value of this pad
 *			if this pad was disabled.
 * @dinput_val_bit:	The start bit of Input-Disable Value register.
 */
struct atlas7_pad_mux {
	u32 bank;
	u32 pin;
	u32 func;
	u32 dinput_reg;
	u32 dinput_bit;
	u32 dinput_val_reg;
	u32 dinput_val_bit;
};

#define MUX(b, pad, f, dr, db, dvr, dvb)	\
	{					\
		.bank = b,			\
		.pin = pad,			\
		.func = f,			\
		.dinput_reg = dr,		\
		.dinput_bit = db,		\
		.dinput_val_reg = dvr,		\
		.dinput_val_bit = dvb,		\
	}

struct atlas7_grp_mux {
	unsigned int group;
	unsigned int pad_mux_count;
	const struct atlas7_pad_mux *pad_mux_list;
};

 /**
 * struct sirfsoc_pin_group - describes a SiRFprimaII pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct atlas7_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
};

#define GROUP(n, p)  \
	{			\
		.name = n,	\
		.pins = p,	\
		.num_pins = ARRAY_SIZE(p),	\
	}

struct atlas7_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
	const struct atlas7_grp_mux *grpmux;
};

#define FUNCTION(n, g, m)		\
	{					\
		.name = n,			\
		.groups = g,			\
		.num_groups = ARRAY_SIZE(g),	\
		.grpmux = m,			\
	}

struct atlas7_pinctrl_data {
	struct pinctrl_pin_desc *pads;
	int pads_cnt;
	struct atlas7_pin_group *grps;
	int grps_cnt;
	struct atlas7_pmx_func *funcs;
	int funcs_cnt;
	struct atlas7_pad_config *confs;
	int confs_cnt;
};

/* Platform info of atlas7 pinctrl */
#define ATLAS7_PINCTRL_REG_BANKS	2
#define ATLAS7_PINCTRL_BANK_0_PINS	18
#define ATLAS7_PINCTRL_BANK_1_PINS	141
#define ATLAS7_PINCTRL_TOTAL_PINS	\
	(ATLAS7_PINCTRL_BANK_0_PINS + ATLAS7_PINCTRL_BANK_1_PINS)

/**
 * Atlas7 GPIO Chip
 */

#define NGPIO_OF_BANK		32
#define GPIO_TO_BANK(gpio)	((gpio) / NGPIO_OF_BANK)

/* Registers of GPIO Controllers */
#define ATLAS7_GPIO_BASE(g, b)		((g)->reg + 0x100 * (b))
#define ATLAS7_GPIO_CTRL(b, i)		((b)->base + 4 * (i))
#define ATLAS7_GPIO_INT_STATUS(b)	((b)->base + 0x8C)

/* Definition bits of GPIO Control Registers */
#define ATLAS7_GPIO_CTL_INTR_LOW_MASK		BIT(0)
#define ATLAS7_GPIO_CTL_INTR_HIGH_MASK		BIT(1)
#define ATLAS7_GPIO_CTL_INTR_TYPE_MASK		BIT(2)
#define ATLAS7_GPIO_CTL_INTR_EN_MASK		BIT(3)
#define ATLAS7_GPIO_CTL_INTR_STATUS_MASK	BIT(4)
#define ATLAS7_GPIO_CTL_OUT_EN_MASK		BIT(5)
#define ATLAS7_GPIO_CTL_DATAOUT_MASK		BIT(6)
#define ATLAS7_GPIO_CTL_DATAIN_MASK		BIT(7)

struct atlas7_gpio_bank {
	int id;
	int irq;
	void __iomem *base;
	unsigned int gpio_offset;
	unsigned int ngpio;
	const unsigned int *gpio_pins;
	u32 sleep_data[NGPIO_OF_BANK];
};

struct atlas7_gpio_chip {
	const char *name;
	void __iomem *reg;
	struct clk *clk;
	int nbank;
	raw_spinlock_t lock;
	struct gpio_chip chip;
	struct atlas7_gpio_bank banks[];
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct atlas7_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctl_desc;
	struct atlas7_pinctrl_data *pctl_data;
	void __iomem *regs[ATLAS7_PINCTRL_REG_BANKS];
	void __iomem *sys2pci_base;
	u32 status_ds[NUM_OF_IN_DISABLE_REG];
	u32 status_dsv[NUM_OF_IN_DISABLE_REG];
	struct atlas7_pad_status sleep_data[ATLAS7_PINCTRL_TOTAL_PINS];
};

/*
 * Pad list for the pinmux subsystem
 * refer to A7DA IO Summary - CS-314158-DD-4E.xls
 */

/*Pads in IOC RTC & TOP */
static const struct pinctrl_pin_desc atlas7_ioc_pads[] = {
	/* RTC PADs */
	PINCTRL_PIN(0, "rtc_gpio_0"),
	PINCTRL_PIN(1, "rtc_gpio_1"),
	PINCTRL_PIN(2, "rtc_gpio_2"),
	PINCTRL_PIN(3, "rtc_gpio_3"),
	PINCTRL_PIN(4, "low_bat_ind_b"),
	PINCTRL_PIN(5, "on_key_b"),
	PINCTRL_PIN(6, "ext_on"),
	PINCTRL_PIN(7, "mem_on"),
	PINCTRL_PIN(8, "core_on"),
	PINCTRL_PIN(9, "io_on"),
	PINCTRL_PIN(10, "can0_tx"),
	PINCTRL_PIN(11, "can0_rx"),
	PINCTRL_PIN(12, "spi0_clk"),
	PINCTRL_PIN(13, "spi0_cs_b"),
	PINCTRL_PIN(14, "spi0_io_0"),
	PINCTRL_PIN(15, "spi0_io_1"),
	PINCTRL_PIN(16, "spi0_io_2"),
	PINCTRL_PIN(17, "spi0_io_3"),

	/* TOP PADs */
	PINCTRL_PIN(18, "spi1_en"),
	PINCTRL_PIN(19, "spi1_clk"),
	PINCTRL_PIN(20, "spi1_din"),
	PINCTRL_PIN(21, "spi1_dout"),
	PINCTRL_PIN(22, "trg_spi_clk"),
	PINCTRL_PIN(23, "trg_spi_di"),
	PINCTRL_PIN(24, "trg_spi_do"),
	PINCTRL_PIN(25, "trg_spi_cs_b"),
	PINCTRL_PIN(26, "trg_acq_d1"),
	PINCTRL_PIN(27, "trg_irq_b"),
	PINCTRL_PIN(28, "trg_acq_d0"),
	PINCTRL_PIN(29, "trg_acq_clk"),
	PINCTRL_PIN(30, "trg_shutdown_b_out"),
	PINCTRL_PIN(31, "sdio2_clk"),
	PINCTRL_PIN(32, "sdio2_cmd"),
	PINCTRL_PIN(33, "sdio2_dat_0"),
	PINCTRL_PIN(34, "sdio2_dat_1"),
	PINCTRL_PIN(35, "sdio2_dat_2"),
	PINCTRL_PIN(36, "sdio2_dat_3"),
	PINCTRL_PIN(37, "df_ad_7"),
	PINCTRL_PIN(38, "df_ad_6"),
	PINCTRL_PIN(39, "df_ad_5"),
	PINCTRL_PIN(40, "df_ad_4"),
	PINCTRL_PIN(41, "df_ad_3"),
	PINCTRL_PIN(42, "df_ad_2"),
	PINCTRL_PIN(43, "df_ad_1"),
	PINCTRL_PIN(44, "df_ad_0"),
	PINCTRL_PIN(45, "df_dqs"),
	PINCTRL_PIN(46, "df_cle"),
	PINCTRL_PIN(47, "df_ale"),
	PINCTRL_PIN(48, "df_we_b"),
	PINCTRL_PIN(49, "df_re_b"),
	PINCTRL_PIN(50, "df_ry_by"),
	PINCTRL_PIN(51, "df_cs_b_1"),
	PINCTRL_PIN(52, "df_cs_b_0"),
	PINCTRL_PIN(53, "l_pclk"),
	PINCTRL_PIN(54, "l_lck"),
	PINCTRL_PIN(55, "l_fck"),
	PINCTRL_PIN(56, "l_de"),
	PINCTRL_PIN(57, "ldd_0"),
	PINCTRL_PIN(58, "ldd_1"),
	PINCTRL_PIN(59, "ldd_2"),
	PINCTRL_PIN(60, "ldd_3"),
	PINCTRL_PIN(61, "ldd_4"),
	PINCTRL_PIN(62, "ldd_5"),
	PINCTRL_PIN(63, "ldd_6"),
	PINCTRL_PIN(64, "ldd_7"),
	PINCTRL_PIN(65, "ldd_8"),
	PINCTRL_PIN(66, "ldd_9"),
	PINCTRL_PIN(67, "ldd_10"),
	PINCTRL_PIN(68, "ldd_11"),
	PINCTRL_PIN(69, "ldd_12"),
	PINCTRL_PIN(70, "ldd_13"),
	PINCTRL_PIN(71, "ldd_14"),
	PINCTRL_PIN(72, "ldd_15"),
	PINCTRL_PIN(73, "lcd_gpio_20"),
	PINCTRL_PIN(74, "vip_0"),
	PINCTRL_PIN(75, "vip_1"),
	PINCTRL_PIN(76, "vip_2"),
	PINCTRL_PIN(77, "vip_3"),
	PINCTRL_PIN(78, "vip_4"),
	PINCTRL_PIN(79, "vip_5"),
	PINCTRL_PIN(80, "vip_6"),
	PINCTRL_PIN(81, "vip_7"),
	PINCTRL_PIN(82, "vip_pxclk"),
	PINCTRL_PIN(83, "vip_hsync"),
	PINCTRL_PIN(84, "vip_vsync"),
	PINCTRL_PIN(85, "sdio3_clk"),
	PINCTRL_PIN(86, "sdio3_cmd"),
	PINCTRL_PIN(87, "sdio3_dat_0"),
	PINCTRL_PIN(88, "sdio3_dat_1"),
	PINCTRL_PIN(89, "sdio3_dat_2"),
	PINCTRL_PIN(90, "sdio3_dat_3"),
	PINCTRL_PIN(91, "sdio5_clk"),
	PINCTRL_PIN(92, "sdio5_cmd"),
	PINCTRL_PIN(93, "sdio5_dat_0"),
	PINCTRL_PIN(94, "sdio5_dat_1"),
	PINCTRL_PIN(95, "sdio5_dat_2"),
	PINCTRL_PIN(96, "sdio5_dat_3"),
	PINCTRL_PIN(97, "rgmii_txd_0"),
	PINCTRL_PIN(98, "rgmii_txd_1"),
	PINCTRL_PIN(99, "rgmii_txd_2"),
	PINCTRL_PIN(100, "rgmii_txd_3"),
	PINCTRL_PIN(101, "rgmii_txclk"),
	PINCTRL_PIN(102, "rgmii_tx_ctl"),
	PINCTRL_PIN(103, "rgmii_rxd_0"),
	PINCTRL_PIN(104, "rgmii_rxd_1"),
	PINCTRL_PIN(105, "rgmii_rxd_2"),
	PINCTRL_PIN(106, "rgmii_rxd_3"),
	PINCTRL_PIN(107, "rgmii_rx_clk"),
	PINCTRL_PIN(108, "rgmii_rxc_ctl"),
	PINCTRL_PIN(109, "rgmii_mdio"),
	PINCTRL_PIN(110, "rgmii_mdc"),
	PINCTRL_PIN(111, "rgmii_intr_n"),
	PINCTRL_PIN(112, "i2s_mclk"),
	PINCTRL_PIN(113, "i2s_bclk"),
	PINCTRL_PIN(114, "i2s_ws"),
	PINCTRL_PIN(115, "i2s_dout0"),
	PINCTRL_PIN(116, "i2s_dout1"),
	PINCTRL_PIN(117, "i2s_dout2"),
	PINCTRL_PIN(118, "i2s_din"),
	PINCTRL_PIN(119, "gpio_0"),
	PINCTRL_PIN(120, "gpio_1"),
	PINCTRL_PIN(121, "gpio_2"),
	PINCTRL_PIN(122, "gpio_3"),
	PINCTRL_PIN(123, "gpio_4"),
	PINCTRL_PIN(124, "gpio_5"),
	PINCTRL_PIN(125, "gpio_6"),
	PINCTRL_PIN(126, "gpio_7"),
	PINCTRL_PIN(127, "sda_0"),
	PINCTRL_PIN(128, "scl_0"),
	PINCTRL_PIN(129, "coex_pio_0"),
	PINCTRL_PIN(130, "coex_pio_1"),
	PINCTRL_PIN(131, "coex_pio_2"),
	PINCTRL_PIN(132, "coex_pio_3"),
	PINCTRL_PIN(133, "uart0_tx"),
	PINCTRL_PIN(134, "uart0_rx"),
	PINCTRL_PIN(135, "uart1_tx"),
	PINCTRL_PIN(136, "uart1_rx"),
	PINCTRL_PIN(137, "uart3_tx"),
	PINCTRL_PIN(138, "uart3_rx"),
	PINCTRL_PIN(139, "uart4_tx"),
	PINCTRL_PIN(140, "uart4_rx"),
	PINCTRL_PIN(141, "usp0_clk"),
	PINCTRL_PIN(142, "usp0_tx"),
	PINCTRL_PIN(143, "usp0_rx"),
	PINCTRL_PIN(144, "usp0_fs"),
	PINCTRL_PIN(145, "usp1_clk"),
	PINCTRL_PIN(146, "usp1_tx"),
	PINCTRL_PIN(147, "usp1_rx"),
	PINCTRL_PIN(148, "usp1_fs"),
	PINCTRL_PIN(149, "lvds_tx0d4p"),
	PINCTRL_PIN(150, "lvds_tx0d4n"),
	PINCTRL_PIN(151, "lvds_tx0d3p"),
	PINCTRL_PIN(152, "lvds_tx0d3n"),
	PINCTRL_PIN(153, "lvds_tx0d2p"),
	PINCTRL_PIN(154, "lvds_tx0d2n"),
	PINCTRL_PIN(155, "lvds_tx0d1p"),
	PINCTRL_PIN(156, "lvds_tx0d1n"),
	PINCTRL_PIN(157, "lvds_tx0d0p"),
	PINCTRL_PIN(158, "lvds_tx0d0n"),
	PINCTRL_PIN(159, "jtag_tdo"),
	PINCTRL_PIN(160, "jtag_tms"),
	PINCTRL_PIN(161, "jtag_tck"),
	PINCTRL_PIN(162, "jtag_tdi"),
	PINCTRL_PIN(163, "jtag_trstn"),
};

static struct atlas7_pad_config atlas7_ioc_pad_confs[] = {
	/* The Configuration of IOC_RTC Pads */
	PADCONF(0, 3, 0x0, 0x100, 0x200, -1, 0, 0, 0, 0),
	PADCONF(1, 3, 0x0, 0x100, 0x200, -1, 4, 2, 2, 0),
	PADCONF(2, 3, 0x0, 0x100, 0x200, -1, 8, 4, 4, 0),
	PADCONF(3, 5, 0x0, 0x100, 0x200, -1, 12, 6, 6, 0),
	PADCONF(4, 4, 0x0, 0x100, 0x200, -1, 16, 8, 8, 0),
	PADCONF(5, 4, 0x0, 0x100, 0x200, -1, 20, 10, 10, 0),
	PADCONF(6, 3, 0x0, 0x100, 0x200, -1, 24, 12, 12, 0),
	PADCONF(7, 3, 0x0, 0x100, 0x200, -1, 28, 14, 14, 0),
	PADCONF(8, 3, 0x8, 0x100, 0x200, -1, 0, 16, 16, 0),
	PADCONF(9, 3, 0x8, 0x100, 0x200, -1, 4, 18, 18, 0),
	PADCONF(10, 4, 0x8, 0x100, 0x200, -1, 8, 20, 20, 0),
	PADCONF(11, 4, 0x8, 0x100, 0x200, -1, 12, 22, 22, 0),
	PADCONF(12, 5, 0x8, 0x100, 0x200, -1, 16, 24, 24, 0),
	PADCONF(13, 6, 0x8, 0x100, 0x200, -1, 20, 26, 26, 0),
	PADCONF(14, 5, 0x8, 0x100, 0x200, -1, 24, 28, 28, 0),
	PADCONF(15, 5, 0x8, 0x100, 0x200, -1, 28, 30, 30, 0),
	PADCONF(16, 5, 0x10, 0x108, 0x208, -1, 0, 0, 0, 0),
	PADCONF(17, 5, 0x10, 0x108, 0x208, -1, 4, 2, 2, 0),
	/* The Configuration of IOC_TOP Pads */
	PADCONF(18, 5, 0x80, 0x180, 0x300, -1, 0, 0, 0, 0),
	PADCONF(19, 5, 0x80, 0x180, 0x300, -1, 4, 2, 2, 0),
	PADCONF(20, 5, 0x80, 0x180, 0x300, -1, 8, 4, 4, 0),
	PADCONF(21, 5, 0x80, 0x180, 0x300, -1, 12, 6, 6, 0),
	PADCONF(22, 5, 0x88, 0x188, 0x308, -1, 0, 0, 0, 0),
	PADCONF(23, 5, 0x88, 0x188, 0x308, -1, 4, 2, 2, 0),
	PADCONF(24, 5, 0x88, 0x188, 0x308, -1, 8, 4, 4, 0),
	PADCONF(25, 6, 0x88, 0x188, 0x308, -1, 12, 6, 6, 0),
	PADCONF(26, 5, 0x88, 0x188, 0x308, -1, 16, 8, 8, 0),
	PADCONF(27, 6, 0x88, 0x188, 0x308, -1, 20, 10, 10, 0),
	PADCONF(28, 5, 0x88, 0x188, 0x308, -1, 24, 12, 12, 0),
	PADCONF(29, 5, 0x88, 0x188, 0x308, -1, 28, 14, 14, 0),
	PADCONF(30, 5, 0x90, 0x188, 0x308, -1, 0, 16, 16, 0),
	PADCONF(31, 2, 0x98, 0x190, 0x310, -1, 0, 0, 0, 0),
	PADCONF(32, 1, 0x98, 0x190, 0x310, -1, 4, 2, 4, 0),
	PADCONF(33, 1, 0x98, 0x190, 0x310, -1, 8, 4, 6, 0),
	PADCONF(34, 1, 0x98, 0x190, 0x310, -1, 12, 6, 8, 0),
	PADCONF(35, 1, 0x98, 0x190, 0x310, -1, 16, 8, 10, 0),
	PADCONF(36, 1, 0x98, 0x190, 0x310, -1, 20, 10, 12, 0),
	PADCONF(37, 1, 0xa0, 0x198, 0x318, -1, 0, 0, 0, 0),
	PADCONF(38, 1, 0xa0, 0x198, 0x318, -1, 4, 2, 2, 0),
	PADCONF(39, 1, 0xa0, 0x198, 0x318, -1, 8, 4, 4, 0),
	PADCONF(40, 1, 0xa0, 0x198, 0x318, -1, 12, 6, 6, 0),
	PADCONF(41, 1, 0xa0, 0x198, 0x318, -1, 16, 8, 8, 0),
	PADCONF(42, 1, 0xa0, 0x198, 0x318, -1, 20, 10, 10, 0),
	PADCONF(43, 1, 0xa0, 0x198, 0x318, -1, 24, 12, 12, 0),
	PADCONF(44, 1, 0xa0, 0x198, 0x318, -1, 28, 14, 14, 0),
	PADCONF(45, 0, 0xa8, 0x198, 0x318, -1, 0, 16, 16, 0),
	PADCONF(46, 0, 0xa8, 0x198, 0x318, -1, 4, 18, 18, 0),
	PADCONF(47, 1, 0xa8, 0x198, 0x318, -1, 8, 20, 20, 0),
	PADCONF(48, 1, 0xa8, 0x198, 0x318, -1, 12, 22, 22, 0),
	PADCONF(49, 1, 0xa8, 0x198, 0x318, -1, 16, 24, 24, 0),
	PADCONF(50, 1, 0xa8, 0x198, 0x318, -1, 20, 26, 26, 0),
	PADCONF(51, 1, 0xa8, 0x198, 0x318, -1, 24, 28, 28, 0),
	PADCONF(52, 1, 0xa8, 0x198, 0x318, -1, 28, 30, 30, 0),
	PADCONF(53, 0, 0xb0, 0x1a0, 0x320, -1, 0, 0, 0, 0),
	PADCONF(54, 0, 0xb0, 0x1a0, 0x320, -1, 4, 2, 2, 0),
	PADCONF(55, 0, 0xb0, 0x1a0, 0x320, -1, 8, 4, 4, 0),
	PADCONF(56, 0, 0xb0, 0x1a0, 0x320, -1, 12, 6, 6, 0),
	PADCONF(57, 0, 0xb0, 0x1a0, 0x320, -1, 16, 8, 8, 0),
	PADCONF(58, 0, 0xb0, 0x1a0, 0x320, -1, 20, 10, 10, 0),
	PADCONF(59, 0, 0xb0, 0x1a0, 0x320, -1, 24, 12, 12, 0),
	PADCONF(60, 0, 0xb0, 0x1a0, 0x320, -1, 28, 14, 14, 0),
	PADCONF(61, 0, 0xb8, 0x1a0, 0x320, -1, 0, 16, 16, 0),
	PADCONF(62, 0, 0xb8, 0x1a0, 0x320, -1, 4, 18, 18, 0),
	PADCONF(63, 0, 0xb8, 0x1a0, 0x320, -1, 8, 20, 20, 0),
	PADCONF(64, 0, 0xb8, 0x1a0, 0x320, -1, 12, 22, 22, 0),
	PADCONF(65, 0, 0xb8, 0x1a0, 0x320, -1, 16, 24, 24, 0),
	PADCONF(66, 0, 0xb8, 0x1a0, 0x320, -1, 20, 26, 26, 0),
	PADCONF(67, 0, 0xb8, 0x1a0, 0x320, -1, 24, 28, 28, 0),
	PADCONF(68, 0, 0xb8, 0x1a0, 0x320, -1, 28, 30, 30, 0),
	PADCONF(69, 0, 0xc0, 0x1a8, 0x328, -1, 0, 0, 0, 0),
	PADCONF(70, 0, 0xc0, 0x1a8, 0x328, -1, 4, 2, 2, 0),
	PADCONF(71, 0, 0xc0, 0x1a8, 0x328, -1, 8, 4, 4, 0),
	PADCONF(72, 0, 0xc0, 0x1a8, 0x328, -1, 12, 6, 6, 0),
	PADCONF(73, 0, 0xc0, 0x1a8, 0x328, -1, 16, 8, 8, 0),
	PADCONF(74, 0, 0xc8, 0x1b0, 0x330, -1, 0, 0, 0, 0),
	PADCONF(75, 0, 0xc8, 0x1b0, 0x330, -1, 4, 2, 2, 0),
	PADCONF(76, 0, 0xc8, 0x1b0, 0x330, -1, 8, 4, 4, 0),
	PADCONF(77, 0, 0xc8, 0x1b0, 0x330, -1, 12, 6, 6, 0),
	PADCONF(78, 0, 0xc8, 0x1b0, 0x330, -1, 16, 8, 8, 0),
	PADCONF(79, 0, 0xc8, 0x1b0, 0x330, -1, 20, 10, 10, 0),
	PADCONF(80, 0, 0xc8, 0x1b0, 0x330, -1, 24, 12, 12, 0),
	PADCONF(81, 0, 0xc8, 0x1b0, 0x330, -1, 28, 14, 14, 0),
	PADCONF(82, 0, 0xd0, 0x1b0, 0x330, -1, 0, 16, 16, 0),
	PADCONF(83, 0, 0xd0, 0x1b0, 0x330, -1, 4, 18, 18, 0),
	PADCONF(84, 0, 0xd0, 0x1b0, 0x330, -1, 8, 20, 20, 0),
	PADCONF(85, 2, 0xd8, 0x1b8, 0x338, -1, 0, 0, 0, 0),
	PADCONF(86, 1, 0xd8, 0x1b8, 0x338, -1, 4, 4, 4, 0),
	PADCONF(87, 1, 0xd8, 0x1b8, 0x338, -1, 8, 6, 6, 0),
	PADCONF(88, 1, 0xd8, 0x1b8, 0x338, -1, 12, 8, 8, 0),
	PADCONF(89, 1, 0xd8, 0x1b8, 0x338, -1, 16, 10, 10, 0),
	PADCONF(90, 1, 0xd8, 0x1b8, 0x338, -1, 20, 12, 12, 0),
	PADCONF(91, 2, 0xe0, 0x1c0, 0x340, -1, 0, 0, 0, 0),
	PADCONF(92, 1, 0xe0, 0x1c0, 0x340, -1, 4, 4, 4, 0),
	PADCONF(93, 1, 0xe0, 0x1c0, 0x340, -1, 8, 6, 6, 0),
	PADCONF(94, 1, 0xe0, 0x1c0, 0x340, -1, 12, 8, 8, 0),
	PADCONF(95, 1, 0xe0, 0x1c0, 0x340, -1, 16, 10, 10, 0),
	PADCONF(96, 1, 0xe0, 0x1c0, 0x340, -1, 20, 12, 12, 0),
	PADCONF(97, 0, 0xe8, 0x1c8, 0x348, -1, 0, 0, 0, 0),
	PADCONF(98, 0, 0xe8, 0x1c8, 0x348, -1, 4, 2, 2, 0),
	PADCONF(99, 0, 0xe8, 0x1c8, 0x348, -1, 8, 4, 4, 0),
	PADCONF(100, 0, 0xe8, 0x1c8, 0x348, -1, 12, 6, 6, 0),
	PADCONF(101, 2, 0xe8, 0x1c8, 0x348, -1, 16, 8, 8, 0),
	PADCONF(102, 0, 0xe8, 0x1c8, 0x348, -1, 20, 12, 12, 0),
	PADCONF(103, 0, 0xe8, 0x1c8, 0x348, -1, 24, 14, 14, 0),
	PADCONF(104, 0, 0xe8, 0x1c8, 0x348, -1, 28, 16, 16, 0),
	PADCONF(105, 0, 0xf0, 0x1c8, 0x348, -1, 0, 18, 18, 0),
	PADCONF(106, 0, 0xf0, 0x1c8, 0x348, -1, 4, 20, 20, 0),
	PADCONF(107, 0, 0xf0, 0x1c8, 0x348, -1, 8, 22, 22, 0),
	PADCONF(108, 0, 0xf0, 0x1c8, 0x348, -1, 12, 24, 24, 0),
	PADCONF(109, 1, 0xf0, 0x1c8, 0x348, -1, 16, 26, 26, 0),
	PADCONF(110, 0, 0xf0, 0x1c8, 0x348, -1, 20, 28, 28, 0),
	PADCONF(111, 1, 0xf0, 0x1c8, 0x348, -1, 24, 30, 30, 0),
	PADCONF(112, 5, 0xf8, 0x200, 0x350, -1, 0, 0, 0, 0),
	PADCONF(113, 5, 0xf8, 0x200, 0x350, -1, 4, 2, 2, 0),
	PADCONF(114, 5, 0xf8, 0x200, 0x350, -1, 8, 4, 4, 0),
	PADCONF(115, 5, 0xf8, 0x200, 0x350, -1, 12, 6, 6, 0),
	PADCONF(116, 5, 0xf8, 0x200, 0x350, -1, 16, 8, 8, 0),
	PADCONF(117, 5, 0xf8, 0x200, 0x350, -1, 20, 10, 10, 0),
	PADCONF(118, 5, 0xf8, 0x200, 0x350, -1, 24, 12, 12, 0),
	PADCONF(119, 5, 0x100, 0x250, 0x358, -1, 0, 0, 0, 0),
	PADCONF(120, 5, 0x100, 0x250, 0x358, -1, 4, 2, 2, 0),
	PADCONF(121, 5, 0x100, 0x250, 0x358, -1, 8, 4, 4, 0),
	PADCONF(122, 5, 0x100, 0x250, 0x358, -1, 12, 6, 6, 0),
	PADCONF(123, 6, 0x100, 0x250, 0x358, -1, 16, 8, 8, 0),
	PADCONF(124, 6, 0x100, 0x250, 0x358, -1, 20, 10, 10, 0),
	PADCONF(125, 6, 0x100, 0x250, 0x358, -1, 24, 12, 12, 0),
	PADCONF(126, 6, 0x100, 0x250, 0x358, -1, 28, 14, 14, 0),
	PADCONF(127, 6, 0x108, 0x250, 0x358, -1, 16, 24, 24, 0),
	PADCONF(128, 6, 0x108, 0x250, 0x358, -1, 20, 26, 26, 0),
	PADCONF(129, 0, 0x110, 0x258, 0x360, -1, 0, 0, 0, 0),
	PADCONF(130, 0, 0x110, 0x258, 0x360, -1, 4, 2, 2, 0),
	PADCONF(131, 0, 0x110, 0x258, 0x360, -1, 8, 4, 4, 0),
	PADCONF(132, 0, 0x110, 0x258, 0x360, -1, 12, 6, 6, 0),
	PADCONF(133, 6, 0x118, 0x260, 0x368, -1, 0, 0, 0, 0),
	PADCONF(134, 6, 0x118, 0x260, 0x368, -1, 4, 2, 2, 0),
	PADCONF(135, 6, 0x118, 0x260, 0x368, -1, 16, 8, 8, 0),
	PADCONF(136, 6, 0x118, 0x260, 0x368, -1, 20, 10, 10, 0),
	PADCONF(137, 6, 0x118, 0x260, 0x368, -1, 24, 12, 12, 0),
	PADCONF(138, 6, 0x118, 0x260, 0x368, -1, 28, 14, 14, 0),
	PADCONF(139, 6, 0x120, 0x260, 0x368, -1, 0, 16, 16, 0),
	PADCONF(140, 6, 0x120, 0x260, 0x368, -1, 4, 18, 18, 0),
	PADCONF(141, 5, 0x128, 0x268, 0x378, -1, 0, 0, 0, 0),
	PADCONF(142, 5, 0x128, 0x268, 0x378, -1, 4, 2, 2, 0),
	PADCONF(143, 5, 0x128, 0x268, 0x378, -1, 8, 4, 4, 0),
	PADCONF(144, 5, 0x128, 0x268, 0x378, -1, 12, 6, 6, 0),
	PADCONF(145, 5, 0x128, 0x268, 0x378, -1, 16, 8, 8, 0),
	PADCONF(146, 5, 0x128, 0x268, 0x378, -1, 20, 10, 10, 0),
	PADCONF(147, 5, 0x128, 0x268, 0x378, -1, 24, 12, 12, 0),
	PADCONF(148, 5, 0x128, 0x268, 0x378, -1, 28, 14, 14, 0),
	PADCONF(149, 7, 0x130, 0x270, -1, 0x480, 0, 0, 0, 0),
	PADCONF(150, 7, 0x130, 0x270, -1, 0x480, 4, 2, 0, 1),
	PADCONF(151, 7, 0x130, 0x270, -1, 0x480, 8, 4, 0, 2),
	PADCONF(152, 7, 0x130, 0x270, -1, 0x480, 12, 6, 0, 3),
	PADCONF(153, 7, 0x130, 0x270, -1, 0x480, 16, 8, 0, 4),
	PADCONF(154, 7, 0x130, 0x270, -1, 0x480, 20, 10, 0, 5),
	PADCONF(155, 7, 0x130, 0x270, -1, 0x480, 24, 12, 0, 6),
	PADCONF(156, 7, 0x130, 0x270, -1, 0x480, 28, 14, 0, 7),
	PADCONF(157, 7, 0x138, 0x278, -1, 0x480, 0, 0, 0, 8),
	PADCONF(158, 7, 0x138, 0x278, -1, 0x480, 4, 2, 0, 9),
	PADCONF(159, 5, 0x140, 0x280, 0x380, -1, 0, 0, 0, 0),
	PADCONF(160, 6, 0x140, 0x280, 0x380, -1, 4, 2, 2, 0),
	PADCONF(161, 5, 0x140, 0x280, 0x380, -1, 8, 4, 4, 0),
	PADCONF(162, 6, 0x140, 0x280, 0x380, -1, 12, 6, 6, 0),
	PADCONF(163, 6, 0x140, 0x280, 0x380, -1, 16, 8, 8, 0),
};

/* pin list of each pin group */
static const unsigned int gnss_gpio_pins[] = { 119, 120, 121, 122, 123, 124,
		125, 126, 127, 128, 22, 23, 24, 25, 26, 27, 28, 29, 30, };
static const unsigned int lcd_vip_gpio_pins[] = { 74, 75, 76, 77, 78, 79, 80,
		81, 82, 83, 84, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71, 72, 73, };
static const unsigned int sdio_i2s_gpio_pins[] = { 31, 32, 33, 34, 35, 36,
		85, 86, 87, 88, 89, 90, 129, 130, 131, 132, 91, 92, 93, 94,
		95, 96, 112, 113, 114, 115, 116, 117, 118, };
static const unsigned int sp_rgmii_gpio_pins[] = { 97, 98, 99, 100, 101, 102,
		103, 104, 105, 106, 107, 108, 109, 110, 111, 18, 19, 20, 21,
		141, 142, 143, 144, 145, 146, 147, 148, };
static const unsigned int lvds_gpio_pins[] = { 157, 158, 155, 156, 153, 154,
		151, 152, 149, 150, };
static const unsigned int jtag_uart_nand_gpio_pins[] = { 44, 43, 42, 41, 40,
		39, 38, 37, 46, 47, 48, 49, 50, 52, 51, 45, 133, 134, 135,
		136, 137, 138, 139, 140, 159, 160, 161, 162, 163, };
static const unsigned int rtc_gpio_pins[] = { 0, 1, 2, 3, 4, 10, 11, 12, 13,
		14, 15, 16, 17, 9, };
static const unsigned int audio_ac97_pins[] = { 113, 118, 115, 114, };
static const unsigned int audio_digmic_pins0[] = { 51, };
static const unsigned int audio_digmic_pins1[] = { 122, };
static const unsigned int audio_digmic_pins2[] = { 161, };
static const unsigned int audio_func_dbg_pins[] = { 141, 144, 44, 43, 42, 41,
		40, 39, 38, 37, 74, 75, 76, 77, 78, 79, 81, 113, 114, 118,
		115, 49, 50, 142, 143, 80, };
static const unsigned int audio_i2s_pins[] = { 118, 115, 116, 117, 112, 113,
		114, };
static const unsigned int audio_i2s_2ch_pins[] = { 118, 115, 112, 113, 114, };
static const unsigned int audio_i2s_extclk_pins[] = { 112, };
static const unsigned int audio_spdif_out_pins0[] = { 112, };
static const unsigned int audio_spdif_out_pins1[] = { 116, };
static const unsigned int audio_spdif_out_pins2[] = { 142, };
static const unsigned int audio_uart0_basic_pins[] = { 143, 142, 141, 144, };
static const unsigned int audio_uart0_urfs_pins0[] = { 117, };
static const unsigned int audio_uart0_urfs_pins1[] = { 139, };
static const unsigned int audio_uart0_urfs_pins2[] = { 163, };
static const unsigned int audio_uart0_urfs_pins3[] = { 162, };
static const unsigned int audio_uart1_basic_pins[] = { 147, 146, 145, 148, };
static const unsigned int audio_uart1_urfs_pins0[] = { 117, };
static const unsigned int audio_uart1_urfs_pins1[] = { 140, };
static const unsigned int audio_uart1_urfs_pins2[] = { 163, };
static const unsigned int audio_uart2_urfs_pins0[] = { 139, };
static const unsigned int audio_uart2_urfs_pins1[] = { 163, };
static const unsigned int audio_uart2_urfs_pins2[] = { 96, };
static const unsigned int audio_uart2_urxd_pins0[] = { 20, };
static const unsigned int audio_uart2_urxd_pins1[] = { 109, };
static const unsigned int audio_uart2_urxd_pins2[] = { 93, };
static const unsigned int audio_uart2_usclk_pins0[] = { 19, };
static const unsigned int audio_uart2_usclk_pins1[] = { 101, };
static const unsigned int audio_uart2_usclk_pins2[] = { 91, };
static const unsigned int audio_uart2_utfs_pins0[] = { 18, };
static const unsigned int audio_uart2_utfs_pins1[] = { 111, };
static const unsigned int audio_uart2_utfs_pins2[] = { 94, };
static const unsigned int audio_uart2_utxd_pins0[] = { 21, };
static const unsigned int audio_uart2_utxd_pins1[] = { 110, };
static const unsigned int audio_uart2_utxd_pins2[] = { 92, };
static const unsigned int c_can_trnsvr_en_pins0[] = { 2, };
static const unsigned int c_can_trnsvr_en_pins1[] = { 0, };
static const unsigned int c_can_trnsvr_intr_pins[] = { 1, };
static const unsigned int c_can_trnsvr_stb_n_pins[] = { 3, };
static const unsigned int c0_can_rxd_trnsv0_pins[] = { 11, };
static const unsigned int c0_can_rxd_trnsv1_pins[] = { 2, };
static const unsigned int c0_can_txd_trnsv0_pins[] = { 10, };
static const unsigned int c0_can_txd_trnsv1_pins[] = { 3, };
static const unsigned int c1_can_rxd_pins0[] = { 138, };
static const unsigned int c1_can_rxd_pins1[] = { 147, };
static const unsigned int c1_can_rxd_pins2[] = { 2, };
static const unsigned int c1_can_rxd_pins3[] = { 162, };
static const unsigned int c1_can_txd_pins0[] = { 137, };
static const unsigned int c1_can_txd_pins1[] = { 146, };
static const unsigned int c1_can_txd_pins2[] = { 3, };
static const unsigned int c1_can_txd_pins3[] = { 161, };
static const unsigned int ca_audio_lpc_pins[] = { 62, 63, 64, 65, 66, 67, 68,
		69, 70, 71, };
static const unsigned int ca_bt_lpc_pins[] = { 85, 86, 87, 88, 89, 90, };
static const unsigned int ca_coex_pins[] = { 129, 130, 131, 132, };
static const unsigned int ca_curator_lpc_pins[] = { 57, 58, 59, 60, };
static const unsigned int ca_pcm_debug_pins[] = { 91, 93, 94, 92, };
static const unsigned int ca_pio_pins[] = { 121, 122, 125, 126, 38, 37, 47,
		49, 50, 54, 55, 56, };
static const unsigned int ca_sdio_debug_pins[] = { 40, 39, 44, 43, 42, 41, };
static const unsigned int ca_spi_pins[] = { 82, 79, 80, 81, };
static const unsigned int ca_trb_pins[] = { 91, 93, 94, 95, 96, 78, 74, 75,
		76, 77, };
static const unsigned int ca_uart_debug_pins[] = { 136, 135, 134, 133, };
static const unsigned int clkc_pins0[] = { 30, 47, };
static const unsigned int clkc_pins1[] = { 78, 54, };
static const unsigned int gn_gnss_i2c_pins[] = { 128, 127, };
static const unsigned int gn_gnss_uart_nopause_pins[] = { 134, 133, };
static const unsigned int gn_gnss_uart_pins[] = { 134, 133, 136, 135, };
static const unsigned int gn_trg_spi_pins0[] = { 22, 25, 23, 24, };
static const unsigned int gn_trg_spi_pins1[] = { 82, 79, 80, 81, };
static const unsigned int cvbs_dbg_pins[] = { 54, 53, 82, 74, 75, 76, 77, 78,
		79, 80, 81, 83, 84, 73, 55, 56, };
static const unsigned int cvbs_dbg_test_pins0[] = { 57, };
static const unsigned int cvbs_dbg_test_pins1[] = { 58, };
static const unsigned int cvbs_dbg_test_pins2[] = { 59, };
static const unsigned int cvbs_dbg_test_pins3[] = { 60, };
static const unsigned int cvbs_dbg_test_pins4[] = { 61, };
static const unsigned int cvbs_dbg_test_pins5[] = { 62, };
static const unsigned int cvbs_dbg_test_pins6[] = { 63, };
static const unsigned int cvbs_dbg_test_pins7[] = { 64, };
static const unsigned int cvbs_dbg_test_pins8[] = { 65, };
static const unsigned int cvbs_dbg_test_pins9[] = { 66, };
static const unsigned int cvbs_dbg_test_pins10[] = { 67, };
static const unsigned int cvbs_dbg_test_pins11[] = { 68, };
static const unsigned int cvbs_dbg_test_pins12[] = { 69, };
static const unsigned int cvbs_dbg_test_pins13[] = { 70, };
static const unsigned int cvbs_dbg_test_pins14[] = { 71, };
static const unsigned int cvbs_dbg_test_pins15[] = { 72, };
static const unsigned int gn_gnss_power_pins[] = { 123, 124, 121, 122, 125,
		120, };
static const unsigned int gn_gnss_sw_status_pins[] = { 57, 58, 59, 60, 61,
		62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 53, 55, 56, 54, };
static const unsigned int gn_gnss_eclk_pins[] = { 113, };
static const unsigned int gn_gnss_irq1_pins0[] = { 112, };
static const unsigned int gn_gnss_irq2_pins0[] = { 118, };
static const unsigned int gn_gnss_tm_pins[] = { 115, };
static const unsigned int gn_gnss_tsync_pins[] = { 114, };
static const unsigned int gn_io_gnsssys_sw_cfg_pins[] = { 44, 43, 42, 41, 40,
		39, 38, 37, 49, 50, 91, 92, 93, 94, 95, 96, };
static const unsigned int gn_trg_pins0[] = { 29, 28, 26, 27, };
static const unsigned int gn_trg_pins1[] = { 77, 76, 74, 75, };
static const unsigned int gn_trg_shutdown_pins0[] = { 30, };
static const unsigned int gn_trg_shutdown_pins1[] = { 83, };
static const unsigned int gn_trg_shutdown_pins2[] = { 117, };
static const unsigned int gn_trg_shutdown_pins3[] = { 123, };
static const unsigned int i2c0_pins[] = { 128, 127, };
static const unsigned int i2c1_pins[] = { 126, 125, };
static const unsigned int i2s0_pins[] = { 91, 93, 94, 92, };
static const unsigned int i2s1_basic_pins[] = { 95, 96, };
static const unsigned int i2s1_rxd0_pins0[] = { 61, };
static const unsigned int i2s1_rxd0_pins1[] = { 131, };
static const unsigned int i2s1_rxd0_pins2[] = { 129, };
static const unsigned int i2s1_rxd0_pins3[] = { 117, };
static const unsigned int i2s1_rxd0_pins4[] = { 83, };
static const unsigned int i2s1_rxd1_pins0[] = { 72, };
static const unsigned int i2s1_rxd1_pins1[] = { 132, };
static const unsigned int i2s1_rxd1_pins2[] = { 130, };
static const unsigned int i2s1_rxd1_pins3[] = { 118, };
static const unsigned int i2s1_rxd1_pins4[] = { 84, };
static const unsigned int jtag_jt_dbg_nsrst_pins[] = { 125, };
static const unsigned int jtag_ntrst_pins0[] = { 4, };
static const unsigned int jtag_ntrst_pins1[] = { 163, };
static const unsigned int jtag_swdiotms_pins0[] = { 2, };
static const unsigned int jtag_swdiotms_pins1[] = { 160, };
static const unsigned int jtag_tck_pins0[] = { 0, };
static const unsigned int jtag_tck_pins1[] = { 161, };
static const unsigned int jtag_tdi_pins0[] = { 1, };
static const unsigned int jtag_tdi_pins1[] = { 162, };
static const unsigned int jtag_tdo_pins0[] = { 3, };
static const unsigned int jtag_tdo_pins1[] = { 159, };
static const unsigned int ks_kas_spi_pins0[] = { 141, 144, 143, 142, };
static const unsigned int ld_ldd_pins[] = { 57, 58, 59, 60, 61, 62, 63, 64,
		65, 66, 67, 68, 69, 70, 71, 72, 74, 75, 76, 77, 78, 79, 80,
		81, 56, 53, };
static const unsigned int ld_ldd_16bit_pins[] = { 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71, 72, 56, 53, };
static const unsigned int ld_ldd_fck_pins[] = { 55, };
static const unsigned int ld_ldd_lck_pins[] = { 54, };
static const unsigned int lr_lcdrom_pins[] = { 73, 54, 57, 58, 59, 60, 61,
		62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 56, 53, 55, };
static const unsigned int lvds_analog_pins[] = { 149, 150, 151, 152, 153, 154,
		155, 156, 157, 158, };
static const unsigned int nd_df_basic_pins[] = { 44, 43, 42, 41, 40, 39, 38,
		37, 47, 46, 52, 45, 49, 50, 48, };
static const unsigned int nd_df_wp_pins[] = { 124, };
static const unsigned int nd_df_cs_pins[] = { 51, };
static const unsigned int ps_pins[] = { 120, 119, 121, };
static const unsigned int ps_no_dir_pins[] = { 119, };
static const unsigned int pwc_core_on_pins[] = { 8, };
static const unsigned int pwc_ext_on_pins[] = { 6, };
static const unsigned int pwc_gpio3_clk_pins[] = { 3, };
static const unsigned int pwc_io_on_pins[] = { 9, };
static const unsigned int pwc_lowbatt_b_pins0[] = { 4, };
static const unsigned int pwc_mem_on_pins[] = { 7, };
static const unsigned int pwc_on_key_b_pins0[] = { 5, };
static const unsigned int pwc_wakeup_src0_pins[] = { 0, };
static const unsigned int pwc_wakeup_src1_pins[] = { 1, };
static const unsigned int pwc_wakeup_src2_pins[] = { 2, };
static const unsigned int pwc_wakeup_src3_pins[] = { 3, };
static const unsigned int pw_cko0_pins0[] = { 123, };
static const unsigned int pw_cko0_pins1[] = { 101, };
static const unsigned int pw_cko0_pins2[] = { 82, };
static const unsigned int pw_cko0_pins3[] = { 162, };
static const unsigned int pw_cko1_pins0[] = { 124, };
static const unsigned int pw_cko1_pins1[] = { 110, };
static const unsigned int pw_cko1_pins2[] = { 163, };
static const unsigned int pw_i2s01_clk_pins0[] = { 125, };
static const unsigned int pw_i2s01_clk_pins1[] = { 117, };
static const unsigned int pw_i2s01_clk_pins2[] = { 132, };
static const unsigned int pw_pwm0_pins0[] = { 119, };
static const unsigned int pw_pwm0_pins1[] = { 159, };
static const unsigned int pw_pwm1_pins0[] = { 120, };
static const unsigned int pw_pwm1_pins1[] = { 160, };
static const unsigned int pw_pwm1_pins2[] = { 131, };
static const unsigned int pw_pwm2_pins0[] = { 121, };
static const unsigned int pw_pwm2_pins1[] = { 98, };
static const unsigned int pw_pwm2_pins2[] = { 161, };
static const unsigned int pw_pwm3_pins0[] = { 122, };
static const unsigned int pw_pwm3_pins1[] = { 73, };
static const unsigned int pw_pwm_cpu_vol_pins0[] = { 121, };
static const unsigned int pw_pwm_cpu_vol_pins1[] = { 98, };
static const unsigned int pw_pwm_cpu_vol_pins2[] = { 161, };
static const unsigned int pw_backlight_pins0[] = { 122, };
static const unsigned int pw_backlight_pins1[] = { 73, };
static const unsigned int rg_eth_mac_pins[] = { 108, 103, 104, 105, 106, 107,
		102, 97, 98, 99, 100, 101, };
static const unsigned int rg_gmac_phy_intr_n_pins[] = { 111, };
static const unsigned int rg_rgmii_mac_pins[] = { 109, 110, };
static const unsigned int rg_rgmii_phy_ref_clk_pins0[] = { 111, };
static const unsigned int rg_rgmii_phy_ref_clk_pins1[] = { 53, };
static const unsigned int sd0_pins[] = { 46, 47, 44, 43, 42, 41, 40, 39, 38,
		37, };
static const unsigned int sd0_4bit_pins[] = { 46, 47, 44, 43, 42, 41, };
static const unsigned int sd1_pins[] = { 48, 49, 44, 43, 42, 41, 40, 39, 38,
		37, };
static const unsigned int sd1_4bit_pins0[] = { 48, 49, 44, 43, 42, 41, };
static const unsigned int sd1_4bit_pins1[] = { 48, 49, 40, 39, 38, 37, };
static const unsigned int sd2_basic_pins[] = { 31, 32, 33, 34, 35, 36, };
static const unsigned int sd2_cdb_pins0[] = { 124, };
static const unsigned int sd2_cdb_pins1[] = { 161, };
static const unsigned int sd2_wpb_pins0[] = { 123, };
static const unsigned int sd2_wpb_pins1[] = { 163, };
static const unsigned int sd3_9_pins[] = { 85, 86, 87, 88, 89, 90, };
static const unsigned int sd5_pins[] = { 91, 92, 93, 94, 95, 96, };
static const unsigned int sd6_pins0[] = { 79, 78, 74, 75, 76, 77, };
static const unsigned int sd6_pins1[] = { 101, 99, 100, 110, 109, 111, };
static const unsigned int sp0_ext_ldo_on_pins[] = { 4, };
static const unsigned int sp0_qspi_pins[] = { 12, 13, 14, 15, 16, 17, };
static const unsigned int sp1_spi_pins[] = { 19, 20, 21, 18, };
static const unsigned int tpiu_trace_pins[] = { 53, 56, 57, 58, 59, 60, 61,
		62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, };
static const unsigned int uart0_pins[] = { 121, 120, 134, 133, };
static const unsigned int uart0_nopause_pins[] = { 134, 133, };
static const unsigned int uart1_pins[] = { 136, 135, };
static const unsigned int uart2_cts_pins0[] = { 132, };
static const unsigned int uart2_cts_pins1[] = { 162, };
static const unsigned int uart2_rts_pins0[] = { 131, };
static const unsigned int uart2_rts_pins1[] = { 161, };
static const unsigned int uart2_rxd_pins0[] = { 11, };
static const unsigned int uart2_rxd_pins1[] = { 160, };
static const unsigned int uart2_rxd_pins2[] = { 130, };
static const unsigned int uart2_txd_pins0[] = { 10, };
static const unsigned int uart2_txd_pins1[] = { 159, };
static const unsigned int uart2_txd_pins2[] = { 129, };
static const unsigned int uart3_cts_pins0[] = { 125, };
static const unsigned int uart3_cts_pins1[] = { 111, };
static const unsigned int uart3_cts_pins2[] = { 140, };
static const unsigned int uart3_rts_pins0[] = { 126, };
static const unsigned int uart3_rts_pins1[] = { 109, };
static const unsigned int uart3_rts_pins2[] = { 139, };
static const unsigned int uart3_rxd_pins0[] = { 138, };
static const unsigned int uart3_rxd_pins1[] = { 84, };
static const unsigned int uart3_rxd_pins2[] = { 162, };
static const unsigned int uart3_txd_pins0[] = { 137, };
static const unsigned int uart3_txd_pins1[] = { 83, };
static const unsigned int uart3_txd_pins2[] = { 161, };
static const unsigned int uart4_basic_pins[] = { 140, 139, };
static const unsigned int uart4_cts_pins0[] = { 122, };
static const unsigned int uart4_cts_pins1[] = { 100, };
static const unsigned int uart4_cts_pins2[] = { 117, };
static const unsigned int uart4_rts_pins0[] = { 123, };
static const unsigned int uart4_rts_pins1[] = { 99, };
static const unsigned int uart4_rts_pins2[] = { 116, };
static const unsigned int usb0_drvvbus_pins0[] = { 51, };
static const unsigned int usb0_drvvbus_pins1[] = { 162, };
static const unsigned int usb1_drvvbus_pins0[] = { 134, };
static const unsigned int usb1_drvvbus_pins1[] = { 163, };
static const unsigned int visbus_dout_pins[] = { 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71, 72, 53, 54, 55, 56, 85, 86,
		87, 88, 89, 90, 91, 92, 93, 94, 95, 96, };
static const unsigned int vi_vip1_pins[] = { 74, 75, 76, 77, 78, 79, 80, 81,
		82, 83, 84, 103, 104, 105, 106, 107, 102, 97, 98, };
static const unsigned int vi_vip1_ext_pins[] = { 74, 75, 76, 77, 78, 79, 80,
		81, 82, 83, 84, 108, 103, 104, 105, 106, 107, 102, 97, 98,
		99, 100, };
static const unsigned int vi_vip1_low8bit_pins[] = { 74, 75, 76, 77, 78, 79,
		80, 81, 82, 83, 84, };
static const unsigned int vi_vip1_high8bit_pins[] = { 82, 83, 84, 103, 104,
		105, 106, 107, 102, 97, 98, };

/* definition of pin group table */
static struct atlas7_pin_group altas7_pin_groups[] = {
	GROUP("gnss_gpio_grp", gnss_gpio_pins),
	GROUP("lcd_vip_gpio_grp", lcd_vip_gpio_pins),
	GROUP("sdio_i2s_gpio_grp", sdio_i2s_gpio_pins),
	GROUP("sp_rgmii_gpio_grp", sp_rgmii_gpio_pins),
	GROUP("lvds_gpio_grp", lvds_gpio_pins),
	GROUP("jtag_uart_nand_gpio_grp", jtag_uart_nand_gpio_pins),
	GROUP("rtc_gpio_grp", rtc_gpio_pins),
	GROUP("audio_ac97_grp", audio_ac97_pins),
	GROUP("audio_digmic_grp0", audio_digmic_pins0),
	GROUP("audio_digmic_grp1", audio_digmic_pins1),
	GROUP("audio_digmic_grp2", audio_digmic_pins2),
	GROUP("audio_func_dbg_grp", audio_func_dbg_pins),
	GROUP("audio_i2s_grp", audio_i2s_pins),
	GROUP("audio_i2s_2ch_grp", audio_i2s_2ch_pins),
	GROUP("audio_i2s_extclk_grp", audio_i2s_extclk_pins),
	GROUP("audio_spdif_out_grp0", audio_spdif_out_pins0),
	GROUP("audio_spdif_out_grp1", audio_spdif_out_pins1),
	GROUP("audio_spdif_out_grp2", audio_spdif_out_pins2),
	GROUP("audio_uart0_basic_grp", audio_uart0_basic_pins),
	GROUP("audio_uart0_urfs_grp0", audio_uart0_urfs_pins0),
	GROUP("audio_uart0_urfs_grp1", audio_uart0_urfs_pins1),
	GROUP("audio_uart0_urfs_grp2", audio_uart0_urfs_pins2),
	GROUP("audio_uart0_urfs_grp3", audio_uart0_urfs_pins3),
	GROUP("audio_uart1_basic_grp", audio_uart1_basic_pins),
	GROUP("audio_uart1_urfs_grp0", audio_uart1_urfs_pins0),
	GROUP("audio_uart1_urfs_grp1", audio_uart1_urfs_pins1),
	GROUP("audio_uart1_urfs_grp2", audio_uart1_urfs_pins2),
	GROUP("audio_uart2_urfs_grp0", audio_uart2_urfs_pins0),
	GROUP("audio_uart2_urfs_grp1", audio_uart2_urfs_pins1),
	GROUP("audio_uart2_urfs_grp2", audio_uart2_urfs_pins2),
	GROUP("audio_uart2_urxd_grp0", audio_uart2_urxd_pins0),
	GROUP("audio_uart2_urxd_grp1", audio_uart2_urxd_pins1),
	GROUP("audio_uart2_urxd_grp2", audio_uart2_urxd_pins2),
	GROUP("audio_uart2_usclk_grp0", audio_uart2_usclk_pins0),
	GROUP("audio_uart2_usclk_grp1", audio_uart2_usclk_pins1),
	GROUP("audio_uart2_usclk_grp2", audio_uart2_usclk_pins2),
	GROUP("audio_uart2_utfs_grp0", audio_uart2_utfs_pins0),
	GROUP("audio_uart2_utfs_grp1", audio_uart2_utfs_pins1),
	GROUP("audio_uart2_utfs_grp2", audio_uart2_utfs_pins2),
	GROUP("audio_uart2_utxd_grp0", audio_uart2_utxd_pins0),
	GROUP("audio_uart2_utxd_grp1", audio_uart2_utxd_pins1),
	GROUP("audio_uart2_utxd_grp2", audio_uart2_utxd_pins2),
	GROUP("c_can_trnsvr_en_grp0", c_can_trnsvr_en_pins0),
	GROUP("c_can_trnsvr_en_grp1", c_can_trnsvr_en_pins1),
	GROUP("c_can_trnsvr_intr_grp", c_can_trnsvr_intr_pins),
	GROUP("c_can_trnsvr_stb_n_grp", c_can_trnsvr_stb_n_pins),
	GROUP("c0_can_rxd_trnsv0_grp", c0_can_rxd_trnsv0_pins),
	GROUP("c0_can_rxd_trnsv1_grp", c0_can_rxd_trnsv1_pins),
	GROUP("c0_can_txd_trnsv0_grp", c0_can_txd_trnsv0_pins),
	GROUP("c0_can_txd_trnsv1_grp", c0_can_txd_trnsv1_pins),
	GROUP("c1_can_rxd_grp0", c1_can_rxd_pins0),
	GROUP("c1_can_rxd_grp1", c1_can_rxd_pins1),
	GROUP("c1_can_rxd_grp2", c1_can_rxd_pins2),
	GROUP("c1_can_rxd_grp3", c1_can_rxd_pins3),
	GROUP("c1_can_txd_grp0", c1_can_txd_pins0),
	GROUP("c1_can_txd_grp1", c1_can_txd_pins1),
	GROUP("c1_can_txd_grp2", c1_can_txd_pins2),
	GROUP("c1_can_txd_grp3", c1_can_txd_pins3),
	GROUP("ca_audio_lpc_grp", ca_audio_lpc_pins),
	GROUP("ca_bt_lpc_grp", ca_bt_lpc_pins),
	GROUP("ca_coex_grp", ca_coex_pins),
	GROUP("ca_curator_lpc_grp", ca_curator_lpc_pins),
	GROUP("ca_pcm_debug_grp", ca_pcm_debug_pins),
	GROUP("ca_pio_grp", ca_pio_pins),
	GROUP("ca_sdio_debug_grp", ca_sdio_debug_pins),
	GROUP("ca_spi_grp", ca_spi_pins),
	GROUP("ca_trb_grp", ca_trb_pins),
	GROUP("ca_uart_debug_grp", ca_uart_debug_pins),
	GROUP("clkc_grp0", clkc_pins0),
	GROUP("clkc_grp1", clkc_pins1),
	GROUP("gn_gnss_i2c_grp", gn_gnss_i2c_pins),
	GROUP("gn_gnss_uart_nopause_grp", gn_gnss_uart_nopause_pins),
	GROUP("gn_gnss_uart_grp", gn_gnss_uart_pins),
	GROUP("gn_trg_spi_grp0", gn_trg_spi_pins0),
	GROUP("gn_trg_spi_grp1", gn_trg_spi_pins1),
	GROUP("cvbs_dbg_grp", cvbs_dbg_pins),
	GROUP("cvbs_dbg_test_grp0", cvbs_dbg_test_pins0),
	GROUP("cvbs_dbg_test_grp1", cvbs_dbg_test_pins1),
	GROUP("cvbs_dbg_test_grp2", cvbs_dbg_test_pins2),
	GROUP("cvbs_dbg_test_grp3", cvbs_dbg_test_pins3),
	GROUP("cvbs_dbg_test_grp4", cvbs_dbg_test_pins4),
	GROUP("cvbs_dbg_test_grp5", cvbs_dbg_test_pins5),
	GROUP("cvbs_dbg_test_grp6", cvbs_dbg_test_pins6),
	GROUP("cvbs_dbg_test_grp7", cvbs_dbg_test_pins7),
	GROUP("cvbs_dbg_test_grp8", cvbs_dbg_test_pins8),
	GROUP("cvbs_dbg_test_grp9", cvbs_dbg_test_pins9),
	GROUP("cvbs_dbg_test_grp10", cvbs_dbg_test_pins10),
	GROUP("cvbs_dbg_test_grp11", cvbs_dbg_test_pins11),
	GROUP("cvbs_dbg_test_grp12", cvbs_dbg_test_pins12),
	GROUP("cvbs_dbg_test_grp13", cvbs_dbg_test_pins13),
	GROUP("cvbs_dbg_test_grp14", cvbs_dbg_test_pins14),
	GROUP("cvbs_dbg_test_grp15", cvbs_dbg_test_pins15),
	GROUP("gn_gnss_power_grp", gn_gnss_power_pins),
	GROUP("gn_gnss_sw_status_grp", gn_gnss_sw_status_pins),
	GROUP("gn_gnss_eclk_grp", gn_gnss_eclk_pins),
	GROUP("gn_gnss_irq1_grp0", gn_gnss_irq1_pins0),
	GROUP("gn_gnss_irq2_grp0", gn_gnss_irq2_pins0),
	GROUP("gn_gnss_tm_grp", gn_gnss_tm_pins),
	GROUP("gn_gnss_tsync_grp", gn_gnss_tsync_pins),
	GROUP("gn_io_gnsssys_sw_cfg_grp", gn_io_gnsssys_sw_cfg_pins),
	GROUP("gn_trg_grp0", gn_trg_pins0),
	GROUP("gn_trg_grp1", gn_trg_pins1),
	GROUP("gn_trg_shutdown_grp0", gn_trg_shutdown_pins0),
	GROUP("gn_trg_shutdown_grp1", gn_trg_shutdown_pins1),
	GROUP("gn_trg_shutdown_grp2", gn_trg_shutdown_pins2),
	GROUP("gn_trg_shutdown_grp3", gn_trg_shutdown_pins3),
	GROUP("i2c0_grp", i2c0_pins),
	GROUP("i2c1_grp", i2c1_pins),
	GROUP("i2s0_grp", i2s0_pins),
	GROUP("i2s1_basic_grp", i2s1_basic_pins),
	GROUP("i2s1_rxd0_grp0", i2s1_rxd0_pins0),
	GROUP("i2s1_rxd0_grp1", i2s1_rxd0_pins1),
	GROUP("i2s1_rxd0_grp2", i2s1_rxd0_pins2),
	GROUP("i2s1_rxd0_grp3", i2s1_rxd0_pins3),
	GROUP("i2s1_rxd0_grp4", i2s1_rxd0_pins4),
	GROUP("i2s1_rxd1_grp0", i2s1_rxd1_pins0),
	GROUP("i2s1_rxd1_grp1", i2s1_rxd1_pins1),
	GROUP("i2s1_rxd1_grp2", i2s1_rxd1_pins2),
	GROUP("i2s1_rxd1_grp3", i2s1_rxd1_pins3),
	GROUP("i2s1_rxd1_grp4", i2s1_rxd1_pins4),
	GROUP("jtag_jt_dbg_nsrst_grp", jtag_jt_dbg_nsrst_pins),
	GROUP("jtag_ntrst_grp0", jtag_ntrst_pins0),
	GROUP("jtag_ntrst_grp1", jtag_ntrst_pins1),
	GROUP("jtag_swdiotms_grp0", jtag_swdiotms_pins0),
	GROUP("jtag_swdiotms_grp1", jtag_swdiotms_pins1),
	GROUP("jtag_tck_grp0", jtag_tck_pins0),
	GROUP("jtag_tck_grp1", jtag_tck_pins1),
	GROUP("jtag_tdi_grp0", jtag_tdi_pins0),
	GROUP("jtag_tdi_grp1", jtag_tdi_pins1),
	GROUP("jtag_tdo_grp0", jtag_tdo_pins0),
	GROUP("jtag_tdo_grp1", jtag_tdo_pins1),
	GROUP("ks_kas_spi_grp0", ks_kas_spi_pins0),
	GROUP("ld_ldd_grp", ld_ldd_pins),
	GROUP("ld_ldd_16bit_grp", ld_ldd_16bit_pins),
	GROUP("ld_ldd_fck_grp", ld_ldd_fck_pins),
	GROUP("ld_ldd_lck_grp", ld_ldd_lck_pins),
	GROUP("lr_lcdrom_grp", lr_lcdrom_pins),
	GROUP("lvds_analog_grp", lvds_analog_pins),
	GROUP("nd_df_basic_grp", nd_df_basic_pins),
	GROUP("nd_df_wp_grp", nd_df_wp_pins),
	GROUP("nd_df_cs_grp", nd_df_cs_pins),
	GROUP("ps_grp", ps_pins),
	GROUP("ps_no_dir_grp", ps_no_dir_pins),
	GROUP("pwc_core_on_grp", pwc_core_on_pins),
	GROUP("pwc_ext_on_grp", pwc_ext_on_pins),
	GROUP("pwc_gpio3_clk_grp", pwc_gpio3_clk_pins),
	GROUP("pwc_io_on_grp", pwc_io_on_pins),
	GROUP("pwc_lowbatt_b_grp0", pwc_lowbatt_b_pins0),
	GROUP("pwc_mem_on_grp", pwc_mem_on_pins),
	GROUP("pwc_on_key_b_grp0", pwc_on_key_b_pins0),
	GROUP("pwc_wakeup_src0_grp", pwc_wakeup_src0_pins),
	GROUP("pwc_wakeup_src1_grp", pwc_wakeup_src1_pins),
	GROUP("pwc_wakeup_src2_grp", pwc_wakeup_src2_pins),
	GROUP("pwc_wakeup_src3_grp", pwc_wakeup_src3_pins),
	GROUP("pw_cko0_grp0", pw_cko0_pins0),
	GROUP("pw_cko0_grp1", pw_cko0_pins1),
	GROUP("pw_cko0_grp2", pw_cko0_pins2),
	GROUP("pw_cko0_grp3", pw_cko0_pins3),
	GROUP("pw_cko1_grp0", pw_cko1_pins0),
	GROUP("pw_cko1_grp1", pw_cko1_pins1),
	GROUP("pw_cko1_grp2", pw_cko1_pins2),
	GROUP("pw_i2s01_clk_grp0", pw_i2s01_clk_pins0),
	GROUP("pw_i2s01_clk_grp1", pw_i2s01_clk_pins1),
	GROUP("pw_i2s01_clk_grp2", pw_i2s01_clk_pins2),
	GROUP("pw_pwm0_grp0", pw_pwm0_pins0),
	GROUP("pw_pwm0_grp1", pw_pwm0_pins1),
	GROUP("pw_pwm1_grp0", pw_pwm1_pins0),
	GROUP("pw_pwm1_grp1", pw_pwm1_pins1),
	GROUP("pw_pwm1_grp2", pw_pwm1_pins2),
	GROUP("pw_pwm2_grp0", pw_pwm2_pins0),
	GROUP("pw_pwm2_grp1", pw_pwm2_pins1),
	GROUP("pw_pwm2_grp2", pw_pwm2_pins2),
	GROUP("pw_pwm3_grp0", pw_pwm3_pins0),
	GROUP("pw_pwm3_grp1", pw_pwm3_pins1),
	GROUP("pw_pwm_cpu_vol_grp0", pw_pwm_cpu_vol_pins0),
	GROUP("pw_pwm_cpu_vol_grp1", pw_pwm_cpu_vol_pins1),
	GROUP("pw_pwm_cpu_vol_grp2", pw_pwm_cpu_vol_pins2),
	GROUP("pw_backlight_grp0", pw_backlight_pins0),
	GROUP("pw_backlight_grp1", pw_backlight_pins1),
	GROUP("rg_eth_mac_grp", rg_eth_mac_pins),
	GROUP("rg_gmac_phy_intr_n_grp", rg_gmac_phy_intr_n_pins),
	GROUP("rg_rgmii_mac_grp", rg_rgmii_mac_pins),
	GROUP("rg_rgmii_phy_ref_clk_grp0", rg_rgmii_phy_ref_clk_pins0),
	GROUP("rg_rgmii_phy_ref_clk_grp1", rg_rgmii_phy_ref_clk_pins1),
	GROUP("sd0_grp", sd0_pins),
	GROUP("sd0_4bit_grp", sd0_4bit_pins),
	GROUP("sd1_grp", sd1_pins),
	GROUP("sd1_4bit_grp0", sd1_4bit_pins0),
	GROUP("sd1_4bit_grp1", sd1_4bit_pins1),
	GROUP("sd2_basic_grp", sd2_basic_pins),
	GROUP("sd2_cdb_grp0", sd2_cdb_pins0),
	GROUP("sd2_cdb_grp1", sd2_cdb_pins1),
	GROUP("sd2_wpb_grp0", sd2_wpb_pins0),
	GROUP("sd2_wpb_grp1", sd2_wpb_pins1),
	GROUP("sd3_9_grp", sd3_9_pins),
	GROUP("sd5_grp", sd5_pins),
	GROUP("sd6_grp0", sd6_pins0),
	GROUP("sd6_grp1", sd6_pins1),
	GROUP("sp0_ext_ldo_on_grp", sp0_ext_ldo_on_pins),
	GROUP("sp0_qspi_grp", sp0_qspi_pins),
	GROUP("sp1_spi_grp", sp1_spi_pins),
	GROUP("tpiu_trace_grp", tpiu_trace_pins),
	GROUP("uart0_grp", uart0_pins),
	GROUP("uart0_nopause_grp", uart0_nopause_pins),
	GROUP("uart1_grp", uart1_pins),
	GROUP("uart2_cts_grp0", uart2_cts_pins0),
	GROUP("uart2_cts_grp1", uart2_cts_pins1),
	GROUP("uart2_rts_grp0", uart2_rts_pins0),
	GROUP("uart2_rts_grp1", uart2_rts_pins1),
	GROUP("uart2_rxd_grp0", uart2_rxd_pins0),
	GROUP("uart2_rxd_grp1", uart2_rxd_pins1),
	GROUP("uart2_rxd_grp2", uart2_rxd_pins2),
	GROUP("uart2_txd_grp0", uart2_txd_pins0),
	GROUP("uart2_txd_grp1", uart2_txd_pins1),
	GROUP("uart2_txd_grp2", uart2_txd_pins2),
	GROUP("uart3_cts_grp0", uart3_cts_pins0),
	GROUP("uart3_cts_grp1", uart3_cts_pins1),
	GROUP("uart3_cts_grp2", uart3_cts_pins2),
	GROUP("uart3_rts_grp0", uart3_rts_pins0),
	GROUP("uart3_rts_grp1", uart3_rts_pins1),
	GROUP("uart3_rts_grp2", uart3_rts_pins2),
	GROUP("uart3_rxd_grp0", uart3_rxd_pins0),
	GROUP("uart3_rxd_grp1", uart3_rxd_pins1),
	GROUP("uart3_rxd_grp2", uart3_rxd_pins2),
	GROUP("uart3_txd_grp0", uart3_txd_pins0),
	GROUP("uart3_txd_grp1", uart3_txd_pins1),
	GROUP("uart3_txd_grp2", uart3_txd_pins2),
	GROUP("uart4_basic_grp", uart4_basic_pins),
	GROUP("uart4_cts_grp0", uart4_cts_pins0),
	GROUP("uart4_cts_grp1", uart4_cts_pins1),
	GROUP("uart4_cts_grp2", uart4_cts_pins2),
	GROUP("uart4_rts_grp0", uart4_rts_pins0),
	GROUP("uart4_rts_grp1", uart4_rts_pins1),
	GROUP("uart4_rts_grp2", uart4_rts_pins2),
	GROUP("usb0_drvvbus_grp0", usb0_drvvbus_pins0),
	GROUP("usb0_drvvbus_grp1", usb0_drvvbus_pins1),
	GROUP("usb1_drvvbus_grp0", usb1_drvvbus_pins0),
	GROUP("usb1_drvvbus_grp1", usb1_drvvbus_pins1),
	GROUP("visbus_dout_grp", visbus_dout_pins),
	GROUP("vi_vip1_grp", vi_vip1_pins),
	GROUP("vi_vip1_ext_grp", vi_vip1_ext_pins),
	GROUP("vi_vip1_low8bit_grp", vi_vip1_low8bit_pins),
	GROUP("vi_vip1_high8bit_grp", vi_vip1_high8bit_pins),
};

/* How many groups that a function can use */
static const char * const gnss_gpio_grp[] = { "gnss_gpio_grp", };
static const char * const lcd_vip_gpio_grp[] = { "lcd_vip_gpio_grp", };
static const char * const sdio_i2s_gpio_grp[] = { "sdio_i2s_gpio_grp", };
static const char * const sp_rgmii_gpio_grp[] = { "sp_rgmii_gpio_grp", };
static const char * const lvds_gpio_grp[] = { "lvds_gpio_grp", };
static const char * const jtag_uart_nand_gpio_grp[] = {
				"jtag_uart_nand_gpio_grp", };
static const char * const rtc_gpio_grp[] = { "rtc_gpio_grp", };
static const char * const audio_ac97_grp[] = { "audio_ac97_grp", };
static const char * const audio_digmic_grp0[] = { "audio_digmic_grp0", };
static const char * const audio_digmic_grp1[] = { "audio_digmic_grp1", };
static const char * const audio_digmic_grp2[] = { "audio_digmic_grp2", };
static const char * const audio_func_dbg_grp[] = { "audio_func_dbg_grp", };
static const char * const audio_i2s_grp[] = { "audio_i2s_grp", };
static const char * const audio_i2s_2ch_grp[] = { "audio_i2s_2ch_grp", };
static const char * const audio_i2s_extclk_grp[] = { "audio_i2s_extclk_grp", };
static const char * const audio_spdif_out_grp0[] = { "audio_spdif_out_grp0", };
static const char * const audio_spdif_out_grp1[] = { "audio_spdif_out_grp1", };
static const char * const audio_spdif_out_grp2[] = { "audio_spdif_out_grp2", };
static const char * const audio_uart0_basic_grp[] = {
				"audio_uart0_basic_grp", };
static const char * const audio_uart0_urfs_grp0[] = {
				"audio_uart0_urfs_grp0", };
static const char * const audio_uart0_urfs_grp1[] = {
				"audio_uart0_urfs_grp1", };
static const char * const audio_uart0_urfs_grp2[] = {
				"audio_uart0_urfs_grp2", };
static const char * const audio_uart0_urfs_grp3[] = {
				"audio_uart0_urfs_grp3", };
static const char * const audio_uart1_basic_grp[] = {
				"audio_uart1_basic_grp", };
static const char * const audio_uart1_urfs_grp0[] = {
				"audio_uart1_urfs_grp0", };
static const char * const audio_uart1_urfs_grp1[] = {
				"audio_uart1_urfs_grp1", };
static const char * const audio_uart1_urfs_grp2[] = {
				"audio_uart1_urfs_grp2", };
static const char * const audio_uart2_urfs_grp0[] = {
				"audio_uart2_urfs_grp0", };
static const char * const audio_uart2_urfs_grp1[] = {
				"audio_uart2_urfs_grp1", };
static const char * const audio_uart2_urfs_grp2[] = {
				"audio_uart2_urfs_grp2", };
static const char * const audio_uart2_urxd_grp0[] = {
				"audio_uart2_urxd_grp0", };
static const char * const audio_uart2_urxd_grp1[] = {
				"audio_uart2_urxd_grp1", };
static const char * const audio_uart2_urxd_grp2[] = {
				"audio_uart2_urxd_grp2", };
static const char * const audio_uart2_usclk_grp0[] = {
				"audio_uart2_usclk_grp0", };
static const char * const audio_uart2_usclk_grp1[] = {
				"audio_uart2_usclk_grp1", };
static const char * const audio_uart2_usclk_grp2[] = {
				"audio_uart2_usclk_grp2", };
static const char * const audio_uart2_utfs_grp0[] = {
				"audio_uart2_utfs_grp0", };
static const char * const audio_uart2_utfs_grp1[] = {
				"audio_uart2_utfs_grp1", };
static const char * const audio_uart2_utfs_grp2[] = {
				"audio_uart2_utfs_grp2", };
static const char * const audio_uart2_utxd_grp0[] = {
				"audio_uart2_utxd_grp0", };
static const char * const audio_uart2_utxd_grp1[] = {
				"audio_uart2_utxd_grp1", };
static const char * const audio_uart2_utxd_grp2[] = {
				"audio_uart2_utxd_grp2", };
static const char * const c_can_trnsvr_en_grp0[] = { "c_can_trnsvr_en_grp0", };
static const char * const c_can_trnsvr_en_grp1[] = { "c_can_trnsvr_en_grp1", };
static const char * const c_can_trnsvr_intr_grp[] = {
				"c_can_trnsvr_intr_grp", };
static const char * const c_can_trnsvr_stb_n_grp[] = {
				"c_can_trnsvr_stb_n_grp", };
static const char * const c0_can_rxd_trnsv0_grp[] = {
				"c0_can_rxd_trnsv0_grp", };
static const char * const c0_can_rxd_trnsv1_grp[] = {
				"c0_can_rxd_trnsv1_grp", };
static const char * const c0_can_txd_trnsv0_grp[] = {
				"c0_can_txd_trnsv0_grp", };
static const char * const c0_can_txd_trnsv1_grp[] = {
				"c0_can_txd_trnsv1_grp", };
static const char * const c1_can_rxd_grp0[] = { "c1_can_rxd_grp0", };
static const char * const c1_can_rxd_grp1[] = { "c1_can_rxd_grp1", };
static const char * const c1_can_rxd_grp2[] = { "c1_can_rxd_grp2", };
static const char * const c1_can_rxd_grp3[] = { "c1_can_rxd_grp3", };
static const char * const c1_can_txd_grp0[] = { "c1_can_txd_grp0", };
static const char * const c1_can_txd_grp1[] = { "c1_can_txd_grp1", };
static const char * const c1_can_txd_grp2[] = { "c1_can_txd_grp2", };
static const char * const c1_can_txd_grp3[] = { "c1_can_txd_grp3", };
static const char * const ca_audio_lpc_grp[] = { "ca_audio_lpc_grp", };
static const char * const ca_bt_lpc_grp[] = { "ca_bt_lpc_grp", };
static const char * const ca_coex_grp[] = { "ca_coex_grp", };
static const char * const ca_curator_lpc_grp[] = { "ca_curator_lpc_grp", };
static const char * const ca_pcm_debug_grp[] = { "ca_pcm_debug_grp", };
static const char * const ca_pio_grp[] = { "ca_pio_grp", };
static const char * const ca_sdio_debug_grp[] = { "ca_sdio_debug_grp", };
static const char * const ca_spi_grp[] = { "ca_spi_grp", };
static const char * const ca_trb_grp[] = { "ca_trb_grp", };
static const char * const ca_uart_debug_grp[] = { "ca_uart_debug_grp", };
static const char * const clkc_grp0[] = { "clkc_grp0", };
static const char * const clkc_grp1[] = { "clkc_grp1", };
static const char * const gn_gnss_i2c_grp[] = { "gn_gnss_i2c_grp", };
static const char * const gn_gnss_uart_nopause_grp[] = {
				"gn_gnss_uart_nopause_grp", };
static const char * const gn_gnss_uart_grp[] = { "gn_gnss_uart_grp", };
static const char * const gn_trg_spi_grp0[] = { "gn_trg_spi_grp0", };
static const char * const gn_trg_spi_grp1[] = { "gn_trg_spi_grp1", };
static const char * const cvbs_dbg_grp[] = { "cvbs_dbg_grp", };
static const char * const cvbs_dbg_test_grp0[] = { "cvbs_dbg_test_grp0", };
static const char * const cvbs_dbg_test_grp1[] = { "cvbs_dbg_test_grp1", };
static const char * const cvbs_dbg_test_grp2[] = { "cvbs_dbg_test_grp2", };
static const char * const cvbs_dbg_test_grp3[] = { "cvbs_dbg_test_grp3", };
static const char * const cvbs_dbg_test_grp4[] = { "cvbs_dbg_test_grp4", };
static const char * const cvbs_dbg_test_grp5[] = { "cvbs_dbg_test_grp5", };
static const char * const cvbs_dbg_test_grp6[] = { "cvbs_dbg_test_grp6", };
static const char * const cvbs_dbg_test_grp7[] = { "cvbs_dbg_test_grp7", };
static const char * const cvbs_dbg_test_grp8[] = { "cvbs_dbg_test_grp8", };
static const char * const cvbs_dbg_test_grp9[] = { "cvbs_dbg_test_grp9", };
static const char * const cvbs_dbg_test_grp10[] = { "cvbs_dbg_test_grp10", };
static const char * const cvbs_dbg_test_grp11[] = { "cvbs_dbg_test_grp11", };
static const char * const cvbs_dbg_test_grp12[] = { "cvbs_dbg_test_grp12", };
static const char * const cvbs_dbg_test_grp13[] = { "cvbs_dbg_test_grp13", };
static const char * const cvbs_dbg_test_grp14[] = { "cvbs_dbg_test_grp14", };
static const char * const cvbs_dbg_test_grp15[] = { "cvbs_dbg_test_grp15", };
static const char * const gn_gnss_power_grp[] = { "gn_gnss_power_grp", };
static const char * const gn_gnss_sw_status_grp[] = {
				"gn_gnss_sw_status_grp", };
static const char * const gn_gnss_eclk_grp[] = { "gn_gnss_eclk_grp", };
static const char * const gn_gnss_irq1_grp0[] = { "gn_gnss_irq1_grp0", };
static const char * const gn_gnss_irq2_grp0[] = { "gn_gnss_irq2_grp0", };
static const char * const gn_gnss_tm_grp[] = { "gn_gnss_tm_grp", };
static const char * const gn_gnss_tsync_grp[] = { "gn_gnss_tsync_grp", };
static const char * const gn_io_gnsssys_sw_cfg_grp[] = {
				"gn_io_gnsssys_sw_cfg_grp", };
static const char * const gn_trg_grp0[] = { "gn_trg_grp0", };
static const char * const gn_trg_grp1[] = { "gn_trg_grp1", };
static const char * const gn_trg_shutdown_grp0[] = { "gn_trg_shutdown_grp0", };
static const char * const gn_trg_shutdown_grp1[] = { "gn_trg_shutdown_grp1", };
static const char * const gn_trg_shutdown_grp2[] = { "gn_trg_shutdown_grp2", };
static const char * const gn_trg_shutdown_grp3[] = { "gn_trg_shutdown_grp3", };
static const char * const i2c0_grp[] = { "i2c0_grp", };
static const char * const i2c1_grp[] = { "i2c1_grp", };
static const char * const i2s0_grp[] = { "i2s0_grp", };
static const char * const i2s1_basic_grp[] = { "i2s1_basic_grp", };
static const char * const i2s1_rxd0_grp0[] = { "i2s1_rxd0_grp0", };
static const char * const i2s1_rxd0_grp1[] = { "i2s1_rxd0_grp1", };
static const char * const i2s1_rxd0_grp2[] = { "i2s1_rxd0_grp2", };
static const char * const i2s1_rxd0_grp3[] = { "i2s1_rxd0_grp3", };
static const char * const i2s1_rxd0_grp4[] = { "i2s1_rxd0_grp4", };
static const char * const i2s1_rxd1_grp0[] = { "i2s1_rxd1_grp0", };
static const char * const i2s1_rxd1_grp1[] = { "i2s1_rxd1_grp1", };
static const char * const i2s1_rxd1_grp2[] = { "i2s1_rxd1_grp2", };
static const char * const i2s1_rxd1_grp3[] = { "i2s1_rxd1_grp3", };
static const char * const i2s1_rxd1_grp4[] = { "i2s1_rxd1_grp4", };
static const char * const jtag_jt_dbg_nsrst_grp[] = {
				"jtag_jt_dbg_nsrst_grp", };
static const char * const jtag_ntrst_grp0[] = { "jtag_ntrst_grp0", };
static const char * const jtag_ntrst_grp1[] = { "jtag_ntrst_grp1", };
static const char * const jtag_swdiotms_grp0[] = { "jtag_swdiotms_grp0", };
static const char * const jtag_swdiotms_grp1[] = { "jtag_swdiotms_grp1", };
static const char * const jtag_tck_grp0[] = { "jtag_tck_grp0", };
static const char * const jtag_tck_grp1[] = { "jtag_tck_grp1", };
static const char * const jtag_tdi_grp0[] = { "jtag_tdi_grp0", };
static const char * const jtag_tdi_grp1[] = { "jtag_tdi_grp1", };
static const char * const jtag_tdo_grp0[] = { "jtag_tdo_grp0", };
static const char * const jtag_tdo_grp1[] = { "jtag_tdo_grp1", };
static const char * const ks_kas_spi_grp0[] = { "ks_kas_spi_grp0", };
static const char * const ld_ldd_grp[] = { "ld_ldd_grp", };
static const char * const ld_ldd_16bit_grp[] = { "ld_ldd_16bit_grp", };
static const char * const ld_ldd_fck_grp[] = { "ld_ldd_fck_grp", };
static const char * const ld_ldd_lck_grp[] = { "ld_ldd_lck_grp", };
static const char * const lr_lcdrom_grp[] = { "lr_lcdrom_grp", };
static const char * const lvds_analog_grp[] = { "lvds_analog_grp", };
static const char * const nd_df_basic_grp[] = { "nd_df_basic_grp", };
static const char * const nd_df_wp_grp[] = { "nd_df_wp_grp", };
static const char * const nd_df_cs_grp[] = { "nd_df_cs_grp", };
static const char * const ps_grp[] = { "ps_grp", };
static const char * const ps_no_dir_grp[] = { "ps_no_dir_grp", };
static const char * const pwc_core_on_grp[] = { "pwc_core_on_grp", };
static const char * const pwc_ext_on_grp[] = { "pwc_ext_on_grp", };
static const char * const pwc_gpio3_clk_grp[] = { "pwc_gpio3_clk_grp", };
static const char * const pwc_io_on_grp[] = { "pwc_io_on_grp", };
static const char * const pwc_lowbatt_b_grp0[] = { "pwc_lowbatt_b_grp0", };
static const char * const pwc_mem_on_grp[] = { "pwc_mem_on_grp", };
static const char * const pwc_on_key_b_grp0[] = { "pwc_on_key_b_grp0", };
static const char * const pwc_wakeup_src0_grp[] = { "pwc_wakeup_src0_grp", };
static const char * const pwc_wakeup_src1_grp[] = { "pwc_wakeup_src1_grp", };
static const char * const pwc_wakeup_src2_grp[] = { "pwc_wakeup_src2_grp", };
static const char * const pwc_wakeup_src3_grp[] = { "pwc_wakeup_src3_grp", };
static const char * const pw_cko0_grp0[] = { "pw_cko0_grp0", };
static const char * const pw_cko0_grp1[] = { "pw_cko0_grp1", };
static const char * const pw_cko0_grp2[] = { "pw_cko0_grp2", };
static const char * const pw_cko0_grp3[] = { "pw_cko0_grp3", };
static const char * const pw_cko1_grp0[] = { "pw_cko1_grp0", };
static const char * const pw_cko1_grp1[] = { "pw_cko1_grp1", };
static const char * const pw_cko1_grp2[] = { "pw_cko1_grp2", };
static const char * const pw_i2s01_clk_grp0[] = { "pw_i2s01_clk_grp0", };
static const char * const pw_i2s01_clk_grp1[] = { "pw_i2s01_clk_grp1", };
static const char * const pw_i2s01_clk_grp2[] = { "pw_i2s01_clk_grp2", };
static const char * const pw_pwm0_grp0[] = { "pw_pwm0_grp0", };
static const char * const pw_pwm0_grp1[] = { "pw_pwm0_grp1", };
static const char * const pw_pwm1_grp0[] = { "pw_pwm1_grp0", };
static const char * const pw_pwm1_grp1[] = { "pw_pwm1_grp1", };
static const char * const pw_pwm1_grp2[] = { "pw_pwm1_grp2", };
static const char * const pw_pwm2_grp0[] = { "pw_pwm2_grp0", };
static const char * const pw_pwm2_grp1[] = { "pw_pwm2_grp1", };
static const char * const pw_pwm2_grp2[] = { "pw_pwm2_grp2", };
static const char * const pw_pwm3_grp0[] = { "pw_pwm3_grp0", };
static const char * const pw_pwm3_grp1[] = { "pw_pwm3_grp1", };
static const char * const pw_pwm_cpu_vol_grp0[] = { "pw_pwm_cpu_vol_grp0", };
static const char * const pw_pwm_cpu_vol_grp1[] = { "pw_pwm_cpu_vol_grp1", };
static const char * const pw_pwm_cpu_vol_grp2[] = { "pw_pwm_cpu_vol_grp2", };
static const char * const pw_backlight_grp0[] = { "pw_backlight_grp0", };
static const char * const pw_backlight_grp1[] = { "pw_backlight_grp1", };
static const char * const rg_eth_mac_grp[] = { "rg_eth_mac_grp", };
static const char * const rg_gmac_phy_intr_n_grp[] = {
				"rg_gmac_phy_intr_n_grp", };
static const char * const rg_rgmii_mac_grp[] = { "rg_rgmii_mac_grp", };
static const char * const rg_rgmii_phy_ref_clk_grp0[] = {
				"rg_rgmii_phy_ref_clk_grp0", };
static const char * const rg_rgmii_phy_ref_clk_grp1[] = {
				"rg_rgmii_phy_ref_clk_grp1", };
static const char * const sd0_grp[] = { "sd0_grp", };
static const char * const sd0_4bit_grp[] = { "sd0_4bit_grp", };
static const char * const sd1_grp[] = { "sd1_grp", };
static const char * const sd1_4bit_grp0[] = { "sd1_4bit_grp0", };
static const char * const sd1_4bit_grp1[] = { "sd1_4bit_grp1", };
static const char * const sd2_basic_grp[] = { "sd2_basic_grp", };
static const char * const sd2_cdb_grp0[] = { "sd2_cdb_grp0", };
static const char * const sd2_cdb_grp1[] = { "sd2_cdb_grp1", };
static const char * const sd2_wpb_grp0[] = { "sd2_wpb_grp0", };
static const char * const sd2_wpb_grp1[] = { "sd2_wpb_grp1", };
static const char * const sd3_9_grp[] = { "sd3_9_grp", };
static const char * const sd5_grp[] = { "sd5_grp", };
static const char * const sd6_grp0[] = { "sd6_grp0", };
static const char * const sd6_grp1[] = { "sd6_grp1", };
static const char * const sp0_ext_ldo_on_grp[] = { "sp0_ext_ldo_on_grp", };
static const char * const sp0_qspi_grp[] = { "sp0_qspi_grp", };
static const char * const sp1_spi_grp[] = { "sp1_spi_grp", };
static const char * const tpiu_trace_grp[] = { "tpiu_trace_grp", };
static const char * const uart0_grp[] = { "uart0_grp", };
static const char * const uart0_nopause_grp[] = { "uart0_nopause_grp", };
static const char * const uart1_grp[] = { "uart1_grp", };
static const char * const uart2_cts_grp0[] = { "uart2_cts_grp0", };
static const char * const uart2_cts_grp1[] = { "uart2_cts_grp1", };
static const char * const uart2_rts_grp0[] = { "uart2_rts_grp0", };
static const char * const uart2_rts_grp1[] = { "uart2_rts_grp1", };
static const char * const uart2_rxd_grp0[] = { "uart2_rxd_grp0", };
static const char * const uart2_rxd_grp1[] = { "uart2_rxd_grp1", };
static const char * const uart2_rxd_grp2[] = { "uart2_rxd_grp2", };
static const char * const uart2_txd_grp0[] = { "uart2_txd_grp0", };
static const char * const uart2_txd_grp1[] = { "uart2_txd_grp1", };
static const char * const uart2_txd_grp2[] = { "uart2_txd_grp2", };
static const char * const uart3_cts_grp0[] = { "uart3_cts_grp0", };
static const char * const uart3_cts_grp1[] = { "uart3_cts_grp1", };
static const char * const uart3_cts_grp2[] = { "uart3_cts_grp2", };
static const char * const uart3_rts_grp0[] = { "uart3_rts_grp0", };
static const char * const uart3_rts_grp1[] = { "uart3_rts_grp1", };
static const char * const uart3_rts_grp2[] = { "uart3_rts_grp2", };
static const char * const uart3_rxd_grp0[] = { "uart3_rxd_grp0", };
static const char * const uart3_rxd_grp1[] = { "uart3_rxd_grp1", };
static const char * const uart3_rxd_grp2[] = { "uart3_rxd_grp2", };
static const char * const uart3_txd_grp0[] = { "uart3_txd_grp0", };
static const char * const uart3_txd_grp1[] = { "uart3_txd_grp1", };
static const char * const uart3_txd_grp2[] = { "uart3_txd_grp2", };
static const char * const uart4_basic_grp[] = { "uart4_basic_grp", };
static const char * const uart4_cts_grp0[] = { "uart4_cts_grp0", };
static const char * const uart4_cts_grp1[] = { "uart4_cts_grp1", };
static const char * const uart4_cts_grp2[] = { "uart4_cts_grp2", };
static const char * const uart4_rts_grp0[] = { "uart4_rts_grp0", };
static const char * const uart4_rts_grp1[] = { "uart4_rts_grp1", };
static const char * const uart4_rts_grp2[] = { "uart4_rts_grp2", };
static const char * const usb0_drvvbus_grp0[] = { "usb0_drvvbus_grp0", };
static const char * const usb0_drvvbus_grp1[] = { "usb0_drvvbus_grp1", };
static const char * const usb1_drvvbus_grp0[] = { "usb1_drvvbus_grp0", };
static const char * const usb1_drvvbus_grp1[] = { "usb1_drvvbus_grp1", };
static const char * const visbus_dout_grp[] = { "visbus_dout_grp", };
static const char * const vi_vip1_grp[] = { "vi_vip1_grp", };
static const char * const vi_vip1_ext_grp[] = { "vi_vip1_ext_grp", };
static const char * const vi_vip1_low8bit_grp[] = { "vi_vip1_low8bit_grp", };
static const char * const vi_vip1_high8bit_grp[] = { "vi_vip1_high8bit_grp", };

static struct atlas7_pad_mux gnss_gpio_grp_pad_mux[] = {
	MUX(1, 119, 0, N, N, N, N),
	MUX(1, 120, 0, N, N, N, N),
	MUX(1, 121, 0, N, N, N, N),
	MUX(1, 122, 0, N, N, N, N),
	MUX(1, 123, 0, N, N, N, N),
	MUX(1, 124, 0, N, N, N, N),
	MUX(1, 125, 0, N, N, N, N),
	MUX(1, 126, 0, N, N, N, N),
	MUX(1, 127, 0, N, N, N, N),
	MUX(1, 128, 0, N, N, N, N),
	MUX(1, 22, 0, N, N, N, N),
	MUX(1, 23, 0, N, N, N, N),
	MUX(1, 24, 0, N, N, N, N),
	MUX(1, 25, 0, N, N, N, N),
	MUX(1, 26, 0, N, N, N, N),
	MUX(1, 27, 0, N, N, N, N),
	MUX(1, 28, 0, N, N, N, N),
	MUX(1, 29, 0, N, N, N, N),
	MUX(1, 30, 0, N, N, N, N),
};

static struct atlas7_grp_mux gnss_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gnss_gpio_grp_pad_mux),
	.pad_mux_list = gnss_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux lcd_vip_gpio_grp_pad_mux[] = {
	MUX(1, 74, 0, N, N, N, N),
	MUX(1, 75, 0, N, N, N, N),
	MUX(1, 76, 0, N, N, N, N),
	MUX(1, 77, 0, N, N, N, N),
	MUX(1, 78, 0, N, N, N, N),
	MUX(1, 79, 0, N, N, N, N),
	MUX(1, 80, 0, N, N, N, N),
	MUX(1, 81, 0, N, N, N, N),
	MUX(1, 82, 0, N, N, N, N),
	MUX(1, 83, 0, N, N, N, N),
	MUX(1, 84, 0, N, N, N, N),
	MUX(1, 53, 0, N, N, N, N),
	MUX(1, 54, 0, N, N, N, N),
	MUX(1, 55, 0, N, N, N, N),
	MUX(1, 56, 0, N, N, N, N),
	MUX(1, 57, 0, N, N, N, N),
	MUX(1, 58, 0, N, N, N, N),
	MUX(1, 59, 0, N, N, N, N),
	MUX(1, 60, 0, N, N, N, N),
	MUX(1, 61, 0, N, N, N, N),
	MUX(1, 62, 0, N, N, N, N),
	MUX(1, 63, 0, N, N, N, N),
	MUX(1, 64, 0, N, N, N, N),
	MUX(1, 65, 0, N, N, N, N),
	MUX(1, 66, 0, N, N, N, N),
	MUX(1, 67, 0, N, N, N, N),
	MUX(1, 68, 0, N, N, N, N),
	MUX(1, 69, 0, N, N, N, N),
	MUX(1, 70, 0, N, N, N, N),
	MUX(1, 71, 0, N, N, N, N),
	MUX(1, 72, 0, N, N, N, N),
	MUX(1, 73, 0, N, N, N, N),
};

static struct atlas7_grp_mux lcd_vip_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(lcd_vip_gpio_grp_pad_mux),
	.pad_mux_list = lcd_vip_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux sdio_i2s_gpio_grp_pad_mux[] = {
	MUX(1, 31, 0, N, N, N, N),
	MUX(1, 32, 0, N, N, N, N),
	MUX(1, 33, 0, N, N, N, N),
	MUX(1, 34, 0, N, N, N, N),
	MUX(1, 35, 0, N, N, N, N),
	MUX(1, 36, 0, N, N, N, N),
	MUX(1, 85, 0, N, N, N, N),
	MUX(1, 86, 0, N, N, N, N),
	MUX(1, 87, 0, N, N, N, N),
	MUX(1, 88, 0, N, N, N, N),
	MUX(1, 89, 0, N, N, N, N),
	MUX(1, 90, 0, N, N, N, N),
	MUX(1, 129, 0, N, N, N, N),
	MUX(1, 130, 0, N, N, N, N),
	MUX(1, 131, 0, N, N, N, N),
	MUX(1, 132, 0, N, N, N, N),
	MUX(1, 91, 0, N, N, N, N),
	MUX(1, 92, 0, N, N, N, N),
	MUX(1, 93, 0, N, N, N, N),
	MUX(1, 94, 0, N, N, N, N),
	MUX(1, 95, 0, N, N, N, N),
	MUX(1, 96, 0, N, N, N, N),
	MUX(1, 112, 0, N, N, N, N),
	MUX(1, 113, 0, N, N, N, N),
	MUX(1, 114, 0, N, N, N, N),
	MUX(1, 115, 0, N, N, N, N),
	MUX(1, 116, 0, N, N, N, N),
	MUX(1, 117, 0, N, N, N, N),
	MUX(1, 118, 0, N, N, N, N),
};

static struct atlas7_grp_mux sdio_i2s_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sdio_i2s_gpio_grp_pad_mux),
	.pad_mux_list = sdio_i2s_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux sp_rgmii_gpio_grp_pad_mux[] = {
	MUX(1, 97, 0, N, N, N, N),
	MUX(1, 98, 0, N, N, N, N),
	MUX(1, 99, 0, N, N, N, N),
	MUX(1, 100, 0, N, N, N, N),
	MUX(1, 101, 0, N, N, N, N),
	MUX(1, 102, 0, N, N, N, N),
	MUX(1, 103, 0, N, N, N, N),
	MUX(1, 104, 0, N, N, N, N),
	MUX(1, 105, 0, N, N, N, N),
	MUX(1, 106, 0, N, N, N, N),
	MUX(1, 107, 0, N, N, N, N),
	MUX(1, 108, 0, N, N, N, N),
	MUX(1, 109, 0, N, N, N, N),
	MUX(1, 110, 0, N, N, N, N),
	MUX(1, 111, 0, N, N, N, N),
	MUX(1, 18, 0, N, N, N, N),
	MUX(1, 19, 0, N, N, N, N),
	MUX(1, 20, 0, N, N, N, N),
	MUX(1, 21, 0, N, N, N, N),
	MUX(1, 141, 0, N, N, N, N),
	MUX(1, 142, 0, N, N, N, N),
	MUX(1, 143, 0, N, N, N, N),
	MUX(1, 144, 0, N, N, N, N),
	MUX(1, 145, 0, N, N, N, N),
	MUX(1, 146, 0, N, N, N, N),
	MUX(1, 147, 0, N, N, N, N),
	MUX(1, 148, 0, N, N, N, N),
};

static struct atlas7_grp_mux sp_rgmii_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sp_rgmii_gpio_grp_pad_mux),
	.pad_mux_list = sp_rgmii_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux lvds_gpio_grp_pad_mux[] = {
	MUX(1, 157, 0, N, N, N, N),
	MUX(1, 158, 0, N, N, N, N),
	MUX(1, 155, 0, N, N, N, N),
	MUX(1, 156, 0, N, N, N, N),
	MUX(1, 153, 0, N, N, N, N),
	MUX(1, 154, 0, N, N, N, N),
	MUX(1, 151, 0, N, N, N, N),
	MUX(1, 152, 0, N, N, N, N),
	MUX(1, 149, 0, N, N, N, N),
	MUX(1, 150, 0, N, N, N, N),
};

static struct atlas7_grp_mux lvds_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(lvds_gpio_grp_pad_mux),
	.pad_mux_list = lvds_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux jtag_uart_nand_gpio_grp_pad_mux[] = {
	MUX(1, 44, 0, N, N, N, N),
	MUX(1, 43, 0, N, N, N, N),
	MUX(1, 42, 0, N, N, N, N),
	MUX(1, 41, 0, N, N, N, N),
	MUX(1, 40, 0, N, N, N, N),
	MUX(1, 39, 0, N, N, N, N),
	MUX(1, 38, 0, N, N, N, N),
	MUX(1, 37, 0, N, N, N, N),
	MUX(1, 46, 0, N, N, N, N),
	MUX(1, 47, 0, N, N, N, N),
	MUX(1, 48, 0, N, N, N, N),
	MUX(1, 49, 0, N, N, N, N),
	MUX(1, 50, 0, N, N, N, N),
	MUX(1, 52, 0, N, N, N, N),
	MUX(1, 51, 0, N, N, N, N),
	MUX(1, 45, 0, N, N, N, N),
	MUX(1, 133, 0, N, N, N, N),
	MUX(1, 134, 0, N, N, N, N),
	MUX(1, 135, 0, N, N, N, N),
	MUX(1, 136, 0, N, N, N, N),
	MUX(1, 137, 0, N, N, N, N),
	MUX(1, 138, 0, N, N, N, N),
	MUX(1, 139, 0, N, N, N, N),
	MUX(1, 140, 0, N, N, N, N),
	MUX(1, 159, 0, N, N, N, N),
	MUX(1, 160, 0, N, N, N, N),
	MUX(1, 161, 0, N, N, N, N),
	MUX(1, 162, 0, N, N, N, N),
	MUX(1, 163, 0, N, N, N, N),
};

static struct atlas7_grp_mux jtag_uart_nand_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_uart_nand_gpio_grp_pad_mux),
	.pad_mux_list = jtag_uart_nand_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux rtc_gpio_grp_pad_mux[] = {
	MUX(0, 0, 0, N, N, N, N),
	MUX(0, 1, 0, N, N, N, N),
	MUX(0, 2, 0, N, N, N, N),
	MUX(0, 3, 0, N, N, N, N),
	MUX(0, 4, 0, N, N, N, N),
	MUX(0, 10, 0, N, N, N, N),
	MUX(0, 11, 0, N, N, N, N),
	MUX(0, 12, 0, N, N, N, N),
	MUX(0, 13, 0, N, N, N, N),
	MUX(0, 14, 0, N, N, N, N),
	MUX(0, 15, 0, N, N, N, N),
	MUX(0, 16, 0, N, N, N, N),
	MUX(0, 17, 0, N, N, N, N),
	MUX(0, 9, 0, N, N, N, N),
};

static struct atlas7_grp_mux rtc_gpio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(rtc_gpio_grp_pad_mux),
	.pad_mux_list = rtc_gpio_grp_pad_mux,
};

static struct atlas7_pad_mux audio_ac97_grp_pad_mux[] = {
	MUX(1, 113, 2, N, N, N, N),
	MUX(1, 118, 2, N, N, N, N),
	MUX(1, 115, 2, N, N, N, N),
	MUX(1, 114, 2, N, N, N, N),
};

static struct atlas7_grp_mux audio_ac97_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_ac97_grp_pad_mux),
	.pad_mux_list = audio_ac97_grp_pad_mux,
};

static struct atlas7_pad_mux audio_digmic_grp0_pad_mux[] = {
	MUX(1, 51, 3, 0xa10, 20, 0xa90, 20),
};

static struct atlas7_grp_mux audio_digmic_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_digmic_grp0_pad_mux),
	.pad_mux_list = audio_digmic_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_digmic_grp1_pad_mux[] = {
	MUX(1, 122, 5, 0xa10, 20, 0xa90, 20),
};

static struct atlas7_grp_mux audio_digmic_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_digmic_grp1_pad_mux),
	.pad_mux_list = audio_digmic_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_digmic_grp2_pad_mux[] = {
	MUX(1, 161, 7, 0xa10, 20, 0xa90, 20),
};

static struct atlas7_grp_mux audio_digmic_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_digmic_grp2_pad_mux),
	.pad_mux_list = audio_digmic_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_func_dbg_grp_pad_mux[] = {
	MUX(1, 141, 4, N, N, N, N),
	MUX(1, 144, 4, N, N, N, N),
	MUX(1, 44, 6, N, N, N, N),
	MUX(1, 43, 6, N, N, N, N),
	MUX(1, 42, 6, N, N, N, N),
	MUX(1, 41, 6, N, N, N, N),
	MUX(1, 40, 6, N, N, N, N),
	MUX(1, 39, 6, N, N, N, N),
	MUX(1, 38, 6, N, N, N, N),
	MUX(1, 37, 6, N, N, N, N),
	MUX(1, 74, 6, N, N, N, N),
	MUX(1, 75, 6, N, N, N, N),
	MUX(1, 76, 6, N, N, N, N),
	MUX(1, 77, 6, N, N, N, N),
	MUX(1, 78, 6, N, N, N, N),
	MUX(1, 79, 6, N, N, N, N),
	MUX(1, 81, 6, N, N, N, N),
	MUX(1, 113, 6, N, N, N, N),
	MUX(1, 114, 6, N, N, N, N),
	MUX(1, 118, 6, N, N, N, N),
	MUX(1, 115, 6, N, N, N, N),
	MUX(1, 49, 6, N, N, N, N),
	MUX(1, 50, 6, N, N, N, N),
	MUX(1, 142, 4, N, N, N, N),
	MUX(1, 143, 4, N, N, N, N),
	MUX(1, 80, 6, N, N, N, N),
};

static struct atlas7_grp_mux audio_func_dbg_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_func_dbg_grp_pad_mux),
	.pad_mux_list = audio_func_dbg_grp_pad_mux,
};

static struct atlas7_pad_mux audio_i2s_grp_pad_mux[] = {
	MUX(1, 118, 1, N, N, N, N),
	MUX(1, 115, 1, N, N, N, N),
	MUX(1, 116, 1, N, N, N, N),
	MUX(1, 117, 1, N, N, N, N),
	MUX(1, 112, 1, N, N, N, N),
	MUX(1, 113, 1, N, N, N, N),
	MUX(1, 114, 1, N, N, N, N),
};

static struct atlas7_grp_mux audio_i2s_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_i2s_grp_pad_mux),
	.pad_mux_list = audio_i2s_grp_pad_mux,
};

static struct atlas7_pad_mux audio_i2s_2ch_grp_pad_mux[] = {
	MUX(1, 118, 1, N, N, N, N),
	MUX(1, 115, 1, N, N, N, N),
	MUX(1, 112, 1, N, N, N, N),
	MUX(1, 113, 1, N, N, N, N),
	MUX(1, 114, 1, N, N, N, N),
};

static struct atlas7_grp_mux audio_i2s_2ch_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_i2s_2ch_grp_pad_mux),
	.pad_mux_list = audio_i2s_2ch_grp_pad_mux,
};

static struct atlas7_pad_mux audio_i2s_extclk_grp_pad_mux[] = {
	MUX(1, 112, 2, N, N, N, N),
};

static struct atlas7_grp_mux audio_i2s_extclk_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_i2s_extclk_grp_pad_mux),
	.pad_mux_list = audio_i2s_extclk_grp_pad_mux,
};

static struct atlas7_pad_mux audio_spdif_out_grp0_pad_mux[] = {
	MUX(1, 112, 3, N, N, N, N),
};

static struct atlas7_grp_mux audio_spdif_out_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_spdif_out_grp0_pad_mux),
	.pad_mux_list = audio_spdif_out_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_spdif_out_grp1_pad_mux[] = {
	MUX(1, 116, 3, N, N, N, N),
};

static struct atlas7_grp_mux audio_spdif_out_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_spdif_out_grp1_pad_mux),
	.pad_mux_list = audio_spdif_out_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_spdif_out_grp2_pad_mux[] = {
	MUX(1, 142, 3, N, N, N, N),
};

static struct atlas7_grp_mux audio_spdif_out_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_spdif_out_grp2_pad_mux),
	.pad_mux_list = audio_spdif_out_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart0_basic_grp_pad_mux[] = {
	MUX(1, 143, 1, N, N, N, N),
	MUX(1, 142, 1, N, N, N, N),
	MUX(1, 141, 1, N, N, N, N),
	MUX(1, 144, 1, N, N, N, N),
};

static struct atlas7_grp_mux audio_uart0_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart0_basic_grp_pad_mux),
	.pad_mux_list = audio_uart0_basic_grp_pad_mux,
};

static struct atlas7_pad_mux audio_uart0_urfs_grp0_pad_mux[] = {
	MUX(1, 117, 5, 0xa10, 28, 0xa90, 28),
};

static struct atlas7_grp_mux audio_uart0_urfs_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart0_urfs_grp0_pad_mux),
	.pad_mux_list = audio_uart0_urfs_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart0_urfs_grp1_pad_mux[] = {
	MUX(1, 139, 3, 0xa10, 28, 0xa90, 28),
};

static struct atlas7_grp_mux audio_uart0_urfs_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart0_urfs_grp1_pad_mux),
	.pad_mux_list = audio_uart0_urfs_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart0_urfs_grp2_pad_mux[] = {
	MUX(1, 163, 3, 0xa10, 28, 0xa90, 28),
};

static struct atlas7_grp_mux audio_uart0_urfs_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart0_urfs_grp2_pad_mux),
	.pad_mux_list = audio_uart0_urfs_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart0_urfs_grp3_pad_mux[] = {
	MUX(1, 162, 6, 0xa10, 28, 0xa90, 28),
};

static struct atlas7_grp_mux audio_uart0_urfs_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart0_urfs_grp3_pad_mux),
	.pad_mux_list = audio_uart0_urfs_grp3_pad_mux,
};

static struct atlas7_pad_mux audio_uart1_basic_grp_pad_mux[] = {
	MUX(1, 147, 1, 0xa10, 24, 0xa90, 24),
	MUX(1, 146, 1, 0xa10, 25, 0xa90, 25),
	MUX(1, 145, 1, 0xa10, 23, 0xa90, 23),
	MUX(1, 148, 1, 0xa10, 22, 0xa90, 22),
};

static struct atlas7_grp_mux audio_uart1_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart1_basic_grp_pad_mux),
	.pad_mux_list = audio_uart1_basic_grp_pad_mux,
};

static struct atlas7_pad_mux audio_uart1_urfs_grp0_pad_mux[] = {
	MUX(1, 117, 6, 0xa10, 29, 0xa90, 29),
};

static struct atlas7_grp_mux audio_uart1_urfs_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart1_urfs_grp0_pad_mux),
	.pad_mux_list = audio_uart1_urfs_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart1_urfs_grp1_pad_mux[] = {
	MUX(1, 140, 3, 0xa10, 29, 0xa90, 29),
};

static struct atlas7_grp_mux audio_uart1_urfs_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart1_urfs_grp1_pad_mux),
	.pad_mux_list = audio_uart1_urfs_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart1_urfs_grp2_pad_mux[] = {
	MUX(1, 163, 4, 0xa10, 29, 0xa90, 29),
};

static struct atlas7_grp_mux audio_uart1_urfs_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart1_urfs_grp2_pad_mux),
	.pad_mux_list = audio_uart1_urfs_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urfs_grp0_pad_mux[] = {
	MUX(1, 139, 4, 0xa10, 30, 0xa90, 30),
};

static struct atlas7_grp_mux audio_uart2_urfs_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urfs_grp0_pad_mux),
	.pad_mux_list = audio_uart2_urfs_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urfs_grp1_pad_mux[] = {
	MUX(1, 163, 6, 0xa10, 30, 0xa90, 30),
};

static struct atlas7_grp_mux audio_uart2_urfs_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urfs_grp1_pad_mux),
	.pad_mux_list = audio_uart2_urfs_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urfs_grp2_pad_mux[] = {
	MUX(1, 96, 3, 0xa10, 30, 0xa90, 30),
};

static struct atlas7_grp_mux audio_uart2_urfs_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urfs_grp2_pad_mux),
	.pad_mux_list = audio_uart2_urfs_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urxd_grp0_pad_mux[] = {
	MUX(1, 20, 2, 0xa00, 24, 0xa80, 24),
};

static struct atlas7_grp_mux audio_uart2_urxd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urxd_grp0_pad_mux),
	.pad_mux_list = audio_uart2_urxd_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urxd_grp1_pad_mux[] = {
	MUX(1, 109, 2, 0xa00, 24, 0xa80, 24),
};

static struct atlas7_grp_mux audio_uart2_urxd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urxd_grp1_pad_mux),
	.pad_mux_list = audio_uart2_urxd_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_urxd_grp2_pad_mux[] = {
	MUX(1, 93, 3, 0xa00, 24, 0xa80, 24),
};

static struct atlas7_grp_mux audio_uart2_urxd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_urxd_grp2_pad_mux),
	.pad_mux_list = audio_uart2_urxd_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_usclk_grp0_pad_mux[] = {
	MUX(1, 19, 2, 0xa00, 23, 0xa80, 23),
};

static struct atlas7_grp_mux audio_uart2_usclk_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_usclk_grp0_pad_mux),
	.pad_mux_list = audio_uart2_usclk_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_usclk_grp1_pad_mux[] = {
	MUX(1, 101, 2, 0xa00, 23, 0xa80, 23),
};

static struct atlas7_grp_mux audio_uart2_usclk_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_usclk_grp1_pad_mux),
	.pad_mux_list = audio_uart2_usclk_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_usclk_grp2_pad_mux[] = {
	MUX(1, 91, 3, 0xa00, 23, 0xa80, 23),
};

static struct atlas7_grp_mux audio_uart2_usclk_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_usclk_grp2_pad_mux),
	.pad_mux_list = audio_uart2_usclk_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utfs_grp0_pad_mux[] = {
	MUX(1, 18, 2, 0xa00, 22, 0xa80, 22),
};

static struct atlas7_grp_mux audio_uart2_utfs_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utfs_grp0_pad_mux),
	.pad_mux_list = audio_uart2_utfs_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utfs_grp1_pad_mux[] = {
	MUX(1, 111, 2, 0xa00, 22, 0xa80, 22),
};

static struct atlas7_grp_mux audio_uart2_utfs_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utfs_grp1_pad_mux),
	.pad_mux_list = audio_uart2_utfs_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utfs_grp2_pad_mux[] = {
	MUX(1, 94, 3, 0xa00, 22, 0xa80, 22),
};

static struct atlas7_grp_mux audio_uart2_utfs_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utfs_grp2_pad_mux),
	.pad_mux_list = audio_uart2_utfs_grp2_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utxd_grp0_pad_mux[] = {
	MUX(1, 21, 2, 0xa00, 25, 0xa80, 25),
};

static struct atlas7_grp_mux audio_uart2_utxd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utxd_grp0_pad_mux),
	.pad_mux_list = audio_uart2_utxd_grp0_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utxd_grp1_pad_mux[] = {
	MUX(1, 110, 2, 0xa00, 25, 0xa80, 25),
};

static struct atlas7_grp_mux audio_uart2_utxd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utxd_grp1_pad_mux),
	.pad_mux_list = audio_uart2_utxd_grp1_pad_mux,
};

static struct atlas7_pad_mux audio_uart2_utxd_grp2_pad_mux[] = {
	MUX(1, 92, 3, 0xa00, 25, 0xa80, 25),
};

static struct atlas7_grp_mux audio_uart2_utxd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(audio_uart2_utxd_grp2_pad_mux),
	.pad_mux_list = audio_uart2_utxd_grp2_pad_mux,
};

static struct atlas7_pad_mux c_can_trnsvr_en_grp0_pad_mux[] = {
	MUX(0, 2, 6, N, N, N, N),
};

static struct atlas7_grp_mux c_can_trnsvr_en_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(c_can_trnsvr_en_grp0_pad_mux),
	.pad_mux_list = c_can_trnsvr_en_grp0_pad_mux,
};

static struct atlas7_pad_mux c_can_trnsvr_en_grp1_pad_mux[] = {
	MUX(0, 0, 2, N, N, N, N),
};

static struct atlas7_grp_mux c_can_trnsvr_en_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(c_can_trnsvr_en_grp1_pad_mux),
	.pad_mux_list = c_can_trnsvr_en_grp1_pad_mux,
};

static struct atlas7_pad_mux c_can_trnsvr_intr_grp_pad_mux[] = {
	MUX(0, 1, 2, N, N, N, N),
};

static struct atlas7_grp_mux c_can_trnsvr_intr_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c_can_trnsvr_intr_grp_pad_mux),
	.pad_mux_list = c_can_trnsvr_intr_grp_pad_mux,
};

static struct atlas7_pad_mux c_can_trnsvr_stb_n_grp_pad_mux[] = {
	MUX(0, 3, 6, N, N, N, N),
};

static struct atlas7_grp_mux c_can_trnsvr_stb_n_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c_can_trnsvr_stb_n_grp_pad_mux),
	.pad_mux_list = c_can_trnsvr_stb_n_grp_pad_mux,
};

static struct atlas7_pad_mux c0_can_rxd_trnsv0_grp_pad_mux[] = {
	MUX(0, 11, 1, 0xa08, 9, 0xa88, 9),
};

static struct atlas7_grp_mux c0_can_rxd_trnsv0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c0_can_rxd_trnsv0_grp_pad_mux),
	.pad_mux_list = c0_can_rxd_trnsv0_grp_pad_mux,
};

static struct atlas7_pad_mux c0_can_rxd_trnsv1_grp_pad_mux[] = {
	MUX(0, 2, 5, 0xa10, 9, 0xa90, 9),
};

static struct atlas7_grp_mux c0_can_rxd_trnsv1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c0_can_rxd_trnsv1_grp_pad_mux),
	.pad_mux_list = c0_can_rxd_trnsv1_grp_pad_mux,
};

static struct atlas7_pad_mux c0_can_txd_trnsv0_grp_pad_mux[] = {
	MUX(0, 10, 1, N, N, N, N),
};

static struct atlas7_grp_mux c0_can_txd_trnsv0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c0_can_txd_trnsv0_grp_pad_mux),
	.pad_mux_list = c0_can_txd_trnsv0_grp_pad_mux,
};

static struct atlas7_pad_mux c0_can_txd_trnsv1_grp_pad_mux[] = {
	MUX(0, 3, 5, N, N, N, N),
};

static struct atlas7_grp_mux c0_can_txd_trnsv1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(c0_can_txd_trnsv1_grp_pad_mux),
	.pad_mux_list = c0_can_txd_trnsv1_grp_pad_mux,
};

static struct atlas7_pad_mux c1_can_rxd_grp0_pad_mux[] = {
	MUX(1, 138, 2, 0xa00, 4, 0xa80, 4),
};

static struct atlas7_grp_mux c1_can_rxd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_rxd_grp0_pad_mux),
	.pad_mux_list = c1_can_rxd_grp0_pad_mux,
};

static struct atlas7_pad_mux c1_can_rxd_grp1_pad_mux[] = {
	MUX(1, 147, 2, 0xa00, 4, 0xa80, 4),
};

static struct atlas7_grp_mux c1_can_rxd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_rxd_grp1_pad_mux),
	.pad_mux_list = c1_can_rxd_grp1_pad_mux,
};

static struct atlas7_pad_mux c1_can_rxd_grp2_pad_mux[] = {
	MUX(0, 2, 2, 0xa00, 4, 0xa80, 4),
};

static struct atlas7_grp_mux c1_can_rxd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_rxd_grp2_pad_mux),
	.pad_mux_list = c1_can_rxd_grp2_pad_mux,
};

static struct atlas7_pad_mux c1_can_rxd_grp3_pad_mux[] = {
	MUX(1, 162, 4, 0xa00, 4, 0xa80, 4),
};

static struct atlas7_grp_mux c1_can_rxd_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_rxd_grp3_pad_mux),
	.pad_mux_list = c1_can_rxd_grp3_pad_mux,
};

static struct atlas7_pad_mux c1_can_txd_grp0_pad_mux[] = {
	MUX(1, 137, 2, N, N, N, N),
};

static struct atlas7_grp_mux c1_can_txd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_txd_grp0_pad_mux),
	.pad_mux_list = c1_can_txd_grp0_pad_mux,
};

static struct atlas7_pad_mux c1_can_txd_grp1_pad_mux[] = {
	MUX(1, 146, 2, N, N, N, N),
};

static struct atlas7_grp_mux c1_can_txd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_txd_grp1_pad_mux),
	.pad_mux_list = c1_can_txd_grp1_pad_mux,
};

static struct atlas7_pad_mux c1_can_txd_grp2_pad_mux[] = {
	MUX(0, 3, 2, N, N, N, N),
};

static struct atlas7_grp_mux c1_can_txd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_txd_grp2_pad_mux),
	.pad_mux_list = c1_can_txd_grp2_pad_mux,
};

static struct atlas7_pad_mux c1_can_txd_grp3_pad_mux[] = {
	MUX(1, 161, 4, N, N, N, N),
};

static struct atlas7_grp_mux c1_can_txd_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(c1_can_txd_grp3_pad_mux),
	.pad_mux_list = c1_can_txd_grp3_pad_mux,
};

static struct atlas7_pad_mux ca_audio_lpc_grp_pad_mux[] = {
	MUX(1, 62, 4, N, N, N, N),
	MUX(1, 63, 4, N, N, N, N),
	MUX(1, 64, 4, N, N, N, N),
	MUX(1, 65, 4, N, N, N, N),
	MUX(1, 66, 4, N, N, N, N),
	MUX(1, 67, 4, N, N, N, N),
	MUX(1, 68, 4, N, N, N, N),
	MUX(1, 69, 4, N, N, N, N),
	MUX(1, 70, 4, N, N, N, N),
	MUX(1, 71, 4, N, N, N, N),
};

static struct atlas7_grp_mux ca_audio_lpc_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_audio_lpc_grp_pad_mux),
	.pad_mux_list = ca_audio_lpc_grp_pad_mux,
};

static struct atlas7_pad_mux ca_bt_lpc_grp_pad_mux[] = {
	MUX(1, 85, 5, N, N, N, N),
	MUX(1, 86, 5, N, N, N, N),
	MUX(1, 87, 5, N, N, N, N),
	MUX(1, 88, 5, N, N, N, N),
	MUX(1, 89, 5, N, N, N, N),
	MUX(1, 90, 5, N, N, N, N),
};

static struct atlas7_grp_mux ca_bt_lpc_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_bt_lpc_grp_pad_mux),
	.pad_mux_list = ca_bt_lpc_grp_pad_mux,
};

static struct atlas7_pad_mux ca_coex_grp_pad_mux[] = {
	MUX(1, 129, 1, N, N, N, N),
	MUX(1, 130, 1, N, N, N, N),
	MUX(1, 131, 1, N, N, N, N),
	MUX(1, 132, 1, N, N, N, N),
};

static struct atlas7_grp_mux ca_coex_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_coex_grp_pad_mux),
	.pad_mux_list = ca_coex_grp_pad_mux,
};

static struct atlas7_pad_mux ca_curator_lpc_grp_pad_mux[] = {
	MUX(1, 57, 4, N, N, N, N),
	MUX(1, 58, 4, N, N, N, N),
	MUX(1, 59, 4, N, N, N, N),
	MUX(1, 60, 4, N, N, N, N),
};

static struct atlas7_grp_mux ca_curator_lpc_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_curator_lpc_grp_pad_mux),
	.pad_mux_list = ca_curator_lpc_grp_pad_mux,
};

static struct atlas7_pad_mux ca_pcm_debug_grp_pad_mux[] = {
	MUX(1, 91, 5, N, N, N, N),
	MUX(1, 93, 5, N, N, N, N),
	MUX(1, 94, 5, N, N, N, N),
	MUX(1, 92, 5, N, N, N, N),
};

static struct atlas7_grp_mux ca_pcm_debug_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_pcm_debug_grp_pad_mux),
	.pad_mux_list = ca_pcm_debug_grp_pad_mux,
};

static struct atlas7_pad_mux ca_pio_grp_pad_mux[] = {
	MUX(1, 121, 2, N, N, N, N),
	MUX(1, 122, 2, N, N, N, N),
	MUX(1, 125, 6, N, N, N, N),
	MUX(1, 126, 6, N, N, N, N),
	MUX(1, 38, 5, N, N, N, N),
	MUX(1, 37, 5, N, N, N, N),
	MUX(1, 47, 5, N, N, N, N),
	MUX(1, 49, 5, N, N, N, N),
	MUX(1, 50, 5, N, N, N, N),
	MUX(1, 54, 4, N, N, N, N),
	MUX(1, 55, 4, N, N, N, N),
	MUX(1, 56, 4, N, N, N, N),
};

static struct atlas7_grp_mux ca_pio_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_pio_grp_pad_mux),
	.pad_mux_list = ca_pio_grp_pad_mux,
};

static struct atlas7_pad_mux ca_sdio_debug_grp_pad_mux[] = {
	MUX(1, 40, 5, N, N, N, N),
	MUX(1, 39, 5, N, N, N, N),
	MUX(1, 44, 5, N, N, N, N),
	MUX(1, 43, 5, N, N, N, N),
	MUX(1, 42, 5, N, N, N, N),
	MUX(1, 41, 5, N, N, N, N),
};

static struct atlas7_grp_mux ca_sdio_debug_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_sdio_debug_grp_pad_mux),
	.pad_mux_list = ca_sdio_debug_grp_pad_mux,
};

static struct atlas7_pad_mux ca_spi_grp_pad_mux[] = {
	MUX(1, 82, 5, N, N, N, N),
	MUX(1, 79, 5, 0xa08, 6, 0xa88, 6),
	MUX(1, 80, 5, N, N, N, N),
	MUX(1, 81, 5, N, N, N, N),
};

static struct atlas7_grp_mux ca_spi_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_spi_grp_pad_mux),
	.pad_mux_list = ca_spi_grp_pad_mux,
};

static struct atlas7_pad_mux ca_trb_grp_pad_mux[] = {
	MUX(1, 91, 4, N, N, N, N),
	MUX(1, 93, 4, N, N, N, N),
	MUX(1, 94, 4, N, N, N, N),
	MUX(1, 95, 4, N, N, N, N),
	MUX(1, 96, 4, N, N, N, N),
	MUX(1, 78, 5, N, N, N, N),
	MUX(1, 74, 5, N, N, N, N),
	MUX(1, 75, 5, N, N, N, N),
	MUX(1, 76, 5, N, N, N, N),
	MUX(1, 77, 5, N, N, N, N),
};

static struct atlas7_grp_mux ca_trb_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_trb_grp_pad_mux),
	.pad_mux_list = ca_trb_grp_pad_mux,
};

static struct atlas7_pad_mux ca_uart_debug_grp_pad_mux[] = {
	MUX(1, 136, 3, N, N, N, N),
	MUX(1, 135, 3, N, N, N, N),
	MUX(1, 134, 3, N, N, N, N),
	MUX(1, 133, 3, N, N, N, N),
};

static struct atlas7_grp_mux ca_uart_debug_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ca_uart_debug_grp_pad_mux),
	.pad_mux_list = ca_uart_debug_grp_pad_mux,
};

static struct atlas7_pad_mux clkc_grp0_pad_mux[] = {
	MUX(1, 30, 2, 0xa08, 14, 0xa88, 14),
	MUX(1, 47, 6, N, N, N, N),
};

static struct atlas7_grp_mux clkc_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(clkc_grp0_pad_mux),
	.pad_mux_list = clkc_grp0_pad_mux,
};

static struct atlas7_pad_mux clkc_grp1_pad_mux[] = {
	MUX(1, 78, 3, 0xa08, 14, 0xa88, 14),
	MUX(1, 54, 5, N, N, N, N),
};

static struct atlas7_grp_mux clkc_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(clkc_grp1_pad_mux),
	.pad_mux_list = clkc_grp1_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_i2c_grp_pad_mux[] = {
	MUX(1, 128, 2, N, N, N, N),
	MUX(1, 127, 2, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_i2c_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_i2c_grp_pad_mux),
	.pad_mux_list = gn_gnss_i2c_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_uart_nopause_grp_pad_mux[] = {
	MUX(1, 134, 4, N, N, N, N),
	MUX(1, 133, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_uart_nopause_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_uart_nopause_grp_pad_mux),
	.pad_mux_list = gn_gnss_uart_nopause_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_uart_grp_pad_mux[] = {
	MUX(1, 134, 4, N, N, N, N),
	MUX(1, 133, 4, N, N, N, N),
	MUX(1, 136, 4, N, N, N, N),
	MUX(1, 135, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_uart_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_uart_grp_pad_mux),
	.pad_mux_list = gn_gnss_uart_grp_pad_mux,
};

static struct atlas7_pad_mux gn_trg_spi_grp0_pad_mux[] = {
	MUX(1, 22, 1, N, N, N, N),
	MUX(1, 25, 1, N, N, N, N),
	MUX(1, 23, 1, 0xa00, 10, 0xa80, 10),
	MUX(1, 24, 1, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_spi_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_spi_grp0_pad_mux),
	.pad_mux_list = gn_trg_spi_grp0_pad_mux,
};

static struct atlas7_pad_mux gn_trg_spi_grp1_pad_mux[] = {
	MUX(1, 82, 3, N, N, N, N),
	MUX(1, 79, 3, N, N, N, N),
	MUX(1, 80, 3, 0xa00, 10, 0xa80, 10),
	MUX(1, 81, 3, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_spi_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_spi_grp1_pad_mux),
	.pad_mux_list = gn_trg_spi_grp1_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_grp_pad_mux[] = {
	MUX(1, 54, 3, N, N, N, N),
	MUX(1, 53, 3, N, N, N, N),
	MUX(1, 82, 7, N, N, N, N),
	MUX(1, 74, 7, N, N, N, N),
	MUX(1, 75, 7, N, N, N, N),
	MUX(1, 76, 7, N, N, N, N),
	MUX(1, 77, 7, N, N, N, N),
	MUX(1, 78, 7, N, N, N, N),
	MUX(1, 79, 7, N, N, N, N),
	MUX(1, 80, 7, N, N, N, N),
	MUX(1, 81, 7, N, N, N, N),
	MUX(1, 83, 7, N, N, N, N),
	MUX(1, 84, 7, N, N, N, N),
	MUX(1, 73, 3, N, N, N, N),
	MUX(1, 55, 3, N, N, N, N),
	MUX(1, 56, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_grp_pad_mux),
	.pad_mux_list = cvbs_dbg_grp_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp0_pad_mux[] = {
	MUX(1, 57, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp0_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp0_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp1_pad_mux[] = {
	MUX(1, 58, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp1_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp1_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp2_pad_mux[] = {
	MUX(1, 59, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp2_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp2_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp3_pad_mux[] = {
	MUX(1, 60, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp3_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp3_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp4_pad_mux[] = {
	MUX(1, 61, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp4_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp4_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp4_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp5_pad_mux[] = {
	MUX(1, 62, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp5_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp5_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp5_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp6_pad_mux[] = {
	MUX(1, 63, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp6_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp6_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp6_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp7_pad_mux[] = {
	MUX(1, 64, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp7_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp7_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp7_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp8_pad_mux[] = {
	MUX(1, 65, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp8_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp8_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp8_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp9_pad_mux[] = {
	MUX(1, 66, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp9_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp9_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp9_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp10_pad_mux[] = {
	MUX(1, 67, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp10_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp10_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp10_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp11_pad_mux[] = {
	MUX(1, 68, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp11_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp11_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp11_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp12_pad_mux[] = {
	MUX(1, 69, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp12_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp12_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp12_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp13_pad_mux[] = {
	MUX(1, 70, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp13_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp13_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp13_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp14_pad_mux[] = {
	MUX(1, 71, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp14_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp14_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp14_pad_mux,
};

static struct atlas7_pad_mux cvbs_dbg_test_grp15_pad_mux[] = {
	MUX(1, 72, 3, N, N, N, N),
};

static struct atlas7_grp_mux cvbs_dbg_test_grp15_mux = {
	.pad_mux_count = ARRAY_SIZE(cvbs_dbg_test_grp15_pad_mux),
	.pad_mux_list = cvbs_dbg_test_grp15_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_power_grp_pad_mux[] = {
	MUX(1, 123, 7, N, N, N, N),
	MUX(1, 124, 7, N, N, N, N),
	MUX(1, 121, 7, N, N, N, N),
	MUX(1, 122, 7, N, N, N, N),
	MUX(1, 125, 7, N, N, N, N),
	MUX(1, 120, 7, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_power_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_power_grp_pad_mux),
	.pad_mux_list = gn_gnss_power_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_sw_status_grp_pad_mux[] = {
	MUX(1, 57, 7, N, N, N, N),
	MUX(1, 58, 7, N, N, N, N),
	MUX(1, 59, 7, N, N, N, N),
	MUX(1, 60, 7, N, N, N, N),
	MUX(1, 61, 7, N, N, N, N),
	MUX(1, 62, 7, N, N, N, N),
	MUX(1, 63, 7, N, N, N, N),
	MUX(1, 64, 7, N, N, N, N),
	MUX(1, 65, 7, N, N, N, N),
	MUX(1, 66, 7, N, N, N, N),
	MUX(1, 67, 7, N, N, N, N),
	MUX(1, 68, 7, N, N, N, N),
	MUX(1, 69, 7, N, N, N, N),
	MUX(1, 70, 7, N, N, N, N),
	MUX(1, 71, 7, N, N, N, N),
	MUX(1, 72, 7, N, N, N, N),
	MUX(1, 53, 7, N, N, N, N),
	MUX(1, 55, 7, N, N, N, N),
	MUX(1, 56, 7, 0xa08, 12, 0xa88, 12),
	MUX(1, 54, 7, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_sw_status_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_sw_status_grp_pad_mux),
	.pad_mux_list = gn_gnss_sw_status_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_eclk_grp_pad_mux[] = {
	MUX(1, 113, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_eclk_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_eclk_grp_pad_mux),
	.pad_mux_list = gn_gnss_eclk_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_irq1_grp0_pad_mux[] = {
	MUX(1, 112, 4, 0xa08, 10, 0xa88, 10),
};

static struct atlas7_grp_mux gn_gnss_irq1_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_irq1_grp0_pad_mux),
	.pad_mux_list = gn_gnss_irq1_grp0_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_irq2_grp0_pad_mux[] = {
	MUX(1, 118, 4, 0xa08, 11, 0xa88, 11),
};

static struct atlas7_grp_mux gn_gnss_irq2_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_irq2_grp0_pad_mux),
	.pad_mux_list = gn_gnss_irq2_grp0_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_tm_grp_pad_mux[] = {
	MUX(1, 115, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_tm_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_tm_grp_pad_mux),
	.pad_mux_list = gn_gnss_tm_grp_pad_mux,
};

static struct atlas7_pad_mux gn_gnss_tsync_grp_pad_mux[] = {
	MUX(1, 114, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_gnss_tsync_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_gnss_tsync_grp_pad_mux),
	.pad_mux_list = gn_gnss_tsync_grp_pad_mux,
};

static struct atlas7_pad_mux gn_io_gnsssys_sw_cfg_grp_pad_mux[] = {
	MUX(1, 44, 7, N, N, N, N),
	MUX(1, 43, 7, N, N, N, N),
	MUX(1, 42, 7, N, N, N, N),
	MUX(1, 41, 7, N, N, N, N),
	MUX(1, 40, 7, N, N, N, N),
	MUX(1, 39, 7, N, N, N, N),
	MUX(1, 38, 7, N, N, N, N),
	MUX(1, 37, 7, N, N, N, N),
	MUX(1, 49, 7, N, N, N, N),
	MUX(1, 50, 7, N, N, N, N),
	MUX(1, 91, 7, N, N, N, N),
	MUX(1, 92, 7, N, N, N, N),
	MUX(1, 93, 7, N, N, N, N),
	MUX(1, 94, 7, N, N, N, N),
	MUX(1, 95, 7, N, N, N, N),
	MUX(1, 96, 7, N, N, N, N),
};

static struct atlas7_grp_mux gn_io_gnsssys_sw_cfg_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_io_gnsssys_sw_cfg_grp_pad_mux),
	.pad_mux_list = gn_io_gnsssys_sw_cfg_grp_pad_mux,
};

static struct atlas7_pad_mux gn_trg_grp0_pad_mux[] = {
	MUX(1, 29, 1, 0xa00, 6, 0xa80, 6),
	MUX(1, 28, 1, 0xa00, 7, 0xa80, 7),
	MUX(1, 26, 1, 0xa00, 8, 0xa80, 8),
	MUX(1, 27, 1, 0xa00, 9, 0xa80, 9),
};

static struct atlas7_grp_mux gn_trg_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_grp0_pad_mux),
	.pad_mux_list = gn_trg_grp0_pad_mux,
};

static struct atlas7_pad_mux gn_trg_grp1_pad_mux[] = {
	MUX(1, 77, 3, 0xa00, 6, 0xa80, 6),
	MUX(1, 76, 3, 0xa00, 7, 0xa80, 7),
	MUX(1, 74, 3, 0xa00, 8, 0xa80, 8),
	MUX(1, 75, 3, 0xa00, 9, 0xa80, 9),
};

static struct atlas7_grp_mux gn_trg_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_grp1_pad_mux),
	.pad_mux_list = gn_trg_grp1_pad_mux,
};

static struct atlas7_pad_mux gn_trg_shutdown_grp0_pad_mux[] = {
	MUX(1, 30, 1, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_shutdown_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_shutdown_grp0_pad_mux),
	.pad_mux_list = gn_trg_shutdown_grp0_pad_mux,
};

static struct atlas7_pad_mux gn_trg_shutdown_grp1_pad_mux[] = {
	MUX(1, 83, 3, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_shutdown_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_shutdown_grp1_pad_mux),
	.pad_mux_list = gn_trg_shutdown_grp1_pad_mux,
};

static struct atlas7_pad_mux gn_trg_shutdown_grp2_pad_mux[] = {
	MUX(1, 117, 4, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_shutdown_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_shutdown_grp2_pad_mux),
	.pad_mux_list = gn_trg_shutdown_grp2_pad_mux,
};

static struct atlas7_pad_mux gn_trg_shutdown_grp3_pad_mux[] = {
	MUX(1, 123, 5, N, N, N, N),
};

static struct atlas7_grp_mux gn_trg_shutdown_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(gn_trg_shutdown_grp3_pad_mux),
	.pad_mux_list = gn_trg_shutdown_grp3_pad_mux,
};

static struct atlas7_pad_mux i2c0_grp_pad_mux[] = {
	MUX(1, 128, 1, N, N, N, N),
	MUX(1, 127, 1, N, N, N, N),
};

static struct atlas7_grp_mux i2c0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(i2c0_grp_pad_mux),
	.pad_mux_list = i2c0_grp_pad_mux,
};

static struct atlas7_pad_mux i2c1_grp_pad_mux[] = {
	MUX(1, 126, 4, N, N, N, N),
	MUX(1, 125, 4, N, N, N, N),
};

static struct atlas7_grp_mux i2c1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(i2c1_grp_pad_mux),
	.pad_mux_list = i2c1_grp_pad_mux,
};

static struct atlas7_pad_mux i2s0_grp_pad_mux[] = {
	MUX(1, 91, 2, 0xa10, 12, 0xa90, 12),
	MUX(1, 93, 2, 0xa10, 13, 0xa90, 13),
	MUX(1, 94, 2, 0xa10, 14, 0xa90, 14),
	MUX(1, 92, 2, 0xa10, 15, 0xa90, 15),
};

static struct atlas7_grp_mux i2s0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s0_grp_pad_mux),
	.pad_mux_list = i2s0_grp_pad_mux,
};

static struct atlas7_pad_mux i2s1_basic_grp_pad_mux[] = {
	MUX(1, 95, 2, 0xa10, 16, 0xa90, 16),
	MUX(1, 96, 2, 0xa10, 19, 0xa90, 19),
};

static struct atlas7_grp_mux i2s1_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_basic_grp_pad_mux),
	.pad_mux_list = i2s1_basic_grp_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd0_grp0_pad_mux[] = {
	MUX(1, 61, 4, 0xa10, 17, 0xa90, 17),
};

static struct atlas7_grp_mux i2s1_rxd0_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd0_grp0_pad_mux),
	.pad_mux_list = i2s1_rxd0_grp0_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd0_grp1_pad_mux[] = {
	MUX(1, 131, 4, 0xa10, 17, 0xa90, 17),
};

static struct atlas7_grp_mux i2s1_rxd0_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd0_grp1_pad_mux),
	.pad_mux_list = i2s1_rxd0_grp1_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd0_grp2_pad_mux[] = {
	MUX(1, 129, 2, 0xa10, 17, 0xa90, 17),
};

static struct atlas7_grp_mux i2s1_rxd0_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd0_grp2_pad_mux),
	.pad_mux_list = i2s1_rxd0_grp2_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd0_grp3_pad_mux[] = {
	MUX(1, 117, 7, 0xa10, 17, 0xa90, 17),
};

static struct atlas7_grp_mux i2s1_rxd0_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd0_grp3_pad_mux),
	.pad_mux_list = i2s1_rxd0_grp3_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd0_grp4_pad_mux[] = {
	MUX(1, 83, 4, 0xa10, 17, 0xa90, 17),
};

static struct atlas7_grp_mux i2s1_rxd0_grp4_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd0_grp4_pad_mux),
	.pad_mux_list = i2s1_rxd0_grp4_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd1_grp0_pad_mux[] = {
	MUX(1, 72, 4, 0xa10, 18, 0xa90, 18),
};

static struct atlas7_grp_mux i2s1_rxd1_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd1_grp0_pad_mux),
	.pad_mux_list = i2s1_rxd1_grp0_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd1_grp1_pad_mux[] = {
	MUX(1, 132, 4, 0xa10, 18, 0xa90, 18),
};

static struct atlas7_grp_mux i2s1_rxd1_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd1_grp1_pad_mux),
	.pad_mux_list = i2s1_rxd1_grp1_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd1_grp2_pad_mux[] = {
	MUX(1, 130, 2, 0xa10, 18, 0xa90, 18),
};

static struct atlas7_grp_mux i2s1_rxd1_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd1_grp2_pad_mux),
	.pad_mux_list = i2s1_rxd1_grp2_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd1_grp3_pad_mux[] = {
	MUX(1, 118, 7, 0xa10, 18, 0xa90, 18),
};

static struct atlas7_grp_mux i2s1_rxd1_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd1_grp3_pad_mux),
	.pad_mux_list = i2s1_rxd1_grp3_pad_mux,
};

static struct atlas7_pad_mux i2s1_rxd1_grp4_pad_mux[] = {
	MUX(1, 84, 4, 0xa10, 18, 0xa90, 18),
};

static struct atlas7_grp_mux i2s1_rxd1_grp4_mux = {
	.pad_mux_count = ARRAY_SIZE(i2s1_rxd1_grp4_pad_mux),
	.pad_mux_list = i2s1_rxd1_grp4_pad_mux,
};

static struct atlas7_pad_mux jtag_jt_dbg_nsrst_grp_pad_mux[] = {
	MUX(1, 125, 5, 0xa08, 2, 0xa88, 2),
};

static struct atlas7_grp_mux jtag_jt_dbg_nsrst_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_jt_dbg_nsrst_grp_pad_mux),
	.pad_mux_list = jtag_jt_dbg_nsrst_grp_pad_mux,
};

static struct atlas7_pad_mux jtag_ntrst_grp0_pad_mux[] = {
	MUX(0, 4, 3, 0xa08, 3, 0xa88, 3),
};

static struct atlas7_grp_mux jtag_ntrst_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_ntrst_grp0_pad_mux),
	.pad_mux_list = jtag_ntrst_grp0_pad_mux,
};

static struct atlas7_pad_mux jtag_ntrst_grp1_pad_mux[] = {
	MUX(1, 163, 1, 0xa08, 3, 0xa88, 3),
};

static struct atlas7_grp_mux jtag_ntrst_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_ntrst_grp1_pad_mux),
	.pad_mux_list = jtag_ntrst_grp1_pad_mux,
};

static struct atlas7_pad_mux jtag_swdiotms_grp0_pad_mux[] = {
	MUX(0, 2, 3, 0xa10, 10, 0xa90, 10),
};

static struct atlas7_grp_mux jtag_swdiotms_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_swdiotms_grp0_pad_mux),
	.pad_mux_list = jtag_swdiotms_grp0_pad_mux,
};

static struct atlas7_pad_mux jtag_swdiotms_grp1_pad_mux[] = {
	MUX(1, 160, 1, 0xa10, 10, 0xa90, 10),
};

static struct atlas7_grp_mux jtag_swdiotms_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_swdiotms_grp1_pad_mux),
	.pad_mux_list = jtag_swdiotms_grp1_pad_mux,
};

static struct atlas7_pad_mux jtag_tck_grp0_pad_mux[] = {
	MUX(0, 0, 3, 0xa10, 11, 0xa90, 11),
};

static struct atlas7_grp_mux jtag_tck_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tck_grp0_pad_mux),
	.pad_mux_list = jtag_tck_grp0_pad_mux,
};

static struct atlas7_pad_mux jtag_tck_grp1_pad_mux[] = {
	MUX(1, 161, 1, 0xa10, 11, 0xa90, 11),
};

static struct atlas7_grp_mux jtag_tck_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tck_grp1_pad_mux),
	.pad_mux_list = jtag_tck_grp1_pad_mux,
};

static struct atlas7_pad_mux jtag_tdi_grp0_pad_mux[] = {
	MUX(0, 1, 3, 0xa10, 31, 0xa90, 31),
};

static struct atlas7_grp_mux jtag_tdi_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tdi_grp0_pad_mux),
	.pad_mux_list = jtag_tdi_grp0_pad_mux,
};

static struct atlas7_pad_mux jtag_tdi_grp1_pad_mux[] = {
	MUX(1, 162, 1, 0xa10, 31, 0xa90, 31),
};

static struct atlas7_grp_mux jtag_tdi_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tdi_grp1_pad_mux),
	.pad_mux_list = jtag_tdi_grp1_pad_mux,
};

static struct atlas7_pad_mux jtag_tdo_grp0_pad_mux[] = {
	MUX(0, 3, 3, N, N, N, N),
};

static struct atlas7_grp_mux jtag_tdo_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tdo_grp0_pad_mux),
	.pad_mux_list = jtag_tdo_grp0_pad_mux,
};

static struct atlas7_pad_mux jtag_tdo_grp1_pad_mux[] = {
	MUX(1, 159, 1, N, N, N, N),
};

static struct atlas7_grp_mux jtag_tdo_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(jtag_tdo_grp1_pad_mux),
	.pad_mux_list = jtag_tdo_grp1_pad_mux,
};

static struct atlas7_pad_mux ks_kas_spi_grp0_pad_mux[] = {
	MUX(1, 141, 2, N, N, N, N),
	MUX(1, 144, 2, 0xa08, 8, 0xa88, 8),
	MUX(1, 143, 2, N, N, N, N),
	MUX(1, 142, 2, N, N, N, N),
};

static struct atlas7_grp_mux ks_kas_spi_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(ks_kas_spi_grp0_pad_mux),
	.pad_mux_list = ks_kas_spi_grp0_pad_mux,
};

static struct atlas7_pad_mux ld_ldd_grp_pad_mux[] = {
	MUX(1, 57, 1, N, N, N, N),
	MUX(1, 58, 1, N, N, N, N),
	MUX(1, 59, 1, N, N, N, N),
	MUX(1, 60, 1, N, N, N, N),
	MUX(1, 61, 1, N, N, N, N),
	MUX(1, 62, 1, N, N, N, N),
	MUX(1, 63, 1, N, N, N, N),
	MUX(1, 64, 1, N, N, N, N),
	MUX(1, 65, 1, N, N, N, N),
	MUX(1, 66, 1, N, N, N, N),
	MUX(1, 67, 1, N, N, N, N),
	MUX(1, 68, 1, N, N, N, N),
	MUX(1, 69, 1, N, N, N, N),
	MUX(1, 70, 1, N, N, N, N),
	MUX(1, 71, 1, N, N, N, N),
	MUX(1, 72, 1, N, N, N, N),
	MUX(1, 74, 2, N, N, N, N),
	MUX(1, 75, 2, N, N, N, N),
	MUX(1, 76, 2, N, N, N, N),
	MUX(1, 77, 2, N, N, N, N),
	MUX(1, 78, 2, N, N, N, N),
	MUX(1, 79, 2, N, N, N, N),
	MUX(1, 80, 2, N, N, N, N),
	MUX(1, 81, 2, N, N, N, N),
	MUX(1, 56, 1, N, N, N, N),
	MUX(1, 53, 1, N, N, N, N),
};

static struct atlas7_grp_mux ld_ldd_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ld_ldd_grp_pad_mux),
	.pad_mux_list = ld_ldd_grp_pad_mux,
};

static struct atlas7_pad_mux ld_ldd_16bit_grp_pad_mux[] = {
	MUX(1, 57, 1, N, N, N, N),
	MUX(1, 58, 1, N, N, N, N),
	MUX(1, 59, 1, N, N, N, N),
	MUX(1, 60, 1, N, N, N, N),
	MUX(1, 61, 1, N, N, N, N),
	MUX(1, 62, 1, N, N, N, N),
	MUX(1, 63, 1, N, N, N, N),
	MUX(1, 64, 1, N, N, N, N),
	MUX(1, 65, 1, N, N, N, N),
	MUX(1, 66, 1, N, N, N, N),
	MUX(1, 67, 1, N, N, N, N),
	MUX(1, 68, 1, N, N, N, N),
	MUX(1, 69, 1, N, N, N, N),
	MUX(1, 70, 1, N, N, N, N),
	MUX(1, 71, 1, N, N, N, N),
	MUX(1, 72, 1, N, N, N, N),
	MUX(1, 56, 1, N, N, N, N),
	MUX(1, 53, 1, N, N, N, N),
};

static struct atlas7_grp_mux ld_ldd_16bit_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ld_ldd_16bit_grp_pad_mux),
	.pad_mux_list = ld_ldd_16bit_grp_pad_mux,
};

static struct atlas7_pad_mux ld_ldd_fck_grp_pad_mux[] = {
	MUX(1, 55, 1, N, N, N, N),
};

static struct atlas7_grp_mux ld_ldd_fck_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ld_ldd_fck_grp_pad_mux),
	.pad_mux_list = ld_ldd_fck_grp_pad_mux,
};

static struct atlas7_pad_mux ld_ldd_lck_grp_pad_mux[] = {
	MUX(1, 54, 1, N, N, N, N),
};

static struct atlas7_grp_mux ld_ldd_lck_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ld_ldd_lck_grp_pad_mux),
	.pad_mux_list = ld_ldd_lck_grp_pad_mux,
};

static struct atlas7_pad_mux lr_lcdrom_grp_pad_mux[] = {
	MUX(1, 73, 2, N, N, N, N),
	MUX(1, 54, 2, N, N, N, N),
	MUX(1, 57, 2, N, N, N, N),
	MUX(1, 58, 2, N, N, N, N),
	MUX(1, 59, 2, N, N, N, N),
	MUX(1, 60, 2, N, N, N, N),
	MUX(1, 61, 2, N, N, N, N),
	MUX(1, 62, 2, N, N, N, N),
	MUX(1, 63, 2, N, N, N, N),
	MUX(1, 64, 2, N, N, N, N),
	MUX(1, 65, 2, N, N, N, N),
	MUX(1, 66, 2, N, N, N, N),
	MUX(1, 67, 2, N, N, N, N),
	MUX(1, 68, 2, N, N, N, N),
	MUX(1, 69, 2, N, N, N, N),
	MUX(1, 70, 2, N, N, N, N),
	MUX(1, 71, 2, N, N, N, N),
	MUX(1, 72, 2, N, N, N, N),
	MUX(1, 56, 2, N, N, N, N),
	MUX(1, 53, 2, N, N, N, N),
	MUX(1, 55, 2, N, N, N, N),
};

static struct atlas7_grp_mux lr_lcdrom_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(lr_lcdrom_grp_pad_mux),
	.pad_mux_list = lr_lcdrom_grp_pad_mux,
};

static struct atlas7_pad_mux lvds_analog_grp_pad_mux[] = {
	MUX(1, 149, 8, N, N, N, N),
	MUX(1, 150, 8, N, N, N, N),
	MUX(1, 151, 8, N, N, N, N),
	MUX(1, 152, 8, N, N, N, N),
	MUX(1, 153, 8, N, N, N, N),
	MUX(1, 154, 8, N, N, N, N),
	MUX(1, 155, 8, N, N, N, N),
	MUX(1, 156, 8, N, N, N, N),
	MUX(1, 157, 8, N, N, N, N),
	MUX(1, 158, 8, N, N, N, N),
};

static struct atlas7_grp_mux lvds_analog_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(lvds_analog_grp_pad_mux),
	.pad_mux_list = lvds_analog_grp_pad_mux,
};

static struct atlas7_pad_mux nd_df_basic_grp_pad_mux[] = {
	MUX(1, 44, 1, N, N, N, N),
	MUX(1, 43, 1, N, N, N, N),
	MUX(1, 42, 1, N, N, N, N),
	MUX(1, 41, 1, N, N, N, N),
	MUX(1, 40, 1, N, N, N, N),
	MUX(1, 39, 1, N, N, N, N),
	MUX(1, 38, 1, N, N, N, N),
	MUX(1, 37, 1, N, N, N, N),
	MUX(1, 47, 1, N, N, N, N),
	MUX(1, 46, 1, N, N, N, N),
	MUX(1, 52, 1, N, N, N, N),
	MUX(1, 45, 1, N, N, N, N),
	MUX(1, 49, 1, N, N, N, N),
	MUX(1, 50, 1, N, N, N, N),
	MUX(1, 48, 1, N, N, N, N),
};

static struct atlas7_grp_mux nd_df_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(nd_df_basic_grp_pad_mux),
	.pad_mux_list = nd_df_basic_grp_pad_mux,
};

static struct atlas7_pad_mux nd_df_wp_grp_pad_mux[] = {
	MUX(1, 124, 4, N, N, N, N),
};

static struct atlas7_grp_mux nd_df_wp_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(nd_df_wp_grp_pad_mux),
	.pad_mux_list = nd_df_wp_grp_pad_mux,
};

static struct atlas7_pad_mux nd_df_cs_grp_pad_mux[] = {
	MUX(1, 51, 1, N, N, N, N),
};

static struct atlas7_grp_mux nd_df_cs_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(nd_df_cs_grp_pad_mux),
	.pad_mux_list = nd_df_cs_grp_pad_mux,
};

static struct atlas7_pad_mux ps_grp_pad_mux[] = {
	MUX(1, 120, 2, N, N, N, N),
	MUX(1, 119, 2, N, N, N, N),
	MUX(1, 121, 5, N, N, N, N),
};

static struct atlas7_grp_mux ps_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ps_grp_pad_mux),
	.pad_mux_list = ps_grp_pad_mux,
};

static struct atlas7_pad_mux ps_no_dir_grp_pad_mux[] = {
	MUX(1, 119, 2, N, N, N, N),
};

static struct atlas7_grp_mux ps_no_dir_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(ps_no_dir_grp_pad_mux),
	.pad_mux_list = ps_no_dir_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_core_on_grp_pad_mux[] = {
	MUX(0, 8, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_core_on_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_core_on_grp_pad_mux),
	.pad_mux_list = pwc_core_on_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_ext_on_grp_pad_mux[] = {
	MUX(0, 6, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_ext_on_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_ext_on_grp_pad_mux),
	.pad_mux_list = pwc_ext_on_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_gpio3_clk_grp_pad_mux[] = {
	MUX(0, 3, 4, N, N, N, N),
};

static struct atlas7_grp_mux pwc_gpio3_clk_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_gpio3_clk_grp_pad_mux),
	.pad_mux_list = pwc_gpio3_clk_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_io_on_grp_pad_mux[] = {
	MUX(0, 9, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_io_on_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_io_on_grp_pad_mux),
	.pad_mux_list = pwc_io_on_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_lowbatt_b_grp0_pad_mux[] = {
	MUX(0, 4, 1, 0xa08, 4, 0xa88, 4),
};

static struct atlas7_grp_mux pwc_lowbatt_b_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_lowbatt_b_grp0_pad_mux),
	.pad_mux_list = pwc_lowbatt_b_grp0_pad_mux,
};

static struct atlas7_pad_mux pwc_mem_on_grp_pad_mux[] = {
	MUX(0, 7, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_mem_on_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_mem_on_grp_pad_mux),
	.pad_mux_list = pwc_mem_on_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_on_key_b_grp0_pad_mux[] = {
	MUX(0, 5, 1, 0xa08, 5, 0xa88, 5),
};

static struct atlas7_grp_mux pwc_on_key_b_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_on_key_b_grp0_pad_mux),
	.pad_mux_list = pwc_on_key_b_grp0_pad_mux,
};

static struct atlas7_pad_mux pwc_wakeup_src0_grp_pad_mux[] = {
	MUX(0, 0, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_wakeup_src0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_wakeup_src0_grp_pad_mux),
	.pad_mux_list = pwc_wakeup_src0_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_wakeup_src1_grp_pad_mux[] = {
	MUX(0, 1, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_wakeup_src1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_wakeup_src1_grp_pad_mux),
	.pad_mux_list = pwc_wakeup_src1_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_wakeup_src2_grp_pad_mux[] = {
	MUX(0, 2, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_wakeup_src2_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_wakeup_src2_grp_pad_mux),
	.pad_mux_list = pwc_wakeup_src2_grp_pad_mux,
};

static struct atlas7_pad_mux pwc_wakeup_src3_grp_pad_mux[] = {
	MUX(0, 3, 1, N, N, N, N),
};

static struct atlas7_grp_mux pwc_wakeup_src3_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(pwc_wakeup_src3_grp_pad_mux),
	.pad_mux_list = pwc_wakeup_src3_grp_pad_mux,
};

static struct atlas7_pad_mux pw_cko0_grp0_pad_mux[] = {
	MUX(1, 123, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko0_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko0_grp0_pad_mux),
	.pad_mux_list = pw_cko0_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_cko0_grp1_pad_mux[] = {
	MUX(1, 101, 4, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko0_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko0_grp1_pad_mux),
	.pad_mux_list = pw_cko0_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_cko0_grp2_pad_mux[] = {
	MUX(1, 82, 2, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko0_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko0_grp2_pad_mux),
	.pad_mux_list = pw_cko0_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_cko0_grp3_pad_mux[] = {
	MUX(1, 162, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko0_grp3_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko0_grp3_pad_mux),
	.pad_mux_list = pw_cko0_grp3_pad_mux,
};

static struct atlas7_pad_mux pw_cko1_grp0_pad_mux[] = {
	MUX(1, 124, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko1_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko1_grp0_pad_mux),
	.pad_mux_list = pw_cko1_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_cko1_grp1_pad_mux[] = {
	MUX(1, 110, 4, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko1_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko1_grp1_pad_mux),
	.pad_mux_list = pw_cko1_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_cko1_grp2_pad_mux[] = {
	MUX(1, 163, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_cko1_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_cko1_grp2_pad_mux),
	.pad_mux_list = pw_cko1_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_i2s01_clk_grp0_pad_mux[] = {
	MUX(1, 125, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_i2s01_clk_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_i2s01_clk_grp0_pad_mux),
	.pad_mux_list = pw_i2s01_clk_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_i2s01_clk_grp1_pad_mux[] = {
	MUX(1, 117, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_i2s01_clk_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_i2s01_clk_grp1_pad_mux),
	.pad_mux_list = pw_i2s01_clk_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_i2s01_clk_grp2_pad_mux[] = {
	MUX(1, 132, 2, N, N, N, N),
};

static struct atlas7_grp_mux pw_i2s01_clk_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_i2s01_clk_grp2_pad_mux),
	.pad_mux_list = pw_i2s01_clk_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_pwm0_grp0_pad_mux[] = {
	MUX(1, 119, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm0_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm0_grp0_pad_mux),
	.pad_mux_list = pw_pwm0_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_pwm0_grp1_pad_mux[] = {
	MUX(1, 159, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm0_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm0_grp1_pad_mux),
	.pad_mux_list = pw_pwm0_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_pwm1_grp0_pad_mux[] = {
	MUX(1, 120, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm1_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm1_grp0_pad_mux),
	.pad_mux_list = pw_pwm1_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_pwm1_grp1_pad_mux[] = {
	MUX(1, 160, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm1_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm1_grp1_pad_mux),
	.pad_mux_list = pw_pwm1_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_pwm1_grp2_pad_mux[] = {
	MUX(1, 131, 2, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm1_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm1_grp2_pad_mux),
	.pad_mux_list = pw_pwm1_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_pwm2_grp0_pad_mux[] = {
	MUX(1, 121, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm2_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm2_grp0_pad_mux),
	.pad_mux_list = pw_pwm2_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_pwm2_grp1_pad_mux[] = {
	MUX(1, 98, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm2_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm2_grp1_pad_mux),
	.pad_mux_list = pw_pwm2_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_pwm2_grp2_pad_mux[] = {
	MUX(1, 161, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm2_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm2_grp2_pad_mux),
	.pad_mux_list = pw_pwm2_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_pwm3_grp0_pad_mux[] = {
	MUX(1, 122, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm3_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm3_grp0_pad_mux),
	.pad_mux_list = pw_pwm3_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_pwm3_grp1_pad_mux[] = {
	MUX(1, 73, 4, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm3_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm3_grp1_pad_mux),
	.pad_mux_list = pw_pwm3_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_pwm_cpu_vol_grp0_pad_mux[] = {
	MUX(1, 121, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm_cpu_vol_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm_cpu_vol_grp0_pad_mux),
	.pad_mux_list = pw_pwm_cpu_vol_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_pwm_cpu_vol_grp1_pad_mux[] = {
	MUX(1, 98, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm_cpu_vol_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm_cpu_vol_grp1_pad_mux),
	.pad_mux_list = pw_pwm_cpu_vol_grp1_pad_mux,
};

static struct atlas7_pad_mux pw_pwm_cpu_vol_grp2_pad_mux[] = {
	MUX(1, 161, 5, N, N, N, N),
};

static struct atlas7_grp_mux pw_pwm_cpu_vol_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_pwm_cpu_vol_grp2_pad_mux),
	.pad_mux_list = pw_pwm_cpu_vol_grp2_pad_mux,
};

static struct atlas7_pad_mux pw_backlight_grp0_pad_mux[] = {
	MUX(1, 122, 3, N, N, N, N),
};

static struct atlas7_grp_mux pw_backlight_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_backlight_grp0_pad_mux),
	.pad_mux_list = pw_backlight_grp0_pad_mux,
};

static struct atlas7_pad_mux pw_backlight_grp1_pad_mux[] = {
	MUX(1, 73, 4, N, N, N, N),
};

static struct atlas7_grp_mux pw_backlight_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(pw_backlight_grp1_pad_mux),
	.pad_mux_list = pw_backlight_grp1_pad_mux,
};

static struct atlas7_pad_mux rg_eth_mac_grp_pad_mux[] = {
	MUX(1, 108, 1, N, N, N, N),
	MUX(1, 103, 1, N, N, N, N),
	MUX(1, 104, 1, N, N, N, N),
	MUX(1, 105, 1, N, N, N, N),
	MUX(1, 106, 1, N, N, N, N),
	MUX(1, 107, 1, N, N, N, N),
	MUX(1, 102, 1, N, N, N, N),
	MUX(1, 97, 1, N, N, N, N),
	MUX(1, 98, 1, N, N, N, N),
	MUX(1, 99, 1, N, N, N, N),
	MUX(1, 100, 1, N, N, N, N),
	MUX(1, 101, 1, N, N, N, N),
};

static struct atlas7_grp_mux rg_eth_mac_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(rg_eth_mac_grp_pad_mux),
	.pad_mux_list = rg_eth_mac_grp_pad_mux,
};

static struct atlas7_pad_mux rg_gmac_phy_intr_n_grp_pad_mux[] = {
	MUX(1, 111, 1, 0xa08, 13, 0xa88, 13),
};

static struct atlas7_grp_mux rg_gmac_phy_intr_n_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(rg_gmac_phy_intr_n_grp_pad_mux),
	.pad_mux_list = rg_gmac_phy_intr_n_grp_pad_mux,
};

static struct atlas7_pad_mux rg_rgmii_mac_grp_pad_mux[] = {
	MUX(1, 109, 1, N, N, N, N),
	MUX(1, 110, 1, N, N, N, N),
};

static struct atlas7_grp_mux rg_rgmii_mac_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(rg_rgmii_mac_grp_pad_mux),
	.pad_mux_list = rg_rgmii_mac_grp_pad_mux,
};

static struct atlas7_pad_mux rg_rgmii_phy_ref_clk_grp0_pad_mux[] = {
	MUX(1, 111, 5, N, N, N, N),
};

static struct atlas7_grp_mux rg_rgmii_phy_ref_clk_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(rg_rgmii_phy_ref_clk_grp0_pad_mux),
	.pad_mux_list = rg_rgmii_phy_ref_clk_grp0_pad_mux,
};

static struct atlas7_pad_mux rg_rgmii_phy_ref_clk_grp1_pad_mux[] = {
	MUX(1, 53, 4, N, N, N, N),
};

static struct atlas7_grp_mux rg_rgmii_phy_ref_clk_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(rg_rgmii_phy_ref_clk_grp1_pad_mux),
	.pad_mux_list = rg_rgmii_phy_ref_clk_grp1_pad_mux,
};

static struct atlas7_pad_mux sd0_grp_pad_mux[] = {
	MUX(1, 46, 2, N, N, N, N),
	MUX(1, 47, 2, N, N, N, N),
	MUX(1, 44, 2, N, N, N, N),
	MUX(1, 43, 2, N, N, N, N),
	MUX(1, 42, 2, N, N, N, N),
	MUX(1, 41, 2, N, N, N, N),
	MUX(1, 40, 2, N, N, N, N),
	MUX(1, 39, 2, N, N, N, N),
	MUX(1, 38, 2, N, N, N, N),
	MUX(1, 37, 2, N, N, N, N),
};

static struct atlas7_grp_mux sd0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd0_grp_pad_mux),
	.pad_mux_list = sd0_grp_pad_mux,
};

static struct atlas7_pad_mux sd0_4bit_grp_pad_mux[] = {
	MUX(1, 46, 2, N, N, N, N),
	MUX(1, 47, 2, N, N, N, N),
	MUX(1, 44, 2, N, N, N, N),
	MUX(1, 43, 2, N, N, N, N),
	MUX(1, 42, 2, N, N, N, N),
	MUX(1, 41, 2, N, N, N, N),
};

static struct atlas7_grp_mux sd0_4bit_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd0_4bit_grp_pad_mux),
	.pad_mux_list = sd0_4bit_grp_pad_mux,
};

static struct atlas7_pad_mux sd1_grp_pad_mux[] = {
	MUX(1, 48, 3, N, N, N, N),
	MUX(1, 49, 3, N, N, N, N),
	MUX(1, 44, 3, 0xa00, 0, 0xa80, 0),
	MUX(1, 43, 3, 0xa00, 1, 0xa80, 1),
	MUX(1, 42, 3, 0xa00, 2, 0xa80, 2),
	MUX(1, 41, 3, 0xa00, 3, 0xa80, 3),
	MUX(1, 40, 3, N, N, N, N),
	MUX(1, 39, 3, N, N, N, N),
	MUX(1, 38, 3, N, N, N, N),
	MUX(1, 37, 3, N, N, N, N),
};

static struct atlas7_grp_mux sd1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd1_grp_pad_mux),
	.pad_mux_list = sd1_grp_pad_mux,
};

static struct atlas7_pad_mux sd1_4bit_grp0_pad_mux[] = {
	MUX(1, 48, 3, N, N, N, N),
	MUX(1, 49, 3, N, N, N, N),
	MUX(1, 44, 3, 0xa00, 0, 0xa80, 0),
	MUX(1, 43, 3, 0xa00, 1, 0xa80, 1),
	MUX(1, 42, 3, 0xa00, 2, 0xa80, 2),
	MUX(1, 41, 3, 0xa00, 3, 0xa80, 3),
};

static struct atlas7_grp_mux sd1_4bit_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(sd1_4bit_grp0_pad_mux),
	.pad_mux_list = sd1_4bit_grp0_pad_mux,
};

static struct atlas7_pad_mux sd1_4bit_grp1_pad_mux[] = {
	MUX(1, 48, 3, N, N, N, N),
	MUX(1, 49, 3, N, N, N, N),
	MUX(1, 40, 4, 0xa00, 0, 0xa80, 0),
	MUX(1, 39, 4, 0xa00, 1, 0xa80, 1),
	MUX(1, 38, 4, 0xa00, 2, 0xa80, 2),
	MUX(1, 37, 4, 0xa00, 3, 0xa80, 3),
};

static struct atlas7_grp_mux sd1_4bit_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(sd1_4bit_grp1_pad_mux),
	.pad_mux_list = sd1_4bit_grp1_pad_mux,
};

static struct atlas7_pad_mux sd2_basic_grp_pad_mux[] = {
	MUX(1, 31, 1, N, N, N, N),
	MUX(1, 32, 1, N, N, N, N),
	MUX(1, 33, 1, N, N, N, N),
	MUX(1, 34, 1, N, N, N, N),
	MUX(1, 35, 1, N, N, N, N),
	MUX(1, 36, 1, N, N, N, N),
};

static struct atlas7_grp_mux sd2_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd2_basic_grp_pad_mux),
	.pad_mux_list = sd2_basic_grp_pad_mux,
};

static struct atlas7_pad_mux sd2_cdb_grp0_pad_mux[] = {
	MUX(1, 124, 2, 0xa08, 7, 0xa88, 7),
};

static struct atlas7_grp_mux sd2_cdb_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(sd2_cdb_grp0_pad_mux),
	.pad_mux_list = sd2_cdb_grp0_pad_mux,
};

static struct atlas7_pad_mux sd2_cdb_grp1_pad_mux[] = {
	MUX(1, 161, 6, 0xa08, 7, 0xa88, 7),
};

static struct atlas7_grp_mux sd2_cdb_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(sd2_cdb_grp1_pad_mux),
	.pad_mux_list = sd2_cdb_grp1_pad_mux,
};

static struct atlas7_pad_mux sd2_wpb_grp0_pad_mux[] = {
	MUX(1, 123, 2, 0xa10, 6, 0xa90, 6),
};

static struct atlas7_grp_mux sd2_wpb_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(sd2_wpb_grp0_pad_mux),
	.pad_mux_list = sd2_wpb_grp0_pad_mux,
};

static struct atlas7_pad_mux sd2_wpb_grp1_pad_mux[] = {
	MUX(1, 163, 7, 0xa10, 6, 0xa90, 6),
};

static struct atlas7_grp_mux sd2_wpb_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(sd2_wpb_grp1_pad_mux),
	.pad_mux_list = sd2_wpb_grp1_pad_mux,
};

static struct atlas7_pad_mux sd3_9_grp_pad_mux[] = {
	MUX(1, 85, 1, N, N, N, N),
	MUX(1, 86, 1, N, N, N, N),
	MUX(1, 87, 1, N, N, N, N),
	MUX(1, 88, 1, N, N, N, N),
	MUX(1, 89, 1, N, N, N, N),
	MUX(1, 90, 1, N, N, N, N),
};

static struct atlas7_grp_mux sd3_9_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd3_9_grp_pad_mux),
	.pad_mux_list = sd3_9_grp_pad_mux,
};

static struct atlas7_pad_mux sd5_grp_pad_mux[] = {
	MUX(1, 91, 1, N, N, N, N),
	MUX(1, 92, 1, N, N, N, N),
	MUX(1, 93, 1, N, N, N, N),
	MUX(1, 94, 1, N, N, N, N),
	MUX(1, 95, 1, N, N, N, N),
	MUX(1, 96, 1, N, N, N, N),
};

static struct atlas7_grp_mux sd5_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sd5_grp_pad_mux),
	.pad_mux_list = sd5_grp_pad_mux,
};

static struct atlas7_pad_mux sd6_grp0_pad_mux[] = {
	MUX(1, 79, 4, 0xa00, 27, 0xa80, 27),
	MUX(1, 78, 4, 0xa00, 26, 0xa80, 26),
	MUX(1, 74, 4, 0xa00, 28, 0xa80, 28),
	MUX(1, 75, 4, 0xa00, 29, 0xa80, 29),
	MUX(1, 76, 4, 0xa00, 30, 0xa80, 30),
	MUX(1, 77, 4, 0xa00, 31, 0xa80, 31),
};

static struct atlas7_grp_mux sd6_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(sd6_grp0_pad_mux),
	.pad_mux_list = sd6_grp0_pad_mux,
};

static struct atlas7_pad_mux sd6_grp1_pad_mux[] = {
	MUX(1, 101, 3, 0xa00, 27, 0xa80, 27),
	MUX(1, 99, 3, 0xa00, 26, 0xa80, 26),
	MUX(1, 100, 3, 0xa00, 28, 0xa80, 28),
	MUX(1, 110, 3, 0xa00, 29, 0xa80, 29),
	MUX(1, 109, 3, 0xa00, 30, 0xa80, 30),
	MUX(1, 111, 3, 0xa00, 31, 0xa80, 31),
};

static struct atlas7_grp_mux sd6_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(sd6_grp1_pad_mux),
	.pad_mux_list = sd6_grp1_pad_mux,
};

static struct atlas7_pad_mux sp0_ext_ldo_on_grp_pad_mux[] = {
	MUX(0, 4, 2, N, N, N, N),
};

static struct atlas7_grp_mux sp0_ext_ldo_on_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sp0_ext_ldo_on_grp_pad_mux),
	.pad_mux_list = sp0_ext_ldo_on_grp_pad_mux,
};

static struct atlas7_pad_mux sp0_qspi_grp_pad_mux[] = {
	MUX(0, 12, 1, N, N, N, N),
	MUX(0, 13, 1, N, N, N, N),
	MUX(0, 14, 1, N, N, N, N),
	MUX(0, 15, 1, N, N, N, N),
	MUX(0, 16, 1, N, N, N, N),
	MUX(0, 17, 1, N, N, N, N),
};

static struct atlas7_grp_mux sp0_qspi_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sp0_qspi_grp_pad_mux),
	.pad_mux_list = sp0_qspi_grp_pad_mux,
};

static struct atlas7_pad_mux sp1_spi_grp_pad_mux[] = {
	MUX(1, 19, 1, N, N, N, N),
	MUX(1, 20, 1, N, N, N, N),
	MUX(1, 21, 1, N, N, N, N),
	MUX(1, 18, 1, N, N, N, N),
};

static struct atlas7_grp_mux sp1_spi_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(sp1_spi_grp_pad_mux),
	.pad_mux_list = sp1_spi_grp_pad_mux,
};

static struct atlas7_pad_mux tpiu_trace_grp_pad_mux[] = {
	MUX(1, 53, 5, N, N, N, N),
	MUX(1, 56, 5, N, N, N, N),
	MUX(1, 57, 5, N, N, N, N),
	MUX(1, 58, 5, N, N, N, N),
	MUX(1, 59, 5, N, N, N, N),
	MUX(1, 60, 5, N, N, N, N),
	MUX(1, 61, 5, N, N, N, N),
	MUX(1, 62, 5, N, N, N, N),
	MUX(1, 63, 5, N, N, N, N),
	MUX(1, 64, 5, N, N, N, N),
	MUX(1, 65, 5, N, N, N, N),
	MUX(1, 66, 5, N, N, N, N),
	MUX(1, 67, 5, N, N, N, N),
	MUX(1, 68, 5, N, N, N, N),
	MUX(1, 69, 5, N, N, N, N),
	MUX(1, 70, 5, N, N, N, N),
	MUX(1, 71, 5, N, N, N, N),
	MUX(1, 72, 5, N, N, N, N),
};

static struct atlas7_grp_mux tpiu_trace_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(tpiu_trace_grp_pad_mux),
	.pad_mux_list = tpiu_trace_grp_pad_mux,
};

static struct atlas7_pad_mux uart0_grp_pad_mux[] = {
	MUX(1, 121, 4, N, N, N, N),
	MUX(1, 120, 4, N, N, N, N),
	MUX(1, 134, 1, N, N, N, N),
	MUX(1, 133, 1, N, N, N, N),
};

static struct atlas7_grp_mux uart0_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(uart0_grp_pad_mux),
	.pad_mux_list = uart0_grp_pad_mux,
};

static struct atlas7_pad_mux uart0_nopause_grp_pad_mux[] = {
	MUX(1, 134, 1, N, N, N, N),
	MUX(1, 133, 1, N, N, N, N),
};

static struct atlas7_grp_mux uart0_nopause_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(uart0_nopause_grp_pad_mux),
	.pad_mux_list = uart0_nopause_grp_pad_mux,
};

static struct atlas7_pad_mux uart1_grp_pad_mux[] = {
	MUX(1, 136, 1, N, N, N, N),
	MUX(1, 135, 1, N, N, N, N),
};

static struct atlas7_grp_mux uart1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(uart1_grp_pad_mux),
	.pad_mux_list = uart1_grp_pad_mux,
};

static struct atlas7_pad_mux uart2_cts_grp0_pad_mux[] = {
	MUX(1, 132, 3, 0xa10, 2, 0xa90, 2),
};

static struct atlas7_grp_mux uart2_cts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_cts_grp0_pad_mux),
	.pad_mux_list = uart2_cts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart2_cts_grp1_pad_mux[] = {
	MUX(1, 162, 2, 0xa10, 2, 0xa90, 2),
};

static struct atlas7_grp_mux uart2_cts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_cts_grp1_pad_mux),
	.pad_mux_list = uart2_cts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart2_rts_grp0_pad_mux[] = {
	MUX(1, 131, 3, N, N, N, N),
};

static struct atlas7_grp_mux uart2_rts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_rts_grp0_pad_mux),
	.pad_mux_list = uart2_rts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart2_rts_grp1_pad_mux[] = {
	MUX(1, 161, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart2_rts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_rts_grp1_pad_mux),
	.pad_mux_list = uart2_rts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart2_rxd_grp0_pad_mux[] = {
	MUX(0, 11, 2, 0xa10, 5, 0xa90, 5),
};

static struct atlas7_grp_mux uart2_rxd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_rxd_grp0_pad_mux),
	.pad_mux_list = uart2_rxd_grp0_pad_mux,
};

static struct atlas7_pad_mux uart2_rxd_grp1_pad_mux[] = {
	MUX(1, 160, 2, 0xa10, 5, 0xa90, 5),
};

static struct atlas7_grp_mux uart2_rxd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_rxd_grp1_pad_mux),
	.pad_mux_list = uart2_rxd_grp1_pad_mux,
};

static struct atlas7_pad_mux uart2_rxd_grp2_pad_mux[] = {
	MUX(1, 130, 3, 0xa10, 5, 0xa90, 5),
};

static struct atlas7_grp_mux uart2_rxd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_rxd_grp2_pad_mux),
	.pad_mux_list = uart2_rxd_grp2_pad_mux,
};

static struct atlas7_pad_mux uart2_txd_grp0_pad_mux[] = {
	MUX(0, 10, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart2_txd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_txd_grp0_pad_mux),
	.pad_mux_list = uart2_txd_grp0_pad_mux,
};

static struct atlas7_pad_mux uart2_txd_grp1_pad_mux[] = {
	MUX(1, 159, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart2_txd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_txd_grp1_pad_mux),
	.pad_mux_list = uart2_txd_grp1_pad_mux,
};

static struct atlas7_pad_mux uart2_txd_grp2_pad_mux[] = {
	MUX(1, 129, 3, N, N, N, N),
};

static struct atlas7_grp_mux uart2_txd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart2_txd_grp2_pad_mux),
	.pad_mux_list = uart2_txd_grp2_pad_mux,
};

static struct atlas7_pad_mux uart3_cts_grp0_pad_mux[] = {
	MUX(1, 125, 2, 0xa08, 0, 0xa88, 0),
};

static struct atlas7_grp_mux uart3_cts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_cts_grp0_pad_mux),
	.pad_mux_list = uart3_cts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart3_cts_grp1_pad_mux[] = {
	MUX(1, 111, 4, 0xa08, 0, 0xa88, 0),
};

static struct atlas7_grp_mux uart3_cts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_cts_grp1_pad_mux),
	.pad_mux_list = uart3_cts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart3_cts_grp2_pad_mux[] = {
	MUX(1, 140, 2, 0xa08, 0, 0xa88, 0),
};

static struct atlas7_grp_mux uart3_cts_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_cts_grp2_pad_mux),
	.pad_mux_list = uart3_cts_grp2_pad_mux,
};

static struct atlas7_pad_mux uart3_rts_grp0_pad_mux[] = {
	MUX(1, 126, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart3_rts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rts_grp0_pad_mux),
	.pad_mux_list = uart3_rts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart3_rts_grp1_pad_mux[] = {
	MUX(1, 109, 4, N, N, N, N),
};

static struct atlas7_grp_mux uart3_rts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rts_grp1_pad_mux),
	.pad_mux_list = uart3_rts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart3_rts_grp2_pad_mux[] = {
	MUX(1, 139, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart3_rts_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rts_grp2_pad_mux),
	.pad_mux_list = uart3_rts_grp2_pad_mux,
};

static struct atlas7_pad_mux uart3_rxd_grp0_pad_mux[] = {
	MUX(1, 138, 1, 0xa00, 5, 0xa80, 5),
};

static struct atlas7_grp_mux uart3_rxd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rxd_grp0_pad_mux),
	.pad_mux_list = uart3_rxd_grp0_pad_mux,
};

static struct atlas7_pad_mux uart3_rxd_grp1_pad_mux[] = {
	MUX(1, 84, 2, 0xa00, 5, 0xa80, 5),
};

static struct atlas7_grp_mux uart3_rxd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rxd_grp1_pad_mux),
	.pad_mux_list = uart3_rxd_grp1_pad_mux,
};

static struct atlas7_pad_mux uart3_rxd_grp2_pad_mux[] = {
	MUX(1, 162, 3, 0xa00, 5, 0xa80, 5),
};

static struct atlas7_grp_mux uart3_rxd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_rxd_grp2_pad_mux),
	.pad_mux_list = uart3_rxd_grp2_pad_mux,
};

static struct atlas7_pad_mux uart3_txd_grp0_pad_mux[] = {
	MUX(1, 137, 1, N, N, N, N),
};

static struct atlas7_grp_mux uart3_txd_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_txd_grp0_pad_mux),
	.pad_mux_list = uart3_txd_grp0_pad_mux,
};

static struct atlas7_pad_mux uart3_txd_grp1_pad_mux[] = {
	MUX(1, 83, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart3_txd_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_txd_grp1_pad_mux),
	.pad_mux_list = uart3_txd_grp1_pad_mux,
};

static struct atlas7_pad_mux uart3_txd_grp2_pad_mux[] = {
	MUX(1, 161, 3, N, N, N, N),
};

static struct atlas7_grp_mux uart3_txd_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart3_txd_grp2_pad_mux),
	.pad_mux_list = uart3_txd_grp2_pad_mux,
};

static struct atlas7_pad_mux uart4_basic_grp_pad_mux[] = {
	MUX(1, 140, 1, N, N, N, N),
	MUX(1, 139, 1, N, N, N, N),
};

static struct atlas7_grp_mux uart4_basic_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_basic_grp_pad_mux),
	.pad_mux_list = uart4_basic_grp_pad_mux,
};

static struct atlas7_pad_mux uart4_cts_grp0_pad_mux[] = {
	MUX(1, 122, 4, 0xa08, 1, 0xa88, 1),
};

static struct atlas7_grp_mux uart4_cts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_cts_grp0_pad_mux),
	.pad_mux_list = uart4_cts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart4_cts_grp1_pad_mux[] = {
	MUX(1, 100, 4, 0xa08, 1, 0xa88, 1),
};

static struct atlas7_grp_mux uart4_cts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_cts_grp1_pad_mux),
	.pad_mux_list = uart4_cts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart4_cts_grp2_pad_mux[] = {
	MUX(1, 117, 2, 0xa08, 1, 0xa88, 1),
};

static struct atlas7_grp_mux uart4_cts_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_cts_grp2_pad_mux),
	.pad_mux_list = uart4_cts_grp2_pad_mux,
};

static struct atlas7_pad_mux uart4_rts_grp0_pad_mux[] = {
	MUX(1, 123, 4, N, N, N, N),
};

static struct atlas7_grp_mux uart4_rts_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_rts_grp0_pad_mux),
	.pad_mux_list = uart4_rts_grp0_pad_mux,
};

static struct atlas7_pad_mux uart4_rts_grp1_pad_mux[] = {
	MUX(1, 99, 4, N, N, N, N),
};

static struct atlas7_grp_mux uart4_rts_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_rts_grp1_pad_mux),
	.pad_mux_list = uart4_rts_grp1_pad_mux,
};

static struct atlas7_pad_mux uart4_rts_grp2_pad_mux[] = {
	MUX(1, 116, 2, N, N, N, N),
};

static struct atlas7_grp_mux uart4_rts_grp2_mux = {
	.pad_mux_count = ARRAY_SIZE(uart4_rts_grp2_pad_mux),
	.pad_mux_list = uart4_rts_grp2_pad_mux,
};

static struct atlas7_pad_mux usb0_drvvbus_grp0_pad_mux[] = {
	MUX(1, 51, 2, N, N, N, N),
};

static struct atlas7_grp_mux usb0_drvvbus_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(usb0_drvvbus_grp0_pad_mux),
	.pad_mux_list = usb0_drvvbus_grp0_pad_mux,
};

static struct atlas7_pad_mux usb0_drvvbus_grp1_pad_mux[] = {
	MUX(1, 162, 7, N, N, N, N),
};

static struct atlas7_grp_mux usb0_drvvbus_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(usb0_drvvbus_grp1_pad_mux),
	.pad_mux_list = usb0_drvvbus_grp1_pad_mux,
};

static struct atlas7_pad_mux usb1_drvvbus_grp0_pad_mux[] = {
	MUX(1, 134, 2, N, N, N, N),
};

static struct atlas7_grp_mux usb1_drvvbus_grp0_mux = {
	.pad_mux_count = ARRAY_SIZE(usb1_drvvbus_grp0_pad_mux),
	.pad_mux_list = usb1_drvvbus_grp0_pad_mux,
};

static struct atlas7_pad_mux usb1_drvvbus_grp1_pad_mux[] = {
	MUX(1, 163, 2, N, N, N, N),
};

static struct atlas7_grp_mux usb1_drvvbus_grp1_mux = {
	.pad_mux_count = ARRAY_SIZE(usb1_drvvbus_grp1_pad_mux),
	.pad_mux_list = usb1_drvvbus_grp1_pad_mux,
};

static struct atlas7_pad_mux visbus_dout_grp_pad_mux[] = {
	MUX(1, 57, 6, N, N, N, N),
	MUX(1, 58, 6, N, N, N, N),
	MUX(1, 59, 6, N, N, N, N),
	MUX(1, 60, 6, N, N, N, N),
	MUX(1, 61, 6, N, N, N, N),
	MUX(1, 62, 6, N, N, N, N),
	MUX(1, 63, 6, N, N, N, N),
	MUX(1, 64, 6, N, N, N, N),
	MUX(1, 65, 6, N, N, N, N),
	MUX(1, 66, 6, N, N, N, N),
	MUX(1, 67, 6, N, N, N, N),
	MUX(1, 68, 6, N, N, N, N),
	MUX(1, 69, 6, N, N, N, N),
	MUX(1, 70, 6, N, N, N, N),
	MUX(1, 71, 6, N, N, N, N),
	MUX(1, 72, 6, N, N, N, N),
	MUX(1, 53, 6, N, N, N, N),
	MUX(1, 54, 6, N, N, N, N),
	MUX(1, 55, 6, N, N, N, N),
	MUX(1, 56, 6, N, N, N, N),
	MUX(1, 85, 6, N, N, N, N),
	MUX(1, 86, 6, N, N, N, N),
	MUX(1, 87, 6, N, N, N, N),
	MUX(1, 88, 6, N, N, N, N),
	MUX(1, 89, 6, N, N, N, N),
	MUX(1, 90, 6, N, N, N, N),
	MUX(1, 91, 6, N, N, N, N),
	MUX(1, 92, 6, N, N, N, N),
	MUX(1, 93, 6, N, N, N, N),
	MUX(1, 94, 6, N, N, N, N),
	MUX(1, 95, 6, N, N, N, N),
	MUX(1, 96, 6, N, N, N, N),
};

static struct atlas7_grp_mux visbus_dout_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(visbus_dout_grp_pad_mux),
	.pad_mux_list = visbus_dout_grp_pad_mux,
};

static struct atlas7_pad_mux vi_vip1_grp_pad_mux[] = {
	MUX(1, 74, 1, N, N, N, N),
	MUX(1, 75, 1, N, N, N, N),
	MUX(1, 76, 1, N, N, N, N),
	MUX(1, 77, 1, N, N, N, N),
	MUX(1, 78, 1, N, N, N, N),
	MUX(1, 79, 1, N, N, N, N),
	MUX(1, 80, 1, N, N, N, N),
	MUX(1, 81, 1, N, N, N, N),
	MUX(1, 82, 1, N, N, N, N),
	MUX(1, 83, 1, N, N, N, N),
	MUX(1, 84, 1, N, N, N, N),
	MUX(1, 103, 2, N, N, N, N),
	MUX(1, 104, 2, N, N, N, N),
	MUX(1, 105, 2, N, N, N, N),
	MUX(1, 106, 2, N, N, N, N),
	MUX(1, 107, 2, N, N, N, N),
	MUX(1, 102, 2, N, N, N, N),
	MUX(1, 97, 2, N, N, N, N),
	MUX(1, 98, 2, N, N, N, N),
};

static struct atlas7_grp_mux vi_vip1_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(vi_vip1_grp_pad_mux),
	.pad_mux_list = vi_vip1_grp_pad_mux,
};

static struct atlas7_pad_mux vi_vip1_ext_grp_pad_mux[] = {
	MUX(1, 74, 1, N, N, N, N),
	MUX(1, 75, 1, N, N, N, N),
	MUX(1, 76, 1, N, N, N, N),
	MUX(1, 77, 1, N, N, N, N),
	MUX(1, 78, 1, N, N, N, N),
	MUX(1, 79, 1, N, N, N, N),
	MUX(1, 80, 1, N, N, N, N),
	MUX(1, 81, 1, N, N, N, N),
	MUX(1, 82, 1, N, N, N, N),
	MUX(1, 83, 1, N, N, N, N),
	MUX(1, 84, 1, N, N, N, N),
	MUX(1, 108, 2, N, N, N, N),
	MUX(1, 103, 2, N, N, N, N),
	MUX(1, 104, 2, N, N, N, N),
	MUX(1, 105, 2, N, N, N, N),
	MUX(1, 106, 2, N, N, N, N),
	MUX(1, 107, 2, N, N, N, N),
	MUX(1, 102, 2, N, N, N, N),
	MUX(1, 97, 2, N, N, N, N),
	MUX(1, 98, 2, N, N, N, N),
	MUX(1, 99, 2, N, N, N, N),
	MUX(1, 100, 2, N, N, N, N),
};

static struct atlas7_grp_mux vi_vip1_ext_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(vi_vip1_ext_grp_pad_mux),
	.pad_mux_list = vi_vip1_ext_grp_pad_mux,
};

static struct atlas7_pad_mux vi_vip1_low8bit_grp_pad_mux[] = {
	MUX(1, 74, 1, N, N, N, N),
	MUX(1, 75, 1, N, N, N, N),
	MUX(1, 76, 1, N, N, N, N),
	MUX(1, 77, 1, N, N, N, N),
	MUX(1, 78, 1, N, N, N, N),
	MUX(1, 79, 1, N, N, N, N),
	MUX(1, 80, 1, N, N, N, N),
	MUX(1, 81, 1, N, N, N, N),
	MUX(1, 82, 1, N, N, N, N),
	MUX(1, 83, 1, N, N, N, N),
	MUX(1, 84, 1, N, N, N, N),
};

static struct atlas7_grp_mux vi_vip1_low8bit_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(vi_vip1_low8bit_grp_pad_mux),
	.pad_mux_list = vi_vip1_low8bit_grp_pad_mux,
};

static struct atlas7_pad_mux vi_vip1_high8bit_grp_pad_mux[] = {
	MUX(1, 82, 1, N, N, N, N),
	MUX(1, 83, 1, N, N, N, N),
	MUX(1, 84, 1, N, N, N, N),
	MUX(1, 103, 2, N, N, N, N),
	MUX(1, 104, 2, N, N, N, N),
	MUX(1, 105, 2, N, N, N, N),
	MUX(1, 106, 2, N, N, N, N),
	MUX(1, 107, 2, N, N, N, N),
	MUX(1, 102, 2, N, N, N, N),
	MUX(1, 97, 2, N, N, N, N),
	MUX(1, 98, 2, N, N, N, N),
};

static struct atlas7_grp_mux vi_vip1_high8bit_grp_mux = {
	.pad_mux_count = ARRAY_SIZE(vi_vip1_high8bit_grp_pad_mux),
	.pad_mux_list = vi_vip1_high8bit_grp_pad_mux,
};

static struct atlas7_pmx_func atlas7_pmx_functions[] = {
	FUNCTION("gnss_gpio", gnss_gpio_grp, &gnss_gpio_grp_mux),
	FUNCTION("lcd_vip_gpio", lcd_vip_gpio_grp, &lcd_vip_gpio_grp_mux),
	FUNCTION("sdio_i2s_gpio", sdio_i2s_gpio_grp, &sdio_i2s_gpio_grp_mux),
	FUNCTION("sp_rgmii_gpio", sp_rgmii_gpio_grp, &sp_rgmii_gpio_grp_mux),
	FUNCTION("lvds_gpio", lvds_gpio_grp, &lvds_gpio_grp_mux),
	FUNCTION("jtag_uart_nand_gpio",
			jtag_uart_nand_gpio_grp,
			&jtag_uart_nand_gpio_grp_mux),
	FUNCTION("rtc_gpio", rtc_gpio_grp, &rtc_gpio_grp_mux),
	FUNCTION("audio_ac97", audio_ac97_grp, &audio_ac97_grp_mux),
	FUNCTION("audio_digmic_m0",
			audio_digmic_grp0,
			&audio_digmic_grp0_mux),
	FUNCTION("audio_digmic_m1",
			audio_digmic_grp1,
			&audio_digmic_grp1_mux),
	FUNCTION("audio_digmic_m2",
			audio_digmic_grp2,
			&audio_digmic_grp2_mux),
	FUNCTION("audio_func_dbg",
			audio_func_dbg_grp,
			&audio_func_dbg_grp_mux),
	FUNCTION("audio_i2s", audio_i2s_grp, &audio_i2s_grp_mux),
	FUNCTION("audio_i2s_2ch", audio_i2s_2ch_grp, &audio_i2s_2ch_grp_mux),
	FUNCTION("audio_i2s_extclk",
			audio_i2s_extclk_grp,
			&audio_i2s_extclk_grp_mux),
	FUNCTION("audio_spdif_out_m0",
			audio_spdif_out_grp0,
			&audio_spdif_out_grp0_mux),
	FUNCTION("audio_spdif_out_m1",
			audio_spdif_out_grp1,
			&audio_spdif_out_grp1_mux),
	FUNCTION("audio_spdif_out_m2",
			audio_spdif_out_grp2,
			&audio_spdif_out_grp2_mux),
	FUNCTION("audio_uart0_basic",
			audio_uart0_basic_grp,
			&audio_uart0_basic_grp_mux),
	FUNCTION("audio_uart0_urfs_m0",
			audio_uart0_urfs_grp0,
			&audio_uart0_urfs_grp0_mux),
	FUNCTION("audio_uart0_urfs_m1",
			audio_uart0_urfs_grp1,
			&audio_uart0_urfs_grp1_mux),
	FUNCTION("audio_uart0_urfs_m2",
			audio_uart0_urfs_grp2,
			&audio_uart0_urfs_grp2_mux),
	FUNCTION("audio_uart0_urfs_m3",
			audio_uart0_urfs_grp3,
			&audio_uart0_urfs_grp3_mux),
	FUNCTION("audio_uart1_basic",
			audio_uart1_basic_grp,
			&audio_uart1_basic_grp_mux),
	FUNCTION("audio_uart1_urfs_m0",
			audio_uart1_urfs_grp0,
			&audio_uart1_urfs_grp0_mux),
	FUNCTION("audio_uart1_urfs_m1",
			audio_uart1_urfs_grp1,
			&audio_uart1_urfs_grp1_mux),
	FUNCTION("audio_uart1_urfs_m2",
			audio_uart1_urfs_grp2,
			&audio_uart1_urfs_grp2_mux),
	FUNCTION("audio_uart2_urfs_m0",
			audio_uart2_urfs_grp0,
			&audio_uart2_urfs_grp0_mux),
	FUNCTION("audio_uart2_urfs_m1",
			audio_uart2_urfs_grp1,
			&audio_uart2_urfs_grp1_mux),
	FUNCTION("audio_uart2_urfs_m2",
			audio_uart2_urfs_grp2,
			&audio_uart2_urfs_grp2_mux),
	FUNCTION("audio_uart2_urxd_m0",
			audio_uart2_urxd_grp0,
			&audio_uart2_urxd_grp0_mux),
	FUNCTION("audio_uart2_urxd_m1",
			audio_uart2_urxd_grp1,
			&audio_uart2_urxd_grp1_mux),
	FUNCTION("audio_uart2_urxd_m2",
			audio_uart2_urxd_grp2,
			&audio_uart2_urxd_grp2_mux),
	FUNCTION("audio_uart2_usclk_m0",
			audio_uart2_usclk_grp0,
			&audio_uart2_usclk_grp0_mux),
	FUNCTION("audio_uart2_usclk_m1",
			audio_uart2_usclk_grp1,
			&audio_uart2_usclk_grp1_mux),
	FUNCTION("audio_uart2_usclk_m2",
			audio_uart2_usclk_grp2,
			&audio_uart2_usclk_grp2_mux),
	FUNCTION("audio_uart2_utfs_m0",
			audio_uart2_utfs_grp0,
			&audio_uart2_utfs_grp0_mux),
	FUNCTION("audio_uart2_utfs_m1",
			audio_uart2_utfs_grp1,
			&audio_uart2_utfs_grp1_mux),
	FUNCTION("audio_uart2_utfs_m2",
			audio_uart2_utfs_grp2,
			&audio_uart2_utfs_grp2_mux),
	FUNCTION("audio_uart2_utxd_m0",
			audio_uart2_utxd_grp0,
			&audio_uart2_utxd_grp0_mux),
	FUNCTION("audio_uart2_utxd_m1",
			audio_uart2_utxd_grp1,
			&audio_uart2_utxd_grp1_mux),
	FUNCTION("audio_uart2_utxd_m2",
			audio_uart2_utxd_grp2,
			&audio_uart2_utxd_grp2_mux),
	FUNCTION("c_can_trnsvr_en_m0",
			c_can_trnsvr_en_grp0,
			&c_can_trnsvr_en_grp0_mux),
	FUNCTION("c_can_trnsvr_en_m1",
			c_can_trnsvr_en_grp1,
			&c_can_trnsvr_en_grp1_mux),
	FUNCTION("c_can_trnsvr_intr",
			c_can_trnsvr_intr_grp,
			&c_can_trnsvr_intr_grp_mux),
	FUNCTION("c_can_trnsvr_stb_n",
			c_can_trnsvr_stb_n_grp,
			&c_can_trnsvr_stb_n_grp_mux),
	FUNCTION("c0_can_rxd_trnsv0",
			c0_can_rxd_trnsv0_grp,
			&c0_can_rxd_trnsv0_grp_mux),
	FUNCTION("c0_can_rxd_trnsv1",
			c0_can_rxd_trnsv1_grp,
			&c0_can_rxd_trnsv1_grp_mux),
	FUNCTION("c0_can_txd_trnsv0",
			c0_can_txd_trnsv0_grp,
			&c0_can_txd_trnsv0_grp_mux),
	FUNCTION("c0_can_txd_trnsv1",
			c0_can_txd_trnsv1_grp,
			&c0_can_txd_trnsv1_grp_mux),
	FUNCTION("c1_can_rxd_m0", c1_can_rxd_grp0, &c1_can_rxd_grp0_mux),
	FUNCTION("c1_can_rxd_m1", c1_can_rxd_grp1, &c1_can_rxd_grp1_mux),
	FUNCTION("c1_can_rxd_m2", c1_can_rxd_grp2, &c1_can_rxd_grp2_mux),
	FUNCTION("c1_can_rxd_m3", c1_can_rxd_grp3, &c1_can_rxd_grp3_mux),
	FUNCTION("c1_can_txd_m0", c1_can_txd_grp0, &c1_can_txd_grp0_mux),
	FUNCTION("c1_can_txd_m1", c1_can_txd_grp1, &c1_can_txd_grp1_mux),
	FUNCTION("c1_can_txd_m2", c1_can_txd_grp2, &c1_can_txd_grp2_mux),
	FUNCTION("c1_can_txd_m3", c1_can_txd_grp3, &c1_can_txd_grp3_mux),
	FUNCTION("ca_audio_lpc", ca_audio_lpc_grp, &ca_audio_lpc_grp_mux),
	FUNCTION("ca_bt_lpc", ca_bt_lpc_grp, &ca_bt_lpc_grp_mux),
	FUNCTION("ca_coex", ca_coex_grp, &ca_coex_grp_mux),
	FUNCTION("ca_curator_lpc",
			ca_curator_lpc_grp,
			&ca_curator_lpc_grp_mux),
	FUNCTION("ca_pcm_debug", ca_pcm_debug_grp, &ca_pcm_debug_grp_mux),
	FUNCTION("ca_pio", ca_pio_grp, &ca_pio_grp_mux),
	FUNCTION("ca_sdio_debug", ca_sdio_debug_grp, &ca_sdio_debug_grp_mux),
	FUNCTION("ca_spi", ca_spi_grp, &ca_spi_grp_mux),
	FUNCTION("ca_trb", ca_trb_grp, &ca_trb_grp_mux),
	FUNCTION("ca_uart_debug", ca_uart_debug_grp, &ca_uart_debug_grp_mux),
	FUNCTION("clkc_m0", clkc_grp0, &clkc_grp0_mux),
	FUNCTION("clkc_m1", clkc_grp1, &clkc_grp1_mux),
	FUNCTION("gn_gnss_i2c", gn_gnss_i2c_grp, &gn_gnss_i2c_grp_mux),
	FUNCTION("gn_gnss_uart_nopause",
			gn_gnss_uart_nopause_grp,
			&gn_gnss_uart_nopause_grp_mux),
	FUNCTION("gn_gnss_uart", gn_gnss_uart_grp, &gn_gnss_uart_grp_mux),
	FUNCTION("gn_trg_spi_m0", gn_trg_spi_grp0, &gn_trg_spi_grp0_mux),
	FUNCTION("gn_trg_spi_m1", gn_trg_spi_grp1, &gn_trg_spi_grp1_mux),
	FUNCTION("cvbs_dbg", cvbs_dbg_grp, &cvbs_dbg_grp_mux),
	FUNCTION("cvbs_dbg_test_m0",
			cvbs_dbg_test_grp0,
			&cvbs_dbg_test_grp0_mux),
	FUNCTION("cvbs_dbg_test_m1",
			cvbs_dbg_test_grp1,
			&cvbs_dbg_test_grp1_mux),
	FUNCTION("cvbs_dbg_test_m2",
			cvbs_dbg_test_grp2,
			&cvbs_dbg_test_grp2_mux),
	FUNCTION("cvbs_dbg_test_m3",
			cvbs_dbg_test_grp3,
			&cvbs_dbg_test_grp3_mux),
	FUNCTION("cvbs_dbg_test_m4",
			cvbs_dbg_test_grp4,
			&cvbs_dbg_test_grp4_mux),
	FUNCTION("cvbs_dbg_test_m5",
			cvbs_dbg_test_grp5,
			&cvbs_dbg_test_grp5_mux),
	FUNCTION("cvbs_dbg_test_m6",
			cvbs_dbg_test_grp6,
			&cvbs_dbg_test_grp6_mux),
	FUNCTION("cvbs_dbg_test_m7",
			cvbs_dbg_test_grp7,
			&cvbs_dbg_test_grp7_mux),
	FUNCTION("cvbs_dbg_test_m8",
			cvbs_dbg_test_grp8,
			&cvbs_dbg_test_grp8_mux),
	FUNCTION("cvbs_dbg_test_m9",
			cvbs_dbg_test_grp9,
			&cvbs_dbg_test_grp9_mux),
	FUNCTION("cvbs_dbg_test_m10",
			cvbs_dbg_test_grp10,
			&cvbs_dbg_test_grp10_mux),
	FUNCTION("cvbs_dbg_test_m11",
			cvbs_dbg_test_grp11,
			&cvbs_dbg_test_grp11_mux),
	FUNCTION("cvbs_dbg_test_m12",
			cvbs_dbg_test_grp12,
			&cvbs_dbg_test_grp12_mux),
	FUNCTION("cvbs_dbg_test_m13",
			cvbs_dbg_test_grp13,
			&cvbs_dbg_test_grp13_mux),
	FUNCTION("cvbs_dbg_test_m14",
			cvbs_dbg_test_grp14,
			&cvbs_dbg_test_grp14_mux),
	FUNCTION("cvbs_dbg_test_m15",
			cvbs_dbg_test_grp15,
			&cvbs_dbg_test_grp15_mux),
	FUNCTION("gn_gnss_power", gn_gnss_power_grp, &gn_gnss_power_grp_mux),
	FUNCTION("gn_gnss_sw_status",
			gn_gnss_sw_status_grp,
			&gn_gnss_sw_status_grp_mux),
	FUNCTION("gn_gnss_eclk", gn_gnss_eclk_grp, &gn_gnss_eclk_grp_mux),
	FUNCTION("gn_gnss_irq1_m0",
			gn_gnss_irq1_grp0,
			&gn_gnss_irq1_grp0_mux),
	FUNCTION("gn_gnss_irq2_m0",
			gn_gnss_irq2_grp0,
			&gn_gnss_irq2_grp0_mux),
	FUNCTION("gn_gnss_tm", gn_gnss_tm_grp, &gn_gnss_tm_grp_mux),
	FUNCTION("gn_gnss_tsync", gn_gnss_tsync_grp, &gn_gnss_tsync_grp_mux),
	FUNCTION("gn_io_gnsssys_sw_cfg",
			gn_io_gnsssys_sw_cfg_grp,
			&gn_io_gnsssys_sw_cfg_grp_mux),
	FUNCTION("gn_trg_m0", gn_trg_grp0, &gn_trg_grp0_mux),
	FUNCTION("gn_trg_m1", gn_trg_grp1, &gn_trg_grp1_mux),
	FUNCTION("gn_trg_shutdown_m0",
			gn_trg_shutdown_grp0,
			&gn_trg_shutdown_grp0_mux),
	FUNCTION("gn_trg_shutdown_m1",
			gn_trg_shutdown_grp1,
			&gn_trg_shutdown_grp1_mux),
	FUNCTION("gn_trg_shutdown_m2",
			gn_trg_shutdown_grp2,
			&gn_trg_shutdown_grp2_mux),
	FUNCTION("gn_trg_shutdown_m3",
			gn_trg_shutdown_grp3,
			&gn_trg_shutdown_grp3_mux),
	FUNCTION("i2c0", i2c0_grp, &i2c0_grp_mux),
	FUNCTION("i2c1", i2c1_grp, &i2c1_grp_mux),
	FUNCTION("i2s0", i2s0_grp, &i2s0_grp_mux),
	FUNCTION("i2s1_basic", i2s1_basic_grp, &i2s1_basic_grp_mux),
	FUNCTION("i2s1_rxd0_m0", i2s1_rxd0_grp0, &i2s1_rxd0_grp0_mux),
	FUNCTION("i2s1_rxd0_m1", i2s1_rxd0_grp1, &i2s1_rxd0_grp1_mux),
	FUNCTION("i2s1_rxd0_m2", i2s1_rxd0_grp2, &i2s1_rxd0_grp2_mux),
	FUNCTION("i2s1_rxd0_m3", i2s1_rxd0_grp3, &i2s1_rxd0_grp3_mux),
	FUNCTION("i2s1_rxd0_m4", i2s1_rxd0_grp4, &i2s1_rxd0_grp4_mux),
	FUNCTION("i2s1_rxd1_m0", i2s1_rxd1_grp0, &i2s1_rxd1_grp0_mux),
	FUNCTION("i2s1_rxd1_m1", i2s1_rxd1_grp1, &i2s1_rxd1_grp1_mux),
	FUNCTION("i2s1_rxd1_m2", i2s1_rxd1_grp2, &i2s1_rxd1_grp2_mux),
	FUNCTION("i2s1_rxd1_m3", i2s1_rxd1_grp3, &i2s1_rxd1_grp3_mux),
	FUNCTION("i2s1_rxd1_m4", i2s1_rxd1_grp4, &i2s1_rxd1_grp4_mux),
	FUNCTION("jtag_jt_dbg_nsrst",
			jtag_jt_dbg_nsrst_grp,
			&jtag_jt_dbg_nsrst_grp_mux),
	FUNCTION("jtag_ntrst_m0", jtag_ntrst_grp0, &jtag_ntrst_grp0_mux),
	FUNCTION("jtag_ntrst_m1", jtag_ntrst_grp1, &jtag_ntrst_grp1_mux),
	FUNCTION("jtag_swdiotms_m0",
			jtag_swdiotms_grp0,
			&jtag_swdiotms_grp0_mux),
	FUNCTION("jtag_swdiotms_m1",
			jtag_swdiotms_grp1,
			&jtag_swdiotms_grp1_mux),
	FUNCTION("jtag_tck_m0", jtag_tck_grp0, &jtag_tck_grp0_mux),
	FUNCTION("jtag_tck_m1", jtag_tck_grp1, &jtag_tck_grp1_mux),
	FUNCTION("jtag_tdi_m0", jtag_tdi_grp0, &jtag_tdi_grp0_mux),
	FUNCTION("jtag_tdi_m1", jtag_tdi_grp1, &jtag_tdi_grp1_mux),
	FUNCTION("jtag_tdo_m0", jtag_tdo_grp0, &jtag_tdo_grp0_mux),
	FUNCTION("jtag_tdo_m1", jtag_tdo_grp1, &jtag_tdo_grp1_mux),
	FUNCTION("ks_kas_spi_m0", ks_kas_spi_grp0, &ks_kas_spi_grp0_mux),
	FUNCTION("ld_ldd", ld_ldd_grp, &ld_ldd_grp_mux),
	FUNCTION("ld_ldd_16bit", ld_ldd_16bit_grp, &ld_ldd_16bit_grp_mux),
	FUNCTION("ld_ldd_fck", ld_ldd_fck_grp, &ld_ldd_fck_grp_mux),
	FUNCTION("ld_ldd_lck", ld_ldd_lck_grp, &ld_ldd_lck_grp_mux),
	FUNCTION("lr_lcdrom", lr_lcdrom_grp, &lr_lcdrom_grp_mux),
	FUNCTION("lvds_analog", lvds_analog_grp, &lvds_analog_grp_mux),
	FUNCTION("nd_df_basic", nd_df_basic_grp, &nd_df_basic_grp_mux),
	FUNCTION("nd_df_wp", nd_df_wp_grp, &nd_df_wp_grp_mux),
	FUNCTION("nd_df_cs", nd_df_cs_grp, &nd_df_cs_grp_mux),
	FUNCTION("ps", ps_grp, &ps_grp_mux),
	FUNCTION("ps_no_dir", ps_no_dir_grp, &ps_no_dir_grp_mux),
	FUNCTION("pwc_core_on", pwc_core_on_grp, &pwc_core_on_grp_mux),
	FUNCTION("pwc_ext_on", pwc_ext_on_grp, &pwc_ext_on_grp_mux),
	FUNCTION("pwc_gpio3_clk", pwc_gpio3_clk_grp, &pwc_gpio3_clk_grp_mux),
	FUNCTION("pwc_io_on", pwc_io_on_grp, &pwc_io_on_grp_mux),
	FUNCTION("pwc_lowbatt_b_m0",
			pwc_lowbatt_b_grp0,
			&pwc_lowbatt_b_grp0_mux),
	FUNCTION("pwc_mem_on", pwc_mem_on_grp, &pwc_mem_on_grp_mux),
	FUNCTION("pwc_on_key_b_m0",
			pwc_on_key_b_grp0,
			&pwc_on_key_b_grp0_mux),
	FUNCTION("pwc_wakeup_src0",
			pwc_wakeup_src0_grp,
			&pwc_wakeup_src0_grp_mux),
	FUNCTION("pwc_wakeup_src1",
			pwc_wakeup_src1_grp,
			&pwc_wakeup_src1_grp_mux),
	FUNCTION("pwc_wakeup_src2",
			pwc_wakeup_src2_grp,
			&pwc_wakeup_src2_grp_mux),
	FUNCTION("pwc_wakeup_src3",
			pwc_wakeup_src3_grp,
			&pwc_wakeup_src3_grp_mux),
	FUNCTION("pw_cko0_m0", pw_cko0_grp0, &pw_cko0_grp0_mux),
	FUNCTION("pw_cko0_m1", pw_cko0_grp1, &pw_cko0_grp1_mux),
	FUNCTION("pw_cko0_m2", pw_cko0_grp2, &pw_cko0_grp2_mux),
	FUNCTION("pw_cko0_m3", pw_cko0_grp3, &pw_cko0_grp3_mux),
	FUNCTION("pw_cko1_m0", pw_cko1_grp0, &pw_cko1_grp0_mux),
	FUNCTION("pw_cko1_m1", pw_cko1_grp1, &pw_cko1_grp1_mux),
	FUNCTION("pw_cko1_m2", pw_cko1_grp2, &pw_cko1_grp2_mux),
	FUNCTION("pw_i2s01_clk_m0",
			pw_i2s01_clk_grp0,
			&pw_i2s01_clk_grp0_mux),
	FUNCTION("pw_i2s01_clk_m1",
			pw_i2s01_clk_grp1,
			&pw_i2s01_clk_grp1_mux),
	FUNCTION("pw_i2s01_clk_m2",
			pw_i2s01_clk_grp2,
			&pw_i2s01_clk_grp2_mux),
	FUNCTION("pw_pwm0_m0", pw_pwm0_grp0, &pw_pwm0_grp0_mux),
	FUNCTION("pw_pwm0_m1", pw_pwm0_grp1, &pw_pwm0_grp1_mux),
	FUNCTION("pw_pwm1_m0", pw_pwm1_grp0, &pw_pwm1_grp0_mux),
	FUNCTION("pw_pwm1_m1", pw_pwm1_grp1, &pw_pwm1_grp1_mux),
	FUNCTION("pw_pwm1_m2", pw_pwm1_grp2, &pw_pwm1_grp2_mux),
	FUNCTION("pw_pwm2_m0", pw_pwm2_grp0, &pw_pwm2_grp0_mux),
	FUNCTION("pw_pwm2_m1", pw_pwm2_grp1, &pw_pwm2_grp1_mux),
	FUNCTION("pw_pwm2_m2", pw_pwm2_grp2, &pw_pwm2_grp2_mux),
	FUNCTION("pw_pwm3_m0", pw_pwm3_grp0, &pw_pwm3_grp0_mux),
	FUNCTION("pw_pwm3_m1", pw_pwm3_grp1, &pw_pwm3_grp1_mux),
	FUNCTION("pw_pwm_cpu_vol_m0",
			pw_pwm_cpu_vol_grp0,
			&pw_pwm_cpu_vol_grp0_mux),
	FUNCTION("pw_pwm_cpu_vol_m1",
			pw_pwm_cpu_vol_grp1,
			&pw_pwm_cpu_vol_grp1_mux),
	FUNCTION("pw_pwm_cpu_vol_m2",
			pw_pwm_cpu_vol_grp2,
			&pw_pwm_cpu_vol_grp2_mux),
	FUNCTION("pw_backlight_m0",
			pw_backlight_grp0,
			&pw_backlight_grp0_mux),
	FUNCTION("pw_backlight_m1",
			pw_backlight_grp1,
			&pw_backlight_grp1_mux),
	FUNCTION("rg_eth_mac", rg_eth_mac_grp, &rg_eth_mac_grp_mux),
	FUNCTION("rg_gmac_phy_intr_n",
			rg_gmac_phy_intr_n_grp,
			&rg_gmac_phy_intr_n_grp_mux),
	FUNCTION("rg_rgmii_mac", rg_rgmii_mac_grp, &rg_rgmii_mac_grp_mux),
	FUNCTION("rg_rgmii_phy_ref_clk_m0",
			rg_rgmii_phy_ref_clk_grp0,
			&rg_rgmii_phy_ref_clk_grp0_mux),
	FUNCTION("rg_rgmii_phy_ref_clk_m1",
			rg_rgmii_phy_ref_clk_grp1,
			&rg_rgmii_phy_ref_clk_grp1_mux),
	FUNCTION("sd0", sd0_grp, &sd0_grp_mux),
	FUNCTION("sd0_4bit", sd0_4bit_grp, &sd0_4bit_grp_mux),
	FUNCTION("sd1", sd1_grp, &sd1_grp_mux),
	FUNCTION("sd1_4bit_m0", sd1_4bit_grp0, &sd1_4bit_grp0_mux),
	FUNCTION("sd1_4bit_m1", sd1_4bit_grp1, &sd1_4bit_grp1_mux),
	FUNCTION("sd2_basic", sd2_basic_grp, &sd2_basic_grp_mux),
	FUNCTION("sd2_cdb_m0", sd2_cdb_grp0, &sd2_cdb_grp0_mux),
	FUNCTION("sd2_cdb_m1", sd2_cdb_grp1, &sd2_cdb_grp1_mux),
	FUNCTION("sd2_wpb_m0", sd2_wpb_grp0, &sd2_wpb_grp0_mux),
	FUNCTION("sd2_wpb_m1", sd2_wpb_grp1, &sd2_wpb_grp1_mux),
	FUNCTION("sd3", sd3_9_grp, &sd3_9_grp_mux),
	FUNCTION("sd5", sd5_grp, &sd5_grp_mux),
	FUNCTION("sd6_m0", sd6_grp0, &sd6_grp0_mux),
	FUNCTION("sd6_m1", sd6_grp1, &sd6_grp1_mux),
	FUNCTION("sd9", sd3_9_grp, &sd3_9_grp_mux),
	FUNCTION("sp0_ext_ldo_on",
			sp0_ext_ldo_on_grp,
			&sp0_ext_ldo_on_grp_mux),
	FUNCTION("sp0_qspi", sp0_qspi_grp, &sp0_qspi_grp_mux),
	FUNCTION("sp1_spi", sp1_spi_grp, &sp1_spi_grp_mux),
	FUNCTION("tpiu_trace", tpiu_trace_grp, &tpiu_trace_grp_mux),
	FUNCTION("uart0", uart0_grp, &uart0_grp_mux),
	FUNCTION("uart0_nopause", uart0_nopause_grp, &uart0_nopause_grp_mux),
	FUNCTION("uart1", uart1_grp, &uart1_grp_mux),
	FUNCTION("uart2_cts_m0", uart2_cts_grp0, &uart2_cts_grp0_mux),
	FUNCTION("uart2_cts_m1", uart2_cts_grp1, &uart2_cts_grp1_mux),
	FUNCTION("uart2_rts_m0", uart2_rts_grp0, &uart2_rts_grp0_mux),
	FUNCTION("uart2_rts_m1", uart2_rts_grp1, &uart2_rts_grp1_mux),
	FUNCTION("uart2_rxd_m0", uart2_rxd_grp0, &uart2_rxd_grp0_mux),
	FUNCTION("uart2_rxd_m1", uart2_rxd_grp1, &uart2_rxd_grp1_mux),
	FUNCTION("uart2_rxd_m2", uart2_rxd_grp2, &uart2_rxd_grp2_mux),
	FUNCTION("uart2_txd_m0", uart2_txd_grp0, &uart2_txd_grp0_mux),
	FUNCTION("uart2_txd_m1", uart2_txd_grp1, &uart2_txd_grp1_mux),
	FUNCTION("uart2_txd_m2", uart2_txd_grp2, &uart2_txd_grp2_mux),
	FUNCTION("uart3_cts_m0", uart3_cts_grp0, &uart3_cts_grp0_mux),
	FUNCTION("uart3_cts_m1", uart3_cts_grp1, &uart3_cts_grp1_mux),
	FUNCTION("uart3_cts_m2", uart3_cts_grp2, &uart3_cts_grp2_mux),
	FUNCTION("uart3_rts_m0", uart3_rts_grp0, &uart3_rts_grp0_mux),
	FUNCTION("uart3_rts_m1", uart3_rts_grp1, &uart3_rts_grp1_mux),
	FUNCTION("uart3_rts_m2", uart3_rts_grp2, &uart3_rts_grp2_mux),
	FUNCTION("uart3_rxd_m0", uart3_rxd_grp0, &uart3_rxd_grp0_mux),
	FUNCTION("uart3_rxd_m1", uart3_rxd_grp1, &uart3_rxd_grp1_mux),
	FUNCTION("uart3_rxd_m2", uart3_rxd_grp2, &uart3_rxd_grp2_mux),
	FUNCTION("uart3_txd_m0", uart3_txd_grp0, &uart3_txd_grp0_mux),
	FUNCTION("uart3_txd_m1", uart3_txd_grp1, &uart3_txd_grp1_mux),
	FUNCTION("uart3_txd_m2", uart3_txd_grp2, &uart3_txd_grp2_mux),
	FUNCTION("uart4_basic", uart4_basic_grp, &uart4_basic_grp_mux),
	FUNCTION("uart4_cts_m0", uart4_cts_grp0, &uart4_cts_grp0_mux),
	FUNCTION("uart4_cts_m1", uart4_cts_grp1, &uart4_cts_grp1_mux),
	FUNCTION("uart4_cts_m2", uart4_cts_grp2, &uart4_cts_grp2_mux),
	FUNCTION("uart4_rts_m0", uart4_rts_grp0, &uart4_rts_grp0_mux),
	FUNCTION("uart4_rts_m1", uart4_rts_grp1, &uart4_rts_grp1_mux),
	FUNCTION("uart4_rts_m2", uart4_rts_grp2, &uart4_rts_grp2_mux),
	FUNCTION("usb0_drvvbus_m0",
			usb0_drvvbus_grp0,
			&usb0_drvvbus_grp0_mux),
	FUNCTION("usb0_drvvbus_m1",
			usb0_drvvbus_grp1,
			&usb0_drvvbus_grp1_mux),
	FUNCTION("usb1_drvvbus_m0",
			usb1_drvvbus_grp0,
			&usb1_drvvbus_grp0_mux),
	FUNCTION("usb1_drvvbus_m1",
			usb1_drvvbus_grp1,
			&usb1_drvvbus_grp1_mux),
	FUNCTION("visbus_dout", visbus_dout_grp, &visbus_dout_grp_mux),
	FUNCTION("vi_vip1", vi_vip1_grp, &vi_vip1_grp_mux),
	FUNCTION("vi_vip1_ext", vi_vip1_ext_grp, &vi_vip1_ext_grp_mux),
	FUNCTION("vi_vip1_low8bit",
			vi_vip1_low8bit_grp,
			&vi_vip1_low8bit_grp_mux),
	FUNCTION("vi_vip1_high8bit",
			vi_vip1_high8bit_grp,
			&vi_vip1_high8bit_grp_mux),
};

static struct atlas7_pinctrl_data atlas7_ioc_data = {
	.pads = (struct pinctrl_pin_desc *)atlas7_ioc_pads,
	.pads_cnt = ARRAY_SIZE(atlas7_ioc_pads),
	.grps = (struct atlas7_pin_group *)altas7_pin_groups,
	.grps_cnt = ARRAY_SIZE(altas7_pin_groups),
	.funcs = (struct atlas7_pmx_func *)atlas7_pmx_functions,
	.funcs_cnt = ARRAY_SIZE(atlas7_pmx_functions),
	.confs = (struct atlas7_pad_config *)atlas7_ioc_pad_confs,
	.confs_cnt = ARRAY_SIZE(atlas7_ioc_pad_confs),
};

/* Simple map data structure */
struct map_data {
	u8 idx;
	u8 data;
};

/**
 * struct atlas7_pull_info - Atlas7 Pad pull info
 * @type:The type of this Pad.
 * @mask:The mas value of this pin's pull bits.
 * @v2s: The map of pull register value to pull status.
 * @s2v: The map of pull status to pull register value.
 */
struct atlas7_pull_info {
	u8 pad_type;
	u8 mask;
	const struct map_data *v2s;
	const struct map_data *s2v;
};

/* Pull Register value map to status */
static const struct map_data p4we_pull_v2s[] = {
	{ P4WE_PULL_UP, PULL_UP },
	{ P4WE_HIGH_HYSTERESIS, HIGH_HYSTERESIS },
	{ P4WE_HIGH_Z, HIGH_Z },
	{ P4WE_PULL_DOWN, PULL_DOWN },
};

static const struct map_data p16st_pull_v2s[] = {
	{ P16ST_PULL_UP, PULL_UP },
	{ PD, PULL_UNKNOWN },
	{ P16ST_HIGH_Z, HIGH_Z },
	{ P16ST_PULL_DOWN, PULL_DOWN },
};

static const struct map_data pm31_pull_v2s[] = {
	{ PM31_PULL_DISABLED, PULL_DOWN },
	{ PM31_PULL_ENABLED, PULL_UP },
};

static const struct map_data pangd_pull_v2s[] = {
	{ PANGD_PULL_UP, PULL_UP },
	{ PD, PULL_UNKNOWN },
	{ PANGD_HIGH_Z, HIGH_Z },
	{ PANGD_PULL_DOWN, PULL_DOWN },
};

/* Pull status map to register value */
static const struct map_data p4we_pull_s2v[] = {
	{ PULL_UP, P4WE_PULL_UP },
	{ HIGH_HYSTERESIS, P4WE_HIGH_HYSTERESIS },
	{ HIGH_Z, P4WE_HIGH_Z },
	{ PULL_DOWN, P4WE_PULL_DOWN },
	{ PULL_DISABLE, -1 },
	{ PULL_ENABLE, -1 },
};

static const struct map_data p16st_pull_s2v[] = {
	{ PULL_UP, P16ST_PULL_UP },
	{ HIGH_HYSTERESIS, -1 },
	{ HIGH_Z, P16ST_HIGH_Z },
	{ PULL_DOWN, P16ST_PULL_DOWN },
	{ PULL_DISABLE, -1 },
	{ PULL_ENABLE, -1 },
};

static const struct map_data pm31_pull_s2v[] = {
	{ PULL_UP, PM31_PULL_ENABLED },
	{ HIGH_HYSTERESIS, -1 },
	{ HIGH_Z, -1 },
	{ PULL_DOWN, PM31_PULL_DISABLED },
	{ PULL_DISABLE, -1 },
	{ PULL_ENABLE, -1 },
};

static const struct map_data pangd_pull_s2v[] = {
	{ PULL_UP, PANGD_PULL_UP },
	{ HIGH_HYSTERESIS, -1 },
	{ HIGH_Z, PANGD_HIGH_Z },
	{ PULL_DOWN, PANGD_PULL_DOWN },
	{ PULL_DISABLE, -1 },
	{ PULL_ENABLE, -1 },
};

static const struct atlas7_pull_info atlas7_pull_map[] = {
	{ PAD_T_4WE_PD, P4WE_PULL_MASK, p4we_pull_v2s, p4we_pull_s2v },
	{ PAD_T_4WE_PU, P4WE_PULL_MASK, p4we_pull_v2s, p4we_pull_s2v },
	{ PAD_T_16ST, P16ST_PULL_MASK, p16st_pull_v2s, p16st_pull_s2v },
	{ PAD_T_M31_0204_PD, PM31_PULL_MASK, pm31_pull_v2s, pm31_pull_s2v },
	{ PAD_T_M31_0204_PU, PM31_PULL_MASK, pm31_pull_v2s, pm31_pull_s2v },
	{ PAD_T_M31_0610_PD, PM31_PULL_MASK, pm31_pull_v2s, pm31_pull_s2v },
	{ PAD_T_M31_0610_PU, PM31_PULL_MASK, pm31_pull_v2s, pm31_pull_s2v },
	{ PAD_T_AD, PANGD_PULL_MASK, pangd_pull_v2s, pangd_pull_s2v },
};

/**
 * struct atlas7_ds_ma_info - Atlas7 Pad DriveStrength & currents info
 * @ma:		The Drive Strength in current value .
 * @ds_16st:	The correspond raw value of 16st pad.
 * @ds_4we:	The correspond raw value of 4we pad.
 * @ds_0204m31:	The correspond raw value of 0204m31 pad.
 * @ds_0610m31:	The correspond raw value of 0610m31 pad.
 */
struct atlas7_ds_ma_info {
	u32 ma;
	u32 ds_16st;
	u32 ds_4we;
	u32 ds_0204m31;
	u32 ds_0610m31;
};

static const struct atlas7_ds_ma_info atlas7_ma2ds_map[] = {
	{ 2, DS_16ST_0, DS_4WE_0, DS_M31_0, DS_NULL },
	{ 4, DS_16ST_1, DS_NULL, DS_M31_1, DS_NULL },
	{ 6, DS_16ST_2, DS_NULL, DS_NULL, DS_M31_0 },
	{ 8, DS_16ST_3, DS_4WE_1, DS_NULL, DS_NULL },
	{ 10, DS_16ST_4, DS_NULL, DS_NULL, DS_M31_1 },
	{ 12, DS_16ST_5, DS_NULL, DS_NULL, DS_NULL },
	{ 14, DS_16ST_6, DS_NULL, DS_NULL, DS_NULL },
	{ 16, DS_16ST_7, DS_4WE_2, DS_NULL, DS_NULL },
	{ 18, DS_16ST_8, DS_NULL, DS_NULL, DS_NULL },
	{ 20, DS_16ST_9, DS_NULL, DS_NULL, DS_NULL },
	{ 22, DS_16ST_10, DS_NULL, DS_NULL, DS_NULL },
	{ 24, DS_16ST_11, DS_NULL, DS_NULL, DS_NULL },
	{ 26, DS_16ST_12, DS_NULL, DS_NULL, DS_NULL },
	{ 28, DS_16ST_13, DS_4WE_3, DS_NULL, DS_NULL },
	{ 30, DS_16ST_14, DS_NULL, DS_NULL, DS_NULL },
	{ 32, DS_16ST_15, DS_NULL, DS_NULL, DS_NULL },
};

/**
 * struct atlas7_ds_info - Atlas7 Pad DriveStrength info
 * @type:		The type of this Pad.
 * @mask:		The mask value of this pin's pull bits.
 * @imval:		The immediate value of drives trength register.
 */
struct atlas7_ds_info {
	u8 type;
	u8 mask;
	u8 imval;
	u8 reserved;
};

static const struct atlas7_ds_info atlas7_ds_map[] = {
	{ PAD_T_4WE_PD, DS_2BIT_MASK, DS_2BIT_IM_VAL },
	{ PAD_T_4WE_PU, DS_2BIT_MASK, DS_2BIT_IM_VAL },
	{ PAD_T_16ST, DS_4BIT_MASK, DS_4BIT_IM_VAL },
	{ PAD_T_M31_0204_PD, DS_1BIT_MASK, DS_1BIT_IM_VAL },
	{ PAD_T_M31_0204_PU, DS_1BIT_MASK, DS_1BIT_IM_VAL },
	{ PAD_T_M31_0610_PD, DS_1BIT_MASK, DS_1BIT_IM_VAL },
	{ PAD_T_M31_0610_PU, DS_1BIT_MASK, DS_1BIT_IM_VAL },
	{ PAD_T_AD, DS_NULL, DS_NULL },
};

static inline u32 atlas7_pin_to_bank(u32 pin)
{
	return (pin >= ATLAS7_PINCTRL_BANK_0_PINS) ? 1 : 0;
}

static int atlas7_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pctl_data->funcs_cnt;
}

static const char *atlas7_pmx_get_func_name(struct pinctrl_dev *pctldev,
					u32 selector)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pctl_data->funcs[selector].name;
}

static int atlas7_pmx_get_func_groups(struct pinctrl_dev *pctldev,
		u32 selector, const char * const **groups,
		u32 * const num_groups)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmx->pctl_data->funcs[selector].groups;
	*num_groups = pmx->pctl_data->funcs[selector].num_groups;

	return 0;
}

static void __atlas7_pmx_pin_input_disable_set(struct atlas7_pmx *pmx,
				const struct atlas7_pad_mux *mux)
{
	/* Set Input Disable to avoid input glitches
	 *
	 * All Input-Disable Control registers are located on IOCRTC.
	 * So the regs bank is always 0.
	 *
	 */
	if (mux->dinput_reg && mux->dinput_val_reg) {
		writel(DI_MASK << mux->dinput_bit,
			pmx->regs[BANK_DS] + CLR_REG(mux->dinput_reg));
		writel(DI_DISABLE << mux->dinput_bit,
			pmx->regs[BANK_DS] + mux->dinput_reg);


		writel(DIV_MASK << mux->dinput_val_bit,
			pmx->regs[BANK_DS] + CLR_REG(mux->dinput_val_reg));
		writel(DIV_DISABLE << mux->dinput_val_bit,
			pmx->regs[BANK_DS] + mux->dinput_val_reg);
	}
}

static void __atlas7_pmx_pin_input_disable_clr(struct atlas7_pmx *pmx,
				const struct atlas7_pad_mux *mux)
{
	/* Clear Input Disable to avoid input glitches */
	if (mux->dinput_reg && mux->dinput_val_reg) {
		writel(DI_MASK << mux->dinput_bit,
			pmx->regs[BANK_DS] + CLR_REG(mux->dinput_reg));
		writel(DI_ENABLE << mux->dinput_bit,
			pmx->regs[BANK_DS] + mux->dinput_reg);

		writel(DIV_MASK << mux->dinput_val_bit,
			pmx->regs[BANK_DS] + CLR_REG(mux->dinput_val_reg));
		writel(DIV_ENABLE << mux->dinput_val_bit,
			pmx->regs[BANK_DS] + mux->dinput_val_reg);
	}
}

static int __atlas7_pmx_pin_ad_sel(struct atlas7_pmx *pmx,
			struct atlas7_pad_config *conf,
			u32 bank, u32 ad_sel)
{
	unsigned long regv;

	/* Write to clear register to clear A/D selector */
	writel(ANA_CLEAR_MASK << conf->ad_ctrl_bit,
		pmx->regs[bank] + CLR_REG(conf->ad_ctrl_reg));

	/* Set target pad A/D selector */
	regv = readl(pmx->regs[bank] + conf->ad_ctrl_reg);
	regv &= ~(ANA_CLEAR_MASK << conf->ad_ctrl_bit);
	writel(regv | (ad_sel << conf->ad_ctrl_bit),
			pmx->regs[bank] + conf->ad_ctrl_reg);

	regv = readl(pmx->regs[bank] + conf->ad_ctrl_reg);
	pr_debug("bank:%d reg:0x%04x val:0x%08lx\n",
			bank, conf->ad_ctrl_reg, regv);
	return 0;
}

static int  __atlas7_pmx_pin_analog_enable(struct atlas7_pmx *pmx,
			struct atlas7_pad_config *conf, u32 bank)
{
	/* Only PAD_T_AD pins can change between Analogue&Digital */
	if (conf->type != PAD_T_AD)
		return -EINVAL;

	return __atlas7_pmx_pin_ad_sel(pmx, conf, bank, 0);
}

static int __atlas7_pmx_pin_digital_enable(struct atlas7_pmx *pmx,
			struct atlas7_pad_config *conf, u32 bank)
{
	/* Other type pads are always digital */
	if (conf->type != PAD_T_AD)
		return 0;

	return __atlas7_pmx_pin_ad_sel(pmx, conf, bank, 1);
}

static int __atlas7_pmx_pin_enable(struct atlas7_pmx *pmx,
				u32 pin, u32 func)
{
	struct atlas7_pad_config *conf;
	u32 bank;
	int ret;
	unsigned long regv;

	pr_debug("PMX DUMP ### pin#%d func:%d #### START >>>\n",
			pin, func);

	/* Get this Pad's descriptor from PINCTRL */
	conf = &pmx->pctl_data->confs[pin];
	bank = atlas7_pin_to_bank(pin);

	/* Just enable the analog function of this pad */
	if (FUNC_ANALOGUE == func) {
		ret = __atlas7_pmx_pin_analog_enable(pmx, conf, bank);
		if (ret)
			dev_err(pmx->dev,
				"Convert pad#%d to analog failed, ret=%d\n",
				pin, ret);
		return ret;
	}

	/* Set Pads from analog to digital */
	ret = __atlas7_pmx_pin_digital_enable(pmx, conf, bank);
	if (ret) {
		dev_err(pmx->dev,
			"Convert pad#%d to digital failed, ret=%d\n",
			pin, ret);
		return ret;
	}

	/* Write to clear register to clear current function */
	writel(FUNC_CLEAR_MASK << conf->mux_bit,
		pmx->regs[bank] + CLR_REG(conf->mux_reg));

	/* Set target pad mux function */
	regv = readl(pmx->regs[bank] + conf->mux_reg);
	regv &= ~(FUNC_CLEAR_MASK << conf->mux_bit);
	writel(regv | (func << conf->mux_bit),
			pmx->regs[bank] + conf->mux_reg);

	regv = readl(pmx->regs[bank] + conf->mux_reg);
	pr_debug("bank:%d reg:0x%04x val:0x%08lx\n",
		bank, conf->mux_reg, regv);

	return 0;
}

static int atlas7_pmx_set_mux(struct pinctrl_dev *pctldev,
			u32 func_selector, u32 group_selector)
{
	int idx, ret;
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct atlas7_pmx_func *pmx_func;
	struct atlas7_pin_group *pin_grp;
	const struct atlas7_grp_mux *grp_mux;
	const struct atlas7_pad_mux *mux;

	pmx_func = &pmx->pctl_data->funcs[func_selector];
	pin_grp = &pmx->pctl_data->grps[group_selector];

	pr_debug("PMX DUMP ### Function:[%s] Group:[%s] #### START >>>\n",
			pmx_func->name, pin_grp->name);

	/* the sd3 and sd9 pin select by SYS2PCI_SDIO9SEL register */
	if (pin_grp->pins == (unsigned int *)&sd3_9_pins) {
		if (!strcmp(pmx_func->name, "sd9"))
			writel(1, pmx->sys2pci_base + SYS2PCI_SDIO9SEL);
		else
			writel(0, pmx->sys2pci_base + SYS2PCI_SDIO9SEL);
	}

	grp_mux = pmx_func->grpmux;

	for (idx = 0; idx < grp_mux->pad_mux_count; idx++) {
		mux = &grp_mux->pad_mux_list[idx];
		__atlas7_pmx_pin_input_disable_set(pmx, mux);
		ret = __atlas7_pmx_pin_enable(pmx, mux->pin, mux->func);
		if (ret) {
			dev_err(pmx->dev,
				"FUNC:%s GRP:%s PIN#%d.%d failed, ret=%d\n",
				pmx_func->name, pin_grp->name,
				mux->pin, mux->func, ret);
			BUG_ON(1);
		}
		__atlas7_pmx_pin_input_disable_clr(pmx, mux);
	}
	pr_debug("PMX DUMP ### Function:[%s] Group:[%s] #### END <<<\n",
			pmx_func->name, pin_grp->name);

	return 0;
}

static u32 convert_current_to_drive_strength(u32 type, u32 ma)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(atlas7_ma2ds_map); idx++) {
		if (atlas7_ma2ds_map[idx].ma != ma)
			continue;

		if (type == PAD_T_4WE_PD || type == PAD_T_4WE_PU)
			return atlas7_ma2ds_map[idx].ds_4we;
		else if (type == PAD_T_16ST)
			return atlas7_ma2ds_map[idx].ds_16st;
		else if (type == PAD_T_M31_0204_PD || type == PAD_T_M31_0204_PU)
			return atlas7_ma2ds_map[idx].ds_0204m31;
		else if (type == PAD_T_M31_0610_PD || type == PAD_T_M31_0610_PU)
			return atlas7_ma2ds_map[idx].ds_0610m31;
	}

	return DS_NULL;
}

static int altas7_pinctrl_set_pull_sel(struct pinctrl_dev *pctldev,
					u32 pin, u32 sel)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct atlas7_pad_config *conf = &pmx->pctl_data->confs[pin];
	const struct atlas7_pull_info *pull_info;
	u32 bank;
	unsigned long regv;
	void __iomem *pull_sel_reg;

	bank = atlas7_pin_to_bank(pin);
	pull_info = &atlas7_pull_map[conf->type];
	pull_sel_reg = pmx->regs[bank] + conf->pupd_reg;

	/* Retrieve correspond register value from table by sel */
	regv = pull_info->s2v[sel].data & pull_info->mask;

	/* Clear & Set new value to pull register */
	writel(pull_info->mask << conf->pupd_bit, CLR_REG(pull_sel_reg));
	writel(regv << conf->pupd_bit, pull_sel_reg);

	pr_debug("PIN_CFG ### SET PIN#%d PULL SELECTOR:%d == OK ####\n",
		pin, sel);
	return 0;
}

static int __altas7_pinctrl_set_drive_strength_sel(struct pinctrl_dev *pctldev,
						u32 pin, u32 sel)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct atlas7_pad_config *conf = &pmx->pctl_data->confs[pin];
	const struct atlas7_ds_info *ds_info;
	u32 bank;
	void __iomem *ds_sel_reg;

	ds_info = &atlas7_ds_map[conf->type];
	if (sel & (~(ds_info->mask)))
		goto unsupport;

	bank = atlas7_pin_to_bank(pin);
	ds_sel_reg = pmx->regs[bank] + conf->drvstr_reg;

	writel(ds_info->imval << conf->drvstr_bit, CLR_REG(ds_sel_reg));
	writel(sel << conf->drvstr_bit, ds_sel_reg);

	return 0;

unsupport:
	pr_err("Pad#%d type[%d] doesn't support ds code[%d]!\n",
		pin, conf->type, sel);
	return -ENOTSUPP;
}

static int altas7_pinctrl_set_drive_strength_sel(struct pinctrl_dev *pctldev,
						u32 pin, u32 ma)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct atlas7_pad_config *conf = &pmx->pctl_data->confs[pin];
	u32 type = conf->type;
	u32 sel;
	int ret;

	sel = convert_current_to_drive_strength(conf->type, ma);
	if (DS_NULL == sel) {
		pr_err("Pad#%d type[%d] doesn't support ds current[%d]!\n",
		pin, type, ma);
		return -ENOTSUPP;
	}

	ret =  __altas7_pinctrl_set_drive_strength_sel(pctldev,
						pin, sel);
	pr_debug("PIN_CFG ### SET PIN#%d DS:%d MA:%d == %s ####\n",
		pin, sel, ma, ret?"FAILED":"OK");
	return ret;
}

static int atlas7_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, u32 pin)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	u32 idx;

	dev_dbg(pmx->dev,
		"atlas7_pmx_gpio_request_enable: pin=%d\n", pin);
	for (idx = 0; idx < range->npins; idx++) {
		if (pin == range->pins[idx])
			break;
	}

	if (idx >= range->npins) {
		dev_err(pmx->dev,
			"The pin#%d could not be requested as GPIO!!\n",
			pin);
		return -EPERM;
	}

	__atlas7_pmx_pin_enable(pmx, pin, FUNC_GPIO);

	return 0;
}

static const struct pinmux_ops atlas7_pinmux_ops = {
	.get_functions_count = atlas7_pmx_get_funcs_count,
	.get_function_name = atlas7_pmx_get_func_name,
	.get_function_groups = atlas7_pmx_get_func_groups,
	.set_mux = atlas7_pmx_set_mux,
	.gpio_request_enable = atlas7_pmx_gpio_request_enable,
};

static int atlas7_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pctl_data->grps_cnt;
}

static const char *atlas7_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						u32 group)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pctl_data->grps[group].name;
}

static int atlas7_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		u32 group, const u32 **pins, u32 *num_pins)
{
	struct atlas7_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*num_pins = pmx->pctl_data->grps[group].num_pins;
	*pins = pmx->pctl_data->grps[group].pins;

	return 0;
}

static int atlas7_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np_config,
					struct pinctrl_map **map,
					u32 *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map,
				num_maps, PIN_MAP_TYPE_INVALID);
}

static void atlas7_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map *map, u32 num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops atlas7_pinctrl_ops = {
	.get_groups_count = atlas7_pinctrl_get_groups_count,
	.get_group_name = atlas7_pinctrl_get_group_name,
	.get_group_pins = atlas7_pinctrl_get_group_pins,
	.dt_node_to_map = atlas7_pinctrl_dt_node_to_map,
	.dt_free_map = atlas7_pinctrl_dt_free_map,
};

static int atlas7_pin_config_set(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *configs,
				unsigned num_configs)
{
	u16 param;
	u32 arg;
	int idx, err;

	for (idx = 0; idx < num_configs; idx++) {
		param = pinconf_to_config_param(configs[idx]);
		arg = pinconf_to_config_argument(configs[idx]);

		pr_debug("PMX CFG###### ATLAS7 PIN#%d [%s] CONFIG PARAM:%d ARG:%d >>>>>\n",
			pin, atlas7_ioc_pads[pin].name, param, arg);
		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			err = altas7_pinctrl_set_pull_sel(pctldev,
							pin, PULL_UP);
			if (err)
				return err;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			err = altas7_pinctrl_set_pull_sel(pctldev,
							pin, PULL_DOWN);
			if (err)
				return err;
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			err = altas7_pinctrl_set_pull_sel(pctldev,
							pin, HIGH_HYSTERESIS);
			if (err)
				return err;
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			err = altas7_pinctrl_set_pull_sel(pctldev,
							pin, HIGH_Z);
			if (err)
				return err;
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			err = altas7_pinctrl_set_drive_strength_sel(pctldev,
							pin, arg);
			if (err)
				return err;
			break;
		default:
			return -ENOTSUPP;
		}
		pr_debug("PMX CFG###### ATLAS7 PIN#%d [%s] CONFIG PARAM:%d ARG:%d <<<<\n",
			pin, atlas7_ioc_pads[pin].name, param, arg);
	}

	return 0;
}

static int atlas7_pin_config_group_set(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *configs,
				unsigned num_configs)
{
	const unsigned *pins;
	unsigned npins;
	int i, ret;

	ret = atlas7_pinctrl_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;
	for (i = 0; i < npins; i++) {
		if (atlas7_pin_config_set(pctldev, pins[i],
					  configs, num_configs))
			return -ENOTSUPP;
	}
	return 0;
}

static const struct pinconf_ops atlas7_pinconf_ops = {
	.pin_config_set = atlas7_pin_config_set,
	.pin_config_group_set = atlas7_pin_config_group_set,
	.is_generic = true,
};

static int atlas7_pinmux_probe(struct platform_device *pdev)
{
	int ret, idx;
	struct atlas7_pmx *pmx;
	struct device_node *np = pdev->dev.of_node;
	u32 banks = ATLAS7_PINCTRL_REG_BANKS;
	struct device_node *sys2pci_np;
	struct resource res;

	/* Create state holders etc for this driver */
	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx)
		return -ENOMEM;

	/* The sd3 and sd9 shared all pins, and the function select by
	 * SYS2PCI_SDIO9SEL register
	 */
	sys2pci_np = of_find_node_by_name(NULL, "sys2pci");
	if (!sys2pci_np)
		return -EINVAL;

	ret = of_address_to_resource(sys2pci_np, 0, &res);
	of_node_put(sys2pci_np);
	if (ret)
		return ret;

	pmx->sys2pci_base = devm_ioremap_resource(&pdev->dev, &res);
	if (IS_ERR(pmx->sys2pci_base))
		return -ENOMEM;

	pmx->dev = &pdev->dev;

	pmx->pctl_data = &atlas7_ioc_data;
	pmx->pctl_desc.name = "pinctrl-atlas7";
	pmx->pctl_desc.pins = pmx->pctl_data->pads;
	pmx->pctl_desc.npins = pmx->pctl_data->pads_cnt;
	pmx->pctl_desc.pctlops = &atlas7_pinctrl_ops;
	pmx->pctl_desc.pmxops = &atlas7_pinmux_ops;
	pmx->pctl_desc.confops = &atlas7_pinconf_ops;

	for (idx = 0; idx < banks; idx++) {
		pmx->regs[idx] = of_iomap(np, idx);
		if (!pmx->regs[idx]) {
			dev_err(&pdev->dev,
				"can't map ioc bank#%d registers\n", idx);
			ret = -ENOMEM;
			goto unmap_io;
		}
	}

	/* Now register the pin controller and all pins it handles */
	pmx->pctl = pinctrl_register(&pmx->pctl_desc, &pdev->dev, pmx);
	if (IS_ERR(pmx->pctl)) {
		dev_err(&pdev->dev, "could not register atlas7 pinmux driver\n");
		ret = PTR_ERR(pmx->pctl);
		goto unmap_io;
	}

	platform_set_drvdata(pdev, pmx);

	dev_info(&pdev->dev, "initialized atlas7 pinmux driver\n");

	return 0;

unmap_io:
	for (idx = 0; idx < banks; idx++) {
		if (!pmx->regs[idx])
			break;
		iounmap(pmx->regs[idx]);
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_pinmux_suspend_noirq(struct device *dev)
{
	struct atlas7_pmx *pmx = dev_get_drvdata(dev);
	struct atlas7_pad_status *status;
	struct atlas7_pad_config *conf;
	const struct atlas7_ds_info *ds_info;
	const struct atlas7_pull_info *pull_info;
	int idx;
	u32 bank;
	unsigned long regv;

	for (idx = 0; idx < pmx->pctl_desc.npins; idx++) {
		/* Get this Pad's descriptor from PINCTRL */
		conf = &pmx->pctl_data->confs[idx];
		bank = atlas7_pin_to_bank(idx);
		status = &pmx->sleep_data[idx];

		/* Save Function selector */
		regv = readl(pmx->regs[bank] + conf->mux_reg);
		status->func = (regv >> conf->mux_bit) & FUNC_CLEAR_MASK;

		/* Check if Pad is in Analogue selector */
		if (conf->ad_ctrl_reg == -1)
			goto save_ds_sel;

		regv = readl(pmx->regs[bank] + conf->ad_ctrl_reg);
		if (!(regv & (conf->ad_ctrl_bit << ANA_CLEAR_MASK)))
			status->func = FUNC_ANALOGUE;

save_ds_sel:
		if (conf->drvstr_reg == -1)
			goto save_pull_sel;

		/* Save Drive Strength selector */
		ds_info = &atlas7_ds_map[conf->type];
		regv = readl(pmx->regs[bank] + conf->drvstr_reg);
		status->dstr = (regv >> conf->drvstr_bit) & ds_info->mask;

save_pull_sel:
		/* Save Pull selector */
		pull_info = &atlas7_pull_map[conf->type];
		regv = readl(pmx->regs[bank] + conf->pupd_reg);
		regv = (regv >> conf->pupd_bit) & pull_info->mask;
		status->pull = pull_info->v2s[regv].data;
	}

	/*
	 * Save disable input selector, this selector is not for Pin,
	 * but for Mux function.
	 */
	for (idx = 0; idx < NUM_OF_IN_DISABLE_REG; idx++) {
		pmx->status_ds[idx] = readl(pmx->regs[BANK_DS] +
					IN_DISABLE_0_REG_SET + 0x8 * idx);
		pmx->status_dsv[idx] = readl(pmx->regs[BANK_DS] +
					IN_DISABLE_VAL_0_REG_SET + 0x8 * idx);
	}

	return 0;
}

static int atlas7_pinmux_resume_noirq(struct device *dev)
{
	struct atlas7_pmx *pmx = dev_get_drvdata(dev);
	struct atlas7_pad_status *status;
	int idx;

	for (idx = 0; idx < pmx->pctl_desc.npins; idx++) {
		/* Get this Pad's descriptor from PINCTRL */
		status = &pmx->sleep_data[idx];

		/* Restore Function selector */
		__atlas7_pmx_pin_enable(pmx, idx, (u32)status->func & 0xff);

		if (FUNC_ANALOGUE == status->func)
			goto restore_pull_sel;

		/* Restore Drive Strength selector */
		__altas7_pinctrl_set_drive_strength_sel(pmx->pctl, idx,
						(u32)status->dstr & 0xff);

restore_pull_sel:
		/* Restore Pull selector */
		altas7_pinctrl_set_pull_sel(pmx->pctl, idx,
						(u32)status->pull & 0xff);
	}

	/*
	 * Restore disable input selector, this selector is not for Pin,
	 * but for Mux function
	 */
	for (idx = 0; idx < NUM_OF_IN_DISABLE_REG; idx++) {
		writel(~0, pmx->regs[BANK_DS] +
					IN_DISABLE_0_REG_CLR + 0x8 * idx);
		writel(pmx->status_ds[idx], pmx->regs[BANK_DS] +
					IN_DISABLE_0_REG_SET + 0x8 * idx);
		writel(~0, pmx->regs[BANK_DS] +
					IN_DISABLE_VAL_0_REG_CLR + 0x8 * idx);
		writel(pmx->status_dsv[idx], pmx->regs[BANK_DS] +
					IN_DISABLE_VAL_0_REG_SET + 0x8 * idx);
	}

	return 0;
}

static const struct dev_pm_ops atlas7_pinmux_pm_ops = {
	.suspend_noirq = atlas7_pinmux_suspend_noirq,
	.resume_noirq = atlas7_pinmux_resume_noirq,
	.freeze_noirq = atlas7_pinmux_suspend_noirq,
	.restore_noirq = atlas7_pinmux_resume_noirq,
};
#endif

static const struct of_device_id atlas7_pinmux_ids[] = {
	{ .compatible = "sirf,atlas7-ioc",},
	{},
};

static struct platform_driver atlas7_pinmux_driver = {
	.driver = {
		.name = "atlas7-ioc",
		.of_match_table = atlas7_pinmux_ids,
#ifdef CONFIG_PM_SLEEP
		.pm = &atlas7_pinmux_pm_ops,
#endif
	},
	.probe = atlas7_pinmux_probe,
};

static int __init atlas7_pinmux_init(void)
{
	return platform_driver_register(&atlas7_pinmux_driver);
}
arch_initcall(atlas7_pinmux_init);


/**
 * The Following is GPIO Code
 */
static inline struct
atlas7_gpio_bank *atlas7_gpio_to_bank(struct atlas7_gpio_chip *a7gc, u32 gpio)
{
	return &a7gc->banks[GPIO_TO_BANK(gpio)];
}

static int __atlas7_gpio_to_pin(struct atlas7_gpio_chip *a7gc, u32 gpio)
{
	struct atlas7_gpio_bank *bank;
	u32 ofs;

	bank = atlas7_gpio_to_bank(a7gc, gpio);
	ofs = gpio - bank->gpio_offset;
	if (ofs >= bank->ngpio)
		return -ENODEV;

	return bank->gpio_pins[ofs];
}

static void atlas7_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(gc);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 val, pin_in_bank;
	unsigned long flags;

	bank = atlas7_gpio_to_bank(a7gc, d->hwirq);
	pin_in_bank = d->hwirq - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	val = readl(ctrl_reg);
	/* clear interrupt status */
	writel(val, ctrl_reg);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);
}

static void __atlas7_gpio_irq_mask(struct atlas7_gpio_chip *a7gc, int idx)
{
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 val, pin_in_bank;

	bank = atlas7_gpio_to_bank(a7gc, idx);
	pin_in_bank = idx - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	val = readl(ctrl_reg);
	val &= ~(ATLAS7_GPIO_CTL_INTR_EN_MASK |
		ATLAS7_GPIO_CTL_INTR_STATUS_MASK);
	writel(val, ctrl_reg);
}

static void atlas7_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	__atlas7_gpio_irq_mask(a7gc, d->hwirq);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);
}

static void atlas7_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(gc);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 val, pin_in_bank;
	unsigned long flags;

	bank = atlas7_gpio_to_bank(a7gc, d->hwirq);
	pin_in_bank = d->hwirq - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	val = readl(ctrl_reg);
	val &= ~ATLAS7_GPIO_CTL_INTR_STATUS_MASK;
	val |= ATLAS7_GPIO_CTL_INTR_EN_MASK;
	writel(val, ctrl_reg);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);
}

static int atlas7_gpio_irq_type(struct irq_data *d,
				unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(gc);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 val, pin_in_bank;
	unsigned long flags;

	bank = atlas7_gpio_to_bank(a7gc, d->hwirq);
	pin_in_bank = d->hwirq - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	val = readl(ctrl_reg);
	val &= ~(ATLAS7_GPIO_CTL_INTR_STATUS_MASK |
		ATLAS7_GPIO_CTL_INTR_EN_MASK);

	switch (type) {
	case IRQ_TYPE_NONE:
		break;

	case IRQ_TYPE_EDGE_RISING:
		val |= ATLAS7_GPIO_CTL_INTR_HIGH_MASK |
			ATLAS7_GPIO_CTL_INTR_TYPE_MASK;
		val &= ~ATLAS7_GPIO_CTL_INTR_LOW_MASK;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		val &= ~ATLAS7_GPIO_CTL_INTR_HIGH_MASK;
		val |= ATLAS7_GPIO_CTL_INTR_LOW_MASK |
			ATLAS7_GPIO_CTL_INTR_TYPE_MASK;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		val |= ATLAS7_GPIO_CTL_INTR_HIGH_MASK |
			ATLAS7_GPIO_CTL_INTR_LOW_MASK |
			ATLAS7_GPIO_CTL_INTR_TYPE_MASK;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		val &= ~(ATLAS7_GPIO_CTL_INTR_HIGH_MASK |
			ATLAS7_GPIO_CTL_INTR_TYPE_MASK);
		val |= ATLAS7_GPIO_CTL_INTR_LOW_MASK;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		val |= ATLAS7_GPIO_CTL_INTR_HIGH_MASK;
		val &= ~(ATLAS7_GPIO_CTL_INTR_LOW_MASK |
			ATLAS7_GPIO_CTL_INTR_TYPE_MASK);
		break;
	}

	writel(val, ctrl_reg);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	return 0;
}

static struct irq_chip atlas7_gpio_irq_chip = {
	.name = "atlas7-gpio-irq",
	.irq_ack = atlas7_gpio_irq_ack,
	.irq_mask = atlas7_gpio_irq_mask,
	.irq_unmask = atlas7_gpio_irq_unmask,
	.irq_set_type = atlas7_gpio_irq_type,
};

static void atlas7_gpio_handle_irq(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(gc);
	struct atlas7_gpio_bank *bank = NULL;
	u32 status, ctrl;
	int pin_in_bank = 0, idx;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irq = irq_desc_get_irq(desc);

	for (idx = 0; idx < a7gc->nbank; idx++) {
		bank = &a7gc->banks[idx];
		if (bank->irq == irq)
			break;
	}
	BUG_ON(idx == a7gc->nbank);

	chained_irq_enter(chip, desc);

	status = readl(ATLAS7_GPIO_INT_STATUS(bank));
	if (!status) {
		pr_warn("%s: gpio [%s] status %#x no interrupt is flagged\n",
			__func__, gc->label, status);
		handle_bad_irq(desc);
		return;
	}

	while (status) {
		ctrl = readl(ATLAS7_GPIO_CTRL(bank, pin_in_bank));

		/*
		 * Here we must check whether the corresponding GPIO's
		 * interrupt has been enabled, otherwise just skip it
		 */
		if ((status & 0x1) && (ctrl & ATLAS7_GPIO_CTL_INTR_EN_MASK)) {
			pr_debug("%s: chip[%s] gpio:%d happens\n",
				__func__, gc->label,
				bank->gpio_offset + pin_in_bank);
			generic_handle_irq(
				irq_find_mapping(gc->irq.domain,
					bank->gpio_offset + pin_in_bank));
		}

		if (++pin_in_bank >= bank->ngpio)
			break;

		status = status >> 1;
	}

	chained_irq_exit(chip, desc);
}

static void __atlas7_gpio_set_input(struct atlas7_gpio_chip *a7gc,
				unsigned int gpio)
{
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 val, pin_in_bank;

	bank = atlas7_gpio_to_bank(a7gc, gpio);
	pin_in_bank = gpio - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	val = readl(ctrl_reg);
	val &= ~ATLAS7_GPIO_CTL_OUT_EN_MASK;
	writel(val, ctrl_reg);
}

static int atlas7_gpio_request(struct gpio_chip *chip,
				unsigned int gpio)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	int ret;
	unsigned long flags;

	ret = __atlas7_gpio_to_pin(a7gc, gpio);
	if (ret < 0)
		return ret;

	if (pinctrl_gpio_request(chip->base + gpio))
		return -ENODEV;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	/*
	 * default status:
	 * set direction as input and mask irq
	 */
	__atlas7_gpio_set_input(a7gc, gpio);
	__atlas7_gpio_irq_mask(a7gc, gpio);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	return 0;
}

static void atlas7_gpio_free(struct gpio_chip *chip,
				unsigned int gpio)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	__atlas7_gpio_irq_mask(a7gc, gpio);
	__atlas7_gpio_set_input(a7gc, gpio);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	pinctrl_gpio_free(chip->base + gpio);
}

static int atlas7_gpio_direction_input(struct gpio_chip *chip,
					unsigned int gpio)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	__atlas7_gpio_set_input(a7gc, gpio);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	return 0;
}

static void __atlas7_gpio_set_output(struct atlas7_gpio_chip *a7gc,
			   unsigned int gpio, int value)
{
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 out_ctrl, pin_in_bank;

	bank = atlas7_gpio_to_bank(a7gc, gpio);
	pin_in_bank = gpio - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	out_ctrl = readl(ctrl_reg);
	if (value)
		out_ctrl |= ATLAS7_GPIO_CTL_DATAOUT_MASK;
	else
		out_ctrl &= ~ATLAS7_GPIO_CTL_DATAOUT_MASK;

	out_ctrl &= ~ATLAS7_GPIO_CTL_INTR_EN_MASK;
	out_ctrl |= ATLAS7_GPIO_CTL_OUT_EN_MASK;
	writel(out_ctrl, ctrl_reg);
}

static int atlas7_gpio_direction_output(struct gpio_chip *chip,
				unsigned int gpio, int value)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	__atlas7_gpio_set_output(a7gc, gpio, value);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	return 0;
}

static int atlas7_gpio_get_value(struct gpio_chip *chip,
					unsigned int gpio)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	struct atlas7_gpio_bank *bank;
	u32 val, pin_in_bank;
	unsigned long flags;

	bank = atlas7_gpio_to_bank(a7gc, gpio);
	pin_in_bank = gpio - bank->gpio_offset;

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	val = readl(ATLAS7_GPIO_CTRL(bank, pin_in_bank));

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);

	return !!(val & ATLAS7_GPIO_CTL_DATAIN_MASK);
}

static void atlas7_gpio_set_value(struct gpio_chip *chip,
				unsigned int gpio, int value)
{
	struct atlas7_gpio_chip *a7gc = gpiochip_get_data(chip);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 ctrl, pin_in_bank;
	unsigned long flags;

	bank = atlas7_gpio_to_bank(a7gc, gpio);
	pin_in_bank = gpio - bank->gpio_offset;
	ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin_in_bank);

	raw_spin_lock_irqsave(&a7gc->lock, flags);

	ctrl = readl(ctrl_reg);
	if (value)
		ctrl |= ATLAS7_GPIO_CTL_DATAOUT_MASK;
	else
		ctrl &= ~ATLAS7_GPIO_CTL_DATAOUT_MASK;
	writel(ctrl, ctrl_reg);

	raw_spin_unlock_irqrestore(&a7gc->lock, flags);
}

static const struct of_device_id atlas7_gpio_ids[] = {
	{ .compatible = "sirf,atlas7-gpio", },
	{},
};

static int atlas7_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct atlas7_gpio_chip *a7gc;
	struct gpio_chip *chip;
	u32 nbank;
	int ret, idx;
	struct gpio_irq_chip *girq;

	ret = of_property_read_u32(np, "gpio-banks", &nbank);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not find GPIO bank info,ret=%d!\n",
			ret);
		return ret;
	}

	/* retrieve gpio descriptor data */
	a7gc = devm_kzalloc(&pdev->dev, struct_size(a7gc, banks, nbank),
			    GFP_KERNEL);
	if (!a7gc)
		return -ENOMEM;

	/* Get Gpio clk */
	a7gc->clk = of_clk_get(np, 0);
	if (!IS_ERR(a7gc->clk)) {
		ret = clk_prepare_enable(a7gc->clk);
		if (ret) {
			dev_err(&pdev->dev,
				"Could not enable clock!\n");
			return ret;
		}
	}

	/* Get Gpio Registers */
	a7gc->reg = of_iomap(np, 0);
	if (!a7gc->reg) {
		dev_err(&pdev->dev, "Could not map GPIO Registers!\n");
		return -ENOMEM;
	}

	a7gc->nbank = nbank;
	raw_spin_lock_init(&a7gc->lock);

	/* Setup GPIO Chip */
	chip = &a7gc->chip;
	chip->request = atlas7_gpio_request;
	chip->free = atlas7_gpio_free;
	chip->direction_input = atlas7_gpio_direction_input;
	chip->get = atlas7_gpio_get_value;
	chip->direction_output = atlas7_gpio_direction_output;
	chip->set = atlas7_gpio_set_value;
	chip->base = -1;
	/* Each chip can support 32 pins at one bank */
	chip->ngpio = NGPIO_OF_BANK * nbank;
	chip->label = kstrdup(np->name, GFP_KERNEL);
	chip->of_node = np;
	chip->of_gpio_n_cells = 2;
	chip->parent = &pdev->dev;

	girq = &chip->irq;
	girq->chip = &atlas7_gpio_irq_chip;
	girq->parent_handler = atlas7_gpio_handle_irq;
	girq->num_parents = nbank;
	girq->parents = devm_kcalloc(&pdev->dev, nbank,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	for (idx = 0; idx < nbank; idx++) {
		struct atlas7_gpio_bank *bank;

		bank = &a7gc->banks[idx];
		/* Set ctrl registers' base of this bank */
		bank->base = ATLAS7_GPIO_BASE(a7gc, idx);
		bank->gpio_offset = idx * NGPIO_OF_BANK;

		/* Get interrupt number from DTS */
		ret = of_irq_get(np, idx);
		if (ret <= 0) {
			dev_err(&pdev->dev,
				"Unable to find IRQ number. ret=%d\n", ret);
			if (!ret)
				ret = -ENXIO;
			goto failed;
		}
		bank->irq = ret;
		girq->parents[idx] = ret;
	}
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	/* Add gpio chip to system */
	ret = gpiochip_add_data(chip, a7gc);
	if (ret) {
		dev_err(&pdev->dev,
			"%pOF: error in probe function with status %d\n",
			np, ret);
		goto failed;
	}

	platform_set_drvdata(pdev, a7gc);
	dev_info(&pdev->dev, "add to system.\n");
	return 0;
failed:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int atlas7_gpio_suspend_noirq(struct device *dev)
{
	struct atlas7_gpio_chip *a7gc = dev_get_drvdata(dev);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 idx, pin;

	for (idx = 0; idx < a7gc->nbank; idx++) {
		bank = &a7gc->banks[idx];
		for (pin = 0; pin < bank->ngpio; pin++) {
			ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin);
			bank->sleep_data[pin] = readl(ctrl_reg);
		}
	}

	return 0;
}

static int atlas7_gpio_resume_noirq(struct device *dev)
{
	struct atlas7_gpio_chip *a7gc = dev_get_drvdata(dev);
	struct atlas7_gpio_bank *bank;
	void __iomem *ctrl_reg;
	u32 idx, pin;

	for (idx = 0; idx < a7gc->nbank; idx++) {
		bank = &a7gc->banks[idx];
		for (pin = 0; pin < bank->ngpio; pin++) {
			ctrl_reg = ATLAS7_GPIO_CTRL(bank, pin);
			writel(bank->sleep_data[pin], ctrl_reg);
		}
	}

	return 0;
}

static const struct dev_pm_ops atlas7_gpio_pm_ops = {
	.suspend_noirq = atlas7_gpio_suspend_noirq,
	.resume_noirq = atlas7_gpio_resume_noirq,
	.freeze_noirq = atlas7_gpio_suspend_noirq,
	.restore_noirq = atlas7_gpio_resume_noirq,
};
#endif

static struct platform_driver atlas7_gpio_driver = {
	.driver = {
		.name = "atlas7-gpio",
		.of_match_table = atlas7_gpio_ids,
#ifdef CONFIG_PM_SLEEP
		.pm = &atlas7_gpio_pm_ops,
#endif
	},
	.probe = atlas7_gpio_probe,
};

static int __init atlas7_gpio_init(void)
{
	return platform_driver_register(&atlas7_gpio_driver);
}
subsys_initcall(atlas7_gpio_init);
