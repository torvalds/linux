/*
 * Marvell Orion SPI controller driver
 *
 * Author: Shadi Ammouri <shadi@marvell.com>
 * Copyright (C) 2007-2008 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/sizes.h>
#include <linux/gpio.h>
#include <asm/unaligned.h>

#define DRIVER_NAME			"orion_spi"

/* Runtime PM autosuspend timeout: PM is fairly light on this driver */
#define SPI_AUTOSUSPEND_TIMEOUT		200

/* Some SoCs using this driver support up to 8 chip selects.
 * It is up to the implementer to only use the chip selects
 * that are available.
 */
#define ORION_NUM_CHIPSELECTS		8

#define ORION_SPI_WAIT_RDY_MAX_LOOP	2000 /* in usec */

#define ORION_SPI_IF_CTRL_REG		0x00
#define ORION_SPI_IF_CONFIG_REG		0x04
#define ORION_SPI_IF_RXLSBF		BIT(14)
#define ORION_SPI_IF_TXLSBF		BIT(13)
#define ORION_SPI_DATA_OUT_REG		0x08
#define ORION_SPI_DATA_IN_REG		0x0c
#define ORION_SPI_INT_CAUSE_REG		0x10
#define ORION_SPI_TIMING_PARAMS_REG	0x18

/* Register for the "Direct Mode" */
#define SPI_DIRECT_WRITE_CONFIG_REG	0x20

#define ORION_SPI_TMISO_SAMPLE_MASK	(0x3 << 6)
#define ORION_SPI_TMISO_SAMPLE_1	(1 << 6)
#define ORION_SPI_TMISO_SAMPLE_2	(2 << 6)

#define ORION_SPI_MODE_CPOL		(1 << 11)
#define ORION_SPI_MODE_CPHA		(1 << 12)
#define ORION_SPI_IF_8_16_BIT_MODE	(1 << 5)
#define ORION_SPI_CLK_PRESCALE_MASK	0x1F
#define ARMADA_SPI_CLK_PRESCALE_MASK	0xDF
#define ORION_SPI_MODE_MASK		(ORION_SPI_MODE_CPOL | \
					 ORION_SPI_MODE_CPHA)
#define ORION_SPI_CS_MASK	0x1C
#define ORION_SPI_CS_SHIFT	2
#define ORION_SPI_CS(cs)	((cs << ORION_SPI_CS_SHIFT) & \
					ORION_SPI_CS_MASK)

enum orion_spi_type {
	ORION_SPI,
	ARMADA_SPI,
};

struct orion_spi_dev {
	enum orion_spi_type	typ;
	/*
	 * min_divisor and max_hz should be exclusive, the only we can
	 * have both is for managing the armada-370-spi case with old
	 * device tree
	 */
	unsigned long		max_hz;
	unsigned int		min_divisor;
	unsigned int		max_divisor;
	u32			prescale_mask;
	bool			is_errata_50mhz_ac;
};

struct orion_direct_acc {
	void __iomem		*vaddr;
	u32			size;
};

struct orion_child_options {
	struct orion_direct_acc direct_access;
};

struct orion_spi {
	struct spi_master	*master;
	void __iomem		*base;
	struct clk              *clk;
	struct clk              *axi_clk;
	const struct orion_spi_dev *devdata;
	int			unused_hw_gpio;

	struct orion_child_options	child[ORION_NUM_CHIPSELECTS];
};

static inline void __iomem *spi_reg(struct orion_spi *orion_spi, u32 reg)
{
	return orion_spi->base + reg;
}

static inline void
orion_spi_setbits(struct orion_spi *orion_spi, u32 reg, u32 mask)
{
	void __iomem *reg_addr = spi_reg(orion_spi, reg);
	u32 val;

	val = readl(reg_addr);
	val |= mask;
	writel(val, reg_addr);
}

static inline void
orion_spi_clrbits(struct orion_spi *orion_spi, u32 reg, u32 mask)
{
	void __iomem *reg_addr = spi_reg(orion_spi, reg);
	u32 val;

	val = readl(reg_addr);
	val &= ~mask;
	writel(val, reg_addr);
}

