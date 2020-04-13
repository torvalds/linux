/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _DRIVERS_FIRMWARE_EFI_EFISTUB_H
#define _DRIVERS_FIRMWARE_EFI_EFISTUB_H

/* error code which can't be mistaken for valid address */
#define EFI_ERROR	(~0UL)

/*
 * __init annotations should not be used in the EFI stub, since the code is
 * either included in the decompressor (x86, ARM) where they have no effect,
 * or the whole stub is __init annotated at the section level (arm64), by
 * renaming the sections, in which case the __init annotation will be
 * redundant, and will result in section names like .init.init.text, and our
 * linker script does not expect that.
 */
#undef __init

/*
 * Allow the platform to override the allocation granularity: this allows
 * systems that have the capability to run with a larger page size to deal
 * with the allocations for initrd and fdt more efficiently.
 */
#ifndef EFI_ALLOC_ALIGN
#define EFI_ALLOC_ALIGN		EFI_PAGE_SIZE
#endif

#ifdef CONFIG_ARM
#define __efistub_global	__section(.data)
#else
#define __efistub_global
#endif

extern bool __pure nochunk(void);
extern bool __pure nokaslr(void);
extern bool __pure noinitrd(void);
extern bool __pure is_quiet(void);
extern bool __pure novamap(void);

extern __pure efi_system_table_t  *efi_system_table(void);

#define pr_efi(msg)		do {			\
	if (!is_quiet()) efi_printk("EFI stub: "msg);	\
} while (0)

#define pr_efi_err(msg) efi_printk("EFI stub: ERROR: "msg)

/* Helper macros for the usual case of using simple C variables: */
#ifndef fdt_setprop_inplace_var
#define fdt_setprop_inplace_var(fdt, node_offset, name, var) \
	fdt_setprop_inplace((fdt), (node_offset), (name), &(var), sizeof(var))
#endif

#ifndef fdt_setprop_var
#define fdt_setprop_var(fdt, node_offset, name, var) \
	fdt_setprop((fdt), (node_offset), (name), &(var), sizeof(var))
#endif

#define get_efi_var(name, vendor, ...)				\
	efi_rt_call(get_variable, (efi_char16_t *)(name),	\
		    (efi_guid_t *)(vendor), __VA_ARGS__)

#define set_efi_var(name, vendor, ...)				\
	efi_rt_call(set_variable, (efi_char16_t *)(name),	\
		    (efi_guid_t *)(vendor), __VA_ARGS__)

#define efi_get_handle_at(array, idx)					\
	(efi_is_native() ? (array)[idx] 				\
		: (efi_handle_t)(unsigned long)((u32 *)(array))[idx])

#define efi_get_handle_num(size)					\
	((size) / (efi_is_native() ? sizeof(efi_handle_t) : sizeof(u32)))

#define for_each_efi_handle(handle, array, size, i)			\
	for (i = 0;							\
	     i < efi_get_handle_num(size) &&				\
		((handle = efi_get_handle_at((array), i)) || true);	\
	     i++)

/*
 * Allocation types for calls to boottime->allocate_pages.
 */
#define EFI_ALLOCATE_ANY_PAGES		0
#define EFI_ALLOCATE_MAX_ADDRESS	1
#define EFI_ALLOCATE_ADDRESS		2
#define EFI_MAX_ALLOCATE_TYPE		3

/*
 * The type of search to perform when calling boottime->locate_handle
 */
#define EFI_LOCATE_ALL_HANDLES			0
#define EFI_LOCATE_BY_REGISTER_NOTIFY		1
#define EFI_LOCATE_BY_PROTOCOL			2

struct efi_boot_memmap {
	efi_memory_desc_t	**map;
	unsigned long		*map_size;
	unsigned long		*desc_size;
	u32			*desc_ver;
	unsigned long		*key_ptr;
	unsigned long		*buff_size;
};

typedef struct efi_generic_dev_path efi_device_path_protocol_t;

/*
 * EFI Boot Services table
 */
