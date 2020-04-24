/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EFI_H
#define _ASM_X86_EFI_H

#include <asm/fpu/api.h>
#include <asm/pgtable.h>
#include <asm/processor-flags.h>
#include <asm/tlb.h>
#include <asm/nospec-branch.h>
#include <asm/mmu_context.h>
#include <linux/build_bug.h>

extern unsigned long efi_fw_vendor, efi_config_table;

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
 * SGI UV1 machines are known to be incompatible with this scheme, so we
 * provide an opt-out for these machines via a DMI quirk that sets the
 * attribute below.
 */
#define EFI_UV1_MEMMAP         EFI_ARCH_1

static inline bool efi_have_uv1_memmap(void)
{
	return IS_ENABLED(CONFIG_X86_UV) && efi_enabled(EFI_UV1_MEMMAP);
}

#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

#define ARCH_EFI_IRQ_FLAGS_MASK	X86_EFLAGS_IF

/*
 * The EFI services are called through variadic functions in many cases. These
 * functions are implemented in assembler and support only a fixed number of
 * arguments. The macros below allows us to check at build time that we don't
 * try to call them with too many arguments.
 *
 * __efi_nargs() will return the number of arguments if it is 7 or less, and
 * cause a BUILD_BUG otherwise. The limitations of the C preprocessor make it
 * impossible to calculate the exact number of arguments beyond some
 * pre-defined limit. The maximum number of arguments currently supported by
 * any of the thunks is 7, so this is good enough for now and can be extended
 * in the obvious way if we ever need more.
 */

#define __efi_nargs(...) __efi_nargs_(__VA_ARGS__)
#define __efi_nargs_(...) __efi_nargs__(0, ##__VA_ARGS__,	\
	__efi_arg_sentinel(7), __efi_arg_sentinel(6),		\
	__efi_arg_sentinel(5), __efi_arg_sentinel(4),		\
	__efi_arg_sentinel(3), __efi_arg_sentinel(2),		\
	__efi_arg_sentinel(1), __efi_arg_sentinel(0))
#define __efi_nargs__(_0, _1, _2, _3, _4, _5, _6, _7, n, ...)	\
	__take_second_arg(n,					\
		({ BUILD_BUG_ON_MSG(1, "__efi_nargs limit exceeded"); 8; }))
#define __efi_arg_sentinel(n) , n

/*
 * __efi_nargs_check(f, n, ...) will cause a BUILD_BUG if the ellipsis
 * represents more than n arguments.
 */

#define __efi_nargs_check(f, n, ...)					\
	__efi_nargs_check_(f, __efi_nargs(__VA_ARGS__), n)
#define __efi_nargs_check_(f, p, n) __efi_nargs_check__(f, p, n)
#define __efi_nargs_check__(f, p, n) ({					\
	BUILD_BUG_ON_MSG(						\
		(p) > (n),						\
		#f " called with too many arguments (" #p ">" #n ")");	\
})

#ifdef CONFIG_X86_32
#define arch_efi_call_virt_setup()					\
({									\
	kernel_fpu_begin();						\
	firmware_restrict_branch_speculation_start();			\
})

#define arch_efi_call_virt_teardown()					\
({									\
	firmware_restrict_branch_speculation_end();			\
	kernel_fpu_end();						\
})


#define arch_efi_call_virt(p, f, args...)	p->f(args)

#define efi_ioremap(addr, size, type, attr)	ioremap_cache(addr, size)

#else /* !CONFIG_X86_32 */

#define EFI_LOADER_SIGNATURE	"EL64"

extern asmlinkage u64 __efi_call(void *fp, ...);

#define efi_call(...) ({						\
	__efi_nargs_check(efi_call, 7, __VA_ARGS__);			\
	__efi_call(__VA_ARGS__);					\
})

/*
 * struct efi_scratch - Scratch space used while switching to/from efi_mm
 * @phys_stack: stack used during EFI Mixed Mode
 * @prev_mm:    store/restore stolen mm_struct while switching to/from efi_mm
 */
struct efi_scratch {
	u64			phys_stack;
	struct mm_struct	*prev_mm;
} __packed;

#define arch_efi_call_virt_setup()					\
({									\
	efi_sync_low_kernel_mappings();					\
	kernel_fpu_begin();						\
	firmware_restrict_branch_speculation_start();			\
									\
	if (!efi_have_uv1_memmap())					\
		efi_switch_mm(&efi_mm);					\
})

