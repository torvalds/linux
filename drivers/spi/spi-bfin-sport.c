/*
 * SPI bus via the Blackfin SPORT peripheral
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#include <asm/portmux.h>
#include <asm/bfin5xx_spi.h>
#include <asm/blackfin.h>
#include <asm/bfin_sport.h>
#include <asm/cacheflush.h>

#define DRV_NAME	"bfin-sport-spi"
#define DRV_DESC	"SPI bus via the Blackfin SPORT"

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bfin-sport-spi");

enum bfin_sport_spi_state {
	START_STATE,
	RUNNING_STATE,
	DONE_STATE,
	ERROR_STATE,
};

struct bfin_sport_spi_master_data;

struct bfin_sport_transfer_ops {
	void (*write) (struct bfin_sport_spi_master_data *);
	void (*read) (struct bfin_sport_spi_master_data *);
	void (*duplex) (struct bfin_sport_spi_master_data *);
};

struct bfin_sport_spi_master_data {
	/* Driver model hookup */
	struct device *dev;

	/* SPI framework hookup */
	struct spi_master *master;

	/* Regs base of SPI controller */
	struct sport_register __iomem *regs;
	int err_irq;

	/* Pin request list */
	u16 *pin_req;

	/* Driver message queue */
	struct workqueue_struct *workqueue;
	struct work_struct pump_messages;
	spinlock_t lock;
	struct list_head queue;
	int busy;
	bool run;

	/* Message Transfer pump */
	struct tasklet_struct pump_transfers;

	/* Current message transfer state info */
	enum bfin_sport_spi_state state;
	struct spi_message *cur_msg;
	struct spi_transfer *cur_transfer;
	struct bfin_sport_spi_slave_data *cur_chip;
	union {
		void *tx;
		u8 *tx8;
		u16 *tx16;
	};
	void *tx_end;
	union {
		void *rx;
		u8 *rx8;
		u16 *rx16;
	};
	void *rx_end;

	int cs_change;
	struct bfin_sport_transfer_ops *ops;
};

struct bfin_sport_spi_slave_data {
	u16 ctl_reg;
	u16 baud;
	u16 cs_chg_udelay;	/* Some devices require > 255usec delay */
	u32 cs_gpio;
	u16 idle_tx_val;
	struct bfin_sport_transfer_ops *ops;
};

static void
bfin_sport_spi_enable(struct bfin_sport_spi_master_data *drv_data)
{
	bfin_write_or(&drv_data->regs->tcr1, TSPEN);
	bfin_write_or(&drv_data->regs->rcr1, TSPEN);
	SSYNC();
}

static void
bfin_sport_spi_disable(struct bfin_sport_spi_master_data *drv_data)
{
	bfin_write_and(&drv_data->regs->tcr1, ~TSPEN);
	bfin_write_and(&drv_data->regs->rcr1, ~TSPEN);
	SSYNC();
}

/* Caculate the SPI_BAUD register value based on input HZ */
static u16
bfin_sport_hz_to_spi_baud(u32 speed_hz)
{
	u_long clk, sclk = get_sclk();
	int div = (sclk / (2 * speed_hz)) - 1;

	if (div < 0)
		div = 0;

	clk = sclk / (2 * (div + 1));

	if (clk > speed_hz)
		div++;

	return div;
}

/* Chip select operation functions for cs_change flag */
static void
bfin_sport_spi_cs_active(struct bfin_sport_spi_slave_data *chip)
{
	gpio_direction_output(chip->cs_gpio, 0);
}

static void
bfin_sport_spi_cs_deactive(struct bfin_sport_spi_slave_data *chip)
{
	gpio_direction_output(chip->cs_gpio, 1);
	/* Move delay here for consistency */
	if (chip->cs_chg_udelay)
		udelay(chip->cs_chg_udelay);
}

static void
bfin_sport_spi_stat_poll_complete(struct bfin_sport_spi_master_data *drv_data)
{
	unsigned long timeout = jiffies + HZ;
	while (!(bfin_read(&drv_data->regs->stat) & RXNE)) {
		if (!time_before(jiffies, timeout))
			break;
	}
}

