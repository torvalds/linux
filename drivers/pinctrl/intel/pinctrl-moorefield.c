// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Moorefield SoC pinctrl driver
 *
 * Copyright (C) 2022, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinctrl-intel.h"

#define MOFLD_FAMILY_NR			64
#define MOFLD_FAMILY_LEN		0x400

#define SLEW_OFFSET			0x000
#define BUFCFG_OFFSET			0x100
#define MISC_OFFSET			0x300

#define BUFCFG_PINMODE_SHIFT		0
#define BUFCFG_PINMODE_MASK		GENMASK(2, 0)
#define BUFCFG_PINMODE_GPIO		0
#define BUFCFG_PUPD_VAL_SHIFT		4
#define BUFCFG_PUPD_VAL_MASK		GENMASK(5, 4)
#define BUFCFG_PUPD_VAL_2K		0
#define BUFCFG_PUPD_VAL_20K		1
#define BUFCFG_PUPD_VAL_50K		2
#define BUFCFG_PUPD_VAL_910		3
#define BUFCFG_PU_EN			BIT(8)
#define BUFCFG_PD_EN			BIT(9)
#define BUFCFG_Px_EN_MASK		GENMASK(9, 8)
#define BUFCFG_SLEWSEL			BIT(10)
#define BUFCFG_OVINEN			BIT(12)
#define BUFCFG_OVINEN_EN		BIT(13)
#define BUFCFG_OVINEN_MASK		GENMASK(13, 12)
#define BUFCFG_OVOUTEN			BIT(14)
#define BUFCFG_OVOUTEN_EN		BIT(15)
#define BUFCFG_OVOUTEN_MASK		GENMASK(15, 14)
#define BUFCFG_INDATAOV_VAL		BIT(16)
#define BUFCFG_INDATAOV_EN		BIT(17)
#define BUFCFG_INDATAOV_MASK		GENMASK(17, 16)
#define BUFCFG_OUTDATAOV_VAL		BIT(18)
#define BUFCFG_OUTDATAOV_EN		BIT(19)
#define BUFCFG_OUTDATAOV_MASK		GENMASK(19, 18)
#define BUFCFG_OD_EN			BIT(21)

/**
 * struct mofld_family - Intel pin family description
 * @barno: MMIO BAR number where registers for this family reside
 * @pin_base: Starting pin of pins in this family
 * @npins: Number of pins in this family
 * @protected: True if family is protected by access
 * @regs: family specific common registers
 */
struct mofld_family {
	unsigned int barno;
	unsigned int pin_base;
	size_t npins;
	bool protected;
	void __iomem *regs;
};

#define MOFLD_FAMILY(b, s, e)				\
	{						\
		.barno = (b),				\
		.pin_base = (s),			\
		.npins = (e) - (s) + 1,			\
	}

