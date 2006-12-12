/*
 * Common prep boot and setup code.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/ide.h>
#include <linux/screen_info.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/console.h>

#include <asm/residual.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigappc.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/cputable.h>
#include <asm/bootx.h>
#include <asm/btext.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/nvram.h>
#include <asm/xmon.h>
#include <asm/ocp.h>
#include <asm/prom.h>

#define USES_PPC_SYS (defined(CONFIG_85xx) || defined(CONFIG_83xx) || \
		      defined(CONFIG_MPC10X_BRIDGE) || defined(CONFIG_8260) || \
		      defined(CONFIG_PPC_MPC52xx))

#if USES_PPC_SYS
#include <asm/ppc_sys.h>
#endif

#if defined CONFIG_KGDB
#include <asm/kgdb.h>
#endif

extern void platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);
extern void reloc_got2(unsigned long offset);

extern void ppc6xx_idle(void);
extern void power4_idle(void);

extern boot_infos_t *boot_infos;
struct ide_machdep_calls ppc_ide_md;

/* Used with the BI_MEMSIZE bootinfo parameter to store the memory
   size value reported by the boot loader. */
unsigned long boot_mem_size;

unsigned long ISA_DMA_THRESHOLD;
unsigned int DMA_MODE_READ;
unsigned int DMA_MODE_WRITE;

#ifdef CONFIG_PPC_PREP
extern void prep_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);

dev_t boot_dev;
#endif /* CONFIG_PPC_PREP */

int have_of;
EXPORT_SYMBOL(have_of);

#ifdef __DO_IRQ_CANON
int ppc_do_canonicalize_irqs;
EXPORT_SYMBOL(ppc_do_canonicalize_irqs);
#endif

#ifdef CONFIG_VGA_CONSOLE
unsigned long vgacon_remap_base;
#endif

struct machdep_calls ppc_md;

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_FB_VGA16) || \
    defined(CONFIG_FB_VGA16_MODULE) || defined(CONFIG_FB_VESA)
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
};
#endif /* CONFIG_VGA_CONSOLE || CONFIG_FB_VGA16 || CONFIG_FB_VESA */

void machine_restart(char *cmd)
{
#ifdef CONFIG_NVRAM
	nvram_sync();
#endif
	ppc_md.restart(cmd);
}

static void ppc_generic_power_off(void)
{
	ppc_md.power_off();
}

void machine_halt(void)
{
#ifdef CONFIG_NVRAM
	nvram_sync();
#endif
	ppc_md.halt();
}

void (*pm_power_off)(void) = ppc_generic_power_off;

void machine_power_off(void)
{
#ifdef CONFIG_NVRAM
	nvram_sync();
#endif
	if (pm_power_off)
		pm_power_off();
	ppc_generic_power_off();
}

#ifdef CONFIG_TAU
extern u32 cpu_temp(unsigned long cpu);
extern u32 cpu_temp_both(unsigned long cpu);
#endif /* CONFIG_TAU */

int show_cpuinfo(struct seq_file *m, void *v)
{
	int i = (int) v - 1;
	int err = 0;
	unsigned int pvr;
	unsigned short maj, min;
	unsigned long lpj;

	if (i >= NR_CPUS) {
		/* Show summary information */
#ifdef CONFIG_SMP
		unsigned long bogosum = 0;
		for_each_online_cpu(i)
			bogosum += cpu_data[i].loops_per_jiffy;
		seq_printf(m, "total bogomips\t: %lu.%02lu\n",
			   bogosum/(500000/HZ), bogosum/(5000/HZ) % 100);
#endif /* CONFIG_SMP */

		if (ppc_md.show_cpuinfo != NULL)
			err = ppc_md.show_cpuinfo(m);
		return err;
	}

#ifdef CONFIG_SMP
	if (!cpu_online(i))
		return 0;
	pvr = cpu_data[i].pvr;
	lpj = cpu_data[i].loops_per_jiffy;
#else
	pvr = mfspr(SPRN_PVR);
	lpj = loops_per_jiffy;
#endif

	seq_printf(m, "processor\t: %d\n", i);
	seq_printf(m, "cpu\t\t: ");

	if (cur_cpu_spec->pvr_mask)
		seq_printf(m, "%s", cur_cpu_spec->cpu_name);
	else
		seq_printf(m, "unknown (%08x)", pvr);
#ifdef CONFIG_ALTIVEC
	if (cur_cpu_spec->cpu_features & CPU_FTR_ALTIVEC)
		seq_printf(m, ", altivec supported");
#endif
	seq_printf(m, "\n");

#ifdef CONFIG_TAU
	if (cur_cpu_spec->cpu_features & CPU_FTR_TAU) {
#ifdef CONFIG_TAU_AVERAGE
		/* more straightforward, but potentially misleading */
		seq_printf(m,  "temperature \t: %u C (uncalibrated)\n",
			   cpu_temp(i));
#else
		/* show the actual temp sensor range */
		u32 temp;
		temp = cpu_temp_both(i);
		seq_printf(m, "temperature \t: %u-%u C (uncalibrated)\n",
			   temp & 0xff, temp >> 16);
#endif
	}
#endif /* CONFIG_TAU */

	if (ppc_md.show_percpuinfo != NULL) {
		err = ppc_md.show_percpuinfo(m, i);
		if (err)
			return err;
	}

	/* If we are a Freescale core do a simple check so
	 * we dont have to keep adding cases in the future */
	if ((PVR_VER(pvr) & 0x8000) == 0x8000) {
		maj = PVR_MAJ(pvr);
		min = PVR_MIN(pvr);
	} else {
		switch (PVR_VER(pvr)) {
			case 0x0020:	/* 403 family */
				maj = PVR_MAJ(pvr) + 1;
				min = PVR_MIN(pvr);
				break;
			case 0x1008:	/* 740P/750P ?? */
				maj = ((pvr >> 8) & 0xFF) - 1;
				min = pvr & 0xFF;
				break;
			default:
				maj = (pvr >> 8) & 0xFF;
				min = pvr & 0xFF;
				break;
		}
	}

	seq_printf(m, "revision\t: %hd.%hd (pvr %04x %04x)\n",
		   maj, min, PVR_VER(pvr), PVR_REV(pvr));

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   lpj / (500000/HZ), (lpj / (5000/HZ)) % 100);

