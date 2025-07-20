// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI K3 Cortex-M4 Remote Processor(s) driver
 *
 * Copyright (C) 2021-2024 Texas Instruments Incorporated - https://www.ti.com/
 *	Hari Nagalla <hnagalla@ti.com>
 */

#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"
#include "ti_sci_proc.h"
#include "ti_k3_common.h"

static const struct rproc_ops k3_m4_rproc_ops = {
	.prepare = k3_rproc_prepare,
	.unprepare = k3_rproc_unprepare,
	.start = k3_rproc_start,
	.stop = k3_rproc_stop,
	.attach = k3_rproc_attach,
	.detach = k3_rproc_detach,
	.kick = k3_rproc_kick,
	.da_to_va = k3_rproc_da_to_va,
	.get_loaded_rsc_table = k3_get_loaded_rsc_table,
};

static int k3_m4_rproc_probe(struct platform_device *pdev)
{
	const struct k3_rproc_dev_data *data;
	struct device *dev = &pdev->dev;
	struct k3_rproc *kproc;
	struct rproc *rproc;
	const char *fw_name;
	bool r_state = false;
	bool p_state = false;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse firmware-name property\n");

	rproc = devm_rproc_alloc(dev, dev_name(dev), &k3_m4_rproc_ops, fw_name,
				 sizeof(*kproc));
	if (!rproc)
		return -ENOMEM;

	rproc->has_iommu = false;
	rproc->recovery_disabled = true;
	kproc = rproc->priv;
	kproc->dev = dev;
	kproc->rproc = rproc;
	kproc->data = data;
	platform_set_drvdata(pdev, rproc);

	kproc->ti_sci = devm_ti_sci_get_by_phandle(dev, "ti,sci");
	if (IS_ERR(kproc->ti_sci))
		return dev_err_probe(dev, PTR_ERR(kproc->ti_sci),
				     "failed to get ti-sci handle\n");

	ret = of_property_read_u32(dev->of_node, "ti,sci-dev-id", &kproc->ti_sci_id);
	if (ret)
		return dev_err_probe(dev, ret, "missing 'ti,sci-dev-id' property\n");

	kproc->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(kproc->reset))
		return dev_err_probe(dev, PTR_ERR(kproc->reset), "failed to get reset\n");

	kproc->tsp = ti_sci_proc_of_get_tsp(dev, kproc->ti_sci);
	if (IS_ERR(kproc->tsp))
		return dev_err_probe(dev, PTR_ERR(kproc->tsp),
				     "failed to construct ti-sci proc control\n");

	ret = ti_sci_proc_request(kproc->tsp);
	if (ret < 0)
		return dev_err_probe(dev, ret, "ti_sci_proc_request failed\n");
	ret = devm_add_action_or_reset(dev, k3_release_tsp, kproc->tsp);
	if (ret)
		return ret;

	ret = k3_rproc_of_get_memories(pdev, kproc);
	if (ret)
		return ret;

	ret = k3_reserved_mem_init(kproc);
	if (ret)
		return dev_err_probe(dev, ret, "reserved memory init failed\n");

	ret = kproc->ti_sci->ops.dev_ops.is_on(kproc->ti_sci, kproc->ti_sci_id,
					       &r_state, &p_state);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get initial state, mode cannot be determined\n");

	/* configure devices for either remoteproc or IPC-only mode */
	if (p_state) {
		rproc->state = RPROC_DETACHED;
		dev_info(dev, "configured M4F for IPC-only mode\n");
	} else {
		dev_info(dev, "configured M4F for remoteproc mode\n");
	}

	ret = k3_rproc_request_mbox(rproc);
	if (ret)
		return ret;

	ret = devm_rproc_add(dev, rproc);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register device with remoteproc core\n");

	return 0;
}

static const struct k3_rproc_mem_data am64_m4_mems[] = {
	{ .name = "iram", .dev_addr = 0x0 },
	{ .name = "dram", .dev_addr = 0x30000 },
};

static const struct k3_rproc_dev_data am64_m4_data = {
	.mems = am64_m4_mems,
	.num_mems = ARRAY_SIZE(am64_m4_mems),
	.boot_align_addr = SZ_1K,
	.uses_lreset = true,
};

static const struct of_device_id k3_m4_of_match[] = {
	{ .compatible = "ti,am64-m4fss", .data = &am64_m4_data, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, k3_m4_of_match);

static struct platform_driver k3_m4_rproc_driver = {
	.probe	= k3_m4_rproc_probe,
	.driver	= {
		.name = "k3-m4-rproc",
		.of_match_table = k3_m4_of_match,
	},
};
module_platform_driver(k3_m4_rproc_driver);

MODULE_AUTHOR("Hari Nagalla <hnagalla@ti.com>");
MODULE_DESCRIPTION("TI K3 M4 Remoteproc driver");
MODULE_LICENSE("GPL");
