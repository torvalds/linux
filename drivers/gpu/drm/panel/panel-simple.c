/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>
#include <linux/of_device.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

struct cmd_ctrl_hdr {
	u8 dtype;	/* data type */
	u8 wait;	/* ms */
	u8 dlen;	/* payload len */
} __packed;

struct cmd_desc {
	struct cmd_ctrl_hdr dchdr;
	u8 *payload;
};

struct panel_cmds {
	u8 *buf;
	int blen;
	struct cmd_desc *cmds;
	int cmd_cnt;
};

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct display_timing *timings;
	unsigned int num_timings;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @reset: the time (in milliseconds) indicates the delay time
	 *         after the panel to operate reset gpio
	 * @init: the time (in milliseconds) that it takes for the panel to
	 *           power on and dsi host can send command to panel
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *           become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *          display the first valid frame after starting to receive
	 *          video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *           turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *             to power itself down completely
	 */
	struct {
		unsigned int reset;
		unsigned int init;
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;

	u32 bus_format;
};

struct panel_simple {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;
	bool prepared;
	bool enabled;
	bool power_invert;

	struct device *dev;
	const struct panel_desc *desc;

	struct backlight_device *backlight;
	struct regulator *supply;
	struct i2c_adapter *ddc;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	int cmd_type;

	struct gpio_desc *spi_sdi_gpio;
	struct gpio_desc *spi_scl_gpio;
	struct gpio_desc *spi_cs_gpio;

	struct panel_cmds *on_cmds;
	struct panel_cmds *off_cmds;
};

enum rockchip_cmd_type {
	CMD_TYPE_DEFAULT,
	CMD_TYPE_SPI,
	CMD_TYPE_MCU
};

static inline int get_panel_cmd_type(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "spi", 3) == 0)
		return CMD_TYPE_SPI;
	else if (strncmp(s, "mcu", 3) == 0)
		return CMD_TYPE_MCU;

	return CMD_TYPE_DEFAULT;
}

static inline struct panel_simple *to_panel_simple(struct drm_panel *panel)
{
	return container_of(panel, struct panel_simple, base);
}

static void panel_simple_cmds_cleanup(struct panel_simple *p)
{
	if (p->on_cmds) {
		kfree(p->on_cmds->buf);
		kfree(p->on_cmds->cmds);
	}

	if (p->off_cmds) {
		kfree(p->off_cmds->buf);
		kfree(p->off_cmds->cmds);
	}
}

static int panel_simple_parse_cmds(struct device *dev,
				   const u8 *data, int blen,
				   struct panel_cmds *pcmds)
{
	int len;
	char *buf, *bp;
	struct cmd_ctrl_hdr *dchdr;
	int i, cnt;

	if (!pcmds)
		return -EINVAL;

	buf = kmemdup(data, blen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* scan init commands */
	bp = buf;
	len = blen;
	cnt = 0;
	while (len > sizeof(*dchdr)) {
		dchdr = (struct cmd_ctrl_hdr *)bp;

		if (dchdr->dlen > len) {
			dev_err(dev, "%s: error, len=%d", __func__,
				dchdr->dlen);
			return -EINVAL;
		}

		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		dev_err(dev, "%s: dcs_cmd=%x len=%d error!",
			__func__, buf[0], blen);
		kfree(buf);
		return -EINVAL;
	}

	pcmds->cmds = kcalloc(cnt, sizeof(struct cmd_desc), GFP_KERNEL);
	if (!pcmds->cmds) {
		kfree(buf);
		return -ENOMEM;
	}

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	bp = buf;
	len = blen;
	for (i = 0; i < cnt; i++) {
		dchdr = (struct cmd_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	return 0;
}

static void panel_simple_spi_write_cmd(struct panel_simple *panel,
				       u8 type, int value)
{
	int i;

	gpiod_direction_output(panel->spi_cs_gpio, 0);

	if (type == 0)
		value &= (~(1 << 8));
	else
		value |= (1 << 8);

	for (i = 0; i < 9; i++) {
		if (value & 0x100)
			gpiod_direction_output(panel->spi_sdi_gpio, 1);
		else
			gpiod_direction_output(panel->spi_sdi_gpio, 0);

		gpiod_direction_output(panel->spi_scl_gpio, 0);
		udelay(10);
		gpiod_direction_output(panel->spi_scl_gpio, 1);
		value <<= 1;
		udelay(10);
	}

	gpiod_direction_output(panel->spi_cs_gpio, 1);
}

static int panel_simple_spi_send_cmds(struct panel_simple *panel,
				      struct panel_cmds *cmds)
{
	int i;

	if (!cmds)
		return -EINVAL;

	for (i = 0; i < cmds->cmd_cnt; i++) {
		struct cmd_desc *cmd = &cmds->cmds[i];
		int value = 0;

		if (cmd->dchdr.dlen == 2)
			value = (cmd->payload[0] << 8) | cmd->payload[1];
		else
			value = cmd->payload[0];
		panel_simple_spi_write_cmd(panel, cmd->dchdr.dtype, value);

		if (cmd->dchdr.wait)
			msleep(cmd->dchdr.wait);
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_MIPI_DSI)
static int panel_simple_dsi_send_cmds(struct panel_simple *panel,
				      struct panel_cmds *cmds)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	int i, err;

	if (!cmds)
		return -EINVAL;

	for (i = 0; i < cmds->cmd_cnt; i++) {
		struct cmd_desc *cmd = &cmds->cmds[i];

		switch (cmd->dchdr.dtype) {
		case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_GENERIC_LONG_WRITE:
			err = mipi_dsi_generic_write(dsi, cmd->payload,
						     cmd->dchdr.dlen);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			err = mipi_dsi_dcs_write_buffer(dsi, cmd->payload,
							cmd->dchdr.dlen);
			break;
		default:
			return -EINVAL;
		}

		if (err < 0)
			dev_err(panel->dev, "failed to write dcs cmd: %d\n",
				err);

		if (cmd->dchdr.wait)
			msleep(cmd->dchdr.wait);
	}

	return 0;
}
#else
static inline int panel_simple_dsi_send_cmds(struct panel_simple *panel,
					     struct panel_cmds *cmds)
{
	return -EINVAL;
}
#endif

static int panel_simple_get_cmds(struct panel_simple *panel)
{
	const void *data;
	int len;
	int err;

	data = of_get_property(panel->dev->of_node, "panel-init-sequence",
			       &len);
	if (data) {
		panel->on_cmds = devm_kzalloc(panel->dev,
					      sizeof(*panel->on_cmds),
					      GFP_KERNEL);
		if (!panel->on_cmds)
			return -ENOMEM;

		err = panel_simple_parse_cmds(panel->dev, data, len,
					      panel->on_cmds);
		if (err) {
			dev_err(panel->dev, "failed to parse panel init sequence\n");
			return err;
		}
	}

	data = of_get_property(panel->dev->of_node, "panel-exit-sequence",
			       &len);
	if (data) {
		panel->off_cmds = devm_kzalloc(panel->dev,
					       sizeof(*panel->off_cmds),
					       GFP_KERNEL);
		if (!panel->off_cmds)
			return -ENOMEM;

		err = panel_simple_parse_cmds(panel->dev, data, len,
					      panel->off_cmds);
		if (err) {
			dev_err(panel->dev, "failed to parse panel exit sequence\n");
			return err;
		}
	}
	return 0;
}

static int panel_simple_get_fixed_modes(struct panel_simple *panel)
{
	struct drm_connector *connector = panel->base.connector;
	struct drm_device *drm = panel->base.drm;
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	if (!panel->desc)
		return 0;

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];
		struct videomode vm;

		videomode_from_timing(dt, &vm);
		mode = drm_mode_create(drm);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u\n",
				dt->hactive.typ, dt->vactive.typ);
			continue;
		}