#if USES_PPC_SYS
	if (cur_ppc_sys_spec->ppc_sys_name)
		seq_printf(m, "chipset\t\t: %s\n",
			cur_ppc_sys_spec->ppc_sys_name);
#endif

#ifdef CONFIG_SMP
	seq_printf(m, "\n");
#endif

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	int i = *pos;

	return i <= NR_CPUS? (void *) (i + 1): NULL;
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
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

/*
 * We're called here very early in the boot.  We determine the machine
 * type and call the appropriate low-level setup functions.
 *  -- Cort <cort@fsmlabs.com>
 *
 * Note that the kernel may be running at an address which is different
 * from the address that it was linked at, so we must use RELOC/PTRRELOC
 * to access static data (including strings).  -- paulus
 */
__init
unsigned long
early_init(int r3, int r4, int r5)
{
 	unsigned long phys;
	unsigned long offset = reloc_offset();
	struct cpu_spec *spec;

 	/* Default */
 	phys = offset + KERNELBASE;

	/* First zero the BSS -- use memset, some arches don't have
	 * caches on yet */
	memset_io(PTRRELOC(&__bss_start), 0, _end - __bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	spec = identify_cpu(offset, mfspr(SPRN_PVR));
	do_feature_fixups(spec->cpu_features,
			  PTRRELOC(&__start___ftr_fixup),
			  PTRRELOC(&__stop___ftr_fixup));

	return phys;
}

#ifdef CONFIG_PPC_PREP
/*
 * The PPC_PREP version of platform_init...
 */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_BOOTX_TEXT
	if (boot_text_mapped) {
		btext_clearscreen();
		btext_welcome();
	}
#endif

	parse_bootinfo(find_bootinfo());

	prep_init(r3, r4, r5, r6, r7);
}
#endif /* CONFIG_PPC_PREP */

struct bi_record *find_bootinfo(void)
{
	struct bi_record *rec;

	rec = (struct bi_record *)_ALIGN((ulong)__bss_start+(1<<20)-1,(1<<20));
	if ( rec->tag != BI_FIRST ) {
		/*
		 * This 0x10000 offset is a terrible hack but it will go away when
		 * we have the bootloader handle all the relocation and
		 * prom calls -- Cort
		 */
		rec = (struct bi_record *)_ALIGN((ulong)__bss_start+0x10000+(1<<20)-1,(1<<20));
		if ( rec->tag != BI_FIRST )
			return NULL;
	}
	return rec;
}

void parse_bootinfo(struct bi_record *rec)
{
	if (rec == NULL || rec->tag != BI_FIRST)
		return;
	while (rec->tag != BI_LAST) {
		ulong *data = rec->data;
		switch (rec->tag) {
		case BI_CMD_LINE:
			strlcpy(cmd_line, (void *)data, sizeof(cmd_line));
			break;
#ifdef CONFIG_BLK_DEV_INITRD
		case BI_INITRD:
			initrd_start = data[0] + KERNELBASE;
			initrd_end = data[0] + data[1] + KERNELBASE;
			break;
#endif /* CONFIG_BLK_DEV_INITRD */
		case BI_MEMSIZE:
			boot_mem_size = data[0];
			break;
		}
		rec = (struct bi_record *)((ulong)rec + rec->size);
	}
}

/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init()).
 * This is called very early on the boot process, after a minimal
 * MMU environment has been set up but before MMU_init is called.
 */
