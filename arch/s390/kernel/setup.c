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
#include <linux/ftrace.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/memory.h>
#include <linux/compat.h>

#include <asm/ipl.h>
#include <asm/uaccess.h>
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
#include "entry.h"

long psw_kernel_bits	= PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_ASC_PRIMARY |
			  PSW_MASK_EA | PSW_MASK_BA;
long psw_user_bits	= PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT |
			  PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_MASK_MCHECK |
			  PSW_MASK_PSTATE | PSW_ASC_HOME;

/*
 * User copy operations.
 */
struct uaccess_ops uaccess;
EXPORT_SYMBOL(uaccess);

/*
 * Machine setup..
 */
unsigned int console_mode = 0;
EXPORT_SYMBOL(console_mode);

unsigned int console_devno = -1;
EXPORT_SYMBOL(console_devno);

unsigned int console_irq = -1;
EXPORT_SYMBOL(console_irq);

unsigned long elf_hwcap = 0;
char elf_platform[ELF_PLATFORM_SIZE];

struct mem_chunk __initdata memory_chunk[MEMORY_CHUNKS];

int __initdata memory_end_set;
unsigned long __initdata memory_end;

unsigned long VMALLOC_START;
EXPORT_SYMBOL(VMALLOC_START);

unsigned long VMALLOC_END;
EXPORT_SYMBOL(VMALLOC_END);

struct page *vmemmap;
EXPORT_SYMBOL(vmemmap);

#ifdef CONFIG_64BIT
unsigned long MODULES_VADDR;
unsigned long MODULES_END;
#endif

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
		if (sclp_has_vt220())
			add_preferred_console("ttyS", 1, NULL);
		else if (sclp_has_linemode())
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

#ifdef CONFIG_ZFCPDUMP
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
#endif /* CONFIG_ZFCPDUMP */

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

unsigned int s390_user_mode = PRIMARY_SPACE_MODE;
EXPORT_SYMBOL_GPL(s390_user_mode);

static void __init set_user_mode_primary(void)
{
	psw_kernel_bits = (psw_kernel_bits & ~PSW_MASK_ASC) | PSW_ASC_HOME;
	psw_user_bits = (psw_user_bits & ~PSW_MASK_ASC) | PSW_ASC_PRIMARY;
#ifdef CONFIG_COMPAT
	psw32_user_bits =
		(psw32_user_bits & ~PSW32_MASK_ASC) | PSW32_ASC_PRIMARY;
#endif
	uaccess = MACHINE_HAS_MVCOS ? uaccess_mvcos_switch : uaccess_pt;
}

static int __init early_parse_user_mode(char *p)
{
	if (p && strcmp(p, "primary") == 0)
		s390_user_mode = PRIMARY_SPACE_MODE;
	else if (!p || strcmp(p, "home") == 0)
		s390_user_mode = HOME_SPACE_MODE;
	else
		return 1;
	return 0;
}
early_param("user_mode", early_parse_user_mode);

static void __init setup_addressing_mode(void)
{
	if (s390_user_mode != PRIMARY_SPACE_MODE)
		return;
	set_user_mode_primary();
	if (MACHINE_HAS_MVCOS)
		pr_info("Address spaces switched, mvcos available\n");
	else
		pr_info("Address spaces switched, mvcos not available\n");
}

void *restart_stack __attribute__((__section__(".data")));

static void __init setup_lowcore(void)
{
	struct _lowcore *lc;

	/*
	 * Setup lowcore for boot cpu
	 */
	BUILD_BUG_ON(sizeof(struct _lowcore) != LC_PAGES * 4096);
	lc = __alloc_bootmem_low(LC_PAGES * PAGE_SIZE, LC_PAGES * PAGE_SIZE, 0);
	lc->restart_psw.mask = psw_kernel_bits;
	lc->restart_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) restart_int_handler;
	lc->external_new_psw.mask = psw_kernel_bits |
		PSW_MASK_DAT | PSW_MASK_MCHECK;
	lc->external_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) ext_int_handler;
	lc->svc_new_psw.mask = psw_kernel_bits |
		PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
	lc->svc_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) system_call;
	lc->program_new_psw.mask = psw_kernel_bits |
		PSW_MASK_DAT | PSW_MASK_MCHECK;
	lc->program_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) pgm_check_handler;
	lc->mcck_new_psw.mask = psw_kernel_bits;
	lc->mcck_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) mcck_int_handler;
	lc->io_new_psw.mask = psw_kernel_bits |
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
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE) {
		lc->extended_save_area_addr = (__u32)
			__alloc_bootmem_low(PAGE_SIZE, PAGE_SIZE, 0);
		/* enable extended save area */
		__ctl_set_bit(14, 29);
	}
