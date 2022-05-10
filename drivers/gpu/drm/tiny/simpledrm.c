// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/of_clk.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define DRIVER_NAME	"simpledrm"
#define DRIVER_DESC	"DRM driver for simple-framebuffer platform devices"
#define DRIVER_DATE	"20200625"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

/*
 * Assume a monitor resolution of 96 dpi to
 * get a somewhat reasonable screen size.
 */
#define RES_MM(d)	\
	(((d) * 254ul) / (96ul * 10ul))

#define SIMPLEDRM_MODE(hd, vd)	\
	DRM_SIMPLE_MODE(hd, vd, RES_MM(hd), RES_MM(vd))

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

/*
 * Simple Framebuffer device
 */

struct simpledrm_device {
	struct drm_device dev;
	struct platform_device *pdev;

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
	struct resource *mem;
	void __iomem *screen_base;

	/* modesetting */
	uint32_t formats[8];
	size_t nformats;
	struct drm_connector connector;
	struct drm_simple_display_pipe pipe;
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
	struct platform_device *pdev = sdev->pdev;
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
	struct platform_device *pdev = sdev->pdev;
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
 *  Simplefb settings
 */

static struct drm_display_mode simpledrm_mode(unsigned int width,
					      unsigned int height)
{
	struct drm_display_mode mode = { SIMPLEDRM_MODE(width, height) };

	mode.clock = mode.hdisplay * mode.vdisplay * 60 / 1000 /* kHz */;
	drm_mode_set_name(&mode);

	return mode;
}

static int simpledrm_device_init_fb(struct simpledrm_device *sdev)
{
	int width, height, stride;
	const struct drm_format_info *format;
	struct drm_device *dev = &sdev->dev;
	struct platform_device *pdev = sdev->pdev;
	const struct simplefb_platform_data *pd = dev_get_platdata(&pdev->dev);
	struct device_node *of_node = pdev->dev.of_node;

	if (pd) {
		width = simplefb_get_width_pd(dev, pd);
		if (width < 0)
			return width;
		height = simplefb_get_height_pd(dev, pd);
		if (height < 0)
			return height;
		stride = simplefb_get_stride_pd(dev, pd);
		if (stride < 0)
			return stride;
		format = simplefb_get_format_pd(dev, pd);
		if (IS_ERR(format))
			return PTR_ERR(format);
	} else if (of_node) {
		width = simplefb_get_width_of(dev, of_node);
		if (width < 0)
			return width;
		height = simplefb_get_height_of(dev, of_node);
		if (height < 0)
			return height;
		stride = simplefb_get_stride_of(dev, of_node);
		if (stride < 0)
			return stride;
		format = simplefb_get_format_of(dev, of_node);
		if (IS_ERR(format))
			return PTR_ERR(format);
	} else {
		drm_err(dev, "no simplefb configuration found\n");
		return -ENODEV;
	}

	sdev->mode = simpledrm_mode(width, height);
	sdev->format = format;
	sdev->pitch = stride;

	drm_dbg_kms(dev, "display mode={" DRM_MODE_FMT "}\n",
		    DRM_MODE_ARG(&sdev->mode));
	drm_dbg_kms(dev,
		    "framebuffer format=%p4cc, size=%dx%d, stride=%d byte\n",
		    &format->format, width, height, stride);

	return 0;
}

/*
 * Memory management
 */

static int simpledrm_device_init_mm(struct simpledrm_device *sdev)
{
	struct drm_device *dev = &sdev->dev;
	struct platform_device *pdev = sdev->pdev;
	struct resource *mem;
	void __iomem *screen_base;
	int ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	ret = devm_aperture_acquire_from_firmware(dev, mem->start, resource_size(mem));
	if (ret) {
		drm_err(dev, "could not acquire memory range %pr: error %d\n",
			mem, ret);
		return ret;
	}

	screen_base = devm_ioremap_wc(&pdev->dev, mem->start,
				      resource_size(mem));
	if (!screen_base)
		return -ENOMEM;

	sdev->mem = mem;
	sdev->screen_base = screen_base;

	return 0;
}

/*
 * Modesetting
 */

/*
 * Support all formats of simplefb and maybe more; in order
 * of preference. The display's update function will do any
 * conversion necessary.
 *
 * TODO: Add blit helpers for remaining formats and uncomment
 *       constants.
 */
static const uint32_t simpledrm_default_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	//DRM_FORMAT_XRGB1555,
	//DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB888,
	//DRM_FORMAT_XRGB2101010,
	//DRM_FORMAT_ARGB2101010,
};

