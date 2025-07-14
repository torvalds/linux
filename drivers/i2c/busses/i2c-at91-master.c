// SPDX-License-Identifier: GPL-2.0
/*
 *  i2c Support for Atmel's AT91 Two-Wire Interface (TWI)
 *
 *  Copyright (C) 2011 Weinmann Medical GmbH
 *  Author: Nikolaus Voss <n.voss@weinmann.de>
 *
 *  Evolved from original work by:
 *  Copyright (C) 2004 Rick Bronson
 *  Converted to 2.6 by Andrew Victor <andrew@sanpeople.com>
 *
 *  Borrowed heavily from original work by:
 *  Copyright (C) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/string_choices.h>

#include "i2c-at91.h"

void at91_init_twi_bus_master(struct at91_twi_dev *dev)
{
	struct at91_twi_pdata *pdata = dev->pdata;
	u32 filtr = 0;

	/* FIFO should be enabled immediately after the software reset */
	if (dev->fifo_size)
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_FIFOEN);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_MSEN);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_SVDIS);
	at91_twi_write(dev, AT91_TWI_CWGR, dev->twi_cwgr_reg);

	/* enable digital filter */
	if (pdata->has_dig_filtr && dev->enable_dig_filt)
		filtr |= AT91_TWI_FILTR_FILT;

	/* enable advanced digital filter */
	if (pdata->has_adv_dig_filtr && dev->enable_dig_filt)
		filtr |= AT91_TWI_FILTR_FILT |
			 (AT91_TWI_FILTR_THRES(dev->filter_width) &
			 AT91_TWI_FILTR_THRES_MASK);

	/* enable analog filter */
	if (pdata->has_ana_filtr && dev->enable_ana_filt)
		filtr |= AT91_TWI_FILTR_PADFEN;

	if (filtr)
		at91_twi_write(dev, AT91_TWI_FILTR, filtr);
}

/*
 * Calculate symmetric clock as stated in datasheet:
 * twi_clk = F_MAIN / (2 * (cdiv * (1 << ckdiv) + offset))
 */
static void at91_calc_twi_clock(struct at91_twi_dev *dev)
{
	int ckdiv, cdiv, div, hold = 0, filter_width = 0;
	struct at91_twi_pdata *pdata = dev->pdata;
	int offset = pdata->clk_offset;
	int max_ckdiv = pdata->clk_max_div;
	struct i2c_timings timings, *t = &timings;

	i2c_parse_fw_timings(dev->dev, t, true);

	div = max(0, (int)DIV_ROUND_UP(clk_get_rate(dev->clk),
				       2 * t->bus_freq_hz) - offset);
	ckdiv = fls(div >> 8);
	cdiv = div >> ckdiv;

	if (ckdiv > max_ckdiv) {
		dev_warn(dev->dev, "%d exceeds ckdiv max value which is %d.\n",
			 ckdiv, max_ckdiv);
		ckdiv = max_ckdiv;
		cdiv = 255;
	}

	if (pdata->has_hold_field) {
		/*
		 * hold time = HOLD + 3 x T_peripheral_clock
		 * Use clk rate in kHz to prevent overflows when computing
		 * hold.
		 */
		hold = DIV_ROUND_UP(t->sda_hold_ns
				    * (clk_get_rate(dev->clk) / 1000), 1000000);
		hold -= 3;
		if (hold < 0)
			hold = 0;
		if (hold > AT91_TWI_CWGR_HOLD_MAX) {
			dev_warn(dev->dev,
				 "HOLD field set to its maximum value (%d instead of %d)\n",
				 AT91_TWI_CWGR_HOLD_MAX, hold);
			hold = AT91_TWI_CWGR_HOLD_MAX;
		}
	}

	if (pdata->has_adv_dig_filtr) {
		/*
		 * filter width = 0 to AT91_TWI_FILTR_THRES_MAX
		 * peripheral clocks
		 */
		filter_width = DIV_ROUND_UP(t->digital_filter_width_ns
				* (clk_get_rate(dev->clk) / 1000), 1000000);
		if (filter_width > AT91_TWI_FILTR_THRES_MAX) {
			dev_warn(dev->dev,
				"Filter threshold set to its maximum value (%d instead of %d)\n",
				AT91_TWI_FILTR_THRES_MAX, filter_width);
			filter_width = AT91_TWI_FILTR_THRES_MAX;
		}
	}

	dev->twi_cwgr_reg = (ckdiv << 16) | (cdiv << 8) | cdiv
			    | AT91_TWI_CWGR_HOLD(hold);

	dev->filter_width = filter_width;

	dev_dbg(dev->dev, "cdiv %d ckdiv %d hold %d (%d ns), filter_width %d (%d ns)\n",
		cdiv, ckdiv, hold, t->sda_hold_ns, filter_width,
		t->digital_filter_width_ns);
}

