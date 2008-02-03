/*
 * Driver for Atmel AT32 and AT91 SPI Controllers
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>

#include <asm/io.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/cpu.h>

#include "atmel_spi.h"

/*
 * The core SPI transfer engine just talks to a register bank to set up
 * DMA transfers; transfer queue progress is driven by IRQs.  The clock
 * framework provides the base clock, subdivided for each spi_device.
 *
 * Newer controllers, marked with "new_1" flag, have:
 *  - CR.LASTXFER
 *  - SPI_MR.DIV32 may become FDIV or must-be-zero (here: always zero)
 *  - SPI_SR.TXEMPTY, SPI_SR.NSSR (and corresponding irqs)
 *  - SPI_CSRx.CSAAT
 *  - SPI_CSRx.SBCR allows faster clocking
 */
struct atmel_spi {
	spinlock_t		lock;

	void __iomem		*regs;
	int			irq;
	struct clk		*clk;
	struct platform_device	*pdev;
	unsigned		new_1:1;
	struct spi_device	*stay;

	u8			stopping;
	struct list_head	queue;
	struct spi_transfer	*current_transfer;
	unsigned long		remaining_bytes;

	void			*buffer;
	dma_addr_t		buffer_dma;
};

#define BUFFER_SIZE		PAGE_SIZE
#define INVALID_DMA_ADDRESS	0xffffffff

/*
 * Earlier SPI controllers (e.g. on at91rm9200) have a design bug whereby
 * they assume that spi slave device state will not change on deselect, so
 * that automagic deselection is OK.  ("NPCSx rises if no data is to be
 * transmitted")  Not so!  Workaround uses nCSx pins as GPIOs; or newer
 * controllers have CSAAT and friends.
 *
 * Since the CSAAT functionality is a bit weird on newer controllers as
 * well, we use GPIO to control nCSx pins on all controllers, updating
 * MR.PCS to avoid confusing the controller.  Using GPIOs also lets us
 * support active-high chipselects despite the controller's belief that
 * only active-low devices/systems exists.
 *
 * However, at91rm9200 has a second erratum whereby nCS0 doesn't work
 * right when driven with GPIO.  ("Mode Fault does not allow more than one
 * Master on Chip Select 0.")  No workaround exists for that ... so for
 * nCS0 on that chip, we (a) don't use the GPIO, (b) can't support CS_HIGH,
 * and (c) will trigger that first erratum in some cases.
 */

static void cs_activate(struct atmel_spi *as, struct spi_device *spi)
{
	unsigned gpio = (unsigned) spi->controller_data;
	unsigned active = spi->mode & SPI_CS_HIGH;
	u32 mr;

	mr = spi_readl(as, MR);
	mr = SPI_BFINS(PCS, ~(1 << spi->chip_select), mr);

	dev_dbg(&spi->dev, "activate %u%s, mr %08x\n",
			gpio, active ? " (high)" : "",
			mr);

	if (!(cpu_is_at91rm9200() && spi->chip_select == 0))
		gpio_set_value(gpio, active);
	spi_writel(as, MR, mr);
}

static void cs_deactivate(struct atmel_spi *as, struct spi_device *spi)
{
	unsigned gpio = (unsigned) spi->controller_data;
	unsigned active = spi->mode & SPI_CS_HIGH;
	u32 mr;

	/* only deactivate *this* device; sometimes transfers to
	 * another device may be active when this routine is called.
	 */
	mr = spi_readl(as, MR);
	if (~SPI_BFEXT(PCS, mr) & (1 << spi->chip_select)) {
		mr = SPI_BFINS(PCS, 0xf, mr);
		spi_writel(as, MR, mr);
	}

	dev_dbg(&spi->dev, "DEactivate %u%s, mr %08x\n",
			gpio, active ? " (low)" : "",
			mr);

	if (!(cpu_is_at91rm9200() && spi->chip_select == 0))
		gpio_set_value(gpio, !active);
}

/*
 * Submit next transfer for DMA.
 * lock is held, spi irq is blocked
 */
static void atmel_spi_next_xfer(struct spi_master *master,
				struct spi_message *msg)
{
	struct atmel_spi	*as = spi_master_get_devdata(master);
	struct spi_transfer	*xfer;
	u32			len;
	dma_addr_t		tx_dma, rx_dma;

