/*  $Id: setup.c,v 1.72 2002/02/09 19:49:30 davem Exp $
 *  linux/arch/sparc64/kernel/setup.c
 *
 *  Copyright (C) 1995,1996  David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997       Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/inet.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/initrd.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/idprom.h>
#include <asm/head.h>
#include <asm/starfire.h>
#include <asm/mmu_context.h>
#include <asm/timer.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/mmu.h>

#ifdef CONFIG_IP_PNP
#include <net/ipconfig.h>
#endif

struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	128,			/* orig-video-cols */
	0, 0, 0,		/* unused, ega_bx, unused */
	54,			/* orig-video-lines */
	0,                      /* orig-video-isVGA */
	16                      /* orig-video-points */
};

/* Typing sync at the prom prompt calls the function pointed to by
 * the sync callback which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */

void (*prom_palette)(int);
void (*prom_keyboard)(void);

static void
prom_console_write(struct console *con, const char *s, unsigned n)
{
	prom_write(s, n);
}

static struct console prom_console = {
	.name =		"prom",
	.write =	prom_console_write,
	.flags =	CON_CONSDEV | CON_ENABLED,
	.index =	-1,
};

#define PROM_TRUE	-1
#define PROM_FALSE	0

/* Pretty sick eh? */
int prom_callback(long *args)
{
	struct console *cons, *saved_console = NULL;
	unsigned long flags;
	char *cmd;
	extern spinlock_t prom_entry_lock;

	if (!args)
		return -1;
	if (!(cmd = (char *)args[0]))
		return -1;

	/*
	 * The callback can be invoked on the cpu that first dropped 
	 * into prom_cmdline after taking the serial interrupt, or on 
	 * a slave processor that was smp_captured() if the 
	 * administrator has done a switch-cpu inside obp. In either 
	 * case, the cpu is marked as in-interrupt. Drop IRQ locks.
	 */
	irq_exit();

	/* XXX Revisit the locking here someday.  This is a debugging
	 * XXX feature so it isnt all that critical.  -DaveM
	 */
	local_irq_save(flags);

	spin_unlock(&prom_entry_lock);
	cons = console_drivers;
	while (cons) {
		unregister_console(cons);
		cons->flags &= ~(CON_PRINTBUFFER);
		cons->next = saved_console;
		saved_console = cons;
		cons = console_drivers;
	}
	register_console(&prom_console);
	if (!strcmp(cmd, "sync")) {
		prom_printf("PROM `%s' command...\n", cmd);
		show_free_areas();
		if (current->pid != 0) {
			local_irq_enable();
			sys_sync();
			local_irq_disable();
		}
		args[2] = 0;
		args[args[1] + 3] = -1;
		prom_printf("Returning to PROM\n");
	} else if (!strcmp(cmd, "va>tte-data")) {
		unsigned long ctx, va;
		unsigned long tte = 0;
		long res = PROM_FALSE;

		ctx = args[3];
		va = args[4];
		if (ctx) {
			/*
			 * Find process owning ctx, lookup mapping.
			 */
			struct task_struct *p;
			struct mm_struct *mm = NULL;
			pgd_t *pgdp;
			pud_t *pudp;
			pmd_t *pmdp;
			pte_t *ptep;
			pte_t pte;

			for_each_process(p) {
				mm = p->mm;
				if (CTX_NRBITS(mm->context) == ctx)
					break;
			}
			if (!mm ||
			    CTX_NRBITS(mm->context) != ctx)
				goto done;

			pgdp = pgd_offset(mm, va);
			if (pgd_none(*pgdp))
				goto done;
			pudp = pud_offset(pgdp, va);
			if (pud_none(*pudp))
				goto done;
			pmdp = pmd_offset(pudp, va);
			if (pmd_none(*pmdp))
				goto done;

			/* Preemption implicitly disabled by virtue of
			 * being called from inside OBP.
			 */
			ptep = pte_offset_map(pmdp, va);
			pte = *ptep;
			if (pte_present(pte)) {
				tte = pte_val(pte);
				res = PROM_TRUE;
			}
			pte_unmap(ptep);
			goto done;
		}

		if ((va >= KERNBASE) && (va < (KERNBASE + (4 * 1024 * 1024)))) {
			extern unsigned long sparc64_kern_pri_context;

			/* Spitfire Errata #32 workaround */
			__asm__ __volatile__("stxa	%0, [%1] %2\n\t"
					     "flush	%%g6"
					     : /* No outputs */
					     : "r" (sparc64_kern_pri_context),
					       "r" (PRIMARY_CONTEXT),
					       "i" (ASI_DMMU));

			/*
			 * Locked down tlb entry.
			 */

			if (tlb_type == spitfire)
				tte = spitfire_get_dtlb_data(SPITFIRE_HIGHEST_LOCKED_TLBENT);
			else if (tlb_type == cheetah || tlb_type == cheetah_plus)
				tte = cheetah_get_ldtlb_data(CHEETAH_HIGHEST_LOCKED_TLBENT);

			res = PROM_TRUE;
			goto done;
		}

		if (va < PGDIR_SIZE) {
			/*
			 * vmalloc or prom_inherited mapping.
			 */
			pgd_t *pgdp;
			pud_t *pudp;
			pmd_t *pmdp;
			pte_t *ptep;
			pte_t pte;
			int error;

			if ((va >= LOW_OBP_ADDRESS) && (va < HI_OBP_ADDRESS)) {
				tte = prom_virt_to_phys(va, &error);
				if (!error)
					res = PROM_TRUE;
				goto done;
			}
			pgdp = pgd_offset_k(va);
			if (pgd_none(*pgdp))
				goto done;
			pudp = pud_offset(pgdp, va);
			if (pud_none(*pudp))
				goto done;
			pmdp = pmd_offset(pudp, va);
			if (pmd_none(*pmdp))
				goto done;

			/* Preemption implicitly disabled by virtue of
			 * being called from inside OBP.
			 */
			ptep = pte_offset_kernel(pmdp, va);
			pte = *ptep;
			if (pte_present(pte)) {
				tte = pte_val(pte);
				res = PROM_TRUE;
			}
			goto done;
		}

		if (va < PAGE_OFFSET) {
			/*
			 * No mappings here.
			 */
			goto done;
		}

		if (va & (1UL << 40)) {
			/*
			 * I/O page.
			 */

			tte = (__pa(va) & _PAGE_PADDR) |
			      _PAGE_VALID | _PAGE_SZ4MB |
			      _PAGE_E | _PAGE_P | _PAGE_W;
			res = PROM_TRUE;
			goto done;
		}

		/*
		 * Normal page.
		 */
		tte = (__pa(va) & _PAGE_PADDR) |
		      _PAGE_VALID | _PAGE_SZ4MB |
		      _PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W;
		res = PROM_TRUE;

	done:
		if (res == PROM_TRUE) {
			args[2] = 3;
			args[args[1] + 3] = 0;
			args[args[1] + 4] = res;
			args[args[1] + 5] = tte;
		} else {
			args[2] = 2;
			args[args[1] + 3] = 0;
			args[args[1] + 4] = res;
		}
	} else if (!strcmp(cmd, ".soft1")) {
		unsigned long tte;

		tte = args[3];
		prom_printf("%lx:\"%s%s%s%s%s\" ",
			    (tte & _PAGE_SOFT) >> 7,
			    tte & _PAGE_MODIFIED ? "M" : "-",
			    tte & _PAGE_ACCESSED ? "A" : "-",
			    tte & _PAGE_READ     ? "W" : "-",
			    tte & _PAGE_WRITE    ? "R" : "-",
			    tte & _PAGE_PRESENT  ? "P" : "-");

		args[2] = 2;
		args[args[1] + 3] = 0;
		args[args[1] + 4] = PROM_TRUE;
	} else if (!strcmp(cmd, ".soft2")) {
		unsigned long tte;

		tte = args[3];
		prom_printf("%lx ", (tte & 0x07FC000000000000UL) >> 50);

		args[2] = 2;
		args[args[1] + 3] = 0;
		args[args[1] + 4] = PROM_TRUE;
	} else {
		prom_printf("unknown PROM `%s' command...\n", cmd);
	}
	unregister_console(&prom_console);
	while (saved_console) {
		cons = saved_console;
		saved_console = cons->next;
		register_console(cons);
	}
	spin_lock(&prom_entry_lock);
	local_irq_restore(flags);

	/*
	 * Restore in-interrupt status for a resume from obp.
	 */
	irq_enter();
	return 0;
}

