/*
 * xilinx_spi.c
 *
 * Xilinx SPI controller driver (master mode only)
 *
 * Author: MontaVista Software, Inc.
 *	source@mvista.com
 *
 * 2002-2007 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_spi.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/io.h>

#define XILINX_SPI_NAME "xilinx_spi"

/* Register definitions as per "OPB Serial Peripheral Interface (SPI) (v1.00e)
 * Product Specification", DS464
 */
#define XSPI_CR_OFFSET		0x62	/* 16-bit Control Register */

#define XSPI_CR_ENABLE		0x02
#define XSPI_CR_MASTER_MODE	0x04
#define XSPI_CR_CPOL		0x08
#define XSPI_CR_CPHA		0x10
#define XSPI_CR_MODE_MASK	(XSPI_CR_CPHA | XSPI_CR_CPOL)
#define XSPI_CR_TXFIFO_RESET	0x20
#define XSPI_CR_RXFIFO_RESET	0x40
#define XSPI_CR_MANUAL_SSELECT	0x80
#define XSPI_CR_TRANS_INHIBIT	0x100

#define XSPI_SR_OFFSET		0x67	/* 8-bit Status Register */

#define XSPI_SR_RX_EMPTY_MASK	0x01	/* Receive FIFO is empty */
#define XSPI_SR_RX_FULL_MASK	0x02	/* Receive FIFO is full */
#define XSPI_SR_TX_EMPTY_MASK	0x04	/* Transmit FIFO is empty */
#define XSPI_SR_TX_FULL_MASK	0x08	/* Transmit FIFO is full */
#define XSPI_SR_MODE_FAULT_MASK	0x10	/* Mode fault error */

#define XSPI_TXD_OFFSET		0x6b	/* 8-bit Data Transmit Register */
#define XSPI_RXD_OFFSET		0x6f	/* 8-bit Data Receive Register */

#define XSPI_SSR_OFFSET		0x70	/* 32-bit Slave Select Register */

/* Register definitions as per "OPB IPIF (v3.01c) Product Specification", DS414
 * IPIF registers are 32 bit
 */
#define XIPIF_V123B_DGIER_OFFSET	0x1c	/* IPIF global int enable reg */
#define XIPIF_V123B_GINTR_ENABLE	0x80000000

#define XIPIF_V123B_IISR_OFFSET		0x20	/* IPIF interrupt status reg */
#define XIPIF_V123B_IIER_OFFSET		0x28	/* IPIF interrupt enable reg */

#define XSPI_INTR_MODE_FAULT		0x01	/* Mode fault error */
#define XSPI_INTR_SLAVE_MODE_FAULT	0x02	/* Selected as slave while
						 * disabled */
#define XSPI_INTR_TX_EMPTY		0x04	/* TxFIFO is empty */
#define XSPI_INTR_TX_UNDERRUN		0x08	/* TxFIFO was underrun */
#define XSPI_INTR_RX_FULL		0x10	/* RxFIFO is full */
#define XSPI_INTR_RX_OVERRUN		0x20	/* RxFIFO was overrun */

#define XIPIF_V123B_RESETR_OFFSET	0x40	/* IPIF reset register */
#define XIPIF_V123B_RESET_MASK		0x0a	/* the value to write */

struct xilinx_spi {
	/* bitbang has to be first */
	struct spi_bitbang bitbang;
	struct completion done;

	void __iomem	*regs;	/* virt. address of the control registers */

	u32		irq;

	u32		speed_hz; /* SCK has a fixed frequency of speed_hz Hz */

	u8 *rx_ptr;		/* pointer in the Tx buffer */
	const u8 *tx_ptr;	/* pointer in the Rx buffer */
	int remaining_bytes;	/* the number of bytes left to transfer */
};