#else
	lc->vdso_per_cpu_data = (unsigned long) &lc->paste[0];
#endif
	lc->sync_enter_timer = S390_lowcore.sync_enter_timer;
	lc->async_enter_timer = S390_lowcore.async_enter_timer;
	lc->exit_timer = S390_lowcore.exit_timer;
	lc->user_timer = S390_lowcore.user_timer;
	lc->system_timer = S390_lowcore.system_timer;
	lc->steal_timer = S390_lowcore.steal_timer;
	lc->last_update_timer = S390_lowcore.last_update_timer;
	lc->last_update_clock = S390_lowcore.last_update_clock;
	lc->ftrace_func = S390_lowcore.ftrace_func;

	restart_stack = __alloc_bootmem(ASYNC_SIZE, ASYNC_SIZE, 0);
	restart_stack += ASYNC_SIZE;

	/*
	 * Set up PSW restart to call ipl.c:do_restart(). Copy the relevant
	 * restart data to the absolute zero lowcore. This is necesary if
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
	int i, j;

	code_resource.start = (unsigned long) &_text;
	code_resource.end = (unsigned long) &_etext - 1;
	data_resource.start = (unsigned long) &_etext;
	data_resource.end = (unsigned long) &_edata - 1;
	bss_resource.start = (unsigned long) &__bss_start;
	bss_resource.end = (unsigned long) &__bss_stop - 1;

	for (i = 0; i < MEMORY_CHUNKS; i++) {
		if (!memory_chunk[i].size)
			continue;
		res = alloc_bootmem_low(sizeof(*res));
		res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;
		switch (memory_chunk[i].type) {
		case CHUNK_READ_WRITE:
			res->name = "System RAM";
			break;
		case CHUNK_READ_ONLY:
			res->name = "System ROM";
			res->flags |= IORESOURCE_READONLY;
			break;
		default:
			res->name = "reserved";
		}
		res->start = memory_chunk[i].addr;
		res->end = res->start + memory_chunk[i].size - 1;
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
	unsigned long real_memory_size = 0;
	int i;


#ifdef CONFIG_ZFCPDUMP
	if (ipl_info.type == IPL_TYPE_FCP_DUMP && !OLDMEM_BASE) {
		memory_end = ZFCPDUMP_HSA_SIZE;
		memory_end_set = 1;
	}
#endif
	memory_end &= PAGE_MASK;

	/*
	 * Make sure all chunks are MAX_ORDER aligned so we don't need the
	 * extra checks that HOLES_IN_ZONE would require.
	 */
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		unsigned long start, end;
		struct mem_chunk *chunk;
		unsigned long align;

		chunk = &memory_chunk[i];
		if (!chunk->size)
			continue;
		align = 1UL << (MAX_ORDER + PAGE_SHIFT - 1);
		start = (chunk->addr + align - 1) & ~(align - 1);
		end = (chunk->addr + chunk->size) & ~(align - 1);
		if (start >= end)
			memset(chunk, 0, sizeof(*chunk));
		else {
			chunk->addr = start;
			chunk->size = end - start;
		}
		real_memory_size = max(real_memory_size,
				       chunk->addr + chunk->size);
	}

	/* Choose kernel address space layout: 2, 3, or 4 levels. */
#ifdef CONFIG_64BIT
	vmalloc_size = VMALLOC_END ?: (128UL << 30) - MODULES_LEN;
	tmp = (memory_end ?: real_memory_size) / PAGE_SIZE;
	tmp = tmp * (sizeof(struct page) + PAGE_SIZE) + vmalloc_size;
	if (tmp <= (1UL << 42))
		vmax = 1UL << 42;	/* 3-level kernel page table */
	else
		vmax = 1UL << 53;	/* 4-level kernel page table */
	/* module area is at the end of the kernel address space. */
	MODULES_END = vmax;
	MODULES_VADDR = MODULES_END - MODULES_LEN;
	VMALLOC_END = MODULES_VADDR;
