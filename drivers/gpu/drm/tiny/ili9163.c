// SPDX-License-Identifier: GPL-2.0+

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modeset_helper.h>

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

static void yx240qv29_enable(struct drm_simple_display_pipe *pipe,
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
	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs ili9163_pipe_funcs = {
	.enable = yx240qv29_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = mipi_dbi_pipe_update,
	.prepare_fb = drm_gem_simple_display_pipe_prepare_fb,
};

static const struct drm_display_mode yx240qv29_mode = {
	DRM_SIMPLE_MODE(128, 160, 28, 35),
};

DEFINE_DRM_GEM_CMA_FOPS(ili9163_fops);

static struct drm_driver ili9163_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &ili9163_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "ili9163",
	.desc			= "Ilitek ILI9163",
	.date			= "20210208",
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
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &ili9163_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

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

	ret = mipi_dbi_dev_init(dbidev, &ili9163_pipe_funcs, &yx240qv29_mode, rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int ili9163_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
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
