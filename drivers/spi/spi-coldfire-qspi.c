// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale/Motorola Coldfire Queued SPI driver
 *
 * Copyright 2010 Steven King <sfking@fdwdc.com>
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfqspi.h>

#define	DRIVER_NAME "mcfqspi"

#define	MCFQSPI_BUSCLK			(MCF_BUSCLK / 2)

#define	MCFQSPI_QMR			0x00
#define		MCFQSPI_QMR_MSTR	0x8000
#define		MCFQSPI_QMR_CPOL	0x0200
#define		MCFQSPI_QMR_CPHA	0x0100
#define	MCFQSPI_QDLYR			0x04
#define		MCFQSPI_QDLYR_SPE	0x8000
#define	MCFQSPI_QWR			0x08
#define		MCFQSPI_QWR_HALT	0x8000
#define		MCFQSPI_QWR_WREN	0x4000
#define		MCFQSPI_QWR_CSIV	0x1000
#define	MCFQSPI_QIR			0x0C
#define		MCFQSPI_QIR_WCEFB	0x8000
#define		MCFQSPI_QIR_ABRTB	0x4000
#define		MCFQSPI_QIR_ABRTL	0x1000
#define		MCFQSPI_QIR_WCEFE	0x0800
#define		MCFQSPI_QIR_ABRTE	0x0400
#define		MCFQSPI_QIR_SPIFE	0x0100
#define		MCFQSPI_QIR_WCEF	0x0008
#define		MCFQSPI_QIR_ABRT	0x0004
#define		MCFQSPI_QIR_SPIF	0x0001
#define	MCFQSPI_QAR			0x010
#define		MCFQSPI_QAR_TXBUF	0x00
#define		MCFQSPI_QAR_RXBUF	0x10
#define		MCFQSPI_QAR_CMDBUF	0x20
#define	MCFQSPI_QDR			0x014
#define	MCFQSPI_QCR			0x014
#define		MCFQSPI_QCR_CONT	0x8000
#define		MCFQSPI_QCR_BITSE	0x4000
#define		MCFQSPI_QCR_DT		0x2000

struct mcfqspi {
	void __iomem *iobase;
	int irq;
	struct clk *clk;
	struct mcfqspi_cs_control *cs_control;

	wait_queue_head_t waitq;
};

static void mcfqspi_wr_qmr(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QMR);
}

static void mcfqspi_wr_qdlyr(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QDLYR);
}

static u16 mcfqspi_rd_qdlyr(struct mcfqspi *mcfqspi)
{
	return readw(mcfqspi->iobase + MCFQSPI_QDLYR);
}

static void mcfqspi_wr_qwr(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QWR);
}

static void mcfqspi_wr_qir(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QIR);
}

static void mcfqspi_wr_qar(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QAR);
}

static void mcfqspi_wr_qdr(struct mcfqspi *mcfqspi, u16 val)
{
	writew(val, mcfqspi->iobase + MCFQSPI_QDR);
}

static u16 mcfqspi_rd_qdr(struct mcfqspi *mcfqspi)
{
	return readw(mcfqspi->iobase + MCFQSPI_QDR);
}

static void mcfqspi_cs_select(struct mcfqspi *mcfqspi, u8 chip_select,
			    bool cs_high)
{
	mcfqspi->cs_control->select(mcfqspi->cs_control, chip_select, cs_high);
}

static void mcfqspi_cs_deselect(struct mcfqspi *mcfqspi, u8 chip_select,
				bool cs_high)
{
	mcfqspi->cs_control->deselect(mcfqspi->cs_control, chip_select, cs_high);
}

static int mcfqspi_cs_setup(struct mcfqspi *mcfqspi)
{
	return (mcfqspi->cs_control->setup) ?
		mcfqspi->cs_control->setup(mcfqspi->cs_control) : 0;
}

static void mcfqspi_cs_teardown(struct mcfqspi *mcfqspi)
{
	if (mcfqspi->cs_control->teardown)
		mcfqspi->cs_control->teardown(mcfqspi->cs_control);
}

