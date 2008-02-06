/*
 * drivers/spi/spi_imx.c
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 *
 * Initial version inspired by:
 *	linux-2.6.17-rc3-mm1/drivers/spi/pxa2xx_spi.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/delay.h>

#include <asm/arch/hardware.h>
#include <asm/arch/imx-dma.h>
#include <asm/arch/spi_imx.h>

/*-------------------------------------------------------------------------*/
/* SPI Registers offsets from peripheral base address */
#define SPI_RXDATA		(0x00)
#define SPI_TXDATA		(0x04)
#define SPI_CONTROL		(0x08)
#define SPI_INT_STATUS		(0x0C)
#define SPI_TEST		(0x10)
#define SPI_PERIOD		(0x14)
#define SPI_DMA			(0x18)
#define SPI_RESET		(0x1C)

/* SPI Control Register Bit Fields & Masks */
#define SPI_CONTROL_BITCOUNT_MASK	(0xF)		/* Bit Count Mask */
#define SPI_CONTROL_BITCOUNT(n)		(((n) - 1) & SPI_CONTROL_BITCOUNT_MASK)
#define SPI_CONTROL_POL			(0x1 << 4)      /* Clock Polarity Mask */
#define SPI_CONTROL_POL_ACT_HIGH	(0x0 << 4)      /* Active high pol. (0=idle) */
#define SPI_CONTROL_POL_ACT_LOW		(0x1 << 4)      /* Active low pol. (1=idle) */
#define SPI_CONTROL_PHA			(0x1 << 5)      /* Clock Phase Mask */
#define SPI_CONTROL_PHA_0		(0x0 << 5)      /* Clock Phase 0 */
#define SPI_CONTROL_PHA_1		(0x1 << 5)      /* Clock Phase 1 */
#define SPI_CONTROL_SSCTL		(0x1 << 6)      /* /SS Waveform Select Mask */
#define SPI_CONTROL_SSCTL_0		(0x0 << 6)      /* Master: /SS stays low between SPI burst
							   Slave: RXFIFO advanced by BIT_COUNT */
#define SPI_CONTROL_SSCTL_1		(0x1 << 6)      /* Master: /SS insert pulse between SPI burst
							   Slave: RXFIFO advanced by /SS rising edge */
#define SPI_CONTROL_SSPOL		(0x1 << 7)      /* /SS Polarity Select Mask */
#define SPI_CONTROL_SSPOL_ACT_LOW	(0x0 << 7)      /* /SS Active low */
#define SPI_CONTROL_SSPOL_ACT_HIGH	(0x1 << 7)      /* /SS Active high */
#define SPI_CONTROL_XCH			(0x1 << 8)      /* Exchange */
#define SPI_CONTROL_SPIEN		(0x1 << 9)      /* SPI Module Enable */
#define SPI_CONTROL_MODE		(0x1 << 10)     /* SPI Mode Select Mask */
#define SPI_CONTROL_MODE_SLAVE		(0x0 << 10)     /* SPI Mode Slave */
#define SPI_CONTROL_MODE_MASTER		(0x1 << 10)     /* SPI Mode Master */
#define SPI_CONTROL_DRCTL		(0x3 << 11)     /* /SPI_RDY Control Mask */
#define SPI_CONTROL_DRCTL_0		(0x0 << 11)     /* Ignore /SPI_RDY */
#define SPI_CONTROL_DRCTL_1		(0x1 << 11)     /* /SPI_RDY falling edge triggers input */
#define SPI_CONTROL_DRCTL_2		(0x2 << 11)     /* /SPI_RDY active low level triggers input */
#define SPI_CONTROL_DATARATE		(0x7 << 13)     /* Data Rate Mask */
#define SPI_PERCLK2_DIV_MIN		(0)		/* PERCLK2:4 */
#define SPI_PERCLK2_DIV_MAX		(7)		/* PERCLK2:512 */
#define SPI_CONTROL_DATARATE_MIN	(SPI_PERCLK2_DIV_MAX << 13)
#define SPI_CONTROL_DATARATE_MAX	(SPI_PERCLK2_DIV_MIN << 13)
#define SPI_CONTROL_DATARATE_BAD	(SPI_CONTROL_DATARATE_MIN + 1)

/* SPI Interrupt/Status Register Bit Fields & Masks */
#define SPI_STATUS_TE	(0x1 << 0)	/* TXFIFO Empty Status */
#define SPI_STATUS_TH	(0x1 << 1)      /* TXFIFO Half Status */
#define SPI_STATUS_TF	(0x1 << 2)      /* TXFIFO Full Status */
#define SPI_STATUS_RR	(0x1 << 3)      /* RXFIFO Data Ready Status */
#define SPI_STATUS_RH	(0x1 << 4)      /* RXFIFO Half Status */
#define SPI_STATUS_RF	(0x1 << 5)      /* RXFIFO Full Status */
#define SPI_STATUS_RO	(0x1 << 6)      /* RXFIFO Overflow */
#define SPI_STATUS_BO	(0x1 << 7)      /* Bit Count Overflow */
#define SPI_STATUS	(0xFF)		/* SPI Status Mask */
#define SPI_INTEN_TE	(0x1 << 8)      /* TXFIFO Empty Interrupt Enable */
#define SPI_INTEN_TH	(0x1 << 9)      /* TXFIFO Half Interrupt Enable */
#define SPI_INTEN_TF	(0x1 << 10)     /* TXFIFO Full Interrupt Enable */
#define SPI_INTEN_RE	(0x1 << 11)     /* RXFIFO Data Ready Interrupt Enable */
#define SPI_INTEN_RH	(0x1 << 12)     /* RXFIFO Half Interrupt Enable */
#define SPI_INTEN_RF	(0x1 << 13)     /* RXFIFO Full Interrupt Enable */
#define SPI_INTEN_RO	(0x1 << 14)     /* RXFIFO Overflow Interrupt Enable */
#define SPI_INTEN_BO	(0x1 << 15)     /* Bit Count Overflow Interrupt Enable */
#define SPI_INTEN	(0xFF << 8)	/* SPI Interrupt Enable Mask */

/* SPI Test Register Bit Fields & Masks */
#define SPI_TEST_TXCNT		(0xF << 0)	/* TXFIFO Counter */
#define SPI_TEST_RXCNT_LSB	(4)		/* RXFIFO Counter LSB */
#define SPI_TEST_RXCNT		(0xF << 4)	/* RXFIFO Counter */
#define SPI_TEST_SSTATUS	(0xF << 8)	/* State Machine Status */
#define SPI_TEST_LBC		(0x1 << 14)	/* Loop Back Control */

/* SPI Period Register Bit Fields & Masks */
#define SPI_PERIOD_WAIT		(0x7FFF << 0)	/* Wait Between Transactions */
#define SPI_PERIOD_MAX_WAIT	(0x7FFF)	/* Max Wait Between
							Transactions */
#define SPI_PERIOD_CSRC		(0x1 << 15)	/* Period Clock Source Mask */
#define SPI_PERIOD_CSRC_BCLK	(0x0 << 15)	/* Period Clock Source is
							Bit Clock */
#define SPI_PERIOD_CSRC_32768	(0x1 << 15)	/* Period Clock Source is
							32.768 KHz Clock */

