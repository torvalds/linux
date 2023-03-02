// SPDX-License-Identifier: GPL-2.0-only
/*
 * APEI Generic Hardware Error Source support
 *
 * Generic Hardware Error Source provides a way to report platform
 * hardware errors (such as that from chipset). It works in so called
 * "Firmware First" mode, that is, hardware errors are reported to
 * firmware firstly, then reported to Linux by firmware. This way,
 * some non-standard hardware error registers or non-standard hardware
 * link can be checked by firmware to produce more hardware error
 * information for Linux.
 *
 * For more information about Generic Hardware Error Source, please
 * refer to ACPI Specification version 4.0, section 17.3.2.6
 *
 * Copyright 2010,2011 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <linux/arm_sdei.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/cper.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <linux/irq_work.h>
#include <linux/llist.h>
#include <linux/genalloc.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/aer.h>
#include <linux/nmi.h>
#include <linux/sched/clock.h>
#include <linux/uuid.h>
#include <linux/ras.h>
#include <linux/task_work.h>

#include <acpi/actbl1.h>
#include <acpi/ghes.h>
#include <acpi/apei.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <ras/ras_event.h>

#include "apei-internal.h"

#define GHES_PFX	"GHES: "

#define GHES_ESTATUS_MAX_SIZE		65536
#define GHES_ESOURCE_PREALLOC_MAX_SIZE	65536

#define GHES_ESTATUS_POOL_MIN_ALLOC_ORDER 3

/* This is just an estimation for memory pool allocation */
#define GHES_ESTATUS_CACHE_AVG_SIZE	512

#define GHES_ESTATUS_CACHES_SIZE	4

#define GHES_ESTATUS_IN_CACHE_MAX_NSEC	10000000000ULL
/* Prevent too many caches are allocated because of RCU */
#define GHES_ESTATUS_CACHE_ALLOCED_MAX	(GHES_ESTATUS_CACHES_SIZE * 3 / 2)

#define GHES_ESTATUS_CACHE_LEN(estatus_len)			\
	(sizeof(struct ghes_estatus_cache) + (estatus_len))
#define GHES_ESTATUS_FROM_CACHE(estatus_cache)			\
	((struct acpi_hest_generic_status *)				\
	 ((struct ghes_estatus_cache *)(estatus_cache) + 1))

#define GHES_ESTATUS_NODE_LEN(estatus_len)			\
	(sizeof(struct ghes_estatus_node) + (estatus_len))
#define GHES_ESTATUS_FROM_NODE(estatus_node)			\
	((struct acpi_hest_generic_status *)				\
	 ((struct ghes_estatus_node *)(estatus_node) + 1))

#define GHES_VENDOR_ENTRY_LEN(gdata_len)                               \
	(sizeof(struct ghes_vendor_record_entry) + (gdata_len))
#define GHES_GDATA_FROM_VENDOR_ENTRY(vendor_entry)                     \
	((struct acpi_hest_generic_data *)                              \
	((struct ghes_vendor_record_entry *)(vendor_entry) + 1))

/*
 *  NMI-like notifications vary by architecture, before the compiler can prune
 *  unused static functions it needs a value for these enums.
 */
#ifndef CONFIG_ARM_SDE_INTERFACE
#define FIX_APEI_GHES_SDEI_NORMAL	__end_of_fixed_addresses
#define FIX_APEI_GHES_SDEI_CRITICAL	__end_of_fixed_addresses
#endif

static ATOMIC_NOTIFIER_HEAD(ghes_report_chain);

static inline bool is_hest_type_generic_v2(struct ghes *ghes)
{
	return ghes->generic->header.type == ACPI_HEST_TYPE_GENERIC_ERROR_V2;
}

/*
 * This driver isn't really modular, however for the time being,
 * continuing to use module_param is the easiest way to remain
 * compatible with existing boot arg use cases.
 */
bool ghes_disable;
module_param_named(disable, ghes_disable, bool, 0);

/*
 * "ghes.edac_force_enable" forcibly enables ghes_edac and skips the platform
 * check.
 */
static bool ghes_edac_force_enable;
module_param_named(edac_force_enable, ghes_edac_force_enable, bool, 0);

/*
 * All error sources notified with HED (Hardware Error Device) share a
 * single notifier callback, so they need to be linked and checked one
 * by one. This holds true for NMI too.
 *
 * RCU is used for these lists, so ghes_list_mutex is only used for
 * list changing, not for traversing.
 */
static LIST_HEAD(ghes_hed);
static DEFINE_MUTEX(ghes_list_mutex);

/*
 * A list of GHES devices which are given to the corresponding EDAC driver
 * ghes_edac for further use.
 */
static LIST_HEAD(ghes_devs);
static DEFINE_MUTEX(ghes_devs_mutex);

/*
 * Because the memory area used to transfer hardware error information
 * from BIOS to Linux can be determined only in NMI, IRQ or timer
 * handler, but general ioremap can not be used in atomic context, so
 * the fixmap is used instead.
 *
 * This spinlock is used to prevent the fixmap entry from being used
 * simultaneously.
 */
static DEFINE_SPINLOCK(ghes_notify_lock_irq);

struct ghes_vendor_record_entry {
	struct work_struct work;
	int error_severity;
	char vendor_record[];
};

static struct gen_pool *ghes_estatus_pool;
static unsigned long ghes_estatus_pool_size_request;

static struct ghes_estatus_cache __rcu *ghes_estatus_caches[GHES_ESTATUS_CACHES_SIZE];
static atomic_t ghes_estatus_cache_alloced;

static int ghes_panic_timeout __read_mostly = 30;

static void __iomem *ghes_map(u64 pfn, enum fixed_addresses fixmap_idx)
{
	phys_addr_t paddr;
	pgprot_t prot;

	paddr = PFN_PHYS(pfn);
	prot = arch_apei_get_mem_attribute(paddr);
	__set_fixmap(fixmap_idx, paddr, prot);

	return (void __iomem *) __fix_to_virt(fixmap_idx);
}

static void ghes_unmap(void __iomem *vaddr, enum fixed_addresses fixmap_idx)
{
	int _idx = virt_to_fix((unsigned long)vaddr);

	WARN_ON_ONCE(fixmap_idx != _idx);
	clear_fixmap(fixmap_idx);
}

