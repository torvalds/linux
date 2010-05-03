/*
    kcomedilib/kcomedilib.c
    a comedlib interface for kernel modules

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/io.h>

#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

MODULE_AUTHOR("David Schleef <ds@schleef.org>");
MODULE_DESCRIPTION("Comedi kernel library");
MODULE_LICENSE("GPL");

void *comedi_open(const char *filename)
{
	struct comedi_device_file_info *dev_file_info;
	struct comedi_device *dev;
	unsigned int minor;

	if (strncmp(filename, "/dev/comedi", 11) != 0)
		return NULL;

	minor = simple_strtoul(filename + 11, NULL, 0);

	if (minor >= COMEDI_NUM_BOARD_MINORS)
		return NULL;

	dev_file_info = comedi_get_device_file_info(minor);
	if (dev_file_info == NULL)
		return NULL;
	dev = dev_file_info->device;

	if (dev == NULL || !dev->attached)
		return NULL;

	if (!try_module_get(dev->driver->module))
		return NULL;

	return (void *)dev;
}
EXPORT_SYMBOL(comedi_open);

int comedi_close(void *d)
{
	struct comedi_device *dev = (struct comedi_device *)d;

	module_put(dev->driver->module);

	return 0;
}
EXPORT_SYMBOL(comedi_close);

/*
 *	COMEDI_INSN
 *	perform an instruction
 */
int comedi_do_insn(void *d, struct comedi_insn *insn)
{
	struct comedi_device *dev = (struct comedi_device *)d;
	struct comedi_subdevice *s;
	int ret = 0;

	if (insn->insn & INSN_MASK_SPECIAL) {
		switch (insn->insn) {
		case INSN_GTOD:
			{
				struct timeval tv;

				do_gettimeofday(&tv);
				insn->data[0] = tv.tv_sec;
				insn->data[1] = tv.tv_usec;
				ret = 2;

				break;
			}
		case INSN_WAIT:
			/* XXX isn't the value supposed to be nanosecs? */
			if (insn->n != 1 || insn->data[0] >= 100) {
				ret = -EINVAL;
				break;
			}
			udelay(insn->data[0]);
			ret = 1;
			break;
		case INSN_INTTRIG:
			if (insn->n != 1) {
				ret = -EINVAL;
				break;
			}
			if (insn->subdev >= dev->n_subdevices) {
				printk("%d not usable subdevice\n",
				       insn->subdev);
				ret = -EINVAL;
				break;
			}
			s = dev->subdevices + insn->subdev;
			if (!s->async) {
				printk("no async\n");
				ret = -EINVAL;
				break;
			}
			if (!s->async->inttrig) {
				printk("no inttrig\n");
				ret = -EAGAIN;
				break;
			}
			ret = s->async->inttrig(dev, s, insn->data[0]);
			if (ret >= 0)
				ret = 1;
			break;
		default:
			ret = -EINVAL;
		}
	} else {
		/* a subdevice instruction */
		if (insn->subdev >= dev->n_subdevices) {
			ret = -EINVAL;
			goto error;
		}
		s = dev->subdevices + insn->subdev;

		if (s->type == COMEDI_SUBD_UNUSED) {
			printk("%d not useable subdevice\n", insn->subdev);
			ret = -EIO;
			goto error;
		}

		/* XXX check lock */

		ret = comedi_check_chanlist(s, 1, &insn->chanspec);
		if (ret < 0) {
			printk("bad chanspec\n");
			ret = -EINVAL;
			goto error;
		}

		if (s->busy) {
			ret = -EBUSY;
			goto error;
		}
		s->busy = d;

		switch (insn->insn) {
		case INSN_READ:
			ret = s->insn_read(dev, s, insn, insn->data);
			break;
		case INSN_WRITE:
			ret = s->insn_write(dev, s, insn, insn->data);
			break;
		case INSN_BITS:
			ret = s->insn_bits(dev, s, insn, insn->data);
			break;
		case INSN_CONFIG:
			/* XXX should check instruction length */
			ret = s->insn_config(dev, s, insn, insn->data);
			break;
		default:
			ret = -EINVAL;
			break;
		}

		s->busy = NULL;
	}
	if (ret < 0)
		goto error;
#if 0
	/* XXX do we want this? -- abbotti #if'ed it out for now. */
	if (ret != insn->n) {
		printk("BUG: result of insn != insn.n\n");
		ret = -EINVAL;
		goto error;
	}
#endif
error:

	return ret;
}