/* SPI DMA Register Bit Fields & Masks */
#define SPI_DMA_RHDMA	(0x1 << 4)	/* RXFIFO Half Status */
#define SPI_DMA_RFDMA	(0x1 << 5)      /* RXFIFO Full Status */
#define SPI_DMA_TEDMA	(0x1 << 6)      /* TXFIFO Empty Status */
#define SPI_DMA_THDMA	(0x1 << 7)      /* TXFIFO Half Status */
#define SPI_DMA_RHDEN	(0x1 << 12)	/* RXFIFO Half DMA Request Enable */
#define SPI_DMA_RFDEN	(0x1 << 13)     /* RXFIFO Full DMA Request Enable */
#define SPI_DMA_TEDEN	(0x1 << 14)     /* TXFIFO Empty DMA Request Enable */
#define SPI_DMA_THDEN	(0x1 << 15)     /* TXFIFO Half DMA Request Enable */

/* SPI Soft Reset Register Bit Fields & Masks */
#define SPI_RESET_START	(0x1)		/* Start */

/* Default SPI configuration values */
#define SPI_DEFAULT_CONTROL		\
(					\
	SPI_CONTROL_BITCOUNT(16) | 	\
	SPI_CONTROL_POL_ACT_HIGH |	\
	SPI_CONTROL_PHA_0 |		\
	SPI_CONTROL_SPIEN |		\
	SPI_CONTROL_SSCTL_1 |		\
	SPI_CONTROL_MODE_MASTER |	\
	SPI_CONTROL_DRCTL_0 |		\
	SPI_CONTROL_DATARATE_MIN	\
)
#define SPI_DEFAULT_ENABLE_LOOPBACK	(0)
#define SPI_DEFAULT_ENABLE_DMA		(0)
#define SPI_DEFAULT_PERIOD_WAIT		(8)
/*-------------------------------------------------------------------------*/


/*-------------------------------------------------------------------------*/
/* TX/RX SPI FIFO size */
#define SPI_FIFO_DEPTH			(8)
#define SPI_FIFO_BYTE_WIDTH		(2)
#define SPI_FIFO_OVERFLOW_MARGIN	(2)

/* DMA burst length for half full/empty request trigger */
#define SPI_DMA_BLR			(SPI_FIFO_DEPTH * SPI_FIFO_BYTE_WIDTH / 2)

/* Dummy char output to achieve reads.
   Choosing something different from all zeroes may help pattern recogition
   for oscilloscope analysis, but may break some drivers. */
#define SPI_DUMMY_u8			0
#define SPI_DUMMY_u16			((SPI_DUMMY_u8 << 8) | SPI_DUMMY_u8)
#define SPI_DUMMY_u32			((SPI_DUMMY_u16 << 16) | SPI_DUMMY_u16)

/**
 * Macro to change a u32 field:
 * @r : register to edit
 * @m : bit mask
 * @v : new value for the field correctly bit-alligned
*/
#define u32_EDIT(r, m, v)		r = (r & ~(m)) | (v)

/* Message state */
#define START_STATE			((void*)0)
#define RUNNING_STATE			((void*)1)
#define DONE_STATE			((void*)2)
#define ERROR_STATE			((void*)-1)

/* Queue state */
#define QUEUE_RUNNING			(0)
#define QUEUE_STOPPED			(1)

#define IS_DMA_ALIGNED(x) 		(((u32)(x) & 0x03) == 0)
/*-------------------------------------------------------------------------*/


/*-------------------------------------------------------------------------*/
/* Driver data structs */

/* Context */
struct driver_data {
	/* Driver model hookup */
	struct platform_device *pdev;

	/* SPI framework hookup */
	struct spi_master *master;

	/* IMX hookup */
	struct spi_imx_master *master_info;

	/* Memory resources and SPI regs virtual address */
	struct resource *ioarea;
	void __iomem *regs;

	/* SPI RX_DATA physical address */
	dma_addr_t rd_data_phys;

	/* Driver message queue */
	struct workqueue_struct	*workqueue;
	struct work_struct work;
	spinlock_t lock;
	struct list_head queue;
	int busy;
	int run;

	/* Message Transfer pump */
	struct tasklet_struct pump_transfers;

	/* Current message, transfer and state */
	struct spi_message *cur_msg;
	struct spi_transfer *cur_transfer;
	struct chip_data *cur_chip;

	/* Rd / Wr buffers pointers */
	size_t len;
	void *tx;
	void *tx_end;
	void *rx;
	void *rx_end;

	u8 rd_only;
	u8 n_bytes;
	int cs_change;

	/* Function pointers */
	irqreturn_t (*transfer_handler)(struct driver_data *drv_data);
	void (*cs_control)(u32 command);

	/* DMA setup */
	int rx_channel;
	int tx_channel;
	dma_addr_t rx_dma;
	dma_addr_t tx_dma;
	int rx_dma_needs_unmap;
	int tx_dma_needs_unmap;
	size_t tx_map_len;
	u32 dummy_dma_buf ____cacheline_aligned;
};

/* Runtime state */
struct chip_data {
	u32 control;
	u32 period;
	u32 test;

	u8 enable_dma:1;
	u8 bits_per_word;
	u8 n_bytes;
	u32 max_speed_hz;

	void (*cs_control)(u32 command);
};
/*-------------------------------------------------------------------------*/


static void pump_messages(struct work_struct *work);

static int flush(struct driver_data *drv_data)
{
	unsigned long limit = loops_per_jiffy << 1;
	void __iomem *regs = drv_data->regs;
	volatile u32 d;

	dev_dbg(&drv_data->pdev->dev, "flush\n");
	do {
		while (readl(regs + SPI_INT_STATUS) & SPI_STATUS_RR)
			d = readl(regs + SPI_RXDATA);
	} while ((readl(regs + SPI_CONTROL) & SPI_CONTROL_XCH) && limit--);

	return limit;
}

static void restore_state(struct driver_data *drv_data)
{
	void __iomem *regs = drv_data->regs;
	struct chip_data *chip = drv_data->cur_chip;

	/* Load chip registers */
	dev_dbg(&drv_data->pdev->dev,
		"restore_state\n"
		"    test    = 0x%08X\n"
		"    control = 0x%08X\n",
		chip->test,
		chip->control);
	writel(chip->test, regs + SPI_TEST);
	writel(chip->period, regs + SPI_PERIOD);
	writel(0, regs + SPI_INT_STATUS);
	writel(chip->control, regs + SPI_CONTROL);
}

static void null_cs_control(u32 command)
{
}

static inline u32 data_to_write(struct driver_data *drv_data)
{
	return ((u32)(drv_data->tx_end - drv_data->tx)) / drv_data->n_bytes;
}

static inline u32 data_to_read(struct driver_data *drv_data)
{
	return ((u32)(drv_data->rx_end - drv_data->rx)) / drv_data->n_bytes;
}

