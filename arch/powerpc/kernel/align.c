/* align.c - handle alignment exceptions for the Power PC.
 *
 * Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 * Copyright (c) 1998-1999 TiVo, Inc.
 *   PowerPC 403GCX modifications.
 * Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *   PowerPC 403GCX/405GP modifications.
 * Copyright (c) 2001-2002 PPC64 team, IBM Corp
 *   64-bit and Power4 support
 * Copyright (c) 2005 Benjamin Herrenschmidt, IBM Corp
 *                    <benh@kernel.crashing.org>
 *   Merge ppc32 and ppc64 implementations
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/emulated_ops.h>
#include <asm/switch_to.h>
#include <asm/disassemble.h>
#include <asm/cpu_has_feature.h>

struct aligninfo {
	unsigned char len;
	unsigned char flags;
};


#define INVALID	{ 0, 0 }

/* Bits in the flags field */
#define LD	0	/* load */
#define ST	1	/* store */
#define SE	2	/* sign-extend value, or FP ld/st as word */
#define F	4	/* to/from fp regs */
#define U	8	/* update index register */
#define M	0x10	/* multiple load/store */
#define SW	0x20	/* byte swap */
#define S	0x40	/* single-precision fp or... */
#define SX	0x40	/* ... byte count in XER */
#define HARD	0x80	/* string, stwcx. */
#define E4	0x40	/* SPE endianness is word */
#define E8	0x80	/* SPE endianness is double word */
#define SPLT	0x80	/* VSX SPLAT load */

/* DSISR bits reported for a DCBZ instruction: */
#define DCBZ	0x5f	/* 8xx/82xx dcbz faults when cache not enabled */

/*
 * The PowerPC stores certain bits of the instruction that caused the
 * alignment exception in the DSISR register.  This array maps those
 * bits to information about the operand length and what the
 * instruction would do.
 */
