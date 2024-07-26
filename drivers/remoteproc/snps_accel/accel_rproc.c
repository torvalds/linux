// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys VPX/NPX remoteporc driver
 *
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>

#include "../remoteproc_elf_helpers.h"
#include "../remoteproc_internal.h"

#include "accel_rproc.h"

static int snps_accel_rproc_prepare(struct rproc *rproc)
{
	struct snps_accel_rproc *aproc = rproc->priv;
	int i;

	/*
	 * If npu-cfg property is specified, setup NPU Cluster Network and
	 * powerup/reset cluster groups
	 */
	if (aproc->first_load) {
		if (aproc->data->setup_cluster)
			aproc->data->setup_cluster(aproc);
		aproc->first_load = 0;
	}

	/* Prepare code memory */
	for (i = 0; i < aproc->num_mems; i++) {
		if (aproc->mem[i].size)
			memset(aproc->mem[i].virt_addr, 0, aproc->mem[i].size);
	}

	return 0;
}

static int snps_accel_rproc_start(struct rproc *rproc)
{
	struct snps_accel_rproc *aproc = rproc->priv;

	if (aproc->data->start_core)
		aproc->data->start_core(aproc);

	return 0;
}

static int snps_accel_rproc_stop(struct rproc *rproc)
{
	struct snps_accel_rproc *aproc = rproc->priv;

	if (aproc->data->stop_core)
		aproc->data->stop_core(aproc);

	return 0;
}

/**
 * snps_accel_rproc_elf_load_segments() - load firmware segments to memory
 * @rproc: remote processor which will be booted using these fw segments
 * @fw: the ELF firmware image
 *
 * This function loads the firmware segments to memory, where the remote
 * processor expects them.
 *
 * Special version was added as a workaround to skip .shared_dram section load
 *
 * Return: 0 on success and an appropriate error code otherwise
 */
static int
snps_accel_rproc_elf_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	const void *ehdr, *phdr;
	int i, ret = 0;
	u16 phnum;
	const u8 *elf_data = fw->data;
	u8 class = fw_elf_get_class(fw);
	u32 elf_phdr_get_size = elf_size_of_phdr(class);
	struct snps_accel_rproc *aproc = rproc->priv;

	ehdr = elf_data;
	phnum = elf_hdr_get_e_phnum(class, ehdr);
	phdr = elf_data + elf_hdr_get_e_phoff(class, ehdr);

	/* go through the available ELF segments */
	for (i = 0; i < phnum; i++, phdr += elf_phdr_get_size) {
		u64 da = elf_phdr_get_p_paddr(class, phdr);
		u64 memsz = elf_phdr_get_p_memsz(class, phdr);
		u64 filesz = elf_phdr_get_p_filesz(class, phdr);
		u64 offset = elf_phdr_get_p_offset(class, phdr);
		u32 type = elf_phdr_get_p_type(class, phdr);
		u32 flags = elf_phdr_get_p_flags(class, phdr);
		bool is_iomem = false;
		void *ptr;

		if (type != PT_LOAD || !memsz || !filesz)
			continue;

		dev_dbg(dev, "phdr: type %d da 0x%llx memsz 0x%llx filesz 0x%llx\n",
			type, da, memsz, filesz);

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%llx memsz 0x%llx\n",
				filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev, "truncated fw: need 0x%llx avail 0x%zx\n",
				offset + filesz, fw->size);
			ret = -EINVAL;
			break;
		}

		if (!rproc_u64_fit_in_size_t(memsz)) {
			dev_err(dev, "size (%llx) does not fit in size_t type\n",
				memsz);
			ret = -EOVERFLOW;
			break;
		}

		/* grab the kernel address for this device address */
		ptr = rproc_da_to_va(rproc, da, memsz, &is_iomem);
		if (!ptr) {
			dev_err(dev, "bad phdr da 0x%llx mem 0x%llx\n", da,
				memsz);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		if (filesz)
			memcpy(ptr, elf_data + offset, filesz);

		/* expects to see vector table on top of the first segment */
		if (i == 0 && (flags & PF_X)) {
			aproc->ivt_base = da;
			dev_dbg(dev, "Found vector section: at addr 0x%llx\n", da);
		}
		/*
		 * Zero out remaining memory for this segment.
		 *
		 * This isn't strictly required since dma_alloc_coherent already
		 * did this for us. albeit harmless, we may consider removing
		 * this.
		 */
		if (memsz > filesz)
			memset(ptr + filesz, 0, memsz - filesz);
	}

	return ret;
}

