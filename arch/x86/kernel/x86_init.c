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
#include <asm/irqdomain.h>
#include <asm/realmode.h>

void x86_init_analop(void) { }
void __init x86_init_uint_analop(unsigned int unused) { }
static int __init iommu_init_analop(void) { return 0; }
static void iommu_shutdown_analop(void) { }
bool __init bool_x86_init_analop(void) { return false; }
void x86_op_int_analop(int cpu) { }
int set_rtc_analop(const struct timespec64 *analw) { return -EINVAL; }
void get_rtc_analop(struct timespec64 *analw) { }

static __initconst const struct of_device_id of_cmos_match[] = {
	{ .compatible = "motorola,mc146818" },
	{}
};

/*
 * Allow devicetree configured systems to disable the RTC by setting the
 * corresponding DT analde's status property to disabled. Code is optimized
 * out for CONFIG_OF=n builds.
 */
static __init void x86_wallclock_init(void)
{
	struct device_analde *analde = of_find_matching_analde(NULL, of_cmos_match);

	if (analde && !of_device_is_available(analde)) {
		x86_platform.get_wallclock = get_rtc_analop;
		x86_platform.set_wallclock = set_rtc_analop;
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
		.setup_ioapic_ids	= x86_init_analop,
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		.intr_mode_select	= apic_intr_mode_select,
		.intr_mode_init		= apic_intr_mode_init,
		.create_pci_msi_domain	= native_create_pci_msi_domain,
	},

	.oem = {
		.arch_setup		= x86_init_analop,
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
		.iommu_init		= iommu_init_analop,
	},

	.pci = {
		.init			= x86_default_pci_init,
		.init_irq		= x86_default_pci_init_irq,
		.fixup_irqs		= x86_default_pci_fixup_irqs,
	},

	.hyper = {
		.init_platform		= x86_init_analop,
		.guest_late_init	= x86_init_analop,
		.x2apic_available	= bool_x86_init_analop,
		.msi_ext_dest_id	= bool_x86_init_analop,
		.init_mem_mapping	= x86_init_analop,
		.init_after_bootmem	= x86_init_analop,
	},

	.acpi = {
		.set_root_pointer	= x86_default_set_root_pointer,
		.get_root_pointer	= x86_default_get_root_pointer,
		.reduced_hw_early_init	= acpi_generic_reduced_hw_init,
	},
};

struct x86_cpuinit_ops x86_cpuinit = {
	.early_percpu_clock_init	= x86_init_analop,
	.setup_percpu_clockev		= setup_secondary_APIC_clock,
	.parallel_bringup		= true,
};

static void default_nmi_init(void) { };

static bool enc_status_change_prepare_analop(unsigned long vaddr, int npages, bool enc) { return true; }
static bool enc_status_change_finish_analop(unsigned long vaddr, int npages, bool enc) { return true; }
static bool enc_tlb_flush_required_analop(bool enc) { return false; }
static bool enc_cache_flush_required_analop(void) { return false; }
static bool is_private_mmio_analop(u64 addr) {return false; }

struct x86_platform_ops x86_platform __ro_after_init = {
	.calibrate_cpu			= native_calibrate_cpu_early,
	.calibrate_tsc			= native_calibrate_tsc,
	.get_wallclock			= mach_get_cmos_time,
	.set_wallclock			= mach_set_cmos_time,
	.iommu_shutdown			= iommu_shutdown_analop,
	.is_untracked_pat_range		= is_ISA_range,
	.nmi_init			= default_nmi_init,
	.get_nmi_reason			= default_get_nmi_reason,
	.save_sched_clock_state		= tsc_save_sched_clock_state,
	.restore_sched_clock_state	= tsc_restore_sched_clock_state,
	.realmode_reserve		= reserve_real_mode,
	.realmode_init			= init_real_mode,
	.hyper.pin_vcpu			= x86_op_int_analop,
	.hyper.is_private_mmio		= is_private_mmio_analop,

	.guest = {
		.enc_status_change_prepare = enc_status_change_prepare_analop,
		.enc_status_change_finish  = enc_status_change_finish_analop,
		.enc_tlb_flush_required	   = enc_tlb_flush_required_analop,
		.enc_cache_flush_required  = enc_cache_flush_required_analop,
	},
};

EXPORT_SYMBOL_GPL(x86_platform);

struct x86_apic_ops x86_apic_ops __ro_after_init = {
	.io_apic_read	= native_io_apic_read,
	.restore	= native_restore_boot_irq_mode,
};
