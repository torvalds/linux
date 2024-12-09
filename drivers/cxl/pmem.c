// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <linux/unaligned.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/ndctl.h>
#include <linux/async.h>
#include <linux/slab.h>
#include <linux/nd.h>
#include "cxlmem.h"
#include "cxl.h"

static __read_mostly DECLARE_BITMAP(exclusive_cmds, CXL_MEM_COMMAND_ID_MAX);

static void clear_exclusive(void *mds)
{
	clear_exclusive_cxl_commands(mds, exclusive_cmds);
}

static void unregister_nvdimm(void *nvdimm)
{
	nvdimm_delete(nvdimm);
}

static ssize_t provider_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);

	return sysfs_emit(buf, "%s\n", dev_name(&cxl_nvd->dev));
}
static DEVICE_ATTR_RO(provider);

static ssize_t id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_dev_state *cxlds = cxl_nvd->cxlmd->cxlds;

	return sysfs_emit(buf, "%lld\n", cxlds->serial);
}
static DEVICE_ATTR_RO(id);

static struct attribute *cxl_dimm_attributes[] = {
	&dev_attr_id.attr,
	&dev_attr_provider.attr,
	NULL
};

static const struct attribute_group cxl_dimm_attribute_group = {
	.name = "cxl",
	.attrs = cxl_dimm_attributes,
};

static const struct attribute_group *cxl_dimm_attribute_groups[] = {
	&cxl_dimm_attribute_group,
	NULL
};

static int cxl_nvdimm_probe(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_nvdimm_bridge *cxl_nvb = cxlmd->cxl_nvb;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	unsigned long flags = 0, cmd_mask = 0;
	struct nvdimm *nvdimm;
	int rc;

	set_exclusive_cxl_commands(mds, exclusive_cmds);
	rc = devm_add_action_or_reset(dev, clear_exclusive, mds);
	if (rc)
		return rc;

	set_bit(NDD_LABELING, &flags);
	set_bit(NDD_REGISTER_SYNC, &flags);
	set_bit(ND_CMD_GET_CONFIG_SIZE, &cmd_mask);
	set_bit(ND_CMD_GET_CONFIG_DATA, &cmd_mask);
	set_bit(ND_CMD_SET_CONFIG_DATA, &cmd_mask);
	nvdimm = __nvdimm_create(cxl_nvb->nvdimm_bus, cxl_nvd,
				 cxl_dimm_attribute_groups, flags,
				 cmd_mask, 0, NULL, cxl_nvd->dev_id,
				 cxl_security_ops, NULL);
	if (!nvdimm)
		return -ENOMEM;

	dev_set_drvdata(dev, nvdimm);
	return devm_add_action_or_reset(dev, unregister_nvdimm, nvdimm);
}

static struct cxl_driver cxl_nvdimm_driver = {
	.name = "cxl_nvdimm",
	.probe = cxl_nvdimm_probe,
	.id = CXL_DEVICE_NVDIMM,
	.drv = {
		.suppress_bind_attrs = true,
	},
};

static int cxl_pmem_get_config_size(struct cxl_memdev_state *mds,
				    struct nd_cmd_get_config_size *cmd,
				    unsigned int buf_len)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	*cmd = (struct nd_cmd_get_config_size){
		.config_size = mds->lsa_size,
		.max_xfer =
			cxl_mbox->payload_size - sizeof(struct cxl_mbox_set_lsa),
	};

	return 0;
}

static int cxl_pmem_get_config_data(struct cxl_memdev_state *mds,
				    struct nd_cmd_get_config_data_hdr *cmd,
				    unsigned int buf_len)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct cxl_mbox_get_lsa get_lsa;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;
	if (struct_size(cmd, out_buf, cmd->in_length) > buf_len)
		return -EINVAL;

	get_lsa = (struct cxl_mbox_get_lsa) {
		.offset = cpu_to_le32(cmd->in_offset),
		.length = cpu_to_le32(cmd->in_length),
	};
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_GET_LSA,
		.payload_in = &get_lsa,
		.size_in = sizeof(get_lsa),
		.size_out = cmd->in_length,
		.payload_out = cmd->out_buf,
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);
	cmd->status = 0;

	return rc;
}

