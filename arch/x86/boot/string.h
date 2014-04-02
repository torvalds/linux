#ifndef BOOT_STRING_H
#define BOOT_STRING_H

/* Undef any of these macros coming from string_32.h. */
#undef memcpy
#undef memset
#undef memcmp

void *memcpy(void *dst, const void *src, size_t len);
void *memset(void *dst, int c, size_t len);
int memcmp(const void *s1, const void *s2, size_t len);

/*
 * Access builtin version by default. If one needs to use optimized version,
 * do "undef memcpy" in .c file and link against right string.c
 */
#define memcpy(d,s,l) __builtin_memcpy(d,s,l)
#define memset(d,c,l) __builtin_memset(d,c,l)
#define memcmp	__builtin_memcmp

#endif /* BOOT_STRING_H */
