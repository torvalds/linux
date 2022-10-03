// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MegaChips STDP4028 with GE B850v3 firmware (LVDS-DP)
 * Driver for MegaChips STDP2690 with GE B850v3 firmware (DP-DP++)

 * Copyright (c) 2017, Collabora Ltd.
 * Copyright (c) 2017, General Electric Company


 * This driver creates a drm_bridge and a drm_connector for the LVDS to DP++
 * display bridge of the GE B850v3. There are two physical bridges on the video
 * signal pipeline: a STDP4028(LVDS to DP) and a STDP2690(DP to DP++). The
 * physical bridges are automatically configured by the input video signal, and
 * the driver has no access to the video processing pipeline. The driver is
 * only needed to read EDID from the STDP2690 and to handle HPD events from the
 * STDP4028. The driver communicates with both bridges over i2c. The video
 * signal pipeline is as follows:
 *
 *   Host -> LVDS|--(STDP4028)--|DP -> DP|--(STDP2690)--|DP++ -> Video output
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define EDID_EXT_BLOCK_CNT 0x7E

#define STDP4028_IRQ_OUT_CONF_REG 0x02
#define STDP4028_DPTX_IRQ_EN_REG 0x3C
#define STDP4028_DPTX_IRQ_STS_REG 0x3D
#define STDP4028_DPTX_STS_REG 0x3E

#define STDP4028_DPTX_DP_IRQ_EN 0x1000

#define STDP4028_DPTX_HOTPLUG_IRQ_EN 0x0400
#define STDP4028_DPTX_LINK_CH_IRQ_EN 0x2000
#define STDP4028_DPTX_IRQ_CONFIG \
		(STDP4028_DPTX_LINK_CH_IRQ_EN | STDP4028_DPTX_HOTPLUG_IRQ_EN)

#define STDP4028_DPTX_HOTPLUG_STS 0x0200
#define STDP4028_DPTX_LINK_STS 0x1000
#define STDP4028_CON_STATE_CONNECTED \
		(STDP4028_DPTX_HOTPLUG_STS | STDP4028_DPTX_LINK_STS)

#define STDP4028_DPTX_HOTPLUG_CH_STS 0x0400
#define STDP4028_DPTX_LINK_CH_STS 0x2000
#define STDP4028_DPTX_IRQ_CLEAR \
		(STDP4028_DPTX_LINK_CH_STS | STDP4028_DPTX_HOTPLUG_CH_STS)

static DEFINE_MUTEX(ge_b850v3_lvds_dev_mutex);

struct ge_b850v3_lvds {
	struct drm_connector connector;
	struct drm_bridge bridge;
	struct i2c_client *stdp4028_i2c;
	struct i2c_client *stdp2690_i2c;
};

static struct ge_b850v3_lvds *ge_b850v3_lvds_ptr;

static u8 *stdp2690_get_edid(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	unsigned char start = 0x00;
	unsigned int total_size;
	u8 *block = kmalloc(EDID_LENGTH, GFP_KERNEL);

	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= EDID_LENGTH,
			.buf	= block,
		}
	};

	if (!block)
		return NULL;

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		DRM_ERROR("Unable to read EDID.\n");
		goto err;
	}

	if (!drm_edid_block_valid(block, 0, false, NULL)) {
		DRM_ERROR("Invalid EDID data\n");
		goto err;
	}

	total_size = (block[EDID_EXT_BLOCK_CNT] + 1) * EDID_LENGTH;
	if (total_size > EDID_LENGTH) {
		kfree(block);
		block = kmalloc(total_size, GFP_KERNEL);
		if (!block)
			return NULL;

		/* Yes, read the entire buffer, and do not skip the first
		 * EDID_LENGTH bytes.
		 */
		start = 0x00;
		msgs[1].len = total_size;
		msgs[1].buf = block;

		if (i2c_transfer(adapter, msgs, 2) != 2) {
			DRM_ERROR("Unable to read EDID extension blocks.\n");
			goto err;
		}
		if (!drm_edid_block_valid(block, 1, false, NULL)) {
			DRM_ERROR("Invalid EDID data\n");
			goto err;
		}
	}

	return block;

err:
	kfree(block);
	return NULL;
}

static struct edid *ge_b850v3_lvds_get_edid(struct drm_bridge *bridge,
					    struct drm_connector *connector)
{
	struct i2c_client *client;

	client = ge_b850v3_lvds_ptr->stdp2690_i2c;

	return (struct edid *)stdp2690_get_edid(client);
}

static int ge_b850v3_lvds_get_modes(struct drm_connector *connector)
{
	struct edid *edid;
	int num_modes;

	edid = ge_b850v3_lvds_get_edid(&ge_b850v3_lvds_ptr->bridge, connector);

	drm_connector_update_edid_property(connector, edid);
	num_modes = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return num_modes;
}

