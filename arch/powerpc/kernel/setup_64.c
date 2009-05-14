/*
 * 
 * Common boot and setup code.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/utsname.h>
#include <linux/tty.h>
#include <linux/root_dev.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/unistd.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/bootmem.h>
#include <linux/pci.h>
#include <linux/lockdep.h>
#include <linux/lmb.h>
#include <asm/io.h>
#include <asm/kdump.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/time.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/btext.h>
#include <asm/nvram.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/serial.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/firmware.h>
#include <asm/xmon.h>
#include <asm/udbg.h>
#include <asm/kexec.h>
#include <asm/swiotlb.h>

#include "setup.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

int boot_cpuid = 0;
u64 ppc64_pft_size;

/* Pick defaults since we might want to patch instructions
 * before we've read this from the device tree.
 */
struct ppc64_caches ppc64_caches = {
	.dline_size = 0x40,
	.log_dline_size = 6,
	.iline_size = 0x40,
	.log_iline_size = 6
};
EXPORT_SYMBOL_GPL(ppc64_caches);

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

#ifdef CONFIG_SMP

static int smt_enabled_cmdline;

/* Look for ibm,smt-enabled OF option */
static void check_smt_enabled(void)
{
	struct device_node *dn;
	const char *smt_option;

	/* Allow the command line to overrule the OF option */
	if (smt_enabled_cmdline)
		return;

	dn = of_find_node_by_path("/options");

	if (dn) {
		smt_option = of_get_property(dn, "ibm,smt-enabled", NULL);

                if (smt_option) {
			if (!strcmp(smt_option, "on"))
				smt_enabled_at_boot = 1;
			else if (!strcmp(smt_option, "off"))
				smt_enabled_at_boot = 0;
                }
        }
}

/* Look for smt-enabled= cmdline option */
static int __init early_smt_enabled(char *p)
{
	smt_enabled_cmdline = 1;

	if (!p)
		return 0;

	if (!strcmp(p, "on") || !strcmp(p, "1"))
		smt_enabled_at_boot = 1;
	else if (!strcmp(p, "off") || !strcmp(p, "0"))
		smt_enabled_at_boot = 0;

	return 0;
}
early_param("smt-enabled", early_smt_enabled);

#else
#define check_smt_enabled()
#endif /* CONFIG_SMP */

/* Put the paca pointer into r13 and SPRG3 */
void __init setup_paca(int cpu)
{
	local_paca = &paca[cpu];
	mtspr(SPRN_SPRG3, local_paca);
}

/*
 * Early initialization entry point. This is called by head.S
 * with MMU translation disabled. We rely on the "feature" of
 * the CPU that ignores the top 2 bits of the address in real
 * mode so we can access kernel globals normally provided we
 * only toy with things in the RMO region. From here, we do
 * some early parsing of the device-tree to setup out LMB
 * data structures, and allocate & initialize the hash table
 * and segment tables so we can start running with translation
 * enabled.
 *
 * It is this function which will call the probe() callback of
 * the various platform types and copy the matching one to the
 * global ppc_md structure. Your platform can eventually do
 * some very early initializations from the probe() routine, but
 * this is not recommended, be very careful as, for example, the
 * device-tree is not accessible via normal means at this point.
 */

void __init early_setup(unsigned long dt_ptr)
{
	/* -------- printk is _NOT_ safe to use here ! ------- */

	/* Fill in any unititialised pacas */
	initialise_pacas();

	/* Identify CPU type */
	identify_cpu(0, mfspr(SPRN_PVR));

	/* Assume we're on cpu 0 for now. Don't write to the paca yet! */
	setup_paca(0);

	/* Initialize lockdep early or else spinlocks will blow */
	lockdep_init();

	/* -------- printk is now safe to use ------- */

	/* Enable early debugging if any specified (see udbg.h) */
	udbg_early_init();

 	DBG(" -> early_setup(), dt_ptr: 0x%lx\n", dt_ptr);

	/*
	 * Do early initialization using the flattened device
	 * tree, such as retrieving the physical memory map or
	 * calculating/retrieving the hash table size.
	 */
	early_init_devtree(__va(dt_ptr));

	/* Now we know the logical id of our boot cpu, setup the paca. */
	setup_paca(boot_cpuid);

	/* Fix up paca fields required for the boot cpu */
	get_paca()->cpu_start = 1;

	/* Probe the machine type */
	probe_machine();

	setup_kdump_trampoline();

	DBG("Found, Initializing memory management...\n");

	/* Initialize the hash table or TLB handling */
	early_init_mmu();

	DBG(" <- early_setup()\n");
}

