// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Shared code by both skx_edac and i10nm_edac. Originally split out
 * from the skx_edac driver.
 *
 * This file is linked into both skx_edac and i10nm_edac drivers. In
 * order to avoid link errors, this file must be like a pure library
 * without including symbols and defines which would otherwise conflict,
 * when linked once into a module and into a built-in object, at the
 * same time. For example, __this_module symbol references when that
 * file is being linked into a built-in object.
 *
 * Copyright (c) 2018, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/adxl.h>
#include <acpi/nfit.h>
#include <asm/mce.h>
#include "edac_module.h"
#include "skx_common.h"

static const char * const component_names[] = {
	[INDEX_SOCKET]		= "ProcessorSocketId",
	[INDEX_MEMCTRL]		= "MemoryControllerId",
	[INDEX_CHANNEL]		= "ChannelId",
	[INDEX_DIMM]		= "DimmSlotId",
	[INDEX_CS]		= "ChipSelect",
	[INDEX_NM_MEMCTRL]	= "NmMemoryControllerId",
	[INDEX_NM_CHANNEL]	= "NmChannelId",
	[INDEX_NM_DIMM]		= "NmDimmSlotId",
	[INDEX_NM_CS]		= "NmChipSelect",
};

static int component_indices[ARRAY_SIZE(component_names)];
static int adxl_component_count;
static const char * const *adxl_component_names;
static u64 *adxl_values;
static char *adxl_msg;
static unsigned long adxl_nm_bitmap;

static char skx_msg[MSG_SIZE];
static skx_decode_f driver_decode;
static skx_show_retry_log_f skx_show_retry_rd_err_log;
static u64 skx_tolm, skx_tohm;
static LIST_HEAD(dev_edac_list);
static bool skx_mem_cfg_2lm;

