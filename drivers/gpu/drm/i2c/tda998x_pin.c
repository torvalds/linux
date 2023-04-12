#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/iommu.h>
#include <drm/drm_drv.h>
#include <drm/drm_of.h>

#define DRIVER_NAME	"starfive"
#define DRIVER_DESC	"StarFive Soc DRM"
#define DRIVER_DATE	"20220624"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0
#define DRIVER_VERSION	"v1.0.0"


static int starfive_drm_platform_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s, ok\n", __func__);

	return 0;
}

static const struct of_device_id tda998x_rgb_dt_ids[] = {
	{ .compatible = "starfive,tda998x_rgb_pin", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, starfive_drm_dt_ids);

static struct platform_driver starfive_drm_platform_driver = {
	.probe = starfive_drm_platform_probe,
	.driver = {
		.name = "tda998x_rgb_dt_ids",
		.of_match_table = tda998x_rgb_dt_ids,
	},
};

module_platform_driver(starfive_drm_platform_driver);

MODULE_AUTHOR("David Li <david.li@starfivetech.com>");
MODULE_DESCRIPTION("starfive DRM Driver");
MODULE_LICENSE("GPL v2");
