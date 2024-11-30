// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * au1550 psc spi controller driver
 * may work also with au1200, au1210, au1250
 * will not work on au1000, au1100 and au1500 (no full spi controller there)
 *
 * Copyright (c) 2006 ATRON electronic GmbH
 * Author: Jan Nikitenko <jan.nikitenko@gmail.com>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>

#include <asm/mach-au1x00/au1550_spi.h>

static unsigned int usedma = 1;
module_param(usedma, uint, 0644);

/*
#define AU1550_SPI_DEBUG_LOOPBACK
*/


#define AU1550_SPI_DBDMA_DESCRIPTORS 1
#define AU1550_SPI_DMA_RXTMP_MINSIZE 2048U

struct au1550_spi {
	struct spi_bitbang bitbang;

	volatile psc_spi_t __iomem *regs;
	int irq;

	unsigned int len;
	unsigned int tx_count;
	unsigned int rx_count;
	const u8 *tx;
	u8 *rx;

	void (*rx_word)(struct au1550_spi *hw);
	void (*tx_word)(struct au1550_spi *hw);
	int (*txrx_bufs)(struct spi_device *spi, struct spi_transfer *t);
	irqreturn_t (*irq_callback)(struct au1550_spi *hw);

	struct completion host_done;

	unsigned int usedma;
	u32 dma_tx_id;
	u32 dma_rx_id;
	u32 dma_tx_ch;
	u32 dma_rx_ch;

	u8 *dma_rx_tmpbuf;
	unsigned int dma_rx_tmpbuf_size;
	u32 dma_rx_tmpbuf_addr;

	struct spi_controller *host;
	struct device *dev;
	struct au1550_spi_info *pdata;
	struct resource *ioarea;
};


/* we use an 8-bit memory device for dma transfers to/from spi fifo */
static dbdev_tab_t au1550_spi_mem_dbdev = {
	.dev_id			= DBDMA_MEM_CHAN,
	.dev_flags		= DEV_FLAGS_ANYUSE|DEV_FLAGS_SYNC,
	.dev_tsize		= 0,
	.dev_devwidth		= 8,
	.dev_physaddr		= 0x00000000,
	.dev_intlevel		= 0,
	.dev_intpolarity	= 0
};

static int ddma_memid;	/* id to above mem dma device */

static void au1550_spi_bits_handlers_set(struct au1550_spi *hw, int bpw);


/*
 *  compute BRG and DIV bits to setup spi clock based on main input clock rate
 *  that was specified in platform data structure
 *  according to au1550 datasheet:
 *    psc_tempclk = psc_mainclk / (2 << DIV)
 *    spiclk = psc_tempclk / (2 * (BRG + 1))
 *    BRG valid range is 4..63
 *    DIV valid range is 0..3
 */
static u32 au1550_spi_baudcfg(struct au1550_spi *hw, unsigned int speed_hz)
{
	u32 mainclk_hz = hw->pdata->mainclk_hz;
	u32 div, brg;

	for (div = 0; div < 4; div++) {
		brg = mainclk_hz / speed_hz / (4 << div);
		/* now we have BRG+1 in brg, so count with that */
		if (brg < (4 + 1)) {
			brg = (4 + 1);	/* speed_hz too big */
			break;		/* set lowest brg (div is == 0) */
		}
		if (brg <= (63 + 1))
			break;		/* we have valid brg and div */
	}
	if (div == 4) {
		div = 3;		/* speed_hz too small */
		brg = (63 + 1);		/* set highest brg and div */
	}
	brg--;
	return PSC_SPICFG_SET_BAUD(brg) | PSC_SPICFG_SET_DIV(div);
}

static inline void au1550_spi_mask_ack_all(struct au1550_spi *hw)
{
	hw->regs->psc_spimsk =
		  PSC_SPIMSK_MM | PSC_SPIMSK_RR | PSC_SPIMSK_RO
		| PSC_SPIMSK_RU | PSC_SPIMSK_TR | PSC_SPIMSK_TO
		| PSC_SPIMSK_TU | PSC_SPIMSK_SD | PSC_SPIMSK_MD;
	wmb(); /* drain writebuffer */

	hw->regs->psc_spievent =
		  PSC_SPIEVNT_MM | PSC_SPIEVNT_RR | PSC_SPIEVNT_RO
		| PSC_SPIEVNT_RU | PSC_SPIEVNT_TR | PSC_SPIEVNT_TO
		| PSC_SPIEVNT_TU | PSC_SPIEVNT_SD | PSC_SPIEVNT_MD;
	wmb(); /* drain writebuffer */
}

