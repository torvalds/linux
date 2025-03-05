// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell Dove pinctrl driver based on mvebu pinctrl core
 *
 * Author: Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>

#include "pinctrl-mvebu.h"

/* Internal registers can be configured at any 1 MiB aligned address */
#define INT_REGS_MASK		~(SZ_1M - 1)
#define MPP4_REGS_OFFS		0xd0440
#define PMU_REGS_OFFS		0xd802c
#define GC_REGS_OFFS		0xe802c

/* MPP Base registers */
#define PMU_MPP_GENERAL_CTRL	0x10
#define  AU0_AC97_SEL		BIT(16)

/* MPP Control 4 register */
#define SPI_GPIO_SEL		BIT(5)
#define UART1_GPIO_SEL		BIT(4)
#define AU1_GPIO_SEL		BIT(3)
#define CAM_GPIO_SEL		BIT(2)
#define SD1_GPIO_SEL		BIT(1)
#define SD0_GPIO_SEL		BIT(0)

/* PMU Signal Select registers */
#define PMU_SIGNAL_SELECT_0	0x00
#define PMU_SIGNAL_SELECT_1	0x04

/* Global Config regmap registers */
#define GLOBAL_CONFIG_1		0x00
#define  TWSI_ENABLE_OPTION1	BIT(7)
#define GLOBAL_CONFIG_2		0x04
#define  TWSI_ENABLE_OPTION2	BIT(20)
#define  TWSI_ENABLE_OPTION3	BIT(21)
#define  TWSI_OPTION3_GPIO	BIT(22)
#define SSP_CTRL_STATUS_1	0x08
#define  SSP_ON_AU1		BIT(0)
#define MPP_GENERAL_CONFIG	0x10
#define  AU1_SPDIFO_GPIO_EN	BIT(1)
#define  NAND_GPIO_EN		BIT(0)

#define CONFIG_PMU	BIT(4)

static void __iomem *mpp4_base;
static void __iomem *pmu_base;
static struct regmap *gconfmap;

static int dove_pmu_mpp_ctrl_get(struct mvebu_mpp_ctrl_data *data,
				 unsigned pid, unsigned long *config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned long pmu = readl(data->base + PMU_MPP_GENERAL_CTRL);
	unsigned long func;

	if ((pmu & BIT(pid)) == 0)
		return mvebu_mmio_mpp_ctrl_get(data, pid, config);

	func = readl(pmu_base + PMU_SIGNAL_SELECT_0 + off);
	*config = (func >> shift) & MVEBU_MPP_MASK;
	*config |= CONFIG_PMU;

	return 0;
}

static int dove_pmu_mpp_ctrl_set(struct mvebu_mpp_ctrl_data *data,
				 unsigned pid, unsigned long config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned long pmu = readl(data->base + PMU_MPP_GENERAL_CTRL);
	unsigned long func;

	if ((config & CONFIG_PMU) == 0) {
		writel(pmu & ~BIT(pid), data->base + PMU_MPP_GENERAL_CTRL);
		return mvebu_mmio_mpp_ctrl_set(data, pid, config);
	}

	writel(pmu | BIT(pid), data->base + PMU_MPP_GENERAL_CTRL);
	func = readl(pmu_base + PMU_SIGNAL_SELECT_0 + off);
	func &= ~(MVEBU_MPP_MASK << shift);
	func |= (config & MVEBU_MPP_MASK) << shift;
	writel(func, pmu_base + PMU_SIGNAL_SELECT_0 + off);

	return 0;
}

static int dove_mpp4_ctrl_get(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long *config)
{
	unsigned long mpp4 = readl(mpp4_base);
	unsigned long mask;

	switch (pid) {
	case 24: /* mpp_camera */
		mask = CAM_GPIO_SEL;
		break;
	case 40: /* mpp_sdio0 */
		mask = SD0_GPIO_SEL;
		break;
	case 46: /* mpp_sdio1 */
		mask = SD1_GPIO_SEL;
		break;
	case 58: /* mpp_spi0 */
		mask = SPI_GPIO_SEL;
		break;
	case 62: /* mpp_uart1 */
		mask = UART1_GPIO_SEL;
		break;
	default:
		return -EINVAL;
	}

	*config = ((mpp4 & mask) != 0);

	return 0;
}

