// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/* 2.3.x bootmem, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Bootup setup stuff.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/screen_info.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/memblock.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/eisa.h>
#include <linux/pfn.h>
#ifdef CONFIG_MAGIC_SYSRQ
#include <linux/sysrq.h>
#include <linux/reboot.h>
#endif
#include <linux/notifier.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <linux/log2.h>
#include <linux/export.h>

static int alpha_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block alpha_panic_block = {
	alpha_panic_event,
        NULL,
        INT_MAX /* try to do it first */
};

#include <linux/uaccess.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/mmu_context.h>
#include <asm/console.h>

#include "proto.h"
#include "pci_impl.h"


struct hwrpb_struct *hwrpb;
EXPORT_SYMBOL(hwrpb);
unsigned long srm_hae;

int alpha_l1i_cacheshape;
int alpha_l1d_cacheshape;
int alpha_l2_cacheshape;
int alpha_l3_cacheshape;

#ifdef CONFIG_VERBOSE_MCHECK
/* 0=minimum, 1=verbose, 2=all */
/* These can be overridden via the command line, ie "verbose_mcheck=2") */
unsigned long alpha_verbose_mcheck = CONFIG_VERBOSE_MCHECK_ON;
#endif

/* Which processor we booted from.  */
int boot_cpuid;

/*
 * Using SRM callbacks for initial console output. This works from
 * setup_arch() time through the end of time_init(), as those places
 * are under our (Alpha) control.

 * "srmcons" specified in the boot command arguments allows us to
 * see kernel messages during the period of time before the true
 * console device is "registered" during console_init(). 
 * As of this version (2.5.59), console_init() will call
 * disable_early_printk() as the last action before initializing
 * the console drivers. That's the last possible time srmcons can be 
 * unregistered without interfering with console behavior.
 *
 * By default, OFF; set it with a bootcommand arg of "srmcons" or 
 * "console=srm". The meaning of these two args is:
 *     "srmcons"     - early callback prints 
 *     "console=srm" - full callback based console, including early prints
 */
int srmcons_output = 0;

/* Enforce a memory size limit; useful for testing. By default, none. */
unsigned long mem_size_limit = 0;

/* Set AGP GART window size (0 means disabled). */
unsigned long alpha_agpgart_size = DEFAULT_AGP_APER_SIZE;

#ifdef CONFIG_ALPHA_GENERIC
struct alpha_machine_vector alpha_mv;
EXPORT_SYMBOL(alpha_mv);
#endif

#ifndef alpha_using_srm
int alpha_using_srm;
EXPORT_SYMBOL(alpha_using_srm);
#endif

#ifndef alpha_using_qemu
int alpha_using_qemu;
#endif

static struct alpha_machine_vector *get_sysvec(unsigned long, unsigned long,
					       unsigned long);
static struct alpha_machine_vector *get_sysvec_byname(const char *);
static void get_sysnames(unsigned long, unsigned long, unsigned long,
			 char **, char **);
static void determine_cpu_caches (unsigned int);

static char __initdata command_line[COMMAND_LINE_SIZE];

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a VGA color display.
 */

struct screen_info screen_info = {
	.orig_x = 0,
	.orig_y = 25,
	.orig_video_cols = 80,
	.orig_video_lines = 25,
	.orig_video_isVGA = 1,
	.orig_video_points = 16
};

EXPORT_SYMBOL(screen_info);

/*
 * The direct map I/O window, if any.  This should be the same
 * for all busses, since it's used by virt_to_bus.
 */

unsigned long __direct_map_base;
unsigned long __direct_map_size;
EXPORT_SYMBOL(__direct_map_base);
EXPORT_SYMBOL(__direct_map_size);

/*
 * Declare all of the machine vectors.
 */

/* GCC 2.7.2 (on alpha at least) is lame.  It does not support either 
   __attribute__((weak)) or #pragma weak.  Bypass it and talk directly
   to the assembler.  */

