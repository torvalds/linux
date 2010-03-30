#ifndef __ASM_SH_SMP_OPS_H
#define __ASM_SH_SMP_OPS_H

struct plat_smp_ops {
	void (*smp_setup)(void);
	unsigned int (*smp_processor_id)(void);
	void (*prepare_cpus)(unsigned int max_cpus);
	void (*start_cpu)(unsigned int cpu, unsigned long entry_point);
	void (*send_ipi)(unsigned int cpu, unsigned int message);
};

extern struct plat_smp_ops shx3_smp_ops;

#ifdef CONFIG_SMP

static inline void plat_smp_setup(void)
{
	extern struct plat_smp_ops *mp_ops;	/* private */

	BUG_ON(!mp_ops);
	mp_ops->smp_setup();
}

extern void register_smp_ops(struct plat_smp_ops *ops);

#else

static inline void plat_smp_setup(void)
{
	/* UP, nothing to do ... */
}

static inline void register_smp_ops(struct plat_smp_ops *ops)
{
}

#endif /* CONFIG_SMP */

#endif /* __ASM_SH_SMP_OPS_H */