	xfer = as->current_transfer;
	if (!xfer || as->remaining_bytes == 0) {
		if (xfer)
			xfer = list_entry(xfer->transfer_list.next,
					struct spi_transfer, transfer_list);
		else
			xfer = list_entry(msg->transfers.next,
					struct spi_transfer, transfer_list);
		as->remaining_bytes = xfer->len;
		as->current_transfer = xfer;
	}

	len = as->remaining_bytes;

	tx_dma = xfer->tx_dma + xfer->len - len;
	rx_dma = xfer->rx_dma + xfer->len - len;

	/* use scratch buffer only when rx or tx data is unspecified */
	if (!xfer->rx_buf) {
		rx_dma = as->buffer_dma;
		if (len > BUFFER_SIZE)
			len = BUFFER_SIZE;
	}
	if (!xfer->tx_buf) {
		tx_dma = as->buffer_dma;
		if (len > BUFFER_SIZE)
			len = BUFFER_SIZE;
		memset(as->buffer, 0, len);
		dma_sync_single_for_device(&as->pdev->dev,
				as->buffer_dma, len, DMA_TO_DEVICE);
	}

	spi_writel(as, RPR, rx_dma);
	spi_writel(as, TPR, tx_dma);

	as->remaining_bytes -= len;
	if (msg->spi->bits_per_word > 8)
		len >>= 1;

	/* REVISIT: when xfer->delay_usecs == 0, the PDC "next transfer"
	 * mechanism might help avoid the IRQ latency between transfers
	 * (and improve the nCS0 errata handling on at91rm9200 chips)
	 *
	 * We're also waiting for ENDRX before we start the next
	 * transfer because we need to handle some difficult timing
	 * issues otherwise. If we wait for ENDTX in one transfer and
	 * then starts waiting for ENDRX in the next, it's difficult
	 * to tell the difference between the ENDRX interrupt we're
	 * actually waiting for and the ENDRX interrupt of the
	 * previous transfer.
	 *
	 * It should be doable, though. Just not now...
	 */
	spi_writel(as, TNCR, 0);
	spi_writel(as, RNCR, 0);
	spi_writel(as, IER, SPI_BIT(ENDRX) | SPI_BIT(OVRES));

	dev_dbg(&msg->spi->dev,
		"  start xfer %p: len %u tx %p/%08x rx %p/%08x imr %03x\n",
		xfer, xfer->len, xfer->tx_buf, xfer->tx_dma,
		xfer->rx_buf, xfer->rx_dma, spi_readl(as, IMR));

	spi_writel(as, RCR, len);
	spi_writel(as, TCR, len);
	spi_writel(as, PTCR, SPI_BIT(TXTEN) | SPI_BIT(RXTEN));
}

static void atmel_spi_next_message(struct spi_master *master)
{
	struct atmel_spi	*as = spi_master_get_devdata(master);
	struct spi_message	*msg;
	struct spi_device	*spi;

	BUG_ON(as->current_transfer);

	msg = list_entry(as->queue.next, struct spi_message, queue);
	spi = msg->spi;

	dev_dbg(master->dev.parent, "start message %p for %s\n",
			msg, spi->dev.bus_id);

	/* select chip if it's not still active */
	if (as->stay) {
		if (as->stay != spi) {
			cs_deactivate(as, as->stay);
			cs_activate(as, spi);
		}
		as->stay = NULL;
	} else
		cs_activate(as, spi);

	atmel_spi_next_xfer(master, msg);
}

/*
 * For DMA, tx_buf/tx_dma have the same relationship as rx_buf/rx_dma:
 *  - The buffer is either valid for CPU access, else NULL
 *  - If the buffer is valid, so is its DMA addresss
 *
 * This driver manages the dma addresss unless message->is_dma_mapped.
 */
