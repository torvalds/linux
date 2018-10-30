// SPDX-License-Identifier: GPL-2.0
/*
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
#include <linux/inet.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/start_kernel.h>
#include <linux/bootmem.h>

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
#include <asm/ns87303.h>
#include <asm/btext.h>
#include <asm/elf.h>
#include <asm/mdesc.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/irq.h>

#ifdef CONFIG_IP_PNP
#include <net/ipconfig.h>
#endif

#include "entry.h"
#include "kernel.h"

/* Used to synchronize accesses to NatSemi SUPER I/O chip configure
 * operations in asm/ns87303.h
 */
DEFINE_SPINLOCK(ns87303_lock);
EXPORT_SYMBOL(ns87303_lock);

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

static void
prom_console_write(struct console *con, const char *s, unsigned int n)
{
	prom_write(s, n);
}

/* Exported for mm/init.c:paging_init. */
unsigned long cmdline_memory_size = 0;

static struct console prom_early_console = {
	.name =		"earlyprom",
	.write =	prom_console_write,
	.flags =	CON_PRINTBUFFER | CON_BOOT | CON_ANYTIME,
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
	case 's':
		break;
	case 'h':
		prom_printf("boot_flags_init: Halt!\n");
		prom_halt();
		break;
	case 'p':
		prom_early_console.flags &= ~CON_BOOT;
		break;
	case 'P':
		/* Force UltraSPARC-III P-Cache on. */
		if (tlb_type != cheetah) {
			printk("BOOT: Ignoring P-Cache force option.\n");
			break;
		}
		cheetah_pcache_forced_on = 1;
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_NOW_UNRELIABLE);
		cheetah_enable_pcache();
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
		while (*commands == ' ')
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
		if (!strncmp(commands, "mem=", 4))
			cmdline_memory_size = memparse(commands + 4, &commands);

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

static struct pt_regs fake_swapper_regs = { { 0, }, 0, 0, 0, 0 };

static void __init per_cpu_patch(void)
{
	struct cpuid_patch_entry *p;
	unsigned long ver;
	int is_jbus;

	if (tlb_type == spitfire && !this_is_starfire)
		return;

	is_jbus = 0;
	if (tlb_type != hypervisor) {
		__asm__ ("rdpr %%ver, %0" : "=r" (ver));
		is_jbus = ((ver >> 32UL) == __JALAPENO_ID ||
			   (ver >> 32UL) == __SERRANO_ID);
	}

	p = &__cpuid_patch;
	while (p < &__cpuid_patch_end) {
		unsigned long addr = p->addr;
		unsigned int *insns;

		switch (tlb_type) {
		case spitfire:
			insns = &p->starfire[0];
			break;
		case cheetah:
		case cheetah_plus:
			if (is_jbus)
				insns = &p->cheetah_jbus[0];
			else
				insns = &p->cheetah_safari[0];
			break;
		case hypervisor:
			insns = &p->sun4v[0];
			break;
		default:
			prom_printf("Unknown cpu type, halting.\n");
			prom_halt();
		}

		*(unsigned int *) (addr +  0) = insns[0];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  0));

		*(unsigned int *) (addr +  4) = insns[1];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  4));

		*(unsigned int *) (addr +  8) = insns[2];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  8));

		*(unsigned int *) (addr + 12) = insns[3];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr + 12));

		p++;
	}
}

void sun4v_patch_1insn_range(struct sun4v_1insn_patch_entry *start,
			     struct sun4v_1insn_patch_entry *end)
{
	while (start < end) {
		unsigned long addr = start->addr;

		*(unsigned int *) (addr +  0) = start->insn;
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  0));

		start++;
	}
}

void sun4v_patch_2insn_range(struct sun4v_2insn_patch_entry *start,
			     struct sun4v_2insn_patch_entry *end)
{
	while (start < end) {
		unsigned long addr = start->addr;

		*(unsigned int *) (addr +  0) = start->insns[0];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  0));

		*(unsigned int *) (addr +  4) = start->insns[1];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  4));

		start++;
	}
}

void sun_m7_patch_2insn_range(struct sun4v_2insn_patch_entry *start,
			     struct sun4v_2insn_patch_entry *end)
{
	while (start < end) {
		unsigned long addr = start->addr;

		*(unsigned int *) (addr +  0) = start->insns[0];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  0));

		*(unsigned int *) (addr +  4) = start->insns[1];
		wmb();
		__asm__ __volatile__("flush	%0" : : "r" (addr +  4));

		start++;
	}
}

