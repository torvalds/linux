// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Firmware-Assisted Dump support on POWER platform (OPAL).
 *
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#define pr_fmt(fmt) "opal fadump: " fmt

#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/opal.h>
#include <asm/fadump-internal.h>

#include "opal-fadump.h"

static struct opal_fadump_mem_struct *opal_fdm;

static int opal_fadump_unregister(struct fw_dump *fadump_conf);

static void opal_fadump_update_config(struct fw_dump *fadump_conf,
				      const struct opal_fadump_mem_struct *fdm)
{
	/*
	 * The destination address of the first boot memory region is the
	 * destination address of boot memory regions.
	 */
	fadump_conf->boot_mem_dest_addr = fdm->rgn[0].dest;
	pr_debug("Destination address of boot memory regions: %#016llx\n",
		 fadump_conf->boot_mem_dest_addr);

	fadump_conf->fadumphdr_addr = fdm->fadumphdr_addr;
}

/* Initialize kernel metadata */
static void opal_fadump_init_metadata(struct opal_fadump_mem_struct *fdm)
{
	fdm->version = OPAL_FADUMP_VERSION;
	fdm->region_cnt = 0;
	fdm->registered_regions = 0;
	fdm->fadumphdr_addr = 0;
}

static u64 opal_fadump_init_mem_struct(struct fw_dump *fadump_conf)
{
	u64 addr = fadump_conf->reserve_dump_area_start;

	opal_fdm = __va(fadump_conf->kernel_metadata);
	opal_fadump_init_metadata(opal_fdm);

	opal_fdm->region_cnt = 1;
	opal_fdm->rgn[0].src	= 0;
	opal_fdm->rgn[0].dest	= addr;
	opal_fdm->rgn[0].size	= fadump_conf->boot_memory_size;
	addr += fadump_conf->boot_memory_size;

	/*
	 * Kernel metadata is passed to f/w and retrieved in capture kerenl.
	 * So, use it to save fadump header address instead of calculating it.
	 */
	opal_fdm->fadumphdr_addr = (opal_fdm->rgn[0].dest +
				    fadump_conf->boot_memory_size);

	opal_fadump_update_config(fadump_conf, opal_fdm);

	return addr;
}

static u64 opal_fadump_get_metadata_size(void)
{
	return PAGE_ALIGN(sizeof(struct opal_fadump_mem_struct));
}

static int opal_fadump_setup_metadata(struct fw_dump *fadump_conf)
{
	int err = 0;
	s64 ret;

	/*
	 * Use the last page(s) in FADump memory reservation for
	 * kernel metadata.
	 */
	fadump_conf->kernel_metadata = (fadump_conf->reserve_dump_area_start +
					fadump_conf->reserve_dump_area_size -
					opal_fadump_get_metadata_size());
	pr_info("Kernel metadata addr: %llx\n", fadump_conf->kernel_metadata);

	/* Initialize kernel metadata before registering the address with f/w */
	opal_fdm = __va(fadump_conf->kernel_metadata);
	opal_fadump_init_metadata(opal_fdm);

	/*
	 * Register metadata address with f/w. Can be retrieved in
	 * the capture kernel.
	 */
	ret = opal_mpipl_register_tag(OPAL_MPIPL_TAG_KERNEL,
				      fadump_conf->kernel_metadata);
	if (ret != OPAL_SUCCESS) {
		pr_err("Failed to set kernel metadata tag!\n");
		err = -EPERM;
	}

	return err;
}

