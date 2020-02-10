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

extern bool __pure nokaslr(void);
extern bool __pure is_quiet(void);
extern bool __pure novamap(void);

extern __pure efi_system_table_t  *efi_system_table(void);

#define pr_efi(msg)		do {			\
	if (!is_quiet()) efi_printk("EFI stub: "msg);	\
} while (0)

#define pr_efi_err(msg) efi_printk("EFI stub: ERROR: "msg)

void efi_char16_printk(efi_char16_t *);
void efi_char16_printk(efi_char16_t *);

unsigned long get_dram_base(void);

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

typedef struct efi_loaded_image {
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
} efi_loaded_image_t;

typedef struct {
	u64			size;
	u64			file_size;
	u64			phys_size;
	efi_time_t		create_time;
	efi_time_t		last_access_time;
	efi_time_t		modification_time;
	__aligned_u64		attribute;
	efi_char16_t		filename[1];
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

#endif
