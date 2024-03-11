// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI-Engine SPI controller driver
 * Copyright 2015 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/clk.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/timer.h>

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
#define SPI_ENGINE_CMD_REG_XFER_BITS		0x2

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

/**
 * struct spi_engine_message_state - SPI engine per-message state
 */
struct spi_engine_message_state {
	/** @p: Instructions for executing this message. */
	struct spi_engine_program *p;
	/** @cmd_length: Number of elements in cmd_buf array. */
	unsigned cmd_length;
	/** @cmd_buf: Array of commands not yet written to CMD FIFO. */
	const uint16_t *cmd_buf;
	/** @tx_xfer: Next xfer with tx_buf not yet fully written to TX FIFO. */
	struct spi_transfer *tx_xfer;
	/** @tx_length: Size of tx_buf in bytes. */
	unsigned int tx_length;
	/** @tx_buf: Bytes not yet written to TX FIFO. */
	const uint8_t *tx_buf;
	/** @rx_xfer: Next xfer with rx_buf not yet fully written to RX FIFO. */
	struct spi_transfer *rx_xfer;
	/** @rx_length: Size of tx_buf in bytes. */
	unsigned int rx_length;
	/** @rx_buf: Bytes not yet written to the RX FIFO. */
	uint8_t *rx_buf;
	/** @sync_id: ID to correlate SYNC interrupts with this message. */
	u8 sync_id;
};

struct spi_engine {
	struct clk *clk;
	struct clk *ref_clk;

	spinlock_t lock;

	void __iomem *base;
	struct ida sync_ida;
	struct timer_list watchdog_timer;
	struct spi_controller *controller;

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

static void spi_engine_gen_xfer(struct spi_engine_program *p, bool dry,
	struct spi_transfer *xfer)
{
	unsigned int len;

	if (xfer->bits_per_word <= 8)
		len = xfer->len;
	else if (xfer->bits_per_word <= 16)
		len = xfer->len / 2;
	else
		len = xfer->len / 4;

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
				 int delay_ns, u32 sclk_hz)
{
	unsigned int t;

	/* negative delay indicates error, e.g. from spi_delay_to_ns() */
	if (delay_ns <= 0)
		return;

	/* rounding down since executing the instruction adds a couple of ticks delay */
	t = DIV_ROUND_DOWN_ULL((u64)delay_ns * sclk_hz, NSEC_PER_SEC);
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
		mask ^= BIT(spi_get_chipselect(spi, 0));

	spi_engine_program_add_cmd(p, dry, SPI_ENGINE_CMD_ASSERT(0, mask));
}

/*
 * Performs precompile steps on the message.
 *
 * The SPI core does most of the message/transfer validation and filling in
 * fields for us via __spi_validate(). This fixes up anything remaining not
 * done there.
 *
 * NB: This is separate from spi_engine_compile_message() because the latter
 * is called twice and would otherwise result in double-evaluation.
 */
static void spi_engine_precompile_message(struct spi_message *msg)
{
	unsigned int clk_div, max_hz = msg->spi->controller->max_speed_hz;
	struct spi_transfer *xfer;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		clk_div = DIV_ROUND_UP(max_hz, xfer->speed_hz);
		xfer->effective_speed_hz = max_hz / min(clk_div, 256U);
	}
}

static void spi_engine_compile_message(struct spi_message *msg, bool dry,
				       struct spi_engine_program *p)
{
	struct spi_device *spi = msg->spi;
	struct spi_controller *host = spi->controller;
	struct spi_transfer *xfer;
	int clk_div, new_clk_div;
	bool keep_cs = false;
	u8 bits_per_word = 0;

	clk_div = 1;

