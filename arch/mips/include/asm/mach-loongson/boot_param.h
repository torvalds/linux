#ifndef __ASM_MACH_LOONGSON_BOOT_PARAM_H_
#define __ASM_MACH_LOONGSON_BOOT_PARAM_H_

#define SYSTEM_RAM_LOW		1
#define SYSTEM_RAM_HIGH		2
#define MEM_RESERVED		3
#define PCI_IO			4
#define PCI_MEM			5
#define LOONGSON_CFG_REG	6
#define VIDEO_ROM		7
#define ADAPTER_ROM		8
#define ACPI_TABLE		9
#define MAX_MEMORY_TYPE		10

#define LOONGSON3_BOOT_MEM_MAP_MAX 128
struct efi_memory_map_loongson {
	u16 vers;	/* version of efi_memory_map */
	u32 nr_map;	/* number of memory_maps */
	u32 mem_freq;	/* memory frequence */
	struct mem_map {
		u32 node_id;	/* node_id which memory attached to */
		u32 mem_type;	/* system memory, pci memory, pci io, etc. */
		u64 mem_start;	/* memory map start address */
		u32 mem_size;	/* each memory_map size, not the total size */
	} map[LOONGSON3_BOOT_MEM_MAP_MAX];
} __packed;

enum loongson_cpu_type {
	Loongson_2E = 0,
	Loongson_2F = 1,
	Loongson_3A = 2,
	Loongson_3B = 3,
	Loongson_1A = 4,
	Loongson_1B = 5
};

/*
 * Capability and feature descriptor structure for MIPS CPU
 */
struct efi_cpuinfo_loongson {
	u16 vers;     /* version of efi_cpuinfo_loongson */
	u32 processor_id; /* PRID, e.g. 6305, 6306 */
	u32 cputype;  /* Loongson_3A/3B, etc. */
	u32 total_node;   /* num of total numa nodes */
	u32 cpu_startup_core_id; /* Core id */
	u32 cpu_clock_freq; /* cpu_clock */
	u32 nr_cpus;
} __packed;

struct system_loongson {
	u16 vers;     /* version of system_loongson */
	u32 ccnuma_smp; /* 0: no numa; 1: has numa */
	u32 sing_double_channel; /* 1:single; 2:double */
} __packed;

struct irq_source_routing_table {
	u16 vers;
	u16 size;
	u16 rtr_bus;
	u16 rtr_devfn;
	u32 vendor;
	u32 device;
	u32 PIC_type;   /* conform use HT or PCI to route to CPU-PIC */
	u64 ht_int_bit; /* 3A: 1<<24; 3B: 1<<16 */
	u64 ht_enable;  /* irqs used in this PIC */
	u32 node_id;    /* node id: 0x0-0; 0x1-1; 0x10-2; 0x11-3 */
	u64 pci_mem_start_addr;
	u64 pci_mem_end_addr;
	u64 pci_io_start_addr;
	u64 pci_io_end_addr;
	u64 pci_config_addr;
	u32 dma_mask_bits;
} __packed;

struct interface_info {
	u16 vers; /* version of the specificition */
	u16 size;
	u8  flag;
	char description[64];
} __packed;

#define MAX_RESOURCE_NUMBER 128
struct resource_loongson {
	u64 start; /* resource start address */
	u64 end;   /* resource end address */
	char name[64];
	u32 flags;
};

struct archdev_data {};  /* arch specific additions */

struct board_devices {
	char name[64];    /* hold the device name */
	u32 num_resources; /* number of device_resource */
	/* for each device's resource */
	struct resource_loongson resource[MAX_RESOURCE_NUMBER];
	/* arch specific additions */
	struct archdev_data archdata;
};

struct loongson_special_attribute {
	u16 vers;     /* version of this special */
	char special_name[64]; /* special_atribute_name */
	u32 loongson_special_type; /* type of special device */
	/* for each device's resource */
	struct resource_loongson resource[MAX_RESOURCE_NUMBER];
};

struct loongson_params {
	u64 memory_offset;	/* efi_memory_map_loongson struct offset */
	u64 cpu_offset;		/* efi_cpuinfo_loongson struct offset */
	u64 system_offset;	/* system_loongson struct offset */
	u64 irq_offset;		/* irq_source_routing_table struct offset */
	u64 interface_offset;	/* interface_info struct offset */
	u64 special_offset;	/* loongson_special_attribute struct offset */
	u64 boarddev_table_offset;  /* board_devices offset */
};

struct smbios_tables {
	u16 vers;     /* version of smbios */
	u64 vga_bios; /* vga_bios address */
	struct loongson_params lp;
};

struct efi_reset_system_t {
	u64 ResetCold;
	u64 ResetWarm;
	u64 ResetType;
	u64 Shutdown;
	u64 DoSuspend; /* NULL if not support */
};

struct efi_loongson {
	u64 mps;	/* MPS table */
	u64 acpi;	/* ACPI table (IA64 ext 0.71) */
	u64 acpi20;	/* ACPI table (ACPI 2.0) */
	struct smbios_tables smbios;	/* SM BIOS table */
	u64 sal_systab;	/* SAL system table */
	u64 boot_info;	/* boot info table */
};

struct boot_params {
	struct efi_loongson efi;
	struct efi_reset_system_t reset_system;
};

struct loongson_system_configuration {
	u32 nr_cpus;
	u32 nr_nodes;
	int cores_per_node;
	int cores_per_package;
	enum loongson_cpu_type cputype;
	u64 ht_control_base;
	u64 pci_mem_start_addr;
	u64 pci_mem_end_addr;
	u64 pci_io_base;
	u64 restart_addr;
	u64 poweroff_addr;
	u64 suspend_addr;
	u64 vgabios_addr;
	u32 dma_mask_bits;
};

extern struct efi_memory_map_loongson *loongson_memmap;
extern struct loongson_system_configuration loongson_sysconf;
extern int cpuhotplug_workaround;
#endif
