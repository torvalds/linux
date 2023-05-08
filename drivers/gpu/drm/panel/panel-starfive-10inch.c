// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 *
 * Author:
 * - keith <keith.zhao@starfivetech.com>
 */
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
 
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
 
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <video/display_timing.h>
#include <video/videomode.h>
 
#define DSI_DRIVER_NAME "starfive-raxda"
#define GPIO_PANEL
enum cmd_type {
	 CMD_TYPE_DCS,
	 CMD_TYPE_DELAY,
};

struct starfive_init_cmd {
	 enum cmd_type type;
	 const char *data;
	 size_t len;
};

#define _INIT_CMD_DCS(...)					\
	 {							 \
		 .type	 = CMD_TYPE_DCS,			 \
		 .data	 = (char[]){__VA_ARGS__},		 \
		 .len	 = sizeof((char[]){__VA_ARGS__})	 \
	 }							 \

#define _INIT_CMD_DELAY(...)					\
	 {							 \
		 .type	 = CMD_TYPE_DELAY,			 \
		 .data	 = (char[]){__VA_ARGS__},		 \
		 .len	 = sizeof((char[]){__VA_ARGS__})	 \
	 }							 \

struct starfive_panel_desc {
	 const struct drm_display_mode mode;
	 unsigned int lanes;
	 enum mipi_dsi_pixel_format format;
	 const struct starfive_init_cmd *init_cmds;
	 u32 num_init_cmds;
	 const struct display_timing *timings;
	 unsigned int num_timings;
};
 
struct starfive {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct starfive_panel_desc *desc;
	struct i2c_client *client;

	struct device	 *dev;

	struct regulator *vdd;
	struct regulator *vccio;

	struct gpio_desc *reset;
	struct gpio_desc *enable;


	bool enable_initialized;
};
 
static inline struct starfive *panel_to_starfive(struct drm_panel *panel)
{
	 return container_of(panel, struct starfive, panel);
}

static int starfive_enable(struct drm_panel *panel)
{
	struct starfive *starfive = panel_to_starfive(panel);

	if (starfive->enable_initialized == true)
		return 0;

	starfive->enable_initialized = true ;

    
	return 0;
}

static int starfive_disable(struct drm_panel *panel)
{
	 struct starfive *starfive = panel_to_starfive(panel);

	 starfive->enable_initialized = false;

	 return 0;
}

static int starfive_prepare(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct starfive *starfive = panel_to_starfive(panel);
	struct mipi_dsi_device *dsi = starfive->dsi;
	unsigned int i;
	int err;

	if (starfive->enable_initialized == true)
		return 0;


	gpiod_direction_output(starfive->enable, 0);
	gpiod_set_value(starfive->enable, 1);
	mdelay(100);

	gpiod_direction_output(starfive->reset, 0);
	mdelay(100);
	gpiod_set_value(starfive->reset, 1);
	mdelay(100);
	gpiod_set_value(starfive->reset, 0);
	mdelay(100);
	gpiod_set_value(starfive->reset, 1);
	mdelay(150);

	 return 0;
}

static int starfive_unprepare(struct drm_panel *panel)
{
	
	struct starfive *starfive = panel_to_starfive(panel);
	gpiod_set_value(starfive->reset, 1);
	msleep(120);
	 return 0;
}

static int starfive_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	 struct starfive *starfive = panel_to_starfive(panel);
	 const struct drm_display_mode *desc_mode = &starfive->desc->mode;
	 struct drm_display_mode *mode;

	 mode = drm_mode_duplicate(connector->dev, desc_mode);
	 if (!mode) {
		 DRM_DEV_ERROR(&starfive->dsi->dev, "failed to add mode %ux%ux@%u\n",
				   desc_mode->hdisplay, desc_mode->vdisplay,
				   drm_mode_vrefresh(desc_mode));
		 return -ENOMEM;
	 }

	 drm_mode_set_name(mode);
	 drm_mode_probed_add(connector, mode);

	 connector->display_info.width_mm = mode->width_mm;
	 connector->display_info.height_mm = mode->height_mm;

	 return 1;
}

