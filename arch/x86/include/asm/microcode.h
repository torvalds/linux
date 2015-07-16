#ifndef _ASM_X86_MICROCODE_H
#define _ASM_X86_MICROCODE_H

#include <linux/earlycpio.h>

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
extern bool dis_ucode_ldr;

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

#define QCHAR(a, b, c, d) ((a) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define CPUID_INTEL1 QCHAR('G', 'e', 'n', 'u')
#define CPUID_INTEL2 QCHAR('i', 'n', 'e', 'I')
#define CPUID_INTEL3 QCHAR('n', 't', 'e', 'l')
#define CPUID_AMD1 QCHAR('A', 'u', 't', 'h')
#define CPUID_AMD2 QCHAR('e', 'n', 't', 'i')
#define CPUID_AMD3 QCHAR('c', 'A', 'M', 'D')

#define CPUID_IS(a, b, c, ebx, ecx, edx)	\
		(!((ebx ^ (a))|(edx ^ (b))|(ecx ^ (c))))

/*
 * In early loading microcode phase on BSP, boot_cpu_data is not set up yet.
 * x86_vendor() gets vendor id for BSP.
 *
 * In 32 bit AP case, accessing boot_cpu_data needs linear address. To simplify
 * coding, we still use x86_vendor() to get vendor id for AP.
 *
 * x86_vendor() gets vendor information directly from CPUID.
 */
static inline int x86_vendor(void)
{
	u32 eax = 0x00000000;
	u32 ebx, ecx = 0, edx;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	if (CPUID_IS(CPUID_INTEL1, CPUID_INTEL2, CPUID_INTEL3, ebx, ecx, edx))
		return X86_VENDOR_INTEL;

	if (CPUID_IS(CPUID_AMD1, CPUID_AMD2, CPUID_AMD3, ebx, ecx, edx))
		return X86_VENDOR_AMD;

	return X86_VENDOR_UNKNOWN;
}

static inline unsigned int __x86_family(unsigned int sig)
{
	unsigned int x86;

	x86 = (sig >> 8) & 0xf;

	if (x86 == 0xf)
		x86 += (sig >> 20) & 0xff;

	return x86;
}

static inline unsigned int x86_family(void)
{
	u32 eax = 0x00000001;
	u32 ebx, ecx = 0, edx;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	return __x86_family(eax);
}

static inline unsigned int x86_model(unsigned int sig)
{
	unsigned int x86, model;

	x86 = __x86_family(sig);

	model = (sig >> 4) & 0xf;

	if (x86 == 0x6 || x86 == 0xf)
		model += ((sig >> 16) & 0xf) << 4;

	return model;
}

extern void __init load_ucode_bsp(void);
extern void load_ucode_ap(void);
extern int __init save_microcode_in_initrd(void);
void reload_early_microcode(void);
extern bool get_builtin_firmware(struct cpio_data *cd, const char *name);
#else
static inline void __init load_ucode_bsp(void) {}
static inline void load_ucode_ap(void) {}
static inline int __init save_microcode_in_initrd(void)
{
	return 0;
}
static inline void reload_early_microcode(void) {}
static inline bool get_builtin_firmware(struct cpio_data *cd, const char *name)
{
	return false;
}
#endif
#endif /* _ASM_X86_MICROCODE_H */
