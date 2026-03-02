// SPDX-License-Identifier: GPL-2.0-only

#include <linux/aperture.h>
#include <linux/coreboot.h>
#include <linux/minmax.h>
#include <linux/platform_device.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "drm_sysfb_helper.h"

#define DRIVER_NAME	"corebootdrm"
#define DRIVER_DESC	"DRM driver for Coreboot framebuffers"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static const struct drm_format_info *
corebootdrm_get_format_fb(struct drm_device *dev, const struct lb_framebuffer *fb)
{
	static const struct drm_sysfb_format formats[] = {
		{ PIXEL_FORMAT_XRGB1555, DRM_FORMAT_XRGB1555, },
		{ PIXEL_FORMAT_RGB565, DRM_FORMAT_RGB565, },
		{ PIXEL_FORMAT_RGB888, DRM_FORMAT_RGB888, },
		{ PIXEL_FORMAT_XRGB8888, DRM_FORMAT_XRGB8888, },
		{ PIXEL_FORMAT_XBGR8888, DRM_FORMAT_XBGR8888, },
		{ PIXEL_FORMAT_XRGB2101010, DRM_FORMAT_XRGB2101010, },
	};
	const struct pixel_format pixel = {
		.bits_per_pixel = fb->bits_per_pixel,
		.indexed  = false,
		.alpha = {
			.offset = 0,
			.length = 0,
		},
		.red = {
			.offset = fb->red_mask_pos,
			.length = fb->red_mask_size,
		},
		.green = {
			.offset = fb->green_mask_pos,
			.length = fb->green_mask_size,
		},
		.blue = {
			.offset = fb->blue_mask_pos,
			.length = fb->blue_mask_size,
		},
	};

	return drm_sysfb_get_format(dev, formats, ARRAY_SIZE(formats), &pixel);
}

static int corebootdrm_get_width_fb(struct drm_device *dev, const struct lb_framebuffer *fb)
{
	return drm_sysfb_get_validated_int0(dev, "width", fb->x_resolution, INT_MAX);
}

static int corebootdrm_get_height_fb(struct drm_device *dev, const struct lb_framebuffer *fb)
{
	return drm_sysfb_get_validated_int0(dev, "height", fb->y_resolution, INT_MAX);
}

static int corebootdrm_get_pitch_fb(struct drm_device *dev, const struct drm_format_info *format,
				    unsigned int width, const struct lb_framebuffer *fb)
{
	u64 bytes_per_line = fb->bytes_per_line;

	if (!bytes_per_line)
		bytes_per_line = drm_format_info_min_pitch(format, 0, width);

	return drm_sysfb_get_validated_int0(dev, "pitch", bytes_per_line, INT_MAX);
}

static resource_size_t corebootdrm_get_size_fb(struct drm_device *dev, unsigned int height,
					       unsigned int pitch,
					       const struct lb_framebuffer *fb)
{
	resource_size_t size;

	if (check_mul_overflow(height, pitch, &size))
		return 0;

	return size;
}

static phys_addr_t corebootdrm_get_address_fb(struct drm_device *dev, resource_size_t size,
					      const struct lb_framebuffer *fb)
{
	if (size > PHYS_ADDR_MAX)
		return 0;
	if (!fb->physical_address)
		return 0;
	if (fb->physical_address > (PHYS_ADDR_MAX - size))
		return 0;

	return fb->physical_address;
}

static enum drm_panel_orientation corebootdrm_get_orientation_fb(struct drm_device *dev,
								 const struct lb_framebuffer *fb)
{
	if (!LB_FRAMEBUFFER_HAS_ORIENTATION(fb))
		return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;

	switch (fb->orientation) {
	case LB_FRAMEBUFFER_ORIENTATION_NORMAL:
		return DRM_MODE_PANEL_ORIENTATION_NORMAL;
	case LB_FRAMEBUFFER_ORIENTATION_BOTTOM_UP:
		return DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP;
	case LB_FRAMEBUFFER_ORIENTATION_LEFT_UP:
		return DRM_MODE_PANEL_ORIENTATION_LEFT_UP;
	case LB_FRAMEBUFFER_ORIENTATION_RIGHT_UP:
		return DRM_MODE_PANEL_ORIENTATION_RIGHT_UP;
	}

	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}

/*
 * Simple Framebuffer device
 */

struct corebootdrm_device {
	struct drm_sysfb_device sysfb;

