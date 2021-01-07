// SPDX-License-Identifier: GPL-2.0-only
/*
 * GHES/EDAC Linux driver
 *
 * Copyright (c) 2013 by Mauro Carvalho Chehab
 *
 * Red Hat Inc. https://www.redhat.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <acpi/ghes.h>
#include <linux/edac.h>
#include <linux/dmi.h>
#include "edac_module.h"
#include <ras/ras_event.h>

struct ghes_pvt {
	struct mem_ctl_info *mci;

	/* Buffers for the error handling routine */
	char other_detail[400];
	char msg[80];
};

static refcount_t ghes_refcount = REFCOUNT_INIT(0);

/*
 * Access to ghes_pvt must be protected by ghes_lock. The spinlock
 * also provides the necessary (implicit) memory barrier for the SMP
 * case to make the pointer visible on another CPU.
 */
static struct ghes_pvt *ghes_pvt;

/*
 * This driver's representation of the system hardware, as collected
 * from DMI.
 */
struct ghes_hw_desc {
	int num_dimms;
	struct dimm_info *dimms;
} ghes_hw;

/* GHES registration mutex */
static DEFINE_MUTEX(ghes_reg_mutex);

/*
 * Sync with other, potentially concurrent callers of
 * ghes_edac_report_mem_error(). We don't know what the
 * "inventive" firmware would do.
 */
static DEFINE_SPINLOCK(ghes_lock);

/* "ghes_edac.force_load=1" skips the platform check */
static bool __read_mostly force_load;
module_param(force_load, bool, 0);

static bool system_scanned;

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

static struct dimm_info *find_dimm_by_handle(struct mem_ctl_info *mci, u16 handle)
{
	struct dimm_info *dimm;

	mci_for_each_dimm(mci, dimm) {
		if (dimm->smbios_handle == handle)
			return dimm;
	}

	return NULL;
}

static void dimm_setup_label(struct dimm_info *dimm, u16 handle)
{
	const char *bank = NULL, *device = NULL;

	dmi_memdev_name(handle, &bank, &device);

	/* both strings must be non-zero */
	if (bank && *bank && device && *device)
		snprintf(dimm->label, sizeof(dimm->label), "%s %s", bank, device);
}

