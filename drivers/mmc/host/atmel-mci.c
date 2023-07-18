// SPDX-License-Identifier: GPL-2.0-only
/*
 * Atmel MultiMedia Card Interface driver
 *
 * Copyright (C) 2004-2008 Atmel Corporation
 */
#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>

#include <linux/atmel-mci.h>
#include <linux/atmel_pdc.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/unaligned.h>

/*
 * Superset of MCI IP registers integrated in Atmel AT91 Processor
 * Registers and bitfields marked with [2] are only available in MCI2
 */

/* MCI Register Definitions */
#define	ATMCI_CR			0x0000	/* Control */
#define		ATMCI_CR_MCIEN			BIT(0)		/* MCI Enable */
#define		ATMCI_CR_MCIDIS			BIT(1)		/* MCI Disable */
#define		ATMCI_CR_PWSEN			BIT(2)		/* Power Save Enable */
#define		ATMCI_CR_PWSDIS			BIT(3)		/* Power Save Disable */
#define		ATMCI_CR_SWRST			BIT(7)		/* Software Reset */
#define	ATMCI_MR			0x0004	/* Mode */
#define		ATMCI_MR_CLKDIV(x)		((x) <<  0)	/* Clock Divider */
#define		ATMCI_MR_PWSDIV(x)		((x) <<  8)	/* Power Saving Divider */
#define		ATMCI_MR_RDPROOF		BIT(11)		/* Read Proof */
#define		ATMCI_MR_WRPROOF		BIT(12)		/* Write Proof */
#define		ATMCI_MR_PDCFBYTE		BIT(13)		/* Force Byte Transfer */
#define		ATMCI_MR_PDCPADV		BIT(14)		/* Padding Value */
#define		ATMCI_MR_PDCMODE		BIT(15)		/* PDC-oriented Mode */
#define		ATMCI_MR_CLKODD(x)		((x) << 16)	/* LSB of Clock Divider */
#define	ATMCI_DTOR			0x0008	/* Data Timeout */
#define		ATMCI_DTOCYC(x)			((x) <<  0)	/* Data Timeout Cycles */
#define		ATMCI_DTOMUL(x)			((x) <<  4)	/* Data Timeout Multiplier */
#define	ATMCI_SDCR			0x000c	/* SD Card / SDIO */
#define		ATMCI_SDCSEL_SLOT_A		(0 <<  0)	/* Select SD slot A */
#define		ATMCI_SDCSEL_SLOT_B		(1 <<  0)	/* Select SD slot A */
#define		ATMCI_SDCSEL_MASK		(3 <<  0)
#define		ATMCI_SDCBUS_1BIT		(0 <<  6)	/* 1-bit data bus */
#define		ATMCI_SDCBUS_4BIT		(2 <<  6)	/* 4-bit data bus */
#define		ATMCI_SDCBUS_8BIT		(3 <<  6)	/* 8-bit data bus[2] */
#define		ATMCI_SDCBUS_MASK		(3 <<  6)
#define	ATMCI_ARGR			0x0010	/* Command Argument */
#define	ATMCI_CMDR			0x0014	/* Command */
#define		ATMCI_CMDR_CMDNB(x)		((x) <<  0)	/* Command Opcode */
#define		ATMCI_CMDR_RSPTYP_NONE		(0 <<  6)	/* No response */
#define		ATMCI_CMDR_RSPTYP_48BIT		(1 <<  6)	/* 48-bit response */
#define		ATMCI_CMDR_RSPTYP_136BIT	(2 <<  6)	/* 136-bit response */
#define		ATMCI_CMDR_SPCMD_INIT		(1 <<  8)	/* Initialization command */
#define		ATMCI_CMDR_SPCMD_SYNC		(2 <<  8)	/* Synchronized command */
#define		ATMCI_CMDR_SPCMD_INT		(4 <<  8)	/* Interrupt command */
#define		ATMCI_CMDR_SPCMD_INTRESP	(5 <<  8)	/* Interrupt response */
#define		ATMCI_CMDR_OPDCMD		(1 << 11)	/* Open Drain */
#define		ATMCI_CMDR_MAXLAT_5CYC		(0 << 12)	/* Max latency 5 cycles */
#define		ATMCI_CMDR_MAXLAT_64CYC		(1 << 12)	/* Max latency 64 cycles */
#define		ATMCI_CMDR_START_XFER		(1 << 16)	/* Start data transfer */
#define		ATMCI_CMDR_STOP_XFER		(2 << 16)	/* Stop data transfer */
#define		ATMCI_CMDR_TRDIR_WRITE		(0 << 18)	/* Write data */
#define		ATMCI_CMDR_TRDIR_READ		(1 << 18)	/* Read data */
#define		ATMCI_CMDR_BLOCK		(0 << 19)	/* Single-block transfer */
#define		ATMCI_CMDR_MULTI_BLOCK		(1 << 19)	/* Multi-block transfer */
#define		ATMCI_CMDR_STREAM		(2 << 19)	/* MMC Stream transfer */
#define		ATMCI_CMDR_SDIO_BYTE		(4 << 19)	/* SDIO Byte transfer */
#define		ATMCI_CMDR_SDIO_BLOCK		(5 << 19)	/* SDIO Block transfer */
#define		ATMCI_CMDR_SDIO_SUSPEND		(1 << 24)	/* SDIO Suspend Command */
#define		ATMCI_CMDR_SDIO_RESUME		(2 << 24)	/* SDIO Resume Command */
#define	ATMCI_BLKR			0x0018	/* Block */
#define		ATMCI_BCNT(x)			((x) <<  0)	/* Data Block Count */
#define		ATMCI_BLKLEN(x)			((x) << 16)	/* Data Block Length */
#define	ATMCI_CSTOR			0x001c	/* Completion Signal Timeout[2] */
#define		ATMCI_CSTOCYC(x)		((x) <<  0)	/* CST cycles */
#define		ATMCI_CSTOMUL(x)		((x) <<  4)	/* CST multiplier */
#define	ATMCI_RSPR			0x0020	/* Response 0 */
#define	ATMCI_RSPR1			0x0024	/* Response 1 */
#define	ATMCI_RSPR2			0x0028	/* Response 2 */
#define	ATMCI_RSPR3			0x002c	/* Response 3 */
#define	ATMCI_RDR			0x0030	/* Receive Data */
#define	ATMCI_TDR			0x0034	/* Transmit Data */
#define	ATMCI_SR			0x0040	/* Status */
#define	ATMCI_IER			0x0044	/* Interrupt Enable */
#define	ATMCI_IDR			0x0048	/* Interrupt Disable */
#define	ATMCI_IMR			0x004c	/* Interrupt Mask */
#define		ATMCI_CMDRDY			BIT(0)		/* Command Ready */
#define		ATMCI_RXRDY			BIT(1)		/* Receiver Ready */
#define		ATMCI_TXRDY			BIT(2)		/* Transmitter Ready */
#define		ATMCI_BLKE			BIT(3)		/* Data Block Ended */
#define		ATMCI_DTIP			BIT(4)		/* Data Transfer In Progress */
#define		ATMCI_NOTBUSY			BIT(5)		/* Data Not Busy */
#define		ATMCI_ENDRX			BIT(6)		/* End of RX Buffer */
#define		ATMCI_ENDTX			BIT(7)		/* End of TX Buffer */
#define		ATMCI_SDIOIRQA			BIT(8)		/* SDIO IRQ in slot A */
#define		ATMCI_SDIOIRQB			BIT(9)		/* SDIO IRQ in slot B */
#define		ATMCI_SDIOWAIT			BIT(12)		/* SDIO Read Wait Operation Status */
#define		ATMCI_CSRCV			BIT(13)		/* CE-ATA Completion Signal Received */
#define		ATMCI_RXBUFF			BIT(14)		/* RX Buffer Full */
#define		ATMCI_TXBUFE			BIT(15)		/* TX Buffer Empty */
#define		ATMCI_RINDE			BIT(16)		/* Response Index Error */
#define		ATMCI_RDIRE			BIT(17)		/* Response Direction Error */
#define		ATMCI_RCRCE			BIT(18)		/* Response CRC Error */
#define		ATMCI_RENDE			BIT(19)		/* Response End Bit Error */
#define		ATMCI_RTOE			BIT(20)		/* Response Time-Out Error */
#define		ATMCI_DCRCE			BIT(21)		/* Data CRC Error */
#define		ATMCI_DTOE			BIT(22)		/* Data Time-Out Error */
#define		ATMCI_CSTOE			BIT(23)		/* Completion Signal Time-out Error */
#define		ATMCI_BLKOVRE			BIT(24)		/* DMA Block Overrun Error */
#define		ATMCI_DMADONE			BIT(25)		/* DMA Transfer Done */
#define		ATMCI_FIFOEMPTY			BIT(26)		/* FIFO Empty Flag */
#define		ATMCI_XFRDONE			BIT(27)		/* Transfer Done Flag */
#define		ATMCI_ACKRCV			BIT(28)		/* Boot Operation Acknowledge Received */
#define		ATMCI_ACKRCVE			BIT(29)		/* Boot Operation Acknowledge Error */
#define		ATMCI_OVRE			BIT(30)		/* RX Overrun Error */
#define		ATMCI_UNRE			BIT(31)		/* TX Underrun Error */
#define	ATMCI_DMA			0x0050	/* DMA Configuration[2] */
#define		ATMCI_DMA_OFFSET(x)		((x) <<  0)	/* DMA Write Buffer Offset */
#define		ATMCI_DMA_CHKSIZE(x)		((x) <<  4)	/* DMA Channel Read and Write Chunk Size */
#define		ATMCI_DMAEN			BIT(8)	/* DMA Hardware Handshaking Enable */
#define	ATMCI_CFG			0x0054	/* Configuration[2] */
#define		ATMCI_CFG_FIFOMODE_1DATA	BIT(0)		/* MCI Internal FIFO control mode */
#define		ATMCI_CFG_FERRCTRL_COR		BIT(4)		/* Flow Error flag reset control mode */
#define		ATMCI_CFG_HSMODE		BIT(8)		/* High Speed Mode */
#define		ATMCI_CFG_LSYNC			BIT(12)		/* Synchronize on the last block */
#define	ATMCI_WPMR			0x00e4	/* Write Protection Mode[2] */
#define		ATMCI_WP_EN			BIT(0)		/* WP Enable */
#define		ATMCI_WP_KEY			(0x4d4349 << 8)	/* WP Key */
#define	ATMCI_WPSR			0x00e8	/* Write Protection Status[2] */
#define		ATMCI_GET_WP_VS(x)		((x) & 0x0f)
#define		ATMCI_GET_WP_VSRC(x)		(((x) >> 8) & 0xffff)
#define	ATMCI_VERSION			0x00FC  /* Version */
#define	ATMCI_FIFO_APERTURE		0x0200	/* FIFO Aperture[2] */

/* This is not including the FIFO Aperture on MCI2 */
#define	ATMCI_REGS_SIZE		0x100

/* Register access macros */
#define	atmci_readl(port, reg)				\
	__raw_readl((port)->regs + reg)
#define	atmci_writel(port, reg, value)			\
	__raw_writel((value), (port)->regs + reg)

