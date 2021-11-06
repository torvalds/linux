/*
 * stdlib support routines for self-contained images.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

/*
 * bcmstdlib.c file should be used only to construct an OSL or alone without any OSL
 * It should not be used with any orbitarary OSL's as there could be a conflict
 * with some of the routines defined here.
 */

/*
 * Define BCMSTDLIB_WIN32_APP if this is a Win32 Application compile
 */
#if defined(_WIN32) && !defined(NDIS) && !defined(EFI)
#define BCMSTDLIB_WIN32_APP 1
#endif /* _WIN32 && !NDIS */

/*
 * Define BCMSTDLIB_SNPRINTF_ONLY if we only want snprintf & vsnprintf implementations
 */
#if defined(_WIN32) && !defined(EFI)
#define BCMSTDLIB_SNPRINTF_ONLY 1
#endif /* _WIN32 || !EFI */

#include <typedefs.h>
#ifdef BCMSTDLIB_WIN32_APP
/* for size_t definition */
#include <stddef.h>
#endif
#include <stdarg.h>
#ifndef BCMSTDLIB_WIN32_APP
#include <bcmutils.h>
#endif
#include <bcmstdlib.h>

/* Don't use compiler builtins for stdlib APIs within the implementation of the stdlib itself. */
#if defined(BCM_FORTIFY_SOURCE) || defined(BCM_STDLIB_USE_BUILTINS)
#undef memcpy
#undef memmove
#undef memset
#undef strncpy
#undef snprintf
#undef vsnprintf
#endif	/* BCM_FORTIFY_SOURCE || BCM_STDLIB_USE_BUILTINS */

#ifdef HND_PRINTF_THREAD_SAFE
#include <osl.h>
#include <osl_ext.h>
#include <bcmstdlib_ext.h>

/* mutex macros for thread safe */
#define HND_PRINTF_MUTEX_DECL(mutex)		static OSL_EXT_MUTEX_DECL(mutex)
#define HND_PRINTF_MUTEX_CREATE(name, mutex)	osl_ext_mutex_create(name, mutex)
#define HND_PRINTF_MUTEX_DELETE(mutex)		osl_ext_mutex_delete(mutex)
#define HND_PRINTF_MUTEX_ACQUIRE(mutex, msec)	osl_ext_mutex_acquire(mutex, msec)
#define HND_PRINTF_MUTEX_RELEASE(mutex)	osl_ext_mutex_release(mutex)

HND_PRINTF_MUTEX_DECL(printf_mutex);
int in_isr_handler = 0, in_trap_handler = 0, in_fiq_handler = 0;

bool
printf_lock_init(void)
{
	/* create mutex for critical section locking */
	if (HND_PRINTF_MUTEX_CREATE("printf_mutex", &printf_mutex) != OSL_EXT_SUCCESS)
		return FALSE;
	return TRUE;
}

bool
printf_lock_cleanup(void)
{
	/* create mutex for critical section locking */
	if (HND_PRINTF_MUTEX_DELETE(&printf_mutex) != OSL_EXT_SUCCESS)
		return FALSE;
	return TRUE;
}

/* returns TRUE if allowed to proceed, FALSE to discard.
* printf from isr hook or fiq hook is not allowed due to IRQ_MODE and FIQ_MODE stack size
* limitation.
*/
static bool
printf_lock(void)
{

	/* discard for irq or fiq context, we need to keep irq/fiq stack small. */
	if (in_isr_handler || in_fiq_handler)
		return FALSE;

	/* allow printf in trap handler, proceed without mutex. */
	if (in_trap_handler)
		return TRUE;

	/* if not in isr or trap, then go thread-protection with mutex. */
	if (HND_PRINTF_MUTEX_ACQUIRE(&printf_mutex, OSL_EXT_TIME_FOREVER) != OSL_EXT_SUCCESS)
		return FALSE;
	else
		return TRUE;
}

