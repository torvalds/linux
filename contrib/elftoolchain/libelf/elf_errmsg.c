/*-
 * Copyright (c) 2006,2008,2011 Joseph Koshy
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

#include <libelf.h>
#include <stdio.h>
#include <string.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_errmsg.c 3174 2015-03-27 17:13:41Z emaste $");

/*
 * Retrieve a human readable translation for an error message.
 */

static const char *_libelf_errors[] = {
#define	DEFINE_ERROR(N,S)	[ELF_E_##N] = S
	DEFINE_ERROR(NONE,	"No Error"),
	DEFINE_ERROR(ARCHIVE,	"Malformed ar(1) archive"),
	DEFINE_ERROR(ARGUMENT,	"Invalid argument"),
	DEFINE_ERROR(CLASS,	"ELF class mismatch"),
	DEFINE_ERROR(DATA,	"Invalid data buffer descriptor"),
	DEFINE_ERROR(HEADER,	"Missing or malformed ELF header"),
	DEFINE_ERROR(IO,	"I/O error"),
	DEFINE_ERROR(LAYOUT,	"Layout constraint violation"),
	DEFINE_ERROR(MODE,	"Incorrect ELF descriptor mode"),
	DEFINE_ERROR(RANGE,	"Value out of range of target"),
	DEFINE_ERROR(RESOURCE,	"Resource exhaustion"),
	DEFINE_ERROR(SECTION,	"Invalid section descriptor"),
	DEFINE_ERROR(SEQUENCE,	"API calls out of sequence"),
	DEFINE_ERROR(UNIMPL,	"Unimplemented feature"),
	DEFINE_ERROR(VERSION,	"Unknown ELF API version"),
	DEFINE_ERROR(NUM,	"Unknown error")
#undef	DEFINE_ERROR
};

const char *
elf_errmsg(int error)
{
	int oserr;

	if (error == ELF_E_NONE &&
	    (error = LIBELF_PRIVATE(error)) == 0)
	    return NULL;
	else if (error == -1)
	    error = LIBELF_PRIVATE(error);

	oserr = error >> LIBELF_OS_ERROR_SHIFT;
	error &= LIBELF_ELF_ERROR_MASK;

	if (error < ELF_E_NONE || error >= ELF_E_NUM)
		return _libelf_errors[ELF_E_NUM];
	if (oserr) {
		(void) snprintf((char *) LIBELF_PRIVATE(msg),
		    sizeof(LIBELF_PRIVATE(msg)), "%s: %s",
		    _libelf_errors[error], strerror(oserr));
		return (const char *)&LIBELF_PRIVATE(msg);
	}
	return _libelf_errors[error];
}
