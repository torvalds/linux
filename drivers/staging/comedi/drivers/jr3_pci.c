/*
  comedi/drivers/jr3_pci.c
  hardware driver for JR3/PCI force sensor board

  COMEDI - Linux Control and Measurement Device Interface
  Copyright (C) 2007 Anders Blomdell <anders.blomdell@control.lth.se>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/
/*
 * Driver: jr3_pci
 * Description: JR3/PCI force sensor board
 * Author: Anders Blomdell <anders.blomdell@control.lth.se>
 * Updated: Thu, 01 Nov 2012 17:34:55 +0000
 * Status: works
 * Devices: [JR3] PCI force sensor board (jr3_pci)
 *
 * Configuration options:
 *   None
 *
 * Manual configuration of comedi devices is not supported by this
 * driver; supported PCI devices are configured as comedi devices
 * automatically.
 *
 * The DSP on the board requires initialization code, which can be
 * loaded by placing it in /lib/firmware/comedi.  The initialization
 * code should be somewhere on the media you got with your card.  One
 * version is available from http://www.comedi.org in the
 * comedi_nonfree_firmware tarball.  The file is called "jr3pci.idm".
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/firmware.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "../comedidev.h"

#include "jr3_pci.h"

#define PCI_VENDOR_ID_JR3 0x1762
#define PCI_DEVICE_ID_JR3_1_CHANNEL 0x3111
#define PCI_DEVICE_ID_JR3_1_CHANNEL_NEW 0x1111
#define PCI_DEVICE_ID_JR3_2_CHANNEL 0x3112
#define PCI_DEVICE_ID_JR3_3_CHANNEL 0x3113
#define PCI_DEVICE_ID_JR3_4_CHANNEL 0x3114

struct jr3_pci_dev_private {
	struct jr3_t __iomem *iobase;
	int n_channels;
	struct timer_list timer;
};

struct poll_delay_t {
	int min;
	int max;
};

struct jr3_pci_subdev_private {
	struct jr3_channel __iomem *channel;
	unsigned long next_time_min;
	unsigned long next_time_max;
	enum { state_jr3_poll,
		state_jr3_init_wait_for_offset,
		state_jr3_init_transform_complete,
		state_jr3_init_set_full_scale_complete,
		state_jr3_init_use_offset_complete,
		state_jr3_done
	} state;
	int channel_no;
	int serial_no;
	int model_no;
	struct {
		int length;
		struct comedi_krange range;
	} range[9];
	const struct comedi_lrange *range_table_list[8 * 7 + 2];
	unsigned int maxdata_list[8 * 7 + 2];
	u16 errors;
	int retries;
};

/* Hotplug firmware loading stuff */
static int comedi_load_firmware(struct comedi_device *dev, const char *name,
				int (*cb)(struct comedi_device *dev,
					const u8 *data, size_t size))
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	int result = 0;
	const struct firmware *fw;
	char *firmware_path;

	firmware_path = kasprintf(GFP_KERNEL, "comedi/%s", name);
	if (!firmware_path) {
		result = -ENOMEM;
	} else {
		result = request_firmware(&fw, firmware_path, &pcidev->dev);
		if (result == 0) {
			if (!cb)
				result = -EINVAL;
			else
				result = cb(dev, fw->data, fw->size);
			release_firmware(fw);
		}
		kfree(firmware_path);
	}
	return result;
}

static struct poll_delay_t poll_delay_min_max(int min, int max)
{
	struct poll_delay_t result;

	result.min = min;
	result.max = max;
	return result;
}

static int is_complete(struct jr3_channel __iomem *channel)
{
	return get_s16(&channel->command_word0) == 0;
}

struct transform_t {
	struct {
		u16 link_type;
		s16 link_amount;
	} link[8];
};

static void set_transforms(struct jr3_channel __iomem *channel,
			   struct transform_t transf, short num)
{
	int i;

	num &= 0x000f;		/*  Make sure that 0 <= num <= 15 */
	for (i = 0; i < 8; i++) {
		set_u16(&channel->transforms[num].link[i].link_type,
			transf.link[i].link_type);
		udelay(1);
		set_s16(&channel->transforms[num].link[i].link_amount,
			transf.link[i].link_amount);
		udelay(1);
		if (transf.link[i].link_type == end_x_form)
			break;
	}
}