static void
printf_unlock(void)
{
	if (in_isr_handler || in_fiq_handler)
		return;

	if (in_trap_handler)
		return;

	if (HND_PRINTF_MUTEX_RELEASE(&printf_mutex) != OSL_EXT_SUCCESS)
		return;
}

#else
#define printf_lock() (TRUE)
#define printf_unlock()
#endif	/* HND_PRINTF_THREAD_SAFE */

#ifdef BCMSTDLIB_WIN32_APP

/* for a WIN32 application, use _vsnprintf as basis of vsnprintf/snprintf to
 * support full set of format specifications.
 */

int
BCMPOSTTRAPFN(vsnprintf)(char *buf, size_t bufsize, const char *fmt, va_list ap)
{
	int r;

	r = _vsnprintf(buf, bufsize, fmt, ap);

	/* Microsoft _vsnprintf() will not null terminate on overflow,
	 * so null terminate at buffer end on error
	 */
	if (r < 0 && bufsize > 0)
		buf[bufsize - 1] = '\0';

	return r;
}

int
BCMPOSTTRAPFN(snprintf)(char *buf, size_t bufsize, const char *fmt, ...)
{
	va_list	ap;
	int	r;

	va_start(ap, fmt);
	r = vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);

	return r;
}

#else /* BCMSTDLIB_WIN32_APP */

#if !defined(BCMROMOFFLOAD_EXCLUDE_STDLIB_FUNCS)

static const char hex_upper[17] = "0123456789ABCDEF";
static const char hex_lower[17] = "0123456789abcdef";

static int
BCMPOSTTRAPFN(__atolx)(char *buf, char * end, unsigned long num, unsigned long radix, int width,
       const char *digits, int zero_pad)
{
	char buffer[32];
	char *op;
	int retval;

	op = &buffer[0];
	retval = 0;

	do {
		*op++ = digits[num % radix];
		retval++;
		num /= radix;
	} while (num != 0);

	if (width && (width > retval) && zero_pad) {
		width = width - retval;
		while (width) {
			*op++ = '0';
			retval++;
			width--;
		}
	}

	while (op != buffer) {
		op--;
		if (buf <= end)
			*buf = *op;
		buf++;
	}

	return retval;
}

static int
BCMPOSTTRAPFN(__atox)(char *buf, char * end, unsigned int num, unsigned int radix, int width,
       const char *digits, int zero_pad)
{
	char buffer[16];
	char *op;
	int retval;

	op = &buffer[0];
	retval = 0;

	do {
		*op++ = digits[num % radix];
		retval++;
		num /= radix;
	} while (num != 0);

	if (width && (width > retval) && zero_pad) {
		width = width - retval;
		while (width) {
			*op++ = '0';
			retval++;
			width--;
		}
	}

	while (op != buffer) {
		op--;
		if (buf <= end)
			*buf = *op;
		buf++;
	}

	return retval;
}

