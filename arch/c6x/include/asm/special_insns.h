/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 */
#ifndef _ASM_C6X_SPECIAL_INSNS_H
#define _ASM_C6X_SPECIAL_INSNS_H


#define get_creg(reg) \
	({ unsigned int __x; \
	   asm volatile ("mvc .s2 " #reg ",%0\n" : "=b"(__x)); __x; })

#define set_creg(reg, v) \
	do { unsigned int __x = (unsigned int)(v); \
		asm volatile ("mvc .s2 %0," #reg "\n" : : "b"(__x)); \
	} while (0)

#define or_creg(reg, n) \
	do { unsigned __x, __n = (unsigned)(n);		  \
		asm volatile ("mvc .s2 " #reg ",%0\n"	  \
			      "or  .l2 %1,%0,%0\n"	  \
			      "mvc .s2 %0," #reg "\n"	  \
			      "nop\n"			  \
			      : "=&b"(__x) : "b"(__n));	  \
	} while (0)

#define and_creg(reg, n) \
	do { unsigned __x, __n = (unsigned)(n);		  \
		asm volatile ("mvc .s2 " #reg ",%0\n"	  \
			      "and .l2 %1,%0,%0\n"	  \
			      "mvc .s2 %0," #reg "\n"	  \
			      "nop\n"    \
			      : "=&b"(__x) : "b"(__n));	  \
	} while (0)

#define get_coreid() (get_creg(DNUM) & 0xff)

/* Set/get IST */
#define set_ist(x)	set_creg(ISTP, x)
#define get_ist()       get_creg(ISTP)

/*
 * Exception management
 */
#define disable_exception()
#define get_except_type()        get_creg(EFR)
#define ack_exception(type)      set_creg(ECR, 1 << (type))
#define get_iexcept()            get_creg(IERR)
#define set_iexcept(mask)        set_creg(IERR, (mask))

#define _extu(x, s, e)							\
	({      unsigned int __x;					\
		asm volatile ("extu .S2 %3,%1,%2,%0\n" :		\
			      "=b"(__x) : "n"(s), "n"(e), "b"(x));	\
	       __x; })

#endif /* _ASM_C6X_SPECIAL_INSNS_H */
