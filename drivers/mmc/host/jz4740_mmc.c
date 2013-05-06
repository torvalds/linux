/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SD/MMC controller driver
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/mmc/host.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/clk.h>

#include <linux/bitops.h>
#include <linux/gpio.h>
#include <asm/mach-jz4740/gpio.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>

#include <asm/mach-jz4740/jz4740_mmc.h>

#define JZ_REG_MMC_STRPCL	0x00
#define JZ_REG_MMC_STATUS	0x04
#define JZ_REG_MMC_CLKRT	0x08
#define JZ_REG_MMC_CMDAT	0x0C
#define JZ_REG_MMC_RESTO	0x10
#define JZ_REG_MMC_RDTO		0x14
#define JZ_REG_MMC_BLKLEN	0x18
#define JZ_REG_MMC_NOB		0x1C
#define JZ_REG_MMC_SNOB		0x20
#define JZ_REG_MMC_IMASK	0x24
#define JZ_REG_MMC_IREG		0x28
#define JZ_REG_MMC_CMD		0x2C
#define JZ_REG_MMC_ARG		0x30
#define JZ_REG_MMC_RESP_FIFO	0x34
#define JZ_REG_MMC_RXFIFO	0x38
#define JZ_REG_MMC_TXFIFO	0x3C

#define JZ_MMC_STRPCL_EXIT_MULTIPLE BIT(7)
#define JZ_MMC_STRPCL_EXIT_TRANSFER BIT(6)
#define JZ_MMC_STRPCL_START_READWAIT BIT(5)
#define JZ_MMC_STRPCL_STOP_READWAIT BIT(4)
#define JZ_MMC_STRPCL_RESET BIT(3)
#define JZ_MMC_STRPCL_START_OP BIT(2)
#define JZ_MMC_STRPCL_CLOCK_CONTROL (BIT(1) | BIT(0))
#define JZ_MMC_STRPCL_CLOCK_STOP BIT(0)
#define JZ_MMC_STRPCL_CLOCK_START BIT(1)


#define JZ_MMC_STATUS_IS_RESETTING BIT(15)
#define JZ_MMC_STATUS_SDIO_INT_ACTIVE BIT(14)
#define JZ_MMC_STATUS_PRG_DONE BIT(13)
#define JZ_MMC_STATUS_DATA_TRAN_DONE BIT(12)
#define JZ_MMC_STATUS_END_CMD_RES BIT(11)
#define JZ_MMC_STATUS_DATA_FIFO_AFULL BIT(10)
#define JZ_MMC_STATUS_IS_READWAIT BIT(9)
#define JZ_MMC_STATUS_CLK_EN BIT(8)
#define JZ_MMC_STATUS_DATA_FIFO_FULL BIT(7)
#define JZ_MMC_STATUS_DATA_FIFO_EMPTY BIT(6)
#define JZ_MMC_STATUS_CRC_RES_ERR BIT(5)
#define JZ_MMC_STATUS_CRC_READ_ERROR BIT(4)
#define JZ_MMC_STATUS_TIMEOUT_WRITE BIT(3)
#define JZ_MMC_STATUS_CRC_WRITE_ERROR BIT(2)
#define JZ_MMC_STATUS_TIMEOUT_RES BIT(1)
#define JZ_MMC_STATUS_TIMEOUT_READ BIT(0)

#define JZ_MMC_STATUS_READ_ERROR_MASK (BIT(4) | BIT(0))
#define JZ_MMC_STATUS_WRITE_ERROR_MASK (BIT(3) | BIT(2))


#define JZ_MMC_CMDAT_IO_ABORT BIT(11)
#define JZ_MMC_CMDAT_BUS_WIDTH_4BIT BIT(10)
#define JZ_MMC_CMDAT_DMA_EN BIT(8)
#define JZ_MMC_CMDAT_INIT BIT(7)
#define JZ_MMC_CMDAT_BUSY BIT(6)
#define JZ_MMC_CMDAT_STREAM BIT(5)
#define JZ_MMC_CMDAT_WRITE BIT(4)
#define JZ_MMC_CMDAT_DATA_EN BIT(3)
#define JZ_MMC_CMDAT_RESPONSE_FORMAT (BIT(2) | BIT(1) | BIT(0))
#define JZ_MMC_CMDAT_RSP_R1 1
#define JZ_MMC_CMDAT_RSP_R2 2
#define JZ_MMC_CMDAT_RSP_R3 3