static enum drm_mode_status ge_b850v3_lvds_mode_valid(
		struct drm_connector *connector, struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct
drm_connector_helper_funcs ge_b850v3_lvds_connector_helper_funcs = {
	.get_modes = ge_b850v3_lvds_get_modes,
	.mode_valid = ge_b850v3_lvds_mode_valid,
};

static enum drm_connector_status ge_b850v3_lvds_bridge_detect(struct drm_bridge *bridge)
{
	struct i2c_client *stdp4028_i2c =
			ge_b850v3_lvds_ptr->stdp4028_i2c;
	s32 link_state;

	link_state = i2c_smbus_read_word_data(stdp4028_i2c,
					      STDP4028_DPTX_STS_REG);

	if (link_state == STDP4028_CON_STATE_CONNECTED)
		return connector_status_connected;

	if (link_state == 0)
		return connector_status_disconnected;

	return connector_status_unknown;
}

static enum drm_connector_status ge_b850v3_lvds_detect(struct drm_connector *connector,
						       bool force)
{
	return ge_b850v3_lvds_bridge_detect(&ge_b850v3_lvds_ptr->bridge);
}

static const struct drm_connector_funcs ge_b850v3_lvds_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = ge_b850v3_lvds_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int ge_b850v3_lvds_create_connector(struct drm_bridge *bridge)
{
	struct drm_connector *connector = &ge_b850v3_lvds_ptr->connector;
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(connector,
				 &ge_b850v3_lvds_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &ge_b850v3_lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	return drm_connector_attach_encoder(connector, bridge->encoder);
}

static irqreturn_t ge_b850v3_lvds_irq_handler(int irq, void *dev_id)
{
	struct i2c_client *stdp4028_i2c
			= ge_b850v3_lvds_ptr->stdp4028_i2c;

	i2c_smbus_write_word_data(stdp4028_i2c,
				  STDP4028_DPTX_IRQ_STS_REG,
				  STDP4028_DPTX_IRQ_CLEAR);

	if (ge_b850v3_lvds_ptr->bridge.dev)
		drm_kms_helper_hotplug_event(ge_b850v3_lvds_ptr->bridge.dev);

	return IRQ_HANDLED;
}

static int ge_b850v3_lvds_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct i2c_client *stdp4028_i2c
			= ge_b850v3_lvds_ptr->stdp4028_i2c;

	/* Configures the bridge to re-enable interrupts after each ack. */
	i2c_smbus_write_word_data(stdp4028_i2c,
				  STDP4028_IRQ_OUT_CONF_REG,
				  STDP4028_DPTX_DP_IRQ_EN);

	/* Enable interrupts */
	i2c_smbus_write_word_data(stdp4028_i2c,
				  STDP4028_DPTX_IRQ_EN_REG,
				  STDP4028_DPTX_IRQ_CONFIG);

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	return ge_b850v3_lvds_create_connector(bridge);
}

static const struct drm_bridge_funcs ge_b850v3_lvds_funcs = {
	.attach = ge_b850v3_lvds_attach,
	.detect = ge_b850v3_lvds_bridge_detect,
	.get_edid = ge_b850v3_lvds_get_edid,
};

static int ge_b850v3_lvds_init(struct device *dev)
{
	mutex_lock(&ge_b850v3_lvds_dev_mutex);

	if (ge_b850v3_lvds_ptr)
		goto success;

	ge_b850v3_lvds_ptr = devm_kzalloc(dev,
					  sizeof(*ge_b850v3_lvds_ptr),
					  GFP_KERNEL);

	if (!ge_b850v3_lvds_ptr) {
		mutex_unlock(&ge_b850v3_lvds_dev_mutex);
		return -ENOMEM;
	}

success:
	mutex_unlock(&ge_b850v3_lvds_dev_mutex);
	return 0;
}

static void ge_b850v3_lvds_remove(void)
{
	mutex_lock(&ge_b850v3_lvds_dev_mutex);
	/*
	 * This check is to avoid both the drivers
	 * removing the bridge in their remove() function
	 */
	if (!ge_b850v3_lvds_ptr ||
	    !ge_b850v3_lvds_ptr->stdp2690_i2c ||
		!ge_b850v3_lvds_ptr->stdp4028_i2c)
		goto out;

	drm_bridge_remove(&ge_b850v3_lvds_ptr->bridge);

	ge_b850v3_lvds_ptr = NULL;
out:
	mutex_unlock(&ge_b850v3_lvds_dev_mutex);
}

static int ge_b850v3_register(void)
{
	struct i2c_client *stdp4028_i2c = ge_b850v3_lvds_ptr->stdp4028_i2c;
	struct device *dev = &stdp4028_i2c->dev;

	/* drm bridge initialization */
	ge_b850v3_lvds_ptr->bridge.funcs = &ge_b850v3_lvds_funcs;
	ge_b850v3_lvds_ptr->bridge.ops = DRM_BRIDGE_OP_DETECT |
					 DRM_BRIDGE_OP_EDID;
	ge_b850v3_lvds_ptr->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	ge_b850v3_lvds_ptr->bridge.of_node = dev->of_node;
	drm_bridge_add(&ge_b850v3_lvds_ptr->bridge);

	/* Clear pending interrupts since power up. */
	i2c_smbus_write_word_data(stdp4028_i2c,
				  STDP4028_DPTX_IRQ_STS_REG,
				  STDP4028_DPTX_IRQ_CLEAR);

	if (!stdp4028_i2c->irq)
		return 0;

	return devm_request_threaded_irq(&stdp4028_i2c->dev,
			stdp4028_i2c->irq, NULL,
			ge_b850v3_lvds_irq_handler,
			IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"ge-b850v3-lvds-dp", ge_b850v3_lvds_ptr);
}

static int stdp4028_ge_b850v3_fw_probe(struct i2c_client *stdp4028_i2c,
				       const struct i2c_device_id *id)
{
	struct device *dev = &stdp4028_i2c->dev;
	int ret;

	ret = ge_b850v3_lvds_init(dev);

	if (ret)
		return ret;

	ge_b850v3_lvds_ptr->stdp4028_i2c = stdp4028_i2c;
	i2c_set_clientdata(stdp4028_i2c, ge_b850v3_lvds_ptr);

	/* Only register after both bridges are probed */
	if (!ge_b850v3_lvds_ptr->stdp2690_i2c)
		return 0;

	return ge_b850v3_register();
}

static int stdp4028_ge_b850v3_fw_remove(struct i2c_client *stdp4028_i2c)
{
	ge_b850v3_lvds_remove();

	return 0;
}

static const struct i2c_device_id stdp4028_ge_b850v3_fw_i2c_table[] = {
	{"stdp4028_ge_fw", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, stdp4028_ge_b850v3_fw_i2c_table);

static const struct of_device_id stdp4028_ge_b850v3_fw_match[] = {
	{ .compatible = "megachips,stdp4028-ge-b850v3-fw" },
	{},
};
MODULE_DEVICE_TABLE(of, stdp4028_ge_b850v3_fw_match);

static struct i2c_driver stdp4028_ge_b850v3_fw_driver = {
	.id_table	= stdp4028_ge_b850v3_fw_i2c_table,
	.probe		= stdp4028_ge_b850v3_fw_probe,
	.remove		= stdp4028_ge_b850v3_fw_remove,
	.driver		= {
		.name		= "stdp4028-ge-b850v3-fw",
		.of_match_table = stdp4028_ge_b850v3_fw_match,
	},
};

static int stdp2690_ge_b850v3_fw_probe(struct i2c_client *stdp2690_i2c,
				       const struct i2c_device_id *id)
{
	struct device *dev = &stdp2690_i2c->dev;
	int ret;

	ret = ge_b850v3_lvds_init(dev);

	if (ret)
		return ret;

	ge_b850v3_lvds_ptr->stdp2690_i2c = stdp2690_i2c;
	i2c_set_clientdata(stdp2690_i2c, ge_b850v3_lvds_ptr);

	/* Only register after both bridges are probed */
	if (!ge_b850v3_lvds_ptr->stdp4028_i2c)
		return 0;

	return ge_b850v3_register();
}

static int stdp2690_ge_b850v3_fw_remove(struct i2c_client *stdp2690_i2c)
{
	ge_b850v3_lvds_remove();

	return 0;
}

static const struct i2c_device_id stdp2690_ge_b850v3_fw_i2c_table[] = {
	{"stdp2690_ge_fw", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, stdp2690_ge_b850v3_fw_i2c_table);

static const struct of_device_id stdp2690_ge_b850v3_fw_match[] = {
	{ .compatible = "megachips,stdp2690-ge-b850v3-fw" },
	{},
};
MODULE_DEVICE_TABLE(of, stdp2690_ge_b850v3_fw_match);

static struct i2c_driver stdp2690_ge_b850v3_fw_driver = {
	.id_table	= stdp2690_ge_b850v3_fw_i2c_table,
	.probe		= stdp2690_ge_b850v3_fw_probe,
	.remove		= stdp2690_ge_b850v3_fw_remove,
	.driver		= {
		.name		= "stdp2690-ge-b850v3-fw",
		.of_match_table = stdp2690_ge_b850v3_fw_match,
	},
};

static int __init stdpxxxx_ge_b850v3_init(void)
{
	int ret;

	ret = i2c_add_driver(&stdp4028_ge_b850v3_fw_driver);
	if (ret)
		return ret;

	return i2c_add_driver(&stdp2690_ge_b850v3_fw_driver);
}
module_init(stdpxxxx_ge_b850v3_init);

static void __exit stdpxxxx_ge_b850v3_exit(void)
{
	i2c_del_driver(&stdp2690_ge_b850v3_fw_driver);
	i2c_del_driver(&stdp4028_ge_b850v3_fw_driver);
}
module_exit(stdpxxxx_ge_b850v3_exit);

MODULE_AUTHOR("Peter Senna Tschudin <peter.senna@collabora.com>");
MODULE_AUTHOR("Martyn Welch <martyn.welch@collabora.co.uk>");
MODULE_DESCRIPTION("GE LVDS to DP++ display bridge)");
MODULE_LICENSE("GPL v2");