/**
 * snps_accel_rproc_da_to_va() - internal memory translation helper
 * @rproc: remote processor to apply the address translation for
 * @da: device address to translate
 * @len: length of the memory buffer
 *
 * Custom function implementing the rproc .da_to_va ops to provide address
 * translation (device address to kernel virtual address) for shared SRAM
 * with VPX and NPX processors). The translated addresses can be used
 * either by the remoteproc core for loading, or by any rpmsg bus drivers.
 *
 * Return: translated virtual address in kernel memory space on success,
 *         or NULL on failure.
 */
static void *
snps_accel_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct snps_accel_rproc *aproc = rproc->priv;
	int i;
	u32 offset;

	if (len <= 0)
		return NULL;

	for (i = 0; i < aproc->num_mems; i++) {
		if (da >= aproc->mem[i].dev_addr &&
		    da + len <= aproc->mem[i].dev_addr + aproc->mem[i].size) {
			offset = da - aproc->mem[i].dev_addr;
			dev_dbg(aproc->device, "da_to_va: idx %d paddr %pap offset %x\n",
				i, &aproc->mem[i].phys_addr, offset);
			return (__force void *)(aproc->mem[i].virt_addr +
						offset);
		}
	}

	return NULL;
}

static const struct rproc_ops snps_accel_rproc_ops = {
	.prepare = snps_accel_rproc_prepare,
	.start = snps_accel_rproc_start,
	.stop = snps_accel_rproc_stop,
	.da_to_va = snps_accel_rproc_da_to_va,
	.get_boot_addr = rproc_elf_get_boot_addr,
	.load = snps_accel_rproc_elf_load_segments,
	.sanity_check = rproc_elf_sanity_check,
};

static void snps_accel_ranges_get_da_offset(struct device *dev, off_t *offset)
{
	const __be32 *prop;
	const __be32 *ranges_start;
	int ranges_len;
	int pa_cells;
	int da_cells;
	int size_cells;
	int tuple_len;
	struct device_node *pnode = dev->of_node->parent;
	u64 da_start;
	phys_addr_t pa_start;

	ranges_start = of_get_property(pnode, "ranges", &ranges_len);
	if (ranges_start == NULL) {
		dev_err(dev,
			"Missing ranges property for device tree node '%pOFn'\n",
			pnode);
		*offset = 0;
		return;
	}

	pa_cells = of_n_addr_cells(pnode);
	prop = of_get_property(pnode, "#address-cells", NULL);
	if (prop)
		da_cells = be32_to_cpup(prop);
	else
		da_cells = pa_cells;

	prop = of_get_property(pnode, "#size-cells", NULL);
	if (prop)
		size_cells = be32_to_cpup(prop);
	else
		size_cells = of_n_size_cells(pnode);

	tuple_len = (pa_cells + da_cells + size_cells) * sizeof(__be32);
	if (ranges_len % tuple_len != 0) {
		dev_err(dev, "Incorrect ranges property '%pOFn'\n", pnode);
		*offset = 0;
		return;
	}

	da_start = of_read_number(ranges_start, da_cells);
	pa_start = of_read_number(ranges_start + da_cells, pa_cells);

	*offset = da_start - pa_start;
}

static int snps_accel_rproc_of_get_mem(struct platform_device *pdev,
				     struct rproc *rproc)
{
	struct snps_accel_rproc *aproc = rproc->priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct resource shared_mem;
	off_t shm_da_offset;
	int num_mems;
	int ret;
	int i;

