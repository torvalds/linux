/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PowerPC Memory Protection Keys management
 *
 * Copyright 2017, Ram Pai, IBM Corporation.
 */

#ifndef _ASM_POWERPC_KEYS_H
#define _ASM_POWERPC_KEYS_H

#include <linux/jump_label.h>
#include <asm/firmware.h>

DECLARE_STATIC_KEY_TRUE(pkey_disabled);
extern int pkeys_total; /* total pkeys as per device tree */
extern u32 initial_allocation_mask; /* bits set for reserved keys */

/*
 * Define these here temporarily so we're not dependent on patching linux/mm.h.
 * Once it's updated we can drop these.
 */
#ifndef VM_PKEY_BIT0
# define VM_PKEY_SHIFT	VM_HIGH_ARCH_BIT_0
# define VM_PKEY_BIT0	VM_HIGH_ARCH_0
# define VM_PKEY_BIT1	VM_HIGH_ARCH_1
# define VM_PKEY_BIT2	VM_HIGH_ARCH_2
# define VM_PKEY_BIT3	VM_HIGH_ARCH_3
# define VM_PKEY_BIT4	VM_HIGH_ARCH_4
#endif

#define ARCH_VM_PKEY_FLAGS (VM_PKEY_BIT0 | VM_PKEY_BIT1 | VM_PKEY_BIT2 | \
			    VM_PKEY_BIT3 | VM_PKEY_BIT4)

/* Override any generic PKEY permission defines */
#define PKEY_DISABLE_EXECUTE   0x4
#define PKEY_ACCESS_MASK       (PKEY_DISABLE_ACCESS | \
				PKEY_DISABLE_WRITE  | \
				PKEY_DISABLE_EXECUTE)

static inline u64 pkey_to_vmflag_bits(u16 pkey)
{
	return (((u64)pkey << VM_PKEY_SHIFT) & ARCH_VM_PKEY_FLAGS);
}

static inline u64 vmflag_to_pte_pkey_bits(u64 vm_flags)
{
	if (static_branch_likely(&pkey_disabled))
		return 0x0UL;

	return (((vm_flags & VM_PKEY_BIT0) ? H_PTE_PKEY_BIT4 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT1) ? H_PTE_PKEY_BIT3 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT2) ? H_PTE_PKEY_BIT2 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT3) ? H_PTE_PKEY_BIT1 : 0x0UL) |
		((vm_flags & VM_PKEY_BIT4) ? H_PTE_PKEY_BIT0 : 0x0UL));
}

static inline int vma_pkey(struct vm_area_struct *vma)
{
	if (static_branch_likely(&pkey_disabled))
		return 0;
	return (vma->vm_flags & ARCH_VM_PKEY_FLAGS) >> VM_PKEY_SHIFT;
}

#define arch_max_pkey() pkeys_total

static inline u64 pte_to_hpte_pkey_bits(u64 pteflags)
{
	return (((pteflags & H_PTE_PKEY_BIT0) ? HPTE_R_KEY_BIT0 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT1) ? HPTE_R_KEY_BIT1 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT2) ? HPTE_R_KEY_BIT2 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT3) ? HPTE_R_KEY_BIT3 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT4) ? HPTE_R_KEY_BIT4 : 0x0UL));
}

static inline u16 pte_to_pkey_bits(u64 pteflags)
{
	return (((pteflags & H_PTE_PKEY_BIT0) ? 0x10 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT1) ? 0x8 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT2) ? 0x4 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT3) ? 0x2 : 0x0UL) |
		((pteflags & H_PTE_PKEY_BIT4) ? 0x1 : 0x0UL));
}

#define pkey_alloc_mask(pkey) (0x1 << pkey)

#define mm_pkey_allocation_map(mm) (mm->context.pkey_allocation_map)

#define __mm_pkey_allocated(mm, pkey) {	\
	mm_pkey_allocation_map(mm) |= pkey_alloc_mask(pkey); \
}

