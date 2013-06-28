/*
 * LCD panel driver for LG.Philips LB035Q02
 *
 * Author: Steve Sakoman <steve@sakoman.com>
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
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/gpio.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

struct lb035q02_data {
	struct mutex lock;
};

static struct omap_video_timings lb035q02_timings = {
	.x_res = 320,
	.y_res = 240,

	.pixel_clock	= 6500,

	.hsw		= 2,
	.hfp		= 20,
	.hbp		= 68,

	.vsw		= 2,
	.vfp		= 4,
	.vbp		= 18,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
};

static inline struct panel_generic_dpi_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct panel_generic_dpi_data *) dssdev->data;
}

static int lb035q02_panel_power_on(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	int r, i;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	omapdss_dpi_set_timings(dssdev, &dssdev->panel.timings);
	omapdss_dpi_set_data_lines(dssdev, dssdev->phy.dpi.data_lines);

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	for (i = 0; i < panel_data->num_gpios; ++i) {
		gpio_set_value_cansleep(panel_data->gpios[i],
				panel_data->gpio_invert[i] ? 0 : 1);
	}

	return 0;

err0:
	return r;
}

static void lb035q02_panel_power_off(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	int i;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	for (i = panel_data->num_gpios - 1; i >= 0; --i) {
		gpio_set_value_cansleep(panel_data->gpios[i],
				panel_data->gpio_invert[i] ? 1 : 0);
	}

	omapdss_dpi_display_disable(dssdev);
}

static int lb035q02_panel_probe(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct lb035q02_data *ld;
	int r, i;

	if (!panel_data)
		return -EINVAL;

	dssdev->panel.timings = lb035q02_timings;

	ld = devm_kzalloc(dssdev->dev, sizeof(*ld), GFP_KERNEL);
	if (!ld)
		return -ENOMEM;

	for (i = 0; i < panel_data->num_gpios; ++i) {
		r = devm_gpio_request_one(dssdev->dev, panel_data->gpios[i],
				panel_data->gpio_invert[i] ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
				"panel gpio");
		if (r)
			return r;
	}

	mutex_init(&ld->lock);
	dev_set_drvdata(dssdev->dev, ld);

	return 0;
}

static void lb035q02_panel_remove(struct omap_dss_device *dssdev)
{
}

static int lb035q02_panel_enable(struct omap_dss_device *dssdev)
{
	struct lb035q02_data *ld = dev_get_drvdata(dssdev->dev);
	int r;

	mutex_lock(&ld->lock);

	r = lb035q02_panel_power_on(dssdev);
	if (r)
		goto err;
	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	mutex_unlock(&ld->lock);
	return 0;
err:
	mutex_unlock(&ld->lock);
	return r;
}

static void lb035q02_panel_disable(struct omap_dss_device *dssdev)
{
	struct lb035q02_data *ld = dev_get_drvdata(dssdev->dev);

	mutex_lock(&ld->lock);

	lb035q02_panel_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&ld->lock);
}

static struct omap_dss_driver lb035q02_driver = {
	.probe		= lb035q02_panel_probe,
	.remove		= lb035q02_panel_remove,

	.enable		= lb035q02_panel_enable,
	.disable	= lb035q02_panel_disable,

	.driver         = {
		.name   = "lgphilips_lb035q02_panel",
		.owner  = THIS_MODULE,
	},
};

static int lb035q02_write_reg(struct spi_device *spi, u8 reg, u16 val)
{
	struct spi_message msg;
	struct spi_transfer index_xfer = {
		.len		= 3,
		.cs_change	= 1,
	};
	struct spi_transfer value_xfer = {
		.len		= 3,
	};
	u8	buffer[16];

	spi_message_init(&msg);

	/* register index */
	buffer[0] = 0x70;
	buffer[1] = 0x00;
	buffer[2] = reg & 0x7f;
	index_xfer.tx_buf = buffer;
	spi_message_add_tail(&index_xfer, &msg);

	/* register value */
	buffer[4] = 0x72;
	buffer[5] = val >> 8;
	buffer[6] = val;
	value_xfer.tx_buf = buffer + 4;
	spi_message_add_tail(&value_xfer, &msg);

	return spi_sync(spi, &msg);
}

static void init_lb035q02_panel(struct spi_device *spi)
{
	/* Init sequence from page 28 of the lb035q02 spec */
	lb035q02_write_reg(spi, 0x01, 0x6300);
	lb035q02_write_reg(spi, 0x02, 0x0200);
	lb035q02_write_reg(spi, 0x03, 0x0177);
	lb035q02_write_reg(spi, 0x04, 0x04c7);
	lb035q02_write_reg(spi, 0x05, 0xffc0);
	lb035q02_write_reg(spi, 0x06, 0xe806);
	lb035q02_write_reg(spi, 0x0a, 0x4008);
	lb035q02_write_reg(spi, 0x0b, 0x0000);
	lb035q02_write_reg(spi, 0x0d, 0x0030);
	lb035q02_write_reg(spi, 0x0e, 0x2800);
	lb035q02_write_reg(spi, 0x0f, 0x0000);
	lb035q02_write_reg(spi, 0x16, 0x9f80);
	lb035q02_write_reg(spi, 0x17, 0x0a0f);
	lb035q02_write_reg(spi, 0x1e, 0x00c1);
	lb035q02_write_reg(spi, 0x30, 0x0300);
	lb035q02_write_reg(spi, 0x31, 0x0007);
	lb035q02_write_reg(spi, 0x32, 0x0000);
	lb035q02_write_reg(spi, 0x33, 0x0000);
	lb035q02_write_reg(spi, 0x34, 0x0707);
	lb035q02_write_reg(spi, 0x35, 0x0004);
	lb035q02_write_reg(spi, 0x36, 0x0302);
	lb035q02_write_reg(spi, 0x37, 0x0202);
	lb035q02_write_reg(spi, 0x3a, 0x0a0d);
	lb035q02_write_reg(spi, 0x3b, 0x0806);
}

static int lb035q02_panel_spi_probe(struct spi_device *spi)
{
	init_lb035q02_panel(spi);
	return omap_dss_register_driver(&lb035q02_driver);
}

static int lb035q02_panel_spi_remove(struct spi_device *spi)
{
	omap_dss_unregister_driver(&lb035q02_driver);
	return 0;
}

static struct spi_driver lb035q02_spi_driver = {
	.driver		= {
		.name	= "lgphilips_lb035q02_panel-spi",
		.owner	= THIS_MODULE,
	},
	.probe		= lb035q02_panel_spi_probe,
	.remove		= lb035q02_panel_spi_remove,
};

module_spi_driver(lb035q02_spi_driver);

MODULE_LICENSE("GPL");
