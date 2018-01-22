/* SPDX-License-Identifier: GPL-2.0 */
/*
 * definition for paravirtual devices on s390
 *
 * Copyright IBM Corp. 2008
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */
/*
 * Hypercalls for KVM on s390. The calling convention is similar to the
 * s390 ABI, so we use R2-R6 for parameters 1-5. In addition we use R1
 * as hypercall number and R7 as parameter 6. The return value is
 * written to R2. We use the diagnose instruction as hypercall. To avoid
 * conflicts with existing diagnoses for LPAR and z/VM, we do not use
 * the instruction encoded number, but specify the number in R1 and
 * use 0x500 as KVM hypercall
 *
 * Copyright IBM Corp. 2007,2008
 * Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */
#ifndef __S390_KVM_PARA_H
#define __S390_KVM_PARA_H

#include <uapi/asm/kvm_para.h>
#include <asm/diag.h>

static inline long __kvm_hypercall0(unsigned long nr)
{
	register unsigned long __nr asm("1") = nr;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr): "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall0(unsigned long nr)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall0(nr);
}

static inline long __kvm_hypercall1(unsigned long nr, unsigned long p1)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1) : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall1(unsigned long nr, unsigned long p1)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall1(nr, p1);
}

static inline long __kvm_hypercall2(unsigned long nr, unsigned long p1,
			       unsigned long p2)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register unsigned long __p2 asm("3") = p2;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1), "d" (__p2)
		      : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall2(unsigned long nr, unsigned long p1,
			       unsigned long p2)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall2(nr, p1, p2);
}

static inline long __kvm_hypercall3(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register unsigned long __p2 asm("3") = p2;
	register unsigned long __p3 asm("4") = p3;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1), "d" (__p2),
			"d" (__p3) : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall3(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall3(nr, p1, p2, p3);
}

static inline long __kvm_hypercall4(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register unsigned long __p2 asm("3") = p2;
	register unsigned long __p3 asm("4") = p3;
	register unsigned long __p4 asm("5") = p4;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1), "d" (__p2),
			"d" (__p3), "d" (__p4) : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall4(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall4(nr, p1, p2, p3, p4);
}

static inline long __kvm_hypercall5(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4, unsigned long p5)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register unsigned long __p2 asm("3") = p2;
	register unsigned long __p3 asm("4") = p3;
	register unsigned long __p4 asm("5") = p4;
	register unsigned long __p5 asm("6") = p5;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1), "d" (__p2),
			"d" (__p3), "d" (__p4), "d" (__p5)  : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall5(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4, unsigned long p5)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall5(nr, p1, p2, p3, p4, p5);
}

static inline long __kvm_hypercall6(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4, unsigned long p5,
			       unsigned long p6)
{
	register unsigned long __nr asm("1") = nr;
	register unsigned long __p1 asm("2") = p1;
	register unsigned long __p2 asm("3") = p2;
	register unsigned long __p3 asm("4") = p3;
	register unsigned long __p4 asm("5") = p4;
	register unsigned long __p5 asm("6") = p5;
	register unsigned long __p6 asm("7") = p6;
	register long __rc asm("2");

	asm volatile ("diag 2,4,0x500\n"
		      : "=d" (__rc) : "d" (__nr), "0" (__p1), "d" (__p2),
			"d" (__p3), "d" (__p4), "d" (__p5), "d" (__p6)
		      : "memory", "cc");
	return __rc;
}

static inline long kvm_hypercall6(unsigned long nr, unsigned long p1,
			       unsigned long p2, unsigned long p3,
			       unsigned long p4, unsigned long p5,
			       unsigned long p6)
{
	diag_stat_inc(DIAG_STAT_X500);
	return __kvm_hypercall6(nr, p1, p2, p3, p4, p5, p6);
}

/* kvm on s390 is always paravirtualization enabled */
static inline int kvm_para_available(void)
{
	return 1;
}

/* No feature bits are currently assigned for kvm on s390 */
static inline unsigned int kvm_arch_para_features(void)
{
	return 0;
}

static inline bool kvm_check_and_clear_guest_paused(void)
{
	return false;
}

#endif /* __S390_KVM_PARA_H */