		drm_display_mode_from_videomode(&vm, mode);
		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	for (i = 0; i < panel->desc->num_modes; i++) {
		const struct drm_display_mode *m = &panel->desc->modes[i];

		mode = drm_mode_duplicate(drm, m);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	connector->display_info.bpc = panel->desc->bpc;
	connector->display_info.width_mm = panel->desc->size.width;
	connector->display_info.height_mm = panel->desc->size.height;
	if (panel->desc->bus_format)
		drm_display_info_set_bus_formats(&connector->display_info,
						 &panel->desc->bus_format, 1);

	return num;
}

static int panel_simple_of_get_native_mode(struct panel_simple *panel)
{
	struct drm_connector *connector = panel->base.connector;
	struct drm_device *drm = panel->base.drm;
	struct drm_display_mode *mode;
	struct device_node *timings_np;
	int ret;

	timings_np = of_get_child_by_name(panel->dev->of_node,
					  "display-timings");
	if (!timings_np) {
		dev_dbg(panel->dev, "failed to find display-timings node\n");
		return 0;
	}

	of_node_put(timings_np);
	mode = drm_mode_create(drm);
	if (!mode)
		return 0;

	ret = of_get_drm_display_mode(panel->dev->of_node, mode,
				      OF_USE_NATIVE_MODE);
	if (ret) {
		dev_dbg(panel->dev, "failed to find dts display timings\n");
		drm_mode_destroy(drm, mode);
		return 0;
	}

	drm_mode_set_name(mode);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static int panel_simple_regulator_enable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int err = 0;

	if (p->power_invert) {
		if (regulator_is_enabled(p->supply) > 0)
			regulator_disable(p->supply);
	} else {
		err = regulator_enable(p->supply);
		if (err < 0) {
			dev_err(panel->dev, "failed to enable supply: %d\n",
				err);
			return err;
		}
	}

	return err;
}

static int panel_simple_regulator_disable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int err = 0;

	if (p->power_invert) {
		if (!regulator_is_enabled(p->supply)) {
			err = regulator_enable(p->supply);
			if (err < 0) {
				dev_err(panel->dev, "failed to enable supply: %d\n",
					err);
				return err;
			}
		}
	} else {
		regulator_disable(p->supply);
	}

	return err;
}

static int panel_simple_loader_protect(struct drm_panel *panel, bool on)
{
	int err;

	if (on) {
		err = panel_simple_regulator_enable(panel);
		if (err < 0) {
			dev_err(panel->dev, "failed to enable supply: %d\n",
				err);
			return err;
		}
	} else {
		panel_simple_regulator_disable(panel);
	}

	return 0;
}

static int panel_simple_disable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (!p->enabled)
		return 0;

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(p->backlight);
	}

	if (p->desc && p->desc->delay.disable)
		msleep(p->desc->delay.disable);

	p->enabled = false;

	return 0;
}

static int panel_simple_unprepare(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int err = 0;

	if (!p->prepared)
		return 0;

	if (p->off_cmds) {
		if (p->dsi)
			err = panel_simple_dsi_send_cmds(p, p->off_cmds);
		else if (p->cmd_type == CMD_TYPE_SPI)
			err = panel_simple_spi_send_cmds(p, p->off_cmds);
		if (err)
			dev_err(p->dev, "failed to send off cmds\n");
	}

	if (p->reset_gpio)
		gpiod_direction_output(p->reset_gpio, 1);

	if (p->enable_gpio)
		gpiod_direction_output(p->enable_gpio, 0);

	panel_simple_regulator_disable(panel);

	if (p->desc && p->desc->delay.unprepare)
		msleep(p->desc->delay.unprepare);

	p->prepared = false;

	return 0;
}

static int panel_simple_prepare(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int err;

	if (p->prepared)
		return 0;

	err = panel_simple_regulator_enable(panel);
	if (err < 0) {
		dev_err(panel->dev, "failed to enable supply: %d\n", err);
		return err;
	}

	if (p->enable_gpio)
		gpiod_direction_output(p->enable_gpio, 1);

	if (p->desc && p->desc->delay.prepare)
		msleep(p->desc->delay.prepare);

	if (p->reset_gpio)
		gpiod_direction_output(p->reset_gpio, 1);

	if (p->desc && p->desc->delay.reset)
		msleep(p->desc->delay.reset);

	if (p->reset_gpio)
		gpiod_direction_output(p->reset_gpio, 0);

	if (p->desc && p->desc->delay.init)
		msleep(p->desc->delay.init);

	if (p->on_cmds) {
		if (p->dsi)
			err = panel_simple_dsi_send_cmds(p, p->on_cmds);
		else if (p->cmd_type == CMD_TYPE_SPI)
			err = panel_simple_spi_send_cmds(p, p->on_cmds);
		if (err)
			dev_err(p->dev, "failed to send on cmds\n");
	}

	p->prepared = true;

	return 0;
}

static int panel_simple_enable(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);

	if (p->enabled)
		return 0;

	if (p->desc && p->desc->delay.enable)
		msleep(p->desc->delay.enable);

	if (p->backlight) {
		p->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(p->backlight);
	}

	p->enabled = true;

	return 0;
}

static int panel_simple_get_modes(struct drm_panel *panel)
{
	struct panel_simple *p = to_panel_simple(panel);
	int num = 0;

	/* add device node plane modes */
	num += panel_simple_of_get_native_mode(p);

	/* add hard-coded panel modes */
	num += panel_simple_get_fixed_modes(p);

	/* probe EDID if a DDC bus is available */
	if (p->ddc) {
		struct edid *edid = drm_get_edid(panel->connector, p->ddc);
		drm_mode_connector_update_edid_property(panel->connector, edid);
		if (edid) {
			num += drm_add_edid_modes(panel->connector, edid);
			kfree(edid);
		}
	}

	return num;
}

static int panel_simple_get_timings(struct drm_panel *panel,
				    unsigned int num_timings,
				    struct display_timing *timings)
{
	struct panel_simple *p = to_panel_simple(panel);
	unsigned int i;

	if (!p->desc)
		return 0;

	if (p->desc->num_timings < num_timings)
		num_timings = p->desc->num_timings;

	if (timings)
		for (i = 0; i < num_timings; i++)
			timings[i] = p->desc->timings[i];

	return p->desc->num_timings;
}

static const struct drm_panel_funcs panel_simple_funcs = {
	.loader_protect = panel_simple_loader_protect,
	.disable = panel_simple_disable,
	.unprepare = panel_simple_unprepare,
	.prepare = panel_simple_prepare,
	.enable = panel_simple_enable,
	.get_modes = panel_simple_get_modes,
	.get_timings = panel_simple_get_timings,
};

