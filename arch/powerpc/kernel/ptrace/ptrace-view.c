// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/nospec.h>
#include <linux/pkeys.h>

#include "ptrace-decl.h"

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define STR(s)	#s			/* convert to string */
#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define GPR_OFFSET_NAME(num)	\
	{.name = STR(r##num), .offset = offsetof(struct pt_regs, gpr[num])}, \
	{.name = STR(gpr##num), .offset = offsetof(struct pt_regs, gpr[num])}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	GPR_OFFSET_NAME(0),
	GPR_OFFSET_NAME(1),
	GPR_OFFSET_NAME(2),
	GPR_OFFSET_NAME(3),
	GPR_OFFSET_NAME(4),
	GPR_OFFSET_NAME(5),
	GPR_OFFSET_NAME(6),
	GPR_OFFSET_NAME(7),
	GPR_OFFSET_NAME(8),
	GPR_OFFSET_NAME(9),
	GPR_OFFSET_NAME(10),
	GPR_OFFSET_NAME(11),
	GPR_OFFSET_NAME(12),
	GPR_OFFSET_NAME(13),
	GPR_OFFSET_NAME(14),
	GPR_OFFSET_NAME(15),
	GPR_OFFSET_NAME(16),
	GPR_OFFSET_NAME(17),
	GPR_OFFSET_NAME(18),
	GPR_OFFSET_NAME(19),
	GPR_OFFSET_NAME(20),
	GPR_OFFSET_NAME(21),
	GPR_OFFSET_NAME(22),
	GPR_OFFSET_NAME(23),
	GPR_OFFSET_NAME(24),
	GPR_OFFSET_NAME(25),
	GPR_OFFSET_NAME(26),
	GPR_OFFSET_NAME(27),
	GPR_OFFSET_NAME(28),
	GPR_OFFSET_NAME(29),
	GPR_OFFSET_NAME(30),
	GPR_OFFSET_NAME(31),
	REG_OFFSET_NAME(nip),
	REG_OFFSET_NAME(msr),
	REG_OFFSET_NAME(ctr),
	REG_OFFSET_NAME(link),
	REG_OFFSET_NAME(xer),
	REG_OFFSET_NAME(ccr),
#ifdef CONFIG_PPC64
	REG_OFFSET_NAME(softe),
#else
	REG_OFFSET_NAME(mq),
#endif
	REG_OFFSET_NAME(trap),
	REG_OFFSET_NAME(dar),
	REG_OFFSET_NAME(dsisr),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_query_register_name() - query register name from its offset
 * @offset:	the offset of a register in struct pt_regs.
 *
 * regs_query_register_name() returns the name of a register from its
 * offset in struct pt_regs. If the @offset is invalid, this returns NULL;
 */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

static unsigned long get_user_msr(struct task_struct *task)
{
	return task->thread.regs->msr | task->thread.fpexc_mode;
}

static __always_inline int set_user_msr(struct task_struct *task, unsigned long msr)
{
	unsigned long newmsr = (task->thread.regs->msr & ~MSR_DEBUGCHANGE) |
				(msr & MSR_DEBUGCHANGE);
	regs_set_return_msr(task->thread.regs, newmsr);
	return 0;
}

#ifdef CONFIG_PPC64
static int get_user_dscr(struct task_struct *task, unsigned long *data)
{
	*data = task->thread.dscr;
	return 0;
}

static int set_user_dscr(struct task_struct *task, unsigned long dscr)
{
	task->thread.dscr = dscr;
	task->thread.dscr_inherit = 1;
	return 0;
}
#else
static int get_user_dscr(struct task_struct *task, unsigned long *data)
{
	return -EIO;
}

static int set_user_dscr(struct task_struct *task, unsigned long dscr)
{
	return -EIO;
}
#endif

/*
 * We prevent mucking around with the reserved area of trap
 * which are used internally by the kernel.
 */
static __always_inline int set_user_trap(struct task_struct *task, unsigned long trap)
{
	set_trap(task->thread.regs, trap);
	return 0;
}

/*
 * Get contents of register REGNO in task TASK.
 */
int ptrace_get_reg(struct task_struct *task, int regno, unsigned long *data)
{
	unsigned int regs_max;

	if (task->thread.regs == NULL || !data)
		return -EIO;

	if (regno == PT_MSR) {
		*data = get_user_msr(task);
		return 0;
	}

	if (regno == PT_DSCR)
		return get_user_dscr(task, data);

	/*
	 * softe copies paca->irq_soft_mask variable state. Since irq_soft_mask is
	 * no more used as a flag, lets force usr to always see the softe value as 1
	 * which means interrupts are not soft disabled.
	 */
	if (IS_ENABLED(CONFIG_PPC64) && regno == PT_SOFTE) {
		*data = 1;
		return  0;
	}

	regs_max = sizeof(struct user_pt_regs) / sizeof(unsigned long);
	if (regno < regs_max) {
		regno = array_index_nospec(regno, regs_max);
		*data = ((unsigned long *)task->thread.regs)[regno];
		return 0;
	}

	return -EIO;
}

/*
 * Write contents of register REGNO in task TASK.
 */
int ptrace_put_reg(struct task_struct *task, int regno, unsigned long data)
{
	if (task->thread.regs == NULL)
		return -EIO;

	if (regno == PT_MSR)
		return set_user_msr(task, data);
	if (regno == PT_TRAP)
		return set_user_trap(task, data);
	if (regno == PT_DSCR)
		return set_user_dscr(task, data);

	if (regno <= PT_MAX_PUT_REG) {
		regno = array_index_nospec(regno, PT_MAX_PUT_REG + 1);
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}

static int gpr_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	struct membuf to_msr = membuf_at(&to, offsetof(struct pt_regs, msr));
#ifdef CONFIG_PPC64
	struct membuf to_softe = membuf_at(&to, offsetof(struct pt_regs, softe));
#endif
	if (target->thread.regs == NULL)
		return -EIO;

	membuf_write(&to, target->thread.regs, sizeof(struct user_pt_regs));

	membuf_store(&to_msr, get_user_msr(target));
#ifdef CONFIG_PPC64
	membuf_store(&to_softe, 0x1ul);
#endif
	return membuf_zero(&to, ELF_NGREG * sizeof(unsigned long) -
				 sizeof(struct user_pt_regs));
}

static int gpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	unsigned long reg;
	int ret;

	if (target->thread.regs == NULL)
		return -EIO;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 target->thread.regs,
				 0, PT_MSR * sizeof(reg));

	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &reg,
					 PT_MSR * sizeof(reg),
					 (PT_MSR + 1) * sizeof(reg));
		if (!ret)
			ret = set_user_msr(target, reg);
	}

	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct pt_regs, msr) + sizeof(long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.regs->orig_gpr3,
					 PT_ORIG_R3 * sizeof(reg),
					 (PT_MAX_PUT_REG + 1) * sizeof(reg));

	if (PT_MAX_PUT_REG + 1 < PT_TRAP && !ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  (PT_MAX_PUT_REG + 1) * sizeof(reg),
					  PT_TRAP * sizeof(reg));

	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &reg,
					 PT_TRAP * sizeof(reg),
					 (PT_TRAP + 1) * sizeof(reg));
		if (!ret)
			ret = set_user_trap(target, reg);
	}

	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  (PT_TRAP + 1) * sizeof(reg), -1);

	return ret;
}

