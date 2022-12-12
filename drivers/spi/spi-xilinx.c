// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx SPI controller driver (master mode only)
 *
 * Author: MontaVista Software, Inc.
 *	source@mvista.com
 *
 * Copyright (c) 2010 Secret Lab Technologies, Ltd.
 * Copyright (c) 2009 Intel Corporation
 * 2002-2007 (c) MontaVista Software, Inc.

 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/io.h>

#define XILINX_SPI_MAX_CS	32

#define XILINX_SPI_NAME "xilinx_spi"

/* Register definitions as per "OPB Serial Peripheral Interface (SPI) (v1.00e)
 * Product Specification", DS464
 */
#define XSPI_CR_OFFSET		0x60	/* Control Register */

#define XSPI_CR_LOOP		0x01
#define XSPI_CR_ENABLE		0x02
#define XSPI_CR_MASTER_MODE	0x04
#define XSPI_CR_CPOL		0x08
#define XSPI_CR_CPHA		0x10
#define XSPI_CR_MODE_MASK	(XSPI_CR_CPHA | XSPI_CR_CPOL | \
				 XSPI_CR_LSB_FIRST | XSPI_CR_LOOP)
#define XSPI_CR_TXFIFO_RESET	0x20
#define XSPI_CR_RXFIFO_RESET	0x40
#define XSPI_CR_MANUAL_SSELECT	0x80
#define XSPI_CR_TRANS_INHIBIT	0x100
#define XSPI_CR_LSB_FIRST	0x200

#define XSPI_SR_OFFSET		0x64	/* Status Register */

#define XSPI_SR_RX_EMPTY_MASK	0x01	/* Receive FIFO is empty */
#define XSPI_SR_RX_FULL_MASK	0x02	/* Receive FIFO is full */
#define XSPI_SR_TX_EMPTY_MASK	0x04	/* Transmit FIFO is empty */
#define XSPI_SR_TX_FULL_MASK	0x08	/* Transmit FIFO is full */
#define XSPI_SR_MODE_FAULT_MASK	0x10	/* Mode fault error */

#define XSPI_TXD_OFFSET		0x68	/* Data Transmit Register */
#define XSPI_RXD_OFFSET		0x6c	/* Data Receive Register */

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
#define XSPI_INTR_TX_HALF_EMPTY		0x40	/* TxFIFO is half empty */

#define XIPIF_V123B_RESETR_OFFSET	0x40	/* IPIF reset register */
#define XIPIF_V123B_RESET_MASK		0x0a	/* the value to write */

struct xilinx_spi {
	/* bitbang has to be first */
	struct spi_bitbang bitbang;
	struct completion done;
	void __iomem	*regs;	/* virt. address of the control registers */

	int		irq;

	u8 *rx_ptr;		/* pointer in the Tx buffer */
	const u8 *tx_ptr;	/* pointer in the Rx buffer */
	u8 bytes_per_word;
	int buffer_size;	/* buffer size in words */
	u32 cs_inactive;	/* Level of the CS pins when inactive*/
	unsigned int (*read_fn)(void __iomem *);
	void (*write_fn)(u32, void __iomem *);
};

static void xspi_write32(u32 val, void __iomem *addr)
{
	iowrite32(val, addr);
}

static unsigned int xspi_read32(void __iomem *addr)
{
	return ioread32(addr);
}

static void xspi_write32_be(u32 val, void __iomem *addr)
{
	iowrite32be(val, addr);
}

static unsigned int xspi_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

static void xilinx_spi_tx(struct xilinx_spi *xspi)
{
	u32 data = 0;

	if (!xspi->tx_ptr) {
		xspi->write_fn(0, xspi->regs + XSPI_TXD_OFFSET);
		return;
	}

	switch (xspi->bytes_per_word) {
	case 1:
		data = *(u8 *)(xspi->tx_ptr);
		break;
	case 2:
		data = *(u16 *)(xspi->tx_ptr);
		break;
	case 4:
		data = *(u32 *)(xspi->tx_ptr);
		break;
	}

	xspi->write_fn(data, xspi->regs + XSPI_TXD_OFFSET);
	xspi->tx_ptr += xspi->bytes_per_word;
}