#else
	vmalloc_size = VMALLOC_END ?: 96UL << 20;
	vmax = 1UL << 31;		/* 2-level kernel page table */
	/* vmalloc area is at the end of the kernel address space. */
	VMALLOC_END = vmax;
#endif
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
	memory_end = min(memory_end ?: real_memory_size, tmp);

	/* Fixup memory chunk array to fit into 0..memory_end */
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		struct mem_chunk *chunk = &memory_chunk[i];

		if (!chunk->size)
			continue;
		if (chunk->addr >= memory_end) {
			memset(chunk, 0, sizeof(*chunk));
			continue;
		}
		if (chunk->addr + chunk->size > memory_end)
			chunk->size = memory_end - chunk->addr;
	}
}

static void __init setup_vmcoreinfo(void)
{
	mem_assign_absolute(S390_lowcore.vmcore_info, paddr_vmcoreinfo_note());
}

#ifdef CONFIG_CRASH_DUMP

/*
 * Find suitable location for crashkernel memory
 */
static unsigned long __init find_crash_base(unsigned long crash_size,
					    char **msg)
{
	unsigned long crash_base;
	struct mem_chunk *chunk;
	int i;

	if (memory_chunk[0].size < crash_size) {
		*msg = "first memory chunk must be at least crashkernel size";
		return 0;
	}
	if (OLDMEM_BASE && crash_size == OLDMEM_SIZE)
		return OLDMEM_BASE;

	for (i = MEMORY_CHUNKS - 1; i >= 0; i--) {
		chunk = &memory_chunk[i];
		if (chunk->size == 0)
			continue;
		if (chunk->type != CHUNK_READ_WRITE)
			continue;
		if (chunk->size < crash_size)
			continue;
		crash_base = (chunk->addr + chunk->size) - crash_size;
		if (crash_base < crash_size)
			continue;
		if (crash_base < ZFCPDUMP_HSA_SIZE_MAX)
			continue;
		if (crash_base < (unsigned long) INITRD_START + INITRD_SIZE)
			continue;
		return crash_base;
	}
	*msg = "no suitable area found";
	return 0;
}

/*
 * Check if crash_base and crash_size is valid
 */
static int __init verify_crash_base(unsigned long crash_base,
				    unsigned long crash_size,
				    char **msg)
{
	struct mem_chunk *chunk;
	int i;

	/*
	 * Because we do the swap to zero, we must have at least 'crash_size'
	 * bytes free space before crash_base
	 */
	if (crash_size > crash_base) {
		*msg = "crashkernel offset must be greater than size";
		return -EINVAL;
	}

	/* First memory chunk must be at least crash_size */
	if (memory_chunk[0].size < crash_size) {
		*msg = "first memory chunk must be at least crashkernel size";
		return -EINVAL;
	}
	/* Check if we fit into the respective memory chunk */
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		chunk = &memory_chunk[i];
		if (chunk->size == 0)
			continue;
		if (crash_base < chunk->addr)
			continue;
		if (crash_base >= chunk->addr + chunk->size)
			continue;
		/* we have found the memory chunk */
		if (crash_base + crash_size > chunk->addr + chunk->size) {
			*msg = "selected memory chunk is too small for "
				"crashkernel memory";
			return -EINVAL;
		}
		return 0;
	}
	*msg = "invalid memory range specified";
	return -EINVAL;
}

/*
 * When kdump is enabled, we have to ensure that no memory from
 * the area [0 - crashkernel memory size] and
 * [crashk_res.start - crashk_res.end] is set offline.
 */
static int kdump_mem_notifier(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct memory_notify *arg = data;

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
 * Make sure that oldmem, where the dump is stored, is protected
 */
static void reserve_oldmem(void)
{
#ifdef CONFIG_CRASH_DUMP
	unsigned long real_size = 0;
	int i;

	if (!OLDMEM_BASE)
		return;
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		struct mem_chunk *chunk = &memory_chunk[i];

		real_size = max(real_size, chunk->addr + chunk->size);
	}
	create_mem_hole(memory_chunk, OLDMEM_BASE, OLDMEM_SIZE);
	create_mem_hole(memory_chunk, OLDMEM_SIZE, real_size - OLDMEM_SIZE);
	if (OLDMEM_BASE + OLDMEM_SIZE == real_size)
		saved_max_pfn = PFN_DOWN(OLDMEM_BASE) - 1;
	else
		saved_max_pfn = PFN_DOWN(real_size) - 1;
#endif
}