#ifdef CONFIG_PPC64
static int ppr_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	if (!target->thread.regs)
		return -EINVAL;

	return membuf_write(&to, &target->thread.regs->ppr, sizeof(u64));
}

static int ppr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	if (!target->thread.regs)
		return -EINVAL;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.regs->ppr, 0, sizeof(u64));
}

static int dscr_get(struct task_struct *target, const struct user_regset *regset,
		    struct membuf to)
{
	return membuf_write(&to, &target->thread.dscr, sizeof(u64));
}
static int dscr_set(struct task_struct *target, const struct user_regset *regset,
		    unsigned int pos, unsigned int count, const void *kbuf,
		    const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.dscr, 0, sizeof(u64));
}
#endif
#ifdef CONFIG_PPC_BOOK3S_64
static int tar_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	return membuf_write(&to, &target->thread.tar, sizeof(u64));
}
static int tar_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.tar, 0, sizeof(u64));
}

static int ebb_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (target->thread.used_ebb)
		return regset->n;

	return 0;
}

static int ebb_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	/* Build tests */
	BUILD_BUG_ON(TSO(ebbrr) + sizeof(unsigned long) != TSO(ebbhr));
	BUILD_BUG_ON(TSO(ebbhr) + sizeof(unsigned long) != TSO(bescr));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (!target->thread.used_ebb)
		return -ENODATA;

	return membuf_write(&to, &target->thread.ebbrr, 3 * sizeof(unsigned long));
}

