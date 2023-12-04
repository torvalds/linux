// SPDX-License-Identifier: GPL-2.0-only
/*
 * Extended Error Log driver
 *
 * Copyright (C) 2013 Intel Corp.
 * Author: Chen, Gong <gong.chen@intel.com>
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/cper.h>
#include <linux/ratelimit.h>
#include <linux/edac.h>
#include <linux/ras.h>
#include <acpi/ghes.h>
#include <asm/cpu.h>
#include <asm/mce.h>

#include "apei/apei-internal.h"
#include <ras/ras_event.h>

#define EXT_ELOG_ENTRY_MASK	GENMASK_ULL(51, 0) /* elog entry address mask */

#define EXTLOG_DSM_REV		0x0
#define	EXTLOG_FN_ADDR		0x1

#define FLAG_OS_OPTIN		BIT(0)
#define ELOG_ENTRY_VALID	(1ULL<<63)
#define ELOG_ENTRY_LEN		0x1000

#define EMCA_BUG \
	"Can not request iomem region <0x%016llx-0x%016llx> - eMCA disabled\n"

struct extlog_l1_head {
	u32 ver;	/* Header Version */
	u32 hdr_len;	/* Header Length */
	u64 total_len;	/* entire L1 Directory length including this header */
	u64 elog_base;	/* MCA Error Log Directory base address */
	u64 elog_len;	/* MCA Error Log Directory length */
	u32 flags;	/* bit 0 - OS/VMM Opt-in */
	u8  rev0[12];
	u32 entries;	/* Valid L1 Directory entries per logical processor */
	u8  rev1[12];
};

static u8 extlog_dsm_uuid[] __initdata = "663E35AF-CC10-41A4-88EA-5470AF055295";

/* L1 table related physical address */
static u64 elog_base;
static size_t elog_size;
static u64 l1_dirbase;
static size_t l1_size;

/* L1 table related virtual address */
static void __iomem *extlog_l1_addr;
static void __iomem *elog_addr;

static void *elog_buf;

static u64 *l1_entry_base;
static u32 l1_percpu_entry;

#define ELOG_IDX(cpu, bank) \
	(cpu_physical_id(cpu) * l1_percpu_entry + (bank))

#define ELOG_ENTRY_DATA(idx) \
	(*(l1_entry_base + (idx)))

#define ELOG_ENTRY_ADDR(phyaddr) \
	(phyaddr - elog_base + (u8 *)elog_addr)

static struct acpi_hest_generic_status *extlog_elog_entry_check(int cpu, int bank)
{
	int idx;
	u64 data;
	struct acpi_hest_generic_status *estatus;

	WARN_ON(cpu < 0);
	idx = ELOG_IDX(cpu, bank);
	data = ELOG_ENTRY_DATA(idx);
	if ((data & ELOG_ENTRY_VALID) == 0)
		return NULL;

	data &= EXT_ELOG_ENTRY_MASK;
	estatus = (struct acpi_hest_generic_status *)ELOG_ENTRY_ADDR(data);

	/* if no valid data in elog entry, just return */
	if (estatus->block_status == 0)
		return NULL;

	return estatus;
}

static void __print_extlog_rcd(const char *pfx,
			       struct acpi_hest_generic_status *estatus, int cpu)
{
	static atomic_t seqno;
	unsigned int curr_seqno;
	char pfx_seq[64];

	if (!pfx) {
		if (estatus->error_severity <= CPER_SEV_CORRECTED)
			pfx = KERN_INFO;
		else
			pfx = KERN_ERR;
	}
	curr_seqno = atomic_inc_return(&seqno);
	snprintf(pfx_seq, sizeof(pfx_seq), "%s{%u}", pfx, curr_seqno);
	printk("%s""Hardware error detected on CPU%d\n", pfx_seq, cpu);
	cper_estatus_print(pfx_seq, estatus);
}

