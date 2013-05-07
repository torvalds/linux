/*
 * A driver for the ARM PL022 PrimeCell SSP/SPI bus master.
 *
 * Copyright (C) 2008-2012 ST-Ericsson AB
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
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
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>

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

#define SPI_POLLING_TIMEOUT 1000

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
	bool loopback;
};

/**
 * struct pl022 - This is the private SSP driver data structure
 * @adev: AMBA device model hookup
 * @vendor: vendor data for the IP block
 * @phybase: the physical memory where the SSP device resides
 * @virtbase: the virtual memory where the SSP is mapped
 * @clk: outgoing clock "SPICLK" for the SPI bus
 * @master: SPI framework hookup
 * @master_info: controller-specific data from machine setup
 * @kworker: thread struct for message pump
 * @kworker_task: pointer to task for message pump kworker thread
 * @pump_messages: work struct for scheduling work to the message pump
 * @queue_lock: spinlock to syncronise access to message queue
 * @queue: message queue
 * @busy: message pump is busy
 * @running: message pump is running
 * @pump_transfers: Tasklet used in Interrupt Transfer mode
 * @cur_msg: Pointer to current spi_message being processed
 * @cur_transfer: Pointer to current spi_transfer
 * @cur_chip: pointer to current clients chip(assigned from controller_state)
 * @next_msg_cs_active: the next message in the queue has been examined
 *  and it was found that it uses the same chip select as the previous
 *  message, so we left it active after the previous transfer, and it's
 *  active already.
 * @tx: current position in TX buffer to be read
 * @tx_end: end position in TX buffer to be read
 * @rx: current position in RX buffer to be written
 * @rx_end: end position in RX buffer to be written
 * @read: the type of read currently going on
 * @write: the type of write currently going on
 * @exp_fifo_level: expected FIFO level
 * @dma_rx_channel: optional channel for RX DMA
 * @dma_tx_channel: optional channel for TX DMA
 * @sgt_rx: scattertable for the RX transfer
 * @sgt_tx: scattertable for the TX transfer
 * @dummypage: a dummy page used for driving data on the bus with DMA
 * @cur_cs: current chip select (gpio)
 * @chipselects: list of chipselects (gpios)
 */