	/* Get accelerator aperture base */
	ret = of_address_to_resource(dev->of_node->parent, 0, &shared_mem);
	if (ret < 0) {
		dev_err(dev, "Accelerator shared memory area is not specified\n");
		return ret;
	}
	dev_dbg(dev, "Shared memory start %pap end %pap\n",
			&shared_mem.start, &shared_mem.end);

	num_mems = of_property_count_elems_of_size(dev->of_node, "reg", sizeof(u32)) / 2;
	if (num_mems < 0) {
		dev_err(dev, "Failed to get code memory regions for %pOF node\n",
			dev->of_node);
		return num_mems;
	}
	/*
	 * The driver calculates the shared memory DA->PA offset based on the
	 * values in the device tree property "ranges" in the top snps_accel
	 * node. If the "ranges" property is not present, the offset is assumed
	 * to be 0.
	 */
	snps_accel_ranges_get_da_offset(dev, &shm_da_offset);

	aproc->mem = devm_kcalloc(dev, num_mems, sizeof(*aproc->mem), GFP_KERNEL);
	if (!aproc->mem)
		return -ENOMEM;

	for (i = 0; i < num_mems; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);

		if (!res) {
			dev_err(dev, "No memory defined in reg idx %d\n", i);
			return -ENOMEM;
		}

		if (res->start + shm_da_offset < shared_mem.start ||
		    res->end + 1 + shm_da_offset > shared_mem.end + 1) {
			dev_err(dev, "Bad memory addr in a reg property (start %pap end %pap)\n",
				&res->start, &res->end);
			return -EINVAL;
		}

		aproc->mem[i].virt_addr = devm_memremap(dev, res->start,
							resource_size(res),
							MEMREMAP_WC);
		if (IS_ERR(aproc->mem[i].virt_addr)) {
			dev_err(dev, "Failed to map shared memory (%pap)\n",
				&res->start);
			return PTR_ERR(aproc->mem[i].virt_addr);
		}
		aproc->mem[i].dev_addr = res->start + shm_da_offset;
		aproc->mem[i].phys_addr = res->start;
		aproc->mem[i].size = resource_size(res);

		dev_dbg(dev, "mem[%d]: phys addr %pa size 0x%zx va %pS da 0x%x\n",
			i, &aproc->mem[i].phys_addr, aproc->mem[i].size,
			aproc->mem[i].virt_addr, aproc->mem[i].dev_addr);
	}
	aproc->num_mems = num_mems;

	return 0;
}

static int
snps_accel_rproc_init_ctrl_with_arcsync_fn(struct snps_accel_rproc *aproc,
					   struct device *arcsync_dev)
{
	const struct arcsync_funcs *arcsync_fn;
	struct snps_accel_rproc_ctrl_fn *ctrl_fn = &aproc->ctrl.fn;

	arcsync_fn = arcsync_get_ctrl_fn(arcsync_dev);
	if (IS_ERR(arcsync_fn))
		return PTR_ERR(arcsync_fn);

	ctrl_fn->clk_ctrl = arcsync_fn->clk_ctrl;
	ctrl_fn->power_ctrl = arcsync_fn->power_ctrl;
	ctrl_fn->reset = arcsync_fn->reset;
	ctrl_fn->start = arcsync_fn->start;
	ctrl_fn->halt = arcsync_fn->halt;
	ctrl_fn->set_ivt = arcsync_fn->set_ivt;
	ctrl_fn->get_status = arcsync_fn->get_status;
	ctrl_fn->reset_cluster_group = arcsync_fn->reset_cluster_group;
	ctrl_fn->clk_ctrl_cluster_group = arcsync_fn->clk_ctrl_cluster_group;
	ctrl_fn->power_ctrl_cluster_group = arcsync_fn->power_ctrl_cluster_group;

	aproc->ctrl.ver = arcsync_fn->get_version(arcsync_dev);
	aproc->ctrl.has_pmu = arcsync_fn->get_has_pmu(arcsync_dev);

	return 0;
}