static void
bfin_sport_spi_u8_writer(struct bfin_sport_spi_master_data *drv_data)
{
	u16 dummy;

	while (drv_data->tx < drv_data->tx_end) {
		bfin_write(&drv_data->regs->tx16, *drv_data->tx8++);
		bfin_sport_spi_stat_poll_complete(drv_data);
		dummy = bfin_read(&drv_data->regs->rx16);
	}
}

static void
bfin_sport_spi_u8_reader(struct bfin_sport_spi_master_data *drv_data)
{
	u16 tx_val = drv_data->cur_chip->idle_tx_val;

	while (drv_data->rx < drv_data->rx_end) {
		bfin_write(&drv_data->regs->tx16, tx_val);
		bfin_sport_spi_stat_poll_complete(drv_data);
		*drv_data->rx8++ = bfin_read(&drv_data->regs->rx16);
	}
}

static void
bfin_sport_spi_u8_duplex(struct bfin_sport_spi_master_data *drv_data)
{
	while (drv_data->rx < drv_data->rx_end) {
		bfin_write(&drv_data->regs->tx16, *drv_data->tx8++);
		bfin_sport_spi_stat_poll_complete(drv_data);
		*drv_data->rx8++ = bfin_read(&drv_data->regs->rx16);
	}
}

static struct bfin_sport_transfer_ops bfin_sport_transfer_ops_u8 = {
	.write  = bfin_sport_spi_u8_writer,
	.read   = bfin_sport_spi_u8_reader,
	.duplex = bfin_sport_spi_u8_duplex,
};

static void
bfin_sport_spi_u16_writer(struct bfin_sport_spi_master_data *drv_data)
{
	u16 dummy;

	while (drv_data->tx < drv_data->tx_end) {
		bfin_write(&drv_data->regs->tx16, *drv_data->tx16++);
		bfin_sport_spi_stat_poll_complete(drv_data);
		dummy = bfin_read(&drv_data->regs->rx16);
	}
}

static void
bfin_sport_spi_u16_reader(struct bfin_sport_spi_master_data *drv_data)
{
	u16 tx_val = drv_data->cur_chip->idle_tx_val;

	while (drv_data->rx < drv_data->rx_end) {
		bfin_write(&drv_data->regs->tx16, tx_val);
		bfin_sport_spi_stat_poll_complete(drv_data);
		*drv_data->rx16++ = bfin_read(&drv_data->regs->rx16);
	}
}

static void
bfin_sport_spi_u16_duplex(struct bfin_sport_spi_master_data *drv_data)
{
	while (drv_data->rx < drv_data->rx_end) {
		bfin_write(&drv_data->regs->tx16, *drv_data->tx16++);
		bfin_sport_spi_stat_poll_complete(drv_data);
		*drv_data->rx16++ = bfin_read(&drv_data->regs->rx16);
	}
}

static struct bfin_sport_transfer_ops bfin_sport_transfer_ops_u16 = {
	.write  = bfin_sport_spi_u16_writer,
	.read   = bfin_sport_spi_u16_reader,
	.duplex = bfin_sport_spi_u16_duplex,
};

/* stop controller and re-config current chip */
static void
bfin_sport_spi_restore_state(struct bfin_sport_spi_master_data *drv_data)
{
	struct bfin_sport_spi_slave_data *chip = drv_data->cur_chip;

	bfin_sport_spi_disable(drv_data);
	dev_dbg(drv_data->dev, "restoring spi ctl state\n");

	bfin_write(&drv_data->regs->tcr1, chip->ctl_reg);
	bfin_write(&drv_data->regs->tclkdiv, chip->baud);
	SSYNC();

	bfin_write(&drv_data->regs->rcr1, chip->ctl_reg & ~(ITCLK | ITFS));
	SSYNC();

	bfin_sport_spi_cs_active(chip);
}

