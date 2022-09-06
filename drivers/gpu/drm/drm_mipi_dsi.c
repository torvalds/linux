/*
 * MIPI DSI Bus
 *
 * Copyright (C) 2012-2013, Samsung Electronics, Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/display/drm_dsc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>

#include <video/mipi_display.h>

/**
 * DOC: dsi helpers
 *
 * These functions contain some common logic and helpers to deal with MIPI DSI
 * peripherals.
 *
 * Helpers are provided for a number of standard MIPI DSI command as well as a
 * subset of the MIPI DCS command set.
 */

static int mipi_dsi_device_match(struct device *dev, struct device_driver *drv)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	/* attempt OF style match */
	if (of_driver_match_device(dev, drv))
		return 1;

	/* compare DSI device and driver names */
	if (!strcmp(dsi->name, drv->name))
		return 1;

	return 0;
}

static int mipi_dsi_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	int err;

	err = of_device_uevent_modalias(dev, env);
	if (err != -ENODEV)
		return err;

	add_uevent_var(env, "MODALIAS=%s%s", MIPI_DSI_MODULE_PREFIX,
		       dsi->name);

	return 0;
}

static const struct dev_pm_ops mipi_dsi_device_pm_ops = {
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
	.suspend = pm_generic_suspend,
	.resume = pm_generic_resume,
	.freeze = pm_generic_freeze,
	.thaw = pm_generic_thaw,
	.poweroff = pm_generic_poweroff,
	.restore = pm_generic_restore,
};

static struct bus_type mipi_dsi_bus_type = {
	.name = "mipi-dsi",
	.match = mipi_dsi_device_match,
	.uevent = mipi_dsi_uevent,
	.pm = &mipi_dsi_device_pm_ops,
};

/**
 * of_find_mipi_dsi_device_by_node() - find the MIPI DSI device matching a
 *    device tree node
 * @np: device tree node
 *
 * Return: A pointer to the MIPI DSI device corresponding to @np or NULL if no
 *    such device exists (or has not been registered yet).
 */
struct mipi_dsi_device *of_find_mipi_dsi_device_by_node(struct device_node *np)
{
	struct device *dev;

	dev = bus_find_device_by_of_node(&mipi_dsi_bus_type, np);

	return dev ? to_mipi_dsi_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_mipi_dsi_device_by_node);

static void mipi_dsi_dev_release(struct device *dev)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	of_node_put(dev->of_node);
	kfree(dsi);
}

static const struct device_type mipi_dsi_device_type = {
	.release = mipi_dsi_dev_release,
};

static struct mipi_dsi_device *mipi_dsi_device_alloc(struct mipi_dsi_host *host)
{
	struct mipi_dsi_device *dsi;

