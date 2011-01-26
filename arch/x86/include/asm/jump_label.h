#ifndef _ASM_X86_JUMP_LABEL_H
#define _ASM_X86_JUMP_LABEL_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/nops.h>

#define JUMP_LABEL_NOP_SIZE 5

# define JUMP_LABEL_INITIAL_NOP ".byte 0xe9 \n\t .long 0\n\t"

# define JUMP_LABEL(key, label)					\
	do {							\
		asm goto("1:"					\
			JUMP_LABEL_INITIAL_NOP			\
			".pushsection __jump_table,  \"aw\" \n\t"\
			_ASM_PTR "1b, %l[" #label "], %c0 \n\t" \
			".popsection \n\t"			\
			: :  "i" (key) :  : label);		\
	} while (0)

#endif /* __KERNEL__ */

#ifdef CONFIG_X86_64
typedef u64 jump_label_t;
#else
typedef u32 jump_label_t;
#endif

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif
