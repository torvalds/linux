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

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/qcom_scm.h>
#include <linux/sizes.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "firmware.h"

#define VENUS_PAS_ID			9
#define VENUS_FW_MEM_SIZE		(6 * SZ_1M)

int venus_boot(struct device *dev, const char *fwname)
{
	const struct firmware *mdt;
	struct device_node *node;
	phys_addr_t mem_phys;
	struct resource r;
	ssize_t fw_size;
	size_t mem_size;
	void *mem_va;
	int ret;

	if (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) || !qcom_scm_is_available())
		return -EPROBE_DEFER;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	mem_phys = r.start;
	mem_size = resource_size(&r);

	if (mem_size < VENUS_FW_MEM_SIZE)
		return -EINVAL;

	mem_va = memremap(r.start, mem_size, MEMREMAP_WC);
	if (!mem_va) {
		dev_err(dev, "unable to map memory region: %pa+%zx\n",
			&r.start, mem_size);
		return -ENOMEM;
	}

	ret = request_firmware(&mdt, fwname, dev);
	if (ret < 0)
		goto err_unmap;

	fw_size = qcom_mdt_get_size(mdt);
	if (fw_size < 0) {
		ret = fw_size;
		release_firmware(mdt);
		goto err_unmap;
	}

	ret = qcom_mdt_load(dev, mdt, fwname, VENUS_PAS_ID, mem_va, mem_phys,
			    mem_size);

	release_firmware(mdt);

	if (ret)
		goto err_unmap;

	ret = qcom_scm_pas_auth_and_reset(VENUS_PAS_ID);
	if (ret)
		goto err_unmap;

err_unmap:
	memunmap(mem_va);
	return ret;
}

int venus_shutdown(struct device *dev)
{
	return qcom_scm_pas_shutdown(VENUS_PAS_ID);
}