int ghes_estatus_pool_init(unsigned int num_ghes)
{
	unsigned long addr, len;
	int rc;

	ghes_estatus_pool = gen_pool_create(GHES_ESTATUS_POOL_MIN_ALLOC_ORDER, -1);
	if (!ghes_estatus_pool)
		return -ENOMEM;

	len = GHES_ESTATUS_CACHE_AVG_SIZE * GHES_ESTATUS_CACHE_ALLOCED_MAX;
	len += (num_ghes * GHES_ESOURCE_PREALLOC_MAX_SIZE);

	ghes_estatus_pool_size_request = PAGE_ALIGN(len);
	addr = (unsigned long)vmalloc(PAGE_ALIGN(len));
	if (!addr)
		goto err_pool_alloc;

	rc = gen_pool_add(ghes_estatus_pool, addr, PAGE_ALIGN(len), -1);
	if (rc)
		goto err_pool_add;

	return 0;

err_pool_add:
	vfree((void *)addr);

err_pool_alloc:
	gen_pool_destroy(ghes_estatus_pool);

	return -ENOMEM;
}

static int map_gen_v2(struct ghes *ghes)
{
	return apei_map_generic_address(&ghes->generic_v2->read_ack_register);
}

static void unmap_gen_v2(struct ghes *ghes)
{
	apei_unmap_generic_address(&ghes->generic_v2->read_ack_register);
}

static void ghes_ack_error(struct acpi_hest_generic_v2 *gv2)
{
	int rc;
	u64 val = 0;

	rc = apei_read(&val, &gv2->read_ack_register);
	if (rc)
		return;

	val &= gv2->read_ack_preserve << gv2->read_ack_register.bit_offset;
	val |= gv2->read_ack_write    << gv2->read_ack_register.bit_offset;

	apei_write(val, &gv2->read_ack_register);
}

static struct ghes *ghes_new(struct acpi_hest_generic *generic)
{
	struct ghes *ghes;
	unsigned int error_block_length;
	int rc;

	ghes = kzalloc(sizeof(*ghes), GFP_KERNEL);
	if (!ghes)
		return ERR_PTR(-ENOMEM);

	ghes->generic = generic;
	if (is_hest_type_generic_v2(ghes)) {
		rc = map_gen_v2(ghes);
		if (rc)
			goto err_free;
	}

	rc = apei_map_generic_address(&generic->error_status_address);
	if (rc)
		goto err_unmap_read_ack_addr;
	error_block_length = generic->error_block_length;
	if (error_block_length > GHES_ESTATUS_MAX_SIZE) {
		pr_warn(FW_WARN GHES_PFX
			"Error status block length is too long: %u for "
			"generic hardware error source: %d.\n",
			error_block_length, generic->header.source_id);
		error_block_length = GHES_ESTATUS_MAX_SIZE;
	}
	ghes->estatus = kmalloc(error_block_length, GFP_KERNEL);
	if (!ghes->estatus) {
		rc = -ENOMEM;
		goto err_unmap_status_addr;
	}

	return ghes;

err_unmap_status_addr:
	apei_unmap_generic_address(&generic->error_status_address);
err_unmap_read_ack_addr:
	if (is_hest_type_generic_v2(ghes))
		unmap_gen_v2(ghes);
err_free:
	kfree(ghes);
	return ERR_PTR(rc);
}

static void ghes_fini(struct ghes *ghes)
{
	kfree(ghes->estatus);
	apei_unmap_generic_address(&ghes->generic->error_status_address);
	if (is_hest_type_generic_v2(ghes))
		unmap_gen_v2(ghes);
}

static inline int ghes_severity(int severity)
{
	switch (severity) {
	case CPER_SEV_INFORMATIONAL:
		return GHES_SEV_NO;
	case CPER_SEV_CORRECTED:
		return GHES_SEV_CORRECTED;
	case CPER_SEV_RECOVERABLE:
		return GHES_SEV_RECOVERABLE;
	case CPER_SEV_FATAL:
		return GHES_SEV_PANIC;
	default:
		/* Unknown, go panic */
		return GHES_SEV_PANIC;
	}
}

static void ghes_copy_tofrom_phys(void *buffer, u64 paddr, u32 len,
				  int from_phys,
				  enum fixed_addresses fixmap_idx)
{
	void __iomem *vaddr;
	u64 offset;
	u32 trunk;

	while (len > 0) {
		offset = paddr - (paddr & PAGE_MASK);
		vaddr = ghes_map(PHYS_PFN(paddr), fixmap_idx);
		trunk = PAGE_SIZE - offset;
		trunk = min(trunk, len);
		if (from_phys)
			memcpy_fromio(buffer, vaddr + offset, trunk);
		else
			memcpy_toio(vaddr + offset, buffer, trunk);
		len -= trunk;
		paddr += trunk;
		buffer += trunk;
		ghes_unmap(vaddr, fixmap_idx);
	}
}

/* Check the top-level record header has an appropriate size. */
static int __ghes_check_estatus(struct ghes *ghes,
				struct acpi_hest_generic_status *estatus)
{
	u32 len = cper_estatus_len(estatus);

	if (len < sizeof(*estatus)) {
		pr_warn_ratelimited(FW_WARN GHES_PFX "Truncated error status block!\n");
		return -EIO;
	}

	if (len > ghes->generic->error_block_length) {
		pr_warn_ratelimited(FW_WARN GHES_PFX "Invalid error status block length!\n");
		return -EIO;
	}

	if (cper_estatus_check_header(estatus)) {
		pr_warn_ratelimited(FW_WARN GHES_PFX "Invalid CPER header!\n");
		return -EIO;
	}

	return 0;
}

/* Read the CPER block, returning its address, and header in estatus. */
static int __ghes_peek_estatus(struct ghes *ghes,
			       struct acpi_hest_generic_status *estatus,
			       u64 *buf_paddr, enum fixed_addresses fixmap_idx)
{
	struct acpi_hest_generic *g = ghes->generic;
	int rc;

	rc = apei_read(buf_paddr, &g->error_status_address);
	if (rc) {
		*buf_paddr = 0;
		pr_warn_ratelimited(FW_WARN GHES_PFX
"Failed to read error status block address for hardware error source: %d.\n",
				   g->header.source_id);
		return -EIO;
	}
	if (!*buf_paddr)
		return -ENOENT;

	ghes_copy_tofrom_phys(estatus, *buf_paddr, sizeof(*estatus), 1,
			      fixmap_idx);
	if (!estatus->block_status) {
		*buf_paddr = 0;
		return -ENOENT;
	}

	return 0;
}

static int __ghes_read_estatus(struct acpi_hest_generic_status *estatus,
			       u64 buf_paddr, enum fixed_addresses fixmap_idx,
			       size_t buf_len)
{
	ghes_copy_tofrom_phys(estatus, buf_paddr, buf_len, 1, fixmap_idx);
	if (cper_estatus_check(estatus)) {
		pr_warn_ratelimited(FW_WARN GHES_PFX
				    "Failed to read error status block!\n");
		return -EIO;
	}

	return 0;
}

