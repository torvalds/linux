/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <asm/xor.h>
#include <asm/checksum.h>
#include <asm/trap_block.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/ftrace.h>
#include <asm/cacheflush.h>
#include <asm/oplib.h>
#include <linux/atomic.h>

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
