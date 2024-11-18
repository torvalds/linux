// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simplest possible simple frame-buffer driver, as a platform device
 *
 * Copyright (c) 2013, Stephen Warren
 *
 * Based on q40fb.c, which was:
 * Copyright (C) 2001 Richard Zidlicky <rz@linux-m68k.org>
 *
 * Also based on offb.c, which was:
 * Copyright (C) 1997 Geert Uytterhoeven
 * Copyright (C) 1996 Paul Mackerras
 */

#include <linux/aperture.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/parser.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>

static const struct fb_fix_screeninfo simplefb_fix = {
	.id		= "simple",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo simplefb_var = {
	.height		= -1,
	.width		= -1,
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

#define PSEUDO_PALETTE_SIZE 16

static int simplefb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value;

	if (regno >= PSEUDO_PALETTE_SIZE)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);
	if (info->var.transp.length > 0) {
		u32 mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}

struct simplefb_par {
	u32 palette[PSEUDO_PALETTE_SIZE];
	resource_size_t base;
	resource_size_t size;
	struct resource *mem;
#if defined CONFIG_OF && defined CONFIG_COMMON_CLK
	bool clks_enabled;
	unsigned int clk_count;
	struct clk **clks;
#endif
#if defined CONFIG_OF && defined CONFIG_PM_GENERIC_DOMAINS
	unsigned int num_genpds;
	struct device **genpds;
	struct device_link **genpd_links;
#endif
#if defined CONFIG_OF && defined CONFIG_REGULATOR
	bool regulators_enabled;
	u32 regulator_count;
	struct regulator **regulators;
#endif
};

static void simplefb_clocks_destroy(struct simplefb_par *par);
static void simplefb_regulators_destroy(struct simplefb_par *par);

/*
 * fb_ops.fb_destroy is called by the last put_fb_info() call at the end
 * of unregister_framebuffer() or fb_release(). Do any cleanup here.
 */
static void simplefb_destroy(struct fb_info *info)
{
	struct simplefb_par *par = info->par;
	struct resource *mem = par->mem;

	simplefb_regulators_destroy(info->par);
	simplefb_clocks_destroy(info->par);
	if (info->screen_base)
		iounmap(info->screen_base);

	framebuffer_release(info);

	if (mem)
		release_mem_region(mem->start, resource_size(mem));
}

static const struct fb_ops simplefb_ops = {
	.owner		= THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_destroy	= simplefb_destroy,
	.fb_setcolreg	= simplefb_setcolreg,
};

static struct simplefb_format simplefb_formats[] = SIMPLEFB_FORMATS;

struct simplefb_params {
	u32 width;
	u32 height;
	u32 stride;
	struct simplefb_format *format;
	struct resource memory;
};

static int simplefb_parse_dt(struct platform_device *pdev,
			   struct simplefb_params *params)
{
	struct device_node *np = pdev->dev.of_node, *mem;
	int ret;
	const char *format;
	int i;

	ret = of_property_read_u32(np, "width", &params->width);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse width property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "height", &params->height);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse height property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "stride", &params->stride);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse stride property\n");
		return ret;
	}

	ret = of_property_read_string(np, "format", &format);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse format property\n");
		return ret;
	}
	params->format = NULL;
	for (i = 0; i < ARRAY_SIZE(simplefb_formats); i++) {
		if (strcmp(format, simplefb_formats[i].name))
			continue;
		params->format = &simplefb_formats[i];
		break;
	}
	if (!params->format) {
		dev_err(&pdev->dev, "Invalid format value\n");
		return -EINVAL;
	}

	mem = of_parse_phandle(np, "memory-region", 0);
	if (mem) {
		ret = of_address_to_resource(mem, 0, &params->memory);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to parse memory-region\n");
			of_node_put(mem);
			return ret;
		}

		if (of_property_present(np, "reg"))
			dev_warn(&pdev->dev, "preferring \"memory-region\" over \"reg\" property\n");

		of_node_put(mem);
	} else {
		memset(&params->memory, 0, sizeof(params->memory));
	}

	return 0;
}

