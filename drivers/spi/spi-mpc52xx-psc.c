// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC52xx PSC in SPI mode driver.
 *
 * Maintainer: Dragos Carp
 *
 * Copyright (C) 2006 TOPTICA Photonics AG.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>

#include <asm/mpc52xx.h>
#include <asm/mpc52xx_psc.h>

#define MCLK 20000000 /* PSC port MClk in hz */

struct mpc52xx_psc_spi {
	/* driver internal data */
	struct mpc52xx_psc __iomem *psc;
	struct mpc52xx_psc_fifo __iomem *fifo;
	int irq;
	u8 bits_per_word;

	struct completion done;
};

/* controller state */
struct mpc52xx_psc_spi_cs {
	int bits_per_word;
	int speed_hz;
};

/* set clock freq, clock ramp, bits per work
 * if t is NULL then reset the values to the default values
 */
static int mpc52xx_psc_spi_transfer_setup(struct spi_device *spi,
		struct spi_transfer *t)
{
	struct mpc52xx_psc_spi_cs *cs = spi->controller_state;

	cs->speed_hz = (t && t->speed_hz)
			? t->speed_hz : spi->max_speed_hz;
	cs->bits_per_word = (t && t->bits_per_word)
			? t->bits_per_word : spi->bits_per_word;
	cs->bits_per_word = ((cs->bits_per_word + 7) / 8) * 8;
	return 0;
}

static void mpc52xx_psc_spi_activate_cs(struct spi_device *spi)
{
	struct mpc52xx_psc_spi_cs *cs = spi->controller_state;
	struct mpc52xx_psc_spi *mps = spi_controller_get_devdata(spi->controller);
	struct mpc52xx_psc __iomem *psc = mps->psc;
	u32 sicr;
	u16 ccr;

	sicr = in_be32(&psc->sicr);

	/* Set clock phase and polarity */
	if (spi->mode & SPI_CPHA)
		sicr |= 0x00001000;
	else
		sicr &= ~0x00001000;
	if (spi->mode & SPI_CPOL)
		sicr |= 0x00002000;
	else
		sicr &= ~0x00002000;

	if (spi->mode & SPI_LSB_FIRST)
		sicr |= 0x10000000;
	else
		sicr &= ~0x10000000;
	out_be32(&psc->sicr, sicr);

	/* Set clock frequency and bits per word
	 * Because psc->ccr is defined as 16bit register instead of 32bit
	 * just set the lower byte of BitClkDiv
	 */
	ccr = in_be16((u16 __iomem *)&psc->ccr);
	ccr &= 0xFF00;
	if (cs->speed_hz)
		ccr |= (MCLK / cs->speed_hz - 1) & 0xFF;
	else /* by default SPI Clk 1MHz */
		ccr |= (MCLK / 1000000 - 1) & 0xFF;
	out_be16((u16 __iomem *)&psc->ccr, ccr);
	mps->bits_per_word = cs->bits_per_word;
}

#define MPC52xx_PSC_BUFSIZE (MPC52xx_PSC_RFNUM_MASK + 1)
/* wake up when 80% fifo full */
#define MPC52xx_PSC_RFALARM (MPC52xx_PSC_BUFSIZE * 20 / 100)

static int mpc52xx_psc_spi_transfer_rxtx(struct spi_device *spi,
						struct spi_transfer *t)
{
	struct mpc52xx_psc_spi *mps = spi_controller_get_devdata(spi->controller);
	struct mpc52xx_psc __iomem *psc = mps->psc;
	struct mpc52xx_psc_fifo __iomem *fifo = mps->fifo;
	unsigned rb = 0;	/* number of bytes received */
	unsigned sb = 0;	/* number of bytes sent */
	unsigned char *rx_buf = (unsigned char *)t->rx_buf;
	unsigned char *tx_buf = (unsigned char *)t->tx_buf;
	unsigned rfalarm;
	unsigned send_at_once = MPC52xx_PSC_BUFSIZE;
	unsigned recv_at_once;
	int last_block = 0;

	if (!t->tx_buf && !t->rx_buf && t->len)
		return -EINVAL;

