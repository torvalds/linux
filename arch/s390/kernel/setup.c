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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/kernel_stat.h>
#include <linux/device.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/mmu_context.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/ptrace.h>

/*
 * Machine setup..
 */
unsigned int console_mode = 0;
unsigned int console_devno = -1;
unsigned int console_irq = -1;
unsigned long memory_size = 0;
unsigned long machine_flags = 0;
struct {
	unsigned long addr, size, type;
} memory_chunk[MEMORY_CHUNKS] = { { 0 } };
#define CHUNK_READ_WRITE 0
#define CHUNK_READ_ONLY 1
volatile int __cpu_logical_map[NR_CPUS]; /* logical cpu to cpu address */
unsigned long __initdata zholes_size[MAX_NR_ZONES];
static unsigned long __initdata memory_end;

/*
 * Setup options
 */
extern int _text,_etext, _edata, _end;

/*
 * This is set up by the setup-routine at boot-time
 * for S390 need to find out, what we have to setup
 * using address 0x10400 ...
 */

#include <asm/setup.h>

static char command_line[COMMAND_LINE_SIZE] = { 0, };

static struct resource code_resource = {
	.name  = "Kernel code",
	.start = (unsigned long) &_text,
	.end = (unsigned long) &_etext - 1,
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.start = (unsigned long) &_etext,
	.end = (unsigned long) &_edata - 1,
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

/*
 * cpu_init() initializes state that is per-CPU.
 */
void __devinit cpu_init (void)
{
        int addr = hard_smp_processor_id();

        /*
         * Store processor id in lowcore (used e.g. in timer_interrupt)
         */
        asm volatile ("stidp %0": "=m" (S390_lowcore.cpu_data.cpu_id));
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

static inline void strncpy_skip_quote(char *dst, char *src, int n)
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
		__cpcmd("QUERY CONSOLE", query_buffer, 1024, NULL);
		console_devno = simple_strtoul(query_buffer + 5, NULL, 16);
		ptr = strstr(query_buffer, "SUBCHANNEL =");
		console_irq = simple_strtoul(ptr + 13, NULL, 16);
		__cpcmd("QUERY TERM", query_buffer, 1024, NULL);
		ptr = strstr(query_buffer, "CONMODE");
		/*
		 * Set the conmode to 3215 so that the device recognition 
		 * will set the cu_type of the console to 3215. If the
		 * conmode is 3270 and we don't set it back then both
		 * 3215 and the 3270 driver will try to access the console
		 * device (3215 as console and 3270 as normal tty).
		 */
		__cpcmd("TERM CONMODE 3215", NULL, 0, NULL);
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

#ifdef CONFIG_SMP
extern void machine_restart_smp(char *);
extern void machine_halt_smp(void);
extern void machine_power_off_smp(void);

void (*_machine_restart)(char *command) = machine_restart_smp;
void (*_machine_halt)(void) = machine_halt_smp;
void (*_machine_power_off)(void) = machine_power_off_smp;
#else
/*
 * Reboot, halt and power_off routines for non SMP.
 */
extern void reipl(unsigned long devno);
extern void reipl_diag(void);
static void do_machine_restart_nonsmp(char * __unused)
{
	reipl_diag();

	if (MACHINE_IS_VM)
		cpcmd ("IPL", NULL, 0, NULL);
	else
		reipl (0x10000 | S390_lowcore.ipl_device);
}

static void do_machine_halt_nonsmp(void)
{
        if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0)
                cpcmd(vmhalt_cmd, NULL, 0, NULL);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

static void do_machine_power_off_nonsmp(void)
{
        if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
                cpcmd(vmpoff_cmd, NULL, 0, NULL);
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
	console_unblank();
	_machine_restart(command);
}

void machine_halt(void)
{
	console_unblank();
	_machine_halt();
}

void machine_power_off(void)
{
	console_unblank();
	_machine_power_off();
}

/*
 * Dummy power off function.
 */
void (*pm_power_off)(void) = machine_power_off;

static void __init
add_memory_hole(unsigned long start, unsigned long end)
{
	unsigned long dma_pfn = MAX_DMA_ADDRESS >> PAGE_SHIFT;

	if (end <= dma_pfn)
		zholes_size[ZONE_DMA] += end - start + 1;
	else if (start > dma_pfn)
		zholes_size[ZONE_NORMAL] += end - start + 1;
	else {
		zholes_size[ZONE_DMA] += dma_pfn - start + 1;
		zholes_size[ZONE_NORMAL] += end - dma_pfn;
	}
}

static void __init
parse_cmdline_early(char **cmdline_p)
{
	char c = ' ', cn, *to = command_line, *from = COMMAND_LINE;
	unsigned long delay = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" sets memsize
		 */
		if (c == ' ' && strncmp(from, "mem=", 4) == 0) {
			memory_end = simple_strtoul(from+4, &from, 0);
			if ( *from == 'K' || *from == 'k' ) {
				memory_end = memory_end << 10;
				from++;
			} else if ( *from == 'M' || *from == 'm' ) {
				memory_end = memory_end << 20;
				from++;
			}
		}
		/*
		 * "ipldelay=XXX[sm]" sets ipl delay in seconds or minutes
		 */
		if (c == ' ' && strncmp(from, "ipldelay=", 9) == 0) {
			delay = simple_strtoul(from+9, &from, 0);
			if (*from == 's' || *from == 'S') {
				delay = delay*1000000;
				from++;
			} else if (*from == 'm' || *from == 'M') {
				delay = delay*60*1000000;
				from++;
			}
			/* now wait for the requested amount of time */
			udelay(delay);
		}
		cn = *(from++);
		if (!cn)
			break;
		if (cn == '\n')
			cn = ' ';  /* replace newlines with space */
		if (cn == 0x0d)
			cn = ' ';  /* replace 0x0d with space */
		if (cn == ' ' && c == ' ')
			continue;  /* remove additional spaces */
		c = cn;
		if (to - command_line >= COMMAND_LINE_SIZE)
			break;
		*(to++) = c;
	}
	if (c == ' ' && to > command_line) to--;
	*to = '\0';
	*cmdline_p = command_line;
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
	lc->external_new_psw.mask = PSW_KERNEL_BITS;
	lc->external_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) ext_int_handler;
	lc->svc_new_psw.mask = PSW_KERNEL_BITS | PSW_MASK_IO | PSW_MASK_EXT;
	lc->svc_new_psw.addr = PSW_ADDR_AMODE | (unsigned long) system_call;
	lc->program_new_psw.mask = PSW_KERNEL_BITS;
	lc->program_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long)pgm_check_handler;
	lc->mcck_new_psw.mask =
		PSW_KERNEL_BITS & ~PSW_MASK_MCHECK & ~PSW_MASK_DAT;
	lc->mcck_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) mcck_int_handler;
	lc->io_new_psw.mask = PSW_KERNEL_BITS;
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
		ctl_set_bit(14, 29);
	}