	dsi = kzalloc(sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return ERR_PTR(-ENOMEM);

	dsi->host = host;
	dsi->dev.bus = &mipi_dsi_bus_type;
	dsi->dev.parent = host->dev;
	dsi->dev.type = &mipi_dsi_device_type;

	device_initialize(&dsi->dev);

	return dsi;
}

static int mipi_dsi_device_add(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_host *host = dsi->host;

	dev_set_name(&dsi->dev, "%s.%d", dev_name(host->dev),  dsi->channel);

	return device_add(&dsi->dev);
}

#if IS_ENABLED(CONFIG_OF)
static struct mipi_dsi_device *
of_mipi_dsi_device_add(struct mipi_dsi_host *host, struct device_node *node)
{
	struct mipi_dsi_device_info info = { };
	int ret;
	u32 reg;

	if (of_modalias_node(node, info.type, sizeof(info.type)) < 0) {
		drm_err(host, "modalias failure on %pOF\n", node);
		return ERR_PTR(-EINVAL);
	}

	ret = of_property_read_u32(node, "reg", &reg);
	if (ret) {
		drm_err(host, "device node %pOF has no valid reg property: %d\n",
			node, ret);
		return ERR_PTR(-EINVAL);
	}

	info.channel = reg;
	info.node = of_node_get(node);

	return mipi_dsi_device_register_full(host, &info);
}
#else
static struct mipi_dsi_device *
of_mipi_dsi_device_add(struct mipi_dsi_host *host, struct device_node *node)
{
	return ERR_PTR(-ENODEV);
}
#endif

/**
 * mipi_dsi_device_register_full - create a MIPI DSI device
 * @host: DSI host to which this device is connected
 * @info: pointer to template containing DSI device information
 *
 * Create a MIPI DSI device by using the device information provided by
 * mipi_dsi_device_info template
 *
 * Returns:
 * A pointer to the newly created MIPI DSI device, or, a pointer encoded
 * with an error
 */
struct mipi_dsi_device *
mipi_dsi_device_register_full(struct mipi_dsi_host *host,
			      const struct mipi_dsi_device_info *info)
{
	struct mipi_dsi_device *dsi;
	int ret;

	if (!info) {
		drm_err(host, "invalid mipi_dsi_device_info pointer\n");
		return ERR_PTR(-EINVAL);
	}

	if (info->channel > 3) {
		drm_err(host, "invalid virtual channel: %u\n", info->channel);
		return ERR_PTR(-EINVAL);
	}

	dsi = mipi_dsi_device_alloc(host);
	if (IS_ERR(dsi)) {
		drm_err(host, "failed to allocate DSI device %ld\n",
			PTR_ERR(dsi));
		return dsi;
	}

	dsi->dev.of_node = info->node;
	dsi->channel = info->channel;
	strlcpy(dsi->name, info->type, sizeof(dsi->name));

	ret = mipi_dsi_device_add(dsi);
	if (ret) {
		drm_err(host, "failed to add DSI device %d\n", ret);
		kfree(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}
EXPORT_SYMBOL(mipi_dsi_device_register_full);

/**
 * mipi_dsi_device_unregister - unregister MIPI DSI device
 * @dsi: DSI peripheral device
 */
void mipi_dsi_device_unregister(struct mipi_dsi_device *dsi)
{
	device_unregister(&dsi->dev);
}
EXPORT_SYMBOL(mipi_dsi_device_unregister);

static void devm_mipi_dsi_device_unregister(void *arg)
{
	struct mipi_dsi_device *dsi = arg;

	mipi_dsi_device_unregister(dsi);
}

/**
 * devm_mipi_dsi_device_register_full - create a managed MIPI DSI device
 * @dev: device to tie the MIPI-DSI device lifetime to
 * @host: DSI host to which this device is connected
 * @info: pointer to template containing DSI device information
 *
 * Create a MIPI DSI device by using the device information provided by
 * mipi_dsi_device_info template
 *
 * This is the managed version of mipi_dsi_device_register_full() which
 * automatically calls mipi_dsi_device_unregister() when @dev is
 * unbound.
 *
 * Returns:
 * A pointer to the newly created MIPI DSI device, or, a pointer encoded
 * with an error
 */
struct mipi_dsi_device *
devm_mipi_dsi_device_register_full(struct device *dev,
				   struct mipi_dsi_host *host,
				   const struct mipi_dsi_device_info *info)
{
	struct mipi_dsi_device *dsi;
	int ret;

	dsi = mipi_dsi_device_register_full(host, info);
	if (IS_ERR(dsi))
		return dsi;

	ret = devm_add_action_or_reset(dev,
				       devm_mipi_dsi_device_unregister,
				       dsi);
	if (ret)
		return ERR_PTR(ret);

	return dsi;
}
EXPORT_SYMBOL_GPL(devm_mipi_dsi_device_register_full);

static DEFINE_MUTEX(host_lock);
static LIST_HEAD(host_list);

/**
 * of_find_mipi_dsi_host_by_node() - find the MIPI DSI host matching a
 *				     device tree node
 * @node: device tree node
 *
 * Returns:
 * A pointer to the MIPI DSI host corresponding to @node or NULL if no
 * such device exists (or has not been registered yet).
 */
struct mipi_dsi_host *of_find_mipi_dsi_host_by_node(struct device_node *node)
{
	struct mipi_dsi_host *host;

	mutex_lock(&host_lock);

	list_for_each_entry(host, &host_list, list) {
		if (host->dev->of_node == node) {
			mutex_unlock(&host_lock);
			return host;
		}
	}

	mutex_unlock(&host_lock);

	return NULL;
}
EXPORT_SYMBOL(of_find_mipi_dsi_host_by_node);

int mipi_dsi_host_register(struct mipi_dsi_host *host)
{
	struct device_node *node;

	for_each_available_child_of_node(host->dev->of_node, node) {
		/* skip nodes without reg property */
		if (!of_find_property(node, "reg", NULL))
			continue;
		of_mipi_dsi_device_add(host, node);
	}

	mutex_lock(&host_lock);
	list_add_tail(&host->list, &host_list);
	mutex_unlock(&host_lock);

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_host_register);

static int mipi_dsi_remove_device_fn(struct device *dev, void *priv)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	mipi_dsi_detach(dsi);
	mipi_dsi_device_unregister(dsi);

	return 0;
}

void mipi_dsi_host_unregister(struct mipi_dsi_host *host)
{
	device_for_each_child(host->dev, NULL, mipi_dsi_remove_device_fn);

	mutex_lock(&host_lock);
	list_del_init(&host->list);
	mutex_unlock(&host_lock);
}
EXPORT_SYMBOL(mipi_dsi_host_unregister);

/**
 * mipi_dsi_attach - attach a DSI device to its DSI host
 * @dsi: DSI peripheral
 */
int mipi_dsi_attach(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->attach)
		return -ENOSYS;

	return ops->attach(dsi->host, dsi);
}
EXPORT_SYMBOL(mipi_dsi_attach);

/**
 * mipi_dsi_detach - detach a DSI device from its DSI host
 * @dsi: DSI peripheral
 */
int mipi_dsi_detach(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->detach)
		return -ENOSYS;

