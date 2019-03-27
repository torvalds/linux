/*-
 * Copyright (c) 2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <assert.h>
#include <errno.h>
#include <libelftc.h>
#include <stdlib.h>
#include <string.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: elftc_demangle.c 3296 2016-01-09 14:17:28Z jkoshy $");

static unsigned int
is_mangled(const char *s, unsigned int style)
{

	switch (style) {
	case ELFTC_DEM_ARM: return (is_cpp_mangled_ARM(s) ? style : 0);
	case ELFTC_DEM_GNU2: return (is_cpp_mangled_gnu2(s) ? style : 0);
	case ELFTC_DEM_GNU3: return (is_cpp_mangled_gnu3(s) ? style : 0);
	}

	/* No style or invalid style spcified, try to guess. */
	if (is_cpp_mangled_gnu3(s))
		return (ELFTC_DEM_GNU3);
	if (is_cpp_mangled_gnu2(s))
		return (ELFTC_DEM_GNU2);
	if (is_cpp_mangled_ARM(s))
		return (ELFTC_DEM_ARM);

	/* Cannot be demangled. */
	return (0);
}

static char *
demangle(const char *s, unsigned int style, unsigned int rc)
{

	(void) rc;			/* XXX */
	switch (style) {
	case ELFTC_DEM_ARM: return (cpp_demangle_ARM(s));
	case ELFTC_DEM_GNU2: return (cpp_demangle_gnu2(s));
	case ELFTC_DEM_GNU3: return (cpp_demangle_gnu3(s));
	default:
		assert(0);
		return (NULL);
	}
}

int
elftc_demangle(const char *mangledname, char *buffer, size_t bufsize,
    unsigned int flags)
{
	unsigned int style, rc;
	char *rlt;

	style = flags & 0xFFFF;
	rc = flags >> 16;

	if (mangledname == NULL ||
	    ((style = is_mangled(mangledname, style)) == 0)) {
		errno = EINVAL;
		return (-1);
	}

	if ((rlt = demangle(mangledname, style, rc)) == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (buffer == NULL || bufsize < strlen(rlt) + 1) {
		free(rlt);
		errno = ENAMETOOLONG;
		return (-1);
	}

	strncpy(buffer, rlt, bufsize);
	buffer[bufsize - 1] = '\0';
	free(rlt);

	return (0);
}
