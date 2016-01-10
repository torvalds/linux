
#ifndef _DRIVERS_FIRMWARE_EFI_EFISTUB_H
#define _DRIVERS_FIRMWARE_EFI_EFISTUB_H

/* error code which can't be mistaken for valid address */
#define EFI_ERROR	(~0UL)

void efi_char16_printk(efi_system_table_t *, efi_char16_t *);

efi_status_t efi_open_volume(efi_system_table_t *sys_table_arg, void *__image,
			     void **__fh);

efi_status_t efi_file_size(efi_system_table_t *sys_table_arg, void *__fh,
			   efi_char16_t *filename_16, void **handle,
			   u64 *file_sz);

efi_status_t efi_file_read(void *handle, unsigned long *size, void *addr);

efi_status_t efi_file_close(void *handle);

unsigned long get_dram_base(efi_system_table_t *sys_table_arg);

efi_status_t update_fdt(efi_system_table_t *sys_table, void *orig_fdt,
			unsigned long orig_fdt_size,
			void *fdt, int new_fdt_size, char *cmdline_ptr,
			u64 initrd_addr, u64 initrd_size,
			efi_memory_desc_t *memory_map,
			unsigned long map_size, unsigned long desc_size,
			u32 desc_ver);

efi_status_t allocate_new_fdt_and_exit_boot(efi_system_table_t *sys_table,
					    void *handle,
					    unsigned long *new_fdt_addr,
					    unsigned long max_addr,
					    u64 initrd_addr, u64 initrd_size,
					    char *cmdline_ptr,
					    unsigned long fdt_addr,
					    unsigned long fdt_size);

void *get_fdt(efi_system_table_t *sys_table, unsigned long *fdt_size);

void efi_get_virtmap(efi_memory_desc_t *memory_map, unsigned long map_size,
		     unsigned long desc_size, efi_memory_desc_t *runtime_map,
		     int *count);

efi_status_t efi_get_random_bytes(efi_system_table_t *sys_table,
				  unsigned long size, u8 *out);

#endif