struct pl022 {
	struct amba_device		*adev;
	struct vendor_data		*vendor;
	resource_size_t			phybase;
	void __iomem			*virtbase;
	struct clk			*clk;
	/* Two optional pin states - default & sleep */
	struct pinctrl			*pinctrl;
	struct pinctrl_state		*pins_default;
	struct pinctrl_state		*pins_idle;
	struct pinctrl_state		*pins_sleep;
	struct spi_master		*master;
	struct pl022_ssp_controller	*master_info;
	/* Message per-transfer pump */
	struct tasklet_struct		pump_transfers;
	struct spi_message		*cur_msg;
	struct spi_transfer		*cur_transfer;
	struct chip_data		*cur_chip;
	bool				next_msg_cs_active;
	void				*tx;
	void				*tx_end;
	void				*rx;
	void				*rx_end;
	enum ssp_reading		read;
	enum ssp_writing		write;
	u32				exp_fifo_level;
	enum ssp_rx_level_trig		rx_lev_trig;
	enum ssp_tx_level_trig		tx_lev_trig;
	/* DMA settings */
#ifdef CONFIG_DMA_ENGINE
	struct dma_chan			*dma_rx_channel;
	struct dma_chan			*dma_tx_channel;
	struct sg_table			sgt_rx;
	struct sg_table			sgt_tx;
	char				*dummypage;
	bool				dma_running;
#endif
	int cur_cs;
	int *chipselects;
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
 * @read: function ptr to be used to read when doing xfer for this chip
 * @write: function ptr to be used to write when doing xfer for this chip
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

static void pl022_cs_control(struct pl022 *pl022, u32 command)
{
	if (gpio_is_valid(pl022->cur_cs))
		gpio_set_value(pl022->cur_cs, command);
	else
		pl022->cur_chip->cs_control(command);
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
	pl022->next_msg_cs_active = false;

	last_transfer = list_entry(pl022->cur_msg->transfers.prev,
					struct spi_transfer,
					transfer_list);

	/* Delay if requested before any change in chip select */
	if (last_transfer->delay_usecs)
		/*
		 * FIXME: This runs in interrupt context.
		 * Is this really smart?
		 */
		udelay(last_transfer->delay_usecs);

	if (!last_transfer->cs_change) {
		struct spi_message *next_msg;

		/*
		 * cs_change was not set. We can keep the chip select
		 * enabled if there is message in the queue and it is
		 * for the same spi device.
		 *
		 * We cannot postpone this until pump_messages, because
		 * after calling msg->complete (below) the driver that
		 * sent the current message could be unloaded, which
		 * could invalidate the cs_control() callback...
		 */
		/* get a pointer to the next message, if any */
		next_msg = spi_get_next_queued_message(pl022->master);

		/*
		 * see if the next and current messages point
		 * to the same spi device.
		 */
		if (next_msg && next_msg->spi != pl022->cur_msg->spi)
			next_msg = NULL;
		if (!next_msg || pl022->cur_msg->state == STATE_ERROR)
			pl022_cs_control(pl022, SSP_CHIP_DESELECT);
		else
			pl022->next_msg_cs_active = true;

	}

	pl022->cur_msg = NULL;
	pl022->cur_transfer = NULL;
	pl022->cur_chip = NULL;
	spi_finalize_current_message(pl022->master);

	/* disable the SPI/SSP operation */
	writew((readw(SSP_CR1(pl022->virtbase)) &
		(~SSP_CR1_MASK_SSE)), SSP_CR1(pl022->virtbase));

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
	 * The FIFO depth is different between primecell variants.
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
		 * and then things are continuously clocked into the RX FIFO.
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
	dma_unmap_sg(pl022->dma_tx_channel->device->dev, pl022->sgt_tx.sgl,
		     pl022->sgt_tx.nents, DMA_TO_DEVICE);
	dma_unmap_sg(pl022->dma_rx_channel->device->dev, pl022->sgt_rx.sgl,
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

	/* Update total bytes transferred */
	msg->actual_length += pl022->cur_transfer->len;
	if (pl022->cur_transfer->cs_change)
		pl022_cs_control(pl022, SSP_CHIP_DESELECT);

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
		.direction = DMA_DEV_TO_MEM,
		.device_fc = false,
	};
	struct dma_slave_config tx_conf = {
		.dst_addr = SSP_DR(pl022->phybase),
		.direction = DMA_MEM_TO_DEV,
		.device_fc = false,
	};
	unsigned int pages;
	int ret;
	int rx_sglen, tx_sglen;
	struct dma_chan *rxchan = pl022->dma_rx_channel;
	struct dma_chan *txchan = pl022->dma_tx_channel;
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_async_tx_descriptor *txdesc;

	/* Check that the channels are available */
	if (!rxchan || !txchan)
		return -ENODEV;

	/*
	 * If supplied, the DMA burstsize should equal the FIFO trigger level.
	 * Notice that the DMA engine uses one-to-one mapping. Since we can
	 * not trigger on 2 elements this needs explicit mapping rather than
	 * calculation.
	 */
	switch (pl022->rx_lev_trig) {
	case SSP_RX_1_OR_MORE_ELEM:
		rx_conf.src_maxburst = 1;
		break;
	case SSP_RX_4_OR_MORE_ELEM:
		rx_conf.src_maxburst = 4;
		break;
	case SSP_RX_8_OR_MORE_ELEM:
		rx_conf.src_maxburst = 8;
		break;
	case SSP_RX_16_OR_MORE_ELEM:
		rx_conf.src_maxburst = 16;
		break;
	case SSP_RX_32_OR_MORE_ELEM:
		rx_conf.src_maxburst = 32;
		break;
	default:
		rx_conf.src_maxburst = pl022->vendor->fifodepth >> 1;
		break;
	}

	switch (pl022->tx_lev_trig) {
	case SSP_TX_1_OR_MORE_EMPTY_LOC:
		tx_conf.dst_maxburst = 1;
		break;
	case SSP_TX_4_OR_MORE_EMPTY_LOC:
		tx_conf.dst_maxburst = 4;
		break;
	case SSP_TX_8_OR_MORE_EMPTY_LOC:
		tx_conf.dst_maxburst = 8;
		break;
	case SSP_TX_16_OR_MORE_EMPTY_LOC:
		tx_conf.dst_maxburst = 16;
		break;
	case SSP_TX_32_OR_MORE_EMPTY_LOC:
		tx_conf.dst_maxburst = 32;
		break;
	default:
		tx_conf.dst_maxburst = pl022->vendor->fifodepth >> 1;
		break;
	}

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
		tx_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	}