	spi_engine_program_add_cmd(p, dry,
		SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_CONFIG,
			spi_engine_get_config(spi)));

	xfer = list_first_entry(&msg->transfers, struct spi_transfer, transfer_list);
	spi_engine_gen_cs(p, dry, spi, !xfer->cs_off);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		new_clk_div = host->max_speed_hz / xfer->effective_speed_hz;
		if (new_clk_div != clk_div) {
			clk_div = new_clk_div;
			/* actual divider used is register value + 1 */
			spi_engine_program_add_cmd(p, dry,
				SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_CLK_DIV,
					clk_div - 1));
		}

		if (bits_per_word != xfer->bits_per_word) {
			bits_per_word = xfer->bits_per_word;
			spi_engine_program_add_cmd(p, dry,
				SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_XFER_BITS,
					bits_per_word));
		}

		spi_engine_gen_xfer(p, dry, xfer);
		spi_engine_gen_sleep(p, dry, spi_delay_to_ns(&xfer->delay, xfer),
				     xfer->effective_speed_hz);

		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list, &msg->transfers)) {
				keep_cs = true;
			} else {
				if (!xfer->cs_off)
					spi_engine_gen_cs(p, dry, spi, false);

				spi_engine_gen_sleep(p, dry, spi_delay_to_ns(
					&xfer->cs_change_delay, xfer),
					xfer->effective_speed_hz);

				if (!list_next_entry(xfer, transfer_list)->cs_off)
					spi_engine_gen_cs(p, dry, spi, true);
			}
		} else if (!list_is_last(&xfer->transfer_list, &msg->transfers) &&
			   xfer->cs_off != list_next_entry(xfer, transfer_list)->cs_off) {
			spi_engine_gen_cs(p, dry, spi, xfer->cs_off);
		}
	}

	if (!keep_cs)
		spi_engine_gen_cs(p, dry, spi, false);

	/*
	 * Restore clockdiv to default so that future gen_sleep commands don't
	 * have to be aware of the current register state.
	 */
	if (clk_div != 1)
		spi_engine_program_add_cmd(p, dry,
			SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_CLK_DIV, 0));
}

static void spi_engine_xfer_next(struct spi_message *msg,
	struct spi_transfer **_xfer)
{
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

static void spi_engine_tx_next(struct spi_message *msg)
{
	struct spi_engine_message_state *st = msg->state;
	struct spi_transfer *xfer = st->tx_xfer;

	do {
		spi_engine_xfer_next(msg, &xfer);
	} while (xfer && !xfer->tx_buf);

	st->tx_xfer = xfer;
	if (xfer) {
		st->tx_length = xfer->len;
		st->tx_buf = xfer->tx_buf;
	} else {
		st->tx_buf = NULL;
	}
}

static void spi_engine_rx_next(struct spi_message *msg)
{
	struct spi_engine_message_state *st = msg->state;
	struct spi_transfer *xfer = st->rx_xfer;

	do {
		spi_engine_xfer_next(msg, &xfer);
	} while (xfer && !xfer->rx_buf);

	st->rx_xfer = xfer;
	if (xfer) {
		st->rx_length = xfer->len;
		st->rx_buf = xfer->rx_buf;
	} else {
		st->rx_buf = NULL;
	}
}

static bool spi_engine_write_cmd_fifo(struct spi_engine *spi_engine,
				      struct spi_message *msg)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_CMD_FIFO;
	struct spi_engine_message_state *st = msg->state;
	unsigned int n, m, i;
	const uint16_t *buf;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_CMD_FIFO_ROOM);
	while (n && st->cmd_length) {
		m = min(n, st->cmd_length);
		buf = st->cmd_buf;
		for (i = 0; i < m; i++)
			writel_relaxed(buf[i], addr);
		st->cmd_buf += m;
		st->cmd_length -= m;
		n -= m;
	}

	return st->cmd_length != 0;
}

static bool spi_engine_write_tx_fifo(struct spi_engine *spi_engine,
				     struct spi_message *msg)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_SDO_DATA_FIFO;
	struct spi_engine_message_state *st = msg->state;
	unsigned int n, m, i;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_SDO_FIFO_ROOM);
	while (n && st->tx_length) {
		if (st->tx_xfer->bits_per_word <= 8) {
			const u8 *buf = st->tx_buf;

			m = min(n, st->tx_length);
			for (i = 0; i < m; i++)
				writel_relaxed(buf[i], addr);
			st->tx_buf += m;
			st->tx_length -= m;
		} else if (st->tx_xfer->bits_per_word <= 16) {
			const u16 *buf = (const u16 *)st->tx_buf;

			m = min(n, st->tx_length / 2);
			for (i = 0; i < m; i++)
				writel_relaxed(buf[i], addr);
			st->tx_buf += m * 2;
			st->tx_length -= m * 2;
		} else {
			const u32 *buf = (const u32 *)st->tx_buf;

			m = min(n, st->tx_length / 4);
			for (i = 0; i < m; i++)
				writel_relaxed(buf[i], addr);
			st->tx_buf += m * 4;
			st->tx_length -= m * 4;
		}
		n -= m;
		if (st->tx_length == 0)
			spi_engine_tx_next(msg);
	}

	return st->tx_length != 0;
}

static bool spi_engine_read_rx_fifo(struct spi_engine *spi_engine,
				    struct spi_message *msg)
{
	void __iomem *addr = spi_engine->base + SPI_ENGINE_REG_SDI_DATA_FIFO;
	struct spi_engine_message_state *st = msg->state;
	unsigned int n, m, i;