	/* enable transmiter/receiver */
	out_8(&psc->command, MPC52xx_PSC_TX_ENABLE | MPC52xx_PSC_RX_ENABLE);
	while (rb < t->len) {
		if (t->len - rb > MPC52xx_PSC_BUFSIZE) {
			rfalarm = MPC52xx_PSC_RFALARM;
			last_block = 0;
		} else {
			send_at_once = t->len - sb;
			rfalarm = MPC52xx_PSC_BUFSIZE - (t->len - rb);
			last_block = 1;
		}

		dev_dbg(&spi->dev, "send %d bytes...\n", send_at_once);
		for (; send_at_once; sb++, send_at_once--) {
			/* set EOF flag before the last word is sent */
			if (send_at_once == 1 && last_block)
				out_8(&psc->ircr2, 0x01);

			if (tx_buf)
				out_8(&psc->mpc52xx_psc_buffer_8, tx_buf[sb]);
			else
				out_8(&psc->mpc52xx_psc_buffer_8, 0);
		}


		/* enable interrupts and wait for wake up
		 * if just one byte is expected the Rx FIFO genererates no
		 * FFULL interrupt, so activate the RxRDY interrupt
		 */
		out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1);
		if (t->len - rb == 1) {
			out_8(&psc->mode, 0);
		} else {
			out_8(&psc->mode, MPC52xx_PSC_MODE_FFULL);
			out_be16(&fifo->rfalarm, rfalarm);
		}
		out_be16(&psc->mpc52xx_psc_imr, MPC52xx_PSC_IMR_RXRDY);
		wait_for_completion(&mps->done);
		recv_at_once = in_be16(&fifo->rfnum);
		dev_dbg(&spi->dev, "%d bytes received\n", recv_at_once);

		send_at_once = recv_at_once;
		if (rx_buf) {
			for (; recv_at_once; rb++, recv_at_once--)
				rx_buf[rb] = in_8(&psc->mpc52xx_psc_buffer_8);
		} else {
			for (; recv_at_once; rb++, recv_at_once--)
				in_8(&psc->mpc52xx_psc_buffer_8);
		}
	}
	/* disable transmiter/receiver */
	out_8(&psc->command, MPC52xx_PSC_TX_DISABLE | MPC52xx_PSC_RX_DISABLE);

	return 0;
}

static int mpc52xx_psc_spi_transfer_one_message(struct spi_controller *ctlr,
						struct spi_message *m)
{
	struct spi_device *spi;
	struct spi_transfer *t = NULL;
	unsigned cs_change;
	int status;

	spi = m->spi;
	cs_change = 1;
	status = 0;
	list_for_each_entry (t, &m->transfers, transfer_list) {
		if (t->bits_per_word || t->speed_hz) {
			status = mpc52xx_psc_spi_transfer_setup(spi, t);
			if (status < 0)
				break;
		}

		if (cs_change)
			mpc52xx_psc_spi_activate_cs(spi);
		cs_change = t->cs_change;

		status = mpc52xx_psc_spi_transfer_rxtx(spi, t);
		if (status)
			break;
		m->actual_length += t->len;

		spi_transfer_delay_exec(t);
	}

	m->status = status;

	mpc52xx_psc_spi_transfer_setup(spi, NULL);

	spi_finalize_current_message(ctlr);

	return 0;
}

static int mpc52xx_psc_spi_setup(struct spi_device *spi)
{
	struct mpc52xx_psc_spi_cs *cs = spi->controller_state;

	if (spi->bits_per_word%8)
		return -EINVAL;

	if (!cs) {
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi->controller_state = cs;
	}

	cs->bits_per_word = spi->bits_per_word;
	cs->speed_hz = spi->max_speed_hz;

	return 0;
}

static void mpc52xx_psc_spi_cleanup(struct spi_device *spi)
{
	kfree(spi->controller_state);
}