static struct aligninfo aligninfo[128] = {
	{ 4, LD },		/* 00 0 0000: lwz / lwarx */
	INVALID,		/* 00 0 0001 */
	{ 4, ST },		/* 00 0 0010: stw */
	INVALID,		/* 00 0 0011 */
	{ 2, LD },		/* 00 0 0100: lhz */
	{ 2, LD+SE },		/* 00 0 0101: lha */
	{ 2, ST },		/* 00 0 0110: sth */
	{ 4, LD+M },		/* 00 0 0111: lmw */
	{ 4, LD+F+S },		/* 00 0 1000: lfs */
	{ 8, LD+F },		/* 00 0 1001: lfd */
	{ 4, ST+F+S },		/* 00 0 1010: stfs */
	{ 8, ST+F },		/* 00 0 1011: stfd */
	{ 16, LD },		/* 00 0 1100: lq */
	{ 8, LD },		/* 00 0 1101: ld/ldu/lwa */
	INVALID,		/* 00 0 1110 */
	{ 8, ST },		/* 00 0 1111: std/stdu */
	{ 4, LD+U },		/* 00 1 0000: lwzu */
	INVALID,		/* 00 1 0001 */
	{ 4, ST+U },		/* 00 1 0010: stwu */
	INVALID,		/* 00 1 0011 */
	{ 2, LD+U },		/* 00 1 0100: lhzu */
	{ 2, LD+SE+U },		/* 00 1 0101: lhau */
	{ 2, ST+U },		/* 00 1 0110: sthu */
	{ 4, ST+M },		/* 00 1 0111: stmw */
	{ 4, LD+F+S+U },	/* 00 1 1000: lfsu */
	{ 8, LD+F+U },		/* 00 1 1001: lfdu */
	{ 4, ST+F+S+U },	/* 00 1 1010: stfsu */
	{ 8, ST+F+U },		/* 00 1 1011: stfdu */
	{ 16, LD+F },		/* 00 1 1100: lfdp */
	INVALID,		/* 00 1 1101 */
	{ 16, ST+F },		/* 00 1 1110: stfdp */
	INVALID,		/* 00 1 1111 */
	{ 8, LD },		/* 01 0 0000: ldx */
	INVALID,		/* 01 0 0001 */
	{ 8, ST },		/* 01 0 0010: stdx */
	INVALID,		/* 01 0 0011 */
	INVALID,		/* 01 0 0100 */
	{ 4, LD+SE },		/* 01 0 0101: lwax */
	INVALID,		/* 01 0 0110 */
	INVALID,		/* 01 0 0111 */
	{ 4, LD+M+HARD+SX },	/* 01 0 1000: lswx */
	{ 4, LD+M+HARD },	/* 01 0 1001: lswi */
	{ 4, ST+M+HARD+SX },	/* 01 0 1010: stswx */
	{ 4, ST+M+HARD },	/* 01 0 1011: stswi */
	INVALID,		/* 01 0 1100 */
	{ 8, LD+U },		/* 01 0 1101: ldu */
	INVALID,		/* 01 0 1110 */
	{ 8, ST+U },		/* 01 0 1111: stdu */
	{ 8, LD+U },		/* 01 1 0000: ldux */
	INVALID,		/* 01 1 0001 */
	{ 8, ST+U },		/* 01 1 0010: stdux */
	INVALID,		/* 01 1 0011 */
	INVALID,		/* 01 1 0100 */
	{ 4, LD+SE+U },		/* 01 1 0101: lwaux */
	INVALID,		/* 01 1 0110 */
	INVALID,		/* 01 1 0111 */
	INVALID,		/* 01 1 1000 */
	INVALID,		/* 01 1 1001 */
	INVALID,		/* 01 1 1010 */
	INVALID,		/* 01 1 1011 */
	INVALID,		/* 01 1 1100 */
	INVALID,		/* 01 1 1101 */
	INVALID,		/* 01 1 1110 */
	INVALID,		/* 01 1 1111 */
	INVALID,		/* 10 0 0000 */
	INVALID,		/* 10 0 0001 */
	INVALID,		/* 10 0 0010: stwcx. */
	INVALID,		/* 10 0 0011 */
	INVALID,		/* 10 0 0100 */
	INVALID,		/* 10 0 0101 */
	INVALID,		/* 10 0 0110 */
	INVALID,		/* 10 0 0111 */
	{ 4, LD+SW },		/* 10 0 1000: lwbrx */
	INVALID,		/* 10 0 1001 */
	{ 4, ST+SW },		/* 10 0 1010: stwbrx */
	INVALID,		/* 10 0 1011 */
	{ 2, LD+SW },		/* 10 0 1100: lhbrx */
	{ 4, LD+SE },		/* 10 0 1101  lwa */
	{ 2, ST+SW },		/* 10 0 1110: sthbrx */
	{ 16, ST },		/* 10 0 1111: stq */
	INVALID,		/* 10 1 0000 */
	INVALID,		/* 10 1 0001 */
	INVALID,		/* 10 1 0010 */
	INVALID,		/* 10 1 0011 */
	INVALID,		/* 10 1 0100 */
	INVALID,		/* 10 1 0101 */
	INVALID,		/* 10 1 0110 */
	INVALID,		/* 10 1 0111 */
	INVALID,		/* 10 1 1000 */
	INVALID,		/* 10 1 1001 */
	INVALID,		/* 10 1 1010 */
	INVALID,		/* 10 1 1011 */
	INVALID,		/* 10 1 1100 */
	INVALID,		/* 10 1 1101 */
	INVALID,		/* 10 1 1110 */
	{ 0, ST+HARD },		/* 10 1 1111: dcbz */
	{ 4, LD },		/* 11 0 0000: lwzx */
	INVALID,		/* 11 0 0001 */
	{ 4, ST },		/* 11 0 0010: stwx */
	INVALID,		/* 11 0 0011 */
	{ 2, LD },		/* 11 0 0100: lhzx */
	{ 2, LD+SE },		/* 11 0 0101: lhax */
	{ 2, ST },		/* 11 0 0110: sthx */
	INVALID,		/* 11 0 0111 */
	{ 4, LD+F+S },		/* 11 0 1000: lfsx */
	{ 8, LD+F },		/* 11 0 1001: lfdx */
	{ 4, ST+F+S },		/* 11 0 1010: stfsx */
	{ 8, ST+F },		/* 11 0 1011: stfdx */
	{ 16, LD+F },		/* 11 0 1100: lfdpx */
	{ 4, LD+F+SE },		/* 11 0 1101: lfiwax */
	{ 16, ST+F },		/* 11 0 1110: stfdpx */
	{ 4, ST+F },		/* 11 0 1111: stfiwx */
	{ 4, LD+U },		/* 11 1 0000: lwzux */
	INVALID,		/* 11 1 0001 */
	{ 4, ST+U },		/* 11 1 0010: stwux */
	INVALID,		/* 11 1 0011 */
	{ 2, LD+U },		/* 11 1 0100: lhzux */
	{ 2, LD+SE+U },		/* 11 1 0101: lhaux */
	{ 2, ST+U },		/* 11 1 0110: sthux */
	INVALID,		/* 11 1 0111 */
	{ 4, LD+F+S+U },	/* 11 1 1000: lfsux */
	{ 8, LD+F+U },		/* 11 1 1001: lfdux */
	{ 4, ST+F+S+U },	/* 11 1 1010: stfsux */
	{ 8, ST+F+U },		/* 11 1 1011: stfdux */
	INVALID,		/* 11 1 1100 */
	{ 4, LD+F },		/* 11 1 1101: lfiwzx */
	INVALID,		/* 11 1 1110 */
	INVALID,		/* 11 1 1111 */
};

