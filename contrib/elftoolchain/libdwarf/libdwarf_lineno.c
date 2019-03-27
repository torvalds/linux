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

ELFTC_VCSID("$Id: libdwarf_lineno.c 3164 2015-02-19 01:20:12Z kaiwang27 $");

static int
_dwarf_lineno_add_file(Dwarf_LineInfo li, uint8_t **p, const char *compdir,
    Dwarf_Error *error, Dwarf_Debug dbg)
{
	Dwarf_LineFile lf;
	const char *dirname;
	uint8_t *src;
	int slen;

	src = *p;

	if ((lf = malloc(sizeof(struct _Dwarf_LineFile))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	lf->lf_fullpath = NULL;
	lf->lf_fname = (char *) src;
	src += strlen(lf->lf_fname) + 1;
	lf->lf_dirndx = _dwarf_decode_uleb128(&src);
	if (lf->lf_dirndx > li->li_inclen) {
		free(lf);
		DWARF_SET_ERROR(dbg, error, DW_DLE_DIR_INDEX_BAD);
		return (DW_DLE_DIR_INDEX_BAD);
	}

	/* Make full pathname if need. */
	if (*lf->lf_fname != '/') {
		dirname = compdir;
		if (lf->lf_dirndx > 0)
			dirname = li->li_incdirs[lf->lf_dirndx - 1];
		if (dirname != NULL) {
			slen = strlen(dirname) + strlen(lf->lf_fname) + 2;
			if ((lf->lf_fullpath = malloc(slen)) == NULL) {
				free(lf);
				DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
				return (DW_DLE_MEMORY);
			}
			snprintf(lf->lf_fullpath, slen, "%s/%s", dirname,
			    lf->lf_fname);
		}
	}

	lf->lf_mtime = _dwarf_decode_uleb128(&src);
	lf->lf_size = _dwarf_decode_uleb128(&src);
	STAILQ_INSERT_TAIL(&li->li_lflist, lf, lf_next);
	li->li_lflen++;

	*p = src;

	return (DW_DLE_NONE);
}

static int
_dwarf_lineno_run_program(Dwarf_CU cu, Dwarf_LineInfo li, uint8_t *p,
    uint8_t *pe, const char *compdir, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_Line ln, tln;
	uint64_t address, file, line, column, opsize;
	int is_stmt, basic_block, end_sequence;
	int ret;

#define	RESET_REGISTERS						\
	do {							\
		address	       = 0;				\
		file	       = 1;				\
		line	       = 1;				\
		column	       = 0;				\
		is_stmt	       = li->li_defstmt;		\
		basic_block    = 0;				\
		end_sequence   = 0;				\
	} while(0)

#define	APPEND_ROW						\
	do {							\
		ln = malloc(sizeof(struct _Dwarf_Line));	\
		if (ln == NULL) {				\
			ret = DW_DLE_MEMORY;			\
			DWARF_SET_ERROR(dbg, error, ret);	\
			goto prog_fail;				\
		}						\
		ln->ln_li     = li;				\
		ln->ln_addr   = address;			\
		ln->ln_symndx = 0;				\
		ln->ln_fileno = file;				\
		ln->ln_lineno = line;				\
		ln->ln_column = column;				\
		ln->ln_bblock = basic_block;			\
		ln->ln_stmt   = is_stmt;			\
		ln->ln_endseq = end_sequence;			\
		STAILQ_INSERT_TAIL(&li->li_lnlist, ln, ln_next);\
		li->li_lnlen++;					\
	} while(0)

#define	LINE(x) (li->li_lbase + (((x) - li->li_opbase) % li->li_lrange))
#define	ADDRESS(x) ((((x) - li->li_opbase) / li->li_lrange) * li->li_minlen)

	dbg = cu->cu_dbg;

	/*
	 * Set registers to their default values.
	 */
	RESET_REGISTERS;

	/*
	 * Start line number program.
	 */
	while (p < pe) {
		if (*p == 0) {

			/*
			 * Extended Opcodes.
			 */

			p++;
			opsize = _dwarf_decode_uleb128(&p);
			switch (*p) {
			case DW_LNE_end_sequence:
				p++;
				end_sequence = 1;
				APPEND_ROW;
				RESET_REGISTERS;
				break;
			case DW_LNE_set_address:
				p++;
				address = dbg->decode(&p, cu->cu_pointer_size);
				break;
			case DW_LNE_define_file:
				p++;
				ret = _dwarf_lineno_add_file(li, &p, compdir,
				    error, dbg);
				if (ret != DW_DLE_NONE)
					goto prog_fail;
				break;
			default:
				/* Unrecognized extened opcodes. */
				p += opsize;
			}

		} else if (*p > 0 && *p < li->li_opbase) {

			/*
			 * Standard Opcodes.
			 */

			switch (*p++) {
			case DW_LNS_copy:
				APPEND_ROW;
				basic_block = 0;
				break;
			case DW_LNS_advance_pc:
				address += _dwarf_decode_uleb128(&p) *
				    li->li_minlen;
				break;
			case DW_LNS_advance_line:
				line += _dwarf_decode_sleb128(&p);
				break;
			case DW_LNS_set_file:
				file = _dwarf_decode_uleb128(&p);
				break;
			case DW_LNS_set_column:
				column = _dwarf_decode_uleb128(&p);
				break;
			case DW_LNS_negate_stmt:
				is_stmt = !is_stmt;
				break;
			case DW_LNS_set_basic_block:
				basic_block = 1;
				break;
			case DW_LNS_const_add_pc:
				address += ADDRESS(255);
				break;
			case DW_LNS_fixed_advance_pc:
				address += dbg->decode(&p, 2);
				break;
			case DW_LNS_set_prologue_end:
				break;
			case DW_LNS_set_epilogue_begin:
				break;
			case DW_LNS_set_isa:
				(void) _dwarf_decode_uleb128(&p);
				break;
			default:
				/* Unrecognized extened opcodes. What to do? */
				break;
			}

		} else {

			/*
			 * Special Opcodes.
			 */

			line += LINE(*p);
			address += ADDRESS(*p);
			APPEND_ROW;
			basic_block = 0;
			p++;
		}
	}

	return (DW_DLE_NONE);

prog_fail:

	STAILQ_FOREACH_SAFE(ln, &li->li_lnlist, ln_next, tln) {
		STAILQ_REMOVE(&li->li_lnlist, ln, _Dwarf_Line, ln_next);
		free(ln);
	}

	return (ret);

#undef	RESET_REGISTERS
#undef	APPEND_ROW
#undef	LINE
#undef	ADDRESS
}

int
_dwarf_lineno_init(Dwarf_Die die, uint64_t offset, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Dwarf_Section *ds;
	Dwarf_CU cu;
	Dwarf_Attribute at;
	Dwarf_LineInfo li;
	Dwarf_LineFile lf, tlf;
	const char *compdir;
	uint64_t length, hdroff, endoff;
	uint8_t *p;
	int dwarf_size, i, ret;

	cu = die->die_cu;
	assert(cu != NULL);

	dbg = cu->cu_dbg;
	assert(dbg != NULL);

	if ((ds = _dwarf_find_section(dbg, ".debug_line")) == NULL)
		return (DW_DLE_NONE);

	/*
	 * Try to find out the dir where the CU was compiled. Later we
	 * will use the dir to create full pathnames, if need.
	 */
	compdir = NULL;
	at = _dwarf_attr_find(die, DW_AT_comp_dir);
	if (at != NULL) {
		switch (at->at_form) {
		case DW_FORM_strp:
			compdir = at->u[1].s;
			break;
		case DW_FORM_string:
			compdir = at->u[0].s;
			break;
		default:
			break;
		}
	}

	length = dbg->read(ds->ds_data, &offset, 4);
	if (length == 0xffffffff) {
		dwarf_size = 8;
		length = dbg->read(ds->ds_data, &offset, 8);
	} else
		dwarf_size = 4;

	if (length > ds->ds_size - offset) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_DEBUG_LINE_LENGTH_BAD);
		return (DW_DLE_DEBUG_LINE_LENGTH_BAD);
	}

	if ((li = calloc(1, sizeof(struct _Dwarf_LineInfo))) == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_MEMORY);
		return (DW_DLE_MEMORY);
	}

	/*
	 * Read in line number program header.
	 */
	li->li_length = length;
	endoff = offset + length;
	li->li_version = dbg->read(ds->ds_data, &offset, 2); /* FIXME: verify version */
	li->li_hdrlen = dbg->read(ds->ds_data, &offset, dwarf_size);
	hdroff = offset;
	li->li_minlen = dbg->read(ds->ds_data, &offset, 1);
	if (li->li_version == 4)
		li->li_maxop = dbg->read(ds->ds_data, &offset, 1);
	li->li_defstmt = dbg->read(ds->ds_data, &offset, 1);
	li->li_lbase = dbg->read(ds->ds_data, &offset, 1);
	li->li_lrange = dbg->read(ds->ds_data, &offset, 1);
	li->li_opbase = dbg->read(ds->ds_data, &offset, 1);
	STAILQ_INIT(&li->li_lflist);
	STAILQ_INIT(&li->li_lnlist);

	if ((int)li->li_hdrlen - 5 < li->li_opbase - 1) {
		ret = DW_DLE_DEBUG_LINE_LENGTH_BAD;
		DWARF_SET_ERROR(dbg, error, ret);
		goto fail_cleanup;
	}

	if ((li->li_oplen = malloc(li->li_opbase)) == NULL) {
		ret = DW_DLE_MEMORY;
		DWARF_SET_ERROR(dbg, error, ret);
		goto fail_cleanup;
	}

	/*
	 * Read in std opcode arg length list. Note that the first
	 * element is not used.
	 */
	for (i = 1; i < li->li_opbase; i++)
		li->li_oplen[i] = dbg->read(ds->ds_data, &offset, 1);

	/*
	 * Check how many strings in the include dir string array.
	 */
	length = 0;
	p = ds->ds_data + offset;
	while (*p != '\0') {
		while (*p++ != '\0')
			;
		length++;
	}
	li->li_inclen = length;

	/* Sanity check. */
	if (p - ds->ds_data > (int) ds->ds_size) {
		ret = DW_DLE_DEBUG_LINE_LENGTH_BAD;
		DWARF_SET_ERROR(dbg, error, ret);
		goto fail_cleanup;
	}

	if (length != 0) {
		if ((li->li_incdirs = malloc(length * sizeof(char *))) ==
		    NULL) {
			ret = DW_DLE_MEMORY;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}
	}

	/* Fill in include dir array. */
	i = 0;
	p = ds->ds_data + offset;
	while (*p != '\0') {
		li->li_incdirs[i++] = (char *) p;
		while (*p++ != '\0')
			;
	}

	p++;

	/*
	 * Process file list.
	 */
	while (*p != '\0') {
		ret = _dwarf_lineno_add_file(li, &p, compdir, error, dbg);
		if (ret != DW_DLE_NONE)
			goto fail_cleanup;
		if (p - ds->ds_data > (int) ds->ds_size) {
			ret = DW_DLE_DEBUG_LINE_LENGTH_BAD;
			DWARF_SET_ERROR(dbg, error, ret);
			goto fail_cleanup;
		}
	}

	p++;

	/* Sanity check. */
	if (p - ds->ds_data - hdroff != li->li_hdrlen) {
		ret = DW_DLE_DEBUG_LINE_LENGTH_BAD;
		DWARF_SET_ERROR(dbg, error, ret);
		goto fail_cleanup;
	}

	/*
	 * Process line number program.
	 */
	ret = _dwarf_lineno_run_program(cu, li, p, ds->ds_data + endoff, compdir,
	    error);
	if (ret != DW_DLE_NONE)
		goto fail_cleanup;

	cu->cu_lineinfo = li;

	return (DW_DLE_NONE);

fail_cleanup:

	STAILQ_FOREACH_SAFE(lf, &li->li_lflist, lf_next, tlf) {
		STAILQ_REMOVE(&li->li_lflist, lf, _Dwarf_LineFile, lf_next);
		if (lf->lf_fullpath)
			free(lf->lf_fullpath);
		free(lf);
	}

	if (li->li_oplen)
		free(li->li_oplen);
	if (li->li_incdirs)
		free(li->li_incdirs);
	free(li);

	return (ret);
}

void
_dwarf_lineno_cleanup(Dwarf_LineInfo li)
{
	Dwarf_LineFile lf, tlf;
	Dwarf_Line ln, tln;

	if (li == NULL)
		return;
	STAILQ_FOREACH_SAFE(lf, &li->li_lflist, lf_next, tlf) {
		STAILQ_REMOVE(&li->li_lflist, lf,
		    _Dwarf_LineFile, lf_next);
		if (lf->lf_fullpath)
			free(lf->lf_fullpath);
		free(lf);
	}
	STAILQ_FOREACH_SAFE(ln, &li->li_lnlist, ln_next, tln) {
		STAILQ_REMOVE(&li->li_lnlist, ln, _Dwarf_Line,
		    ln_next);
		free(ln);
	}
	if (li->li_oplen)
		free(li->li_oplen);
	if (li->li_incdirs)
		free(li->li_incdirs);
	if (li->li_lnarray)
		free(li->li_lnarray);
	if (li->li_lfnarray)
		free(li->li_lfnarray);
	free(li);
}