#define __mm_pkey_free(mm, pkey) {	\
	mm_pkey_allocation_map(mm) &= ~pkey_alloc_mask(pkey);	\
}

#define __mm_pkey_is_allocated(mm, pkey)	\
	(mm_pkey_allocation_map(mm) & pkey_alloc_mask(pkey))

#define __mm_pkey_is_reserved(pkey) (initial_allocation_mask & \
				       pkey_alloc_mask(pkey))

static inline bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	/* A reserved key is never considered as 'explicitly allocated' */
	return ((pkey < arch_max_pkey()) &&
		!__mm_pkey_is_reserved(pkey) &&
		__mm_pkey_is_allocated(mm, pkey));
}

extern void __arch_activate_pkey(int pkey);
extern void __arch_deactivate_pkey(int pkey);
/*
 * Returns a positive, 5-bit key on success, or -1 on failure.
 * Relies on the mmap_sem to protect against concurrency in mm_pkey_alloc() and
 * mm_pkey_free().
 */
static inline int mm_pkey_alloc(struct mm_struct *mm)
{
	/*
	 * Note: this is the one and only place we make sure that the pkey is
	 * valid as far as the hardware is concerned. The rest of the kernel
	 * trusts that only good, valid pkeys come out of here.
	 */
	u32 all_pkeys_mask = (u32)(~(0x0));
	int ret;

	if (static_branch_likely(&pkey_disabled))
		return -1;

	/*
	 * Are we out of pkeys? We must handle this specially because ffz()
	 * behavior is undefined if there are no zeros.
	 */
	if (mm_pkey_allocation_map(mm) == all_pkeys_mask)
		return -1;

	ret = ffz((u32)mm_pkey_allocation_map(mm));
	__mm_pkey_allocated(mm, ret);

	/*
	 * Enable the key in the hardware
	 */
	if (ret > 0)
		__arch_activate_pkey(ret);
	return ret;
}

static inline int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	if (static_branch_likely(&pkey_disabled))
		return -1;

	if (!mm_pkey_is_allocated(mm, pkey))
		return -EINVAL;

	/*
	 * Disable the key in the hardware
	 */
	__arch_deactivate_pkey(pkey);
	__mm_pkey_free(mm, pkey);

	return 0;
}

/*
 * Try to dedicate one of the protection keys to be used as an
 * execute-only protection key.
 */
extern int __execute_only_pkey(struct mm_struct *mm);
static inline int execute_only_pkey(struct mm_struct *mm)
{
	if (static_branch_likely(&pkey_disabled))
		return -1;

	return __execute_only_pkey(mm);
}

extern int __arch_override_mprotect_pkey(struct vm_area_struct *vma,
					 int prot, int pkey);
static inline int arch_override_mprotect_pkey(struct vm_area_struct *vma,
					      int prot, int pkey)
{
	if (static_branch_likely(&pkey_disabled))
		return 0;

	/*
	 * Is this an mprotect_pkey() call? If so, never override the value that
	 * came from the user.
	 */
	if (pkey != -1)
		return pkey;

	return __arch_override_mprotect_pkey(vma, prot, pkey);
}

extern int __arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
				       unsigned long init_val);
static inline int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
					    unsigned long init_val)
{
	if (static_branch_likely(&pkey_disabled))
		return -EINVAL;
	return __arch_set_user_pkey_access(tsk, pkey, init_val);
}

static inline bool arch_pkeys_enabled(void)
{
	return !static_branch_likely(&pkey_disabled);
}

extern void pkey_mm_init(struct mm_struct *mm);
extern bool arch_supports_pkeys(int cap);
extern unsigned int arch_usable_pkeys(void);
extern void thread_pkey_regs_save(struct thread_struct *thread);
extern void thread_pkey_regs_restore(struct thread_struct *new_thread,
				     struct thread_struct *old_thread);
extern void thread_pkey_regs_init(struct thread_struct *thread);
#endif /*_ASM_POWERPC_KEYS_H */