/*
 * The dcbz (data cache block zero) instruction
 * gives an alignment fault if used on non-cacheable
 * memory.  We handle the fault mainly for the
 * case when we are running with the cache disabled
 * for debugging.
 */
static int emulate_dcbz(struct pt_regs *regs, unsigned char __user *addr)
{
	long __user *p;
	int i, size;

#ifdef __powerpc64__
	size = ppc64_caches.dline_size;
#else
	size = L1_CACHE_BYTES;
#endif
	p = (long __user *) (regs->dar & -size);
	if (user_mode(regs) && !access_ok(VERIFY_WRITE, p, size))
		return -EFAULT;
	for (i = 0; i < size / sizeof(long); ++i)
		if (__put_user_inatomic(0, p+i))
			return -EFAULT;
	return 1;
}

/*
 * Emulate load & store multiple instructions
 * On 64-bit machines, these instructions only affect/use the
 * bottom 4 bytes of each register, and the loads clear the
 * top 4 bytes of the affected register.
 */
#ifdef __BIG_ENDIAN__
#ifdef CONFIG_PPC64
#define REG_BYTE(rp, i)		*((u8 *)((rp) + ((i) >> 2)) + ((i) & 3) + 4)
#else
#define REG_BYTE(rp, i)		*((u8 *)(rp) + (i))
#endif
#else
#define REG_BYTE(rp, i)		(*(((u8 *)((rp) + ((i)>>2)) + ((i)&3))))
#endif

#define SWIZ_PTR(p)		((unsigned char __user *)((p) ^ swiz))

static int emulate_multiple(struct pt_regs *regs, unsigned char __user *addr,
			    unsigned int reg, unsigned int nb,
			    unsigned int flags, unsigned int instr,
			    unsigned long swiz)
{
	unsigned long *rptr;
	unsigned int nb0, i, bswiz;
	unsigned long p;

	/*
	 * We do not try to emulate 8 bytes multiple as they aren't really
	 * available in our operating environments and we don't try to
	 * emulate multiples operations in kernel land as they should never
	 * be used/generated there at least not on unaligned boundaries
	 */
	if (unlikely((nb > 4) || !user_mode(regs)))
		return 0;

	/* lmw, stmw, lswi/x, stswi/x */
	nb0 = 0;
	if (flags & HARD) {
		if (flags & SX) {
			nb = regs->xer & 127;
			if (nb == 0)
				return 1;
		} else {
			unsigned long pc = regs->nip ^ (swiz & 4);

			if (__get_user_inatomic(instr,
						(unsigned int __user *)pc))
				return -EFAULT;
			if (swiz == 0 && (flags & SW))
				instr = cpu_to_le32(instr);
			nb = (instr >> 11) & 0x1f;
			if (nb == 0)
				nb = 32;
		}
		if (nb + reg * 4 > 128) {
			nb0 = nb + reg * 4 - 128;
			nb = 128 - reg * 4;
		}
#ifdef __LITTLE_ENDIAN__
		/*
		 *  String instructions are endian neutral but the code
		 *  below is not.  Force byte swapping on so that the
		 *  effects of swizzling are undone in the load/store
		 *  loops below.
		 */
		flags ^= SW;
#endif
	} else {
		/* lwm, stmw */
		nb = (32 - reg) * 4;
	}

	if (!access_ok((flags & ST ? VERIFY_WRITE: VERIFY_READ), addr, nb+nb0))
		return -EFAULT;	/* bad address */

	rptr = &regs->gpr[reg];
	p = (unsigned long) addr;
	bswiz = (flags & SW)? 3: 0;

	if (!(flags & ST)) {
		/*
		 * This zeroes the top 4 bytes of the affected registers
		 * in 64-bit mode, and also zeroes out any remaining
		 * bytes of the last register for lsw*.
		 */
		memset(rptr, 0, ((nb + 3) / 4) * sizeof(unsigned long));
		if (nb0 > 0)
			memset(&regs->gpr[0], 0,
			       ((nb0 + 3) / 4) * sizeof(unsigned long));

		for (i = 0; i < nb; ++i, ++p)
			if (__get_user_inatomic(REG_BYTE(rptr, i ^ bswiz),
						SWIZ_PTR(p)))
				return -EFAULT;
		if (nb0 > 0) {
			rptr = &regs->gpr[0];
			addr += nb;
			for (i = 0; i < nb0; ++i, ++p)
				if (__get_user_inatomic(REG_BYTE(rptr,
								 i ^ bswiz),
							SWIZ_PTR(p)))
					return -EFAULT;
		}

	} else {
		for (i = 0; i < nb; ++i, ++p)
			if (__put_user_inatomic(REG_BYTE(rptr, i ^ bswiz),
						SWIZ_PTR(p)))
				return -EFAULT;
		if (nb0 > 0) {
			rptr = &regs->gpr[0];
			addr += nb;
			for (i = 0; i < nb0; ++i, ++p)
				if (__put_user_inatomic(REG_BYTE(rptr,
								 i ^ bswiz),
							SWIZ_PTR(p)))
					return -EFAULT;
		}
	}
	return 1;
}

