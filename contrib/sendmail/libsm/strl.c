/*
 * Copyright (c) 1999-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: strl.c,v 1.32 2013-11-22 20:51:43 ca Exp $")
#include <sm/config.h>
#include <sm/string.h>

/*
**  Notice: this file is used by libmilter. Please try to avoid
**	using libsm specific functions.
*/

/*
**  XXX the type of the length parameter has been changed
**  from size_t to ssize_t to avoid theoretical problems with negative
**  numbers passed into these functions.
**  The real solution to this problem is to make sure that this doesn't
**  happen, but for now we'll use this workaround.
*/

/*
**  SM_STRLCPY -- size bounded string copy
**
**	This is a bounds-checking variant of strcpy.
**	If size > 0, copy up to size-1 characters from the nul terminated
**	string src to dst, nul terminating the result.  If size == 0,
**	the dst buffer is not modified.
**	Additional note: this function has been "tuned" to run fast and tested
**	as such (versus versions in some OS's libc).
**
**	The result is strlen(src).  You can detect truncation (not all
**	of the characters in the source string were copied) using the
**	following idiom:
**
**		char *s, buf[BUFSIZ];
**		...
**		if (sm_strlcpy(buf, s, sizeof(buf)) >= sizeof(buf))
**			goto overflow;
**
**	Parameters:
**		dst -- destination buffer
**		src -- source string
**		size -- size of destination buffer
**
**	Returns:
**		strlen(src)
*/

size_t
sm_strlcpy(dst, src, size)
	register char *dst;
	register const char *src;
	ssize_t size;
{
	register ssize_t i;

	if (size-- <= 0)
		return strlen(src);
	for (i = 0; i < size && (dst[i] = src[i]) != 0; i++)
		continue;
	dst[i] = '\0';
	if (src[i] == '\0')
		return i;
	else
		return i + strlen(src + i);
}

/*
**  SM_STRLCAT -- size bounded string concatenation
**
**	This is a bounds-checking variant of strcat.
**	If strlen(dst) < size, then append at most size - strlen(dst) - 1
**	characters from the source string to the destination string,
**	nul terminating the result.  Otherwise, dst is not modified.
**
**	The result is the initial length of dst + the length of src.
**	You can detect overflow (not all of the characters in the
**	source string were copied) using the following idiom:
**
**		char *s, buf[BUFSIZ];
**		...
**		if (sm_strlcat(buf, s, sizeof(buf)) >= sizeof(buf))
**			goto overflow;
**
**	Parameters:
**		dst -- nul-terminated destination string buffer
**		src -- nul-terminated source string
**		size -- size of destination buffer
**
**	Returns:
**		total length of the string tried to create
**		(= initial length of dst + length of src)
*/

size_t
sm_strlcat(dst, src, size)
	register char *dst;
	register const char *src;
	ssize_t size;
{
	register ssize_t i, j, o;

	o = strlen(dst);
	if (size < o + 1)
		return o + strlen(src);
	size -= o + 1;
	for (i = 0, j = o; i < size && (dst[j] = src[i]) != 0; i++, j++)
		continue;
	dst[j] = '\0';
	if (src[i] == '\0')
		return j;
	else
		return j + strlen(src + i);
}
/*
**  SM_STRLCAT2 -- append two strings to dst obeying length and
**		'\0' terminate it
**
**		strlcat2 will append at most len - strlen(dst) - 1 chars.
**		terminates with '\0' if len > 0
**		dst = dst "+" src1 "+" src2
**		use this instead of sm_strlcat(dst,src1); sm_strlcat(dst,src2);
**		for better speed.
**
**	Parameters:
**		dst -- "destination" string.
**		src1 -- "from" string 1.
**		src2 -- "from" string 2.
**		len -- max. length of "destination" string.
**
**	Returns:
**		total length of the string tried to create
**		(= initial length of dst + length of src)
**		if this is greater than len then an overflow would have
**		occurred.
**
*/