int
BCMPOSTTRAPFN(vsnprintf)(char *buf, size_t size, const char *fmt, va_list ap)
{
	char *optr;
	char *end;
	const char *iptr, *tmpptr;
	unsigned int x;
	int i;
	int islong;
	int width;
	int width2 = 0;
	int hashash = 0;
	int zero_pad;
	unsigned long ul = 0;
	long int li = 0;

	optr = buf;
	end = buf + size - 1;
	iptr = fmt;

	if (FWSIGN_ENAB()) {
		return 0;
	}

	if (end < buf - 1) {
		end = ((void *) -1);
		size = end - buf + 1;
	}

	while (*iptr) {
		zero_pad = 0;
		if (*iptr != '%') {
			if (optr <= end)
				*optr = *iptr;
			++optr;
			++iptr;
			continue;
		}

		iptr++;

		if (*iptr == '#') {
			hashash = 1;
			iptr++;
		}
		if (*iptr == '-') {
			iptr++;
		}

		if (*iptr == '0') {
			zero_pad = 1;
			iptr++;
		}

		width = 0;
		while (*iptr && bcm_isdigit(*iptr)) {
			width += (*iptr - '0');
			iptr++;
			if (bcm_isdigit(*iptr))
				width *= 10;
		}
		if (*iptr == '.') {
			iptr++;
			width2 = 0;
			while (*iptr && bcm_isdigit(*iptr)) {
				width2 += (*iptr - '0');
				iptr++;
				if (bcm_isdigit(*iptr)) width2 *= 10;
			}
		}

		islong = 0;
		if (*iptr == 'l') {
			islong++;
			iptr++;
			if (*iptr == 'l') {
				++islong;
				++iptr;
			}
		}

		switch (*iptr) {
		case 's':
			tmpptr = va_arg(ap, const char *);
			if (!tmpptr)
				tmpptr = "(null)";
			if ((width == 0) & (width2 == 0)) {
				while (*tmpptr) {
					if (optr <= end)
						*optr = *tmpptr;
					++optr;
					++tmpptr;
				}
				break;
			}
			while (width && *tmpptr) {
				if (optr <= end)
					*optr = *tmpptr;
				++optr;
				++tmpptr;
				width--;
			}
			while (width) {
				if (optr <= end)
					*optr = ' ';
				++optr;
				width--;
			}
			break;
		case 'd':
		case 'i':
			if (!islong) {
				i = va_arg(ap, int);
				if (i < 0) {
					if (optr <= end)
						*optr = '-';
					++optr;
					i = -i;
				}
				optr += __atox(optr, end, i, 10, width, hex_upper, zero_pad);
			} else {
				li = va_arg(ap, long int);
				if (li < 0) {
					if (optr <= end)
						*optr = '-';
					++optr;
					li = -li;
				}
				optr += __atolx(optr, end, li, 10, width, hex_upper, zero_pad);
			}
			break;
		case 'u':
			if (!islong) {
				x = va_arg(ap, unsigned int);
				optr += __atox(optr, end, x, 10, width, hex_upper, zero_pad);
			} else {
				ul = va_arg(ap, unsigned long);
				optr += __atolx(optr, end, ul, 10, width, hex_upper, zero_pad);
			}
			break;
		case 'X':
		case 'x':
			if (hashash) {
				*optr++ = '0';
				*optr++ = *iptr;
			}
			if (!islong) {
				x = va_arg(ap, unsigned int);
				optr += __atox(optr, end, x, 16, width,
						(*iptr == 'X') ? hex_upper : hex_lower, zero_pad);
			} else {
				ul = va_arg(ap, unsigned long);
				optr += __atolx(optr, end, ul, 16, width,
						(*iptr == 'X') ? hex_upper : hex_lower, zero_pad);
			}
			break;
		case 'p':
		case 'P':
			x = va_arg(ap, unsigned int);
			optr += __atox(optr, end, x, 16, 8,
			               (*iptr == 'P') ? hex_upper : hex_lower, zero_pad);
			break;
		case 'c':
			x = va_arg(ap, int);
			if (optr <= end)
				*optr = x & 0xff;
			optr++;
			break;

		default:
			if (optr <= end)
				*optr = *iptr;
			optr++;
			break;
		}
		iptr++;
	}

	if (optr <= end) {
		*optr = '\0';
		return (int)(optr - buf);
	} else {
		*end = '\0';
		return (int)(end - buf);
	}
}

int
BCMPOSTTRAPFN(snprintf)(char *buf, size_t bufsize, const char *fmt, ...)
{
	va_list		ap;
	int			r;

	if (FWSIGN_ENAB()) {
		return 0;
	}

	va_start(ap, fmt);
	r = vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);

	return r;
}
#endif	/* !BCMROMOFFLOAD_EXCLUDE_STDLIB_FUNCS */

#endif /* BCMSTDLIB_WIN32_APP */