static const uint64_t simpledrm_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static int simpledrm_connector_helper_get_modes(struct drm_connector *connector)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(connector->dev);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sdev->mode);
	if (!mode)
		return 0;

	if (mode->name[0] == '\0')
		drm_mode_set_name(mode);

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	if (mode->width_mm)
		connector->display_info.width_mm = mode->width_mm;
	if (mode->height_mm)
		connector->display_info.height_mm = mode->height_mm;

	return 1;
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

static int
simpledrm_simple_display_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
				    const struct drm_display_mode *mode)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(pipe->crtc.dev);

	if (mode->hdisplay != sdev->mode.hdisplay &&
	    mode->vdisplay != sdev->mode.vdisplay)
		return MODE_ONE_SIZE;
	else if (mode->hdisplay != sdev->mode.hdisplay)
		return MODE_ONE_WIDTH;
	else if (mode->vdisplay != sdev->mode.vdisplay)
		return MODE_ONE_HEIGHT;

	return MODE_OK;
}

static void
simpledrm_simple_display_pipe_enable(struct drm_simple_display_pipe *pipe,
				     struct drm_crtc_state *crtc_state,
				     struct drm_plane_state *plane_state)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	void *vmap = shadow_plane_state->data[0].vaddr; /* TODO: Use mapping abstraction */
	struct drm_device *dev = &sdev->dev;
	int idx;

	if (!fb)
		return;

	if (!drm_dev_enter(dev, &idx))
		return;

	drm_fb_blit_dstclip(sdev->screen_base, sdev->pitch,
			    sdev->format->format, vmap, fb);
	drm_dev_exit(idx);
}

static void
simpledrm_simple_display_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(pipe->crtc.dev);
	struct drm_device *dev = &sdev->dev;
	int idx;

	if (!drm_dev_enter(dev, &idx))
		return;

	/* Clear screen to black if disabled */
	memset_io(sdev->screen_base, 0, sdev->pitch * sdev->mode.vdisplay);

	drm_dev_exit(idx);
}

static void
simpledrm_simple_display_pipe_update(struct drm_simple_display_pipe *pipe,
				     struct drm_plane_state *old_plane_state)
{
	struct simpledrm_device *sdev = simpledrm_device_of_dev(pipe->crtc.dev);
	struct drm_plane_state *plane_state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	void *vmap = shadow_plane_state->data[0].vaddr; /* TODO: Use mapping abstraction */
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_device *dev = &sdev->dev;
	struct drm_rect clip;
	int idx;

	if (!fb)
		return;

	if (!drm_atomic_helper_damage_merged(old_plane_state, plane_state, &clip))
		return;

	if (!drm_dev_enter(dev, &idx))
		return;

