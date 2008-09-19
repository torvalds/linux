/*
 * Atmel MultiMedia Card Interface driver
 *
 * Copyright (C) 2004-2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/stat.h>

#include <linux/mmc/host.h>

#include <asm/atmel-mci.h>
#include <asm/io.h>
#include <asm/unaligned.h>

#include <mach/board.h>

#include "atmel-mci-regs.h"

#define ATMCI_DATA_ERROR_FLAGS	(MCI_DCRCE | MCI_DTOE | MCI_OVRE | MCI_UNRE)

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_DATA_ERROR,
	EVENT_DATA_COMPLETE,
	EVENT_STOP_SENT,
	EVENT_STOP_COMPLETE,
	EVENT_XFER_COMPLETE,
};

struct atmel_mci {
	struct mmc_host		*mmc;
	void __iomem		*regs;

	struct scatterlist	*sg;
	unsigned int		pio_offset;

	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	u32			cmd_status;
	u32			data_status;
	u32			stop_status;
	u32			stop_cmdr;

	u32			mode_reg;
	u32			sdc_reg;

	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;

	int			present;
	int			detect_pin;
	int			wp_pin;

	/* For detect pin debouncing */
	struct timer_list	detect_timer;

	unsigned long		bus_hz;
	unsigned long		mapbase;
	struct clk		*mck;
	struct platform_device	*pdev;
};

#define atmci_is_completed(host, event)				\
	test_bit(event, &host->completed_events)
#define atmci_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define atmci_test_and_set_completed(host, event)		\
	test_and_set_bit(event, &host->completed_events)
#define atmci_set_completed(host, event)			\
	set_bit(event, &host->completed_events)
#define atmci_set_pending(host, event)				\
	set_bit(event, &host->pending_events)
#define atmci_clear_pending(host, event)			\
	clear_bit(event, &host->pending_events)

/*
 * The debugfs stuff below is mostly optimized away when
 * CONFIG_DEBUG_FS is not set.
 */
static int atmci_req_show(struct seq_file *s, void *v)
{
	struct atmel_mci	*host = s->private;
	struct mmc_request	*mrq = host->mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

	/* Make sure we get a consistent snapshot */
	spin_lock_irq(&host->mmc->lock);

	if (mrq) {
		cmd = mrq->cmd;
		data = mrq->data;
		stop = mrq->stop;

		if (cmd)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				cmd->opcode, cmd->arg, cmd->flags,
				cmd->resp[0], cmd->resp[1], cmd->resp[2],
				cmd->resp[2], cmd->error);
		if (data)
			seq_printf(s, "DATA %u / %u * %u flg %x err %d\n",
				data->bytes_xfered, data->blocks,
				data->blksz, data->flags, data->error);
		if (stop)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				stop->opcode, stop->arg, stop->flags,
				stop->resp[0], stop->resp[1], stop->resp[2],
				stop->resp[2], stop->error);
	}

	spin_unlock_irq(&host->mmc->lock);

	return 0;
}

static int atmci_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, atmci_req_show, inode->i_private);
}

