// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI-Engine SPI controller driver
 * Copyright 2015 Analog Devices Inc.
 * Copyright 2024 BayLibre, SAS
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/fpga/adi-axi-common.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/spi.h>
#include <trace/events/spi.h>

#define SPI_ENGINE_REG_OFFLOAD_MEM_ADDR_WIDTH	0x10
#define SPI_ENGINE_REG_RESET			0x40

#define SPI_ENGINE_REG_INT_ENABLE		0x80
#define SPI_ENGINE_REG_INT_PENDING		0x84
#define SPI_ENGINE_REG_INT_SOURCE		0x88

#define SPI_ENGINE_REG_SYNC_ID			0xc0
#define SPI_ENGINE_REG_OFFLOAD_SYNC_ID		0xc4

#define SPI_ENGINE_REG_CMD_FIFO_ROOM		0xd0
#define SPI_ENGINE_REG_SDO_FIFO_ROOM		0xd4
#define SPI_ENGINE_REG_SDI_FIFO_LEVEL		0xd8

#define SPI_ENGINE_REG_CMD_FIFO			0xe0
#define SPI_ENGINE_REG_SDO_DATA_FIFO		0xe4
#define SPI_ENGINE_REG_SDI_DATA_FIFO		0xe8
#define SPI_ENGINE_REG_SDI_DATA_FIFO_PEEK	0xec

#define SPI_ENGINE_MAX_NUM_OFFLOADS		32

#define SPI_ENGINE_REG_OFFLOAD_CTRL(x)		(0x100 + SPI_ENGINE_MAX_NUM_OFFLOADS * (x))
#define SPI_ENGINE_REG_OFFLOAD_STATUS(x)	(0x104 + SPI_ENGINE_MAX_NUM_OFFLOADS * (x))
#define SPI_ENGINE_REG_OFFLOAD_RESET(x)		(0x108 + SPI_ENGINE_MAX_NUM_OFFLOADS * (x))
#define SPI_ENGINE_REG_OFFLOAD_CMD_FIFO(x)	(0x110 + SPI_ENGINE_MAX_NUM_OFFLOADS * (x))
#define SPI_ENGINE_REG_OFFLOAD_SDO_FIFO(x)	(0x114 + SPI_ENGINE_MAX_NUM_OFFLOADS * (x))

#define SPI_ENGINE_SPI_OFFLOAD_MEM_WIDTH_SDO	GENMASK(15, 8)
#define SPI_ENGINE_SPI_OFFLOAD_MEM_WIDTH_CMD	GENMASK(7, 0)

#define SPI_ENGINE_INT_CMD_ALMOST_EMPTY		BIT(0)
#define SPI_ENGINE_INT_SDO_ALMOST_EMPTY		BIT(1)
#define SPI_ENGINE_INT_SDI_ALMOST_FULL		BIT(2)
#define SPI_ENGINE_INT_SYNC			BIT(3)
#define SPI_ENGINE_INT_OFFLOAD_SYNC		BIT(4)

#define SPI_ENGINE_OFFLOAD_CTRL_ENABLE		BIT(0)

#define SPI_ENGINE_CONFIG_CPHA			BIT(0)
#define SPI_ENGINE_CONFIG_CPOL			BIT(1)
#define SPI_ENGINE_CONFIG_3WIRE			BIT(2)
#define SPI_ENGINE_CONFIG_SDO_IDLE_HIGH		BIT(3)

#define SPI_ENGINE_INST_TRANSFER		0x0
#define SPI_ENGINE_INST_ASSERT			0x1
#define SPI_ENGINE_INST_WRITE			0x2
#define SPI_ENGINE_INST_MISC			0x3
#define SPI_ENGINE_INST_CS_INV			0x4

#define SPI_ENGINE_CMD_REG_CLK_DIV		0x0
#define SPI_ENGINE_CMD_REG_CONFIG		0x1
#define SPI_ENGINE_CMD_REG_XFER_BITS		0x2

