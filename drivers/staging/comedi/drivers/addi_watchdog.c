/*
 * COMEDI driver for the watchdog subdevice found on some addi-data boards
 * Copyright (c) 2013 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on implementations in various addi-data COMEDI drivers.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "../comedidev.h"
#include "addi_watchdog.h"

/*
 * Register offsets/defines for the addi-data watchdog
 */
#define ADDI_WDOG_REG			0x00
#define ADDI_WDOG_RELOAD_REG		0x04
#define ADDI_WDOG_TIMEBASE		0x08
#define ADDI_WDOG_CTRL_REG		0x0c
#define ADDI_WDOG_CTRL_ENABLE		(1 << 0)
#define ADDI_WDOG_CTRL_SW_TRIG		(1 << 9)
#define ADDI_WDOG_STATUS_REG		0x10
#define ADDI_WDOG_STATUS_ENABLED	(1 << 0)
#define ADDI_WDOG_STATUS_SW_TRIG	(1 << 1)

struct addi_watchdog_private {
	unsigned long iobase;
	unsigned int wdog_ctrl;
};

/*
 * The watchdog subdevice is configured with two INSN_CONFIG instructions:
 *
 * Enable the watchdog and set the reload timeout:
 *	data[0] = INSN_CONFIG_ARM
 *	data[1] = timeout reload value
 *
 * Disable the watchdog:
 *	data[0] = INSN_CONFIG_DISARM
 */
static int addi_watchdog_insn_config(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct addi_watchdog_private *spriv = s->private;
	unsigned int reload;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		spriv->wdog_ctrl = ADDI_WDOG_CTRL_ENABLE;
		reload = data[1] & s->maxdata;
		outl(reload, spriv->iobase + ADDI_WDOG_RELOAD_REG);

		/* Time base is 20ms, let the user know the timeout */
		dev_info(dev->class_dev, "watchdog enabled, timeout:%dms\n",
			20 * reload + 20);
		break;
	case INSN_CONFIG_DISARM:
		spriv->wdog_ctrl = 0;
		break;
	default:
		return -EINVAL;
	}

	outl(spriv->wdog_ctrl, spriv->iobase + ADDI_WDOG_CTRL_REG);

	return insn->n;
}

static int addi_watchdog_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct addi_watchdog_private *spriv = s->private;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = inl(spriv->iobase + ADDI_WDOG_STATUS_REG);

	return insn->n;
}

static int addi_watchdog_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct addi_watchdog_private *spriv = s->private;
	int i;

	if (spriv->wdog_ctrl == 0) {
		dev_warn(dev->class_dev, "watchdog is disabled\n");
		return -EINVAL;
	}

	/* "ping" the watchdog */
	for (i = 0; i < insn->n; i++) {
		outl(spriv->wdog_ctrl | ADDI_WDOG_CTRL_SW_TRIG,
		     spriv->iobase + ADDI_WDOG_CTRL_REG);
	}

	return insn->n;
}

void addi_watchdog_reset(unsigned long iobase)
{
	outl(0x0, iobase + ADDI_WDOG_CTRL_REG);
	outl(0x0, iobase + ADDI_WDOG_RELOAD_REG);
}
EXPORT_SYMBOL_GPL(addi_watchdog_reset);

int addi_watchdog_init(struct comedi_subdevice *s, unsigned long iobase)
{
	struct addi_watchdog_private *spriv;

	spriv = kzalloc(sizeof(*spriv), GFP_KERNEL);
	if (!spriv)
		return -ENOMEM;

	spriv->iobase = iobase;

	s->private	= spriv;

	s->type		= COMEDI_SUBD_TIMER;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 1;
	s->maxdata	= 0xff;
	s->insn_config	= addi_watchdog_insn_config;
	s->insn_read	= addi_watchdog_insn_read;
	s->insn_write	= addi_watchdog_insn_write;

	return 0;
}
EXPORT_SYMBOL_GPL(addi_watchdog_init);

static int __init addi_watchdog_module_init(void)
{
	return 0;
}
module_init(addi_watchdog_module_init);

static void __exit addi_watchdog_module_exit(void)
{
}
module_exit(addi_watchdog_module_exit);

MODULE_DESCRIPTION("ADDI-DATA Watchdog subdevice");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_LICENSE("GPL");