int skx_adxl_get(void)
{
	const char * const *names;
	int i, j;

	names = adxl_get_component_names();
	if (!names) {
		skx_printk(KERN_NOTICE, "No firmware support for address translation.\n");
		return -ENODEV;
	}

	for (i = 0; i < INDEX_MAX; i++) {
		for (j = 0; names[j]; j++) {
			if (!strcmp(component_names[i], names[j])) {
				component_indices[i] = j;

				if (i >= INDEX_NM_FIRST)
					adxl_nm_bitmap |= 1 << i;

				break;
			}
		}

		if (!names[j] && i < INDEX_NM_FIRST)
			goto err;
	}

	if (skx_mem_cfg_2lm) {
		if (!adxl_nm_bitmap)
			skx_printk(KERN_NOTICE, "Not enough ADXL components for 2-level memory.\n");
		else
			edac_dbg(2, "adxl_nm_bitmap: 0x%lx\n", adxl_nm_bitmap);
	}

	adxl_component_names = names;
	while (*names++)
		adxl_component_count++;

	adxl_values = kcalloc(adxl_component_count, sizeof(*adxl_values),
			      GFP_KERNEL);
	if (!adxl_values) {
		adxl_component_count = 0;
		return -ENOMEM;
	}

	adxl_msg = kzalloc(MSG_SIZE, GFP_KERNEL);
	if (!adxl_msg) {
		adxl_component_count = 0;
		kfree(adxl_values);
		return -ENOMEM;
	}

	return 0;
err:
	skx_printk(KERN_ERR, "'%s' is not matched from DSM parameters: ",
		   component_names[i]);
	for (j = 0; names[j]; j++)
		skx_printk(KERN_CONT, "%s ", names[j]);
	skx_printk(KERN_CONT, "\n");

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(skx_adxl_get);

void skx_adxl_put(void)
{
	kfree(adxl_values);
	kfree(adxl_msg);
}
EXPORT_SYMBOL_GPL(skx_adxl_put);

static bool skx_adxl_decode(struct decoded_addr *res, bool error_in_1st_level_mem)
{
	struct skx_dev *d;
	int i, len = 0;

	if (res->addr >= skx_tohm || (res->addr >= skx_tolm &&
				      res->addr < BIT_ULL(32))) {
		edac_dbg(0, "Address 0x%llx out of range\n", res->addr);
		return false;
	}

	if (adxl_decode(res->addr, adxl_values)) {
		edac_dbg(0, "Failed to decode 0x%llx\n", res->addr);
		return false;
	}

	res->socket  = (int)adxl_values[component_indices[INDEX_SOCKET]];
	if (error_in_1st_level_mem) {
		res->imc     = (adxl_nm_bitmap & BIT_NM_MEMCTRL) ?
			       (int)adxl_values[component_indices[INDEX_NM_MEMCTRL]] : -1;
		res->channel = (adxl_nm_bitmap & BIT_NM_CHANNEL) ?
			       (int)adxl_values[component_indices[INDEX_NM_CHANNEL]] : -1;
		res->dimm    = (adxl_nm_bitmap & BIT_NM_DIMM) ?
			       (int)adxl_values[component_indices[INDEX_NM_DIMM]] : -1;
		res->cs      = (adxl_nm_bitmap & BIT_NM_CS) ?
			       (int)adxl_values[component_indices[INDEX_NM_CS]] : -1;
	} else {
		res->imc     = (int)adxl_values[component_indices[INDEX_MEMCTRL]];
		res->channel = (int)adxl_values[component_indices[INDEX_CHANNEL]];
		res->dimm    = (int)adxl_values[component_indices[INDEX_DIMM]];
		res->cs      = (int)adxl_values[component_indices[INDEX_CS]];
	}

	if (res->imc > NUM_IMC - 1 || res->imc < 0) {
		skx_printk(KERN_ERR, "Bad imc %d\n", res->imc);
		return false;
	}

	list_for_each_entry(d, &dev_edac_list, list) {
		if (d->imc[0].src_id == res->socket) {
			res->dev = d;
			break;
		}
	}

	if (!res->dev) {
		skx_printk(KERN_ERR, "No device for src_id %d imc %d\n",
			   res->socket, res->imc);
		return false;
	}

	for (i = 0; i < adxl_component_count; i++) {
		if (adxl_values[i] == ~0x0ull)
			continue;

		len += snprintf(adxl_msg + len, MSG_SIZE - len, " %s:0x%llx",
				adxl_component_names[i], adxl_values[i]);
		if (MSG_SIZE - len <= 0)
			break;
	}

	res->decoded_by_adxl = true;

	return true;
}

void skx_set_mem_cfg(bool mem_cfg_2lm)
{
	skx_mem_cfg_2lm = mem_cfg_2lm;
}
EXPORT_SYMBOL_GPL(skx_set_mem_cfg);

void skx_set_decode(skx_decode_f decode, skx_show_retry_log_f show_retry_log)
{
	driver_decode = decode;
	skx_show_retry_rd_err_log = show_retry_log;
}
EXPORT_SYMBOL_GPL(skx_set_decode);

int skx_get_src_id(struct skx_dev *d, int off, u8 *id)
{
	u32 reg;

	if (pci_read_config_dword(d->util_all, off, &reg)) {
		skx_printk(KERN_ERR, "Failed to read src id\n");
		return -ENODEV;
	}

	*id = GET_BITFIELD(reg, 12, 14);
	return 0;
}
EXPORT_SYMBOL_GPL(skx_get_src_id);

int skx_get_node_id(struct skx_dev *d, u8 *id)
{
	u32 reg;

	if (pci_read_config_dword(d->util_all, 0xf4, &reg)) {
		skx_printk(KERN_ERR, "Failed to read node id\n");
		return -ENODEV;
	}

	*id = GET_BITFIELD(reg, 0, 2);
	return 0;
}
EXPORT_SYMBOL_GPL(skx_get_node_id);

static int get_width(u32 mtr)
{
	switch (GET_BITFIELD(mtr, 8, 9)) {
	case 0:
		return DEV_X4;
	case 1:
		return DEV_X8;
	case 2:
		return DEV_X16;
	}
	return DEV_UNKNOWN;
}

/*
 * We use the per-socket device @cfg->did to count how many sockets are present,
 * and to detemine which PCI buses are associated with each socket. Allocate
 * and build the full list of all the skx_dev structures that we need here.
 */
int skx_get_all_bus_mappings(struct res_config *cfg, struct list_head **list)
{
	struct pci_dev *pdev, *prev;
	struct skx_dev *d;
	u32 reg;
	int ndev = 0;

	prev = NULL;
	for (;;) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, cfg->decs_did, prev);
		if (!pdev)
			break;
		ndev++;
		d = kzalloc(sizeof(*d), GFP_KERNEL);
		if (!d) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}

		if (pci_read_config_dword(pdev, cfg->busno_cfg_offset, &reg)) {
			kfree(d);
			pci_dev_put(pdev);
			skx_printk(KERN_ERR, "Failed to read bus idx\n");
			return -ENODEV;
		}

		d->bus[0] = GET_BITFIELD(reg, 0, 7);
		d->bus[1] = GET_BITFIELD(reg, 8, 15);
		if (cfg->type == SKX) {
			d->seg = pci_domain_nr(pdev->bus);
			d->bus[2] = GET_BITFIELD(reg, 16, 23);
			d->bus[3] = GET_BITFIELD(reg, 24, 31);
		} else {
			d->seg = GET_BITFIELD(reg, 16, 23);
		}

		edac_dbg(2, "busses: 0x%x, 0x%x, 0x%x, 0x%x\n",
			 d->bus[0], d->bus[1], d->bus[2], d->bus[3]);
		list_add_tail(&d->list, &dev_edac_list);
		prev = pdev;
	}

	if (list)
		*list = &dev_edac_list;
	return ndev;
}
EXPORT_SYMBOL_GPL(skx_get_all_bus_mappings);

