/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:  Daniel Martensson
 * License terms: GNU General Public License (GPL) version 2.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <net/caif/caif_spi.h>

#ifndef CONFIG_CAIF_SPI_SYNC
#define SPI_DATA_POS 0
static inline int forward_to_spi_cmd(struct cfspi *cfspi)
{
	return cfspi->rx_cpck_len;
}
#else
#define SPI_DATA_POS SPI_CMD_SZ
static inline int forward_to_spi_cmd(struct cfspi *cfspi)
{
	return 0;
}
#endif

int spi_frm_align = 2;

/*
 * SPI padding options.
 * Warning: must be a base of 2 (& operation used) and can not be zero !
 */
int spi_up_head_align   = 1 << 1;
int spi_up_tail_align   = 1 << 0;
int spi_down_head_align = 1 << 2;
int spi_down_tail_align = 1 << 1;

#ifdef CONFIG_DEBUG_FS
static inline void debugfs_store_prev(struct cfspi *cfspi)
{
	/* Store previous command for debugging reasons.*/
	cfspi->pcmd = cfspi->cmd;
	/* Store previous transfer. */
	cfspi->tx_ppck_len = cfspi->tx_cpck_len;
	cfspi->rx_ppck_len = cfspi->rx_cpck_len;
}
#else
static inline void debugfs_store_prev(struct cfspi *cfspi)
{
}
#endif