void __init
machine_init(unsigned long r3, unsigned long r4, unsigned long r5,
	     unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_CMDLINE
	strlcpy(cmd_line, CONFIG_CMDLINE, sizeof(cmd_line));
#endif /* CONFIG_CMDLINE */

#ifdef CONFIG_6xx
	ppc_md.power_save = ppc6xx_idle;
#endif

	platform_init(r3, r4, r5, r6, r7);

	if (ppc_md.progress)
		ppc_md.progress("id mach(): done", 0x200);
}
#ifdef CONFIG_BOOKE_WDT
/* Checks wdt=x and wdt_period=xx command-line option */
int __init early_parse_wdt(char *p)
{
	if (p && strncmp(p, "0", 1) != 0)
	       booke_wdt_enabled = 1;

	return 0;
}
early_param("wdt", early_parse_wdt);

int __init early_parse_wdt_period (char *p)
{
	if (p)
		booke_wdt_period = simple_strtoul(p, NULL, 0);

	return 0;
}
early_param("wdt_period", early_parse_wdt_period);
#endif	/* CONFIG_BOOKE_WDT */

/* Checks "l2cr=xxxx" command-line option */
int __init ppc_setup_l2cr(char *str)
{
	if (cpu_has_feature(CPU_FTR_L2CR)) {
		unsigned long val = simple_strtoul(str, NULL, 0);
		printk(KERN_INFO "l2cr set to %lx\n", val);
		_set_L2CR(0);		/* force invalidate by disable cache */
		_set_L2CR(val);		/* and enable it */
	}
	return 1;
}
__setup("l2cr=", ppc_setup_l2cr);

#ifdef CONFIG_GENERIC_NVRAM

/* Generic nvram hooks used by drivers/char/gen_nvram.c */
unsigned char nvram_read_byte(int addr)
{
	if (ppc_md.nvram_read_val)
		return ppc_md.nvram_read_val(addr);
	return 0xff;
}
EXPORT_SYMBOL(nvram_read_byte);

void nvram_write_byte(unsigned char val, int addr)
{
	if (ppc_md.nvram_write_val)
		ppc_md.nvram_write_val(addr, val);
}
EXPORT_SYMBOL(nvram_write_byte);

void nvram_sync(void)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
}
EXPORT_SYMBOL(nvram_sync);

#endif /* CONFIG_NVRAM */

static struct cpu cpu_devices[NR_CPUS];

int __init ppc_init(void)
{
	int i;

	/* clear the progress line */
	if ( ppc_md.progress ) ppc_md.progress("             ", 0xffff);

	/* register CPU devices */
	for_each_possible_cpu(i)
		register_cpu(&cpu_devices[i], i);

	/* call platform init */
	if (ppc_md.init != NULL) {
		ppc_md.init();
	}
	return 0;
}

arch_initcall(ppc_init);

/* Warning, IO base is not yet inited */
void __init setup_arch(char **cmdline_p)
{
	extern char *klimit;
	extern void do_init_bootmem(void);

	/* so udelay does something sensible, assume <= 1000 bogomips */
	loops_per_jiffy = 500000000 / HZ;

	if (ppc_md.init_early)
		ppc_md.init_early();

#ifdef CONFIG_XMON
	xmon_init(1);
	if (strstr(cmd_line, "xmon"))
		xmon(NULL);
#endif /* CONFIG_XMON */
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: enter", 0x3eab);

#if defined(CONFIG_KGDB)
	if (ppc_md.kgdb_map_scc)
		ppc_md.kgdb_map_scc();
	set_debug_traps();
	if (strstr(cmd_line, "gdb")) {
		if (ppc_md.progress)
			ppc_md.progress("setup_arch: kgdb breakpoint", 0x4000);
		printk("kgdb breakpoint activated\n");
		breakpoint();
	}
#endif

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	if (cpu_has_feature(CPU_FTR_SPLIT_ID_CACHE)) {
		dcache_bsize = cur_cpu_spec->dcache_bsize;
		icache_bsize = cur_cpu_spec->icache_bsize;
		ucache_bsize = 0;
	} else
		ucache_bsize = dcache_bsize = icache_bsize
			= cur_cpu_spec->dcache_bsize;

	/* reboot on panic */
	panic_timeout = 180;

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) klimit;

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(saved_command_line, cmd_line, COMMAND_LINE_SIZE);
	*cmdline_p = cmd_line;

	parse_early_param();

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: bootmem", 0x3eab);

#ifdef CONFIG_PPC_OCP
	/* Initialize OCP device list */
	ocp_early_init();
	if ( ppc_md.progress ) ppc_md.progress("ocp: exit", 0x3eab);
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	ppc_md.setup_arch();
	if ( ppc_md.progress ) ppc_md.progress("arch: exit", 0x3eab);

	paging_init();
}