static int orion_spi_baudrate_set(struct spi_device *spi, unsigned int speed)
{
	u32 tclk_hz;
	u32 rate;
	u32 prescale;
	u32 reg;
	struct orion_spi *orion_spi;
	const struct orion_spi_dev *devdata;

	orion_spi = spi_master_get_devdata(spi->master);
	devdata = orion_spi->devdata;

	tclk_hz = clk_get_rate(orion_spi->clk);

	if (devdata->typ == ARMADA_SPI) {
		/*
		 * Given the core_clk (tclk_hz) and the target rate (speed) we
		 * determine the best values for SPR (in [0 .. 15]) and SPPR (in
		 * [0..7]) such that
		 *
		 * 	core_clk / (SPR * 2 ** SPPR)
		 *
		 * is as big as possible but not bigger than speed.
		 */

		/* best integer divider: */
		unsigned divider = DIV_ROUND_UP(tclk_hz, speed);
		unsigned spr, sppr;

		if (divider < 16) {
			/* This is the easy case, divider is less than 16 */
			spr = divider;
			sppr = 0;

		} else {
			unsigned two_pow_sppr;
			/*
			 * Find the highest bit set in divider. This and the
			 * three next bits define SPR (apart from rounding).
			 * SPPR is then the number of zero bits that must be
			 * appended:
			 */
			sppr = fls(divider) - 4;

			/*
			 * As SPR only has 4 bits, we have to round divider up
			 * to the next multiple of 2 ** sppr.
			 */
			two_pow_sppr = 1 << sppr;
			divider = (divider + two_pow_sppr - 1) & -two_pow_sppr;

			/*
			 * recalculate sppr as rounding up divider might have
			 * increased it enough to change the position of the
			 * highest set bit. In this case the bit that now
			 * doesn't make it into SPR is 0, so there is no need to
			 * round again.
			 */
			sppr = fls(divider) - 4;
			spr = divider >> sppr;

			/*
			 * Now do range checking. SPR is constructed to have a
			 * width of 4 bits, so this is fine for sure. So we
			 * still need to check for sppr to fit into 3 bits:
			 */
			if (sppr > 7)
				return -EINVAL;
		}

		prescale = ((sppr & 0x6) << 5) | ((sppr & 0x1) << 4) | spr;
	} else {
		/*
		 * the supported rates are: 4,6,8...30
		 * round up as we look for equal or less speed
		 */
		rate = DIV_ROUND_UP(tclk_hz, speed);
		rate = roundup(rate, 2);

		/* check if requested speed is too small */
		if (rate > 30)
			return -EINVAL;

		if (rate < 4)
			rate = 4;

		/* Convert the rate to SPI clock divisor value.	*/
		prescale = 0x10 + rate/2;
	}

	reg = readl(spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
	reg = ((reg & ~devdata->prescale_mask) | prescale);
	writel(reg, spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));

	return 0;
}

static void
orion_spi_mode_set(struct spi_device *spi)
{
	u32 reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);

	reg = readl(spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
	reg &= ~ORION_SPI_MODE_MASK;
	if (spi->mode & SPI_CPOL)
		reg |= ORION_SPI_MODE_CPOL;
	if (spi->mode & SPI_CPHA)
		reg |= ORION_SPI_MODE_CPHA;
	if (spi->mode & SPI_LSB_FIRST)
		reg |= ORION_SPI_IF_RXLSBF | ORION_SPI_IF_TXLSBF;
	else
		reg &= ~(ORION_SPI_IF_RXLSBF | ORION_SPI_IF_TXLSBF);

	writel(reg, spi_reg(orion_spi, ORION_SPI_IF_CONFIG_REG));
}

static void
orion_spi_50mhz_ac_timing_erratum(struct spi_device *spi, unsigned int speed)
{
	u32 reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);

	/*
	 * Erratum description: (Erratum NO. FE-9144572) The device
	 * SPI interface supports frequencies of up to 50 MHz.
	 * However, due to this erratum, when the device core clock is
	 * 250 MHz and the SPI interfaces is configured for 50MHz SPI
	 * clock and CPOL=CPHA=1 there might occur data corruption on
	 * reads from the SPI device.
	 * Erratum Workaround:
	 * Work in one of the following configurations:
	 * 1. Set CPOL=CPHA=0 in "SPI Interface Configuration
	 * Register".
	 * 2. Set TMISO_SAMPLE value to 0x2 in "SPI Timing Parameters 1
	 * Register" before setting the interface.
	 */
	reg = readl(spi_reg(orion_spi, ORION_SPI_TIMING_PARAMS_REG));
	reg &= ~ORION_SPI_TMISO_SAMPLE_MASK;

	if (clk_get_rate(orion_spi->clk) == 250000000 &&
			speed == 50000000 && spi->mode & SPI_CPOL &&
			spi->mode & SPI_CPHA)
		reg |= ORION_SPI_TMISO_SAMPLE_2;
	else
		reg |= ORION_SPI_TMISO_SAMPLE_1; /* This is the default value */

	writel(reg, spi_reg(orion_spi, ORION_SPI_TIMING_PARAMS_REG));
}