static const struct pinctrl_pin_desc mofld_pins[] = {
	/* ULPI (13 pins) */
	PINCTRL_PIN(0, "GP101_ULPI_CLK"),
	PINCTRL_PIN(1, "GP136_ULPI_D0"),
	PINCTRL_PIN(2, "GP143_ULPI_D1"),
	PINCTRL_PIN(3, "GP144_ULPI_D2"),
	PINCTRL_PIN(4, "GP145_ULPI_D3"),
	PINCTRL_PIN(5, "GP146_ULPI_D4"),
	PINCTRL_PIN(6, "GP147_ULPI_D5"),
	PINCTRL_PIN(7, "GP148_ULPI_D6"),
	PINCTRL_PIN(8, "GP149_ULPI_D7"),
	PINCTRL_PIN(9, "ULPI_DIR"),
	PINCTRL_PIN(10, "ULPI_NXT"),
	PINCTRL_PIN(11, "ULPI_REFCLK"),
	PINCTRL_PIN(12, "ULPI_STP"),
	/* eMMC (12 pins) */
	PINCTRL_PIN(13, "EMMC_CLK"),
	PINCTRL_PIN(14, "EMMC_CMD"),
	PINCTRL_PIN(15, "EMMC_D0"),
	PINCTRL_PIN(16, "EMMC_D1"),
	PINCTRL_PIN(17, "EMMC_D2"),
	PINCTRL_PIN(18, "EMMC_D3"),
	PINCTRL_PIN(19, "EMMC_D4"),
	PINCTRL_PIN(20, "EMMC_D5"),
	PINCTRL_PIN(21, "EMMC_D6"),
	PINCTRL_PIN(22, "EMMC_D7"),
	PINCTRL_PIN(23, "EMMC_RST_N"),
	PINCTRL_PIN(24, "EMMC_RCLK"),
	/* SDIO (20 pins) */
	PINCTRL_PIN(25, "GP77_SD_CD"),
	PINCTRL_PIN(26, "GP78_SD_CLK"),
	PINCTRL_PIN(27, "GP79_SD_CMD"),
	PINCTRL_PIN(28, "GP80_SD_D0"),
	PINCTRL_PIN(29, "GP81_SD_D1"),
	PINCTRL_PIN(30, "GP82_SD_D2"),
	PINCTRL_PIN(31, "GP83_SD_D3"),
	PINCTRL_PIN(32, "GP84_SD_LS_CLK_FB"),
	PINCTRL_PIN(33, "GP85_SD_LS_CMD_DIR"),
	PINCTRL_PIN(34, "GP86_SD_LS_D_DIR"),
	PINCTRL_PIN(35, "GP88_SD_LS_SEL"),
	PINCTRL_PIN(36, "GP87_SD_PD"),
	PINCTRL_PIN(37, "GP89_SD_WP"),
	PINCTRL_PIN(38, "GP90_SDIO_CLK"),
	PINCTRL_PIN(39, "GP91_SDIO_CMD"),
	PINCTRL_PIN(40, "GP92_SDIO_D0"),
	PINCTRL_PIN(41, "GP93_SDIO_D1"),
	PINCTRL_PIN(42, "GP94_SDIO_D2"),
	PINCTRL_PIN(43, "GP95_SDIO_D3"),
	PINCTRL_PIN(44, "GP96_SDIO_PD"),
	/* HSI (8 pins) */
	PINCTRL_PIN(45, "HSI_ACDATA"),
	PINCTRL_PIN(46, "HSI_ACFLAG"),
	PINCTRL_PIN(47, "HSI_ACREADY"),
	PINCTRL_PIN(48, "HSI_ACWAKE"),
	PINCTRL_PIN(49, "HSI_CADATA"),
	PINCTRL_PIN(50, "HSI_CAFLAG"),
	PINCTRL_PIN(51, "HSI_CAREADY"),
	PINCTRL_PIN(52, "HSI_CAWAKE"),
	/* SSP Audio (14 pins) */
	PINCTRL_PIN(53, "GP70"),
	PINCTRL_PIN(54, "GP71"),
	PINCTRL_PIN(55, "GP32_I2S_0_CLK"),
	PINCTRL_PIN(56, "GP33_I2S_0_FS"),
	PINCTRL_PIN(57, "GP34_I2S_0_RXD"),
	PINCTRL_PIN(58, "GP35_I2S_0_TXD"),
	PINCTRL_PIN(59, "GP36_I2S_1_CLK"),
	PINCTRL_PIN(60, "GP37_I2S_1_FS"),
	PINCTRL_PIN(61, "GP38_I2S_1_RXD"),
	PINCTRL_PIN(62, "GP39_I2S_1_TXD"),
	PINCTRL_PIN(63, "GP40_I2S_2_CLK"),
	PINCTRL_PIN(64, "GP41_I2S_2_FS"),
	PINCTRL_PIN(65, "GP42_I2S_2_RXD"),
	PINCTRL_PIN(66, "GP43_I2S_2_TXD"),
	/* GP SSP (22 pins) */
	PINCTRL_PIN(67, "GP120_SPI_0_CLK"),
	PINCTRL_PIN(68, "GP121_SPI_0_SS"),
	PINCTRL_PIN(69, "GP122_SPI_0_RXD"),
	PINCTRL_PIN(70, "GP123_SPI_0_TXD"),
	PINCTRL_PIN(71, "GP102_SPI_1_CLK"),
	PINCTRL_PIN(72, "GP103_SPI_1_SS0"),
	PINCTRL_PIN(73, "GP104_SPI_1_SS1"),
	PINCTRL_PIN(74, "GP105_SPI_1_SS2"),
	PINCTRL_PIN(75, "GP106_SPI_1_SS3"),
	PINCTRL_PIN(76, "GP107_SPI_1_RXD"),
	PINCTRL_PIN(77, "GP108_SPI_1_TXD"),
	PINCTRL_PIN(78, "GP109_SPI_2_CLK"),
	PINCTRL_PIN(79, "GP110_SPI_2_SS0"),
	PINCTRL_PIN(80, "GP111_SPI_2_SS1"),
	PINCTRL_PIN(81, "GP112_SPI_2_SS2"),
	PINCTRL_PIN(82, "GP113_SPI_2_SS3"),
	PINCTRL_PIN(83, "GP114_SPI_2_RXD"),
	PINCTRL_PIN(84, "GP115_SPI_2_TXD"),
	PINCTRL_PIN(85, "GP116_SPI_3_CLK"),
	PINCTRL_PIN(86, "GP117_SPI_3_SS"),
	PINCTRL_PIN(87, "GP118_SPI_3_RXD"),
	PINCTRL_PIN(88, "GP119_SPI_3_TXD"),
	/* I2C (20 pins) */
	PINCTRL_PIN(89, "I2C_0_SCL"),
	PINCTRL_PIN(90, "I2C_0_SDA"),
	PINCTRL_PIN(91, "GP19_I2C_1_SCL"),
	PINCTRL_PIN(92, "GP20_I2C_1_SDA"),
	PINCTRL_PIN(93, "GP21_I2C_2_SCL"),
	PINCTRL_PIN(94, "GP22_I2C_2_SDA"),
	PINCTRL_PIN(95, "GP17_I2C_3_SCL_HDMI"),
	PINCTRL_PIN(96, "GP18_I2C_3_SDA_HDMI"),
	PINCTRL_PIN(97, "GP23_I2C_4_SCL"),
	PINCTRL_PIN(98, "GP24_I2C_4_SDA"),
	PINCTRL_PIN(99, "GP25_I2C_5_SCL"),
	PINCTRL_PIN(100, "GP26_I2C_5_SDA"),
	PINCTRL_PIN(101, "GP27_I2C_6_SCL"),
	PINCTRL_PIN(102, "GP28_I2C_6_SDA"),
	PINCTRL_PIN(103, "GP29_I2C_7_SCL"),
	PINCTRL_PIN(104, "GP30_I2C_7_SDA"),
	PINCTRL_PIN(105, "I2C_8_SCL"),
	PINCTRL_PIN(106, "I2C_8_SDA"),
	PINCTRL_PIN(107, "I2C_9_SCL"),
	PINCTRL_PIN(108, "I2C_9_SDA"),
	/* UART (23 pins) */
	PINCTRL_PIN(109, "GP124_UART_0_CTS"),
	PINCTRL_PIN(110, "GP125_UART_0_RTS"),
	PINCTRL_PIN(111, "GP126_UART_0_RX"),
	PINCTRL_PIN(112, "GP127_UART_0_TX"),
	PINCTRL_PIN(113, "GP128_UART_1_CTS"),
	PINCTRL_PIN(114, "GP129_UART_1_RTS"),
	PINCTRL_PIN(115, "GP130_UART_1_RX"),
	PINCTRL_PIN(116, "GP131_UART_1_TX"),
	PINCTRL_PIN(117, "GP132_UART_2_CTS"),
	PINCTRL_PIN(118, "GP133_UART_2_RTS"),
	PINCTRL_PIN(119, "GP134_UART_2_RX"),
	PINCTRL_PIN(120, "GP135_UART_2_TX"),
	PINCTRL_PIN(121, "GP97"),
	PINCTRL_PIN(122, "GP154"),
	PINCTRL_PIN(123, "GP155"),
	PINCTRL_PIN(124, "GP156"),
	PINCTRL_PIN(125, "GP157"),
	PINCTRL_PIN(126, "GP158"),
	PINCTRL_PIN(127, "GP159"),
	PINCTRL_PIN(128, "GP160"),
	PINCTRL_PIN(129, "GP161"),
	PINCTRL_PIN(130, "GP12_PWM0"),
	PINCTRL_PIN(131, "GP13_PWM1"),
	/* GPIO South (20 pins) */
	PINCTRL_PIN(132, "GP176"),
	PINCTRL_PIN(133, "GP177"),
	PINCTRL_PIN(134, "GP178"),
	PINCTRL_PIN(135, "GP179"),
	PINCTRL_PIN(136, "GP180"),
	PINCTRL_PIN(137, "GP181"),
	PINCTRL_PIN(138, "GP182_PWM2"),
	PINCTRL_PIN(139, "GP183_PWM3"),
	PINCTRL_PIN(140, "GP184"),
	PINCTRL_PIN(141, "GP185"),
	PINCTRL_PIN(142, "GP186"),
	PINCTRL_PIN(143, "GP187"),
	PINCTRL_PIN(144, "GP188"),
	PINCTRL_PIN(145, "GP189"),
	PINCTRL_PIN(146, "GP190"),
	PINCTRL_PIN(147, "GP191"),
	PINCTRL_PIN(148, "GP14"),
	PINCTRL_PIN(149, "GP15"),
	PINCTRL_PIN(150, "GP162"),
	PINCTRL_PIN(151, "GP163"),
	/* Camera Sideband (15 pins) */
	PINCTRL_PIN(152, "GP0"),
	PINCTRL_PIN(153, "GP1"),
	PINCTRL_PIN(154, "GP2"),
	PINCTRL_PIN(155, "GP3"),
	PINCTRL_PIN(156, "GP4"),
	PINCTRL_PIN(157, "GP5"),
	PINCTRL_PIN(158, "GP6"),
	PINCTRL_PIN(159, "GP7"),
	PINCTRL_PIN(160, "GP8"),
	PINCTRL_PIN(161, "GP9"),
	PINCTRL_PIN(162, "GP10"),
	PINCTRL_PIN(163, "GP11"),
	PINCTRL_PIN(164, "GP16_HDMI_HPD"),
	PINCTRL_PIN(165, "GP68_DSI_A_TE"),
	PINCTRL_PIN(166, "GP69_DSI_C_TE"),
	/* Clock (14 pins) */
	PINCTRL_PIN(167, "GP137"),
	PINCTRL_PIN(168, "GP138"),
	PINCTRL_PIN(169, "GP139"),
	PINCTRL_PIN(170, "GP140"),
	PINCTRL_PIN(171, "GP141"),
	PINCTRL_PIN(172, "GP142"),
	PINCTRL_PIN(173, "GP98"),
	PINCTRL_PIN(174, "OSC_CLK_CTRL0"),
	PINCTRL_PIN(175, "OSC_CLK_CTRL1"),
	PINCTRL_PIN(176, "OSC_CLK0"),
	PINCTRL_PIN(177, "OSC_CLK1"),
	PINCTRL_PIN(178, "OSC_CLK2"),
	PINCTRL_PIN(179, "OSC_CLK3"),
	PINCTRL_PIN(180, "OSC_CLK4"),
	/* PMIC (15 pins) */
	PINCTRL_PIN(181, "PROCHOT"),
	PINCTRL_PIN(182, "RESETOUT"),
	PINCTRL_PIN(183, "RTC_CLK"),
	PINCTRL_PIN(184, "STANDBY"),
	PINCTRL_PIN(185, "SVID_ALERT"),
	PINCTRL_PIN(186, "SVID_CLK"),
	PINCTRL_PIN(187, "SVID_D"),
	PINCTRL_PIN(188, "THERMTRIP"),
	PINCTRL_PIN(189, "PREQ"),
	PINCTRL_PIN(190, "ZQ_A"),
	PINCTRL_PIN(191, "ZQ_B"),
	PINCTRL_PIN(192, "GP64_FAST_INT0"),
	PINCTRL_PIN(193, "GP65_FAST_INT1"),
	PINCTRL_PIN(194, "GP66_FAST_INT2"),
	PINCTRL_PIN(195, "GP67_FAST_INT3"),
	/* Keyboard (20 pins) */
	PINCTRL_PIN(196, "GP44"),
	PINCTRL_PIN(197, "GP45"),
	PINCTRL_PIN(198, "GP46"),
	PINCTRL_PIN(199, "GP47"),
	PINCTRL_PIN(200, "GP48"),
	PINCTRL_PIN(201, "GP49"),
	PINCTRL_PIN(202, "GP50"),
	PINCTRL_PIN(203, "GP51"),
	PINCTRL_PIN(204, "GP52"),
	PINCTRL_PIN(205, "GP53"),
	PINCTRL_PIN(206, "GP54"),
	PINCTRL_PIN(207, "GP55"),
	PINCTRL_PIN(208, "GP56"),
	PINCTRL_PIN(209, "GP57"),
	PINCTRL_PIN(210, "GP58"),
	PINCTRL_PIN(211, "GP59"),
	PINCTRL_PIN(212, "GP60"),
	PINCTRL_PIN(213, "GP61"),
	PINCTRL_PIN(214, "GP62"),
	PINCTRL_PIN(215, "GP63"),
	/* GPIO North (13 pins) */
	PINCTRL_PIN(216, "GP164"),
	PINCTRL_PIN(217, "GP165"),
	PINCTRL_PIN(218, "GP166"),
	PINCTRL_PIN(219, "GP167"),
	PINCTRL_PIN(220, "GP168_MJTAG_TCK"),
	PINCTRL_PIN(221, "GP169_MJTAG_TDI"),
	PINCTRL_PIN(222, "GP170_MJTAG_TDO"),
	PINCTRL_PIN(223, "GP171_MJTAG_TMS"),
	PINCTRL_PIN(224, "GP172_MJTAG_TRST"),
	PINCTRL_PIN(225, "GP173"),
	PINCTRL_PIN(226, "GP174"),
	PINCTRL_PIN(227, "GP175"),
	PINCTRL_PIN(228, "GP176"),
	/* PTI (22 pins) */
	PINCTRL_PIN(229, "GP72_PTI_CLK"),
	PINCTRL_PIN(230, "GP73_PTI_D0"),
	PINCTRL_PIN(231, "GP74_PTI_D1"),
	PINCTRL_PIN(232, "GP75_PTI_D2"),
	PINCTRL_PIN(233, "GP76_PTI_D3"),
	PINCTRL_PIN(234, "GP164"),
	PINCTRL_PIN(235, "GP165"),
	PINCTRL_PIN(236, "GP166"),
	PINCTRL_PIN(237, "GP167"),
	PINCTRL_PIN(238, "GP168_MJTAG_TCK"),
	PINCTRL_PIN(239, "GP169_MJTAG_TDI"),
	PINCTRL_PIN(240, "GP170_MJTAG_TDO"),
	PINCTRL_PIN(241, "GP171_MJTAG_TMS"),
	PINCTRL_PIN(242, "GP172_MJTAG_TRST"),
	PINCTRL_PIN(243, "GP173"),
	PINCTRL_PIN(244, "GP174"),
	PINCTRL_PIN(245, "GP175"),
	PINCTRL_PIN(246, "JTAG_TCK"),
	PINCTRL_PIN(247, "JTAG_TDI"),
	PINCTRL_PIN(248, "JTAG_TDO"),
	PINCTRL_PIN(249, "JTAG_TMS"),
	PINCTRL_PIN(250, "JTAG_TRST"),
};

