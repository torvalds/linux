// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *    Unaligned memory access handler
 *
 *    Copyright (C) 2001 Randolph Chung <tausq@debian.org>
 *    Copyright (C) 2022 Helge Deller <deller@gmx.de>
 *    Significantly tweaked by LaMont Jones <lamont@debian.org>
 */

#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/ratelimit.h>
#include <linux/uaccess.h>
#include <linux/sysctl.h>
#include <linux/unaligned.h>
#include <linux/perf_event.h>
#include <asm/hardirq.h>
#include <asm/traps.h>
#include "unaligned.h"

/* #define DEBUG_UNALIGNED 1 */

#ifdef DEBUG_UNALIGNED
#define DPRINTF(fmt, args...) do { printk(KERN_DEBUG "%s:%d:%s ", __FILE__, __LINE__, __func__ ); printk(KERN_DEBUG fmt, ##args ); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#define RFMT "0x%08lx"

/* 1111 1100 0000 0000 0001 0011 1100 0000 */
#define OPCODE1(a,b,c)	((a)<<26|(b)<<12|(c)<<6) 
#define OPCODE2(a,b)	((a)<<26|(b)<<1)
#define OPCODE3(a,b)	((a)<<26|(b)<<2)
#define OPCODE4(a)	((a)<<26)
#define OPCODE1_MASK	OPCODE1(0x3f,1,0xf)
#define OPCODE2_MASK 	OPCODE2(0x3f,1)
#define OPCODE3_MASK	OPCODE3(0x3f,1)
#define OPCODE4_MASK    OPCODE4(0x3f)

/* skip LDB - never unaligned (index) */
#define OPCODE_LDH_I	OPCODE1(0x03,0,0x1)
#define OPCODE_LDW_I	OPCODE1(0x03,0,0x2)
#define OPCODE_LDD_I	OPCODE1(0x03,0,0x3)
#define OPCODE_LDDA_I	OPCODE1(0x03,0,0x4)
#define OPCODE_LDCD_I	OPCODE1(0x03,0,0x5)
#define OPCODE_LDWA_I	OPCODE1(0x03,0,0x6)
#define OPCODE_LDCW_I	OPCODE1(0x03,0,0x7)
/* skip LDB - never unaligned (short) */
#define OPCODE_LDH_S	OPCODE1(0x03,1,0x1)
#define OPCODE_LDW_S	OPCODE1(0x03,1,0x2)
#define OPCODE_LDD_S	OPCODE1(0x03,1,0x3)
#define OPCODE_LDDA_S	OPCODE1(0x03,1,0x4)
#define OPCODE_LDCD_S	OPCODE1(0x03,1,0x5)
#define OPCODE_LDWA_S	OPCODE1(0x03,1,0x6)
#define OPCODE_LDCW_S	OPCODE1(0x03,1,0x7)
/* skip STB - never unaligned */
#define OPCODE_STH	OPCODE1(0x03,1,0x9)
#define OPCODE_STW	OPCODE1(0x03,1,0xa)
#define OPCODE_STD	OPCODE1(0x03,1,0xb)
/* skip STBY - never unaligned */
/* skip STDBY - never unaligned */
#define OPCODE_STWA	OPCODE1(0x03,1,0xe)
#define OPCODE_STDA	OPCODE1(0x03,1,0xf)

#define OPCODE_FLDWX	OPCODE1(0x09,0,0x0)
#define OPCODE_FLDWXR	OPCODE1(0x09,0,0x1)
#define OPCODE_FSTWX	OPCODE1(0x09,0,0x8)
#define OPCODE_FSTWXR	OPCODE1(0x09,0,0x9)
#define OPCODE_FLDWS	OPCODE1(0x09,1,0x0)
#define OPCODE_FLDWSR	OPCODE1(0x09,1,0x1)
#define OPCODE_FSTWS	OPCODE1(0x09,1,0x8)
#define OPCODE_FSTWSR	OPCODE1(0x09,1,0x9)
#define OPCODE_FLDDX	OPCODE1(0x0b,0,0x0)
#define OPCODE_FSTDX	OPCODE1(0x0b,0,0x8)
#define OPCODE_FLDDS	OPCODE1(0x0b,1,0x0)
#define OPCODE_FSTDS	OPCODE1(0x0b,1,0x8)

