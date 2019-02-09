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
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/platform_data/dma-atmel.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>

#define DEFAULT_TWI_CLK_HZ		100000		/* max 400 Kbits/s */
#define AT91_I2C_TIMEOUT	msecs_to_jiffies(100)	/* transfer timeout */
#define AT91_I2C_DMA_THRESHOLD	8			/* enable DMA if transfer size is bigger than this threshold */
#define AUTOSUSPEND_TIMEOUT		2000

/* AT91 TWI register definitions */
#define	AT91_TWI_CR		0x0000	/* Control Register */
#define	AT91_TWI_START		BIT(0)	/* Send a Start Condition */
#define	AT91_TWI_STOP		BIT(1)	/* Send a Stop Condition */
#define	AT91_TWI_MSEN		BIT(2)	/* Master Transfer Enable */
#define	AT91_TWI_MSDIS		BIT(3)	/* Master Transfer Disable */
#define	AT91_TWI_SVEN		BIT(4)	/* Slave Transfer Enable */
#define	AT91_TWI_SVDIS		BIT(5)	/* Slave Transfer Disable */
#define	AT91_TWI_QUICK		BIT(6)	/* SMBus quick command */
#define	AT91_TWI_SWRST		BIT(7)	/* Software Reset */
#define	AT91_TWI_ACMEN		BIT(16) /* Alternative Command Mode Enable */
#define	AT91_TWI_ACMDIS		BIT(17) /* Alternative Command Mode Disable */
#define	AT91_TWI_THRCLR		BIT(24) /* Transmit Holding Register Clear */
#define	AT91_TWI_RHRCLR		BIT(25) /* Receive Holding Register Clear */
#define	AT91_TWI_LOCKCLR	BIT(26) /* Lock Clear */
#define	AT91_TWI_FIFOEN		BIT(28) /* FIFO Enable */
#define	AT91_TWI_FIFODIS	BIT(29) /* FIFO Disable */

#define	AT91_TWI_MMR		0x0004	/* Master Mode Register */
#define	AT91_TWI_IADRSZ_1	0x0100	/* Internal Device Address Size */
#define	AT91_TWI_MREAD		BIT(12)	/* Master Read Direction */

#define	AT91_TWI_IADR		0x000c	/* Internal Address Register */

#define	AT91_TWI_CWGR		0x0010	/* Clock Waveform Generator Reg */

#define	AT91_TWI_SR		0x0020	/* Status Register */
#define	AT91_TWI_TXCOMP		BIT(0)	/* Transmission Complete */
#define	AT91_TWI_RXRDY		BIT(1)	/* Receive Holding Register Ready */
#define	AT91_TWI_TXRDY		BIT(2)	/* Transmit Holding Register Ready */
#define	AT91_TWI_OVRE		BIT(6)	/* Overrun Error */
#define	AT91_TWI_UNRE		BIT(7)	/* Underrun Error */
#define	AT91_TWI_NACK		BIT(8)	/* Not Acknowledged */
#define	AT91_TWI_LOCK		BIT(23) /* TWI Lock due to Frame Errors */

#define	AT91_TWI_INT_MASK \
	(AT91_TWI_TXCOMP | AT91_TWI_RXRDY | AT91_TWI_TXRDY | AT91_TWI_NACK)

#define	AT91_TWI_IER		0x0024	/* Interrupt Enable Register */
#define	AT91_TWI_IDR		0x0028	/* Interrupt Disable Register */
#define	AT91_TWI_IMR		0x002c	/* Interrupt Mask Register */
#define	AT91_TWI_RHR		0x0030	/* Receive Holding Register */
#define	AT91_TWI_THR		0x0034	/* Transmit Holding Register */

#define	AT91_TWI_ACR		0x0040	/* Alternative Command Register */
#define	AT91_TWI_ACR_DATAL(len)	((len) & 0xff)
#define	AT91_TWI_ACR_DIR	BIT(8)

#define	AT91_TWI_FMR		0x0050	/* FIFO Mode Register */
#define	AT91_TWI_FMR_TXRDYM(mode)	(((mode) & 0x3) << 0)
#define	AT91_TWI_FMR_TXRDYM_MASK	(0x3 << 0)
#define	AT91_TWI_FMR_RXRDYM(mode)	(((mode) & 0x3) << 4)
#define	AT91_TWI_FMR_RXRDYM_MASK	(0x3 << 4)
#define	AT91_TWI_ONE_DATA	0x0
#define	AT91_TWI_TWO_DATA	0x1
#define	AT91_TWI_FOUR_DATA	0x2

