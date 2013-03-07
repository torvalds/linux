/*
 * CPU-measurement facilities
 *
 *  Copyright IBM Corp. 2012
 *  Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *	       Jan Glauber <jang@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */
#ifndef _ASM_S390_CPU_MF_H
#define _ASM_S390_CPU_MF_H

#include <linux/errno.h>
#include <asm/facility.h>

#define CPU_MF_INT_SF_IAE	(1 << 31)	/* invalid entry address */
#define CPU_MF_INT_SF_ISE	(1 << 30)	/* incorrect SDBT entry */
#define CPU_MF_INT_SF_PRA	(1 << 29)	/* program request alert */
#define CPU_MF_INT_SF_SACA	(1 << 23)	/* sampler auth. change alert */
#define CPU_MF_INT_SF_LSDA	(1 << 22)	/* loss of sample data alert */
#define CPU_MF_INT_CF_CACA	(1 <<  7)	/* counter auth. change alert */
#define CPU_MF_INT_CF_LCDA	(1 <<  6)	/* loss of counter data alert */
#define CPU_MF_INT_RI_HALTED	(1 <<  5)	/* run-time instr. halted */
#define CPU_MF_INT_RI_BUF_FULL	(1 <<  4)	/* run-time instr. program
						   buffer full */

#define CPU_MF_INT_CF_MASK	(CPU_MF_INT_CF_CACA|CPU_MF_INT_CF_LCDA)
#define CPU_MF_INT_SF_MASK	(CPU_MF_INT_SF_IAE|CPU_MF_INT_SF_ISE|	\
				 CPU_MF_INT_SF_PRA|CPU_MF_INT_SF_SACA|	\
				 CPU_MF_INT_SF_LSDA)
#define CPU_MF_INT_RI_MASK	(CPU_MF_INT_RI_HALTED|CPU_MF_INT_RI_BUF_FULL)

/* CPU measurement facility support */
static inline int cpum_cf_avail(void)
{
	return MACHINE_HAS_SPP && test_facility(67);
}

static inline int cpum_sf_avail(void)
{
	return MACHINE_HAS_SPP && test_facility(68);
}


struct cpumf_ctr_info {
	u16   cfvn;
	u16   auth_ctl;
	u16   enable_ctl;
	u16   act_ctl;
	u16   max_cpu;
	u16   csvn;
	u16   max_cg;
	u16   reserved1;
	u32   reserved2[12];
} __packed;

/* Query counter information */
static inline int qctri(struct cpumf_ctr_info *info)
{
	int rc = -EINVAL;

	asm volatile (
		"0:	.insn	s,0xb28e0000,%1\n"
		"1:	lhi	%0,0\n"
		"2:\n"
		EX_TABLE(1b, 2b)
		: "+d" (rc), "=Q" (*info));
	return rc;
}

/* Load CPU-counter-set controls */
static inline int lcctl(u64 ctl)
{
	int cc;

	asm volatile (
		"	.insn	s,0xb2840000,%1\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc) : "m" (ctl) : "cc");
	return cc;
}

/* Extract CPU counter */
static inline int ecctr(u64 ctr, u64 *val)
{
	register u64 content asm("4") = 0;
	int cc;

	asm volatile (
		"	.insn	rre,0xb2e40000,%0,%2\n"
		"	ipm	%1\n"
		"	srl	%1,28\n"
		: "=d" (content), "=d" (cc) : "d" (ctr) : "cc");
	if (!cc)
		*val = content;
	return cc;
}

#endif /* _ASM_S390_CPU_MF_H */
