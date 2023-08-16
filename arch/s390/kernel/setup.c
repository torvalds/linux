// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "arch/i386/kernel/setup.c"
 *    Copyright (C) 1995, Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#define KMSG_COMPONENT "setup"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/kernel_stat.h>
#include <linux/dma-map-ops.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pfn.h>
#include <linux/ctype.h>
#include <linux/reboot.h>
#include <linux/topology.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/memory.h>
#include <linux/compat.h>
#include <linux/start_kernel.h>
#include <linux/hugetlb.h>
#include <linux/kmemleak.h>

#include <asm/archrandom.h>
#include <asm/boot_data.h>
#include <asm/ipl.h>
#include <asm/facility.h>
#include <asm/smp.h>
#include <asm/mmu_context.h>
#include <asm/cpcmd.h>
#include <asm/abs_lowcore.h>
#include <asm/nmi.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/ebcdic.h>
#include <asm/diag.h>
#include <asm/os_info.h>
#include <asm/sclp.h>
#include <asm/stacktrace.h>
#include <asm/sysinfo.h>
#include <asm/numa.h>
#include <asm/alternative.h>
#include <asm/nospec-branch.h>
#include <asm/physmem_info.h>
#include <asm/maccess.h>
#include <asm/uv.h>
#include <asm/asm-offsets.h>
#include "entry.h"

/*
 * Machine setup..
 */
unsigned int console_mode = 0;
EXPORT_SYMBOL(console_mode);

unsigned int console_devno = -1;
EXPORT_SYMBOL(console_devno);

unsigned int console_irq = -1;
EXPORT_SYMBOL(console_irq);

/*
 * Some code and data needs to stay below 2 GB, even when the kernel would be
 * relocated above 2 GB, because it has to use 31 bit addresses.
 * Such code and data is part of the .amode31 section.
 */
unsigned long __amode31_ref __samode31 = (unsigned long)&_samode31;
unsigned long __amode31_ref __eamode31 = (unsigned long)&_eamode31;
unsigned long __amode31_ref __stext_amode31 = (unsigned long)&_stext_amode31;
unsigned long __amode31_ref __etext_amode31 = (unsigned long)&_etext_amode31;
struct exception_table_entry __amode31_ref *__start_amode31_ex_table = _start_amode31_ex_table;
struct exception_table_entry __amode31_ref *__stop_amode31_ex_table = _stop_amode31_ex_table;

/*
 * Control registers CR2, CR5 and CR15 are initialized with addresses
 * of tables that must be placed below 2G which is handled by the AMODE31
 * sections.
 * Because the AMODE31 sections are relocated below 2G at startup,
 * the content of control registers CR2, CR5 and CR15 must be updated
 * with new addresses after the relocation. The initial initialization of
 * control registers occurs in head64.S and then gets updated again after AMODE31
 * relocation. We must access the relevant AMODE31 tables indirectly via
 * pointers placed in the .amode31.refs linker section. Those pointers get
 * updated automatically during AMODE31 relocation and always contain a valid
 * address within AMODE31 sections.
 */

static __amode31_data u32 __ctl_duct_amode31[16] __aligned(64);

static __amode31_data u64 __ctl_aste_amode31[8] __aligned(64) = {
	[1] = 0xffffffffffffffff
};

static __amode31_data u32 __ctl_duald_amode31[32] __aligned(128) = {
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0,
	0x80000000, 0, 0, 0
};

static __amode31_data u32 __ctl_linkage_stack_amode31[8] __aligned(64) = {
	0, 0, 0x89000000, 0,
	0, 0, 0x8a000000, 0
};

static u64 __amode31_ref *__ctl_aste = __ctl_aste_amode31;
static u32 __amode31_ref *__ctl_duald = __ctl_duald_amode31;
static u32 __amode31_ref *__ctl_linkage_stack = __ctl_linkage_stack_amode31;
static u32 __amode31_ref *__ctl_duct = __ctl_duct_amode31;

int __bootdata(noexec_disabled);
unsigned long __bootdata_preserved(max_mappable);
unsigned long __bootdata(ident_map_size);
struct physmem_info __bootdata(physmem_info);

