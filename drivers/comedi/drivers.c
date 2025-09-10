// SPDX-License-Identifier: GPL-2.0+
/*
 *  module/drivers.c
 *  functions for manipulating drivers
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 *  Copyright (C) 2002 Frank Mori Hess <fmhess@users.sourceforge.net>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/comedi/comedidev.h>
#include "comedi_internal.h"

struct comedi_driver *comedi_drivers;
/* protects access to comedi_drivers */
DEFINE_MUTEX(comedi_drivers_list_lock);

/**
 * comedi_set_hw_dev() - Set hardware device associated with COMEDI device
 * @dev: COMEDI device.
 * @hw_dev: Hardware device.
 *
 * For automatically configured COMEDI devices (resulting from a call to
 * comedi_auto_config() or one of its wrappers from the low-level COMEDI
 * driver), comedi_set_hw_dev() is called automatically by the COMEDI core
 * to associate the COMEDI device with the hardware device.  It can also be
 * called directly by "legacy" low-level COMEDI drivers that rely on the
 * %COMEDI_DEVCONFIG ioctl to configure the hardware as long as the hardware
 * has a &struct device.
 *
 * If @dev->hw_dev is NULL, it gets a reference to @hw_dev and sets
 * @dev->hw_dev, otherwise, it does nothing.  Calling it multiple times
 * with the same hardware device is not considered an error.  If it gets
 * a reference to the hardware device, it will be automatically 'put' when
 * the device is detached from COMEDI.
 *
 * Returns 0 if @dev->hw_dev was NULL or the same as @hw_dev, otherwise
 * returns -EEXIST.
 */
