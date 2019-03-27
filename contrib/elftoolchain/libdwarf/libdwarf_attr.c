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

ELFTC_VCSID("$Id: libdwarf_attr.c 3064 2014-06-06 19:35:55Z kaiwang27 $");

int
_dwarf_attr_alloc(Dwarf_Die die, Dwarf_Attribute *atp, Dwarf_Error *error)
{
	Dwarf_Attribute at;

	assert(die != NULL);
	assert(atp != NULL);

	if ((at = calloc(1, sizeof(struct _Dwarf_Attribute))) == NULL) {
		DWARF_SET_ERROR(die->die_dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	*atp = at;

	return (DW_DLE_NONE);
}

static int
_dwarf_attr_add(Dwarf_Die die, Dwarf_Attribute atref, Dwarf_Attribute *atp,
    Dwarf_Error *error)
{
	Dwarf_Attribute at;
	int ret;

	if ((ret = _dwarf_attr_alloc(die, &at, error)) != DW_DLE_NONE)
		return (ret);

	memcpy(at, atref, sizeof(struct _Dwarf_Attribute));

	STAILQ_INSERT_TAIL(&die->die_attr, at, at_next);

	/* Save a pointer to the attribute name if this is one. */
	if (at->at_attrib == DW_AT_name) {
		switch (at->at_form) {
		case DW_FORM_strp:
			die->die_name = at->u[1].s;
			break;
		case DW_FORM_string:
			die->die_name = at->u[0].s;
			break;
		default:
			break;
		}
	}

	if (atp != NULL)
		*atp = at;

	return (DW_DLE_NONE);
}

Dwarf_Attribute
_dwarf_attr_find(Dwarf_Die die, Dwarf_Half attr)
{
	Dwarf_Attribute at;

	STAILQ_FOREACH(at, &die->die_attr, at_next) {
		if (at->at_attrib == attr)
			break;
	}

	return (at);
}

int
_dwarf_attr_init(Dwarf_Debug dbg, Dwarf_Section *ds, uint64_t *offsetp,
    int dwarf_size, Dwarf_CU cu, Dwarf_Die die, Dwarf_AttrDef ad,
    uint64_t form, int indirect, Dwarf_Error *error)
{
	struct _Dwarf_Attribute atref;
	Dwarf_Section *str;
	int ret;

	ret = DW_DLE_NONE;
	memset(&atref, 0, sizeof(atref));
	atref.at_die = die;
	atref.at_offset = *offsetp;
	atref.at_attrib = ad->ad_attrib;
	atref.at_form = indirect ? form : ad->ad_form;
	atref.at_indirect = indirect;
	atref.at_ld = NULL;

	switch (form) {
	case DW_FORM_addr:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp,
		    cu->cu_pointer_size);
		break;
	case DW_FORM_block:
	case DW_FORM_exprloc:
		atref.u[0].u64 = _dwarf_read_uleb128(ds->ds_data, offsetp);
		atref.u[1].u8p = _dwarf_read_block(ds->ds_data, offsetp,
		    atref.u[0].u64);
		break;
	case DW_FORM_block1:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 1);
		atref.u[1].u8p = _dwarf_read_block(ds->ds_data, offsetp,
		    atref.u[0].u64);
		break;
	case DW_FORM_block2:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 2);
		atref.u[1].u8p = _dwarf_read_block(ds->ds_data, offsetp,
		    atref.u[0].u64);
		break;
	case DW_FORM_block4:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 4);
		atref.u[1].u8p = _dwarf_read_block(ds->ds_data, offsetp,
		    atref.u[0].u64);
		break;
	case DW_FORM_data1:
	case DW_FORM_flag:
	case DW_FORM_ref1:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 1);
		break;
	case DW_FORM_data2:
	case DW_FORM_ref2:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 2);
		break;
	case DW_FORM_data4:
	case DW_FORM_ref4:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 4);
		break;
	case DW_FORM_data8:
	case DW_FORM_ref8:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, 8);
		break;
	case DW_FORM_indirect:
		form = _dwarf_read_uleb128(ds->ds_data, offsetp);
		return (_dwarf_attr_init(dbg, ds, offsetp, dwarf_size, cu, die,
		    ad, form, 1, error));
	case DW_FORM_ref_addr:
		if (cu->cu_version == 2)
			atref.u[0].u64 = dbg->read(ds->ds_data, offsetp,
			    cu->cu_pointer_size);
		else
			atref.u[0].u64 = dbg->read(ds->ds_data, offsetp,
			    dwarf_size);
		break;
	case DW_FORM_ref_udata:
	case DW_FORM_udata:
		atref.u[0].u64 = _dwarf_read_uleb128(ds->ds_data, offsetp);
		break;
	case DW_FORM_sdata:
		atref.u[0].s64 = _dwarf_read_sleb128(ds->ds_data, offsetp);
		break;
	case DW_FORM_sec_offset:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, dwarf_size);
		break;
	case DW_FORM_string:
		atref.u[0].s = _dwarf_read_string(ds->ds_data, ds->ds_size,
		    offsetp);
		break;
	case DW_FORM_strp:
		atref.u[0].u64 = dbg->read(ds->ds_data, offsetp, dwarf_size);
		str = _dwarf_find_section(dbg, ".debug_str");
		assert(str != NULL);
		atref.u[1].s = (char *) str->ds_data + atref.u[0].u64;
		break;
	case DW_FORM_ref_sig8:
		atref.u[0].u64 = 8;
		atref.u[1].u8p = _dwarf_read_block(ds->ds_data, offsetp,
		    atref.u[0].u64);
		break;
	case DW_FORM_flag_present:
		/* This form has no value encoded in the DIE. */
		atref.u[0].u64 = 1;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLE_ATTR_FORM_BAD;
		break;
	}

	if (ret == DW_DLE_NONE) {
		if (form == DW_FORM_block || form == DW_FORM_block1 ||
		    form == DW_FORM_block2 || form == DW_FORM_block4) {
			atref.at_block.bl_len = atref.u[0].u64;
			atref.at_block.bl_data = atref.u[1].u8p;
		}
		ret = _dwarf_attr_add(die, &atref, NULL, error);
	}

	return (ret);
}