	/* SPI pecularity: we need to read and write the same width */
	if (rx_conf.src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		rx_conf.src_addr_width = tx_conf.dst_addr_width;
	if (tx_conf.dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
		tx_conf.dst_addr_width = rx_conf.src_addr_width;
	BUG_ON(rx_conf.src_addr_width != tx_conf.dst_addr_width);

	dmaengine_slave_config(rxchan, &rx_conf);
	dmaengine_slave_config(txchan, &tx_conf);

	/* Create sglists for the transfers */
	pages = DIV_ROUND_UP(pl022->cur_transfer->len, PAGE_SIZE);
	dev_dbg(&pl022->adev->dev, "using %d pages for transfer\n", pages);

	ret = sg_alloc_table(&pl022->sgt_rx, pages, GFP_ATOMIC);
	if (ret)
		goto err_alloc_rx_sg;

	ret = sg_alloc_table(&pl022->sgt_tx, pages, GFP_ATOMIC);
	if (ret)
		goto err_alloc_tx_sg;

	/* Fill in the scatterlists for the RX+TX buffers */
	setup_dma_scatter(pl022, pl022->rx,
			  pl022->cur_transfer->len, &pl022->sgt_rx);
	setup_dma_scatter(pl022, pl022->tx,
			  pl022->cur_transfer->len, &pl022->sgt_tx);

	/* Map DMA buffers */
	rx_sglen = dma_map_sg(rxchan->device->dev, pl022->sgt_rx.sgl,
			   pl022->sgt_rx.nents, DMA_FROM_DEVICE);
	if (!rx_sglen)
		goto err_rx_sgmap;

	tx_sglen = dma_map_sg(txchan->device->dev, pl022->sgt_tx.sgl,
			   pl022->sgt_tx.nents, DMA_TO_DEVICE);
	if (!tx_sglen)
		goto err_tx_sgmap;

	/* Send both scatterlists */
	rxdesc = dmaengine_prep_slave_sg(rxchan,
				      pl022->sgt_rx.sgl,
				      rx_sglen,
				      DMA_DEV_TO_MEM,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc)
		goto err_rxdesc;

	txdesc = dmaengine_prep_slave_sg(txchan,
				      pl022->sgt_tx.sgl,
				      tx_sglen,
				      DMA_MEM_TO_DEV,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		goto err_txdesc;

	/* Put the callback on the RX transfer only, that should finish last */
	rxdesc->callback = dma_callback;
	rxdesc->callback_param = pl022;

	/* Submit and fire RX and TX with TX last so we're ready to read! */
	dmaengine_submit(rxdesc);
	dmaengine_submit(txdesc);
	dma_async_issue_pending(rxchan);
	dma_async_issue_pending(txchan);
	pl022->dma_running = true;

	return 0;

err_txdesc:
	dmaengine_terminate_all(txchan);
err_rxdesc:
	dmaengine_terminate_all(rxchan);
	dma_unmap_sg(txchan->device->dev, pl022->sgt_tx.sgl,
		     pl022->sgt_tx.nents, DMA_TO_DEVICE);
err_tx_sgmap:
	dma_unmap_sg(rxchan->device->dev, pl022->sgt_rx.sgl,
		     pl022->sgt_tx.nents, DMA_FROM_DEVICE);
err_rx_sgmap:
	sg_free_table(&pl022->sgt_tx);
err_alloc_tx_sg:
	sg_free_table(&pl022->sgt_rx);
err_alloc_rx_sg:
	return -ENOMEM;
}

static int pl022_dma_probe(struct pl022 *pl022)
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
		dev_dbg(&pl022->adev->dev, "no RX DMA channel!\n");
		goto err_no_rxchan;
	}

	pl022->dma_tx_channel = dma_request_channel(mask,
					    pl022->master_info->dma_filter,
					    pl022->master_info->dma_tx_param);
	if (!pl022->dma_tx_channel) {
		dev_dbg(&pl022->adev->dev, "no TX DMA channel!\n");
		goto err_no_txchan;
	}

	pl022->dummypage = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pl022->dummypage) {
		dev_dbg(&pl022->adev->dev, "no DMA dummypage!\n");
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
	dev_err(&pl022->adev->dev,
			"Failed to work in dma mode, work without dma!\n");
	return -ENODEV;
}

static int pl022_dma_autoprobe(struct pl022 *pl022)
{
	struct device *dev = &pl022->adev->dev;

	/* automatically configure DMA channels from platform, normally using DT */
	pl022->dma_rx_channel = dma_request_slave_channel(dev, "rx");
	if (!pl022->dma_rx_channel)
		goto err_no_rxchan;

	pl022->dma_tx_channel = dma_request_slave_channel(dev, "tx");
	if (!pl022->dma_tx_channel)
		goto err_no_txchan;

	pl022->dummypage = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pl022->dummypage)
		goto err_no_dummypage;

	return 0;

err_no_dummypage:
	dma_release_channel(pl022->dma_tx_channel);
	pl022->dma_tx_channel = NULL;
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

	dmaengine_terminate_all(rxchan);
	dmaengine_terminate_all(txchan);
	unmap_free_dma_scatter(pl022);
	pl022->dma_running = false;
}

