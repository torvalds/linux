/*
 *
 * (C) COPYRIGHT 2022-2023 Arm Limited.
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
#include "ethosn_dma.h"
#include "ethosn_dma_iommu.h"
#include "ethosn_sub_allocator.h"
#include "ethosn_backport.h"

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static struct platform_device *ethosn_global_buffer_data_pdev_for_testing;

struct platform_device *ethosn_get_global_buffer_data_pdev_for_testing(void)
{
	return ethosn_global_buffer_data_pdev_for_testing;
}

/* Exported for use by test module */
EXPORT_SYMBOL(ethosn_get_global_buffer_data_pdev_for_testing);

static struct ethosn_dma_allocator *ethosn_get_top_allocator(
	struct platform_device *pdev)
{
	struct ethosn_dma_allocator *top_allocator = dev_get_drvdata(
		pdev->dev.parent);

	return top_allocator;
}

static enum ethosn_stream_type get_stream_type(struct platform_device *pdev)
{
	const struct device_node *np = pdev->dev.of_node;

	if (of_node_name_eq(np, "firmware"))
		return ETHOSN_STREAM_FIRMWARE;
	else if (of_node_name_eq(np, "working_data"))
		return ETHOSN_STREAM_WORKING_DATA;
	else if (of_node_name_eq(np, "command_stream"))
		return ETHOSN_STREAM_COMMAND_STREAM;
	else if (of_node_name_eq(np, "weight_data"))
		return ETHOSN_STREAM_WEIGHT_DATA;
	else if (of_node_name_eq(np, "buffer_data"))
		return ETHOSN_STREAM_IO_BUFFER;
	else if (of_node_name_eq(np, "intermediate_data"))
		return ETHOSN_STREAM_INTERMEDIATE_BUFFER;
	else
		return ETHOSN_STREAM_INVALID;

	return ETHOSN_STREAM_INVALID;
}

static phys_addr_t get_stream_speculative_page_addr(
	struct ethosn_device *ethosn,
	enum ethosn_stream_type stream_type)
{
#ifdef ETHOSN_TZMP1
	/* Use page from firmware padding for speculative accesses */
	const phys_addr_t speculative_page_addr =
		(ethosn->protected_firmware.addr - PAGE_SIZE);
#else
	const phys_addr_t speculative_page_addr = 0U;
#endif

	switch (stream_type) {
	case ETHOSN_STREAM_FIRMWARE:
	/* Fallthrough */
	case ETHOSN_STREAM_PLE_CODE:

		return speculative_page_addr;

	case ETHOSN_STREAM_WORKING_DATA:
	/* Fallthrough */
	case ETHOSN_STREAM_COMMAND_STREAM:
	/* Fallthrough */
	case ETHOSN_STREAM_WEIGHT_DATA:
	/* Fallthrough */
	case ETHOSN_STREAM_IO_BUFFER:
	/* Fallthrough */
	case ETHOSN_STREAM_INTERMEDIATE_BUFFER:

		return 0U;
	default:
		WARN_ON(1);

		return 0U;
	}
}

static dma_addr_t get_stream_addr_base(struct ethosn_device *ethosn,
				       enum ethosn_stream_type stream_type)
{
#ifdef ETHOSN_TZMP1
	const dma_addr_t firmware_base =
		ethosn->protected_firmware.firmware_addr_base;
	const dma_addr_t working_data_base =
		ethosn->protected_firmware.working_data_addr_base;
	const dma_addr_t command_stream_base =
		ethosn->protected_firmware.command_stream_addr_base;
#else
	const dma_addr_t firmware_base = IOMMU_FIRMWARE_ADDR_BASE;
	const dma_addr_t working_data_base = IOMMU_WORKING_DATA_ADDR_BASE;
	const dma_addr_t command_stream_base = IOMMU_COMMAND_STREAM_ADDR_BASE;
#endif

