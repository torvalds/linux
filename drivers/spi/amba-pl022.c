/*
 * drivers/spi/amba-pl022.c
 *
 * A driver for the ARM PL022 PrimeCell SSP/SPI bus master.
 *
 * Copyright (C) 2008-2009 ST-Ericsson AB
 * Copyright (C) 2006 STMicroelectronics Pvt. Ltd.
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 *
 * Initial version inspired by:
 *	linux-2.6.17-rc3-mm1/drivers/spi/pxa2xx_spi.c
 * Initial adoption to PL022 by:
 *      Sachin Verma <sachin.verma@st.com>
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

/*
 * TODO:
 * - add timeout on polled transfers
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

/*
 * This macro is used to define some register default values.
 * reg is masked with mask, the OR:ed with an (again masked)
 * val shifted sb steps to the left.
 */
#define SSP_WRITE_BITS(reg, val, mask, sb) \
 ((reg) = (((reg) & ~(mask)) | (((val)<<(sb)) & (mask))))

/*
 * This macro is also used to define some default values.
 * It will just shift val by sb steps to the left and mask
 * the result with mask.
 */
#define GEN_MASK_BITS(val, mask, sb) \
 (((val)<<(sb)) & (mask))

#define DRIVE_TX		0
#define DO_NOT_DRIVE_TX		1

#define DO_NOT_QUEUE_DMA	0
#define QUEUE_DMA		1

#define RX_TRANSFER		1
#define TX_TRANSFER		2

/*
 * Macros to access SSP Registers with their offsets
 */
#define SSP_CR0(r)	(r + 0x000)
#define SSP_CR1(r)	(r + 0x004)
#define SSP_DR(r)	(r + 0x008)
#define SSP_SR(r)	(r + 0x00C)
#define SSP_CPSR(r)	(r + 0x010)
#define SSP_IMSC(r)	(r + 0x014)
#define SSP_RIS(r)	(r + 0x018)
#define SSP_MIS(r)	(r + 0x01C)
#define SSP_ICR(r)	(r + 0x020)
#define SSP_DMACR(r)	(r + 0x024)
#define SSP_ITCR(r)	(r + 0x080)
#define SSP_ITIP(r)	(r + 0x084)
#define SSP_ITOP(r)	(r + 0x088)
#define SSP_TDR(r)	(r + 0x08C)

#define SSP_PID0(r)	(r + 0xFE0)
#define SSP_PID1(r)	(r + 0xFE4)
#define SSP_PID2(r)	(r + 0xFE8)
#define SSP_PID3(r)	(r + 0xFEC)

#define SSP_CID0(r)	(r + 0xFF0)
#define SSP_CID1(r)	(r + 0xFF4)
#define SSP_CID2(r)	(r + 0xFF8)
#define SSP_CID3(r)	(r + 0xFFC)

/*
 * SSP Control Register 0  - SSP_CR0
 */
#define SSP_CR0_MASK_DSS	(0x0FUL << 0)
#define SSP_CR0_MASK_FRF	(0x3UL << 4)
#define SSP_CR0_MASK_SPO	(0x1UL << 6)
#define SSP_CR0_MASK_SPH	(0x1UL << 7)
#define SSP_CR0_MASK_SCR	(0xFFUL << 8)

/*
 * The ST version of this block moves som bits
 * in SSP_CR0 and extends it to 32 bits
 */
#define SSP_CR0_MASK_DSS_ST	(0x1FUL << 0)
#define SSP_CR0_MASK_HALFDUP_ST	(0x1UL << 5)
#define SSP_CR0_MASK_CSS_ST	(0x1FUL << 16)
#define SSP_CR0_MASK_FRF_ST	(0x3UL << 21)


/*
 * SSP Control Register 0  - SSP_CR1
 */
#define SSP_CR1_MASK_LBM	(0x1UL << 0)
#define SSP_CR1_MASK_SSE	(0x1UL << 1)
#define SSP_CR1_MASK_MS		(0x1UL << 2)
#define SSP_CR1_MASK_SOD	(0x1UL << 3)

/*
 * The ST version of this block adds some bits
 * in SSP_CR1
 */
#define SSP_CR1_MASK_RENDN_ST	(0x1UL << 4)
#define SSP_CR1_MASK_TENDN_ST	(0x1UL << 5)
#define SSP_CR1_MASK_MWAIT_ST	(0x1UL << 6)
#define SSP_CR1_MASK_RXIFLSEL_ST (0x7UL << 7)
#define SSP_CR1_MASK_TXIFLSEL_ST (0x7UL << 10)
/* This one is only in the PL023 variant */
#define SSP_CR1_MASK_FBCLKDEL_ST (0x7UL << 13)

/*
 * SSP Status Register - SSP_SR
 */
#define SSP_SR_MASK_TFE		(0x1UL << 0) /* Transmit FIFO empty */
#define SSP_SR_MASK_TNF		(0x1UL << 1) /* Transmit FIFO not full */
#define SSP_SR_MASK_RNE		(0x1UL << 2) /* Receive FIFO not empty */
#define SSP_SR_MASK_RFF		(0x1UL << 3) /* Receive FIFO full */
#define SSP_SR_MASK_BSY		(0x1UL << 4) /* Busy Flag */

/*
 * SSP Clock Prescale Register  - SSP_CPSR
 */
#define SSP_CPSR_MASK_CPSDVSR	(0xFFUL << 0)

/*
 * SSP Interrupt Mask Set/Clear Register - SSP_IMSC
 */
#define SSP_IMSC_MASK_RORIM (0x1UL << 0) /* Receive Overrun Interrupt mask */
#define SSP_IMSC_MASK_RTIM  (0x1UL << 1) /* Receive timeout Interrupt mask */
#define SSP_IMSC_MASK_RXIM  (0x1UL << 2) /* Receive FIFO Interrupt mask */
#define SSP_IMSC_MASK_TXIM  (0x1UL << 3) /* Transmit FIFO Interrupt mask */

/*
 * SSP Raw Interrupt Status Register - SSP_RIS
 */
/* Receive Overrun Raw Interrupt status */
#define SSP_RIS_MASK_RORRIS		(0x1UL << 0)
/* Receive Timeout Raw Interrupt status */
#define SSP_RIS_MASK_RTRIS		(0x1UL << 1)
/* Receive FIFO Raw Interrupt status */
#define SSP_RIS_MASK_RXRIS		(0x1UL << 2)
/* Transmit FIFO Raw Interrupt status */
#define SSP_RIS_MASK_TXRIS		(0x1UL << 3)

/*
 * SSP Masked Interrupt Status Register - SSP_MIS
 */
/* Receive Overrun Masked Interrupt status */
#define SSP_MIS_MASK_RORMIS		(0x1UL << 0)
/* Receive Timeout Masked Interrupt status */
#define SSP_MIS_MASK_RTMIS		(0x1UL << 1)
/* Receive FIFO Masked Interrupt status */
#define SSP_MIS_MASK_RXMIS		(0x1UL << 2)
/* Transmit FIFO Masked Interrupt status */
#define SSP_MIS_MASK_TXMIS		(0x1UL << 3)

/*
 * SSP Interrupt Clear Register - SSP_ICR
 */
/* Receive Overrun Raw Clear Interrupt bit */
#define SSP_ICR_MASK_RORIC		(0x1UL << 0)
/* Receive Timeout Clear Interrupt bit */
#define SSP_ICR_MASK_RTIC		(0x1UL << 1)

/*
 * SSP DMA Control Register - SSP_DMACR
 */
/* Receive DMA Enable bit */
#define SSP_DMACR_MASK_RXDMAE		(0x1UL << 0)
/* Transmit DMA Enable bit */
#define SSP_DMACR_MASK_TXDMAE		(0x1UL << 1)

/*
 * SSP Integration Test control Register - SSP_ITCR
 */
#define SSP_ITCR_MASK_ITEN		(0x1UL << 0)
#define SSP_ITCR_MASK_TESTFIFO		(0x1UL << 1)

/*
 * SSP Integration Test Input Register - SSP_ITIP
 */
#define ITIP_MASK_SSPRXD		 (0x1UL << 0)
#define ITIP_MASK_SSPFSSIN		 (0x1UL << 1)
#define ITIP_MASK_SSPCLKIN		 (0x1UL << 2)
#define ITIP_MASK_RXDMAC		 (0x1UL << 3)
#define ITIP_MASK_TXDMAC		 (0x1UL << 4)
#define ITIP_MASK_SSPTXDIN		 (0x1UL << 5)

/*
 * SSP Integration Test output Register - SSP_ITOP
 */
#define ITOP_MASK_SSPTXD		 (0x1UL << 0)
#define ITOP_MASK_SSPFSSOUT		 (0x1UL << 1)
#define ITOP_MASK_SSPCLKOUT		 (0x1UL << 2)
#define ITOP_MASK_SSPOEn		 (0x1UL << 3)
#define ITOP_MASK_SSPCTLOEn		 (0x1UL << 4)
#define ITOP_MASK_RORINTR		 (0x1UL << 5)
#define ITOP_MASK_RTINTR		 (0x1UL << 6)
#define ITOP_MASK_RXINTR		 (0x1UL << 7)
#define ITOP_MASK_TXINTR		 (0x1UL << 8)
#define ITOP_MASK_INTR			 (0x1UL << 9)
#define ITOP_MASK_RXDMABREQ		 (0x1UL << 10)
#define ITOP_MASK_RXDMASREQ		 (0x1UL << 11)
#define ITOP_MASK_TXDMABREQ		 (0x1UL << 12)
#define ITOP_MASK_TXDMASREQ		 (0x1UL << 13)

/*
 * SSP Test Data Register - SSP_TDR
 */
#define TDR_MASK_TESTDATA		(0xFFFFFFFF)

/*
 * Message State
 * we use the spi_message.state (void *) pointer to
 * hold a single state value, that's why all this
 * (void *) casting is done here.
 */
#define STATE_START			((void *) 0)
#define STATE_RUNNING			((void *) 1)
#define STATE_DONE			((void *) 2)
#define STATE_ERROR			((void *) -1)

/*
 * Queue State
 */
#define QUEUE_RUNNING			(0)
#define QUEUE_STOPPED			(1)
/*
 * SSP State - Whether Enabled or Disabled
 */
#define SSP_DISABLED			(0)
#define SSP_ENABLED			(1)

