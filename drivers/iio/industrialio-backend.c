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
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/iio/backend.h>

struct iio_backend {
	struct list_head entry;
	const struct iio_backend_ops *ops;
	struct device *dev;
	struct module *owner;
	void *priv;
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
}

/**
 * iio_backend_chan_enable - Enable a backend channel
 * @back:	Backend device
 * @chan:	Channel number
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
 * @back:	Backend device
 * @chan:	Channel number
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
 * devm_iio_backend_enable - Device managed backend enable
 * @dev:	Consumer device for the backend
 * @back:	Backend device
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_iio_backend_enable(struct device *dev, struct iio_backend *back)
{
	int ret;

	ret = iio_backend_op_call(back, enable);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, __iio_backend_disable, back);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_enable, IIO_BACKEND);

/**
 * iio_backend_data_format_set - Configure the channel data format
 * @back:	Backend device
 * @chan:	Channel number
 * @data:	Data format
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

static void iio_backend_free_buffer(void *arg)
{
	struct iio_backend_buffer_pair *pair = arg;

	iio_backend_void_op_call(pair->back, free_buffer, pair->buffer);
}

/**
 * devm_iio_backend_request_buffer - Device managed buffer request
 * @dev:	Consumer device for the backend
 * @back:	Backend device
 * @indio_dev:	IIO device
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

	dev_dbg(dev, "Found backend(%s) device\n", dev_name(back->dev));

	return 0;
}

/**
 * devm_iio_backend_get - Device managed backend device get
 * @dev:	Consumer device for the backend
 * @name:	Backend name
 *
 * Get's the backend associated with @dev.
 *
 * RETURNS:
 * A backend pointer, negative error pointer otherwise.
 */
struct iio_backend *devm_iio_backend_get(struct device *dev, const char *name)
{
	struct fwnode_handle *fwnode;
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

	fwnode = fwnode_find_reference(dev_fwnode(dev), "io-backends", index);
	if (IS_ERR(fwnode)) {
		dev_err_probe(dev, PTR_ERR(fwnode),
			      "Cannot get Firmware reference\n");
		return ERR_CAST(fwnode);
	}

	guard(mutex)(&iio_back_lock);
	list_for_each_entry(back, &iio_back_list, entry) {
		if (!device_match_fwnode(back->dev, fwnode))
			continue;

		fwnode_handle_put(fwnode);
		ret = __devm_iio_backend_get(dev, back);
		if (ret)
			return ERR_PTR(ret);

		return back;
	}

	fwnode_handle_put(fwnode);
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_NS_GPL(devm_iio_backend_get, IIO_BACKEND);

/**
 * __devm_iio_backend_get_from_fwnode_lookup - Device managed fwnode backend device get
 * @dev:	Consumer device for the backend
 * @fwnode:	Firmware node of the backend device
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
 * @back:	Backend device
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
 * @dev:	Backend device being registered
 * @ops:	Backend ops
 * @priv:	Device private data
 *
 * @ops is mandatory. Not providing it results in -EINVAL.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_iio_backend_register(struct device *dev,
			      const struct iio_backend_ops *ops, void *priv)
{
	struct iio_backend *back;

	if (!ops)
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

	back->ops = ops;
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
