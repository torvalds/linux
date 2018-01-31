/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/rk_fb.h>
#include <linux/device.h>
#include "lcd.h"
#include "../hdmi/rockchip-hdmi.h"

static struct rk_screen *rk_screen;

int rk_fb_get_extern_screen(struct rk_screen *screen)
{
	if (unlikely(!rk_screen) || unlikely(!screen))
		return -1;

	memcpy(screen, rk_screen, sizeof(struct rk_screen));
	screen->dsp_lut = NULL;
	screen->cabc_lut = NULL;
	screen->type = SCREEN_NULL;

	return 0;
}

int  rk_fb_get_prmry_screen(struct rk_screen *screen)
{
	if (unlikely(!rk_screen) || unlikely(!screen))
		return -1;

	memcpy(screen, rk_screen, sizeof(struct rk_screen));
	return 0;
}

int rk_fb_set_prmry_screen(struct rk_screen *screen)
{
	if (unlikely(!rk_screen) || unlikely(!screen))
		return -1;

	rk_screen->lcdc_id = screen->lcdc_id;
	rk_screen->screen_id = screen->screen_id;
	rk_screen->x_mirror = screen->x_mirror;
	rk_screen->y_mirror = screen->y_mirror;
	rk_screen->overscan.left = screen->overscan.left;
	rk_screen->overscan.top = screen->overscan.left;
	rk_screen->overscan.right = screen->overscan.left;
	rk_screen->overscan.bottom = screen->overscan.left;
	return 0;
}

size_t get_fb_size(u8 reserved_fb)
{
	size_t size = 0;
	u32 xres = 0;
	u32 yres = 0;

	if (unlikely(!rk_screen))
		return 0;

	xres = rk_screen->mode.xres;
	yres = rk_screen->mode.yres;

	/* align as 64 bytes(16*4) in an odd number of times */
	xres = ALIGN_64BYTE_ODD_TIMES(xres, ALIGN_PIXEL_64BYTE_RGB8888);
        if (reserved_fb == 1) {
                size = (xres * yres << 2) << 1;/*two buffer*/
        } else {
#if defined(CONFIG_THREE_FB_BUFFER)
		size = (xres * yres << 2) * 3;	/* three buffer */
#else
		size = (xres * yres << 2) << 1; /* two buffer */
#endif
	}
	return ALIGN(size, SZ_1M);
}

static int rk_screen_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}
	rk_screen = devm_kzalloc(&pdev->dev,
			sizeof(struct rk_screen), GFP_KERNEL);
	if (!rk_screen) {
		dev_err(&pdev->dev, "kmalloc for rk screen fail!");
		return  -ENOMEM;
	}
	ret = rk_fb_prase_timing_dt(np, rk_screen);
	dev_info(&pdev->dev, "rockchip screen probe %s\n",
				ret ? "failed" : "success");
	return ret;
}

static const struct of_device_id rk_screen_dt_ids[] = {
	{ .compatible = "rockchip,screen", },
	{}
};

static struct platform_driver rk_screen_driver = {
	.probe		= rk_screen_probe,
	.driver		= {
		.name	= "rk-screen",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rk_screen_dt_ids),
	},
};

static int __init rk_screen_init(void)
{
	return platform_driver_register(&rk_screen_driver);
}

static void __exit rk_screen_exit(void)
{
	platform_driver_unregister(&rk_screen_driver);
}

fs_initcall(rk_screen_init);
module_exit(rk_screen_exit);

