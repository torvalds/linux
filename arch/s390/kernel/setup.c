/*
 *  arch/s390/kernel/setup.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "arch/i386/kernel/setup.c"
 *    Copyright (C) 1995, Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/kernel_stat.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pfn.h>
#include <linux/ctype.h>
#include <linux/reboot.h>

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
unsigned int console_devno = -1;
unsigned int console_irq = -1;
unsigned long machine_flags = 0;
unsigned long elf_hwcap = 0;
char elf_platform[ELF_PLATFORM_SIZE];

struct mem_chunk __initdata memory_chunk[MEMORY_CHUNKS];
volatile int __cpu_logical_map[NR_CPUS]; /* logical cpu to cpu address */
static unsigned long __initdata memory_end;

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
 * cpu_init() initializes state that is per-CPU.
 */
void __cpuinit cpu_init(void)
{
        int addr = hard_smp_processor_id();

        /*
         * Store processor id in lowcore (used e.g. in timer_interrupt)
         */
	get_cpu_id(&S390_lowcore.cpu_data.cpu_id);
        S390_lowcore.cpu_data.cpu_addr = addr;

        /*
         * Force FPU initialization:
         */
        clear_thread_flag(TIF_USEDFPU);
        clear_used_math();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
        if (current->mm)
                BUG();
        enter_lazy_tlb(&init_mm, current);
}

/*
 * VM halt and poweroff setup routines
 */
char vmhalt_cmd[128] = "";
char vmpoff_cmd[128] = "";
static char vmpanic_cmd[128] = "";

static void strncpy_skip_quote(char *dst, char *src, int n)
{
        int sx, dx;

        dx = 0;
        for (sx = 0; src[sx] != 0; sx++) {
                if (src[sx] == '"') continue;
                dst[dx++] = src[sx];
                if (dx >= n) break;
        }
}

static int __init vmhalt_setup(char *str)
{
        strncpy_skip_quote(vmhalt_cmd, str, 127);
        vmhalt_cmd[127] = 0;
        return 1;
}

__setup("vmhalt=", vmhalt_setup);

static int __init vmpoff_setup(char *str)
{
        strncpy_skip_quote(vmpoff_cmd, str, 127);
        vmpoff_cmd[127] = 0;
        return 1;
}

__setup("vmpoff=", vmpoff_setup);

static int vmpanic_notify(struct notifier_block *self, unsigned long event,
			  void *data)
{
	if (MACHINE_IS_VM && strlen(vmpanic_cmd) > 0)
		cpcmd(vmpanic_cmd, NULL, 0, NULL);

	return NOTIFY_OK;
}

#define PANIC_PRI_VMPANIC	0

static struct notifier_block vmpanic_nb = {
	.notifier_call = vmpanic_notify,
	.priority = PANIC_PRI_VMPANIC
};

static int __init vmpanic_setup(char *str)
{
	static int register_done __initdata = 0;

	strncpy_skip_quote(vmpanic_cmd, str, 127);
	vmpanic_cmd[127] = 0;
	if (!register_done) {
		register_done = 1;
		atomic_notifier_chain_register(&panic_notifier_list,
					       &vmpanic_nb);
	}
	return 1;
}

__setup("vmpanic=", vmpanic_setup);

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

static int __init conmode_setup(char *str)
{
#if defined(CONFIG_SCLP_CONSOLE)
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
#if defined(CONFIG_SCLP_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
			return;
		}
		if (strncmp(ptr + 8, "3270", 4) == 0) {
#if defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_SCLP_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		} else if (strncmp(ptr + 8, "3215", 4) == 0) {
#if defined(CONFIG_TN3215_CONSOLE)
			SET_CONSOLE_3215;
#elif defined(CONFIG_TN3270_CONSOLE)
			SET_CONSOLE_3270;
#elif defined(CONFIG_SCLP_CONSOLE)
			SET_CONSOLE_SCLP;
#endif
		}
        } else if (MACHINE_IS_P390) {
#if defined(CONFIG_TN3215_CONSOLE)
		SET_CONSOLE_3215;
#elif defined(CONFIG_TN3270_CONSOLE)
		SET_CONSOLE_3270;
#endif
	} else {
#if defined(CONFIG_SCLP_CONSOLE)
		SET_CONSOLE_SCLP;
#endif
	}
}