#define WEAK(X) \
	extern struct alpha_machine_vector X; \
	asm(".weak "#X)

WEAK(alcor_mv);
WEAK(alphabook1_mv);
WEAK(avanti_mv);
WEAK(cabriolet_mv);
WEAK(clipper_mv);
WEAK(dp264_mv);
WEAK(eb164_mv);
WEAK(eb64p_mv);
WEAK(eb66_mv);
WEAK(eb66p_mv);
WEAK(eiger_mv);
WEAK(jensen_mv);
WEAK(lx164_mv);
WEAK(lynx_mv);
WEAK(marvel_ev7_mv);
WEAK(miata_mv);
WEAK(mikasa_mv);
WEAK(mikasa_primo_mv);
WEAK(monet_mv);
WEAK(nautilus_mv);
WEAK(noname_mv);
WEAK(noritake_mv);
WEAK(noritake_primo_mv);
WEAK(p2k_mv);
WEAK(pc164_mv);
WEAK(privateer_mv);
WEAK(rawhide_mv);
WEAK(ruffian_mv);
WEAK(rx164_mv);
WEAK(sable_mv);
WEAK(sable_gamma_mv);
WEAK(shark_mv);
WEAK(sx164_mv);
WEAK(takara_mv);
WEAK(titan_mv);
WEAK(webbrick_mv);
WEAK(wildfire_mv);
WEAK(xl_mv);
WEAK(xlt_mv);

#undef WEAK

/*
 * I/O resources inherited from PeeCees.  Except for perhaps the
 * turbochannel alphas, everyone has these on some sort of SuperIO chip.
 *
 * ??? If this becomes less standard, move the struct out into the
 * machine vector.
 */

static void __init
reserve_std_resources(void)
{
	static struct resource standard_io_resources[] = {
		{ .name = "rtc", .start = -1, .end = -1 },
        	{ .name = "dma1", .start = 0x00, .end = 0x1f },
        	{ .name = "pic1", .start = 0x20, .end = 0x3f },
        	{ .name = "timer", .start = 0x40, .end = 0x5f },
        	{ .name = "keyboard", .start = 0x60, .end = 0x6f },
        	{ .name = "dma page reg", .start = 0x80, .end = 0x8f },
        	{ .name = "pic2", .start = 0xa0, .end = 0xbf },
        	{ .name = "dma2", .start = 0xc0, .end = 0xdf },
	};

	struct resource *io = &ioport_resource;
	size_t i;

	if (hose_head) {
		struct pci_controller *hose;
		for (hose = hose_head; hose; hose = hose->next)
			if (hose->index == 0) {
				io = hose->io_space;
				break;
			}
	}

	/* Fix up for the Jensen's queer RTC placement.  */
	standard_io_resources[0].start = RTC_PORT(0);
	standard_io_resources[0].end = RTC_PORT(0) + 0x0f;

	for (i = 0; i < ARRAY_SIZE(standard_io_resources); ++i)
		request_resource(io, standard_io_resources+i);
}

#define PFN_MAX		PFN_DOWN(0x80000000)
#define for_each_mem_cluster(memdesc, _cluster, i)		\
	for ((_cluster) = (memdesc)->cluster, (i) = 0;		\
	     (i) < (memdesc)->numclusters; (i)++, (_cluster)++)

static unsigned long __init
get_mem_size_limit(char *s)
{
        unsigned long end = 0;
        char *from = s;

        end = simple_strtoul(from, &from, 0);
        if ( *from == 'K' || *from == 'k' ) {
                end = end << 10;
                from++;
        } else if ( *from == 'M' || *from == 'm' ) {
                end = end << 20;
                from++;
        } else if ( *from == 'G' || *from == 'g' ) {
                end = end << 30;
                from++;
        }
        return end >> PAGE_SHIFT; /* Return the PFN of the limit. */
}

#ifdef CONFIG_BLK_DEV_INITRD
void * __init
move_initrd(unsigned long mem_limit)
{
	void *start;
	unsigned long size;

	size = initrd_end - initrd_start;
	start = memblock_alloc(PAGE_ALIGN(size), PAGE_SIZE);
	if (!start || __pa(start) + size > mem_limit) {
		initrd_start = initrd_end = 0;
		return NULL;
	}
	memmove(start, (void *)initrd_start, size);
	initrd_start = (unsigned long)start;
	initrd_end = initrd_start + size;
	printk("initrd moved to %p\n", start);
	return start;
}
#endif

static void __init
setup_memory(void *kernel_end)
{
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	unsigned long kernel_size;
	unsigned long i;

	/* Find free clusters, and init and free the bootmem accordingly.  */
	memdesc = (struct memdesc_struct *)
	  (hwrpb->mddt_offset + (unsigned long) hwrpb);

	for_each_mem_cluster(memdesc, cluster, i) {
		unsigned long end;

		printk("memcluster %lu, usage %01lx, start %8lu, end %8lu\n",
		       i, cluster->usage, cluster->start_pfn,
		       cluster->start_pfn + cluster->numpages);

		end = cluster->start_pfn + cluster->numpages;
		if (end > max_low_pfn)
			max_low_pfn = end;

		memblock_add(PFN_PHYS(cluster->start_pfn),
			     cluster->numpages << PAGE_SHIFT);

		/* Bit 0 is console/PALcode reserved.  Bit 1 is
		   non-volatile memory -- we might want to mark
		   this for later.  */
		if (cluster->usage & 3)
			memblock_reserve(PFN_PHYS(cluster->start_pfn),
				         cluster->numpages << PAGE_SHIFT);
	}

	/*
	 * Except for the NUMA systems (wildfire, marvel) all of the 
	 * Alpha systems we run on support 32GB of memory or less.
	 * Since the NUMA systems introduce large holes in memory addressing,
	 * we can get into a situation where there is not enough contiguous
	 * memory for the memory map. 
	 *
	 * Limit memory to the first 32GB to limit the NUMA systems to 
	 * memory on their first node (wildfire) or 2 (marvel) to avoid 
	 * not being able to produce the memory map. In order to access 
	 * all of the memory on the NUMA systems, build with discontiguous
	 * memory support.
	 *
	 * If the user specified a memory limit, let that memory limit stand.
	 */
	if (!mem_size_limit) 
		mem_size_limit = (32ul * 1024 * 1024 * 1024) >> PAGE_SHIFT;

	if (mem_size_limit && max_low_pfn >= mem_size_limit)
	{
		printk("setup: forcing memory size to %ldK (from %ldK).\n",
		       mem_size_limit << (PAGE_SHIFT - 10),
		       max_low_pfn    << (PAGE_SHIFT - 10));
		max_low_pfn = mem_size_limit;
	}

	/* Reserve the kernel memory. */
	kernel_size = virt_to_phys(kernel_end) - KERNEL_START_PHYS;
	memblock_reserve(KERNEL_START_PHYS, kernel_size);

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = INITRD_START;
	if (initrd_start) {
		initrd_end = initrd_start+INITRD_SIZE;
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, INITRD_SIZE);

		if ((void *)initrd_end > phys_to_virt(PFN_PHYS(max_low_pfn))) {
			if (!move_initrd(PFN_PHYS(max_low_pfn)))
				printk("initrd extends beyond end of memory "
				       "(0x%08lx > 0x%p)\ndisabling initrd\n",
				       initrd_end,
				       phys_to_virt(PFN_PHYS(max_low_pfn)));
		} else {
			memblock_reserve(virt_to_phys((void *)initrd_start),
					INITRD_SIZE);
		}
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}

int __init
page_is_ram(unsigned long pfn)
{
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	unsigned long i;

	memdesc = (struct memdesc_struct *)
		(hwrpb->mddt_offset + (unsigned long) hwrpb);
	for_each_mem_cluster(memdesc, cluster, i)
	{
		if (pfn >= cluster->start_pfn  &&
		    pfn < cluster->start_pfn + cluster->numpages) {
			return (cluster->usage & 3) ? 0 : 1;
		}
	}

	return 0;
}

static int __init
register_cpus(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct cpu *p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		register_cpu(p, i);
	}
	return 0;
}

