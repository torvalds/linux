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
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/initrd.h>

#include <asm/bootinfo.h>
#include <asm/setup.h>
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

unsigned long m68k_machtype;
unsigned long m68k_cputype;
EXPORT_SYMBOL(m68k_machtype);
EXPORT_SYMBOL(m68k_cputype);
unsigned long m68k_fputype;
unsigned long m68k_mmutype;
#ifdef CONFIG_VME
unsigned long vme_brdtype;
EXPORT_SYMBOL(vme_brdtype);
#endif

int m68k_is040or060;
EXPORT_SYMBOL(m68k_is040or060);

extern int end;
extern unsigned long availmem;

int m68k_num_memory;
int m68k_realnum_memory;
EXPORT_SYMBOL(m68k_realnum_memory);
#ifdef CONFIG_SINGLE_MEMORY_CHUNK
unsigned long m68k_memoffset;
EXPORT_SYMBOL(m68k_memoffset);
#endif
struct mem_info m68k_memory[NUM_MEMINFO];
EXPORT_SYMBOL(m68k_memory);

static struct mem_info m68k_ramdisk;

static char m68k_command_line[CL_SIZE];

char m68k_debug_device[6] = "";
EXPORT_SYMBOL(m68k_debug_device);

void (*mach_sched_init) (irq_handler_t handler) __initdata = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata = NULL;
void (*mach_get_model) (char *model);
int (*mach_get_hardware_list) (char *buffer);
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void);
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
#if defined(CONFIG_INPUT_M68K_BEEP) || defined(CONFIG_INPUT_M68K_BEEP_MODULE)
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
	while (record->tag != BI_LAST) {
		int unknown = 0;
		const unsigned long *data = record->data;

		switch (record->tag) {
		case BI_MACHTYPE:
		case BI_CPUTYPE:
		case BI_FPUTYPE:
		case BI_MMUTYPE:
			/* Already set up by head.S */
			break;

		case BI_MEMCHUNK:
			if (m68k_num_memory < NUM_MEMINFO) {
				m68k_memory[m68k_num_memory].addr = data[0];
				m68k_memory[m68k_num_memory].size = data[1];
				m68k_num_memory++;
			} else
				printk("m68k_parse_bootinfo: too many memory chunks\n");
			break;

		case BI_RAMDISK:
			m68k_ramdisk.addr = data[0];
			m68k_ramdisk.size = data[1];
			break;

		case BI_COMMAND_LINE:
			strlcpy(m68k_command_line, (const char *)data,
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
			else
				unknown = 1;
		}
		if (unknown)
			printk("m68k_parse_bootinfo: unknown tag 0x%04x ignored\n",
			       record->tag);
		record = (struct bi_record *)((unsigned long)record +
					      record->size);
	}

	m68k_realnum_memory = m68k_num_memory;
#ifdef CONFIG_SINGLE_MEMORY_CHUNK
	if (m68k_num_memory > 1) {
		printk("Ignoring last %i chunks of physical memory\n",
		       (m68k_num_memory - 1));
		m68k_num_memory = 1;
	}
	m68k_memoffset = m68k_memory[0].addr-PAGE_OFFSET;
#endif
}

void __init setup_arch(char **cmdline_p)
{
	extern int _etext, _edata, _end;
#ifndef CONFIG_SUN3
	unsigned long endmem, startmem;
#endif
	int i;
	char *p, *q;

	/* The bootinfo is located right after the kernel bss */
	m68k_parse_bootinfo((const struct bi_record *)&_end);

	if (CPU_IS_040)
		m68k_is040or060 = 4;
	else if (CPU_IS_060)
		m68k_is040or060 = 6;

	/* FIXME: m68k_fputype is passed in by Penguin booter, which can
	 * be confused by software FPU emulation. BEWARE.
	 * We should really do our own FPU check at startup.
	 * [what do we do with buggy 68LC040s? if we have problems
	 *  with them, we should add a test to check_bugs() below] */
#ifndef CONFIG_M68KFPU_EMU_ONLY
	/* clear the fpu if we have one */
	if (m68k_fputype & (FPU_68881|FPU_68882|FPU_68040|FPU_68060)) {
		volatile int zero = 0;
		asm volatile ("frestore %0" : : "m" (zero));
	}
#endif

	if (CPU_IS_060) {
		u32 pcr;

		asm (".chip 68060; movec %%pcr,%0; .chip 68k"
		     : "=d" (pcr));
		if (((pcr >> 8) & 0xff) <= 5) {
			printk("Enabling workaround for errata I14\n");
			asm (".chip 68060; movec %0,%%pcr; .chip 68k"
			     : : "d" (pcr | 0x20));
		}
	}

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	*cmdline_p = m68k_command_line;
	memcpy(boot_command_line, *cmdline_p, CL_SIZE);

	/* Parse the command line for arch-specific options.
	 * For the m68k, this is currently only "debug=xxx" to enable printing
	 * certain kernel messages to some machine-specific device.
	 */
	for (p = *cmdline_p; p && *p;) {
		i = 0;
		if (!strncmp(p, "debug=", 6)) {
			strlcpy(m68k_debug_device, p+6, sizeof(m68k_debug_device));
			q = strchr(m68k_debug_device, ' ');
			if (q)
				*q = 0;
			i = 1;
		}
#ifdef CONFIG_ATARI
		/* This option must be parsed very early */
		if (!strncmp(p, "switches=", 9)) {
			extern void atari_switches_setup(const char *, int);
			q = strchr(p + 9, ' ');
			atari_switches_setup(p + 9, q ? (q - (p + 9)) : strlen(p + 9));
			i = 1;
		}
#endif

		if (i) {
			/* option processed, delete it */
			if ((q = strchr(p, ' ')))
				strcpy(p, q + 1);
			else
				*p = 0;
		} else {
			if ((p = strchr(p, ' ')))
				++p;
		}
	}

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
	default:
		panic("No configuration setup");
	}

