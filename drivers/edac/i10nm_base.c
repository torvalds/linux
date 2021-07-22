// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Intel(R) 10nm server memory controller.
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/mce.h>
#include "edac_module.h"
#include "skx_common.h"

#define I10NM_REVISION	"v0.0.5"
#define EDAC_MOD_STR	"i10nm_edac"

/* Debug macros */
#define i10nm_printk(level, fmt, arg...)	\
	edac_printk(level, "i10nm", fmt, ##arg)

#define I10NM_GET_SCK_BAR(d, reg)	\
	pci_read_config_dword((d)->uracu, 0xd0, &(reg))
#define I10NM_GET_IMC_BAR(d, i, reg)	\
	pci_read_config_dword((d)->uracu, 0xd8 + (i) * 4, &(reg))
#define I10NM_GET_SAD(d, offset, i, reg)\
	pci_read_config_dword((d)->sad_all, (offset) + (i) * 8, &(reg))
#define I10NM_GET_HBM_IMC_BAR(d, reg)	\
	pci_read_config_dword((d)->uracu, 0xd4, &(reg))
#define I10NM_GET_CAPID3_CFG(d, reg)	\
	pci_read_config_dword((d)->pcu_cr3, 0x90, &(reg))
#define I10NM_GET_DIMMMTR(m, i, j)	\
	readl((m)->mbase + ((m)->hbm_mc ? 0x80c : 0x2080c) + \
	(i) * (m)->chan_mmio_sz + (j) * 4)
#define I10NM_GET_MCDDRTCFG(m, i, j)	\
	readl((m)->mbase + ((m)->hbm_mc ? 0x970 : 0x20970) + \
	(i) * (m)->chan_mmio_sz + (j) * 4)
#define I10NM_GET_MCMTR(m, i)		\
	readl((m)->mbase + ((m)->hbm_mc ? 0xef8 : 0x20ef8) + \
	(i) * (m)->chan_mmio_sz)
#define I10NM_GET_AMAP(m, i)		\
	readl((m)->mbase + ((m)->hbm_mc ? 0x814 : 0x20814) + \
	(i) * (m)->chan_mmio_sz)

#define I10NM_GET_SCK_MMIO_BASE(reg)	(GET_BITFIELD(reg, 0, 28) << 23)
#define I10NM_GET_IMC_MMIO_OFFSET(reg)	(GET_BITFIELD(reg, 0, 10) << 12)
#define I10NM_GET_IMC_MMIO_SIZE(reg)	((GET_BITFIELD(reg, 13, 23) - \
					 GET_BITFIELD(reg, 0, 10) + 1) << 12)
#define I10NM_GET_HBM_IMC_MMIO_OFFSET(reg)	\
	((GET_BITFIELD(reg, 0, 10) << 12) + 0x140000)

#define I10NM_HBM_IMC_MMIO_SIZE		0x9000
#define I10NM_IS_HBM_PRESENT(reg)	GET_BITFIELD(reg, 27, 30)
#define I10NM_IS_HBM_IMC(reg)		GET_BITFIELD(reg, 29, 29)

#define I10NM_MAX_SAD			16
#define I10NM_SAD_ENABLE(reg)		GET_BITFIELD(reg, 0, 0)
#define I10NM_SAD_NM_CACHEABLE(reg)	GET_BITFIELD(reg, 5, 5)

static struct list_head *i10nm_edac_list;

static struct pci_dev *pci_get_dev_wrapper(int dom, unsigned int bus,
					   unsigned int dev, unsigned int fun)
{
	struct pci_dev *pdev;

	pdev = pci_get_domain_bus_and_slot(dom, bus, PCI_DEVFN(dev, fun));
	if (!pdev) {
		edac_dbg(2, "No device %02x:%02x.%x\n",
			 bus, dev, fun);
		return NULL;
	}

	if (unlikely(pci_enable_device(pdev) < 0)) {
		edac_dbg(2, "Failed to enable device %02x:%02x.%x\n",
			 bus, dev, fun);
		return NULL;
	}

	pci_dev_get(pdev);

	return pdev;
}

static bool i10nm_check_2lm(struct res_config *cfg)
{
	struct skx_dev *d;
	u32 reg;
	int i;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->sad_all = pci_get_dev_wrapper(d->seg, d->bus[1],
						 PCI_SLOT(cfg->sad_all_devfn),
						 PCI_FUNC(cfg->sad_all_devfn));
		if (!d->sad_all)
			continue;

		for (i = 0; i < I10NM_MAX_SAD; i++) {
			I10NM_GET_SAD(d, cfg->sad_all_offset, i, reg);
			if (I10NM_SAD_ENABLE(reg) && I10NM_SAD_NM_CACHEABLE(reg)) {
				edac_dbg(2, "2-level memory configuration.\n");
				return true;
			}
		}
	}

	return false;
}