#if defined(CONFIG_ZFCPDUMP) || defined(CONFIG_ZFCPDUMP_MODULE)
static void __init setup_zfcpdump(unsigned int console_devno)
{
	static char str[64];

	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return;
	if (console_devno != -1)
		sprintf(str, "cio_ignore=all,!0.0.%04x,!0.0.%04x",
			ipl_info.data.fcp.dev_id.devno, console_devno);
	else
		sprintf(str, "cio_ignore=all,!0.0.%04x",
			ipl_info.data.fcp.dev_id.devno);
	strcat(COMMAND_LINE, " ");
	strcat(COMMAND_LINE, str);
	console_loglevel = 2;
}
#else
static inline void setup_zfcpdump(unsigned int console_devno) {}
#endif /* CONFIG_ZFCPDUMP */

#ifdef CONFIG_SMP
void (*_machine_restart)(char *command) = machine_restart_smp;
void (*_machine_halt)(void) = machine_halt_smp;
void (*_machine_power_off)(void) = machine_power_off_smp;
#else
/*
 * Reboot, halt and power_off routines for non SMP.
 */
static void do_machine_restart_nonsmp(char * __unused)
{
	do_reipl();
}

static void do_machine_halt_nonsmp(void)
{
        if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0)
		__cpcmd(vmhalt_cmd, NULL, 0, NULL);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

static void do_machine_power_off_nonsmp(void)
{
        if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
		__cpcmd(vmpoff_cmd, NULL, 0, NULL);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

void (*_machine_restart)(char *command) = do_machine_restart_nonsmp;
void (*_machine_halt)(void) = do_machine_halt_nonsmp;
void (*_machine_power_off)(void) = do_machine_power_off_nonsmp;
#endif

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
	return 0;
}
early_param("mem", early_parse_mem);

/*
 * "ipldelay=XXX[sm]" sets ipl delay in seconds or minutes
 */
static int __init early_parse_ipldelay(char *p)
{
	unsigned long delay = 0;

	delay = simple_strtoul(p, &p, 0);

	switch (*p) {
	case 's':
	case 'S':
		delay *= 1000000;
		break;
	case 'm':
	case 'M':
		delay *= 60 * 1000000;
	}

	/* now wait for the requested amount of time */
	udelay(delay);

	return 0;
}
early_param("ipldelay", early_parse_ipldelay);

#ifdef CONFIG_S390_SWITCH_AMODE
unsigned int switch_amode = 0;
EXPORT_SYMBOL_GPL(switch_amode);

static void set_amode_and_uaccess(unsigned long user_amode,
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
		printk("mvcos available.\n");
		memcpy(&uaccess, &uaccess_mvcos_switch, sizeof(uaccess));
	} else {
		printk("mvcos not available.\n");
		memcpy(&uaccess, &uaccess_pt, sizeof(uaccess));
	}
}

/*
 * Switch kernel/user addressing modes?
 */
static int __init early_parse_switch_amode(char *p)
{
	switch_amode = 1;
	return 0;
}
early_param("switch_amode", early_parse_switch_amode);

#else /* CONFIG_S390_SWITCH_AMODE */
static inline void set_amode_and_uaccess(unsigned long user_amode,
					 unsigned long user32_amode)
{
}
#endif /* CONFIG_S390_SWITCH_AMODE */

#ifdef CONFIG_S390_EXEC_PROTECT
unsigned int s390_noexec = 0;
EXPORT_SYMBOL_GPL(s390_noexec);

/*
 * Enable execute protection?
 */
static int __init early_parse_noexec(char *p)
{
	if (!strncmp(p, "off", 3))
		return 0;
	switch_amode = 1;
	s390_noexec = 1;
	return 0;
}
early_param("noexec", early_parse_noexec);
#endif /* CONFIG_S390_EXEC_PROTECT */

static void setup_addressing_mode(void)
{
	if (s390_noexec) {
		printk("S390 execute protection active, ");
		set_amode_and_uaccess(PSW_ASC_SECONDARY, PSW32_ASC_SECONDARY);
	} else if (switch_amode) {
		printk("S390 address spaces switched, ");
		set_amode_and_uaccess(PSW_ASC_PRIMARY, PSW32_ASC_PRIMARY);
	}
#ifdef CONFIG_TRACE_IRQFLAGS
	sysc_restore_trace_psw.mask = psw_kernel_bits & ~PSW_MASK_MCHECK;
	io_restore_trace_psw.mask = psw_kernel_bits & ~PSW_MASK_MCHECK;
#endif
}

