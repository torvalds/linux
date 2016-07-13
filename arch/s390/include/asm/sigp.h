#ifndef __S390_ASM_SIGP_H
#define __S390_ASM_SIGP_H

/* SIGP order codes */
#define SIGP_SENSE		      1
#define SIGP_EXTERNAL_CALL	      2
#define SIGP_EMERGENCY_SIGNAL	      3
#define SIGP_START		      4
#define SIGP_STOP		      5
#define SIGP_RESTART		      6
#define SIGP_STOP_AND_STORE_STATUS    9
#define SIGP_INITIAL_CPU_RESET	     11
#define SIGP_CPU_RESET		     12
#define SIGP_SET_PREFIX		     13
#define SIGP_STORE_STATUS_AT_ADDRESS 14
#define SIGP_SET_ARCHITECTURE	     18
#define SIGP_COND_EMERGENCY_SIGNAL   19
#define SIGP_SENSE_RUNNING	     21
#define SIGP_SET_MULTI_THREADING     22
#define SIGP_STORE_ADDITIONAL_STATUS 23

/* SIGP condition codes */
#define SIGP_CC_ORDER_CODE_ACCEPTED 0
#define SIGP_CC_STATUS_STORED	    1
#define SIGP_CC_BUSY		    2
#define SIGP_CC_NOT_OPERATIONAL	    3

/* SIGP cpu status bits */

#define SIGP_STATUS_INVALID_ORDER	0x00000002UL
#define SIGP_STATUS_CHECK_STOP		0x00000010UL
#define SIGP_STATUS_STOPPED		0x00000040UL
#define SIGP_STATUS_EXT_CALL_PENDING	0x00000080UL
#define SIGP_STATUS_INVALID_PARAMETER	0x00000100UL
#define SIGP_STATUS_INCORRECT_STATE	0x00000200UL
#define SIGP_STATUS_NOT_RUNNING		0x00000400UL

#ifndef __ASSEMBLY__

static inline int __pcpu_sigp(u16 addr, u8 order, unsigned long parm,
			      u32 *status)
{
	register unsigned long reg1 asm ("1") = parm;
	int cc;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc), "+d" (reg1) : "d" (addr), "a" (order) : "cc");
	if (status && cc == 1)
		*status = reg1;
	return cc;
}

#endif /* __ASSEMBLY__ */

#endif /* __S390_ASM_SIGP_H */