#define OPCODE_LDD_L	OPCODE2(0x14,0)
#define OPCODE_FLDD_L	OPCODE2(0x14,1)
#define OPCODE_STD_L	OPCODE2(0x1c,0)
#define OPCODE_FSTD_L	OPCODE2(0x1c,1)

#define OPCODE_LDW_M	OPCODE3(0x17,1)
#define OPCODE_FLDW_L	OPCODE3(0x17,0)
#define OPCODE_FSTW_L	OPCODE3(0x1f,0)
#define OPCODE_STW_M	OPCODE3(0x1f,1)

#define OPCODE_LDH_L    OPCODE4(0x11)
#define OPCODE_LDW_L    OPCODE4(0x12)
#define OPCODE_LDWM     OPCODE4(0x13)
#define OPCODE_STH_L    OPCODE4(0x19)
#define OPCODE_STW_L    OPCODE4(0x1A)
#define OPCODE_STWM     OPCODE4(0x1B)

#define MAJOR_OP(i) (((i)>>26)&0x3f)
#define R1(i) (((i)>>21)&0x1f)
#define R2(i) (((i)>>16)&0x1f)
#define R3(i) ((i)&0x1f)
#define FR3(i) ((((i)&0x1f)<<1)|(((i)>>6)&1))
#define IM(i,n) (((i)>>1&((1<<(n-1))-1))|((i)&1?((0-1L)<<(n-1)):0))
#define IM5_2(i) IM((i)>>16,5)
#define IM5_3(i) IM((i),5)
#define IM14(i) IM((i),14)

#define ERR_NOTHANDLED	-1

int unaligned_enabled __read_mostly = 1;
int no_unaligned_warning __read_mostly;

static int emulate_ldh(struct pt_regs *regs, int toreg)
{
	unsigned long saddr = regs->ior;
	unsigned long val = 0, temp1;
	ASM_EXCEPTIONTABLE_VAR(ret);

	DPRINTF("load " RFMT ":" RFMT " to r%d for 2 bytes\n", 
		regs->isr, regs->ior, toreg);

	__asm__ __volatile__  (
"	mtsp	%4, %%sr1\n"
"1:	ldbs	0(%%sr1,%3), %2\n"
"2:	ldbs	1(%%sr1,%3), %0\n"
"	depw	%2, 23, 24, %0\n"
"3:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 3b, "%1")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 3b, "%1")
	: "+r" (val), "+r" (ret), "=&r" (temp1)
	: "r" (saddr), "r" (regs->isr) );

	DPRINTF("val = " RFMT "\n", val);

	if (toreg)
		regs->gr[toreg] = val;

	return ret;
}

