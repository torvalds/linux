/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mm/proc-arm7tdmi.S: utility functions for ARM7TDMI
 *
 *  Copyright (C) 2003-2006 Hyok S. Choi <hyok.choi@samsung.com>
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/pgtable.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hwcap.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptrace.h>

#include "proc-macros.S"

	.text
/*
 * cpu_arm7tdmi_proc_init()
 * cpu_arm7tdmi_do_idle()
 * cpu_arm7tdmi_dcache_clean_area()
 * cpu_arm7tdmi_switch_mm()
 *
 * These are not required.
 */
ENTRY(cpu_arm7tdmi_proc_init)
ENTRY(cpu_arm7tdmi_do_idle)
ENTRY(cpu_arm7tdmi_dcache_clean_area)
ENTRY(cpu_arm7tdmi_switch_mm)
		ret	lr

/*
 * cpu_arm7tdmi_proc_fin()
 */
ENTRY(cpu_arm7tdmi_proc_fin)
		ret	lr

/*
 * Function: cpu_arm7tdmi_reset(loc)
 * Params  : loc(r0)	address to jump to
 * Purpose : Sets up everything for a reset and jump to the location for soft reset.
 */
		.pushsection	.idmap.text, "ax"
ENTRY(cpu_arm7tdmi_reset)
		ret	r0
ENDPROC(cpu_arm7tdmi_reset)
		.popsection

		.type	__arm7tdmi_setup, #function
__arm7tdmi_setup:
		ret	lr
		.size	__arm7tdmi_setup, . - __arm7tdmi_setup

		__INITDATA

		@ define struct processor (see <asm/proc-fns.h> and proc-macros.S)
		define_processor_functions arm7tdmi, dabort=v4t_late_abort, pabort=legacy_pabort, nommu=1

		.section ".rodata"

		string	cpu_arch_name, "armv4t"
		string	cpu_elf_name, "v4"
		string	cpu_arm7tdmi_name, "ARM7TDMI"
		string	cpu_triscenda7_name, "Triscend-A7x"
		string	cpu_at91_name, "Atmel-AT91M40xxx"
		string	cpu_s3c3410_name, "Samsung-S3C3410"
		string	cpu_s3c44b0x_name, "Samsung-S3C44B0x"
		string	cpu_s3c4510b_name, "Samsung-S3C4510B"
		string	cpu_s3c4530_name, "Samsung-S3C4530"
		string	cpu_netarm_name, "NETARM"

		.align

		.section ".proc.info.init", "a"

.macro arm7tdmi_proc_info name:req, cpu_val:req, cpu_mask:req, cpu_name:req, \
	extra_hwcaps=0
		.type	__\name\()_proc_info, #object
__\name\()_proc_info:
		.long	\cpu_val
		.long	\cpu_mask
		.long	0
		.long	0
		initfn	__arm7tdmi_setup, __\name\()_proc_info
		.long	cpu_arch_name
		.long	cpu_elf_name
		.long	HWCAP_SWP | HWCAP_26BIT | ( \extra_hwcaps )
		.long	\cpu_name
		.long	arm7tdmi_processor_functions
		.long	0
		.long	0
		.long	v4_cache_fns
		.size	__\name\()_proc_info, . - __\name\()_proc_info
.endm

		arm7tdmi_proc_info arm7tdmi, 0x41007700, 0xfff8ff00, \
			cpu_arm7tdmi_name
		arm7tdmi_proc_info triscenda7, 0x0001d2ff, 0x0001ffff, \
			cpu_triscenda7_name, extra_hwcaps=HWCAP_THUMB
		arm7tdmi_proc_info at91, 0x14000040, 0xfff000e0, \
			cpu_at91_name, extra_hwcaps=HWCAP_THUMB
		arm7tdmi_proc_info s3c4510b, 0x36365000, 0xfffff000, \
			cpu_s3c4510b_name, extra_hwcaps=HWCAP_THUMB
		arm7tdmi_proc_info s3c4530, 0x4c000000, 0xfff000e0, \
			cpu_s3c4530_name, extra_hwcaps=HWCAP_THUMB
		arm7tdmi_proc_info s3c3410, 0x34100000, 0xffff0000, \
			cpu_s3c3410_name, extra_hwcaps=HWCAP_THUMB
		arm7tdmi_proc_info s3c44b0x, 0x44b00000, 0xffff0000, \
			cpu_s3c44b0x_name, extra_hwcaps=HWCAP_THUMB