/*
 * SSP DMA State - Whether DMA Enabled or Disabled
 */
#define SSP_DMA_DISABLED		(0)
#define SSP_DMA_ENABLED			(1)

/*
 * SSP Clock Defaults
 */
#define SSP_DEFAULT_CLKRATE 0x2
#define SSP_DEFAULT_PRESCALE 0x40

/*
 * SSP Clock Parameter ranges
 */
#define CPSDVR_MIN 0x02
#define CPSDVR_MAX 0xFE
#define SCR_MIN 0x00
#define SCR_MAX 0xFF

/*
 * SSP Interrupt related Macros
 */
#define DEFAULT_SSP_REG_IMSC  0x0UL
#define DISABLE_ALL_INTERRUPTS DEFAULT_SSP_REG_IMSC
#define ENABLE_ALL_INTERRUPTS (~DEFAULT_SSP_REG_IMSC)

#define CLEAR_ALL_INTERRUPTS  0x3


/*
 * The type of reading going on on this chip
 */
enum ssp_reading {
	READING_NULL,
	READING_U8,
	READING_U16,
	READING_U32
};

/**
 * The type of writing going on on this chip
 */
enum ssp_writing {
	WRITING_NULL,
	WRITING_U8,
	WRITING_U16,
	WRITING_U32
};

/**
 * struct vendor_data - vendor-specific config parameters
 * for PL022 derivates
 * @fifodepth: depth of FIFOs (both)
 * @max_bpw: maximum number of bits per word
 * @unidir: supports unidirection transfers
 * @extended_cr: 32 bit wide control register 0 with extra
 * features and extra features in CR1 as found in the ST variants
 * @pl023: supports a subset of the ST extensions called "PL023"
 */
struct vendor_data {
	int fifodepth;
	int max_bpw;
	bool unidir;
	bool extended_cr;
	bool pl023;
};

/**
 * struct pl022 - This is the private SSP driver data structure
 * @adev: AMBA device model hookup
 * @vendor: Vendor data for the IP block
 * @phybase: The physical memory where the SSP device resides
 * @virtbase: The virtual memory where the SSP is mapped
 * @master: SPI framework hookup
 * @master_info: controller-specific data from machine setup
 * @regs: SSP controller register's virtual address
 * @pump_messages: Work struct for scheduling work to the workqueue
 * @lock: spinlock to syncronise access to driver data
 * @workqueue: a workqueue on which any spi_message request is queued
 * @busy: workqueue is busy
 * @run: workqueue is running
 * @pump_transfers: Tasklet used in Interrupt Transfer mode
 * @cur_msg: Pointer to current spi_message being processed
 * @cur_transfer: Pointer to current spi_transfer
 * @cur_chip: pointer to current clients chip(assigned from controller_state)
 * @tx: current position in TX buffer to be read
 * @tx_end: end position in TX buffer to be read
 * @rx: current position in RX buffer to be written
 * @rx_end: end position in RX buffer to be written
 * @readingtype: the type of read currently going on
 * @writingtype: the type or write currently going on
 */
struct pl022 {
	struct amba_device		*adev;
	struct vendor_data		*vendor;
	resource_size_t			phybase;
	void __iomem			*virtbase;
	struct clk			*clk;
	struct spi_master		*master;
	struct pl022_ssp_controller	*master_info;
	/* Driver message queue */
	struct workqueue_struct		*workqueue;
	struct work_struct		pump_messages;
	spinlock_t			queue_lock;
	struct list_head		queue;
	int				busy;
	int				run;
	/* Message transfer pump */
	struct tasklet_struct		pump_transfers;
	struct spi_message		*cur_msg;
	struct spi_transfer		*cur_transfer;
	struct chip_data		*cur_chip;
	void				*tx;
	void				*tx_end;
	void				*rx;
	void				*rx_end;
	enum ssp_reading		read;
	enum ssp_writing		write;
	u32				exp_fifo_level;
	/* DMA settings */
#ifdef CONFIG_DMA_ENGINE
	struct dma_chan			*dma_rx_channel;
	struct dma_chan			*dma_tx_channel;
	struct sg_table			sgt_rx;
	struct sg_table			sgt_tx;
	char				*dummypage;
#endif
};

/**
 * struct chip_data - To maintain runtime state of SSP for each client chip
 * @cr0: Value of control register CR0 of SSP - on later ST variants this
 *       register is 32 bits wide rather than just 16
 * @cr1: Value of control register CR1 of SSP
 * @dmacr: Value of DMA control Register of SSP
 * @cpsr: Value of Clock prescale register
 * @n_bytes: how many bytes(power of 2) reqd for a given data width of client
 * @enable_dma: Whether to enable DMA or not
 * @write: function ptr to be used to write when doing xfer for this chip
 * @read: function ptr to be used to read when doing xfer for this chip
 * @cs_control: chip select callback provided by chip
 * @xfer_type: polling/interrupt/DMA
 *
 * Runtime state of the SSP controller, maintained per chip,
 * This would be set according to the current message that would be served
 */
struct chip_data {
	u32 cr0;
	u16 cr1;
	u16 dmacr;
	u16 cpsr;
	u8 n_bytes;
	bool enable_dma;
	enum ssp_reading read;
	enum ssp_writing write;
	void (*cs_control) (u32 command);
	int xfer_type;
};

/**
 * null_cs_control - Dummy chip select function
 * @command: select/delect the chip
 *
 * If no chip select function is provided by client this is used as dummy
 * chip select
 */
static void null_cs_control(u32 command)
{
	pr_debug("pl022: dummy chip select control, CS=0x%x\n", command);
}

/**
 * giveback - current spi_message is over, schedule next message and call
 * callback of this message. Assumes that caller already
 * set message->status; dma and pio irqs are blocked
 * @pl022: SSP driver private data structure
 */
static void giveback(struct pl022 *pl022)
{
	struct spi_transfer *last_transfer;
	unsigned long flags;
	struct spi_message *msg;
	void (*curr_cs_control) (u32 command);

	/*
	 * This local reference to the chip select function
	 * is needed because we set curr_chip to NULL
	 * as a step toward termininating the message.
	 */
	curr_cs_control = pl022->cur_chip->cs_control;
	spin_lock_irqsave(&pl022->queue_lock, flags);
	msg = pl022->cur_msg;
	pl022->cur_msg = NULL;
	pl022->cur_transfer = NULL;
	pl022->cur_chip = NULL;
	queue_work(pl022->workqueue, &pl022->pump_messages);
	spin_unlock_irqrestore(&pl022->queue_lock, flags);

	last_transfer = list_entry(msg->transfers.prev,
					struct spi_transfer,
					transfer_list);

	/* Delay if requested before any change in chip select */
	if (last_transfer->delay_usecs)
		/*
		 * FIXME: This runs in interrupt context.
		 * Is this really smart?
		 */
		udelay(last_transfer->delay_usecs);

	/*
	 * Drop chip select UNLESS cs_change is true or we are returning
	 * a message with an error, or next message is for another chip
	 */
	if (!last_transfer->cs_change)
		curr_cs_control(SSP_CHIP_DESELECT);
	else {
		struct spi_message *next_msg;

		/* Holding of cs was hinted, but we need to make sure
		 * the next message is for the same chip.  Don't waste
		 * time with the following tests unless this was hinted.
		 *
		 * We cannot postpone this until pump_messages, because
		 * after calling msg->complete (below) the driver that
		 * sent the current message could be unloaded, which
		 * could invalidate the cs_control() callback...
		 */

		/* get a pointer to the next message, if any */
		spin_lock_irqsave(&pl022->queue_lock, flags);
		if (list_empty(&pl022->queue))
			next_msg = NULL;
		else
			next_msg = list_entry(pl022->queue.next,
					struct spi_message, queue);
		spin_unlock_irqrestore(&pl022->queue_lock, flags);

		/* see if the next and current messages point
		 * to the same chip
		 */
		if (next_msg && next_msg->spi != msg->spi)
			next_msg = NULL;
		if (!next_msg || msg->state == STATE_ERROR)
			curr_cs_control(SSP_CHIP_DESELECT);
	}
	msg->state = NULL;
	if (msg->complete)
		msg->complete(msg->context);
	/* This message is completed, so let's turn off the clocks! */
	clk_disable(pl022->clk);
	amba_pclk_disable(pl022->adev);
}

/**
 * flush - flush the FIFO to reach a clean state
 * @pl022: SSP driver private data structure
 */
static int flush(struct pl022 *pl022)
{
	unsigned long limit = loops_per_jiffy << 1;

	dev_dbg(&pl022->adev->dev, "flush\n");
	do {
		while (readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_RNE)
			readw(SSP_DR(pl022->virtbase));
	} while ((readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_BSY) && limit--);

	pl022->exp_fifo_level = 0;

	return limit;
}

/**
 * restore_state - Load configuration of current chip
 * @pl022: SSP driver private data structure
 */
static void restore_state(struct pl022 *pl022)
{
	struct chip_data *chip = pl022->cur_chip;

	if (pl022->vendor->extended_cr)
		writel(chip->cr0, SSP_CR0(pl022->virtbase));
	else
		writew(chip->cr0, SSP_CR0(pl022->virtbase));
	writew(chip->cr1, SSP_CR1(pl022->virtbase));
	writew(chip->dmacr, SSP_DMACR(pl022->virtbase));
	writew(chip->cpsr, SSP_CPSR(pl022->virtbase));
	writew(DISABLE_ALL_INTERRUPTS, SSP_IMSC(pl022->virtbase));
	writew(CLEAR_ALL_INTERRUPTS, SSP_ICR(pl022->virtbase));
}

/*
 * Default SSP Register Values
 */
#define DEFAULT_SSP_REG_CR0 ( \
	GEN_MASK_BITS(SSP_DATA_BITS_12, SSP_CR0_MASK_DSS, 0)	| \
	GEN_MASK_BITS(SSP_INTERFACE_MOTOROLA_SPI, SSP_CR0_MASK_FRF, 4) | \
	GEN_MASK_BITS(SSP_CLK_POL_IDLE_LOW, SSP_CR0_MASK_SPO, 6) | \
	GEN_MASK_BITS(SSP_CLK_SECOND_EDGE, SSP_CR0_MASK_SPH, 7) | \
	GEN_MASK_BITS(SSP_DEFAULT_CLKRATE, SSP_CR0_MASK_SCR, 8) \
)