	drm_fb_blit_rect_dstclip(sdev->screen_base, sdev->pitch,
				 sdev->format->format, vmap, fb, &clip);

	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs
simpledrm_simple_display_pipe_funcs = {
	.mode_valid = simpledrm_simple_display_pipe_mode_valid,
	.enable = simpledrm_simple_display_pipe_enable,
	.disable = simpledrm_simple_display_pipe_disable,
	.update = simpledrm_simple_display_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const struct drm_mode_config_funcs simpledrm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const uint32_t *simpledrm_device_formats(struct simpledrm_device *sdev,
						size_t *nformats_out)
{
	struct drm_device *dev = &sdev->dev;
	size_t i;

	if (sdev->nformats)
		goto out; /* don't rebuild list on recurring calls */

	/* native format goes first */
	sdev->formats[0] = sdev->format->format;
	sdev->nformats = 1;

	/* default formats go second */
	for (i = 0; i < ARRAY_SIZE(simpledrm_default_formats); ++i) {
		if (simpledrm_default_formats[i] == sdev->format->format)
			continue; /* native format already went first */
		sdev->formats[sdev->nformats] = simpledrm_default_formats[i];
		sdev->nformats++;
	}

	/*
	 * TODO: The simpledrm driver converts framebuffers to the native
	 * format when copying them to device memory. If there are more
	 * formats listed than supported by the driver, the native format
	 * is not supported by the conversion helpers. Therefore *only*
	 * support the native format and add a conversion helper ASAP.
	 */
	if (drm_WARN_ONCE(dev, i != sdev->nformats,
			  "format conversion helpers required for %p4cc",
			  &sdev->format->format)) {
		sdev->nformats = 1;
	}

out:
	*nformats_out = sdev->nformats;
	return sdev->formats;
}

static int simpledrm_device_init_modeset(struct simpledrm_device *sdev)
{
	struct drm_device *dev = &sdev->dev;
	struct drm_display_mode *mode = &sdev->mode;
	struct drm_connector *connector = &sdev->connector;
	struct drm_simple_display_pipe *pipe = &sdev->pipe;
	const uint32_t *formats;
	size_t nformats;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = mode->hdisplay;
	dev->mode_config.max_width = mode->hdisplay;
	dev->mode_config.min_height = mode->vdisplay;
	dev->mode_config.max_height = mode->vdisplay;
	dev->mode_config.prefer_shadow_fbdev = true;
	dev->mode_config.preferred_depth = sdev->format->cpp[0] * 8;
	dev->mode_config.funcs = &simpledrm_mode_config_funcs;

	ret = drm_connector_init(dev, connector, &simpledrm_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &simpledrm_connector_helper_funcs);
	drm_connector_set_panel_orientation_with_quirk(connector,
						       DRM_MODE_PANEL_ORIENTATION_UNKNOWN,
						       mode->hdisplay, mode->vdisplay);

	formats = simpledrm_device_formats(sdev, &nformats);

	ret = drm_simple_display_pipe_init(dev, pipe, &simpledrm_simple_display_pipe_funcs,
					   formats, nformats, simpledrm_format_modifiers,
					   connector);
	if (ret)
		return ret;

	drm_mode_config_reset(dev);

	return 0;
}

/*
 * Init / Cleanup
 */

static struct simpledrm_device *
simpledrm_device_create(struct drm_driver *drv, struct platform_device *pdev)
{
	struct simpledrm_device *sdev;
	int ret;

	sdev = devm_drm_dev_alloc(&pdev->dev, drv, struct simpledrm_device,
				  dev);
	if (IS_ERR(sdev))
		return ERR_CAST(sdev);
	sdev->pdev = pdev;
	platform_set_drvdata(pdev, sdev);

	ret = simpledrm_device_init_clocks(sdev);
	if (ret)
		return ERR_PTR(ret);
	ret = simpledrm_device_init_regulators(sdev);
	if (ret)
		return ERR_PTR(ret);
	ret = simpledrm_device_init_fb(sdev);
	if (ret)
		return ERR_PTR(ret);
	ret = simpledrm_device_init_mm(sdev);
	if (ret)
		return ERR_PTR(ret);
	ret = simpledrm_device_init_modeset(sdev);
	if (ret)
		return ERR_PTR(ret);

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
	int ret;

	sdev = simpledrm_device_create(&simpledrm_driver, pdev);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);
	dev = &sdev->dev;

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_fbdev_generic_setup(dev, 0);

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