/*
 * called only when no transfer is active on the bus
 */
static int
orion_spi_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct orion_spi *orion_spi;
	unsigned int speed = spi->max_speed_hz;
	unsigned int bits_per_word = spi->bits_per_word;
	int	rc;

	orion_spi = spi_master_get_devdata(spi->master);

	if ((t != NULL) && t->speed_hz)
		speed = t->speed_hz;

	if ((t != NULL) && t->bits_per_word)
		bits_per_word = t->bits_per_word;

	orion_spi_mode_set(spi);

	if (orion_spi->devdata->is_errata_50mhz_ac)
		orion_spi_50mhz_ac_timing_erratum(spi, speed);

	rc = orion_spi_baudrate_set(spi, speed);
	if (rc)
		return rc;

	if (bits_per_word == 16)
		orion_spi_setbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);
	else
		orion_spi_clrbits(orion_spi, ORION_SPI_IF_CONFIG_REG,
				  ORION_SPI_IF_8_16_BIT_MODE);

	return 0;
}

static void orion_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct orion_spi *orion_spi;
	int cs;

	orion_spi = spi_master_get_devdata(spi->master);

	if (gpio_is_valid(spi->cs_gpio))
		cs = orion_spi->unused_hw_gpio;
	else
		cs = spi->chip_select;

	orion_spi_clrbits(orion_spi, ORION_SPI_IF_CTRL_REG, ORION_SPI_CS_MASK);
	orion_spi_setbits(orion_spi, ORION_SPI_IF_CTRL_REG,
				ORION_SPI_CS(cs));

	/* Chip select logic is inverted from spi_set_cs */
	if (!enable)
		orion_spi_setbits(orion_spi, ORION_SPI_IF_CTRL_REG, 0x1);
	else
		orion_spi_clrbits(orion_spi, ORION_SPI_IF_CTRL_REG, 0x1);
}

static inline int orion_spi_wait_till_ready(struct orion_spi *orion_spi)
{
	int i;

	for (i = 0; i < ORION_SPI_WAIT_RDY_MAX_LOOP; i++) {
		if (readl(spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG)))
			return 1;

		udelay(1);
	}

	return -1;
}

static inline int
orion_spi_write_read_8bit(struct spi_device *spi,
			  const u8 **tx_buf, u8 **rx_buf)
{
	void __iomem *tx_reg, *rx_reg, *int_reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);
	tx_reg = spi_reg(orion_spi, ORION_SPI_DATA_OUT_REG);
	rx_reg = spi_reg(orion_spi, ORION_SPI_DATA_IN_REG);
	int_reg = spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG);

	/* clear the interrupt cause register */
	writel(0x0, int_reg);

	if (tx_buf && *tx_buf)
		writel(*(*tx_buf)++, tx_reg);
	else
		writel(0, tx_reg);

	if (orion_spi_wait_till_ready(orion_spi) < 0) {
		dev_err(&spi->dev, "TXS timed out\n");
		return -1;
	}

	if (rx_buf && *rx_buf)
		*(*rx_buf)++ = readl(rx_reg);

	return 1;
}