static int dove_mpp4_ctrl_set(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long config)
{
	unsigned long mpp4 = readl(mpp4_base);
	unsigned long mask;

	switch (pid) {
	case 24: /* mpp_camera */
		mask = CAM_GPIO_SEL;
		break;
	case 40: /* mpp_sdio0 */
		mask = SD0_GPIO_SEL;
		break;
	case 46: /* mpp_sdio1 */
		mask = SD1_GPIO_SEL;
		break;
	case 58: /* mpp_spi0 */
		mask = SPI_GPIO_SEL;
		break;
	case 62: /* mpp_uart1 */
		mask = UART1_GPIO_SEL;
		break;
	default:
		return -EINVAL;
	}

	mpp4 &= ~mask;
	if (config)
		mpp4 |= mask;

	writel(mpp4, mpp4_base);

	return 0;
}

static int dove_nand_ctrl_get(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long *config)
{
	unsigned int gmpp;

	regmap_read(gconfmap, MPP_GENERAL_CONFIG, &gmpp);
	*config = ((gmpp & NAND_GPIO_EN) != 0);

	return 0;
}

static int dove_nand_ctrl_set(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long config)
{
	regmap_update_bits(gconfmap, MPP_GENERAL_CONFIG,
			   NAND_GPIO_EN,
			   (config) ? NAND_GPIO_EN : 0);
	return 0;
}

static int dove_audio0_ctrl_get(struct mvebu_mpp_ctrl_data *data, unsigned pid,
				unsigned long *config)
{
	unsigned long pmu = readl(data->base + PMU_MPP_GENERAL_CTRL);

	*config = ((pmu & AU0_AC97_SEL) != 0);

	return 0;
}

static int dove_audio0_ctrl_set(struct mvebu_mpp_ctrl_data *data, unsigned pid,
				unsigned long config)
{
	unsigned long pmu = readl(data->base + PMU_MPP_GENERAL_CTRL);

	pmu &= ~AU0_AC97_SEL;
	if (config)
		pmu |= AU0_AC97_SEL;
	writel(pmu, data->base + PMU_MPP_GENERAL_CTRL);

	return 0;
}

static int dove_audio1_ctrl_get(struct mvebu_mpp_ctrl_data *data, unsigned pid,
				unsigned long *config)
{
	unsigned int mpp4 = readl(mpp4_base);
	unsigned int sspc1;
	unsigned int gmpp;
	unsigned int gcfg2;

	regmap_read(gconfmap, SSP_CTRL_STATUS_1, &sspc1);
	regmap_read(gconfmap, MPP_GENERAL_CONFIG, &gmpp);
	regmap_read(gconfmap, GLOBAL_CONFIG_2, &gcfg2);

	*config = 0;
	if (mpp4 & AU1_GPIO_SEL)
		*config |= BIT(3);
	if (sspc1 & SSP_ON_AU1)
		*config |= BIT(2);
	if (gmpp & AU1_SPDIFO_GPIO_EN)
		*config |= BIT(1);
	if (gcfg2 & TWSI_OPTION3_GPIO)
		*config |= BIT(0);

	/* SSP/TWSI only if I2S1 not set*/
	if ((*config & BIT(3)) == 0)
		*config &= ~(BIT(2) | BIT(0));
	/* TWSI only if SPDIFO not set*/
	if ((*config & BIT(1)) == 0)
		*config &= ~BIT(0);
	return 0;
}