#define	AT91_TWI_FLR		0x0054	/* FIFO Level Register */

#define	AT91_TWI_FSR		0x0060	/* FIFO Status Register */
#define	AT91_TWI_FIER		0x0064	/* FIFO Interrupt Enable Register */
#define	AT91_TWI_FIDR		0x0068	/* FIFO Interrupt Disable Register */
#define	AT91_TWI_FIMR		0x006c	/* FIFO Interrupt Mask Register */

#define	AT91_TWI_VER		0x00fc	/* Version Register */

struct at91_twi_pdata {
	unsigned clk_max_div;
	unsigned clk_offset;
	bool has_unre_flag;
	bool has_alt_cmd;
	struct at_dma_slave dma_slave;
};

struct at91_twi_dma {
	struct dma_chan *chan_rx;
	struct dma_chan *chan_tx;
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor *data_desc;
	enum dma_data_direction direction;
	bool buf_mapped;
	bool xfer_in_progress;
};

struct at91_twi_dev {
	struct device *dev;
	void __iomem *base;
	struct completion cmd_complete;
	struct clk *clk;
	u8 *buf;
	size_t buf_len;
	struct i2c_msg *msg;
	int irq;
	unsigned imr;
	unsigned transfer_status;
	struct i2c_adapter adapter;
	unsigned twi_cwgr_reg;
	struct at91_twi_pdata *pdata;
	bool use_dma;
	bool recv_len_abort;
	u32 fifo_size;
	struct at91_twi_dma dma;
};

static unsigned at91_twi_read(struct at91_twi_dev *dev, unsigned reg)
{
	return readl_relaxed(dev->base + reg);
}

static void at91_twi_write(struct at91_twi_dev *dev, unsigned reg, unsigned val)
{
	writel_relaxed(val, dev->base + reg);
}

static void at91_disable_twi_interrupts(struct at91_twi_dev *dev)
{
	at91_twi_write(dev, AT91_TWI_IDR, AT91_TWI_INT_MASK);
}

static void at91_twi_irq_save(struct at91_twi_dev *dev)
{
	dev->imr = at91_twi_read(dev, AT91_TWI_IMR) & AT91_TWI_INT_MASK;
	at91_disable_twi_interrupts(dev);
}

static void at91_twi_irq_restore(struct at91_twi_dev *dev)
{
	at91_twi_write(dev, AT91_TWI_IER, dev->imr);
}

static void at91_init_twi_bus(struct at91_twi_dev *dev)
{
	at91_disable_twi_interrupts(dev);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_SWRST);
	/* FIFO should be enabled immediately after the software reset */
	if (dev->fifo_size)
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_FIFOEN);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_MSEN);
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_SVDIS);
	at91_twi_write(dev, AT91_TWI_CWGR, dev->twi_cwgr_reg);
}

/*
 * Calculate symmetric clock as stated in datasheet:
 * twi_clk = F_MAIN / (2 * (cdiv * (1 << ckdiv) + offset))
 */
static void at91_calc_twi_clock(struct at91_twi_dev *dev, int twi_clk)
{
	int ckdiv, cdiv, div;
	struct at91_twi_pdata *pdata = dev->pdata;
	int offset = pdata->clk_offset;
	int max_ckdiv = pdata->clk_max_div;

	div = max(0, (int)DIV_ROUND_UP(clk_get_rate(dev->clk),
				       2 * twi_clk) - offset);
	ckdiv = fls(div >> 8);
	cdiv = div >> ckdiv;

	if (ckdiv > max_ckdiv) {
		dev_warn(dev->dev, "%d exceeds ckdiv max value which is %d.\n",
			 ckdiv, max_ckdiv);
		ckdiv = max_ckdiv;
		cdiv = 255;
	}

	dev->twi_cwgr_reg = (ckdiv << 16) | (cdiv << 8) | cdiv;
	dev_dbg(dev->dev, "cdiv %d ckdiv %d\n", cdiv, ckdiv);
}