static int ebb_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	int ret = 0;

	/* Build tests */
	BUILD_BUG_ON(TSO(ebbrr) + sizeof(unsigned long) != TSO(ebbhr));
	BUILD_BUG_ON(TSO(ebbhr) + sizeof(unsigned long) != TSO(bescr));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	if (target->thread.used_ebb)
		return -ENODATA;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &target->thread.ebbrr,
				 0, sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.ebbhr, sizeof(unsigned long),
					 2 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.bescr, 2 * sizeof(unsigned long),
					 3 * sizeof(unsigned long));

	return ret;
}
static int pmu_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return regset->n;
}

static int pmu_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	/* Build tests */
	BUILD_BUG_ON(TSO(siar) + sizeof(unsigned long) != TSO(sdar));
	BUILD_BUG_ON(TSO(sdar) + sizeof(unsigned long) != TSO(sier));
	BUILD_BUG_ON(TSO(sier) + sizeof(unsigned long) != TSO(mmcr2));
	BUILD_BUG_ON(TSO(mmcr2) + sizeof(unsigned long) != TSO(mmcr0));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	return membuf_write(&to, &target->thread.siar, 5 * sizeof(unsigned long));
}

static int pmu_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count, const void *kbuf,
		   const void __user *ubuf)
{
	int ret = 0;

	/* Build tests */
	BUILD_BUG_ON(TSO(siar) + sizeof(unsigned long) != TSO(sdar));
	BUILD_BUG_ON(TSO(sdar) + sizeof(unsigned long) != TSO(sier));
	BUILD_BUG_ON(TSO(sier) + sizeof(unsigned long) != TSO(mmcr2));
	BUILD_BUG_ON(TSO(mmcr2) + sizeof(unsigned long) != TSO(mmcr0));

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return -ENODEV;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &target->thread.siar,
				 0, sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.sdar, sizeof(unsigned long),
					 2 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.sier, 2 * sizeof(unsigned long),
					 3 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.mmcr2, 3 * sizeof(unsigned long),
					 4 * sizeof(unsigned long));

	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.mmcr0, 4 * sizeof(unsigned long),
					 5 * sizeof(unsigned long));
	return ret;
}
#endif

#ifdef CONFIG_PPC_MEM_KEYS
static int pkey_active(struct task_struct *target, const struct user_regset *regset)
{
	if (!arch_pkeys_enabled())
		return -ENODEV;

	return regset->n;
}

static int pkey_get(struct task_struct *target, const struct user_regset *regset,
		    struct membuf to)
{

	if (!arch_pkeys_enabled())
		return -ENODEV;

	membuf_store(&to, target->thread.regs->amr);
	membuf_store(&to, target->thread.regs->iamr);
	return membuf_store(&to, default_uamor);
}

