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
 * Now, only SCI notification type and memory errors are
 * supported. More notification type and hardware error type will be
 * added later.
 *
 * Copyright 2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/cper.h>
#include <linux/kdebug.h>
#include <acpi/apei.h>
#include <acpi/atomicio.h>
#include <acpi/hed.h>
#include <asm/mce.h>

#include "apei-internal.h"

#define GHES_PFX	"GHES: "

#define GHES_ESTATUS_MAX_SIZE		65536

/*
 * One struct ghes is created for each generic hardware error
 * source.
 *
 * It provides the context for APEI hardware error timer/IRQ/SCI/NMI
 * handler. Handler for one generic hardware error source is only
 * triggered after the previous one is done. So handler can uses
 * struct ghes without locking.
 *
 * estatus: memory buffer for error status block, allocated during
 * HEST parsing.
 */
#define GHES_TO_CLEAR		0x0001

struct ghes {
	struct acpi_hest_generic *generic;
	struct acpi_hest_generic_status *estatus;
	struct list_head list;
	u64 buffer_paddr;
	unsigned long flags;
};

/*
 * Error source lists, one list for each notification method. The
 * members in lists are struct ghes.
 *
 * The list members are only added in HEST parsing and deleted during
 * module_exit, that is, single-threaded. So no lock is needed for
 * that.
 *
 * But the mutual exclusion is needed between members adding/deleting
 * and timer/IRQ/SCI/NMI handler, which may traverse the list. RCU is
 * used for that.
 */
static LIST_HEAD(ghes_sci);

static struct ghes *ghes_new(struct acpi_hest_generic *generic)
{
	struct ghes *ghes;
	unsigned int error_block_length;
	int rc;

	ghes = kzalloc(sizeof(*ghes), GFP_KERNEL);
	if (!ghes)
		return ERR_PTR(-ENOMEM);
	ghes->generic = generic;
	INIT_LIST_HEAD(&ghes->list);
	rc = acpi_pre_map_gar(&generic->error_status_address);
	if (rc)
		goto err_free;
	error_block_length = generic->error_block_length;
	if (error_block_length > GHES_ESTATUS_MAX_SIZE) {
		pr_warning(FW_WARN GHES_PFX
			   "Error status block length is too long: %u for "
			   "generic hardware error source: %d.\n",
			   error_block_length, generic->header.source_id);
		error_block_length = GHES_ESTATUS_MAX_SIZE;
	}
	ghes->estatus = kmalloc(error_block_length, GFP_KERNEL);
	if (!ghes->estatus) {
		rc = -ENOMEM;
		goto err_unmap;
	}

	return ghes;

err_unmap:
	acpi_post_unmap_gar(&generic->error_status_address);
err_free:
	kfree(ghes);
	return ERR_PTR(rc);
}

static void ghes_fini(struct ghes *ghes)
{
	kfree(ghes->estatus);
	acpi_post_unmap_gar(&ghes->generic->error_status_address);
}

enum {
	GHES_SER_NO = 0x0,
	GHES_SER_CORRECTED = 0x1,
	GHES_SER_RECOVERABLE = 0x2,
	GHES_SER_PANIC = 0x3,
};

static inline int ghes_severity(int severity)
{
	switch (severity) {
	case CPER_SER_INFORMATIONAL:
		return GHES_SER_NO;
	case CPER_SER_CORRECTED:
		return GHES_SER_CORRECTED;
	case CPER_SER_RECOVERABLE:
		return GHES_SER_RECOVERABLE;
	case CPER_SER_FATAL:
		return GHES_SER_PANIC;
	default:
		/* Unkown, go panic */
		return GHES_SER_PANIC;
	}
}

/* SCI handler run in work queue, so ioremap can be used here */
static int ghes_copy_tofrom_phys(void *buffer, u64 paddr, u32 len,
				 int from_phys)
{
	void *vaddr;

	vaddr = ioremap_cache(paddr, len);
	if (!vaddr)
		return -ENOMEM;
	if (from_phys)
		memcpy(buffer, vaddr, len);
	else
		memcpy(vaddr, buffer, len);
	iounmap(vaddr);

	return 0;
}

static int ghes_read_estatus(struct ghes *ghes, int silent)
{
	struct acpi_hest_generic *g = ghes->generic;
	u64 buf_paddr;
	u32 len;
	int rc;

	rc = acpi_atomic_read(&buf_paddr, &g->error_status_address);
	if (rc) {
		if (!silent && printk_ratelimit())
			pr_warning(FW_WARN GHES_PFX
"Failed to read error status block address for hardware error source: %d.\n",
				   g->header.source_id);
		return -EIO;
	}
	if (!buf_paddr)
		return -ENOENT;

	rc = ghes_copy_tofrom_phys(ghes->estatus, buf_paddr,
				   sizeof(*ghes->estatus), 1);
	if (rc)
		return rc;
	if (!ghes->estatus->block_status)
		return -ENOENT;

	ghes->buffer_paddr = buf_paddr;
	ghes->flags |= GHES_TO_CLEAR;

	rc = -EIO;
	len = apei_estatus_len(ghes->estatus);
	if (len < sizeof(*ghes->estatus))
		goto err_read_block;
	if (len > ghes->generic->error_block_length)
		goto err_read_block;
	if (apei_estatus_check_header(ghes->estatus))
		goto err_read_block;
	rc = ghes_copy_tofrom_phys(ghes->estatus + 1,
				   buf_paddr + sizeof(*ghes->estatus),
				   len - sizeof(*ghes->estatus), 1);
	if (rc)
		return rc;
	if (apei_estatus_check(ghes->estatus))
		goto err_read_block;
	rc = 0;

err_read_block:
	if (rc && !silent)
		pr_warning(FW_WARN GHES_PFX
			   "Failed to read error status block!\n");
	return rc;
}

