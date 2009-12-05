#ifndef _ASM_X86_ALTERNATIVE_H
#define _ASM_X86_ALTERNATIVE_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stringify.h>
#include <asm/asm.h>

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
#define LOCK_PREFIX \
		".section .smp_locks,\"a\"\n"	\
		_ASM_ALIGN "\n"			\
		_ASM_PTR "661f\n" /* address */	\
		".previous\n"			\
		"661:\n\tlock; "

#else /* ! CONFIG_SMP */
#define LOCK_PREFIX ""
#endif

/* This must be included *after* the definition of LOCK_PREFIX */
#include <asm/cpufeature.h>

struct alt_instr {
	u8 *instr;		/* original instruction */
	u8 *replacement;
	u8  cpuid;		/* cpuid bit set for replacement */
	u8  instrlen;		/* length of original instruction */
	u8  replacementlen;	/* length of new instruction, <= instrlen */
	u8  pad1;
#ifdef CONFIG_X86_64
	u32 pad2;
#endif
};

extern void alternative_instructions(void);
extern void apply_alternatives(struct alt_instr *start, struct alt_instr *end);

struct module;

#ifdef CONFIG_SMP
extern void alternatives_smp_module_add(struct module *mod, char *name,
					void *locks, void *locks_end,
					void *text, void *text_end);
extern void alternatives_smp_module_del(struct module *mod);
extern void alternatives_smp_switch(int smp);
#else
static inline void alternatives_smp_module_add(struct module *mod, char *name,
					       void *locks, void *locks_end,
					       void *text, void *text_end) {}
static inline void alternatives_smp_module_del(struct module *mod) {}
static inline void alternatives_smp_switch(int smp) {}
#endif	/* CONFIG_SMP */

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, newinstr, feature)			\
									\
      "661:\n\t" oldinstr "\n662:\n"					\
      ".section .altinstructions,\"a\"\n"				\
      _ASM_ALIGN "\n"							\
      _ASM_PTR "661b\n"				/* label           */	\
      _ASM_PTR "663f\n"				/* new instruction */	\
      "	 .byte " __stringify(feature) "\n"	/* feature bit     */	\
      "	 .byte 662b-661b\n"			/* sourcelen       */	\
      "	 .byte 664f-663f\n"			/* replacementlen  */	\
      "	 .byte 0xff + (664f-663f) - (662b-661b)\n" /* rlen <= slen */	\
      ".previous\n"							\
      ".section .altinstr_replacement, \"ax\"\n"			\
      "663:\n\t" newinstr "\n664:\n"		/* replacement     */	\
      ".previous"

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
#define alternative(oldinstr, newinstr, feature)			\
	asm volatile (ALTERNATIVE(oldinstr, newinstr, feature) : : : "memory")

/*
 * Alternative inline assembly with input.
 *
 * Pecularities:
 * No memory clobber here.
 * Argument numbers start with 1.
 * Best is to use constraints that are fixed size (like (%1) ... "r")
 * If you use variable sized constraints like "m" or "g" in the
 * replacement make sure to pad to the worst case length.
 * Leaving an unused argument 0 to keep API compatibility.
 */
#define alternative_input(oldinstr, newinstr, feature, input...)	\
	asm volatile (ALTERNATIVE(oldinstr, newinstr, feature)		\
		: : "i" (0), ## input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, newinstr, feature, output, input...)	\
	asm volatile (ALTERNATIVE(oldinstr, newinstr, feature)		\
		: output : "i" (0), ## input)

/*
 * use this macro(s) if you need more than one output parameter
 * in alternative_io
 */
#define ASM_OUTPUT2(a, b) a, b

struct paravirt_patch_site;
#ifdef CONFIG_PARAVIRT
void apply_paravirt(struct paravirt_patch_site *start,
		    struct paravirt_patch_site *end);
#else
static inline void apply_paravirt(struct paravirt_patch_site *start,
				  struct paravirt_patch_site *end)
{}
#define __parainstructions	NULL
#define __parainstructions_end	NULL
#endif

/*
 * Clear and restore the kernel write-protection flag on the local CPU.
 * Allows the kernel to edit read-only pages.
 * Side-effect: any interrupt handler running between save and restore will have
 * the ability to write to read-only pages.
 *
 * Warning:
 * Code patching in the UP case is safe if NMIs and MCE handlers are stopped and
 * no thread can be preempted in the instructions being modified (no iret to an
 * invalid instruction possible) or if the instructions are changed from a
 * consistent state to another consistent state atomically.
 * More care must be taken when modifying code in the SMP case because of
 * Intel's errata.
 * On the local CPU you need to be protected again NMI or MCE handlers seeing an
 * inconsistent instruction while you patch.
 */
extern void *text_poke(void *addr, const void *opcode, size_t len);

#endif /* _ASM_X86_ALTERNATIVE_H */
