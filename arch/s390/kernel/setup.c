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
#include <linux/bootmem.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/kernel_stat.h>
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

#include <asm/ipl.h>
#include <asm/facility.h>
#include <asm/smp.h>
#include <asm/mmu_context.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/ebcdic.h>
#include <asm/kvm_virtio.h>
#include <asm/diag.h>
#include <asm/os_info.h>
#include <asm/sclp.h>
#include <asm/sysinfo.h>
#include <asm/numa.h>
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

unsigned long elf_hwcap __read_mostly = 0;
char elf_platform[ELF_PLATFORM_SIZE];

int __initdata memory_end_set;
unsigned long __initdata memory_end;
unsigned long __initdata max_physmem_end;

unsigned long VMALLOC_START;
EXPORT_SYMBOL(VMALLOC_START);

unsigned long VMALLOC_END;
EXPORT_SYMBOL(VMALLOC_END);

struct page *vmemmap;
EXPORT_SYMBOL(vmemmap);

unsigned long MODULES_VADDR;
unsigned long MODULES_END;

/* An array with a pointer to the lowcore of every CPU. */
struct _lowcore *lowcore_ptr[NR_CPUS];
EXPORT_SYMBOL(lowcore_ptr);

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
	if (MACHINE_IS_KVM) {
		if (sclp.has_vt220)
			add_preferred_console("ttyS", 1, NULL);
		else if (sclp.has_linemode)
			add_preferred_console("ttyS", 0, NULL);
		else
			add_preferred_console("hvc", 0, NULL);
	} else if (CONSOLE_IS_3215 || CONSOLE_IS_SCLP)
		add_preferred_console("ttyS", 0, NULL);
	else if (CONSOLE_IS_3270)
		add_preferred_console("tty3270", 0, NULL);
}

static int __init conmode_setup(char *str)
{
#if defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
	if (strncmp(str, "hwc", 4) == 0 || strncmp(str, "sclp", 5) == 0)
                SET_CONSOLE_SCLP;
#endif
#if defined(CONFIG_TN3215_CONSOLE)
	if (strncmp(str, "3215", 5) == 0)
		SET_CONSOLE_3215;
#endif
#if defined(CONFIG_TN3270_CONSOLE)
	if (strncmp(str, "3270", 5) == 0)
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
		if (strncmp(ptr + 8, "3270", 4) == 0) {
#if defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		} else if (strncmp(ptr + 8, "3215", 4) == 0) {
#if defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		}
	} else {
#if defined(CONFIG_SCLP_CONSOLE) || defined(CONFIG_SCLP_VT220_CONSOLE)
		SET_CONSOLE_SCLP;
#endif
	}
}

#ifdef CONFIG_CRASH_DUMP
static void __init setup_zfcpdump(void)
{
	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return;
	if (OLDMEM_BASE)
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

static int __init early_parse_mem(char *p)
{
	memory_end = memparse(p, &p);
	memory_end &= PAGE_MASK;
	memory_end_set = 1;
	return 0;
}
early_param("mem", early_parse_mem);

static int __init parse_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;
	VMALLOC_END = (memparse(arg, &arg) + PAGE_SIZE - 1) & PAGE_MASK;
	return 0;
}
early_param("vmalloc", parse_vmalloc);

void *restart_stack __attribute__((__section__(".data")));

