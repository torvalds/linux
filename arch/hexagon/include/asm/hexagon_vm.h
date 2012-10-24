/*
 * Declarations for to Hexagon Virtal Machine.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef ASM_HEXAGON_VM_H
#define ASM_HEXAGON_VM_H

/*
 * In principle, a Linux kernel for the VM could
 * selectively define the virtual instructions
 * as inline assembler macros, but for a first pass,
 * we'll use subroutines for both the VM and the native
 * kernels.  It's costing a subroutine call/return,
 * but it makes for a single set of entry points
 * for tracing/debugging.
 */

/*
 * Lets make this stuff visible only if configured,
 * so we can unconditionally include the file.
 */

#ifndef __ASSEMBLY__

enum VM_CACHE_OPS {
	ickill,
	dckill,
	l2kill,
	dccleaninva,
	icinva,
	idsync,
	fetch_cfg
};

enum VM_INT_OPS {
	nop,
	globen,
	globdis,
	locen,
	locdis,
	affinity,
	get,
	peek,
	status,
	post,
	clear
};

extern void _K_VM_event_vector(void);

void __vmrte(void);
long __vmsetvec(void *);
long __vmsetie(long);
long __vmgetie(void);
long __vmintop(enum VM_INT_OPS, long, long, long, long);
long __vmclrmap(void *, unsigned long);
long __vmnewmap(void *);
long __vmcache(enum VM_CACHE_OPS op, unsigned long addr, unsigned long len);
unsigned long long __vmgettime(void);
long __vmsettime(unsigned long long);
long __vmstart(void *, void *);
void __vmstop(void);
long __vmwait(void);
void __vmyield(void);
long __vmvpid(void);

static inline long __vmcache_ickill(void)
{
	return __vmcache(ickill, 0, 0);
}

static inline long __vmcache_dckill(void)
{
	return __vmcache(dckill, 0, 0);
}

static inline long __vmcache_l2kill(void)
{
	return __vmcache(l2kill, 0, 0);
}

static inline long __vmcache_dccleaninva(unsigned long addr, unsigned long len)
{
	return __vmcache(dccleaninva, addr, len);
}

static inline long __vmcache_icinva(unsigned long addr, unsigned long len)
{
	return __vmcache(icinva, addr, len);
}

static inline long __vmcache_idsync(unsigned long addr,
					   unsigned long len)
{
	return __vmcache(idsync, addr, len);
}

static inline long __vmcache_fetch_cfg(unsigned long val)
{
	return __vmcache(fetch_cfg, val, 0);
}

/* interrupt operations  */

static inline long __vmintop_nop(void)
{
	return __vmintop(nop, 0, 0, 0, 0);
}

static inline long __vmintop_globen(long i)
{
	return __vmintop(globen, i, 0, 0, 0);
}

static inline long __vmintop_globdis(long i)
{
	return __vmintop(globdis, i, 0, 0, 0);
}

static inline long __vmintop_locen(long i)
{
	return __vmintop(locen, i, 0, 0, 0);
}

static inline long __vmintop_locdis(long i)
{
	return __vmintop(locdis, i, 0, 0, 0);
}

static inline long __vmintop_affinity(long i, long cpu)
{
	return __vmintop(locdis, i, cpu, 0, 0);
}

static inline long __vmintop_get(void)
{
	return __vmintop(get, 0, 0, 0, 0);
}

static inline long __vmintop_peek(void)
{
	return __vmintop(peek, 0, 0, 0, 0);
}

static inline long __vmintop_status(long i)
{
	return __vmintop(status, i, 0, 0, 0);
}

static inline long __vmintop_post(long i)
{
	return __vmintop(post, i, 0, 0, 0);
}

static inline long __vmintop_clear(long i)
{
	return __vmintop(clear, i, 0, 0, 0);
}

#else /* Only assembly code should reference these */

#define HVM_TRAP1_VMRTE			1
#define HVM_TRAP1_VMSETVEC		2
#define HVM_TRAP1_VMSETIE		3
#define HVM_TRAP1_VMGETIE		4
#define HVM_TRAP1_VMINTOP		5
#define HVM_TRAP1_VMCLRMAP		10
#define HVM_TRAP1_VMNEWMAP		11
#define HVM_TRAP1_FORMERLY_VMWIRE	12
#define HVM_TRAP1_VMCACHE		13
#define HVM_TRAP1_VMGETTIME		14
#define HVM_TRAP1_VMSETTIME		15
#define HVM_TRAP1_VMWAIT		16
#define HVM_TRAP1_VMYIELD		17
#define HVM_TRAP1_VMSTART		18
#define HVM_TRAP1_VMSTOP		19
#define HVM_TRAP1_VMVPID		20
#define HVM_TRAP1_VMSETREGS		21
#define HVM_TRAP1_VMGETREGS		22

#endif /* __ASSEMBLY__ */

/*
 * Constants for virtual instruction parameters and return values
 */

/* vmsetie arguments */

#define VM_INT_DISABLE	0
#define VM_INT_ENABLE	1

/* vmsetimask arguments */

#define VM_INT_UNMASK	0
#define VM_INT_MASK	1

#define VM_NEWMAP_TYPE_LINEAR	0
#define VM_NEWMAP_TYPE_PGTABLES	1


/*
 * Event Record definitions useful to both C and Assembler
 */

/* VMEST Layout */

#define HVM_VMEST_UM_SFT	31
#define HVM_VMEST_UM_MSK	1
#define HVM_VMEST_IE_SFT	30
#define HVM_VMEST_IE_MSK	1
#define HVM_VMEST_EVENTNUM_SFT	16
#define HVM_VMEST_EVENTNUM_MSK	0xff
#define HVM_VMEST_CAUSE_SFT	0
#define HVM_VMEST_CAUSE_MSK	0xffff

/*
 * The initial program gets to find a system environment descriptor
 * on its stack when it begins exection. The first word is a version
 * code to indicate what is there.  Zero means nothing more.
 */

#define HEXAGON_VM_SED_NULL	0

/*
 * Event numbers for vector binding
 */

#define HVM_EV_RESET		0
#define HVM_EV_MACHCHECK	1
#define HVM_EV_GENEX		2
#define HVM_EV_TRAP		8
#define HVM_EV_INTR		15
/* These shoud be nuked as soon as we know the VM is up to spec v0.1.1 */
#define HVM_EV_INTR_0		16
#define HVM_MAX_INTR		240

/*
 * Cause values for General Exception
 */

#define HVM_GE_C_BUS	0x01
#define HVM_GE_C_XPROT	0x11
#define HVM_GE_C_XUSER	0x14
#define HVM_GE_C_INVI	0x15
#define HVM_GE_C_PRIVI	0x1B
#define HVM_GE_C_XMAL	0x1C
#define HVM_GE_C_RMAL	0x20
#define HVM_GE_C_WMAL	0x21
#define HVM_GE_C_RPROT	0x22
#define HVM_GE_C_WPROT	0x23
#define HVM_GE_C_RUSER	0x24
#define HVM_GE_C_WUSER	0x25
#define HVM_GE_C_CACHE	0x28

/*
 * Cause codes for Machine Check
 */

#define	HVM_MCHK_C_DOWN		0x00
#define	HVM_MCHK_C_BADSP	0x01
#define	HVM_MCHK_C_BADEX	0x02
#define	HVM_MCHK_C_BADPT	0x03
#define	HVM_MCHK_C_REGWR	0x29

#endif
