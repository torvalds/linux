/*
 *  arch/s390/kernel/setup.c
 *
 *  S390 version
 *    Copyright (C) IBM Corp. 1999,2010
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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
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

#include <asm/ipl.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/mmu_context.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/ebcdic.h>
#include <asm/compat.h>
#include <asm/kvm_virtio.h>

long psw_kernel_bits	= (PSW_BASE_BITS | PSW_MASK_DAT | PSW_ASC_PRIMARY |
			   PSW_MASK_MCHECK | PSW_DEFAULT_KEY);
long psw_user_bits	= (PSW_BASE_BITS | PSW_MASK_DAT | PSW_ASC_HOME |
			   PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK |
			   PSW_MASK_PSTATE | PSW_DEFAULT_KEY);

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

/* An array with a pointer to the lowcore of every CPU. */
struct _lowcore *lowcore_ptr[NR_CPUS];
EXPORT_SYMBOL(lowcore_ptr);

/*
 * This is set up by the setup-routine at boot-time
 * for S390 need to find out, what we have to setup
 * using address 0x10400 ...
 */

#include <asm/setup.h>

static struct resource code_resource = {
	.name  = "Kernel code",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

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
	if (MACHINE_IS_KVM)
		add_preferred_console("hvc", 0, NULL);
	else if (CONSOLE_IS_3215 || CONSOLE_IS_SCLP)
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
static void __init setup_zfcpdump(unsigned int console_devno)
{
	static char str[41];

	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return;
	if (console_devno != -1)
		sprintf(str, " cio_ignore=all,!0.0.%04x,!0.0.%04x",
			ipl_info.data.fcp.dev_id.devno, console_devno);
	else
		sprintf(str, " cio_ignore=all,!0.0.%04x",
			ipl_info.data.fcp.dev_id.devno);
	strcat(boot_command_line, str);
	console_loglevel = 2;
}
#else
static inline void setup_zfcpdump(unsigned int console_devno) {}
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

static int __init early_parse_mem(char *p)
{
	memory_end = memparse(p, &p);
	memory_end_set = 1;
	return 0;
}
early_param("mem", early_parse_mem);

unsigned int user_mode = HOME_SPACE_MODE;
EXPORT_SYMBOL_GPL(user_mode);

static int set_amode_and_uaccess(unsigned long user_amode,
				 unsigned long user32_amode)
{
	psw_user_bits = PSW_BASE_BITS | PSW_MASK_DAT | user_amode |
			PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK |
			PSW_MASK_PSTATE | PSW_DEFAULT_KEY;
#ifdef CONFIG_COMPAT
	psw_user32_bits = PSW_BASE32_BITS | PSW_MASK_DAT | user_amode |
			  PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK |
			  PSW_MASK_PSTATE | PSW_DEFAULT_KEY;
	psw32_user_bits = PSW32_BASE_BITS | PSW32_MASK_DAT | user32_amode |
			  PSW32_MASK_IO | PSW32_MASK_EXT | PSW32_MASK_MCHECK |
			  PSW32_MASK_PSTATE;
#endif
	psw_kernel_bits = PSW_BASE_BITS | PSW_MASK_DAT | PSW_ASC_HOME |
			  PSW_MASK_MCHECK | PSW_DEFAULT_KEY;

	if (MACHINE_HAS_MVCOS) {
		memcpy(&uaccess, &uaccess_mvcos_switch, sizeof(uaccess));
		return 1;
	} else {
		memcpy(&uaccess, &uaccess_pt, sizeof(uaccess));
		return 0;
	}
}

/*
 * Switch kernel/user addressing modes?
 */
static int __init early_parse_switch_amode(char *p)
{
	if (user_mode != SECONDARY_SPACE_MODE)
		user_mode = PRIMARY_SPACE_MODE;
	return 0;
}
early_param("switch_amode", early_parse_switch_amode);

static int __init early_parse_user_mode(char *p)
{
	if (p && strcmp(p, "primary") == 0)
		user_mode = PRIMARY_SPACE_MODE;
#ifdef CONFIG_S390_EXEC_PROTECT
	else if (p && strcmp(p, "secondary") == 0)
		user_mode = SECONDARY_SPACE_MODE;
#endif
	else if (!p || strcmp(p, "home") == 0)
		user_mode = HOME_SPACE_MODE;
	else
		return 1;
	return 0;
}
early_param("user_mode", early_parse_user_mode);

#ifdef CONFIG_S390_EXEC_PROTECT
/*
 * Enable execute protection?
 */
static int __init early_parse_noexec(char *p)
{
	if (!strncmp(p, "off", 3))
		return 0;
	user_mode = SECONDARY_SPACE_MODE;
	return 0;
}
early_param("noexec", early_parse_noexec);
#endif /* CONFIG_S390_EXEC_PROTECT */

static void setup_addressing_mode(void)
{
	if (user_mode == SECONDARY_SPACE_MODE) {
		if (set_amode_and_uaccess(PSW_ASC_SECONDARY,
					  PSW32_ASC_SECONDARY))
			pr_info("Execute protection active, "
				"mvcos available\n");
		else
			pr_info("Execute protection active, "
				"mvcos not available\n");
	} else if (user_mode == PRIMARY_SPACE_MODE) {
		if (set_amode_and_uaccess(PSW_ASC_PRIMARY, PSW32_ASC_PRIMARY))
			pr_info("Address spaces switched, "
				"mvcos available\n");
		else
			pr_info("Address spaces switched, "
				"mvcos not available\n");
	}
}

static void __init
setup_lowcore(void)
{
	struct _lowcore *lc;

	/*
	 * Setup lowcore for boot cpu
	 */
	BUILD_BUG_ON(sizeof(struct _lowcore) != LC_PAGES * 4096);
	lc = __alloc_bootmem_low(LC_PAGES * PAGE_SIZE, LC_PAGES * PAGE_SIZE, 0);
	lc->restart_psw.mask = PSW_BASE_BITS | PSW_DEFAULT_KEY;
	lc->restart_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) restart_int_handler;
	if (user_mode != HOME_SPACE_MODE)
		lc->restart_psw.mask |= PSW_ASC_HOME;
	lc->external_new_psw.mask = psw_kernel_bits;
	lc->external_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) ext_int_handler;
	lc->svc_new_psw.mask = psw_kernel_bits | PSW_MASK_IO | PSW_MASK_EXT;
	lc->svc_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) system_call;
	lc->program_new_psw.mask = psw_kernel_bits;
	lc->program_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long)pgm_check_handler;
	lc->mcck_new_psw.mask =
		psw_kernel_bits & ~PSW_MASK_MCHECK & ~PSW_MASK_DAT;
	lc->mcck_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) mcck_int_handler;
	lc->io_new_psw.mask = psw_kernel_bits;
	lc->io_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) io_int_handler;
	lc->clock_comparator = -1ULL;
	lc->kernel_stack = ((unsigned long) &init_thread_union) + THREAD_SIZE;
	lc->async_stack = (unsigned long)
		__alloc_bootmem(ASYNC_SIZE, ASYNC_SIZE, 0) + ASYNC_SIZE;
	lc->panic_stack = (unsigned long)
		__alloc_bootmem(PAGE_SIZE, PAGE_SIZE, 0) + PAGE_SIZE;
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
	lc->cmf_hpp = -1ULL;
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
	set_prefix((u32)(unsigned long) lc);
	lowcore_ptr[0] = lc;
}