static void ghes_clear_estatus(struct ghes *ghes)
{
	ghes->estatus->block_status = 0;
	if (!(ghes->flags & GHES_TO_CLEAR))
		return;
	ghes_copy_tofrom_phys(ghes->estatus, ghes->buffer_paddr,
			      sizeof(ghes->estatus->block_status), 0);
	ghes->flags &= ~GHES_TO_CLEAR;
}

static void ghes_do_proc(struct ghes *ghes)
{
	int ser, processed = 0;
	struct acpi_hest_generic_data *gdata;

	ser = ghes_severity(ghes->estatus->error_severity);
	apei_estatus_for_each_section(ghes->estatus, gdata) {
#ifdef CONFIG_X86_MCE
		if (!uuid_le_cmp(*(uuid_le *)gdata->section_type,
				 CPER_SEC_PLATFORM_MEM)) {
			apei_mce_report_mem_error(
				ser == GHES_SER_CORRECTED,
				(struct cper_sec_mem_err *)(gdata+1));
			processed = 1;
		}
#endif
	}

	if (!processed && printk_ratelimit())
		pr_warning(GHES_PFX
		"Unknown error record from generic hardware error source: %d\n",
			   ghes->generic->header.source_id);
}

static int ghes_proc(struct ghes *ghes)
{
	int rc;

	rc = ghes_read_estatus(ghes, 0);
	if (rc)
		goto out;
	ghes_do_proc(ghes);

out:
	ghes_clear_estatus(ghes);
	return 0;
}

static int ghes_notify_sci(struct notifier_block *this,
				  unsigned long event, void *data)
{
	struct ghes *ghes;
	int ret = NOTIFY_DONE;

	rcu_read_lock();
	list_for_each_entry_rcu(ghes, &ghes_sci, list) {
		if (!ghes_proc(ghes))
			ret = NOTIFY_OK;
	}
	rcu_read_unlock();

	return ret;
}

static struct notifier_block ghes_notifier_sci = {
	.notifier_call = ghes_notify_sci,
};

static int hest_ghes_parse(struct acpi_hest_header *hest_hdr, void *data)
{
	struct acpi_hest_generic *generic;
	struct ghes *ghes = NULL;
	int rc = 0;

	if (hest_hdr->type != ACPI_HEST_TYPE_GENERIC_ERROR)
		return 0;

	generic = (struct acpi_hest_generic *)hest_hdr;
	if (!generic->enabled)
		return 0;

	if (generic->error_block_length <
	    sizeof(struct acpi_hest_generic_status)) {
		pr_warning(FW_BUG GHES_PFX
"Invalid error block length: %u for generic hardware error source: %d\n",
			   generic->error_block_length,
			   generic->header.source_id);
		goto err;
	}
	if (generic->records_to_preallocate == 0) {
		pr_warning(FW_BUG GHES_PFX
"Invalid records to preallocate: %u for generic hardware error source: %d\n",
			   generic->records_to_preallocate,
			   generic->header.source_id);
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
		pr_warning(GHES_PFX
"Generic hardware error source: %d notified via POLL is not supported!\n",
			   generic->header.source_id);
		break;
	case ACPI_HEST_NOTIFY_EXTERNAL:
	case ACPI_HEST_NOTIFY_LOCAL:
		pr_warning(GHES_PFX
"Generic hardware error source: %d notified via IRQ is not supported!\n",
			   generic->header.source_id);
		break;
	case ACPI_HEST_NOTIFY_SCI:
		if (list_empty(&ghes_sci))
			register_acpi_hed_notifier(&ghes_notifier_sci);
		list_add_rcu(&ghes->list, &ghes_sci);
		break;
	case ACPI_HEST_NOTIFY_NMI:
		pr_warning(GHES_PFX
"Generic hardware error source: %d notified via NMI is not supported!\n",
			   generic->header.source_id);
		break;
	default:
		pr_warning(FW_WARN GHES_PFX
	"Unknown notification type: %u for generic hardware error source: %d\n",
			   generic->notify.type, generic->header.source_id);
		break;
	}

	return 0;
err:
	if (ghes)
		ghes_fini(ghes);
	return rc;
}

static void ghes_cleanup(void)
{
	struct ghes *ghes, *nghes;

	if (!list_empty(&ghes_sci))
		unregister_acpi_hed_notifier(&ghes_notifier_sci);

	synchronize_rcu();

	list_for_each_entry_safe(ghes, nghes, &ghes_sci, list) {
		list_del(&ghes->list);
		ghes_fini(ghes);
		kfree(ghes);
	}
}

static int __init ghes_init(void)
{
	int rc;

	if (acpi_disabled)
		return -ENODEV;

	if (hest_disable) {
		pr_info(GHES_PFX "HEST is not enabled!\n");
		return -EINVAL;
	}

	rc = apei_hest_parse(hest_ghes_parse, NULL);
	if (rc) {
		pr_err(GHES_PFX
		"Error during parsing HEST generic hardware error sources.\n");
		goto err_cleanup;
	}

	if (list_empty(&ghes_sci)) {
		pr_info(GHES_PFX
			"No functional generic hardware error sources.\n");
		rc = -ENODEV;
		goto err_cleanup;
	}

	pr_info(GHES_PFX
		"Generic Hardware Error Source support is initialized.\n");

	return 0;
err_cleanup:
	ghes_cleanup();
	return rc;
}

static void __exit ghes_exit(void)
{
	ghes_cleanup();
}

module_init(ghes_init);
module_exit(ghes_exit);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("APEI Generic Hardware Error Source support");
MODULE_LICENSE("GPL");
