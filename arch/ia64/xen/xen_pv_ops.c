/******************************************************************************
 * arch/ia64/xen/xen_pv_ops.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/console.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/unistd.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/xencomm.h>
#include <asm/xen/privop.h>

#include "irq_xen.h"
#include "time.h"

/***************************************************************************
 * general info
 */
static struct pv_info xen_info __initdata = {
	.kernel_rpl = 2,	/* or 1: determin at runtime */
	.paravirt_enabled = 1,
	.name = "Xen/ia64",
};

#define IA64_RSC_PL_SHIFT	2
#define IA64_RSC_PL_BIT_SIZE	2
#define IA64_RSC_PL_MASK	\
	(((1UL << IA64_RSC_PL_BIT_SIZE) - 1) << IA64_RSC_PL_SHIFT)

static void __init
xen_info_init(void)
{
	/* Xenified Linux/ia64 may run on pl = 1 or 2.
	 * determin at run time. */
	unsigned long rsc = ia64_getreg(_IA64_REG_AR_RSC);
	unsigned int rpl = (rsc & IA64_RSC_PL_MASK) >> IA64_RSC_PL_SHIFT;
	xen_info.kernel_rpl = rpl;
}

/***************************************************************************
 * pv_init_ops
 * initialization hooks.
 */

static void
xen_panic_hypercall(struct unw_frame_info *info, void *arg)
{
	current->thread.ksp = (__u64)info->sw - 16;
	HYPERVISOR_shutdown(SHUTDOWN_crash);
	/* we're never actually going to get here... */
}

static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	unw_init_running(xen_panic_hypercall, NULL);
	/* we're never actually going to get here... */
	return NOTIFY_DONE;
}

static struct notifier_block xen_panic_block = {
	xen_panic_event, NULL, 0 /* try to go last */
};

static void xen_pm_power_off(void)
{
	local_irq_disable();
	HYPERVISOR_shutdown(SHUTDOWN_poweroff);
}

static void __init
xen_banner(void)
{
	printk(KERN_INFO
	       "Running on Xen! pl = %d start_info_pfn=0x%lx nr_pages=%ld "
	       "flags=0x%x\n",
	       xen_info.kernel_rpl,
	       HYPERVISOR_shared_info->arch.start_info_pfn,
	       xen_start_info->nr_pages, xen_start_info->flags);
}

static int __init
xen_reserve_memory(struct rsvd_region *region)
{
	region->start = (unsigned long)__va(
		(HYPERVISOR_shared_info->arch.start_info_pfn << PAGE_SHIFT));
	region->end   = region->start + PAGE_SIZE;
	return 1;
}

static void __init
xen_arch_setup_early(void)
{
	struct shared_info *s;
	BUG_ON(!xen_pv_domain());

	s = HYPERVISOR_shared_info;
	xen_start_info = __va(s->arch.start_info_pfn << PAGE_SHIFT);

	/* Must be done before any hypercall.  */
	xencomm_initialize();

	xen_setup_features();
	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list,
				       &xen_panic_block);
	pm_power_off = xen_pm_power_off;

	xen_ia64_enable_opt_feature();
}

static void __init
xen_arch_setup_console(char **cmdline_p)
{
	add_preferred_console("xenboot", 0, NULL);
	add_preferred_console("tty", 0, NULL);
	/* use hvc_xen */
	add_preferred_console("hvc", 0, NULL);

#if !defined(CONFIG_VT) || !defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = NULL;
#endif
}

static int __init
xen_arch_setup_nomca(void)
{
	return 1;
}

static void __init
xen_post_smp_prepare_boot_cpu(void)
{
	xen_setup_vcpu_info_placement();
}

#ifdef ASM_SUPPORTED
static unsigned long __init_or_module
xen_patch_bundle(void *sbundle, void *ebundle, unsigned long type);
#endif
static void __init
xen_patch_branch(unsigned long tag, unsigned long type);

static const struct pv_init_ops xen_init_ops __initconst = {
	.banner = xen_banner,

	.reserve_memory = xen_reserve_memory,

	.arch_setup_early = xen_arch_setup_early,
	.arch_setup_console = xen_arch_setup_console,
	.arch_setup_nomca = xen_arch_setup_nomca,

	.post_smp_prepare_boot_cpu = xen_post_smp_prepare_boot_cpu,
#ifdef ASM_SUPPORTED
	.patch_bundle = xen_patch_bundle,
#endif
	.patch_branch = xen_patch_branch,
};

/***************************************************************************
 * pv_fsys_data
 * addresses for fsys
 */

extern unsigned long xen_fsyscall_table[NR_syscalls];
extern char xen_fsys_bubble_down[];
struct pv_fsys_data xen_fsys_data __initdata = {
	.fsyscall_table = (unsigned long *)xen_fsyscall_table,
	.fsys_bubble_down = (void *)xen_fsys_bubble_down,
};

/***************************************************************************
 * pv_patchdata
 * patchdata addresses
 */

#define DECLARE(name)							\
	extern unsigned long __xen_start_gate_##name##_patchlist[];	\
	extern unsigned long __xen_end_gate_##name##_patchlist[]

DECLARE(fsyscall);
DECLARE(brl_fsys_bubble_down);
DECLARE(vtop);
DECLARE(mckinley_e9);

extern unsigned long __xen_start_gate_section[];

