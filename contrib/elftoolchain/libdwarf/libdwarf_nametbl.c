/*-
 * Copyright (c) 2009,2010 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_nametbl.c 3029 2014-04-21 23:26:02Z kaiwang27 $");

void
_dwarf_nametbl_cleanup(Dwarf_NameSec *nsp)
{
	Dwarf_NameSec ns;
	Dwarf_NameTbl nt, tnt;
	Dwarf_NamePair np, tnp;

	assert(nsp != NULL);
	if ((ns = *nsp) == NULL)
		return;

	STAILQ_FOREACH_SAFE(nt, &ns->ns_ntlist, nt_next, tnt) {
		STAILQ_FOREACH_SAFE(np, &nt->nt_nplist, np_next, tnp) {
			STAILQ_REMOVE(&nt->nt_nplist, np, _Dwarf_NamePair,
			    np_next);
			free(np);
		}
		STAILQ_REMOVE(&ns->ns_ntlist, nt, _Dwarf_NameTbl, nt_next);
		free(nt);
	}
	if (ns->ns_array)
		free(ns->ns_array);
	free(ns);
	*nsp = NULL;
}

int
_dwarf_nametbl_init(Dwarf_Debug dbg, Dwarf_NameSec *namesec, Dwarf_Section *ds,
    Dwarf_Error *error)
{
	Dwarf_CU cu;
	Dwarf_NameSec ns;
	Dwarf_NameTbl nt;
	Dwarf_NamePair np;
	uint64_t offset, dwarf_size, length, cuoff;
	char *p;
	int i, ret;

	assert(*namesec == NULL);

	if ((ns = malloc(sizeof(struct _Dwarf_NameSec))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INIT(&ns->ns_ntlist);
	ns->ns_array = NULL;
	ns->ns_len = 0;

	offset = 0;
	while (offset < ds->ds_size) {

		/* Allocate a new name table. */
		if ((nt = malloc(sizeof(struct _Dwarf_NameTbl))) == NULL) {
			ret = DW_DLE_MEMORY;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}
		STAILQ_INIT(&nt->nt_nplist);
		STAILQ_INSERT_TAIL(&ns->ns_ntlist, nt, nt_next);

		/* Read in the table header. */
		length = dbg->read(ds->ds_data, &offset, 4);
		if (length == 0xffffffff) {
			dwarf_size = 8;
			length = dbg->read(ds->ds_data, &offset, 8);
		} else
			dwarf_size = 4;

		nt->nt_length = length;
		/* FIXME: verify version */
		nt->nt_version = dbg->read(ds->ds_data, &offset, 2);
		nt->nt_cu_offset = dbg->read(ds->ds_data, &offset, dwarf_size);
		nt->nt_cu_length = dbg->read(ds->ds_data, &offset, dwarf_size);

		if (!dbg->dbg_info_loaded) {
			ret = _dwarf_info_load(dbg, 1, 1, error);
			if (ret != DW_DLE_NONE)
				goto fail_cleanup;
		}

		/* Find the referenced CU. */
		STAILQ_FOREACH(cu, &dbg->dbg_cu, cu_next) {
			if (cu->cu_offset == nt->nt_cu_offset)
				break;
		}
		nt->nt_cu = cu;	/* FIXME: Check if NULL here */

		/* Add name pairs. */
		while (offset < ds->ds_size) {
			cuoff = dbg->read(ds->ds_data, &offset, dwarf_size);
			if (cuoff == 0)
				break;
			if ((np = malloc(sizeof(struct _Dwarf_NamePair))) ==
			    NULL) {
				ret = DW_DLE_MEMORY;
				DWARF_SET_ERROR(dbg, error, ret);
				goto fail_cleanup;
			}
			np->np_nt = nt;
			np->np_offset = cuoff;
			p = (char *) ds->ds_data;
			np->np_name = &p[offset];
			while (p[offset++] != '\0')
				;
			STAILQ_INSERT_TAIL(&nt->nt_nplist, np, np_next);
			ns->ns_len++;
		}
	}

	/* Build array of name pairs from all tables. */
	if (ns->ns_len > 0) {
		if ((ns->ns_array = malloc(sizeof(Dwarf_NamePair) *
		    ns->ns_len)) == NULL) {
			ret = DW_DLE_MEMORY;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}

		i = 0;
		STAILQ_FOREACH(nt, &ns->ns_ntlist, nt_next) {
			STAILQ_FOREACH(np, &nt->nt_nplist, np_next)
				ns->ns_array[i++] = np;
		}
		assert((Dwarf_Unsigned)i == ns->ns_len);
	}

	*namesec = ns;

	return (DW_DLE_NONE);

