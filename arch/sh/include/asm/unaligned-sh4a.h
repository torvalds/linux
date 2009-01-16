#ifndef __ASM_SH_UNALIGNED_SH4A_H
#define __ASM_SH_UNALIGNED_SH4A_H

/*
 * SH-4A has support for unaligned 32-bit loads, and 32-bit loads only.
 * Support for 16 and 64-bit accesses are done through shifting and
 * masking relative to the endianness. Unaligned stores are not supported
 * by the instruction encoding, so these continue to use the packed
 * struct.
 *
 * The same note as with the movli.l/movco.l pair applies here, as long
 * as the load is gauranteed to be inlined, nothing else will hook in to
 * r0 and we get the return value for free.
 *
 * NOTE: Due to the fact we require r0 encoding, care should be taken to
 * avoid mixing these heavily with other r0 consumers, such as the atomic
 * ops. Failure to adhere to this can result in the compiler running out
 * of spill registers and blowing up when building at low optimization
 * levels. See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=34777.
 */
#include <linux/types.h>
#include <asm/byteorder.h>

static __always_inline u32 __get_unaligned_cpu32(const u8 *p)
{
	unsigned long unaligned;

	__asm__ __volatile__ (
		"movua.l	@%1, %0\n\t"
		 : "=z" (unaligned)
		 : "r" (p)
	);

	return unaligned;
}

struct __una_u16 { u16 x __attribute__((packed)); };
struct __una_u32 { u32 x __attribute__((packed)); };
struct __una_u64 { u64 x __attribute__((packed)); };

static inline u16 __get_unaligned_cpu16(const u8 *p)
{
#ifdef __LITTLE_ENDIAN
	return __get_unaligned_cpu32(p) & 0xffff;
#else
	return __get_unaligned_cpu32(p) >> 16;
#endif
}

/*
 * Even though movua.l supports auto-increment on the read side, it can
 * only store to r0 due to instruction encoding constraints, so just let
 * the compiler sort it out on its own.
 */
static inline u64 __get_unaligned_cpu64(const u8 *p)
{
#ifdef __LITTLE_ENDIAN
	return (u64)__get_unaligned_cpu32(p + 4) << 32 |
		    __get_unaligned_cpu32(p);
#else
	return (u64)__get_unaligned_cpu32(p) << 32 |
		    __get_unaligned_cpu32(p + 4);
#endif
}

static inline u16 get_unaligned_le16(const void *p)
{
	return le16_to_cpu(__get_unaligned_cpu16(p));
}

static inline u32 get_unaligned_le32(const void *p)
{
	return le32_to_cpu(__get_unaligned_cpu32(p));
}

static inline u64 get_unaligned_le64(const void *p)
{
	return le64_to_cpu(__get_unaligned_cpu64(p));
}

static inline u16 get_unaligned_be16(const void *p)
{
	return be16_to_cpu(__get_unaligned_cpu16(p));
}

static inline u32 get_unaligned_be32(const void *p)
{
	return be32_to_cpu(__get_unaligned_cpu32(p));
}

static inline u64 get_unaligned_be64(const void *p)
{
	return be64_to_cpu(__get_unaligned_cpu64(p));
}

static inline void __put_le16_noalign(u8 *p, u16 val)
{
	*p++ = val;
	*p++ = val >> 8;
}

static inline void __put_le32_noalign(u8 *p, u32 val)
{
	__put_le16_noalign(p, val);
	__put_le16_noalign(p + 2, val >> 16);
}

static inline void __put_le64_noalign(u8 *p, u64 val)
{
	__put_le32_noalign(p, val);
	__put_le32_noalign(p + 4, val >> 32);
}

static inline void __put_be16_noalign(u8 *p, u16 val)
{
	*p++ = val >> 8;
	*p++ = val;
}

static inline void __put_be32_noalign(u8 *p, u32 val)
{
	__put_be16_noalign(p, val >> 16);
	__put_be16_noalign(p + 2, val);
}

static inline void __put_be64_noalign(u8 *p, u64 val)
{
	__put_be32_noalign(p, val >> 32);
	__put_be32_noalign(p + 4, val);
}

