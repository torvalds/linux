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
	[INDEX_SOCKET]	= "ProcessorSocketId",
	[INDEX_MEMCTRL]	= "MemoryControllerId",
	[INDEX_CHANNEL]	= "ChannelId",
	[INDEX_DIMM]	= "DimmSlotId",
};

static int component_indices[ARRAY_SIZE(component_names)];
static int adxl_component_count;
static const char * const *adxl_component_names;
static u64 *adxl_values;
static char *adxl_msg;

static char skx_msg[MSG_SIZE];
static skx_decode_f skx_decode;
static skx_show_retry_log_f skx_show_retry_rd_err_log;
static u64 skx_tolm, skx_tohm;
static LIST_HEAD(dev_edac_list);

int __init skx_adxl_get(void)
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
				break;
			}
		}

		if (!names[j])
			goto err;
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

void __exit skx_adxl_put(void)
{
	kfree(adxl_values);
	kfree(adxl_msg);
}

static bool skx_adxl_decode(struct decoded_addr *res)
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
	res->imc     = (int)adxl_values[component_indices[INDEX_MEMCTRL]];
	res->channel = (int)adxl_values[component_indices[INDEX_CHANNEL]];
	res->dimm    = (int)adxl_values[component_indices[INDEX_DIMM]];

	if (res->imc > NUM_IMC - 1) {
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

	return true;
}

void skx_set_decode(skx_decode_f decode, skx_show_retry_log_f show_retry_log)
{
	skx_decode = decode;
	skx_show_retry_rd_err_log = show_retry_log;
}

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
 * We use the per-socket device @did to count how many sockets are present,
 * and to detemine which PCI buses are associated with each socket. Allocate
 * and build the full list of all the skx_dev structures that we need here.
 */
int skx_get_all_bus_mappings(unsigned int did, int off, enum type type,
			     struct list_head **list)
{
	struct pci_dev *pdev, *prev;
	struct skx_dev *d;
	u32 reg;
	int ndev = 0;