/* test if there is more transfer to be done */
static enum bfin_sport_spi_state
bfin_sport_spi_next_transfer(struct bfin_sport_spi_master_data *drv_data)
{
	struct spi_message *msg = drv_data->cur_msg;
	struct spi_transfer *trans = drv_data->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		drv_data->cur_transfer =
		    list_entry(trans->transfer_list.next,
			       struct spi_transfer, transfer_list);
		return RUNNING_STATE;
	}

	return DONE_STATE;
}

/*
 * caller already set message->status;
 * dma and pio irqs are blocked give finished message back
 */
static void
bfin_sport_spi_giveback(struct bfin_sport_spi_master_data *drv_data)
{
	struct bfin_sport_spi_slave_data *chip = drv_data->cur_chip;
	unsigned long flags;
	struct spi_message *msg;

	spin_lock_irqsave(&drv_data->lock, flags);
	msg = drv_data->cur_msg;
	drv_data->state = START_STATE;
	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	queue_work(drv_data->workqueue, &drv_data->pump_messages);
	spin_unlock_irqrestore(&drv_data->lock, flags);

	if (!drv_data->cs_change)
		bfin_sport_spi_cs_deactive(chip);

	if (msg->complete)
		msg->complete(msg->context);
}

static irqreturn_t
sport_err_handler(int irq, void *dev_id)
{
	struct bfin_sport_spi_master_data *drv_data = dev_id;
	u16 status;

	dev_dbg(drv_data->dev, "%s enter\n", __func__);
	status = bfin_read(&drv_data->regs->stat) & (TOVF | TUVF | ROVF | RUVF);

	if (status) {
		bfin_write(&drv_data->regs->stat, status);
		SSYNC();

		bfin_sport_spi_disable(drv_data);
		dev_err(drv_data->dev, "status error:%s%s%s%s\n",
			status & TOVF ? " TOVF" : "",
			status & TUVF ? " TUVF" : "",
			status & ROVF ? " ROVF" : "",
			status & RUVF ? " RUVF" : "");
	}

	return IRQ_HANDLED;
}

