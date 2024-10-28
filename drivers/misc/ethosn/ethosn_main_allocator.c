/*
 *
 * (C) COPYRIGHT 2022 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_device.h"
#include "ethosn_core.h"
#include "ethosn_main_allocator.h"

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static struct ethosn_core *ethosn_core(struct platform_device *pdev)
{
	struct ethosn_core *core = dev_get_drvdata(pdev->dev.parent);

	return core;
}

static int ethosn_main_allocator_pdev_remove(struct platform_device *pdev)
{
	struct ethosn_core *core = ethosn_core(pdev);
	int ret = 0;

	dev_info(&pdev->dev, "Removing main allocator");

	if (!core) {
		dev_err(&pdev->dev, "ethosn core NULL");

		return -EINVAL;
	}

	if (!core->main_allocator) {
		dev_err(&pdev->dev, "main_allocator NULL");

		return -EINVAL;
	}

	dev_info(&pdev->dev, "Depopulate main allocator's child devices\n");

	of_platform_depopulate(&pdev->dev);

	ret = ethosn_dma_top_allocator_destroy(&pdev->dev, &core->main_allocator);

	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev, NULL);

	return ret;
}

// 当遍历设备树发现 ethosn-main_allocator 节点时调用
// 从设备树文件中获取 pdev 所指向的平台设备节点，初始化与之对应的抽象数据结构，并绑定到父设备core的抽象数据结构中
static int ethosn_main_allocator_pdev_probe(struct platform_device *pdev)
{
	// 这里根据设备树文件，找到当前设备 main_allocator 所属的 core
	struct ethosn_core *core = ethosn_core(pdev);
	struct ethosn_dma_allocator *main_allocator;
	int ret = 0;

	dev_info(&pdev->dev, "Probing main allocator\n");

	if (IS_ERR_OR_NULL(core)) {
		dev_err(&pdev->dev, "Invalid parent device driver");

		return -EINVAL;
	}

	// 注意到实际为该平台设备分配的并非 ethosn_dma_allocator 结构, 而是 ethosn_main_allocator
	main_allocator = ethosn_dma_top_allocator_create(&pdev->dev, ETHOSN_ALLOCATOR_MAIN);

	if (IS_ERR_OR_NULL(main_allocator))
		return -ENOMEM;

	core->main_allocator = main_allocator;

	// 这里将平台设备 pdev 与抽象设备数据结构绑定起来
	dev_set_drvdata(&pdev->dev, main_allocator);

	// 为当前 pdev 向内核自动创建和注册平台设备
	ret = of_platform_default_populate(pdev->dev.of_node, NULL, &pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "Failed to populate child devices\n");

	return ret;
}

static const struct of_device_id ethosn_main_allocator_child_pdev_match[] = {
	{ .compatible = ETHOSN_MAIN_ALLOCATOR_DRIVER_NAME },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, ethosn_main_allocator_child_pdev_match);

static struct platform_driver ethosn_main_allocator_pdev_driver = {
	.probe                  = &ethosn_main_allocator_pdev_probe,
	.remove                 = &ethosn_main_allocator_pdev_remove,
	.driver                 = {
		.name           = ETHOSN_MAIN_ALLOCATOR_DRIVER_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(
			ethosn_main_allocator_child_pdev_match),
		.pm             = NULL,
	},
};

int ethosn_main_allocator_platform_driver_register(void)
{
	pr_info("Registering %s", ETHOSN_MAIN_ALLOCATOR_DRIVER_NAME);

	return platform_driver_register(&ethosn_main_allocator_pdev_driver);
}

void ethosn_main_allocator_platform_driver_unregister(void)
{
	pr_info("Unregistering %s", ETHOSN_MAIN_ALLOCATOR_DRIVER_NAME);
	platform_driver_unregister(&ethosn_main_allocator_pdev_driver);
}
