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
	int ret;

	/*
	 * We do not want to go through the relatively costly
	 * dance to set PKRU if we do not need to.  Check it
	 * first and assume that if the execute-only pkey is
	 * write-disabled that we do not have to set it
	 * ourselves.  We need preempt off so that nobody
	 * can make fpregs inactive.
	 */
	preempt_disable();
	if (fpregs_active() &&
	    !__pkru_allows_read(read_pkru(), PKEY_DEDICATED_EXECUTE_ONLY)) {
		preempt_enable();
		return PKEY_DEDICATED_EXECUTE_ONLY;
	}
	preempt_enable();
	ret = arch_set_user_pkey_access(current, PKEY_DEDICATED_EXECUTE_ONLY,
			PKEY_DISABLE_ACCESS);
	/*
	 * If the PKRU-set operation failed somehow, just return
	 * 0 and effectively disable execute-only support.
	 */
	if (ret)
		return 0;

	return PKEY_DEDICATED_EXECUTE_ONLY;
}

static inline bool vma_is_pkey_exec_only(struct vm_area_struct *vma)
{
	/* Do this check first since the vm_flags should be hot */
	if ((vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) != VM_EXEC)
		return false;
	if (vma_pkey(vma) != PKEY_DEDICATED_EXECUTE_ONLY)
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