static void
bfin_sport_spi_pump_transfers(unsigned long data)
{
	struct bfin_sport_spi_master_data *drv_data = (void *)data;
	struct spi_message *message = NULL;
	struct spi_transfer *transfer = NULL;
	struct spi_transfer *previous = NULL;
	struct bfin_sport_spi_slave_data *chip = NULL;
	unsigned int bits_per_word;
	u32 tranf_success = 1;
	u32 transfer_speed;
	u8 full_duplex = 0;

	/* Get current state information */
	message = drv_data->cur_msg;
	transfer = drv_data->cur_transfer;
	chip = drv_data->cur_chip;

	if (transfer->speed_hz)
		transfer_speed = bfin_sport_hz_to_spi_baud(transfer->speed_hz);
	else
		transfer_speed = chip->baud;
	bfin_write(&drv_data->regs->tclkdiv, transfer_speed);
	SSYNC();

	/*
	 * if msg is error or done, report it back using complete() callback
	 */

	 /* Handle for abort */
	if (drv_data->state == ERROR_STATE) {
		dev_dbg(drv_data->dev, "transfer: we've hit an error\n");
		message->status = -EIO;
		bfin_sport_spi_giveback(drv_data);
		return;
	}

	/* Handle end of message */
	if (drv_data->state == DONE_STATE) {
		dev_dbg(drv_data->dev, "transfer: all done!\n");
		message->status = 0;
		bfin_sport_spi_giveback(drv_data);
		return;
	}

	/* Delay if requested at end of transfer */
	if (drv_data->state == RUNNING_STATE) {
		dev_dbg(drv_data->dev, "transfer: still running ...\n");
		previous = list_entry(transfer->transfer_list.prev,
				      struct spi_transfer, transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);
	}

	if (transfer->len == 0) {
		/* Move to next transfer of this msg */
		drv_data->state = bfin_sport_spi_next_transfer(drv_data);
		/* Schedule next transfer tasklet */
		tasklet_schedule(&drv_data->pump_transfers);
	}

	if (transfer->tx_buf != NULL) {
		drv_data->tx = (void *)transfer->tx_buf;
		drv_data->tx_end = drv_data->tx + transfer->len;
		dev_dbg(drv_data->dev, "tx_buf is %p, tx_end is %p\n",
			transfer->tx_buf, drv_data->tx_end);
	} else
		drv_data->tx = NULL;

	if (transfer->rx_buf != NULL) {
		full_duplex = transfer->tx_buf != NULL;
		drv_data->rx = transfer->rx_buf;
		drv_data->rx_end = drv_data->rx + transfer->len;
		dev_dbg(drv_data->dev, "rx_buf is %p, rx_end is %p\n",
			transfer->rx_buf, drv_data->rx_end);
	} else
		drv_data->rx = NULL;

	drv_data->cs_change = transfer->cs_change;

	/* Bits per word setup */
	bits_per_word = transfer->bits_per_word ? :
		message->spi->bits_per_word ? : 8;
	if (bits_per_word % 16 == 0)
		drv_data->ops = &bfin_sport_transfer_ops_u16;
	else
		drv_data->ops = &bfin_sport_transfer_ops_u8;
	bfin_write(&drv_data->regs->tcr2, bits_per_word - 1);
	bfin_write(&drv_data->regs->tfsdiv, bits_per_word - 1);
	bfin_write(&drv_data->regs->rcr2, bits_per_word - 1);

	drv_data->state = RUNNING_STATE;

	if (drv_data->cs_change)
		bfin_sport_spi_cs_active(chip);

	dev_dbg(drv_data->dev,
		"now pumping a transfer: width is %d, len is %d\n",
		bits_per_word, transfer->len);

	/* PIO mode write then read */
	dev_dbg(drv_data->dev, "doing IO transfer\n");

	bfin_sport_spi_enable(drv_data);
	if (full_duplex) {
		/* full duplex mode */
		BUG_ON((drv_data->tx_end - drv_data->tx) !=
		       (drv_data->rx_end - drv_data->rx));
		drv_data->ops->duplex(drv_data);

		if (drv_data->tx != drv_data->tx_end)
			tranf_success = 0;
	} else if (drv_data->tx != NULL) {
		/* write only half duplex */

		drv_data->ops->write(drv_data);

		if (drv_data->tx != drv_data->tx_end)
			tranf_success = 0;
	} else if (drv_data->rx != NULL) {
		/* read only half duplex */

		drv_data->ops->read(drv_data);
		if (drv_data->rx != drv_data->rx_end)
			tranf_success = 0;
	}
	bfin_sport_spi_disable(drv_data);

	if (!tranf_success) {
		dev_dbg(drv_data->dev, "IO write error!\n");
		drv_data->state = ERROR_STATE;
	} else {
		/* Update total byte transfered */
		message->actual_length += transfer->len;
		/* Move to next transfer of this msg */
		drv_data->state = bfin_sport_spi_next_transfer(drv_data);
		if (drv_data->cs_change)
			bfin_sport_spi_cs_deactive(chip);
	}

	/* Schedule next transfer tasklet */
	tasklet_schedule(&drv_data->pump_transfers);
}