static void xspi_init_hw(void __iomem *regs_base)
{
	/* Reset the SPI device */
	out_be32(regs_base + XIPIF_V123B_RESETR_OFFSET,
		 XIPIF_V123B_RESET_MASK);
	/* Disable all the interrupts just in case */
	out_be32(regs_base + XIPIF_V123B_IIER_OFFSET, 0);
	/* Enable the global IPIF interrupt */
	out_be32(regs_base + XIPIF_V123B_DGIER_OFFSET,
		 XIPIF_V123B_GINTR_ENABLE);
	/* Deselect the slave on the SPI bus */
	out_be32(regs_base + XSPI_SSR_OFFSET, 0xffff);
	/* Disable the transmitter, enable Manual Slave Select Assertion,
	 * put SPI controller into master mode, and enable it */
	out_be16(regs_base + XSPI_CR_OFFSET,
		 XSPI_CR_TRANS_INHIBIT | XSPI_CR_MANUAL_SSELECT
		 | XSPI_CR_MASTER_MODE | XSPI_CR_ENABLE);
}

static void xilinx_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);

	if (is_on == BITBANG_CS_INACTIVE) {
		/* Deselect the slave on the SPI bus */
		out_be32(xspi->regs + XSPI_SSR_OFFSET, 0xffff);
	} else if (is_on == BITBANG_CS_ACTIVE) {
		/* Set the SPI clock phase and polarity */
		u16 cr = in_be16(xspi->regs + XSPI_CR_OFFSET)
			 & ~XSPI_CR_MODE_MASK;
		if (spi->mode & SPI_CPHA)
			cr |= XSPI_CR_CPHA;
		if (spi->mode & SPI_CPOL)
			cr |= XSPI_CR_CPOL;
		out_be16(xspi->regs + XSPI_CR_OFFSET, cr);

		/* We do not check spi->max_speed_hz here as the SPI clock
		 * frequency is not software programmable (the IP block design
		 * parameter)
		 */

		/* Activate the chip select */
		out_be32(xspi->regs + XSPI_SSR_OFFSET,
			 ~(0x0001 << spi->chip_select));
	}
}

/* spi_bitbang requires custom setup_transfer() to be defined if there is a
 * custom txrx_bufs(). We have nothing to setup here as the SPI IP block
 * supports just 8 bits per word, and SPI clock can't be changed in software.
 * Check for 8 bits per word. Chip select delay calculations could be
 * added here as soon as bitbang_work() can be made aware of the delay value.
 */
static int xilinx_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	u8 bits_per_word;

	bits_per_word = (t) ? t->bits_per_word : spi->bits_per_word;
	if (bits_per_word != 8) {
		dev_err(&spi->dev, "%s, unsupported bits_per_word=%d\n",
			__func__, bits_per_word);
		return -EINVAL;
	}

	return 0;
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA)

static int xilinx_spi_setup(struct spi_device *spi)
{
	struct spi_bitbang *bitbang;
	struct xilinx_spi *xspi;
	int retval;

	xspi = spi_master_get_devdata(spi->master);
	bitbang = &xspi->bitbang;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	if (spi->mode & ~MODEBITS) {
		dev_err(&spi->dev, "%s, unsupported mode bits %x\n",
			__func__, spi->mode & ~MODEBITS);
		return -EINVAL;
	}

	retval = xilinx_spi_setup_transfer(spi, NULL);
	if (retval < 0)
		return retval;

	dev_dbg(&spi->dev, "%s, mode %d, %u bits/w, %u nsec/bit\n",
		__func__, spi->mode & MODEBITS, spi->bits_per_word, 0);

	return 0;
}

static void xilinx_spi_fill_tx_fifo(struct xilinx_spi *xspi)
{
	u8 sr;

	/* Fill the Tx FIFO with as many bytes as possible */
	sr = in_8(xspi->regs + XSPI_SR_OFFSET);
	while ((sr & XSPI_SR_TX_FULL_MASK) == 0 && xspi->remaining_bytes > 0) {
		if (xspi->tx_ptr) {
			out_8(xspi->regs + XSPI_TXD_OFFSET, *xspi->tx_ptr++);
		} else {
			out_8(xspi->regs + XSPI_TXD_OFFSET, 0);
		}
		xspi->remaining_bytes--;
		sr = in_8(xspi->regs + XSPI_SR_OFFSET);
	}
}