/*
 * Emulate floating-point pair loads and stores.
 * Only POWER6 has these instructions, and it does true little-endian,
 * so we don't need the address swizzling.
 */
static int emulate_fp_pair(unsigned char __user *addr, unsigned int reg,
			   unsigned int flags)
{
	char *ptr0 = (char *) &current->thread.TS_FPR(reg);
	char *ptr1 = (char *) &current->thread.TS_FPR(reg+1);
	int i, ret, sw = 0;

	if (reg & 1)
		return 0;	/* invalid form: FRS/FRT must be even */
	if (flags & SW)
		sw = 7;
	ret = 0;
	for (i = 0; i < 8; ++i) {
		if (!(flags & ST)) {
			ret |= __get_user(ptr0[i^sw], addr + i);
			ret |= __get_user(ptr1[i^sw], addr + i + 8);
		} else {
			ret |= __put_user(ptr0[i^sw], addr + i);
			ret |= __put_user(ptr1[i^sw], addr + i + 8);
		}
	}
	if (ret)
		return -EFAULT;
	return 1;	/* exception handled and fixed up */
}

#ifdef CONFIG_PPC64
static int emulate_lq_stq(struct pt_regs *regs, unsigned char __user *addr,
			  unsigned int reg, unsigned int flags)
{
	char *ptr0 = (char *)&regs->gpr[reg];
	char *ptr1 = (char *)&regs->gpr[reg+1];
	int i, ret, sw = 0;

	if (reg & 1)
		return 0;	/* invalid form: GPR must be even */
	if (flags & SW)
		sw = 7;
	ret = 0;
	for (i = 0; i < 8; ++i) {
		if (!(flags & ST)) {
			ret |= __get_user(ptr0[i^sw], addr + i);
			ret |= __get_user(ptr1[i^sw], addr + i + 8);
		} else {
			ret |= __put_user(ptr0[i^sw], addr + i);
			ret |= __put_user(ptr1[i^sw], addr + i + 8);
		}
	}
	if (ret)
		return -EFAULT;
	return 1;	/* exception handled and fixed up */
}
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_SPE

static struct aligninfo spe_aligninfo[32] = {
	{ 8, LD+E8 },		/* 0 00 00: evldd[x] */
	{ 8, LD+E4 },		/* 0 00 01: evldw[x] */
	{ 8, LD },		/* 0 00 10: evldh[x] */
	INVALID,		/* 0 00 11 */
	{ 2, LD },		/* 0 01 00: evlhhesplat[x] */
	INVALID,		/* 0 01 01 */
	{ 2, LD },		/* 0 01 10: evlhhousplat[x] */
	{ 2, LD+SE },		/* 0 01 11: evlhhossplat[x] */
	{ 4, LD },		/* 0 10 00: evlwhe[x] */
	INVALID,		/* 0 10 01 */
	{ 4, LD },		/* 0 10 10: evlwhou[x] */
	{ 4, LD+SE },		/* 0 10 11: evlwhos[x] */
	{ 4, LD+E4 },		/* 0 11 00: evlwwsplat[x] */
	INVALID,		/* 0 11 01 */
	{ 4, LD },		/* 0 11 10: evlwhsplat[x] */
	INVALID,		/* 0 11 11 */

	{ 8, ST+E8 },		/* 1 00 00: evstdd[x] */
	{ 8, ST+E4 },		/* 1 00 01: evstdw[x] */
	{ 8, ST },		/* 1 00 10: evstdh[x] */
	INVALID,		/* 1 00 11 */
	INVALID,		/* 1 01 00 */
	INVALID,		/* 1 01 01 */
	INVALID,		/* 1 01 10 */
	INVALID,		/* 1 01 11 */
	{ 4, ST },		/* 1 10 00: evstwhe[x] */
	INVALID,		/* 1 10 01 */
	{ 4, ST },		/* 1 10 10: evstwho[x] */
	INVALID,		/* 1 10 11 */
	{ 4, ST+E4 },		/* 1 11 00: evstwwe[x] */
	INVALID,		/* 1 11 01 */
	{ 4, ST+E4 },		/* 1 11 10: evstwwo[x] */
	INVALID,		/* 1 11 11 */
};