static void at91_twi_dma_cleanup(struct at91_twi_dev *dev)
{
	struct at91_twi_dma *dma = &dev->dma;

	at91_twi_irq_save(dev);

	if (dma->xfer_in_progress) {
		if (dma->direction == DMA_FROM_DEVICE)
			dmaengine_terminate_all(dma->chan_rx);
		else
			dmaengine_terminate_all(dma->chan_tx);
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
	if (--dev->buf_len == 0)
		if (!dev->pdata->has_alt_cmd)
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_STOP);

	dev_dbg(dev->dev, "wrote 0x%x, to go %d\n", *dev->buf, dev->buf_len);

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
	if (!dev->pdata->has_alt_cmd)
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
			dev_dbg(dev->dev, "received block length %d\n",
					 dev->buf_len);
		} else {
			/* abort and send the stop by reading one more byte */
			dev->recv_len_abort = true;
			dev->buf_len = 1;
		}
	}

	/* send stop if second but last byte has been read */
	if (!dev->pdata->has_alt_cmd && dev->buf_len == 1)
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_STOP);

	dev_dbg(dev->dev, "read 0x%x, to go %d\n", *dev->buf, dev->buf_len);

	++dev->buf;
}

static void at91_twi_read_data_dma_callback(void *data)
{
	struct at91_twi_dev *dev = (struct at91_twi_dev *)data;
	unsigned ier = AT91_TWI_TXCOMP;

	dma_unmap_single(dev->dev, sg_dma_address(&dev->dma.sg[0]),
			 dev->buf_len, DMA_FROM_DEVICE);

	if (!dev->pdata->has_alt_cmd) {
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

	buf_len = (dev->pdata->has_alt_cmd) ? dev->buf_len : dev->buf_len - 2;
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
	if (irqstatus & AT91_TWI_RXRDY)
		at91_twi_read_next_byte(dev);

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

	dev_dbg(dev->dev, "transfer: %s %d bytes.\n",
		(dev->msg->flags & I2C_M_RD) ? "read" : "write", dev->buf_len);

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
		if (!has_alt_cmd && dev->buf_len <= 1 &&
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
				       AT91_TWI_TXCOMP |
				       AT91_TWI_NACK |
				       AT91_TWI_TXRDY);
		}
	}

	time_left = wait_for_completion_timeout(&dev->cmd_complete,
					      dev->adapter.timeout);
	if (time_left == 0) {
		dev->transfer_status |= at91_twi_read(dev, AT91_TWI_SR);
		dev_err(dev->dev, "controller timed out\n");
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
	return ret;
}

static int at91_twi_xfer(struct i2c_adapter *adap, struct i2c_msg *msg, int num)
{
	struct at91_twi_dev *dev = i2c_get_adapdata(adap);
	int ret;
	unsigned int_addr_flag = 0;
	struct i2c_msg *m_start = msg;
	bool is_read, use_alt_cmd = false;

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

	is_read = (m_start->flags & I2C_M_RD);
	if (dev->pdata->has_alt_cmd) {
		if (m_start->len > 0) {
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_ACMEN);
			at91_twi_write(dev, AT91_TWI_ACR,
				       AT91_TWI_ACR_DATAL(m_start->len) |
				       ((is_read) ? AT91_TWI_ACR_DIR : 0));
			use_alt_cmd = true;
		} else {
			at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_ACMDIS);
		}
	}

	at91_twi_write(dev, AT91_TWI_MMR,
		       (m_start->addr << 16) |
		       int_addr_flag |
		       ((!use_alt_cmd && is_read) ? AT91_TWI_MREAD : 0));

	dev->buf_len = m_start->len;
	dev->buf = m_start->buf;
	dev->msg = m_start;
	dev->recv_len_abort = false;

	ret = at91_do_twi_transfer(dev);

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
static struct i2c_adapter_quirks at91_twi_quirks = {
	.flags = I2C_AQ_COMB | I2C_AQ_COMB_WRITE_FIRST | I2C_AQ_COMB_SAME_ADDR,
	.max_comb_1st_msg_len = 3,
};

static u32 at91_twi_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL
		| I2C_FUNC_SMBUS_READ_BLOCK_DATA;
}

static struct i2c_algorithm at91_twi_algorithm = {
	.master_xfer	= at91_twi_xfer,
	.functionality	= at91_twi_func,
};

static struct at91_twi_pdata at91rm9200_config = {
	.clk_max_div = 5,
	.clk_offset = 3,
	.has_unre_flag = true,
	.has_alt_cmd = false,
};