static u8 mcfqspi_qmr_baud(u32 speed_hz)
{
	return clamp((MCFQSPI_BUSCLK + speed_hz - 1) / speed_hz, 2u, 255u);
}

static bool mcfqspi_qdlyr_spe(struct mcfqspi *mcfqspi)
{
	return mcfqspi_rd_qdlyr(mcfqspi) & MCFQSPI_QDLYR_SPE;
}

static irqreturn_t mcfqspi_irq_handler(int this_irq, void *dev_id)
{
	struct mcfqspi *mcfqspi = dev_id;

	/* clear interrupt */
	mcfqspi_wr_qir(mcfqspi, MCFQSPI_QIR_SPIFE | MCFQSPI_QIR_SPIF);
	wake_up(&mcfqspi->waitq);

	return IRQ_HANDLED;
}

static void mcfqspi_transfer_msg8(struct mcfqspi *mcfqspi, unsigned count,
				  const u8 *txbuf, u8 *rxbuf)
{
	unsigned i, n, offset = 0;

	n = min(count, 16u);

	mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_CMDBUF);
	for (i = 0; i < n; ++i)
		mcfqspi_wr_qdr(mcfqspi, MCFQSPI_QCR_BITSE);

	mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_TXBUF);
	if (txbuf)
		for (i = 0; i < n; ++i)
			mcfqspi_wr_qdr(mcfqspi, *txbuf++);
	else
		for (i = 0; i < count; ++i)
			mcfqspi_wr_qdr(mcfqspi, 0);

	count -= n;
	if (count) {
		u16 qwr = 0xf08;
		mcfqspi_wr_qwr(mcfqspi, 0x700);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);

		do {
			wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
			mcfqspi_wr_qwr(mcfqspi, qwr);
			mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
			if (rxbuf) {
				mcfqspi_wr_qar(mcfqspi,
					       MCFQSPI_QAR_RXBUF + offset);
				for (i = 0; i < 8; ++i)
					*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
			}
			n = min(count, 8u);
			if (txbuf) {
				mcfqspi_wr_qar(mcfqspi,
					       MCFQSPI_QAR_TXBUF + offset);
				for (i = 0; i < n; ++i)
					mcfqspi_wr_qdr(mcfqspi, *txbuf++);
			}
			qwr = (offset ? 0x808 : 0) + ((n - 1) << 8);
			offset ^= 8;
			count -= n;
		} while (count);
		wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
		mcfqspi_wr_qwr(mcfqspi, qwr);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
		if (rxbuf) {
			mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_RXBUF + offset);
			for (i = 0; i < 8; ++i)
				*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
			offset ^= 8;
		}
	} else {
		mcfqspi_wr_qwr(mcfqspi, (n - 1) << 8);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
	}
	wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
	if (rxbuf) {
		mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_RXBUF + offset);
		for (i = 0; i < n; ++i)
			*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
	}
}