#ifdef CONFIG_SMP
void early_setup_secondary(void)
{
	/* Mark interrupts enabled in PACA */
	get_paca()->soft_enabled = 0;

	/* Initialize the hash table or TLB handling */
	early_init_mmu_secondary();
}

#endif /* CONFIG_SMP */

#if defined(CONFIG_SMP) || defined(CONFIG_KEXEC)
extern unsigned long __secondary_hold_spinloop;
extern void generic_secondary_smp_init(void);

void smp_release_cpus(void)
{
	unsigned long *ptr;

	DBG(" -> smp_release_cpus()\n");

	/* All secondary cpus are spinning on a common spinloop, release them
	 * all now so they can start to spin on their individual paca
	 * spinloops. For non SMP kernels, the secondary cpus never get out
	 * of the common spinloop.
	 */

	ptr  = (unsigned long *)((unsigned long)&__secondary_hold_spinloop
			- PHYSICAL_START);
	*ptr = __pa(generic_secondary_smp_init);
	mb();

	DBG(" <- smp_release_cpus()\n");
}
#endif /* CONFIG_SMP || CONFIG_KEXEC */

/*
 * Initialize some remaining members of the ppc64_caches and systemcfg
 * structures
 * (at least until we get rid of them completely). This is mostly some
 * cache informations about the CPU that will be used by cache flush
 * routines and/or provided to userland
 */
static void __init initialize_cache_info(void)
{
	struct device_node *np;
	unsigned long num_cpus = 0;

	DBG(" -> initialize_cache_info()\n");

	for (np = NULL; (np = of_find_node_by_type(np, "cpu"));) {
		num_cpus += 1;

		/* We're assuming *all* of the CPUs have the same
		 * d-cache and i-cache sizes... -Peter
		 */

		if ( num_cpus == 1 ) {
			const u32 *sizep, *lsizep;
			u32 size, lsize;

			size = 0;
			lsize = cur_cpu_spec->dcache_bsize;
			sizep = of_get_property(np, "d-cache-size", NULL);
			if (sizep != NULL)
				size = *sizep;
			lsizep = of_get_property(np, "d-cache-block-size", NULL);
			/* fallback if block size missing */
			if (lsizep == NULL)
				lsizep = of_get_property(np, "d-cache-line-size", NULL);
			if (lsizep != NULL)
				lsize = *lsizep;
			if (sizep == 0 || lsizep == 0)
				DBG("Argh, can't find dcache properties ! "
				    "sizep: %p, lsizep: %p\n", sizep, lsizep);

			ppc64_caches.dsize = size;
			ppc64_caches.dline_size = lsize;
			ppc64_caches.log_dline_size = __ilog2(lsize);
			ppc64_caches.dlines_per_page = PAGE_SIZE / lsize;

			size = 0;
			lsize = cur_cpu_spec->icache_bsize;
			sizep = of_get_property(np, "i-cache-size", NULL);
			if (sizep != NULL)
				size = *sizep;
			lsizep = of_get_property(np, "i-cache-block-size", NULL);
			if (lsizep == NULL)
				lsizep = of_get_property(np, "i-cache-line-size", NULL);
			if (lsizep != NULL)
				lsize = *lsizep;
			if (sizep == 0 || lsizep == 0)
				DBG("Argh, can't find icache properties ! "
				    "sizep: %p, lsizep: %p\n", sizep, lsizep);

			ppc64_caches.isize = size;
			ppc64_caches.iline_size = lsize;
			ppc64_caches.log_iline_size = __ilog2(lsize);
			ppc64_caches.ilines_per_page = PAGE_SIZE / lsize;
		}
	}

	DBG(" <- initialize_cache_info()\n");
}


