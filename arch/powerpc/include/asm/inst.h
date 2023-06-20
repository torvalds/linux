/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

#include <asm/ppc-opcode.h>
#include <asm/reg.h>
#include <asm/disassemble.h>
#include <asm/uaccess.h>

#define ___get_user_instr(gu_op, dest, ptr)				\
({									\
	long __gui_ret;							\
	u32 __user *__gui_ptr = (u32 __user *)ptr;			\
	ppc_inst_t __gui_inst;						\
	unsigned int __prefix, __suffix;				\
									\
	__chk_user_ptr(ptr);						\
	__gui_ret = gu_op(__prefix, __gui_ptr);				\
	if (__gui_ret == 0) {						\
		if (IS_ENABLED(CONFIG_PPC64) && (__prefix >> 26) == OP_PREFIX) { \
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

#define get_user_instr(x, ptr) ___get_user_instr(get_user, x, ptr)

#define __get_user_instr(x, ptr) ___get_user_instr(__get_user, x, ptr)

/*
 * Instruction data type for POWER
 */

#if defined(CONFIG_PPC64) || defined(__CHECKER__)
static inline u32 ppc_inst_val(ppc_inst_t x)
{
	return x.val;
}

#define ppc_inst(x) ((ppc_inst_t){ .val = (x) })

#else
static inline u32 ppc_inst_val(ppc_inst_t x)
{
	return x;
}
#define ppc_inst(x) (x)
#endif

static inline int ppc_inst_primary_opcode(ppc_inst_t x)
{
	return ppc_inst_val(x) >> 26;
}

#ifdef CONFIG_PPC64
#define ppc_inst_prefix(x, y) ((ppc_inst_t){ .val = (x), .suffix = (y) })

static inline u32 ppc_inst_suffix(ppc_inst_t x)
{
	return x.suffix;
}

#else
#define ppc_inst_prefix(x, y) ((void)y, ppc_inst(x))

static inline u32 ppc_inst_suffix(ppc_inst_t x)
{
	return 0;
}

#endif /* CONFIG_PPC64 */

static inline ppc_inst_t ppc_inst_read(const u32 *ptr)
{
	if (IS_ENABLED(CONFIG_PPC64) && (*ptr >> 26) == OP_PREFIX)
		return ppc_inst_prefix(*ptr, *(ptr + 1));
	else
		return ppc_inst(*ptr);
}

static inline bool ppc_inst_prefixed(ppc_inst_t x)
{
	return IS_ENABLED(CONFIG_PPC64) && ppc_inst_primary_opcode(x) == OP_PREFIX;
}

static inline ppc_inst_t ppc_inst_swab(ppc_inst_t x)
{
	return ppc_inst_prefix(swab32(ppc_inst_val(x)), swab32(ppc_inst_suffix(x)));
}

static inline bool ppc_inst_equal(ppc_inst_t x, ppc_inst_t y)
{
	if (ppc_inst_val(x) != ppc_inst_val(y))
		return false;
	if (!ppc_inst_prefixed(x))
		return true;
	return ppc_inst_suffix(x) == ppc_inst_suffix(y);
}

static inline int ppc_inst_len(ppc_inst_t x)
{
	return ppc_inst_prefixed(x) ? 8 : 4;
}

/*
 * Return the address of the next instruction, if the instruction @value was
 * located at @location.
 */
static inline u32 *ppc_inst_next(u32 *location, u32 *value)
{
	ppc_inst_t tmp;

	tmp = ppc_inst_read(value);

	return (void *)location + ppc_inst_len(tmp);
}

static inline unsigned long ppc_inst_as_ulong(ppc_inst_t x)
{
	if (IS_ENABLED(CONFIG_PPC32))
		return ppc_inst_val(x);
	else if (IS_ENABLED(CONFIG_CPU_LITTLE_ENDIAN))
		return (u64)ppc_inst_suffix(x) << 32 | ppc_inst_val(x);
	else
		return (u64)ppc_inst_val(x) << 32 | ppc_inst_suffix(x);
}

static inline void ppc_inst_write(u32 *ptr, ppc_inst_t x)
{
	if (!ppc_inst_prefixed(x))
		*ptr = ppc_inst_val(x);
	else
		*(u64 *)ptr = ppc_inst_as_ulong(x);
}

#define PPC_INST_STR_LEN sizeof("00000000 00000000")

static inline char *__ppc_inst_as_str(char str[PPC_INST_STR_LEN], ppc_inst_t x)
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

static inline int __copy_inst_from_kernel_nofault(ppc_inst_t *inst, u32 *src)
{
	unsigned int val, suffix;

/* See https://github.com/ClangBuiltLinux/linux/issues/1521 */
#if defined(CONFIG_CC_IS_CLANG) && CONFIG_CLANG_VERSION < 140000
	val = suffix = 0;
#endif
	__get_kernel_nofault(&val, src, u32, Efault);
	if (IS_ENABLED(CONFIG_PPC64) && get_op(val) == OP_PREFIX) {
		__get_kernel_nofault(&suffix, src + 1, u32, Efault);
		*inst = ppc_inst_prefix(val, suffix);
	} else {
		*inst = ppc_inst(val);
	}
	return 0;
Efault:
	return -EFAULT;
}

static inline int copy_inst_from_kernel_nofault(ppc_inst_t *inst, u32 *src)
{
	if (unlikely(!is_kernel_addr((unsigned long)src)))
		return -ERANGE;

	return __copy_inst_from_kernel_nofault(inst, src);
}

#endif /* _ASM_POWERPC_INST_H */