static int ghes_read_estatus(struct ghes *ghes,
			     struct acpi_hest_generic_status *estatus,
			     u64 *buf_paddr, enum fixed_addresses fixmap_idx)
{
	int rc;

	rc = __ghes_peek_estatus(ghes, estatus, buf_paddr, fixmap_idx);
	if (rc)
		return rc;

	rc = __ghes_check_estatus(ghes, estatus);
	if (rc)
		return rc;

	return __ghes_read_estatus(estatus, *buf_paddr, fixmap_idx,
				   cper_estatus_len(estatus));
}

static void ghes_clear_estatus(struct ghes *ghes,
			       struct acpi_hest_generic_status *estatus,
			       u64 buf_paddr, enum fixed_addresses fixmap_idx)
{
	estatus->block_status = 0;

	if (!buf_paddr)
		return;

	ghes_copy_tofrom_phys(estatus, buf_paddr,
			      sizeof(estatus->block_status), 0,
			      fixmap_idx);

	/*
	 * GHESv2 type HEST entries introduce support for error acknowledgment,
	 * so only acknowledge the error if this support is present.
	 */
	if (is_hest_type_generic_v2(ghes))
		ghes_ack_error(ghes->generic_v2);
}

/*
 * Called as task_work before returning to user-space.
 * Ensure any queued work has been done before we return to the context that
 * triggered the notification.
 */
static void ghes_kick_task_work(struct callback_head *head)
{
	struct acpi_hest_generic_status *estatus;
	struct ghes_estatus_node *estatus_node;
	u32 node_len;

	estatus_node = container_of(head, struct ghes_estatus_node, task_work);
	if (IS_ENABLED(CONFIG_ACPI_APEI_MEMORY_FAILURE))
		memory_failure_queue_kick(estatus_node->task_work_cpu);

	estatus = GHES_ESTATUS_FROM_NODE(estatus_node);
	node_len = GHES_ESTATUS_NODE_LEN(cper_estatus_len(estatus));
	gen_pool_free(ghes_estatus_pool, (unsigned long)estatus_node, node_len);
}

static bool ghes_do_memory_failure(u64 physical_addr, int flags)
{
	unsigned long pfn;

	if (!IS_ENABLED(CONFIG_ACPI_APEI_MEMORY_FAILURE))
		return false;

	pfn = PHYS_PFN(physical_addr);
	if (!pfn_valid(pfn) && !arch_is_platform_page(physical_addr)) {
		pr_warn_ratelimited(FW_WARN GHES_PFX
		"Invalid address in generic error data: %#llx\n",
		physical_addr);
		return false;
	}

	memory_failure_queue(pfn, flags);
	return true;
}

static bool ghes_handle_memory_failure(struct acpi_hest_generic_data *gdata,
				       int sev)
{
	int flags = -1;
	int sec_sev = ghes_severity(gdata->error_severity);
	struct cper_sec_mem_err *mem_err = acpi_hest_get_payload(gdata);

	if (!(mem_err->validation_bits & CPER_MEM_VALID_PA))
		return false;

	/* iff following two events can be handled properly by now */
	if (sec_sev == GHES_SEV_CORRECTED &&
	    (gdata->flags & CPER_SEC_ERROR_THRESHOLD_EXCEEDED))
		flags = MF_SOFT_OFFLINE;
	if (sev == GHES_SEV_RECOVERABLE && sec_sev == GHES_SEV_RECOVERABLE)
		flags = 0;

	if (flags != -1)
		return ghes_do_memory_failure(mem_err->physical_addr, flags);

	return false;
}

static bool ghes_handle_arm_hw_error(struct acpi_hest_generic_data *gdata, int sev)
{
	struct cper_sec_proc_arm *err = acpi_hest_get_payload(gdata);
	bool queued = false;
	int sec_sev, i;
	char *p;

	log_arm_hw_error(err);

	sec_sev = ghes_severity(gdata->error_severity);
	if (sev != GHES_SEV_RECOVERABLE || sec_sev != GHES_SEV_RECOVERABLE)
		return false;

	p = (char *)(err + 1);
	for (i = 0; i < err->err_info_num; i++) {
		struct cper_arm_err_info *err_info = (struct cper_arm_err_info *)p;
		bool is_cache = (err_info->type == CPER_ARM_CACHE_ERROR);
		bool has_pa = (err_info->validation_bits & CPER_ARM_INFO_VALID_PHYSICAL_ADDR);
		const char *error_type = "unknown error";

		/*
		 * The field (err_info->error_info & BIT(26)) is fixed to set to
		 * 1 in some old firmware of HiSilicon Kunpeng920. We assume that
		 * firmware won't mix corrected errors in an uncorrected section,
		 * and don't filter out 'corrected' error here.
		 */
		if (is_cache && has_pa) {
			queued = ghes_do_memory_failure(err_info->physical_fault_addr, 0);
			p += err_info->length;
			continue;
		}

		if (err_info->type < ARRAY_SIZE(cper_proc_error_type_strs))
			error_type = cper_proc_error_type_strs[err_info->type];

		pr_warn_ratelimited(FW_WARN GHES_PFX
				    "Unhandled processor error type: %s\n",
				    error_type);
		p += err_info->length;
	}

	return queued;
}

/*
 * PCIe AER errors need to be sent to the AER driver for reporting and
 * recovery. The GHES severities map to the following AER severities and
 * require the following handling:
 *
 * GHES_SEV_CORRECTABLE -> AER_CORRECTABLE
 *     These need to be reported by the AER driver but no recovery is
 *     necessary.
 * GHES_SEV_RECOVERABLE -> AER_NONFATAL
 * GHES_SEV_RECOVERABLE && CPER_SEC_RESET -> AER_FATAL
 *     These both need to be reported and recovered from by the AER driver.
 * GHES_SEV_PANIC does not make it to this handling since the kernel must
 *     panic.
 */
static void ghes_handle_aer(struct acpi_hest_generic_data *gdata)
{
#ifdef CONFIG_ACPI_APEI_PCIEAER
	struct cper_sec_pcie *pcie_err = acpi_hest_get_payload(gdata);

	if (pcie_err->validation_bits & CPER_PCIE_VALID_DEVICE_ID &&
	    pcie_err->validation_bits & CPER_PCIE_VALID_AER_INFO) {
		unsigned int devfn;
		int aer_severity;

		devfn = PCI_DEVFN(pcie_err->device_id.device,
				  pcie_err->device_id.function);
		aer_severity = cper_severity_to_aer(gdata->error_severity);

		/*
		 * If firmware reset the component to contain
		 * the error, we must reinitialize it before
		 * use, so treat it as a fatal AER error.
		 */
		if (gdata->flags & CPER_SEC_RESET)
			aer_severity = AER_FATAL;

		aer_recover_queue(pcie_err->device_id.segment,
				  pcie_err->device_id.bus,
				  devfn, aer_severity,
				  (struct aer_capability_regs *)
				  pcie_err->aer_info);
	}
#endif
}

