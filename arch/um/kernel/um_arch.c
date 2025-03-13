// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kmsg_dump.h>
#include <linux/suspend.h>
#include <linux/random.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/text-patching.h>
#include <as-layout.h>
#include <arch.h>
#include <init.h>
#include <kern.h>
#include <kern_util.h>
#include <mem_user.h>
#include <os.h>

#include "um_arch.h"

#define DEFAULT_COMMAND_LINE_ROOT "root=98:0"
#define DEFAULT_COMMAND_LINE_CONSOLE "console=tty0"

/* Changed in add_arg and setup_arch, which run before SMP is started */
static char __initdata command_line[COMMAND_LINE_SIZE] = { 0 };

static void __init add_arg(char *arg)
{
	if (strlen(command_line) + strlen(arg) + 1 > COMMAND_LINE_SIZE) {
		os_warn("add_arg: Too many command line arguments!\n");
		exit(1);
	}
	if (strlen(command_line) > 0)
		strcat(command_line, " ");
	strcat(command_line, arg);
}

/*
 * These fields are initialized at boot time and not changed.
 * XXX This structure is used only in the non-SMP case.  Maybe this
 * should be moved to smp.c.
 */
struct cpuinfo_um boot_cpu_data = {
	.loops_per_jiffy	= 0,
	.ipi_pipe		= { -1, -1 },
	.cache_alignment	= L1_CACHE_BYTES,
	.x86_capability		= { 0 }
};

EXPORT_SYMBOL(boot_cpu_data);


/* Changed in setup_arch, which is called in early boot */
static char host_info[(__NEW_UTS_LEN + 1) * 5];

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int i = 0;

	seq_printf(m, "processor\t: %d\n", i);
	seq_printf(m, "vendor_id\t: User Mode Linux\n");
	seq_printf(m, "model name\t: UML\n");
	seq_printf(m, "mode\t\t: skas\n");
	seq_printf(m, "host\t\t: %s\n", host_info);
	seq_printf(m, "fpu\t\t: %s\n", cpu_has(&boot_cpu_data, X86_FEATURE_FPU) ? "yes" : "no");
	seq_printf(m, "flags\t\t:");
	for (i = 0; i < 32*NCAPINTS; i++)
		if (cpu_has(&boot_cpu_data, i) && (x86_cap_flags[i] != NULL))
			seq_printf(m, " %s", x86_cap_flags[i]);
	seq_printf(m, "\n");
	seq_printf(m, "cache_alignment\t: %d\n", boot_cpu_data.cache_alignment);
	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   loops_per_jiffy/(500000/HZ),
		   (loops_per_jiffy/(5000/HZ)) % 100);


	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? &boot_cpu_data + *pos : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

/* Set in linux_main */
unsigned long uml_physmem;
EXPORT_SYMBOL(uml_physmem);

unsigned long uml_reserved; /* Also modified in mem_init */
unsigned long start_vm;
unsigned long end_vm;

/* Set in early boot */
static int have_root __initdata;
static int have_console __initdata;

/* Set in uml_mem_setup and modified in linux_main */
unsigned long long physmem_size = 64 * 1024 * 1024;
EXPORT_SYMBOL(physmem_size);

static const char *usage_string =
"User Mode Linux v%s\n"
"	available at http://user-mode-linux.sourceforge.net/\n\n";

static int __init uml_version_setup(char *line, int *add)
{
	/* Explicitly use printf() to show version in stdout */
	printf("%s\n", init_utsname()->release);
	exit(0);

	return 0;
}

__uml_setup("--version", uml_version_setup,
"--version\n"
"    Prints the version number of the kernel.\n\n"
);

static int __init uml_root_setup(char *line, int *add)
{
	have_root = 1;
	return 0;
}

__uml_setup("root=", uml_root_setup,
"root=<file containing the root fs>\n"
"    This is actually used by the generic kernel in exactly the same\n"
"    way as in any other kernel. If you configure a number of block\n"
"    devices and want to boot off something other than ubd0, you \n"
"    would use something like:\n"
"        root=/dev/ubd5\n\n"
);

static int __init uml_console_setup(char *line, int *add)
{
	have_console = 1;
	return 0;
}

__uml_setup("console=", uml_console_setup,
"console=<preferred console>\n"
"    Specify the preferred console output driver\n\n"
);

static int __init Usage(char *line, int *add)
{
	const char **p;

	printf(usage_string, init_utsname()->release);
	p = &__uml_help_start;
	/* Explicitly use printf() to show help in stdout */
	while (p < &__uml_help_end) {
		printf("%s", *p);
		p++;
	}
	exit(0);
	return 0;
}

__uml_setup("--help", Usage,
"--help\n"
"    Prints this message.\n\n"
);