static void use_transform(struct jr3_channel __iomem *channel,
			  short transf_num)
{
	set_s16(&channel->command_word0, 0x0500 + (transf_num & 0x000f));
}

static void use_offset(struct jr3_channel __iomem *channel, short offset_num)
{
	set_s16(&channel->command_word0, 0x0600 + (offset_num & 0x000f));
}

static void set_offset(struct jr3_channel __iomem *channel)
{
	set_s16(&channel->command_word0, 0x0700);
}

struct six_axis_t {
	s16 fx;
	s16 fy;
	s16 fz;
	s16 mx;
	s16 my;
	s16 mz;
};

static void set_full_scales(struct jr3_channel __iomem *channel,
			    struct six_axis_t full_scale)
{
	set_s16(&channel->full_scale.fx, full_scale.fx);
	set_s16(&channel->full_scale.fy, full_scale.fy);
	set_s16(&channel->full_scale.fz, full_scale.fz);
	set_s16(&channel->full_scale.mx, full_scale.mx);
	set_s16(&channel->full_scale.my, full_scale.my);
	set_s16(&channel->full_scale.mz, full_scale.mz);
	set_s16(&channel->command_word0, 0x0a00);
}

static struct six_axis_t get_min_full_scales(struct jr3_channel __iomem
					     *channel)
{
	struct six_axis_t result;
	result.fx = get_s16(&channel->min_full_scale.fx);
	result.fy = get_s16(&channel->min_full_scale.fy);
	result.fz = get_s16(&channel->min_full_scale.fz);
	result.mx = get_s16(&channel->min_full_scale.mx);
	result.my = get_s16(&channel->min_full_scale.my);
	result.mz = get_s16(&channel->min_full_scale.mz);
	return result;
}

static struct six_axis_t get_max_full_scales(struct jr3_channel __iomem
					     *channel)
{
	struct six_axis_t result;
	result.fx = get_s16(&channel->max_full_scale.fx);
	result.fy = get_s16(&channel->max_full_scale.fy);
	result.fz = get_s16(&channel->max_full_scale.fz);
	result.mx = get_s16(&channel->max_full_scale.mx);
	result.my = get_s16(&channel->max_full_scale.my);
	result.mz = get_s16(&channel->max_full_scale.mz);
	return result;
}

static int jr3_pci_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int result;
	struct jr3_pci_subdev_private *p;
	int channel;

	p = s->private;
	channel = CR_CHAN(insn->chanspec);
	if (p == NULL || channel > 57) {
		result = -EINVAL;
	} else {
		int i;

		result = insn->n;
		if (p->state != state_jr3_done ||
		    (get_u16(&p->channel->errors) & (watch_dog | watch_dog2 |
						     sensor_change))) {
			/* No sensor or sensor changed */
			if (p->state == state_jr3_done) {
				/* Restart polling */
				p->state = state_jr3_poll;
			}
			result = -EAGAIN;
		}
		for (i = 0; i < insn->n; i++) {
			if (channel < 56) {
				int axis, filter;

				axis = channel % 8;
				filter = channel / 8;
				if (p->state != state_jr3_done) {
					data[i] = 0;
				} else {
					int F = 0;
					switch (axis) {
					case 0:
						F = get_s16(&p->channel->
							    filter[filter].fx);
						break;
					case 1:
						F = get_s16(&p->channel->
							    filter[filter].fy);
						break;
					case 2:
						F = get_s16(&p->channel->
							    filter[filter].fz);
						break;
					case 3:
						F = get_s16(&p->channel->
							    filter[filter].mx);
						break;
					case 4:
						F = get_s16(&p->channel->
							    filter[filter].my);
						break;
					case 5:
						F = get_s16(&p->channel->
							    filter[filter].mz);
						break;
					case 6:
						F = get_s16(&p->channel->
							    filter[filter].v1);
						break;
					case 7:
						F = get_s16(&p->channel->
							    filter[filter].v2);
						break;
					}
					data[i] = F + 0x4000;
				}
			} else if (channel == 56) {
				if (p->state != state_jr3_done)
					data[i] = 0;
				else
					data[i] =
					get_u16(&p->channel->model_no);
			} else if (channel == 57) {
				if (p->state != state_jr3_done)
					data[i] = 0;
				else
					data[i] =
					get_u16(&p->channel->serial_no);
			}
		}
	}
	return result;
}

