// SPDX-License-Identifier: GPL-2.0-only

#include <linux/aperture.h>
#include <linux/ioport.h>
#include <linux/limits.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include <video/edid.h>
#include <video/pixel_format.h>

#include "drm_sysfb_helper.h"

#define DRIVER_NAME	"vesadrm"
#define DRIVER_DESC	"DRM driver for VESA platform devices"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static int vesadrm_get_validated_int(struct drm_device *dev, const char *name,
				     u64 value, u32 max)
{
	if (max > INT_MAX)
		max = INT_MAX;
	if (value > max) {
		drm_err(dev, "%s of %llu exceeds maximum of %u\n", name, value, max);
		return -EINVAL;
	}
	return value;
}

static int vesadrm_get_validated_int0(struct drm_device *dev, const char *name,
				      u64 value, u32 max)
{
	if (!value) {
		drm_err(dev, "%s of 0 not allowed\n", name);
		return -EINVAL;
	}
	return vesadrm_get_validated_int(dev, name, value, max);
}

static s64 vesadrm_get_validated_size0(struct drm_device *dev, const char *name,
				       u64 value, u64 max)
{
	if (!value) {
		drm_err(dev, "vesadrm: %s of 0 not allowed\n", name);
		return -EINVAL;
	} else if (value > max) {
		drm_err(dev, "vesadrm: %s of %llu exceeds maximum of %llu\n", name, value, max);
		return -EINVAL;
	}
	return value;
}

static int vesadrm_get_width_si(struct drm_device *dev, const struct screen_info *si)
{
	return vesadrm_get_validated_int0(dev, "width", si->lfb_width, U16_MAX);
}

static int vesadrm_get_height_si(struct drm_device *dev, const struct screen_info *si)
{
	return vesadrm_get_validated_int0(dev, "height", si->lfb_height, U16_MAX);
}

static struct resource *vesadrm_get_memory_si(struct drm_device *dev,
					      const struct screen_info *si,
					      struct resource *res)
{
	ssize_t	num;

	num = screen_info_resources(si, res, 1);
	if (!num) {
		drm_err(dev, "vesadrm: memory resource not found\n");
		return NULL;
	}

	return res;
}

static int vesadrm_get_stride_si(struct drm_device *dev, const struct screen_info *si,
				 const struct drm_format_info *format,
				 unsigned int width, unsigned int height, u64 size)
{
	u64 lfb_linelength = si->lfb_linelength;

	if (!lfb_linelength)
		lfb_linelength = drm_format_info_min_pitch(format, 0, width);

	return vesadrm_get_validated_int0(dev, "stride", lfb_linelength, div64_u64(size, height));
}

static u64 vesadrm_get_visible_size_si(struct drm_device *dev, const struct screen_info *si,
				       unsigned int height, unsigned int stride, u64 size)
{
	u64 vsize = PAGE_ALIGN(height * stride);

	return vesadrm_get_validated_size0(dev, "visible size", vsize, size);
}

static const struct drm_format_info *vesadrm_get_format_si(struct drm_device *dev,
							   const struct screen_info *si)
{
	static const struct {
		struct pixel_format pixel;
		u32 fourcc;
	} vesa_formats[] = {
		{ PIXEL_FORMAT_XRGB1555, DRM_FORMAT_XRGB1555, },
		{ PIXEL_FORMAT_RGB565, DRM_FORMAT_RGB565, },
		{ PIXEL_FORMAT_RGB888, DRM_FORMAT_RGB888, },
		{ PIXEL_FORMAT_XRGB8888, DRM_FORMAT_XRGB8888, },
		{ PIXEL_FORMAT_XBGR8888, DRM_FORMAT_XBGR8888, },
	};
	const struct drm_format_info *format = NULL;
	u32 bits_per_pixel;
	size_t i;

	bits_per_pixel = __screen_info_lfb_bits_per_pixel(si);

	for (i = 0; i < ARRAY_SIZE(vesa_formats); ++i) {
		const struct pixel_format *f = &vesa_formats[i].pixel;

		if (bits_per_pixel == f->bits_per_pixel &&
		    si->red_size == f->red.length &&
		    si->red_pos == f->red.offset &&
		    si->green_size == f->green.length &&
		    si->green_pos == f->green.offset &&
		    si->blue_size == f->blue.length &&
		    si->blue_pos == f->blue.offset) {
			format = drm_format_info(vesa_formats[i].fourcc);
			break;
		}
	}

	if (!format)
		return ERR_PTR(-EINVAL);
	if (format->is_color_indexed)
		return ERR_PTR(-EINVAL);

	return format;
}

/*
 * VESA device
 */

