/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ALTERNATIVE_H
#define _ASM_S390_ALTERNATIVE_H

/*
 * Each alternative comes with a 32 bit feature field:
 *	union {
 *		u32 feature;
 *		struct {
 *			u32 ctx	 : 4;
 *			u32 type : 8;
 *			u32 data : 20;
 *		};
 *	}
 *
 * @ctx is a bitfield, where only one bit must be set. Each bit defines
 * in which context an alternative is supposed to be applied to the
 * kernel image:
 *
 * - from the decompressor before the kernel itself is executed
 * - from early kernel code from within the kernel
 *
 * @type is a number which defines the type and with that the type
 * specific alternative patching.
 *
 * @data is additional type specific information which defines if an
 * alternative should be applied.
 */

#define ALT_CTX_EARLY		1
#define ALT_CTX_LATE		2
#define ALT_CTX_ALL		(ALT_CTX_EARLY | ALT_CTX_LATE)

#define ALT_TYPE_FACILITY	0
#define ALT_TYPE_FEATURE	1
#define ALT_TYPE_SPEC		2

#define ALT_DATA_SHIFT		0
#define ALT_TYPE_SHIFT		20
#define ALT_CTX_SHIFT		28

#define ALT_FACILITY(facility)		(ALT_CTX_EARLY << ALT_CTX_SHIFT		| \
					 ALT_TYPE_FACILITY << ALT_TYPE_SHIFT	| \
					 (facility) << ALT_DATA_SHIFT)

#define ALT_FEATURE(feature)		(ALT_CTX_EARLY << ALT_CTX_SHIFT		| \
					 ALT_TYPE_FEATURE << ALT_TYPE_SHIFT	| \
					 (feature) << ALT_DATA_SHIFT)

#define ALT_SPEC(facility)		(ALT_CTX_LATE << ALT_CTX_SHIFT		| \
					 ALT_TYPE_SPEC << ALT_TYPE_SHIFT	| \
					 (facility) << ALT_DATA_SHIFT)

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct alt_instr {
	s32 instr_offset;	/* original instruction */
	s32 repl_offset;	/* offset to replacement instruction */
	union {
		u32 feature;	/* feature required for replacement */
		struct {
			u32 ctx	 : 4;  /* context */
			u32 type : 8;  /* type of alternative */
			u32 data : 20; /* patching information */
		};
	};
	u8  instrlen;		/* length of original instruction */
} __packed;

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];

void __apply_alternatives(struct alt_instr *start, struct alt_instr *end, unsigned int ctx);

static inline void apply_alternative_instructions(void)
{
	__apply_alternatives(__alt_instructions, __alt_instructions_end, ALT_CTX_LATE);
}

static inline void apply_alternatives(struct alt_instr *start, struct alt_instr *end)
{
	__apply_alternatives(start, end, ALT_CTX_ALL);
}

/*
 * +---------------------------------+
 * |661:			     |662:
 * | oldinstr			     |
 * +---------------------------------+
 *
 * .altinstr_replacement section
 * +---------------------------------+
 * |6641:			     |6651:
 * | alternative instr 1	     |
 * +---------------------------------+
 * |6642:			     |6652:
 * | alternative instr 2	     |
 * +---------------------------------+
 *
 * .altinstructions section
 * +---------------------------------+
 * | alt_instr entries for each      |
 * | alternative instr		     |
 * +---------------------------------+
 */

#define b_altinstr(num)		"664"#num
#define e_altinstr(num)		"665"#num
#define oldinstr_len		"662b-661b"
#define altinstr_len(num)	e_altinstr(num)"b-"b_altinstr(num)"b"

#define OLDINSTR(oldinstr) \
	"661:\n\t" oldinstr "\n662:\n"

