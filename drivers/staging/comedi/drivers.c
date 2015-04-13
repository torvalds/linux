/*
    module/drivers.c
    functions for manipulating drivers

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
    Copyright (C) 2002 Frank Mori Hess <fmhess@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>	/* for SuSE brokenness */
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

#include "comedidev.h"
#include "comedi_internal.h"

struct comedi_driver *comedi_drivers;
/* protects access to comedi_drivers */
DEFINE_MUTEX(comedi_drivers_list_lock);

int comedi_set_hw_dev(struct comedi_device *dev, struct device *hw_dev)
{
	if (hw_dev == dev->hw_dev)
		return 0;
	if (dev->hw_dev != NULL)
		return -EEXIST;
	dev->hw_dev = get_device(hw_dev);
	return 0;
}
EXPORT_SYMBOL_GPL(comedi_set_hw_dev);

static void comedi_clear_hw_dev(struct comedi_device *dev)
{
	put_device(dev->hw_dev);
	dev->hw_dev = NULL;
}

/**
 * comedi_alloc_devpriv() - Allocate memory for the device private data.
 * @dev: comedi_device struct
 * @size: size of the memory to allocate
 */
void *comedi_alloc_devpriv(struct comedi_device *dev, size_t size)
{
	dev->private = kzalloc(size, GFP_KERNEL);
	return dev->private;
}
EXPORT_SYMBOL_GPL(comedi_alloc_devpriv);

int comedi_alloc_subdevices(struct comedi_device *dev, int num_subdevices)
{
	struct comedi_subdevice *s;
	int i;

	if (num_subdevices < 1)
		return -EINVAL;

	s = kcalloc(num_subdevices, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	dev->subdevices = s;
	dev->n_subdevices = num_subdevices;

	for (i = 0; i < num_subdevices; ++i) {
		s = &dev->subdevices[i];
		s->device = dev;
		s->index = i;
		s->async_dma_dir = DMA_NONE;
		spin_lock_init(&s->spin_lock);
		s->minor = -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(comedi_alloc_subdevices);

/**
 * comedi_alloc_subdev_readback() - Allocate memory for the subdevice readback.
 * @s: comedi_subdevice struct
 */
int comedi_alloc_subdev_readback(struct comedi_subdevice *s)
{
	if (!s->n_chan)
		return -EINVAL;

	s->readback = kcalloc(s->n_chan, sizeof(*s->readback), GFP_KERNEL);
	if (!s->readback)
		return -ENOMEM;

	if (!s->insn_read)
		s->insn_read = comedi_readback_insn_read;

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_alloc_subdev_readback);

static void comedi_device_detach_cleanup(struct comedi_device *dev)
{
	int i;
	struct comedi_subdevice *s;

	if (dev->subdevices) {
		for (i = 0; i < dev->n_subdevices; i++) {
			s = &dev->subdevices[i];
			if (s->runflags & COMEDI_SRF_FREE_SPRIV)
				kfree(s->private);
			comedi_free_subdevice_minor(s);
			if (s->async) {
				comedi_buf_alloc(dev, s, 0);
				kfree(s->async);
			}
			kfree(s->readback);
		}
		kfree(dev->subdevices);
		dev->subdevices = NULL;
		dev->n_subdevices = 0;
	}
	kfree(dev->private);
	dev->private = NULL;
	dev->driver = NULL;
	dev->board_name = NULL;
	dev->board_ptr = NULL;
	dev->mmio = NULL;
	dev->iobase = 0;
	dev->iolen = 0;
	dev->ioenabled = false;
	dev->irq = 0;
	dev->read_subdev = NULL;
	dev->write_subdev = NULL;
	dev->open = NULL;
	dev->close = NULL;
	comedi_clear_hw_dev(dev);
}

void comedi_device_detach(struct comedi_device *dev)
{
	comedi_device_cancel_all(dev);
	down_write(&dev->attach_lock);
	dev->attached = false;
	dev->detach_count++;
	if (dev->driver)
		dev->driver->detach(dev);
	comedi_device_detach_cleanup(dev);
	up_write(&dev->attach_lock);
}

static int poll_invalid(struct comedi_device *dev, struct comedi_subdevice *s)
{
	return -EINVAL;
}

int insn_inval(struct comedi_device *dev, struct comedi_subdevice *s,
	       struct comedi_insn *insn, unsigned int *data)
{
	return -EINVAL;
}

/**
 * comedi_readback_insn_read() - A generic (*insn_read) for subdevice readback.
 * @dev: comedi_device struct
 * @s: comedi_subdevice struct
 * @insn: comedi_insn struct
 * @data: pointer to return the readback data
 */
int comedi_readback_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	if (!s->readback)
		return -EINVAL;

	for (i = 0; i < insn->n; i++)
		data[i] = s->readback[chan];

	return insn->n;
}
EXPORT_SYMBOL_GPL(comedi_readback_insn_read);

/**
 * comedi_timeout() - busy-wait for a driver condition to occur.
 * @dev: comedi_device struct
 * @s: comedi_subdevice struct
 * @insn: comedi_insn struct
 * @cb: callback to check for the condition
 * @context: private context from the driver
 */
int comedi_timeout(struct comedi_device *dev,
		   struct comedi_subdevice *s,
		   struct comedi_insn *insn,
		   int (*cb)(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context),
		   unsigned long context)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(COMEDI_TIMEOUT_MS);
	int ret;

	while (time_before(jiffies, timeout)) {
		ret = cb(dev, s, insn, context);
		if (ret != -EBUSY)
			return ret;	/* success (0) or non EBUSY errno */
		cpu_relax();
	}
	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(comedi_timeout);

/**
 * comedi_dio_insn_config() - boilerplate (*insn_config) for DIO subdevices.
 * @dev: comedi_device struct
 * @s: comedi_subdevice struct
 * @insn: comedi_insn struct
 * @data: parameters for the @insn
 * @mask: io_bits mask for grouped channels
 */
int comedi_dio_insn_config(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data,
			   unsigned int mask)
{
	unsigned int chan_mask = 1 << CR_CHAN(insn->chanspec);

	if (!mask)
		mask = chan_mask;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;

	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;

	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;

	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_dio_insn_config);

/**
 * comedi_dio_update_state() - update the internal state of DIO subdevices.
 * @s: comedi_subdevice struct
 * @data: the channel mask and bits to update
 */
unsigned int comedi_dio_update_state(struct comedi_subdevice *s,
				     unsigned int *data)
{
	unsigned int chanmask = (s->n_chan < 32) ? ((1 << s->n_chan) - 1)
						 : 0xffffffff;
	unsigned int mask = data[0] & chanmask;
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);
	}

	return mask;
}
EXPORT_SYMBOL_GPL(comedi_dio_update_state);

/**
 * comedi_bytes_per_scan - get length of asynchronous command "scan" in bytes
 * @s: comedi_subdevice struct
 *
 * Determines the overall scan length according to the subdevice type and the
 * number of channels in the scan.
 *
 * For digital input, output or input/output subdevices, samples for multiple
 * channels are assumed to be packed into one or more unsigned short or
 * unsigned int values according to the subdevice's SDF_LSAMPL flag.  For other
 * types of subdevice, samples are assumed to occupy a whole unsigned short or
 * unsigned int according to the SDF_LSAMPL flag.
 *
 * Returns the overall scan length in bytes.
 */
unsigned int comedi_bytes_per_scan(struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int num_samples;
	unsigned int bits_per_sample;

	switch (s->type) {
	case COMEDI_SUBD_DI:
	case COMEDI_SUBD_DO:
	case COMEDI_SUBD_DIO:
		bits_per_sample = 8 * comedi_bytes_per_sample(s);
		num_samples = DIV_ROUND_UP(cmd->scan_end_arg, bits_per_sample);
		break;
	default:
		num_samples = cmd->scan_end_arg;
		break;
	}
	return comedi_samples_to_bytes(s, num_samples);
}
EXPORT_SYMBOL_GPL(comedi_bytes_per_scan);

/**
 * comedi_nscans_left - return the number of scans left in the command
 * @s: comedi_subdevice struct
 * @nscans: the expected number of scans
 *
 * If nscans is 0, the number of scans available in the async buffer will be
 * used. Otherwise the expected number of scans will be used.
 *
 * If the async command has a stop_src of TRIG_COUNT, the nscans will be
 * checked against the number of scans left in the command.
 *
 * The return value will then be either the expected number of scans or the
 * number of scans remaining in the command.
 */
