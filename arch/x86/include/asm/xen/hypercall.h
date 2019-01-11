/******************************************************************************
 * hypercall.h
 *
 * Linux-specific hypervisor handling.
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _ASM_X86_XEN_HYPERCALL_H
#define _ASM_X86_XEN_HYPERCALL_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#include <trace/events/xen.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/smap.h>
#include <asm/nospec-branch.h>

#include <xen/interface/xen.h>
#include <xen/interface/sched.h>
#include <xen/interface/physdev.h>
#include <xen/interface/platform.h>
#include <xen/interface/xen-mca.h>

struct xen_dm_op_buf;

/*
 * The hypercall asms have to meet several constraints:
 * - Work on 32- and 64-bit.
 *    The two architectures put their arguments in different sets of
 *    registers.
 *
 * - Work around asm syntax quirks
 *    It isn't possible to specify one of the rNN registers in a
 *    constraint, so we use explicit register variables to get the
 *    args into the right place.
 *
 * - Mark all registers as potentially clobbered
 *    Even unused parameters can be clobbered by the hypervisor, so we
 *    need to make sure gcc knows it.
 *
 * - Avoid compiler bugs.
 *    This is the tricky part.  Because x86_32 has such a constrained
 *    register set, gcc versions below 4.3 have trouble generating
 *    code when all the arg registers and memory are trashed by the
 *    asm.  There are syntactically simpler ways of achieving the
 *    semantics below, but they cause the compiler to crash.
 *
 *    The only combination I found which works is:
 *     - assign the __argX variables first
 *     - list all actually used parameters as "+r" (__argX)
 *     - clobber the rest
 *
 * The result certainly isn't pretty, and it really shows up cpp's
 * weakness as as macro language.  Sorry.  (But let's just give thanks
 * there aren't more than 5 arguments...)
 */

extern struct { char _entry[32]; } hypercall_page[];