static int pkey_set(struct task_struct *target, const struct user_regset *regset,
		    unsigned int pos, unsigned int count, const void *kbuf,
		    const void __user *ubuf)
{
	u64 new_amr;
	int ret;

	if (!arch_pkeys_enabled())
		return -ENODEV;

	/* Only the AMR can be set from userspace */
	if (pos != 0 || count != sizeof(new_amr))
		return -EINVAL;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &new_amr, 0, sizeof(new_amr));
	if (ret)
		return ret;

	/*
	 * UAMOR determines which bits of the AMR can be set from userspace.
	 * UAMOR value 0b11 indicates that the AMR value can be modified
	 * from userspace. If the kernel is using a specific key, we avoid
	 * userspace modifying the AMR value for that key by masking them
	 * via UAMOR 0b00.
	 *
	 * Pick the AMR values for the keys that kernel is using. This
	 * will be indicated by the ~default_uamor bits.
	 */
	target->thread.regs->amr = (new_amr & default_uamor) |
		(target->thread.regs->amr & ~default_uamor);

	return 0;
}
#endif /* CONFIG_PPC_MEM_KEYS */

static const struct user_regset native_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.regset_get = gpr_get, .set = gpr_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.regset_get = fpr_get, .set = fpr_set
	},
#ifdef CONFIG_ALTIVEC
	[REGSET_VMX] = {
		.core_note_type = NT_PPC_VMX, .n = 34,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = vr_active, .regset_get = vr_get, .set = vr_set
	},
#endif
#ifdef CONFIG_VSX
	[REGSET_VSX] = {
		.core_note_type = NT_PPC_VSX, .n = 32,
		.size = sizeof(double), .align = sizeof(double),
		.active = vsr_active, .regset_get = vsr_get, .set = vsr_set
	},
#endif
#ifdef CONFIG_SPE
	[REGSET_SPE] = {
		.core_note_type = NT_PPC_SPE, .n = 35,
		.size = sizeof(u32), .align = sizeof(u32),
		.active = evr_active, .regset_get = evr_get, .set = evr_set
	},
#endif
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	[REGSET_TM_CGPR] = {
		.core_note_type = NT_PPC_TM_CGPR, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.active = tm_cgpr_active, .regset_get = tm_cgpr_get, .set = tm_cgpr_set
	},
	[REGSET_TM_CFPR] = {
		.core_note_type = NT_PPC_TM_CFPR, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cfpr_active, .regset_get = tm_cfpr_get, .set = tm_cfpr_set
	},
	[REGSET_TM_CVMX] = {
		.core_note_type = NT_PPC_TM_CVMX, .n = ELF_NVMX,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = tm_cvmx_active, .regset_get = tm_cvmx_get, .set = tm_cvmx_set
	},
	[REGSET_TM_CVSX] = {
		.core_note_type = NT_PPC_TM_CVSX, .n = ELF_NVSX,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cvsx_active, .regset_get = tm_cvsx_get, .set = tm_cvsx_set
	},
	[REGSET_TM_SPR] = {
		.core_note_type = NT_PPC_TM_SPR, .n = ELF_NTMSPRREG,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_spr_active, .regset_get = tm_spr_get, .set = tm_spr_set
	},
	[REGSET_TM_CTAR] = {
		.core_note_type = NT_PPC_TM_CTAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_tar_active, .regset_get = tm_tar_get, .set = tm_tar_set
	},
	[REGSET_TM_CPPR] = {
		.core_note_type = NT_PPC_TM_CPPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_ppr_active, .regset_get = tm_ppr_get, .set = tm_ppr_set
	},
	[REGSET_TM_CDSCR] = {
		.core_note_type = NT_PPC_TM_CDSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_dscr_active, .regset_get = tm_dscr_get, .set = tm_dscr_set
	},