static int
atmel_spi_dma_map_xfer(struct atmel_spi *as, struct spi_transfer *xfer)
{
	struct device	*dev = &as->pdev->dev;

	xfer->tx_dma = xfer->rx_dma = INVALID_DMA_ADDRESS;
	if (xfer->tx_buf) {
		xfer->tx_dma = dma_map_single(dev,
				(void *) xfer->tx_buf, xfer->len,
				DMA_TO_DEVICE);
		if (dma_mapping_error(xfer->tx_dma))
			return -ENOMEM;
	}
	if (xfer->rx_buf) {
		xfer->rx_dma = dma_map_single(dev,
				xfer->rx_buf, xfer->len,
				DMA_FROM_DEVICE);
		if (dma_mapping_error(xfer->rx_dma)) {
			if (xfer->tx_buf)
				dma_unmap_single(dev,
						xfer->tx_dma, xfer->len,
						DMA_TO_DEVICE);
			return -ENOMEM;
		}
	}
	return 0;
}

static void atmel_spi_dma_unmap_xfer(struct spi_master *master,
				     struct spi_transfer *xfer)
{
	if (xfer->tx_dma != INVALID_DMA_ADDRESS)
		dma_unmap_single(master->dev.parent, xfer->tx_dma,
				 xfer->len, DMA_TO_DEVICE);
	if (xfer->rx_dma != INVALID_DMA_ADDRESS)
		dma_unmap_single(master->dev.parent, xfer->rx_dma,
				 xfer->len, DMA_FROM_DEVICE);
}

static void
atmel_spi_msg_done(struct spi_master *master, struct atmel_spi *as,
		struct spi_message *msg, int status, int stay)
{
	if (!stay || status < 0)
		cs_deactivate(as, msg->spi);
	else
		as->stay = msg->spi;

	list_del(&msg->queue);
	msg->status = status;

	dev_dbg(master->dev.parent,
		"xfer complete: %u bytes transferred\n",
		msg->actual_length);

	spin_unlock(&as->lock);
	msg->complete(msg->context);
	spin_lock(&as->lock);

	as->current_transfer = NULL;

	/* continue if needed */
	if (list_empty(&as->queue) || as->stopping)
		spi_writel(as, PTCR, SPI_BIT(RXTDIS) | SPI_BIT(TXTDIS));
	else
		atmel_spi_next_message(master);
}

static irqreturn_t
atmel_spi_interrupt(int irq, void *dev_id)
{
	struct spi_master	*master = dev_id;
	struct atmel_spi	*as = spi_master_get_devdata(master);
	struct spi_message	*msg;
	struct spi_transfer	*xfer;
	u32			status, pending, imr;
	int			ret = IRQ_NONE;

	spin_lock(&as->lock);

	xfer = as->current_transfer;
	msg = list_entry(as->queue.next, struct spi_message, queue);

	imr = spi_readl(as, IMR);
	status = spi_readl(as, SR);
	pending = status & imr;

	if (pending & SPI_BIT(OVRES)) {
		int timeout;

		ret = IRQ_HANDLED;

		spi_writel(as, IDR, (SPI_BIT(ENDTX) | SPI_BIT(ENDRX)
				     | SPI_BIT(OVRES)));

		/*
		 * When we get an overrun, we disregard the current
		 * transfer. Data will not be copied back from any
		 * bounce buffer and msg->actual_len will not be
		 * updated with the last xfer.
		 *
		 * We will also not process any remaning transfers in
		 * the message.
		 *
		 * First, stop the transfer and unmap the DMA buffers.
		 */
		spi_writel(as, PTCR, SPI_BIT(RXTDIS) | SPI_BIT(TXTDIS));
		if (!msg->is_dma_mapped)
			atmel_spi_dma_unmap_xfer(master, xfer);

		/* REVISIT: udelay in irq is unfriendly */
		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		dev_warn(master->dev.parent, "fifo overrun (%u/%u remaining)\n",
			 spi_readl(as, TCR), spi_readl(as, RCR));

		/*
		 * Clean up DMA registers and make sure the data
		 * registers are empty.
		 */
		spi_writel(as, RNCR, 0);
		spi_writel(as, TNCR, 0);
		spi_writel(as, RCR, 0);
		spi_writel(as, TCR, 0);
		for (timeout = 1000; timeout; timeout--)
			if (spi_readl(as, SR) & SPI_BIT(TXEMPTY))
				break;
		if (!timeout)
			dev_warn(master->dev.parent,
				 "timeout waiting for TXEMPTY");
		while (spi_readl(as, SR) & SPI_BIT(RDRF))
			spi_readl(as, RDR);

		/* Clear any overrun happening while cleaning up */
		spi_readl(as, SR);

		atmel_spi_msg_done(master, as, msg, -EIO, 0);
	} else if (pending & SPI_BIT(ENDRX)) {
		ret = IRQ_HANDLED;

		spi_writel(as, IDR, pending);

		if (as->remaining_bytes == 0) {
			msg->actual_length += xfer->len;

			if (!msg->is_dma_mapped)
				atmel_spi_dma_unmap_xfer(master, xfer);

			/* REVISIT: udelay in irq is unfriendly */
			if (xfer->delay_usecs)
				udelay(xfer->delay_usecs);

			if (msg->transfers.prev == &xfer->transfer_list) {
				/* report completed message */
				atmel_spi_msg_done(master, as, msg, 0,
						xfer->cs_change);
			} else {
				if (xfer->cs_change) {
					cs_deactivate(as, msg->spi);
					udelay(1);
					cs_activate(as, msg->spi);
				}

				/*
				 * Not done yet. Submit the next transfer.
				 *
				 * FIXME handle protocol options for xfer
				 */
				atmel_spi_next_xfer(master, msg);
			}
		} else {
			/*
			 * Keep going, we still have data to send in
			 * the current transfer.
			 */
			atmel_spi_next_xfer(master, msg);
		}
	}

	spin_unlock(&as->lock);

	return ret;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH)

