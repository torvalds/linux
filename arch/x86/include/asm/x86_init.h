#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/pgtable_types.h>
#include <asm/bootparam.h>

struct mpc_bus;
struct mpc_cpu;
struct mpc_table;
struct cpuinfo_x86;

/**
 * struct x86_init_mpparse - platform specific mpparse ops
 * @mpc_record:			platform specific mpc record accounting
 * @setup_ioapic_ids:		platform specific ioapic id override
 * @mpc_apic_id:		platform specific mpc apic id assignment
 * @smp_read_mpc_oem:		platform specific oem mpc table setup
 * @mpc_oem_pci_bus:		platform specific pci bus setup (default NULL)
 * @mpc_oem_bus_info:		platform specific mpc bus info
 * @find_smp_config:		find the smp configuration
 * @get_smp_config:		get the smp configuration
 */
struct x86_init_mpparse {
	void (*mpc_record)(unsigned int mode);
	void (*setup_ioapic_ids)(void);
	int (*mpc_apic_id)(struct mpc_cpu *m);
	void (*smp_read_mpc_oem)(struct mpc_table *mpc);
	void (*mpc_oem_pci_bus)(struct mpc_bus *m);
	void (*mpc_oem_bus_info)(struct mpc_bus *m, char *name);
	void (*find_smp_config)(void);
	void (*get_smp_config)(unsigned int early);
};

/**
 * struct x86_init_resources - platform specific resource related ops
 * @probe_roms:			probe BIOS roms
 * @reserve_resources:		reserve the standard resources for the
 *				platform
 * @memory_setup:		platform specific memory setup
 *
 */
struct x86_init_resources {
	void (*probe_roms)(void);
	void (*reserve_resources)(void);
	char *(*memory_setup)(void);
};

/**
 * struct x86_init_irqs - platform specific interrupt setup
 * @pre_vector_init:		init code to run before interrupt vectors
 *				are set up.
 * @intr_init:			interrupt init code
 * @trap_init:			platform specific trap setup
 */
struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
	void (*trap_init)(void);
};

/**
 * struct x86_init_oem - oem platform specific customizing functions
 * @arch_setup:			platform specific architecure setup
 * @banner:			print a platform specific banner
 */
struct x86_init_oem {
	void (*arch_setup)(void);
	void (*banner)(void);
};

/**
 * struct x86_init_mapping - platform specific initial kernel pagetable setup
 * @pagetable_reserve:	reserve a range of addresses for kernel pagetable usage
 *
 * For more details on the purpose of this hook, look in
 * init_memory_mapping and the commit that added it.
 */
struct x86_init_mapping {
	void (*pagetable_reserve)(u64 start, u64 end);
};

/**
 * struct x86_init_paging - platform specific paging functions
 * @pagetable_setup_start:	platform specific pre paging_init() call
 * @pagetable_setup_done:	platform specific post paging_init() call
 */
struct x86_init_paging {
	void (*pagetable_setup_start)(pgd_t *base);
	void (*pagetable_setup_done)(pgd_t *base);
};

/**
 * struct x86_init_timers - platform specific timer setup
 * @setup_perpcu_clockev:	set up the per cpu clock event device for the
 *				boot cpu
 * @tsc_pre_init:		platform function called before TSC init
 * @timer_init:			initialize the platform timer (default PIT/HPET)
 * @wallclock_init:		init the wallclock device
 */
struct x86_init_timers {
	void (*setup_percpu_clockev)(void);
	void (*tsc_pre_init)(void);
	void (*timer_init)(void);
	void (*wallclock_init)(void);
};

/**
 * struct x86_init_iommu - platform specific iommu setup
 * @iommu_init:			platform specific iommu setup
 */
struct x86_init_iommu {
	int (*iommu_init)(void);
};

/**
 * struct x86_init_pci - platform specific pci init functions
 * @arch_init:			platform specific pci arch init call
 * @init:			platform specific pci subsystem init
 * @init_irq:			platform specific pci irq init
 * @fixup_irqs:			platform specific pci irq fixup
 */
struct x86_init_pci {
	int (*arch_init)(void);
	int (*init)(void);
	void (*init_irq)(void);
	void (*fixup_irqs)(void);
};

/**
 * struct x86_init_ops - functions for platform specific setup
 *
 */
struct x86_init_ops {
	struct x86_init_resources	resources;
	struct x86_init_mpparse		mpparse;
	struct x86_init_irqs		irqs;
	struct x86_init_oem		oem;
	struct x86_init_mapping		mapping;
	struct x86_init_paging		paging;
	struct x86_init_timers		timers;
	struct x86_init_iommu		iommu;
	struct x86_init_pci		pci;
};

/**
 * struct x86_cpuinit_ops - platform specific cpu hotplug setups
 * @setup_percpu_clockev:	set up the per cpu clock event device
 * @early_percpu_clock_init:	early init of the per cpu clock event device
 */
struct x86_cpuinit_ops {
	void (*setup_percpu_clockev)(void);
	void (*early_percpu_clock_init)(void);
	void (*fixup_cpu_id)(struct cpuinfo_x86 *c, int node);
};

/**
 * struct x86_platform_ops - platform specific runtime functions
 * @calibrate_tsc:		calibrate TSC
 * @get_wallclock:		get time from HW clock like RTC etc.
 * @set_wallclock:		set time back to HW clock
 * @is_untracked_pat_range	exclude from PAT logic
 * @nmi_init			enable NMI on cpus
 * @i8042_detect		pre-detect if i8042 controller exists
 * @save_sched_clock_state:	save state for sched_clock() on suspend
 * @restore_sched_clock_state:	restore state for sched_clock() on resume
 * @apic_post_init:		adjust apic if neeeded
 */
struct x86_platform_ops {
	unsigned long (*calibrate_tsc)(void);
	unsigned long (*get_wallclock)(void);
	int (*set_wallclock)(unsigned long nowtime);
	void (*iommu_shutdown)(void);
	bool (*is_untracked_pat_range)(u64 start, u64 end);
	void (*nmi_init)(void);
	unsigned char (*get_nmi_reason)(void);
	int (*i8042_detect)(void);
	void (*save_sched_clock_state)(void);
	void (*restore_sched_clock_state)(void);
	void (*apic_post_init)(void);
};

struct pci_dev;

struct x86_msi_ops {
	int (*setup_msi_irqs)(struct pci_dev *dev, int nvec, int type);
	void (*teardown_msi_irq)(unsigned int irq);
	void (*teardown_msi_irqs)(struct pci_dev *dev);
	void (*restore_msi_irqs)(struct pci_dev *dev, int irq);
};

struct x86_io_apic_ops {
	void		(*init)  (void);
	unsigned int	(*read)  (unsigned int apic, unsigned int reg);
	void		(*write) (unsigned int apic, unsigned int reg, unsigned int value);
	void		(*modify)(unsigned int apic, unsigned int reg, unsigned int value);
};

extern struct x86_init_ops x86_init;
extern struct x86_cpuinit_ops x86_cpuinit;
extern struct x86_platform_ops x86_platform;
extern struct x86_msi_ops x86_msi;
extern struct x86_io_apic_ops x86_io_apic_ops;
extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);

#endif
