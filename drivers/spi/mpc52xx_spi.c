/*
 * MPC52xx SPI bus driver.
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 *
 * This file is released under the GPLv2
 *
 * This is the driver for the MPC5200's dedicated SPI controller.
 *
 * Note: this driver does not support the MPC5200 PSC in SPI mode.  For
 * that driver see drivers/spi/mpc52xx_psc_spi.c
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/of_spi.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <asm/time.h>
#include <asm/mpc52xx.h>

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("MPC52xx SPI (non-PSC) Driver");
MODULE_LICENSE("GPL");

/* Register offsets */
#define SPI_CTRL1	0x00
#define SPI_CTRL1_SPIE		(1 << 7)
#define SPI_CTRL1_SPE		(1 << 6)
#define SPI_CTRL1_MSTR		(1 << 4)
#define SPI_CTRL1_CPOL		(1 << 3)
#define SPI_CTRL1_CPHA		(1 << 2)
#define SPI_CTRL1_SSOE		(1 << 1)
#define SPI_CTRL1_LSBFE		(1 << 0)

#define SPI_CTRL2	0x01
#define SPI_BRR		0x04

#define SPI_STATUS	0x05
#define SPI_STATUS_SPIF		(1 << 7)
#define SPI_STATUS_WCOL		(1 << 6)
#define SPI_STATUS_MODF		(1 << 4)

#define SPI_DATA	0x09
#define SPI_PORTDATA	0x0d
#define SPI_DATADIR	0x10

/* FSM state return values */
#define FSM_STOP	0	/* Nothing more for the state machine to */
				/* do.  If something interesting happens */
				/* then an IRQ will be received */
#define FSM_POLL	1	/* need to poll for completion, an IRQ is */
				/* not expected */
#define FSM_CONTINUE	2	/* Keep iterating the state machine */

/* Driver internal data */
struct mpc52xx_spi {
	struct spi_master *master;
	void __iomem *regs;
	int irq0;	/* MODF irq */
	int irq1;	/* SPIF irq */
	unsigned int ipb_freq;

	/* Statistics; not used now, but will be reintroduced for debugfs */
	int msg_count;
	int wcol_count;
	int wcol_ticks;
	u32 wcol_tx_timestamp;
	int modf_count;
	int byte_count;

	struct list_head queue;		/* queue of pending messages */
	spinlock_t lock;
	struct work_struct work;

	/* Details of current transfer (length, and buffer pointers) */
	struct spi_message *message;	/* current message */
	struct spi_transfer *transfer;	/* current transfer */
	int (*state)(int irq, struct mpc52xx_spi *ms, u8 status, u8 data);
	int len;
	int timestamp;
	u8 *rx_buf;
	const u8 *tx_buf;
	int cs_change;
	int gpio_cs_count;
	unsigned int *gpio_cs;
};

/*
 * CS control function
 */
static void mpc52xx_spi_chipsel(struct mpc52xx_spi *ms, int value)
{
	int cs;

	if (ms->gpio_cs_count > 0) {
		cs = ms->message->spi->chip_select;
		gpio_set_value(ms->gpio_cs[cs], value ? 0 : 1);
	} else
		out_8(ms->regs + SPI_PORTDATA, value ? 0 : 0x08);
}

/*
 * Start a new transfer.  This is called both by the idle state
 * for the first transfer in a message, and by the wait state when the
 * previous transfer in a message is complete.
 */
static void mpc52xx_spi_start_transfer(struct mpc52xx_spi *ms)
{
	ms->rx_buf = ms->transfer->rx_buf;
	ms->tx_buf = ms->transfer->tx_buf;
	ms->len = ms->transfer->len;

	/* Activate the chip select */
	if (ms->cs_change)
		mpc52xx_spi_chipsel(ms, 1);
	ms->cs_change = ms->transfer->cs_change;

	/* Write out the first byte */
	ms->wcol_tx_timestamp = get_tbl();
	if (ms->tx_buf)
		out_8(ms->regs + SPI_DATA, *ms->tx_buf++);
	else
		out_8(ms->regs + SPI_DATA, 0);
}

