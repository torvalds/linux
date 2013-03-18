/*
 * TXx9 SPI controller driver.
 *
 * Based on linux/arch/mips/tx4938/toshiba_rbtx4938/spi_txx9.c
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 *
 * Convert to generic SPI framework - Atsushi Nemoto (anemo@mba.ocn.ne.jp)
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <asm/gpio.h>


#define SPI_FIFO_SIZE 4
#define SPI_MAX_DIVIDER 0xff	/* Max. value for SPCR1.SER */
#define SPI_MIN_DIVIDER 1	/* Min. value for SPCR1.SER */

#define TXx9_SPMCR		0x00
#define TXx9_SPCR0		0x04
#define TXx9_SPCR1		0x08
#define TXx9_SPFS		0x0c
#define TXx9_SPSR		0x14
#define TXx9_SPDR		0x18

/* SPMCR : SPI Master Control */
#define TXx9_SPMCR_OPMODE	0xc0
#define TXx9_SPMCR_CONFIG	0x40
#define TXx9_SPMCR_ACTIVE	0x80
#define TXx9_SPMCR_SPSTP	0x02
#define TXx9_SPMCR_BCLR		0x01

/* SPCR0 : SPI Control 0 */
#define TXx9_SPCR0_TXIFL_MASK	0xc000
#define TXx9_SPCR0_RXIFL_MASK	0x3000
#define TXx9_SPCR0_SIDIE	0x0800
#define TXx9_SPCR0_SOEIE	0x0400
#define TXx9_SPCR0_RBSIE	0x0200
#define TXx9_SPCR0_TBSIE	0x0100
#define TXx9_SPCR0_IFSPSE	0x0010
#define TXx9_SPCR0_SBOS		0x0004
#define TXx9_SPCR0_SPHA		0x0002
#define TXx9_SPCR0_SPOL		0x0001

/* SPSR : SPI Status */
#define TXx9_SPSR_TBSI		0x8000
#define TXx9_SPSR_RBSI		0x4000
#define TXx9_SPSR_TBS_MASK	0x3800
#define TXx9_SPSR_RBS_MASK	0x0700
#define TXx9_SPSR_SPOE		0x0080
#define TXx9_SPSR_IFSD		0x0008
#define TXx9_SPSR_SIDLE		0x0004
#define TXx9_SPSR_STRDY		0x0002
#define TXx9_SPSR_SRRDY		0x0001


struct txx9spi {
	struct workqueue_struct	*workqueue;
	struct work_struct work;
	spinlock_t lock;	/* protect 'queue' */
	struct list_head queue;
	wait_queue_head_t waitq;
	void __iomem *membase;
	int baseclk;
	struct clk *clk;
	u32 max_speed_hz, min_speed_hz;
	int last_chipselect;
	int last_chipselect_val;
};

static u32 txx9spi_rd(struct txx9spi *c, int reg)
{
	return __raw_readl(c->membase + reg);
}
static void txx9spi_wr(struct txx9spi *c, u32 val, int reg)
{
	__raw_writel(val, c->membase + reg);
}

static void txx9spi_cs_func(struct spi_device *spi, struct txx9spi *c,
		int on, unsigned int cs_delay)
{
	int val = (spi->mode & SPI_CS_HIGH) ? on : !on;
	if (on) {
		/* deselect the chip with cs_change hint in last transfer */
		if (c->last_chipselect >= 0)
			gpio_set_value(c->last_chipselect,
					!c->last_chipselect_val);
		c->last_chipselect = spi->chip_select;
		c->last_chipselect_val = val;
	} else {
		c->last_chipselect = -1;
		ndelay(cs_delay);	/* CS Hold Time */
	}
	gpio_set_value(spi->chip_select, val);
	ndelay(cs_delay);	/* CS Setup Time / CS Recovery Time */
}