static const struct mofld_family mofld_families[] = {
	MOFLD_FAMILY(0, 0, 12),
	MOFLD_FAMILY(1, 13, 24),
	MOFLD_FAMILY(2, 25, 44),
	MOFLD_FAMILY(3, 45, 52),
	MOFLD_FAMILY(4, 53, 66),
	MOFLD_FAMILY(5, 67, 88),
	MOFLD_FAMILY(6, 89, 108),
	MOFLD_FAMILY(7, 109, 131),
	MOFLD_FAMILY(8, 132, 151),
	MOFLD_FAMILY(9, 152, 166),
	MOFLD_FAMILY(10, 167, 180),
	MOFLD_FAMILY(11, 181, 195),
	MOFLD_FAMILY(12, 196, 215),
	MOFLD_FAMILY(13, 216, 228),
	MOFLD_FAMILY(14, 229, 250),
};

/**
 * struct mofld_pinctrl - Intel Merrifield pinctrl private structure
 * @dev: Pointer to the device structure
 * @lock: Lock to serialize register access
 * @pctldesc: Pin controller description
 * @pctldev: Pointer to the pin controller device
 * @families: Array of families this pinctrl handles
 * @nfamilies: Number of families in the array
 * @functions: Array of functions
 * @nfunctions: Number of functions in the array
 * @groups: Array of pin groups
 * @ngroups: Number of groups in the array
 * @pins: Array of pins this pinctrl controls
 * @npins: Number of pins in the array
 */
