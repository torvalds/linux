/*
 * Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>

SM_RCSID("@(#)$Id: util.c,v 1.10 2013-11-22 20:51:44 ca Exp $")
#include <sm/setjmp.h>
#include <sm/conf.h>
#include <sm/assert.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/sendmail.h>
#include <ctype.h>

/*
**  STR2PRT -- convert "unprintable" characters in a string to \oct
**
**	Parameters:
**		s -- string to convert
**
**	Returns:
**		converted string.
**		This is a static local buffer, string must be copied
**		before this function is called again!
*/

char *
str2prt(s)
	char *s;
{
	int l;
	char c, *h;
	bool ok;
	static int len = 0;
	static char *buf = NULL;

	if (s == NULL)
		return NULL;
	ok = true;
	for (h = s, l = 1; *h != '\0'; h++, l++)
	{
		if (*h == '\\')
		{
			++l;
			ok = false;
		}
		else if (!(isascii(*h) && isprint(*h)))
		{
			l += 3;
			ok = false;
		}
	}
	if (ok)
		return s;
	if (l > len)
	{
		char *nbuf = sm_pmalloc_x(l);

		if (buf != NULL)
			sm_free(buf);
		len = l;
		buf = nbuf;
	}
	for (h = buf; *s != '\0' && l > 0; s++, l--)
	{
		c = *s;
		if (isascii(c) && isprint(c) && c != '\\')
		{
			*h++ = c;
		}
		else
		{
			*h++ = '\\';
			--l;
			switch (c)
			{
			  case '\\':
				*h++ = '\\';
				break;
			  case '\t':
				*h++ = 't';
				break;
			  case '\n':
				*h++ = 'n';
				break;
			  case '\r':
				*h++ = 'r';
				break;
			  default:
				SM_ASSERT(l >= 2);
				(void) sm_snprintf(h, l, "%03o",
					(unsigned int)((unsigned char) c));

				/*
				**  XXX since l is unsigned this may
				**  wrap around if the calculation is screwed
				**  up...
				*/

				l -= 2;
				h += 3;
				break;
			}
		}
	}
	*h = '\0';
	buf[len - 1] = '\0';
	return buf;
}

/*
**  QUOTE_INTERNAL_CHARS -- do quoting of internal characters
**
**	Necessary to make sure that we don't have metacharacters such
**	as the internal versions of "$*" or "$&" in a string.
**	The input and output pointers can be the same.
**
**	Parameters:
**		ibp -- a pointer to the string to translate
**		obp -- a pointer to an output buffer
**		bsp -- pointer to the length of the output buffer
**
**	Returns:
**		A possibly new bp (if the buffer needed to grow); if
**		it is different, *bsp will updated to the size of
**		the new buffer and the caller is responsible for
**		freeing the memory.
*/

#define SM_MM_QUOTE(ch) (((ch) & 0377) == METAQUOTE || (((ch) & 0340) == 0200))

char *
quote_internal_chars(ibp, obp, bsp)
	char *ibp;
	char *obp;
	int *bsp;
{
	char *ip, *op;
	int bufused, olen;
	bool buffer_same, needs_quoting;

	buffer_same = ibp == obp;
	needs_quoting = false;

	/* determine length of output string (starts at 1 for trailing '\0') */
	for (ip = ibp, olen = 1; *ip != '\0'; ip++, olen++)
	{
		if (SM_MM_QUOTE(*ip))
		{
			olen++;
			needs_quoting = true;
		}
	}

	/* is the output buffer big enough? */
	if (olen > *bsp)
	{
		obp = sm_malloc_x(olen);
		buffer_same = false;
		*bsp = olen;
	}

	/*
	**  shortcut: no change needed?
	**  Note: we don't check this first as some bozo may use the same
	**  buffers but restrict the size of the output buffer to less
	**  than the length of the input buffer in which case we need to
	**  allocate a new buffer.
	*/

	if (!needs_quoting)
	{
		if (!buffer_same)
		{
			bufused = sm_strlcpy(obp, ibp, *bsp);
			SM_ASSERT(bufused <= olen);
		}
		return obp;
	}

	if (buffer_same)
	{
		obp = sm_malloc_x(olen);
		buffer_same = false;
		*bsp = olen;
	}

	for (ip = ibp, op = obp, bufused = 0; *ip != '\0'; ip++)
	{
		if (SM_MM_QUOTE(*ip))
		{
			SM_ASSERT(bufused < olen);
			op[bufused++] = METAQUOTE;
		}
		SM_ASSERT(bufused < olen);
		op[bufused++] = *ip;
	}
	op[bufused] = '\0';
	return obp;
}

/*
**  DEQUOTE_INTERNAL_CHARS -- undo the effect of quote_internal_chars
**
**	Parameters:
**		ibp -- a pointer to the string to be translated.
**		obp -- a pointer to the output buffer.  Can be the
**			same as ibp.
**		obs -- the size of the output buffer.
**
**	Returns:
**		number of character added to obp
*/

int
dequote_internal_chars(ibp, obp, obs)
	char *ibp;
	char *obp;
	int obs;
{
	char *ip, *op;
	int len;
	bool quoted;

	quoted = false;
	len = 0;
	for (ip = ibp, op = obp; *ip != '\0'; ip++)
	{
		if ((*ip & 0377) == METAQUOTE && !quoted)
		{
			quoted = true;
			continue;
		}
		if (op < &obp[obs - 1])
		{
			*op++ = *ip;
			++len;
		}
		quoted = false;
	}
	*op = '\0';
	return len;
}