static struct at91_twi_pdata at91sam9261_config = {
	.clk_max_div = 5,
	.clk_offset = 4,
	.has_unre_flag = false,
	.has_alt_cmd = false,
};

static struct at91_twi_pdata at91sam9260_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_unre_flag = false,
	.has_alt_cmd = false,
};

static struct at91_twi_pdata at91sam9g20_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_unre_flag = false,
	.has_alt_cmd = false,
};

static struct at91_twi_pdata at91sam9g10_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_unre_flag = false,
	.has_alt_cmd = false,
};

static const struct platform_device_id at91_twi_devtypes[] = {
	{
		.name = "i2c-at91rm9200",
		.driver_data = (unsigned long) &at91rm9200_config,
	}, {
		.name = "i2c-at91sam9261",
		.driver_data = (unsigned long) &at91sam9261_config,
	}, {
		.name = "i2c-at91sam9260",
		.driver_data = (unsigned long) &at91sam9260_config,
	}, {
		.name = "i2c-at91sam9g20",
		.driver_data = (unsigned long) &at91sam9g20_config,
	}, {
		.name = "i2c-at91sam9g10",
		.driver_data = (unsigned long) &at91sam9g10_config,
	}, {
		/* sentinel */
	}
};

#if defined(CONFIG_OF)
static struct at91_twi_pdata at91sam9x5_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_unre_flag = false,
	.has_alt_cmd = false,
};

static struct at91_twi_pdata sama5d2_config = {
	.clk_max_div = 7,
	.clk_offset = 4,
	.has_unre_flag = true,
	.has_alt_cmd = true,
};

static const struct of_device_id atmel_twi_dt_ids[] = {
	{
		.compatible = "atmel,at91rm9200-i2c",
		.data = &at91rm9200_config,
	} , {
		.compatible = "atmel,at91sam9260-i2c",
		.data = &at91sam9260_config,
	} , {
		.compatible = "atmel,at91sam9261-i2c",
		.data = &at91sam9261_config,
	} , {
		.compatible = "atmel,at91sam9g20-i2c",
		.data = &at91sam9g20_config,
	} , {
		.compatible = "atmel,at91sam9g10-i2c",
		.data = &at91sam9g10_config,
	}, {
		.compatible = "atmel,at91sam9x5-i2c",
		.data = &at91sam9x5_config,
	}, {
		.compatible = "atmel,sama5d2-i2c",
		.data = &sama5d2_config,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_twi_dt_ids);
#endif

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

	dma->chan_tx = dma_request_slave_channel_reason(dev->dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		ret = PTR_ERR(dma->chan_tx);
		dma->chan_tx = NULL;
		goto error;
	}

	dma->chan_rx = dma_request_slave_channel_reason(dev->dev, "rx");
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
		dev_info(dev->dev, "can't use DMA, error %d\n", ret);
	if (dma->chan_rx)
		dma_release_channel(dma->chan_rx);
	if (dma->chan_tx)
		dma_release_channel(dma->chan_tx);
	return ret;
}

static struct at91_twi_pdata *at91_twi_get_driver_data(
					struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(atmel_twi_dt_ids, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct at91_twi_pdata *)match->data;
	}
	return (struct at91_twi_pdata *) platform_get_device_id(pdev)->driver_data;
}

