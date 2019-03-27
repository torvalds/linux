/*-
 * Copyright (c) 2009,2011 Kai Wang
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

ELFTC_VCSID("$Id: libdwarf_init.c 3136 2014-12-24 16:04:38Z kaiwang27 $");

static int
_dwarf_consumer_init(Dwarf_Debug dbg, Dwarf_Error *error)
{
	const Dwarf_Obj_Access_Methods *m;
	Dwarf_Obj_Access_Section sec;
	void *obj;
	Dwarf_Unsigned cnt;
	Dwarf_Half i;
	int ret;

	assert(dbg != NULL);
	assert(dbg->dbg_iface != NULL);

	m = dbg->dbg_iface->methods;
	obj = dbg->dbg_iface->object;

	assert(m != NULL);
	assert(obj != NULL);

	if (m->get_byte_order(obj) == DW_OBJECT_MSB) {
		dbg->read = _dwarf_read_msb;
		dbg->write = _dwarf_write_msb;
		dbg->decode = _dwarf_decode_msb;
	} else {
		dbg->read = _dwarf_read_lsb;
		dbg->write = _dwarf_write_lsb;
		dbg->decode = _dwarf_decode_lsb;
	}

	dbg->dbg_pointer_size = m->get_pointer_size(obj);
	dbg->dbg_offset_size = m->get_length_size(obj);

	cnt = m->get_section_count(obj);

	if (cnt == 0) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_DEBUG_INFO_NULL);
		return (DW_DLE_DEBUG_INFO_NULL);
	}

	dbg->dbg_seccnt = cnt;

	if ((dbg->dbg_section = calloc(cnt + 1, sizeof(Dwarf_Section))) ==
	    NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	for (i = 0; i < cnt; i++) {
		if (m->get_section_info(obj, i, &sec, &ret) != DW_DLV_OK) {
			DWARF_SET_ERROR(dbg, error, ret);
			return (ret);
		}

		dbg->dbg_section[i].ds_addr = sec.addr;
		dbg->dbg_section[i].ds_size = sec.size;
		dbg->dbg_section[i].ds_name = sec.name;

		if (m->load_section(obj, i, &dbg->dbg_section[i].ds_data, &ret)
		    != DW_DLV_OK) {
			DWARF_SET_ERROR(dbg, error, ret);
			return (ret);
		}
	}
	dbg->dbg_section[cnt].ds_name = NULL;

	dbg->dbg_info_sec = _dwarf_find_section(dbg, ".debug_info");

	/* Try to find the optional DWARF4 .debug_types section. */
	dbg->dbg_types_sec = _dwarf_find_next_types_section(dbg, NULL);

	/* Initialise call frame API related parameters. */
	_dwarf_frame_params_init(dbg);

	return (DW_DLV_OK);
}

static int
_dwarf_producer_init(Dwarf_Debug dbg, Dwarf_Unsigned pf, Dwarf_Error *error)
{

	/* Producer only support DWARF2 which has fixed 32bit offset. */
	dbg->dbg_offset_size = 4;

	if (pf & DW_DLC_SIZE_32 && pf & DW_DLC_SIZE_64) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if ((pf & DW_DLC_SIZE_32) == 0 && (pf & DW_DLC_SIZE_64) == 0)
		pf |= DW_DLC_SIZE_32;

	if (pf & DW_DLC_SIZE_64)
		dbg->dbg_pointer_size = 8;
	else
		dbg->dbg_pointer_size = 4;

	if (pf & DW_DLC_ISA_IA64 && pf & DW_DLC_ISA_MIPS) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if (pf & DW_DLC_ISA_IA64)
		dbg->dbgp_isa = DW_ISA_IA64;
	else
		dbg->dbgp_isa = DW_ISA_MIPS;

	if (pf & DW_DLC_TARGET_BIGENDIAN && pf & DW_DLC_TARGET_LITTLEENDIAN) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if ((pf & DW_DLC_TARGET_BIGENDIAN) == 0 &&
	    (pf & DW_DLC_TARGET_LITTLEENDIAN) == 0) {
#if ELFTC_BYTE_ORDER == ELFTC_BYTE_ORDER_BIG_ENDIAN
		pf |= DW_DLC_TARGET_BIGENDIAN;
#else
		pf |= DW_DLC_TARGET_LITTLEENDIAN;
#endif
	}

	if (pf & DW_DLC_TARGET_BIGENDIAN) {
		dbg->write = _dwarf_write_msb;
		dbg->write_alloc = _dwarf_write_msb_alloc;
	} else if (pf & DW_DLC_TARGET_LITTLEENDIAN) {
		dbg->write = _dwarf_write_lsb;
		dbg->write_alloc = _dwarf_write_lsb_alloc;
	} else
		assert(0);

	if (pf & DW_DLC_STREAM_RELOCATIONS &&
	    pf & DW_DLC_SYMBOLIC_RELOCATIONS) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLE_ARGUMENT);
	}

	if ((pf & DW_DLC_STREAM_RELOCATIONS) == 0 &&
	    (pf & DW_DLC_SYMBOLIC_RELOCATIONS) == 0)
		pf |= DW_DLC_STREAM_RELOCATIONS;

	dbg->dbgp_flags = pf;

	STAILQ_INIT(&dbg->dbgp_dielist);
	STAILQ_INIT(&dbg->dbgp_pelist);
	STAILQ_INIT(&dbg->dbgp_seclist);
	STAILQ_INIT(&dbg->dbgp_drslist);
	STAILQ_INIT(&dbg->dbgp_cielist);
	STAILQ_INIT(&dbg->dbgp_fdelist);

	if ((dbg->dbgp_lineinfo = calloc(1, sizeof(struct _Dwarf_LineInfo))) ==
	    NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INIT(&dbg->dbgp_lineinfo->li_lflist);
	STAILQ_INIT(&dbg->dbgp_lineinfo->li_lnlist);

	if ((dbg->dbgp_as = calloc(1, sizeof(struct _Dwarf_ArangeSet))) ==
	    NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}
	STAILQ_INIT(&dbg->dbgp_as->as_arlist);

	return (DW_DLE_NONE);
}

