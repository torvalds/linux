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

#define I10NM_REVISION	"v0.0.6"
#define EDAC_MOD_STR	"i10nm_edac"

/* Debug macros */
#define i10nm_printk(level, fmt, arg...)	\
	edac_printk(level, "i10nm", fmt, ##arg)

#define I10NM_GET_SCK_BAR(d, reg)	\
	pci_read_config_dword((d)->uracu, 0xd0, &(reg))
#define I10NM_GET_IMC_BAR(d, i, reg)		\
	pci_read_config_dword((d)->uracu,	\
	(res_cfg->type == GNR ? 0xd4 : 0xd8) + (i) * 4, &(reg))
#define I10NM_GET_SAD(d, offset, i, reg)\
	pci_read_config_dword((d)->sad_all, (offset) + (i) * \
	(res_cfg->type == GNR ? 12 : 8), &(reg))
#define I10NM_GET_HBM_IMC_BAR(d, reg)	\
	pci_read_config_dword((d)->uracu, 0xd4, &(reg))
#define I10NM_GET_CAPID3_CFG(d, reg)	\
	pci_read_config_dword((d)->pcu_cr3,	\
	res_cfg->type == GNR ? 0x290 : 0x90, &(reg))
#define I10NM_GET_CAPID5_CFG(d, reg)	\
	pci_read_config_dword((d)->pcu_cr3,	\
	res_cfg->type == GNR ? 0x298 : 0x98, &(reg))
#define I10NM_GET_DIMMMTR(m, i, j)	\
	readl((m)->mbase + ((m)->hbm_mc ? 0x80c :	\
	(res_cfg->type == GNR ? 0xc0c : 0x2080c)) +	\
	(i) * (m)->chan_mmio_sz + (j) * 4)
#define I10NM_GET_MCDDRTCFG(m, i)	\
	readl((m)->mbase + ((m)->hbm_mc ? 0x970 : 0x20970) + \
	(i) * (m)->chan_mmio_sz)
#define I10NM_GET_MCMTR(m, i)		\
	readl((m)->mbase + ((m)->hbm_mc ? 0xef8 :	\
	(res_cfg->type == GNR ? 0xaf8 : 0x20ef8)) +	\
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

#define I10NM_GNR_IMC_MMIO_OFFSET	0x24c000
#define I10NM_GNR_D_IMC_MMIO_OFFSET	0x206000
#define I10NM_GNR_IMC_MMIO_SIZE		0x4000
#define I10NM_HBM_IMC_MMIO_SIZE		0x9000
#define I10NM_DDR_IMC_CH_CNT(reg)	GET_BITFIELD(reg, 21, 24)
#define I10NM_IS_HBM_PRESENT(reg)	GET_BITFIELD(reg, 27, 30)
#define I10NM_IS_HBM_IMC(reg)		GET_BITFIELD(reg, 29, 29)

#define I10NM_MAX_SAD			16
#define I10NM_SAD_ENABLE(reg)		GET_BITFIELD(reg, 0, 0)
#define I10NM_SAD_NM_CACHEABLE(reg)	GET_BITFIELD(reg, 5, 5)

static struct list_head *i10nm_edac_list;

static struct res_config *res_cfg;
static int retry_rd_err_log;
static int decoding_via_mca;
static bool mem_cfg_2lm;

static struct reg_rrl icx_reg_rrl_ddr = {
	.set_num = 2,
	.reg_num = 6,
	.modes = {LRE_SCRUB, LRE_DEMAND},
	.offsets = {
		{0x22c60, 0x22c54, 0x22c5c, 0x22c58, 0x22c28, 0x20ed8},
		{0x22e54, 0x22e60, 0x22e64, 0x22e58, 0x22e5c, 0x20ee0},
	},
	.widths		= {4, 4, 4, 4, 4, 8},
	.v_mask		= BIT(0),
	.uc_mask	= BIT(1),
	.over_mask	= BIT(2),
	.en_patspr_mask	= BIT(13),
	.noover_mask	= BIT(14),
	.en_mask	= BIT(15),

	.cecnt_num	= 4,
	.cecnt_offsets	= {0x22c18, 0x22c1c, 0x22c20, 0x22c24},
	.cecnt_widths	= {4, 4, 4, 4},
};

static struct reg_rrl spr_reg_rrl_ddr = {
	.set_num = 3,
	.reg_num = 6,
	.modes = {LRE_SCRUB, LRE_DEMAND, FRE_DEMAND},
	.offsets = {
		{0x22c60, 0x22c54, 0x22f08, 0x22c58, 0x22c28, 0x20ed8},
		{0x22e54, 0x22e60, 0x22f10, 0x22e58, 0x22e5c, 0x20ee0},
		{0x22c70, 0x22d80, 0x22f18, 0x22d58, 0x22c64, 0x20f10},
	},
	.widths		= {4, 4, 8, 4, 4, 8},
	.v_mask		= BIT(0),
	.uc_mask	= BIT(1),
	.over_mask	= BIT(2),
	.en_patspr_mask	= BIT(13),
	.noover_mask	= BIT(14),
	.en_mask	= BIT(15),

	.cecnt_num	= 4,
	.cecnt_offsets	= {0x22c18, 0x22c1c, 0x22c20, 0x22c24},
	.cecnt_widths	= {4, 4, 4, 4},
};

static struct reg_rrl spr_reg_rrl_hbm_pch0 = {
	.set_num = 2,
	.reg_num = 6,
	.modes = {LRE_SCRUB, LRE_DEMAND},
	.offsets = {
		{0x2860, 0x2854, 0x2b08, 0x2858, 0x2828, 0x0ed8},
		{0x2a54, 0x2a60, 0x2b10, 0x2a58, 0x2a5c, 0x0ee0},
	},
	.widths		= {4, 4, 8, 4, 4, 8},
	.v_mask		= BIT(0),
	.uc_mask	= BIT(1),
	.over_mask	= BIT(2),
	.en_patspr_mask	= BIT(13),
	.noover_mask	= BIT(14),
	.en_mask	= BIT(15),

	.cecnt_num	= 4,
	.cecnt_offsets	= {0x2818, 0x281c, 0x2820, 0x2824},
	.cecnt_widths	= {4, 4, 4, 4},
};

static struct reg_rrl spr_reg_rrl_hbm_pch1 = {
	.set_num = 2,
	.reg_num = 6,
	.modes = {LRE_SCRUB, LRE_DEMAND},
	.offsets = {
		{0x2c60, 0x2c54, 0x2f08, 0x2c58, 0x2c28, 0x0fa8},
		{0x2e54, 0x2e60, 0x2f10, 0x2e58, 0x2e5c, 0x0fb0},
	},
	.widths		= {4, 4, 8, 4, 4, 8},
	.v_mask		= BIT(0),
	.uc_mask	= BIT(1),
	.over_mask	= BIT(2),
	.en_patspr_mask	= BIT(13),
	.noover_mask	= BIT(14),
	.en_mask	= BIT(15),

	.cecnt_num	= 4,
	.cecnt_offsets	= {0x2c18, 0x2c1c, 0x2c20, 0x2c24},
	.cecnt_widths	= {4, 4, 4, 4},
};

static struct reg_rrl gnr_reg_rrl_ddr = {
	.set_num = 4,
	.reg_num = 6,
	.modes = {FRE_SCRUB, FRE_DEMAND, LRE_SCRUB, LRE_DEMAND},
	.offsets = {
		{0x2f10, 0x2f20, 0x2f30, 0x2f50, 0x2f60, 0xba0},
		{0x2f14, 0x2f24, 0x2f38, 0x2f54, 0x2f64, 0xba8},
		{0x2f18, 0x2f28, 0x2f40, 0x2f58, 0x2f68, 0xbb0},
		{0x2f1c, 0x2f2c, 0x2f48, 0x2f5c, 0x2f6c, 0xbb8},
	},
	.widths		= {4, 4, 8, 4, 4, 8},
	.v_mask		= BIT(0),
	.uc_mask	= BIT(1),
	.over_mask	= BIT(2),
	.en_patspr_mask	= BIT(14),
	.noover_mask	= BIT(15),
	.en_mask	= BIT(12),

	.cecnt_num	= 8,
	.cecnt_offsets	= {0x2c10, 0x2c14, 0x2c18, 0x2c1c, 0x2c20, 0x2c24, 0x2c28, 0x2c2c},
	.cecnt_widths	= {4, 4, 4, 4, 4, 4, 4, 4},
};

static u64 read_imc_reg(struct skx_imc *imc, int chan, u32 offset, u8 width)
{
	switch (width) {
	case 4:
		return I10NM_GET_REG32(imc, chan, offset);
	case 8:
		return I10NM_GET_REG64(imc, chan, offset);
	default:
		i10nm_printk(KERN_ERR, "Invalid readd RRL 0x%x width %d\n", offset, width);
		return 0;
	}
}

static void write_imc_reg(struct skx_imc *imc, int chan, u32 offset, u8 width, u64 val)
{
	switch (width) {
	case 4:
		return I10NM_SET_REG32(imc, chan, offset, (u32)val);
	default:
		i10nm_printk(KERN_ERR, "Invalid write RRL 0x%x width %d\n", offset, width);
	}
}

static void enable_rrl(struct skx_imc *imc, int chan, struct reg_rrl *rrl,
		       int rrl_set, bool enable, u32 *rrl_ctl)
{
	enum rrl_mode mode = rrl->modes[rrl_set];
	u32 offset = rrl->offsets[rrl_set][0], v;
	u8 width = rrl->widths[0];
	bool first, scrub;

	/* First or last read error. */
	first = (mode == FRE_SCRUB || mode == FRE_DEMAND);
	/* Patrol scrub or on-demand read error. */
	scrub = (mode == FRE_SCRUB || mode == LRE_SCRUB);

	v = read_imc_reg(imc, chan, offset, width);

	if (enable) {
		/* Save default configurations. */
		*rrl_ctl = v;
		v &= ~rrl->uc_mask;

		if (first)
			v |= rrl->noover_mask;
		else
			v &= ~rrl->noover_mask;

		if (scrub)
			v |= rrl->en_patspr_mask;
		else
			v &= ~rrl->en_patspr_mask;

		v |= rrl->en_mask;
	} else {
		/* Restore default configurations. */
		if (*rrl_ctl & rrl->uc_mask)
			v |= rrl->uc_mask;

		if (first) {
			if (!(*rrl_ctl & rrl->noover_mask))
				v &= ~rrl->noover_mask;
		} else {
			if (*rrl_ctl & rrl->noover_mask)
				v |= rrl->noover_mask;
		}

		if (scrub) {
			if (!(*rrl_ctl & rrl->en_patspr_mask))
				v &= ~rrl->en_patspr_mask;
		} else {
			if (*rrl_ctl & rrl->en_patspr_mask)
				v |= rrl->en_patspr_mask;
		}

		if (!(*rrl_ctl & rrl->en_mask))
			v &= ~rrl->en_mask;
	}

	write_imc_reg(imc, chan, offset, width, v);
}

static void enable_rrls(struct skx_imc *imc, int chan, struct reg_rrl *rrl,
			bool enable, u32 *rrl_ctl)
{
	for (int i = 0; i < rrl->set_num; i++)
		enable_rrl(imc, chan, rrl, i, enable, rrl_ctl + i);
}

static void enable_rrls_ddr(struct skx_imc *imc, bool enable)
{
	struct reg_rrl *rrl_ddr = res_cfg->reg_rrl_ddr;
	int i, chan_num = res_cfg->ddr_chan_num;
	struct skx_channel *chan = imc->chan;

	if (!imc->mbase)
		return;

	for (i = 0; i < chan_num; i++)
		enable_rrls(imc, i, rrl_ddr, enable, chan[i].rrl_ctl[0]);
}

static void enable_rrls_hbm(struct skx_imc *imc, bool enable)
{
	struct reg_rrl **rrl_hbm = res_cfg->reg_rrl_hbm;
	int i, chan_num = res_cfg->hbm_chan_num;
	struct skx_channel *chan = imc->chan;

	if (!imc->mbase || !imc->hbm_mc || !rrl_hbm[0] || !rrl_hbm[1])
		return;

	for (i = 0; i < chan_num; i++) {
		enable_rrls(imc, i, rrl_hbm[0], enable, chan[i].rrl_ctl[0]);
		enable_rrls(imc, i, rrl_hbm[1], enable, chan[i].rrl_ctl[1]);
	}
}

static void enable_retry_rd_err_log(bool enable)
{
	struct skx_dev *d;
	int i, imc_num;

	edac_dbg(2, "\n");

	list_for_each_entry(d, i10nm_edac_list, list) {
		imc_num  = res_cfg->ddr_imc_num;
		for (i = 0; i < imc_num; i++)
			enable_rrls_ddr(&d->imc[i], enable);

		imc_num += res_cfg->hbm_imc_num;
		for (; i < imc_num; i++)
			enable_rrls_hbm(&d->imc[i], enable);
	}
}

static void show_retry_rd_err_log(struct decoded_addr *res, char *msg,
				  int len, bool scrub_err)
{
	int i, j, n, ch = res->channel, pch = res->cs & 1;
	struct skx_imc *imc = &res->dev->imc[res->imc];
	u64 log, corr, status_mask;
	struct reg_rrl *rrl;
	bool scrub;
	u32 offset;
	u8 width;

	if (!imc->mbase)
		return;

	rrl = imc->hbm_mc ? res_cfg->reg_rrl_hbm[pch] : res_cfg->reg_rrl_ddr;

	if (!rrl)
		return;

	status_mask = rrl->over_mask | rrl->uc_mask | rrl->v_mask;

	n = scnprintf(msg, len, " retry_rd_err_log[");
	for (i = 0; i < rrl->set_num; i++) {
		scrub = (rrl->modes[i] == FRE_SCRUB || rrl->modes[i] == LRE_SCRUB);
		if (scrub_err != scrub)
			continue;

		for (j = 0; j < rrl->reg_num && len - n > 0; j++) {
			offset = rrl->offsets[i][j];
			width = rrl->widths[j];
			log = read_imc_reg(imc, ch, offset, width);

			if (width == 4)
				n += scnprintf(msg + n, len - n, "%.8llx ", log);
			else
				n += scnprintf(msg + n, len - n, "%.16llx ", log);

			/* Clear RRL status if RRL in Linux control mode. */
			if (retry_rd_err_log == 2 && !j && (log & status_mask))
				write_imc_reg(imc, ch, offset, width, log & ~status_mask);
		}
	}

	/* Move back one space. */
	n--;
	n += scnprintf(msg + n, len - n, "]");

	if (len - n > 0) {
		n += scnprintf(msg + n, len - n, " correrrcnt[");
		for (i = 0; i < rrl->cecnt_num && len - n > 0; i++) {
			offset = rrl->cecnt_offsets[i];
			width = rrl->cecnt_widths[i];
			corr = read_imc_reg(imc, ch, offset, width);

			/* CPUs {ICX,SPR} encode two counters per 4-byte CORRERRCNT register. */
			if (res_cfg->type <= SPR) {
				n += scnprintf(msg + n, len - n, "%.4llx %.4llx ",
					      corr & 0xffff, corr >> 16);
			} else {
			/* CPUs {GNR} encode one counter per CORRERRCNT register. */
				if (width == 4)
					n += scnprintf(msg + n, len - n, "%.8llx ", corr);
				else
					n += scnprintf(msg + n, len - n, "%.16llx ", corr);
			}
		}

		/* Move back one space. */
		n--;
		n += scnprintf(msg + n, len - n, "]");
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
		pci_dev_put(pdev);
		return NULL;
	}

	return pdev;
}

/**
 * i10nm_get_imc_num() - Get the number of present DDR memory controllers.
 *
 * @cfg : The pointer to the structure of EDAC resource configurations.
 *
 * For Granite Rapids CPUs, the number of present DDR memory controllers read
 * at runtime overwrites the value statically configured in @cfg->ddr_imc_num.
 * For other CPUs, the number of present DDR memory controllers is statically
 * configured in @cfg->ddr_imc_num.
 *
 * RETURNS : 0 on success, < 0 on failure.
 */
static int i10nm_get_imc_num(struct res_config *cfg)
{
	int n, imc_num, chan_num = 0;
	struct skx_dev *d;
	u32 reg;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->pcu_cr3 = pci_get_dev_wrapper(d->seg, d->bus[res_cfg->pcu_cr3_bdf.bus],
						 res_cfg->pcu_cr3_bdf.dev,
						 res_cfg->pcu_cr3_bdf.fun);
		if (!d->pcu_cr3)
			continue;

		if (I10NM_GET_CAPID5_CFG(d, reg))
			continue;

		n = I10NM_DDR_IMC_CH_CNT(reg);

		if (!chan_num) {
			chan_num = n;
			edac_dbg(2, "Get DDR CH number: %d\n", chan_num);
		} else if (chan_num != n) {
			i10nm_printk(KERN_NOTICE, "Get DDR CH numbers: %d, %d\n", chan_num, n);
		}
	}

	switch (cfg->type) {
	case GNR:
		/*
		 * One channel per DDR memory controller for Granite Rapids CPUs.
		 */
		imc_num = chan_num;

		if (!imc_num) {
			i10nm_printk(KERN_ERR, "Invalid DDR MC number\n");
			return -ENODEV;
		}

		if (cfg->ddr_imc_num != imc_num) {
			/*
			 * Update the configuration data to reflect the number of
			 * present DDR memory controllers.
			 */
			cfg->ddr_imc_num = imc_num;
			edac_dbg(2, "Set DDR MC number: %d", imc_num);

			/* Release and reallocate skx_dev list with the updated number. */
			skx_remove();
			if (skx_get_all_bus_mappings(cfg, &i10nm_edac_list) <= 0)
				return -ENODEV;
		}

		return 0;
	default:
		/*
		 * For other CPUs, the number of present DDR memory controllers
		 * is statically pre-configured in cfg->ddr_imc_num.
		 */
		return 0;
	}
}

static bool i10nm_check_2lm(struct res_config *cfg)
{
	struct skx_dev *d;
	u32 reg;
	int i;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->sad_all = pci_get_dev_wrapper(d->seg, d->bus[res_cfg->sad_all_bdf.bus],
						 res_cfg->sad_all_bdf.dev,
						 res_cfg->sad_all_bdf.fun);
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

/*
 * Check whether the error comes from DDRT by ICX/Tremont/SPR model specific error code.
 * Refer to SDM vol3B 17.11.3/17.13.2 Intel IMC MC error codes for IA32_MCi_STATUS.
 */
static bool i10nm_mscod_is_ddrt(u32 mscod)
{
	switch (res_cfg->type) {
	case I10NM:
		switch (mscod) {
		case 0x0106: case 0x0107:
		case 0x0800: case 0x0804:
		case 0x0806 ... 0x0808:
		case 0x080a ... 0x080e:
		case 0x0810: case 0x0811:
		case 0x0816: case 0x081e:
		case 0x081f:
			return true;
		}

		break;
	case SPR:
		switch (mscod) {
		case 0x0800: case 0x0804:
		case 0x0806 ... 0x0808:
		case 0x080a ... 0x080e:
		case 0x0810: case 0x0811:
		case 0x0816: case 0x081e:
		case 0x081f:
			return true;
		}

		break;
	default:
		return false;
	}

	return false;
}

static bool i10nm_mc_decode_available(struct mce *mce)
{
#define ICX_IMCx_CHy		0x06666000
	u8 bank;

	if (!decoding_via_mca || mem_cfg_2lm)
		return false;

	if ((mce->status & (MCI_STATUS_MISCV | MCI_STATUS_ADDRV))
			!= (MCI_STATUS_MISCV | MCI_STATUS_ADDRV))
		return false;

	bank = mce->bank;

	switch (res_cfg->type) {
	case I10NM:
		/* Check whether the bank is one of {13,14,17,18,21,22,25,26} */
		if (!(ICX_IMCx_CHy & (1 << bank)))
			return false;
		break;
	case SPR:
		if (bank < 13 || bank > 20)
			return false;
		break;
	default:
		return false;
	}

	/* DDRT errors can't be decoded from MCA bank registers */
	if (MCI_MISC_ECC_MODE(mce->misc) == MCI_MISC_ECC_DDRT)
		return false;

	if (i10nm_mscod_is_ddrt(MCI_STATUS_MSCOD(mce->status)))
		return false;

	return true;
}

static bool i10nm_mc_decode(struct decoded_addr *res)
{
	struct mce *m = res->mce;
	struct skx_dev *d;
	u8 bank;

	if (!i10nm_mc_decode_available(m))
		return false;

	list_for_each_entry(d, i10nm_edac_list, list) {
		if (d->imc[0].src_id == m->socketid) {
			res->socket = m->socketid;
			res->dev = d;
			break;
		}
	}

	switch (res_cfg->type) {
	case I10NM:
		bank              = m->bank - 13;
		res->imc          = bank / 4;
		res->channel      = bank % 2;
		res->column       = GET_BITFIELD(m->misc, 9, 18) << 2;
		res->row          = GET_BITFIELD(m->misc, 19, 39);
		res->bank_group   = GET_BITFIELD(m->misc, 40, 41);
		res->bank_address = GET_BITFIELD(m->misc, 42, 43);
		res->bank_group  |= GET_BITFIELD(m->misc, 44, 44) << 2;
		res->rank         = GET_BITFIELD(m->misc, 56, 58);
		res->dimm         = res->rank >> 2;
		res->rank         = res->rank % 4;
		break;
	case SPR:
		bank              = m->bank - 13;
		res->imc          = bank / 2;
		res->channel      = bank % 2;
		res->column       = GET_BITFIELD(m->misc, 9, 18) << 2;
		res->row          = GET_BITFIELD(m->misc, 19, 36);
		res->bank_group   = GET_BITFIELD(m->misc, 37, 38);
		res->bank_address = GET_BITFIELD(m->misc, 39, 40);
		res->bank_group  |= GET_BITFIELD(m->misc, 41, 41) << 2;
		res->rank         = GET_BITFIELD(m->misc, 57, 57);
		res->dimm         = GET_BITFIELD(m->misc, 58, 58);
		break;
	default:
		return false;
	}

	if (!res->dev) {
		skx_printk(KERN_ERR, "No device for src_id %d imc %d\n",
			   m->socketid, res->imc);
		return false;
	}

	return true;
}

/**
 * get_gnr_mdev() - Get the PCI device of the @logical_idx-th DDR memory controller.
 *
 * @d            : The pointer to the structure of CPU socket EDAC device.
 * @logical_idx  : The logical index of the present memory controller (0 ~ max present MC# - 1).
 * @physical_idx : To store the corresponding physical index of @logical_idx.
 *
 * RETURNS       : The PCI device of the @logical_idx-th DDR memory controller, NULL on failure.
 */
static struct pci_dev *get_gnr_mdev(struct skx_dev *d, int logical_idx, int *physical_idx)
{
#define GNR_MAX_IMC_PCI_CNT	28

	struct pci_dev *mdev;
	int i, logical = 0;

	/*
	 * Detect present memory controllers from { PCI device: 8-5, function 7-1 }
	 */
	for (i = 0; i < GNR_MAX_IMC_PCI_CNT; i++) {
		mdev = pci_get_dev_wrapper(d->seg,
					   d->bus[res_cfg->ddr_mdev_bdf.bus],
					   res_cfg->ddr_mdev_bdf.dev + i / 7,
					   res_cfg->ddr_mdev_bdf.fun + i % 7);

		if (mdev) {
			if (logical == logical_idx) {
				*physical_idx = i;
				return mdev;
			}

			pci_dev_put(mdev);
			logical++;
		}
	}

	return NULL;
}

static u32 get_gnr_imc_mmio_offset(void)
{
	if (boot_cpu_data.x86_vfm == INTEL_GRANITERAPIDS_D)
		return I10NM_GNR_D_IMC_MMIO_OFFSET;

	return I10NM_GNR_IMC_MMIO_OFFSET;
}

/**
 * get_ddr_munit() - Get the resource of the i-th DDR memory controller.
 *
 * @d      : The pointer to the structure of CPU socket EDAC device.
 * @i      : The index of the CPU socket relative DDR memory controller.
 * @offset : To store the MMIO offset of the i-th DDR memory controller.
 * @size   : To store the MMIO size of the i-th DDR memory controller.
 *
 * RETURNS : The PCI device of the i-th DDR memory controller, NULL on failure.
 */
static struct pci_dev *get_ddr_munit(struct skx_dev *d, int i, u32 *offset, unsigned long *size)
{
	struct pci_dev *mdev;
	int physical_idx;
	u32 reg;

	switch (res_cfg->type) {
	case GNR:
		if (I10NM_GET_IMC_BAR(d, 0, reg)) {
			i10nm_printk(KERN_ERR, "Failed to get mc0 bar\n");
			return NULL;
		}

		mdev = get_gnr_mdev(d, i, &physical_idx);
		if (!mdev)
			return NULL;

		*offset = I10NM_GET_IMC_MMIO_OFFSET(reg) +
			  get_gnr_imc_mmio_offset() +
			  physical_idx * I10NM_GNR_IMC_MMIO_SIZE;
		*size   = I10NM_GNR_IMC_MMIO_SIZE;

		break;
	default:
		if (I10NM_GET_IMC_BAR(d, i, reg)) {
			i10nm_printk(KERN_ERR, "Failed to get mc%d bar\n", i);
			return NULL;
		}

		mdev = pci_get_dev_wrapper(d->seg,
					   d->bus[res_cfg->ddr_mdev_bdf.bus],
					   res_cfg->ddr_mdev_bdf.dev + i,
					   res_cfg->ddr_mdev_bdf.fun);
		if (!mdev)
			return NULL;

		*offset  = I10NM_GET_IMC_MMIO_OFFSET(reg);
		*size    = I10NM_GET_IMC_MMIO_SIZE(reg);
	}

	return mdev;
}

/**
 * i10nm_imc_absent() - Check whether the memory controller @imc is absent
 *
 * @imc    : The pointer to the structure of memory controller EDAC device.
 *
 * RETURNS : true if the memory controller EDAC device is absent, false otherwise.
 */
static bool i10nm_imc_absent(struct skx_imc *imc)
{
	u32 mcmtr;
	int i;

	switch (res_cfg->type) {
	case SPR:
		for (i = 0; i < res_cfg->ddr_chan_num; i++) {
			mcmtr = I10NM_GET_MCMTR(imc, i);
			edac_dbg(1, "ch%d mcmtr reg %x\n", i, mcmtr);
			if (mcmtr != ~0)
				return false;
		}

		/*
		 * Some workstations' absent memory controllers still
		 * appear as PCIe devices, misleading the EDAC driver.
		 * By observing that the MMIO registers of these absent
		 * memory controllers consistently hold the value of ~0.
		 *
		 * We identify a memory controller as absent by checking
		 * if its MMIO register "mcmtr" == ~0 in all its channels.
		 */
		return true;
	default:
		return false;
	}
}

static int i10nm_get_ddr_munits(void)
{
	struct pci_dev *mdev;
	void __iomem *mbase;
	unsigned long size;
	struct skx_dev *d;
	int i, lmc, j = 0;
	u32 reg, off;
	u64 base;

	list_for_each_entry(d, i10nm_edac_list, list) {
		d->util_all = pci_get_dev_wrapper(d->seg, d->bus[res_cfg->util_all_bdf.bus],
						  res_cfg->util_all_bdf.dev,
						  res_cfg->util_all_bdf.fun);
		if (!d->util_all)
			return -ENODEV;

		d->uracu = pci_get_dev_wrapper(d->seg, d->bus[res_cfg->uracu_bdf.bus],
					       res_cfg->uracu_bdf.dev,
					       res_cfg->uracu_bdf.fun);
		if (!d->uracu)
			return -ENODEV;

		if (I10NM_GET_SCK_BAR(d, reg)) {
			i10nm_printk(KERN_ERR, "Failed to socket bar\n");
			return -ENODEV;
		}

		base = I10NM_GET_SCK_MMIO_BASE(reg);
		edac_dbg(2, "socket%d mmio base 0x%llx (reg 0x%x)\n",
			 j++, base, reg);

		for (lmc = 0, i = 0; i < res_cfg->ddr_imc_num; i++) {
			mdev = get_ddr_munit(d, i, &off, &size);

			if (i == 0 && !mdev) {
				i10nm_printk(KERN_ERR, "No IMC found\n");
				return -ENODEV;
			}
			if (!mdev)
				continue;

			edac_dbg(2, "mc%d mmio base 0x%llx size 0x%lx (reg 0x%x)\n",
				 i, base + off, size, reg);

			mbase = ioremap(base + off, size);
			if (!mbase) {
				i10nm_printk(KERN_ERR, "Failed to ioremap 0x%llx\n",
					     base + off);
				return -ENODEV;
			}

			d->imc[lmc].mbase = mbase;
			if (i10nm_imc_absent(&d->imc[lmc])) {
				pci_dev_put(mdev);
				iounmap(mbase);
				d->imc[lmc].mbase = NULL;
				edac_dbg(2, "Skip absent mc%d\n", i);
				continue;
			} else {
				d->imc[lmc].mdev = mdev;
				if (res_cfg->type == SPR)
					skx_set_mc_mapping(d, i, lmc);
				lmc++;
			}
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

		lmc = res_cfg->ddr_imc_num;

		for (i = 0; i < res_cfg->hbm_imc_num; i++) {
			mdev = pci_get_dev_wrapper(d->seg, d->bus[res_cfg->hbm_mdev_bdf.bus],
						   res_cfg->hbm_mdev_bdf.dev + i / 4,
						   res_cfg->hbm_mdev_bdf.fun + i % 4);

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
	.ddr_imc_num		= 4,
	.ddr_chan_num		= 2,
	.ddr_dimm_num		= 2,
	.ddr_chan_mmio_sz	= 0x4000,
	.sad_all_bdf		= {1, 29, 0},
	.pcu_cr3_bdf		= {1, 30, 3},
	.util_all_bdf		= {1, 29, 1},
	.uracu_bdf		= {0, 0, 1},
	.ddr_mdev_bdf		= {0, 12, 0},
	.hbm_mdev_bdf		= {0, 12, 1},
	.sad_all_offset		= 0x108,
	.reg_rrl_ddr		= &icx_reg_rrl_ddr,
};

static struct res_config i10nm_cfg1 = {
	.type			= I10NM,
	.decs_did		= 0x3452,
	.busno_cfg_offset	= 0xd0,
	.ddr_imc_num		= 4,
	.ddr_chan_num		= 2,
	.ddr_dimm_num		= 2,
	.ddr_chan_mmio_sz	= 0x4000,
	.sad_all_bdf		= {1, 29, 0},
	.pcu_cr3_bdf		= {1, 30, 3},
	.util_all_bdf		= {1, 29, 1},
	.uracu_bdf		= {0, 0, 1},
	.ddr_mdev_bdf		= {0, 12, 0},
	.hbm_mdev_bdf		= {0, 12, 1},
	.sad_all_offset		= 0x108,
	.reg_rrl_ddr		= &icx_reg_rrl_ddr,
};

static struct res_config spr_cfg = {
	.type			= SPR,
	.decs_did		= 0x3252,
	.busno_cfg_offset	= 0xd0,
	.ddr_imc_num		= 4,
	.ddr_chan_num		= 2,
	.ddr_dimm_num		= 2,
	.hbm_imc_num		= 16,
	.hbm_chan_num		= 2,
	.hbm_dimm_num		= 1,
	.ddr_chan_mmio_sz	= 0x8000,
	.hbm_chan_mmio_sz	= 0x4000,
	.support_ddr5		= true,
	.sad_all_bdf		= {1, 10, 0},
	.pcu_cr3_bdf		= {1, 30, 3},
	.util_all_bdf		= {1, 29, 1},
	.uracu_bdf		= {0, 0, 1},
	.ddr_mdev_bdf		= {0, 12, 0},
	.hbm_mdev_bdf		= {0, 12, 1},
	.sad_all_offset		= 0x300,
	.reg_rrl_ddr		= &spr_reg_rrl_ddr,
	.reg_rrl_hbm[0]		= &spr_reg_rrl_hbm_pch0,
	.reg_rrl_hbm[1]		= &spr_reg_rrl_hbm_pch1,
};

static struct res_config gnr_cfg = {
	.type			= GNR,
	.decs_did		= 0x3252,
	.busno_cfg_offset	= 0xd0,
	.ddr_imc_num		= 12,
	.ddr_chan_num		= 1,
	.ddr_dimm_num		= 2,
	.ddr_chan_mmio_sz	= 0x4000,
	.support_ddr5		= true,
	.sad_all_bdf		= {0, 13, 0},
	.pcu_cr3_bdf		= {0, 5, 0},
	.util_all_bdf		= {0, 13, 1},
	.uracu_bdf		= {0, 0, 1},
	.ddr_mdev_bdf		= {0, 5, 1},
	.sad_all_offset		= 0x300,
	.reg_rrl_ddr		= &gnr_reg_rrl_ddr,
};

static const struct x86_cpu_id i10nm_cpuids[] = {
	X86_MATCH_VFM_STEPS(INTEL_ATOM_TREMONT_D, X86_STEP_MIN,		 0x3, &i10nm_cfg0),
	X86_MATCH_VFM_STEPS(INTEL_ATOM_TREMONT_D,	   0x4,	X86_STEP_MAX, &i10nm_cfg1),
	X86_MATCH_VFM_STEPS(INTEL_ICELAKE_X,	  X86_STEP_MIN,		 0x3, &i10nm_cfg0),
	X86_MATCH_VFM_STEPS(INTEL_ICELAKE_X,		   0x4, X86_STEP_MAX, &i10nm_cfg1),
	X86_MATCH_VFM(	    INTEL_ICELAKE_D,				      &i10nm_cfg1),

	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X, &spr_cfg),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X,  &spr_cfg),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_X,  &gnr_cfg),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_D,  &gnr_cfg),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT_X, &gnr_cfg),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT,   &gnr_cfg),
	X86_MATCH_VFM(INTEL_ATOM_DARKMONT_X,  &gnr_cfg),
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

