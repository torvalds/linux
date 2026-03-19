// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for Multi-Inno MI0283QT panels
 *
 * Copyright 2016 Noralf Trønnes
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_print.h>
#include <video/mipi_display.h>

#define ILI9341_FRMCTR1		0xb1
#define ILI9341_DISCTRL		0xb6
#define ILI9341_ETMOD		0xb7

#define ILI9341_PWCTRL1		0xc0
#define ILI9341_PWCTRL2		0xc1
#define ILI9341_VMCTRL1		0xc5
#define ILI9341_VMCTRL2		0xc7
#define ILI9341_PWCTRLA		0xcb
#define ILI9341_PWCTRLB		0xcf

#define ILI9341_PGAMCTRL	0xe0
#define ILI9341_NGAMCTRL	0xe1
#define ILI9341_DTCTRLA		0xe8
#define ILI9341_DTCTRLB		0xea
#define ILI9341_PWRSEQ		0xed

#define ILI9341_EN3GAM		0xf2
#define ILI9341_PUMPCTRL	0xf7

#define ILI9341_MADCTL_BGR	BIT(3)
#define ILI9341_MADCTL_MV	BIT(5)
#define ILI9341_MADCTL_MX	BIT(6)
#define ILI9341_MADCTL_MY	BIT(7)

