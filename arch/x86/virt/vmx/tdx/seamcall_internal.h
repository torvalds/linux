/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SEAMCALL utilities for TDX host-side operations.
 *
 * Provides convenient wrappers around SEAMCALL assembly with retry logic,
 * error reporting and cache coherency tracking.
 *
 * Copyright (C) 2021-2023 Intel Corporation
 */

#ifndef _X86_VIRT_SEAMCALL_INTERNAL_H
#define _X86_VIRT_SEAMCALL_INTERNAL_H

#include <linux/printk.h>
#include <linux/types.h>
#include <asm/archrandom.h>
#include <asm/processor.h>
#include <asm/tdx.h>

u64 __seamcall(u64 fn, struct tdx_module_args *args);
u64 __seamcall_ret(u64 fn, struct tdx_module_args *args);
u64 __seamcall_saved_ret(u64 fn, struct tdx_module_args *args);

typedef u64 (*sc_func_t)(u64 fn, struct tdx_module_args *args);

static __always_inline u64 __seamcall_dirty_cache(sc_func_t func, u64 fn,
						  struct tdx_module_args *args)
{
	lockdep_assert_preemption_disabled();

	/*
	 * SEAMCALLs are made to the TDX module and can generate dirty
	 * cachelines of TDX private memory.  Mark cache state incoherent
	 * so that the cache can be flushed during kexec.
	 *
	 * This needs to be done before actually making the SEAMCALL,
	 * because kexec-ing CPU could send NMI to stop remote CPUs,
	 * in which case even disabling IRQ won't help here.
	 */
	this_cpu_write(cache_state_incoherent, true);

	return func(fn, args);
}

static __always_inline u64 sc_retry(sc_func_t func, u64 fn,
			   struct tdx_module_args *args)
{
	int retry = RDRAND_RETRY_LOOPS;
	u64 ret;

	do {
		preempt_disable();
		ret = __seamcall_dirty_cache(func, fn, args);
		preempt_enable();
	} while (ret == TDX_RND_NO_ENTROPY && --retry);

	return ret;
}

#define seamcall(_fn, _args)		sc_retry(__seamcall, (_fn), (_args))
#define seamcall_ret(_fn, _args)	sc_retry(__seamcall_ret, (_fn), (_args))
#define seamcall_saved_ret(_fn, _args)	sc_retry(__seamcall_saved_ret, (_fn), (_args))

typedef void (*sc_err_func_t)(u64 fn, u64 err, struct tdx_module_args *args);

static inline void seamcall_err(u64 fn, u64 err, struct tdx_module_args *args)
{
	pr_err("SEAMCALL (0x%016llx) failed: 0x%016llx\n", fn, err);
}

static inline void seamcall_err_ret(u64 fn, u64 err,
				    struct tdx_module_args *args)
{
	seamcall_err(fn, err, args);
	pr_err("RCX 0x%016llx RDX 0x%016llx R08 0x%016llx\n",
			args->rcx, args->rdx, args->r8);
	pr_err("R09 0x%016llx R10 0x%016llx R11 0x%016llx\n",
			args->r9, args->r10, args->r11);
}

static __always_inline int sc_retry_prerr(sc_func_t func,
					  sc_err_func_t err_func,
					  u64 fn, struct tdx_module_args *args)
{
	u64 sret = sc_retry(func, fn, args);

	if (sret == TDX_SUCCESS)
		return 0;

	if (sret == TDX_SEAMCALL_VMFAILINVALID)
		return -ENODEV;

	if (sret == TDX_SEAMCALL_GP)
		return -EOPNOTSUPP;

	if (sret == TDX_SEAMCALL_UD)
		return -EACCES;

	err_func(fn, sret, args);
	return -EIO;
}

#define seamcall_prerr(__fn, __args)						\
	sc_retry_prerr(__seamcall, seamcall_err, (__fn), (__args))

#define seamcall_prerr_ret(__fn, __args)					\
	sc_retry_prerr(__seamcall_ret, seamcall_err_ret, (__fn), (__args))

#endif /* _X86_VIRT_SEAMCALL_INTERNAL_H */