#define SPI_ENGINE_MISC_SYNC			0x0
#define SPI_ENGINE_MISC_SLEEP			0x1

#define SPI_ENGINE_TRANSFER_WRITE		0x1
#define SPI_ENGINE_TRANSFER_READ		0x2

/* Arbitrary sync ID for use by host->cur_msg */
#define AXI_SPI_ENGINE_CUR_MSG_SYNC_ID		0x1

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
#define SPI_ENGINE_CMD_CS_INV(flags) \
	SPI_ENGINE_CMD(SPI_ENGINE_INST_CS_INV, 0, (flags))

/* default sizes - can be changed when SPI Engine firmware is compiled */
#define SPI_ENGINE_OFFLOAD_CMD_FIFO_SIZE	16
#define SPI_ENGINE_OFFLOAD_SDO_FIFO_SIZE	16

struct spi_engine_program {
	unsigned int length;
	uint16_t instructions[] __counted_by(length);
};

/**
 * struct spi_engine_message_state - SPI engine per-message state
 */
struct spi_engine_message_state {
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
};

enum {
	SPI_ENGINE_OFFLOAD_FLAG_ASSIGNED,
	SPI_ENGINE_OFFLOAD_FLAG_PREPARED,
};

struct spi_engine_offload {
	struct spi_engine *spi_engine;
	unsigned long flags;
	unsigned int offload_num;
};

struct spi_engine {
	struct clk *clk;
	struct clk *ref_clk;

	spinlock_t lock;

	void __iomem *base;
	struct spi_engine_message_state msg_state;
	struct completion msg_complete;
	unsigned int int_enable;
	/* shadows hardware CS inversion flag state */
	u8 cs_inv;

	unsigned int offload_ctrl_mem_size;
	unsigned int offload_sdo_mem_size;
	struct spi_offload *offload;
	u32 offload_caps;
};

static void spi_engine_program_add_cmd(struct spi_engine_program *p,
	bool dry, uint16_t cmd)
{
	p->length++;

	if (!dry)
		p->instructions[p->length - 1] = cmd;
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
	if (spi->mode & SPI_MOSI_IDLE_HIGH)
		config |= SPI_ENGINE_CONFIG_SDO_IDLE_HIGH;
	if (spi->mode & SPI_MOSI_IDLE_LOW)
		config &= ~SPI_ENGINE_CONFIG_SDO_IDLE_HIGH;

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

		if (xfer->tx_buf || (xfer->offload_flags & SPI_OFFLOAD_XFER_TX_STREAM))
			flags |= SPI_ENGINE_TRANSFER_WRITE;
		if (xfer->rx_buf || (xfer->offload_flags & SPI_OFFLOAD_XFER_RX_STREAM))
			flags |= SPI_ENGINE_TRANSFER_READ;

		spi_engine_program_add_cmd(p, dry,
			SPI_ENGINE_CMD_TRANSFER(flags, n - 1));
		len -= n;
	}
}

static void spi_engine_gen_sleep(struct spi_engine_program *p, bool dry,
				 int delay_ns, int inst_ns, u32 sclk_hz)
{
	unsigned int t;

	/*
	 * Negative delay indicates error, e.g. from spi_delay_to_ns(). And if
	 * delay is less that the instruction execution time, there is no need
	 * for an extra sleep instruction since the instruction execution time
	 * will already cover the required delay.
	 */
	if (delay_ns < 0 || delay_ns <= inst_ns)
		return;

	t = DIV_ROUND_UP_ULL((u64)(delay_ns - inst_ns) * sclk_hz, NSEC_PER_SEC);
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
 *
 * Returns 0 on success, -EINVAL on failure.
 */
static int spi_engine_precompile_message(struct spi_message *msg)
{
	unsigned int clk_div, max_hz = msg->spi->controller->max_speed_hz;
	struct spi_transfer *xfer;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* If we have an offload transfer, we can't rx to buffer */
		if (msg->offload && xfer->rx_buf)
			return -EINVAL;

		clk_div = DIV_ROUND_UP(max_hz, xfer->speed_hz);
		xfer->effective_speed_hz = max_hz / min(clk_div, 256U);
	}

	return 0;
}

