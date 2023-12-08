// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>

struct display_connector {
	struct drm_bridge	bridge;

	struct gpio_desc	*hpd_gpio;
	int			hpd_irq;

	struct regulator	*dp_pwr;
	struct gpio_desc	*ddc_en;
};

static inline struct display_connector *
to_display_connector(struct drm_bridge *bridge)
{
	return container_of(bridge, struct display_connector, bridge);
}

static int display_connector_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	return flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR ? 0 : -EINVAL;
}

static enum drm_connector_status
display_connector_detect(struct drm_bridge *bridge)
{
	struct display_connector *conn = to_display_connector(bridge);

	if (conn->hpd_gpio) {
		if (gpiod_get_value_cansleep(conn->hpd_gpio))
			return connector_status_connected;
		else
			return connector_status_disconnected;
	}

	if (conn->bridge.ddc && drm_probe_ddc(conn->bridge.ddc))
		return connector_status_connected;

	switch (conn->bridge.type) {
	case DRM_MODE_CONNECTOR_DVIA:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		/*
		 * For DVI and HDMI connectors a DDC probe failure indicates
		 * that no cable is connected.
		 */
		return connector_status_disconnected;

	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_VGA:
	default:
		/*
		 * Composite and S-Video connectors have no other detection
		 * mean than the HPD GPIO. For VGA connectors, even if we have
		 * an I2C bus, we can't assume that the cable is disconnected
		 * if drm_probe_ddc fails, as some cables don't wire the DDC
		 * pins.
		 */
		return connector_status_unknown;
	}
}

static struct edid *display_connector_get_edid(struct drm_bridge *bridge,
					       struct drm_connector *connector)
{
	struct display_connector *conn = to_display_connector(bridge);

	return drm_get_edid(connector, conn->bridge.ddc);
}

/*
 * Since this bridge is tied to the connector, it acts like a passthrough,
 * so concerning the output bus formats, either pass the bus formats from the
 * previous bridge or return fallback data like done in the bridge function:
 * drm_atomic_bridge_chain_select_bus_fmts().
 * This supports negotiation if the bridge chain has all bits in place.
 */
static u32 *display_connector_get_output_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					unsigned int *num_output_fmts)
{
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);
	struct drm_bridge_state *prev_bridge_state;

	if (!prev_bridge || !prev_bridge->funcs->atomic_get_output_bus_fmts) {
		struct drm_connector *conn = conn_state->connector;
		u32 *out_bus_fmts;

		*num_output_fmts = 1;
		out_bus_fmts = kmalloc(sizeof(*out_bus_fmts), GFP_KERNEL);
		if (!out_bus_fmts)
			return NULL;

		if (conn->display_info.num_bus_formats &&
		    conn->display_info.bus_formats)
			out_bus_fmts[0] = conn->display_info.bus_formats[0];
		else
			out_bus_fmts[0] = MEDIA_BUS_FMT_FIXED;

		return out_bus_fmts;
	}

	prev_bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
							    prev_bridge);

	return prev_bridge->funcs->atomic_get_output_bus_fmts(prev_bridge, prev_bridge_state,
							      crtc_state, conn_state,
							      num_output_fmts);
}

/*
 * Since this bridge is tied to the connector, it acts like a passthrough,
 * so concerning the input bus formats, either pass the bus formats from the
 * previous bridge or MEDIA_BUS_FMT_FIXED (like select_bus_fmt_recursive())
 * when atomic_get_input_bus_fmts is not supported.
 * This supports negotiation if the bridge chain has all bits in place.
 */
static u32 *display_connector_get_input_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);
	struct drm_bridge_state *prev_bridge_state;

	if (!prev_bridge || !prev_bridge->funcs->atomic_get_input_bus_fmts) {
		u32 *in_bus_fmts;

		*num_input_fmts = 1;
		in_bus_fmts = kmalloc(sizeof(*in_bus_fmts), GFP_KERNEL);
		if (!in_bus_fmts)
			return NULL;

		in_bus_fmts[0] = MEDIA_BUS_FMT_FIXED;

		return in_bus_fmts;
	}

	prev_bridge_state = drm_atomic_get_new_bridge_state(crtc_state->state,
							    prev_bridge);

	return prev_bridge->funcs->atomic_get_input_bus_fmts(prev_bridge, prev_bridge_state,
							     crtc_state, conn_state, output_fmt,
							     num_input_fmts);
}

