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
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/ratelimit.h>
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
static DEFINE_MUTEX(ghes_list_mutex);

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
	GHES_SEV_NO = 0x0,
	GHES_SEV_CORRECTED = 0x1,
	GHES_SEV_RECOVERABLE = 0x2,
	GHES_SEV_PANIC = 0x3,
};

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
		/* Unkown, go panic */
		return GHES_SEV_PANIC;
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
	int sev, processed = 0;
	struct acpi_hest_generic_data *gdata;

	sev = ghes_severity(ghes->estatus->error_severity);
	apei_estatus_for_each_section(ghes->estatus, gdata) {
#ifdef CONFIG_X86_MCE
		if (!uuid_le_cmp(*(uuid_le *)gdata->section_type,
				 CPER_SEC_PLATFORM_MEM)) {
			apei_mce_report_mem_error(
				sev == GHES_SEV_CORRECTED,
				(struct cper_sec_mem_err *)(gdata+1));
			processed = 1;
		}
#endif
	}
}

static void ghes_print_estatus(const char *pfx, struct ghes *ghes)
{
	/* Not more than 2 messages every 5 seconds */
	static DEFINE_RATELIMIT_STATE(ratelimit, 5*HZ, 2);

	if (pfx == NULL) {
		if (ghes_severity(ghes->estatus->error_severity) <=
		    GHES_SEV_CORRECTED)
			pfx = KERN_WARNING HW_ERR;
		else
			pfx = KERN_ERR HW_ERR;
	}
	if (__ratelimit(&ratelimit)) {
		printk(
	"%s""Hardware error from APEI Generic Hardware Error Source: %d\n",
	pfx, ghes->generic->header.source_id);
		apei_estatus_print(pfx, ghes->estatus);
	}
}

static int ghes_proc(struct ghes *ghes)
{
	int rc;

	rc = ghes_read_estatus(ghes, 0);
	if (rc)
		goto out;
	ghes_print_estatus(NULL, ghes);
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

static int __devinit ghes_probe(struct platform_device *ghes_dev)
{
	struct acpi_hest_generic *generic;
	struct ghes *ghes = NULL;
	int rc = -EINVAL;

	generic = *(struct acpi_hest_generic **)ghes_dev->dev.platform_data;
	if (!generic->enabled)
		return -ENODEV;

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
	if (generic->notify.type == ACPI_HEST_NOTIFY_SCI) {
		mutex_lock(&ghes_list_mutex);
		if (list_empty(&ghes_sci))
			register_acpi_hed_notifier(&ghes_notifier_sci);
		list_add_rcu(&ghes->list, &ghes_sci);
		mutex_unlock(&ghes_list_mutex);
	} else {
		unsigned char *notify = NULL;

		switch (generic->notify.type) {
		case ACPI_HEST_NOTIFY_POLLED:
			notify = "POLL";
			break;
		case ACPI_HEST_NOTIFY_EXTERNAL:
		case ACPI_HEST_NOTIFY_LOCAL:
			notify = "IRQ";
			break;
		case ACPI_HEST_NOTIFY_NMI:
			notify = "NMI";
			break;
		}
		if (notify) {
			pr_warning(GHES_PFX
"Generic hardware error source: %d notified via %s is not supported!\n",
				   generic->header.source_id, notify);
		} else {
			pr_warning(FW_WARN GHES_PFX
"Unknown notification type: %u for generic hardware error source: %d\n",
			generic->notify.type, generic->header.source_id);
		}
		rc = -ENODEV;
		goto err;
	}
	platform_set_drvdata(ghes_dev, ghes);

	return 0;
err:
	if (ghes) {
		ghes_fini(ghes);
		kfree(ghes);
	}
	return rc;
}

static int __devexit ghes_remove(struct platform_device *ghes_dev)
{
	struct ghes *ghes;
	struct acpi_hest_generic *generic;

	ghes = platform_get_drvdata(ghes_dev);
	generic = ghes->generic;

	switch (generic->notify.type) {
	case ACPI_HEST_NOTIFY_SCI:
		mutex_lock(&ghes_list_mutex);
		list_del_rcu(&ghes->list);
		if (list_empty(&ghes_sci))
			unregister_acpi_hed_notifier(&ghes_notifier_sci);
		mutex_unlock(&ghes_list_mutex);
		break;
	default:
		BUG();
		break;
	}

	synchronize_rcu();
	ghes_fini(ghes);
	kfree(ghes);

	platform_set_drvdata(ghes_dev, NULL);

	return 0;
}

static struct platform_driver ghes_platform_driver = {
	.driver		= {
		.name	= "GHES",
		.owner	= THIS_MODULE,
	},
	.probe		= ghes_probe,
	.remove		= ghes_remove,
};

static int __init ghes_init(void)
{
	if (acpi_disabled)
		return -ENODEV;

	if (hest_disable) {
		pr_info(GHES_PFX "HEST is not enabled!\n");
		return -EINVAL;
	}

	return platform_driver_register(&ghes_platform_driver);
}

static void __exit ghes_exit(void)
{
	platform_driver_unregister(&ghes_platform_driver);
}

module_init(ghes_init);
module_exit(ghes_exit);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("APEI Generic Hardware Error Source support");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:GHES");