static void mcfqspi_transfer_msg16(struct mcfqspi *mcfqspi, unsigned count,
				   const u16 *txbuf, u16 *rxbuf)
{
	unsigned i, n, offset = 0;

	n = min(count, 16u);

	mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_CMDBUF);
	for (i = 0; i < n; ++i)
		mcfqspi_wr_qdr(mcfqspi, MCFQSPI_QCR_BITSE);

	mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_TXBUF);
	if (txbuf)
		for (i = 0; i < n; ++i)
			mcfqspi_wr_qdr(mcfqspi, *txbuf++);
	else
		for (i = 0; i < count; ++i)
			mcfqspi_wr_qdr(mcfqspi, 0);

	count -= n;
	if (count) {
		u16 qwr = 0xf08;
		mcfqspi_wr_qwr(mcfqspi, 0x700);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);

		do {
			wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
			mcfqspi_wr_qwr(mcfqspi, qwr);
			mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
			if (rxbuf) {
				mcfqspi_wr_qar(mcfqspi,
					       MCFQSPI_QAR_RXBUF + offset);
				for (i = 0; i < 8; ++i)
					*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
			}
			n = min(count, 8u);
			if (txbuf) {
				mcfqspi_wr_qar(mcfqspi,
					       MCFQSPI_QAR_TXBUF + offset);
				for (i = 0; i < n; ++i)
					mcfqspi_wr_qdr(mcfqspi, *txbuf++);
			}
			qwr = (offset ? 0x808 : 0x000) + ((n - 1) << 8);
			offset ^= 8;
			count -= n;
		} while (count);
		wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
		mcfqspi_wr_qwr(mcfqspi, qwr);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
		if (rxbuf) {
			mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_RXBUF + offset);
			for (i = 0; i < 8; ++i)
				*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
			offset ^= 8;
		}
	} else {
		mcfqspi_wr_qwr(mcfqspi, (n - 1) << 8);
		mcfqspi_wr_qdlyr(mcfqspi, MCFQSPI_QDLYR_SPE);
	}
	wait_event(mcfqspi->waitq, !mcfqspi_qdlyr_spe(mcfqspi));
	if (rxbuf) {
		mcfqspi_wr_qar(mcfqspi, MCFQSPI_QAR_RXBUF + offset);
		for (i = 0; i < n; ++i)
			*rxbuf++ = mcfqspi_rd_qdr(mcfqspi);
	}
}

static void mcfqspi_set_cs(struct spi_device *spi, bool enable)
{
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(spi->controller);
	bool cs_high = spi->mode & SPI_CS_HIGH;

	if (enable)
		mcfqspi_cs_select(mcfqspi, spi_get_chipselect(spi, 0), cs_high);
	else
		mcfqspi_cs_deselect(mcfqspi, spi_get_chipselect(spi, 0), cs_high);
}

static int mcfqspi_transfer_one(struct spi_controller *host,
				struct spi_device *spi,
				struct spi_transfer *t)
{
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);
	u16 qmr = MCFQSPI_QMR_MSTR;

	qmr |= t->bits_per_word << 10;
	if (spi->mode & SPI_CPHA)
		qmr |= MCFQSPI_QMR_CPHA;
	if (spi->mode & SPI_CPOL)
		qmr |= MCFQSPI_QMR_CPOL;
	qmr |= mcfqspi_qmr_baud(t->speed_hz);
	mcfqspi_wr_qmr(mcfqspi, qmr);

	mcfqspi_wr_qir(mcfqspi, MCFQSPI_QIR_SPIFE);
	if (t->bits_per_word == 8)
		mcfqspi_transfer_msg8(mcfqspi, t->len, t->tx_buf, t->rx_buf);
	else
		mcfqspi_transfer_msg16(mcfqspi, t->len / 2, t->tx_buf,
				       t->rx_buf);
	mcfqspi_wr_qir(mcfqspi, 0);

	return 0;
}

static int mcfqspi_setup(struct spi_device *spi)
{
	mcfqspi_cs_deselect(spi_controller_get_devdata(spi->controller),
			    spi_get_chipselect(spi, 0), spi->mode & SPI_CS_HIGH);

	dev_dbg(&spi->dev,
			"bits per word %d, chip select %d, speed %d KHz\n",
			spi->bits_per_word, spi_get_chipselect(spi, 0),
			(MCFQSPI_BUSCLK / mcfqspi_qmr_baud(spi->max_speed_hz))
			/ 1000);

	return 0;
}

