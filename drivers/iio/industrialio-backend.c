// SPDX-License-Identifier: GPL-2.0-only
/*
 * Framework to handle complex IIO aggregate devices.
 *
 * The typical architecture is to have one device as the frontend device which
 * can be "linked" against one or multiple backend devices. All the IIO and
 * userspace interface is expected to be registers/managed by the frontend
 * device which will callback into the backends when needed (to get/set some
 * configuration that it does not directly control).
 *
 *                                           -------------------------------------------------------
 * ------------------                        | ------------         ------------      -------  FPGA|
 * |     ADC        |------------------------| | ADC CORE |---------| DMA CORE |------| RAM |      |
 * | (Frontend/IIO) | Serial Data (eg: LVDS) | |(backend) |---------|          |------|     |      |
 * |                |------------------------| ------------         ------------      -------      |
 * ------------------                        -------------------------------------------------------
 *
 * The framework interface is pretty simple:
 *   - Backends should register themselves with devm_iio_backend_register()
 *   - Frontend devices should get backends with devm_iio_backend_get()
 *
 * Also to note that the primary target for this framework are converters like
 * ADC/DACs so iio_backend_ops will have some operations typical of converter
 * devices. On top of that, this is "generic" for all IIO which means any kind
 * of device can make use of the framework. That said, If the iio_backend_ops
 * struct begins to grow out of control, we can always refactor things so that
 * the industrialio-backend.c is only left with the really generic stuff. Then,
 * we can build on top of it depending on the needs.
 *
 * Copyright (C) 2023-2024 Analog Devices Inc.
 */
#define dev_fmt(fmt) "iio-backend: " fmt

#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/stringify.h>
#include <linux/types.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>

struct iio_backend {
	struct list_head entry;
	const struct iio_backend_ops *ops;
	struct device *frontend_dev;
	struct device *dev;
	struct module *owner;
	void *priv;
	const char *name;
	unsigned int cached_reg_addr;
	/*
	 * This index is relative to the frontend. Meaning that for
	 * frontends with multiple backends, this will be the index of this
	 * backend. Used for the debugfs directory name.
	 */
	u8 idx;
};

/*
 * Helper struct for requesting buffers. This ensures that we have all data
 * that we need to free the buffer in a device managed action.
 */
struct iio_backend_buffer_pair {
	struct iio_backend *back;
	struct iio_buffer *buffer;
};

static LIST_HEAD(iio_back_list);
static DEFINE_MUTEX(iio_back_lock);

/*
 * Helper macros to call backend ops. Makes sure the option is supported.
 */
#define iio_backend_check_op(back, op) ({ \
	struct iio_backend *____back = back;				\
	int ____ret = 0;						\
									\
	if (!____back->ops->op)						\
		____ret = -EOPNOTSUPP;					\
									\
	____ret;							\
})

#define iio_backend_op_call(back, op, args...) ({		\
	struct iio_backend *__back = back;			\
	int __ret;						\
								\
	__ret = iio_backend_check_op(__back, op);		\
	if (!__ret)						\
		__ret = __back->ops->op(__back, ##args);	\
								\
	__ret;							\
})

#define iio_backend_ptr_op_call(back, op, args...) ({		\
	struct iio_backend *__back = back;			\
	void *ptr_err;						\
	int __ret;						\
								\
	__ret = iio_backend_check_op(__back, op);		\
	if (__ret)						\
		ptr_err = ERR_PTR(__ret);			\
	else							\
		ptr_err = __back->ops->op(__back, ##args);	\
								\
	ptr_err;						\
})

#define iio_backend_void_op_call(back, op, args...) {		\
	struct iio_backend *__back = back;			\
	int __ret;						\
								\
	__ret = iio_backend_check_op(__back, op);		\
	if (!__ret)						\
		__back->ops->op(__back, ##args);		\
	else							\
		dev_dbg(__back->dev, "Op(%s) not implemented\n",\
			__stringify(op));			\
}

static ssize_t iio_backend_debugfs_read_reg(struct file *file,
					    char __user *userbuf,
					    size_t count, loff_t *ppos)
{
	struct iio_backend *back = file->private_data;
	char read_buf[20];
	unsigned int val;
	int ret, len;

	ret = iio_backend_op_call(back, debugfs_reg_access,
				  back->cached_reg_addr, 0, &val);
	if (ret)
		return ret;

	len = scnprintf(read_buf, sizeof(read_buf), "0x%X\n", val);

	return simple_read_from_buffer(userbuf, count, ppos, read_buf, len);
}