static int simplefb_parse_pd(struct platform_device *pdev,
			     struct simplefb_params *params)
{
	struct simplefb_platform_data *pd = dev_get_platdata(&pdev->dev);
	int i;

	params->width = pd->width;
	params->height = pd->height;
	params->stride = pd->stride;

	params->format = NULL;
	for (i = 0; i < ARRAY_SIZE(simplefb_formats); i++) {
		if (strcmp(pd->format, simplefb_formats[i].name))
			continue;

		params->format = &simplefb_formats[i];
		break;
	}

	if (!params->format) {
		dev_err(&pdev->dev, "Invalid format value\n");
		return -EINVAL;
	}

	memset(&params->memory, 0, sizeof(params->memory));

	return 0;
}

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
static int simplefb_clocks_get(struct simplefb_par *par,
			       struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk *clock;
	int i;

	if (dev_get_platdata(&pdev->dev) || !np)
		return 0;

	par->clk_count = of_clk_get_parent_count(np);
	if (!par->clk_count)
		return 0;

	par->clks = kcalloc(par->clk_count, sizeof(struct clk *), GFP_KERNEL);
	if (!par->clks)
		return -ENOMEM;

	for (i = 0; i < par->clk_count; i++) {
		clock = of_clk_get(np, i);
		if (IS_ERR(clock)) {
			if (PTR_ERR(clock) == -EPROBE_DEFER) {
				while (--i >= 0) {
					clk_put(par->clks[i]);
				}
				kfree(par->clks);
				return -EPROBE_DEFER;
			}
			dev_err(&pdev->dev, "%s: clock %d not found: %ld\n",
				__func__, i, PTR_ERR(clock));
			continue;
		}
		par->clks[i] = clock;
	}

	return 0;
}

static void simplefb_clocks_enable(struct simplefb_par *par,
				   struct platform_device *pdev)
{
	int i, ret;

	for (i = 0; i < par->clk_count; i++) {
		if (par->clks[i]) {
			ret = clk_prepare_enable(par->clks[i]);
			if (ret) {
				dev_err(&pdev->dev,
					"%s: failed to enable clock %d: %d\n",
					__func__, i, ret);
				clk_put(par->clks[i]);
				par->clks[i] = NULL;
			}
		}
	}
	par->clks_enabled = true;
}

static void simplefb_clocks_destroy(struct simplefb_par *par)
{
	int i;

	if (!par->clks)
		return;

	for (i = 0; i < par->clk_count; i++) {
		if (par->clks[i]) {
			if (par->clks_enabled)
				clk_disable_unprepare(par->clks[i]);
			clk_put(par->clks[i]);
		}
	}

	kfree(par->clks);
}
#else
static int simplefb_clocks_get(struct simplefb_par *par,
	struct platform_device *pdev) { return 0; }
static void simplefb_clocks_enable(struct simplefb_par *par,
	struct platform_device *pdev) { }
static void simplefb_clocks_destroy(struct simplefb_par *par) { }
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
static int simplefb_regulators_get(struct simplefb_par *par,
				   struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct property *prop;
	struct regulator *regulator;
	const char *p;
	int count = 0, i = 0;

	if (dev_get_platdata(&pdev->dev) || !np)
		return 0;

	/* Count the number of regulator supplies */
	for_each_property_of_node(np, prop) {
		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (p && p != prop->name)
			count++;
	}

	if (!count)
		return 0;

	par->regulators = devm_kcalloc(&pdev->dev, count,
				       sizeof(struct regulator *), GFP_KERNEL);
	if (!par->regulators)
		return -ENOMEM;

	/* Get all the regulators */
	for_each_property_of_node(np, prop) {
		char name[32]; /* 32 is max size of property name */

		p = strstr(prop->name, SUPPLY_SUFFIX);
		if (!p || p == prop->name)
			continue;

		strscpy(name, prop->name,
			strlen(prop->name) - strlen(SUPPLY_SUFFIX) + 1);
		regulator = devm_regulator_get_optional(&pdev->dev, name);
		if (IS_ERR(regulator)) {
			if (PTR_ERR(regulator) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(&pdev->dev, "regulator %s not found: %ld\n",
				name, PTR_ERR(regulator));
			continue;
		}
		par->regulators[i++] = regulator;
	}
	par->regulator_count = i;

	return 0;
}

static void simplefb_regulators_enable(struct simplefb_par *par,
				       struct platform_device *pdev)
{
	int i, ret;

	/* Enable all the regulators */
	for (i = 0; i < par->regulator_count; i++) {
		ret = regulator_enable(par->regulators[i]);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to enable regulator %d: %d\n",
				i, ret);
			devm_regulator_put(par->regulators[i]);
			par->regulators[i] = NULL;
		}
	}
	par->regulators_enabled = true;
}