	/* modesetting */
	u32 formats[DRM_SYSFB_PLANE_NFORMATS(1)];
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

/*
 * Modesetting
 */

static const u64 corebootdrm_primary_plane_format_modifiers[] = {
	DRM_SYSFB_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs corebootdrm_primary_plane_helper_funcs = {
	DRM_SYSFB_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs corebootdrm_primary_plane_funcs = {
	DRM_SYSFB_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static const struct drm_crtc_helper_funcs corebootdrm_crtc_helper_funcs = {
	DRM_SYSFB_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs corebootdrm_crtc_funcs = {
	DRM_SYSFB_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs corebootdrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs corebootdrm_connector_helper_funcs = {
	DRM_SYSFB_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs corebootdrm_connector_funcs = {
	DRM_SYSFB_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_funcs corebootdrm_mode_config_funcs = {
	DRM_SYSFB_MODE_CONFIG_FUNCS,
};

static int corebootdrm_mode_config_init(struct corebootdrm_device *cdev,
					enum drm_panel_orientation orientation)
{
	struct drm_sysfb_device *sysfb = &cdev->sysfb;
	struct drm_device *dev = &sysfb->dev;
	const struct drm_format_info *format = sysfb->fb_format;
	unsigned int width = sysfb->fb_mode.hdisplay;
	unsigned int height = sysfb->fb_mode.vdisplay;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	size_t nformats;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = width;
	dev->mode_config.max_width = max_t(unsigned int, width, DRM_SHADOW_PLANE_MAX_WIDTH);
	dev->mode_config.min_height = height;
	dev->mode_config.max_height = max_t(unsigned int, height, DRM_SHADOW_PLANE_MAX_HEIGHT);
	dev->mode_config.funcs = &corebootdrm_mode_config_funcs;
	dev->mode_config.preferred_depth = format->depth;

	/* Primary plane */

	nformats = drm_sysfb_build_fourcc_list(dev, &format->format, 1,
					       cdev->formats, ARRAY_SIZE(cdev->formats));

	primary_plane = &cdev->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0, &corebootdrm_primary_plane_funcs,
				       cdev->formats, nformats,
				       corebootdrm_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(primary_plane, &corebootdrm_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	/* CRTC */

	crtc = &cdev->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&corebootdrm_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &corebootdrm_crtc_helper_funcs);

	/* Encoder */

	encoder = &cdev->encoder;
	ret = drm_encoder_init(dev, encoder, &corebootdrm_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* Connector */

	connector = &cdev->connector;
	ret = drm_connector_init(dev, connector, &corebootdrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &corebootdrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector, orientation,
						       width, height);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(corebootdrm_fops);

static struct drm_driver corebootdrm_drm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &corebootdrm_fops,
};

/*
 * Coreboot driver
 */

static int corebootdrm_probe(struct platform_device *pdev)
{
	const struct lb_framebuffer *fb = dev_get_platdata(&pdev->dev);
	struct corebootdrm_device *cdev;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	const struct drm_format_info *format;
	int width, height, pitch;
	resource_size_t size;
	phys_addr_t address;
	enum drm_panel_orientation orientation;
	struct resource *res, *mem = NULL;
	struct resource aperture;
	void __iomem *screen_base;
	int ret;

	cdev = devm_drm_dev_alloc(&pdev->dev, &corebootdrm_drm_driver,
				  struct corebootdrm_device, sysfb.dev);
	if (IS_ERR(cdev))
		return PTR_ERR(cdev);
	platform_set_drvdata(pdev, cdev);

	sysfb = &cdev->sysfb;
	dev = &sysfb->dev;

	if (!fb) {
		drm_err(dev, "coreboot framebuffer not found\n");
		return -EINVAL;
	} else if (!LB_FRAMEBUFFER_HAS_LFB(fb)) {
		drm_err(dev, "coreboot framebuffer entry too small\n");
		return -EINVAL;
	}

	/*
	 * Hardware settings
	 */

	format = corebootdrm_get_format_fb(dev, fb);
	if (!format)
		return -EINVAL;
	width = corebootdrm_get_width_fb(dev, fb);
	if (width < 0)
		return width;
	height = corebootdrm_get_height_fb(dev, fb);
	if (height < 0)
		return height;
	pitch = corebootdrm_get_pitch_fb(dev, format, width, fb);
	if (pitch < 0)
		return pitch;
	size = corebootdrm_get_size_fb(dev, height, pitch, fb);
	if (!size)
		return -EINVAL;
	address = corebootdrm_get_address_fb(dev, size, fb);
	if (!address)
		return -EINVAL;
	orientation = corebootdrm_get_orientation_fb(dev, fb);

	sysfb->fb_mode = drm_sysfb_mode(width, height, 0, 0);
	sysfb->fb_format = format;
	sysfb->fb_pitch = pitch;

	drm_dbg(dev, "display mode={" DRM_MODE_FMT "}\n", DRM_MODE_ARG(&sysfb->fb_mode));
	drm_dbg(dev, "framebuffer format=%p4cc, size=%dx%d, pitch=%d byte\n",
		&format->format, width, height, pitch);

	/*
	 * Memory management
	 */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		drm_err(dev, "memory resource not found\n");
		return -EINVAL;
	}

	mem = devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
				      dev->driver->name);
	if (!mem) {
		drm_warn(dev, "could not acquire memory resource at %pr\n", res);
		/*
		 * We cannot make this fatal. Sometimes this comes from magic
		 * spaces our resource handlers simply don't know about. Use
		 * the memory resource as-is and try to map that instead.
		 */
		mem = res;
	}

	drm_dbg(dev, "using memory resource at %pr\n", mem);

	aperture = DEFINE_RES_MEM(address, size);
	if (!resource_contains(mem, &aperture)) {
		drm_err(dev, "framebuffer aperture at invalid memory range %pr\n", &aperture);
		return -EINVAL;
	}

	ret = devm_aperture_acquire_for_platform_device(pdev, address, size);
	if (ret) {
		drm_err(dev, "could not acquire framebuffer aperture: %d\n", ret);
		return ret;
	}

	screen_base = devm_ioremap_wc(&pdev->dev, address, size);
	if (!screen_base)
		return -ENOMEM;

	iosys_map_set_vaddr_iomem(&sysfb->fb_addr, screen_base);

	/*
	 * DRM mode setting and registration
	 */

	ret = corebootdrm_mode_config_init(cdev, orientation);
	if (ret)
		return ret;

	drm_mode_config_reset(dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, sysfb->fb_format);

	return 0;
}

static void corebootdrm_remove(struct platform_device *pdev)
{
	struct corebootdrm_device *cdev = platform_get_drvdata(pdev);
	struct drm_device *dev = &cdev->sysfb.dev;

	drm_dev_unplug(dev);
}

static struct platform_driver corebootdrm_platform_driver = {
	.driver = {
		.name = "coreboot-framebuffer",
	},
	.probe = corebootdrm_probe,
	.remove = corebootdrm_remove,
};

module_platform_driver(corebootdrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
