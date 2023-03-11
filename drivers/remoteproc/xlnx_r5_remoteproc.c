// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP R5 Remote Processor driver
 *
 */

#include <dt-bindings/power/xlnx-zynqmp-power.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>

#include "remoteproc_internal.h"

/*
 * settings for RPU cluster mode which
 * reflects possible values of xlnx,cluster-mode dt-property
 */
enum zynqmp_r5_cluster_mode {
	SPLIT_MODE = 0, /* When cores run as separate processor */
	LOCKSTEP_MODE = 1, /* cores execute same code in lockstep,clk-for-clk */
	SINGLE_CPU_MODE = 2, /* core0 is held in reset and only core1 runs */
};

/**
 * struct mem_bank_data - Memory Bank description
 *
 * @addr: Start address of memory bank
 * @size: Size of Memory bank
 * @pm_domain_id: Power-domains id of memory bank for firmware to turn on/off
 * @bank_name: name of the bank for remoteproc framework
 */
struct mem_bank_data {
	phys_addr_t addr;
	size_t size;
	u32 pm_domain_id;
	char *bank_name;
};

/*
 * Hardcoded TCM bank values. This will be removed once TCM bindings are
 * accepted for system-dt specifications and upstreamed in linux kernel
 */
static const struct mem_bank_data zynqmp_tcm_banks[] = {
	{0xffe00000UL, 0x10000UL, PD_R5_0_ATCM, "atcm0"}, /* TCM 64KB each */
	{0xffe20000UL, 0x10000UL, PD_R5_0_BTCM, "btcm0"},
	{0xffe90000UL, 0x10000UL, PD_R5_1_ATCM, "atcm1"},
	{0xffeb0000UL, 0x10000UL, PD_R5_1_BTCM, "btcm1"},
};

/**
 * struct zynqmp_r5_core
 *
 * @dev: device of RPU instance
 * @np: device node of RPU instance
 * @tcm_bank_count: number TCM banks accessible to this RPU
 * @tcm_banks: array of each TCM bank data
 * @rproc: rproc handle
 * @pm_domain_id: RPU CPU power domain id
 */
struct zynqmp_r5_core {
	struct device *dev;
	struct device_node *np;
	int tcm_bank_count;
	struct mem_bank_data **tcm_banks;
	struct rproc *rproc;
	u32 pm_domain_id;
};

/**
 * struct zynqmp_r5_cluster
 *
 * @dev: r5f subsystem cluster device node
 * @mode: cluster mode of type zynqmp_r5_cluster_mode
 * @core_count: number of r5 cores used for this cluster mode
 * @r5_cores: Array of pointers pointing to r5 core
 */
struct zynqmp_r5_cluster {
	struct device *dev;
	enum  zynqmp_r5_cluster_mode mode;
	int core_count;
	struct zynqmp_r5_core **r5_cores;
};

/*
 * zynqmp_r5_set_mode()
 *
 * set RPU cluster and TCM operation mode
 *
 * @r5_core: pointer to zynqmp_r5_core type object
 * @fw_reg_val: value expected by firmware to configure RPU cluster mode
 * @tcm_mode: value expected by fw to configure TCM mode (lockstep or split)
 *
 * Return: 0 for success and < 0 for failure
 */
static int zynqmp_r5_set_mode(struct zynqmp_r5_core *r5_core,
			      enum rpu_oper_mode fw_reg_val,
			      enum rpu_tcm_comb tcm_mode)
{
	int ret;

	ret = zynqmp_pm_set_rpu_mode(r5_core->pm_domain_id, fw_reg_val);
	if (ret < 0) {
		dev_err(r5_core->dev, "failed to set RPU mode\n");
		return ret;
	}

	ret = zynqmp_pm_set_tcm_config(r5_core->pm_domain_id, tcm_mode);
	if (ret < 0)
		dev_err(r5_core->dev, "failed to configure TCM\n");

	return ret;
}