#ifndef BCMSTDLIB_SNPRINTF_ONLY
int
BCMPOSTTRAPFN(vsprintf)(char *buf, const char *fmt, va_list ap)
{
	if (FWSIGN_ENAB()) {
		return 0;
	}
	return (vsnprintf(buf, INT_MAX, fmt, ap));
}

int
BCMPOSTTRAPFN(sprintf)(char *buf, const char *fmt, ...)
{
	va_list ap;
	int count;

	if (FWSIGN_ENAB()) {
		return 0;
	}

	va_start(ap, fmt);
	count = vsprintf(buf, fmt, ap);
	va_end(ap);

	return count;
}

#if !defined(EFI) || !defined(COMPILER_INTRINSICS_LIB)
void *
memmove(void *dest, const void *src, size_t n)
{
	/* only use memcpy if there is no overlap. otherwise copy each byte in a safe sequence */
	if (((const char *)src >= (char *)dest + n) || ((const char *)src + n <= (char *)dest)) {
		return memcpy(dest, src, n);
	}

	/* Overlapping copy forward or backward */
	if (src < dest) {
		unsigned char *d = (unsigned char *)dest + (n - 1);
		const unsigned char *s = (const unsigned char *)src + (n - 1);
		while (n) {
			*d-- = *s--;
			n--;
		}
	} else if (src > dest) {
		unsigned char *d = (unsigned char *)dest;
		const unsigned char *s = (const unsigned char *)src;
		while (n) {
			*d++ = *s++;
			n--;
		}
	}

	return dest;
}
#endif /* !EFI || !COMPILER_INTRINSICS_LIB */

#ifndef EFI
int
memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *ss1;
	const unsigned char *ss2;

	ss1 = (const unsigned char *)s1;
	ss2 = (const unsigned char *)s2;

	while (n) {
		if (*ss1 < *ss2)
			return -1;
		if (*ss1 > *ss2)
			return 1;
		ss1++;
		ss2++;
		n--;
	}

	return 0;
}

/* Skip over functions that are being used from DriverLibrary to save space */
char *
strncpy(char *dest, const char *src, size_t n)
{
	char *endp;
	char *p;

	p = dest;
	endp = p + n;

	while (p != endp && (*p++ = *src++) != '\0')
		;

	/* zero fill remainder */
	bzero(p, (endp - p));

	return dest;
}

size_t
BCMPOSTTRAPFN(strlen)(const char *s)
{
	size_t n = 0;

	while (*s) {
		s++;
		n++;
	}

	return n;
}

int
BCMPOSTTRAPFN(strcmp)(const char *s1, const char *s2)
{
	while (*s2 && *s1) {
		if (*s1 < *s2)
			return -1;
		if (*s1 > *s2)
			return 1;
		s1++;
		s2++;
	}

	if (*s1 && !*s2)
		return 1;
	if (!*s1 && *s2)
		return -1;
	return 0;
}
#endif /* EFI */

int
strncmp(const char *s1, const char *s2, size_t n)
{
	while (*s2 && *s1 && n) {
		if (*s1 < *s2)
			return -1;
		if (*s1 > *s2)
			return 1;
		s1++;
		s2++;
		n--;
	}

	if (!n)
		return 0;
	if (*s1 && !*s2)
		return 1;
	if (!*s1 && *s2)
		return -1;
	return 0;
}

char *
strchr(const char *str, int c)
{
	const char *x = str;

	while (*x != (char)c) {
		if (*x++ == '\0')
			return (NULL);
	}

	return DISCARD_QUAL(x, char);
}

char *
strrchr(const char *str, int c)
{
	const char *save = NULL;

	do {
		if (*str == (char)c)
			save = str;
	} while (*str++ != '\0');

	return DISCARD_QUAL(save, char);
}