union efi_boot_services {
	struct {
		efi_table_hdr_t hdr;
		void *raise_tpl;
		void *restore_tpl;
		efi_status_t (__efiapi *allocate_pages)(int, int, unsigned long,
							efi_physical_addr_t *);
		efi_status_t (__efiapi *free_pages)(efi_physical_addr_t,
						    unsigned long);
		efi_status_t (__efiapi *get_memory_map)(unsigned long *, void *,
							unsigned long *,
							unsigned long *, u32 *);
		efi_status_t (__efiapi *allocate_pool)(int, unsigned long,
						       void **);
		efi_status_t (__efiapi *free_pool)(void *);
		void *create_event;
		void *set_timer;
		void *wait_for_event;
		void *signal_event;
		void *close_event;
		void *check_event;
		void *install_protocol_interface;
		void *reinstall_protocol_interface;
		void *uninstall_protocol_interface;
		efi_status_t (__efiapi *handle_protocol)(efi_handle_t,
							 efi_guid_t *, void **);
		void *__reserved;
		void *register_protocol_notify;
		efi_status_t (__efiapi *locate_handle)(int, efi_guid_t *,
						       void *, unsigned long *,
						       efi_handle_t *);
		efi_status_t (__efiapi *locate_device_path)(efi_guid_t *,
							    efi_device_path_protocol_t **,
							    efi_handle_t *);
		efi_status_t (__efiapi *install_configuration_table)(efi_guid_t *,
								     void *);
		void *load_image;
		void *start_image;
		efi_status_t __noreturn (__efiapi *exit)(efi_handle_t,
							 efi_status_t,
							 unsigned long,
							 efi_char16_t *);
		void *unload_image;
		efi_status_t (__efiapi *exit_boot_services)(efi_handle_t,
							    unsigned long);
		void *get_next_monotonic_count;
		void *stall;
		void *set_watchdog_timer;
		void *connect_controller;
		efi_status_t (__efiapi *disconnect_controller)(efi_handle_t,
							       efi_handle_t,
							       efi_handle_t);
		void *open_protocol;
		void *close_protocol;
		void *open_protocol_information;
		void *protocols_per_handle;
		void *locate_handle_buffer;
		efi_status_t (__efiapi *locate_protocol)(efi_guid_t *, void *,
							 void **);
		void *install_multiple_protocol_interfaces;
		void *uninstall_multiple_protocol_interfaces;
		void *calculate_crc32;
		void *copy_mem;
		void *set_mem;
		void *create_event_ex;
	};
	struct {
		efi_table_hdr_t hdr;
		u32 raise_tpl;
		u32 restore_tpl;
		u32 allocate_pages;
		u32 free_pages;
		u32 get_memory_map;
		u32 allocate_pool;
		u32 free_pool;
		u32 create_event;
		u32 set_timer;
		u32 wait_for_event;
		u32 signal_event;
		u32 close_event;
		u32 check_event;
		u32 install_protocol_interface;
		u32 reinstall_protocol_interface;
		u32 uninstall_protocol_interface;
		u32 handle_protocol;
		u32 __reserved;
		u32 register_protocol_notify;
		u32 locate_handle;
		u32 locate_device_path;
		u32 install_configuration_table;
		u32 load_image;
		u32 start_image;
		u32 exit;
		u32 unload_image;
		u32 exit_boot_services;
		u32 get_next_monotonic_count;
		u32 stall;
		u32 set_watchdog_timer;
		u32 connect_controller;
		u32 disconnect_controller;
		u32 open_protocol;
		u32 close_protocol;
		u32 open_protocol_information;
		u32 protocols_per_handle;
		u32 locate_handle_buffer;
		u32 locate_protocol;
		u32 install_multiple_protocol_interfaces;
		u32 uninstall_multiple_protocol_interfaces;
		u32 calculate_crc32;
		u32 copy_mem;
		u32 set_mem;
		u32 create_event_ex;
	} mixed_mode;
};

typedef union efi_uga_draw_protocol efi_uga_draw_protocol_t;

union efi_uga_draw_protocol {
	struct {
		efi_status_t (__efiapi *get_mode)(efi_uga_draw_protocol_t *,
						  u32*, u32*, u32*, u32*);
		void *set_mode;
		void *blt;
	};
	struct {
		u32 get_mode;
		u32 set_mode;
		u32 blt;
	} mixed_mode;
};

union efi_simple_text_output_protocol {
	struct {
		void *reset;
		efi_status_t (__efiapi *output_string)(efi_simple_text_output_protocol_t *,
						       efi_char16_t *);
		void *test_string;
	};
	struct {
		u32 reset;
		u32 output_string;
		u32 test_string;
	} mixed_mode;
};

#define PIXEL_RGB_RESERVED_8BIT_PER_COLOR		0
#define PIXEL_BGR_RESERVED_8BIT_PER_COLOR		1
#define PIXEL_BIT_MASK					2
#define PIXEL_BLT_ONLY					3
#define PIXEL_FORMAT_MAX				4