static void spi_engine_compile_message(struct spi_message *msg, bool dry,
				       struct spi_engine_program *p)
{
	struct spi_device *spi = msg->spi;
	struct spi_controller *host = spi->controller;
	struct spi_transfer *xfer;
	int clk_div, new_clk_div, inst_ns;
	bool keep_cs = false;
	u8 bits_per_word = 0;

	/*
	 * Take into account instruction execution time for more accurate sleep
	 * times, especially when the delay is small.
	 */
	inst_ns = DIV_ROUND_UP(NSEC_PER_SEC, host->max_speed_hz);

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

		if (bits_per_word != xfer->bits_per_word && xfer->len) {
			bits_per_word = xfer->bits_per_word;
			spi_engine_program_add_cmd(p, dry,
				SPI_ENGINE_CMD_WRITE(SPI_ENGINE_CMD_REG_XFER_BITS,
					bits_per_word));
		}

		spi_engine_gen_xfer(p, dry, xfer);
		spi_engine_gen_sleep(p, dry, spi_delay_to_ns(&xfer->delay, xfer),
				     inst_ns, xfer->effective_speed_hz);

		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list, &msg->transfers)) {
				keep_cs = true;
			} else {
				if (!xfer->cs_off)
					spi_engine_gen_cs(p, dry, spi, false);

				spi_engine_gen_sleep(p, dry, spi_delay_to_ns(
					&xfer->cs_change_delay, xfer), inst_ns,
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
		if (completed_id == AXI_SPI_ENGINE_CUR_MSG_SYNC_ID) {
			msg->status = 0;
			msg->actual_length = msg->frame_length;
			complete(&spi_engine->msg_complete);
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

static int spi_engine_offload_prepare(struct spi_message *msg)
{
	struct spi_controller *host = msg->spi->controller;
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_program *p = msg->opt_state;
	struct spi_engine_offload *priv = msg->offload->priv;
	struct spi_transfer *xfer;
	void __iomem *cmd_addr;
	void __iomem *sdo_addr;
	size_t tx_word_count = 0;
	unsigned int i;

	if (p->length > spi_engine->offload_ctrl_mem_size)
		return -EINVAL;

	/* count total number of tx words in message */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* no support for reading to rx_buf */
		if (xfer->rx_buf)
			return -EINVAL;

		if (!xfer->tx_buf)
			continue;

		if (xfer->bits_per_word <= 8)
			tx_word_count += xfer->len;
		else if (xfer->bits_per_word <= 16)
			tx_word_count += xfer->len / 2;
		else
			tx_word_count += xfer->len / 4;
	}

	if (tx_word_count && !(spi_engine->offload_caps & SPI_OFFLOAD_CAP_TX_STATIC_DATA))
		return -EINVAL;

	if (tx_word_count > spi_engine->offload_sdo_mem_size)
		return -EINVAL;

	/*
	 * This protects against calling spi_optimize_message() with an offload
	 * that has already been prepared with a different message.
	 */
	if (test_and_set_bit_lock(SPI_ENGINE_OFFLOAD_FLAG_PREPARED, &priv->flags))
		return -EBUSY;

	cmd_addr = spi_engine->base +
		   SPI_ENGINE_REG_OFFLOAD_CMD_FIFO(priv->offload_num);
	sdo_addr = spi_engine->base +
		   SPI_ENGINE_REG_OFFLOAD_SDO_FIFO(priv->offload_num);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!xfer->tx_buf)
			continue;

		if (xfer->bits_per_word <= 8) {
			const u8 *buf = xfer->tx_buf;

			for (i = 0; i < xfer->len; i++)
				writel_relaxed(buf[i], sdo_addr);
		} else if (xfer->bits_per_word <= 16) {
			const u16 *buf = xfer->tx_buf;

			for (i = 0; i < xfer->len / 2; i++)
				writel_relaxed(buf[i], sdo_addr);
		} else {
			const u32 *buf = xfer->tx_buf;

			for (i = 0; i < xfer->len / 4; i++)
				writel_relaxed(buf[i], sdo_addr);
		}
	}

	for (i = 0; i < p->length; i++)
		writel_relaxed(p->instructions[i], cmd_addr);

	return 0;
}