#define ASSIGN(name)							\
	.start_##name##_patchlist =					\
		(unsigned long)__xen_start_gate_##name##_patchlist,	\
	.end_##name##_patchlist =					\
		(unsigned long)__xen_end_gate_##name##_patchlist

static struct pv_patchdata xen_patchdata __initdata = {
	ASSIGN(fsyscall),
	ASSIGN(brl_fsys_bubble_down),
	ASSIGN(vtop),
	ASSIGN(mckinley_e9),

	.gate_section = (void*)__xen_start_gate_section,
};

/***************************************************************************
 * pv_cpu_ops
 * intrinsics hooks.
 */

#ifndef ASM_SUPPORTED
static void
xen_set_itm_with_offset(unsigned long val)
{
	/* ia64_cpu_local_tick() calls this with interrupt enabled. */
	/* WARN_ON(!irqs_disabled()); */
	xen_set_itm(val - XEN_MAPPEDREGS->itc_offset);
}

static unsigned long
xen_get_itm_with_offset(void)
{
	/* unused at this moment */
	printk(KERN_DEBUG "%s is called.\n", __func__);

	WARN_ON(!irqs_disabled());
	return ia64_native_getreg(_IA64_REG_CR_ITM) +
		XEN_MAPPEDREGS->itc_offset;
}

/* ia64_set_itc() is only called by
 * cpu_init() with ia64_set_itc(0) and ia64_sync_itc().
 * So XEN_MAPPEDRESG->itc_offset cal be considered as almost constant.
 */
static void
xen_set_itc(unsigned long val)
{
	unsigned long mitc;

	WARN_ON(!irqs_disabled());
	mitc = ia64_native_getreg(_IA64_REG_AR_ITC);
	XEN_MAPPEDREGS->itc_offset = val - mitc;
	XEN_MAPPEDREGS->itc_last = val;
}

static unsigned long
xen_get_itc(void)
{
	unsigned long res;
	unsigned long itc_offset;
	unsigned long itc_last;
	unsigned long ret_itc_last;

	itc_offset = XEN_MAPPEDREGS->itc_offset;
	do {
		itc_last = XEN_MAPPEDREGS->itc_last;
		res = ia64_native_getreg(_IA64_REG_AR_ITC);
		res += itc_offset;
		if (itc_last >= res)
			res = itc_last + 1;
		ret_itc_last = cmpxchg(&XEN_MAPPEDREGS->itc_last,
				       itc_last, res);
	} while (unlikely(ret_itc_last != itc_last));
	return res;

#if 0
	/* ia64_itc_udelay() calls ia64_get_itc() with interrupt enabled.
	   Should it be paravirtualized instead? */
	WARN_ON(!irqs_disabled());
	itc_offset = XEN_MAPPEDREGS->itc_offset;
	itc_last = XEN_MAPPEDREGS->itc_last;
	res = ia64_native_getreg(_IA64_REG_AR_ITC);
	res += itc_offset;
	if (itc_last >= res)
		res = itc_last + 1;
	XEN_MAPPEDREGS->itc_last = res;
	return res;
#endif
}

static void xen_setreg(int regnum, unsigned long val)
{
	switch (regnum) {
	case _IA64_REG_AR_KR0 ... _IA64_REG_AR_KR7:
		xen_set_kr(regnum - _IA64_REG_AR_KR0, val);
		break;
	case _IA64_REG_AR_ITC:
		xen_set_itc(val);
		break;
	case _IA64_REG_CR_TPR:
		xen_set_tpr(val);
		break;
	case _IA64_REG_CR_ITM:
		xen_set_itm_with_offset(val);
		break;
	case _IA64_REG_CR_EOI:
		xen_eoi(val);
		break;
	default:
		ia64_native_setreg_func(regnum, val);
		break;
	}
}

static unsigned long xen_getreg(int regnum)
{
	unsigned long res;

	switch (regnum) {
	case _IA64_REG_PSR:
		res = xen_get_psr();
		break;
	case _IA64_REG_AR_ITC:
		res = xen_get_itc();
		break;
	case _IA64_REG_CR_ITM:
		res = xen_get_itm_with_offset();
		break;
	case _IA64_REG_CR_IVR:
		res = xen_get_ivr();
		break;
	case _IA64_REG_CR_TPR:
		res = xen_get_tpr();
		break;
	default:
		res = ia64_native_getreg_func(regnum);
		break;
	}
	return res;
}

/* turning on interrupts is a bit more complicated.. write to the
 * memory-mapped virtual psr.i bit first (to avoid race condition),
 * then if any interrupts were pending, we have to execute a hyperprivop
 * to ensure the pending interrupt gets delivered; else we're done! */
static void
xen_ssm_i(void)
{
	int old = xen_get_virtual_psr_i();
	xen_set_virtual_psr_i(1);
	barrier();
	if (!old && xen_get_virtual_pend())
		xen_hyper_ssm_i();
}

/* turning off interrupts can be paravirtualized simply by writing
 * to a memory-mapped virtual psr.i bit (implemented as a 16-bit bool) */
static void
xen_rsm_i(void)
{
	xen_set_virtual_psr_i(0);
	barrier();
}

static unsigned long
xen_get_psr_i(void)
{
	return xen_get_virtual_psr_i() ? IA64_PSR_I : 0;
}