static void __init
setup_lowcore(void)
{
	struct _lowcore *lc;
	int lc_pages;

	/*
	 * Setup lowcore for boot cpu
	 */
	lc_pages = sizeof(void *) == 8 ? 2 : 1;
	lc = (struct _lowcore *)
		__alloc_bootmem(lc_pages * PAGE_SIZE, lc_pages * PAGE_SIZE, 0);
	memset(lc, 0, lc_pages * PAGE_SIZE);
	lc->restart_psw.mask = PSW_BASE_BITS | PSW_DEFAULT_KEY;
	lc->restart_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) restart_int_handler;
	if (switch_amode)
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
	lc->ipl_device = S390_lowcore.ipl_device;
	lc->jiffy_timer = -1LL;
	lc->kernel_stack = ((unsigned long) &init_thread_union) + THREAD_SIZE;
	lc->async_stack = (unsigned long)
		__alloc_bootmem(ASYNC_SIZE, ASYNC_SIZE, 0) + ASYNC_SIZE;
	lc->panic_stack = (unsigned long)
		__alloc_bootmem(PAGE_SIZE, PAGE_SIZE, 0) + PAGE_SIZE;
	lc->current_task = (unsigned long) init_thread_union.thread_info.task;
	lc->thread_info = (unsigned long) &init_thread_union;
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE) {
		lc->extended_save_area_addr = (__u32)
			__alloc_bootmem(PAGE_SIZE, PAGE_SIZE, 0);
		/* enable extended save area */
		__ctl_set_bit(14, 29);
	}
#endif
	set_prefix((u32)(unsigned long) lc);
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

	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
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
	unsigned long max_mem, max_phys;
	int i;

#if defined(CONFIG_ZFCPDUMP) || defined(CONFIG_ZFCPDUMP_MODULE)
	if (ipl_info.type == IPL_TYPE_FCP_DUMP)
		memory_end = ZFCPDUMP_HSA_SIZE;
#endif
	memory_size = 0;
	max_phys = VMALLOC_END_INIT - VMALLOC_MIN_SIZE;
	memory_end &= PAGE_MASK;

	max_mem = memory_end ? min(max_phys, memory_end) : max_phys;

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
				printk("initrd extends beyond end of memory "
				       "(0x%08lx > 0x%08lx)\n"
				       "disabling initrd\n",
				       start + INITRD_SIZE, memory_end);
				INITRD_START = INITRD_SIZE = 0;
			} else {
				printk("Moving initrd (0x%08lx -> 0x%08lx, "
				       "size: %ld)\n",
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
		end_chunk = start_chunk + PFN_DOWN(memory_chunk[i].size) - 1;
		end_chunk = min(end_chunk, end_pfn);
		if (start_chunk >= end_chunk)
			continue;
		add_active_range(0, start_chunk, end_chunk);
		pfn = max(start_chunk, start_pfn);
		for (; pfn <= end_chunk; pfn++)
			page_set_storage_key(PFN_PHYS(pfn), PAGE_DEFAULT_KEY);
	}

	psw_set_key(PAGE_DEFAULT_KEY);

	free_bootmem_with_active_regions(0, max_pfn);

	/*
	 * Reserve memory used for lowcore/command line/kernel image.
	 */
	reserve_bootmem(0, (unsigned long)_ehead);
	reserve_bootmem((unsigned long)_stext,
			PFN_PHYS(start_pfn) - (unsigned long)_stext);
	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(start_pfn << PAGE_SHIFT, bootmap_size);

#ifdef CONFIG_BLK_DEV_INITRD
	if (INITRD_START && INITRD_SIZE) {
		if (INITRD_START + INITRD_SIZE <= memory_end) {
			reserve_bootmem(INITRD_START, INITRD_SIZE);
			initrd_start = INITRD_START;
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_start + INITRD_SIZE, memory_end);
			initrd_start = initrd_end = 0;
		}
	}
#endif
}

static __init unsigned int stfl(void)
{
	asm volatile(
		"	.insn	s,0xb2b10000,0(0)\n" /* stfl */
		"0:\n"
		EX_TABLE(0b,0b));
	return S390_lowcore.stfl_fac_list;
}

static __init int stfle(unsigned long long *list, int doublewords)
{
	typedef struct { unsigned long long _[doublewords]; } addrtype;
	register unsigned long __nr asm("0") = doublewords - 1;

	asm volatile(".insn s,0xb2b00000,%0" /* stfle */
		     : "=m" (*(addrtype *) list), "+d" (__nr) : : "cc");
	return __nr + 1;
}

/*
 * Setup hardware capabilities.
 */