static int xilinx_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);
	u32 ipif_ier;
	u16 cr;

	/* We get here with transmitter inhibited */

	xspi->tx_ptr = t->tx_buf;
	xspi->rx_ptr = t->rx_buf;
	xspi->remaining_bytes = t->len;
	INIT_COMPLETION(xspi->done);

	xilinx_spi_fill_tx_fifo(xspi);

	/* Enable the transmit empty interrupt, which we use to determine
	 * progress on the transmission.
	 */
	ipif_ier = in_be32(xspi->regs + XIPIF_V123B_IIER_OFFSET);
	out_be32(xspi->regs + XIPIF_V123B_IIER_OFFSET,
		 ipif_ier | XSPI_INTR_TX_EMPTY);

	/* Start the transfer by not inhibiting the transmitter any longer */
	cr = in_be16(xspi->regs + XSPI_CR_OFFSET) & ~XSPI_CR_TRANS_INHIBIT;
	out_be16(xspi->regs + XSPI_CR_OFFSET, cr);

	wait_for_completion(&xspi->done);

	/* Disable the transmit empty interrupt */
	out_be32(xspi->regs + XIPIF_V123B_IIER_OFFSET, ipif_ier);

	return t->len - xspi->remaining_bytes;
}


/* This driver supports single master mode only. Hence Tx FIFO Empty
 * is the only interrupt we care about.
 * Receive FIFO Overrun, Transmit FIFO Underrun, Mode Fault, and Slave Mode
 * Fault are not to happen.
 */
static irqreturn_t xilinx_spi_irq(int irq, void *dev_id)
{
	struct xilinx_spi *xspi = dev_id;
	u32 ipif_isr;

	/* Get the IPIF interrupts, and clear them immediately */
	ipif_isr = in_be32(xspi->regs + XIPIF_V123B_IISR_OFFSET);
	out_be32(xspi->regs + XIPIF_V123B_IISR_OFFSET, ipif_isr);

	if (ipif_isr & XSPI_INTR_TX_EMPTY) {	/* Transmission completed */
		u16 cr;
		u8 sr;

		/* A transmit has just completed. Process received data and
		 * check for more data to transmit. Always inhibit the
		 * transmitter while the Isr refills the transmit register/FIFO,
		 * or make sure it is stopped if we're done.
		 */
		cr = in_be16(xspi->regs + XSPI_CR_OFFSET);
		out_be16(xspi->regs + XSPI_CR_OFFSET,
			 cr | XSPI_CR_TRANS_INHIBIT);

		/* Read out all the data from the Rx FIFO */
		sr = in_8(xspi->regs + XSPI_SR_OFFSET);
		while ((sr & XSPI_SR_RX_EMPTY_MASK) == 0) {
			u8 data;

			data = in_8(xspi->regs + XSPI_RXD_OFFSET);
			if (xspi->rx_ptr) {
				*xspi->rx_ptr++ = data;
			}
			sr = in_8(xspi->regs + XSPI_SR_OFFSET);
		}

		/* See if there is more data to send */
		if (xspi->remaining_bytes > 0) {
			xilinx_spi_fill_tx_fifo(xspi);
			/* Start the transfer by not inhibiting the
			 * transmitter any longer
			 */
			out_be16(xspi->regs + XSPI_CR_OFFSET, cr);
		} else {
			/* No more data to send.
			 * Indicate the transfer is completed.
			 */
			complete(&xspi->done);
		}
	}

	return IRQ_HANDLED;
}