#define ATMCI_CMD_TIMEOUT_MS	2000
#define AUTOSUSPEND_DELAY	50

#define ATMCI_DATA_ERROR_FLAGS	(ATMCI_DCRCE | ATMCI_DTOE | ATMCI_OVRE | ATMCI_UNRE)
#define ATMCI_DMA_THRESHOLD	16

enum {
	EVENT_CMD_RDY = 0,
	EVENT_XFER_COMPLETE,
	EVENT_NOTBUSY,
	EVENT_DATA_ERROR,
};

enum atmel_mci_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_DATA_XFER,
	STATE_WAITING_NOTBUSY,
	STATE_SENDING_STOP,
	STATE_END_REQUEST,
};

enum atmci_xfer_dir {
	XFER_RECEIVE = 0,
	XFER_TRANSMIT,
};

enum atmci_pdc_buf {
	PDC_FIRST_BUF = 0,
	PDC_SECOND_BUF,
};

struct atmel_mci_caps {
	bool    has_dma_conf_reg;
	bool    has_pdc;
	bool    has_cfg_reg;
	bool    has_cstor_reg;
	bool    has_highspeed;
	bool    has_rwproof;
	bool	has_odd_clk_div;
	bool	has_bad_data_ordering;
	bool	need_reset_after_xfer;
	bool	need_blksz_mul_4;
	bool	need_notbusy_for_read_ops;
};

struct atmel_mci_dma {
	struct dma_chan			*chan;
	struct dma_async_tx_descriptor	*data_desc;
};

/**
 * struct atmel_mci - MMC controller state shared between all slots
 * @lock: Spinlock protecting the queue and associated data.
 * @regs: Pointer to MMIO registers.
 * @sg: Scatterlist entry currently being processed by PIO or PDC code.
 * @sg_len: Size of the scatterlist
 * @pio_offset: Offset into the current scatterlist entry.
 * @buffer: Buffer used if we don't have the r/w proof capability. We
 *      don't have the time to switch pdc buffers so we have to use only
 *      one buffer for the full transaction.
 * @buf_size: size of the buffer.
 * @buf_phys_addr: buffer address needed for pdc.
 * @cur_slot: The slot which is currently using the controller.
 * @mrq: The request currently being processed on @cur_slot,
 *	or NULL if the controller is idle.
 * @cmd: The command currently being sent to the card, or NULL.
 * @data: The data currently being transferred, or NULL if no data
 *	transfer is in progress.
 * @data_size: just data->blocks * data->blksz.
 * @dma: DMA client state.
 * @data_chan: DMA channel being used for the current data transfer.
 * @dma_conf: Configuration for the DMA slave
 * @cmd_status: Snapshot of SR taken upon completion of the current
 *	command. Only valid when EVENT_CMD_COMPLETE is pending.
 * @data_status: Snapshot of SR taken upon completion of the current
 *	data transfer. Only valid when EVENT_DATA_COMPLETE or
 *	EVENT_DATA_ERROR is pending.
 * @stop_cmdr: Value to be loaded into CMDR when the stop command is
 *	to be sent.
 * @tasklet: Tasklet running the request state machine.
 * @pending_events: Bitmask of events flagged by the interrupt handler
 *	to be processed by the tasklet.
 * @completed_events: Bitmask of events which the state machine has
 *	processed.
 * @state: Tasklet state.
 * @queue: List of slots waiting for access to the controller.
 * @need_clock_update: Update the clock rate before the next request.
 * @need_reset: Reset controller before next request.
 * @timer: Timer to balance the data timeout error flag which cannot rise.
 * @mode_reg: Value of the MR register.
 * @cfg_reg: Value of the CFG register.
 * @bus_hz: The rate of @mck in Hz. This forms the basis for MMC bus
 *	rate and timeout calculations.
 * @mapbase: Physical address of the MMIO registers.
 * @mck: The peripheral bus clock hooked up to the MMC controller.
 * @pdev: Platform device associated with the MMC controller.
 * @slot: Slots sharing this MMC controller.
 * @caps: MCI capabilities depending on MCI version.
 * @prepare_data: function to setup MCI before data transfer which
 * depends on MCI capabilities.
 * @submit_data: function to start data transfer which depends on MCI
 * capabilities.
 * @stop_transfer: function to stop data transfer which depends on MCI
 * capabilities.
 *
 * Locking
 * =======
 *
 * @lock is a softirq-safe spinlock protecting @queue as well as
 * @cur_slot, @mrq and @state. These must always be updated
 * at the same time while holding @lock.
 *
 * @lock also protects mode_reg and need_clock_update since these are
 * used to synchronize mode register updates with the queue
 * processing.
 *
 * The @mrq field of struct atmel_mci_slot is also protected by @lock,
 * and must always be written at the same time as the slot is added to
 * @queue.
 *
 * @pending_events and @completed_events are accessed using atomic bit
 * operations, so they don't need any locking.
 *
 * None of the fields touched by the interrupt handler need any
 * locking. However, ordering is important: Before EVENT_DATA_ERROR or
 * EVENT_DATA_COMPLETE is set in @pending_events, all data-related
 * interrupts must be disabled and @data_status updated with a
 * snapshot of SR. Similarly, before EVENT_CMD_COMPLETE is set, the
 * CMDRDY interrupt must be disabled and @cmd_status updated with a
 * snapshot of SR, and before EVENT_XFER_COMPLETE can be set, the
 * bytes_xfered field of @data must be written. This is ensured by
 * using barriers.
 */
struct atmel_mci {
	spinlock_t		lock;
	void __iomem		*regs;

	struct scatterlist	*sg;
	unsigned int		sg_len;
	unsigned int		pio_offset;
	unsigned int		*buffer;
	unsigned int		buf_size;
	dma_addr_t		buf_phys_addr;

	struct atmel_mci_slot	*cur_slot;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	unsigned int		data_size;

	struct atmel_mci_dma	dma;
	struct dma_chan		*data_chan;
	struct dma_slave_config	dma_conf;

	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;

	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum atmel_mci_state	state;
	struct list_head	queue;

	bool			need_clock_update;
	bool			need_reset;
	struct timer_list	timer;
	u32			mode_reg;
	u32			cfg_reg;
	unsigned long		bus_hz;
	unsigned long		mapbase;
	struct clk		*mck;
	struct platform_device	*pdev;

	struct atmel_mci_slot	*slot[ATMCI_MAX_NR_SLOTS];

	struct atmel_mci_caps   caps;

	u32 (*prepare_data)(struct atmel_mci *host, struct mmc_data *data);
	void (*submit_data)(struct atmel_mci *host, struct mmc_data *data);
	void (*stop_transfer)(struct atmel_mci *host);
};

/**
 * struct atmel_mci_slot - MMC slot state
 * @mmc: The mmc_host representing this slot.
 * @host: The MMC controller this slot is using.
 * @sdc_reg: Value of SDCR to be written before using this slot.
 * @sdio_irq: SDIO irq mask for this slot.
 * @mrq: mmc_request currently being processed or waiting to be
 *	processed, or NULL when the slot is idle.
 * @queue_node: List node for placing this node in the @queue list of
 *	&struct atmel_mci.
 * @clock: Clock rate configured by set_ios(). Protected by host->lock.
 * @flags: Random state bits associated with the slot.
 * @detect_pin: GPIO pin used for card detection, or negative if not
 *	available.
 * @wp_pin: GPIO pin used for card write protect sending, or negative
 *	if not available.
 * @detect_is_active_high: The state of the detect pin when it is active.
 * @detect_timer: Timer used for debouncing @detect_pin interrupts.
 */
struct atmel_mci_slot {
	struct mmc_host		*mmc;
	struct atmel_mci	*host;

	u32			sdc_reg;
	u32			sdio_irq;

	struct mmc_request	*mrq;
	struct list_head	queue_node;

	unsigned int		clock;
	unsigned long		flags;
#define ATMCI_CARD_PRESENT	0
#define ATMCI_CARD_NEED_INIT	1
#define ATMCI_SHUTDOWN		2

	int			detect_pin;
	int			wp_pin;
	bool			detect_is_active_high;

	struct timer_list	detect_timer;
};

#define atmci_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define atmci_set_completed(host, event)			\
	set_bit(event, &host->completed_events)
#define atmci_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

/*
 * The debugfs stuff below is mostly optimized away when
 * CONFIG_DEBUG_FS is not set.
 */