static void at91_twi_dma_cleanup(struct at91_twi_dev *dev)
{
	struct at91_twi_dma *dma = &dev->dma;

	at91_twi_irq_save(dev);

	if (dma->xfer_in_progress) {
		if (dma->direction == DMA_FROM_DEVICE)
			dmaengine_terminate_sync(dma->chan_rx);
		else
			dmaengine_terminate_sync(dma->chan_tx);
		dma->xfer_in_progress = false;
	}
	if (dma->buf_mapped) {
		dma_unmap_single(dev->dev, sg_dma_address(&dma->sg[0]),
				 dev->buf_len, dma->direction);
		dma->buf_mapped = false;
	}

	at91_twi_irq_restore(dev);
}

static void at91_twi_write_next_byte(struct at91_twi_dev *dev)
{
	if (!dev->buf_len)
		return;

	/* 8bit write works with and without FIFO */
	writeb_relaxed(*dev->buf, dev->base + AT91_TWI_THR);

	/* send stop when last byte has been written */
	if (--dev->buf_len == 0) {
		if (!dev->use_alt_cmd)
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_STOP);
		at91_twi_write(dev, AT91_TWI_IDR, AT91_TWI_TXRDY);
	}

	dev_dbg(dev->dev, "wrote 0x%x, to go %zu\n", *dev->buf, dev->buf_len);

	++dev->buf;
}

static void at91_twi_write_data_dma_callback(void *data)
{
	struct at91_twi_dev *dev = (struct at91_twi_dev *)data;

	dma_unmap_single(dev->dev, sg_dma_address(&dev->dma.sg[0]),
			 dev->buf_len, DMA_TO_DEVICE);

	/*
	 * When this callback is called, THR/TX FIFO is likely not to be empty
	 * yet. So we have to wait for TXCOMP or NACK bits to be set into the
	 * Status Register to be sure that the STOP bit has been sent and the
	 * transfer is completed. The NACK interrupt has already been enabled,
	 * we just have to enable TXCOMP one.
	 */
	at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_TXCOMP);
	if (!dev->use_alt_cmd)
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_STOP);
}

static void at91_twi_write_data_dma(struct at91_twi_dev *dev)
{
	dma_addr_t dma_addr;
	struct dma_async_tx_descriptor *txdesc;
	struct at91_twi_dma *dma = &dev->dma;
	struct dma_chan *chan_tx = dma->chan_tx;
	unsigned int sg_len = 1;

	if (!dev->buf_len)
		return;

	dma->direction = DMA_TO_DEVICE;

	at91_twi_irq_save(dev);
	dma_addr = dma_map_single(dev->dev, dev->buf, dev->buf_len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev->dev, dma_addr)) {
		dev_err(dev->dev, "dma map failed\n");
		return;
	}
	dma->buf_mapped = true;
	at91_twi_irq_restore(dev);

	if (dev->fifo_size) {
		size_t part1_len, part2_len;
		struct scatterlist *sg;
		unsigned fifo_mr;

		sg_len = 0;

		part1_len = dev->buf_len & ~0x3;
		if (part1_len) {
			sg = &dma->sg[sg_len++];
			sg_dma_len(sg) = part1_len;
			sg_dma_address(sg) = dma_addr;
		}

		part2_len = dev->buf_len & 0x3;
		if (part2_len) {
			sg = &dma->sg[sg_len++];
			sg_dma_len(sg) = part2_len;
			sg_dma_address(sg) = dma_addr + part1_len;
		}

		/*
		 * DMA controller is triggered when at least 4 data can be
		 * written into the TX FIFO
		 */
		fifo_mr = at91_twi_read(dev, AT91_TWI_FMR);
		fifo_mr &= ~AT91_TWI_FMR_TXRDYM_MASK;
		fifo_mr |= AT91_TWI_FMR_TXRDYM(AT91_TWI_FOUR_DATA);
		at91_twi_write(dev, AT91_TWI_FMR, fifo_mr);
	} else {
		sg_dma_len(&dma->sg[0]) = dev->buf_len;
		sg_dma_address(&dma->sg[0]) = dma_addr;
	}

	txdesc = dmaengine_prep_slave_sg(chan_tx, dma->sg, sg_len,
					 DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc) {
		dev_err(dev->dev, "dma prep slave sg failed\n");
		goto error;
	}

	txdesc->callback = at91_twi_write_data_dma_callback;
	txdesc->callback_param = dev;

	dma->xfer_in_progress = true;
	dmaengine_submit(txdesc);
	dma_async_issue_pending(chan_tx);

	return;