static int jr3_pci_open(struct comedi_device *dev)
{
	int i;
	struct jr3_pci_dev_private *devpriv = dev->private;

	dev_dbg(dev->class_dev, "jr3_pci_open\n");
	for (i = 0; i < devpriv->n_channels; i++) {
		struct jr3_pci_subdev_private *p;

		p = dev->subdevices[i].private;
		if (p) {
			dev_dbg(dev->class_dev, "serial: %p %d (%d)\n", p,
				p->serial_no, p->channel_no);
		}
	}
	return 0;
}

static int read_idm_word(const u8 *data, size_t size, int *pos,
			 unsigned int *val)
{
	int result = 0;
	if (pos && val) {
		/*  Skip over non hex */
		for (; *pos < size && !isxdigit(data[*pos]); (*pos)++)
			;
		/*  Collect value */
		*val = 0;
		for (; *pos < size; (*pos)++) {
			int value;
			value = hex_to_bin(data[*pos]);
			if (value >= 0) {
				result = 1;
				*val = (*val << 4) + value;
			} else {
				break;
			}
		}
	}
	return result;
}

static int jr3_download_firmware(struct comedi_device *dev, const u8 *data,
				 size_t size)
{
	/*
	 * IDM file format is:
	 *   { count, address, data <count> } *
	 *   ffff
	 */
	int result, more, pos, OK;

	result = 0;
	more = 1;
	pos = 0;
	OK = 0;
	while (more) {
		unsigned int count, addr;

		more = more && read_idm_word(data, size, &pos, &count);
		if (more && count == 0xffff) {
			OK = 1;
			break;
		}
		more = more && read_idm_word(data, size, &pos, &addr);
		while (more && count > 0) {
			unsigned int dummy;
			more = more && read_idm_word(data, size, &pos, &dummy);
			count--;
		}
	}

	if (!OK) {
		result = -ENODATA;
	} else {
		int i;
		struct jr3_pci_dev_private *p = dev->private;

		for (i = 0; i < p->n_channels; i++) {
			struct jr3_pci_subdev_private *sp;

			sp = dev->subdevices[i].private;
			more = 1;
			pos = 0;
			while (more) {
				unsigned int count, addr;
				more = more &&
				       read_idm_word(data, size, &pos, &count);
				if (more && count == 0xffff)
					break;
				more = more &&
				       read_idm_word(data, size, &pos, &addr);
				dev_dbg(dev->class_dev,
					"Loading#%d %4.4x bytes at %4.4x\n",
					i, count, addr);
				while (more && count > 0) {
					if (addr & 0x4000) {
						/*  16 bit data, never seen
						 *  in real life!! */
						unsigned int data1;

						more = more &&
						       read_idm_word(data,
								     size, &pos,
								     &data1);
						count--;
						/* jr3[addr + 0x20000 * pnum] =
						   data1; */
					} else {
						/*   Download 24 bit program */
						unsigned int data1, data2;

						more = more &&
						       read_idm_word(data,
								     size, &pos,
								     &data1);
						more = more &&
						       read_idm_word(data, size,
								     &pos,
								     &data2);
						count -= 2;
						if (more) {
							set_u16(&p->
								iobase->channel
								[i].program_low
								[addr], data1);
							udelay(1);
							set_u16(&p->
								iobase->channel
								[i].program_high
								[addr], data2);
							udelay(1);
						}
					}
					addr++;
				}
			}
		}
	}
	return result;
}

static struct poll_delay_t jr3_pci_poll_subdevice(struct comedi_subdevice *s)
{
	struct poll_delay_t result = poll_delay_min_max(1000, 2000);
	struct jr3_pci_subdev_private *p = s->private;
	int i;