struct vesadrm_device {
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

static const u64 vesadrm_primary_plane_format_modifiers[] = {
	DRM_SYSFB_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs vesadrm_primary_plane_helper_funcs = {
	DRM_SYSFB_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs vesadrm_primary_plane_funcs = {
	DRM_SYSFB_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static const struct drm_crtc_helper_funcs vesadrm_crtc_helper_funcs = {
	DRM_SYSFB_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs vesadrm_crtc_funcs = {
	DRM_SYSFB_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs vesadrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs vesadrm_connector_helper_funcs = {
	DRM_SYSFB_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs vesadrm_connector_funcs = {
	DRM_SYSFB_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_funcs vesadrm_mode_config_funcs = {
	DRM_SYSFB_MODE_CONFIG_FUNCS,
};

/*
 * Init / Cleanup
 */

static struct vesadrm_device *vesadrm_device_create(struct drm_driver *drv,
						    struct platform_device *pdev)
{
	const struct screen_info *si;
	const struct drm_format_info *format;
	int width, height, stride;
	u64 vsize;
	struct resource resbuf;
	struct resource *res;
	struct vesadrm_device *vesa;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	struct resource *mem = NULL;
	void __iomem *screen_base;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	unsigned long max_width, max_height;
	size_t nformats;
	int ret;

	si = dev_get_platdata(&pdev->dev);
	if (!si)
		return ERR_PTR(-ENODEV);
	if (screen_info_video_type(si) != VIDEO_TYPE_VLFB)
		return ERR_PTR(-ENODEV);

	/*
	 * VESA DRM driver
	 */

	vesa = devm_drm_dev_alloc(&pdev->dev, drv, struct vesadrm_device, sysfb.dev);
	if (IS_ERR(vesa))
		return ERR_CAST(vesa);
	sysfb = &vesa->sysfb;
	dev = &sysfb->dev;
	platform_set_drvdata(pdev, dev);

	/*
	 * Hardware settings
	 */

	format = vesadrm_get_format_si(dev, si);
	if (IS_ERR(format))
		return ERR_CAST(format);
	width = vesadrm_get_width_si(dev, si);
	if (width < 0)
		return ERR_PTR(width);
	height = vesadrm_get_height_si(dev, si);
	if (height < 0)
		return ERR_PTR(height);
	res = vesadrm_get_memory_si(dev, si, &resbuf);
	if (!res)
		return ERR_PTR(-EINVAL);
	stride = vesadrm_get_stride_si(dev, si, format, width, height, resource_size(res));
	if (stride < 0)
		return ERR_PTR(stride);
	vsize = vesadrm_get_visible_size_si(dev, si, height, stride, resource_size(res));
	if (!vsize)
		return ERR_PTR(-EINVAL);

	drm_dbg(dev, "framebuffer format=%p4cc, size=%dx%d, stride=%d bytes\n",
		&format->format, width, height, stride);

#ifdef CONFIG_X86
	if (drm_edid_header_is_valid(edid_info.dummy) == 8)
		sysfb->edid = edid_info.dummy;
#endif
	sysfb->fb_mode = drm_sysfb_mode(width, height, 0, 0);
	sysfb->fb_format = format;
	sysfb->fb_pitch = stride;

	/*
	 * Memory management
	 */

	ret = devm_aperture_acquire_for_platform_device(pdev, res->start, vsize);
	if (ret) {
		drm_err(dev, "could not acquire memory range %pr: %d\n", res, ret);
		return ERR_PTR(ret);
	}

	drm_dbg(dev, "using I/O memory framebuffer at %pr\n", res);

	mem = devm_request_mem_region(&pdev->dev, res->start, vsize, drv->name);
	if (!mem) {
		/*
		 * We cannot make this fatal. Sometimes this comes from magic
		 * spaces our resource handlers simply don't know about. Use
		 * the I/O-memory resource as-is and try to map that instead.
		 */
		drm_warn(dev, "could not acquire memory region %pr\n", res);
		mem = res;
	}

	screen_base = devm_ioremap_wc(&pdev->dev, mem->start, resource_size(mem));
	if (!screen_base)
		return ERR_PTR(-ENOMEM);
	iosys_map_set_vaddr_iomem(&sysfb->fb_addr, screen_base);

	/*
	 * Modesetting
	 */

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ERR_PTR(ret);

	max_width = max_t(unsigned long, width, DRM_SHADOW_PLANE_MAX_WIDTH);
	max_height = max_t(unsigned long, height, DRM_SHADOW_PLANE_MAX_HEIGHT);

	dev->mode_config.min_width = width;
	dev->mode_config.max_width = max_width;
	dev->mode_config.min_height = height;
	dev->mode_config.max_height = max_height;
	dev->mode_config.preferred_depth = format->depth;
	dev->mode_config.funcs = &vesadrm_mode_config_funcs;

	/* Primary plane */

	nformats = drm_fb_build_fourcc_list(dev, &format->format, 1,
					    vesa->formats, ARRAY_SIZE(vesa->formats));

	primary_plane = &vesa->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0, &vesadrm_primary_plane_funcs,
				       vesa->formats, nformats,
				       vesadrm_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_plane_helper_add(primary_plane, &vesadrm_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	/* CRTC */

	crtc = &vesa->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&vesadrm_crtc_funcs, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_crtc_helper_add(crtc, &vesadrm_crtc_helper_funcs);

	/* Encoder */

	encoder = &vesa->encoder;
	ret = drm_encoder_init(dev, encoder, &vesadrm_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ERR_PTR(ret);
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* Connector */

	connector = &vesa->connector;
	ret = drm_connector_init(dev, connector, &vesadrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ERR_PTR(ret);
	drm_connector_helper_add(connector, &vesadrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       width, height);
	if (sysfb->edid)
		drm_connector_attach_edid_property(connector);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return vesa;
}

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(vesadrm_fops);

static struct drm_driver vesadrm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &vesadrm_fops,
};

/*
 * Platform driver
 */

static int vesadrm_probe(struct platform_device *pdev)
{
	struct vesadrm_device *vesa;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	int ret;

	vesa = vesadrm_device_create(&vesadrm_driver, pdev);
	if (IS_ERR(vesa))
		return PTR_ERR(vesa);
	sysfb = &vesa->sysfb;
	dev = &sysfb->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, sysfb->fb_format);

	return 0;
}

static void vesadrm_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);

	drm_dev_unplug(dev);
}

static struct platform_driver vesadrm_platform_driver = {
	.driver = {
		.name = "vesa-framebuffer",
	},
	.probe = vesadrm_probe,
	.remove = vesadrm_remove,
};

module_platform_driver(vesadrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