/*
 * zynqmp_r5_rproc_start()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Start R5 Core from designated boot address.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_start(struct rproc *rproc)
{
	struct zynqmp_r5_core *r5_core = rproc->priv;
	enum rpu_boot_mem bootmem;
	int ret;

	/*
	 * The exception vector pointers (EVP) refer to the base-address of
	 * exception vectors (for reset, IRQ, FIQ, etc). The reset-vector
	 * starts at the base-address and subsequent vectors are on 4-byte
	 * boundaries.
	 *
	 * Exception vectors can start either from 0x0000_0000 (LOVEC) or
	 * from 0xFFFF_0000 (HIVEC) which is mapped in the OCM (On-Chip Memory)
	 *
	 * Usually firmware will put Exception vectors at LOVEC.
	 *
	 * It is not recommend that you change the exception vector.
	 * Changing the EVP to HIVEC will result in increased interrupt latency
	 * and jitter. Also, if the OCM is secured and the Cortex-R5F processor
	 * is non-secured, then the Cortex-R5F processor cannot access the
	 * HIVEC exception vectors in the OCM.
	 */
	bootmem = (rproc->bootaddr >= 0xFFFC0000) ?
		   PM_RPU_BOOTMEM_HIVEC : PM_RPU_BOOTMEM_LOVEC;

	dev_dbg(r5_core->dev, "RPU boot addr 0x%llx from %s.", rproc->bootaddr,
		bootmem == PM_RPU_BOOTMEM_HIVEC ? "OCM" : "TCM");

	ret = zynqmp_pm_request_wake(r5_core->pm_domain_id, 1,
				     bootmem, ZYNQMP_PM_REQUEST_ACK_NO);
	if (ret)
		dev_err(r5_core->dev,
			"failed to start RPU = 0x%x\n", r5_core->pm_domain_id);
	return ret;
}

/*
 * zynqmp_r5_rproc_stop()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Power down  R5 Core.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_stop(struct rproc *rproc)
{
	struct zynqmp_r5_core *r5_core = rproc->priv;
	int ret;

	ret = zynqmp_pm_force_pwrdwn(r5_core->pm_domain_id,
				     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret)
		dev_err(r5_core->dev, "failed to stop remoteproc RPU %d\n", ret);

	return ret;
}

/*
 * zynqmp_r5_mem_region_map()
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem descriptor to map reserved memory-regions
 *
 * Callback to map va for memory-region's carveout.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_mem_region_map(struct rproc *rproc,
				    struct rproc_mem_entry *mem)
{
	void __iomem *va;

	va = ioremap_wc(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va))
		return -ENOMEM;

	mem->va = (void *)va;

	return 0;
}

/*
 * zynqmp_r5_rproc_mem_unmap
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem entry to unmap
 *
 * Unmap memory-region carveout
 *
 * return: always returns 0
 */
static int zynqmp_r5_mem_region_unmap(struct rproc *rproc,
				      struct rproc_mem_entry *mem)
{
	iounmap((void __iomem *)mem->va);
	return 0;
}

/*
 * add_mem_regions_carveout()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Construct rproc mem carveouts from memory-region property nodes
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int add_mem_regions_carveout(struct rproc *rproc)
{
	struct rproc_mem_entry *rproc_mem;
	struct zynqmp_r5_core *r5_core;
	struct of_phandle_iterator it;
	struct reserved_mem *rmem;
	int i = 0;

	r5_core = (struct zynqmp_r5_core *)rproc->priv;

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, r5_core->np, "memory-region", NULL, 0);

	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			of_node_put(it.node);
			dev_err(&rproc->dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		if (!strcmp(it.node->name, "vdev0buffer")) {
			/* Init reserved memory for vdev buffer */
			rproc_mem = rproc_of_resm_mem_entry_init(&rproc->dev, i,
								 rmem->size,
								 rmem->base,
								 it.node->name);
		} else {
			/* Register associated reserved memory regions */
			rproc_mem = rproc_mem_entry_init(&rproc->dev, NULL,
							 (dma_addr_t)rmem->base,
							 rmem->size, rmem->base,
							 zynqmp_r5_mem_region_map,
							 zynqmp_r5_mem_region_unmap,
							 it.node->name);
		}

		if (!rproc_mem) {
			of_node_put(it.node);
			return -ENOMEM;
		}

		rproc_add_carveout(rproc, rproc_mem);

		dev_dbg(&rproc->dev, "reserved mem carveout %s addr=%llx, size=0x%llx",
			it.node->name, rmem->base, rmem->size);
		i++;
	}

	return 0;
}

