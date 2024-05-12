// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/of_clk.h>
#include <linux/minmax.h>
#include <linux/of_address.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>

#define DRIVER_NAME	"simpledrm"
#define DRIVER_DESC	"DRM driver for simple-framebuffer platform devices"
#define DRIVER_DATE	"20200625"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

/*
 * Helpers for simplefb
 */

static int
simplefb_get_validated_int(struct drm_device *dev, const char *name,
			   uint32_t value)
{
	if (value > INT_MAX) {
		drm_err(dev, "simplefb: invalid framebuffer %s of %u\n",
			name, value);
		return -EINVAL;
	}
	return (int)value;
}

static int
simplefb_get_validated_int0(struct drm_device *dev, const char *name,
			    uint32_t value)
{
	if (!value) {
		drm_err(dev, "simplefb: invalid framebuffer %s of %u\n",
			name, value);
		return -EINVAL;
	}
	return simplefb_get_validated_int(dev, name, value);
}

static const struct drm_format_info *
simplefb_get_validated_format(struct drm_device *dev, const char *format_name)
{
	static const struct simplefb_format formats[] = SIMPLEFB_FORMATS;
	const struct simplefb_format *fmt = formats;
	const struct simplefb_format *end = fmt + ARRAY_SIZE(formats);
	const struct drm_format_info *info;

	if (!format_name) {
		drm_err(dev, "simplefb: missing framebuffer format\n");
		return ERR_PTR(-EINVAL);
	}

	while (fmt < end) {
		if (!strcmp(format_name, fmt->name)) {
			info = drm_format_info(fmt->fourcc);
			if (!info)
				return ERR_PTR(-EINVAL);
			return info;
		}
		++fmt;
	}

	drm_err(dev, "simplefb: unknown framebuffer format %s\n",
		format_name);

	return ERR_PTR(-EINVAL);
}

static int
simplefb_get_width_pd(struct drm_device *dev,
		      const struct simplefb_platform_data *pd)
{
	return simplefb_get_validated_int0(dev, "width", pd->width);
}

static int
simplefb_get_height_pd(struct drm_device *dev,
		       const struct simplefb_platform_data *pd)
{
	return simplefb_get_validated_int0(dev, "height", pd->height);
}

static int
simplefb_get_stride_pd(struct drm_device *dev,
		       const struct simplefb_platform_data *pd)
{
	return simplefb_get_validated_int(dev, "stride", pd->stride);
}

static const struct drm_format_info *
simplefb_get_format_pd(struct drm_device *dev,
		       const struct simplefb_platform_data *pd)
{
	return simplefb_get_validated_format(dev, pd->format);
}

static int
simplefb_read_u32_of(struct drm_device *dev, struct device_node *of_node,
		     const char *name, u32 *value)
{
	int ret = of_property_read_u32(of_node, name, value);

	if (ret)
		drm_err(dev, "simplefb: cannot parse framebuffer %s: error %d\n",
			name, ret);
	return ret;
}

static int
simplefb_read_string_of(struct drm_device *dev, struct device_node *of_node,
			const char *name, const char **value)
{
	int ret = of_property_read_string(of_node, name, value);

	if (ret)
		drm_err(dev, "simplefb: cannot parse framebuffer %s: error %d\n",
			name, ret);
	return ret;
}

static int
simplefb_get_width_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 width;
	int ret = simplefb_read_u32_of(dev, of_node, "width", &width);

	if (ret)
		return ret;
	return simplefb_get_validated_int0(dev, "width", width);
}

static int
simplefb_get_height_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 height;
	int ret = simplefb_read_u32_of(dev, of_node, "height", &height);

	if (ret)
		return ret;
	return simplefb_get_validated_int0(dev, "height", height);
}

static int
simplefb_get_stride_of(struct drm_device *dev, struct device_node *of_node)
{
	u32 stride;
	int ret = simplefb_read_u32_of(dev, of_node, "stride", &stride);

	if (ret)
		return ret;
	return simplefb_get_validated_int(dev, "stride", stride);
}

static const struct drm_format_info *
simplefb_get_format_of(struct drm_device *dev, struct device_node *of_node)
{
	const char *format;
	int ret = simplefb_read_string_of(dev, of_node, "format", &format);

	if (ret)
		return ERR_PTR(ret);
	return simplefb_get_validated_format(dev, format);
}