static void spi_engine_offload_unprepare(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;
	struct spi_engine *spi_engine = priv->spi_engine;

	writel_relaxed(1, spi_engine->base +
			  SPI_ENGINE_REG_OFFLOAD_RESET(priv->offload_num));
	writel_relaxed(0, spi_engine->base +
			  SPI_ENGINE_REG_OFFLOAD_RESET(priv->offload_num));

	clear_bit_unlock(SPI_ENGINE_OFFLOAD_FLAG_PREPARED, &priv->flags);
}

static int spi_engine_optimize_message(struct spi_message *msg)
{
	struct spi_engine_program p_dry, *p;
	int ret;

	ret = spi_engine_precompile_message(msg);
	if (ret)
		return ret;

	p_dry.length = 0;
	spi_engine_compile_message(msg, true, &p_dry);

	p = kzalloc(struct_size(p, instructions, p_dry.length + 1), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	spi_engine_compile_message(msg, false, p);

	spi_engine_program_add_cmd(p, false, SPI_ENGINE_CMD_SYNC(
		msg->offload ? 0 : AXI_SPI_ENGINE_CUR_MSG_SYNC_ID));

	msg->opt_state = p;

	if (msg->offload) {
		ret = spi_engine_offload_prepare(msg);
		if (ret) {
			msg->opt_state = NULL;
			kfree(p);
			return ret;
		}
	}

	return 0;
}

static int spi_engine_unoptimize_message(struct spi_message *msg)
{
	if (msg->offload)
		spi_engine_offload_unprepare(msg->offload);

	kfree(msg->opt_state);

	return 0;
}

static struct spi_offload
*spi_engine_get_offload(struct spi_device *spi,
			const struct spi_offload_config *config)
{
	struct spi_controller *host = spi->controller;
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_offload *priv;

	if (!spi_engine->offload)
		return ERR_PTR(-ENODEV);

	if (config->capability_flags & ~spi_engine->offload_caps)
		return ERR_PTR(-EINVAL);

	priv = spi_engine->offload->priv;

	if (test_and_set_bit_lock(SPI_ENGINE_OFFLOAD_FLAG_ASSIGNED, &priv->flags))
		return ERR_PTR(-EBUSY);

	return spi_engine->offload;
}

static void spi_engine_put_offload(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;

	clear_bit_unlock(SPI_ENGINE_OFFLOAD_FLAG_ASSIGNED, &priv->flags);
}

static int spi_engine_setup(struct spi_device *device)
{
	struct spi_controller *host = device->controller;
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);

	if (device->mode & SPI_CS_HIGH)
		spi_engine->cs_inv |= BIT(spi_get_chipselect(device, 0));
	else
		spi_engine->cs_inv &= ~BIT(spi_get_chipselect(device, 0));

	writel_relaxed(SPI_ENGINE_CMD_CS_INV(spi_engine->cs_inv),
		       spi_engine->base + SPI_ENGINE_REG_CMD_FIFO);

	/*
	 * In addition to setting the flags, we have to do a CS assert command
	 * to make the new setting actually take effect.
	 */
	writel_relaxed(SPI_ENGINE_CMD_ASSERT(0, 0xff),
		       spi_engine->base + SPI_ENGINE_REG_CMD_FIFO);

	return 0;
}

static int spi_engine_transfer_one_message(struct spi_controller *host,
	struct spi_message *msg)
{
	struct spi_engine *spi_engine = spi_controller_get_devdata(host);
	struct spi_engine_message_state *st = &spi_engine->msg_state;
	struct spi_engine_program *p = msg->opt_state;
	unsigned int int_enable = 0;
	unsigned long flags;

