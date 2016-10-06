/*
 *  linux/arch/m68k/kernel/setup.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

/*
 * This file handles the architecture-dependent parts of system setup
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/initrd.h>

#include <asm/bootinfo.h>
#include <asm/byteorder.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/fpu.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/machdep.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif
#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atari_stram.h>
#endif
#ifdef CONFIG_SUN3X
#include <asm/dvma.h>
#endif
#include <asm/natfeat.h>

#if !FPSTATESIZE || !NR_IRQS
#warning No CPU/platform type selected, your kernel will not work!
#warning Are you building an allnoconfig kernel?
#endif

unsigned long m68k_machtype;
EXPORT_SYMBOL(m68k_machtype);
unsigned long m68k_cputype;
EXPORT_SYMBOL(m68k_cputype);
unsigned long m68k_fputype;
unsigned long m68k_mmutype;
EXPORT_SYMBOL(m68k_mmutype);
#ifdef CONFIG_VME
unsigned long vme_brdtype;
EXPORT_SYMBOL(vme_brdtype);
#endif

int m68k_is040or060;
EXPORT_SYMBOL(m68k_is040or060);

extern unsigned long availmem;

int m68k_num_memory;
EXPORT_SYMBOL(m68k_num_memory);
int m68k_realnum_memory;
EXPORT_SYMBOL(m68k_realnum_memory);
unsigned long m68k_memoffset;
struct m68k_mem_info m68k_memory[NUM_MEMINFO];
EXPORT_SYMBOL(m68k_memory);

static struct m68k_mem_info m68k_ramdisk __initdata;

static char m68k_command_line[CL_SIZE] __initdata;

void (*mach_sched_init) (irq_handler_t handler) __initdata = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata = NULL;
void (*mach_get_model) (char *model);
void (*mach_get_hardware_list) (struct seq_file *m);
/* machine dependent timer functions */
int (*mach_hwclk) (int, struct rtc_time*);
EXPORT_SYMBOL(mach_hwclk);
int (*mach_set_clock_mmss) (unsigned long);
unsigned int (*mach_get_ss)(void);
int (*mach_get_rtc_pll)(struct rtc_pll_info *);
int (*mach_set_rtc_pll)(struct rtc_pll_info *);
EXPORT_SYMBOL(mach_get_ss);
EXPORT_SYMBOL(mach_get_rtc_pll);
EXPORT_SYMBOL(mach_set_rtc_pll);
void (*mach_reset)( void );
void (*mach_halt)( void );
void (*mach_power_off)( void );
long mach_max_dma_address = 0x00ffffff; /* default set to the lower 16MB */
#ifdef CONFIG_HEARTBEAT
void (*mach_heartbeat) (int);
EXPORT_SYMBOL(mach_heartbeat);
#endif
#ifdef CONFIG_M68K_L2_CACHE
void (*mach_l2_flush) (int);
#endif
#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
void (*mach_beep)(unsigned int, unsigned int);
EXPORT_SYMBOL(mach_beep);
#endif
#if defined(CONFIG_ISA) && defined(MULTI_ISA)
int isa_type;
int isa_sex;
EXPORT_SYMBOL(isa_type);
EXPORT_SYMBOL(isa_sex);
#endif

extern int amiga_parse_bootinfo(const struct bi_record *);
extern int atari_parse_bootinfo(const struct bi_record *);
extern int mac_parse_bootinfo(const struct bi_record *);
extern int q40_parse_bootinfo(const struct bi_record *);
extern int bvme6000_parse_bootinfo(const struct bi_record *);
extern int mvme16x_parse_bootinfo(const struct bi_record *);
extern int mvme147_parse_bootinfo(const struct bi_record *);
extern int hp300_parse_bootinfo(const struct bi_record *);
extern int apollo_parse_bootinfo(const struct bi_record *);

extern void config_amiga(void);
extern void config_atari(void);
extern void config_mac(void);
extern void config_sun3(void);
extern void config_apollo(void);
extern void config_mvme147(void);
extern void config_mvme16x(void);
extern void config_bvme6000(void);
extern void config_hp300(void);
extern void config_q40(void);
extern void config_sun3x(void);

#define MASK_256K 0xfffc0000

