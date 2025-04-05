/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ALTERNATIVE_H
#define _ASM_X86_ALTERNATIVE_H

#include <linux/types.h>
#include <linux/stringify.h>
#include <linux/objtool.h>
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
struct module;

extern void alternative_instructions(void);
extern void apply_alternatives(struct alt_instr *start, struct alt_instr *end,
			       struct module *mod);
extern void apply_retpolines(s32 *start, s32 *end, struct module *mod);
extern void apply_returns(s32 *start, s32 *end, struct module *mod);
extern void apply_seal_endbr(s32 *start, s32 *end, struct module *mod);
extern void apply_fineibt(s32 *start_retpoline, s32 *end_retpoine,
			  s32 *start_cfi, s32 *end_cfi, struct module *mod);

struct callthunk_sites {
	s32				*call_start, *call_end;
	struct alt_instr		*alt_start, *alt_end;
};

#ifdef CONFIG_CALL_THUNKS
extern void callthunks_patch_builtin_calls(void);
extern void callthunks_patch_module_calls(struct callthunk_sites *sites,
					  struct module *mod);
extern void *callthunks_translate_call_dest(void *dest);
extern int x86_call_depth_emit_accounting(u8 **pprog, void *func, void *ip);
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
							  void *func, void *ip)
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

#define alt_slen		"772b-771b"
#define alt_total_slen		"773b-771b"
#define alt_rlen		"775f-774f"

#define OLDINSTR(oldinstr)						\
	"# ALT: oldinstr\n"						\
	"771:\n\t" oldinstr "\n772:\n"					\
	"# ALT: padding\n"						\
	".skip -(((" alt_rlen ")-(" alt_slen ")) > 0) * "		\
		"((" alt_rlen ")-(" alt_slen ")),0x90\n"		\
	"773:\n"

#define ALTINSTR_ENTRY(ft_flags)					      \
	".pushsection .altinstructions,\"a\"\n"				      \
	" .long 771b - .\n"				/* label           */ \
	" .long 774f - .\n"				/* new instruction */ \
	" .4byte " __stringify(ft_flags) "\n"		/* feature + flags */ \
	" .byte " alt_total_slen "\n"			/* source len      */ \
	" .byte " alt_rlen "\n"				/* replacement len */ \
	".popsection\n"

#define ALTINSTR_REPLACEMENT(newinstr)		/* replacement */	\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	"# ALT: replacement\n"						\
	"774:\n\t" newinstr "\n775:\n"					\
	".popsection\n"

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, newinstr, ft_flags)			\
	OLDINSTR(oldinstr)						\
	ALTINSTR_ENTRY(ft_flags)					\
	ALTINSTR_REPLACEMENT(newinstr)

#define ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) \
	ALTERNATIVE(ALTERNATIVE(oldinstr, newinstr1, ft_flags1), newinstr2, ft_flags2)

/* If @feature is set, patch in @newinstr_yes, otherwise @newinstr_no. */
#define ALTERNATIVE_TERNARY(oldinstr, ft_flags, newinstr_yes, newinstr_no) \
	ALTERNATIVE_2(oldinstr, newinstr_no, X86_FEATURE_ALWAYS, newinstr_yes, ft_flags)

#define ALTERNATIVE_3(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2, \
			newinstr3, ft_flags3)				\
	ALTERNATIVE(ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2), \
		      newinstr3, ft_flags3)

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
	asm_inline volatile(ALTERNATIVE(oldinstr, newinstr, ft_flags) : : : "memory")

#define alternative_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) \
	asm_inline volatile(ALTERNATIVE_2(oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2) ::: "memory")

/*
 * Alternative inline assembly with input.
 *
 * Peculiarities:
 * No memory clobber here.
 * Argument numbers start with 1.
 * Leaving an unused argument 0 to keep API compatibility.
 */