static int snps_accel_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;
	const char *firmware_name = NULL;
	struct snps_accel_rproc *aproc;
	struct rproc *rproc;
	int ret;
	int i;

	ret = of_property_read_string(of_node, "firmware-name",
				      &firmware_name);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "Unable to read firmware-name\n");
		return ret;
	}

	rproc = devm_rproc_alloc(dev, of_node->name, &snps_accel_rproc_ops,
				firmware_name, sizeof(struct snps_accel_rproc));
	if (!rproc)
		return -ENOMEM;

	aproc = rproc->priv;
	aproc->rproc = rproc;
	aproc->device = dev;
	aproc->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, aproc);
	aproc->first_load = 1;

	/* Turns on/off auto_boot depending on snps,auto-boot property */
	rproc->auto_boot = of_property_read_bool(of_node, "snps,auto-boot");

	/* TODO: set flag to disable state change from sysfs
	 * In later kernels proc->sysfs_read_only appears for that.
	 * rproc->sysfs_read_only = true;
	 */

	/* Get Cores ID to work with */
	aproc->num_cores_start = of_property_count_u32_elems(of_node, "snps,arcsync-core-id");
	if (aproc->num_cores_start < 0) {
		dev_err(&pdev->dev, "Invalid or missing snps,arcsync-core-id property\n");
		return -EINVAL;
	}
	aproc->core_id = devm_kcalloc(dev, aproc->num_cores_start,
				sizeof(u32), GFP_KERNEL);
	if (!aproc->core_id)
		return -ENOMEM;

	for (i = 0; i < aproc->num_cores_start; i++) {
		ret = of_property_read_u32_index(of_node, "snps,arcsync-core-id",
						 i, &aproc->core_id[i]);
		if (ret < 0)
			return ret;
	}

	/* Get ARCsync device reference init ctrl func struct with arcsync funcs*/
	aproc->ctrl.dev = arcsync_get_device_by_phandle(of_node, "snps,arcsync-ctrl");
	if (IS_ERR(aproc->ctrl.dev)) {
		dev_err(dev,
			"Failed to get ARCSync ref: %ld\n",
			PTR_ERR(aproc->ctrl.dev));
		return PTR_ERR(aproc->ctrl.dev);
	}
	ret = snps_accel_rproc_init_ctrl_with_arcsync_fn(aproc, aproc->ctrl.dev);
	if (ret) {
		dev_err(dev, "Failed to get ARCSync funcs\n");
		return ret;
	}

	/* Get and map memory regions for firmware */
	ret = snps_accel_rproc_of_get_mem(pdev, rproc);
	if (ret)
		return ret;

	of_property_read_u32(of_node, "snps,arcsync-cluster-id", &aproc->cluster_id);

	/* Print some properties */
	dev_dbg(dev, "Cores to start: %d\n", aproc->num_cores_start);
	for (i = 0; i < aproc->num_cores_start; i++)
		dev_dbg(dev, "CoreID: 0x%x\n", aproc->core_id[i]);

	dev_dbg(dev, "ClusterID: %x\n", aproc->cluster_id);
	dev_dbg(dev, "Firmware-name: %s\n", rproc->firmware);

	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "Failed to register rproc\n");
		return ret;
	}

	return 0;
}

static int snps_accel_rproc_remove(struct platform_device *pdev)
{
	return 0;
}

static int
arcsync_wait_status_clr(struct snps_accel_rproc *aproc, u32 clid, u32 cid, u32 st)
{
	u32 count = 10;

	while ((aproc->ctrl.fn.get_status(aproc->ctrl.dev, clid, cid) & st) && --count)
		udelay(1);
	return count ? 0 : -EBUSY;
}

static int
arcsync_wait_status_set(struct snps_accel_rproc *aproc, u32 clid, u32 cid, u32 st)
{
	u32 count = 10;

	while (!(aproc->ctrl.fn.get_status(aproc->ctrl.dev, clid, cid) & st) && --count)
		udelay(1);
	return count ? 0 : -EBUSY;
}