	if (msg->offload) {
		dev_err(&host->dev, "Single transfer offload not supported\n");
		msg->status = -EOPNOTSUPP;
		goto out;
	}

	/* reinitialize message state for this transfer */
	memset(st, 0, sizeof(*st));
	st->cmd_buf = p->instructions;
	st->cmd_length = p->length;
	msg->state = st;

	reinit_completion(&spi_engine->msg_complete);

	if (trace_spi_transfer_start_enabled()) {
		struct spi_transfer *xfer;

		list_for_each_entry(xfer, &msg->transfers, transfer_list)
			trace_spi_transfer_start(msg, xfer);
	}

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

	if (!wait_for_completion_timeout(&spi_engine->msg_complete,
					 msecs_to_jiffies(5000))) {
		dev_err(&host->dev,
			"Timeout occurred while waiting for transfer to complete. Hardware is probably broken.\n");
		msg->status = -ETIMEDOUT;
	}

	if (trace_spi_transfer_stop_enabled()) {
		struct spi_transfer *xfer;

		list_for_each_entry(xfer, &msg->transfers, transfer_list)
			trace_spi_transfer_stop(msg, xfer);
	}

out:
	spi_finalize_current_message(host);

	return msg->status;
}

static int spi_engine_trigger_enable(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;
	struct spi_engine *spi_engine = priv->spi_engine;
	unsigned int reg;

	reg = readl_relaxed(spi_engine->base +
			    SPI_ENGINE_REG_OFFLOAD_CTRL(priv->offload_num));
	reg |= SPI_ENGINE_OFFLOAD_CTRL_ENABLE;
	writel_relaxed(reg, spi_engine->base +
			    SPI_ENGINE_REG_OFFLOAD_CTRL(priv->offload_num));
	return 0;
}

static void spi_engine_trigger_disable(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;
	struct spi_engine *spi_engine = priv->spi_engine;
	unsigned int reg;

	reg = readl_relaxed(spi_engine->base +
			    SPI_ENGINE_REG_OFFLOAD_CTRL(priv->offload_num));
	reg &= ~SPI_ENGINE_OFFLOAD_CTRL_ENABLE;
	writel_relaxed(reg, spi_engine->base +
			    SPI_ENGINE_REG_OFFLOAD_CTRL(priv->offload_num));
}

static struct dma_chan
*spi_engine_tx_stream_request_dma_chan(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;
	char name[16];

	snprintf(name, sizeof(name), "offload%u-tx", priv->offload_num);

	return dma_request_chan(offload->provider_dev, name);
}

static struct dma_chan
*spi_engine_rx_stream_request_dma_chan(struct spi_offload *offload)
{
	struct spi_engine_offload *priv = offload->priv;
	char name[16];

	snprintf(name, sizeof(name), "offload%u-rx", priv->offload_num);

	return dma_request_chan(offload->provider_dev, name);
}