int skx_get_hi_lo(unsigned int did, int off[], u64 *tolm, u64 *tohm)
{
	struct pci_dev *pdev;
	u32 reg;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, did, NULL);
	if (!pdev) {
		edac_dbg(2, "Can't get tolm/tohm\n");
		return -ENODEV;
	}

	if (pci_read_config_dword(pdev, off[0], &reg)) {
		skx_printk(KERN_ERR, "Failed to read tolm\n");
		goto fail;
	}
	skx_tolm = reg;

	if (pci_read_config_dword(pdev, off[1], &reg)) {
		skx_printk(KERN_ERR, "Failed to read lower tohm\n");
		goto fail;
	}
	skx_tohm = reg;

	if (pci_read_config_dword(pdev, off[2], &reg)) {
		skx_printk(KERN_ERR, "Failed to read upper tohm\n");
		goto fail;
	}
	skx_tohm |= (u64)reg << 32;

	pci_dev_put(pdev);
	*tolm = skx_tolm;
	*tohm = skx_tohm;
	edac_dbg(2, "tolm = 0x%llx tohm = 0x%llx\n", skx_tolm, skx_tohm);
	return 0;
fail:
	pci_dev_put(pdev);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(skx_get_hi_lo);

static int skx_get_dimm_attr(u32 reg, int lobit, int hibit, int add,
			     int minval, int maxval, const char *name)
{
	u32 val = GET_BITFIELD(reg, lobit, hibit);

	if (val < minval || val > maxval) {
		edac_dbg(2, "bad %s = %d (raw=0x%x)\n", name, val, reg);
		return -EINVAL;
	}
	return val + add;
}

#define numrank(reg)	skx_get_dimm_attr(reg, 12, 13, 0, 0, 2, "ranks")
#define numrow(reg)	skx_get_dimm_attr(reg, 2, 4, 12, 1, 6, "rows")
#define numcol(reg)	skx_get_dimm_attr(reg, 0, 1, 10, 0, 2, "cols")

