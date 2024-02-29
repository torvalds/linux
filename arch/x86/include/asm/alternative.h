/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ALTERNATIVE_H
#define _ASM_X86_ALTERNATIVE_H

#include <linux/types.h>
#include <linux/stringify.h>
#include <asm/asm.h>

#define ALT_FLAGS_SHIFT		16

#define ALT_FLAG_NOT		(1 << 0)
#define ALT_NOT(feature)	((ALT_FLAG_NOT << ALT_FLAGS_SHIFT) | (feature))
#define ALT_FLAG_DIRECT_CALL	(1 << 1)
#define ALT_DIRECT_CALL(feature) ((ALT_FLAG_DIRECT_CALL << ALT_FLAGS_SHIFT) | (feature))
#define ALT_CALL_ALWAYS		ALT_DIRECT_CALL(X86_FEATURE_ALWAYS)

#ifndef __ASSEMBLY__

#include <linux/stddef.h>

/*
 * Alternative inline assembly for SMP.
 *
 * The LOCK_PREFIX macro defined here replaces the LOCK and
 * LOCK_PREFIX macros used everywhere in the source tree.
 *
 * SMP alternatives use the same data structures as the other
 * alternatives and the X86_FEATURE_UP flag to indicate the case of a
 * UP system running a SMP kernel.  The existing apply_alternatives()
 * works fine for patching a SMP kernel for UP.
 *
 * The SMP alternative tables can be kept after boot and contain both
 * UP and SMP versions of the instructions to allow switching back to
 * SMP at runtime, when hotplugging in a new CPU, which is especially
 * useful in virtualized environments.
 *
 * The very common lock prefix is handled as special case in a
 * separate table which is a pure address list without replacement ptr
 * and size information.  That keeps the table sizes small.
 */

#ifdef CONFIG_SMP
#define LOCK_PREFIX_HERE \
		".pushsection .smp_locks,\"a\"\n"	\
		".balign 4\n"				\
		".long 671f - .\n" /* offset */		\
		".popsection\n"				\
		"671:"

#define LOCK_PREFIX LOCK_PREFIX_HERE "\n\tlock; "

#else /* ! CONFIG_SMP */
#define LOCK_PREFIX_HERE ""
#define LOCK_PREFIX ""
#endif

/*
 * objtool annotation to ignore the alternatives and only consider the original
 * instruction(s).
 */
#define ANNOTATE_IGNORE_ALTERNATIVE				\
	"999:\n\t"						\
	".pushsection .discard.ignore_alts\n\t"			\
	".long 999b\n\t"					\
	".popsection\n\t"

/*
 * The patching flags are part of the upper bits of the @ft_flags parameter when
 * specifying them. The split is currently like this:
 *
 * [31... flags ...16][15... CPUID feature bit ...0]
 *
 * but since this is all hidden in the macros argument being split, those fields can be
 * extended in the future to fit in a u64 or however the need arises.
 */
struct alt_instr {
	s32 instr_offset;	/* original instruction */
	s32 repl_offset;	/* offset to replacement instruction */

	union {
		struct {
			u32 cpuid: 16;	/* CPUID bit set for replacement */
			u32 flags: 16;	/* patching control flags */
		};
		u32 ft_flags;
	};

	u8  instrlen;		/* length of original instruction */
	u8  replacementlen;	/* length of new instruction */
} __packed;

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];

/*
 * Debug flag that can be tested to see whether alternative
 * instructions were patched in already:
 */
extern int alternatives_patched;

extern void alternative_instructions(void);
extern void apply_alternatives(struct alt_instr *start, struct alt_instr *end);
extern void apply_retpolines(s32 *start, s32 *end);
extern void apply_returns(s32 *start, s32 *end);
extern void apply_seal_endbr(s32 *start, s32 *end);
extern void apply_fineibt(s32 *start_retpoline, s32 *end_retpoine,
			  s32 *start_cfi, s32 *end_cfi);

struct module;

struct callthunk_sites {
	s32				*call_start, *call_end;
	struct alt_instr		*alt_start, *alt_end;
};

#ifdef CONFIG_CALL_THUNKS
extern void callthunks_patch_builtin_calls(void);
extern void callthunks_patch_module_calls(struct callthunk_sites *sites,
					  struct module *mod);
extern void *callthunks_translate_call_dest(void *dest);
extern int x86_call_depth_emit_accounting(u8 **pprog, void *func);
#else
static __always_inline void callthunks_patch_builtin_calls(void) {}
static __always_inline void
callthunks_patch_module_calls(struct callthunk_sites *sites,
			      struct module *mod) {}