static int __init xilinx_spi_of_probe(struct of_device *ofdev,
					const struct of_device_id *match)
{
	struct spi_master *master;
	struct xilinx_spi *xspi;
	struct resource r_irq_struct;
	struct resource r_mem_struct;

	struct resource *r_irq = &r_irq_struct;
	struct resource *r_mem = &r_mem_struct;
	int rc = 0;
	const u32 *prop;
	int len;

	/* Get resources(memory, IRQ) associated with the device */
	master = spi_alloc_master(&ofdev->dev, sizeof(struct xilinx_spi));

	if (master == NULL) {
		return -ENOMEM;
	}

	dev_set_drvdata(&ofdev->dev, master);

	rc = of_address_to_resource(ofdev->node, 0, r_mem);
	if (rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		goto put_master;
	}

	rc = of_irq_to_resource(ofdev->node, 0, r_irq);
	if (rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found\n");
		goto put_master;
	}

	xspi = spi_master_get_devdata(master);
	xspi->bitbang.master = spi_master_get(master);
	xspi->bitbang.chipselect = xilinx_spi_chipselect;
	xspi->bitbang.setup_transfer = xilinx_spi_setup_transfer;
	xspi->bitbang.txrx_bufs = xilinx_spi_txrx_bufs;
	xspi->bitbang.master->setup = xilinx_spi_setup;
	init_completion(&xspi->done);

	xspi->irq = r_irq->start;

	if (!request_mem_region(r_mem->start,
			r_mem->end - r_mem->start + 1, XILINX_SPI_NAME)) {
		rc = -ENXIO;
		dev_warn(&ofdev->dev, "memory request failure\n");
		goto put_master;
	}

	xspi->regs = ioremap(r_mem->start, r_mem->end - r_mem->start + 1);
	if (xspi->regs == NULL) {
		rc = -ENOMEM;
		dev_warn(&ofdev->dev, "ioremap failure\n");
		goto put_master;
	}
	xspi->irq = r_irq->start;

	/* dynamic bus assignment */
	master->bus_num = -1;

	/* number of slave select bits is required */
	prop = of_get_property(ofdev->node, "xlnx,num-ss-bits", &len);
	if (!prop || len < sizeof(*prop)) {
		dev_warn(&ofdev->dev, "no 'xlnx,num-ss-bits' property\n");
		goto put_master;
	}
	master->num_chipselect = *prop;

	/* SPI controller initializations */
	xspi_init_hw(xspi->regs);

	/* Register for SPI Interrupt */
	rc = request_irq(xspi->irq, xilinx_spi_irq, 0, XILINX_SPI_NAME, xspi);
	if (rc != 0) {
		dev_warn(&ofdev->dev, "irq request failure: %d\n", xspi->irq);
		goto unmap_io;
	}

	rc = spi_bitbang_start(&xspi->bitbang);
	if (rc != 0) {
		dev_err(&ofdev->dev, "spi_bitbang_start FAILED\n");
		goto free_irq;
	}

	dev_info(&ofdev->dev, "at 0x%08X mapped to 0x%08X, irq=%d\n",
			(unsigned int)r_mem->start, (u32)xspi->regs, xspi->irq);

	/* Add any subnodes on the SPI bus */
	of_register_spi_devices(master, ofdev->node);

	return rc;

free_irq:
	free_irq(xspi->irq, xspi);
unmap_io:
	iounmap(xspi->regs);
put_master:
	spi_master_put(master);
	return rc;
}

static int __devexit xilinx_spi_remove(struct of_device *ofdev)
{
	struct xilinx_spi *xspi;
	struct spi_master *master;

	master = platform_get_drvdata(ofdev);
	xspi = spi_master_get_devdata(master);

	spi_bitbang_stop(&xspi->bitbang);
	free_irq(xspi->irq, xspi);
	iounmap(xspi->regs);
	dev_set_drvdata(&ofdev->dev, 0);
	spi_master_put(xspi->bitbang.master);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" XILINX_SPI_NAME);

static int __exit xilinx_spi_of_remove(struct of_device *op)
{
	return xilinx_spi_remove(op);
}

static struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};

MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static struct of_platform_driver xilinx_spi_of_driver = {
	.owner = THIS_MODULE,
	.name = "xilinx-xps-spi",
	.match_table = xilinx_spi_of_match,
	.probe = xilinx_spi_of_probe,
	.remove = __exit_p(xilinx_spi_of_remove),
	.driver = {
		.name = "xilinx-xps-spi",
		.owner = THIS_MODULE,
	},
};

static int __init xilinx_spi_init(void)
{
	return of_register_platform_driver(&xilinx_spi_of_driver);
}
module_init(xilinx_spi_init);

static void __exit xilinx_spi_exit(void)
{
	of_unregister_platform_driver(&xilinx_spi_of_driver);
}
module_exit(xilinx_spi_exit);
MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx SPI driver");
MODULE_LICENSE("GPL");