struct mofld_pinctrl {
	struct device *dev;
	raw_spinlock_t lock;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;

	/* Pin controller configuration */
	const struct mofld_family *families;
	size_t nfamilies;
	const struct intel_function *functions;
	size_t nfunctions;
	const struct intel_pingroup *groups;
	size_t ngroups;
	const struct pinctrl_pin_desc *pins;
	size_t npins;
};

#define pin_to_bufno(f, p)		((p) - (f)->pin_base)

static const struct mofld_family *mofld_get_family(struct mofld_pinctrl *mp, unsigned int pin)
{
	const struct mofld_family *family;
	unsigned int i;

	for (i = 0; i < mp->nfamilies; i++) {
		family = &mp->families[i];
		if (pin >= family->pin_base &&
		    pin < family->pin_base + family->npins)
			return family;
	}

	dev_warn(mp->dev, "failed to find family for pin %u\n", pin);
	return NULL;
}

static bool mofld_buf_available(struct mofld_pinctrl *mp, unsigned int pin)
{
	const struct mofld_family *family;

	family = mofld_get_family(mp, pin);
	if (!family)
		return false;

	return !family->protected;
}

static void __iomem *mofld_get_bufcfg(struct mofld_pinctrl *mp, unsigned int pin)
{
	const struct mofld_family *family;
	unsigned int bufno;

	family = mofld_get_family(mp, pin);
	if (!family)
		return NULL;

	bufno = pin_to_bufno(family, pin);
	return family->regs + BUFCFG_OFFSET + bufno * 4;
}