	return ops->detach(dsi->host, dsi);
}
EXPORT_SYMBOL(mipi_dsi_detach);

static void devm_mipi_dsi_detach(void *arg)
{
	struct mipi_dsi_device *dsi = arg;

	mipi_dsi_detach(dsi);
}

/**
 * devm_mipi_dsi_attach - Attach a MIPI-DSI device to its DSI Host
 * @dev: device to tie the MIPI-DSI device attachment lifetime to
 * @dsi: DSI peripheral
 *
 * This is the managed version of mipi_dsi_attach() which automatically
 * calls mipi_dsi_detach() when @dev is unbound.
 *
 * Returns:
 * 0 on success, a negative error code on failure.
 */
int devm_mipi_dsi_attach(struct device *dev,
			 struct mipi_dsi_device *dsi)
{
	int ret;

	ret = mipi_dsi_attach(dsi);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, devm_mipi_dsi_detach, dsi);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(devm_mipi_dsi_attach);

static ssize_t mipi_dsi_device_transfer(struct mipi_dsi_device *dsi,
					struct mipi_dsi_msg *msg)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->transfer)
		return -ENOSYS;

	if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
		msg->flags |= MIPI_DSI_MSG_USE_LPM;

	return ops->transfer(dsi->host, msg);
}

/**
 * mipi_dsi_packet_format_is_short - check if a packet is of the short format
 * @type: MIPI DSI data type of the packet
 *
 * Return: true if the packet for the given data type is a short packet, false
 * otherwise.
 */
bool mipi_dsi_packet_format_is_short(u8 type)
{
	switch (type) {
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_COMPRESSION_MODE:
	case MIPI_DSI_END_OF_TRANSMISSION:
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_EXECUTE_QUEUE:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		return true;
	}

	return false;
}
EXPORT_SYMBOL(mipi_dsi_packet_format_is_short);

/**
 * mipi_dsi_packet_format_is_long - check if a packet is of the long format
 * @type: MIPI DSI data type of the packet
 *
 * Return: true if the packet for the given data type is a long packet, false
 * otherwise.
 */
bool mipi_dsi_packet_format_is_long(u8 type)
{
	switch (type) {
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	case MIPI_DSI_PICTURE_PARAMETER_SET:
	case MIPI_DSI_COMPRESSED_PIXEL_STREAM:
	case MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_30:
	case MIPI_DSI_PACKED_PIXEL_STREAM_36:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12:
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		return true;
	}

	return false;
}
EXPORT_SYMBOL(mipi_dsi_packet_format_is_long);