static int mpc52xx_psc_spi_port_config(int psc_id, struct mpc52xx_psc_spi *mps)
{
	struct mpc52xx_psc __iomem *psc = mps->psc;
	struct mpc52xx_psc_fifo __iomem *fifo = mps->fifo;
	u32 mclken_div;
	int ret;

	/* default sysclk is 512MHz */
	mclken_div = 512000000 / MCLK;
	ret = mpc52xx_set_psc_clkdiv(psc_id, mclken_div);
	if (ret)
		return ret;

	/* Reset the PSC into a known state */
	out_8(&psc->command, MPC52xx_PSC_RST_RX);
	out_8(&psc->command, MPC52xx_PSC_RST_TX);
	out_8(&psc->command, MPC52xx_PSC_TX_DISABLE | MPC52xx_PSC_RX_DISABLE);

	/* Disable interrupts, interrupts are based on alarm level */
	out_be16(&psc->mpc52xx_psc_imr, 0);
	out_8(&psc->command, MPC52xx_PSC_SEL_MODE_REG_1);
	out_8(&fifo->rfcntl, 0);
	out_8(&psc->mode, MPC52xx_PSC_MODE_FFULL);

	/* Configure 8bit codec mode as a SPI host and use EOF flags */
	/* SICR_SIM_CODEC8|SICR_GENCLK|SICR_SPI|SICR_MSTR|SICR_USEEOF */
	out_be32(&psc->sicr, 0x0180C800);
	out_be16((u16 __iomem *)&psc->ccr, 0x070F); /* default SPI Clk 1MHz */

	/* Set 2ms DTL delay */
	out_8(&psc->ctur, 0x00);
	out_8(&psc->ctlr, 0x84);

	mps->bits_per_word = 8;

	return 0;
}

static irqreturn_t mpc52xx_psc_spi_isr(int irq, void *dev_id)
{
	struct mpc52xx_psc_spi *mps = (struct mpc52xx_psc_spi *)dev_id;
	struct mpc52xx_psc __iomem *psc = mps->psc;

	/* disable interrupt and wake up the work queue */
	if (in_be16(&psc->mpc52xx_psc_isr) & MPC52xx_PSC_IMR_RXRDY) {
		out_be16(&psc->mpc52xx_psc_imr, 0);
		complete(&mps->done);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int mpc52xx_psc_spi_of_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpc52xx_psc_spi *mps;
	struct spi_controller *host;
	u32 bus_num;
	int ret;

	host = devm_spi_alloc_host(dev, sizeof(*mps));
	if (host == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, host);
	mps = spi_controller_get_devdata(host);

	/* the spi->mode bits understood by this driver: */
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;

	ret = device_property_read_u32(dev, "cell-index", &bus_num);
	if (ret || bus_num > 5)
		return dev_err_probe(dev, ret ? : -EINVAL, "Invalid cell-index property\n");
	host->bus_num = bus_num + 1;

	host->num_chipselect = 255;
	host->setup = mpc52xx_psc_spi_setup;
	host->transfer_one_message = mpc52xx_psc_spi_transfer_one_message;
	host->cleanup = mpc52xx_psc_spi_cleanup;

	device_set_node(&host->dev, dev_fwnode(dev));

	mps->psc = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(mps->psc))
		return dev_err_probe(dev, PTR_ERR(mps->psc), "could not ioremap I/O port range\n");

	/* On the 5200, fifo regs are immediately adjacent to the psc regs */
	mps->fifo = ((void __iomem *)mps->psc) + sizeof(struct mpc52xx_psc);

	mps->irq = platform_get_irq(pdev, 0);
	if (mps->irq < 0)
		return mps->irq;

	ret = devm_request_irq(dev, mps->irq, mpc52xx_psc_spi_isr, 0,
			       "mpc52xx-psc-spi", mps);
	if (ret)
		return ret;

	ret = mpc52xx_psc_spi_port_config(host->bus_num, mps);
	if (ret < 0)
		return dev_err_probe(dev, ret, "can't configure PSC! Is it capable of SPI?\n");

	init_completion(&mps->done);

	return devm_spi_register_controller(dev, host);
}

static const struct of_device_id mpc52xx_psc_spi_of_match[] = {
	{ .compatible = "fsl,mpc5200-psc-spi", },
	{ .compatible = "mpc5200-psc-spi", }, /* old */
	{}
};

MODULE_DEVICE_TABLE(of, mpc52xx_psc_spi_of_match);

static struct platform_driver mpc52xx_psc_spi_of_driver = {
	.probe = mpc52xx_psc_spi_of_probe,
	.driver = {
		.name = "mpc52xx-psc-spi",
		.of_match_table = mpc52xx_psc_spi_of_match,
	},
};
module_platform_driver(mpc52xx_psc_spi_of_driver);

MODULE_AUTHOR("Dragos Carp");
MODULE_DESCRIPTION("MPC52xx PSC SPI Driver");
MODULE_LICENSE("GPL");