#endif
#ifdef CONFIG_PPC64
	[REGSET_PPR] = {
		.core_note_type = NT_PPC_PPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = ppr_get, .set = ppr_set
	},
	[REGSET_DSCR] = {
		.core_note_type = NT_PPC_DSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = dscr_get, .set = dscr_set
	},
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	[REGSET_TAR] = {
		.core_note_type = NT_PPC_TAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = tar_get, .set = tar_set
	},
	[REGSET_EBB] = {
		.core_note_type = NT_PPC_EBB, .n = ELF_NEBB,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = ebb_active, .regset_get = ebb_get, .set = ebb_set
	},
	[REGSET_PMR] = {
		.core_note_type = NT_PPC_PMU, .n = ELF_NPMU,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = pmu_active, .regset_get = pmu_get, .set = pmu_set
	},
#endif
#ifdef CONFIG_PPC_MEM_KEYS
	[REGSET_PKEY] = {
		.core_note_type = NT_PPC_PKEY, .n = ELF_NPKEY,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = pkey_active, .regset_get = pkey_get, .set = pkey_set
	},
#endif
};

const struct user_regset_view user_ppc_native_view = {
	.name = UTS_MACHINE, .e_machine = ELF_ARCH, .ei_osabi = ELF_OSABI,
	.regsets = native_regsets, .n = ARRAY_SIZE(native_regsets)
};

#include <linux/compat.h>

int gpr32_get_common(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to, unsigned long *regs)
{
	int i;

	for (i = 0; i < PT_MSR; i++)
		membuf_store(&to, (u32)regs[i]);
	membuf_store(&to, (u32)get_user_msr(target));
	for (i++ ; i < PT_REGS_COUNT; i++)
		membuf_store(&to, (u32)regs[i]);
	return membuf_zero(&to, (ELF_NGREG - PT_REGS_COUNT) * sizeof(u32));
}

int gpr32_set_common(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf,
		     unsigned long *regs)
{
	const compat_ulong_t *k = kbuf;
	const compat_ulong_t __user *u = ubuf;
	compat_ulong_t reg;

	if (!kbuf && !user_read_access_begin(u, count))
		return -EFAULT;

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf)
		for (; count > 0 && pos < PT_MSR; --count)
			regs[pos++] = *k++;
	else
		for (; count > 0 && pos < PT_MSR; --count) {
			unsafe_get_user(reg, u++, Efault);
			regs[pos++] = reg;
		}


	if (count > 0 && pos == PT_MSR) {
		if (kbuf)
			reg = *k++;
		else
			unsafe_get_user(reg, u++, Efault);
		set_user_msr(target, reg);
		++pos;
		--count;
	}

	if (kbuf) {
		for (; count > 0 && pos <= PT_MAX_PUT_REG; --count)
			regs[pos++] = *k++;
		for (; count > 0 && pos < PT_TRAP; --count, ++pos)
			++k;
	} else {
		for (; count > 0 && pos <= PT_MAX_PUT_REG; --count) {
			unsafe_get_user(reg, u++, Efault);
			regs[pos++] = reg;
		}
		for (; count > 0 && pos < PT_TRAP; --count, ++pos)
			unsafe_get_user(reg, u++, Efault);
	}

	if (count > 0 && pos == PT_TRAP) {
		if (kbuf)
			reg = *k++;
		else
			unsafe_get_user(reg, u++, Efault);
		set_user_trap(target, reg);
		++pos;
		--count;
	}
	if (!kbuf)
		user_read_access_end();

	kbuf = k;
	ubuf = u;
	pos *= sizeof(reg);
	count *= sizeof(reg);
	user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
				  (PT_TRAP + 1) * sizeof(reg), -1);
	return 0;

Efault:
	user_read_access_end();
	return -EFAULT;
}

static int gpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to)
{
	if (target->thread.regs == NULL)
		return -EIO;

	return gpr32_get_common(target, regset, to,
			&target->thread.regs->gpr[0]);
}

static int gpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	if (target->thread.regs == NULL)
		return -EIO;

	return gpr32_set_common(target, regset, pos, count, kbuf, ubuf,
			&target->thread.regs->gpr[0]);
}

/*
 * These are the regset flavors matching the CONFIG_PPC32 native set.
 */
