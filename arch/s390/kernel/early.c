// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Hongjie Yang <hongjie@us.ibm.com>,
 */

#define KMSG_COMPONENT "setup"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/sched/debug.h>
#include <linux/cpufeature.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/lockdep.h>
#include <linux/extable.h>
#include <linux/pfn.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <asm/asm-extable.h>
#include <linux/memblock.h>
#include <asm/access-regs.h>
#include <asm/machine.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/fpu.h>
#include <asm/ipl.h>
#include <asm/lowcore.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/sysinfo.h>
#include <asm/cpcmd.h>
#include <asm/sclp.h>
#include <asm/facility.h>
#include <asm/boot_data.h>
#include "entry.h"

#define __decompressor_handled_param(func, param)		\
static int __init ignore_decompressor_param_##func(char *s)	\
{								\
	return 0;						\
}								\
early_param(#param, ignore_decompressor_param_##func)

#define decompressor_handled_param(param) __decompressor_handled_param(param, param)

decompressor_handled_param(mem);
decompressor_handled_param(vmalloc);
decompressor_handled_param(dfltcc);
decompressor_handled_param(facilities);
decompressor_handled_param(nokaslr);
decompressor_handled_param(cmma);
decompressor_handled_param(relocate_lowcore);
decompressor_handled_param(bootdebug);
__decompressor_handled_param(debug_alternative, debug-alternative);
#if IS_ENABLED(CONFIG_KVM)
decompressor_handled_param(prot_virt);
#endif

static void __init kasan_early_init(void)
{
#ifdef CONFIG_KASAN
	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
#endif
}

/*
 * Initialize storage key for kernel pages
 */
static noinline __init void init_kernel_storage_key(void)
{
#if PAGE_DEFAULT_KEY
	unsigned long end_pfn, init_pfn;

	end_pfn = PFN_UP(__pa(_end));

	for (init_pfn = 0 ; init_pfn < end_pfn; init_pfn++)
		page_set_storage_key(init_pfn << PAGE_SHIFT,
				     PAGE_DEFAULT_KEY, 0);
#endif
}

static __initdata char sysinfo_page[PAGE_SIZE] __aligned(PAGE_SIZE);

/* Remove leading, trailing and double whitespace. */
static inline void strim_all(char *str)
{
	char *s;

	s = strim(str);
	if (s != str)
		memmove(str, s, strlen(s));
	while (*str) {
		if (!isspace(*str++))
			continue;
		if (isspace(*str)) {
			s = skip_spaces(str);
			memmove(str, s, strlen(s) + 1);
		}
	}
}

static noinline __init void setup_arch_string(void)
{
	struct sysinfo_1_1_1 *mach = (struct sysinfo_1_1_1 *)&sysinfo_page;
	struct sysinfo_3_2_2 *vm = (struct sysinfo_3_2_2 *)&sysinfo_page;
	char mstr[80], hvstr[17];

	if (stsi(mach, 1, 1, 1))
		return;
	EBCASC(mach->manufacturer, sizeof(mach->manufacturer));
	EBCASC(mach->type, sizeof(mach->type));
	EBCASC(mach->model, sizeof(mach->model));
	EBCASC(mach->model_capacity, sizeof(mach->model_capacity));
	sprintf(mstr, "%-16.16s %-4.4s %-16.16s %-16.16s",
		mach->manufacturer, mach->type,
		mach->model, mach->model_capacity);
	strim_all(mstr);
	if (stsi(vm, 3, 2, 2) == 0 && vm->count) {
		EBCASC(vm->vm[0].cpi, sizeof(vm->vm[0].cpi));
		sprintf(hvstr, "%-16.16s", vm->vm[0].cpi);
		strim_all(hvstr);
	} else {
		sprintf(hvstr, "%s",
			machine_is_lpar() ? "LPAR" :
			machine_is_vm() ? "z/VM" :
			machine_is_kvm() ? "KVM" : "unknown");
	}
	dump_stack_set_arch_desc("%s (%s)", mstr, hvstr);
}

static __init void setup_topology(void)
{
	int max_mnest;

	if (!cpu_has_topology())
		return;
	for (max_mnest = 6; max_mnest > 1; max_mnest--) {
		if (stsi(&sysinfo_page, 15, 1, max_mnest) == 0)
			break;
	}
	topology_max_mnest = max_mnest;
}

void __init __do_early_pgm_check(struct pt_regs *regs)
{
	struct lowcore *lc = get_lowcore();
	unsigned long ip;

	regs->int_code = lc->pgm_int_code;
	regs->int_parm_long = lc->trans_exc_code;
	ip = __rewind_psw(regs->psw, regs->int_code >> 16);

	/* Monitor Event? Might be a warning */
	if ((regs->int_code & PGM_INT_CODE_MASK) == 0x40) {
		if (report_bug(ip, regs) == BUG_TRAP_TYPE_WARN)
			return;
	}
	if (fixup_exception(regs))
		return;
	/*
	 * Unhandled exception - system cannot continue but try to get some
	 * helpful messages to the console. Use early_printk() to print
	 * some basic information in case it is too early for printk().
	 */
	register_early_console();
	early_printk("PANIC: early exception %04x PSW: %016lx %016lx\n",
		     regs->int_code & 0xffff, regs->psw.mask, regs->psw.addr);
	show_regs(regs);
	disabled_wait();
}

static noinline __init void setup_lowcore_early(void)
{
	struct lowcore *lc = get_lowcore();
	psw_t psw;

	psw.addr = (unsigned long)early_pgm_check_handler;
	psw.mask = PSW_KERNEL_BITS;
	lc->program_new_psw = psw;
	lc->preempt_count = INIT_PREEMPT_COUNT;
	lc->return_lpswe = gen_lpswe(__LC_RETURN_PSW);
	lc->return_mcck_lpswe = gen_lpswe(__LC_RETURN_MCCK_PSW);
}

static inline void save_vector_registers(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (cpu_has_vx())
		save_vx_regs(boot_cpu_vector_save_area);
#endif
}

static inline void setup_low_address_protection(void)
{
	system_ctl_set_bit(0, CR0_LOW_ADDRESS_PROTECTION_BIT);
}

static inline void setup_access_registers(void)
{
	unsigned int acrs[NUM_ACRS] = { 0 };

	restore_access_regs(acrs);
}

char __bootdata(early_command_line)[COMMAND_LINE_SIZE];
static void __init setup_boot_command_line(void)
{
	/* copy arch command line */
	strscpy(boot_command_line, early_command_line, COMMAND_LINE_SIZE);
}

static void __init sort_amode31_extable(void)
{
	sort_extable(__start_amode31_ex_table, __stop_amode31_ex_table);
}

void __init startup_init(void)
{
	kasan_early_init();
	time_early_init();
	init_kernel_storage_key();
	lockdep_off();
	sort_amode31_extable();
	setup_lowcore_early();
	setup_arch_string();
	setup_boot_command_line();
	save_vector_registers();
	setup_topology();
	sclp_early_detect();
	setup_low_address_protection();
	setup_access_registers();
	lockdep_on();
}
