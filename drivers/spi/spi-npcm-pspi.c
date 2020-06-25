// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Nuvoton Technology corporation.

#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/reset.h>

#include <asm/unaligned.h>

#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

struct npcm_pspi {
	struct completion xfer_done;
	struct reset_control *reset;
	struct spi_master *master;
	unsigned int tx_bytes;
	unsigned int rx_bytes;
	void __iomem *base;
	bool is_save_param;
	u8 bits_per_word;
	const u8 *tx_buf;
	struct clk *clk;
	u32 speed_hz;
	u8 *rx_buf;
	u16 mode;
	u32 id;
};

#define DRIVER_NAME "npcm-pspi"

#define NPCM_PSPI_DATA		0x00
#define NPCM_PSPI_CTL1		0x02
#define NPCM_PSPI_STAT		0x04

/* definitions for control and status register */
#define NPCM_PSPI_CTL1_SPIEN	BIT(0)
#define NPCM_PSPI_CTL1_MOD	BIT(2)
#define NPCM_PSPI_CTL1_EIR	BIT(5)
#define NPCM_PSPI_CTL1_EIW	BIT(6)
#define NPCM_PSPI_CTL1_SCM	BIT(7)
#define NPCM_PSPI_CTL1_SCIDL	BIT(8)
#define NPCM_PSPI_CTL1_SCDV6_0	GENMASK(15, 9)

#define NPCM_PSPI_STAT_BSY	BIT(0)
#define NPCM_PSPI_STAT_RBF	BIT(1)

/* general definitions */
#define NPCM_PSPI_TIMEOUT_MS		2000
#define NPCM_PSPI_MAX_CLK_DIVIDER	256
#define NPCM_PSPI_MIN_CLK_DIVIDER	4
#define NPCM_PSPI_DEFAULT_CLK		25000000

static inline unsigned int bytes_per_word(unsigned int bits)
{
	return bits <= 8 ? 1 : 2;
}

static inline void npcm_pspi_irq_enable(struct npcm_pspi *priv, u16 mask)
{
	u16 val;

	val = ioread16(priv->base + NPCM_PSPI_CTL1);
	val |= mask;
	iowrite16(val, priv->base + NPCM_PSPI_CTL1);
}

static inline void npcm_pspi_irq_disable(struct npcm_pspi *priv, u16 mask)
{
	u16 val;

	val = ioread16(priv->base + NPCM_PSPI_CTL1);
	val &= ~mask;
	iowrite16(val, priv->base + NPCM_PSPI_CTL1);
}

static inline void npcm_pspi_enable(struct npcm_pspi *priv)
{
	u16 val;

	val = ioread16(priv->base + NPCM_PSPI_CTL1);
	val |= NPCM_PSPI_CTL1_SPIEN;
	iowrite16(val, priv->base + NPCM_PSPI_CTL1);
}

static inline void npcm_pspi_disable(struct npcm_pspi *priv)
{
	u16 val;

	val = ioread16(priv->base + NPCM_PSPI_CTL1);
	val &= ~NPCM_PSPI_CTL1_SPIEN;
	iowrite16(val, priv->base + NPCM_PSPI_CTL1);
}

static void npcm_pspi_set_mode(struct spi_device *spi)
{
	struct npcm_pspi *priv = spi_master_get_devdata(spi->master);
	u16 regtemp;
	u16 mode_val;

	switch (spi->mode & (SPI_CPOL | SPI_CPHA)) {
	case SPI_MODE_0:
		mode_val = 0;
		break;
	case SPI_MODE_1:
		mode_val = NPCM_PSPI_CTL1_SCIDL;
		break;
	case SPI_MODE_2:
		mode_val = NPCM_PSPI_CTL1_SCM;
		break;
	case SPI_MODE_3:
		mode_val = NPCM_PSPI_CTL1_SCIDL | NPCM_PSPI_CTL1_SCM;
		break;
	}

	regtemp = ioread16(priv->base + NPCM_PSPI_CTL1);
	regtemp &= ~(NPCM_PSPI_CTL1_SCM | NPCM_PSPI_CTL1_SCIDL);
	iowrite16(regtemp | mode_val, priv->base + NPCM_PSPI_CTL1);
}