static void
xen_intrin_local_irq_restore(unsigned long mask)
{
	if (mask & IA64_PSR_I)
		xen_ssm_i();
	else
		xen_rsm_i();
}
#else
#define __DEFINE_FUNC(name, code)					\
	extern const char xen_ ## name ## _direct_start[];		\
	extern const char xen_ ## name ## _direct_end[];		\
	asm (".align 32\n"						\
	     ".proc xen_" #name "\n"					\
	     "xen_" #name ":\n"						\
	     "xen_" #name "_direct_start:\n"				\
	     code							\
	     "xen_" #name "_direct_end:\n"				\
	     "br.cond.sptk.many b6\n"					\
	     ".endp xen_" #name "\n")

#define DEFINE_VOID_FUNC0(name, code)		\
	extern void				\
	xen_ ## name (void);			\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC1(name, code)		\
	extern void				\
	xen_ ## name (unsigned long arg);	\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC1_VOID(name, code)	\
	extern void				\
	xen_ ## name (void *arg);		\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC2(name, code)		\
	extern void				\
	xen_ ## name (unsigned long arg0,	\
		      unsigned long arg1);	\
	__DEFINE_FUNC(name, code)

#define DEFINE_FUNC0(name, code)		\
	extern unsigned long			\
	xen_ ## name (void);			\
	__DEFINE_FUNC(name, code)

#define DEFINE_FUNC1(name, type, code)		\
	extern unsigned long			\
	xen_ ## name (type arg);		\
	__DEFINE_FUNC(name, code)

#define XEN_PSR_I_ADDR_ADDR     (XSI_BASE + XSI_PSR_I_ADDR_OFS)

/*
 * static void xen_set_itm_with_offset(unsigned long val)
 *        xen_set_itm(val - XEN_MAPPEDREGS->itc_offset);
 */
/* 2 bundles */
DEFINE_VOID_FUNC1(set_itm_with_offset,
		  "mov r2 = " __stringify(XSI_BASE) " + "
		  __stringify(XSI_ITC_OFFSET_OFS) "\n"
		  ";;\n"
		  "ld8 r3 = [r2]\n"
		  ";;\n"
		  "sub r8 = r8, r3\n"
		  "break " __stringify(HYPERPRIVOP_SET_ITM) "\n");

/*
 * static unsigned long xen_get_itm_with_offset(void)
 *    return ia64_native_getreg(_IA64_REG_CR_ITM) + XEN_MAPPEDREGS->itc_offset;
 */
/* 2 bundles */
DEFINE_FUNC0(get_itm_with_offset,
	     "mov r2 = " __stringify(XSI_BASE) " + "
	     __stringify(XSI_ITC_OFFSET_OFS) "\n"
	     ";;\n"
	     "ld8 r3 = [r2]\n"
	     "mov r8 = cr.itm\n"
	     ";;\n"
	     "add r8 = r8, r2\n");

/*
 * static void xen_set_itc(unsigned long val)
 *	unsigned long mitc;
 *
 *	WARN_ON(!irqs_disabled());
 *	mitc = ia64_native_getreg(_IA64_REG_AR_ITC);
 *	XEN_MAPPEDREGS->itc_offset = val - mitc;
 *	XEN_MAPPEDREGS->itc_last = val;
 */
/* 2 bundles */
DEFINE_VOID_FUNC1(set_itc,
		  "mov r2 = " __stringify(XSI_BASE) " + "
		  __stringify(XSI_ITC_LAST_OFS) "\n"
		  "mov r3 = ar.itc\n"
		  ";;\n"
		  "sub r3 = r8, r3\n"
		  "st8 [r2] = r8, "
		  __stringify(XSI_ITC_LAST_OFS) " - "
		  __stringify(XSI_ITC_OFFSET_OFS) "\n"
		  ";;\n"
		  "st8 [r2] = r3\n");

/*
 * static unsigned long xen_get_itc(void)
 *	unsigned long res;
 *	unsigned long itc_offset;
 *	unsigned long itc_last;
 *	unsigned long ret_itc_last;
 *
 *	itc_offset = XEN_MAPPEDREGS->itc_offset;
 *	do {
 *		itc_last = XEN_MAPPEDREGS->itc_last;
 *		res = ia64_native_getreg(_IA64_REG_AR_ITC);
 *		res += itc_offset;
 *		if (itc_last >= res)
 *			res = itc_last + 1;
 *		ret_itc_last = cmpxchg(&XEN_MAPPEDREGS->itc_last,
 *				       itc_last, res);
 *	} while (unlikely(ret_itc_last != itc_last));
 *	return res;
 */
/* 5 bundles */
DEFINE_FUNC0(get_itc,
	     "mov r2 = " __stringify(XSI_BASE) " + "
	     __stringify(XSI_ITC_OFFSET_OFS) "\n"
	     ";;\n"
	     "ld8 r9 = [r2], " __stringify(XSI_ITC_LAST_OFS) " - "
	     __stringify(XSI_ITC_OFFSET_OFS) "\n"
					/* r9 = itc_offset */
					/* r2 = XSI_ITC_OFFSET */
	     "888:\n"
	     "mov r8 = ar.itc\n"	/* res = ar.itc */
	     ";;\n"
	     "ld8 r3 = [r2]\n"		/* r3 = itc_last */
	     "add r8 = r8, r9\n"	/* res = ar.itc + itc_offset */
	     ";;\n"
	     "cmp.gtu p6, p0 = r3, r8\n"
	     ";;\n"
	     "(p6) add r8 = 1, r3\n"	/* if (itc_last > res) itc_last + 1 */
	     ";;\n"
	     "mov ar.ccv = r8\n"
	     ";;\n"
	     "cmpxchg8.acq r10 = [r2], r8, ar.ccv\n"
	     ";;\n"
	     "cmp.ne p6, p0 = r10, r3\n"
	     "(p6) hint @pause\n"
	     "(p6) br.cond.spnt 888b\n");

