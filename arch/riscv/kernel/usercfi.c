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

unsigned long riscv_nousercfi __read_mostly;

#define SHSTK_ENTRY_SIZE sizeof(void *)

bool is_shstk_enabled(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.ubcfi_en;
}

bool is_shstk_allocated(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.shdw_stk_base;
}

bool is_shstk_locked(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.ubcfi_locked;
}

void set_shstk_base(struct task_struct *task, unsigned long shstk_addr, unsigned long size)
{
	task->thread_info.user_cfi_state.shdw_stk_base = shstk_addr;
	task->thread_info.user_cfi_state.shdw_stk_size = size;
}

unsigned long get_shstk_base(struct task_struct *task, unsigned long *size)
{
	if (size)
		*size = task->thread_info.user_cfi_state.shdw_stk_size;
	return task->thread_info.user_cfi_state.shdw_stk_base;
}

void set_active_shstk(struct task_struct *task, unsigned long shstk_addr)
{
	task->thread_info.user_cfi_state.user_shdw_stk = shstk_addr;
}

unsigned long get_active_shstk(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.user_shdw_stk;
}

void set_shstk_status(struct task_struct *task, bool enable)
{
	if (!is_user_shstk_enabled())
		return;

	task->thread_info.user_cfi_state.ubcfi_en = enable ? 1 : 0;

	if (enable)
		task->thread.envcfg |= ENVCFG_SSE;
	else
		task->thread.envcfg &= ~ENVCFG_SSE;

	csr_write(CSR_ENVCFG, task->thread.envcfg);
}

void set_shstk_lock(struct task_struct *task)
{
	task->thread_info.user_cfi_state.ubcfi_locked = 1;
}

bool is_indir_lp_enabled(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.ufcfi_en;
}

bool is_indir_lp_locked(struct task_struct *task)
{
	return task->thread_info.user_cfi_state.ufcfi_locked;
}

void set_indir_lp_status(struct task_struct *task, bool enable)
{
	if (!is_user_lpad_enabled())
		return;

	task->thread_info.user_cfi_state.ufcfi_en = enable ? 1 : 0;

	if (enable)
		task->thread.envcfg |= ENVCFG_LPE;
	else
		task->thread.envcfg &= ~ENVCFG_LPE;

	csr_write(CSR_ENVCFG, task->thread.envcfg);
}

void set_indir_lp_lock(struct task_struct *task)
{
	task->thread_info.user_cfi_state.ufcfi_locked = 1;
}
/*
 * If size is 0, then to be compatible with regular stack we want it to be as big as
 * regular stack. Else PAGE_ALIGN it and return back
 */
static unsigned long calc_shstk_size(unsigned long size)
{
	if (size)
		return PAGE_ALIGN(size);

	return PAGE_ALIGN(min_t(unsigned long long, rlimit(RLIMIT_STACK), SZ_4G));
}

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

/*
 * Save user shadow stack pointer on the shadow stack itself and return a pointer to saved location.
 * Returns -EFAULT if unsuccessful.
 */
int save_user_shstk(struct task_struct *tsk, unsigned long *saved_shstk_ptr)
{
	unsigned long ss_ptr = 0;
	unsigned long token_loc = 0;
	int ret = 0;

	if (!saved_shstk_ptr)
		return -EINVAL;

	ss_ptr = get_active_shstk(tsk);
	ret = create_rstor_token(ss_ptr, &token_loc);

	if (!ret) {
		*saved_shstk_ptr = token_loc;
		set_active_shstk(tsk, token_loc);
	}

	return ret;
}

/*
 * Restores the user shadow stack pointer from the token on the shadow stack for task 'tsk'.
 * Returns -EFAULT if unsuccessful.
 */
