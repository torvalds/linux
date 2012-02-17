/*
 * DVI output support
 *
 * Copyright (C) 2011 Texas Instruments Inc
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <video/omapdss.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <drm/drm_edid.h>

#include <video/omap-panel-dvi.h>

static const struct omap_video_timings panel_dvi_default_timings = {
	.x_res		= 640,
	.y_res		= 480,

	.pixel_clock	= 23500,

	.hfp		= 48,
	.hsw		= 32,
	.hbp		= 80,

	.vfp		= 3,
	.vsw		= 4,
	.vbp		= 7,
};

struct panel_drv_data {
	struct omap_dss_device *dssdev;

	struct mutex lock;

	int pd_gpio;
};

static inline struct panel_dvi_platform_data
*get_pdata(const struct omap_dss_device *dssdev)
{
	return dssdev->data;
}

static int panel_dvi_power_on(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	int r;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	if (gpio_is_valid(ddata->pd_gpio))
		gpio_set_value(ddata->pd_gpio, 1);

	return 0;
err0:
	return r;
}

static void panel_dvi_power_off(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (gpio_is_valid(ddata->pd_gpio))
		gpio_set_value(ddata->pd_gpio, 0);

	omapdss_dpi_display_disable(dssdev);
}

static int panel_dvi_probe(struct omap_dss_device *dssdev)
{
	struct panel_dvi_platform_data *pdata = get_pdata(dssdev);
	struct panel_drv_data *ddata;
	int r;

	ddata = kzalloc(sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	dssdev->panel.timings = panel_dvi_default_timings;
	dssdev->panel.config = OMAP_DSS_LCD_TFT;

	ddata->dssdev = dssdev;
	mutex_init(&ddata->lock);

	if (pdata)
		ddata->pd_gpio = pdata->power_down_gpio;
	else
		ddata->pd_gpio = -1;

	if (gpio_is_valid(ddata->pd_gpio)) {
		r = gpio_request_one(ddata->pd_gpio, GPIOF_OUT_INIT_LOW,
				"tfp410 pd");
		if (r) {
			dev_err(&dssdev->dev, "Failed to request PD GPIO %d\n",
					ddata->pd_gpio);
			ddata->pd_gpio = -1;
		}
	}

	dev_set_drvdata(&dssdev->dev, ddata);

	return 0;
}

static void __exit panel_dvi_remove(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	mutex_lock(&ddata->lock);

	if (gpio_is_valid(ddata->pd_gpio))
		gpio_free(ddata->pd_gpio);

	dev_set_drvdata(&dssdev->dev, NULL);

	mutex_unlock(&ddata->lock);

	kfree(ddata);
}

static int panel_dvi_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&ddata->lock);

	r = panel_dvi_power_on(dssdev);
	if (r == 0)
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&ddata->lock);

	return r;
}

static void panel_dvi_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	mutex_lock(&ddata->lock);

	panel_dvi_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&ddata->lock);
}

static int panel_dvi_suspend(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	mutex_lock(&ddata->lock);

	panel_dvi_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	mutex_unlock(&ddata->lock);

	return 0;
}

static int panel_dvi_resume(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&ddata->lock);

	r = panel_dvi_power_on(dssdev);
	if (r == 0)
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&ddata->lock);

	return r;
}

static void panel_dvi_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	mutex_lock(&ddata->lock);
	dpi_set_timings(dssdev, timings);
	mutex_unlock(&ddata->lock);
}

static void panel_dvi_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);

	mutex_lock(&ddata->lock);
	*timings = dssdev->panel.timings;
	mutex_unlock(&ddata->lock);
}

static int panel_dvi_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	int r;

	mutex_lock(&ddata->lock);
	r = dpi_check_timings(dssdev, timings);
	mutex_unlock(&ddata->lock);

	return r;
}


static int panel_dvi_ddc_read(struct i2c_adapter *adapter,
		unsigned char *buf, u16 count, u8 offset)
{
	int r, retries;

	for (retries = 3; retries > 0; retries--) {
		struct i2c_msg msgs[] = {
			{
				.addr   = DDC_ADDR,
				.flags  = 0,
				.len    = 1,
				.buf    = &offset,
			}, {
				.addr   = DDC_ADDR,
				.flags  = I2C_M_RD,
				.len    = count,
				.buf    = buf,
			}
		};

		r = i2c_transfer(adapter, msgs, 2);
		if (r == 2)
			return 0;

		if (r != -EAGAIN)
			break;
	}

	return r < 0 ? r : -EIO;
}

static int panel_dvi_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	struct panel_dvi_platform_data *pdata = get_pdata(dssdev);
	struct i2c_adapter *adapter;
	int r, l, bytes_read;

	mutex_lock(&ddata->lock);

	if (pdata->i2c_bus_num == 0) {
		r = -ENODEV;
		goto err;
	}

	adapter = i2c_get_adapter(pdata->i2c_bus_num);
	if (!adapter) {
		dev_err(&dssdev->dev, "Failed to get I2C adapter, bus %d\n",
				pdata->i2c_bus_num);
		r = -EINVAL;
		goto err;
	}

	l = min(EDID_LENGTH, len);
	r = panel_dvi_ddc_read(adapter, edid, l, 0);
	if (r)
		goto err;

	bytes_read = l;

	/* if there are extensions, read second block */
	if (len > EDID_LENGTH && edid[0x7e] > 0) {
		l = min(EDID_LENGTH, len - EDID_LENGTH);

		r = panel_dvi_ddc_read(adapter, edid + EDID_LENGTH,
				l, EDID_LENGTH);
		if (r)
			goto err;

		bytes_read += l;
	}

	mutex_unlock(&ddata->lock);

	return bytes_read;

err:
	mutex_unlock(&ddata->lock);
	return r;
}

static bool panel_dvi_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&dssdev->dev);
	struct panel_dvi_platform_data *pdata = get_pdata(dssdev);
	struct i2c_adapter *adapter;
	unsigned char out;
	int r;

	mutex_lock(&ddata->lock);

	if (pdata->i2c_bus_num == 0)
		goto out;

	adapter = i2c_get_adapter(pdata->i2c_bus_num);
	if (!adapter)
		goto out;

	r = panel_dvi_ddc_read(adapter, &out, 1, 0);

	mutex_unlock(&ddata->lock);

	return r == 0;

out:
	mutex_unlock(&ddata->lock);
	return true;
}

static struct omap_dss_driver panel_dvi_driver = {
	.probe		= panel_dvi_probe,
	.remove		= __exit_p(panel_dvi_remove),

	.enable		= panel_dvi_enable,
	.disable	= panel_dvi_disable,
	.suspend	= panel_dvi_suspend,
	.resume		= panel_dvi_resume,

	.set_timings	= panel_dvi_set_timings,
	.get_timings	= panel_dvi_get_timings,
	.check_timings	= panel_dvi_check_timings,

	.read_edid	= panel_dvi_read_edid,
	.detect		= panel_dvi_detect,

	.driver         = {
		.name   = "dvi",
		.owner  = THIS_MODULE,
	},
};

static int __init panel_dvi_init(void)
{
	return omap_dss_register_driver(&panel_dvi_driver);
}

static void __exit panel_dvi_exit(void)
{
	omap_dss_unregister_driver(&panel_dvi_driver);
}

module_init(panel_dvi_init);
module_exit(panel_dvi_exit);
MODULE_LICENSE("GPL");