static int
arcsync_start_core(struct snps_accel_rproc *aproc)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 status;
	int i;

	for (i = 0; i < aproc->num_cores_start; i++) {
		fn->reset(ctrl, aproc->cluster_id, aproc->core_id[i], ARCSYNC_RESET_ASSERT);
		fn->set_ivt(ctrl, aproc->cluster_id, aproc->core_id[i], aproc->ivt_base);
		status = fn->get_status(ctrl, aproc->cluster_id, aproc->core_id[i]);
		if (aproc->ctrl.has_pmu && (status & ARCSYNC_CORE_POWERDOWN)) {
			fn->clk_ctrl(ctrl, aproc->cluster_id,
				     aproc->core_id[i], ARCSYNC_CLK_DIS);
			fn->power_ctrl(ctrl, aproc->cluster_id,
				       aproc->core_id[i], ARCSYNC_POWER_UP);
			fn->clk_ctrl(ctrl, aproc->cluster_id,
				     aproc->core_id[i], ARCSYNC_CLK_EN);
			arcsync_wait_status_clr(aproc, aproc->cluster_id,
						aproc->core_id[i], ARCSYNC_CORE_POWERDOWN);
		} else {
			fn->clk_ctrl(ctrl, aproc->cluster_id, aproc->core_id[i], ARCSYNC_CLK_EN);
		}
		fn->reset(ctrl, aproc->cluster_id, aproc->core_id[i], ARCSYNC_RESET_DEASSERT);
		fn->start(ctrl, aproc->cluster_id, aproc->core_id[i]);
	}

	return 0;
}

static int arcsync_stop_core(struct snps_accel_rproc *aproc)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 status;
	int i;

	for (i = 0; i < aproc->num_cores_start; i++) {
		status = fn->get_status(ctrl, aproc->cluster_id, aproc->core_id[i]);
		if (aproc->ctrl.has_pmu && !(status & ARCSYNC_CORE_POWERDOWN)) {
			fn->halt(ctrl, aproc->cluster_id, aproc->core_id[i]);
			arcsync_wait_status_set(aproc, aproc->cluster_id,
						aproc->core_id[i],
						ARCSYNC_CORE_HALTED);
			fn->clk_ctrl(ctrl, aproc->cluster_id, aproc->core_id[i], ARCSYNC_CLK_DIS);
			fn->power_ctrl(ctrl, aproc->cluster_id, aproc->core_id[i],
				       ARCSYNC_POWER_DOWN);
			arcsync_wait_status_set(aproc, aproc->cluster_id,
						aproc->core_id[i],
						ARCSYNC_CORE_POWERDOWN);
		} else {
			fn->halt(ctrl, aproc->cluster_id, aproc->core_id[i]);
			fn->clk_ctrl(ctrl, aproc->cluster_id, aproc->core_id[i], ARCSYNC_CLK_DIS);
		}
	}

	return 0;
}

static const struct snps_accel_rproc_dev_data vpx_def_conf = {
	.setup_cluster		= NULL,
	.start_core		= arcsync_start_core,
	.stop_core		= arcsync_stop_core,
};

static const struct snps_accel_rproc_dev_data npx_def_conf = {
	.setup_cluster		= npx_setup_cluster_default,
	.start_core		= arcsync_start_core,
	.stop_core		= arcsync_stop_core,
};

static const struct of_device_id snps_accel_rproc_of_match[] = {
	{ .compatible = "snps,vpx-rproc", .data = &vpx_def_conf },
	{ .compatible = "snps,npx-rproc", .data = &npx_def_conf },
	{},
};

MODULE_DEVICE_TABLE(of, snps_accel_rproc_of_match);

static struct platform_driver snps_accel_rproc_driver = {
	.probe = snps_accel_rproc_probe,
	.remove = snps_accel_rproc_remove,
	.driver = {
		.name = "snps_accel_rproc",
		.of_match_table = snps_accel_rproc_of_match,
	},
};

module_platform_driver(snps_accel_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys VPX/NPX remote processor control driver");
MODULE_AUTHOR("Synopsys Inc.");