/* ST versions have slightly different bit layout */
#define DEFAULT_SSP_REG_CR0_ST ( \
	GEN_MASK_BITS(SSP_DATA_BITS_12, SSP_CR0_MASK_DSS_ST, 0)	| \
	GEN_MASK_BITS(SSP_MICROWIRE_CHANNEL_FULL_DUPLEX, SSP_CR0_MASK_HALFDUP_ST, 5) | \
	GEN_MASK_BITS(SSP_CLK_POL_IDLE_LOW, SSP_CR0_MASK_SPO, 6) | \
	GEN_MASK_BITS(SSP_CLK_SECOND_EDGE, SSP_CR0_MASK_SPH, 7) | \
	GEN_MASK_BITS(SSP_DEFAULT_CLKRATE, SSP_CR0_MASK_SCR, 8) | \
	GEN_MASK_BITS(SSP_BITS_8, SSP_CR0_MASK_CSS_ST, 16)	| \
	GEN_MASK_BITS(SSP_INTERFACE_MOTOROLA_SPI, SSP_CR0_MASK_FRF_ST, 21) \
)

/* The PL023 version is slightly different again */
#define DEFAULT_SSP_REG_CR0_ST_PL023 ( \
	GEN_MASK_BITS(SSP_DATA_BITS_12, SSP_CR0_MASK_DSS_ST, 0)	| \
	GEN_MASK_BITS(SSP_CLK_POL_IDLE_LOW, SSP_CR0_MASK_SPO, 6) | \
	GEN_MASK_BITS(SSP_CLK_SECOND_EDGE, SSP_CR0_MASK_SPH, 7) | \
	GEN_MASK_BITS(SSP_DEFAULT_CLKRATE, SSP_CR0_MASK_SCR, 8) \
)

#define DEFAULT_SSP_REG_CR1 ( \
	GEN_MASK_BITS(LOOPBACK_DISABLED, SSP_CR1_MASK_LBM, 0) | \
	GEN_MASK_BITS(SSP_DISABLED, SSP_CR1_MASK_SSE, 1) | \
	GEN_MASK_BITS(SSP_MASTER, SSP_CR1_MASK_MS, 2) | \
	GEN_MASK_BITS(DO_NOT_DRIVE_TX, SSP_CR1_MASK_SOD, 3) \
)

/* ST versions extend this register to use all 16 bits */
#define DEFAULT_SSP_REG_CR1_ST ( \
	DEFAULT_SSP_REG_CR1 | \
	GEN_MASK_BITS(SSP_RX_MSB, SSP_CR1_MASK_RENDN_ST, 4) | \
	GEN_MASK_BITS(SSP_TX_MSB, SSP_CR1_MASK_TENDN_ST, 5) | \
	GEN_MASK_BITS(SSP_MWIRE_WAIT_ZERO, SSP_CR1_MASK_MWAIT_ST, 6) |\
	GEN_MASK_BITS(SSP_RX_1_OR_MORE_ELEM, SSP_CR1_MASK_RXIFLSEL_ST, 7) | \
	GEN_MASK_BITS(SSP_TX_1_OR_MORE_EMPTY_LOC, SSP_CR1_MASK_TXIFLSEL_ST, 10) \
)

/*
 * The PL023 variant has further differences: no loopback mode, no microwire
 * support, and a new clock feedback delay setting.
 */
#define DEFAULT_SSP_REG_CR1_ST_PL023 ( \
	GEN_MASK_BITS(SSP_DISABLED, SSP_CR1_MASK_SSE, 1) | \
	GEN_MASK_BITS(SSP_MASTER, SSP_CR1_MASK_MS, 2) | \
	GEN_MASK_BITS(DO_NOT_DRIVE_TX, SSP_CR1_MASK_SOD, 3) | \
	GEN_MASK_BITS(SSP_RX_MSB, SSP_CR1_MASK_RENDN_ST, 4) | \
	GEN_MASK_BITS(SSP_TX_MSB, SSP_CR1_MASK_TENDN_ST, 5) | \
	GEN_MASK_BITS(SSP_RX_1_OR_MORE_ELEM, SSP_CR1_MASK_RXIFLSEL_ST, 7) | \
	GEN_MASK_BITS(SSP_TX_1_OR_MORE_EMPTY_LOC, SSP_CR1_MASK_TXIFLSEL_ST, 10) | \
	GEN_MASK_BITS(SSP_FEEDBACK_CLK_DELAY_NONE, SSP_CR1_MASK_FBCLKDEL_ST, 13) \
)

#define DEFAULT_SSP_REG_CPSR ( \
	GEN_MASK_BITS(SSP_DEFAULT_PRESCALE, SSP_CPSR_MASK_CPSDVSR, 0) \
)

#define DEFAULT_SSP_REG_DMACR (\
	GEN_MASK_BITS(SSP_DMA_DISABLED, SSP_DMACR_MASK_RXDMAE, 0) | \
	GEN_MASK_BITS(SSP_DMA_DISABLED, SSP_DMACR_MASK_TXDMAE, 1) \
)

/**
 * load_ssp_default_config - Load default configuration for SSP
 * @pl022: SSP driver private data structure
 */
static void load_ssp_default_config(struct pl022 *pl022)
{
	if (pl022->vendor->pl023) {
		writel(DEFAULT_SSP_REG_CR0_ST_PL023, SSP_CR0(pl022->virtbase));
		writew(DEFAULT_SSP_REG_CR1_ST_PL023, SSP_CR1(pl022->virtbase));
	} else if (pl022->vendor->extended_cr) {
		writel(DEFAULT_SSP_REG_CR0_ST, SSP_CR0(pl022->virtbase));
		writew(DEFAULT_SSP_REG_CR1_ST, SSP_CR1(pl022->virtbase));
	} else {
		writew(DEFAULT_SSP_REG_CR0, SSP_CR0(pl022->virtbase));
		writew(DEFAULT_SSP_REG_CR1, SSP_CR1(pl022->virtbase));
	}
	writew(DEFAULT_SSP_REG_DMACR, SSP_DMACR(pl022->virtbase));
	writew(DEFAULT_SSP_REG_CPSR, SSP_CPSR(pl022->virtbase));
	writew(DISABLE_ALL_INTERRUPTS, SSP_IMSC(pl022->virtbase));
	writew(CLEAR_ALL_INTERRUPTS, SSP_ICR(pl022->virtbase));
}

/**
 * This will write to TX and read from RX according to the parameters
 * set in pl022.
 */
static void readwriter(struct pl022 *pl022)
{

	/*
	 * The FIFO depth is different inbetween primecell variants.
	 * I believe filling in too much in the FIFO might cause
	 * errons in 8bit wide transfers on ARM variants (just 8 words
	 * FIFO, means only 8x8 = 64 bits in FIFO) at least.
	 *
	 * To prevent this issue, the TX FIFO is only filled to the
	 * unused RX FIFO fill length, regardless of what the TX
	 * FIFO status flag indicates.
	 */
	dev_dbg(&pl022->adev->dev,
		"%s, rx: %p, rxend: %p, tx: %p, txend: %p\n",
		__func__, pl022->rx, pl022->rx_end, pl022->tx, pl022->tx_end);

	/* Read as much as you can */
	while ((readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_RNE)
	       && (pl022->rx < pl022->rx_end)) {
		switch (pl022->read) {
		case READING_NULL:
			readw(SSP_DR(pl022->virtbase));
			break;
		case READING_U8:
			*(u8 *) (pl022->rx) =
				readw(SSP_DR(pl022->virtbase)) & 0xFFU;
			break;
		case READING_U16:
			*(u16 *) (pl022->rx) =
				(u16) readw(SSP_DR(pl022->virtbase));
			break;
		case READING_U32:
			*(u32 *) (pl022->rx) =
				readl(SSP_DR(pl022->virtbase));
			break;
		}
		pl022->rx += (pl022->cur_chip->n_bytes);
		pl022->exp_fifo_level--;
	}
	/*
	 * Write as much as possible up to the RX FIFO size
	 */
	while ((pl022->exp_fifo_level < pl022->vendor->fifodepth)
	       && (pl022->tx < pl022->tx_end)) {
		switch (pl022->write) {
		case WRITING_NULL:
			writew(0x0, SSP_DR(pl022->virtbase));
			break;
		case WRITING_U8:
			writew(*(u8 *) (pl022->tx), SSP_DR(pl022->virtbase));
			break;
		case WRITING_U16:
			writew((*(u16 *) (pl022->tx)), SSP_DR(pl022->virtbase));
			break;
		case WRITING_U32:
			writel(*(u32 *) (pl022->tx), SSP_DR(pl022->virtbase));
			break;
		}
		pl022->tx += (pl022->cur_chip->n_bytes);
		pl022->exp_fifo_level++;
		/*
		 * This inner reader takes care of things appearing in the RX
		 * FIFO as we're transmitting. This will happen a lot since the
		 * clock starts running when you put things into the TX FIFO,
		 * and then things are continously clocked into the RX FIFO.
		 */
		while ((readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_RNE)
		       && (pl022->rx < pl022->rx_end)) {
			switch (pl022->read) {
			case READING_NULL:
				readw(SSP_DR(pl022->virtbase));
				break;
			case READING_U8:
				*(u8 *) (pl022->rx) =
					readw(SSP_DR(pl022->virtbase)) & 0xFFU;
				break;
			case READING_U16:
				*(u16 *) (pl022->rx) =
					(u16) readw(SSP_DR(pl022->virtbase));
				break;
			case READING_U32:
				*(u32 *) (pl022->rx) =
					readl(SSP_DR(pl022->virtbase));
				break;
			}
			pl022->rx += (pl022->cur_chip->n_bytes);
			pl022->exp_fifo_level--;
		}
	}
	/*
	 * When we exit here the TX FIFO should be full and the RX FIFO
	 * should be empty
	 */
}


/**
 * next_transfer - Move to the Next transfer in the current spi message
 * @pl022: SSP driver private data structure
 *
 * This function moves though the linked list of spi transfers in the
 * current spi message and returns with the state of current spi
 * message i.e whether its last transfer is done(STATE_DONE) or
 * Next transfer is ready(STATE_RUNNING)
 */
static void *next_transfer(struct pl022 *pl022)
{
	struct spi_message *msg = pl022->cur_msg;
	struct spi_transfer *trans = pl022->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		pl022->cur_transfer =
		    list_entry(trans->transfer_list.next,
			       struct spi_transfer, transfer_list);
		return STATE_RUNNING;
	}
	return STATE_DONE;
}