int comedi_set_hw_dev(struct comedi_device *dev, struct device *hw_dev)
{
	if (hw_dev == dev->hw_dev)
		return 0;
	if (dev->hw_dev)
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
 * comedi_alloc_devpriv() - Allocate memory for the device private data
 * @dev: COMEDI device.
 * @size: Size of the memory to allocate.
 *
 * The allocated memory is zero-filled.  @dev->private points to it on
 * return.  The memory will be automatically freed when the COMEDI device is
 * "detached".
 *
 * Returns a pointer to the allocated memory, or NULL on failure.
 */
void *comedi_alloc_devpriv(struct comedi_device *dev, size_t size)
{
	dev->private = kzalloc(size, GFP_KERNEL);
	return dev->private;
}
EXPORT_SYMBOL_GPL(comedi_alloc_devpriv);

/**
 * comedi_alloc_subdevices() - Allocate subdevices for COMEDI device
 * @dev: COMEDI device.
 * @num_subdevices: Number of subdevices to allocate.
 *
 * Allocates and initializes an array of &struct comedi_subdevice for the
 * COMEDI device.  If successful, sets @dev->subdevices to point to the
 * first one and @dev->n_subdevices to the number.
 *
 * Returns 0 on success, -EINVAL if @num_subdevices is < 1, or -ENOMEM if
 * failed to allocate the memory.
 */
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
 * comedi_alloc_subdev_readback() - Allocate memory for the subdevice readback
 * @s: COMEDI subdevice.
 *
 * This is called by low-level COMEDI drivers to allocate an array to record
 * the last values written to a subdevice's analog output channels (at least
 * by the %INSN_WRITE instruction), to allow them to be read back by an
 * %INSN_READ instruction.  It also provides a default handler for the
 * %INSN_READ instruction unless one has already been set.
 *
 * On success, @s->readback points to the first element of the array, which
 * is zero-filled.  The low-level driver is responsible for updating its
 * contents.  @s->insn_read will be set to comedi_readback_insn_read()
 * unless it is already non-NULL.
 *
 * Returns 0 on success, -EINVAL if the subdevice has no channels, or
 * -ENOMEM on allocation failure.
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

	lockdep_assert_held_write(&dev->attach_lock);
	lockdep_assert_held(&dev->mutex);
	if (dev->subdevices) {
		for (i = 0; i < dev->n_subdevices; i++) {
			s = &dev->subdevices[i];
			if (comedi_can_auto_free_spriv(s))
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
	if (!IS_ERR(dev->pacer))
		kfree(dev->pacer);
	dev->private = NULL;
	dev->pacer = NULL;
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

void comedi_device_detach_locked(struct comedi_device *dev)
{
	lockdep_assert_held_write(&dev->attach_lock);
	lockdep_assert_held(&dev->mutex);
	comedi_device_cancel_all(dev);
	dev->attached = false;
	dev->detach_count++;
	if (dev->driver)
		dev->driver->detach(dev);
	comedi_device_detach_cleanup(dev);
}

void comedi_device_detach(struct comedi_device *dev)
{
	lockdep_assert_held(&dev->mutex);
	down_write(&dev->attach_lock);
	comedi_device_detach_locked(dev);
	up_write(&dev->attach_lock);
}

static int poll_invalid(struct comedi_device *dev, struct comedi_subdevice *s)
{
	return -EINVAL;
}

static int insn_device_inval(struct comedi_device *dev,
			     struct comedi_insn *insn, unsigned int *data)
{
	return -EINVAL;
}

static unsigned int get_zero_valid_routes(struct comedi_device *dev,
					  unsigned int n_pairs,
					  unsigned int *pair_data)
{
	return 0;
}

int insn_inval(struct comedi_device *dev, struct comedi_subdevice *s,
	       struct comedi_insn *insn, unsigned int *data)
{
	return -EINVAL;
}

/**
 * comedi_readback_insn_read() - A generic (*insn_read) for subdevice readback.
 * @dev: COMEDI device.
 * @s: COMEDI subdevice.
 * @insn: COMEDI instruction.
 * @data: Pointer to return the readback data.
 *
 * Handles the %INSN_READ instruction for subdevices that use the readback
 * array allocated by comedi_alloc_subdev_readback().  It may be used
 * directly as the subdevice's handler (@s->insn_read) or called via a
 * wrapper.
 *
 * @insn->n is normally 1, which will read a single value.  If higher, the
 * same element of the readback array will be read multiple times.
 *
 * Returns @insn->n on success, or -EINVAL if @s->readback is NULL.
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
 * comedi_timeout() - Busy-wait for a driver condition to occur
 * @dev: COMEDI device.
 * @s: COMEDI subdevice.
 * @insn: COMEDI instruction.
 * @cb: Callback to check for the condition.
 * @context: Private context from the driver.
 *
 * Busy-waits for up to a second (%COMEDI_TIMEOUT_MS) for the condition or
 * some error (other than -EBUSY) to occur.  The parameters @dev, @s, @insn,
 * and @context are passed to the callback function, which returns -EBUSY to
 * continue waiting or some other value to stop waiting (generally 0 if the
 * condition occurred, or some error value).
 *
 * Returns -ETIMEDOUT if timed out, otherwise the return value from the
 * callback function.
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
 * comedi_dio_insn_config() - Boilerplate (*insn_config) for DIO subdevices
 * @dev: COMEDI device.
 * @s: COMEDI subdevice.
 * @insn: COMEDI instruction.
 * @data: Instruction parameters and return data.
 * @mask: io_bits mask for grouped channels, or 0 for single channel.
 *
 * If @mask is 0, it is replaced with a single-bit mask corresponding to the
 * channel number specified by @insn->chanspec.  Otherwise, @mask
 * corresponds to a group of channels (which should include the specified
 * channel) that are always configured together as inputs or outputs.
 *
 * Partially handles the %INSN_CONFIG_DIO_INPUT, %INSN_CONFIG_DIO_OUTPUTS,
 * and %INSN_CONFIG_DIO_QUERY instructions.  The first two update
 * @s->io_bits to record the directions of the masked channels.  The last
 * one sets @data[1] to the current direction of the group of channels
 * (%COMEDI_INPUT) or %COMEDI_OUTPUT) as recorded in @s->io_bits.
 *
 * The caller is responsible for updating the DIO direction in the hardware
 * registers if this function returns 0.
 *
 * Returns 0 for a %INSN_CONFIG_DIO_INPUT or %INSN_CONFIG_DIO_OUTPUT
 * instruction, @insn->n (> 0) for a %INSN_CONFIG_DIO_QUERY instruction, or
 * -EINVAL for some other instruction.
 */
int comedi_dio_insn_config(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data,
			   unsigned int mask)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	if (!mask && chan < 32)
		mask = 1U << chan;

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
 * comedi_dio_update_state() - Update the internal state of DIO subdevices
 * @s: COMEDI subdevice.
 * @data: The channel mask and bits to update.
 *
 * Updates @s->state which holds the internal state of the outputs for DIO
 * or DO subdevices (up to 32 channels).  @data[0] contains a bit-mask of
 * the channels to be updated.  @data[1] contains a bit-mask of those
 * channels to be set to '1'.  The caller is responsible for updating the
 * outputs in hardware according to @s->state.  As a minimum, the channels
 * in the returned bit-mask need to be updated.
 *
 * Returns @mask with non-existent channels removed.
 */
unsigned int comedi_dio_update_state(struct comedi_subdevice *s,
				     unsigned int *data)
{
	unsigned int chanmask = (s->n_chan < 32) ? ((1U << s->n_chan) - 1)
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
 * comedi_bytes_per_scan_cmd() - Get length of asynchronous command "scan" in
 * bytes
 * @s: COMEDI subdevice.
 * @cmd: COMEDI command.
 *
 * Determines the overall scan length according to the subdevice type and the
 * number of channels in the scan for the specified command.
 *
 * For digital input, output or input/output subdevices, samples for
 * multiple channels are assumed to be packed into one or more unsigned
 * short or unsigned int values according to the subdevice's %SDF_LSAMPL
 * flag.  For other types of subdevice, samples are assumed to occupy a
 * whole unsigned short or unsigned int according to the %SDF_LSAMPL flag.
 *
 * Returns the overall scan length in bytes.
 */
unsigned int comedi_bytes_per_scan_cmd(struct comedi_subdevice *s,
				       struct comedi_cmd *cmd)
{
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
EXPORT_SYMBOL_GPL(comedi_bytes_per_scan_cmd);

/**
 * comedi_bytes_per_scan() - Get length of asynchronous command "scan" in bytes
 * @s: COMEDI subdevice.
 *
 * Determines the overall scan length according to the subdevice type and the
 * number of channels in the scan for the current command.
 *
 * For digital input, output or input/output subdevices, samples for
 * multiple channels are assumed to be packed into one or more unsigned
 * short or unsigned int values according to the subdevice's %SDF_LSAMPL
 * flag.  For other types of subdevice, samples are assumed to occupy a
 * whole unsigned short or unsigned int according to the %SDF_LSAMPL flag.
 *
 * Returns the overall scan length in bytes.
 */
unsigned int comedi_bytes_per_scan(struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	return comedi_bytes_per_scan_cmd(s, cmd);
}
EXPORT_SYMBOL_GPL(comedi_bytes_per_scan);

static unsigned int __comedi_nscans_left(struct comedi_subdevice *s,
					 unsigned int nscans)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	if (cmd->stop_src == TRIG_COUNT) {
		unsigned int scans_left = 0;

		if (async->scans_done < cmd->stop_arg)
			scans_left = cmd->stop_arg - async->scans_done;

		if (nscans > scans_left)
			nscans = scans_left;
	}
	return nscans;
}

/**
 * comedi_nscans_left() - Return the number of scans left in the command
 * @s: COMEDI subdevice.
 * @nscans: The expected number of scans or 0 for all available scans.
 *
 * If @nscans is 0, it is set to the number of scans available in the
 * async buffer.
 *
 * If the async command has a stop_src of %TRIG_COUNT, the @nscans will be
 * checked against the number of scans remaining to complete the command.
 *
 * The return value will then be either the expected number of scans or the
 * number of scans remaining to complete the command, whichever is fewer.
 */
unsigned int comedi_nscans_left(struct comedi_subdevice *s,
				unsigned int nscans)
{
	if (nscans == 0) {
		unsigned int nbytes = comedi_buf_read_n_available(s);

		nscans = nbytes / comedi_bytes_per_scan(s);
	}
	return __comedi_nscans_left(s, nscans);
}
EXPORT_SYMBOL_GPL(comedi_nscans_left);

/**
 * comedi_nsamples_left() - Return the number of samples left in the command
 * @s: COMEDI subdevice.
 * @nsamples: The expected number of samples.
 *
 * Returns the number of samples remaining to complete the command, or the
 * specified expected number of samples (@nsamples), whichever is fewer.
 */
unsigned int comedi_nsamples_left(struct comedi_subdevice *s,
				  unsigned int nsamples)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned long long scans_left;
	unsigned long long samples_left;

	if (cmd->stop_src != TRIG_COUNT)
		return nsamples;

	scans_left = __comedi_nscans_left(s, cmd->stop_arg);
	if (!scans_left)
		return 0;

	samples_left = scans_left * cmd->scan_end_arg -
		comedi_bytes_to_samples(s, async->scan_progress);

	if (samples_left < nsamples)
		return samples_left;
	return nsamples;
}
EXPORT_SYMBOL_GPL(comedi_nsamples_left);

/**
 * comedi_inc_scan_progress() - Update scan progress in asynchronous command
 * @s: COMEDI subdevice.
 * @num_bytes: Amount of data in bytes to increment scan progress.
 *
 * Increments the scan progress by the number of bytes specified by @num_bytes.
 * If the scan progress reaches or exceeds the scan length in bytes, reduce
 * it modulo the scan length in bytes and set the "end of scan" asynchronous
 * event flag (%COMEDI_CB_EOS) to be processed later.
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
 * comedi_handle_events() - Handle events and possibly stop acquisition
 * @dev: COMEDI device.
 * @s: COMEDI subdevice.
 *
 * Handles outstanding asynchronous acquisition event flags associated
 * with the subdevice.  Call the subdevice's @s->cancel() handler if the
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

	if ((events & COMEDI_CB_CANCEL_MASK) && s->cancel)
		s->cancel(dev, s);

	comedi_event(dev, s);

	return events;
}
EXPORT_SYMBOL_GPL(comedi_handle_events);

static int insn_rw_emulate_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct comedi_insn _insn;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int base_chan = (chan < 32) ? 0 : chan;
	unsigned int _data[2];
	int ret;

	if (insn->n == 0)
		return 0;

	memset(_data, 0, sizeof(_data));
	memset(&_insn, 0, sizeof(_insn));
	_insn.insn = INSN_BITS;
	_insn.chanspec = base_chan;
	_insn.n = 2;
	_insn.subdev = insn->subdev;

	if (insn->insn == INSN_WRITE) {
		if (!(s->subdev_flags & SDF_WRITABLE))
			return -EINVAL;
		_data[0] = 1U << (chan - base_chan);		     /* mask */
		_data[1] = data[0] ? (1U << (chan - base_chan)) : 0; /* bits */
	}

	ret = s->insn_bits(dev, s, &_insn, _data);
	if (ret < 0)
		return ret;

	if (insn->insn == INSN_READ)
		data[0] = (_data[1] >> (chan - base_chan)) & 1;

	return 1;
}

