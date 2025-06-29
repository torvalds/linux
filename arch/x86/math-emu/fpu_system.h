/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------+
 |  fpu_system.h                                                             |
 |                                                                           |
 | Copyright (C) 1992,1994,1997                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@suburbia.net             |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _FPU_SYSTEM_H
#define _FPU_SYSTEM_H

/* system dependent definitions */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/desc.h>
#include <asm/mmu_context.h>

static inline struct desc_struct FPU_get_ldt_descriptor(unsigned seg)
{
	static struct desc_struct zero_desc;
	struct desc_struct ret = zero_desc;

#ifdef CONFIG_MODIFY_LDT_SYSCALL
	seg >>= 3;
	mutex_lock(&current->mm->context.lock);
	if (current->mm->context.ldt && seg < current->mm->context.ldt->nr_entries)
		ret = current->mm->context.ldt->entries[seg];
	mutex_unlock(&current->mm->context.lock);
#endif
	return ret;
}

#define SEG_TYPE_WRITABLE	(1U << 1)
#define SEG_TYPE_EXPANDS_DOWN	(1U << 2)
#define SEG_TYPE_EXECUTE	(1U << 3)
#define SEG_TYPE_EXPAND_MASK	(SEG_TYPE_EXPANDS_DOWN | SEG_TYPE_EXECUTE)
#define SEG_TYPE_EXECUTE_MASK	(SEG_TYPE_WRITABLE | SEG_TYPE_EXECUTE)

static inline unsigned long seg_get_base(struct desc_struct *d)
{
	unsigned long base = (unsigned long)d->base2 << 24;

	return base | ((unsigned long)d->base1 << 16) | d->base0;
}

static inline unsigned long seg_get_limit(struct desc_struct *d)
{
	return ((unsigned long)d->limit1 << 16) | d->limit0;
}

static inline unsigned long seg_get_granularity(struct desc_struct *d)
{
	return d->g ? 4096 : 1;
}

static inline bool seg_expands_down(struct desc_struct *d)
{
	return (d->type & SEG_TYPE_EXPAND_MASK) == SEG_TYPE_EXPANDS_DOWN;
}

static inline bool seg_execute_only(struct desc_struct *d)
{
	return (d->type & SEG_TYPE_EXECUTE_MASK) == SEG_TYPE_EXECUTE;
}

static inline bool seg_writable(struct desc_struct *d)
{
	return (d->type & SEG_TYPE_EXECUTE_MASK) == SEG_TYPE_WRITABLE;
}

#define I387			(&x86_task_fpu(current)->fpstate->regs)
#define FPU_info		(I387->soft.info)

#define FPU_CS			(*(unsigned short *) &(FPU_info->regs->cs))
#define FPU_SS			(*(unsigned short *) &(FPU_info->regs->ss))
#define FPU_DS			(*(unsigned short *) &(FPU_info->regs->ds))
#define FPU_EAX			(FPU_info->regs->ax)
#define FPU_EFLAGS		(FPU_info->regs->flags)
#define FPU_EIP			(FPU_info->regs->ip)
#define FPU_ORIG_EIP		(FPU_info->___orig_eip)

#define FPU_lookahead           (I387->soft.lookahead)

/* nz if ip_offset and cs_selector are not to be set for the current
   instruction. */
#define no_ip_update		(*(u_char *)&(I387->soft.no_update))
#define FPU_rm			(*(u_char *)&(I387->soft.rm))

/* Number of bytes of data which can be legally accessed by the current
   instruction. This only needs to hold a number <= 108, so a byte will do. */
#define access_limit		(*(u_char *)&(I387->soft.alimit))

#define partial_status		(I387->soft.swd)
#define control_word		(I387->soft.cwd)
#define fpu_tag_word		(I387->soft.twd)
#define registers		(I387->soft.st_space)
#define top			(I387->soft.ftop)

#define instruction_address	(*(struct address *)&I387->soft.fip)
#define operand_address		(*(struct address *)&I387->soft.foo)

#define FPU_access_ok(y,z)	if ( !access_ok(y,z) ) \
				math_abort(FPU_info,SIGSEGV)
#define FPU_abort		math_abort(FPU_info, SIGSEGV)
#define FPU_copy_from_user(to, from, n)	\
		do { if (copy_from_user(to, from, n)) FPU_abort; } while (0)

#undef FPU_IGNORE_CODE_SEGV
#ifdef FPU_IGNORE_CODE_SEGV
/* access_ok() is very expensive, and causes the emulator to run
   about 20% slower if applied to the code. Anyway, errors due to bad
   code addresses should be much rarer than errors due to bad data
   addresses. */
#define	FPU_code_access_ok(z)
#else
/* A simpler test than access_ok() can probably be done for
   FPU_code_access_ok() because the only possible error is to step
   past the upper boundary of a legal code area. */
#define	FPU_code_access_ok(z) FPU_access_ok((void __user *)FPU_EIP,z)
#endif

#define FPU_get_user(x,y) do { if (get_user((x),(y))) FPU_abort; } while (0)
#define FPU_put_user(x,y) do { if (put_user((x),(y))) FPU_abort; } while (0)

#endif
