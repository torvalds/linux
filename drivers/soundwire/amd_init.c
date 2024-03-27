// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * SoundWire AMD Manager Initialize routines
 *
 * Initializes and creates SDW devices based on ACPI and Hardware values
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "amd_init.h"

#define ACP_PAD_PULLDOWN_CTRL				0x0001448
#define ACP_SW_PAD_KEEPER_EN				0x0001454
#define AMD_SDW0_PAD_CTRL_MASK				0x60
#define AMD_SDW1_PAD_CTRL_MASK				5
#define AMD_SDW_PAD_CTRL_MASK		(AMD_SDW0_PAD_CTRL_MASK | AMD_SDW1_PAD_CTRL_MASK)
#define AMD_SDW0_PAD_EN					1
#define AMD_SDW1_PAD_EN					0x10
#define AMD_SDW_PAD_EN			(AMD_SDW0_PAD_EN | AMD_SDW1_PAD_EN)

static int amd_enable_sdw_pads(void __iomem *mmio, u32 link_mask, struct device *dev)
{
	u32 pad_keeper_en, pad_pulldown_ctrl_mask;

	switch (link_mask) {
	case 1:
		pad_keeper_en = AMD_SDW0_PAD_EN;
		pad_pulldown_ctrl_mask = AMD_SDW0_PAD_CTRL_MASK;
		break;
	case 2:
		pad_keeper_en = AMD_SDW1_PAD_EN;
		pad_pulldown_ctrl_mask = AMD_SDW1_PAD_CTRL_MASK;
		break;
	case 3:
		pad_keeper_en = AMD_SDW_PAD_EN;
		pad_pulldown_ctrl_mask = AMD_SDW_PAD_CTRL_MASK;
		break;
	default:
		dev_err(dev, "No SDW Links are enabled\n");
		return -ENODEV;
	}

	amd_updatel(mmio, ACP_SW_PAD_KEEPER_EN, pad_keeper_en, pad_keeper_en);
	amd_updatel(mmio, ACP_PAD_PULLDOWN_CTRL, pad_pulldown_ctrl_mask, 0);

	return 0;
}

static int sdw_amd_cleanup(struct sdw_amd_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->count; i++) {
		if (!(ctx->link_mask & BIT(i)))
			continue;
		platform_device_unregister(ctx->pdev[i]);
	}

	return 0;
}

static struct sdw_amd_ctx *sdw_amd_probe_controller(struct sdw_amd_res *res)
{
	struct sdw_amd_ctx *ctx;
	struct acpi_device *adev;
	struct resource *sdw_res;
	struct acp_sdw_pdata sdw_pdata[2];
	struct platform_device_info pdevinfo[2];
	u32 link_mask;
	int count, index;
	int ret;

	if (!res)
		return NULL;

	adev = acpi_fetch_acpi_dev(res->handle);
	if (!adev)
		return NULL;

	if (!res->count)
		return NULL;

	count = res->count;
	dev_dbg(&adev->dev, "Creating %d SDW Link devices\n", count);
	ret = amd_enable_sdw_pads(res->mmio_base, res->link_mask, res->parent);
	if (ret)
		return NULL;

