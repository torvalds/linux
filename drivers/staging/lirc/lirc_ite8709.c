/*
 * LIRC driver for ITE8709 CIR port
 *
 * Copyright (C) 2008 Grégory Lardière <spmf2004-lirc@yahoo.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pnp.h>
#include <linux/io.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>

#define LIRC_DRIVER_NAME "lirc_ite8709"

#define BUF_CHUNK_SIZE	sizeof(int)
#define BUF_SIZE	(128*BUF_CHUNK_SIZE)

/*
 * The ITE8709 device seems to be the combination of IT8512 superIO chip and
 * a specific firmware running on the IT8512's embedded micro-controller.
 * In addition of the embedded micro-controller, the IT8512 chip contains a
 * CIR module and several other modules. A few modules are directly accessible
 * by the host CPU, but most of them are only accessible by the
 * micro-controller. The CIR module is only accessible by the micro-controller.
 * The battery-backed SRAM module is accessible by the host CPU and the
 * micro-controller. So one of the MC's firmware role is to act as a bridge
 * between the host CPU and the CIR module. The firmware implements a kind of
 * communication protocol using the SRAM module as a shared memory. The IT8512
 * specification is publicly available on ITE's web site, but the communication
 * protocol is not, so it was reverse-engineered.
 */

/* ITE8709 Registers addresses and values (reverse-engineered) */
#define ITE8709_MODE		0x1a
#define ITE8709_REG_ADR		0x1b
#define ITE8709_REG_VAL		0x1c
#define ITE8709_IIR		0x1e  /* Interrupt identification register */
#define ITE8709_RFSR		0x1f  /* Receiver FIFO status register */
#define ITE8709_FIFO_START	0x20

#define ITE8709_MODE_READY	0X00
#define ITE8709_MODE_WRITE	0X01
#define ITE8709_MODE_READ	0X02
#define ITE8709_IIR_RDAI	0x02  /* Receiver data available interrupt */
#define ITE8709_IIR_RFOI	0x04  /* Receiver FIFO overrun interrupt */
#define ITE8709_RFSR_MASK	0x3f  /* FIFO byte count mask */

/*
 * IT8512 CIR-module registers addresses and values
 * (from IT8512 E/F specification v0.4.1)
 */
#define IT8512_REG_MSTCR	0x01  /* Master control register */
#define IT8512_REG_IER		0x02  /* Interrupt enable register */
#define IT8512_REG_CFR		0x04  /* Carrier frequency register */
#define IT8512_REG_RCR		0x05  /* Receive control register */
#define IT8512_REG_BDLR		0x08  /* Baud rate divisor low byte register */
#define IT8512_REG_BDHR		0x09  /* Baud rate divisor high byte register */

#define IT8512_MSTCR_RESET	0x01  /* Reset registers to default value */
#define IT8512_MSTCR_FIFOCLR	0x02  /* Clear FIFO */
#define IT8512_MSTCR_FIFOTL_7	0x04  /* FIFO threshold level : 7 */
#define IT8512_MSTCR_FIFOTL_25	0x0c  /* FIFO threshold level : 25 */
#define IT8512_IER_RDAIE	0x02  /* Enable data interrupt request */
#define IT8512_IER_RFOIE	0x04  /* Enable FIFO overrun interrupt req */
#define IT8512_IER_IEC		0x80  /* Enable interrupt request */
#define IT8512_CFR_CF_36KHZ	0x09  /* Carrier freq : low speed, 36kHz */
#define IT8512_RCR_RXDCR_1	0x01  /* Demodulation carrier range : 1 */
#define IT8512_RCR_RXACT	0x08  /* Receiver active */
#define IT8512_RCR_RXEN		0x80  /* Receiver enable */
#define IT8512_BDR_6		6     /* Baud rate divisor : 6 */

