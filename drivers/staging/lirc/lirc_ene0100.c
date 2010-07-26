/*
 * driver for ENE KB3926 B/C/D CIR (also known as ENE0100)
 *
 * Copyright (C) 2009 Maxim Levitsky <maximlevitsky@gmail.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include "lirc_ene0100.h"

static int sample_period = 75;
static int enable_idle = 1;
static int enable_learning;

static void ene_set_idle(struct ene_device *dev, int idle);
static void ene_set_inputs(struct ene_device *dev, int enable);

/* read a hardware register */
static u8 ene_hw_read_reg(struct ene_device *dev, u16 reg)
{
	outb(reg >> 8, dev->hw_io + ENE_ADDR_HI);
	outb(reg & 0xFF, dev->hw_io + ENE_ADDR_LO);
	return inb(dev->hw_io + ENE_IO);
}

/* write a hardware register */
static void ene_hw_write_reg(struct ene_device *dev, u16 reg, u8 value)
{
	outb(reg >> 8, dev->hw_io + ENE_ADDR_HI);
	outb(reg & 0xFF, dev->hw_io + ENE_ADDR_LO);
	outb(value, dev->hw_io + ENE_IO);
}

/* change specific bits in hardware register */
static void ene_hw_write_reg_mask(struct ene_device *dev,
				  u16 reg, u8 value, u8 mask)
{
	u8 regvalue;

	outb(reg >> 8, dev->hw_io + ENE_ADDR_HI);
	outb(reg & 0xFF, dev->hw_io + ENE_ADDR_LO);

	regvalue = inb(dev->hw_io + ENE_IO) & ~mask;
	regvalue |= (value & mask);
	outb(regvalue, dev->hw_io + ENE_IO);
}

/* read irq status and ack it */
static int ene_hw_irq_status(struct ene_device *dev, int *buffer_pointer)
{
	u8 irq_status;
	u8 fw_flags1, fw_flags2;

	fw_flags2 = ene_hw_read_reg(dev, ENE_FW2);

	if (buffer_pointer)
		*buffer_pointer = 4 * (fw_flags2 & ENE_FW2_BUF_HIGH);

	if (dev->hw_revision < ENE_HW_C) {
		irq_status = ene_hw_read_reg(dev, ENEB_IRQ_STATUS);

		if (!(irq_status & ENEB_IRQ_STATUS_IR))
			return 0;
		ene_hw_write_reg(dev, ENEB_IRQ_STATUS,
				 irq_status & ~ENEB_IRQ_STATUS_IR);

		/* rev B support only recieving */
		return ENE_IRQ_RX;
	}

	irq_status = ene_hw_read_reg(dev, ENEC_IRQ);

	if (!(irq_status & ENEC_IRQ_STATUS))
		return 0;

	/* original driver does that twice - a workaround ? */
	ene_hw_write_reg(dev, ENEC_IRQ, irq_status & ~ENEC_IRQ_STATUS);
	ene_hw_write_reg(dev, ENEC_IRQ, irq_status & ~ENEC_IRQ_STATUS);

	/* clear unknown flag in F8F9 */
	if (fw_flags2 & ENE_FW2_IRQ_CLR)
		ene_hw_write_reg(dev, ENE_FW2, fw_flags2 & ~ENE_FW2_IRQ_CLR);

	/* check if this is a TX interrupt */
	fw_flags1 = ene_hw_read_reg(dev, ENE_FW1);

	if (fw_flags1 & ENE_FW1_TXIRQ) {
		ene_hw_write_reg(dev, ENE_FW1, fw_flags1 & ~ENE_FW1_TXIRQ);
		return ENE_IRQ_TX;
	} else
		return ENE_IRQ_RX;
}