static int write(struct driver_data *drv_data)
{
	void __iomem *regs = drv_data->regs;
	void *tx = drv_data->tx;
	void *tx_end = drv_data->tx_end;
	u8 n_bytes = drv_data->n_bytes;
	u32 remaining_writes;
	u32 fifo_avail_space;
	u32 n;
	u16 d;

	/* Compute how many fifo writes to do */
	remaining_writes = (u32)(tx_end - tx) / n_bytes;
	fifo_avail_space = SPI_FIFO_DEPTH -
				(readl(regs + SPI_TEST) & SPI_TEST_TXCNT);
	if (drv_data->rx && (fifo_avail_space > SPI_FIFO_OVERFLOW_MARGIN))
		/* Fix misunderstood receive overflow */
		fifo_avail_space -= SPI_FIFO_OVERFLOW_MARGIN;
	n = min(remaining_writes, fifo_avail_space);

	dev_dbg(&drv_data->pdev->dev,
		"write type %s\n"
		"    remaining writes = %d\n"
		"    fifo avail space = %d\n"
		"    fifo writes      = %d\n",
		(n_bytes == 1) ? "u8" : "u16",
		remaining_writes,
		fifo_avail_space,
		n);

	if (n > 0) {
		/* Fill SPI TXFIFO */
		if (drv_data->rd_only) {
			tx += n * n_bytes;
			while (n--)
				writel(SPI_DUMMY_u16, regs + SPI_TXDATA);
		} else {
			if (n_bytes == 1) {
				while (n--) {
					d = *(u8*)tx;
					writel(d, regs + SPI_TXDATA);
					tx += 1;
				}
			} else {
				while (n--) {
					d = *(u16*)tx;
					writel(d, regs + SPI_TXDATA);
					tx += 2;
				}
			}
		}

		/* Trigger transfer */
		writel(readl(regs + SPI_CONTROL) | SPI_CONTROL_XCH,
			regs + SPI_CONTROL);

		/* Update tx pointer */
		drv_data->tx = tx;
	}

	return (tx >= tx_end);
}

static int read(struct driver_data *drv_data)
{
	void __iomem *regs = drv_data->regs;
	void *rx = drv_data->rx;
	void *rx_end = drv_data->rx_end;
	u8 n_bytes = drv_data->n_bytes;
	u32 remaining_reads;
	u32 fifo_rxcnt;
	u32 n;
	u16 d;

	/* Compute how many fifo reads to do */
	remaining_reads = (u32)(rx_end - rx) / n_bytes;
	fifo_rxcnt = (readl(regs + SPI_TEST) & SPI_TEST_RXCNT) >>
			SPI_TEST_RXCNT_LSB;
	n = min(remaining_reads, fifo_rxcnt);

	dev_dbg(&drv_data->pdev->dev,
		"read type %s\n"
		"    remaining reads = %d\n"
		"    fifo rx count   = %d\n"
		"    fifo reads      = %d\n",
		(n_bytes == 1) ? "u8" : "u16",
		remaining_reads,
		fifo_rxcnt,
		n);

	if (n > 0) {
		/* Read SPI RXFIFO */
		if (n_bytes == 1) {
			while (n--) {
				d = readl(regs + SPI_RXDATA);
				*((u8*)rx) = d;
				rx += 1;
			}
		} else {
			while (n--) {
				d = readl(regs + SPI_RXDATA);
				*((u16*)rx) = d;
				rx += 2;
			}
		}

		/* Update rx pointer */
		drv_data->rx = rx;
	}

	return (rx >= rx_end);
}

static void *next_transfer(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	struct spi_transfer *trans = drv_data->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		drv_data->cur_transfer =
			list_entry(trans->transfer_list.next,
					struct spi_transfer,
					transfer_list);
		return RUNNING_STATE;
	}

	return DONE_STATE;
}

static int map_dma_buffers(struct driver_data *drv_data)
{
	struct spi_message *msg;
	struct device *dev;
	void *buf;

	drv_data->rx_dma_needs_unmap = 0;
	drv_data->tx_dma_needs_unmap = 0;

	if (!drv_data->master_info->enable_dma ||
		!drv_data->cur_chip->enable_dma)
			return -1;

	msg = drv_data->cur_msg;
	dev = &msg->spi->dev;
	if (msg->is_dma_mapped) {
		if (drv_data->tx_dma)
			/* The caller provided at least dma and cpu virtual
			   address for write; pump_transfers() will consider the
			   transfer as write only if cpu rx virtual address is
			   NULL */
			return 0;

		if (drv_data->rx_dma) {
			/* The caller provided dma and cpu virtual address to
			   performe read only transfer -->
			   use drv_data->dummy_dma_buf for dummy writes to
			   achive reads */
			buf = &drv_data->dummy_dma_buf;
			drv_data->tx_map_len = sizeof(drv_data->dummy_dma_buf);
			drv_data->tx_dma = dma_map_single(dev,
							buf,
							drv_data->tx_map_len,
							DMA_TO_DEVICE);
			if (dma_mapping_error(drv_data->tx_dma))
				return -1;

			drv_data->tx_dma_needs_unmap = 1;

			/* Flags transfer as rd_only for pump_transfers() DMA
			   regs programming (should be redundant) */
			drv_data->tx = NULL;

			return 0;
		}
	}

	if (!IS_DMA_ALIGNED(drv_data->rx) || !IS_DMA_ALIGNED(drv_data->tx))
		return -1;

	/* NULL rx means write-only transfer and no map needed
	   since rx DMA will not be used */
	if (drv_data->rx) {
		buf = drv_data->rx;
		drv_data->rx_dma = dma_map_single(
					dev,
					buf,
					drv_data->len,
					DMA_FROM_DEVICE);
		if (dma_mapping_error(drv_data->rx_dma))
			return -1;
		drv_data->rx_dma_needs_unmap = 1;
	}

	if (drv_data->tx == NULL) {
		/* Read only message --> use drv_data->dummy_dma_buf for dummy
		   writes to achive reads */
		buf = &drv_data->dummy_dma_buf;
		drv_data->tx_map_len = sizeof(drv_data->dummy_dma_buf);
	} else {
		buf = drv_data->tx;
		drv_data->tx_map_len = drv_data->len;
	}
	drv_data->tx_dma = dma_map_single(dev,
					buf,
					drv_data->tx_map_len,
					DMA_TO_DEVICE);
	if (dma_mapping_error(drv_data->tx_dma)) {
		if (drv_data->rx_dma) {
			dma_unmap_single(dev,
					drv_data->rx_dma,
					drv_data->len,
					DMA_FROM_DEVICE);
			drv_data->rx_dma_needs_unmap = 0;
		}
		return -1;
	}
	drv_data->tx_dma_needs_unmap = 1;

	return 0;
}

static void unmap_dma_buffers(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	struct device *dev = &msg->spi->dev;

	if (drv_data->rx_dma_needs_unmap) {
		dma_unmap_single(dev,
				drv_data->rx_dma,
				drv_data->len,
				DMA_FROM_DEVICE);
		drv_data->rx_dma_needs_unmap = 0;
	}
	if (drv_data->tx_dma_needs_unmap) {
		dma_unmap_single(dev,
				drv_data->tx_dma,
				drv_data->tx_map_len,
				DMA_TO_DEVICE);
		drv_data->tx_dma_needs_unmap = 0;
	}
}

