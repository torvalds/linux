/*
 * Copyright (C) 2009 Thomas Gleixner <tglx@linutronix.de>
 *
 *  For licencing details see kernel-base/COPYING
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/pci.h>

#include <asm/acpi.h>
#include <asm/bios_ebda.h>
#include <asm/paravirt.h>
#include <asm/pci_x86.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820/api.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/io_apic.h>
#include <asm/hpet.h>
#include <asm/memtype.h>
#include <asm/tsc.h>
#include <asm/iommu.h>
#include <asm/mach_traps.h>

void x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }
static int __init iommu_init_noop(void) { return 0; }
static void iommu_shutdown_noop(void) { }
bool __init bool_x86_init_noop(void) { return false; }
void x86_op_int_noop(int cpu) { }
static __init int set_rtc_noop(const struct timespec64 *now) { return -EINVAL; }
static __init void get_rtc_noop(struct timespec64 *now) { }

static __initconst const struct of_device_id of_cmos_match[] = {
	{ .compatible = "motorola,mc146818" },
	{}
};

/*
 * Allow devicetree configured systems to disable the RTC by setting the
 * corresponding DT node's status property to disabled. Code is optimized
 * out for CONFIG_OF=n builds.
 */
static __init void x86_wallclock_init(void)
{
	struct device_node *node = of_find_matching_node(NULL, of_cmos_match);

	if (node && !of_device_is_available(node)) {
		x86_platform.get_wallclock = get_rtc_noop;
		x86_platform.set_wallclock = set_rtc_noop;
	}
}

/*
 * The platform setup functions are preset with the default functions
 * for standard PC hardware.
 */
struct x86_init_ops x86_init __initdata = {

	.resources = {
		.probe_roms		= probe_roms,
		.reserve_resources	= reserve_standard_io_resources,
		.memory_setup		= e820__memory_setup_default,
	},

	.mpparse = {
		.mpc_record		= x86_init_uint_noop,
		.setup_ioapic_ids	= x86_init_noop,
		.mpc_apic_id		= default_mpc_apic_id,
		.smp_read_mpc_oem	= default_smp_read_mpc_oem,
		.mpc_oem_bus_info	= default_mpc_oem_bus_info,
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		.trap_init		= x86_init_noop,
		.intr_mode_select	= apic_intr_mode_select,
		.intr_mode_init		= apic_intr_mode_init
	},

	.oem = {
		.arch_setup		= x86_init_noop,
		.banner			= default_banner,
	},

	.paging = {
		.pagetable_init		= native_pagetable_init,
	},

	.timers = {
		.setup_percpu_clockev	= setup_boot_APIC_clock,
		.timer_init		= hpet_time_init,
		.wallclock_init		= x86_wallclock_init,
	},

	.iommu = {
		.iommu_init		= iommu_init_noop,
	},

	.pci = {
		.init			= x86_default_pci_init,
		.init_irq		= x86_default_pci_init_irq,
		.fixup_irqs		= x86_default_pci_fixup_irqs,
	},

	.hyper = {
		.init_platform		= x86_init_noop,
		.guest_late_init	= x86_init_noop,
		.x2apic_available	= bool_x86_init_noop,
		.init_mem_mapping	= x86_init_noop,
		.init_after_bootmem	= x86_init_noop,
	},

	.acpi = {
		.set_root_pointer	= x86_default_set_root_pointer,
		.get_root_pointer	= x86_default_get_root_pointer,
		.reduced_hw_early_init	= acpi_generic_reduced_hw_init,
	},
};

struct x86_cpuinit_ops x86_cpuinit = {
	.early_percpu_clock_init	= x86_init_noop,
	.setup_percpu_clockev		= setup_secondary_APIC_clock,
};

static void default_nmi_init(void) { };

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu			= native_calibrate_cpu_early,
	.calibrate_tsc			= native_calibrate_tsc,
	.get_wallclock			= mach_get_cmos_time,
	.set_wallclock			= mach_set_rtc_mmss,
	.iommu_shutdown			= iommu_shutdown_noop,
	.is_untracked_pat_range		= is_ISA_range,
	.nmi_init			= default_nmi_init,
	.get_nmi_reason			= default_get_nmi_reason,
	.save_sched_clock_state 	= tsc_save_sched_clock_state,
	.restore_sched_clock_state 	= tsc_restore_sched_clock_state,
	.hyper.pin_vcpu			= x86_op_int_noop,
};

EXPORT_SYMBOL_GPL(x86_platform);

#if defined(CONFIG_PCI_MSI)
struct x86_msi_ops x86_msi __ro_after_init = {
	.setup_msi_irqs		= native_setup_msi_irqs,
	.teardown_msi_irq	= native_teardown_msi_irq,
	.teardown_msi_irqs	= default_teardown_msi_irqs,
	.restore_msi_irqs	= default_restore_msi_irqs,
};

/* MSI arch specific hooks */
int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	return x86_msi.setup_msi_irqs(dev, nvec, type);
}

void arch_teardown_msi_irqs(struct pci_dev *dev)
{
	x86_msi.teardown_msi_irqs(dev);
}

void arch_teardown_msi_irq(unsigned int irq)
{
	x86_msi.teardown_msi_irq(irq);
}

void arch_restore_msi_irqs(struct pci_dev *dev)
{
	x86_msi.restore_msi_irqs(dev);
}
#endif

struct x86_apic_ops x86_apic_ops __ro_after_init = {
	.io_apic_read	= native_io_apic_read,
	.restore	= native_restore_boot_irq_mode,
};
