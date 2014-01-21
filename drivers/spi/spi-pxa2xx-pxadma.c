/*
 * PXA2xx SPI private DMA support.
 *
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/pxa2xx_ssp.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>

#include "spi-pxa2xx.h"

#define DMA_INT_MASK		(DCSR_ENDINTR | DCSR_STARTINTR | DCSR_BUSERR)
#define RESET_DMA_CHANNEL	(DCSR_NODESC | DMA_INT_MASK)

bool pxa2xx_spi_dma_is_possible(size_t len)
{
	/* Try to map dma buffer and do a dma transfer if successful, but
	 * only if the length is non-zero and less than MAX_DMA_LEN.
	 *
	 * Zero-length non-descriptor DMA is illegal on PXA2xx; force use
	 * of PIO instead.  Care is needed above because the transfer may
	 * have have been passed with buffers that are already dma mapped.
	 * A zero-length transfer in PIO mode will not try to write/read
	 * to/from the buffers
	 *
	 * REVISIT large transfers are exactly where we most want to be
	 * using DMA.  If this happens much, split those transfers into
	 * multiple DMA segments rather than forcing PIO.
	 */
	return len > 0 && len <= MAX_DMA_LEN;
}

int pxa2xx_spi_map_dma_buffers(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	struct device *dev = &msg->spi->dev;

	if (!drv_data->cur_chip->enable_dma)
		return 0;

	if (msg->is_dma_mapped)
		return  drv_data->rx_dma && drv_data->tx_dma;

	if (!IS_DMA_ALIGNED(drv_data->rx) || !IS_DMA_ALIGNED(drv_data->tx))
		return 0;

	/* Modify setup if rx buffer is null */
	if (drv_data->rx == NULL) {
		*drv_data->null_dma_buf = 0;
		drv_data->rx = drv_data->null_dma_buf;
		drv_data->rx_map_len = 4;
	} else
		drv_data->rx_map_len = drv_data->len;


	/* Modify setup if tx buffer is null */
	if (drv_data->tx == NULL) {
		*drv_data->null_dma_buf = 0;
		drv_data->tx = drv_data->null_dma_buf;
		drv_data->tx_map_len = 4;
	} else
		drv_data->tx_map_len = drv_data->len;

	/* Stream map the tx buffer. Always do DMA_TO_DEVICE first
	 * so we flush the cache *before* invalidating it, in case
	 * the tx and rx buffers overlap.
	 */
	drv_data->tx_dma = dma_map_single(dev, drv_data->tx,
					drv_data->tx_map_len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, drv_data->tx_dma))
		return 0;

	/* Stream map the rx buffer */
	drv_data->rx_dma = dma_map_single(dev, drv_data->rx,
					drv_data->rx_map_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, drv_data->rx_dma)) {
		dma_unmap_single(dev, drv_data->tx_dma,
					drv_data->tx_map_len, DMA_TO_DEVICE);
		return 0;
	}

	return 1;
}

static void pxa2xx_spi_unmap_dma_buffers(struct driver_data *drv_data)
{
	struct device *dev;

	if (!drv_data->dma_mapped)
		return;

	if (!drv_data->cur_msg->is_dma_mapped) {
		dev = &drv_data->cur_msg->spi->dev;
		dma_unmap_single(dev, drv_data->rx_dma,
					drv_data->rx_map_len, DMA_FROM_DEVICE);
		dma_unmap_single(dev, drv_data->tx_dma,
					drv_data->tx_map_len, DMA_TO_DEVICE);
	}

	drv_data->dma_mapped = 0;
}

static int wait_ssp_rx_stall(void const __iomem *ioaddr)
{
	unsigned long limit = loops_per_jiffy << 1;

	while ((read_SSSR(ioaddr) & SSSR_BSY) && --limit)
		cpu_relax();

	return limit;
}

static int wait_dma_channel_stop(int channel)
{
	unsigned long limit = loops_per_jiffy << 1;

	while (!(DCSR(channel) & DCSR_STOPSTATE) && --limit)
		cpu_relax();

	return limit;
}

