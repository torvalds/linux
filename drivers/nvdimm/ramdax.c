// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Mike Rapoport, Microsoft
 *
 * Based on e820 pmem driver:
 * Copyright (c) 2015, Christoph Hellwig.
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/platform_device.h>
#include <linux/memory_hotplug.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <linux/numa.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>

#include <uapi/linux/ndctl.h>

#define LABEL_AREA_SIZE	SZ_128K

struct ramdax_dimm {
	struct nvdimm *nvdimm;
	void *label_area;
};

static void ramdax_remove(struct platform_device *pdev)
{
	struct nvdimm_bus *nvdimm_bus = platform_get_drvdata(pdev);

	nvdimm_bus_unregister(nvdimm_bus);
}

static int ramdax_register_region(struct resource *res,
		struct nvdimm *nvdimm,
		struct nvdimm_bus *nvdimm_bus)
{
	struct nd_mapping_desc mapping;
	struct nd_region_desc ndr_desc;
	struct nd_interleave_set *nd_set;
	int nid = phys_to_target_node(res->start);

	nd_set = kzalloc(sizeof(*nd_set), GFP_KERNEL);
	if (!nd_set)
		return -ENOMEM;

	nd_set->cookie1 = 0xcafebeefcafebeef;
	nd_set->cookie2 = nd_set->cookie1;
	nd_set->altcookie = nd_set->cookie1;

	memset(&mapping, 0, sizeof(mapping));
	mapping.nvdimm = nvdimm;
	mapping.start = 0;
	mapping.size = resource_size(res) - LABEL_AREA_SIZE;

	memset(&ndr_desc, 0, sizeof(ndr_desc));
	ndr_desc.res = res;
	ndr_desc.numa_node = numa_map_to_online_node(nid);
	ndr_desc.target_node = nid;
	ndr_desc.num_mappings = 1;
	ndr_desc.mapping = &mapping;
	ndr_desc.nd_set = nd_set;

	if (!nvdimm_pmem_region_create(nvdimm_bus, &ndr_desc))
		goto err_free_nd_set;

	return 0;

err_free_nd_set:
	kfree(nd_set);
	return -ENXIO;
}

static int ramdax_register_dimm(struct resource *res, void *data)
{
	resource_size_t start = res->start;
	resource_size_t size = resource_size(res);
	unsigned long flags = 0, cmd_mask = 0;
	struct nvdimm_bus *nvdimm_bus = data;
	struct ramdax_dimm *dimm;
	int err;

	dimm = kzalloc(sizeof(*dimm), GFP_KERNEL);
	if (!dimm)
		return -ENOMEM;

	dimm->label_area = memremap(start + size - LABEL_AREA_SIZE,
				    LABEL_AREA_SIZE, MEMREMAP_WB);
	if (!dimm->label_area) {
		err = -ENOMEM;
		goto err_free_dimm;
	}

	set_bit(NDD_LABELING, &flags);
	set_bit(NDD_REGISTER_SYNC, &flags);
	set_bit(ND_CMD_GET_CONFIG_SIZE, &cmd_mask);
	set_bit(ND_CMD_GET_CONFIG_DATA, &cmd_mask);
	set_bit(ND_CMD_SET_CONFIG_DATA, &cmd_mask);
	dimm->nvdimm = nvdimm_create(nvdimm_bus, dimm,
				     /* dimm_attribute_groups */ NULL,
				     flags, cmd_mask, 0, NULL);
	if (!dimm->nvdimm) {
		err = -ENOMEM;
		goto err_unmap_label;
	}

	err = ramdax_register_region(res, dimm->nvdimm, nvdimm_bus);
	if (err)
		goto err_remove_nvdimm;

	return 0;

err_remove_nvdimm:
	nvdimm_delete(dimm->nvdimm);
err_unmap_label:
	memunmap(dimm->label_area);
err_free_dimm:
	kfree(dimm);
	return err;
}

static int ramdax_get_config_size(struct nvdimm *nvdimm, int buf_len,
		struct nd_cmd_get_config_size *cmd)
{
	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	*cmd = (struct nd_cmd_get_config_size){
		.status = 0,
		.config_size = LABEL_AREA_SIZE,
		.max_xfer = 8,
	};

	return 0;
}

