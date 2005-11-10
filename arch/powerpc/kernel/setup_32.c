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
#include <asm/time.h>

#include "setup.h"

#define DBG(fmt...)

#if defined CONFIG_KGDB
#include <asm/kgdb.h>
#endif

extern void platform_init(void);
extern void bootx_init(unsigned long r4, unsigned long phys);

extern void ppc6xx_idle(void);
extern void power4_idle(void);

boot_infos_t *boot_infos;
struct ide_machdep_calls ppc_ide_md;

/* XXX should go elsewhere */
int __irq_offset_value;
EXPORT_SYMBOL(__irq_offset_value);

int boot_cpuid;
EXPORT_SYMBOL_GPL(boot_cpuid);
int boot_cpuid_phys;

unsigned long ISA_DMA_THRESHOLD;
unsigned int DMA_MODE_READ;
unsigned int DMA_MODE_WRITE;

int have_of = 1;

#ifdef CONFIG_PPC_MULTIPLATFORM
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
#endif

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
	extern void do_init_bootmem(void);

	/* so udelay does something sensible, assume <= 1000 bogomips */
	loops_per_jiffy = 500000000 / HZ;

	unflatten_device_tree();
	check_for_initrd();
	finish_device_tree();

	smp_setup_cpu_maps();

#ifdef CONFIG_BOOTX_TEXT
	init_boot_display();
#endif

#ifdef CONFIG_PPC_PMAC
	/* This could be called "early setup arch", it must be done
	 * now because xmon need it
	 */
	if (_machine == _MACH_Pmac)
		pmac_feature_init();	/* New cool way */
#endif

#ifdef CONFIG_XMON_DEFAULT
	xmon_init(1);
#endif

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
	init_mm.brk = klimit;

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