/**
 * mipi_dsi_create_packet - create a packet from a message according to the
 *     DSI protocol
 * @packet: pointer to a DSI packet structure
 * @msg: message to translate into a packet
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_create_packet(struct mipi_dsi_packet *packet,
			   const struct mipi_dsi_msg *msg)
{
	if (!packet || !msg)
		return -EINVAL;

	/* do some minimum sanity checking */
	if (!mipi_dsi_packet_format_is_short(msg->type) &&
	    !mipi_dsi_packet_format_is_long(msg->type))
		return -EINVAL;

	if (msg->channel > 3)
		return -EINVAL;

	memset(packet, 0, sizeof(*packet));
	packet->header[0] = ((msg->channel & 0x3) << 6) | (msg->type & 0x3f);

	/* TODO: compute ECC if hardware support is not available */

	/*
	 * Long write packets contain the word count in header bytes 1 and 2.
	 * The payload follows the header and is word count bytes long.
	 *
	 * Short write packets encode up to two parameters in header bytes 1
	 * and 2.
	 */
	if (mipi_dsi_packet_format_is_long(msg->type)) {
		packet->header[1] = (msg->tx_len >> 0) & 0xff;
		packet->header[2] = (msg->tx_len >> 8) & 0xff;

		packet->payload_length = msg->tx_len;
		packet->payload = msg->tx_buf;
	} else {
		const u8 *tx = msg->tx_buf;

		packet->header[1] = (msg->tx_len > 0) ? tx[0] : 0;
		packet->header[2] = (msg->tx_len > 1) ? tx[1] : 0;
	}

	packet->size = sizeof(packet->header) + packet->payload_length;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_create_packet);

/**
 * mipi_dsi_shutdown_peripheral() - sends a Shutdown Peripheral command
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_shutdown_peripheral(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_SHUTDOWN_PERIPHERAL,
		.tx_buf = (u8 [2]) { 0, 0 },
		.tx_len = 2,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(mipi_dsi_shutdown_peripheral);

/**
 * mipi_dsi_turn_on_peripheral() - sends a Turn On Peripheral command
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_turn_on_peripheral(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_TURN_ON_PERIPHERAL,
		.tx_buf = (u8 [2]) { 0, 0 },
		.tx_len = 2,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(mipi_dsi_turn_on_peripheral);

/*
 * mipi_dsi_set_maximum_return_packet_size() - specify the maximum size of the
 *    the payload in a long packet transmitted from the peripheral back to the
 *    host processor
 * @dsi: DSI peripheral device
 * @value: the maximum size of the payload
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_set_maximum_return_packet_size(struct mipi_dsi_device *dsi,
					    u16 value)
{
	u8 tx[2] = { value & 0xff, value >> 8 };
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
		.tx_len = sizeof(tx),
		.tx_buf = tx,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(mipi_dsi_set_maximum_return_packet_size);

/**
 * mipi_dsi_compression_mode() - enable/disable DSC on the peripheral
 * @dsi: DSI peripheral device
 * @enable: Whether to enable or disable the DSC
 *
 * Enable or disable Display Stream Compression on the peripheral using the
 * default Picture Parameter Set and VESA DSC 1.1 algorithm.
 *
 * Return: 0 on success or a negative error code on failure.
 */
