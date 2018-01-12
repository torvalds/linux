/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
		err = output->panel->funcs->get_modes(output->panel);
		if (err > 0)
			return err;
	}

	if (output->edid)
		edid = kmemdup(output->edid, sizeof(*edid), GFP_KERNEL);
	else if (output->ddc)
		edid = drm_get_edid(connector, output->ddc);

	cec_notifier_set_phys_addr_from_edid(output->notifier, edid);
	drm_mode_connector_update_edid_property(connector, edid);

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

	if (gpio_is_valid(output->hpd_gpio)) {
		if (output->hpd_gpio_flags & OF_GPIO_ACTIVE_LOW) {
			if (gpio_get_value(output->hpd_gpio) != 0)
				status = connector_status_disconnected;
			else
				status = connector_status_connected;
		} else {
			if (gpio_get_value(output->hpd_gpio) == 0)
				status = connector_status_disconnected;
			else
				status = connector_status_connected;
		}
	} else {
		if (!output->panel)
			status = connector_status_disconnected;
		else
			status = connector_status_connected;
	}

	if (status != connector_status_connected)
		cec_notifier_phys_addr_invalidate(output->notifier);

	return status;
}

void tegra_output_connector_destroy(struct drm_connector *connector)
{
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
	int err, size;

	if (!output->of_node)
		output->of_node = output->dev->of_node;

	panel = of_parse_phandle(output->of_node, "nvidia,panel", 0);
	if (panel) {
		output->panel = of_drm_find_panel(panel);
		if (!output->panel)
			return -EPROBE_DEFER;

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

	output->hpd_gpio = of_get_named_gpio_flags(output->of_node,
						   "nvidia,hpd-gpio", 0,
						   &output->hpd_gpio_flags);
	if (gpio_is_valid(output->hpd_gpio)) {
		unsigned long flags;

		err = gpio_request_one(output->hpd_gpio, GPIOF_DIR_IN,
				       "HDMI hotplug detect");
		if (err < 0) {
			dev_err(output->dev, "gpio_request_one(): %d\n", err);
			return err;
		}

		err = gpio_to_irq(output->hpd_gpio);
		if (err < 0) {
			dev_err(output->dev, "gpio_to_irq(): %d\n", err);
			gpio_free(output->hpd_gpio);
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
			gpio_free(output->hpd_gpio);
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
	if (gpio_is_valid(output->hpd_gpio)) {
		free_irq(output->hpd_irq, output);
		gpio_free(output->hpd_gpio);
	}

	if (output->ddc)
		put_device(&output->ddc->dev);
}

int tegra_output_init(struct drm_device *drm, struct tegra_output *output)
{
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
	if (gpio_is_valid(output->hpd_gpio))
		enable_irq(output->hpd_irq);

	return 0;
}

void tegra_output_exit(struct tegra_output *output)
{
	/*
	 * The connector is going away, so the interrupt must be disabled to
	 * prevent the hotplug interrupt handler from potentially crashing.
	 */
	if (gpio_is_valid(output->hpd_gpio))
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