/* Caller already set message->status (dma is already blocked) */
static void giveback(struct spi_message *message, struct driver_data *drv_data)
{
	void __iomem *regs = drv_data->regs;

	/* Bring SPI to sleep; restore_state() and pump_transfer()
	   will do new setup */
	writel(0, regs + SPI_INT_STATUS);
	writel(0, regs + SPI_DMA);

	drv_data->cs_control(SPI_CS_DEASSERT);

	message->state = NULL;
	if (message->complete)
		message->complete(message->context);

	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	queue_work(drv_data->workqueue, &drv_data->work);
}

static void dma_err_handler(int channel, void *data, int errcode)
{
	struct driver_data *drv_data = data;
	struct spi_message *msg = drv_data->cur_msg;

	dev_dbg(&drv_data->pdev->dev, "dma_err_handler\n");

	/* Disable both rx and tx dma channels */
	imx_dma_disable(drv_data->rx_channel);
	imx_dma_disable(drv_data->tx_channel);

	if (flush(drv_data) == 0)
		dev_err(&drv_data->pdev->dev,
				"dma_err_handler - flush failed\n");

	unmap_dma_buffers(drv_data);

	msg->state = ERROR_STATE;
	tasklet_schedule(&drv_data->pump_transfers);
}

static void dma_tx_handler(int channel, void *data)
{
	struct driver_data *drv_data = data;

	dev_dbg(&drv_data->pdev->dev, "dma_tx_handler\n");

	imx_dma_disable(channel);

	/* Now waits for TX FIFO empty */
	writel(readl(drv_data->regs + SPI_INT_STATUS) | SPI_INTEN_TE,
			drv_data->regs + SPI_INT_STATUS);
}

static irqreturn_t dma_transfer(struct driver_data *drv_data)
{
	u32 status;
	struct spi_message *msg = drv_data->cur_msg;
	void __iomem *regs = drv_data->regs;
	unsigned long limit;

	status = readl(regs + SPI_INT_STATUS);

	if ((status & SPI_INTEN_RO) && (status & SPI_STATUS_RO)) {
		writel(status & ~SPI_INTEN, regs + SPI_INT_STATUS);

		imx_dma_disable(drv_data->rx_channel);
		unmap_dma_buffers(drv_data);

		if (flush(drv_data) == 0)
			dev_err(&drv_data->pdev->dev,
				"dma_transfer - flush failed\n");

		dev_warn(&drv_data->pdev->dev,
				"dma_transfer - fifo overun\n");

		msg->state = ERROR_STATE;
		tasklet_schedule(&drv_data->pump_transfers);

		return IRQ_HANDLED;
	}

	if (status & SPI_STATUS_TE) {
		writel(status & ~SPI_INTEN_TE, regs + SPI_INT_STATUS);

		if (drv_data->rx) {
			/* Wait end of transfer before read trailing data */
			limit = loops_per_jiffy << 1;
			while ((readl(regs + SPI_CONTROL) & SPI_CONTROL_XCH) &&
					limit--);

			if (limit == 0)
				dev_err(&drv_data->pdev->dev,
					"dma_transfer - end of tx failed\n");
			else
				dev_dbg(&drv_data->pdev->dev,
					"dma_transfer - end of tx\n");

			imx_dma_disable(drv_data->rx_channel);
			unmap_dma_buffers(drv_data);

			/* Calculate number of trailing data and read them */
			dev_dbg(&drv_data->pdev->dev,
				"dma_transfer - test = 0x%08X\n",
				readl(regs + SPI_TEST));
			drv_data->rx = drv_data->rx_end -
					((readl(regs + SPI_TEST) &
					SPI_TEST_RXCNT) >>
					SPI_TEST_RXCNT_LSB)*drv_data->n_bytes;
			read(drv_data);
		} else {
			/* Write only transfer */
			unmap_dma_buffers(drv_data);

			if (flush(drv_data) == 0)
				dev_err(&drv_data->pdev->dev,
					"dma_transfer - flush failed\n");
		}

		/* End of transfer, update total byte transfered */
		msg->actual_length += drv_data->len;

		/* Release chip select if requested, transfer delays are
		   handled in pump_transfers() */
		if (drv_data->cs_change)
			drv_data->cs_control(SPI_CS_DEASSERT);

		/* Move to next transfer */
		msg->state = next_transfer(drv_data);

		/* Schedule transfer tasklet */
		tasklet_schedule(&drv_data->pump_transfers);

		return IRQ_HANDLED;
	}

	/* Opps problem detected */
	return IRQ_NONE;
}

static irqreturn_t interrupt_wronly_transfer(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	void __iomem *regs = drv_data->regs;
	u32 status;
	irqreturn_t handled = IRQ_NONE;

	status = readl(regs + SPI_INT_STATUS);

	while (status & SPI_STATUS_TH) {
		dev_dbg(&drv_data->pdev->dev,
			"interrupt_wronly_transfer - status = 0x%08X\n", status);

		/* Pump data */
		if (write(drv_data)) {
			writel(readl(regs + SPI_INT_STATUS) & ~SPI_INTEN,
				regs + SPI_INT_STATUS);

			dev_dbg(&drv_data->pdev->dev,
				"interrupt_wronly_transfer - end of tx\n");

			if (flush(drv_data) == 0)
				dev_err(&drv_data->pdev->dev,
					"interrupt_wronly_transfer - "
					"flush failed\n");

			/* End of transfer, update total byte transfered */
			msg->actual_length += drv_data->len;

			/* Release chip select if requested, transfer delays are
			   handled in pump_transfers */
			if (drv_data->cs_change)
				drv_data->cs_control(SPI_CS_DEASSERT);

			/* Move to next transfer */
			msg->state = next_transfer(drv_data);

			/* Schedule transfer tasklet */
			tasklet_schedule(&drv_data->pump_transfers);

			return IRQ_HANDLED;
		}

		status = readl(regs + SPI_INT_STATUS);

		/* We did something */
		handled = IRQ_HANDLED;
	}

	return handled;
}

static irqreturn_t interrupt_transfer(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	void __iomem *regs = drv_data->regs;
	u32 status;
	irqreturn_t handled = IRQ_NONE;
	unsigned long limit;

	status = readl(regs + SPI_INT_STATUS);

	while (status & (SPI_STATUS_TH | SPI_STATUS_RO)) {
		dev_dbg(&drv_data->pdev->dev,
			"interrupt_transfer - status = 0x%08X\n", status);

		if (status & SPI_STATUS_RO) {
			writel(readl(regs + SPI_INT_STATUS) & ~SPI_INTEN,
				regs + SPI_INT_STATUS);

			dev_warn(&drv_data->pdev->dev,
				"interrupt_transfer - fifo overun\n"
				"    data not yet written = %d\n"
				"    data not yet read    = %d\n",
				data_to_write(drv_data),
				data_to_read(drv_data));

			if (flush(drv_data) == 0)
				dev_err(&drv_data->pdev->dev,
					"interrupt_transfer - flush failed\n");

			msg->state = ERROR_STATE;
			tasklet_schedule(&drv_data->pump_transfers);

			return IRQ_HANDLED;
		}

		/* Pump data */
		read(drv_data);
		if (write(drv_data)) {
			writel(readl(regs + SPI_INT_STATUS) & ~SPI_INTEN,
				regs + SPI_INT_STATUS);

			dev_dbg(&drv_data->pdev->dev,
				"interrupt_transfer - end of tx\n");

			/* Read trailing bytes */
			limit = loops_per_jiffy << 1;
			while ((read(drv_data) == 0) && limit--);

			if (limit == 0)
				dev_err(&drv_data->pdev->dev,
					"interrupt_transfer - "
					"trailing byte read failed\n");
			else
				dev_dbg(&drv_data->pdev->dev,
					"interrupt_transfer - end of rx\n");

			/* End of transfer, update total byte transfered */
			msg->actual_length += drv_data->len;

			/* Release chip select if requested, transfer delays are
			   handled in pump_transfers */
			if (drv_data->cs_change)
				drv_data->cs_control(SPI_CS_DEASSERT);

			/* Move to next transfer */
			msg->state = next_transfer(drv_data);

			/* Schedule transfer tasklet */
			tasklet_schedule(&drv_data->pump_transfers);

			return IRQ_HANDLED;
		}

		status = readl(regs + SPI_INT_STATUS);

		/* We did something */
		handled = IRQ_HANDLED;
	}

	return handled;
}

