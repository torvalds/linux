/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/compiler.h>
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/elf.h"
#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "sysdep/sc.h"

void arch_switch_to_tt(struct task_struct *from, struct task_struct *to)
{
	update_debugregs(to->thread.arch.debugregs_seq);
	arch_switch_tls_tt(from, to);
}

void arch_switch_to_skas(struct task_struct *from, struct task_struct *to)
{
	int err = arch_switch_tls_skas(from, to);
	if (!err)
		return;

	if (err != -EINVAL)
		printk(KERN_WARNING "arch_switch_tls_skas failed, errno %d, not EINVAL\n", -err);
	else
		printk(KERN_WARNING "arch_switch_tls_skas failed, errno = EINVAL\n");
}

int is_syscall(unsigned long addr)
{
	unsigned short instr;
	int n;

	n = copy_from_user(&instr, (void __user *) addr, sizeof(instr));
	if(n){
		/* access_process_vm() grants access to vsyscall and stub,
		 * while copy_from_user doesn't. Maybe access_process_vm is
		 * slow, but that doesn't matter, since it will be called only
		 * in case of singlestepping, if copy_from_user failed.
		 */
		n = access_process_vm(current, addr, &instr, sizeof(instr), 0);
		if(n != sizeof(instr)) {
			printk("is_syscall : failed to read instruction from "
			       "0x%lx\n", addr);
			return(1);
		}
	}
	/* int 0x80 or sysenter */
	return((instr == 0x80cd) || (instr == 0x340f));
}

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

int putreg(struct task_struct *child, int regno, unsigned long value)
{
	regno >>= 2;
	switch (regno) {
	case FS:
		if (value && (value & 3) != 3)
			return -EIO;
		PT_REGS_FS(&child->thread.regs) = value;
		return 0;
	case GS:
		if (value && (value & 3) != 3)
			return -EIO;
		PT_REGS_GS(&child->thread.regs) = value;
		return 0;
	case DS:
	case ES:
		if (value && (value & 3) != 3)
			return -EIO;
		value &= 0xffff;
		break;
	case SS:
	case CS:
		if ((value & 3) != 3)
			return -EIO;
		value &= 0xffff;
		break;
	case EFL:
		value &= FLAG_MASK;
		value |= PT_REGS_EFLAGS(&child->thread.regs);
		break;
	}
	PT_REGS_SET(&child->thread.regs, regno, value);
	return 0;
}

int poke_user(struct task_struct *child, long addr, long data)
{
        if ((addr & 3) || addr < 0)
                return -EIO;

        if (addr < MAX_REG_OFFSET)
                return putreg(child, addr, data);

        else if((addr >= offsetof(struct user, u_debugreg[0])) &&
                (addr <= offsetof(struct user, u_debugreg[7]))){
                addr -= offsetof(struct user, u_debugreg[0]);
                addr = addr >> 2;
                if((addr == 4) || (addr == 5)) return -EIO;
                child->thread.arch.debugregs[addr] = data;
                return 0;
        }
        return -EIO;
}

unsigned long getreg(struct task_struct *child, int regno)
{
	unsigned long retval = ~0UL;

	regno >>= 2;
	switch (regno) {
	case FS:
	case GS:
	case DS:
	case ES:
	case SS:
	case CS:
		retval = 0xffff;
		/* fall through */
	default:
		retval &= PT_REG(&child->thread.regs, regno);
	}
	return retval;
}

int peek_user(struct task_struct *child, long addr, long data)
{
/* read the word at location addr in the USER area. */
	unsigned long tmp;

	if ((addr & 3) || addr < 0)
		return -EIO;

	tmp = 0;  /* Default return condition */
	if(addr < MAX_REG_OFFSET){
		tmp = getreg(child, addr);
	}
	else if((addr >= offsetof(struct user, u_debugreg[0])) &&
		(addr <= offsetof(struct user, u_debugreg[7]))){
		addr -= offsetof(struct user, u_debugreg[0]);
		addr = addr >> 2;
		tmp = child->thread.arch.debugregs[addr];
	}
	return put_user(tmp, (unsigned long __user *) data);
}