static int mcfqspi_probe(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct mcfqspi *mcfqspi;
	struct mcfqspi_platform_data *pdata;
	int status;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_dbg(&pdev->dev, "platform data is missing\n");
		return -ENOENT;
	}

	if (!pdata->cs_control) {
		dev_dbg(&pdev->dev, "pdata->cs_control is NULL\n");
		return -EINVAL;
	}

	host = spi_alloc_host(&pdev->dev, sizeof(*mcfqspi));
	if (host == NULL) {
		dev_dbg(&pdev->dev, "spi_alloc_host failed\n");
		return -ENOMEM;
	}

	mcfqspi = spi_controller_get_devdata(host);

	mcfqspi->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mcfqspi->iobase)) {
		status = PTR_ERR(mcfqspi->iobase);
		goto fail0;
	}

	mcfqspi->irq = platform_get_irq(pdev, 0);
	if (mcfqspi->irq < 0) {
		dev_dbg(&pdev->dev, "platform_get_irq failed\n");
		status = -ENXIO;
		goto fail0;
	}

	status = devm_request_irq(&pdev->dev, mcfqspi->irq, mcfqspi_irq_handler,
				0, pdev->name, mcfqspi);
	if (status) {
		dev_dbg(&pdev->dev, "request_irq failed\n");
		goto fail0;
	}

	mcfqspi->clk = devm_clk_get_enabled(&pdev->dev, "qspi_clk");
	if (IS_ERR(mcfqspi->clk)) {
		dev_dbg(&pdev->dev, "clk_get failed\n");
		status = PTR_ERR(mcfqspi->clk);
		goto fail0;
	}

	host->bus_num = pdata->bus_num;
	host->num_chipselect = pdata->num_chipselect;

	mcfqspi->cs_control = pdata->cs_control;
	status = mcfqspi_cs_setup(mcfqspi);
	if (status) {
		dev_dbg(&pdev->dev, "error initializing cs_control\n");
		goto fail0;
	}

	init_waitqueue_head(&mcfqspi->waitq);

	host->mode_bits = SPI_CS_HIGH | SPI_CPOL | SPI_CPHA;
	host->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 16);
	host->setup = mcfqspi_setup;
	host->set_cs = mcfqspi_set_cs;
	host->transfer_one = mcfqspi_transfer_one;
	host->auto_runtime_pm = true;

	platform_set_drvdata(pdev, host);
	pm_runtime_enable(&pdev->dev);

	status = devm_spi_register_controller(&pdev->dev, host);
	if (status) {
		dev_dbg(&pdev->dev, "devm_spi_register_controller failed\n");
		goto fail1;
	}

	dev_info(&pdev->dev, "Coldfire QSPI bus driver\n");

	return 0;

fail1:
	pm_runtime_disable(&pdev->dev);
	mcfqspi_cs_teardown(mcfqspi);
fail0:
	spi_controller_put(host);

	dev_dbg(&pdev->dev, "Coldfire QSPI probe failed\n");

	return status;
}

static void mcfqspi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);

	pm_runtime_disable(&pdev->dev);
	/* disable the hardware (set the baud rate to 0) */
	mcfqspi_wr_qmr(mcfqspi, MCFQSPI_QMR_MSTR);

	mcfqspi_cs_teardown(mcfqspi);
}

#ifdef CONFIG_PM_SLEEP
static int mcfqspi_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);
	int ret;

	ret = spi_controller_suspend(host);
	if (ret)
		return ret;

	clk_disable(mcfqspi->clk);

	return 0;
}

static int mcfqspi_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);

	clk_enable(mcfqspi->clk);

	return spi_controller_resume(host);
}
#endif

#ifdef CONFIG_PM
static int mcfqspi_runtime_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);

	clk_disable(mcfqspi->clk);

	return 0;
}

static int mcfqspi_runtime_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mcfqspi *mcfqspi = spi_controller_get_devdata(host);

	clk_enable(mcfqspi->clk);

	return 0;
}
#endif

static const struct dev_pm_ops mcfqspi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(mcfqspi_suspend, mcfqspi_resume)
	SET_RUNTIME_PM_OPS(mcfqspi_runtime_suspend, mcfqspi_runtime_resume,
			NULL)
};

static struct platform_driver mcfqspi_driver = {
	.driver.name	= DRIVER_NAME,
	.driver.pm	= &mcfqspi_pm,
	.probe		= mcfqspi_probe,
	.remove		= mcfqspi_remove,
};
module_platform_driver(mcfqspi_driver);

MODULE_AUTHOR("Steven King <sfking@fdwdc.com>");
MODULE_DESCRIPTION("Coldfire QSPI Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