static irqreturn_t spi_int(int irq, void *dev_id)
{
	struct driver_data *drv_data = (struct driver_data *)dev_id;

	if (!drv_data->cur_msg) {
		dev_err(&drv_data->pdev->dev,
			"spi_int - bad message state\n");
		/* Never fail */
		return IRQ_HANDLED;
	}

	return drv_data->transfer_handler(drv_data);
}

static inline u32 spi_speed_hz(u32 data_rate)
{
	return imx_get_perclk2() / (4 << ((data_rate) >> 13));
}

static u32 spi_data_rate(u32 speed_hz)
{
	u32 div;
	u32 quantized_hz = imx_get_perclk2() >> 2;

	for (div = SPI_PERCLK2_DIV_MIN;
		div <= SPI_PERCLK2_DIV_MAX;
		div++, quantized_hz >>= 1) {
			if (quantized_hz <= speed_hz)
				/* Max available speed LEQ required speed */
				return div << 13;
	}
	return SPI_CONTROL_DATARATE_BAD;
}

static void pump_transfers(unsigned long data)
{
	struct driver_data *drv_data = (struct driver_data *)data;
	struct spi_message *message;
	struct spi_transfer *transfer, *previous;
	struct chip_data *chip;
	void __iomem *regs;
	u32 tmp, control;

	dev_dbg(&drv_data->pdev->dev, "pump_transfer\n");

	message = drv_data->cur_msg;

	/* Handle for abort */
	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		giveback(message, drv_data);
		return;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		giveback(message, drv_data);
		return;
	}

	chip = drv_data->cur_chip;

	/* Delay if requested at end of transfer*/
	transfer = drv_data->cur_transfer;
	if (message->state == RUNNING_STATE) {
		previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer,
					transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);
	} else {
		/* START_STATE */
		message->state = RUNNING_STATE;
		drv_data->cs_control = chip->cs_control;
	}

	transfer = drv_data->cur_transfer;
	drv_data->tx = (void *)transfer->tx_buf;
	drv_data->tx_end = drv_data->tx + transfer->len;
	drv_data->rx = transfer->rx_buf;
	drv_data->rx_end = drv_data->rx + transfer->len;
	drv_data->rx_dma = transfer->rx_dma;
	drv_data->tx_dma = transfer->tx_dma;
	drv_data->len = transfer->len;
	drv_data->cs_change = transfer->cs_change;
	drv_data->rd_only = (drv_data->tx == NULL);

	regs = drv_data->regs;
	control = readl(regs + SPI_CONTROL);

	/* Bits per word setup */
	tmp = transfer->bits_per_word;
	if (tmp == 0) {
		/* Use device setup */
		tmp = chip->bits_per_word;
		drv_data->n_bytes = chip->n_bytes;
	} else
		/* Use per-transfer setup */
		drv_data->n_bytes = (tmp <= 8) ? 1 : 2;
	u32_EDIT(control, SPI_CONTROL_BITCOUNT_MASK, tmp - 1);

	/* Speed setup (surely valid because already checked) */
	tmp = transfer->speed_hz;
	if (tmp == 0)
		tmp = chip->max_speed_hz;
	tmp = spi_data_rate(tmp);
	u32_EDIT(control, SPI_CONTROL_DATARATE, tmp);

	writel(control, regs + SPI_CONTROL);

	/* Assert device chip-select */
	drv_data->cs_control(SPI_CS_ASSERT);

	/* DMA cannot read/write SPI FIFOs other than 16 bits at a time; hence
	   if bits_per_word is less or equal 8 PIO transfers are performed.
	   Moreover DMA is convinient for transfer length bigger than FIFOs
	   byte size. */
	if ((drv_data->n_bytes == 2) &&
		(drv_data->len > SPI_FIFO_DEPTH*SPI_FIFO_BYTE_WIDTH) &&
		(map_dma_buffers(drv_data) == 0)) {
		dev_dbg(&drv_data->pdev->dev,
			"pump dma transfer\n"
			"    tx      = %p\n"
			"    tx_dma  = %08X\n"
			"    rx      = %p\n"
			"    rx_dma  = %08X\n"
			"    len     = %d\n",
			drv_data->tx,
			(unsigned int)drv_data->tx_dma,
			drv_data->rx,
			(unsigned int)drv_data->rx_dma,
			drv_data->len);

		/* Ensure we have the correct interrupt handler */
		drv_data->transfer_handler = dma_transfer;

		/* Trigger transfer */
		writel(readl(regs + SPI_CONTROL) | SPI_CONTROL_XCH,
			regs + SPI_CONTROL);

		/* Setup tx DMA */
		if (drv_data->tx)
			/* Linear source address */
			CCR(drv_data->tx_channel) =
				CCR_DMOD_FIFO |
				CCR_SMOD_LINEAR |
				CCR_SSIZ_32 | CCR_DSIZ_16 |
				CCR_REN;
		else
			/* Read only transfer -> fixed source address for
			   dummy write to achive read */
			CCR(drv_data->tx_channel) =
				CCR_DMOD_FIFO |
				CCR_SMOD_FIFO |
				CCR_SSIZ_32 | CCR_DSIZ_16 |
				CCR_REN;

		imx_dma_setup_single(
			drv_data->tx_channel,
			drv_data->tx_dma,
			drv_data->len,
			drv_data->rd_data_phys + 4,
			DMA_MODE_WRITE);

		if (drv_data->rx) {
			/* Setup rx DMA for linear destination address */
			CCR(drv_data->rx_channel) =
				CCR_DMOD_LINEAR |
				CCR_SMOD_FIFO |
				CCR_DSIZ_32 | CCR_SSIZ_16 |
				CCR_REN;
			imx_dma_setup_single(
				drv_data->rx_channel,
				drv_data->rx_dma,
				drv_data->len,
				drv_data->rd_data_phys,
				DMA_MODE_READ);
			imx_dma_enable(drv_data->rx_channel);

			/* Enable SPI interrupt */
			writel(SPI_INTEN_RO, regs + SPI_INT_STATUS);

			/* Set SPI to request DMA service on both
			   Rx and Tx half fifo watermark */
			writel(SPI_DMA_RHDEN | SPI_DMA_THDEN, regs + SPI_DMA);
		} else
			/* Write only access -> set SPI to request DMA
			   service on Tx half fifo watermark */
			writel(SPI_DMA_THDEN, regs + SPI_DMA);

		imx_dma_enable(drv_data->tx_channel);
	} else {
		dev_dbg(&drv_data->pdev->dev,
			"pump pio transfer\n"
			"    tx      = %p\n"
			"    rx      = %p\n"
			"    len     = %d\n",
			drv_data->tx,
			drv_data->rx,
			drv_data->len);

		/* Ensure we have the correct interrupt handler	*/
		if (drv_data->rx)
			drv_data->transfer_handler = interrupt_transfer;
		else
			drv_data->transfer_handler = interrupt_wronly_transfer;

		/* Enable SPI interrupt */
		if (drv_data->rx)
			writel(SPI_INTEN_TH | SPI_INTEN_RO,
				regs + SPI_INT_STATUS);
		else
			writel(SPI_INTEN_TH, regs + SPI_INT_STATUS);
	}
}