static int mofld_read_bufcfg(struct mofld_pinctrl *mp, unsigned int pin, u32 *value)
{
	void __iomem *bufcfg;

	if (!mofld_buf_available(mp, pin))
		return -EBUSY;

	bufcfg = mofld_get_bufcfg(mp, pin);
	*value = readl(bufcfg);

	return 0;
}

static void mofld_update_bufcfg(struct mofld_pinctrl *mp, unsigned int pin, u32 bits, u32 mask)
{
	void __iomem *bufcfg;
	u32 value;

	bufcfg = mofld_get_bufcfg(mp, pin);
	value = readl(bufcfg);

	value &= ~mask;
	value |= bits & mask;

	writel(value, bufcfg);
}

static int mofld_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	return mp->ngroups;
}

static const char *mofld_get_group_name(struct pinctrl_dev *pctldev, unsigned int group)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	return mp->groups[group].grp.name;
}

static int mofld_get_group_pins(struct pinctrl_dev *pctldev, unsigned int group,
				const unsigned int **pins, unsigned int *npins)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	*pins = mp->groups[group].grp.pins;
	*npins = mp->groups[group].grp.npins;
	return 0;
}

static void mofld_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			       unsigned int pin)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);
	u32 value, mode;
	int ret;

	ret = mofld_read_bufcfg(mp, pin, &value);
	if (ret) {
		seq_puts(s, "not available");
		return;
	}

	mode = (value & BUFCFG_PINMODE_MASK) >> BUFCFG_PINMODE_SHIFT;
	if (!mode)
		seq_puts(s, "GPIO ");
	else
		seq_printf(s, "mode %d ", mode);

	seq_printf(s, "0x%08x", value);
}

