#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

struct mpc_bus;
struct mpc_cpu;
struct mpc_table;

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
	void (*find_smp_config)(unsigned int reserve);
	void (*get_smp_config)(unsigned int early);
};

/**
 * struct x86_init_resources - platform specific resource related ops
 * @probe_roms:			probe BIOS roms
 * @reserve_resources:		reserve the standard resources for the
 *				platform
 * @reserve_ebda_region:	reserve the extended bios data area
 * @memory_setup:		platform specific memory setup
 *
 */
struct x86_init_resources {
	void (*probe_roms)(void);
	void (*reserve_resources)(void);
	void (*reserve_ebda_region)(void);
	char *(*memory_setup)(void);
};

/**
 * struct x86_init_ops - functions for platform specific setup
 *
 */
struct x86_init_ops {
	struct x86_init_resources	resources;
	struct x86_init_mpparse		mpparse;
};

extern struct x86_init_ops x86_init;

extern void x86_init_noop(void);
extern void x86_init_uint_noop(unsigned int unused);

#endif
