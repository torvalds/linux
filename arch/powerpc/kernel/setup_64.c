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
#include <linux/memory.h>
#include <linux/nmi.h>

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
#include <asm/livepatch.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

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

#if defined(CONFIG_PPC_BOOK3E) && defined(CONFIG_SMP)
static void setup_tlb_core_data(void)
{
	int cpu;

	BUILD_BUG_ON(offsetof(struct tlb_core_data, lock) != 0);

	for_each_possible_cpu(cpu) {
		int first = cpu_first_thread_sibling(cpu);

		/*
		 * If we boot via kdump on a non-primary thread,
		 * make sure we point at the thread that actually
		 * set up this TLB.
		 */
		if (cpu_first_thread_sibling(boot_cpuid) == first)
			first = boot_cpuid;

		paca[cpu].tcd_ptr = &paca[first].tcd;

		/*
		 * If we have threads, we need either tlbsrx.
		 * or e6500 tablewalk mode, or else TLB handlers
		 * will be racy and could produce duplicate entries.
		 */
		if (smt_enabled_at_boot >= 2 &&
		    !mmu_has_feature(MMU_FTR_USE_TLBRSRV) &&
		    book3e_htw_mode != PPC_HTW_E6500) {
			/* Should we panic instead? */
			WARN_ONCE("%s: unsupported MMU configuration -- expect problems\n",
				  __func__);
		}
	}
}
#else
static void setup_tlb_core_data(void)
{
}
#endif

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
			int smt;
			int rc;

			rc = kstrtoint(smt_enabled_cmdline, 10, &smt);
			if (!rc)
				smt_enabled_at_boot =
					min(threads_per_core, smt);
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

static void cpu_ready_for_interrupts(void)
{
	/* Set IR and DR in PACA MSR */
	get_paca()->kernel_msr = MSR_KERNEL;

	/*
	 * Enable AIL if supported, and we are in hypervisor mode. If we are
	 * not in hypervisor mode, we enable relocation-on interrupts later
	 * in pSeries_setup_arch() using the H_SET_MODE hcall.
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S)) {
		unsigned long lpcr = mfspr(SPRN_LPCR);
		mtspr(SPRN_LPCR, lpcr | LPCR_AIL_3);
	}
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
	setup_paca(&paca[boot_cpuid]);
	fixup_boot_paca();

	/* Probe the machine type */
	probe_machine();

	/*
	 * Setup the trampolines from the lowmem exception vectors
	 * to the kdump kernel when not using a relocatable kernel.
	 */
	setup_kdump_trampoline();

	/* Initialize the hash table or TLB handling */
	early_init_mmu();

	/* Apply all the dynamic patching */
	apply_feature_fixups();

	/*
	 * At this point, we can let interrupts switch to virtual mode
	 * (the MMU has been setup), so adjust the MSR in the PACA to
	 * have IR and DR set and enable AIL if it exists
	 */
	cpu_ready_for_interrupts();

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
	/* Mark interrupts disabled in PACA */
	get_paca()->soft_enabled = 0;

	/* Initialize the hash table or TLB handling */
	early_init_mmu_secondary();

	/*
	 * At this point, we can let interrupts switch to virtual mode
	 * (the MMU has been setup), so adjust the MSR in the PACA to
	 * have IR and DR set.
	 */
	cpu_ready_for_interrupts();
}

#endif /* CONFIG_SMP */

#if defined(CONFIG_SMP) || defined(CONFIG_KEXEC)
static bool use_spinloop(void)
{
	if (!IS_ENABLED(CONFIG_PPC_BOOK3E))
		return true;

	/*
	 * When book3e boots from kexec, the ePAPR spin table does
	 * not get used.
	 */
	return of_property_read_bool(of_chosen, "linux,booted-from-kexec");
}