static void xilinx_spi_rx(struct xilinx_spi *xspi)
{
	u32 data = xspi->read_fn(xspi->regs + XSPI_RXD_OFFSET);

	if (!xspi->rx_ptr)
		return;

	switch (xspi->bytes_per_word) {
	case 1:
		*(u8 *)(xspi->rx_ptr) = data;
		break;
	case 2:
		*(u16 *)(xspi->rx_ptr) = data;
		break;
	case 4:
		*(u32 *)(xspi->rx_ptr) = data;
		break;
	}

	xspi->rx_ptr += xspi->bytes_per_word;
}

static void xspi_init_hw(struct xilinx_spi *xspi)
{
	void __iomem *regs_base = xspi->regs;

	/* Reset the SPI device */
	xspi->write_fn(XIPIF_V123B_RESET_MASK,
		regs_base + XIPIF_V123B_RESETR_OFFSET);
	/* Enable the transmit empty interrupt, which we use to determine
	 * progress on the transmission.
	 */
	xspi->write_fn(XSPI_INTR_TX_EMPTY,
			regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Disable the global IPIF interrupt */
	xspi->write_fn(0, regs_base + XIPIF_V123B_DGIER_OFFSET);
	/* Deselect the slave on the SPI bus */
	xspi->write_fn(0xffff, regs_base + XSPI_SSR_OFFSET);
	/* Disable the transmitter, enable Manual Slave Select Assertion,
	 * put SPI controller into master mode, and enable it */
	xspi->write_fn(XSPI_CR_MANUAL_SSELECT |	XSPI_CR_MASTER_MODE |
		XSPI_CR_ENABLE | XSPI_CR_TXFIFO_RESET |	XSPI_CR_RXFIFO_RESET,
		regs_base + XSPI_CR_OFFSET);
}

static void xilinx_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);
	u16 cr;
	u32 cs;

	if (is_on == BITBANG_CS_INACTIVE) {
		/* Deselect the slave on the SPI bus */
		xspi->write_fn(xspi->cs_inactive, xspi->regs + XSPI_SSR_OFFSET);
		return;
	}

	/* Set the SPI clock phase and polarity */
	cr = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET)	& ~XSPI_CR_MODE_MASK;
	if (spi->mode & SPI_CPHA)
		cr |= XSPI_CR_CPHA;
	if (spi->mode & SPI_CPOL)
		cr |= XSPI_CR_CPOL;
	if (spi->mode & SPI_LSB_FIRST)
		cr |= XSPI_CR_LSB_FIRST;
	if (spi->mode & SPI_LOOP)
		cr |= XSPI_CR_LOOP;
	xspi->write_fn(cr, xspi->regs + XSPI_CR_OFFSET);

	/* We do not check spi->max_speed_hz here as the SPI clock
	 * frequency is not software programmable (the IP block design
	 * parameter)
	 */

	cs = xspi->cs_inactive;
	cs ^= BIT(spi->chip_select);

	/* Activate the chip select */
	xspi->write_fn(cs, xspi->regs + XSPI_SSR_OFFSET);
}

/* spi_bitbang requires custom setup_transfer() to be defined if there is a
 * custom txrx_bufs().
 */
static int xilinx_spi_setup_transfer(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);

	if (spi->mode & SPI_CS_HIGH)
		xspi->cs_inactive &= ~BIT(spi->chip_select);
	else
		xspi->cs_inactive |= BIT(spi->chip_select);

	return 0;
}