static void pxa2xx_spi_dma_error_stop(struct driver_data *drv_data,
				      const char *msg)
{
	void __iomem *reg = drv_data->ioaddr;

	/* Stop and reset */
	DCSR(drv_data->rx_channel) = RESET_DMA_CHANNEL;
	DCSR(drv_data->tx_channel) = RESET_DMA_CHANNEL;
	write_SSSR_CS(drv_data, drv_data->clear_sr);
	write_SSCR1(read_SSCR1(reg) & ~drv_data->dma_cr1, reg);
	if (!pxa25x_ssp_comp(drv_data))
		write_SSTO(0, reg);
	pxa2xx_spi_flush(drv_data);
	write_SSCR0(read_SSCR0(reg) & ~SSCR0_SSE, reg);

	pxa2xx_spi_unmap_dma_buffers(drv_data);

	dev_err(&drv_data->pdev->dev, "%s\n", msg);

	drv_data->cur_msg->state = ERROR_STATE;
	tasklet_schedule(&drv_data->pump_transfers);
}

static void pxa2xx_spi_dma_transfer_complete(struct driver_data *drv_data)
{
	void __iomem *reg = drv_data->ioaddr;
	struct spi_message *msg = drv_data->cur_msg;

	/* Clear and disable interrupts on SSP and DMA channels*/
	write_SSCR1(read_SSCR1(reg) & ~drv_data->dma_cr1, reg);
	write_SSSR_CS(drv_data, drv_data->clear_sr);
	DCSR(drv_data->tx_channel) = RESET_DMA_CHANNEL;
	DCSR(drv_data->rx_channel) = RESET_DMA_CHANNEL;

	if (wait_dma_channel_stop(drv_data->rx_channel) == 0)
		dev_err(&drv_data->pdev->dev,
			"dma_handler: dma rx channel stop failed\n");

	if (wait_ssp_rx_stall(drv_data->ioaddr) == 0)
		dev_err(&drv_data->pdev->dev,
			"dma_transfer: ssp rx stall failed\n");

	pxa2xx_spi_unmap_dma_buffers(drv_data);

	/* update the buffer pointer for the amount completed in dma */
	drv_data->rx += drv_data->len -
			(DCMD(drv_data->rx_channel) & DCMD_LENGTH);

	/* read trailing data from fifo, it does not matter how many
	 * bytes are in the fifo just read until buffer is full
	 * or fifo is empty, which ever occurs first */
	drv_data->read(drv_data);

	/* return count of what was actually read */
	msg->actual_length += drv_data->len -
				(drv_data->rx_end - drv_data->rx);

	/* Transfer delays and chip select release are
	 * handled in pump_transfers or giveback
	 */

	/* Move to next transfer */
	msg->state = pxa2xx_spi_next_transfer(drv_data);

	/* Schedule transfer tasklet */
	tasklet_schedule(&drv_data->pump_transfers);
}

void pxa2xx_spi_dma_handler(int channel, void *data)
{
	struct driver_data *drv_data = data;
	u32 irq_status = DCSR(channel) & DMA_INT_MASK;

	if (irq_status & DCSR_BUSERR) {

		if (channel == drv_data->tx_channel)
			pxa2xx_spi_dma_error_stop(drv_data,
				"dma_handler: bad bus address on tx channel");
		else
			pxa2xx_spi_dma_error_stop(drv_data,
				"dma_handler: bad bus address on rx channel");
		return;
	}

	/* PXA255x_SSP has no timeout interrupt, wait for tailing bytes */
	if ((channel == drv_data->tx_channel)
		&& (irq_status & DCSR_ENDINTR)
		&& (drv_data->ssp_type == PXA25x_SSP)) {

		/* Wait for rx to stall */
		if (wait_ssp_rx_stall(drv_data->ioaddr) == 0)
			dev_err(&drv_data->pdev->dev,
				"dma_handler: ssp rx stall failed\n");

		/* finish this transfer, start the next */
		pxa2xx_spi_dma_transfer_complete(drv_data);
	}
}

