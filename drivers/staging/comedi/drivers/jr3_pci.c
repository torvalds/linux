// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/jr3_pci.c
 * hardware driver for JR3/PCI force sensor board
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2007 Anders Blomdell <anders.blomdell@control.lth.se>
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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "../comedi_pci.h"

#include "jr3_pci.h"

#define PCI_VENDOR_ID_JR3 0x1762

enum jr3_pci_boardid {
	BOARD_JR3_1,
	BOARD_JR3_2,
	BOARD_JR3_3,
	BOARD_JR3_4,
};

struct jr3_pci_board {
	const char *name;
	int n_subdevs;
};

static const struct jr3_pci_board jr3_pci_boards[] = {
	[BOARD_JR3_1] = {
		.name		= "jr3_pci_1",
		.n_subdevs	= 1,
	},
	[BOARD_JR3_2] = {
		.name		= "jr3_pci_2",
		.n_subdevs	= 2,
	},
	[BOARD_JR3_3] = {
		.name		= "jr3_pci_3",
		.n_subdevs	= 3,
	},
	[BOARD_JR3_4] = {
		.name		= "jr3_pci_4",
		.n_subdevs	= 4,
	},
};

struct jr3_pci_transform {
	struct {
		u16 link_type;
		s16 link_amount;
	} link[8];
};

struct jr3_pci_poll_delay {
	int min;
	int max;
};

struct jr3_pci_dev_private {
	struct timer_list timer;
	struct comedi_device *dev;
};

union jr3_pci_single_range {
	struct comedi_lrange l;
	char _reserved[offsetof(struct comedi_lrange, range[1])];
};

enum jr3_pci_poll_state {
	state_jr3_poll,
	state_jr3_init_wait_for_offset,
	state_jr3_init_transform_complete,
	state_jr3_init_set_full_scale_complete,
	state_jr3_init_use_offset_complete,
	state_jr3_done
};

struct jr3_pci_subdev_private {
	struct jr3_sensor __iomem *sensor;
	unsigned long next_time_min;
	enum jr3_pci_poll_state state;
	int serial_no;
	int model_no;
	union jr3_pci_single_range range[9];
	const struct comedi_lrange *range_table_list[8 * 7 + 2];
	unsigned int maxdata_list[8 * 7 + 2];
	u16 errors;
	int retries;
};

static struct jr3_pci_poll_delay poll_delay_min_max(int min, int max)
{
	struct jr3_pci_poll_delay result;

	result.min = min;
	result.max = max;
	return result;
}

static int is_complete(struct jr3_sensor __iomem *sensor)
{
	return get_s16(&sensor->command_word0) == 0;
}

static void set_transforms(struct jr3_sensor __iomem *sensor,
			   const struct jr3_pci_transform *transf, short num)
{
	int i;

	num &= 0x000f;		/* Make sure that 0 <= num <= 15 */
	for (i = 0; i < 8; i++) {
		set_u16(&sensor->transforms[num].link[i].link_type,
			transf->link[i].link_type);
		udelay(1);
		set_s16(&sensor->transforms[num].link[i].link_amount,
			transf->link[i].link_amount);
		udelay(1);
		if (transf->link[i].link_type == end_x_form)
			break;
	}
}

static void use_transform(struct jr3_sensor __iomem *sensor,
			  short transf_num)
{
	set_s16(&sensor->command_word0, 0x0500 + (transf_num & 0x000f));
}

static void use_offset(struct jr3_sensor __iomem *sensor, short offset_num)
{
	set_s16(&sensor->command_word0, 0x0600 + (offset_num & 0x000f));
}

static void set_offset(struct jr3_sensor __iomem *sensor)
{
	set_s16(&sensor->command_word0, 0x0700);
}

struct six_axis_t {
	s16 fx;
	s16 fy;
	s16 fz;
	s16 mx;
	s16 my;
	s16 mz;
};

static void set_full_scales(struct jr3_sensor __iomem *sensor,
			    struct six_axis_t full_scale)
{
	set_s16(&sensor->full_scale.fx, full_scale.fx);
	set_s16(&sensor->full_scale.fy, full_scale.fy);
	set_s16(&sensor->full_scale.fz, full_scale.fz);
	set_s16(&sensor->full_scale.mx, full_scale.mx);
	set_s16(&sensor->full_scale.my, full_scale.my);
	set_s16(&sensor->full_scale.mz, full_scale.mz);
	set_s16(&sensor->command_word0, 0x0a00);
}

static struct six_axis_t get_min_full_scales(struct jr3_sensor __iomem *sensor)
{
	struct six_axis_t result;