static void pump_messages(struct work_struct *work)
{
	struct driver_data *drv_data =
				container_of(work, struct driver_data, work);
	unsigned long flags;

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&drv_data->lock, flags);
	if (list_empty(&drv_data->queue) || drv_data->run == QUEUE_STOPPED) {
		drv_data->busy = 0;
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Make sure we are not already running a message */
	if (drv_data->cur_msg) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Extract head of queue */
	drv_data->cur_msg = list_entry(drv_data->queue.next,
					struct spi_message, queue);
	list_del_init(&drv_data->cur_msg->queue);
	drv_data->busy = 1;
	spin_unlock_irqrestore(&drv_data->lock, flags);

	/* Initial message state */
	drv_data->cur_msg->state = START_STATE;
	drv_data->cur_transfer = list_entry(drv_data->cur_msg->transfers.next,
						struct spi_transfer,
						transfer_list);

	/* Setup the SPI using the per chip configuration */
	drv_data->cur_chip = spi_get_ctldata(drv_data->cur_msg->spi);
	restore_state(drv_data);

	/* Mark as busy and launch transfers */
	tasklet_schedule(&drv_data->pump_transfers);
}

static int transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);
	u32 min_speed_hz, max_speed_hz, tmp;
	struct spi_transfer *trans;
	unsigned long flags;

	msg->actual_length = 0;

	/* Per transfer setup check */
	min_speed_hz = spi_speed_hz(SPI_CONTROL_DATARATE_MIN);
	max_speed_hz = spi->max_speed_hz;
	list_for_each_entry(trans, &msg->transfers, transfer_list) {
		tmp = trans->bits_per_word;
		if (tmp > 16) {
			dev_err(&drv_data->pdev->dev,
				"message rejected : "
				"invalid transfer bits_per_word (%d bits)\n",
				tmp);
			goto msg_rejected;
		}
		tmp = trans->speed_hz;
		if (tmp) {
			if (tmp < min_speed_hz) {
				dev_err(&drv_data->pdev->dev,
					"message rejected : "
					"device min speed (%d Hz) exceeds "
					"required transfer speed (%d Hz)\n",
					min_speed_hz,
					tmp);
				goto msg_rejected;
			} else if (tmp > max_speed_hz) {
				dev_err(&drv_data->pdev->dev,
					"message rejected : "
					"transfer speed (%d Hz) exceeds "
					"device max speed (%d Hz)\n",
					tmp,
					max_speed_hz);
				goto msg_rejected;
			}
		}
	}

	/* Message accepted */
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	spin_lock_irqsave(&drv_data->lock, flags);
	if (drv_data->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -ESHUTDOWN;
	}

	list_add_tail(&msg->queue, &drv_data->queue);
	if (drv_data->run == QUEUE_RUNNING && !drv_data->busy)
		queue_work(drv_data->workqueue, &drv_data->work);

	spin_unlock_irqrestore(&drv_data->lock, flags);
	return 0;

msg_rejected:
	/* Message rejected and not queued */
	msg->status = -EINVAL;
	msg->state = ERROR_STATE;
	if (msg->complete)
		msg->complete(msg->context);
	return -EINVAL;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)

/* On first setup bad values must free chip_data memory since will cause
   spi_new_device to fail. Bad value setup from protocol driver are simply not
   applied and notified to the calling driver. */