static void pl022_dma_remove(struct pl022 *pl022)
{
	if (pl022->dma_running)
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

static inline int pl022_dma_autoprobe(struct pl022 *pl022)
{
	return 0;
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
		/* Disable Transmit interrupt, enable receive interrupt */
		writew((readw(SSP_IMSC(pl022->virtbase)) &
		       ~SSP_IMSC_MASK_TXIM) | SSP_IMSC_MASK_RXIM,
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
		/* Update total bytes transferred */
		msg->actual_length += pl022->cur_transfer->len;
		if (pl022->cur_transfer->cs_change)
			pl022_cs_control(pl022, SSP_CHIP_DESELECT);
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

		/* Reselect chip select only if cs_change was requested */
		if (previous->cs_change)
			pl022_cs_control(pl022, SSP_CHIP_SELECT);
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
	/* enable all interrupts except RX */
	writew(ENABLE_ALL_INTERRUPTS & ~SSP_IMSC_MASK_RXIM, SSP_IMSC(pl022->virtbase));
}

static void do_interrupt_dma_transfer(struct pl022 *pl022)
{
	/*
	 * Default is to enable all interrupts except RX -
	 * this will be enabled once TX is complete
	 */
	u32 irqflags = ENABLE_ALL_INTERRUPTS & ~SSP_IMSC_MASK_RXIM;

	/* Enable target chip, if not already active */
	if (!pl022->next_msg_cs_active)
		pl022_cs_control(pl022, SSP_CHIP_SELECT);

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
	unsigned long time, timeout;

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
				pl022_cs_control(pl022, SSP_CHIP_SELECT);
		} else {
			/* STATE_START */
			message->state = STATE_RUNNING;
			if (!pl022->next_msg_cs_active)
				pl022_cs_control(pl022, SSP_CHIP_SELECT);
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

		timeout = jiffies + msecs_to_jiffies(SPI_POLLING_TIMEOUT);
		while (pl022->tx < pl022->tx_end || pl022->rx < pl022->rx_end) {
			time = jiffies;
			readwriter(pl022);
			if (time_after(time, timeout)) {
				dev_warn(&pl022->adev->dev,
				"%s: timeout!\n", __func__);
				message->state = STATE_ERROR;
				goto out;
			}
			cpu_relax();
		}

		/* Update total byte transferred */
		message->actual_length += pl022->cur_transfer->len;
		if (pl022->cur_transfer->cs_change)
			pl022_cs_control(pl022, SSP_CHIP_DESELECT);
		/* Move to next transfer */
		message->state = next_transfer(pl022);
	}
out:
	/* Handle end of message */
	if (message->state == STATE_DONE)
		message->status = 0;
	else
		message->status = -EIO;

	giveback(pl022);
	return;
}

static int pl022_transfer_one_message(struct spi_master *master,
				      struct spi_message *msg)
{
	struct pl022 *pl022 = spi_master_get_devdata(master);

	/* Initial message state */
	pl022->cur_msg = msg;
	msg->state = STATE_START;

	pl022->cur_transfer = list_entry(msg->transfers.next,
					 struct spi_transfer, transfer_list);

	/* Setup the SPI using the per chip configuration */
	pl022->cur_chip = spi_get_ctldata(msg->spi);
	pl022->cur_cs = pl022->chipselects[msg->spi->chip_select];

	restore_state(pl022);
	flush(pl022);

	if (pl022->cur_chip->xfer_type == POLLING_TRANSFER)
		do_polling_transfer(pl022);
	else
		do_interrupt_dma_transfer(pl022);

	return 0;
}

static int pl022_prepare_transfer_hardware(struct spi_master *master)
{
	struct pl022 *pl022 = spi_master_get_devdata(master);

	/*
	 * Just make sure we have all we need to run the transfer by syncing
	 * with the runtime PM framework.
	 */
	pm_runtime_get_sync(&pl022->adev->dev);
	return 0;
}

static int pl022_unprepare_transfer_hardware(struct spi_master *master)
{
	struct pl022 *pl022 = spi_master_get_devdata(master);

	/* nothing more to do - disable spi/ssp and power off */
	writew((readw(SSP_CR1(pl022->virtbase)) &
		(~SSP_CR1_MASK_SSE)), SSP_CR1(pl022->virtbase));

	if (pl022->master_info->autosuspend_delay > 0) {
		pm_runtime_mark_last_busy(&pl022->adev->dev);
		pm_runtime_put_autosuspend(&pl022->adev->dev);
	} else {
		pm_runtime_put(&pl022->adev->dev);
	}

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
	switch (chip_info->rx_lev_trig) {
	case SSP_RX_1_OR_MORE_ELEM:
	case SSP_RX_4_OR_MORE_ELEM:
	case SSP_RX_8_OR_MORE_ELEM:
		/* These are always OK, all variants can handle this */
		break;
	case SSP_RX_16_OR_MORE_ELEM:
		if (pl022->vendor->fifodepth < 16) {
			dev_err(&pl022->adev->dev,
			"RX FIFO Trigger Level is configured incorrectly\n");
			return -EINVAL;
		}
		break;
	case SSP_RX_32_OR_MORE_ELEM:
		if (pl022->vendor->fifodepth < 32) {
			dev_err(&pl022->adev->dev,
			"RX FIFO Trigger Level is configured incorrectly\n");
			return -EINVAL;
		}
		break;
	default:
		dev_err(&pl022->adev->dev,
			"RX FIFO Trigger Level is configured incorrectly\n");
		return -EINVAL;
		break;
	}
	switch (chip_info->tx_lev_trig) {
	case SSP_TX_1_OR_MORE_EMPTY_LOC:
	case SSP_TX_4_OR_MORE_EMPTY_LOC:
	case SSP_TX_8_OR_MORE_EMPTY_LOC:
		/* These are always OK, all variants can handle this */
		break;
	case SSP_TX_16_OR_MORE_EMPTY_LOC:
		if (pl022->vendor->fifodepth < 16) {
			dev_err(&pl022->adev->dev,
			"TX FIFO Trigger Level is configured incorrectly\n");
			return -EINVAL;
		}
		break;
	case SSP_TX_32_OR_MORE_EMPTY_LOC:
		if (pl022->vendor->fifodepth < 32) {
			dev_err(&pl022->adev->dev,
			"TX FIFO Trigger Level is configured incorrectly\n");
			return -EINVAL;
		}
		break;
	default:
		dev_err(&pl022->adev->dev,
			"TX FIFO Trigger Level is configured incorrectly\n");
		return -EINVAL;
		break;
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

static inline u32 spi_rate(u32 rate, u16 cpsdvsr, u16 scr)
{
	return rate / (cpsdvsr * (1 + scr));
}

static int calculate_effective_freq(struct pl022 *pl022, int freq, struct
				    ssp_clock_params * clk_freq)
{
	/* Lets calculate the frequency parameters */
	u16 cpsdvsr = CPSDVR_MIN, scr = SCR_MIN;
	u32 rate, max_tclk, min_tclk, best_freq = 0, best_cpsdvsr = 0,
		best_scr = 0, tmp, found = 0;

	rate = clk_get_rate(pl022->clk);
	/* cpsdvscr = 2 & scr 0 */
	max_tclk = spi_rate(rate, CPSDVR_MIN, SCR_MIN);
	/* cpsdvsr = 254 & scr = 255 */
	min_tclk = spi_rate(rate, CPSDVR_MAX, SCR_MAX);

	if (freq > max_tclk)
		dev_warn(&pl022->adev->dev,
			"Max speed that can be programmed is %d Hz, you requested %d\n",
			max_tclk, freq);

	if (freq < min_tclk) {
		dev_err(&pl022->adev->dev,
			"Requested frequency: %d Hz is less than minimum possible %d Hz\n",
			freq, min_tclk);
		return -EINVAL;
	}

	/*
	 * best_freq will give closest possible available rate (<= requested
	 * freq) for all values of scr & cpsdvsr.
	 */
	while ((cpsdvsr <= CPSDVR_MAX) && !found) {
		while (scr <= SCR_MAX) {
			tmp = spi_rate(rate, cpsdvsr, scr);

			if (tmp > freq) {
				/* we need lower freq */
				scr++;
				continue;
			}

			/*
			 * If found exact value, mark found and break.
			 * If found more closer value, update and break.
			 */
			if (tmp > best_freq) {
				best_freq = tmp;
				best_cpsdvsr = cpsdvsr;
				best_scr = scr;

				if (tmp == freq)
					found = 1;
			}
			/*
			 * increased scr will give lower rates, which are not
			 * required
			 */
			break;
		}
		cpsdvsr += 2;
		scr = SCR_MIN;
	}

	WARN(!best_freq, "pl022: Matching cpsdvsr and scr not found for %d Hz rate \n",
			freq);

	clk_freq->cpsdvsr = (u8) (best_cpsdvsr & 0xFF);
	clk_freq->scr = (u8) (best_scr & 0xFF);
	dev_dbg(&pl022->adev->dev,
		"SSP Target Frequency is: %u, Effective Frequency is %u\n",
		freq, best_freq);
	dev_dbg(&pl022->adev->dev, "SSP cpsdvsr = %d, scr = %d\n",
		clk_freq->cpsdvsr, clk_freq->scr);

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
	struct pl022_config_chip chip_info_dt;
	struct chip_data *chip;
	struct ssp_clock_params clk_freq = { .cpsdvsr = 0, .scr = 0};
	int status = 0;
	struct pl022 *pl022 = spi_master_get_devdata(spi->master);
	unsigned int bits = spi->bits_per_word;
	u32 tmp;
	struct device_node *np = spi->dev.of_node;

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
		if (np) {
			chip_info_dt = pl022_default_chip_info;

			chip_info_dt.hierarchy = SSP_MASTER;
			of_property_read_u32(np, "pl022,interface",
				&chip_info_dt.iface);
			of_property_read_u32(np, "pl022,com-mode",
				&chip_info_dt.com_mode);
			of_property_read_u32(np, "pl022,rx-level-trig",
				&chip_info_dt.rx_lev_trig);
			of_property_read_u32(np, "pl022,tx-level-trig",
				&chip_info_dt.tx_lev_trig);
			of_property_read_u32(np, "pl022,ctrl-len",
				&chip_info_dt.ctrl_len);
			of_property_read_u32(np, "pl022,wait-state",
				&chip_info_dt.wait_state);
			of_property_read_u32(np, "pl022,duplex",
				&chip_info_dt.duplex);

			chip_info = &chip_info_dt;
		} else {
			chip_info = &pl022_default_chip_info;
			/* spi_board_info.controller_data not is supplied */
			dev_dbg(&spi->dev,
				"using default controller_data settings\n");
		}
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
		status = -EINVAL;
		dev_err(&spi->dev,
			"cpsdvsr is configured incorrectly\n");
		goto err_config_params;
	}

	status = verify_controller_parameters(pl022, chip_info);
	if (status) {
		dev_err(&spi->dev, "controller data is incorrect");
		goto err_config_params;
	}

	pl022->rx_lev_trig = chip_info->rx_lev_trig;
	pl022->tx_lev_trig = chip_info->tx_lev_trig;

	/* Now set controller state based on controller data */
	chip->xfer_type = chip_info->com_mode;
	if (!chip_info->cs_control) {
		chip->cs_control = null_cs_control;
		if (!gpio_is_valid(pl022->chipselects[spi->chip_select]))
			dev_warn(&spi->dev,
				 "invalid chip select\n");
	} else
		chip->cs_control = chip_info->cs_control;

	/* Check bits per word with vendor specific range */
	if ((bits <= 3) || (bits > pl022->vendor->max_bpw)) {
		status = -ENOTSUPP;
		dev_err(&spi->dev, "illegal data size for this controller!\n");
		dev_err(&spi->dev, "This controller can only handle 4 <= n <= %d bit words\n",
				pl022->vendor->max_bpw);
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
		dev_dbg(&spi->dev, "17 <= n <= 32 bits per word\n");
		chip->n_bytes = 4;
		chip->read = READING_U32;
		chip->write = WRITING_U32;
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
	if (pl022->vendor->loopback) {
		if (spi->mode & SPI_LOOP)
			tmp = LOOPBACK_ENABLED;
		else
			tmp = LOOPBACK_DISABLED;
		SSP_WRITE_BITS(chip->cr1, tmp, SSP_CR1_MASK_LBM, 0);
	}
	SSP_WRITE_BITS(chip->cr1, SSP_DISABLED, SSP_CR1_MASK_SSE, 1);
	SSP_WRITE_BITS(chip->cr1, chip_info->hierarchy, SSP_CR1_MASK_MS, 2);
	SSP_WRITE_BITS(chip->cr1, chip_info->slave_tx_disable, SSP_CR1_MASK_SOD,
		3);

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

static struct pl022_ssp_controller *
pl022_platform_data_dt_get(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct pl022_ssp_controller *pd;
	u32 tmp;

	if (!np) {
		dev_err(dev, "no dt node defined\n");
		return NULL;
	}

	pd = devm_kzalloc(dev, sizeof(struct pl022_ssp_controller), GFP_KERNEL);
	if (!pd) {
		dev_err(dev, "cannot allocate platform data memory\n");
		return NULL;
	}

	pd->bus_id = -1;
	of_property_read_u32(np, "num-cs", &tmp);
	pd->num_chipselect = tmp;
	of_property_read_u32(np, "pl022,autosuspend-delay",
			     &pd->autosuspend_delay);
	pd->rt = of_property_read_bool(np, "pl022,rt");

	return pd;
}

static int pl022_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct pl022_ssp_controller *platform_info = adev->dev.platform_data;
	struct spi_master *master;
	struct pl022 *pl022 = NULL;	/*Data for this driver */
	struct device_node *np = adev->dev.of_node;
	int status = 0, i, num_cs;

	dev_info(&adev->dev,
		 "ARM PL022 driver, device ID: 0x%08x\n", adev->periphid);
	if (!platform_info && IS_ENABLED(CONFIG_OF))
		platform_info = pl022_platform_data_dt_get(dev);

	if (!platform_info) {
		dev_err(dev, "probe: no platform data defined\n");
		return -ENODEV;
	}

	if (platform_info->num_chipselect) {
		num_cs = platform_info->num_chipselect;
	} else {
		dev_err(dev, "probe: no chip select defined\n");
		return -ENODEV;
	}

	/* Allocate master with space for data */
	master = spi_alloc_master(dev, sizeof(struct pl022));
	if (master == NULL) {
		dev_err(&adev->dev, "probe - cannot alloc SPI master\n");
		return -ENOMEM;
	}

	pl022 = spi_master_get_devdata(master);
	pl022->master = master;
	pl022->master_info = platform_info;
	pl022->adev = adev;
	pl022->vendor = id->data;
	pl022->chipselects = devm_kzalloc(dev, num_cs * sizeof(int),
					  GFP_KERNEL);

	pl022->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pl022->pinctrl)) {
		status = PTR_ERR(pl022->pinctrl);
		goto err_no_pinctrl;
	}

	pl022->pins_default = pinctrl_lookup_state(pl022->pinctrl,
						 PINCTRL_STATE_DEFAULT);
	/* enable pins to be muxed in and configured */
	if (!IS_ERR(pl022->pins_default)) {
		status = pinctrl_select_state(pl022->pinctrl,
				pl022->pins_default);
		if (status)
			dev_err(dev, "could not set default pins\n");
	} else
		dev_err(dev, "could not get default pinstate\n");

	pl022->pins_idle = pinctrl_lookup_state(pl022->pinctrl,
					      PINCTRL_STATE_IDLE);
	if (IS_ERR(pl022->pins_idle))
		dev_dbg(dev, "could not get idle pinstate\n");

	pl022->pins_sleep = pinctrl_lookup_state(pl022->pinctrl,
					       PINCTRL_STATE_SLEEP);
	if (IS_ERR(pl022->pins_sleep))
		dev_dbg(dev, "could not get sleep pinstate\n");

	/*
	 * Bus Number Which has been Assigned to this SSP controller
	 * on this board
	 */
	master->bus_num = platform_info->bus_id;
	master->num_chipselect = num_cs;
	master->cleanup = pl022_cleanup;
	master->setup = pl022_setup;
	master->prepare_transfer_hardware = pl022_prepare_transfer_hardware;
	master->transfer_one_message = pl022_transfer_one_message;
	master->unprepare_transfer_hardware = pl022_unprepare_transfer_hardware;
	master->rt = platform_info->rt;
	master->dev.of_node = dev->of_node;

	if (platform_info->num_chipselect && platform_info->chipselects) {
		for (i = 0; i < num_cs; i++)
			pl022->chipselects[i] = platform_info->chipselects[i];
	} else if (IS_ENABLED(CONFIG_OF)) {
		for (i = 0; i < num_cs; i++) {
			int cs_gpio = of_get_named_gpio(np, "cs-gpios", i);

			if (cs_gpio == -EPROBE_DEFER) {
				status = -EPROBE_DEFER;
				goto err_no_gpio;
			}

			pl022->chipselects[i] = cs_gpio;

			if (gpio_is_valid(cs_gpio)) {
				if (devm_gpio_request(dev, cs_gpio, "ssp-pl022"))
					dev_err(&adev->dev,
						"could not request %d gpio\n",
						cs_gpio);
				else if (gpio_direction_output(cs_gpio, 1))
					dev_err(&adev->dev,
						"could set gpio %d as output\n",
						cs_gpio);
			}
		}
	}

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
	pl022->virtbase = devm_ioremap(dev, adev->res.start,
				       resource_size(&adev->res));
	if (pl022->virtbase == NULL) {
		status = -ENOMEM;
		goto err_no_ioremap;
	}
	printk(KERN_INFO "pl022: mapped registers from 0x%08x to %p\n",
	       adev->res.start, pl022->virtbase);

	pl022->clk = devm_clk_get(&adev->dev, NULL);
	if (IS_ERR(pl022->clk)) {
		status = PTR_ERR(pl022->clk);
		dev_err(&adev->dev, "could not retrieve SSP/SPI bus clock\n");
		goto err_no_clk;
	}

	status = clk_prepare(pl022->clk);
	if (status) {
		dev_err(&adev->dev, "could not prepare SSP/SPI bus clock\n");
		goto  err_clk_prep;
	}

	status = clk_enable(pl022->clk);
	if (status) {
		dev_err(&adev->dev, "could not enable SSP/SPI bus clock\n");
		goto err_no_clk_en;
	}

	/* Initialize transfer pump */
	tasklet_init(&pl022->pump_transfers, pump_transfers,
		     (unsigned long)pl022);

	/* Disable SSP */
	writew((readw(SSP_CR1(pl022->virtbase)) & (~SSP_CR1_MASK_SSE)),
	       SSP_CR1(pl022->virtbase));
	load_ssp_default_config(pl022);

	status = devm_request_irq(dev, adev->irq[0], pl022_interrupt_handler,
				  0, "pl022", pl022);
	if (status < 0) {
		dev_err(&adev->dev, "probe - cannot get IRQ (%d)\n", status);
		goto err_no_irq;
	}

	/* Get DMA channels, try autoconfiguration first */
	status = pl022_dma_autoprobe(pl022);

	/* If that failed, use channels from platform_info */
	if (status == 0)
		platform_info->enable_dma = 1;
	else if (platform_info->enable_dma) {
		status = pl022_dma_probe(pl022);
		if (status != 0)
			platform_info->enable_dma = 0;
	}

	/* Register with the SPI framework */
	amba_set_drvdata(adev, pl022);
	status = spi_register_master(master);
	if (status != 0) {
		dev_err(&adev->dev,
			"probe - problem registering spi master\n");
		goto err_spi_register;
	}
	dev_dbg(dev, "probe succeeded\n");

	/* let runtime pm put suspend */
	if (platform_info->autosuspend_delay > 0) {
		dev_info(&adev->dev,
			"will use autosuspend for runtime pm, delay %dms\n",
			platform_info->autosuspend_delay);
		pm_runtime_set_autosuspend_delay(dev,
			platform_info->autosuspend_delay);
		pm_runtime_use_autosuspend(dev);
	}
	pm_runtime_put(dev);

	return 0;

 err_spi_register:
	if (platform_info->enable_dma)
		pl022_dma_remove(pl022);
 err_no_irq:
	clk_disable(pl022->clk);
 err_no_clk_en:
	clk_unprepare(pl022->clk);
 err_clk_prep:
 err_no_clk:
 err_no_ioremap:
	amba_release_regions(adev);
 err_no_ioregion:
 err_no_gpio:
 err_no_pinctrl:
	spi_master_put(master);
	return status;
}

static int
pl022_remove(struct amba_device *adev)
{
	struct pl022 *pl022 = amba_get_drvdata(adev);

	if (!pl022)
		return 0;

	/*
	 * undo pm_runtime_put() in probe.  I assume that we're not
	 * accessing the primecell here.
	 */
	pm_runtime_get_noresume(&adev->dev);

	load_ssp_default_config(pl022);
	if (pl022->master_info->enable_dma)
		pl022_dma_remove(pl022);

	clk_disable(pl022->clk);
	clk_unprepare(pl022->clk);
	amba_release_regions(adev);
	tasklet_disable(&pl022->pump_transfers);
	spi_unregister_master(pl022->master);
	amba_set_drvdata(adev, NULL);
	return 0;
}

#if defined(CONFIG_SUSPEND) || defined(CONFIG_PM_RUNTIME)
/*
 * These two functions are used from both suspend/resume and
 * the runtime counterparts to handle external resources like
 * clocks, pins and regulators when going to sleep.
 */
static void pl022_suspend_resources(struct pl022 *pl022, bool runtime)
{
	int ret;
	struct pinctrl_state *pins_state;

	clk_disable(pl022->clk);

	pins_state = runtime ? pl022->pins_idle : pl022->pins_sleep;
	/* Optionally let pins go into sleep states */
	if (!IS_ERR(pins_state)) {
		ret = pinctrl_select_state(pl022->pinctrl, pins_state);
		if (ret)
			dev_err(&pl022->adev->dev, "could not set %s pins\n",
				runtime ? "idle" : "sleep");
	}
}

static void pl022_resume_resources(struct pl022 *pl022, bool runtime)
{
	int ret;

	/* Optionaly enable pins to be muxed in and configured */
	/* First go to the default state */
	if (!IS_ERR(pl022->pins_default)) {
		ret = pinctrl_select_state(pl022->pinctrl, pl022->pins_default);
		if (ret)
			dev_err(&pl022->adev->dev,
				"could not set default pins\n");
	}

	if (!runtime) {
		/* Then let's idle the pins until the next transfer happens */
		if (!IS_ERR(pl022->pins_idle)) {
			ret = pinctrl_select_state(pl022->pinctrl,
					pl022->pins_idle);
		if (ret)
			dev_err(&pl022->adev->dev,
				"could not set idle pins\n");
		}
	}

	clk_enable(pl022->clk);
}
#endif

#ifdef CONFIG_SUSPEND
static int pl022_suspend(struct device *dev)
{
	struct pl022 *pl022 = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_suspend(pl022->master);
	if (ret) {
		dev_warn(dev, "cannot suspend master\n");
		return ret;
	}

	pm_runtime_get_sync(dev);
	pl022_suspend_resources(pl022, false);

	dev_dbg(dev, "suspended\n");
	return 0;
}

static int pl022_resume(struct device *dev)
{
	struct pl022 *pl022 = dev_get_drvdata(dev);
	int ret;

	pl022_resume_resources(pl022, false);
	pm_runtime_put(dev);

	/* Start the queue running */
	ret = spi_master_resume(pl022->master);
	if (ret)
		dev_err(dev, "problem starting queue (%d)\n", ret);
	else
		dev_dbg(dev, "resumed\n");

	return ret;
}
#endif	/* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME
static int pl022_runtime_suspend(struct device *dev)
{
	struct pl022 *pl022 = dev_get_drvdata(dev);

	pl022_suspend_resources(pl022, true);
	return 0;
}

static int pl022_runtime_resume(struct device *dev)
{
	struct pl022 *pl022 = dev_get_drvdata(dev);

	pl022_resume_resources(pl022, true);
	return 0;
}
#endif

static const struct dev_pm_ops pl022_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pl022_suspend, pl022_resume)
	SET_RUNTIME_PM_OPS(pl022_runtime_suspend, pl022_runtime_resume, NULL)
};

static struct vendor_data vendor_arm = {
	.fifodepth = 8,
	.max_bpw = 16,
	.unidir = false,
	.extended_cr = false,
	.pl023 = false,
	.loopback = true,
};

static struct vendor_data vendor_st = {
	.fifodepth = 32,
	.max_bpw = 32,
	.unidir = false,
	.extended_cr = true,
	.pl023 = false,
	.loopback = true,
};

static struct vendor_data vendor_st_pl023 = {
	.fifodepth = 32,
	.max_bpw = 32,
	.unidir = false,
	.extended_cr = true,
	.pl023 = true,
	.loopback = false,
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
		.id	= 0x00080023,
		.mask	= 0xffffffff,
		.data	= &vendor_st_pl023,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl022_ids);

static struct amba_driver pl022_driver = {
	.drv = {
		.name	= "ssp-pl022",
		.pm	= &pl022_dev_pm_ops,
	},
	.id_table	= pl022_ids,
	.probe		= pl022_probe,
	.remove		= pl022_remove,
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