static int ramdax_get_config_data(struct nvdimm *nvdimm, int buf_len,
		struct nd_cmd_get_config_data_hdr *cmd)
{
	struct ramdax_dimm *dimm = nvdimm_provider_data(nvdimm);

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;
	if (struct_size(cmd, out_buf, cmd->in_length) > buf_len)
		return -EINVAL;
	if (size_add(cmd->in_offset, cmd->in_length) > LABEL_AREA_SIZE)
		return -EINVAL;

	memcpy(cmd->out_buf, dimm->label_area + cmd->in_offset, cmd->in_length);

	return 0;
}

static int ramdax_set_config_data(struct nvdimm *nvdimm, int buf_len,
		struct nd_cmd_set_config_hdr *cmd)
{
	struct ramdax_dimm *dimm = nvdimm_provider_data(nvdimm);

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;
	if (struct_size(cmd, in_buf, cmd->in_length) > buf_len)
		return -EINVAL;
	if (size_add(cmd->in_offset, cmd->in_length) > LABEL_AREA_SIZE)
		return -EINVAL;

	memcpy(dimm->label_area + cmd->in_offset, cmd->in_buf, cmd->in_length);

	return 0;
}

static int ramdax_nvdimm_ctl(struct nvdimm *nvdimm, unsigned int cmd,
		void *buf, unsigned int buf_len)
{
	unsigned long cmd_mask = nvdimm_cmd_mask(nvdimm);

	if (!test_bit(cmd, &cmd_mask))
		return -ENOTTY;

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		return ramdax_get_config_size(nvdimm, buf_len, buf);
	case ND_CMD_GET_CONFIG_DATA:
		return ramdax_get_config_data(nvdimm, buf_len, buf);
	case ND_CMD_SET_CONFIG_DATA:
		return ramdax_set_config_data(nvdimm, buf_len, buf);
	default:
		return -ENOTTY;
	}
}

static int ramdax_ctl(struct nvdimm_bus_descriptor *nd_desc,
		struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		unsigned int buf_len, int *cmd_rc)
{
	/*
	 * No firmware response to translate, let the transport error
	 * code take precedence.
	 */
	*cmd_rc = 0;

	if (!nvdimm)
		return -ENOTTY;
	return ramdax_nvdimm_ctl(nvdimm, cmd, buf, buf_len);
}

#ifdef CONFIG_OF
static const struct of_device_id ramdax_of_matches[] = {
	{ .compatible = "pmem-region", },
	{ },
};
#endif

static int ramdax_probe_of(struct platform_device *pdev,
		struct nvdimm_bus *bus, struct device_node *np)
{
	int err;

	if (!of_match_node(ramdax_of_matches, np))
		return -ENODEV;

	for (int i = 0; i < pdev->num_resources; i++) {
		err = ramdax_register_dimm(&pdev->resource[i], bus);
		if (err)
			goto err_unregister;
	}

	return 0;

err_unregister:
	/*
	 * FIXME: should we unregister the dimms that were registered
	 * successfully
	 */
	return err;
}

static int ramdax_probe(struct platform_device *pdev)
{
	static struct nvdimm_bus_descriptor nd_desc;
	struct device *dev = &pdev->dev;
	struct nvdimm_bus *nvdimm_bus;
	struct device_node *np;
	int rc = -ENXIO;

	nd_desc.provider_name = "ramdax";
	nd_desc.module = THIS_MODULE;
	nd_desc.ndctl = ramdax_ctl;
	nvdimm_bus = nvdimm_bus_register(dev, &nd_desc);
	if (!nvdimm_bus)
		goto err;

	np = dev_of_node(&pdev->dev);
	if (np)
		rc = ramdax_probe_of(pdev, nvdimm_bus, np);
	else
		rc = walk_iomem_res_desc(IORES_DESC_PERSISTENT_MEMORY_LEGACY,
					 IORESOURCE_MEM, 0, -1, nvdimm_bus,
					 ramdax_register_dimm);
	if (rc)
		goto err;

	platform_set_drvdata(pdev, nvdimm_bus);

	return 0;
err:
	nvdimm_bus_unregister(nvdimm_bus);
	return rc;
}

static struct platform_driver ramdax_driver = {
	.probe = ramdax_probe,
	.remove = ramdax_remove,
	.driver = {
		.name = "ramdax",
	},
};

module_platform_driver(ramdax_driver);

MODULE_DESCRIPTION("NVDIMM support for e820 type-12 memory and OF pmem-region");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Microsoft Corporation");