static const struct file_operations atmci_req_fops = {
	.owner		= THIS_MODULE,
	.open		= atmci_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void atmci_show_status_reg(struct seq_file *s,
		const char *regname, u32 value)
{
	static const char	*sr_bit[] = {
		[0]	= "CMDRDY",
		[1]	= "RXRDY",
		[2]	= "TXRDY",
		[3]	= "BLKE",
		[4]	= "DTIP",
		[5]	= "NOTBUSY",
		[8]	= "SDIOIRQA",
		[9]	= "SDIOIRQB",
		[16]	= "RINDE",
		[17]	= "RDIRE",
		[18]	= "RCRCE",
		[19]	= "RENDE",
		[20]	= "RTOE",
		[21]	= "DCRCE",
		[22]	= "DTOE",
		[30]	= "OVRE",
		[31]	= "UNRE",
	};
	unsigned int		i;

	seq_printf(s, "%s:\t0x%08x", regname, value);
	for (i = 0; i < ARRAY_SIZE(sr_bit); i++) {
		if (value & (1 << i)) {
			if (sr_bit[i])
				seq_printf(s, " %s", sr_bit[i]);
			else
				seq_puts(s, " UNKNOWN");
		}
	}
	seq_putc(s, '\n');
}

static int atmci_regs_show(struct seq_file *s, void *v)
{
	struct atmel_mci	*host = s->private;
	u32			*buf;

	buf = kmalloc(MCI_REGS_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Grab a more or less consistent snapshot */
	spin_lock_irq(&host->mmc->lock);
	clk_enable(host->mck);
	memcpy_fromio(buf, host->regs, MCI_REGS_SIZE);
	clk_disable(host->mck);
	spin_unlock_irq(&host->mmc->lock);

	seq_printf(s, "MR:\t0x%08x%s%s CLKDIV=%u\n",
			buf[MCI_MR / 4],
			buf[MCI_MR / 4] & MCI_MR_RDPROOF ? " RDPROOF" : "",
			buf[MCI_MR / 4] & MCI_MR_WRPROOF ? " WRPROOF" : "",
			buf[MCI_MR / 4] & 0xff);
	seq_printf(s, "DTOR:\t0x%08x\n", buf[MCI_DTOR / 4]);
	seq_printf(s, "SDCR:\t0x%08x\n", buf[MCI_SDCR / 4]);
	seq_printf(s, "ARGR:\t0x%08x\n", buf[MCI_ARGR / 4]);
	seq_printf(s, "BLKR:\t0x%08x BCNT=%u BLKLEN=%u\n",
			buf[MCI_BLKR / 4],
			buf[MCI_BLKR / 4] & 0xffff,
			(buf[MCI_BLKR / 4] >> 16) & 0xffff);

	/* Don't read RSPR and RDR; it will consume the data there */

	atmci_show_status_reg(s, "SR", buf[MCI_SR / 4]);
	atmci_show_status_reg(s, "IMR", buf[MCI_IMR / 4]);

	return 0;
}

static int atmci_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, atmci_regs_show, inode->i_private);
}

static const struct file_operations atmci_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= atmci_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void atmci_init_debugfs(struct atmel_mci *host)
{
	struct mmc_host	*mmc;
	struct dentry	*root;
	struct dentry	*node;
	struct resource	*res;

	mmc = host->mmc;
	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
			&atmci_regs_fops);
	if (IS_ERR(node))
		return;
	if (!node)
		goto err;

	res = platform_get_resource(host->pdev, IORESOURCE_MEM, 0);
	node->d_inode->i_size = res->end - res->start + 1;

	node = debugfs_create_file("req", S_IRUSR, root, host, &atmci_req_fops);
	if (!node)
		goto err;

	node = debugfs_create_x32("pending_events", S_IRUSR, root,
				     (u32 *)&host->pending_events);
	if (!node)
		goto err;

	node = debugfs_create_x32("completed_events", S_IRUSR, root,
				     (u32 *)&host->completed_events);
	if (!node)
		goto err;

	return;

err:
	dev_err(&host->pdev->dev,
		"failed to initialize debugfs for controller\n");
}

static void atmci_enable(struct atmel_mci *host)
{
	clk_enable(host->mck);
	mci_writel(host, CR, MCI_CR_MCIEN);
	mci_writel(host, MR, host->mode_reg);
	mci_writel(host, SDCR, host->sdc_reg);
}

static void atmci_disable(struct atmel_mci *host)
{
	mci_writel(host, CR, MCI_CR_SWRST);

	/* Stall until write is complete, then disable the bus clock */
	mci_readl(host, SR);
	clk_disable(host->mck);
}

static inline unsigned int ns_to_clocks(struct atmel_mci *host,
					unsigned int ns)
{
	return (ns * (host->bus_hz / 1000000) + 999) / 1000;
}

static void atmci_set_timeout(struct atmel_mci *host,
			      struct mmc_data *data)
{
	static unsigned	dtomul_to_shift[] = {
		0, 4, 7, 8, 10, 12, 16, 20
	};
	unsigned	timeout;
	unsigned	dtocyc;
	unsigned	dtomul;