irqreturn_t pxa2xx_spi_dma_transfer(struct driver_data *drv_data)
{
	u32 irq_status;
	void __iomem *reg = drv_data->ioaddr;

	irq_status = read_SSSR(reg) & drv_data->mask_sr;
	if (irq_status & SSSR_ROR) {
		pxa2xx_spi_dma_error_stop(drv_data,
					  "dma_transfer: fifo overrun");
		return IRQ_HANDLED;
	}

	/* Check for false positive timeout */
	if ((irq_status & SSSR_TINT)
		&& (DCSR(drv_data->tx_channel) & DCSR_RUN)) {
		write_SSSR(SSSR_TINT, reg);
		return IRQ_HANDLED;
	}

	if (irq_status & SSSR_TINT || drv_data->rx == drv_data->rx_end) {

		/* Clear and disable timeout interrupt, do the rest in
		 * dma_transfer_complete */
		if (!pxa25x_ssp_comp(drv_data))
			write_SSTO(0, reg);

		/* finish this transfer, start the next */
		pxa2xx_spi_dma_transfer_complete(drv_data);

		return IRQ_HANDLED;
	}

	/* Opps problem detected */
	return IRQ_NONE;
}

int pxa2xx_spi_dma_prepare(struct driver_data *drv_data, u32 dma_burst)
{
	u32 dma_width;

	switch (drv_data->n_bytes) {
	case 1:
		dma_width = DCMD_WIDTH1;
		break;
	case 2:
		dma_width = DCMD_WIDTH2;
		break;
	default:
		dma_width = DCMD_WIDTH4;
		break;
	}

	/* Setup rx DMA Channel */
	DCSR(drv_data->rx_channel) = RESET_DMA_CHANNEL;
	DSADR(drv_data->rx_channel) = drv_data->ssdr_physical;
	DTADR(drv_data->rx_channel) = drv_data->rx_dma;
	if (drv_data->rx == drv_data->null_dma_buf)
		/* No target address increment */
		DCMD(drv_data->rx_channel) = DCMD_FLOWSRC
						| dma_width
						| dma_burst
						| drv_data->len;
	else
		DCMD(drv_data->rx_channel) = DCMD_INCTRGADDR
						| DCMD_FLOWSRC
						| dma_width
						| dma_burst
						| drv_data->len;

	/* Setup tx DMA Channel */
	DCSR(drv_data->tx_channel) = RESET_DMA_CHANNEL;
	DSADR(drv_data->tx_channel) = drv_data->tx_dma;
	DTADR(drv_data->tx_channel) = drv_data->ssdr_physical;
	if (drv_data->tx == drv_data->null_dma_buf)
		/* No source address increment */
		DCMD(drv_data->tx_channel) = DCMD_FLOWTRG
						| dma_width
						| dma_burst
						| drv_data->len;
	else
		DCMD(drv_data->tx_channel) = DCMD_INCSRCADDR
						| DCMD_FLOWTRG
						| dma_width
						| dma_burst
						| drv_data->len;

	/* Enable dma end irqs on SSP to detect end of transfer */
	if (drv_data->ssp_type == PXA25x_SSP)
		DCMD(drv_data->tx_channel) |= DCMD_ENDIRQEN;

	return 0;
}

void pxa2xx_spi_dma_start(struct driver_data *drv_data)
{
	DCSR(drv_data->rx_channel) |= DCSR_RUN;
	DCSR(drv_data->tx_channel) |= DCSR_RUN;
}

int pxa2xx_spi_dma_setup(struct driver_data *drv_data)
{
	struct device *dev = &drv_data->pdev->dev;
	struct ssp_device *ssp = drv_data->ssp;

	/* Get two DMA channels	(rx and tx) */
	drv_data->rx_channel = pxa_request_dma("pxa2xx_spi_ssp_rx",
						DMA_PRIO_HIGH,
						pxa2xx_spi_dma_handler,
						drv_data);
	if (drv_data->rx_channel < 0) {
		dev_err(dev, "problem (%d) requesting rx channel\n",
			drv_data->rx_channel);
		return -ENODEV;
	}
	drv_data->tx_channel = pxa_request_dma("pxa2xx_spi_ssp_tx",
						DMA_PRIO_MEDIUM,
						pxa2xx_spi_dma_handler,
						drv_data);
	if (drv_data->tx_channel < 0) {
		dev_err(dev, "problem (%d) requesting tx channel\n",
			drv_data->tx_channel);
		pxa_free_dma(drv_data->rx_channel);
		return -ENODEV;
	}

	DRCMR(ssp->drcmr_rx) = DRCMR_MAPVLD | drv_data->rx_channel;
	DRCMR(ssp->drcmr_tx) = DRCMR_MAPVLD | drv_data->tx_channel;

	return 0;
}

