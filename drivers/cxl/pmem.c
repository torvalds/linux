// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <asm/unaligned.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/ndctl.h>
#include <linux/async.h>
#include <linux/slab.h>
#include <linux/nd.h>
#include "cxlmem.h"
#include "cxl.h"

/*
 * Ordered workqueue for cxl nvdimm device arrival and departure
 * to coordinate bus rescans when a bridge arrives and trigger remove
 * operations when the bridge is removed.
 */
static struct workqueue_struct *cxl_pmem_wq;

static __read_mostly DECLARE_BITMAP(exclusive_cmds, CXL_MEM_COMMAND_ID_MAX);

static void clear_exclusive(void *cxlds)
{
	clear_exclusive_cxl_commands(cxlds, exclusive_cmds);
}

static void unregister_nvdimm(void *nvdimm)
{
	struct cxl_nvdimm *cxl_nvd = nvdimm_provider_data(nvdimm);
	struct cxl_nvdimm_bridge *cxl_nvb = cxl_nvd->bridge;
	struct cxl_pmem_region *cxlr_pmem;

	device_lock(&cxl_nvb->dev);
	cxlr_pmem = cxl_nvd->region;
	dev_set_drvdata(&cxl_nvd->dev, NULL);
	cxl_nvd->region = NULL;
	device_unlock(&cxl_nvb->dev);

	if (cxlr_pmem) {
		device_release_driver(&cxlr_pmem->dev);
		put_device(&cxlr_pmem->dev);
	}

	nvdimm_delete(nvdimm);
	cxl_nvd->bridge = NULL;
}

static int cxl_nvdimm_probe(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	unsigned long flags = 0, cmd_mask = 0;
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct nvdimm *nvdimm;
	int rc;

	cxl_nvb = cxl_find_nvdimm_bridge(dev);
	if (!cxl_nvb)
		return -ENXIO;

	device_lock(&cxl_nvb->dev);
	if (!cxl_nvb->nvdimm_bus) {
		rc = -ENXIO;
		goto out;
	}

	set_exclusive_cxl_commands(cxlds, exclusive_cmds);
	rc = devm_add_action_or_reset(dev, clear_exclusive, cxlds);
	if (rc)
		goto out;

	set_bit(NDD_LABELING, &flags);
	set_bit(ND_CMD_GET_CONFIG_SIZE, &cmd_mask);
	set_bit(ND_CMD_GET_CONFIG_DATA, &cmd_mask);
	set_bit(ND_CMD_SET_CONFIG_DATA, &cmd_mask);
	nvdimm = nvdimm_create(cxl_nvb->nvdimm_bus, cxl_nvd, NULL, flags,
			       cmd_mask, 0, NULL);
	if (!nvdimm) {
		rc = -ENOMEM;
		goto out;
	}

	dev_set_drvdata(dev, nvdimm);
	cxl_nvd->bridge = cxl_nvb;
	rc = devm_add_action_or_reset(dev, unregister_nvdimm, nvdimm);
out:
	device_unlock(&cxl_nvb->dev);
	put_device(&cxl_nvb->dev);

	return rc;
}

static struct cxl_driver cxl_nvdimm_driver = {
	.name = "cxl_nvdimm",
	.probe = cxl_nvdimm_probe,
	.id = CXL_DEVICE_NVDIMM,
};

static int cxl_pmem_get_config_size(struct cxl_dev_state *cxlds,
				    struct nd_cmd_get_config_size *cmd,
				    unsigned int buf_len)
{
	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	*cmd = (struct nd_cmd_get_config_size) {
		 .config_size = cxlds->lsa_size,
		 .max_xfer = cxlds->payload_size,
	};

	return 0;
}

static int cxl_pmem_get_config_data(struct cxl_dev_state *cxlds,
				    struct nd_cmd_get_config_data_hdr *cmd,
				    unsigned int buf_len)
{
	struct cxl_mbox_get_lsa get_lsa;
	int rc;

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;
	if (struct_size(cmd, out_buf, cmd->in_length) > buf_len)
		return -EINVAL;

	get_lsa = (struct cxl_mbox_get_lsa) {
		.offset = cpu_to_le32(cmd->in_offset),
		.length = cpu_to_le32(cmd->in_length),
	};

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_GET_LSA, &get_lsa,
			       sizeof(get_lsa), cmd->out_buf, cmd->in_length);
	cmd->status = 0;

	return rc;
}

