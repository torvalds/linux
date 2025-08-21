// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <asm/user32.h>
#include <asm/sigcontext.h>

#ifdef CONFIG_X86_32
/*
 * FPU tag word conversions.
 */

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

static inline unsigned long
twd_fxsr_to_i387(const struct user_fxsr_struct *fxsave)
{
	struct _fpxreg *st = NULL;
	unsigned long twd = (unsigned long) fxsave->twd;
	unsigned long tag;
	unsigned long ret = 0xffff0000;
	int i;

#define FPREG_ADDR(f, n)	((char *)&(f)->st_space + (n) * 16)

	for (i = 0; i < 8; i++) {
		if (twd & 0x1) {
			st = (struct _fpxreg *) FPREG_ADDR(fxsave, i);

			switch (st->exponent & 0x7fff) {
			case 0x7fff:
				tag = 2;		/* Special */
				break;
			case 0x0000:
				if (!st->significand[0] &&
				    !st->significand[1] &&
				    !st->significand[2] &&
				    !st->significand[3]) {
					tag = 1;	/* Zero */
				} else {
					tag = 2;	/* Special */
				}
				break;
			default:
				if (st->significand[3] & 0x8000)
					tag = 0;	/* Valid */
				else
					tag = 2;	/* Special */
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
 * Get/set the old 32bit i387 registers (pre-FPX)
 *
 * We provide simple wrappers for mcontext.c, they are only defined locally
 * because mcontext.c is userspace facing and needs to a different definition
 * of the structures.
 */
static int _um_i387_from_fxsr(struct membuf to,
			      const struct user_fxsr_struct *fxsave)
{
	int i;

	membuf_store(&to, (unsigned long)fxsave->cwd | 0xffff0000ul);
	membuf_store(&to, (unsigned long)fxsave->swd | 0xffff0000ul);
	membuf_store(&to, twd_fxsr_to_i387(fxsave));
	membuf_store(&to, fxsave->fip);
	membuf_store(&to, fxsave->fcs | ((unsigned long)fxsave->fop << 16));
	membuf_store(&to, fxsave->foo);
	membuf_store(&to, fxsave->fos);

	for (i = 0; i < 8; i++)
		membuf_write(&to, (void *)fxsave->st_space + i * 16, 10);

	return 0;
}

int um_i387_from_fxsr(struct user_i387_struct *i387,
		      const struct user_fxsr_struct *fxsave);

int um_i387_from_fxsr(struct user_i387_struct *i387,
		      const struct user_fxsr_struct *fxsave)
{
	struct membuf to = {
		.p = i387,
		.left = sizeof(*i387),
	};

	return _um_i387_from_fxsr(to, fxsave);
}

static int fpregs_legacy_get(struct task_struct *target,
			     const struct user_regset *regset,
			     struct membuf to)
{
	struct user_fxsr_struct *fxsave = (void *)target->thread.regs.regs.fp;

	return _um_i387_from_fxsr(to, fxsave);
}

int um_fxsr_from_i387(struct user_fxsr_struct *fxsave,
		      const struct user_i387_struct *from);

int um_fxsr_from_i387(struct user_fxsr_struct *fxsave,
		      const struct user_i387_struct *from)
{
	int i;

	fxsave->cwd = (unsigned short)(from->cwd & 0xffff);
	fxsave->swd = (unsigned short)(from->swd & 0xffff);
	fxsave->twd = twd_i387_to_fxsr((unsigned short)(from->twd & 0xffff));
	fxsave->fip = from->fip;
	fxsave->fop = (unsigned short)((from->fcs & 0xffff0000ul) >> 16);
	fxsave->fcs = (from->fcs & 0xffff);
	fxsave->foo = from->foo;
	fxsave->fos = from->fos;

	for (i = 0; i < 8; i++) {
		memcpy((void *)fxsave->st_space + i * 16,
		       (void *)from->st_space + i * 10, 10);
	}

	return 0;
}

static int fpregs_legacy_set(struct task_struct *target,
			     const struct user_regset *regset,
			     unsigned int pos, unsigned int count,
			     const void *kbuf, const void __user *ubuf)
{
	struct user_fxsr_struct *fxsave = (void *)target->thread.regs.regs.fp;
	const struct user_i387_struct *from;
	struct user_i387_struct buf;

	if (ubuf) {
		if (copy_from_user(&buf, ubuf, sizeof(buf)))
			return -EFAULT;
		from = &buf;
	} else {
		from = kbuf;
	}

	return um_fxsr_from_i387(fxsave, from);
}
#endif

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	int reg;

	for (reg = 0; to.left; reg++)
		membuf_store(&to, getreg(target, reg * sizeof(unsigned long)));
	return 0;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret = 0;

	if (kbuf) {
		const unsigned long *k = kbuf;

		while (count >= sizeof(*k) && !ret) {
			ret = putreg(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const unsigned long  __user *u = ubuf;

		while (count >= sizeof(*u) && !ret) {
			unsigned long word;

			ret = __get_user(word, u++);
			if (ret)
				break;
			ret = putreg(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return ret;
}

static int generic_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return regset->n;
}

static int generic_fpregs_get(struct task_struct *target,
			      const struct user_regset *regset,
			      struct membuf to)
{
	void *fpregs = task_pt_regs(target)->regs.fp;

	membuf_write(&to, fpregs, regset->size * regset->n);
	return 0;
}

static int generic_fpregs_set(struct task_struct *target,
			      const struct user_regset *regset,
			      unsigned int pos, unsigned int count,
			      const void *kbuf, const void __user *ubuf)
{
	void *fpregs = task_pt_regs(target)->regs.fp;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  fpregs, 0, regset->size * regset->n);
}

static struct user_regset uml_regsets[] __ro_after_init = {
	[REGSET_GENERAL] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n		= sizeof(struct user_regs_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.regset_get	= genregs_get,
		.set		= genregs_set
	},
#ifdef CONFIG_X86_32
	/* Old FP registers, they are needed in signal frames */
	[REGSET_FP_LEGACY] = {
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n		= sizeof(struct user_i387_ia32_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.active		= generic_fpregs_active,
		.regset_get	= fpregs_legacy_get,
		.set		= fpregs_legacy_set,
	},
#endif
	[REGSET_FP] = {
#ifdef CONFIG_X86_32
		USER_REGSET_NOTE_TYPE(PRXFPREG),
		.n		= sizeof(struct user32_fxsr_struct) / sizeof(long),
#else
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n		= sizeof(struct user_i387_struct) / sizeof(long),
#endif
		.size		= sizeof(long),
		.align		= sizeof(long),
		.active		= generic_fpregs_active,
		.regset_get	= generic_fpregs_get,
		.set		= generic_fpregs_set,
	},
	[REGSET_XSTATE] = {
		USER_REGSET_NOTE_TYPE(X86_XSTATE),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.active		= generic_fpregs_active,
		.regset_get	= generic_fpregs_get,
		.set		= generic_fpregs_set,
	},
	/* TODO: Add TLS regset for 32bit */
};

static const struct user_regset_view user_uml_view = {
#ifdef CONFIG_X86_32
	.name = "i386", .e_machine = EM_386,
#else
	.name = "x86_64", .e_machine = EM_X86_64,
#endif
	.regsets = uml_regsets, .n = ARRAY_SIZE(uml_regsets)
};

const struct user_regset_view *
task_user_regset_view(struct task_struct *tsk)
{
	return &user_uml_view;
}

static int __init init_regset_xstate_info(void)
{
	uml_regsets[REGSET_XSTATE].n =
		host_fp_size / uml_regsets[REGSET_XSTATE].size;

	return 0;
}
arch_initcall(init_regset_xstate_info);
