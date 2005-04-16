/* 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * FXSAVE<->i387 conversion support. Based on code by Gareth Hughes.
 * This is used for ptrace, signals and coredumps in 32bit emulation.
 * $Id: fpu32.c,v 1.1 2002/03/21 14:16:32 ak Exp $
 */ 

#include <linux/sched.h>
#include <asm/sigcontext32.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/i387.h>

static inline unsigned short twd_i387_to_fxsr(unsigned short twd)
{
	unsigned int tmp; /* to avoid 16 bit prefixes in the code */
 
	/* Transform each pair of bits into 01 (valid) or 00 (empty) */
        tmp = ~twd;
        tmp = (tmp | (tmp>>1)) & 0x5555; /* 0V0V0V0V0V0V0V0V */
        /* and move the valid bits to the lower byte. */
        tmp = (tmp | (tmp >> 1)) & 0x3333; /* 00VV00VV00VV00VV */
        tmp = (tmp | (tmp >> 2)) & 0x0f0f; /* 0000VVVV0000VVVV */
        tmp = (tmp | (tmp >> 4)) & 0x00ff; /* 00000000VVVVVVVV */
        return tmp;
}

static inline unsigned long twd_fxsr_to_i387(struct i387_fxsave_struct *fxsave)
{
	struct _fpxreg *st = NULL;
	unsigned long tos = (fxsave->swd >> 11) & 7;
	unsigned long twd = (unsigned long) fxsave->twd;
	unsigned long tag;
	unsigned long ret = 0xffff0000;
	int i;

#define FPREG_ADDR(f, n)	((void *)&(f)->st_space + (n) * 16);

	for (i = 0 ; i < 8 ; i++) {
		if (twd & 0x1) {
			st = FPREG_ADDR( fxsave, (i - tos) & 7 );

			switch (st->exponent & 0x7fff) {
			case 0x7fff:
				tag = 2;		/* Special */
				break;
			case 0x0000:
				if ( !st->significand[0] &&
				     !st->significand[1] &&
				     !st->significand[2] &&
				     !st->significand[3] ) {
					tag = 1;	/* Zero */
				} else {
					tag = 2;	/* Special */
				}
				break;
			default:
				if (st->significand[3] & 0x8000) {
					tag = 0;	/* Valid */
				} else {
					tag = 2;	/* Special */
				}
				break;
			}
		} else {
			tag = 3;			/* Empty */
		}
		ret |= (tag << (2 * i));
		twd = twd >> 1;
	}
	return ret;
}


static inline int convert_fxsr_from_user(struct i387_fxsave_struct *fxsave,
					 struct _fpstate_ia32 __user *buf)
{
	struct _fpxreg *to;
	struct _fpreg __user *from;
	int i;
	u32 v;
	int err = 0;

#define G(num,val) err |= __get_user(val, num + (u32 __user *)buf)
	G(0, fxsave->cwd);
	G(1, fxsave->swd);
	G(2, fxsave->twd);
	fxsave->twd = twd_i387_to_fxsr(fxsave->twd);
	G(3, fxsave->rip);
	G(4, v);
	fxsave->fop = v>>16;	/* cs ignored */
	G(5, fxsave->rdp);
	/* 6: ds ignored */
#undef G
	if (err) 
		return -1; 

	to = (struct _fpxreg *)&fxsave->st_space[0];
	from = &buf->_st[0];
	for (i = 0 ; i < 8 ; i++, to++, from++) {
		if (__copy_from_user(to, from, sizeof(*from)))
			return -1;
	}
	return 0;
}


static inline int convert_fxsr_to_user(struct _fpstate_ia32 __user *buf,
				       struct i387_fxsave_struct *fxsave,
				       struct pt_regs *regs,
				       struct task_struct *tsk)
{
	struct _fpreg __user *to;
	struct _fpxreg *from;
	int i;
	u16 cs,ds; 
	int err = 0; 

	if (tsk == current) {
		/* should be actually ds/cs at fpu exception time,
		   but that information is not available in 64bit mode. */
		asm("movw %%ds,%0 " : "=r" (ds)); 
		asm("movw %%cs,%0 " : "=r" (cs)); 		
	} else { /* ptrace. task has stopped. */
		ds = tsk->thread.ds;
		cs = regs->cs;
	} 

#define P(num,val) err |= __put_user(val, num + (u32 __user *)buf)
	P(0, (u32)fxsave->cwd | 0xffff0000);
	P(1, (u32)fxsave->swd | 0xffff0000);
	P(2, twd_fxsr_to_i387(fxsave));
	P(3, (u32)fxsave->rip);
	P(4,  cs | ((u32)fxsave->fop) << 16); 
	P(5, fxsave->rdp);
	P(6, 0xffff0000 | ds);
#undef P

	if (err) 
		return -1; 

	to = &buf->_st[0];
	from = (struct _fpxreg *) &fxsave->st_space[0];
	for ( i = 0 ; i < 8 ; i++, to++, from++ ) {
		if (__copy_to_user(to, from, sizeof(*to)))
			return -1;
	}
	return 0;
}

int restore_i387_ia32(struct task_struct *tsk, struct _fpstate_ia32 __user *buf, int fsave) 
{ 
	clear_fpu(tsk);
	if (!fsave) { 
		if (__copy_from_user(&tsk->thread.i387.fxsave, 
				     &buf->_fxsr_env[0],
				     sizeof(struct i387_fxsave_struct)))
			return -1;
		tsk->thread.i387.fxsave.mxcsr &= mxcsr_feature_mask;
		set_stopped_child_used_math(tsk);
	} 
	return convert_fxsr_from_user(&tsk->thread.i387.fxsave, buf);
}  

int save_i387_ia32(struct task_struct *tsk, 
		   struct _fpstate_ia32 __user *buf, 
		   struct pt_regs *regs,
		   int fsave)
{
	int err = 0;

	init_fpu(tsk);
	if (convert_fxsr_to_user(buf, &tsk->thread.i387.fxsave, regs, tsk))
		return -1;
	if (fsave)
		return 0;
	err |= __put_user(tsk->thread.i387.fxsave.swd, &buf->status);
	if (fsave) 
		return err ? -1 : 1; 	
	err |= __put_user(X86_FXSR_MAGIC, &buf->magic);
	err |= __copy_to_user(&buf->_fxsr_env[0], &tsk->thread.i387.fxsave,
			      sizeof(struct i387_fxsave_struct));
	return err ? -1 : 1;
}