/* Forward declaration of state handlers */
static int mpc52xx_spi_fsmstate_transfer(int irq, struct mpc52xx_spi *ms,
					 u8 status, u8 data);
static int mpc52xx_spi_fsmstate_wait(int irq, struct mpc52xx_spi *ms,
				     u8 status, u8 data);

/*
 * IDLE state
 *
 * No transfers are in progress; if another transfer is pending then retrieve
 * it and kick it off.  Otherwise, stop processing the state machine
 */
static int
mpc52xx_spi_fsmstate_idle(int irq, struct mpc52xx_spi *ms, u8 status, u8 data)
{
	struct spi_device *spi;
	int spr, sppr;
	u8 ctrl1;

	if (status && (irq != NO_IRQ))
		dev_err(&ms->master->dev, "spurious irq, status=0x%.2x\n",
			status);

	/* Check if there is another transfer waiting. */
	if (list_empty(&ms->queue))
		return FSM_STOP;

	/* get the head of the queue */
	ms->message = list_first_entry(&ms->queue, struct spi_message, queue);
	list_del_init(&ms->message->queue);

	/* Setup the controller parameters */
	ctrl1 = SPI_CTRL1_SPIE | SPI_CTRL1_SPE | SPI_CTRL1_MSTR;
	spi = ms->message->spi;
	if (spi->mode & SPI_CPHA)
		ctrl1 |= SPI_CTRL1_CPHA;
	if (spi->mode & SPI_CPOL)
		ctrl1 |= SPI_CTRL1_CPOL;
	if (spi->mode & SPI_LSB_FIRST)
		ctrl1 |= SPI_CTRL1_LSBFE;
	out_8(ms->regs + SPI_CTRL1, ctrl1);

	/* Setup the controller speed */
	/* minimum divider is '2'.  Also, add '1' to force rounding the
	 * divider up. */
	sppr = ((ms->ipb_freq / ms->message->spi->max_speed_hz) + 1) >> 1;
	spr = 0;
	if (sppr < 1)
		sppr = 1;
	while (((sppr - 1) & ~0x7) != 0) {
		sppr = (sppr + 1) >> 1; /* add '1' to force rounding up */
		spr++;
	}
	sppr--;		/* sppr quantity in register is offset by 1 */
	if (spr > 7) {
		/* Don't overrun limits of SPI baudrate register */
		spr = 7;
		sppr = 7;
	}
	out_8(ms->regs + SPI_BRR, sppr << 4 | spr); /* Set speed */

	ms->cs_change = 1;
	ms->transfer = container_of(ms->message->transfers.next,
				    struct spi_transfer, transfer_list);

	mpc52xx_spi_start_transfer(ms);
	ms->state = mpc52xx_spi_fsmstate_transfer;

	return FSM_CONTINUE;
}

/*
 * TRANSFER state
 *
 * In the middle of a transfer.  If the SPI core has completed processing
 * a byte, then read out the received data and write out the next byte
 * (unless this transfer is finished; in which case go on to the wait
 * state)
 */