struct mi0283qt_device {
	struct mipi_dbi_dev dbidev;

	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct mi0283qt_device *to_mi0283qt_device(struct drm_device *dev)
{
	return container_of(drm_to_mipi_dbi_dev(dev), struct mi0283qt_device, dbidev);
}

static const u32 mi0283qt_plane_formats[] = {
	DRM_MIPI_DBI_PLANE_FORMATS,
};

static const u64 mi0283qt_plane_format_modifiers[] = {
	DRM_MIPI_DBI_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs mi0283qt_plane_helper_funcs = {
	DRM_MIPI_DBI_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mi0283qt_plane_funcs = {
	DRM_MIPI_DBI_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static void mi0283qt_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					       struct drm_atomic_state *state)
{
	struct drm_device *drm = crtc->dev;
	struct mi0283qt_device *mi0283qt = to_mi0283qt_device(drm);
	struct mipi_dbi_dev *dbidev = &mi0283qt->dbidev;
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode;
	int ret, idx;

	if (!drm_dev_enter(drm, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_conditional_reset(dbidev);
	if (ret < 0)
		goto out_exit;
	if (ret == 1)
		goto out_enable;

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);

	mipi_dbi_command(dbi, ILI9341_PWCTRLB, 0x00, 0x83, 0x30);
	mipi_dbi_command(dbi, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	mipi_dbi_command(dbi, ILI9341_DTCTRLA, 0x85, 0x01, 0x79);
	mipi_dbi_command(dbi, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	mipi_dbi_command(dbi, ILI9341_PUMPCTRL, 0x20);
	mipi_dbi_command(dbi, ILI9341_DTCTRLB, 0x00, 0x00);

	/* Power Control */
	mipi_dbi_command(dbi, ILI9341_PWCTRL1, 0x26);
	mipi_dbi_command(dbi, ILI9341_PWCTRL2, 0x11);
	/* VCOM */
	mipi_dbi_command(dbi, ILI9341_VMCTRL1, 0x35, 0x3e);
	mipi_dbi_command(dbi, ILI9341_VMCTRL2, 0xbe);

	/* Memory Access Control */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);

	/* Frame Rate */
	mipi_dbi_command(dbi, ILI9341_FRMCTR1, 0x00, 0x1b);

	/* Gamma */
	mipi_dbi_command(dbi, ILI9341_EN3GAM, 0x08);
	mipi_dbi_command(dbi, MIPI_DCS_SET_GAMMA_CURVE, 0x01);
	mipi_dbi_command(dbi, ILI9341_PGAMCTRL,
		       0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87,
		       0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00);
	mipi_dbi_command(dbi, ILI9341_NGAMCTRL,
		       0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78,
		       0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f);

	/* DDRAM */
	mipi_dbi_command(dbi, ILI9341_ETMOD, 0x07);

	/* Display */
	mipi_dbi_command(dbi, ILI9341_DISCTRL, 0x0a, 0x82, 0x27, 0x00);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

out_enable:
	/* The PiTFT (ili9340) has a hardware reset circuit that
	 * resets only on power-on and not on each reboot through
	 * a gpio like the rpi-display does.
	 * As a result, we need to always apply the rotation value
	 * regardless of the display "on/off" state.
	 */
	switch (dbidev->rotation) {
	default:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9341_MADCTL_BGR;
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	backlight_enable(dbidev->backlight);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs mi0283qt_crtc_helper_funcs = {
	DRM_MIPI_DBI_CRTC_HELPER_FUNCS,
	.atomic_enable = mi0283qt_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs mi0283qt_crtc_funcs = {
	DRM_MIPI_DBI_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs mi0283qt_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs mi0283qt_connector_helper_funcs = {
	DRM_MIPI_DBI_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs mi0283qt_connector_funcs = {
	DRM_MIPI_DBI_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_helper_funcs mi0283qt_mode_config_helper_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_HELPER_FUNCS,
};

static const struct drm_mode_config_funcs mi0283qt_mode_config_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_FUNCS,
};

static const struct drm_display_mode mi0283qt_mode = {
	DRM_SIMPLE_MODE(320, 240, 58, 43),
};

DEFINE_DRM_GEM_DMA_FOPS(mi0283qt_fops);

static const struct drm_driver mi0283qt_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &mi0283qt_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "mi0283qt",
	.desc			= "Multi-Inno MI0283QT",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id mi0283qt_of_match[] = {
	{ .compatible = "multi-inno,mi0283qt" },
	{},
};
MODULE_DEVICE_TABLE(of, mi0283qt_of_match);

static const struct spi_device_id mi0283qt_id[] = {
	{ "mi0283qt", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, mi0283qt_id);

static int mi0283qt_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mi0283qt_device *mi0283qt;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	u32 rotation = 0;
	int ret;

	mi0283qt = devm_drm_dev_alloc(dev, &mi0283qt_driver, struct mi0283qt_device, dbidev.drm);
	if (IS_ERR(mi0283qt))
		return PTR_ERR(mi0283qt);
	dbidev = &mi0283qt->dbidev;
	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc), "Failed to get GPIO 'dc'\n");

	dbidev->regulator = devm_regulator_get(dev, "power");
	if (IS_ERR(dbidev->regulator))
		return PTR_ERR(dbidev->regulator);

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	ret = drm_mipi_dbi_dev_init(dbidev, &mi0283qt_mode, mi0283qt_plane_formats[0],
				    rotation, 0);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = dbidev->mode.hdisplay;
	drm->mode_config.max_width = dbidev->mode.hdisplay;
	drm->mode_config.min_height = dbidev->mode.vdisplay;
	drm->mode_config.max_height = dbidev->mode.vdisplay;
	drm->mode_config.funcs = &mi0283qt_mode_config_funcs;
	drm->mode_config.preferred_depth = 16;
	drm->mode_config.helper_private = &mi0283qt_mode_config_helper_funcs;

	plane = &mi0283qt->plane;
	ret = drm_universal_plane_init(drm, plane, 0, &mi0283qt_plane_funcs,
				       mi0283qt_plane_formats, ARRAY_SIZE(mi0283qt_plane_formats),
				       mi0283qt_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &mi0283qt_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(plane);

	crtc = &mi0283qt->crtc;
	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL, &mi0283qt_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &mi0283qt_crtc_helper_funcs);

	encoder = &mi0283qt->encoder;
	ret = drm_encoder_init(drm, encoder, &mi0283qt_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	connector = &mi0283qt->connector;
	ret = drm_connector_init(drm, connector, &mi0283qt_connector_funcs,
				 DRM_MODE_CONNECTOR_SPI);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &mi0283qt_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_client_setup(drm, NULL);

	return 0;
}

static void mi0283qt_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void mi0283qt_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static int __maybe_unused mi0283qt_pm_suspend(struct device *dev)
{
	return drm_mode_config_helper_suspend(dev_get_drvdata(dev));
}

static int __maybe_unused mi0283qt_pm_resume(struct device *dev)
{
	drm_mode_config_helper_resume(dev_get_drvdata(dev));

	return 0;
}

static const struct dev_pm_ops mi0283qt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mi0283qt_pm_suspend, mi0283qt_pm_resume)
};

static struct spi_driver mi0283qt_spi_driver = {
	.driver = {
		.name = "mi0283qt",
		.of_match_table = mi0283qt_of_match,
		.pm = &mi0283qt_pm_ops,
	},
	.id_table = mi0283qt_id,
	.probe = mi0283qt_probe,
	.remove = mi0283qt_remove,
	.shutdown = mi0283qt_shutdown,
};
module_spi_driver(mi0283qt_spi_driver);

MODULE_DESCRIPTION("Multi-Inno MI0283QT DRM driver");
MODULE_AUTHOR("Noralf Trønnes");
MODULE_LICENSE("GPL");