static int
_dwarf_attr_write(Dwarf_P_Debug dbg, Dwarf_P_Section ds, Dwarf_Rel_Section drs,
    Dwarf_CU cu, Dwarf_Attribute at, int pass2, Dwarf_Error *error)
{
	struct _Dwarf_P_Expr_Entry *ee;
	uint64_t value, offset, bs;
	int ret;

	assert(dbg != NULL && ds != NULL && cu != NULL && at != NULL);

	/* Fill in reference to other DIE in the second pass. */
	if (pass2) {
		if (at->at_form != DW_FORM_ref4 && at->at_form != DW_FORM_ref8)
			return (DW_DLE_NONE);
		if (at->at_refdie == NULL || at->at_offset == 0)
			return (DW_DLE_NONE);
		offset = at->at_offset;
		dbg->write(ds->ds_data, &offset, at->at_refdie->die_offset,
		    at->at_form == DW_FORM_ref4 ? 4 : 8);
		return (DW_DLE_NONE);
	}

	switch (at->at_form) {
	case DW_FORM_addr:
		if (at->at_relsym)
			ret = _dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, cu->cu_pointer_size,
			    ds->ds_size, at->at_relsym, at->u[0].u64, NULL,
			    error);
		else
			ret = WRITE_VALUE(at->u[0].u64, cu->cu_pointer_size);
		break;
	case DW_FORM_block:
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
		/* Write block size. */
		if (at->at_form == DW_FORM_block) {
			ret = _dwarf_write_uleb128_alloc(&ds->ds_data,
			    &ds->ds_cap, &ds->ds_size, at->u[0].u64, error);
			if (ret != DW_DLE_NONE)
				break;
		} else {
			if (at->at_form == DW_FORM_block1)
				bs = 1;
			else if (at->at_form == DW_FORM_block2)
				bs = 2;
			else
				bs = 4;
			ret = WRITE_VALUE(at->u[0].u64, bs);
			if (ret != DW_DLE_NONE)
				break;
		}

		/* Keep block data offset for later use. */
		offset = ds->ds_size;

		/* Write block data. */
		ret = WRITE_BLOCK(at->u[1].u8p, at->u[0].u64);
		if (ret != DW_DLE_NONE)
			break;
		if (at->at_expr == NULL)
			break;

		/* Generate relocation entry for DW_OP_addr expressions. */
		STAILQ_FOREACH(ee, &at->at_expr->pe_eelist, ee_next) {
			if (ee->ee_loc.lr_atom != DW_OP_addr || ee->ee_sym == 0)
				continue;
			ret = _dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, dbg->dbg_pointer_size,
			    offset + ee->ee_loc.lr_offset + 1, ee->ee_sym,
			    ee->ee_loc.lr_number, NULL, error);
			if (ret != DW_DLE_NONE)
				break;
		}
		break;
	case DW_FORM_data1:
	case DW_FORM_flag:
	case DW_FORM_ref1:
		ret = WRITE_VALUE(at->u[0].u64, 1);
		break;
	case DW_FORM_data2:
	case DW_FORM_ref2:
		ret = WRITE_VALUE(at->u[0].u64, 2);
		break;
	case DW_FORM_data4:
		if (at->at_relsym || at->at_relsec != NULL)
			ret = _dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, 4, ds->ds_size, at->at_relsym,
			    at->u[0].u64, at->at_relsec, error);
		else
			ret = WRITE_VALUE(at->u[0].u64, 4);
		break;
	case DW_FORM_data8:
		if (at->at_relsym || at->at_relsec != NULL)
			ret = _dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, 8, ds->ds_size, at->at_relsym,
			    at->u[0].u64, at->at_relsec, error);
		else
			ret = WRITE_VALUE(at->u[0].u64, 8);
		break;
	case DW_FORM_ref4:
	case DW_FORM_ref8:
		/*
		 * The value of ref4 and ref8 could be a reference to another
		 * DIE within the CU. And if we don't know the ref DIE's
		 * offset at the moement, then we remember at_offset and fill
		 * it in the second pass.
		 */
		if (at->at_refdie) {
			value = at->at_refdie->die_offset;
			if (value == 0) {
				cu->cu_pass2 = 1;
				at->at_offset = ds->ds_size;
			}
		} else
			value = at->u[0].u64;
		ret = WRITE_VALUE(value, at->at_form == DW_FORM_ref4 ? 4 : 8);
		break;
	case DW_FORM_indirect:
		/* TODO. */
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLE_ATTR_FORM_BAD;
		break;
	case DW_FORM_ref_addr:
		/* DWARF2 format. */
		if (at->at_relsym)
			ret = _dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, cu->cu_pointer_size,
			    ds->ds_size, at->at_relsym, at->u[0].u64, NULL,
			    error);
		else
			ret = WRITE_VALUE(at->u[0].u64, cu->cu_pointer_size);
		break;
	case DW_FORM_ref_udata:
	case DW_FORM_udata:
		ret = WRITE_ULEB128(at->u[0].u64);
		break;
	case DW_FORM_sdata:
		ret = WRITE_SLEB128(at->u[0].s64);
		break;
	case DW_FORM_string:
		assert(at->u[0].s != NULL);
		ret = WRITE_STRING(at->u[0].s);
		break;
	case DW_FORM_strp:
		ret = _dwarf_reloc_entry_add(dbg, drs, ds, dwarf_drt_data_reloc,
		    4, ds->ds_size, 0, at->u[0].u64, ".debug_str", error);
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLE_ATTR_FORM_BAD;
		break;
	}

	return (ret);
}