static int __comedi_device_postconfig_async(struct comedi_device *dev,
					    struct comedi_subdevice *s)
{
	struct comedi_async *async;
	unsigned int buf_size;
	int ret;

	lockdep_assert_held(&dev->mutex);
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
	if (!s->cancel)
		dev_warn(dev->class_dev,
			 "async subdevices should have a cancel() function\n");

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

	lockdep_assert_held(&dev->mutex);
	if (!dev->insn_device_config)
		dev->insn_device_config = insn_device_inval;

	if (!dev->get_valid_routes)
		dev->get_valid_routes = get_zero_valid_routes;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];

		if (s->type == COMEDI_SUBD_UNUSED)
			continue;

		if (s->type == COMEDI_SUBD_DO) {
			if (s->n_chan < 32)
				s->io_bits = (1U << s->n_chan) - 1;
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

	lockdep_assert_held(&dev->mutex);
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
 * comedi_load_firmware() - Request and load firmware for a device
 * @dev: COMEDI device.
 * @device: Hardware device.
 * @name: The name of the firmware image.
 * @cb: Callback to the upload the firmware image.
 * @context: Private context from the driver.
 *
 * Sends a firmware request for the hardware device and waits for it.  Calls
 * the callback function to upload the firmware to the device, them releases
 * the firmware.
 *
 * Returns 0 on success, -EINVAL if @cb is NULL, or a negative error number
 * from the firmware request or the callback function.
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

	return min(ret, 0);
}
EXPORT_SYMBOL_GPL(comedi_load_firmware);