#define	EVLDD		0x00
#define	EVLDW		0x01
#define	EVLDH		0x02
#define	EVLHHESPLAT	0x04
#define	EVLHHOUSPLAT	0x06
#define	EVLHHOSSPLAT	0x07
#define	EVLWHE		0x08
#define	EVLWHOU		0x0A
#define	EVLWHOS		0x0B
#define	EVLWWSPLAT	0x0C
#define	EVLWHSPLAT	0x0E
#define	EVSTDD		0x10
#define	EVSTDW		0x11
#define	EVSTDH		0x12
#define	EVSTWHE		0x18
#define	EVSTWHO		0x1A
#define	EVSTWWE		0x1C
#define	EVSTWWO		0x1E

/*
 * Emulate SPE loads and stores.
 * Only Book-E has these instructions, and it does true little-endian,
 * so we don't need the address swizzling.
 */
static int emulate_spe(struct pt_regs *regs, unsigned int reg,
		       unsigned int instr)
{
	int ret;
	union {
		u64 ll;
		u32 w[2];
		u16 h[4];
		u8 v[8];
	} data, temp;
	unsigned char __user *p, *addr;
	unsigned long *evr = &current->thread.evr[reg];
	unsigned int nb, flags;

	instr = (instr >> 1) & 0x1f;

	/* DAR has the operand effective address */
	addr = (unsigned char __user *)regs->dar;

	nb = spe_aligninfo[instr].len;
	flags = spe_aligninfo[instr].flags;

	/* Verify the address of the operand */
	if (unlikely(user_mode(regs) &&
		     !access_ok((flags & ST ? VERIFY_WRITE : VERIFY_READ),
				addr, nb)))
		return -EFAULT;

	/* userland only */
	if (unlikely(!user_mode(regs)))
		return 0;

	flush_spe_to_thread(current);

	/* If we are loading, get the data from user space, else
	 * get it from register values
	 */
	if (flags & ST) {
		data.ll = 0;
		switch (instr) {
		case EVSTDD:
		case EVSTDW:
		case EVSTDH:
			data.w[0] = *evr;
			data.w[1] = regs->gpr[reg];
			break;
		case EVSTWHE:
			data.h[2] = *evr >> 16;
			data.h[3] = regs->gpr[reg] >> 16;
			break;
		case EVSTWHO:
			data.h[2] = *evr & 0xffff;
			data.h[3] = regs->gpr[reg] & 0xffff;
			break;
		case EVSTWWE:
			data.w[1] = *evr;
			break;
		case EVSTWWO:
			data.w[1] = regs->gpr[reg];
			break;
		default:
			return -EINVAL;
		}
	} else {
		temp.ll = data.ll = 0;
		ret = 0;
		p = addr;

		switch (nb) {
		case 8:
			ret |= __get_user_inatomic(temp.v[0], p++);
			ret |= __get_user_inatomic(temp.v[1], p++);
			ret |= __get_user_inatomic(temp.v[2], p++);
			ret |= __get_user_inatomic(temp.v[3], p++);
		case 4:
			ret |= __get_user_inatomic(temp.v[4], p++);
			ret |= __get_user_inatomic(temp.v[5], p++);
		case 2:
			ret |= __get_user_inatomic(temp.v[6], p++);
			ret |= __get_user_inatomic(temp.v[7], p++);
			if (unlikely(ret))
				return -EFAULT;
		}

		switch (instr) {
		case EVLDD:
		case EVLDW:
		case EVLDH:
			data.ll = temp.ll;
			break;
		case EVLHHESPLAT:
			data.h[0] = temp.h[3];
			data.h[2] = temp.h[3];
			break;
		case EVLHHOUSPLAT:
		case EVLHHOSSPLAT:
			data.h[1] = temp.h[3];
			data.h[3] = temp.h[3];
			break;
		case EVLWHE:
			data.h[0] = temp.h[2];
			data.h[2] = temp.h[3];
			break;
		case EVLWHOU:
		case EVLWHOS:
			data.h[1] = temp.h[2];
			data.h[3] = temp.h[3];
			break;
		case EVLWWSPLAT:
			data.w[0] = temp.w[1];
			data.w[1] = temp.w[1];
			break;
		case EVLWHSPLAT:
			data.h[0] = temp.h[2];
			data.h[1] = temp.h[2];
			data.h[2] = temp.h[3];
			data.h[3] = temp.h[3];
			break;
		default:
			return -EINVAL;
		}
	}

	if (flags & SW) {
		switch (flags & 0xf0) {
		case E8:
			data.ll = swab64(data.ll);
			break;
		case E4:
			data.w[0] = swab32(data.w[0]);
			data.w[1] = swab32(data.w[1]);
			break;
		/* Its half word endian */
		default:
			data.h[0] = swab16(data.h[0]);
			data.h[1] = swab16(data.h[1]);
			data.h[2] = swab16(data.h[2]);
			data.h[3] = swab16(data.h[3]);
			break;
		}
	}

	if (flags & SE) {
		data.w[0] = (s16)data.h[1];
		data.w[1] = (s16)data.h[3];
	}

	/* Store result to memory or update registers */
	if (flags & ST) {
		ret = 0;
		p = addr;
		switch (nb) {
		case 8:
			ret |= __put_user_inatomic(data.v[0], p++);
			ret |= __put_user_inatomic(data.v[1], p++);
			ret |= __put_user_inatomic(data.v[2], p++);
			ret |= __put_user_inatomic(data.v[3], p++);
		case 4:
			ret |= __put_user_inatomic(data.v[4], p++);
			ret |= __put_user_inatomic(data.v[5], p++);
		case 2:
			ret |= __put_user_inatomic(data.v[6], p++);
			ret |= __put_user_inatomic(data.v[7], p++);
		}
		if (unlikely(ret))
			return -EFAULT;
	} else {
		*evr = data.w[0];
		regs->gpr[reg] = data.w[1];
	}

	return 1;
}
#endif /* CONFIG_SPE */