static int atmci_req_show(struct seq_file *s, void *v)
{
	struct atmel_mci_slot	*slot = s->private;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

	/* Make sure we get a consistent snapshot */
	spin_lock_bh(&slot->host->lock);
	mrq = slot->mrq;

	if (mrq) {
		cmd = mrq->cmd;
		data = mrq->data;
		stop = mrq->stop;

		if (cmd)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				cmd->opcode, cmd->arg, cmd->flags,
				cmd->resp[0], cmd->resp[1], cmd->resp[2],
				cmd->resp[3], cmd->error);
		if (data)
			seq_printf(s, "DATA %u / %u * %u flg %x err %d\n",
				data->bytes_xfered, data->blocks,
				data->blksz, data->flags, data->error);
		if (stop)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				stop->opcode, stop->arg, stop->flags,
				stop->resp[0], stop->resp[1], stop->resp[2],
				stop->resp[3], stop->error);
	}

	spin_unlock_bh(&slot->host->lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(atmci_req);

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
		[6]	= "ENDRX",
		[7]	= "ENDTX",
		[8]	= "SDIOIRQA",
		[9]	= "SDIOIRQB",
		[12]	= "SDIOWAIT",
		[14]	= "RXBUFF",
		[15]	= "TXBUFE",
		[16]	= "RINDE",
		[17]	= "RDIRE",
		[18]	= "RCRCE",
		[19]	= "RENDE",
		[20]	= "RTOE",
		[21]	= "DCRCE",
		[22]	= "DTOE",
		[23]	= "CSTOE",
		[24]	= "BLKOVRE",
		[25]	= "DMADONE",
		[26]	= "FIFOEMPTY",
		[27]	= "XFRDONE",
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
	int			ret = 0;


	buf = kmalloc(ATMCI_REGS_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pm_runtime_get_sync(&host->pdev->dev);

	/*
	 * Grab a more or less consistent snapshot. Note that we're
	 * not disabling interrupts, so IMR and SR may not be
	 * consistent.
	 */
	spin_lock_bh(&host->lock);
	memcpy_fromio(buf, host->regs, ATMCI_REGS_SIZE);
	spin_unlock_bh(&host->lock);

	pm_runtime_mark_last_busy(&host->pdev->dev);
	pm_runtime_put_autosuspend(&host->pdev->dev);

	seq_printf(s, "MR:\t0x%08x%s%s ",
			buf[ATMCI_MR / 4],
			buf[ATMCI_MR / 4] & ATMCI_MR_RDPROOF ? " RDPROOF" : "",
			buf[ATMCI_MR / 4] & ATMCI_MR_WRPROOF ? " WRPROOF" : "");
	if (host->caps.has_odd_clk_div)
		seq_printf(s, "{CLKDIV,CLKODD}=%u\n",
				((buf[ATMCI_MR / 4] & 0xff) << 1)
				| ((buf[ATMCI_MR / 4] >> 16) & 1));
	else
		seq_printf(s, "CLKDIV=%u\n",
				(buf[ATMCI_MR / 4] & 0xff));
	seq_printf(s, "DTOR:\t0x%08x\n", buf[ATMCI_DTOR / 4]);
	seq_printf(s, "SDCR:\t0x%08x\n", buf[ATMCI_SDCR / 4]);
	seq_printf(s, "ARGR:\t0x%08x\n", buf[ATMCI_ARGR / 4]);
	seq_printf(s, "BLKR:\t0x%08x BCNT=%u BLKLEN=%u\n",
			buf[ATMCI_BLKR / 4],
			buf[ATMCI_BLKR / 4] & 0xffff,
			(buf[ATMCI_BLKR / 4] >> 16) & 0xffff);
	if (host->caps.has_cstor_reg)
		seq_printf(s, "CSTOR:\t0x%08x\n", buf[ATMCI_CSTOR / 4]);

	/* Don't read RSPR and RDR; it will consume the data there */

	atmci_show_status_reg(s, "SR", buf[ATMCI_SR / 4]);
	atmci_show_status_reg(s, "IMR", buf[ATMCI_IMR / 4]);

	if (host->caps.has_dma_conf_reg) {
		u32 val;

		val = buf[ATMCI_DMA / 4];
		seq_printf(s, "DMA:\t0x%08x OFFSET=%u CHKSIZE=%u%s\n",
				val, val & 3,
				((val >> 4) & 3) ?
					1 << (((val >> 4) & 3) + 1) : 1,
				val & ATMCI_DMAEN ? " DMAEN" : "");
	}
	if (host->caps.has_cfg_reg) {
		u32 val;

		val = buf[ATMCI_CFG / 4];
		seq_printf(s, "CFG:\t0x%08x%s%s%s%s\n",
				val,
				val & ATMCI_CFG_FIFOMODE_1DATA ? " FIFOMODE_ONE_DATA" : "",
				val & ATMCI_CFG_FERRCTRL_COR ? " FERRCTRL_CLEAR_ON_READ" : "",
				val & ATMCI_CFG_HSMODE ? " HSMODE" : "",
				val & ATMCI_CFG_LSYNC ? " LSYNC" : "");
	}

	kfree(buf);

	return ret;
}

DEFINE_SHOW_ATTRIBUTE(atmci_regs);

static void atmci_init_debugfs(struct atmel_mci_slot *slot)
{
	struct mmc_host		*mmc = slot->mmc;
	struct atmel_mci	*host = slot->host;
	struct dentry		*root;

	root = mmc->debugfs_root;
	if (!root)
		return;

	debugfs_create_file("regs", S_IRUSR, root, host, &atmci_regs_fops);
	debugfs_create_file("req", S_IRUSR, root, slot, &atmci_req_fops);
	debugfs_create_u32("state", S_IRUSR, root, &host->state);
	debugfs_create_xul("pending_events", S_IRUSR, root,
			   &host->pending_events);
	debugfs_create_xul("completed_events", S_IRUSR, root,
			   &host->completed_events);
}

#if defined(CONFIG_OF)
static const struct of_device_id atmci_dt_ids[] = {
	{ .compatible = "atmel,hsmci" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmci_dt_ids);

static struct mci_platform_data*
atmci_of_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cnp;
	struct mci_platform_data *pdata;
	u32 slot_id;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "reg", &slot_id)) {
			dev_warn(&pdev->dev, "reg property is missing for %pOF\n",
				 cnp);
			continue;
		}

		if (slot_id >= ATMCI_MAX_NR_SLOTS) {
			dev_warn(&pdev->dev, "can't have more than %d slots\n",
			         ATMCI_MAX_NR_SLOTS);
			of_node_put(cnp);
			break;
		}

		if (of_property_read_u32(cnp, "bus-width",
		                         &pdata->slot[slot_id].bus_width))
			pdata->slot[slot_id].bus_width = 1;

		pdata->slot[slot_id].detect_pin =
			of_get_named_gpio(cnp, "cd-gpios", 0);

		pdata->slot[slot_id].detect_is_active_high =
			of_property_read_bool(cnp, "cd-inverted");

		pdata->slot[slot_id].non_removable =
			of_property_read_bool(cnp, "non-removable");

		pdata->slot[slot_id].wp_pin =
			of_get_named_gpio(cnp, "wp-gpios", 0);
	}

	return pdata;
}
#else /* CONFIG_OF */
static inline struct mci_platform_data*
atmci_of_init(struct platform_device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static inline unsigned int atmci_get_version(struct atmel_mci *host)
{
	return atmci_readl(host, ATMCI_VERSION) & 0x00000fff;
}

/*
 * Fix sconfig's burst size according to atmel MCI. We need to convert them as:
 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3.
 * With version 0x600, we need to convert them as: 1 -> 0, 2 -> 1, 4 -> 2,
 * 8 -> 3, 16 -> 4.
 *
 * This can be done by finding most significant bit set.
 */
static inline unsigned int atmci_convert_chksize(struct atmel_mci *host,
						 unsigned int maxburst)
{
	unsigned int version = atmci_get_version(host);
	unsigned int offset = 2;

	if (version >= 0x600)
		offset = 1;

	if (maxburst > 1)
		return fls(maxburst) - offset;
	else
		return 0;
}

static void atmci_timeout_timer(struct timer_list *t)
{
	struct atmel_mci *host;

	host = from_timer(host, t, timer);

	dev_dbg(&host->pdev->dev, "software timeout\n");

	if (host->mrq->cmd->data) {
		host->mrq->cmd->data->error = -ETIMEDOUT;
		host->data = NULL;
		/*
		 * With some SDIO modules, sometimes DMA transfer hangs. If
		 * stop_transfer() is not called then the DMA request is not
		 * removed, following ones are queued and never computed.
		 */
		if (host->state == STATE_DATA_XFER)
			host->stop_transfer(host);
	} else {
		host->mrq->cmd->error = -ETIMEDOUT;
		host->cmd = NULL;
	}
	host->need_reset = 1;
	host->state = STATE_END_REQUEST;
	smp_wmb();
	tasklet_schedule(&host->tasklet);
}

static inline unsigned int atmci_ns_to_clocks(struct atmel_mci *host,
					unsigned int ns)
{
	/*
	 * It is easier here to use us instead of ns for the timeout,
	 * it prevents from overflows during calculation.
	 */
	unsigned int us = DIV_ROUND_UP(ns, 1000);

	/* Maximum clock frequency is host->bus_hz/2 */
	return us * (DIV_ROUND_UP(host->bus_hz, 2000000));
}

static void atmci_set_timeout(struct atmel_mci *host,
		struct atmel_mci_slot *slot, struct mmc_data *data)
{
	static unsigned	dtomul_to_shift[] = {
		0, 4, 7, 8, 10, 12, 16, 20
	};
	unsigned	timeout;
	unsigned	dtocyc;
	unsigned	dtomul;

	timeout = atmci_ns_to_clocks(host, data->timeout_ns)
		+ data->timeout_clks;

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

	dev_vdbg(&slot->mmc->class_dev, "setting timeout to %u cycles\n",
			dtocyc << dtomul_to_shift[dtomul]);
	atmci_writel(host, ATMCI_DTOR, (ATMCI_DTOMUL(dtomul) | ATMCI_DTOCYC(dtocyc)));
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

	cmdr = ATMCI_CMDR_CMDNB(cmd->opcode);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			cmdr |= ATMCI_CMDR_RSPTYP_136BIT;
		else
			cmdr |= ATMCI_CMDR_RSPTYP_48BIT;
	}

	/*
	 * This should really be MAXLAT_5 for CMD2 and ACMD41, but
	 * it's too difficult to determine whether this is an ACMD or
	 * not. Better make it 64.
	 */
	cmdr |= ATMCI_CMDR_MAXLAT_64CYC;

	if (mmc->ios.bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdr |= ATMCI_CMDR_OPDCMD;

	data = cmd->data;
	if (data) {
		cmdr |= ATMCI_CMDR_START_XFER;

		if (cmd->opcode == SD_IO_RW_EXTENDED) {
			cmdr |= ATMCI_CMDR_SDIO_BLOCK;
		} else {
			if (data->blocks > 1)
				cmdr |= ATMCI_CMDR_MULTI_BLOCK;
			else
				cmdr |= ATMCI_CMDR_BLOCK;
		}

		if (data->flags & MMC_DATA_READ)
			cmdr |= ATMCI_CMDR_TRDIR_READ;
	}

	return cmdr;
}

static void atmci_send_command(struct atmel_mci *host,
		struct mmc_command *cmd, u32 cmd_flags)
{
	unsigned int timeout_ms = cmd->busy_timeout ? cmd->busy_timeout :
		ATMCI_CMD_TIMEOUT_MS;

	WARN_ON(host->cmd);
	host->cmd = cmd;

	dev_vdbg(&host->pdev->dev,
			"start command: ARGR=0x%08x CMDR=0x%08x\n",
			cmd->arg, cmd_flags);

	atmci_writel(host, ATMCI_ARGR, cmd->arg);
	atmci_writel(host, ATMCI_CMDR, cmd_flags);

	mod_timer(&host->timer, jiffies + msecs_to_jiffies(timeout_ms));
}

static void atmci_send_stop_cmd(struct atmel_mci *host, struct mmc_data *data)
{
	dev_dbg(&host->pdev->dev, "send stop command\n");
	atmci_send_command(host, data->stop, host->stop_cmdr);
	atmci_writel(host, ATMCI_IER, ATMCI_CMDRDY);
}

/*
 * Configure given PDC buffer taking care of alignement issues.
 * Update host->data_size and host->sg.
 */
static void atmci_pdc_set_single_buf(struct atmel_mci *host,
	enum atmci_xfer_dir dir, enum atmci_pdc_buf buf_nb)
{
	u32 pointer_reg, counter_reg;
	unsigned int buf_size;

	if (dir == XFER_RECEIVE) {
		pointer_reg = ATMEL_PDC_RPR;
		counter_reg = ATMEL_PDC_RCR;
	} else {
		pointer_reg = ATMEL_PDC_TPR;
		counter_reg = ATMEL_PDC_TCR;
	}

	if (buf_nb == PDC_SECOND_BUF) {
		pointer_reg += ATMEL_PDC_SCND_BUF_OFF;
		counter_reg += ATMEL_PDC_SCND_BUF_OFF;
	}

	if (!host->caps.has_rwproof) {
		buf_size = host->buf_size;
		atmci_writel(host, pointer_reg, host->buf_phys_addr);
	} else {
		buf_size = sg_dma_len(host->sg);
		atmci_writel(host, pointer_reg, sg_dma_address(host->sg));
	}

	if (host->data_size <= buf_size) {
		if (host->data_size & 0x3) {
			/* If size is different from modulo 4, transfer bytes */
			atmci_writel(host, counter_reg, host->data_size);
			atmci_writel(host, ATMCI_MR, host->mode_reg | ATMCI_MR_PDCFBYTE);
		} else {
			/* Else transfer 32-bits words */
			atmci_writel(host, counter_reg, host->data_size / 4);
		}
		host->data_size = 0;
	} else {
		/* We assume the size of a page is 32-bits aligned */
		atmci_writel(host, counter_reg, sg_dma_len(host->sg) / 4);
		host->data_size -= sg_dma_len(host->sg);
		if (host->data_size)
			host->sg = sg_next(host->sg);
	}
}

/*
 * Configure PDC buffer according to the data size ie configuring one or two
 * buffers. Don't use this function if you want to configure only the second
 * buffer. In this case, use atmci_pdc_set_single_buf.
 */
static void atmci_pdc_set_both_buf(struct atmel_mci *host, int dir)
{
	atmci_pdc_set_single_buf(host, dir, PDC_FIRST_BUF);
	if (host->data_size)
		atmci_pdc_set_single_buf(host, dir, PDC_SECOND_BUF);
}

/*
 * Unmap sg lists, called when transfer is finished.
 */
static void atmci_pdc_cleanup(struct atmel_mci *host)
{
	struct mmc_data         *data = host->data;

	if (data)
		dma_unmap_sg(&host->pdev->dev,
				data->sg, data->sg_len,
				mmc_get_dma_dir(data));
}

/*
 * Disable PDC transfers. Update pending flags to EVENT_XFER_COMPLETE after
 * having received ATMCI_TXBUFE or ATMCI_RXBUFF interrupt. Enable ATMCI_NOTBUSY
 * interrupt needed for both transfer directions.
 */
static void atmci_pdc_complete(struct atmel_mci *host)
{
	int transfer_size = host->data->blocks * host->data->blksz;
	int i;

	atmci_writel(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTDIS | ATMEL_PDC_TXTDIS);

	if ((!host->caps.has_rwproof)
	    && (host->data->flags & MMC_DATA_READ)) {
		if (host->caps.has_bad_data_ordering)
			for (i = 0; i < transfer_size; i++)
				host->buffer[i] = swab32(host->buffer[i]);
		sg_copy_from_buffer(host->data->sg, host->data->sg_len,
		                    host->buffer, transfer_size);
	}

	atmci_pdc_cleanup(host);

	dev_dbg(&host->pdev->dev, "(%s) set pending xfer complete\n", __func__);
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
	tasklet_schedule(&host->tasklet);
}

static void atmci_dma_cleanup(struct atmel_mci *host)
{
	struct mmc_data                 *data = host->data;

	if (data)
		dma_unmap_sg(host->dma.chan->device->dev,
				data->sg, data->sg_len,
				mmc_get_dma_dir(data));
}

/*
 * This function is called by the DMA driver from tasklet context.
 */
static void atmci_dma_complete(void *arg)
{
	struct atmel_mci	*host = arg;
	struct mmc_data		*data = host->data;

	dev_vdbg(&host->pdev->dev, "DMA complete\n");

	if (host->caps.has_dma_conf_reg)
		/* Disable DMA hardware handshaking on MCI */
		atmci_writel(host, ATMCI_DMA, atmci_readl(host, ATMCI_DMA) & ~ATMCI_DMAEN);

	atmci_dma_cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point trying
	 * to send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		dev_dbg(&host->pdev->dev,
		        "(%s) set pending xfer complete\n", __func__);
		atmci_set_pending(host, EVENT_XFER_COMPLETE);
		tasklet_schedule(&host->tasklet);

		/*
		 * Regardless of what the documentation says, we have
		 * to wait for NOTBUSY even after block read
		 * operations.
		 *
		 * When the DMA transfer is complete, the controller
		 * may still be reading the CRC from the card, i.e.
		 * the data transfer is still in progress and we
		 * haven't seen all the potential error bits yet.
		 *
		 * The interrupt handler will schedule a different
		 * tasklet to finish things up when the data transfer
		 * is completely done.
		 *
		 * We may not complete the mmc request here anyway
		 * because the mmc layer may call back and cause us to
		 * violate the "don't submit new operations from the
		 * completion callback" rule of the dma engine
		 * framework.
		 */
		atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
	}
}