	if (p) {
		struct jr3_channel __iomem *channel = p->channel;
		int errors = get_u16(&channel->errors);

		if (errors != p->errors)
			p->errors = errors;

		if (errors & (watch_dog | watch_dog2 | sensor_change))
			/*  Sensor communication lost, force poll mode */
			p->state = state_jr3_poll;

		switch (p->state) {
		case state_jr3_poll: {
				u16 model_no = get_u16(&channel->model_no);
				u16 serial_no = get_u16(&channel->serial_no);
				if ((errors & (watch_dog | watch_dog2)) ||
				    model_no == 0 || serial_no == 0) {
					/*
					 * Still no sensor, keep on polling.
					 * Since it takes up to 10 seconds
					 * for offsets to stabilize, polling
					 * each second should suffice.
					 */
					result = poll_delay_min_max(1000, 2000);
				} else {
					p->retries = 0;
					p->state =
						state_jr3_init_wait_for_offset;
					result = poll_delay_min_max(1000, 2000);
				}
			}
			break;
		case state_jr3_init_wait_for_offset:
			p->retries++;
			if (p->retries < 10) {
				/*  Wait for offeset to stabilize
				 *  (< 10 s according to manual) */
				result = poll_delay_min_max(1000, 2000);
			} else {
				struct transform_t transf;

				p->model_no = get_u16(&channel->model_no);
				p->serial_no = get_u16(&channel->serial_no);

				/*  Transformation all zeros */
				for (i = 0; i < ARRAY_SIZE(transf.link); i++) {
					transf.link[i].link_type =
						(enum link_types)0;
					transf.link[i].link_amount = 0;
				}

				set_transforms(channel, transf, 0);
				use_transform(channel, 0);
				p->state = state_jr3_init_transform_complete;
				/*  Allow 20 ms for completion */
				result = poll_delay_min_max(20, 100);
			}
			break;
		case state_jr3_init_transform_complete:
			if (!is_complete(channel)) {
				result = poll_delay_min_max(20, 100);
			} else {
				/*  Set full scale */
				struct six_axis_t min_full_scale;
				struct six_axis_t max_full_scale;

				min_full_scale = get_min_full_scales(channel);
				max_full_scale = get_max_full_scales(channel);
				set_full_scales(channel, max_full_scale);

				p->state =
					state_jr3_init_set_full_scale_complete;
				/*  Allow 20 ms for completion */
				result = poll_delay_min_max(20, 100);
			}
			break;
		case state_jr3_init_set_full_scale_complete:
			if (!is_complete(channel)) {
				result = poll_delay_min_max(20, 100);
			} else {
				struct force_array __iomem *full_scale;

				/*  Use ranges in kN or we will
				 *  overflow around 2000N! */
				full_scale = &channel->full_scale;
				p->range[0].range.min =
					-get_s16(&full_scale->fx) * 1000;
				p->range[0].range.max =
					get_s16(&full_scale->fx) * 1000;
				p->range[1].range.min =
					-get_s16(&full_scale->fy) * 1000;
				p->range[1].range.max =
					get_s16(&full_scale->fy) * 1000;
				p->range[2].range.min =
					-get_s16(&full_scale->fz) * 1000;
				p->range[2].range.max =
					get_s16(&full_scale->fz) * 1000;
				p->range[3].range.min =
					-get_s16(&full_scale->mx) * 100;
				p->range[3].range.max =
					get_s16(&full_scale->mx) * 100;
				p->range[4].range.min =
					-get_s16(&full_scale->my) * 100;
				p->range[4].range.max =
					get_s16(&full_scale->my) * 100;
				p->range[5].range.min =
					-get_s16(&full_scale->mz) * 100;
				p->range[5].range.max =
					get_s16(&full_scale->mz) * 100;	/* ?? */
				p->range[6].range.min =
					-get_s16(&full_scale->v1) * 100;/* ?? */
				p->range[6].range.max =
					get_s16(&full_scale->v1) * 100;	/* ?? */
				p->range[7].range.min =
					-get_s16(&full_scale->v2) * 100;/* ?? */
				p->range[7].range.max =
					get_s16(&full_scale->v2) * 100;	/* ?? */
				p->range[8].range.min = 0;
				p->range[8].range.max = 65535;

				use_offset(channel, 0);
				p->state = state_jr3_init_use_offset_complete;
				/*  Allow 40 ms for completion */
				result = poll_delay_min_max(40, 100);
			}
			break;
		case state_jr3_init_use_offset_complete:
			if (!is_complete(channel)) {
				result = poll_delay_min_max(20, 100);
			} else {
				set_s16(&channel->offsets.fx, 0);
				set_s16(&channel->offsets.fy, 0);
				set_s16(&channel->offsets.fz, 0);
				set_s16(&channel->offsets.mx, 0);
				set_s16(&channel->offsets.my, 0);
				set_s16(&channel->offsets.mz, 0);

				set_offset(channel);

				p->state = state_jr3_done;
			}
			break;
		case state_jr3_done:
			poll_delay_min_max(10000, 20000);
			break;
		default:
			poll_delay_min_max(1000, 2000);
			break;
		}
	}
	return result;
}