#define JZ_MMC_IRQ_SDIO BIT(7)
#define JZ_MMC_IRQ_TXFIFO_WR_REQ BIT(6)
#define JZ_MMC_IRQ_RXFIFO_RD_REQ BIT(5)
#define JZ_MMC_IRQ_END_CMD_RES BIT(2)
#define JZ_MMC_IRQ_PRG_DONE BIT(1)
#define JZ_MMC_IRQ_DATA_TRAN_DONE BIT(0)


#define JZ_MMC_CLK_RATE 24000000

enum jz4740_mmc_state {
	JZ4740_MMC_STATE_READ_RESPONSE,
	JZ4740_MMC_STATE_TRANSFER_DATA,
	JZ4740_MMC_STATE_SEND_STOP,
	JZ4740_MMC_STATE_DONE,
};

struct jz4740_mmc_host {
	struct mmc_host *mmc;
	struct platform_device *pdev;
	struct jz4740_mmc_platform_data *pdata;
	struct clk *clk;

	int irq;
	int card_detect_irq;

	struct resource *mem;
	void __iomem *base;
	struct mmc_request *req;
	struct mmc_command *cmd;

	unsigned long waiting;

	uint32_t cmdat;

	uint16_t irq_mask;

	spinlock_t lock;

	struct timer_list timeout_timer;
	struct sg_mapping_iter miter;
	enum jz4740_mmc_state state;
};

static void jz4740_mmc_set_irq_enabled(struct jz4740_mmc_host *host,
	unsigned int irq, bool enabled)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (enabled)
		host->irq_mask &= ~irq;
	else
		host->irq_mask |= irq;
	spin_unlock_irqrestore(&host->lock, flags);

	writew(host->irq_mask, host->base + JZ_REG_MMC_IMASK);
}

static void jz4740_mmc_clock_enable(struct jz4740_mmc_host *host,
	bool start_transfer)
{
	uint16_t val = JZ_MMC_STRPCL_CLOCK_START;

	if (start_transfer)
		val |= JZ_MMC_STRPCL_START_OP;

	writew(val, host->base + JZ_REG_MMC_STRPCL);
}

static void jz4740_mmc_clock_disable(struct jz4740_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_CLOCK_STOP, host->base + JZ_REG_MMC_STRPCL);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_CLK_EN && --timeout);
}

static void jz4740_mmc_reset(struct jz4740_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_RESET, host->base + JZ_REG_MMC_STRPCL);
	udelay(10);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_IS_RESETTING && --timeout);
}

static void jz4740_mmc_request_done(struct jz4740_mmc_host *host)
{
	struct mmc_request *req;

	req = host->req;
	host->req = NULL;

	mmc_request_done(host->mmc, req);
}

static unsigned int jz4740_mmc_poll_irq(struct jz4740_mmc_host *host,
	unsigned int irq)
{
	unsigned int timeout = 0x800;
	uint16_t status;

	do {
		status = readw(host->base + JZ_REG_MMC_IREG);
	} while (!(status & irq) && --timeout);

	if (timeout == 0) {
		set_bit(0, &host->waiting);
		mod_timer(&host->timeout_timer, jiffies + 5*HZ);
		jz4740_mmc_set_irq_enabled(host, irq, true);
		return true;
	}

	return false;
}

static void jz4740_mmc_transfer_check_state(struct jz4740_mmc_host *host,
	struct mmc_data *data)
{
	int status;