static int txx9spi_setup(struct spi_device *spi)
{
	struct txx9spi *c = spi_master_get_devdata(spi->master);
	u8 bits_per_word;

	if (!spi->max_speed_hz
			|| spi->max_speed_hz > c->max_speed_hz
			|| spi->max_speed_hz < c->min_speed_hz)
		return -EINVAL;

	bits_per_word = spi->bits_per_word;
	if (bits_per_word != 8 && bits_per_word != 16)
		return -EINVAL;

	if (gpio_direction_output(spi->chip_select,
			!(spi->mode & SPI_CS_HIGH))) {
		dev_err(&spi->dev, "Cannot setup GPIO for chipselect.\n");
		return -EINVAL;
	}

	/* deselect chip */
	spin_lock(&c->lock);
	txx9spi_cs_func(spi, c, 0, (NSEC_PER_SEC / 2) / spi->max_speed_hz);
	spin_unlock(&c->lock);

	return 0;
}

static irqreturn_t txx9spi_interrupt(int irq, void *dev_id)
{
	struct txx9spi *c = dev_id;

	/* disable rx intr */
	txx9spi_wr(c, txx9spi_rd(c, TXx9_SPCR0) & ~TXx9_SPCR0_RBSIE,
			TXx9_SPCR0);
	wake_up(&c->waitq);
	return IRQ_HANDLED;
}

static void txx9spi_work_one(struct txx9spi *c, struct spi_message *m)
{
	struct spi_device *spi = m->spi;
	struct spi_transfer *t;
	unsigned int cs_delay;
	unsigned int cs_change = 1;
	int status = 0;
	u32 mcr;
	u32 prev_speed_hz = 0;
	u8 prev_bits_per_word = 0;

	/* CS setup/hold/recovery time in nsec */
	cs_delay = 100 + (NSEC_PER_SEC / 2) / spi->max_speed_hz;

	mcr = txx9spi_rd(c, TXx9_SPMCR);
	if (unlikely((mcr & TXx9_SPMCR_OPMODE) == TXx9_SPMCR_ACTIVE)) {
		dev_err(&spi->dev, "Bad mode.\n");
		status = -EIO;
		goto exit;
	}
	mcr &= ~(TXx9_SPMCR_OPMODE | TXx9_SPMCR_SPSTP | TXx9_SPMCR_BCLR);

	/* enter config mode */
	txx9spi_wr(c, mcr | TXx9_SPMCR_CONFIG | TXx9_SPMCR_BCLR, TXx9_SPMCR);
	txx9spi_wr(c, TXx9_SPCR0_SBOS
			| ((spi->mode & SPI_CPOL) ? TXx9_SPCR0_SPOL : 0)
			| ((spi->mode & SPI_CPHA) ? TXx9_SPCR0_SPHA : 0)
			| 0x08,
			TXx9_SPCR0);

	list_for_each_entry (t, &m->transfers, transfer_list) {
		const void *txbuf = t->tx_buf;
		void *rxbuf = t->rx_buf;
		u32 data;
		unsigned int len = t->len;
		unsigned int wsize;
		u32 speed_hz = t->speed_hz ? : spi->max_speed_hz;
		u8 bits_per_word = t->bits_per_word;

		wsize = bits_per_word >> 3; /* in bytes */

		if (prev_speed_hz != speed_hz
				|| prev_bits_per_word != bits_per_word) {
			int n = DIV_ROUND_UP(c->baseclk, speed_hz) - 1;
			n = clamp(n, SPI_MIN_DIVIDER, SPI_MAX_DIVIDER);
			/* enter config mode */
			txx9spi_wr(c, mcr | TXx9_SPMCR_CONFIG | TXx9_SPMCR_BCLR,
					TXx9_SPMCR);
			txx9spi_wr(c, (n << 8) | bits_per_word, TXx9_SPCR1);
			/* enter active mode */
			txx9spi_wr(c, mcr | TXx9_SPMCR_ACTIVE, TXx9_SPMCR);

			prev_speed_hz = speed_hz;
			prev_bits_per_word = bits_per_word;
		}

		if (cs_change)
			txx9spi_cs_func(spi, c, 1, cs_delay);
		cs_change = t->cs_change;
		while (len) {
			unsigned int count = SPI_FIFO_SIZE;
			int i;
			u32 cr0;

			if (len < count * wsize)
				count = len / wsize;
			/* now tx must be idle... */
			while (!(txx9spi_rd(c, TXx9_SPSR) & TXx9_SPSR_SIDLE))
				cpu_relax();
			cr0 = txx9spi_rd(c, TXx9_SPCR0);
			cr0 &= ~TXx9_SPCR0_RXIFL_MASK;
			cr0 |= (count - 1) << 12;
			/* enable rx intr */
			cr0 |= TXx9_SPCR0_RBSIE;
			txx9spi_wr(c, cr0, TXx9_SPCR0);
			/* send */
			for (i = 0; i < count; i++) {
				if (txbuf) {
					data = (wsize == 1)
						? *(const u8 *)txbuf
						: *(const u16 *)txbuf;
					txx9spi_wr(c, data, TXx9_SPDR);
					txbuf += wsize;
				} else
					txx9spi_wr(c, 0, TXx9_SPDR);
			}
			/* wait all rx data */
			wait_event(c->waitq,
				txx9spi_rd(c, TXx9_SPSR) & TXx9_SPSR_RBSI);
			/* receive */
			for (i = 0; i < count; i++) {
				data = txx9spi_rd(c, TXx9_SPDR);
				if (rxbuf) {
					if (wsize == 1)
						*(u8 *)rxbuf = data;
					else
						*(u16 *)rxbuf = data;
					rxbuf += wsize;
				}
			}
			len -= count * wsize;
		}
		m->actual_length += t->len;
		if (t->delay_usecs)
			udelay(t->delay_usecs);

		if (!cs_change)
			continue;
		if (t->transfer_list.next == &m->transfers)
			break;
		/* sometimes a short mid-message deselect of the chip
		 * may be needed to terminate a mode or command
		 */
		txx9spi_cs_func(spi, c, 0, cs_delay);
	}

exit:
	m->status = status;
	m->complete(m->context);

	/* normally deactivate chipselect ... unless no error and
	 * cs_change has hinted that the next message will probably
	 * be for this chip too.
	 */
	if (!(status == 0 && cs_change))
		txx9spi_cs_func(spi, c, 0, cs_delay);

	/* enter config mode */
	txx9spi_wr(c, mcr | TXx9_SPMCR_CONFIG | TXx9_SPMCR_BCLR, TXx9_SPMCR);
}

