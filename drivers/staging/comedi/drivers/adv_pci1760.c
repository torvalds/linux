// SPDX-License-Identifier: GPL-2.0+
/*
 * COMEDI driver for the Advantech PCI-1760
 * Copyright (C) 2015 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on the pci1760 support in the adv_pci_dio driver written by:
 *	Michal Dobes <dobes@tesnet.cz>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: adv_pci1760
 * Description: Advantech PCI-1760 Relay & Isolated Digital Input Card
 * Devices: [Advantech] PCI-1760 (adv_pci1760)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Fri, 13 Nov 2015 12:34:00 -0700
 * Status: untested
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>

#include "../comedi_pci.h"

/*
 * PCI-1760 Register Map
 *
 * Outgoing Mailbox Bytes
 * OMB3: Not used (must be 0)
 * OMB2: The command code to the PCI-1760
 * OMB1: The hi byte of the parameter for the command in OMB2
 * OMB0: The lo byte of the parameter for the command in OMB2
 *
 * Incoming Mailbox Bytes
 * IMB3: The Isolated Digital Input status (updated every 100us)
 * IMB2: The current command (matches OMB2 when command is successful)
 * IMB1: The hi byte of the feedback data for the command in OMB2
 * IMB0: The lo byte of the feedback data for the command in OMB2
 *
 * Interrupt Control/Status
 * INTCSR3: Not used (must be 0)
 * INTCSR2: The interrupt status (read only)
 * INTCSR1: Interrupt enable/disable
 * INTCSR0: Not used (must be 0)
 */
#define PCI1760_OMB_REG(x)		(0x0c + (x))
#define PCI1760_IMB_REG(x)		(0x1c + (x))
#define PCI1760_INTCSR_REG(x)		(0x38 + (x))
#define PCI1760_INTCSR1_IRQ_ENA		BIT(5)
#define PCI1760_INTCSR2_OMB_IRQ		BIT(0)
#define PCI1760_INTCSR2_IMB_IRQ		BIT(1)
#define PCI1760_INTCSR2_IRQ_STATUS	BIT(6)
#define PCI1760_INTCSR2_IRQ_ASSERTED	BIT(7)

/* PCI-1760 command codes */
#define PCI1760_CMD_CLR_IMB2		0x00	/* Clears IMB2 */
#define PCI1760_CMD_SET_DO		0x01	/* Set output state */
#define PCI1760_CMD_GET_DO		0x02	/* Read output status */
#define PCI1760_CMD_GET_STATUS		0x03	/* Read current status */
#define PCI1760_CMD_GET_FW_VER		0x0e	/* Read firware version */
#define PCI1760_CMD_GET_HW_VER		0x0f	/* Read hardware version */
#define PCI1760_CMD_SET_PWM_HI(x)	(0x10 + (x) * 2) /* Set "hi" period */
#define PCI1760_CMD_SET_PWM_LO(x)	(0x11 + (x) * 2) /* Set "lo" period */
#define PCI1760_CMD_SET_PWM_CNT(x)	(0x14 + (x)) /* Set burst count */
#define PCI1760_CMD_ENA_PWM		0x1f	/* Enable PWM outputs */
#define PCI1760_CMD_ENA_FILT		0x20	/* Enable input filter */
#define PCI1760_CMD_ENA_PAT_MATCH	0x21	/* Enable input pattern match */
#define PCI1760_CMD_SET_PAT_MATCH	0x22	/* Set input pattern match */
#define PCI1760_CMD_ENA_RISE_EDGE	0x23	/* Enable input rising edge */
#define PCI1760_CMD_ENA_FALL_EDGE	0x24	/* Enable input falling edge */
#define PCI1760_CMD_ENA_CNT		0x28	/* Enable counter */
#define PCI1760_CMD_RST_CNT		0x29	/* Reset counter */
#define PCI1760_CMD_ENA_CNT_OFLOW	0x2a	/* Enable counter overflow */
#define PCI1760_CMD_ENA_CNT_MATCH	0x2b	/* Enable counter match */
#define PCI1760_CMD_SET_CNT_EDGE	0x2c	/* Set counter edge */
#define PCI1760_CMD_GET_CNT		0x2f	/* Reads counter value */
#define PCI1760_CMD_SET_HI_SAMP(x)	(0x30 + (x)) /* Set "hi" sample time */
#define PCI1760_CMD_SET_LO_SAMP(x)	(0x38 + (x)) /* Set "lo" sample time */
#define PCI1760_CMD_SET_CNT(x)		(0x40 + (x)) /* Set counter reset val */
#define PCI1760_CMD_SET_CNT_MATCH(x)	(0x48 + (x)) /* Set counter match val */
#define PCI1760_CMD_GET_INT_FLAGS	0x60	/* Read interrupt flags */
#define PCI1760_CMD_GET_INT_FLAGS_MATCH	BIT(0)
#define PCI1760_CMD_GET_INT_FLAGS_COS	BIT(1)
#define PCI1760_CMD_GET_INT_FLAGS_OFLOW	BIT(2)
#define PCI1760_CMD_GET_OS		0x61	/* Read edge change flags */
#define PCI1760_CMD_GET_CNT_STATUS	0x62	/* Read counter oflow/match */

