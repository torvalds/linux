// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Hongjie Yang <hongjie@us.ibm.com>,
 */

#define KMSG_COMPONENT "setup"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

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

#define decompressor_handled_param(param)			\
static int __init ignore_decompressor_param_##param(char *s)	\
{								\
	return 0;						\
}								\
early_param(#param, ignore_decompressor_param_##param)

decompressor_handled_param(mem);
decompressor_handled_param(vmalloc);
decompressor_handled_param(dfltcc);
decompressor_handled_param(facilities);
decompressor_handled_param(nokaslr);
decompressor_handled_param(cmma);
decompressor_handled_param(relocate_lowcore);
#if IS_ENABLED(CONFIG_KVM)
decompressor_handled_param(prot_virt);
#endif

static void __init kasan_early_init(void)
{
#ifdef CONFIG_KASAN
	init_task.kasan_depth = 0;
	sclp_early_printk("KernelAddressSanitizer initialized\n");
#endif
}

static void __init reset_tod_clock(void)
{
	union tod_clock clk;

	if (store_tod_clock_ext_cc(&clk) == 0)
		return;
	/* TOD clock not running. Set the clock to Unix Epoch. */
	if (set_tod_clock(TOD_UNIX_EPOCH) || store_tod_clock_ext_cc(&clk))
		disabled_wait();

	memset(&tod_clock_base, 0, sizeof(tod_clock_base));
	tod_clock_base.tod = TOD_UNIX_EPOCH;
	get_lowcore()->last_update_clock = TOD_UNIX_EPOCH;
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

static noinline __init void detect_machine_type(void)
{
	struct sysinfo_3_2_2 *vmms = (struct sysinfo_3_2_2 *)&sysinfo_page;

	/* Check current-configuration-level */
	if (stsi(NULL, 0, 0, 0) <= 2) {
		get_lowcore()->machine_flags |= MACHINE_FLAG_LPAR;
		return;
	}
	/* Get virtual-machine cpu information. */
	if (stsi(vmms, 3, 2, 2) || !vmms->count)
		return;

	/* Detect known hypervisors */
	if (!memcmp(vmms->vm[0].cpi, "\xd2\xe5\xd4", 3))
		get_lowcore()->machine_flags |= MACHINE_FLAG_KVM;
	else if (!memcmp(vmms->vm[0].cpi, "\xa9\x61\xe5\xd4", 4))
		get_lowcore()->machine_flags |= MACHINE_FLAG_VM;
}

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
			MACHINE_IS_LPAR ? "LPAR" :
			MACHINE_IS_VM ? "z/VM" :
			MACHINE_IS_KVM ? "KVM" : "unknown");
	}
	dump_stack_set_arch_desc("%s (%s)", mstr, hvstr);
}

static __init void setup_topology(void)
{
	int max_mnest;

	if (!test_facility(11))
		return;
	get_lowcore()->machine_flags |= MACHINE_FLAG_TOPOLOGY;
	for (max_mnest = 6; max_mnest > 1; max_mnest--) {
		if (stsi(&sysinfo_page, 15, 1, max_mnest) == 0)
			break;
	}
	topology_max_mnest = max_mnest;
}

void __do_early_pgm_check(struct pt_regs *regs)
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

static __init void detect_diag9c(void)
{
	unsigned int cpu_address;
	int rc;

	cpu_address = stap();
	diag_stat_inc(DIAG_STAT_X09C);
	asm volatile(
		"	diag	%2,0,0x9c\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "0" (-EOPNOTSUPP), "d" (cpu_address) : "cc");
	if (!rc)
		get_lowcore()->machine_flags |= MACHINE_FLAG_DIAG9C;
}

static __init void detect_machine_facilities(void)
{
	if (test_facility(8)) {
		get_lowcore()->machine_flags |= MACHINE_FLAG_EDAT1;
		system_ctl_set_bit(0, CR0_EDAT_BIT);
	}
	if (test_facility(78))
		get_lowcore()->machine_flags |= MACHINE_FLAG_EDAT2;
	if (test_facility(3))
		get_lowcore()->machine_flags |= MACHINE_FLAG_IDTE;
	if (test_facility(50) && test_facility(73)) {
		get_lowcore()->machine_flags |= MACHINE_FLAG_TE;
		system_ctl_set_bit(0, CR0_TRANSACTIONAL_EXECUTION_BIT);
	}
	if (test_facility(51))
		get_lowcore()->machine_flags |= MACHINE_FLAG_TLB_LC;
	if (test_facility(129))
		system_ctl_set_bit(0, CR0_VECTOR_BIT);
	if (test_facility(130))
		get_lowcore()->machine_flags |= MACHINE_FLAG_NX;
	if (test_facility(133))
		get_lowcore()->machine_flags |= MACHINE_FLAG_GS;
	if (test_facility(139) && (tod_clock_base.tod >> 63)) {
		/* Enabled signed clock comparator comparisons */
		get_lowcore()->machine_flags |= MACHINE_FLAG_SCC;
		clock_comparator_max = -1ULL >> 1;
		system_ctl_set_bit(0, CR0_CLOCK_COMPARATOR_SIGN_BIT);
	}
	if (IS_ENABLED(CONFIG_PCI) && test_facility(153)) {
		get_lowcore()->machine_flags |= MACHINE_FLAG_PCI_MIO;
		/* the control bit is set during PCI initialization */
	}
	if (test_facility(194))
		get_lowcore()->machine_flags |= MACHINE_FLAG_RDP;
}

static inline void save_vector_registers(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (test_facility(129))
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
	reset_tod_clock();
	time_early_init();
	init_kernel_storage_key();
	lockdep_off();
	sort_amode31_extable();
	setup_lowcore_early();
	detect_machine_type();
	setup_arch_string();
	setup_boot_command_line();
	detect_diag9c();
	detect_machine_facilities();
	save_vector_registers();
	setup_topology();
	sclp_early_detect();
	setup_low_address_protection();
	setup_access_registers();
	lockdep_on();
}
