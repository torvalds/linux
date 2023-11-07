// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 * Chin-Ting Kuo <chin-ting_kuo@aspeedtech.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/reset.h>

#include <linux/of_address.h>
#include <linux/of_platform.h>

#define SPI_CONFIG		0x00
#define SPI_CTRL		0x04
#define SPI_CE0_CTRL		0x10
#define SPI_DECODE_ADDR_REG	0x30

#define SPI_FULL_DUPLEX_RX_REG	0x1e4

#define SPI_LSB_FIRST_CTRL	BIT(5)
#define SPI_CE_INACTIVE		BIT(2)
#define SPI_CMD_USER_MODE	(0x3)

#define SPI_FULL_DUPLEX		0x00000001

struct aspeed_spi_host {
	phys_addr_t			 ahb_base_phy;
	size_t				 ahb_window_sz;
	void __iomem			*ctrl_reg;
	struct spi_controller		*ctrl;
	struct spi_device		*spi_dev;
	struct device			*dev;
	struct clk			*clk;
	u32				 ahb_clk;
	u32				 ctrl_val[5];
	void __iomem			*chip_ahb_base[5];
	/* lock: make sure only a user can access the controller once */
	spinlock_t			 lock;
	u8				 cs_change;
	const struct aspeed_spi_info	*info;
	u32				 flag;
};

struct aspeed_spi_info {
	u32	max_cs;
	size_t	min_window_sz;
	u32	hclk_mask;
	u32 (*get_clk_div)(struct aspeed_spi_host *host, u32 hz);
	void (*set_segment)(struct aspeed_spi_host *host);
};

static inline void spi_aspeed_dump_buf(const u8 *buf, u32 len)
{
	u32 i;

	if (len > 10) {
		for (i = 0; i < 10; i++)
			pr_info("%02x ", buf[i]);
	} else {
		for (i = 0; i < len; i++)
			pr_info("%02x ", buf[i]);
	}
	pr_info("\n");
}

#define G5_SEGMENT_ADDR_VALUE(start, end) \
	(((((start) >> 23) & 0xFF) << 16) | ((((end) >> 23) & 0xFF) << 24))

static void aspeed_spi_set_segment_addr_ast2500(struct aspeed_spi_host *host)
{
	const struct aspeed_spi_info *info = host->info;
	u32 cs;
	phys_addr_t start = host->ahb_base_phy;
	phys_addr_t end;
	u32 reg_val;

	for (cs = 0; cs < info->max_cs; cs++) {
		end = start + info->min_window_sz;
		if (cs == info->max_cs - 1)
			end = host->ahb_base_phy + host->ahb_window_sz;

		reg_val = G5_SEGMENT_ADDR_VALUE(start, end);
		writel(reg_val, host->ctrl_reg + SPI_CE0_CTRL + cs * 4);
		/* always mapping min_window_sz due to user mode is used */
		host->chip_ahb_base[cs] = devm_ioremap(host->dev,
						       start,
						       info->min_window_sz);
		start = end;
	}
}

#define G6_SEGMENT_ADDR_VALUE(start, end) \
	((((start) & 0x0ff00000) >> 16) | (((end) - 1) & 0x0ff00000))

static void aspeed_spi_set_segment_addr_ast2600(struct aspeed_spi_host *host)
{
	const struct aspeed_spi_info *info = host->info;
	u32 cs;
	phys_addr_t start = host->ahb_base_phy;
	phys_addr_t end;
	u32 reg_val;

	for (cs = 0; cs < info->max_cs; cs++) {
		end = start + info->min_window_sz;
		reg_val = G6_SEGMENT_ADDR_VALUE(start, end);
		writel(reg_val, host->ctrl_reg + SPI_DECODE_ADDR_REG + cs * 4);
		host->chip_ahb_base[cs] = devm_ioremap(host->dev,
						       start,
						       info->min_window_sz);
		start = end;
	}
}

#define G7_SEGMENT_ADDR_VALUE(start, end) \
	((((start) >> 16) & 0x7fff) | (((end) + 1) & 0x7fff0000))

static void aspeed_spi_set_segment_addr_ast2700(struct aspeed_spi_host *host)
{
	const struct aspeed_spi_info *info = host->info;
	u32 cs;
	phys_addr_t start = host->ahb_base_phy;
	phys_addr_t end;
	u32 reg_val;

	for (cs = 0; cs < info->max_cs; cs++) {
		end = start + info->min_window_sz;
		reg_val = G7_SEGMENT_ADDR_VALUE(start - host->ahb_base_phy,
						end - host->ahb_base_phy);
		writel(reg_val, host->ctrl_reg + SPI_DECODE_ADDR_REG + cs * 4);
		host->chip_ahb_base[cs] = devm_ioremap(host->dev,
						       start,
						       info->min_window_sz);
		start = end;
	}
}