static const struct user_regset compat_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS, .n = ELF_NGREG,
		.size = sizeof(compat_long_t), .align = sizeof(compat_long_t),
		.regset_get = gpr32_get, .set = gpr32_set
	},
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.regset_get = fpr_get, .set = fpr_set
	},
#ifdef CONFIG_ALTIVEC
	[REGSET_VMX] = {
		.core_note_type = NT_PPC_VMX, .n = 34,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = vr_active, .regset_get = vr_get, .set = vr_set
	},
#endif
#ifdef CONFIG_SPE
	[REGSET_SPE] = {
		.core_note_type = NT_PPC_SPE, .n = 35,
		.size = sizeof(u32), .align = sizeof(u32),
		.active = evr_active, .regset_get = evr_get, .set = evr_set
	},
#endif
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	[REGSET_TM_CGPR] = {
		.core_note_type = NT_PPC_TM_CGPR, .n = ELF_NGREG,
		.size = sizeof(long), .align = sizeof(long),
		.active = tm_cgpr_active,
		.regset_get = tm_cgpr32_get, .set = tm_cgpr32_set
	},
	[REGSET_TM_CFPR] = {
		.core_note_type = NT_PPC_TM_CFPR, .n = ELF_NFPREG,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cfpr_active, .regset_get = tm_cfpr_get, .set = tm_cfpr_set
	},
	[REGSET_TM_CVMX] = {
		.core_note_type = NT_PPC_TM_CVMX, .n = ELF_NVMX,
		.size = sizeof(vector128), .align = sizeof(vector128),
		.active = tm_cvmx_active, .regset_get = tm_cvmx_get, .set = tm_cvmx_set
	},
	[REGSET_TM_CVSX] = {
		.core_note_type = NT_PPC_TM_CVSX, .n = ELF_NVSX,
		.size = sizeof(double), .align = sizeof(double),
		.active = tm_cvsx_active, .regset_get = tm_cvsx_get, .set = tm_cvsx_set
	},
	[REGSET_TM_SPR] = {
		.core_note_type = NT_PPC_TM_SPR, .n = ELF_NTMSPRREG,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_spr_active, .regset_get = tm_spr_get, .set = tm_spr_set
	},
	[REGSET_TM_CTAR] = {
		.core_note_type = NT_PPC_TM_CTAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_tar_active, .regset_get = tm_tar_get, .set = tm_tar_set
	},
	[REGSET_TM_CPPR] = {
		.core_note_type = NT_PPC_TM_CPPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_ppr_active, .regset_get = tm_ppr_get, .set = tm_ppr_set
	},
	[REGSET_TM_CDSCR] = {
		.core_note_type = NT_PPC_TM_CDSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = tm_dscr_active, .regset_get = tm_dscr_get, .set = tm_dscr_set
	},
#endif
#ifdef CONFIG_PPC64
	[REGSET_PPR] = {
		.core_note_type = NT_PPC_PPR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = ppr_get, .set = ppr_set
	},
	[REGSET_DSCR] = {
		.core_note_type = NT_PPC_DSCR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = dscr_get, .set = dscr_set
	},
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	[REGSET_TAR] = {
		.core_note_type = NT_PPC_TAR, .n = 1,
		.size = sizeof(u64), .align = sizeof(u64),
		.regset_get = tar_get, .set = tar_set
	},
	[REGSET_EBB] = {
		.core_note_type = NT_PPC_EBB, .n = ELF_NEBB,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = ebb_active, .regset_get = ebb_get, .set = ebb_set
	},
#endif
};

static const struct user_regset_view user_ppc_compat_view = {
	.name = "ppc", .e_machine = EM_PPC, .ei_osabi = ELF_OSABI,
	.regsets = compat_regsets, .n = ARRAY_SIZE(compat_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	if (IS_ENABLED(CONFIG_COMPAT) && is_tsk_32bit_task(task))
		return &user_ppc_compat_view;
	return &user_ppc_native_view;
}
