/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/proc-arm9tdmi.S: utility functions for ARM9TDMI
 *
 *  Copyright (C) 2003-2006 Hyok S. Choi <hyok.choi@samsung.com>
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/cfi_types.h>
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

	.text
/*
 * cpu_arm9tdmi_proc_init()
 * cpu_arm9tdmi_do_idle()
 * cpu_arm9tdmi_dcache_clean_area()
 * cpu_arm9tdmi_switch_mm()
 *
 * These are not required.
 */
SYM_TYPED_FUNC_START(cpu_arm9tdmi_proc_init)
		ret	lr
SYM_FUNC_END(cpu_arm9tdmi_proc_init)

SYM_TYPED_FUNC_START(cpu_arm9tdmi_do_idle)
		ret	lr
SYM_FUNC_END(cpu_arm9tdmi_do_idle)

SYM_TYPED_FUNC_START(cpu_arm9tdmi_dcache_clean_area)
		ret	lr
SYM_FUNC_END(cpu_arm9tdmi_dcache_clean_area)

SYM_TYPED_FUNC_START(cpu_arm9tdmi_switch_mm)
		ret	lr
SYM_FUNC_END(cpu_arm9tdmi_switch_mm)

/*
 * cpu_arm9tdmi_proc_fin()
 */
SYM_TYPED_FUNC_START(cpu_arm9tdmi_proc_fin)
		ret	lr
SYM_FUNC_END(cpu_arm9tdmi_proc_fin)

/*
 * Function: cpu_arm9tdmi_reset(loc)
 * Params  : loc(r0)	address to jump to
 * Purpose : Sets up everything for a reset and jump to the location for soft reset.
 */
		.pushsection	.idmap.text, "ax"
SYM_TYPED_FUNC_START(cpu_arm9tdmi_reset)
		ret	r0
SYM_FUNC_END(cpu_arm9tdmi_reset)
		.popsection

		.type	__arm9tdmi_setup, #function
__arm9tdmi_setup:
		ret	lr
		.size	__arm9tdmi_setup, . - __arm9tdmi_setup

		__INITDATA

		@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
		define_processor_functions arm9tdmi, dabort=nommu_early_abort, pabort=legacy_pabort, nommu=1

		.section ".rodata"

		string	cpu_arch_name, "armv4t"
		string	cpu_elf_name, "v4"
		string	cpu_arm9tdmi_name, "ARM9TDMI"
		string	cpu_p2001_name, "P2001"

		.align

		.section ".proc.info.init", "a"

.macro arm9tdmi_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req
		.type	__\name\()_proc_info, #object
__\name\()_proc_info:
		.long	\cpu_val
		.long	\cpu_mask
		.long	0
		.long	0
		initfn	__arm9tdmi_setup, __\name\()_proc_info
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_THUMB | HWCAP_26BIT
		.long	\cpu_name
		.long	arm9tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

	arm9tdmi_proc_info arm9tdmi, 0x41009900, 0xfff8ff00, cpu_arm9tdmi_name
	arm9tdmi_proc_info p2001, 0x41029000, 0xffffffff, cpu_p2001_name