	prev = NULL;
	for (;;) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, did, prev);
		if (!pdev)
			break;
		ndev++;
		d = kzalloc(sizeof(*d), GFP_KERNEL);
		if (!d) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}

		if (pci_read_config_dword(pdev, off, &reg)) {
			kfree(d);
			pci_dev_put(pdev);
			skx_printk(KERN_ERR, "Failed to read bus idx\n");
			return -ENODEV;
		}

		d->bus[0] = GET_BITFIELD(reg, 0, 7);
		d->bus[1] = GET_BITFIELD(reg, 8, 15);
		if (type == SKX) {
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

int skx_get_dimm_info(u32 mtr, u32 amap, struct dimm_info *dimm,
		      struct skx_imc *imc, int chan, int dimmno)
{
	int  banks = 16, ranks, rows, cols, npages;
	u64 size;

	ranks = numrank(mtr);
	rows = numrow(mtr);
	cols = numcol(mtr);

	/*
	 * Compute size in 8-byte (2^3) words, then shift to MiB (2^20)
	 */
	size = ((1ull << (rows + cols + ranks)) * banks) >> (20 - 3);
	npages = MiB_TO_PAGES(size);

	edac_dbg(0, "mc#%d: channel %d, dimm %d, %lld MiB (%d pages) bank: %d, rank: %d, row: 0x%x, col: 0x%x\n",
		 imc->mc, chan, dimmno, size, npages,
		 banks, 1 << ranks, rows, cols);

	imc->chan[chan].dimms[dimmno].close_pg = GET_BITFIELD(mtr, 0, 0);
	imc->chan[chan].dimms[dimmno].bank_xor_enable = GET_BITFIELD(mtr, 9, 9);
	imc->chan[chan].dimms[dimmno].fine_grain_bank = GET_BITFIELD(amap, 0, 0);
	imc->chan[chan].dimms[dimmno].rowbits = rows;
	imc->chan[chan].dimms[dimmno].colbits = cols;

	dimm->nr_pages = npages;
	dimm->grain = 32;
	dimm->dtype = get_width(mtr);
	dimm->mtype = MEM_DDR4;
	dimm->edac_mode = EDAC_SECDED; /* likely better than this */
	snprintf(dimm->label, sizeof(dimm->label), "CPU_SrcID#%u_MC#%u_Chan#%u_DIMM#%u",
		 imc->src_id, imc->lmc, chan, dimmno);

	return 1;
}

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

int skx_register_mci(struct skx_imc *imc, struct pci_dev *pdev,
		     const char *ctl_name, const char *mod_str,
		     get_dimm_config_f get_dimm_config)
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
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = mod_str;
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	rc = get_dimm_config(mci);
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
			tp_event = HW_EVENT_ERR_FATAL;
		} else {
			tp_event = HW_EVENT_ERR_UNCORRECTED;
		}
	} else {
		tp_event = HW_EVENT_ERR_CORRECTED;
	}

	/*
	 * According to Intel Architecture spec vol 3B,
	 * Table 15-10 "IA32_MCi_Status [15:0] Compound Error Code Encoding"
	 * memory errors should fit one of these masks:
	 *	000f 0000 1mmm cccc (binary)
	 *	000f 0010 1mmm cccc (binary)	[RAM used as cache]
	 * where:
	 *	f = Correction Report Filtering Bit. If 1, subsequent errors
	 *	    won't be shown
	 *	mmm = error type
	 *	cccc = channel
	 * If the mask doesn't match, report an error to the parsing logic
	 */
	if (!((errcode & 0xef80) == 0x80 || (errcode & 0xef80) == 0x280)) {
		optype = "Can't parse: it is not a mem";
	} else {
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
			break;
		default:
			optype = "reserved";
			break;
		}
	}
	if (adxl_component_count) {
		len = snprintf(skx_msg, MSG_SIZE, "%s%s err_code:0x%04x:0x%04x %s",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode, adxl_msg);
	} else {
		len = snprintf(skx_msg, MSG_SIZE,
			 "%s%s err_code:0x%04x:0x%04x socket:%d imc:%d rank:%d bg:%d ba:%d row:0x%x col:0x%x",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode,
			 res->socket, res->imc, res->rank,
			 res->bank_group, res->bank_address, res->row, res->column);
	}

	if (skx_show_retry_rd_err_log)
		skx_show_retry_rd_err_log(res, skx_msg + len, MSG_SIZE - len);

	edac_dbg(0, "%s\n", skx_msg);

	/* Call the helper to output message */
	edac_mc_handle_error(tp_event, mci, core_err_cnt,
			     m->addr >> PAGE_SHIFT, m->addr & ~PAGE_MASK, 0,
			     res->channel, res->dimm, -1,
			     optype, skx_msg);
}

int skx_mce_check_error(struct notifier_block *nb, unsigned long val,
			void *data)
{
	struct mce *mce = (struct mce *)data;
	struct decoded_addr res;
	struct mem_ctl_info *mci;
	char *type;

	if (edac_get_report_status() == EDAC_REPORTING_DISABLED)
		return NOTIFY_DONE;

	/* ignore unless this is memory related with an address */
	if ((mce->status & 0xefff) >> 7 != 1 || !(mce->status & MCI_STATUS_ADDRV))
		return NOTIFY_DONE;

	memset(&res, 0, sizeof(res));
	res.addr = mce->addr;

	if (adxl_component_count) {
		if (!skx_adxl_decode(&res))
			return NOTIFY_DONE;
	} else if (!skx_decode || !skx_decode(&res)) {
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

	return NOTIFY_DONE;
}

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
		if (d->sad_all)
			pci_dev_put(d->sad_all);
		if (d->uracu)
			pci_dev_put(d->uracu);

		kfree(d);
	}
}