void cfspi_xfer(struct work_struct *work)
{
	struct cfspi *cfspi;
	u8 *ptr = NULL;
	unsigned long flags;
	int ret;
	cfspi = container_of(work, struct cfspi, work);

	/* Initialize state. */
	cfspi->cmd = SPI_CMD_EOT;

	for (;;) {

		cfspi_dbg_state(cfspi, CFSPI_STATE_WAITING);

		/* Wait for master talk or transmit event. */
		wait_event_interruptible(cfspi->wait,
				 test_bit(SPI_XFER, &cfspi->state) ||
				 test_bit(SPI_TERMINATE, &cfspi->state));

		if (test_bit(SPI_TERMINATE, &cfspi->state))
			return;

#if CFSPI_DBG_PREFILL
		/* Prefill buffers for easier debugging. */
		memset(cfspi->xfer.va_tx, 0xFF, SPI_DMA_BUF_LEN);
		memset(cfspi->xfer.va_rx, 0xFF, SPI_DMA_BUF_LEN);
#endif	/* CFSPI_DBG_PREFILL */

		cfspi_dbg_state(cfspi, CFSPI_STATE_AWAKE);

	/* Check whether we have a committed frame. */
		if (cfspi->tx_cpck_len) {
			int len;

			cfspi_dbg_state(cfspi, CFSPI_STATE_FETCH_PKT);

			/* Copy committed SPI frames after the SPI indication. */
			ptr = (u8 *) cfspi->xfer.va_tx;
			ptr += SPI_IND_SZ;
			len = cfspi_xmitfrm(cfspi, ptr, cfspi->tx_cpck_len);
			WARN_ON(len != cfspi->tx_cpck_len);
	}

		cfspi_dbg_state(cfspi, CFSPI_STATE_GET_NEXT);

		/* Get length of next frame to commit. */
		cfspi->tx_npck_len = cfspi_xmitlen(cfspi);

		WARN_ON(cfspi->tx_npck_len > SPI_DMA_BUF_LEN);

		/*
		 * Add indication and length at the beginning of the frame,
		 * using little endian.
		 */
		ptr = (u8 *) cfspi->xfer.va_tx;
		*ptr++ = SPI_CMD_IND;
		*ptr++ = (SPI_CMD_IND  & 0xFF00) >> 8;
		*ptr++ = cfspi->tx_npck_len & 0x00FF;
		*ptr++ = (cfspi->tx_npck_len & 0xFF00) >> 8;

		/* Calculate length of DMAs. */
		cfspi->xfer.tx_dma_len = cfspi->tx_cpck_len + SPI_IND_SZ;
		cfspi->xfer.rx_dma_len = cfspi->rx_cpck_len + SPI_CMD_SZ;

		/* Add SPI TX frame alignment padding, if necessary. */
		if (cfspi->tx_cpck_len &&
			(cfspi->xfer.tx_dma_len % spi_frm_align)) {

			cfspi->xfer.tx_dma_len += spi_frm_align -
			    (cfspi->xfer.tx_dma_len % spi_frm_align);
		}

		/* Add SPI RX frame alignment padding, if necessary. */
		if (cfspi->rx_cpck_len &&
			(cfspi->xfer.rx_dma_len % spi_frm_align)) {

			cfspi->xfer.rx_dma_len += spi_frm_align -
			    (cfspi->xfer.rx_dma_len % spi_frm_align);
		}

		cfspi_dbg_state(cfspi, CFSPI_STATE_INIT_XFER);

		/* Start transfer. */
		ret = cfspi->dev->init_xfer(&cfspi->xfer, cfspi->dev);
		WARN_ON(ret);

		cfspi_dbg_state(cfspi, CFSPI_STATE_WAIT_ACTIVE);

		/*
		 * TODO: We might be able to make an assumption if this is the
		 * first loop. Make sure that minimum toggle time is respected.
		 */
		udelay(MIN_TRANSITION_TIME_USEC);

		cfspi_dbg_state(cfspi, CFSPI_STATE_SIG_ACTIVE);

		/* Signal that we are ready to receive data. */
		cfspi->dev->sig_xfer(true, cfspi->dev);

		cfspi_dbg_state(cfspi, CFSPI_STATE_WAIT_XFER_DONE);

		/* Wait for transfer completion. */
		wait_for_completion(&cfspi->comp);

		cfspi_dbg_state(cfspi, CFSPI_STATE_XFER_DONE);

		if (cfspi->cmd == SPI_CMD_EOT) {
			/*
			 * Clear the master talk bit. A xfer is always at
			 *  least two bursts.
			 */
			clear_bit(SPI_SS_ON, &cfspi->state);
		}

		cfspi_dbg_state(cfspi, CFSPI_STATE_WAIT_INACTIVE);

		/* Make sure that the minimum toggle time is respected. */
		if (SPI_XFER_TIME_USEC(cfspi->xfer.tx_dma_len,
					cfspi->dev->clk_mhz) <
			MIN_TRANSITION_TIME_USEC) {

			udelay(MIN_TRANSITION_TIME_USEC -
				SPI_XFER_TIME_USEC
				(cfspi->xfer.tx_dma_len, cfspi->dev->clk_mhz));
		}

		cfspi_dbg_state(cfspi, CFSPI_STATE_SIG_INACTIVE);

		/* De-assert transfer signal. */
		cfspi->dev->sig_xfer(false, cfspi->dev);

		/* Check whether we received a CAIF packet. */
		if (cfspi->rx_cpck_len) {
			int len;

			cfspi_dbg_state(cfspi, CFSPI_STATE_DELIVER_PKT);

			/* Parse SPI frame. */
			ptr = ((u8 *)(cfspi->xfer.va_rx + SPI_DATA_POS));

			len = cfspi_rxfrm(cfspi, ptr, cfspi->rx_cpck_len);
			WARN_ON(len != cfspi->rx_cpck_len);
		}

		/* Check the next SPI command and length. */
		ptr = (u8 *) cfspi->xfer.va_rx;

		ptr += forward_to_spi_cmd(cfspi);

		cfspi->cmd = *ptr++;
		cfspi->cmd |= ((*ptr++) << 8) & 0xFF00;
		cfspi->rx_npck_len = *ptr++;
		cfspi->rx_npck_len |= ((*ptr++) << 8) & 0xFF00;

		WARN_ON(cfspi->rx_npck_len > SPI_DMA_BUF_LEN);
		WARN_ON(cfspi->cmd > SPI_CMD_EOT);

		debugfs_store_prev(cfspi);

		/* Check whether the master issued an EOT command. */
		if (cfspi->cmd == SPI_CMD_EOT) {
			/* Reset state. */
			cfspi->tx_cpck_len = 0;
			cfspi->rx_cpck_len = 0;
		} else {
			/* Update state. */
			cfspi->tx_cpck_len = cfspi->tx_npck_len;
			cfspi->rx_cpck_len = cfspi->rx_npck_len;
		}

		/*
		 * Check whether we need to clear the xfer bit.
		 * Spin lock needed for packet insertion.
		 * Test and clear of different bits
		 * are not supported.
		 */
		spin_lock_irqsave(&cfspi->lock, flags);
		if (cfspi->cmd == SPI_CMD_EOT && !cfspi_xmitlen(cfspi)
			&& !test_bit(SPI_SS_ON, &cfspi->state))
			clear_bit(SPI_XFER, &cfspi->state);

		spin_unlock_irqrestore(&cfspi->lock, flags);
	}
}

struct platform_driver cfspi_spi_driver = {
	.probe = cfspi_spi_probe,
	.remove = cfspi_spi_remove,
	.driver = {
		   .name = "cfspi_sspi",
		   .owner = THIS_MODULE,
		   },
};