static void assign_dmi_dimm_info(struct dimm_info *dimm, struct memdev_dmi_entry *entry)
{
	u16 rdr_mask = BIT(7) | BIT(13);

	if (entry->size == 0xffff) {
		pr_info("Can't get DIMM%i size\n", dimm->idx);
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

	dimm_setup_label(dimm, entry->handle);

	if (dimm->nr_pages) {
		edac_dbg(1, "DIMM%i: %s size = %d MB%s\n",
			dimm->idx, edac_mem_types[dimm->mtype],
			PAGES_TO_MiB(dimm->nr_pages),
			(dimm->edac_mode != EDAC_NONE) ? "(ECC)" : "");
		edac_dbg(2, "\ttype %d, detail 0x%02x, width %d(total %d)\n",
			entry->memory_type, entry->type_detail,
			entry->total_width, entry->data_width);
	}

	dimm->smbios_handle = entry->handle;
}

static void enumerate_dimms(const struct dmi_header *dh, void *arg)
{
	struct memdev_dmi_entry *entry = (struct memdev_dmi_entry *)dh;
	struct ghes_hw_desc *hw = (struct ghes_hw_desc *)arg;
	struct dimm_info *d;

	if (dh->type != DMI_ENTRY_MEM_DEVICE)
		return;

	/* Enlarge the array with additional 16 */
	if (!hw->num_dimms || !(hw->num_dimms % 16)) {
		struct dimm_info *new;

		new = krealloc_array(hw->dimms, hw->num_dimms + 16,
				     sizeof(struct dimm_info), GFP_KERNEL);
		if (!new) {
			WARN_ON_ONCE(1);
			return;
		}

		hw->dimms = new;
	}

	d = &hw->dimms[hw->num_dimms];
	d->idx = hw->num_dimms;

	assign_dmi_dimm_info(d, entry);

	hw->num_dimms++;
}

static void ghes_scan_system(void)
{
	if (system_scanned)
		return;

	dmi_walk(enumerate_dimms, &ghes_hw);

	system_scanned = true;
}

void ghes_edac_report_mem_error(int sev, struct cper_sec_mem_err *mem_err)
{
	struct edac_raw_error_desc *e;
	struct mem_ctl_info *mci;
	struct ghes_pvt *pvt;
	unsigned long flags;
	char *p;

	/*
	 * We can do the locking below because GHES defers error processing
	 * from NMI to IRQ context. Whenever that changes, we'd at least
	 * know.
	 */
	if (WARN_ON_ONCE(in_nmi()))
		return;

	spin_lock_irqsave(&ghes_lock, flags);

	pvt = ghes_pvt;
	if (!pvt)
		goto unlock;

	mci = pvt->mci;
	e = &mci->error_desc;

	/* Cleans the error report buffer */
	memset(e, 0, sizeof (*e));
	e->error_count = 1;
	e->grain = 1;
	e->msg = pvt->msg;
	e->other_detail = pvt->other_detail;
	e->top_layer = -1;
	e->mid_layer = -1;
	e->low_layer = -1;
	*pvt->other_detail = '\0';
	*pvt->msg = '\0';

	switch (sev) {
	case GHES_SEV_CORRECTED:
		e->type = HW_EVENT_ERR_CORRECTED;
		break;
	case GHES_SEV_RECOVERABLE:
		e->type = HW_EVENT_ERR_UNCORRECTED;
		break;
	case GHES_SEV_PANIC:
		e->type = HW_EVENT_ERR_FATAL;
		break;
	default:
	case GHES_SEV_NO:
		e->type = HW_EVENT_ERR_INFO;
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
		e->page_frame_number = PHYS_PFN(mem_err->physical_addr);
		e->offset_in_page = offset_in_page(mem_err->physical_addr);
	}

	/* Error grain */
	if (mem_err->validation_bits & CPER_MEM_VALID_PA_MASK)
		e->grain = ~mem_err->physical_addr_mask + 1;

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
	if (mem_err->validation_bits & CPER_MEM_VALID_BANK_GROUP)
		p += sprintf(p, "bank_group:%d ",
			     mem_err->bank >> CPER_MEM_BANK_GROUP_SHIFT);
	if (mem_err->validation_bits & CPER_MEM_VALID_BANK_ADDRESS)
		p += sprintf(p, "bank_address:%d ",
			     mem_err->bank & CPER_MEM_BANK_ADDRESS_MASK);
	if (mem_err->validation_bits & (CPER_MEM_VALID_ROW | CPER_MEM_VALID_ROW_EXT)) {
		u32 row = mem_err->row;

		row |= cper_get_mem_extension(mem_err->validation_bits, mem_err->extended);
		p += sprintf(p, "row:%d ", row);
	}
	if (mem_err->validation_bits & CPER_MEM_VALID_COLUMN)
		p += sprintf(p, "col:%d ", mem_err->column);
	if (mem_err->validation_bits & CPER_MEM_VALID_BIT_POSITION)
		p += sprintf(p, "bit_pos:%d ", mem_err->bit_pos);
	if (mem_err->validation_bits & CPER_MEM_VALID_MODULE_HANDLE) {
		const char *bank = NULL, *device = NULL;
		struct dimm_info *dimm;

		dmi_memdev_name(mem_err->mem_dev_handle, &bank, &device);
		if (bank != NULL && device != NULL)
			p += sprintf(p, "DIMM location:%s %s ", bank, device);
		else
			p += sprintf(p, "DIMM DMI handle: 0x%.4x ",
				     mem_err->mem_dev_handle);

		dimm = find_dimm_by_handle(mci, mem_err->mem_dev_handle);
		if (dimm) {
			e->top_layer = dimm->idx;
			strcpy(e->label, dimm->label);
		}
	}
	if (mem_err->validation_bits & CPER_MEM_VALID_CHIP_ID)
		p += sprintf(p, "chipID: %d ",
			     mem_err->extended >> CPER_MEM_CHIP_ID_SHIFT);
	if (p > e->location)
		*(p - 1) = '\0';

	if (!*e->label)
		strcpy(e->label, "unknown memory");

	/* All other fields are mapped on e->other_detail */
	p = pvt->other_detail;
	p += snprintf(p, sizeof(pvt->other_detail),
		"APEI location: %s ", e->location);
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

	edac_raw_mc_handle_error(e);

unlock:
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
	struct mem_ctl_info *mci;
	struct ghes_pvt *pvt;
	struct edac_mc_layer layers[1];
	unsigned long flags;
	int idx = -1;
	int rc = 0;

	if (IS_ENABLED(CONFIG_X86)) {
		/* Check if safe to enable on this system */
		idx = acpi_match_platform_list(plat_list);
		if (!force_load && idx < 0)
			return -ENODEV;
	} else {
		force_load = true;
		idx = 0;
	}

	/* finish another registration/unregistration instance first */
	mutex_lock(&ghes_reg_mutex);

	/*
	 * We have only one logical memory controller to which all DIMMs belong.
	 */
	if (refcount_inc_not_zero(&ghes_refcount))
		goto unlock;

	ghes_scan_system();

	/* Check if we've got a bogus BIOS */
	if (!ghes_hw.num_dimms) {
		fake = true;
		ghes_hw.num_dimms = 1;
	}

	layers[0].type = EDAC_MC_LAYER_ALL_MEM;
	layers[0].size = ghes_hw.num_dimms;
	layers[0].is_virt_csrow = true;

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(struct ghes_pvt));
	if (!mci) {
		pr_info("Can't allocate memory for EDAC data\n");
		rc = -ENOMEM;
		goto unlock;
	}

	pvt		= mci->pvt_info;
	pvt->mci	= mci;

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
		pr_info("This system has %d DIMM sockets.\n", ghes_hw.num_dimms);
	}

	if (!fake) {
		struct dimm_info *src, *dst;
		int i = 0;

		mci_for_each_dimm(mci, dst) {
			src = &ghes_hw.dimms[i];

			dst->idx	   = src->idx;
			dst->smbios_handle = src->smbios_handle;
			dst->nr_pages	   = src->nr_pages;
			dst->mtype	   = src->mtype;
			dst->edac_mode	   = src->edac_mode;
			dst->dtype	   = src->dtype;
			dst->grain	   = src->grain;

			/*
			 * If no src->label, preserve default label assigned
			 * from EDAC core.
			 */
			if (strlen(src->label))
				memcpy(dst->label, src->label, sizeof(src->label));

			i++;
		}

	} else {
		struct dimm_info *dimm = edac_get_dimm(mci, 0, 0, 0);

		dimm->nr_pages = 1;
		dimm->grain = 128;
		dimm->mtype = MEM_UNKNOWN;
		dimm->dtype = DEV_UNKNOWN;
		dimm->edac_mode = EDAC_SECDED;
	}

	rc = edac_mc_add_mc(mci);
	if (rc < 0) {
		pr_info("Can't register with the EDAC core\n");
		edac_mc_free(mci);
		rc = -ENODEV;
		goto unlock;
	}

	spin_lock_irqsave(&ghes_lock, flags);
	ghes_pvt = pvt;
	spin_unlock_irqrestore(&ghes_lock, flags);

	/* only set on success */
	refcount_set(&ghes_refcount, 1);

unlock:

	/* Not needed anymore */
	kfree(ghes_hw.dimms);
	ghes_hw.dimms = NULL;

	mutex_unlock(&ghes_reg_mutex);

	return rc;
}

void ghes_edac_unregister(struct ghes *ghes)
{
	struct mem_ctl_info *mci;
	unsigned long flags;

	if (!force_load)
		return;

	mutex_lock(&ghes_reg_mutex);

	system_scanned = false;
	memset(&ghes_hw, 0, sizeof(struct ghes_hw_desc));

	if (!refcount_dec_and_test(&ghes_refcount))
		goto unlock;

	/*
	 * Wait for the irq handler being finished.
	 */
	spin_lock_irqsave(&ghes_lock, flags);
	mci = ghes_pvt ? ghes_pvt->mci : NULL;
	ghes_pvt = NULL;
	spin_unlock_irqrestore(&ghes_lock, flags);

	if (!mci)
		goto unlock;

	mci = edac_mc_del_mc(mci->pdev);
	if (mci)
		edac_mc_free(mci);

unlock:
	mutex_unlock(&ghes_reg_mutex);
}