arch_initcall(register_cpus);

#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_reboot_handler(int unused)
{
	machine_halt();
}

static const struct sysrq_key_op srm_sysrq_reboot_op = {
	.handler	= sysrq_reboot_handler,
	.help_msg       = "reboot(b)",
	.action_msg     = "Resetting",
	.enable_mask    = SYSRQ_ENABLE_BOOT,
};
#endif

void __init
setup_arch(char **cmdline_p)
{
	extern char _end[];

	struct alpha_machine_vector *vec = NULL;
	struct percpu_struct *cpu;
	char *type_name, *var_name, *p;
	void *kernel_end = _end; /* end of kernel */
	char *args = command_line;

	hwrpb = (struct hwrpb_struct*) __va(INIT_HWRPB->phys_addr);
	boot_cpuid = hard_smp_processor_id();

        /*
	 * Pre-process the system type to make sure it will be valid.
	 *
	 * This may restore real CABRIO and EB66+ family names, ie
	 * EB64+ and EB66.
	 *
	 * Oh, and "white box" AS800 (aka DIGITAL Server 3000 series)
	 * and AS1200 (DIGITAL Server 5000 series) have the type as
	 * the negative of the real one.
	 */
        if ((long)hwrpb->sys_type < 0) {
		hwrpb->sys_type = -((long)hwrpb->sys_type);
		hwrpb_update_checksum(hwrpb);
	}

	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list,
			&alpha_panic_block);

#ifndef alpha_using_srm
	/* Assume that we've booted from SRM if we haven't booted from MILO.
	   Detect the later by looking for "MILO" in the system serial nr.  */
	alpha_using_srm = !str_has_prefix((const char *)hwrpb->ssn, "MILO");
#endif
#ifndef alpha_using_qemu
	/* Similarly, look for QEMU.  */
	alpha_using_qemu = strstr((const char *)hwrpb->ssn, "QEMU") != 0;
#endif

	/* If we are using SRM, we want to allow callbacks
	   as early as possible, so do this NOW, and then
	   they should work immediately thereafter.
	*/
	kernel_end = callback_init(kernel_end);

	/* 
	 * Locate the command line.
	 */
	/* Hack for Jensen... since we're restricted to 8 or 16 chars for
	   boot flags depending on the boot mode, we need some shorthand.
	   This should do for installation.  */
	if (strcmp(COMMAND_LINE, "INSTALL") == 0) {
		strscpy(command_line, "root=/dev/fd0 load_ramdisk=1", sizeof(command_line));
	} else {
		strscpy(command_line, COMMAND_LINE, sizeof(command_line));
	}
	strcpy(boot_command_line, command_line);
	*cmdline_p = command_line;

	/* 
	 * Process command-line arguments.
	 */
	while ((p = strsep(&args, " \t")) != NULL) {
		if (!*p) continue;
		if (strncmp(p, "alpha_mv=", 9) == 0) {
			vec = get_sysvec_byname(p+9);
			continue;
		}
		if (strncmp(p, "cycle=", 6) == 0) {
			est_cycle_freq = simple_strtol(p+6, NULL, 0);
			continue;
		}
		if (strncmp(p, "mem=", 4) == 0) {
			mem_size_limit = get_mem_size_limit(p+4);
			continue;
		}
		if (strncmp(p, "srmcons", 7) == 0) {
			srmcons_output |= 1;
			continue;
		}
		if (strncmp(p, "console=srm", 11) == 0) {
			srmcons_output |= 2;
			continue;
		}
		if (strncmp(p, "gartsize=", 9) == 0) {
			alpha_agpgart_size =
				get_mem_size_limit(p+9) << PAGE_SHIFT;
			continue;
		}
#ifdef CONFIG_VERBOSE_MCHECK
		if (strncmp(p, "verbose_mcheck=", 15) == 0) {
			alpha_verbose_mcheck = simple_strtol(p+15, NULL, 0);
			continue;
		}
#endif
	}

	/* Replace the command line, now that we've killed it with strsep.  */
	strcpy(command_line, boot_command_line);

	/* If we want SRM console printk echoing early, do it now. */
	if (alpha_using_srm && srmcons_output) {
		register_srm_console();

		/*
		 * If "console=srm" was specified, clear the srmcons_output
		 * flag now so that time.c won't unregister_srm_console
		 */
		if (srmcons_output & 2)
			srmcons_output = 0;
	}

#ifdef CONFIG_MAGIC_SYSRQ
	/* If we're using SRM, make sysrq-b halt back to the prom,
	   not auto-reboot.  */
	if (alpha_using_srm) {
		unregister_sysrq_key('b', __sysrq_reboot_op);
		register_sysrq_key('b', &srm_sysrq_reboot_op);
	}
