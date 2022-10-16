// SPDX-License-Identifier: GPL-2.0

#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/string.h>

#include "efistub.h"

#ifdef CONFIG_KASAN
#undef memcpy
#undef memmove
#undef memset
void *__memcpy(void *__dest, const void *__src, size_t __n) __alias(memcpy);
void *__memmove(void *__dest, const void *__src, size_t count) __alias(memmove);
void *__memset(void *s, int c, size_t count) __alias(memset);
#endif

void *memcpy(void *dst, const void *src, size_t len)
{
	efi_bs_call(copy_mem, dst, src, len);
	return dst;
}

extern void *memmove(void *dst, const void *src, size_t len) __alias(memcpy);

void *memset(void *dst, int c, size_t len)
{
	efi_bs_call(set_mem, dst, len, c & U8_MAX);
	return dst;
}