fail_cleanup:

	_dwarf_nametbl_cleanup(&ns);

	return (ret);
}

int
_dwarf_nametbl_gen(Dwarf_P_Debug dbg, const char *name, Dwarf_NameTbl nt,
    Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Rel_Section drs;
	Dwarf_NamePair np;
	uint64_t offset;
	int ret;

	assert(dbg != NULL && name != NULL);
	if (nt == NULL || STAILQ_EMPTY(&nt->nt_nplist))
		return (DW_DLE_NONE);

	nt->nt_length = 0;
	nt->nt_version = 2;
	nt->nt_cu = STAILQ_FIRST(&dbg->dbg_cu);
	assert(nt->nt_cu != NULL);
	nt->nt_cu_offset = nt->nt_cu->cu_offset;
	nt->nt_cu_length = nt->nt_cu->cu_length;

	/* Create name lookup section. */
	if ((ret = _dwarf_section_init(dbg, &ds, name, 0, error)) !=
	    DW_DLE_NONE)
		goto gen_fail0;

	/* Create relocation section for the name lookup section. */
	RCHECK(_dwarf_reloc_section_init(dbg, &drs, ds, error));

	/* Write table header. */
	RCHECK(WRITE_VALUE(nt->nt_length, 4));
	RCHECK(WRITE_VALUE(nt->nt_version, 2));
	RCHECK(_dwarf_reloc_entry_add(dbg, drs, ds, dwarf_drt_data_reloc, 4,
	    ds->ds_size, 0, nt->nt_cu_offset, ".debug_info", error));
	RCHECK(WRITE_VALUE(nt->nt_cu_length, 4));

	/* Write tuples. */
	STAILQ_FOREACH(np, &nt->nt_nplist, np_next) {
		assert(np->np_die != NULL);
		np->np_offset = np->np_die->die_offset;
		RCHECK(WRITE_VALUE(np->np_offset, 4));
		RCHECK(WRITE_STRING(np->np_name));
	}
	RCHECK(WRITE_VALUE(0, 4));

	/* Fill in the length field. */
	nt->nt_length = ds->ds_size - 4;
	offset = 0;
	dbg->write(ds->ds_data, &offset, nt->nt_length, 4);

	/* Inform application the creation of name lookup ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	/* Finalize relocation section for the name lookup section. */
	RCHECK(_dwarf_reloc_section_finalize(dbg, drs, error));

	return (DW_DLE_NONE);

gen_fail:
	_dwarf_reloc_section_free(dbg, &drs);

gen_fail0:
	_dwarf_section_free(dbg, &ds);

	return (ret);
}

void
_dwarf_nametbl_pro_cleanup(Dwarf_NameTbl *ntp)
{
	Dwarf_NameTbl nt;
	Dwarf_NamePair np, tnp;

	assert(ntp != NULL);
	if ((nt = *ntp) == NULL)
		return;

	STAILQ_FOREACH_SAFE(np, &nt->nt_nplist, np_next, tnp) {
		STAILQ_REMOVE(&nt->nt_nplist, np, _Dwarf_NamePair, np_next);
		if (np->np_name)
			free(np->np_name);
		free(np);
	}
	free(nt);
	*ntp = NULL;
}
