/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

#include <asm/bootparam.h>

struct ghcb;
struct mpc_bus;
struct mpc_cpu;
struct pt_regs;
struct mpc_table;
struct cpuinfo_x86;
struct irq_domain;

/**
 * struct x86_init_mpparse - platform specific mpparse ops
 * @setup_ioapic_ids:		platform specific ioapic id override
 * @find_smp_config:		find the smp configuration
 * @get_smp_config:		get the smp configuration
 */
struct x86_init_mpparse {
	void (*setup_ioapic_ids)(void);
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
 * @intr_mode_select:		interrupt delivery mode selection
 * @intr_mode_init:		interrupt delivery mode setup
 * @create_pci_msi_domain:	Create the PCI/MSI interrupt domain
 */
struct x86_init_irqs {
	void (*pre_vector_init)(void);
	void (*intr_init)(void);
	void (*intr_mode_select)(void);
	void (*intr_mode_init)(void);
	struct irq_domain *(*create_pci_msi_domain)(void);
};

/**
 * struct x86_init_oem - oem platform specific customizing functions
 * @arch_setup:			platform specific architecture setup
 * @banner:			print a platform specific banner
 */
struct x86_init_oem {
	void (*arch_setup)(void);
	void (*banner)(void);
};

/**
 * struct x86_init_paging - platform specific paging functions
 * @pagetable_init:	platform specific paging initialization call to setup
 *			the kernel pagetables and prepare accessors functions.
 *			Callback must call paging_init(). Called once after the
 *			direct mapping for phys memory is available.
 */
struct x86_init_paging {
	void (*pagetable_init)(void);
};

/**
 * struct x86_init_timers - platform specific timer setup
 * @setup_perpcu_clockev:	set up the per cpu clock event device for the
 *				boot cpu
 * @timer_init:			initialize the platform timer (default PIT/HPET)
 * @wallclock_init:		init the wallclock device
 */
struct x86_init_timers {
	void (*setup_percpu_clockev)(void);
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
 * struct x86_hyper_init - x86 hypervisor init functions
 * @init_platform:		platform setup
 * @guest_late_init:		guest late init
 * @x2apic_available:		X2APIC detection
 * @msi_ext_dest_id:		MSI supports 15-bit APIC IDs
 * @init_mem_mapping:		setup early mappings during init_mem_mapping()
 * @init_after_bootmem:		guest init after boot allocator is finished
 */
struct x86_hyper_init {
	void (*init_platform)(void);
	void (*guest_late_init)(void);
	bool (*x2apic_available)(void);
	bool (*msi_ext_dest_id)(void);
	void (*init_mem_mapping)(void);
	void (*init_after_bootmem)(void);
};

/**
 * struct x86_init_acpi - x86 ACPI init functions
 * @set_root_poitner:		set RSDP address
 * @get_root_pointer:		get RSDP address
 * @reduced_hw_early_init:	hardware reduced platform early init
 */
struct x86_init_acpi {
	void (*set_root_pointer)(u64 addr);
	u64 (*get_root_pointer)(void);
	void (*reduced_hw_early_init)(void);
};

/**
 * struct x86_guest - Functions used by misc guest incarnations like SEV, TDX, etc.
 *
 * @enc_status_change_prepare	Notify HV before the encryption status of a range is changed
 * @enc_status_change_finish	Notify HV after the encryption status of a range is changed
 * @enc_tlb_flush_required	Returns true if a TLB flush is needed before changing page encryption status
 * @enc_cache_flush_required	Returns true if a cache flush is needed before changing page encryption status
 */
struct x86_guest {
	bool (*enc_status_change_prepare)(unsigned long vaddr, int npages, bool enc);
	bool (*enc_status_change_finish)(unsigned long vaddr, int npages, bool enc);
	bool (*enc_tlb_flush_required)(bool enc);
	bool (*enc_cache_flush_required)(void);
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
	struct x86_init_paging		paging;
	struct x86_init_timers		timers;
	struct x86_init_iommu		iommu;
	struct x86_init_pci		pci;
	struct x86_hyper_init		hyper;
	struct x86_init_acpi		acpi;
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

struct timespec64;

/**
 * struct x86_legacy_devices - legacy x86 devices
 *
 * @pnpbios: this platform can have a PNPBIOS. If this is disabled the platform
 * 	is known to never have a PNPBIOS.
 *
 * These are devices known to require LPC or ISA bus. The definition of legacy
 * devices adheres to the ACPI 5.2.9.3 IA-PC Boot Architecture flag
 * ACPI_FADT_LEGACY_DEVICES. These devices consist of user visible devices on
 * the LPC or ISA bus. User visible devices are devices that have end-user
 * accessible connectors (for example, LPT parallel port). Legacy devices on
 * the LPC bus consist for example of serial and parallel ports, PS/2 keyboard
 * / mouse, and the floppy disk controller. A system that lacks all known
 * legacy devices can assume all devices can be detected exclusively via
 * standard device enumeration mechanisms including the ACPI namespace.
 *
 * A system which has does not have ACPI_FADT_LEGACY_DEVICES enabled must not
 * have any of the legacy devices enumerated below present.
 */
struct x86_legacy_devices {
	int pnpbios;
};

/**
 * enum x86_legacy_i8042_state - i8042 keyboard controller state
 * @X86_LEGACY_I8042_PLATFORM_ABSENT: the controller is always absent on
 *	given platform/subarch.
 * @X86_LEGACY_I8042_FIRMWARE_ABSENT: firmware reports that the controller
 *	is absent.
 * @X86_LEGACY_i8042_EXPECTED_PRESENT: the controller is likely to be
 *	present, the i8042 driver should probe for controller existence.
 */
enum x86_legacy_i8042_state {
	X86_LEGACY_I8042_PLATFORM_ABSENT,
	X86_LEGACY_I8042_FIRMWARE_ABSENT,
	X86_LEGACY_I8042_EXPECTED_PRESENT,
};

/**
 * struct x86_legacy_features - legacy x86 features
 *
 * @i8042: indicated if we expect the device to have i8042 controller
 *	present.
 * @rtc: this device has a CMOS real-time clock present
 * @reserve_bios_regions: boot code will search for the EBDA address and the
 * 	start of the 640k - 1M BIOS region.  If false, the platform must
 * 	ensure that its memory map correctly reserves sub-1MB regions as needed.
 * @devices: legacy x86 devices, refer to struct x86_legacy_devices
 * 	documentation for further details.
 */
struct x86_legacy_features {
	enum x86_legacy_i8042_state i8042;
	int rtc;
	int warm_reset;
	int no_vga;
	int reserve_bios_regions;
	struct x86_legacy_devices devices;
};

/**
 * struct x86_hyper_runtime - x86 hypervisor specific runtime callbacks
 *
 * @pin_vcpu:			pin current vcpu to specified physical
 *				cpu (run rarely)
 * @sev_es_hcall_prepare:	Load additional hypervisor-specific
 *				state into the GHCB when doing a VMMCALL under
 *				SEV-ES. Called from the #VC exception handler.
 * @sev_es_hcall_finish:	Copies state from the GHCB back into the
 *				processor (or pt_regs). Also runs checks on the
 *				state returned from the hypervisor after a
 *				VMMCALL under SEV-ES.  Needs to return 'false'
 *				if the checks fail.  Called from the #VC
 *				exception handler.
 */
struct x86_hyper_runtime {
	void (*pin_vcpu)(int cpu);
	void (*sev_es_hcall_prepare)(struct ghcb *ghcb, struct pt_regs *regs);
	bool (*sev_es_hcall_finish)(struct ghcb *ghcb, struct pt_regs *regs);
};

/**
 * struct x86_platform_ops - platform specific runtime functions
 * @calibrate_cpu:		calibrate CPU
 * @calibrate_tsc:		calibrate TSC, if different from CPU
 * @get_wallclock:		get time from HW clock like RTC etc.
 * @set_wallclock:		set time back to HW clock
 * @is_untracked_pat_range	exclude from PAT logic
 * @nmi_init			enable NMI on cpus
 * @save_sched_clock_state:	save state for sched_clock() on suspend
 * @restore_sched_clock_state:	restore state for sched_clock() on resume
 * @apic_post_init:		adjust apic if needed
 * @legacy:			legacy features
 * @set_legacy_features:	override legacy features. Use of this callback
 * 				is highly discouraged. You should only need
 * 				this if your hardware platform requires further
 * 				custom fine tuning far beyond what may be
 * 				possible in x86_early_init_platform_quirks() by
 * 				only using the current x86_hardware_subarch
 * 				semantics.
 * @realmode_reserve:		reserve memory for realmode trampoline
 * @realmode_init:		initialize realmode trampoline
 * @hyper:			x86 hypervisor specific runtime callbacks
 */
struct x86_platform_ops {
	unsigned long (*calibrate_cpu)(void);
	unsigned long (*calibrate_tsc)(void);
	void (*get_wallclock)(struct timespec64 *ts);
	int (*set_wallclock)(const struct timespec64 *ts);
	void (*iommu_shutdown)(void);
	bool (*is_untracked_pat_range)(u64 start, u64 end);
	void (*nmi_init)(void);
	unsigned char (*get_nmi_reason)(void);
	void (*save_sched_clock_state)(void);
	void (*restore_sched_clock_state)(void);
	void (*apic_post_init)(void);
	struct x86_legacy_features legacy;
	void (*set_legacy_features)(void);
	void (*realmode_reserve)(void);
	void (*realmode_init)(void);
	struct x86_hyper_runtime hyper;
	struct x86_guest guest;
};

struct x86_apic_ops {
	unsigned int	(*io_apic_read)   (unsigned int apic, unsigned int reg);
	void		(*restore)(void);
};

extern struct x86_init_ops x86_init;
extern struct x86_cpuinit_ops x86_cpuinit;
extern struct x86_platform_ops x86_platform;
extern struct x86_msi_ops x86_msi;
extern struct x86_apic_ops x86_apic_ops;

extern void x86_early_init_platform_quirks(void);
extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);
extern bool bool_x86_init_noop(void);
extern void x86_op_int_noop(int cpu);
extern bool x86_pnpbios_disabled(void);

#endif