/* Skip over functions that are being used from DriverLibrary to save space */
#ifndef EFI
char *
strstr(const char *s, const char *substr)
{
	int substr_len = strlen(substr);

	for (; *s; s++)
		if (strncmp(s, substr, substr_len) == 0)
			return DISCARD_QUAL(s, char);

	return NULL;
}
#endif /* EFI */

size_t
strspn(const char *s, const char *accept)
{
	uint count = 0;

	while (s[count] && strchr(accept, s[count]))
		count++;

	return count;
}

size_t
strcspn(const char *s, const char *reject)
{
	uint count = 0;

	while (s[count] && !strchr(reject, s[count]))
		count++;

	return count;
}

void *
memchr(const void *s, int c, size_t n)
{
	if (n != 0) {
		const unsigned char *ptr = s;

		do {
			if (*ptr == (unsigned char)c)
				return DISCARD_QUAL(ptr, void);
			ptr++;
			n--;
		} while (n != 0);
	}
	return NULL;
}

unsigned long
strtoul(const char *cp, char **endp, int base)
{
	ulong result, value;
	bool minus;

	minus = FALSE;

	while (bcm_isspace(*cp))
		cp++;

	if (cp[0] == '+')
		cp++;
	else if (cp[0] == '-') {
		minus = TRUE;
		cp++;
	}

	if (base == 0) {
		if (cp[0] == '0') {
			if ((cp[1] == 'x') || (cp[1] == 'X')) {
				base = 16;
				cp = &cp[2];
			} else {
				base = 8;
				cp = &cp[1];
			}
		} else
			base = 10;
	} else if (base == 16 && (cp[0] == '0') && ((cp[1] == 'x') || (cp[1] == 'X'))) {
		cp = &cp[2];
	}

	result = 0;

	while (bcm_isxdigit(*cp) &&
	       (value = bcm_isdigit(*cp) ? *cp - '0' : bcm_toupper(*cp) - 'A' + 10) <
	       (ulong) base) {
		result = result * base + value;
		cp++;
	}

	if (minus)
		result = (ulong)(result * -1);

	if (endp)
		*endp = DISCARD_QUAL(cp, char);

	return (result);
}

#ifdef EFI
int
putchar(int c)
{
	return putc(c, stdout);
}

int
puts(const char *s)
{
	char c;

	while ((c = *s++))
		putchar(c);

	putchar('\n');

	return 0;
}

#else /* !EFI */

/* memset is not in ROM offload because it is used directly by the compiler in
 * structure assignments/character array initialization with "".
 */
void *
BCMPOSTTRAPFN(memset)(void *dest, int c, size_t n)
{
	uint32 w, *dw;
	unsigned char *d;

	dw = (uint32 *)dest;

	/* 8 min because we have to create w */
	if ((n >= 8) && (((uintptr)dest & 3) == 0)) {
		if (c == 0)
			w = 0;
		else {
			unsigned char ch;

			ch = (unsigned char)(c & 0xff);
			w = (ch << 8) | ch;
			w |= w << 16;
		}
		while (n >= 4) {
			*dw++ = w;
			n -= 4;
		}
	}
	d = (unsigned char *)dw;

	while (n) {
		*d++ = (unsigned char)c;
		n--;
	}

	return dest;
}

/* memcpy is not in ROM offload because it is used directly by the compiler in
 * structure assignments.
 */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7A__)