unsigned int comedi_nscans_left(struct comedi_subdevice *s,
				unsigned int nscans)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	if (nscans == 0) {
		unsigned int nbytes = comedi_buf_read_n_available(s);

		nscans = nbytes / comedi_bytes_per_scan(s);
	}

	if (cmd->stop_src == TRIG_COUNT) {
		unsigned int scans_left = 0;

		if (async->scans_done < cmd->stop_arg)
			scans_left = cmd->stop_arg - async->scans_done;

		if (nscans > scans_left)
			nscans = scans_left;
	}
	return nscans;
}
EXPORT_SYMBOL_GPL(comedi_nscans_left);

/**
 * comedi_nsamples_left - return the number of samples left in the command
 * @s: comedi_subdevice struct
 * @nsamples: the expected number of samples
 *
 * Returns the expected number of samples of the number of samples remaining
 * in the command.
 */
unsigned int comedi_nsamples_left(struct comedi_subdevice *s,
				  unsigned int nsamples)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	if (cmd->stop_src == TRIG_COUNT) {
		/* +1 to force comedi_nscans_left() to return the scans left */
		unsigned int nscans = (nsamples / cmd->scan_end_arg) + 1;
		unsigned int scans_left = comedi_nscans_left(s, nscans);
		unsigned int scan_pos =
		    comedi_bytes_to_samples(s, async->scan_progress);
		unsigned long long samples_left = 0;

		if (scans_left) {
			samples_left = ((unsigned long long)scans_left *
					cmd->scan_end_arg) - scan_pos;
		}

		if (samples_left < nsamples)
			nsamples = samples_left;
	}
	return nsamples;
}
EXPORT_SYMBOL_GPL(comedi_nsamples_left);

/**
 * comedi_inc_scan_progress - update scan progress in asynchronous command
 * @s: comedi_subdevice struct
 * @num_bytes: amount of data in bytes to increment scan progress
 *
 * Increments the scan progress by the number of bytes specified by num_bytes.
 * If the scan progress reaches or exceeds the scan length in bytes, reduce
 * it modulo the scan length in bytes and set the "end of scan" asynchronous
 * event flag to be processed later.
 */
void comedi_inc_scan_progress(struct comedi_subdevice *s,
			      unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int scan_length = comedi_bytes_per_scan(s);

	/* track the 'cur_chan' for non-SDF_PACKED subdevices */
	if (!(s->subdev_flags & SDF_PACKED)) {
		async->cur_chan += comedi_bytes_to_samples(s, num_bytes);
		async->cur_chan %= cmd->chanlist_len;
	}

	async->scan_progress += num_bytes;
	if (async->scan_progress >= scan_length) {
		unsigned int nscans = async->scan_progress / scan_length;

		if (async->scans_done < (UINT_MAX - nscans))
			async->scans_done += nscans;
		else
			async->scans_done = UINT_MAX;

		async->scan_progress %= scan_length;
		async->events |= COMEDI_CB_EOS;
	}
}
EXPORT_SYMBOL_GPL(comedi_inc_scan_progress);

/**
 * comedi_handle_events - handle events and possibly stop acquisition
 * @dev: comedi_device struct
 * @s: comedi_subdevice struct
 *
 * Handles outstanding asynchronous acquisition event flags associated
 * with the subdevice.  Call the subdevice's "->cancel()" handler if the
 * "end of acquisition", "error" or "overflow" event flags are set in order
 * to stop the acquisition at the driver level.
 *
 * Calls comedi_event() to further process the event flags, which may mark
 * the asynchronous command as no longer running, possibly terminated with
 * an error, and may wake up tasks.
 *
 * Return a bit-mask of the handled events.
 */
unsigned int comedi_handle_events(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	unsigned int events = s->async->events;

	if (events == 0)
		return events;

	if (events & COMEDI_CB_CANCEL_MASK)
		s->cancel(dev, s);

	comedi_event(dev, s);

	return events;
}
EXPORT_SYMBOL_GPL(comedi_handle_events);

static int insn_rw_emulate_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_insn new_insn;
	int ret;
	static const unsigned channels_per_bitfield = 32;

	unsigned chan = CR_CHAN(insn->chanspec);
	const unsigned base_bitfield_channel =
	    (chan < channels_per_bitfield) ? 0 : chan;
	unsigned int new_data[2];

	memset(new_data, 0, sizeof(new_data));
	memset(&new_insn, 0, sizeof(new_insn));
	new_insn.insn = INSN_BITS;
	new_insn.chanspec = base_bitfield_channel;
	new_insn.n = 2;
	new_insn.subdev = insn->subdev;

	if (insn->insn == INSN_WRITE) {
		if (!(s->subdev_flags & SDF_WRITABLE))
			return -EINVAL;
		new_data[0] = 1 << (chan - base_bitfield_channel); /* mask */
		new_data[1] = data[0] ? (1 << (chan - base_bitfield_channel))
			      : 0; /* bits */
	}

	ret = s->insn_bits(dev, s, &new_insn, new_data);
	if (ret < 0)
		return ret;

	if (insn->insn == INSN_READ)
		data[0] = (new_data[1] >> (chan - base_bitfield_channel)) & 1;

	return 1;
}