int skx_get_dimm_info(u32 mtr, u32 mcmtr, u32 amap, struct dimm_info *dimm,
		      struct skx_imc *imc, int chan, int dimmno,
		      struct res_config *cfg)
{
	int  banks, ranks, rows, cols, npages;
	enum mem_type mtype;
	u64 size;

	ranks = numrank(mtr);
	rows = numrow(mtr);
	cols = imc->hbm_mc ? 6 : numcol(mtr);

	if (imc->hbm_mc) {
		banks = 32;
		mtype = MEM_HBM2;
	} else if (cfg->support_ddr5) {
		banks = 32;
		mtype = MEM_DDR5;
	} else {
		banks = 16;
		mtype = MEM_DDR4;
	}

	/*
	 * Compute size in 8-byte (2^3) words, then shift to MiB (2^20)
	 */
	size = ((1ull << (rows + cols + ranks)) * banks) >> (20 - 3);
	npages = MiB_TO_PAGES(size);

	edac_dbg(0, "mc#%d: channel %d, dimm %d, %lld MiB (%d pages) bank: %d, rank: %d, row: 0x%x, col: 0x%x\n",
		 imc->mc, chan, dimmno, size, npages,
		 banks, 1 << ranks, rows, cols);

	imc->chan[chan].dimms[dimmno].close_pg = GET_BITFIELD(mcmtr, 0, 0);
	imc->chan[chan].dimms[dimmno].bank_xor_enable = GET_BITFIELD(mcmtr, 9, 9);
	imc->chan[chan].dimms[dimmno].fine_grain_bank = GET_BITFIELD(amap, 0, 0);
	imc->chan[chan].dimms[dimmno].rowbits = rows;
	imc->chan[chan].dimms[dimmno].colbits = cols;

	dimm->nr_pages = npages;
	dimm->grain = 32;
	dimm->dtype = get_width(mtr);
	dimm->mtype = mtype;
	dimm->edac_mode = EDAC_SECDED; /* likely better than this */

	if (imc->hbm_mc)
		snprintf(dimm->label, sizeof(dimm->label), "CPU_SrcID#%u_HBMC#%u_Chan#%u",
			 imc->src_id, imc->lmc, chan);
	else
		snprintf(dimm->label, sizeof(dimm->label), "CPU_SrcID#%u_MC#%u_Chan#%u_DIMM#%u",
			 imc->src_id, imc->lmc, chan, dimmno);

	return 1;
}
EXPORT_SYMBOL_GPL(skx_get_dimm_info);

int skx_get_nvdimm_info(struct dimm_info *dimm, struct skx_imc *imc,
			int chan, int dimmno, const char *mod_str)
{
	int smbios_handle;
	u32 dev_handle;
	u16 flags;
	u64 size = 0;

	dev_handle = ACPI_NFIT_BUILD_DEVICE_HANDLE(dimmno, chan, imc->lmc,
						   imc->src_id, 0);

	smbios_handle = nfit_get_smbios_id(dev_handle, &flags);
	if (smbios_handle == -EOPNOTSUPP) {
		pr_warn_once("%s: Can't find size of NVDIMM. Try enabling CONFIG_ACPI_NFIT\n", mod_str);
		goto unknown_size;
	}

	if (smbios_handle < 0) {
		skx_printk(KERN_ERR, "Can't find handle for NVDIMM ADR=0x%x\n", dev_handle);
		goto unknown_size;
	}

	if (flags & ACPI_NFIT_MEM_MAP_FAILED) {
		skx_printk(KERN_ERR, "NVDIMM ADR=0x%x is not mapped\n", dev_handle);
		goto unknown_size;
	}

	size = dmi_memdev_size(smbios_handle);
	if (size == ~0ull)
		skx_printk(KERN_ERR, "Can't find size for NVDIMM ADR=0x%x/SMBIOS=0x%x\n",
			   dev_handle, smbios_handle);

unknown_size:
	dimm->nr_pages = size >> PAGE_SHIFT;
	dimm->grain = 32;
	dimm->dtype = DEV_UNKNOWN;
	dimm->mtype = MEM_NVDIMM;
	dimm->edac_mode = EDAC_SECDED; /* likely better than this */

	edac_dbg(0, "mc#%d: channel %d, dimm %d, %llu MiB (%u pages)\n",
		 imc->mc, chan, dimmno, size >> 20, dimm->nr_pages);

	snprintf(dimm->label, sizeof(dimm->label), "CPU_SrcID#%u_MC#%u_Chan#%u_DIMM#%u",
		 imc->src_id, imc->lmc, chan, dimmno);

	return (size == 0 || size == ~0ull) ? 0 : 1;
}
EXPORT_SYMBOL_GPL(skx_get_nvdimm_info);

