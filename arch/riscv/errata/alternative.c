// SPDX-License-Identifier: GPL-2.0-only
/*
 * alternative runtime patching
 * inspired by the ARM64 and x86 version
 *
 * Copyright (C) 2021 Sifive.
 */

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/vendorid_list.h>
#include <asm/sbi.h>
#include <asm/csr.h>

static struct cpu_manufacturer_info_t {
	unsigned long vendor_id;
	unsigned long arch_id;
	unsigned long imp_id;
} cpu_mfr_info;

static void (*vendor_patch_func)(struct alt_entry *begin, struct alt_entry *end,
				 unsigned long archid,
				 unsigned long impid) __initdata;

static inline void __init riscv_fill_cpu_mfr_info(void)
{
#ifdef CONFIG_RISCV_M_MODE
	cpu_mfr_info.vendor_id = csr_read(CSR_MVENDORID);
	cpu_mfr_info.arch_id = csr_read(CSR_MARCHID);
	cpu_mfr_info.imp_id = csr_read(CSR_MIMPID);
#else
	cpu_mfr_info.vendor_id = sbi_get_mvendorid();
	cpu_mfr_info.arch_id = sbi_get_marchid();
	cpu_mfr_info.imp_id = sbi_get_mimpid();
#endif
}

static void __init init_alternative(void)
{
	riscv_fill_cpu_mfr_info();

	switch (cpu_mfr_info.vendor_id) {
#ifdef CONFIG_ERRATA_SIFIVE
	case SIFIVE_VENDOR_ID:
		vendor_patch_func = sifive_errata_patch_func;
		break;
#endif
	default:
		vendor_patch_func = NULL;
	}
}

/*
 * This is called very early in the boot process (directly after we run
 * a feature detect on the boot CPU). No need to worry about other CPUs
 * here.
 */
void __init apply_boot_alternatives(void)
{
	/* If called on non-boot cpu things could go wrong */
	WARN_ON(smp_processor_id() != 0);

	init_alternative();

	if (!vendor_patch_func)
		return;

	vendor_patch_func((struct alt_entry *)__alt_start,
			  (struct alt_entry *)__alt_end,
			  cpu_mfr_info.arch_id, cpu_mfr_info.imp_id);
}