static inline int
orion_spi_write_read_16bit(struct spi_device *spi,
			   const u16 **tx_buf, u16 **rx_buf)
{
	void __iomem *tx_reg, *rx_reg, *int_reg;
	struct orion_spi *orion_spi;

	orion_spi = spi_master_get_devdata(spi->master);
	tx_reg = spi_reg(orion_spi, ORION_SPI_DATA_OUT_REG);
	rx_reg = spi_reg(orion_spi, ORION_SPI_DATA_IN_REG);
	int_reg = spi_reg(orion_spi, ORION_SPI_INT_CAUSE_REG);

	/* clear the interrupt cause register */
	writel(0x0, int_reg);

	if (tx_buf && *tx_buf)
		writel(__cpu_to_le16(get_unaligned((*tx_buf)++)), tx_reg);
	else
		writel(0, tx_reg);

	if (orion_spi_wait_till_ready(orion_spi) < 0) {
		dev_err(&spi->dev, "TXS timed out\n");
		return -1;
	}

	if (rx_buf && *rx_buf)
		put_unaligned(__le16_to_cpu(readl(rx_reg)), (*rx_buf)++);

	return 1;
}

static unsigned int
orion_spi_write_read(struct spi_device *spi, struct spi_transfer *xfer)
{
	unsigned int count;
	int word_len;
	struct orion_spi *orion_spi;
	int cs = spi->chip_select;

	word_len = spi->bits_per_word;
	count = xfer->len;

	orion_spi = spi_master_get_devdata(spi->master);

	/*
	 * Use SPI direct write mode if base address is available. Otherwise
	 * fall back to PIO mode for this transfer.
	 */
	if ((orion_spi->child[cs].direct_access.vaddr) && (xfer->tx_buf) &&
	    (word_len == 8)) {
		unsigned int cnt = count / 4;
		unsigned int rem = count % 4;

		/*
		 * Send the TX-data to the SPI device via the direct
		 * mapped address window
		 */
		iowrite32_rep(orion_spi->child[cs].direct_access.vaddr,
			      xfer->tx_buf, cnt);
		if (rem) {
			u32 *buf = (u32 *)xfer->tx_buf;

			iowrite8_rep(orion_spi->child[cs].direct_access.vaddr,
				     &buf[cnt], rem);
		}

		return count;
	}

	if (word_len == 8) {
		const u8 *tx = xfer->tx_buf;
		u8 *rx = xfer->rx_buf;

		do {
			if (orion_spi_write_read_8bit(spi, &tx, &rx) < 0)
				goto out;
			count--;
		} while (count);
	} else if (word_len == 16) {
		const u16 *tx = xfer->tx_buf;
		u16 *rx = xfer->rx_buf;

		do {
			if (orion_spi_write_read_16bit(spi, &tx, &rx) < 0)
				goto out;
			count -= 2;
		} while (count);
	}

out:
	return xfer->len - count;
}

static int orion_spi_transfer_one(struct spi_master *master,
					struct spi_device *spi,
					struct spi_transfer *t)
{
	int status = 0;

	status = orion_spi_setup_transfer(spi, t);
	if (status < 0)
		return status;

	if (t->len)
		orion_spi_write_read(spi, t);

	return status;
}

static int orion_spi_setup(struct spi_device *spi)
{
	if (gpio_is_valid(spi->cs_gpio)) {
		gpio_direction_output(spi->cs_gpio, !(spi->mode & SPI_CS_HIGH));
	}
	return orion_spi_setup_transfer(spi, NULL);
}

static int orion_spi_reset(struct orion_spi *orion_spi)
{
	/* Verify that the CS is deasserted */
	orion_spi_clrbits(orion_spi, ORION_SPI_IF_CTRL_REG, 0x1);

	/* Don't deassert CS between the direct mapped SPI transfers */
	writel(0, spi_reg(orion_spi, SPI_DIRECT_WRITE_CONFIG_REG));

	return 0;
}

static const struct orion_spi_dev orion_spi_dev_data = {
	.typ = ORION_SPI,
	.min_divisor = 4,
	.max_divisor = 30,
	.prescale_mask = ORION_SPI_CLK_PRESCALE_MASK,
};

static const struct orion_spi_dev armada_370_spi_dev_data = {
	.typ = ARMADA_SPI,
	.min_divisor = 4,
	.max_divisor = 1920,
	.max_hz = 50000000,
	.prescale_mask = ARMADA_SPI_CLK_PRESCALE_MASK,
};

static const struct orion_spi_dev armada_xp_spi_dev_data = {
	.typ = ARMADA_SPI,
	.max_hz = 50000000,
	.max_divisor = 1920,
	.prescale_mask = ARMADA_SPI_CLK_PRESCALE_MASK,
};