static const struct drm_bridge_funcs display_connector_bridge_funcs = {
	.attach = display_connector_attach,
	.detect = display_connector_detect,
	.get_edid = display_connector_get_edid,
	.atomic_get_output_bus_fmts = display_connector_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = display_connector_get_input_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static irqreturn_t display_connector_hpd_irq(int irq, void *arg)
{
	struct display_connector *conn = arg;
	struct drm_bridge *bridge = &conn->bridge;

	drm_bridge_hpd_notify(bridge, display_connector_detect(bridge));

	return IRQ_HANDLED;
}

static int display_connector_probe(struct platform_device *pdev)
{
	struct display_connector *conn;
	unsigned int type;
	const char *label = NULL;
	int ret;

	conn = devm_kzalloc(&pdev->dev, sizeof(*conn), GFP_KERNEL);
	if (!conn)
		return -ENOMEM;

	platform_set_drvdata(pdev, conn);

	type = (uintptr_t)of_device_get_match_data(&pdev->dev);

	/* Get the exact connector type. */
	switch (type) {
	case DRM_MODE_CONNECTOR_DVII: {
		bool analog, digital;

		analog = of_property_read_bool(pdev->dev.of_node, "analog");
		digital = of_property_read_bool(pdev->dev.of_node, "digital");
		if (analog && !digital) {
			conn->bridge.type = DRM_MODE_CONNECTOR_DVIA;
		} else if (!analog && digital) {
			conn->bridge.type = DRM_MODE_CONNECTOR_DVID;
		} else if (analog && digital) {
			conn->bridge.type = DRM_MODE_CONNECTOR_DVII;
		} else {
			dev_err(&pdev->dev, "DVI connector with no type\n");
			return -EINVAL;
		}
		break;
	}

	case DRM_MODE_CONNECTOR_HDMIA: {
		const char *hdmi_type;

		ret = of_property_read_string(pdev->dev.of_node, "type",
					      &hdmi_type);
		if (ret < 0) {
			dev_err(&pdev->dev, "HDMI connector with no type\n");
			return -EINVAL;
		}

		if (!strcmp(hdmi_type, "a") || !strcmp(hdmi_type, "c") ||
		    !strcmp(hdmi_type, "d") || !strcmp(hdmi_type, "e")) {
			conn->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
		} else if (!strcmp(hdmi_type, "b")) {
			conn->bridge.type = DRM_MODE_CONNECTOR_HDMIB;
		} else {
			dev_err(&pdev->dev,
				"Unsupported HDMI connector type '%s'\n",
				hdmi_type);
			return -EINVAL;
		}

		break;
	}

	default:
		conn->bridge.type = type;
		break;
	}

	/* All the supported connector types support interlaced modes. */
	conn->bridge.interlace_allowed = true;

	/* Get the optional connector label. */
	of_property_read_string(pdev->dev.of_node, "label", &label);

	/*
	 * Get the HPD GPIO for DVI, HDMI and DP connectors. If the GPIO can provide
	 * edge interrupts, register an interrupt handler.
	 */
	if (type == DRM_MODE_CONNECTOR_DVII ||
	    type == DRM_MODE_CONNECTOR_HDMIA ||
	    type == DRM_MODE_CONNECTOR_DisplayPort) {
		conn->hpd_gpio = devm_gpiod_get_optional(&pdev->dev, "hpd",
							 GPIOD_IN);
		if (IS_ERR(conn->hpd_gpio)) {
			if (PTR_ERR(conn->hpd_gpio) != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"Unable to retrieve HPD GPIO\n");
			return PTR_ERR(conn->hpd_gpio);
		}

		conn->hpd_irq = gpiod_to_irq(conn->hpd_gpio);
	} else {
		conn->hpd_irq = -EINVAL;
	}

	if (conn->hpd_irq >= 0) {
		ret = devm_request_threaded_irq(&pdev->dev, conn->hpd_irq,
						NULL, display_connector_hpd_irq,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"HPD", conn);
		if (ret) {
			dev_info(&pdev->dev,
				 "Failed to request HPD edge interrupt, falling back to polling\n");
			conn->hpd_irq = -EINVAL;
		}
	}

	/* Retrieve the DDC I2C adapter for DVI, HDMI and VGA connectors. */
	if (type == DRM_MODE_CONNECTOR_DVII ||
	    type == DRM_MODE_CONNECTOR_HDMIA ||
	    type == DRM_MODE_CONNECTOR_VGA) {
		struct device_node *phandle;

		phandle = of_parse_phandle(pdev->dev.of_node, "ddc-i2c-bus", 0);
		if (phandle) {
			conn->bridge.ddc = of_get_i2c_adapter_by_node(phandle);
			of_node_put(phandle);
			if (!conn->bridge.ddc)
				return -EPROBE_DEFER;
		} else {
			dev_dbg(&pdev->dev,
				"No I2C bus specified, disabling EDID readout\n");
		}
	}

	/* Get the DP PWR for DP connector. */
	if (type == DRM_MODE_CONNECTOR_DisplayPort) {
		int ret;

		conn->dp_pwr = devm_regulator_get_optional(&pdev->dev, "dp-pwr");

		if (IS_ERR(conn->dp_pwr)) {
			ret = PTR_ERR(conn->dp_pwr);

			switch (ret) {
			case -ENODEV:
				conn->dp_pwr = NULL;
				break;

			case -EPROBE_DEFER:
				return -EPROBE_DEFER;

			default:
				dev_err(&pdev->dev, "failed to get DP PWR regulator: %d\n", ret);
				return ret;
			}
		}

		if (conn->dp_pwr) {
			ret = regulator_enable(conn->dp_pwr);
			if (ret) {
				dev_err(&pdev->dev, "failed to enable DP PWR regulator: %d\n", ret);
				return ret;
			}
		}
	}

	/* enable DDC */
	if (type == DRM_MODE_CONNECTOR_HDMIA) {
		conn->ddc_en = devm_gpiod_get_optional(&pdev->dev, "ddc-en",
						       GPIOD_OUT_HIGH);

		if (IS_ERR(conn->ddc_en)) {
			dev_err(&pdev->dev, "Couldn't get ddc-en gpio\n");
			return PTR_ERR(conn->ddc_en);
		}
	}

	conn->bridge.funcs = &display_connector_bridge_funcs;
	conn->bridge.of_node = pdev->dev.of_node;

	if (conn->bridge.ddc)
		conn->bridge.ops |= DRM_BRIDGE_OP_EDID
				 |  DRM_BRIDGE_OP_DETECT;
	if (conn->hpd_gpio)
		conn->bridge.ops |= DRM_BRIDGE_OP_DETECT;
	if (conn->hpd_irq >= 0)
		conn->bridge.ops |= DRM_BRIDGE_OP_HPD;

	dev_dbg(&pdev->dev,
		"Found %s display connector '%s' %s DDC bus and %s HPD GPIO (ops 0x%x)\n",
		drm_get_connector_type_name(conn->bridge.type),
		label ? label : "<unlabelled>",
		conn->bridge.ddc ? "with" : "without",
		conn->hpd_gpio ? "with" : "without",
		conn->bridge.ops);

	drm_bridge_add(&conn->bridge);

	return 0;
}

static int display_connector_remove(struct platform_device *pdev)
{
	struct display_connector *conn = platform_get_drvdata(pdev);

	if (conn->ddc_en)
		gpiod_set_value(conn->ddc_en, 0);

	if (conn->dp_pwr)
		regulator_disable(conn->dp_pwr);

	drm_bridge_remove(&conn->bridge);

	if (!IS_ERR(conn->bridge.ddc))
		i2c_put_adapter(conn->bridge.ddc);

	return 0;
}

static const struct of_device_id display_connector_match[] = {
	{
		.compatible = "composite-video-connector",
		.data = (void *)DRM_MODE_CONNECTOR_Composite,
	}, {
		.compatible = "dvi-connector",
		.data = (void *)DRM_MODE_CONNECTOR_DVII,
	}, {
		.compatible = "hdmi-connector",
		.data = (void *)DRM_MODE_CONNECTOR_HDMIA,
	}, {
		.compatible = "svideo-connector",
		.data = (void *)DRM_MODE_CONNECTOR_SVIDEO,
	}, {
		.compatible = "vga-connector",
		.data = (void *)DRM_MODE_CONNECTOR_VGA,
	}, {
		.compatible = "dp-connector",
		.data = (void *)DRM_MODE_CONNECTOR_DisplayPort,
	},
	{},
};
MODULE_DEVICE_TABLE(of, display_connector_match);

static struct platform_driver display_connector_driver = {
	.probe	= display_connector_probe,
	.remove	= display_connector_remove,
	.driver		= {
		.name		= "display-connector",
		.of_match_table	= display_connector_match,
	},
};
module_platform_driver(display_connector_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Display connector driver");
MODULE_LICENSE("GPL");