static void simplefb_regulators_destroy(struct simplefb_par *par)
{
	int i;

	if (!par->regulators || !par->regulators_enabled)
		return;

	for (i = 0; i < par->regulator_count; i++)
		if (par->regulators[i])
			regulator_disable(par->regulators[i]);
}
#else
static int simplefb_regulators_get(struct simplefb_par *par,
	struct platform_device *pdev) { return 0; }
static void simplefb_regulators_enable(struct simplefb_par *par,
	struct platform_device *pdev) { }
static void simplefb_regulators_destroy(struct simplefb_par *par) { }
#endif

#if defined CONFIG_OF && defined CONFIG_PM_GENERIC_DOMAINS
static void simplefb_detach_genpds(void *res)
{
	struct simplefb_par *par = res;
	unsigned int i = par->num_genpds;

	if (par->num_genpds <= 1)
		return;

	while (i--) {
		if (par->genpd_links[i])
			device_link_del(par->genpd_links[i]);

		if (!IS_ERR_OR_NULL(par->genpds[i]))
			dev_pm_domain_detach(par->genpds[i], true);
	}
}

static int simplefb_attach_genpds(struct simplefb_par *par,
				  struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int i;
	int err;

	err = of_count_phandle_with_args(dev->of_node, "power-domains",
					 "#power-domain-cells");
	if (err < 0) {
		/* Nothing wrong if optional PDs are missing */
		if (err == -ENOENT)
			return 0;

		dev_err(dev, "failed to parse power-domains: %d\n", err);
		return err;
	}

	par->num_genpds = err;

	/*
	 * Single power-domain devices are handled by the driver core, so
	 * nothing to do here.
	 */
	if (par->num_genpds <= 1)
		return 0;

	par->genpds = devm_kcalloc(dev, par->num_genpds, sizeof(*par->genpds),
				   GFP_KERNEL);
	if (!par->genpds)
		return -ENOMEM;

	par->genpd_links = devm_kcalloc(dev, par->num_genpds,
					sizeof(*par->genpd_links),
					GFP_KERNEL);
	if (!par->genpd_links)
		return -ENOMEM;

	for (i = 0; i < par->num_genpds; i++) {
		par->genpds[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(par->genpds[i])) {
			err = PTR_ERR(par->genpds[i]);
			if (err == -EPROBE_DEFER) {
				simplefb_detach_genpds(par);
				return err;
			}

			dev_warn(dev, "failed to attach domain %u: %d\n", i, err);
			continue;
		}

		par->genpd_links[i] = device_link_add(dev, par->genpds[i],
						      DL_FLAG_STATELESS |
						      DL_FLAG_PM_RUNTIME |
						      DL_FLAG_RPM_ACTIVE);
		if (!par->genpd_links[i])
			dev_warn(dev, "failed to link power-domain %u\n", i);
	}

	return devm_add_action_or_reset(dev, simplefb_detach_genpds, par);
}
#else
static int simplefb_attach_genpds(struct simplefb_par *par,
				  struct platform_device *pdev)
{
	return 0;
}
#endif