static const struct orion_spi_dev armada_375_spi_dev_data = {
	.typ = ARMADA_SPI,
	.min_divisor = 15,
	.max_divisor = 1920,
	.prescale_mask = ARMADA_SPI_CLK_PRESCALE_MASK,
};

static const struct orion_spi_dev armada_380_spi_dev_data = {
	.typ = ARMADA_SPI,
	.max_hz = 50000000,
	.max_divisor = 1920,
	.prescale_mask = ARMADA_SPI_CLK_PRESCALE_MASK,
	.is_errata_50mhz_ac = true,
};

static const struct of_device_id orion_spi_of_match_table[] = {
	{
		.compatible = "marvell,orion-spi",
		.data = &orion_spi_dev_data,
	},
	{
		.compatible = "marvell,armada-370-spi",
		.data = &armada_370_spi_dev_data,
	},
	{
		.compatible = "marvell,armada-375-spi",
		.data = &armada_375_spi_dev_data,
	},
	{
		.compatible = "marvell,armada-380-spi",
		.data = &armada_380_spi_dev_data,
	},
	{
		.compatible = "marvell,armada-390-spi",
		.data = &armada_xp_spi_dev_data,
	},
	{
		.compatible = "marvell,armada-xp-spi",
		.data = &armada_xp_spi_dev_data,
	},

	{}
};
MODULE_DEVICE_TABLE(of, orion_spi_of_match_table);

static int orion_spi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	const struct orion_spi_dev *devdata;
	struct spi_master *master;
	struct orion_spi *spi;
	struct resource *r;
	unsigned long tclk_hz;
	int status = 0;
	struct device_node *np;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi));
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	if (pdev->id != -1)
		master->bus_num = pdev->id;
	if (pdev->dev.of_node) {
		u32 cell_index;

		if (!of_property_read_u32(pdev->dev.of_node, "cell-index",
					  &cell_index))
			master->bus_num = cell_index;
	}

	/* we support all 4 SPI modes and LSB first option */
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_LSB_FIRST;
	master->set_cs = orion_spi_set_cs;
	master->transfer_one = orion_spi_transfer_one;
	master->num_chipselect = ORION_NUM_CHIPSELECTS;
	master->setup = orion_spi_setup;
	master->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	master->auto_runtime_pm = true;
	master->flags = SPI_MASTER_GPIO_SS;

	platform_set_drvdata(pdev, master);

	spi = spi_master_get_devdata(master);
	spi->master = master;
	spi->unused_hw_gpio = -1;

	of_id = of_match_device(orion_spi_of_match_table, &pdev->dev);
	devdata = (of_id) ? of_id->data : &orion_spi_dev_data;
	spi->devdata = devdata;

	spi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		status = PTR_ERR(spi->clk);
		goto out;
	}

	status = clk_prepare_enable(spi->clk);
	if (status)
		goto out;

	/* The following clock is only used by some SoCs */
	spi->axi_clk = devm_clk_get(&pdev->dev, "axi");
	if (IS_ERR(spi->axi_clk) &&
	    PTR_ERR(spi->axi_clk) == -EPROBE_DEFER) {
		status = -EPROBE_DEFER;
		goto out_rel_clk;
	}
	if (!IS_ERR(spi->axi_clk))
		clk_prepare_enable(spi->axi_clk);

	tclk_hz = clk_get_rate(spi->clk);

	/*
	 * With old device tree, armada-370-spi could be used with
	 * Armada XP, however for this SoC the maximum frequency is
	 * 50MHz instead of tclk/4. On Armada 370, tclk cannot be
	 * higher than 200MHz. So, in order to be able to handle both
	 * SoCs, we can take the minimum of 50MHz and tclk/4.
	 */
	if (of_device_is_compatible(pdev->dev.of_node,
					"marvell,armada-370-spi"))
		master->max_speed_hz = min(devdata->max_hz,
				DIV_ROUND_UP(tclk_hz, devdata->min_divisor));
	else if (devdata->min_divisor)
		master->max_speed_hz =
			DIV_ROUND_UP(tclk_hz, devdata->min_divisor);
	else
		master->max_speed_hz = devdata->max_hz;
	master->min_speed_hz = DIV_ROUND_UP(tclk_hz, devdata->max_divisor);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(spi->base)) {
		status = PTR_ERR(spi->base);
		goto out_rel_axi_clk;
	}

	/* Scan all SPI devices of this controller for direct mapped devices */
	for_each_available_child_of_node(pdev->dev.of_node, np) {
		u32 cs;

		/* Get chip-select number from the "reg" property */
		status = of_property_read_u32(np, "reg", &cs);
		if (status) {
			dev_err(&pdev->dev,
				"%pOF has no valid 'reg' property (%d)\n",
				np, status);
			continue;
		}

		/*
		 * Check if an address is configured for this SPI device. If
		 * not, the MBus mapping via the 'ranges' property in the 'soc'
		 * node is not configured and this device should not use the
		 * direct mode. In this case, just continue with the next
		 * device.
		 */
		status = of_address_to_resource(pdev->dev.of_node, cs + 1, r);
		if (status)
			continue;

		/*
		 * Only map one page for direct access. This is enough for the
		 * simple TX transfer which only writes to the first word.
		 * This needs to get extended for the direct SPI-NOR / SPI-NAND
		 * support, once this gets implemented.
		 */
		spi->child[cs].direct_access.vaddr = devm_ioremap(&pdev->dev,
							    r->start,
							    PAGE_SIZE);
		if (!spi->child[cs].direct_access.vaddr) {
			status = -ENOMEM;
			goto out_rel_axi_clk;
		}
		spi->child[cs].direct_access.size = PAGE_SIZE;

		dev_info(&pdev->dev, "CS%d configured for direct access\n", cs);
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_enable(&pdev->dev);

	status = orion_spi_reset(spi);
	if (status < 0)
		goto out_rel_pm;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	master->dev.of_node = pdev->dev.of_node;
	status = spi_register_master(master);
	if (status < 0)
		goto out_rel_pm;

	if (master->cs_gpios) {
		int i;
		for (i = 0; i < master->num_chipselect; ++i) {
			char *gpio_name;

			if (!gpio_is_valid(master->cs_gpios[i])) {
				continue;
			}

			gpio_name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					"%s-CS%d", dev_name(&pdev->dev), i);
			if (!gpio_name) {
				status = -ENOMEM;
				goto out_rel_master;
			}

			status = devm_gpio_request(&pdev->dev,
					master->cs_gpios[i], gpio_name);
			if (status) {
				dev_err(&pdev->dev,
					"Can't request GPIO for CS %d\n",
					master->cs_gpios[i]);
				goto out_rel_master;
			}
			if (spi->unused_hw_gpio == -1) {
				dev_info(&pdev->dev,
					"Selected unused HW CS#%d for any GPIO CSes\n",
					i);
				spi->unused_hw_gpio = i;
			}
		}
	}


	return status;