static const struct pinctrl_ops mofld_pinctrl_ops = {
	.get_groups_count = mofld_get_groups_count,
	.get_group_name = mofld_get_group_name,
	.get_group_pins = mofld_get_group_pins,
	.pin_dbg_show = mofld_pin_dbg_show,
};

static int mofld_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	return mp->nfunctions;
}

static const char *mofld_get_function_name(struct pinctrl_dev *pctldev, unsigned int function)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	return mp->functions[function].func.name;
}

static int mofld_get_function_groups(struct pinctrl_dev *pctldev, unsigned int function,
				     const char * const **groups, unsigned int * const ngroups)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);

	*groups = mp->functions[function].func.groups;
	*ngroups = mp->functions[function].func.ngroups;
	return 0;
}

static int mofld_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int function,
				unsigned int group)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);
	const struct intel_pingroup *grp = &mp->groups[group];
	u32 bits = grp->mode << BUFCFG_PINMODE_SHIFT;
	u32 mask = BUFCFG_PINMODE_MASK;
	unsigned long flags;
	unsigned int i;

	/*
	 * All pins in the groups needs to be accessible and writable
	 * before we can enable the mux for this group.
	 */
	for (i = 0; i < grp->grp.npins; i++) {
		if (!mofld_buf_available(mp, grp->grp.pins[i]))
			return -EBUSY;
	}

	/* Now enable the mux setting for each pin in the group */
	raw_spin_lock_irqsave(&mp->lock, flags);
	for (i = 0; i < grp->grp.npins; i++)
		mofld_update_bufcfg(mp, grp->grp.pins[i], bits, mask);
	raw_spin_unlock_irqrestore(&mp->lock, flags);

	return 0;
}