static int ene_hw_detect(struct ene_device *dev)
{
	u8 chip_major, chip_minor;
	u8 hw_revision, old_ver;
	u8 tmp;
	u8 fw_capabilities;

	tmp = ene_hw_read_reg(dev, ENE_HW_UNK);
	ene_hw_write_reg(dev, ENE_HW_UNK, tmp & ~ENE_HW_UNK_CLR);

	chip_major = ene_hw_read_reg(dev, ENE_HW_VER_MAJOR);
	chip_minor = ene_hw_read_reg(dev, ENE_HW_VER_MINOR);

	ene_hw_write_reg(dev, ENE_HW_UNK, tmp);
	hw_revision = ene_hw_read_reg(dev, ENE_HW_VERSION);
	old_ver = ene_hw_read_reg(dev, ENE_HW_VER_OLD);

	if (hw_revision == 0xFF) {

		ene_printk(KERN_WARNING, "device seems to be disabled\n");
		ene_printk(KERN_WARNING,
			"send a mail to lirc-list@lists.sourceforge.net\n");
		ene_printk(KERN_WARNING, "please attach output of acpidump\n");

		return -ENODEV;
	}

	if (chip_major == 0x33) {
		ene_printk(KERN_WARNING, "chips 0x33xx aren't supported yet\n");
		return -ENODEV;
	}

	if (chip_major == 0x39 && chip_minor == 0x26 && hw_revision == 0xC0) {
		dev->hw_revision = ENE_HW_C;
		ene_printk(KERN_WARNING,
		       "KB3926C detected, driver support is not complete!\n");

	} else if (old_ver == 0x24 && hw_revision == 0xC0) {
		dev->hw_revision = ENE_HW_B;
		ene_printk(KERN_NOTICE, "KB3926B detected\n");
	} else {
		dev->hw_revision = ENE_HW_D;
		ene_printk(KERN_WARNING,
			"unknown ENE chip detected, assuming KB3926D\n");
		ene_printk(KERN_WARNING, "driver support incomplete");

	}

	ene_printk(KERN_DEBUG, "chip is 0x%02x%02x - 0x%02x, 0x%02x\n",
		chip_major, chip_minor, old_ver, hw_revision);


	/* detect features hardware supports */

	if (dev->hw_revision < ENE_HW_C)
		return 0;

	fw_capabilities = ene_hw_read_reg(dev, ENE_FW2);

	dev->hw_gpio40_learning = fw_capabilities & ENE_FW2_GP40_AS_LEARN;
	dev->hw_learning_and_tx_capable = fw_capabilities & ENE_FW2_LEARNING;

	dev->hw_fan_as_normal_input = dev->hw_learning_and_tx_capable &&
	    fw_capabilities & ENE_FW2_FAN_AS_NRML_IN;

	ene_printk(KERN_NOTICE, "hardware features:\n");
	ene_printk(KERN_NOTICE,
		"learning and tx %s, gpio40_learn %s, fan_in %s\n",
	       dev->hw_learning_and_tx_capable ? "on" : "off",
	       dev->hw_gpio40_learning ? "on" : "off",
	       dev->hw_fan_as_normal_input ? "on" : "off");

	if (!dev->hw_learning_and_tx_capable && enable_learning)
		enable_learning = 0;

	if (dev->hw_learning_and_tx_capable) {
		ene_printk(KERN_WARNING,
		"Device supports transmitting, but the driver doesn't\n");
		ene_printk(KERN_WARNING,
		"due to lack of hardware to test against.\n");
		ene_printk(KERN_WARNING,
		"Send a mail to: lirc-list@lists.sourceforge.net\n");
	}
	return 0;
}

/* hardware initialization */
static int ene_hw_init(void *data)
{
	u8 reg_value;
	struct ene_device *dev = (struct ene_device *)data;
	dev->in_use = 1;

	if (dev->hw_revision < ENE_HW_C) {
		ene_hw_write_reg(dev, ENEB_IRQ, dev->irq << 1);
		ene_hw_write_reg(dev, ENEB_IRQ_UNK1, 0x01);
	} else {
		reg_value = ene_hw_read_reg(dev, ENEC_IRQ) & 0xF0;
		reg_value |= ENEC_IRQ_UNK_EN;
		reg_value &= ~ENEC_IRQ_STATUS;
		reg_value |= (dev->irq & ENEC_IRQ_MASK);
		ene_hw_write_reg(dev, ENEC_IRQ, reg_value);
		ene_hw_write_reg(dev, ENE_TX_UNK1, 0x63);
	}

	ene_hw_write_reg(dev, ENE_CIR_CONF2, 0x00);
	ene_set_inputs(dev, enable_learning);

	/* set sampling period */
	ene_hw_write_reg(dev, ENE_CIR_SAMPLE_PERIOD, sample_period);

	/* ack any pending irqs - just in case */
	ene_hw_irq_status(dev, NULL);

	/* enter idle mode */
	ene_set_idle(dev, 1);

	/* enable firmware bits */
	ene_hw_write_reg_mask(dev, ENE_FW1,
			      ENE_FW1_ENABLE | ENE_FW1_IRQ,
			      ENE_FW1_ENABLE | ENE_FW1_IRQ);
	/* clear stats */
	dev->sample = 0;
	return 0;
}