#define PCI1760_CMD_TIMEOUT		250	/* 250 usec timeout */
#define PCI1760_CMD_RETRIES		3	/* limit number of retries */

#define PCI1760_PWM_TIMEBASE		100000	/* 1 unit = 100 usec */

static int pci1760_send_cmd(struct comedi_device *dev,
			    unsigned char cmd, unsigned short val)
{
	unsigned long timeout;

	/* send the command and parameter */
	outb(val & 0xff, dev->iobase + PCI1760_OMB_REG(0));
	outb((val >> 8) & 0xff, dev->iobase + PCI1760_OMB_REG(1));
	outb(cmd, dev->iobase + PCI1760_OMB_REG(2));
	outb(0, dev->iobase + PCI1760_OMB_REG(3));

	/* datasheet says to allow up to 250 usec for the command to complete */
	timeout = jiffies + usecs_to_jiffies(PCI1760_CMD_TIMEOUT);
	do {
		if (inb(dev->iobase + PCI1760_IMB_REG(2)) == cmd) {
			/* command success; return the feedback data */
			return inb(dev->iobase + PCI1760_IMB_REG(0)) |
			       (inb(dev->iobase + PCI1760_IMB_REG(1)) << 8);
		}
		cpu_relax();
	} while (time_before(jiffies, timeout));

	return -EBUSY;
}

static int pci1760_cmd(struct comedi_device *dev,
		       unsigned char cmd, unsigned short val)
{
	int repeats;
	int ret;

	/* send PCI1760_CMD_CLR_IMB2 between identical commands */
	if (inb(dev->iobase + PCI1760_IMB_REG(2)) == cmd) {
		ret = pci1760_send_cmd(dev, PCI1760_CMD_CLR_IMB2, 0);
		if (ret < 0) {
			/* timeout? try it once more */
			ret = pci1760_send_cmd(dev, PCI1760_CMD_CLR_IMB2, 0);
			if (ret < 0)
				return -ETIMEDOUT;
		}
	}

	/* datasheet says to keep retrying the command */
	for (repeats = 0; repeats < PCI1760_CMD_RETRIES; repeats++) {
		ret = pci1760_send_cmd(dev, cmd, val);
		if (ret >= 0)
			return ret;
	}

	/* command failed! */
	return -ETIMEDOUT;
}

static int pci1760_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + PCI1760_IMB_REG(3));

	return insn->n;
}

static int pci1760_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int ret;

	if (comedi_dio_update_state(s, data)) {
		ret = pci1760_cmd(dev, PCI1760_CMD_SET_DO, s->state);
		if (ret < 0)
			return ret;
	}

	data[1] = s->state;

	return insn->n;
}

static int pci1760_pwm_ns_to_div(unsigned int flags, unsigned int ns)
{
	unsigned int divisor;

	switch (flags) {
	case CMDF_ROUND_NEAREST:
		divisor = DIV_ROUND_CLOSEST(ns, PCI1760_PWM_TIMEBASE);
		break;
	case CMDF_ROUND_UP:
		divisor = DIV_ROUND_UP(ns, PCI1760_PWM_TIMEBASE);
		break;
	case CMDF_ROUND_DOWN:
		divisor = ns / PCI1760_PWM_TIMEBASE;
		break;
	default:
		return -EINVAL;
	}

	if (divisor < 1)
		divisor = 1;
	if (divisor > 0xffff)
		divisor = 0xffff;

	return divisor;
}

static int pci1760_pwm_enable(struct comedi_device *dev,
			      unsigned int chan, bool enable)
{
	int ret;

	ret = pci1760_cmd(dev, PCI1760_CMD_GET_STATUS, PCI1760_CMD_ENA_PWM);
	if (ret < 0)
		return ret;

	if (enable)
		ret |= BIT(chan);
	else
		ret &= ~BIT(chan);

	return pci1760_cmd(dev, PCI1760_CMD_ENA_PWM, ret);
}

