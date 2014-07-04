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

#endif /* _ASM_EFI_H */