static void __init
setup_resources(void)
{
	struct resource *res, *sub_res;
	int i;

	code_resource.start = (unsigned long) &_text;
	code_resource.end = (unsigned long) &_etext - 1;
	data_resource.start = (unsigned long) &_etext;
	data_resource.end = (unsigned long) &_edata - 1;

	for (i = 0; i < MEMORY_CHUNKS; i++) {
		if (!memory_chunk[i].size)
			continue;
		res = alloc_bootmem_low(sizeof(struct resource));
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
		res->end = memory_chunk[i].addr +  memory_chunk[i].size - 1;
		request_resource(&iomem_resource, res);

		if (code_resource.start >= res->start  &&
			code_resource.start <= res->end &&
			code_resource.end > res->end) {
			sub_res = alloc_bootmem_low(sizeof(struct resource));
			memcpy(sub_res, &code_resource,
				sizeof(struct resource));
			sub_res->end = res->end;
			code_resource.start = res->end + 1;
			request_resource(res, sub_res);
		}

		if (code_resource.start >= res->start &&
			code_resource.start <= res->end &&
			code_resource.end <= res->end)
			request_resource(res, &code_resource);

		if (data_resource.start >= res->start &&
			data_resource.start <= res->end &&
			data_resource.end > res->end) {
			sub_res = alloc_bootmem_low(sizeof(struct resource));
			memcpy(sub_res, &data_resource,
				sizeof(struct resource));
			sub_res->end = res->end;
			data_resource.start = res->end + 1;
			request_resource(res, sub_res);
		}

		if (data_resource.start >= res->start &&
			data_resource.start <= res->end &&
			data_resource.end <= res->end)
			request_resource(res, &data_resource);
	}
}

