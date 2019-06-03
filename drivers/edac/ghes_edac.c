// SPDX-License-Identifier: GPL-2.0-only
/*
 * GHES/EDAC Linux driver
 *
 * Copyright (c) 2013 by Mauro Carvalho Chehab
 *
 * Red Hat Inc. http://www.redhat.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <acpi/ghes.h>
#include <linux/edac.h>
#include <linux/dmi.h>
#include "edac_module.h"
#include <ras/ras_event.h>

struct ghes_edac_pvt {
	struct list_head list;
	struct ghes *ghes;
	struct mem_ctl_info *mci;

	/* Buffers for the error handling routine */
	char detail_location[240];
	char other_detail[160];
	char msg[80];
};

static atomic_t ghes_init = ATOMIC_INIT(0);
static struct ghes_edac_pvt *ghes_pvt;

/*
 * Sync with other, potentially concurrent callers of
 * ghes_edac_report_mem_error(). We don't know what the
 * "inventive" firmware would do.
 */
static DEFINE_SPINLOCK(ghes_lock);

/* "ghes_edac.force_load=1" skips the platform check */
static bool __read_mostly force_load;
module_param(force_load, bool, 0);

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

static void ghes_edac_count_dimms(const struct dmi_header *dh, void *arg)
{
	int *num_dimm = arg;

	if (dh->type == DMI_ENTRY_MEM_DEVICE)
		(*num_dimm)++;
}

static int get_dimm_smbios_index(u16 handle)
{
	struct mem_ctl_info *mci = ghes_pvt->mci;
	int i;

	for (i = 0; i < mci->tot_dimms; i++) {
		if (mci->dimms[i]->smbios_handle == handle)
			return i;
	}
	return -1;
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
		u16 rdr_mask = BIT(7) | BIT(13);

		if (entry->size == 0xffff) {
			pr_info("Can't get DIMM%i size\n",
				dimm_fill->count);
			dimm->nr_pages = MiB_TO_PAGES(32);/* Unknown */
		} else if (entry->size == 0x7fff) {
			dimm->nr_pages = MiB_TO_PAGES(entry->extended_size);
		} else {
			if (entry->size & BIT(15))
				dimm->nr_pages = MiB_TO_PAGES((entry->size & 0x7fff) << 10);
			else
				dimm->nr_pages = MiB_TO_PAGES(entry->size);
		}

		switch (entry->memory_type) {
		case 0x12:
			if (entry->type_detail & BIT(13))
				dimm->mtype = MEM_RDDR;
			else
				dimm->mtype = MEM_DDR;
			break;
		case 0x13:
			if (entry->type_detail & BIT(13))
				dimm->mtype = MEM_RDDR2;
			else
				dimm->mtype = MEM_DDR2;
			break;
		case 0x14:
			dimm->mtype = MEM_FB_DDR2;
			break;
		case 0x18:
			if (entry->type_detail & BIT(12))
				dimm->mtype = MEM_NVDIMM;
			else if (entry->type_detail & BIT(13))
				dimm->mtype = MEM_RDDR3;
			else
				dimm->mtype = MEM_DDR3;
			break;
		case 0x1a:
			if (entry->type_detail & BIT(12))
				dimm->mtype = MEM_NVDIMM;
			else if (entry->type_detail & BIT(13))
				dimm->mtype = MEM_RDDR4;
			else
				dimm->mtype = MEM_DDR4;
			break;
		default:
			if (entry->type_detail & BIT(6))
				dimm->mtype = MEM_RMBS;
			else if ((entry->type_detail & rdr_mask) == rdr_mask)
				dimm->mtype = MEM_RDR;
			else if (entry->type_detail & BIT(7))
				dimm->mtype = MEM_SDR;
			else if (entry->type_detail & BIT(9))
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
			edac_dbg(1, "DIMM%i: %s size = %d MB%s\n",
				dimm_fill->count, edac_mem_types[dimm->mtype],
				PAGES_TO_MiB(dimm->nr_pages),
				(dimm->edac_mode != EDAC_NONE) ? "(ECC)" : "");
			edac_dbg(2, "\ttype %d, detail 0x%02x, width %d(total %d)\n",
				entry->memory_type, entry->type_detail,
				entry->total_width, entry->data_width);
		}

		dimm->smbios_handle = entry->handle;

		dimm_fill->count++;
	}
}

