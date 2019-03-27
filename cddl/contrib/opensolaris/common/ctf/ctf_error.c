/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012, Joyent, Inc.
 */

#include <ctf_impl.h>

static const char *const _ctf_errlist[] = {
	"File is not in CTF or ELF format",		 /* ECTF_FMT */
	"File uses more recent ELF version than libctf", /* ECTF_ELFVERS */
	"File uses more recent CTF version than libctf", /* ECTF_CTFVERS */
	"File is a different endian-ness than libctf",	 /* ECTF_ENDIAN */
	"Symbol table uses invalid entry size",		 /* ECTF_SYMTAB */
	"Symbol table data buffer is not valid",	 /* ECTF_SYMBAD */
	"String table data buffer is not valid",	 /* ECTF_STRBAD */
	"File data structure corruption detected",	 /* ECTF_CORRUPT */
	"File does not contain CTF data",		 /* ECTF_NOCTFDATA */
	"Buffer does not contain CTF data",		 /* ECTF_NOCTFBUF */
	"Symbol table information is not available",	 /* ECTF_NOSYMTAB */
	"Type information is in parent and unavailable", /* ECTF_NOPARENT */
	"Cannot import types with different data model", /* ECTF_DMODEL */
	"Failed to mmap a needed data section",		 /* ECTF_MMAP */
	"Decompression package SUNWzlib not installed",	 /* ECTF_ZMISSING */
	"Failed to initialize decompression library",	 /* ECTF_ZINIT */
	"Failed to allocate decompression buffer",	 /* ECTF_ZALLOC */
	"Failed to decompress CTF data",		 /* ECTF_DECOMPRESS */
	"External string table is not available",	 /* ECTF_STRTAB */
	"String name offset is corrupt",		 /* ECTF_BADNAME */
	"Invalid type identifier",			 /* ECTF_BADID */
	"Type is not a struct or union",		 /* ECTF_NOTSOU */
	"Type is not an enum",				 /* ECTF_NOTENUM */
	"Type is not a struct, union, or enum",		 /* ECTF_NOTSUE */
	"Type is not an integer or float",		 /* ECTF_NOTINTFP */
	"Type is not an array",				 /* ECTF_NOTARRAY */
	"Type does not reference another type",		 /* ECTF_NOTREF */
	"Input buffer is too small for type name",	 /* ECTF_NAMELEN */
	"No type information available for that name",	 /* ECTF_NOTYPE */
	"Syntax error in type name",			 /* ECTF_SYNTAX */
	"Symbol table entry is not a function",		 /* ECTF_NOTFUNC */
	"No function information available for symbol",	 /* ECTF_NOFUNCDAT */
	"Symbol table entry is not a data object",	 /* ECTF_NOTDATA */
	"No type information available for symbol",	 /* ECTF_NOTYPEDAT */
	"No label information available for that name",	 /* ECTF_NOLABEL */
	"File does not contain any labels",		 /* ECTF_NOLABELDATA */
	"Feature not supported",			 /* ECTF_NOTSUP */
	"Invalid enum element name",			 /* ECTF_NOENUMNAM */
	"Invalid member name",				 /* ECTF_NOMEMBNAM */
	"CTF container is read-only",			 /* ECTF_RDONLY */
	"Limit on number of dynamic type members reached", /* ECTF_DTFULL */
	"Limit on number of dynamic types reached",	 /* ECTF_FULL */
	"Duplicate member name definition",		 /* ECTF_DUPMEMBER */
	"Conflicting type is already defined",		 /* ECTF_CONFLICT */
	"Type has outstanding references",		 /* ECTF_REFERENCED */
	"Type is not a dynamic type"			 /* ECTF_NOTDYN */
};

static const int _ctf_nerr = sizeof (_ctf_errlist) / sizeof (_ctf_errlist[0]);

const char *
ctf_errmsg(int error)
{
	const char *str;

	if (error >= ECTF_BASE && (error - ECTF_BASE) < _ctf_nerr)
		str = _ctf_errlist[error - ECTF_BASE];
	else
		str = ctf_strerror(error);

	return (str ? str : "Unknown error");
}

int
ctf_errno(ctf_file_t *fp)
{
	return (fp->ctf_errno);
}