static void __init setup_hwcaps(void)
{
	static const int stfl_bits[6] = { 0, 2, 7, 17, 19, 21 };
	struct cpuinfo_S390 *cpuinfo = &S390_lowcore.cpu_data;
	unsigned long long facility_list_extended;
	unsigned int facility_list;
	int i;

	facility_list = stfl();
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
	 * These get translated to:
	 *   HWCAP_S390_ESAN3 bit 0, HWCAP_S390_ZARCH bit 1,
	 *   HWCAP_S390_STFLE bit 2, HWCAP_S390_MSA bit 3,
	 *   HWCAP_S390_LDISP bit 4, and HWCAP_S390_EIMM bit 5.
	 */
	for (i = 0; i < 6; i++)
		if (facility_list & (1UL << (31 - stfl_bits[i])))
			elf_hwcap |= 1UL << i;

	/*
	 * Check for additional facilities with store-facility-list-extended.
	 * stfle stores doublewords (8 byte) with bit 1ULL<<63 as bit 0
	 * and 1ULL<<0 as bit 63. Bits 0-31 contain the same information
	 * as stored by stfl, bits 32-xxx contain additional facilities.
	 * How many facility words are stored depends on the number of
	 * doublewords passed to the instruction. The additional facilites
	 * are:
	 *   Bit 43: decimal floating point facility is installed
	 * translated to:
	 *   HWCAP_S390_DFP bit 6.
	 */
	if ((elf_hwcap & (1UL << 2)) &&
	    stfle(&facility_list_extended, 1) > 0) {
		if (facility_list_extended & (1ULL << (64 - 43)))
			elf_hwcap |= 1UL << 6;
	}

	switch (cpuinfo->cpu_id.machine) {
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
		strcpy(elf_platform, "z9-109");
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
	printk((MACHINE_IS_VM) ?
	       "We are running under VM (31 bit mode)\n" :
	       "We are running native (31 bit mode)\n");
	printk((MACHINE_HAS_IEEE) ?
	       "This machine has an IEEE fpu\n" :
	       "This machine has no IEEE fpu\n");
#else /* CONFIG_64BIT */
	printk((MACHINE_IS_VM) ?
	       "We are running under VM (64 bit mode)\n" :
	       "We are running native (64 bit mode)\n");
#endif /* CONFIG_64BIT */

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(boot_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);

	*cmdline_p = COMMAND_LINE;
	*(*cmdline_p + COMMAND_LINE_SIZE - 1) = '\0';

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

	setup_ipl_info();
	setup_memory_end();
	setup_addressing_mode();
	setup_memory();
	setup_resources();
	setup_lowcore();

        cpu_init();
        __cpu_logical_map[0] = S390_lowcore.cpu_data.cpu_addr;
	smp_setup_cpu_possible_map();

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

	/* Setup zfcpdump support */
	setup_zfcpdump(console_devno);
}

void __cpuinit print_cpu_info(struct cpuinfo_S390 *cpuinfo)
{
   printk("cpu %d "
#ifdef CONFIG_SMP
           "phys_idx=%d "
#endif
           "vers=%02X ident=%06X machine=%04X unused=%04X\n",
           cpuinfo->cpu_nr,
#ifdef CONFIG_SMP
           cpuinfo->cpu_addr,
#endif
           cpuinfo->cpu_id.version,
           cpuinfo->cpu_id.ident,
           cpuinfo->cpu_id.machine,
           cpuinfo->cpu_id.unused);
}

/*
 * show_cpuinfo - Get information on one CPU for use by procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
	static const char *hwcap_str[7] = {
		"esan3", "zarch", "stfle", "msa", "ldisp", "eimm", "dfp"
	};
        struct cpuinfo_S390 *cpuinfo;
	unsigned long n = (unsigned long) v - 1;
	int i;

	s390_adjust_jiffies();
	preempt_disable();
	if (!n) {
		seq_printf(m, "vendor_id       : IBM/S390\n"
			       "# processors    : %i\n"
			       "bogomips per cpu: %lu.%02lu\n",
			       num_online_cpus(), loops_per_jiffy/(500000/HZ),
			       (loops_per_jiffy/(5000/HZ))%100);
		seq_puts(m, "features\t: ");
		for (i = 0; i < 7; i++)
			if (hwcap_str[i] && (elf_hwcap & (1UL << i)))
				seq_printf(m, "%s ", hwcap_str[i]);
		seq_puts(m, "\n");
	}

	if (cpu_online(n)) {
#ifdef CONFIG_SMP
		if (smp_processor_id() == n)
			cpuinfo = &S390_lowcore.cpu_data;
		else
			cpuinfo = &lowcore_ptr[n]->cpu_data;
#else
		cpuinfo = &S390_lowcore.cpu_data;
#endif
		seq_printf(m, "processor %li: "
			       "version = %02X,  "
			       "identification = %06X,  "
			       "machine = %04X\n",
			       n, cpuinfo->cpu_id.version,
			       cpuinfo->cpu_id.ident,
			       cpuinfo->cpu_id.machine);
	}
	preempt_enable();
        return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? (void *)((unsigned long) *pos + 1) : NULL;
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
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

