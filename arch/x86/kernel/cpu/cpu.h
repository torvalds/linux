#ifndef ARCH_X86_CPU_H

#define ARCH_X86_CPU_H

struct cpu_model_info {
	int		vendor;
	int		family;
	const char	*model_names[16];
};

/* attempt to consolidate cpu attributes */
struct cpu_dev {
	const char	*c_vendor;

	/* some have two possibilities for cpuid string */
	const char	*c_ident[2];

	struct		cpu_model_info c_models[4];

	void            (*c_early_init)(struct cpuinfo_x86 *);
	void		(*c_init)(struct cpuinfo_x86 *);
	void		(*c_identify)(struct cpuinfo_x86 *);
	unsigned int	(*c_size_cache)(struct cpuinfo_x86 *, unsigned int);
	int		c_x86_vendor;
};

#define cpu_dev_register(cpu_devX) \
	static const struct cpu_dev *const __cpu_dev_##cpu_devX __used \
	__attribute__((__section__(".x86_cpu_dev.init"))) = \
	&cpu_devX;

extern const struct cpu_dev *const __x86_cpu_dev_start[],
			    *const __x86_cpu_dev_end[];

extern void cpu_detect_cache_sizes(struct cpuinfo_x86 *c);

#endif