static __always_inline void *callthunks_translate_call_dest(void *dest)
{
	return dest;
}
static __always_inline int x86_call_depth_emit_accounting(u8 **pprog,
							  void *func)
{
	return 0;
}
#endif

#ifdef CONFIG_SMP
extern void alternatives_smp_module_add(struct module *mod, char *name,
					void *locks, void *locks_end,
					void *text, void *text_end);
extern void alternatives_smp_module_del(struct module *mod);
extern void alternatives_enable_smp(void);
extern int alternatives_text_reserved(void *start, void *end);
extern bool skip_smp_alternatives;
#else
static inline void alternatives_smp_module_add(struct module *mod, char *name,
					       void *locks, void *locks_end,
					       void *text, void *text_end) {}
static inline void alternatives_smp_module_del(struct module *mod) {}
static inline void alternatives_enable_smp(void) {}
static inline int alternatives_text_reserved(void *start, void *end)
{
	return 0;
}
#endif	/* CONFIG_SMP */

#define ALT_CALL_INSTR		"call BUG_func"

#define b_replacement(num)	"664"#num
#define e_replacement(num)	"665"#num

#define alt_end_marker		"663"
#define alt_slen		"662b-661b"
#define alt_total_slen		alt_end_marker"b-661b"
#define alt_rlen(num)		e_replacement(num)"f-"b_replacement(num)"f"

#define OLDINSTR(oldinstr, num)						\
	"# ALT: oldnstr\n"						\
	"661:\n\t" oldinstr "\n662:\n"					\
	"# ALT: padding\n"						\
	".skip -(((" alt_rlen(num) ")-(" alt_slen ")) > 0) * "		\
		"((" alt_rlen(num) ")-(" alt_slen ")),0x90\n"		\
	alt_end_marker ":\n"

/*
 * gas compatible max based on the idea from:
 * http://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax
 *
 * The additional "-" is needed because gas uses a "true" value of -1.
 */
#define alt_max_short(a, b)	"((" a ") ^ (((" a ") ^ (" b ")) & -(-((" a ") < (" b ")))))"

/*
 * Pad the second replacement alternative with additional NOPs if it is
 * additionally longer than the first replacement alternative.
 */
#define OLDINSTR_2(oldinstr, num1, num2) \
	"# ALT: oldinstr2\n"									\
	"661:\n\t" oldinstr "\n662:\n"								\
	"# ALT: padding2\n"									\
	".skip -((" alt_max_short(alt_rlen(num1), alt_rlen(num2)) " - (" alt_slen ")) > 0) * "	\
		"(" alt_max_short(alt_rlen(num1), alt_rlen(num2)) " - (" alt_slen ")), 0x90\n"	\
	alt_end_marker ":\n"

#define OLDINSTR_3(oldinsn, n1, n2, n3)								\
	"# ALT: oldinstr3\n"									\
	"661:\n\t" oldinsn "\n662:\n"								\
	"# ALT: padding3\n"									\
	".skip -((" alt_max_short(alt_max_short(alt_rlen(n1), alt_rlen(n2)), alt_rlen(n3))	\
		" - (" alt_slen ")) > 0) * "							\
		"(" alt_max_short(alt_max_short(alt_rlen(n1), alt_rlen(n2)), alt_rlen(n3))	\
		" - (" alt_slen ")), 0x90\n"							\
	alt_end_marker ":\n"

#define ALTINSTR_ENTRY(ft_flags, num)					      \
	" .long 661b - .\n"				/* label           */ \
	" .long " b_replacement(num)"f - .\n"		/* new instruction */ \
	" .4byte " __stringify(ft_flags) "\n"		/* feature + flags */ \
	" .byte " alt_total_slen "\n"			/* source len      */ \
	" .byte " alt_rlen(num) "\n"			/* replacement len */

#define ALTINSTR_REPLACEMENT(newinstr, num)		/* replacement */	\
	"# ALT: replacement " #num "\n"						\
	b_replacement(num)":\n\t" newinstr "\n" e_replacement(num) ":\n"

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, newinstr, ft_flags)			\
	OLDINSTR(oldinstr, 1)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(ft_flags, 1)					\
	".popsection\n"							\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(newinstr, 1)				\
	".popsection\n"

#define ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) \
	OLDINSTR_2(oldinstr, 1, 2)					\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(ft_flags1, 1)					\
	ALTINSTR_ENTRY(ft_flags2, 2)					\
	".popsection\n"							\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(newinstr1, 1)				\
	ALTINSTR_REPLACEMENT(newinstr2, 2)				\
	".popsection\n"

