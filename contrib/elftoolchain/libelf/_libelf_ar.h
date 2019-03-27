/*-
 * Copyright (c) 2010 Joseph Koshy
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS `AS IS' AND
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
 *
 * $Id: _libelf_ar.h 3013 2014-03-23 06:16:59Z jkoshy $
 */

#ifndef	__LIBELF_AR_H_
#define	__LIBELF_AR_H_

/*
 * Prototypes and declarations needed by libelf's ar(1) archive
 * handling code.
 */

#include <ar.h>

#define	LIBELF_AR_BSD_EXTENDED_NAME_PREFIX	"#1/"
#define	LIBELF_AR_BSD_SYMTAB_NAME		"__.SYMDEF"
#define	LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE	\
	(sizeof(LIBELF_AR_BSD_EXTENDED_NAME_PREFIX) - 1)

#define	IS_EXTENDED_BSD_NAME(NAME)				\
	(strncmp((const char *) (NAME),				\
	 LIBELF_AR_BSD_EXTENDED_NAME_PREFIX,			\
	 LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE) == 0)


unsigned char *_libelf_ar_get_string(const char *_buf, size_t _sz,
    unsigned int _rawname, int _svr4names);
char	*_libelf_ar_get_raw_name(const struct ar_hdr *_arh);
char	*_libelf_ar_get_translated_name(const struct ar_hdr *_arh, Elf *_ar);
int	_libelf_ar_get_number(const char *_buf, size_t _sz,
    unsigned int _base, size_t *_ret);

#endif	/* __LIBELF_AR_H_ */