#endif

	/*
	 * Identify and reconfigure for the current system.
	 */
	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     cpu->type, &type_name, &var_name);
	if (*var_name == '0')
		var_name = "";

	if (!vec) {
		vec = get_sysvec(hwrpb->sys_type, hwrpb->sys_variation,
				 cpu->type);
	}

	if (!vec) {
		panic("Unsupported system type: %s%s%s (%ld %ld)\n",
		      type_name, (*var_name ? " variation " : ""), var_name,
		      hwrpb->sys_type, hwrpb->sys_variation);
	}
	if (vec != &alpha_mv) {
		alpha_mv = *vec;
	}
	
	printk("Booting "
#ifdef CONFIG_ALPHA_GENERIC
	       "GENERIC "
#endif
	       "on %s%s%s using machine vector %s from %s\n",
	       type_name, (*var_name ? " variation " : ""),
	       var_name, alpha_mv.vector_name,
	       (alpha_using_srm ? "SRM" : "MILO"));

	printk("Major Options: "
#ifdef CONFIG_SMP
	       "SMP "
#endif
#ifdef CONFIG_ALPHA_EV56
	       "EV56 "
#endif
#ifdef CONFIG_ALPHA_EV67
	       "EV67 "
#endif
#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
	       "LEGACY_START "
#endif
#ifdef CONFIG_VERBOSE_MCHECK
	       "VERBOSE_MCHECK "
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
	       "DEBUG_SPINLOCK "
#endif
#ifdef CONFIG_MAGIC_SYSRQ
	       "MAGIC_SYSRQ "
#endif
	       "\n");

	printk("Command line: %s\n", command_line);

	/* 
	 * Sync up the HAE.
	 * Save the SRM's current value for restoration.
	 */
	srm_hae = *alpha_mv.hae_register;
	__set_hae(alpha_mv.hae_cache);

	/* Reset enable correctable error reports.  */
	wrmces(0x7);

	/* Find our memory.  */
	setup_memory(kernel_end);
	memblock_set_bottom_up(true);
	sparse_init();

	/* First guess at cpu cache sizes.  Do this before init_arch.  */
	determine_cpu_caches(cpu->type);

	/* Initialize the machine.  Usually has to do with setting up
	   DMA windows and the like.  */
	if (alpha_mv.init_arch)
		alpha_mv.init_arch();

	/* Reserve standard resources.  */
	reserve_std_resources();

	/* 
	 * Give us a default console.  TGA users will see nothing until
	 * chr_dev_init is called, rather late in the boot sequence.
	 */

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#endif
#endif

	/* Default root filesystem to sda2.  */
	ROOT_DEV = Root_SDA2;

#ifdef CONFIG_EISA
	/* FIXME:  only set this when we actually have EISA in this box? */
	EISA_bus = 1;
#endif

 	/*
	 * Check ASN in HWRPB for validity, report if bad.
	 * FIXME: how was this failing?  Should we trust it instead,
	 * and copy the value into alpha_mv.max_asn?
 	 */

 	if (hwrpb->max_asn != MAX_ASN) {
		printk("Max ASN from HWRPB is bad (0x%lx)\n", hwrpb->max_asn);
 	}

	/*
	 * Identify the flock of penguins.
	 */

#ifdef CONFIG_SMP
	setup_smp();
#endif
	paging_init();
}

static char sys_unknown[] = "Unknown";
static char systype_names[][16] = {
	"0",
	"ADU", "Cobra", "Ruby", "Flamingo", "Mannequin", "Jensen",
	"Pelican", "Morgan", "Sable", "Medulla", "Noname",
	"Turbolaser", "Avanti", "Mustang", "Alcor", "Tradewind",
	"Mikasa", "EB64", "EB66", "EB64+", "AlphaBook1",
	"Rawhide", "K2", "Lynx", "XL", "EB164", "Noritake",
	"Cortex", "29", "Miata", "XXM", "Takara", "Yukon",
	"Tsunami", "Wildfire", "CUSCO", "Eiger", "Titan", "Marvel"
};

static char unofficial_names[][8] = {"100", "Ruffian"};

static char api_names[][16] = {"200", "Nautilus"};

static char eb164_names[][8] = {"EB164", "PC164", "LX164", "SX164", "RX164"};
static int eb164_indices[] = {0,0,0,1,1,1,1,1,2,2,2,2,3,3,3,3,4};

static char alcor_names[][16] = {"Alcor", "Maverick", "Bret"};
static int alcor_indices[] = {0,0,0,1,1,1,0,0,0,0,0,0,2,2,2,2,2,2};

static char eb64p_names[][16] = {"EB64+", "Cabriolet", "AlphaPCI64"};
static int eb64p_indices[] = {0,0,1,2};

static char eb66_names[][8] = {"EB66", "EB66+"};
static int eb66_indices[] = {0,0,1};

static char marvel_names[][16] = {
	"Marvel/EV7"
};
static int marvel_indices[] = { 0 };

static char rawhide_names[][16] = {
	"Dodge", "Wrangler", "Durango", "Tincup", "DaVinci"
};
static int rawhide_indices[] = {0,0,0,1,1,2,2,3,3,4,4};

static char titan_names[][16] = {
	"DEFAULT", "Privateer", "Falcon", "Granite"
};
static int titan_indices[] = {0,1,2,2,3};

static char tsunami_names[][16] = {
	"0", "DP264", "Warhol", "Windjammer", "Monet", "Clipper",
	"Goldrush", "Webbrick", "Catamaran", "Brisbane", "Melbourne",
	"Flying Clipper", "Shark"
};
static int tsunami_indices[] = {0,1,2,3,4,5,6,7,8,9,10,11,12};

