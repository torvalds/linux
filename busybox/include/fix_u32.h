/* vi: set sw=4 ts=4: */
/*
 * This header makes it easier to include kernel headers
 * which use u32 and such.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#ifndef FIX_U32_H
#define FIX_U32_H 1

/* Try hard to pull in u32 types and such.
 * Otherwise, #include "fix_u32.h" + #include <linux/foo.h>
 * may end up typedef'ing bb_hack_u32 inside foo.h,
 * and repeated typedefs aren't allowed in C/C++.
 */
#include <asm/types.h>
#include <linux/types.h>

/* In case above includes still failed to provide the types,
 * provide them ourself
 */
#undef __u64
#undef u64
#undef u32
#undef u16
#undef u8
#undef __s64
#undef s64
#undef s32
#undef s16
#undef s8

#define __u64 bb_hack___u64
#define u64   bb_hack_u64
#define u32   bb_hack_u32
#define u16   bb_hack_u16
#define u8    bb_hack_u8
#define __s64 bb_hack___s64
#define s64   bb_hack_s64
#define s32   bb_hack_s32
#define s16   bb_hack_s16
#define s8    bb_hack_s8

typedef uint64_t __u64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t __s64;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

#endif
