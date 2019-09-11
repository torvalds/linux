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
#include <linux/crash_dump.h>

#include <asm/page.h>
#include <asm/opal.h>
#include <asm/fadump-internal.h>

#include "opal-fadump.h"

static const struct opal_fadump_mem_struct *opal_fdm_active;
static struct opal_fadump_mem_struct *opal_fdm;

static int opal_fadump_unregister(struct fw_dump *fadump_conf);

static void opal_fadump_update_config(struct fw_dump *fadump_conf,
				      const struct opal_fadump_mem_struct *fdm)
{
	pr_debug("Boot memory regions count: %d\n", fdm->region_cnt);

	/*
	 * The destination address of the first boot memory region is the
	 * destination address of boot memory regions.
	 */
	fadump_conf->boot_mem_dest_addr = fdm->rgn[0].dest;
	pr_debug("Destination address of boot memory regions: %#016llx\n",
		 fadump_conf->boot_mem_dest_addr);

	fadump_conf->fadumphdr_addr = fdm->fadumphdr_addr;
}

/*
 * This function is called in the capture kernel to get configuration details
 * from metadata setup by the first kernel.
 */
static void opal_fadump_get_config(struct fw_dump *fadump_conf,
				   const struct opal_fadump_mem_struct *fdm)
{
	int i;

	if (!fadump_conf->dump_active)
		return;

	fadump_conf->boot_memory_size = 0;

	pr_debug("Boot memory regions:\n");
	for (i = 0; i < fdm->region_cnt; i++) {
		pr_debug("\t%d. base: 0x%llx, size: 0x%llx\n",
			 (i + 1), fdm->rgn[i].src, fdm->rgn[i].size);

		fadump_conf->boot_memory_size += fdm->rgn[i].size;
	}

	/*
	 * Start address of reserve dump area (permanent reservation) for
	 * re-registering FADump after dump capture.
	 */
	fadump_conf->reserve_dump_area_start = fdm->rgn[0].dest;

	/*
	 * Rarely, but it can so happen that system crashes before all
	 * boot memory regions are registered for MPIPL. In such
	 * cases, warn that the vmcore may not be accurate and proceed
	 * anyway as that is the best bet considering free pages, cache
	 * pages, user pages, etc are usually filtered out.
	 *
	 * Hope the memory that could not be preserved only has pages
	 * that are usually filtered out while saving the vmcore.
	 */
	if (fdm->region_cnt > fdm->registered_regions) {
		pr_warn("Not all memory regions were saved!!!\n");
		pr_warn("  Unsaved memory regions:\n");
		i = fdm->registered_regions;
		while (i < fdm->region_cnt) {
			pr_warn("\t[%03d] base: 0x%llx, size: 0x%llx\n",
				i, fdm->rgn[i].src, fdm->rgn[i].size);
			i++;
		}

		pr_warn("If the unsaved regions only contain pages that are filtered out (eg. free/user pages), the vmcore should still be usable.\n");
		pr_warn("WARNING: If the unsaved regions contain kernel pages, the vmcore will be corrupted.\n");
	}

	opal_fadump_update_config(fadump_conf, fdm);
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
	int max_copy_size, cur_size, size;
	u64 src_addr, dest_addr;

	opal_fdm = __va(fadump_conf->kernel_metadata);
	opal_fadump_init_metadata(opal_fdm);

	/*
	 * Firmware supports 32-bit field for size. Align it to PAGE_SIZE
	 * and request firmware to copy multiple kernel boot memory regions.
	 */
	max_copy_size = _ALIGN_DOWN(U32_MAX, PAGE_SIZE);

	/* Boot memory regions */
	src_addr = 0;
	dest_addr = fadump_conf->reserve_dump_area_start;
	size = fadump_conf->boot_memory_size;
	while (size) {
		cur_size = size > max_copy_size ? max_copy_size : size;

		opal_fdm->rgn[opal_fdm->region_cnt].src  = src_addr;
		opal_fdm->rgn[opal_fdm->region_cnt].dest = dest_addr;
		opal_fdm->rgn[opal_fdm->region_cnt].size = cur_size;

		opal_fdm->region_cnt++;
		dest_addr	+= cur_size;
		src_addr	+= cur_size;
		size		-= cur_size;
	}

	/*
	 * Kernel metadata is passed to f/w and retrieved in capture kerenl.
	 * So, use it to save fadump header address instead of calculating it.
	 */
	opal_fdm->fadumphdr_addr = (opal_fdm->rgn[0].dest +
				    fadump_conf->boot_memory_size);

	opal_fadump_update_config(fadump_conf, opal_fdm);

	return dest_addr;
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

/*
 * Convert CPU state data saved at the time of crash into ELF notes.
 *
 * Append crashing CPU's register data saved by the kernel in the PT_NOTE.
 */
static int __init
opal_fadump_build_cpu_notes(struct fw_dump *fadump_conf,
			    struct fadump_crash_info_header *fdh)
{
	u32 num_cpus = 1, *note_buf;
	int rc;

	if (fdh->crashing_cpu == FADUMP_CPU_UNKNOWN)
		return -ENODEV;

	/* Allocate CPU notes buffer to hold crashing cpu notes. */
	rc = fadump_setup_cpu_notes_buf(num_cpus);
	if (rc != 0)
		return rc;

	note_buf = (u32 *)fadump_conf->cpu_notes_buf_vaddr;
	note_buf = fadump_regs_to_elf_notes(note_buf, &(fdh->regs));
	final_note(note_buf);

	pr_debug("Updating elfcore header (%llx) with cpu notes\n",
		 fdh->elfcorehdr_addr);
	fadump_update_elfcore_header(__va(fdh->elfcorehdr_addr));
	return 0;
}

static int __init opal_fadump_process(struct fw_dump *fadump_conf)
{
	struct fadump_crash_info_header *fdh;
	int rc = -EINVAL;

	if (!opal_fdm_active || !fadump_conf->fadumphdr_addr)
		return rc;

	/* Validate the fadump crash info header */
	fdh = __va(fadump_conf->fadumphdr_addr);
	if (fdh->magic_number != FADUMP_CRASH_INFO_MAGIC) {
		pr_err("Crash info header is not valid.\n");
		return rc;
	}

	rc = opal_fadump_build_cpu_notes(fadump_conf, fdh);
	if (rc)
		return rc;

	/*
	 * We are done validating dump info and elfcore header is now ready
	 * to be exported. set elfcorehdr_addr so that vmcore module will
	 * export the elfcore header through '/proc/vmcore'.
	 */
	elfcorehdr_addr = fdh->elfcorehdr_addr;

	return rc;
}

static void opal_fadump_region_show(struct fw_dump *fadump_conf,
				    struct seq_file *m)
{
	const struct opal_fadump_mem_struct *fdm_ptr;
	u64 dumped_bytes = 0;
	int i;

	if (fadump_conf->dump_active)
		fdm_ptr = opal_fdm_active;
	else
		fdm_ptr = opal_fdm;

	for (i = 0; i < fdm_ptr->region_cnt; i++) {
		/*
		 * Only regions that are registered for MPIPL
		 * would have dump data.
		 */
		if ((fadump_conf->dump_active) &&
		    (i < fdm_ptr->registered_regions))
			dumped_bytes = fdm_ptr->rgn[i].size;

		seq_printf(m, "DUMP: Src: %#016llx, Dest: %#016llx, ",
			   fdm_ptr->rgn[i].src, fdm_ptr->rgn[i].dest);
		seq_printf(m, "Size: %#llx, Dumped: %#llx bytes\n",
			   fdm_ptr->rgn[i].size, dumped_bytes);
	}

	/* Dump is active. Show reserved area start address. */
	if (fadump_conf->dump_active) {
		seq_printf(m, "\nMemory above %#016lx is reserved for saving crash dump\n",
			   fadump_conf->reserve_dump_area_start);
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
	const __be32 *prop;
	unsigned long dn;
	u64 addr = 0;
	s64 ret;


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

	/*
	 * Check if dump has been initiated on last reboot.
	 */
	prop = of_get_flat_dt_prop(dn, "mpipl-boot", NULL);
	if (!prop)
		return;

	ret = opal_mpipl_query_tag(OPAL_MPIPL_TAG_KERNEL, &addr);
	if ((ret != OPAL_SUCCESS) || !addr) {
		pr_err("Failed to get Kernel metadata (%lld)\n", ret);
		return;
	}

	addr = be64_to_cpu(addr);
	pr_debug("Kernel metadata addr: %llx\n", addr);

	opal_fdm_active = __va(addr);
	if (opal_fdm_active->version != OPAL_FADUMP_VERSION) {
		pr_warn("Supported kernel metadata version: %u, found: %d!\n",
			OPAL_FADUMP_VERSION, opal_fdm_active->version);
		pr_warn("WARNING: Kernel metadata format mismatch identified! Core file maybe corrupted..\n");
	}

	/* Kernel regions not registered with f/w for MPIPL */
	if (opal_fdm_active->registered_regions == 0) {
		opal_fdm_active = NULL;
		return;
	}

	pr_info("Firmware-assisted dump is active.\n");
	fadump_conf->dump_active = 1;
	opal_fadump_get_config(fadump_conf, opal_fdm_active);
}
