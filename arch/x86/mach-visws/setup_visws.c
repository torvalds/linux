/*
 *  Unmaintained SGI Visual Workstation support.
 *  Split out from setup.c by davej@suse.de
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/arch_hooks.h>
#include <asm/fixmap.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/e820.h>
#include <asm/smp.h>
#include <asm/io.h>

#include <mach_ipi.h>

#include "cobalt.h"
#include "piix4.h"
#include "mach_apic.h"

#include <linux/init.h>
#include <linux/smp.h>

char visws_board_type	= -1;
char visws_board_rev	= -1;

int is_visws_box(void)
{
	return visws_board_type >= 0;
}

static int __init visws_time_init_quirk(void)
{
	printk(KERN_INFO "Starting Cobalt Timer system clock\n");

	/* Set the countdown value */
	co_cpu_write(CO_CPU_TIMEVAL, CO_TIME_HZ/HZ);

	/* Start the timer */
	co_cpu_write(CO_CPU_CTRL, co_cpu_read(CO_CPU_CTRL) | CO_CTRL_TIMERUN);

	/* Enable (unmask) the timer interrupt */
	co_cpu_write(CO_CPU_CTRL, co_cpu_read(CO_CPU_CTRL) & ~CO_CTRL_TIMEMASK);

	/*
	 * Zero return means the generic timer setup code will set up
	 * the standard vector:
	 */
	return 0;
}

static int __init visws_pre_intr_init_quirk(void)
{
	init_VISWS_APIC_irqs();

	/*
	 * We dont want ISA irqs to be set up by the generic code:
	 */
	return 1;
}

/* Quirk for machine specific memory setup. */

#define MB (1024 * 1024)

unsigned long sgivwfb_mem_phys;
unsigned long sgivwfb_mem_size;
EXPORT_SYMBOL(sgivwfb_mem_phys);
EXPORT_SYMBOL(sgivwfb_mem_size);

long long mem_size __initdata = 0;

static char * __init visws_memory_setup_quirk(void)
{
	long long gfx_mem_size = 8 * MB;

	mem_size = boot_params.alt_mem_k;

	if (!mem_size) {
		printk(KERN_WARNING "Bootloader didn't set memory size, upgrade it !\n");
		mem_size = 128 * MB;
	}

	/*
	 * this hardcodes the graphics memory to 8 MB
	 * it really should be sized dynamically (or at least
	 * set as a boot param)
	 */
	if (!sgivwfb_mem_size) {
		printk(KERN_WARNING "Defaulting to 8 MB framebuffer size\n");
		sgivwfb_mem_size = 8 * MB;
	}

	/*
	 * Trim to nearest MB
	 */
	sgivwfb_mem_size &= ~((1 << 20) - 1);
	sgivwfb_mem_phys = mem_size - gfx_mem_size;

	e820_add_region(0, LOWMEMSIZE(), E820_RAM);
	e820_add_region(HIGH_MEMORY, mem_size - sgivwfb_mem_size - HIGH_MEMORY, E820_RAM);
	e820_add_region(sgivwfb_mem_phys, sgivwfb_mem_size, E820_RESERVED);

	return "PROM";
}

static void visws_machine_emergency_restart(void)
{
	/*
	 * Visual Workstations restart after this
	 * register is poked on the PIIX4
	 */
	outb(PIIX4_RESET_VAL, PIIX4_RESET_PORT);
}

static void visws_machine_power_off(void)
{
	unsigned short pm_status;
/*	extern unsigned int pci_bus0; */

	while ((pm_status = inw(PMSTS_PORT)) & 0x100)
		outw(pm_status, PMSTS_PORT);

	outw(PM_SUSPEND_ENABLE, PMCNTRL_PORT);

	mdelay(10);

#define PCI_CONF1_ADDRESS(bus, devfn, reg) \
	(0x80000000 | (bus << 16) | (devfn << 8) | (reg & ~3))

/*	outl(PCI_CONF1_ADDRESS(pci_bus0, SPECIAL_DEV, SPECIAL_REG), 0xCF8); */
	outl(PIIX_SPECIAL_STOP, 0xCFC);
}

static int __init visws_get_smp_config_quirk(unsigned int early)
{
	/*
	 * Prevent MP-table parsing by the generic code:
	 */
	return 1;
}

extern unsigned int __cpuinitdata maxcpus;

/*
 * The Visual Workstation is Intel MP compliant in the hardware
 * sense, but it doesn't have a BIOS(-configuration table).
 * No problem for Linux.
 */