static struct resource *
simplefb_get_memory_of(struct drm_device *dev, struct device_node *of_node)
{
	struct device_node *np;
	struct resource *res;
	int err;

	np = of_parse_phandle(of_node, "memory-region", 0);
	if (!np)
		return NULL;

	res = devm_kzalloc(dev->dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	err = of_address_to_resource(np, 0, res);
	if (err)
		return ERR_PTR(err);

	if (of_property_present(of_node, "reg"))
		drm_warn(dev, "preferring \"memory-region\" over \"reg\" property\n");

	return res;
}

/*
 * Simple Framebuffer device
 */

struct simpledrm_device {
	struct drm_device dev;

	/* clocks */
#if defined CONFIG_OF && defined CONFIG_COMMON_CLK
	unsigned int clk_count;
	struct clk **clks;
#endif
	/* regulators */
#if defined CONFIG_OF && defined CONFIG_REGULATOR
	unsigned int regulator_count;
	struct regulator **regulators;
#endif

	/* simplefb settings */
	struct drm_display_mode mode;
	const struct drm_format_info *format;
	unsigned int pitch;

	/* memory management */
	struct iosys_map screen_base;

	/* modesetting */
	uint32_t formats[8];
	size_t nformats;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static struct simpledrm_device *simpledrm_device_of_dev(struct drm_device *dev)
{
	return container_of(dev, struct simpledrm_device, dev);
}

/*
 * Hardware
 */

#if defined CONFIG_OF && defined CONFIG_COMMON_CLK
/*
 * Clock handling code.
 *
 * Here we handle the clocks property of our "simple-framebuffer" dt node.
 * This is necessary so that we can make sure that any clocks needed by
 * the display engine that the bootloader set up for us (and for which it
 * provided a simplefb dt node), stay up, for the life of the simplefb
 * driver.
 *
 * When the driver unloads, we cleanly disable, and then release the clocks.
 *
 * We only complain about errors here, no action is taken as the most likely
 * error can only happen due to a mismatch between the bootloader which set
 * up simplefb, and the clock definitions in the device tree. Chances are
 * that there are no adverse effects, and if there are, a clean teardown of
 * the fb probe will not help us much either. So just complain and carry on,
 * and hope that the user actually gets a working fb at the end of things.
 */

static void simpledrm_device_release_clocks(void *res)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(res);
	unsigned int i;

	for (i = 0; i < sdev->clk_count; ++i) {
		if (sdev->clks[i]) {
			clk_disable_unprepare(sdev->clks[i]);
			clk_put(sdev->clks[i]);
		}
	}
}

static int simpledrm_device_init_clocks(struct simpledrm_device *sdev)
{
	struct drm_device *dev = &sdev->dev;
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct device_node *of_node = pdev->dev.of_node;
	struct clk *clock;
	unsigned int i;
	int ret;

	if (dev_get_platdata(&pdev->dev) || !of_node)
		return 0;

	sdev->clk_count = of_clk_get_parent_count(of_node);
	if (!sdev->clk_count)
		return 0;

	sdev->clks = drmm_kzalloc(dev, sdev->clk_count * sizeof(sdev->clks[0]),
				  GFP_KERNEL);
	if (!sdev->clks)
		return -ENOMEM;

	for (i = 0; i < sdev->clk_count; ++i) {
		clock = of_clk_get(of_node, i);
		if (IS_ERR(clock)) {
			ret = PTR_ERR(clock);
			if (ret == -EPROBE_DEFER)
				goto err;
			drm_err(dev, "clock %u not found: %d\n", i, ret);
			continue;
		}
		ret = clk_prepare_enable(clock);
		if (ret) {
			drm_err(dev, "failed to enable clock %u: %d\n",
				i, ret);
			clk_put(clock);
			continue;
		}
		sdev->clks[i] = clock;
	}

	return devm_add_action_or_reset(&pdev->dev,
					simpledrm_device_release_clocks,
					sdev);

err:
	while (i) {
		--i;
		if (sdev->clks[i]) {
			clk_disable_unprepare(sdev->clks[i]);
			clk_put(sdev->clks[i]);
		}
	}
	return ret;
}
#else
static int simpledrm_device_init_clocks(struct simpledrm_device *sdev)
{
	return 0;
}
#endif

#if defined CONFIG_OF && defined CONFIG_REGULATOR

#define SUPPLY_SUFFIX "-supply"

/*
 * Regulator handling code.
 *
 * Here we handle the num-supplies and vin*-supply properties of our
 * "simple-framebuffer" dt node. This is necessary so that we can make sure
 * that any regulators needed by the display hardware that the bootloader
 * set up for us (and for which it provided a simplefb dt node), stay up,
 * for the life of the simplefb driver.
 *
 * When the driver unloads, we cleanly disable, and then release the
 * regulators.
 *
 * We only complain about errors here, no action is taken as the most likely
 * error can only happen due to a mismatch between the bootloader which set
 * up simplefb, and the regulator definitions in the device tree. Chances are
 * that there are no adverse effects, and if there are, a clean teardown of
 * the fb probe will not help us much either. So just complain and carry on,
 * and hope that the user actually gets a working fb at the end of things.
 */

static void simpledrm_device_release_regulators(void *res)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(res);
	unsigned int i;

	for (i = 0; i < sdev->regulator_count; ++i) {
		if (sdev->regulators[i]) {
			regulator_disable(sdev->regulators[i]);
			regulator_put(sdev->regulators[i]);
		}
	}
}