static const struct spi_offload_ops spi_engine_offload_ops = {
	.trigger_enable = spi_engine_trigger_enable,
	.trigger_disable = spi_engine_trigger_disable,
	.tx_stream_request_dma_chan = spi_engine_tx_stream_request_dma_chan,
	.rx_stream_request_dma_chan = spi_engine_rx_stream_request_dma_chan,
};

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
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = devm_spi_alloc_host(&pdev->dev, sizeof(*spi_engine));
	if (!host)
		return -ENOMEM;

	spi_engine = spi_controller_get_devdata(host);

	spin_lock_init(&spi_engine->lock);
	init_completion(&spi_engine->msg_complete);

	/*
	 * REVISIT: for now, all SPI Engines only have one offload. In the
	 * future, this should be read from a memory mapped register to
	 * determine the number of offloads enabled at HDL compile time. For
	 * now, we can tell if an offload is present if there is a trigger
	 * source wired up to it.
	 */
	if (device_property_present(&pdev->dev, "trigger-sources")) {
		struct spi_engine_offload *priv;

		spi_engine->offload =
			devm_spi_offload_alloc(&pdev->dev,
					       sizeof(struct spi_engine_offload));
		if (IS_ERR(spi_engine->offload))
			return PTR_ERR(spi_engine->offload);

		priv = spi_engine->offload->priv;
		priv->spi_engine = spi_engine;
		priv->offload_num = 0;

		spi_engine->offload->ops = &spi_engine_offload_ops;
		spi_engine->offload_caps = SPI_OFFLOAD_CAP_TRIGGER;

		if (device_property_match_string(&pdev->dev, "dma-names", "offload0-rx") >= 0) {
			spi_engine->offload_caps |= SPI_OFFLOAD_CAP_RX_STREAM_DMA;
			spi_engine->offload->xfer_flags |= SPI_OFFLOAD_XFER_RX_STREAM;
		}

		if (device_property_match_string(&pdev->dev, "dma-names", "offload0-tx") >= 0) {
			spi_engine->offload_caps |= SPI_OFFLOAD_CAP_TX_STREAM_DMA;
			spi_engine->offload->xfer_flags |= SPI_OFFLOAD_XFER_TX_STREAM;
		} else {
			/*
			 * HDL compile option to enable TX DMA stream also disables
			 * the SDO memory, so can't do both at the same time.
			 */
			spi_engine->offload_caps |= SPI_OFFLOAD_CAP_TX_STATIC_DATA;
		}
	}

	spi_engine->clk = devm_clk_get_enabled(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(spi_engine->clk))
		return PTR_ERR(spi_engine->clk);

	spi_engine->ref_clk = devm_clk_get_enabled(&pdev->dev, "spi_clk");
	if (IS_ERR(spi_engine->ref_clk))
		return PTR_ERR(spi_engine->ref_clk);

	spi_engine->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi_engine->base))
		return PTR_ERR(spi_engine->base);

	version = readl(spi_engine->base + ADI_AXI_REG_VERSION);
	if (ADI_AXI_PCORE_VER_MAJOR(version) != 1) {
		dev_err(&pdev->dev, "Unsupported peripheral version %u.%u.%u\n",
			ADI_AXI_PCORE_VER_MAJOR(version),
			ADI_AXI_PCORE_VER_MINOR(version),
			ADI_AXI_PCORE_VER_PATCH(version));
		return -ENODEV;
	}

	if (ADI_AXI_PCORE_VER_MINOR(version) >= 1) {
		unsigned int sizes = readl(spi_engine->base +
				SPI_ENGINE_REG_OFFLOAD_MEM_ADDR_WIDTH);

		spi_engine->offload_ctrl_mem_size = 1 <<
			FIELD_GET(SPI_ENGINE_SPI_OFFLOAD_MEM_WIDTH_CMD, sizes);
		spi_engine->offload_sdo_mem_size = 1 <<
			FIELD_GET(SPI_ENGINE_SPI_OFFLOAD_MEM_WIDTH_SDO, sizes);
	} else {
		spi_engine->offload_ctrl_mem_size = SPI_ENGINE_OFFLOAD_CMD_FIFO_SIZE;
		spi_engine->offload_sdo_mem_size = SPI_ENGINE_OFFLOAD_SDO_FIFO_SIZE;
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
	host->optimize_message = spi_engine_optimize_message;
	host->unoptimize_message = spi_engine_unoptimize_message;
	host->get_offload = spi_engine_get_offload;
	host->put_offload = spi_engine_put_offload;
	host->num_chipselect = 8;

	/* Some features depend of the IP core version. */
	if (ADI_AXI_PCORE_VER_MAJOR(version) >= 1) {
		if (ADI_AXI_PCORE_VER_MINOR(version) >= 2) {
			host->mode_bits |= SPI_CS_HIGH;
			host->setup = spi_engine_setup;
		}
		if (ADI_AXI_PCORE_VER_MINOR(version) >= 3)
			host->mode_bits |= SPI_MOSI_IDLE_LOW | SPI_MOSI_IDLE_HIGH;
	}

	if (host->max_speed_hz == 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "spi_clk rate is 0");

	return devm_spi_register_controller(&pdev->dev, host);
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