/*
 * This DMA functionality is only compiled in if we have
 * access to the generic DMA devices/DMA engine.
 */
#ifdef CONFIG_DMA_ENGINE
static void unmap_free_dma_scatter(struct pl022 *pl022)
{
	/* Unmap and free the SG tables */
	dma_unmap_sg(&pl022->adev->dev, pl022->sgt_tx.sgl,
		     pl022->sgt_tx.nents, DMA_TO_DEVICE);
	dma_unmap_sg(&pl022->adev->dev, pl022->sgt_rx.sgl,
		     pl022->sgt_rx.nents, DMA_FROM_DEVICE);
	sg_free_table(&pl022->sgt_rx);
	sg_free_table(&pl022->sgt_tx);
}

static void dma_callback(void *data)
{
	struct pl022 *pl022 = data;
	struct spi_message *msg = pl022->cur_msg;

	BUG_ON(!pl022->sgt_rx.sgl);

#ifdef VERBOSE_DEBUG
	/*
	 * Optionally dump out buffers to inspect contents, this is
	 * good if you want to convince yourself that the loopback
	 * read/write contents are the same, when adopting to a new
	 * DMA engine.
	 */
	{
		struct scatterlist *sg;
		unsigned int i;

		dma_sync_sg_for_cpu(&pl022->adev->dev,
				    pl022->sgt_rx.sgl,
				    pl022->sgt_rx.nents,
				    DMA_FROM_DEVICE);

		for_each_sg(pl022->sgt_rx.sgl, sg, pl022->sgt_rx.nents, i) {
			dev_dbg(&pl022->adev->dev, "SPI RX SG ENTRY: %d", i);
			print_hex_dump(KERN_ERR, "SPI RX: ",
				       DUMP_PREFIX_OFFSET,
				       16,
				       1,
				       sg_virt(sg),
				       sg_dma_len(sg),
				       1);
		}
		for_each_sg(pl022->sgt_tx.sgl, sg, pl022->sgt_tx.nents, i) {
			dev_dbg(&pl022->adev->dev, "SPI TX SG ENTRY: %d", i);
			print_hex_dump(KERN_ERR, "SPI TX: ",
				       DUMP_PREFIX_OFFSET,
				       16,
				       1,
				       sg_virt(sg),
				       sg_dma_len(sg),
				       1);
		}
	}
#endif

	unmap_free_dma_scatter(pl022);

	/* Update total bytes transfered */
	msg->actual_length += pl022->cur_transfer->len;
	if (pl022->cur_transfer->cs_change)
		pl022->cur_chip->
			cs_control(SSP_CHIP_DESELECT);

	/* Move to next transfer */
	msg->state = next_transfer(pl022);
	tasklet_schedule(&pl022->pump_transfers);
}

static void setup_dma_scatter(struct pl022 *pl022,
			      void *buffer,
			      unsigned int length,
			      struct sg_table *sgtab)
{
	struct scatterlist *sg;
	int bytesleft = length;
	void *bufp = buffer;
	int mapbytes;
	int i;

	if (buffer) {
		for_each_sg(sgtab->sgl, sg, sgtab->nents, i) {
			/*
			 * If there are less bytes left than what fits
			 * in the current page (plus page alignment offset)
			 * we just feed in this, else we stuff in as much
			 * as we can.
			 */
			if (bytesleft < (PAGE_SIZE - offset_in_page(bufp)))
				mapbytes = bytesleft;
			else
				mapbytes = PAGE_SIZE - offset_in_page(bufp);
			sg_set_page(sg, virt_to_page(bufp),
				    mapbytes, offset_in_page(bufp));
			bufp += mapbytes;
			bytesleft -= mapbytes;
			dev_dbg(&pl022->adev->dev,
				"set RX/TX target page @ %p, %d bytes, %d left\n",
				bufp, mapbytes, bytesleft);
		}
	} else {
		/* Map the dummy buffer on every page */
		for_each_sg(sgtab->sgl, sg, sgtab->nents, i) {
			if (bytesleft < PAGE_SIZE)
				mapbytes = bytesleft;
			else
				mapbytes = PAGE_SIZE;
			sg_set_page(sg, virt_to_page(pl022->dummypage),
				    mapbytes, 0);
			bytesleft -= mapbytes;
			dev_dbg(&pl022->adev->dev,
				"set RX/TX to dummy page %d bytes, %d left\n",
				mapbytes, bytesleft);

		}
	}
	BUG_ON(bytesleft);
}

/**
 * configure_dma - configures the channels for the next transfer
 * @pl022: SSP driver's private data structure
 */