static int cxl_pmem_set_config_data(struct cxl_memdev_state *mds,
				    struct nd_cmd_set_config_hdr *cmd,
				    unsigned int buf_len)
{
	struct cxl_mailbox *cxl_mbox = &mds->cxlds.cxl_mbox;
	struct cxl_mbox_set_lsa *set_lsa;
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	/* 4-byte status follows the input data in the payload */
	if (size_add(struct_size(cmd, in_buf, cmd->in_length), 4) > buf_len)
		return -EINVAL;

	set_lsa =
		kvzalloc(struct_size(set_lsa, data, cmd->in_length), GFP_KERNEL);
	if (!set_lsa)
		return -ENOMEM;

	*set_lsa = (struct cxl_mbox_set_lsa) {
		.offset = cpu_to_le32(cmd->in_offset),
	};
	memcpy(set_lsa->data, cmd->in_buf, cmd->in_length);
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_SET_LSA,
		.payload_in = set_lsa,
		.size_in = struct_size(set_lsa, data, cmd->in_length),
	};

	rc = cxl_internal_send_cmd(cxl_mbox, &mbox_cmd);

	/*
	 * Set "firmware" status (4-packed bytes at the end of the input
	 * payload.
	 */
	put_unaligned(0, (u32 *) &cmd->in_buf[cmd->in_length]);
	kvfree(set_lsa);

	return rc;
}

static int cxl_pmem_nvdimm_ctl(struct nvdimm *nvdimm, unsigned int cmd,
			       void *buf, unsigned int buf_len)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	unsigned long cmd_mask = nvdimm_cmd_mask(nvdimm);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);

	if (!test_bit(cmd, &cmd_mask))
		return -ENOTTY;

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		return cxl_pmem_get_config_size(mds, buf, buf_len);
	case ND_CMD_GET_CONFIG_DATA:
		return cxl_pmem_get_config_data(mds, buf, buf_len);
	case ND_CMD_SET_CONFIG_DATA:
		return cxl_pmem_set_config_data(mds, buf, buf_len);
	default:
		return -ENOTTY;
	}
}

static int cxl_pmem_ctl(struct nvdimm_bus_descriptor *nd_desc,
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
	return cxl_pmem_nvdimm_ctl(nvdimm, cmd, buf, buf_len);
}

static int detach_nvdimm(struct device *dev, void *data)
{
	struct cxl_nvdimm *cxl_nvd;
	bool release = false;

	if (!is_cxl_nvdimm(dev))
		return 0;

	scoped_guard(device, dev) {
		if (dev->driver) {
			cxl_nvd = to_cxl_nvdimm(dev);
			if (cxl_nvd->cxlmd && cxl_nvd->cxlmd->cxl_nvb == data)
				release = true;
		}
	}
	if (release)
		device_release_driver(dev);
	return 0;
}

static void unregister_nvdimm_bus(void *_cxl_nvb)
{
	struct cxl_nvdimm_bridge *cxl_nvb = _cxl_nvb;
	struct nvdimm_bus *nvdimm_bus = cxl_nvb->nvdimm_bus;

	bus_for_each_dev(&cxl_bus_type, NULL, cxl_nvb, detach_nvdimm);

	cxl_nvb->nvdimm_bus = NULL;
	nvdimm_bus_unregister(nvdimm_bus);
}

static int cxl_nvdimm_bridge_probe(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	cxl_nvb->nd_desc = (struct nvdimm_bus_descriptor) {
		.provider_name = "CXL",
		.module = THIS_MODULE,
		.ndctl = cxl_pmem_ctl,
	};

	cxl_nvb->nvdimm_bus =
		nvdimm_bus_register(&cxl_nvb->dev, &cxl_nvb->nd_desc);

	if (!cxl_nvb->nvdimm_bus)
		return -ENOMEM;

	return devm_add_action_or_reset(dev, unregister_nvdimm_bus, cxl_nvb);
}

static struct cxl_driver cxl_nvdimm_bridge_driver = {
	.name = "cxl_nvdimm_bridge",
	.probe = cxl_nvdimm_bridge_probe,
	.id = CXL_DEVICE_NVDIMM_BRIDGE,
	.drv = {
		.suppress_bind_attrs = true,
	},
};

static void unregister_nvdimm_region(void *nd_region)
{
	nvdimm_region_delete(nd_region);
}

static void cxlr_pmem_remove_resource(void *res)
{
	remove_resource(res);
}

struct cxl_pmem_region_info {
	u64 offset;
	u64 serial;
};

