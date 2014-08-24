/*
 *  linux/arch/arm/mm/proc-arm9tdmi.S: utility functions for ARM9TDMI
 *
 *  Copyright (C) 2003-2006 Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
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
ENTRY(cpu_arm9tdmi_proc_init)
ENTRY(cpu_arm9tdmi_do_idle)
ENTRY(cpu_arm9tdmi_dcache_clean_area)
ENTRY(cpu_arm9tdmi_switch_mm)
		ret	lr

/*
 * cpu_arm9tdmi_proc_fin()
 */
ENTRY(cpu_arm9tdmi_proc_fin)
		ret	lr

/*
 * Function: cpu_arm9tdmi_reset(loc)
 * Params  : loc(r0)	address to jump to
 * Purpose : Sets up everything for a reset and jump to the location for soft reset.
 */
		.pushsection	.idmap.text, "ax"
ENTRY(cpu_arm9tdmi_reset)
		ret	r0
ENDPROC(cpu_arm9tdmi_reset)
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

		.section ".proc.info.init", #alloc, #execinstr

.macro arm9tdmi_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req
		.type	__\name\()_proc_info, #object
__\name\()_proc_info:
		.long	\cpu_val
		.long	\cpu_mask
		.long	0
		.long	0
		b	__arm9tdmi_setup
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
