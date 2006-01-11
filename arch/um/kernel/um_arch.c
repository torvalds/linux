/*
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/notifier.h"
#include "linux/mm.h"
#include "linux/types.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "linux/bootmem.h"
#include "linux/spinlock.h"
#include "linux/utsname.h"
#include "linux/sysrq.h"
#include "linux/seq_file.h"
#include "linux/delay.h"
#include "linux/module.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/ptrace.h"
#include "asm/elf.h"
#include "asm/user.h"
#include "asm/setup.h"
#include "ubd_user.h"
#include "asm/current.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "mem_user.h"
#include "mem.h"
#include "umid.h"
#include "initrd.h"
#include "init.h"
#include "os.h"
#include "choose-mode.h"
#include "mode_kern.h"
#include "mode.h"
#ifdef UML_CONFIG_MODE_SKAS
#include "skas.h"
#endif

#define DEFAULT_COMMAND_LINE "root=98:0"

/* Changed in linux_main and setup_arch, which run before SMP is started */
static char command_line[COMMAND_LINE_SIZE] = { 0 };

static void add_arg(char *arg)
{
	if (strlen(command_line) + strlen(arg) + 1 > COMMAND_LINE_SIZE) {
		printf("add_arg: Too many command line arguments!\n");
		exit(1);
	}
	if(strlen(command_line) > 0)
		strcat(command_line, " ");
	strcat(command_line, arg);
}

struct cpuinfo_um boot_cpu_data = { 
	.loops_per_jiffy	= 0,
	.ipi_pipe		= { -1, -1 }
};