error:
	at91_twi_dma_cleanup(dev);
}

static void at91_twi_read_next_byte(struct at91_twi_dev *dev)
{
	/*
	 * If we are in this case, it means there is garbage data in RHR, so
	 * delete them.
	 */
	if (!dev->buf_len) {
		at91_twi_read(dev, AT91_TWI_RHR);
		return;
	}

	/* 8bit read works with and without FIFO */
	*dev->buf = readb_relaxed(dev->base + AT91_TWI_RHR);
	--dev->buf_len;

	/* return if aborting, we only needed to read RHR to clear RXRDY*/
	if (dev->recv_len_abort)
		return;

	/* handle I2C_SMBUS_BLOCK_DATA */
	if (unlikely(dev->msg->flags & I2C_M_RECV_LEN)) {
		/* ensure length byte is a valid value */
		if (*dev->buf <= I2C_SMBUS_BLOCK_MAX && *dev->buf > 0) {
			dev->msg->flags &= ~I2C_M_RECV_LEN;
			dev->buf_len += *dev->buf;
			dev->msg->len = dev->buf_len + 1;
			dev_dbg(dev->dev, "received block length %zu\n",
					 dev->buf_len);
		} else {
			/* abort and send the stop by reading one more byte */
			dev->recv_len_abort = true;
			dev->buf_len = 1;
		}
	}

	/* send stop if second but last byte has been read */
	if (!dev->use_alt_cmd && dev->buf_len == 1)
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_STOP);

	dev_dbg(dev->dev, "read 0x%x, to go %zu\n", *dev->buf, dev->buf_len);

	++dev->buf;
}

static void at91_twi_read_data_dma_callback(void *data)
{
	struct at91_twi_dev *dev = (struct at91_twi_dev *)data;
	unsigned ier = AT91_TWI_TXCOMP;

	dma_unmap_single(dev->dev, sg_dma_address(&dev->dma.sg[0]),
			 dev->buf_len, DMA_FROM_DEVICE);

	if (!dev->use_alt_cmd) {
		/* The last two bytes have to be read without using dma */
		dev->buf += dev->buf_len - 2;
		dev->buf_len = 2;
		ier |= AT91_TWI_RXRDY;
	}
	at91_twi_write(dev, AT91_TWI_IER, ier);
}

static void at91_twi_read_data_dma(struct at91_twi_dev *dev)
{
	dma_addr_t dma_addr;
	struct dma_async_tx_descriptor *rxdesc;
	struct at91_twi_dma *dma = &dev->dma;
	struct dma_chan *chan_rx = dma->chan_rx;
	size_t buf_len;

	buf_len = (dev->use_alt_cmd) ? dev->buf_len : dev->buf_len - 2;
	dma->direction = DMA_FROM_DEVICE;

	/* Keep in mind that we won't use dma to read the last two bytes */
	at91_twi_irq_save(dev);
	dma_addr = dma_map_single(dev->dev, dev->buf, buf_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev->dev, dma_addr)) {
		dev_err(dev->dev, "dma map failed\n");
		return;
	}
	dma->buf_mapped = true;
	at91_twi_irq_restore(dev);

	if (dev->fifo_size && IS_ALIGNED(buf_len, 4)) {
		unsigned fifo_mr;

		/*
		 * DMA controller is triggered when at least 4 data can be
		 * read from the RX FIFO
		 */
		fifo_mr = at91_twi_read(dev, AT91_TWI_FMR);
		fifo_mr &= ~AT91_TWI_FMR_RXRDYM_MASK;
		fifo_mr |= AT91_TWI_FMR_RXRDYM(AT91_TWI_FOUR_DATA);
		at91_twi_write(dev, AT91_TWI_FMR, fifo_mr);
	}

	sg_dma_len(&dma->sg[0]) = buf_len;
	sg_dma_address(&dma->sg[0]) = dma_addr;

	rxdesc = dmaengine_prep_slave_sg(chan_rx, dma->sg, 1, DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc) {
		dev_err(dev->dev, "dma prep slave sg failed\n");
		goto error;
	}

	rxdesc->callback = at91_twi_read_data_dma_callback;
	rxdesc->callback_param = dev;

	dma->xfer_in_progress = true;
	dmaengine_submit(rxdesc);
	dma_async_issue_pending(dma->chan_rx);

	return;