unsigned long __bootdata_preserved(__kaslr_offset);
int __bootdata_preserved(__kaslr_enabled);
unsigned int __bootdata_preserved(zlib_dfltcc_support);
EXPORT_SYMBOL(zlib_dfltcc_support);
u64 __bootdata_preserved(stfle_fac_list[16]);
EXPORT_SYMBOL(stfle_fac_list);
u64 __bootdata_preserved(alt_stfle_fac_list[16]);
struct oldmem_data __bootdata_preserved(oldmem_data);

unsigned long VMALLOC_START;
EXPORT_SYMBOL(VMALLOC_START);

unsigned long VMALLOC_END;
EXPORT_SYMBOL(VMALLOC_END);

struct page *vmemmap;
EXPORT_SYMBOL(vmemmap);
unsigned long vmemmap_size;

unsigned long MODULES_VADDR;
unsigned long MODULES_END;

/* An array with a pointer to the lowcore of every CPU. */
struct lowcore *lowcore_ptr[NR_CPUS];
EXPORT_SYMBOL(lowcore_ptr);

DEFINE_STATIC_KEY_FALSE(cpu_has_bear);

/*
 * The Write Back bit position in the physaddr is given by the SLPC PCI.
 * Leaving the mask zero always uses write through which is safe
 */
unsigned long mio_wb_bit_mask __ro_after_init;

/*
 * This is set up by the setup-routine at boot-time
 * for S390 need to find out, what we have to setup
 * using address 0x10400 ...
 */

#include <asm/setup.h>

/*
 * condev= and conmode= setup parameter.
 */

static int __init condev_setup(char *str)
{
	int vdev;

	vdev = simple_strtoul(str, &str, 0);
	if (vdev >= 0 && vdev < 65536) {
		console_devno = vdev;
		console_irq = -1;
	}
	return 1;
}

__setup("condev=", condev_setup);

static void __init set_preferred_console(void)
{
	if (CONSOLE_IS_3215 || CONSOLE_IS_SCLP)
		add_preferred_console("ttyS", 0, NULL);
	else if (CONSOLE_IS_3270)
		add_preferred_console("tty3270", 0, NULL);
	else if (CONSOLE_IS_VT220)
		add_preferred_console("ttysclp", 0, NULL);
	else if (CONSOLE_IS_HVC)
		add_preferred_console("hvc", 0, NULL);
}

static int __init conmode_setup(char *str)
{
#if defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
	if (!strcmp(str, "hwc") || !strcmp(str, "sclp"))
                SET_CONSOLE_SCLP;
#endif
#if defined(CONFIG_TN3215_CONSOLE)
	if (!strcmp(str, "3215"))
		SET_CONSOLE_3215;
#endif
#if defined(CONFIG_TN3270_CONSOLE)
	if (!strcmp(str, "3270"))
		SET_CONSOLE_3270;
#endif
	set_preferred_console();
        return 1;
}

__setup("conmode=", conmode_setup);

static void __init conmode_default(void)
{
	char query_buffer[1024];
	char *ptr;

        if (MACHINE_IS_VM) {
		cpcmd("QUERY CONSOLE", query_buffer, 1024, NULL);
		console_devno = simple_strtoul(query_buffer + 5, NULL, 16);
		ptr = strstr(query_buffer, "SUBCHANNEL =");
		console_irq = simple_strtoul(ptr + 13, NULL, 16);
		cpcmd("QUERY TERM", query_buffer, 1024, NULL);
		ptr = strstr(query_buffer, "CONMODE");
		/*
		 * Set the conmode to 3215 so that the device recognition 
		 * will set the cu_type of the console to 3215. If the
		 * conmode is 3270 and we don't set it back then both
		 * 3215 and the 3270 driver will try to access the console
		 * device (3215 as console and 3270 as normal tty).
		 */
		cpcmd("TERM CONMODE 3215", NULL, 0, NULL);
		if (ptr == NULL) {
#if defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
			return;
		}
		if (str_has_prefix(ptr + 8, "3270")) {
#if defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		} else if (str_has_prefix(ptr + 8, "3215")) {
#if defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		}
	} else if (MACHINE_IS_KVM) {
		if (sclp.has_vt220 && IS_ENABLED(CONFIG_SCLP_VT220_CONSOLE))
			SET_CONSOLE_VT220;
		else if (sclp.has_linemode && IS_ENABLED(CONFIG_SCLP_CONSOLE))
			SET_CONSOLE_SCLP;
		else
			SET_CONSOLE_HVC;
	} else {
#if defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
		SET_CONSOLE_SCLP;
#endif
	}
}

#ifdef CONFIG_CRASH_DUMP
static void __init setup_zfcpdump(void)
{
	if (!is_ipl_type_dump())
		return;
	if (oldmem_data.start)
		return;
	strcat(boot_command_line, " cio_ignore=all,!ipldev,!condev");
	console_loglevel = 2;
}
#else
static inline void setup_zfcpdump(void) {}
#endif /* CONFIG_CRASH_DUMP */

 /*
 * Reboot, halt and power_off stubs. They just call _machine_restart,
 * _machine_halt or _machine_power_off. 
 */

void machine_restart(char *command)
{
	if ((!in_interrupt() && !in_atomic()) || oops_in_progress)
		/*
		 * Only unblank the console if we are called in enabled
		 * context or a bust_spinlocks cleared the way for us.
		 */
		console_unblank();
	_machine_restart(command);
}

void machine_halt(void)
{
	if (!in_interrupt() || oops_in_progress)
		/*
		 * Only unblank the console if we are called in enabled
		 * context or a bust_spinlocks cleared the way for us.
		 */
		console_unblank();
	_machine_halt();
}

void machine_power_off(void)
{
	if (!in_interrupt() || oops_in_progress)
		/*
		 * Only unblank the console if we are called in enabled
		 * context or a bust_spinlocks cleared the way for us.
		 */
		console_unblank();
	_machine_power_off();
}

/*
 * Dummy power off function.
 */
void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL_GPL(pm_power_off);

void *restart_stack;

unsigned long stack_alloc(void)
{
#ifdef CONFIG_VMAP_STACK
	void *ret;

	ret = __vmalloc_node(THREAD_SIZE, THREAD_SIZE, THREADINFO_GFP,
			     NUMA_NO_NODE, __builtin_return_address(0));
	kmemleak_not_leak(ret);
	return (unsigned long)ret;
#else
	return __get_free_pages(GFP_KERNEL, THREAD_SIZE_ORDER);
#endif
}

void stack_free(unsigned long stack)
{
#ifdef CONFIG_VMAP_STACK
	vfree((void *) stack);
#else
	free_pages(stack, THREAD_SIZE_ORDER);
#endif
}

void __init __noreturn arch_call_rest_init(void)
{
	smp_reinit_ipl_cpu();
	rest_init();
}

static unsigned long __init stack_alloc_early(void)
{
	unsigned long stack;

	stack = (unsigned long)memblock_alloc(THREAD_SIZE, THREAD_SIZE);
	if (!stack) {
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, THREAD_SIZE, THREAD_SIZE);
	}
	return stack;
}