static int setup(struct spi_device *spi)
{
	struct spi_imx_chip *chip_info;
	struct chip_data *chip;
	int first_setup = 0;
	u32 tmp;
	int status = 0;

	if (spi->mode & ~MODEBITS) {
		dev_dbg(&spi->dev, "setup: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	/* Get controller data */
	chip_info = spi->controller_data;

	/* Get controller_state */
	chip = spi_get_ctldata(spi);
	if (chip == NULL) {
		first_setup = 1;

		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip) {
			dev_err(&spi->dev,
				"setup - cannot allocate controller state\n");
			return -ENOMEM;
		}
		chip->control = SPI_DEFAULT_CONTROL;

		if (chip_info == NULL) {
			/* spi_board_info.controller_data not is supplied */
			chip_info = kzalloc(sizeof(struct spi_imx_chip),
						GFP_KERNEL);
			if (!chip_info) {
				dev_err(&spi->dev,
					"setup - "
					"cannot allocate controller data\n");
				status = -ENOMEM;
				goto err_first_setup;
			}
			/* Set controller data default value */
			chip_info->enable_loopback =
						SPI_DEFAULT_ENABLE_LOOPBACK;
			chip_info->enable_dma = SPI_DEFAULT_ENABLE_DMA;
			chip_info->ins_ss_pulse = 1;
			chip_info->bclk_wait = SPI_DEFAULT_PERIOD_WAIT;
			chip_info->cs_control = null_cs_control;
		}
	}

	/* Now set controller state based on controller data */

	if (first_setup) {
		/* SPI loopback */
		if (chip_info->enable_loopback)
			chip->test = SPI_TEST_LBC;
		else
			chip->test = 0;

		/* SPI dma driven */
		chip->enable_dma = chip_info->enable_dma;

		/* SPI /SS pulse between spi burst */
		if (chip_info->ins_ss_pulse)
			u32_EDIT(chip->control,
				SPI_CONTROL_SSCTL, SPI_CONTROL_SSCTL_1);
		else
			u32_EDIT(chip->control,
				SPI_CONTROL_SSCTL, SPI_CONTROL_SSCTL_0);

		/* SPI bclk waits between each bits_per_word spi burst */
		if (chip_info->bclk_wait > SPI_PERIOD_MAX_WAIT) {
			dev_err(&spi->dev,
				"setup - "
				"bclk_wait exceeds max allowed (%d)\n",
				SPI_PERIOD_MAX_WAIT);
			goto err_first_setup;
		}
		chip->period = SPI_PERIOD_CSRC_BCLK |
				(chip_info->bclk_wait & SPI_PERIOD_WAIT);
	}

	/* SPI mode */
	tmp = spi->mode;
	if (tmp & SPI_CS_HIGH) {
		u32_EDIT(chip->control,
				SPI_CONTROL_SSPOL, SPI_CONTROL_SSPOL_ACT_HIGH);
	}
	switch (tmp & SPI_MODE_3) {
	case SPI_MODE_0:
		tmp = 0;
		break;
	case SPI_MODE_1:
		tmp = SPI_CONTROL_PHA_1;
		break;
	case SPI_MODE_2:
		tmp = SPI_CONTROL_POL_ACT_LOW;
		break;
	default:
		/* SPI_MODE_3 */
		tmp = SPI_CONTROL_PHA_1 | SPI_CONTROL_POL_ACT_LOW;
		break;
	}
	u32_EDIT(chip->control, SPI_CONTROL_POL | SPI_CONTROL_PHA, tmp);

	/* SPI word width */
	tmp = spi->bits_per_word;
	if (tmp == 0) {
		tmp = 8;
		spi->bits_per_word = 8;
	} else if (tmp > 16) {
		status = -EINVAL;
		dev_err(&spi->dev,
			"setup - "
			"invalid bits_per_word (%d)\n",
			tmp);
		if (first_setup)
			goto err_first_setup;
		else {
			/* Undo setup using chip as backup copy */
			tmp = chip->bits_per_word;
			spi->bits_per_word = tmp;
		}
	}
	chip->bits_per_word = tmp;
	u32_EDIT(chip->control, SPI_CONTROL_BITCOUNT_MASK, tmp - 1);
	chip->n_bytes = (tmp <= 8) ? 1 : 2;

	/* SPI datarate */
	tmp = spi_data_rate(spi->max_speed_hz);
	if (tmp == SPI_CONTROL_DATARATE_BAD) {
		status = -EINVAL;
		dev_err(&spi->dev,
			"setup - "
			"HW min speed (%d Hz) exceeds required "
			"max speed (%d Hz)\n",
			spi_speed_hz(SPI_CONTROL_DATARATE_MIN),
			spi->max_speed_hz);
		if (first_setup)
			goto err_first_setup;
		else
			/* Undo setup using chip as backup copy */
			spi->max_speed_hz = chip->max_speed_hz;
	} else {
		u32_EDIT(chip->control, SPI_CONTROL_DATARATE, tmp);
		/* Actual rounded max_speed_hz */
		tmp = spi_speed_hz(tmp);
		spi->max_speed_hz = tmp;
		chip->max_speed_hz = tmp;
	}

	/* SPI chip-select management */
	if (chip_info->cs_control)
		chip->cs_control = chip_info->cs_control;
	else
		chip->cs_control = null_cs_control;

	/* Save controller_state */
	spi_set_ctldata(spi, chip);

	/* Summary */
	dev_dbg(&spi->dev,
		"setup succeded\n"
		"    loopback enable   = %s\n"
		"    dma enable        = %s\n"
		"    insert /ss pulse  = %s\n"
		"    period wait       = %d\n"
		"    mode              = %d\n"
		"    bits per word     = %d\n"
		"    min speed         = %d Hz\n"
		"    rounded max speed = %d Hz\n",
		chip->test & SPI_TEST_LBC ? "Yes" : "No",
		chip->enable_dma ? "Yes" : "No",
		chip->control & SPI_CONTROL_SSCTL ? "Yes" : "No",
		chip->period & SPI_PERIOD_WAIT,
		spi->mode,
		spi->bits_per_word,
		spi_speed_hz(SPI_CONTROL_DATARATE_MIN),
		spi->max_speed_hz);
	return status;

err_first_setup:
	kfree(chip);
	return status;
}

static void cleanup(struct spi_device *spi)
{
	kfree(spi_get_ctldata(spi));
}

static int __init init_queue(struct driver_data *drv_data)
{
	INIT_LIST_HEAD(&drv_data->queue);
	spin_lock_init(&drv_data->lock);

	drv_data->run = QUEUE_STOPPED;
	drv_data->busy = 0;

	tasklet_init(&drv_data->pump_transfers,
			pump_transfers,	(unsigned long)drv_data);

	INIT_WORK(&drv_data->work, pump_messages);
	drv_data->workqueue = create_singlethread_workqueue(
					drv_data->master->dev.parent->bus_id);
	if (drv_data->workqueue == NULL)
		return -EBUSY;

	return 0;
}

static int start_queue(struct driver_data *drv_data)
{
	unsigned long flags;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (drv_data->run == QUEUE_RUNNING || drv_data->busy) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -EBUSY;
	}

	drv_data->run = QUEUE_RUNNING;
	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	spin_unlock_irqrestore(&drv_data->lock, flags);

	queue_work(drv_data->workqueue, &drv_data->work);

	return 0;
}

static int stop_queue(struct driver_data *drv_data)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&drv_data->lock, flags);

	/* This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the drv_data->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead */
	drv_data->run = QUEUE_STOPPED;
	while (!list_empty(&drv_data->queue) && drv_data->busy && limit--) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		msleep(10);
		spin_lock_irqsave(&drv_data->lock, flags);
	}

	if (!list_empty(&drv_data->queue) || drv_data->busy)
		status = -EBUSY;

	spin_unlock_irqrestore(&drv_data->lock, flags);

	return status;
}

static int destroy_queue(struct driver_data *drv_data)
{
	int status;

	status = stop_queue(drv_data);
	if (status != 0)
		return status;

	if (drv_data->workqueue)
		destroy_workqueue(drv_data->workqueue);

	return 0;
}

