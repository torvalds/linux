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

/*
 * Custom function to translate a remote processor device address (internal
 * RAMs only) to a kernel virtual address.  The remote processors can access
 * their RAMs at either an internal address visible only from a remote
 * processor, or at the SoC-level bus address. Both these addresses need to be
 * looked through for translation. The translated addresses can be used either
 * by the remoteproc core for loading (when using kernel remoteproc loader), or
 * by any rpmsg bus drivers.
 */
static void *k3_m4_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct k3_rproc *kproc = rproc->priv;
	void __iomem *va = NULL;
	phys_addr_t bus_addr;
	u32 dev_addr, offset;
	size_t size;
	int i;

	if (len == 0)
		return NULL;

	for (i = 0; i < kproc->num_mems; i++) {
		bus_addr = kproc->mem[i].bus_addr;
		dev_addr = kproc->mem[i].dev_addr;
		size = kproc->mem[i].size;

		/* handle M4-view addresses */
		if (da >= dev_addr && ((da + len) <= (dev_addr + size))) {
			offset = da - dev_addr;
			va = kproc->mem[i].cpu_addr + offset;
			return (__force void *)va;
		}

		/* handle SoC-view addresses */
		if (da >= bus_addr && ((da + len) <= (bus_addr + size))) {
			offset = da - bus_addr;
			va = kproc->mem[i].cpu_addr + offset;
			return (__force void *)va;
		}
	}

	/* handle static DDR reserved memory regions */
	for (i = 0; i < kproc->num_rmems; i++) {
		dev_addr = kproc->rmem[i].dev_addr;
		size = kproc->rmem[i].size;

		if (da >= dev_addr && ((da + len) <= (dev_addr + size))) {
			offset = da - dev_addr;
			va = kproc->rmem[i].cpu_addr + offset;
			return (__force void *)va;
		}
	}

	return NULL;
}

static int k3_m4_rproc_of_get_memories(struct platform_device *pdev,
				       struct k3_rproc *kproc)
{
	const struct k3_rproc_dev_data *data = kproc->data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int num_mems;
	int i;

	num_mems = data->num_mems;
	kproc->mem = devm_kcalloc(kproc->dev, num_mems,
				  sizeof(*kproc->mem), GFP_KERNEL);
	if (!kproc->mem)
		return -ENOMEM;

	for (i = 0; i < num_mems; i++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   data->mems[i].name);
		if (!res) {
			dev_err(dev, "found no memory resource for %s\n",
				data->mems[i].name);
			return -EINVAL;
		}
		if (!devm_request_mem_region(dev, res->start,
					     resource_size(res),
					     dev_name(dev))) {
			dev_err(dev, "could not request %s region for resource\n",
				data->mems[i].name);
			return -EBUSY;
		}

		kproc->mem[i].cpu_addr = devm_ioremap_wc(dev, res->start,
							 resource_size(res));
		if (!kproc->mem[i].cpu_addr) {
			dev_err(dev, "failed to map %s memory\n",
				data->mems[i].name);
			return -ENOMEM;
		}
		kproc->mem[i].bus_addr = res->start;
		kproc->mem[i].dev_addr = data->mems[i].dev_addr;
		kproc->mem[i].size = resource_size(res);

		dev_dbg(dev, "memory %8s: bus addr %pa size 0x%zx va %pK da 0x%x\n",
			data->mems[i].name, &kproc->mem[i].bus_addr,
			kproc->mem[i].size, kproc->mem[i].cpu_addr,
			kproc->mem[i].dev_addr);
	}
	kproc->num_mems = num_mems;

	return 0;
}

static void k3_m4_rproc_dev_mem_release(void *data)
{
	struct device *dev = data;

	of_reserved_mem_device_release(dev);
}

static int k3_m4_reserved_mem_init(struct k3_rproc *kproc)
{
	struct device *dev = kproc->dev;
	struct device_node *np = dev->of_node;
	struct device_node *rmem_np;
	struct reserved_mem *rmem;
	int num_rmems;
	int ret, i;

	num_rmems = of_property_count_elems_of_size(np, "memory-region",
						    sizeof(phandle));
	if (num_rmems < 0) {
		dev_err(dev, "device does not reserved memory regions (%d)\n",
			num_rmems);
		return -EINVAL;
	}
	if (num_rmems < 2) {
		dev_err(dev, "device needs at least two memory regions to be defined, num = %d\n",
			num_rmems);
		return -EINVAL;
	}

	/* use reserved memory region 0 for vring DMA allocations */
	ret = of_reserved_mem_device_init_by_idx(dev, np, 0);
	if (ret) {
		dev_err(dev, "device cannot initialize DMA pool (%d)\n", ret);
		return ret;
	}
	ret = devm_add_action_or_reset(dev, k3_m4_rproc_dev_mem_release, dev);
	if (ret)
		return ret;

	num_rmems--;
	kproc->rmem = devm_kcalloc(dev, num_rmems, sizeof(*kproc->rmem), GFP_KERNEL);
	if (!kproc->rmem)
		return -ENOMEM;

	/* use remaining reserved memory regions for static carveouts */
	for (i = 0; i < num_rmems; i++) {
		rmem_np = of_parse_phandle(np, "memory-region", i + 1);
		if (!rmem_np)
			return -EINVAL;

		rmem = of_reserved_mem_lookup(rmem_np);
		of_node_put(rmem_np);
		if (!rmem)
			return -EINVAL;

		kproc->rmem[i].bus_addr = rmem->base;
		/* 64-bit address regions currently not supported */
		kproc->rmem[i].dev_addr = (u32)rmem->base;
		kproc->rmem[i].size = rmem->size;
		kproc->rmem[i].cpu_addr = devm_ioremap_wc(dev, rmem->base, rmem->size);
		if (!kproc->rmem[i].cpu_addr) {
			dev_err(dev, "failed to map reserved memory#%d at %pa of size %pa\n",
				i + 1, &rmem->base, &rmem->size);
			return -ENOMEM;
		}

		dev_dbg(dev, "reserved memory%d: bus addr %pa size 0x%zx va %pK da 0x%x\n",
			i + 1, &kproc->rmem[i].bus_addr,
			kproc->rmem[i].size, kproc->rmem[i].cpu_addr,
			kproc->rmem[i].dev_addr);
	}
	kproc->num_rmems = num_rmems;

	return 0;
}

static void k3_m4_release_tsp(void *data)
{
	struct ti_sci_proc *tsp = data;

	ti_sci_proc_release(tsp);
}

static const struct rproc_ops k3_m4_rproc_ops = {
	.prepare = k3_rproc_prepare,
	.unprepare = k3_rproc_unprepare,
	.start = k3_rproc_start,
	.stop = k3_rproc_stop,
	.attach = k3_rproc_attach,
	.detach = k3_rproc_detach,
	.kick = k3_rproc_kick,
	.da_to_va = k3_m4_rproc_da_to_va,
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
	ret = devm_add_action_or_reset(dev, k3_m4_release_tsp, kproc->tsp);
	if (ret)
		return ret;

	ret = k3_m4_rproc_of_get_memories(pdev, kproc);
	if (ret)
		return ret;

	ret = k3_m4_reserved_mem_init(kproc);
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