/*
 * Returns a mask of interrupt flags to be enabled after the whole
 * request has been prepared.
 */
static u32 atmci_prepare_data(struct atmel_mci *host, struct mmc_data *data)
{
	u32 iflags;

	data->error = -EINPROGRESS;

	host->sg = data->sg;
	host->sg_len = data->sg_len;
	host->data = data;
	host->data_chan = NULL;

	iflags = ATMCI_DATA_ERROR_FLAGS;

	/*
	 * Errata: MMC data write operation with less than 12
	 * bytes is impossible.
	 *
	 * Errata: MCI Transmit Data Register (TDR) FIFO
	 * corruption when length is not multiple of 4.
	 */
	if (data->blocks * data->blksz < 12
			|| (data->blocks * data->blksz) & 3)
		host->need_reset = true;

	host->pio_offset = 0;
	if (data->flags & MMC_DATA_READ)
		iflags |= ATMCI_RXRDY;
	else
		iflags |= ATMCI_TXRDY;

	return iflags;
}

/*
 * Set interrupt flags and set block length into the MCI mode register even
 * if this value is also accessible in the MCI block register. It seems to be
 * necessary before the High Speed MCI version. It also map sg and configure
 * PDC registers.
 */
static u32
atmci_prepare_data_pdc(struct atmel_mci *host, struct mmc_data *data)
{
	u32 iflags, tmp;
	int i;

	data->error = -EINPROGRESS;

	host->data = data;
	host->sg = data->sg;
	iflags = ATMCI_DATA_ERROR_FLAGS;

	/* Enable pdc mode */
	atmci_writel(host, ATMCI_MR, host->mode_reg | ATMCI_MR_PDCMODE);

	if (data->flags & MMC_DATA_READ)
		iflags |= ATMCI_ENDRX | ATMCI_RXBUFF;
	else
		iflags |= ATMCI_ENDTX | ATMCI_TXBUFE | ATMCI_BLKE;

	/* Set BLKLEN */
	tmp = atmci_readl(host, ATMCI_MR);
	tmp &= 0x0000ffff;
	tmp |= ATMCI_BLKLEN(data->blksz);
	atmci_writel(host, ATMCI_MR, tmp);

	/* Configure PDC */
	host->data_size = data->blocks * data->blksz;
	dma_map_sg(&host->pdev->dev, data->sg, data->sg_len,
		   mmc_get_dma_dir(data));

	if ((!host->caps.has_rwproof)
	    && (host->data->flags & MMC_DATA_WRITE)) {
		sg_copy_to_buffer(host->data->sg, host->data->sg_len,
		                  host->buffer, host->data_size);
		if (host->caps.has_bad_data_ordering)
			for (i = 0; i < host->data_size; i++)
				host->buffer[i] = swab32(host->buffer[i]);
	}

	if (host->data_size)
		atmci_pdc_set_both_buf(host, data->flags & MMC_DATA_READ ?
				       XFER_RECEIVE : XFER_TRANSMIT);
	return iflags;
}

static u32
atmci_prepare_data_dma(struct atmel_mci *host, struct mmc_data *data)
{
	struct dma_chan			*chan;
	struct dma_async_tx_descriptor	*desc;
	struct scatterlist		*sg;
	unsigned int			i;
	enum dma_transfer_direction	slave_dirn;
	unsigned int			sglen;
	u32				maxburst;
	u32 iflags;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	iflags = ATMCI_DATA_ERROR_FLAGS;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < ATMCI_DMA_THRESHOLD)
		return atmci_prepare_data(host, data);
	if (data->blksz & 3)
		return atmci_prepare_data(host, data);

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return atmci_prepare_data(host, data);
	}

	/* If we don't have a channel, we can't do DMA */
	if (!host->dma.chan)
		return -ENODEV;

	chan = host->dma.chan;
	host->data_chan = chan;

	if (data->flags & MMC_DATA_READ) {
		host->dma_conf.direction = slave_dirn = DMA_DEV_TO_MEM;
		maxburst = atmci_convert_chksize(host,
						 host->dma_conf.src_maxburst);
	} else {
		host->dma_conf.direction = slave_dirn = DMA_MEM_TO_DEV;
		maxburst = atmci_convert_chksize(host,
						 host->dma_conf.dst_maxburst);
	}

	if (host->caps.has_dma_conf_reg)
		atmci_writel(host, ATMCI_DMA, ATMCI_DMA_CHKSIZE(maxburst) |
			ATMCI_DMAEN);

	sglen = dma_map_sg(chan->device->dev, data->sg,
			data->sg_len, mmc_get_dma_dir(data));

	dmaengine_slave_config(chan, &host->dma_conf);
	desc = dmaengine_prep_slave_sg(chan,
			data->sg, sglen, slave_dirn,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		goto unmap_exit;

	host->dma.data_desc = desc;
	desc->callback = atmci_dma_complete;
	desc->callback_param = host;

	return iflags;
unmap_exit:
	dma_unmap_sg(chan->device->dev, data->sg, data->sg_len,
		     mmc_get_dma_dir(data));
	return -ENOMEM;
}

static void
atmci_submit_data(struct atmel_mci *host, struct mmc_data *data)
{
	return;
}

/*
 * Start PDC according to transfer direction.
 */
static void
atmci_submit_data_pdc(struct atmel_mci *host, struct mmc_data *data)
{
	if (data->flags & MMC_DATA_READ)
		atmci_writel(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTEN);
	else
		atmci_writel(host, ATMEL_PDC_PTCR, ATMEL_PDC_TXTEN);
}

static void
atmci_submit_data_dma(struct atmel_mci *host, struct mmc_data *data)
{
	struct dma_chan			*chan = host->data_chan;
	struct dma_async_tx_descriptor	*desc = host->dma.data_desc;

	if (chan) {
		dmaengine_submit(desc);
		dma_async_issue_pending(chan);
	}
}

static void atmci_stop_transfer(struct atmel_mci *host)
{
	dev_dbg(&host->pdev->dev,
	        "(%s) set pending xfer complete\n", __func__);
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
	atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
}

/*
 * Stop data transfer because error(s) occurred.
 */
static void atmci_stop_transfer_pdc(struct atmel_mci *host)
{
	atmci_writel(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTDIS | ATMEL_PDC_TXTDIS);
}

