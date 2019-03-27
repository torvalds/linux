/*-
 * Copyright (c) 2015 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <errno.h>
#include <stdlib.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: pe_init.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

PE *
pe_init(int fd, PE_Cmd c, PE_Object o)
{
	PE *pe;

	if ((pe = calloc(1, sizeof(*pe))) == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	pe->pe_fd = fd;
	pe->pe_cmd = c;
	pe->pe_obj = o;
	STAILQ_INIT(&pe->pe_scn);

	switch (c) {
	case PE_C_READ:
	case PE_C_RDWR:
		if (libpe_open_object(pe) < 0)
			goto init_fail;
		break;

	case PE_C_WRITE:
		if (o < PE_O_PE32 || o > PE_O_COFF) {
			errno = EINVAL;
			goto init_fail;
		}
		break;

	default:
		errno = EINVAL;
		goto init_fail;
	}

	return (pe);

init_fail:
	pe_finish(pe);
	return (NULL);
}

void
pe_finish(PE *pe)
{

	if (pe == NULL)
		return;

	libpe_release_object(pe);
}

PE_Object
pe_object(PE *pe)
{

	if (pe == NULL) {
		errno = EINVAL;
		return (PE_O_UNKNOWN);
	}

	return (pe->pe_obj);
}
