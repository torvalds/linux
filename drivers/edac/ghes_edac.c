/*
 * GHES/EDAC Linux driver
 *
 * This file may be distributed under the terms of the GNU General Public
 * License version 2.
 *
 * Copyright (c) 2013 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 */

#include <acpi/ghes.h>
#include <linux/edac.h>
#include <linux/dmi.h>
#include "edac_core.h"

#define GHES_PFX   "ghes_edac: "
#define GHES_EDAC_REVISION " Ver: 1.0.0"

struct ghes_edac_pvt {
	struct list_head list;
	struct ghes *ghes;
	struct mem_ctl_info *mci;
};

static LIST_HEAD(ghes_reglist);
static DEFINE_MUTEX(ghes_edac_lock);
static int ghes_edac_mc_num;

/* Memory Device - Type 17 of SMBIOS spec */
struct memdev_dmi_entry {
	u8 type;
	u8 length;
	u16 handle;
	u16 phys_mem_array_handle;
	u16 mem_err_info_handle;
	u16 total_width;
	u16 data_width;
	u16 size;
	u8 form_factor;
	u8 device_set;
	u8 device_locator;
	u8 bank_locator;
	u8 memory_type;
	u16 type_detail;
	u16 speed;
	u8 manufacturer;
	u8 serial_number;
	u8 asset_tag;
	u8 part_number;
	u8 attributes;
	u32 extended_size;
	u16 conf_mem_clk_speed;
} __attribute__((__packed__));

struct ghes_edac_dimm_fill {
	struct mem_ctl_info *mci;
	unsigned count;
};

char *memory_type[] = {
	[MEM_EMPTY] = "EMPTY",
	[MEM_RESERVED] = "RESERVED",
	[MEM_UNKNOWN] = "UNKNOWN",
	[MEM_FPM] = "FPM",
	[MEM_EDO] = "EDO",
	[MEM_BEDO] = "BEDO",
	[MEM_SDR] = "SDR",
	[MEM_RDR] = "RDR",
	[MEM_DDR] = "DDR",
	[MEM_RDDR] = "RDDR",
	[MEM_RMBS] = "RMBS",
	[MEM_DDR2] = "DDR2",
	[MEM_FB_DDR2] = "FB_DDR2",
	[MEM_RDDR2] = "RDDR2",
	[MEM_XDR] = "XDR",
	[MEM_DDR3] = "DDR3",
	[MEM_RDDR3] = "RDDR3",
};

static void ghes_edac_count_dimms(const struct dmi_header *dh, void *arg)
{
	int *num_dimm = arg;

	if (dh->type == DMI_ENTRY_MEM_DEVICE)
		(*num_dimm)++;
}

static void ghes_edac_dmidecode(const struct dmi_header *dh, void *arg)
{
	struct ghes_edac_dimm_fill *dimm_fill = arg;
	struct mem_ctl_info *mci = dimm_fill->mci;

	if (dh->type == DMI_ENTRY_MEM_DEVICE) {
		struct memdev_dmi_entry *entry = (struct memdev_dmi_entry *)dh;
		struct dimm_info *dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
						       mci->n_layers,
						       dimm_fill->count, 0, 0);

		if (entry->size == 0xffff) {
			pr_info(GHES_PFX "Can't get dimm size\n");
			dimm->nr_pages = MiB_TO_PAGES(32);/* Unknown */
		} else if (entry->size == 0x7fff) {
			dimm->nr_pages = MiB_TO_PAGES(entry->extended_size);
		} else {
			if (entry->size & 1 << 15)
				dimm->nr_pages = MiB_TO_PAGES((entry->size &
							       0x7fff) << 10);
			else
				dimm->nr_pages = MiB_TO_PAGES(entry->size);
		}

		switch (entry->memory_type) {
		case 0x12:
			if (entry->type_detail & 1 << 13)
				dimm->mtype = MEM_RDDR;
			else
				dimm->mtype = MEM_DDR;
			break;
		case 0x13:
			if (entry->type_detail & 1 << 13)
				dimm->mtype = MEM_RDDR2;
			else
				dimm->mtype = MEM_DDR2;
			break;
		case 0x14:
			dimm->mtype = MEM_FB_DDR2;
			break;
		case 0x18:
			if (entry->type_detail & 1 << 13)
				dimm->mtype = MEM_RDDR3;
			else
				dimm->mtype = MEM_DDR3;
			break;
		default:
			if (entry->type_detail & 1 << 6)
				dimm->mtype = MEM_RMBS;
			else if ((entry->type_detail & ((1 << 7) | (1 << 13)))
				 == ((1 << 7) | (1 << 13)))
				dimm->mtype = MEM_RDR;
			else if (entry->type_detail & 1 << 7)
				dimm->mtype = MEM_SDR;
			else if (entry->type_detail & 1 << 9)
				dimm->mtype = MEM_EDO;
			else
				dimm->mtype = MEM_UNKNOWN;
		}

		/*
		 * Actually, we can only detect if the memory has bits for
		 * checksum or not
		 */
		if (entry->total_width == entry->data_width)
			dimm->edac_mode = EDAC_NONE;
		else
			dimm->edac_mode = EDAC_SECDED;

		dimm->dtype = DEV_UNKNOWN;
		dimm->grain = 128;		/* Likely, worse case */

		/*
		 * FIXME: It shouldn't be hard to also fill the DIMM labels
		 */

		if (dimm->nr_pages) {
			pr_info(GHES_PFX "DIMM%i: %s size = %d MB%s\n",
				dimm_fill->count, memory_type[dimm->mtype],
				PAGES_TO_MiB(dimm->nr_pages),
				(dimm->edac_mode != EDAC_NONE) ? "(ECC)" : "");
			pr_info(GHES_PFX "\ttype %d, detail 0x%02x, width %d(total %d)\n",
				entry->memory_type, entry->type_detail,
				entry->total_width, entry->data_width);
		}

		dimm_fill->count++;
	}
}

