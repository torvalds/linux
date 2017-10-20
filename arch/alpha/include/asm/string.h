#ifndef __ALPHA_STRING_H__
#define __ALPHA_STRING_H__

#ifdef __KERNEL__

/*
 * GCC of any recent vintage doesn't do stupid things with bcopy.
 * EGCS 1.1 knows all about expanding memcpy inline, others don't.
 *
 * Similarly for a memset with data = 0.
 */

#define __HAVE_ARCH_MEMCPY
extern void * memcpy(void *, const void *, size_t);
#define __HAVE_ARCH_MEMMOVE
extern void * memmove(void *, const void *, size_t);

/* For backward compatibility with modules.  Unused otherwise.  */
extern void * __memcpy(void *, const void *, size_t);

#define memcpy __builtin_memcpy

#define __HAVE_ARCH_MEMSET
extern void * __constant_c_memset(void *, unsigned long, size_t);
extern void * ___memset(void *, int, size_t);
extern void * __memset(void *, int, size_t);
extern void * memset(void *, int, size_t);

/* For gcc 3.x, we cannot have the inline function named "memset" because
   the __builtin_memset will attempt to resolve to the inline as well,
   leading to a "sorry" about unimplemented recursive inlining.  */
extern inline void *__memset(void *s, int c, size_t n)
{
	if (__builtin_constant_p(c)) {
		if (__builtin_constant_p(n)) {
			return __builtin_memset(s, c, n);
		} else {
			unsigned long c8 = (c & 0xff) * 0x0101010101010101UL;
			return __constant_c_memset(s, c8, n);
		}
	}
	return ___memset(s, c, n);
}

#define memset __memset

#define __HAVE_ARCH_STRCPY
extern char * strcpy(char *,const char *);
#define __HAVE_ARCH_STRNCPY
extern char * strncpy(char *, const char *, size_t);
#define __HAVE_ARCH_STRCAT
extern char * strcat(char *, const char *);
#define __HAVE_ARCH_STRNCAT
extern char * strncat(char *, const char *, size_t);
#define __HAVE_ARCH_STRCHR
extern char * strchr(const char *,int);
#define __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int);
#define __HAVE_ARCH_STRLEN
extern size_t strlen(const char *);
#define __HAVE_ARCH_MEMCHR
extern void * memchr(const void *, int, size_t);

/* The following routine is like memset except that it writes 16-bit
   aligned values.  The DEST and COUNT parameters must be even for 
   correct operation.  */

#define __HAVE_ARCH_MEMSET16
extern void * __memset16(void *dest, unsigned short, size_t count);
static inline void *memset16(uint16_t *p, uint16_t v, size_t n)
{
	if (__builtin_constant_p(v))
		return __constant_c_memset(p, 0x0001000100010001UL * v, n * 2);
	return __memset16(p, v, n * 2);
}

#endif /* __KERNEL__ */

#endif /* __ALPHA_STRING_H__ */