DEFINE_VOID_FUNC1_VOID(fc,
		       "break " __stringify(HYPERPRIVOP_FC) "\n");

/*
 * psr_i_addr_addr = XEN_PSR_I_ADDR_ADDR
 * masked_addr = *psr_i_addr_addr
 * pending_intr_addr = masked_addr - 1
 * if (val & IA64_PSR_I) {
 *   masked = *masked_addr
 *   *masked_addr = 0:xen_set_virtual_psr_i(1)
 *   compiler barrier
 *   if (masked) {
 *      uint8_t pending = *pending_intr_addr;
 *      if (pending)
 *              XEN_HYPER_SSM_I
 *   }
 * } else {
 *   *masked_addr = 1:xen_set_virtual_psr_i(0)
 * }
 */
/* 6 bundles */
DEFINE_VOID_FUNC1(intrin_local_irq_restore,
		  /* r8 = input value: 0 or IA64_PSR_I
		   * p6 =  (flags & IA64_PSR_I)
		   *    = if clause
		   * p7 = !(flags & IA64_PSR_I)
		   *    = else clause
		   */
		  "cmp.ne p6, p7 = r8, r0\n"
		  "mov r9 = " __stringify(XEN_PSR_I_ADDR_ADDR) "\n"
		  ";;\n"
		  /* r9 = XEN_PSR_I_ADDR */
		  "ld8 r9 = [r9]\n"
		  ";;\n"

		  /* r10 = masked previous value */
		  "(p6)	ld1.acq r10 = [r9]\n"
		  ";;\n"

		  /* p8 = !masked interrupt masked previously? */
		  "(p6)	cmp.ne.unc p8, p0 = r10, r0\n"

		  /* p7 = else clause */
		  "(p7)	mov r11 = 1\n"
		  ";;\n"
		  /* masked = 1 */
		  "(p7)	st1.rel [r9] = r11\n"

		  /* p6 = if clause */
		  /* masked = 0
		   * r9 = masked_addr - 1
		   *    = pending_intr_addr
		   */
		  "(p8)	st1.rel [r9] = r0, -1\n"
		  ";;\n"
		  /* r8 = pending_intr */
		  "(p8)	ld1.acq r11 = [r9]\n"
		  ";;\n"
		  /* p9 = interrupt pending? */
		  "(p8)	cmp.ne.unc p9, p10 = r11, r0\n"
		  ";;\n"
		  "(p10) mf\n"
		  /* issue hypercall to trigger interrupt */
		  "(p9)	break " __stringify(HYPERPRIVOP_SSM_I) "\n");

DEFINE_VOID_FUNC2(ptcga,
		  "break " __stringify(HYPERPRIVOP_PTC_GA) "\n");
DEFINE_VOID_FUNC2(set_rr,
		  "break " __stringify(HYPERPRIVOP_SET_RR) "\n");

/*
 * tmp = XEN_MAPPEDREGS->interrupt_mask_addr = XEN_PSR_I_ADDR_ADDR;
 * tmp = *tmp
 * tmp = *tmp;
 * psr_i = tmp? 0: IA64_PSR_I;
 */
/* 4 bundles */
DEFINE_FUNC0(get_psr_i,
	     "mov r9 = " __stringify(XEN_PSR_I_ADDR_ADDR) "\n"
	     ";;\n"
	     "ld8 r9 = [r9]\n"			/* r9 = XEN_PSR_I_ADDR */
	     "mov r8 = 0\n"			/* psr_i = 0 */
	     ";;\n"
	     "ld1.acq r9 = [r9]\n"		/* r9 = XEN_PSR_I */
	     ";;\n"
	     "cmp.eq.unc p6, p0 = r9, r0\n"	/* p6 = (XEN_PSR_I != 0) */
	     ";;\n"
	     "(p6) mov r8 = " __stringify(1 << IA64_PSR_I_BIT) "\n");

DEFINE_FUNC1(thash, unsigned long,
	     "break " __stringify(HYPERPRIVOP_THASH) "\n");
DEFINE_FUNC1(get_cpuid, int,
	     "break " __stringify(HYPERPRIVOP_GET_CPUID) "\n");
DEFINE_FUNC1(get_pmd, int,
	     "break " __stringify(HYPERPRIVOP_GET_PMD) "\n");
DEFINE_FUNC1(get_rr, unsigned long,
	     "break " __stringify(HYPERPRIVOP_GET_RR) "\n");

