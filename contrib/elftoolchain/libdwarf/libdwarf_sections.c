/*-
 * Copyright (c) 2010 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_sections.c 3041 2014-05-18 15:11:03Z kaiwang27 $");

#define	_SECTION_INIT_SIZE	128

int
_dwarf_section_init(Dwarf_P_Debug dbg, Dwarf_P_Section *dsp, const char *name,
    int pseudo, Dwarf_Error *error)
{
	Dwarf_P_Section ds;

	assert(dbg != NULL && dsp != NULL && name != NULL);

	if ((ds = calloc(1, sizeof(struct _Dwarf_P_Section))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	if ((ds->ds_name = strdup(name)) == NULL) {
		free(ds);
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	if (!pseudo) {
		ds->ds_cap = _SECTION_INIT_SIZE;
		if ((ds->ds_data = malloc((size_t) ds->ds_cap)) == NULL) {
			free(ds->ds_name);
			free(ds);
			DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
			return (DW_DLE_MEMORY);
		}
		STAILQ_INSERT_TAIL(&dbg->dbgp_seclist, ds, ds_next);
		dbg->dbgp_seccnt++;
	}

	*dsp = ds;

	return (DW_DLE_NONE);
}

void
_dwarf_section_free(Dwarf_P_Debug dbg, Dwarf_P_Section *dsp)
{
	Dwarf_P_Section ds, tds;

	assert(dbg != NULL && dsp != NULL);

	if (*dsp == NULL)
		return;

	STAILQ_FOREACH_SAFE(ds, &dbg->dbgp_seclist, ds_next, tds) {
		if (ds == *dsp) {
			STAILQ_REMOVE(&dbg->dbgp_seclist, ds, _Dwarf_P_Section,
			    ds_next);
			dbg->dbgp_seccnt--;
			break;
		}
	}
	ds = *dsp;
	if (ds->ds_name)
		free(ds->ds_name);
	if (ds->ds_data)
		free(ds->ds_data);
	free(ds);
	*dsp = NULL;
}

int
_dwarf_pro_callback(Dwarf_P_Debug dbg, char *name, int size,
    Dwarf_Unsigned type, Dwarf_Unsigned flags, Dwarf_Unsigned link,
    Dwarf_Unsigned info, Dwarf_Unsigned *symndx, int *error)
{
	int e, ret, isymndx;

	assert(dbg != NULL && name != NULL && symndx != NULL);

	if (dbg->dbgp_func_b)
		ret = dbg->dbgp_func_b(name, size, type, flags, link, info,
		    symndx, &e);
	else {
		ret = dbg->dbgp_func(name, size, type, flags, link, info,
		    &isymndx, &e);
		*symndx = isymndx;
	}
	if (ret < 0) {
		if (error)
			*error = e;
	}

	return (ret);
}

int
_dwarf_section_callback(Dwarf_P_Debug dbg, Dwarf_P_Section ds,
    Dwarf_Unsigned type, Dwarf_Unsigned flags, Dwarf_Unsigned link,
    Dwarf_Unsigned info, Dwarf_Error *error)
{
	int ret, ndx;

	ndx = _dwarf_pro_callback(dbg, ds->ds_name, (int) ds->ds_size,
	    type, flags, link, info, &ds->ds_symndx, NULL);
	if (ndx < 0) {
		ret = DW_DLE_ELF_SECT_ERR;
		DWARF_SET_ERROR(dbg, error, ret);
		return (ret);
	}
	ds->ds_ndx = ndx;

	return (DW_DLE_NONE);
}

int
_dwarf_generate_sections(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	int ret;

	/* Produce .debug_info section. */
	if ((ret = _dwarf_info_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_abbrev section. */
	if ((ret = _dwarf_abbrev_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_line section. */
	if ((ret = _dwarf_lineno_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_frame section. */
	if ((ret = _dwarf_frame_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_aranges section. */
	if ((ret = _dwarf_arange_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_macinfo section. */
	if ((ret = _dwarf_macinfo_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_pubnames section. */
	if ((ret = _dwarf_nametbl_gen(dbg, ".debug_pubnames", dbg->dbgp_pubs,
	    error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_weaknames section. */
	if ((ret = _dwarf_nametbl_gen(dbg, ".debug_weaknames", dbg->dbgp_weaks,
	    error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_funcnames section. */
	if ((ret = _dwarf_nametbl_gen(dbg, ".debug_funcnames", dbg->dbgp_funcs,
	    error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_typenames section. */
	if ((ret = _dwarf_nametbl_gen(dbg, ".debug_typenames", dbg->dbgp_types,
	    error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_varnames section. */
	if ((ret = _dwarf_nametbl_gen(dbg, ".debug_varnames", dbg->dbgp_vars,
	    error)) != DW_DLE_NONE)
		return (ret);

	/* Produce .debug_str section. */
	if ((ret = _dwarf_strtab_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Finally, update and generate all relocation sections. */
	if ((ret = _dwarf_reloc_gen(dbg, error)) != DW_DLE_NONE)
		return (ret);

	/* Set section/relocation iterator to the first element. */
	dbg->dbgp_secpos = STAILQ_FIRST(&dbg->dbgp_seclist);
	dbg->dbgp_drspos = STAILQ_FIRST(&dbg->dbgp_drslist);
	
	return (DW_DLE_NONE);
}

Dwarf_Section *
_dwarf_find_section(Dwarf_Debug dbg, const char *name)
{
	Dwarf_Section *ds;
	Dwarf_Half i;

	assert(dbg != NULL && name != NULL);

	for (i = 0; i < dbg->dbg_seccnt; i++) {
		ds = &dbg->dbg_section[i];
		if (ds->ds_name != NULL && !strcmp(ds->ds_name, name))
			return (ds);
	}

	return (NULL);
}

Dwarf_Section *
_dwarf_find_next_types_section(Dwarf_Debug dbg, Dwarf_Section *ds)
{

	assert(dbg != NULL);

	if (ds == NULL)
		return (_dwarf_find_section(dbg, ".debug_types"));

	assert(ds->ds_name != NULL);

	do {
		ds++;
		if (ds->ds_name != NULL &&
		    !strcmp(ds->ds_name, ".debug_types"))
			return (ds);
	} while (ds->ds_name != NULL);

	return (NULL);
}

Dwarf_P_Section
_dwarf_pro_find_section(Dwarf_P_Debug dbg, const char *name)
{
	Dwarf_P_Section ds;

	assert(dbg != NULL && name != NULL);

	STAILQ_FOREACH(ds, &dbg->dbgp_seclist, ds_next) {
		if (ds->ds_name != NULL && !strcmp(ds->ds_name ,name))
			return (ds);
	}

	return (NULL);
}

void
_dwarf_section_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_P_Section ds, tds;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	STAILQ_FOREACH_SAFE(ds, &dbg->dbgp_seclist, ds_next, tds) {
		STAILQ_REMOVE(&dbg->dbgp_seclist, ds, _Dwarf_P_Section,
		    ds_next);
		if (ds->ds_name)
			free(ds->ds_name);
		if (ds->ds_data)
			free(ds->ds_data);
		free(ds);
	}
	dbg->dbgp_seccnt = 0;
	dbg->dbgp_secpos = 0;
}