static void __init setup_lowcore(void)
{
	struct lowcore *lc, *abs_lc;

	/*
	 * Setup lowcore for boot cpu
	 */
	BUILD_BUG_ON(sizeof(struct lowcore) != LC_PAGES * PAGE_SIZE);
	lc = memblock_alloc_low(sizeof(*lc), sizeof(*lc));
	if (!lc)
		panic("%s: Failed to allocate %zu bytes align=%zx\n",
		      __func__, sizeof(*lc), sizeof(*lc));

	lc->restart_psw.mask = PSW_KERNEL_BITS & ~PSW_MASK_DAT;
	lc->restart_psw.addr = __pa(restart_int_handler);
	lc->external_new_psw.mask = PSW_KERNEL_BITS | PSW_MASK_MCHECK;
	lc->external_new_psw.addr = (unsigned long) ext_int_handler;
	lc->svc_new_psw.mask = PSW_KERNEL_BITS | PSW_MASK_MCHECK;
	lc->svc_new_psw.addr = (unsigned long) system_call;
	lc->program_new_psw.mask = PSW_KERNEL_BITS | PSW_MASK_MCHECK;
	lc->program_new_psw.addr = (unsigned long) pgm_check_handler;
	lc->mcck_new_psw.mask = PSW_KERNEL_BITS;
	lc->mcck_new_psw.addr = (unsigned long) mcck_int_handler;
	lc->io_new_psw.mask = PSW_KERNEL_BITS | PSW_MASK_MCHECK;
	lc->io_new_psw.addr = (unsigned long) io_int_handler;
	lc->clock_comparator = clock_comparator_max;
	lc->current_task = (unsigned long)&init_task;
	lc->lpp = LPP_MAGIC;
	lc->machine_flags = S390_lowcore.machine_flags;
	lc->preempt_count = S390_lowcore.preempt_count;
	nmi_alloc_mcesa_early(&lc->mcesad);
	lc->sys_enter_timer = S390_lowcore.sys_enter_timer;
	lc->exit_timer = S390_lowcore.exit_timer;
	lc->user_timer = S390_lowcore.user_timer;
	lc->system_timer = S390_lowcore.system_timer;
	lc->steal_timer = S390_lowcore.steal_timer;
	lc->last_update_timer = S390_lowcore.last_update_timer;
	lc->last_update_clock = S390_lowcore.last_update_clock;
	/*
	 * Allocate the global restart stack which is the same for
	 * all CPUs in case *one* of them does a PSW restart.
	 */
	restart_stack = (void *)(stack_alloc_early() + STACK_INIT_OFFSET);
	lc->mcck_stack = stack_alloc_early() + STACK_INIT_OFFSET;
	lc->async_stack = stack_alloc_early() + STACK_INIT_OFFSET;
	lc->nodat_stack = stack_alloc_early() + STACK_INIT_OFFSET;
	lc->kernel_stack = S390_lowcore.kernel_stack;
	/*
	 * Set up PSW restart to call ipl.c:do_restart(). Copy the relevant
	 * restart data to the absolute zero lowcore. This is necessary if
	 * PSW restart is done on an offline CPU that has lowcore zero.
	 */
	lc->restart_stack = (unsigned long) restart_stack;
	lc->restart_fn = (unsigned long) do_restart;
	lc->restart_data = 0;
	lc->restart_source = -1U;
	__ctl_store(lc->cregs_save_area, 0, 15);
	lc->spinlock_lockval = arch_spin_lockval(0);
	lc->spinlock_index = 0;
	arch_spin_lock_setup(0);
	lc->return_lpswe = gen_lpswe(__LC_RETURN_PSW);
	lc->return_mcck_lpswe = gen_lpswe(__LC_RETURN_MCCK_PSW);
	lc->preempt_count = PREEMPT_DISABLED;
	lc->kernel_asce = S390_lowcore.kernel_asce;
	lc->user_asce = S390_lowcore.user_asce;

	abs_lc = get_abs_lowcore();
	abs_lc->restart_stack = lc->restart_stack;
	abs_lc->restart_fn = lc->restart_fn;
	abs_lc->restart_data = lc->restart_data;
	abs_lc->restart_source = lc->restart_source;
	abs_lc->restart_psw = lc->restart_psw;
	abs_lc->restart_flags = RESTART_FLAG_CTLREGS;
	memcpy(abs_lc->cregs_save_area, lc->cregs_save_area, sizeof(abs_lc->cregs_save_area));
	abs_lc->program_new_psw = lc->program_new_psw;
	abs_lc->mcesad = lc->mcesad;
	put_abs_lowcore(abs_lc);

	set_prefix(__pa(lc));
	lowcore_ptr[0] = lc;
	if (abs_lowcore_map(0, lowcore_ptr[0], false))
		panic("Couldn't setup absolute lowcore");
}

static struct resource code_resource = {
	.name  = "Kernel code",
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
};

static struct resource bss_resource = {
	.name = "Kernel bss",
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
};

static struct resource __initdata *standard_resources[] = {
	&code_resource,
	&data_resource,
	&bss_resource,
};