void ghes_edac_report_mem_error(int sev, struct cper_sec_mem_err *mem_err)
{
	enum hw_event_mc_err_type type;
	struct edac_raw_error_desc *e;
	struct mem_ctl_info *mci;
	struct ghes_edac_pvt *pvt = ghes_pvt;
	unsigned long flags;
	char *p;
	u8 grain_bits;

	if (!pvt)
		return;

	/*
	 * We can do the locking below because GHES defers error processing
	 * from NMI to IRQ context. Whenever that changes, we'd at least
	 * know.
	 */
	if (WARN_ON_ONCE(in_nmi()))
		return;

	spin_lock_irqsave(&ghes_lock, flags);

	mci = pvt->mci;
	e = &mci->error_desc;

	/* Cleans the error report buffer */
	memset(e, 0, sizeof (*e));
	e->error_count = 1;
	strcpy(e->label, "unknown label");
	e->msg = pvt->msg;
	e->other_detail = pvt->other_detail;
	e->top_layer = -1;
	e->mid_layer = -1;
	e->low_layer = -1;
	*pvt->other_detail = '\0';
	*pvt->msg = '\0';

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

	edac_dbg(1, "error validation_bits: 0x%08llx\n",
		 (long long)mem_err->validation_bits);

	/* Error type, mapped on e->msg */
	if (mem_err->validation_bits & CPER_MEM_VALID_ERROR_TYPE) {
		p = pvt->msg;
		switch (mem_err->error_type) {
		case 0:
			p += sprintf(p, "Unknown");
			break;
		case 1:
			p += sprintf(p, "No error");
			break;
		case 2:
			p += sprintf(p, "Single-bit ECC");
			break;
		case 3:
			p += sprintf(p, "Multi-bit ECC");
			break;
		case 4:
			p += sprintf(p, "Single-symbol ChipKill ECC");
			break;
		case 5:
			p += sprintf(p, "Multi-symbol ChipKill ECC");
			break;
		case 6:
			p += sprintf(p, "Master abort");
			break;
		case 7:
			p += sprintf(p, "Target abort");
			break;
		case 8:
			p += sprintf(p, "Parity Error");
			break;
		case 9:
			p += sprintf(p, "Watchdog timeout");
			break;
		case 10:
			p += sprintf(p, "Invalid address");
			break;
		case 11:
			p += sprintf(p, "Mirror Broken");
			break;
		case 12:
			p += sprintf(p, "Memory Sparing");
			break;
		case 13:
			p += sprintf(p, "Scrub corrected error");
			break;
		case 14:
			p += sprintf(p, "Scrub uncorrected error");
			break;
		case 15:
			p += sprintf(p, "Physical Memory Map-out event");
			break;
		default:
			p += sprintf(p, "reserved error (%d)",
				     mem_err->error_type);
		}
	} else {
		strcpy(pvt->msg, "unknown error");
	}

	/* Error address */
	if (mem_err->validation_bits & CPER_MEM_VALID_PA) {
		e->page_frame_number = mem_err->physical_addr >> PAGE_SHIFT;
		e->offset_in_page = mem_err->physical_addr & ~PAGE_MASK;
	}

	/* Error grain */
	if (mem_err->validation_bits & CPER_MEM_VALID_PA_MASK)
		e->grain = ~(mem_err->physical_addr_mask & ~PAGE_MASK);

	/* Memory error location, mapped on e->location */
	p = e->location;
	if (mem_err->validation_bits & CPER_MEM_VALID_NODE)
		p += sprintf(p, "node:%d ", mem_err->node);
	if (mem_err->validation_bits & CPER_MEM_VALID_CARD)
		p += sprintf(p, "card:%d ", mem_err->card);
	if (mem_err->validation_bits & CPER_MEM_VALID_MODULE)
		p += sprintf(p, "module:%d ", mem_err->module);
	if (mem_err->validation_bits & CPER_MEM_VALID_RANK_NUMBER)
		p += sprintf(p, "rank:%d ", mem_err->rank);
	if (mem_err->validation_bits & CPER_MEM_VALID_BANK)
		p += sprintf(p, "bank:%d ", mem_err->bank);
	if (mem_err->validation_bits & CPER_MEM_VALID_ROW)
		p += sprintf(p, "row:%d ", mem_err->row);
	if (mem_err->validation_bits & CPER_MEM_VALID_COLUMN)
		p += sprintf(p, "col:%d ", mem_err->column);
	if (mem_err->validation_bits & CPER_MEM_VALID_BIT_POSITION)
		p += sprintf(p, "bit_pos:%d ", mem_err->bit_pos);
	if (mem_err->validation_bits & CPER_MEM_VALID_MODULE_HANDLE) {
		const char *bank = NULL, *device = NULL;
		int index = -1;

		dmi_memdev_name(mem_err->mem_dev_handle, &bank, &device);
		if (bank != NULL && device != NULL)
			p += sprintf(p, "DIMM location:%s %s ", bank, device);
		else
			p += sprintf(p, "DIMM DMI handle: 0x%.4x ",
				     mem_err->mem_dev_handle);

		index = get_dimm_smbios_index(mem_err->mem_dev_handle);
		if (index >= 0) {
			e->top_layer = index;
			e->enable_per_layer_report = true;
		}

	}
	if (p > e->location)
		*(p - 1) = '\0';

	/* All other fields are mapped on e->other_detail */
	p = pvt->other_detail;
	if (mem_err->validation_bits & CPER_MEM_VALID_ERROR_STATUS) {
		u64 status = mem_err->error_status;

		p += sprintf(p, "status(0x%016llx): ", (long long)status);
		switch ((status >> 8) & 0xff) {
		case 1:
			p += sprintf(p, "Error detected internal to the component ");
			break;
		case 16:
			p += sprintf(p, "Error detected in the bus ");
			break;
		case 4:
			p += sprintf(p, "Storage error in DRAM memory ");
			break;
		case 5:
			p += sprintf(p, "Storage error in TLB ");
			break;
		case 6:
			p += sprintf(p, "Storage error in cache ");
			break;
		case 7:
			p += sprintf(p, "Error in one or more functional units ");
			break;
		case 8:
			p += sprintf(p, "component failed self test ");
			break;
		case 9:
			p += sprintf(p, "Overflow or undervalue of internal queue ");
			break;
		case 17:
			p += sprintf(p, "Virtual address not found on IO-TLB or IO-PDIR ");
			break;
		case 18:
			p += sprintf(p, "Improper access error ");
			break;
		case 19:
			p += sprintf(p, "Access to a memory address which is not mapped to any component ");
			break;
		case 20:
			p += sprintf(p, "Loss of Lockstep ");
			break;
		case 21:
			p += sprintf(p, "Response not associated with a request ");
			break;
		case 22:
			p += sprintf(p, "Bus parity error - must also set the A, C, or D Bits ");
			break;
		case 23:
			p += sprintf(p, "Detection of a PATH_ERROR ");
			break;
		case 25:
			p += sprintf(p, "Bus operation timeout ");
			break;
		case 26:
			p += sprintf(p, "A read was issued to data that has been poisoned ");
			break;
		default:
			p += sprintf(p, "reserved ");
			break;
		}
	}
	if (mem_err->validation_bits & CPER_MEM_VALID_REQUESTOR_ID)
		p += sprintf(p, "requestorID: 0x%016llx ",
			     (long long)mem_err->requestor_id);
	if (mem_err->validation_bits & CPER_MEM_VALID_RESPONDER_ID)
		p += sprintf(p, "responderID: 0x%016llx ",
			     (long long)mem_err->responder_id);
	if (mem_err->validation_bits & CPER_MEM_VALID_TARGET_ID)
		p += sprintf(p, "targetID: 0x%016llx ",
			     (long long)mem_err->responder_id);
	if (p > pvt->other_detail)
		*(p - 1) = '\0';

	/* Generate the trace event */
	grain_bits = fls_long(e->grain);
	snprintf(pvt->detail_location, sizeof(pvt->detail_location),
		 "APEI location: %s %s", e->location, e->other_detail);
	trace_mc_event(type, e->msg, e->label, e->error_count,
		       mci->mc_idx, e->top_layer, e->mid_layer, e->low_layer,
		       (e->page_frame_number << PAGE_SHIFT) | e->offset_in_page,
		       grain_bits, e->syndrome, pvt->detail_location);

	edac_raw_mc_handle_error(type, mci, e);
	spin_unlock_irqrestore(&ghes_lock, flags);
}