static ssize_t iio_backend_debugfs_write_reg(struct file *file,
					     const char __user *userbuf,
					     size_t count, loff_t *ppos)
{
	struct iio_backend *back = file->private_data;
	unsigned int val;
	char buf[80];
	ssize_t rc;
	int ret;

	rc = simple_write_to_buffer(buf, sizeof(buf), ppos, userbuf, count);
	if (rc < 0)
		return rc;

	ret = sscanf(buf, "%i %i", &back->cached_reg_addr, &val);

	switch (ret) {
	case 1:
		return count;
	case 2:
		ret = iio_backend_op_call(back, debugfs_reg_access,
					  back->cached_reg_addr, val, NULL);
		if (ret)
			return ret;
		return count;
	default:
		return -EINVAL;
	}
}

static const struct file_operations iio_backend_debugfs_reg_fops = {
	.open = simple_open,
	.read = iio_backend_debugfs_read_reg,
	.write = iio_backend_debugfs_write_reg,
};

static ssize_t iio_backend_debugfs_read_name(struct file *file,
					     char __user *userbuf,
					     size_t count, loff_t *ppos)
{
	struct iio_backend *back = file->private_data;
	char name[128];
	int len;

	len = scnprintf(name, sizeof(name), "%s\n", back->name);

	return simple_read_from_buffer(userbuf, count, ppos, name, len);
}

static const struct file_operations iio_backend_debugfs_name_fops = {
	.open = simple_open,
	.read = iio_backend_debugfs_read_name,
};

/**
 * iio_backend_debugfs_add - Add debugfs interfaces for Backends
 * @back: Backend device
 * @indio_dev: IIO device
 */
void iio_backend_debugfs_add(struct iio_backend *back,
			     struct iio_dev *indio_dev)
{
	struct dentry *d = iio_get_debugfs_dentry(indio_dev);
	struct dentry *back_d;
	char name[128];

	if (!IS_ENABLED(CONFIG_DEBUG_FS) || !d)
		return;
	if (!back->ops->debugfs_reg_access && !back->name)
		return;

	snprintf(name, sizeof(name), "backend%d", back->idx);

	back_d = debugfs_create_dir(name, d);
	if (IS_ERR(back_d))
		return;

	if (back->ops->debugfs_reg_access)
		debugfs_create_file("direct_reg_access", 0600, back_d, back,
				    &iio_backend_debugfs_reg_fops);

	if (back->name)
		debugfs_create_file("name", 0400, back_d, back,
				    &iio_backend_debugfs_name_fops);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_debugfs_add, IIO_BACKEND);

/**
 * iio_backend_debugfs_print_chan_status - Print channel status
 * @back: Backend device
 * @chan: Channel number
 * @buf: Buffer where to print the status
 * @len: Available space
 *
 * One usecase where this is useful is for testing test tones in a digital
 * interface and "ask" the backend to dump more details on why a test tone might
 * have errors.
 *
 * RETURNS:
 * Number of copied bytes on success, negative error code on failure.
 */
ssize_t iio_backend_debugfs_print_chan_status(struct iio_backend *back,
					      unsigned int chan, char *buf,
					      size_t len)
{
	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return -ENODEV;

	return iio_backend_op_call(back, debugfs_print_chan_status, chan, buf,
				   len);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_debugfs_print_chan_status, IIO_BACKEND);

/**
 * iio_backend_chan_enable - Enable a backend channel
 * @back: Backend device
 * @chan: Channel number
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_chan_enable(struct iio_backend *back, unsigned int chan)
{
	return iio_backend_op_call(back, chan_enable, chan);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_chan_enable, IIO_BACKEND);

/**
 * iio_backend_chan_disable - Disable a backend channel
 * @back: Backend device
 * @chan: Channel number
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_chan_disable(struct iio_backend *back, unsigned int chan)
{
	return iio_backend_op_call(back, chan_disable, chan);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_chan_disable, IIO_BACKEND);

static void __iio_backend_disable(void *back)
{
	iio_backend_void_op_call(back, disable);
}

/**
 * iio_backend_disable - Backend disable
 * @back: Backend device
 */