static int atmel_spi_setup(struct spi_device *spi)
{
	struct atmel_spi	*as;
	u32			scbr, csr;
	unsigned int		bits = spi->bits_per_word;
	unsigned long		bus_hz, sck_hz;
	unsigned int		npcs_pin;
	int			ret;

	as = spi_master_get_devdata(spi->master);

	if (as->stopping)
		return -ESHUTDOWN;

	if (spi->chip_select > spi->master->num_chipselect) {
		dev_dbg(&spi->dev,
				"setup: invalid chipselect %u (%u defined)\n",
				spi->chip_select, spi->master->num_chipselect);
		return -EINVAL;
	}

	if (bits == 0)
		bits = 8;
	if (bits < 8 || bits > 16) {
		dev_dbg(&spi->dev,
				"setup: invalid bits_per_word %u (8 to 16)\n",
				bits);
		return -EINVAL;
	}

	if (spi->mode & ~MODEBITS) {
		dev_dbg(&spi->dev, "setup: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	/* see notes above re chipselect */
	if (cpu_is_at91rm9200()
			&& spi->chip_select == 0
			&& (spi->mode & SPI_CS_HIGH)) {
		dev_dbg(&spi->dev, "setup: can't be active-high\n");
		return -EINVAL;
	}

	/* speed zero convention is used by some upper layers */
	bus_hz = clk_get_rate(as->clk);
	if (spi->max_speed_hz) {
		/* assume div32/fdiv/mbz == 0 */
		if (!as->new_1)
			bus_hz /= 2;
		scbr = ((bus_hz + spi->max_speed_hz - 1)
			/ spi->max_speed_hz);
		if (scbr >= (1 << SPI_SCBR_SIZE)) {
			dev_dbg(&spi->dev,
				"setup: %d Hz too slow, scbr %u; min %ld Hz\n",
				spi->max_speed_hz, scbr, bus_hz/255);
			return -EINVAL;
		}
	} else
		scbr = 0xff;
	sck_hz = bus_hz / scbr;

	csr = SPI_BF(SCBR, scbr) | SPI_BF(BITS, bits - 8);
	if (spi->mode & SPI_CPOL)
		csr |= SPI_BIT(CPOL);
	if (!(spi->mode & SPI_CPHA))
		csr |= SPI_BIT(NCPHA);

	/* TODO: DLYBS and DLYBCT */
	csr |= SPI_BF(DLYBS, 10);
	csr |= SPI_BF(DLYBCT, 10);

	/* chipselect must have been muxed as GPIO (e.g. in board setup) */
	npcs_pin = (unsigned int)spi->controller_data;
	if (!spi->controller_state) {
		ret = gpio_request(npcs_pin, spi->dev.bus_id);
		if (ret)
			return ret;
		spi->controller_state = (void *)npcs_pin;
		gpio_direction_output(npcs_pin, !(spi->mode & SPI_CS_HIGH));
	} else {
		unsigned long		flags;

		spin_lock_irqsave(&as->lock, flags);
		if (as->stay == spi)
			as->stay = NULL;
		cs_deactivate(as, spi);
		spin_unlock_irqrestore(&as->lock, flags);
	}

	dev_dbg(&spi->dev,
		"setup: %lu Hz bpw %u mode 0x%x -> csr%d %08x\n",
		sck_hz, bits, spi->mode, spi->chip_select, csr);

	spi_writel(as, CSR0 + 4 * spi->chip_select, csr);

	return 0;
}

static int atmel_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct atmel_spi	*as;
	struct spi_transfer	*xfer;
	unsigned long		flags;
	struct device		*controller = spi->master->dev.parent;

	as = spi_master_get_devdata(spi->master);

	dev_dbg(controller, "new message %p submitted for %s\n",
			msg, spi->dev.bus_id);

	if (unlikely(list_empty(&msg->transfers)
			|| !spi->max_speed_hz))
		return -EINVAL;

	if (as->stopping)
		return -ESHUTDOWN;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!(xfer->tx_buf || xfer->rx_buf)) {
			dev_dbg(&spi->dev, "missing rx or tx buf\n");
			return -EINVAL;
		}

		/* FIXME implement these protocol options!! */
		if (xfer->bits_per_word || xfer->speed_hz) {
			dev_dbg(&spi->dev, "no protocol options yet\n");
			return -ENOPROTOOPT;
		}

		/*
		 * DMA map early, for performance (empties dcache ASAP) and
		 * better fault reporting.  This is a DMA-only driver.
		 *
		 * NOTE that if dma_unmap_single() ever starts to do work on
		 * platforms supported by this driver, we would need to clean
		 * up mappings for previously-mapped transfers.
		 */
		if (!msg->is_dma_mapped) {
			if (atmel_spi_dma_map_xfer(as, xfer) < 0)
				return -ENOMEM;
		}
	}

