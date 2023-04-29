// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI controller driver for the Mikrotik RB4xx boards
 *
 * Copyright (C) 2010 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
 *
 * This file was based on the patches for Linux 2.6.27.39 published by
 * MikroTik for their RouterBoard 4xx series devices.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include <asm/mach-ath79/ar71xx_regs.h>

struct rb4xx_spi {
	void __iomem *base;
	struct clk *clk;
};

static inline u32 rb4xx_read(struct rb4xx_spi *rbspi, u32 reg)
{
	return __raw_readl(rbspi->base + reg);
}

static inline void rb4xx_write(struct rb4xx_spi *rbspi, u32 reg, u32 value)
{
	__raw_writel(value, rbspi->base + reg);
}

static inline void do_spi_clk(struct rb4xx_spi *rbspi, u32 spi_ioc, int value)
{
	u32 regval;

	regval = spi_ioc;
	if (value & BIT(0))
		regval |= AR71XX_SPI_IOC_DO;

	rb4xx_write(rbspi, AR71XX_SPI_REG_IOC, regval);
	rb4xx_write(rbspi, AR71XX_SPI_REG_IOC, regval | AR71XX_SPI_IOC_CLK);
}

static void do_spi_byte(struct rb4xx_spi *rbspi, u32 spi_ioc, u8 byte)
{
	int i;

	for (i = 7; i >= 0; i--)
		do_spi_clk(rbspi, spi_ioc, byte >> i);
}

/* The CS2 pin is used to clock in a second bit per clock cycle. */
static inline void do_spi_clk_two(struct rb4xx_spi *rbspi, u32 spi_ioc,
				   u8 value)
{
	u32 regval;

	regval = spi_ioc;
	if (value & BIT(1))
		regval |= AR71XX_SPI_IOC_DO;
	if (value & BIT(0))
		regval |= AR71XX_SPI_IOC_CS2;

	rb4xx_write(rbspi, AR71XX_SPI_REG_IOC, regval);
	rb4xx_write(rbspi, AR71XX_SPI_REG_IOC, regval | AR71XX_SPI_IOC_CLK);
}

/* Two bits at a time, msb first */
static void do_spi_byte_two(struct rb4xx_spi *rbspi, u32 spi_ioc, u8 byte)
{
	do_spi_clk_two(rbspi, spi_ioc, byte >> 6);
	do_spi_clk_two(rbspi, spi_ioc, byte >> 4);
	do_spi_clk_two(rbspi, spi_ioc, byte >> 2);
	do_spi_clk_two(rbspi, spi_ioc, byte >> 0);
}

static void rb4xx_set_cs(struct spi_device *spi, bool enable)
{
	struct rb4xx_spi *rbspi = spi_master_get_devdata(spi->master);

	/*
	 * Setting CS is done along with bitbanging the actual values,
	 * since it's all on the same hardware register. However the
	 * CPLD needs CS deselected after every command.
	 */
	if (enable)
		rb4xx_write(rbspi, AR71XX_SPI_REG_IOC,
			    AR71XX_SPI_IOC_CS0 | AR71XX_SPI_IOC_CS1);
}

static int rb4xx_transfer_one(struct spi_master *master,
			      struct spi_device *spi, struct spi_transfer *t)
{
	struct rb4xx_spi *rbspi = spi_master_get_devdata(master);
	int i;
	u32 spi_ioc;
	u8 *rx_buf;
	const u8 *tx_buf;

	/*
	 * Prime the SPI register with the SPI device selected. The m25p80 boot
	 * flash and CPLD share the CS0 pin. This works because the CPLD's
	 * command set was designed to almost not clash with that of the
	 * boot flash.
	 */
	if (spi_get_chipselect(spi, 0) == 2)
		/* MMC */
		spi_ioc = AR71XX_SPI_IOC_CS0;
	else
		/* Boot flash and CPLD */
		spi_ioc = AR71XX_SPI_IOC_CS1;

	tx_buf = t->tx_buf;
	rx_buf = t->rx_buf;
	for (i = 0; i < t->len; ++i) {
		if (t->tx_nbits == SPI_NBITS_DUAL)
			/* CPLD can use two-wire transfers */
			do_spi_byte_two(rbspi, spi_ioc, tx_buf[i]);
		else
			do_spi_byte(rbspi, spi_ioc, tx_buf[i]);
		if (!rx_buf)
			continue;
		rx_buf[i] = rb4xx_read(rbspi, AR71XX_SPI_REG_RDS);
	}
	spi_finalize_current_transfer(master);

	return 0;
}

static int rb4xx_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct clk *ahb_clk;
	struct rb4xx_spi *rbspi;
	int err;
	void __iomem *spi_base;

	spi_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi_base))
		return PTR_ERR(spi_base);

	master = devm_spi_alloc_master(&pdev->dev, sizeof(*rbspi));
	if (!master)
		return -ENOMEM;

	ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(ahb_clk))
		return PTR_ERR(ahb_clk);

	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = 0;
	master->num_chipselect = 3;
	master->mode_bits = SPI_TX_DUAL;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->flags = SPI_MASTER_MUST_TX;
	master->transfer_one = rb4xx_transfer_one;
	master->set_cs = rb4xx_set_cs;

	rbspi = spi_master_get_devdata(master);
	rbspi->base = spi_base;
	rbspi->clk = ahb_clk;
	platform_set_drvdata(pdev, rbspi);

	err = devm_spi_register_master(&pdev->dev, master);
	if (err) {
		dev_err(&pdev->dev, "failed to register SPI master\n");
		return err;
	}

	err = clk_prepare_enable(ahb_clk);
	if (err)
		return err;

	/* Enable SPI */
	rb4xx_write(rbspi, AR71XX_SPI_REG_FS, AR71XX_SPI_FS_GPIO);

	return 0;
}

static void rb4xx_spi_remove(struct platform_device *pdev)
{
	struct rb4xx_spi *rbspi = platform_get_drvdata(pdev);

	clk_disable_unprepare(rbspi->clk);
}

static const struct of_device_id rb4xx_spi_dt_match[] = {
	{ .compatible = "mikrotik,rb4xx-spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, rb4xx_spi_dt_match);

static struct platform_driver rb4xx_spi_drv = {
	.probe = rb4xx_spi_probe,
	.remove_new = rb4xx_spi_remove,
	.driver = {
		.name = "rb4xx-spi",
		.of_match_table = of_match_ptr(rb4xx_spi_dt_match),
	},
};

module_platform_driver(rb4xx_spi_drv);

MODULE_DESCRIPTION("Mikrotik RB4xx SPI controller driver");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org>");
MODULE_AUTHOR("Bert Vermeulen <bert@biot.com>");
MODULE_LICENSE("GPL v2");