static int seiko_panel_get_timings(struct drm_panel *panel,
					 unsigned int num_timings,
					 struct display_timing *timings)
{
	 struct starfive *starfive = panel_to_starfive(panel);
	 unsigned int i;

	 if (starfive->desc->num_timings < num_timings)
		 num_timings = starfive->desc->num_timings;

	 if (timings)
		 for (i = 0; i < num_timings; i++)
			 timings[i] = starfive->desc->timings[i];

	 return starfive->desc->num_timings;
}

static const struct drm_panel_funcs starfive_funcs = {
	 .disable = starfive_disable,
	 .unprepare = starfive_unprepare,
	 .prepare = starfive_prepare,
	 .enable = starfive_enable,
	 .get_modes = starfive_get_modes,
	 .get_timings = seiko_panel_get_timings,
};

static const struct display_timing starfive_timing = {
	 .pixelclock = { 148500000, 148500000, 148500000 },
	 .hactive = { 1200, 1200, 1200 },
	 .hfront_porch = {	246, 246, 246 },
	 .hback_porch = { 5, 5, 5 },
	 .hsync_len = { 5, 5, 5 },
	 .vactive = { 1920, 1920, 1920 },
	 .vfront_porch = { 84, 84, 84 },
	 .vback_porch = { 20, 20, 20 },
	 .vsync_len = { 16, 16, 16 },
	 .flags = DISPLAY_FLAGS_DE_LOW,
};

static const struct starfive_panel_desc cz101b4001_desc = {
	 .mode = {
		 .clock 	 = 148500,

		 .hdisplay	 = 1200,
		 .hsync_start	 = 1200 + 246,
		 .hsync_end  = 1200 + 246 + 5,
		 .htotal	 = 1200 + 246 + 5 + 5,

		 .vdisplay	 = 1920,
		 .vsync_start	 = 1920 + 84,
		 .vsync_end  = 1920 + 84 + 20,
		 .vtotal	 = 1920 + 84+ 20 + 16,

		 .width_mm	 = 62,
		 .height_mm  = 110,
		 .type		 = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	 },
	 .lanes = 4,
	 .format = MIPI_DSI_FMT_RGB888,
	 //.init_cmds = cz101b4001_init_cmds,
	 //.num_init_cmds = ARRAY_SIZE(cz101b4001_init_cmds),
	 .timings = &starfive_timing,
	 .num_timings = 1,
};

static int panel_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct starfive *jd_panel;
	const struct starfive_panel_desc *desc;
	int err; u8 mode = 1;

	struct device_node *endpoint, *dsi_host_node;
	struct mipi_dsi_host *host;
	struct device *dev = &client->dev;
	struct mipi_dsi_device_info info = {
		.type = DSI_DRIVER_NAME,
		.channel = 2, //0,
		.node = NULL,
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev,
			"I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	jd_panel = devm_kzalloc(&client->dev, sizeof(struct starfive), GFP_KERNEL);
	if (!jd_panel )
	 return -ENOMEM;
	desc = of_device_get_match_data(dev);

	jd_panel ->client = client;
	i2c_set_clientdata(client, jd_panel);

	jd_panel->enable_initialized = false;

	jd_panel->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jd_panel->reset)) {
		DRM_DEV_ERROR(dev, "failed to get our reset GPIO\n");
		return PTR_ERR(jd_panel->reset);
	}

	jd_panel->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(jd_panel->enable)) {
		DRM_DEV_ERROR(dev, "failed to get our enable GPIO\n");
		return PTR_ERR(jd_panel->enable);
	}

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
	 	return -ENODEV;

	dsi_host_node = of_graph_get_remote_port_parent(endpoint);
	if (!dsi_host_node)
	 goto error;

	host = of_find_mipi_dsi_host_by_node(dsi_host_node);
	of_node_put(dsi_host_node);
	if (!host) {
		of_node_put(endpoint);
		return -EPROBE_DEFER;
	}

	drm_panel_init(&jd_panel->panel, dev, &starfive_funcs,
			DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&jd_panel->panel);

	info.node = of_node_get(of_graph_get_remote_port(endpoint));
	if (!info.node)
		goto error;

	of_node_put(endpoint);
	jd_panel->desc = desc;

	jd_panel->dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(jd_panel->dsi)) {
		dev_err(dev, "DSI device registration failed: %ld\n",
			PTR_ERR(jd_panel->dsi));
		return PTR_ERR(jd_panel->dsi);
	}

	mipi_dsi_set_drvdata(jd_panel->dsi, jd_panel);

	gpiod_direction_output(jd_panel->enable, 0);
	gpiod_set_value(jd_panel->enable, 1);
	mdelay(100);

	gpiod_direction_output(jd_panel->reset, 0);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 1);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 0);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 1);
	mdelay(150);

	jd_panel->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	if(jd_panel->dsi)
	{
		//use this command to detect the connected status
		err = mipi_dsi_dcs_get_power_mode(jd_panel->dsi, &mode);
		dev_info(dev,"dsi command return %d, mode %d\n", err, mode);
		if(err == -EIO)
	  		dev_info(dev, "raxda 10 inch detected\n");
		else
			goto no_panel;
	}
	jd_panel->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
