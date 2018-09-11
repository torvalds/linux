/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002 Ralf Baechle
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#ifndef __ASM_SMP_OPS_H
#define __ASM_SMP_OPS_H

#include <linux/errno.h>

#include <asm/mips-cps.h>

#ifdef CONFIG_SMP

#include <linux/cpumask.h>

struct task_struct;

struct plat_smp_ops {
	void (*send_ipi_single)(int cpu, unsigned int action);
	void (*send_ipi_mask)(const struct cpumask *mask, unsigned int action);
	void (*init_secondary)(void);
	void (*smp_finish)(void);
	int (*boot_secondary)(int cpu, struct task_struct *idle);
	void (*smp_setup)(void);
	void (*prepare_cpus)(unsigned int max_cpus);
#ifdef CONFIG_HOTPLUG_CPU
	int (*cpu_disable)(void);
	void (*cpu_die)(unsigned int cpu);
#endif
#ifdef CONFIG_KEXEC
	void (*kexec_nonboot_cpu)(void);
#endif
};

extern void register_smp_ops(const struct plat_smp_ops *ops);

static inline void plat_smp_setup(void)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->smp_setup();
}

extern void mips_smp_send_ipi_single(int cpu, unsigned int action);
extern void mips_smp_send_ipi_mask(const struct cpumask *mask,
				      unsigned int action);

#else /* !CONFIG_SMP */

struct plat_smp_ops;

static inline void plat_smp_setup(void)
{
	/* UP, nothing to do ...  */
}

static inline void register_smp_ops(const struct plat_smp_ops *ops)
{
}

#endif /* !CONFIG_SMP */

static inline int register_up_smp_ops(void)
{
#ifdef CONFIG_SMP_UP
	extern const struct plat_smp_ops up_smp_ops;

	register_smp_ops(&up_smp_ops);

	return 0;
#else
	return -ENODEV;
#endif
}

static inline int register_cmp_smp_ops(void)
{
#ifdef CONFIG_MIPS_CMP
	extern const struct plat_smp_ops cmp_smp_ops;

	if (!mips_cm_present())
		return -ENODEV;

	register_smp_ops(&cmp_smp_ops);

	return 0;
#else
	return -ENODEV;
#endif
}

static inline int register_vsmp_smp_ops(void)
{
#ifdef CONFIG_MIPS_MT_SMP
	extern const struct plat_smp_ops vsmp_smp_ops;

	register_smp_ops(&vsmp_smp_ops);

	return 0;
#else
	return -ENODEV;
#endif
}

#ifdef CONFIG_MIPS_CPS
extern int register_cps_smp_ops(void);
#else
static inline int register_cps_smp_ops(void)
{
	return -ENODEV;
}
#endif

#endif /* __ASM_SMP_OPS_H */