static int cxl_pmem_region_probe(struct device *dev)
{
	struct nd_mapping_desc mappings[CXL_DECODER_MAX_INTERLEAVE];
	struct cxl_pmem_region *cxlr_pmem = to_cxl_pmem_region(dev);
	struct cxl_region *cxlr = cxlr_pmem->cxlr;
	struct cxl_nvdimm_bridge *cxl_nvb = cxlr->cxl_nvb;
	struct cxl_pmem_region_info *info = NULL;
	struct nd_interleave_set *nd_set;
	struct nd_region_desc ndr_desc;
	struct cxl_nvdimm *cxl_nvd;
	struct nvdimm *nvdimm;
	struct resource *res;
	int rc, i = 0;

	memset(&mappings, 0, sizeof(mappings));
	memset(&ndr_desc, 0, sizeof(ndr_desc));

	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->name = "Persistent Memory";
	res->start = cxlr_pmem->hpa_range.start;
	res->end = cxlr_pmem->hpa_range.end;
	res->flags = IORESOURCE_MEM;
	res->desc = IORES_DESC_PERSISTENT_MEMORY;

	rc = insert_resource(&iomem_resource, res);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, cxlr_pmem_remove_resource, res);
	if (rc)
		return rc;

	ndr_desc.res = res;
	ndr_desc.provider_data = cxlr_pmem;

	ndr_desc.numa_node = memory_add_physaddr_to_nid(res->start);
	ndr_desc.target_node = phys_to_target_node(res->start);
	if (ndr_desc.target_node == NUMA_NO_NODE) {
		ndr_desc.target_node = ndr_desc.numa_node;
		dev_dbg(&cxlr->dev, "changing target node from %d to %d",
			NUMA_NO_NODE, ndr_desc.target_node);
	}

	nd_set = devm_kzalloc(dev, sizeof(*nd_set), GFP_KERNEL);
	if (!nd_set)
		return -ENOMEM;

	ndr_desc.memregion = cxlr->id;
	set_bit(ND_REGION_CXL, &ndr_desc.flags);
	set_bit(ND_REGION_PERSIST_MEMCTRL, &ndr_desc.flags);

	info = kmalloc_array(cxlr_pmem->nr_mappings, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	for (i = 0; i < cxlr_pmem->nr_mappings; i++) {
		struct cxl_pmem_region_mapping *m = &cxlr_pmem->mapping[i];
		struct cxl_memdev *cxlmd = m->cxlmd;
		struct cxl_dev_state *cxlds = cxlmd->cxlds;

		cxl_nvd = cxlmd->cxl_nvd;
		nvdimm = dev_get_drvdata(&cxl_nvd->dev);
		if (!nvdimm) {
			dev_dbg(dev, "[%d]: %s: no nvdimm found\n", i,
				dev_name(&cxlmd->dev));
			rc = -ENODEV;
			goto out_nvd;
		}

		m->cxl_nvd = cxl_nvd;
		mappings[i] = (struct nd_mapping_desc) {
			.nvdimm = nvdimm,
			.start = m->start,
			.size = m->size,
			.position = i,
		};
		info[i].offset = m->start;
		info[i].serial = cxlds->serial;
	}
	ndr_desc.num_mappings = cxlr_pmem->nr_mappings;
	ndr_desc.mapping = mappings;

	/*
	 * TODO enable CXL labels which skip the need for 'interleave-set cookie'
	 */
	nd_set->cookie1 =
		nd_fletcher64(info, sizeof(*info) * cxlr_pmem->nr_mappings, 0);
	nd_set->cookie2 = nd_set->cookie1;
	ndr_desc.nd_set = nd_set;

	cxlr_pmem->nd_region =
		nvdimm_pmem_region_create(cxl_nvb->nvdimm_bus, &ndr_desc);
	if (!cxlr_pmem->nd_region) {
		rc = -ENOMEM;
		goto out_nvd;
	}

	rc = devm_add_action_or_reset(dev, unregister_nvdimm_region,
				      cxlr_pmem->nd_region);
out_nvd:
	kfree(info);

	return rc;
}

static struct cxl_driver cxl_pmem_region_driver = {
	.name = "cxl_pmem_region",
	.probe = cxl_pmem_region_probe,
	.id = CXL_DEVICE_PMEM_REGION,
	.drv = {
		.suppress_bind_attrs = true,
	},
};

static __init int cxl_pmem_init(void)
{
	int rc;

	set_bit(CXL_MEM_COMMAND_ID_SET_SHUTDOWN_STATE, exclusive_cmds);
	set_bit(CXL_MEM_COMMAND_ID_SET_LSA, exclusive_cmds);

	rc = cxl_driver_register(&cxl_nvdimm_bridge_driver);
	if (rc)
		return rc;

	rc = cxl_driver_register(&cxl_nvdimm_driver);
	if (rc)
		goto err_nvdimm;

	rc = cxl_driver_register(&cxl_pmem_region_driver);
	if (rc)
		goto err_region;

	return 0;

err_region:
	cxl_driver_unregister(&cxl_nvdimm_driver);
err_nvdimm:
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
	return rc;
}

static __exit void cxl_pmem_exit(void)
{
	cxl_driver_unregister(&cxl_pmem_region_driver);
	cxl_driver_unregister(&cxl_nvdimm_driver);
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
}

MODULE_DESCRIPTION("CXL PMEM: Persistent Memory Support");
MODULE_LICENSE("GPL v2");
module_init(cxl_pmem_init);
module_exit(cxl_pmem_exit);
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM_BRIDGE);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM);
MODULE_ALIAS_CXL(CXL_DEVICE_PMEM_REGION);
