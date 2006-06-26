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
#include <asm/serial.h>
#include <asm/udbg.h>

#include "setup.h"

#define DBG(fmt...)

#if defined CONFIG_KGDB
#include <asm/kgdb.h>
#endif

extern void bootx_init(unsigned long r4, unsigned long phys);

boot_infos_t *boot_infos;
struct ide_machdep_calls ppc_ide_md;

int boot_cpuid;
EXPORT_SYMBOL_GPL(boot_cpuid);
int boot_cpuid_phys;

unsigned long ISA_DMA_THRESHOLD;
unsigned int DMA_MODE_READ;
unsigned int DMA_MODE_WRITE;

int have_of = 1;

#ifdef CONFIG_PPC_MULTIPLATFORM
dev_t boot_dev;
#endif /* CONFIG_PPC_MULTIPLATFORM */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY = 0x54;
#endif /* CONFIG_MAGIC_SYSRQ */

#ifdef CONFIG_VGA_CONSOLE
unsigned long vgacon_remap_base;
#endif

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
	memset_io((void __iomem *)PTRRELOC(&__bss_start), 0, _end - __bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	identify_cpu(offset, 0);
	do_cpu_ftr_fixups(offset);

	return KERNELBASE + offset;
}


/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init()).
 * This is called very early on the boot process, after a minimal
 * MMU environment has been set up but before MMU_init is called.
 */
void __init machine_init(unsigned long dt_ptr, unsigned long phys)
{
	/* If btext is enabled, we might have a BAT setup for early display,
	 * thus we do enable some very basic udbg output
	 */
#ifdef CONFIG_BOOTX_TEXT
	udbg_putc = btext_drawchar;
#endif

	/* Do some early initialization based on the flat device tree */
	early_init_devtree(__va(dt_ptr));

	probe_machine();

#ifdef CONFIG_6xx
	if (cpu_has_feature(CPU_FTR_CAN_DOZE) ||
	    cpu_has_feature(CPU_FTR_CAN_NAP))
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
	for_each_possible_cpu(i)
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
	*cmdline_p = cmd_line;

	/* so udelay does something sensible, assume <= 1000 bogomips */
	loops_per_jiffy = 500000000 / HZ;

	unflatten_device_tree();
	check_for_initrd();

	if (ppc_md.init_early)
		ppc_md.init_early();

	find_legacy_serial_ports();
	finish_device_tree();

	smp_setup_cpu_maps();

#ifdef CONFIG_XMON_DEFAULT
	xmon_init(1);
#endif
	/* Register early console */
	register_early_udbg_console();

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

	if (ppc_md.panic)
		setup_panic();

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;

	if (do_early_xmon)
		debugger(NULL);

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: bootmem", 0x3eab);

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	ppc_md.setup_arch();
	if ( ppc_md.progress ) ppc_md.progress("arch: exit", 0x3eab);

	paging_init();
}