extern void paging_init(void);

static void __init m68k_parse_bootinfo(const struct bi_record *record)
{
	uint16_t tag;

	save_bootinfo(record);

	while ((tag = be16_to_cpu(record->tag)) != BI_LAST) {
		int unknown = 0;
		const void *data = record->data;
		uint16_t size = be16_to_cpu(record->size);

		switch (tag) {
		case BI_MACHTYPE:
		case BI_CPUTYPE:
		case BI_FPUTYPE:
		case BI_MMUTYPE:
			/* Already set up by head.S */
			break;

		case BI_MEMCHUNK:
			if (m68k_num_memory < NUM_MEMINFO) {
				const struct mem_info *m = data;
				m68k_memory[m68k_num_memory].addr =
					be32_to_cpu(m->addr);
				m68k_memory[m68k_num_memory].size =
					be32_to_cpu(m->size);
				m68k_num_memory++;
			} else
				pr_warn("%s: too many memory chunks\n",
					__func__);
			break;

		case BI_RAMDISK:
			{
				const struct mem_info *m = data;
				m68k_ramdisk.addr = be32_to_cpu(m->addr);
				m68k_ramdisk.size = be32_to_cpu(m->size);
			}
			break;

		case BI_COMMAND_LINE:
			strlcpy(m68k_command_line, data,
				sizeof(m68k_command_line));
			break;

		default:
			if (MACH_IS_AMIGA)
				unknown = amiga_parse_bootinfo(record);
			else if (MACH_IS_ATARI)
				unknown = atari_parse_bootinfo(record);
			else if (MACH_IS_MAC)
				unknown = mac_parse_bootinfo(record);
			else if (MACH_IS_Q40)
				unknown = q40_parse_bootinfo(record);
			else if (MACH_IS_BVME6000)
				unknown = bvme6000_parse_bootinfo(record);
			else if (MACH_IS_MVME16x)
				unknown = mvme16x_parse_bootinfo(record);
			else if (MACH_IS_MVME147)
				unknown = mvme147_parse_bootinfo(record);
			else if (MACH_IS_HP300)
				unknown = hp300_parse_bootinfo(record);
			else if (MACH_IS_APOLLO)
				unknown = apollo_parse_bootinfo(record);
			else
				unknown = 1;
		}
		if (unknown)
			pr_warn("%s: unknown tag 0x%04x ignored\n", __func__,
				tag);
		record = (struct bi_record *)((unsigned long)record + size);
	}

	m68k_realnum_memory = m68k_num_memory;
#ifdef CONFIG_SINGLE_MEMORY_CHUNK
	if (m68k_num_memory > 1) {
		pr_warn("%s: ignoring last %i chunks of physical memory\n",
			__func__, (m68k_num_memory - 1));
		m68k_num_memory = 1;
	}
#endif
}