static const u32 aspeed_spi_hclk_divs[] = {
	/* HCLK, HCLK/2, HCLK/3, HCLK/4, HCLK/5, ..., HCLK/16 */
	0xf, 0x7, 0xe, 0x6, 0xd,
	0x5, 0xc, 0x4, 0xb, 0x3,
	0xa, 0x2, 0x9, 0x1, 0x8,
	0x0
};

#define ASPEED_SPI_HCLK_DIV(i)	(aspeed_spi_hclk_divs[(i)] << 8)

static u32 apseed_get_clk_div_ast2500(struct aspeed_spi_host *host,
				      u32 max_hz)
{
	struct device *dev = host->dev;
	u32 hclk_clk = host->ahb_clk;
	u32 hclk_div = 0;
	u32 i;
	bool found = false;

	/* FMC/SPIR10[11:8] */
	for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		if (hclk_clk / (i + 1) <= max_hz) {
			found = true;
			break;
		}
	}

	if (found) {
		hclk_div = ASPEED_SPI_HCLK_DIV(i);
		goto end;
	}

	for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		if (hclk_clk / ((i + 1) * 4) <= max_hz) {
			found = true;
			break;
		}
	}

	if (found)
		hclk_div = BIT(13) | ASPEED_SPI_HCLK_DIV(i);

end:
	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n",
		found ? "yes" : "no", hclk_clk, max_hz);

	if (found) {
		dev_dbg(dev, "h_div: %d (mask %x)\n",
			i + 1, hclk_div);
	}

	return hclk_div;
}

static u32 apseed_get_clk_div_ast2600(struct aspeed_spi_host *host,
				      u32 max_hz)
{
	struct device *dev = host->dev;
	u32 hclk_clk = host->ahb_clk;
	u32 hclk_div = 0;
	u32 i, j;
	bool found = false;

	/* FMC/SPIR10[27:24] */
	for (j = 0; j < 16; j++) {
		/* FMC/SPIR10[11:8] */
		for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
			if (i == 0 && j == 0)
				continue;

			if (hclk_clk / (i + 1 + (j * 16)) <= max_hz) {
				found = true;
				break;
			}
		}

		if (found) {
			hclk_div = ((j << 24) | ASPEED_SPI_HCLK_DIV(i));
			break;
		}
	}

	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n",
		found ? "yes" : "no", hclk_clk, max_hz);

	if (found) {
		dev_dbg(dev, "base_clk: %d, h_div: %d (mask %x)\n",
			j, i + 1, hclk_div);
	}

	return hclk_div;
}

static void aspeed_spi_chip_set_type(struct aspeed_spi_host *host)
{
	u32 reg;
	u32 cs;

	for (cs = 0; cs < host->info->max_cs; cs++) {
		reg = readl(host->ctrl_reg + SPI_CONFIG);
		reg &= ~(0x3 << (cs * 2));
		reg |= 0x2 << (cs * 2);
		writel(reg, host->ctrl_reg + SPI_CONFIG);
	}
}

static void aspeed_spi_enable(struct aspeed_spi_host *host, bool enable)
{
	u32 cs;
	u32 reg;
	u32 we_bit;

	for (cs = 0; cs < host->info->max_cs; cs++) {
		reg = readl(host->ctrl_reg + SPI_CONFIG);
		we_bit = (0x1 << cs) << 16;

		if (enable)
			reg |= we_bit;
		else
			reg &= ~we_bit;

		writel(reg, host->ctrl_reg + SPI_CONFIG);
	}
}

static int aspeed_spi_setup(struct spi_device *spi)
{
	struct aspeed_spi_host *host =
		(struct aspeed_spi_host *)spi_controller_get_devdata(spi->controller);
	struct device *dev = host->dev;
	u32 clk_div;

	dev_dbg(dev, "cs: %d, mode: %d, max_speed: %d, bits_per_word: %d\n",
		spi->chip_select, spi->mode,
		spi->max_speed_hz, spi->bits_per_word);

	if (spi->mode & SPI_MODE_X_MASK) {
		dev_dbg(dev, "unsupported mode bits: %x\n",
			(u32)(spi->mode & SPI_MODE_X_MASK));
		return -EINVAL;
	}

	if (spi->mode & SPI_CS_HIGH) {
		dev_dbg(dev, "can't be active-high\n");
		return -EINVAL;
	}

	host->ctrl_val[spi->chip_select] = SPI_CE_INACTIVE | SPI_CMD_USER_MODE;

	if (spi->max_speed_hz) {
		clk_div = host->info->get_clk_div(host, spi->max_speed_hz);
	} else {
		/* speed zero means "as slow as possible" */
		clk_div = ~(host->info->hclk_mask);
	}

	host->ctrl_val[spi->chip_select] |= clk_div;

	if (spi->mode & SPI_LSB_FIRST)
		host->ctrl_val[spi->chip_select] |= SPI_LSB_FIRST_CTRL;

	dev_info(dev, "cs: %d, ctrl_val: 0x%08x\n",
		 spi->chip_select, host->ctrl_val[spi->chip_select]);

	return 0;
}