/*
 * tcm_mem_unmap()
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: tcm mem entry to unmap
 *
 * Unmap TCM banks when powering down R5 core.
 *
 * return always 0
 */
static int tcm_mem_unmap(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	iounmap((void __iomem *)mem->va);

	return 0;
}

/*
 * tcm_mem_map()
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: tcm memory entry descriptor
 *
 * Given TCM bank entry, this func setup virtual address for TCM bank
 * remoteproc carveout. It also takes care of va to da address translation
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int tcm_mem_map(struct rproc *rproc,
		       struct rproc_mem_entry *mem)
{
	void __iomem *va;

	va = ioremap_wc(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va))
		return -ENOMEM;

	/* Update memory entry va */
	mem->va = (void *)va;

	/* clear TCMs */
	memset_io(va, 0, mem->len);

	/*
	 * The R5s expect their TCM banks to be at address 0x0 and 0x2000,
	 * while on the Linux side they are at 0xffexxxxx.
	 *
	 * Zero out the high 12 bits of the address. This will give
	 * expected values for TCM Banks 0A and 0B (0x0 and 0x20000).
	 */
	mem->da &= 0x000fffff;

	/*
	 * TCM Banks 1A and 1B still have to be translated.
	 *
	 * Below handle these two banks' absolute addresses (0xffe90000 and
	 * 0xffeb0000) and convert to the expected relative addresses
	 * (0x0 and 0x20000).
	 */
	if (mem->da == 0x90000 || mem->da == 0xB0000)
		mem->da -= 0x90000;

	/* if translated TCM bank address is not valid report error */
	if (mem->da != 0x0 && mem->da != 0x20000) {
		dev_err(&rproc->dev, "invalid TCM address: %x\n", mem->da);
		return -EINVAL;
	}
	return 0;
}

/*
 * add_tcm_carveout_split_mode()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * allocate and add remoteproc carveout for TCM memory in split mode
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int add_tcm_carveout_split_mode(struct rproc *rproc)
{
	struct rproc_mem_entry *rproc_mem;
	struct zynqmp_r5_core *r5_core;
	int i, num_banks, ret;
	phys_addr_t bank_addr;
	struct device *dev;
	u32 pm_domain_id;
	size_t bank_size;
	char *bank_name;

	r5_core = (struct zynqmp_r5_core *)rproc->priv;
	dev = r5_core->dev;
	num_banks = r5_core->tcm_bank_count;

	/*
	 * Power-on Each 64KB TCM,
	 * register its address space, map and unmap functions
	 * and add carveouts accordingly
	 */
	for (i = 0; i < num_banks; i++) {
		bank_addr = r5_core->tcm_banks[i]->addr;
		bank_name = r5_core->tcm_banks[i]->bank_name;
		bank_size = r5_core->tcm_banks[i]->size;
		pm_domain_id = r5_core->tcm_banks[i]->pm_domain_id;

		ret = zynqmp_pm_request_node(pm_domain_id,
					     ZYNQMP_PM_CAPABILITY_ACCESS, 0,
					     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		if (ret < 0) {
			dev_err(dev, "failed to turn on TCM 0x%x", pm_domain_id);
			goto release_tcm_split;
		}

		dev_dbg(dev, "TCM carveout split mode %s addr=%llx, size=0x%lx",
			bank_name, bank_addr, bank_size);

		rproc_mem = rproc_mem_entry_init(dev, NULL, bank_addr,
						 bank_size, bank_addr,
						 tcm_mem_map, tcm_mem_unmap,
						 bank_name);
		if (!rproc_mem) {
			ret = -ENOMEM;
			zynqmp_pm_release_node(pm_domain_id);
			goto release_tcm_split;
		}

		rproc_add_carveout(rproc, rproc_mem);
	}

	return 0;

release_tcm_split:
	/* If failed, Turn off all TCM banks turned on before */
	for (i--; i >= 0; i--) {
		pm_domain_id = r5_core->tcm_banks[i]->pm_domain_id;
		zynqmp_pm_release_node(pm_domain_id);
	}
	return ret;
}

