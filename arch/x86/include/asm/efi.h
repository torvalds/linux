#ifndef _ASM_X86_EFI_H
#define _ASM_X86_EFI_H

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

#ifdef CONFIG_X86_32


extern unsigned long asmlinkage efi_call_phys(void *, ...);

#define efi_call_phys0(f)		efi_call_phys(f)
#define efi_call_phys1(f, a1)		efi_call_phys(f, a1)
#define efi_call_phys2(f, a1, a2)	efi_call_phys(f, a1, a2)
#define efi_call_phys3(f, a1, a2, a3)	efi_call_phys(f, a1, a2, a3)
#define efi_call_phys4(f, a1, a2, a3, a4)	\
	efi_call_phys(f, a1, a2, a3, a4)
#define efi_call_phys5(f, a1, a2, a3, a4, a5)	\
	efi_call_phys(f, a1, a2, a3, a4, a5)
#define efi_call_phys6(f, a1, a2, a3, a4, a5, a6)	\
	efi_call_phys(f, a1, a2, a3, a4, a5, a6)
/*
 * Wrap all the virtual calls in a way that forces the parameters on the stack.
 */

#define efi_call_virt(f, args...) \
	((efi_##f##_t __attribute__((regparm(0)))*)efi.systab->runtime->f)(args)

#define efi_call_virt0(f)		efi_call_virt(f)
#define efi_call_virt1(f, a1)		efi_call_virt(f, a1)
#define efi_call_virt2(f, a1, a2)	efi_call_virt(f, a1, a2)
#define efi_call_virt3(f, a1, a2, a3)	efi_call_virt(f, a1, a2, a3)
#define efi_call_virt4(f, a1, a2, a3, a4)	\
	efi_call_virt(f, a1, a2, a3, a4)
#define efi_call_virt5(f, a1, a2, a3, a4, a5)	\
	efi_call_virt(f, a1, a2, a3, a4, a5)
#define efi_call_virt6(f, a1, a2, a3, a4, a5, a6)	\
	efi_call_virt(f, a1, a2, a3, a4, a5, a6)

#define efi_ioremap(addr, size, type, attr)	ioremap_cache(addr, size)

#else /* !CONFIG_X86_32 */

extern u64 efi_call0(void *fp);
extern u64 efi_call1(void *fp, u64 arg1);
extern u64 efi_call2(void *fp, u64 arg1, u64 arg2);
extern u64 efi_call3(void *fp, u64 arg1, u64 arg2, u64 arg3);
extern u64 efi_call4(void *fp, u64 arg1, u64 arg2, u64 arg3, u64 arg4);
extern u64 efi_call5(void *fp, u64 arg1, u64 arg2, u64 arg3,
		     u64 arg4, u64 arg5);
extern u64 efi_call6(void *fp, u64 arg1, u64 arg2, u64 arg3,
		     u64 arg4, u64 arg5, u64 arg6);

#define efi_call_phys0(f)			\
	efi_call0((f))
#define efi_call_phys1(f, a1)			\
	efi_call1((f), (u64)(a1))
#define efi_call_phys2(f, a1, a2)			\
	efi_call2((f), (u64)(a1), (u64)(a2))
#define efi_call_phys3(f, a1, a2, a3)				\
	efi_call3((f), (u64)(a1), (u64)(a2), (u64)(a3))
#define efi_call_phys4(f, a1, a2, a3, a4)				\
	efi_call4((f), (u64)(a1), (u64)(a2), (u64)(a3),		\
		  (u64)(a4))
#define efi_call_phys5(f, a1, a2, a3, a4, a5)				\
	efi_call5((f), (u64)(a1), (u64)(a2), (u64)(a3),		\
		  (u64)(a4), (u64)(a5))
#define efi_call_phys6(f, a1, a2, a3, a4, a5, a6)			\
	efi_call6((f), (u64)(a1), (u64)(a2), (u64)(a3),		\
		  (u64)(a4), (u64)(a5), (u64)(a6))

#define _efi_call_virtX(x, f, ...)					\
({									\
	efi_status_t __s;						\
									\
	efi_sync_low_kernel_mappings();					\
	preempt_disable();						\
	__s = efi_call##x((void *)efi.systab->runtime->f, __VA_ARGS__);	\
	preempt_enable();						\
	__s;								\
})

#define efi_call_virt0(f)				\
	_efi_call_virtX(0, f)
#define efi_call_virt1(f, a1)				\
	_efi_call_virtX(1, f, (u64)(a1))
#define efi_call_virt2(f, a1, a2)			\
	_efi_call_virtX(2, f, (u64)(a1), (u64)(a2))
#define efi_call_virt3(f, a1, a2, a3)			\
	_efi_call_virtX(3, f, (u64)(a1), (u64)(a2), (u64)(a3))
#define efi_call_virt4(f, a1, a2, a3, a4)		\
	_efi_call_virtX(4, f, (u64)(a1), (u64)(a2), (u64)(a3), (u64)(a4))
#define efi_call_virt5(f, a1, a2, a3, a4, a5)		\
	_efi_call_virtX(5, f, (u64)(a1), (u64)(a2), (u64)(a3), (u64)(a4), (u64)(a5))
#define efi_call_virt6(f, a1, a2, a3, a4, a5, a6)	\
	_efi_call_virtX(6, f, (u64)(a1), (u64)(a2), (u64)(a3), (u64)(a4), (u64)(a5), (u64)(a6))

extern void __iomem *efi_ioremap(unsigned long addr, unsigned long size,
				 u32 type, u64 attribute);

#endif /* CONFIG_X86_32 */

extern int add_efi_memmap;
extern struct efi_scratch efi_scratch;
extern void efi_set_executable(efi_memory_desc_t *md, bool executable);
extern int efi_memblock_x86_reserve_range(void);
extern void efi_call_phys_prelog(void);
extern void efi_call_phys_epilog(void);
extern void efi_unmap_memmap(void);
extern void efi_memory_uc(u64 addr, unsigned long size);
extern void __init efi_map_region(efi_memory_desc_t *md);
extern void __init efi_map_region_fixed(efi_memory_desc_t *md);
extern void efi_sync_low_kernel_mappings(void);
extern int efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages);
extern void efi_cleanup_page_tables(unsigned long pa_memmap, unsigned num_pages);
extern void __init old_map_region(efi_memory_desc_t *md);
extern void __init runtime_code_page_mkexec(void);
extern void __init efi_runtime_mkexec(void);
extern void __init efi_dump_pagetable(void);
extern void __init efi_apply_memmap_quirks(void);

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
#else
/*
 * IF EFI is not configured, have the EFI calls return -ENOSYS.
 */
#define efi_call0(_f)					(-ENOSYS)
#define efi_call1(_f, _a1)				(-ENOSYS)
#define efi_call2(_f, _a1, _a2)				(-ENOSYS)
#define efi_call3(_f, _a1, _a2, _a3)			(-ENOSYS)
#define efi_call4(_f, _a1, _a2, _a3, _a4)		(-ENOSYS)
#define efi_call5(_f, _a1, _a2, _a3, _a4, _a5)		(-ENOSYS)
#define efi_call6(_f, _a1, _a2, _a3, _a4, _a5, _a6)	(-ENOSYS)
static inline void parse_efi_setup(u64 phys_addr, u32 data_len) {}
#endif /* CONFIG_EFI */

#endif /* _ASM_X86_EFI_H */
