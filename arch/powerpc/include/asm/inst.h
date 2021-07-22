/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

#include <asm/ppc-opcode.h>

#ifdef CONFIG_PPC64

#define ___get_user_instr(gu_op, dest, ptr)				\
({									\
	long __gui_ret;							\
	u32 __user *__gui_ptr = (u32 __user *)ptr;			\
	struct ppc_inst __gui_inst;					\
	unsigned int __prefix, __suffix;				\
									\
	__chk_user_ptr(ptr);						\
	__gui_ret = gu_op(__prefix, __gui_ptr);				\
	if (__gui_ret == 0) {						\
		if ((__prefix >> 26) == OP_PREFIX) {			\
			__gui_ret = gu_op(__suffix, __gui_ptr + 1);	\
			__gui_inst = ppc_inst_prefix(__prefix, __suffix); \
		} else {						\
			__gui_inst = ppc_inst(__prefix);		\
		}							\
		if (__gui_ret == 0)					\
			(dest) = __gui_inst;				\
	}								\
	__gui_ret;							\
})
#else /* !CONFIG_PPC64 */
#define ___get_user_instr(gu_op, dest, ptr)				\
({									\
	__chk_user_ptr(ptr);						\
	gu_op((dest).val, (u32 __user *)(ptr));				\
})
#endif /* CONFIG_PPC64 */

#define get_user_instr(x, ptr) ___get_user_instr(get_user, x, ptr)

#define __get_user_instr(x, ptr) ___get_user_instr(__get_user, x, ptr)

/*
 * Instruction data type for POWER
 */

struct ppc_inst {
	u32 val;
#ifdef CONFIG_PPC64
	u32 suffix;
#endif
} __packed;

static inline u32 ppc_inst_val(struct ppc_inst x)
{
	return x.val;
}

static inline int ppc_inst_primary_opcode(struct ppc_inst x)
{
	return ppc_inst_val(x) >> 26;
}

#define ppc_inst(x) ((struct ppc_inst){ .val = (x) })

#ifdef CONFIG_PPC64
#define ppc_inst_prefix(x, y) ((struct ppc_inst){ .val = (x), .suffix = (y) })

static inline u32 ppc_inst_suffix(struct ppc_inst x)
{
	return x.suffix;
}

#else
#define ppc_inst_prefix(x, y) ppc_inst(x)

static inline u32 ppc_inst_suffix(struct ppc_inst x)
{
	return 0;
}

#endif /* CONFIG_PPC64 */

static inline struct ppc_inst ppc_inst_read(const u32 *ptr)
{
	if (IS_ENABLED(CONFIG_PPC64) && (*ptr >> 26) == OP_PREFIX)
		return ppc_inst_prefix(*ptr, *(ptr + 1));
	else
		return ppc_inst(*ptr);
}

static inline bool ppc_inst_prefixed(struct ppc_inst x)
{
	return IS_ENABLED(CONFIG_PPC64) && ppc_inst_primary_opcode(x) == OP_PREFIX;
}

static inline struct ppc_inst ppc_inst_swab(struct ppc_inst x)
{
	return ppc_inst_prefix(swab32(ppc_inst_val(x)), swab32(ppc_inst_suffix(x)));
}

static inline bool ppc_inst_equal(struct ppc_inst x, struct ppc_inst y)
{
	if (ppc_inst_val(x) != ppc_inst_val(y))
		return false;
	if (!ppc_inst_prefixed(x))
		return true;
	return ppc_inst_suffix(x) == ppc_inst_suffix(y);
}

static inline int ppc_inst_len(struct ppc_inst x)
{
	return ppc_inst_prefixed(x) ? 8 : 4;
}

/*
 * Return the address of the next instruction, if the instruction @value was
 * located at @location.
 */
static inline u32 *ppc_inst_next(u32 *location, u32 *value)
{
	struct ppc_inst tmp;

	tmp = ppc_inst_read(value);

	return (void *)location + ppc_inst_len(tmp);
}

static inline unsigned long ppc_inst_as_ulong(struct ppc_inst x)
{
	if (IS_ENABLED(CONFIG_PPC32))
		return ppc_inst_val(x);
	else if (IS_ENABLED(CONFIG_CPU_LITTLE_ENDIAN))
		return (u64)ppc_inst_suffix(x) << 32 | ppc_inst_val(x);
	else
		return (u64)ppc_inst_val(x) << 32 | ppc_inst_suffix(x);
}

#define PPC_INST_STR_LEN sizeof("00000000 00000000")

static inline char *__ppc_inst_as_str(char str[PPC_INST_STR_LEN], struct ppc_inst x)
{
	if (ppc_inst_prefixed(x))
		sprintf(str, "%08x %08x", ppc_inst_val(x), ppc_inst_suffix(x));
	else
		sprintf(str, "%08x", ppc_inst_val(x));

	return str;
}

#define ppc_inst_as_str(x)		\
({					\
	char __str[PPC_INST_STR_LEN];	\
	__ppc_inst_as_str(__str, x);	\
	__str;				\
})

int copy_inst_from_kernel_nofault(struct ppc_inst *inst, u32 *src);

#endif /* _ASM_POWERPC_INST_H */