/*
 * add_tcm_carveout_lockstep_mode()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * allocate and add remoteproc carveout for TCM memory in lockstep mode
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int add_tcm_carveout_lockstep_mode(struct rproc *rproc)
{
	struct rproc_mem_entry *rproc_mem;
	struct zynqmp_r5_core *r5_core;
	int i, num_banks, ret;
	phys_addr_t bank_addr;
	size_t bank_size = 0;
	struct device *dev;
	u32 pm_domain_id;
	char *bank_name;

	r5_core = (struct zynqmp_r5_core *)rproc->priv;
	dev = r5_core->dev;

	/* Go through zynqmp banks for r5 node */
	num_banks = r5_core->tcm_bank_count;

	/*
	 * In lockstep mode, TCM is contiguous memory block
	 * However, each TCM block still needs to be enabled individually.
	 * So, Enable each TCM block individually, but add their size
	 * to create contiguous memory region.
	 */
	bank_addr = r5_core->tcm_banks[0]->addr;
	bank_name = r5_core->tcm_banks[0]->bank_name;

	for (i = 0; i < num_banks; i++) {
		bank_size += r5_core->tcm_banks[i]->size;
		pm_domain_id = r5_core->tcm_banks[i]->pm_domain_id;

		/* Turn on each TCM bank individually */
		ret = zynqmp_pm_request_node(pm_domain_id,
					     ZYNQMP_PM_CAPABILITY_ACCESS, 0,
					     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		if (ret < 0) {
			dev_err(dev, "failed to turn on TCM 0x%x", pm_domain_id);
			goto release_tcm_lockstep;
		}
	}

	dev_dbg(dev, "TCM add carveout lockstep mode %s addr=0x%llx, size=0x%lx",
		bank_name, bank_addr, bank_size);

	/* Register TCM address range, TCM map and unmap functions */
	rproc_mem = rproc_mem_entry_init(dev, NULL, bank_addr,
					 bank_size, bank_addr,
					 tcm_mem_map, tcm_mem_unmap,
					 bank_name);
	if (!rproc_mem) {
		ret = -ENOMEM;
		goto release_tcm_lockstep;
	}

	/* If registration is success, add carveouts */
	rproc_add_carveout(rproc, rproc_mem);

	return 0;

release_tcm_lockstep:
	/* If failed, Turn off all TCM banks turned on before */
	for (i--; i >= 0; i--) {
		pm_domain_id = r5_core->tcm_banks[i]->pm_domain_id;
		zynqmp_pm_release_node(pm_domain_id);
	}
	return ret;
}