#endif
	set_prefix((u32)(unsigned long) lc);
}

static void __init
setup_resources(void)
{
	struct resource *res;
	int i;

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
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
	}
}

static void __init
setup_memory(void)
{
        unsigned long bootmap_size;
	unsigned long start_pfn, end_pfn, init_pfn;
	unsigned long last_rw_end;
	int i;

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = (__pa(&_end) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	end_pfn = max_pfn = memory_end >> PAGE_SHIFT;

	/* Initialize storage key for kernel pages */
	for (init_pfn = 0 ; init_pfn < start_pfn; init_pfn++)
		page_set_storage_key(init_pfn << PAGE_SHIFT, PAGE_DEFAULT_KEY);

	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem(start_pfn, end_pfn);

	/*
	 * Register RAM areas with the bootmem allocator.
	 */
	last_rw_end = start_pfn;

	for (i = 0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		unsigned long start_chunk, end_chunk;

		if (memory_chunk[i].type != CHUNK_READ_WRITE)
			continue;
		start_chunk = (memory_chunk[i].addr + PAGE_SIZE - 1);
		start_chunk >>= PAGE_SHIFT;
		end_chunk = (memory_chunk[i].addr + memory_chunk[i].size);
		end_chunk >>= PAGE_SHIFT;
		if (start_chunk < start_pfn)
			start_chunk = start_pfn;
		if (end_chunk > end_pfn)
			end_chunk = end_pfn;
		if (start_chunk < end_chunk) {
			/* Initialize storage key for RAM pages */
			for (init_pfn = start_chunk ; init_pfn < end_chunk;
			     init_pfn++)
				page_set_storage_key(init_pfn << PAGE_SHIFT,
						     PAGE_DEFAULT_KEY);
			free_bootmem(start_chunk << PAGE_SHIFT,
				     (end_chunk - start_chunk) << PAGE_SHIFT);
			if (last_rw_end < start_chunk)
				add_memory_hole(last_rw_end, start_chunk - 1);
			last_rw_end = end_chunk;
		}
	}

	psw_set_key(PAGE_DEFAULT_KEY);

	if (last_rw_end < end_pfn - 1)
		add_memory_hole(last_rw_end, end_pfn - 1);

	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 */
	reserve_bootmem(start_pfn << PAGE_SHIFT, bootmap_size);

#ifdef CONFIG_BLK_DEV_INITRD
	if (INITRD_START) {
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

        ROOT_DEV = Root_RAM0;
#ifndef CONFIG_64BIT
	memory_end = memory_size & ~0x400000UL;  /* align memory end to 4MB */
        /*
         * We need some free virtual space to be able to do vmalloc.
         * On a machine with 2GB memory we make sure that we have at
         * least 128 MB free space for vmalloc.
         */
        if (memory_end > 1920*1024*1024)
                memory_end = 1920*1024*1024;
#else /* CONFIG_64BIT */
	memory_end = memory_size & ~0x200000UL;  /* detected in head.s */
#endif /* CONFIG_64BIT */

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	parse_cmdline_early(cmdline_p);

	setup_memory();
	setup_resources();
	setup_lowcore();

        cpu_init();
        __cpu_logical_map[0] = S390_lowcore.cpu_data.cpu_addr;

	/*
	 * Create kernel page tables and switch to virtual addressing.
	 */
        paging_init();

        /* Setup default console */
	conmode_default();
}

void print_cpu_info(struct cpuinfo_S390 *cpuinfo)
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
        struct cpuinfo_S390 *cpuinfo;
	unsigned long n = (unsigned long) v - 1;

	preempt_disable();
	if (!n) {
		seq_printf(m, "vendor_id       : IBM/S390\n"
			       "# processors    : %i\n"
			       "bogomips per cpu: %lu.%02lu\n",
			       num_online_cpus(), loops_per_jiffy/(500000/HZ),
			       (loops_per_jiffy/(5000/HZ))%100);
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

#define DEFINE_IPL_ATTR(_name, _format, _value)			\
static ssize_t ipl_##_name##_show(struct subsystem *subsys,	\
		char *page)					\
{								\
	return sprintf(page, _format, _value);			\
}								\
static struct subsys_attribute ipl_##_name##_attr =		\
	__ATTR(_name, S_IRUGO, ipl_##_name##_show, NULL);

DEFINE_IPL_ATTR(wwpn, "0x%016llx\n", (unsigned long long)
		IPL_PARMBLOCK_START->fcp.wwpn);
DEFINE_IPL_ATTR(lun, "0x%016llx\n", (unsigned long long)
		IPL_PARMBLOCK_START->fcp.lun);
DEFINE_IPL_ATTR(bootprog, "%lld\n", (unsigned long long)
		IPL_PARMBLOCK_START->fcp.bootprog);
DEFINE_IPL_ATTR(br_lba, "%lld\n", (unsigned long long)
		IPL_PARMBLOCK_START->fcp.br_lba);

enum ipl_type_type {
	ipl_type_unknown,
	ipl_type_ccw,
	ipl_type_fcp,
};

static enum ipl_type_type
get_ipl_type(void)
{
	struct ipl_parameter_block *ipl = IPL_PARMBLOCK_START;

	if (!IPL_DEVNO_VALID)
		return ipl_type_unknown;
	if (!IPL_PARMBLOCK_VALID)
		return ipl_type_ccw;
	if (ipl->hdr.header.version > IPL_MAX_SUPPORTED_VERSION)
		return ipl_type_unknown;
	if (ipl->fcp.pbt != IPL_TYPE_FCP)
		return ipl_type_unknown;
	return ipl_type_fcp;
}

static ssize_t
ipl_type_show(struct subsystem *subsys, char *page)
{
	switch (get_ipl_type()) {
	case ipl_type_ccw:
		return sprintf(page, "ccw\n");
	case ipl_type_fcp:
		return sprintf(page, "fcp\n");
	default:
		return sprintf(page, "unknown\n");
	}
}

static struct subsys_attribute ipl_type_attr = __ATTR_RO(ipl_type);

static ssize_t
ipl_device_show(struct subsystem *subsys, char *page)
{
	struct ipl_parameter_block *ipl = IPL_PARMBLOCK_START;

	switch (get_ipl_type()) {
	case ipl_type_ccw:
		return sprintf(page, "0.0.%04x\n", ipl_devno);
	case ipl_type_fcp:
		return sprintf(page, "0.0.%04x\n", ipl->fcp.devno);
	default:
		return 0;
	}
}

static struct subsys_attribute ipl_device_attr =
	__ATTR(device, S_IRUGO, ipl_device_show, NULL);

static struct attribute *ipl_fcp_attrs[] = {
	&ipl_type_attr.attr,
	&ipl_device_attr.attr,
	&ipl_wwpn_attr.attr,
	&ipl_lun_attr.attr,
	&ipl_bootprog_attr.attr,
	&ipl_br_lba_attr.attr,
	NULL,
};

static struct attribute_group ipl_fcp_attr_group = {
	.attrs = ipl_fcp_attrs,
};

static struct attribute *ipl_ccw_attrs[] = {
	&ipl_type_attr.attr,
	&ipl_device_attr.attr,
	NULL,
};

static struct attribute_group ipl_ccw_attr_group = {
	.attrs = ipl_ccw_attrs,
};

static struct attribute *ipl_unknown_attrs[] = {
	&ipl_type_attr.attr,
	NULL,
};

static struct attribute_group ipl_unknown_attr_group = {
	.attrs = ipl_unknown_attrs,
};

static ssize_t
ipl_parameter_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned int size = IPL_PARMBLOCK_SIZE;

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;

	memcpy(buf, (void *) IPL_PARMBLOCK_START + off, count);
	return count;
}

static struct bin_attribute ipl_parameter_attr = {
	.attr = {
		.name = "binary_parameter",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = PAGE_SIZE,
	.read = &ipl_parameter_read,
};

static ssize_t
ipl_scp_data_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	unsigned int size =  IPL_PARMBLOCK_START->fcp.scp_data_len;
	void *scp_data = &IPL_PARMBLOCK_START->fcp.scp_data;

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;

	memcpy(buf, scp_data + off, count);
	return count;
}

static struct bin_attribute ipl_scp_data_attr = {
	.attr = {
		.name = "scp_data",
		.mode = S_IRUGO,
		.owner = THIS_MODULE,
	},
	.size = PAGE_SIZE,
	.read = &ipl_scp_data_read,
};

static decl_subsys(ipl, NULL, NULL);

static int __init
ipl_device_sysfs_register(void) {
	int rc;

	rc = firmware_register(&ipl_subsys);
	if (rc)
		return rc;

	switch (get_ipl_type()) {
	case ipl_type_ccw:
		sysfs_create_group(&ipl_subsys.kset.kobj, &ipl_ccw_attr_group);
		break;
	case ipl_type_fcp:
		sysfs_create_group(&ipl_subsys.kset.kobj, &ipl_fcp_attr_group);
		sysfs_create_bin_file(&ipl_subsys.kset.kobj,
				      &ipl_parameter_attr);
		sysfs_create_bin_file(&ipl_subsys.kset.kobj,
				      &ipl_scp_data_attr);
		break;
	default:
		sysfs_create_group(&ipl_subsys.kset.kobj,
				   &ipl_unknown_attr_group);
		break;
	}
	return 0;
}

__initcall(ipl_device_sysfs_register);