static int mpc52xx_spi_fsmstate_transfer(int irq, struct mpc52xx_spi *ms,
					 u8 status, u8 data)
{
	if (!status)
		return ms->irq0 ? FSM_STOP : FSM_POLL;

	if (status & SPI_STATUS_WCOL) {
		/* The SPI controller is stoopid.  At slower speeds, it may
		 * raise the SPIF flag before the state machine is actually
		 * finished, which causes a collision (internal to the state
		 * machine only).  The manual recommends inserting a delay
		 * between receiving the interrupt and sending the next byte,
		 * but it can also be worked around simply by retrying the
		 * transfer which is what we do here. */
		ms->wcol_count++;
		ms->wcol_ticks += get_tbl() - ms->wcol_tx_timestamp;
		ms->wcol_tx_timestamp = get_tbl();
		data = 0;
		if (ms->tx_buf)
			data = *(ms->tx_buf - 1);
		out_8(ms->regs + SPI_DATA, data); /* try again */
		return FSM_CONTINUE;
	} else if (status & SPI_STATUS_MODF) {
		ms->modf_count++;
		dev_err(&ms->master->dev, "mode fault\n");
		mpc52xx_spi_chipsel(ms, 0);
		ms->message->status = -EIO;
		ms->message->complete(ms->message->context);
		ms->state = mpc52xx_spi_fsmstate_idle;
		return FSM_CONTINUE;
	}

	/* Read data out of the spi device */
	ms->byte_count++;
	if (ms->rx_buf)
		*ms->rx_buf++ = data;

	/* Is the transfer complete? */
	ms->len--;
	if (ms->len == 0) {
		ms->timestamp = get_tbl();
		ms->timestamp += ms->transfer->delay_usecs * tb_ticks_per_usec;
		ms->state = mpc52xx_spi_fsmstate_wait;
		return FSM_CONTINUE;
	}

	/* Write out the next byte */
	ms->wcol_tx_timestamp = get_tbl();
	if (ms->tx_buf)
		out_8(ms->regs + SPI_DATA, *ms->tx_buf++);
	else
		out_8(ms->regs + SPI_DATA, 0);

	return FSM_CONTINUE;
}

/*
 * WAIT state
 *
 * A transfer has completed; need to wait for the delay period to complete
 * before starting the next transfer
 */
static int
mpc52xx_spi_fsmstate_wait(int irq, struct mpc52xx_spi *ms, u8 status, u8 data)
{
	if (status && irq)
		dev_err(&ms->master->dev, "spurious irq, status=0x%.2x\n",
			status);

	if (((int)get_tbl()) - ms->timestamp < 0)
		return FSM_POLL;

	ms->message->actual_length += ms->transfer->len;

	/* Check if there is another transfer in this message.  If there
	 * aren't then deactivate CS, notify sender, and drop back to idle
	 * to start the next message. */
	if (ms->transfer->transfer_list.next == &ms->message->transfers) {
		ms->msg_count++;
		mpc52xx_spi_chipsel(ms, 0);
		ms->message->status = 0;
		ms->message->complete(ms->message->context);
		ms->state = mpc52xx_spi_fsmstate_idle;
		return FSM_CONTINUE;
	}

	/* There is another transfer; kick it off */

	if (ms->cs_change)
		mpc52xx_spi_chipsel(ms, 0);

	ms->transfer = container_of(ms->transfer->transfer_list.next,
				    struct spi_transfer, transfer_list);
	mpc52xx_spi_start_transfer(ms);
	ms->state = mpc52xx_spi_fsmstate_transfer;
	return FSM_CONTINUE;
}

/**
 * mpc52xx_spi_fsm_process - Finite State Machine iteration function
 * @irq: irq number that triggered the FSM or 0 for polling
 * @ms: pointer to mpc52xx_spi driver data
 */
static void mpc52xx_spi_fsm_process(int irq, struct mpc52xx_spi *ms)
{
	int rc = FSM_CONTINUE;
	u8 status, data;

	while (rc == FSM_CONTINUE) {
		/* Interrupt cleared by read of STATUS followed by
		 * read of DATA registers */
		status = in_8(ms->regs + SPI_STATUS);
		data = in_8(ms->regs + SPI_DATA);
		rc = ms->state(irq, ms, status, data);
	}

	if (rc == FSM_POLL)
		schedule_work(&ms->work);
}

/**
 * mpc52xx_spi_irq - IRQ handler
 */
static irqreturn_t mpc52xx_spi_irq(int irq, void *_ms)
{
	struct mpc52xx_spi *ms = _ms;
	spin_lock(&ms->lock);
	mpc52xx_spi_fsm_process(irq, ms);
	spin_unlock(&ms->lock);
	return IRQ_HANDLED;
}

