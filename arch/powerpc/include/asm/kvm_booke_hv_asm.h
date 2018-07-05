/*
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef ASM_KVM_BOOKE_HV_ASM_H
#define ASM_KVM_BOOKE_HV_ASM_H

#include <asm/feature-fixups.h>

#ifdef __ASSEMBLY__

/*
 * All exceptions from guest state must go through KVM
 * (except for those which are delivered directly to the guest) --
 * there are no exceptions for which we fall through directly to
 * the normal host handler.
 *
 * 32-bit host
 * Expected inputs (normal exceptions):
 *   SCRATCH0 = saved r10
 *   r10 = thread struct
 *   r11 = appropriate SRR1 variant (currently used as scratch)
 *   r13 = saved CR
 *   *(r10 + THREAD_NORMSAVE(0)) = saved r11
 *   *(r10 + THREAD_NORMSAVE(2)) = saved r13
 *
 * Expected inputs (crit/mcheck/debug exceptions):
 *   appropriate SCRATCH = saved r8
 *   r8 = exception level stack frame
 *   r9 = *(r8 + _CCR) = saved CR
 *   r11 = appropriate SRR1 variant (currently used as scratch)
 *   *(r8 + GPR9) = saved r9
 *   *(r8 + GPR10) = saved r10 (r10 not yet clobbered)
 *   *(r8 + GPR11) = saved r11
 *
 * 64-bit host
 * Expected inputs (GEN/GDBELL/DBG/CRIT/MC exception types):
 *  r10 = saved CR
 *  r13 = PACA_POINTER
 *  *(r13 + PACA_EX##type + EX_R10) = saved r10
 *  *(r13 + PACA_EX##type + EX_R11) = saved r11
 *  SPRN_SPRG_##type##_SCRATCH = saved r13
 *
 * Expected inputs (TLB exception type):
 *  r10 = saved CR
 *  r12 = extlb pointer
 *  r13 = PACA_POINTER
 *  *(r12 + EX_TLB_R10) = saved r10
 *  *(r12 + EX_TLB_R11) = saved r11
 *  *(r12 + EX_TLB_R13) = saved r13
 *  SPRN_SPRG_GEN_SCRATCH = saved r12
 *
 * Only the bolted version of TLB miss exception handlers is supported now.
 */
.macro DO_KVM intno srr1
#ifdef CONFIG_KVM_BOOKE_HV
BEGIN_FTR_SECTION
	mtocrf	0x80, r11	/* check MSR[GS] without clobbering reg */
	bf	3, 1975f
	b	kvmppc_handler_\intno\()_\srr1
1975:
END_FTR_SECTION_IFSET(CPU_FTR_EMB_HV)
#endif
.endm

#endif /*__ASSEMBLY__ */
#endif /* ASM_KVM_BOOKE_HV_ASM_H */
