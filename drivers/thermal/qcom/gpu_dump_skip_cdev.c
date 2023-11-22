// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/io.h>

#define GPU_DUMP_SKIP_CDEV_NAME "gpu-dump-skip-cdev"
#define GPU_DUMP_SKIP_MAX_LVL   1

struct gpu_dump_skip_cdev {
	uint8_t gup_dump_skip_state;
	struct thermal_cooling_device *cdev;
	struct device *dev;
	struct nvmem_cell *cell;
	struct device_node *np;
};

static int gpu_dump_skip_set_state(struct thermal_cooling_device *cdev,
					unsigned long state)
{
	struct gpu_dump_skip_cdev *gpu_dump_skip_cdev = cdev->devdata;
	void __iomem *addr;

	if (state > GPU_DUMP_SKIP_MAX_LVL)
		return -EINVAL;

	if (gpu_dump_skip_cdev->gup_dump_skip_state == state)
		return 0;

	gpu_dump_skip_cdev->gup_dump_skip_state = state;

	if (gpu_dump_skip_cdev->cell)
		nvmem_cell_write(gpu_dump_skip_cdev->cell,
				&gpu_dump_skip_cdev->gup_dump_skip_state,
				sizeof(gpu_dump_skip_cdev->gup_dump_skip_state));

	addr =  of_iomap(gpu_dump_skip_cdev->np, 0);
	if (!addr) {
		pr_err("Failed to  of_iomap\n");
		return -ENOMEM;
	}
	__raw_writel(gpu_dump_skip_cdev->gup_dump_skip_state, addr);
	iounmap(addr);

	return 0;
}

static int gpu_dump_skip_get_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{

	struct gpu_dump_skip_cdev *gpu_dump_skip_cdev = cdev->devdata;

	*state = (gpu_dump_skip_cdev->gup_dump_skip_state) ?
			GPU_DUMP_SKIP_MAX_LVL : 0;

	return 0;
}

static int gpu_dump_skip_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	*state = GPU_DUMP_SKIP_MAX_LVL;
	return 0;
}

static struct thermal_cooling_device_ops gpu_dump_skip_ops = {
	.set_cur_state = gpu_dump_skip_set_state,
	.get_cur_state = gpu_dump_skip_get_state,
	.get_max_state = gpu_dump_skip_get_max_state,
};

static int gpu_dump_skip_probe(struct platform_device *pdev)
{
	int ret = 0;
	const char *name;
	char cdev_name[THERMAL_NAME_LENGTH] = GPU_DUMP_SKIP_CDEV_NAME;
	struct device_node *gpu_dump_np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct gpu_dump_skip_cdev *gpu_dump_skip_cdev = NULL;

	gpu_dump_skip_cdev = devm_kzalloc(&pdev->dev, sizeof(*gpu_dump_skip_cdev),
					GFP_KERNEL);
	if (!gpu_dump_skip_cdev)
		return -ENOMEM;

	ret = of_property_read_string(pdev->dev.of_node, "nvmem-cell-names", &name);
	if (ret) {
		pr_debug("Reset thermal flag sdam not available\n");
		return 0;
	}

	gpu_dump_skip_cdev->cell = nvmem_cell_get(&pdev->dev, name);
	if (IS_ERR(gpu_dump_skip_cdev->cell)) {
		dev_err(&pdev->dev, "failed to get nvmem cell %s\n", name);
		return PTR_ERR(gpu_dump_skip_cdev->cell);
	}

	gpu_dump_skip_cdev->np = of_find_compatible_node(NULL, NULL,
				"qcom,msm-imem-gpu-dump-skip");
	if (!gpu_dump_skip_cdev->np)
		return -ENOENT;

	gpu_dump_skip_cdev->gup_dump_skip_state = GPU_DUMP_SKIP_MAX_LVL + 1;
	gpu_dump_skip_cdev->cdev = devm_thermal_of_cooling_device_register(&pdev->dev,
				gpu_dump_np, cdev_name, gpu_dump_skip_cdev,
				&gpu_dump_skip_ops);
	if (IS_ERR(gpu_dump_skip_cdev->cdev)) {
		ret = PTR_ERR(gpu_dump_skip_cdev->cdev);
		dev_err(dev, "Cdev register failed for %s, ret:%d\n",
			cdev_name, ret);
	}

	return ret;
}

static int gpu_dump_skip_remove(struct platform_device *pdev)
{
	struct gpu_dump_skip_cdev *gpu_dump_skip_cdev =
		(struct gpu_dump_skip_cdev *)dev_get_drvdata(&pdev->dev);

	if (gpu_dump_skip_cdev->cell)
		nvmem_cell_put(gpu_dump_skip_cdev->cell);

	return 0;
}

static const struct of_device_id gpu_dump_skip_match[] = {
	{ .compatible = "qcom,gpu-dump-skip-cdev", },
	{},
};

static struct platform_driver gpu_dump_skip_driver = {
	.probe		= gpu_dump_skip_probe,
	.remove         = gpu_dump_skip_remove,
	.driver		= {
		.name = KBUILD_MODNAME,
		.of_match_table = gpu_dump_skip_match,
	},
};
module_platform_driver(gpu_dump_skip_driver);
MODULE_LICENSE("GPL");
