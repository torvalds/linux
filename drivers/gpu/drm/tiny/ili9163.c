// SPDX-License-Identifier: GPL-2.0+

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_print.h>

#include <video/mipi_display.h>

#define ILI9163_FRMCTR1		0xb1

#define ILI9163_PWCTRL1		0xc0
#define ILI9163_PWCTRL2		0xc1
#define ILI9163_VMCTRL1		0xc5
#define ILI9163_VMCTRL2		0xc7
#define ILI9163_PWCTRLA		0xcb
#define ILI9163_PWCTRLB		0xcf

#define ILI9163_EN3GAM		0xf2

#define ILI9163_MADCTL_BGR	BIT(3)
#define ILI9163_MADCTL_MV	BIT(5)
#define ILI9163_MADCTL_MX	BIT(6)
#define ILI9163_MADCTL_MY	BIT(7)

struct ili9163_device {
	struct mipi_dbi_dev dbidev;

	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct ili9163_device *to_ili9163_device(struct drm_device *dev)
{
	return container_of(drm_to_mipi_dbi_dev(dev), struct ili9163_device, dbidev);
}

static const u32 ili9163_plane_formats[] = {
	DRM_MIPI_DBI_PLANE_FORMATS,
};

static const u64 ili9163_plane_format_modifiers[] = {
	DRM_MIPI_DBI_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs ili9163_plane_helper_funcs = {
	DRM_MIPI_DBI_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs ili9163_plane_funcs = {
	DRM_MIPI_DBI_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static void ili9163_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					      struct drm_atomic_state *state)
{
	struct drm_device *drm = crtc->dev;
	struct ili9163_device *ili9163 = to_ili9163_device(drm);
	struct mipi_dbi_dev *dbidev = &ili9163->dbidev;
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

	/* Gamma */
	mipi_dbi_command(dbi, MIPI_DCS_SET_GAMMA_CURVE, 0x04);
	mipi_dbi_command(dbi, ILI9163_EN3GAM, 0x00);

	/* Frame Rate */
	mipi_dbi_command(dbi, ILI9163_FRMCTR1, 0x0a, 0x14);

	/* Power Control */
	mipi_dbi_command(dbi, ILI9163_PWCTRL1, 0x0a, 0x00);
	mipi_dbi_command(dbi, ILI9163_PWCTRL2, 0x02);

	/* VCOM */
	mipi_dbi_command(dbi, ILI9163_VMCTRL1, 0x2f, 0x3e);
	mipi_dbi_command(dbi, ILI9163_VMCTRL2, 0x40);

	/* Memory Access Control */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);

	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

out_enable:
	switch (dbidev->rotation) {
	default:
		addr_mode = ILI9163_MADCTL_MX | ILI9163_MADCTL_MY;
		break;
	case 90:
		addr_mode = ILI9163_MADCTL_MX | ILI9163_MADCTL_MV;
		break;
	case 180:
		addr_mode = 0;
		break;
	case 270:
		addr_mode = ILI9163_MADCTL_MY | ILI9163_MADCTL_MV;
		break;
	}
	addr_mode |= ILI9163_MADCTL_BGR;
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	backlight_enable(dbidev->backlight);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs ili9163_crtc_helper_funcs = {
	DRM_MIPI_DBI_CRTC_HELPER_FUNCS,
	.atomic_enable = ili9163_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs ili9163_crtc_funcs = {
	DRM_MIPI_DBI_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs ili9163_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs ili9163_connector_helper_funcs = {
	DRM_MIPI_DBI_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs ili9163_connector_funcs = {
	DRM_MIPI_DBI_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_helper_funcs ili9163_mode_config_helper_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_HELPER_FUNCS,
};

static const struct drm_mode_config_funcs ili9163_mode_config_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_FUNCS,
};

static const struct drm_display_mode yx240qv29_mode = {
	DRM_SIMPLE_MODE(128, 160, 28, 35),
};

DEFINE_DRM_GEM_DMA_FOPS(ili9163_fops);

static struct drm_driver ili9163_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9163_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9163",
	.desc			= "Ilitek ILI9163",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id ili9163_of_match[] = {
	{ .compatible = "newhaven,1.8-128160EF" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9163_of_match);

static const struct spi_device_id ili9163_id[] = {
	{ "nhd-1.8-128160EF", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9163_id);

static int ili9163_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ili9163_device *ili9163;
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

	ili9163 = devm_drm_dev_alloc(dev, &ili9163_driver, struct ili9163_device, dbidev.drm);
	if (IS_ERR(ili9163))
		return PTR_ERR(ili9163);
	dbidev = &ili9163->dbidev;
	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	spi_set_drvdata(spi, drm);

	dbi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	ret = drm_mipi_dbi_dev_init(dbidev, &yx240qv29_mode, ili9163_plane_formats[0],
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
	drm->mode_config.funcs = &ili9163_mode_config_funcs;
	drm->mode_config.preferred_depth = 16;
	drm->mode_config.helper_private = &ili9163_mode_config_helper_funcs;

	plane = &ili9163->plane;
	ret = drm_universal_plane_init(drm, plane, 0, &ili9163_plane_funcs,
				       ili9163_plane_formats, ARRAY_SIZE(ili9163_plane_formats),
				       ili9163_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &ili9163_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(plane);

	crtc = &ili9163->crtc;
	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL, &ili9163_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &ili9163_crtc_helper_funcs);

	encoder = &ili9163->encoder;
	ret = drm_encoder_init(drm, encoder, &ili9163_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	connector = &ili9163->connector;
	ret = drm_connector_init(drm, connector, &ili9163_connector_funcs,
				 DRM_MODE_CONNECTOR_SPI);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &ili9163_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	drm_client_setup(drm, NULL);

	return 0;
}

static void ili9163_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void ili9163_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9163_spi_driver = {
	.driver = {
		.name = "ili9163",
		.of_match_table = ili9163_of_match,
	},
	.id_table = ili9163_id,
	.probe = ili9163_probe,
	.remove = ili9163_remove,
	.shutdown = ili9163_shutdown,
};
module_spi_driver(ili9163_spi_driver);

MODULE_DESCRIPTION("Ilitek ILI9163 DRM driver");
MODULE_AUTHOR("Daniel Mack <daniel@zonque.org>");
MODULE_LICENSE("GPL");
