/*
 * linux/drivers/mmc/host/tmio_mmc.h
 *
 * Copyright (C) 2016 Sang Engineering, Wolfram Sang
 * Copyright (C) 2015-16 Renesas Electronics Corporation
 * Copyright (C) 2007 Ian Molton
 * Copyright (C) 2004 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the MMC / SD / SDIO cell found in:
 *
 * TC6393XB TC6391XB TC6387XB T7L66XB ASIC3
 */

#ifndef TMIO_MMC_H
#define TMIO_MMC_H

#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>

#define CTL_SD_CMD 0x00
#define CTL_ARG_REG 0x04
#define CTL_STOP_INTERNAL_ACTION 0x08
#define CTL_XFER_BLK_COUNT 0xa
#define CTL_RESPONSE 0x0c
/* driver merges STATUS and following STATUS2 */
#define CTL_STATUS 0x1c
/* driver merges IRQ_MASK and following IRQ_MASK2 */
#define CTL_IRQ_MASK 0x20
#define CTL_SD_CARD_CLK_CTL 0x24
#define CTL_SD_XFER_LEN 0x26
#define CTL_SD_MEM_CARD_OPT 0x28
#define CTL_SD_ERROR_DETAIL_STATUS 0x2c
#define CTL_SD_DATA_PORT 0x30
#define CTL_TRANSACTION_CTL 0x34
#define CTL_SDIO_STATUS 0x36
#define CTL_SDIO_IRQ_MASK 0x38
#define CTL_DMA_ENABLE 0xd8
#define CTL_RESET_SD 0xe0
#define CTL_VERSION 0xe2
#define CTL_SDIO_REGS 0x100
#define CTL_CLK_AND_WAIT_CTL 0x138
#define CTL_RESET_SDIO 0x1e0

/* Definitions for values the CTRL_STATUS register can take. */
#define TMIO_STAT_CMDRESPEND    BIT(0)
#define TMIO_STAT_DATAEND       BIT(2)
#define TMIO_STAT_CARD_REMOVE   BIT(3)
#define TMIO_STAT_CARD_INSERT   BIT(4)
#define TMIO_STAT_SIGSTATE      BIT(5)
#define TMIO_STAT_WRPROTECT     BIT(7)
#define TMIO_STAT_CARD_REMOVE_A BIT(8)
#define TMIO_STAT_CARD_INSERT_A BIT(9)
#define TMIO_STAT_SIGSTATE_A    BIT(10)

/* These belong technically to CTRL_STATUS2, but the driver merges them */
#define TMIO_STAT_CMD_IDX_ERR   BIT(16)
#define TMIO_STAT_CRCFAIL       BIT(17)
#define TMIO_STAT_STOPBIT_ERR   BIT(18)
#define TMIO_STAT_DATATIMEOUT   BIT(19)
#define TMIO_STAT_RXOVERFLOW    BIT(20)
#define TMIO_STAT_TXUNDERRUN    BIT(21)
#define TMIO_STAT_CMDTIMEOUT    BIT(22)
#define TMIO_STAT_DAT0		BIT(23)	/* only known on R-Car so far */
#define TMIO_STAT_RXRDY         BIT(24)
#define TMIO_STAT_TXRQ          BIT(25)
#define TMIO_STAT_ILL_FUNC      BIT(29) /* only when !TMIO_MMC_HAS_IDLE_WAIT */
#define TMIO_STAT_SCLKDIVEN     BIT(29) /* only when TMIO_MMC_HAS_IDLE_WAIT */
#define TMIO_STAT_CMD_BUSY      BIT(30)
#define TMIO_STAT_ILL_ACCESS    BIT(31)

#define	CLK_CTL_DIV_MASK	0xff
#define	CLK_CTL_SCLKEN		BIT(8)

#define TMIO_BBS		512		/* Boot block size */

/* Definitions for values the CTRL_SDIO_STATUS register can take. */
#define TMIO_SDIO_STAT_IOIRQ	0x0001
#define TMIO_SDIO_STAT_EXPUB52	0x4000
#define TMIO_SDIO_STAT_EXWT	0x8000
#define TMIO_SDIO_MASK_ALL	0xc007

/* Define some IRQ masks */
/* This is the mask used at reset by the chip */
#define TMIO_MASK_ALL           0x837f031d
#define TMIO_MASK_READOP  (TMIO_STAT_RXRDY | TMIO_STAT_DATAEND)
#define TMIO_MASK_WRITEOP (TMIO_STAT_TXRQ | TMIO_STAT_DATAEND)
#define TMIO_MASK_CMD     (TMIO_STAT_CMDRESPEND | TMIO_STAT_CMDTIMEOUT | \
		TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_IRQ     (TMIO_MASK_READOP | TMIO_MASK_WRITEOP | TMIO_MASK_CMD)

struct tmio_mmc_data;
struct tmio_mmc_host;

struct tmio_mmc_dma {
	enum dma_slave_buswidth dma_buswidth;
	bool (*filter)(struct dma_chan *chan, void *arg);
	void (*enable)(struct tmio_mmc_host *host, bool enable);
};

struct tmio_mmc_host {
	void __iomem *ctl;
	struct mmc_command      *cmd;
	struct mmc_request      *mrq;
	struct mmc_data         *data;
	struct mmc_host         *mmc;

