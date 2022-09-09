// SPDX-License-Identifier: GPL-2.0-only
/*
 * alternative runtime patching
 * inspired by the ARM64 and x86 version
 *
 * Copyright (C) 2021 Sifive.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/vendorid_list.h>
#include <asm/sbi.h>
#include <asm/csr.h>

struct cpu_manufacturer_info_t {
	unsigned long vendor_id;
	unsigned long arch_id;
	unsigned long imp_id;
	void (*patch_func)(struct alt_entry *begin, struct alt_entry *end,
				  unsigned long archid, unsigned long impid,
				  unsigned int stage);
};

static void __init_or_module riscv_fill_cpu_mfr_info(struct cpu_manufacturer_info_t *cpu_mfr_info)
{
#ifdef CONFIG_RISCV_M_MODE
	cpu_mfr_info->vendor_id = csr_read(CSR_MVENDORID);
	cpu_mfr_info->arch_id = csr_read(CSR_MARCHID);
	cpu_mfr_info->imp_id = csr_read(CSR_MIMPID);
#else
	cpu_mfr_info->vendor_id = sbi_get_mvendorid();
	cpu_mfr_info->arch_id = sbi_get_marchid();
	cpu_mfr_info->imp_id = sbi_get_mimpid();
#endif

	switch (cpu_mfr_info->vendor_id) {
#ifdef CONFIG_ERRATA_SIFIVE
	case SIFIVE_VENDOR_ID:
		cpu_mfr_info->patch_func = sifive_errata_patch_func;
		break;
#endif
#ifdef CONFIG_ERRATA_THEAD
	case THEAD_VENDOR_ID:
		cpu_mfr_info->patch_func = thead_errata_patch_func;
		break;
#endif
	default:
		cpu_mfr_info->patch_func = NULL;
	}
}

/*
 * This is called very early in the boot process (directly after we run
 * a feature detect on the boot CPU). No need to worry about other CPUs
 * here.
 */
static void __init_or_module _apply_alternatives(struct alt_entry *begin,
						 struct alt_entry *end,
						 unsigned int stage)
{
	struct cpu_manufacturer_info_t cpu_mfr_info;

	riscv_fill_cpu_mfr_info(&cpu_mfr_info);

	riscv_cpufeature_patch_func(begin, end, stage);

	if (!cpu_mfr_info.patch_func)
		return;

	cpu_mfr_info.patch_func(begin, end,
				cpu_mfr_info.arch_id,
				cpu_mfr_info.imp_id,
				stage);
}

void __init apply_boot_alternatives(void)
{
	/* If called on non-boot cpu things could go wrong */
	WARN_ON(smp_processor_id() != 0);

	_apply_alternatives((struct alt_entry *)__alt_start,
			    (struct alt_entry *)__alt_end,
			    RISCV_ALTERNATIVES_BOOT);
}

/*
 * apply_early_boot_alternatives() is called from setup_vm() with MMU-off.
 *
 * Following requirements should be honoured for it to work correctly:
 * 1) It should use PC-relative addressing for accessing kernel symbols.
 *    To achieve this we always use GCC cmodel=medany.
 * 2) The compiler instrumentation for FTRACE will not work for setup_vm()
 *    so disable compiler instrumentation when FTRACE is enabled.
 *
 * Currently, the above requirements are honoured by using custom CFLAGS
 * for alternative.o in kernel/Makefile.
 */
void __init apply_early_boot_alternatives(void)
{
#ifdef CONFIG_RISCV_ALTERNATIVE_EARLY
	_apply_alternatives((struct alt_entry *)__alt_start,
			    (struct alt_entry *)__alt_end,
			    RISCV_ALTERNATIVES_EARLY_BOOT);
#endif
}

#ifdef CONFIG_MODULES
void apply_module_alternatives(void *start, size_t length)
{
	_apply_alternatives((struct alt_entry *)start,
			    (struct alt_entry *)(start + length),
			    RISCV_ALTERNATIVES_MODULE);
}
#endif
