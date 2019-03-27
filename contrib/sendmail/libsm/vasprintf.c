/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

/*
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND TODD C. MILLER DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL TODD C. MILLER BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: vasprintf.c,v 1.28 2013-11-22 20:51:44 ca Exp $")
#include <stdlib.h>
#include <errno.h>
#include <sm/io.h>
#include <sm/heap.h>
#include "local.h"

/*
**  SM_VASPRINTF -- printf to a dynamically allocated string
**
**  Write 'printf' output to a dynamically allocated string
**  buffer which is returned to the caller.
**
**	Parameters:
**		str -- *str receives a pointer to the allocated string
**		fmt -- format directives for printing
**		ap -- variable argument list
**
**	Results:
**		On failure, set *str to NULL, set errno, and return -1.
**
**		On success, set *str to a pointer to a nul-terminated
**		string buffer containing printf output,	and return the
**		length of the string (not counting the nul).
*/

#define SM_VA_BUFSIZE	128

int
sm_vasprintf(str, fmt, ap)
	char **str;
	const char *fmt;
	SM_VA_LOCAL_DECL
{
	int ret;
	SM_FILE_T fake;
	unsigned char *base;

	fake.sm_magic = SmFileMagic;
	fake.f_timeout = SM_TIME_FOREVER;
	fake.f_timeoutstate = SM_TIME_BLOCK;
	fake.f_file = -1;
	fake.f_flags = SMWR | SMSTR | SMALC;
	fake.f_bf.smb_base = fake.f_p = (unsigned char *)sm_malloc(SM_VA_BUFSIZE);
	if (fake.f_bf.smb_base == NULL)
		goto err2;
	fake.f_close = NULL;
	fake.f_open = NULL;
	fake.f_read = NULL;
	fake.f_write = NULL;
	fake.f_seek = NULL;
	fake.f_setinfo = fake.f_getinfo = NULL;
	fake.f_type = "sm_vasprintf:fake";
	fake.f_bf.smb_size = fake.f_w = SM_VA_BUFSIZE - 1;
	fake.f_timeout = SM_TIME_FOREVER;
	ret = sm_io_vfprintf(&fake, SM_TIME_FOREVER, fmt, ap);
	if (ret == -1)
		goto err;
	*fake.f_p = '\0';

	/* use no more space than necessary */
	base = (unsigned char *) sm_realloc(fake.f_bf.smb_base, ret + 1);
	if (base == NULL)
		goto err;
	*str = (char *)base;
	return ret;

err:
	if (fake.f_bf.smb_base != NULL)
	{
		sm_free(fake.f_bf.smb_base);
		fake.f_bf.smb_base = NULL;
	}
err2:
	*str = NULL;
	errno = ENOMEM;
	return -1;
}