static int __comedi_device_postconfig_async(struct comedi_device *dev,
					    struct comedi_subdevice *s)
{
	struct comedi_async *async;
	unsigned int buf_size;
	int ret;

	if ((s->subdev_flags & (SDF_CMD_READ | SDF_CMD_WRITE)) == 0) {
		dev_warn(dev->class_dev,
			 "async subdevices must support SDF_CMD_READ or SDF_CMD_WRITE\n");
		return -EINVAL;
	}
	if (!s->do_cmdtest) {
		dev_warn(dev->class_dev,
			 "async subdevices must have a do_cmdtest() function\n");
		return -EINVAL;
	}

	async = kzalloc(sizeof(*async), GFP_KERNEL);
	if (!async)
		return -ENOMEM;

	init_waitqueue_head(&async->wait_head);
	s->async = async;

	async->max_bufsize = comedi_default_buf_maxsize_kb * 1024;
	buf_size = comedi_default_buf_size_kb * 1024;
	if (buf_size > async->max_bufsize)
		buf_size = async->max_bufsize;

	if (comedi_buf_alloc(dev, s, buf_size) < 0) {
		dev_warn(dev->class_dev, "Buffer allocation failed\n");
		return -ENOMEM;
	}
	if (s->buf_change) {
		ret = s->buf_change(dev, s);
		if (ret < 0)
			return ret;
	}

	comedi_alloc_subdevice_minor(s);

	return 0;
}

static int __comedi_device_postconfig(struct comedi_device *dev)
{
	struct comedi_subdevice *s;
	int ret;
	int i;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];

		if (s->type == COMEDI_SUBD_UNUSED)
			continue;

		if (s->type == COMEDI_SUBD_DO) {
			if (s->n_chan < 32)
				s->io_bits = (1 << s->n_chan) - 1;
			else
				s->io_bits = 0xffffffff;
		}

		if (s->len_chanlist == 0)
			s->len_chanlist = 1;

		if (s->do_cmd) {
			ret = __comedi_device_postconfig_async(dev, s);
			if (ret)
				return ret;
		}

		if (!s->range_table && !s->range_table_list)
			s->range_table = &range_unknown;

		if (!s->insn_read && s->insn_bits)
			s->insn_read = insn_rw_emulate_bits;
		if (!s->insn_write && s->insn_bits)
			s->insn_write = insn_rw_emulate_bits;

		if (!s->insn_read)
			s->insn_read = insn_inval;
		if (!s->insn_write)
			s->insn_write = insn_inval;
		if (!s->insn_bits)
			s->insn_bits = insn_inval;
		if (!s->insn_config)
			s->insn_config = insn_inval;

		if (!s->poll)
			s->poll = poll_invalid;
	}

	return 0;
}

/* do a little post-config cleanup */
static int comedi_device_postconfig(struct comedi_device *dev)
{
	int ret;

	ret = __comedi_device_postconfig(dev);
	if (ret < 0)
		return ret;
	down_write(&dev->attach_lock);
	dev->attached = true;
	up_write(&dev->attach_lock);
	return 0;
}

/*
 * Generic recognize function for drivers that register their supported
 * board names.
 *
 * 'driv->board_name' points to a 'const char *' member within the
 * zeroth element of an array of some private board information
 * structure, say 'struct foo_board' containing a member 'const char
 * *board_name' that is initialized to point to a board name string that
 * is one of the candidates matched against this function's 'name'
 * parameter.
 *
 * 'driv->offset' is the size of the private board information
 * structure, say 'sizeof(struct foo_board)', and 'driv->num_names' is
 * the length of the array of private board information structures.
 *
 * If one of the board names in the array of private board information
 * structures matches the name supplied to this function, the function
 * returns a pointer to the pointer to the board name, otherwise it
 * returns NULL.  The return value ends up in the 'board_ptr' member of
 * a 'struct comedi_device' that the low-level comedi driver's
 * 'attach()' hook can convert to a point to a particular element of its
 * array of private board information structures by subtracting the
 * offset of the member that points to the board name.  (No subtraction
 * is required if the board name pointer is the first member of the
 * private board information structure, which is generally the case.)
 */
