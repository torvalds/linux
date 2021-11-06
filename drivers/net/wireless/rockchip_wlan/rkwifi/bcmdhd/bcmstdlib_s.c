/*
 * Broadcom Secure Standard Library.
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

#include <typedefs.h>
#include <bcmdefs.h>
#ifdef BCMDRIVER
#include <osl.h>
#else /* BCMDRIVER */
#include <stddef.h>
#include <string.h>
#endif /* else BCMDRIVER */

#include <bcmstdlib_s.h>
#include <bcmutils.h>

/* Don't use compiler builtins for stdlib APIs within the implementation of the stdlib itself. */
#if defined(BCM_STDLIB_S_BUILTINS_TEST)
	#undef memmove_s
	#undef memcpy_s
	#undef memset_s
	#undef strlcpy
	#undef strlcat_s
#endif /* BCM_STDLIB_S_BUILTINS_TEST */

/*
 * __SIZE_MAX__ value is depending on platform:
 * Firmware Dongle: RAMSIZE (Dongle Specific Limit).
 * LINUX NIC/Windows/MACOSX/Application: OS Native or
 * 0xFFFFFFFFu if not defined.
 */
#ifndef SIZE_MAX
#ifndef __SIZE_MAX__
#ifdef DONGLEBUILD
#define __SIZE_MAX__ RAMSIZE
#else
#define __SIZE_MAX__ 0xFFFFFFFFu
#endif /* DONGLEBUILD */
#endif /* __SIZE_MAX__ */
#define SIZE_MAX __SIZE_MAX__
#endif /* SIZE_MAX */
#define RSIZE_MAX (SIZE_MAX >> 1u)

#if !defined(__STDC_WANT_SECURE_LIB__) && \
	!(defined(__STDC_LIB_EXT1__) && defined(__STDC_WANT_LIB_EXT1__))
/*
 * memmove_s - secure memmove
 * dest : pointer to the object to copy to
 * destsz : size of the destination buffer
 * src : pointer to the object to copy from
 * n : number of bytes to copy
 * Return Value : zero on success and non-zero on error
 * Also on error, if dest is not a null pointer and destsz not greater
 * than RSIZE_MAX, writes destsz zero bytes into the dest object.
 */
int
memmove_s(void *dest, size_t destsz, const void *src, size_t n)
{
	int err = BCME_OK;

	if ((!dest) || (((char *)dest + destsz) < (char *)dest)) {
		err = BCME_BADARG;
		goto exit;
	}

	if (destsz > RSIZE_MAX) {
		err = BCME_BADLEN;
		goto exit;
	}

	if (destsz < n) {
		memset(dest, 0, destsz);
		err = BCME_BADLEN;
		goto exit;
	}

	if ((!src) || (((const char *)src + n) < (const char *)src)) {
		memset(dest, 0, destsz);
		err = BCME_BADARG;
		goto exit;
	}

	memmove(dest, src, n);
exit:
	return err;
}

/*
 * memcpy_s - secure memcpy
 * dest : pointer to the object to copy to
 * destsz : size of the destination buffer
 * src : pointer to the object to copy from
 * n : number of bytes to copy
 * Return Value : zero on success and non-zero on error
 * Also on error, if dest is not a null pointer and destsz not greater
 * than RSIZE_MAX, writes destsz zero bytes into the dest object.
 */
int
BCMPOSTTRAPFN(memcpy_s)(void *dest, size_t destsz, const void *src, size_t n)
{
	int err = BCME_OK;
	char *d = dest;
	const char *s = src;

	if ((!d) || ((d + destsz) < d)) {
		err = BCME_BADARG;
		goto exit;
	}

	if (destsz > RSIZE_MAX) {
		err = BCME_BADLEN;
		goto exit;
	}

	if (destsz < n) {
		memset(dest, 0, destsz);
		err = BCME_BADLEN;
		goto exit;
	}

	if ((!s) || ((s + n) < s)) {
		memset(dest, 0, destsz);
		err = BCME_BADARG;
		goto exit;
	}

	/* overlap checking between dest and src */
	if (!(((d + destsz) <= s) || (d >= (s + n)))) {
		memset(dest, 0, destsz);
		err = BCME_BADARG;
		goto exit;
	}

	(void)memcpy(dest, src, n);
exit:
	return err;
}

