#ifndef _ASM_X86_CPUFEATURE_H
#define _ASM_X86_CPUFEATURE_H

#include <asm/processor.h>

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

#include <asm/asm.h>
#include <linux/bitops.h>

enum cpuid_leafs
{
	CPUID_1_EDX		= 0,
	CPUID_8000_0001_EDX,
	CPUID_8086_0001_EDX,
	CPUID_LNX_1,
	CPUID_1_ECX,
	CPUID_C000_0001_EDX,
	CPUID_8000_0001_ECX,
	CPUID_LNX_2,
	CPUID_LNX_3,
	CPUID_7_0_EBX,
	CPUID_D_1_EAX,
	CPUID_F_0_EDX,
	CPUID_F_1_EDX,
	CPUID_8000_0008_EBX,
	CPUID_6_EAX,
	CPUID_8000_000A_EDX,
	CPUID_7_ECX,
};

#ifdef CONFIG_X86_FEATURE_NAMES
extern const char * const x86_cap_flags[NCAPINTS*32];
extern const char * const x86_power_flags[32];
#define X86_CAP_FMT "%s"
#define x86_cap_flag(flag) x86_cap_flags[flag]
#else
#define X86_CAP_FMT "%d:%d"
#define x86_cap_flag(flag) ((flag) >> 5), ((flag) & 31)
#endif

/*
 * In order to save room, we index into this array by doing
 * X86_BUG_<name> - NCAPINTS*32.
 */
extern const char * const x86_bug_flags[NBUGINTS*32];

#define test_cpu_cap(c, bit)						\
	 test_bit(bit, (unsigned long *)((c)->x86_capability))

#define REQUIRED_MASK_BIT_SET(bit)					\
	 ( (((bit)>>5)==0  && (1UL<<((bit)&31) & REQUIRED_MASK0 )) ||	\
	   (((bit)>>5)==1  && (1UL<<((bit)&31) & REQUIRED_MASK1 )) ||	\
	   (((bit)>>5)==2  && (1UL<<((bit)&31) & REQUIRED_MASK2 )) ||	\
	   (((bit)>>5)==3  && (1UL<<((bit)&31) & REQUIRED_MASK3 )) ||	\
	   (((bit)>>5)==4  && (1UL<<((bit)&31) & REQUIRED_MASK4 )) ||	\
	   (((bit)>>5)==5  && (1UL<<((bit)&31) & REQUIRED_MASK5 )) ||	\
	   (((bit)>>5)==6  && (1UL<<((bit)&31) & REQUIRED_MASK6 )) ||	\
	   (((bit)>>5)==7  && (1UL<<((bit)&31) & REQUIRED_MASK7 )) ||	\
	   (((bit)>>5)==8  && (1UL<<((bit)&31) & REQUIRED_MASK8 )) ||	\
	   (((bit)>>5)==9  && (1UL<<((bit)&31) & REQUIRED_MASK9 )) ||	\
	   (((bit)>>5)==10 && (1UL<<((bit)&31) & REQUIRED_MASK10)) ||	\
	   (((bit)>>5)==11 && (1UL<<((bit)&31) & REQUIRED_MASK11)) ||	\
	   (((bit)>>5)==12 && (1UL<<((bit)&31) & REQUIRED_MASK12)) ||	\
	   (((bit)>>5)==13 && (1UL<<((bit)&31) & REQUIRED_MASK13)) ||	\
	   (((bit)>>5)==14 && (1UL<<((bit)&31) & REQUIRED_MASK14)) ||	\
	   (((bit)>>5)==15 && (1UL<<((bit)&31) & REQUIRED_MASK15)) ||	\
	   (((bit)>>5)==16 && (1UL<<((bit)&31) & REQUIRED_MASK16)) )

