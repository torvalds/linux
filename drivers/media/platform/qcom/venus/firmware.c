/*
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "firmware.h"

#define VENUS_FIRMWARE_NAME		"venus.mdt"
#define VENUS_PAS_ID			9
#define VENUS_FW_MEM_SIZE		SZ_8M

static void device_release_dummy(struct device *dev)
{
	of_reserved_mem_device_release(dev);
}

int venus_boot(struct device *parent, struct device *fw_dev)
{
	const struct firmware *mdt;
	phys_addr_t mem_phys;
	ssize_t fw_size;
	size_t mem_size;
	void *mem_va;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	fw_dev->parent = parent;
	fw_dev->release = device_release_dummy;

	ret = dev_set_name(fw_dev, "%s:%s", dev_name(parent), "firmware");
	if (ret)
		return ret;

	ret = device_register(fw_dev);
	if (ret < 0)
		return ret;

	ret = of_reserved_mem_device_init_by_idx(fw_dev, parent->of_node, 0);
	if (ret)
		goto err_unreg_device;

	mem_size = VENUS_FW_MEM_SIZE;

	mem_va = dmam_alloc_coherent(fw_dev, mem_size, &mem_phys, GFP_KERNEL);
	if (!mem_va) {
		ret = -ENOMEM;
		goto err_unreg_device;
	}

	ret = request_firmware(&mdt, VENUS_FIRMWARE_NAME, fw_dev);
	if (ret < 0)
		goto err_unreg_device;

	fw_size = qcom_mdt_get_size(mdt);
	if (fw_size < 0) {
		ret = fw_size;
		release_firmware(mdt);
		goto err_unreg_device;
	}

	ret = qcom_mdt_load(fw_dev, mdt, VENUS_FIRMWARE_NAME, VENUS_PAS_ID,
			    mem_va, mem_phys, mem_size);

	release_firmware(mdt);

	if (ret)
		goto err_unreg_device;

	ret = qcom_scm_pas_auth_and_reset(VENUS_PAS_ID);
	if (ret)
		goto err_unreg_device;

	return 0;

err_unreg_device:
	device_unregister(fw_dev);
	return ret;
}

int venus_shutdown(struct device *fw_dev)
{
	int ret;

	ret = qcom_scm_pas_shutdown(VENUS_PAS_ID);
	device_unregister(fw_dev);
	memset(fw_dev, 0, sizeof(*fw_dev));

	return ret;
}