	/* Callbacks for clock / power control */
	void (*set_pwr)(struct platform_device *host, int state);
	void (*set_clk_div)(struct platform_device *host, int state);

	/* pio related stuff */
	struct scatterlist      *sg_ptr;
	struct scatterlist      *sg_orig;
	unsigned int            sg_len;
	unsigned int            sg_off;
	unsigned long		bus_shift;

	struct platform_device *pdev;
	struct tmio_mmc_data *pdata;
	struct tmio_mmc_dma	*dma;

	/* DMA support */
	bool			force_pio;
	struct dma_chan		*chan_rx;
	struct dma_chan		*chan_tx;
	struct tasklet_struct	dma_complete;
	struct tasklet_struct	dma_issue;
	struct scatterlist	bounce_sg;
	u8			*bounce_buf;

	/* Track lost interrupts */
	struct delayed_work	delayed_reset_work;
	struct work_struct	done;

	/* Cache */
	u32			sdcard_irq_mask;
	u32			sdio_irq_mask;
	unsigned int		clk_cache;

	spinlock_t		lock;		/* protect host private data */
	unsigned long		last_req_ts;
	struct mutex		ios_lock;	/* protect set_ios() context */
	bool			native_hotplug;
	bool			sdio_irq_enabled;

	int (*write16_hook)(struct tmio_mmc_host *host, int addr);
	int (*clk_enable)(struct tmio_mmc_host *host);
	unsigned int (*clk_update)(struct tmio_mmc_host *host,
				   unsigned int new_clock);
	void (*clk_disable)(struct tmio_mmc_host *host);
	int (*multi_io_quirk)(struct mmc_card *card,
			      unsigned int direction, int blk_size);
	int (*start_signal_voltage_switch)(struct mmc_host *mmc,
					   struct mmc_ios *ios);
};

struct tmio_mmc_host *tmio_mmc_host_alloc(struct platform_device *pdev);
void tmio_mmc_host_free(struct tmio_mmc_host *host);
int tmio_mmc_host_probe(struct tmio_mmc_host *host,
			struct tmio_mmc_data *pdata);
void tmio_mmc_host_remove(struct tmio_mmc_host *host);
void tmio_mmc_do_data_irq(struct tmio_mmc_host *host);

void tmio_mmc_enable_mmc_irqs(struct tmio_mmc_host *host, u32 i);
void tmio_mmc_disable_mmc_irqs(struct tmio_mmc_host *host, u32 i);
irqreturn_t tmio_mmc_irq(int irq, void *devid);

static inline char *tmio_mmc_kmap_atomic(struct scatterlist *sg,
					 unsigned long *flags)
{
	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg)) + sg->offset;
}

static inline void tmio_mmc_kunmap_atomic(struct scatterlist *sg,
					  unsigned long *flags, void *virt)
{
	kunmap_atomic(virt - sg->offset);
	local_irq_restore(*flags);
}

#if defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
void tmio_mmc_start_dma(struct tmio_mmc_host *host, struct mmc_data *data);
void tmio_mmc_enable_dma(struct tmio_mmc_host *host, bool enable);
void tmio_mmc_request_dma(struct tmio_mmc_host *host, struct tmio_mmc_data *pdata);
void tmio_mmc_release_dma(struct tmio_mmc_host *host);
void tmio_mmc_abort_dma(struct tmio_mmc_host *host);
#else
static inline void tmio_mmc_start_dma(struct tmio_mmc_host *host,
			       struct mmc_data *data)
{
}

static inline void tmio_mmc_enable_dma(struct tmio_mmc_host *host, bool enable)
{
}

static inline void tmio_mmc_request_dma(struct tmio_mmc_host *host,
				 struct tmio_mmc_data *pdata)
{
	host->chan_tx = NULL;
	host->chan_rx = NULL;
}

static inline void tmio_mmc_release_dma(struct tmio_mmc_host *host)
{
}

static inline void tmio_mmc_abort_dma(struct tmio_mmc_host *host)
{
}
#endif

#ifdef CONFIG_PM
int tmio_mmc_host_runtime_suspend(struct device *dev);
int tmio_mmc_host_runtime_resume(struct device *dev);
#endif

static inline u16 sd_ctrl_read16(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift));
}

static inline void sd_ctrl_read16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	readsw(host->ctl + (addr << host->bus_shift), buf, count);
}

static inline u32 sd_ctrl_read16_and_16_as_32(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift)) |
	       readw(host->ctl + ((addr + 2) << host->bus_shift)) << 16;
}

static inline void sd_ctrl_write16(struct tmio_mmc_host *host, int addr, u16 val)
{
	/* If there is a hook and it returns non-zero then there
	 * is an error and the write should be skipped
	 */
	if (host->write16_hook && host->write16_hook(host, addr))
		return;
	writew(val, host->ctl + (addr << host->bus_shift));
}

static inline void sd_ctrl_write16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	writesw(host->ctl + (addr << host->bus_shift), buf, count);
}

static inline void sd_ctrl_write32_as_16_and_16(struct tmio_mmc_host *host, int addr, u32 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
	writew(val >> 16, host->ctl + ((addr + 2) << host->bus_shift));
}

#endif