struct i387_fxsave_struct {
	unsigned short	cwd;
	unsigned short	swd;
	unsigned short	twd;
	unsigned short	fop;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	mxcsr;
	long	reserved;
	long	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	long	xmm_space[32];	/* 8*16 bytes for each XMM-reg = 128 bytes */
	long	padding[56];
};

/*
 * FPU tag word conversions.
 */

static inline unsigned short twd_i387_to_fxsr( unsigned short twd )
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

static inline unsigned long twd_fxsr_to_i387( struct i387_fxsave_struct *fxsave )
{
	struct _fpxreg *st = NULL;
	unsigned long twd = (unsigned long) fxsave->twd;
	unsigned long tag;
	unsigned long ret = 0xffff0000;
	int i;

#define FPREG_ADDR(f, n)	((char *)&(f)->st_space + (n) * 16);

	for ( i = 0 ; i < 8 ; i++ ) {
		if ( twd & 0x1 ) {
			st = (struct _fpxreg *) FPREG_ADDR( fxsave, i );

			switch ( st->exponent & 0x7fff ) {
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
				if ( st->significand[3] & 0x8000 ) {
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

/*
 * FXSR floating point environment conversions.
 */

#ifdef CONFIG_MODE_TT
static inline int convert_fxsr_to_user_tt(struct _fpstate __user *buf,
					  struct pt_regs *regs)
{
	struct i387_fxsave_struct *fxsave = SC_FXSR_ENV(PT_REGS_SC(regs));
	unsigned long env[7];
	struct _fpreg __user *to;
	struct _fpxreg *from;
	int i;

	env[0] = (unsigned long)fxsave->cwd | 0xffff0000;
	env[1] = (unsigned long)fxsave->swd | 0xffff0000;
	env[2] = twd_fxsr_to_i387(fxsave);
	env[3] = fxsave->fip;
	env[4] = fxsave->fcs | ((unsigned long)fxsave->fop << 16);
	env[5] = fxsave->foo;
	env[6] = fxsave->fos;

	if ( __copy_to_user( buf, env, 7 * sizeof(unsigned long) ) )
		return 1;

	to = &buf->_st[0];
	from = (struct _fpxreg *) &fxsave->st_space[0];
	for ( i = 0 ; i < 8 ; i++, to++, from++ ) {
		if ( __copy_to_user( to, from, sizeof(*to) ) )
			return 1;
	}
	return 0;
}
#endif

static inline int convert_fxsr_to_user(struct _fpstate __user *buf,
				       struct pt_regs *regs)
{
	return(CHOOSE_MODE(convert_fxsr_to_user_tt(buf, regs), 0));
}

#ifdef CONFIG_MODE_TT
static inline int convert_fxsr_from_user_tt(struct pt_regs *regs,
					    struct _fpstate __user *buf)
{
	struct i387_fxsave_struct *fxsave = SC_FXSR_ENV(PT_REGS_SC(regs));
	unsigned long env[7];
	struct _fpxreg *to;
	struct _fpreg __user *from;
	int i;

	if ( __copy_from_user( env, buf, 7 * sizeof(long) ) )
		return 1;

	fxsave->cwd = (unsigned short)(env[0] & 0xffff);
	fxsave->swd = (unsigned short)(env[1] & 0xffff);
	fxsave->twd = twd_i387_to_fxsr((unsigned short)(env[2] & 0xffff));
	fxsave->fip = env[3];
	fxsave->fop = (unsigned short)((env[4] & 0xffff0000) >> 16);
	fxsave->fcs = (env[4] & 0xffff);
	fxsave->foo = env[5];
	fxsave->fos = env[6];

	to = (struct _fpxreg *) &fxsave->st_space[0];
	from = &buf->_st[0];
	for ( i = 0 ; i < 8 ; i++, to++, from++ ) {
		if ( __copy_from_user( to, from, sizeof(*from) ) )
			return 1;
	}
	return 0;
}
#endif

static inline int convert_fxsr_from_user(struct pt_regs *regs, 
					 struct _fpstate __user *buf)
{
	return(CHOOSE_MODE(convert_fxsr_from_user_tt(regs, buf), 0));
}

int get_fpregs(unsigned long buf, struct task_struct *child)
{
	int err;

	err = convert_fxsr_to_user((struct _fpstate __user *) buf,
				   &child->thread.regs);
	if(err) return(-EFAULT);
	else return(0);
}

int set_fpregs(unsigned long buf, struct task_struct *child)
{
	int err;

	err = convert_fxsr_from_user(&child->thread.regs, 
				     (struct _fpstate __user *) buf);
	if(err) return(-EFAULT);
	else return(0);
}

#ifdef CONFIG_MODE_TT
int get_fpxregs_tt(unsigned long buf, struct task_struct *tsk)
{
	struct pt_regs *regs = &tsk->thread.regs;
	struct i387_fxsave_struct *fxsave = SC_FXSR_ENV(PT_REGS_SC(regs));
	int err;

	err = __copy_to_user((void __user *) buf, fxsave,
			     sizeof(struct user_fxsr_struct));
	if(err) return -EFAULT;
	else return 0;
}
#endif

int get_fpxregs(unsigned long buf, struct task_struct *tsk)
{
	return(CHOOSE_MODE(get_fpxregs_tt(buf, tsk), 0));
}

#ifdef CONFIG_MODE_TT
int set_fpxregs_tt(unsigned long buf, struct task_struct *tsk)
{
	struct pt_regs *regs = &tsk->thread.regs;
	struct i387_fxsave_struct *fxsave = SC_FXSR_ENV(PT_REGS_SC(regs));
	int err;

	err = __copy_from_user(fxsave, (void __user *) buf,
			       sizeof(struct user_fxsr_struct) );
	if(err) return -EFAULT;
	else return 0;
}
#endif

int set_fpxregs(unsigned long buf, struct task_struct *tsk)
{
	return(CHOOSE_MODE(set_fpxregs_tt(buf, tsk), 0));
}

#ifdef notdef
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	fpu->cwd = (((SC_FP_CW(PT_REGS_SC(regs)) & 0xffff) << 16) |
		    (SC_FP_SW(PT_REGS_SC(regs)) & 0xffff));
	fpu->swd = SC_FP_CSSEL(PT_REGS_SC(regs)) & 0xffff;
	fpu->twd = SC_FP_IPOFF(PT_REGS_SC(regs));
	fpu->fip = SC_FP_CSSEL(PT_REGS_SC(regs)) & 0xffff;
	fpu->fcs = SC_FP_DATAOFF(PT_REGS_SC(regs));
	fpu->foo = SC_FP_DATASEL(PT_REGS_SC(regs));
	fpu->fos = 0;
	memcpy(fpu->st_space, (void *) SC_FP_ST(PT_REGS_SC(regs)),
	       sizeof(fpu->st_space));
	return(1);
}
#endif

#ifdef CONFIG_MODE_TT
static inline void copy_fpu_fxsave_tt(struct pt_regs *regs,
				      struct user_i387_struct *buf)
{
	struct i387_fxsave_struct *fpu = SC_FXSR_ENV(PT_REGS_SC(regs));
	unsigned short *to;
	unsigned short *from;
	int i;

	memcpy( buf, fpu, 7 * sizeof(long) );

	to = (unsigned short *) &buf->st_space[0];
	from = (unsigned short *) &fpu->st_space[0];
	for ( i = 0 ; i < 8 ; i++, to += 5, from += 8 ) {
		memcpy( to, from, 5 * sizeof(unsigned short) );
	}
}
#endif

static inline void copy_fpu_fxsave(struct pt_regs *regs,
				   struct user_i387_struct *buf)
{
	(void) CHOOSE_MODE(copy_fpu_fxsave_tt(regs, buf), 0);
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu )
{
	copy_fpu_fxsave(regs, (struct user_i387_struct *) fpu);
	return(1);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