	status = readl(host->base + JZ_REG_MMC_STATUS);
	if (status & JZ_MMC_STATUS_WRITE_ERROR_MASK) {
		if (status & (JZ_MMC_STATUS_TIMEOUT_WRITE)) {
			host->req->cmd->error = -ETIMEDOUT;
			data->error = -ETIMEDOUT;
		} else {
			host->req->cmd->error = -EIO;
			data->error = -EIO;
		}
	}
}

static bool jz4740_mmc_write_data(struct jz4740_mmc_host *host,
	struct mmc_data *data)
{
	struct sg_mapping_iter *miter = &host->miter;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_TXFIFO;
	uint32_t *buf;
	bool timeout;
	size_t i, j;

	while (sg_miter_next(miter)) {
		buf = miter->addr;
		i = miter->length / 4;
		j = i / 8;
		i = i & 0x7;
		while (j) {
			timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_TXFIFO_WR_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			writel(buf[0], fifo_addr);
			writel(buf[1], fifo_addr);
			writel(buf[2], fifo_addr);
			writel(buf[3], fifo_addr);
			writel(buf[4], fifo_addr);
			writel(buf[5], fifo_addr);
			writel(buf[6], fifo_addr);
			writel(buf[7], fifo_addr);
			buf += 8;
			--j;
		}
		if (unlikely(i)) {
			timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_TXFIFO_WR_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			while (i) {
				writel(*buf, fifo_addr);
				++buf;
				--i;
			}
		}
		data->bytes_xfered += miter->length;
	}
	sg_miter_stop(miter);

	return false;

poll_timeout:
	miter->consumed = (void *)buf - miter->addr;
	data->bytes_xfered += miter->consumed;
	sg_miter_stop(miter);

	return true;
}

static bool jz4740_mmc_read_data(struct jz4740_mmc_host *host,
				struct mmc_data *data)
{
	struct sg_mapping_iter *miter = &host->miter;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_RXFIFO;
	uint32_t *buf;
	uint32_t d;
	uint16_t status;
	size_t i, j;
	unsigned int timeout;

	while (sg_miter_next(miter)) {
		buf = miter->addr;
		i = miter->length;
		j = i / 32;
		i = i & 0x1f;
		while (j) {
			timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_RXFIFO_RD_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			buf[0] = readl(fifo_addr);
			buf[1] = readl(fifo_addr);
			buf[2] = readl(fifo_addr);
			buf[3] = readl(fifo_addr);
			buf[4] = readl(fifo_addr);
			buf[5] = readl(fifo_addr);
			buf[6] = readl(fifo_addr);
			buf[7] = readl(fifo_addr);

			buf += 8;
			--j;
		}

		if (unlikely(i)) {
			timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_RXFIFO_RD_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			while (i >= 4) {
				*buf++ = readl(fifo_addr);
				i -= 4;
			}
			if (unlikely(i > 0)) {
				d = readl(fifo_addr);
				memcpy(buf, &d, i);
			}
		}
		data->bytes_xfered += miter->length;

		/* This can go away once MIPS implements
		 * flush_kernel_dcache_page */
		flush_dcache_page(miter->page);
	}
	sg_miter_stop(miter);

	/* For whatever reason there is sometime one word more in the fifo then
	 * requested */
	timeout = 1000;
	status = readl(host->base + JZ_REG_MMC_STATUS);
	while (!(status & JZ_MMC_STATUS_DATA_FIFO_EMPTY) && --timeout) {
		d = readl(fifo_addr);
		status = readl(host->base + JZ_REG_MMC_STATUS);
	}

	return false;

poll_timeout:
	miter->consumed = (void *)buf - miter->addr;
	data->bytes_xfered += miter->consumed;
	sg_miter_stop(miter);

	return true;
}

static void jz4740_mmc_timeout(unsigned long data)
{
	struct jz4740_mmc_host *host = (struct jz4740_mmc_host *)data;

	if (!test_and_clear_bit(0, &host->waiting))
		return;

	jz4740_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, false);

	host->req->cmd->error = -ETIMEDOUT;
	jz4740_mmc_request_done(host);
}