#define arch_efi_call_virt(p, f, args...)				\
	efi_call((void *)p->f, args)					\

#define arch_efi_call_virt_teardown()					\
({									\
	if (!efi_have_uv1_memmap())					\
		efi_switch_mm(efi_scratch.prev_mm);			\
									\
	firmware_restrict_branch_speculation_end();			\
	kernel_fpu_end();						\
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
extern void __init efi_print_memmap(void);
extern void __init efi_memory_uc(u64 addr, unsigned long size);
extern void __init efi_map_region(efi_memory_desc_t *md);
extern void __init efi_map_region_fixed(efi_memory_desc_t *md);
extern void efi_sync_low_kernel_mappings(void);
extern int __init efi_alloc_page_tables(void);
extern int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages);
extern void __init old_map_region(efi_memory_desc_t *md);
extern void __init runtime_code_page_mkexec(void);
extern void __init efi_runtime_update_mappings(void);
extern void __init efi_dump_pagetable(void);
extern void __init efi_apply_memmap_quirks(void);
extern int __init efi_reuse_config(u64 tables, int nr_tables);
extern void efi_delete_dummy_variable(void);
extern void efi_switch_mm(struct mm_struct *mm);
extern void efi_recover_from_page_fault(unsigned long phys_addr);
extern void efi_free_boot_services(void);
extern pgd_t * __init efi_uv1_memmap_phys_prolog(void);
extern void __init efi_uv1_memmap_phys_epilog(pgd_t *save_pgd);

/* kexec external ABI */
struct efi_setup_data {
	u64 fw_vendor;
	u64 __unused;
	u64 tables;
	u64 smbios;
	u64 reserved[8];
};

extern u64 efi_setup;

#ifdef CONFIG_EFI
extern efi_status_t __efi64_thunk(u32, ...);

#define efi64_thunk(...) ({						\
	__efi_nargs_check(efi64_thunk, 6, __VA_ARGS__);			\
	__efi64_thunk(__VA_ARGS__);					\
})

static inline bool efi_is_mixed(void)
{
	if (!IS_ENABLED(CONFIG_EFI_MIXED))
		return false;
	return IS_ENABLED(CONFIG_X86_64) && !efi_enabled(EFI_64BIT);
}

static inline bool efi_runtime_supported(void)
{
	if (IS_ENABLED(CONFIG_X86_64) == efi_enabled(EFI_64BIT))
		return true;

	return IS_ENABLED(CONFIG_EFI_MIXED);
}

extern void parse_efi_setup(u64 phys_addr, u32 data_len);

extern void efifb_setup_from_dmi(struct screen_info *si, const char *opt);

extern void efi_thunk_runtime_setup(void);
efi_status_t efi_set_virtual_address_map(unsigned long memory_map_size,
					 unsigned long descriptor_size,
					 u32 descriptor_version,
					 efi_memory_desc_t *virtual_map,
					 unsigned long systab_phys);

/* arch specific definitions used by the stub code */

__attribute_const__ bool efi_is_64bit(void);

static inline bool efi_is_native(void)
{
	if (!IS_ENABLED(CONFIG_X86_64))
		return true;
	if (!IS_ENABLED(CONFIG_EFI_MIXED))
		return true;
	return efi_is_64bit();
}

#define efi_mixed_mode_cast(attr)					\
	__builtin_choose_expr(						\
		__builtin_types_compatible_p(u32, __typeof__(attr)),	\
			(unsigned long)(attr), (attr))

#define efi_table_attr(inst, attr)					\
	(efi_is_native()						\
		? inst->attr						\
		: (__typeof__(inst->attr))				\
			efi_mixed_mode_cast(inst->mixed_mode.attr))

/*
 * The following macros allow translating arguments if necessary from native to
 * mixed mode. The use case for this is to initialize the upper 32 bits of
 * output parameters, and where the 32-bit method requires a 64-bit argument,
 * which must be split up into two arguments to be thunked properly.
 *
 * As examples, the AllocatePool boot service returns the address of the
 * allocation, but it will not set the high 32 bits of the address. To ensure
 * that the full 64-bit address is initialized, we zero-init the address before
 * calling the thunk.
 *
 * The FreePages boot service takes a 64-bit physical address even in 32-bit
 * mode. For the thunk to work correctly, a native 64-bit call of
 * 	free_pages(addr, size)
 * must be translated to
 * 	efi64_thunk(free_pages, addr & U32_MAX, addr >> 32, size)
 * so that the two 32-bit halves of addr get pushed onto the stack separately.
 */