int skx_register_mci(struct skx_imc *imc, struct pci_dev *pdev,
		     const char *ctl_name, const char *mod_str,
		     get_dimm_config_f get_dimm_config,
		     struct res_config *cfg)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct skx_pvt *pvt;
	int rc;

	/* Allocate a new MC control structure */
	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = NUM_CHANNELS;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = NUM_DIMMS;
	layers[1].is_virt_csrow = true;
	mci = edac_mc_alloc(imc->mc, ARRAY_SIZE(layers), layers,
			    sizeof(struct skx_pvt));

	if (unlikely(!mci))
		return -ENOMEM;

	edac_dbg(0, "MC#%d: mci = %p\n", imc->mc, mci);

	/* Associate skx_dev and mci for future usage */
	imc->mci = mci;
	pvt = mci->pvt_info;
	pvt->imc = imc;

	mci->ctl_name = kasprintf(GFP_KERNEL, "%s#%d IMC#%d", ctl_name,
				  imc->node_id, imc->lmc);
	if (!mci->ctl_name) {
		rc = -ENOMEM;
		goto fail0;
	}

	mci->mtype_cap = MEM_FLAG_DDR4 | MEM_FLAG_NVDIMM;
	if (cfg->support_ddr5)
		mci->mtype_cap |= MEM_FLAG_DDR5;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = mod_str;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	rc = get_dimm_config(mci, cfg);
	if (rc < 0)
		goto fail;

	/* Record ptr to the generic device */
	mci->pdev = &pdev->dev;

	/* Add this new MC control structure to EDAC's list of MCs */
	if (unlikely(edac_mc_add_mc(mci))) {
		edac_dbg(0, "MC: failed edac_mc_add_mc()\n");
		rc = -EINVAL;
		goto fail;
	}

	return 0;

fail:
	kfree(mci->ctl_name);
fail0:
	edac_mc_free(mci);
	imc->mci = NULL;
	return rc;
}
EXPORT_SYMBOL_GPL(skx_register_mci);

static void skx_unregister_mci(struct skx_imc *imc)
{
	struct mem_ctl_info *mci = imc->mci;

	if (!mci)
		return;

	edac_dbg(0, "MC%d: mci = %p\n", imc->mc, mci);

	/* Remove MC sysfs nodes */
	edac_mc_del_mc(mci->pdev);

	edac_dbg(1, "%s: free mci struct\n", mci->ctl_name);
	kfree(mci->ctl_name);
	edac_mc_free(mci);
}