static BLOCKING_NOTIFIER_HEAD(vendor_record_notify_list);

int ghes_register_vendor_record_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&vendor_record_notify_list, nb);
}
EXPORT_SYMBOL_GPL(ghes_register_vendor_record_notifier);

void ghes_unregister_vendor_record_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&vendor_record_notify_list, nb);
}
EXPORT_SYMBOL_GPL(ghes_unregister_vendor_record_notifier);

static void ghes_vendor_record_work_func(struct work_struct *work)
{
	struct ghes_vendor_record_entry *entry;
	struct acpi_hest_generic_data *gdata;
	u32 len;

	entry = container_of(work, struct ghes_vendor_record_entry, work);
	gdata = GHES_GDATA_FROM_VENDOR_ENTRY(entry);

	blocking_notifier_call_chain(&vendor_record_notify_list,
				     entry->error_severity, gdata);

	len = GHES_VENDOR_ENTRY_LEN(acpi_hest_get_record_size(gdata));
	gen_pool_free(ghes_estatus_pool, (unsigned long)entry, len);
}

static void ghes_defer_non_standard_event(struct acpi_hest_generic_data *gdata,
					  int sev)
{
	struct acpi_hest_generic_data *copied_gdata;
	struct ghes_vendor_record_entry *entry;
	u32 len;

	len = GHES_VENDOR_ENTRY_LEN(acpi_hest_get_record_size(gdata));
	entry = (void *)gen_pool_alloc(ghes_estatus_pool, len);
	if (!entry)
		return;

	copied_gdata = GHES_GDATA_FROM_VENDOR_ENTRY(entry);
	memcpy(copied_gdata, gdata, acpi_hest_get_record_size(gdata));
	entry->error_severity = sev;

	INIT_WORK(&entry->work, ghes_vendor_record_work_func);
	schedule_work(&entry->work);
}

static bool ghes_do_proc(struct ghes *ghes,
			 const struct acpi_hest_generic_status *estatus)
{
	int sev, sec_sev;
	struct acpi_hest_generic_data *gdata;
	guid_t *sec_type;
	const guid_t *fru_id = &guid_null;
	char *fru_text = "";
	bool queued = false;

	sev = ghes_severity(estatus->error_severity);
	apei_estatus_for_each_section(estatus, gdata) {
		sec_type = (guid_t *)gdata->section_type;
		sec_sev = ghes_severity(gdata->error_severity);
		if (gdata->validation_bits & CPER_SEC_VALID_FRU_ID)
			fru_id = (guid_t *)gdata->fru_id;

		if (gdata->validation_bits & CPER_SEC_VALID_FRU_TEXT)
			fru_text = gdata->fru_text;

		if (guid_equal(sec_type, &CPER_SEC_PLATFORM_MEM)) {
			struct cper_sec_mem_err *mem_err = acpi_hest_get_payload(gdata);

			atomic_notifier_call_chain(&ghes_report_chain, sev, mem_err);

			arch_apei_report_mem_error(sev, mem_err);
			queued = ghes_handle_memory_failure(gdata, sev);
		}
		else if (guid_equal(sec_type, &CPER_SEC_PCIE)) {
			ghes_handle_aer(gdata);
		}
		else if (guid_equal(sec_type, &CPER_SEC_PROC_ARM)) {
			queued = ghes_handle_arm_hw_error(gdata, sev);
		} else {
			void *err = acpi_hest_get_payload(gdata);

			ghes_defer_non_standard_event(gdata, sev);
			log_non_standard_event(sec_type, fru_id, fru_text,
					       sec_sev, err,
					       gdata->error_data_length);
		}
	}

	return queued;
}

static void __ghes_print_estatus(const char *pfx,
				 const struct acpi_hest_generic *generic,
				 const struct acpi_hest_generic_status *estatus)
{
	static atomic_t seqno;
	unsigned int curr_seqno;
	char pfx_seq[64];

	if (pfx == NULL) {
		if (ghes_severity(estatus->error_severity) <=
		    GHES_SEV_CORRECTED)
			pfx = KERN_WARNING;
		else
			pfx = KERN_ERR;
	}
	curr_seqno = atomic_inc_return(&seqno);
	snprintf(pfx_seq, sizeof(pfx_seq), "%s{%u}" HW_ERR, pfx, curr_seqno);
	printk("%s""Hardware error from APEI Generic Hardware Error Source: %d\n",
	       pfx_seq, generic->header.source_id);
	cper_estatus_print(pfx_seq, estatus);
}

static int ghes_print_estatus(const char *pfx,
			      const struct acpi_hest_generic *generic,
			      const struct acpi_hest_generic_status *estatus)
{
	/* Not more than 2 messages every 5 seconds */
	static DEFINE_RATELIMIT_STATE(ratelimit_corrected, 5*HZ, 2);
	static DEFINE_RATELIMIT_STATE(ratelimit_uncorrected, 5*HZ, 2);
	struct ratelimit_state *ratelimit;

	if (ghes_severity(estatus->error_severity) <= GHES_SEV_CORRECTED)
		ratelimit = &ratelimit_corrected;
	else
		ratelimit = &ratelimit_uncorrected;
	if (__ratelimit(ratelimit)) {
		__ghes_print_estatus(pfx, generic, estatus);
		return 1;
	}
	return 0;
}

/*
 * GHES error status reporting throttle, to report more kinds of
 * errors, instead of just most frequently occurred errors.
 */
static int ghes_estatus_cached(struct acpi_hest_generic_status *estatus)
{
	u32 len;
	int i, cached = 0;
	unsigned long long now;
	struct ghes_estatus_cache *cache;
	struct acpi_hest_generic_status *cache_estatus;

	len = cper_estatus_len(estatus);
	rcu_read_lock();
	for (i = 0; i < GHES_ESTATUS_CACHES_SIZE; i++) {
		cache = rcu_dereference(ghes_estatus_caches[i]);
		if (cache == NULL)
			continue;
		if (len != cache->estatus_len)
			continue;
		cache_estatus = GHES_ESTATUS_FROM_CACHE(cache);
		if (memcmp(estatus, cache_estatus, len))
			continue;
		atomic_inc(&cache->count);
		now = sched_clock();
		if (now - cache->time_in < GHES_ESTATUS_IN_CACHE_MAX_NSEC)
			cached = 1;
		break;
	}
	rcu_read_unlock();
	return cached;
}

static struct ghes_estatus_cache *ghes_estatus_cache_alloc(
	struct acpi_hest_generic *generic,
	struct acpi_hest_generic_status *estatus)
{
	int alloced;
	u32 len, cache_len;
	struct ghes_estatus_cache *cache;
	struct acpi_hest_generic_status *cache_estatus;

