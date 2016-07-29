/*
 * Intel Memory Protection Keys management
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/mm_types.h>             /* mm_struct, vma, etc...       */
#include <linux/pkeys.h>                /* PKEY_*                       */
#include <uapi/asm-generic/mman-common.h>

#include <asm/cpufeature.h>             /* boot_cpu_has, ...            */
#include <asm/mmu_context.h>            /* vma_pkey()                   */
#include <asm/fpu/internal.h>           /* fpregs_active()              */

int __execute_only_pkey(struct mm_struct *mm)
{
	bool need_to_set_mm_pkey = false;
	int execute_only_pkey = mm->context.execute_only_pkey;
	int ret;

	/* Do we need to assign a pkey for mm's execute-only maps? */
	if (execute_only_pkey == -1) {
		/* Go allocate one to use, which might fail */
		execute_only_pkey = mm_pkey_alloc(mm);
		if (execute_only_pkey < 0)
			return -1;
		need_to_set_mm_pkey = true;
	}

	/*
	 * We do not want to go through the relatively costly
	 * dance to set PKRU if we do not need to.  Check it
	 * first and assume that if the execute-only pkey is
	 * write-disabled that we do not have to set it
	 * ourselves.  We need preempt off so that nobody
	 * can make fpregs inactive.
	 */
	preempt_disable();
	if (!need_to_set_mm_pkey &&
	    fpregs_active() &&
	    !__pkru_allows_read(read_pkru(), execute_only_pkey)) {
		preempt_enable();
		return execute_only_pkey;
	}
	preempt_enable();

	/*
	 * Set up PKRU so that it denies access for everything
	 * other than execution.
	 */
	ret = arch_set_user_pkey_access(current, execute_only_pkey,
			PKEY_DISABLE_ACCESS);
	/*
	 * If the PKRU-set operation failed somehow, just return
	 * 0 and effectively disable execute-only support.
	 */
	if (ret) {
		mm_set_pkey_free(mm, execute_only_pkey);
		return -1;
	}

	/* We got one, store it and use it from here on out */
	if (need_to_set_mm_pkey)
		mm->context.execute_only_pkey = execute_only_pkey;
	return execute_only_pkey;
}

static inline bool vma_is_pkey_exec_only(struct vm_area_struct *vma)
{
	/* Do this check first since the vm_flags should be hot */
	if ((vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) != VM_EXEC)
		return false;
	if (vma_pkey(vma) != vma->vm_mm->context.execute_only_pkey)
		return false;

	return true;
}

/*
 * This is only called for *plain* mprotect calls.
 */
int __arch_override_mprotect_pkey(struct vm_area_struct *vma, int prot, int pkey)
{
	/*
	 * Is this an mprotect_pkey() call?  If so, never
	 * override the value that came from the user.
	 */
	if (pkey != -1)
		return pkey;
	/*
	 * Look for a protection-key-drive execute-only mapping
	 * which is now being given permissions that are not
	 * execute-only.  Move it back to the default pkey.
	 */
	if (vma_is_pkey_exec_only(vma) &&
	    (prot & (PROT_READ|PROT_WRITE))) {
		return 0;
	}
	/*
	 * The mapping is execute-only.  Go try to get the
	 * execute-only protection key.  If we fail to do that,
	 * fall through as if we do not have execute-only
	 * support.
	 */
	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(vma->vm_mm);
		if (pkey > 0)
			return pkey;
	}
	/*
	 * This is a vanilla, non-pkey mprotect (or we failed to
	 * setup execute-only), inherit the pkey from the VMA we
	 * are working on.
	 */
	return vma_pkey(vma);
}

#define PKRU_AD_KEY(pkey)	(PKRU_AD_BIT << ((pkey) * PKRU_BITS_PER_PKEY))

/*
 * Make the default PKRU value (at execve() time) as restrictive
 * as possible.  This ensures that any threads clone()'d early
 * in the process's lifetime will not accidentally get access
 * to data which is pkey-protected later on.
 */
u32 init_pkru_value = PKRU_AD_KEY( 1) | PKRU_AD_KEY( 2) | PKRU_AD_KEY( 3) |
		      PKRU_AD_KEY( 4) | PKRU_AD_KEY( 5) | PKRU_AD_KEY( 6) |
		      PKRU_AD_KEY( 7) | PKRU_AD_KEY( 8) | PKRU_AD_KEY( 9) |
		      PKRU_AD_KEY(10) | PKRU_AD_KEY(11) | PKRU_AD_KEY(12) |
		      PKRU_AD_KEY(13) | PKRU_AD_KEY(14) | PKRU_AD_KEY(15);

/*
 * Called from the FPU code when creating a fresh set of FPU
 * registers.  This is called from a very specific context where
 * we know the FPU regstiers are safe for use and we can use PKRU
 * directly.  The fact that PKRU is only available when we are
 * using eagerfpu mode makes this possible.
 */
void copy_init_pkru_to_fpregs(void)
{
	u32 init_pkru_value_snapshot = READ_ONCE(init_pkru_value);
	/*
	 * Any write to PKRU takes it out of the XSAVE 'init
	 * state' which increases context switch cost.  Avoid
	 * writing 0 when PKRU was already 0.
	 */
	if (!init_pkru_value_snapshot && !read_pkru())
		return;
	/*
	 * Override the PKRU state that came from 'init_fpstate'
	 * with the baseline from the process.
	 */
	write_pkru(init_pkru_value_snapshot);
}
