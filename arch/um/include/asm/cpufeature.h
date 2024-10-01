/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_CPUFEATURE_H
#define _ASM_UM_CPUFEATURE_H

#include <asm/processor.h>

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

#include <asm/asm.h>
#include <linux/bitops.h>

extern const char * const x86_cap_flags[NCAPINTS*32];
extern const char * const x86_power_flags[32];
#define X86_CAP_FMT "%s"
#define x86_cap_flag(flag) x86_cap_flags[flag]

/*
 * In order to save room, we index into this array by doing
 * X86_BUG_<name> - NCAPINTS*32.
 */
extern const char * const x86_bug_flags[NBUGINTS*32];

#define test_cpu_cap(c, bit)						\
	 test_bit(bit, (unsigned long *)((c)->x86_capability))

/*
 * There are 32 bits/features in each mask word.  The high bits
 * (selected with (bit>>5) give us the word number and the low 5
 * bits give us the bit/feature number inside the word.
 * (1UL<<((bit)&31) gives us a mask for the feature_bit so we can
 * see if it is set in the mask word.
 */
#define CHECK_BIT_IN_MASK_WORD(maskname, word, bit)	\
	(((bit)>>5)==(word) && (1UL<<((bit)&31) & maskname##word ))

#define cpu_has(c, bit)							\
	 test_cpu_cap(c, bit)

#define this_cpu_has(bit)						\
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? 1 :	\
	 x86_this_cpu_test_bit(bit,					\
		(unsigned long __percpu *)&cpu_info.x86_capability))

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

extern void setup_clear_cpu_cap(unsigned int bit);

#define setup_force_cpu_cap(bit) do { \
	set_cpu_cap(&boot_cpu_data, bit);	\
	set_bit(bit, (unsigned long *)cpu_caps_set);	\
} while (0)

#define setup_force_cpu_bug(bit) setup_force_cpu_cap(bit)

/*
 * Static testing of CPU features. Used the same as boot_cpu_has(). It
 * statically patches the target code for additional performance. Use
 * static_cpu_has() only in fast paths, where every cycle counts. Which
 * means that the boot_cpu_has() variant is already fast enough for the
 * majority of cases and you should stick to using it as it is generally
 * only two instructions: a RIP-relative MOV and a TEST.
 */
static __always_inline bool _static_cpu_has(u16 bit)
{
	asm goto("1: jmp 6f\n"
		 "2:\n"
		 ".skip -(((5f-4f) - (2b-1b)) > 0) * "
			 "((5f-4f) - (2b-1b)),0x90\n"
		 "3:\n"
		 ".section .altinstructions,\"a\"\n"
		 " .long 1b - .\n"		/* src offset */
		 " .long 4f - .\n"		/* repl offset */
		 " .word %P[always]\n"		/* always replace */
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
		 " .word %P[feature]\n"		/* feature bit */
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
		 : : [feature]  "i" (bit),
		     [always]   "i" (X86_FEATURE_ALWAYS),
		     [bitnum]   "i" (1 << (bit & 7)),
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

#define cpu_has_bug(c, bit)		cpu_has(c, (bit))
#define set_cpu_bug(c, bit)		set_cpu_cap(c, (bit))

#define static_cpu_has_bug(bit)		static_cpu_has((bit))
#define boot_cpu_has_bug(bit)		cpu_has_bug(&boot_cpu_data, (bit))
#define boot_cpu_set_bug(bit)		set_cpu_cap(&boot_cpu_data, (bit))

#define MAX_CPU_FEATURES		(NCAPINTS * 32)
#define cpu_have_feature		boot_cpu_has

#define CPU_FEATURE_TYPEFMT		"x86,ven%04Xfam%04Xmod%04X"
#define CPU_FEATURE_TYPEVAL		boot_cpu_data.x86_vendor, boot_cpu_data.x86, \
					boot_cpu_data.x86_model

#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */
#endif /* _ASM_UM_CPUFEATURE_H */