static int cxl_pmem_set_config_data(struct cxl_dev_state *cxlds,
				    struct nd_cmd_set_config_hdr *cmd,
				    unsigned int buf_len)
{
	struct cxl_mbox_set_lsa *set_lsa;
	int rc;

	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	/* 4-byte status follows the input data in the payload */
	if (struct_size(cmd, in_buf, cmd->in_length) + 4 > buf_len)
		return -EINVAL;

	set_lsa =
		kvzalloc(struct_size(set_lsa, data, cmd->in_length), GFP_KERNEL);
	if (!set_lsa)
		return -ENOMEM;

	*set_lsa = (struct cxl_mbox_set_lsa) {
		.offset = cpu_to_le32(cmd->in_offset),
	};
	memcpy(set_lsa->data, cmd->in_buf, cmd->in_length);

	rc = cxl_mbox_send_cmd(cxlds, CXL_MBOX_OP_SET_LSA, set_lsa,
			       struct_size(set_lsa, data, cmd->in_length),
			       NULL, 0);

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
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	if (!test_bit(cmd, &cmd_mask))
		return -ENOTTY;

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		return cxl_pmem_get_config_size(cxlds, buf, buf_len);
	case ND_CMD_GET_CONFIG_DATA:
		return cxl_pmem_get_config_data(cxlds, buf, buf_len);
	case ND_CMD_SET_CONFIG_DATA:
		return cxl_pmem_set_config_data(cxlds, buf, buf_len);
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

static bool online_nvdimm_bus(struct cxl_nvdimm_bridge *cxl_nvb)
{
	if (cxl_nvb->nvdimm_bus)
		return true;
	cxl_nvb->nvdimm_bus =
		nvdimm_bus_register(&cxl_nvb->dev, &cxl_nvb->nd_desc);
	return cxl_nvb->nvdimm_bus != NULL;
}

static int cxl_nvdimm_release_driver(struct device *dev, void *cxl_nvb)
{
	struct cxl_nvdimm *cxl_nvd;

	if (!is_cxl_nvdimm(dev))
		return 0;

	cxl_nvd = to_cxl_nvdimm(dev);
	if (cxl_nvd->bridge != cxl_nvb)
		return 0;

	device_release_driver(dev);
	return 0;
}

static int cxl_pmem_region_release_driver(struct device *dev, void *cxl_nvb)
{
	struct cxl_pmem_region *cxlr_pmem;

	if (!is_cxl_pmem_region(dev))
		return 0;

	cxlr_pmem = to_cxl_pmem_region(dev);
	if (cxlr_pmem->bridge != cxl_nvb)
		return 0;

	device_release_driver(dev);
	return 0;
}

static void offline_nvdimm_bus(struct cxl_nvdimm_bridge *cxl_nvb,
			       struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;

	/*
	 * Set the state of cxl_nvdimm devices to unbound / idle before
	 * nvdimm_bus_unregister() rips the nvdimm objects out from
	 * underneath them.
	 */
	bus_for_each_dev(&cxl_bus_type, NULL, cxl_nvb,
			 cxl_pmem_region_release_driver);
	bus_for_each_dev(&cxl_bus_type, NULL, cxl_nvb,
			 cxl_nvdimm_release_driver);
	nvdimm_bus_unregister(nvdimm_bus);
}

static void cxl_nvb_update_state(struct work_struct *work)
{
	struct cxl_nvdimm_bridge *cxl_nvb =
		container_of(work, typeof(*cxl_nvb), state_work);
	struct nvdimm_bus *victim_bus = NULL;
	bool release = false, rescan = false;

	device_lock(&cxl_nvb->dev);
	switch (cxl_nvb->state) {
	case CXL_NVB_ONLINE:
		if (!online_nvdimm_bus(cxl_nvb)) {
			dev_err(&cxl_nvb->dev,
				"failed to establish nvdimm bus\n");
			release = true;
		} else
			rescan = true;
		break;
	case CXL_NVB_OFFLINE:
	case CXL_NVB_DEAD:
		victim_bus = cxl_nvb->nvdimm_bus;
		cxl_nvb->nvdimm_bus = NULL;
		break;
	default:
		break;
	}
	device_unlock(&cxl_nvb->dev);

	if (release)
		device_release_driver(&cxl_nvb->dev);
	if (rescan) {
		int rc = bus_rescan_devices(&cxl_bus_type);

		dev_dbg(&cxl_nvb->dev, "rescan: %d\n", rc);
	}
	offline_nvdimm_bus(cxl_nvb, victim_bus);

	put_device(&cxl_nvb->dev);
}

static void cxl_nvdimm_bridge_state_work(struct cxl_nvdimm_bridge *cxl_nvb)
{
	/*
	 * Take a reference that the workqueue will drop if new work
	 * gets queued.
	 */
	get_device(&cxl_nvb->dev);
	if (!queue_work(cxl_pmem_wq, &cxl_nvb->state_work))
		put_device(&cxl_nvb->dev);
}

static void cxl_nvdimm_bridge_remove(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	if (cxl_nvb->state == CXL_NVB_ONLINE)
		cxl_nvb->state = CXL_NVB_OFFLINE;
	cxl_nvdimm_bridge_state_work(cxl_nvb);
}

static int cxl_nvdimm_bridge_probe(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	if (cxl_nvb->state == CXL_NVB_DEAD)
		return -ENXIO;

	if (cxl_nvb->state == CXL_NVB_NEW) {
		cxl_nvb->nd_desc = (struct nvdimm_bus_descriptor) {
			.provider_name = "CXL",
			.module = THIS_MODULE,
			.ndctl = cxl_pmem_ctl,
		};

		INIT_WORK(&cxl_nvb->state_work, cxl_nvb_update_state);
	}

	cxl_nvb->state = CXL_NVB_ONLINE;
	cxl_nvdimm_bridge_state_work(cxl_nvb);

	return 0;
}

static struct cxl_driver cxl_nvdimm_bridge_driver = {
	.name = "cxl_nvdimm_bridge",
	.probe = cxl_nvdimm_bridge_probe,
	.remove = cxl_nvdimm_bridge_remove,
	.id = CXL_DEVICE_NVDIMM_BRIDGE,
};

static int match_cxl_nvdimm(struct device *dev, void *data)
{
	return is_cxl_nvdimm(dev);
}

static void unregister_nvdimm_region(void *nd_region)
{
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct cxl_pmem_region *cxlr_pmem;
	int i;

	cxlr_pmem = nd_region_provider_data(nd_region);
	cxl_nvb = cxlr_pmem->bridge;
	device_lock(&cxl_nvb->dev);
	for (i = 0; i < cxlr_pmem->nr_mappings; i++) {
		struct cxl_pmem_region_mapping *m = &cxlr_pmem->mapping[i];
		struct cxl_nvdimm *cxl_nvd = m->cxl_nvd;

		if (cxl_nvd->region) {
			put_device(&cxlr_pmem->dev);
			cxl_nvd->region = NULL;
		}
	}
	device_unlock(&cxl_nvb->dev);

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
	struct cxl_pmem_region_info *info = NULL;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct nd_interleave_set *nd_set;
	struct nd_region_desc ndr_desc;
	struct cxl_nvdimm *cxl_nvd;
	struct nvdimm *nvdimm;
	struct resource *res;
	int rc, i = 0;

	cxl_nvb = cxl_find_nvdimm_bridge(&cxlr_pmem->mapping[0].cxlmd->dev);
	if (!cxl_nvb) {
		dev_dbg(dev, "bridge not found\n");
		return -ENXIO;
	}
	cxlr_pmem->bridge = cxl_nvb;

	device_lock(&cxl_nvb->dev);
	if (!cxl_nvb->nvdimm_bus) {
		dev_dbg(dev, "nvdimm bus not found\n");
		rc = -ENXIO;
		goto err;
	}

	memset(&mappings, 0, sizeof(mappings));
	memset(&ndr_desc, 0, sizeof(ndr_desc));

	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res) {
		rc = -ENOMEM;
		goto err;
	}

	res->name = "Persistent Memory";
	res->start = cxlr_pmem->hpa_range.start;
	res->end = cxlr_pmem->hpa_range.end;
	res->flags = IORESOURCE_MEM;
	res->desc = IORES_DESC_PERSISTENT_MEMORY;

	rc = insert_resource(&iomem_resource, res);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(dev, cxlr_pmem_remove_resource, res);
	if (rc)
		goto err;

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
	if (!nd_set) {
		rc = -ENOMEM;
		goto err;
	}

	ndr_desc.memregion = cxlr->id;
	set_bit(ND_REGION_CXL, &ndr_desc.flags);
	set_bit(ND_REGION_PERSIST_MEMCTRL, &ndr_desc.flags);

	info = kmalloc_array(cxlr_pmem->nr_mappings, sizeof(*info), GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < cxlr_pmem->nr_mappings; i++) {
		struct cxl_pmem_region_mapping *m = &cxlr_pmem->mapping[i];
		struct cxl_memdev *cxlmd = m->cxlmd;
		struct cxl_dev_state *cxlds = cxlmd->cxlds;
		struct device *d;

		d = device_find_child(&cxlmd->dev, NULL, match_cxl_nvdimm);
		if (!d) {
			dev_dbg(dev, "[%d]: %s: no cxl_nvdimm found\n", i,
				dev_name(&cxlmd->dev));
			rc = -ENODEV;
			goto err;
		}

		/* safe to drop ref now with bridge lock held */
		put_device(d);

		cxl_nvd = to_cxl_nvdimm(d);
		nvdimm = dev_get_drvdata(&cxl_nvd->dev);
		if (!nvdimm) {
			dev_dbg(dev, "[%d]: %s: no nvdimm found\n", i,
				dev_name(&cxlmd->dev));
			rc = -ENODEV;
			goto err;
		}
		cxl_nvd->region = cxlr_pmem;
		get_device(&cxlr_pmem->dev);
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
		goto err;
	}

	rc = devm_add_action_or_reset(dev, unregister_nvdimm_region,
				      cxlr_pmem->nd_region);
out:
	kfree(info);
	device_unlock(&cxl_nvb->dev);
	put_device(&cxl_nvb->dev);

	return rc;

err:
	dev_dbg(dev, "failed to create nvdimm region\n");
	for (i--; i >= 0; i--) {
		nvdimm = mappings[i].nvdimm;
		cxl_nvd = nvdimm_provider_data(nvdimm);
		put_device(&cxl_nvd->region->dev);
		cxl_nvd->region = NULL;
	}
	goto out;
}