/*
 * Do some initial setup of the system.  The parameters are those which 
 * were passed in from the bootloader.
 */
void __init setup_system(void)
{
	DBG(" -> setup_system()\n");

	/* Apply the CPUs-specific and firmware specific fixups to kernel
	 * text (nop out sections not relevant to this CPU or this firmware)
	 */
	do_feature_fixups(cur_cpu_spec->cpu_features,
			  &__start___ftr_fixup, &__stop___ftr_fixup);
	do_feature_fixups(cur_cpu_spec->mmu_features,
			  &__start___mmu_ftr_fixup, &__stop___mmu_ftr_fixup);
	do_feature_fixups(powerpc_firmware_features,
			  &__start___fw_ftr_fixup, &__stop___fw_ftr_fixup);
	do_lwsync_fixups(cur_cpu_spec->cpu_features,
			 &__start___lwsync_fixup, &__stop___lwsync_fixup);

	/*
	 * Unflatten the device-tree passed by prom_init or kexec
	 */
	unflatten_device_tree();

	/*
	 * Fill the ppc64_caches & systemcfg structures with informations
 	 * retrieved from the device-tree.
	 */
	initialize_cache_info();

	/*
	 * Initialize irq remapping subsystem
	 */
	irq_early_init();

#ifdef CONFIG_PPC_RTAS
	/*
	 * Initialize RTAS if available
	 */
	rtas_initialize();
#endif /* CONFIG_PPC_RTAS */

	/*
	 * Check if we have an initrd provided via the device-tree
	 */
	check_for_initrd();

	/*
	 * Do some platform specific early initializations, that includes
	 * setting up the hash table pointers. It also sets up some interrupt-mapping
	 * related options that will be used by finish_device_tree()
	 */
	if (ppc_md.init_early)
		ppc_md.init_early();

 	/*
	 * We can discover serial ports now since the above did setup the
	 * hash table management for us, thus ioremap works. We do that early
	 * so that further code can be debugged
	 */
	find_legacy_serial_ports();

	/*
	 * Register early console
	 */
	register_early_udbg_console();

	/*
	 * Initialize xmon
	 */
	xmon_setup();

	check_smt_enabled();
	smp_setup_cpu_maps();

#ifdef CONFIG_SMP
	/* Release secondary cpus out of their spinloops at 0x60 now that
	 * we can map physical -> logical CPU ids
	 */
	smp_release_cpus();
#endif

	printk("Starting Linux PPC64 %s\n", init_utsname()->version);

	printk("-----------------------------------------------------\n");
	printk("ppc64_pft_size                = 0x%llx\n", ppc64_pft_size);
	printk("physicalMemorySize            = 0x%llx\n", lmb_phys_mem_size());
	if (ppc64_caches.dline_size != 0x80)
		printk("ppc64_caches.dcache_line_size = 0x%x\n",
		       ppc64_caches.dline_size);
	if (ppc64_caches.iline_size != 0x80)
		printk("ppc64_caches.icache_line_size = 0x%x\n",
		       ppc64_caches.iline_size);
#ifdef CONFIG_PPC_STD_MMU_64
	if (htab_address)
		printk("htab_address                  = 0x%p\n", htab_address);
	printk("htab_hash_mask                = 0x%lx\n", htab_hash_mask);
#endif /* CONFIG_PPC_STD_MMU_64 */
	if (PHYSICAL_START > 0)
		printk("physical_start                = 0x%lx\n",
		       PHYSICAL_START);
	printk("-----------------------------------------------------\n");

	DBG(" <- setup_system()\n");
}

