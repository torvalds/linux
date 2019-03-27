/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include "unwind.h"

void _Unwind_VRS_Get(struct _Unwind_Context *, int, _Unwind_Word, int, void *);
void _Unwind_VRS_Set(struct _Unwind_Context *, int, _Unwind_Word, int, void *);

_Unwind_Word
_Unwind_GetGR(struct _Unwind_Context *context, int regno)
{
	_Unwind_Word val;
	_Unwind_VRS_Get(context, 0 /*_UVRSC_CORE*/, regno, 0 /*_UVRSD_UINT32*/,
	    &val);

	return val;
}

_Unwind_Ptr
_Unwind_GetIP(struct _Unwind_Context *context)
{
	return (_Unwind_Ptr)(_Unwind_GetGR (context, 15) & ~(_Unwind_Word)1);
}

_Unwind_Ptr
_Unwind_GetIPInfo(struct _Unwind_Context *context, int *p)
{
	*p = 0;
	return _Unwind_GetIP(context);
}

void
_Unwind_SetGR(struct _Unwind_Context *context, int reg, _Unwind_Ptr val)
{
	_Unwind_VRS_Set(context, 0 /*_UVRSC_CORE*/, reg, 0 /*_UVRSD_UINT32*/,
		&val);
}
