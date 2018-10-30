/*
 * SPI-Engine SPI controller driver
 * Copyright 2015 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#define SPI_ENGINE_VERSION_MAJOR(x)	((x >> 16) & 0xff)
#define SPI_ENGINE_VERSION_MINOR(x)	((x >> 8) & 0xff)
#define SPI_ENGINE_VERSION_PATCH(x)	(x & 0xff)

#define SPI_ENGINE_REG_VERSION			0x00

#define SPI_ENGINE_REG_RESET			0x40

#define SPI_ENGINE_REG_INT_ENABLE		0x80
#define SPI_ENGINE_REG_INT_PENDING		0x84
#define SPI_ENGINE_REG_INT_SOURCE		0x88

#define SPI_ENGINE_REG_SYNC_ID			0xc0

#define SPI_ENGINE_REG_CMD_FIFO_ROOM		0xd0
#define SPI_ENGINE_REG_SDO_FIFO_ROOM		0xd4
#define SPI_ENGINE_REG_SDI_FIFO_LEVEL		0xd8

#define SPI_ENGINE_REG_CMD_FIFO			0xe0
#define SPI_ENGINE_REG_SDO_DATA_FIFO		0xe4
#define SPI_ENGINE_REG_SDI_DATA_FIFO		0xe8
#define SPI_ENGINE_REG_SDI_DATA_FIFO_PEEK	0xec

#define SPI_ENGINE_INT_CMD_ALMOST_EMPTY		BIT(0)
#define SPI_ENGINE_INT_SDO_ALMOST_EMPTY		BIT(1)
#define SPI_ENGINE_INT_SDI_ALMOST_FULL		BIT(2)
#define SPI_ENGINE_INT_SYNC			BIT(3)

#define SPI_ENGINE_CONFIG_CPHA			BIT(0)
#define SPI_ENGINE_CONFIG_CPOL			BIT(1)
#define SPI_ENGINE_CONFIG_3WIRE			BIT(2)

#define SPI_ENGINE_INST_TRANSFER		0x0
#define SPI_ENGINE_INST_ASSERT			0x1
#define SPI_ENGINE_INST_WRITE			0x2
#define SPI_ENGINE_INST_MISC			0x3

#define SPI_ENGINE_CMD_REG_CLK_DIV		0x0
#define SPI_ENGINE_CMD_REG_CONFIG		0x1

#define SPI_ENGINE_MISC_SYNC			0x0
#define SPI_ENGINE_MISC_SLEEP			0x1

#define SPI_ENGINE_TRANSFER_WRITE		0x1
#define SPI_ENGINE_TRANSFER_READ		0x2

#define SPI_ENGINE_CMD(inst, arg1, arg2) \
	(((inst) << 12) | ((arg1) << 8) | (arg2))

#define SPI_ENGINE_CMD_TRANSFER(flags, n) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_TRANSFER, (flags), (n))
#define SPI_ENGINE_CMD_ASSERT(delay, cs) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_ASSERT, (delay), (cs))
#define SPI_ENGINE_CMD_WRITE(reg, val) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_WRITE, (reg), (val))
#define SPI_ENGINE_CMD_SLEEP(delay) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_MISC, SPI_ENGINE_MISC_SLEEP, (delay))
#define SPI_ENGINE_CMD_SYNC(id) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_MISC, SPI_ENGINE_MISC_SYNC, (id))

struct spi_engine_program {
	unsigned int length;
	uint16_t instructions[];
};

struct spi_engine {
	struct clk *clk;
	struct clk *ref_clk;

	spinlock_t lock;

	void __iomem *base;

	struct spi_message *msg;
	struct spi_engine_program *p;
	unsigned cmd_length;
	const uint16_t *cmd_buf;

	struct spi_transfer *tx_xfer;
	unsigned int tx_length;
	const uint8_t *tx_buf;

	struct spi_transfer *rx_xfer;
	unsigned int rx_length;
	uint8_t *rx_buf;

	unsigned int sync_id;
	unsigned int completed_id;

	unsigned int int_enable;
};

static void spi_engine_program_add_cmd(struct spi_engine_program *p,
	bool dry, uint16_t cmd)
{
	if (!dry)
		p->instructions[p->length] = cmd;
	p->length++;
}

static unsigned int spi_engine_get_config(struct spi_device *spi)
{
	unsigned int config = 0;

	if (spi->mode & SPI_CPOL)
		config |= SPI_ENGINE_CONFIG_CPOL;
	if (spi->mode & SPI_CPHA)
		config |= SPI_ENGINE_CONFIG_CPHA;
	if (spi->mode & SPI_3WIRE)
		config |= SPI_ENGINE_CONFIG_3WIRE;

	return config;
}

static unsigned int spi_engine_get_clk_div(struct spi_engine *spi_engine,
	struct spi_device *spi, struct spi_transfer *xfer)
{
	unsigned int clk_div;

	clk_div = DIV_ROUND_UP(clk_get_rate(spi_engine->ref_clk),
		xfer->speed_hz * 2);
	if (clk_div > 255)
		clk_div = 255;
	else if (clk_div > 0)
		clk_div -= 1;

	return clk_div;
}

static void spi_engine_gen_xfer(struct spi_engine_program *p, bool dry,
	struct spi_transfer *xfer)
{
	unsigned int len = xfer->len;

	while (len) {
		unsigned int n = min(len, 256U);
		unsigned int flags = 0;

		if (xfer->tx_buf)
			flags |= SPI_ENGINE_TRANSFER_WRITE;
		if (xfer->rx_buf)
			flags |= SPI_ENGINE_TRANSFER_READ;

		spi_engine_program_add_cmd(p, dry,
			SPI_ENGINE_CMD_TRANSFER(flags, n - 1));
		len -= n;
	}
}

static void spi_engine_gen_sleep(struct spi_engine_program *p, bool dry,
	struct spi_engine *spi_engine, unsigned int clk_div, unsigned int delay)
{
	unsigned int spi_clk = clk_get_rate(spi_engine->ref_clk);
	unsigned int t;

	if (delay == 0)
		return;

	t = DIV_ROUND_UP(delay * spi_clk, (clk_div + 1) * 2);
	while (t) {
		unsigned int n = min(t, 256U);

		spi_engine_program_add_cmd(p, dry, SPI_ENGINE_CMD_SLEEP(n - 1));
		t -= n;
	}
}

static void spi_engine_gen_cs(struct spi_engine_program *p, bool dry,
		struct spi_device *spi, bool assert)
{
	unsigned int mask = 0xff;

	if (assert)
		mask ^= BIT(spi->chip_select);

	spi_engine_program_add_cmd(p, dry, SPI_ENGINE_CMD_ASSERT(1, mask));
}

static int spi_engine_compile_message(struct spi_engine *spi_engine,
	struct spi_message *msg, bool dry, struct spi_engine_program *p)
{
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer;
	int clk_div, new_clk_div;
	bool cs_change = true;

	clk_div = -1;

	spi_engine_program_add_cmd(p, dry,
		SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_CONFIG,
			spi_engine_get_config(spi)));

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		new_clk_div = spi_engine_get_clk_div(spi_engine, spi, xfer);
		if (new_clk_div != clk_div) {
			clk_div = new_clk_div;
			spi_engine_program_add_cmd(p, dry,
				SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_CLK_DIV,
					clk_div));
		}

		if (cs_change)
			spi_engine_gen_cs(p, dry, spi, true);

		spi_engine_gen_xfer(p, dry, xfer);
		spi_engine_gen_sleep(p, dry, spi_engine, clk_div,
			xfer->delay_usecs);

		cs_change = xfer->cs_change;
		if (list_is_last(&xfer->transfer_list, &msg->transfers))
			cs_change = !cs_change;

		if (cs_change)
			spi_engine_gen_cs(p, dry, spi, false);
	}

	return 0;
}

static void spi_engine_xfer_next(struct spi_engine *spi_engine,
	struct spi_transfer **_xfer)
{
	struct spi_message *msg = spi_engine->msg;
	struct spi_transfer *xfer = *_xfer;

	if (!xfer) {
		xfer = list_first_entry(&msg->transfers,
			struct spi_transfer, transfer_list);
	} else if (list_is_last(&xfer->transfer_list, &msg->transfers)) {
		xfer = NULL;
	} else {
		xfer = list_next_entry(xfer, transfer_list);
	}

	*_xfer = xfer;
}

static void spi_engine_tx_next(struct spi_engine *spi_engine)
{
	struct spi_transfer *xfer = spi_engine->tx_xfer;

	do {
		spi_engine_xfer_next(spi_engine, &xfer);
	} while (xfer && !xfer->tx_buf);

	spi_engine->tx_xfer = xfer;
	if (xfer) {
		spi_engine->tx_length = xfer->len;
		spi_engine->tx_buf = xfer->tx_buf;
	} else {
		spi_engine->tx_buf = NULL;
	}
}

static void spi_engine_rx_next(struct spi_engine *spi_engine)
{
	struct spi_transfer *xfer = spi_engine->rx_xfer;

	do {
		spi_engine_xfer_next(spi_engine, &xfer);
	} while (xfer && !xfer->rx_buf);

	spi_engine->rx_xfer = xfer;
	if (xfer) {
		spi_engine->rx_length = xfer->len;
		spi_engine->rx_buf = xfer->rx_buf;
	} else {
		spi_engine->rx_buf = NULL;
	}
}

static bool spi_engine_write_cmd_fifo(struct spi_engine *spi_engine)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_CMD_FIFO;
	unsigned int n, m, i;
	const uint16_t *buf;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_CMD_FIFO_ROOM);
	while (n && spi_engine->cmd_length) {
		m = min(n, spi_engine->cmd_length);
		buf = spi_engine->cmd_buf;
		for (i = 0; i < m; i++)
			writel_relaxed(buf[i], addr);
		spi_engine->cmd_buf += m;
		spi_engine->cmd_length -= m;
		n -= m;
	}

	return spi_engine->cmd_length != 0;
}

static bool spi_engine_write_tx_fifo(struct spi_engine *spi_engine)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_SDO_DATA_FIFO;
	unsigned int n, m, i;
	const uint8_t *buf;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_SDO_FIFO_ROOM);
	while (n && spi_engine->tx_length) {
		m = min(n, spi_engine->tx_length);
		buf = spi_engine->tx_buf;
		for (i = 0; i < m; i++)
			writel_relaxed(buf[i], addr);
		spi_engine->tx_buf += m;
		spi_engine->tx_length -= m;
		n -= m;
		if (spi_engine->tx_length == 0)
			spi_engine_tx_next(spi_engine);
	}

	return spi_engine->tx_length != 0;
}

static bool spi_engine_read_rx_fifo(struct spi_engine *spi_engine)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_SDI_DATA_FIFO;
	unsigned int n, m, i;
	uint8_t *buf;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_SDI_FIFO_LEVEL);
	while (n && spi_engine->rx_length) {
		m = min(n, spi_engine->rx_length);
		buf = spi_engine->rx_buf;
		for (i = 0; i < m; i++)
			buf[i] = readl_relaxed(addr);
		spi_engine->rx_buf += m;
		spi_engine->rx_length -= m;
		n -= m;
		if (spi_engine->rx_length == 0)
			spi_engine_rx_next(spi_engine);
	}

	return spi_engine->rx_length != 0;
}

static irqreturn_t spi_engine_irq(int irq, void *devid)
{
	struct spi_master *master = devid;
	struct spi_engine *spi_engine = spi_master_get_devdata(master);
	unsigned int disable_int = 0;
	unsigned int pending;

	pending = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_INT_PENDING);

	if (pending & SPI_ENGINE_INT_SYNC) {
		writel_relaxed(SPI_ENGINE_INT_SYNC,
			spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
		spi_engine->completed_id = readl_relaxed(
			spi_engine->base + SPI_ENGINE_REG_SYNC_ID);
	}

	spin_lock(&spi_engine->lock);

	if (pending & SPI_ENGINE_INT_CMD_ALMOST_EMPTY) {
		if (!spi_engine_write_cmd_fifo(spi_engine))
			disable_int |= SPI_ENGINE_INT_CMD_ALMOST_EMPTY;
	}

	if (pending & SPI_ENGINE_INT_SDO_ALMOST_EMPTY) {
		if (!spi_engine_write_tx_fifo(spi_engine))
			disable_int |= SPI_ENGINE_INT_SDO_ALMOST_EMPTY;
	}

	if (pending & (SPI_ENGINE_INT_SDI_ALMOST_FULL | SPI_ENGINE_INT_SYNC)) {
		if (!spi_engine_read_rx_fifo(spi_engine))
			disable_int |= SPI_ENGINE_INT_SDI_ALMOST_FULL;
	}

	if (pending & SPI_ENGINE_INT_SYNC) {
		if (spi_engine->msg &&
		    spi_engine->completed_id == spi_engine->sync_id) {
			struct spi_message *msg = spi_engine->msg;

			kfree(spi_engine->p);
			msg->status = 0;
			msg->actual_length = msg->frame_length;
			spi_engine->msg = NULL;
			spi_finalize_current_message(master);
			disable_int |= SPI_ENGINE_INT_SYNC;
		}
	}

	if (disable_int) {
		spi_engine->int_enable &= ~disable_int;
		writel_relaxed(spi_engine->int_enable,
			spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);
	}

	spin_unlock(&spi_engine->lock);

	return IRQ_HANDLED;
}

static int spi_engine_transfer_one_message(struct spi_master *master,
	struct spi_message *msg)
{
	struct spi_engine_program p_dry, *p;
	struct spi_engine *spi_engine = spi_master_get_devdata(master);
	unsigned int int_enable = 0;
	unsigned long flags;
	size_t size;

	p_dry.length = 0;
	spi_engine_compile_message(spi_engine, msg, true, &p_dry);

	size = sizeof(*p->instructions) * (p_dry.length + 1);
	p = kzalloc(sizeof(*p) + size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	spi_engine_compile_message(spi_engine, msg, false, p);

	spin_lock_irqsave(&spi_engine->lock, flags);
	spi_engine->sync_id = (spi_engine->sync_id + 1) & 0xff;
	spi_engine_program_add_cmd(p, false,
		SPI_ENGINE_CMD_SYNC(spi_engine->sync_id));

	spi_engine->msg = msg;
	spi_engine->p = p;

	spi_engine->cmd_buf = p->instructions;
	spi_engine->cmd_length = p->length;
	if (spi_engine_write_cmd_fifo(spi_engine))
		int_enable |= SPI_ENGINE_INT_CMD_ALMOST_EMPTY;

	spi_engine_tx_next(spi_engine);
	if (spi_engine_write_tx_fifo(spi_engine))
		int_enable |= SPI_ENGINE_INT_SDO_ALMOST_EMPTY;

	spi_engine_rx_next(spi_engine);
	if (spi_engine->rx_length != 0)
		int_enable |= SPI_ENGINE_INT_SDI_ALMOST_FULL;

	int_enable |= SPI_ENGINE_INT_SYNC;

	writel_relaxed(int_enable,
		spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);
	spi_engine->int_enable = int_enable;
	spin_unlock_irqrestore(&spi_engine->lock, flags);

	return 0;
}

static int spi_engine_probe(struct platform_device *pdev)
{
	struct spi_engine *spi_engine;
	struct spi_master *master;
	unsigned int version;
	struct resource *res;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENXIO;

	spi_engine = devm_kzalloc(&pdev->dev, sizeof(*spi_engine), GFP_KERNEL);
	if (!spi_engine)
		return -ENOMEM;

	master = spi_alloc_master(&pdev->dev, 0);
	if (!master)
		return -ENOMEM;

	spi_master_set_devdata(master, spi_engine);

	spin_lock_init(&spi_engine->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spi_engine->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spi_engine->base)) {
		ret = PTR_ERR(spi_engine->base);
		goto err_put_master;
	}

	version = readl(spi_engine->base + SPI_ENGINE_REG_VERSION);
	if (SPI_ENGINE_VERSION_MAJOR(version) != 1) {
		dev_err(&pdev->dev, "Unsupported peripheral version %u.%u.%c\n",
			SPI_ENGINE_VERSION_MAJOR(version),
			SPI_ENGINE_VERSION_MINOR(version),
			SPI_ENGINE_VERSION_PATCH(version));
		ret = -ENODEV;
		goto err_put_master;
	}

	spi_engine->clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(spi_engine->clk)) {
		ret = PTR_ERR(spi_engine->clk);
		goto err_put_master;
	}

	spi_engine->ref_clk = devm_clk_get(&pdev->dev, "spi_clk");
	if (IS_ERR(spi_engine->ref_clk)) {
		ret = PTR_ERR(spi_engine->ref_clk);
		goto err_put_master;
	}

	ret = clk_prepare_enable(spi_engine->clk);
	if (ret)
		goto err_put_master;

	ret = clk_prepare_enable(spi_engine->ref_clk);
	if (ret)
		goto err_clk_disable;

	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_RESET);
	writel_relaxed(0xff, spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);

	ret = request_irq(irq, spi_engine_irq, 0, pdev->name, master);
	if (ret)
		goto err_ref_clk_disable;

	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_3WIRE;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->max_speed_hz = clk_get_rate(spi_engine->ref_clk) / 2;
	master->transfer_one_message = spi_engine_transfer_one_message;
	master->num_chipselect = 8;

	ret = spi_register_master(master);
	if (ret)
		goto err_free_irq;

	platform_set_drvdata(pdev, master);

	return 0;
err_free_irq:
	free_irq(irq, master);
err_ref_clk_disable:
	clk_disable_unprepare(spi_engine->ref_clk);
err_clk_disable:
	clk_disable_unprepare(spi_engine->clk);
err_put_master:
	spi_master_put(master);
	return ret;
}

static int spi_engine_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct spi_engine *spi_engine = spi_master_get_devdata(master);
	int irq = platform_get_irq(pdev, 0);

	spi_unregister_master(master);

	free_irq(irq, master);

	spi_master_put(master);

	writel_relaxed(0xff, spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);
	writel_relaxed(0x01, spi_engine->base + SPI_ENGINE_REG_RESET);

	clk_disable_unprepare(spi_engine->ref_clk);
	clk_disable_unprepare(spi_engine->clk);

	return 0;
}

static const struct of_device_id spi_engine_match_table[] = {
	{ .compatible = "adi,axi-spi-engine-1.00.a" },
	{ },
};
MODULE_DEVICE_TABLE(of, spi_engine_match_table);

static struct platform_driver spi_engine_driver = {
	.probe = spi_engine_probe,
	.remove = spi_engine_remove,
	.driver = {
		.name = "spi-engine",
		.of_match_table = spi_engine_match_table,
	},
};
module_platform_driver(spi_engine_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices SPI engine peripheral driver");
MODULE_LICENSE("GPL");
