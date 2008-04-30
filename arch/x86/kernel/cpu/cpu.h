
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
};

extern struct cpu_dev * cpu_devs [X86_VENDOR_NUM];

struct cpu_vendor_dev {
	int vendor;
	struct cpu_dev *cpu_dev;
};

#define cpu_vendor_dev_register(cpu_vendor_id, cpu_dev) \
	static struct cpu_vendor_dev __cpu_vendor_dev_##cpu_vendor_id __used \
	__attribute__((__section__(".x86cpuvendor.init"))) = \
	{ cpu_vendor_id, cpu_dev }

extern struct cpu_vendor_dev __x86cpuvendor_start[], __x86cpuvendor_end[];

extern int get_model_name(struct cpuinfo_x86 *c);
extern void display_cacheinfo(struct cpuinfo_x86 *c);
