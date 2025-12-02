// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI K3 Remote Processor(s) driver common code
 *
 * Refactored out of ti_k3_r5_remoteproc.c, ti_k3_dsp_remoteproc.c and
 * ti_k3_m4_remoteproc.c.
 *
 * ti_k3_r5_remoteproc.c:
 * Copyright (C) 2017-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_dsp_remoteproc.c:
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Suman Anna <s-anna@ti.com>
 *
 * ti_k3_m4_remoteproc.c:
 * Copyright (C) 2021-2024 Texas Instruments Incorporated - https://www.ti.com/
 *	Hari Nagalla <hnagalla@ti.com>
 */

#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/omap-mailbox.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"
#include "ti_sci_proc.h"
#include "ti_k3_common.h"

/**
 * k3_rproc_mbox_callback() - inbound mailbox message handler
 * @client: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * This handler is invoked by the K3 mailbox driver whenever a mailbox
 * message is received. Usually, the mailbox payload simply contains
 * the index of the virtqueue that is kicked by the remote processor,
 * and we let remoteproc core handle it.
 *
 * In addition to virtqueue indices, we also have some out-of-band values
 * that indicate different events. Those values are deliberately very
 * large so they don't coincide with virtqueue indices.
 */
void k3_rproc_mbox_callback(struct mbox_client *client, void *data)
{
	struct k3_rproc *kproc = container_of(client, struct k3_rproc, client);
	struct device *dev = kproc->rproc->dev.parent;
	struct rproc *rproc = kproc->rproc;
	u32 msg = (u32)(uintptr_t)(data);

	dev_dbg(dev, "mbox msg: 0x%x\n", msg);

	switch (msg) {
	case RP_MBOX_CRASH:
		/*
		 * remoteproc detected an exception, but error recovery is not
		 * supported. So, just log this for now
		 */
		dev_err(dev, "K3 rproc %s crashed\n", rproc->name);
		break;
	case RP_MBOX_ECHO_REPLY:
		dev_info(dev, "received echo reply from %s\n", rproc->name);
		break;
	default:
		/* silently handle all other valid messages */
		if (msg >= RP_MBOX_READY && msg < RP_MBOX_END_MSG)
			return;
		if (msg > rproc->max_notifyid) {
			dev_dbg(dev, "dropping unknown message 0x%x", msg);
			return;
		}
		/* msg contains the index of the triggered vring */
		if (rproc_vq_interrupt(rproc, msg) == IRQ_NONE)
			dev_dbg(dev, "no message was found in vqid %d\n", msg);
	}
}
EXPORT_SYMBOL_GPL(k3_rproc_mbox_callback);

/*
 * Kick the remote processor to notify about pending unprocessed messages.
 * The vqid usage is not used and is inconsequential, as the kick is performed
 * through a simulated GPIO (a bit in an IPC interrupt-triggering register),
 * the remote processor is expected to process both its Tx and Rx virtqueues.
 */
void k3_rproc_kick(struct rproc *rproc, int vqid)
{
	struct k3_rproc *kproc = rproc->priv;
	struct device *dev = kproc->dev;
	u32 msg = (u32)vqid;
	int ret;

	/*
	 * Send the index of the triggered virtqueue in the mailbox payload.
	 * NOTE: msg is cast to uintptr_t to prevent compiler warnings when
	 * void* is 64bit. It is safely cast back to u32 in the mailbox driver.
	 */
	ret = mbox_send_message(kproc->mbox, (void *)(uintptr_t)msg);
	if (ret < 0)
		dev_err(dev, "failed to send mailbox message, status = %d\n",
			ret);
}
EXPORT_SYMBOL_GPL(k3_rproc_kick);

/* Put the remote processor into reset */
int k3_rproc_reset(struct k3_rproc *kproc)
{
	struct device *dev = kproc->dev;
	int ret;

	if (kproc->data->uses_lreset) {
		ret = reset_control_assert(kproc->reset);
		if (ret)
			dev_err(dev, "local-reset assert failed (%pe)\n", ERR_PTR(ret));
	} else {
		ret = kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret)
			dev_err(dev, "module-reset assert failed (%pe)\n", ERR_PTR(ret));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_rproc_reset);

/* Release the remote processor from reset */
int k3_rproc_release(struct k3_rproc *kproc)
{
	struct device *dev = kproc->dev;
	int ret;

	if (kproc->data->uses_lreset) {
		ret = reset_control_deassert(kproc->reset);
		if (ret) {
			dev_err(dev, "local-reset deassert failed, (%pe)\n", ERR_PTR(ret));
			if (kproc->ti_sci->ops.dev_ops.put_device(kproc->ti_sci,
								  kproc->ti_sci_id))
				dev_warn(dev, "module-reset assert back failed\n");
		}
	} else {
		ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
							    kproc->ti_sci_id);
		if (ret)
			dev_err(dev, "module-reset deassert failed (%pe)\n", ERR_PTR(ret));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_rproc_release);

static void k3_rproc_free_channel(void *data)
{
	struct k3_rproc *kproc = data;

	mbox_free_channel(kproc->mbox);
}

int k3_rproc_request_mbox(struct rproc *rproc)
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

	ret = devm_add_action_or_reset(dev, k3_rproc_free_channel, kproc);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_request_mbox);

/*
 * The K3 DSP and M4 cores have a local reset that affects only the CPU, and a
 * generic module reset that powers on the device and allows the internal
 * memories to be accessed while the local reset is asserted. This function is
 * used to release the global reset on remote cores to allow loading into the
 * internal RAMs. The .prepare() ops is invoked by remoteproc core before any
 * firmware loading, and is followed by the .start() ops after loading to
 * actually let the remote cores to run.
 */
