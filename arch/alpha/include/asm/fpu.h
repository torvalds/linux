#ifndef __ASM_ALPHA_FPU_H
#define __ASM_ALPHA_FPU_H

#include <asm/special_insns.h>
#include <uapi/asm/fpu.h>

/* The following two functions don't need trapb/excb instructions
   around the mf_fpcr/mt_fpcr instructions because (a) the kernel
   never generates arithmetic faults and (b) call_pal instructions
   are implied trap barriers.  */

static inline unsigned long
rdfpcr(void)
{
	unsigned long tmp, ret;

#if defined(CONFIG_ALPHA_EV6) || defined(CONFIG_ALPHA_EV67)
	__asm__ __volatile__ (
		"ftoit $f0,%0\n\t"
		"mf_fpcr $f0\n\t"
		"ftoit $f0,%1\n\t"
		"itoft %0,$f0"
		: "=r"(tmp), "=r"(ret));
#else
	__asm__ __volatile__ (
		"stt $f0,%0\n\t"
		"mf_fpcr $f0\n\t"
		"stt $f0,%1\n\t"
		"ldt $f0,%0"
		: "=m"(tmp), "=m"(ret));
#endif

	return ret;
}

static inline void
wrfpcr(unsigned long val)
{
	unsigned long tmp;

#if defined(CONFIG_ALPHA_EV6) || defined(CONFIG_ALPHA_EV67)
	__asm__ __volatile__ (
		"ftoit $f0,%0\n\t"
		"itoft %1,$f0\n\t"
		"mt_fpcr $f0\n\t"
		"itoft %0,$f0"
		: "=&r"(tmp) : "r"(val));
#else
	__asm__ __volatile__ (
		"stt $f0,%0\n\t"
		"ldt $f0,%1\n\t"
		"mt_fpcr $f0\n\t"
		"ldt $f0,%0"
		: "=m"(tmp) : "m"(val));
#endif
}

static inline unsigned long
swcr_update_status(unsigned long swcr, unsigned long fpcr)
{
	/* EV6 implements most of the bits in hardware.  Collect
	   the acrued exception bits from the real fpcr.  */
	if (implver() == IMPLVER_EV6) {
		swcr &= ~IEEE_STATUS_MASK;
		swcr |= (fpcr >> 35) & IEEE_STATUS_MASK;
	}
	return swcr;
}

extern unsigned long alpha_read_fp_reg (unsigned long reg);
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
extern unsigned long alpha_read_fp_reg_s (unsigned long reg);
extern void alpha_write_fp_reg_s (unsigned long reg, unsigned long val);

#endif /* __ASM_ALPHA_FPU_H */