static void atmci_stop_transfer_dma(struct atmel_mci *host)
{
	struct dma_chan *chan = host->data_chan;

	if (chan) {
		dmaengine_terminate_all(chan);
		atmci_dma_cleanup(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		dev_dbg(&host->pdev->dev,
		        "(%s) set pending xfer complete\n", __func__);
		atmci_set_pending(host, EVENT_XFER_COMPLETE);
		atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
	}
}

/*
 * Start a request: prepare data if needed, prepare the command and activate
 * interrupts.
 */
static void atmci_start_request(struct atmel_mci *host,
		struct atmel_mci_slot *slot)
{
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	u32			iflags;
	u32			cmdflags;

	mrq = slot->mrq;
	host->cur_slot = slot;
	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->cmd_status = 0;
	host->data_status = 0;

	dev_dbg(&host->pdev->dev, "start request: cmd %u\n", mrq->cmd->opcode);

	if (host->need_reset || host->caps.need_reset_after_xfer) {
		iflags = atmci_readl(host, ATMCI_IMR);
		iflags &= (ATMCI_SDIOIRQA | ATMCI_SDIOIRQB);
		atmci_writel(host, ATMCI_CR, ATMCI_CR_SWRST);
		atmci_writel(host, ATMCI_CR, ATMCI_CR_MCIEN);
		atmci_writel(host, ATMCI_MR, host->mode_reg);
		if (host->caps.has_cfg_reg)
			atmci_writel(host, ATMCI_CFG, host->cfg_reg);
		atmci_writel(host, ATMCI_IER, iflags);
		host->need_reset = false;
	}
	atmci_writel(host, ATMCI_SDCR, slot->sdc_reg);

	iflags = atmci_readl(host, ATMCI_IMR);
	if (iflags & ~(ATMCI_SDIOIRQA | ATMCI_SDIOIRQB))
		dev_dbg(&slot->mmc->class_dev, "WARNING: IMR=0x%08x\n",
				iflags);

	if (unlikely(test_and_clear_bit(ATMCI_CARD_NEED_INIT, &slot->flags))) {
		/* Send init sequence (74 clock cycles) */
		atmci_writel(host, ATMCI_CMDR, ATMCI_CMDR_SPCMD_INIT);
		while (!(atmci_readl(host, ATMCI_SR) & ATMCI_CMDRDY))
			cpu_relax();
	}
	iflags = 0;
	data = mrq->data;
	if (data) {
		atmci_set_timeout(host, slot, data);

		/* Must set block count/size before sending command */
		atmci_writel(host, ATMCI_BLKR, ATMCI_BCNT(data->blocks)
				| ATMCI_BLKLEN(data->blksz));
		dev_vdbg(&slot->mmc->class_dev, "BLKR=0x%08x\n",
			ATMCI_BCNT(data->blocks) | ATMCI_BLKLEN(data->blksz));

		iflags |= host->prepare_data(host, data);
	}

	iflags |= ATMCI_CMDRDY;
	cmd = mrq->cmd;
	cmdflags = atmci_prepare_command(slot->mmc, cmd);

	/*
	 * DMA transfer should be started before sending the command to avoid
	 * unexpected errors especially for read operations in SDIO mode.
	 * Unfortunately, in PDC mode, command has to be sent before starting
	 * the transfer.
	 */
	if (host->submit_data != &atmci_submit_data_dma)
		atmci_send_command(host, cmd, cmdflags);

	if (data)
		host->submit_data(host, data);

	if (host->submit_data == &atmci_submit_data_dma)
		atmci_send_command(host, cmd, cmdflags);

	if (mrq->stop) {
		host->stop_cmdr = atmci_prepare_command(slot->mmc, mrq->stop);
		host->stop_cmdr |= ATMCI_CMDR_STOP_XFER;
		if (!(data->flags & MMC_DATA_WRITE))
			host->stop_cmdr |= ATMCI_CMDR_TRDIR_READ;
		host->stop_cmdr |= ATMCI_CMDR_MULTI_BLOCK;
	}

	/*
	 * We could have enabled interrupts earlier, but I suspect
	 * that would open up a nice can of interesting race
	 * conditions (e.g. command and data complete, but stop not
	 * prepared yet.)
	 */
	atmci_writel(host, ATMCI_IER, iflags);
}

static void atmci_queue_request(struct atmel_mci *host,
		struct atmel_mci_slot *slot, struct mmc_request *mrq)
{
	dev_vdbg(&slot->mmc->class_dev, "queue request: state=%d\n",
			host->state);

	spin_lock_bh(&host->lock);
	slot->mrq = mrq;
	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		atmci_start_request(host, slot);
	} else {
		dev_dbg(&host->pdev->dev, "queue request\n");
		list_add_tail(&slot->queue_node, &host->queue);
	}
	spin_unlock_bh(&host->lock);
}

static void atmci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct atmel_mci_slot	*slot = mmc_priv(mmc);
	struct atmel_mci	*host = slot->host;
	struct mmc_data		*data;

	WARN_ON(slot->mrq);
	dev_dbg(&host->pdev->dev, "MRQ: cmd %u\n", mrq->cmd->opcode);

	/*
	 * We may "know" the card is gone even though there's still an
	 * electrical connection. If so, we really need to communicate
	 * this to the MMC core since there won't be any more
	 * interrupts as the card is completely removed. Otherwise,
	 * the MMC core might believe the card is still there even
	 * though the card was just removed very slowly.
	 */
	if (!test_bit(ATMCI_CARD_PRESENT, &slot->flags)) {
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	/* We don't support multiple blocks of weird lengths. */
	data = mrq->data;
	if (data && data->blocks > 1 && data->blksz & 3) {
		mrq->cmd->error = -EINVAL;
		mmc_request_done(mmc, mrq);
	}

	atmci_queue_request(host, slot, mrq);
}

static void atmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct atmel_mci_slot	*slot = mmc_priv(mmc);
	struct atmel_mci	*host = slot->host;
	unsigned int		i;

	slot->sdc_reg &= ~ATMCI_SDCBUS_MASK;
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		slot->sdc_reg |= ATMCI_SDCBUS_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		slot->sdc_reg |= ATMCI_SDCBUS_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		slot->sdc_reg |= ATMCI_SDCBUS_8BIT;
		break;
	}

	if (ios->clock) {
		unsigned int clock_min = ~0U;
		int clkdiv;

		spin_lock_bh(&host->lock);
		if (!host->mode_reg) {
			atmci_writel(host, ATMCI_CR, ATMCI_CR_SWRST);
			atmci_writel(host, ATMCI_CR, ATMCI_CR_MCIEN);
			if (host->caps.has_cfg_reg)
				atmci_writel(host, ATMCI_CFG, host->cfg_reg);
		}

		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		slot->clock = ios->clock;
		for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
			if (host->slot[i] && host->slot[i]->clock
					&& host->slot[i]->clock < clock_min)
				clock_min = host->slot[i]->clock;
		}

		/* Calculate clock divider */
		if (host->caps.has_odd_clk_div) {
			clkdiv = DIV_ROUND_UP(host->bus_hz, clock_min) - 2;
			if (clkdiv < 0) {
				dev_warn(&mmc->class_dev,
					 "clock %u too fast; using %lu\n",
					 clock_min, host->bus_hz / 2);
				clkdiv = 0;
			} else if (clkdiv > 511) {
				dev_warn(&mmc->class_dev,
				         "clock %u too slow; using %lu\n",
				         clock_min, host->bus_hz / (511 + 2));
				clkdiv = 511;
			}
			host->mode_reg = ATMCI_MR_CLKDIV(clkdiv >> 1)
			                 | ATMCI_MR_CLKODD(clkdiv & 1);
		} else {
			clkdiv = DIV_ROUND_UP(host->bus_hz, 2 * clock_min) - 1;
			if (clkdiv > 255) {
				dev_warn(&mmc->class_dev,
				         "clock %u too slow; using %lu\n",
				         clock_min, host->bus_hz / (2 * 256));
				clkdiv = 255;
			}
			host->mode_reg = ATMCI_MR_CLKDIV(clkdiv);
		}

		/*
		 * WRPROOF and RDPROOF prevent overruns/underruns by
		 * stopping the clock when the FIFO is full/empty.
		 * This state is not expected to last for long.
		 */
		if (host->caps.has_rwproof)
			host->mode_reg |= (ATMCI_MR_WRPROOF | ATMCI_MR_RDPROOF);

		if (host->caps.has_cfg_reg) {
			/* setup High Speed mode in relation with card capacity */
			if (ios->timing == MMC_TIMING_SD_HS)
				host->cfg_reg |= ATMCI_CFG_HSMODE;
			else
				host->cfg_reg &= ~ATMCI_CFG_HSMODE;
		}

		if (list_empty(&host->queue)) {
			atmci_writel(host, ATMCI_MR, host->mode_reg);
			if (host->caps.has_cfg_reg)
				atmci_writel(host, ATMCI_CFG, host->cfg_reg);
		} else {
			host->need_clock_update = true;
		}

		spin_unlock_bh(&host->lock);
	} else {
		bool any_slot_active = false;

		spin_lock_bh(&host->lock);
		slot->clock = 0;
		for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
			if (host->slot[i] && host->slot[i]->clock) {
				any_slot_active = true;
				break;
			}
		}
		if (!any_slot_active) {
			atmci_writel(host, ATMCI_CR, ATMCI_CR_MCIDIS);
			if (host->mode_reg) {
				atmci_readl(host, ATMCI_MR);
			}
			host->mode_reg = 0;
		}
		spin_unlock_bh(&host->lock);
	}

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		break;
	case MMC_POWER_UP:
		set_bit(ATMCI_CARD_NEED_INIT, &slot->flags);
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, ios->vdd);
		break;
	default:
		break;
	}
}

static int atmci_get_ro(struct mmc_host *mmc)
{
	int			read_only = -ENOSYS;
	struct atmel_mci_slot	*slot = mmc_priv(mmc);

	if (gpio_is_valid(slot->wp_pin)) {
		read_only = gpio_get_value(slot->wp_pin);
		dev_dbg(&mmc->class_dev, "card is %s\n",
				read_only ? "read-only" : "read-write");
	}

	return read_only;
}

static int atmci_get_cd(struct mmc_host *mmc)
{
	int			present = -ENOSYS;
	struct atmel_mci_slot	*slot = mmc_priv(mmc);

	if (gpio_is_valid(slot->detect_pin)) {
		present = !(gpio_get_value(slot->detect_pin) ^
			    slot->detect_is_active_high);
		dev_dbg(&mmc->class_dev, "card is %spresent\n",
				present ? "" : "not ");
	}

	return present;
}

static void atmci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct atmel_mci_slot	*slot = mmc_priv(mmc);
	struct atmel_mci	*host = slot->host;

	if (enable)
		atmci_writel(host, ATMCI_IER, slot->sdio_irq);
	else
		atmci_writel(host, ATMCI_IDR, slot->sdio_irq);
}

static const struct mmc_host_ops atmci_ops = {
	.request	= atmci_request,
	.set_ios	= atmci_set_ios,
	.get_ro		= atmci_get_ro,
	.get_cd		= atmci_get_cd,
	.enable_sdio_irq = atmci_enable_sdio_irq,
};