static void skx_mce_output_error(struct mem_ctl_info *mci,
				 const struct mce *m,
				 struct decoded_addr *res)
{
	enum hw_event_mc_err_type tp_event;
	char *optype;
	bool ripv = GET_BITFIELD(m->mcgstatus, 0, 0);
	bool overflow = GET_BITFIELD(m->status, 62, 62);
	bool uncorrected_error = GET_BITFIELD(m->status, 61, 61);
	bool scrub_err = false;
	bool recoverable;
	int len;
	u32 core_err_cnt = GET_BITFIELD(m->status, 38, 52);
	u32 mscod = GET_BITFIELD(m->status, 16, 31);
	u32 errcode = GET_BITFIELD(m->status, 0, 15);
	u32 optypenum = GET_BITFIELD(m->status, 4, 6);

	recoverable = GET_BITFIELD(m->status, 56, 56);

	if (uncorrected_error) {
		core_err_cnt = 1;
		if (ripv) {
			tp_event = HW_EVENT_ERR_UNCORRECTED;
		} else {
			tp_event = HW_EVENT_ERR_FATAL;
		}
	} else {
		tp_event = HW_EVENT_ERR_CORRECTED;
	}

	switch (optypenum) {
	case 0:
		optype = "generic undef request error";
		break;
	case 1:
		optype = "memory read error";
		break;
	case 2:
		optype = "memory write error";
		break;
	case 3:
		optype = "addr/cmd error";
		break;
	case 4:
		optype = "memory scrubbing error";
		scrub_err = true;
		break;
	default:
		optype = "reserved";
		break;
	}

	if (res->decoded_by_adxl) {
		len = snprintf(skx_msg, MSG_SIZE, "%s%s err_code:0x%04x:0x%04x %s",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode, adxl_msg);
	} else {
		len = snprintf(skx_msg, MSG_SIZE,
			 "%s%s err_code:0x%04x:0x%04x ProcessorSocketId:0x%x MemoryControllerId:0x%x PhysicalRankId:0x%x Row:0x%x Column:0x%x Bank:0x%x BankGroup:0x%x",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode,
			 res->socket, res->imc, res->rank,
			 res->row, res->column, res->bank_address, res->bank_group);
	}

	if (skx_show_retry_rd_err_log)
		skx_show_retry_rd_err_log(res, skx_msg + len, MSG_SIZE - len, scrub_err);

	edac_dbg(0, "%s\n", skx_msg);

	/* Call the helper to output message */
	edac_mc_handle_error(tp_event, mci, core_err_cnt,
			     m->addr >> PAGE_SHIFT, m->addr & ~PAGE_MASK, 0,
			     res->channel, res->dimm, -1,
			     optype, skx_msg);
}

static bool skx_error_in_1st_level_mem(const struct mce *m)
{
	u32 errcode;

	if (!skx_mem_cfg_2lm)
		return false;

	errcode = GET_BITFIELD(m->status, 0, 15) & MCACOD_MEM_ERR_MASK;

	return errcode == MCACOD_EXT_MEM_ERR;
}

static bool skx_error_in_mem(const struct mce *m)
{
	u32 errcode;

	errcode = GET_BITFIELD(m->status, 0, 15) & MCACOD_MEM_ERR_MASK;

	return (errcode == MCACOD_MEM_CTL_ERR || errcode == MCACOD_EXT_MEM_ERR);
}