static void __init sun4v_patch(void)
{
	extern void sun4v_hvapi_init(void);

	if (tlb_type != hypervisor)
		return;

	sun4v_patch_1insn_range(&__sun4v_1insn_patch,
				&__sun4v_1insn_patch_end);

	sun4v_patch_2insn_range(&__sun4v_2insn_patch,
				&__sun4v_2insn_patch_end);

	switch (sun4v_chip_type) {
	case SUN4V_CHIP_SPARC_M7:
	case SUN4V_CHIP_SPARC_M8:
	case SUN4V_CHIP_SPARC_SN:
		sun4v_patch_1insn_range(&__sun_m7_1insn_patch,
					&__sun_m7_1insn_patch_end);
		sun_m7_patch_2insn_range(&__sun_m7_2insn_patch,
					 &__sun_m7_2insn_patch_end);
		break;
	default:
		break;
	}

	if (sun4v_chip_type != SUN4V_CHIP_NIAGARA1) {
		sun4v_patch_1insn_range(&__fast_win_ctrl_1insn_patch,
					&__fast_win_ctrl_1insn_patch_end);
	}

	sun4v_hvapi_init();
}

static void __init popc_patch(void)
{
	struct popc_3insn_patch_entry *p3;
	struct popc_6insn_patch_entry *p6;

	p3 = &__popc_3insn_patch;
	while (p3 < &__popc_3insn_patch_end) {
		unsigned long i, addr = p3->addr;

		for (i = 0; i < 3; i++) {
			*(unsigned int *) (addr +  (i * 4)) = p3->insns[i];
			wmb();
			__asm__ __volatile__("flush	%0"
					     : : "r" (addr +  (i * 4)));
		}

		p3++;
	}

	p6 = &__popc_6insn_patch;
	while (p6 < &__popc_6insn_patch_end) {
		unsigned long i, addr = p6->addr;

		for (i = 0; i < 6; i++) {
			*(unsigned int *) (addr +  (i * 4)) = p6->insns[i];
			wmb();
			__asm__ __volatile__("flush	%0"
					     : : "r" (addr +  (i * 4)));
		}

		p6++;
	}
}

static void __init pause_patch(void)
{
	struct pause_patch_entry *p;

	p = &__pause_3insn_patch;
	while (p < &__pause_3insn_patch_end) {
		unsigned long i, addr = p->addr;

		for (i = 0; i < 3; i++) {
			*(unsigned int *) (addr +  (i * 4)) = p->insns[i];
			wmb();
			__asm__ __volatile__("flush	%0"
					     : : "r" (addr +  (i * 4)));
		}

		p++;
	}
}

void __init start_early_boot(void)
{
	int cpu;

	check_if_starfire();
	per_cpu_patch();
	sun4v_patch();
	smp_init_cpu_poke();

	cpu = hard_smp_processor_id();
	if (cpu >= NR_CPUS) {
		prom_printf("Serious problem, boot cpu id (%d) >= NR_CPUS (%d)\n",
			    cpu, NR_CPUS);
		prom_halt();
	}
	current_thread_info()->cpu = cpu;

	time_init_early();
	prom_init_report();
	start_kernel();
}

/* On Ultra, we support all of the v8 capabilities. */
unsigned long sparc64_elf_hwcap = (HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR |
				   HWCAP_SPARC_SWAP | HWCAP_SPARC_MULDIV |
				   HWCAP_SPARC_V9);
EXPORT_SYMBOL(sparc64_elf_hwcap);

static const char *hwcaps[] = {
	"flush", "stbar", "swap", "muldiv", "v9",
	"ultra3", "blkinit", "n2",

	/* These strings are as they appear in the machine description
	 * 'hwcap-list' property for cpu nodes.
	 */
	"mul32", "div32", "fsmuld", "v8plus", "popc", "vis", "vis2",
	"ASIBlkInit", "fmaf", "vis3", "hpc", "random", "trans", "fjfmau",
	"ima", "cspare", "pause", "cbcond", NULL /*reserved for crypto */,
	"adp",
};

static const char *crypto_hwcaps[] = {
	"aes", "des", "kasumi", "camellia", "md5", "sha1", "sha256",
	"sha512", "mpmul", "montmul", "montsqr", "crc32c",
};