static int xilinx_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct xilinx_spi *xspi = spi_master_get_devdata(spi->master);
	int remaining_words;	/* the number of words left to transfer */
	bool use_irq = false;
	u16 cr = 0;

	/* We get here with transmitter inhibited */

	xspi->tx_ptr = t->tx_buf;
	xspi->rx_ptr = t->rx_buf;
	remaining_words = t->len / xspi->bytes_per_word;

	if (xspi->irq >= 0 &&  remaining_words > xspi->buffer_size) {
		u32 isr;
		use_irq = true;
		/* Inhibit irq to avoid spurious irqs on tx_empty*/
		cr = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET);
		xspi->write_fn(cr | XSPI_CR_TRANS_INHIBIT,
			       xspi->regs + XSPI_CR_OFFSET);
		/* ACK old irqs (if any) */
		isr = xspi->read_fn(xspi->regs + XIPIF_V123B_IISR_OFFSET);
		if (isr)
			xspi->write_fn(isr,
				       xspi->regs + XIPIF_V123B_IISR_OFFSET);
		/* Enable the global IPIF interrupt */
		xspi->write_fn(XIPIF_V123B_GINTR_ENABLE,
				xspi->regs + XIPIF_V123B_DGIER_OFFSET);
		reinit_completion(&xspi->done);
	}

	while (remaining_words) {
		int n_words, tx_words, rx_words;
		u32 sr;
		int stalled;

		n_words = min(remaining_words, xspi->buffer_size);

		tx_words = n_words;
		while (tx_words--)
			xilinx_spi_tx(xspi);

		/* Start the transfer by not inhibiting the transmitter any
		 * longer
		 */

		if (use_irq) {
			xspi->write_fn(cr, xspi->regs + XSPI_CR_OFFSET);
			wait_for_completion(&xspi->done);
			/* A transmit has just completed. Process received data
			 * and check for more data to transmit. Always inhibit
			 * the transmitter while the Isr refills the transmit
			 * register/FIFO, or make sure it is stopped if we're
			 * done.
			 */
			xspi->write_fn(cr | XSPI_CR_TRANS_INHIBIT,
				       xspi->regs + XSPI_CR_OFFSET);
			sr = XSPI_SR_TX_EMPTY_MASK;
		} else
			sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);

		/* Read out all the data from the Rx FIFO */
		rx_words = n_words;
		stalled = 10;
		while (rx_words) {
			if (rx_words == n_words && !(stalled--) &&
			    !(sr & XSPI_SR_TX_EMPTY_MASK) &&
			    (sr & XSPI_SR_RX_EMPTY_MASK)) {
				dev_err(&spi->dev,
					"Detected stall. Check C_SPI_MODE and C_SPI_MEMORY\n");
				xspi_init_hw(xspi);
				return -EIO;
			}

			if ((sr & XSPI_SR_TX_EMPTY_MASK) && (rx_words > 1)) {
				xilinx_spi_rx(xspi);
				rx_words--;
				continue;
			}

			sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
			if (!(sr & XSPI_SR_RX_EMPTY_MASK)) {
				xilinx_spi_rx(xspi);
				rx_words--;
			}
		}

		remaining_words -= n_words;
	}

	if (use_irq) {
		xspi->write_fn(0, xspi->regs + XIPIF_V123B_DGIER_OFFSET);
		xspi->write_fn(cr, xspi->regs + XSPI_CR_OFFSET);
	}

	return t->len;
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
	ipif_isr = xspi->read_fn(xspi->regs + XIPIF_V123B_IISR_OFFSET);
	xspi->write_fn(ipif_isr, xspi->regs + XIPIF_V123B_IISR_OFFSET);

	if (ipif_isr & XSPI_INTR_TX_EMPTY) {	/* Transmission completed */
		complete(&xspi->done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int xilinx_spi_find_buffer_size(struct xilinx_spi *xspi)
{
	u8 sr;
	int n_words = 0;

	/*
	 * Before the buffer_size detection we reset the core
	 * to make sure we start with a clean state.
	 */
	xspi->write_fn(XIPIF_V123B_RESET_MASK,
		xspi->regs + XIPIF_V123B_RESETR_OFFSET);

	/* Fill the Tx FIFO with as many words as possible */
	do {
		xspi->write_fn(0, xspi->regs + XSPI_TXD_OFFSET);
		sr = xspi->read_fn(xspi->regs + XSPI_SR_OFFSET);
		n_words++;
	} while (!(sr & XSPI_SR_TX_FULL_MASK));

	return n_words;
}

static const struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,axi-quad-spi-1.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static int xilinx_spi_probe(struct platform_device *pdev)
{
	struct xilinx_spi *xspi;
	struct xspi_platform_data *pdata;
	struct resource *res;
	int ret, num_cs = 0, bits_per_word;
	struct spi_master *master;
	u32 tmp;
	u8 i;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		num_cs = pdata->num_chipselect;
		bits_per_word = pdata->bits_per_word;
	} else {
		of_property_read_u32(pdev->dev.of_node, "xlnx,num-ss-bits",
					  &num_cs);
		ret = of_property_read_u32(pdev->dev.of_node,
					   "xlnx,num-transfer-bits",
					   &bits_per_word);
		if (ret)
			bits_per_word = 8;
	}

	if (!num_cs) {
		dev_err(&pdev->dev,
			"Missing slave select configuration data\n");
		return -EINVAL;
	}

	if (num_cs > XILINX_SPI_MAX_CS) {
		dev_err(&pdev->dev, "Invalid number of spi slaves\n");
		return -EINVAL;
	}

	master = devm_spi_alloc_master(&pdev->dev, sizeof(struct xilinx_spi));
	if (!master)
		return -ENODEV;

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST | SPI_LOOP |
			    SPI_CS_HIGH;

	xspi = spi_master_get_devdata(master);
	xspi->cs_inactive = 0xffffffff;
	xspi->bitbang.master = master;
	xspi->bitbang.chipselect = xilinx_spi_chipselect;
	xspi->bitbang.setup_transfer = xilinx_spi_setup_transfer;
	xspi->bitbang.txrx_bufs = xilinx_spi_txrx_bufs;
	init_completion(&xspi->done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xspi->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xspi->regs))
		return PTR_ERR(xspi->regs);

	master->bus_num = pdev->id;
	master->num_chipselect = num_cs;
	master->dev.of_node = pdev->dev.of_node;

	/*
	 * Detect endianess on the IP via loop bit in CR. Detection
	 * must be done before reset is sent because incorrect reset
	 * value generates error interrupt.
	 * Setup little endian helper functions first and try to use them
	 * and check if bit was correctly setup or not.
	 */
	xspi->read_fn = xspi_read32;
	xspi->write_fn = xspi_write32;

	xspi->write_fn(XSPI_CR_LOOP, xspi->regs + XSPI_CR_OFFSET);
	tmp = xspi->read_fn(xspi->regs + XSPI_CR_OFFSET);
	tmp &= XSPI_CR_LOOP;
	if (tmp != XSPI_CR_LOOP) {
		xspi->read_fn = xspi_read32_be;
		xspi->write_fn = xspi_write32_be;
	}

	master->bits_per_word_mask = SPI_BPW_MASK(bits_per_word);
	xspi->bytes_per_word = bits_per_word / 8;
	xspi->buffer_size = xilinx_spi_find_buffer_size(xspi);

	xspi->irq = platform_get_irq(pdev, 0);
	if (xspi->irq < 0 && xspi->irq != -ENXIO) {
		return xspi->irq;
	} else if (xspi->irq >= 0) {
		/* Register for SPI Interrupt */
		ret = devm_request_irq(&pdev->dev, xspi->irq, xilinx_spi_irq, 0,
				dev_name(&pdev->dev), xspi);
		if (ret)
			return ret;
	}

	/* SPI controller initializations */
	xspi_init_hw(xspi);

	ret = spi_bitbang_start(&xspi->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "spi_bitbang_start FAILED\n");
		return ret;
	}

	dev_info(&pdev->dev, "at %pR, irq=%d\n", res, xspi->irq);

	if (pdata) {
		for (i = 0; i < pdata->num_devices; i++)
			spi_new_device(master, pdata->devices + i);
	}

	platform_set_drvdata(pdev, master);
	return 0;
}

static int xilinx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct xilinx_spi *xspi = spi_master_get_devdata(master);
	void __iomem *regs_base = xspi->regs;

	spi_bitbang_stop(&xspi->bitbang);

	/* Disable all the interrupts just in case */
	xspi->write_fn(0, regs_base + XIPIF_V123B_IIER_OFFSET);
	/* Disable the global IPIF interrupt */
	xspi->write_fn(0, regs_base + XIPIF_V123B_DGIER_OFFSET);

	spi_master_put(xspi->bitbang.master);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" XILINX_SPI_NAME);

static struct platform_driver xilinx_spi_driver = {
	.probe = xilinx_spi_probe,
	.remove = xilinx_spi_remove,
	.driver = {
		.name = XILINX_SPI_NAME,
		.of_match_table = xilinx_spi_of_match,
	},
};
module_platform_driver(xilinx_spi_driver);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx SPI driver");
MODULE_LICENSE("GPL");
