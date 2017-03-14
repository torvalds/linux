/*
 *  linux/arch/sparc/kernel/setup.c
 *
 *  Copyright (C) 1995  David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 2000  Anton Blanchard (anton@samba.org)
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/initrd.h>
#include <asm/smp.h>
#include <linux/user.h>
#include <linux/screen_info.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/spinlock.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/kdebug.h>
#include <linux/export.h>
#include <linux/start_kernel.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/vaddrs.h>
#include <asm/mbus.h>
#include <asm/idprom.h>
#include <asm/cpudata.h>
#include <asm/setup.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>

#include "kernel.h"

struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	128,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	54,			/* orig-video-lines */
	0,                      /* orig-video-isVGA */
	16                      /* orig-video-points */
};

/* Typing sync at the prom prompt calls the function pointed to by
 * romvec->pv_synchook which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */

/* Pretty sick eh? */
static void prom_sync_me(void)
{
	unsigned long prom_tbr, flags;

	/* XXX Badly broken. FIX! - Anton */
	local_irq_save(flags);
	__asm__ __volatile__("rd %%tbr, %0\n\t" : "=r" (prom_tbr));
	__asm__ __volatile__("wr %0, 0x0, %%tbr\n\t"
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t" : : "r" (&trapbase));

	prom_printf("PROM SYNC COMMAND...\n");
	show_free_areas(0, NULL);
	if (!is_idle_task(current)) {
		local_irq_enable();
		sys_sync();
		local_irq_disable();
	}
	prom_printf("Returning to prom\n");

	__asm__ __volatile__("wr %0, 0x0, %%tbr\n\t"
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t" : : "r" (prom_tbr));
	local_irq_restore(flags);
}

static unsigned int boot_flags __initdata = 0;
#define BOOTME_DEBUG  0x1

/* Exported for mm/init.c:paging_init. */
unsigned long cmdline_memory_size __initdata = 0;

/* which CPU booted us (0xff = not set) */
unsigned char boot_cpu_id = 0xff; /* 0xff will make it into DATA section... */

static void
prom_console_write(struct console *con, const char *s, unsigned int n)
{
	prom_write(s, n);
}

static struct console prom_early_console = {
	.name =		"earlyprom",
	.write =	prom_console_write,
	.flags =	CON_PRINTBUFFER | CON_BOOT,
	.index =	-1,
};

/* 
 * Process kernel command line switches that are specific to the
 * SPARC or that require special low-level processing.
 */
static void __init process_switch(char c)
{
	switch (c) {
	case 'd':
		boot_flags |= BOOTME_DEBUG;
		break;
	case 's':
		break;
	case 'h':
		prom_printf("boot_flags_init: Halt!\n");
		prom_halt();
		break;
	case 'p':
		prom_early_console.flags &= ~CON_BOOT;
		break;
	default:
		printk("Unknown boot switch (-%c)\n", c);
		break;
	}
}

static void __init boot_flags_init(char *commands)
{
	while (*commands) {
		/* Move to the start of the next "argument". */
		while (*commands && *commands == ' ')
			commands++;

		/* Process any command switches, otherwise skip it. */
		if (*commands == '\0')
			break;
		if (*commands == '-') {
			commands++;
			while (*commands && *commands != ' ')
				process_switch(*commands++);
			continue;
		}
		if (!strncmp(commands, "mem=", 4)) {
			/*
			 * "mem=XXX[kKmM] overrides the PROM-reported
			 * memory size.
			 */
			cmdline_memory_size = simple_strtoul(commands + 4,
						     &commands, 0);
			if (*commands == 'K' || *commands == 'k') {
				cmdline_memory_size <<= 10;
				commands++;
			} else if (*commands=='M' || *commands=='m') {
				cmdline_memory_size <<= 20;
				commands++;
			}
		}
		while (*commands && *commands != ' ')
			commands++;
	}
}

extern unsigned short root_flags;
extern unsigned short root_dev;
extern unsigned short ram_flags;
#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

extern int root_mountflags;

char reboot_command[COMMAND_LINE_SIZE];

struct cpuid_patch_entry {
	unsigned int	addr;
	unsigned int	sun4d[3];
	unsigned int	leon[3];
};
extern struct cpuid_patch_entry __cpuid_patch, __cpuid_patch_end;

static void __init per_cpu_patch(void)
{
	struct cpuid_patch_entry *p;

	if (sparc_cpu_model == sun4m) {
		/* Nothing to do, this is what the unpatched code
		 * targets.
		 */
		return;
	}

	p = &__cpuid_patch;
	while (p < &__cpuid_patch_end) {
		unsigned long addr = p->addr;
		unsigned int *insns;

		switch (sparc_cpu_model) {
		case sun4d:
			insns = &p->sun4d[0];
			break;

		case sparc_leon:
			insns = &p->leon[0];
			break;
		default:
			prom_printf("Unknown cpu type, halting.\n");
			prom_halt();
		}
		*(unsigned int *) (addr + 0) = insns[0];
		flushi(addr + 0);
		*(unsigned int *) (addr + 4) = insns[1];
		flushi(addr + 4);
		*(unsigned int *) (addr + 8) = insns[2];
		flushi(addr + 8);

		p++;
	}
}

struct leon_1insn_patch_entry {
	unsigned int addr;
	unsigned int insn;
};

enum sparc_cpu sparc_cpu_model;
EXPORT_SYMBOL(sparc_cpu_model);

static __init void leon_patch(void)
{
	struct leon_1insn_patch_entry *start = (void *)__leon_1insn_patch;
	struct leon_1insn_patch_entry *end = (void *)__leon_1insn_patch_end;

	/* Default instruction is leon - no patching */
	if (sparc_cpu_model == sparc_leon)
		return;

	while (start < end) {
		unsigned long addr = start->addr;

		*(unsigned int *)(addr) = start->insn;
		flushi(addr);

		start++;
	}
}

struct tt_entry *sparc_ttable;
static struct pt_regs fake_swapper_regs;

/* Called from head_32.S - before we have setup anything
 * in the kernel. Be very careful with what you do here.
 */
void __init sparc32_start_kernel(struct linux_romvec *rp)
{
	prom_init(rp);

	/* Set sparc_cpu_model */
	sparc_cpu_model = sun_unknown;
	if (!strcmp(&cputypval[0], "sun4m"))
		sparc_cpu_model = sun4m;
	if (!strcmp(&cputypval[0], "sun4s"))
		sparc_cpu_model = sun4m; /* CP-1200 with PROM 2.30 -E */
	if (!strcmp(&cputypval[0], "sun4d"))
		sparc_cpu_model = sun4d;
	if (!strcmp(&cputypval[0], "sun4e"))
		sparc_cpu_model = sun4e;
	if (!strcmp(&cputypval[0], "sun4u"))
		sparc_cpu_model = sun4u;
	if (!strncmp(&cputypval[0], "leon" , 4))
		sparc_cpu_model = sparc_leon;

	leon_patch();
	start_kernel();
}

void __init setup_arch(char **cmdline_p)
{
	int i;
	unsigned long highest_paddr;

	sparc_ttable = &trapbase;

	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	strlcpy(boot_command_line, *cmdline_p, COMMAND_LINE_SIZE);
	parse_early_param();

	boot_flags_init(*cmdline_p);

	register_console(&prom_early_console);

	printk("ARCH: ");
	switch(sparc_cpu_model) {
	case sun4m:
		printk("SUN4M\n");
		break;
	case sun4d:
		printk("SUN4D\n");
		break;
	case sun4e:
		printk("SUN4E\n");
		break;
	case sun4u:
		printk("SUN4U\n");
		break;
	case sparc_leon:
		printk("LEON\n");
		break;
	default:
		printk("UNKNOWN!\n");
		break;
	}

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	idprom_init();
	load_mmu();

	phys_base = 0xffffffffUL;
	highest_paddr = 0UL;
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long top;

		if (sp_banks[i].base_addr < phys_base)
			phys_base = sp_banks[i].base_addr;
		top = sp_banks[i].base_addr +
			sp_banks[i].num_bytes;
		if (highest_paddr < top)
			highest_paddr = top;
	}
	pfn_base = phys_base >> PAGE_SHIFT;

	if (!root_flags)
		root_mountflags &= ~MS_RDONLY;
	ROOT_DEV = old_decode_dev(root_dev);
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = ram_flags & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((ram_flags & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((ram_flags & RAMDISK_LOAD_FLAG) != 0);	
#endif

	prom_setsync(prom_sync_me);

	if((boot_flags & BOOTME_DEBUG) && (linux_dbvec != NULL) &&
	   ((*(short *)linux_dbvec) != -1)) {
		printk("Booted under KADB. Syncing trap table.\n");
		(*(linux_dbvec->teach_debugger))();
	}

	init_task.thread.kregs = &fake_swapper_regs;

	/* Run-time patch instructions to match the cpu model */
	per_cpu_patch();

	paging_init();

	smp_setup_cpu_possible_map();
}

extern int stop_a_enabled;

void sun_do_break(void)
{
	if (!stop_a_enabled)
		return;

	printk("\n");
	flush_user_windows();

	prom_cmdline();
}
EXPORT_SYMBOL(sun_do_break);

int stop_a_enabled = 1;

static int __init topology_init(void)
{
	int i, ncpus, err;

	/* Count the number of physically present processors in
	 * the machine, even on uniprocessor, so that /proc/cpuinfo
	 * output is consistent with 2.4.x
	 */
	ncpus = 0;
	while (!cpu_find_by_instance(ncpus, NULL, NULL))
		ncpus++;
	ncpus_probed = ncpus;

	err = 0;
	for_each_online_cpu(i) {
		struct cpu *p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			err = -ENOMEM;
		else
			register_cpu(p, i);
	}

	return err;
}

subsys_initcall(topology_init);
