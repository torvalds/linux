/*
 * dump.c - save precompiled ktap chunks
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"


typedef struct {
	ktap_writer writer;
	void *data;
	int strip;
	int status;
} DumpState;

#define DumpMem(b, n, size, D)	DumpBlock(b, (n)*(size), D)
#define DumpVar(x, D)		DumpMem(&x, 1, sizeof(x), D)

static void DumpBlock(const void *b, size_t size, DumpState *D)
{
	if (D->status == 0)
		D->status = ((D->writer))(b, size, D->data);
}

static void DumpChar(int y, DumpState *D)
{
	char x = (char)y;
	DumpVar(x, D);
}

static void DumpInt(int x, DumpState *D)
{
	DumpVar(x, D);
}

static void DumpNumber(ktap_number x, DumpState *D)
{
	DumpVar(x,D);
}

static void DumpVector(const void *b, int n, size_t size, DumpState *D)
{
	DumpInt(n, D);
	DumpMem(b, n, size, D);
}

static void DumpString(const ktap_string *s, DumpState *D)
{
	if (s == NULL) {
		int size = 0;
		DumpVar(size, D);
	} else {
		int size = s->tsv.len + 1;		/* include trailing '\0' */
		DumpVar(size, D);
		DumpBlock(getstr(s), size * sizeof(char), D);
	}
}

#define DumpCode(f, D)	 DumpVector(f->code, f->sizecode, sizeof(ktap_instruction), D)

static void DumpFunction(const ktap_proto *f, DumpState *D);

static void DumpConstants(const ktap_proto *f, DumpState *D)
{
	int i, n = f->sizek;

	DumpInt(n, D);
	for (i = 0; i < n; i++) {
		const ktap_value* o=&f->k[i];
		DumpChar(ttypenv(o), D);
		switch (ttypenv(o)) {
		case KTAP_TNIL:
			break;
		case KTAP_TBOOLEAN:
			DumpChar(bvalue(o), D);
			break;
		case KTAP_TNUMBER:
			DumpNumber(nvalue(o), D);
			break;
		case KTAP_TSTRING:
			DumpString(rawtsvalue(o), D);
			break;
		default:
			printf("ktap: DumpConstants with unknown vaule type %d\n", ttypenv(o));
			ktap_assert(0);
		}
	}
	n = f->sizep;
	DumpInt(n, D);
	for (i = 0; i < n; i++)
		DumpFunction(f->p[i], D);
}

static void DumpUpvalues(const ktap_proto *f, DumpState *D)
{
	int i, n = f->sizeupvalues;

	DumpInt(n, D);
	for (i = 0; i < n; i++) {
		DumpChar(f->upvalues[i].instack, D);
		DumpChar(f->upvalues[i].idx, D);
	}
}

static void DumpDebug(const ktap_proto *f, DumpState *D)
{
	int i,n;

	DumpString((D->strip) ? NULL : f->source, D);
	n= (D->strip) ? 0 : f->sizelineinfo;
	DumpVector(f->lineinfo, n, sizeof(int), D);
	n = (D->strip) ? 0 : f->sizelocvars;
	DumpInt(n, D);

	for (i = 0; i < n; i++) {
		DumpString(f->locvars[i].varname, D);
		DumpInt(f->locvars[i].startpc, D);
		DumpInt(f->locvars[i].endpc, D);
	}
	n = (D->strip) ? 0 : f->sizeupvalues;
	DumpInt(n, D);
	for (i = 0; i < n; i++)
		DumpString(f->upvalues[i].name, D);
}

static void DumpFunction(const ktap_proto *f, DumpState *D)
{
	DumpInt(f->linedefined, D);
	DumpInt(f->lastlinedefined, D);
	DumpChar(f->numparams, D);
	DumpChar(f->is_vararg, D);
	DumpChar(f->maxstacksize, D);
	DumpCode(f, D);
	DumpConstants(f, D);
	DumpUpvalues(f, D);
	DumpDebug(f, D);
}

static void DumpHeader(DumpState *D)
{
	u8 h[KTAPC_HEADERSIZE];

	kp_header(h);
	DumpBlock(h, KTAPC_HEADERSIZE, D);
}

/*
 * dump ktap function as precompiled chunk
 */
int ktapc_dump(const ktap_proto *f, ktap_writer w, void *data, int strip)
{
	DumpState D;

	D.writer = w;
	D.data = data;
	D.strip = strip;
	D.status = 0;
	DumpHeader(&D);
	DumpFunction(f, &D);
	return D.status;
}