static void __init setup_resources(void)
{
	struct resource *res, *std_res, *sub_res;
	phys_addr_t start, end;
	int j;
	u64 i;

	code_resource.start = (unsigned long) _text;
	code_resource.end = (unsigned long) _etext - 1;
	data_resource.start = (unsigned long) _etext;
	data_resource.end = (unsigned long) _edata - 1;
	bss_resource.start = (unsigned long) __bss_start;
	bss_resource.end = (unsigned long) __bss_stop - 1;

	for_each_mem_range(i, &start, &end) {
		res = memblock_alloc(sizeof(*res), 8);
		if (!res)
			panic("%s: Failed to allocate %zu bytes align=0x%x\n",
			      __func__, sizeof(*res), 8);
		res->flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM;

		res->name = "System RAM";
		res->start = start;
		/*
		 * In memblock, end points to the first byte after the
		 * range while in resources, end points to the last byte in
		 * the range.
		 */
		res->end = end - 1;
		request_resource(&iomem_resource, res);

		for (j = 0; j < ARRAY_SIZE(standard_resources); j++) {
			std_res = standard_resources[j];
			if (std_res->start < res->start ||
			    std_res->start > res->end)
				continue;
			if (std_res->end > res->end) {
				sub_res = memblock_alloc(sizeof(*sub_res), 8);
				if (!sub_res)
					panic("%s: Failed to allocate %zu bytes align=0x%x\n",
					      __func__, sizeof(*sub_res), 8);
				*sub_res = *std_res;
				sub_res->end = res->end;
				std_res->start = res->end + 1;
				request_resource(res, sub_res);
			} else {
				request_resource(res, std_res);
			}
		}
	}
#ifdef CONFIG_CRASH_DUMP
	/*
	 * Re-add removed crash kernel memory as reserved memory. This makes
	 * sure it will be mapped with the identity mapping and struct pages
	 * will be created, so it can be resized later on.
	 * However add it later since the crash kernel resource should not be
	 * part of the System RAM resource.
	 */
	if (crashk_res.end) {
		memblock_add_node(crashk_res.start, resource_size(&crashk_res),
				  0, MEMBLOCK_NONE);
		memblock_reserve(crashk_res.start, resource_size(&crashk_res));
		insert_resource(&iomem_resource, &crashk_res);
	}
#endif
}

static void __init setup_memory_end(void)
{
	max_pfn = max_low_pfn = PFN_DOWN(ident_map_size);
	pr_notice("The maximum memory size is %luMB\n", ident_map_size >> 20);
}

#ifdef CONFIG_CRASH_DUMP

/*
 * When kdump is enabled, we have to ensure that no memory from the area
 * [0 - crashkernel memory size] is set offline - it will be exchanged with
 * the crashkernel memory region when kdump is triggered. The crashkernel
 * memory region can never get offlined (pages are unmovable).
 */
static int kdump_mem_notifier(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct memory_notify *arg = data;

	if (action != MEM_GOING_OFFLINE)
		return NOTIFY_OK;
	if (arg->start_pfn < PFN_DOWN(resource_size(&crashk_res)))
		return NOTIFY_BAD;
	return NOTIFY_OK;
}

static struct notifier_block kdump_mem_nb = {
	.notifier_call = kdump_mem_notifier,
};

#endif

/*
 * Reserve page tables created by decompressor
 */
static void __init reserve_pgtables(void)
{
	unsigned long start, end;
	struct reserved_range *range;

	for_each_physmem_reserved_type_range(RR_VMEM, range, &start, &end)
		memblock_reserve(start, end - start);
}

/*
 * Reserve memory for kdump kernel to be loaded with kexec
 */
