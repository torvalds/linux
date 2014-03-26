
#include <linux/rk_fb.h>
#include <linux/device.h>
#include "lcd.h"

static struct rk_screen *rk_screen;
int  rk_fb_get_prmry_screen(struct rk_screen *screen)
{
	memcpy(screen, rk_screen, sizeof(struct rk_screen));
	return 0;
}


size_t get_fb_size(void)
{
	
}

static int rk_screen_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}
	rk_screen = devm_kzalloc(&pdev->dev, sizeof(struct rk_screen), GFP_KERNEL);
	if (!rk_screen) {
		dev_err(&pdev->dev, "kmalloc for rk screen fail!");
		return  -ENOMEM;
	}
	ret = rk_fb_prase_timing_dt(np,rk_screen);
	dev_info(&pdev->dev, "rockchip screen probe %s\n",
				ret? "failed" : "success");
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

subsys_initcall_sync(rk_screen_init);
module_exit(rk_screen_exit);