void iio_backend_disable(struct iio_backend *back)
{
	__iio_backend_disable(back);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_disable, IIO_BACKEND);

/**
 * iio_backend_enable - Backend enable
 * @back: Backend device
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_enable(struct iio_backend *back)
{
	return iio_backend_op_call(back, enable);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_enable, IIO_BACKEND);

/**
 * devm_iio_backend_enable - Device managed backend enable
 * @dev: Consumer device for the backend
 * @back: Backend device
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_iio_backend_enable(struct device *dev, struct iio_backend *back)
{
	int ret;

	ret = iio_backend_enable(back);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, __iio_backend_disable, back);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_enable, IIO_BACKEND);

/**
 * iio_backend_data_format_set - Configure the channel data format
 * @back: Backend device
 * @chan: Channel number
 * @data: Data format
 *
 * Properly configure a channel with respect to the expected data format. A
 * @struct iio_backend_data_fmt must be passed with the settings.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_data_format_set(struct iio_backend *back, unsigned int chan,
				const struct iio_backend_data_fmt *data)
{
	if (!data || data->type >= IIO_BACKEND_DATA_TYPE_MAX)
		return -EINVAL;

	return iio_backend_op_call(back, data_format_set, chan, data);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_data_format_set, IIO_BACKEND);

/**
 * iio_backend_data_source_set - Select data source
 * @back: Backend device
 * @chan: Channel number
 * @data: Data source
 *
 * A given backend may have different sources to stream/sync data. This allows
 * to choose that source.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_data_source_set(struct iio_backend *back, unsigned int chan,
				enum iio_backend_data_source data)
{
	if (data >= IIO_BACKEND_DATA_SOURCE_MAX)
		return -EINVAL;

	return iio_backend_op_call(back, data_source_set, chan, data);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_data_source_set, IIO_BACKEND);

/**
 * iio_backend_set_sampling_freq - Set channel sampling rate
 * @back: Backend device
 * @chan: Channel number
 * @sample_rate_hz: Sample rate
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_set_sampling_freq(struct iio_backend *back, unsigned int chan,
				  u64 sample_rate_hz)
{
	return iio_backend_op_call(back, set_sample_rate, chan, sample_rate_hz);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_set_sampling_freq, IIO_BACKEND);

/**
 * iio_backend_test_pattern_set - Configure a test pattern
 * @back: Backend device
 * @chan: Channel number
 * @pattern: Test pattern
 *
 * Configure a test pattern on the backend. This is typically used for
 * calibrating the timings on the data digital interface.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_test_pattern_set(struct iio_backend *back,
				 unsigned int chan,
				 enum iio_backend_test_pattern pattern)
{
	if (pattern >= IIO_BACKEND_TEST_PATTERN_MAX)
		return -EINVAL;

	return iio_backend_op_call(back, test_pattern_set, chan, pattern);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_test_pattern_set, IIO_BACKEND);

/**
 * iio_backend_chan_status - Get the channel status
 * @back: Backend device
 * @chan: Channel number
 * @error: Error indication
 *
 * Get the current state of the backend channel. Typically used to check if
 * there were any errors sending/receiving data.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_chan_status(struct iio_backend *back, unsigned int chan,
			    bool *error)
{
	return iio_backend_op_call(back, chan_status, chan, error);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_chan_status, IIO_BACKEND);

/**
 * iio_backend_iodelay_set - Set digital I/O delay
 * @back: Backend device
 * @lane: Lane number
 * @taps: Number of taps
 *
 * Controls delays on sending/receiving data. One usecase for this is to
 * calibrate the data digital interface so we get the best results when
 * transferring data. Note that @taps has no unit since the actual delay per tap
 * is very backend specific. Hence, frontend devices typically should go through
 * an array of @taps (the size of that array should typically match the size of
 * calibration points on the frontend device) and call this API.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_iodelay_set(struct iio_backend *back, unsigned int lane,
			    unsigned int taps)
{
	return iio_backend_op_call(back, iodelay_set, lane, taps);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_iodelay_set, IIO_BACKEND);

/**
 * iio_backend_data_sample_trigger - Control when to sample data
 * @back: Backend device
 * @trigger: Data trigger
 *
 * Mostly useful for input backends. Configures the backend for when to sample
 * data (eg: rising vs falling edge).
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_data_sample_trigger(struct iio_backend *back,
				    enum iio_backend_sample_trigger trigger)
{
	if (trigger >= IIO_BACKEND_SAMPLE_TRIGGER_MAX)
		return -EINVAL;

	return iio_backend_op_call(back, data_sample_trigger, trigger);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_data_sample_trigger, IIO_BACKEND);

static void iio_backend_free_buffer(void *arg)
{
	struct iio_backend_buffer_pair *pair = arg;

	iio_backend_void_op_call(pair->back, free_buffer, pair->buffer);
}

/**
 * devm_iio_backend_request_buffer - Device managed buffer request
 * @dev: Consumer device for the backend
 * @back: Backend device
 * @indio_dev: IIO device
 *
 * Request an IIO buffer from the backend. The type of the buffer (typically
 * INDIO_BUFFER_HARDWARE) is up to the backend to decide. This is because,
 * normally, the backend dictates what kind of buffering we can get.
 *
 * The backend .free_buffer() hooks is automatically called on @dev detach.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_iio_backend_request_buffer(struct device *dev,
				    struct iio_backend *back,
				    struct iio_dev *indio_dev)
{
	struct iio_backend_buffer_pair *pair;
	struct iio_buffer *buffer;

	pair = devm_kzalloc(dev, sizeof(*pair), GFP_KERNEL);
	if (!pair)
		return -ENOMEM;

	buffer = iio_backend_ptr_op_call(back, request_buffer, indio_dev);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	/* weak reference should be all what we need */
	pair->back = back;
	pair->buffer = buffer;

	return devm_add_action_or_reset(dev, iio_backend_free_buffer, pair);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_request_buffer, IIO_BACKEND);