static inline void put_unaligned_le16(u16 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	((struct __una_u16 *)p)->x = val;
#else
	__put_le16_noalign(p, val);
#endif
}

static inline void put_unaligned_le32(u32 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	((struct __una_u32 *)p)->x = val;
#else
	__put_le32_noalign(p, val);
#endif
}

static inline void put_unaligned_le64(u64 val, void *p)
{
#ifdef __LITTLE_ENDIAN
	((struct __una_u64 *)p)->x = val;
#else
	__put_le64_noalign(p, val);
#endif
}

static inline void put_unaligned_be16(u16 val, void *p)
{
#ifdef __BIG_ENDIAN
	((struct __una_u16 *)p)->x = val;
#else
	__put_be16_noalign(p, val);
#endif
}

static inline void put_unaligned_be32(u32 val, void *p)
{
#ifdef __BIG_ENDIAN
	((struct __una_u32 *)p)->x = val;
#else
	__put_be32_noalign(p, val);
#endif
}

static inline void put_unaligned_be64(u64 val, void *p)
{
#ifdef __BIG_ENDIAN
	((struct __una_u64 *)p)->x = val;
#else
	__put_be64_noalign(p, val);
#endif
}

/*
 * Cause a link-time error if we try an unaligned access other than
 * 1,2,4 or 8 bytes long
 */
extern void __bad_unaligned_access_size(void);

#define __get_unaligned_le(ptr) ((__force typeof(*(ptr)))({			\
	__builtin_choose_expr(sizeof(*(ptr)) == 1, *(ptr),			\
	__builtin_choose_expr(sizeof(*(ptr)) == 2, get_unaligned_le16((ptr)),	\
	__builtin_choose_expr(sizeof(*(ptr)) == 4, get_unaligned_le32((ptr)),	\
	__builtin_choose_expr(sizeof(*(ptr)) == 8, get_unaligned_le64((ptr)),	\
	__bad_unaligned_access_size()))));					\
	}))

#define __get_unaligned_be(ptr) ((__force typeof(*(ptr)))({			\
	__builtin_choose_expr(sizeof(*(ptr)) == 1, *(ptr),			\
	__builtin_choose_expr(sizeof(*(ptr)) == 2, get_unaligned_be16((ptr)),	\
	__builtin_choose_expr(sizeof(*(ptr)) == 4, get_unaligned_be32((ptr)),	\
	__builtin_choose_expr(sizeof(*(ptr)) == 8, get_unaligned_be64((ptr)),	\
	__bad_unaligned_access_size()))));					\
	}))

#define __put_unaligned_le(val, ptr) ({					\
	void *__gu_p = (ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		*(u8 *)__gu_p = (__force u8)(val);			\
		break;							\
	case 2:								\
		put_unaligned_le16((__force u16)(val), __gu_p);		\
		break;							\
	case 4:								\
		put_unaligned_le32((__force u32)(val), __gu_p);		\
		break;							\
	case 8:								\
		put_unaligned_le64((__force u64)(val), __gu_p);		\
		break;							\
	default:							\
		__bad_unaligned_access_size();				\
		break;							\
	}								\
	(void)0; })

#define __put_unaligned_be(val, ptr) ({					\
	void *__gu_p = (ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		*(u8 *)__gu_p = (__force u8)(val);			\
		break;							\
	case 2:								\
		put_unaligned_be16((__force u16)(val), __gu_p);		\
		break;							\
	case 4:								\
		put_unaligned_be32((__force u32)(val), __gu_p);		\
		break;							\
	case 8:								\
		put_unaligned_be64((__force u64)(val), __gu_p);		\
		break;							\
	default:							\
		__bad_unaligned_access_size();				\
		break;							\
	}								\
	(void)0; })

#ifdef __LITTLE_ENDIAN
# define get_unaligned __get_unaligned_le
# define put_unaligned __put_unaligned_le
#else
# define get_unaligned __get_unaligned_be
# define put_unaligned __put_unaligned_be
#endif

#endif /* __ASM_SH_UNALIGNED_SH4A_H */
