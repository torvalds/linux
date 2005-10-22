/*
 * Common prep/pmac/chrp boot and setup code.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/ide.h>
#include <linux/tty.h>
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
#include <asm/pmac_feature.h>
#include <asm/sections.h>
#include <asm/nvram.h>
#include <asm/xmon.h>
#include <asm/ocp.h>

#define USES_PPC_SYS (defined(CONFIG_85xx) || defined(CONFIG_83xx) || \
		      defined(CONFIG_MPC10X_BRIDGE) || defined(CONFIG_8260) || \
		      defined(CONFIG_PPC_MPC52xx))

#if USES_PPC_SYS
#include <asm/ppc_sys.h>
#endif

#if defined CONFIG_KGDB
#include <asm/kgdb.h>
#endif

extern void platform_init(void);
extern void bootx_init(unsigned long r4, unsigned long phys);

extern void ppc6xx_idle(void);
extern void power4_idle(void);

boot_infos_t *boot_infos;
struct ide_machdep_calls ppc_ide_md;

/* Used with the BI_MEMSIZE bootinfo parameter to store the memory
   size value reported by the boot loader. */
unsigned long boot_mem_size;

unsigned long ISA_DMA_THRESHOLD;
unsigned int DMA_MODE_READ;
unsigned int DMA_MODE_WRITE;

int have_of = 1;

#ifdef CONFIG_PPC_MULTIPLATFORM
int _machine = 0;

extern void prep_init(void);
extern void pmac_init(void);
extern void chrp_init(void);

dev_t boot_dev;
#endif /* CONFIG_PPC_MULTIPLATFORM */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY = 0x54;
#endif /* CONFIG_MAGIC_SYSRQ */

#ifdef CONFIG_VGA_CONSOLE
unsigned long vgacon_remap_base;
#endif

struct machdep_calls ppc_md;
EXPORT_SYMBOL(ppc_md);

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

void machine_power_off(void)
{
#ifdef CONFIG_NVRAM
	nvram_sync();
#endif
	ppc_md.power_off();
}

void machine_halt(void)
{
#ifdef CONFIG_NVRAM
	nvram_sync();
#endif
	ppc_md.halt();
}

void (*pm_power_off)(void) = machine_power_off;

#ifdef CONFIG_TAU
extern u32 cpu_temp(unsigned long cpu);
extern u32 cpu_temp_both(unsigned long cpu);
#endif /* CONFIG_TAU */