#ifdef VERBOSE
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		dev_dbg(controller,
			"  xfer %p: len %u tx %p/%08x rx %p/%08x\n",
			xfer, xfer->len,
			xfer->tx_buf, xfer->tx_dma,
			xfer->rx_buf, xfer->rx_dma);
	}
#endif

	msg->status = -EINPROGRESS;
	msg->actual_length = 0;

	spin_lock_irqsave(&as->lock, flags);
	list_add_tail(&msg->queue, &as->queue);
	if (!as->current_transfer)
		atmel_spi_next_message(spi->master);
	spin_unlock_irqrestore(&as->lock, flags);

	return 0;
}

static void atmel_spi_cleanup(struct spi_device *spi)
{
	struct atmel_spi	*as = spi_master_get_devdata(spi->master);
	unsigned		gpio = (unsigned) spi->controller_data;
	unsigned long		flags;

	if (!spi->controller_state)
		return;

	spin_lock_irqsave(&as->lock, flags);
	if (as->stay == spi) {
		as->stay = NULL;
		cs_deactivate(as, spi);
	}
	spin_unlock_irqrestore(&as->lock, flags);

	gpio_free(gpio);
}

/*-------------------------------------------------------------------------*/

static int __init atmel_spi_probe(struct platform_device *pdev)
{
	struct resource		*regs;
	int			irq;
	struct clk		*clk;
	int			ret;
	struct spi_master	*master;
	struct atmel_spi	*as;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	clk = clk_get(&pdev->dev, "spi_clk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* setup spi core then atmel-specific driver state */
	ret = -ENOMEM;
	master = spi_alloc_master(&pdev->dev, sizeof *as);
	if (!master)
		goto out_free;

	master->bus_num = pdev->id;
	master->num_chipselect = 4;
	master->setup = atmel_spi_setup;
	master->transfer = atmel_spi_transfer;
	master->cleanup = atmel_spi_cleanup;
	platform_set_drvdata(pdev, master);

	as = spi_master_get_devdata(master);

	/*
	 * Scratch buffer is used for throwaway rx and tx data.
	 * It's coherent to minimize dcache pollution.
	 */
	as->buffer = dma_alloc_coherent(&pdev->dev, BUFFER_SIZE,
					&as->buffer_dma, GFP_KERNEL);
	if (!as->buffer)
		goto out_free;

	spin_lock_init(&as->lock);
	INIT_LIST_HEAD(&as->queue);
	as->pdev = pdev;
	as->regs = ioremap(regs->start, (regs->end - regs->start) + 1);
	if (!as->regs)
		goto out_free_buffer;
	as->irq = irq;
	as->clk = clk;
	if (!cpu_is_at91rm9200())
		as->new_1 = 1;

	ret = request_irq(irq, atmel_spi_interrupt, 0,
			pdev->dev.bus_id, master);
	if (ret)
		goto out_unmap_regs;

	/* Initialize the hardware */
	clk_enable(clk);
	spi_writel(as, CR, SPI_BIT(SWRST));
	spi_writel(as, MR, SPI_BIT(MSTR) | SPI_BIT(MODFDIS));
	spi_writel(as, PTCR, SPI_BIT(RXTDIS) | SPI_BIT(TXTDIS));
	spi_writel(as, CR, SPI_BIT(SPIEN));

	/* go! */
	dev_info(&pdev->dev, "Atmel SPI Controller at 0x%08lx (irq %d)\n",
			(unsigned long)regs->start, irq);

	ret = spi_register_master(master);
	if (ret)
		goto out_reset_hw;

	return 0;

out_reset_hw:
	spi_writel(as, CR, SPI_BIT(SWRST));
	clk_disable(clk);
	free_irq(irq, master);
out_unmap_regs:
	iounmap(as->regs);
out_free_buffer:
	dma_free_coherent(&pdev->dev, BUFFER_SIZE, as->buffer,
			as->buffer_dma);
out_free:
	clk_put(clk);
	spi_master_put(master);
	return ret;
}

static int __exit atmel_spi_remove(struct platform_device *pdev)
{
	struct spi_master	*master = platform_get_drvdata(pdev);
	struct atmel_spi	*as = spi_master_get_devdata(master);
	struct spi_message	*msg;

	/* reset the hardware and block queue progress */
	spin_lock_irq(&as->lock);
	as->stopping = 1;
	spi_writel(as, CR, SPI_BIT(SWRST));
	spi_readl(as, SR);
	spin_unlock_irq(&as->lock);

	/* Terminate remaining queued transfers */
	list_for_each_entry(msg, &as->queue, queue) {
		/* REVISIT unmapping the dma is a NOP on ARM and AVR32
		 * but we shouldn't depend on that...
		 */
		msg->status = -ESHUTDOWN;
		msg->complete(msg->context);
	}

	dma_free_coherent(&pdev->dev, BUFFER_SIZE, as->buffer,
			as->buffer_dma);

	clk_disable(as->clk);
	clk_put(as->clk);
	free_irq(as->irq, master);
	iounmap(as->regs);

	spi_unregister_master(master);

	return 0;
}

#ifdef	CONFIG_PM

static int atmel_spi_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct spi_master	*master = platform_get_drvdata(pdev);
	struct atmel_spi	*as = spi_master_get_devdata(master);

	clk_disable(as->clk);
	return 0;
}

static int atmel_spi_resume(struct platform_device *pdev)
{
	struct spi_master	*master = platform_get_drvdata(pdev);
	struct atmel_spi	*as = spi_master_get_devdata(master);

	clk_enable(as->clk);
	return 0;
}

#else
#define	atmel_spi_suspend	NULL
#define	atmel_spi_resume	NULL
#endif


static struct platform_driver atmel_spi_driver = {
	.driver		= {
		.name	= "atmel_spi",
		.owner	= THIS_MODULE,
	},
	.suspend	= atmel_spi_suspend,
	.resume		= atmel_spi_resume,
	.remove		= __exit_p(atmel_spi_remove),
};

static int __init atmel_spi_init(void)
{
	return platform_driver_probe(&atmel_spi_driver, atmel_spi_probe);
}
module_init(atmel_spi_init);

static void __exit atmel_spi_exit(void)
{
	platform_driver_unregister(&atmel_spi_driver);
}
module_exit(atmel_spi_exit);

MODULE_DESCRIPTION("Atmel AT32/AT91 SPI Controller driver");
MODULE_AUTHOR("Haavard Skinnemoen <hskinnemoen@atmel.com>");
MODULE_LICENSE("GPL");