static void npcm_pspi_set_transfer_size(struct npcm_pspi *priv, int size)
{
	u16 regtemp;

	regtemp = ioread16(NPCM_PSPI_CTL1 + priv->base);

	switch (size) {
	case 8:
		regtemp &= ~NPCM_PSPI_CTL1_MOD;
		break;
	case 16:
		regtemp |= NPCM_PSPI_CTL1_MOD;
		break;
	}

	iowrite16(regtemp, NPCM_PSPI_CTL1 + priv->base);
}

static void npcm_pspi_set_baudrate(struct npcm_pspi *priv, unsigned int speed)
{
	u32 ckdiv;
	u16 regtemp;

	/* the supported rates are numbers from 4 to 256. */
	ckdiv = DIV_ROUND_CLOSEST(clk_get_rate(priv->clk), (2 * speed)) - 1;

	regtemp = ioread16(NPCM_PSPI_CTL1 + priv->base);
	regtemp &= ~NPCM_PSPI_CTL1_SCDV6_0;
	iowrite16(regtemp | (ckdiv << 9), NPCM_PSPI_CTL1 + priv->base);
}

static void npcm_pspi_setup_transfer(struct spi_device *spi,
				     struct spi_transfer *t)
{
	struct npcm_pspi *priv = spi_master_get_devdata(spi->master);

	priv->tx_buf = t->tx_buf;
	priv->rx_buf = t->rx_buf;
	priv->tx_bytes = t->len;
	priv->rx_bytes = t->len;

	if (!priv->is_save_param || priv->mode != spi->mode) {
		npcm_pspi_set_mode(spi);
		priv->mode = spi->mode;
	}

	/*
	 * If transfer is even length, and 8 bits per word transfer,
	 * then implement 16 bits-per-word transfer.
	 */
	if (priv->bits_per_word == 8 && !(t->len & 0x1))
		t->bits_per_word = 16;

	if (!priv->is_save_param || priv->bits_per_word != t->bits_per_word) {
		npcm_pspi_set_transfer_size(priv, t->bits_per_word);
		priv->bits_per_word = t->bits_per_word;
	}

	if (!priv->is_save_param || priv->speed_hz != t->speed_hz) {
		npcm_pspi_set_baudrate(priv, t->speed_hz);
		priv->speed_hz = t->speed_hz;
	}

	if (!priv->is_save_param)
		priv->is_save_param = true;
}