void pxa2xx_spi_dma_release(struct driver_data *drv_data)
{
	struct ssp_device *ssp = drv_data->ssp;

	DRCMR(ssp->drcmr_rx) = 0;
	DRCMR(ssp->drcmr_tx) = 0;

	if (drv_data->tx_channel != 0)
		pxa_free_dma(drv_data->tx_channel);
	if (drv_data->rx_channel != 0)
		pxa_free_dma(drv_data->rx_channel);
}

void pxa2xx_spi_dma_resume(struct driver_data *drv_data)
{
	if (drv_data->rx_channel != -1)
		DRCMR(drv_data->ssp->drcmr_rx) =
			DRCMR_MAPVLD | drv_data->rx_channel;
	if (drv_data->tx_channel != -1)
		DRCMR(drv_data->ssp->drcmr_tx) =
			DRCMR_MAPVLD | drv_data->tx_channel;
}

int pxa2xx_spi_set_dma_burst_and_threshold(struct chip_data *chip,
					   struct spi_device *spi,
					   u8 bits_per_word, u32 *burst_code,
					   u32 *threshold)
{
	struct pxa2xx_spi_chip *chip_info =
			(struct pxa2xx_spi_chip *)spi->controller_data;
	int bytes_per_word;
	int burst_bytes;
	int thresh_words;
	int req_burst_size;
	int retval = 0;

	/* Set the threshold (in registers) to equal the same amount of data
	 * as represented by burst size (in bytes).  The computation below
	 * is (burst_size rounded up to nearest 8 byte, word or long word)
	 * divided by (bytes/register); the tx threshold is the inverse of
	 * the rx, so that there will always be enough data in the rx fifo
	 * to satisfy a burst, and there will always be enough space in the
	 * tx fifo to accept a burst (a tx burst will overwrite the fifo if
	 * there is not enough space), there must always remain enough empty
	 * space in the rx fifo for any data loaded to the tx fifo.
	 * Whenever burst_size (in bytes) equals bits/word, the fifo threshold
	 * will be 8, or half the fifo;
	 * The threshold can only be set to 2, 4 or 8, but not 16, because
	 * to burst 16 to the tx fifo, the fifo would have to be empty;
	 * however, the minimum fifo trigger level is 1, and the tx will
	 * request service when the fifo is at this level, with only 15 spaces.
	 */

	/* find bytes/word */
	if (bits_per_word <= 8)
		bytes_per_word = 1;
	else if (bits_per_word <= 16)
		bytes_per_word = 2;
	else
		bytes_per_word = 4;

	/* use struct pxa2xx_spi_chip->dma_burst_size if available */
	if (chip_info)
		req_burst_size = chip_info->dma_burst_size;
	else {
		switch (chip->dma_burst_size) {
		default:
			/* if the default burst size is not set,
			 * do it now */
			chip->dma_burst_size = DCMD_BURST8;
		case DCMD_BURST8:
			req_burst_size = 8;
			break;
		case DCMD_BURST16:
			req_burst_size = 16;
			break;
		case DCMD_BURST32:
			req_burst_size = 32;
			break;
		}
	}
	if (req_burst_size <= 8) {
		*burst_code = DCMD_BURST8;
		burst_bytes = 8;
	} else if (req_burst_size <= 16) {
		if (bytes_per_word == 1) {
			/* don't burst more than 1/2 the fifo */
			*burst_code = DCMD_BURST8;
			burst_bytes = 8;
			retval = 1;
		} else {
			*burst_code = DCMD_BURST16;
			burst_bytes = 16;
		}
	} else {
		if (bytes_per_word == 1) {
			/* don't burst more than 1/2 the fifo */
			*burst_code = DCMD_BURST8;
			burst_bytes = 8;
			retval = 1;
		} else if (bytes_per_word == 2) {
			/* don't burst more than 1/2 the fifo */
			*burst_code = DCMD_BURST16;
			burst_bytes = 16;
			retval = 1;
		} else {
			*burst_code = DCMD_BURST32;
			burst_bytes = 32;
		}
	}

	thresh_words = burst_bytes / bytes_per_word;

	/* thresh_words will be between 2 and 8 */
	*threshold = (SSCR1_RxTresh(thresh_words) & SSCR1_RFT)
			| (SSCR1_TxTresh(16-thresh_words) & SSCR1_TFT);

	return retval;
}