	timeout = ns_to_clocks(host, data->timeout_ns) + data->timeout_clks;

	for (dtomul = 0; dtomul < 8; dtomul++) {
		unsigned shift = dtomul_to_shift[dtomul];
		dtocyc = (timeout + (1 << shift) - 1) >> shift;
		if (dtocyc < 15)
			break;
	}

	if (dtomul >= 8) {
		dtomul = 7;
		dtocyc = 15;
	}

	dev_vdbg(&host->mmc->class_dev, "setting timeout to %u cycles\n",
			dtocyc << dtomul_to_shift[dtomul]);
	mci_writel(host, DTOR, (MCI_DTOMUL(dtomul) | MCI_DTOCYC(dtocyc)));
}

/*
 * Return mask with command flags to be enabled for this command.
 */
static u32 atmci_prepare_command(struct mmc_host *mmc,
				 struct mmc_command *cmd)
{
	struct mmc_data	*data;
	u32		cmdr;

	cmd->error = -EINPROGRESS;

	cmdr = MCI_CMDR_CMDNB(cmd->opcode);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			cmdr |= MCI_CMDR_RSPTYP_136BIT;
		else
			cmdr |= MCI_CMDR_RSPTYP_48BIT;
	}

	/*
	 * This should really be MAXLAT_5 for CMD2 and ACMD41, but
	 * it's too difficult to determine whether this is an ACMD or
	 * not. Better make it 64.
	 */
	cmdr |= MCI_CMDR_MAXLAT_64CYC;

	if (mmc->ios.bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdr |= MCI_CMDR_OPDCMD;

	data = cmd->data;
	if (data) {
		cmdr |= MCI_CMDR_START_XFER;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= MCI_CMDR_STREAM;
		else if (data->blocks > 1)
			cmdr |= MCI_CMDR_MULTI_BLOCK;
		else
			cmdr |= MCI_CMDR_BLOCK;

		if (data->flags & MMC_DATA_READ)
			cmdr |= MCI_CMDR_TRDIR_READ;
	}

	return cmdr;
}

static void atmci_start_command(struct atmel_mci *host,
				struct mmc_command *cmd,
				u32 cmd_flags)
{
	/* Must read host->cmd after testing event flags */
	smp_rmb();
	WARN_ON(host->cmd);
	host->cmd = cmd;

	dev_vdbg(&host->mmc->class_dev,
			"start command: ARGR=0x%08x CMDR=0x%08x\n",
			cmd->arg, cmd_flags);

	mci_writel(host, ARGR, cmd->arg);
	mci_writel(host, CMDR, cmd_flags);
}

static void send_stop_cmd(struct mmc_host *mmc, struct mmc_data *data)
{
	struct atmel_mci *host = mmc_priv(mmc);

	atmci_start_command(host, data->stop, host->stop_cmdr);
	mci_writel(host, IER, MCI_CMDRDY);
}

static void atmci_request_end(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct atmel_mci *host = mmc_priv(mmc);

	WARN_ON(host->cmd || host->data);
	host->mrq = NULL;

	atmci_disable(host);

	mmc_request_done(mmc, mrq);
}

/*
 * Returns a mask of interrupt flags to be enabled after the whole
 * request has been prepared.
 */
static u32 atmci_submit_data(struct mmc_host *mmc, struct mmc_data *data)
{
	struct atmel_mci	*host = mmc_priv(mmc);
	u32			iflags;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	mci_writel(host, BLKR, MCI_BCNT(data->blocks)
			| MCI_BLKLEN(data->blksz));
	dev_vdbg(&mmc->class_dev, "BLKR=0x%08x\n",
			MCI_BCNT(data->blocks) | MCI_BLKLEN(data->blksz));

	iflags = ATMCI_DATA_ERROR_FLAGS;
	host->sg = data->sg;
	host->pio_offset = 0;
	if (data->flags & MMC_DATA_READ)
		iflags |= MCI_RXRDY;
	else
		iflags |= MCI_TXRDY;

	return iflags;
}