void cpucap_info(struct seq_file *m)
{
	unsigned long caps = sparc64_elf_hwcap;
	int i, printed = 0;

	seq_puts(m, "cpucaps\t\t: ");
	for (i = 0; i < ARRAY_SIZE(hwcaps); i++) {
		unsigned long bit = 1UL << i;
		if (hwcaps[i] && (caps & bit)) {
			seq_printf(m, "%s%s",
				   printed ? "," : "", hwcaps[i]);
			printed++;
		}
	}
	if (caps & HWCAP_SPARC_CRYPTO) {
		unsigned long cfr;

		__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
		for (i = 0; i < ARRAY_SIZE(crypto_hwcaps); i++) {
			unsigned long bit = 1UL << i;
			if (cfr & bit) {
				seq_printf(m, "%s%s",
					   printed ? "," : "", crypto_hwcaps[i]);
				printed++;
			}
		}
	}
	seq_putc(m, '\n');
}

static void __init report_one_hwcap(int *printed, const char *name)
{
	if ((*printed) == 0)
		printk(KERN_INFO "CPU CAPS: [");
	printk(KERN_CONT "%s%s",
	       (*printed) ? "," : "", name);
	if (++(*printed) == 8) {
		printk(KERN_CONT "]\n");
		*printed = 0;
	}
}

static void __init report_crypto_hwcaps(int *printed)
{
	unsigned long cfr;
	int i;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));

	for (i = 0; i < ARRAY_SIZE(crypto_hwcaps); i++) {
		unsigned long bit = 1UL << i;
		if (cfr & bit)
			report_one_hwcap(printed, crypto_hwcaps[i]);
	}
}

static void __init report_hwcaps(unsigned long caps)
{
	int i, printed = 0;

	for (i = 0; i < ARRAY_SIZE(hwcaps); i++) {
		unsigned long bit = 1UL << i;
		if (hwcaps[i] && (caps & bit))
			report_one_hwcap(&printed, hwcaps[i]);
	}
	if (caps & HWCAP_SPARC_CRYPTO)
		report_crypto_hwcaps(&printed);
	if (printed != 0)
		printk(KERN_CONT "]\n");
}

static unsigned long __init mdesc_cpu_hwcap_list(void)
{
	struct mdesc_handle *hp;
	unsigned long caps = 0;
	const char *prop;
	int len;
	u64 pn;

	hp = mdesc_grab();
	if (!hp)
		return 0;

	pn = mdesc_node_by_name(hp, MDESC_NODE_NULL, "cpu");
	if (pn == MDESC_NODE_NULL)
		goto out;

	prop = mdesc_get_property(hp, pn, "hwcap-list", &len);
	if (!prop)
		goto out;

	while (len) {
		int i, plen;

		for (i = 0; i < ARRAY_SIZE(hwcaps); i++) {
			unsigned long bit = 1UL << i;

			if (hwcaps[i] && !strcmp(prop, hwcaps[i])) {
				caps |= bit;
				break;
			}
		}
		for (i = 0; i < ARRAY_SIZE(crypto_hwcaps); i++) {
			if (!strcmp(prop, crypto_hwcaps[i]))
				caps |= HWCAP_SPARC_CRYPTO;
		}

		plen = strlen(prop) + 1;
		prop += plen;
		len -= plen;
	}

out:
	mdesc_release(hp);
	return caps;
}