/**
 * __comedi_request_region() - Request an I/O region for a legacy driver
 * @dev: COMEDI device.
 * @start: Base address of the I/O region.
 * @len: Length of the I/O region.
 *
 * Requests the specified I/O port region which must start at a non-zero
 * address.
 *
 * Returns 0 on success, -EINVAL if @start is 0, or -EIO if the request
 * fails.
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
 * comedi_request_region() - Request an I/O region for a legacy driver
 * @dev: COMEDI device.
 * @start: Base address of the I/O region.
 * @len: Length of the I/O region.
 *
 * Requests the specified I/O port region which must start at a non-zero
 * address.
 *
 * On success, @dev->iobase is set to the base address of the region and
 * @dev->iolen is set to its length.
 *
 * Returns 0 on success, -EINVAL if @start is 0, or -EIO if the request
 * fails.
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
 * comedi_legacy_detach() - A generic (*detach) function for legacy drivers
 * @dev: COMEDI device.
 *
 * This is a simple, generic 'detach' handler for legacy COMEDI devices that
 * just use a single I/O port region and possibly an IRQ and that don't need
 * any special clean-up for their private device or subdevice storage.  It
 * can also be called by a driver-specific 'detach' handler.
 *
 * If @dev->irq is non-zero, the IRQ will be freed.  If @dev->iobase and
 * @dev->iolen are both non-zero, the I/O port region will be released.
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

	lockdep_assert_held(&dev->mutex);
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
	if (!driv) {
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
	if (!driv->attach) {
		/* driver does not support manual configuration */
		dev_warn(dev->class_dev,
			 "driver '%s' does not support attach using comedi_config\n",
			 driv->driver_name);
		module_put(driv->module);
		ret = -EIO;
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

