
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

	void		(*c_init)(struct cpuinfo_x86 * c);
	void		(*c_identify)(struct cpuinfo_x86 * c);
	unsigned int	(*c_size_cache)(struct cpuinfo_x86 * c, unsigned int size);
};

extern struct cpu_dev * cpu_devs [X86_VENDOR_NUM];

extern int get_model_name(struct cpuinfo_x86 *c);
extern void display_cacheinfo(struct cpuinfo_x86 *c);

extern void early_init_intel(struct cpuinfo_x86 *c);
extern void early_init_amd(struct cpuinfo_x86 *c);

/* Specific CPU type init functions */
int intel_cpu_init(void);
int amd_init_cpu(void);
int cyrix_init_cpu(void);
int nsc_init_cpu(void);
int centaur_init_cpu(void);
int transmeta_init_cpu(void);
int nexgen_init_cpu(void);
int umc_init_cpu(void);