static void jr3_pci_poll_dev(unsigned long data)
{
	unsigned long flags;
	struct comedi_device *dev = (struct comedi_device *)data;
	struct jr3_pci_dev_private *devpriv = dev->private;
	unsigned long now;
	int delay;
	int i;

	spin_lock_irqsave(&dev->spinlock, flags);
	delay = 1000;
	now = jiffies;
	/*  Poll all channels that are ready to be polled */
	for (i = 0; i < devpriv->n_channels; i++) {
		struct jr3_pci_subdev_private *subdevpriv =
			dev->subdevices[i].private;
		if (now > subdevpriv->next_time_min) {
			struct poll_delay_t sub_delay;

			sub_delay = jr3_pci_poll_subdevice(&dev->subdevices[i]);
			subdevpriv->next_time_min =
				jiffies + msecs_to_jiffies(sub_delay.min);
			subdevpriv->next_time_max =
				jiffies + msecs_to_jiffies(sub_delay.max);
			if (sub_delay.max && sub_delay.max < delay)
				/*
				 * Wake up as late as possible ->
				 * poll as many channels as possible at once.
				 */
				delay = sub_delay.max;
		}
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	devpriv->timer.expires = jiffies + msecs_to_jiffies(delay);
	add_timer(&devpriv->timer);
}

static int jr3_pci_auto_attach(struct comedi_device *dev,
					 unsigned long context_unused)
{
	int result;
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	int i;
	struct jr3_pci_dev_private *devpriv;

	if (sizeof(struct jr3_channel) != 0xc00) {
		dev_err(dev->class_dev,
			"sizeof(struct jr3_channel) = %x [expected %x]\n",
			(unsigned)sizeof(struct jr3_channel), 0xc00);
		return -EINVAL;
	}

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	init_timer(&devpriv->timer);
	switch (pcidev->device) {
	case PCI_DEVICE_ID_JR3_1_CHANNEL:
	case PCI_DEVICE_ID_JR3_1_CHANNEL_NEW:
		devpriv->n_channels = 1;
		break;
	case PCI_DEVICE_ID_JR3_2_CHANNEL:
		devpriv->n_channels = 2;
		break;
	case PCI_DEVICE_ID_JR3_3_CHANNEL:
		devpriv->n_channels = 3;
		break;
	case PCI_DEVICE_ID_JR3_4_CHANNEL:
		devpriv->n_channels = 4;
		break;
	default:
		dev_err(dev->class_dev, "jr3_pci: pci %s not supported\n",
			pci_name(pcidev));
		return -EINVAL;
		break;
	}

	result = comedi_pci_enable(dev);
	if (result)
		return result;

	devpriv->iobase = pci_ioremap_bar(pcidev, 0);
	if (!devpriv->iobase)
		return -ENOMEM;

	result = comedi_alloc_subdevices(dev, devpriv->n_channels);
	if (result)
		return result;

	dev->open = jr3_pci_open;
	for (i = 0; i < devpriv->n_channels; i++) {
		dev->subdevices[i].type = COMEDI_SUBD_AI;
		dev->subdevices[i].subdev_flags = SDF_READABLE | SDF_GROUND;
		dev->subdevices[i].n_chan = 8 * 7 + 2;
		dev->subdevices[i].insn_read = jr3_pci_ai_insn_read;
		dev->subdevices[i].private =
			kzalloc(sizeof(struct jr3_pci_subdev_private),
				GFP_KERNEL);
		if (dev->subdevices[i].private) {
			struct jr3_pci_subdev_private *p;
			int j;

			p = dev->subdevices[i].private;
			p->channel = &devpriv->iobase->channel[i].data;
			dev_dbg(dev->class_dev, "p->channel %p %p (%tx)\n",
				p->channel, devpriv->iobase,
				((char __iomem *)p->channel -
				 (char __iomem *)devpriv->iobase));
			p->channel_no = i;
			for (j = 0; j < 8; j++) {
				int k;

				p->range[j].length = 1;
				p->range[j].range.min = -1000000;
				p->range[j].range.max = 1000000;
				for (k = 0; k < 7; k++) {
					p->range_table_list[j + k * 8] =
					    (struct comedi_lrange *)&p->
					    range[j];
					p->maxdata_list[j + k * 8] = 0x7fff;
				}
			}
			p->range[8].length = 1;
			p->range[8].range.min = 0;
			p->range[8].range.max = 65536;

			p->range_table_list[56] =
				(struct comedi_lrange *)&p->range[8];
			p->range_table_list[57] =
				(struct comedi_lrange *)&p->range[8];
			p->maxdata_list[56] = 0xffff;
			p->maxdata_list[57] = 0xffff;
			/*  Channel specific range and maxdata */
			dev->subdevices[i].range_table = NULL;
			dev->subdevices[i].range_table_list =
				p->range_table_list;
			dev->subdevices[i].maxdata = 0;
			dev->subdevices[i].maxdata_list = p->maxdata_list;
		}
	}

	/*  Reset DSP card */
	writel(0, &devpriv->iobase->channel[0].reset);

	result = comedi_load_firmware(dev, "jr3pci.idm", jr3_download_firmware);
	dev_dbg(dev->class_dev, "Firmare load %d\n", result);

	if (result < 0)
		return result;
	/*
	 * TODO: use firmware to load preferred offset tables. Suggested
	 * format:
	 *     model serial Fx Fy Fz Mx My Mz\n
	 *
	 *     comedi_load_firmware(dev, "jr3_offsets_table",
	 *                          jr3_download_firmware);
	 */

	/*
	 * It takes a few milliseconds for software to settle as much as we
	 * can read firmware version
	 */
	msleep_interruptible(25);
	for (i = 0; i < 0x18; i++) {
		dev_dbg(dev->class_dev, "%c\n",
			get_u16(&devpriv->iobase->channel[0].
				data.copyright[i]) >> 8);
	}

	/*  Start card timer */
	for (i = 0; i < devpriv->n_channels; i++) {
		struct jr3_pci_subdev_private *p = dev->subdevices[i].private;

		p->next_time_min = jiffies + msecs_to_jiffies(500);
		p->next_time_max = jiffies + msecs_to_jiffies(2000);
	}

	devpriv->timer.data = (unsigned long)dev;
	devpriv->timer.function = jr3_pci_poll_dev;
	devpriv->timer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&devpriv->timer);

	return result;
}