int show_cpuinfo(struct seq_file *m, void *v)
{
	int i = (int) v - 1;
	unsigned int pvr;
	unsigned short maj, min;
	unsigned long lpj;

	if (i >= NR_CPUS) {
		/* Show summary information */
#ifdef CONFIG_SMP
		unsigned long bogosum = 0;
		for (i = 0; i < NR_CPUS; ++i)
			if (cpu_online(i))
				bogosum += cpu_data[i].loops_per_jiffy;
		seq_printf(m, "total bogomips\t: %lu.%02lu\n",
			   bogosum/(500000/HZ), bogosum/(5000/HZ) % 100);
#endif /* CONFIG_SMP */

		if (ppc_md.show_cpuinfo != NULL)
			ppc_md.show_cpuinfo(m);
		return 0;
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

	if (ppc_md.show_percpuinfo != NULL)
		ppc_md.show_percpuinfo(m, i);

	/* If we are a Freescale core do a simple check so
	 * we dont have to keep adding cases in the future */
	if (PVR_VER(pvr) & 0x8000) {
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
unsigned long __init early_init(unsigned long dt_ptr)
{
	unsigned long offset = reloc_offset();

	/* First zero the BSS -- use memset_io, some platforms don't have
	 * caches on yet */
	memset_io(PTRRELOC(&__bss_start), 0, _end - __bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	identify_cpu(offset, 0);
	do_cpu_ftr_fixups(offset);

	return KERNELBASE + offset;
}

#ifdef CONFIG_PPC_OF
/*
 * Assume here that all clock rates are the same in a
 * smp system.  -- Cort
 */
int
of_show_percpuinfo(struct seq_file *m, int i)
{
	struct device_node *cpu_node;
	u32 *fp;
	int s;
	
	cpu_node = find_type_devices("cpu");
	if (!cpu_node)
		return 0;
	for (s = 0; s < i && cpu_node->next; s++)
		cpu_node = cpu_node->next;
	fp = (u32 *)get_property(cpu_node, "clock-frequency", NULL);
	if (fp)
		seq_printf(m, "clock\t\t: %dMHz\n", *fp / 1000000);
	return 0;
}

void __init
intuit_machine_type(void)
{
	char *model;
	struct device_node *root;
	
	/* ask the OF info if we're a chrp or pmac */
	root = find_path_device("/");
	if (root != 0) {
		/* assume pmac unless proven to be chrp -- Cort */
		_machine = _MACH_Pmac;
		model = get_property(root, "device_type", NULL);
		if (model && !strncmp("chrp", model, 4))
			_machine = _MACH_chrp;
		else {
			model = get_property(root, "model", NULL);
			if (model && !strncmp(model, "IBM", 3))
				_machine = _MACH_chrp;
		}
	}
}
#endif

#ifdef CONFIG_PPC_MULTIPLATFORM
/*
 * The PPC_MULTIPLATFORM version of platform_init...
 */
void __init platform_init(void)
{
	/* if we didn't get any bootinfo telling us what we are... */
	if (_machine == 0) {
		/* prep boot loader tells us if we're prep or not */
		if ( *(unsigned long *)(KERNELBASE) == (0xdeadc0de) )
			_machine = _MACH_prep;
	}

#ifdef CONFIG_PPC_PREP
	/* not much more to do here, if prep */
	if (_machine == _MACH_prep) {
		prep_init();
		return;
	}
#endif

#ifdef CONFIG_ADB
	if (strstr(cmd_line, "adb_sync")) {
		extern int __adb_probe_sync;
		__adb_probe_sync = 1;
	}
#endif /* CONFIG_ADB */

	switch (_machine) {
#ifdef CONFIG_PPC_PMAC
	case _MACH_Pmac:
		pmac_init();
		break;
#endif
#ifdef CONFIG_PPC_CHRP
	case _MACH_chrp:
		chrp_init();
		break;
#endif
	}
}

#ifdef CONFIG_SERIAL_CORE_CONSOLE
extern char *of_stdout_device;

static int __init set_preferred_console(void)
{
	struct device_node *prom_stdout;
	char *name;
	int offset = 0;

	if (of_stdout_device == NULL)
		return -ENODEV;

	/* The user has requested a console so this is already set up. */
	if (strstr(saved_command_line, "console="))
		return -EBUSY;

	prom_stdout = find_path_device(of_stdout_device);
	if (!prom_stdout)
		return -ENODEV;

	name = (char *)get_property(prom_stdout, "name", NULL);
	if (!name)
		return -ENODEV;

	if (strcmp(name, "serial") == 0) {
		int i;
		u32 *reg = (u32 *)get_property(prom_stdout, "reg", &i);
		if (i > 8) {
			switch (reg[1]) {
				case 0x3f8:
					offset = 0;
					break;
				case 0x2f8:
					offset = 1;
					break;
				case 0x898:
					offset = 2;
					break;
				case 0x890:
					offset = 3;
					break;
				default:
					/* We dont recognise the serial port */
					return -ENODEV;
			}
		}
	} else if (strcmp(name, "ch-a") == 0)
		offset = 0;
	else if (strcmp(name, "ch-b") == 0)
		offset = 1;
	else
		return -ENODEV;
	return add_preferred_console("ttyS", offset, NULL);
}
console_initcall(set_preferred_console);
#endif /* CONFIG_SERIAL_CORE_CONSOLE */
#endif /* CONFIG_PPC_MULTIPLATFORM */

/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init()).
 * This is called very early on the boot process, after a minimal
 * MMU environment has been set up but before MMU_init is called.
 */
void __init machine_init(unsigned long dt_ptr, unsigned long phys)
{
	early_init_devtree(__va(dt_ptr));

#ifdef CONFIG_CMDLINE
	strlcpy(cmd_line, CONFIG_CMDLINE, sizeof(cmd_line));
#endif /* CONFIG_CMDLINE */

	platform_init();

#ifdef CONFIG_6xx
	ppc_md.power_save = ppc6xx_idle;
#endif

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
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_possible(i))
			register_cpu(&cpu_devices[i], i, NULL);

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

	unflatten_device_tree();
	finish_device_tree();

#ifdef CONFIG_BOOTX_TEXT
	init_boot_display();
#endif

#ifdef CONFIG_PPC_MULTIPLATFORM
	/* This could be called "early setup arch", it must be done
	 * now because xmon need it
	 */
	if (_machine == _MACH_Pmac)
		pmac_feature_init();	/* New cool way */
#endif

#ifdef CONFIG_XMON
	xmon_map_scc();
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

	/* this is for modules since _machine can be a define -- Cort */
	ppc_md.ppc_machine = _machine;
}