void __init setup_arch(char **cmdline_p)
{
#ifndef CONFIG_SUN3
	int i;
#endif

	/* The bootinfo is located right after the kernel */
	if (!CPU_IS_COLDFIRE)
		m68k_parse_bootinfo((const struct bi_record *)_end);

	if (CPU_IS_040)
		m68k_is040or060 = 4;
	else if (CPU_IS_060)
		m68k_is040or060 = 6;

	/* FIXME: m68k_fputype is passed in by Penguin booter, which can
	 * be confused by software FPU emulation. BEWARE.
	 * We should really do our own FPU check at startup.
	 * [what do we do with buggy 68LC040s? if we have problems
	 *  with them, we should add a test to check_bugs() below] */
#if defined(CONFIG_FPU) && !defined(CONFIG_M68KFPU_EMU_ONLY)
	/* clear the fpu if we have one */
	if (m68k_fputype & (FPU_68881|FPU_68882|FPU_68040|FPU_68060|FPU_COLDFIRE)) {
		volatile int zero = 0;
		asm volatile ("frestore %0" : : "m" (zero));
	}
#endif

	if (CPU_IS_060) {
		u32 pcr;

		asm (".chip 68060; movec %%pcr,%0; .chip 68k"
		     : "=d" (pcr));
		if (((pcr >> 8) & 0xff) <= 5) {
			pr_warn("Enabling workaround for errata I14\n");
			asm (".chip 68060; movec %0,%%pcr; .chip 68k"
			     : : "d" (pcr | 0x20));
		}
	}

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

#if defined(CONFIG_BOOTPARAM)
	strncpy(m68k_command_line, CONFIG_BOOTPARAM_STRING, CL_SIZE);
	m68k_command_line[CL_SIZE - 1] = 0;
#endif /* CONFIG_BOOTPARAM */
	process_uboot_commandline(&m68k_command_line[0], CL_SIZE);
	*cmdline_p = m68k_command_line;
	memcpy(boot_command_line, *cmdline_p, CL_SIZE);

	parse_early_param();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	switch (m68k_machtype) {
#ifdef CONFIG_AMIGA
	case MACH_AMIGA:
		config_amiga();
		break;
#endif
#ifdef CONFIG_ATARI
	case MACH_ATARI:
		config_atari();
		break;
#endif
#ifdef CONFIG_MAC
	case MACH_MAC:
		config_mac();
		break;
#endif
#ifdef CONFIG_SUN3
	case MACH_SUN3:
		config_sun3();
		break;
#endif
#ifdef CONFIG_APOLLO
	case MACH_APOLLO:
		config_apollo();
		break;
#endif
#ifdef CONFIG_MVME147
	case MACH_MVME147:
		config_mvme147();
		break;
#endif
#ifdef CONFIG_MVME16x
	case MACH_MVME16x:
		config_mvme16x();
		break;
#endif
#ifdef CONFIG_BVME6000
	case MACH_BVME6000:
		config_bvme6000();
		break;
#endif
#ifdef CONFIG_HP300
	case MACH_HP300:
		config_hp300();
		break;
#endif
#ifdef CONFIG_Q40
	case MACH_Q40:
		config_q40();
		break;
#endif
#ifdef CONFIG_SUN3X
	case MACH_SUN3X:
		config_sun3x();
		break;
#endif
#ifdef CONFIG_COLDFIRE
	case MACH_M54XX:
	case MACH_M5441X:
		config_BSP(NULL, 0);
		break;
#endif
	default:
		panic("No configuration setup");
	}

	paging_init();

#ifdef CONFIG_NATFEAT
	nf_init();
#endif

#ifndef CONFIG_SUN3
	for (i = 1; i < m68k_num_memory; i++)
		free_bootmem_node(NODE_DATA(i), m68k_memory[i].addr,
				  m68k_memory[i].size);
#ifdef CONFIG_BLK_DEV_INITRD
	if (m68k_ramdisk.size) {
		reserve_bootmem_node(__virt_to_node(phys_to_virt(m68k_ramdisk.addr)),
				     m68k_ramdisk.addr, m68k_ramdisk.size,
				     BOOTMEM_DEFAULT);
		initrd_start = (unsigned long)phys_to_virt(m68k_ramdisk.addr);
		initrd_end = initrd_start + m68k_ramdisk.size;
		pr_info("initrd: %08lx - %08lx\n", initrd_start, initrd_end);
	}
#endif

#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI)
		atari_stram_reserve_pages((void *)availmem);
#endif
#ifdef CONFIG_SUN3X
	if (MACH_IS_SUN3X) {
		dvma_init();
	}
#endif

#endif /* !CONFIG_SUN3 */

/* set ISA defs early as possible */
#if defined(CONFIG_ISA) && defined(MULTI_ISA)
	if (MACH_IS_Q40) {
		isa_type = ISA_TYPE_Q40;
		isa_sex = 0;
	}
#ifdef CONFIG_AMIGA_PCMCIA
	if (MACH_IS_AMIGA && AMIGAHW_PRESENT(PCMCIA)) {
		isa_type = ISA_TYPE_AG;
		isa_sex = 1;
	}
#endif
#ifdef CONFIG_ATARI_ROM_ISA
	if (MACH_IS_ATARI) {
		isa_type = ISA_TYPE_ENEC;
		isa_sex = 0;
	}
#endif
#endif
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	const char *cpu, *mmu, *fpu;
	unsigned long clockfreq, clockfactor;

#define LOOP_CYCLES_68020	(8)
#define LOOP_CYCLES_68030	(8)
#define LOOP_CYCLES_68040	(3)
#define LOOP_CYCLES_68060	(1)
#define LOOP_CYCLES_COLDFIRE	(2)

	if (CPU_IS_020) {
		cpu = "68020";
		clockfactor = LOOP_CYCLES_68020;
	} else if (CPU_IS_030) {
		cpu = "68030";
		clockfactor = LOOP_CYCLES_68030;
	} else if (CPU_IS_040) {
		cpu = "68040";
		clockfactor = LOOP_CYCLES_68040;
	} else if (CPU_IS_060) {
		cpu = "68060";
		clockfactor = LOOP_CYCLES_68060;
	} else if (CPU_IS_COLDFIRE) {
		cpu = "ColdFire";
		clockfactor = LOOP_CYCLES_COLDFIRE;
	} else {
		cpu = "680x0";
		clockfactor = 0;
	}

#ifdef CONFIG_M68KFPU_EMU_ONLY
	fpu = "none(soft float)";
#else
	if (m68k_fputype & FPU_68881)
		fpu = "68881";
	else if (m68k_fputype & FPU_68882)
		fpu = "68882";
	else if (m68k_fputype & FPU_68040)
		fpu = "68040";
	else if (m68k_fputype & FPU_68060)
		fpu = "68060";
	else if (m68k_fputype & FPU_SUNFPA)
		fpu = "Sun FPA";
	else if (m68k_fputype & FPU_COLDFIRE)
		fpu = "ColdFire";
	else
		fpu = "none";
#endif

	if (m68k_mmutype & MMU_68851)
		mmu = "68851";
	else if (m68k_mmutype & MMU_68030)
		mmu = "68030";
	else if (m68k_mmutype & MMU_68040)
		mmu = "68040";
	else if (m68k_mmutype & MMU_68060)
		mmu = "68060";
	else if (m68k_mmutype & MMU_SUN3)
		mmu = "Sun-3";
	else if (m68k_mmutype & MMU_APOLLO)
		mmu = "Apollo";
	else if (m68k_mmutype & MMU_COLDFIRE)
		mmu = "ColdFire";
	else
		mmu = "unknown";

	clockfreq = loops_per_jiffy * HZ * clockfactor;

	seq_printf(m, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   loops_per_jiffy/(500000/HZ),(loops_per_jiffy/(5000/HZ))%100,
		   loops_per_jiffy);
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}
static void c_stop(struct seq_file *m, void *v)
{
}
const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