static struct alpha_machine_vector * __init
get_sysvec(unsigned long type, unsigned long variation, unsigned long cpu)
{
	static struct alpha_machine_vector *systype_vecs[] __initdata =
	{
		NULL,		/* 0 */
		NULL,		/* ADU */
		NULL,		/* Cobra */
		NULL,		/* Ruby */
		NULL,		/* Flamingo */
		NULL,		/* Mannequin */
		&jensen_mv,
		NULL, 		/* Pelican */
		NULL,		/* Morgan */
		NULL,		/* Sable -- see below.  */
		NULL,		/* Medulla */
		&noname_mv,
		NULL,		/* Turbolaser */
		&avanti_mv,
		NULL,		/* Mustang */
		NULL,		/* Alcor, Bret, Maverick. HWRPB inaccurate? */
		NULL,		/* Tradewind */
		NULL,		/* Mikasa -- see below.  */
		NULL,		/* EB64 */
		NULL,		/* EB66 -- see variation.  */
		NULL,		/* EB64+ -- see variation.  */
		&alphabook1_mv,
		&rawhide_mv,
		NULL,		/* K2 */
		&lynx_mv,	/* Lynx */
		&xl_mv,
		NULL,		/* EB164 -- see variation.  */
		NULL,		/* Noritake -- see below.  */
		NULL,		/* Cortex */
		NULL,		/* 29 */
		&miata_mv,
		NULL,		/* XXM */
		&takara_mv,
		NULL,		/* Yukon */
		NULL,		/* Tsunami -- see variation.  */
		&wildfire_mv,	/* Wildfire */
		NULL,		/* CUSCO */
		&eiger_mv,	/* Eiger */
		NULL,		/* Titan */
		NULL,		/* Marvel */
	};

	static struct alpha_machine_vector *unofficial_vecs[] __initdata =
	{
		NULL,		/* 100 */
		&ruffian_mv,
	};

	static struct alpha_machine_vector *api_vecs[] __initdata =
	{
		NULL,		/* 200 */
		&nautilus_mv,
	};

	static struct alpha_machine_vector *alcor_vecs[] __initdata = 
	{
		&alcor_mv, &xlt_mv, &xlt_mv
	};

	static struct alpha_machine_vector *eb164_vecs[] __initdata =
	{
		&eb164_mv, &pc164_mv, &lx164_mv, &sx164_mv, &rx164_mv
	};

	static struct alpha_machine_vector *eb64p_vecs[] __initdata =
	{
		&eb64p_mv,
		&cabriolet_mv,
		&cabriolet_mv		/* AlphaPCI64 */
	};

	static struct alpha_machine_vector *eb66_vecs[] __initdata =
	{
		&eb66_mv,
		&eb66p_mv
	};

	static struct alpha_machine_vector *marvel_vecs[] __initdata =
	{
		&marvel_ev7_mv,
	};

	static struct alpha_machine_vector *titan_vecs[] __initdata =
	{
		&titan_mv,		/* default   */
		&privateer_mv,		/* privateer */
		&titan_mv,		/* falcon    */
		&privateer_mv,		/* granite   */
	};

	static struct alpha_machine_vector *tsunami_vecs[]  __initdata =
	{
		NULL,
		&dp264_mv,		/* dp264 */
		&dp264_mv,		/* warhol */
		&dp264_mv,		/* windjammer */
		&monet_mv,		/* monet */
		&clipper_mv,		/* clipper */
		&dp264_mv,		/* goldrush */
		&webbrick_mv,		/* webbrick */
		&dp264_mv,		/* catamaran */
		NULL,			/* brisbane? */
		NULL,			/* melbourne? */
		NULL,			/* flying clipper? */
		&shark_mv,		/* shark */
	};

	/* ??? Do we need to distinguish between Rawhides?  */

	struct alpha_machine_vector *vec;

	/* Search the system tables first... */
	vec = NULL;
	if (type < ARRAY_SIZE(systype_vecs)) {
		vec = systype_vecs[type];
	} else if ((type > ST_API_BIAS) &&
		   (type - ST_API_BIAS) < ARRAY_SIZE(api_vecs)) {
		vec = api_vecs[type - ST_API_BIAS];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < ARRAY_SIZE(unofficial_vecs)) {
		vec = unofficial_vecs[type - ST_UNOFFICIAL_BIAS];
	}

	/* If we've not found one, try for a variation.  */

	if (!vec) {
		/* Member ID is a bit-field. */
		unsigned long member = (variation >> 10) & 0x3f;

		cpu &= 0xffffffff; /* make it usable */

		switch (type) {
		case ST_DEC_ALCOR:
			if (member < ARRAY_SIZE(alcor_indices))
				vec = alcor_vecs[alcor_indices[member]];
			break;
		case ST_DEC_EB164:
			if (member < ARRAY_SIZE(eb164_indices))
				vec = eb164_vecs[eb164_indices[member]];
			/* PC164 may show as EB164 variation with EV56 CPU,
			   but, since no true EB164 had anything but EV5... */
			if (vec == &eb164_mv && cpu == EV56_CPU)
				vec = &pc164_mv;
			break;
		case ST_DEC_EB64P:
			if (member < ARRAY_SIZE(eb64p_indices))
				vec = eb64p_vecs[eb64p_indices[member]];
			break;
		case ST_DEC_EB66:
			if (member < ARRAY_SIZE(eb66_indices))
				vec = eb66_vecs[eb66_indices[member]];
			break;
		case ST_DEC_MARVEL:
			if (member < ARRAY_SIZE(marvel_indices))
				vec = marvel_vecs[marvel_indices[member]];
			break;
		case ST_DEC_TITAN:
			vec = titan_vecs[0];	/* default */
			if (member < ARRAY_SIZE(titan_indices))
				vec = titan_vecs[titan_indices[member]];
			break;
		case ST_DEC_TSUNAMI:
			if (member < ARRAY_SIZE(tsunami_indices))
				vec = tsunami_vecs[tsunami_indices[member]];
			break;
		case ST_DEC_1000:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &mikasa_primo_mv;
			else
				vec = &mikasa_mv;
			break;
		case ST_DEC_NORITAKE:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &noritake_primo_mv;
			else
				vec = &noritake_mv;
			break;
		case ST_DEC_2100_A500:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &sable_gamma_mv;
			else
				vec = &sable_mv;
			break;
		}
	}
	return vec;
}