static void __init uml_checksetup(char *line, int *add)
{
	struct uml_param *p;

	p = &__uml_setup_start;
	while (p < &__uml_setup_end) {
		size_t n;

		n = strlen(p->str);
		if (!strncmp(line, p->str, n) && p->setup_func(line + n, add))
			return;
		p++;
	}
}

static void __init uml_postsetup(void)
{
	initcall_t *p;

	p = &__uml_postsetup_start;
	while (p < &__uml_postsetup_end) {
		(*p)();
		p++;
	}
	return;
}

static int panic_exit(struct notifier_block *self, unsigned long unused1,
		      void *unused2)
{
	kmsg_dump(KMSG_DUMP_PANIC);
	bust_spinlocks(1);
	bust_spinlocks(0);
	uml_exitcode = 1;
	os_dump_core();

	return NOTIFY_DONE;
}

static struct notifier_block panic_exit_notifier = {
	.notifier_call	= panic_exit,
	.priority	= INT_MAX - 1, /* run as 2nd notifier, won't return */
};

void uml_finishsetup(void)
{
	cpu_tasks[0] = &init_task;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &panic_exit_notifier);

	uml_postsetup();

	new_thread_handler();
}

/* Set during early boot */
unsigned long stub_start;
unsigned long task_size;
EXPORT_SYMBOL(task_size);

unsigned long host_task_size;

unsigned long brk_start;
unsigned long end_iomem;
EXPORT_SYMBOL(end_iomem);

#define MIN_VMALLOC (32 * 1024 * 1024)

static void __init parse_host_cpu_flags(char *line)
{
	int i;
	for (i = 0; i < 32*NCAPINTS; i++) {
		if ((x86_cap_flags[i] != NULL) && strstr(line, x86_cap_flags[i]))
			set_cpu_cap(&boot_cpu_data, i);
	}
}

static void __init parse_cache_line(char *line)
{
	long res;
	char *to_parse = strstr(line, ":");
	if (to_parse) {
		to_parse++;
		while (*to_parse != 0 && isspace(*to_parse)) {
			to_parse++;
		}
		if (kstrtoul(to_parse, 10, &res) == 0 && is_power_of_2(res))
			boot_cpu_data.cache_alignment = res;
		else
			boot_cpu_data.cache_alignment = L1_CACHE_BYTES;
	}
}

static unsigned long __init get_top_address(char **envp)
{
	unsigned long top_addr = (unsigned long) &top_addr;
	int i;

	/* The earliest variable should be after the program name in ELF */
	for (i = 0; envp[i]; i++) {
		if ((unsigned long) envp[i] > top_addr)
			top_addr = (unsigned long) envp[i];
	}

	top_addr &= ~(UM_KERN_PAGE_SIZE - 1);
	top_addr += UM_KERN_PAGE_SIZE;

	return top_addr;
}

int __init linux_main(int argc, char **argv, char **envp)
{
	unsigned long avail, diff;
	unsigned long virtmem_size, max_physmem;
	unsigned long stack;
	unsigned int i;
	int add;

	for (i = 1; i < argc; i++) {
		if ((i == 1) && (argv[i][0] == ' '))
			continue;
		add = 1;
		uml_checksetup(argv[i], &add);
		if (add)
			add_arg(argv[i]);
	}
	if (have_root == 0)
		add_arg(DEFAULT_COMMAND_LINE_ROOT);

	if (have_console == 0)
		add_arg(DEFAULT_COMMAND_LINE_CONSOLE);

	host_task_size = get_top_address(envp);
	/* reserve a few pages for the stubs */
	stub_start = host_task_size - STUB_DATA_PAGES * PAGE_SIZE;
	/* another page for the code portion */
	stub_start -= PAGE_SIZE;
	host_task_size = stub_start;

	/* Limit TASK_SIZE to what is addressable by the page table */
	task_size = host_task_size;
	if (task_size > (unsigned long long) PTRS_PER_PGD * PGDIR_SIZE)
		task_size = PTRS_PER_PGD * PGDIR_SIZE;

	/*
	 * TASK_SIZE needs to be PGDIR_SIZE aligned or else exit_mmap craps
	 * out
	 */
	task_size = task_size & PGDIR_MASK;

	/* OS sanity checks that need to happen before the kernel runs */
	os_early_checks();

	get_host_cpu_features(parse_host_cpu_flags, parse_cache_line);

	brk_start = (unsigned long) sbrk(0);

	/*
	 * Increase physical memory size for exec-shield users
	 * so they actually get what they asked for. This should
	 * add zero for non-exec shield users
	 */

	diff = UML_ROUND_UP(brk_start) - UML_ROUND_UP(&_end);
	if (diff > 1024 * 1024) {
		os_info("Adding %ld bytes to physical memory to account for "
			"exec-shield gap\n", diff);
		physmem_size += UML_ROUND_UP(brk_start) - UML_ROUND_UP(&_end);
	}

	uml_physmem = (unsigned long) __binary_start & PAGE_MASK;

	/* Reserve up to 4M after the current brk */
	uml_reserved = ROUND_4M(brk_start) + (1 << 22);

	setup_machinename(init_utsname()->machine);

	physmem_size = (physmem_size + PAGE_SIZE - 1) & PAGE_MASK;
	iomem_size = (iomem_size + PAGE_SIZE - 1) & PAGE_MASK;

	max_physmem = TASK_SIZE - uml_physmem - iomem_size - MIN_VMALLOC;
	if (physmem_size > max_physmem) {
		physmem_size = max_physmem;
		os_info("Physical memory size shrunk to %llu bytes\n",
			physmem_size);
	}

	high_physmem = uml_physmem + physmem_size;
	end_iomem = high_physmem + iomem_size;

	start_vm = VMALLOC_START;

	virtmem_size = physmem_size;
	stack = (unsigned long) argv;
	stack &= ~(1024 * 1024 - 1);
	avail = stack - start_vm;
	if (physmem_size > avail)
		virtmem_size = avail;
	end_vm = start_vm + virtmem_size;

	if (virtmem_size < physmem_size)
		os_info("Kernel virtual memory size shrunk to %lu bytes\n",
			virtmem_size);

	arch_task_struct_size = sizeof(struct task_struct) + host_fp_size;

	os_flush_stdout();

	return start_uml();
}