static int simpledrm_device_init_regulators(struct simpledrm_device *sdev)
{
	struct drm_device *dev = &sdev->dev;
	struct platform_device *pdev = to_platform_device(dev->dev);
	struct device_node *of_node = pdev->dev.of_node;
	struct property *prop;
	struct regulator *regulator;
	const char *p;
	unsigned int count = 0, i = 0;
	int ret;

	if (dev_get_platdata(&pdev->dev) || !of_node)
		return 0;

	/* Count the number of regulator supplies */
	for_each_property_of_node(of_node, prop) {
		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (p && p != prop->name)
			++count;
	}

	if (!count)
		return 0;

	sdev->regulators = drmm_kzalloc(dev,
					count * sizeof(sdev->regulators[0]),
					GFP_KERNEL);
	if (!sdev->regulators)
		return -ENOMEM;

	for_each_property_of_node(of_node, prop) {
		char name[32]; /* 32 is max size of property name */
		size_t len;

		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (!p || p == prop->name)
			continue;
		len = strlen(prop->name) - strlen(SUPPLY_SUFFIX) + 1;
		strscpy(name, prop->name, min(sizeof(name), len));

		regulator = regulator_get_optional(&pdev->dev, name);
		if (IS_ERR(regulator)) {
			ret = PTR_ERR(regulator);
			if (ret == -EPROBE_DEFER)
				goto err;
			drm_err(dev, "regulator %s not found: %d\n",
				name, ret);
			continue;
		}

		ret = regulator_enable(regulator);
		if (ret) {
			drm_err(dev, "failed to enable regulator %u: %d\n",
				i, ret);
			regulator_put(regulator);
			continue;
		}

		sdev->regulators[i++] = regulator;
	}
	sdev->regulator_count = i;

	return devm_add_action_or_reset(&pdev->dev,
					simpledrm_device_release_regulators,
					sdev);

err:
	while (i) {
		--i;
		if (sdev->regulators[i]) {
			regulator_disable(sdev->regulators[i]);
			regulator_put(sdev->regulators[i]);
		}
	}
	return ret;
}
#else
static int simpledrm_device_init_regulators(struct simpledrm_device *sdev)
{
	return 0;
}
#endif

/*
 * Modesetting
 */

static const uint64_t simpledrm_primary_plane_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static void simpledrm_primary_plane_helper_atomic_update(struct drm_plane *plane,
							 struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_device *dev = plane->dev;
	struct simpledrm_device *sdev = simpledrm_device_of_dev(dev);
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	int ret, idx;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		return;

	if (!drm_dev_enter(dev, &idx))
		goto out_drm_gem_fb_end_cpu_access;

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		struct drm_rect dst_clip = plane_state->dst;
		struct iosys_map dst = sdev->screen_base;

		if (!drm_rect_intersect(&dst_clip, &damage))
			continue;

		iosys_map_incr(&dst, drm_fb_clip_offset(sdev->pitch, sdev->format, &dst_clip));
		drm_fb_blit(&dst, &sdev->pitch, sdev->format->format, shadow_plane_state->data,
			    fb, &damage);
	}

	drm_dev_exit(idx);
out_drm_gem_fb_end_cpu_access:
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
}

static void simpledrm_primary_plane_helper_atomic_disable(struct drm_plane *plane,
							  struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct simpledrm_device *sdev = simpledrm_device_of_dev(dev);
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	/* Clear screen to black if disabled */
	iosys_map_memset(&sdev->screen_base, 0, 0, sdev->pitch * sdev->mode.vdisplay);

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs simpledrm_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = drm_plane_helper_atomic_check,
	.atomic_update = simpledrm_primary_plane_helper_atomic_update,
	.atomic_disable = simpledrm_primary_plane_helper_atomic_disable,
};

