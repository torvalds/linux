/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ALTERNATIVE_H
#define _ASM_S390_ALTERNATIVE_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/stringify.h>

struct alt_instr {
	s32 instr_offset;	/* original instruction */
	s32 repl_offset;	/* offset to replacement instruction */
	u16 facility;		/* facility bit set for replacement */
	u8  instrlen;		/* length of original instruction */
} __packed;

void apply_alternative_instructions(void);
void apply_alternatives(struct alt_instr *start, struct alt_instr *end);

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

#define ALTINSTR_ENTRY(facility, num)					\
	"\t.long 661b - .\n"			/* old instruction */	\
	"\t.long " b_altinstr(num)"b - .\n"	/* alt instruction */	\
	"\t.word " __stringify(facility) "\n"	/* facility bit    */	\
	"\t.byte " oldinstr_len "\n"		/* instruction len */	\
	"\t.org . - (" oldinstr_len ") & 1\n"				\
	"\t.org . - (" oldinstr_len ") + (" altinstr_len(num) ")\n"	\
	"\t.org . - (" altinstr_len(num) ") + (" oldinstr_len ")\n"

#define ALTINSTR_REPLACEMENT(altinstr, num)	/* replacement */	\
	b_altinstr(num)":\n\t" altinstr "\n" e_altinstr(num) ":\n"

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, altinstr, facility) \
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr, 1)				\
	".popsection\n"							\
	OLDINSTR(oldinstr)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(facility, 1)					\
	".popsection\n"

#define ALTERNATIVE_2(oldinstr, altinstr1, facility1, altinstr2, facility2)\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr1, 1)				\
	ALTINSTR_REPLACEMENT(altinstr2, 2)				\
	".popsection\n"							\
	OLDINSTR(oldinstr)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(facility1, 1)					\
	ALTINSTR_ENTRY(facility2, 2)					\
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
#define alternative(oldinstr, altinstr, facility)			\
	asm_inline volatile(ALTERNATIVE(oldinstr, altinstr, facility) : : : "memory")

#define alternative_2(oldinstr, altinstr1, facility1, altinstr2, facility2) \
	asm_inline volatile(ALTERNATIVE_2(oldinstr, altinstr1, facility1,   \
				   altinstr2, facility2) ::: "memory")

/* Alternative inline assembly with input. */
#define alternative_input(oldinstr, newinstr, feature, input...)	\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, feature)	\
		: : input)

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, altinstr, facility, output, input...)	\
	asm_inline volatile(ALTERNATIVE(oldinstr, altinstr, facility)	\
		: output : input)

/* Use this macro if more than one output parameter is needed. */
#define ASM_OUTPUT2(a...) a

/* Use this macro if clobbers are needed without inputs. */
#define ASM_NO_INPUT_CLOBBER(clobber...) : clobber

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_ALTERNATIVE_H */
