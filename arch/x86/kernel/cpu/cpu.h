#ifndef ARCH_X86_CPU_H

#define ARCH_X86_CPU_H

struct cpu_model_info {
	int vendor;
	int family;
	char *model_names[16];
};

/* attempt to consolidate cpu attributes */
struct cpu_dev {
	char	* c_vendor;

	/* some have two possibilities for cpuid string */
	char	* c_ident[2];	

	struct		cpu_model_info c_models[4];

	void            (*c_early_init)(struct cpuinfo_x86 *c);
	void		(*c_init)(struct cpuinfo_x86 * c);
	void		(*c_identify)(struct cpuinfo_x86 * c);
	unsigned int	(*c_size_cache)(struct cpuinfo_x86 * c, unsigned int size);
	int	c_x86_vendor;
};

#define cpu_dev_register(cpu_devX) \
	static struct cpu_dev *__cpu_dev_##cpu_devX __used \
	__attribute__((__section__(".x86_cpu_dev.init"))) = \
	&cpu_devX;

extern struct cpu_dev *__x86_cpu_dev_start[], *__x86_cpu_dev_end[];

extern void display_cacheinfo(struct cpuinfo_x86 *c);

#endif