#define __HYPERCALL		"call hypercall_page+%c[offset]"
#define __HYPERCALL_ENTRY(x)						\
	[offset] "i" (__HYPERVISOR_##x * sizeof(hypercall_page[0]))

#ifdef CONFIG_X86_32
#define __HYPERCALL_RETREG	"eax"
#define __HYPERCALL_ARG1REG	"ebx"
#define __HYPERCALL_ARG2REG	"ecx"
#define __HYPERCALL_ARG3REG	"edx"
#define __HYPERCALL_ARG4REG	"esi"
#define __HYPERCALL_ARG5REG	"edi"
#else
#define __HYPERCALL_RETREG	"rax"
#define __HYPERCALL_ARG1REG	"rdi"
#define __HYPERCALL_ARG2REG	"rsi"
#define __HYPERCALL_ARG3REG	"rdx"
#define __HYPERCALL_ARG4REG	"r10"
#define __HYPERCALL_ARG5REG	"r8"
#endif

#define __HYPERCALL_DECLS						\
	register unsigned long __res  asm(__HYPERCALL_RETREG);		\
	register unsigned long __arg1 asm(__HYPERCALL_ARG1REG) = __arg1; \
	register unsigned long __arg2 asm(__HYPERCALL_ARG2REG) = __arg2; \
	register unsigned long __arg3 asm(__HYPERCALL_ARG3REG) = __arg3; \
	register unsigned long __arg4 asm(__HYPERCALL_ARG4REG) = __arg4; \
	register unsigned long __arg5 asm(__HYPERCALL_ARG5REG) = __arg5;

#define __HYPERCALL_0PARAM	"=r" (__res), ASM_CALL_CONSTRAINT
#define __HYPERCALL_1PARAM	__HYPERCALL_0PARAM, "+r" (__arg1)
#define __HYPERCALL_2PARAM	__HYPERCALL_1PARAM, "+r" (__arg2)
#define __HYPERCALL_3PARAM	__HYPERCALL_2PARAM, "+r" (__arg3)
#define __HYPERCALL_4PARAM	__HYPERCALL_3PARAM, "+r" (__arg4)
#define __HYPERCALL_5PARAM	__HYPERCALL_4PARAM, "+r" (__arg5)

#define __HYPERCALL_0ARG()
#define __HYPERCALL_1ARG(a1)						\
	__HYPERCALL_0ARG()		__arg1 = (unsigned long)(a1);
#define __HYPERCALL_2ARG(a1,a2)						\
	__HYPERCALL_1ARG(a1)		__arg2 = (unsigned long)(a2);
#define __HYPERCALL_3ARG(a1,a2,a3)					\
	__HYPERCALL_2ARG(a1,a2)		__arg3 = (unsigned long)(a3);
#define __HYPERCALL_4ARG(a1,a2,a3,a4)					\
	__HYPERCALL_3ARG(a1,a2,a3)	__arg4 = (unsigned long)(a4);
#define __HYPERCALL_5ARG(a1,a2,a3,a4,a5)				\
	__HYPERCALL_4ARG(a1,a2,a3,a4)	__arg5 = (unsigned long)(a5);

#define __HYPERCALL_CLOBBER5	"memory"
#define __HYPERCALL_CLOBBER4	__HYPERCALL_CLOBBER5, __HYPERCALL_ARG5REG
#define __HYPERCALL_CLOBBER3	__HYPERCALL_CLOBBER4, __HYPERCALL_ARG4REG
#define __HYPERCALL_CLOBBER2	__HYPERCALL_CLOBBER3, __HYPERCALL_ARG3REG
#define __HYPERCALL_CLOBBER1	__HYPERCALL_CLOBBER2, __HYPERCALL_ARG2REG
#define __HYPERCALL_CLOBBER0	__HYPERCALL_CLOBBER1, __HYPERCALL_ARG1REG

#define _hypercall0(type, name)						\
({									\
	__HYPERCALL_DECLS;						\
	__HYPERCALL_0ARG();						\
	asm volatile (__HYPERCALL					\
		      : __HYPERCALL_0PARAM				\
		      : __HYPERCALL_ENTRY(name)				\
		      : __HYPERCALL_CLOBBER0);				\
	(type)__res;							\
})

#define _hypercall1(type, name, a1)					\
({									\
	__HYPERCALL_DECLS;						\
	__HYPERCALL_1ARG(a1);						\
	asm volatile (__HYPERCALL					\
		      : __HYPERCALL_1PARAM				\
		      : __HYPERCALL_ENTRY(name)				\
		      : __HYPERCALL_CLOBBER1);				\
	(type)__res;							\
})

#define _hypercall2(type, name, a1, a2)					\
({									\
	__HYPERCALL_DECLS;						\
	__HYPERCALL_2ARG(a1, a2);					\
	asm volatile (__HYPERCALL					\
		      : __HYPERCALL_2PARAM				\
		      : __HYPERCALL_ENTRY(name)				\
		      : __HYPERCALL_CLOBBER2);				\
	(type)__res;							\
})

#define _hypercall3(type, name, a1, a2, a3)				\
({									\
	__HYPERCALL_DECLS;						\
	__HYPERCALL_3ARG(a1, a2, a3);					\
	asm volatile (__HYPERCALL					\
		      : __HYPERCALL_3PARAM				\
		      : __HYPERCALL_ENTRY(name)				\
		      : __HYPERCALL_CLOBBER3);				\
	(type)__res;							\
})

#define _hypercall4(type, name, a1, a2, a3, a4)				\
({									\
	__HYPERCALL_DECLS;						\
	__HYPERCALL_4ARG(a1, a2, a3, a4);				\
	asm volatile (__HYPERCALL					\
		      : __HYPERCALL_4PARAM				\
		      : __HYPERCALL_ENTRY(name)				\
		      : __HYPERCALL_CLOBBER4);				\
	(type)__res;							\
})

static inline long
xen_single_call(unsigned int call,
		unsigned long a1, unsigned long a2,
		unsigned long a3, unsigned long a4,
		unsigned long a5)
{
	__HYPERCALL_DECLS;
	__HYPERCALL_5ARG(a1, a2, a3, a4, a5);

	asm volatile(CALL_NOSPEC
		     : __HYPERCALL_5PARAM
		     : [thunk_target] "a" (&hypercall_page[call])
		     : __HYPERCALL_CLOBBER5);

	return (long)__res;
}

static inline long
privcmd_call(unsigned int call,
	     unsigned long a1, unsigned long a2,
	     unsigned long a3, unsigned long a4,
	     unsigned long a5)
{
	long res;

	stac();
	res = xen_single_call(call, a1, a2, a3, a4, a5);
	clac();

	return res;
}

static inline int
HYPERVISOR_set_trap_table(struct trap_info *table)
{
	return _hypercall1(int, set_trap_table, table);
}

static inline int
HYPERVISOR_mmu_update(struct mmu_update *req, int count,
		      int *success_count, domid_t domid)
{
	return _hypercall4(int, mmu_update, req, count, success_count, domid);
}

static inline int
HYPERVISOR_mmuext_op(struct mmuext_op *op, int count,
		     int *success_count, domid_t domid)
{
	return _hypercall4(int, mmuext_op, op, count, success_count, domid);
}

static inline int
HYPERVISOR_set_gdt(unsigned long *frame_list, int entries)
{
	return _hypercall2(int, set_gdt, frame_list, entries);
}

static inline int
HYPERVISOR_callback_op(int cmd, void *arg)
{
	return _hypercall2(int, callback_op, cmd, arg);
}

static inline int
HYPERVISOR_sched_op(int cmd, void *arg)
{
	return _hypercall2(int, sched_op, cmd, arg);
}

static inline long
HYPERVISOR_set_timer_op(u64 timeout)
{
	unsigned long timeout_hi = (unsigned long)(timeout>>32);
	unsigned long timeout_lo = (unsigned long)timeout;
	return _hypercall2(long, set_timer_op, timeout_lo, timeout_hi);
}

static inline int
HYPERVISOR_mca(struct xen_mc *mc_op)
{
	mc_op->interface_version = XEN_MCA_INTERFACE_VERSION;
	return _hypercall1(int, mca, mc_op);
}

static inline int
HYPERVISOR_platform_op(struct xen_platform_op *op)
{
	op->interface_version = XENPF_INTERFACE_VERSION;
	return _hypercall1(int, platform_op, op);
}

static inline int
HYPERVISOR_set_debugreg(int reg, unsigned long value)
{
	return _hypercall2(int, set_debugreg, reg, value);
}

static inline unsigned long
HYPERVISOR_get_debugreg(int reg)
{
	return _hypercall1(unsigned long, get_debugreg, reg);
}

static inline int
HYPERVISOR_update_descriptor(u64 ma, u64 desc)
{
	if (sizeof(u64) == sizeof(long))
		return _hypercall2(int, update_descriptor, ma, desc);
	return _hypercall4(int, update_descriptor, ma, ma>>32, desc, desc>>32);
}

static inline long
HYPERVISOR_memory_op(unsigned int cmd, void *arg)
{
	return _hypercall2(long, memory_op, cmd, arg);
}

static inline int
HYPERVISOR_multicall(void *call_list, uint32_t nr_calls)
{
	return _hypercall2(int, multicall, call_list, nr_calls);
}

static inline int
HYPERVISOR_update_va_mapping(unsigned long va, pte_t new_val,
			     unsigned long flags)
{
	if (sizeof(new_val) == sizeof(long))
		return _hypercall3(int, update_va_mapping, va,
				   new_val.pte, flags);
	else
		return _hypercall4(int, update_va_mapping, va,
				   new_val.pte, new_val.pte >> 32, flags);
}
extern int __must_check xen_event_channel_op_compat(int, void *);

static inline int
HYPERVISOR_event_channel_op(int cmd, void *arg)
{
	int rc = _hypercall2(int, event_channel_op, cmd, arg);
	if (unlikely(rc == -ENOSYS))
		rc = xen_event_channel_op_compat(cmd, arg);
	return rc;
}

static inline int
HYPERVISOR_xen_version(int cmd, void *arg)
{
	return _hypercall2(int, xen_version, cmd, arg);
}

static inline int
HYPERVISOR_console_io(int cmd, int count, char *str)
{
	return _hypercall3(int, console_io, cmd, count, str);
}

extern int __must_check xen_physdev_op_compat(int, void *);

static inline int
HYPERVISOR_physdev_op(int cmd, void *arg)
{
	int rc = _hypercall2(int, physdev_op, cmd, arg);
	if (unlikely(rc == -ENOSYS))
		rc = xen_physdev_op_compat(cmd, arg);
	return rc;
}

static inline int
HYPERVISOR_grant_table_op(unsigned int cmd, void *uop, unsigned int count)
{
	return _hypercall3(int, grant_table_op, cmd, uop, count);
}

static inline int
HYPERVISOR_vm_assist(unsigned int cmd, unsigned int type)
{
	return _hypercall2(int, vm_assist, cmd, type);
}

static inline int
HYPERVISOR_vcpu_op(int cmd, int vcpuid, void *extra_args)
{
	return _hypercall3(int, vcpu_op, cmd, vcpuid, extra_args);
}

#ifdef CONFIG_X86_64
static inline int
HYPERVISOR_set_segment_base(int reg, unsigned long value)
{
	return _hypercall2(int, set_segment_base, reg, value);
}
#endif

static inline int
HYPERVISOR_suspend(unsigned long start_info_mfn)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_suspend };

	/*
	 * For a PV guest the tools require that the start_info mfn be
	 * present in rdx/edx when the hypercall is made. Per the
	 * hypercall calling convention this is the third hypercall
	 * argument, which is start_info_mfn here.
	 */
	return _hypercall3(int, sched_op, SCHEDOP_shutdown, &r, start_info_mfn);
}