	alloced = atomic_add_return(1, &ghes_estatus_cache_alloced);
	if (alloced > GHES_ESTATUS_CACHE_ALLOCED_MAX) {
		atomic_dec(&ghes_estatus_cache_alloced);
		return NULL;
	}
	len = cper_estatus_len(estatus);
	cache_len = GHES_ESTATUS_CACHE_LEN(len);
	cache = (void *)gen_pool_alloc(ghes_estatus_pool, cache_len);
	if (!cache) {
		atomic_dec(&ghes_estatus_cache_alloced);
		return NULL;
	}
	cache_estatus = GHES_ESTATUS_FROM_CACHE(cache);
	memcpy(cache_estatus, estatus, len);
	cache->estatus_len = len;
	atomic_set(&cache->count, 0);
	cache->generic = generic;
	cache->time_in = sched_clock();
	return cache;
}

static void ghes_estatus_cache_rcu_free(struct rcu_head *head)
{
	struct ghes_estatus_cache *cache;
	u32 len;

	cache = container_of(head, struct ghes_estatus_cache, rcu);
	len = cper_estatus_len(GHES_ESTATUS_FROM_CACHE(cache));
	len = GHES_ESTATUS_CACHE_LEN(len);
	gen_pool_free(ghes_estatus_pool, (unsigned long)cache, len);
	atomic_dec(&ghes_estatus_cache_alloced);
}

static void
ghes_estatus_cache_add(struct acpi_hest_generic *generic,
		       struct acpi_hest_generic_status *estatus)
{
	unsigned long long now, duration, period, max_period = 0;
	struct ghes_estatus_cache *cache, *new_cache;
	struct ghes_estatus_cache __rcu *victim;
	int i, slot = -1, count;

	new_cache = ghes_estatus_cache_alloc(generic, estatus);
	if (!new_cache)
		return;

	rcu_read_lock();
	now = sched_clock();
	for (i = 0; i < GHES_ESTATUS_CACHES_SIZE; i++) {
		cache = rcu_dereference(ghes_estatus_caches[i]);
		if (cache == NULL) {
			slot = i;
			break;
		}
		duration = now - cache->time_in;
		if (duration >= GHES_ESTATUS_IN_CACHE_MAX_NSEC) {
			slot = i;
			break;
		}
		count = atomic_read(&cache->count);
		period = duration;
		do_div(period, (count + 1));
		if (period > max_period) {
			max_period = period;
			slot = i;
		}
	}
	rcu_read_unlock();

	if (slot != -1) {
		/*
		 * Use release semantics to ensure that ghes_estatus_cached()
		 * running on another CPU will see the updated cache fields if
		 * it can see the new value of the pointer.
		 */
		victim = xchg_release(&ghes_estatus_caches[slot],
				      RCU_INITIALIZER(new_cache));

		/*
		 * At this point, victim may point to a cached item different
		 * from the one based on which we selected the slot. Instead of
		 * going to the loop again to pick another slot, let's just
		 * drop the other item anyway: this may cause a false cache
		 * miss later on, but that won't cause any problems.
		 */
		if (victim)
			call_rcu(&unrcu_pointer(victim)->rcu,
				 ghes_estatus_cache_rcu_free);
	}
}

static void __ghes_panic(struct ghes *ghes,
			 struct acpi_hest_generic_status *estatus,
			 u64 buf_paddr, enum fixed_addresses fixmap_idx)
{
	__ghes_print_estatus(KERN_EMERG, ghes->generic, estatus);

	ghes_clear_estatus(ghes, estatus, buf_paddr, fixmap_idx);

	/* reboot to log the error! */
	if (!panic_timeout)
		panic_timeout = ghes_panic_timeout;
	panic("Fatal hardware error!");
}

static int ghes_proc(struct ghes *ghes)
{
	struct acpi_hest_generic_status *estatus = ghes->estatus;
	u64 buf_paddr;
	int rc;

	rc = ghes_read_estatus(ghes, estatus, &buf_paddr, FIX_APEI_GHES_IRQ);
	if (rc)
		goto out;

	if (ghes_severity(estatus->error_severity) >= GHES_SEV_PANIC)
		__ghes_panic(ghes, estatus, buf_paddr, FIX_APEI_GHES_IRQ);

	if (!ghes_estatus_cached(estatus)) {
		if (ghes_print_estatus(NULL, ghes->generic, estatus))
			ghes_estatus_cache_add(ghes->generic, estatus);
	}
	ghes_do_proc(ghes, estatus);

out:
	ghes_clear_estatus(ghes, estatus, buf_paddr, FIX_APEI_GHES_IRQ);

	return rc;
}

static void ghes_add_timer(struct ghes *ghes)
{
	struct acpi_hest_generic *g = ghes->generic;
	unsigned long expire;

	if (!g->notify.poll_interval) {
		pr_warn(FW_WARN GHES_PFX "Poll interval is 0 for generic hardware error source: %d, disabled.\n",
			g->header.source_id);
		return;
	}
	expire = jiffies + msecs_to_jiffies(g->notify.poll_interval);
	ghes->timer.expires = round_jiffies_relative(expire);
	add_timer(&ghes->timer);
}

static void ghes_poll_func(struct timer_list *t)
{
	struct ghes *ghes = from_timer(ghes, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&ghes_notify_lock_irq, flags);
	ghes_proc(ghes);
	spin_unlock_irqrestore(&ghes_notify_lock_irq, flags);
	if (!(ghes->flags & GHES_EXITING))
		ghes_add_timer(ghes);
}

static irqreturn_t ghes_irq_func(int irq, void *data)
{
	struct ghes *ghes = data;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&ghes_notify_lock_irq, flags);
	rc = ghes_proc(ghes);
	spin_unlock_irqrestore(&ghes_notify_lock_irq, flags);
	if (rc)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int ghes_notify_hed(struct notifier_block *this, unsigned long event,
			   void *data)
{
	struct ghes *ghes;
	unsigned long flags;
	int ret = NOTIFY_DONE;

	spin_lock_irqsave(&ghes_notify_lock_irq, flags);
	rcu_read_lock();
	list_for_each_entry_rcu(ghes, &ghes_hed, list) {
		if (!ghes_proc(ghes))
			ret = NOTIFY_OK;
	}
	rcu_read_unlock();
	spin_unlock_irqrestore(&ghes_notify_lock_irq, flags);

	return ret;
}

static struct notifier_block ghes_notifier_hed = {
	.notifier_call = ghes_notify_hed,
};

/*
 * Handlers for CPER records may not be NMI safe. For example,
 * memory_failure_queue() takes spinlocks and calls schedule_work_on().
 * In any NMI-like handler, memory from ghes_estatus_pool is used to save
 * estatus, and added to the ghes_estatus_llist. irq_work_queue() causes
 * ghes_proc_in_irq() to run in IRQ context where each estatus in
 * ghes_estatus_llist is processed.
 *
 * Memory from the ghes_estatus_pool is also used with the ghes_estatus_cache
 * to suppress frequent messages.
 */