/* Called with host->lock held */
static void atmci_request_end(struct atmel_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct atmel_mci_slot	*slot = NULL;
	struct mmc_host		*prev_mmc = host->cur_slot->mmc;

	WARN_ON(host->cmd || host->data);

	del_timer(&host->timer);

	/*
	 * Update the MMC clock rate if necessary. This may be
	 * necessary if set_ios() is called when a different slot is
	 * busy transferring data.
	 */
	if (host->need_clock_update) {
		atmci_writel(host, ATMCI_MR, host->mode_reg);
		if (host->caps.has_cfg_reg)
			atmci_writel(host, ATMCI_CFG, host->cfg_reg);
	}

	host->cur_slot->mrq = NULL;
	host->mrq = NULL;
	if (!list_empty(&host->queue)) {
		slot = list_entry(host->queue.next,
				struct atmel_mci_slot, queue_node);
		list_del(&slot->queue_node);
		dev_vdbg(&host->pdev->dev, "list not empty: %s is next\n",
				mmc_hostname(slot->mmc));
		host->state = STATE_SENDING_CMD;
		atmci_start_request(host, slot);
	} else {
		dev_vdbg(&host->pdev->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	mmc_request_done(prev_mmc, mrq);
	spin_lock(&host->lock);
}

static void atmci_command_complete(struct atmel_mci *host,
			struct mmc_command *cmd)
{
	u32		status = host->cmd_status;

	/* Read the response from the card (up to 16 bytes) */
	cmd->resp[0] = atmci_readl(host, ATMCI_RSPR);
	cmd->resp[1] = atmci_readl(host, ATMCI_RSPR);
	cmd->resp[2] = atmci_readl(host, ATMCI_RSPR);
	cmd->resp[3] = atmci_readl(host, ATMCI_RSPR);

	if (status & ATMCI_RTOE)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & ATMCI_RCRCE))
		cmd->error = -EILSEQ;
	else if (status & (ATMCI_RINDE | ATMCI_RDIRE | ATMCI_RENDE))
		cmd->error = -EIO;
	else if (host->mrq->data && (host->mrq->data->blksz & 3)) {
		if (host->caps.need_blksz_mul_4) {
			cmd->error = -EINVAL;
			host->need_reset = 1;
		}
	} else
		cmd->error = 0;
}

static void atmci_detect_change(struct timer_list *t)
{
	struct atmel_mci_slot	*slot = from_timer(slot, t, detect_timer);
	bool			present;
	bool			present_old;

	/*
	 * atmci_cleanup_slot() sets the ATMCI_SHUTDOWN flag before
	 * freeing the interrupt. We must not re-enable the interrupt
	 * if it has been freed, and if we're shutting down, it
	 * doesn't really matter whether the card is present or not.
	 */
	smp_rmb();
	if (test_bit(ATMCI_SHUTDOWN, &slot->flags))
		return;

	enable_irq(gpio_to_irq(slot->detect_pin));
	present = !(gpio_get_value(slot->detect_pin) ^
		    slot->detect_is_active_high);
	present_old = test_bit(ATMCI_CARD_PRESENT, &slot->flags);

	dev_vdbg(&slot->mmc->class_dev, "detect change: %d (was %d)\n",
			present, present_old);

	if (present != present_old) {
		struct atmel_mci	*host = slot->host;
		struct mmc_request	*mrq;

		dev_dbg(&slot->mmc->class_dev, "card %s\n",
			present ? "inserted" : "removed");

		spin_lock(&host->lock);

		if (!present)
			clear_bit(ATMCI_CARD_PRESENT, &slot->flags);
		else
			set_bit(ATMCI_CARD_PRESENT, &slot->flags);

		/* Clean up queue if present */
		mrq = slot->mrq;
		if (mrq) {
			if (mrq == host->mrq) {
				/*
				 * Reset controller to terminate any ongoing
				 * commands or data transfers.
				 */
				atmci_writel(host, ATMCI_CR, ATMCI_CR_SWRST);
				atmci_writel(host, ATMCI_CR, ATMCI_CR_MCIEN);
				atmci_writel(host, ATMCI_MR, host->mode_reg);
				if (host->caps.has_cfg_reg)
					atmci_writel(host, ATMCI_CFG, host->cfg_reg);

				host->data = NULL;
				host->cmd = NULL;

				switch (host->state) {
				case STATE_IDLE:
					break;
				case STATE_SENDING_CMD:
					mrq->cmd->error = -ENOMEDIUM;
					if (mrq->data)
						host->stop_transfer(host);
					break;
				case STATE_DATA_XFER:
					mrq->data->error = -ENOMEDIUM;
					host->stop_transfer(host);
					break;
				case STATE_WAITING_NOTBUSY:
					mrq->data->error = -ENOMEDIUM;
					break;
				case STATE_SENDING_STOP:
					mrq->stop->error = -ENOMEDIUM;
					break;
				case STATE_END_REQUEST:
					break;
				}

				atmci_request_end(host, mrq);
			} else {
				list_del(&slot->queue_node);
				mrq->cmd->error = -ENOMEDIUM;
				if (mrq->data)
					mrq->data->error = -ENOMEDIUM;
				if (mrq->stop)
					mrq->stop->error = -ENOMEDIUM;

				spin_unlock(&host->lock);
				mmc_request_done(slot->mmc, mrq);
				spin_lock(&host->lock);
			}
		}
		spin_unlock(&host->lock);

		mmc_detect_change(slot->mmc, 0);
	}
}

static void atmci_tasklet_func(struct tasklet_struct *t)
{
	struct atmel_mci        *host = from_tasklet(host, t, tasklet);
	struct mmc_request	*mrq = host->mrq;
	struct mmc_data		*data = host->data;
	enum atmel_mci_state	state = host->state;
	enum atmel_mci_state	prev_state;
	u32			status;

	spin_lock(&host->lock);

	state = host->state;

	dev_vdbg(&host->pdev->dev,
		"tasklet: state %u pending/completed/mask %lx/%lx/%x\n",
		state, host->pending_events, host->completed_events,
		atmci_readl(host, ATMCI_IMR));

	do {
		prev_state = state;
		dev_dbg(&host->pdev->dev, "FSM: state=%d\n", state);

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			/*
			 * Command has been sent, we are waiting for command
			 * ready. Then we have three next states possible:
			 * END_REQUEST by default, WAITING_NOTBUSY if it's a
			 * command needing it or DATA_XFER if there is data.
			 */
			dev_dbg(&host->pdev->dev, "FSM: cmd ready?\n");
			if (!atmci_test_and_clear_pending(host,
						EVENT_CMD_RDY))
				break;

			dev_dbg(&host->pdev->dev, "set completed cmd ready\n");
			host->cmd = NULL;
			atmci_set_completed(host, EVENT_CMD_RDY);
			atmci_command_complete(host, mrq->cmd);
			if (mrq->data) {
				dev_dbg(&host->pdev->dev,
				        "command with data transfer");
				/*
				 * If there is a command error don't start
				 * data transfer.
				 */
				if (mrq->cmd->error) {
					host->stop_transfer(host);
					host->data = NULL;
					atmci_writel(host, ATMCI_IDR,
					             ATMCI_TXRDY | ATMCI_RXRDY
					             | ATMCI_DATA_ERROR_FLAGS);
					state = STATE_END_REQUEST;
				} else
					state = STATE_DATA_XFER;
			} else if ((!mrq->data) && (mrq->cmd->flags & MMC_RSP_BUSY)) {
				dev_dbg(&host->pdev->dev,
				        "command response need waiting notbusy");
				atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
				state = STATE_WAITING_NOTBUSY;
			} else
				state = STATE_END_REQUEST;

			break;

		case STATE_DATA_XFER:
			if (atmci_test_and_clear_pending(host,
						EVENT_DATA_ERROR)) {
				dev_dbg(&host->pdev->dev, "set completed data error\n");
				atmci_set_completed(host, EVENT_DATA_ERROR);
				state = STATE_END_REQUEST;
				break;
			}

			/*
			 * A data transfer is in progress. The event expected
			 * to move to the next state depends of data transfer
			 * type (PDC or DMA). Once transfer done we can move
			 * to the next step which is WAITING_NOTBUSY in write
			 * case and directly SENDING_STOP in read case.
			 */
			dev_dbg(&host->pdev->dev, "FSM: xfer complete?\n");
			if (!atmci_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;

			dev_dbg(&host->pdev->dev,
			        "(%s) set completed xfer complete\n",
				__func__);
			atmci_set_completed(host, EVENT_XFER_COMPLETE);

			if (host->caps.need_notbusy_for_read_ops ||
			   (host->data->flags & MMC_DATA_WRITE)) {
				atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
				state = STATE_WAITING_NOTBUSY;
			} else if (host->mrq->stop) {
				atmci_send_stop_cmd(host, data);
				state = STATE_SENDING_STOP;
			} else {
				host->data = NULL;
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
				state = STATE_END_REQUEST;
			}
			break;

		case STATE_WAITING_NOTBUSY:
			/*
			 * We can be in the state for two reasons: a command
			 * requiring waiting not busy signal (stop command
			 * included) or a write operation. In the latest case,
			 * we need to send a stop command.
			 */
			dev_dbg(&host->pdev->dev, "FSM: not busy?\n");
			if (!atmci_test_and_clear_pending(host,
						EVENT_NOTBUSY))
				break;

			dev_dbg(&host->pdev->dev, "set completed not busy\n");
			atmci_set_completed(host, EVENT_NOTBUSY);

			if (host->data) {
				/*
				 * For some commands such as CMD53, even if
				 * there is data transfer, there is no stop
				 * command to send.
				 */
				if (host->mrq->stop) {
					atmci_send_stop_cmd(host, data);
					state = STATE_SENDING_STOP;
				} else {
					host->data = NULL;
					data->bytes_xfered = data->blocks
					                     * data->blksz;
					data->error = 0;
					state = STATE_END_REQUEST;
				}
			} else
				state = STATE_END_REQUEST;
			break;

		case STATE_SENDING_STOP:
			/*
			 * In this state, it is important to set host->data to
			 * NULL (which is tested in the waiting notbusy state)
			 * in order to go to the end request state instead of
			 * sending stop again.
			 */
			dev_dbg(&host->pdev->dev, "FSM: cmd ready?\n");
			if (!atmci_test_and_clear_pending(host,
						EVENT_CMD_RDY))
				break;

			dev_dbg(&host->pdev->dev, "FSM: cmd ready\n");
			host->cmd = NULL;
			data->bytes_xfered = data->blocks * data->blksz;
			data->error = 0;
			atmci_command_complete(host, mrq->stop);
			if (mrq->stop->error) {
				host->stop_transfer(host);
				atmci_writel(host, ATMCI_IDR,
				             ATMCI_TXRDY | ATMCI_RXRDY
				             | ATMCI_DATA_ERROR_FLAGS);
				state = STATE_END_REQUEST;
			} else {
				atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
				state = STATE_WAITING_NOTBUSY;
			}
			host->data = NULL;
			break;

		case STATE_END_REQUEST:
			atmci_writel(host, ATMCI_IDR, ATMCI_TXRDY | ATMCI_RXRDY
			                   | ATMCI_DATA_ERROR_FLAGS);
			status = host->data_status;
			if (unlikely(status)) {
				host->stop_transfer(host);
				host->data = NULL;
				if (data) {
					if (status & ATMCI_DTOE) {
						data->error = -ETIMEDOUT;
					} else if (status & ATMCI_DCRCE) {
						data->error = -EILSEQ;
					} else {
						data->error = -EIO;
					}
				}
			}

			atmci_request_end(host, host->mrq);
			goto unlock; /* atmci_request_end() sets host->state */
			break;
		}
	} while (state != prev_state);

	host->state = state;

unlock:
	spin_unlock(&host->lock);
}