static int print_extlog_rcd(const char *pfx,
			    struct acpi_hest_generic_status *estatus, int cpu)
{
	/* Not more than 2 messages every 5 seconds */
	static DEFINE_RATELIMIT_STATE(ratelimit_corrected, 5*HZ, 2);
	static DEFINE_RATELIMIT_STATE(ratelimit_uncorrected, 5*HZ, 2);
	struct ratelimit_state *ratelimit;

	if (estatus->error_severity == CPER_SEV_CORRECTED ||
	    (estatus->error_severity == CPER_SEV_INFORMATIONAL))
		ratelimit = &ratelimit_corrected;
	else
		ratelimit = &ratelimit_uncorrected;
	if (__ratelimit(ratelimit)) {
		__print_extlog_rcd(pfx, estatus, cpu);
		return 0;
	}

	return 1;
}

static int extlog_print(struct notifier_block *nb, unsigned long val,
			void *data)
{
	struct mce *mce = (struct mce *)data;
	int	bank = mce->bank;
	int	cpu = mce->extcpu;
	struct acpi_hest_generic_status *estatus, *tmp;
	struct acpi_hest_generic_data *gdata;
	const guid_t *fru_id;
	char *fru_text;
	guid_t *sec_type;
	static u32 err_seq;

	estatus = extlog_elog_entry_check(cpu, bank);
	if (!estatus)
		return NOTIFY_DONE;

	if (mce->kflags & MCE_HANDLED_CEC) {
		estatus->block_status = 0;
		return NOTIFY_DONE;
	}

	memcpy(elog_buf, (void *)estatus, ELOG_ENTRY_LEN);
	/* clear record status to enable BIOS to update it again */
	estatus->block_status = 0;

	tmp = (struct acpi_hest_generic_status *)elog_buf;

	if (!ras_userspace_consumers()) {
		print_extlog_rcd(NULL, tmp, cpu);
		goto out;
	}

	/* log event via trace */
	err_seq++;
	apei_estatus_for_each_section(tmp, gdata) {
		if (gdata->validation_bits & CPER_SEC_VALID_FRU_ID)
			fru_id = (guid_t *)gdata->fru_id;
		else
			fru_id = &guid_null;
		if (gdata->validation_bits & CPER_SEC_VALID_FRU_TEXT)
			fru_text = gdata->fru_text;
		else
			fru_text = "";
		sec_type = (guid_t *)gdata->section_type;
		if (guid_equal(sec_type, &CPER_SEC_PLATFORM_MEM)) {
			struct cper_sec_mem_err *mem = acpi_hest_get_payload(gdata);

			if (gdata->error_data_length >= sizeof(*mem))
				trace_extlog_mem_event(mem, err_seq, fru_id, fru_text,
						       (u8)gdata->error_severity);
		}
	}

out:
	mce->kflags |= MCE_HANDLED_EXTLOG;
	return NOTIFY_OK;
}

static bool __init extlog_get_l1addr(void)
{
	guid_t guid;
	acpi_handle handle;
	union acpi_object *obj;

	if (guid_parse(extlog_dsm_uuid, &guid))
		return false;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &handle)))
		return false;
	if (!acpi_check_dsm(handle, &guid, EXTLOG_DSM_REV, 1 << EXTLOG_FN_ADDR))
		return false;
	obj = acpi_evaluate_dsm_typed(handle, &guid, EXTLOG_DSM_REV,
				      EXTLOG_FN_ADDR, NULL, ACPI_TYPE_INTEGER);
	if (!obj) {
		return false;
	} else {
		l1_dirbase = obj->integer.value;
		ACPI_FREE(obj);
	}

	/* Spec says L1 directory must be 4K aligned, bail out if it isn't */
	if (l1_dirbase & ((1 << 12) - 1)) {
		pr_warn(FW_BUG "L1 Directory is invalid at physical %llx\n",
			l1_dirbase);
		return false;
	}

	return true;
}
static struct notifier_block extlog_mce_dec = {
	.notifier_call	= extlog_print,
	.priority	= MCE_PRIO_EXTLOG,
};