/* This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
static void __init init_sparc64_elf_hwcap(void)
{
	unsigned long cap = sparc64_elf_hwcap;
	unsigned long mdesc_caps;

	if (tlb_type == cheetah || tlb_type == cheetah_plus)
		cap |= HWCAP_SPARC_ULTRA3;
	else if (tlb_type == hypervisor) {
		if (sun4v_chip_type == SUN4V_CHIP_NIAGARA1 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA2 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA3 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA4 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA5 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M6 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M7 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M8 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_SN ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC64X)
			cap |= HWCAP_SPARC_BLKINIT;
		if (sun4v_chip_type == SUN4V_CHIP_NIAGARA2 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA3 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA4 ||
		    sun4v_chip_type == SUN4V_CHIP_NIAGARA5 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M6 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M7 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_M8 ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC_SN ||
		    sun4v_chip_type == SUN4V_CHIP_SPARC64X)
			cap |= HWCAP_SPARC_N2;
	}

	cap |= (AV_SPARC_MUL32 | AV_SPARC_DIV32 | AV_SPARC_V8PLUS);

	mdesc_caps = mdesc_cpu_hwcap_list();
	if (!mdesc_caps) {
		if (tlb_type == spitfire)
			cap |= AV_SPARC_VIS;
		if (tlb_type == cheetah || tlb_type == cheetah_plus)
			cap |= AV_SPARC_VIS | AV_SPARC_VIS2;
		if (tlb_type == cheetah_plus) {
			unsigned long impl, ver;

			__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
			impl = ((ver >> 32) & 0xffff);
			if (impl == PANTHER_IMPL)
				cap |= AV_SPARC_POPC;
		}
		if (tlb_type == hypervisor) {
			if (sun4v_chip_type == SUN4V_CHIP_NIAGARA1)
				cap |= AV_SPARC_ASI_BLK_INIT;
			if (sun4v_chip_type == SUN4V_CHIP_NIAGARA2 ||
			    sun4v_chip_type == SUN4V_CHIP_NIAGARA3 ||
			    sun4v_chip_type == SUN4V_CHIP_NIAGARA4 ||
			    sun4v_chip_type == SUN4V_CHIP_NIAGARA5 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M6 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M7 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M8 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_SN ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC64X)
				cap |= (AV_SPARC_VIS | AV_SPARC_VIS2 |
					AV_SPARC_ASI_BLK_INIT |
					AV_SPARC_POPC);
			if (sun4v_chip_type == SUN4V_CHIP_NIAGARA3 ||
			    sun4v_chip_type == SUN4V_CHIP_NIAGARA4 ||
			    sun4v_chip_type == SUN4V_CHIP_NIAGARA5 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M6 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M7 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_M8 ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC_SN ||
			    sun4v_chip_type == SUN4V_CHIP_SPARC64X)
				cap |= (AV_SPARC_VIS3 | AV_SPARC_HPC |
					AV_SPARC_FMAF);
		}
	}
	sparc64_elf_hwcap = cap | mdesc_caps;

	report_hwcaps(sparc64_elf_hwcap);

	if (sparc64_elf_hwcap & AV_SPARC_POPC)
		popc_patch();
	if (sparc64_elf_hwcap & AV_SPARC_PAUSE)
		pause_patch();
}

void __init alloc_irqstack_bootmem(void)
{
	unsigned int i, node;

	for_each_possible_cpu(i) {
		node = cpu_to_node(i);

		softirq_stack[i] = memblock_alloc_node(THREAD_SIZE,
						       THREAD_SIZE, node);
		hardirq_stack[i] = memblock_alloc_node(THREAD_SIZE,
						       THREAD_SIZE, node);
	}
}

void __init setup_arch(char **cmdline_p)
{
	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	strlcpy(boot_command_line, *cmdline_p, COMMAND_LINE_SIZE);
	parse_early_param();

	boot_flags_init(*cmdline_p);
#ifdef CONFIG_EARLYFB
	if (btext_find_display())
#endif
		register_console(&prom_early_console);

	if (tlb_type == hypervisor)
		printk("ARCH: SUN4V\n");
	else
		printk("ARCH: SUN4U\n");

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	idprom_init();

	if (!root_flags)
		root_mountflags &= ~MS_RDONLY;
	ROOT_DEV = old_decode_dev(root_dev);
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = ram_flags & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((ram_flags & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((ram_flags & RAMDISK_LOAD_FLAG) != 0);
#endif

	task_thread_info(&init_task)->kregs = &fake_swapper_regs;

#ifdef CONFIG_IP_PNP
	if (!ic_set_manually) {
		phandle chosen = prom_finddevice("/chosen");
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

	/* Get boot processor trap_block[] setup.  */
	init_cur_cpu_trap(current_thread_info());

	paging_init();
	init_sparc64_elf_hwcap();
	smp_fill_in_cpu_possible_map();
	/*
	 * Once the OF device tree and MDESC have been setup and nr_cpus has
	 * been parsed, we know the list of possible cpus.  Therefore we can
	 * allocate the IRQ stacks.
	 */
	alloc_irqstack_bootmem();
}

extern int stop_a_enabled;

void sun_do_break(void)
{
	if (!stop_a_enabled)
		return;

	prom_printf("\n");
	flush_user_windows();

	prom_cmdline();
}
EXPORT_SYMBOL(sun_do_break);

int stop_a_enabled = 1;
EXPORT_SYMBOL(stop_a_enabled);