error:
	at91_twi_dma_cleanup(dev);
}

static irqreturn_t atmel_twi_interrupt(int irq, void *dev_id)
{
	struct at91_twi_dev *dev = dev_id;
	const unsigned status = at91_twi_read(dev, AT91_TWI_SR);
	const unsigned irqstatus = status & at91_twi_read(dev, AT91_TWI_IMR);

	if (!irqstatus)
		return IRQ_NONE;
	/*
	 * In reception, the behavior of the twi device (before sama5d2) is
	 * weird. There is some magic about RXRDY flag! When a data has been
	 * almost received, the reception of a new one is anticipated if there
	 * is no stop command to send. That is the reason why ask for sending
	 * the stop command not on the last data but on the second last one.
	 *
	 * Unfortunately, we could still have the RXRDY flag set even if the
	 * transfer is done and we have read the last data. It might happen
	 * when the i2c slave device sends too quickly data after receiving the
	 * ack from the master. The data has been almost received before having
	 * the order to send stop. In this case, sending the stop command could
	 * cause a RXRDY interrupt with a TXCOMP one. It is better to manage
	 * the RXRDY interrupt first in order to not keep garbage data in the
	 * Receive Holding Register for the next transfer.
	 */
	if (irqstatus & AT91_TWI_RXRDY) {
		/*
		 * Read all available bytes at once by polling RXRDY usable w/
		 * and w/o FIFO. With FIFO enabled we could also read RXFL and
		 * avoid polling RXRDY.
		 */
		do {
			at91_twi_read_next_byte(dev);
		} while (at91_twi_read(dev, AT91_TWI_SR) & AT91_TWI_RXRDY);
	}

	/*
	 * When a NACK condition is detected, the I2C controller sets the NACK,
	 * TXCOMP and TXRDY bits all together in the Status Register (SR).
	 *
	 * 1 - Handling NACK errors with CPU write transfer.
	 *
	 * In such case, we should not write the next byte into the Transmit
	 * Holding Register (THR) otherwise the I2C controller would start a new
	 * transfer and the I2C slave is likely to reply by another NACK.
	 *
	 * 2 - Handling NACK errors with DMA write transfer.
	 *
	 * By setting the TXRDY bit in the SR, the I2C controller also triggers
	 * the DMA controller to write the next data into the THR. Then the
	 * result depends on the hardware version of the I2C controller.
	 *
	 * 2a - Without support of the Alternative Command mode.
	 *
	 * This is the worst case: the DMA controller is triggered to write the
	 * next data into the THR, hence starting a new transfer: the I2C slave
	 * is likely to reply by another NACK.
	 * Concurrently, this interrupt handler is likely to be called to manage
	 * the first NACK before the I2C controller detects the second NACK and
	 * sets once again the NACK bit into the SR.
	 * When handling the first NACK, this interrupt handler disables the I2C
	 * controller interruptions, especially the NACK interrupt.
	 * Hence, the NACK bit is pending into the SR. This is why we should
	 * read the SR to clear all pending interrupts at the beginning of
	 * at91_do_twi_transfer() before actually starting a new transfer.
	 *
	 * 2b - With support of the Alternative Command mode.
	 *
	 * When a NACK condition is detected, the I2C controller also locks the
	 * THR (and sets the LOCK bit in the SR): even though the DMA controller
	 * is triggered by the TXRDY bit to write the next data into the THR,
	 * this data actually won't go on the I2C bus hence a second NACK is not
	 * generated.
	 */
	if (irqstatus & (AT91_TWI_TXCOMP | AT91_TWI_NACK)) {
		at91_disable_twi_interrupts(dev);
		complete(&dev->cmd_complete);
	} else if (irqstatus & AT91_TWI_TXRDY) {
		at91_twi_write_next_byte(dev);
	}

	/* catch error flags */
	dev->transfer_status |= status;

	return IRQ_HANDLED;
}