/*
 * Known systems that are safe to enable this module.
 */
static struct acpi_platform_list plat_list[] = {
	{"HPE   ", "Server  ", 0, ACPI_SIG_FADT, all_versions},
	{ } /* End */
};

int ghes_edac_register(struct ghes *ghes, struct device *dev)
{
	bool fake = false;
	int rc, num_dimm = 0;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[1];
	struct ghes_edac_dimm_fill dimm_fill;
	int idx = -1;

	if (IS_ENABLED(CONFIG_X86)) {
		/* Check if safe to enable on this system */
		idx = acpi_match_platform_list(plat_list);
		if (!force_load && idx < 0)
			return -ENODEV;
	} else {
		idx = 0;
	}

	/*
	 * We have only one logical memory controller to which all DIMMs belong.
	 */
	if (atomic_inc_return(&ghes_init) > 1)
		return 0;

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

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(struct ghes_edac_pvt));
	if (!mci) {
		pr_info("Can't allocate memory for EDAC data\n");
		return -ENOMEM;
	}

	ghes_pvt	= mci->pvt_info;
	ghes_pvt->ghes	= ghes;
	ghes_pvt->mci	= mci;

	mci->pdev = dev;
	mci->mtype_cap = MEM_FLAG_EMPTY;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "ghes_edac.c";
	mci->ctl_name = "ghes_edac";
	mci->dev_name = "ghes";

	if (fake) {
		pr_info("This system has a very crappy BIOS: It doesn't even list the DIMMS.\n");
		pr_info("Its SMBIOS info is wrong. It is doubtful that the error report would\n");
		pr_info("work on such system. Use this driver with caution\n");
	} else if (idx < 0) {
		pr_info("This EDAC driver relies on BIOS to enumerate memory and get error reports.\n");
		pr_info("Unfortunately, not all BIOSes reflect the memory layout correctly.\n");
		pr_info("So, the end result of using this driver varies from vendor to vendor.\n");
		pr_info("If you find incorrect reports, please contact your hardware vendor\n");
		pr_info("to correct its BIOS.\n");
		pr_info("This system has %d DIMM sockets.\n", num_dimm);
	}

	if (!fake) {
		dimm_fill.count = 0;
		dimm_fill.mci = mci;
		dmi_walk(ghes_edac_dmidecode, &dimm_fill);
	} else {
		struct dimm_info *dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
						       mci->n_layers, 0, 0, 0);

		dimm->nr_pages = 1;
		dimm->grain = 128;
		dimm->mtype = MEM_UNKNOWN;
		dimm->dtype = DEV_UNKNOWN;
		dimm->edac_mode = EDAC_SECDED;
	}

	rc = edac_mc_add_mc(mci);
	if (rc < 0) {
		pr_info("Can't register at EDAC core\n");
		edac_mc_free(mci);
		return -ENODEV;
	}
	return 0;
}

void ghes_edac_unregister(struct ghes *ghes)
{
	struct mem_ctl_info *mci;

	if (!ghes_pvt)
		return;

	mci = ghes_pvt->mci;
	edac_mc_del_mc(mci->pdev);
	edac_mc_free(mci);
}