/* Actual values used by this driver */
#define CFG_FIFOTL	IT8512_MSTCR_FIFOTL_25
#define CFG_CR_FREQ	IT8512_CFR_CF_36KHZ
#define CFG_DCR		IT8512_RCR_RXDCR_1
#define CFG_BDR		IT8512_BDR_6
#define CFG_TIMEOUT	100000 /* Rearm interrupt when a space is > 100 ms */

static int debug;

struct ite8709_device {
	int use_count;
	int io;
	int irq;
	spinlock_t hardware_lock;
	__u64 acc_pulse;
	__u64 acc_space;
	char lastbit;
	struct timeval last_tv;
	struct lirc_driver driver;
	struct tasklet_struct tasklet;
	char force_rearm;
	char rearmed;
	char device_busy;
};

#define dprintk(fmt, args...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG LIRC_DRIVER_NAME ": "	\
				fmt, ## args);			\
	} while (0)


static unsigned char ite8709_read(struct ite8709_device *dev,
					unsigned char port)
{
	outb(port, dev->io);
	return inb(dev->io+1);
}

static void ite8709_write(struct ite8709_device *dev, unsigned char port,
				unsigned char data)
{
	outb(port, dev->io);
	outb(data, dev->io+1);
}

static void ite8709_wait_device(struct ite8709_device *dev)
{
	int i = 0;
	/*
	 * loop until device tells it's ready to continue
	 * iterations count is usually ~750 but can sometimes achieve 13000
	 */
	for (i = 0; i < 15000; i++) {
		udelay(2);
		if (ite8709_read(dev, ITE8709_MODE) == ITE8709_MODE_READY)
			break;
	}
}

static void ite8709_write_register(struct ite8709_device *dev,
				unsigned char reg_adr, unsigned char reg_value)
{
	ite8709_wait_device(dev);

	ite8709_write(dev, ITE8709_REG_VAL, reg_value);
	ite8709_write(dev, ITE8709_REG_ADR, reg_adr);
	ite8709_write(dev, ITE8709_MODE, ITE8709_MODE_WRITE);
}

static void ite8709_init_hardware(struct ite8709_device *dev)
{
	spin_lock_irq(&dev->hardware_lock);
	dev->device_busy = 1;
	spin_unlock_irq(&dev->hardware_lock);

	ite8709_write_register(dev, IT8512_REG_BDHR, (CFG_BDR >> 8) & 0xff);
	ite8709_write_register(dev, IT8512_REG_BDLR, CFG_BDR & 0xff);
	ite8709_write_register(dev, IT8512_REG_CFR, CFG_CR_FREQ);
	ite8709_write_register(dev, IT8512_REG_IER,
			IT8512_IER_IEC | IT8512_IER_RFOIE | IT8512_IER_RDAIE);
	ite8709_write_register(dev, IT8512_REG_RCR, CFG_DCR);
	ite8709_write_register(dev, IT8512_REG_MSTCR,
					CFG_FIFOTL | IT8512_MSTCR_FIFOCLR);
	ite8709_write_register(dev, IT8512_REG_RCR,
				IT8512_RCR_RXEN | IT8512_RCR_RXACT | CFG_DCR);

	spin_lock_irq(&dev->hardware_lock);
	dev->device_busy = 0;
	spin_unlock_irq(&dev->hardware_lock);

	tasklet_enable(&dev->tasklet);
}

static void ite8709_drop_hardware(struct ite8709_device *dev)
{
	tasklet_disable(&dev->tasklet);

	spin_lock_irq(&dev->hardware_lock);
	dev->device_busy = 1;
	spin_unlock_irq(&dev->hardware_lock);

	ite8709_write_register(dev, IT8512_REG_RCR, 0);
	ite8709_write_register(dev, IT8512_REG_MSTCR,
				IT8512_MSTCR_RESET | IT8512_MSTCR_FIFOCLR);

	spin_lock_irq(&dev->hardware_lock);
	dev->device_busy = 0;
	spin_unlock_irq(&dev->hardware_lock);
}

static int ite8709_set_use_inc(void *data)
{
	struct ite8709_device *dev;
	dev = data;
	if (dev->use_count == 0)
		ite8709_init_hardware(dev);
	dev->use_count++;
	return 0;
}

