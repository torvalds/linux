/*
 * EDAC driver for Intel(R) Xeon(R) Skylake processors
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/bitmap.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/adxl.h>
#include <acpi/nfit.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/processor.h>
#include <asm/mce.h>

#include "edac_module.h"

#define EDAC_MOD_STR    "skx_edac"
#define MSG_SIZE	1024

/*
 * Debug macros
 */
#define skx_printk(level, fmt, arg...)			\
	edac_printk(level, "skx", fmt, ##arg)

#define skx_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "skx", fmt, ##arg)

/*
 * Get a bit field at register value <v>, from bit <lo> to bit <hi>
 */
#define GET_BITFIELD(v, lo, hi) \
	(((v) & GENMASK_ULL((hi), (lo))) >> (lo))

static LIST_HEAD(skx_edac_list);

static u64 skx_tolm, skx_tohm;
static char *skx_msg;
static unsigned int nvdimm_count;

enum {
	INDEX_SOCKET,
	INDEX_MEMCTRL,
	INDEX_CHANNEL,
	INDEX_DIMM,
	INDEX_MAX
};

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

#define NUM_IMC			2	/* memory controllers per socket */
#define NUM_CHANNELS		3	/* channels per memory controller */
#define NUM_DIMMS		2	/* Max DIMMS per channel */

#define	MASK26	0x3FFFFFF		/* Mask for 2^26 */
#define MASK29	0x1FFFFFFF		/* Mask for 2^29 */

/*
 * Each cpu socket contains some pci devices that provide global
 * information, and also some that are local to each of the two
 * memory controllers on the die.
 */
struct skx_dev {
	struct list_head	list;
	u8			bus[4];
	int			seg;
	struct pci_dev	*sad_all;
	struct pci_dev	*util_all;
	u32	mcroute;
	struct skx_imc {
		struct mem_ctl_info *mci;
		u8	mc;	/* system wide mc# */
		u8	lmc;	/* socket relative mc# */
		u8	src_id, node_id;
		struct skx_channel {
			struct pci_dev *cdev;
			struct skx_dimm {
				u8	close_pg;
				u8	bank_xor_enable;
				u8	fine_grain_bank;
				u8	rowbits;
				u8	colbits;
			} dimms[NUM_DIMMS];
		} chan[NUM_CHANNELS];
	} imc[NUM_IMC];
};
static int skx_num_sockets;

struct skx_pvt {
	struct skx_imc	*imc;
};

struct decoded_addr {
	struct skx_dev *dev;
	u64	addr;
	int	socket;
	int	imc;
	int	channel;
	u64	chan_addr;
	int	sktways;
	int	chanways;
	int	dimm;
	int	rank;
	int	channel_rank;
	u64	rank_address;
	int	row;
	int	column;
	int	bank_address;
	int	bank_group;
};

static struct skx_dev *get_skx_dev(struct pci_bus *bus, u8 idx)
{
	struct skx_dev *d;

	list_for_each_entry(d, &skx_edac_list, list) {
		if (d->seg == pci_domain_nr(bus) && d->bus[idx] == bus->number)
			return d;
	}

	return NULL;
}

enum munittype {
	CHAN0, CHAN1, CHAN2, SAD_ALL, UTIL_ALL, SAD
};

struct munit {
	u16	did;
	u16	devfn[NUM_IMC];
	u8	busidx;
	u8	per_socket;
	enum munittype mtype;
};

/*
 * List of PCI device ids that we need together with some device
 * number and function numbers to tell which memory controller the
 * device belongs to.
 */
static const struct munit skx_all_munits[] = {
	{ 0x2054, { }, 1, 1, SAD_ALL },
	{ 0x2055, { }, 1, 1, UTIL_ALL },
	{ 0x2040, { PCI_DEVFN(10, 0), PCI_DEVFN(12, 0) }, 2, 2, CHAN0 },
	{ 0x2044, { PCI_DEVFN(10, 4), PCI_DEVFN(12, 4) }, 2, 2, CHAN1 },
	{ 0x2048, { PCI_DEVFN(11, 0), PCI_DEVFN(13, 0) }, 2, 2, CHAN2 },
	{ 0x208e, { }, 1, 0, SAD },
	{ }
};

/*
 * We use the per-socket device 0x2016 to count how many sockets are present,
 * and to detemine which PCI buses are associated with each socket. Allocate
 * and build the full list of all the skx_dev structures that we need here.
 */
static int get_all_bus_mappings(void)
{
	struct pci_dev *pdev, *prev;
	struct skx_dev *d;
	u32 reg;
	int ndev = 0;

	prev = NULL;
	for (;;) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x2016, prev);
		if (!pdev)
			break;
		ndev++;
		d = kzalloc(sizeof(*d), GFP_KERNEL);
		if (!d) {
			pci_dev_put(pdev);
			return -ENOMEM;
		}
		d->seg = pci_domain_nr(pdev->bus);
		pci_read_config_dword(pdev, 0xCC, &reg);
		d->bus[0] =  GET_BITFIELD(reg, 0, 7);
		d->bus[1] =  GET_BITFIELD(reg, 8, 15);
		d->bus[2] =  GET_BITFIELD(reg, 16, 23);
		d->bus[3] =  GET_BITFIELD(reg, 24, 31);
		edac_dbg(2, "busses: 0x%x, 0x%x, 0x%x, 0x%x\n",
			 d->bus[0], d->bus[1], d->bus[2], d->bus[3]);
		list_add_tail(&d->list, &skx_edac_list);
		skx_num_sockets++;
		prev = pdev;
	}

	return ndev;
}