static void atmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct atmel_mci	*host = mmc_priv(mmc);
	struct mmc_data		*data;
	struct mmc_command	*cmd;
	u32			iflags;
	u32			cmdflags = 0;

	iflags = mci_readl(host, IMR);
	if (iflags)
		dev_warn(&mmc->class_dev, "WARNING: IMR=0x%08x\n",
				mci_readl(host, IMR));

	WARN_ON(host->mrq != NULL);

	/*
	 * We may "know" the card is gone even though there's still an
	 * electrical connection. If so, we really need to communicate
	 * this to the MMC core since there won't be any more
	 * interrupts as the card is completely removed. Otherwise,
	 * the MMC core might believe the card is still there even
	 * though the card was just removed very slowly.
	 */
	if (!host->present) {
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	host->mrq = mrq;
	host->pending_events = 0;
	host->completed_events = 0;

	atmci_enable(host);

	/* We don't support multiple blocks of weird lengths. */
	data = mrq->data;
	if (data) {
		if (data->blocks > 1 && data->blksz & 3)
			goto fail;
		atmci_set_timeout(host, data);
	}

	iflags = MCI_CMDRDY;
	cmd = mrq->cmd;
	cmdflags = atmci_prepare_command(mmc, cmd);
	atmci_start_command(host, cmd, cmdflags);

	if (data)
		iflags |= atmci_submit_data(mmc, data);

	if (mrq->stop) {
		host->stop_cmdr = atmci_prepare_command(mmc, mrq->stop);
		host->stop_cmdr |= MCI_CMDR_STOP_XFER;
		if (!(data->flags & MMC_DATA_WRITE))
			host->stop_cmdr |= MCI_CMDR_TRDIR_READ;
		if (data->flags & MMC_DATA_STREAM)
			host->stop_cmdr |= MCI_CMDR_STREAM;
		else
			host->stop_cmdr |= MCI_CMDR_MULTI_BLOCK;
	}

	/*
	 * We could have enabled interrupts earlier, but I suspect
	 * that would open up a nice can of interesting race
	 * conditions (e.g. command and data complete, but stop not
	 * prepared yet.)
	 */
	mci_writel(host, IER, iflags);

	return;

fail:
	atmci_disable(host);
	host->mrq = NULL;
	mrq->cmd->error = -EINVAL;
	mmc_request_done(mmc, mrq);
}

static void atmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct atmel_mci	*host = mmc_priv(mmc);

	if (ios->clock) {
		u32 clkdiv;

		/* Set clock rate */
		clkdiv = DIV_ROUND_UP(host->bus_hz, 2 * ios->clock) - 1;
		if (clkdiv > 255) {
			dev_warn(&mmc->class_dev,
				"clock %u too slow; using %lu\n",
				ios->clock, host->bus_hz / (2 * 256));
			clkdiv = 255;
		}

		host->mode_reg = MCI_MR_CLKDIV(clkdiv) | MCI_MR_WRPROOF
					| MCI_MR_RDPROOF;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->sdc_reg = 0;
		break;
	case MMC_BUS_WIDTH_4:
		host->sdc_reg = MCI_SDCBUS_4BIT;
		break;
	}

	switch (ios->power_mode) {
	case MMC_POWER_ON:
		/* Send init sequence (74 clock cycles) */
		atmci_enable(host);
		mci_writel(host, CMDR, MCI_CMDR_SPCMD_INIT);
		while (!(mci_readl(host, SR) & MCI_CMDRDY))
			cpu_relax();
		atmci_disable(host);
		break;
	default:
		/*
		 * TODO: None of the currently available AVR32-based
		 * boards allow MMC power to be turned off. Implement
		 * power control when this can be tested properly.
		 */
		break;
	}
}

static int atmci_get_ro(struct mmc_host *mmc)
{
	int			read_only = 0;
	struct atmel_mci	*host = mmc_priv(mmc);

	if (gpio_is_valid(host->wp_pin)) {
		read_only = gpio_get_value(host->wp_pin);
		dev_dbg(&mmc->class_dev, "card is %s\n",
				read_only ? "read-only" : "read-write");
	} else {
		dev_dbg(&mmc->class_dev,
			"no pin for checking read-only switch."
			" Assuming write-enable.\n");
	}

	return read_only;
}