static int
_dwarf_lineno_gen_program(Dwarf_P_Debug dbg, Dwarf_P_Section ds,
    Dwarf_Rel_Section drs, Dwarf_Error * error)
{
	Dwarf_LineInfo li;
	Dwarf_Line ln;
	Dwarf_Unsigned address, file, line, spc;
	Dwarf_Unsigned addr0, maddr;
	Dwarf_Signed line0, column;
	int is_stmt, basic_block;
	int need_copy;
	int ret;

#define	RESET_REGISTERS						\
	do {							\
		address	       = 0;				\
		file	       = 1;				\
		line	       = 1;				\
		column	       = 0;				\
		is_stmt	       = li->li_defstmt;		\
		basic_block    = 0;				\
	} while(0)

	li = dbg->dbgp_lineinfo;
	maddr = (255 - li->li_opbase) / li->li_lrange;

	RESET_REGISTERS;

	STAILQ_FOREACH(ln, &li->li_lnlist, ln_next) {
		if (ln->ln_symndx > 0) {
			/*
			 * Generate DW_LNE_set_address extended op.
			 */
			RCHECK(WRITE_VALUE(0, 1));
			RCHECK(WRITE_ULEB128(dbg->dbg_pointer_size + 1));
			RCHECK(WRITE_VALUE(DW_LNE_set_address, 1));
			RCHECK(_dwarf_reloc_entry_add(dbg, drs, ds,
			    dwarf_drt_data_reloc, dbg->dbg_pointer_size,
			    ds->ds_size, ln->ln_symndx, ln->ln_addr,
			    NULL, error));
			address = ln->ln_addr;
			continue;
		} else if (ln->ln_endseq) {
			addr0 = (ln->ln_addr - address) / li->li_minlen;
			if (addr0 != 0) {
				RCHECK(WRITE_VALUE(DW_LNS_advance_pc, 1));
				RCHECK(WRITE_ULEB128(addr0));
			}

			/*
			 * Generate DW_LNE_end_sequence.
			 */
			RCHECK(WRITE_VALUE(0, 1));
			RCHECK(WRITE_ULEB128(1));
			RCHECK(WRITE_VALUE(DW_LNE_end_sequence, 1));
			RESET_REGISTERS;
			continue;
		}

		/*
		 * Generate standard opcodes for file, column, is_stmt or
		 * basic_block changes.
		 */
		if (ln->ln_fileno != file) {
			RCHECK(WRITE_VALUE(DW_LNS_set_file, 1));
			RCHECK(WRITE_ULEB128(ln->ln_fileno));
			file = ln->ln_fileno;
		}
		if (ln->ln_column != column) {
			RCHECK(WRITE_VALUE(DW_LNS_set_column, 1));
			RCHECK(WRITE_ULEB128(ln->ln_column));
			column = ln->ln_column;
		}
		if (ln->ln_stmt != is_stmt) {
			RCHECK(WRITE_VALUE(DW_LNS_negate_stmt, 1));
			is_stmt = ln->ln_stmt;
		}
		if (ln->ln_bblock && !basic_block) {
			RCHECK(WRITE_VALUE(DW_LNS_set_basic_block, 1));
			basic_block = 1;
		}

		/*
		 * Calculate address and line number change.
		 */
		addr0 = (ln->ln_addr - address) / li->li_minlen;
		line0 = ln->ln_lineno - line;

		if (addr0 == 0 && line0 == 0)
			continue;

		/*
		 * Check if line delta is with the range and if the special
		 * opcode can be used.
		 */
		assert(li->li_lbase <= 0);
		if (line0 >= li->li_lbase &&
		    line0 <= li->li_lbase + li->li_lrange - 1) {
			spc = (line0 - li->li_lbase) +
			    (li->li_lrange * addr0) + li->li_opbase;
			if (spc <= 255) {
				RCHECK(WRITE_VALUE(spc, 1));
				basic_block = 0;
				goto next_line;
			}
		}

		/* Generate DW_LNS_advance_line for line number change. */
		if (line0 != 0) {
			RCHECK(WRITE_VALUE(DW_LNS_advance_line, 1));
			RCHECK(WRITE_SLEB128(line0));
			line0 = 0;
			need_copy = 1;
		} else
			need_copy = basic_block;

		if (addr0 != 0) {
			/* See if it can be handled by DW_LNS_const_add_pc. */
			spc = (line0 - li->li_lbase) +
			    (li->li_lrange * (addr0 - maddr)) + li->li_opbase;
			if (addr0 >= maddr && spc <= 255) {
				RCHECK(WRITE_VALUE(DW_LNS_const_add_pc, 1));
				RCHECK(WRITE_VALUE(spc, 1));
			} else {
				/* Otherwise we use DW_LNS_advance_pc. */
				RCHECK(WRITE_VALUE(DW_LNS_advance_pc, 1));
				RCHECK(WRITE_ULEB128(addr0));
			}
		}

		if (need_copy) {
			RCHECK(WRITE_VALUE(DW_LNS_copy, 1));
			basic_block = 0;
		}

	next_line:
		address = ln->ln_addr;
		line = ln->ln_lineno;
	}

	return (DW_DLE_NONE);

gen_fail:
	return (ret);

#undef	RESET_REGISTERS
}