unsigned long real_memory_size;
EXPORT_SYMBOL_GPL(real_memory_size);

static void __init setup_memory_end(void)
{
	unsigned long memory_size;
	unsigned long max_mem;
	int i;

#ifdef CONFIG_ZFCPDUMP
	if (ipl_info.type == IPL_TYPE_FCP_DUMP) {
		memory_end = ZFCPDUMP_HSA_SIZE;
		memory_end_set = 1;
	}
#endif
	memory_size = 0;
	memory_end &= PAGE_MASK;

	max_mem = memory_end ? min(VMEM_MAX_PHYS, memory_end) : VMEM_MAX_PHYS;
	memory_end = min(max_mem, memory_end);

	/*
	 * Make sure all chunks are MAX_ORDER aligned so we don't need the
	 * extra checks that HOLES_IN_ZONE would require.
	 */
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		unsigned long start, end;
		struct mem_chunk *chunk;
		unsigned long align;

		chunk = &memory_chunk[i];
		align = 1UL << (MAX_ORDER + PAGE_SHIFT - 1);
		start = (chunk->addr + align - 1) & ~(align - 1);
		end = (chunk->addr + chunk->size) & ~(align - 1);
		if (start >= end)
			memset(chunk, 0, sizeof(*chunk));
		else {
			chunk->addr = start;
			chunk->size = end - start;
		}
	}

	for (i = 0; i < MEMORY_CHUNKS; i++) {
		struct mem_chunk *chunk = &memory_chunk[i];

		real_memory_size = max(real_memory_size,
				       chunk->addr + chunk->size);
		if (chunk->addr >= max_mem) {
			memset(chunk, 0, sizeof(*chunk));
			continue;
		}
		if (chunk->addr + chunk->size > max_mem)
			chunk->size = max_mem - chunk->addr;
		memory_size = max(memory_size, chunk->addr + chunk->size);
	}
	if (!memory_end)
		memory_end = memory_size;
}

static void __init
setup_memory(void)
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

	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		unsigned long start_chunk, end_chunk, pfn;

		if (memory_chunk[i].type != CHUNK_READ_WRITE)
			continue;
		start_chunk = PFN_DOWN(memory_chunk[i].addr);
		end_chunk = start_chunk + PFN_DOWN(memory_chunk[i].size);
		end_chunk = min(end_chunk, end_pfn);
		if (start_chunk >= end_chunk)
			continue;
		add_active_range(0, start_chunk, end_chunk);
		pfn = max(start_chunk, start_pfn);
		for (; pfn < end_chunk; pfn++)
			page_set_storage_key(PFN_PHYS(pfn),
					     PAGE_DEFAULT_KEY, 0);
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
	 * doublewords passed to the instruction. The additional facilites
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
		strcpy(elf_platform, "z196");
		break;
	}
}

/*
 * Setup function called from init/main.c just after the banner
 * was printed.
 */

void __init
setup_arch(char **cmdline_p)
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

	setup_ipl();
	setup_memory_end();
	setup_addressing_mode();
	setup_memory();
	setup_resources();
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
	setup_zfcpdump(console_devno);
}
