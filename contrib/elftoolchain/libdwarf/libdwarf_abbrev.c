/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009-2011 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_abbrev.c 3420 2016-02-27 02:14:05Z emaste $");

int
_dwarf_abbrev_add(Dwarf_CU cu, uint64_t entry, uint64_t tag, uint8_t children,
    uint64_t aboff, Dwarf_Abbrev *abp, Dwarf_Error *error)
{
	Dwarf_Abbrev ab;
	Dwarf_Debug dbg;

	dbg = cu != NULL ? cu->cu_dbg : NULL;

	if ((ab = malloc(sizeof(struct _Dwarf_Abbrev))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	/* Initialise the abbrev structure. */
	ab->ab_entry	= entry;
	ab->ab_tag	= tag;
	ab->ab_children	= children;
	ab->ab_offset	= aboff;
	ab->ab_length	= 0;	/* fill in later. */
	ab->ab_atnum	= 0;	/* fill in later. */

	/* Initialise the list of attribute definitions. */
	STAILQ_INIT(&ab->ab_attrdef);

	/* Add the abbrev to the hash table of the compilation unit. */
	if (cu != NULL)
		HASH_ADD(ab_hh, cu->cu_abbrev_hash, ab_entry,
		    sizeof(ab->ab_entry), ab);

	if (abp != NULL)
		*abp = ab;

	return (DW_DLE_NONE);
}

int
_dwarf_attrdef_add(Dwarf_Debug dbg, Dwarf_Abbrev ab, uint64_t attr,
    uint64_t form, uint64_t adoff, Dwarf_AttrDef *adp, Dwarf_Error *error)
{
	Dwarf_AttrDef ad;

	if (ab == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if ((ad = malloc(sizeof(struct _Dwarf_AttrDef))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	/* Initialise the attribute definition structure. */
	ad->ad_attrib	= attr;
	ad->ad_form	= form;
	ad->ad_offset	= adoff;

	/* Add the attribute definition to the list in the abbrev. */
	STAILQ_INSERT_TAIL(&ab->ab_attrdef, ad, ad_next);

	/* Increase number of attribute counter. */
	ab->ab_atnum++;

	if (adp != NULL)
		*adp = ad;

	return (DW_DLE_NONE);
}

int
_dwarf_abbrev_parse(Dwarf_Debug dbg, Dwarf_CU cu, Dwarf_Unsigned *offset,
    Dwarf_Abbrev *abp, Dwarf_Error *error)
{
	Dwarf_Section *ds;
	uint64_t attr;
	uint64_t entry;
	uint64_t form;
	uint64_t aboff;
	uint64_t adoff;
	uint64_t tag;
	uint8_t children;
	int ret;

	assert(abp != NULL);

	ds = _dwarf_find_section(dbg, ".debug_abbrev");
	if (ds == NULL || *offset >= ds->ds_size)
		return (DW_DLE_NO_ENTRY);

	aboff = *offset;

	entry = _dwarf_read_uleb128(ds->ds_data, offset);
	if (entry == 0) {
		/* Last entry. */
		ret = _dwarf_abbrev_add(cu, entry, 0, 0, aboff, abp,
		    error);
		if (ret == DW_DLE_NONE) {
			(*abp)->ab_length = 1;
			return (ret);
		} else
			return (ret);
	}
	tag = _dwarf_read_uleb128(ds->ds_data, offset);
	children = dbg->read(ds->ds_data, offset, 1);
	if ((ret = _dwarf_abbrev_add(cu, entry, tag, children, aboff,
	    abp, error)) != DW_DLE_NONE)
		return (ret);

	/* Parse attribute definitions. */
	do {
		adoff = *offset;
		attr = _dwarf_read_uleb128(ds->ds_data, offset);
		form = _dwarf_read_uleb128(ds->ds_data, offset);
		if (attr != 0)
			if ((ret = _dwarf_attrdef_add(dbg, *abp, attr,
			    form, adoff, NULL, error)) != DW_DLE_NONE)
				return (ret);
	} while (attr != 0);

	(*abp)->ab_length = *offset - aboff;

	return (ret);
}

int
_dwarf_abbrev_find(Dwarf_CU cu, uint64_t entry, Dwarf_Abbrev *abp,
    Dwarf_Error *error)
{
	Dwarf_Abbrev ab;
	Dwarf_Section *ds;
	Dwarf_Unsigned offset;
	int ret;

	if (entry == 0)
		return (DW_DLE_NO_ENTRY);

	/* Check if the desired abbrev entry is already in the hash table. */
	HASH_FIND(ab_hh, cu->cu_abbrev_hash, &entry, sizeof(entry), ab);
	if (ab != NULL) {
		*abp = ab;
		return (DW_DLE_NONE);
	}

	if (cu->cu_abbrev_loaded) {
		return (DW_DLE_NO_ENTRY);
	}

	/* Load and search the abbrev table. */
	ds = _dwarf_find_section(cu->cu_dbg, ".debug_abbrev");
	if (ds == NULL)
		return (DW_DLE_NO_ENTRY);

	offset = cu->cu_abbrev_offset_cur;
	while (offset < ds->ds_size) {
		ret = _dwarf_abbrev_parse(cu->cu_dbg, cu, &offset, &ab, error);
		if (ret != DW_DLE_NONE)
			return (ret);
		if (ab->ab_entry == entry) {
			cu->cu_abbrev_offset_cur = offset;
			*abp = ab;
			return (DW_DLE_NONE);
		}
		if (ab->ab_entry == 0) {
			cu->cu_abbrev_offset_cur = offset;
			cu->cu_abbrev_loaded = 1;
			break;
		}
	}

	return (DW_DLE_NO_ENTRY);
}

void
_dwarf_abbrev_cleanup(Dwarf_CU cu)
{
	Dwarf_Abbrev ab, tab;
	Dwarf_AttrDef ad, tad;

	assert(cu != NULL);

	HASH_ITER(ab_hh, cu->cu_abbrev_hash, ab, tab) {
		HASH_DELETE(ab_hh, cu->cu_abbrev_hash, ab);
		STAILQ_FOREACH_SAFE(ad, &ab->ab_attrdef, ad_next, tad) {
			STAILQ_REMOVE(&ab->ab_attrdef, ad, _Dwarf_AttrDef,
			    ad_next);
			free(ad);
		}
		free(ab);
	}
}

int
_dwarf_abbrev_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_CU cu;
	Dwarf_Abbrev ab;
	Dwarf_AttrDef ad;
	Dwarf_P_Section ds;
	int ret;

	cu = STAILQ_FIRST(&dbg->dbg_cu);
	if (cu == NULL)
		return (DW_DLE_NONE);

	/* Create .debug_abbrev section. */
	if ((ret = _dwarf_section_init(dbg, &ds, ".debug_abbrev", 0, error)) !=
	    DW_DLE_NONE)
		return (ret);

	for (ab = cu->cu_abbrev_hash; ab != NULL; ab = ab->ab_hh.next) {
		RCHECK(WRITE_ULEB128(ab->ab_entry));
		RCHECK(WRITE_ULEB128(ab->ab_tag));
		RCHECK(WRITE_VALUE(ab->ab_children, 1));
		STAILQ_FOREACH(ad, &ab->ab_attrdef, ad_next) {
			RCHECK(WRITE_ULEB128(ad->ad_attrib));
			RCHECK(WRITE_ULEB128(ad->ad_form));
		}
		/* Signal end of attribute spec list. */
		RCHECK(WRITE_ULEB128(0));
		RCHECK(WRITE_ULEB128(0));
	}
	/* End of abbreviation for this CU. */
	RCHECK(WRITE_ULEB128(0));

	/* Notify the creation of .debug_abbrev ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	return (DW_DLE_NONE);

gen_fail:

	_dwarf_section_free(dbg, &ds);

	return (ret);
}