static int emulate_ldw(struct pt_regs *regs, int toreg, int flop)
{
	unsigned long saddr = regs->ior;
	unsigned long val = 0, temp1, temp2;
	ASM_EXCEPTIONTABLE_VAR(ret);

	DPRINTF("load " RFMT ":" RFMT " to r%d for 4 bytes\n", 
		regs->isr, regs->ior, toreg);

	__asm__ __volatile__  (
"	zdep	%4,28,2,%2\n"		/* r19=(ofs&3)*8 */
"	mtsp	%5, %%sr1\n"
"	depw	%%r0,31,2,%4\n"
"1:	ldw	0(%%sr1,%4),%0\n"
"2:	ldw	4(%%sr1,%4),%3\n"
"	subi	32,%2,%2\n"
"	mtctl	%2,11\n"
"	vshd	%0,%3,%0\n"
"3:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 3b, "%1")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 3b, "%1")
	: "+r" (val), "+r" (ret), "=&r" (temp1), "=&r" (temp2)
	: "r" (saddr), "r" (regs->isr) );

	DPRINTF("val = " RFMT "\n", val);

	if (flop)
		((__u32*)(regs->fr))[toreg] = val;
	else if (toreg)
		regs->gr[toreg] = val;

	return ret;
}
static int emulate_ldd(struct pt_regs *regs, int toreg, int flop)
{
	unsigned long saddr = regs->ior;
	unsigned long shift, temp1;
	__u64 val = 0;
	ASM_EXCEPTIONTABLE_VAR(ret);

	DPRINTF("load " RFMT ":" RFMT " to r%d for 8 bytes\n", 
		regs->isr, regs->ior, toreg);

	if (!IS_ENABLED(CONFIG_64BIT) && !flop)
		return ERR_NOTHANDLED;

#ifdef CONFIG_64BIT
	__asm__ __volatile__  (
"	depd,z	%2,60,3,%3\n"		/* shift=(ofs&7)*8 */
"	mtsp	%5, %%sr1\n"
"	depd	%%r0,63,3,%2\n"
"1:	ldd	0(%%sr1,%2),%0\n"
"2:	ldd	8(%%sr1,%2),%4\n"
"	subi	64,%3,%3\n"
"	mtsar	%3\n"
"	shrpd	%0,%4,%%sar,%0\n"
"3:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 3b, "%1")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 3b, "%1")
	: "+r" (val), "+r" (ret), "+r" (saddr), "=&r" (shift), "=&r" (temp1)
	: "r" (regs->isr) );
#else
	__asm__ __volatile__  (
"	zdep	%2,29,2,%3\n"		/* shift=(ofs&3)*8 */
"	mtsp	%5, %%sr1\n"
"	dep	%%r0,31,2,%2\n"
"1:	ldw	0(%%sr1,%2),%0\n"
"2:	ldw	4(%%sr1,%2),%R0\n"
"3:	ldw	8(%%sr1,%2),%4\n"
"	subi	32,%3,%3\n"
"	mtsar	%3\n"
"	vshd	%0,%R0,%0\n"
"	vshd	%R0,%4,%R0\n"
"4:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 4b, "%1")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 4b, "%1")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(3b, 4b, "%1")
	: "+r" (val), "+r" (ret), "+r" (saddr), "=&r" (shift), "=&r" (temp1)
	: "r" (regs->isr) );
#endif

	DPRINTF("val = 0x%llx\n", val);

	if (flop)
		regs->fr[toreg] = val;
	else if (toreg)
		regs->gr[toreg] = val;

	return ret;
}

static int emulate_sth(struct pt_regs *regs, int frreg)
{
	unsigned long val = regs->gr[frreg], temp1;
	ASM_EXCEPTIONTABLE_VAR(ret);

	if (!frreg)
		val = 0;

	DPRINTF("store r%d (" RFMT ") to " RFMT ":" RFMT " for 2 bytes\n", frreg,
		val, regs->isr, regs->ior);

	__asm__ __volatile__ (
"	mtsp %4, %%sr1\n"
"	extrw,u %2, 23, 8, %1\n"
"1:	stb %1, 0(%%sr1, %3)\n"
"2:	stb %2, 1(%%sr1, %3)\n"
"3:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 3b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 3b, "%0")
	: "+r" (ret), "=&r" (temp1)
	: "r" (val), "r" (regs->ior), "r" (regs->isr) );

	return ret;
}