/*
 * Reserve memory for kdump kernel to be loaded with kexec
 */
static void __init reserve_crashkernel(void)
{
#ifdef CONFIG_CRASH_DUMP
	unsigned long long crash_base, crash_size;
	char *msg = NULL;
	int rc;

	rc = parse_crashkernel(boot_command_line, memory_end, &crash_size,
			       &crash_base);
	if (rc || crash_size == 0)
		return;
	crash_base = ALIGN(crash_base, KEXEC_CRASH_MEM_ALIGN);
	crash_size = ALIGN(crash_size, KEXEC_CRASH_MEM_ALIGN);
	if (register_memory_notifier(&kdump_mem_nb))
		return;
	if (!crash_base)
		crash_base = find_crash_base(crash_size, &msg);
	if (!crash_base) {
		pr_info("crashkernel reservation failed: %s\n", msg);
		unregister_memory_notifier(&kdump_mem_nb);
		return;
	}
	if (verify_crash_base(crash_base, crash_size, &msg)) {
		pr_info("crashkernel reservation failed: %s\n", msg);
		unregister_memory_notifier(&kdump_mem_nb);
		return;
	}
	if (!OLDMEM_BASE && MACHINE_IS_VM)
		diag10_range(PFN_DOWN(crash_base), PFN_DOWN(crash_size));
	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
	create_mem_hole(memory_chunk, crash_base, crash_size);
	pr_info("Reserving %lluMB of memory at %lluMB "
		"for crashkernel (System RAM: %luMB)\n",
		crash_size >> 20, crash_base >> 20, memory_end >> 20);
	os_info_crashkernel_add(crash_base, crash_size);
#endif
}

static void __init setup_memory(void)
{
        unsigned long bootmap_size;
	unsigned long start_pfn, end_pfn;
	int i;

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(&_end));
	end_pfn = max_pfn = PFN_DOWN(memory_end);

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * Move the initrd in case the bitmap of the bootmem allocater
	 * would overwrite it.
	 */

	if (INITRD_START && INITRD_SIZE) {
		unsigned long bmap_size;
		unsigned long start;

		bmap_size = bootmem_bootmap_pages(end_pfn - start_pfn + 1);
		bmap_size = PFN_PHYS(bmap_size);

		if (PFN_PHYS(start_pfn) + bmap_size > INITRD_START) {
			start = PFN_PHYS(start_pfn) + bmap_size + PAGE_SIZE;

#ifdef CONFIG_CRASH_DUMP
			if (OLDMEM_BASE) {
				/* Move initrd behind kdump oldmem */
				if (start + INITRD_SIZE > OLDMEM_BASE &&
				    start < OLDMEM_BASE + OLDMEM_SIZE)
					start = OLDMEM_BASE + OLDMEM_SIZE;
			}
#endif
			if (start + INITRD_SIZE > memory_end) {
				pr_err("initrd extends beyond end of "
				       "memory (0x%08lx > 0x%08lx) "
				       "disabling initrd\n",
				       start + INITRD_SIZE, memory_end);
				INITRD_START = INITRD_SIZE = 0;
			} else {
				pr_info("Moving initrd (0x%08lx -> "
					"0x%08lx, size: %ld)\n",
					INITRD_START, start, INITRD_SIZE);
				memmove((void *) start, (void *) INITRD_START,
					INITRD_SIZE);
				INITRD_START = start;
			}
		}
	}
#endif

	/*
	 * Initialize the boot-time allocator
	 */
	bootmap_size = init_bootmem(start_pfn, end_pfn);

	/*
	 * Register RAM areas with the bootmem allocator.
	 */

	for (i = 0; i < MEMORY_CHUNKS; i++) {
		unsigned long start_chunk, end_chunk, pfn;

		if (!memory_chunk[i].size)
			continue;
		start_chunk = PFN_DOWN(memory_chunk[i].addr);
		end_chunk = start_chunk + PFN_DOWN(memory_chunk[i].size);
		end_chunk = min(end_chunk, end_pfn);
		if (start_chunk >= end_chunk)
			continue;
		memblock_add_node(PFN_PHYS(start_chunk),
				  PFN_PHYS(end_chunk - start_chunk), 0);
		pfn = max(start_chunk, start_pfn);
		storage_key_init_range(PFN_PHYS(pfn), PFN_PHYS(end_chunk));
	}

	psw_set_key(PAGE_DEFAULT_KEY);

	free_bootmem_with_active_regions(0, max_pfn);

	/*
	 * Reserve memory used for lowcore/command line/kernel image.
	 */
	reserve_bootmem(0, (unsigned long)_ehead, BOOTMEM_DEFAULT);
	reserve_bootmem((unsigned long)_stext,
			PFN_PHYS(start_pfn) - (unsigned long)_stext,
			BOOTMEM_DEFAULT);
	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(start_pfn << PAGE_SHIFT, bootmap_size,
			BOOTMEM_DEFAULT);