static void ite8709_set_use_dec(void *data)
{
	struct ite8709_device *dev;
	dev = data;
	dev->use_count--;
	if (dev->use_count == 0)
		ite8709_drop_hardware(dev);
}

static void ite8709_add_read_queue(struct ite8709_device *dev, int flag,
				   __u64 val)
{
	int value;

	dprintk("add a %llu usec %s\n", val, flag ? "pulse" : "space");

	value = (val > PULSE_MASK) ? PULSE_MASK : val;
	if (flag)
		value |= PULSE_BIT;

	if (!lirc_buffer_full(dev->driver.rbuf)) {
		lirc_buffer_write(dev->driver.rbuf, (void *) &value);
		wake_up(&dev->driver.rbuf->wait_poll);
	}
}

static irqreturn_t ite8709_interrupt(int irq, void *dev_id)
{
	unsigned char data;
	int iir, rfsr, i;
	int fifo = 0;
	char bit;
	struct timeval curr_tv;

	/* Bit duration in microseconds */
	const unsigned long bit_duration = 1000000ul / (115200 / CFG_BDR);

	struct ite8709_device *dev;
	dev = dev_id;

	/*
	 * If device is busy, we simply discard data because we are in one of
	 * these two cases : shutting down or rearming the device, so this
	 * doesn't really matter and this avoids waiting too long in IRQ ctx
	 */
	spin_lock(&dev->hardware_lock);
	if (dev->device_busy) {
		spin_unlock(&dev->hardware_lock);
		return IRQ_RETVAL(IRQ_HANDLED);
	}

	iir = ite8709_read(dev, ITE8709_IIR);

	switch (iir) {
	case ITE8709_IIR_RFOI:
		dprintk("fifo overrun, scheduling forced rearm just in case\n");
		dev->force_rearm = 1;
		tasklet_schedule(&dev->tasklet);
		spin_unlock(&dev->hardware_lock);
		return IRQ_RETVAL(IRQ_HANDLED);

	case ITE8709_IIR_RDAI:
		rfsr = ite8709_read(dev, ITE8709_RFSR);
		fifo = rfsr & ITE8709_RFSR_MASK;
		if (fifo > 32)
			fifo = 32;
		dprintk("iir: 0x%x rfsr: 0x%x fifo: %d\n", iir, rfsr, fifo);

		if (dev->rearmed) {
			do_gettimeofday(&curr_tv);
			dev->acc_space += 1000000ull
				* (curr_tv.tv_sec - dev->last_tv.tv_sec)
				+ (curr_tv.tv_usec - dev->last_tv.tv_usec);
			dev->rearmed = 0;
		}
		for (i = 0; i < fifo; i++) {
			data = ite8709_read(dev, i+ITE8709_FIFO_START);
			data = ~data;
			/* Loop through */
			for (bit = 0; bit < 8; ++bit) {
				if ((data >> bit) & 1) {
					dev->acc_pulse += bit_duration;
					if (dev->lastbit == 0) {
						ite8709_add_read_queue(dev, 0,
							dev->acc_space);
						dev->acc_space = 0;
					}
				} else {
					dev->acc_space += bit_duration;
					if (dev->lastbit == 1) {
						ite8709_add_read_queue(dev, 1,
							dev->acc_pulse);
						dev->acc_pulse = 0;
					}
				}
				dev->lastbit = (data >> bit) & 1;
			}
		}
		ite8709_write(dev, ITE8709_RFSR, 0);

		if (dev->acc_space > CFG_TIMEOUT) {
			dprintk("scheduling rearm IRQ\n");
			do_gettimeofday(&dev->last_tv);
			dev->force_rearm = 0;
			tasklet_schedule(&dev->tasklet);
		}

		spin_unlock(&dev->hardware_lock);
		return IRQ_RETVAL(IRQ_HANDLED);

	default:
		/* not our irq */
		dprintk("unknown IRQ (shouldn't happen) !!\n");
		spin_unlock(&dev->hardware_lock);
		return IRQ_RETVAL(IRQ_NONE);
	}
}