size_t
sm_strlcat2(dst, src1, src2, len)
	register char *dst;
	register const char *src1;
	register const char *src2;
	ssize_t len;
{
	register ssize_t i, j, o;

	/* current size of dst */
	o = strlen(dst);

	/* max. size is less than current? */
	if (len < o + 1)
		return o + strlen(src1) + strlen(src2);

	len -= o + 1;	/* space left in dst */

	/* copy the first string; i: index in src1; j: index in dst */
	for (i = 0, j = o; i < len && (dst[j] = src1[i]) != 0; i++, j++)
		continue;

	/* src1: end reached? */
	if (src1[i] != '\0')
	{
		/* no: terminate dst; there is space since i < len */
		dst[j] = '\0';
		return j + strlen(src1 + i) + strlen(src2);
	}

	len -= i;	/* space left in dst */

	/* copy the second string; i: index in src2; j: index in dst */
	for (i = 0; i < len && (dst[j] = src2[i]) != 0; i++, j++)
		continue;
	dst[j] = '\0';	/* terminate dst; there is space since i < len */
	if (src2[i] == '\0')
		return j;
	else
		return j + strlen(src2 + i);
}

/*
**  SM_STRLCPYN -- concatenate n strings and assign the result to dst
**		while obeying length and '\0' terminate it
**
**		dst = src1 "+" src2 "+" ...
**		use this instead of sm_snprintf() for string values
**		and repeated sm_strlc*() calls for better speed.
**
**	Parameters:
**		dst -- "destination" string.
**		len -- max. length of "destination" string.
**		n -- number of strings
**		strings...
**
**	Returns:
**		total length of the string tried to create
**		(= initial length of dst + length of src)
**		if this is greater than len then an overflow would have
**		occurred.
*/

size_t
#ifdef __STDC__
sm_strlcpyn(char *dst, ssize_t len, int n, ...)
#else /* __STDC__ */
sm_strlcpyn(dst, len, n, va_alist)
	register char *dst;
	ssize_t len;
	int n;
	va_dcl
#endif /* __STDC__ */
{
	register ssize_t i, j;
	char *str;
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, n);

	if (len-- <= 0) /* This allows space for the terminating '\0' */
	{
		i = 0;
		while (n-- > 0)
			i += strlen(SM_VA_ARG(ap, char *));
		SM_VA_END(ap);
		return i;
	}

	j = 0;	/* index in dst */

	/* loop through all source strings */
	while (n-- > 0)
	{
		str = SM_VA_ARG(ap, char *);

		/* copy string; i: index in str; j: index in dst */
		for (i = 0; j < len && (dst[j] = str[i]) != 0; i++, j++)
			continue;

		/* str: end reached? */
		if (str[i] != '\0')
		{
			/* no: terminate dst; there is space since j < len */
			dst[j] = '\0';
			j += strlen(str + i);
			while (n-- > 0)
				j += strlen(SM_VA_ARG(ap, char *));
			SM_VA_END(ap);
			return j;
		}
	}
	SM_VA_END(ap);

	dst[j] = '\0';	/* terminate dst; there is space since j < len */
	return j;
}

#if 0
/*
**  SM_STRLAPP -- append string if it fits into buffer.
**
**	If size > 0, copy up to size-1 characters from the nul terminated
**	string src to dst, nul terminating the result.  If size == 0,
**	the dst buffer is not modified.
**
**	This routine is useful for appending strings in a loop, e.g, instead of
**	s = buf;
**	for (ptr, ptr != NULL, ptr = next->ptr)
**	{
**		(void) sm_strlcpy(s, ptr->string, sizeof buf - (s - buf));
**		s += strlen(s);
**	}
**	replace the loop body with:
**		if (!sm_strlapp(*s, ptr->string, sizeof buf - (s - buf)))
**			break;
**	it's faster...
**
**	XXX interface isn't completely clear (yet), hence this code is
**	not available.
**
**
**	Parameters:
**		dst -- (pointer to) destination buffer
**		src -- source string
**		size -- size of destination buffer
**
**	Returns:
**		true if strlen(src) < size
**
**	Side Effects:
**		modifies dst if append succeeds (enough space).
*/

bool
sm_strlapp(dst, src, size)
	register char **dst;
	register const char *src;
	ssize_t size;
{
	register size_t i;

	if (size-- <= 0)
		return false;
	for (i = 0; i < size && ((*dst)[i] = src[i]) != '\0'; i++)
		continue;
	(*dst)[i] = '\0';
	if (src[i] == '\0')
	{
		*dst += i;
		return true;
	}

	/* undo */
	(*dst)[0] = '\0';
	return false;
}
#endif /* 0 */