#define alternative_input(oldinstr, newinstr, ft_flags, input...)	\
	asm_inline volatile(ALTERNATIVE(oldinstr, newinstr, ft_flags) \
		: : "i" (0), ## input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, newinstr, ft_flags, output, input...)	\
	asm_inline volatile(ALTERNATIVE(oldinstr, newinstr, ft_flags)	\
		: output : "i" (0), ## input)

/*
 * Like alternative_io, but for replacing a direct call with another one.
 *
 * Use the %c operand modifier which is the generic way to print a bare
 * constant expression with all syntax-specific punctuation omitted. %P
 * is the x86-specific variant which can handle constants too, for
 * historical reasons, but it should be used primarily for PIC
 * references: i.e., if used for a function, it would add the PLT
 * suffix.
 */
#define alternative_call(oldfunc, newfunc, ft_flags, output, input...)			\
	asm_inline volatile(ALTERNATIVE("call %c[old]", "call %c[new]", ft_flags)	\
		: ALT_OUTPUT_SP(output)							\
		: [old] "i" (oldfunc), [new] "i" (newfunc), ## input)

/*
 * Like alternative_call, but there are two features and respective functions.
 * If CPU has feature2, function2 is used.
 * Otherwise, if CPU has feature1, function1 is used.
 * Otherwise, old function is used.
 */
#define alternative_call_2(oldfunc, newfunc1, ft_flags1, newfunc2, ft_flags2,		\
			   output, input...)						\
	asm_inline volatile(ALTERNATIVE_2("call %c[old]", "call %c[new1]", ft_flags1,	\
		"call %c[new2]", ft_flags2)						\
		: ALT_OUTPUT_SP(output)							\
		: [old] "i" (oldfunc), [new1] "i" (newfunc1),				\
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

#define ALT_OUTPUT_SP(...) ASM_CALL_CONSTRAINT, ## __VA_ARGS__

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
#define __ALTERNATIVE(oldinst, newinst, flag)				\
740:									\
	oldinst	;							\
741:									\
	.skip -(((744f-743f)-(741b-740b)) > 0) * ((744f-743f)-(741b-740b)),0x90	;\
742:									\
	.pushsection .altinstructions,"a" ;				\
	altinstr_entry 740b,743f,flag,742b-740b,744f-743f ;		\
	.popsection ;							\
	.pushsection .altinstr_replacement,"ax"	;			\
743:									\
	newinst	;							\
744:									\
	.popsection ;

.macro ALTERNATIVE oldinstr, newinstr, ft_flags
	__ALTERNATIVE(\oldinstr, \newinstr, \ft_flags)
.endm

#define old_len			141b-140b
#define new_len1		144f-143f
#define new_len2		145f-144f
#define new_len3		146f-145f

/*
 * Same as ALTERNATIVE macro above but for two alternatives. If CPU
 * has @feature1, it replaces @oldinstr with @newinstr1. If CPU has
 * @feature2, it replaces @oldinstr with @feature2.
 */
.macro ALTERNATIVE_2 oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2
	__ALTERNATIVE(__ALTERNATIVE(\oldinstr, \newinstr1, \ft_flags1),
		      \newinstr2, \ft_flags2)
.endm

.macro ALTERNATIVE_3 oldinstr, newinstr1, ft_flags1, newinstr2, ft_flags2, newinstr3, ft_flags3
	__ALTERNATIVE(ALTERNATIVE_2(\oldinstr, \newinstr1, \ft_flags1, \newinstr2, \ft_flags2),
		      \newinstr3, \ft_flags3)
.endm

/* If @feature is set, patch in @newinstr_yes, otherwise @newinstr_no. */
#define ALTERNATIVE_TERNARY(oldinstr, ft_flags, newinstr_yes, newinstr_no) \
	ALTERNATIVE_2 oldinstr, newinstr_no, X86_FEATURE_ALWAYS,	\
	newinstr_yes, ft_flags

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_ALTERNATIVE_H */