ssize_t mipi_dsi_compression_mode(struct mipi_dsi_device *dsi, bool enable)
{
	/* Note: Needs updating for non-default PPS or algorithm */
	u8 tx[2] = { enable << 0, 0 };
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_COMPRESSION_MODE,
		.tx_len = sizeof(tx),
		.tx_buf = tx,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(mipi_dsi_compression_mode);

/**
 * mipi_dsi_picture_parameter_set() - transmit the DSC PPS to the peripheral
 * @dsi: DSI peripheral device
 * @pps: VESA DSC 1.1 Picture Parameter Set
 *
 * Transmit the VESA DSC 1.1 Picture Parameter Set to the peripheral.
 *
 * Return: 0 on success or a negative error code on failure.
 */
ssize_t mipi_dsi_picture_parameter_set(struct mipi_dsi_device *dsi,
				       const struct drm_dsc_picture_parameter_set *pps)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_PICTURE_PARAMETER_SET,
		.tx_len = sizeof(*pps),
		.tx_buf = pps,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(mipi_dsi_picture_parameter_set);

/**
 * mipi_dsi_generic_write() - transmit data using a generic write packet
 * @dsi: DSI peripheral device
 * @payload: buffer containing the payload
 * @size: size of payload buffer
 *
 * This function will automatically choose the right data type depending on
 * the payload length.
 *
 * Return: The number of bytes transmitted on success or a negative error code
 * on failure.
 */
ssize_t mipi_dsi_generic_write(struct mipi_dsi_device *dsi, const void *payload,
			       size_t size)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.tx_buf = payload,
		.tx_len = size
	};

	switch (size) {
	case 0:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM;
		break;

	case 1:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM;
		break;

	case 2:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM;
		break;

	default:
		msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
		break;
	}

	return mipi_dsi_device_transfer(dsi, &msg);
}
EXPORT_SYMBOL(mipi_dsi_generic_write);

/**
 * mipi_dsi_generic_read() - receive data using a generic read packet
 * @dsi: DSI peripheral device
 * @params: buffer containing the request parameters
 * @num_params: number of request parameters
 * @data: buffer in which to return the received data
 * @size: size of receive buffer
 *
 * This function will automatically choose the right data type depending on
 * the number of parameters passed in.
 *
 * Return: The number of bytes successfully read or a negative error code on
 * failure.
 */
ssize_t mipi_dsi_generic_read(struct mipi_dsi_device *dsi, const void *params,
			      size_t num_params, void *data, size_t size)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.tx_len = num_params,
		.tx_buf = params,
		.rx_len = size,
		.rx_buf = data
	};

	switch (num_params) {
	case 0:
		msg.type = MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM;
		break;

	case 1:
		msg.type = MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM;
		break;

	case 2:
		msg.type = MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM;
		break;

	default:
		return -EINVAL;
	}

	return mipi_dsi_device_transfer(dsi, &msg);
}
EXPORT_SYMBOL(mipi_dsi_generic_read);

/**
 * mipi_dsi_dcs_write_buffer() - transmit a DCS command with payload
 * @dsi: DSI peripheral device
 * @data: buffer containing data to be transmitted
 * @len: size of transmission buffer
 *
 * This function will automatically choose the right data type depending on
 * the command payload length.
 *
 * Return: The number of bytes successfully transmitted or a negative error
 * code on failure.
 */
ssize_t mipi_dsi_dcs_write_buffer(struct mipi_dsi_device *dsi,
				  const void *data, size_t len)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.tx_buf = data,
		.tx_len = len
	};

	switch (len) {
	case 0:
		return -EINVAL;

	case 1:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE;
		break;

	case 2:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		break;

	default:
		msg.type = MIPI_DSI_DCS_LONG_WRITE;
		break;
	}

	return mipi_dsi_device_transfer(dsi, &msg);
}
EXPORT_SYMBOL(mipi_dsi_dcs_write_buffer);

/**
 * mipi_dsi_dcs_write() - send DCS write command
 * @dsi: DSI peripheral device
 * @cmd: DCS command
 * @data: buffer containing the command payload
 * @len: command payload length
 *
 * This function will automatically choose the right data type depending on
 * the command payload length.
 *
 * Return: The number of bytes successfully transmitted or a negative error
 * code on failure.
 */
ssize_t mipi_dsi_dcs_write(struct mipi_dsi_device *dsi, u8 cmd,
			   const void *data, size_t len)
{
	ssize_t err;
	size_t size;
	u8 stack_tx[8];
	u8 *tx;

	size = 1 + len;
	if (len > ARRAY_SIZE(stack_tx) - 1) {
		tx = kmalloc(size, GFP_KERNEL);
		if (!tx)
			return -ENOMEM;
	} else {
		tx = stack_tx;
	}

	/* concatenate the DCS command byte and the payload */
	tx[0] = cmd;
	if (data)
		memcpy(&tx[1], data, len);

	err = mipi_dsi_dcs_write_buffer(dsi, tx, size);

	if (tx != stack_tx)
		kfree(tx);

	return err;
}
EXPORT_SYMBOL(mipi_dsi_dcs_write);