	n = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_SDI_FIFO_LEVEL);
	while (n && st->rx_length) {
		if (st->rx_xfer->bits_per_word <= 8) {
			u8 *buf = st->rx_buf;

			m = min(n, st->rx_length);
			for (i = 0; i < m; i++)
				buf[i] = readl_relaxed(addr);
			st->rx_buf += m;
			st->rx_length -= m;
		} else if (st->rx_xfer->bits_per_word <= 16) {
			u16 *buf = (u16 *)st->rx_buf;

			m = min(n, st->rx_length / 2);
			for (i = 0; i < m; i++)
				buf[i] = readl_relaxed(addr);
			st->rx_buf += m * 2;
			st->rx_length -= m * 2;
		} else {
			u32 *buf = (u32 *)st->rx_buf;

			m = min(n, st->rx_length / 4);
			for (i = 0; i < m; i++)
				buf[i] = readl_relaxed(addr);
			st->rx_buf += m * 4;
			st->rx_length -= m * 4;
		}
		n -= m;
		if (st->rx_length == 0)
			spi_engine_rx_next(msg);
	}

	return st->rx_length != 0;
}

static irqreturn_t spi_engine_irq(int irq, void *devid)
{
	struct spi_controller *host = devid;
	struct spi_message *msg = host->cur_msg;
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	unsigned int disable_int = 0;
	unsigned int pending;
	int completed_id = -1;

	pending = readl_relaxed(spi_engine->base + SPI_ENGINE_REG_INT_PENDING);

	if (pending & SPI_ENGINE_INT_SYNC) {
		writel_relaxed(SPI_ENGINE_INT_SYNC,
			spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
		completed_id = readl_relaxed(
			spi_engine->base + SPI_ENGINE_REG_SYNC_ID);
	}

	spin_lock(&spi_engine->lock);

	if (pending & SPI_ENGINE_INT_CMD_ALMOST_EMPTY) {
		if (!spi_engine_write_cmd_fifo(spi_engine, msg))
			disable_int |= SPI_ENGINE_INT_CMD_ALMOST_EMPTY;
	}

	if (pending & SPI_ENGINE_INT_SDO_ALMOST_EMPTY) {
		if (!spi_engine_write_tx_fifo(spi_engine, msg))
			disable_int |= SPI_ENGINE_INT_SDO_ALMOST_EMPTY;
	}

	if (pending & (SPI_ENGINE_INT_SDI_ALMOST_FULL | SPI_ENGINE_INT_SYNC)) {
		if (!spi_engine_read_rx_fifo(spi_engine, msg))
			disable_int |= SPI_ENGINE_INT_SDI_ALMOST_FULL;
	}

	if (pending & SPI_ENGINE_INT_SYNC && msg) {
		struct spi_engine_message_state *st = msg->state;

		if (completed_id == st->sync_id) {
			if (timer_delete_sync(&spi_engine->watchdog_timer)) {
				msg->status = 0;
				msg->actual_length = msg->frame_length;
				spi_finalize_current_message(host);
			}
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

static int spi_engine_prepare_message(struct spi_controller *host,
				      struct spi_message *msg)
{
	struct spi_engine_program p_dry, *p;
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_message_state *st;
	size_t size;
	int ret;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	spi_engine_precompile_message(msg);

	p_dry.length = 0;
	spi_engine_compile_message(msg, true, &p_dry);

	size = sizeof(*p->instructions) * (p_dry.length + 1);
	p = kzalloc(sizeof(*p) + size, GFP_KERNEL);
	if (!p) {
		kfree(st);
		return -ENOMEM;
	}

	ret = ida_alloc_range(&spi_engine->sync_ida, 0, U8_MAX, GFP_KERNEL);
	if (ret < 0) {
		kfree(p);
		kfree(st);
		return ret;
	}

	st->sync_id = ret;

	spi_engine_compile_message(msg, false, p);

	spi_engine_program_add_cmd(p, false, SPI_ENGINE_CMD_SYNC(st->sync_id));

	st->p = p;
	st->cmd_buf = p->instructions;
	st->cmd_length = p->length;
	msg->state = st;

	return 0;
}

static int spi_engine_unprepare_message(struct spi_controller *host,
					struct spi_message *msg)
{
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_message_state *st = msg->state;

	ida_free(&spi_engine->sync_ida, st->sync_id);
	kfree(st->p);
	kfree(st);

	return 0;
}

static int spi_engine_transfer_one_message(struct spi_controller *host,
	struct spi_message *msg)
{
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_message_state *st = msg->state;
	unsigned int int_enable = 0;
	unsigned long flags;

	mod_timer(&spi_engine->watchdog_timer, jiffies + msecs_to_jiffies(5000));

	spin_lock_irqsave(&spi_engine->lock, flags);

	if (spi_engine_write_cmd_fifo(spi_engine, msg))
		int_enable |= SPI_ENGINE_INT_CMD_ALMOST_EMPTY;

	spi_engine_tx_next(msg);
	if (spi_engine_write_tx_fifo(spi_engine, msg))
		int_enable |= SPI_ENGINE_INT_SDO_ALMOST_EMPTY;

	spi_engine_rx_next(msg);
	if (st->rx_length != 0)
		int_enable |= SPI_ENGINE_INT_SDI_ALMOST_FULL;

	int_enable |= SPI_ENGINE_INT_SYNC;

	writel_relaxed(int_enable,
		spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);
	spi_engine->int_enable = int_enable;
	spin_unlock_irqrestore(&spi_engine->lock, flags);

	return 0;
}

static void spi_engine_timeout(struct timer_list *timer)
{
	struct spi_engine *spi_engine = from_timer(spi_engine, timer, watchdog_timer);
	struct spi_controller *host = spi_engine->controller;

	if (WARN_ON(!host->cur_msg))
		return;

	dev_err(&host->dev,
		"Timeout occurred while waiting for transfer to complete. Hardware is probably broken.\n");
	host->cur_msg->status = -ETIMEDOUT;
	spi_finalize_current_message(host);
}

static void spi_engine_release_hw(void *p)
{
	struct spi_engine *spi_engine = p;

	writel_relaxed(0xff, spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);
	writel_relaxed(0x01, spi_engine->base + SPI_ENGINE_REG_RESET);
}

static int spi_engine_probe(struct platform_device *pdev)
{
	struct spi_engine *spi_engine;
	struct spi_controller *host;
	unsigned int version;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = devm_spi_alloc_host(&pdev->dev, sizeof(*spi_engine));
	if (!host)
		return -ENOMEM;

	spi_engine = spi_controller_get_devdata(host);

	spin_lock_init(&spi_engine->lock);
	ida_init(&spi_engine->sync_ida);
	timer_setup(&spi_engine->watchdog_timer, spi_engine_timeout, TIMER_IRQSAFE);
	spi_engine->controller = host;

	spi_engine->clk = devm_clk_get_enabled(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(spi_engine->clk))
		return PTR_ERR(spi_engine->clk);

	spi_engine->ref_clk = devm_clk_get_enabled(&pdev->dev, "spi_clk");
	if (IS_ERR(spi_engine->ref_clk))
		return PTR_ERR(spi_engine->ref_clk);

	spi_engine->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi_engine->base))
		return PTR_ERR(spi_engine->base);

	version = readl(spi_engine->base + SPI_ENGINE_REG_VERSION);
	if (SPI_ENGINE_VERSION_MAJOR(version) != 1) {
		dev_err(&pdev->dev, "Unsupported peripheral version %u.%u.%c\n",
			SPI_ENGINE_VERSION_MAJOR(version),
			SPI_ENGINE_VERSION_MINOR(version),
			SPI_ENGINE_VERSION_PATCH(version));
		return -ENODEV;
	}

	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_RESET);
	writel_relaxed(0xff, spi_engine->base + SPI_ENGINE_REG_INT_PENDING);
	writel_relaxed(0x00, spi_engine->base + SPI_ENGINE_REG_INT_ENABLE);

	ret = devm_add_action_or_reset(&pdev->dev, spi_engine_release_hw,
				       spi_engine);
	if (ret)
		return ret;

	ret = devm_request_irq(&pdev->dev, irq, spi_engine_irq, 0, pdev->name,
			       host);
	if (ret)
		return ret;

	host->dev.of_node = pdev->dev.of_node;
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_3WIRE;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	host->max_speed_hz = clk_get_rate(spi_engine->ref_clk) / 2;
	host->transfer_one_message = spi_engine_transfer_one_message;
	host->prepare_message = spi_engine_prepare_message;
	host->unprepare_message = spi_engine_unprepare_message;
	host->num_chipselect = 8;

	if (host->max_speed_hz == 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "spi_clk rate is 0");

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, host);

	return 0;
}

static const struct of_device_id spi_engine_match_table[] = {
	{ .compatible = "adi,axi-spi-engine-1.00.a" },
	{ },
};
MODULE_DEVICE_TABLE(of, spi_engine_match_table);

static struct platform_driver spi_engine_driver = {
	.probe = spi_engine_probe,
	.driver = {
		.name = "spi-engine",
		.of_match_table = spi_engine_match_table,
	},
};
module_platform_driver(spi_engine_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices SPI engine peripheral driver");
MODULE_LICENSE("GPL");