void smp_release_cpus(void)
{
	unsigned long *ptr;
	int i;

	if (!use_spinloop())
		return;

	DBG(" -> smp_release_cpus()\n");

	/* All secondary cpus are spinning on a common spinloop, release them
	 * all now so they can start to spin on their individual paca
	 * spinloops. For non SMP kernels, the secondary cpus never get out
	 * of the common spinloop.
	 */

	ptr  = (unsigned long *)((unsigned long)&__secondary_hold_spinloop
			- PHYSICAL_START);
	*ptr = ppc_function_entry(generic_secondary_smp_init);

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
	setup_tlb_core_data();

	/*
	 * Freescale Book3e parts spin in a loop provided by firmware,
	 * so smp_release_cpus() does nothing for them
	 */
#if defined(CONFIG_SMP)
	/* Release secondary cpus out of their spinloops at 0x60 now that
	 * we can map physical -> logical CPU ids
	 */
	smp_release_cpus();
#endif

	pr_info("Starting Linux %s %s\n", init_utsname()->machine,
		 init_utsname()->version);

	pr_info("-----------------------------------------------------\n");
	pr_info("ppc64_pft_size    = 0x%llx\n", ppc64_pft_size);
	pr_info("phys_mem_size     = 0x%llx\n", memblock_phys_mem_size());

	if (ppc64_caches.dline_size != 0x80)
		pr_info("dcache_line_size  = 0x%x\n", ppc64_caches.dline_size);
	if (ppc64_caches.iline_size != 0x80)
		pr_info("icache_line_size  = 0x%x\n", ppc64_caches.iline_size);

	pr_info("cpu_features      = 0x%016lx\n", cur_cpu_spec->cpu_features);
	pr_info("  possible        = 0x%016lx\n", CPU_FTRS_POSSIBLE);
	pr_info("  always          = 0x%016lx\n", CPU_FTRS_ALWAYS);
	pr_info("cpu_user_features = 0x%08x 0x%08x\n", cur_cpu_spec->cpu_user_features,
		cur_cpu_spec->cpu_user_features2);
	pr_info("mmu_features      = 0x%08x\n", cur_cpu_spec->mmu_features);
	pr_info("firmware_features = 0x%016lx\n", powerpc_firmware_features);

#ifdef CONFIG_PPC_STD_MMU_64
	if (htab_address)
		pr_info("htab_address      = 0x%p\n", htab_address);

	pr_info("htab_hash_mask    = 0x%lx\n", htab_hash_mask);
#endif

	if (PHYSICAL_START > 0)
		pr_info("physical_start    = 0x%llx\n",
		       (unsigned long long)PHYSICAL_START);
	pr_info("-----------------------------------------------------\n");

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
	unsigned int i;
	unsigned long sp;

	for_each_possible_cpu(i) {
		sp = memblock_alloc(THREAD_SIZE, THREAD_SIZE);
		critirq_ctx[i] = (struct thread_info *)__va(sp);
		paca[i].crit_kstack = __va(sp + THREAD_SIZE);

		sp = memblock_alloc(THREAD_SIZE, THREAD_SIZE);
		dbgirq_ctx[i] = (struct thread_info *)__va(sp);
		paca[i].dbg_kstack = __va(sp + THREAD_SIZE);

		sp = memblock_alloc(THREAD_SIZE, THREAD_SIZE);
		mcheckirq_ctx[i] = (struct thread_info *)__va(sp);
		paca[i].mc_kstack = __va(sp + THREAD_SIZE);
	}

	if (cpu_has_feature(CPU_FTR_DEBUG_LVL_EXC))
		patch_exception(0x040, exc_debug_debug_book3e);
}
#else
#define exc_lvl_early_init()
#endif

/*
 * Stack space used when we detect a bad kernel stack pointer, and
 * early in SMP boots before relocation is enabled. Exclusive emergency
 * stack for machine checks.
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
		struct thread_info *ti;
		ti = __va(memblock_alloc_base(THREAD_SIZE, THREAD_SIZE, limit));
		klp_init_thread_info(ti);
		paca[i].emergency_sp = (void *)ti + THREAD_SIZE;

#ifdef CONFIG_PPC_BOOK3S_64
		/* emergency stack for machine check exception handling. */
		ti = __va(memblock_alloc_base(THREAD_SIZE, THREAD_SIZE, limit));
		klp_init_thread_info(ti);
		paca[i].mc_emergency_sp = (void *)ti + THREAD_SIZE;
#endif
	}
}

/*
 * Called into from start_kernel this initializes memblock, which is used
 * to manage page allocation until mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	*cmdline_p = boot_command_line;

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	dcache_bsize = ppc64_caches.dline_size;
	icache_bsize = ppc64_caches.iline_size;


	/* Reserve large chunks of memory for use by CMA for KVM */
	kvm_cma_reserve();

	/*
	 * Reserve any gigantic pages requested on the command line.
	 * memblock needs to have been initialized by the time this is
	 * called since this will reserve memory.
	 */
	reserve_hugetlb_gpages();

	if (ppc_md.panic)
		setup_panic();

	klp_init_thread_info(&init_thread_info);

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;
#ifdef CONFIG_PPC_64K_PAGES
	init_mm.context.pte_frag = NULL;
#endif
#ifdef CONFIG_SPAPR_TCE_IOMMU
	mm_iommu_init(&init_mm.context);
#endif
	irqstack_early_init();
	exc_lvl_early_init();
	emergency_stack_init();

	initmem_init();

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

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
unsigned long memory_block_size_bytes(void)
{
	if (ppc_md.memory_block_size)
		return ppc_md.memory_block_size();

	return MIN_MEMORY_BLOCK_SIZE;
}
#endif

#if defined(CONFIG_PPC_INDIRECT_PIO) || defined(CONFIG_PPC_INDIRECT_MMIO)
struct ppc_pci_io ppc_pci_io;
EXPORT_SYMBOL(ppc_pci_io);
#endif

#ifdef CONFIG_HARDLOCKUP_DETECTOR
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	return ppc_proc_freq * watchdog_thresh;
}

/*
 * The hardlockup detector breaks PMU event based branches and is likely
 * to get false positives in KVM guests, so disable it by default.
 */
static int __init disable_hardlockup_detector(void)
{
	hardlockup_detector_disable();

	return 0;
}
early_initcall(disable_hardlockup_detector);
#endif