static void jz4740_mmc_read_response(struct jz4740_mmc_host *host,
	struct mmc_command *cmd)
{
	int i;
	uint16_t tmp;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_RESP_FIFO;

	if (cmd->flags & MMC_RSP_136) {
		tmp = readw(fifo_addr);
		for (i = 0; i < 4; ++i) {
			cmd->resp[i] = tmp << 24;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp << 8;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp >> 8;
		}
	} else {
		cmd->resp[0] = readw(fifo_addr) << 24;
		cmd->resp[0] |= readw(fifo_addr) << 8;
		cmd->resp[0] |= readw(fifo_addr) & 0xff;
	}
}

static void jz4740_mmc_send_command(struct jz4740_mmc_host *host,
	struct mmc_command *cmd)
{
	uint32_t cmdat = host->cmdat;

	host->cmdat &= ~JZ_MMC_CMDAT_INIT;
	jz4740_mmc_clock_disable(host);

	host->cmd = cmd;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= JZ_MMC_CMDAT_BUSY;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1B:
	case MMC_RSP_R1:
		cmdat |= JZ_MMC_CMDAT_RSP_R1;
		break;
	case MMC_RSP_R2:
		cmdat |= JZ_MMC_CMDAT_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmdat |= JZ_MMC_CMDAT_RSP_R3;
		break;
	default:
		break;
	}

	if (cmd->data) {
		cmdat |= JZ_MMC_CMDAT_DATA_EN;
		if (cmd->data->flags & MMC_DATA_WRITE)
			cmdat |= JZ_MMC_CMDAT_WRITE;
		if (cmd->data->flags & MMC_DATA_STREAM)
			cmdat |= JZ_MMC_CMDAT_STREAM;

		writew(cmd->data->blksz, host->base + JZ_REG_MMC_BLKLEN);
		writew(cmd->data->blocks, host->base + JZ_REG_MMC_NOB);
	}

	writeb(cmd->opcode, host->base + JZ_REG_MMC_CMD);
	writel(cmd->arg, host->base + JZ_REG_MMC_ARG);
	writel(cmdat, host->base + JZ_REG_MMC_CMDAT);

	jz4740_mmc_clock_enable(host, 1);
}

static void jz_mmc_prepare_data_transfer(struct jz4740_mmc_host *host)
{
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_data *data = cmd->data;
	int direction;

	if (data->flags & MMC_DATA_READ)
		direction = SG_MITER_TO_SG;
	else
		direction = SG_MITER_FROM_SG;

	sg_miter_start(&host->miter, data->sg, data->sg_len, direction);
}


static irqreturn_t jz_mmc_irq_worker(int irq, void *devid)
{
	struct jz4740_mmc_host *host = (struct jz4740_mmc_host *)devid;
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_request *req = host->req;
	bool timeout = false;

	if (cmd->error)
		host->state = JZ4740_MMC_STATE_DONE;

	switch (host->state) {
	case JZ4740_MMC_STATE_READ_RESPONSE:
		if (cmd->flags & MMC_RSP_PRESENT)
			jz4740_mmc_read_response(host, cmd);

		if (!cmd->data)
			break;

		jz_mmc_prepare_data_transfer(host);

	case JZ4740_MMC_STATE_TRANSFER_DATA:
		if (cmd->data->flags & MMC_DATA_READ)
			timeout = jz4740_mmc_read_data(host, cmd->data);
		else
			timeout = jz4740_mmc_write_data(host, cmd->data);

		if (unlikely(timeout)) {
			host->state = JZ4740_MMC_STATE_TRANSFER_DATA;
			break;
		}

		jz4740_mmc_transfer_check_state(host, cmd->data);

		timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_DATA_TRAN_DONE);
		if (unlikely(timeout)) {
			host->state = JZ4740_MMC_STATE_SEND_STOP;
			break;
		}
		writew(JZ_MMC_IRQ_DATA_TRAN_DONE, host->base + JZ_REG_MMC_IREG);

	case JZ4740_MMC_STATE_SEND_STOP:
		if (!req->stop)
			break;

		jz4740_mmc_send_command(host, req->stop);

		timeout = jz4740_mmc_poll_irq(host, JZ_MMC_IRQ_PRG_DONE);
		if (timeout) {
			host->state = JZ4740_MMC_STATE_DONE;
			break;
		}
	case JZ4740_MMC_STATE_DONE:
		break;
	}

	if (!timeout)
		jz4740_mmc_request_done(host);

	return IRQ_HANDLED;
}

