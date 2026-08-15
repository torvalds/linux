// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for Ilitek ILI9486 panels
 *
 * Copyright 2020 Kamlesh Gurudasani <kamlesh.gurudasani@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

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

#define ILI9486_ITFCTR1         0xb0
#define ILI9486_PWCTRL1         0xc2
#define ILI9486_VMCTRL1         0xc5
#define ILI9486_PGAMCTRL        0xe0
#define ILI9486_NGAMCTRL        0xe1
#define ILI9486_DGAMCTRL        0xe2
#define ILI9486_MADCTL_BGR      BIT(3)
#define ILI9486_MADCTL_MV       BIT(5)
#define ILI9486_MADCTL_MX       BIT(6)
#define ILI9486_MADCTL_MY       BIT(7)

struct ili9486_device {
	struct mipi_dbi_dev dbidev;

	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct ili9486_device *to_ili9486_device(struct drm_device *dev)
{
	return container_of(drm_to_mipi_dbi_dev(dev), struct ili9486_device, dbidev);
}

/*
 * The PiScreen/waveshare rpi-lcd-35 has a SPI to 16-bit parallel bus converter
 * in front of the  display controller. This means that 8-bit values have to be
 * transferred as 16-bit.
 */
static int waveshare_command(struct mipi_dbi *mipi, u8 *cmd, u8 *par,
			     size_t num)
{
	struct spi_device *spi = mipi->spi;
	unsigned int bpw = 8;
	void *data = par;
	u32 speed_hz;
	int i, ret;
	__be16 *buf;

	buf = kmalloc(32 * sizeof(u16), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * The displays are Raspberry Pi HATs and connected to the 8-bit only
	 * SPI controller, so 16-bit command and parameters need byte swapping
	 * before being transferred as 8-bit on the big endian SPI bus.
	 */
	buf[0] = cpu_to_be16(*cmd);
	spi_bus_lock(spi->controller);
	gpiod_set_value_cansleep(mipi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 2);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, 8, buf, 2);
	spi_bus_unlock(spi->controller);
	if (ret || !num)
		goto free;

	/* 8-bit configuration data, not 16-bit pixel data */
	if (num <= 32) {
		for (i = 0; i < num; i++)
			buf[i] = cpu_to_be16(par[i]);
		num *= 2;
		data = buf;
	}

	/*
	 * Check whether pixel data bytes needs to be swapped or not
	 */
	if (*cmd == MIPI_DCS_WRITE_MEMORY_START && !mipi->swap_bytes)
		bpw = 16;

	spi_bus_lock(spi->controller);
	gpiod_set_value_cansleep(mipi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, bpw, data, num);
	spi_bus_unlock(spi->controller);
 free:
	kfree(buf);

	return ret;
}

static const u32 ili9486_plane_formats[] = {
	DRM_MIPI_DBI_PLANE_FORMATS,
};

static const u64 ili9486_plane_format_modifiers[] = {
	DRM_MIPI_DBI_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs ili9486_plane_helper_funcs = {
	DRM_MIPI_DBI_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs ili9486_plane_funcs = {
	DRM_MIPI_DBI_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static void ili9486_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					      struct drm_atomic_commit *state)
{
	struct drm_device *drm = crtc->dev;
	struct ili9486_device *ili9486 = to_ili9486_device(drm);
	struct mipi_dbi_dev *dbidev = &ili9486->dbidev;
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

	mipi_dbi_command(dbi, ILI9486_ITFCTR1);
	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(250);

	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT, 0x55);

	mipi_dbi_command(dbi, ILI9486_PWCTRL1, 0x44);

	mipi_dbi_command(dbi, ILI9486_VMCTRL1, 0x00, 0x00, 0x00, 0x00);

	mipi_dbi_command(dbi, ILI9486_PGAMCTRL,
			 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
			 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x0);
	mipi_dbi_command(dbi, ILI9486_NGAMCTRL,
			 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
			 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00);
	mipi_dbi_command(dbi, ILI9486_DGAMCTRL,
			 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
			 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

 out_enable:
	switch (dbidev->rotation) {
	case 90:
		addr_mode = ILI9486_MADCTL_MY;
		break;
	case 180:
		addr_mode = ILI9486_MADCTL_MV;
		break;
	case 270:
		addr_mode = ILI9486_MADCTL_MX;
		break;
	default:
		addr_mode = ILI9486_MADCTL_MV | ILI9486_MADCTL_MY |
			ILI9486_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9486_MADCTL_BGR;
	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	backlight_enable(dbidev->backlight);
 out_exit:
	drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs ili9486_crtc_helper_funcs = {
	DRM_MIPI_DBI_CRTC_HELPER_FUNCS,
	.atomic_enable = ili9486_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs ili9486_crtc_funcs = {
	DRM_MIPI_DBI_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs ili9486_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs ili9486_connector_helper_funcs = {
	DRM_MIPI_DBI_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs ili9486_connector_funcs = {
	DRM_MIPI_DBI_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_helper_funcs ili9486_mode_config_helper_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_HELPER_FUNCS,
};

static const struct drm_mode_config_funcs ili9486_mode_config_funcs = {
	DRM_MIPI_DBI_MODE_CONFIG_FUNCS,
};

static const struct drm_display_mode waveshare_mode = {
	DRM_SIMPLE_MODE(480, 320, 73, 49),
};

DEFINE_DRM_GEM_DMA_FOPS(ili9486_fops);

static const struct drm_driver ili9486_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9486_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9486",
	.desc			= "Ilitek ILI9486",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id ili9486_of_match[] = {
	{ .compatible = "waveshare,rpi-lcd-35" },
	{ .compatible = "ozzmaker,piscreen" },
	{},
};
MODULE_DEVICE_TABLE(of, ili9486_of_match);

static const struct spi_device_id ili9486_id[] = {
	{ "ili9486", 0 },
	{ "rpi-lcd-35", 0 },
	{ "piscreen", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9486_id);

static int ili9486_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ili9486_device *ili9486;
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

	ili9486 = devm_drm_dev_alloc(dev, &ili9486_driver, struct ili9486_device, dbidev.drm);
	if (IS_ERR(ili9486))
		return PTR_ERR(ili9486);
	dbidev = &ili9486->dbidev;
	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get GPIO 'reset'\n");

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc), "Failed to get GPIO 'dc'\n");

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	dbi->command = waveshare_command;
	dbi->read_commands = NULL;

	ret = drm_mipi_dbi_dev_init(dbidev, &waveshare_mode, ili9486_plane_formats[0],
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
	drm->mode_config.funcs = &ili9486_mode_config_funcs;
	drm->mode_config.preferred_depth = 16;
	drm->mode_config.helper_private = &ili9486_mode_config_helper_funcs;

	plane = &ili9486->plane;
	ret = drm_universal_plane_init(drm, plane, 0, &ili9486_plane_funcs,
				       ili9486_plane_formats, ARRAY_SIZE(ili9486_plane_formats),
				       ili9486_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(plane, &ili9486_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(plane);

	crtc = &ili9486->crtc;
	ret = drm_crtc_init_with_planes(drm, crtc, plane, NULL, &ili9486_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &ili9486_crtc_helper_funcs);

	encoder = &ili9486->encoder;
	ret = drm_encoder_init(drm, encoder, &ili9486_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	connector = &ili9486->connector;
	ret = drm_connector_init(drm, connector, &ili9486_connector_funcs,
				 DRM_MODE_CONNECTOR_SPI);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &ili9486_connector_helper_funcs);

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

static void ili9486_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static void ili9486_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver ili9486_spi_driver = {
	.driver = {
		.name = "ili9486",
		.of_match_table = ili9486_of_match,
	},
	.id_table = ili9486_id,
	.probe = ili9486_probe,
	.remove = ili9486_remove,
	.shutdown = ili9486_shutdown,
};
module_spi_driver(ili9486_spi_driver);

MODULE_DESCRIPTION("Ilitek ILI9486 DRM driver");
MODULE_AUTHOR("Kamlesh Gurudasani <kamlesh.gurudasani@gmail.com>");
MODULE_LICENSE("GPL");