static void ite8709_rearm_irq(unsigned long data)
{
	struct ite8709_device *dev;
	unsigned long flags;
	dev = (struct ite8709_device *) data;

	spin_lock_irqsave(&dev->hardware_lock, flags);
	dev->device_busy = 1;
	spin_unlock_irqrestore(&dev->hardware_lock, flags);

	if (dev->force_rearm || dev->acc_space > CFG_TIMEOUT) {
		dprintk("rearming IRQ\n");
		ite8709_write_register(dev, IT8512_REG_RCR,
						IT8512_RCR_RXACT | CFG_DCR);
		ite8709_write_register(dev, IT8512_REG_MSTCR,
					CFG_FIFOTL | IT8512_MSTCR_FIFOCLR);
		ite8709_write_register(dev, IT8512_REG_RCR,
				IT8512_RCR_RXEN | IT8512_RCR_RXACT | CFG_DCR);
		if (!dev->force_rearm)
			dev->rearmed = 1;
		dev->force_rearm = 0;
	}

	spin_lock_irqsave(&dev->hardware_lock, flags);
	dev->device_busy = 0;
	spin_unlock_irqrestore(&dev->hardware_lock, flags);
}

static int ite8709_cleanup(struct ite8709_device *dev, int stage, int errno,
				char *msg)
{
	if (msg != NULL)
		printk(KERN_ERR LIRC_DRIVER_NAME ": %s\n", msg);

	switch (stage) {
	case 6:
		if (dev->use_count > 0)
			ite8709_drop_hardware(dev);
	case 5:
		free_irq(dev->irq, dev);
	case 4:
		release_region(dev->io, 2);
	case 3:
		lirc_unregister_driver(dev->driver.minor);
	case 2:
		lirc_buffer_free(dev->driver.rbuf);
		kfree(dev->driver.rbuf);
	case 1:
		kfree(dev);
	case 0:
		;
	}

	return errno;
}

static int __devinit ite8709_pnp_probe(struct pnp_dev *dev,
					const struct pnp_device_id *dev_id)
{
	struct lirc_driver *driver;
	struct ite8709_device *ite8709_dev;
	int ret;

	/* Check resources validity */
	if (!pnp_irq_valid(dev, 0))
		return ite8709_cleanup(NULL, 0, -ENODEV, "invalid IRQ");
	if (!pnp_port_valid(dev, 2))
		return ite8709_cleanup(NULL, 0, -ENODEV, "invalid IO port");

	/* Allocate memory for device struct */
	ite8709_dev = kzalloc(sizeof(struct ite8709_device), GFP_KERNEL);
	if (ite8709_dev == NULL)
		return ite8709_cleanup(NULL, 0, -ENOMEM, "kzalloc failed");
	pnp_set_drvdata(dev, ite8709_dev);

	/* Initialize device struct */
	ite8709_dev->use_count = 0;
	ite8709_dev->irq = pnp_irq(dev, 0);
	ite8709_dev->io = pnp_port_start(dev, 2);
	ite8709_dev->hardware_lock =
		__SPIN_LOCK_UNLOCKED(ite8709_dev->hardware_lock);
	ite8709_dev->acc_pulse = 0;
	ite8709_dev->acc_space = 0;
	ite8709_dev->lastbit = 0;
	do_gettimeofday(&ite8709_dev->last_tv);
	tasklet_init(&ite8709_dev->tasklet, ite8709_rearm_irq,
							(long) ite8709_dev);
	ite8709_dev->force_rearm = 0;
	ite8709_dev->rearmed = 0;
	ite8709_dev->device_busy = 0;

	/* Initialize driver struct */
	driver = &ite8709_dev->driver;
	strcpy(driver->name, LIRC_DRIVER_NAME);
	driver->minor = -1;
	driver->code_length = sizeof(int) * 8;
	driver->sample_rate = 0;
	driver->features = LIRC_CAN_REC_MODE2;
	driver->data = ite8709_dev;
	driver->add_to_buf = NULL;
	driver->set_use_inc = ite8709_set_use_inc;
	driver->set_use_dec = ite8709_set_use_dec;
	driver->dev = &dev->dev;
	driver->owner = THIS_MODULE;

	/* Initialize LIRC buffer */
	driver->rbuf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL);
	if (!driver->rbuf)
		return ite8709_cleanup(ite8709_dev, 1, -ENOMEM,
				       "can't allocate lirc_buffer");
	if (lirc_buffer_init(driver->rbuf, BUF_CHUNK_SIZE, BUF_SIZE))
		return ite8709_cleanup(ite8709_dev, 1, -ENOMEM,
				       "lirc_buffer_init() failed");

	/* Register LIRC driver */
	ret = lirc_register_driver(driver);
	if (ret < 0)
		return ite8709_cleanup(ite8709_dev, 2, ret,
					"lirc_register_driver() failed");

	/* Reserve I/O port access */
	if (!request_region(ite8709_dev->io, 2, LIRC_DRIVER_NAME))
		return ite8709_cleanup(ite8709_dev, 3, -EBUSY,
						"i/o port already in use");

	/* Reserve IRQ line */
	ret = request_irq(ite8709_dev->irq, ite8709_interrupt, 0,
					LIRC_DRIVER_NAME, ite8709_dev);
	if (ret < 0)
		return ite8709_cleanup(ite8709_dev, 4, ret,
						"IRQ already in use");

	/* Initialize hardware */
	ite8709_drop_hardware(ite8709_dev); /* Shutdown hw until first use */

	printk(KERN_INFO LIRC_DRIVER_NAME ": device found : irq=%d io=0x%x\n",
					ite8709_dev->irq, ite8709_dev->io);

	return 0;
}

static void __devexit ite8709_pnp_remove(struct pnp_dev *dev)
{
	struct ite8709_device *ite8709_dev;
	ite8709_dev = pnp_get_drvdata(dev);

	ite8709_cleanup(ite8709_dev, 6, 0, NULL);

	printk(KERN_INFO LIRC_DRIVER_NAME ": device removed\n");
}

#ifdef CONFIG_PM
static int ite8709_pnp_suspend(struct pnp_dev *dev, pm_message_t state)
{
	struct ite8709_device *ite8709_dev;
	ite8709_dev = pnp_get_drvdata(dev);

	if (ite8709_dev->use_count > 0)
		ite8709_drop_hardware(ite8709_dev);

	return 0;
}

static int ite8709_pnp_resume(struct pnp_dev *dev)
{
	struct ite8709_device *ite8709_dev;
	ite8709_dev = pnp_get_drvdata(dev);

	if (ite8709_dev->use_count > 0)
		ite8709_init_hardware(ite8709_dev);

	return 0;
}
#else
#define ite8709_pnp_suspend NULL
#define ite8709_pnp_resume NULL
#endif

static const struct pnp_device_id pnp_dev_table[] = {
	{"ITE8709", 0},
	{}
};

MODULE_DEVICE_TABLE(pnp, pnp_dev_table);

static struct pnp_driver ite8709_pnp_driver = {
	.name           = LIRC_DRIVER_NAME,
	.probe          = ite8709_pnp_probe,
	.remove         = __devexit_p(ite8709_pnp_remove),
	.suspend        = ite8709_pnp_suspend,
	.resume         = ite8709_pnp_resume,
	.id_table       = pnp_dev_table,
};

static int __init ite8709_init_module(void)
{
	return pnp_register_driver(&ite8709_pnp_driver);
}
module_init(ite8709_init_module);

static void __exit ite8709_cleanup_module(void)
{
	pnp_unregister_driver(&ite8709_pnp_driver);
}
module_exit(ite8709_cleanup_module);

MODULE_DESCRIPTION("LIRC driver for ITE8709 CIR port");
MODULE_AUTHOR("Grégory Lardière");
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");