void *
BCMPOSTTRAPFN(memcpy)(void *dest, const void *src, size_t n)
{
	uint32 *dw;
	const uint32 *sw;
	unsigned char *d;
	const unsigned char *s;

	sw = (const uint32 *)src;
	dw = (uint32 *)dest;

	if (n >= 4 && ((uintptr)src & 3) == ((uintptr)dest & 3)) {
		uint32 t1, t2, t3, t4, t5, t6, t7, t8;
		int i = (4 - ((uintptr)src & 3)) % 4;

		n -= i;

		d = (unsigned char *)dw;
		s = (const unsigned char *)sw;
		while (i--) {
			*d++ = *s++;
		}
		sw = (const uint32 *)s;
		dw = (uint32 *)d;

		if (n >= 32) {
			const uint32 *sfinal = (const uint32 *)((const uint8 *)sw + (n & ~31));

			asm volatile("\n1:\t"
				     "ldmia.w\t%0!, {%3, %4, %5, %6, %7, %8, %9, %10}\n\t"
				     "stmia.w\t%1!, {%3, %4, %5, %6, %7, %8, %9, %10}\n\t"
				     "cmp\t%2, %0\n\t"
				     "bhi.n\t1b\n\t"
				     : "=r" (sw), "=r" (dw), "=r" (sfinal), "=r" (t1), "=r" (t2),
				     "=r" (t3), "=r" (t4), "=r" (t5), "=r" (t6), "=r" (t7),
				     "=r" (t8)
				     : "0" (sw), "1" (dw), "2" (sfinal));

			n %= 32;
		}

		/* Copy the remaining words */
		switch (n / 4) {
		case 0:
			break;
		case 1:
			asm volatile("ldr\t%2, [%0]\n\t"
			             "str\t%2, [%1]\n\t"
			             "adds\t%0, #4\n\t"
			             "adds\t%1, #4\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1)
			             : "0" (sw), "1" (dw));
			break;
		case 2:
			asm volatile("ldmia.w\t%0!, {%2, %3}\n\t"
			             "stmia.w\t%1!, {%2, %3}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2)
			             : "0" (sw), "1" (dw));
			break;
		case 3:
			asm volatile("ldmia.w\t%0!, {%2, %3, %4}\n\t"
			             "stmia.w\t%1!, {%2, %3, %4}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2),
			             "=r" (t3)
			             : "0" (sw), "1" (dw));
			break;
		case 4:
			asm volatile("ldmia.w\t%0!, {%2, %3, %4, %5}\n\t"
			             "stmia.w\t%1!, {%2, %3, %4, %5}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2),
			             "=r" (t3), "=r" (t4)
			             : "0" (sw), "1" (dw));
			break;
		case 5:
			asm volatile(
				     "ldmia.w\t%0!, {%2, %3, %4, %5, %6}\n\t"
			             "stmia.w\t%1!, {%2, %3, %4, %5, %6}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2),
			             "=r" (t3), "=r" (t4), "=r" (t5)
			             : "0" (sw), "1" (dw));
			break;
		case 6:
			asm volatile(
				     "ldmia.w\t%0!, {%2, %3, %4, %5, %6, %7}\n\t"
			             "stmia.w\t%1!, {%2, %3, %4, %5, %6, %7}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2),
			             "=r" (t3), "=r" (t4), "=r" (t5), "=r" (t6)
			             : "0" (sw), "1" (dw));
			break;
		case 7:
			asm volatile(
				     "ldmia.w\t%0!, {%2, %3, %4, %5, %6, %8, %7}\n\t"
			             "stmia.w\t%1!, {%2, %3, %4, %5, %6, %8, %7}\n\t"
			             : "=r" (sw), "=r" (dw), "=r" (t1), "=r" (t2),
			             "=r" (t3), "=r" (t4), "=r" (t5), "=r" (t6),
			             "=r" (t7)
			             : "0" (sw), "1" (dw));
			break;
		default:
			ASSERT(0);
			break;
		}
		n = n % 4;
	}

	/* Copy the remaining bytes */
	d = (unsigned char *)dw;
	s = (const unsigned char *)sw;
	while (n--) {
		*d++ = *s++;
	}

	return dest;
}

#ifdef __clang__
/* TODO: remove once toolchain builtin libraries are available */
/* simulate compiler builtins */

/* not aligned */
void *__aeabi_memcpy(void *dest, const void *src, size_t n);
void *
__aeabi_memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