static void au1550_spi_reset_fifos(struct au1550_spi *hw)
{
	u32 pcr;

	hw->regs->psc_spipcr = PSC_SPIPCR_RC | PSC_SPIPCR_TC;
	wmb(); /* drain writebuffer */
	do {
		pcr = hw->regs->psc_spipcr;
		wmb(); /* drain writebuffer */
	} while (pcr != 0);
}

/*
 * dma transfers are used for the most common spi word size of 8-bits
 * we cannot easily change already set up dma channels' width, so if we wanted
 * dma support for more than 8-bit words (up to 24 bits), we would need to
 * setup dma channels from scratch on each spi transfer, based on bits_per_word
 * instead we have pre set up 8 bit dma channels supporting spi 4 to 8 bits
 * transfers, and 9 to 24 bits spi transfers will be done in pio irq based mode
 * callbacks to handle dma or pio are set up in au1550_spi_bits_handlers_set()
 */
static void au1550_spi_chipsel(struct spi_device *spi, int value)
{
	struct au1550_spi *hw = spi_controller_get_devdata(spi->controller);
	unsigned int cspol = spi->mode & SPI_CS_HIGH ? 1 : 0;
	u32 cfg, stat;

	switch (value) {
	case BITBANG_CS_INACTIVE:
		if (hw->pdata->deactivate_cs)
			hw->pdata->deactivate_cs(hw->pdata, spi_get_chipselect(spi, 0),
					cspol);
		break;

	case BITBANG_CS_ACTIVE:
		au1550_spi_bits_handlers_set(hw, spi->bits_per_word);

		cfg = hw->regs->psc_spicfg;
		wmb(); /* drain writebuffer */
		hw->regs->psc_spicfg = cfg & ~PSC_SPICFG_DE_ENABLE;
		wmb(); /* drain writebuffer */

		if (spi->mode & SPI_CPOL)
			cfg |= PSC_SPICFG_BI;
		else
			cfg &= ~PSC_SPICFG_BI;
		if (spi->mode & SPI_CPHA)
			cfg &= ~PSC_SPICFG_CDE;
		else
			cfg |= PSC_SPICFG_CDE;

		if (spi->mode & SPI_LSB_FIRST)
			cfg |= PSC_SPICFG_MLF;
		else
			cfg &= ~PSC_SPICFG_MLF;

		if (hw->usedma && spi->bits_per_word <= 8)
			cfg &= ~PSC_SPICFG_DD_DISABLE;
		else
			cfg |= PSC_SPICFG_DD_DISABLE;
		cfg = PSC_SPICFG_CLR_LEN(cfg);
		cfg |= PSC_SPICFG_SET_LEN(spi->bits_per_word);

		cfg = PSC_SPICFG_CLR_BAUD(cfg);
		cfg &= ~PSC_SPICFG_SET_DIV(3);
		cfg |= au1550_spi_baudcfg(hw, spi->max_speed_hz);

		hw->regs->psc_spicfg = cfg | PSC_SPICFG_DE_ENABLE;
		wmb(); /* drain writebuffer */
		do {
			stat = hw->regs->psc_spistat;
			wmb(); /* drain writebuffer */
		} while ((stat & PSC_SPISTAT_DR) == 0);

		if (hw->pdata->activate_cs)
			hw->pdata->activate_cs(hw->pdata, spi_get_chipselect(spi, 0),
					cspol);
		break;
	}
}