unsigned int boot_flags = 0;
#define BOOTME_DEBUG  0x1
#define BOOTME_SINGLE 0x2

/* Exported for mm/init.c:paging_init. */
unsigned long cmdline_memory_size = 0;

static struct console prom_debug_console = {
	.name =		"debug",
	.write =	prom_console_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

/* XXX Implement this at some point... */
void kernel_enter_debugger(void)
{
}

int obp_system_intr(void)
{
	if (boot_flags & BOOTME_DEBUG) {
		printk("OBP: system interrupted\n");
		prom_halt();
		return 1;
	}
	return 0;
}

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
		boot_flags |= BOOTME_SINGLE;
		break;
	case 'h':
		prom_printf("boot_flags_init: Halt!\n");
		prom_halt();
		break;
	case 'p':
		/* Use PROM debug console. */
		register_console(&prom_debug_console);
		break;
	case 'P':
		/* Force UltraSPARC-III P-Cache on. */
		if (tlb_type != cheetah) {
			printk("BOOT: Ignoring P-Cache force option.\n");
			break;
		}
		cheetah_pcache_forced_on = 1;
		add_taint(TAINT_MACHINE_CHECK);
		cheetah_enable_pcache();
		break;

	default:
		printk("Unknown boot switch (-%c)\n", c);
		break;
	}
}