static bool i10nm_channel_disabled(struct skx_imc *imc, int chan)
{
	u32 mcmtr = I10NM_GET_MCMTR(imc, chan);

	edac_dbg(1, "mc%d ch%d mcmtr reg %x\n", imc->mc, chan, mcmtr);

	return (mcmtr == ~0 || GET_BITFIELD(mcmtr, 18, 18));
}

static int i10nm_get_dimm_config(struct mem_ctl_info *mci,
				 struct res_config *cfg)
{
	struct skx_pvt *pvt = mci->pvt_info;
	struct skx_imc *imc = pvt->imc;
	u32 mtr, mcddrtcfg = 0;
	struct dimm_info *dimm;
	int i, j, ndimms;

	for (i = 0; i < imc->num_channels; i++) {
		if (!imc->mbase)
			continue;

		if (i10nm_channel_disabled(imc, i)) {
			edac_dbg(1, "mc%d ch%d is disabled.\n", imc->mc, i);
			continue;
		}

		ndimms = 0;

		if (res_cfg->type != GNR)
			mcddrtcfg = I10NM_GET_MCDDRTCFG(imc, i);

		for (j = 0; j < imc->num_dimms; j++) {
			dimm = edac_get_dimm(mci, i, j, 0);
			mtr = I10NM_GET_DIMMMTR(imc, i, j);
			edac_dbg(1, "dimmmtr 0x%x mcddrtcfg 0x%x (mc%d ch%d dimm%d)\n",
				 mtr, mcddrtcfg, imc->mc, i, j);

			if (IS_DIMM_PRESENT(mtr))
				ndimms += skx_get_dimm_info(mtr, 0, 0, dimm,
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

static int __init i10nm_init(void)
{
	u8 mc = 0, src_id = 0;
	const struct x86_cpu_id *id;
	struct res_config *cfg;
	const char *owner;
	struct skx_dev *d;
	int rc, i, off[3] = {0xd0, 0xc8, 0xcc};
	u64 tolm, tohm;
	int imc_num;

	edac_dbg(2, "\n");

	if (ghes_get_devices())
		return -EBUSY;

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	if (cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	id = x86_match_cpu(i10nm_cpuids);
	if (!id)
		return -ENODEV;

	cfg = (struct res_config *)id->driver_data;
	skx_set_res_cfg(cfg);
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

	rc = i10nm_get_imc_num(cfg);
	if (rc < 0)
		goto fail;

	mem_cfg_2lm = i10nm_check_2lm(cfg);
	skx_set_mem_cfg(mem_cfg_2lm);

	rc = i10nm_get_ddr_munits();

	if (i10nm_get_hbm_munits() && rc)
		goto fail;

	imc_num = res_cfg->ddr_imc_num + res_cfg->hbm_imc_num;

	list_for_each_entry(d, i10nm_edac_list, list) {
		rc = skx_get_src_id(d, 0xf8, &src_id);
		if (rc < 0)
			goto fail;

		edac_dbg(2, "src_id = %d\n", src_id);
		for (i = 0; i < imc_num; i++) {
			if (!d->imc[i].mdev)
				continue;

			d->imc[i].mc  = mc++;
			d->imc[i].lmc = i;
			d->imc[i].src_id = src_id;
			if (d->imc[i].hbm_mc) {
				d->imc[i].chan_mmio_sz = cfg->hbm_chan_mmio_sz;
				d->imc[i].num_channels = cfg->hbm_chan_num;
				d->imc[i].num_dimms    = cfg->hbm_dimm_num;
			} else {
				d->imc[i].chan_mmio_sz = cfg->ddr_chan_mmio_sz;
				d->imc[i].num_channels = cfg->ddr_chan_num;
				d->imc[i].num_dimms    = cfg->ddr_dimm_num;
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
	skx_setup_debug("i10nm_test");

	if (retry_rd_err_log && res_cfg->reg_rrl_ddr) {
		skx_set_decode(i10nm_mc_decode, show_retry_rd_err_log);
		if (retry_rd_err_log == 2)
			enable_retry_rd_err_log(true);
	} else {
		skx_set_decode(i10nm_mc_decode, NULL);
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

	if (retry_rd_err_log && res_cfg->reg_rrl_ddr) {
		skx_set_decode(NULL, NULL);
		if (retry_rd_err_log == 2)
			enable_retry_rd_err_log(false);
	}

	skx_teardown_debug();
	mce_unregister_decode_chain(&i10nm_mce_dec);
	skx_adxl_put();
	skx_remove();
}

module_init(i10nm_init);
module_exit(i10nm_exit);

static int set_decoding_via_mca(const char *buf, const struct kernel_param *kp)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);

	if (ret || val > 1)
		return -EINVAL;

	if (val && mem_cfg_2lm) {
		i10nm_printk(KERN_NOTICE, "Decoding errors via MCA banks for 2LM isn't supported yet\n");
		return -EIO;
	}

	ret = param_set_int(buf, kp);

	return ret;
}

static const struct kernel_param_ops decoding_via_mca_param_ops = {
	.set = set_decoding_via_mca,
	.get = param_get_int,
};

module_param_cb(decoding_via_mca, &decoding_via_mca_param_ops, &decoding_via_mca, 0644);
MODULE_PARM_DESC(decoding_via_mca, "decoding_via_mca: 0=off(default), 1=enable");

module_param(retry_rd_err_log, int, 0444);
MODULE_PARM_DESC(retry_rd_err_log, "retry_rd_err_log: 0=off(default), 1=bios(Linux doesn't reset any control bits, but just reports values.), 2=linux(Linux tries to take control and resets mode bits, clear valid/UC bits after reading.)");

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MC Driver for Intel 10nm server processors");