static int dove_audio1_ctrl_set(struct mvebu_mpp_ctrl_data *data, unsigned pid,
				unsigned long config)
{
	unsigned int mpp4 = readl(mpp4_base);

	mpp4 &= ~AU1_GPIO_SEL;
	if (config & BIT(3))
		mpp4 |= AU1_GPIO_SEL;
	writel(mpp4, mpp4_base);

	regmap_update_bits(gconfmap, SSP_CTRL_STATUS_1,
			   SSP_ON_AU1,
			   (config & BIT(2)) ? SSP_ON_AU1 : 0);
	regmap_update_bits(gconfmap, MPP_GENERAL_CONFIG,
			   AU1_SPDIFO_GPIO_EN,
			   (config & BIT(1)) ? AU1_SPDIFO_GPIO_EN : 0);
	regmap_update_bits(gconfmap, GLOBAL_CONFIG_2,
			   TWSI_OPTION3_GPIO,
			   (config & BIT(0)) ? TWSI_OPTION3_GPIO : 0);

	return 0;
}

/* mpp[52:57] gpio pins depend heavily on current config;
 * gpio_req does not try to mux in gpio capabilities to not
 * break other functions. If you require all mpps as gpio
 * enforce gpio setting by pinctrl mapping.
 */
static int dove_audio1_ctrl_gpio_req(struct mvebu_mpp_ctrl_data *data,
				     unsigned pid)
{
	unsigned long config;

	dove_audio1_ctrl_get(data, pid, &config);

	switch (config) {
	case 0x02: /* i2s1 : gpio[56:57] */
	case 0x0e: /* ssp  : gpio[56:57] */
		if (pid >= 56)
			return 0;
		return -ENOTSUPP;
	case 0x08: /* spdifo : gpio[52:55] */
	case 0x0b: /* twsi   : gpio[52:55] */
		if (pid <= 55)
			return 0;
		return -ENOTSUPP;
	case 0x0a: /* all gpio */
		return 0;
	/* 0x00 : i2s1/spdifo : no gpio */
	/* 0x0c : ssp/spdifo  : no gpio */
	/* 0x0f : ssp/twsi    : no gpio */
	}
	return -ENOTSUPP;
}

/* mpp[52:57] has gpio pins capable of in and out */
static int dove_audio1_ctrl_gpio_dir(struct mvebu_mpp_ctrl_data *data,
				     unsigned pid, bool input)
{
	if (pid < 52 || pid > 57)
		return -ENOTSUPP;
	return 0;
}

static int dove_twsi_ctrl_get(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long *config)
{
	unsigned int gcfg1;
	unsigned int gcfg2;

	regmap_read(gconfmap, GLOBAL_CONFIG_1, &gcfg1);
	regmap_read(gconfmap, GLOBAL_CONFIG_2, &gcfg2);

	*config = 0;
	if (gcfg1 & TWSI_ENABLE_OPTION1)
		*config = 1;
	else if (gcfg2 & TWSI_ENABLE_OPTION2)
		*config = 2;
	else if (gcfg2 & TWSI_ENABLE_OPTION3)
		*config = 3;

	return 0;
}

static int dove_twsi_ctrl_set(struct mvebu_mpp_ctrl_data *data, unsigned pid,
			      unsigned long config)
{
	unsigned int gcfg1 = 0;
	unsigned int gcfg2 = 0;

	switch (config) {
	case 1:
		gcfg1 = TWSI_ENABLE_OPTION1;
		break;
	case 2:
		gcfg2 = TWSI_ENABLE_OPTION2;
		break;
	case 3:
		gcfg2 = TWSI_ENABLE_OPTION3;
		break;
	}

	regmap_update_bits(gconfmap, GLOBAL_CONFIG_1,
			   TWSI_ENABLE_OPTION1,
			   gcfg1);
	regmap_update_bits(gconfmap, GLOBAL_CONFIG_2,
			   TWSI_ENABLE_OPTION2 | TWSI_ENABLE_OPTION3,
			   gcfg2);

	return 0;
}

