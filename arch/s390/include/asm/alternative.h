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
	u8  replacementlen;	/* length of new instruction */
} __packed;

void apply_alternative_instructions(void);
void apply_alternatives(struct alt_instr *start, struct alt_instr *end);

/*
 * |661:       |662:	  |6620      |663:
 * +-----------+---------------------+
 * | oldinstr  | oldinstr_padding    |
 * |	       +----------+----------+
 * |	       |	  |	     |
 * |	       | >6 bytes |6/4/2 nops|
 * |	       |6 bytes jg----------->
 * +-----------+---------------------+
 *		 ^^ static padding ^^
 *
 * .altinstr_replacement section
 * +---------------------+-----------+
 * |6641:			     |6651:
 * | alternative instr 1	     |
 * +-----------+---------+- - - - - -+
 * |6642:		 |6652:      |
 * | alternative instr 2 | padding
 * +---------------------+- - - - - -+
 *			  ^ runtime ^
 *
 * .altinstructions section
 * +---------------------------------+
 * | alt_instr entries for each      |
 * | alternative instr		     |
 * +---------------------------------+
 */

#define b_altinstr(num)	"664"#num
#define e_altinstr(num)	"665"#num

#define e_oldinstr_pad_end	"663"
#define oldinstr_len		"662b-661b"
#define oldinstr_total_len	e_oldinstr_pad_end"b-661b"
#define altinstr_len(num)	e_altinstr(num)"b-"b_altinstr(num)"b"
#define oldinstr_pad_len(num) \
	"-(((" altinstr_len(num) ")-(" oldinstr_len ")) > 0) * " \
	"((" altinstr_len(num) ")-(" oldinstr_len "))"

#define INSTR_LEN_SANITY_CHECK(len)					\
	".if " len " > 254\n"						\
	"\t.error \"cpu alternatives does not support instructions "	\
		"blocks > 254 bytes\"\n"				\
	".endif\n"							\
	".if (" len ") %% 2\n"						\
	"\t.error \"cpu alternatives instructions length is odd\"\n"	\
	".endif\n"

#define OLDINSTR_PADDING(oldinstr, num)					\
	".if " oldinstr_pad_len(num) " > 6\n"				\
	"\tjg " e_oldinstr_pad_end "f\n"				\
	"6620:\n"							\
	"\t.fill (" oldinstr_pad_len(num) " - (6620b-662b)) / 2, 2, 0x0700\n" \
	".else\n"							\
	"\t.fill " oldinstr_pad_len(num) " / 6, 6, 0xc0040000\n"	\
	"\t.fill " oldinstr_pad_len(num) " %% 6 / 4, 4, 0x47000000\n"	\
	"\t.fill " oldinstr_pad_len(num) " %% 6 %% 4 / 2, 2, 0x0700\n"	\
	".endif\n"

#define OLDINSTR(oldinstr, num)						\
	"661:\n\t" oldinstr "\n662:\n"					\
	OLDINSTR_PADDING(oldinstr, num)					\
	e_oldinstr_pad_end ":\n"					\
	INSTR_LEN_SANITY_CHECK(oldinstr_len)

#define OLDINSTR_2(oldinstr, num1, num2)				\
	"661:\n\t" oldinstr "\n662:\n"					\
	".if " altinstr_len(num1) " < " altinstr_len(num2) "\n"		\
	OLDINSTR_PADDING(oldinstr, num2)				\
	".else\n"							\
	OLDINSTR_PADDING(oldinstr, num1)				\
	".endif\n"							\
	e_oldinstr_pad_end ":\n"					\
	INSTR_LEN_SANITY_CHECK(oldinstr_len)

#define ALTINSTR_ENTRY(facility, num)					\
	"\t.long 661b - .\n"			/* old instruction */	\
	"\t.long " b_altinstr(num)"b - .\n"	/* alt instruction */	\
	"\t.word " __stringify(facility) "\n"	/* facility bit    */	\
	"\t.byte " oldinstr_total_len "\n"	/* source len	   */	\
	"\t.byte " altinstr_len(num) "\n"	/* alt instruction len */

#define ALTINSTR_REPLACEMENT(altinstr, num)	/* replacement */	\
	b_altinstr(num)":\n\t" altinstr "\n" e_altinstr(num) ":\n"	\
	INSTR_LEN_SANITY_CHECK(altinstr_len(num))

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, altinstr, facility) \
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr, 1)				\
	".popsection\n"							\
	OLDINSTR(oldinstr, 1)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(facility, 1)					\
	".popsection\n"

#define ALTERNATIVE_2(oldinstr, altinstr1, facility1, altinstr2, facility2)\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(altinstr1, 1)				\
	ALTINSTR_REPLACEMENT(altinstr2, 2)				\
	".popsection\n"							\
	OLDINSTR_2(oldinstr, 1, 2)					\
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
	asm volatile(ALTERNATIVE(oldinstr, altinstr, facility) : : : "memory")

#define alternative_2(oldinstr, altinstr1, facility1, altinstr2, facility2) \
	asm volatile(ALTERNATIVE_2(oldinstr, altinstr1, facility1,	    \
				   altinstr2, facility2) ::: "memory")

#endif /* __ASSEMBLY__ */

#endif /* _ASM_S390_ALTERNATIVE_H */