/* this enables gpio40 signal, used if connected to wide band input*/
static void ene_enable_gpio40(struct ene_device *dev, int enable)
{
	ene_hw_write_reg_mask(dev, ENE_CIR_CONF1, enable ?
			      0 : ENE_CIR_CONF2_GPIO40DIS,
			      ENE_CIR_CONF2_GPIO40DIS);
}

/* this enables the classic sampler */
static void ene_enable_normal_recieve(struct ene_device *dev, int enable)
{
	ene_hw_write_reg(dev, ENE_CIR_CONF1, enable ? ENE_CIR_CONF1_ADC_ON : 0);
}

/* this enables recieve via fan input */
static void ene_enable_fan_recieve(struct ene_device *dev, int enable)
{
	if (!enable)
		ene_hw_write_reg(dev, ENE_FAN_AS_IN1, 0);
	else {
		ene_hw_write_reg(dev, ENE_FAN_AS_IN1, ENE_FAN_AS_IN1_EN);
		ene_hw_write_reg(dev, ENE_FAN_AS_IN2, ENE_FAN_AS_IN2_EN);
	}
	dev->fan_input_inuse = enable;
}

/* determine which input to use*/
static void ene_set_inputs(struct ene_device *dev, int learning_enable)
{
	ene_enable_normal_recieve(dev, 1);

	/* old hardware doesn't support learning mode for sure */
	if (dev->hw_revision <= ENE_HW_B)
		return;

	/* reciever not learning capable, still set gpio40 correctly */
	if (!dev->hw_learning_and_tx_capable) {
		ene_enable_gpio40(dev, !dev->hw_gpio40_learning);
		return;
	}

	/* enable learning mode */
	if (learning_enable) {
		ene_enable_gpio40(dev, dev->hw_gpio40_learning);

		/* fan input is not used for learning */
		if (dev->hw_fan_as_normal_input)
			ene_enable_fan_recieve(dev, 0);

	/* disable learning mode */
	} else {
		if (dev->hw_fan_as_normal_input) {
			ene_enable_fan_recieve(dev, 1);
			ene_enable_normal_recieve(dev, 0);
		} else
			ene_enable_gpio40(dev, !dev->hw_gpio40_learning);
	}

	/* set few additional settings for this mode */
	ene_hw_write_reg_mask(dev, ENE_CIR_CONF1, learning_enable ?
			      ENE_CIR_CONF1_LEARN1 : 0, ENE_CIR_CONF1_LEARN1);

	ene_hw_write_reg_mask(dev, ENE_CIR_CONF2, learning_enable ?
			      ENE_CIR_CONF2_LEARN2 : 0, ENE_CIR_CONF2_LEARN2);
}

/* deinitialization */
static void ene_hw_deinit(void *data)
{
	struct ene_device *dev = (struct ene_device *)data;

	/* disable samplers */
	ene_enable_normal_recieve(dev, 0);

	if (dev->hw_fan_as_normal_input)
		ene_enable_fan_recieve(dev, 0);

	/* disable hardware IRQ and firmware flag */
	ene_hw_write_reg_mask(dev, ENE_FW1, 0, ENE_FW1_ENABLE | ENE_FW1_IRQ);

	ene_set_idle(dev, 1);
	dev->in_use = 0;
}

/*  sends current sample to userspace */
static void send_sample(struct ene_device *dev)
{
	int value = abs(dev->sample) & PULSE_MASK;

	if (dev->sample > 0)
		value |= PULSE_BIT;

	if (!lirc_buffer_full(dev->lirc_driver->rbuf)) {
		lirc_buffer_write(dev->lirc_driver->rbuf, (void *)&value);
		wake_up(&dev->lirc_driver->rbuf->wait_poll);
	}
	dev->sample = 0;
}

/*  this updates current sample */
static void update_sample(struct ene_device *dev, int sample)
{
	if (!dev->sample)
		dev->sample = sample;
	else if (same_sign(dev->sample, sample))
		dev->sample += sample;
	else {
		send_sample(dev);
		dev->sample = sample;
	}
}