void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
				struct cper_sec_mem_err *mem_err)
{
	enum hw_event_mc_err_type type;
	struct edac_raw_error_desc *e;
	struct mem_ctl_info *mci;
	struct ghes_edac_pvt *pvt = NULL;

	list_for_each_entry(pvt, &ghes_reglist, list) {
		if (ghes == pvt->ghes)
			break;
	}
	if (!pvt) {
		pr_err("Internal error: Can't find EDAC structure\n");
		return;
	}
	mci = pvt->mci;
	e = &mci->error_desc;

	/* Cleans the error report buffer */
	memset(e, 0, sizeof (*e));
	e->error_count = 1;
	e->msg = "APEI";
	strcpy(e->label, "unknown");
	e->other_detail = "";

	if (mem_err->validation_bits & CPER_MEM_VALID_PHYSICAL_ADDRESS) {
		e->page_frame_number = mem_err->physical_addr >> PAGE_SHIFT;
		e->offset_in_page = mem_err->physical_addr & ~PAGE_MASK;
		e->grain = ~(mem_err->physical_addr_mask & ~PAGE_MASK);
	}

	switch (sev) {
	case GHES_SEV_CORRECTED:
		type = HW_EVENT_ERR_CORRECTED;
		break;
	case GHES_SEV_RECOVERABLE:
		type = HW_EVENT_ERR_UNCORRECTED;
		break;
	case GHES_SEV_PANIC:
		type = HW_EVENT_ERR_FATAL;
		break;
	default:
	case GHES_SEV_NO:
		type = HW_EVENT_ERR_INFO;
	}

	sprintf(e->location,
		"node:%d card:%d module:%d bank:%d device:%d row: %d column:%d bit_pos:%d",
		mem_err->node, mem_err->card, mem_err->module,
		mem_err->bank, mem_err->device, mem_err->row, mem_err->column,
		mem_err->bit_pos);
	edac_dbg(3, "error at location %s\n", e->location);

	edac_raw_mc_handle_error(type, mci, e);
}
EXPORT_SYMBOL_GPL(ghes_edac_report_mem_error);

int ghes_edac_register(struct ghes *ghes, struct device *dev)
{
	bool fake = false;
	int rc, num_dimm = 0;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[1];
	struct ghes_edac_pvt *pvt;
	struct ghes_edac_dimm_fill dimm_fill;

	/* Get the number of DIMMs */
	dmi_walk(ghes_edac_count_dimms, &num_dimm);

	/* Check if we've got a bogus BIOS */
	if (num_dimm == 0) {
		fake = true;
		num_dimm = 1;
	}

	layers[0].type = EDAC_MC_LAYER_ALL_MEM;
	layers[0].size = num_dimm;
	layers[0].is_virt_csrow = true;

	/*
	 * We need to serialize edac_mc_alloc() and edac_mc_add_mc(),
	 * to avoid duplicated memory controller numbers
	 */
	mutex_lock(&ghes_edac_lock);
	pr_info("ghes_edac#%d: allocating space for %d dimms\n",
		ghes_edac_mc_num, num_dimm);
	mci = edac_mc_alloc(ghes_edac_mc_num, ARRAY_SIZE(layers), layers,
			    sizeof(*pvt));
	if (!mci) {
		pr_info(GHES_PFX "Can't allocate memory for EDAC data\n");
		mutex_unlock(&ghes_edac_lock);
		return -ENOMEM;
	}

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));
	list_add_tail(&pvt->list, &ghes_reglist);
	pvt->ghes = ghes;
	pvt->mci  = mci;
	mci->pdev = dev;

	mci->mtype_cap = MEM_FLAG_EMPTY;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "ghes_edac.c";
	mci->mod_ver = GHES_EDAC_REVISION;
	mci->ctl_name = "ghes_edac";
	mci->dev_name = "ghes";

	if (!fake) {
		/* Fill DIMM info from DMI */
		dimm_fill.count = 0;
		dimm_fill.mci = mci;
		dmi_walk(ghes_edac_dmidecode, &dimm_fill);
	} else {
		struct dimm_info *dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
						       mci->n_layers, 0, 0, 0);

		pr_info(GHES_PFX "Crappy BIOS detected. Faking DIMM EDAC data\n");
		dimm->nr_pages = 1000;
		dimm->grain = 128;
		dimm->mtype = MEM_UNKNOWN;
		dimm->dtype = DEV_UNKNOWN;
		dimm->edac_mode = EDAC_SECDED;
	}

	rc = edac_mc_add_mc(mci);
	if (rc < 0) {
		pr_info(GHES_PFX "Can't register at EDAC core\n");
		edac_mc_free(mci);
		mutex_unlock(&ghes_edac_lock);
		return -ENODEV;
	}

	ghes_edac_mc_num++;
	mutex_unlock(&ghes_edac_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ghes_edac_register);

void ghes_edac_unregister(struct ghes *ghes)
{
	struct mem_ctl_info *mci;
	struct ghes_edac_pvt *pvt;

	list_for_each_entry(pvt, &ghes_reglist, list) {
		if (ghes == pvt->ghes) {
			mci = pvt->mci;
			edac_mc_del_mc(mci->pdev);
			edac_mc_free(mci);
			list_del(&pvt->list);
		}
	}
}
EXPORT_SYMBOL_GPL(ghes_edac_unregister);