out_rel_master:
	spi_unregister_master(master);
out_rel_pm:
	pm_runtime_disable(&pdev->dev);
out_rel_axi_clk:
	clk_disable_unprepare(spi->axi_clk);
out_rel_clk:
	clk_disable_unprepare(spi->clk);
out:
	spi_master_put(master);
	return status;
}


static int orion_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	pm_runtime_get_sync(&pdev->dev);
	clk_disable_unprepare(spi->axi_clk);
	clk_disable_unprepare(spi->clk);

	spi_unregister_master(master);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

#ifdef CONFIG_PM
static int orion_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	clk_disable_unprepare(spi->axi_clk);
	clk_disable_unprepare(spi->clk);
	return 0;
}

static int orion_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct orion_spi *spi = spi_master_get_devdata(master);

	if (!IS_ERR(spi->axi_clk))
		clk_prepare_enable(spi->axi_clk);
	return clk_prepare_enable(spi->clk);
}
#endif

static const struct dev_pm_ops orion_spi_pm_ops = {
	SET_RUNTIME_PM_OPS(orion_spi_runtime_suspend,
			   orion_spi_runtime_resume,
			   NULL)
};

static struct platform_driver orion_spi_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm	= &orion_spi_pm_ops,
		.of_match_table = of_match_ptr(orion_spi_of_match_table),
	},
	.probe		= orion_spi_probe,
	.remove		= orion_spi_remove,
};

module_platform_driver(orion_spi_driver);

MODULE_DESCRIPTION("Orion SPI driver");
MODULE_AUTHOR("Shadi Ammouri <shadi@marvell.com>");
MODULE_LICENSE("GPL");