static struct alpha_machine_vector * __init
get_sysvec_byname(const char *name)
{
	static struct alpha_machine_vector *all_vecs[] __initdata =
	{
		&alcor_mv,
		&alphabook1_mv,
		&avanti_mv,
		&cabriolet_mv,
		&clipper_mv,
		&dp264_mv,
		&eb164_mv,
		&eb64p_mv,
		&eb66_mv,
		&eb66p_mv,
		&eiger_mv,
		&jensen_mv,
		&lx164_mv,
		&lynx_mv,
		&miata_mv,
		&mikasa_mv,
		&mikasa_primo_mv,
		&monet_mv,
		&nautilus_mv,
		&noname_mv,
		&noritake_mv,
		&noritake_primo_mv,
		&p2k_mv,
		&pc164_mv,
		&privateer_mv,
		&rawhide_mv,
		&ruffian_mv,
		&rx164_mv,
		&sable_mv,
		&sable_gamma_mv,
		&shark_mv,
		&sx164_mv,
		&takara_mv,
		&webbrick_mv,
		&wildfire_mv,
		&xl_mv,
		&xlt_mv
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(all_vecs); ++i) {
		struct alpha_machine_vector *mv = all_vecs[i];
		if (strcasecmp(mv->vector_name, name) == 0)
			return mv;
	}
	return NULL;
}

static void
get_sysnames(unsigned long type, unsigned long variation, unsigned long cpu,
	     char **type_name, char **variation_name)
{
	unsigned long member;

	/* If not in the tables, make it UNKNOWN,
	   else set type name to family */
	if (type < ARRAY_SIZE(systype_names)) {
		*type_name = systype_names[type];
	} else if ((type > ST_API_BIAS) &&
		   (type - ST_API_BIAS) < ARRAY_SIZE(api_names)) {
		*type_name = api_names[type - ST_API_BIAS];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < ARRAY_SIZE(unofficial_names)) {
		*type_name = unofficial_names[type - ST_UNOFFICIAL_BIAS];
	} else {
		*type_name = sys_unknown;
		*variation_name = sys_unknown;
		return;
	}

	/* Set variation to "0"; if variation is zero, done.  */
	*variation_name = systype_names[0];
	if (variation == 0) {
		return;
	}

	member = (variation >> 10) & 0x3f; /* member ID is a bit-field */

	cpu &= 0xffffffff; /* make it usable */

	switch (type) { /* select by family */
	default: /* default to variation "0" for now */
		break;
	case ST_DEC_EB164:
		if (member >= ARRAY_SIZE(eb164_indices))
			break;
		*variation_name = eb164_names[eb164_indices[member]];
		/* PC164 may show as EB164 variation, but with EV56 CPU,
		   so, since no true EB164 had anything but EV5... */
		if (eb164_indices[member] == 0 && cpu == EV56_CPU)
			*variation_name = eb164_names[1]; /* make it PC164 */
		break;
	case ST_DEC_ALCOR:
		if (member < ARRAY_SIZE(alcor_indices))
			*variation_name = alcor_names[alcor_indices[member]];
		break;
	case ST_DEC_EB64P:
		if (member < ARRAY_SIZE(eb64p_indices))
			*variation_name = eb64p_names[eb64p_indices[member]];
		break;
	case ST_DEC_EB66:
		if (member < ARRAY_SIZE(eb66_indices))
			*variation_name = eb66_names[eb66_indices[member]];
		break;
	case ST_DEC_MARVEL:
		if (member < ARRAY_SIZE(marvel_indices))
			*variation_name = marvel_names[marvel_indices[member]];
		break;
	case ST_DEC_RAWHIDE:
		if (member < ARRAY_SIZE(rawhide_indices))
			*variation_name = rawhide_names[rawhide_indices[member]];
		break;
	case ST_DEC_TITAN:
		*variation_name = titan_names[0];	/* default */
		if (member < ARRAY_SIZE(titan_indices))
			*variation_name = titan_names[titan_indices[member]];
		break;
	case ST_DEC_TSUNAMI:
		if (member < ARRAY_SIZE(tsunami_indices))
			*variation_name = tsunami_names[tsunami_indices[member]];
		break;
	}
}

/*
 * A change was made to the HWRPB via an ECO and the following code
 * tracks a part of the ECO.  In HWRPB versions less than 5, the ECO
 * was not implemented in the console firmware.  If it's revision 5 or
 * greater we can get the name of the platform as an ASCII string from
 * the HWRPB.  That's what this function does.  It checks the revision
 * level and if the string is in the HWRPB it returns the address of
 * the string--a pointer to the name of the platform.
 *
 * Returns:
 *      - Pointer to a ASCII string if it's in the HWRPB
 *      - Pointer to a blank string if the data is not in the HWRPB.
 */

static char *
platform_string(void)
{
	struct dsr_struct *dsr;
	static char unk_system_string[] = "N/A";

	/* Go to the console for the string pointer.
	 * If the rpb_vers is not 5 or greater the rpb
	 * is old and does not have this data in it.
	 */
	if (hwrpb->revision < 5)
		return (unk_system_string);
	else {
		/* The Dynamic System Recognition struct
		 * has the system platform name starting
		 * after the character count of the string.
		 */
		dsr =  ((struct dsr_struct *)
			((char *)hwrpb + hwrpb->dsr_offset));
		return ((char *)dsr + (dsr->sysname_off +
				       sizeof(long)));
	}
}