/**
 * iio_backend_read_raw - Read a channel attribute from a backend device.
 * @back:	Backend device
 * @chan:	IIO channel reference
 * @val:	First returned value
 * @val2:	Second returned value
 * @mask:	Specify the attribute to return
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_read_raw(struct iio_backend *back,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	return iio_backend_op_call(back, read_raw, chan, val, val2, mask);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_read_raw, IIO_BACKEND);

static struct iio_backend *iio_backend_from_indio_dev_parent(const struct device *dev)
{
	struct iio_backend *back = ERR_PTR(-ENODEV), *iter;

	/*
	 * We deliberately go through all backends even after finding a match.
	 * The reason is that we want to catch frontend devices which have more
	 * than one backend in which case returning the first we find is bogus.
	 * For those cases, frontends need to explicitly define
	 * get_iio_backend() in struct iio_info.
	 */
	guard(mutex)(&iio_back_lock);
	list_for_each_entry(iter, &iio_back_list, entry) {
		if (dev == iter->frontend_dev) {
			if (!IS_ERR(back)) {
				dev_warn(dev,
					 "Multiple backends! get_iio_backend() needs to be implemented");
				return ERR_PTR(-ENODEV);
			}

			back = iter;
		}
	}

	return back;
}

/**
 * iio_backend_ext_info_get - IIO ext_info read callback
 * @indio_dev: IIO device
 * @private: Data private to the driver
 * @chan: IIO channel
 * @buf: Buffer where to place the attribute data
 *
 * This helper is intended to be used by backends that extend an IIO channel
 * (through iio_backend_extend_chan_spec()) with extended info. In that case,
 * backends are not supposed to give their own callbacks (as they would not have
 * a way to get the backend from indio_dev). This is the getter.
 *
 * RETURNS:
 * Number of bytes written to buf, negative error number on failure.
 */
ssize_t iio_backend_ext_info_get(struct iio_dev *indio_dev, uintptr_t private,
				 const struct iio_chan_spec *chan, char *buf)
{
	struct iio_backend *back;

	/*
	 * The below should work for the majority of the cases. It will not work
	 * when one frontend has multiple backends in which case we'll need a
	 * new callback in struct iio_info so we can directly request the proper
	 * backend from the frontend. Anyways, let's only introduce new options
	 * when really needed...
	 */
	back = iio_backend_from_indio_dev_parent(indio_dev->dev.parent);
	if (IS_ERR(back))
		return PTR_ERR(back);

	return iio_backend_op_call(back, ext_info_get, private, chan, buf);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_ext_info_get, IIO_BACKEND);