/*
 * add_tcm_banks()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * allocate and add remoteproc carveouts for TCM memory based on cluster mode
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int add_tcm_banks(struct rproc *rproc)
{
	struct zynqmp_r5_cluster *cluster;
	struct zynqmp_r5_core *r5_core;
	struct device *dev;

	r5_core = (struct zynqmp_r5_core *)rproc->priv;
	if (!r5_core)
		return -EINVAL;

	dev = r5_core->dev;

	cluster = dev_get_drvdata(dev->parent);
	if (!cluster) {
		dev_err(dev->parent, "Invalid driver data\n");
		return -EINVAL;
	}

	/*
	 * In lockstep mode TCM banks are one contiguous memory region of 256Kb
	 * In split mode, each TCM bank is 64Kb and not contiguous.
	 * We add memory carveouts accordingly.
	 */
	if (cluster->mode == SPLIT_MODE)
		return add_tcm_carveout_split_mode(rproc);
	else if (cluster->mode == LOCKSTEP_MODE)
		return add_tcm_carveout_lockstep_mode(rproc);

	return -EINVAL;
}

/*
 * zynqmp_r5_parse_fw()
 * @rproc: single R5 core's corresponding rproc instance
 * @fw: ptr to firmware to be loaded onto r5 core
 *
 * get resource table if available
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		/*
		 * resource table only required for IPC.
		 * if not present, this is not necessarily an error;
		 * for example, loading r5 hello world application
		 * so simply inform user and keep going.
		 */
		dev_info(&rproc->dev, "no resource table found.\n");
		ret = 0;
	}
	return ret;
}

/**
 * zynqmp_r5_rproc_prepare()
 * adds carveouts for TCM bank and reserved memory regions
 *
 * @rproc: Device node of each rproc
 *
 * Return: 0 for success else < 0 error code
 */