static int configure_dma(struct pl022 *pl022)
{
	struct dma_slave_config rx_conf = {
		.src_addr = SSP_DR(pl022->phybase),
		.direction = DMA_FROM_DEVICE,
		.src_maxburst = pl022->vendor->fifodepth >> 1,
	};
	struct dma_slave_config tx_conf = {
		.dst_addr = SSP_DR(pl022->phybase),
		.direction = DMA_TO_DEVICE,
		.dst_maxburst = pl022->vendor->fifodepth >> 1,
	};
	unsigned int pages;
	int ret;
	int sglen;
	struct dma_chan *rxchan = pl022->dma_rx_channel;
	struct dma_chan *txchan = pl022->dma_tx_channel;
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_async_tx_descriptor *txdesc;
	dma_cookie_t cookie;

	/* Check that the channels are available */
	if (!rxchan || !txchan)
		return -ENODEV;

	switch (pl022->read) {
	case READING_NULL:
		/* Use the same as for writing */
		rx_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
		break;
	case READING_U8:
		rx_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case READING_U16:
		rx_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case READING_U32:
		rx_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	}

	switch (pl022->write) {
	case WRITING_NULL:
		/* Use the same as for reading */
		tx_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
		break;
	case WRITING_U8:
		tx_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case WRITING_U16:
		tx_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case WRITING_U32:
		tx_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;;
		break;
	}

	/* SPI pecularity: we need to read and write the same width */
	if (rx_conf.src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		rx_conf.src_addr_width = tx_conf.dst_addr_width;
	if (tx_conf.dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		tx_conf.dst_addr_width = rx_conf.src_addr_width;
	BUG_ON(rx_conf.src_addr_width != tx_conf.dst_addr_width);

	rxchan->device->device_control(rxchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &rx_conf);
	txchan->device->device_control(txchan, DMA_SLAVE_CONFIG,
				       (unsigned long) &tx_conf);

	/* Create sglists for the transfers */
	pages = (pl022->cur_transfer->len >> PAGE_SHIFT) + 1;
	dev_dbg(&pl022->adev->dev, "using %d pages for transfer\n", pages);

	ret = sg_alloc_table(&pl022->sgt_rx, pages, GFP_KERNEL);
	if (ret)
		goto err_alloc_rx_sg;

	ret = sg_alloc_table(&pl022->sgt_tx, pages, GFP_KERNEL);
	if (ret)
		goto err_alloc_tx_sg;

	/* Fill in the scatterlists for the RX+TX buffers */
	setup_dma_scatter(pl022, pl022->rx,
			  pl022->cur_transfer->len, &pl022->sgt_rx);
	setup_dma_scatter(pl022, pl022->tx,
			  pl022->cur_transfer->len, &pl022->sgt_tx);

	/* Map DMA buffers */
	sglen = dma_map_sg(&pl022->adev->dev, pl022->sgt_rx.sgl,
			   pl022->sgt_rx.nents, DMA_FROM_DEVICE);
	if (!sglen)
		goto err_rx_sgmap;

	sglen = dma_map_sg(&pl022->adev->dev, pl022->sgt_tx.sgl,
			   pl022->sgt_tx.nents, DMA_TO_DEVICE);
	if (!sglen)
		goto err_tx_sgmap;

	/* Send both scatterlists */
	rxdesc = rxchan->device->device_prep_slave_sg(rxchan,
				      pl022->sgt_rx.sgl,
				      pl022->sgt_rx.nents,
				      DMA_FROM_DEVICE,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		goto err_rxdesc;

	txdesc = txchan->device->device_prep_slave_sg(txchan,
				      pl022->sgt_tx.sgl,
				      pl022->sgt_tx.nents,
				      DMA_TO_DEVICE,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		goto err_txdesc;

	/* Put the callback on the RX transfer only, that should finish last */
	rxdesc->callback = dma_callback;
	rxdesc->callback_param = pl022;

	/* Submit and fire RX and TX with TX last so we're ready to read! */
	cookie = rxdesc->tx_submit(rxdesc);
	if (dma_submit_error(cookie))
		goto err_submit_rx;
	cookie = txdesc->tx_submit(txdesc);
	if (dma_submit_error(cookie))
		goto err_submit_tx;
	rxchan->device->device_issue_pending(rxchan);
	txchan->device->device_issue_pending(txchan);

	return 0;

err_submit_tx:
err_submit_rx:
err_txdesc:
	txchan->device->device_control(txchan, DMA_TERMINATE_ALL, 0);
err_rxdesc:
	rxchan->device->device_control(rxchan, DMA_TERMINATE_ALL, 0);
	dma_unmap_sg(&pl022->adev->dev, pl022->sgt_tx.sgl,
		     pl022->sgt_tx.nents, DMA_TO_DEVICE);
err_tx_sgmap:
	dma_unmap_sg(&pl022->adev->dev, pl022->sgt_rx.sgl,
		     pl022->sgt_tx.nents, DMA_FROM_DEVICE);
err_rx_sgmap:
	sg_free_table(&pl022->sgt_tx);
err_alloc_tx_sg:
	sg_free_table(&pl022->sgt_rx);
err_alloc_rx_sg:
	return -ENOMEM;
}

static int __init pl022_dma_probe(struct pl022 *pl022)
{
	dma_cap_mask_t mask;

	/* Try to acquire a generic DMA engine slave channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	/*
	 * We need both RX and TX channels to do DMA, else do none
	 * of them.
	 */
	pl022->dma_rx_channel = dma_request_channel(mask,
					    pl022->master_info->dma_filter,
					    pl022->master_info->dma_rx_param);
	if (!pl022->dma_rx_channel) {
		dev_err(&pl022->adev->dev, "no RX DMA channel!\n");
		goto err_no_rxchan;
	}

	pl022->dma_tx_channel = dma_request_channel(mask,
					    pl022->master_info->dma_filter,
					    pl022->master_info->dma_tx_param);
	if (!pl022->dma_tx_channel) {
		dev_err(&pl022->adev->dev, "no TX DMA channel!\n");
		goto err_no_txchan;
	}

	pl022->dummypage = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pl022->dummypage) {
		dev_err(&pl022->adev->dev, "no DMA dummypage!\n");
		goto err_no_dummypage;
	}

	dev_info(&pl022->adev->dev, "setup for DMA on RX %s, TX %s\n",
		 dma_chan_name(pl022->dma_rx_channel),
		 dma_chan_name(pl022->dma_tx_channel));

	return 0;

err_no_dummypage:
	dma_release_channel(pl022->dma_tx_channel);
err_no_txchan:
	dma_release_channel(pl022->dma_rx_channel);
	pl022->dma_rx_channel = NULL;
err_no_rxchan:
	return -ENODEV;
}

static void terminate_dma(struct pl022 *pl022)
{
	struct dma_chan *rxchan = pl022->dma_rx_channel;
	struct dma_chan *txchan = pl022->dma_tx_channel;

	rxchan->device->device_control(rxchan, DMA_TERMINATE_ALL, 0);
	txchan->device->device_control(txchan, DMA_TERMINATE_ALL, 0);
	unmap_free_dma_scatter(pl022);
}

static void pl022_dma_remove(struct pl022 *pl022)
{
	if (pl022->busy)
		terminate_dma(pl022);
	if (pl022->dma_tx_channel)
		dma_release_channel(pl022->dma_tx_channel);
	if (pl022->dma_rx_channel)
		dma_release_channel(pl022->dma_rx_channel);
	kfree(pl022->dummypage);
}

#else
static inline int configure_dma(struct pl022 *pl022)
{
	return -ENODEV;
}

static inline int pl022_dma_probe(struct pl022 *pl022)
{
	return 0;
}

static inline void pl022_dma_remove(struct pl022 *pl022)
{
}
#endif

/**
 * pl022_interrupt_handler - Interrupt handler for SSP controller
 *
 * This function handles interrupts generated for an interrupt based transfer.
 * If a receive overrun (ROR) interrupt is there then we disable SSP, flag the
 * current message's state as STATE_ERROR and schedule the tasklet
 * pump_transfers which will do the postprocessing of the current message by
 * calling giveback(). Otherwise it reads data from RX FIFO till there is no
 * more data, and writes data in TX FIFO till it is not full. If we complete
 * the transfer we move to the next transfer and schedule the tasklet.
 */
static irqreturn_t pl022_interrupt_handler(int irq, void *dev_id)
{
	struct pl022 *pl022 = dev_id;
	struct spi_message *msg = pl022->cur_msg;
	u16 irq_status = 0;
	u16 flag = 0;

	if (unlikely(!msg)) {
		dev_err(&pl022->adev->dev,
			"bad message state in interrupt handler");
		/* Never fail */
		return IRQ_HANDLED;
	}

	/* Read the Interrupt Status Register */
	irq_status = readw(SSP_MIS(pl022->virtbase));

	if (unlikely(!irq_status))
		return IRQ_NONE;

	/*
	 * This handles the FIFO interrupts, the timeout
	 * interrupts are flatly ignored, they cannot be
	 * trusted.
	 */
	if (unlikely(irq_status & SSP_MIS_MASK_RORMIS)) {
		/*
		 * Overrun interrupt - bail out since our Data has been
		 * corrupted
		 */
		dev_err(&pl022->adev->dev, "FIFO overrun\n");
		if (readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_RFF)
			dev_err(&pl022->adev->dev,
				"RXFIFO is full\n");
		if (readw(SSP_SR(pl022->virtbase)) & SSP_SR_MASK_TNF)
			dev_err(&pl022->adev->dev,
				"TXFIFO is full\n");

		/*
		 * Disable and clear interrupts, disable SSP,
		 * mark message with bad status so it can be
		 * retried.
		 */
		writew(DISABLE_ALL_INTERRUPTS,
		       SSP_IMSC(pl022->virtbase));
		writew(CLEAR_ALL_INTERRUPTS, SSP_ICR(pl022->virtbase));
		writew((readw(SSP_CR1(pl022->virtbase)) &
			(~SSP_CR1_MASK_SSE)), SSP_CR1(pl022->virtbase));
		msg->state = STATE_ERROR;

		/* Schedule message queue handler */
		tasklet_schedule(&pl022->pump_transfers);
		return IRQ_HANDLED;
	}

	readwriter(pl022);

	if ((pl022->tx == pl022->tx_end) && (flag == 0)) {
		flag = 1;
		/* Disable Transmit interrupt */
		writew(readw(SSP_IMSC(pl022->virtbase)) &
		       (~SSP_IMSC_MASK_TXIM),
		       SSP_IMSC(pl022->virtbase));
	}

	/*
	 * Since all transactions must write as much as shall be read,
	 * we can conclude the entire transaction once RX is complete.
	 * At this point, all TX will always be finished.
	 */
	if (pl022->rx >= pl022->rx_end) {
		writew(DISABLE_ALL_INTERRUPTS,
		       SSP_IMSC(pl022->virtbase));
		writew(CLEAR_ALL_INTERRUPTS, SSP_ICR(pl022->virtbase));
		if (unlikely(pl022->rx > pl022->rx_end)) {
			dev_warn(&pl022->adev->dev, "read %u surplus "
				 "bytes (did you request an odd "
				 "number of bytes on a 16bit bus?)\n",
				 (u32) (pl022->rx - pl022->rx_end));
		}
		/* Update total bytes transfered */
		msg->actual_length += pl022->cur_transfer->len;
		if (pl022->cur_transfer->cs_change)
			pl022->cur_chip->
				cs_control(SSP_CHIP_DESELECT);
		/* Move to next transfer */
		msg->state = next_transfer(pl022);
		tasklet_schedule(&pl022->pump_transfers);
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

/**
 * This sets up the pointers to memory for the next message to
 * send out on the SPI bus.
 */
static int set_up_next_transfer(struct pl022 *pl022,
				struct spi_transfer *transfer)
{
	int residue;

	/* Sanity check the message for this bus width */
	residue = pl022->cur_transfer->len % pl022->cur_chip->n_bytes;
	if (unlikely(residue != 0)) {
		dev_err(&pl022->adev->dev,
			"message of %u bytes to transmit but the current "
			"chip bus has a data width of %u bytes!\n",
			pl022->cur_transfer->len,
			pl022->cur_chip->n_bytes);
		dev_err(&pl022->adev->dev, "skipping this message\n");
		return -EIO;
	}
	pl022->tx = (void *)transfer->tx_buf;
	pl022->tx_end = pl022->tx + pl022->cur_transfer->len;
	pl022->rx = (void *)transfer->rx_buf;
	pl022->rx_end = pl022->rx + pl022->cur_transfer->len;
	pl022->write =
	    pl022->tx ? pl022->cur_chip->write : WRITING_NULL;
	pl022->read = pl022->rx ? pl022->cur_chip->read : READING_NULL;
	return 0;
}

/**
 * pump_transfers - Tasklet function which schedules next transfer
 * when running in interrupt or DMA transfer mode.
 * @data: SSP driver private data structure
 *
 */
static void pump_transfers(unsigned long data)
{
	struct pl022 *pl022 = (struct pl022 *) data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;

	/* Get current state information */
	message = pl022->cur_msg;
	transfer = pl022->cur_transfer;

	/* Handle for abort */
	if (message->state == STATE_ERROR) {
		message->status = -EIO;
		giveback(pl022);
		return;
	}

	/* Handle end of message */
	if (message->state == STATE_DONE) {
		message->status = 0;
		giveback(pl022);
		return;
	}

	/* Delay if requested at end of transfer before CS change */
	if (message->state == STATE_RUNNING) {
		previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer,
					transfer_list);
		if (previous->delay_usecs)
			/*
			 * FIXME: This runs in interrupt context.
			 * Is this really smart?
			 */
			udelay(previous->delay_usecs);

		/* Drop chip select only if cs_change is requested */
		if (previous->cs_change)
			pl022->cur_chip->cs_control(SSP_CHIP_SELECT);
	} else {
		/* STATE_START */
		message->state = STATE_RUNNING;
	}

	if (set_up_next_transfer(pl022, transfer)) {
		message->state = STATE_ERROR;
		message->status = -EIO;
		giveback(pl022);
		return;
	}
	/* Flush the FIFOs and let's go! */
	flush(pl022);

	if (pl022->cur_chip->enable_dma) {
		if (configure_dma(pl022)) {
			dev_dbg(&pl022->adev->dev,
				"configuration of DMA failed, fall back to interrupt mode\n");
			goto err_config_dma;
		}
		return;
	}

err_config_dma:
	writew(ENABLE_ALL_INTERRUPTS, SSP_IMSC(pl022->virtbase));
}

static void do_interrupt_dma_transfer(struct pl022 *pl022)
{
	u32 irqflags = ENABLE_ALL_INTERRUPTS;

	/* Enable target chip */
	pl022->cur_chip->cs_control(SSP_CHIP_SELECT);
	if (set_up_next_transfer(pl022, pl022->cur_transfer)) {
		/* Error path */
		pl022->cur_msg->state = STATE_ERROR;
		pl022->cur_msg->status = -EIO;
		giveback(pl022);
		return;
	}
	/* If we're using DMA, set up DMA here */
	if (pl022->cur_chip->enable_dma) {
		/* Configure DMA transfer */
		if (configure_dma(pl022)) {
			dev_dbg(&pl022->adev->dev,
				"configuration of DMA failed, fall back to interrupt mode\n");
			goto err_config_dma;
		}
		/* Disable interrupts in DMA mode, IRQ from DMA controller */
		irqflags = DISABLE_ALL_INTERRUPTS;
	}
err_config_dma:
	/* Enable SSP, turn on interrupts */
	writew((readw(SSP_CR1(pl022->virtbase)) | SSP_CR1_MASK_SSE),
	       SSP_CR1(pl022->virtbase));
	writew(irqflags, SSP_IMSC(pl022->virtbase));
}

static void do_polling_transfer(struct pl022 *pl022)
{
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct chip_data *chip;

	chip = pl022->cur_chip;
	message = pl022->cur_msg;

	while (message->state != STATE_DONE) {
		/* Handle for abort */
		if (message->state == STATE_ERROR)
			break;
		transfer = pl022->cur_transfer;

		/* Delay if requested at end of transfer */
		if (message->state == STATE_RUNNING) {
			previous =
			    list_entry(transfer->transfer_list.prev,
				       struct spi_transfer, transfer_list);
			if (previous->delay_usecs)
				udelay(previous->delay_usecs);
			if (previous->cs_change)
				pl022->cur_chip->cs_control(SSP_CHIP_SELECT);
		} else {
			/* STATE_START */
			message->state = STATE_RUNNING;
			pl022->cur_chip->cs_control(SSP_CHIP_SELECT);
		}

		/* Configuration Changing Per Transfer */
		if (set_up_next_transfer(pl022, transfer)) {
			/* Error path */
			message->state = STATE_ERROR;
			break;
		}
		/* Flush FIFOs and enable SSP */
		flush(pl022);
		writew((readw(SSP_CR1(pl022->virtbase)) | SSP_CR1_MASK_SSE),
		       SSP_CR1(pl022->virtbase));

		dev_dbg(&pl022->adev->dev, "polling transfer ongoing ...\n");
		/* FIXME: insert a timeout so we don't hang here indefinately */
		while (pl022->tx < pl022->tx_end || pl022->rx < pl022->rx_end)
			readwriter(pl022);

		/* Update total byte transfered */
		message->actual_length += pl022->cur_transfer->len;
		if (pl022->cur_transfer->cs_change)
			pl022->cur_chip->cs_control(SSP_CHIP_DESELECT);
		/* Move to next transfer */
		message->state = next_transfer(pl022);
	}

	/* Handle end of message */
	if (message->state == STATE_DONE)
		message->status = 0;
	else
		message->status = -EIO;

	giveback(pl022);
	return;
}

/**
 * pump_messages - Workqueue function which processes spi message queue
 * @data: pointer to private data of SSP driver
 *
 * This function checks if there is any spi message in the queue that
 * needs processing and delegate control to appropriate function
 * do_polling_transfer()/do_interrupt_dma_transfer()
 * based on the kind of the transfer
 *
 */
static void pump_messages(struct work_struct *work)
{
	struct pl022 *pl022 =
		container_of(work, struct pl022, pump_messages);
	unsigned long flags;

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&pl022->queue_lock, flags);
	if (list_empty(&pl022->queue) || pl022->run == QUEUE_STOPPED) {
		pl022->busy = 0;
		spin_unlock_irqrestore(&pl022->queue_lock, flags);
		return;
	}
	/* Make sure we are not already running a message */
	if (pl022->cur_msg) {
		spin_unlock_irqrestore(&pl022->queue_lock, flags);
		return;
	}
	/* Extract head of queue */
	pl022->cur_msg =
	    list_entry(pl022->queue.next, struct spi_message, queue);

	list_del_init(&pl022->cur_msg->queue);
	pl022->busy = 1;
	spin_unlock_irqrestore(&pl022->queue_lock, flags);

	/* Initial message state */
	pl022->cur_msg->state = STATE_START;
	pl022->cur_transfer = list_entry(pl022->cur_msg->transfers.next,
					    struct spi_transfer,
					    transfer_list);

	/* Setup the SPI using the per chip configuration */
	pl022->cur_chip = spi_get_ctldata(pl022->cur_msg->spi);
	/*
	 * We enable the clocks here, then the clocks will be disabled when
	 * giveback() is called in each method (poll/interrupt/DMA)
	 */
	amba_pclk_enable(pl022->adev);
	clk_enable(pl022->clk);
	restore_state(pl022);
	flush(pl022);

	if (pl022->cur_chip->xfer_type == POLLING_TRANSFER)
		do_polling_transfer(pl022);
	else
		do_interrupt_dma_transfer(pl022);
}


static int __init init_queue(struct pl022 *pl022)
{
	INIT_LIST_HEAD(&pl022->queue);
	spin_lock_init(&pl022->queue_lock);

	pl022->run = QUEUE_STOPPED;
	pl022->busy = 0;

	tasklet_init(&pl022->pump_transfers,
			pump_transfers,	(unsigned long)pl022);

	INIT_WORK(&pl022->pump_messages, pump_messages);
	pl022->workqueue = create_singlethread_workqueue(
					dev_name(pl022->master->dev.parent));
	if (pl022->workqueue == NULL)
		return -EBUSY;

	return 0;
}


static int start_queue(struct pl022 *pl022)
{
	unsigned long flags;

	spin_lock_irqsave(&pl022->queue_lock, flags);

	if (pl022->run == QUEUE_RUNNING || pl022->busy) {
		spin_unlock_irqrestore(&pl022->queue_lock, flags);
		return -EBUSY;
	}

	pl022->run = QUEUE_RUNNING;
	pl022->cur_msg = NULL;
	pl022->cur_transfer = NULL;
	pl022->cur_chip = NULL;
	spin_unlock_irqrestore(&pl022->queue_lock, flags);

	queue_work(pl022->workqueue, &pl022->pump_messages);

	return 0;
}


static int stop_queue(struct pl022 *pl022)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&pl022->queue_lock, flags);

	/* This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the pl022->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead */
	while (!list_empty(&pl022->queue) && pl022->busy && limit--) {
		spin_unlock_irqrestore(&pl022->queue_lock, flags);
		msleep(10);
		spin_lock_irqsave(&pl022->queue_lock, flags);
	}

	if (!list_empty(&pl022->queue) || pl022->busy)
		status = -EBUSY;
	else pl022->run = QUEUE_STOPPED;

	spin_unlock_irqrestore(&pl022->queue_lock, flags);

	return status;
}

static int destroy_queue(struct pl022 *pl022)
{
	int status;

	status = stop_queue(pl022);
	/* we are unloading the module or failing to load (only two calls
	 * to this routine), and neither call can handle a return value.
	 * However, destroy_workqueue calls flush_workqueue, and that will
	 * block until all work is done.  If the reason that stop_queue
	 * timed out is that the work will never finish, then it does no
	 * good to call destroy_workqueue, so return anyway. */
	if (status != 0)
		return status;

	destroy_workqueue(pl022->workqueue);

	return 0;
}

static int verify_controller_parameters(struct pl022 *pl022,
				struct pl022_config_chip const *chip_info)
{
	if ((chip_info->iface < SSP_INTERFACE_MOTOROLA_SPI)
	    || (chip_info->iface > SSP_INTERFACE_UNIDIRECTIONAL)) {
		dev_err(&pl022->adev->dev,
			"interface is configured incorrectly\n");
		return -EINVAL;
	}
	if ((chip_info->iface == SSP_INTERFACE_UNIDIRECTIONAL) &&
	    (!pl022->vendor->unidir)) {
		dev_err(&pl022->adev->dev,
			"unidirectional mode not supported in this "
			"hardware version\n");
		return -EINVAL;
	}
	if ((chip_info->hierarchy != SSP_MASTER)
	    && (chip_info->hierarchy != SSP_SLAVE)) {
		dev_err(&pl022->adev->dev,
			"hierarchy is configured incorrectly\n");
		return -EINVAL;
	}
	if ((chip_info->com_mode != INTERRUPT_TRANSFER)
	    && (chip_info->com_mode != DMA_TRANSFER)
	    && (chip_info->com_mode != POLLING_TRANSFER)) {
		dev_err(&pl022->adev->dev,
			"Communication mode is configured incorrectly\n");
		return -EINVAL;
	}
	if ((chip_info->rx_lev_trig < SSP_RX_1_OR_MORE_ELEM)
	    || (chip_info->rx_lev_trig > SSP_RX_32_OR_MORE_ELEM)) {
		dev_err(&pl022->adev->dev,
			"RX FIFO Trigger Level is configured incorrectly\n");
		return -EINVAL;
	}
	if ((chip_info->tx_lev_trig < SSP_TX_1_OR_MORE_EMPTY_LOC)
	    || (chip_info->tx_lev_trig > SSP_TX_32_OR_MORE_EMPTY_LOC)) {
		dev_err(&pl022->adev->dev,
			"TX FIFO Trigger Level is configured incorrectly\n");
		return -EINVAL;
	}
	if (chip_info->iface == SSP_INTERFACE_NATIONAL_MICROWIRE) {
		if ((chip_info->ctrl_len < SSP_BITS_4)
		    || (chip_info->ctrl_len > SSP_BITS_32)) {
			dev_err(&pl022->adev->dev,
				"CTRL LEN is configured incorrectly\n");
			return -EINVAL;
		}
		if ((chip_info->wait_state != SSP_MWIRE_WAIT_ZERO)
		    && (chip_info->wait_state != SSP_MWIRE_WAIT_ONE)) {
			dev_err(&pl022->adev->dev,
				"Wait State is configured incorrectly\n");
			return -EINVAL;
		}
		/* Half duplex is only available in the ST Micro version */
		if (pl022->vendor->extended_cr) {
			if ((chip_info->duplex !=
			     SSP_MICROWIRE_CHANNEL_FULL_DUPLEX)
			    && (chip_info->duplex !=
				SSP_MICROWIRE_CHANNEL_HALF_DUPLEX)) {
				dev_err(&pl022->adev->dev,
					"Microwire duplex mode is configured incorrectly\n");
				return -EINVAL;
			}
		} else {
			if (chip_info->duplex != SSP_MICROWIRE_CHANNEL_FULL_DUPLEX)
				dev_err(&pl022->adev->dev,
					"Microwire half duplex mode requested,"
					" but this is only available in the"
					" ST version of PL022\n");
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * pl022_transfer - transfer function registered to SPI master framework
 * @spi: spi device which is requesting transfer
 * @msg: spi message which is to handled is queued to driver queue
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. It will queue the spi_message in the queue of driver if
 * the queue is not stopped and return.
 */
static int pl022_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct pl022 *pl022 = spi_master_get_devdata(spi->master);
	unsigned long flags;

	spin_lock_irqsave(&pl022->queue_lock, flags);

	if (pl022->run == QUEUE_STOPPED) {
		spin_unlock_irqrestore(&pl022->queue_lock, flags);
		return -ESHUTDOWN;
	}
	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = STATE_START;

	list_add_tail(&msg->queue, &pl022->queue);
	if (pl022->run == QUEUE_RUNNING && !pl022->busy)
		queue_work(pl022->workqueue, &pl022->pump_messages);

	spin_unlock_irqrestore(&pl022->queue_lock, flags);
	return 0;
}

static int calculate_effective_freq(struct pl022 *pl022,
				    int freq,
				    struct ssp_clock_params *clk_freq)
{
	/* Lets calculate the frequency parameters */
	u16 cpsdvsr = 2;
	u16 scr = 0;
	bool freq_found = false;
	u32 rate;
	u32 max_tclk;
	u32 min_tclk;

	rate = clk_get_rate(pl022->clk);
	/* cpsdvscr = 2 & scr 0 */
	max_tclk = (rate / (CPSDVR_MIN * (1 + SCR_MIN)));
	/* cpsdvsr = 254 & scr = 255 */
	min_tclk = (rate / (CPSDVR_MAX * (1 + SCR_MAX)));

	if ((freq <= max_tclk) && (freq >= min_tclk)) {
		while (cpsdvsr <= CPSDVR_MAX && !freq_found) {
			while (scr <= SCR_MAX && !freq_found) {
				if ((rate /
				     (cpsdvsr * (1 + scr))) > freq)
					scr += 1;
				else {
					/*
					 * This bool is made true when
					 * effective frequency >=
					 * target frequency is found
					 */
					freq_found = true;
					if ((rate /
					     (cpsdvsr * (1 + scr))) != freq) {
						if (scr == SCR_MIN) {
							cpsdvsr -= 2;
							scr = SCR_MAX;
						} else
							scr -= 1;
					}
				}
			}
			if (!freq_found) {
				cpsdvsr += 2;
				scr = SCR_MIN;
			}
		}
		if (cpsdvsr != 0) {
			dev_dbg(&pl022->adev->dev,
				"SSP Effective Frequency is %u\n",
				(rate / (cpsdvsr * (1 + scr))));
			clk_freq->cpsdvsr = (u8) (cpsdvsr & 0xFF);
			clk_freq->scr = (u8) (scr & 0xFF);
			dev_dbg(&pl022->adev->dev,
				"SSP cpsdvsr = %d, scr = %d\n",
				clk_freq->cpsdvsr, clk_freq->scr);
		}
	} else {
		dev_err(&pl022->adev->dev,
			"controller data is incorrect: out of range frequency");
		return -EINVAL;
	}
	return 0;
}


/*
 * A piece of default chip info unless the platform
 * supplies it.
 */
static const struct pl022_config_chip pl022_default_chip_info = {
	.com_mode = POLLING_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	.hierarchy = SSP_SLAVE,
	.slave_tx_disable = DO_NOT_DRIVE_TX,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_1_OR_MORE_EMPTY_LOC,
	.ctrl_len = SSP_BITS_8,
	.wait_state = SSP_MWIRE_WAIT_ZERO,
	.duplex = SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	.cs_control = null_cs_control,
};


/**
 * pl022_setup - setup function registered to SPI master framework
 * @spi: spi device which is requesting setup
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. If it is the first time when setup is called by this device,
 * this function will initialize the runtime state for this chip and save
 * the same in the device structure. Else it will update the runtime info
 * with the updated chip info. Nothing is really being written to the
 * controller hardware here, that is not done until the actual transfer
 * commence.
 */
static int pl022_setup(struct spi_device *spi)
{
	struct pl022_config_chip const *chip_info;
	struct chip_data *chip;
	struct ssp_clock_params clk_freq;
	int status = 0;
	struct pl022 *pl022 = spi_master_get_devdata(spi->master);
	unsigned int bits = spi->bits_per_word;
	u32 tmp;

	if (!spi->max_speed_hz)
		return -EINVAL;

	/* Get controller_state if one is supplied */
	chip = spi_get_ctldata(spi);

	if (chip == NULL) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip) {
			dev_err(&spi->dev,
				"cannot allocate controller state\n");
			return -ENOMEM;
		}
		dev_dbg(&spi->dev,
			"allocated memory for controller's runtime state\n");
	}

	/* Get controller data if one is supplied */
	chip_info = spi->controller_data;

	if (chip_info == NULL) {
		chip_info = &pl022_default_chip_info;
		/* spi_board_info.controller_data not is supplied */
		dev_dbg(&spi->dev,
			"using default controller_data settings\n");
	} else
		dev_dbg(&spi->dev,
			"using user supplied controller_data settings\n");

	/*
	 * We can override with custom divisors, else we use the board
	 * frequency setting
	 */
	if ((0 == chip_info->clk_freq.cpsdvsr)
	    && (0 == chip_info->clk_freq.scr)) {
		status = calculate_effective_freq(pl022,
						  spi->max_speed_hz,
						  &clk_freq);
		if (status < 0)
			goto err_config_params;
	} else {
		memcpy(&clk_freq, &chip_info->clk_freq, sizeof(clk_freq));
		if ((clk_freq.cpsdvsr % 2) != 0)
			clk_freq.cpsdvsr =
				clk_freq.cpsdvsr - 1;
	}
	if ((clk_freq.cpsdvsr < CPSDVR_MIN)
	    || (clk_freq.cpsdvsr > CPSDVR_MAX)) {
		dev_err(&spi->dev,
			"cpsdvsr is configured incorrectly\n");
		goto err_config_params;
	}


	status = verify_controller_parameters(pl022, chip_info);
	if (status) {
		dev_err(&spi->dev, "controller data is incorrect");
		goto err_config_params;
	}

	/* Now set controller state based on controller data */
	chip->xfer_type = chip_info->com_mode;
	if (!chip_info->cs_control) {
		chip->cs_control = null_cs_control;
		dev_warn(&spi->dev,
			 "chip select function is NULL for this chip\n");
	} else
		chip->cs_control = chip_info->cs_control;

	if (bits <= 3) {
		/* PL022 doesn't support less than 4-bits */
		status = -ENOTSUPP;
		goto err_config_params;
	} else if (bits <= 8) {
		dev_dbg(&spi->dev, "4 <= n <=8 bits per word\n");
		chip->n_bytes = 1;
		chip->read = READING_U8;
		chip->write = WRITING_U8;
	} else if (bits <= 16) {
		dev_dbg(&spi->dev, "9 <= n <= 16 bits per word\n");
		chip->n_bytes = 2;
		chip->read = READING_U16;
		chip->write = WRITING_U16;
	} else {
		if (pl022->vendor->max_bpw >= 32) {
			dev_dbg(&spi->dev, "17 <= n <= 32 bits per word\n");
			chip->n_bytes = 4;
			chip->read = READING_U32;
			chip->write = WRITING_U32;
		} else {
			dev_err(&spi->dev,
				"illegal data size for this controller!\n");
			dev_err(&spi->dev,
				"a standard pl022 can only handle "
				"1 <= n <= 16 bit words\n");
			status = -ENOTSUPP;
			goto err_config_params;
		}
	}

	/* Now Initialize all register settings required for this chip */
	chip->cr0 = 0;
	chip->cr1 = 0;
	chip->dmacr = 0;
	chip->cpsr = 0;
	if ((chip_info->com_mode == DMA_TRANSFER)
	    && ((pl022->master_info)->enable_dma)) {
		chip->enable_dma = true;
		dev_dbg(&spi->dev, "DMA mode set in controller state\n");
		if (status < 0)
			goto err_config_params;
		SSP_WRITE_BITS(chip->dmacr, SSP_DMA_ENABLED,
			       SSP_DMACR_MASK_RXDMAE, 0);
		SSP_WRITE_BITS(chip->dmacr, SSP_DMA_ENABLED,
			       SSP_DMACR_MASK_TXDMAE, 1);
	} else {
		chip->enable_dma = false;
		dev_dbg(&spi->dev, "DMA mode NOT set in controller state\n");
		SSP_WRITE_BITS(chip->dmacr, SSP_DMA_DISABLED,
			       SSP_DMACR_MASK_RXDMAE, 0);
		SSP_WRITE_BITS(chip->dmacr, SSP_DMA_DISABLED,
			       SSP_DMACR_MASK_TXDMAE, 1);
	}

	chip->cpsr = clk_freq.cpsdvsr;

	/* Special setup for the ST micro extended control registers */
	if (pl022->vendor->extended_cr) {
		u32 etx;

		if (pl022->vendor->pl023) {
			/* These bits are only in the PL023 */
			SSP_WRITE_BITS(chip->cr1, chip_info->clkdelay,
				       SSP_CR1_MASK_FBCLKDEL_ST, 13);
		} else {
			/* These bits are in the PL022 but not PL023 */
			SSP_WRITE_BITS(chip->cr0, chip_info->duplex,
				       SSP_CR0_MASK_HALFDUP_ST, 5);
			SSP_WRITE_BITS(chip->cr0, chip_info->ctrl_len,
				       SSP_CR0_MASK_CSS_ST, 16);
			SSP_WRITE_BITS(chip->cr0, chip_info->iface,
				       SSP_CR0_MASK_FRF_ST, 21);
			SSP_WRITE_BITS(chip->cr1, chip_info->wait_state,
				       SSP_CR1_MASK_MWAIT_ST, 6);
		}
		SSP_WRITE_BITS(chip->cr0, bits - 1,
			       SSP_CR0_MASK_DSS_ST, 0);

		if (spi->mode & SPI_LSB_FIRST) {
			tmp = SSP_RX_LSB;
			etx = SSP_TX_LSB;
		} else {
			tmp = SSP_RX_MSB;
			etx = SSP_TX_MSB;
		}
		SSP_WRITE_BITS(chip->cr1, tmp, SSP_CR1_MASK_RENDN_ST, 4);
		SSP_WRITE_BITS(chip->cr1, etx, SSP_CR1_MASK_TENDN_ST, 5);
		SSP_WRITE_BITS(chip->cr1, chip_info->rx_lev_trig,
			       SSP_CR1_MASK_RXIFLSEL_ST, 7);
		SSP_WRITE_BITS(chip->cr1, chip_info->tx_lev_trig,
			       SSP_CR1_MASK_TXIFLSEL_ST, 10);
	} else {
		SSP_WRITE_BITS(chip->cr0, bits - 1,
			       SSP_CR0_MASK_DSS, 0);
		SSP_WRITE_BITS(chip->cr0, chip_info->iface,
			       SSP_CR0_MASK_FRF, 4);
	}

	/* Stuff that is common for all versions */
	if (spi->mode & SPI_CPOL)
		tmp = SSP_CLK_POL_IDLE_HIGH;
	else
		tmp = SSP_CLK_POL_IDLE_LOW;
	SSP_WRITE_BITS(chip->cr0, tmp, SSP_CR0_MASK_SPO, 6);

	if (spi->mode & SPI_CPHA)
		tmp = SSP_CLK_SECOND_EDGE;
	else
		tmp = SSP_CLK_FIRST_EDGE;
	SSP_WRITE_BITS(chip->cr0, tmp, SSP_CR0_MASK_SPH, 7);

	SSP_WRITE_BITS(chip->cr0, clk_freq.scr, SSP_CR0_MASK_SCR, 8);
	/* Loopback is available on all versions except PL023 */
	if (!pl022->vendor->pl023) {
		if (spi->mode & SPI_LOOP)
			tmp = LOOPBACK_ENABLED;
		else
			tmp = LOOPBACK_DISABLED;
		SSP_WRITE_BITS(chip->cr1, tmp, SSP_CR1_MASK_LBM, 0);
	}
	SSP_WRITE_BITS(chip->cr1, SSP_DISABLED, SSP_CR1_MASK_SSE, 1);
	SSP_WRITE_BITS(chip->cr1, chip_info->hierarchy, SSP_CR1_MASK_MS, 2);
	SSP_WRITE_BITS(chip->cr1, chip_info->slave_tx_disable, SSP_CR1_MASK_SOD, 3);

	/* Save controller_state */
	spi_set_ctldata(spi, chip);
	return status;
 err_config_params:
	spi_set_ctldata(spi, NULL);
	kfree(chip);
	return status;
}

/**
 * pl022_cleanup - cleanup function registered to SPI master framework
 * @spi: spi device which is requesting cleanup
 *
 * This function is registered to the SPI framework for this SPI master
 * controller. It will free the runtime state of chip.
 */
static void pl022_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);

	spi_set_ctldata(spi, NULL);
	kfree(chip);
}


