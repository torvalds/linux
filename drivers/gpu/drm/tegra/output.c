// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include "drm.h"
#include "dc.h"

#include <media/cec-notifier.h>

int tegra_output_connector_get_modes(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);
	struct edid *edid = NULL;
	int err = 0;

	/*
	 * If the panel provides one or more modes, use them exclusively and
	 * ignore any other means of obtaining a mode.
	 */
	if (output->panel) {
		err = drm_panel_get_modes(output->panel, connector);
		if (err > 0)
			return err;
	}

	if (output->edid)
		edid = kmemdup(output->edid, sizeof(*edid), GFP_KERNEL);
	else if (output->ddc)
		edid = drm_get_edid(connector, output->ddc);

	cec_notifier_set_phys_addr_from_edid(output->cec, edid);
	drm_connector_update_edid_property(connector, edid);

	if (edid) {
		err = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return err;
}

enum drm_connector_status
tegra_output_connector_detect(struct drm_connector *connector, bool force)
{
	struct tegra_output *output = connector_to_output(connector);
	enum drm_connector_status status = connector_status_unknown;

	if (output->hpd_gpio) {
		if (gpiod_get_value(output->hpd_gpio) == 0)
			status = connector_status_disconnected;
		else
			status = connector_status_connected;
	} else {
		if (!output->panel)
			status = connector_status_disconnected;
		else
			status = connector_status_connected;
	}

	if (status != connector_status_connected)
		cec_notifier_phys_addr_invalidate(output->cec);

	return status;
}

void tegra_output_connector_destroy(struct drm_connector *connector)
{
	struct tegra_output *output = connector_to_output(connector);

	if (output->cec)
		cec_notifier_conn_unregister(output->cec);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

void tegra_output_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static irqreturn_t hpd_irq(int irq, void *data)
{
	struct tegra_output *output = data;

	if (output->connector.dev)
		drm_helper_hpd_irq_event(output->connector.dev);

	return IRQ_HANDLED;
}

int tegra_output_probe(struct tegra_output *output)
{
	struct device_node *ddc, *panel;
	unsigned long flags;
	int err, size;

	if (!output->of_node)
		output->of_node = output->dev->of_node;

	panel = of_parse_phandle(output->of_node, "nvidia,panel", 0);
	if (panel) {
		output->panel = of_drm_find_panel(panel);
		if (IS_ERR(output->panel))
			return PTR_ERR(output->panel);

		of_node_put(panel);
	}

	output->edid = of_get_property(output->of_node, "nvidia,edid", &size);

	ddc = of_parse_phandle(output->of_node, "nvidia,ddc-i2c-bus", 0);
	if (ddc) {
		output->ddc = of_find_i2c_adapter_by_node(ddc);
		if (!output->ddc) {
			err = -EPROBE_DEFER;
			of_node_put(ddc);
			return err;
		}

		of_node_put(ddc);
	}

	output->hpd_gpio = devm_gpiod_get_from_of_node(output->dev,
						       output->of_node,
						       "nvidia,hpd-gpio", 0,
						       GPIOD_IN,
						       "HDMI hotplug detect");
	if (IS_ERR(output->hpd_gpio)) {
		if (PTR_ERR(output->hpd_gpio) != -ENOENT)
			return PTR_ERR(output->hpd_gpio);

		output->hpd_gpio = NULL;
	}

	if (output->hpd_gpio) {
		err = gpiod_to_irq(output->hpd_gpio);
		if (err < 0) {
			dev_err(output->dev, "gpiod_to_irq(): %d\n", err);
			return err;
		}

		output->hpd_irq = err;

		flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			IRQF_ONESHOT;

		err = request_threaded_irq(output->hpd_irq, NULL, hpd_irq,
					   flags, "hpd", output);
		if (err < 0) {
			dev_err(output->dev, "failed to request IRQ#%u: %d\n",
				output->hpd_irq, err);
			return err;
		}

		output->connector.polled = DRM_CONNECTOR_POLL_HPD;

		/*
		 * Disable the interrupt until the connector has been
		 * initialized to avoid a race in the hotplug interrupt
		 * handler.
		 */
		disable_irq(output->hpd_irq);
	}

	return 0;
}

void tegra_output_remove(struct tegra_output *output)
{
	if (output->hpd_gpio)
		free_irq(output->hpd_irq, output);

	if (output->ddc)
		put_device(&output->ddc->dev);
}

int tegra_output_init(struct drm_device *drm, struct tegra_output *output)
{
	int connector_type;
	int err;

	if (output->panel) {
		err = drm_panel_attach(output->panel, &output->connector);
		if (err < 0)
			return err;
	}

	/*
	 * The connector is now registered and ready to receive hotplug events
	 * so the hotplug interrupt can be enabled.
	 */
	if (output->hpd_gpio)
		enable_irq(output->hpd_irq);

	connector_type = output->connector.connector_type;
	/*
	 * Create a CEC notifier for HDMI connector.
	 */
	if (connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		struct cec_connector_info conn_info;

		cec_fill_conn_info_from_drm(&conn_info, &output->connector);
		output->cec = cec_notifier_conn_register(output->dev, NULL,
							 &conn_info);
		if (!output->cec)
			return -ENOMEM;
	}

	return 0;
}

void tegra_output_exit(struct tegra_output *output)
{
	/*
	 * The connector is going away, so the interrupt must be disabled to
	 * prevent the hotplug interrupt handler from potentially crashing.
	 */
	if (output->hpd_gpio)
		disable_irq(output->hpd_irq);

	if (output->panel)
		drm_panel_detach(output->panel);
}

void tegra_output_find_possible_crtcs(struct tegra_output *output,
				      struct drm_device *drm)
{
	struct device *dev = output->dev;
	struct drm_crtc *crtc;
	unsigned int mask = 0;

	drm_for_each_crtc(crtc, drm) {
		struct tegra_dc *dc = to_tegra_dc(crtc);

		if (tegra_dc_has_output(dc, dev))
			mask |= drm_crtc_mask(crtc);
	}

	if (mask == 0) {
		dev_warn(dev, "missing output definition for heads in DT\n");
		mask = 0x3;
	}

	output->encoder.possible_crtcs = mask;
}

int tegra_output_suspend(struct tegra_output *output)
{
	if (output->hpd_irq)
		disable_irq(output->hpd_irq);

	return 0;
}

int tegra_output_resume(struct tegra_output *output)
{
	if (output->hpd_irq)
		enable_irq(output->hpd_irq);

	return 0;
}