static int i10nm_get_ddr_munits(void)
{
	struct pci_dev *mdev;
	void __iomem *mbase;
	unsigned long size;
	struct skx_dev *d;
	int i, j = 0;
	u32 reg, off;
	u64 base;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->util_all = pci_get_dev_wrapper(d->seg, d->bus[1], 29, 1);
		if (!d->util_all)
			return -ENODEV;

		d->uracu = pci_get_dev_wrapper(d->seg, d->bus[0], 0, 1);
		if (!d->uracu)
			return -ENODEV;

		if (I10NM_GET_SCK_BAR(d, reg)) {
			i10nm_printk(KERN_ERR, "Failed to socket bar\n");
			return -ENODEV;
		}

		base = I10NM_GET_SCK_MMIO_BASE(reg);
		edac_dbg(2, "socket%d mmio base 0x%llx (reg 0x%x)\n",
			 j++, base, reg);

		for (i = 0; i < I10NM_NUM_DDR_IMC; i++) {
			mdev = pci_get_dev_wrapper(d->seg, d->bus[0],
						   12 + i, 0);
			if (i == 0 && !mdev) {
				i10nm_printk(KERN_ERR, "No IMC found\n");
				return -ENODEV;
			}
			if (!mdev)
				continue;

			d->imc[i].mdev = mdev;

			if (I10NM_GET_IMC_BAR(d, i, reg)) {
				i10nm_printk(KERN_ERR, "Failed to get mc bar\n");
				return -ENODEV;
			}

			off  = I10NM_GET_IMC_MMIO_OFFSET(reg);
			size = I10NM_GET_IMC_MMIO_SIZE(reg);
			edac_dbg(2, "mc%d mmio base 0x%llx size 0x%lx (reg 0x%x)\n",
				 i, base + off, size, reg);

			mbase = ioremap(base + off, size);
			if (!mbase) {
				i10nm_printk(KERN_ERR, "Failed to ioremap 0x%llx\n",
					     base + off);
				return -ENODEV;
			}

			d->imc[i].mbase = mbase;
		}
	}

	return 0;
}

static bool i10nm_check_hbm_imc(struct skx_dev *d)
{
	u32 reg;

	if (I10NM_GET_CAPID3_CFG(d, reg)) {
		i10nm_printk(KERN_ERR, "Failed to get capid3_cfg\n");
		return false;
	}

	return I10NM_IS_HBM_PRESENT(reg) != 0;
}

static int i10nm_get_hbm_munits(void)
{
	struct pci_dev *mdev;
	void __iomem *mbase;
	u32 reg, off, mcmtr;
	struct skx_dev *d;
	int i, lmc;
	u64 base;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->pcu_cr3 = pci_get_dev_wrapper(d->seg, d->bus[1], 30, 3);
		if (!d->pcu_cr3)
			return -ENODEV;

		if (!i10nm_check_hbm_imc(d)) {
			i10nm_printk(KERN_DEBUG, "No hbm memory\n");
			return -ENODEV;
		}

		if (I10NM_GET_SCK_BAR(d, reg)) {
			i10nm_printk(KERN_ERR, "Failed to get socket bar\n");
			return -ENODEV;
		}
		base = I10NM_GET_SCK_MMIO_BASE(reg);

		if (I10NM_GET_HBM_IMC_BAR(d, reg)) {
			i10nm_printk(KERN_ERR, "Failed to get hbm mc bar\n");
			return -ENODEV;
		}
		base += I10NM_GET_HBM_IMC_MMIO_OFFSET(reg);

		lmc = I10NM_NUM_DDR_IMC;

		for (i = 0; i < I10NM_NUM_HBM_IMC; i++) {
			mdev = pci_get_dev_wrapper(d->seg, d->bus[0],
						   12 + i / 4, 1 + i % 4);
			if (i == 0 && !mdev) {
				i10nm_printk(KERN_ERR, "No hbm mc found\n");
				return -ENODEV;
			}
			if (!mdev)
				continue;

			d->imc[lmc].mdev = mdev;
			off = i * I10NM_HBM_IMC_MMIO_SIZE;

			edac_dbg(2, "hbm mc%d mmio base 0x%llx size 0x%x\n",
				 lmc, base + off, I10NM_HBM_IMC_MMIO_SIZE);

			mbase = ioremap(base + off, I10NM_HBM_IMC_MMIO_SIZE);
			if (!mbase) {
				i10nm_printk(KERN_ERR, "Failed to ioremap for hbm mc 0x%llx\n",
					     base + off);
				return -ENOMEM;
			}

			d->imc[lmc].mbase = mbase;
			d->imc[lmc].hbm_mc = true;

			mcmtr = I10NM_GET_MCMTR(&d->imc[lmc], 0);
			if (!I10NM_IS_HBM_IMC(mcmtr)) {
				i10nm_printk(KERN_ERR, "This isn't an hbm mc!\n");
				return -ENODEV;
			}

			lmc++;
		}
	}

	return 0;
}