static inline unsigned long __must_check
HYPERVISOR_hvm_op(int op, void *arg)
{
       return _hypercall2(unsigned long, hvm_op, op, arg);
}

static inline int
HYPERVISOR_tmem_op(
	struct tmem_op *op)
{
	return _hypercall1(int, tmem_op, op);
}

static inline int
HYPERVISOR_xenpmu_op(unsigned int op, void *arg)
{
	return _hypercall2(int, xenpmu_op, op, arg);
}

static inline int
HYPERVISOR_dm_op(
	domid_t dom, unsigned int nr_bufs, struct xen_dm_op_buf *bufs)
{
	int ret;
	stac();
	ret = _hypercall3(int, dm_op, dom, nr_bufs, bufs);
	clac();
	return ret;
}

static inline void
MULTI_fpu_taskswitch(struct multicall_entry *mcl, int set)
{
	mcl->op = __HYPERVISOR_fpu_taskswitch;
	mcl->args[0] = set;

	trace_xen_mc_entry(mcl, 1);
}

static inline void
MULTI_update_va_mapping(struct multicall_entry *mcl, unsigned long va,
			pte_t new_val, unsigned long flags)
{
	mcl->op = __HYPERVISOR_update_va_mapping;
	mcl->args[0] = va;
	if (sizeof(new_val) == sizeof(long)) {
		mcl->args[1] = new_val.pte;
		mcl->args[2] = flags;
	} else {
		mcl->args[1] = new_val.pte;
		mcl->args[2] = new_val.pte >> 32;
		mcl->args[3] = flags;
	}

	trace_xen_mc_entry(mcl, sizeof(new_val) == sizeof(long) ? 3 : 4);
}