static void __init setup_lowcore(void)
{
	struct _lowcore *lc;

	/*
	 * Setup lowcore for boot cpu
	 */
	BUILD_BUG_ON(sizeof(struct _lowcore) != LC_PAGES * 4096);
	lc = __alloc_bootmem_low(LC_PAGES * PAGE_SIZE, LC_PAGES * PAGE_SIZE, 0);
	lc->restart_psw.mask = PSW_KERNEL_BITS;
	lc->restart_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) restart_int_handler;
	lc->external_new_psw.mask = PSW_KERNEL_BITS |
		PSW_MASK_DAT | PSW_MASK_MCHECK;
	lc->external_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) ext_int_handler;
	lc->svc_new_psw.mask = PSW_KERNEL_BITS |
		PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	lc->svc_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) system_call;
	lc->program_new_psw.mask = PSW_KERNEL_BITS |
		PSW_MASK_DAT | PSW_MASK_MCHECK;
	lc->program_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) pgm_check_handler;
	lc->mcck_new_psw.mask = PSW_KERNEL_BITS;
	lc->mcck_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) mcck_int_handler;
	lc->io_new_psw.mask = PSW_KERNEL_BITS |
		PSW_MASK_DAT | PSW_MASK_MCHECK;
	lc->io_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) io_int_handler;
	lc->clock_comparator = -1ULL;
	lc->kernel_stack = ((unsigned long) &init_thread_union)
		+ THREAD_SIZE - STACK_FRAME_OVERHEAD - sizeof(struct pt_regs);
	lc->async_stack = (unsigned long)
		__alloc_bootmem(ASYNC_SIZE, ASYNC_SIZE, 0)
		+ ASYNC_SIZE - STACK_FRAME_OVERHEAD - sizeof(struct pt_regs);
	lc->panic_stack = (unsigned long)
		__alloc_bootmem(PAGE_SIZE, PAGE_SIZE, 0)
		+ PAGE_SIZE - STACK_FRAME_OVERHEAD - sizeof(struct pt_regs);
	lc->current_task = (unsigned long) init_thread_union.thread_info.task;
	lc->thread_info = (unsigned long) &init_thread_union;
	lc->machine_flags = S390_lowcore.machine_flags;
	lc->stfl_fac_list = S390_lowcore.stfl_fac_list;
	memcpy(lc->stfle_fac_list, S390_lowcore.stfle_fac_list,
	       MAX_FACILITY_BIT/8);
	if (MACHINE_HAS_VX)
		lc->vector_save_area_addr =
			(unsigned long) &lc->vector_save_area;
	lc->vdso_per_cpu_data = (unsigned long) &lc->paste[0];
	lc->sync_enter_timer = S390_lowcore.sync_enter_timer;
	lc->async_enter_timer = S390_lowcore.async_enter_timer;
	lc->exit_timer = S390_lowcore.exit_timer;
	lc->user_timer = S390_lowcore.user_timer;
	lc->system_timer = S390_lowcore.system_timer;
	lc->steal_timer = S390_lowcore.steal_timer;
	lc->last_update_timer = S390_lowcore.last_update_timer;
	lc->last_update_clock = S390_lowcore.last_update_clock;

	restart_stack = __alloc_bootmem(ASYNC_SIZE, ASYNC_SIZE, 0);
	restart_stack += ASYNC_SIZE;

	/*
	 * Set up PSW restart to call ipl.c:do_restart(). Copy the relevant
	 * restart data to the absolute zero lowcore. This is necessary if
	 * PSW restart is done on an offline CPU that has lowcore zero.
	 */
	lc->restart_stack = (unsigned long) restart_stack;
	lc->restart_fn = (unsigned long) do_restart;
	lc->restart_data = 0;
	lc->restart_source = -1UL;

	/* Setup absolute zero lowcore */
	mem_assign_absolute(S390_lowcore.restart_stack, lc->restart_stack);
	mem_assign_absolute(S390_lowcore.restart_fn, lc->restart_fn);
	mem_assign_absolute(S390_lowcore.restart_data, lc->restart_data);
	mem_assign_absolute(S390_lowcore.restart_source, lc->restart_source);
	mem_assign_absolute(S390_lowcore.restart_psw, lc->restart_psw);

#ifdef CONFIG_SMP
	lc->spinlock_lockval = arch_spin_lockval(0);
#endif

	set_prefix((u32)(unsigned long) lc);
	lowcore_ptr[0] = lc;
}

