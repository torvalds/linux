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

ELFTC_VCSID("$Id: dwarf_seterror.c 2075 2011-10-27 03:47:28Z jkoshy $");

#define	_SET_FIELD(R, F, V)					\
	do {							\
		(R) = (F);					\
		(F) = (V);					\
	} while (0)

#define	SET_FIELD(D, R, F)					\
	do {							\
		if (D)						\
			_SET_FIELD(R, (D)->dbg_##F, F);		\
		else						\
			_SET_FIELD(R, _libdwarf.F, F);		\
	} while (0)

Dwarf_Handler
dwarf_seterrhand(Dwarf_Debug dbg, Dwarf_Handler errhand)
{
	Dwarf_Handler oldhandler;

	SET_FIELD(dbg, oldhandler, errhand);

	return (oldhandler);
}

Dwarf_Ptr
dwarf_seterrarg(Dwarf_Debug dbg, Dwarf_Ptr errarg)
{
	Dwarf_Ptr oldarg;

	SET_FIELD(dbg, oldarg, errarg);

	return (oldarg);
}