static struct mmc_host_ops atmci_ops = {
	.request	= atmci_request,
	.set_ios	= atmci_set_ios,
	.get_ro		= atmci_get_ro,
};

static void atmci_command_complete(struct atmel_mci *host,
			struct mmc_command *cmd, u32 status)
{
	/* Read the response from the card (up to 16 bytes) */
	cmd->resp[0] = mci_readl(host, RSPR);
	cmd->resp[1] = mci_readl(host, RSPR);
	cmd->resp[2] = mci_readl(host, RSPR);
	cmd->resp[3] = mci_readl(host, RSPR);

	if (status & MCI_RTOE)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & MCI_RCRCE))
		cmd->error = -EILSEQ;
	else if (status & (MCI_RINDE | MCI_RDIRE | MCI_RENDE))
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		dev_dbg(&host->mmc->class_dev,
			"command error: status=0x%08x\n", status);

		if (cmd->data) {
			host->data = NULL;
			mci_writel(host, IDR, MCI_NOTBUSY
					| MCI_TXRDY | MCI_RXRDY
					| ATMCI_DATA_ERROR_FLAGS);
		}
	}
}

static void atmci_detect_change(unsigned long data)
{
	struct atmel_mci *host = (struct atmel_mci *)data;
	struct mmc_request *mrq = host->mrq;
	int present;

	/*
	 * atmci_remove() sets detect_pin to -1 before freeing the
	 * interrupt. We must not re-enable the interrupt if it has
	 * been freed.
	 */
	smp_rmb();
	if (!gpio_is_valid(host->detect_pin))
		return;

	enable_irq(gpio_to_irq(host->detect_pin));
	present = !gpio_get_value(host->detect_pin);

	dev_vdbg(&host->pdev->dev, "detect change: %d (was %d)\n",
			present, host->present);

	if (present != host->present) {
		dev_dbg(&host->mmc->class_dev, "card %s\n",
			present ? "inserted" : "removed");
		host->present = present;

		/* Reset controller if card is gone */
		if (!present) {
			mci_writel(host, CR, MCI_CR_SWRST);
			mci_writel(host, IDR, ~0UL);
			mci_writel(host, CR, MCI_CR_MCIEN);
		}

		/* Clean up queue if present */
		if (mrq) {
			/*
			 * Reset controller to terminate any ongoing
			 * commands or data transfers.
			 */
			mci_writel(host, CR, MCI_CR_SWRST);

			if (!atmci_is_completed(host, EVENT_CMD_COMPLETE))
				mrq->cmd->error = -ENOMEDIUM;

			if (mrq->data && !atmci_is_completed(host,
						EVENT_DATA_COMPLETE)) {
				host->data = NULL;
				mrq->data->error = -ENOMEDIUM;
			}
			if (mrq->stop && !atmci_is_completed(host,
						EVENT_STOP_COMPLETE))
				mrq->stop->error = -ENOMEDIUM;

			host->cmd = NULL;
			atmci_request_end(host->mmc, mrq);
		}

		mmc_detect_change(host->mmc, 0);
	}
}

