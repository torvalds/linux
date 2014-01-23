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

#define DEBUG

#include <linux/export.h>
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
#include <linux/memblock.h>
#include <linux/hugetlb.h>

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
#include <asm/mmu_context.h>
#include <asm/code-patching.h>
#include <asm/kvm_ppc.h>
#include <asm/hugetlb.h>
#include <asm/epapr_hcalls.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

int boot_cpuid = 0;
int spinning_secondaries;
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

static char *smt_enabled_cmdline;

/* Look for ibm,smt-enabled OF option */
static void check_smt_enabled(void)
{
	struct device_node *dn;
	const char *smt_option;

	/* Default to enabling all threads */
	smt_enabled_at_boot = threads_per_core;

	/* Allow the command line to overrule the OF option */
	if (smt_enabled_cmdline) {
		if (!strcmp(smt_enabled_cmdline, "on"))
			smt_enabled_at_boot = threads_per_core;
		else if (!strcmp(smt_enabled_cmdline, "off"))
			smt_enabled_at_boot = 0;
		else {
			long smt;
			int rc;

			rc = strict_strtol(smt_enabled_cmdline, 10, &smt);
			if (!rc)
				smt_enabled_at_boot =
					min(threads_per_core, (int)smt);
		}
	} else {
		dn = of_find_node_by_path("/options");
		if (dn) {
			smt_option = of_get_property(dn, "ibm,smt-enabled",
						     NULL);

			if (smt_option) {
				if (!strcmp(smt_option, "on"))
					smt_enabled_at_boot = threads_per_core;
				else if (!strcmp(smt_option, "off"))
					smt_enabled_at_boot = 0;
			}

			of_node_put(dn);
		}
	}
}

/* Look for smt-enabled= cmdline option */
static int __init early_smt_enabled(char *p)
{
	smt_enabled_cmdline = p;
	return 0;
}
early_param("smt-enabled", early_smt_enabled);

#else
#define check_smt_enabled()
#endif /* CONFIG_SMP */

/** Fix up paca fields required for the boot cpu */
static void fixup_boot_paca(void)
{
	/* The boot cpu is started */
	get_paca()->cpu_start = 1;
	/* Allow percpu accesses to work until we setup percpu data */
	get_paca()->data_offset = 0;
}

/*
 * Early initialization entry point. This is called by head.S
 * with MMU translation disabled. We rely on the "feature" of
 * the CPU that ignores the top 2 bits of the address in real
 * mode so we can access kernel globals normally provided we
 * only toy with things in the RMO region. From here, we do
 * some early parsing of the device-tree to setup out MEMBLOCK
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
	static __initdata struct paca_struct boot_paca;

	/* -------- printk is _NOT_ safe to use here ! ------- */

	/* Identify CPU type */
	identify_cpu(0, mfspr(SPRN_PVR));

	/* Assume we're on cpu 0 for now. Don't write to the paca yet! */
	initialise_paca(&boot_paca, 0);
	setup_paca(&boot_paca);
	fixup_boot_paca();

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

	epapr_paravirt_early_init();

	/* Now we know the logical id of our boot cpu, setup the paca. */
	setup_paca(&paca[boot_cpuid]);
	fixup_boot_paca();

	/* Probe the machine type */
	probe_machine();

	setup_kdump_trampoline();

	DBG("Found, Initializing memory management...\n");

	/* Initialize the hash table or TLB handling */
	early_init_mmu();

	kvm_cma_reserve();

	/*
	 * Reserve any gigantic pages requested on the command line.
	 * memblock needs to have been initialized by the time this is
	 * called since this will reserve memory.
	 */
	reserve_hugetlb_gpages();

	DBG(" <- early_setup()\n");

#ifdef CONFIG_PPC_EARLY_DEBUG_BOOTX
	/*
	 * This needs to be done *last* (after the above DBG() even)
	 *
	 * Right after we return from this function, we turn on the MMU
	 * which means the real-mode access trick that btext does will
	 * no longer work, it needs to switch to using a real MMU
	 * mapping. This call will ensure that it does
	 */
	btext_map();