static irqreturn_t jz_mmc_irq(int irq, void *devid)
{
	struct jz4740_mmc_host *host = devid;
	struct mmc_command *cmd = host->cmd;
	uint16_t irq_reg, status, tmp;

	irq_reg = readw(host->base + JZ_REG_MMC_IREG);

	tmp = irq_reg;
	irq_reg &= ~host->irq_mask;

	tmp &= ~(JZ_MMC_IRQ_TXFIFO_WR_REQ | JZ_MMC_IRQ_RXFIFO_RD_REQ |
		JZ_MMC_IRQ_PRG_DONE | JZ_MMC_IRQ_DATA_TRAN_DONE);

	if (tmp != irq_reg)
		writew(tmp & ~irq_reg, host->base + JZ_REG_MMC_IREG);

	if (irq_reg & JZ_MMC_IRQ_SDIO) {
		writew(JZ_MMC_IRQ_SDIO, host->base + JZ_REG_MMC_IREG);
		mmc_signal_sdio_irq(host->mmc);
		irq_reg &= ~JZ_MMC_IRQ_SDIO;
	}

	if (host->req && cmd && irq_reg) {
		if (test_and_clear_bit(0, &host->waiting)) {
			del_timer(&host->timeout_timer);

			status = readl(host->base + JZ_REG_MMC_STATUS);

			if (status & JZ_MMC_STATUS_TIMEOUT_RES) {
					cmd->error = -ETIMEDOUT;
			} else if (status & JZ_MMC_STATUS_CRC_RES_ERR) {
					cmd->error = -EIO;
			} else if (status & (JZ_MMC_STATUS_CRC_READ_ERROR |
				    JZ_MMC_STATUS_CRC_WRITE_ERROR)) {
					if (cmd->data)
							cmd->data->error = -EIO;
					cmd->error = -EIO;
			} else if (status & (JZ_MMC_STATUS_CRC_READ_ERROR |
					JZ_MMC_STATUS_CRC_WRITE_ERROR)) {
					if (cmd->data)
							cmd->data->error = -EIO;
					cmd->error = -EIO;
			}

			jz4740_mmc_set_irq_enabled(host, irq_reg, false);
			writew(irq_reg, host->base + JZ_REG_MMC_IREG);

			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_HANDLED;
}

static int jz4740_mmc_set_clock_rate(struct jz4740_mmc_host *host, int rate)
{
	int div = 0;
	int real_rate;

	jz4740_mmc_clock_disable(host);
	clk_set_rate(host->clk, JZ_MMC_CLK_RATE);

	real_rate = clk_get_rate(host->clk);

	while (real_rate > rate && div < 7) {
		++div;
		real_rate >>= 1;
	}

	writew(div, host->base + JZ_REG_MMC_CLKRT);
	return real_rate;
}

static void jz4740_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct jz4740_mmc_host *host = mmc_priv(mmc);

	host->req = req;

	writew(0xffff, host->base + JZ_REG_MMC_IREG);

	writew(JZ_MMC_IRQ_END_CMD_RES, host->base + JZ_REG_MMC_IREG);
	jz4740_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, true);

	host->state = JZ4740_MMC_STATE_READ_RESPONSE;
	set_bit(0, &host->waiting);
	mod_timer(&host->timeout_timer, jiffies + 5*HZ);
	jz4740_mmc_send_command(host, req->cmd);
}

