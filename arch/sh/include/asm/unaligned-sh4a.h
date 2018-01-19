/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_UNALIGNED_SH4A_H
#define __ASM_SH_UNALIGNED_SH4A_H

/*
 * SH-4A has support for unaligned 32-bit loads, and 32-bit loads only.
 * Support for 64-bit accesses are done through shifting and masking
 * relative to the endianness. Unaligned stores are not supported by the
 * instruction encoding, so these continue to use the packed
 * struct.
 *
 * The same note as with the movli.l/movco.l pair applies here, as long
 * as the load is guaranteed to be inlined, nothing else will hook in to
 * r0 and we get the return value for free.
 *
 * NOTE: Due to the fact we require r0 encoding, care should be taken to
 * avoid mixing these heavily with other r0 consumers, such as the atomic
 * ops. Failure to adhere to this can result in the compiler running out
 * of spill registers and blowing up when building at low optimization
 * levels. See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=34777.
 */
#include <linux/unaligned/packed_struct.h>
#include <linux/types.h>
#include <asm/byteorder.h>

static inline u16 sh4a_get_unaligned_cpu16(const u8 *p)
{
#ifdef __LITTLE_ENDIAN
	return p[0] | p[1] << 8;
#else
	return p[0] << 8 | p[1];
#endif
}

static __always_inline u32 sh4a_get_unaligned_cpu32(const u8 *p)
{
	unsigned long unaligned;

	__asm__ __volatile__ (
		"movua.l	@%1, %0\n\t"
		 : "=z" (unaligned)
		 : "r" (p)
	);

	return unaligned;
}

/*
 * Even though movua.l supports auto-increment on the read side, it can
 * only store to r0 due to instruction encoding constraints, so just let
 * the compiler sort it out on its own.
 */
static inline u64 sh4a_get_unaligned_cpu64(const u8 *p)
{
#ifdef __LITTLE_ENDIAN
	return (u64)sh4a_get_unaligned_cpu32(p + 4) << 32 |
		    sh4a_get_unaligned_cpu32(p);
#else
	return (u64)sh4a_get_unaligned_cpu32(p) << 32 |
		    sh4a_get_unaligned_cpu32(p + 4);
#endif
}

static inline u16 get_unaligned_le16(const void *p)
{
	return le16_to_cpu(sh4a_get_unaligned_cpu16(p));
}

static inline u32 get_unaligned_le32(const void *p)
{
	return le32_to_cpu(sh4a_get_unaligned_cpu32(p));
}

static inline u64 get_unaligned_le64(const void *p)
{
	return le64_to_cpu(sh4a_get_unaligned_cpu64(p));
}

static inline u16 get_unaligned_be16(const void *p)
{
	return be16_to_cpu(sh4a_get_unaligned_cpu16(p));
}

static inline u32 get_unaligned_be32(const void *p)
{
	return be32_to_cpu(sh4a_get_unaligned_cpu32(p));
}

static inline u64 get_unaligned_be64(const void *p)
{
	return be64_to_cpu(sh4a_get_unaligned_cpu64(p));
}

static inline void nonnative_put_le16(u16 val, u8 *p)
{
	*p++ = val;
	*p++ = val >> 8;
}

static inline void nonnative_put_le32(u32 val, u8 *p)
{
	nonnative_put_le16(val, p);
	nonnative_put_le16(val >> 16, p + 2);
}

static inline void nonnative_put_le64(u64 val, u8 *p)
{
	nonnative_put_le32(val, p);
	nonnative_put_le32(val >> 32, p + 4);
}

static inline void nonnative_put_be16(u16 val, u8 *p)
{
	*p++ = val >> 8;
	*p++ = val;
}

static inline void nonnative_put_be32(u32 val, u8 *p)
{
	nonnative_put_be16(val >> 16, p);
	nonnative_put_be16(val, p + 2);
}

static inline void nonnative_put_be64(u64 val, u8 *p)
{
	nonnative_put_be32(val >> 32, p);
	nonnative_put_be32(val, p + 4);
}

static inline void put_unaligned_le16(u16 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	__put_unaligned_cpu16(val, p);
#else
	nonnative_put_le16(val, p);
#endif
}

static inline void put_unaligned_le32(u32 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	__put_unaligned_cpu32(val, p);
#else
	nonnative_put_le32(val, p);
#endif
}

static inline void put_unaligned_le64(u64 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	__put_unaligned_cpu64(val, p);
#else
	nonnative_put_le64(val, p);
#endif
}

static inline void put_unaligned_be16(u16 val, void *p)
{
#ifdef __BIG_ENDIAN
	__put_unaligned_cpu16(val, p);
#else
	nonnative_put_be16(val, p);
#endif
}

static inline void put_unaligned_be32(u32 val, void *p)
{
#ifdef __BIG_ENDIAN
	__put_unaligned_cpu32(val, p);
#else
	nonnative_put_be32(val, p);
#endif
}

static inline void put_unaligned_be64(u64 val, void *p)
{
#ifdef __BIG_ENDIAN
	__put_unaligned_cpu64(val, p);
#else
	nonnative_put_be64(val, p);
#endif
}

/*
 * While it's a bit non-obvious, even though the generic le/be wrappers
 * use the __get/put_xxx prefixing, they actually wrap in to the
 * non-prefixed get/put_xxx variants as provided above.
 */
#include <linux/unaligned/generic.h>

#ifdef __LITTLE_ENDIAN
# define get_unaligned __get_unaligned_le
# define put_unaligned __put_unaligned_le
#else
# define get_unaligned __get_unaligned_be
# define put_unaligned __put_unaligned_be
#endif

#endif /* __ASM_SH_UNALIGNED_SH4A_H */