	result.fx = get_s16(&sensor->min_full_scale.fx);
	result.fy = get_s16(&sensor->min_full_scale.fy);
	result.fz = get_s16(&sensor->min_full_scale.fz);
	result.mx = get_s16(&sensor->min_full_scale.mx);
	result.my = get_s16(&sensor->min_full_scale.my);
	result.mz = get_s16(&sensor->min_full_scale.mz);
	return result;
}

static struct six_axis_t get_max_full_scales(struct jr3_sensor __iomem *sensor)
{
	struct six_axis_t result;

	result.fx = get_s16(&sensor->max_full_scale.fx);
	result.fy = get_s16(&sensor->max_full_scale.fy);
	result.fz = get_s16(&sensor->max_full_scale.fz);
	result.mx = get_s16(&sensor->max_full_scale.mx);
	result.my = get_s16(&sensor->max_full_scale.my);
	result.mz = get_s16(&sensor->max_full_scale.mz);
	return result;
}

static unsigned int jr3_pci_ai_read_chan(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 unsigned int chan)
{
	struct jr3_pci_subdev_private *spriv = s->private;
	unsigned int val = 0;

	if (spriv->state != state_jr3_done)
		return 0;

	if (chan < 56) {
		unsigned int axis = chan % 8;
		unsigned int filter = chan / 8;

		switch (axis) {
		case 0:
			val = get_s16(&spriv->sensor->filter[filter].fx);
			break;
		case 1:
			val = get_s16(&spriv->sensor->filter[filter].fy);
			break;
		case 2:
			val = get_s16(&spriv->sensor->filter[filter].fz);
			break;
		case 3:
			val = get_s16(&spriv->sensor->filter[filter].mx);
			break;
		case 4:
			val = get_s16(&spriv->sensor->filter[filter].my);
			break;
		case 5:
			val = get_s16(&spriv->sensor->filter[filter].mz);
			break;
		case 6:
			val = get_s16(&spriv->sensor->filter[filter].v1);
			break;
		case 7:
			val = get_s16(&spriv->sensor->filter[filter].v2);
			break;
		}
		val += 0x4000;
	} else if (chan == 56) {
		val = get_u16(&spriv->sensor->model_no);
	} else if (chan == 57) {
		val = get_u16(&spriv->sensor->serial_no);
	}

	return val;
}

static int jr3_pci_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct jr3_pci_subdev_private *spriv = s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	u16 errors;
	int i;

	errors = get_u16(&spriv->sensor->errors);
	if (spriv->state != state_jr3_done ||
	    (errors & (watch_dog | watch_dog2 | sensor_change))) {
		/* No sensor or sensor changed */
		if (spriv->state == state_jr3_done) {
			/* Restart polling */
			spriv->state = state_jr3_poll;
		}
		return -EAGAIN;
	}

	for (i = 0; i < insn->n; i++)
		data[i] = jr3_pci_ai_read_chan(dev, s, chan);

	return insn->n;
}

static int jr3_pci_open(struct comedi_device *dev)
{
	struct jr3_pci_subdev_private *spriv;
	struct comedi_subdevice *s;
	int i;

	dev_dbg(dev->class_dev, "jr3_pci_open\n");
	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		spriv = s->private;
		dev_dbg(dev->class_dev, "serial[%d]: %d\n", s->index,
			spriv->serial_no);
	}
	return 0;
}