int restore_user_shstk(struct task_struct *tsk, unsigned long shstk_ptr)
{
	unsigned long token = 0;

	token = amo_user_shstk((unsigned long __user *)shstk_ptr, 0);

	if (token == -1)
		return -EFAULT;

	/* invalid token, return EINVAL */
	if ((token - shstk_ptr) != SHSTK_ENTRY_SIZE) {
		pr_info_ratelimited("%s[%d]: bad restore token in %s: pc=%p sp=%p, token=%p, shstk_ptr=%p\n",
				    tsk->comm, task_pid_nr(tsk), __func__,
				    (void *)(task_pt_regs(tsk)->epc),
				    (void *)(task_pt_regs(tsk)->sp),
				    (void *)token, (void *)shstk_ptr);
		return -EINVAL;
	}

	/* all checks passed, set active shstk and return success */
	set_active_shstk(tsk, token);
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

	if (!is_user_shstk_enabled())
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

/*
 * This gets called during clone/clone3/fork. And is needed to allocate a shadow stack for
 * cases where CLONE_VM is specified and thus a different stack is specified by user. We
 * thus need a separate shadow stack too. How a separate shadow stack is specified by
 * user is still being debated. Once that's settled, remove this part of the comment.
 * This function simply returns 0 if shadow stacks are not supported or if separate shadow
 * stack allocation is not needed (like in case of !CLONE_VM)
 */
unsigned long shstk_alloc_thread_stack(struct task_struct *tsk,
				       const struct kernel_clone_args *args)
{
	unsigned long addr, size;

	/* If shadow stack is not supported, return 0 */
	if (!is_user_shstk_enabled())
		return 0;

	/*
	 * If shadow stack is not enabled on the new thread, skip any
	 * switch to a new shadow stack.
	 */
	if (!is_shstk_enabled(tsk))
		return 0;

	/*
	 * For CLONE_VFORK the child will share the parents shadow stack.
	 * Set base = 0 and size = 0, this is special means to track this state
	 * so the freeing logic run for child knows to leave it alone.
	 */
	if (args->flags & CLONE_VFORK) {
		set_shstk_base(tsk, 0, 0);
		return 0;
	}

	/*
	 * For !CLONE_VM the child will use a copy of the parents shadow
	 * stack.
	 */
	if (!(args->flags & CLONE_VM))
		return 0;

	/*
	 * reaching here means, CLONE_VM was specified and thus a separate shadow
	 * stack is needed for new cloned thread. Note: below allocation is happening
	 * using current mm.
	 */
	size = calc_shstk_size(args->stack_size);
	addr = allocate_shadow_stack(0, size, 0, false);
	if (IS_ERR_VALUE(addr))
		return addr;

	set_shstk_base(tsk, addr, size);

	return addr + size;
}

void shstk_release(struct task_struct *tsk)
{
	unsigned long base = 0, size = 0;
	/* If shadow stack is not supported or not enabled, nothing to release */
	if (!is_user_shstk_enabled() || !is_shstk_enabled(tsk))
		return;

	/*
	 * When fork() with CLONE_VM fails, the child (tsk) already has a
	 * shadow stack allocated, and exit_thread() calls this function to
	 * free it.  In this case the parent (current) and the child share
	 * the same mm struct. Move forward only when they're same.
	 */
	if (!tsk->mm || tsk->mm != current->mm)
		return;

	/*
	 * We know shadow stack is enabled but if base is NULL, then
	 * this task is not managing its own shadow stack (CLONE_VFORK). So
	 * skip freeing it.
	 */
	base = get_shstk_base(tsk, &size);
	if (!base)
		return;

	vm_munmap(base, size);
	set_shstk_base(tsk, 0, 0);
}

int arch_get_shadow_stack_status(struct task_struct *t, unsigned long __user *status)
{
	unsigned long bcfi_status = 0;

	if (!is_user_shstk_enabled())
		return -EINVAL;

	/* this means shadow stack is enabled on the task */
	bcfi_status |= (is_shstk_enabled(t) ? PR_SHADOW_STACK_ENABLE : 0);

	return copy_to_user(status, &bcfi_status, sizeof(bcfi_status)) ? -EFAULT : 0;
}

int arch_set_shadow_stack_status(struct task_struct *t, unsigned long status)
{
	unsigned long size = 0, addr = 0;
	bool enable_shstk = false;

	if (!is_user_shstk_enabled())
		return -EINVAL;

	/* Reject unknown flags */
	if (status & ~PR_SHADOW_STACK_SUPPORTED_STATUS_MASK)
		return -EINVAL;

	/* bcfi status is locked and further can't be modified by user */
	if (is_shstk_locked(t))
		return -EINVAL;

	enable_shstk = status & PR_SHADOW_STACK_ENABLE;
	/* Request is to enable shadow stack and shadow stack is not enabled already */
	if (enable_shstk && !is_shstk_enabled(t)) {
		/* shadow stack was allocated and enable request again
		 * no need to support such usecase and return EINVAL.
		 */
		if (is_shstk_allocated(t))
			return -EINVAL;

		size = calc_shstk_size(0);
		addr = allocate_shadow_stack(0, size, 0, false);
		if (IS_ERR_VALUE(addr))
			return -ENOMEM;
		set_shstk_base(t, addr, size);
		set_active_shstk(t, addr + size);
	}

	/*
	 * If a request to disable shadow stack happens, let's go ahead and release it
	 * Although, if CLONE_VFORKed child did this, then in that case we will end up
	 * not releasing the shadow stack (because it might be needed in parent). Although
	 * we will disable it for VFORKed child. And if VFORKed child tries to enable again
	 * then in that case, it'll get entirely new shadow stack because following condition
	 * are true
	 *  - shadow stack was not enabled for vforked child
	 *  - shadow stack base was anyways pointing to 0
	 * This shouldn't be a big issue because we want parent to have availability of shadow
	 * stack whenever VFORKed child releases resources via exit or exec but at the same
	 * time we want VFORKed child to break away and establish new shadow stack if it desires
	 *
	 */
	if (!enable_shstk)
		shstk_release(t);

	set_shstk_status(t, enable_shstk);
	return 0;
}

int arch_lock_shadow_stack_status(struct task_struct *task,
				  unsigned long arg)
{
	/* If shtstk not supported or not enabled on task, nothing to lock here */
	if (!is_user_shstk_enabled() ||
	    !is_shstk_enabled(task) || arg != 0)
		return -EINVAL;

	set_shstk_lock(task);

	return 0;
}

int arch_get_indir_br_lp_status(struct task_struct *t, unsigned long __user *status)
{
	unsigned long fcfi_status = 0;

	if (!is_user_lpad_enabled())
		return -EINVAL;

	/* indirect branch tracking is enabled on the task or not */
	fcfi_status |= (is_indir_lp_enabled(t) ? PR_INDIR_BR_LP_ENABLE : 0);

	return copy_to_user(status, &fcfi_status, sizeof(fcfi_status)) ? -EFAULT : 0;
}

int arch_set_indir_br_lp_status(struct task_struct *t, unsigned long status)
{
	bool enable_indir_lp = false;

	if (!is_user_lpad_enabled())
		return -EINVAL;

	/* indirect branch tracking is locked and further can't be modified by user */
	if (is_indir_lp_locked(t))
		return -EINVAL;

	/* Reject unknown flags */
	if (status & ~PR_INDIR_BR_LP_ENABLE)
		return -EINVAL;

	enable_indir_lp = (status & PR_INDIR_BR_LP_ENABLE);
	set_indir_lp_status(t, enable_indir_lp);

	return 0;
}

int arch_lock_indir_br_lp_status(struct task_struct *task,
				 unsigned long arg)
{
	/*
	 * If indirect branch tracking is not supported or not enabled on task,
	 * nothing to lock here
	 */
	if (!is_user_lpad_enabled() ||
	    !is_indir_lp_enabled(task) || arg != 0)
		return -EINVAL;

	set_indir_lp_lock(task);

	return 0;
}

bool is_user_shstk_enabled(void)
{
	return (cpu_supports_shadow_stack() &&
		!(riscv_nousercfi & CMDLINE_DISABLE_RISCV_USERCFI_BCFI));
}

bool is_user_lpad_enabled(void)
{
	return (cpu_supports_indirect_br_lp_instr() &&
		!(riscv_nousercfi & CMDLINE_DISABLE_RISCV_USERCFI_FCFI));
}

static int __init setup_global_riscv_enable(char *str)
{
	if (strcmp(str, "all") == 0)
		riscv_nousercfi = CMDLINE_DISABLE_RISCV_USERCFI;

	if (strcmp(str, "fcfi") == 0)
		riscv_nousercfi |= CMDLINE_DISABLE_RISCV_USERCFI_FCFI;

	if (strcmp(str, "bcfi") == 0)
		riscv_nousercfi |= CMDLINE_DISABLE_RISCV_USERCFI_BCFI;

	if (riscv_nousercfi)
		pr_info("RISC-V user CFI disabled via cmdline - shadow stack status : %s, landing pad status : %s\n",
			(riscv_nousercfi & CMDLINE_DISABLE_RISCV_USERCFI_BCFI) ? "disabled" :
			"enabled", (riscv_nousercfi & CMDLINE_DISABLE_RISCV_USERCFI_FCFI) ?
			"disabled" : "enabled");

	return 1;
}

__setup("riscv_nousercfi=", setup_global_riscv_enable);