static void __init reserve_crashkernel(void)
{
#ifdef CONFIG_CRASH_DUMP
	unsigned long long crash_base, crash_size;
	phys_addr_t low, high;
	int rc;

	rc = parse_crashkernel(boot_command_line, ident_map_size, &crash_size,
			       &crash_base);

	crash_base = ALIGN(crash_base, KEXEC_CRASH_MEM_ALIGN);
	crash_size = ALIGN(crash_size, KEXEC_CRASH_MEM_ALIGN);
	if (rc || crash_size == 0)
		return;

	if (memblock.memory.regions[0].size < crash_size) {
		pr_info("crashkernel reservation failed: %s\n",
			"first memory chunk must be at least crashkernel size");
		return;
	}

	low = crash_base ?: oldmem_data.start;
	high = low + crash_size;
	if (low >= oldmem_data.start && high <= oldmem_data.start + oldmem_data.size) {
		/* The crashkernel fits into OLDMEM, reuse OLDMEM */
		crash_base = low;
	} else {
		/* Find suitable area in free memory */
		low = max_t(unsigned long, crash_size, sclp.hsa_size);
		high = crash_base ? crash_base + crash_size : ULONG_MAX;

		if (crash_base && crash_base < low) {
			pr_info("crashkernel reservation failed: %s\n",
				"crash_base too low");
			return;
		}
		low = crash_base ?: low;
		crash_base = memblock_phys_alloc_range(crash_size,
						       KEXEC_CRASH_MEM_ALIGN,
						       low, high);
	}

	if (!crash_base) {
		pr_info("crashkernel reservation failed: %s\n",
			"no suitable area found");
		return;
	}

	if (register_memory_notifier(&kdump_mem_nb)) {
		memblock_phys_free(crash_base, crash_size);
		return;
	}

	if (!oldmem_data.start && MACHINE_IS_VM)
		diag10_range(PFN_DOWN(crash_base), PFN_DOWN(crash_size));
	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
	memblock_remove(crash_base, crash_size);
	pr_info("Reserving %lluMB of memory at %lluMB "
		"for crashkernel (System RAM: %luMB)\n",
		crash_size >> 20, crash_base >> 20,
		(unsigned long)memblock.memory.total_size >> 20);
	os_info_crashkernel_add(crash_base, crash_size);
#endif
}

/*
 * Reserve the initrd from being used by memblock
 */
static void __init reserve_initrd(void)
{
	unsigned long addr, size;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD) || !get_physmem_reserved(RR_INITRD, &addr, &size))
		return;
	initrd_start = (unsigned long)__va(addr);
	initrd_end = initrd_start + size;
	memblock_reserve(addr, size);
}

/*
 * Reserve the memory area used to pass the certificate lists
 */
static void __init reserve_certificate_list(void)
{
	if (ipl_cert_list_addr)
		memblock_reserve(ipl_cert_list_addr, ipl_cert_list_size);
}

static void __init reserve_physmem_info(void)
{
	unsigned long addr, size;

	if (get_physmem_reserved(RR_MEM_DETECT_EXTENDED, &addr, &size))
		memblock_reserve(addr, size);
}

static void __init free_physmem_info(void)
{
	unsigned long addr, size;

	if (get_physmem_reserved(RR_MEM_DETECT_EXTENDED, &addr, &size))
		memblock_phys_free(addr, size);
}

static void __init memblock_add_physmem_info(void)
{
	unsigned long start, end;
	int i;

	pr_debug("physmem info source: %s (%hhd)\n",
		 get_physmem_info_source(), physmem_info.info_source);
	/* keep memblock lists close to the kernel */
	memblock_set_bottom_up(true);
	for_each_physmem_usable_range(i, &start, &end)
		memblock_add(start, end - start);
	for_each_physmem_online_range(i, &start, &end)
		memblock_physmem_add(start, end - start);
	memblock_set_bottom_up(false);
	memblock_set_node(0, ULONG_MAX, &memblock.memory, 0);
}

/*
 * Reserve memory used for lowcore/command line/kernel image.
 */
static void __init reserve_kernel(void)
{
	memblock_reserve(0, STARTUP_NORMAL_OFFSET);
	memblock_reserve(OLDMEM_BASE, sizeof(unsigned long));
	memblock_reserve(OLDMEM_SIZE, sizeof(unsigned long));
	memblock_reserve(physmem_info.reserved[RR_AMODE31].start, __eamode31 - __samode31);
	memblock_reserve(__pa(sclp_early_sccb), EXT_SCCB_READ_SCP);
	memblock_reserve(__pa(_stext), _end - _stext);
}