static int __devinit
pl022_probe(struct amba_device *adev, struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct pl022_ssp_controller *platform_info = adev->dev.platform_data;
	struct spi_master *master;
	struct pl022 *pl022 = NULL;	/*Data for this driver */
	int status = 0;

	dev_info(&adev->dev,
		 "ARM PL022 driver, device ID: 0x%08x\n", adev->periphid);
	if (platform_info == NULL) {
		dev_err(&adev->dev, "probe - no platform data supplied\n");
		status = -ENODEV;
		goto err_no_pdata;
	}

	/* Allocate master with space for data */
	master = spi_alloc_master(dev, sizeof(struct pl022));
	if (master == NULL) {
		dev_err(&adev->dev, "probe - cannot alloc SPI master\n");
		status = -ENOMEM;
		goto err_no_master;
	}

	pl022 = spi_master_get_devdata(master);
	pl022->master = master;
	pl022->master_info = platform_info;
	pl022->adev = adev;
	pl022->vendor = id->data;

	/*
	 * Bus Number Which has been Assigned to this SSP controller
	 * on this board
	 */
	master->bus_num = platform_info->bus_id;
	master->num_chipselect = platform_info->num_chipselect;
	master->cleanup = pl022_cleanup;
	master->setup = pl022_setup;
	master->transfer = pl022_transfer;

	/*
	 * Supports mode 0-3, loopback, and active low CS. Transfers are
	 * always MS bit first on the original pl022.
	 */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP;
	if (pl022->vendor->extended_cr)
		master->mode_bits |= SPI_LSB_FIRST;

	dev_dbg(&adev->dev, "BUSNO: %d\n", master->bus_num);

	status = amba_request_regions(adev, NULL);
	if (status)
		goto err_no_ioregion;

	pl022->phybase = adev->res.start;
	pl022->virtbase = ioremap(adev->res.start, resource_size(&adev->res));
	if (pl022->virtbase == NULL) {
		status = -ENOMEM;
		goto err_no_ioremap;
	}
	printk(KERN_INFO "pl022: mapped registers from 0x%08x to %p\n",
	       adev->res.start, pl022->virtbase);

	pl022->clk = clk_get(&adev->dev, NULL);
	if (IS_ERR(pl022->clk)) {
		status = PTR_ERR(pl022->clk);
		dev_err(&adev->dev, "could not retrieve SSP/SPI bus clock\n");
		goto err_no_clk;
	}

	/* Disable SSP */
	writew((readw(SSP_CR1(pl022->virtbase)) & (~SSP_CR1_MASK_SSE)),
	       SSP_CR1(pl022->virtbase));
	load_ssp_default_config(pl022);

	status = request_irq(adev->irq[0], pl022_interrupt_handler, 0, "pl022",
			     pl022);
	if (status < 0) {
		dev_err(&adev->dev, "probe - cannot get IRQ (%d)\n", status);
		goto err_no_irq;
	}

	/* Get DMA channels */
	if (platform_info->enable_dma) {
		status = pl022_dma_probe(pl022);
		if (status != 0)
			goto err_no_dma;
	}

	/* Initialize and start queue */
	status = init_queue(pl022);
	if (status != 0) {
		dev_err(&adev->dev, "probe - problem initializing queue\n");
		goto err_init_queue;
	}
	status = start_queue(pl022);
	if (status != 0) {
		dev_err(&adev->dev, "probe - problem starting queue\n");
		goto err_start_queue;
	}
	/* Register with the SPI framework */
	amba_set_drvdata(adev, pl022);
	status = spi_register_master(master);
	if (status != 0) {
		dev_err(&adev->dev,
			"probe - problem registering spi master\n");
		goto err_spi_register;
	}
	dev_dbg(dev, "probe succeded\n");
	/* Disable the silicon block pclk and clock it when needed */
	amba_pclk_disable(adev);
	return 0;

 err_spi_register:
 err_start_queue:
 err_init_queue:
	destroy_queue(pl022);
	pl022_dma_remove(pl022);
 err_no_dma:
	free_irq(adev->irq[0], pl022);
 err_no_irq:
	clk_put(pl022->clk);
 err_no_clk:
	iounmap(pl022->virtbase);
 err_no_ioremap:
	amba_release_regions(adev);
 err_no_ioregion:
	spi_master_put(master);
 err_no_master:
 err_no_pdata:
	return status;
}