static void __init MP_processor_info (struct mpc_config_processor *m)
{
	int ver, logical_apicid;
	physid_mask_t apic_cpus;

	if (!(m->mpc_cpuflag & CPU_ENABLED))
		return;

	logical_apicid = m->mpc_apicid;
	printk(KERN_INFO "%sCPU #%d %u:%u APIC version %d\n",
	       m->mpc_cpuflag & CPU_BOOTPROCESSOR ? "Bootup " : "",
	       m->mpc_apicid,
	       (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	       (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	       m->mpc_apicver);

	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR)
		boot_cpu_physical_apicid = m->mpc_apicid;

	ver = m->mpc_apicver;
	if ((ver >= 0x14 && m->mpc_apicid >= 0xff) || m->mpc_apicid >= 0xf) {
		printk(KERN_ERR "Processor #%d INVALID. (Max ID: %d).\n",
			m->mpc_apicid, MAX_APICS);
		return;
	}

	apic_cpus = apicid_to_cpu_present(m->mpc_apicid);
	physids_or(phys_cpu_present_map, phys_cpu_present_map, apic_cpus);
	/*
	 * Validate version
	 */
	if (ver == 0x0) {
		printk(KERN_ERR "BIOS bug, APIC version is 0 for CPU#%d! "
			"fixing up to 0x10. (tell your hw vendor)\n",
			m->mpc_apicid);
		ver = 0x10;
	}
	apic_version[m->mpc_apicid] = ver;
}

int __init visws_find_smp_config_quirk(unsigned int reserve)
{
	struct mpc_config_processor *mp = phys_to_virt(CO_CPU_TAB_PHYS);
	unsigned short ncpus = readw(phys_to_virt(CO_CPU_NUM_PHYS));

	if (ncpus > CO_CPU_MAX) {
		printk(KERN_WARNING "find_visws_smp: got cpu count of %d at %p\n",
			ncpus, mp);

		ncpus = CO_CPU_MAX;
	}

	if (ncpus > maxcpus)
		ncpus = maxcpus;

#ifdef CONFIG_X86_LOCAL_APIC
	smp_found_config = 1;
#endif
	while (ncpus--)
		MP_processor_info(mp++);

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	return 1;
}

extern int visws_trap_init_quirk(void);

void __init visws_early_detect(void)
{
	int raw;

	visws_board_type = (char)(inb_p(PIIX_GPI_BD_REG) & PIIX_GPI_BD_REG)
							 >> PIIX_GPI_BD_SHIFT;

	if (visws_board_type < 0)
		return;

	/*
	 * Install special quirks for timer, interrupt and memory setup:
	 */
	arch_time_init_quirk		= visws_time_init_quirk;
	arch_pre_intr_init_quirk	= visws_pre_intr_init_quirk;
	arch_memory_setup_quirk		= visws_memory_setup_quirk;

	/*
	 * Fall back to generic behavior for traps:
	 */
	arch_intr_init_quirk		= NULL;
	arch_trap_init_quirk		= visws_trap_init_quirk;

	/*
	 * Install reboot quirks:
	 */
	pm_power_off			= visws_machine_power_off;
	machine_ops.emergency_restart	= visws_machine_emergency_restart;

	/*
	 * Do not use broadcast IPIs:
	 */
	no_broadcast = 0;

	/*
	 * Override generic MP-table parsing:
	 */
	mach_get_smp_config_quirk	= visws_get_smp_config_quirk;
	mach_find_smp_config_quirk	= visws_find_smp_config_quirk;

	/*
	 * Get Board rev.
	 * First, we have to initialize the 307 part to allow us access
	 * to the GPIO registers.  Let's map them at 0x0fc0 which is right
	 * after the PIIX4 PM section.
	 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_GP_DEV, SIO_DATA);	/* Talk to GPIO regs. */

	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_GP_MSB, SIO_DATA);	/* MSB of GPIO base address */

	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_GP_LSB, SIO_DATA);	/* LSB of GPIO base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable GPIO registers. */

	/*
	 * Now, we have to map the power management section to write
	 * a bit which enables access to the GPIO registers.
	 * What lunatic came up with this shit?
	 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_PM_DEV, SIO_DATA);	/* Talk to GPIO regs. */

	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_PM_MSB, SIO_DATA);	/* MSB of PM base address */

	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_PM_LSB, SIO_DATA);	/* LSB of PM base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable PM registers. */

	/*
	 * Now, write the PM register which enables the GPIO registers.
	 */
	outb_p(SIO_PM_FER2, SIO_PM_INDEX);
	outb_p(SIO_PM_GP_EN, SIO_PM_DATA);

	/*
	 * Now, initialize the GPIO registers.
	 * We want them all to be inputs which is the
	 * power on default, so let's leave them alone.
	 * So, let's just read the board rev!
	 */
	raw = inb_p(SIO_GP_DATA1);
	raw &= 0x7f;	/* 7 bits of valid board revision ID. */

	if (visws_board_type == VISWS_320) {
		if (raw < 0x6) {
			visws_board_rev = 4;
		} else if (raw < 0xc) {
			visws_board_rev = 5;
		} else {
			visws_board_rev = 6;
		}
	} else if (visws_board_type == VISWS_540) {
			visws_board_rev = 2;
		} else {
			visws_board_rev = raw;
		}

	printk(KERN_INFO "Silicon Graphics Visual Workstation %s (rev %d) detected\n",
	       (visws_board_type == VISWS_320 ? "320" :
	       (visws_board_type == VISWS_540 ? "540" :
		"unknown")), visws_board_rev);
}