static struct res_config i10nm_cfg0 = {
	.type			= I10NM,
	.decs_did		= 0x3452,
	.busno_cfg_offset	= 0xcc,
	.ddr_chan_mmio_sz	= 0x4000,
	.sad_all_devfn		= PCI_DEVFN(29, 0),
	.sad_all_offset		= 0x108,
};

static struct res_config i10nm_cfg1 = {
	.type			= I10NM,
	.decs_did		= 0x3452,
	.busno_cfg_offset	= 0xd0,
	.ddr_chan_mmio_sz	= 0x4000,
	.sad_all_devfn		= PCI_DEVFN(29, 0),
	.sad_all_offset		= 0x108,
};

static struct res_config spr_cfg = {
	.type			= SPR,
	.decs_did		= 0x3252,
	.busno_cfg_offset	= 0xd0,
	.ddr_chan_mmio_sz	= 0x8000,
	.hbm_chan_mmio_sz	= 0x4000,
	.support_ddr5		= true,
	.sad_all_devfn		= PCI_DEVFN(10, 0),
	.sad_all_offset		= 0x300,
};

static const struct x86_cpu_id i10nm_cpuids[] = {
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(ATOM_TREMONT_D,	X86_STEPPINGS(0x0, 0x3), &i10nm_cfg0),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(ATOM_TREMONT_D,	X86_STEPPINGS(0x4, 0xf), &i10nm_cfg1),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(ICELAKE_X,		X86_STEPPINGS(0x0, 0x3), &i10nm_cfg0),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(ICELAKE_X,		X86_STEPPINGS(0x4, 0xf), &i10nm_cfg1),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(ICELAKE_D,		X86_STEPPINGS(0x0, 0xf), &i10nm_cfg1),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(SAPPHIRERAPIDS_X,	X86_STEPPINGS(0x0, 0xf), &spr_cfg),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, i10nm_cpuids);

static bool i10nm_check_ecc(struct skx_imc *imc, int chan)
{
	u32 mcmtr;

	mcmtr = I10NM_GET_MCMTR(imc, chan);
	edac_dbg(1, "ch%d mcmtr reg %x\n", chan, mcmtr);

	return !!GET_BITFIELD(mcmtr, 2, 2);
}

static int i10nm_get_dimm_config(struct mem_ctl_info *mci,
				 struct res_config *cfg)
{
	struct skx_pvt *pvt = mci->pvt_info;
	struct skx_imc *imc = pvt->imc;
	u32 mtr, amap, mcddrtcfg;
	struct dimm_info *dimm;
	int i, j, ndimms;

	for (i = 0; i < imc->num_channels; i++) {
		if (!imc->mbase)
			continue;

		ndimms = 0;
		amap = I10NM_GET_AMAP(imc, i);
		for (j = 0; j < imc->num_dimms; j++) {
			dimm = edac_get_dimm(mci, i, j, 0);
			mtr = I10NM_GET_DIMMMTR(imc, i, j);
			mcddrtcfg = I10NM_GET_MCDDRTCFG(imc, i, j);
			edac_dbg(1, "dimmmtr 0x%x mcddrtcfg 0x%x (mc%d ch%d dimm%d)\n",
				 mtr, mcddrtcfg, imc->mc, i, j);

			if (IS_DIMM_PRESENT(mtr))
				ndimms += skx_get_dimm_info(mtr, 0, amap, dimm,
							    imc, i, j, cfg);
			else if (IS_NVDIMM_PRESENT(mcddrtcfg, j))
				ndimms += skx_get_nvdimm_info(dimm, imc, i, j,
							      EDAC_MOD_STR);
		}
		if (ndimms && !i10nm_check_ecc(imc, i)) {
			i10nm_printk(KERN_ERR, "ECC is disabled on imc %d channel %d\n",
				     imc->mc, i);
			return -ENODEV;
		}
	}

	return 0;
}

static struct notifier_block i10nm_mce_dec = {
	.notifier_call	= skx_mce_check_error,
	.priority	= MCE_PRIO_EDAC,
};

#ifdef CONFIG_EDAC_DEBUG
/*
 * Debug feature.
 * Exercise the address decode logic by writing an address to
 * /sys/kernel/debug/edac/i10nm_test/addr.
 */
