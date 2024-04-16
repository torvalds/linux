/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_ALPHA_FPU_H
#define _UAPI__ASM_ALPHA_FPU_H


/*
 * Alpha floating-point control register defines:
 */
#define FPCR_DNOD	(1UL<<47)	/* denorm INV trap disable */
#define FPCR_DNZ	(1UL<<48)	/* denorms to zero */
#define FPCR_INVD	(1UL<<49)	/* invalid op disable (opt.) */
#define FPCR_DZED	(1UL<<50)	/* division by zero disable (opt.) */
#define FPCR_OVFD	(1UL<<51)	/* overflow disable (optional) */
#define FPCR_INV	(1UL<<52)	/* invalid operation */
#define FPCR_DZE	(1UL<<53)	/* division by zero */
#define FPCR_OVF	(1UL<<54)	/* overflow */
#define FPCR_UNF	(1UL<<55)	/* underflow */
#define FPCR_INE	(1UL<<56)	/* inexact */
#define FPCR_IOV	(1UL<<57)	/* integer overflow */
#define FPCR_UNDZ	(1UL<<60)	/* underflow to zero (opt.) */
#define FPCR_UNFD	(1UL<<61)	/* underflow disable (opt.) */
#define FPCR_INED	(1UL<<62)	/* inexact disable (opt.) */
#define FPCR_SUM	(1UL<<63)	/* summary bit */

#define FPCR_DYN_SHIFT	58		/* first dynamic rounding mode bit */
#define FPCR_DYN_CHOPPED (0x0UL << FPCR_DYN_SHIFT)	/* towards 0 */
#define FPCR_DYN_MINUS	 (0x1UL << FPCR_DYN_SHIFT)	/* towards -INF */
#define FPCR_DYN_NORMAL	 (0x2UL << FPCR_DYN_SHIFT)	/* towards nearest */
#define FPCR_DYN_PLUS	 (0x3UL << FPCR_DYN_SHIFT)	/* towards +INF */
#define FPCR_DYN_MASK	 (0x3UL << FPCR_DYN_SHIFT)

#define FPCR_MASK	0xffff800000000000L

/*
 * IEEE trap enables are implemented in software.  These per-thread
 * bits are stored in the "ieee_state" field of "struct thread_info".
 * Thus, the bits are defined so as not to conflict with the
 * floating-point enable bit (which is architected).  On top of that,
 * we want to make these bits compatible with OSF/1 so
 * ieee_set_fp_control() etc. can be implemented easily and
 * compatibly.  The corresponding definitions are in
 * /usr/include/machine/fpu.h under OSF/1.
 */
#define IEEE_TRAP_ENABLE_INV	(1UL<<1)	/* invalid op */
#define IEEE_TRAP_ENABLE_DZE	(1UL<<2)	/* division by zero */
#define IEEE_TRAP_ENABLE_OVF	(1UL<<3)	/* overflow */
#define IEEE_TRAP_ENABLE_UNF	(1UL<<4)	/* underflow */
#define IEEE_TRAP_ENABLE_INE	(1UL<<5)	/* inexact */
#define IEEE_TRAP_ENABLE_DNO	(1UL<<6)	/* denorm */
#define IEEE_TRAP_ENABLE_MASK	(IEEE_TRAP_ENABLE_INV | IEEE_TRAP_ENABLE_DZE |\
				 IEEE_TRAP_ENABLE_OVF | IEEE_TRAP_ENABLE_UNF |\
				 IEEE_TRAP_ENABLE_INE | IEEE_TRAP_ENABLE_DNO)

/* Denorm and Underflow flushing */
#define IEEE_MAP_DMZ		(1UL<<12)	/* Map denorm inputs to zero */
#define IEEE_MAP_UMZ		(1UL<<13)	/* Map underflowed outputs to zero */

#define IEEE_MAP_MASK		(IEEE_MAP_DMZ | IEEE_MAP_UMZ)

/* status bits coming from fpcr: */
#define IEEE_STATUS_INV		(1UL<<17)
#define IEEE_STATUS_DZE		(1UL<<18)
#define IEEE_STATUS_OVF		(1UL<<19)
#define IEEE_STATUS_UNF		(1UL<<20)
#define IEEE_STATUS_INE		(1UL<<21)
#define IEEE_STATUS_DNO		(1UL<<22)

#define IEEE_STATUS_MASK	(IEEE_STATUS_INV | IEEE_STATUS_DZE |	\
				 IEEE_STATUS_OVF | IEEE_STATUS_UNF |	\
				 IEEE_STATUS_INE | IEEE_STATUS_DNO)

#define IEEE_SW_MASK		(IEEE_TRAP_ENABLE_MASK |		\
				 IEEE_STATUS_MASK | IEEE_MAP_MASK)

#define IEEE_CURRENT_RM_SHIFT	32
#define IEEE_CURRENT_RM_MASK	(3UL<<IEEE_CURRENT_RM_SHIFT)

#define IEEE_STATUS_TO_EXCSUM_SHIFT	16

#define IEEE_INHERIT    (1UL<<63)	/* inherit on thread create? */

/*
 * Convert the software IEEE trap enable and status bits into the
 * hardware fpcr format. 
 *
 * Digital Unix engineers receive my thanks for not defining the
 * software bits identical to the hardware bits.  The chip designers
 * receive my thanks for making all the not-implemented fpcr bits
 * RAZ forcing us to use system calls to read/write this value.
 */

static inline unsigned long
ieee_swcr_to_fpcr(unsigned long sw)
{
	unsigned long fp;
	fp = (sw & IEEE_STATUS_MASK) << 35;
	fp |= (sw & IEEE_MAP_DMZ) << 36;
	fp |= (sw & IEEE_STATUS_MASK ? FPCR_SUM : 0);
	fp |= (~sw & (IEEE_TRAP_ENABLE_INV
		      | IEEE_TRAP_ENABLE_DZE
		      | IEEE_TRAP_ENABLE_OVF)) << 48;
	fp |= (~sw & (IEEE_TRAP_ENABLE_UNF | IEEE_TRAP_ENABLE_INE)) << 57;
	fp |= (sw & IEEE_MAP_UMZ ? FPCR_UNDZ | FPCR_UNFD : 0);
	fp |= (~sw & IEEE_TRAP_ENABLE_DNO) << 41;
	return fp;
}

static inline unsigned long
ieee_fpcr_to_swcr(unsigned long fp)
{
	unsigned long sw;
	sw = (fp >> 35) & IEEE_STATUS_MASK;
	sw |= (fp >> 36) & IEEE_MAP_DMZ;
	sw |= (~fp >> 48) & (IEEE_TRAP_ENABLE_INV
			     | IEEE_TRAP_ENABLE_DZE
			     | IEEE_TRAP_ENABLE_OVF);
	sw |= (~fp >> 57) & (IEEE_TRAP_ENABLE_UNF | IEEE_TRAP_ENABLE_INE);
	sw |= (fp >> 47) & IEEE_MAP_UMZ;
	sw |= (~fp >> 41) & IEEE_TRAP_ENABLE_DNO;
	return sw;
}


#endif /* _UAPI__ASM_ALPHA_FPU_H */