#ifdef CONFIG_IRQSTACKS
static void __init irqstack_early_init(void)
{
	unsigned int i;

	/*
	 * interrupt stacks must be under 256MB, we cannot afford to take
	 * SLB misses on them.
	 */
	for_each_possible_cpu(i) {
		softirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc_base(THREAD_SIZE,
					    THREAD_SIZE, 0x10000000));
		hardirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc_base(THREAD_SIZE,
					    THREAD_SIZE, 0x10000000));
	}
}
#else
#define irqstack_early_init()
#endif

/*
 * Stack space used when we detect a bad kernel stack pointer, and
 * early in SMP boots before relocation is enabled.
 */
static void __init emergency_stack_init(void)
{
	unsigned long limit;
	unsigned int i;

	/*
	 * Emergency stacks must be under 256MB, we cannot afford to take
	 * SLB misses on them. The ABI also requires them to be 128-byte
	 * aligned.
	 *
	 * Since we use these as temporary stacks during secondary CPU
	 * bringup, we need to get at them in real mode. This means they
	 * must also be within the RMO region.
	 */
	limit = min(0x10000000ULL, lmb.rmo_size);

	for_each_possible_cpu(i) {
		unsigned long sp;
		sp  = lmb_alloc_base(THREAD_SIZE, THREAD_SIZE, limit);
		sp += THREAD_SIZE;
		paca[i].emergency_sp = __va(sp);
	}
}

/*
 * Called into from start_kernel, after lock_kernel has been called.
 * Initializes bootmem, which is unsed to manage page allocation until
 * mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	ppc64_boot_msg(0x12, "Setup Arch");

	*cmdline_p = cmd_line;

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	dcache_bsize = ppc64_caches.dline_size;
	icache_bsize = ppc64_caches.iline_size;

	/* reboot on panic */
	panic_timeout = 180;

	if (ppc_md.panic)
		setup_panic();

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;
	
	irqstack_early_init();
	emergency_stack_init();

#ifdef CONFIG_PPC_STD_MMU_64
	stabs_alloc();
#endif
	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	sparse_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	if (ppc_md.setup_arch)
		ppc_md.setup_arch();

#ifdef CONFIG_SWIOTLB
	if (ppc_swiotlb_enable)
		swiotlb_init();
#endif

	paging_init();
	ppc64_boot_msg(0x15, "Setup Done");
}


/* ToDo: do something useful if ppc_md is not yet setup. */
#define PPC64_LINUX_FUNCTION 0x0f000000
#define PPC64_IPL_MESSAGE 0xc0000000
#define PPC64_TERM_MESSAGE 0xb0000000

static void ppc64_do_msg(unsigned int src, const char *msg)
{
	if (ppc_md.progress) {
		char buf[128];

		sprintf(buf, "%08X\n", src);
		ppc_md.progress(buf, 0);
		snprintf(buf, 128, "%s", msg);
		ppc_md.progress(buf, 0);
	}
}

/* Print a boot progress message. */
void ppc64_boot_msg(unsigned int src, const char *msg)
{
	ppc64_do_msg(PPC64_LINUX_FUNCTION|PPC64_IPL_MESSAGE|src, msg);
	printk("[boot]%04x %s\n", src, msg);
}

void cpu_die(void)
{
	if (ppc_md.cpu_die)
		ppc_md.cpu_die();
}

#ifdef CONFIG_SMP
void __init setup_per_cpu_areas(void)
{
	int i;
	unsigned long size;
	char *ptr;

	/* Copy section for each CPU (we discard the original) */
	size = ALIGN(__per_cpu_end - __per_cpu_start, PAGE_SIZE);
#ifdef CONFIG_MODULES
	if (size < PERCPU_ENOUGH_ROOM)
		size = PERCPU_ENOUGH_ROOM;
#endif

	for_each_possible_cpu(i) {
		ptr = alloc_bootmem_pages_node(NODE_DATA(cpu_to_node(i)), size);

		paca[i].data_offset = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
	}
}
#endif


#ifdef CONFIG_PPC_INDIRECT_IO
struct ppc_pci_io ppc_pci_io;
EXPORT_SYMBOL(ppc_pci_io);
#endif /* CONFIG_PPC_INDIRECT_IO */