static int zynqmp_r5_rproc_prepare(struct rproc *rproc)
{
	int ret;

	ret = add_tcm_banks(rproc);
	if (ret) {
		dev_err(&rproc->dev, "failed to get TCM banks, err %d\n", ret);
		return ret;
	}

	ret = add_mem_regions_carveout(rproc);
	if (ret) {
		dev_err(&rproc->dev, "failed to get reserve mem regions %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * zynqmp_r5_rproc_unprepare()
 * Turns off TCM banks using power-domain id
 *
 * @rproc: Device node of each rproc
 *
 * Return: always 0
 */
static int zynqmp_r5_rproc_unprepare(struct rproc *rproc)
{
	struct zynqmp_r5_core *r5_core;
	u32 pm_domain_id;
	int i;

	r5_core = (struct zynqmp_r5_core *)rproc->priv;

	for (i = 0; i < r5_core->tcm_bank_count; i++) {
		pm_domain_id = r5_core->tcm_banks[i]->pm_domain_id;
		if (zynqmp_pm_release_node(pm_domain_id))
			dev_warn(r5_core->dev,
				 "can't turn off TCM bank 0x%x", pm_domain_id);
	}

	return 0;
}

static const struct rproc_ops zynqmp_r5_rproc_ops = {
	.prepare	= zynqmp_r5_rproc_prepare,
	.unprepare	= zynqmp_r5_rproc_unprepare,
	.start		= zynqmp_r5_rproc_start,
	.stop		= zynqmp_r5_rproc_stop,
	.load		= rproc_elf_load_segments,
	.parse_fw	= zynqmp_r5_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
};

/**
 * zynqmp_r5_add_rproc_core()
 * Allocate and add struct rproc object for each r5f core
 * This is called for each individual r5f core
 *
 * @cdev: Device node of each r5 core
 *
 * Return: zynqmp_r5_core object for success else error code pointer
 */
static struct zynqmp_r5_core *zynqmp_r5_add_rproc_core(struct device *cdev)
{
	struct zynqmp_r5_core *r5_core;
	struct rproc *r5_rproc;
	int ret;

	/* Set up DMA mask */
	ret = dma_set_coherent_mask(cdev, DMA_BIT_MASK(32));
	if (ret)
		return ERR_PTR(ret);

	/* Allocate remoteproc instance */
	r5_rproc = rproc_alloc(cdev, dev_name(cdev),
			       &zynqmp_r5_rproc_ops,
			       NULL, sizeof(struct zynqmp_r5_core));
	if (!r5_rproc) {
		dev_err(cdev, "failed to allocate memory for rproc instance\n");
		return ERR_PTR(-ENOMEM);
	}

	r5_rproc->auto_boot = false;
	r5_core = (struct zynqmp_r5_core *)r5_rproc->priv;
	r5_core->dev = cdev;
	r5_core->np = dev_of_node(cdev);
	if (!r5_core->np) {
		dev_err(cdev, "can't get device node for r5 core\n");
		ret = -EINVAL;
		goto free_rproc;
	}

	/* Add R5 remoteproc core */
	ret = rproc_add(r5_rproc);
	if (ret) {
		dev_err(cdev, "failed to add r5 remoteproc\n");
		goto free_rproc;
	}

	r5_core->rproc = r5_rproc;
	return r5_core;

free_rproc:
	rproc_free(r5_rproc);
	return ERR_PTR(ret);
}

/**
 * zynqmp_r5_get_tcm_node()
 * Ideally this function should parse tcm node and store information
 * in r5_core instance. For now, Hardcoded TCM information is used.
 * This approach is used as TCM bindings for system-dt is being developed
 *
 * @cluster: pointer to zynqmp_r5_cluster type object
 *
 * Return: 0 for success and < 0 error code for failure.
 */
static int zynqmp_r5_get_tcm_node(struct zynqmp_r5_cluster *cluster)
{
	struct device *dev = cluster->dev;
	struct zynqmp_r5_core *r5_core;
	int tcm_bank_count, tcm_node;
	int i, j;

	tcm_bank_count = ARRAY_SIZE(zynqmp_tcm_banks);

	/* count per core tcm banks */
	tcm_bank_count = tcm_bank_count / cluster->core_count;

	/*
	 * r5 core 0 will use all of TCM banks in lockstep mode.
	 * In split mode, r5 core0 will use 128k and r5 core1 will use another
	 * 128k. Assign TCM banks to each core accordingly
	 */
	tcm_node = 0;
	for (i = 0; i < cluster->core_count; i++) {
		r5_core = cluster->r5_cores[i];
		r5_core->tcm_banks = devm_kcalloc(dev, tcm_bank_count,
						  sizeof(struct mem_bank_data *),
						  GFP_KERNEL);
		if (!r5_core->tcm_banks)
			return -ENOMEM;

		for (j = 0; j < tcm_bank_count; j++) {
			/*
			 * Use pre-defined TCM reg values.
			 * Eventually this should be replaced by values
			 * parsed from dts.
			 */
			r5_core->tcm_banks[j] =
				(struct mem_bank_data *)&zynqmp_tcm_banks[tcm_node];
			tcm_node++;
		}

		r5_core->tcm_bank_count = tcm_bank_count;
	}

	return 0;
}

/*
 * zynqmp_r5_core_init()
 * Create and initialize zynqmp_r5_core type object
 *
 * @cluster: pointer to zynqmp_r5_cluster type object
 * @fw_reg_val: value expected by firmware to configure RPU cluster mode
 * @tcm_mode: value expected by fw to configure TCM mode (lockstep or split)
 *
 * Return: 0 for success and error code for failure.
 */
static int zynqmp_r5_core_init(struct zynqmp_r5_cluster *cluster,
			       enum rpu_oper_mode fw_reg_val,
			       enum rpu_tcm_comb tcm_mode)
{
	struct device *dev = cluster->dev;
	struct zynqmp_r5_core *r5_core;
	int ret, i;

	ret = zynqmp_r5_get_tcm_node(cluster);
	if (ret < 0) {
		dev_err(dev, "can't get tcm node, err %d\n", ret);
		return ret;
	}

	for (i = 0; i < cluster->core_count; i++) {
		r5_core = cluster->r5_cores[i];

		/* Initialize r5 cores with power-domains parsed from dts */
		ret = of_property_read_u32_index(r5_core->np, "power-domains",
						 1, &r5_core->pm_domain_id);
		if (ret) {
			dev_err(dev, "failed to get power-domains property\n");
			return ret;
		}

		ret = zynqmp_r5_set_mode(r5_core, fw_reg_val, tcm_mode);
		if (ret) {
			dev_err(dev, "failed to set r5 cluster mode %d, err %d\n",
				cluster->mode, ret);
			return ret;
		}
	}

	return 0;
}

/*
 * zynqmp_r5_cluster_init()
 * Create and initialize zynqmp_r5_cluster type object
 *
 * @cluster: pointer to zynqmp_r5_cluster type object
 *
 * Return: 0 for success and error code for failure.
 */
static int zynqmp_r5_cluster_init(struct zynqmp_r5_cluster *cluster)
{
	enum zynqmp_r5_cluster_mode cluster_mode = LOCKSTEP_MODE;
	struct device *dev = cluster->dev;
	struct device_node *dev_node = dev_of_node(dev);
	struct platform_device *child_pdev;
	struct zynqmp_r5_core **r5_cores;
	enum rpu_oper_mode fw_reg_val;
	struct device **child_devs;
	struct device_node *child;
	enum rpu_tcm_comb tcm_mode;
	int core_count, ret, i;

	ret = of_property_read_u32(dev_node, "xlnx,cluster-mode", &cluster_mode);

	/*
	 * on success returns 0, if not defined then returns -EINVAL,
	 * In that case, default is LOCKSTEP mode. Other than that
	 * returns relative error code < 0.
	 */
	if (ret != -EINVAL && ret != 0) {
		dev_err(dev, "Invalid xlnx,cluster-mode property\n");
		return ret;
	}

	/*
	 * For now driver only supports split mode and lockstep mode.
	 * fail driver probe if either of that is not set in dts.
	 */
	if (cluster_mode == LOCKSTEP_MODE) {
		tcm_mode = PM_RPU_TCM_COMB;
		fw_reg_val = PM_RPU_MODE_LOCKSTEP;
	} else if (cluster_mode == SPLIT_MODE) {
		tcm_mode = PM_RPU_TCM_SPLIT;
		fw_reg_val = PM_RPU_MODE_SPLIT;
	} else {
		dev_err(dev, "driver does not support cluster mode %d\n", cluster_mode);
		return -EINVAL;
	}

	/*
	 * Number of cores is decided by number of child nodes of
	 * r5f subsystem node in dts. If Split mode is used in dts
	 * 2 child nodes are expected.
	 * In lockstep mode if two child nodes are available,
	 * only use first child node and consider it as core0
	 * and ignore core1 dt node.
	 */
	core_count = of_get_available_child_count(dev_node);
	if (core_count == 0) {
		dev_err(dev, "Invalid number of r5 cores %d", core_count);
		return -EINVAL;
	} else if (cluster_mode == SPLIT_MODE && core_count != 2) {
		dev_err(dev, "Invalid number of r5 cores for split mode\n");
		return -EINVAL;
	} else if (cluster_mode == LOCKSTEP_MODE && core_count == 2) {
		dev_warn(dev, "Only r5 core0 will be used\n");
		core_count = 1;
	}

	child_devs = kcalloc(core_count, sizeof(struct device *), GFP_KERNEL);
	if (!child_devs)
		return -ENOMEM;

	r5_cores = kcalloc(core_count,
			   sizeof(struct zynqmp_r5_core *), GFP_KERNEL);
	if (!r5_cores) {
		kfree(child_devs);
		return -ENOMEM;
	}

	i = 0;
	for_each_available_child_of_node(dev_node, child) {
		child_pdev = of_find_device_by_node(child);
		if (!child_pdev) {
			of_node_put(child);
			ret = -ENODEV;
			goto release_r5_cores;
		}

		child_devs[i] = &child_pdev->dev;

		/* create and add remoteproc instance of type struct rproc */
		r5_cores[i] = zynqmp_r5_add_rproc_core(&child_pdev->dev);
		if (IS_ERR(r5_cores[i])) {
			of_node_put(child);
			ret = PTR_ERR(r5_cores[i]);
			r5_cores[i] = NULL;
			goto release_r5_cores;
		}

		/*
		 * If two child nodes are available in dts in lockstep mode,
		 * then ignore second child node.
		 */
		if (cluster_mode == LOCKSTEP_MODE) {
			of_node_put(child);
			break;
		}

		i++;
	}

	cluster->mode = cluster_mode;
	cluster->core_count = core_count;
	cluster->r5_cores = r5_cores;

	ret = zynqmp_r5_core_init(cluster, fw_reg_val, tcm_mode);
	if (ret < 0) {
		dev_err(dev, "failed to init r5 core err %d\n", ret);
		cluster->core_count = 0;
		cluster->r5_cores = NULL;

		/*
		 * at this point rproc resources for each core are allocated.
		 * adjust index to free resources in reverse order
		 */
		i = core_count - 1;
		goto release_r5_cores;
	}

	kfree(child_devs);
	return 0;

release_r5_cores:
	while (i >= 0) {
		put_device(child_devs[i]);
		if (r5_cores[i]) {
			of_reserved_mem_device_release(r5_cores[i]->dev);
			rproc_del(r5_cores[i]->rproc);
			rproc_free(r5_cores[i]->rproc);
		}
		i--;
	}
	kfree(r5_cores);
	kfree(child_devs);
	return ret;
}

static void zynqmp_r5_cluster_exit(void *data)
{
	struct platform_device *pdev = (struct platform_device *)data;
	struct zynqmp_r5_cluster *cluster;
	struct zynqmp_r5_core *r5_core;
	int i;

	cluster = (struct zynqmp_r5_cluster *)platform_get_drvdata(pdev);
	if (!cluster)
		return;

	for (i = 0; i < cluster->core_count; i++) {
		r5_core = cluster->r5_cores[i];
		of_reserved_mem_device_release(r5_core->dev);
		put_device(r5_core->dev);
		rproc_del(r5_core->rproc);
		rproc_free(r5_core->rproc);
	}

	kfree(cluster->r5_cores);
	kfree(cluster);
	platform_set_drvdata(pdev, NULL);
}

/*
 * zynqmp_r5_remoteproc_probe()
 * parse device-tree, initialize hardware and allocate required resources
 * and remoteproc ops
 *
 * @pdev: domain platform device for R5 cluster
 *
 * Return: 0 for success and < 0 for failure.
 */
static int zynqmp_r5_remoteproc_probe(struct platform_device *pdev)
{
	struct zynqmp_r5_cluster *cluster;
	struct device *dev = &pdev->dev;
	int ret;

	cluster = kzalloc(sizeof(*cluster), GFP_KERNEL);
	if (!cluster)
		return -ENOMEM;

	cluster->dev = dev;

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to populate platform dev\n");
		kfree(cluster);
		return ret;
	}

	/* wire in so each core can be cleaned up at driver remove */
	platform_set_drvdata(pdev, cluster);

	ret = zynqmp_r5_cluster_init(cluster);
	if (ret) {
		kfree(cluster);
		platform_set_drvdata(pdev, NULL);
		dev_err_probe(dev, ret, "Invalid r5f subsystem device tree\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, zynqmp_r5_cluster_exit, pdev);
	if (ret)
		return ret;

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id zynqmp_r5_remoteproc_match[] = {
	{ .compatible = "xlnx,zynqmp-r5fss", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_r5_remoteproc_match);

static struct platform_driver zynqmp_r5_remoteproc_driver = {
	.probe = zynqmp_r5_remoteproc_probe,
	.driver = {
		.name = "zynqmp_r5_remoteproc",
		.of_match_table = zynqmp_r5_remoteproc_match,
	},
};
module_platform_driver(zynqmp_r5_remoteproc_driver);

MODULE_DESCRIPTION("Xilinx R5F remote processor driver");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL");