/* If @feature is set, patch in @newinstr_yes, otherwise @newinstr_no. */
#define ALTERNATIVE_TERNARY(oldinstr, ft_flags, newinstr_yes, newinstr_no) \
	ALTERNATIVE_2(oldinstr, newinstr_no, X86_FEATURE_ALWAYS,	\
		      newinstr_yes, ft_flags)

#define ALTERNATIVE_3(oldinsn, newinsn1, ft_flags1, newinsn2, ft_flags2, \
			newinsn3, ft_flags3)				\
	OLDINSTR_3(oldinsn, 1, 2, 3)					\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(ft_flags1, 1)					\
	ALTINSTR_ENTRY(ft_flags2, 2)					\
	ALTINSTR_ENTRY(ft_flags3, 3)					\
	".popsection\n"							\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(newinsn1, 1)				\
	ALTINSTR_REPLACEMENT(newinsn2, 2)				\
	ALTINSTR_REPLACEMENT(newinsn3, 3)				\
	".popsection\n"

/*
 * Alternative instructions for different CPU types or capabilities.
 *
 * This allows to use optimized instructions even on generic binary
 * kernels.
 *
 * length of oldinstr must be longer or equal the length of newinstr
 * It can be padded with nops as needed.
 *
 * For non barrier like inlines please define new variants
 * without volatile and memory clobber.
 */
#define alternative(oldinstr, newinstr, ft_flags)			\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, ft_flags) : : : "memory")

#define alternative_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) \
	asm_inline volatile(ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) ::: "memory")

#define alternative_ternary(oldinstr, ft_flags, newinstr_yes, newinstr_no) \
	asm_inline volatile(ALTERNATIVE_TERNARY(oldinstr, ft_flags, newinstr_yes, newinstr_no) ::: "memory")

/*
 * Alternative inline assembly with input.
 *
 * Peculiarities:
 * No memory clobber here.
 * Argument numbers start with 1.
 * Leaving an unused argument 0 to keep API compatibility.
 */
#define alternative_input(oldinstr, newinstr, ft_flags, input...)	\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, ft_flags)	\
		: : "i" (0), ## input)

/*
 * This is similar to alternative_input. But it has two features and
 * respective instructions.
 *
 * If CPU has feature2, newinstr2 is used.
 * Otherwise, if CPU has feature1, newinstr1 is used.
 * Otherwise, oldinstr is used.
 */
#define alternative_input_2(oldinstr, newinstr1, ft_flags1, newinstr2,	     \
			   ft_flags2, input...)				     \
	asm_inline volatile(ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1,     \
		newinstr2, ft_flags2)					     \
		: : "i" (0), ## input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, newinstr, ft_flags, output, input...)	\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, ft_flags)	\
		: output : "i" (0), ## input)