static void atmci_tasklet_func(unsigned long priv)
{
	struct mmc_host		*mmc = (struct mmc_host *)priv;
	struct atmel_mci	*host = mmc_priv(mmc);
	struct mmc_request	*mrq = host->mrq;
	struct mmc_data		*data = host->data;

	dev_vdbg(&mmc->class_dev,
		"tasklet: pending/completed/mask %lx/%lx/%x\n",
		host->pending_events, host->completed_events,
		mci_readl(host, IMR));

	if (atmci_test_and_clear_pending(host, EVENT_CMD_COMPLETE)) {
		/*
		 * host->cmd must be set to NULL before the interrupt
		 * handler sees EVENT_CMD_COMPLETE
		 */
		host->cmd = NULL;
		smp_wmb();
		atmci_set_completed(host, EVENT_CMD_COMPLETE);
		atmci_command_complete(host, mrq->cmd, host->cmd_status);

		if (!mrq->cmd->error && mrq->stop
				&& atmci_is_completed(host, EVENT_XFER_COMPLETE)
				&& !atmci_test_and_set_completed(host,
					EVENT_STOP_SENT))
			send_stop_cmd(host->mmc, mrq->data);
	}
	if (atmci_test_and_clear_pending(host, EVENT_STOP_COMPLETE)) {
		/*
		 * host->cmd must be set to NULL before the interrupt
		 * handler sees EVENT_STOP_COMPLETE
		 */
		host->cmd = NULL;
		smp_wmb();
		atmci_set_completed(host, EVENT_STOP_COMPLETE);
		atmci_command_complete(host, mrq->stop, host->stop_status);
	}
	if (atmci_test_and_clear_pending(host, EVENT_DATA_ERROR)) {
		u32 status = host->data_status;

		dev_vdbg(&mmc->class_dev, "data error: status=%08x\n", status);

		atmci_set_completed(host, EVENT_DATA_ERROR);
		atmci_set_completed(host, EVENT_DATA_COMPLETE);

		if (status & MCI_DTOE) {
			dev_dbg(&mmc->class_dev,
					"data timeout error\n");
			data->error = -ETIMEDOUT;
		} else if (status & MCI_DCRCE) {
			dev_dbg(&mmc->class_dev, "data CRC error\n");
			data->error = -EILSEQ;
		} else {
			dev_dbg(&mmc->class_dev,
					"data FIFO error (status=%08x)\n",
					status);
			data->error = -EIO;
		}

		if (host->present && data->stop
				&& atmci_is_completed(host, EVENT_CMD_COMPLETE)
				&& !atmci_test_and_set_completed(
					host, EVENT_STOP_SENT))
			send_stop_cmd(host->mmc, data);

		host->data = NULL;
	}
	if (atmci_test_and_clear_pending(host, EVENT_DATA_COMPLETE)) {
		atmci_set_completed(host, EVENT_DATA_COMPLETE);

		if (!atmci_is_completed(host, EVENT_DATA_ERROR)) {
			data->bytes_xfered = data->blocks * data->blksz;
			data->error = 0;
		}

		host->data = NULL;
	}

	if (host->mrq && !host->cmd && !host->data)
		atmci_request_end(mmc, host->mrq);
}