static void atmci_read_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		value = atmci_readl(host, ATMCI_RDR);
		if (likely(offset + 4 <= sg->length)) {
			sg_pcopy_from_buffer(sg, 1, &value, sizeof(u32), offset);

			offset += 4;
			nbytes += 4;

			if (offset == sg->length) {
				flush_dcache_page(sg_page(sg));
				host->sg = sg = sg_next(sg);
				host->sg_len--;
				if (!sg || !host->sg_len)
					goto done;

				offset = 0;
			}
		} else {
			unsigned int remaining = sg->length - offset;

			sg_pcopy_from_buffer(sg, 1, &value, remaining, offset);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			host->sg_len--;
			if (!sg || !host->sg_len)
				goto done;

			offset = 4 - remaining;
			sg_pcopy_from_buffer(sg, 1, (u8 *)&value + remaining,
					offset, 0);
			nbytes += offset;
		}

		status = atmci_readl(host, ATMCI_SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			atmci_writel(host, ATMCI_IDR, (ATMCI_NOTBUSY | ATMCI_RXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			data->bytes_xfered += nbytes;
			return;
		}
	} while (status & ATMCI_RXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	atmci_writel(host, ATMCI_IDR, ATMCI_RXRDY);
	atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	smp_wmb();
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
}

static void atmci_write_data_pio(struct atmel_mci *host)
{
	struct scatterlist	*sg = host->sg;
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			value;
	u32			status;
	unsigned int		nbytes = 0;

	do {
		if (likely(offset + 4 <= sg->length)) {
			sg_pcopy_to_buffer(sg, 1, &value, sizeof(u32), offset);
			atmci_writel(host, ATMCI_TDR, value);

			offset += 4;
			nbytes += 4;
			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				host->sg_len--;
				if (!sg || !host->sg_len)
					goto done;

				offset = 0;
			}
		} else {
			unsigned int remaining = sg->length - offset;

			value = 0;
			sg_pcopy_to_buffer(sg, 1, &value, remaining, offset);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			host->sg_len--;
			if (!sg || !host->sg_len) {
				atmci_writel(host, ATMCI_TDR, value);
				goto done;
			}

			offset = 4 - remaining;
			sg_pcopy_to_buffer(sg, 1, (u8 *)&value + remaining,
					offset, 0);
			atmci_writel(host, ATMCI_TDR, value);
			nbytes += offset;
		}

		status = atmci_readl(host, ATMCI_SR);
		if (status & ATMCI_DATA_ERROR_FLAGS) {
			atmci_writel(host, ATMCI_IDR, (ATMCI_NOTBUSY | ATMCI_TXRDY
						| ATMCI_DATA_ERROR_FLAGS));
			host->data_status = status;
			data->bytes_xfered += nbytes;
			return;
		}
	} while (status & ATMCI_TXRDY);

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	atmci_writel(host, ATMCI_IDR, ATMCI_TXRDY);
	atmci_writel(host, ATMCI_IER, ATMCI_NOTBUSY);
	data->bytes_xfered += nbytes;
	smp_wmb();
	atmci_set_pending(host, EVENT_XFER_COMPLETE);
}

static void atmci_sdio_interrupt(struct atmel_mci *host, u32 status)
{
	int	i;

	for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
		struct atmel_mci_slot *slot = host->slot[i];
		if (slot && (status & slot->sdio_irq)) {
			mmc_signal_sdio_irq(slot->mmc);
		}
	}
}


