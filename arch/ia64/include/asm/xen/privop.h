#ifndef _ASM_IA64_XEN_PRIVOP_H
#define _ASM_IA64_XEN_PRIVOP_H

/*
 * Copyright (C) 2005 Hewlett-Packard Co
 *	Dan Magenheimer <dan.magenheimer@hp.com>
 *
 * Paravirtualizations of privileged operations for Xen/ia64
 *
 *
 * inline privop and paravirt_alt support
 * Copyright (c) 2007 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 */

#ifndef __ASSEMBLY__
#include <linux/types.h>		/* arch-ia64.h requires uint64_t */
#endif
#include <asm/xen/interface.h>

/* At 1 MB, before per-cpu space but still addressable using addl instead
   of movl. */
#define XSI_BASE			0xfffffffffff00000

/* Address of mapped regs.  */
#define XMAPPEDREGS_BASE		(XSI_BASE + XSI_SIZE)

#ifdef __ASSEMBLY__
#define XEN_HYPER_RFI			break HYPERPRIVOP_RFI
#define XEN_HYPER_RSM_PSR_DT		break HYPERPRIVOP_RSM_DT
#define XEN_HYPER_SSM_PSR_DT		break HYPERPRIVOP_SSM_DT
#define XEN_HYPER_COVER			break HYPERPRIVOP_COVER
#define XEN_HYPER_ITC_D			break HYPERPRIVOP_ITC_D
#define XEN_HYPER_ITC_I			break HYPERPRIVOP_ITC_I
#define XEN_HYPER_SSM_I			break HYPERPRIVOP_SSM_I
#define XEN_HYPER_GET_IVR		break HYPERPRIVOP_GET_IVR
#define XEN_HYPER_THASH			break HYPERPRIVOP_THASH
#define XEN_HYPER_ITR_D			break HYPERPRIVOP_ITR_D
#define XEN_HYPER_SET_KR		break HYPERPRIVOP_SET_KR
#define XEN_HYPER_GET_PSR		break HYPERPRIVOP_GET_PSR
#define XEN_HYPER_SET_RR0_TO_RR4	break HYPERPRIVOP_SET_RR0_TO_RR4

#define XSI_IFS				(XSI_BASE + XSI_IFS_OFS)
#define XSI_PRECOVER_IFS		(XSI_BASE + XSI_PRECOVER_IFS_OFS)
#define XSI_IFA				(XSI_BASE + XSI_IFA_OFS)
#define XSI_ISR				(XSI_BASE + XSI_ISR_OFS)
#define XSI_IIM				(XSI_BASE + XSI_IIM_OFS)
#define XSI_ITIR			(XSI_BASE + XSI_ITIR_OFS)
#define XSI_PSR_I_ADDR			(XSI_BASE + XSI_PSR_I_ADDR_OFS)
#define XSI_PSR_IC			(XSI_BASE + XSI_PSR_IC_OFS)
#define XSI_IPSR			(XSI_BASE + XSI_IPSR_OFS)
#define XSI_IIP				(XSI_BASE + XSI_IIP_OFS)
#define XSI_B1NAT			(XSI_BASE + XSI_B1NATS_OFS)
#define XSI_BANK1_R16			(XSI_BASE + XSI_BANK1_R16_OFS)
#define XSI_BANKNUM			(XSI_BASE + XSI_BANKNUM_OFS)
#define XSI_IHA				(XSI_BASE + XSI_IHA_OFS)
#define XSI_ITC_OFFSET			(XSI_BASE + XSI_ITC_OFFSET_OFS)
#define XSI_ITC_LAST			(XSI_BASE + XSI_ITC_LAST_OFS)
#endif

#ifndef __ASSEMBLY__

/************************************************/
/* Instructions paravirtualized for correctness */
/************************************************/

/* "fc" and "thash" are privilege-sensitive instructions, meaning they
 *  may have different semantics depending on whether they are executed
 *  at PL0 vs PL!=0.  When paravirtualized, these instructions mustn't
 *  be allowed to execute directly, lest incorrect semantics result. */
extern void xen_fc(void *addr);
extern unsigned long xen_thash(unsigned long addr);

/* Note that "ttag" and "cover" are also privilege-sensitive; "ttag"
 * is not currently used (though it may be in a long-format VHPT system!)
 * and the semantics of cover only change if psr.ic is off which is very
 * rare (and currently non-existent outside of assembly code */

/* There are also privilege-sensitive registers.  These registers are
 * readable at any privilege level but only writable at PL0. */
extern unsigned long xen_get_cpuid(int index);
extern unsigned long xen_get_pmd(int index);

#ifndef ASM_SUPPORTED
extern unsigned long xen_get_eflag(void);	/* see xen_ia64_getreg */
extern void xen_set_eflag(unsigned long);	/* see xen_ia64_setreg */
#endif

/************************************************/
/* Instructions paravirtualized for performance */
/************************************************/

/* Xen uses memory-mapped virtual privileged registers for access to many
 * performance-sensitive privileged registers.  Some, like the processor
 * status register (psr), are broken up into multiple memory locations.
 * Others, like "pend", are abstractions based on privileged registers.
 * "Pend" is guaranteed to be set if reading cr.ivr would return a
 * (non-spurious) interrupt. */
#define XEN_MAPPEDREGS ((struct mapped_regs *)XMAPPEDREGS_BASE)

#define XSI_PSR_I			\
	(*XEN_MAPPEDREGS->interrupt_mask_addr)
#define xen_get_virtual_psr_i()		\
	(!XSI_PSR_I)
#define xen_set_virtual_psr_i(_val)	\
	({ XSI_PSR_I = (uint8_t)(_val) ? 0 : 1; })
#define xen_set_virtual_psr_ic(_val)	\
	({ XEN_MAPPEDREGS->interrupt_collection_enabled = _val ? 1 : 0; })
#define xen_get_virtual_pend()		\
	(*(((uint8_t *)XEN_MAPPEDREGS->interrupt_mask_addr) - 1))

#ifndef ASM_SUPPORTED
/* Although all privileged operations can be left to trap and will
 * be properly handled by Xen, some are frequent enough that we use
 * hyperprivops for performance. */
extern unsigned long xen_get_psr(void);
extern unsigned long xen_get_ivr(void);
extern unsigned long xen_get_tpr(void);
extern void xen_hyper_ssm_i(void);
extern void xen_set_itm(unsigned long);
extern void xen_set_tpr(unsigned long);
extern void xen_eoi(unsigned long);
extern unsigned long xen_get_rr(unsigned long index);
extern void xen_set_rr(unsigned long index, unsigned long val);
extern void xen_set_rr0_to_rr4(unsigned long val0, unsigned long val1,
			       unsigned long val2, unsigned long val3,
			       unsigned long val4);
extern void xen_set_kr(unsigned long index, unsigned long val);
extern void xen_ptcga(unsigned long addr, unsigned long size);
#endif /* !ASM_SUPPORTED */

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_XEN_PRIVOP_H */
