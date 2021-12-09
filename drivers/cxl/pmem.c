// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/libnvdimm.h>
#include <asm/unaligned.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/ndctl.h>
#include <linux/async.h>
#include <linux/slab.h>
#include "cxlmem.h"
#include "cxl.h"

/*
 * Ordered workqueue for cxl nvdimm device arrival and departure
 * to coordinate bus rescans when a bridge arrives and trigger remove
 * operations when the bridge is removed.
 */
static struct workqueue_struct *cxl_pmem_wq;

static __read_mostly DECLARE_BITMAP(exclusive_cmds, CXL_MEM_COMMAND_ID_MAX);

static void clear_exclusive(void *cxlm)
{
	clear_exclusive_cxl_commands(cxlm, exclusive_cmds);
}

static void unregister_nvdimm(void *nvdimm)
{
	nvdimm_delete(nvdimm);
}

static int cxl_nvdimm_probe(struct device *dev)
{
	struct cxl_nvdimm *cxl_nvd = to_cxl_nvdimm(dev);
	struct cxl_memdev *cxlmd = cxl_nvd->cxlmd;
	unsigned long flags = 0, cmd_mask = 0;
	struct cxl_mem *cxlm = cxlmd->cxlm;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct nvdimm *nvdimm;
	int rc;

	cxl_nvb = cxl_find_nvdimm_bridge(cxl_nvd);
	if (!cxl_nvb)
		return -ENXIO;

	device_lock(&cxl_nvb->dev);
	if (!cxl_nvb->nvdimm_bus) {
		rc = -ENXIO;
		goto out;
	}

	set_exclusive_cxl_commands(cxlm, exclusive_cmds);
	rc = devm_add_action_or_reset(dev, clear_exclusive, cxlm);
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

static int cxl_pmem_get_config_size(struct cxl_mem *cxlm,
				    struct nd_cmd_get_config_size *cmd,
				    unsigned int buf_len)
{
	if (sizeof(*cmd) > buf_len)
		return -EINVAL;

	*cmd = (struct nd_cmd_get_config_size) {
		 .config_size = cxlm->lsa_size,
		 .max_xfer = cxlm->payload_size,
	};

	return 0;
}

static int cxl_pmem_get_config_data(struct cxl_mem *cxlm,
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
		.offset = cmd->in_offset,
		.length = cmd->in_length,
	};

	rc = cxl_mem_mbox_send_cmd(cxlm, CXL_MBOX_OP_GET_LSA, &get_lsa,
				   sizeof(get_lsa), cmd->out_buf,
				   cmd->in_length);
	cmd->status = 0;

	return rc;
}

static int cxl_pmem_set_config_data(struct cxl_mem *cxlm,
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
		.offset = cmd->in_offset,
	};
	memcpy(set_lsa->data, cmd->in_buf, cmd->in_length);

	rc = cxl_mem_mbox_send_cmd(cxlm, CXL_MBOX_OP_SET_LSA, set_lsa,
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
	struct cxl_mem *cxlm = cxlmd->cxlm;

	if (!test_bit(cmd, &cmd_mask))
		return -ENOTTY;

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		return cxl_pmem_get_config_size(cxlm, buf, buf_len);
	case ND_CMD_GET_CONFIG_DATA:
		return cxl_pmem_get_config_data(cxlm, buf, buf_len);
	case ND_CMD_SET_CONFIG_DATA:
		return cxl_pmem_set_config_data(cxlm, buf, buf_len);
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

static int cxl_nvdimm_release_driver(struct device *dev, void *data)
{
	if (!is_cxl_nvdimm(dev))
		return 0;
	device_release_driver(dev);
	return 0;
}

static void offline_nvdimm_bus(struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;

	/*
	 * Set the state of cxl_nvdimm devices to unbound / idle before
	 * nvdimm_bus_unregister() rips the nvdimm objects out from
	 * underneath them.
	 */
	bus_for_each_dev(&cxl_bus_type, NULL, NULL, cxl_nvdimm_release_driver);
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
	offline_nvdimm_bus(victim_bus);

	put_device(&cxl_nvb->dev);
}

static void cxl_nvdimm_bridge_remove(struct device *dev)
{
	struct cxl_nvdimm_bridge *cxl_nvb = to_cxl_nvdimm_bridge(dev);

	if (cxl_nvb->state == CXL_NVB_ONLINE)
		cxl_nvb->state = CXL_NVB_OFFLINE;
	if (queue_work(cxl_pmem_wq, &cxl_nvb->state_work))
		get_device(&cxl_nvb->dev);
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
	if (queue_work(cxl_pmem_wq, &cxl_nvb->state_work))
		get_device(&cxl_nvb->dev);

	return 0;
}

static struct cxl_driver cxl_nvdimm_bridge_driver = {
	.name = "cxl_nvdimm_bridge",
	.probe = cxl_nvdimm_bridge_probe,
	.remove = cxl_nvdimm_bridge_remove,
	.id = CXL_DEVICE_NVDIMM_BRIDGE,
};

static __init int cxl_pmem_init(void)
{
	int rc;

	set_bit(CXL_MEM_COMMAND_ID_SET_PARTITION_INFO, exclusive_cmds);
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

	return 0;

err_nvdimm:
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
err_bridge:
	destroy_workqueue(cxl_pmem_wq);
	return rc;
}

static __exit void cxl_pmem_exit(void)
{
	cxl_driver_unregister(&cxl_nvdimm_driver);
	cxl_driver_unregister(&cxl_nvdimm_bridge_driver);
	destroy_workqueue(cxl_pmem_wq);
}

MODULE_LICENSE("GPL v2");
module_init(cxl_pmem_init);
module_exit(cxl_pmem_exit);
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM_BRIDGE);
MODULE_ALIAS_CXL(CXL_DEVICE_NVDIMM);