error:
	of_node_put(endpoint);
	return -ENODEV;
no_panel:
	drm_panel_remove(&jd_panel->panel);
	mipi_dsi_device_unregister(jd_panel->dsi);

	return -ENODEV;

}

static int panel_remove(struct i2c_client *client)
{
	 struct starfive *jd_panel = i2c_get_clientdata(client);

	 mipi_dsi_detach(jd_panel->dsi);
	 drm_panel_remove(&jd_panel->panel);
	 mipi_dsi_device_unregister(jd_panel->dsi);
	 return 0;
}

static const struct i2c_device_id panel_id[] = {
	 { "panel_10inch", 0 },
	 { }
};

static const struct of_device_id panel_dt_ids[] = {
	 { .compatible = "panel_10inch", .data = &cz101b4001_desc},
	 { /* sentinel */ }
};

static struct i2c_driver panel_driver_raxda_10inch = {
	 .driver = {
		 .owner  = THIS_MODULE,
		 .name	 = "starfive_raxda_10inch",
		 .of_match_table = panel_dt_ids,
	 },
	 .probe 	 = panel_probe,
	 .remove	 = panel_remove,
	 .id_table	 = panel_id,
};

static int starfive_dsi_probe(struct mipi_dsi_device *dsi)
{
	 int ret;

	 dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE ;
	 dsi->format = MIPI_DSI_FMT_RGB888;
	 dsi->lanes = 4;
	 dsi->channel = 2;
	 dsi->hs_rate = 980000000;

	 ret = mipi_dsi_attach(dsi);
	 if (ret < 0) {
		 return ret;
	 }

	 return 0;
}

static int starfive_dsi_remove(struct mipi_dsi_device *dsi)
{
	 struct starfive *starfive = mipi_dsi_get_drvdata(dsi);

	 mipi_dsi_detach(dsi);
	 drm_panel_remove(&starfive->panel);

	 return 0;
}

static const struct of_device_id starfive_of_match[] = {
	 { .compatible = "starfive-raxda-10inch-2", .data = &cz101b4001_desc },
	 { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, starfive_of_match);

static struct mipi_dsi_driver starfive_mipi_driver = {
	 .probe = starfive_dsi_probe,
	 .remove = starfive_dsi_remove,
	 .driver.name = DSI_DRIVER_NAME,
};

static int __init init_panel(void)
{
	 int err;

	 mipi_dsi_driver_register(&starfive_mipi_driver);
	 err = i2c_add_driver(&panel_driver_raxda_10inch);

	 return err;

}
late_initcall(init_panel);

static void __exit exit_panel(void)
{
	 i2c_del_driver(&panel_driver_raxda_10inch);
	 mipi_dsi_driver_unregister(&starfive_mipi_driver);
}
module_exit(exit_panel);


MODULE_AUTHOR("keith <keithzhao@starfivetech.com>");
MODULE_DESCRIPTION("10inch DSI panel");
MODULE_LICENSE("GPL");