static int
get_nr_processors(struct percpu_struct *cpubase, unsigned long num)
{
	struct percpu_struct *cpu;
	unsigned long i;
	int count = 0;

	for (i = 0; i < num; i++) {
		cpu = (struct percpu_struct *)
			((char *)cpubase + i*hwrpb->processor_size);
		if ((cpu->flags & 0x1cc) == 0x1cc)
			count++;
	}
	return count;
}

static void
show_cache_size (struct seq_file *f, const char *which, int shape)
{
	if (shape == -1)
		seq_printf (f, "%s\t\t: n/a\n", which);
	else if (shape == 0)
		seq_printf (f, "%s\t\t: unknown\n", which);
	else
		seq_printf (f, "%s\t\t: %dK, %d-way, %db line\n",
			    which, shape >> 10, shape & 15,
			    1 << ((shape >> 4) & 15));
}

static int
show_cpuinfo(struct seq_file *f, void *slot)
{
	extern struct unaligned_stat {
		unsigned long count, va, pc;
	} unaligned[2];

	static char cpu_names[][8] = {
		"EV3", "EV4", "Simulate", "LCA4", "EV5", "EV45", "EV56",
		"EV6", "PCA56", "PCA57", "EV67", "EV68CB", "EV68AL",
		"EV68CX", "EV7", "EV79", "EV69"
	};

	struct percpu_struct *cpu = slot;
	unsigned int cpu_index;
	char *cpu_name;
	char *systype_name;
	char *sysvariation_name;
	int nr_processors;
	unsigned long timer_freq;

	cpu_index = (unsigned) (cpu->type - 1);
	cpu_name = "Unknown";
	if (cpu_index < ARRAY_SIZE(cpu_names))
		cpu_name = cpu_names[cpu_index];

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     cpu->type, &systype_name, &sysvariation_name);

	nr_processors = get_nr_processors(cpu, hwrpb->nr_processors);

#if CONFIG_HZ == 1024 || CONFIG_HZ == 1200
	timer_freq = (100UL * hwrpb->intr_freq) / 4096;
#else
	timer_freq = 100UL * CONFIG_HZ;
#endif

	seq_printf(f, "cpu\t\t\t: Alpha\n"
		      "cpu model\t\t: %s\n"
		      "cpu variation\t\t: %ld\n"
		      "cpu revision\t\t: %ld\n"
		      "cpu serial number\t: %s\n"
		      "system type\t\t: %s\n"
		      "system variation\t: %s\n"
		      "system revision\t\t: %ld\n"
		      "system serial number\t: %s\n"
		      "cycle frequency [Hz]\t: %lu %s\n"
		      "timer frequency [Hz]\t: %lu.%02lu\n"
		      "page size [bytes]\t: %ld\n"
		      "phys. address bits\t: %ld\n"
		      "max. addr. space #\t: %ld\n"
		      "BogoMIPS\t\t: %lu.%02lu\n"
		      "kernel unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "user unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "platform string\t\t: %s\n"
		      "cpus detected\t\t: %d\n",
		       cpu_name, cpu->variation, cpu->revision,
		       (char*)cpu->serial_no,
		       systype_name, sysvariation_name, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       est_cycle_freq ? : hwrpb->cycle_freq,
		       est_cycle_freq ? "est." : "",
		       timer_freq / 100, timer_freq % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_jiffy / (500000/HZ),
		       (loops_per_jiffy / (5000/HZ)) % 100,
		       unaligned[0].count, unaligned[0].pc, unaligned[0].va,
		       unaligned[1].count, unaligned[1].pc, unaligned[1].va,
		       platform_string(), nr_processors);

#ifdef CONFIG_SMP
	seq_printf(f, "cpus active\t\t: %u\n"
		      "cpu active mask\t\t: %016lx\n",
		       num_online_cpus(), cpumask_bits(cpu_possible_mask)[0]);
#endif

	show_cache_size (f, "L1 Icache", alpha_l1i_cacheshape);
	show_cache_size (f, "L1 Dcache", alpha_l1d_cacheshape);
	show_cache_size (f, "L2 cache", alpha_l2_cacheshape);
	show_cache_size (f, "L3 cache", alpha_l3_cacheshape);

	return 0;
}

static int __init
read_mem_block(int *addr, int stride, int size)
{
	long nloads = size / stride, cnt, tmp;

	__asm__ __volatile__(
	"	rpcc    %0\n"
	"1:	ldl	%3,0(%2)\n"
	"	subq	%1,1,%1\n"
	/* Next two XORs introduce an explicit data dependency between
	   consecutive loads in the loop, which will give us true load
	   latency. */
	"	xor	%3,%2,%2\n"
	"	xor	%3,%2,%2\n"
	"	addq	%2,%4,%2\n"
	"	bne	%1,1b\n"
	"	rpcc	%3\n"
	"	subl	%3,%0,%0\n"
	: "=&r" (cnt), "=&r" (nloads), "=&r" (addr), "=&r" (tmp)
	: "r" (stride), "1" (nloads), "2" (addr));

	return cnt / (size / stride);
}

#define CSHAPE(totalsize, linesize, assoc) \
  ((totalsize & ~0xff) | (linesize << 4) | assoc)

/* ??? EV5 supports up to 64M, but did the systems with more than
   16M of BCACHE ever exist? */
#define MAX_BCACHE_SIZE	16*1024*1024