#endif /* CONFIG_PPC_EARLY_DEBUG_BOOTX */
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
void smp_release_cpus(void)
{
	unsigned long *ptr;
	int i;

	DBG(" -> smp_release_cpus()\n");

	/* All secondary cpus are spinning on a common spinloop, release them
	 * all now so they can start to spin on their individual paca
	 * spinloops. For non SMP kernels, the secondary cpus never get out
	 * of the common spinloop.
	 */

	ptr  = (unsigned long *)((unsigned long)&__secondary_hold_spinloop
			- PHYSICAL_START);
	*ptr = __pa(generic_secondary_smp_init);

	/* And wait a bit for them to catch up */
	for (i = 0; i < 100000; i++) {
		mb();
		HMT_low();
		if (spinning_secondaries == 0)
			break;
		udelay(1);
	}
	DBG("spinning_secondaries = %d\n", spinning_secondaries);

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

	for_each_node_by_type(np, "cpu") {
		num_cpus += 1;

		/*
		 * We're assuming *all* of the CPUs have the same
		 * d-cache and i-cache sizes... -Peter
		 */
		if (num_cpus == 1) {
			const __be32 *sizep, *lsizep;
			u32 size, lsize;

			size = 0;
			lsize = cur_cpu_spec->dcache_bsize;
			sizep = of_get_property(np, "d-cache-size", NULL);
			if (sizep != NULL)
				size = be32_to_cpu(*sizep);
			lsizep = of_get_property(np, "d-cache-block-size",
						 NULL);
			/* fallback if block size missing */
			if (lsizep == NULL)
				lsizep = of_get_property(np,
							 "d-cache-line-size",
							 NULL);
			if (lsizep != NULL)
				lsize = be32_to_cpu(*lsizep);
			if (sizep == NULL || lsizep == NULL)
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
				size = be32_to_cpu(*sizep);
			lsizep = of_get_property(np, "i-cache-block-size",
						 NULL);
			if (lsizep == NULL)
				lsizep = of_get_property(np,
							 "i-cache-line-size",
							 NULL);
			if (lsizep != NULL)
				lsize = be32_to_cpu(*lsizep);
			if (sizep == NULL || lsizep == NULL)
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
	do_final_fixups();

	/*
	 * Unflatten the device-tree passed by prom_init or kexec
	 */
	unflatten_device_tree();

	/*
	 * Fill the ppc64_caches & systemcfg structures with informations
 	 * retrieved from the device-tree.
	 */
	initialize_cache_info();

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

	smp_setup_cpu_maps();
	check_smt_enabled();

#ifdef CONFIG_SMP
	/* Release secondary cpus out of their spinloops at 0x60 now that
	 * we can map physical -> logical CPU ids
	 */
	smp_release_cpus();
#endif

	printk("Starting Linux PPC64 %s\n", init_utsname()->version);

	printk("-----------------------------------------------------\n");
	printk("ppc64_pft_size                = 0x%llx\n", ppc64_pft_size);
	printk("physicalMemorySize            = 0x%llx\n", memblock_phys_mem_size());
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
		printk("physical_start                = 0x%llx\n",
		       (unsigned long long)PHYSICAL_START);
	printk("-----------------------------------------------------\n");

	DBG(" <- setup_system()\n");
}

/* This returns the limit below which memory accesses to the linear
 * mapping are guarnateed not to cause a TLB or SLB miss. This is
 * used to allocate interrupt or emergency stacks for which our
 * exception entry path doesn't deal with being interrupted.
 */
static u64 safe_stack_limit(void)
{
#ifdef CONFIG_PPC_BOOK3E
	/* Freescale BookE bolts the entire linear mapping */
	if (mmu_has_feature(MMU_FTR_TYPE_FSL_E))
		return linear_map_top;
	/* Other BookE, we assume the first GB is bolted */
	return 1ul << 30;
#else
	/* BookS, the first segment is bolted */
	if (mmu_has_feature(MMU_FTR_1T_SEGMENT))
		return 1UL << SID_SHIFT_1T;
	return 1UL << SID_SHIFT;
#endif
}

static void __init irqstack_early_init(void)
{
	u64 limit = safe_stack_limit();
	unsigned int i;

	/*
	 * Interrupt stacks must be in the first segment since we
	 * cannot afford to take SLB misses on them.
	 */
	for_each_possible_cpu(i) {
		softirq_ctx[i] = (struct thread_info *)
			__va(memblock_alloc_base(THREAD_SIZE,
					    THREAD_SIZE, limit));
		hardirq_ctx[i] = (struct thread_info *)
			__va(memblock_alloc_base(THREAD_SIZE,
					    THREAD_SIZE, limit));
	}
}

#ifdef CONFIG_PPC_BOOK3E
static void __init exc_lvl_early_init(void)
{
	extern unsigned int interrupt_base_book3e;
	extern unsigned int exc_debug_debug_book3e;

	unsigned int i;

	for_each_possible_cpu(i) {
		critirq_ctx[i] = (struct thread_info *)
			__va(memblock_alloc(THREAD_SIZE, THREAD_SIZE));
		dbgirq_ctx[i] = (struct thread_info *)
			__va(memblock_alloc(THREAD_SIZE, THREAD_SIZE));
		mcheckirq_ctx[i] = (struct thread_info *)
			__va(memblock_alloc(THREAD_SIZE, THREAD_SIZE));
	}

	if (cpu_has_feature(CPU_FTR_DEBUG_LVL_EXC))
		patch_branch(&interrupt_base_book3e + (0x040 / 4) + 1,
			     (unsigned long)&exc_debug_debug_book3e, 0);
}
#else
#define exc_lvl_early_init()
#endif

/*
 * Stack space used when we detect a bad kernel stack pointer, and
 * early in SMP boots before relocation is enabled.
 */
static void __init emergency_stack_init(void)
{
	u64 limit;
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
	limit = min(safe_stack_limit(), ppc64_rma_size);

	for_each_possible_cpu(i) {
		unsigned long sp;
		sp  = memblock_alloc_base(THREAD_SIZE, THREAD_SIZE, limit);
		sp += THREAD_SIZE;
		paca[i].emergency_sp = __va(sp);
	}
}

/*
 * Called into from start_kernel this initializes bootmem, which is used
 * to manage page allocation until mem_init is called.
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
#ifdef CONFIG_PPC_64K_PAGES
	init_mm.context.pte_frag = NULL;
#endif
	irqstack_early_init();
	exc_lvl_early_init();
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

	paging_init();

	/* Initialize the MMU context management stuff */
	mmu_context_init();

	/* Interrupt code needs to be 64K-aligned */
	if ((unsigned long)_stext & 0xffff)
		panic("Kernelbase not 64K-aligned (0x%lx)!\n",
		      (unsigned long)_stext);

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

#ifdef CONFIG_SMP
#define PCPU_DYN_SIZE		()

static void * __init pcpu_fc_alloc(unsigned int cpu, size_t size, size_t align)
{
	return __alloc_bootmem_node(NODE_DATA(cpu_to_node(cpu)), size, align,
				    __pa(MAX_DMA_ADDRESS));
}

static void __init pcpu_fc_free(void *ptr, size_t size)
{
	free_bootmem(__pa(ptr), size);
}

static int pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	if (cpu_to_node(from) == cpu_to_node(to))
		return LOCAL_DISTANCE;
	else
		return REMOTE_DISTANCE;
}

unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

void __init setup_per_cpu_areas(void)
{
	const size_t dyn_size = PERCPU_MODULE_RESERVE + PERCPU_DYNAMIC_RESERVE;
	size_t atom_size;
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Linear mapping is one of 4K, 1M and 16M.  For 4K, no need
	 * to group units.  For larger mappings, use 1M atom which
	 * should be large enough to contain a number of units.
	 */
	if (mmu_linear_psize == MMU_PAGE_4K)
		atom_size = PAGE_SIZE;
	else
		atom_size = 1 << 20;

	rc = pcpu_embed_first_chunk(0, dyn_size, atom_size, pcpu_cpu_distance,
				    pcpu_fc_alloc, pcpu_fc_free);
	if (rc < 0)
		panic("cannot initialize percpu area (err=%d)", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu) {
                __per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
		paca[cpu].data_offset = __per_cpu_offset[cpu];
	}
}
#endif


#if defined(CONFIG_PPC_INDIRECT_PIO) || defined(CONFIG_PPC_INDIRECT_MMIO)
struct ppc_pci_io ppc_pci_io;
EXPORT_SYMBOL(ppc_pci_io);
#endif