static int au1550_spi_setupxfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct au1550_spi *hw = spi_controller_get_devdata(spi->controller);
	unsigned int bpw, hz;
	u32 cfg, stat;

	if (t) {
		bpw = t->bits_per_word;
		hz = t->speed_hz;
	} else {
		bpw = spi->bits_per_word;
		hz = spi->max_speed_hz;
	}

	if (!hz)
		return -EINVAL;

	au1550_spi_bits_handlers_set(hw, spi->bits_per_word);

	cfg = hw->regs->psc_spicfg;
	wmb(); /* drain writebuffer */
	hw->regs->psc_spicfg = cfg & ~PSC_SPICFG_DE_ENABLE;
	wmb(); /* drain writebuffer */

	if (hw->usedma && bpw <= 8)
		cfg &= ~PSC_SPICFG_DD_DISABLE;
	else
		cfg |= PSC_SPICFG_DD_DISABLE;
	cfg = PSC_SPICFG_CLR_LEN(cfg);
	cfg |= PSC_SPICFG_SET_LEN(bpw);

	cfg = PSC_SPICFG_CLR_BAUD(cfg);
	cfg &= ~PSC_SPICFG_SET_DIV(3);
	cfg |= au1550_spi_baudcfg(hw, hz);

	hw->regs->psc_spicfg = cfg;
	wmb(); /* drain writebuffer */

	if (cfg & PSC_SPICFG_DE_ENABLE) {
		do {
			stat = hw->regs->psc_spistat;
			wmb(); /* drain writebuffer */
		} while ((stat & PSC_SPISTAT_DR) == 0);
	}

	au1550_spi_reset_fifos(hw);
	au1550_spi_mask_ack_all(hw);
	return 0;
}

/*
 * for dma spi transfers, we have to setup rx channel, otherwise there is
 * no reliable way how to recognize that spi transfer is done
 * dma complete callbacks are called before real spi transfer is finished
 * and if only tx dma channel is set up (and rx fifo overflow event masked)
 * spi host done event irq is not generated unless rx fifo is empty (emptied)
 * so we need rx tmp buffer to use for rx dma if user does not provide one
 */
static int au1550_spi_dma_rxtmp_alloc(struct au1550_spi *hw, unsigned int size)
{
	hw->dma_rx_tmpbuf = kmalloc(size, GFP_KERNEL);
	if (!hw->dma_rx_tmpbuf)
		return -ENOMEM;
	hw->dma_rx_tmpbuf_size = size;
	hw->dma_rx_tmpbuf_addr = dma_map_single(hw->dev, hw->dma_rx_tmpbuf,
			size, DMA_FROM_DEVICE);
	if (dma_mapping_error(hw->dev, hw->dma_rx_tmpbuf_addr)) {
		kfree(hw->dma_rx_tmpbuf);
		hw->dma_rx_tmpbuf = 0;
		hw->dma_rx_tmpbuf_size = 0;
		return -EFAULT;
	}
	return 0;
}

static void au1550_spi_dma_rxtmp_free(struct au1550_spi *hw)
{
	dma_unmap_single(hw->dev, hw->dma_rx_tmpbuf_addr,
			hw->dma_rx_tmpbuf_size, DMA_FROM_DEVICE);
	kfree(hw->dma_rx_tmpbuf);
	hw->dma_rx_tmpbuf = 0;
	hw->dma_rx_tmpbuf_size = 0;
}

