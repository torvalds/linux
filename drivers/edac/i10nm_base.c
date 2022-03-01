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
#define I10NM_GET_MCDDRTCFG(m, i)	\
	readl((m)->mbase + ((m)->hbm_mc ? 0x970 : 0x20970) + \
	(i) * (m)->chan_mmio_sz)
#define I10NM_GET_MCMTR(m, i)		\
	readl((m)->mbase + ((m)->hbm_mc ? 0xef8 : 0x20ef8) + \
	(i) * (m)->chan_mmio_sz)
#define I10NM_GET_AMAP(m, i)		\
	readl((m)->mbase + ((m)->hbm_mc ? 0x814 : 0x20814) + \
	(i) * (m)->chan_mmio_sz)
#define I10NM_GET_REG32(m, i, offset)	\
	readl((m)->mbase + (i) * (m)->chan_mmio_sz + (offset))
#define I10NM_GET_REG64(m, i, offset)	\
	readq((m)->mbase + (i) * (m)->chan_mmio_sz + (offset))
#define I10NM_SET_REG32(m, i, offset, v)	\
	writel(v, (m)->mbase + (i) * (m)->chan_mmio_sz + (offset))

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

#define RETRY_RD_ERR_LOG_UC		BIT(1)
#define RETRY_RD_ERR_LOG_NOOVER		BIT(14)
#define RETRY_RD_ERR_LOG_EN		BIT(15)
#define RETRY_RD_ERR_LOG_NOOVER_UC	(BIT(14) | BIT(1))
#define RETRY_RD_ERR_LOG_OVER_UC_V	(BIT(2) | BIT(1) | BIT(0))

static struct list_head *i10nm_edac_list;

static struct res_config *res_cfg;
static int retry_rd_err_log;

static u32 offsets_scrub_icx[]  = {0x22c60, 0x22c54, 0x22c5c, 0x22c58, 0x22c28, 0x20ed8};
static u32 offsets_scrub_spr[]  = {0x22c60, 0x22c54, 0x22f08, 0x22c58, 0x22c28, 0x20ed8};
static u32 offsets_demand_icx[] = {0x22e54, 0x22e60, 0x22e64, 0x22e58, 0x22e5c, 0x20ee0};
static u32 offsets_demand_spr[] = {0x22e54, 0x22e60, 0x22f10, 0x22e58, 0x22e5c, 0x20ee0};

static void __enable_retry_rd_err_log(struct skx_imc *imc, int chan, bool enable)
{
	u32 s, d;

	if (!imc->mbase)
		return;

	s = I10NM_GET_REG32(imc, chan, res_cfg->offsets_scrub[0]);
	d = I10NM_GET_REG32(imc, chan, res_cfg->offsets_demand[0]);

	if (enable) {
		/* Save default configurations */
		imc->chan[chan].retry_rd_err_log_s = s;
		imc->chan[chan].retry_rd_err_log_d = d;

		s &= ~RETRY_RD_ERR_LOG_NOOVER_UC;
		s |=  RETRY_RD_ERR_LOG_EN;
		d &= ~RETRY_RD_ERR_LOG_NOOVER_UC;
		d |=  RETRY_RD_ERR_LOG_EN;
	} else {
		/* Restore default configurations */
		if (imc->chan[chan].retry_rd_err_log_s & RETRY_RD_ERR_LOG_UC)
			s |=  RETRY_RD_ERR_LOG_UC;
		if (imc->chan[chan].retry_rd_err_log_s & RETRY_RD_ERR_LOG_NOOVER)
			s |=  RETRY_RD_ERR_LOG_NOOVER;
		if (!(imc->chan[chan].retry_rd_err_log_s & RETRY_RD_ERR_LOG_EN))
			s &= ~RETRY_RD_ERR_LOG_EN;
		if (imc->chan[chan].retry_rd_err_log_d & RETRY_RD_ERR_LOG_UC)
			d |=  RETRY_RD_ERR_LOG_UC;
		if (imc->chan[chan].retry_rd_err_log_d & RETRY_RD_ERR_LOG_NOOVER)
			d |=  RETRY_RD_ERR_LOG_NOOVER;
		if (!(imc->chan[chan].retry_rd_err_log_d & RETRY_RD_ERR_LOG_EN))
			d &= ~RETRY_RD_ERR_LOG_EN;
	}

	I10NM_SET_REG32(imc, chan, res_cfg->offsets_scrub[0], s);
	I10NM_SET_REG32(imc, chan, res_cfg->offsets_demand[0], d);
}

static void enable_retry_rd_err_log(bool enable)
{
	struct skx_dev *d;
	int i, j;

	edac_dbg(2, "\n");

	list_for_each_entry(d, i10nm_edac_list, list)
		for (i = 0; i < I10NM_NUM_IMC; i++)
			for (j = 0; j < I10NM_NUM_CHANNELS; j++)
				__enable_retry_rd_err_log(&d->imc[i], j, enable);
}