static void jz4740_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct jz4740_mmc_host *host = mmc_priv(mmc);
	if (ios->clock)
		jz4740_mmc_set_clock_rate(host, ios->clock);

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		jz4740_mmc_reset(host);
		if (gpio_is_valid(host->pdata->gpio_power))
			gpio_set_value(host->pdata->gpio_power,
					!host->pdata->power_active_low);
		host->cmdat |= JZ_MMC_CMDAT_INIT;
		clk_enable(host->clk);
		break;
	case MMC_POWER_ON:
		break;
	default:
		if (gpio_is_valid(host->pdata->gpio_power))
			gpio_set_value(host->pdata->gpio_power,
					host->pdata->power_active_low);
		clk_disable(host->clk);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH_4BIT;
		break;
	case MMC_BUS_WIDTH_4:
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_4BIT;
		break;
	default:
		break;
	}
}

static int jz4740_mmc_get_ro(struct mmc_host *mmc)
{
	struct jz4740_mmc_host *host = mmc_priv(mmc);
	if (!gpio_is_valid(host->pdata->gpio_read_only))
		return -ENOSYS;

	return gpio_get_value(host->pdata->gpio_read_only) ^
		host->pdata->read_only_active_low;
}

static int jz4740_mmc_get_cd(struct mmc_host *mmc)
{
	struct jz4740_mmc_host *host = mmc_priv(mmc);
	if (!gpio_is_valid(host->pdata->gpio_card_detect))
		return -ENOSYS;

	return gpio_get_value(host->pdata->gpio_card_detect) ^
			host->pdata->card_detect_active_low;
}

static irqreturn_t jz4740_mmc_card_detect_irq(int irq, void *devid)
{
	struct jz4740_mmc_host *host = devid;

	mmc_detect_change(host->mmc, HZ / 2);

	return IRQ_HANDLED;
}

static void jz4740_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct jz4740_mmc_host *host = mmc_priv(mmc);
	jz4740_mmc_set_irq_enabled(host, JZ_MMC_IRQ_SDIO, enable);
}

static const struct mmc_host_ops jz4740_mmc_ops = {
	.request	= jz4740_mmc_request,
	.set_ios	= jz4740_mmc_set_ios,
	.get_ro		= jz4740_mmc_get_ro,
	.get_cd		= jz4740_mmc_get_cd,
	.enable_sdio_irq = jz4740_mmc_enable_sdio_irq,
};

static const struct jz_gpio_bulk_request jz4740_mmc_pins[] = {
	JZ_GPIO_BULK_PIN(MSC_CMD),
	JZ_GPIO_BULK_PIN(MSC_CLK),
	JZ_GPIO_BULK_PIN(MSC_DATA0),
	JZ_GPIO_BULK_PIN(MSC_DATA1),
	JZ_GPIO_BULK_PIN(MSC_DATA2),
	JZ_GPIO_BULK_PIN(MSC_DATA3),
};

static int jz4740_mmc_request_gpio(struct device *dev, int gpio,
	const char *name, bool output, int value)
{
	int ret;

	if (!gpio_is_valid(gpio))
		return 0;

	ret = gpio_request(gpio, name);
	if (ret) {
		dev_err(dev, "Failed to request %s gpio: %d\n", name, ret);
		return ret;
	}

	if (output)
		gpio_direction_output(gpio, value);
	else
		gpio_direction_input(gpio);

	return 0;
}

static int jz4740_mmc_request_gpios(struct platform_device *pdev)
{
	int ret;
	struct jz4740_mmc_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return 0;

	ret = jz4740_mmc_request_gpio(&pdev->dev, pdata->gpio_card_detect,
			"MMC detect change", false, 0);
	if (ret)
		goto err;

	ret = jz4740_mmc_request_gpio(&pdev->dev, pdata->gpio_read_only,
			"MMC read only", false, 0);
	if (ret)
		goto err_free_gpio_card_detect;

	ret = jz4740_mmc_request_gpio(&pdev->dev, pdata->gpio_power,
			"MMC read only", true, pdata->power_active_low);
	if (ret)
		goto err_free_gpio_read_only;

	return 0;

err_free_gpio_read_only:
	if (gpio_is_valid(pdata->gpio_read_only))
		gpio_free(pdata->gpio_read_only);
err_free_gpio_card_detect:
	if (gpio_is_valid(pdata->gpio_card_detect))
		gpio_free(pdata->gpio_card_detect);
err:
	return ret;
}