#ifdef CONFIG_PROC_HARDWARE
static int hardware_proc_show(struct seq_file *m, void *v)
{
	char model[80];
	unsigned long mem;
	int i;

	if (mach_get_model)
		mach_get_model(model);
	else
		strcpy(model, "Unknown m68k");

	seq_printf(m, "Model:\t\t%s\n", model);
	for (mem = 0, i = 0; i < m68k_num_memory; i++)
		mem += m68k_memory[i].size;
	seq_printf(m, "System Memory:\t%ldK\n", mem >> 10);

	if (mach_get_hardware_list)
		mach_get_hardware_list(m);

	return 0;
}

static int hardware_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, hardware_proc_show, NULL);
}

static const struct file_operations hardware_proc_fops = {
	.open		= hardware_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_hardware_init(void)
{
	proc_create("hardware", 0, NULL, &hardware_proc_fops);
	return 0;
}
module_init(proc_hardware_init);
#endif

void check_bugs(void)
{
#if defined(CONFIG_FPU) && !defined(CONFIG_M68KFPU_EMU)
	if (m68k_fputype == 0) {
		pr_emerg("*** YOU DO NOT HAVE A FLOATING POINT UNIT, "
			"WHICH IS REQUIRED BY LINUX/M68K ***\n");
		pr_emerg("Upgrade your hardware or join the FPU "
			"emulation project\n");
		panic("no FPU");
	}
#endif /* !CONFIG_M68KFPU_EMU */
}

#ifdef CONFIG_ADB
static int __init adb_probe_sync_enable (char *str) {
	extern int __adb_probe_sync;
	__adb_probe_sync = 1;
	return 1;
}

__setup("adb_sync", adb_probe_sync_enable);
#endif /* CONFIG_ADB */