static struct llist_head ghes_estatus_llist;
static struct irq_work ghes_proc_irq_work;

static void ghes_proc_in_irq(struct irq_work *irq_work)
{
	struct llist_node *llnode, *next;
	struct ghes_estatus_node *estatus_node;
	struct acpi_hest_generic *generic;
	struct acpi_hest_generic_status *estatus;
	bool task_work_pending;
	u32 len, node_len;
	int ret;

	llnode = llist_del_all(&ghes_estatus_llist);
	/*
	 * Because the time order of estatus in list is reversed,
	 * revert it back to proper order.
	 */
	llnode = llist_reverse_order(llnode);
	while (llnode) {
		next = llnode->next;
		estatus_node = llist_entry(llnode, struct ghes_estatus_node,
					   llnode);
		estatus = GHES_ESTATUS_FROM_NODE(estatus_node);
		len = cper_estatus_len(estatus);
		node_len = GHES_ESTATUS_NODE_LEN(len);
		task_work_pending = ghes_do_proc(estatus_node->ghes, estatus);
		if (!ghes_estatus_cached(estatus)) {
			generic = estatus_node->generic;
			if (ghes_print_estatus(NULL, generic, estatus))
				ghes_estatus_cache_add(generic, estatus);
		}

		if (task_work_pending && current->mm) {
			estatus_node->task_work.func = ghes_kick_task_work;
			estatus_node->task_work_cpu = smp_processor_id();
			ret = task_work_add(current, &estatus_node->task_work,
					    TWA_RESUME);
			if (ret)
				estatus_node->task_work.func = NULL;
		}

		if (!estatus_node->task_work.func)
			gen_pool_free(ghes_estatus_pool,
				      (unsigned long)estatus_node, node_len);

		llnode = next;
	}
}

static void ghes_print_queued_estatus(void)
{
	struct llist_node *llnode;
	struct ghes_estatus_node *estatus_node;
	struct acpi_hest_generic *generic;
	struct acpi_hest_generic_status *estatus;

	llnode = llist_del_all(&ghes_estatus_llist);
	/*
	 * Because the time order of estatus in list is reversed,
	 * revert it back to proper order.
	 */
	llnode = llist_reverse_order(llnode);
	while (llnode) {
		estatus_node = llist_entry(llnode, struct ghes_estatus_node,
					   llnode);
		estatus = GHES_ESTATUS_FROM_NODE(estatus_node);
		generic = estatus_node->generic;
		ghes_print_estatus(NULL, generic, estatus);
		llnode = llnode->next;
	}
}

static int ghes_in_nmi_queue_one_entry(struct ghes *ghes,
				       enum fixed_addresses fixmap_idx)
{
	struct acpi_hest_generic_status *estatus, tmp_header;
	struct ghes_estatus_node *estatus_node;
	u32 len, node_len;
	u64 buf_paddr;
	int sev, rc;

	if (!IS_ENABLED(CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG))
		return -EOPNOTSUPP;

	rc = __ghes_peek_estatus(ghes, &tmp_header, &buf_paddr, fixmap_idx);
	if (rc) {
		ghes_clear_estatus(ghes, &tmp_header, buf_paddr, fixmap_idx);
		return rc;
	}

	rc = __ghes_check_estatus(ghes, &tmp_header);
	if (rc) {
		ghes_clear_estatus(ghes, &tmp_header, buf_paddr, fixmap_idx);
		return rc;
	}

	len = cper_estatus_len(&tmp_header);
	node_len = GHES_ESTATUS_NODE_LEN(len);
	estatus_node = (void *)gen_pool_alloc(ghes_estatus_pool, node_len);
	if (!estatus_node)
		return -ENOMEM;

	estatus_node->ghes = ghes;
	estatus_node->generic = ghes->generic;
	estatus_node->task_work.func = NULL;
	estatus = GHES_ESTATUS_FROM_NODE(estatus_node);

	if (__ghes_read_estatus(estatus, buf_paddr, fixmap_idx, len)) {
		ghes_clear_estatus(ghes, estatus, buf_paddr, fixmap_idx);
		rc = -ENOENT;
		goto no_work;
	}

	sev = ghes_severity(estatus->error_severity);
	if (sev >= GHES_SEV_PANIC) {
		ghes_print_queued_estatus();
		__ghes_panic(ghes, estatus, buf_paddr, fixmap_idx);
	}

	ghes_clear_estatus(ghes, &tmp_header, buf_paddr, fixmap_idx);

	/* This error has been reported before, don't process it again. */
	if (ghes_estatus_cached(estatus))
		goto no_work;

	llist_add(&estatus_node->llnode, &ghes_estatus_llist);

	return rc;

no_work:
	gen_pool_free(ghes_estatus_pool, (unsigned long)estatus_node,
		      node_len);

	return rc;
}

static int ghes_in_nmi_spool_from_list(struct list_head *rcu_list,
				       enum fixed_addresses fixmap_idx)
{
	int ret = -ENOENT;
	struct ghes *ghes;

	rcu_read_lock();
	list_for_each_entry_rcu(ghes, rcu_list, list) {
		if (!ghes_in_nmi_queue_one_entry(ghes, fixmap_idx))
			ret = 0;
	}
	rcu_read_unlock();

	if (IS_ENABLED(CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG) && !ret)
		irq_work_queue(&ghes_proc_irq_work);

	return ret;
}

#ifdef CONFIG_ACPI_APEI_SEA
static LIST_HEAD(ghes_sea);

/*
 * Return 0 only if one of the SEA error sources successfully reported an error
 * record sent from the firmware.
 */
int ghes_notify_sea(void)
{
	static DEFINE_RAW_SPINLOCK(ghes_notify_lock_sea);
	int rv;

	raw_spin_lock(&ghes_notify_lock_sea);
	rv = ghes_in_nmi_spool_from_list(&ghes_sea, FIX_APEI_GHES_SEA);
	raw_spin_unlock(&ghes_notify_lock_sea);

	return rv;
}

static void ghes_sea_add(struct ghes *ghes)
{
	mutex_lock(&ghes_list_mutex);
	list_add_rcu(&ghes->list, &ghes_sea);
	mutex_unlock(&ghes_list_mutex);
}

static void ghes_sea_remove(struct ghes *ghes)
{
	mutex_lock(&ghes_list_mutex);
	list_del_rcu(&ghes->list);
	mutex_unlock(&ghes_list_mutex);
	synchronize_rcu();
}
#else /* CONFIG_ACPI_APEI_SEA */
static inline void ghes_sea_add(struct ghes *ghes) { }
static inline void ghes_sea_remove(struct ghes *ghes) { }
#endif /* CONFIG_ACPI_APEI_SEA */