static inline void
MULTI_update_descriptor(struct multicall_entry *mcl, u64 maddr,
			struct desc_struct desc)
{
	mcl->op = __HYPERVISOR_update_descriptor;
	if (sizeof(maddr) == sizeof(long)) {
		mcl->args[0] = maddr;
		mcl->args[1] = *(unsigned long *)&desc;
	} else {
		u32 *p = (u32 *)&desc;

		mcl->args[0] = maddr;
		mcl->args[1] = maddr >> 32;
		mcl->args[2] = *p++;
		mcl->args[3] = *p;
	}

	trace_xen_mc_entry(mcl, sizeof(maddr) == sizeof(long) ? 2 : 4);
}

static inline void
MULTI_mmu_update(struct multicall_entry *mcl, struct mmu_update *req,
		 int count, int *success_count, domid_t domid)
{
	mcl->op = __HYPERVISOR_mmu_update;
	mcl->args[0] = (unsigned long)req;
	mcl->args[1] = count;
	mcl->args[2] = (unsigned long)success_count;
	mcl->args[3] = domid;

	trace_xen_mc_entry(mcl, 4);
}

static inline void
MULTI_mmuext_op(struct multicall_entry *mcl, struct mmuext_op *op, int count,
		int *success_count, domid_t domid)
{
	mcl->op = __HYPERVISOR_mmuext_op;
	mcl->args[0] = (unsigned long)op;
	mcl->args[1] = count;
	mcl->args[2] = (unsigned long)success_count;
	mcl->args[3] = domid;

	trace_xen_mc_entry(mcl, 4);
}

static inline void
MULTI_stack_switch(struct multicall_entry *mcl,
		   unsigned long ss, unsigned long esp)
{
	mcl->op = __HYPERVISOR_stack_switch;
	mcl->args[0] = ss;
	mcl->args[1] = esp;

	trace_xen_mc_entry(mcl, 2);
}

#endif /* _ASM_X86_XEN_HYPERCALL_H */