int
_dwarf_add_AT_dataref(Dwarf_P_Debug dbg, Dwarf_P_Die die, Dwarf_Half attr,
    Dwarf_Unsigned pc_value, Dwarf_Unsigned sym_index, const char *secname,
    Dwarf_P_Attribute *atp, Dwarf_Error *error)
{
	Dwarf_Attribute at;
	int ret;

	assert(dbg != NULL && die != NULL);

	if ((ret = _dwarf_attr_alloc(die, &at, error)) != DW_DLE_NONE)
		return (ret);

	at->at_die = die;
	at->at_attrib = attr;
	if (dbg->dbg_pointer_size == 4)
		at->at_form = DW_FORM_data4;
	else
		at->at_form = DW_FORM_data8;
	at->at_relsym = sym_index;
	at->at_relsec = secname;
	at->u[0].u64 = pc_value;

	STAILQ_INSERT_TAIL(&die->die_attr, at, at_next);

	if (atp)
		*atp = at;

	return (DW_DLE_NONE);
}

int
_dwarf_add_string_attr(Dwarf_P_Die die, Dwarf_P_Attribute *atp, Dwarf_Half attr,
    char *string, Dwarf_Error *error)
{
	Dwarf_Attribute at;
	Dwarf_Debug dbg;
	int ret;

	dbg = die != NULL ? die->die_dbg : NULL;

	assert(atp != NULL);

	if (die == NULL || string == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if ((ret = _dwarf_attr_alloc(die, &at, error)) != DW_DLE_NONE)
		return (ret);

	at->at_die = die;
	at->at_attrib = attr;
	at->at_form = DW_FORM_strp;
	if ((ret = _dwarf_strtab_add(dbg, string, &at->u[0].u64,
	    error)) != DW_DLE_NONE) {
		free(at);
		return (ret);
	}
	at->u[1].s = _dwarf_strtab_get_table(dbg) + at->u[0].u64;

	*atp = at;

	STAILQ_INSERT_TAIL(&die->die_attr, at, at_next);

	return (DW_DLE_NONE);
}

int
_dwarf_attr_gen(Dwarf_P_Debug dbg, Dwarf_P_Section ds, Dwarf_Rel_Section drs,
    Dwarf_CU cu, Dwarf_Die die, int pass2, Dwarf_Error *error)
{
	Dwarf_Attribute at;
	int ret;

	assert(dbg != NULL && ds != NULL && cu != NULL && die != NULL);

	STAILQ_FOREACH(at, &die->die_attr, at_next) {
		ret = _dwarf_attr_write(dbg, ds, drs, cu, at, pass2, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	return (DW_DLE_NONE);
}