static void npcm_pspi_send(struct npcm_pspi *priv)
{
	int wsize;
	u16 val;

	wsize = min(bytes_per_word(priv->bits_per_word), priv->tx_bytes);
	priv->tx_bytes -= wsize;

	if (!priv->tx_buf)
		return;

	switch (wsize) {
	case 1:
		val = *priv->tx_buf++;
		iowrite8(val, NPCM_PSPI_DATA + priv->base);
		break;
	case 2:
		val = *priv->tx_buf++;
		val = *priv->tx_buf++ | (val << 8);
		iowrite16(val, NPCM_PSPI_DATA + priv->base);
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static void npcm_pspi_recv(struct npcm_pspi *priv)
{
	int rsize;
	u16 val;

	rsize = min(bytes_per_word(priv->bits_per_word), priv->rx_bytes);
	priv->rx_bytes -= rsize;

	if (!priv->rx_buf)
		return;

	switch (rsize) {
	case 1:
		*priv->rx_buf++ = ioread8(priv->base + NPCM_PSPI_DATA);
		break;
	case 2:
		val = ioread16(priv->base + NPCM_PSPI_DATA);
		*priv->rx_buf++ = (val >> 8);
		*priv->rx_buf++ = val & 0xff;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static int npcm_pspi_transfer_one(struct spi_master *master,
				  struct spi_device *spi,
				  struct spi_transfer *t)
{
	struct npcm_pspi *priv = spi_master_get_devdata(master);
	int status;

	npcm_pspi_setup_transfer(spi, t);
	reinit_completion(&priv->xfer_done);
	npcm_pspi_enable(priv);
	status = wait_for_completion_timeout(&priv->xfer_done,
					     msecs_to_jiffies
					     (NPCM_PSPI_TIMEOUT_MS));
	if (status == 0) {
		npcm_pspi_disable(priv);
		return -ETIMEDOUT;
	}

	return 0;
}

static int npcm_pspi_prepare_transfer_hardware(struct spi_master *master)
{
	struct npcm_pspi *priv = spi_master_get_devdata(master);

	npcm_pspi_irq_enable(priv, NPCM_PSPI_CTL1_EIR | NPCM_PSPI_CTL1_EIW);

	return 0;
}

static int npcm_pspi_unprepare_transfer_hardware(struct spi_master *master)
{
	struct npcm_pspi *priv = spi_master_get_devdata(master);

	npcm_pspi_irq_disable(priv, NPCM_PSPI_CTL1_EIR | NPCM_PSPI_CTL1_EIW);

	return 0;
}

static void npcm_pspi_reset_hw(struct npcm_pspi *priv)
{
	reset_control_assert(priv->reset);
	udelay(5);
	reset_control_deassert(priv->reset);
}

static irqreturn_t npcm_pspi_handler(int irq, void *dev_id)
{
	struct npcm_pspi *priv = dev_id;
	u8 stat;

	stat = ioread8(priv->base + NPCM_PSPI_STAT);

	if (!priv->tx_buf && !priv->rx_buf)
		return IRQ_NONE;

	if (priv->tx_buf) {
		if (stat & NPCM_PSPI_STAT_RBF) {
			ioread8(NPCM_PSPI_DATA + priv->base);
			if (priv->tx_bytes == 0) {
				npcm_pspi_disable(priv);
				complete(&priv->xfer_done);
				return IRQ_HANDLED;
			}
		}

		if ((stat & NPCM_PSPI_STAT_BSY) == 0)
			if (priv->tx_bytes)
				npcm_pspi_send(priv);
	}

	if (priv->rx_buf) {
		if (stat & NPCM_PSPI_STAT_RBF) {
			if (!priv->rx_bytes)
				return IRQ_NONE;

			npcm_pspi_recv(priv);

			if (!priv->rx_bytes) {
				npcm_pspi_disable(priv);
				complete(&priv->xfer_done);
				return IRQ_HANDLED;
			}
		}

		if (((stat & NPCM_PSPI_STAT_BSY) == 0) && !priv->tx_buf)
			iowrite8(0x0, NPCM_PSPI_DATA + priv->base);
	}

	return IRQ_HANDLED;
}

static int npcm_pspi_probe(struct platform_device *pdev)
{
	struct npcm_pspi *priv;
	struct spi_master *master;
	unsigned long clk_hz;
	int irq;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(*priv));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	priv = spi_master_get_devdata(master);
	priv->master = master;
	priv->is_save_param = false;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out_master_put;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(priv->clk);
		goto out_master_put;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto out_master_put;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out_disable_clk;
	}

	priv->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		goto out_disable_clk;
	}

	/* reset SPI-HW block */
	npcm_pspi_reset_hw(priv);

	ret = devm_request_irq(&pdev->dev, irq, npcm_pspi_handler, 0,
			       "npcm-pspi", priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto out_disable_clk;
	}

	init_completion(&priv->xfer_done);

	clk_hz = clk_get_rate(priv->clk);

	master->max_speed_hz = DIV_ROUND_UP(clk_hz, NPCM_PSPI_MIN_CLK_DIVIDER);
	master->min_speed_hz = DIV_ROUND_UP(clk_hz, NPCM_PSPI_MAX_CLK_DIVIDER);
	master->mode_bits = SPI_CPHA | SPI_CPOL;
	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = -1;
	master->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
	master->transfer_one = npcm_pspi_transfer_one;
	master->prepare_transfer_hardware =
		npcm_pspi_prepare_transfer_hardware;
	master->unprepare_transfer_hardware =
		npcm_pspi_unprepare_transfer_hardware;
	master->use_gpio_descriptors = true;

	/* set to default clock rate */
	npcm_pspi_set_baudrate(priv, NPCM_PSPI_DEFAULT_CLK);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret)
		goto out_disable_clk;

	pr_info("NPCM Peripheral SPI %d probed\n", master->bus_num);

	return 0;

out_disable_clk:
	clk_disable_unprepare(priv->clk);

out_master_put:
	spi_master_put(master);
	return ret;
}

static int npcm_pspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct npcm_pspi *priv = spi_master_get_devdata(master);

	npcm_pspi_reset_hw(priv);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id npcm_pspi_match[] = {
	{ .compatible = "nuvoton,npcm750-pspi", .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, npcm_pspi_match);

static struct platform_driver npcm_pspi_driver = {
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= npcm_pspi_match,
	},
	.probe		= npcm_pspi_probe,
	.remove		= npcm_pspi_remove,
};
module_platform_driver(npcm_pspi_driver);

MODULE_DESCRIPTION("NPCM peripheral SPI Controller driver");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL v2");