static void show_retry_rd_err_log(struct decoded_addr *res, char *msg,
				  int len, bool scrub_err)
{
	struct skx_imc *imc = &res->dev->imc[res->imc];
	u32 log0, log1, log2, log3, log4;
	u32 corr0, corr1, corr2, corr3;
	u64 log2a, log5;
	u32 *offsets;
	int n;

	if (!imc->mbase)
		return;

	offsets = scrub_err ? res_cfg->offsets_scrub : res_cfg->offsets_demand;

	log0 = I10NM_GET_REG32(imc, res->channel, offsets[0]);
	log1 = I10NM_GET_REG32(imc, res->channel, offsets[1]);
	log3 = I10NM_GET_REG32(imc, res->channel, offsets[3]);
	log4 = I10NM_GET_REG32(imc, res->channel, offsets[4]);
	log5 = I10NM_GET_REG64(imc, res->channel, offsets[5]);

	if (res_cfg->type == SPR) {
		log2a = I10NM_GET_REG64(imc, res->channel, offsets[2]);
		n = snprintf(msg, len, " retry_rd_err_log[%.8x %.8x %.16llx %.8x %.8x %.16llx]",
			     log0, log1, log2a, log3, log4, log5);
	} else {
		log2 = I10NM_GET_REG32(imc, res->channel, offsets[2]);
		n = snprintf(msg, len, " retry_rd_err_log[%.8x %.8x %.8x %.8x %.8x %.16llx]",
			     log0, log1, log2, log3, log4, log5);
	}

	corr0 = I10NM_GET_REG32(imc, res->channel, 0x22c18);
	corr1 = I10NM_GET_REG32(imc, res->channel, 0x22c1c);
	corr2 = I10NM_GET_REG32(imc, res->channel, 0x22c20);
	corr3 = I10NM_GET_REG32(imc, res->channel, 0x22c24);

	if (len - n > 0)
		snprintf(msg + n, len - n,
			 " correrrcnt[%.4x %.4x %.4x %.4x %.4x %.4x %.4x %.4x]",
			 corr0 & 0xffff, corr0 >> 16,
			 corr1 & 0xffff, corr1 >> 16,
			 corr2 & 0xffff, corr2 >> 16,
			 corr3 & 0xffff, corr3 >> 16);

	/* Clear status bits */
	if (retry_rd_err_log == 2 && (log0 & RETRY_RD_ERR_LOG_OVER_UC_V)) {
		log0 &= ~RETRY_RD_ERR_LOG_OVER_UC_V;
		I10NM_SET_REG32(imc, res->channel, offsets[0], log0);
	}
}

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
				pci_dev_put(d->imc[lmc].mdev);
				d->imc[lmc].mdev = NULL;

				i10nm_printk(KERN_ERR, "Failed to ioremap for hbm mc 0x%llx\n",
					     base + off);
				return -ENOMEM;
			}

			d->imc[lmc].mbase = mbase;
			d->imc[lmc].hbm_mc = true;

			mcmtr = I10NM_GET_MCMTR(&d->imc[lmc], 0);
			if (!I10NM_IS_HBM_IMC(mcmtr)) {
				iounmap(d->imc[lmc].mbase);
				d->imc[lmc].mbase = NULL;
				d->imc[lmc].hbm_mc = false;
				pci_dev_put(d->imc[lmc].mdev);
				d->imc[lmc].mdev = NULL;

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
	.offsets_scrub		= offsets_scrub_icx,
	.offsets_demand		= offsets_demand_icx,
};

static struct res_config i10nm_cfg1 = {
	.type			= I10NM,
	.decs_did		= 0x3452,
	.busno_cfg_offset	= 0xd0,
	.ddr_chan_mmio_sz	= 0x4000,
	.sad_all_devfn		= PCI_DEVFN(29, 0),
	.sad_all_offset		= 0x108,
	.offsets_scrub		= offsets_scrub_icx,
	.offsets_demand		= offsets_demand_icx,
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
	.offsets_scrub		= offsets_scrub_spr,
	.offsets_demand		= offsets_demand_spr,
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
		mcddrtcfg = I10NM_GET_MCDDRTCFG(imc, i);
		for (j = 0; j < imc->num_dimms; j++) {
			dimm = edac_get_dimm(mci, i, j, 0);
			mtr = I10NM_GET_DIMMMTR(imc, i, j);
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
	res_cfg = cfg;

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

	if (retry_rd_err_log && res_cfg->offsets_scrub && res_cfg->offsets_demand) {
		skx_set_decode(NULL, show_retry_rd_err_log);
		if (retry_rd_err_log == 2)
			enable_retry_rd_err_log(true);
	}

	i10nm_printk(KERN_INFO, "%s\n", I10NM_REVISION);

	return 0;
fail:
	skx_remove();
	return rc;
}

static void __exit i10nm_exit(void)
{
	edac_dbg(2, "\n");

	if (retry_rd_err_log && res_cfg->offsets_scrub && res_cfg->offsets_demand) {
		skx_set_decode(NULL, NULL);
		if (retry_rd_err_log == 2)
			enable_retry_rd_err_log(false);
	}

	teardown_i10nm_debug();
	mce_unregister_decode_chain(&i10nm_mce_dec);
	skx_adxl_put();
	skx_remove();
}

module_init(i10nm_init);
module_exit(i10nm_exit);

module_param(retry_rd_err_log, int, 0444);
MODULE_PARM_DESC(retry_rd_err_log, "retry_rd_err_log: 0=off(default), 1=bios(Linux doesn't reset any control bits, but just reports values.), 2=linux(Linux tries to take control and resets mode bits, clear valid/UC bits after reading.)");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MC Driver for Intel 10nm server processors");