static void txx9spi_work(struct work_struct *work)
{
	struct txx9spi *c = container_of(work, struct txx9spi, work);
	unsigned long flags;

	spin_lock_irqsave(&c->lock, flags);
	while (!list_empty(&c->queue)) {
		struct spi_message *m;

		m = container_of(c->queue.next, struct spi_message, queue);
		list_del_init(&m->queue);
		spin_unlock_irqrestore(&c->lock, flags);

		txx9spi_work_one(c, m);

		spin_lock_irqsave(&c->lock, flags);
	}
	spin_unlock_irqrestore(&c->lock, flags);
}

static int txx9spi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct spi_master *master = spi->master;
	struct txx9spi *c = spi_master_get_devdata(master);
	struct spi_transfer *t;
	unsigned long flags;

	m->actual_length = 0;

	/* check each transfer's parameters */
	list_for_each_entry (t, &m->transfers, transfer_list) {
		u32 speed_hz = t->speed_hz ? : spi->max_speed_hz;
		u8 bits_per_word = t->bits_per_word;

		if (!t->tx_buf && !t->rx_buf && t->len)
			return -EINVAL;
		if (bits_per_word != 8 && bits_per_word != 16)
			return -EINVAL;
		if (t->len & ((bits_per_word >> 3) - 1))
			return -EINVAL;
		if (speed_hz < c->min_speed_hz || speed_hz > c->max_speed_hz)
			return -EINVAL;
	}

	spin_lock_irqsave(&c->lock, flags);
	list_add_tail(&m->queue, &c->queue);
	queue_work(c->workqueue, &c->work);
	spin_unlock_irqrestore(&c->lock, flags);

	return 0;
}