static struct cxl_driver cxl_pmem_region_driver = {
	.name = "cxl_pmem_region",
	.probe = cxl_pmem_region_probe,
	.id = CXL_DEVICE_PMEM_REGION,
};

/*
 * Return all bridges to the CXL_NVB_NEW state to invalidate any
 * ->state_work referring to the now destroyed cxl_pmem_wq.
 */
static int cxl_nvdimm_bridge_reset(struct device *dev, void *data)
{
	struct cxl_nvdimm_bridge *cxl_nvb;

	if (!is_cxl_nvdimm_bridge(dev))
		return 0;

	cxl_nvb = to_cxl_nvdimm_bridge(dev);
	device_lock(dev);
	cxl_nvb->state = CXL_NVB_NEW;
	device_unlock(dev);

	return 0;
}

static void destroy_cxl_pmem_wq(void)
{
	destroy_workqueue(cxl_pmem_wq);
	bus_for_each_dev(&cxl_bus_type, NULL, NULL, cxl_nvdimm_bridge_reset);
}

static __init int cxl_pmem_init(void)
{
	int rc;

	set_bit(CXL_MEM_COMMAND_ID_SET_SHUTDOWN_STATE, exclusive_cmds);
	set_bit(CXL_MEM_COMMAND_ID_SET_LSA, exclusive_cmds);

	cxl_pmem_wq = alloc_ordered_workqueue("cxl_pmem", 0);
	if (!cxl_pmem_wq)
		return -ENXIO;

	rc = cxl_driver_register(&cxl_nvdimm_bridge_driver);
	if (rc)
		goto err_bridge;

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
err_bridge:
	destroy_cxl_pmem_wq();
	return rc;
}

static __exit void cxl_pmem_exit(void)
{
	cxl_driver_unregister(&cxl_pmem_region_driver);
	cxl_driver_unregister(&cxl_nvdimm_driver);
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
	destroy_cxl_pmem_wq();
}

MODULE_LICENSE("GPL v2");
module_init(cxl_pmem_init);
module_exit(cxl_pmem_exit);
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM_BRIDGE);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM);
MODULE_ALIAS_CXL(CXL_DEVICE_PMEM_REGION);
