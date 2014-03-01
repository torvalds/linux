#ifndef _ASM_X86_MICROCODE_H
#define _ASM_X86_MICROCODE_H

#define native_rdmsr(msr, val1, val2)			\
do {							\
	u64 __val = native_read_msr((msr));		\
	(void)((val1) = (u32)__val);			\
	(void)((val2) = (u32)(__val >> 32));		\
} while (0)

#define native_wrmsr(msr, low, high)			\
	native_write_msr(msr, low, high)

#define native_wrmsrl(msr, val)				\
	native_write_msr((msr),				\
			 (u32)((u64)(val)),		\
			 (u32)((u64)(val) >> 32))

struct cpu_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int rev;
};

struct device;

enum ucode_state { UCODE_ERROR, UCODE_OK, UCODE_NFOUND };

struct microcode_ops {
	enum ucode_state (*request_microcode_user) (int cpu,
				const void __user *buf, size_t size);

	enum ucode_state (*request_microcode_fw) (int cpu, struct device *,
						  bool refresh_fw);

	void (*microcode_fini_cpu) (int cpu);

	/*
	 * The generic 'microcode_core' part guarantees that
	 * the callbacks below run on a target cpu when they
	 * are being called.
	 * See also the "Synchronization" section in microcode_core.c.
	 */
	int (*apply_microcode) (int cpu);
	int (*collect_cpu_info) (int cpu, struct cpu_signature *csig);
};

struct ucode_cpu_info {
	struct cpu_signature	cpu_sig;
	int			valid;
	void			*mc;
};
extern struct ucode_cpu_info ucode_cpu_info[];

#ifdef CONFIG_MICROCODE_INTEL
extern struct microcode_ops * __init init_intel_microcode(void);
#else
static inline struct microcode_ops * __init init_intel_microcode(void)
{
	return NULL;
}
#endif /* CONFIG_MICROCODE_INTEL */

#ifdef CONFIG_MICROCODE_AMD
extern struct microcode_ops * __init init_amd_microcode(void);
extern void __exit exit_amd_microcode(void);
#else
static inline struct microcode_ops * __init init_amd_microcode(void)
{
	return NULL;
}
static inline void __exit exit_amd_microcode(void) {}
#endif

#ifdef CONFIG_MICROCODE_EARLY
#define MAX_UCODE_COUNT 128
extern void __init load_ucode_bsp(void);
extern void load_ucode_ap(void);
extern int __init save_microcode_in_initrd(void);
#else
static inline void __init load_ucode_bsp(void) {}
static inline void load_ucode_ap(void) {}
static inline int __init save_microcode_in_initrd(void)
{
	return 0;
}
#endif

#endif /* _ASM_X86_MICROCODE_H */