/**
 * comedi_auto_config() - Create a COMEDI device for a hardware device
 * @hardware_device: Hardware device.
 * @driver: COMEDI low-level driver for the hardware device.
 * @context: Driver context for the auto_attach handler.
 *
 * Allocates a new COMEDI device for the hardware device and calls the
 * low-level driver's 'auto_attach' handler to set-up the hardware and
 * allocate the COMEDI subdevices.  Additional "post-configuration" setting
 * up is performed on successful return from the 'auto_attach' handler.
 * If the 'auto_attach' handler fails, the low-level driver's 'detach'
 * handler will be called as part of the clean-up.
 *
 * This is usually called from a wrapper function in a bus-specific COMEDI
 * module, which in turn is usually called from a bus device 'probe'
 * function in the low-level driver.
 *
 * Returns 0 on success, -EINVAL if the parameters are invalid or the
 * post-configuration determines the driver has set the COMEDI device up
 * incorrectly, -ENOMEM if failed to allocate memory, -EBUSY if run out of
 * COMEDI minor device numbers, or some negative error number returned by
 * the driver's 'auto_attach' handler.
 */
int comedi_auto_config(struct device *hardware_device,
		       struct comedi_driver *driver, unsigned long context)
{
	struct comedi_device *dev;
	int ret;

	if (!hardware_device) {
		pr_warn("BUG! %s called with NULL hardware_device\n", __func__);
		return -EINVAL;
	}
	if (!driver) {
		dev_warn(hardware_device,
			 "BUG! %s called with NULL comedi driver\n", __func__);
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
	lockdep_assert_held(&dev->mutex);

	dev->driver = driver;
	dev->board_name = dev->driver->driver_name;
	ret = driver->auto_attach(dev, context);
	if (ret >= 0)
		ret = comedi_device_postconfig(dev);

	if (ret < 0) {
		dev_warn(hardware_device,
			 "driver '%s' failed to auto-configure device.\n",
			 driver->driver_name);
		mutex_unlock(&dev->mutex);
		comedi_release_hardware_device(hardware_device);
	} else {
		/*
		 * class_dev should be set properly here
		 *  after a successful auto config
		 */
		dev_info(dev->class_dev,
			 "driver '%s' has successfully auto-configured '%s'.\n",
			 driver->driver_name, dev->board_name);
		mutex_unlock(&dev->mutex);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_auto_config);

/**
 * comedi_auto_unconfig() - Unconfigure auto-allocated COMEDI device
 * @hardware_device: Hardware device previously passed to
 *                   comedi_auto_config().
 *
 * Cleans up and eventually destroys the COMEDI device allocated by
 * comedi_auto_config() for the same hardware device.  As part of this
 * clean-up, the low-level COMEDI driver's 'detach' handler will be called.
 * (The COMEDI device itself will persist in an unattached state if it is
 * still open, until it is released, and any mmapped buffers will persist
 * until they are munmapped.)
 *
 * This is usually called from a wrapper module in a bus-specific COMEDI
 * module, which in turn is usually set as the bus device 'remove' function
 * in the low-level COMEDI driver.
 */
void comedi_auto_unconfig(struct device *hardware_device)
{
	if (!hardware_device)
		return;
	comedi_release_hardware_device(hardware_device);
}
EXPORT_SYMBOL_GPL(comedi_auto_unconfig);

/**
 * comedi_driver_register() - Register a low-level COMEDI driver
 * @driver: Low-level COMEDI driver.
 *
 * The low-level COMEDI driver is added to the list of registered COMEDI
 * drivers.  This is used by the handler for the "/proc/comedi" file and is
 * also used by the handler for the %COMEDI_DEVCONFIG ioctl to configure
 * "legacy" COMEDI devices (for those low-level drivers that support it).
 *
 * Returns 0.
 */
int comedi_driver_register(struct comedi_driver *driver)
{
	mutex_lock(&comedi_drivers_list_lock);
	driver->next = comedi_drivers;
	comedi_drivers = driver;
	mutex_unlock(&comedi_drivers_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(comedi_driver_register);

/**
 * comedi_driver_unregister() - Unregister a low-level COMEDI driver
 * @driver: Low-level COMEDI driver.
 *
 * The low-level COMEDI driver is removed from the list of registered COMEDI
 * drivers.  Detaches any COMEDI devices attached to the driver, which will
 * result in the low-level driver's 'detach' handler being called for those
 * devices before this function returns.
 */
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