static uint8_t
_dwarf_get_minlen(Dwarf_P_Debug dbg)
{

	assert(dbg != NULL);

	switch (dbg->dbgp_isa) {
	case DW_ISA_ARM:
		return (2);
	case DW_ISA_X86:
	case DW_ISA_X86_64:
		return (1);
	default:
		return (4);
	}
}

static uint8_t oplen[] = {0, 1, 1, 1, 1, 0, 0, 0, 1};

int
_dwarf_lineno_gen(Dwarf_P_Debug dbg, Dwarf_Error *error)
{
	Dwarf_LineInfo li;
	Dwarf_LineFile lf;
	Dwarf_P_Section ds;
	Dwarf_Rel_Section drs;
	Dwarf_Unsigned offset;
	int i, ret;

	assert(dbg != NULL && dbg->dbgp_lineinfo != NULL);

	li = dbg->dbgp_lineinfo;
	if (STAILQ_EMPTY(&li->li_lnlist))
		return (DW_DLE_NONE);

	li->li_length = 0;
	li->li_version = 2;
	li->li_hdrlen = 0;
	li->li_minlen = _dwarf_get_minlen(dbg);
	li->li_defstmt = 1;
	li->li_lbase = -5;
	li->li_lrange = 14;
	li->li_opbase = 10;

	/* Create .debug_line section. */
	if ((ret = _dwarf_section_init(dbg, &ds, ".debug_line", 0, error)) !=
	    DW_DLE_NONE)
		return (ret);

	/* Create relocation section for .debug_line */
	if ((ret = _dwarf_reloc_section_init(dbg, &drs, ds, error)) !=
	    DW_DLE_NONE)
		goto gen_fail1;

	/* Length placeholder. (We only use 32-bit DWARF format) */
	RCHECK(WRITE_VALUE(0, 4));

	/* Write line number dwarf version. (DWARF2) */
	RCHECK(WRITE_VALUE(li->li_version, 2));

	/* Header length placeholder. */
	offset = ds->ds_size;
	RCHECK(WRITE_VALUE(li->li_hdrlen, 4));

	/* Write minimum instruction length. */
	RCHECK(WRITE_VALUE(li->li_minlen, 1));

	/*
	 * Write initial value for is_stmt. XXX Which default value we
	 * should use?
	 */
	RCHECK(WRITE_VALUE(li->li_defstmt, 1));

	/*
	 * Write line_base and line_range. FIXME These value needs to be
	 * fine tuned.
	 */
	RCHECK(WRITE_VALUE(li->li_lbase, 1));
	RCHECK(WRITE_VALUE(li->li_lrange, 1));

	/* Write opcode_base. (DWARF2) */
	RCHECK(WRITE_VALUE(li->li_opbase, 1));

	/* Write standard op length array. */
	RCHECK(WRITE_BLOCK(oplen, sizeof(oplen) / sizeof(oplen[0])));

	/* Write the list of include directories. */
	for (i = 0; (Dwarf_Unsigned) i < li->li_inclen; i++)
		RCHECK(WRITE_STRING(li->li_incdirs[i]));
	RCHECK(WRITE_VALUE(0, 1));

	/* Write the list of filenames. */
	STAILQ_FOREACH(lf, &li->li_lflist, lf_next) {
		RCHECK(WRITE_STRING(lf->lf_fname));
		RCHECK(WRITE_ULEB128(lf->lf_dirndx));
		RCHECK(WRITE_ULEB128(lf->lf_mtime));
		RCHECK(WRITE_ULEB128(lf->lf_size));
	}
	RCHECK(WRITE_VALUE(0, 1));

	/* Fill in the header length. */
	li->li_hdrlen = ds->ds_size - offset - 4;
	dbg->write(ds->ds_data, &offset, li->li_hdrlen, 4);

	/* Generate the line number program. */
	RCHECK(_dwarf_lineno_gen_program(dbg, ds, drs, error));

	/* Fill in the length of this line info. */
	li->li_length = ds->ds_size - 4;
	offset = 0;
	dbg->write(ds->ds_data, &offset, li->li_length, 4);

	/* Notify the creation of .debug_line ELF section. */
	RCHECK(_dwarf_section_callback(dbg, ds, SHT_PROGBITS, 0, 0, 0, error));

	/* Finalize relocation section for .debug_line. */
	RCHECK(_dwarf_reloc_section_finalize(dbg, drs, error));

	return (DW_DLE_NONE);

gen_fail:
	_dwarf_reloc_section_free(dbg, &drs);

gen_fail1:
	_dwarf_section_free(dbg, &ds);

	return (ret);
}

void
_dwarf_lineno_pro_cleanup(Dwarf_P_Debug dbg)
{
	Dwarf_LineInfo li;
	Dwarf_LineFile lf, tlf;
	Dwarf_Line ln, tln;
	int i;

	assert(dbg != NULL && dbg->dbg_mode == DW_DLC_WRITE);
	if (dbg->dbgp_lineinfo == NULL)
		return;

	li = dbg->dbgp_lineinfo;
	STAILQ_FOREACH_SAFE(lf, &li->li_lflist, lf_next, tlf) {
		STAILQ_REMOVE(&li->li_lflist, lf, _Dwarf_LineFile,
		    lf_next);
		if (lf->lf_fname)
			free(lf->lf_fname);
		free(lf);
	}
	STAILQ_FOREACH_SAFE(ln, &li->li_lnlist, ln_next, tln) {
		STAILQ_REMOVE(&li->li_lnlist, ln, _Dwarf_Line, ln_next);
		free(ln);
	}
	if (li->li_incdirs) {
		for (i = 0; (Dwarf_Unsigned) i < li->li_inclen; i++)
			free(li->li_incdirs[i]);
		free(li->li_incdirs);
	}
	free(li);
	dbg->dbgp_lineinfo = NULL;
}
