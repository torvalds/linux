#include "rk3288_hdmi_hw.h"
#include "rk3288_hdmi.h"



#if defined(CONFIG_OF)
static const struct of_device_id rk3288_hdmi_dt_ids[] = {
	{.compatible = "rockchips,rk3288-hdmi",},
	{}
};
MODULE_DEVICE_TABLE(of, rk3288_hdmi_dt_ids);
#endif

static int rk3288_hdmi_probe (struct platform_device *pdev)
{
	return 0;
}

static int rk3288_hdmi_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk3288_hdmi_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver rk3288_hdmi_driver = {
	.probe		= rk3288_hdmi_probe,
	.remove		= rk3288_hdmi_remove,
	.driver		= {
		.name	= "rk3288-hdmi",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rk3288_hdmi_dt_ids),
	},
	.shutdown   = rk30_hdmi_shutdown,
};

static int __init rk3288_hdmi_init(void)
{
    return platform_driver_register(&rk3288_hdmi_driver);
}

static void __exit rk3288_hdmi_exit(void)
{
    platform_driver_unregister(&rk3288_hdmi_driver);
}

device_initcall_sync(rk3288_hdmi_init);
module_exit(rk3288_hdmi_exit);

