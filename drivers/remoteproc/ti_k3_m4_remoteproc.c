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

static int k3_m4_rproc_request_mbox(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct mbox_client *client = &kproc->client;
	struct device *dev = kproc->dev;
	int ret;

	client->dev = dev;
	client->tx_done = NULL;
	client->rx_callback = k3_rproc_mbox_callback;
	client->tx_block = false;
	client->knows_txdone = false;

	kproc->mbox = mbox_request_channel(client, 0);
	if (IS_ERR(kproc->mbox))
		return dev_err_probe(dev, PTR_ERR(kproc->mbox),
				     "mbox_request_channel failed\n");

	/*
	 * Ping the remote processor, this is only for sanity-sake for now;
	 * there is no functional effect whatsoever.
	 *
	 * Note that the reply will _not_ arrive immediately: this message
	 * will wait in the mailbox fifo until the remote processor is booted.
	 */
	ret = mbox_send_message(kproc->mbox, (void *)RP_MBOX_ECHO_REQUEST);
	if (ret < 0) {
		dev_err(dev, "mbox_send_message failed: %d\n", ret);
		mbox_free_channel(kproc->mbox);
		return ret;
	}

	return 0;
}

/*
 * The M4 cores have a local reset that affects only the CPU, and a
 * generic module reset that powers on the device and allows the internal
 * memories to be accessed while the local reset is asserted. This function is
 * used to release the global reset on remote cores to allow loading into the
 * internal RAMs. The .prepare() ops is invoked by remoteproc core before any
 * firmware loading, and is followed by the .start() ops after loading to
 * actually let the remote cores to run.
 */
static int k3_m4_rproc_prepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	int ret;

	/* If the core is running already no need to deassert the module reset */
	if (rproc->state == RPROC_DETACHED)
		return 0;

	/*
	 * Ensure the local reset is asserted so the core doesn't
	 * execute bogus code when the module reset is released.
	 */
	ret = k3_rproc_reset(kproc);
	if (ret)
		return ret;

	ret = reset_control_status(kproc->reset);
	if (ret <= 0) {
		dev_err(dev, "local reset still not asserted\n");
		return ret;
	}

	ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(dev, "could not deassert module-reset for internal RAM loading\n");
		return ret;
	}

	return 0;
}

/*
 * This function implements the .unprepare() ops and performs the complimentary
 * operations to that of the .prepare() ops. The function is used to assert the
 * global reset on applicable cores. This completes the second portion of
 * powering down the remote core. The cores themselves are only halted in the
 * .stop() callback through the local reset, and the .unprepare() ops is invoked
 * by the remoteproc core after the remoteproc is stopped to balance the global
 * reset.
 */
static int k3_m4_rproc_unprepare(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	int ret;

	/* If the core is going to be detached do not assert the module reset */
	if (rproc->state == RPROC_DETACHED)
		return 0;

	ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(dev, "module-reset assert failed\n");
		return ret;
	}

	return 0;
}

/*
 * This function implements the .get_loaded_rsc_table() callback and is used
 * to provide the resource table for a booted remote processor in IPC-only
 * mode. The remote processor firmwares follow a design-by-contract approach
 * and are expected to have the resource table at the base of the DDR region
 * reserved for firmware usage. This provides flexibility for the remote
 * processor to be booted by different bootloaders that may or may not have the
 * ability to publish the resource table address and size through a DT
 * property.
 */
static struct resource_table *k3_m4_get_loaded_rsc_table(struct rproc *rproc,
							 size_t *rsc_table_sz)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;

	if (!kproc->rmem[0].cpu_addr) {
		dev_err(dev, "memory-region #1 does not exist, loaded rsc table can't be found");
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * NOTE: The resource table size is currently hard-coded to a maximum
	 * of 256 bytes. The most common resource table usage for K3 firmwares
	 * is to only have the vdev resource entry and an optional trace entry.
	 * The exact size could be computed based on resource table address, but
	 * the hard-coded value suffices to support the IPC-only mode.
	 */
	*rsc_table_sz = 256;
	return (__force struct resource_table *)kproc->rmem[0].cpu_addr;
}

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

/*
 * Power up the M4 remote processor.
 *
 * This function will be invoked only after the firmware for this rproc
 * was loaded, parsed successfully, and all of its resource requirements
 * were met. This callback is invoked only in remoteproc mode.
 */
static int k3_m4_rproc_start(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_release(kproc);
}

/*
 * Stop the M4 remote processor.
 *
 * This function puts the M4 processor into reset, and finishes processing
 * of any pending messages. This callback is invoked only in remoteproc mode.
 */
static int k3_m4_rproc_stop(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_reset(kproc);
}

/*
 * Attach to a running M4 remote processor (IPC-only mode)
 *
 * The remote processor is already booted, so there is no need to issue any
 * TI-SCI commands to boot the M4 core. This callback is used only in IPC-only
 * mode.
 */
static int k3_m4_rproc_attach(struct rproc *rproc)
{
	return 0;
}

/*
 * Detach from a running M4 remote processor (IPC-only mode)
 *
 * This rproc detach callback performs the opposite operation to attach
 * callback, the M4 core is not stopped and will be left to continue to
 * run its booted firmware. This callback is invoked only in IPC-only mode.
 */
static int k3_m4_rproc_detach(struct rproc *rproc)
{
	return 0;
}

static const struct rproc_ops k3_m4_rproc_ops = {
	.prepare = k3_m4_rproc_prepare,
	.unprepare = k3_m4_rproc_unprepare,
	.start = k3_m4_rproc_start,
	.stop = k3_m4_rproc_stop,
	.attach = k3_m4_rproc_attach,
	.detach = k3_m4_rproc_detach,
	.kick = k3_rproc_kick,
	.da_to_va = k3_m4_rproc_da_to_va,
	.get_loaded_rsc_table = k3_m4_get_loaded_rsc_table,
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

	ret = k3_m4_rproc_request_mbox(rproc);
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
