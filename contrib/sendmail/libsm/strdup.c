/*
 * Copyright (c) 2000-2001, 2003 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: strdup.c,v 1.16 2013-11-22 20:51:43 ca Exp $")

#include <sm/heap.h>
#include <sm/string.h>

/*
**  SM_STRNDUP_X -- Duplicate a string of a given length
**
**	Allocates memory and copies source string (of given length) into it.
**
**	Parameters:
**		s -- string to copy.
**		n -- length to copy.
**
**	Returns:
**		copy of string, raises exception if out of memory.
**
**	Side Effects:
**		allocate memory for new string.
*/

char *
sm_strndup_x(s, n)
	const char *s;
	size_t n;
{
	char *d = sm_malloc_x(n + 1);

	(void) memcpy(d, s, n);
	d[n] = '\0';
	return d;
}

/*
**  SM_STRDUP -- Duplicate a string
**
**	Allocates memory and copies source string into it.
**
**	Parameters:
**		s -- string to copy.
**
**	Returns:
**		copy of string, NULL if out of memory.
**
**	Side Effects:
**		allocate memory for new string.
*/

char *
sm_strdup(s)
	char *s;
{
	size_t l;
	char *d;

	l = strlen(s) + 1;
	d = sm_malloc_tagged(l, "sm_strdup", 0, sm_heap_group());
	if (d != NULL)
		(void) sm_strlcpy(d, s, l);
	return d;
}

#if DO_NOT_USE_STRCPY

/*
**  SM_STRDUP_X -- Duplicate a string
**
**	Allocates memory and copies source string into it.
**
**	Parameters:
**		s -- string to copy.
**
**	Returns:
**		copy of string, exception if out of memory.
**
**	Side Effects:
**		allocate memory for new string.
*/

char *
sm_strdup_x(s)
	const char *s;
{
	size_t l;
	char *d;

	l = strlen(s) + 1;
	d = sm_malloc_tagged_x(l, "sm_strdup_x", 0, sm_heap_group());
	(void) sm_strlcpy(d, s, l);
	return d;
}

/*
**  SM_PSTRDUP_X -- Duplicate a string (using "permanent" memory)
**
**	Allocates memory and copies source string into it.
**
**	Parameters:
**		s -- string to copy.
**
**	Returns:
**		copy of string, exception if out of memory.
**
**	Side Effects:
**		allocate memory for new string.
*/

char *
sm_pstrdup_x(s)
	const char *s;
{
	size_t l;
	char *d;

	l = strlen(s) + 1;
	d = sm_pmalloc_x(l);
	(void) sm_strlcpy(d, s, l);
	return d;
}

/*
**  SM_STRDUP_X -- Duplicate a string
**
**	Allocates memory and copies source string into it.
**
**	Parameters:
**		s -- string to copy.
**		file -- name of source file
**		line -- line in source file
**		group -- heap group
**
**	Returns:
**		copy of string, exception if out of memory.
**
**	Side Effects:
**		allocate memory for new string.
*/

char *
sm_strdup_tagged_x(s, file, line, group)
	const char *s;
	char *file;
	int line, group;
{
	size_t l;
	char *d;

	l = strlen(s) + 1;
	d = sm_malloc_tagged_x(l, file, line, group);
	(void) sm_strlcpy(d, s, l);
	return d;
}

#endif /* DO_NOT_USE_STRCPY */