/* pop a msg from queue and kick off real transfer */
static void
bfin_sport_spi_pump_messages(struct work_struct *work)
{
	struct bfin_sport_spi_master_data *drv_data;
	unsigned long flags;
	struct spi_message *next_msg;

	drv_data = container_of(work, struct bfin_sport_spi_master_data, pump_messages);

	/* Lock queue and check for queue work */
	spin_lock_irqsave(&drv_data->lock, flags);
	if (list_empty(&drv_data->queue) || !drv_data->run) {
		/* pumper kicked off but no work to do */
		drv_data->busy = 0;
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Make sure we are not already running a message */
	if (drv_data->cur_msg) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return;
	}

	/* Extract head of queue */
	next_msg = list_entry(drv_data->queue.next,
		struct spi_message, queue);

	drv_data->cur_msg = next_msg;

	/* Setup the SSP using the per chip configuration */
	drv_data->cur_chip = spi_get_ctldata(drv_data->cur_msg->spi);

	list_del_init(&drv_data->cur_msg->queue);

	/* Initialize message state */
	drv_data->cur_msg->state = START_STATE;
	drv_data->cur_transfer = list_entry(drv_data->cur_msg->transfers.next,
					    struct spi_transfer, transfer_list);
	bfin_sport_spi_restore_state(drv_data);
	dev_dbg(drv_data->dev, "got a message to pump, "
		"state is set to: baud %d, cs_gpio %i, ctl 0x%x\n",
		drv_data->cur_chip->baud, drv_data->cur_chip->cs_gpio,
		drv_data->cur_chip->ctl_reg);

	dev_dbg(drv_data->dev,
		"the first transfer len is %d\n",
		drv_data->cur_transfer->len);

	/* Mark as busy and launch transfers */
	tasklet_schedule(&drv_data->pump_transfers);

	drv_data->busy = 1;
	spin_unlock_irqrestore(&drv_data->lock, flags);
}

/*
 * got a msg to transfer, queue it in drv_data->queue.
 * And kick off message pumper
 */
static int
bfin_sport_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct bfin_sport_spi_master_data *drv_data = spi_master_get_devdata(spi->master);
	unsigned long flags;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (!drv_data->run) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -ESHUTDOWN;
	}

	msg->actual_length = 0;
	msg->status = -EINPROGRESS;
	msg->state = START_STATE;

	dev_dbg(&spi->dev, "adding an msg in transfer()\n");
	list_add_tail(&msg->queue, &drv_data->queue);

	if (drv_data->run && !drv_data->busy)
		queue_work(drv_data->workqueue, &drv_data->pump_messages);

	spin_unlock_irqrestore(&drv_data->lock, flags);

	return 0;
}