unsigned long thread_saved_pc(struct task_struct *task)
{
	return(os_process_pc(CHOOSE_MODE_PROC(thread_pid_tt, thread_pid_skas,
					      task)));
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int index = 0;

#ifdef CONFIG_SMP
	index = (struct cpuinfo_um *) v - cpu_data;
	if (!cpu_online(index))
		return 0;
#endif

	seq_printf(m, "processor\t: %d\n", index);
	seq_printf(m, "vendor_id\t: User Mode Linux\n");
	seq_printf(m, "model name\t: UML\n");
	seq_printf(m, "mode\t\t: %s\n", CHOOSE_MODE("tt", "skas"));
	seq_printf(m, "host\t\t: %s\n", host_info);
	seq_printf(m, "bogomips\t: %lu.%02lu\n\n",
		   loops_per_jiffy/(500000/HZ),
		   (loops_per_jiffy/(5000/HZ)) % 100);

	return(0);
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

/* Set in linux_main */
unsigned long host_task_size;
unsigned long task_size;

unsigned long uml_start;

/* Set in early boot */
unsigned long uml_physmem;
unsigned long uml_reserved;
unsigned long start_vm;
unsigned long end_vm;
int ncpus = 1;

#ifdef CONFIG_CMDLINE_ON_HOST
/* Pointer set in linux_main, the array itself is private to each thread,
 * and changed at address space creation time so this poses no concurrency
 * problems.
 */
static char *argv1_begin = NULL;
static char *argv1_end = NULL;
#endif

/* Set in early boot */
static int have_root __initdata = 0;
long long physmem_size = 32 * 1024 * 1024;

void set_cmdline(char *cmd)
{
#ifdef CONFIG_CMDLINE_ON_HOST
	char *umid, *ptr;

	if(CHOOSE_MODE(honeypot, 0)) return;

	umid = get_umid();
	if(*umid != '\0'){
		snprintf(argv1_begin, 
			 (argv1_end - argv1_begin) * sizeof(*ptr), 
			 "(%s) ", umid);
		ptr = &argv1_begin[strlen(argv1_begin)];
	}
	else ptr = argv1_begin;

	snprintf(ptr, (argv1_end - ptr) * sizeof(*ptr), "[%s]", cmd);
	memset(argv1_begin + strlen(argv1_begin), '\0', 
	       argv1_end - argv1_begin - strlen(argv1_begin));
#endif
}

static char *usage_string = 
"User Mode Linux v%s\n"
"	available at http://user-mode-linux.sourceforge.net/\n\n";

static int __init uml_version_setup(char *line, int *add)
{
	printf("%s\n", system_utsname.release);
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

#ifdef CONFIG_SMP
static int __init uml_ncpus_setup(char *line, int *add)
{
       if (!sscanf(line, "%d", &ncpus)) {
               printf("Couldn't parse [%s]\n", line);
               return -1;
       }

       return 0;
}

__uml_setup("ncpus=", uml_ncpus_setup,
"ncpus=<# of desired CPUs>\n"
"    This tells an SMP kernel how many virtual processors to start.\n\n" 
);
#endif

static int force_tt = 0;

#if defined(CONFIG_MODE_TT) && defined(CONFIG_MODE_SKAS)
#define DEFAULT_TT 0

static int __init mode_tt_setup(char *line, int *add)
{
	force_tt = 1;
	return(0);
}

#else
#ifdef CONFIG_MODE_SKAS

#define DEFAULT_TT 0

static int __init mode_tt_setup(char *line, int *add)
{
	printf("CONFIG_MODE_TT disabled - 'mode=tt' ignored\n");
	return(0);
}

#else
#ifdef CONFIG_MODE_TT

#define DEFAULT_TT 1

static int __init mode_tt_setup(char *line, int *add)
{
	printf("CONFIG_MODE_SKAS disabled - 'mode=tt' redundant\n");
	return(0);
}

#endif
#endif
#endif

__uml_setup("mode=tt", mode_tt_setup,
"mode=tt\n"
"    When both CONFIG_MODE_TT and CONFIG_MODE_SKAS are enabled, this option\n"
"    forces UML to run in tt (tracing thread) mode.  It is not the default\n"
"    because it's slower and less secure than skas mode.\n\n"
);

int mode_tt = DEFAULT_TT;

static int __init Usage(char *line, int *add)
{
 	const char **p;

	printf(usage_string, system_utsname.release);
 	p = &__uml_help_start;
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

static int __init uml_checksetup(char *line, int *add)
{
	struct uml_param *p;

	p = &__uml_setup_start;
	while(p < &__uml_setup_end) {
		int n;

		n = strlen(p->str);
		if(!strncmp(line, p->str, n)){
			if (p->setup_func(line + n, add)) return 1;
		}
		p++;
	}
	return 0;
}

static void __init uml_postsetup(void)
{
	initcall_t *p;

	p = &__uml_postsetup_start;
	while(p < &__uml_postsetup_end){
		(*p)();
		p++;
	}
	return;
}

/* Set during early boot */
unsigned long brk_start;
unsigned long end_iomem;
EXPORT_SYMBOL(end_iomem);

#define MIN_VMALLOC (32 * 1024 * 1024)

int linux_main(int argc, char **argv)
{
	unsigned long avail, diff;
	unsigned long virtmem_size, max_physmem;
	unsigned int i, add;
	char * mode;

	for (i = 1; i < argc; i++){
		if((i == 1) && (argv[i][0] == ' ')) continue;
		add = 1;
		uml_checksetup(argv[i], &add);
		if (add)
			add_arg(argv[i]);
	}
	if(have_root == 0)
		add_arg(DEFAULT_COMMAND_LINE);

	os_early_checks();
	if (force_tt)
		clear_can_do_skas();
	mode_tt = force_tt ? 1 : !can_do_skas();
#ifndef CONFIG_MODE_TT
	if (mode_tt) {
		/*Since CONFIG_MODE_TT is #undef'ed, force_tt cannot be 1. So,
		 * can_do_skas() returned 0, and the message is correct. */
		printf("Support for TT mode is disabled, and no SKAS support is present on the host.\n");
		exit(1);
	}
#endif

#ifndef CONFIG_MODE_SKAS
	mode = "TT";
#else
	/* Show to the user the result of selection */
	if (mode_tt)
		mode = "TT";
	else if (proc_mm && ptrace_faultinfo)
		mode = "SKAS3";
	else
		mode = "SKAS0";
#endif

	printf("UML running in %s mode\n", mode);

	uml_start = CHOOSE_MODE_PROC(set_task_sizes_tt, set_task_sizes_skas, 0,
				     &host_task_size, &task_size);

	/*
 	 * Setting up handlers to 'sig_info' struct
 	 */
	os_fill_handlinfo(handlinfo_kern);

	brk_start = (unsigned long) sbrk(0);
	CHOOSE_MODE_PROC(before_mem_tt, before_mem_skas, brk_start);
	/* Increase physical memory size for exec-shield users
	so they actually get what they asked for. This should
	add zero for non-exec shield users */

	diff = UML_ROUND_UP(brk_start) - UML_ROUND_UP(&_end);
	if(diff > 1024 * 1024){
		printf("Adding %ld bytes to physical memory to account for "
		       "exec-shield gap\n", diff);
		physmem_size += UML_ROUND_UP(brk_start) - UML_ROUND_UP(&_end);
	}

	uml_physmem = uml_start;

	/* Reserve up to 4M after the current brk */
	uml_reserved = ROUND_4M(brk_start) + (1 << 22);

	setup_machinename(system_utsname.machine);

#ifdef CONFIG_CMDLINE_ON_HOST
	argv1_begin = argv[1];
	argv1_end = &argv[1][strlen(argv[1])];
#endif
  
	highmem = 0;
	iomem_size = (iomem_size + PAGE_SIZE - 1) & PAGE_MASK;
	max_physmem = get_kmem_end() - uml_physmem - iomem_size - MIN_VMALLOC;

	/* Zones have to begin on a 1 << MAX_ORDER page boundary,
	 * so this makes sure that's true for highmem
	 */
	max_physmem &= ~((1 << (PAGE_SHIFT + MAX_ORDER)) - 1);
	if(physmem_size + iomem_size > max_physmem){
		highmem = physmem_size + iomem_size - max_physmem;
		physmem_size -= highmem;
#ifndef CONFIG_HIGHMEM
		highmem = 0;
		printf("CONFIG_HIGHMEM not enabled - physical memory shrunk "
		       "to %lu bytes\n", physmem_size);
#endif
	}

	high_physmem = uml_physmem + physmem_size;
	end_iomem = high_physmem + iomem_size;
	high_memory = (void *) end_iomem;

	start_vm = VMALLOC_START;

	setup_physmem(uml_physmem, uml_reserved, physmem_size, highmem);
	if(init_maps(physmem_size, iomem_size, highmem)){
		printf("Failed to allocate mem_map for %lu bytes of physical "
		       "memory and %lu bytes of highmem\n", physmem_size,
		       highmem);
		exit(1);
	}

	virtmem_size = physmem_size;
	avail = get_kmem_end() - start_vm;
	if(physmem_size > avail) virtmem_size = avail;
	end_vm = start_vm + virtmem_size;

	if(virtmem_size < physmem_size)
		printf("Kernel virtual memory size shrunk to %lu bytes\n",
		       virtmem_size);

  	uml_postsetup();

	task_protections((unsigned long) &init_thread_info);
	os_flush_stdout();

	return(CHOOSE_MODE(start_uml_tt(), start_uml_skas()));
}

extern int uml_exitcode;

static int panic_exit(struct notifier_block *self, unsigned long unused1,
		      void *unused2)
{
	bust_spinlocks(1);
	show_regs(&(current->thread.regs));
	bust_spinlocks(0);
	uml_exitcode = 1;
	machine_halt();
	return(0);
}

static struct notifier_block panic_exit_notifier = {
	.notifier_call 		= panic_exit,
	.next 			= NULL,
	.priority 		= 0
};

void __init setup_arch(char **cmdline_p)
{
	notifier_chain_register(&panic_notifier_list, &panic_exit_notifier);
	paging_init();
        strlcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);
 	*cmdline_p = command_line;
	setup_hostinfo();
}

void __init check_bugs(void)
{
	arch_check_bugs();
	check_sigio();
	check_devanon();
}

void apply_alternatives(void *start, void *end)
{
}