static int get_all_munits(const struct munit *m)
{
	struct pci_dev *pdev, *prev;
	struct skx_dev *d;
	u32 reg;
	int i = 0, ndev = 0;

	prev = NULL;
	for (;;) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, m->did, prev);
		if (!pdev)
			break;
		ndev++;
		if (m->per_socket == NUM_IMC) {
			for (i = 0; i < NUM_IMC; i++)
				if (m->devfn[i] == pdev->devfn)
					break;
			if (i == NUM_IMC)
				goto fail;
		}
		d = get_skx_dev(pdev->bus, m->busidx);
		if (!d)
			goto fail;

		/* Be sure that the device is enabled */
		if (unlikely(pci_enable_device(pdev) < 0)) {
			skx_printk(KERN_ERR, "Couldn't enable device %04x:%04x\n",
				   PCI_VENDOR_ID_INTEL, m->did);
			goto fail;
		}

		switch (m->mtype) {
		case CHAN0: case CHAN1: case CHAN2:
			pci_dev_get(pdev);
			d->imc[i].chan[m->mtype].cdev = pdev;
			break;
		case SAD_ALL:
			pci_dev_get(pdev);
			d->sad_all = pdev;
			break;
		case UTIL_ALL:
			pci_dev_get(pdev);
			d->util_all = pdev;
			break;
		case SAD:
			/*
			 * one of these devices per core, including cores
			 * that don't exist on this SKU. Ignore any that
			 * read a route table of zero, make sure all the
			 * non-zero values match.
			 */
			pci_read_config_dword(pdev, 0xB4, &reg);
			if (reg != 0) {
				if (d->mcroute == 0)
					d->mcroute = reg;
				else if (d->mcroute != reg) {
					skx_printk(KERN_ERR,
						"mcroute mismatch\n");
					goto fail;
				}
			}
			ndev--;
			break;
		}

		prev = pdev;
	}

	return ndev;
fail:
	pci_dev_put(pdev);
	return -ENODEV;
}

static const struct x86_cpu_id skx_cpuids[] = {
	{ X86_VENDOR_INTEL, 6, INTEL_FAM6_SKYLAKE_X, 0, 0 },
	{ }
};
MODULE_DEVICE_TABLE(x86cpu, skx_cpuids);

static u8 get_src_id(struct skx_dev *d)
{
	u32 reg;

	pci_read_config_dword(d->util_all, 0xF0, &reg);

	return GET_BITFIELD(reg, 12, 14);
}

static u8 skx_get_node_id(struct skx_dev *d)
{
	u32 reg;

	pci_read_config_dword(d->util_all, 0xF4, &reg);

	return GET_BITFIELD(reg, 0, 2);
}

static int get_dimm_attr(u32 reg, int lobit, int hibit, int add, int minval,
			 int maxval, char *name)
{
	u32 val = GET_BITFIELD(reg, lobit, hibit);

	if (val < minval || val > maxval) {
		edac_dbg(2, "bad %s = %d (raw=0x%x)\n", name, val, reg);
		return -EINVAL;
	}
	return val + add;
}

#define IS_DIMM_PRESENT(mtr)		GET_BITFIELD((mtr), 15, 15)
#define IS_NVDIMM_PRESENT(mcddrtcfg, i)	GET_BITFIELD((mcddrtcfg), (i), (i))

#define numrank(reg) get_dimm_attr((reg), 12, 13, 0, 0, 2, "ranks")
#define numrow(reg) get_dimm_attr((reg), 2, 4, 12, 1, 6, "rows")
#define numcol(reg) get_dimm_attr((reg), 0, 1, 10, 0, 2, "cols")

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

