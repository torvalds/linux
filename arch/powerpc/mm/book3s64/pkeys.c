// SPDX-License-Identifier: GPL-2.0+
/*
 * PowerPC Memory Protection Keys management
 *
 * Copyright 2017, Ram Pai, IBM Corporation.
 */

#include <asm/mman.h>
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#include <asm/setup.h>
#include <linux/pkeys.h>
#include <linux/of_fdt.h>

int  num_pkey;		/* Max number of pkeys supported */
/*
 *  Keys marked in the reservation list cannot be allocated by  userspace
 */
u32 reserved_allocation_mask __ro_after_init;

/* Bits set for the initially allocated keys */
static u32 initial_allocation_mask __ro_after_init;

/*
 * Even if we allocate keys with sys_pkey_alloc(), we need to make sure
 * other thread still find the access denied using the same keys.
 */
static u64 default_amr = ~0x0UL;
static u64 default_iamr = 0x5555555555555555UL;
u64 default_uamor __ro_after_init;
/*
 * Key used to implement PROT_EXEC mmap. Denies READ/WRITE
 * We pick key 2 because 0 is special key and 1 is reserved as per ISA.
 */
static int execute_only_key = 2;
static bool pkey_execute_disable_supported;


#define AMR_BITS_PER_PKEY 2
#define AMR_RD_BIT 0x1UL
#define AMR_WR_BIT 0x2UL
#define IAMR_EX_BIT 0x1UL
#define PKEY_REG_BITS (sizeof(u64) * 8)
#define pkeyshift(pkey) (PKEY_REG_BITS - ((pkey+1) * AMR_BITS_PER_PKEY))

static int __init dt_scan_storage_keys(unsigned long node,
				       const char *uname, int depth,
				       void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *prop;
	int *pkeys_total = (int *) data;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "ibm,processor-storage-keys", NULL);
	if (!prop)
		return 0;
	*pkeys_total = be32_to_cpu(prop[0]);
	return 1;
}

static int scan_pkey_feature(void)
{
	int ret;
	int pkeys_total = 0;

	/*
	 * Pkey is not supported with Radix translation.
	 */
	if (early_radix_enabled())
		return 0;

	/*
	 * Only P7 and above supports SPRN_AMR update with MSR[PR] = 1
	 */
	if (!early_cpu_has_feature(CPU_FTR_ARCH_206))
		return 0;

	ret = of_scan_flat_dt(dt_scan_storage_keys, &pkeys_total);
	if (ret == 0) {
		/*
		 * Let's assume 32 pkeys on P8/P9 bare metal, if its not defined by device
		 * tree. We make this exception since some version of skiboot forgot to
		 * expose this property on power8/9.
		 */
		if (!firmware_has_feature(FW_FEATURE_LPAR)) {
			unsigned long pvr = mfspr(SPRN_PVR);

			if (PVR_VER(pvr) == PVR_POWER8 || PVR_VER(pvr) == PVR_POWER8E ||
			    PVR_VER(pvr) == PVR_POWER8NVL || PVR_VER(pvr) == PVR_POWER9)
				pkeys_total = 32;
		}
	}

	/*
	 * Adjust the upper limit, based on the number of bits supported by
	 * arch-neutral code.
	 */
	pkeys_total = min_t(int, pkeys_total,
			    ((ARCH_VM_PKEY_FLAGS >> VM_PKEY_SHIFT) + 1));
	return pkeys_total;
}