/**
 * mpc52xx_spi_wq - Workqueue function for polling the state machine
 */
static void mpc52xx_spi_wq(struct work_struct *work)
{
	struct mpc52xx_spi *ms = container_of(work, struct mpc52xx_spi, work);
	unsigned long flags;

	spin_lock_irqsave(&ms->lock, flags);
	mpc52xx_spi_fsm_process(0, ms);
	spin_unlock_irqrestore(&ms->lock, flags);
}

/*
 * spi_master ops
 */

static int mpc52xx_spi_setup(struct spi_device *spi)
{
	if (spi->bits_per_word % 8)
		return -EINVAL;

	if (spi->mode & ~(SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST))
		return -EINVAL;

	if (spi->chip_select >= spi->master->num_chipselect)
		return -EINVAL;

	return 0;
}

static int mpc52xx_spi_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct mpc52xx_spi *ms = spi_master_get_devdata(spi->master);
	unsigned long flags;

	m->actual_length = 0;
	m->status = -EINPROGRESS;

	spin_lock_irqsave(&ms->lock, flags);
	list_add_tail(&m->queue, &ms->queue);
	spin_unlock_irqrestore(&ms->lock, flags);
	schedule_work(&ms->work);

	return 0;
}

/*
 * OF Platform Bus Binding
 */