static int au1550_spi_dma_txrxb(struct spi_device *spi, struct spi_transfer *t)
{
	struct au1550_spi *hw = spi_controller_get_devdata(spi->controller);
	dma_addr_t dma_tx_addr;
	dma_addr_t dma_rx_addr;
	u32 res;

	hw->len = t->len;
	hw->tx_count = 0;
	hw->rx_count = 0;

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;

	/*
	 * - first map the TX buffer, so cache data gets written to memory
	 * - then map the RX buffer, so that cache entries (with
	 *   soon-to-be-stale data) get removed
	 * use rx buffer in place of tx if tx buffer was not provided
	 * use temp rx buffer (preallocated or realloc to fit) for rx dma
	 */
	if (t->tx_buf) {
		dma_tx_addr = dma_map_single(hw->dev, (void *)t->tx_buf,
					     t->len, DMA_TO_DEVICE);
		if (dma_mapping_error(hw->dev, dma_tx_addr))
			dev_err(hw->dev, "tx dma map error\n");
	}

	if (t->rx_buf) {
		dma_rx_addr = dma_map_single(hw->dev, (void *)t->rx_buf,
					     t->len, DMA_FROM_DEVICE);
		if (dma_mapping_error(hw->dev, dma_rx_addr))
			dev_err(hw->dev, "rx dma map error\n");
	} else {
		if (t->len > hw->dma_rx_tmpbuf_size) {
			int ret;

			au1550_spi_dma_rxtmp_free(hw);
			ret = au1550_spi_dma_rxtmp_alloc(hw, max(t->len,
					AU1550_SPI_DMA_RXTMP_MINSIZE));
			if (ret < 0)
				return ret;
		}
		hw->rx = hw->dma_rx_tmpbuf;
		dma_rx_addr = hw->dma_rx_tmpbuf_addr;
		dma_sync_single_for_device(hw->dev, dma_rx_addr,
			t->len, DMA_FROM_DEVICE);
	}

	if (!t->tx_buf) {
		dma_sync_single_for_device(hw->dev, dma_rx_addr,
				t->len, DMA_BIDIRECTIONAL);
		hw->tx = hw->rx;
	}

	/* put buffers on the ring */
	res = au1xxx_dbdma_put_dest(hw->dma_rx_ch, virt_to_phys(hw->rx),
				    t->len, DDMA_FLAGS_IE);
	if (!res)
		dev_err(hw->dev, "rx dma put dest error\n");

	res = au1xxx_dbdma_put_source(hw->dma_tx_ch, virt_to_phys(hw->tx),
				      t->len, DDMA_FLAGS_IE);
	if (!res)
		dev_err(hw->dev, "tx dma put source error\n");

	au1xxx_dbdma_start(hw->dma_rx_ch);
	au1xxx_dbdma_start(hw->dma_tx_ch);

	/* by default enable nearly all events interrupt */
	hw->regs->psc_spimsk = PSC_SPIMSK_SD;
	wmb(); /* drain writebuffer */

	/* start the transfer */
	hw->regs->psc_spipcr = PSC_SPIPCR_MS;
	wmb(); /* drain writebuffer */

	wait_for_completion(&hw->host_done);

	au1xxx_dbdma_stop(hw->dma_tx_ch);
	au1xxx_dbdma_stop(hw->dma_rx_ch);

	if (!t->rx_buf) {
		/* using the temporal preallocated and premapped buffer */
		dma_sync_single_for_cpu(hw->dev, dma_rx_addr, t->len,
			DMA_FROM_DEVICE);
	}
	/* unmap buffers if mapped above */
	if (t->rx_buf)
		dma_unmap_single(hw->dev, dma_rx_addr, t->len,
			DMA_FROM_DEVICE);
	if (t->tx_buf)
		dma_unmap_single(hw->dev, dma_tx_addr, t->len,
			DMA_TO_DEVICE);

	return min(hw->rx_count, hw->tx_count);
}

static irqreturn_t au1550_spi_dma_irq_callback(struct au1550_spi *hw)
{
	u32 stat, evnt;

	stat = hw->regs->psc_spistat;
	evnt = hw->regs->psc_spievent;
	wmb(); /* drain writebuffer */
	if ((stat & PSC_SPISTAT_DI) == 0) {
		dev_err(hw->dev, "Unexpected IRQ!\n");
		return IRQ_NONE;
	}

	if ((evnt & (PSC_SPIEVNT_MM | PSC_SPIEVNT_RO
				| PSC_SPIEVNT_RU | PSC_SPIEVNT_TO
				| PSC_SPIEVNT_TU | PSC_SPIEVNT_SD))
			!= 0) {
		/*
		 * due to an spi error we consider transfer as done,
		 * so mask all events until before next transfer start
		 * and stop the possibly running dma immediately
		 */
		au1550_spi_mask_ack_all(hw);
		au1xxx_dbdma_stop(hw->dma_rx_ch);
		au1xxx_dbdma_stop(hw->dma_tx_ch);

		/* get number of transferred bytes */
		hw->rx_count = hw->len - au1xxx_get_dma_residue(hw->dma_rx_ch);
		hw->tx_count = hw->len - au1xxx_get_dma_residue(hw->dma_tx_ch);

		au1xxx_dbdma_reset(hw->dma_rx_ch);
		au1xxx_dbdma_reset(hw->dma_tx_ch);
		au1550_spi_reset_fifos(hw);

		if (evnt == PSC_SPIEVNT_RO)
			dev_err(hw->dev,
				"dma transfer: receive FIFO overflow!\n");
		else
			dev_err(hw->dev,
				"dma transfer: unexpected SPI error (event=0x%x stat=0x%x)!\n",
				evnt, stat);

		complete(&hw->host_done);
		return IRQ_HANDLED;
	}

	if ((evnt & PSC_SPIEVNT_MD) != 0) {
		/* transfer completed successfully */
		au1550_spi_mask_ack_all(hw);
		hw->rx_count = hw->len;
		hw->tx_count = hw->len;
		complete(&hw->host_done);
	}
	return IRQ_HANDLED;
}


