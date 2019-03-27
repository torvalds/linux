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

ELFTC_VCSID("$Id: libdwarf_die.c 3039 2014-05-18 15:10:56Z kaiwang27 $");

int
_dwarf_die_alloc(Dwarf_Debug dbg, Dwarf_Die *ret_die, Dwarf_Error *error)
{
	Dwarf_Die die;

	assert(ret_die != NULL);

	if ((die = calloc(1, sizeof(struct _Dwarf_Die))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	STAILQ_INIT(&die->die_attr);

	*ret_die = die;

	return (DW_DLE_NONE);
}

static int
_dwarf_die_add(Dwarf_CU cu, uint64_t offset, uint64_t abnum, Dwarf_Abbrev ab,
    Dwarf_Die *diep, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_Die die;
	int ret;

	assert(cu != NULL);
	assert(ab != NULL);

	dbg = cu->cu_dbg;

	if ((ret = _dwarf_die_alloc(dbg, &die, error)) != DW_DLE_NONE)
		return (ret);

	die->die_offset	= offset;
	die->die_abnum	= abnum;
	die->die_ab	= ab;
	die->die_cu	= cu;
	die->die_dbg	= cu->cu_dbg;

	if (diep != NULL)
		*diep = die;

	return (DW_DLE_NONE);
}

/* Find die at offset 'off' within the same CU. */
Dwarf_Die
_dwarf_die_find(Dwarf_Die die, Dwarf_Unsigned off)
{
	Dwarf_Debug dbg;
	Dwarf_Section *ds;
	Dwarf_CU cu;
	Dwarf_Die die1;
	Dwarf_Error de;
	int ret;

	cu = die->die_cu;
	dbg = die->die_dbg;
	ds = cu->cu_is_info ? dbg->dbg_info_sec : dbg->dbg_types_sec;

	ret = _dwarf_die_parse(dbg, ds, cu, cu->cu_dwarf_size, off,
	    cu->cu_next_offset, &die1, 0, &de);

	if (ret == DW_DLE_NONE)
		return (die1);
	else
		return (NULL);
}

int
_dwarf_die_parse(Dwarf_Debug dbg, Dwarf_Section *ds, Dwarf_CU cu,
    int dwarf_size, uint64_t offset, uint64_t next_offset, Dwarf_Die *ret_die,
    int search_sibling, Dwarf_Error *error)
{
	Dwarf_Abbrev ab;
	Dwarf_AttrDef ad;
	Dwarf_Die die;
	uint64_t abnum;
	uint64_t die_offset;
	int ret, level;

	assert(cu != NULL);

	level = 1;
	die = NULL;

	while (offset < next_offset && offset < ds->ds_size) {

		die_offset = offset;

		abnum = _dwarf_read_uleb128(ds->ds_data, &offset);

		if (abnum == 0) {
			if (level == 0 || !search_sibling)
				return (DW_DLE_NO_ENTRY);

			/*
			 * Return to previous DIE level.
			 */
			level--;
			continue;
		}

		if ((ret = _dwarf_abbrev_find(cu, abnum, &ab, error)) !=
		    DW_DLE_NONE)
			return (ret);

		if ((ret = _dwarf_die_add(cu, die_offset, abnum, ab, &die,
		    error)) != DW_DLE_NONE)
			return (ret);

		STAILQ_FOREACH(ad, &ab->ab_attrdef, ad_next) {
			if ((ret = _dwarf_attr_init(dbg, ds, &offset,
			    dwarf_size, cu, die, ad, ad->ad_form, 0,
			    error)) != DW_DLE_NONE)
				return (ret);
		}

		die->die_next_off = offset;
		if (search_sibling && level > 0) {
			dwarf_dealloc(dbg, die, DW_DLA_DIE);
			if (ab->ab_children == DW_CHILDREN_yes) {
				/* Advance to next DIE level. */
				level++;
			}
		} else {
			*ret_die = die;
			return (DW_DLE_NONE);
		}
	}

	return (DW_DLE_NO_ENTRY);
}

void
_dwarf_die_link(Dwarf_P_Die die, Dwarf_P_Die parent, Dwarf_P_Die child,
    Dwarf_P_Die left_sibling, Dwarf_P_Die right_sibling)
{
	Dwarf_P_Die last_child;

	assert(die != NULL);

	if (parent) {

		/* Disconnect from old parent. */
		if (die->die_parent) {
			if (die->die_parent != parent) {
				if (die->die_parent->die_child == die)
					die->die_parent->die_child = NULL;
				die->die_parent = NULL;
                     }
		}

		/* Find the last child of this parent. */
		last_child = parent->die_child;
		if (last_child) {
			while (last_child->die_right != NULL)
				last_child = last_child->die_right;
		}

		/* Connect to new parent. */
		die->die_parent = parent;

		/*
		 * Attach this DIE to the end of sibling list. If new
		 * parent doesn't have any child, set this DIE as the
		 * first child.
		 */
		if (last_child) {
			assert(last_child->die_right == NULL);
			last_child->die_right = die;
			die->die_left = last_child;
		} else
			parent->die_child = die;
	}

	if (child) {

		/* Disconnect from old child. */
		if (die->die_child) {
			if (die->die_child != child) {
				die->die_child->die_parent = NULL;
				die->die_child = NULL;
			}
		}

		/* Connect to new child. */
		die->die_child = child;
		child->die_parent = die;
	}

	if (left_sibling) {

		/* Disconnect from old left sibling. */
		if (die->die_left) {
			if (die->die_left != left_sibling) {
				die->die_left->die_right = NULL;
				die->die_left = NULL;
			}
		}

		/* Connect to new right sibling. */
		die->die_left = left_sibling;
		left_sibling->die_right = die;
	}

	if (right_sibling) {

		/* Disconnect from old right sibling. */
		if (die->die_right) {
			if (die->die_right != right_sibling) {
				die->die_right->die_left = NULL;
				die->die_right = NULL;
			}
		}

		/* Connect to new right sibling. */
		die->die_right = right_sibling;
		right_sibling->die_left = die;
	}
}

int
_dwarf_die_count_links(Dwarf_P_Die parent, Dwarf_P_Die child,
    Dwarf_P_Die left_sibling, Dwarf_P_Die right_sibling)
{
	int count;

	count = 0;

	if (parent)
		count++;
	if (child)
		count++;
	if (left_sibling)
		count++;
	if (right_sibling)
		count++;

	return (count);
}

static int
_dwarf_die_gen_recursive(Dwarf_P_Debug dbg, Dwarf_CU cu, Dwarf_Rel_Section drs,
    Dwarf_P_Die die, int pass2, Dwarf_Error *error)
{
	Dwarf_P_Section ds;
	Dwarf_Abbrev ab;
	Dwarf_Attribute at;
	Dwarf_AttrDef ad;
	int match, ret;

	ds = dbg->dbgp_info;
	assert(ds != NULL);

	if (pass2)
		goto attr_gen;

	/*
	 * Add DW_AT_sibling attribute for DIEs with children, so consumers
	 * can quickly scan chains of siblings, while ignoring the children
	 * of individual siblings.
	 */
	if (die->die_child && die->die_right) {
		if (_dwarf_attr_find(die, DW_AT_sibling) == NULL)
			(void) dwarf_add_AT_reference(dbg, die, DW_AT_sibling,
			    die->die_right, error);
	}

	/*
	 * Search abbrev list to find a matching entry.
	 */
	die->die_ab = NULL;
	for (ab = cu->cu_abbrev_hash; ab != NULL; ab = ab->ab_hh.next) {
		if (die->die_tag != ab->ab_tag)
			continue;
		if (ab->ab_children == DW_CHILDREN_no && die->die_child != NULL)
			continue;
		if (ab->ab_children == DW_CHILDREN_yes &&
		    die->die_child == NULL)
			continue;
		at = STAILQ_FIRST(&die->die_attr);
		ad = STAILQ_FIRST(&ab->ab_attrdef);
		match = 1;
		while (at != NULL && ad != NULL) {
			if (at->at_attrib != ad->ad_attrib ||
			    at->at_form != ad->ad_form) {
				match = 0;
				break;
			}
			at = STAILQ_NEXT(at, at_next);
			ad = STAILQ_NEXT(ad, ad_next);
		}
		if ((at == NULL && ad != NULL) || (at != NULL && ad == NULL))
			match = 0;
		if (match) {
			die->die_ab = ab;
			break;
		}
	}

	/*
	 * Create a new abbrev entry if we can not reuse any existing one.
	 */
	if (die->die_ab == NULL) {
		ret = _dwarf_abbrev_add(cu, ++cu->cu_abbrev_cnt, die->die_tag,
		    die->die_child != NULL ? DW_CHILDREN_yes : DW_CHILDREN_no,
		    0, &ab, error);
		if (ret != DW_DLE_NONE)
			return (ret);
		STAILQ_FOREACH(at, &die->die_attr, at_next) {
			ret = _dwarf_attrdef_add(dbg, ab, at->at_attrib,
			    at->at_form, 0, NULL, error);
			if (ret != DW_DLE_NONE)
				return (ret);
		}
		die->die_ab = ab;
	}

	die->die_offset = ds->ds_size;

	/*
	 * Transform the DIE to bytes stream.
	 */
	ret = _dwarf_write_uleb128_alloc(&ds->ds_data, &ds->ds_cap,
	    &ds->ds_size, die->die_ab->ab_entry, error);
	if (ret != DW_DLE_NONE)
		return (ret);

attr_gen:

	/* Transform the attributes of this DIE. */
	ret = _dwarf_attr_gen(dbg, ds, drs, cu, die, pass2, error);
	if (ret != DW_DLE_NONE)
		return (ret);

	/* Proceed to child DIE. */
	if (die->die_child != NULL) {
		ret = _dwarf_die_gen_recursive(dbg, cu, drs, die->die_child,
		    pass2, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	/* Proceed to sibling DIE. */
	if (die->die_right != NULL) {
		ret = _dwarf_die_gen_recursive(dbg, cu, drs, die->die_right,
		    pass2, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	/* Write a null DIE indicating the end of current level. */
	if (die->die_right == NULL) {
		ret = _dwarf_write_uleb128_alloc(&ds->ds_data, &ds->ds_cap,
		    &ds->ds_size, 0, error);
		if (ret != DW_DLE_NONE)
			return (ret);
	}

	return (DW_DLE_NONE);
}

int
_dwarf_die_gen(Dwarf_P_Debug dbg, Dwarf_CU cu, Dwarf_Rel_Section drs,
    Dwarf_Error *error)
{
	Dwarf_Abbrev ab, tab;
	Dwarf_AttrDef ad, tad;
	Dwarf_Die die;
	int ret;

	assert(dbg != NULL && cu != NULL);
	assert(dbg->dbgp_root_die != NULL);

	die = dbg->dbgp_root_die;

	/*
	 * Insert a DW_AT_stmt_list attribute into root DIE, if there are
	 * line number information.
	 */
	if (!STAILQ_EMPTY(&dbg->dbgp_lineinfo->li_lnlist))
		RCHECK(_dwarf_add_AT_dataref(dbg, die, DW_AT_stmt_list, 0, 0,
		    ".debug_line", NULL, error));

	RCHECK(_dwarf_die_gen_recursive(dbg, cu, drs, die, 0, error));

	if (cu->cu_pass2)
		RCHECK(_dwarf_die_gen_recursive(dbg, cu, drs, die, 1, error));

	return (DW_DLE_NONE);

gen_fail:

	HASH_ITER(ab_hh, cu->cu_abbrev_hash, ab, tab) {
		HASH_DELETE(ab_hh, cu->cu_abbrev_hash, ab);
		STAILQ_FOREACH_SAFE(ad, &ab->ab_attrdef, ad_next, tad) {
			STAILQ_REMOVE(&ab->ab_attrdef, ad, _Dwarf_AttrDef,
			    ad_next);
			free(ad);
		}
		free(ab);
	}

	return (ret);
}

void
_dwarf_die_pro_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_P_Die die, tdie;
	Dwarf_P_Attribute at, tat;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	STAILQ_FOREACH_SAFE(die, &dbg->dbgp_dielist, die_pro_next, tdie) {
		STAILQ_FOREACH_SAFE(at, &die->die_attr, at_next, tat) {
			STAILQ_REMOVE(&die->die_attr, at, _Dwarf_Attribute,
			    at_next);
			free(at);
		}
		free(die);
	}
}