static int at91_do_twi_transfer(struct at91_twi_dev *dev)
{
	int ret;
	unsigned long time_left;
	bool has_unre_flag = dev->pdata->has_unre_flag;
	bool has_alt_cmd = dev->pdata->has_alt_cmd;

	/*
	 * WARNING: the TXCOMP bit in the Status Register is NOT a clear on
	 * read flag but shows the state of the transmission at the time the
	 * Status Register is read. According to the programmer datasheet,
	 * TXCOMP is set when both holding register and internal shifter are
	 * empty and STOP condition has been sent.
	 * Consequently, we should enable NACK interrupt rather than TXCOMP to
	 * detect transmission failure.
	 * Indeed let's take the case of an i2c write command using DMA.
	 * Whenever the slave doesn't acknowledge a byte, the LOCK, NACK and
	 * TXCOMP bits are set together into the Status Register.
	 * LOCK is a clear on write bit, which is set to prevent the DMA
	 * controller from sending new data on the i2c bus after a NACK
	 * condition has happened. Once locked, this i2c peripheral stops
	 * triggering the DMA controller for new data but it is more than
	 * likely that a new DMA transaction is already in progress, writing
	 * into the Transmit Holding Register. Since the peripheral is locked,
	 * these new data won't be sent to the i2c bus but they will remain
	 * into the Transmit Holding Register, so TXCOMP bit is cleared.
	 * Then when the interrupt handler is called, the Status Register is
	 * read: the TXCOMP bit is clear but NACK bit is still set. The driver
	 * manage the error properly, without waiting for timeout.
	 * This case can be reproduced easyly when writing into an at24 eeprom.
	 *
	 * Besides, the TXCOMP bit is already set before the i2c transaction
	 * has been started. For read transactions, this bit is cleared when
	 * writing the START bit into the Control Register. So the
	 * corresponding interrupt can safely be enabled just after.
	 * However for write transactions managed by the CPU, we first write
	 * into THR, so TXCOMP is cleared. Then we can safely enable TXCOMP
	 * interrupt. If TXCOMP interrupt were enabled before writing into THR,
	 * the interrupt handler would be called immediately and the i2c command
	 * would be reported as completed.
	 * Also when a write transaction is managed by the DMA controller,
	 * enabling the TXCOMP interrupt in this function may lead to a race
	 * condition since we don't know whether the TXCOMP interrupt is enabled
	 * before or after the DMA has started to write into THR. So the TXCOMP
	 * interrupt is enabled later by at91_twi_write_data_dma_callback().
	 * Immediately after in that DMA callback, if the alternative command
	 * mode is not used, we still need to send the STOP condition manually
	 * writing the corresponding bit into the Control Register.
	 */

	dev_dbg(dev->dev, "transfer: %s %zu bytes.\n",
		str_read_write(dev->msg->flags & I2C_M_RD), dev->buf_len);

	reinit_completion(&dev->cmd_complete);
	dev->transfer_status = 0;

	/* Clear pending interrupts, such as NACK. */
	at91_twi_read(dev, AT91_TWI_SR);

	if (dev->fifo_size) {
		unsigned fifo_mr = at91_twi_read(dev, AT91_TWI_FMR);

		/* Reset FIFO mode register */
		fifo_mr &= ~(AT91_TWI_FMR_TXRDYM_MASK |
			     AT91_TWI_FMR_RXRDYM_MASK);
		fifo_mr |= AT91_TWI_FMR_TXRDYM(AT91_TWI_ONE_DATA);
		fifo_mr |= AT91_TWI_FMR_RXRDYM(AT91_TWI_ONE_DATA);
		at91_twi_write(dev, AT91_TWI_FMR, fifo_mr);

		/* Flush FIFOs */
		at91_twi_write(dev, AT91_TWI_CR,
			       AT91_TWI_THRCLR | AT91_TWI_RHRCLR);
	}

	if (!dev->buf_len) {
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_QUICK);
		at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_TXCOMP);
	} else if (dev->msg->flags & I2C_M_RD) {
		unsigned start_flags = AT91_TWI_START;

		/* if only one byte is to be read, immediately stop transfer */
		if (!dev->use_alt_cmd && dev->buf_len <= 1 &&
		    !(dev->msg->flags & I2C_M_RECV_LEN))
			start_flags |= AT91_TWI_STOP;
		at91_twi_write(dev, AT91_TWI_CR, start_flags);
		/*
		 * When using dma without alternative command mode, the last
		 * byte has to be read manually in order to not send the stop
		 * command too late and then to receive extra data.
		 * In practice, there are some issues if you use the dma to
		 * read n-1 bytes because of latency.
		 * Reading n-2 bytes with dma and the two last ones manually
		 * seems to be the best solution.
		 */
		if (dev->use_dma && (dev->buf_len > AT91_I2C_DMA_THRESHOLD)) {
			at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_NACK);
			at91_twi_read_data_dma(dev);
		} else {
			at91_twi_write(dev, AT91_TWI_IER,
				       AT91_TWI_TXCOMP |
				       AT91_TWI_NACK |
				       AT91_TWI_RXRDY);
		}
	} else {
		if (dev->use_dma && (dev->buf_len > AT91_I2C_DMA_THRESHOLD)) {
			at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_NACK);
			at91_twi_write_data_dma(dev);
		} else {
			at91_twi_write_next_byte(dev);
			at91_twi_write(dev, AT91_TWI_IER,
				       AT91_TWI_TXCOMP | AT91_TWI_NACK |
				       (dev->buf_len ? AT91_TWI_TXRDY : 0));
		}
	}

	time_left = wait_for_completion_timeout(&dev->cmd_complete,
					      dev->adapter.timeout);
	if (time_left == 0) {
		dev->transfer_status |= at91_twi_read(dev, AT91_TWI_SR);
		at91_init_twi_bus(dev);
		ret = -ETIMEDOUT;
		goto error;
	}
	if (dev->transfer_status & AT91_TWI_NACK) {
		dev_dbg(dev->dev, "received nack\n");
		ret = -EREMOTEIO;
		goto error;
	}
	if (dev->transfer_status & AT91_TWI_OVRE) {
		dev_err(dev->dev, "overrun while reading\n");
		ret = -EIO;
		goto error;
	}
	if (has_unre_flag && dev->transfer_status & AT91_TWI_UNRE) {
		dev_err(dev->dev, "underrun while writing\n");
		ret = -EIO;
		goto error;
	}
	if ((has_alt_cmd || dev->fifo_size) &&
	    (dev->transfer_status & AT91_TWI_LOCK)) {
		dev_err(dev->dev, "tx locked\n");
		ret = -EIO;
		goto error;
	}
	if (dev->recv_len_abort) {
		dev_err(dev->dev, "invalid smbus block length recvd\n");
		ret = -EPROTO;
		goto error;
	}

	dev_dbg(dev->dev, "transfer complete\n");

	return 0;