/* routines to handle different word sizes in pio mode */
#define AU1550_SPI_RX_WORD(size, mask)					\
static void au1550_spi_rx_word_##size(struct au1550_spi *hw)		\
{									\
	u32 fifoword = hw->regs->psc_spitxrx & (u32)(mask);		\
	wmb(); /* drain writebuffer */					\
	if (hw->rx) {							\
		*(u##size *)hw->rx = (u##size)fifoword;			\
		hw->rx += (size) / 8;					\
	}								\
	hw->rx_count += (size) / 8;					\
}

#define AU1550_SPI_TX_WORD(size, mask)					\
static void au1550_spi_tx_word_##size(struct au1550_spi *hw)		\
{									\
	u32 fifoword = 0;						\
	if (hw->tx) {							\
		fifoword = *(u##size *)hw->tx & (u32)(mask);		\
		hw->tx += (size) / 8;					\
	}								\
	hw->tx_count += (size) / 8;					\
	if (hw->tx_count >= hw->len)					\
		fifoword |= PSC_SPITXRX_LC;				\
	hw->regs->psc_spitxrx = fifoword;				\
	wmb(); /* drain writebuffer */					\
}

AU1550_SPI_RX_WORD(8, 0xff)
AU1550_SPI_RX_WORD(16, 0xffff)
AU1550_SPI_RX_WORD(32, 0xffffff)
AU1550_SPI_TX_WORD(8, 0xff)
AU1550_SPI_TX_WORD(16, 0xffff)
AU1550_SPI_TX_WORD(32, 0xffffff)

static int au1550_spi_pio_txrxb(struct spi_device *spi, struct spi_transfer *t)
{
	u32 stat, mask;
	struct au1550_spi *hw = spi_controller_get_devdata(spi->controller);

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;
	hw->len = t->len;
	hw->tx_count = 0;
	hw->rx_count = 0;

	/* by default enable nearly all events after filling tx fifo */
	mask = PSC_SPIMSK_SD;

	/* fill the transmit FIFO */
	while (hw->tx_count < hw->len) {

		hw->tx_word(hw);

		if (hw->tx_count >= hw->len) {
			/* mask tx fifo request interrupt as we are done */
			mask |= PSC_SPIMSK_TR;
		}

		stat = hw->regs->psc_spistat;
		wmb(); /* drain writebuffer */
		if (stat & PSC_SPISTAT_TF)
			break;
	}

	/* enable event interrupts */
	hw->regs->psc_spimsk = mask;
	wmb(); /* drain writebuffer */

	/* start the transfer */
	hw->regs->psc_spipcr = PSC_SPIPCR_MS;
	wmb(); /* drain writebuffer */

	wait_for_completion(&hw->host_done);

	return min(hw->rx_count, hw->tx_count);
}

static irqreturn_t au1550_spi_pio_irq_callback(struct au1550_spi *hw)
{
	int busy;
	u32 stat, evnt;

	stat = hw->regs->psc_spistat;
	evnt = hw->regs->psc_spievent;
	wmb(); /* drain writebuffer */
	if ((stat & PSC_SPISTAT_DI) == 0) {
		dev_err(hw->dev, "Unexpected IRQ!\n");
		return IRQ_NONE;
	}

	if ((evnt & (PSC_SPIEVNT_MM | PSC_SPIEVNT_RO
				| PSC_SPIEVNT_RU | PSC_SPIEVNT_TO
				| PSC_SPIEVNT_SD))
			!= 0) {
		/*
		 * due to an error we consider transfer as done,
		 * so mask all events until before next transfer start
		 */
		au1550_spi_mask_ack_all(hw);
		au1550_spi_reset_fifos(hw);
		dev_err(hw->dev,
			"pio transfer: unexpected SPI error (event=0x%x stat=0x%x)!\n",
			evnt, stat);
		complete(&hw->host_done);
		return IRQ_HANDLED;
	}

	/*
	 * while there is something to read from rx fifo
	 * or there is a space to write to tx fifo:
	 */
	do {
		busy = 0;
		stat = hw->regs->psc_spistat;
		wmb(); /* drain writebuffer */

		/*
		 * Take care to not let the Rx FIFO overflow.
		 *
		 * We only write a byte if we have read one at least. Initially,
		 * the write fifo is full, so we should read from the read fifo
		 * first.
		 * In case we miss a word from the read fifo, we should get a
		 * RO event and should back out.
		 */
		if (!(stat & PSC_SPISTAT_RE) && hw->rx_count < hw->len) {
			hw->rx_word(hw);
			busy = 1;

			if (!(stat & PSC_SPISTAT_TF) && hw->tx_count < hw->len)
				hw->tx_word(hw);
		}
	} while (busy);

	hw->regs->psc_spievent = PSC_SPIEVNT_RR | PSC_SPIEVNT_TR;
	wmb(); /* drain writebuffer */

	/*
	 * Restart the SPI transmission in case of a transmit underflow.
	 * This seems to work despite the notes in the Au1550 data book
	 * of Figure 8-4 with flowchart for SPI host operation:
	 *
	 * """Note 1: An XFR Error Interrupt occurs, unless masked,
	 * for any of the following events: Tx FIFO Underflow,
	 * Rx FIFO Overflow, or Multiple-host Error
	 *    Note 2: In case of a Tx Underflow Error, all zeroes are
	 * transmitted."""
	 *
	 * By simply restarting the spi transfer on Tx Underflow Error,
	 * we assume that spi transfer was paused instead of zeroes
	 * transmittion mentioned in the Note 2 of Au1550 data book.
	 */
	if (evnt & PSC_SPIEVNT_TU) {
		hw->regs->psc_spievent = PSC_SPIEVNT_TU | PSC_SPIEVNT_MD;
		wmb(); /* drain writebuffer */
		hw->regs->psc_spipcr = PSC_SPIPCR_MS;
		wmb(); /* drain writebuffer */
	}

	if (hw->rx_count >= hw->len) {
		/* transfer completed successfully */
		au1550_spi_mask_ack_all(hw);
		complete(&hw->host_done);
	}
	return IRQ_HANDLED;
}

static int au1550_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct au1550_spi *hw = spi_controller_get_devdata(spi->controller);

	return hw->txrx_bufs(spi, t);
}

static irqreturn_t au1550_spi_irq(int irq, void *dev)
{
	struct au1550_spi *hw = dev;

	return hw->irq_callback(hw);
}

static void au1550_spi_bits_handlers_set(struct au1550_spi *hw, int bpw)
{
	if (bpw <= 8) {
		if (hw->usedma) {
			hw->txrx_bufs = &au1550_spi_dma_txrxb;
			hw->irq_callback = &au1550_spi_dma_irq_callback;
		} else {
			hw->rx_word = &au1550_spi_rx_word_8;
			hw->tx_word = &au1550_spi_tx_word_8;
			hw->txrx_bufs = &au1550_spi_pio_txrxb;
			hw->irq_callback = &au1550_spi_pio_irq_callback;
		}
	} else if (bpw <= 16) {
		hw->rx_word = &au1550_spi_rx_word_16;
		hw->tx_word = &au1550_spi_tx_word_16;
		hw->txrx_bufs = &au1550_spi_pio_txrxb;
		hw->irq_callback = &au1550_spi_pio_irq_callback;
	} else {
		hw->rx_word = &au1550_spi_rx_word_32;
		hw->tx_word = &au1550_spi_tx_word_32;
		hw->txrx_bufs = &au1550_spi_pio_txrxb;
		hw->irq_callback = &au1550_spi_pio_irq_callback;
	}
}

static void au1550_spi_setup_psc_as_spi(struct au1550_spi *hw)
{
	u32 stat, cfg;

	/* set up the PSC for SPI mode */
	hw->regs->psc_ctrl = PSC_CTRL_DISABLE;
	wmb(); /* drain writebuffer */
	hw->regs->psc_sel = PSC_SEL_PS_SPIMODE;
	wmb(); /* drain writebuffer */

	hw->regs->psc_spicfg = 0;
	wmb(); /* drain writebuffer */

	hw->regs->psc_ctrl = PSC_CTRL_ENABLE;
	wmb(); /* drain writebuffer */

	do {
		stat = hw->regs->psc_spistat;
		wmb(); /* drain writebuffer */
	} while ((stat & PSC_SPISTAT_SR) == 0);


	cfg = hw->usedma ? 0 : PSC_SPICFG_DD_DISABLE;
	cfg |= PSC_SPICFG_SET_LEN(8);
	cfg |= PSC_SPICFG_RT_FIFO8 | PSC_SPICFG_TT_FIFO8;
	/* use minimal allowed brg and div values as initial setting: */
	cfg |= PSC_SPICFG_SET_BAUD(4) | PSC_SPICFG_SET_DIV(0);

#ifdef AU1550_SPI_DEBUG_LOOPBACK
	cfg |= PSC_SPICFG_LB;
#endif

	hw->regs->psc_spicfg = cfg;
	wmb(); /* drain writebuffer */

	au1550_spi_mask_ack_all(hw);

	hw->regs->psc_spicfg |= PSC_SPICFG_DE_ENABLE;
	wmb(); /* drain writebuffer */

	do {
		stat = hw->regs->psc_spistat;
		wmb(); /* drain writebuffer */
	} while ((stat & PSC_SPISTAT_DR) == 0);

	au1550_spi_reset_fifos(hw);
}


static int au1550_spi_probe(struct platform_device *pdev)
{
	struct au1550_spi *hw;
	struct spi_controller *host;
	struct resource *r;
	int err = 0;

	host = spi_alloc_host(&pdev->dev, sizeof(struct au1550_spi));
	if (host == NULL) {
		dev_err(&pdev->dev, "No memory for spi_controller\n");
		err = -ENOMEM;
		goto err_nomem;
	}

	/* the spi->mode bits understood by this driver: */
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 24);

	hw = spi_controller_get_devdata(host);

	hw->host = host;
	hw->pdata = dev_get_platdata(&pdev->dev);
	hw->dev = &pdev->dev;

	if (hw->pdata == NULL) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		err = -ENOENT;
		goto err_no_pdata;
	}

	r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r) {
		dev_err(&pdev->dev, "no IRQ\n");
		err = -ENODEV;
		goto err_no_iores;
	}
	hw->irq = r->start;

	hw->usedma = 0;
	r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (r) {
		hw->dma_tx_id = r->start;
		r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (r) {
			hw->dma_rx_id = r->start;
			if (usedma && ddma_memid) {
				if (pdev->dev.dma_mask == NULL)
					dev_warn(&pdev->dev, "no dma mask\n");
				else
					hw->usedma = 1;
			}
		}
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no mmio resource\n");
		err = -ENODEV;
		goto err_no_iores;
	}

	hw->ioarea = request_mem_region(r->start, sizeof(psc_spi_t),
					pdev->name);
	if (!hw->ioarea) {
		dev_err(&pdev->dev, "Cannot reserve iomem region\n");
		err = -ENXIO;
		goto err_no_iores;
	}

	hw->regs = (psc_spi_t __iomem *)ioremap(r->start, sizeof(psc_spi_t));
	if (!hw->regs) {
		dev_err(&pdev->dev, "cannot ioremap\n");
		err = -ENXIO;
		goto err_ioremap;
	}

	platform_set_drvdata(pdev, hw);

	init_completion(&hw->host_done);

	hw->bitbang.ctlr = hw->host;
	hw->bitbang.setup_transfer = au1550_spi_setupxfer;
	hw->bitbang.chipselect = au1550_spi_chipsel;
	hw->bitbang.txrx_bufs = au1550_spi_txrx_bufs;

	if (hw->usedma) {
		hw->dma_tx_ch = au1xxx_dbdma_chan_alloc(ddma_memid,
			hw->dma_tx_id, NULL, (void *)hw);
		if (hw->dma_tx_ch == 0) {
			dev_err(&pdev->dev,
				"Cannot allocate tx dma channel\n");
			err = -ENXIO;
			goto err_no_txdma;
		}
		au1xxx_dbdma_set_devwidth(hw->dma_tx_ch, 8);
		if (au1xxx_dbdma_ring_alloc(hw->dma_tx_ch,
			AU1550_SPI_DBDMA_DESCRIPTORS) == 0) {
			dev_err(&pdev->dev,
				"Cannot allocate tx dma descriptors\n");
			err = -ENXIO;
			goto err_no_txdma_descr;
		}


		hw->dma_rx_ch = au1xxx_dbdma_chan_alloc(hw->dma_rx_id,
			ddma_memid, NULL, (void *)hw);
		if (hw->dma_rx_ch == 0) {
			dev_err(&pdev->dev,
				"Cannot allocate rx dma channel\n");
			err = -ENXIO;
			goto err_no_rxdma;
		}
		au1xxx_dbdma_set_devwidth(hw->dma_rx_ch, 8);
		if (au1xxx_dbdma_ring_alloc(hw->dma_rx_ch,
			AU1550_SPI_DBDMA_DESCRIPTORS) == 0) {
			dev_err(&pdev->dev,
				"Cannot allocate rx dma descriptors\n");
			err = -ENXIO;
			goto err_no_rxdma_descr;
		}

		err = au1550_spi_dma_rxtmp_alloc(hw,
			AU1550_SPI_DMA_RXTMP_MINSIZE);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Cannot allocate initial rx dma tmp buffer\n");
			goto err_dma_rxtmp_alloc;
		}
	}

	au1550_spi_bits_handlers_set(hw, 8);

	err = request_irq(hw->irq, au1550_spi_irq, 0, pdev->name, hw);
	if (err) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		goto err_no_irq;
	}

	host->bus_num = pdev->id;
	host->num_chipselect = hw->pdata->num_chipselect;

	/*
	 *  precompute valid range for spi freq - from au1550 datasheet:
	 *    psc_tempclk = psc_mainclk / (2 << DIV)
	 *    spiclk = psc_tempclk / (2 * (BRG + 1))
	 *    BRG valid range is 4..63
	 *    DIV valid range is 0..3
	 *  round the min and max frequencies to values that would still
	 *  produce valid brg and div
	 */
	{
		int min_div = (2 << 0) * (2 * (4 + 1));
		int max_div = (2 << 3) * (2 * (63 + 1));

		host->max_speed_hz = hw->pdata->mainclk_hz / min_div;
		host->min_speed_hz =
				hw->pdata->mainclk_hz / (max_div + 1) + 1;
	}

	au1550_spi_setup_psc_as_spi(hw);

	err = spi_bitbang_start(&hw->bitbang);
	if (err) {
		dev_err(&pdev->dev, "Failed to register SPI host\n");
		goto err_register;
	}

	dev_info(&pdev->dev,
		"spi host registered: bus_num=%d num_chipselect=%d\n",
		host->bus_num, host->num_chipselect);

	return 0;