static inline void *efi64_zero_upper(void *p)
{
	((u32 *)p)[1] = 0;
	return p;
}

static inline u32 efi64_convert_status(efi_status_t status)
{
	return (u32)(status | (u64)status >> 32);
}

#define __efi64_argmap_free_pages(addr, size)				\
	((addr), 0, (size))

#define __efi64_argmap_get_memory_map(mm_size, mm, key, size, ver)	\
	((mm_size), (mm), efi64_zero_upper(key), efi64_zero_upper(size), (ver))

#define __efi64_argmap_allocate_pool(type, size, buffer)		\
	((type), (size), efi64_zero_upper(buffer))

#define __efi64_argmap_handle_protocol(handle, protocol, interface)	\
	((handle), (protocol), efi64_zero_upper(interface))

#define __efi64_argmap_locate_protocol(protocol, reg, interface)	\
	((protocol), (reg), efi64_zero_upper(interface))

#define __efi64_argmap_locate_device_path(protocol, path, handle)	\
	((protocol), (path), efi64_zero_upper(handle))

#define __efi64_argmap_exit(handle, status, size, data)			\
	((handle), efi64_convert_status(status), (size), (data))

/* PCI I/O */
#define __efi64_argmap_get_location(protocol, seg, bus, dev, func)	\
	((protocol), efi64_zero_upper(seg), efi64_zero_upper(bus),	\
	 efi64_zero_upper(dev), efi64_zero_upper(func))

/* LoadFile */
#define __efi64_argmap_load_file(protocol, path, policy, bufsize, buf)	\
	((protocol), (path), (policy), efi64_zero_upper(bufsize), (buf))

/*
 * The macros below handle the plumbing for the argument mapping. To add a
 * mapping for a specific EFI method, simply define a macro
 * __efi64_argmap_<method name>, following the examples above.
 */

#define __efi64_thunk_map(inst, func, ...)				\
	efi64_thunk(inst->mixed_mode.func,				\
		__efi64_argmap(__efi64_argmap_ ## func(__VA_ARGS__),	\
			       (__VA_ARGS__)))

#define __efi64_argmap(mapped, args)					\
	__PASTE(__efi64_argmap__, __efi_nargs(__efi_eat mapped))(mapped, args)
#define __efi64_argmap__0(mapped, args) __efi_eval mapped
#define __efi64_argmap__1(mapped, args) __efi_eval args

#define __efi_eat(...)
#define __efi_eval(...) __VA_ARGS__

/* The three macros below handle dispatching via the thunk if needed */

#define efi_call_proto(inst, func, ...)					\
	(efi_is_native()						\
		? inst->func(inst, ##__VA_ARGS__)			\
		: __efi64_thunk_map(inst, func, inst, ##__VA_ARGS__))

#define efi_bs_call(func, ...)						\
	(efi_is_native()						\
		? efi_system_table()->boottime->func(__VA_ARGS__)	\
		: __efi64_thunk_map(efi_table_attr(efi_system_table(),	\
				boottime), func, __VA_ARGS__))

#define efi_rt_call(func, ...)						\
	(efi_is_native()						\
		? efi_system_table()->runtime->func(__VA_ARGS__)	\
		: __efi64_thunk_map(efi_table_attr(efi_system_table(),	\
				runtime), func, __VA_ARGS__))

extern bool efi_reboot_required(void);
extern bool efi_is_table_address(unsigned long phys_addr);

extern void efi_find_mirror(void);
extern void efi_reserve_boot_services(void);
#else
static inline void parse_efi_setup(u64 phys_addr, u32 data_len) {}
static inline bool efi_reboot_required(void)
{
	return false;
}
static inline  bool efi_is_table_address(unsigned long phys_addr)
{
	return false;
}
static inline void efi_find_mirror(void)
{
}
static inline void efi_reserve_boot_services(void)
{
}
#endif /* CONFIG_EFI */

#ifdef CONFIG_EFI_FAKE_MEMMAP
extern void __init efi_fake_memmap_early(void);
#else
static inline void efi_fake_memmap_early(void)
{
}
#endif

#endif /* _ASM_X86_EFI_H */