	switch (stream_type) {
	case ETHOSN_STREAM_FIRMWARE:
	/* Fallthrough */
	case ETHOSN_STREAM_PLE_CODE:

		return firmware_base;

	case ETHOSN_STREAM_WORKING_DATA:

		return working_data_base;

	case ETHOSN_STREAM_COMMAND_STREAM:

		return command_stream_base;

	case ETHOSN_STREAM_WEIGHT_DATA:

		return IOMMU_WEIGHT_DATA_ADDR_BASE;

	case ETHOSN_STREAM_IO_BUFFER:

		return IOMMU_BUFFER_ADDR_BASE;

	case ETHOSN_STREAM_INTERMEDIATE_BUFFER:

		return IOMMU_INTERMEDIATE_BUFFER_ADDR_BASE;

	default:
		WARN_ON(1);

		return 0U;
	}
}

static int ethosn_mem_stream_pdev_remove(struct platform_device *pdev)
{
	struct ethosn_dma_allocator *top_allocator =
		ethosn_get_top_allocator(pdev);
	struct ethosn_dma_sub_allocator *sub_allocator =
		dev_get_drvdata(&pdev->dev);
	enum ethosn_stream_type stream_type = get_stream_type(pdev);

	if (!top_allocator || !sub_allocator)
		return -EINVAL;

	dev_info(&pdev->dev, "Removing sub_allocator\n");

	ethosn_dma_sub_allocator_destroy(top_allocator, stream_type);

	dev_set_drvdata(&pdev->dev, NULL);

	if (stream_type == ETHOSN_STREAM_IO_BUFFER)
		ethosn_global_buffer_data_pdev_for_testing = NULL;

	return 0;
}

static int ethosn_mem_stream_pdev_probe(struct platform_device *pdev)
{
	struct ethosn_device *ethosn;
	struct ethosn_dma_allocator *top_allocator =
		ethosn_get_top_allocator(pdev);
	int ret;
	enum ethosn_stream_type stream_type = get_stream_type(pdev);

	if (!top_allocator)
		return -EINVAL;

	switch (top_allocator->type) {
	case ETHOSN_ALLOCATOR_ASSET: {
		ethosn = dev_get_drvdata(pdev->dev.parent->parent);
		break;
	}
	case ETHOSN_ALLOCATOR_MAIN: {
		ethosn = dev_get_drvdata(pdev->dev.parent->parent->parent);
		break;
	}
	default:

		return -EINVAL;
	}

	if (stream_type >= ETHOSN_STREAM_INVALID) {
		dev_err(&pdev->dev, "Invalid stream type");

		return -EINVAL;
	}

	ret = ethosn_dma_sub_allocator_create(&pdev->dev, top_allocator,
					      stream_type,
					      get_stream_addr_base(ethosn,
								   stream_type),
					      get_stream_speculative_page_addr(
						      ethosn,
						      stream_type),
					      ethosn->smmu_available);

	if (ret)
		return ret;

	dev_set_drvdata(&pdev->dev,
			ethosn_get_sub_allocator(top_allocator, stream_type));

	if (stream_type == ETHOSN_STREAM_IO_BUFFER &&
	    ethosn_global_buffer_data_pdev_for_testing == NULL)
		ethosn_global_buffer_data_pdev_for_testing = pdev;

	return 0;
}

static const struct of_device_id ethosn_mem_stream_pdev_match[] = {
	{ .compatible = ETHOSN_MEM_STREAM_DRIVER_NAME },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, ethosn_mem_stream_pdev_match);

static struct platform_driver ethosn_mem_stream_pdev_driver = {
	.probe                  = &ethosn_mem_stream_pdev_probe,
	.remove                 = &ethosn_mem_stream_pdev_remove,
	.driver                 = {
		.name           = ETHOSN_MEM_STREAM_DRIVER_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(ethosn_mem_stream_pdev_match),
		.pm             = NULL,
	},
};

int ethosn_mem_stream_platform_driver_register(void)
{
	pr_info("Registering %s", ETHOSN_MEM_STREAM_DRIVER_NAME);

	return platform_driver_register(&ethosn_mem_stream_pdev_driver);
}

void ethosn_mem_stream_platform_driver_unregister(void)
{
	pr_info("Unregistering %s", ETHOSN_MEM_STREAM_DRIVER_NAME);

	platform_driver_unregister(&ethosn_mem_stream_pdev_driver);
}