static int __init spi_imx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_imx_master *platform_info;
	struct spi_master *master;
	struct driver_data *drv_data = NULL;
	struct resource *res;
	int irq, status = 0;

	platform_info = dev->platform_data;
	if (platform_info == NULL) {
		dev_err(&pdev->dev, "probe - no platform data supplied\n");
		status = -ENODEV;
		goto err_no_pdata;
	}

	/* Allocate master with space for drv_data */
	master = spi_alloc_master(dev, sizeof(struct driver_data));
	if (!master) {
		dev_err(&pdev->dev, "probe - cannot alloc spi_master\n");
		status = -ENOMEM;
		goto err_no_mem;
	}
	drv_data = spi_master_get_devdata(master);
	drv_data->master = master;
	drv_data->master_info = platform_info;
	drv_data->pdev = pdev;

	master->bus_num = pdev->id;
	master->num_chipselect = platform_info->num_chipselect;
	master->cleanup = cleanup;
	master->setup = setup;
	master->transfer = transfer;

	drv_data->dummy_dma_buf = SPI_DUMMY_u32;

	/* Find and map resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "probe - MEM resources not defined\n");
		status = -ENODEV;
		goto err_no_iores;
	}
	drv_data->ioarea = request_mem_region(res->start,
						res->end - res->start + 1,
						pdev->name);
	if (drv_data->ioarea == NULL) {
		dev_err(&pdev->dev, "probe - cannot reserve region\n");
		status = -ENXIO;
		goto err_no_iores;
	}
	drv_data->regs = ioremap(res->start, res->end - res->start + 1);
	if (drv_data->regs == NULL) {
		dev_err(&pdev->dev, "probe - cannot map IO\n");
		status = -ENXIO;
		goto err_no_iomap;
	}
	drv_data->rd_data_phys = (dma_addr_t)res->start;

	/* Attach to IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "probe - IRQ resource not defined\n");
		status = -ENODEV;
		goto err_no_irqres;
	}
	status = request_irq(irq, spi_int, IRQF_DISABLED, dev->bus_id, drv_data);
	if (status < 0) {
		dev_err(&pdev->dev, "probe - cannot get IRQ (%d)\n", status);
		goto err_no_irqres;
	}

	/* Setup DMA if requested */
	drv_data->tx_channel = -1;
	drv_data->rx_channel = -1;
	if (platform_info->enable_dma) {
		/* Get rx DMA channel */
		status = imx_dma_request_by_prio(&drv_data->rx_channel,
			"spi_imx_rx", DMA_PRIO_HIGH);
		if (status < 0) {
			dev_err(dev,
				"probe - problem (%d) requesting rx channel\n",
				status);
			goto err_no_rxdma;
		} else
			imx_dma_setup_handlers(drv_data->rx_channel, NULL,
						dma_err_handler, drv_data);

		/* Get tx DMA channel */
		status = imx_dma_request_by_prio(&drv_data->tx_channel,
						"spi_imx_tx", DMA_PRIO_MEDIUM);
		if (status < 0) {
			dev_err(dev,
				"probe - problem (%d) requesting tx channel\n",
				status);
			imx_dma_free(drv_data->rx_channel);
			goto err_no_txdma;
		} else
			imx_dma_setup_handlers(drv_data->tx_channel,
						dma_tx_handler, dma_err_handler,
						drv_data);

		/* Set request source and burst length for allocated channels */
		switch (drv_data->pdev->id) {
		case 1:
			/* Using SPI1 */
			RSSR(drv_data->rx_channel) = DMA_REQ_SPI1_R;
			RSSR(drv_data->tx_channel) = DMA_REQ_SPI1_T;
			break;
		case 2:
			/* Using SPI2 */
			RSSR(drv_data->rx_channel) = DMA_REQ_SPI2_R;
			RSSR(drv_data->tx_channel) = DMA_REQ_SPI2_T;
			break;
		default:
			dev_err(dev, "probe - bad SPI Id\n");
			imx_dma_free(drv_data->rx_channel);
			imx_dma_free(drv_data->tx_channel);
			status = -ENODEV;
			goto err_no_devid;
		}
		BLR(drv_data->rx_channel) = SPI_DMA_BLR;
		BLR(drv_data->tx_channel) = SPI_DMA_BLR;
	}

	/* Load default SPI configuration */
	writel(SPI_RESET_START, drv_data->regs + SPI_RESET);
	writel(0, drv_data->regs + SPI_RESET);
	writel(SPI_DEFAULT_CONTROL, drv_data->regs + SPI_CONTROL);

	/* Initial and start queue */
	status = init_queue(drv_data);
	if (status != 0) {
		dev_err(&pdev->dev, "probe - problem initializing queue\n");
		goto err_init_queue;
	}
	status = start_queue(drv_data);
	if (status != 0) {
		dev_err(&pdev->dev, "probe - problem starting queue\n");
		goto err_start_queue;
	}

	/* Register with the SPI framework */
	platform_set_drvdata(pdev, drv_data);
	status = spi_register_master(master);
	if (status != 0) {
		dev_err(&pdev->dev, "probe - problem registering spi master\n");
		goto err_spi_register;
	}

	dev_dbg(dev, "probe succeded\n");
	return 0;

err_init_queue:
err_start_queue:
err_spi_register:
	destroy_queue(drv_data);

err_no_rxdma:
err_no_txdma:
err_no_devid:
	free_irq(irq, drv_data);

err_no_irqres:
	iounmap(drv_data->regs);

err_no_iomap:
	release_resource(drv_data->ioarea);
	kfree(drv_data->ioarea);

err_no_iores:
	spi_master_put(master);

err_no_pdata:
err_no_mem:
	return status;
}

static int __exit spi_imx_remove(struct platform_device *pdev)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);
	int irq;
	int status = 0;

	if (!drv_data)
		return 0;

	tasklet_kill(&drv_data->pump_transfers);

	/* Remove the queue */
	status = destroy_queue(drv_data);
	if (status != 0) {
		dev_err(&pdev->dev, "queue remove failed (%d)\n", status);
		return status;
	}

	/* Reset SPI */
	writel(SPI_RESET_START, drv_data->regs + SPI_RESET);
	writel(0, drv_data->regs + SPI_RESET);

	/* Release DMA */
	if (drv_data->master_info->enable_dma) {
		RSSR(drv_data->rx_channel) = 0;
		RSSR(drv_data->tx_channel) = 0;
		imx_dma_free(drv_data->tx_channel);
		imx_dma_free(drv_data->rx_channel);
	}

	/* Release IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq >= 0)
		free_irq(irq, drv_data);

	/* Release map resources */
	iounmap(drv_data->regs);
	release_resource(drv_data->ioarea);
	kfree(drv_data->ioarea);

	/* Disconnect from the SPI framework */
	spi_unregister_master(drv_data->master);
	spi_master_put(drv_data->master);

	/* Prevent double remove */
	platform_set_drvdata(pdev, NULL);

	dev_dbg(&pdev->dev, "remove succeded\n");

	return 0;
}

static void spi_imx_shutdown(struct platform_device *pdev)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);

	/* Reset SPI */
	writel(SPI_RESET_START, drv_data->regs + SPI_RESET);
	writel(0, drv_data->regs + SPI_RESET);

	dev_dbg(&pdev->dev, "shutdown succeded\n");
}

#ifdef CONFIG_PM

static int spi_imx_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);
	int status = 0;

	status = stop_queue(drv_data);
	if (status != 0) {
		dev_warn(&pdev->dev, "suspend cannot stop queue\n");
		return status;
	}

	dev_dbg(&pdev->dev, "suspended\n");

	return 0;
}

static int spi_imx_resume(struct platform_device *pdev)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);
	int status = 0;

	/* Start the queue running */
	status = start_queue(drv_data);
	if (status != 0)
		dev_err(&pdev->dev, "problem starting queue (%d)\n", status);
	else
		dev_dbg(&pdev->dev, "resumed\n");

	return status;
}
#else
#define spi_imx_suspend NULL
#define spi_imx_resume NULL
#endif /* CONFIG_PM */

static struct platform_driver driver = {
	.driver = {
		.name = "spi_imx",
		.bus = &platform_bus_type,
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(spi_imx_remove),
	.shutdown = spi_imx_shutdown,
	.suspend = spi_imx_suspend,
	.resume = spi_imx_resume,
};

static int __init spi_imx_init(void)
{
	return platform_driver_probe(&driver, spi_imx_probe);
}
module_init(spi_imx_init);

static void __exit spi_imx_exit(void)
{
	platform_driver_unregister(&driver);
}
module_exit(spi_imx_exit);

MODULE_AUTHOR("Andrea Paterniani, <a.paterniani@swapp-eng.it>");
MODULE_DESCRIPTION("iMX SPI Controller Driver");
MODULE_LICENSE("GPL");