static void atmci_read_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		value = mci_readl(host, RDR);
		if (likely(offset + 4 <= sg->length)) {
			put_unaligned(value, (u32 *)(buf + offset));

			offset += 4;
			nbytes += 4;

			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;
			memcpy(buf + offset, &value, remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = 4 - remaining;
			buf = sg_virt(sg);
			memcpy(buf, (u8 *)&value + remaining, offset);
			nbytes += offset;
		}

		status = mci_readl(host, SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, (MCI_NOTBUSY | MCI_RXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			break;
		}
	} while (status & MCI_RXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	mci_writel(host, IDR, MCI_RXRDY);
	mci_writel(host, IER, MCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	atmci_set_completed(host, EVENT_XFER_COMPLETE);
	if (data->stop && atmci_is_completed(host, EVENT_CMD_COMPLETE)
			&& !atmci_test_and_set_completed(host, EVENT_STOP_SENT))
		send_stop_cmd(host->mmc, data);
}

static void atmci_write_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		if (likely(offset + 4 <= sg->length)) {
			value = get_unaligned((u32 *)(buf + offset));
			mci_writel(host, TDR, value);

			offset += 4;
			nbytes += 4;
			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;

			value = 0;
			memcpy(&value, buf + offset, remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg) {
				mci_writel(host, TDR, value);
				goto done;
			}

			offset = 4 - remaining;
			buf = sg_virt(sg);
			memcpy((u8 *)&value + remaining, buf, offset);
			mci_writel(host, TDR, value);
			nbytes += offset;
		}

		status = mci_readl(host, SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, (MCI_NOTBUSY | MCI_TXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			break;
		}
	} while (status & MCI_TXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	mci_writel(host, IDR, MCI_TXRDY);
	mci_writel(host, IER, MCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	atmci_set_completed(host, EVENT_XFER_COMPLETE);
	if (data->stop && atmci_is_completed(host, EVENT_CMD_COMPLETE)
			&& !atmci_test_and_set_completed(host, EVENT_STOP_SENT))
		send_stop_cmd(host->mmc, data);
}

static void atmci_cmd_interrupt(struct mmc_host *mmc, u32 status)
{
	struct atmel_mci	*host = mmc_priv(mmc);

	mci_writel(host, IDR, MCI_CMDRDY);

	if (atmci_is_completed(host, EVENT_STOP_SENT)) {
		host->stop_status = status;
		atmci_set_pending(host, EVENT_STOP_COMPLETE);
	} else {
		host->cmd_status = status;
		atmci_set_pending(host, EVENT_CMD_COMPLETE);
	}

	tasklet_schedule(&host->tasklet);
}

static irqreturn_t atmci_interrupt(int irq, void *dev_id)
{
	struct mmc_host		*mmc = dev_id;
	struct atmel_mci	*host = mmc_priv(mmc);
	u32			status, mask, pending;
	unsigned int		pass_count = 0;

	spin_lock(&mmc->lock);

	do {
		status = mci_readl(host, SR);
		mask = mci_readl(host, IMR);
		pending = status & mask;
		if (!pending)
			break;

		if (pending & ATMCI_DATA_ERROR_FLAGS) {
			mci_writel(host, IDR, ATMCI_DATA_ERROR_FLAGS
					| MCI_RXRDY | MCI_TXRDY);
			pending &= mci_readl(host, IMR);
			host->data_status = status;
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
		}
		if (pending & MCI_NOTBUSY) {
			mci_writel(host, IDR, (MCI_NOTBUSY
					       | ATMCI_DATA_ERROR_FLAGS));
			atmci_set_pending(host, EVENT_DATA_COMPLETE);
			tasklet_schedule(&host->tasklet);
		}
		if (pending & MCI_RXRDY)
			atmci_read_data_pio(host);
		if (pending & MCI_TXRDY)
			atmci_write_data_pio(host);

		if (pending & MCI_CMDRDY)
			atmci_cmd_interrupt(mmc, status);
	} while (pass_count++ < 5);

	spin_unlock(&mmc->lock);

	return pass_count ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t atmci_detect_interrupt(int irq, void *dev_id)
{
	struct mmc_host		*mmc = dev_id;
	struct atmel_mci	*host = mmc_priv(mmc);

	/*
	 * Disable interrupts until the pin has stabilized and check
	 * the state then. Use mod_timer() since we may be in the
	 * middle of the timer routine when this interrupt triggers.
	 */
	disable_irq_nosync(irq);
	mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(20));

	return IRQ_HANDLED;
}

static int __init atmci_probe(struct platform_device *pdev)
{
	struct mci_platform_data	*pdata;
	struct atmel_mci *host;
	struct mmc_host *mmc;
	struct resource *regs;
	int irq;
	int ret;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;
	pdata = pdev->dev.platform_data;
	if (!pdata)
		return -ENXIO;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mmc = mmc_alloc_host(sizeof(struct atmel_mci), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->pdev = pdev;
	host->mmc = mmc;
	host->detect_pin = pdata->detect_pin;
	host->wp_pin = pdata->wp_pin;

	host->mck = clk_get(&pdev->dev, "mci_clk");
	if (IS_ERR(host->mck)) {
		ret = PTR_ERR(host->mck);
		goto err_clk_get;
	}

	ret = -ENOMEM;
	host->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!host->regs)
		goto err_ioremap;

	clk_enable(host->mck);
	mci_writel(host, CR, MCI_CR_SWRST);
	host->bus_hz = clk_get_rate(host->mck);
	clk_disable(host->mck);

	host->mapbase = regs->start;

	mmc->ops = &atmci_ops;
	mmc->f_min = (host->bus_hz + 511) / 512;
	mmc->f_max = host->bus_hz / 2;
	mmc->ocr_avail	= MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps |= MMC_CAP_4_BIT_DATA;

	mmc->max_hw_segs = 64;
	mmc->max_phys_segs = 64;
	mmc->max_req_size = 32768 * 512;
	mmc->max_blk_size = 32768;
	mmc->max_blk_count = 512;

	tasklet_init(&host->tasklet, atmci_tasklet_func, (unsigned long)mmc);

	ret = request_irq(irq, atmci_interrupt, 0, pdev->dev.bus_id, mmc);
	if (ret)
		goto err_request_irq;

	/* Assume card is present if we don't have a detect pin */
	host->present = 1;
	if (gpio_is_valid(host->detect_pin)) {
		if (gpio_request(host->detect_pin, "mmc_detect")) {
			dev_dbg(&mmc->class_dev, "no detect pin available\n");
			host->detect_pin = -1;
		} else {
			host->present = !gpio_get_value(host->detect_pin);
		}
	}
	if (gpio_is_valid(host->wp_pin)) {
		if (gpio_request(host->wp_pin, "mmc_wp")) {
			dev_dbg(&mmc->class_dev, "no WP pin available\n");
			host->wp_pin = -1;
		}
	}

	platform_set_drvdata(pdev, host);

	mmc_add_host(mmc);

	if (gpio_is_valid(host->detect_pin)) {
		setup_timer(&host->detect_timer, atmci_detect_change,
				(unsigned long)host);

		ret = request_irq(gpio_to_irq(host->detect_pin),
				atmci_detect_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"mmc-detect", mmc);
		if (ret) {
			dev_dbg(&mmc->class_dev,
				"could not request IRQ %d for detect pin\n",
				gpio_to_irq(host->detect_pin));
			gpio_free(host->detect_pin);
			host->detect_pin = -1;
		}
	}

	dev_info(&mmc->class_dev,
			"Atmel MCI controller at 0x%08lx irq %d\n",
			host->mapbase, irq);

	atmci_init_debugfs(host);

	return 0;

err_request_irq:
	iounmap(host->regs);
err_ioremap:
	clk_put(host->mck);
err_clk_get:
	mmc_free_host(mmc);
	return ret;
}

static int __exit atmci_remove(struct platform_device *pdev)
{
	struct atmel_mci *host = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (host) {
		/* Debugfs stuff is cleaned up by mmc core */

		if (gpio_is_valid(host->detect_pin)) {
			int pin = host->detect_pin;

			/* Make sure the timer doesn't enable the interrupt */
			host->detect_pin = -1;
			smp_wmb();

			free_irq(gpio_to_irq(pin), host->mmc);
			del_timer_sync(&host->detect_timer);
			gpio_free(pin);
		}

		mmc_remove_host(host->mmc);

		clk_enable(host->mck);
		mci_writel(host, IDR, ~0UL);
		mci_writel(host, CR, MCI_CR_MCIDIS);
		mci_readl(host, SR);
		clk_disable(host->mck);

		if (gpio_is_valid(host->wp_pin))
			gpio_free(host->wp_pin);

		free_irq(platform_get_irq(pdev, 0), host->mmc);
		iounmap(host->regs);

		clk_put(host->mck);

		mmc_free_host(host->mmc);
	}
	return 0;
}

static struct platform_driver atmci_driver = {
	.remove		= __exit_p(atmci_remove),
	.driver		= {
		.name		= "atmel_mci",
	},
};

static int __init atmci_init(void)
{
	return platform_driver_probe(&atmci_driver, atmci_probe);
}

static void __exit atmci_exit(void)
{
	platform_driver_unregister(&atmci_driver);
}

module_init(atmci_init);
module_exit(atmci_exit);

MODULE_DESCRIPTION("Atmel Multimedia Card Interface driver");
MODULE_AUTHOR("Haavard Skinnemoen <haavard.skinnemoen@atmel.com>");
MODULE_LICENSE("GPL v2");