static int simplefb_probe(struct platform_device *pdev)
{
	int ret;
	struct simplefb_params params;
	struct fb_info *info;
	struct simplefb_par *par;
	struct resource *res, *mem;

	if (fb_get_options("simplefb", NULL))
		return -ENODEV;

	ret = -ENODEV;
	if (dev_get_platdata(&pdev->dev))
		ret = simplefb_parse_pd(pdev, &params);
	else if (pdev->dev.of_node)
		ret = simplefb_parse_dt(pdev, &params);

	if (ret)
		return ret;

	if (params.memory.start == 0 && params.memory.end == 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			dev_err(&pdev->dev, "No memory resource\n");
			return -EINVAL;
		}
	} else {
		res = &params.memory;
	}

	mem = request_mem_region(res->start, resource_size(res), "simplefb");
	if (!mem) {
		/*
		 * We cannot make this fatal. Sometimes this comes from magic
		 * spaces our resource handlers simply don't know about. Use
		 * the I/O-memory resource as-is and try to map that instead.
		 */
		dev_warn(&pdev->dev, "simplefb: cannot reserve video memory at %pR\n", res);
		mem = res;
	}

	info = framebuffer_alloc(sizeof(struct simplefb_par), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto error_release_mem_region;
	}
	platform_set_drvdata(pdev, info);

	par = info->par;

	info->fix = simplefb_fix;
	info->fix.smem_start = mem->start;
	info->fix.smem_len = resource_size(mem);
	info->fix.line_length = params.stride;

	info->var = simplefb_var;
	info->var.xres = params.width;
	info->var.yres = params.height;
	info->var.xres_virtual = params.width;
	info->var.yres_virtual = params.height;
	info->var.bits_per_pixel = params.format->bits_per_pixel;
	info->var.red = params.format->red;
	info->var.green = params.format->green;
	info->var.blue = params.format->blue;
	info->var.transp = params.format->transp;

	par->base = info->fix.smem_start;
	par->size = info->fix.smem_len;

	info->fbops = &simplefb_ops;
	info->screen_base = ioremap_wc(info->fix.smem_start,
				       info->fix.smem_len);
	if (!info->screen_base) {
		ret = -ENOMEM;
		goto error_fb_release;
	}
	info->pseudo_palette = par->palette;

	ret = simplefb_clocks_get(par, pdev);
	if (ret < 0)
		goto error_unmap;

	ret = simplefb_regulators_get(par, pdev);
	if (ret < 0)
		goto error_clocks;

	ret = simplefb_attach_genpds(par, pdev);
	if (ret < 0)
		goto error_regulators;

	simplefb_clocks_enable(par, pdev);
	simplefb_regulators_enable(par, pdev);

	dev_info(&pdev->dev, "framebuffer at 0x%lx, 0x%x bytes\n",
			     info->fix.smem_start, info->fix.smem_len);
	dev_info(&pdev->dev, "format=%s, mode=%dx%dx%d, linelength=%d\n",
			     params.format->name,
			     info->var.xres, info->var.yres,
			     info->var.bits_per_pixel, info->fix.line_length);

	if (mem != res)
		par->mem = mem; /* release in clean-up handler */

	ret = devm_aperture_acquire_for_platform_device(pdev, par->base, par->size);
	if (ret) {
		dev_err(&pdev->dev, "Unable to acquire aperture: %d\n", ret);
		goto error_regulators;
	}
	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to register simplefb: %d\n", ret);
		goto error_regulators;
	}

	dev_info(&pdev->dev, "fb%d: simplefb registered!\n", info->node);

	return 0;

error_regulators:
	simplefb_regulators_destroy(par);
error_clocks:
	simplefb_clocks_destroy(par);
error_unmap:
	iounmap(info->screen_base);
error_fb_release:
	framebuffer_release(info);
error_release_mem_region:
	if (mem != res)
		release_mem_region(mem->start, resource_size(mem));
	return ret;
}

static void simplefb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	/* simplefb_destroy takes care of info cleanup */
	unregister_framebuffer(info);
}

static const struct of_device_id simplefb_of_match[] = {
	{ .compatible = "simple-framebuffer", },
	{ },
};
MODULE_DEVICE_TABLE(of, simplefb_of_match);

static struct platform_driver simplefb_driver = {
	.driver = {
		.name = "simple-framebuffer",
		.of_match_table = simplefb_of_match,
	},
	.probe = simplefb_probe,
	.remove = simplefb_remove,
};

module_platform_driver(simplefb_driver);

MODULE_AUTHOR("Stephen Warren <swarren@wwwdotorg.org>");
MODULE_DESCRIPTION("Simple framebuffer driver");
MODULE_LICENSE("GPL v2");