#ifdef CONFIG_HAVE_ACPI_APEI_NMI
/*
 * NMI may be triggered on any CPU, so ghes_in_nmi is used for
 * having only one concurrent reader.
 */
static atomic_t ghes_in_nmi = ATOMIC_INIT(0);

static LIST_HEAD(ghes_nmi);

static int ghes_notify_nmi(unsigned int cmd, struct pt_regs *regs)
{
	static DEFINE_RAW_SPINLOCK(ghes_notify_lock_nmi);
	int ret = NMI_DONE;

	if (!atomic_add_unless(&ghes_in_nmi, 1, 1))
		return ret;

	raw_spin_lock(&ghes_notify_lock_nmi);
	if (!ghes_in_nmi_spool_from_list(&ghes_nmi, FIX_APEI_GHES_NMI))
		ret = NMI_HANDLED;
	raw_spin_unlock(&ghes_notify_lock_nmi);

	atomic_dec(&ghes_in_nmi);
	return ret;
}

static void ghes_nmi_add(struct ghes *ghes)
{
	mutex_lock(&ghes_list_mutex);
	if (list_empty(&ghes_nmi))
		register_nmi_handler(NMI_LOCAL, ghes_notify_nmi, 0, "ghes");
	list_add_rcu(&ghes->list, &ghes_nmi);
	mutex_unlock(&ghes_list_mutex);
}

static void ghes_nmi_remove(struct ghes *ghes)
{
	mutex_lock(&ghes_list_mutex);
	list_del_rcu(&ghes->list);
	if (list_empty(&ghes_nmi))
		unregister_nmi_handler(NMI_LOCAL, "ghes");
	mutex_unlock(&ghes_list_mutex);
	/*
	 * To synchronize with NMI handler, ghes can only be
	 * freed after NMI handler finishes.
	 */
	synchronize_rcu();
}
#else /* CONFIG_HAVE_ACPI_APEI_NMI */
static inline void ghes_nmi_add(struct ghes *ghes) { }
static inline void ghes_nmi_remove(struct ghes *ghes) { }
#endif /* CONFIG_HAVE_ACPI_APEI_NMI */

static void ghes_nmi_init_cxt(void)
{
	init_irq_work(&ghes_proc_irq_work, ghes_proc_in_irq);
}

static int __ghes_sdei_callback(struct ghes *ghes,
				enum fixed_addresses fixmap_idx)
{
	if (!ghes_in_nmi_queue_one_entry(ghes, fixmap_idx)) {
		irq_work_queue(&ghes_proc_irq_work);

		return 0;
	}

	return -ENOENT;
}

static int ghes_sdei_normal_callback(u32 event_num, struct pt_regs *regs,
				      void *arg)
{
	static DEFINE_RAW_SPINLOCK(ghes_notify_lock_sdei_normal);
	struct ghes *ghes = arg;
	int err;

	raw_spin_lock(&ghes_notify_lock_sdei_normal);
	err = __ghes_sdei_callback(ghes, FIX_APEI_GHES_SDEI_NORMAL);
	raw_spin_unlock(&ghes_notify_lock_sdei_normal);

	return err;
}

static int ghes_sdei_critical_callback(u32 event_num, struct pt_regs *regs,
				       void *arg)
{
	static DEFINE_RAW_SPINLOCK(ghes_notify_lock_sdei_critical);
	struct ghes *ghes = arg;
	int err;

	raw_spin_lock(&ghes_notify_lock_sdei_critical);
	err = __ghes_sdei_callback(ghes, FIX_APEI_GHES_SDEI_CRITICAL);
	raw_spin_unlock(&ghes_notify_lock_sdei_critical);

	return err;
}

static int apei_sdei_register_ghes(struct ghes *ghes)
{
	if (!IS_ENABLED(CONFIG_ARM_SDE_INTERFACE))
		return -EOPNOTSUPP;

	return sdei_register_ghes(ghes, ghes_sdei_normal_callback,
				 ghes_sdei_critical_callback);
}

static int apei_sdei_unregister_ghes(struct ghes *ghes)
{
	if (!IS_ENABLED(CONFIG_ARM_SDE_INTERFACE))
		return -EOPNOTSUPP;

	return sdei_unregister_ghes(ghes);
}