static int panel_simple_probe(struct device *dev, const struct panel_desc *desc)
{
	struct device_node *backlight, *ddc;
	struct panel_simple *panel;
	struct panel_desc *of_desc;
	const char *cmd_type;
	u32 val;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	if (!desc)
		of_desc = devm_kzalloc(dev, sizeof(*of_desc), GFP_KERNEL);
	else
		of_desc = devm_kmemdup(dev, desc, sizeof(*of_desc), GFP_KERNEL);

	if (!of_property_read_u32(dev->of_node, "bus-format", &val))
		of_desc->bus_format = val;
	if (!of_property_read_u32(dev->of_node, "bpc", &val))
		of_desc->bpc = val;
	if (!of_property_read_u32(dev->of_node, "prepare-delay-ms", &val))
		of_desc->delay.prepare = val;
	if (!of_property_read_u32(dev->of_node, "enable-delay-ms", &val))
		of_desc->delay.enable = val;
	if (!of_property_read_u32(dev->of_node, "disable-delay-ms", &val))
		of_desc->delay.disable = val;
	if (!of_property_read_u32(dev->of_node, "unprepare-delay-ms", &val))
		of_desc->delay.unprepare = val;
	if (!of_property_read_u32(dev->of_node, "reset-delay-ms", &val))
		of_desc->delay.reset = val;
	if (!of_property_read_u32(dev->of_node, "init-delay-ms", &val))
		of_desc->delay.init = val;
	if (!of_property_read_u32(dev->of_node, "width-mm", &val))
		of_desc->size.width = val;
	if (!of_property_read_u32(dev->of_node, "height-mm", &val))
		of_desc->size.height = val;

	panel->enabled = false;
	panel->prepared = false;
	panel->desc = of_desc;
	panel->dev = dev;

	err = panel_simple_get_cmds(panel);
	if (err) {
		dev_err(dev, "failed to get init cmd: %d\n", err);
		return err;
	}
	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply))
		return PTR_ERR(panel->supply);

	panel->enable_gpio = devm_gpiod_get_optional(dev, "enable", 0);
	if (IS_ERR(panel->enable_gpio)) {
		err = PTR_ERR(panel->enable_gpio);
		dev_err(dev, "failed to request enable GPIO: %d\n", err);
		return err;
	}

	panel->reset_gpio = devm_gpiod_get_optional(dev, "reset", 0);
	if (IS_ERR(panel->reset_gpio)) {
		err = PTR_ERR(panel->reset_gpio);
		dev_err(dev, "failed to request reset GPIO: %d\n", err);
		return err;
	}

	if (of_property_read_string(dev->of_node, "rockchip,cmd-type",
				    &cmd_type))
		panel->cmd_type = CMD_TYPE_DEFAULT;
	else
		panel->cmd_type = get_panel_cmd_type(cmd_type);

	if (panel->cmd_type == CMD_TYPE_SPI) {
		panel->spi_sdi_gpio =
				devm_gpiod_get_optional(dev, "spi-sdi", 0);
		if (IS_ERR(panel->spi_sdi_gpio)) {
			err = PTR_ERR(panel->spi_sdi_gpio);
			dev_err(dev, "failed to request spi_sdi: %d\n", err);
			return err;
		}

		panel->spi_scl_gpio =
				devm_gpiod_get_optional(dev, "spi-scl", 0);
		if (IS_ERR(panel->spi_scl_gpio)) {
			err = PTR_ERR(panel->spi_scl_gpio);
			dev_err(dev, "failed to request spi_scl: %d\n", err);
			return err;
		}

		panel->spi_cs_gpio = devm_gpiod_get_optional(dev, "spi-cs", 0);
		if (IS_ERR(panel->spi_cs_gpio)) {
			err = PTR_ERR(panel->spi_cs_gpio);
			dev_err(dev, "failed to request spi_cs: %d\n", err);
			return err;
		}
		gpiod_direction_output(panel->spi_cs_gpio, 1);
		gpiod_direction_output(panel->spi_sdi_gpio, 1);
		gpiod_direction_output(panel->spi_scl_gpio, 1);
	}
	panel->power_invert =
			of_property_read_bool(dev->of_node, "power-invert");

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!panel->backlight)
			return -EPROBE_DEFER;
	}

	ddc = of_parse_phandle(dev->of_node, "ddc-i2c-bus", 0);
	if (ddc) {
		panel->ddc = of_find_i2c_adapter_by_node(ddc);
		of_node_put(ddc);

		if (!panel->ddc) {
			err = -EPROBE_DEFER;
			goto free_backlight;
		}
	}

	drm_panel_init(&panel->base);
	panel->base.dev = dev;
	panel->base.funcs = &panel_simple_funcs;

	err = drm_panel_add(&panel->base);
	if (err < 0)
		goto free_ddc;

	dev_set_drvdata(dev, panel);

	return 0;

free_ddc:
	if (panel->ddc)
		put_device(&panel->ddc->dev);
free_backlight:
	if (panel->backlight)
		put_device(&panel->backlight->dev);

	return err;
}

static int panel_simple_remove(struct device *dev)
{
	struct panel_simple *panel = dev_get_drvdata(dev);

	drm_panel_detach(&panel->base);
	drm_panel_remove(&panel->base);

	panel_simple_disable(&panel->base);
	panel_simple_unprepare(&panel->base);

	if (panel->ddc)
		put_device(&panel->ddc->dev);

	if (panel->backlight)
		put_device(&panel->backlight->dev);

	panel_simple_cmds_cleanup(panel);

	return 0;
}

static void panel_simple_shutdown(struct device *dev)
{
	struct panel_simple *panel = dev_get_drvdata(dev);

	panel_simple_disable(&panel->base);
	panel_simple_unprepare(&panel->base);
}