error:
	/* first stop DMA transfer if still in progress */
	at91_twi_dma_cleanup(dev);
	/* then flush THR/FIFO and unlock TX if locked */
	if ((has_alt_cmd || dev->fifo_size) &&
	    (dev->transfer_status & AT91_TWI_LOCK)) {
		dev_dbg(dev->dev, "unlock tx\n");
		at91_twi_write(dev, AT91_TWI_CR,
			       AT91_TWI_THRCLR | AT91_TWI_LOCKCLR);
	}

	/*
	 * some faulty I2C slave devices might hold SDA down;
	 * we can send a bus clear command, hoping that the pins will be
	 * released
	 */
	i2c_recover_bus(&dev->adapter);

	return ret;
}

static int at91_twi_xfer(struct i2c_adapter *adap, struct i2c_msg *msg, int num)
{
	struct at91_twi_dev *dev = i2c_get_adapdata(adap);
	int ret;
	unsigned int_addr_flag = 0;
	struct i2c_msg *m_start = msg;
	bool is_read;
	u8 *dma_buf = NULL;

	dev_dbg(&adap->dev, "at91_xfer: processing %d messages:\n", num);

	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		goto out;

	if (num == 2) {
		int internal_address = 0;
		int i;

		/* 1st msg is put into the internal address, start with 2nd */
		m_start = &msg[1];
		for (i = 0; i < msg->len; ++i) {
			const unsigned addr = msg->buf[msg->len - 1 - i];

			internal_address |= addr << (8 * i);
			int_addr_flag += AT91_TWI_IADRSZ_1;
		}
		at91_twi_write(dev, AT91_TWI_IADR, internal_address);
	}

	dev->use_alt_cmd = false;
	is_read = (m_start->flags & I2C_M_RD);
	if (dev->pdata->has_alt_cmd) {
		if (m_start->len > 0 &&
		    m_start->len < AT91_I2C_MAX_ALT_CMD_DATA_SIZE) {
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_ACMEN);
			at91_twi_write(dev, AT91_TWI_ACR,
				       AT91_TWI_ACR_DATAL(m_start->len) |
				       ((is_read) ? AT91_TWI_ACR_DIR : 0));
			dev->use_alt_cmd = true;
		} else {
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_ACMDIS);
		}
	}

	at91_twi_write(dev, AT91_TWI_MMR,
		       (m_start->addr << 16) |
		       int_addr_flag |
		       ((!dev->use_alt_cmd && is_read) ? AT91_TWI_MREAD : 0));

	dev->buf_len = m_start->len;
	dev->buf = m_start->buf;
	dev->msg = m_start;
	dev->recv_len_abort = false;

	if (dev->use_dma) {
		dma_buf = i2c_get_dma_safe_msg_buf(m_start, 1);
		if (!dma_buf) {
			ret = -ENOMEM;
			goto out;
		}
		dev->buf = dma_buf;
	}

	ret = at91_do_twi_transfer(dev);
	i2c_put_dma_safe_msg_buf(dma_buf, m_start, !ret);

	ret = (ret < 0) ? ret : num;
out:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return ret;
}

/*
 * The hardware can handle at most two messages concatenated by a
 * repeated start via it's internal address feature.
 */
static const struct i2c_adapter_quirks at91_twi_quirks = {
	.flags = I2C_AQ_COMB | I2C_AQ_COMB_WRITE_FIRST | I2C_AQ_COMB_SAME_ADDR,
	.max_comb_1st_msg_len = 3,
};

static u32 at91_twi_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
		| I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}

static const struct i2c_algorithm at91_twi_algorithm = {
	.xfer = at91_twi_xfer,
	.functionality = at91_twi_func,
};

static int at91_twi_configure_dma(struct at91_twi_dev *dev, u32 phy_addr)
{
	int ret = 0;
	struct dma_slave_config slave_config;
	struct at91_twi_dma *dma = &dev->dma;
	enum dma_slave_buswidth addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;

	/*
	 * The actual width of the access will be chosen in
	 * dmaengine_prep_slave_sg():
	 * for each buffer in the scatter-gather list, if its size is aligned
	 * to addr_width then addr_width accesses will be performed to transfer
	 * the buffer. On the other hand, if the buffer size is not aligned to
	 * addr_width then the buffer is transferred using single byte accesses.
	 * Please refer to the Atmel eXtended DMA controller driver.
	 * When FIFOs are used, the TXRDYM threshold can always be set to
	 * trigger the XDMAC when at least 4 data can be written into the TX
	 * FIFO, even if single byte accesses are performed.
	 * However the RXRDYM threshold must be set to fit the access width,
	 * deduced from buffer length, so the XDMAC is triggered properly to
	 * read data from the RX FIFO.
	 */
	if (dev->fifo_size)
		addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	memset(&slave_config, 0, sizeof(slave_config));
	slave_config.src_addr = (dma_addr_t)phy_addr + AT91_TWI_RHR;
	slave_config.src_addr_width = addr_width;
	slave_config.src_maxburst = 1;
	slave_config.dst_addr = (dma_addr_t)phy_addr + AT91_TWI_THR;
	slave_config.dst_addr_width = addr_width;
	slave_config.dst_maxburst = 1;
	slave_config.device_fc = false;

	dma->chan_tx = dma_request_chan(dev->dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		ret = PTR_ERR(dma->chan_tx);
		dma->chan_tx = NULL;
		goto error;
	}

	dma->chan_rx = dma_request_chan(dev->dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		ret = PTR_ERR(dma->chan_rx);
		dma->chan_rx = NULL;
		goto error;
	}

	slave_config.direction = DMA_MEM_TO_DEV;
	if (dmaengine_slave_config(dma->chan_tx, &slave_config)) {
		dev_err(dev->dev, "failed to configure tx channel\n");
		ret = -EINVAL;
		goto error;
	}

	slave_config.direction = DMA_DEV_TO_MEM;
	if (dmaengine_slave_config(dma->chan_rx, &slave_config)) {
		dev_err(dev->dev, "failed to configure rx channel\n");
		ret = -EINVAL;
		goto error;
	}

	sg_init_table(dma->sg, 2);
	dma->buf_mapped = false;
	dma->xfer_in_progress = false;
	dev->use_dma = true;

	dev_info(dev->dev, "using %s (tx) and %s (rx) for DMA transfers\n",
		 dma_chan_name(dma->chan_tx), dma_chan_name(dma->chan_rx));

	return ret;

error:
	if (ret != -EPROBE_DEFER)
		dev_info(dev->dev, "can't get DMA channel, continue without DMA support\n");
	if (dma->chan_rx)
		dma_release_channel(dma->chan_rx);
	if (dma->chan_tx)
		dma_release_channel(dma->chan_tx);
	return ret;
}