/* enable or disable idle mode */
static void ene_set_idle(struct ene_device *dev, int idle)
{
	struct timeval now;
	int disable = idle && enable_idle && (dev->hw_revision < ENE_HW_C);

	ene_hw_write_reg_mask(dev, ENE_CIR_SAMPLE_PERIOD,
			      disable ? 0 : ENE_CIR_SAMPLE_OVERFLOW,
			      ENE_CIR_SAMPLE_OVERFLOW);
	dev->idle = idle;

	/* remember when we have entered the idle mode */
	if (idle) {
		do_gettimeofday(&dev->gap_start);
		return;
	}

	/* send the gap between keypresses now */
	do_gettimeofday(&now);

	if (now.tv_sec - dev->gap_start.tv_sec > 16)
		dev->sample = space(PULSE_MASK);
	else
		dev->sample = dev->sample +
		    space(1000000ull * (now.tv_sec - dev->gap_start.tv_sec))
		    + space(now.tv_usec - dev->gap_start.tv_usec);

	if (abs(dev->sample) > PULSE_MASK)
		dev->sample = space(PULSE_MASK);
	send_sample(dev);
}

/* interrupt handler */
static irqreturn_t ene_hw_irq(int irq, void *data)
{
	u16 hw_value;
	int i, hw_sample;
	int space;
	int buffer_pointer;
	int irq_status;

	struct ene_device *dev = (struct ene_device *)data;
	irq_status = ene_hw_irq_status(dev, &buffer_pointer);

	if (!irq_status)
		return IRQ_NONE;

	/* TODO: only RX for now */
	if (irq_status == ENE_IRQ_TX)
		return IRQ_HANDLED;

	for (i = 0; i < ENE_SAMPLES_SIZE; i++) {

		hw_value = ene_hw_read_reg(dev,
				ENE_SAMPLE_BUFFER + buffer_pointer + i);

		if (dev->fan_input_inuse) {
			/* read high part of the sample */
			hw_value |= ene_hw_read_reg(dev,
			    ENE_SAMPLE_BUFFER_FAN + buffer_pointer + i) << 8;

			/* test for _space_ bit */
			space = !(hw_value & ENE_FAN_SMPL_PULS_MSK);

			/* clear space bit, and other unused bits */
			hw_value &= ENE_FAN_VALUE_MASK;
			hw_sample = hw_value * ENE_SAMPLE_PERIOD_FAN;

		} else {
			space = hw_value & ENE_SAMPLE_SPC_MASK;
			hw_value &= ENE_SAMPLE_VALUE_MASK;
			hw_sample = hw_value * sample_period;
		}

		/* no more data */
		if (!(hw_value))
			break;

		if (space)
			hw_sample *= -1;

		/* overflow sample recieved, handle it */

		if (!dev->fan_input_inuse && hw_value == ENE_SAMPLE_OVERFLOW) {

			if (dev->idle)
				continue;

			if (dev->sample > 0 || abs(dev->sample) <= ENE_MAXGAP)
				update_sample(dev, hw_sample);
			else
				ene_set_idle(dev, 1);

			continue;
		}

		/* normal first sample recieved */
		if (!dev->fan_input_inuse && dev->idle) {
			ene_set_idle(dev, 0);

			/* discard first recieved value, its random
			   since its the time signal was off before
			   first pulse if idle mode is enabled, HW
			   does that for us */

			if (!enable_idle)
				continue;
		}
		update_sample(dev, hw_sample);
		send_sample(dev);
	}
	return IRQ_HANDLED;
}