static void aspeed_spi_start_user(struct spi_device *spi)
{
	struct aspeed_spi_host *host =
		(struct aspeed_spi_host *)spi_controller_get_devdata(spi->controller);
	u32 ctrl_val = host->ctrl_val[spi->chip_select];
	void __iomem *ctrl_reg = host->ctrl_reg + SPI_CE0_CTRL +
				 spi->chip_select * 4;

	ctrl_val |= SPI_CE_INACTIVE;
	writel(ctrl_val, ctrl_reg);

	ctrl_val &= ~SPI_CE_INACTIVE;
	writel(ctrl_val, ctrl_reg);
}

static void aspeed_spi_stop_user(struct spi_device *spi)
{
	struct aspeed_spi_host *host =
		(struct aspeed_spi_host *)spi_controller_get_devdata(spi->controller);
	u32 ctrl_val = host->ctrl_val[spi->chip_select] | SPI_CE_INACTIVE;
	void __iomem *ctrl_reg = host->ctrl_reg + SPI_CE0_CTRL +
				 spi->chip_select * 4;

	writel(ctrl_val, ctrl_reg);
}

static void aspeed_spi_transfer_tx(struct aspeed_spi_host *host, const u8 *tx_buf,
				   u8 *rx_buf, void *dst, u32 len,
				   bool *full_duplex_rx)
{
	u32 i;

	for (i = 0; i < len; i++) {
		writeb(tx_buf[i], dst);

		if (rx_buf && (host->flag & SPI_FULL_DUPLEX)) {
			rx_buf[i] = readb(host->ctrl_reg + SPI_FULL_DUPLEX_RX_REG);
			*full_duplex_rx = true;
		}
	}
}

static int aspeed_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct aspeed_spi_host *host =
		(struct aspeed_spi_host *)spi_master_get_devdata(spi->controller);
	struct device *dev = host->dev;
	struct spi_transfer *xfer;
	const u8 *tx_buf;
	bool full_duplex_rx;
	u8 *rx_buf;
	u32 cs;
	unsigned long flags;
	u32 j = 0;

	if (host->cs_change == 0) {
		spin_lock_irqsave(&host->lock, flags);
		aspeed_spi_start_user(spi);
	}

	cs = spi->chip_select;

	dev_dbg(dev, "cs: %d\n", cs);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		dev_dbg(dev,
			"xfer[%d]: width %d, len %u, tx %p, rx %p\n",
			j,
			xfer->bits_per_word, xfer->len,
			xfer->tx_buf, xfer->rx_buf);

		tx_buf = xfer->tx_buf;
		rx_buf = xfer->rx_buf;

		full_duplex_rx = false;

		if (tx_buf) {
#if defined(SPI_ASPEED_TXRX_DBG)
			pr_info("tx : ");
			spi_aspeed_dump_buf(tx_buf, xfer->len);
#endif

			aspeed_spi_transfer_tx(host, tx_buf, rx_buf,
					       (void *)host->chip_ahb_base[cs],
					       xfer->len, &full_duplex_rx);
		}

		if (rx_buf && !full_duplex_rx) {
			ioread8_rep(host->chip_ahb_base[cs], rx_buf, xfer->len);

#if defined(SPI_ASPEED_TXRX_DBG)
			pr_info("rx : ");
			spi_aspeed_dump_buf(rx_buf, xfer->len);
#endif
		}

		msg->actual_length += xfer->len;
		host->cs_change = xfer->cs_change;
		j++;
	}

	if (host->cs_change == 0)
		aspeed_spi_stop_user(spi);

	msg->status = 0;
	msg->complete(msg->context);

	if (host->cs_change == 0)
		spin_unlock_irqrestore(&host->lock, flags);

	return 0;
}