/* Called every time common spi devices change state */
static int
bfin_sport_spi_setup(struct spi_device *spi)
{
	struct bfin_sport_spi_slave_data *chip, *first = NULL;
	int ret;

	/* Only alloc (or use chip_info) on first setup */
	chip = spi_get_ctldata(spi);
	if (chip == NULL) {
		struct bfin5xx_spi_chip *chip_info;

		chip = first = kzalloc(sizeof(*chip), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		/* platform chip_info isn't required */
		chip_info = spi->controller_data;
		if (chip_info) {
			/*
			 * DITFS and TDTYPE are only thing we don't set, but
			 * they probably shouldn't be changed by people.
			 */
			if (chip_info->ctl_reg || chip_info->enable_dma) {
				ret = -EINVAL;
				dev_err(&spi->dev, "don't set ctl_reg/enable_dma fields");
				goto error;
			}
			chip->cs_chg_udelay = chip_info->cs_chg_udelay;
			chip->idle_tx_val = chip_info->idle_tx_val;
		}
	}

	if (spi->bits_per_word % 8) {
		dev_err(&spi->dev, "%d bits_per_word is not supported\n",
				spi->bits_per_word);
		ret = -EINVAL;
		goto error;
	}

	/* translate common spi framework into our register
	 * following configure contents are same for tx and rx.
	 */

	if (spi->mode & SPI_CPHA)
		chip->ctl_reg &= ~TCKFE;
	else
		chip->ctl_reg |= TCKFE;

	if (spi->mode & SPI_LSB_FIRST)
		chip->ctl_reg |= TLSBIT;
	else
		chip->ctl_reg &= ~TLSBIT;

	/* Sport in master mode */
	chip->ctl_reg |= ITCLK | ITFS | TFSR | LATFS | LTFS;

	chip->baud = bfin_sport_hz_to_spi_baud(spi->max_speed_hz);

	chip->cs_gpio = spi->chip_select;
	ret = gpio_request(chip->cs_gpio, spi->modalias);
	if (ret)
		goto error;

	dev_dbg(&spi->dev, "setup spi chip %s, width is %d\n",
			spi->modalias, spi->bits_per_word);
	dev_dbg(&spi->dev, "ctl_reg is 0x%x, GPIO is %i\n",
			chip->ctl_reg, spi->chip_select);

	spi_set_ctldata(spi, chip);

	bfin_sport_spi_cs_deactive(chip);

	return ret;

 error:
	kfree(first);
	return ret;
}

/*
 * callback for spi framework.
 * clean driver specific data
 */
static void
bfin_sport_spi_cleanup(struct spi_device *spi)
{
	struct bfin_sport_spi_slave_data *chip = spi_get_ctldata(spi);

	if (!chip)
		return;

	gpio_free(chip->cs_gpio);

	kfree(chip);
}

static int
bfin_sport_spi_init_queue(struct bfin_sport_spi_master_data *drv_data)
{
	INIT_LIST_HEAD(&drv_data->queue);
	spin_lock_init(&drv_data->lock);

	drv_data->run = false;
	drv_data->busy = 0;

	/* init transfer tasklet */
	tasklet_init(&drv_data->pump_transfers,
		     bfin_sport_spi_pump_transfers, (unsigned long)drv_data);

	/* init messages workqueue */
	INIT_WORK(&drv_data->pump_messages, bfin_sport_spi_pump_messages);
	drv_data->workqueue =
	    create_singlethread_workqueue(dev_name(drv_data->master->dev.parent));
	if (drv_data->workqueue == NULL)
		return -EBUSY;

	return 0;
}

static int
bfin_sport_spi_start_queue(struct bfin_sport_spi_master_data *drv_data)
{
	unsigned long flags;

	spin_lock_irqsave(&drv_data->lock, flags);

	if (drv_data->run || drv_data->busy) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		return -EBUSY;
	}

	drv_data->run = true;
	drv_data->cur_msg = NULL;
	drv_data->cur_transfer = NULL;
	drv_data->cur_chip = NULL;
	spin_unlock_irqrestore(&drv_data->lock, flags);

	queue_work(drv_data->workqueue, &drv_data->pump_messages);

	return 0;
}

static inline int
bfin_sport_spi_stop_queue(struct bfin_sport_spi_master_data *drv_data)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&drv_data->lock, flags);

	/*
	 * This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the drv_data->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead
	 */
	drv_data->run = false;
	while (!list_empty(&drv_data->queue) && drv_data->busy && limit--) {
		spin_unlock_irqrestore(&drv_data->lock, flags);
		msleep(10);
		spin_lock_irqsave(&drv_data->lock, flags);
	}

	if (!list_empty(&drv_data->queue) || drv_data->busy)
		status = -EBUSY;

	spin_unlock_irqrestore(&drv_data->lock, flags);

	return status;
}

static inline int
bfin_sport_spi_destroy_queue(struct bfin_sport_spi_master_data *drv_data)
{
	int status;

	status = bfin_sport_spi_stop_queue(drv_data);
	if (status)
		return status;

	destroy_workqueue(drv_data->workqueue);

	return 0;
}

