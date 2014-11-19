#ifndef _ASM_EFI_H
#define _ASM_EFI_H

#include <asm/io.h>
#include <asm/neon.h>

#ifdef CONFIG_EFI
extern void efi_init(void);
extern void efi_idmap_init(void);
#else
#define efi_init()
#define efi_idmap_init()
#endif

#define efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f = efi.systab->runtime->f;			\
	efi_status_t __s;						\
									\
	kernel_neon_begin();						\
	__s = __f(__VA_ARGS__);						\
	kernel_neon_end();						\
	__s;								\
})

#define __efi_call_virt(f, ...)						\
({									\
	efi_##f##_t *__f = efi.systab->runtime->f;			\
									\
	kernel_neon_begin();						\
	__f(__VA_ARGS__);						\
	kernel_neon_end();						\
})

/* arch specific definitions used by the stub code */

/*
 * AArch64 requires the DTB to be 8-byte aligned in the first 512MiB from
 * start of kernel and may not cross a 2MiB boundary. We set alignment to
 * 2MiB so we know it won't cross a 2MiB boundary.
 */
#define EFI_FDT_ALIGN	SZ_2M   /* used by allocate_new_fdt_and_exit_boot() */
#define MAX_FDT_OFFSET	SZ_512M

#define efi_call_early(f, ...) sys_table_arg->boottime->f(__VA_ARGS__)

#endif /* _ASM_EFI_H */