int __init __weak read_initrd(void)
{
	return 0;
}

void __init setup_arch(char **cmdline_p)
{
	u8 rng_seed[32];

	stack_protections((unsigned long) init_task.stack);
	setup_physmem(uml_physmem, uml_reserved, physmem_size);
	uml_dtb_init();
	read_initrd();

	paging_init();
	strscpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;
	setup_hostinfo(host_info, sizeof host_info);

	if (os_getrandom(rng_seed, sizeof(rng_seed), 0) == sizeof(rng_seed)) {
		add_bootloader_randomness(rng_seed, sizeof(rng_seed));
		memzero_explicit(rng_seed, sizeof(rng_seed));
	}
}

void __init arch_cpu_finalize_init(void)
{
	arch_check_bugs();
	os_check_bugs();
}

void apply_seal_endbr(s32 *start, s32 *end, struct module *mod)
{
}

void apply_retpolines(s32 *start, s32 *end, struct module *mod)
{
}

void apply_returns(s32 *start, s32 *end, struct module *mod)
{
}

void apply_fineibt(s32 *start_retpoline, s32 *end_retpoline,
		   s32 *start_cfi, s32 *end_cfi, struct module *mod)
{
}

void apply_alternatives(struct alt_instr *start, struct alt_instr *end,
			struct module *mod)
{
}

void *text_poke(void *addr, const void *opcode, size_t len)
{
	/*
	 * In UML, the only reference to this function is in
	 * apply_relocate_add(), which shouldn't ever actually call this
	 * because UML doesn't have live patching.
	 */
	WARN_ON(1);

	return memcpy(addr, opcode, len);
}

void *text_poke_copy(void *addr, const void *opcode, size_t len)
{
	return text_poke(addr, opcode, len);
}

void text_poke_sync(void)
{
}

void uml_pm_wake(void)
{
	pm_system_wakeup();
}

#ifdef CONFIG_PM_SLEEP
static int um_suspend_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM;
}

static int um_suspend_prepare(void)
{
	um_irqs_suspend();
	return 0;
}

static int um_suspend_enter(suspend_state_t state)
{
	if (WARN_ON(state != PM_SUSPEND_MEM))
		return -EINVAL;

	/*
	 * This is identical to the idle sleep, but we've just
	 * (during suspend) turned off all interrupt sources
	 * except for the ones we want, so now we can only wake
	 * up on something we actually want to wake up on. All
	 * timing has also been suspended.
	 */
	um_idle_sleep();
	return 0;
}

static void um_suspend_finish(void)
{
	um_irqs_resume();
}

const struct platform_suspend_ops um_suspend_ops = {
	.valid = um_suspend_valid,
	.prepare = um_suspend_prepare,
	.enter = um_suspend_enter,
	.finish = um_suspend_finish,
};

static int init_pm_wake_signal(void)
{
	/*
	 * In external time-travel mode we can't use signals to wake up
	 * since that would mess with the scheduling. We'll have to do
	 * some additional work to support wakeup on virtio devices or
	 * similar, perhaps implementing a fake RTC controller that can
	 * trigger wakeup (and request the appropriate scheduling from
	 * the external scheduler when going to suspend.)
	 */
	if (time_travel_mode != TT_MODE_EXTERNAL)
		register_pm_wake_signal();

	suspend_set_ops(&um_suspend_ops);

	return 0;
}

late_initcall(init_pm_wake_signal);
#endif
