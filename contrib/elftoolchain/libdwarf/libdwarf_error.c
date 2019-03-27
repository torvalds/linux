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

#include "_libdwarf.h"

ELFTC_VCSID("$Id: libdwarf_error.c 2070 2011-10-27 03:05:32Z jkoshy $");

void
_dwarf_set_error(Dwarf_Debug dbg, Dwarf_Error *error, int errorcode,
    int elferrorcode, const char *functionname, int linenumber)
{
	Dwarf_Error de;

	de.err_error = errorcode;
	de.err_elferror = elferrorcode;
	de.err_func  = functionname;
	de.err_line  = linenumber;
	de.err_msg[0] = '\0';
	
	/*
	 * If the user supplied a destination for the error, copy the
	 * error descriptor over and return.  Otherwise, if the debug
	 * context is known and has an error handler, invoke that.
	 * Otherwise, if a 'default' error handler was registered,
	 * invoke it.
	 */
	if (error)
		*error = de;
	else if (dbg && dbg->dbg_errhand)
		dbg->dbg_errhand(de, dbg->dbg_errarg);
	else if (_libdwarf.errhand)
		_libdwarf.errhand(de, _libdwarf.errarg);

	/* No handler found, do nothing. */
}
