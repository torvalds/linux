/*
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef ASM_KVM_BOOKE_HV_ASM_H
#define ASM_KVM_BOOKE_HV_ASM_H

#ifdef __ASSEMBLY__

/*
 * All exceptions from guest state must go through KVM
 * (except for those which are delivered directly to the guest) --
 * there are no exceptions for which we fall through directly to
 * the normal host handler.
 *
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
 */
.macro DO_KVM intno srr1
#ifdef CONFIG_KVM_BOOKE_HV
BEGIN_FTR_SECTION
	mtocrf	0x80, r11	/* check MSR[GS] without clobbering reg */
	bf	3, kvmppc_resume_\intno\()_\srr1
	b	kvmppc_handler_\intno\()_\srr1
kvmppc_resume_\intno\()_\srr1:
END_FTR_SECTION_IFSET(CPU_FTR_EMB_HV)
#endif
.endm

#endif /*__ASSEMBLY__ */
#endif /* ASM_KVM_BOOKE_HV_ASM_H */
