// SPDX-License-Identifier: GPL-2.0-only

#include <linux/aperture.h>
#include <linux/efi.h>
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

#define DRIVER_NAME	"efidrm"
#define DRIVER_DESC	"DRM driver for EFI platform devices"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static const struct drm_format_info *efidrm_get_format_si(struct drm_device *dev,
							  const struct screen_info *si)
{
	static const struct drm_sysfb_format formats[] = {
		{ PIXEL_FORMAT_XRGB1555, DRM_FORMAT_XRGB1555, },
		{ PIXEL_FORMAT_RGB565, DRM_FORMAT_RGB565, },
		{ PIXEL_FORMAT_RGB888, DRM_FORMAT_RGB888, },
		{ PIXEL_FORMAT_XRGB8888, DRM_FORMAT_XRGB8888, },
		{ PIXEL_FORMAT_XBGR8888, DRM_FORMAT_XBGR8888, },
		{ PIXEL_FORMAT_XRGB2101010, DRM_FORMAT_XRGB2101010, },
	};

	return drm_sysfb_get_format_si(dev, formats, ARRAY_SIZE(formats), si);
}

static u64 efidrm_get_mem_flags(struct drm_device *dev, resource_size_t start,
				resource_size_t len)
{
	u64 attribute = EFI_MEMORY_UC | EFI_MEMORY_WC |
			EFI_MEMORY_WT | EFI_MEMORY_WB;
	u64 mem_flags = EFI_MEMORY_WC | EFI_MEMORY_UC;
	resource_size_t end = start + len;
	efi_memory_desc_t md;
	u64 md_end;

	if (!efi_enabled(EFI_MEMMAP) || efi_mem_desc_lookup(start, &md))
		goto out;

	md_end = md.phys_addr + (md.num_pages << EFI_PAGE_SHIFT);
	if (end > md_end)
		goto out;

	attribute &= md.attribute;
	if (attribute) {
		mem_flags |= EFI_MEMORY_WT | EFI_MEMORY_WB;
		mem_flags &= attribute;
	}

out:
	return mem_flags;
}

/*
 * EFI device
 */