static struct resource code_resource = {
	.name  = "Kernel code",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource bss_resource = {
	.name = "Kernel bss",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource __initdata *standard_resources[] = {
	&code_resource,
	&data_resource,
	&bss_resource,
};

static void __init setup_resources(void)
{
	struct resource *res, *std_res, *sub_res;
	struct memblock_region *reg;
	int j;

	code_resource.start = (unsigned long) &_text;
	code_resource.end = (unsigned long) &_etext - 1;
	data_resource.start = (unsigned long) &_etext;
	data_resource.end = (unsigned long) &_edata - 1;
	bss_resource.start = (unsigned long) &__bss_start;
	bss_resource.end = (unsigned long) &__bss_stop - 1;

	for_each_memblock(memory, reg) {
		res = alloc_bootmem_low(sizeof(*res));
		res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;

		res->name = "System RAM";
		res->start = reg->base;
		res->end = reg->base + reg->size - 1;
		request_resource(&iomem_resource, res);

		for (j = 0; j < ARRAY_SIZE(standard_resources); j++) {
			std_res = standard_resources[j];
			if (std_res->start < res->start ||
			    std_res->start > res->end)
				continue;
			if (std_res->end > res->end) {
				sub_res = alloc_bootmem_low(sizeof(*sub_res));
				*sub_res = *std_res;
				sub_res->end = res->end;
				std_res->start = res->end + 1;
				request_resource(res, sub_res);
			} else {
				request_resource(res, std_res);
			}
		}
	}
}

static void __init setup_memory_end(void)
{
	unsigned long vmax, vmalloc_size, tmp;

	/* Choose kernel address space layout: 2, 3, or 4 levels. */
	vmalloc_size = VMALLOC_END ?: (128UL << 30) - MODULES_LEN;
	tmp = (memory_end ?: max_physmem_end) / PAGE_SIZE;
	tmp = tmp * (sizeof(struct page) + PAGE_SIZE);
	if (tmp + vmalloc_size + MODULES_LEN <= (1UL << 42))
		vmax = 1UL << 42;	/* 3-level kernel page table */
	else
		vmax = 1UL << 53;	/* 4-level kernel page table */
	/* module area is at the end of the kernel address space. */
	MODULES_END = vmax;
	MODULES_VADDR = MODULES_END - MODULES_LEN;
	VMALLOC_END = MODULES_VADDR;
	VMALLOC_START = vmax - vmalloc_size;

	/* Split remaining virtual space between 1:1 mapping & vmemmap array */
	tmp = VMALLOC_START / (PAGE_SIZE + sizeof(struct page));
	/* vmemmap contains a multiple of PAGES_PER_SECTION struct pages */
	tmp = SECTION_ALIGN_UP(tmp);
	tmp = VMALLOC_START - tmp * sizeof(struct page);
	tmp &= ~((vmax >> 11) - 1);	/* align to page table level */
	tmp = min(tmp, 1UL << MAX_PHYSMEM_BITS);
	vmemmap = (struct page *) tmp;

	/* Take care that memory_end is set and <= vmemmap */
	memory_end = min(memory_end ?: max_physmem_end, tmp);
	max_pfn = max_low_pfn = PFN_DOWN(memory_end);
	memblock_remove(memory_end, ULONG_MAX);

	pr_notice("Max memory size: %luMB\n", memory_end >> 20);
}

static void __init setup_vmcoreinfo(void)
{
	mem_assign_absolute(S390_lowcore.vmcore_info, paddr_vmcoreinfo_note());
}

#ifdef CONFIG_CRASH_DUMP

/*
 * When kdump is enabled, we have to ensure that no memory from
 * the area [0 - crashkernel memory size] and
 * [crashk_res.start - crashk_res.end] is set offline.
 */
static int kdump_mem_notifier(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct memory_notify *arg = data;

	if (action != MEM_GOING_OFFLINE)
		return NOTIFY_OK;
	if (arg->start_pfn < PFN_DOWN(resource_size(&crashk_res)))
		return NOTIFY_BAD;
	if (arg->start_pfn > PFN_DOWN(crashk_res.end))
		return NOTIFY_OK;
	if (arg->start_pfn + arg->nr_pages - 1 < PFN_DOWN(crashk_res.start))
		return NOTIFY_OK;
	return NOTIFY_BAD;
}

static struct notifier_block kdump_mem_nb = {
	.notifier_call = kdump_mem_notifier,
};

#endif

/*
 * Make sure that the area behind memory_end is protected
 */
static void reserve_memory_end(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (ipl_info.type == IPL_TYPE_FCP_DUMP &&
	    !OLDMEM_BASE && sclp.hsa_size) {
		memory_end = sclp.hsa_size;
		memory_end &= PAGE_MASK;
		memory_end_set = 1;
	}
#endif
	if (!memory_end_set)
		return;
	memblock_reserve(memory_end, ULONG_MAX);
}

/*
 * Make sure that oldmem, where the dump is stored, is protected
 */
static void reserve_oldmem(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (OLDMEM_BASE)
		/* Forget all memory above the running kdump system */
		memblock_reserve(OLDMEM_SIZE, (phys_addr_t)ULONG_MAX);
#endif
}

/*
 * Make sure that oldmem, where the dump is stored, is protected
 */
static void remove_oldmem(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (OLDMEM_BASE)
		/* Forget all memory above the running kdump system */
		memblock_remove(OLDMEM_SIZE, (phys_addr_t)ULONG_MAX);
#endif
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

	rc = parse_crashkernel(boot_command_line, memory_end, &crash_size,
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

	low = crash_base ?: OLDMEM_BASE;
	high = low + crash_size;
	if (low >= OLDMEM_BASE && high <= OLDMEM_BASE + OLDMEM_SIZE) {
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
		crash_base = memblock_find_in_range(low, high, crash_size,
						    KEXEC_CRASH_MEM_ALIGN);
	}

	if (!crash_base) {
		pr_info("crashkernel reservation failed: %s\n",
			"no suitable area found");
		return;
	}

	if (register_memory_notifier(&kdump_mem_nb))
		return;

	if (!OLDMEM_BASE && MACHINE_IS_VM)
		diag10_range(PFN_DOWN(crash_base), PFN_DOWN(crash_size));
	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
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
#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = INITRD_START;
	initrd_end = initrd_start + INITRD_SIZE;
	memblock_reserve(INITRD_START, INITRD_SIZE);
#endif
}

/*
 * Check for initrd being in usable memory
 */
static void __init check_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (INITRD_START && INITRD_SIZE &&
	    !memblock_is_region_memory(INITRD_START, INITRD_SIZE)) {
		pr_err("initrd does not fit memory.\n");
		memblock_free(INITRD_START, INITRD_SIZE);
		initrd_start = initrd_end = 0;
	}
#endif
}

/*
 * Reserve memory used for lowcore/command line/kernel image.
 */
static void __init reserve_kernel(void)
{
	unsigned long start_pfn = PFN_UP(__pa(&_end));

#ifdef CONFIG_DMA_API_DEBUG
	/*
	 * DMA_API_DEBUG code stumbles over addresses from the
	 * range [_ehead, _stext]. Mark the memory as reserved
	 * so it is not used for CONFIG_DMA_API_DEBUG=y.
	 */
	memblock_reserve(0, PFN_PHYS(start_pfn));
#else
	memblock_reserve(0, (unsigned long)_ehead);
	memblock_reserve((unsigned long)_stext, PFN_PHYS(start_pfn)
			 - (unsigned long)_stext);
#endif
}

static void __init reserve_elfcorehdr(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (is_kdump_kernel())
		memblock_reserve(elfcorehdr_addr - OLDMEM_BASE,
				 PAGE_ALIGN(elfcorehdr_size));
#endif
}

static void __init setup_memory(void)
{
	struct memblock_region *reg;

	/*
	 * Init storage key for present memory
	 */
	for_each_memblock(memory, reg) {
		storage_key_init_range(reg->base, reg->base + reg->size);
	}
	psw_set_key(PAGE_DEFAULT_KEY);

	/* Only cosmetics */
	memblock_enforce_memory_limit(memblock_end_of_DRAM());
}

/*
 * Setup hardware capabilities.
 */
static int __init setup_hwcaps(void)
{
	static const int stfl_bits[6] = { 0, 2, 7, 17, 19, 21 };
	struct cpuid cpu_id;
	int i;

	/*
	 * The store facility list bits numbers as found in the principles
	 * of operation are numbered with bit 1UL<<31 as number 0 to
	 * bit 1UL<<0 as number 31.
	 *   Bit 0: instructions named N3, "backported" to esa-mode
	 *   Bit 2: z/Architecture mode is active
	 *   Bit 7: the store-facility-list-extended facility is installed
	 *   Bit 17: the message-security assist is installed
	 *   Bit 19: the long-displacement facility is installed
	 *   Bit 21: the extended-immediate facility is installed
	 *   Bit 22: extended-translation facility 3 is installed
	 *   Bit 30: extended-translation facility 3 enhancement facility
	 * These get translated to:
	 *   HWCAP_S390_ESAN3 bit 0, HWCAP_S390_ZARCH bit 1,
	 *   HWCAP_S390_STFLE bit 2, HWCAP_S390_MSA bit 3,
	 *   HWCAP_S390_LDISP bit 4, HWCAP_S390_EIMM bit 5 and
	 *   HWCAP_S390_ETF3EH bit 8 (22 && 30).
	 */
	for (i = 0; i < 6; i++)
		if (test_facility(stfl_bits[i]))
			elf_hwcap |= 1UL << i;

	if (test_facility(22) && test_facility(30))
		elf_hwcap |= HWCAP_S390_ETF3EH;

	/*
	 * Check for additional facilities with store-facility-list-extended.
	 * stfle stores doublewords (8 byte) with bit 1ULL<<63 as bit 0
	 * and 1ULL<<0 as bit 63. Bits 0-31 contain the same information
	 * as stored by stfl, bits 32-xxx contain additional facilities.
	 * How many facility words are stored depends on the number of
	 * doublewords passed to the instruction. The additional facilities
	 * are:
	 *   Bit 42: decimal floating point facility is installed
	 *   Bit 44: perform floating point operation facility is installed
	 * translated to:
	 *   HWCAP_S390_DFP bit 6 (42 && 44).
	 */
	if ((elf_hwcap & (1UL << 2)) && test_facility(42) && test_facility(44))
		elf_hwcap |= HWCAP_S390_DFP;

	/*
	 * Huge page support HWCAP_S390_HPAGE is bit 7.
	 */
	if (MACHINE_HAS_HPAGE)
		elf_hwcap |= HWCAP_S390_HPAGE;

	/*
	 * 64-bit register support for 31-bit processes
	 * HWCAP_S390_HIGH_GPRS is bit 9.
	 */
	elf_hwcap |= HWCAP_S390_HIGH_GPRS;

	/*
	 * Transactional execution support HWCAP_S390_TE is bit 10.
	 */
	if (test_facility(50) && test_facility(73))
		elf_hwcap |= HWCAP_S390_TE;

	/*
	 * Vector extension HWCAP_S390_VXRS is bit 11. The Vector extension
	 * can be disabled with the "novx" parameter. Use MACHINE_HAS_VX
	 * instead of facility bit 129.
	 */
	if (MACHINE_HAS_VX)
		elf_hwcap |= HWCAP_S390_VXRS;
	get_cpu_id(&cpu_id);
	add_device_randomness(&cpu_id, sizeof(cpu_id));
	switch (cpu_id.machine) {
	case 0x2064:
	case 0x2066:
	default:	/* Use "z900" as default for 64 bit kernels. */
		strcpy(elf_platform, "z900");
		break;
	case 0x2084:
	case 0x2086:
		strcpy(elf_platform, "z990");
		break;
	case 0x2094:
	case 0x2096:
		strcpy(elf_platform, "z9-109");
		break;
	case 0x2097:
	case 0x2098:
		strcpy(elf_platform, "z10");
		break;
	case 0x2817:
	case 0x2818:
		strcpy(elf_platform, "z196");
		break;
	case 0x2827:
	case 0x2828:
		strcpy(elf_platform, "zEC12");
		break;
	case 0x2964:
		strcpy(elf_platform, "z13");
		break;
	}
	return 0;
}
arch_initcall(setup_hwcaps);

/*
 * Add system information as device randomness
 */
static void __init setup_randomness(void)
{
	struct sysinfo_3_2_2 *vmms;

	vmms = (struct sysinfo_3_2_2 *) alloc_page(GFP_KERNEL);
	if (vmms && stsi(vmms, 3, 2, 2) == 0 && vmms->count)
		add_device_randomness(&vmms, vmms->count);
	free_page((unsigned long) vmms);
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

	/* Have one command line that is parsed and saved in /proc/cmdline */
	/* boot_command_line has been already set up in early.c */
	*cmdline_p = boot_command_line;

        ROOT_DEV = Root_RAM0;

	/* Is init_mm really needed? */
	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	parse_early_param();
	os_info_init();
	setup_ipl();

	/* Do some memory reservations *before* memory is added to memblock */
	reserve_memory_end();
	reserve_oldmem();
	reserve_kernel();
	reserve_initrd();
	reserve_elfcorehdr();
	memblock_allow_resize();

	/* Get information about *all* installed memory */
	detect_memory_memblock();

	remove_oldmem();

	/*
	 * Make sure all chunks are MAX_ORDER aligned so we don't need the
	 * extra checks that HOLES_IN_ZONE would require.
	 *
	 * Is this still required?
	 */
	memblock_trim_memory(1UL << (MAX_ORDER - 1 + PAGE_SHIFT));

	setup_memory_end();
	setup_memory();

	check_initrd();
	reserve_crashkernel();
	/*
	 * Be aware that smp_save_dump_cpus() triggers a system reset.
	 * Therefore CPU and device initialization should be done afterwards.
	 */
	smp_save_dump_cpus();

	setup_resources();
	setup_vmcoreinfo();
	setup_lowcore();
	smp_fill_possible_mask();
        cpu_init();
	numa_setup();

	/*
	 * Create kernel page tables and switch to virtual addressing.
	 */
        paging_init();

        /* Setup default console */
	conmode_default();
	set_preferred_console();

	/* Setup zfcpdump support */
	setup_zfcpdump();

	/* Add system specific data to the random pool */
	setup_randomness();
}