typedef struct {
	u32 red_mask;
	u32 green_mask;
	u32 blue_mask;
	u32 reserved_mask;
} efi_pixel_bitmask_t;

typedef struct {
	u32 version;
	u32 horizontal_resolution;
	u32 vertical_resolution;
	int pixel_format;
	efi_pixel_bitmask_t pixel_information;
	u32 pixels_per_scan_line;
} efi_graphics_output_mode_info_t;

typedef union efi_graphics_output_protocol_mode efi_graphics_output_protocol_mode_t;

union efi_graphics_output_protocol_mode {
	struct {
		u32 max_mode;
		u32 mode;
		efi_graphics_output_mode_info_t *info;
		unsigned long size_of_info;
		efi_physical_addr_t frame_buffer_base;
		unsigned long frame_buffer_size;
	};
	struct {
		u32 max_mode;
		u32 mode;
		u32 info;
		u32 size_of_info;
		u64 frame_buffer_base;
		u32 frame_buffer_size;
	} mixed_mode;
};

typedef union efi_graphics_output_protocol efi_graphics_output_protocol_t;

union efi_graphics_output_protocol {
	struct {
		void *query_mode;
		void *set_mode;
		void *blt;
		efi_graphics_output_protocol_mode_t *mode;
	};
	struct {
		u32 query_mode;
		u32 set_mode;
		u32 blt;
		u32 mode;
	} mixed_mode;
};

typedef union {
	struct {
		u32			revision;
		efi_handle_t		parent_handle;
		efi_system_table_t	*system_table;
		efi_handle_t		device_handle;
		void			*file_path;
		void			*reserved;
		u32			load_options_size;
		void			*load_options;
		void			*image_base;
		__aligned_u64		image_size;
		unsigned int		image_code_type;
		unsigned int		image_data_type;
		efi_status_t		(__efiapi *unload)(efi_handle_t image_handle);
	};
	struct {
		u32		revision;
		u32		parent_handle;
		u32		system_table;
		u32		device_handle;
		u32		file_path;
		u32		reserved;
		u32		load_options_size;
		u32		load_options;
		u32		image_base;
		__aligned_u64	image_size;
		u32		image_code_type;
		u32		image_data_type;
		u32		unload;
	} mixed_mode;
} efi_loaded_image_t;

typedef struct {
	u64			size;
	u64			file_size;
	u64			phys_size;
	efi_time_t		create_time;
	efi_time_t		last_access_time;
	efi_time_t		modification_time;
	__aligned_u64		attribute;
	efi_char16_t		filename[];
} efi_file_info_t;

typedef struct efi_file_protocol efi_file_protocol_t;

struct efi_file_protocol {
	u64		revision;
	efi_status_t	(__efiapi *open)	(efi_file_protocol_t *,
						 efi_file_protocol_t **,
						 efi_char16_t *, u64, u64);
	efi_status_t	(__efiapi *close)	(efi_file_protocol_t *);
	efi_status_t	(__efiapi *delete)	(efi_file_protocol_t *);
	efi_status_t	(__efiapi *read)	(efi_file_protocol_t *,
						 unsigned long *, void *);
	efi_status_t	(__efiapi *write)	(efi_file_protocol_t *,
						 unsigned long, void *);
	efi_status_t	(__efiapi *get_position)(efi_file_protocol_t *, u64 *);
	efi_status_t	(__efiapi *set_position)(efi_file_protocol_t *, u64);
	efi_status_t	(__efiapi *get_info)	(efi_file_protocol_t *,
						 efi_guid_t *, unsigned long *,
						 void *);
	efi_status_t	(__efiapi *set_info)	(efi_file_protocol_t *,
						 efi_guid_t *, unsigned long,
						 void *);
	efi_status_t	(__efiapi *flush)	(efi_file_protocol_t *);
};

typedef struct efi_simple_file_system_protocol efi_simple_file_system_protocol_t;

struct efi_simple_file_system_protocol {
	u64	revision;
	int	(__efiapi *open_volume)(efi_simple_file_system_protocol_t *,
					efi_file_protocol_t **);
};

#define EFI_FILE_MODE_READ	0x0000000000000001
#define EFI_FILE_MODE_WRITE	0x0000000000000002
#define EFI_FILE_MODE_CREATE	0x8000000000000000