/**
 * mipi_dsi_dcs_read() - send DCS read request command
 * @dsi: DSI peripheral device
 * @cmd: DCS command
 * @data: buffer in which to receive data
 * @len: size of receive buffer
 *
 * Return: The number of bytes read or a negative error code on failure.
 */
ssize_t mipi_dsi_dcs_read(struct mipi_dsi_device *dsi, u8 cmd, void *data,
			  size_t len)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_DCS_READ,
		.tx_buf = &cmd,
		.tx_len = 1,
		.rx_buf = data,
		.rx_len = len
	};

	return mipi_dsi_device_transfer(dsi, &msg);
}
EXPORT_SYMBOL(mipi_dsi_dcs_read);

/**
 * mipi_dsi_dcs_nop() - send DCS nop packet
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_nop(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_NOP, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_nop);

/**
 * mipi_dsi_dcs_soft_reset() - perform a software reset of the display module
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_soft_reset(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SOFT_RESET, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_soft_reset);

/**
 * mipi_dsi_dcs_get_power_mode() - query the display module's current power
 *    mode
 * @dsi: DSI peripheral device
 * @mode: return location for the current power mode
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_get_power_mode(struct mipi_dsi_device *dsi, u8 *mode)
{
	ssize_t err;

	err = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_POWER_MODE, mode,
				sizeof(*mode));
	if (err <= 0) {
		if (err == 0)
			err = -ENODATA;

		return err;
	}

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_get_power_mode);

/**
 * mipi_dsi_dcs_get_pixel_format() - gets the pixel format for the RGB image
 *    data used by the interface
 * @dsi: DSI peripheral device
 * @format: return location for the pixel format
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_get_pixel_format(struct mipi_dsi_device *dsi, u8 *format)
{
	ssize_t err;

	err = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_PIXEL_FORMAT, format,
				sizeof(*format));
	if (err <= 0) {
		if (err == 0)
			err = -ENODATA;

		return err;
	}

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_get_pixel_format);

/**
 * mipi_dsi_dcs_enter_sleep_mode() - disable all unnecessary blocks inside the
 *    display module except interface communication
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_enter_sleep_mode(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_ENTER_SLEEP_MODE, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_enter_sleep_mode);

/**
 * mipi_dsi_dcs_exit_sleep_mode() - enable all blocks inside the display
 *    module
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_exit_sleep_mode(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_exit_sleep_mode);

/**
 * mipi_dsi_dcs_set_display_off() - stop displaying the image data on the
 *    display device
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_set_display_off(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_display_off);

/**
 * mipi_dsi_dcs_set_display_on() - start displaying the image data on the
 *    display device
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure
 */
int mipi_dsi_dcs_set_display_on(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_ON, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_display_on);

/**
 * mipi_dsi_dcs_set_column_address() - define the column extent of the frame
 *    memory accessed by the host processor
 * @dsi: DSI peripheral device
 * @start: first column of frame memory
 * @end: last column of frame memory
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_set_column_address(struct mipi_dsi_device *dsi, u16 start,
				    u16 end)
{
	u8 payload[4] = { start >> 8, start & 0xff, end >> 8, end & 0xff };
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_COLUMN_ADDRESS, payload,
				 sizeof(payload));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_column_address);

/**
 * mipi_dsi_dcs_set_page_address() - define the page extent of the frame
 *    memory accessed by the host processor
 * @dsi: DSI peripheral device
 * @start: first page of frame memory
 * @end: last page of frame memory
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_set_page_address(struct mipi_dsi_device *dsi, u16 start,
				  u16 end)
{
	u8 payload[4] = { start >> 8, start & 0xff, end >> 8, end & 0xff };
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_PAGE_ADDRESS, payload,
				 sizeof(payload));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_page_address);

/**
 * mipi_dsi_dcs_set_tear_off() - turn off the display module's Tearing Effect
 *    output signal on the TE signal line
 * @dsi: DSI peripheral device
 *
 * Return: 0 on success or a negative error code on failure
 */
