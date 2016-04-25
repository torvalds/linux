#ifndef _ASM_X86_EFI_H
#define _ASM_X86_EFI_H

#include <asm/fpu/api.h>
#include <asm/pgtable.h>
#include <asm/processor-flags.h>
#include <asm/tlb.h>

/*
 * We map the EFI regions needed for runtime services non-contiguously,
 * with preserved alignment on virtual addresses starting from -4G down
 * for a total max space of 64G. This way, we provide for stable runtime
 * services addresses across kernels so that a kexec'd kernel can still
 * use them.
 *
 * This is the main reason why we're doing stable VA mappings for RT
 * services.
 *
 * This flag is used in conjuction with a chicken bit called
 * "efi=old_map" which can be used as a fallback to the old runtime
 * services mapping method in case there's some b0rkage with a
 * particular EFI implementation (haha, it is hard to hold up the
 * sarcasm here...).
 */
#define EFI_OLD_MEMMAP		EFI_ARCH_1

#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

#define MAX_CMDLINE_ADDRESS	UINT_MAX

#define ARCH_EFI_IRQ_FLAGS_MASK	X86_EFLAGS_IF

#ifdef CONFIG_X86_32

extern unsigned long asmlinkage efi_call_phys(void *, ...);

#define arch_efi_call_virt_setup()	kernel_fpu_begin()
#define arch_efi_call_virt_teardown()	kernel_fpu_end()

/*
 * Wrap all the virtual calls in a way that forces the parameters on the stack.
 */
#define arch_efi_call_virt(f, args...)					\
({									\
	((efi_##f##_t __attribute__((regparm(0)))*)			\
		efi.systab->runtime->f)(args);				\
})

#define efi_ioremap(addr, size, type, attr)	ioremap_cache(addr, size)

#else /* !CONFIG_X86_32 */

#define EFI_LOADER_SIGNATURE	"EL64"

extern u64 asmlinkage efi_call(void *fp, ...);

#define efi_call_phys(f, args...)		efi_call((f), args)

/*
 * Scratch space used for switching the pagetable in the EFI stub
 */
struct efi_scratch {
	u64	r15;
	u64	prev_cr3;
	pgd_t	*efi_pgt;
	bool	use_pgd;
	u64	phys_stack;
} __packed;

#define arch_efi_call_virt_setup()					\
({									\
	efi_sync_low_kernel_mappings();					\
	preempt_disable();						\
	__kernel_fpu_begin();						\
									\
	if (efi_scratch.use_pgd) {					\
		efi_scratch.prev_cr3 = read_cr3();			\
		write_cr3((unsigned long)efi_scratch.efi_pgt);		\
		__flush_tlb_all();					\
	}								\
})

#define arch_efi_call_virt(f, args...)					\
	efi_call((void *)efi.systab->runtime->f, args)			\

#define arch_efi_call_virt_teardown()					\
({									\
	if (efi_scratch.use_pgd) {					\
		write_cr3(efi_scratch.prev_cr3);			\
		__flush_tlb_all();					\
	}								\
									\
	__kernel_fpu_end();						\
	preempt_enable();						\
})

extern void __iomem *__init efi_ioremap(unsigned long addr, unsigned long size,
					u32 type, u64 attribute);

#ifdef CONFIG_KASAN
/*
 * CONFIG_KASAN may redefine memset to __memset.  __memset function is present
 * only in kernel binary.  Since the EFI stub linked into a separate binary it
 * doesn't have __memset().  So we should use standard memset from
 * arch/x86/boot/compressed/string.c.  The same applies to memcpy and memmove.
 */
#undef memcpy
#undef memset
#undef memmove
#endif

#endif /* CONFIG_X86_32 */

extern struct efi_scratch efi_scratch;
extern void __init efi_set_executable(efi_memory_desc_t *md, bool executable);
extern int __init efi_memblock_x86_reserve_range(void);
extern pgd_t * __init efi_call_phys_prolog(void);
extern void __init efi_call_phys_epilog(pgd_t *save_pgd);
extern void __init efi_print_memmap(void);
extern void __init efi_unmap_memmap(void);
extern void __init efi_memory_uc(u64 addr, unsigned long size);
extern void __init efi_map_region(efi_memory_desc_t *md);
extern void __init efi_map_region_fixed(efi_memory_desc_t *md);
extern void efi_sync_low_kernel_mappings(void);
extern int __init efi_alloc_page_tables(void);
extern int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages);
extern void __init efi_cleanup_page_tables(unsigned long pa_memmap, unsigned num_pages);
extern void __init old_map_region(efi_memory_desc_t *md);
extern void __init runtime_code_page_mkexec(void);
extern void __init efi_runtime_update_mappings(void);
extern void __init efi_dump_pagetable(void);
extern void __init efi_apply_memmap_quirks(void);
extern int __init efi_reuse_config(u64 tables, int nr_tables);
extern void efi_delete_dummy_variable(void);

struct efi_setup_data {
	u64 fw_vendor;
	u64 runtime;
	u64 tables;
	u64 smbios;
	u64 reserved[8];
};

extern u64 efi_setup;

#ifdef CONFIG_EFI

static inline bool efi_is_native(void)
{
	return IS_ENABLED(CONFIG_X86_64) == efi_enabled(EFI_64BIT);
}

static inline bool efi_runtime_supported(void)
{
	if (efi_is_native())
		return true;

	if (IS_ENABLED(CONFIG_EFI_MIXED) && !efi_enabled(EFI_OLD_MEMMAP))
		return true;

	return false;
}

extern struct console early_efi_console;
extern void parse_efi_setup(u64 phys_addr, u32 data_len);

extern void efifb_setup_from_dmi(struct screen_info *si, const char *opt);

#ifdef CONFIG_EFI_MIXED
extern void efi_thunk_runtime_setup(void);
extern efi_status_t efi_thunk_set_virtual_address_map(
	void *phys_set_virtual_address_map,
	unsigned long memory_map_size,
	unsigned long descriptor_size,
	u32 descriptor_version,
	efi_memory_desc_t *virtual_map);
#else
static inline void efi_thunk_runtime_setup(void) {}
static inline efi_status_t efi_thunk_set_virtual_address_map(
	void *phys_set_virtual_address_map,
	unsigned long memory_map_size,
	unsigned long descriptor_size,
	u32 descriptor_version,
	efi_memory_desc_t *virtual_map)
{
	return EFI_SUCCESS;
}
#endif /* CONFIG_EFI_MIXED */


/* arch specific definitions used by the stub code */

struct efi_config {
	u64 image_handle;
	u64 table;
	u64 allocate_pool;
	u64 allocate_pages;
	u64 get_memory_map;
	u64 free_pool;
	u64 free_pages;
	u64 locate_handle;
	u64 handle_protocol;
	u64 exit_boot_services;
	u64 text_output;
	efi_status_t (*call)(unsigned long, ...);
	bool is64;
} __packed;

__pure const struct efi_config *__efi_early(void);

#define efi_call_early(f, ...)						\
	__efi_early()->call(__efi_early()->f, __VA_ARGS__);

#define __efi_call_early(f, ...)					\
	__efi_early()->call((unsigned long)f, __VA_ARGS__);

#define efi_is_64bit()		__efi_early()->is64

extern bool efi_reboot_required(void);

#else
static inline void parse_efi_setup(u64 phys_addr, u32 data_len) {}
static inline bool efi_reboot_required(void)
{
	return false;
}
#endif /* CONFIG_EFI */

#endif /* _ASM_X86_EFI_H */
