// SPDX-License-Identifier: GPL-2.0
//
// SPI controller driver for Qualcomm Atheros AR934x/QCA95xx SoCs
//
// Copyright (C) 2020 Chuanhong Guo <gch981213@gmail.com>
//
// Based on spi-mt7621.c:
// Copyright (C) 2011 Sergiy <piratfm@gmail.com>
// Copyright (C) 2011-2013 Gabor Juhos <juhosg@openwrt.org>
// Copyright (C) 2014-2015 Felix Fietkau <nbd@nbd.name>

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>

#define DRIVER_NAME "spi-ar934x"

#define AR934X_SPI_REG_FS		0x00
#define AR934X_SPI_ENABLE		BIT(0)

#define AR934X_SPI_REG_IOC		0x08
#define AR934X_SPI_IOC_INITVAL		0x70000

#define AR934X_SPI_REG_CTRL		0x04
#define AR934X_SPI_CLK_MASK		GENMASK(5, 0)

#define AR934X_SPI_DATAOUT		0x10

#define AR934X_SPI_REG_SHIFT_CTRL	0x14
#define AR934X_SPI_SHIFT_EN		BIT(31)
#define AR934X_SPI_SHIFT_CS(n)		BIT(28 + (n))
#define AR934X_SPI_SHIFT_TERM		26
#define AR934X_SPI_SHIFT_VAL(cs, term, count)			\
	(AR934X_SPI_SHIFT_EN | AR934X_SPI_SHIFT_CS(cs) |	\
	(term) << AR934X_SPI_SHIFT_TERM | (count))

#define AR934X_SPI_DATAIN 0x18

struct ar934x_spi {
	struct spi_controller *ctlr;
	void __iomem *base;
	struct clk *clk;
	unsigned int clk_freq;
};

static inline int ar934x_spi_clk_div(struct ar934x_spi *sp, unsigned int freq)
{
	int div = DIV_ROUND_UP(sp->clk_freq, freq * 2) - 1;

	if (div < 0)
		return 0;
	else if (div > AR934X_SPI_CLK_MASK)
		return -EINVAL;
	else
		return div;
}

static int ar934x_spi_setup(struct spi_device *spi)
{
	struct ar934x_spi *sp = spi_controller_get_devdata(spi->master);

	if ((spi->max_speed_hz == 0) ||
	    (spi->max_speed_hz > (sp->clk_freq / 2))) {
		spi->max_speed_hz = sp->clk_freq / 2;
	} else if (spi->max_speed_hz < (sp->clk_freq / 128)) {
		dev_err(&spi->dev, "spi clock is too low\n");
		return -EINVAL;
	}

	return 0;
}

static int ar934x_spi_transfer_one_message(struct spi_controller *master,
					   struct spi_message *m)
{
	struct ar934x_spi *sp = spi_controller_get_devdata(master);
	struct spi_transfer *t = NULL;
	struct spi_device *spi = m->spi;
	unsigned long trx_done, trx_cur;
	int stat = 0;
	u8 term = 0;
	int div, i;
	u32 reg;
	const u8 *tx_buf;
	u8 *buf;

	m->actual_length = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->speed_hz)
			div = ar934x_spi_clk_div(sp, t->speed_hz);
		else
			div = ar934x_spi_clk_div(sp, spi->max_speed_hz);
		if (div < 0) {
			stat = -EIO;
			goto msg_done;
		}

		reg = ioread32(sp->base + AR934X_SPI_REG_CTRL);
		reg &= ~AR934X_SPI_CLK_MASK;
		reg |= div;
		iowrite32(reg, sp->base + AR934X_SPI_REG_CTRL);
		iowrite32(0, sp->base + AR934X_SPI_DATAOUT);

		for (trx_done = 0; trx_done < t->len; trx_done += 4) {
			trx_cur = t->len - trx_done;
			if (trx_cur > 4)
				trx_cur = 4;
			else if (list_is_last(&t->transfer_list, &m->transfers))
				term = 1;

			if (t->tx_buf) {
				tx_buf = t->tx_buf + trx_done;
				reg = tx_buf[0];
				for (i = 1; i < trx_cur; i++)
					reg = reg << 8 | tx_buf[i];
				iowrite32(reg, sp->base + AR934X_SPI_DATAOUT);
			}

			reg = AR934X_SPI_SHIFT_VAL(spi->chip_select, term,
						   trx_cur * 8);
			iowrite32(reg, sp->base + AR934X_SPI_REG_SHIFT_CTRL);
			stat = readl_poll_timeout(
				sp->base + AR934X_SPI_REG_SHIFT_CTRL, reg,
				!(reg & AR934X_SPI_SHIFT_EN), 0, 5);
			if (stat < 0)
				goto msg_done;

			if (t->rx_buf) {
				reg = ioread32(sp->base + AR934X_SPI_DATAIN);
				buf = t->rx_buf + trx_done;
				for (i = 0; i < trx_cur; i++) {
					buf[trx_cur - i - 1] = reg & 0xff;
					reg >>= 8;
				}
			}
		}
		m->actual_length += t->len;
	}

msg_done:
	m->status = stat;
	spi_finalize_current_message(master);

	return 0;
}

static const struct of_device_id ar934x_spi_match[] = {
	{ .compatible = "qca,ar934x-spi" },
	{},
};
MODULE_DEVICE_TABLE(of, ar934x_spi_match);

static int ar934x_spi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct ar934x_spi *sp;
	void __iomem *base;
	struct clk *clk;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*sp));
	if (!ctlr) {
		dev_info(&pdev->dev, "failed to allocate spi controller\n");
		ret = -ENOMEM;
		goto err_clk_disable;
	}

	/* disable flash mapping and expose spi controller registers */
	iowrite32(AR934X_SPI_ENABLE, base + AR934X_SPI_REG_FS);
	/* restore pins to default state: CSn=1 DO=CLK=0 */
	iowrite32(AR934X_SPI_IOC_INITVAL, base + AR934X_SPI_REG_IOC);

	ctlr->mode_bits = SPI_LSB_FIRST;
	ctlr->setup = ar934x_spi_setup;
	ctlr->transfer_one_message = ar934x_spi_transfer_one_message;
	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->num_chipselect = 3;

	dev_set_drvdata(&pdev->dev, ctlr);

	sp = spi_controller_get_devdata(ctlr);
	sp->base = base;
	sp->clk = clk;
	sp->clk_freq = clk_get_rate(clk);
	sp->ctlr = ctlr;

	ret = spi_register_controller(ctlr);
	if (!ret)
		return 0;

err_clk_disable:
	clk_disable_unprepare(clk);
	return ret;
}

static int ar934x_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct ar934x_spi *sp;

	ctlr = dev_get_drvdata(&pdev->dev);
	sp = spi_controller_get_devdata(ctlr);

	spi_unregister_controller(ctlr);
	clk_disable_unprepare(sp->clk);

	return 0;
}

static struct platform_driver ar934x_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ar934x_spi_match,
	},
	.probe = ar934x_spi_probe,
	.remove = ar934x_spi_remove,
};

module_platform_driver(ar934x_spi_driver);

MODULE_DESCRIPTION("SPI controller driver for Qualcomm Atheros AR934x/QCA95xx");
MODULE_AUTHOR("Chuanhong Guo <gch981213@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
