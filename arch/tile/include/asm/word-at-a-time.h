#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <asm/byteorder.h>

struct word_at_a_time { /* unused */ };
#define WORD_AT_A_TIME_CONSTANTS {}

/* Generate 0x01 byte values for zero bytes using a SIMD instruction. */
static inline unsigned long has_zero(unsigned long val, unsigned long *data,
				     const struct word_at_a_time *c)
{
#ifdef __tilegx__
	unsigned long mask = __insn_v1cmpeqi(val, 0);
#else /* tilepro */
	unsigned long mask = __insn_seqib(val, 0);
#endif
	*data = mask;
	return mask;
}

/* These operations are both nops. */
#define prep_zero_mask(val, data, c) (data)
#define create_zero_mask(data) (data)

/* And this operation just depends on endianness. */
static inline long find_zero(unsigned long mask)
{
#ifdef __BIG_ENDIAN
	return __builtin_clzl(mask) >> 3;
#else
	return __builtin_ctzl(mask) >> 3;
#endif
}

#ifdef __BIG_ENDIAN
#define zero_bytemask(mask) (~1ul << (63 - __builtin_clzl(mask)))
#else
#define zero_bytemask(mask) ((2ul << __builtin_ctzl(mask)) - 1)
#endif

#endif /* _ASM_WORD_AT_A_TIME_H */