err_register:
	free_irq(hw->irq, hw);

err_no_irq:
	au1550_spi_dma_rxtmp_free(hw);

err_dma_rxtmp_alloc:
err_no_rxdma_descr:
	if (hw->usedma)
		au1xxx_dbdma_chan_free(hw->dma_rx_ch);

err_no_rxdma:
err_no_txdma_descr:
	if (hw->usedma)
		au1xxx_dbdma_chan_free(hw->dma_tx_ch);

err_no_txdma:
	iounmap((void __iomem *)hw->regs);

err_ioremap:
	release_mem_region(r->start, sizeof(psc_spi_t));

err_no_iores:
err_no_pdata:
	spi_controller_put(hw->host);

err_nomem:
	return err;
}

static void au1550_spi_remove(struct platform_device *pdev)
{
	struct au1550_spi *hw = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "spi host remove: bus_num=%d\n",
		hw->host->bus_num);

	spi_bitbang_stop(&hw->bitbang);
	free_irq(hw->irq, hw);
	iounmap((void __iomem *)hw->regs);
	release_mem_region(hw->ioarea->start, sizeof(psc_spi_t));

	if (hw->usedma) {
		au1550_spi_dma_rxtmp_free(hw);
		au1xxx_dbdma_chan_free(hw->dma_rx_ch);
		au1xxx_dbdma_chan_free(hw->dma_tx_ch);
	}

	spi_controller_put(hw->host);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:au1550-spi");