static void __init process_console(char *commands)
{
	serial_console = 0;
	commands += 8;
	/* Linux-style serial */
	if (!strncmp(commands, "ttyS", 4))
		serial_console = simple_strtoul(commands + 4, NULL, 10) + 1;
	else if (!strncmp(commands, "tty", 3)) {
		char c = *(commands + 3);
		/* Solaris-style serial */
		if (c == 'a' || c == 'b') {
			serial_console = c - 'a' + 1;
			prom_printf ("Using /dev/tty%c as console.\n", c);
		}
		/* else Linux-style fbcon, not serial */
	}
#if defined(CONFIG_PROM_CONSOLE)
	if (!strncmp(commands, "prom", 4)) {
		char *p;

		for (p = commands - 8; *p && *p != ' '; p++)
			*p = ' ';
		conswitchp = &prom_con;
	}
#endif
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
		if (!strncmp(commands, "console=", 8)) {
			process_console(commands);
		} else if (!strncmp(commands, "mem=", 4)) {
			/*
			 * "mem=XXX[kKmM]" overrides the PROM-reported
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

extern void panic_setup(char *, int *);

extern unsigned short root_flags;
extern unsigned short root_dev;
extern unsigned short ram_flags;
#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

extern int root_mountflags;

char reboot_command[COMMAND_LINE_SIZE];

static struct pt_regs fake_swapper_regs = { { 0, }, 0, 0, 0, 0 };

void register_prom_callbacks(void)
{
	prom_setcallback(prom_callback);
	prom_feval(": linux-va>tte-data 2 \" va>tte-data\" $callback drop ; "
		   "' linux-va>tte-data to va>tte-data");
	prom_feval(": linux-.soft1 1 \" .soft1\" $callback 2drop ; "
		   "' linux-.soft1 to .soft1");
	prom_feval(": linux-.soft2 1 \" .soft2\" $callback 2drop ; "
		   "' linux-.soft2 to .soft2");
}

void __init setup_arch(char **cmdline_p)
{
	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	strcpy(saved_command_line, *cmdline_p);

	printk("ARCH: SUN4U\n");

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#elif defined(CONFIG_PROM_CONSOLE)
	conswitchp = &prom_con;
#endif

	/* Work out if we are starfire early on */
	check_if_starfire();

	boot_flags_init(*cmdline_p);

	idprom_init();

	if (!root_flags)
		root_mountflags &= ~MS_RDONLY;
	ROOT_DEV = old_decode_dev(root_dev);
#ifdef CONFIG_BLK_DEV_INITRD
	rd_image_start = ram_flags & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((ram_flags & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((ram_flags & RAMDISK_LOAD_FLAG) != 0);	
#endif

	task_thread_info(&init_task)->kregs = &fake_swapper_regs;

#ifdef CONFIG_IP_PNP
	if (!ic_set_manually) {
		int chosen = prom_finddevice ("/chosen");
		u32 cl, sv, gw;
		
		cl = prom_getintdefault (chosen, "client-ip", 0);
		sv = prom_getintdefault (chosen, "server-ip", 0);
		gw = prom_getintdefault (chosen, "gateway-ip", 0);
		if (cl && sv) {
			ic_myaddr = cl;
			ic_servaddr = sv;
			if (gw)
				ic_gateway = gw;
#if defined(CONFIG_IP_PNP_BOOTP) || defined(CONFIG_IP_PNP_RARP)
			ic_proto_enabled = 0;
#endif
		}
	}
#endif

	smp_setup_cpu_possible_map();

	paging_init();
}

static int __init set_preferred_console(void)
{
	int idev, odev;

	/* The user has requested a console so this is already set up. */
	if (serial_console >= 0)
		return -EBUSY;

	idev = prom_query_input_device();
	odev = prom_query_output_device();
	if (idev == PROMDEV_IKBD && odev == PROMDEV_OSCREEN) {
		serial_console = 0;
	} else if (idev == PROMDEV_ITTYA && odev == PROMDEV_OTTYA) {
		serial_console = 1;
	} else if (idev == PROMDEV_ITTYB && odev == PROMDEV_OTTYB) {
		serial_console = 2;
	} else if (idev == PROMDEV_IRSC && odev == PROMDEV_ORSC) {
		serial_console = 3;
	} else {
		prom_printf("Inconsistent console: "
			    "input %d, output %d\n",
			    idev, odev);
		prom_halt();
	}

	if (serial_console)
		return add_preferred_console("ttyS", serial_console - 1, NULL);

	return -ENODEV;
}
console_initcall(set_preferred_console);

/* BUFFER is PAGE_SIZE bytes long. */

extern char *sparc_cpu_type;
extern char *sparc_fpu_type;

extern void smp_info(struct seq_file *);
extern void smp_bogo(struct seq_file *);
extern void mmu_info(struct seq_file *);

unsigned int dcache_parity_tl1_occurred;
unsigned int icache_parity_tl1_occurred;

static int ncpus_probed;

static int show_cpuinfo(struct seq_file *m, void *__unused)
{
	seq_printf(m, 
		   "cpu\t\t: %s\n"
		   "fpu\t\t: %s\n"
		   "promlib\t\t: Version 3 Revision %d\n"
		   "prom\t\t: %d.%d.%d\n"
		   "type\t\t: sun4u\n"
		   "ncpus probed\t: %d\n"
		   "ncpus active\t: %d\n"
		   "D$ parity tl1\t: %u\n"
		   "I$ parity tl1\t: %u\n"
#ifndef CONFIG_SMP
		   "Cpu0Bogo\t: %lu.%02lu\n"
		   "Cpu0ClkTck\t: %016lx\n"
#endif
		   ,
		   sparc_cpu_type,
		   sparc_fpu_type,
		   prom_rev,
		   prom_prev >> 16,
		   (prom_prev >> 8) & 0xff,
		   prom_prev & 0xff,
		   ncpus_probed,
		   num_online_cpus(),
		   dcache_parity_tl1_occurred,
		   icache_parity_tl1_occurred
#ifndef CONFIG_SMP
		   , cpu_data(0).udelay_val/(500000/HZ),
		   (cpu_data(0).udelay_val/(5000/HZ)) % 100,
		   cpu_data(0).clock_tick
#endif
		);
#ifdef CONFIG_SMP
	smp_bogo(m);
#endif
	mmu_info(m);
#ifdef CONFIG_SMP
	smp_info(m);
#endif
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	/* The pointer we are returning is arbitrary,
	 * it just has to be non-NULL and not IS_ERR
	 * in the success case.
	 */
	return *pos == 0 ? &c_start : NULL;
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
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

extern int stop_a_enabled;

void sun_do_break(void)
{
	if (!stop_a_enabled)
		return;

	prom_printf("\n");
	flush_user_windows();

	prom_cmdline();
}

int serial_console = -1;
int stop_a_enabled = 1;

static int __init topology_init(void)
{
	int i, err;

	err = -ENOMEM;

	/* Count the number of physically present processors in
	 * the machine, even on uniprocessor, so that /proc/cpuinfo
	 * output is consistent with 2.4.x
	 */
	ncpus_probed = 0;
	while (!cpu_find_by_instance(ncpus_probed, NULL, NULL))
		ncpus_probed++;

	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_possible(i)) {
			struct cpu *p = kmalloc(sizeof(*p), GFP_KERNEL);

			if (p) {
				memset(p, 0, sizeof(*p));
				register_cpu(p, i, NULL);
				err = 0;
			}
		}
	}

	return err;
}

subsys_initcall(topology_init);
