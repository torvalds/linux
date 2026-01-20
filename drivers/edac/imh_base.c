// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Intel(R) servers with Integrated Memory/IO Hub-based memory controller.
 * Copyright (c) 2025, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/mce.h>
#include <asm/cpu.h>
#include "edac_module.h"
#include "skx_common.h"

#define IMH_REVISION	"v0.0.1"
#define EDAC_MOD_STR	"imh_edac"

/* Debug macros */
#define imh_printk(level, fmt, arg...)	\
	edac_printk(level, "imh", fmt, ##arg)

/* Configuration Agent(Ubox) */
#define MMIO_BASE_H(reg)		(((u64)GET_BITFIELD(reg, 0, 29)) << 23)
#define SOCKET_ID(reg)			GET_BITFIELD(reg, 0, 3)

/* PUNIT */
#define DDR_IMC_BITMAP(reg)		GET_BITFIELD(reg, 23, 30)

/* Memory Controller */
#define ECC_ENABLED(reg)		GET_BITFIELD(reg, 2, 2)
#define DIMM_POPULATED(reg)		GET_BITFIELD(reg, 15, 15)

/* System Cache Agent(SCA) */
#define TOLM(reg)			(((u64)GET_BITFIELD(reg, 16, 31)) << 16)
#define TOHM(reg)			(((u64)GET_BITFIELD(reg, 16, 51)) << 16)

/* Home Agent (HA) */
#define NMCACHING(reg)			GET_BITFIELD(reg, 8, 8)

/**
 * struct local_reg - A register as described in the local package view.
 *
 * @pkg: (input)	The package where the register is located.
 * @pbase: (input)	The IP MMIO base physical address in the local package view.
 * @size: (input)	The IP MMIO size.
 * @offset: (input)	The register offset from the IP MMIO base @pbase.
 * @width: (input)	The register width in byte.
 * @vbase: (internal)	The IP MMIO base virtual address.
 * @val: (output)	The register value.
 */
struct local_reg {
	int pkg;
	u64 pbase;
	u32 size;
	u32 offset;
	u8  width;
	void __iomem *vbase;
	u64 val;
};

#define DEFINE_LOCAL_REG(name, cfg, package, north, ip_name, ip_idx, reg_name)	\
	struct local_reg name = {						\
		.pkg	= package,						\
		.pbase	= (north ? (cfg)->mmio_base_l_north :			\
			  (cfg)->mmio_base_l_south) +				\
			  (cfg)->ip_name##_base +				\
			  (cfg)->ip_name##_size * (ip_idx),			\
		.size	= (cfg)->ip_name##_size,				\
		.offset	= (cfg)->ip_name##_reg_##reg_name##_offset,		\
		.width	= (cfg)->ip_name##_reg_##reg_name##_width,		\
	}

static u64 readx(void __iomem *addr, u8 width)
{
	switch (width) {
	case 1:
		return readb(addr);
	case 2:
		return readw(addr);
	case 4:
		return readl(addr);
	case 8:
		return readq(addr);
	default:
		imh_printk(KERN_ERR, "Invalid reg 0x%p width %d\n", addr, width);
		return 0;
	}
}

static void __read_local_reg(void *reg)
{
	struct local_reg *r = (struct local_reg *)reg;

	r->val = readx(r->vbase + r->offset, r->width);
}

/* Read a local-view register. */
static bool read_local_reg(struct local_reg *reg)
{
	int cpu;

	/* Get the target CPU in the package @reg->pkg. */
	for_each_online_cpu(cpu) {
		if (reg->pkg == topology_physical_package_id(cpu))
			break;
	}

	if (cpu >= nr_cpu_ids)
		return false;

	reg->vbase = ioremap(reg->pbase, reg->size);
	if (!reg->vbase) {
		imh_printk(KERN_ERR, "Failed to ioremap 0x%llx\n", reg->pbase);
		return false;
	}

	/* Get the target CPU to read the register. */
	smp_call_function_single(cpu, __read_local_reg, reg, 1);
	iounmap(reg->vbase);

	return true;
}

/* Get the bitmap of memory controller instances in package @pkg. */
static u32 get_imc_bitmap(struct res_config *cfg, int pkg, bool north)
{
	DEFINE_LOCAL_REG(reg, cfg, pkg, north, pcu, 0, capid3);

	if (!read_local_reg(&reg))
		return 0;

	edac_dbg(2, "Pkg%d %s mc instances bitmap 0x%llx (reg 0x%llx)\n",
		 pkg, north ? "north" : "south",
		 DDR_IMC_BITMAP(reg.val), reg.val);

	return DDR_IMC_BITMAP(reg.val);
}

static void imc_release(struct device *dev)
{
	edac_dbg(2, "imc device %s released\n", dev_name(dev));
	kfree(dev);
}

static int __get_ddr_munits(struct res_config *cfg, struct skx_dev *d,
			    bool north, int lmc)
{
	unsigned long size = cfg->ddr_chan_mmio_sz * cfg->ddr_chan_num;
	unsigned long bitmap = get_imc_bitmap(cfg, d->pkg, north);
	void __iomem *mbase;
	struct device *dev;
	int i, rc, pmc;
	u64 base;

	for_each_set_bit(i, &bitmap, sizeof(bitmap) * 8) {
		base  = north ? d->mmio_base_h_north : d->mmio_base_h_south;
		base += cfg->ddr_imc_base + size * i;

		edac_dbg(2, "Pkg%d mc%d mmio base 0x%llx size 0x%lx\n",
			 d->pkg, lmc, base, size);

		/* Set up the imc MMIO. */
		mbase = ioremap(base, size);
		if (!mbase) {
			imh_printk(KERN_ERR, "Failed to ioremap 0x%llx\n", base);
			return -ENOMEM;
		}

		d->imc[lmc].mbase = mbase;
		d->imc[lmc].lmc = lmc;

		/* Create the imc device instance. */
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;

		dev->release = imc_release;
		device_initialize(dev);
		rc = dev_set_name(dev, "0x%llx", base);
		if (rc) {
			imh_printk(KERN_ERR, "Failed to set dev name\n");
			put_device(dev);
			return rc;
		}

		d->imc[lmc].dev = dev;

		/* Set up the imc index mapping. */
		pmc = north ? i : 8 + i;
		skx_set_mc_mapping(d, pmc, lmc);

		lmc++;
	}

	return lmc;
}

static bool get_ddr_munits(struct res_config *cfg, struct skx_dev *d)
{
	int lmc = __get_ddr_munits(cfg, d, true, 0);

	if (lmc < 0)
		return false;

	lmc = __get_ddr_munits(cfg, d, false, lmc);
	if (lmc <= 0)
		return false;

	return true;
}

static bool get_socket_id(struct res_config *cfg, struct skx_dev *d)
{
	DEFINE_LOCAL_REG(reg, cfg, d->pkg, true, ubox, 0, socket_id);
	u8 src_id;
	int i;

	if (!read_local_reg(&reg))
		return false;

	src_id = SOCKET_ID(reg.val);
	edac_dbg(2, "socket id 0x%x (reg 0x%llx)\n", src_id, reg.val);

	for (i = 0; i < cfg->ddr_imc_num; i++)
		d->imc[i].src_id   = src_id;

	return true;
}

/* Get TOLM (Top Of Low Memory) and TOHM (Top Of High Memory) parameters. */
static bool imh_get_tolm_tohm(struct res_config *cfg, u64 *tolm, u64 *tohm)
{
	DEFINE_LOCAL_REG(reg, cfg, 0, true, sca, 0, tolm);

	if (!read_local_reg(&reg))
		return false;

	*tolm = TOLM(reg.val);
	edac_dbg(2, "tolm 0x%llx (reg 0x%llx)\n", *tolm, reg.val);

	DEFINE_LOCAL_REG(reg2, cfg, 0, true, sca, 0, tohm);

	if (!read_local_reg(&reg2))
		return false;

	*tohm = TOHM(reg2.val);
	edac_dbg(2, "tohm 0x%llx (reg 0x%llx)\n", *tohm, reg2.val);

	return true;
}

/* Get the system-view MMIO_BASE_H for {north,south}-IMH. */
static int imh_get_all_mmio_base_h(struct res_config *cfg, struct list_head *edac_list)
{
	int i, n = topology_max_packages(), imc_num = cfg->ddr_imc_num + cfg->hbm_imc_num;
	struct skx_dev *d;

	for (i = 0; i < n; i++) {
		d = kzalloc(struct_size(d, imc, imc_num), GFP_KERNEL);
		if (!d)
			return -ENOMEM;

		DEFINE_LOCAL_REG(reg, cfg, i, true, ubox, 0, mmio_base);

		/* Get MMIO_BASE_H for the north-IMH. */
		if (!read_local_reg(&reg) || !reg.val) {
			kfree(d);
			imh_printk(KERN_ERR, "Pkg%d has no north mmio_base_h\n", i);
			return -ENODEV;
		}

		d->mmio_base_h_north = MMIO_BASE_H(reg.val);
		edac_dbg(2, "Pkg%d north mmio_base_h 0x%llx (reg 0x%llx)\n",
			 i, d->mmio_base_h_north, reg.val);

		/* Get MMIO_BASE_H for the south-IMH (optional). */
		DEFINE_LOCAL_REG(reg2, cfg, i, false, ubox, 0, mmio_base);

		if (read_local_reg(&reg2)) {
			d->mmio_base_h_south = MMIO_BASE_H(reg2.val);
			edac_dbg(2, "Pkg%d south mmio_base_h 0x%llx (reg 0x%llx)\n",
				 i, d->mmio_base_h_south, reg2.val);
		}

		d->pkg = i;
		d->num_imc = imc_num;
		skx_init_mc_mapping(d);
		list_add_tail(&d->list, edac_list);
	}

	return 0;
}

/* Get the number of per-package memory controllers. */
static int imh_get_imc_num(struct res_config *cfg)
{
	int imc_num = hweight32(get_imc_bitmap(cfg, 0, true)) +
		      hweight32(get_imc_bitmap(cfg, 0, false));

	if (!imc_num) {
		imh_printk(KERN_ERR, "Invalid mc number\n");
		return -ENODEV;
	}

	if (cfg->ddr_imc_num != imc_num) {
		/*
		 * Update the configuration data to reflect the number of
		 * present DDR memory controllers.
		 */
		cfg->ddr_imc_num = imc_num;
		edac_dbg(2, "Set ddr mc number %d\n", imc_num);
	}

	return 0;
}

/* Get all memory controllers' parameters. */
static int imh_get_munits(struct res_config *cfg, struct list_head *edac_list)
{
	struct skx_imc *imc;
	struct skx_dev *d;
	u8 mc = 0;
	int i;

	list_for_each_entry(d, edac_list, list) {
		if (!get_ddr_munits(cfg, d)) {
			imh_printk(KERN_ERR, "No mc found\n");
			return -ENODEV;
		}

		if (!get_socket_id(cfg, d)) {
			imh_printk(KERN_ERR, "Failed to get socket id\n");
			return -ENODEV;
		}

		for (i = 0; i < cfg->ddr_imc_num; i++) {
			imc = &d->imc[i];
			if (!imc->mbase)
				continue;

			imc->chan_mmio_sz = cfg->ddr_chan_mmio_sz;
			imc->num_channels = cfg->ddr_chan_num;
			imc->num_dimms    = cfg->ddr_dimm_num;
			imc->mc		  = mc++;
		}
	}

	return 0;
}

static bool check_2lm_enabled(struct res_config *cfg, struct skx_dev *d, int ha_idx)
{
	DEFINE_LOCAL_REG(reg, cfg, d->pkg, true, ha, ha_idx, mode);

	if (!read_local_reg(&reg))
		return false;

	if (!NMCACHING(reg.val))
		return false;

	edac_dbg(2, "2-level memory configuration (reg 0x%llx, ha idx %d)\n", reg.val, ha_idx);
	return true;
}

/* Check whether the system has a 2-level memory configuration. */
static bool imh_2lm_enabled(struct res_config *cfg, struct list_head *head)
{
	struct skx_dev *d;
	int i;

	list_for_each_entry(d, head, list) {
		for (i = 0; i < cfg->ddr_imc_num; i++)
			if (check_2lm_enabled(cfg, d, i))
				return true;
	}

	return false;
}

/* Helpers to read memory controller registers */
static u64 read_imc_reg(struct skx_imc *imc, int chan, u32 offset, u8 width)
{
	return readx(imc->mbase + imc->chan_mmio_sz * chan + offset, width);
}

static u32 read_imc_mcmtr(struct res_config *cfg, struct skx_imc *imc, int chan)
{
	return (u32)read_imc_reg(imc, chan, cfg->ddr_reg_mcmtr_offset, cfg->ddr_reg_mcmtr_width);
}

static u32 read_imc_dimmmtr(struct res_config *cfg, struct skx_imc *imc, int chan, int dimm)
{
	return (u32)read_imc_reg(imc, chan, cfg->ddr_reg_dimmmtr_offset +
				 cfg->ddr_reg_dimmmtr_width * dimm,
				 cfg->ddr_reg_dimmmtr_width);
}

static bool ecc_enabled(u32 mcmtr)
{
	return (bool)ECC_ENABLED(mcmtr);
}

static bool dimm_populated(u32 dimmmtr)
{
	return (bool)DIMM_POPULATED(dimmmtr);
}

/* Get each DIMM's configurations of the memory controller @mci. */
static int imh_get_dimm_config(struct mem_ctl_info *mci, struct res_config *cfg)
{
	struct skx_pvt *pvt = mci->pvt_info;
	struct skx_imc *imc = pvt->imc;
	struct dimm_info *dimm;
	u32 mcmtr, dimmmtr;
	int i, j, ndimms;

	for (i = 0; i < imc->num_channels; i++) {
		if (!imc->mbase)
			continue;

		mcmtr = read_imc_mcmtr(cfg, imc, i);

		for (ndimms = 0, j = 0; j < imc->num_dimms; j++) {
			dimmmtr = read_imc_dimmmtr(cfg, imc, i, j);
			edac_dbg(1, "mcmtr 0x%x dimmmtr 0x%x (mc%d ch%d dimm%d)\n",
				 mcmtr, dimmmtr, imc->mc, i, j);

			if (!dimm_populated(dimmmtr))
				continue;

			dimm = edac_get_dimm(mci, i, j, 0);
			ndimms += skx_get_dimm_info(dimmmtr, 0, 0, dimm,
						    imc, i, j, cfg);
		}

		if (ndimms && !ecc_enabled(mcmtr)) {
			imh_printk(KERN_ERR, "ECC is disabled on mc%d ch%d\n",
				   imc->mc, i);
			return -ENODEV;
		}
	}

	return 0;
}

/* Register all memory controllers to the EDAC core. */
static int imh_register_mci(struct res_config *cfg, struct list_head *edac_list)
{
	struct skx_imc *imc;
	struct skx_dev *d;
	int i, rc;

	list_for_each_entry(d, edac_list, list) {
		for (i = 0; i < cfg->ddr_imc_num; i++) {
			imc = &d->imc[i];
			if (!imc->mbase)
				continue;

			rc = skx_register_mci(imc, imc->dev,
					      dev_name(imc->dev),
					      "Intel IMH-based Socket",
					      EDAC_MOD_STR,
					      imh_get_dimm_config, cfg);
			if (rc)
				return rc;
		}
	}

	return 0;
}

static struct res_config dmr_cfg = {
	.type				= DMR,
	.support_ddr5			= true,
	.mmio_base_l_north		= 0xf6800000,
	.mmio_base_l_south		= 0xf6000000,
	.ddr_chan_num			= 1,
	.ddr_dimm_num			= 2,
	.ddr_imc_base			= 0x39b000,
	.ddr_chan_mmio_sz		= 0x8000,
	.ddr_reg_mcmtr_offset		= 0x360,
	.ddr_reg_mcmtr_width		= 4,
	.ddr_reg_dimmmtr_offset		= 0x370,
	.ddr_reg_dimmmtr_width		= 4,
	.ubox_base			= 0x0,
	.ubox_size			= 0x2000,
	.ubox_reg_mmio_base_offset	= 0x580,
	.ubox_reg_mmio_base_width	= 4,
	.ubox_reg_socket_id_offset	= 0x1080,
	.ubox_reg_socket_id_width	= 4,
	.pcu_base			= 0x3000,
	.pcu_size			= 0x10000,
	.pcu_reg_capid3_offset		= 0x290,
	.pcu_reg_capid3_width		= 4,
	.sca_base			= 0x24c000,
	.sca_size			= 0x2500,
	.sca_reg_tolm_offset		= 0x2100,
	.sca_reg_tolm_width		= 8,
	.sca_reg_tohm_offset		= 0x2108,
	.sca_reg_tohm_width		= 8,
	.ha_base			= 0x3eb000,
	.ha_size			= 0x1000,
	.ha_reg_mode_offset		= 0x4a0,
	.ha_reg_mode_width		= 4,
};

static const struct x86_cpu_id imh_cpuids[] = {
	X86_MATCH_VFM(INTEL_DIAMONDRAPIDS_X, &dmr_cfg),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, imh_cpuids);

static struct notifier_block imh_mce_dec = {
	.notifier_call	= skx_mce_check_error,
	.priority	= MCE_PRIO_EDAC,
};

static int __init imh_init(void)
{
	const struct x86_cpu_id *id;
	struct list_head *edac_list;
	struct res_config *cfg;
	const char *owner;
	u64 tolm, tohm;
	int rc;

	edac_dbg(2, "\n");

	if (ghes_get_devices())
		return -EBUSY;

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	if (cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	id = x86_match_cpu(imh_cpuids);
	if (!id)
		return -ENODEV;
	cfg = (struct res_config *)id->driver_data;
	skx_set_res_cfg(cfg);

	if (!imh_get_tolm_tohm(cfg, &tolm, &tohm))
		return -ENODEV;

	skx_set_hi_lo(tolm, tohm);

	rc = imh_get_imc_num(cfg);
	if (rc < 0)
		goto fail;

	edac_list = skx_get_edac_list();

	rc = imh_get_all_mmio_base_h(cfg, edac_list);
	if (rc)
		goto fail;

	rc = imh_get_munits(cfg, edac_list);
	if (rc)
		goto fail;

	skx_set_mem_cfg(imh_2lm_enabled(cfg, edac_list));

	rc = imh_register_mci(cfg, edac_list);
	if (rc)
		goto fail;

	rc = skx_adxl_get();
	if (rc)
		goto fail;

	opstate_init();
	mce_register_decode_chain(&imh_mce_dec);
	skx_setup_debug("imh_test");

	imh_printk(KERN_INFO, "%s\n", IMH_REVISION);

	return 0;
fail:
	skx_remove();
	return rc;
}

static void __exit imh_exit(void)
{
	edac_dbg(2, "\n");

	skx_teardown_debug();
	mce_unregister_decode_chain(&imh_mce_dec);
	skx_adxl_put();
	skx_remove();
}

module_init(imh_init);
module_exit(imh_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Qiuxu Zhuo");
MODULE_DESCRIPTION("MC Driver for Intel servers using IMH-based memory controller");