static int jz4740_mmc_request_cd_irq(struct platform_device *pdev,
	struct jz4740_mmc_host *host)
{
	struct jz4740_mmc_platform_data *pdata = pdev->dev.platform_data;

	if (!gpio_is_valid(pdata->gpio_card_detect))
		return 0;

	host->card_detect_irq = gpio_to_irq(pdata->gpio_card_detect);
	if (host->card_detect_irq < 0) {
		dev_warn(&pdev->dev, "Failed to get card detect irq\n");
		return 0;
	}

	return request_irq(host->card_detect_irq, jz4740_mmc_card_detect_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"MMC card detect", host);
}

static void jz4740_mmc_free_gpios(struct platform_device *pdev)
{
	struct jz4740_mmc_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return;

	if (gpio_is_valid(pdata->gpio_power))
		gpio_free(pdata->gpio_power);
	if (gpio_is_valid(pdata->gpio_read_only))
		gpio_free(pdata->gpio_read_only);
	if (gpio_is_valid(pdata->gpio_card_detect))
		gpio_free(pdata->gpio_card_detect);
}

static inline size_t jz4740_mmc_num_pins(struct jz4740_mmc_host *host)
{
	size_t num_pins = ARRAY_SIZE(jz4740_mmc_pins);
	if (host->pdata && host->pdata->data_1bit)
		num_pins -= 3;

	return num_pins;
}

static int jz4740_mmc_probe(struct platform_device* pdev)
{
	int ret;
	struct mmc_host *mmc;
	struct jz4740_mmc_host *host;
	struct jz4740_mmc_platform_data *pdata;

	pdata = pdev->dev.platform_data;

	mmc = mmc_alloc_host(sizeof(struct jz4740_mmc_host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Failed to alloc mmc host structure\n");
		return -ENOMEM;
	}

	host = mmc_priv(mmc);
	host->pdata = pdata;

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = host->irq;
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n", ret);
		goto err_free_host;
	}

	host->clk = clk_get(&pdev->dev, "mmc");
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		dev_err(&pdev->dev, "Failed to get mmc clock\n");
		goto err_free_host;
	}

	host->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!host->mem) {
		ret = -ENOENT;
		dev_err(&pdev->dev, "Failed to get base platform memory\n");
		goto err_clk_put;
	}

	host->mem = request_mem_region(host->mem->start,
					resource_size(host->mem), pdev->name);
	if (!host->mem) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to request base memory region\n");
		goto err_clk_put;
	}

	host->base = ioremap_nocache(host->mem->start, resource_size(host->mem));
	if (!host->base) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to ioremap base memory\n");
		goto err_release_mem_region;
	}

	ret = jz_gpio_bulk_request(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
	if (ret) {
		dev_err(&pdev->dev, "Failed to request mmc pins: %d\n", ret);
		goto err_iounmap;
	}

	ret = jz4740_mmc_request_gpios(pdev);
	if (ret)
		goto err_gpio_bulk_free;

	mmc->ops = &jz4740_mmc_ops;
	mmc->f_min = JZ_MMC_CLK_RATE / 128;
	mmc->f_max = JZ_MMC_CLK_RATE;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = (pdata && pdata->data_1bit) ? 0 : MMC_CAP_4_BIT_DATA;
	mmc->caps |= MMC_CAP_SDIO_IRQ;

	mmc->max_blk_size = (1 << 10) - 1;
	mmc->max_blk_count = (1 << 15) - 1;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;

	mmc->max_segs = 128;
	mmc->max_seg_size = mmc->max_req_size;

	host->mmc = mmc;
	host->pdev = pdev;
	spin_lock_init(&host->lock);
	host->irq_mask = 0xffff;

	ret = jz4740_mmc_request_cd_irq(pdev, host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request card detect irq\n");
		goto err_free_gpios;
	}

	ret = request_threaded_irq(host->irq, jz_mmc_irq, jz_mmc_irq_worker, 0,
			dev_name(&pdev->dev), host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		goto err_free_card_detect_irq;
	}

	jz4740_mmc_reset(host);
	jz4740_mmc_clock_disable(host);
	setup_timer(&host->timeout_timer, jz4740_mmc_timeout,
			(unsigned long)host);
	/* It is not important when it times out, it just needs to timeout. */
	set_timer_slack(&host->timeout_timer, HZ);

	platform_set_drvdata(pdev, host);
	ret = mmc_add_host(mmc);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add mmc host: %d\n", ret);
		goto err_free_irq;
	}
	dev_info(&pdev->dev, "JZ SD/MMC card driver registered\n");

	return 0;

