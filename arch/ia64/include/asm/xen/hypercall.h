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

#ifndef _ASM_IA64_XEN_HYPERCALL_H
#define _ASM_IA64_XEN_HYPERCALL_H

#include <xen/interface/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/sched.h>
#include <asm/xen/xcom_hcall.h>
struct xencomm_handle;
extern unsigned long __hypercall(unsigned long a1, unsigned long a2,
				 unsigned long a3, unsigned long a4,
				 unsigned long a5, unsigned long cmd);

/*
 * Assembler stubs for hyper-calls.
 */

#define _hypercall0(type, name)					\
({								\
	long __res;						\
	__res = __hypercall(0, 0, 0, 0, 0, __HYPERVISOR_##name);\
	(type)__res;						\
})

#define _hypercall1(type, name, a1)				\
({								\
	long __res;						\
	__res = __hypercall((unsigned long)a1,			\
			     0, 0, 0, 0, __HYPERVISOR_##name);	\
	(type)__res;						\
})

#define _hypercall2(type, name, a1, a2)				\
({								\
	long __res;						\
	__res = __hypercall((unsigned long)a1,			\
			    (unsigned long)a2,			\
			    0, 0, 0, __HYPERVISOR_##name);	\
	(type)__res;						\
})

#define _hypercall3(type, name, a1, a2, a3)			\
({								\
	long __res;						\
	__res = __hypercall((unsigned long)a1,			\
			    (unsigned long)a2,			\
			    (unsigned long)a3,			\
			    0, 0, __HYPERVISOR_##name);		\
	(type)__res;						\
})

#define _hypercall4(type, name, a1, a2, a3, a4)			\
({								\
	long __res;						\
	__res = __hypercall((unsigned long)a1,			\
			    (unsigned long)a2,			\
			    (unsigned long)a3,			\
			    (unsigned long)a4,			\
			    0, __HYPERVISOR_##name);		\
	(type)__res;						\
})

#define _hypercall5(type, name, a1, a2, a3, a4, a5)		\
({								\
	long __res;						\
	__res = __hypercall((unsigned long)a1,			\
			    (unsigned long)a2,			\
			    (unsigned long)a3,			\
			    (unsigned long)a4,			\
			    (unsigned long)a5,			\
			    __HYPERVISOR_##name);		\
	(type)__res;						\
})


static inline int
xencomm_arch_hypercall_sched_op(int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, sched_op_new, cmd, arg);
}

static inline long
HYPERVISOR_set_timer_op(u64 timeout)
{
	unsigned long timeout_hi = (unsigned long)(timeout >> 32);
	unsigned long timeout_lo = (unsigned long)timeout;
	return _hypercall2(long, set_timer_op, timeout_lo, timeout_hi);
}

static inline int
xencomm_arch_hypercall_multicall(struct xencomm_handle *call_list,
				 int nr_calls)
{
	return _hypercall2(int, multicall, call_list, nr_calls);
}

static inline int
xencomm_arch_hypercall_memory_op(unsigned int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, memory_op, cmd, arg);
}

static inline int
xencomm_arch_hypercall_event_channel_op(int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, event_channel_op, cmd, arg);
}

static inline int
xencomm_arch_hypercall_xen_version(int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, xen_version, cmd, arg);
}

static inline int
xencomm_arch_hypercall_console_io(int cmd, int count,
				  struct xencomm_handle *str)
{
	return _hypercall3(int, console_io, cmd, count, str);
}

static inline int
xencomm_arch_hypercall_physdev_op(int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, physdev_op, cmd, arg);
}

static inline int
xencomm_arch_hypercall_grant_table_op(unsigned int cmd,
				      struct xencomm_handle *uop,
				      unsigned int count)
{
	return _hypercall3(int, grant_table_op, cmd, uop, count);
}

int HYPERVISOR_grant_table_op(unsigned int cmd, void *uop, unsigned int count);

extern int xencomm_arch_hypercall_suspend(struct xencomm_handle *arg);

static inline int
xencomm_arch_hypercall_callback_op(int cmd, struct xencomm_handle *arg)
{
	return _hypercall2(int, callback_op, cmd, arg);
}

static inline long
xencomm_arch_hypercall_vcpu_op(int cmd, int cpu, void *arg)
{
	return _hypercall3(long, vcpu_op, cmd, cpu, arg);
}

static inline int
HYPERVISOR_physdev_op(int cmd, void *arg)
{
	switch (cmd) {
	case PHYSDEVOP_eoi:
		return _hypercall1(int, ia64_fast_eoi,
				   ((struct physdev_eoi *)arg)->irq);
	default:
		return xencomm_hypercall_physdev_op(cmd, arg);
	}
}

static inline long
xencomm_arch_hypercall_opt_feature(struct xencomm_handle *arg)
{
	return _hypercall1(long, opt_feature, arg);
}

/* for balloon driver */
#define HYPERVISOR_update_va_mapping(va, new_val, flags) (0)

/* Use xencomm to do hypercalls.  */
#define HYPERVISOR_sched_op xencomm_hypercall_sched_op
#define HYPERVISOR_event_channel_op xencomm_hypercall_event_channel_op
#define HYPERVISOR_callback_op xencomm_hypercall_callback_op
#define HYPERVISOR_multicall xencomm_hypercall_multicall
#define HYPERVISOR_xen_version xencomm_hypercall_xen_version
#define HYPERVISOR_console_io xencomm_hypercall_console_io
#define HYPERVISOR_memory_op xencomm_hypercall_memory_op
#define HYPERVISOR_suspend xencomm_hypercall_suspend
#define HYPERVISOR_vcpu_op xencomm_hypercall_vcpu_op
#define HYPERVISOR_opt_feature xencomm_hypercall_opt_feature

/* to compile gnttab_copy_grant_page() in drivers/xen/core/gnttab.c */
#define HYPERVISOR_mmu_update(req, count, success_count, domid) ({ BUG(); 0; })

static inline int
HYPERVISOR_shutdown(
	unsigned int reason)
{
	struct sched_shutdown sched_shutdown = {
		.reason = reason
	};

	int rc = HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);

	return rc;
}

/* for netfront.c, netback.c */
#define MULTI_UVMFLAGS_INDEX 0 /* XXX any value */

static inline void
MULTI_update_va_mapping(
	struct multicall_entry *mcl, unsigned long va,
	pte_t new_val, unsigned long flags)
{
	mcl->op = __HYPERVISOR_update_va_mapping;
	mcl->result = 0;
}

static inline void
MULTI_grant_table_op(struct multicall_entry *mcl, unsigned int cmd,
	void *uop, unsigned int count)
{
	mcl->op = __HYPERVISOR_grant_table_op;
	mcl->args[0] = cmd;
	mcl->args[1] = (unsigned long)uop;
	mcl->args[2] = count;
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
}

#endif /* _ASM_IA64_XEN_HYPERCALL_H */