static void __init setup_memory(void)
{
	phys_addr_t start, end;
	u64 i;

	/*
	 * Init storage key for present memory
	 */
	for_each_mem_range(i, &start, &end)
		storage_key_init_range(start, end);

	psw_set_key(PAGE_DEFAULT_KEY);
}

static void __init relocate_amode31_section(void)
{
	unsigned long amode31_size = __eamode31 - __samode31;
	long amode31_offset = physmem_info.reserved[RR_AMODE31].start - __samode31;
	long *ptr;

	pr_info("Relocating AMODE31 section of size 0x%08lx\n", amode31_size);

	/* Move original AMODE31 section to the new one */
	memmove((void *)physmem_info.reserved[RR_AMODE31].start, (void *)__samode31, amode31_size);
	/* Zero out the old AMODE31 section to catch invalid accesses within it */
	memset((void *)__samode31, 0, amode31_size);

	/* Update all AMODE31 region references */
	for (ptr = _start_amode31_refs; ptr != _end_amode31_refs; ptr++)
		*ptr += amode31_offset;
}

/* This must be called after AMODE31 relocation */
static void __init setup_cr(void)
{
	union ctlreg2 cr2;
	union ctlreg5 cr5;
	union ctlreg15 cr15;

	__ctl_duct[1] = (unsigned long)__ctl_aste;
	__ctl_duct[2] = (unsigned long)__ctl_aste;
	__ctl_duct[4] = (unsigned long)__ctl_duald;

	/* Update control registers CR2, CR5 and CR15 */
	__ctl_store(cr2.val, 2, 2);
	__ctl_store(cr5.val, 5, 5);
	__ctl_store(cr15.val, 15, 15);
	cr2.ducto = (unsigned long)__ctl_duct >> 6;
	cr5.pasteo = (unsigned long)__ctl_duct >> 6;
	cr15.lsea = (unsigned long)__ctl_linkage_stack >> 3;
	__ctl_load(cr2.val, 2, 2);
	__ctl_load(cr5.val, 5, 5);
	__ctl_load(cr15.val, 15, 15);
}

/*
 * Add system information as device randomness
 */
static void __init setup_randomness(void)
{
	struct sysinfo_3_2_2 *vmms;

	vmms = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!vmms)
		panic("Failed to allocate memory for sysinfo structure\n");
	if (stsi(vmms, 3, 2, 2) == 0 && vmms->count)
		add_device_randomness(&vmms->vm, sizeof(vmms->vm[0]) * vmms->count);
	memblock_free(vmms, PAGE_SIZE);

	if (cpacf_query_func(CPACF_PRNO, CPACF_PRNO_TRNG))
		static_branch_enable(&s390_arch_random_available);
}

/*
 * Find the correct size for the task_struct. This depends on
 * the size of the struct fpu at the end of the thread_struct
 * which is embedded in the task_struct.
 */
static void __init setup_task_size(void)
{
	int task_size = sizeof(struct task_struct);

	if (!MACHINE_HAS_VX) {
		task_size -= sizeof(__vector128) * __NUM_VXRS;
		task_size += sizeof(freg_t) * __NUM_FPRS;
	}
	arch_task_struct_size = task_size;
}

/*
 * Issue diagnose 318 to set the control program name and
 * version codes.
 */
static void __init setup_control_program_code(void)
{
	union diag318_info diag318_info = {
		.cpnc = CPNC_LINUX,
		.cpvc = 0,
	};

	if (!sclp.has_diag318)
		return;

	diag_stat_inc(DIAG_STAT_X318);
	asm volatile("diag %0,0,0x318\n" : : "d" (diag318_info.val));
}

/*
 * Print the component list from the IPL report
 */
