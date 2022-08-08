/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

#include <asm/ppc-opcode.h>

#ifdef CONFIG_PPC64

#define ___get_user_instr(gu_op, dest, ptr)				\
({									\
	long __gui_ret = 0;						\
	unsigned long __gui_ptr = (unsigned long)ptr;			\
	struct ppc_inst __gui_inst;					\
	unsigned int __prefix, __suffix;				\
	__gui_ret = gu_op(__prefix, (unsigned int __user *)__gui_ptr);	\
	if (__gui_ret == 0) {						\
		if ((__prefix >> 26) == OP_PREFIX) {			\
			__gui_ret = gu_op(__suffix,			\
				(unsigned int __user *)__gui_ptr + 1);	\
			__gui_inst = ppc_inst_prefix(__prefix,		\
						     __suffix);		\
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
	gu_op((dest).val, (u32 __user *)(ptr))
#endif /* CONFIG_PPC64 */

#define get_user_instr(x, ptr) \
	___get_user_instr(get_user, x, ptr)

#define __get_user_instr(x, ptr) \
	___get_user_instr(__get_user, x, ptr)

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

#ifdef CONFIG_PPC64
#define ppc_inst(x) ((struct ppc_inst){ .val = (x), .suffix = 0xff })

#define ppc_inst_prefix(x, y) ((struct ppc_inst){ .val = (x), .suffix = (y) })

static inline u32 ppc_inst_suffix(struct ppc_inst x)
{
	return x.suffix;
}

static inline bool ppc_inst_prefixed(struct ppc_inst x)
{
	return (ppc_inst_primary_opcode(x) == 1) && ppc_inst_suffix(x) != 0xff;
}

static inline struct ppc_inst ppc_inst_swab(struct ppc_inst x)
{
	return ppc_inst_prefix(swab32(ppc_inst_val(x)),
			       swab32(ppc_inst_suffix(x)));
}

static inline struct ppc_inst ppc_inst_read(const struct ppc_inst *ptr)
{
	u32 val, suffix;

	val = *(u32 *)ptr;
	if ((val >> 26) == OP_PREFIX) {
		suffix = *((u32 *)ptr + 1);
		return ppc_inst_prefix(val, suffix);
	} else {
		return ppc_inst(val);
	}
}

static inline bool ppc_inst_equal(struct ppc_inst x, struct ppc_inst y)
{
	return *(u64 *)&x == *(u64 *)&y;
}

#else

#define ppc_inst(x) ((struct ppc_inst){ .val = x })

#define ppc_inst_prefix(x, y) ppc_inst(x)

static inline bool ppc_inst_prefixed(struct ppc_inst x)
{
	return false;
}

static inline u32 ppc_inst_suffix(struct ppc_inst x)
{
	return 0;
}

static inline struct ppc_inst ppc_inst_swab(struct ppc_inst x)
{
	return ppc_inst(swab32(ppc_inst_val(x)));
}

static inline struct ppc_inst ppc_inst_read(const struct ppc_inst *ptr)
{
	return *ptr;
}

static inline bool ppc_inst_equal(struct ppc_inst x, struct ppc_inst y)
{
	return ppc_inst_val(x) == ppc_inst_val(y);
}

#endif /* CONFIG_PPC64 */

static inline int ppc_inst_len(struct ppc_inst x)
{
	return ppc_inst_prefixed(x) ? 8 : 4;
}

/*
 * Return the address of the next instruction, if the instruction @value was
 * located at @location.
 */
static inline struct ppc_inst *ppc_inst_next(void *location, struct ppc_inst *value)
{
	struct ppc_inst tmp;

	tmp = ppc_inst_read(value);

	return location + ppc_inst_len(tmp);
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

int copy_inst_from_kernel_nofault(struct ppc_inst *inst, struct ppc_inst *src);

#endif /* _ASM_POWERPC_INST_H */