int
_dwarf_init(Dwarf_Debug dbg, Dwarf_Unsigned pro_flags, Dwarf_Handler errhand,
    Dwarf_Ptr errarg, Dwarf_Error *error)
{
	int ret;

	ret = DW_DLE_NONE;
	
	/*
	 * Set the error handler fields early, so that the application
	 * is notified of initialization errors.
	 */
	dbg->dbg_errhand = errhand;
	dbg->dbg_errarg = errarg;

	STAILQ_INIT(&dbg->dbg_cu);
	STAILQ_INIT(&dbg->dbg_tu);
	STAILQ_INIT(&dbg->dbg_rllist);
	STAILQ_INIT(&dbg->dbg_aslist);
	STAILQ_INIT(&dbg->dbg_mslist);

	if (dbg->dbg_mode == DW_DLC_READ || dbg->dbg_mode == DW_DLC_RDWR) {
		ret = _dwarf_consumer_init(dbg, error);
		if (ret != DW_DLE_NONE) {
			_dwarf_deinit(dbg);
			return (ret);
		}
	}

	if (dbg->dbg_mode == DW_DLC_WRITE) {
		ret = _dwarf_producer_init(dbg, pro_flags, error);
		if (ret != DW_DLE_NONE) {
			_dwarf_deinit(dbg);
			return (ret);
		}
	}

	/*
	 * Initialise internal string table.
	 */
	if ((ret = _dwarf_strtab_init(dbg, error)) != DW_DLE_NONE)
		return (ret);

	return (DW_DLE_NONE);
}

static void
_dwarf_producer_deinit(Dwarf_P_Debug dbg)
{

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);

	_dwarf_info_pro_cleanup(dbg);
	_dwarf_die_pro_cleanup(dbg);
	_dwarf_expr_cleanup(dbg);
	_dwarf_lineno_pro_cleanup(dbg);
	_dwarf_frame_pro_cleanup(dbg);
	_dwarf_arange_pro_cleanup(dbg);
	_dwarf_macinfo_pro_cleanup(dbg);
	_dwarf_strtab_cleanup(dbg);
	_dwarf_nametbl_pro_cleanup(&dbg->dbgp_pubs);
	_dwarf_nametbl_pro_cleanup(&dbg->dbgp_weaks);
	_dwarf_nametbl_pro_cleanup(&dbg->dbgp_funcs);
	_dwarf_nametbl_pro_cleanup(&dbg->dbgp_types);
	_dwarf_nametbl_pro_cleanup(&dbg->dbgp_vars);
	_dwarf_section_cleanup(dbg);
	_dwarf_reloc_cleanup(dbg);
}

static void
_dwarf_consumer_deinit(Dwarf_Debug dbg)
{

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_READ);

	_dwarf_info_cleanup(dbg);
	_dwarf_ranges_cleanup(dbg);
	_dwarf_frame_cleanup(dbg);
	_dwarf_arange_cleanup(dbg);
	_dwarf_macinfo_cleanup(dbg);
	_dwarf_strtab_cleanup(dbg);
	_dwarf_nametbl_cleanup(&dbg->dbg_globals);
	_dwarf_nametbl_cleanup(&dbg->dbg_pubtypes);
	_dwarf_nametbl_cleanup(&dbg->dbg_weaks);
	_dwarf_nametbl_cleanup(&dbg->dbg_funcs);
	_dwarf_nametbl_cleanup(&dbg->dbg_vars);
	_dwarf_nametbl_cleanup(&dbg->dbg_types);

	free(dbg->dbg_section);
}

void
_dwarf_deinit(Dwarf_Debug dbg)
{

	assert(dbg != NULL);

	if (dbg->dbg_mode == DW_DLC_READ)
		_dwarf_consumer_deinit(dbg);
	else if (dbg->dbg_mode == DW_DLC_WRITE)
		_dwarf_producer_deinit(dbg);
}

int
_dwarf_alloc(Dwarf_Debug *ret_dbg, int mode, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	if ((dbg = calloc(sizeof(struct _Dwarf_Debug), 1)) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	dbg->dbg_mode = mode;

	*ret_dbg = dbg;

	return (DW_DLE_NONE);
}
