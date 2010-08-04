/*
 * Support routines for Xen hypercalls
 *
 * Copyright (C) 2005 Dan Magenheimer <dan.magenheimer@hp.com>
 * Copyright (C) 2008 Yaozu (Eddie) Dong <eddie.dong@intel.com>
 */

#include <asm/asmmacro.h>
#include <asm/intrinsics.h>
#include <asm/xen/privop.h>

#ifdef __INTEL_COMPILER
/*
 * Hypercalls without parameter.
 */
#define __HCALL0(name,hcall)		\
	GLOBAL_ENTRY(name);		\
	break	hcall;			\
	br.ret.sptk.many rp;		\
	END(name)

/*
 * Hypercalls with 1 parameter.
 */
#define __HCALL1(name,hcall)		\
	GLOBAL_ENTRY(name);		\
	mov r8=r32;			\
	break	hcall;			\
	br.ret.sptk.many rp;		\
	END(name)

/*
 * Hypercalls with 2 parameters.
 */
#define __HCALL2(name,hcall)		\
	GLOBAL_ENTRY(name);		\
	mov r8=r32;			\
	mov r9=r33;			\
	break	hcall;			\
	br.ret.sptk.many rp;		\
	END(name)

__HCALL0(xen_get_psr, HYPERPRIVOP_GET_PSR)
__HCALL0(xen_get_ivr, HYPERPRIVOP_GET_IVR)
__HCALL0(xen_get_tpr, HYPERPRIVOP_GET_TPR)
__HCALL0(xen_hyper_ssm_i, HYPERPRIVOP_SSM_I)

__HCALL1(xen_set_tpr, HYPERPRIVOP_SET_TPR)
__HCALL1(xen_eoi, HYPERPRIVOP_EOI)
__HCALL1(xen_thash, HYPERPRIVOP_THASH)
__HCALL1(xen_set_itm, HYPERPRIVOP_SET_ITM)
__HCALL1(xen_get_rr, HYPERPRIVOP_GET_RR)
__HCALL1(xen_fc, HYPERPRIVOP_FC)
__HCALL1(xen_get_cpuid, HYPERPRIVOP_GET_CPUID)
__HCALL1(xen_get_pmd, HYPERPRIVOP_GET_PMD)

__HCALL2(xen_ptcga, HYPERPRIVOP_PTC_GA)
__HCALL2(xen_set_rr, HYPERPRIVOP_SET_RR)
__HCALL2(xen_set_kr, HYPERPRIVOP_SET_KR)

GLOBAL_ENTRY(xen_set_rr0_to_rr4)
	mov r8=r32
	mov r9=r33
	mov r10=r34
	mov r11=r35
	mov r14=r36
	XEN_HYPER_SET_RR0_TO_RR4
	br.ret.sptk.many rp
	;;
END(xen_set_rr0_to_rr4)
#endif

GLOBAL_ENTRY(xen_send_ipi)
	mov r14=r32
	mov r15=r33
	mov r2=0x400
	break 0x1000
	;;
	br.ret.sptk.many rp
	;;
END(xen_send_ipi)

GLOBAL_ENTRY(__hypercall)
	mov r2=r37
	break 0x1000
	br.ret.sptk.many b0
	;;
END(__hypercall)