static struct dentry *i10nm_test;

static int debugfs_u64_set(void *data, u64 val)
{
	struct mce m;

	pr_warn_once("Fake error to 0x%llx injected via debugfs\n", val);

	memset(&m, 0, sizeof(m));
	/* ADDRV + MemRd + Unknown channel */
	m.status = MCI_STATUS_ADDRV + 0x90;
	/* One corrected error */
	m.status |= BIT_ULL(MCI_STATUS_CEC_SHIFT);
	m.addr = val;
	skx_mce_check_error(NULL, 0, &m);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_wo, NULL, debugfs_u64_set, "%llu\n");

static void setup_i10nm_debug(void)
{
	i10nm_test = edac_debugfs_create_dir("i10nm_test");
	if (!i10nm_test)
		return;

	if (!edac_debugfs_create_file("addr", 0200, i10nm_test,
				      NULL, &fops_u64_wo)) {
		debugfs_remove(i10nm_test);
		i10nm_test = NULL;
	}
}

static void teardown_i10nm_debug(void)
{
	debugfs_remove_recursive(i10nm_test);
}
#else
static inline void setup_i10nm_debug(void) {}
static inline void teardown_i10nm_debug(void) {}
#endif /*CONFIG_EDAC_DEBUG*/

static int __init i10nm_init(void)
{
	u8 mc = 0, src_id = 0, node_id = 0;
	const struct x86_cpu_id *id;
	struct res_config *cfg;
	const char *owner;
	struct skx_dev *d;
	int rc, i, off[3] = {0xd0, 0xc8, 0xcc};
	u64 tolm, tohm;

	edac_dbg(2, "\n");

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	if (cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	id = x86_match_cpu(i10nm_cpuids);
	if (!id)
		return -ENODEV;

	cfg = (struct res_config *)id->driver_data;

	rc = skx_get_hi_lo(0x09a2, off, &tolm, &tohm);
	if (rc)
		return rc;

	rc = skx_get_all_bus_mappings(cfg, &i10nm_edac_list);
	if (rc < 0)
		goto fail;
	if (rc == 0) {
		i10nm_printk(KERN_ERR, "No memory controllers found\n");
		return -ENODEV;
	}

	skx_set_mem_cfg(i10nm_check_2lm(cfg));

	rc = i10nm_get_ddr_munits();

	if (i10nm_get_hbm_munits() && rc)
		goto fail;

	list_for_each_entry(d, i10nm_edac_list, list) {
		rc = skx_get_src_id(d, 0xf8, &src_id);
		if (rc < 0)
			goto fail;

		rc = skx_get_node_id(d, &node_id);
		if (rc < 0)
			goto fail;

		edac_dbg(2, "src_id = %d node_id = %d\n", src_id, node_id);
		for (i = 0; i < I10NM_NUM_IMC; i++) {
			if (!d->imc[i].mdev)
				continue;

			d->imc[i].mc  = mc++;
			d->imc[i].lmc = i;
			d->imc[i].src_id  = src_id;
			d->imc[i].node_id = node_id;
			if (d->imc[i].hbm_mc) {
				d->imc[i].chan_mmio_sz = cfg->hbm_chan_mmio_sz;
				d->imc[i].num_channels = I10NM_NUM_HBM_CHANNELS;
				d->imc[i].num_dimms    = I10NM_NUM_HBM_DIMMS;
			} else {
				d->imc[i].chan_mmio_sz = cfg->ddr_chan_mmio_sz;
				d->imc[i].num_channels = I10NM_NUM_DDR_CHANNELS;
				d->imc[i].num_dimms    = I10NM_NUM_DDR_DIMMS;
			}

			rc = skx_register_mci(&d->imc[i], d->imc[i].mdev,
					      "Intel_10nm Socket", EDAC_MOD_STR,
					      i10nm_get_dimm_config, cfg);
			if (rc < 0)
				goto fail;
		}
	}

	rc = skx_adxl_get();
	if (rc)
		goto fail;

	opstate_init();
	mce_register_decode_chain(&i10nm_mce_dec);
	setup_i10nm_debug();

	i10nm_printk(KERN_INFO, "%s\n", I10NM_REVISION);

	return 0;
fail:
	skx_remove();
	return rc;
}

static void __exit i10nm_exit(void)
{
	edac_dbg(2, "\n");
	teardown_i10nm_debug();
	mce_unregister_decode_chain(&i10nm_mce_dec);
	skx_adxl_put();
	skx_remove();
}

module_init(i10nm_init);
module_exit(i10nm_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MC Driver for Intel 10nm server processors");