static const struct drm_plane_funcs simpledrm_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static enum drm_mode_status simpledrm_crtc_helper_mode_valid(struct drm_crtc *crtc,
							     const struct drm_display_mode *mode)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(crtc->dev);

	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &sdev->mode);
}

/*
 * The CRTC is always enabled. Screen updates are performed by
 * the primary plane's atomic_update function. Disabling clears
 * the screen in the primary plane's atomic_disable function.
 */
static const struct drm_crtc_helper_funcs simpledrm_crtc_helper_funcs = {
	.mode_valid = simpledrm_crtc_helper_mode_valid,
	.atomic_check = drm_crtc_helper_atomic_check,
};

static const struct drm_crtc_funcs simpledrm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs simpledrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int simpledrm_connector_helper_get_modes(struct drm_connector *connector)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &sdev->mode);
}

static const struct drm_connector_helper_funcs simpledrm_connector_helper_funcs = {
	.get_modes = simpledrm_connector_helper_get_modes,
};

static const struct drm_connector_funcs simpledrm_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs simpledrm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

/*
 * Init / Cleanup
 */

static struct drm_display_mode simpledrm_mode(unsigned int width,
					      unsigned int height,
					      unsigned int width_mm,
					      unsigned int height_mm)
{
	const struct drm_display_mode mode = {
		DRM_MODE_INIT(60, width, height, width_mm, height_mm)
	};

	return mode;
}

static struct simpledrm_device *simpledrm_device_create(struct drm_driver *drv,
							struct platform_device *pdev)
{
	const struct simplefb_platform_data *pd = dev_get_platdata(&pdev->dev);
	struct device_node *of_node = pdev->dev.of_node;
	struct simpledrm_device *sdev;
	struct drm_device *dev;
	int width, height, stride;
	int width_mm = 0, height_mm = 0;
	struct device_node *panel_node;
	const struct drm_format_info *format;
	struct resource *res, *mem = NULL;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	unsigned long max_width, max_height;
	size_t nformats;
	int ret;

	sdev = devm_drm_dev_alloc(&pdev->dev, drv, struct simpledrm_device, dev);
	if (IS_ERR(sdev))
		return ERR_CAST(sdev);
	dev = &sdev->dev;
	platform_set_drvdata(pdev, sdev);

	/*
	 * Hardware settings
	 */

	ret = simpledrm_device_init_clocks(sdev);
	if (ret)
		return ERR_PTR(ret);
	ret = simpledrm_device_init_regulators(sdev);
	if (ret)
		return ERR_PTR(ret);

	if (pd) {
		width = simplefb_get_width_pd(dev, pd);
		if (width < 0)
			return ERR_PTR(width);
		height = simplefb_get_height_pd(dev, pd);
		if (height < 0)
			return ERR_PTR(height);
		stride = simplefb_get_stride_pd(dev, pd);
		if (stride < 0)
			return ERR_PTR(stride);
		format = simplefb_get_format_pd(dev, pd);
		if (IS_ERR(format))
			return ERR_CAST(format);
	} else if (of_node) {
		width = simplefb_get_width_of(dev, of_node);
		if (width < 0)
			return ERR_PTR(width);
		height = simplefb_get_height_of(dev, of_node);
		if (height < 0)
			return ERR_PTR(height);
		stride = simplefb_get_stride_of(dev, of_node);
		if (stride < 0)
			return ERR_PTR(stride);
		format = simplefb_get_format_of(dev, of_node);
		if (IS_ERR(format))
			return ERR_CAST(format);
		mem = simplefb_get_memory_of(dev, of_node);
		if (IS_ERR(mem))
			return ERR_CAST(mem);
		panel_node = of_parse_phandle(of_node, "panel", 0);
		if (panel_node) {
			simplefb_read_u32_of(dev, panel_node, "width-mm", &width_mm);
			simplefb_read_u32_of(dev, panel_node, "height-mm", &height_mm);
			of_node_put(panel_node);
		}
	} else {
		drm_err(dev, "no simplefb configuration found\n");
		return ERR_PTR(-ENODEV);
	}
	if (!stride) {
		stride = drm_format_info_min_pitch(format, 0, width);
		if (drm_WARN_ON(dev, !stride))
			return ERR_PTR(-EINVAL);
	}

	/*
	 * Assume a monitor resolution of 96 dpi if physical dimensions
	 * are not specified to get a somewhat reasonable screen size.
	 */
	if (!width_mm)
		width_mm = DRM_MODE_RES_MM(width, 96ul);
	if (!height_mm)
		height_mm = DRM_MODE_RES_MM(height, 96ul);