static int pci1760_pwm_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int hi_div;
	int lo_div;
	int ret;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		ret = pci1760_pwm_enable(dev, chan, false);
		if (ret < 0)
			return ret;

		if (data[1] > 0xffff)
			return -EINVAL;
		ret = pci1760_cmd(dev, PCI1760_CMD_SET_PWM_CNT(chan), data[1]);
		if (ret < 0)
			return ret;

		ret = pci1760_pwm_enable(dev, chan, true);
		if (ret < 0)
			return ret;
		break;
	case INSN_CONFIG_DISARM:
		ret = pci1760_pwm_enable(dev, chan, false);
		if (ret < 0)
			return ret;
		break;
	case INSN_CONFIG_PWM_OUTPUT:
		ret = pci1760_pwm_enable(dev, chan, false);
		if (ret < 0)
			return ret;

		hi_div = pci1760_pwm_ns_to_div(data[1], data[2]);
		lo_div = pci1760_pwm_ns_to_div(data[3], data[4]);
		if (hi_div < 0 || lo_div < 0)
			return -EINVAL;
		if ((hi_div * PCI1760_PWM_TIMEBASE) != data[2] ||
		    (lo_div * PCI1760_PWM_TIMEBASE) != data[4]) {
			data[2] = hi_div * PCI1760_PWM_TIMEBASE;
			data[4] = lo_div * PCI1760_PWM_TIMEBASE;
			return -EAGAIN;
		}
		ret = pci1760_cmd(dev, PCI1760_CMD_SET_PWM_HI(chan), hi_div);
		if (ret < 0)
			return ret;
		ret = pci1760_cmd(dev, PCI1760_CMD_SET_PWM_LO(chan), lo_div);
		if (ret < 0)
			return ret;
		break;
	case INSN_CONFIG_GET_PWM_OUTPUT:
		hi_div = pci1760_cmd(dev, PCI1760_CMD_GET_STATUS,
				     PCI1760_CMD_SET_PWM_HI(chan));
		lo_div = pci1760_cmd(dev, PCI1760_CMD_GET_STATUS,
				     PCI1760_CMD_SET_PWM_LO(chan));
		if (hi_div < 0 || lo_div < 0)
			return -ETIMEDOUT;

		data[1] = hi_div * PCI1760_PWM_TIMEBASE;
		data[2] = lo_div * PCI1760_PWM_TIMEBASE;
		break;
	case INSN_CONFIG_GET_PWM_STATUS:
		ret = pci1760_cmd(dev, PCI1760_CMD_GET_STATUS,
				  PCI1760_CMD_ENA_PWM);
		if (ret < 0)
			return ret;

		data[1] = (ret & BIT(chan)) ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static void pci1760_reset(struct comedi_device *dev)
{
	int i;

	/* disable interrupts (intcsr2 is read-only) */
	outb(0, dev->iobase + PCI1760_INTCSR_REG(0));
	outb(0, dev->iobase + PCI1760_INTCSR_REG(1));
	outb(0, dev->iobase + PCI1760_INTCSR_REG(3));

	/* disable counters */
	pci1760_cmd(dev, PCI1760_CMD_ENA_CNT, 0);

	/* disable overflow interrupts */
	pci1760_cmd(dev, PCI1760_CMD_ENA_CNT_OFLOW, 0);

	/* disable match */
	pci1760_cmd(dev, PCI1760_CMD_ENA_CNT_MATCH, 0);

	/* set match and counter reset values */
	for (i = 0; i < 8; i++) {
		pci1760_cmd(dev, PCI1760_CMD_SET_CNT_MATCH(i), 0x8000);
		pci1760_cmd(dev, PCI1760_CMD_SET_CNT(i), 0x0000);
	}

	/* reset counters to reset values */
	pci1760_cmd(dev, PCI1760_CMD_RST_CNT, 0xff);

	/* set counter count edges */
	pci1760_cmd(dev, PCI1760_CMD_SET_CNT_EDGE, 0);

	/* disable input filters */
	pci1760_cmd(dev, PCI1760_CMD_ENA_FILT, 0);

	/* disable pattern matching */
	pci1760_cmd(dev, PCI1760_CMD_ENA_PAT_MATCH, 0);

	/* set pattern match value */
	pci1760_cmd(dev, PCI1760_CMD_SET_PAT_MATCH, 0);
}

static int pci1760_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 0);

	pci1760_reset(dev);

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci1760_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci1760_do_insn_bits;

	/* get the current state of the outputs */
	ret = pci1760_cmd(dev, PCI1760_CMD_GET_DO, 0);
	if (ret < 0)
		return ret;
	s->state	= ret;

	/* PWM subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_PWM;
	s->subdev_flags	= SDF_PWM_COUNTER;
	s->n_chan	= 2;
	s->insn_config	= pci1760_pwm_insn_config;

	/* Counter subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_UNUSED;

	return 0;
}

static struct comedi_driver pci1760_driver = {
	.driver_name	= "adv_pci1760",
	.module		= THIS_MODULE,
	.auto_attach	= pci1760_auto_attach,
	.detach		= comedi_pci_detach,
};

static int pci1760_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &pci1760_driver, id->driver_data);
}

static const struct pci_device_id pci1760_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADVANTECH, 0x1760) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, pci1760_pci_table);

static struct pci_driver pci1760_pci_driver = {
	.name		= "adv_pci1760",
	.id_table	= pci1760_pci_table,
	.probe		= pci1760_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(pci1760_driver, pci1760_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Advantech PCI-1760");
MODULE_LICENSE("GPL");