int k3_rproc_prepare(struct rproc *rproc)
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
	if (kproc->data->uses_lreset) {
		ret = k3_rproc_reset(kproc);
		if (ret)
			return ret;

		ret = reset_control_status(kproc->reset);
		if (ret <= 0) {
			dev_err(dev, "local reset still not asserted\n");
			return ret;
		}
	}

	ret = kproc->ti_sci->ops.dev_ops.get_device(kproc->ti_sci,
						    kproc->ti_sci_id);
	if (ret) {
		dev_err(dev, "could not deassert module-reset for internal RAM loading\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_prepare);

/*
 * This function implements the .unprepare() ops and performs the complimentary
 * operations to that of the .prepare() ops. The function is used to assert the
 * global reset on applicable K3 DSP and M4 cores. This completes the second
 * portion of powering down the remote core. The cores themselves are only
 * halted in the .stop() callback through the local reset, and the .unprepare()
 * ops is invoked by the remoteproc core after the remoteproc is stopped to
 * balance the global reset.
 */
int k3_rproc_unprepare(struct rproc *rproc)
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
EXPORT_SYMBOL_GPL(k3_rproc_unprepare);

/*
 * Power up the remote processor.
 *
 * This function will be invoked only after the firmware for this rproc
 * was loaded, parsed successfully, and all of its resource requirements
 * were met. This callback is invoked only in remoteproc mode.
 */
int k3_rproc_start(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_release(kproc);
}
EXPORT_SYMBOL_GPL(k3_rproc_start);

/*
 * Stop the remote processor.
 *
 * This function puts the remote processor into reset, and finishes processing
 * of any pending messages. This callback is invoked only in remoteproc mode.
 */
int k3_rproc_stop(struct rproc *rproc)
{
	struct k3_rproc *kproc = rproc->priv;

	return k3_rproc_reset(kproc);
}
EXPORT_SYMBOL_GPL(k3_rproc_stop);

/*
 * Attach to a running remote processor (IPC-only mode)
 *
 * The rproc attach callback is a NOP. The remote processor is already booted,
 * and all required resources have been acquired during probe routine, so there
 * is no need to issue any TI-SCI commands to boot the remote cores in IPC-only
 * mode. This callback is invoked only in IPC-only mode and exists because
 * rproc_validate() checks for its existence.
 */
int k3_rproc_attach(struct rproc *rproc) { return 0; }
EXPORT_SYMBOL_GPL(k3_rproc_attach);

/*
 * Detach from a running remote processor (IPC-only mode)
 *
 * The rproc detach callback is a NOP. The remote processor is not stopped and
 * will be left in booted state in IPC-only mode. This callback is invoked only
 * in IPC-only mode and exists for sanity sake
 */
int k3_rproc_detach(struct rproc *rproc) { return 0; }
EXPORT_SYMBOL_GPL(k3_rproc_detach);

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
struct resource_table *k3_get_loaded_rsc_table(struct rproc *rproc,
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
EXPORT_SYMBOL_GPL(k3_get_loaded_rsc_table);

/*
 * Custom function to translate a remote processor device address (internal
 * RAMs only) to a kernel virtual address.  The remote processors can access
 * their RAMs at either an internal address visible only from a remote
 * processor, or at the SoC-level bus address. Both these addresses need to be
 * looked through for translation. The translated addresses can be used either
 * by the remoteproc core for loading (when using kernel remoteproc loader), or
 * by any rpmsg bus drivers.
 */
void *k3_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
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

		/* handle rproc-view addresses */
		if (da >= dev_addr && ((da + len) <= (dev_addr + size))) {
			offset = da - dev_addr;
			va = kproc->mem[i].cpu_addr + offset;
			return (__force void *)va;
		}

		/* handle SoC-view addresses */
		if (da >= bus_addr && (da + len) <= (bus_addr + size)) {
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
EXPORT_SYMBOL_GPL(k3_rproc_da_to_va);

int k3_rproc_of_get_memories(struct platform_device *pdev,
			     struct k3_rproc *kproc)
{
	const struct k3_rproc_dev_data *data = kproc->data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int num_mems = 0;
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

		dev_dbg(dev, "memory %8s: bus addr %pa size 0x%zx va %p da 0x%x\n",
			data->mems[i].name, &kproc->mem[i].bus_addr,
			kproc->mem[i].size, kproc->mem[i].cpu_addr,
			kproc->mem[i].dev_addr);
	}
	kproc->num_mems = num_mems;

	return 0;
}
EXPORT_SYMBOL_GPL(k3_rproc_of_get_memories);

void k3_mem_release(void *data)
{
	struct device *dev = data;

	of_reserved_mem_device_release(dev);
}
EXPORT_SYMBOL_GPL(k3_mem_release);

int k3_reserved_mem_init(struct k3_rproc *kproc)
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
	ret = devm_add_action_or_reset(dev, k3_mem_release, dev);
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

		dev_dbg(dev, "reserved memory%d: bus addr %pa size 0x%zx va %p da 0x%x\n",
			i + 1, &kproc->rmem[i].bus_addr,
			kproc->rmem[i].size, kproc->rmem[i].cpu_addr,
			kproc->rmem[i].dev_addr);
	}
	kproc->num_rmems = num_rmems;

	return 0;
}
EXPORT_SYMBOL_GPL(k3_reserved_mem_init);

void k3_release_tsp(void *data)
{
	struct ti_sci_proc *tsp = data;

	ti_sci_proc_release(tsp);
}
EXPORT_SYMBOL_GPL(k3_release_tsp);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI K3 common Remoteproc code");