static int __devinit
bfin_sport_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bfin5xx_spi_master *platform_info;
	struct spi_master *master;
	struct resource *res, *ires;
	struct bfin_sport_spi_master_data *drv_data;
	int status;

	platform_info = dev->platform_data;

	/* Allocate master with space for drv_data */
	master = spi_alloc_master(dev, sizeof(*master) + 16);
	if (!master) {
		dev_err(dev, "cannot alloc spi_master\n");
		return -ENOMEM;
	}

	drv_data = spi_master_get_devdata(master);
	drv_data->master = master;
	drv_data->dev = dev;
	drv_data->pin_req = platform_info->pin_req;

	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;
	master->bus_num = pdev->id;
	master->num_chipselect = platform_info->num_chipselect;
	master->cleanup = bfin_sport_spi_cleanup;
	master->setup = bfin_sport_spi_setup;
	master->transfer = bfin_sport_spi_transfer;

	/* Find and map our resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "cannot get IORESOURCE_MEM\n");
		status = -ENOENT;
		goto out_error_get_res;
	}

	drv_data->regs = ioremap(res->start, resource_size(res));
	if (drv_data->regs == NULL) {
		dev_err(dev, "cannot map registers\n");
		status = -ENXIO;
		goto out_error_ioremap;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		dev_err(dev, "cannot get IORESOURCE_IRQ\n");
		status = -ENODEV;
		goto out_error_get_ires;
	}
	drv_data->err_irq = ires->start;

	/* Initial and start queue */
	status = bfin_sport_spi_init_queue(drv_data);
	if (status) {
		dev_err(dev, "problem initializing queue\n");
		goto out_error_queue_alloc;
	}

	status = bfin_sport_spi_start_queue(drv_data);
	if (status) {
		dev_err(dev, "problem starting queue\n");
		goto out_error_queue_alloc;
	}

	status = request_irq(drv_data->err_irq, sport_err_handler,
		0, "sport_spi_err", drv_data);
	if (status) {
		dev_err(dev, "unable to request sport err irq\n");
		goto out_error_irq;
	}

	status = peripheral_request_list(drv_data->pin_req, DRV_NAME);
	if (status) {
		dev_err(dev, "requesting peripherals failed\n");
		goto out_error_peripheral;
	}

	/* Register with the SPI framework */
	platform_set_drvdata(pdev, drv_data);
	status = spi_register_master(master);
	if (status) {
		dev_err(dev, "problem registering spi master\n");
		goto out_error_master;
	}

	dev_info(dev, "%s, regs_base@%p\n", DRV_DESC, drv_data->regs);
	return 0;

 out_error_master:
	peripheral_free_list(drv_data->pin_req);
 out_error_peripheral:
	free_irq(drv_data->err_irq, drv_data);
 out_error_irq:
 out_error_queue_alloc:
	bfin_sport_spi_destroy_queue(drv_data);
 out_error_get_ires:
	iounmap(drv_data->regs);
 out_error_ioremap:
 out_error_get_res:
	spi_master_put(master);

	return status;
}

/* stop hardware and remove the driver */
static int __devexit
bfin_sport_spi_remove(struct platform_device *pdev)
{
	struct bfin_sport_spi_master_data *drv_data = platform_get_drvdata(pdev);
	int status = 0;

	if (!drv_data)
		return 0;

	/* Remove the queue */
	status = bfin_sport_spi_destroy_queue(drv_data);
	if (status)
		return status;

	/* Disable the SSP at the peripheral and SOC level */
	bfin_sport_spi_disable(drv_data);

	/* Disconnect from the SPI framework */
	spi_unregister_master(drv_data->master);

	peripheral_free_list(drv_data->pin_req);

	/* Prevent double remove */
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int
bfin_sport_spi_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bfin_sport_spi_master_data *drv_data = platform_get_drvdata(pdev);
	int status;

	status = bfin_sport_spi_stop_queue(drv_data);
	if (status)
		return status;

	/* stop hardware */
	bfin_sport_spi_disable(drv_data);

	return status;
}

static int
bfin_sport_spi_resume(struct platform_device *pdev)
{
	struct bfin_sport_spi_master_data *drv_data = platform_get_drvdata(pdev);
	int status;

	/* Enable the SPI interface */
	bfin_sport_spi_enable(drv_data);

	/* Start the queue running */
	status = bfin_sport_spi_start_queue(drv_data);
	if (status)
		dev_err(drv_data->dev, "problem resuming queue\n");

	return status;
}
#else
# define bfin_sport_spi_suspend NULL
# define bfin_sport_spi_resume  NULL
#endif

static struct platform_driver bfin_sport_spi_driver = {
	.driver	= {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe   = bfin_sport_spi_probe,
	.remove  = __devexit_p(bfin_sport_spi_remove),
	.suspend = bfin_sport_spi_suspend,
	.resume  = bfin_sport_spi_resume,
};
module_platform_driver(bfin_sport_spi_driver);