int mipi_dsi_dcs_set_tear_off(struct mipi_dsi_device *dsi)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_TEAR_OFF, NULL, 0);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_tear_off);

/**
 * mipi_dsi_dcs_set_tear_on() - turn on the display module's Tearing Effect
 *    output signal on the TE signal line.
 * @dsi: DSI peripheral device
 * @mode: the Tearing Effect Output Line mode
 *
 * Return: 0 on success or a negative error code on failure
 */
int mipi_dsi_dcs_set_tear_on(struct mipi_dsi_device *dsi,
			     enum mipi_dsi_dcs_tear_mode mode)
{
	u8 value = mode;
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_TEAR_ON, &value,
				 sizeof(value));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_tear_on);

/**
 * mipi_dsi_dcs_set_pixel_format() - sets the pixel format for the RGB image
 *    data used by the interface
 * @dsi: DSI peripheral device
 * @format: pixel format
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_set_pixel_format(struct mipi_dsi_device *dsi, u8 format)
{
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_PIXEL_FORMAT, &format,
				 sizeof(format));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_pixel_format);

/**
 * mipi_dsi_dcs_set_tear_scanline() - set the scanline to use as trigger for
 *    the Tearing Effect output signal of the display module
 * @dsi: DSI peripheral device
 * @scanline: scanline to use as trigger
 *
 * Return: 0 on success or a negative error code on failure
 */
int mipi_dsi_dcs_set_tear_scanline(struct mipi_dsi_device *dsi, u16 scanline)
{
	u8 payload[2] = { scanline >> 8, scanline & 0xff };
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_TEAR_SCANLINE, payload,
				 sizeof(payload));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_tear_scanline);

/**
 * mipi_dsi_dcs_set_display_brightness() - sets the brightness value of the
 *    display
 * @dsi: DSI peripheral device
 * @brightness: brightness value
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_set_display_brightness(struct mipi_dsi_device *dsi,
					u16 brightness)
{
	u8 payload[2] = { brightness & 0xff, brightness >> 8 };
	ssize_t err;

	err = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				 payload, sizeof(payload));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_set_display_brightness);

/**
 * mipi_dsi_dcs_get_display_brightness() - gets the current brightness value
 *    of the display
 * @dsi: DSI peripheral device
 * @brightness: brightness value
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_dcs_get_display_brightness(struct mipi_dsi_device *dsi,
					u16 *brightness)
{
	ssize_t err;

	err = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_BRIGHTNESS,
				brightness, sizeof(*brightness));
	if (err <= 0) {
		if (err == 0)
			err = -ENODATA;

		return err;
	}

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_dcs_get_display_brightness);

static int mipi_dsi_drv_probe(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	return drv->probe(dsi);
}

static int mipi_dsi_drv_remove(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	drv->remove(dsi);

	return 0;
}

static void mipi_dsi_drv_shutdown(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	drv->shutdown(dsi);
}

/**
 * mipi_dsi_driver_register_full() - register a driver for DSI devices
 * @drv: DSI driver structure
 * @owner: owner module
 *
 * Return: 0 on success or a negative error code on failure.
 */
int mipi_dsi_driver_register_full(struct mipi_dsi_driver *drv,
				  struct module *owner)
{
	drv->driver.bus = &mipi_dsi_bus_type;
	drv->driver.owner = owner;

	if (drv->probe)
		drv->driver.probe = mipi_dsi_drv_probe;
	if (drv->remove)
		drv->driver.remove = mipi_dsi_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = mipi_dsi_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(mipi_dsi_driver_register_full);

/**
 * mipi_dsi_driver_unregister() - unregister a driver for DSI devices
 * @drv: DSI driver structure
 *
 * Return: 0 on success or a negative error code on failure.
 */
void mipi_dsi_driver_unregister(struct mipi_dsi_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(mipi_dsi_driver_unregister);

static int __init mipi_dsi_bus_init(void)
{
	return bus_register(&mipi_dsi_bus_type);
}
postcore_initcall(mipi_dsi_bus_init);

MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("MIPI DSI Bus");
MODULE_LICENSE("GPL and additional rights");
