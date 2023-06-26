// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include "rk628.h"
#include <linux/gpio/consumer.h>
#include <linux/backlight.h>

#include "panel.h"

static int
dsi_panel_parse_cmds(const u8 *data, int blen, struct panel_cmds *pcmds)
{
	unsigned int len;
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
			pr_err("%s: error, len=%d", __func__, dchdr->dlen);
			return -EINVAL;
		}

		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		pr_err("%s: dcs_cmd=%x len=%d error!", __func__, buf[0], blen);
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

static int dsi_panel_get_cmds(struct rk628 *rk628, struct device_node *dsi_np)
{
	struct device_node *np;
	const void *data;
	int len;
	int ret, err;

	np = of_find_node_by_name(dsi_np, "rk628-panel");
	if (!np)
		return -EINVAL;

	data = of_get_property(np, "panel-init-sequence", &len);
	if (data) {
		rk628->panel->on_cmds = kcalloc(1, sizeof(struct panel_cmds), GFP_KERNEL);
		if (!rk628->panel->on_cmds)
			return -ENOMEM;

		err = dsi_panel_parse_cmds(data, len, rk628->panel->on_cmds);
		if (err) {
			dev_err(rk628->dev, "failed to parse dsi panel init sequence\n");
			ret = err;
			goto init_err;
		}
	}

	data = of_get_property(np, "panel-exit-sequence", &len);
	if (data) {
		rk628->panel->off_cmds = kcalloc(1, sizeof(struct panel_cmds), GFP_KERNEL);
		if (!rk628->panel->off_cmds) {
			ret = -ENOMEM;
			goto on_err;
		}

		err = dsi_panel_parse_cmds(data, len, rk628->panel->off_cmds);
		if (err) {
			dev_err(rk628->dev, "failed to parse dsi panel exit sequence\n");
			ret = err;
			goto exit_err;
		}
	}

	return 0;

exit_err:
	kfree(rk628->panel->off_cmds);
on_err:
	kfree(rk628->panel->on_cmds->cmds);
	kfree(rk628->panel->on_cmds->buf);
init_err:
	kfree(rk628->panel->on_cmds);

	return ret;
}

int rk628_panel_info_get(struct rk628 *rk628, struct device_node *np)
{
	struct panel_simple *panel;
	struct device *dev = rk628->dev;
	struct device_node *backlight;
	int ret;

	panel = devm_kzalloc(dev, sizeof(struct panel_simple), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply)) {
		ret = PTR_ERR(panel->supply);
		dev_err(dev, "failed to get power regulator: %d\n", ret);
		return ret;
	}

	panel->enable_gpio = devm_gpiod_get_optional(dev, "panel-enable", GPIOD_OUT_LOW);
	if (IS_ERR(panel->enable_gpio)) {
		ret = PTR_ERR(panel->enable_gpio);
		dev_err(dev, "failed to request panel enable GPIO: %d\n", ret);
		return ret;
	}

	panel->reset_gpio = devm_gpiod_get_optional(dev, "panel-reset", GPIOD_OUT_LOW);
	if (IS_ERR(panel->reset_gpio)) {
		ret = PTR_ERR(panel->reset_gpio);
		dev_err(dev, "failed to request panel reset GPIO: %d\n", ret);
		return ret;
	}

	backlight = of_parse_phandle(dev->of_node, "panel-backlight", 0);
	if (backlight) {
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!panel->backlight) {
			dev_err(dev, "failed to find backlight\n");
			return -EPROBE_DEFER;
		}

	}

	rk628->panel = panel;

	if (rk628->output_mode == OUTPUT_MODE_DSI) {
		ret = dsi_panel_get_cmds(rk628, np);
		if (ret) {
			dev_err(dev, "failed to get cmds\n");
			return ret;
		}
	}

	return 0;
}

void rk628_panel_prepare(struct rk628 *rk628)
{
	int ret;

	if (rk628->panel->supply) {
		ret = regulator_enable(rk628->panel->supply);
		if (ret)
			dev_info(rk628->dev, "failed to enable panel power supply\n");
	}

	if (rk628->panel->enable_gpio) {
		gpiod_set_value(rk628->panel->enable_gpio, 0);
		mdelay(120);
		gpiod_set_value(rk628->panel->enable_gpio, 1);
		mdelay(120);
	}

	if (rk628->panel->reset_gpio) {
		gpiod_set_value(rk628->panel->reset_gpio, 0);
		mdelay(120);
		gpiod_set_value(rk628->panel->reset_gpio, 1);
		mdelay(120);
		gpiod_set_value(rk628->panel->reset_gpio, 0);
		mdelay(120);
	}
}

void rk628_panel_enable(struct rk628 *rk628)
{
	if (rk628->panel->backlight)
		backlight_enable(rk628->panel->backlight);
}

void rk628_panel_unprepare(struct rk628 *rk628)
{

	if (rk628->panel->reset_gpio) {
		gpiod_set_value(rk628->panel->reset_gpio, 1);
		mdelay(120);
	}

	if (rk628->panel->enable_gpio) {
		gpiod_set_value(rk628->panel->enable_gpio, 0);
		mdelay(120);
	}

	if (rk628->panel->supply)
		regulator_disable(rk628->panel->supply);
}

void rk628_panel_disable(struct rk628 *rk628)
{
	if (rk628->panel->backlight)
		backlight_disable(rk628->panel->backlight);

}