static struct platform_driver au1550_spi_drv = {
	.probe = au1550_spi_probe,
	.remove = au1550_spi_remove,
	.driver = {
		.name = "au1550-spi",
	},
};

static int __init au1550_spi_init(void)
{
	/*
	 * create memory device with 8 bits dev_devwidth
	 * needed for proper byte ordering to spi fifo
	 */
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1550:
	case ALCHEMY_CPU_AU1200:
	case ALCHEMY_CPU_AU1300:
		break;
	default:
		return -ENODEV;
	}

	if (usedma) {
		ddma_memid = au1xxx_ddma_add_device(&au1550_spi_mem_dbdev);
		if (!ddma_memid)
			printk(KERN_ERR "au1550-spi: cannot add memory dbdma device\n");
	}
	return platform_driver_register(&au1550_spi_drv);
}
module_init(au1550_spi_init);

static void __exit au1550_spi_exit(void)
{
	if (usedma && ddma_memid)
		au1xxx_ddma_del_device(ddma_memid);
	platform_driver_unregister(&au1550_spi_drv);
}
module_exit(au1550_spi_exit);

MODULE_DESCRIPTION("Au1550 PSC SPI Driver");
MODULE_AUTHOR("Jan Nikitenko <jan.nikitenko@gmail.com>");
MODULE_LICENSE("GPL");