/**
 * iio_backend_ext_info_set - IIO ext_info write callback
 * @indio_dev: IIO device
 * @private: Data private to the driver
 * @chan: IIO channel
 * @buf: Buffer holding the sysfs attribute
 * @len: Buffer length
 *
 * This helper is intended to be used by backends that extend an IIO channel
 * (trough iio_backend_extend_chan_spec()) with extended info. In that case,
 * backends are not supposed to give their own callbacks (as they would not have
 * a way to get the backend from indio_dev). This is the setter.
 *
 * RETURNS:
 * Buffer length on success, negative error number on failure.
 */
ssize_t iio_backend_ext_info_set(struct iio_dev *indio_dev, uintptr_t private,
				 const struct iio_chan_spec *chan,
				 const char *buf, size_t len)
{
	struct iio_backend *back;

	back = iio_backend_from_indio_dev_parent(indio_dev->dev.parent);
	if (IS_ERR(back))
		return PTR_ERR(back);

	return iio_backend_op_call(back, ext_info_set, private, chan, buf, len);
}
EXPORT_SYMBOL_NS_GPL(iio_backend_ext_info_set, IIO_BACKEND);

/**
 * iio_backend_extend_chan_spec - Extend an IIO channel
 * @back: Backend device
 * @chan: IIO channel
 *
 * Some backends may have their own functionalities and hence capable of
 * extending a frontend's channel.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int iio_backend_extend_chan_spec(struct iio_backend *back,
				 struct iio_chan_spec *chan)
{
	const struct iio_chan_spec_ext_info *frontend_ext_info = chan->ext_info;
	const struct iio_chan_spec_ext_info *back_ext_info;
	int ret;

	ret = iio_backend_op_call(back, extend_chan_spec, chan);
	if (ret)
		return ret;
	/*
	 * Let's keep things simple for now. Don't allow to overwrite the
	 * frontend's extended info. If ever needed, we can support appending
	 * it.
	 */
	if (frontend_ext_info && chan->ext_info != frontend_ext_info)
		return -EOPNOTSUPP;
	if (!chan->ext_info)
		return 0;

	/* Don't allow backends to get creative and force their own handlers */
	for (back_ext_info = chan->ext_info; back_ext_info->name; back_ext_info++) {
		if (back_ext_info->read != iio_backend_ext_info_get)
			return -EINVAL;
		if (back_ext_info->write != iio_backend_ext_info_set)
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(iio_backend_extend_chan_spec, IIO_BACKEND);

static void iio_backend_release(void *arg)
{
	struct iio_backend *back = arg;

	module_put(back->owner);
}

static int __devm_iio_backend_get(struct device *dev, struct iio_backend *back)
{
	struct device_link *link;
	int ret;

	/*
	 * Make sure the provider cannot be unloaded before the consumer module.
	 * Note that device_links would still guarantee that nothing is
	 * accessible (and breaks) but this makes it explicit that the consumer
	 * module must be also unloaded.
	 */
	if (!try_module_get(back->owner))
		return dev_err_probe(dev, -ENODEV,
				     "Cannot get module reference\n");

	ret = devm_add_action_or_reset(dev, iio_backend_release, back);
	if (ret)
		return ret;

	link = device_link_add(dev, back->dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!link)
		return dev_err_probe(dev, -EINVAL,
				     "Could not link to supplier(%s)\n",
				     dev_name(back->dev));

	back->frontend_dev = dev;

	dev_dbg(dev, "Found backend(%s) device\n", dev_name(back->dev));

	return 0;
}

static struct iio_backend *__devm_iio_backend_fwnode_get(struct device *dev, const char *name,
							 struct fwnode_handle *fwnode)
{
	struct fwnode_handle *fwnode_back;
	struct iio_backend *back;
	unsigned int index;
	int ret;

	if (name) {
		ret = device_property_match_string(dev, "io-backend-names",
						   name);
		if (ret < 0)
			return ERR_PTR(ret);
		index = ret;
	} else {
		index = 0;
	}

	fwnode_back = fwnode_find_reference(fwnode, "io-backends", index);
	if (IS_ERR(fwnode))
		return dev_err_cast_probe(dev, fwnode,
					  "Cannot get Firmware reference\n");

	guard(mutex)(&iio_back_lock);
	list_for_each_entry(back, &iio_back_list, entry) {
		if (!device_match_fwnode(back->dev, fwnode_back))
			continue;

		fwnode_handle_put(fwnode_back);
		ret = __devm_iio_backend_get(dev, back);
		if (ret)
			return ERR_PTR(ret);

		if (name)
			back->idx = index;

		return back;
	}

	fwnode_handle_put(fwnode_back);
	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * devm_iio_backend_get - Device managed backend device get
 * @dev: Consumer device for the backend
 * @name: Backend name
 *
 * Get's the backend associated with @dev.
 *
 * RETURNS:
 * A backend pointer, negative error pointer otherwise.
 */
struct iio_backend *devm_iio_backend_get(struct device *dev, const char *name)
{
	return __devm_iio_backend_fwnode_get(dev, name, dev_fwnode(dev));
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_get, IIO_BACKEND);

/**
 * devm_iio_backend_fwnode_get - Device managed backend firmware node get
 * @dev: Consumer device for the backend
 * @name: Backend name
 * @fwnode: Firmware node of the backend consumer
 *
 * Get's the backend associated with a firmware node.
 *
 * RETURNS:
 * A backend pointer, negative error pointer otherwise.
 */
struct iio_backend *devm_iio_backend_fwnode_get(struct device *dev,
						const char *name,
						struct fwnode_handle *fwnode)
{
	return __devm_iio_backend_fwnode_get(dev, name, fwnode);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_fwnode_get, IIO_BACKEND);

/**
 * __devm_iio_backend_get_from_fwnode_lookup - Device managed fwnode backend device get
 * @dev: Consumer device for the backend
 * @fwnode: Firmware node of the backend device
 *
 * Search the backend list for a device matching @fwnode.
 * This API should not be used and it's only present for preventing the first
 * user of this framework to break it's DT ABI.
 *
 * RETURNS:
 * A backend pointer, negative error pointer otherwise.
 */
struct iio_backend *
__devm_iio_backend_get_from_fwnode_lookup(struct device *dev,
					  struct fwnode_handle *fwnode)
{
	struct iio_backend *back;
	int ret;

	guard(mutex)(&iio_back_lock);
	list_for_each_entry(back, &iio_back_list, entry) {
		if (!device_match_fwnode(back->dev, fwnode))
			continue;

		ret = __devm_iio_backend_get(dev, back);
		if (ret)
			return ERR_PTR(ret);

		return back;
	}

	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_NS_GPL(__devm_iio_backend_get_from_fwnode_lookup, IIO_BACKEND);

/**
 * iio_backend_get_priv - Get driver private data
 * @back: Backend device
 */
void *iio_backend_get_priv(const struct iio_backend *back)
{
	return back->priv;
}
EXPORT_SYMBOL_NS_GPL(iio_backend_get_priv, IIO_BACKEND);

static void iio_backend_unregister(void *arg)
{
	struct iio_backend *back = arg;

	guard(mutex)(&iio_back_lock);
	list_del(&back->entry);
}

/**
 * devm_iio_backend_register - Device managed backend device register
 * @dev: Backend device being registered
 * @info: Backend info
 * @priv: Device private data
 *
 * @info is mandatory. Not providing it results in -EINVAL.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_iio_backend_register(struct device *dev,
			      const struct iio_backend_info *info, void *priv)
{
	struct iio_backend *back;

	if (!info || !info->ops)
		return dev_err_probe(dev, -EINVAL, "No backend ops given\n");

	/*
	 * Through device_links, we guarantee that a frontend device cannot be
	 * bound/exist if the backend driver is not around. Hence, we can bind
	 * the backend object lifetime with the device being passed since
	 * removing it will tear the frontend/consumer down.
	 */
	back = devm_kzalloc(dev, sizeof(*back), GFP_KERNEL);
	if (!back)
		return -ENOMEM;

	back->ops = info->ops;
	back->name = info->name;
	back->owner = dev->driver->owner;
	back->dev = dev;
	back->priv = priv;
	scoped_guard(mutex, &iio_back_lock)
		list_add(&back->entry, &iio_back_list);

	return devm_add_action_or_reset(dev, iio_backend_unregister, back);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_register, IIO_BACKEND);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Framework to handle complex IIO aggregate devices");
MODULE_LICENSE("GPL");