typedef enum {
	EfiPciIoWidthUint8,
	EfiPciIoWidthUint16,
	EfiPciIoWidthUint32,
	EfiPciIoWidthUint64,
	EfiPciIoWidthFifoUint8,
	EfiPciIoWidthFifoUint16,
	EfiPciIoWidthFifoUint32,
	EfiPciIoWidthFifoUint64,
	EfiPciIoWidthFillUint8,
	EfiPciIoWidthFillUint16,
	EfiPciIoWidthFillUint32,
	EfiPciIoWidthFillUint64,
	EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

typedef enum {
	EfiPciIoAttributeOperationGet,
	EfiPciIoAttributeOperationSet,
	EfiPciIoAttributeOperationEnable,
	EfiPciIoAttributeOperationDisable,
	EfiPciIoAttributeOperationSupported,
    EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef struct {
	u32 read;
	u32 write;
} efi_pci_io_protocol_access_32_t;

typedef union efi_pci_io_protocol efi_pci_io_protocol_t;

typedef
efi_status_t (__efiapi *efi_pci_io_protocol_cfg_t)(efi_pci_io_protocol_t *,
						   EFI_PCI_IO_PROTOCOL_WIDTH,
						   u32 offset,
						   unsigned long count,
						   void *buffer);

typedef struct {
	void *read;
	void *write;
} efi_pci_io_protocol_access_t;

typedef struct {
	efi_pci_io_protocol_cfg_t read;
	efi_pci_io_protocol_cfg_t write;
} efi_pci_io_protocol_config_access_t;

union efi_pci_io_protocol {
	struct {
		void *poll_mem;
		void *poll_io;
		efi_pci_io_protocol_access_t mem;
		efi_pci_io_protocol_access_t io;
		efi_pci_io_protocol_config_access_t pci;
		void *copy_mem;
		void *map;
		void *unmap;
		void *allocate_buffer;
		void *free_buffer;
		void *flush;
		efi_status_t (__efiapi *get_location)(efi_pci_io_protocol_t *,
						      unsigned long *segment_nr,
						      unsigned long *bus_nr,
						      unsigned long *device_nr,
						      unsigned long *func_nr);
		void *attributes;
		void *get_bar_attributes;
		void *set_bar_attributes;
		uint64_t romsize;
		void *romimage;
	};
	struct {
		u32 poll_mem;
		u32 poll_io;
		efi_pci_io_protocol_access_32_t mem;
		efi_pci_io_protocol_access_32_t io;
		efi_pci_io_protocol_access_32_t pci;
		u32 copy_mem;
		u32 map;
		u32 unmap;
		u32 allocate_buffer;
		u32 free_buffer;
		u32 flush;
		u32 get_location;
		u32 attributes;
		u32 get_bar_attributes;
		u32 set_bar_attributes;
		u64 romsize;
		u32 romimage;
	} mixed_mode;
};

#define EFI_PCI_IO_ATTRIBUTE_ISA_MOTHERBOARD_IO 0x0001
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO 0x0002
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO 0x0004
#define EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY 0x0008
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO 0x0010
#define EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO 0x0020
#define EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO 0x0040
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE 0x0080
#define EFI_PCI_IO_ATTRIBUTE_IO 0x0100
#define EFI_PCI_IO_ATTRIBUTE_MEMORY 0x0200
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER 0x0400
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_CACHED 0x0800
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_DISABLE 0x1000
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_DEVICE 0x2000
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_ROM 0x4000
#define EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE 0x8000
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO_16 0x10000
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO_16 0x20000
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO_16 0x40000

struct efi_dev_path;

typedef union apple_properties_protocol apple_properties_protocol_t;

union apple_properties_protocol {
	struct {
		unsigned long version;
		efi_status_t (__efiapi *get)(apple_properties_protocol_t *,
					     struct efi_dev_path *,
					     efi_char16_t *, void *, u32 *);
		efi_status_t (__efiapi *set)(apple_properties_protocol_t *,
					     struct efi_dev_path *,
					     efi_char16_t *, void *, u32);
		efi_status_t (__efiapi *del)(apple_properties_protocol_t *,
					     struct efi_dev_path *,
					     efi_char16_t *);
		efi_status_t (__efiapi *get_all)(apple_properties_protocol_t *,
						 void *buffer, u32 *);
	};
	struct {
		u32 version;
		u32 get;
		u32 set;
		u32 del;
		u32 get_all;
	} mixed_mode;
};

typedef u32 efi_tcg2_event_log_format;

typedef union efi_tcg2_protocol efi_tcg2_protocol_t;

union efi_tcg2_protocol {
	struct {
		void *get_capability;
		efi_status_t (__efiapi *get_event_log)(efi_handle_t,
						       efi_tcg2_event_log_format,
						       efi_physical_addr_t *,
						       efi_physical_addr_t *,
						       efi_bool_t *);
		void *hash_log_extend_event;
		void *submit_command;
		void *get_active_pcr_banks;
		void *set_active_pcr_banks;
		void *get_result_of_set_active_pcr_banks;
	};
	struct {
		u32 get_capability;
		u32 get_event_log;
		u32 hash_log_extend_event;
		u32 submit_command;
		u32 get_active_pcr_banks;
		u32 set_active_pcr_banks;
		u32 get_result_of_set_active_pcr_banks;
	} mixed_mode;
};

typedef union efi_load_file_protocol efi_load_file_protocol_t;
typedef union efi_load_file_protocol efi_load_file2_protocol_t;

union efi_load_file_protocol {
	struct {
		efi_status_t (__efiapi *load_file)(efi_load_file_protocol_t *,
						   efi_device_path_protocol_t *,
						   bool, unsigned long *, void *);
	};
	struct {
		u32 load_file;
	} mixed_mode;
};

void efi_pci_disable_bridge_busmaster(void);

typedef efi_status_t (*efi_exit_boot_map_processing)(
	struct efi_boot_memmap *map,
	void *priv);

efi_status_t efi_exit_boot_services(void *handle,
				    struct efi_boot_memmap *map,
				    void *priv,
				    efi_exit_boot_map_processing priv_func);

void efi_char16_printk(efi_char16_t *);

efi_status_t allocate_new_fdt_and_exit_boot(void *handle,
					    unsigned long *new_fdt_addr,
					    unsigned long max_addr,
					    u64 initrd_addr, u64 initrd_size,
					    char *cmdline_ptr,
					    unsigned long fdt_addr,
					    unsigned long fdt_size);

void *get_fdt(unsigned long *fdt_size);

void efi_get_virtmap(efi_memory_desc_t *memory_map, unsigned long map_size,
		     unsigned long desc_size, efi_memory_desc_t *runtime_map,
		     int *count);

efi_status_t efi_get_random_bytes(unsigned long size, u8 *out);

efi_status_t efi_random_alloc(unsigned long size, unsigned long align,
			      unsigned long *addr, unsigned long random_seed);

efi_status_t check_platform_features(void);

void *get_efi_config_table(efi_guid_t guid);

void efi_printk(char *str);

void efi_free(unsigned long size, unsigned long addr);

char *efi_convert_cmdline(efi_loaded_image_t *image, int *cmd_line_len,
			  unsigned long max_addr);

efi_status_t efi_get_memory_map(struct efi_boot_memmap *map);

efi_status_t efi_low_alloc_above(unsigned long size, unsigned long align,
				 unsigned long *addr, unsigned long min);

static inline
efi_status_t efi_low_alloc(unsigned long size, unsigned long align,
			   unsigned long *addr)
{
	/*
	 * Don't allocate at 0x0. It will confuse code that
	 * checks pointers against NULL. Skip the first 8
	 * bytes so we start at a nice even number.
	 */
	return efi_low_alloc_above(size, align, addr, 0x8);
}

efi_status_t efi_allocate_pages(unsigned long size, unsigned long *addr,
				unsigned long max);

efi_status_t efi_relocate_kernel(unsigned long *image_addr,
				 unsigned long image_size,
				 unsigned long alloc_size,
				 unsigned long preferred_addr,
				 unsigned long alignment,
				 unsigned long min_addr);

efi_status_t efi_parse_options(char const *cmdline);

efi_status_t efi_setup_gop(struct screen_info *si, efi_guid_t *proto,
			   unsigned long size);

efi_status_t efi_load_dtb(efi_loaded_image_t *image,
			  unsigned long *load_addr,
			  unsigned long *load_size);

efi_status_t efi_load_initrd(efi_loaded_image_t *image,
			     unsigned long *load_addr,
			     unsigned long *load_size,
			     unsigned long soft_limit,
			     unsigned long hard_limit);

efi_status_t efi_load_initrd_dev_path(unsigned long *load_addr,
				      unsigned long *load_size,
				      unsigned long max);

#endif