static int mofld_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int pin)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);
	u32 bits = BUFCFG_PINMODE_GPIO << BUFCFG_PINMODE_SHIFT;
	u32 mask = BUFCFG_PINMODE_MASK;
	unsigned long flags;

	if (!mofld_buf_available(mp, pin))
		return -EBUSY;

	raw_spin_lock_irqsave(&mp->lock, flags);
	mofld_update_bufcfg(mp, pin, bits, mask);
	raw_spin_unlock_irqrestore(&mp->lock, flags);

	return 0;
}

static const struct pinmux_ops mofld_pinmux_ops = {
	.get_functions_count = mofld_get_functions_count,
	.get_function_name = mofld_get_function_name,
	.get_function_groups = mofld_get_function_groups,
	.set_mux = mofld_pinmux_set_mux,
	.gpio_request_enable = mofld_gpio_request_enable,
};

static int mofld_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 value, term;
	u16 arg = 0;
	int ret;

	ret = mofld_read_bufcfg(mp, pin, &value);
	if (ret)
		return -ENOTSUPP;

	term = (value & BUFCFG_PUPD_VAL_MASK) >> BUFCFG_PUPD_VAL_SHIFT;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (value & BUFCFG_Px_EN_MASK)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if ((value & BUFCFG_Px_EN_MASK) != BUFCFG_PU_EN)
			return -EINVAL;

		switch (term) {
		case BUFCFG_PUPD_VAL_910:
			arg = 910;
			break;
		case BUFCFG_PUPD_VAL_2K:
			arg = 2000;
			break;
		case BUFCFG_PUPD_VAL_20K:
			arg = 20000;
			break;
		case BUFCFG_PUPD_VAL_50K:
			arg = 50000;
			break;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if ((value & BUFCFG_Px_EN_MASK) != BUFCFG_PD_EN)
			return -EINVAL;

		switch (term) {
		case BUFCFG_PUPD_VAL_910:
			arg = 910;
			break;
		case BUFCFG_PUPD_VAL_2K:
			arg = 2000;
			break;
		case BUFCFG_PUPD_VAL_20K:
			arg = 20000;
			break;
		case BUFCFG_PUPD_VAL_50K:
			arg = 50000;
			break;
		}

		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!(value & BUFCFG_OD_EN))
			return -EINVAL;
		break;

	case PIN_CONFIG_SLEW_RATE:
		if (!(value & BUFCFG_SLEWSEL))
			arg = 0;
		else
			arg = 1;
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int mofld_config_set_pin(struct mofld_pinctrl *mp, unsigned int pin,
				unsigned long config)
{
	unsigned int param = pinconf_to_config_param(config);
	unsigned int arg = pinconf_to_config_argument(config);
	u32 bits = 0, mask = 0;
	unsigned long flags;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		mask |= BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		mask |= BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		bits |= BUFCFG_PU_EN;

		switch (arg) {
		case 50000:
			bits |= BUFCFG_PUPD_VAL_50K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		case 20000:
			bits |= BUFCFG_PUPD_VAL_20K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		case 2000:
			bits |= BUFCFG_PUPD_VAL_2K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		default:
			return -EINVAL;
		}

		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		mask |= BUFCFG_Px_EN_MASK | BUFCFG_PUPD_VAL_MASK;
		bits |= BUFCFG_PD_EN;

		switch (arg) {
		case 50000:
			bits |= BUFCFG_PUPD_VAL_50K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		case 20000:
			bits |= BUFCFG_PUPD_VAL_20K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		case 2000:
			bits |= BUFCFG_PUPD_VAL_2K << BUFCFG_PUPD_VAL_SHIFT;
			break;
		default:
			return -EINVAL;
		}

		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		mask |= BUFCFG_OD_EN;
		if (arg)
			bits |= BUFCFG_OD_EN;
		break;

	case PIN_CONFIG_SLEW_RATE:
		mask |= BUFCFG_SLEWSEL;
		if (arg)
			bits |= BUFCFG_SLEWSEL;
		break;
	}

	raw_spin_lock_irqsave(&mp->lock, flags);
	mofld_update_bufcfg(mp, pin, bits, mask);
	raw_spin_unlock_irqrestore(&mp->lock, flags);

	return 0;
}

