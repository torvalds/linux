#ifndef _ASM_POWERPC_TM_H
#define _ASM_POWERPC_TM_H

/* Reason codes describing kernel causes for transaction aborts.  By
 * convention, bit0 is copied to TEXASR[56] (IBM bit 7) which is set if
 * the failure is persistent.  PAPR saves 0xff-0xe0 for the hypervisor.
 */
#define TM_CAUSE_PERSISTENT	0x01
#define TM_CAUSE_KVM_RESCHED	0xe0  /* From PAPR */
#define TM_CAUSE_KVM_FAC_UNAV	0xe2  /* From PAPR */
#define TM_CAUSE_RESCHED	0xde
#define TM_CAUSE_TLBI		0xdc
#define TM_CAUSE_FAC_UNAV	0xda
#define TM_CAUSE_SYSCALL	0xd8  /* future use */
#define TM_CAUSE_MISC		0xd6  /* future use */
#define TM_CAUSE_SIGNAL		0xd4
#define TM_CAUSE_ALIGNMENT	0xd2
#define TM_CAUSE_EMULATE	0xd0

#endif