#define DISABLED_MASK_BIT_SET(bit)					\
	 ( (((bit)>>5)==0  && (1UL<<((bit)&31) & DISABLED_MASK0 )) ||	\
	   (((bit)>>5)==1  && (1UL<<((bit)&31) & DISABLED_MASK1 )) ||	\
	   (((bit)>>5)==2  && (1UL<<((bit)&31) & DISABLED_MASK2 )) ||	\
	   (((bit)>>5)==3  && (1UL<<((bit)&31) & DISABLED_MASK3 )) ||	\
	   (((bit)>>5)==4  && (1UL<<((bit)&31) & DISABLED_MASK4 )) ||	\
	   (((bit)>>5)==5  && (1UL<<((bit)&31) & DISABLED_MASK5 )) ||	\
	   (((bit)>>5)==6  && (1UL<<((bit)&31) & DISABLED_MASK6 )) ||	\
	   (((bit)>>5)==7  && (1UL<<((bit)&31) & DISABLED_MASK7 )) ||	\
	   (((bit)>>5)==8  && (1UL<<((bit)&31) & DISABLED_MASK8 )) ||	\
	   (((bit)>>5)==9  && (1UL<<((bit)&31) & DISABLED_MASK9 )) ||	\
	   (((bit)>>5)==10 && (1UL<<((bit)&31) & DISABLED_MASK10)) ||	\
	   (((bit)>>5)==11 && (1UL<<((bit)&31) & DISABLED_MASK11)) ||	\
	   (((bit)>>5)==12 && (1UL<<((bit)&31) & DISABLED_MASK12)) ||	\
	   (((bit)>>5)==13 && (1UL<<((bit)&31) & DISABLED_MASK13)) ||	\
	   (((bit)>>5)==14 && (1UL<<((bit)&31) & DISABLED_MASK14)) ||	\
	   (((bit)>>5)==15 && (1UL<<((bit)&31) & DISABLED_MASK15)) ||	\
	   (((bit)>>5)==16 && (1UL<<((bit)&31) & DISABLED_MASK16)) )

#define cpu_has(c, bit)							\
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? 1 :	\
	 test_cpu_cap(c, bit))

#define this_cpu_has(bit)						\
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? 1 : 	\
	 x86_this_cpu_test_bit(bit, (unsigned long *)&cpu_info.x86_capability))

/*
 * This macro is for detection of features which need kernel
 * infrastructure to be used.  It may *not* directly test the CPU
 * itself.  Use the cpu_has() family if you want true runtime
 * testing of CPU features, like in hypervisor code where you are
 * supporting a possible guest feature where host support for it
 * is not relevant.
 */
#define cpu_feature_enabled(bit)	\
	(__builtin_constant_p(bit) && DISABLED_MASK_BIT_SET(bit) ? 0 : static_cpu_has(bit))

#define boot_cpu_has(bit)	cpu_has(&boot_cpu_data, bit)

#define set_cpu_cap(c, bit)	set_bit(bit, (unsigned long *)((c)->x86_capability))
#define clear_cpu_cap(c, bit)	clear_bit(bit, (unsigned long *)((c)->x86_capability))
#define setup_clear_cpu_cap(bit) do { \
	clear_cpu_cap(&boot_cpu_data, bit);	\
	set_bit(bit, (unsigned long *)cpu_caps_cleared); \
} while (0)
#define setup_force_cpu_cap(bit) do { \
	set_cpu_cap(&boot_cpu_data, bit);	\
	set_bit(bit, (unsigned long *)cpu_caps_set);	\
} while (0)

#define cpu_has_fpu		boot_cpu_has(X86_FEATURE_FPU)
#define cpu_has_pse		boot_cpu_has(X86_FEATURE_PSE)
#define cpu_has_tsc		boot_cpu_has(X86_FEATURE_TSC)
#define cpu_has_pge		boot_cpu_has(X86_FEATURE_PGE)
#define cpu_has_apic		boot_cpu_has(X86_FEATURE_APIC)
#define cpu_has_fxsr		boot_cpu_has(X86_FEATURE_FXSR)
#define cpu_has_xmm		boot_cpu_has(X86_FEATURE_XMM)
#define cpu_has_xmm2		boot_cpu_has(X86_FEATURE_XMM2)
#define cpu_has_aes		boot_cpu_has(X86_FEATURE_AES)
#define cpu_has_avx		boot_cpu_has(X86_FEATURE_AVX)
#define cpu_has_avx2		boot_cpu_has(X86_FEATURE_AVX2)
#define cpu_has_clflush		boot_cpu_has(X86_FEATURE_CLFLUSH)
#define cpu_has_gbpages		boot_cpu_has(X86_FEATURE_GBPAGES)
#define cpu_has_arch_perfmon	boot_cpu_has(X86_FEATURE_ARCH_PERFMON)
#define cpu_has_pat		boot_cpu_has(X86_FEATURE_PAT)
#define cpu_has_x2apic		boot_cpu_has(X86_FEATURE_X2APIC)
#define cpu_has_xsave		boot_cpu_has(X86_FEATURE_XSAVE)
#define cpu_has_xsaves		boot_cpu_has(X86_FEATURE_XSAVES)
#define cpu_has_osxsave		boot_cpu_has(X86_FEATURE_OSXSAVE)
#define cpu_has_hypervisor	boot_cpu_has(X86_FEATURE_HYPERVISOR)
/*
 * Do not add any more of those clumsy macros - use static_cpu_has() for
 * fast paths and boot_cpu_has() otherwise!
 */