static int mofld_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int nconfigs)
{
	struct mofld_pinctrl *mp = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	int ret;

	if (!mofld_buf_available(mp, pin))
		return -ENOTSUPP;

	for (i = 0; i < nconfigs; i++) {
		switch (pinconf_to_config_param(configs[i])) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		case PIN_CONFIG_SLEW_RATE:
			ret = mofld_config_set_pin(mp, pin, configs[i]);
			if (ret)
				return ret;
			break;

		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int mofld_config_group_get(struct pinctrl_dev *pctldev, unsigned int group,
				  unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = mofld_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	ret = mofld_config_get(pctldev, pins[0], config);
	if (ret)
		return ret;

	return 0;
}

static int mofld_config_group_set(struct pinctrl_dev *pctldev, unsigned int group,
				  unsigned long *configs, unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	ret = mofld_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = mofld_config_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops mofld_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = mofld_config_get,
	.pin_config_set = mofld_config_set,
	.pin_config_group_get = mofld_config_group_get,
	.pin_config_group_set = mofld_config_group_set,
};

static const struct pinctrl_desc mofld_pinctrl_desc = {
	.pctlops = &mofld_pinctrl_ops,
	.pmxops = &mofld_pinmux_ops,
	.confops = &mofld_pinconf_ops,
	.owner = THIS_MODULE,
};

static int mofld_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mofld_family *families;
	struct mofld_pinctrl *mp;
	void __iomem *regs;
	size_t nfamilies;
	unsigned int i;

	mp = devm_kzalloc(dev, sizeof(*mp), GFP_KERNEL);
	if (!mp)
		return -ENOMEM;

	mp->dev = dev;
	raw_spin_lock_init(&mp->lock);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	nfamilies = ARRAY_SIZE(mofld_families),
	families = devm_kmemdup(dev, mofld_families, sizeof(mofld_families), GFP_KERNEL);
	if (!families)
		return -ENOMEM;

	/* Splice memory resource by chunk per family */
	for (i = 0; i < nfamilies; i++) {
		struct mofld_family *family = &families[i];

		family->regs = regs + family->barno * MOFLD_FAMILY_LEN;
	}

	mp->families = families;
	mp->nfamilies = nfamilies;
	mp->pctldesc = mofld_pinctrl_desc;
	mp->pctldesc.name = dev_name(dev);
	mp->pctldesc.pins = mofld_pins;
	mp->pctldesc.npins = ARRAY_SIZE(mofld_pins);

	mp->pctldev = devm_pinctrl_register(dev, &mp->pctldesc, mp);
	if (IS_ERR(mp->pctldev))
		return PTR_ERR(mp->pctldev);

	platform_set_drvdata(pdev, mp);
	return 0;
}

static const struct acpi_device_id mofld_acpi_table[] = {
	{ "INTC1003" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, mofld_acpi_table);

static struct platform_driver mofld_pinctrl_driver = {
	.probe = mofld_pinctrl_probe,
	.driver = {
		.name = "pinctrl-moorefield",
		.acpi_match_table = mofld_acpi_table,
	},
};

static int __init mofld_pinctrl_init(void)
{
	return platform_driver_register(&mofld_pinctrl_driver);
}
subsys_initcall(mofld_pinctrl_init);

static void __exit mofld_pinctrl_exit(void)
{
	platform_driver_unregister(&mofld_pinctrl_driver);
}
module_exit(mofld_pinctrl_exit);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel Moorefield SoC pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pinctrl-moorefield");
