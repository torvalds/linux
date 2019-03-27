/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: stringf.c,v 1.16 2013-11-22 20:51:43 ca Exp $")
#include <errno.h>
#include <stdio.h>
#include <sm/exc.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/varargs.h>

/*
**  SM_STRINGF_X -- printf() to dynamically allocated string.
**
**	Takes the same arguments as printf.
**	It returns a pointer to a dynamically allocated string
**	containing the text that printf would print to standard output.
**	It raises an exception on error.
**	The name comes from a PWB Unix function called stringf.
**
**	Parameters:
**		fmt -- format string.
**		... -- arguments for format.
**
**	Returns:
**		Pointer to a dynamically allocated string.
**
**	Exceptions:
**		F:sm_heap -- out of memory (via sm_vstringf_x()).
*/

char *
#if SM_VA_STD
sm_stringf_x(const char *fmt, ...)
#else /* SM_VA_STD */
sm_stringf_x(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif /* SM_VA_STD */
{
	SM_VA_LOCAL_DECL
	char *s;

	SM_VA_START(ap, fmt);
	s = sm_vstringf_x(fmt, ap);
	SM_VA_END(ap);
	return s;
}

/*
**  SM_VSTRINGF_X -- printf() to dynamically allocated string.
**
**	Parameters:
**		fmt -- format string.
**		ap -- arguments for format.
**
**	Returns:
**		Pointer to a dynamically allocated string.
**
**	Exceptions:
**		F:sm_heap -- out of memory
*/

char *
sm_vstringf_x(fmt, ap)
	const char *fmt;
	SM_VA_LOCAL_DECL
{
	char *s;

	sm_vasprintf(&s, fmt, ap);
	if (s == NULL)
	{
		if (errno == ENOMEM)
			sm_exc_raise_x(&SmHeapOutOfMemory);
		sm_exc_raisenew_x(&SmEtypeOs, errno, "sm_vasprintf", NULL);
	}
	return s;
}