static int opal_fadump_register(struct fw_dump *fadump_conf)
{
	s64 rc = OPAL_PARAMETER;
	int i, err = -EIO;

	for (i = 0; i < opal_fdm->region_cnt; i++) {
		rc = opal_mpipl_update(OPAL_MPIPL_ADD_RANGE,
				       opal_fdm->rgn[i].src,
				       opal_fdm->rgn[i].dest,
				       opal_fdm->rgn[i].size);
		if (rc != OPAL_SUCCESS)
			break;

		opal_fdm->registered_regions++;
	}

	switch (rc) {
	case OPAL_SUCCESS:
		pr_info("Registration is successful!\n");
		fadump_conf->dump_registered = 1;
		err = 0;
		break;
	case OPAL_RESOURCE:
		/* If MAX regions limit in f/w is hit, warn and proceed. */
		pr_warn("%d regions could not be registered for MPIPL as MAX limit is reached!\n",
			(opal_fdm->region_cnt - opal_fdm->registered_regions));
		fadump_conf->dump_registered = 1;
		err = 0;
		break;
	case OPAL_PARAMETER:
		pr_err("Failed to register. Parameter Error(%lld).\n", rc);
		break;
	case OPAL_HARDWARE:
		pr_err("Support not available.\n");
		fadump_conf->fadump_supported = 0;
		fadump_conf->fadump_enabled = 0;
		break;
	default:
		pr_err("Failed to register. Unknown Error(%lld).\n", rc);
		break;
	}

	/*
	 * If some regions were registered before OPAL_MPIPL_ADD_RANGE
	 * OPAL call failed, unregister all regions.
	 */
	if ((err < 0) && (opal_fdm->registered_regions > 0))
		opal_fadump_unregister(fadump_conf);

	return err;
}

static int opal_fadump_unregister(struct fw_dump *fadump_conf)
{
	s64 rc;

	rc = opal_mpipl_update(OPAL_MPIPL_REMOVE_ALL, 0, 0, 0);
	if (rc) {
		pr_err("Failed to un-register - unexpected Error(%lld).\n", rc);
		return -EIO;
	}

	opal_fdm->registered_regions = 0;
	fadump_conf->dump_registered = 0;
	return 0;
}

static int opal_fadump_invalidate(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static void opal_fadump_cleanup(struct fw_dump *fadump_conf)
{
	s64 ret;

	ret = opal_mpipl_register_tag(OPAL_MPIPL_TAG_KERNEL, 0);
	if (ret != OPAL_SUCCESS)
		pr_warn("Could not reset (%llu) kernel metadata tag!\n", ret);
}

static int __init opal_fadump_process(struct fw_dump *fadump_conf)
{
	return -EINVAL;
}

static void opal_fadump_region_show(struct fw_dump *fadump_conf,
				    struct seq_file *m)
{
	const struct opal_fadump_mem_struct *fdm_ptr = opal_fdm;
	u64 dumped_bytes = 0;
	int i;

	for (i = 0; i < fdm_ptr->region_cnt; i++) {
		seq_printf(m, "DUMP: Src: %#016llx, Dest: %#016llx, ",
			   fdm_ptr->rgn[i].src, fdm_ptr->rgn[i].dest);
		seq_printf(m, "Size: %#llx, Dumped: %#llx bytes\n",
			   fdm_ptr->rgn[i].size, dumped_bytes);
	}
}

static void opal_fadump_trigger(struct fadump_crash_info_header *fdh,
				const char *msg)
{
	int rc;

	rc = opal_cec_reboot2(OPAL_REBOOT_MPIPL, msg);
	if (rc == OPAL_UNSUPPORTED) {
		pr_emerg("Reboot type %d not supported.\n",
			 OPAL_REBOOT_MPIPL);
	} else if (rc == OPAL_HARDWARE)
		pr_emerg("No backend support for MPIPL!\n");
}

static struct fadump_ops opal_fadump_ops = {
	.fadump_init_mem_struct		= opal_fadump_init_mem_struct,
	.fadump_get_metadata_size	= opal_fadump_get_metadata_size,
	.fadump_setup_metadata		= opal_fadump_setup_metadata,
	.fadump_register		= opal_fadump_register,
	.fadump_unregister		= opal_fadump_unregister,
	.fadump_invalidate		= opal_fadump_invalidate,
	.fadump_cleanup			= opal_fadump_cleanup,
	.fadump_process			= opal_fadump_process,
	.fadump_region_show		= opal_fadump_region_show,
	.fadump_trigger			= opal_fadump_trigger,
};

void __init opal_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node)
{
	unsigned long dn;

	/*
	 * Check if Firmware-Assisted Dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	dn = of_get_flat_dt_subnode_by_name(node, "dump");
	if (dn == -FDT_ERR_NOTFOUND) {
		pr_debug("FADump support is missing!\n");
		return;
	}

	if (!of_flat_dt_is_compatible(dn, "ibm,opal-dump")) {
		pr_err("Support missing for this f/w version!\n");
		return;
	}

	fadump_conf->ops		= &opal_fadump_ops;
	fadump_conf->fadump_supported	= 1;
}