struct efidrm_device {
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

static const u64 efidrm_primary_plane_format_modifiers[] = {
	DRM_SYSFB_PLANE_FORMAT_MODIFIERS,
};

static const struct drm_plane_helper_funcs efidrm_primary_plane_helper_funcs = {
	DRM_SYSFB_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs efidrm_primary_plane_funcs = {
	DRM_SYSFB_PLANE_FUNCS,
	.destroy = drm_plane_cleanup,
};

static const struct drm_crtc_helper_funcs efidrm_crtc_helper_funcs = {
	DRM_SYSFB_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs efidrm_crtc_funcs = {
	DRM_SYSFB_CRTC_FUNCS,
	.destroy = drm_crtc_cleanup,
};

static const struct drm_encoder_funcs efidrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs efidrm_connector_helper_funcs = {
	DRM_SYSFB_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs efidrm_connector_funcs = {
	DRM_SYSFB_CONNECTOR_FUNCS,
	.destroy = drm_connector_cleanup,
};

static const struct drm_mode_config_funcs efidrm_mode_config_funcs = {
	DRM_SYSFB_MODE_CONFIG_FUNCS,
};

/*
 * Init / Cleanup
 */

static struct efidrm_device *efidrm_device_create(struct drm_driver *drv,
						  struct platform_device *pdev)
{
	const struct screen_info *si;
	const struct drm_format_info *format;
	int width, height, stride;
	u64 vsize, mem_flags;
	struct resource resbuf;
	struct resource *res;
	struct efidrm_device *efi;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	struct resource *mem = NULL;
	void __iomem *screen_base = NULL;
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
	if (screen_info_video_type(si) != VIDEO_TYPE_EFI)
		return ERR_PTR(-ENODEV);

	/*
	 * EFI DRM driver
	 */

	efi = devm_drm_dev_alloc(&pdev->dev, drv, struct efidrm_device, sysfb.dev);
	if (IS_ERR(efi))
		return ERR_CAST(efi);
	sysfb = &efi->sysfb;
	dev = &sysfb->dev;
	platform_set_drvdata(pdev, dev);

	/*
	 * Hardware settings
	 */

	format = efidrm_get_format_si(dev, si);
	if (!format)
		return ERR_PTR(-EINVAL);
	width = drm_sysfb_get_width_si(dev, si);
	if (width < 0)
		return ERR_PTR(width);
	height = drm_sysfb_get_height_si(dev, si);
	if (height < 0)
		return ERR_PTR(height);
	res = drm_sysfb_get_memory_si(dev, si, &resbuf);
	if (!res)
		return ERR_PTR(-EINVAL);
	stride = drm_sysfb_get_stride_si(dev, si, format, width, height, resource_size(res));
	if (stride < 0)
		return ERR_PTR(stride);
	vsize = drm_sysfb_get_visible_size_si(dev, si, height, stride, resource_size(res));
	if (!vsize)
		return ERR_PTR(-EINVAL);

	drm_dbg(dev, "framebuffer format=%p4cc, size=%dx%d, stride=%d bytes\n",
		&format->format, width, height, stride);

#if defined(CONFIG_FIRMWARE_EDID)
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

	mem_flags = efidrm_get_mem_flags(dev, res->start, vsize);

	if (mem_flags & EFI_MEMORY_WC)
		screen_base = devm_ioremap_wc(&pdev->dev, mem->start, resource_size(mem));
	else if (mem_flags & EFI_MEMORY_UC)
		screen_base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	else if (mem_flags & EFI_MEMORY_WT)
		screen_base = devm_memremap(&pdev->dev, mem->start, resource_size(mem),
					    MEMREMAP_WT);
	else if (mem_flags & EFI_MEMORY_WB)
		screen_base = devm_memremap(&pdev->dev, mem->start, resource_size(mem),
					    MEMREMAP_WB);
	else
		drm_err(dev, "invalid mem_flags: 0x%llx\n", mem_flags);
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
	dev->mode_config.funcs = &efidrm_mode_config_funcs;

	/* Primary plane */

	nformats = drm_sysfb_build_fourcc_list(dev, &format->format, 1,
					       efi->formats, ARRAY_SIZE(efi->formats));

	primary_plane = &efi->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0, &efidrm_primary_plane_funcs,
				       efi->formats, nformats,
				       efidrm_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_plane_helper_add(primary_plane, &efidrm_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	/* CRTC */

	crtc = &efi->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&efidrm_crtc_funcs, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_crtc_helper_add(crtc, &efidrm_crtc_helper_funcs);

	/* Encoder */

	encoder = &efi->encoder;
	ret = drm_encoder_init(dev, encoder, &efidrm_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ERR_PTR(ret);
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* Connector */

	connector = &efi->connector;
	ret = drm_connector_init(dev, connector, &efidrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ERR_PTR(ret);
	drm_connector_helper_add(connector, &efidrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       width, height);
	if (sysfb->edid)
		drm_connector_attach_edid_property(connector);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return efi;
}

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(efidrm_fops);

static struct drm_driver efidrm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &efidrm_fops,
};

/*
 * Platform driver
 */

static int efidrm_probe(struct platform_device *pdev)
{
	struct efidrm_device *efi;
	struct drm_sysfb_device *sysfb;
	struct drm_device *dev;
	int ret;

	efi = efidrm_device_create(&efidrm_driver, pdev);
	if (IS_ERR(efi))
		return PTR_ERR(efi);
	sysfb = &efi->sysfb;
	dev = &sysfb->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, sysfb->fb_format);

	return 0;
}

static void efidrm_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);

	drm_dev_unplug(dev);
}

static struct platform_driver efidrm_platform_driver = {
	.driver = {
		.name = "efi-framebuffer",
	},
	.probe = efidrm_probe,
	.remove = efidrm_remove,
};

module_platform_driver(efidrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
