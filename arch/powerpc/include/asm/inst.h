/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INST_H
#define _ASM_POWERPC_INST_H

#include <asm/ppc-opcode.h>

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

static inline u64 ppc_inst_as_u64(struct ppc_inst x)
{
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	return (u64)ppc_inst_suffix(x) << 32 | ppc_inst_val(x);
#else
	return (u64)ppc_inst_val(x) << 32 | ppc_inst_suffix(x);
#endif
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

int probe_user_read_inst(struct ppc_inst *inst,
			 struct ppc_inst __user *nip);

int probe_kernel_read_inst(struct ppc_inst *inst,
			   struct ppc_inst *src);

#endif /* _ASM_POWERPC_INST_H */