static int __devexit
pl022_remove(struct amba_device *adev)
{
	struct pl022 *pl022 = amba_get_drvdata(adev);
	int status = 0;
	if (!pl022)
		return 0;

	/* Remove the queue */
	status = destroy_queue(pl022);
	if (status != 0) {
		dev_err(&adev->dev,
			"queue remove failed (%d)\n", status);
		return status;
	}
	load_ssp_default_config(pl022);
	pl022_dma_remove(pl022);
	free_irq(adev->irq[0], pl022);
	clk_disable(pl022->clk);
	clk_put(pl022->clk);
	iounmap(pl022->virtbase);
	amba_release_regions(adev);
	tasklet_disable(&pl022->pump_transfers);
	spi_unregister_master(pl022->master);
	spi_master_put(pl022->master);
	amba_set_drvdata(adev, NULL);
	dev_dbg(&adev->dev, "remove succeded\n");
	return 0;
}

#ifdef CONFIG_PM
static int pl022_suspend(struct amba_device *adev, pm_message_t state)
{
	struct pl022 *pl022 = amba_get_drvdata(adev);
	int status = 0;

	status = stop_queue(pl022);
	if (status) {
		dev_warn(&adev->dev, "suspend cannot stop queue\n");
		return status;
	}

	amba_pclk_enable(adev);
	load_ssp_default_config(pl022);
	amba_pclk_disable(adev);
	dev_dbg(&adev->dev, "suspended\n");
	return 0;
}

static int pl022_resume(struct amba_device *adev)
{
	struct pl022 *pl022 = amba_get_drvdata(adev);
	int status = 0;

	/* Start the queue running */
	status = start_queue(pl022);
	if (status)
		dev_err(&adev->dev, "problem starting queue (%d)\n", status);
	else
		dev_dbg(&adev->dev, "resumed\n");

	return status;
}
#else
#define pl022_suspend NULL
#define pl022_resume NULL
#endif	/* CONFIG_PM */

static struct vendor_data vendor_arm = {
	.fifodepth = 8,
	.max_bpw = 16,
	.unidir = false,
	.extended_cr = false,
	.pl023 = false,
};


static struct vendor_data vendor_st = {
	.fifodepth = 32,
	.max_bpw = 32,
	.unidir = false,
	.extended_cr = true,
	.pl023 = false,
};

static struct vendor_data vendor_st_pl023 = {
	.fifodepth = 32,
	.max_bpw = 32,
	.unidir = false,
	.extended_cr = true,
	.pl023 = true,
};

static struct amba_id pl022_ids[] = {
	{
		/*
		 * ARM PL022 variant, this has a 16bit wide
		 * and 8 locations deep TX/RX FIFO
		 */
		.id	= 0x00041022,
		.mask	= 0x000fffff,
		.data	= &vendor_arm,
	},
	{
		/*
		 * ST Micro derivative, this has 32bit wide
		 * and 32 locations deep TX/RX FIFO
		 */
		.id	= 0x01080022,
		.mask	= 0xffffffff,
		.data	= &vendor_st,
	},
	{
		/*
		 * ST-Ericsson derivative "PL023" (this is not
		 * an official ARM number), this is a PL022 SSP block
		 * stripped to SPI mode only, it has 32bit wide
		 * and 32 locations deep TX/RX FIFO but no extended
		 * CR0/CR1 register
		 */
		.id     = 0x00080023,
		.mask   = 0xffffffff,
		.data   = &vendor_st_pl023,
	},
	{ 0, 0 },
};

static struct amba_driver pl022_driver = {
	.drv = {
		.name	= "ssp-pl022",
	},
	.id_table	= pl022_ids,
	.probe		= pl022_probe,
	.remove		= __devexit_p(pl022_remove),
	.suspend        = pl022_suspend,
	.resume         = pl022_resume,
};


static int __init pl022_init(void)
{
	return amba_driver_register(&pl022_driver);
}

subsys_initcall(pl022_init);

static void __exit pl022_exit(void)
{
	amba_driver_unregister(&pl022_driver);
}

module_exit(pl022_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("PL022 SSP Controller Driver");
MODULE_LICENSE("GPL");