#ifdef CONFIG_VSX
/*
 * Emulate VSX instructions...
 */
static int emulate_vsx(unsigned char __user *addr, unsigned int reg,
		       unsigned int areg, struct pt_regs *regs,
		       unsigned int flags, unsigned int length,
		       unsigned int elsize)
{
	char *ptr;
	unsigned long *lptr;
	int ret = 0;
	int sw = 0;
	int i, j;

	/* userland only */
	if (unlikely(!user_mode(regs)))
		return 0;

	flush_vsx_to_thread(current);

	if (reg < 32)
		ptr = (char *) &current->thread.fp_state.fpr[reg][0];
	else
		ptr = (char *) &current->thread.vr_state.vr[reg - 32];

	lptr = (unsigned long *) ptr;

#ifdef __LITTLE_ENDIAN__
	if (flags & SW) {
		elsize = length;
		sw = length-1;
	} else {
		/*
		 * The elements are BE ordered, even in LE mode, so process
		 * them in reverse order.
		 */
		addr += length - elsize;

		/* 8 byte memory accesses go in the top 8 bytes of the VR */
		if (length == 8)
			ptr += 8;
	}
#else
	if (flags & SW)
		sw = elsize-1;
#endif

	for (j = 0; j < length; j += elsize) {
		for (i = 0; i < elsize; ++i) {
			if (flags & ST)
				ret |= __put_user(ptr[i^sw], addr + i);
			else
				ret |= __get_user(ptr[i^sw], addr + i);
		}
		ptr  += elsize;
#ifdef __LITTLE_ENDIAN__
		addr -= elsize;
#else
		addr += elsize;
#endif
	}

#ifdef __BIG_ENDIAN__
#define VSX_HI 0
#define VSX_LO 1
#else
#define VSX_HI 1
#define VSX_LO 0
#endif

	if (!ret) {
		if (flags & U)
			regs->gpr[areg] = regs->dar;

		/* Splat load copies the same data to top and bottom 8 bytes */
		if (flags & SPLT)
			lptr[VSX_LO] = lptr[VSX_HI];
		/* For 8 byte loads, zero the low 8 bytes */
		else if (!(flags & ST) && (8 == length))
			lptr[VSX_LO] = 0;
	} else
		return -EFAULT;

	return 1;
}
#endif

/*
 * Called on alignment exception. Attempts to fixup
 *
 * Return 1 on success
 * Return 0 if unable to handle the interrupt
 * Return -EFAULT if data address is bad
 */