/*
 * void xen_privop_ssm_i(void)
 *
 * int masked = !xen_get_virtual_psr_i();
 *	// masked = *(*XEN_MAPPEDREGS->interrupt_mask_addr)
 * xen_set_virtual_psr_i(1)
 *	// *(*XEN_MAPPEDREGS->interrupt_mask_addr) = 0
 * // compiler barrier
 * if (masked) {
 *	uint8_t* pend_int_addr =
 *		(uint8_t*)(*XEN_MAPPEDREGS->interrupt_mask_addr) - 1;
 *	uint8_t pending = *pend_int_addr;
 *	if (pending)
 *		XEN_HYPER_SSM_I
 * }
 */
/* 4 bundles */
DEFINE_VOID_FUNC0(ssm_i,
		  "mov r8 = " __stringify(XEN_PSR_I_ADDR_ADDR) "\n"
		  ";;\n"
		  "ld8 r8 = [r8]\n"		/* r8 = XEN_PSR_I_ADDR */
		  ";;\n"
		  "ld1.acq r9 = [r8]\n"		/* r9 = XEN_PSR_I */
		  ";;\n"
		  "st1.rel [r8] = r0, -1\n"	/* psr_i = 0. enable interrupt
						 * r8 = XEN_PSR_I_ADDR - 1
						 *    = pend_int_addr
						 */
		  "cmp.eq.unc p0, p6 = r9, r0\n"/* p6 = !XEN_PSR_I
						 * previously interrupt
						 * masked?
						 */
		  ";;\n"
		  "(p6) ld1.acq r8 = [r8]\n"	/* r8 = xen_pend_int */
		  ";;\n"
		  "(p6) cmp.eq.unc p6, p7 = r8, r0\n"	/*interrupt pending?*/
		  ";;\n"
		  /* issue hypercall to get interrupt */
		  "(p7) break " __stringify(HYPERPRIVOP_SSM_I) "\n"
		  ";;\n");

/*
 * psr_i_addr_addr = XEN_MAPPEDREGS->interrupt_mask_addr
 *		   = XEN_PSR_I_ADDR_ADDR;
 * psr_i_addr = *psr_i_addr_addr;
 * *psr_i_addr = 1;
 */
/* 2 bundles */
DEFINE_VOID_FUNC0(rsm_i,
		  "mov r8 = " __stringify(XEN_PSR_I_ADDR_ADDR) "\n"
						/* r8 = XEN_PSR_I_ADDR */
		  "mov r9 = 1\n"
		  ";;\n"
		  "ld8 r8 = [r8]\n"		/* r8 = XEN_PSR_I */
		  ";;\n"
		  "st1.rel [r8] = r9\n");	/* XEN_PSR_I = 1 */

extern void
xen_set_rr0_to_rr4(unsigned long val0, unsigned long val1,
		   unsigned long val2, unsigned long val3,
		   unsigned long val4);
__DEFINE_FUNC(set_rr0_to_rr4,
	      "break " __stringify(HYPERPRIVOP_SET_RR0_TO_RR4) "\n");