/* Like alternative_io, but for replacing a direct call with another one. */
#define alternative_call(oldfunc, newfunc, ft_flags, output, input...)	\
	asm_inline volatile (ALTERNATIVE("call %P[old]", "call %P[new]", ft_flags) \
		: output : [old] "i" (oldfunc), [new] "i" (newfunc), ## input)

/*
 * Like alternative_call, but there are two features and respective functions.
 * If CPU has feature2, function2 is used.
 * Otherwise, if CPU has feature1, function1 is used.
 * Otherwise, old function is used.
 */
#define alternative_call_2(oldfunc, newfunc1, ft_flags1, newfunc2, ft_flags2,   \
			   output, input...)				      \
	asm_inline volatile (ALTERNATIVE_2("call %P[old]", "call %P[new1]", ft_flags1,\
		"call %P[new2]", ft_flags2)				      \
		: output, ASM_CALL_CONSTRAINT				      \
		: [old] "i" (oldfunc), [new1] "i" (newfunc1),		      \
		  [new2] "i" (newfunc2), ## input)

/*
 * use this macro(s) if you need more than one output parameter
 * in alternative_io
 */
#define ASM_OUTPUT2(a...) a

/*
 * use this macro if you need clobbers but no inputs in
 * alternative_{input,io,call}()
 */
#define ASM_NO_INPUT_CLOBBER(clbr...) "i" (0) : clbr

/* Macro for creating assembler functions avoiding any C magic. */
#define DEFINE_ASM_FUNC(func, instr, sec)		\
	asm (".pushsection " #sec ", \"ax\"\n"		\
	     ".global " #func "\n\t"			\
	     ".type " #func ", @function\n\t"		\
	     ASM_FUNC_ALIGN "\n"			\
	     #func ":\n\t"				\
	     ASM_ENDBR					\
	     instr "\n\t"				\
	     ASM_RET					\
	     ".size " #func ", . - " #func "\n\t"	\
	     ".popsection")

void BUG_func(void);
void nop_func(void);

#else /* __ASSEMBLY__ */

#ifdef CONFIG_SMP
	.macro LOCK_PREFIX
672:	lock
	.pushsection .smp_locks,"a"
	.balign 4
	.long 672b - .
	.popsection
	.endm
#else
	.macro LOCK_PREFIX
	.endm
#endif

/*
 * objtool annotation to ignore the alternatives and only consider the original
 * instruction(s).
 */
.macro ANNOTATE_IGNORE_ALTERNATIVE
	.Lannotate_\@:
	.pushsection .discard.ignore_alts
	.long .Lannotate_\@
	.popsection
.endm

/*
 * Issue one struct alt_instr descriptor entry (need to put it into
 * the section .altinstructions, see below). This entry contains
 * enough information for the alternatives patching code to patch an
 * instruction. See apply_alternatives().
 */
.macro altinstr_entry orig alt ft_flags orig_len alt_len
	.long \orig - .
	.long \alt - .
	.4byte \ft_flags
	.byte \orig_len
	.byte \alt_len
.endm

.macro ALT_CALL_INSTR
	call BUG_func
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr. ".skip" directive takes care of proper instruction padding
 * in case @newinstr is longer than @oldinstr.
 */
.macro ALTERNATIVE oldinstr, newinstr, ft_flags
140:
	\oldinstr
141:
	.skip -(((144f-143f)-(141b-140b)) > 0) * ((144f-143f)-(141b-140b)),0x90
142:

	.pushsection .altinstructions,"a"
	altinstr_entry 140b,143f,\ft_flags,142b-140b,144f-143f
	.popsection

	.pushsection .altinstr_replacement,"ax"
143:
	\newinstr
144:
	.popsection
.endm

#define old_len			141b-140b
#define new_len1		144f-143f
#define new_len2		145f-144f
#define new_len3		146f-145f

/*
 * gas compatible max based on the idea from:
 * http://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax
 *
 * The additional "-" is needed because gas uses a "true" value of -1.
 */
#define alt_max_2(a, b)		((a) ^ (((a) ^ (b)) & -(-((a) < (b)))))
#define alt_max_3(a, b, c)	(alt_max_2(alt_max_2(a, b), c))


/*
 * Same as ALTERNATIVE macro above but for two alternatives. If CPU
 * has @feature1, it replaces @oldinstr with @newinstr1. If CPU has
 * @feature2, it replaces @oldinstr with @feature2.
 */
.macro ALTERNATIVE_2 oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2
140:
	\oldinstr
141:
	.skip -((alt_max_2(new_len1, new_len2) - (old_len)) > 0) * \
		(alt_max_2(new_len1, new_len2) - (old_len)),0x90
142:

	.pushsection .altinstructions,"a"
	altinstr_entry 140b,143f,\ft_flags1,142b-140b,144f-143f
	altinstr_entry 140b,144f,\ft_flags2,142b-140b,145f-144f
	.popsection

	.pushsection .altinstr_replacement,"ax"
143:
	\newinstr1
144:
	\newinstr2
145:
	.popsection
.endm

.macro ALTERNATIVE_3 oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2, newinstr3, ft_flags3
140:
	\oldinstr
141:
	.skip -((alt_max_3(new_len1, new_len2, new_len3) - (old_len)) > 0) * \
		(alt_max_3(new_len1, new_len2, new_len3) - (old_len)),0x90
142:

	.pushsection .altinstructions,"a"
	altinstr_entry 140b,143f,\ft_flags1,142b-140b,144f-143f
	altinstr_entry 140b,144f,\ft_flags2,142b-140b,145f-144f
	altinstr_entry 140b,145f,\ft_flags3,142b-140b,146f-145f
	.popsection

	.pushsection .altinstr_replacement,"ax"
143:
	\newinstr1
144:
	\newinstr2
145:
	\newinstr3
146:
	.popsection
.endm

/* If @feature is set, patch in @newinstr_yes, otherwise @newinstr_no. */
#define ALTERNATIVE_TERNARY(oldinstr, ft_flags, newinstr_yes, newinstr_no) \
	ALTERNATIVE_2 oldinstr, newinstr_no, X86_FEATURE_ALWAYS,	\
	newinstr_yes, ft_flags

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_ALTERNATIVE_H */