static const struct mvebu_mpp_ctrl dove_mpp_controls[] = {
	MPP_FUNC_CTRL(0, 15, NULL, dove_pmu_mpp_ctrl),
	MPP_FUNC_CTRL(16, 23, NULL, mvebu_mmio_mpp_ctrl),
	MPP_FUNC_CTRL(24, 39, "mpp_camera", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(40, 45, "mpp_sdio0", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(46, 51, "mpp_sdio1", dove_mpp4_ctrl),
	MPP_FUNC_GPIO_CTRL(52, 57, "mpp_audio1", dove_audio1_ctrl),
	MPP_FUNC_CTRL(58, 61, "mpp_spi0", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(62, 63, "mpp_uart1", dove_mpp4_ctrl),
	MPP_FUNC_CTRL(64, 71, "mpp_nand", dove_nand_ctrl),
	MPP_FUNC_CTRL(72, 72, "audio0", dove_audio0_ctrl),
	MPP_FUNC_CTRL(73, 73, "twsi", dove_twsi_ctrl),
};

static struct mvebu_mpp_mode dove_mpp_modes[] = {
	MPP_MODE(0,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "rts"),
		MPP_FUNCTION(0x03, "sdio0", "cd"),
		MPP_FUNCTION(0x0f, "lcd0", "pwm"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(1,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "cts"),
		MPP_FUNCTION(0x03, "sdio0", "wp"),
		MPP_FUNCTION(0x0f, "lcd1", "pwm"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(2,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "prsnt"),
		MPP_FUNCTION(0x02, "uart2", "txd"),
		MPP_FUNCTION(0x03, "sdio0", "buspwr"),
		MPP_FUNCTION(0x04, "uart1", "rts"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(3,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "act"),
		MPP_FUNCTION(0x02, "uart2", "rxd"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "uart1", "cts"),
		MPP_FUNCTION(0x0f, "lcd-spi", "cs1"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(4,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rts"),
		MPP_FUNCTION(0x03, "sdio1", "cd"),
		MPP_FUNCTION(0x04, "spi1", "miso"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(5,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "cts"),
		MPP_FUNCTION(0x03, "sdio1", "wp"),
		MPP_FUNCTION(0x04, "spi1", "cs"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(6,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "txd"),
		MPP_FUNCTION(0x03, "sdio1", "buspwr"),
		MPP_FUNCTION(0x04, "spi1", "mosi"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(7,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rxd"),
		MPP_FUNCTION(0x03, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x04, "spi1", "sck"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "core-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(8,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "watchdog", "rstout"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(9,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x05, "pex1", "clkreq"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(10,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x05, "ssp", "sclk"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(11,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "prsnt"),
		MPP_FUNCTION(0x02, "sata-1", "act"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x05, "pex0", "clkreq"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(12,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "sata", "act"),
		MPP_FUNCTION(0x02, "uart2", "rts"),
		MPP_FUNCTION(0x03, "audio0", "extclk"),
		MPP_FUNCTION(0x04, "sdio1", "cd"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(13,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "cts"),
		MPP_FUNCTION(0x03, "audio1", "extclk"),
		MPP_FUNCTION(0x04, "sdio1", "wp"),
		MPP_FUNCTION(0x05, "ssp", "extclk"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(14,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "txd"),
		MPP_FUNCTION(0x04, "sdio1", "buspwr"),
		MPP_FUNCTION(0x05, "ssp", "rxd"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(15,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart2", "rxd"),
		MPP_FUNCTION(0x04, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x05, "ssp", "sfrm"),
		MPP_FUNCTION(CONFIG_PMU | 0x0, "pmu-nc", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x1, "pmu-low", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x2, "pmu-high", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x3, "pmic", "sdi"),
		MPP_FUNCTION(CONFIG_PMU | 0x4, "cpu-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x5, "standby-pwr-down", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0x8, "cpu-pwr-good", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xa, "bat-fault", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xb, "ext0-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xc, "ext1-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xd, "ext2-wakeup", NULL),
		MPP_FUNCTION(CONFIG_PMU | 0xe, "pmu-blink", NULL)),
	MPP_MODE(16,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rts"),
		MPP_FUNCTION(0x03, "sdio0", "cd"),
		MPP_FUNCTION(0x04, "lcd-spi", "cs1"),
		MPP_FUNCTION(0x05, "ac97", "sdi1")),
	MPP_MODE(17,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "ac97-1", "sysclko"),
		MPP_FUNCTION(0x02, "uart3", "cts"),
		MPP_FUNCTION(0x03, "sdio0", "wp"),
		MPP_FUNCTION(0x04, "twsi", "sda"),
		MPP_FUNCTION(0x05, "ac97", "sdi2")),
	MPP_MODE(18,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "txd"),
		MPP_FUNCTION(0x03, "sdio0", "buspwr"),
		MPP_FUNCTION(0x04, "lcd0", "pwm"),
		MPP_FUNCTION(0x05, "ac97", "sdi3")),
	MPP_MODE(19,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "uart3", "rxd"),
		MPP_FUNCTION(0x03, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x04, "twsi", "sck")),
	MPP_MODE(20,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "ac97", "sysclko"),
		MPP_FUNCTION(0x02, "lcd-spi", "miso"),
		MPP_FUNCTION(0x03, "sdio1", "cd"),
		MPP_FUNCTION(0x05, "sdio0", "cd"),
		MPP_FUNCTION(0x06, "spi1", "miso")),
	MPP_MODE(21,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "uart1", "rts"),
		MPP_FUNCTION(0x02, "lcd-spi", "cs0"),
		MPP_FUNCTION(0x03, "sdio1", "wp"),
		MPP_FUNCTION(0x04, "ssp", "sfrm"),
		MPP_FUNCTION(0x05, "sdio0", "wp"),
		MPP_FUNCTION(0x06, "spi1", "cs")),
	MPP_MODE(22,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x01, "uart1", "cts"),
		MPP_FUNCTION(0x02, "lcd-spi", "mosi"),
		MPP_FUNCTION(0x03, "sdio1", "buspwr"),
		MPP_FUNCTION(0x04, "ssp", "txd"),
		MPP_FUNCTION(0x05, "sdio0", "buspwr"),
		MPP_FUNCTION(0x06, "spi1", "mosi")),
	MPP_MODE(23,
		MPP_FUNCTION(0x00, "gpio", NULL),
		MPP_FUNCTION(0x02, "lcd-spi", "sck"),
		MPP_FUNCTION(0x03, "sdio1", "ledctrl"),
		MPP_FUNCTION(0x04, "ssp", "sclk"),
		MPP_FUNCTION(0x05, "sdio0", "ledctrl"),
		MPP_FUNCTION(0x06, "spi1", "sck")),
	MPP_MODE(24,
		MPP_FUNCTION(0x00, "camera", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(40,
		MPP_FUNCTION(0x00, "sdio0", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(46,
		MPP_FUNCTION(0x00, "sdio1", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(52,
		MPP_FUNCTION(0x00, "i2s1/spdifo", NULL),
		MPP_FUNCTION(0x02, "i2s1", NULL),
		MPP_FUNCTION(0x08, "spdifo", NULL),
		MPP_FUNCTION(0x0a, "gpio", NULL),
		MPP_FUNCTION(0x0b, "twsi", NULL),
		MPP_FUNCTION(0x0c, "ssp/spdifo", NULL),
		MPP_FUNCTION(0x0e, "ssp", NULL),
		MPP_FUNCTION(0x0f, "ssp/twsi", NULL)),
	MPP_MODE(58,
		MPP_FUNCTION(0x00, "spi0", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(62,
		MPP_FUNCTION(0x00, "uart1", NULL),
		MPP_FUNCTION(0x01, "gpio", NULL)),
	MPP_MODE(64,
		MPP_FUNCTION(0x00, "nand", NULL),
		MPP_FUNCTION(0x01, "gpo", NULL)),
	MPP_MODE(72,
		MPP_FUNCTION(0x00, "i2s", NULL),
		MPP_FUNCTION(0x01, "ac97", NULL)),
	MPP_MODE(73,
		MPP_FUNCTION(0x00, "twsi-none", NULL),
		MPP_FUNCTION(0x01, "twsi-opt1", NULL),
		MPP_FUNCTION(0x02, "twsi-opt2", NULL),
		MPP_FUNCTION(0x03, "twsi-opt3", NULL)),
};

static struct pinctrl_gpio_range dove_mpp_gpio_ranges[] = {
	MPP_GPIO_RANGE(0,  0,  0, 32),
	MPP_GPIO_RANGE(1, 32, 32, 32),
	MPP_GPIO_RANGE(2, 64, 64,  8),
};

static struct mvebu_pinctrl_soc_info dove_pinctrl_info = {
	.controls = dove_mpp_controls,
	.ncontrols = ARRAY_SIZE(dove_mpp_controls),
	.modes = dove_mpp_modes,
	.nmodes = ARRAY_SIZE(dove_mpp_modes),
	.gpioranges = dove_mpp_gpio_ranges,
	.ngpioranges = ARRAY_SIZE(dove_mpp_gpio_ranges),
	.variant = 0,
};

static struct clk *clk;

static const struct of_device_id dove_pinctrl_of_match[] = {
	{ .compatible = "marvell,dove-pinctrl", .data = &dove_pinctrl_info },
	{ }
};

static const struct regmap_config gc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 5,
};

static int dove_pinctrl_probe(struct platform_device *pdev)
{
	struct resource *res, *mpp_res;
	struct resource fb_res;
	const struct of_device_id *match =
		of_match_device(dove_pinctrl_of_match, &pdev->dev);
	struct mvebu_mpp_ctrl_data *mpp_data;
	void __iomem *base;
	int i, ret;

	pdev->dev.platform_data = (void *)match->data;

	/*
	 * General MPP Configuration Register is part of pdma registers.
	 * grab clk to make sure it is ticking.
	 */
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Unable to get pdma clock");
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &mpp_res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err_probe;
	}

	mpp_data = devm_kcalloc(&pdev->dev, dove_pinctrl_info.ncontrols,
				sizeof(*mpp_data), GFP_KERNEL);
	if (!mpp_data) {
		ret = -ENOMEM;
		goto err_probe;
	}

	dove_pinctrl_info.control_data = mpp_data;
	for (i = 0; i < ARRAY_SIZE(dove_mpp_controls); i++)
		mpp_data[i].base = base;

	/* prepare fallback resource */
	memcpy(&fb_res, mpp_res, sizeof(struct resource));
	fb_res.start = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_warn(&pdev->dev, "falling back to hardcoded MPP4 resource\n");
		adjust_resource(&fb_res,
			(mpp_res->start & INT_REGS_MASK) + MPP4_REGS_OFFS, 0x4);
		res = &fb_res;
	}

	mpp4_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mpp4_base)) {
		ret = PTR_ERR(mpp4_base);
		goto err_probe;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_warn(&pdev->dev, "falling back to hardcoded PMU resource\n");
		adjust_resource(&fb_res,
			(mpp_res->start & INT_REGS_MASK) + PMU_REGS_OFFS, 0x8);
		res = &fb_res;
	}

	pmu_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmu_base)) {
		ret = PTR_ERR(pmu_base);
		goto err_probe;
	}

	gconfmap = syscon_regmap_lookup_by_compatible("marvell,dove-global-config");
	if (IS_ERR(gconfmap)) {
		void __iomem *gc_base;

		dev_warn(&pdev->dev, "falling back to hardcoded global registers\n");
		adjust_resource(&fb_res,
			(mpp_res->start & INT_REGS_MASK) + GC_REGS_OFFS, 0x14);
		gc_base = devm_ioremap_resource(&pdev->dev, &fb_res);
		if (IS_ERR(gc_base)) {
			ret = PTR_ERR(gc_base);
			goto err_probe;
		}

		gconfmap = devm_regmap_init_mmio(&pdev->dev,
						 gc_base, &gc_regmap_config);
		if (IS_ERR(gconfmap)) {
			ret = PTR_ERR(gconfmap);
			goto err_probe;
		}
	}

	/* Warn on any missing DT resource */
	if (fb_res.start)
		dev_warn(&pdev->dev, FW_BUG "Missing pinctrl regs in DTB. Please update your firmware.\n");

	return mvebu_pinctrl_probe(pdev);
err_probe:
	clk_disable_unprepare(clk);
	return ret;
}

static struct platform_driver dove_pinctrl_driver = {
	.driver = {
		.name = "dove-pinctrl",
		.suppress_bind_attrs = true,
		.of_match_table = dove_pinctrl_of_match,
	},
	.probe = dove_pinctrl_probe,
};
builtin_platform_driver(dove_pinctrl_driver);