extern unsigned long xen_getreg(int regnum);
#define __DEFINE_GET_REG(id, privop)					\
	"mov r2 = " __stringify(_IA64_REG_ ## id) "\n"			\
	";;\n"								\
	"cmp.eq p6, p0 = r2, r8\n"					\
	";;\n"								\
	"(p6) break " __stringify(HYPERPRIVOP_GET_ ## privop) "\n"	\
	"(p6) br.cond.sptk.many b6\n"					\
	";;\n"

__DEFINE_FUNC(getreg,
	      __DEFINE_GET_REG(PSR, PSR)

	      /* get_itc */
	      "mov r2 = " __stringify(_IA64_REG_AR_ITC) "\n"
	      ";;\n"
	      "cmp.eq p6, p0 = r2, r8\n"
	      ";;\n"
	      "(p6) br.cond.spnt xen_get_itc\n"
	      ";;\n"

	      /* get itm */
	      "mov r2 = " __stringify(_IA64_REG_CR_ITM) "\n"
	      ";;\n"
	      "cmp.eq p6, p0 = r2, r8\n"
	      ";;\n"
	      "(p6) br.cond.spnt xen_get_itm_with_offset\n"
	      ";;\n"

	      __DEFINE_GET_REG(CR_IVR, IVR)
	      __DEFINE_GET_REG(CR_TPR, TPR)

	      /* fall back */
	      "movl r2 = ia64_native_getreg_func\n"
	      ";;\n"
	      "mov b7 = r2\n"
	      ";;\n"
	      "br.cond.sptk.many b7\n");

extern void xen_setreg(int regnum, unsigned long val);
#define __DEFINE_SET_REG(id, privop)					\
	"mov r2 = " __stringify(_IA64_REG_ ## id) "\n"			\
	";;\n"								\
	"cmp.eq p6, p0 = r2, r9\n"					\
	";;\n"								\
	"(p6) break " __stringify(HYPERPRIVOP_ ## privop) "\n"		\
	"(p6) br.cond.sptk.many b6\n"					\
	";;\n"

__DEFINE_FUNC(setreg,
	      /* kr0 .. kr 7*/
	      /*
	       * if (_IA64_REG_AR_KR0 <= regnum &&
	       *     regnum <= _IA64_REG_AR_KR7) {
	       *     register __index asm ("r8") = regnum - _IA64_REG_AR_KR0
	       *     register __val asm ("r9") = val
	       *    "break HYPERPRIVOP_SET_KR"
	       * }
	       */
	      "mov r17 = r9\n"
	      "mov r2 = " __stringify(_IA64_REG_AR_KR0) "\n"
	      ";;\n"
	      "cmp.ge p6, p0 = r9, r2\n"
	      "sub r17 = r17, r2\n"
	      ";;\n"
	      "(p6) cmp.ge.unc p7, p0 = "
	      __stringify(_IA64_REG_AR_KR7) " - " __stringify(_IA64_REG_AR_KR0)
	      ", r17\n"
	      ";;\n"
	      "(p7) mov r9 = r8\n"
	      ";;\n"
	      "(p7) mov r8 = r17\n"
	      "(p7) break " __stringify(HYPERPRIVOP_SET_KR) "\n"

	      /* set itm */
	      "mov r2 = " __stringify(_IA64_REG_CR_ITM) "\n"
	      ";;\n"
	      "cmp.eq p6, p0 = r2, r8\n"
	      ";;\n"
	      "(p6) br.cond.spnt xen_set_itm_with_offset\n"

	      /* set itc */
	      "mov r2 = " __stringify(_IA64_REG_AR_ITC) "\n"
	      ";;\n"
	      "cmp.eq p6, p0 = r2, r8\n"
	      ";;\n"
	      "(p6) br.cond.spnt xen_set_itc\n"

	      __DEFINE_SET_REG(CR_TPR, SET_TPR)
	      __DEFINE_SET_REG(CR_EOI, EOI)

	      /* fall back */
	      "movl r2 = ia64_native_setreg_func\n"
	      ";;\n"
	      "mov b7 = r2\n"
	      ";;\n"
	      "br.cond.sptk.many b7\n");
#endif

static const struct pv_cpu_ops xen_cpu_ops __initconst = {
	.fc		= xen_fc,
	.thash		= xen_thash,
	.get_cpuid	= xen_get_cpuid,
	.get_pmd	= xen_get_pmd,
	.getreg		= xen_getreg,
	.setreg		= xen_setreg,
	.ptcga		= xen_ptcga,
	.get_rr		= xen_get_rr,
	.set_rr		= xen_set_rr,
	.set_rr0_to_rr4	= xen_set_rr0_to_rr4,
	.ssm_i		= xen_ssm_i,
	.rsm_i		= xen_rsm_i,
	.get_psr_i	= xen_get_psr_i,
	.intrin_local_irq_restore
			= xen_intrin_local_irq_restore,
};

/******************************************************************************
 * replacement of hand written assembly codes.
 */

extern char xen_switch_to;
extern char xen_leave_syscall;
extern char xen_work_processed_syscall;
extern char xen_leave_kernel;

const struct pv_cpu_asm_switch xen_cpu_asm_switch = {
	.switch_to		= (unsigned long)&xen_switch_to,
	.leave_syscall		= (unsigned long)&xen_leave_syscall,
	.work_processed_syscall	= (unsigned long)&xen_work_processed_syscall,
	.leave_kernel		= (unsigned long)&xen_leave_kernel,
};

/***************************************************************************
 * pv_iosapic_ops
 * iosapic read/write hooks.
 */
static void
xen_pcat_compat_init(void)
{
	/* nothing */
}

static struct irq_chip*
xen_iosapic_get_irq_chip(unsigned long trigger)
{
	return NULL;
}

static unsigned int
xen_iosapic_read(char __iomem *iosapic, unsigned int reg)
{
	struct physdev_apic apic_op;
	int ret;

	apic_op.apic_physbase = (unsigned long)iosapic -
					__IA64_UNCACHED_OFFSET;
	apic_op.reg = reg;
	ret = HYPERVISOR_physdev_op(PHYSDEVOP_apic_read, &apic_op);
	if (ret)
		return ret;
	return apic_op.value;
}

static void
xen_iosapic_write(char __iomem *iosapic, unsigned int reg, u32 val)
{
	struct physdev_apic apic_op;

	apic_op.apic_physbase = (unsigned long)iosapic -
					__IA64_UNCACHED_OFFSET;
	apic_op.reg = reg;
	apic_op.value = val;
	HYPERVISOR_physdev_op(PHYSDEVOP_apic_write, &apic_op);
}

static struct pv_iosapic_ops xen_iosapic_ops __initdata = {
	.pcat_compat_init = xen_pcat_compat_init,
	.__get_irq_chip = xen_iosapic_get_irq_chip,

	.__read = xen_iosapic_read,
	.__write = xen_iosapic_write,
};

/***************************************************************************
 * pv_ops initialization
 */

void __init
xen_setup_pv_ops(void)
{
	xen_info_init();
	pv_info = xen_info;
	pv_init_ops = xen_init_ops;
	pv_fsys_data = xen_fsys_data;
	pv_patchdata = xen_patchdata;
	pv_cpu_ops = xen_cpu_ops;
	pv_iosapic_ops = xen_iosapic_ops;
	pv_irq_ops = xen_irq_ops;
	pv_time_ops = xen_time_ops;

	paravirt_cpu_asm_init(&xen_cpu_asm_switch);
}

#ifdef ASM_SUPPORTED
/***************************************************************************
 * binary pacthing
 * pv_init_ops.patch_bundle
 */

#define DEFINE_FUNC_GETREG(name, privop)				\
	DEFINE_FUNC0(get_ ## name,					\
		     "break "__stringify(HYPERPRIVOP_GET_ ## privop) "\n")

DEFINE_FUNC_GETREG(psr, PSR);
DEFINE_FUNC_GETREG(eflag, EFLAG);
DEFINE_FUNC_GETREG(ivr, IVR);
DEFINE_FUNC_GETREG(tpr, TPR);

#define DEFINE_FUNC_SET_KR(n)						\
	DEFINE_VOID_FUNC0(set_kr ## n,					\
			  ";;\n"					\
			  "mov r9 = r8\n"				\
			  "mov r8 = " #n "\n"				\
			  "break " __stringify(HYPERPRIVOP_SET_KR) "\n")

DEFINE_FUNC_SET_KR(0);
DEFINE_FUNC_SET_KR(1);
DEFINE_FUNC_SET_KR(2);
DEFINE_FUNC_SET_KR(3);
DEFINE_FUNC_SET_KR(4);
DEFINE_FUNC_SET_KR(5);
DEFINE_FUNC_SET_KR(6);
DEFINE_FUNC_SET_KR(7);

#define __DEFINE_FUNC_SETREG(name, privop)				\
	DEFINE_VOID_FUNC0(name,						\
			  "break "__stringify(HYPERPRIVOP_ ## privop) "\n")

#define DEFINE_FUNC_SETREG(name, privop)			\
	__DEFINE_FUNC_SETREG(set_ ## name, SET_ ## privop)

DEFINE_FUNC_SETREG(eflag, EFLAG);
DEFINE_FUNC_SETREG(tpr, TPR);
__DEFINE_FUNC_SETREG(eoi, EOI);

extern const char xen_check_events[];
extern const char __xen_intrin_local_irq_restore_direct_start[];
extern const char __xen_intrin_local_irq_restore_direct_end[];
extern const unsigned long __xen_intrin_local_irq_restore_direct_reloc;

asm (
	".align 32\n"
	".proc xen_check_events\n"
	"xen_check_events:\n"
	/* masked = 0
	 * r9 = masked_addr - 1
	 *    = pending_intr_addr
	 */
	"st1.rel [r9] = r0, -1\n"
	";;\n"
	/* r8 = pending_intr */
	"ld1.acq r11 = [r9]\n"
	";;\n"
	/* p9 = interrupt pending? */
	"cmp.ne p9, p10 = r11, r0\n"
	";;\n"
	"(p10) mf\n"
	/* issue hypercall to trigger interrupt */
	"(p9) break " __stringify(HYPERPRIVOP_SSM_I) "\n"
	"br.cond.sptk.many b6\n"
	".endp xen_check_events\n"
	"\n"
	".align 32\n"
	".proc __xen_intrin_local_irq_restore_direct\n"
	"__xen_intrin_local_irq_restore_direct:\n"
	"__xen_intrin_local_irq_restore_direct_start:\n"
	"1:\n"
	"{\n"
	"cmp.ne p6, p7 = r8, r0\n"
	"mov r17 = ip\n" /* get ip to calc return address */
	"mov r9 = "__stringify(XEN_PSR_I_ADDR_ADDR) "\n"
	";;\n"
	"}\n"
	"{\n"
	/* r9 = XEN_PSR_I_ADDR */
	"ld8 r9 = [r9]\n"
	";;\n"
	/* r10 = masked previous value */
	"(p6) ld1.acq r10 = [r9]\n"
	"adds r17 =  1f - 1b, r17\n" /* calculate return address */
	";;\n"
	"}\n"
	"{\n"
	/* p8 = !masked interrupt masked previously? */
	"(p6) cmp.ne.unc p8, p0 = r10, r0\n"
	"\n"
	/* p7 = else clause */
	"(p7) mov r11 = 1\n"
	";;\n"
	"(p8) mov b6 = r17\n" /* set return address */
	"}\n"
	"{\n"
	/* masked = 1 */
	"(p7) st1.rel [r9] = r11\n"
	"\n"
	"[99:]\n"
	"(p8) brl.cond.dptk.few xen_check_events\n"
	"}\n"
	/* pv calling stub is 5 bundles. fill nop to adjust return address */
	"{\n"
	"nop 0\n"
	"nop 0\n"
	"nop 0\n"
	"}\n"
	"1:\n"
	"__xen_intrin_local_irq_restore_direct_end:\n"
	".endp __xen_intrin_local_irq_restore_direct\n"
	"\n"
	".align 8\n"
	"__xen_intrin_local_irq_restore_direct_reloc:\n"
	"data8 99b\n"
);

static struct paravirt_patch_bundle_elem xen_patch_bundle_elems[]
__initdata_or_module =
{
#define XEN_PATCH_BUNDLE_ELEM(name, type)		\
	{						\
		(void*)xen_ ## name ## _direct_start,	\
		(void*)xen_ ## name ## _direct_end,	\
		PARAVIRT_PATCH_TYPE_ ## type,		\
	}

	XEN_PATCH_BUNDLE_ELEM(fc, FC),
	XEN_PATCH_BUNDLE_ELEM(thash, THASH),
	XEN_PATCH_BUNDLE_ELEM(get_cpuid, GET_CPUID),
	XEN_PATCH_BUNDLE_ELEM(get_pmd, GET_PMD),
	XEN_PATCH_BUNDLE_ELEM(ptcga, PTCGA),
	XEN_PATCH_BUNDLE_ELEM(get_rr, GET_RR),
	XEN_PATCH_BUNDLE_ELEM(set_rr, SET_RR),
	XEN_PATCH_BUNDLE_ELEM(set_rr0_to_rr4, SET_RR0_TO_RR4),
	XEN_PATCH_BUNDLE_ELEM(ssm_i, SSM_I),
	XEN_PATCH_BUNDLE_ELEM(rsm_i, RSM_I),
	XEN_PATCH_BUNDLE_ELEM(get_psr_i, GET_PSR_I),
	{
		(void*)__xen_intrin_local_irq_restore_direct_start,
		(void*)__xen_intrin_local_irq_restore_direct_end,
		PARAVIRT_PATCH_TYPE_INTRIN_LOCAL_IRQ_RESTORE,
	},

#define XEN_PATCH_BUNDLE_ELEM_GETREG(name, reg)			\
	{							\
		xen_get_ ## name ## _direct_start,		\
		xen_get_ ## name ## _direct_end,		\
		PARAVIRT_PATCH_TYPE_GETREG + _IA64_REG_ ## reg, \
	}

	XEN_PATCH_BUNDLE_ELEM_GETREG(psr, PSR),
	XEN_PATCH_BUNDLE_ELEM_GETREG(eflag, AR_EFLAG),

	XEN_PATCH_BUNDLE_ELEM_GETREG(ivr, CR_IVR),
	XEN_PATCH_BUNDLE_ELEM_GETREG(tpr, CR_TPR),

	XEN_PATCH_BUNDLE_ELEM_GETREG(itc, AR_ITC),
	XEN_PATCH_BUNDLE_ELEM_GETREG(itm_with_offset, CR_ITM),


#define __XEN_PATCH_BUNDLE_ELEM_SETREG(name, reg)		\
	{							\
		xen_ ## name ## _direct_start,			\
		xen_ ## name ## _direct_end,			\
		PARAVIRT_PATCH_TYPE_SETREG + _IA64_REG_ ## reg, \
	}

#define XEN_PATCH_BUNDLE_ELEM_SETREG(name, reg)			\
	__XEN_PATCH_BUNDLE_ELEM_SETREG(set_ ## name, reg)

	XEN_PATCH_BUNDLE_ELEM_SETREG(kr0, AR_KR0),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr1, AR_KR1),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr2, AR_KR2),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr3, AR_KR3),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr4, AR_KR4),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr5, AR_KR5),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr6, AR_KR6),
	XEN_PATCH_BUNDLE_ELEM_SETREG(kr7, AR_KR7),

	XEN_PATCH_BUNDLE_ELEM_SETREG(eflag, AR_EFLAG),
	XEN_PATCH_BUNDLE_ELEM_SETREG(tpr, CR_TPR),
	__XEN_PATCH_BUNDLE_ELEM_SETREG(eoi, CR_EOI),

	XEN_PATCH_BUNDLE_ELEM_SETREG(itc, AR_ITC),
	XEN_PATCH_BUNDLE_ELEM_SETREG(itm_with_offset, CR_ITM),
};

static unsigned long __init_or_module
xen_patch_bundle(void *sbundle, void *ebundle, unsigned long type)
{
	const unsigned long nelems = sizeof(xen_patch_bundle_elems) /
		sizeof(xen_patch_bundle_elems[0]);
	unsigned long used;
	const struct paravirt_patch_bundle_elem *found;

	used = __paravirt_patch_apply_bundle(sbundle, ebundle, type,
					     xen_patch_bundle_elems, nelems,
					     &found);

	if (found == NULL)
		/* fallback */
		return ia64_native_patch_bundle(sbundle, ebundle, type);
	if (used == 0)
		return used;

	/* relocation */
	switch (type) {
	case PARAVIRT_PATCH_TYPE_INTRIN_LOCAL_IRQ_RESTORE: {
		unsigned long reloc =
			__xen_intrin_local_irq_restore_direct_reloc;
		unsigned long reloc_offset = reloc - (unsigned long)
			__xen_intrin_local_irq_restore_direct_start;
		unsigned long tag = (unsigned long)sbundle + reloc_offset;
		paravirt_patch_reloc_brl(tag, xen_check_events);
		break;
	}
	default:
		/* nothing */
		break;
	}
	return used;
}
#endif /* ASM_SUPPOTED */

const struct paravirt_patch_branch_target xen_branch_target[]
__initconst = {
#define PARAVIRT_BR_TARGET(name, type)			\
	{						\
		&xen_ ## name,				\
		PARAVIRT_PATCH_TYPE_BR_ ## type,	\
	}
	PARAVIRT_BR_TARGET(switch_to, SWITCH_TO),
	PARAVIRT_BR_TARGET(leave_syscall, LEAVE_SYSCALL),
	PARAVIRT_BR_TARGET(work_processed_syscall, WORK_PROCESSED_SYSCALL),
	PARAVIRT_BR_TARGET(leave_kernel, LEAVE_KERNEL),
};

static void __init
xen_patch_branch(unsigned long tag, unsigned long type)
{
	const unsigned long nelem =
		sizeof(xen_branch_target) / sizeof(xen_branch_target[0]);
	__paravirt_patch_apply_branch(tag, type, xen_branch_target, nelem);
}