static irqreturn_t atmci_interrupt(int irq, void *dev_id)
{
	struct atmel_mci	*host = dev_id;
	u32			status, mask, pending;
	unsigned int		pass_count = 0;

	do {
		status = atmci_readl(host, ATMCI_SR);
		mask = atmci_readl(host, ATMCI_IMR);
		pending = status & mask;
		if (!pending)
			break;

		if (pending & ATMCI_DATA_ERROR_FLAGS) {
			dev_dbg(&host->pdev->dev, "IRQ: data error\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_DATA_ERROR_FLAGS
					| ATMCI_RXRDY | ATMCI_TXRDY
					| ATMCI_ENDRX | ATMCI_ENDTX
					| ATMCI_RXBUFF | ATMCI_TXBUFE);

			host->data_status = status;
			dev_dbg(&host->pdev->dev, "set pending data error\n");
			smp_wmb();
			atmci_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & ATMCI_TXBUFE) {
			dev_dbg(&host->pdev->dev, "IRQ: tx buffer empty\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_TXBUFE);
			atmci_writel(host, ATMCI_IDR, ATMCI_ENDTX);
			/*
			 * We can receive this interruption before having configured
			 * the second pdc buffer, so we need to reconfigure first and
			 * second buffers again
			 */
			if (host->data_size) {
				atmci_pdc_set_both_buf(host, XFER_TRANSMIT);
				atmci_writel(host, ATMCI_IER, ATMCI_ENDTX);
				atmci_writel(host, ATMCI_IER, ATMCI_TXBUFE);
			} else {
				atmci_pdc_complete(host);
			}
		} else if (pending & ATMCI_ENDTX) {
			dev_dbg(&host->pdev->dev, "IRQ: end of tx buffer\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_ENDTX);

			if (host->data_size) {
				atmci_pdc_set_single_buf(host,
						XFER_TRANSMIT, PDC_SECOND_BUF);
				atmci_writel(host, ATMCI_IER, ATMCI_ENDTX);
			}
		}

		if (pending & ATMCI_RXBUFF) {
			dev_dbg(&host->pdev->dev, "IRQ: rx buffer full\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_RXBUFF);
			atmci_writel(host, ATMCI_IDR, ATMCI_ENDRX);
			/*
			 * We can receive this interruption before having configured
			 * the second pdc buffer, so we need to reconfigure first and
			 * second buffers again
			 */
			if (host->data_size) {
				atmci_pdc_set_both_buf(host, XFER_RECEIVE);
				atmci_writel(host, ATMCI_IER, ATMCI_ENDRX);
				atmci_writel(host, ATMCI_IER, ATMCI_RXBUFF);
			} else {
				atmci_pdc_complete(host);
			}
		} else if (pending & ATMCI_ENDRX) {
			dev_dbg(&host->pdev->dev, "IRQ: end of rx buffer\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_ENDRX);

			if (host->data_size) {
				atmci_pdc_set_single_buf(host,
						XFER_RECEIVE, PDC_SECOND_BUF);
				atmci_writel(host, ATMCI_IER, ATMCI_ENDRX);
			}
		}

		/*
		 * First mci IPs, so mainly the ones having pdc, have some
		 * issues with the notbusy signal. You can't get it after
		 * data transmission if you have not sent a stop command.
		 * The appropriate workaround is to use the BLKE signal.
		 */
		if (pending & ATMCI_BLKE) {
			dev_dbg(&host->pdev->dev, "IRQ: blke\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_BLKE);
			smp_wmb();
			dev_dbg(&host->pdev->dev, "set pending notbusy\n");
			atmci_set_pending(host, EVENT_NOTBUSY);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & ATMCI_NOTBUSY) {
			dev_dbg(&host->pdev->dev, "IRQ: not_busy\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_NOTBUSY);
			smp_wmb();
			dev_dbg(&host->pdev->dev, "set pending notbusy\n");
			atmci_set_pending(host, EVENT_NOTBUSY);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & ATMCI_RXRDY)
			atmci_read_data_pio(host);
		if (pending & ATMCI_TXRDY)
			atmci_write_data_pio(host);

		if (pending & ATMCI_CMDRDY) {
			dev_dbg(&host->pdev->dev, "IRQ: cmd ready\n");
			atmci_writel(host, ATMCI_IDR, ATMCI_CMDRDY);
			host->cmd_status = status;
			smp_wmb();
			dev_dbg(&host->pdev->dev, "set pending cmd rdy\n");
			atmci_set_pending(host, EVENT_CMD_RDY);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & (ATMCI_SDIOIRQA | ATMCI_SDIOIRQB))
			atmci_sdio_interrupt(host, status);

	} while (pass_count++ < 5);

	return pass_count ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t atmci_detect_interrupt(int irq, void *dev_id)
{
	struct atmel_mci_slot	*slot = dev_id;

	/*
	 * Disable interrupts until the pin has stabilized and check
	 * the state then. Use mod_timer() since we may be in the
	 * middle of the timer routine when this interrupt triggers.
	 */
	disable_irq_nosync(irq);
	mod_timer(&slot->detect_timer, jiffies + msecs_to_jiffies(20));

	return IRQ_HANDLED;
}

static int atmci_init_slot(struct atmel_mci *host,
		struct mci_slot_pdata *slot_data, unsigned int id,
		u32 sdc_reg, u32 sdio_irq)
{
	struct mmc_host			*mmc;
	struct atmel_mci_slot		*slot;
	int ret;

	mmc = mmc_alloc_host(sizeof(struct atmel_mci_slot), &host->pdev->dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->mmc = mmc;
	slot->host = host;
	slot->detect_pin = slot_data->detect_pin;
	slot->wp_pin = slot_data->wp_pin;
	slot->detect_is_active_high = slot_data->detect_is_active_high;
	slot->sdc_reg = sdc_reg;
	slot->sdio_irq = sdio_irq;

	dev_dbg(&mmc->class_dev,
	        "slot[%u]: bus_width=%u, detect_pin=%d, "
		"detect_is_active_high=%s, wp_pin=%d\n",
		id, slot_data->bus_width, slot_data->detect_pin,
		slot_data->detect_is_active_high ? "true" : "false",
		slot_data->wp_pin);

	mmc->ops = &atmci_ops;
	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 512);
	mmc->f_max = host->bus_hz / 2;
	mmc->ocr_avail	= MMC_VDD_32_33 | MMC_VDD_33_34;
	if (sdio_irq)
		mmc->caps |= MMC_CAP_SDIO_IRQ;
	if (host->caps.has_highspeed)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;
	/*
	 * Without the read/write proof capability, it is strongly suggested to
	 * use only one bit for data to prevent fifo underruns and overruns
	 * which will corrupt data.
	 */
	if ((slot_data->bus_width >= 4) && host->caps.has_rwproof) {
		mmc->caps |= MMC_CAP_4_BIT_DATA;
		if (slot_data->bus_width >= 8)
			mmc->caps |= MMC_CAP_8_BIT_DATA;
	}

	if (atmci_get_version(host) < 0x200) {
		mmc->max_segs = 256;
		mmc->max_blk_size = 4095;
		mmc->max_blk_count = 256;
		mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
		mmc->max_seg_size = mmc->max_blk_size * mmc->max_segs;
	} else {
		mmc->max_segs = 64;
		mmc->max_req_size = 32768 * 512;
		mmc->max_blk_size = 32768;
		mmc->max_blk_count = 512;
	}

	/* Assume card is present initially */
	set_bit(ATMCI_CARD_PRESENT, &slot->flags);
	if (gpio_is_valid(slot->detect_pin)) {
		if (devm_gpio_request(&host->pdev->dev, slot->detect_pin,
				      "mmc_detect")) {
			dev_dbg(&mmc->class_dev, "no detect pin available\n");
			slot->detect_pin = -EBUSY;
		} else if (gpio_get_value(slot->detect_pin) ^
				slot->detect_is_active_high) {
			clear_bit(ATMCI_CARD_PRESENT, &slot->flags);
		}
	}

	if (!gpio_is_valid(slot->detect_pin)) {
		if (slot_data->non_removable)
			mmc->caps |= MMC_CAP_NONREMOVABLE;
		else
			mmc->caps |= MMC_CAP_NEEDS_POLL;
	}

	if (gpio_is_valid(slot->wp_pin)) {
		if (devm_gpio_request(&host->pdev->dev, slot->wp_pin,
				      "mmc_wp")) {
			dev_dbg(&mmc->class_dev, "no WP pin available\n");
			slot->wp_pin = -EBUSY;
		}
	}

	host->slot[id] = slot;
	mmc_regulator_get_supply(mmc);
	ret = mmc_add_host(mmc);
	if (ret) {
		mmc_free_host(mmc);
		return ret;
	}

	if (gpio_is_valid(slot->detect_pin)) {
		timer_setup(&slot->detect_timer, atmci_detect_change, 0);

		ret = request_irq(gpio_to_irq(slot->detect_pin),
				atmci_detect_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"mmc-detect", slot);
		if (ret) {
			dev_dbg(&mmc->class_dev,
				"could not request IRQ %d for detect pin\n",
				gpio_to_irq(slot->detect_pin));
			slot->detect_pin = -EBUSY;
		}
	}

	atmci_init_debugfs(slot);

	return 0;
}

static void atmci_cleanup_slot(struct atmel_mci_slot *slot,
		unsigned int id)
{
	/* Debugfs stuff is cleaned up by mmc core */

	set_bit(ATMCI_SHUTDOWN, &slot->flags);
	smp_wmb();

	mmc_remove_host(slot->mmc);

	if (gpio_is_valid(slot->detect_pin)) {
		int pin = slot->detect_pin;

		free_irq(gpio_to_irq(pin), slot);
		del_timer_sync(&slot->detect_timer);
	}

	slot->host->slot[id] = NULL;
	mmc_free_host(slot->mmc);
}

static int atmci_configure_dma(struct atmel_mci *host)
{
	host->dma.chan = dma_request_chan(&host->pdev->dev, "rxtx");

	if (PTR_ERR(host->dma.chan) == -ENODEV) {
		struct mci_platform_data *pdata = host->pdev->dev.platform_data;
		dma_cap_mask_t mask;

		if (!pdata || !pdata->dma_filter)
			return -ENODEV;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->dma.chan = dma_request_channel(mask, pdata->dma_filter,
						     pdata->dma_slave);
		if (!host->dma.chan)
			host->dma.chan = ERR_PTR(-ENODEV);
	}

	if (IS_ERR(host->dma.chan))
		return PTR_ERR(host->dma.chan);

	dev_info(&host->pdev->dev, "using %s for DMA transfers\n",
		 dma_chan_name(host->dma.chan));

	host->dma_conf.src_addr = host->mapbase + ATMCI_RDR;
	host->dma_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_conf.src_maxburst = 1;
	host->dma_conf.dst_addr = host->mapbase + ATMCI_TDR;
	host->dma_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_conf.dst_maxburst = 1;
	host->dma_conf.device_fc = false;

	return 0;
}

/*
 * HSMCI (High Speed MCI) module is not fully compatible with MCI module.
 * HSMCI provides DMA support and a new config register but no more supports
 * PDC.
 */
static void atmci_get_cap(struct atmel_mci *host)
{
	unsigned int version;

	version = atmci_get_version(host);
	dev_info(&host->pdev->dev,
			"version: 0x%x\n", version);

	host->caps.has_dma_conf_reg = false;
	host->caps.has_pdc = true;
	host->caps.has_cfg_reg = false;
	host->caps.has_cstor_reg = false;
	host->caps.has_highspeed = false;
	host->caps.has_rwproof = false;
	host->caps.has_odd_clk_div = false;
	host->caps.has_bad_data_ordering = true;
	host->caps.need_reset_after_xfer = true;
	host->caps.need_blksz_mul_4 = true;
	host->caps.need_notbusy_for_read_ops = false;

	/* keep only major version number */
	switch (version & 0xf00) {
	case 0x600:
	case 0x500:
		host->caps.has_odd_clk_div = true;
		fallthrough;
	case 0x400:
	case 0x300:
		host->caps.has_dma_conf_reg = true;
		host->caps.has_pdc = false;
		host->caps.has_cfg_reg = true;
		host->caps.has_cstor_reg = true;
		host->caps.has_highspeed = true;
		fallthrough;
	case 0x200:
		host->caps.has_rwproof = true;
		host->caps.need_blksz_mul_4 = false;
		host->caps.need_notbusy_for_read_ops = true;
		fallthrough;
	case 0x100:
		host->caps.has_bad_data_ordering = false;
		host->caps.need_reset_after_xfer = false;
		fallthrough;
	case 0x0:
		break;
	default:
		host->caps.has_pdc = false;
		dev_warn(&host->pdev->dev,
				"Unmanaged mci version, set minimum capabilities\n");
		break;
	}
}

static int atmci_probe(struct platform_device *pdev)
{
	struct mci_platform_data	*pdata;
	struct atmel_mci		*host;
	struct resource			*regs;
	unsigned int			nr_slots;
	int				irq;
	int				ret, i;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		pdata = atmci_of_init(pdev);
		if (IS_ERR(pdata)) {
			dev_err(&pdev->dev, "platform data not available\n");
			return PTR_ERR(pdata);
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;
	spin_lock_init(&host->lock);
	INIT_LIST_HEAD(&host->queue);

	host->mck = devm_clk_get(&pdev->dev, "mci_clk");
	if (IS_ERR(host->mck))
		return PTR_ERR(host->mck);

	host->regs = devm_ioremap(&pdev->dev, regs->start, resource_size(regs));
	if (!host->regs)
		return -ENOMEM;

	ret = clk_prepare_enable(host->mck);
	if (ret)
		return ret;

	atmci_writel(host, ATMCI_CR, ATMCI_CR_SWRST);
	host->bus_hz = clk_get_rate(host->mck);

	host->mapbase = regs->start;

	tasklet_setup(&host->tasklet, atmci_tasklet_func);

	ret = request_irq(irq, atmci_interrupt, 0, dev_name(&pdev->dev), host);
	if (ret) {
		clk_disable_unprepare(host->mck);
		return ret;
	}

	/* Get MCI capabilities and set operations according to it */
	atmci_get_cap(host);
	ret = atmci_configure_dma(host);
	if (ret == -EPROBE_DEFER)
		goto err_dma_probe_defer;
	if (ret == 0) {
		host->prepare_data = &atmci_prepare_data_dma;
		host->submit_data = &atmci_submit_data_dma;
		host->stop_transfer = &atmci_stop_transfer_dma;
	} else if (host->caps.has_pdc) {
		dev_info(&pdev->dev, "using PDC\n");
		host->prepare_data = &atmci_prepare_data_pdc;
		host->submit_data = &atmci_submit_data_pdc;
		host->stop_transfer = &atmci_stop_transfer_pdc;
	} else {
		dev_info(&pdev->dev, "using PIO\n");
		host->prepare_data = &atmci_prepare_data;
		host->submit_data = &atmci_submit_data;
		host->stop_transfer = &atmci_stop_transfer;
	}

	platform_set_drvdata(pdev, host);

	timer_setup(&host->timer, atmci_timeout_timer, 0);

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	/* We need at least one slot to succeed */
	nr_slots = 0;
	ret = -ENODEV;
	if (pdata->slot[0].bus_width) {
		ret = atmci_init_slot(host, &pdata->slot[0],
				0, ATMCI_SDCSEL_SLOT_A, ATMCI_SDIOIRQA);
		if (!ret) {
			nr_slots++;
			host->buf_size = host->slot[0]->mmc->max_req_size;
		}
	}
	if (pdata->slot[1].bus_width) {
		ret = atmci_init_slot(host, &pdata->slot[1],
				1, ATMCI_SDCSEL_SLOT_B, ATMCI_SDIOIRQB);
		if (!ret) {
			nr_slots++;
			if (host->slot[1]->mmc->max_req_size > host->buf_size)
				host->buf_size =
					host->slot[1]->mmc->max_req_size;
		}
	}

	if (!nr_slots) {
		dev_err(&pdev->dev, "init failed: no slot defined\n");
		goto err_init_slot;
	}

	if (!host->caps.has_rwproof) {
		host->buffer = dma_alloc_coherent(&pdev->dev, host->buf_size,
		                                  &host->buf_phys_addr,
						  GFP_KERNEL);
		if (!host->buffer) {
			ret = -ENOMEM;
			dev_err(&pdev->dev, "buffer allocation failed\n");
			goto err_dma_alloc;
		}
	}

	dev_info(&pdev->dev,
			"Atmel MCI controller at 0x%08lx irq %d, %u slots\n",
			host->mapbase, irq, nr_slots);

	pm_runtime_mark_last_busy(&host->pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

err_dma_alloc:
	for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
		if (host->slot[i])
			atmci_cleanup_slot(host->slot[i], i);
	}
err_init_slot:
	clk_disable_unprepare(host->mck);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	del_timer_sync(&host->timer);
	if (!IS_ERR(host->dma.chan))
		dma_release_channel(host->dma.chan);
err_dma_probe_defer:
	free_irq(irq, host);
	return ret;
}

static int atmci_remove(struct platform_device *pdev)
{
	struct atmel_mci	*host = platform_get_drvdata(pdev);
	unsigned int		i;

	pm_runtime_get_sync(&pdev->dev);

	if (host->buffer)
		dma_free_coherent(&pdev->dev, host->buf_size,
		                  host->buffer, host->buf_phys_addr);

	for (i = 0; i < ATMCI_MAX_NR_SLOTS; i++) {
		if (host->slot[i])
			atmci_cleanup_slot(host->slot[i], i);
	}

	atmci_writel(host, ATMCI_IDR, ~0UL);
	atmci_writel(host, ATMCI_CR, ATMCI_CR_MCIDIS);
	atmci_readl(host, ATMCI_SR);

	del_timer_sync(&host->timer);
	if (!IS_ERR(host->dma.chan))
		dma_release_channel(host->dma.chan);

	free_irq(platform_get_irq(pdev, 0), host);

	clk_disable_unprepare(host->mck);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int atmci_runtime_suspend(struct device *dev)
{
	struct atmel_mci *host = dev_get_drvdata(dev);

	clk_disable_unprepare(host->mck);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int atmci_runtime_resume(struct device *dev)
{
	struct atmel_mci *host = dev_get_drvdata(dev);

	pinctrl_select_default_state(dev);

	return clk_prepare_enable(host->mck);
}
#endif

static const struct dev_pm_ops atmci_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(atmci_runtime_suspend, atmci_runtime_resume, NULL)
};

static struct platform_driver atmci_driver = {
	.probe		= atmci_probe,
	.remove		= atmci_remove,
	.driver		= {
		.name		= "atmel_mci",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= of_match_ptr(atmci_dt_ids),
		.pm		= &atmci_dev_pm_ops,
	},
};
module_platform_driver(atmci_driver);

MODULE_DESCRIPTION("Atmel Multimedia Card Interface driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_LICENSE("GPL v2");
