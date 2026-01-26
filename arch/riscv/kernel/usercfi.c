// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Rivos, Inc.
 * Deepak Gupta <debug@rivosinc.com>
 */

#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/sizes.h>
#include <linux/user.h>
#include <linux/syscalls.h>
#include <linux/prctl.h>
#include <asm/csr.h>
#include <asm/usercfi.h>

#define SHSTK_ENTRY_SIZE sizeof(void *)

/*
 * Writes on shadow stack can either be `sspush` or `ssamoswap`. `sspush` can happen
 * implicitly on current shadow stack pointed to by CSR_SSP. `ssamoswap` takes pointer to
 * shadow stack. To keep it simple, we plan to use `ssamoswap` to perform writes on shadow
 * stack.
 */
static noinline unsigned long amo_user_shstk(unsigned long __user *addr, unsigned long val)
{
	/*
	 * Never expect -1 on shadow stack. Expect return addresses and zero
	 */
	unsigned long swap = -1;

	__enable_user_access();
	asm goto(".option push\n"
		".option arch, +zicfiss\n"
		"1: ssamoswap.d %[swap], %[val], %[addr]\n"
		_ASM_EXTABLE(1b, %l[fault])
		".option pop\n"
		 : [swap] "=r" (swap), [addr] "+A" (*(__force unsigned long *)addr)
		: [val] "r" (val)
		: "memory"
		: fault
		);
	__disable_user_access();
	return swap;
fault:
	__disable_user_access();
	return -1;
}

/*
 * Create a restore token on the shadow stack.  A token is always XLEN wide
 * and aligned to XLEN.
 */
static int create_rstor_token(unsigned long ssp, unsigned long *token_addr)
{
	unsigned long addr;

	/* Token must be aligned */
	if (!IS_ALIGNED(ssp, SHSTK_ENTRY_SIZE))
		return -EINVAL;

	/* On RISC-V we're constructing token to be function of address itself */
	addr = ssp - SHSTK_ENTRY_SIZE;

	if (amo_user_shstk((unsigned long __user *)addr, (unsigned long)ssp) == -1)
		return -EFAULT;

	if (token_addr)
		*token_addr = addr;

	return 0;
}

static unsigned long allocate_shadow_stack(unsigned long addr, unsigned long size,
					   unsigned long token_offset, bool set_tok)
{
	int flags = MAP_ANONYMOUS | MAP_PRIVATE;
	struct mm_struct *mm = current->mm;
	unsigned long populate;

	if (addr)
		flags |= MAP_FIXED_NOREPLACE;

	mmap_write_lock(mm);
	addr = do_mmap(NULL, addr, size, PROT_READ, flags,
		       VM_SHADOW_STACK | VM_WRITE, 0, &populate, NULL);
	mmap_write_unlock(mm);

	if (!set_tok || IS_ERR_VALUE(addr))
		goto out;

	if (create_rstor_token(addr + token_offset, NULL)) {
		vm_munmap(addr, size);
		return -EINVAL;
	}

out:
	return addr;
}

SYSCALL_DEFINE3(map_shadow_stack, unsigned long, addr, unsigned long, size, unsigned int, flags)
{
	bool set_tok = flags & SHADOW_STACK_SET_TOKEN;
	unsigned long aligned_size = 0;

	if (!cpu_supports_shadow_stack())
		return -EOPNOTSUPP;

	/* Anything other than set token should result in invalid param */
	if (flags & ~SHADOW_STACK_SET_TOKEN)
		return -EINVAL;

	/*
	 * Unlike other architectures, on RISC-V, SSP pointer is held in CSR_SSP and is an available
	 * CSR in all modes. CSR accesses are performed using 12bit index programmed in instruction
	 * itself. This provides static property on register programming and writes to CSR can't
	 * be unintentional from programmer's perspective. As long as programmer has guarded areas
	 * which perform writes to CSR_SSP properly, shadow stack pivoting is not possible. Since
	 * CSR_SSP is writable by user mode, it itself can setup a shadow stack token subsequent
	 * to allocation. Although in order to provide portablity with other architectures (because
	 * `map_shadow_stack` is arch agnostic syscall), RISC-V will follow expectation of a token
	 * flag in flags and if provided in flags, will setup a token at the base.
	 */

	/* If there isn't space for a token */
	if (set_tok && size < SHSTK_ENTRY_SIZE)
		return -ENOSPC;

	if (addr && (addr & (PAGE_SIZE - 1)))
		return -EINVAL;

	aligned_size = PAGE_ALIGN(size);
	if (aligned_size < size)
		return -EOVERFLOW;

	return allocate_shadow_stack(addr, aligned_size, size, set_tok);
}