/*
 * memset_s - secure memset
 * dest : pointer to the object to be set
 * destsz : size of the destination buffer
 * c : byte value
 * n : number of bytes to be set
 * Return Value : zero on success and non-zero on error
 * Also on error, if dest is not a null pointer and destsz not greater
 * than RSIZE_MAX, writes destsz bytes with value c into the dest object.
 */
int
BCMPOSTTRAPFN(memset_s)(void *dest, size_t destsz, int c, size_t n)
{
	int err = BCME_OK;
	if ((!dest) || (((char *)dest + destsz) < (char *)dest)) {
		err = BCME_BADARG;
		goto exit;
	}

	if (destsz > RSIZE_MAX) {
		err = BCME_BADLEN;
		goto exit;
	}

	if (destsz < n) {
		(void)memset(dest, c, destsz);
		err = BCME_BADLEN;
		goto exit;
	}

	(void)memset(dest, c, n);
exit:
	return err;
}
#endif /* !__STDC_WANT_SECURE_LIB__ && !(__STDC_LIB_EXT1__ && __STDC_WANT_LIB_EXT1__) */

#if !defined(FREEBSD) && !defined(MACOSX) && !defined(BCM_USE_PLATFORM_STRLCPY)
/**
 * strlcpy - Copy a %NUL terminated string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer 0 if input parameters are NOK
 * return: string leng of src (which is always < size) on success or size on failure
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t i;

	if (dest == NULL || size == 0) {
		return 0;
	}

	if (src == NULL) {
		*dest = '\0';
		return 0;
	}

	for (i = 0; i < size; i++) {
		dest[i] = src[i];
		if (dest[i] == '\0') {
			/* success - src string copied */
			return i;
		}
	}

	/* NULL terminate since not found in src */
	dest[size - 1u] = '\0';

	/* fail - src string truncated */
	return size;
}
#endif /* !defined(FREEBSD) && !defined(MACOSX) && !defined(BCM_USE_PLATFORM_STRLCPY) */

/**
 * strlcat_s - Concatenate a %NUL terminated string with a sized buffer
 * @dest: Where to concatenate the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 * return: string length of created string (i.e. the initial length of dest plus the length of src)
 *         not including the NUL char, up until size
 *
 * Unlike strncat(), strlcat() take the full size of the buffer (not just the number of bytes to
 * copy) and guarantee to NUL-terminate the result (even when there's nothing to concat).
 * If the length of dest string concatinated with the src string >= size, truncation occurs.
 *
 * Compatible with *BSD: the result is always a valid NUL-terminated string that fits in the buffer
 * (unless, of course, the buffer size is zero).
 *
 * If either src or dest is not NUL-terminated, dest[size-1] will be set to NUL.
 * If size < strlen(dest) + strlen(src), dest[size-1] will be set to NUL.
 * If size == 0, dest[0] will be set to NUL.
 */
size_t
strlcat_s(char *dest, const char *src, size_t size)
{
	char *d = dest;
	const char *s = src;	/* point to the start of the src string */
	size_t n = size;
	size_t dlen;
	size_t bytes_to_copy = 0;

	if (dest == NULL) {
		return 0;
	}

	/* set d to point to the end of dest string (up to size) */
	while (n != 0 && *d != '\0') {
		d++;
		n--;
	}
	dlen = (size_t)(d - dest);

	if (s != NULL) {
		size_t slen = 0;

		/* calculate src len in case it's not null-terminated */
		n = size;
		while (n-- != 0 && *(s + slen) != '\0') {
			++slen;
		}

		n = size - dlen;	/* maximum num of chars to copy */
		if (n != 0) {
			/* copy relevant chars (until end of src buf or given size is reached) */
			bytes_to_copy = MIN(slen - (size_t)(s - src), n - 1);
			(void)memcpy(d, s, bytes_to_copy);
			d += bytes_to_copy;
		}
	}
	if (n == 0 && dlen != 0) {
		--d;	/* nothing to copy, but NUL-terminate dest anyway */
	}
	*d = '\0';	/* NUL-terminate dest */

	return (dlen + bytes_to_copy);
}