static void *comedi_recognize(struct comedi_driver *driv, const char *name)
{
	char **name_ptr = (char **)driv->board_name;
	int i;

	for (i = 0; i < driv->num_names; i++) {
		if (strcmp(*name_ptr, name) == 0)
			return name_ptr;
		name_ptr = (void *)name_ptr + driv->offset;
	}

	return NULL;
}

static void comedi_report_boards(struct comedi_driver *driv)
{
	unsigned int i;
	const char *const *name_ptr;

	pr_info("comedi: valid board names for %s driver are:\n",
		driv->driver_name);

	name_ptr = driv->board_name;
	for (i = 0; i < driv->num_names; i++) {
		pr_info(" %s\n", *name_ptr);
		name_ptr = (const char **)((char *)name_ptr + driv->offset);
	}

	if (driv->num_names == 0)
		pr_info(" %s\n", driv->driver_name);
}

/**
 * comedi_load_firmware() - Request and load firmware for a device.
 * @dev: comedi_device struct
 * @hw_device: device struct for the comedi_device
 * @name: the name of the firmware image
 * @cb: callback to the upload the firmware image
 * @context: private context from the driver
 */
int comedi_load_firmware(struct comedi_device *dev,
			 struct device *device,
			 const char *name,
			 int (*cb)(struct comedi_device *dev,
				   const u8 *data, size_t size,
				   unsigned long context),
			 unsigned long context)
{
	const struct firmware *fw;
	int ret;

	if (!cb)
		return -EINVAL;

	ret = request_firmware(&fw, name, device);
	if (ret == 0) {
		ret = cb(dev, fw->data, fw->size, context);
		release_firmware(fw);
	}

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(comedi_load_firmware);

/**
 * __comedi_request_region() - Request an I/O reqion for a legacy driver.
 * @dev: comedi_device struct
 * @start: base address of the I/O reqion
 * @len: length of the I/O region
 */
int __comedi_request_region(struct comedi_device *dev,
			    unsigned long start, unsigned long len)
{
	if (!start) {
		dev_warn(dev->class_dev,
			 "%s: a I/O base address must be specified\n",
			 dev->board_name);
		return -EINVAL;
	}

	if (!request_region(start, len, dev->board_name)) {
		dev_warn(dev->class_dev, "%s: I/O port conflict (%#lx,%lu)\n",
			 dev->board_name, start, len);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__comedi_request_region);

/**
 * comedi_request_region() - Request an I/O reqion for a legacy driver.
 * @dev: comedi_device struct
 * @start: base address of the I/O reqion
 * @len: length of the I/O region
 */
int comedi_request_region(struct comedi_device *dev,
			  unsigned long start, unsigned long len)
{
	int ret;

	ret = __comedi_request_region(dev, start, len);
	if (ret == 0) {
		dev->iobase = start;
		dev->iolen = len;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(comedi_request_region);

/**
 * comedi_legacy_detach() - A generic (*detach) function for legacy drivers.
 * @dev: comedi_device struct
 */
void comedi_legacy_detach(struct comedi_device *dev)
{
	if (dev->irq) {
		free_irq(dev->irq, dev);
		dev->irq = 0;
	}
	if (dev->iobase && dev->iolen) {
		release_region(dev->iobase, dev->iolen);
		dev->iobase = 0;
		dev->iolen = 0;
	}
}
EXPORT_SYMBOL_GPL(comedi_legacy_detach);

int comedi_device_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_driver *driv;
	int ret;

	if (dev->attached)
		return -EBUSY;

	mutex_lock(&comedi_drivers_list_lock);
	for (driv = comedi_drivers; driv; driv = driv->next) {
		if (!try_module_get(driv->module))
			continue;
		if (driv->num_names) {
			dev->board_ptr = comedi_recognize(driv, it->board_name);
			if (dev->board_ptr)
				break;
		} else if (strcmp(driv->driver_name, it->board_name) == 0) {
			break;
		}
		module_put(driv->module);
	}
	if (driv == NULL) {
		/*  recognize has failed if we get here */
		/*  report valid board names before returning error */
		for (driv = comedi_drivers; driv; driv = driv->next) {
			if (!try_module_get(driv->module))
				continue;
			comedi_report_boards(driv);
			module_put(driv->module);
		}
		ret = -EIO;
		goto out;
	}
	if (driv->attach == NULL) {
		/* driver does not support manual configuration */
		dev_warn(dev->class_dev,
			 "driver '%s' does not support attach using comedi_config\n",
			 driv->driver_name);
		module_put(driv->module);
		ret = -ENOSYS;
		goto out;
	}
	dev->driver = driv;
	dev->board_name = dev->board_ptr ? *(const char **)dev->board_ptr
					 : dev->driver->driver_name;
	ret = driv->attach(dev, it);
	if (ret >= 0)
		ret = comedi_device_postconfig(dev);
	if (ret < 0) {
		comedi_device_detach(dev);
		module_put(driv->module);
	}
	/* On success, the driver module count has been incremented. */
out:
	mutex_unlock(&comedi_drivers_list_lock);
	return ret;
}

int comedi_auto_config(struct device *hardware_device,
		       struct comedi_driver *driver, unsigned long context)
{
	struct comedi_device *dev;
	int ret;

	if (!hardware_device) {
		pr_warn("BUG! comedi_auto_config called with NULL hardware_device\n");
		return -EINVAL;
	}
	if (!driver) {
		dev_warn(hardware_device,
			 "BUG! comedi_auto_config called with NULL comedi driver\n");
		return -EINVAL;
	}

	if (!driver->auto_attach) {
		dev_warn(hardware_device,
			 "BUG! comedi driver '%s' has no auto_attach handler\n",
			 driver->driver_name);
		return -EINVAL;
	}

	dev = comedi_alloc_board_minor(hardware_device);
	if (IS_ERR(dev)) {
		dev_warn(hardware_device,
			 "driver '%s' could not create device.\n",
			 driver->driver_name);
		return PTR_ERR(dev);
	}
	/* Note: comedi_alloc_board_minor() locked dev->mutex. */

	dev->driver = driver;
	dev->board_name = dev->driver->driver_name;
	ret = driver->auto_attach(dev, context);
	if (ret >= 0)
		ret = comedi_device_postconfig(dev);
	mutex_unlock(&dev->mutex);

	if (ret < 0) {
		dev_warn(hardware_device,
			 "driver '%s' failed to auto-configure device.\n",
			 driver->driver_name);
		comedi_release_hardware_device(hardware_device);
	} else {
		/*
		 * class_dev should be set properly here
		 *  after a successful auto config
		 */
		dev_info(dev->class_dev,
			 "driver '%s' has successfully auto-configured '%s'.\n",
			 driver->driver_name, dev->board_name);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_auto_config);

void comedi_auto_unconfig(struct device *hardware_device)
{
	if (hardware_device == NULL)
		return;
	comedi_release_hardware_device(hardware_device);
}
EXPORT_SYMBOL_GPL(comedi_auto_unconfig);

int comedi_driver_register(struct comedi_driver *driver)
{
	mutex_lock(&comedi_drivers_list_lock);
	driver->next = comedi_drivers;
	comedi_drivers = driver;
	mutex_unlock(&comedi_drivers_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_driver_register);

void comedi_driver_unregister(struct comedi_driver *driver)
{
	struct comedi_driver *prev;
	int i;

	/* unlink the driver */
	mutex_lock(&comedi_drivers_list_lock);
	if (comedi_drivers == driver) {
		comedi_drivers = driver->next;
	} else {
		for (prev = comedi_drivers; prev->next; prev = prev->next) {
			if (prev->next == driver) {
				prev->next = driver->next;
				break;
			}
		}
	}
	mutex_unlock(&comedi_drivers_list_lock);

	/* check for devices using this driver */
	for (i = 0; i < COMEDI_NUM_BOARD_MINORS; i++) {
		struct comedi_device *dev = comedi_dev_get_from_minor(i);

		if (!dev)
			continue;

		mutex_lock(&dev->mutex);
		if (dev->attached && dev->driver == driver) {
			if (dev->use_count)
				dev_warn(dev->class_dev,
					 "BUG! detaching device with use_count=%d\n",
					 dev->use_count);
			comedi_device_detach(dev);
		}
		mutex_unlock(&dev->mutex);
		comedi_dev_put(dev);
	}
}
EXPORT_SYMBOL_GPL(comedi_driver_unregister);