static int txx9spi_probe(struct platform_device *dev)
{
	struct spi_master *master;
	struct txx9spi *c;
	struct resource *res;
	int ret = -ENODEV;
	u32 mcr;
	int irq;

	master = spi_alloc_master(&dev->dev, sizeof(*c));
	if (!master)
		return ret;
	c = spi_master_get_devdata(master);
	platform_set_drvdata(dev, master);

	INIT_WORK(&c->work, txx9spi_work);
	spin_lock_init(&c->lock);
	INIT_LIST_HEAD(&c->queue);
	init_waitqueue_head(&c->waitq);

	c->clk = clk_get(&dev->dev, "spi-baseclk");
	if (IS_ERR(c->clk)) {
		ret = PTR_ERR(c->clk);
		c->clk = NULL;
		goto exit;
	}
	ret = clk_enable(c->clk);
	if (ret) {
		clk_put(c->clk);
		c->clk = NULL;
		goto exit;
	}
	c->baseclk = clk_get_rate(c->clk);
	c->min_speed_hz = DIV_ROUND_UP(c->baseclk, SPI_MAX_DIVIDER + 1);
	c->max_speed_hz = c->baseclk / (SPI_MIN_DIVIDER + 1);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		goto exit_busy;
	if (!devm_request_mem_region(&dev->dev, res->start, resource_size(res),
				     "spi_txx9"))
		goto exit_busy;
	c->membase = devm_ioremap(&dev->dev, res->start, resource_size(res));
	if (!c->membase)
		goto exit_busy;

	/* enter config mode */
	mcr = txx9spi_rd(c, TXx9_SPMCR);
	mcr &= ~(TXx9_SPMCR_OPMODE | TXx9_SPMCR_SPSTP | TXx9_SPMCR_BCLR);
	txx9spi_wr(c, mcr | TXx9_SPMCR_CONFIG | TXx9_SPMCR_BCLR, TXx9_SPMCR);

	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		goto exit_busy;
	ret = devm_request_irq(&dev->dev, irq, txx9spi_interrupt, 0,
			       "spi_txx9", c);
	if (ret)
		goto exit;

	c->workqueue = create_singlethread_workqueue(
				dev_name(master->dev.parent));
	if (!c->workqueue)
		goto exit_busy;
	c->last_chipselect = -1;

	dev_info(&dev->dev, "at %#llx, irq %d, %dMHz\n",
		 (unsigned long long)res->start, irq,
		 (c->baseclk + 500000) / 1000000);

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CS_HIGH | SPI_CPOL | SPI_CPHA;

	master->bus_num = dev->id;
	master->setup = txx9spi_setup;
	master->transfer = txx9spi_transfer;
	master->num_chipselect = (u16)UINT_MAX; /* any GPIO numbers */

	ret = spi_register_master(master);
	if (ret)
		goto exit;
	return 0;
exit_busy:
	ret = -EBUSY;
exit:
	if (c->workqueue)
		destroy_workqueue(c->workqueue);
	if (c->clk) {
		clk_disable(c->clk);
		clk_put(c->clk);
	}
	platform_set_drvdata(dev, NULL);
	spi_master_put(master);
	return ret;
}

static int txx9spi_remove(struct platform_device *dev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(dev));
	struct txx9spi *c = spi_master_get_devdata(master);

	spi_unregister_master(master);
	platform_set_drvdata(dev, NULL);
	destroy_workqueue(c->workqueue);
	clk_disable(c->clk);
	clk_put(c->clk);
	spi_master_put(master);
	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:spi_txx9");

static struct platform_driver txx9spi_driver = {
	.remove = txx9spi_remove,
	.driver = {
		.name = "spi_txx9",
		.owner = THIS_MODULE,
	},
};

static int __init txx9spi_init(void)
{
	return platform_driver_probe(&txx9spi_driver, txx9spi_probe);
}
subsys_initcall(txx9spi_init);

static void __exit txx9spi_exit(void)
{
	platform_driver_unregister(&txx9spi_driver);
}
module_exit(txx9spi_exit);

MODULE_DESCRIPTION("TXx9 SPI Driver");
MODULE_LICENSE("GPL");