static void jr3_pci_detach(struct comedi_device *dev)
{
	int i;
	struct jr3_pci_dev_private *devpriv = dev->private;

	if (devpriv) {
		del_timer_sync(&devpriv->timer);

		if (dev->subdevices) {
			for (i = 0; i < devpriv->n_channels; i++)
				kfree(dev->subdevices[i].private);
		}
		if (devpriv->iobase)
			iounmap(devpriv->iobase);
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver jr3_pci_driver = {
	.driver_name	= "jr3_pci",
	.module		= THIS_MODULE,
	.auto_attach	= jr3_pci_auto_attach,
	.detach		= jr3_pci_detach,
};

static int jr3_pci_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &jr3_pci_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(jr3_pci_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_JR3, PCI_DEVICE_ID_JR3_1_CHANNEL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JR3, PCI_DEVICE_ID_JR3_1_CHANNEL_NEW) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JR3, PCI_DEVICE_ID_JR3_2_CHANNEL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JR3, PCI_DEVICE_ID_JR3_3_CHANNEL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_JR3, PCI_DEVICE_ID_JR3_4_CHANNEL) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, jr3_pci_pci_table);

static struct pci_driver jr3_pci_pci_driver = {
	.name		= "jr3_pci",
	.id_table	= jr3_pci_pci_table,
	.probe		= jr3_pci_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(jr3_pci_driver, jr3_pci_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("comedi/jr3pci.idm");