int skx_mce_check_error(struct notifier_block *nb, unsigned long val,
			void *data)
{
	struct mce *mce = (struct mce *)data;
	struct decoded_addr res;
	struct mem_ctl_info *mci;
	char *type;

	if (mce->kflags & MCE_HANDLED_CEC)
		return NOTIFY_DONE;

	/* Ignore unless this is memory related with an address */
	if (!skx_error_in_mem(mce) || !(mce->status & MCI_STATUS_ADDRV))
		return NOTIFY_DONE;

	memset(&res, 0, sizeof(res));
	res.mce  = mce;
	res.addr = mce->addr & MCI_ADDR_PHYSADDR;
	if (!pfn_to_online_page(res.addr >> PAGE_SHIFT) && !arch_is_platform_page(res.addr)) {
		pr_err("Invalid address 0x%llx in IA32_MC%d_ADDR\n", mce->addr, mce->bank);
		return NOTIFY_DONE;
	}

	/* Try driver decoder first */
	if (!(driver_decode && driver_decode(&res))) {
		/* Then try firmware decoder (ACPI DSM methods) */
		if (!(adxl_component_count && skx_adxl_decode(&res, skx_error_in_1st_level_mem(mce))))
			return NOTIFY_DONE;
	}

	mci = res.dev->imc[res.imc].mci;

	if (!mci)
		return NOTIFY_DONE;

	if (mce->mcgstatus & MCG_STATUS_MCIP)
		type = "Exception";
	else
		type = "Event";

	skx_mc_printk(mci, KERN_DEBUG, "HANDLING MCE MEMORY ERROR\n");

	skx_mc_printk(mci, KERN_DEBUG, "CPU %d: Machine Check %s: 0x%llx "
			   "Bank %d: 0x%llx\n", mce->extcpu, type,
			   mce->mcgstatus, mce->bank, mce->status);
	skx_mc_printk(mci, KERN_DEBUG, "TSC 0x%llx ", mce->tsc);
	skx_mc_printk(mci, KERN_DEBUG, "ADDR 0x%llx ", mce->addr);
	skx_mc_printk(mci, KERN_DEBUG, "MISC 0x%llx ", mce->misc);

	skx_mc_printk(mci, KERN_DEBUG, "PROCESSOR %u:0x%x TIME %llu SOCKET "
			   "%u APIC 0x%x\n", mce->cpuvendor, mce->cpuid,
			   mce->time, mce->socketid, mce->apicid);

	skx_mce_output_error(mci, mce, &res);

	mce->kflags |= MCE_HANDLED_EDAC;
	return NOTIFY_DONE;
}
EXPORT_SYMBOL_GPL(skx_mce_check_error);

void skx_remove(void)
{
	int i, j;
	struct skx_dev *d, *tmp;

	edac_dbg(0, "\n");

	list_for_each_entry_safe(d, tmp, &dev_edac_list, list) {
		list_del(&d->list);
		for (i = 0; i < NUM_IMC; i++) {
			if (d->imc[i].mci)
				skx_unregister_mci(&d->imc[i]);

			if (d->imc[i].mdev)
				pci_dev_put(d->imc[i].mdev);

			if (d->imc[i].mbase)
				iounmap(d->imc[i].mbase);

			for (j = 0; j < NUM_CHANNELS; j++) {
				if (d->imc[i].chan[j].cdev)
					pci_dev_put(d->imc[i].chan[j].cdev);
			}
		}
		if (d->util_all)
			pci_dev_put(d->util_all);
		if (d->pcu_cr3)
			pci_dev_put(d->pcu_cr3);
		if (d->sad_all)
			pci_dev_put(d->sad_all);
		if (d->uracu)
			pci_dev_put(d->uracu);

		kfree(d);
	}
}
EXPORT_SYMBOL_GPL(skx_remove);

#ifdef CONFIG_EDAC_DEBUG
/*
 * Debug feature.
 * Exercise the address decode logic by writing an address to
 * /sys/kernel/debug/edac/{skx,i10nm}_test/addr.
 */
static struct dentry *skx_test;

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

void skx_setup_debug(const char *name)
{
	skx_test = edac_debugfs_create_dir(name);
	if (!skx_test)
		return;

	if (!edac_debugfs_create_file("addr", 0200, skx_test,
				      NULL, &fops_u64_wo)) {
		debugfs_remove(skx_test);
		skx_test = NULL;
	}
}
EXPORT_SYMBOL_GPL(skx_setup_debug);

void skx_teardown_debug(void)
{
	debugfs_remove_recursive(skx_test);
}
EXPORT_SYMBOL_GPL(skx_teardown_debug);
#endif /*CONFIG_EDAC_DEBUG*/

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Luck");
MODULE_DESCRIPTION("MC Driver for Intel server processors");