static const struct drm_display_mode ampire_am800480r3tmqwa1h_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 0,
	.hsync_end = 800 + 0 + 255,
	.htotal = 800 + 0 + 255 + 0,
	.vdisplay = 480,
	.vsync_start = 480 + 2,
	.vsync_end = 480 + 2 + 45,
	.vtotal = 480 + 2 + 45 + 0,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static const struct panel_desc ampire_am800480r3tmqwa1h = {
	.modes = &ampire_am800480r3tmqwa1h_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode auo_b101aw03_mode = {
	.clock = 51450,
	.hdisplay = 1024,
	.hsync_start = 1024 + 156,
	.hsync_end = 1024 + 156 + 8,
	.htotal = 1024 + 156 + 8 + 156,
	.vdisplay = 600,
	.vsync_start = 600 + 16,
	.vsync_end = 600 + 16 + 6,
	.vtotal = 600 + 16 + 6 + 16,
	.vrefresh = 60,
};

static const struct panel_desc auo_b101aw03 = {
	.modes = &auo_b101aw03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode auo_b101ean01_mode = {
	.clock = 72500,
	.hdisplay = 1280,
	.hsync_start = 1280 + 119,
	.hsync_end = 1280 + 119 + 32,
	.htotal = 1280 + 119 + 32 + 21,
	.vdisplay = 800,
	.vsync_start = 800 + 4,
	.vsync_end = 800 + 4 + 20,
	.vtotal = 800 + 4 + 20 + 8,
	.vrefresh = 60,
};

static const struct panel_desc auo_b101ean01 = {
	.modes = &auo_b101ean01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 217,
		.height = 136,
	},
};

static const struct drm_display_mode auo_b101ew05_mode = {
	.clock = 71000,
	.hdisplay = 1280,
	.hsync_start = 1280 + 18,
	.hsync_end = 1280 + 18 + 10,
	.htotal = 1280 + 18 + 10 + 100,
	.vdisplay = 800,
	.vsync_start = 800 + 6,
	.vsync_end = 800 + 6 + 2,
	.vtotal = 800 + 6 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc auo_b101ew05 = {
	.modes = &auo_b101ew05_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 217,
		.height = 136,
	},
};

static const struct drm_display_mode auo_b101xtn01_mode = {
	.clock = 72000,
	.hdisplay = 1366,
	.hsync_start = 1366 + 20,
	.hsync_end = 1366 + 20 + 70,
	.htotal = 1366 + 20 + 70,
	.vdisplay = 768,
	.vsync_start = 768 + 14,
	.vsync_end = 768 + 14 + 42,
	.vtotal = 768 + 14 + 42,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc auo_b101xtn01 = {
	.modes = &auo_b101xtn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode auo_b116xw03_mode = {
	.clock = 70589,
	.hdisplay = 1366,
	.hsync_start = 1366 + 40,
	.hsync_end = 1366 + 40 + 40,
	.htotal = 1366 + 40 + 40 + 32,
	.vdisplay = 768,
	.vsync_start = 768 + 10,
	.vsync_end = 768 + 10 + 12,
	.vtotal = 768 + 10 + 12 + 6,
	.vrefresh = 60,
};

static const struct panel_desc auo_b116xw03 = {
	.modes = &auo_b116xw03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
};

static const struct drm_display_mode auo_b125han03_mode = {
	.clock = 146900,
	.hdisplay = 1920,
	.hsync_start = 1920 + 48,
	.hsync_end = 1920 + 48 + 32,
	.htotal = 1920 + 48 + 32 + 140,
	.vdisplay = 1080,
	.vsync_start = 1080 + 2,
	.vsync_end = 1080 + 2 + 5,
	.vtotal = 1080 + 2 + 5 + 57,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc auo_b125han03 = {
	.modes = &auo_b125han03_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 276,
		.height = 156,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode auo_b133xtn01_mode = {
	.clock = 69500,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 3,
	.vsync_end = 768 + 3 + 6,
	.vtotal = 768 + 3 + 6 + 13,
	.vrefresh = 60,
};

static const struct panel_desc auo_b133xtn01 = {
	.modes = &auo_b133xtn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
};

static const struct drm_display_mode auo_b133htn01_mode = {
	.clock = 150660,
	.hdisplay = 1920,
	.hsync_start = 1920 + 172,
	.hsync_end = 1920 + 172 + 80,
	.htotal = 1920 + 172 + 80 + 60,
	.vdisplay = 1080,
	.vsync_start = 1080 + 25,
	.vsync_end = 1080 + 25 + 10,
	.vtotal = 1080 + 25 + 10 + 10,
	.vrefresh = 60,
};

static const struct panel_desc auo_b133htn01 = {
	.modes = &auo_b133htn01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 293,
		.height = 165,
	},
	.delay = {
		.prepare = 105,
		.enable = 20,
		.unprepare = 50,
	},
};

static const struct drm_display_mode avic_tm070ddh03_mode = {
	.clock = 51200,
	.hdisplay = 1024,
	.hsync_start = 1024 + 160,
	.hsync_end = 1024 + 160 + 4,
	.htotal = 1024 + 160 + 4 + 156,
	.vdisplay = 600,
	.vsync_start = 600 + 17,
	.vsync_end = 600 + 17 + 1,
	.vtotal = 600 + 17 + 1 + 17,
	.vrefresh = 60,
};

static const struct panel_desc avic_tm070ddh03 = {
	.modes = &avic_tm070ddh03_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 154,
		.height = 90,
	},
	.delay = {
		.prepare = 20,
		.enable = 200,
		.disable = 200,
	},
};

static const struct drm_display_mode boe_mv238qum_n20_mode = {
	.clock = 559440,
	.hdisplay = 3840,
	.hsync_start = 3840 + 150,
	.hsync_end = 3840 + 150 + 60,
	.htotal = 3840 + 150 + 60 + 150,
	.vdisplay = 2160,
	.vsync_start = 2160 + 24,
	.vsync_end = 2160 + 24 + 12,
	.vtotal = 2160 + 24 + 12 + 24,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc boe_mv238qum_n20 = {
	.modes = &boe_mv238qum_n20_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 527,
		.height = 296,
	},
	.delay = {
		.prepare = 20,
		.enable = 20,
		.unprepare = 20,
		.disable = 20,
	},
};

static const struct drm_display_mode boe_mv270qum_n10_mode = {
	.clock = 533000,
	.hdisplay = 3840,
	.hsync_start = 3840 + 78,
	.hsync_end = 3840 + 78 + 28,
	.htotal = 3840 + 78 + 28 + 54,
	.vdisplay = 2160,
	.vsync_start = 2160 + 47,
	.vsync_end = 2160 + 47 + 8,
	.vtotal = 2160 + 47 + 8 + 7,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc boe_mv270qum_n10 = {
	.modes = &boe_mv270qum_n10_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 597,
		.height = 336,
	},
};

static const struct drm_display_mode boe_nv125fhm_n73_mode = {
	.clock = 72300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 80,
	.hsync_end = 1366 + 80 + 20,
	.htotal = 1366 + 80 + 20 + 60,
	.vdisplay = 768,
	.vsync_start = 768 + 12,
	.vsync_end = 768 + 12 + 2,
	.vtotal = 768 + 12 + 2 + 8,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc boe_nv125fhm_n73 = {
	.modes = &boe_nv125fhm_n73_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 276,
		.height = 156,
	},
	.delay = {
		.unprepare = 160,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode chunghwa_claa070wp03xg_mode = {
	.clock = 67000,
	.hdisplay = 800,
	.hsync_start = 800 + 24,
	.hsync_end = 800 + 24 + 16,
	.htotal = 800 + 24 + 16 + 24,
	.vdisplay = 1280,
	.vsync_start = 1280 + 2,
	.vsync_end = 1280 + 2 + 2,
	.vtotal = 1280 + 2 + 2 + 4,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa070wp03xg = {
	.modes = &chunghwa_claa070wp03xg_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 94,
		.height = 151,
	},
};

static const struct drm_display_mode chunghwa_claa101wa01a_mode = {
	.clock = 72070,
	.hdisplay = 1366,
	.hsync_start = 1366 + 58,
	.hsync_end = 1366 + 58 + 58,
	.htotal = 1366 + 58 + 58 + 58,
	.vdisplay = 768,
	.vsync_start = 768 + 4,
	.vsync_end = 768 + 4 + 4,
	.vtotal = 768 + 4 + 4 + 4,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa101wa01a = {
	.modes = &chunghwa_claa101wa01a_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 220,
		.height = 120,
	},
};

static const struct drm_display_mode chunghwa_claa101wb01_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 48,
	.hsync_end = 1366 + 48 + 32,
	.htotal = 1366 + 48 + 32 + 20,
	.vdisplay = 768,
	.vsync_start = 768 + 16,
	.vsync_end = 768 + 16 + 8,
	.vtotal = 768 + 16 + 8 + 16,
	.vrefresh = 60,
};

static const struct panel_desc chunghwa_claa101wb01 = {
	.modes = &chunghwa_claa101wb01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 223,
		.height = 125,
	},
};

static const struct drm_display_mode edt_et057090dhu_mode = {
	.clock = 25175,
	.hdisplay = 640,
	.hsync_start = 640 + 16,
	.hsync_end = 640 + 16 + 30,
	.htotal = 640 + 16 + 30 + 114,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 3,
	.vtotal = 480 + 10 + 3 + 32,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc edt_et057090dhu = {
	.modes = &edt_et057090dhu_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 115,
		.height = 86,
	},
};

static const struct drm_display_mode edt_etm0700g0dh6_mode = {
	.clock = 33260,
	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 128,
	.htotal = 800 + 40 + 128 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 2,
	.vtotal = 480 + 10 + 2 + 33,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc edt_etm0700g0dh6 = {
	.modes = &edt_etm0700g0dh6_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 152,
		.height = 91,
	},
};

static const struct drm_display_mode foxlink_fl500wvr00_a0t_mode = {
	.clock = 32260,
	.hdisplay = 800,
	.hsync_start = 800 + 168,
	.hsync_end = 800 + 168 + 64,
	.htotal = 800 + 168 + 64 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 37,
	.vsync_end = 480 + 37 + 2,
	.vtotal = 480 + 37 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc foxlink_fl500wvr00_a0t = {
	.modes = &foxlink_fl500wvr00_a0t_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 108,
		.height = 65,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode giantplus_gpg482739qs5_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 5,
	.hsync_end = 480 + 5 + 1,
	.htotal = 480 + 5 + 1 + 40,
	.vdisplay = 272,
	.vsync_start = 272 + 8,
	.vsync_end = 272 + 8 + 1,
	.vtotal = 272 + 8 + 1 + 8,
	.vrefresh = 60,
};

static const struct panel_desc giantplus_gpg482739qs5 = {
	.modes = &giantplus_gpg482739qs5_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct display_timing hannstar_hsd070pww1_timing = {
	.pixelclock = { 64300000, 71100000, 82000000 },
	.hactive = { 1280, 1280, 1280 },
	.hfront_porch = { 1, 1, 10 },
	.hback_porch = { 1, 1, 10 },
	/*
	 * According to the data sheet, the minimum horizontal blanking interval
	 * is 54 clocks (1 + 52 + 1), but tests with a Nitrogen6X have shown the
	 * minimum working horizontal blanking interval to be 60 clocks.
	 */
	.hsync_len = { 58, 158, 661 },
	.vactive = { 800, 800, 800 },
	.vfront_porch = { 1, 1, 10 },
	.vback_porch = { 1, 1, 10 },
	.vsync_len = { 1, 21, 203 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc hannstar_hsd070pww1 = {
	.timings = &hannstar_hsd070pww1_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 151,
		.height = 94,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
};

static const struct display_timing hannstar_hsd100pxn1_timing = {
	.pixelclock = { 55000000, 65000000, 75000000 },
	.hactive = { 1024, 1024, 1024 },
	.hfront_porch = { 40, 40, 40 },
	.hback_porch = { 220, 220, 220 },
	.hsync_len = { 20, 60, 100 },
	.vactive = { 768, 768, 768 },
	.vfront_porch = { 7, 7, 7 },
	.vback_porch = { 21, 21, 21 },
	.vsync_len = { 10, 10, 10 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc hannstar_hsd100pxn1 = {
	.timings = &hannstar_hsd100pxn1_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 203,
		.height = 152,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X7X3_SPWG,
};

static const struct drm_display_mode hitachi_tx23d38vm0caa_mode = {
	.clock = 33333,
	.hdisplay = 800,
	.hsync_start = 800 + 85,
	.hsync_end = 800 + 85 + 86,
	.htotal = 800 + 85 + 86 + 85,
	.vdisplay = 480,
	.vsync_start = 480 + 16,
	.vsync_end = 480 + 16 + 13,
	.vtotal = 480 + 16 + 13 + 16,
	.vrefresh = 60,
};

static const struct panel_desc hitachi_tx23d38vm0caa = {
	.modes = &hitachi_tx23d38vm0caa_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 195,
		.height = 117,
	},
};

static const struct drm_display_mode innolux_at043tn24_mode = {
	.clock = 9000,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 11,
	.vtotal = 272 + 2 + 11 + 2,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_at043tn24 = {
	.modes = &innolux_at043tn24_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode innolux_g121i1_l01_mode = {
	.clock = 71000,
	.hdisplay = 1280,
	.hsync_start = 1280 + 64,
	.hsync_end = 1280 + 64 + 32,
	.htotal = 1280 + 64 + 32 + 64,
	.vdisplay = 800,
	.vsync_start = 800 + 9,
	.vsync_end = 800 + 9 + 6,
	.vtotal = 800 + 9 + 6 + 9,
	.vrefresh = 60,
};

static const struct panel_desc innolux_g121i1_l01 = {
	.modes = &innolux_g121i1_l01_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 261,
		.height = 163,
	},
};

static const struct drm_display_mode innolux_n116bge_mode = {
	.clock = 76420,
	.hdisplay = 1366,
	.hsync_start = 1366 + 136,
	.hsync_end = 1366 + 136 + 30,
	.htotal = 1366 + 136 + 30 + 60,
	.vdisplay = 768,
	.vsync_start = 768 + 8,
	.vsync_end = 768 + 8 + 12,
	.vtotal = 768 + 8 + 12 + 12,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_n116bge = {
	.modes = &innolux_n116bge_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 256,
		.height = 144,
	},
};

static const struct drm_display_mode innolux_n125hce_mode = {
	.clock = 138780,
	.hdisplay = 1920,
	.hsync_start = 1920 + 80,
	.hsync_end = 1920 + 80 + 30,
	.htotal = 1920 + 80 + 30 + 50,
	.vdisplay = 1080,
	.vsync_start = 1080 + 12,
	.vsync_end = 1080 + 12 + 4,
	.vtotal = 1080 + 12 + 4 + 16,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct panel_desc innolux_n125hce = {
	.modes = &innolux_n125hce_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 283,
		.height = 168,
	},
	.delay = {
		.unprepare = 600,
		.enable = 100,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode innolux_n156bge_l21_mode = {
	.clock = 69300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 16,
	.hsync_end = 1366 + 16 + 34,
	.htotal = 1366 + 16 + 34 + 50,
	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 2 + 6,
	.vtotal = 768 + 2 + 6 + 12,
	.vrefresh = 60,
};

static const struct panel_desc innolux_n156bge_l21 = {
	.modes = &innolux_n156bge_l21_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 344,
		.height = 193,
	},
};

static const struct drm_display_mode innolux_zj070na_01p_mode = {
	.clock = 51501,
	.hdisplay = 1024,
	.hsync_start = 1024 + 128,
	.hsync_end = 1024 + 128 + 64,
	.htotal = 1024 + 128 + 64 + 128,
	.vdisplay = 600,
	.vsync_start = 600 + 16,
	.vsync_end = 600 + 16 + 4,
	.vtotal = 600 + 16 + 4 + 16,
	.vrefresh = 60,
};

static const struct panel_desc innolux_zj070na_01p = {
	.modes = &innolux_zj070na_01p_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 1024,
		.height = 600,
	},
};

static const struct drm_display_mode lg_lb070wv8_mode = {
	.clock = 33246,
	.hdisplay = 800,
	.hsync_start = 800 + 88,
	.hsync_end = 800 + 88 + 80,
	.htotal = 800 + 88 + 80 + 88,
	.vdisplay = 480,
	.vsync_start = 480 + 10,
	.vsync_end = 480 + 10 + 25,
	.vtotal = 480 + 10 + 25 + 10,
	.vrefresh = 60,
};

static const struct panel_desc lg_lb070wv8 = {
	.modes = &lg_lb070wv8_mode,
	.num_modes = 1,
	.bpc = 16,
	.size = {
		.width = 151,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG,
};

static const struct drm_display_mode sharp_lcd_f402_mode = {
	.clock = 205000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 12,
	.hsync_end = 1536 + 12 + 48,
	.htotal = 1536 + 12 + 48 + 16,
	.vdisplay = 2048,
	.vsync_start = 2048 + 8,
	.vsync_end = 2048 + 8 + 8,
	.vtotal = 2048 + 8 + 8 + 4,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc sharp_lcd_f402 = {
	.modes = &sharp_lcd_f402_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode lg_lm238wr2_spa1_mode = {
	.clock = 533250,
	.hdisplay = 3840,
	.hsync_start = 3840 + 48,
	.hsync_end = 3840 + 48 + 32,
	.htotal = 3840 + 48 + 32 + 80,
	.vdisplay = 2160,
	.vsync_start = 2160 + 3,
	.vsync_end = 2160 + 3 + 5,
	.vtotal = 2160 + 3 + 5 + 54,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc lg_lm238wr2_spa1 = {
	.modes = &lg_lm238wr2_spa1_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 527,
		.height = 297,
	},
	.delay = {
		.prepare = 20,
		.enable = 20,
		.unprepare = 20,
		.disable = 20,
	},
};

static const struct drm_display_mode lg_lm270wr3_ssa1_mode = {
	.clock = 533250,
	.hdisplay = 3840,
	.hsync_start = 3840 + 48,
	.hsync_end = 3840 + 48 + 32,
	.htotal = 3840 + 48 + 32 + 80,
	.vdisplay = 2160,
	.vsync_start = 2160 + 3,
	.vsync_end = 2160 + 3 + 5,
	.vtotal = 2160 + 3 + 5 + 54,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc lg_lm270wr3_ssa1 = {
	.modes = &lg_lm270wr3_ssa1_mode,
	.num_modes = 1,
	.bpc = 10,
	.size = {
		.width = 598,
		.height = 336,
	},
	.delay = {
		.prepare = 20,
		.enable = 20,
		.unprepare = 20,
		.disable = 20,
	},
};

static const struct drm_display_mode lg_lp079qx1_sp0v_mode = {
	.clock = 200000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 12,
	.hsync_end = 1536 + 12 + 16,
	.htotal = 1536 + 12 + 16 + 48,
	.vdisplay = 2048,
	.vsync_start = 2048 + 8,
	.vsync_end = 2048 + 8 + 4,
	.vtotal = 2048 + 8 + 4 + 8,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc lg_lp079qx1_sp0v = {
	.modes = &lg_lp079qx1_sp0v_mode,
	.num_modes = 1,
	.size = {
		.width = 129,
		.height = 171,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode lg_lp097qx1_spa1_mode = {
	.clock = 205210,
	.hdisplay = 2048,
	.hsync_start = 2048 + 150,
	.hsync_end = 2048 + 150 + 5,
	.htotal = 2048 + 150 + 5 + 5,
	.vdisplay = 1536,
	.vsync_start = 1536 + 3,
	.vsync_end = 1536 + 3 + 1,
	.vtotal = 1536 + 3 + 1 + 9,
	.vrefresh = 60,
};

static const struct panel_desc lg_lp097qx1_spa1 = {
	.modes = &lg_lp097qx1_spa1_mode,
	.num_modes = 1,
	.size = {
		.width = 320,
		.height = 187,
	},
};

static const struct drm_display_mode lg_lp129qe_mode = {
	.clock = 285250,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1700,
	.vsync_start = 1700 + 3,
	.vsync_end = 1700 + 3 + 10,
	.vtotal = 1700 + 3 + 10 + 36,
	.vrefresh = 60,
};

static const struct panel_desc lg_lp129qe = {
	.modes = &lg_lp129qe_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 272,
		.height = 181,
	},
};

static const struct drm_display_mode nec_nl4827hc19_05b_mode = {
	.clock = 10870,
	.hdisplay = 480,
	.hsync_start = 480 + 2,
	.hsync_end = 480 + 2 + 41,
	.htotal = 480 + 2 + 41 + 2,
	.vdisplay = 272,
	.vsync_start = 272 + 2,
	.vsync_end = 272 + 2 + 4,
	.vtotal = 272 + 2 + 4 + 2,
	.vrefresh = 74,
};

static const struct panel_desc nec_nl4827hc19_05b = {
	.modes = &nec_nl4827hc19_05b_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 95,
		.height = 54,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24
};

static const struct display_timing okaya_rs800480t_7x0gp_timing = {
	.pixelclock = { 30000000, 30000000, 40000000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = { 40, 40, 40 },
	.hback_porch = { 40, 40, 40 },
	.hsync_len = { 1, 48, 48 },
	.vactive = { 480, 480, 480 },
	.vfront_porch = { 13, 13, 13 },
	.vback_porch = { 29, 29, 29 },
	.vsync_len = { 3, 3, 3 },
	.flags = DISPLAY_FLAGS_DE_HIGH,
};

static const struct panel_desc okaya_rs800480t_7x0gp = {
	.timings = &okaya_rs800480t_7x0gp_timing,
	.num_timings = 1,
	.bpc = 6,
	.size = {
		.width = 154,
		.height = 87,
	},
	.delay = {
		.prepare = 41,
		.enable = 50,
		.unprepare = 41,
		.disable = 50,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct drm_display_mode ortustech_com43h4m85ulc_mode  = {
	.clock = 25000,
	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 10,
	.htotal = 480 + 10 + 10 + 15,
	.vdisplay = 800,
	.vsync_start = 800 + 3,
	.vsync_end = 800 + 3 + 3,
	.vtotal = 800 + 3 + 3 + 3,
	.vrefresh = 60,
};

static const struct panel_desc ortustech_com43h4m85ulc = {
	.modes = &ortustech_com43h4m85ulc_mode,
	.num_modes = 1,
	.bpc = 8,
	.size = {
		.width = 56,
		.height = 93,
	},
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
};

static const struct drm_display_mode samsung_lsn122dl01_c01_mode = {
	.clock = 271560,
	.hdisplay = 2560,
	.hsync_start = 2560 + 48,
	.hsync_end = 2560 + 48 + 32,
	.htotal = 2560 + 48 + 32 + 80,
	.vdisplay = 1600,
	.vsync_start = 1600 + 2,
	.vsync_end = 1600 + 2 + 5,
	.vtotal = 1600 + 2 + 5 + 57,
	.vrefresh = 60,
};

static const struct panel_desc samsung_lsn122dl01_c01 = {
	.modes = &samsung_lsn122dl01_c01_mode,
	.num_modes = 1,
	.size = {
		.width = 2560,
		.height = 1600,
	},
};

static const struct drm_display_mode samsung_ltn101nt05_mode = {
	.clock = 54030,
	.hdisplay = 1024,
	.hsync_start = 1024 + 24,
	.hsync_end = 1024 + 24 + 136,
	.htotal = 1024 + 24 + 136 + 160,
	.vdisplay = 600,
	.vsync_start = 600 + 3,
	.vsync_end = 600 + 3 + 6,
	.vtotal = 600 + 3 + 6 + 61,
	.vrefresh = 60,
};

static const struct panel_desc samsung_ltn101nt05 = {
	.modes = &samsung_ltn101nt05_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 1024,
		.height = 600,
	},
};

static const struct drm_display_mode samsung_ltn140at29_301_mode = {
	.clock = 76300,
	.hdisplay = 1366,
	.hsync_start = 1366 + 64,
	.hsync_end = 1366 + 64 + 48,
	.htotal = 1366 + 64 + 48 + 128,
	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 2 + 5,
	.vtotal = 768 + 2 + 5 + 17,
	.vrefresh = 60,
};

static const struct panel_desc samsung_ltn140at29_301 = {
	.modes = &samsung_ltn140at29_301_mode,
	.num_modes = 1,
	.bpc = 6,
	.size = {
		.width = 320,
		.height = 187,
	},
};

static const struct drm_display_mode shelly_sca07010_bfn_lnn_mode = {
	.clock = 33300,
	.hdisplay = 800,
	.hsync_start = 800 + 1,
	.hsync_end = 800 + 1 + 64,
	.htotal = 800 + 1 + 64 + 64,
	.vdisplay = 480,
	.vsync_start = 480 + 1,
	.vsync_end = 480 + 1 + 23,
	.vtotal = 480 + 1 + 23 + 22,
	.vrefresh = 60,
};

static const struct panel_desc shelly_sca07010_bfn_lnn = {
	.modes = &shelly_sca07010_bfn_lnn_mode,
	.num_modes = 1,
	.size = {
		.width = 152,
		.height = 91,
	},
	.bus_format = MEDIA_BUS_FMT_RGB666_1X18,
};

static const struct of_device_id platform_of_match[] = {
	{
		.compatible = "simple-panel",
		.data = NULL,
	}, {
		.compatible = "ampire,am800480r3tmqwa1h",
		.data = &ampire_am800480r3tmqwa1h,
	}, {
		.compatible = "auo,b101aw03",
		.data = &auo_b101aw03,
	}, {
		.compatible = "auo,b101ean01",
		.data = &auo_b101ean01,
	}, {
		.compatible = "auo,b101ew05",
		.data = &auo_b101ew05,
	}, {
		.compatible = "auo,b101xtn01",
		.data = &auo_b101xtn01,
	}, {
		.compatible = "auo,b116xw03",
		.data = &auo_b116xw03,
	}, {
		.compatible = "auo,b125han03",
		.data = &auo_b125han03,
	}, {
		.compatible = "auo,b133htn01",
		.data = &auo_b133htn01,
	}, {
		.compatible = "auo,b133xtn01",
		.data = &auo_b133xtn01,
	}, {
		.compatible = "avic,tm070ddh03",
		.data = &avic_tm070ddh03,
	}, {
		.compatible = "boe,mv238qum-n20",
		.data = &boe_mv238qum_n20,
	}, {
		.compatible = "boe,mv270qum-n10",
		.data = &boe_mv270qum_n10,
	}, {
		.compatible = "boe,nv125fhm-n73",
		.data = &boe_nv125fhm_n73,
	}, {
		.compatible = "chunghwa,claa070wp03xg",
		.data = &chunghwa_claa070wp03xg,
	}, {
		.compatible = "chunghwa,claa101wa01a",
		.data = &chunghwa_claa101wa01a
	}, {
		.compatible = "chunghwa,claa101wb01",
		.data = &chunghwa_claa101wb01
	}, {
		.compatible = "edt,et057090dhu",
		.data = &edt_et057090dhu,
	}, {
		.compatible = "edt,et070080dh6",
		.data = &edt_etm0700g0dh6,
	}, {
		.compatible = "edt,etm0700g0dh6",
		.data = &edt_etm0700g0dh6,
	}, {
		.compatible = "foxlink,fl500wvr00-a0t",
		.data = &foxlink_fl500wvr00_a0t,
	}, {
		.compatible = "giantplus,gpg482739qs5",
		.data = &giantplus_gpg482739qs5
	}, {
		.compatible = "hannstar,hsd070pww1",
		.data = &hannstar_hsd070pww1,
	}, {
		.compatible = "hannstar,hsd100pxn1",
		.data = &hannstar_hsd100pxn1,
	}, {
		.compatible = "hit,tx23d38vm0caa",
		.data = &hitachi_tx23d38vm0caa
	}, {
		.compatible = "innolux,at043tn24",
		.data = &innolux_at043tn24,
	}, {
		.compatible ="innolux,g121i1-l01",
		.data = &innolux_g121i1_l01
	}, {
		.compatible = "innolux,n116bge",
		.data = &innolux_n116bge,
	}, {
		.compatible = "innolux,n125hce",
		.data = &innolux_n125hce,
	}, {
		.compatible = "innolux,n156bge-l21",
		.data = &innolux_n156bge_l21,
	}, {
		.compatible = "innolux,zj070na-01p",
		.data = &innolux_zj070na_01p,
	}, {
		.compatible = "lg,lb070wv8",
		.data = &lg_lb070wv8,
	}, {
		.compatible = "lg,lm238wr2-spa1",
		.data = &lg_lm238wr2_spa1,
	}, {
		.compatible = "lg,lm270wr3-ssa1",
		.data = &lg_lm270wr3_ssa1,
	}, {
		.compatible = "lg,lp079qx1-sp0v",
		.data = &lg_lp079qx1_sp0v,
	}, {
		.compatible = "lg,lp097qx1-spa1",
		.data = &lg_lp097qx1_spa1,
	}, {
		.compatible = "lg,lp129qe",
		.data = &lg_lp129qe,
	}, {
		.compatible = "nec,nl4827hc19-05b",
		.data = &nec_nl4827hc19_05b,
	}, {
		.compatible = "okaya,rs800480t-7x0gp",
		.data = &okaya_rs800480t_7x0gp,
	}, {
		.compatible = "ortustech,com43h4m85ulc",
		.data = &ortustech_com43h4m85ulc,
	}, {
		.compatible = "samsung,lsn122dl01-c01",
		.data = &samsung_lsn122dl01_c01,
	}, {
		.compatible = "samsung,ltn101nt05",
		.data = &samsung_ltn101nt05,
	}, {
		.compatible = "samsung,ltn140at29-301",
		.data = &samsung_ltn140at29_301,
	}, {
		.compatible = "sharp,lcd-f402",
		.data = &sharp_lcd_f402,
	}, {
		.compatible = "shelly,sca07010-bfn-lnn",
		.data = &shelly_sca07010_bfn_lnn,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, platform_of_match);

static int panel_simple_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	id = of_match_node(platform_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return panel_simple_probe(&pdev->dev, id->data);
}

static int panel_simple_platform_remove(struct platform_device *pdev)
{
	return panel_simple_remove(&pdev->dev);
}

static void panel_simple_platform_shutdown(struct platform_device *pdev)
{
	panel_simple_shutdown(&pdev->dev);
}

static struct platform_driver panel_simple_platform_driver = {
	.driver = {
		.name = "panel-simple",
		.of_match_table = platform_of_match,
	},
	.probe = panel_simple_platform_probe,
	.remove = panel_simple_platform_remove,
	.shutdown = panel_simple_platform_shutdown,
};

struct panel_desc_dsi {
	struct panel_desc desc;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};

static const struct drm_display_mode auo_b080uan01_mode = {
	.clock = 154500,
	.hdisplay = 1200,
	.hsync_start = 1200 + 62,
	.hsync_end = 1200 + 62 + 4,
	.htotal = 1200 + 62 + 4 + 62,
	.vdisplay = 1920,
	.vsync_start = 1920 + 9,
	.vsync_end = 1920 + 9 + 2,
	.vtotal = 1920 + 9 + 2 + 8,
	.vrefresh = 60,
};

static const struct panel_desc_dsi auo_b080uan01 = {
	.desc = {
		.modes = &auo_b080uan01_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 108,
			.height = 272,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode boe_tv080wum_nl0_mode = {
	.clock = 160000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 120,
	.hsync_end = 1200 + 120 + 20,
	.htotal = 1200 + 120 + 20 + 21,
	.vdisplay = 1920,
	.vsync_start = 1920 + 21,
	.vsync_end = 1920 + 21 + 3,
	.vtotal = 1920 + 21 + 3 + 18,
	.vrefresh = 60,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static const struct panel_desc_dsi boe_tv080wum_nl0 = {
	.desc = {
		.modes = &boe_tv080wum_nl0_mode,
		.num_modes = 1,
		.size = {
			.width = 107,
			.height = 172,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO |
		 MIPI_DSI_MODE_VIDEO_BURST |
		 MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode lg_ld070wx3_sl01_mode = {
	.clock = 71000,
	.hdisplay = 800,
	.hsync_start = 800 + 32,
	.hsync_end = 800 + 32 + 1,
	.htotal = 800 + 32 + 1 + 57,
	.vdisplay = 1280,
	.vsync_start = 1280 + 28,
	.vsync_end = 1280 + 28 + 1,
	.vtotal = 1280 + 28 + 1 + 14,
	.vrefresh = 60,
};

static const struct panel_desc_dsi lg_ld070wx3_sl01 = {
	.desc = {
		.modes = &lg_ld070wx3_sl01_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 94,
			.height = 151,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode lg_lh500wx1_sd03_mode = {
	.clock = 67000,
	.hdisplay = 720,
	.hsync_start = 720 + 12,
	.hsync_end = 720 + 12 + 4,
	.htotal = 720 + 12 + 4 + 112,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8 + 4,
	.vtotal = 1280 + 8 + 4 + 12,
	.vrefresh = 60,
};

static const struct panel_desc_dsi lg_lh500wx1_sd03 = {
	.desc = {
		.modes = &lg_lh500wx1_sd03_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 62,
			.height = 110,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};

static const struct drm_display_mode panasonic_vvx10f004b00_mode = {
	.clock = 157200,
	.hdisplay = 1920,
	.hsync_start = 1920 + 154,
	.hsync_end = 1920 + 154 + 16,
	.htotal = 1920 + 154 + 16 + 32,
	.vdisplay = 1200,
	.vsync_start = 1200 + 17,
	.vsync_end = 1200 + 17 + 2,
	.vtotal = 1200 + 17 + 2 + 16,
	.vrefresh = 60,
};

static const struct panel_desc_dsi panasonic_vvx10f004b00 = {
	.desc = {
		.modes = &panasonic_vvx10f004b00_mode,
		.num_modes = 1,
		.bpc = 8,
		.size = {
			.width = 217,
			.height = 136,
		},
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
};


static const struct of_device_id dsi_of_match[] = {
	{
		.compatible = "simple-panel-dsi",
		.data = NULL
	}, {
		.compatible = "auo,b080uan01",
		.data = &auo_b080uan01
	}, {
		.compatible = "boe,tv080wum-nl0",
		.data = &boe_tv080wum_nl0
	}, {
		.compatible = "lg,ld070wx3-sl01",
		.data = &lg_ld070wx3_sl01
	}, {
		.compatible = "lg,lh500wx1-sd03",
		.data = &lg_lh500wx1_sd03
	}, {
		.compatible = "panasonic,vvx10f004b00",
		.data = &panasonic_vvx10f004b00
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, dsi_of_match);

static int panel_simple_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct panel_simple *panel;
	const struct panel_desc_dsi *desc;
	const struct of_device_id *id;
	const struct panel_desc *pdesc;
	int err;
	u32 val;

	id = of_match_node(dsi_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	desc = id->data;

	if (desc) {
		dsi->mode_flags = desc->flags;
		dsi->format = desc->format;
		dsi->lanes = desc->lanes;
		pdesc = &desc->desc;
	} else {
		pdesc = NULL;
	}

	err = panel_simple_probe(&dsi->dev, pdesc);
	if (err < 0)
		return err;

	panel = dev_get_drvdata(&dsi->dev);
	panel->dsi = dsi;

	if (!of_property_read_u32(dsi->dev.of_node, "dsi,flags", &val))
		dsi->mode_flags = val;

	if (!of_property_read_u32(dsi->dev.of_node, "dsi,format", &val))
		dsi->format = val;

	if (!of_property_read_u32(dsi->dev.of_node, "dsi,lanes", &val))
		dsi->lanes = val;

	return mipi_dsi_attach(dsi);
}

static int panel_simple_dsi_remove(struct mipi_dsi_device *dsi)
{
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	return panel_simple_remove(&dsi->dev);
}

static void panel_simple_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	panel_simple_shutdown(&dsi->dev);
}

static struct mipi_dsi_driver panel_simple_dsi_driver = {
	.driver = {
		.name = "panel-simple-dsi",
		.of_match_table = dsi_of_match,
	},
	.probe = panel_simple_dsi_probe,
	.remove = panel_simple_dsi_remove,
	.shutdown = panel_simple_dsi_shutdown,
};

static int __init panel_simple_init(void)
{
	int err;

	err = platform_driver_register(&panel_simple_platform_driver);
	if (err < 0)
		return err;

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI)) {
		err = mipi_dsi_driver_register(&panel_simple_dsi_driver);
		if (err < 0)
			return err;
	}

	return 0;
}
module_init(panel_simple_init);

static void __exit panel_simple_exit(void)
{
	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&panel_simple_dsi_driver);

	platform_driver_unregister(&panel_simple_platform_driver);
}
module_exit(panel_simple_exit);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("DRM Driver for Simple Panels");
MODULE_LICENSE("GPL and additional rights");