static int read_idm_word(const u8 *data, size_t size, int *pos,
			 unsigned int *val)
{
	int result = 0;
	int value;

	if (pos && val) {
		/* Skip over non hex */
		for (; *pos < size && !isxdigit(data[*pos]); (*pos)++)
			;
		/* Collect value */
		*val = 0;
		for (; *pos < size; (*pos)++) {
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

static int jr3_check_firmware(struct comedi_device *dev,
			      const u8 *data, size_t size)
{
	int more = 1;
	int pos = 0;

	/*
	 * IDM file format is:
	 *   { count, address, data <count> } *
	 *   ffff
	 */
	while (more) {
		unsigned int count = 0;
		unsigned int addr = 0;

		more = more && read_idm_word(data, size, &pos, &count);
		if (more && count == 0xffff)
			return 0;

		more = more && read_idm_word(data, size, &pos, &addr);
		while (more && count > 0) {
			unsigned int dummy = 0;

			more = more && read_idm_word(data, size, &pos, &dummy);
			count--;
		}
	}

	return -ENODATA;
}

static void jr3_write_firmware(struct comedi_device *dev,
			       int subdev, const u8 *data, size_t size)
{
	struct jr3_block __iomem *block = dev->mmio;
	u32 __iomem *lo;
	u32 __iomem *hi;
	int more = 1;
	int pos = 0;

	while (more) {
		unsigned int count = 0;
		unsigned int addr = 0;

		more = more && read_idm_word(data, size, &pos, &count);
		if (more && count == 0xffff)
			return;

		more = more && read_idm_word(data, size, &pos, &addr);

		dev_dbg(dev->class_dev, "Loading#%d %4.4x bytes at %4.4x\n",
			subdev, count, addr);

		while (more && count > 0) {
			if (addr & 0x4000) {
				/* 16 bit data, never seen in real life!! */
				unsigned int data1 = 0;

				more = more &&
				       read_idm_word(data, size, &pos, &data1);
				count--;
				/* jr3[addr + 0x20000 * pnum] = data1; */
			} else {
				/* Download 24 bit program */
				unsigned int data1 = 0;
				unsigned int data2 = 0;

				lo = &block[subdev].program_lo[addr];
				hi = &block[subdev].program_hi[addr];

				more = more &&
				       read_idm_word(data, size, &pos, &data1);
				more = more &&
				       read_idm_word(data, size, &pos, &data2);
				count -= 2;
				if (more) {
					set_u16(lo, data1);
					udelay(1);
					set_u16(hi, data2);
					udelay(1);
				}
			}
			addr++;
		}
	}
}

static int jr3_download_firmware(struct comedi_device *dev,
				 const u8 *data, size_t size,
				 unsigned long context)
{
	int subdev;
	int ret;

	/* verify IDM file format */
	ret = jr3_check_firmware(dev, data, size);
	if (ret)
		return ret;

	/* write firmware to each subdevice */
	for (subdev = 0; subdev < dev->n_subdevices; subdev++)
		jr3_write_firmware(dev, subdev, data, size);

	return 0;
}

static struct jr3_pci_poll_delay
jr3_pci_poll_subdevice(struct comedi_subdevice *s)
{
	struct jr3_pci_subdev_private *spriv = s->private;
	struct jr3_pci_poll_delay result = poll_delay_min_max(1000, 2000);
	struct jr3_sensor __iomem *sensor;
	u16 model_no;
	u16 serial_no;
	int errors;
	int i;

	sensor = spriv->sensor;
	errors = get_u16(&sensor->errors);

	if (errors != spriv->errors)
		spriv->errors = errors;

	/* Sensor communication lost? force poll mode */
	if (errors & (watch_dog | watch_dog2 | sensor_change))
		spriv->state = state_jr3_poll;

	switch (spriv->state) {
	case state_jr3_poll:
		model_no = get_u16(&sensor->model_no);
		serial_no = get_u16(&sensor->serial_no);

		if ((errors & (watch_dog | watch_dog2)) ||
		    model_no == 0 || serial_no == 0) {
			/*
			 * Still no sensor, keep on polling.
			 * Since it takes up to 10 seconds for offsets to
			 * stabilize, polling each second should suffice.
			 */
		} else {
			spriv->retries = 0;
			spriv->state = state_jr3_init_wait_for_offset;
		}
		break;
	case state_jr3_init_wait_for_offset:
		spriv->retries++;
		if (spriv->retries < 10) {
			/*
			 * Wait for offeset to stabilize
			 * (< 10 s according to manual)
			 */
		} else {
			struct jr3_pci_transform transf;

			spriv->model_no = get_u16(&sensor->model_no);
			spriv->serial_no = get_u16(&sensor->serial_no);

			/* Transformation all zeros */
			for (i = 0; i < ARRAY_SIZE(transf.link); i++) {
				transf.link[i].link_type = (enum link_types)0;
				transf.link[i].link_amount = 0;
			}

			set_transforms(sensor, &transf, 0);
			use_transform(sensor, 0);
			spriv->state = state_jr3_init_transform_complete;
			/* Allow 20 ms for completion */
			result = poll_delay_min_max(20, 100);
		}
		break;
	case state_jr3_init_transform_complete:
		if (!is_complete(sensor)) {
			result = poll_delay_min_max(20, 100);
		} else {
			/* Set full scale */
			struct six_axis_t min_full_scale;
			struct six_axis_t max_full_scale;

			min_full_scale = get_min_full_scales(sensor);
			max_full_scale = get_max_full_scales(sensor);
			set_full_scales(sensor, max_full_scale);

			spriv->state = state_jr3_init_set_full_scale_complete;
			/* Allow 20 ms for completion */
			result = poll_delay_min_max(20, 100);
		}
		break;
	case state_jr3_init_set_full_scale_complete:
		if (!is_complete(sensor)) {
			result = poll_delay_min_max(20, 100);
		} else {
			struct force_array __iomem *fs = &sensor->full_scale;
			union jr3_pci_single_range *r = spriv->range;

			/* Use ranges in kN or we will overflow around 2000N! */
			r[0].l.range[0].min = -get_s16(&fs->fx) * 1000;
			r[0].l.range[0].max = get_s16(&fs->fx) * 1000;
			r[1].l.range[0].min = -get_s16(&fs->fy) * 1000;
			r[1].l.range[0].max = get_s16(&fs->fy) * 1000;
			r[2].l.range[0].min = -get_s16(&fs->fz) * 1000;
			r[2].l.range[0].max = get_s16(&fs->fz) * 1000;
			r[3].l.range[0].min = -get_s16(&fs->mx) * 100;
			r[3].l.range[0].max = get_s16(&fs->mx) * 100;
			r[4].l.range[0].min = -get_s16(&fs->my) * 100;
			r[4].l.range[0].max = get_s16(&fs->my) * 100;
			r[5].l.range[0].min = -get_s16(&fs->mz) * 100;
			/* the next five are questionable */
			r[5].l.range[0].max = get_s16(&fs->mz) * 100;
			r[6].l.range[0].min = -get_s16(&fs->v1) * 100;
			r[6].l.range[0].max = get_s16(&fs->v1) * 100;
			r[7].l.range[0].min = -get_s16(&fs->v2) * 100;
			r[7].l.range[0].max = get_s16(&fs->v2) * 100;
			r[8].l.range[0].min = 0;
			r[8].l.range[0].max = 65535;

			use_offset(sensor, 0);
			spriv->state = state_jr3_init_use_offset_complete;
			/* Allow 40 ms for completion */
			result = poll_delay_min_max(40, 100);
		}
		break;
	case state_jr3_init_use_offset_complete:
		if (!is_complete(sensor)) {
			result = poll_delay_min_max(20, 100);
		} else {
			set_s16(&sensor->offsets.fx, 0);
			set_s16(&sensor->offsets.fy, 0);
			set_s16(&sensor->offsets.fz, 0);
			set_s16(&sensor->offsets.mx, 0);
			set_s16(&sensor->offsets.my, 0);
			set_s16(&sensor->offsets.mz, 0);

			set_offset(sensor);

			spriv->state = state_jr3_done;
		}
		break;
	case state_jr3_done:
		result = poll_delay_min_max(10000, 20000);
		break;
	default:
		break;
	}

	return result;
}

static void jr3_pci_poll_dev(struct timer_list *t)
{
	struct jr3_pci_dev_private *devpriv = from_timer(devpriv, t, timer);
	struct comedi_device *dev = devpriv->dev;
	struct jr3_pci_subdev_private *spriv;
	struct comedi_subdevice *s;
	unsigned long flags;
	unsigned long now;
	int delay;
	int i;

	spin_lock_irqsave(&dev->spinlock, flags);
	delay = 1000;
	now = jiffies;

	/* Poll all sensors that are ready to be polled */
	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		spriv = s->private;

		if (time_after_eq(now, spriv->next_time_min)) {
			struct jr3_pci_poll_delay sub_delay;

			sub_delay = jr3_pci_poll_subdevice(s);

			spriv->next_time_min = jiffies +
					       msecs_to_jiffies(sub_delay.min);

			if (sub_delay.max && sub_delay.max < delay)
				/*
				 * Wake up as late as possible ->
				 * poll as many sensors as possible at once.
				 */
				delay = sub_delay.max;
		}
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	devpriv->timer.expires = jiffies + msecs_to_jiffies(delay);
	add_timer(&devpriv->timer);
}

static struct jr3_pci_subdev_private *
jr3_pci_alloc_spriv(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct jr3_block __iomem *block = dev->mmio;
	struct jr3_pci_subdev_private *spriv;
	int j;
	int k;

	spriv = comedi_alloc_spriv(s, sizeof(*spriv));
	if (!spriv)
		return NULL;

	spriv->sensor = &block[s->index].sensor;

	for (j = 0; j < 8; j++) {
		spriv->range[j].l.length = 1;
		spriv->range[j].l.range[0].min = -1000000;
		spriv->range[j].l.range[0].max = 1000000;

		for (k = 0; k < 7; k++) {
			spriv->range_table_list[j + k * 8] = &spriv->range[j].l;
			spriv->maxdata_list[j + k * 8] = 0x7fff;
		}
	}
	spriv->range[8].l.length = 1;
	spriv->range[8].l.range[0].min = 0;
	spriv->range[8].l.range[0].max = 65535;

	spriv->range_table_list[56] = &spriv->range[8].l;
	spriv->range_table_list[57] = &spriv->range[8].l;
	spriv->maxdata_list[56] = 0xffff;
	spriv->maxdata_list[57] = 0xffff;

	return spriv;
}

static void jr3_pci_show_copyright(struct comedi_device *dev)
{
	struct jr3_block __iomem *block = dev->mmio;
	struct jr3_sensor __iomem *sensor0 = &block[0].sensor;
	char copy[ARRAY_SIZE(sensor0->copyright) + 1];
	int i;

	for (i = 0; i < ARRAY_SIZE(sensor0->copyright); i++)
		copy[i] = (char)(get_u16(&sensor0->copyright[i]) >> 8);
	copy[i] = '\0';
	dev_dbg(dev->class_dev, "Firmware copyright: %s\n", copy);
}

static int jr3_pci_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	static const struct jr3_pci_board *board;
	struct jr3_pci_dev_private *devpriv;
	struct jr3_pci_subdev_private *spriv;
	struct jr3_block __iomem *block;
	struct comedi_subdevice *s;
	int ret;
	int i;

	BUILD_BUG_ON(sizeof(struct jr3_block) != 0x80000);

	if (context < ARRAY_SIZE(jr3_pci_boards))
		board = &jr3_pci_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	if (pci_resource_len(pcidev, 0) < board->n_subdevs * sizeof(*block))
		return -ENXIO;

	dev->mmio = pci_ioremap_bar(pcidev, 0);
	if (!dev->mmio)
		return -ENOMEM;

	block = dev->mmio;

	ret = comedi_alloc_subdevices(dev, board->n_subdevs);
	if (ret)
		return ret;

	dev->open = jr3_pci_open;
	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | SDF_GROUND;
		s->n_chan	= 8 * 7 + 2;
		s->insn_read	= jr3_pci_ai_insn_read;

		spriv = jr3_pci_alloc_spriv(dev, s);
		if (!spriv)
			return -ENOMEM;

		/* Channel specific range and maxdata */
		s->range_table_list	= spriv->range_table_list;
		s->maxdata_list		= spriv->maxdata_list;
	}

	/* Reset DSP card */
	for (i = 0; i < dev->n_subdevices; i++)
		writel(0, &block[i].reset);

	ret = comedi_load_firmware(dev, &comedi_to_pci_dev(dev)->dev,
				   "comedi/jr3pci.idm",
				   jr3_download_firmware, 0);
	dev_dbg(dev->class_dev, "Firmware load %d\n", ret);
	if (ret < 0)
		return ret;
	/*
	 * TODO: use firmware to load preferred offset tables. Suggested
	 * format:
	 *     model serial Fx Fy Fz Mx My Mz\n
	 *
	 *     comedi_load_firmware(dev, &comedi_to_pci_dev(dev)->dev,
	 *                          "comedi/jr3_offsets_table",
	 *                          jr3_download_firmware, 1);
	 */

	/*
	 * It takes a few milliseconds for software to settle as much as we
	 * can read firmware version
	 */
	msleep_interruptible(25);
	jr3_pci_show_copyright(dev);

	/* Start card timer */
	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		spriv = s->private;

		spriv->next_time_min = jiffies + msecs_to_jiffies(500);
	}

	devpriv->dev = dev;
	timer_setup(&devpriv->timer, jr3_pci_poll_dev, 0);
	devpriv->timer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&devpriv->timer);

	return 0;
}

static void jr3_pci_detach(struct comedi_device *dev)
{
	struct jr3_pci_dev_private *devpriv = dev->private;

	if (devpriv)
		del_timer_sync(&devpriv->timer);

	comedi_pci_detach(dev);
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

static const struct pci_device_id jr3_pci_pci_table[] = {
	{ PCI_VDEVICE(JR3, 0x1111), BOARD_JR3_1 },
	{ PCI_VDEVICE(JR3, 0x3111), BOARD_JR3_1 },
	{ PCI_VDEVICE(JR3, 0x3112), BOARD_JR3_2 },
	{ PCI_VDEVICE(JR3, 0x3113), BOARD_JR3_3 },
	{ PCI_VDEVICE(JR3, 0x3114), BOARD_JR3_4 },
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
MODULE_DESCRIPTION("Comedi driver for JR3/PCI force sensor board");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("comedi/jr3pci.idm");