static int emulate_stw(struct pt_regs *regs, int frreg, int flop)
{
	unsigned long val;
	ASM_EXCEPTIONTABLE_VAR(ret);

	if (flop)
		val = ((__u32*)(regs->fr))[frreg];
	else if (frreg)
		val = regs->gr[frreg];
	else
		val = 0;

	DPRINTF("store r%d (" RFMT ") to " RFMT ":" RFMT " for 4 bytes\n", frreg,
		val, regs->isr, regs->ior);


	__asm__ __volatile__ (
"	mtsp %3, %%sr1\n"
"	zdep	%2, 28, 2, %%r19\n"
"	dep	%%r0, 31, 2, %2\n"
"	mtsar	%%r19\n"
"	depwi,z	-2, %%sar, 32, %%r19\n"
"1:	ldw	0(%%sr1,%2),%%r20\n"
"2:	ldw	4(%%sr1,%2),%%r21\n"
"	vshd	%%r0, %1, %%r22\n"
"	vshd	%1, %%r0, %%r1\n"
"	and	%%r20, %%r19, %%r20\n"
"	andcm	%%r21, %%r19, %%r21\n"
"	or	%%r22, %%r20, %%r20\n"
"	or	%%r1, %%r21, %%r21\n"
"	stw	%%r20,0(%%sr1,%2)\n"
"	stw	%%r21,4(%%sr1,%2)\n"
"3:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 3b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 3b, "%0")
	: "+r" (ret)
	: "r" (val), "r" (regs->ior), "r" (regs->isr)
	: "r19", "r20", "r21", "r22", "r1" );

	return ret;
}
static int emulate_std(struct pt_regs *regs, int frreg, int flop)
{
	__u64 val;
	ASM_EXCEPTIONTABLE_VAR(ret);

	if (flop)
		val = regs->fr[frreg];
	else if (frreg)
		val = regs->gr[frreg];
	else
		val = 0;

	DPRINTF("store r%d (0x%016llx) to " RFMT ":" RFMT " for 8 bytes\n", frreg, 
		val,  regs->isr, regs->ior);

	if (!IS_ENABLED(CONFIG_64BIT) && !flop)
		return ERR_NOTHANDLED;

#ifdef CONFIG_64BIT
	__asm__ __volatile__ (
"	mtsp %3, %%sr1\n"
"	depd,z	%2, 60, 3, %%r19\n"
"	depd	%%r0, 63, 3, %2\n"
"	mtsar	%%r19\n"
"	depdi,z	-2, %%sar, 64, %%r19\n"
"1:	ldd	0(%%sr1,%2),%%r20\n"
"2:	ldd	8(%%sr1,%2),%%r21\n"
"	shrpd	%%r0, %1, %%sar, %%r22\n"
"	shrpd	%1, %%r0, %%sar, %%r1\n"
"	and	%%r20, %%r19, %%r20\n"
"	andcm	%%r21, %%r19, %%r21\n"
"	or	%%r22, %%r20, %%r20\n"
"	or	%%r1, %%r21, %%r21\n"
"3:	std	%%r20,0(%%sr1,%2)\n"
"4:	std	%%r21,8(%%sr1,%2)\n"
"5:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 5b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 5b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(3b, 5b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(4b, 5b, "%0")
	: "+r" (ret)
	: "r" (val), "r" (regs->ior), "r" (regs->isr)
	: "r19", "r20", "r21", "r22", "r1" );
#else
    {
	__asm__ __volatile__ (
"	mtsp	%3, %%sr1\n"
"	zdep	%R1, 29, 2, %%r19\n"
"	dep	%%r0, 31, 2, %2\n"
"	mtsar	%%r19\n"
"	zvdepi	-2, 32, %%r19\n"
"1:	ldw	0(%%sr1,%2),%%r20\n"
"2:	ldw	8(%%sr1,%2),%%r21\n"
"	vshd	%1, %R1, %%r1\n"
"	vshd	%%r0, %1, %1\n"
"	vshd	%R1, %%r0, %R1\n"
"	and	%%r20, %%r19, %%r20\n"
"	andcm	%%r21, %%r19, %%r21\n"
"	or	%1, %%r20, %1\n"
"	or	%R1, %%r21, %R1\n"
"3:	stw	%1,0(%%sr1,%2)\n"
"4:	stw	%%r1,4(%%sr1,%2)\n"
"5:	stw	%R1,8(%%sr1,%2)\n"
"6:	\n"
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(1b, 6b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(2b, 6b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(3b, 6b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(4b, 6b, "%0")
	ASM_EXCEPTIONTABLE_ENTRY_EFAULT(5b, 6b, "%0")
	: "+r" (ret)
	: "r" (val), "r" (regs->ior), "r" (regs->isr)
	: "r19", "r20", "r21", "r1" );
    }
#endif

	return ret;
}

void handle_unaligned(struct pt_regs *regs)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 5);
	unsigned long newbase = R1(regs->iir)?regs->gr[R1(regs->iir)]:0;
	int modify = 0;
	int ret = ERR_NOTHANDLED;

	__inc_irq_stat(irq_unaligned_count);
	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, regs->ior);

	/* log a message with pacing */
	if (user_mode(regs)) {
		if (current->thread.flags & PARISC_UAC_SIGBUS) {
			goto force_sigbus;
		}

		if (!(current->thread.flags & PARISC_UAC_NOPRINT) &&
			__ratelimit(&ratelimit)) {
			printk(KERN_WARNING "%s(%d): unaligned access to " RFMT
				" at ip " RFMT " (iir " RFMT ")\n",
				current->comm, task_pid_nr(current), regs->ior,
				regs->iaoq[0], regs->iir);
#ifdef DEBUG_UNALIGNED
			show_regs(regs);
#endif		
		}

		if (!unaligned_enabled)
			goto force_sigbus;
	} else {
		static DEFINE_RATELIMIT_STATE(kernel_ratelimit, 5 * HZ, 5);
		if (!(current->thread.flags & PARISC_UAC_NOPRINT) &&
			!no_unaligned_warning &&
			__ratelimit(&kernel_ratelimit))
			pr_warn("Kernel: unaligned access to " RFMT " in %pS "
					"(iir " RFMT ")\n",
				regs->ior, (void *)regs->iaoq[0], regs->iir);
	}

	/* handle modification - OK, it's ugly, see the instruction manual */
	switch (MAJOR_OP(regs->iir))
	{
	case 0x03:
	case 0x09:
	case 0x0b:
		if (regs->iir&0x20)
		{
			modify = 1;
			if (regs->iir&0x1000)		/* short loads */
				if (regs->iir&0x200)
					newbase += IM5_3(regs->iir);
				else
					newbase += IM5_2(regs->iir);
			else if (regs->iir&0x2000)	/* scaled indexed */
			{
				int shift=0;
				switch (regs->iir & OPCODE1_MASK)
				{
				case OPCODE_LDH_I:
					shift= 1; break;
				case OPCODE_LDW_I:
					shift= 2; break;
				case OPCODE_LDD_I:
				case OPCODE_LDDA_I:
					shift= 3; break;
				}
				newbase += (R2(regs->iir)?regs->gr[R2(regs->iir)]:0)<<shift;
			} else				/* simple indexed */
				newbase += (R2(regs->iir)?regs->gr[R2(regs->iir)]:0);
		}
		break;
	case 0x13:
	case 0x1b:
		modify = 1;
		newbase += IM14(regs->iir);
		break;
	case 0x14:
	case 0x1c:
		if (regs->iir&8)
		{
			modify = 1;
			newbase += IM14(regs->iir&~0xe);
		}
		break;
	case 0x16:
	case 0x1e:
		modify = 1;
		newbase += IM14(regs->iir&6);
		break;
	case 0x17:
	case 0x1f:
		if (regs->iir&4)
		{
			modify = 1;
			newbase += IM14(regs->iir&~4);
		}
		break;
	}

	/* TODO: make this cleaner... */
	switch (regs->iir & OPCODE1_MASK)
	{
	case OPCODE_LDH_I:
	case OPCODE_LDH_S:
		ret = emulate_ldh(regs, R3(regs->iir));
		break;

	case OPCODE_LDW_I:
	case OPCODE_LDWA_I:
	case OPCODE_LDW_S:
	case OPCODE_LDWA_S:
		ret = emulate_ldw(regs, R3(regs->iir), 0);
		break;

	case OPCODE_STH:
		ret = emulate_sth(regs, R2(regs->iir));
		break;

	case OPCODE_STW:
	case OPCODE_STWA:
		ret = emulate_stw(regs, R2(regs->iir), 0);
		break;

#ifdef CONFIG_64BIT
	case OPCODE_LDD_I:
	case OPCODE_LDDA_I:
	case OPCODE_LDD_S:
	case OPCODE_LDDA_S:
		ret = emulate_ldd(regs, R3(regs->iir), 0);
		break;

	case OPCODE_STD:
	case OPCODE_STDA:
		ret = emulate_std(regs, R2(regs->iir), 0);
		break;
#endif

	case OPCODE_FLDWX:
	case OPCODE_FLDWS:
	case OPCODE_FLDWXR:
	case OPCODE_FLDWSR:
		ret = emulate_ldw(regs, FR3(regs->iir), 1);
		break;

	case OPCODE_FLDDX:
	case OPCODE_FLDDS:
		ret = emulate_ldd(regs, R3(regs->iir), 1);
		break;

	case OPCODE_FSTWX:
	case OPCODE_FSTWS:
	case OPCODE_FSTWXR:
	case OPCODE_FSTWSR:
		ret = emulate_stw(regs, FR3(regs->iir), 1);
		break;

	case OPCODE_FSTDX:
	case OPCODE_FSTDS:
		ret = emulate_std(regs, R3(regs->iir), 1);
		break;

	case OPCODE_LDCD_I:
	case OPCODE_LDCW_I:
	case OPCODE_LDCD_S:
	case OPCODE_LDCW_S:
		ret = ERR_NOTHANDLED;	/* "undefined", but lets kill them. */
		break;
	}
	switch (regs->iir & OPCODE2_MASK)
	{
	case OPCODE_FLDD_L:
		ret = emulate_ldd(regs,R2(regs->iir),1);
		break;
	case OPCODE_FSTD_L:
		ret = emulate_std(regs, R2(regs->iir),1);
		break;
#ifdef CONFIG_64BIT
	case OPCODE_LDD_L:
		ret = emulate_ldd(regs, R2(regs->iir),0);
		break;
	case OPCODE_STD_L:
		ret = emulate_std(regs, R2(regs->iir),0);
		break;
#endif
	}
	switch (regs->iir & OPCODE3_MASK)
	{
	case OPCODE_FLDW_L:
		ret = emulate_ldw(regs, R2(regs->iir), 1);
		break;
	case OPCODE_LDW_M:
		ret = emulate_ldw(regs, R2(regs->iir), 0);
		break;

	case OPCODE_FSTW_L:
		ret = emulate_stw(regs, R2(regs->iir),1);
		break;
	case OPCODE_STW_M:
		ret = emulate_stw(regs, R2(regs->iir),0);
		break;
	}
	switch (regs->iir & OPCODE4_MASK)
	{
	case OPCODE_LDH_L:
		ret = emulate_ldh(regs, R2(regs->iir));
		break;
	case OPCODE_LDW_L:
	case OPCODE_LDWM:
		ret = emulate_ldw(regs, R2(regs->iir),0);
		break;
	case OPCODE_STH_L:
		ret = emulate_sth(regs, R2(regs->iir));
		break;
	case OPCODE_STW_L:
	case OPCODE_STWM:
		ret = emulate_stw(regs, R2(regs->iir),0);
		break;
	}

	if (ret == 0 && modify && R1(regs->iir))
		regs->gr[R1(regs->iir)] = newbase;


	if (ret == ERR_NOTHANDLED)
		printk(KERN_CRIT "Not-handled unaligned insn 0x%08lx\n", regs->iir);

	DPRINTF("ret = %d\n", ret);

	if (ret)
	{
		/*
		 * The unaligned handler failed.
		 * If we were called by __get_user() or __put_user() jump
		 * to it's exception fixup handler instead of crashing.
		 */
		if (!user_mode(regs) && fixup_exception(regs))
			return;

		printk(KERN_CRIT "Unaligned handler failed, ret = %d\n", ret);
		die_if_kernel("Unaligned data reference", regs, 28);

		if (ret == -EFAULT)
		{
			force_sig_fault(SIGSEGV, SEGV_MAPERR,
					(void __user *)regs->ior);
		}
		else
		{
force_sigbus:
			/* couldn't handle it ... */
			force_sig_fault(SIGBUS, BUS_ADRALN,
					(void __user *)regs->ior);
		}
		
		return;
	}

	/* else we handled it, let life go on. */
	regs->gr[0]|=PSW_N;
}

/*
 * NB: check_unaligned() is only used for PCXS processors right
 * now, so we only check for PA1.1 encodings at this point.
 */

int
check_unaligned(struct pt_regs *regs)
{
	unsigned long align_mask;

	/* Get alignment mask */

	align_mask = 0UL;
	switch (regs->iir & OPCODE1_MASK) {

	case OPCODE_LDH_I:
	case OPCODE_LDH_S:
	case OPCODE_STH:
		align_mask = 1UL;
		break;

	case OPCODE_LDW_I:
	case OPCODE_LDWA_I:
	case OPCODE_LDW_S:
	case OPCODE_LDWA_S:
	case OPCODE_STW:
	case OPCODE_STWA:
		align_mask = 3UL;
		break;

	default:
		switch (regs->iir & OPCODE4_MASK) {
		case OPCODE_LDH_L:
		case OPCODE_STH_L:
			align_mask = 1UL;
			break;
		case OPCODE_LDW_L:
		case OPCODE_LDWM:
		case OPCODE_STW_L:
		case OPCODE_STWM:
			align_mask = 3UL;
			break;
		}
		break;
	}

	return (int)(regs->ior & align_mask);
}