static int at91_init_twi_recovery_gpio(struct platform_device *pdev,
				       struct at91_twi_dev *dev)
{
	struct i2c_bus_recovery_info *rinfo = &dev->rinfo;

	rinfo->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!rinfo->pinctrl) {
		dev_info(dev->dev, "pinctrl unavailable, bus recovery not supported\n");
		return 0;
	}
	if (IS_ERR(rinfo->pinctrl)) {
		dev_info(dev->dev, "can't get pinctrl, bus recovery not supported\n");
		return PTR_ERR(rinfo->pinctrl);
	}
	dev->adapter.bus_recovery_info = rinfo;

	return 0;
}

static int at91_twi_recover_bus_cmd(struct i2c_adapter *adap)
{
	struct at91_twi_dev *dev = i2c_get_adapdata(adap);

	dev->transfer_status |= at91_twi_read(dev, AT91_TWI_SR);
	if (!(dev->transfer_status & AT91_TWI_SDA)) {
		dev_dbg(dev->dev, "SDA is down; sending bus clear command\n");
		if (dev->use_alt_cmd) {
			unsigned int acr;

			acr = at91_twi_read(dev, AT91_TWI_ACR);
			acr &= ~AT91_TWI_ACR_DATAL_MASK;
			at91_twi_write(dev, AT91_TWI_ACR, acr);
		}
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_CLEAR);
	}

	return 0;
}

static int at91_init_twi_recovery_info(struct platform_device *pdev,
				       struct at91_twi_dev *dev)
{
	struct i2c_bus_recovery_info *rinfo = &dev->rinfo;
	bool has_clear_cmd = dev->pdata->has_clear_cmd;

	if (!has_clear_cmd)
		return at91_init_twi_recovery_gpio(pdev, dev);

	rinfo->recover_bus = at91_twi_recover_bus_cmd;
	dev->adapter.bus_recovery_info = rinfo;

	return 0;
}

int at91_twi_probe_master(struct platform_device *pdev,
			  u32 phy_addr, struct at91_twi_dev *dev)
{
	int rc;

	init_completion(&dev->cmd_complete);

	rc = devm_request_irq(&pdev->dev, dev->irq, atmel_twi_interrupt, 0,
			      dev_name(dev->dev), dev);
	if (rc) {
		dev_err(dev->dev, "Cannot get irq %d: %d\n", dev->irq, rc);
		return rc;
	}

	if (dev->dev->of_node) {
		rc = at91_twi_configure_dma(dev, phy_addr);
		if (rc == -EPROBE_DEFER)
			return rc;
	}

	if (!of_property_read_u32(pdev->dev.of_node, "atmel,fifo-size",
				  &dev->fifo_size)) {
		dev_info(dev->dev, "Using FIFO (%u data)\n", dev->fifo_size);
	}

	dev->enable_dig_filt = of_property_read_bool(pdev->dev.of_node,
						     "i2c-digital-filter");

	dev->enable_ana_filt = of_property_read_bool(pdev->dev.of_node,
						     "i2c-analog-filter");
	at91_calc_twi_clock(dev);

	rc = at91_init_twi_recovery_info(pdev, dev);
	if (rc == -EPROBE_DEFER)
		return rc;

	dev->adapter.algo = &at91_twi_algorithm;
	dev->adapter.quirks = &at91_twi_quirks;

	return 0;
}
