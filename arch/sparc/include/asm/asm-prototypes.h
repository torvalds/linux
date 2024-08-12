/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/uaccess.h>

#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <asm/delay.h>
#include <asm/ftrace.h>
#include <asm/oplib.h>
#include <asm/pgtable.h>
#include <asm/trap_block.h>
#include <asm/xor.h>

void *__memscan_zero(void *, size_t);
void *__memscan_generic(void *, int, size_t);
void *__bzero(void *, size_t);
void VISenter(void); /* Dummy prototype to supress warning */
#undef memcpy
#undef memset
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
typedef int TItype __attribute__((mode(TI)));
TItype __multi3(TItype a, TItype b);

s64 __ashldi3(s64, int);
s64 __lshrdi3(s64, int);
s64 __ashrdi3(s64, int);