static int aspeed_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource	*res;
	struct aspeed_spi_host *host;
	struct spi_controller *ctrl;
	struct reset_control *rst;
	int err = 0;

	ctrl = devm_spi_alloc_master(dev, sizeof(struct aspeed_spi_host));
	if (!ctrl) {
		dev_err(dev, "No memory for spi controller\n");
		return -ENOMEM;
	}

	ctrl->mode_bits = 0;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->dev.of_node = pdev->dev.of_node;
	ctrl->bus_num = pdev->id;

	host = spi_controller_get_devdata(ctrl);
	platform_set_drvdata(pdev, host);

	memset(host, 0, sizeof(struct aspeed_spi_host));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot get IORESOURCE_MEM 0\n");
		return -ENXIO;
	}

	host->ctrl_reg = devm_ioremap_resource(dev, res);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "cannot get IORESOURCE_MEM 1\n");
		return -ENXIO;
	}

	host->ahb_base_phy = res->start;
	host->ahb_window_sz = resource_size(res);

	host->ctrl = spi_controller_get(ctrl);
	host->dev = &pdev->dev;
	host->info = of_device_get_match_data(&pdev->dev);
	if (!host->info)
		return -ENODEV;

	rst = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (rst) {
		err = reset_control_deassert(rst);
		if (err) {
			dev_err(dev, "fail to deassert reset control\n");
			return -EBUSY;
		}
	}

	host->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(dev, "missing clock\n");
		return PTR_ERR(host->clk);
	}

	host->ahb_clk = clk_get_rate(host->clk);
	if (!host->ahb_clk) {
		dev_err(dev, "invalid clock\n");
		return -EINVAL;
	}

	err = clk_prepare_enable(host->clk);
	if (err) {
		dev_err(dev, "can not enable the clock\n");
		return err;
	}

	host->flag = 0;
	if (of_property_read_bool(dev->of_node, "spi-aspeed-full-duplex"))
		host->flag |= SPI_FULL_DUPLEX;

	host->ctrl->setup = aspeed_spi_setup;
	host->ctrl->transfer = aspeed_spi_transfer;
	host->ctrl->num_chipselect = host->info->max_cs;

	/* configure minimum segment window */
	host->info->set_segment(host);
	aspeed_spi_enable(host, true);
	aspeed_spi_chip_set_type(host);

	err = devm_spi_register_controller(dev, host->ctrl);
	if (err) {
		dev_err(dev, "failed to register SPI controller\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	clk_disable_unprepare(host->clk);
	return err;
}

static int aspeed_spi_remove(struct platform_device *pdev)
{
	struct aspeed_spi_host *host = platform_get_drvdata(pdev);

	aspeed_spi_enable(host, false);
	clk_disable_unprepare(host->clk);

	return 0;
}

struct aspeed_spi_info ast2500_fmc_info = {
	.max_cs        = 3,
	.min_window_sz = 0x800000,
	.hclk_mask     = 0xffffd0ff,
	.get_clk_div   = apseed_get_clk_div_ast2500,
	.set_segment   = aspeed_spi_set_segment_addr_ast2500,
};

struct aspeed_spi_info ast2500_spi_info = {
	.max_cs        = 2,
	.min_window_sz = 0x800000,
	.hclk_mask     = 0xffffd0ff,
	.get_clk_div   = apseed_get_clk_div_ast2500,
	.set_segment   = aspeed_spi_set_segment_addr_ast2500,
};

struct aspeed_spi_info ast2600_fmc_info = {
	.max_cs        = 3,
	.min_window_sz = 0x200000,
	.hclk_mask     = 0xf0fff0ff,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.set_segment   = aspeed_spi_set_segment_addr_ast2600,
};

struct aspeed_spi_info ast2600_spi_info = {
	.max_cs        = 2,
	.min_window_sz = 0x200000,
	.hclk_mask     = 0xf0fff0ff,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.set_segment   = aspeed_spi_set_segment_addr_ast2600,
};

struct aspeed_spi_info ast2700_fmc_info = {
	.max_cs        = 3,
	.min_window_sz = 0x10000,
	.hclk_mask     = 0xf0fff0ff,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.set_segment   = aspeed_spi_set_segment_addr_ast2700,
};

struct aspeed_spi_info ast2700_spi_info = {
	.max_cs        = 2,
	.min_window_sz = 0x10000,
	.hclk_mask     = 0xf0fff0ff,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.set_segment   = aspeed_spi_set_segment_addr_ast2700,
};

static const struct of_device_id aspeed_spi_of_match[] = {
	{ .compatible = "aspeed,ast2500-fmc-txrx", .data = &ast2500_fmc_info},
	{ .compatible = "aspeed,ast2500-spi-txrx", .data = &ast2500_spi_info},
	{ .compatible = "aspeed,ast2600-fmc-txrx", .data = &ast2600_fmc_info},
	{ .compatible = "aspeed,ast2600-spi-txrx", .data = &ast2600_spi_info},
	{ .compatible = "aspeed,ast2700-fmc-txrx", .data = &ast2700_fmc_info},
	{ .compatible = "aspeed,ast2700-spi-txrx", .data = &ast2700_spi_info},
	{ },
};

static struct platform_driver aspeed_spi_driver = {
	.probe = aspeed_spi_probe,
	.remove = aspeed_spi_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table = aspeed_spi_of_match,
	},
};

module_platform_driver(aspeed_spi_driver);

MODULE_DESCRIPTION("ASPEED Pure SPI Driver");
MODULE_AUTHOR("Ryan Chen");
MODULE_AUTHOR("Chin-Ting Kuo");
MODULE_LICENSE("GPL");