static void __init log_component_list(void)
{
	struct ipl_rb_component_entry *ptr, *end;
	char *str;

	if (!early_ipl_comp_list_addr)
		return;
	if (ipl_block.hdr.flags & IPL_PL_FLAG_SIPL)
		pr_info("Linux is running with Secure-IPL enabled\n");
	else
		pr_info("Linux is running with Secure-IPL disabled\n");
	ptr = __va(early_ipl_comp_list_addr);
	end = (void *) ptr + early_ipl_comp_list_size;
	pr_info("The IPL report contains the following components:\n");
	while (ptr < end) {
		if (ptr->flags & IPL_RB_COMPONENT_FLAG_SIGNED) {
			if (ptr->flags & IPL_RB_COMPONENT_FLAG_VERIFIED)
				str = "signed, verified";
			else
				str = "signed, verification failed";
		} else {
			str = "not signed";
		}
		pr_info("%016llx - %016llx (%s)\n",
			ptr->addr, ptr->addr + ptr->len, str);
		ptr++;
	}
}

/*
 * Setup function called from init/main.c just after the banner
 * was printed.
 */

void __init setup_arch(char **cmdline_p)
{
        /*
         * print what head.S has found out about the machine
         */
	if (MACHINE_IS_VM)
		pr_info("Linux is running as a z/VM "
			"guest operating system in 64-bit mode\n");
	else if (MACHINE_IS_KVM)
		pr_info("Linux is running under KVM in 64-bit mode\n");
	else if (MACHINE_IS_LPAR)
		pr_info("Linux is running natively in 64-bit mode\n");
	else
		pr_info("Linux is running as a guest in 64-bit mode\n");

	log_component_list();

	/* Have one command line that is parsed and saved in /proc/cmdline */
	/* boot_command_line has been already set up in early.c */
	*cmdline_p = boot_command_line;

        ROOT_DEV = Root_RAM0;

	setup_initial_init_mm(_text, _etext, _edata, _end);

	if (IS_ENABLED(CONFIG_EXPOLINE_AUTO))
		nospec_auto_detect();

	jump_label_init();
	parse_early_param();
#ifdef CONFIG_CRASH_DUMP
	/* Deactivate elfcorehdr= kernel parameter */
	elfcorehdr_addr = ELFCORE_ADDR_MAX;
#endif

	os_info_init();
	setup_ipl();
	setup_task_size();
	setup_control_program_code();

	/* Do some memory reservations *before* memory is added to memblock */
	reserve_pgtables();
	reserve_kernel();
	reserve_initrd();
	reserve_certificate_list();
	reserve_physmem_info();
	memblock_set_current_limit(ident_map_size);
	memblock_allow_resize();

	/* Get information about *all* installed memory */
	memblock_add_physmem_info();

	free_physmem_info();
	setup_memory_end();
	memblock_dump_all();
	setup_memory();

	relocate_amode31_section();
	setup_cr();
	setup_uv();
	dma_contiguous_reserve(ident_map_size);
	vmcp_cma_reserve();
	if (MACHINE_HAS_EDAT2)
		hugetlb_cma_reserve(PUD_SHIFT - PAGE_SHIFT);

	reserve_crashkernel();
#ifdef CONFIG_CRASH_DUMP
	/*
	 * Be aware that smp_save_dump_secondary_cpus() triggers a system reset.
	 * Therefore CPU and device initialization should be done afterwards.
	 */
	smp_save_dump_secondary_cpus();
#endif

	setup_resources();
	setup_lowcore();
	smp_fill_possible_mask();
	cpu_detect_mhz_feature();
        cpu_init();
	numa_setup();
	smp_detect_cpus();
	topology_init_early();

	if (test_facility(193))
		static_branch_enable(&cpu_has_bear);

	/*
	 * Create kernel page tables.
	 */
        paging_init();

	/*
	 * After paging_init created the kernel page table, the new PSWs
	 * in lowcore can now run with DAT enabled.
	 */
#ifdef CONFIG_CRASH_DUMP
	smp_save_dump_ipl_cpu();
#endif

        /* Setup default console */
	conmode_default();
	set_preferred_console();

	apply_alternative_instructions();
	if (IS_ENABLED(CONFIG_EXPOLINE))
		nospec_init_branches();

	/* Setup zfcp/nvme dump support */
	setup_zfcpdump();

	/* Add system specific data to the random pool */
	setup_randomness();
}