void __init pkey_early_init_devtree(void)
{
	int pkeys_total, i;

	/*
	 * We define PKEY_DISABLE_EXECUTE in addition to the arch-neutral
	 * generic defines for PKEY_DISABLE_ACCESS and PKEY_DISABLE_WRITE.
	 * Ensure that the bits a distinct.
	 */
	BUILD_BUG_ON(PKEY_DISABLE_EXECUTE &
		     (PKEY_DISABLE_ACCESS | PKEY_DISABLE_WRITE));

	/*
	 * pkey_to_vmflag_bits() assumes that the pkey bits are contiguous
	 * in the vmaflag. Make sure that is really the case.
	 */
	BUILD_BUG_ON(__builtin_clzl(ARCH_VM_PKEY_FLAGS >> VM_PKEY_SHIFT) +
		     __builtin_popcountl(ARCH_VM_PKEY_FLAGS >> VM_PKEY_SHIFT)
				!= (sizeof(u64) * BITS_PER_BYTE));

	/* scan the device tree for pkey feature */
	pkeys_total = scan_pkey_feature();
	if (!pkeys_total)
		goto out;

	/* Allow all keys to be modified by default */
	default_uamor = ~0x0UL;

	cur_cpu_spec->mmu_features |= MMU_FTR_PKEY;

	/*
	 * The device tree cannot be relied to indicate support for
	 * execute_disable support. Instead we use a PVR check.
	 */
	if (pvr_version_is(PVR_POWER7) || pvr_version_is(PVR_POWER7p))
		pkey_execute_disable_supported = false;
	else
		pkey_execute_disable_supported = true;

#ifdef CONFIG_PPC_4K_PAGES
	/*
	 * The OS can manage only 8 pkeys due to its inability to represent them
	 * in the Linux 4K PTE. Mark all other keys reserved.
	 */
	num_pkey = min(8, pkeys_total);
#else
	num_pkey = pkeys_total;
#endif

	if (unlikely(num_pkey <= execute_only_key) || !pkey_execute_disable_supported) {
		/*
		 * Insufficient number of keys to support
		 * execute only key. Mark it unavailable.
		 */
		execute_only_key = -1;
	} else {
		/*
		 * Mark the execute_only_pkey as not available for
		 * user allocation via pkey_alloc.
		 */
		reserved_allocation_mask |= (0x1 << execute_only_key);

		/*
		 * Deny READ/WRITE for execute_only_key.
		 * Allow execute in IAMR.
		 */
		default_amr  |= (0x3ul << pkeyshift(execute_only_key));
		default_iamr &= ~(0x1ul << pkeyshift(execute_only_key));

		/*
		 * Clear the uamor bits for this key.
		 */
		default_uamor &= ~(0x3ul << pkeyshift(execute_only_key));
	}

	/*
	 * Allow access for only key 0. And prevent any other modification.
	 */
	default_amr   &= ~(0x3ul << pkeyshift(0));
	default_iamr  &= ~(0x1ul << pkeyshift(0));
	default_uamor &= ~(0x3ul << pkeyshift(0));
	/*
	 * key 0 is special in that we want to consider it an allocated
	 * key which is preallocated. We don't allow changing AMR bits
	 * w.r.t key 0. But one can pkey_free(key0)
	 */
	initial_allocation_mask |= (0x1 << 0);

	/*
	 * key 1 is recommended not to be used. PowerISA(3.0) page 1015,
	 * programming note.
	 */
	reserved_allocation_mask |= (0x1 << 1);
	default_uamor &= ~(0x3ul << pkeyshift(1));

	/*
	 * Prevent the usage of OS reserved keys. Update UAMOR
	 * for those keys. Also mark the rest of the bits in the
	 * 32 bit mask as reserved.
	 */
	for (i = num_pkey; i < 32 ; i++) {
		reserved_allocation_mask |= (0x1 << i);
		default_uamor &= ~(0x3ul << pkeyshift(i));
	}
	/*
	 * Prevent the allocation of reserved keys too.
	 */
	initial_allocation_mask |= reserved_allocation_mask;

	pr_info("Enabling pkeys with max key count %d\n", num_pkey);
out:
	/*
	 * Setup uamor on boot cpu
	 */
	mtspr(SPRN_UAMOR, default_uamor);

	return;
}

void pkey_mm_init(struct mm_struct *mm)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;
	mm_pkey_allocation_map(mm) = initial_allocation_mask;
	mm->context.execute_only_pkey = execute_only_key;
}

static inline u64 read_amr(void)
{
	return mfspr(SPRN_AMR);
}

static inline void write_amr(u64 value)
{
	mtspr(SPRN_AMR, value);
}

static inline u64 read_iamr(void)
{
	if (!likely(pkey_execute_disable_supported))
		return 0x0UL;

	return mfspr(SPRN_IAMR);
}

static inline void write_iamr(u64 value)
{
	if (!likely(pkey_execute_disable_supported))
		return;

	mtspr(SPRN_IAMR, value);
}

static inline void init_amr(int pkey, u8 init_bits)
{
	u64 new_amr_bits = (((u64)init_bits & 0x3UL) << pkeyshift(pkey));
	u64 old_amr = read_amr() & ~((u64)(0x3ul) << pkeyshift(pkey));

	write_amr(old_amr | new_amr_bits);
}

static inline void init_iamr(int pkey, u8 init_bits)
{
	u64 new_iamr_bits = (((u64)init_bits & 0x1UL) << pkeyshift(pkey));
	u64 old_iamr = read_iamr() & ~((u64)(0x1ul) << pkeyshift(pkey));

	write_iamr(old_iamr | new_iamr_bits);
}

/*
 * Set the access rights in AMR IAMR and UAMOR registers for @pkey to that
 * specified in @init_val.
 */