err_free_irq:
	free_irq(host->irq, host);
err_free_card_detect_irq:
	if (host->card_detect_irq >= 0)
		free_irq(host->card_detect_irq, host);
err_free_gpios:
	jz4740_mmc_free_gpios(pdev);
err_gpio_bulk_free:
	jz_gpio_bulk_free(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
err_iounmap:
	iounmap(host->base);
err_release_mem_region:
	release_mem_region(host->mem->start, resource_size(host->mem));
err_clk_put:
	clk_put(host->clk);
err_free_host:
	mmc_free_host(mmc);

	return ret;
}

static int jz4740_mmc_remove(struct platform_device *pdev)
{
	struct jz4740_mmc_host *host = platform_get_drvdata(pdev);

	del_timer_sync(&host->timeout_timer);
	jz4740_mmc_set_irq_enabled(host, 0xff, false);
	jz4740_mmc_reset(host);

	mmc_remove_host(host->mmc);

	free_irq(host->irq, host);
	if (host->card_detect_irq >= 0)
		free_irq(host->card_detect_irq, host);

	jz4740_mmc_free_gpios(pdev);
	jz_gpio_bulk_free(jz4740_mmc_pins, jz4740_mmc_num_pins(host));

	iounmap(host->base);
	release_mem_region(host->mem->start, resource_size(host->mem));

	clk_put(host->clk);

	mmc_free_host(host->mmc);

	return 0;
}

#ifdef CONFIG_PM

static int jz4740_mmc_suspend(struct device *dev)
{
	struct jz4740_mmc_host *host = dev_get_drvdata(dev);

	mmc_suspend_host(host->mmc);

	jz_gpio_bulk_suspend(jz4740_mmc_pins, jz4740_mmc_num_pins(host));

	return 0;
}

static int jz4740_mmc_resume(struct device *dev)
{
	struct jz4740_mmc_host *host = dev_get_drvdata(dev);

	jz_gpio_bulk_resume(jz4740_mmc_pins, jz4740_mmc_num_pins(host));

	mmc_resume_host(host->mmc);

	return 0;
}

const struct dev_pm_ops jz4740_mmc_pm_ops = {
	.suspend	= jz4740_mmc_suspend,
	.resume		= jz4740_mmc_resume,
	.poweroff	= jz4740_mmc_suspend,
	.restore	= jz4740_mmc_resume,
};

#define JZ4740_MMC_PM_OPS (&jz4740_mmc_pm_ops)
#else
#define JZ4740_MMC_PM_OPS NULL
#endif

static struct platform_driver jz4740_mmc_driver = {
	.probe = jz4740_mmc_probe,
	.remove = jz4740_mmc_remove,
	.driver = {
		.name = "jz4740-mmc",
		.owner = THIS_MODULE,
		.pm = JZ4740_MMC_PM_OPS,
	},
};

module_platform_driver(jz4740_mmc_driver);

MODULE_DESCRIPTION("JZ4740 SD/MMC controller driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