static int skx_get_hi_lo(void)
{
	struct pci_dev *pdev;
	u32 reg;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x2034, NULL);
	if (!pdev) {
		edac_dbg(0, "Can't get tolm/tohm\n");
		return -ENODEV;
	}

	pci_read_config_dword(pdev, 0xD0, &reg);
	skx_tolm = reg;
	pci_read_config_dword(pdev, 0xD4, &reg);
	skx_tohm = reg;
	pci_read_config_dword(pdev, 0xD8, &reg);
	skx_tohm |= (u64)reg << 32;

	pci_dev_put(pdev);
	edac_dbg(2, "tolm=0x%llx tohm=0x%llx\n", skx_tolm, skx_tohm);

	return 0;
}

static int get_dimm_info(u32 mtr, u32 amap, struct dimm_info *dimm,
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

	edac_dbg(0, "mc#%d: channel %d, dimm %d, %lld MiB (%d pages) bank: %d, rank: %d, row: 0x%#x, col: 0x%#x\n",
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

static int get_nvdimm_info(struct dimm_info *dimm, struct skx_imc *imc,
			   int chan, int dimmno)
{
	int smbios_handle;
	u32 dev_handle;
	u16 flags;
	u64 size = 0;

	nvdimm_count++;

	dev_handle = ACPI_NFIT_BUILD_DEVICE_HANDLE(dimmno, chan, imc->lmc,
						   imc->src_id, 0);

	smbios_handle = nfit_get_smbios_id(dev_handle, &flags);
	if (smbios_handle == -EOPNOTSUPP) {
		pr_warn_once(EDAC_MOD_STR ": Can't find size of NVDIMM. Try enabling CONFIG_ACPI_NFIT\n");
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

#define SKX_GET_MTMTR(dev, reg) \
	pci_read_config_dword((dev), 0x87c, &reg)

static bool skx_check_ecc(struct pci_dev *pdev)
{
	u32 mtmtr;

	SKX_GET_MTMTR(pdev, mtmtr);

	return !!GET_BITFIELD(mtmtr, 2, 2);
}

static int skx_get_dimm_config(struct mem_ctl_info *mci)
{
	struct skx_pvt *pvt = mci->pvt_info;
	struct skx_imc *imc = pvt->imc;
	u32 mtr, amap, mcddrtcfg;
	struct dimm_info *dimm;
	int i, j;
	int ndimms;

	for (i = 0; i < NUM_CHANNELS; i++) {
		ndimms = 0;
		pci_read_config_dword(imc->chan[i].cdev, 0x8C, &amap);
		pci_read_config_dword(imc->chan[i].cdev, 0x400, &mcddrtcfg);
		for (j = 0; j < NUM_DIMMS; j++) {
			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
					     mci->n_layers, i, j, 0);
			pci_read_config_dword(imc->chan[i].cdev,
					0x80 + 4*j, &mtr);
			if (IS_DIMM_PRESENT(mtr))
				ndimms += get_dimm_info(mtr, amap, dimm, imc, i, j);
			else if (IS_NVDIMM_PRESENT(mcddrtcfg, j))
				ndimms += get_nvdimm_info(dimm, imc, i, j);
		}
		if (ndimms && !skx_check_ecc(imc->chan[0].cdev)) {
			skx_printk(KERN_ERR, "ECC is disabled on imc %d\n", imc->mc);
			return -ENODEV;
		}
	}

	return 0;
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

static int skx_register_mci(struct skx_imc *imc)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct pci_dev *pdev = imc->chan[0].cdev;
	struct skx_pvt *pvt;
	int rc;

	/* allocate a new MC control structure */
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

	mci->ctl_name = kasprintf(GFP_KERNEL, "Skylake Socket#%d IMC#%d",
				  imc->node_id, imc->lmc);
	if (!mci->ctl_name) {
		rc = -ENOMEM;
		goto fail0;
	}

	mci->mtype_cap = MEM_FLAG_DDR4 | MEM_FLAG_NVDIMM;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = EDAC_MOD_STR;
	mci->dev_name = pci_name(imc->chan[0].cdev);
	mci->ctl_page_to_phys = NULL;

	rc = skx_get_dimm_config(mci);
	if (rc < 0)
		goto fail;

	/* record ptr to the generic device */
	mci->pdev = &pdev->dev;

	/* add this new MC control structure to EDAC's list of MCs */
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

#define	SKX_MAX_SAD 24

#define SKX_GET_SAD(d, i, reg)	\
	pci_read_config_dword((d)->sad_all, 0x60 + 8 * (i), &reg)
#define SKX_GET_ILV(d, i, reg)	\
	pci_read_config_dword((d)->sad_all, 0x64 + 8 * (i), &reg)

#define	SKX_SAD_MOD3MODE(sad)	GET_BITFIELD((sad), 30, 31)
#define	SKX_SAD_MOD3(sad)	GET_BITFIELD((sad), 27, 27)
#define SKX_SAD_LIMIT(sad)	(((u64)GET_BITFIELD((sad), 7, 26) << 26) | MASK26)
#define	SKX_SAD_MOD3ASMOD2(sad)	GET_BITFIELD((sad), 5, 6)
#define	SKX_SAD_ATTR(sad)	GET_BITFIELD((sad), 3, 4)
#define	SKX_SAD_INTERLEAVE(sad)	GET_BITFIELD((sad), 1, 2)
#define SKX_SAD_ENABLE(sad)	GET_BITFIELD((sad), 0, 0)

#define SKX_ILV_REMOTE(tgt)	(((tgt) & 8) == 0)
#define SKX_ILV_TARGET(tgt)	((tgt) & 7)

static bool skx_sad_decode(struct decoded_addr *res)
{
	struct skx_dev *d = list_first_entry(&skx_edac_list, typeof(*d), list);
	u64 addr = res->addr;
	int i, idx, tgt, lchan, shift;
	u32 sad, ilv;
	u64 limit, prev_limit;
	int remote = 0;

	/* Simple sanity check for I/O space or out of range */
	if (addr >= skx_tohm || (addr >= skx_tolm && addr < BIT_ULL(32))) {
		edac_dbg(0, "Address 0x%llx out of range\n", addr);
		return false;
	}

restart:
	prev_limit = 0;
	for (i = 0; i < SKX_MAX_SAD; i++) {
		SKX_GET_SAD(d, i, sad);
		limit = SKX_SAD_LIMIT(sad);
		if (SKX_SAD_ENABLE(sad)) {
			if (addr >= prev_limit && addr <= limit)
				goto sad_found;
		}
		prev_limit = limit + 1;
	}
	edac_dbg(0, "No SAD entry for 0x%llx\n", addr);
	return false;

sad_found:
	SKX_GET_ILV(d, i, ilv);

	switch (SKX_SAD_INTERLEAVE(sad)) {
	case 0:
		idx = GET_BITFIELD(addr, 6, 8);
		break;
	case 1:
		idx = GET_BITFIELD(addr, 8, 10);
		break;
	case 2:
		idx = GET_BITFIELD(addr, 12, 14);
		break;
	case 3:
		idx = GET_BITFIELD(addr, 30, 32);
		break;
	}

	tgt = GET_BITFIELD(ilv, 4 * idx, 4 * idx + 3);

	/* If point to another node, find it and start over */
	if (SKX_ILV_REMOTE(tgt)) {
		if (remote) {
			edac_dbg(0, "Double remote!\n");
			return false;
		}
		remote = 1;
		list_for_each_entry(d, &skx_edac_list, list) {
			if (d->imc[0].src_id == SKX_ILV_TARGET(tgt))
				goto restart;
		}
		edac_dbg(0, "Can't find node %d\n", SKX_ILV_TARGET(tgt));
		return false;
	}

	if (SKX_SAD_MOD3(sad) == 0)
		lchan = SKX_ILV_TARGET(tgt);
	else {
		switch (SKX_SAD_MOD3MODE(sad)) {
		case 0:
			shift = 6;
			break;
		case 1:
			shift = 8;
			break;
		case 2:
			shift = 12;
			break;
		default:
			edac_dbg(0, "illegal mod3mode\n");
			return false;
		}
		switch (SKX_SAD_MOD3ASMOD2(sad)) {
		case 0:
			lchan = (addr >> shift) % 3;
			break;
		case 1:
			lchan = (addr >> shift) % 2;
			break;
		case 2:
			lchan = (addr >> shift) % 2;
			lchan = (lchan << 1) | !lchan;
			break;
		case 3:
			lchan = ((addr >> shift) % 2) << 1;
			break;
		}
		lchan = (lchan << 1) | (SKX_ILV_TARGET(tgt) & 1);
	}

	res->dev = d;
	res->socket = d->imc[0].src_id;
	res->imc = GET_BITFIELD(d->mcroute, lchan * 3, lchan * 3 + 2);
	res->channel = GET_BITFIELD(d->mcroute, lchan * 2 + 18, lchan * 2 + 19);

	edac_dbg(2, "0x%llx: socket=%d imc=%d channel=%d\n",
		 res->addr, res->socket, res->imc, res->channel);
	return true;
}

#define	SKX_MAX_TAD 8

#define SKX_GET_TADBASE(d, mc, i, reg)			\
	pci_read_config_dword((d)->imc[mc].chan[0].cdev, 0x850 + 4 * (i), &reg)
#define SKX_GET_TADWAYNESS(d, mc, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[0].cdev, 0x880 + 4 * (i), &reg)
#define SKX_GET_TADCHNILVOFFSET(d, mc, ch, i, reg)	\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev, 0x90 + 4 * (i), &reg)

#define	SKX_TAD_BASE(b)		((u64)GET_BITFIELD((b), 12, 31) << 26)
#define SKX_TAD_SKT_GRAN(b)	GET_BITFIELD((b), 4, 5)
#define SKX_TAD_CHN_GRAN(b)	GET_BITFIELD((b), 6, 7)
#define	SKX_TAD_LIMIT(b)	(((u64)GET_BITFIELD((b), 12, 31) << 26) | MASK26)
#define	SKX_TAD_OFFSET(b)	((u64)GET_BITFIELD((b), 4, 23) << 26)
#define	SKX_TAD_SKTWAYS(b)	(1 << GET_BITFIELD((b), 10, 11))
#define	SKX_TAD_CHNWAYS(b)	(GET_BITFIELD((b), 8, 9) + 1)

/* which bit used for both socket and channel interleave */
static int skx_granularity[] = { 6, 8, 12, 30 };

static u64 skx_do_interleave(u64 addr, int shift, int ways, u64 lowbits)
{
	addr >>= shift;
	addr /= ways;
	addr <<= shift;

	return addr | (lowbits & ((1ull << shift) - 1));
}

static bool skx_tad_decode(struct decoded_addr *res)
{
	int i;
	u32 base, wayness, chnilvoffset;
	int skt_interleave_bit, chn_interleave_bit;
	u64 channel_addr;

	for (i = 0; i < SKX_MAX_TAD; i++) {
		SKX_GET_TADBASE(res->dev, res->imc, i, base);
		SKX_GET_TADWAYNESS(res->dev, res->imc, i, wayness);
		if (SKX_TAD_BASE(base) <= res->addr && res->addr <= SKX_TAD_LIMIT(wayness))
			goto tad_found;
	}
	edac_dbg(0, "No TAD entry for 0x%llx\n", res->addr);
	return false;

tad_found:
	res->sktways = SKX_TAD_SKTWAYS(wayness);
	res->chanways = SKX_TAD_CHNWAYS(wayness);
	skt_interleave_bit = skx_granularity[SKX_TAD_SKT_GRAN(base)];
	chn_interleave_bit = skx_granularity[SKX_TAD_CHN_GRAN(base)];

	SKX_GET_TADCHNILVOFFSET(res->dev, res->imc, res->channel, i, chnilvoffset);
	channel_addr = res->addr - SKX_TAD_OFFSET(chnilvoffset);

	if (res->chanways == 3 && skt_interleave_bit > chn_interleave_bit) {
		/* Must handle channel first, then socket */
		channel_addr = skx_do_interleave(channel_addr, chn_interleave_bit,
						 res->chanways, channel_addr);
		channel_addr = skx_do_interleave(channel_addr, skt_interleave_bit,
						 res->sktways, channel_addr);
	} else {
		/* Handle socket then channel. Preserve low bits from original address */
		channel_addr = skx_do_interleave(channel_addr, skt_interleave_bit,
						 res->sktways, res->addr);
		channel_addr = skx_do_interleave(channel_addr, chn_interleave_bit,
						 res->chanways, res->addr);
	}

	res->chan_addr = channel_addr;

	edac_dbg(2, "0x%llx: chan_addr=0x%llx sktways=%d chanways=%d\n",
		 res->addr, res->chan_addr, res->sktways, res->chanways);
	return true;
}

#define SKX_MAX_RIR 4

#define SKX_GET_RIRWAYNESS(d, mc, ch, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev,	\
			      0x108 + 4 * (i), &reg)
#define SKX_GET_RIRILV(d, mc, ch, idx, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev,	\
			      0x120 + 16 * idx + 4 * (i), &reg)

#define	SKX_RIR_VALID(b) GET_BITFIELD((b), 31, 31)
#define	SKX_RIR_LIMIT(b) (((u64)GET_BITFIELD((b), 1, 11) << 29) | MASK29)
#define	SKX_RIR_WAYS(b) (1 << GET_BITFIELD((b), 28, 29))
#define	SKX_RIR_CHAN_RANK(b) GET_BITFIELD((b), 16, 19)
#define	SKX_RIR_OFFSET(b) ((u64)(GET_BITFIELD((b), 2, 15) << 26))

static bool skx_rir_decode(struct decoded_addr *res)
{
	int i, idx, chan_rank;
	int shift;
	u32 rirway, rirlv;
	u64 rank_addr, prev_limit = 0, limit;

	if (res->dev->imc[res->imc].chan[res->channel].dimms[0].close_pg)
		shift = 6;
	else
		shift = 13;

	for (i = 0; i < SKX_MAX_RIR; i++) {
		SKX_GET_RIRWAYNESS(res->dev, res->imc, res->channel, i, rirway);
		limit = SKX_RIR_LIMIT(rirway);
		if (SKX_RIR_VALID(rirway)) {
			if (prev_limit <= res->chan_addr &&
			    res->chan_addr <= limit)
				goto rir_found;
		}
		prev_limit = limit;
	}
	edac_dbg(0, "No RIR entry for 0x%llx\n", res->addr);
	return false;

rir_found:
	rank_addr = res->chan_addr >> shift;
	rank_addr /= SKX_RIR_WAYS(rirway);
	rank_addr <<= shift;
	rank_addr |= res->chan_addr & GENMASK_ULL(shift - 1, 0);

	res->rank_address = rank_addr;
	idx = (res->chan_addr >> shift) % SKX_RIR_WAYS(rirway);

	SKX_GET_RIRILV(res->dev, res->imc, res->channel, idx, i, rirlv);
	res->rank_address = rank_addr - SKX_RIR_OFFSET(rirlv);
	chan_rank = SKX_RIR_CHAN_RANK(rirlv);
	res->channel_rank = chan_rank;
	res->dimm = chan_rank / 4;
	res->rank = chan_rank % 4;

	edac_dbg(2, "0x%llx: dimm=%d rank=%d chan_rank=%d rank_addr=0x%llx\n",
		 res->addr, res->dimm, res->rank,
		 res->channel_rank, res->rank_address);
	return true;
}

static u8 skx_close_row[] = {
	15, 16, 17, 18, 20, 21, 22, 28, 10, 11, 12, 13, 29, 30, 31, 32, 33
};
static u8 skx_close_column[] = {
	3, 4, 5, 14, 19, 23, 24, 25, 26, 27
};
static u8 skx_open_row[] = {
	14, 15, 16, 20, 28, 21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 32, 33
};
static u8 skx_open_column[] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};
static u8 skx_open_fine_column[] = {
	3, 4, 5, 7, 8, 9, 10, 11, 12, 13
};

static int skx_bits(u64 addr, int nbits, u8 *bits)
{
	int i, res = 0;

	for (i = 0; i < nbits; i++)
		res |= ((addr >> bits[i]) & 1) << i;
	return res;
}

static int skx_bank_bits(u64 addr, int b0, int b1, int do_xor, int x0, int x1)
{
	int ret = GET_BITFIELD(addr, b0, b0) | (GET_BITFIELD(addr, b1, b1) << 1);

	if (do_xor)
		ret ^= GET_BITFIELD(addr, x0, x0) | (GET_BITFIELD(addr, x1, x1) << 1);

	return ret;
}

static bool skx_mad_decode(struct decoded_addr *r)
{
	struct skx_dimm *dimm = &r->dev->imc[r->imc].chan[r->channel].dimms[r->dimm];
	int bg0 = dimm->fine_grain_bank ? 6 : 13;

	if (dimm->close_pg) {
		r->row = skx_bits(r->rank_address, dimm->rowbits, skx_close_row);
		r->column = skx_bits(r->rank_address, dimm->colbits, skx_close_column);
		r->column |= 0x400; /* C10 is autoprecharge, always set */
		r->bank_address = skx_bank_bits(r->rank_address, 8, 9, dimm->bank_xor_enable, 22, 28);
		r->bank_group = skx_bank_bits(r->rank_address, 6, 7, dimm->bank_xor_enable, 20, 21);
	} else {
		r->row = skx_bits(r->rank_address, dimm->rowbits, skx_open_row);
		if (dimm->fine_grain_bank)
			r->column = skx_bits(r->rank_address, dimm->colbits, skx_open_fine_column);
		else
			r->column = skx_bits(r->rank_address, dimm->colbits, skx_open_column);
		r->bank_address = skx_bank_bits(r->rank_address, 18, 19, dimm->bank_xor_enable, 22, 23);
		r->bank_group = skx_bank_bits(r->rank_address, bg0, 17, dimm->bank_xor_enable, 20, 21);
	}
	r->row &= (1u << dimm->rowbits) - 1;

	edac_dbg(2, "0x%llx: row=0x%x col=0x%x bank_addr=%d bank_group=%d\n",
		 r->addr, r->row, r->column, r->bank_address,
		 r->bank_group);
	return true;
}

static bool skx_decode(struct decoded_addr *res)
{

	return skx_sad_decode(res) && skx_tad_decode(res) &&
		skx_rir_decode(res) && skx_mad_decode(res);
}

static bool skx_adxl_decode(struct decoded_addr *res)

{
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

static void skx_mce_output_error(struct mem_ctl_info *mci,
				 const struct mce *m,
				 struct decoded_addr *res)
{
	enum hw_event_mc_err_type tp_event;
	char *type, *optype;
	bool ripv = GET_BITFIELD(m->mcgstatus, 0, 0);
	bool overflow = GET_BITFIELD(m->status, 62, 62);
	bool uncorrected_error = GET_BITFIELD(m->status, 61, 61);
	bool recoverable;
	u32 core_err_cnt = GET_BITFIELD(m->status, 38, 52);
	u32 mscod = GET_BITFIELD(m->status, 16, 31);
	u32 errcode = GET_BITFIELD(m->status, 0, 15);
	u32 optypenum = GET_BITFIELD(m->status, 4, 6);

	recoverable = GET_BITFIELD(m->status, 56, 56);

	if (uncorrected_error) {
		core_err_cnt = 1;
		if (ripv) {
			type = "FATAL";
			tp_event = HW_EVENT_ERR_FATAL;
		} else {
			type = "NON_FATAL";
			tp_event = HW_EVENT_ERR_UNCORRECTED;
		}
	} else {
		type = "CORRECTED";
		tp_event = HW_EVENT_ERR_CORRECTED;
	}

	/*
	 * According with Table 15-9 of the Intel Architecture spec vol 3A,
	 * memory errors should fit in this mask:
	 *	000f 0000 1mmm cccc (binary)
	 * where:
	 *	f = Correction Report Filtering Bit. If 1, subsequent errors
	 *	    won't be shown
	 *	mmm = error type
	 *	cccc = channel
	 * If the mask doesn't match, report an error to the parsing logic
	 */
	if (!((errcode & 0xef80) == 0x80)) {
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
		snprintf(skx_msg, MSG_SIZE, "%s%s err_code:0x%04x:0x%04x %s",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode, adxl_msg);
	} else {
		snprintf(skx_msg, MSG_SIZE,
			 "%s%s err_code:0x%04x:0x%04x socket:%d imc:%d rank:%d bg:%d ba:%d row:0x%x col:0x%x",
			 overflow ? " OVERFLOW" : "",
			 (uncorrected_error && recoverable) ? " recoverable" : "",
			 mscod, errcode,
			 res->socket, res->imc, res->rank,
			 res->bank_group, res->bank_address, res->row, res->column);
	}

	edac_dbg(0, "%s\n", skx_msg);

	/* Call the helper to output message */
	edac_mc_handle_error(tp_event, mci, core_err_cnt,
			     m->addr >> PAGE_SHIFT, m->addr & ~PAGE_MASK, 0,
			     res->channel, res->dimm, -1,
			     optype, skx_msg);
}

static struct mem_ctl_info *get_mci(int src_id, int lmc)
{
	struct skx_dev *d;

	if (lmc > NUM_IMC - 1) {
		skx_printk(KERN_ERR, "Bad lmc %d\n", lmc);
		return NULL;
	}

	list_for_each_entry(d, &skx_edac_list, list) {
		if (d->imc[0].src_id == src_id)
			return d->imc[lmc].mci;
	}

	skx_printk(KERN_ERR, "No mci for src_id %d lmc %d\n", src_id, lmc);

	return NULL;
}

static int skx_mce_check_error(struct notifier_block *nb, unsigned long val,
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

		mci = get_mci(res.socket, res.imc);
	} else {
		if (!skx_decode(&res))
			return NOTIFY_DONE;

		mci = res.dev->imc[res.imc].mci;
	}

	if (!mci)
		return NOTIFY_DONE;

	if (mce->mcgstatus & MCG_STATUS_MCIP)
		type = "Exception";
	else
		type = "Event";

	skx_mc_printk(mci, KERN_DEBUG, "HANDLING MCE MEMORY ERROR\n");

	skx_mc_printk(mci, KERN_DEBUG, "CPU %d: Machine Check %s: 0x%llx "
			  "Bank %d: %016Lx\n", mce->extcpu, type,
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

static struct notifier_block skx_mce_dec = {
	.notifier_call	= skx_mce_check_error,
	.priority	= MCE_PRIO_EDAC,
};

#ifdef CONFIG_EDAC_DEBUG
/*
 * Debug feature.
 * Exercise the address decode logic by writing an address to
 * /sys/kernel/debug/edac/skx_test/addr.
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

static void setup_skx_debug(void)
{
	skx_test = edac_debugfs_create_dir("skx_test");
	if (!skx_test)
		return;

	if (!edac_debugfs_create_file("addr", 0200, skx_test,
				      NULL, &fops_u64_wo)) {
		debugfs_remove(skx_test);
		skx_test = NULL;
	}
}

static void teardown_skx_debug(void)
{
	debugfs_remove_recursive(skx_test);
}
#else
static void setup_skx_debug(void) {}
static void teardown_skx_debug(void) {}
#endif /*CONFIG_EDAC_DEBUG*/

static void skx_remove(void)
{
	int i, j;
	struct skx_dev *d, *tmp;

	edac_dbg(0, "\n");

	list_for_each_entry_safe(d, tmp, &skx_edac_list, list) {
		list_del(&d->list);
		for (i = 0; i < NUM_IMC; i++) {
			skx_unregister_mci(&d->imc[i]);
			for (j = 0; j < NUM_CHANNELS; j++)
				pci_dev_put(d->imc[i].chan[j].cdev);
		}
		pci_dev_put(d->util_all);
		pci_dev_put(d->sad_all);

		kfree(d);
	}
}

static void __init skx_adxl_get(void)
{
	const char * const *names;
	int i, j;

	names = adxl_get_component_names();
	if (!names) {
		skx_printk(KERN_NOTICE, "No firmware support for address translation.");
		skx_printk(KERN_CONT, " Only decoding DDR4 address!\n");
		return;
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
		return;
	}

	adxl_msg = kzalloc(MSG_SIZE, GFP_KERNEL);
	if (!adxl_msg) {
		adxl_component_count = 0;
		kfree(adxl_values);
	}

	return;
err:
	skx_printk(KERN_ERR, "'%s' is not matched from DSM parameters: ",
		   component_names[i]);
	for (j = 0; names[j]; j++)
		skx_printk(KERN_CONT, "%s ", names[j]);
	skx_printk(KERN_CONT, "\n");
}

static void __exit skx_adxl_put(void)
{
	kfree(adxl_values);
	kfree(adxl_msg);
}

/*
 * skx_init:
 *	make sure we are running on the correct cpu model
 *	search for all the devices we need
 *	check which DIMMs are present.
 */
static int __init skx_init(void)
{
	const struct x86_cpu_id *id;
	const struct munit *m;
	const char *owner;
	int rc = 0, i;
	u8 mc = 0, src_id, node_id;
	struct skx_dev *d;

	edac_dbg(2, "\n");

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	id = x86_match_cpu(skx_cpuids);
	if (!id)
		return -ENODEV;

	rc = skx_get_hi_lo();
	if (rc)
		return rc;

	rc = get_all_bus_mappings();
	if (rc < 0)
		goto fail;
	if (rc == 0) {
		edac_dbg(2, "No memory controllers found\n");
		return -ENODEV;
	}

	for (m = skx_all_munits; m->did; m++) {
		rc = get_all_munits(m);
		if (rc < 0)
			goto fail;
		if (rc != m->per_socket * skx_num_sockets) {
			edac_dbg(2, "Expected %d, got %d of 0x%x\n",
				 m->per_socket * skx_num_sockets, rc, m->did);
			rc = -ENODEV;
			goto fail;
		}
	}

	list_for_each_entry(d, &skx_edac_list, list) {
		src_id = get_src_id(d);
		node_id = skx_get_node_id(d);
		edac_dbg(2, "src_id=%d node_id=%d\n", src_id, node_id);
		for (i = 0; i < NUM_IMC; i++) {
			d->imc[i].mc = mc++;
			d->imc[i].lmc = i;
			d->imc[i].src_id = src_id;
			d->imc[i].node_id = node_id;
			rc = skx_register_mci(&d->imc[i]);
			if (rc < 0)
				goto fail;
		}
	}

	skx_msg = kzalloc(MSG_SIZE, GFP_KERNEL);
	if (!skx_msg) {
		rc = -ENOMEM;
		goto fail;
	}

	if (nvdimm_count)
		skx_adxl_get();

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	setup_skx_debug();

	mce_register_decode_chain(&skx_mce_dec);

	return 0;
fail:
	skx_remove();
	return rc;
}

static void __exit skx_exit(void)
{
	edac_dbg(2, "\n");
	mce_unregister_decode_chain(&skx_mce_dec);
	teardown_skx_debug();
	if (nvdimm_count)
		skx_adxl_put();
	kfree(skx_msg);
	skx_remove();
}

module_init(skx_init);
module_exit(skx_exit);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Luck");
MODULE_DESCRIPTION("MC Driver for Intel Skylake server processors");