int __arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
				unsigned long init_val)
{
	u64 new_amr_bits = 0x0ul;
	u64 new_iamr_bits = 0x0ul;
	u64 pkey_bits, uamor_pkey_bits;

	/*
	 * Check whether the key is disabled by UAMOR.
	 */
	pkey_bits = 0x3ul << pkeyshift(pkey);
	uamor_pkey_bits = (default_uamor & pkey_bits);

	/*
	 * Both the bits in UAMOR corresponding to the key should be set
	 */
	if (uamor_pkey_bits != pkey_bits)
		return -EINVAL;

	if (init_val & PKEY_DISABLE_EXECUTE) {
		if (!pkey_execute_disable_supported)
			return -EINVAL;
		new_iamr_bits |= IAMR_EX_BIT;
	}
	init_iamr(pkey, new_iamr_bits);

	/* Set the bits we need in AMR: */
	if (init_val & PKEY_DISABLE_ACCESS)
		new_amr_bits |= AMR_RD_BIT | AMR_WR_BIT;
	else if (init_val & PKEY_DISABLE_WRITE)
		new_amr_bits |= AMR_WR_BIT;

	init_amr(pkey, new_amr_bits);
	return 0;
}

void thread_pkey_regs_save(struct thread_struct *thread)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	/*
	 * TODO: Skip saving registers if @thread hasn't used any keys yet.
	 */
	thread->amr = read_amr();
	thread->iamr = read_iamr();
}

void thread_pkey_regs_restore(struct thread_struct *new_thread,
			      struct thread_struct *old_thread)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	if (old_thread->amr != new_thread->amr)
		write_amr(new_thread->amr);
	if (old_thread->iamr != new_thread->iamr)
		write_iamr(new_thread->iamr);
}

void thread_pkey_regs_init(struct thread_struct *thread)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	thread->amr   = default_amr;
	thread->iamr  = default_iamr;

	write_amr(default_amr);
	write_iamr(default_iamr);
}

int execute_only_pkey(struct mm_struct *mm)
{
	return mm->context.execute_only_pkey;
}

static inline bool vma_is_pkey_exec_only(struct vm_area_struct *vma)
{
	/* Do this check first since the vm_flags should be hot */
	if ((vma->vm_flags & VM_ACCESS_FLAGS) != VM_EXEC)
		return false;

	return (vma_pkey(vma) == vma->vm_mm->context.execute_only_pkey);
}

/*
 * This should only be called for *plain* mprotect calls.
 */
int __arch_override_mprotect_pkey(struct vm_area_struct *vma, int prot,
				  int pkey)
{
	/*
	 * If the currently associated pkey is execute-only, but the requested
	 * protection is not execute-only, move it back to the default pkey.
	 */
	if (vma_is_pkey_exec_only(vma) && (prot != PROT_EXEC))
		return 0;

	/*
	 * The requested protection is execute-only. Hence let's use an
	 * execute-only pkey.
	 */
	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(vma->vm_mm);
		if (pkey > 0)
			return pkey;
	}

	/* Nothing to override. */
	return vma_pkey(vma);
}

static bool pkey_access_permitted(int pkey, bool write, bool execute)
{
	int pkey_shift;
	u64 amr;

	pkey_shift = pkeyshift(pkey);
	if (execute)
		return !(read_iamr() & (IAMR_EX_BIT << pkey_shift));

	amr = read_amr();
	if (write)
		return !(amr & (AMR_WR_BIT << pkey_shift));

	return !(amr & (AMR_RD_BIT << pkey_shift));
}

bool arch_pte_access_permitted(u64 pte, bool write, bool execute)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return true;

	return pkey_access_permitted(pte_to_pkey_bits(pte), write, execute);
}

/*
 * We only want to enforce protection keys on the current thread because we
 * effectively have no access to AMR/IAMR for other threads or any way to tell
 * which AMR/IAMR in a threaded process we could use.
 *
 * So do not enforce things if the VMA is not from the current mm, or if we are
 * in a kernel thread.
 */
bool arch_vma_access_permitted(struct vm_area_struct *vma, bool write,
			       bool execute, bool foreign)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return true;
	/*
	 * Do not enforce our key-permissions on a foreign vma.
	 */
	if (foreign || vma_is_foreign(vma))
		return true;

	return pkey_access_permitted(vma_pkey(vma), write, execute);
}

void arch_dup_pkeys(struct mm_struct *oldmm, struct mm_struct *mm)
{
	if (!mmu_has_feature(MMU_FTR_PKEY))
		return;

	/* Duplicate the oldmm pkey state in mm: */
	mm_pkey_allocation_map(mm) = mm_pkey_allocation_map(oldmm);
	mm->context.execute_only_pkey = oldmm->context.execute_only_pkey;
}
