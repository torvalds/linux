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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>

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
	gpiod_set_value_cansleep(mipi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 2);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, 8, buf, 2);
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

	gpiod_set_value_cansleep(mipi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);
	ret = mipi_dbi_spi_transfer(spi, speed_hz, bpw, data, num);
 free:
	kfree(buf);

	return ret;
}

static void waveshare_enable(struct drm_simple_display_pipe *pipe,
			     struct drm_crtc_state *crtc_state,
			     struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	u8 addr_mode;
	int ret, idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
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
	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
 out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs waveshare_pipe_funcs = {
	.enable = waveshare_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode waveshare_mode = {
	DRM_SIMPLE_MODE(480, 320, 73, 49),
};

DEFINE_DRM_GEM_CMA_FOPS(ili9486_fops);

static struct drm_driver ili9486_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9486_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9486",
	.desc			= "Ilitek ILI9486",
	.date			= "20200118",
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
	{ }
};
MODULE_DEVICE_TABLE(spi, ili9486_id);

static int ili9486_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &ili9486_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset)) {
		DRM_DEV_ERROR(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(dbi->reset);
	}

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
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

	dbi->command = waveshare_command;
	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init(dbidev, &waveshare_pipe_funcs,
				&waveshare_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int ili9486_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
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