/* 4 byte aligned */
void *__aeabi_memcpy4(void *dest, const void *src, size_t n);
void *
__aeabi_memcpy4(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

/* 8 byte aligned */
void *__aeabi_memcpy8(void *dest, const void *src, size_t n);
void *
__aeabi_memcpy8(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

/* 8 byte aligned */
void *__aeabi_memclr8(void *dest, size_t n);
void *
__aeabi_memclr8(void *dest, size_t n)
{
	return memset(dest, 0, n);
}
#endif /* __clang__ */
#else
void *
memcpy(void *dest, const void *src, size_t n)
{
	uint32 *dw;
	const uint32 *sw;
	unsigned char *d;
	const unsigned char *s;

	sw = (const uint32 *)src;
	dw = (uint32 *)dest;

	if ((n >= 4) && (((uintptr)src & 3) == ((uintptr)dest & 3))) {
		int i = (4 - ((uintptr)src & 3)) % 4;
		n -= i;
		d = (unsigned char *)dw;
		s = (const unsigned char *)sw;
		while (i--) {
			*d++ = *s++;
		}

		sw = (const uint32 *)s;
		dw = (uint32 *)d;
		while (n >= 4) {
			*dw++ = *sw++;
			n -= 4;
		}
	}
	d = (unsigned char *)dw;
	s = (const unsigned char *)sw;
	while (n--) {
		*d++ = *s++;
	}

	return dest;
}
#endif /* defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7A__) */
#endif /* EFI */

/* a hook to send printf output to the host */
static printf_sendup_output_fn_t g_printf_sendup_output_fn = NULL;
static void *g_printf_sendup_output_ctx = NULL;

#ifdef DONGLEBUILD
static bool _rodata_overwritten = FALSE;

/* Ensure this string is not const. */
CONST char BCMPOST_TRAP_RODATA(warn_str)[] = "RO overwritten %p\n";
CONST char BCMPOST_TRAP_RODATA(time_str)[] = "%06u.%03u ";
#endif /* DONGLEBUILD */

void
printf_set_sendup_output_fn(printf_sendup_output_fn_t fn, void *ctx)
{
	g_printf_sendup_output_fn = fn;
	g_printf_sendup_output_ctx = ctx;
}

#ifdef DONGLEBUILD
void
BCMPOSTTRAPFN(printf_set_rodata_invalid)(void)
{
	_rodata_overwritten = TRUE;
}

bool
printf_get_rodata_invalid(void)
{
	return (_rodata_overwritten);
}
#endif /* DONGLEBUILD */

/* Include printf if it has already not been defined as NULL */
#ifndef printf
static bool last_nl = FALSE;
int
BCMPOSTTRAPFN(printf)(const char *fmt, ...)
{
	va_list ap;
	int count = 0, i;
	char buffer[PRINTF_BUFLEN + 1];

	if (FWSIGN_ENAB()) {
		return 0;
	}

	if (!printf_lock())
		return 0;

#ifdef DONGLEBUILD
	if (_rodata_overwritten == TRUE) {
		/* Regular printf will be garbage if ROdata is overwritten. In that case,
		 * print the caller address.
		 */
		_rodata_overwritten = FALSE;
		count = printf(warn_str, CALL_SITE);
		_rodata_overwritten = TRUE;
		return count;
	}

	if (last_nl) {
		/* add the dongle ref time */
		uint32 dongle_time_ms = hnd_get_reftime_ms();
		count = sprintf(buffer, time_str, dongle_time_ms / 1000, dongle_time_ms % 1000);
	}
#endif /* DONGLEBUILD */

	va_start(ap, fmt);
	count += vsnprintf(buffer + count, sizeof(buffer) - count, fmt, ap);
	va_end(ap);

	for (i = 0; i < count; i++) {
		putchar(buffer[i]);

		/* EFI environment requires CR\LF in a printf, etc.
		 * so unless the string has \r\n, it will not execute CR
		 * So force it!
		 */
#ifdef EFI
		if (buffer[i] == '\n')
			putchar('\r');
#endif
	}

	/* send the output up to the host */
	if (g_printf_sendup_output_fn != NULL) {
		g_printf_sendup_output_fn(g_printf_sendup_output_ctx, buffer, count);
	}

	if (buffer[count - 1] == '\n')
		last_nl = TRUE;
	else
		last_nl = FALSE;

	printf_unlock();

	return count;
}
#endif /* printf */

#if !defined(_WIN32) && !defined(EFI)
int
fputs(const char *s, FILE *stream /* UNUSED */)
{
	char c;

	UNUSED_PARAMETER(stream);
	while ((c = *s++))
		putchar(c);
	return 0;
}

int
puts(const char *s)
{
	fputs(s, stdout);
	putchar('\n');
	return 0;
}

int
fputc(int c, FILE *stream)
{
	return putc(c, stream);
}

unsigned long
rand(void)
{
	static unsigned long seed = 1;
	long x, hi, lo, t;

	x = seed;
	hi = x / 127773;
	lo = x % 127773;
	t = 16807 * lo - 2836 * hi;
	if (t <= 0) t += 0x7fffffff;
	seed = t;
	return t;
}
#endif /* !_WIN32 && !EFI */

#endif /* BCMSTDLIB_SNPRINTF_ONLY */

#if !defined(_WIN32) || defined(EFI)
size_t
strnlen(const char *s, size_t maxlen)
{
	const char *b = s;
	const char *e = s + maxlen;

	while (s < e && *s) {
		s++;
	}

	return s - b;
}
#endif /* !_WIN32 || EFI */

/* FORTIFY_SOURCE: Implementation of compiler built-in functions for C standard library functions
 * that provide run-time buffer overflow detection.
 */
#if defined(BCM_FORTIFY_SOURCE)

void*
__memcpy_chk(void *dest, const void *src, size_t n, size_t destsz)
{
	if (memcpy_s(dest, destsz, src, n) != 0) {
		OSL_SYS_HALT();
	}

	return (dest);
}

void *
__memmove_chk(void *dest, const void *src, size_t n, size_t destsz)
{
	if (memmove_s(dest, destsz, src, n) != 0) {
		OSL_SYS_HALT();
	}

	return (dest);
}

void *
__memset_chk(void *dest, int c, size_t n, size_t destsz)
{
	if (memset_s(dest, destsz, c, n) != 0) {
		OSL_SYS_HALT();
	}

	return (dest);
}

int
__snprintf_chk(char *str, size_t n, int flag, size_t destsz, const char *fmt, ...)
{
	va_list arg;
	int rc;

	if (n > destsz) {
		OSL_SYS_HALT();
	}

	va_start(arg, fmt);
	rc = vsnprintf(str, n, fmt, arg);
	va_end(arg);

	return (rc);
}

int
__vsnprintf_chk(char *str, size_t n, int flags, size_t destsz, const char *fmt, va_list ap)
{
	if (n > destsz) {
		OSL_SYS_HALT();
	}

	return (vsnprintf(str, n, fmt, ap));
}

char *
__strncpy_chk(char *dest, const char *src, size_t n, size_t destsz)
{
	if (n > destsz) {
		OSL_SYS_HALT();
	}

	return (strncpy(dest, src, n));
}
#endif	/* BCM_FORTIFY_SOURCE */

/* Provide stub implementations for xxx_s() APIs that are remapped to compiler builtins.
 * This allows the target to link.
 *
 * This is only intended as a compile-time test, and should be used by compile-only targets.
 */
#if defined(BCM_STDLIB_S_BUILTINS_TEST)
#undef strcpy
char* strcpy(char *dest, const char *src);
char*
strcpy(char *dest, const char *src)
{
	return (NULL);
}

#undef strcat
char* strcat(char *dest, const char *src);
char*
strcat(char *dest, const char *src)
{
	return (NULL);
}
#endif /* BCM_STDLIB_S_BUILTINS_TEST */