#ifndef CONFIG_SUN3
	startmem= m68k_memory[0].addr;
	endmem = startmem + m68k_memory[0].size;
	high_memory = (void *)PAGE_OFFSET;
	for (i = 0; i < m68k_num_memory; i++) {
		m68k_memory[i].size &= MASK_256K;
		if (m68k_memory[i].addr < startmem)
			startmem = m68k_memory[i].addr;
		if (m68k_memory[i].addr+m68k_memory[i].size > endmem)
			endmem = m68k_memory[i].addr+m68k_memory[i].size;
		high_memory += m68k_memory[i].size;
	}

	availmem += init_bootmem_node(NODE_DATA(0), availmem >> PAGE_SHIFT,
				      startmem >> PAGE_SHIFT, endmem >> PAGE_SHIFT);

	for (i = 0; i < m68k_num_memory; i++)
		free_bootmem(m68k_memory[i].addr, m68k_memory[i].size);

	reserve_bootmem(m68k_memory[0].addr, availmem - m68k_memory[0].addr);

#ifdef CONFIG_BLK_DEV_INITRD
	if (m68k_ramdisk.size) {
		reserve_bootmem(m68k_ramdisk.addr, m68k_ramdisk.size);
		initrd_start = (unsigned long)phys_to_virt(m68k_ramdisk.addr);
		initrd_end = initrd_start + m68k_ramdisk.size;
		printk("initrd: %08lx - %08lx\n", initrd_start, initrd_end);
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

	paging_init();

/* set ISA defs early as possible */
#if defined(CONFIG_ISA) && defined(MULTI_ISA)
#if defined(CONFIG_Q40)
	if (MACH_IS_Q40) {
		isa_type = Q40_ISA;
		isa_sex = 0;
	}
#elif defined(CONFIG_GG2)
	if (MACH_IS_AMIGA && AMIGAHW_PRESENT(GG2_ISA)) {
		isa_type = GG2_ISA;
		isa_sex = 0;
	}
#elif defined(CONFIG_AMIGA_PCMCIA)
	if (MACH_IS_AMIGA && AMIGAHW_PRESENT(PCMCIA)) {
		isa_type = AG_ISA;
		isa_sex = 1;
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
struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

int get_hardware_list(char *buffer)
{
	int len = 0;
	char model[80];
	unsigned long mem;
	int i;

	if (mach_get_model)
		mach_get_model(model);
	else
		strcpy(model, "Unknown m68k");

	len += sprintf(buffer + len, "Model:\t\t%s\n", model);
	for (mem = 0, i = 0; i < m68k_num_memory; i++)
		mem += m68k_memory[i].size;
	len += sprintf(buffer + len, "System Memory:\t%ldK\n", mem >> 10);

	if (mach_get_hardware_list)
		len += mach_get_hardware_list(buffer + len);

	return len;
}

void check_bugs(void)
{
#ifndef CONFIG_M68KFPU_EMU
	if (m68k_fputype == 0) {
		printk(KERN_EMERG "*** YOU DO NOT HAVE A FLOATING POINT UNIT, "
			"WHICH IS REQUIRED BY LINUX/M68K ***\n");
		printk(KERN_EMERG "Upgrade your hardware or join the FPU "
			"emulation project\n");
		panic("no FPU");
	}
#endif /* !CONFIG_M68KFPU_EMU */
}