static int ghes_probe(struct platform_device *ghes_dev)
{
	struct acpi_hest_generic *generic;
	struct ghes *ghes = NULL;
	unsigned long flags;

	int rc = -EINVAL;

	generic = *(struct acpi_hest_generic **)ghes_dev->dev.platform_data;
	if (!generic->enabled)
		return -ENODEV;

	switch (generic->notify.type) {
	case ACPI_HEST_NOTIFY_POLLED:
	case ACPI_HEST_NOTIFY_EXTERNAL:
	case ACPI_HEST_NOTIFY_SCI:
	case ACPI_HEST_NOTIFY_GSIV:
	case ACPI_HEST_NOTIFY_GPIO:
		break;

	case ACPI_HEST_NOTIFY_SEA:
		if (!IS_ENABLED(CONFIG_ACPI_APEI_SEA)) {
			pr_warn(GHES_PFX "Generic hardware error source: %d notified via SEA is not supported\n",
				generic->header.source_id);
			rc = -ENOTSUPP;
			goto err;
		}
		break;
	case ACPI_HEST_NOTIFY_NMI:
		if (!IS_ENABLED(CONFIG_HAVE_ACPI_APEI_NMI)) {
			pr_warn(GHES_PFX "Generic hardware error source: %d notified via NMI interrupt is not supported!\n",
				generic->header.source_id);
			goto err;
		}
		break;
	case ACPI_HEST_NOTIFY_SOFTWARE_DELEGATED:
		if (!IS_ENABLED(CONFIG_ARM_SDE_INTERFACE)) {
			pr_warn(GHES_PFX "Generic hardware error source: %d notified via SDE Interface is not supported!\n",
				generic->header.source_id);
			goto err;
		}
		break;
	case ACPI_HEST_NOTIFY_LOCAL:
		pr_warn(GHES_PFX "Generic hardware error source: %d notified via local interrupt is not supported!\n",
			generic->header.source_id);
		goto err;
	default:
		pr_warn(FW_WARN GHES_PFX "Unknown notification type: %u for generic hardware error source: %d\n",
			generic->notify.type, generic->header.source_id);
		goto err;
	}

	rc = -EIO;
	if (generic->error_block_length <
	    sizeof(struct acpi_hest_generic_status)) {
		pr_warn(FW_BUG GHES_PFX "Invalid error block length: %u for generic hardware error source: %d\n",
			generic->error_block_length, generic->header.source_id);
		goto err;
	}
	ghes = ghes_new(generic);
	if (IS_ERR(ghes)) {
		rc = PTR_ERR(ghes);
		ghes = NULL;
		goto err;
	}

	switch (generic->notify.type) {
	case ACPI_HEST_NOTIFY_POLLED:
		timer_setup(&ghes->timer, ghes_poll_func, 0);
		ghes_add_timer(ghes);
		break;
	case ACPI_HEST_NOTIFY_EXTERNAL:
		/* External interrupt vector is GSI */
		rc = acpi_gsi_to_irq(generic->notify.vector, &ghes->irq);
		if (rc) {
			pr_err(GHES_PFX "Failed to map GSI to IRQ for generic hardware error source: %d\n",
			       generic->header.source_id);
			goto err;
		}
		rc = request_irq(ghes->irq, ghes_irq_func, IRQF_SHARED,
				 "GHES IRQ", ghes);
		if (rc) {
			pr_err(GHES_PFX "Failed to register IRQ for generic hardware error source: %d\n",
			       generic->header.source_id);
			goto err;
		}
		break;

	case ACPI_HEST_NOTIFY_SCI:
	case ACPI_HEST_NOTIFY_GSIV:
	case ACPI_HEST_NOTIFY_GPIO:
		mutex_lock(&ghes_list_mutex);
		if (list_empty(&ghes_hed))
			register_acpi_hed_notifier(&ghes_notifier_hed);
		list_add_rcu(&ghes->list, &ghes_hed);
		mutex_unlock(&ghes_list_mutex);
		break;

	case ACPI_HEST_NOTIFY_SEA:
		ghes_sea_add(ghes);
		break;
	case ACPI_HEST_NOTIFY_NMI:
		ghes_nmi_add(ghes);
		break;
	case ACPI_HEST_NOTIFY_SOFTWARE_DELEGATED:
		rc = apei_sdei_register_ghes(ghes);
		if (rc)
			goto err;
		break;
	default:
		BUG();
	}

	platform_set_drvdata(ghes_dev, ghes);

	ghes->dev = &ghes_dev->dev;

	mutex_lock(&ghes_devs_mutex);
	list_add_tail(&ghes->elist, &ghes_devs);
	mutex_unlock(&ghes_devs_mutex);

	/* Handle any pending errors right away */
	spin_lock_irqsave(&ghes_notify_lock_irq, flags);
	ghes_proc(ghes);
	spin_unlock_irqrestore(&ghes_notify_lock_irq, flags);

	return 0;

err:
	if (ghes) {
		ghes_fini(ghes);
		kfree(ghes);
	}
	return rc;
}

static int ghes_remove(struct platform_device *ghes_dev)
{
	int rc;
	struct ghes *ghes;
	struct acpi_hest_generic *generic;

	ghes = platform_get_drvdata(ghes_dev);
	generic = ghes->generic;

	ghes->flags |= GHES_EXITING;
	switch (generic->notify.type) {
	case ACPI_HEST_NOTIFY_POLLED:
		timer_shutdown_sync(&ghes->timer);
		break;
	case ACPI_HEST_NOTIFY_EXTERNAL:
		free_irq(ghes->irq, ghes);
		break;

	case ACPI_HEST_NOTIFY_SCI:
	case ACPI_HEST_NOTIFY_GSIV:
	case ACPI_HEST_NOTIFY_GPIO:
		mutex_lock(&ghes_list_mutex);
		list_del_rcu(&ghes->list);
		if (list_empty(&ghes_hed))
			unregister_acpi_hed_notifier(&ghes_notifier_hed);
		mutex_unlock(&ghes_list_mutex);
		synchronize_rcu();
		break;

	case ACPI_HEST_NOTIFY_SEA:
		ghes_sea_remove(ghes);
		break;
	case ACPI_HEST_NOTIFY_NMI:
		ghes_nmi_remove(ghes);
		break;
	case ACPI_HEST_NOTIFY_SOFTWARE_DELEGATED:
		rc = apei_sdei_unregister_ghes(ghes);
		if (rc)
			return rc;
		break;
	default:
		BUG();
		break;
	}

	ghes_fini(ghes);

	mutex_lock(&ghes_devs_mutex);
	list_del(&ghes->elist);
	mutex_unlock(&ghes_devs_mutex);

	kfree(ghes);

	return 0;
}

static struct platform_driver ghes_platform_driver = {
	.driver		= {
		.name	= "GHES",
	},
	.probe		= ghes_probe,
	.remove		= ghes_remove,
};

void __init acpi_ghes_init(void)
{
	int rc;

	sdei_init();

	if (acpi_disabled)
		return;

	switch (hest_disable) {
	case HEST_NOT_FOUND:
		return;
	case HEST_DISABLED:
		pr_info(GHES_PFX "HEST is not enabled!\n");
		return;
	default:
		break;
	}

	if (ghes_disable) {
		pr_info(GHES_PFX "GHES is not enabled!\n");
		return;
	}

	ghes_nmi_init_cxt();

	rc = platform_driver_register(&ghes_platform_driver);
	if (rc)
		return;

	rc = apei_osc_setup();
	if (rc == 0 && osc_sb_apei_support_acked)
		pr_info(GHES_PFX "APEI firmware first mode is enabled by APEI bit and WHEA _OSC.\n");
	else if (rc == 0 && !osc_sb_apei_support_acked)
		pr_info(GHES_PFX "APEI firmware first mode is enabled by WHEA _OSC.\n");
	else if (rc && osc_sb_apei_support_acked)
		pr_info(GHES_PFX "APEI firmware first mode is enabled by APEI bit.\n");
	else
		pr_info(GHES_PFX "Failed to enable APEI firmware first mode.\n");
}

/*
 * Known x86 systems that prefer GHES error reporting:
 */
static struct acpi_platform_list plat_list[] = {
	{"HPE   ", "Server  ", 0, ACPI_SIG_FADT, all_versions},
	{ } /* End */
};

struct list_head *ghes_get_devices(void)
{
	int idx = -1;

	if (IS_ENABLED(CONFIG_X86)) {
		idx = acpi_match_platform_list(plat_list);
		if (idx < 0) {
			if (!ghes_edac_force_enable)
				return NULL;

			pr_warn_once("Force-loading ghes_edac on an unsupported platform. You're on your own!\n");
		}
	}

	return &ghes_devs;
}
EXPORT_SYMBOL_GPL(ghes_get_devices);

void ghes_register_report_chain(struct notifier_block *nb)
{
	atomic_notifier_chain_register(&ghes_report_chain, nb);
}
EXPORT_SYMBOL_GPL(ghes_register_report_chain);

void ghes_unregister_report_chain(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&ghes_report_chain, nb);
}
EXPORT_SYMBOL_GPL(ghes_unregister_report_chain);