	/*
	 * we need to alloc/free memory manually and can't use devm:
	 * this routine may be called from a workqueue, and not from
	 * the parent .probe.
	 * If devm_ was used, the memory might never be freed on errors.
	 */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->count = count;
	ctx->link_mask = res->link_mask;
	sdw_res = kzalloc(sizeof(*sdw_res), GFP_KERNEL);
	if (!sdw_res) {
		kfree(ctx);
		return NULL;
	}
	sdw_res->flags = IORESOURCE_MEM;
	sdw_res->start = res->addr;
	sdw_res->end = res->addr + res->reg_range;
	memset(&pdevinfo, 0, sizeof(pdevinfo));
	link_mask = ctx->link_mask;
	for (index = 0; index < count; index++) {
		if (!(link_mask & BIT(index)))
			continue;

		sdw_pdata[index].instance = index;
		sdw_pdata[index].acp_sdw_lock = res->acp_lock;
		pdevinfo[index].name = "amd_sdw_manager";
		pdevinfo[index].id = index;
		pdevinfo[index].parent = res->parent;
		pdevinfo[index].num_res = 1;
		pdevinfo[index].res = sdw_res;
		pdevinfo[index].data = &sdw_pdata[index];
		pdevinfo[index].size_data = sizeof(struct acp_sdw_pdata);
		pdevinfo[index].fwnode = acpi_fwnode_handle(adev);
		ctx->pdev[index] = platform_device_register_full(&pdevinfo[index]);
		if (IS_ERR(ctx->pdev[index]))
			goto err;
	}
	kfree(sdw_res);
	return ctx;
err:
	while (index--) {
		if (!(link_mask & BIT(index)))
			continue;

		platform_device_unregister(ctx->pdev[index]);
	}

	kfree(sdw_res);
	kfree(ctx);
	return NULL;
}

static int sdw_amd_startup(struct sdw_amd_ctx *ctx)
{
	struct amd_sdw_manager *amd_manager;
	int i, ret;

	/* Startup SDW Manager devices */
	for (i = 0; i < ctx->count; i++) {
		if (!(ctx->link_mask & BIT(i)))
			continue;
		amd_manager = dev_get_drvdata(&ctx->pdev[i]->dev);
		ret = amd_sdw_manager_start(amd_manager);
		if (ret)
			return ret;
	}

	return 0;
}

int sdw_amd_probe(struct sdw_amd_res *res, struct sdw_amd_ctx **sdw_ctx)
{
	*sdw_ctx = sdw_amd_probe_controller(res);
	if (!*sdw_ctx)
		return -ENODEV;

	return sdw_amd_startup(*sdw_ctx);
}
EXPORT_SYMBOL_NS(sdw_amd_probe, SOUNDWIRE_AMD_INIT);

void sdw_amd_exit(struct sdw_amd_ctx *ctx)
{
	sdw_amd_cleanup(ctx);
	kfree(ctx->ids);
	kfree(ctx);
}
EXPORT_SYMBOL_NS(sdw_amd_exit, SOUNDWIRE_AMD_INIT);

int sdw_amd_get_slave_info(struct sdw_amd_ctx *ctx)
{
	struct amd_sdw_manager *amd_manager;
	struct sdw_bus *bus;
	struct sdw_slave *slave;
	struct list_head *node;
	int index;
	int i = 0;
	int num_slaves = 0;

	for (index = 0; index < ctx->count; index++) {
		if (!(ctx->link_mask & BIT(index)))
			continue;
		amd_manager = dev_get_drvdata(&ctx->pdev[index]->dev);
		if (!amd_manager)
			return -ENODEV;
		bus = &amd_manager->bus;
		/* Calculate number of slaves */
		list_for_each(node, &bus->slaves)
			num_slaves++;
	}

	ctx->ids = kcalloc(num_slaves, sizeof(*ctx->ids), GFP_KERNEL);
	if (!ctx->ids)
		return -ENOMEM;
	ctx->num_slaves = num_slaves;
	for (index = 0; index < ctx->count; index++) {
		if (!(ctx->link_mask & BIT(index)))
			continue;
		amd_manager = dev_get_drvdata(&ctx->pdev[index]->dev);
		if (amd_manager) {
			bus = &amd_manager->bus;
			list_for_each_entry(slave, &bus->slaves, node) {
				ctx->ids[i].id = slave->id;
				ctx->ids[i].link_id = bus->link_id;
				i++;
			}
		}
	}
	return 0;
}
EXPORT_SYMBOL_NS(sdw_amd_get_slave_info, SOUNDWIRE_AMD_INIT);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD SoundWire Init Library");
MODULE_LICENSE("Dual BSD/GPL");