static int ene_probe(struct pnp_dev *pnp_dev,
		     const struct pnp_device_id *dev_id)
{
	struct ene_device *dev;
	struct lirc_driver *lirc_driver;
	int error = -ENOMEM;

	dev = kzalloc(sizeof(struct ene_device), GFP_KERNEL);

	if (!dev)
		goto err1;

	dev->pnp_dev = pnp_dev;
	pnp_set_drvdata(pnp_dev, dev);


	/* prepare lirc interface */
	error = -ENOMEM;
	lirc_driver = kzalloc(sizeof(struct lirc_driver), GFP_KERNEL);

	if (!lirc_driver)
		goto err2;

	dev->lirc_driver = lirc_driver;

	strcpy(lirc_driver->name, ENE_DRIVER_NAME);
	lirc_driver->minor = -1;
	lirc_driver->code_length = sizeof(int) * 8;
	lirc_driver->features = LIRC_CAN_REC_MODE2;
	lirc_driver->data = dev;
	lirc_driver->set_use_inc = ene_hw_init;
	lirc_driver->set_use_dec = ene_hw_deinit;
	lirc_driver->dev = &pnp_dev->dev;
	lirc_driver->owner = THIS_MODULE;

	lirc_driver->rbuf = kzalloc(sizeof(struct lirc_buffer), GFP_KERNEL);

	if (!lirc_driver->rbuf)
		goto err3;

	if (lirc_buffer_init(lirc_driver->rbuf, sizeof(int), sizeof(int) * 256))
		goto err4;

	error = -ENODEV;
	if (lirc_register_driver(lirc_driver))
		goto err5;

	/* validate resources */
	if (!pnp_port_valid(pnp_dev, 0) ||
	    pnp_port_len(pnp_dev, 0) < ENE_MAX_IO)
		goto err6;

	if (!pnp_irq_valid(pnp_dev, 0))
		goto err6;

	dev->hw_io = pnp_port_start(pnp_dev, 0);
	dev->irq = pnp_irq(pnp_dev, 0);

	/* claim the resources */
	error = -EBUSY;
	if (!request_region(dev->hw_io, ENE_MAX_IO, ENE_DRIVER_NAME))
		goto err6;

	if (request_irq(dev->irq, ene_hw_irq,
			IRQF_SHARED, ENE_DRIVER_NAME, (void *)dev))
		goto err7;

	/* detect hardware version and features */
	error = ene_hw_detect(dev);
	if (error)
		goto err8;

	ene_printk(KERN_NOTICE, "driver has been succesfully loaded\n");
	return 0;

err8:
	free_irq(dev->irq, dev);
err7:
	release_region(dev->hw_io, ENE_MAX_IO);
err6:
	lirc_unregister_driver(lirc_driver->minor);
err5:
	lirc_buffer_free(lirc_driver->rbuf);
err4:
	kfree(lirc_driver->rbuf);
err3:
	kfree(lirc_driver);
err2:
	kfree(dev);
err1:
	return error;
}

static void ene_remove(struct pnp_dev *pnp_dev)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	ene_hw_deinit(dev);
	free_irq(dev->irq, dev);
	release_region(dev->hw_io, ENE_MAX_IO);
	lirc_unregister_driver(dev->lirc_driver->minor);
	lirc_buffer_free(dev->lirc_driver->rbuf);
	kfree(dev->lirc_driver);
	kfree(dev);
}

#ifdef CONFIG_PM

/* TODO: make 'wake on IR' configurable and add .shutdown */
/* currently impossible due to lack of kernel support */

static int ene_suspend(struct pnp_dev *pnp_dev, pm_message_t state)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	ene_hw_write_reg_mask(dev, ENE_FW1, ENE_FW1_WAKE, ENE_FW1_WAKE);
	return 0;
}

static int ene_resume(struct pnp_dev *pnp_dev)
{
	struct ene_device *dev = pnp_get_drvdata(pnp_dev);
	if (dev->in_use)
		ene_hw_init(dev);

	ene_hw_write_reg_mask(dev, ENE_FW1, 0, ENE_FW1_WAKE);
	return 0;
}

#endif

static const struct pnp_device_id ene_ids[] = {
	{.id = "ENE0100",},
	{},
};

static struct pnp_driver ene_driver = {
	.name = ENE_DRIVER_NAME,
	.id_table = ene_ids,
	.flags = PNP_DRIVER_RES_DO_NOT_CHANGE,

	.probe = ene_probe,
	.remove = __devexit_p(ene_remove),

#ifdef CONFIG_PM
	.suspend = ene_suspend,
	.resume = ene_resume,
#endif
};

static int __init ene_init(void)
{
	if (sample_period < 5) {
		ene_printk(KERN_ERR, "sample period must be at\n");
		ene_printk(KERN_ERR, "least 5 us, (at least 30 recommended)\n");
		return -EINVAL;
	}
	return pnp_register_driver(&ene_driver);
}

static void ene_exit(void)
{
	pnp_unregister_driver(&ene_driver);
}

module_param(sample_period, int, S_IRUGO);
MODULE_PARM_DESC(sample_period, "Hardware sample period (75 us default)");

module_param(enable_idle, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(enable_idle,
	"Enables turning off signal sampling after long inactivity time; "
	"if disabled might help detecting input signal (default: enabled)");

module_param(enable_learning, bool, S_IRUGO);
MODULE_PARM_DESC(enable_learning, "Use wide band (learning) reciever");

MODULE_DEVICE_TABLE(pnp, ene_ids);
MODULE_DESCRIPTION
    ("LIRC driver for KB3926B/KB3926C/KB3926D (aka ENE0100) CIR port");
MODULE_AUTHOR("Maxim Levitsky");
MODULE_LICENSE("GPL");

module_init(ene_init);
module_exit(ene_exit);
