#ifndef _ASM_SPARC_JUMP_LABEL_H
#define _ASM_SPARC_JUMP_LABEL_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/system.h>

#define JUMP_LABEL_NOP_SIZE 4

#define JUMP_LABEL(key, label)					\
	do {							\
		asm goto("1:\n\t"				\
			 "nop\n\t"				\
			 "nop\n\t"				\
			 ".pushsection __jump_table,  \"a\"\n\t"\
			 ".word 1b, %l[" #label "], %c0\n\t"	\
			 ".popsection \n\t"			\
			 : :  "i" (key) :  : label);\
	} while (0)

#endif /* __KERNEL__ */

typedef u32 jump_label_t;

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif
