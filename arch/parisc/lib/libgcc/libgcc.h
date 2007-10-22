#ifndef _PA_LIBGCC_H_
#define _PA_LIBGCC_H_

#include <linux/types.h>
#include <linux/module.h>

/* Cribbed from klibc/libgcc/ */
u64 __ashldi3(u64 v, int cnt);
u64 __ashrdi3(u64 v, int cnt);

u32 __clzsi2(u32 v);

s64 __divdi3(s64 num, s64 den);
s32 __divsi3(s32 num, s32 den);

u64 __lshrdi3(u64 v, int cnt);

s64 __moddi3(s64 num, s64 den);
s32 __modsi3(s32 num, s32 den);

u64 __udivdi3(u64 num, u64 den);
u32 __udivsi3(u32 num, u32 den);

u64 __udivmoddi4(u64 num, u64 den, u64 * rem_p);
u32 __udivmodsi4(u32 num, u32 den, u32 * rem_p);

u64 __umulsidi3(u32 u, u32 v);

u64 __umoddi3(u64 num, u64 den);
u32 __umodsi3(u32 num, u32 den);

#endif /*_PA_LIBGCC_H_*/