#define ALTINSTR_ENTRY(feature, num)					\
	"\t.long 661b - .\n"			/* old instruction */	\
	"\t.long " b_altinstr(num)"b - .\n"	/* alt instruction */	\
	"\t.long " __stringify(feature) "\n"	/* feature	   */	\
	"\t.byte " oldinstr_len "\n"		/* instruction len */	\
	"\t.org . - (" oldinstr_len ") & 1\n"				\
	"\t.org . - (" oldinstr_len ") + (" altinstr_len(num) ")\n"	\
	"\t.org . - (" altinstr_len(num) ") + (" oldinstr_len ")\n"

#define ALTINSTR_REPLACEMENT(altinstr, num)	/* replacement */	\
	b_altinstr(num)":\n\t" altinstr "\n" e_altinstr(num) ":\n"

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, altinstr, feature) \
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr, 1)				\
	".popsection\n"							\
	OLDINSTR(oldinstr)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(feature, 1)					\
	".popsection\n"

#define ALTERNATIVE_2(oldinstr, altinstr1, feature1, altinstr2, feature2)\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr1, 1)				\
	ALTINSTR_REPLACEMENT(altinstr2, 2)				\
	".popsection\n"							\
	OLDINSTR(oldinstr)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(feature1, 1)					\
	ALTINSTR_ENTRY(feature2, 2)					\
	".popsection\n"

/*
 * Alternative instructions for different CPU types or capabilities.
 *
 * This allows to use optimized instructions even on generic binary
 * kernels.
 *
 * oldinstr is padded with jump and nops at compile time if altinstr is
 * longer. altinstr is padded with jump and nops at run-time during patching.
 *
 * For non barrier like inlines please define new variants
 * without volatile and memory clobber.
 */
#define alternative(oldinstr, altinstr, feature)			\
	asm_inline volatile(ALTERNATIVE(oldinstr, altinstr, feature) : : : "memory")

#define alternative_2(oldinstr, altinstr1, feature1, altinstr2, feature2) \
	asm_inline volatile(ALTERNATIVE_2(oldinstr, altinstr1, feature1,   \
				   altinstr2, feature2) ::: "memory")

/* Alternative inline assembly with input. */
#define alternative_input(oldinstr, newinstr, feature, input...)	\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, feature)	\
		: : input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, altinstr, feature, output, input...)	\
	asm_inline volatile(ALTERNATIVE(oldinstr, altinstr, feature)	\
		: output : input)

/* Use this macro if more than one output parameter is needed. */
#define ASM_OUTPUT2(a...) a

/* Use this macro if clobbers are needed without inputs. */
#define ASM_NO_INPUT_CLOBBER(clobber...) : clobber

#else  /* __ASSEMBLY__ */

/*
 * Issue one struct alt_instr descriptor entry (need to put it into
 * the section .altinstructions, see below). This entry contains
 * enough information for the alternatives patching code to patch an
 * instruction. See apply_alternatives().
 */
.macro alt_entry orig_start, orig_end, alt_start, alt_end, feature
	.long	\orig_start - .
	.long	\alt_start - .
	.long	\feature
	.byte	\orig_end - \orig_start
	.org	. - ( \orig_end - \orig_start ) & 1
	.org	. - ( \orig_end - \orig_start ) + ( \alt_end - \alt_start )
	.org	. - ( \alt_end - \alt_start ) + ( \orig_end - \orig_start )
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr.
 */
.macro ALTERNATIVE oldinstr, newinstr, feature
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr
771:	.popsection
772:	\oldinstr
773:	.pushsection .altinstructions,"a"
	alt_entry 772b, 773b, 770b, 771b, \feature
	.popsection
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr.
 */
.macro ALTERNATIVE_2 oldinstr, newinstr1, feature1, newinstr2, feature2
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr1
771:	\newinstr2
772:	.popsection
773:	\oldinstr
774:	.pushsection .altinstructions,"a"
	alt_entry 773b, 774b, 770b, 771b,\feature1
	alt_entry 773b, 774b, 771b, 772b,\feature2
	.popsection
.endm

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_ALTERNATIVE_H */