	sdev->mode = simpledrm_mode(width, height, width_mm, height_mm);
	sdev->format = format;
	sdev->pitch = stride;

	drm_dbg(dev, "display mode={" DRM_MODE_FMT "}\n", DRM_MODE_ARG(&sdev->mode));
	drm_dbg(dev, "framebuffer format=%p4cc, size=%dx%d, stride=%d byte\n",
		&format->format, width, height, stride);

	/*
	 * Memory management
	 */

	if (mem) {
		void *screen_base;

		ret = devm_aperture_acquire_from_firmware(dev, mem->start, resource_size(mem));
		if (ret) {
			drm_err(dev, "could not acquire memory range %pr: %d\n", mem, ret);
			return ERR_PTR(ret);
		}

		drm_dbg(dev, "using system memory framebuffer at %pr\n", mem);

		screen_base = devm_memremap(dev->dev, mem->start, resource_size(mem), MEMREMAP_WC);
		if (IS_ERR(screen_base))
			return screen_base;

		iosys_map_set_vaddr(&sdev->screen_base, screen_base);
	} else {
		void __iomem *screen_base;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return ERR_PTR(-EINVAL);

		ret = devm_aperture_acquire_from_firmware(dev, res->start, resource_size(res));
		if (ret) {
			drm_err(dev, "could not acquire memory range %pr: %d\n", &res, ret);
			return ERR_PTR(ret);
		}

		drm_dbg(dev, "using I/O memory framebuffer at %pr\n", res);

		mem = devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
					      drv->name);
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

		iosys_map_set_vaddr_iomem(&sdev->screen_base, screen_base);
	}

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
	dev->mode_config.funcs = &simpledrm_mode_config_funcs;

	/* Primary plane */

	nformats = drm_fb_build_fourcc_list(dev, &format->format, 1,
					    sdev->formats, ARRAY_SIZE(sdev->formats));

	primary_plane = &sdev->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0, &simpledrm_primary_plane_funcs,
				       sdev->formats, nformats,
				       simpledrm_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_plane_helper_add(primary_plane, &simpledrm_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	/* CRTC */

	crtc = &sdev->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&simpledrm_crtc_funcs, NULL);
	if (ret)
		return ERR_PTR(ret);
	drm_crtc_helper_add(crtc, &simpledrm_crtc_helper_funcs);

	/* Encoder */

	encoder = &sdev->encoder;
	ret = drm_encoder_init(dev, encoder, &simpledrm_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ERR_PTR(ret);
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	/* Connector */

	connector = &sdev->connector;
	ret = drm_connector_init(dev, connector, &simpledrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ERR_PTR(ret);
	drm_connector_helper_add(connector, &simpledrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       width, height);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return sdev;
}

/*
 * DRM driver
 */

DEFINE_DRM_GEM_FOPS(simpledrm_fops);

static struct drm_driver simpledrm_driver = {
	DRM_GEM_SHMEM_DRIVER_OPS,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.driver_features	= DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,
	.fops			= &simpledrm_fops,
};

/*
 * Platform driver
 */

static int simpledrm_probe(struct platform_device *pdev)
{
	struct simpledrm_device *sdev;
	struct drm_device *dev;
	unsigned int color_mode;
	int ret;

	sdev = simpledrm_device_create(&simpledrm_driver, pdev);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);
	dev = &sdev->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	color_mode = drm_format_info_bpp(sdev->format, 0);
	if (color_mode == 16)
		color_mode = sdev->format->depth; // can be 15 or 16

	drm_fbdev_generic_setup(dev, color_mode);

	return 0;
}

static int simpledrm_remove(struct platform_device *pdev)
{
	struct simpledrm_device *sdev = platform_get_drvdata(pdev);
	struct drm_device *dev = &sdev->dev;

	drm_dev_unplug(dev);

	return 0;
}

static const struct of_device_id simpledrm_of_match_table[] = {
	{ .compatible = "simple-framebuffer", },
	{ },
};
MODULE_DEVICE_TABLE(of, simpledrm_of_match_table);

static struct platform_driver simpledrm_platform_driver = {
	.driver = {
		.name = "simple-framebuffer", /* connect to sysfb */
		.of_match_table = simpledrm_of_match_table,
	},
	.probe = simpledrm_probe,
	.remove = simpledrm_remove,
};

module_platform_driver(simpledrm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