int fix_alignment(struct pt_regs *regs)
{
	unsigned int instr, nb, flags, instruction = 0;
	unsigned int reg, areg;
	unsigned int dsisr;
	unsigned char __user *addr;
	unsigned long p, swiz;
	int ret, i;
	union data {
		u64 ll;
		double dd;
		unsigned char v[8];
		struct {
#ifdef __LITTLE_ENDIAN__
			int	 low32;
			unsigned hi32;
#else
			unsigned hi32;
			int	 low32;
#endif
		} x32;
		struct {
#ifdef __LITTLE_ENDIAN__
			short	      low16;
			unsigned char hi48[6];
#else
			unsigned char hi48[6];
			short	      low16;
#endif
		} x16;
	} data;

	/*
	 * We require a complete register set, if not, then our assembly
	 * is broken
	 */
	CHECK_FULL_REGS(regs);

	dsisr = regs->dsisr;

	/* Some processors don't provide us with a DSISR we can use here,
	 * let's make one up from the instruction
	 */
	if (cpu_has_feature(CPU_FTR_NODSISRALIGN)) {
		unsigned long pc = regs->nip;

		if (cpu_has_feature(CPU_FTR_PPC_LE) && (regs->msr & MSR_LE))
			pc ^= 4;
		if (unlikely(__get_user_inatomic(instr,
						 (unsigned int __user *)pc)))
			return -EFAULT;
		if (cpu_has_feature(CPU_FTR_REAL_LE) && (regs->msr & MSR_LE))
			instr = cpu_to_le32(instr);
		dsisr = make_dsisr(instr);
		instruction = instr;
	}

	/* extract the operation and registers from the dsisr */
	reg = (dsisr >> 5) & 0x1f;	/* source/dest register */
	areg = dsisr & 0x1f;		/* register to update */

#ifdef CONFIG_SPE
	if ((instr >> 26) == 0x4) {
		PPC_WARN_ALIGNMENT(spe, regs);
		return emulate_spe(regs, reg, instr);
	}
#endif

	instr = (dsisr >> 10) & 0x7f;
	instr |= (dsisr >> 13) & 0x60;

	/* Lookup the operation in our table */
	nb = aligninfo[instr].len;
	flags = aligninfo[instr].flags;

	/* ldbrx/stdbrx overlap lfs/stfs in the DSISR unfortunately */
	if (IS_XFORM(instruction) && ((instruction >> 1) & 0x3ff) == 532) {
		nb = 8;
		flags = LD+SW;
	} else if (IS_XFORM(instruction) &&
		   ((instruction >> 1) & 0x3ff) == 660) {
		nb = 8;
		flags = ST+SW;
	}

	/* Byteswap little endian loads and stores */
	swiz = 0;
	if ((regs->msr & MSR_LE) != (MSR_KERNEL & MSR_LE)) {
		flags ^= SW;
#ifdef __BIG_ENDIAN__
		/*
		 * So-called "PowerPC little endian" mode works by
		 * swizzling addresses rather than by actually doing
		 * any byte-swapping.  To emulate this, we XOR each
		 * byte address with 7.  We also byte-swap, because
		 * the processor's address swizzling depends on the
		 * operand size (it xors the address with 7 for bytes,
		 * 6 for halfwords, 4 for words, 0 for doublewords) but
		 * we will xor with 7 and load/store each byte separately.
		 */
		if (cpu_has_feature(CPU_FTR_PPC_LE))
			swiz = 7;
#endif
	}

	/* DAR has the operand effective address */
	addr = (unsigned char __user *)regs->dar;

#ifdef CONFIG_VSX
	if ((instruction & 0xfc00003e) == 0x7c000018) {
		unsigned int elsize;

		/* Additional register addressing bit (64 VSX vs 32 FPR/GPR) */
		reg |= (instruction & 0x1) << 5;
		/* Simple inline decoder instead of a table */
		/* VSX has only 8 and 16 byte memory accesses */
		nb = 8;
		if (instruction & 0x200)
			nb = 16;

		/* Vector stores in little-endian mode swap individual
		   elements, so process them separately */
		elsize = 4;
		if (instruction & 0x80)
			elsize = 8;

		flags = 0;
		if ((regs->msr & MSR_LE) != (MSR_KERNEL & MSR_LE))
			flags |= SW;
		if (instruction & 0x100)
			flags |= ST;
		if (instruction & 0x040)
			flags |= U;
		/* splat load needs a special decoder */
		if ((instruction & 0x400) == 0){
			flags |= SPLT;
			nb = 8;
		}
		PPC_WARN_ALIGNMENT(vsx, regs);
		return emulate_vsx(addr, reg, areg, regs, flags, nb, elsize);
	}
#endif

	/*
	 * ISA 3.0 (such as P9) copy, copy_first, paste and paste_last alignment
	 * check.
	 *
	 * Send a SIGBUS to the process that caused the fault.
	 *
	 * We do not emulate these because paste may contain additional metadata
	 * when pasting to a co-processor. Furthermore, paste_last is the
	 * synchronisation point for preceding copy/paste sequences.
	 */
	if ((instruction & 0xfc0006fe) == PPC_INST_COPY)
		return -EIO;

	/* A size of 0 indicates an instruction we don't support, with
	 * the exception of DCBZ which is handled as a special case here
	 */
	if (instr == DCBZ) {
		PPC_WARN_ALIGNMENT(dcbz, regs);
		return emulate_dcbz(regs, addr);
	}
	if (unlikely(nb == 0))
		return 0;

	/* Load/Store Multiple instructions are handled in their own
	 * function
	 */
	if (flags & M) {
		PPC_WARN_ALIGNMENT(multiple, regs);
		return emulate_multiple(regs, addr, reg, nb,
					flags, instr, swiz);
	}

	/* Verify the address of the operand */
	if (unlikely(user_mode(regs) &&
		     !access_ok((flags & ST ? VERIFY_WRITE : VERIFY_READ),
				addr, nb)))
		return -EFAULT;

	/* Force the fprs into the save area so we can reference them */
	if (flags & F) {
		/* userland only */
		if (unlikely(!user_mode(regs)))
			return 0;
		flush_fp_to_thread(current);
	}

	if (nb == 16) {
		if (flags & F) {
			/* Special case for 16-byte FP loads and stores */
			PPC_WARN_ALIGNMENT(fp_pair, regs);
			return emulate_fp_pair(addr, reg, flags);
		} else {
#ifdef CONFIG_PPC64
			/* Special case for 16-byte loads and stores */
			PPC_WARN_ALIGNMENT(lq_stq, regs);
			return emulate_lq_stq(regs, addr, reg, flags);
#else
			return 0;
#endif
		}
	}

	PPC_WARN_ALIGNMENT(unaligned, regs);

	/* If we are loading, get the data from user space, else
	 * get it from register values
	 */
	if (!(flags & ST)) {
		unsigned int start = 0;

		switch (nb) {
		case 4:
			start = offsetof(union data, x32.low32);
			break;
		case 2:
			start = offsetof(union data, x16.low16);
			break;
		}

		data.ll = 0;
		ret = 0;
		p = (unsigned long)addr;

		for (i = 0; i < nb; i++)
			ret |= __get_user_inatomic(data.v[start + i],
						   SWIZ_PTR(p++));

		if (unlikely(ret))
			return -EFAULT;

	} else if (flags & F) {
		data.ll = current->thread.TS_FPR(reg);
		if (flags & S) {
			/* Single-precision FP store requires conversion... */
#ifdef CONFIG_PPC_FPU
			preempt_disable();
			enable_kernel_fp();
			cvt_df(&data.dd, (float *)&data.x32.low32);
			disable_kernel_fp();
			preempt_enable();
#else
			return 0;
#endif
		}
	} else
		data.ll = regs->gpr[reg];

	if (flags & SW) {
		switch (nb) {
		case 8:
			data.ll = swab64(data.ll);
			break;
		case 4:
			data.x32.low32 = swab32(data.x32.low32);
			break;
		case 2:
			data.x16.low16 = swab16(data.x16.low16);
			break;
		}
	}

	/* Perform other misc operations like sign extension
	 * or floating point single precision conversion
	 */
	switch (flags & ~(U|SW)) {
	case LD+SE:	/* sign extending integer loads */
	case LD+F+SE:	/* sign extend for lfiwax */
		if ( nb == 2 )
			data.ll = data.x16.low16;
		else	/* nb must be 4 */
			data.ll = data.x32.low32;
		break;

	/* Single-precision FP load requires conversion... */
	case LD+F+S:
#ifdef CONFIG_PPC_FPU
		preempt_disable();
		enable_kernel_fp();
		cvt_fd((float *)&data.x32.low32, &data.dd);
		disable_kernel_fp();
		preempt_enable();
#else
		return 0;
#endif
		break;
	}

	/* Store result to memory or update registers */
	if (flags & ST) {
		unsigned int start = 0;

		switch (nb) {
		case 4:
			start = offsetof(union data, x32.low32);
			break;
		case 2:
			start = offsetof(union data, x16.low16);
			break;
		}

		ret = 0;
		p = (unsigned long)addr;

		for (i = 0; i < nb; i++)
			ret |= __put_user_inatomic(data.v[start + i],
						   SWIZ_PTR(p++));

		if (unlikely(ret))
			return -EFAULT;
	} else if (flags & F)
		current->thread.TS_FPR(reg) = data.ll;
	else
		regs->gpr[reg] = data.ll;

	/* Update RA as needed */
	if (flags & U)
		regs->gpr[areg] = regs->dar;

	return 1;
}