/* Note that the offchip caches are direct mapped on all Alphas. */
static int __init
external_cache_probe(int minsize, int width)
{
	int cycles, prev_cycles = 1000000;
	int stride = 1 << width;
	long size = minsize, maxsize = MAX_BCACHE_SIZE * 2;

	if (maxsize > (max_low_pfn + 1) << PAGE_SHIFT)
		maxsize = 1 << (ilog2(max_low_pfn + 1) + PAGE_SHIFT);

	/* Get the first block cached. */
	read_mem_block(__va(0), stride, size);

	while (size < maxsize) {
		/* Get an average load latency in cycles. */
		cycles = read_mem_block(__va(0), stride, size);
		if (cycles > prev_cycles * 2) {
			/* Fine, we exceed the cache. */
			printk("%ldK Bcache detected; load hit latency %d "
			       "cycles, load miss latency %d cycles\n",
			       size >> 11, prev_cycles, cycles);
			return CSHAPE(size >> 1, width, 1);
		}
		/* Try to get the next block cached. */
		read_mem_block(__va(size), stride, size);
		prev_cycles = cycles;
		size <<= 1;
	}
	return -1;	/* No BCACHE found. */
}

static void __init
determine_cpu_caches (unsigned int cpu_type)
{
	int L1I, L1D, L2, L3;

	switch (cpu_type) {
	case EV4_CPU:
	case EV45_CPU:
	  {
		if (cpu_type == EV4_CPU)
			L1I = CSHAPE(8*1024, 5, 1);
		else
			L1I = CSHAPE(16*1024, 5, 1);
		L1D = L1I;
		L3 = -1;
	
		/* BIU_CTL is a write-only Abox register.  PALcode has a
		   shadow copy, and may be available from some versions
		   of the CSERVE PALcall.  If we can get it, then

			unsigned long biu_ctl, size;
			size = 128*1024 * (1 << ((biu_ctl >> 28) & 7));
			L2 = CSHAPE (size, 5, 1);

		   Unfortunately, we can't rely on that.
		*/
		L2 = external_cache_probe(128*1024, 5);
		break;
	  }

	case LCA4_CPU:
	  {
		unsigned long car, size;

		L1I = L1D = CSHAPE(8*1024, 5, 1);
		L3 = -1;

		car = *(vuip) phys_to_virt (0x120000078UL);
		size = 64*1024 * (1 << ((car >> 5) & 7));
		/* No typo -- 8 byte cacheline size.  Whodathunk.  */
		L2 = (car & 1 ? CSHAPE (size, 3, 1) : -1);
		break;
	  }

	case EV5_CPU:
	case EV56_CPU:
	  {
		unsigned long sc_ctl, width;

		L1I = L1D = CSHAPE(8*1024, 5, 1);

		/* Check the line size of the Scache.  */
		sc_ctl = *(vulp) phys_to_virt (0xfffff000a8UL);
		width = sc_ctl & 0x1000 ? 6 : 5;
		L2 = CSHAPE (96*1024, width, 3);

		/* BC_CONTROL and BC_CONFIG are write-only IPRs.  PALcode
		   has a shadow copy, and may be available from some versions
		   of the CSERVE PALcall.  If we can get it, then

			unsigned long bc_control, bc_config, size;
			size = 1024*1024 * (1 << ((bc_config & 7) - 1));
			L3 = (bc_control & 1 ? CSHAPE (size, width, 1) : -1);

		   Unfortunately, we can't rely on that.
		*/
		L3 = external_cache_probe(1024*1024, width);
		break;
	  }

	case PCA56_CPU:
	case PCA57_CPU:
	  {
		if (cpu_type == PCA56_CPU) {
			L1I = CSHAPE(16*1024, 6, 1);
			L1D = CSHAPE(8*1024, 5, 1);
		} else {
			L1I = CSHAPE(32*1024, 6, 2);
			L1D = CSHAPE(16*1024, 5, 1);
		}
		L3 = -1;

#if 0
		unsigned long cbox_config, size;

		cbox_config = *(vulp) phys_to_virt (0xfffff00008UL);
		size = 512*1024 * (1 << ((cbox_config >> 12) & 3));

		L2 = ((cbox_config >> 31) & 1 ? CSHAPE (size, 6, 1) : -1);
#else
		L2 = external_cache_probe(512*1024, 6);
#endif
		break;
	  }

	case EV6_CPU:
	case EV67_CPU:
	case EV68CB_CPU:
	case EV68AL_CPU:
	case EV68CX_CPU:
	case EV69_CPU:
		L1I = L1D = CSHAPE(64*1024, 6, 2);
		L2 = external_cache_probe(1024*1024, 6);
		L3 = -1;
		break;

	case EV7_CPU:
	case EV79_CPU:
		L1I = L1D = CSHAPE(64*1024, 6, 2);
		L2 = CSHAPE(7*1024*1024/4, 6, 7);
		L3 = -1;
		break;

	default:
		/* Nothing known about this cpu type.  */
		L1I = L1D = L2 = L3 = 0;
		break;
	}

	alpha_l1i_cacheshape = L1I;
	alpha_l1d_cacheshape = L1D;
	alpha_l2_cacheshape = L2;
	alpha_l3_cacheshape = L3;
}

/*
 * We show only CPU #0 info.
 */
static void *
c_start(struct seq_file *f, loff_t *pos)
{
	return *pos ? NULL : (char *)hwrpb + hwrpb->processor_offset;
}

static void *
c_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void
c_stop(struct seq_file *f, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};


static int
alpha_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
#if 1
	/* FIXME FIXME FIXME */
	/* If we are using SRM and serial console, just hard halt here. */
	if (alpha_using_srm && srmcons_output)
		__halt();
#endif
        return NOTIFY_DONE;
}

static __init int add_pcspkr(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("pcspkr", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}
device_initcall(add_pcspkr);