static int __init extlog_init(void)
{
	struct extlog_l1_head *l1_head;
	void __iomem *extlog_l1_hdr;
	size_t l1_hdr_size;
	struct resource *r;
	u64 cap;
	int rc;

	if (rdmsrl_safe(MSR_IA32_MCG_CAP, &cap) ||
	    !(cap & MCG_ELOG_P) ||
	    !extlog_get_l1addr())
		return -ENODEV;

	rc = -EINVAL;
	/* get L1 header to fetch necessary information */
	l1_hdr_size = sizeof(struct extlog_l1_head);
	r = request_mem_region(l1_dirbase, l1_hdr_size, "L1 DIR HDR");
	if (!r) {
		pr_warn(FW_BUG EMCA_BUG,
			(unsigned long long)l1_dirbase,
			(unsigned long long)l1_dirbase + l1_hdr_size);
		goto err;
	}

	extlog_l1_hdr = acpi_os_map_iomem(l1_dirbase, l1_hdr_size);
	l1_head = (struct extlog_l1_head *)extlog_l1_hdr;
	l1_size = l1_head->total_len;
	l1_percpu_entry = l1_head->entries;
	elog_base = l1_head->elog_base;
	elog_size = l1_head->elog_len;
	acpi_os_unmap_iomem(extlog_l1_hdr, l1_hdr_size);
	release_mem_region(l1_dirbase, l1_hdr_size);

	/* remap L1 header again based on completed information */
	r = request_mem_region(l1_dirbase, l1_size, "L1 Table");
	if (!r) {
		pr_warn(FW_BUG EMCA_BUG,
			(unsigned long long)l1_dirbase,
			(unsigned long long)l1_dirbase + l1_size);
		goto err;
	}
	extlog_l1_addr = acpi_os_map_iomem(l1_dirbase, l1_size);
	l1_entry_base = (u64 *)((u8 *)extlog_l1_addr + l1_hdr_size);

	/* remap elog table */
	r = request_mem_region(elog_base, elog_size, "Elog Table");
	if (!r) {
		pr_warn(FW_BUG EMCA_BUG,
			(unsigned long long)elog_base,
			(unsigned long long)elog_base + elog_size);
		goto err_release_l1_dir;
	}
	elog_addr = acpi_os_map_iomem(elog_base, elog_size);

	rc = -ENOMEM;
	/* allocate buffer to save elog record */
	elog_buf = kmalloc(ELOG_ENTRY_LEN, GFP_KERNEL);
	if (elog_buf == NULL)
		goto err_release_elog;

	mce_register_decode_chain(&extlog_mce_dec);
	/* enable OS to be involved to take over management from BIOS */
	((struct extlog_l1_head *)extlog_l1_addr)->flags |= FLAG_OS_OPTIN;

	return 0;

err_release_elog:
	if (elog_addr)
		acpi_os_unmap_iomem(elog_addr, elog_size);
	release_mem_region(elog_base, elog_size);
err_release_l1_dir:
	if (extlog_l1_addr)
		acpi_os_unmap_iomem(extlog_l1_addr, l1_size);
	release_mem_region(l1_dirbase, l1_size);
err:
	pr_warn(FW_BUG "Extended error log disabled because of problems parsing f/w tables\n");
	return rc;
}

static void __exit extlog_exit(void)
{
	mce_unregister_decode_chain(&extlog_mce_dec);
	if (extlog_l1_addr) {
		((struct extlog_l1_head *)extlog_l1_addr)->flags &= ~FLAG_OS_OPTIN;
		acpi_os_unmap_iomem(extlog_l1_addr, l1_size);
	}
	if (elog_addr)
		acpi_os_unmap_iomem(elog_addr, elog_size);
	release_mem_region(elog_base, elog_size);
	release_mem_region(l1_dirbase, l1_size);
	kfree(elog_buf);
}

module_init(extlog_init);
module_exit(extlog_exit);

MODULE_AUTHOR("Chen, Gong <gong.chen@intel.com>");
MODULE_DESCRIPTION("Extended MCA Error Log Driver");
MODULE_LICENSE("GPL");