static int at91_twi_probe(struct platform_device *pdev)
{
	struct at91_twi_dev *dev;
	struct resource *mem;
	int rc;
	u32 phy_addr;
	u32 bus_clk_rate;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	init_completion(&dev->cmd_complete);
	dev->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -ENODEV;
	phy_addr = mem->start;

	dev->pdata = at91_twi_get_driver_data(pdev);
	if (!dev->pdata)
		return -ENODEV;

	dev->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0)
		return dev->irq;

	rc = devm_request_irq(&pdev->dev, dev->irq, atmel_twi_interrupt, 0,
			 dev_name(dev->dev), dev);
	if (rc) {
		dev_err(dev->dev, "Cannot get irq %d: %d\n", dev->irq, rc);
		return rc;
	}

	platform_set_drvdata(pdev, dev);

	dev->clk = devm_clk_get(dev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(dev->dev, "no clock defined\n");
		return -ENODEV;
	}
	clk_prepare_enable(dev->clk);

	if (dev->dev->of_node) {
		rc = at91_twi_configure_dma(dev, phy_addr);
		if (rc == -EPROBE_DEFER)
			return rc;
	}

	if (!of_property_read_u32(pdev->dev.of_node, "atmel,fifo-size",
				  &dev->fifo_size)) {
		dev_info(dev->dev, "Using FIFO (%u data)\n", dev->fifo_size);
	}

	rc = of_property_read_u32(dev->dev->of_node, "clock-frequency",
			&bus_clk_rate);
	if (rc)
		bus_clk_rate = DEFAULT_TWI_CLK_HZ;

	at91_calc_twi_clock(dev, bus_clk_rate);
	at91_init_twi_bus(dev);

	snprintf(dev->adapter.name, sizeof(dev->adapter.name), "AT91");
	i2c_set_adapdata(&dev->adapter, dev);
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_DEPRECATED;
	dev->adapter.algo = &at91_twi_algorithm;
	dev->adapter.quirks = &at91_twi_quirks;
	dev->adapter.dev.parent = dev->dev;
	dev->adapter.nr = pdev->id;
	dev->adapter.timeout = AT91_I2C_TIMEOUT;
	dev->adapter.dev.of_node = pdev->dev.of_node;

	pm_runtime_set_autosuspend_delay(dev->dev, AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev->dev);
	pm_runtime_set_active(dev->dev);
	pm_runtime_enable(dev->dev);

	rc = i2c_add_numbered_adapter(&dev->adapter);
	if (rc) {
		dev_err(dev->dev, "Adapter %s registration failed\n",
			dev->adapter.name);
		clk_disable_unprepare(dev->clk);

		pm_runtime_disable(dev->dev);
		pm_runtime_set_suspended(dev->dev);

		return rc;
	}

	dev_info(dev->dev, "AT91 i2c bus driver (hw version: %#x).\n",
		 at91_twi_read(dev, AT91_TWI_VER));
	return 0;
}

static int at91_twi_remove(struct platform_device *pdev)
{
	struct at91_twi_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adapter);
	clk_disable_unprepare(dev->clk);

	pm_runtime_disable(dev->dev);
	pm_runtime_set_suspended(dev->dev);

	return 0;
}

#ifdef CONFIG_PM

static int at91_twi_runtime_suspend(struct device *dev)
{
	struct at91_twi_dev *twi_dev = dev_get_drvdata(dev);

	clk_disable_unprepare(twi_dev->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int at91_twi_runtime_resume(struct device *dev)
{
	struct at91_twi_dev *twi_dev = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);

	return clk_prepare_enable(twi_dev->clk);
}

static int at91_twi_suspend_noirq(struct device *dev)
{
	if (!pm_runtime_status_suspended(dev))
		at91_twi_runtime_suspend(dev);

	return 0;
}

static int at91_twi_resume_noirq(struct device *dev)
{
	int ret;

	if (!pm_runtime_status_suspended(dev)) {
		ret = at91_twi_runtime_resume(dev);
		if (ret)
			return ret;
	}

	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);

	return 0;
}

static const struct dev_pm_ops at91_twi_pm = {
	.suspend_noirq	= at91_twi_suspend_noirq,
	.resume_noirq	= at91_twi_resume_noirq,
	.runtime_suspend	= at91_twi_runtime_suspend,
	.runtime_resume		= at91_twi_runtime_resume,
};

#define at91_twi_pm_ops (&at91_twi_pm)
#else
#define at91_twi_pm_ops NULL
#endif

static struct platform_driver at91_twi_driver = {
	.probe		= at91_twi_probe,
	.remove		= at91_twi_remove,
	.id_table	= at91_twi_devtypes,
	.driver		= {
		.name	= "at91_i2c",
		.of_match_table = of_match_ptr(atmel_twi_dt_ids),
		.pm	= at91_twi_pm_ops,
	},
};

static int __init at91_twi_init(void)
{
	return platform_driver_register(&at91_twi_driver);
}

static void __exit at91_twi_exit(void)
{
	platform_driver_unregister(&at91_twi_driver);
}

subsys_initcall(at91_twi_init);
module_exit(at91_twi_exit);

MODULE_AUTHOR("Nikolaus Voss <n.voss@weinmann.de>");
MODULE_DESCRIPTION("I2C (TWI) driver for Atmel AT91");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_i2c");