#ifdef CONFIG_CRASH_DUMP
	if (crashk_res.start)
		reserve_bootmem(crashk_res.start,
				crashk_res.end - crashk_res.start + 1,
				BOOTMEM_DEFAULT);
	if (is_kdump_kernel())
		reserve_bootmem(elfcorehdr_addr - OLDMEM_BASE,
				PAGE_ALIGN(elfcorehdr_size), BOOTMEM_DEFAULT);
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (INITRD_START && INITRD_SIZE) {
		if (INITRD_START + INITRD_SIZE <= memory_end) {
			reserve_bootmem(INITRD_START, INITRD_SIZE,
					BOOTMEM_DEFAULT);
			initrd_start = INITRD_START;
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			pr_err("initrd extends beyond end of "
			       "memory (0x%08lx > 0x%08lx) "
			       "disabling initrd\n",
			       initrd_start + INITRD_SIZE, memory_end);
			initrd_start = initrd_end = 0;
		}
	}
#endif
}

/*
 * Setup hardware capabilities.
 */
static void __init setup_hwcaps(void)
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

#if defined(CONFIG_64BIT)
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
#endif

	get_cpu_id(&cpu_id);
	switch (cpu_id.machine) {
	case 0x9672:
#if !defined(CONFIG_64BIT)
	default:	/* Use "g5" as default for 31 bit kernels. */
#endif
		strcpy(elf_platform, "g5");
		break;
	case 0x2064:
	case 0x2066:
#if defined(CONFIG_64BIT)
	default:	/* Use "z900" as default for 64 bit kernels. */
#endif
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
		strcpy(elf_platform, "zEC12");
		break;
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
#ifndef CONFIG_64BIT
	if (MACHINE_IS_VM)
		pr_info("Linux is running as a z/VM "
			"guest operating system in 31-bit mode\n");
	else if (MACHINE_IS_LPAR)
		pr_info("Linux is running natively in 31-bit mode\n");
	if (MACHINE_HAS_IEEE)
		pr_info("The hardware system has IEEE compatible "
			"floating point units\n");
	else
		pr_info("The hardware system has no IEEE compatible "
			"floating point units\n");
#else /* CONFIG_64BIT */
	if (MACHINE_IS_VM)
		pr_info("Linux is running as a z/VM "
			"guest operating system in 64-bit mode\n");
	else if (MACHINE_IS_KVM)
		pr_info("Linux is running under KVM in 64-bit mode\n");
	else if (MACHINE_IS_LPAR)
		pr_info("Linux is running natively in 64-bit mode\n");
#endif /* CONFIG_64BIT */

	/* Have one command line that is parsed and saved in /proc/cmdline */
	/* boot_command_line has been already set up in early.c */
	*cmdline_p = boot_command_line;

        ROOT_DEV = Root_RAM0;

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	if (MACHINE_HAS_MVCOS)
		memcpy(&uaccess, &uaccess_mvcos, sizeof(uaccess));
	else
		memcpy(&uaccess, &uaccess_std, sizeof(uaccess));

	parse_early_param();
	detect_memory_layout(memory_chunk, memory_end);
	os_info_init();
	setup_ipl();
	reserve_oldmem();
	setup_memory_end();
	setup_addressing_mode();
	reserve_crashkernel();
	setup_memory();
	setup_resources();
	setup_vmcoreinfo();
	setup_lowcore();

        cpu_init();
	s390_init_cpu_topology();

	/*
	 * Setup capabilities (ELF_HWCAP & ELF_PLATFORM).
	 */
	setup_hwcaps();

	/*
	 * Create kernel page tables and switch to virtual addressing.
	 */
        paging_init();

        /* Setup default console */
	conmode_default();
	set_preferred_console();

	/* Setup zfcpdump support */
	setup_zfcpdump();
}