static int __devinit mpc52xx_spi_probe(struct of_device *op,
				       const struct of_device_id *match)
{
	struct spi_master *master;
	struct mpc52xx_spi *ms;
	void __iomem *regs;
	u8 ctrl1;
	int rc, i = 0;
	int gpio_cs;

	/* MMIO registers */
	dev_dbg(&op->dev, "probing mpc5200 SPI device\n");
	regs = of_iomap(op->node, 0);
	if (!regs)
		return -ENODEV;

	/* initialize the device */
	ctrl1 = SPI_CTRL1_SPIE | SPI_CTRL1_SPE | SPI_CTRL1_MSTR;
	out_8(regs + SPI_CTRL1, ctrl1);
	out_8(regs + SPI_CTRL2, 0x0);
	out_8(regs + SPI_DATADIR, 0xe);	/* Set output pins */
	out_8(regs + SPI_PORTDATA, 0x8);	/* Deassert /SS signal */

	/* Clear the status register and re-read it to check for a MODF
	 * failure.  This driver cannot currently handle multiple masters
	 * on the SPI bus.  This fault will also occur if the SPI signals
	 * are not connected to any pins (port_config setting) */
	in_8(regs + SPI_STATUS);
	out_8(regs + SPI_CTRL1, ctrl1);

	in_8(regs + SPI_DATA);
	if (in_8(regs + SPI_STATUS) & SPI_STATUS_MODF) {
		dev_err(&op->dev, "mode fault; is port_config correct?\n");
		rc = -EIO;
		goto err_init;
	}

	dev_dbg(&op->dev, "allocating spi_master struct\n");
	master = spi_alloc_master(&op->dev, sizeof *ms);
	if (!master) {
		rc = -ENOMEM;
		goto err_alloc;
	}

	master->bus_num = -1;
	master->setup = mpc52xx_spi_setup;
	master->transfer = mpc52xx_spi_transfer;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;

	dev_set_drvdata(&op->dev, master);

	ms = spi_master_get_devdata(master);
	ms->master = master;
	ms->regs = regs;
	ms->irq0 = irq_of_parse_and_map(op->node, 0);
	ms->irq1 = irq_of_parse_and_map(op->node, 1);
	ms->state = mpc52xx_spi_fsmstate_idle;
	ms->ipb_freq = mpc5xxx_get_bus_frequency(op->node);
	ms->gpio_cs_count = of_gpio_count(op->node);
	if (ms->gpio_cs_count > 0) {
		master->num_chipselect = ms->gpio_cs_count;
		ms->gpio_cs = kmalloc(ms->gpio_cs_count * sizeof(unsigned int),
				GFP_KERNEL);
		if (!ms->gpio_cs) {
			rc = -ENOMEM;
			goto err_alloc;
		}

		for (i = 0; i < ms->gpio_cs_count; i++) {
			gpio_cs = of_get_gpio(op->node, i);
			if (gpio_cs < 0) {
				dev_err(&op->dev,
					"could not parse the gpio field "
					"in oftree\n");
				rc = -ENODEV;
				goto err_gpio;
			}

			rc = gpio_request(gpio_cs, dev_name(&op->dev));
			if (rc) {
				dev_err(&op->dev,
					"can't request spi cs gpio #%d "
					"on gpio line %d\n", i, gpio_cs);
				goto err_gpio;
			}

			gpio_direction_output(gpio_cs, 1);
			ms->gpio_cs[i] = gpio_cs;
		}
	} else {
		master->num_chipselect = 1;
	}

	spin_lock_init(&ms->lock);
	INIT_LIST_HEAD(&ms->queue);
	INIT_WORK(&ms->work, mpc52xx_spi_wq);

	/* Decide if interrupts can be used */
	if (ms->irq0 && ms->irq1) {
		rc = request_irq(ms->irq0, mpc52xx_spi_irq, 0,
				  "mpc5200-spi-modf", ms);
		rc |= request_irq(ms->irq1, mpc52xx_spi_irq, 0,
				  "mpc5200-spi-spif", ms);
		if (rc) {
			free_irq(ms->irq0, ms);
			free_irq(ms->irq1, ms);
			ms->irq0 = ms->irq1 = 0;
		}
	} else {
		/* operate in polled mode */
		ms->irq0 = ms->irq1 = 0;
	}

	if (!ms->irq0)
		dev_info(&op->dev, "using polled mode\n");

	dev_dbg(&op->dev, "registering spi_master struct\n");
	rc = spi_register_master(master);
	if (rc)
		goto err_register;

	of_register_spi_devices(master, op->node);
	dev_info(&ms->master->dev, "registered MPC5200 SPI bus\n");

	return rc;

 err_register:
	dev_err(&ms->master->dev, "initialization failed\n");
	spi_master_put(master);
 err_gpio:
	while (i-- > 0)
		gpio_free(ms->gpio_cs[i]);

	kfree(ms->gpio_cs);
 err_alloc:
 err_init:
	iounmap(regs);
	return rc;
}

static int __devexit mpc52xx_spi_remove(struct of_device *op)
{
	struct spi_master *master = dev_get_drvdata(&op->dev);
	struct mpc52xx_spi *ms = spi_master_get_devdata(master);
	int i;

	free_irq(ms->irq0, ms);
	free_irq(ms->irq1, ms);

	for (i = 0; i < ms->gpio_cs_count; i++)
		gpio_free(ms->gpio_cs[i]);

	kfree(ms->gpio_cs);
	spi_unregister_master(master);
	spi_master_put(master);
	iounmap(ms->regs);

	return 0;
}

static const struct of_device_id mpc52xx_spi_match[] __devinitconst = {
	{ .compatible = "fsl,mpc5200-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, mpc52xx_spi_match);

static struct of_platform_driver mpc52xx_spi_of_driver = {
	.owner = THIS_MODULE,
	.name = "mpc52xx-spi",
	.match_table = mpc52xx_spi_match,
	.probe = mpc52xx_spi_probe,
	.remove = __exit_p(mpc52xx_spi_remove),
};

static int __init mpc52xx_spi_init(void)
{
	return of_register_platform_driver(&mpc52xx_spi_of_driver);
}
module_init(mpc52xx_spi_init);

static void __exit mpc52xx_spi_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_spi_of_driver);
}
module_exit(mpc52xx_spi_exit);