#if defined(CC_HAVE_ASM_GOTO) && defined(CONFIG_X86_FAST_FEATURE_TESTS)
/*
 * Static testing of CPU features.  Used the same as boot_cpu_has().
 * These will statically patch the target code for additional
 * performance.
 */
static __always_inline __pure bool _static_cpu_has(u16 bit)
{
		asm_volatile_goto("1: jmp 6f\n"
			 "2:\n"
			 ".skip -(((5f-4f) - (2b-1b)) > 0) * "
			         "((5f-4f) - (2b-1b)),0x90\n"
			 "3:\n"
			 ".section .altinstructions,\"a\"\n"
			 " .long 1b - .\n"		/* src offset */
			 " .long 4f - .\n"		/* repl offset */
			 " .word %P1\n"			/* always replace */
			 " .byte 3b - 1b\n"		/* src len */
			 " .byte 5f - 4f\n"		/* repl len */
			 " .byte 3b - 2b\n"		/* pad len */
			 ".previous\n"
			 ".section .altinstr_replacement,\"ax\"\n"
			 "4: jmp %l[t_no]\n"
			 "5:\n"
			 ".previous\n"
			 ".section .altinstructions,\"a\"\n"
			 " .long 1b - .\n"		/* src offset */
			 " .long 0\n"			/* no replacement */
			 " .word %P0\n"			/* feature bit */
			 " .byte 3b - 1b\n"		/* src len */
			 " .byte 0\n"			/* repl len */
			 " .byte 0\n"			/* pad len */
			 ".previous\n"
			 ".section .altinstr_aux,\"ax\"\n"
			 "6:\n"
			 " testb %[bitnum],%[cap_byte]\n"
			 " jnz %l[t_yes]\n"
			 " jmp %l[t_no]\n"
			 ".previous\n"
			 : : "i" (bit), "i" (X86_FEATURE_ALWAYS),
			     [bitnum] "i" (1 << (bit & 7)),
			     [cap_byte] "m" (((const char *)boot_cpu_data.x86_capability)[bit >> 3])
			 : : t_yes, t_no);
	t_yes:
		return true;
	t_no:
		return false;
}

#define static_cpu_has(bit)					\
(								\
	__builtin_constant_p(boot_cpu_has(bit)) ?		\
		boot_cpu_has(bit) :				\
		_static_cpu_has(bit)				\
)
#else
/*
 * Fall back to dynamic for gcc versions which don't support asm goto. Should be
 * a minority now anyway.
 */
#define static_cpu_has(bit)		boot_cpu_has(bit)
#endif

#define cpu_has_bug(c, bit)		cpu_has(c, (bit))
#define set_cpu_bug(c, bit)		set_cpu_cap(c, (bit))
#define clear_cpu_bug(c, bit)		clear_cpu_cap(c, (bit))

#define static_cpu_has_bug(bit)		static_cpu_has((bit))
#define boot_cpu_has_bug(bit)		cpu_has_bug(&boot_cpu_data, (bit))

#define MAX_CPU_FEATURES		(NCAPINTS * 32)
#define cpu_have_feature		boot_cpu_has

#define CPU_FEATURE_TYPEFMT		"x86,ven%04Xfam%04Xmod%04X"
#define CPU_FEATURE_TYPEVAL		boot_cpu_data.x86_vendor, boot_cpu_data.x86, \
					boot_cpu_data.x86_model

#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */
#endif /* _ASM_X86_CPUFEATURE_H */
