/*
 * cload.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/slab.h>

#include "header.h"

#include "module_list.h"
#define LINKER_MODULES_HEADER ("_" MODULES_HEADER)

/*
 * forward references
 */
static void dload_symbols(struct dload_state *dlthis);
static void dload_data(struct dload_state *dlthis);
static void allocate_sections(struct dload_state *dlthis);
static void string_table_free(struct dload_state *dlthis);
static void symbol_table_free(struct dload_state *dlthis);
static void section_table_free(struct dload_state *dlthis);
static void init_module_handle(struct dload_state *dlthis);
#if BITS_PER_AU > BITS_PER_BYTE
static char *unpack_name(struct dload_state *dlthis, u32 soffset);
#endif

static const char cinitname[] = { ".cinit" };
static const char loader_dllview_root[] = { "?DLModules?" };

/*
 * Error strings
 */
static const char readstrm[] = { "Error reading %s from input stream" };
static const char err_alloc[] = { "Syms->dload_allocate( %d ) failed" };
static const char tgtalloc[] = {
	"Target memory allocate failed, section %s size " FMT_UI32 };
static const char initfail[] = { "%s to target address " FMT_UI32 " failed" };
static const char dlvwrite[] = { "Write to DLLview list failed" };
static const char iconnect[] = { "Connect call to init interface failed" };
static const char err_checksum[] = { "Checksum failed on %s" };

/*************************************************************************
 * Procedure dload_error
 *
 * Parameters:
 *	errtxt	description of the error, printf style
 *	...		additional information
 *
 * Effect:
 *	Reports or records the error as appropriate.
 *********************************************************************** */
void dload_error(struct dload_state *dlthis, const char *errtxt, ...)
{
	va_list args;

	va_start(args, errtxt);
	dlthis->mysym->error_report(dlthis->mysym, errtxt, args);
	va_end(args);
	dlthis->dload_errcount += 1;

}				/* dload_error */

#define DL_ERROR(zza, zzb) dload_error(dlthis, zza, zzb)

/*************************************************************************
 * Procedure dload_syms_error
 *
 * Parameters:
 *	errtxt	description of the error, printf style
 *	...		additional information
 *
 * Effect:
 *	Reports or records the error as appropriate.
 *********************************************************************** */
void dload_syms_error(struct dynamic_loader_sym *syms, const char *errtxt, ...)
{
	va_list args;

	va_start(args, errtxt);
	syms->error_report(syms, errtxt, args);
	va_end(args);
}

/*************************************************************************
 * Procedure dynamic_load_module
 *
 * Parameters:
 *	module	The input stream that supplies the module image
 *	syms	Host-side symbol table and malloc/free functions
 *	alloc	Target-side memory allocation
 *	init	Target-side memory initialization
 *	options	Option flags DLOAD_*
 *	mhandle	A module handle for use with Dynamic_Unload
 *
 * Effect:
 *	The module image is read using *module.  Target storage for the new
 *	image is
 * obtained from *alloc.  Symbols defined and referenced by the module are
 * managed using *syms.  The image is then relocated and references
 *	resolved as necessary, and the resulting executable bits are placed
 *	into target memory using *init.
 *
 * Returns:
 *	On a successful load, a module handle is placed in *mhandle,
 *	and zero is returned.  On error, the number of errors detected is
 *	returned.  Individual errors are reported during the load process
 *	using syms->error_report().
 ********************************************************************** */
int dynamic_load_module(struct dynamic_loader_stream *module,
			struct dynamic_loader_sym *syms,
			struct dynamic_loader_allocate *alloc,
			struct dynamic_loader_initialize *init,
			unsigned options, void **mhandle)
{
	register unsigned *dp, sz;
	struct dload_state dl_state;	/* internal state for this call */

	/* blast our internal state */
	dp = (unsigned *)&dl_state;
	for (sz = sizeof(dl_state) / sizeof(unsigned); sz > 0; sz -= 1)
		*dp++ = 0;

	/* Enable _only_ BSS initialization if enabled by user */
	if ((options & DLOAD_INITBSS) == DLOAD_INITBSS)
		dl_state.myoptions = DLOAD_INITBSS;

	/* Check that mandatory arguments are present */
	if (!module || !syms) {
		dload_error(&dl_state, "Required parameter is NULL");
	} else {
		dl_state.strm = module;
		dl_state.mysym = syms;
		dload_headers(&dl_state);
		if (!dl_state.dload_errcount)
			dload_strings(&dl_state, false);
		if (!dl_state.dload_errcount)
			dload_sections(&dl_state);

		if (init && !dl_state.dload_errcount) {
			if (init->connect(init)) {
				dl_state.myio = init;
				dl_state.myalloc = alloc;
				/* do now, before reducing symbols */
				allocate_sections(&dl_state);
			} else
				dload_error(&dl_state, iconnect);
		}

		if (!dl_state.dload_errcount) {
			/* fix up entry point address */
			unsigned sref = dl_state.dfile_hdr.df_entry_secn - 1;

			if (sref < dl_state.allocated_secn_count)
				dl_state.dfile_hdr.df_entrypt +=
				    dl_state.ldr_sections[sref].run_addr;

			dload_symbols(&dl_state);
		}

		if (init && !dl_state.dload_errcount)
			dload_data(&dl_state);

		init_module_handle(&dl_state);

		/* dl_state.myio is init or 0 at this point. */
		if (dl_state.myio) {
			if ((!dl_state.dload_errcount) &&
			    (dl_state.dfile_hdr.df_entry_secn != DN_UNDEF) &&
			    (!init->execute(init,
					    dl_state.dfile_hdr.df_entrypt)))
				dload_error(&dl_state, "Init->Execute Failed");
			init->release(init);
		}

		symbol_table_free(&dl_state);
		section_table_free(&dl_state);
		string_table_free(&dl_state);
		dload_tramp_cleanup(&dl_state);

		if (dl_state.dload_errcount) {
			dynamic_unload_module(dl_state.myhandle, syms, alloc,
					      init);
			dl_state.myhandle = NULL;
		}
	}

	if (mhandle)
		*mhandle = dl_state.myhandle;	/* give back the handle */

	return dl_state.dload_errcount;
}				/* DLOAD_File */

/*************************************************************************
 * Procedure dynamic_open_module
 *
 * Parameters:
 *      module  The input stream that supplies the module image
 *      syms    Host-side symbol table and malloc/free functions
 *      alloc   Target-side memory allocation
 *      init    Target-side memory initialization
 *      options Option flags DLOAD_*
 *      mhandle A module handle for use with Dynamic_Unload
 *
 * Effect:
 *      The module image is read using *module.  Target storage for the new
 *      image is
 * 	obtained from *alloc.  Symbols defined and referenced by the module are
 * 	managed using *syms.  The image is then relocated and references
 *      resolved as necessary, and the resulting executable bits are placed
 *      into target memory using *init.
 *
 * Returns:
 *      On a successful load, a module handle is placed in *mhandle,
 *      and zero is returned.  On error, the number of errors detected is
 *      returned.  Individual errors are reported during the load process
 *      using syms->error_report().
 ********************************************************************** */
int
dynamic_open_module(struct dynamic_loader_stream *module,
		    struct dynamic_loader_sym *syms,
		    struct dynamic_loader_allocate *alloc,
		    struct dynamic_loader_initialize *init,
		    unsigned options, void **mhandle)
{
	register unsigned *dp, sz;
	struct dload_state dl_state;	/* internal state for this call */

	/* blast our internal state */
	dp = (unsigned *)&dl_state;
	for (sz = sizeof(dl_state) / sizeof(unsigned); sz > 0; sz -= 1)
		*dp++ = 0;

	/* Enable _only_ BSS initialization if enabled by user */
	if ((options & DLOAD_INITBSS) == DLOAD_INITBSS)
		dl_state.myoptions = DLOAD_INITBSS;

	/* Check that mandatory arguments are present */
	if (!module || !syms) {
		dload_error(&dl_state, "Required parameter is NULL");
	} else {
		dl_state.strm = module;
		dl_state.mysym = syms;
		dload_headers(&dl_state);
		if (!dl_state.dload_errcount)
			dload_strings(&dl_state, false);
		if (!dl_state.dload_errcount)
			dload_sections(&dl_state);

		if (init && !dl_state.dload_errcount) {
			if (init->connect(init)) {
				dl_state.myio = init;
				dl_state.myalloc = alloc;
				/* do now, before reducing symbols */
				allocate_sections(&dl_state);
			} else
				dload_error(&dl_state, iconnect);
		}

		if (!dl_state.dload_errcount) {
			/* fix up entry point address */
			unsigned sref = dl_state.dfile_hdr.df_entry_secn - 1;

			if (sref < dl_state.allocated_secn_count)
				dl_state.dfile_hdr.df_entrypt +=
				    dl_state.ldr_sections[sref].run_addr;

			dload_symbols(&dl_state);
		}

		init_module_handle(&dl_state);

		/* dl_state.myio is either 0 or init at this point. */
		if (dl_state.myio) {
			if ((!dl_state.dload_errcount) &&
			    (dl_state.dfile_hdr.df_entry_secn != DN_UNDEF) &&
			    (!init->execute(init,
					    dl_state.dfile_hdr.df_entrypt)))
				dload_error(&dl_state, "Init->Execute Failed");
			init->release(init);
		}

		symbol_table_free(&dl_state);
		section_table_free(&dl_state);
		string_table_free(&dl_state);

		if (dl_state.dload_errcount) {
			dynamic_unload_module(dl_state.myhandle, syms, alloc,
					      init);
			dl_state.myhandle = NULL;
		}
	}

	if (mhandle)
		*mhandle = dl_state.myhandle;	/* give back the handle */

	return dl_state.dload_errcount;
}				/* DLOAD_File */

/*************************************************************************
 * Procedure dload_headers
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Loads the DOFF header and verify record.  Deals with any byte-order
 * issues and checks them for validity.
 *********************************************************************** */
#define COMBINED_HEADER_SIZE (sizeof(struct doff_filehdr_t)+ \
			     sizeof(struct doff_verify_rec_t))

void dload_headers(struct dload_state *dlthis)
{
	u32 map;

	/* Read the header and the verify record as one.  If we don't get it
	   all, we're done */
	if (dlthis->strm->read_buffer(dlthis->strm, &dlthis->dfile_hdr,
				      COMBINED_HEADER_SIZE) !=
	    COMBINED_HEADER_SIZE) {
		DL_ERROR(readstrm, "File Headers");
		return;
	}
	/*
	 * Verify that we have the byte order of the file correct.
	 * If not, must fix it before we can continue
	 */
	map = REORDER_MAP(dlthis->dfile_hdr.df_byte_reshuffle);
	if (map != REORDER_MAP(BYTE_RESHUFFLE_VALUE)) {
		/* input is either byte-shuffled or bad */
		if ((map & 0xFCFCFCFC) == 0) {	/* no obviously bogus bits */
			dload_reorder(&dlthis->dfile_hdr, COMBINED_HEADER_SIZE,
				      map);
		}
		if (dlthis->dfile_hdr.df_byte_reshuffle !=
		    BYTE_RESHUFFLE_VALUE) {
			/* didn't fix the problem, the byte swap map is bad */
			dload_error(dlthis,
				    "Bad byte swap map " FMT_UI32 " in header",
				    dlthis->dfile_hdr.df_byte_reshuffle);
			return;
		}
		dlthis->reorder_map = map;	/* keep map for future use */
	}

	/*
	 * Verify checksum of header and verify record
	 */
	if (~dload_checksum(&dlthis->dfile_hdr,
			    sizeof(struct doff_filehdr_t)) ||
	    ~dload_checksum(&dlthis->verify,
			    sizeof(struct doff_verify_rec_t))) {
		DL_ERROR(err_checksum, "header or verify record");
		return;
	}
#if HOST_ENDIANNESS
	dlthis->dfile_hdr.df_byte_reshuffle = map;	/* put back for later */
#endif

	/* Check for valid target ID */
	if ((dlthis->dfile_hdr.df_target_id != TARGET_ID) &&
	    -(dlthis->dfile_hdr.df_target_id != TMS470_ID)) {
		dload_error(dlthis, "Bad target ID 0x%x and TARGET_ID 0x%x",
			    dlthis->dfile_hdr.df_target_id, TARGET_ID);
		return;
	}
	/* Check for valid file format */
	if (dlthis->dfile_hdr.df_doff_version != DOFF0) {
		dload_error(dlthis, "Bad DOFF version 0x%x",
			    dlthis->dfile_hdr.df_doff_version);
		return;
	}

	/*
	 * Apply reasonableness checks to count fields
	 */
	if (dlthis->dfile_hdr.df_strtab_size > MAX_REASONABLE_STRINGTAB) {
		dload_error(dlthis, "Excessive string table size " FMT_UI32,
			    dlthis->dfile_hdr.df_strtab_size);
		return;
	}
	if (dlthis->dfile_hdr.df_no_scns > MAX_REASONABLE_SECTIONS) {
		dload_error(dlthis, "Excessive section count 0x%x",
			    dlthis->dfile_hdr.df_no_scns);
		return;
	}
#ifndef TARGET_ENDIANNESS
	/*
	 * Check that endianness does not disagree with explicit specification
	 */
	if ((dlthis->dfile_hdr.df_flags >> ALIGN_COFF_ENDIANNESS) &
	    dlthis->myoptions & ENDIANNESS_MASK) {
		dload_error(dlthis,
			    "Input endianness disagrees with specified option");
		return;
	}
	dlthis->big_e_target = dlthis->dfile_hdr.df_flags & DF_BIG;
#endif

}				/* dload_headers */

/*	COFF Section Processing
 *
 *	COFF sections are read in and retained intact.  Each record is embedded
 * 	in a new structure that records the updated load and
 * 	run addresses of the section */

static const char secn_errid[] = { "section" };

/*************************************************************************
 * Procedure dload_sections
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Loads the section records into an internal table.
 *********************************************************************** */
void dload_sections(struct dload_state *dlthis)
{
	s16 siz;
	struct doff_scnhdr_t *shp;
	unsigned nsecs = dlthis->dfile_hdr.df_no_scns;

	/* allocate space for the DOFF section records */
	siz = nsecs * sizeof(struct doff_scnhdr_t);
	shp =
	    (struct doff_scnhdr_t *)dlthis->mysym->dload_allocate(dlthis->mysym,
								  siz);
	if (!shp) {		/* not enough storage */
		DL_ERROR(err_alloc, siz);
		return;
	}
	dlthis->sect_hdrs = shp;

	/* read in the section records */
	if (dlthis->strm->read_buffer(dlthis->strm, shp, siz) != siz) {
		DL_ERROR(readstrm, secn_errid);
		return;
	}

	/* if we need to fix up byte order, do it now */
	if (dlthis->reorder_map)
		dload_reorder(shp, siz, dlthis->reorder_map);

	/* check for validity */
	if (~dload_checksum(dlthis->sect_hdrs, siz) !=
	    dlthis->verify.dv_scn_rec_checksum) {
		DL_ERROR(err_checksum, secn_errid);
		return;
	}

}				/* dload_sections */

/*****************************************************************************
 * Procedure allocate_sections
 *
 * Parameters:
 *	alloc	target memory allocator class
 *
 * Effect:
 *	Assigns new (target) addresses for sections
 **************************************************************************** */
static void allocate_sections(struct dload_state *dlthis)
{
	u16 curr_sect, nsecs, siz;
	struct doff_scnhdr_t *shp;
	struct ldr_section_info *asecs;
	struct my_handle *hndl;

	nsecs = dlthis->dfile_hdr.df_no_scns;
	if (!nsecs)
		return;
	if ((dlthis->myalloc == NULL) &&
	    (dlthis->dfile_hdr.df_target_scns > 0)) {
		DL_ERROR("Arg 3 (alloc) required but NULL", 0);
		return;
	}
	/*
	 * allocate space for the module handle, which we will keep for unload
	 * purposes include an additional section store for an auto-generated
	 * trampoline section in case we need it.
	 */
	siz = (dlthis->dfile_hdr.df_target_scns + 1) *
	    sizeof(struct ldr_section_info) + MY_HANDLE_SIZE;

	hndl =
	    (struct my_handle *)dlthis->mysym->dload_allocate(dlthis->mysym,
							      siz);
	if (!hndl) {		/* not enough storage */
		DL_ERROR(err_alloc, siz);
		return;
	}
	/* initialize the handle header */
	hndl->dm.next = hndl->dm.prev = hndl;	/* circular list */
	hndl->dm.root = NULL;
	hndl->dm.dbthis = 0;
	dlthis->myhandle = hndl;	/* save away for return */
	/* pointer to the section list of allocated sections */
	dlthis->ldr_sections = asecs = hndl->secns;
	/* * Insert names into all sections, make copies of
	   the sections we allocate */
	shp = dlthis->sect_hdrs;
	for (curr_sect = 0; curr_sect < nsecs; curr_sect++) {
		u32 soffset = shp->ds_offset;
#if BITS_PER_AU <= BITS_PER_BYTE
		/* attempt to insert the name of this section */
		if (soffset < dlthis->dfile_hdr.df_strtab_size)
			((struct ldr_section_info *)shp)->name =
				dlthis->str_head + soffset;
		else {
			dload_error(dlthis, "Bad name offset in section %d",
				    curr_sect);
			((struct ldr_section_info *)shp)->name = NULL;
		}
#endif
		/* allocate target storage for sections that require it */
		if (ds_needs_allocation(shp)) {
			*asecs = *(struct ldr_section_info *)shp;
			asecs->context = 0;	/* zero the context field */
#if BITS_PER_AU > BITS_PER_BYTE
			asecs->name = unpack_name(dlthis, soffset);
			dlthis->debug_string_size = soffset + dlthis->temp_len;
#else
			dlthis->debug_string_size = soffset;
#endif
			if (dlthis->myalloc != NULL) {
				if (!dlthis->myalloc->
				    dload_allocate(dlthis->myalloc, asecs,
						   ds_alignment(asecs->type))) {
					dload_error(dlthis, tgtalloc,
						    asecs->name, asecs->size);
					return;
				}
			}
			/* keep address deltas in original section table */
			shp->ds_vaddr = asecs->load_addr - shp->ds_vaddr;
			shp->ds_paddr = asecs->run_addr - shp->ds_paddr;
			dlthis->allocated_secn_count += 1;
		}		/* allocate target storage */
		shp += 1;
		asecs += 1;
	}
#if BITS_PER_AU <= BITS_PER_BYTE
	dlthis->debug_string_size +=
	    strlen(dlthis->str_head + dlthis->debug_string_size) + 1;
#endif
}				/* allocate sections */

/*************************************************************************
 * Procedure section_table_free
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Frees any state used by the symbol table.
 *
 * WARNING:
 *	This routine is not allowed to declare errors!
 *********************************************************************** */
static void section_table_free(struct dload_state *dlthis)
{
	struct doff_scnhdr_t *shp;

	shp = dlthis->sect_hdrs;
	if (shp)
		dlthis->mysym->dload_deallocate(dlthis->mysym, shp);

}				/* section_table_free */

/*************************************************************************
 * Procedure dload_strings
 *
 * Parameters:
 *  sec_names_only   If true only read in the "section names"
 *		     portion of the string table
 *
 * Effect:
 *	Loads the DOFF string table into memory. DOFF keeps all strings in a
 * big unsorted array.  We just read that array into memory in bulk.
 *********************************************************************** */
static const char stringtbl[] = { "string table" };

void dload_strings(struct dload_state *dlthis, bool sec_names_only)
{
	u32 ssiz;
	char *strbuf;

	if (sec_names_only) {
		ssiz = BYTE_TO_HOST(DOFF_ALIGN
				    (dlthis->dfile_hdr.df_scn_name_size));
	} else {
		ssiz = BYTE_TO_HOST(DOFF_ALIGN
				    (dlthis->dfile_hdr.df_strtab_size));
	}
	if (ssiz == 0)
		return;

	/* get some memory for the string table */
#if BITS_PER_AU > BITS_PER_BYTE
	strbuf = (char *)dlthis->mysym->dload_allocate(dlthis->mysym, ssiz +
						       dlthis->dfile_hdr.
						       df_max_str_len);
#else
	strbuf = (char *)dlthis->mysym->dload_allocate(dlthis->mysym, ssiz);
#endif
	if (strbuf == NULL) {
		DL_ERROR(err_alloc, ssiz);
		return;
	}
	dlthis->str_head = strbuf;
#if BITS_PER_AU > BITS_PER_BYTE
	dlthis->str_temp = strbuf + ssiz;
#endif
	/* read in the strings and verify them */
	if ((unsigned)(dlthis->strm->read_buffer(dlthis->strm, strbuf,
						 ssiz)) != ssiz) {
		DL_ERROR(readstrm, stringtbl);
	}
	/* if we need to fix up byte order, do it now */
#ifndef _BIG_ENDIAN
	if (dlthis->reorder_map)
		dload_reorder(strbuf, ssiz, dlthis->reorder_map);

	if ((!sec_names_only) && (~dload_checksum(strbuf, ssiz) !=
				  dlthis->verify.dv_str_tab_checksum)) {
		DL_ERROR(err_checksum, stringtbl);
	}
#else
	if (dlthis->dfile_hdr.df_byte_reshuffle !=
	    HOST_BYTE_ORDER(REORDER_MAP(BYTE_RESHUFFLE_VALUE))) {
		/* put strings in big-endian order, not in PC order */
		dload_reorder(strbuf, ssiz,
			      HOST_BYTE_ORDER(dlthis->
					      dfile_hdr.df_byte_reshuffle));
	}
	if ((!sec_names_only) && (~dload_reverse_checksum(strbuf, ssiz) !=
				  dlthis->verify.dv_str_tab_checksum)) {
		DL_ERROR(err_checksum, stringtbl);
	}
#endif
}				/* dload_strings */

/*************************************************************************
 * Procedure string_table_free
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Frees any state used by the string table.
 *
 * WARNING:
 *	This routine is not allowed to declare errors!
 ************************************************************************ */
static void string_table_free(struct dload_state *dlthis)
{
	if (dlthis->str_head)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						dlthis->str_head);

}				/* string_table_free */

/*
 * Symbol Table Maintenance Functions
 *
 * COFF symbols are read by dload_symbols(), which is called after
 * sections have been allocated.  Symbols which might be used in
 * relocation (ie, not debug info) are retained in an internal temporary
 * compressed table (type local_symbol). A particular symbol is recovered
 * by index by calling dload_find_symbol().  dload_find_symbol
 * reconstructs a more explicit representation (type SLOTVEC) which is
 * used by reloc.c
 */
/* real size of debug header */
#define DBG_HDR_SIZE (sizeof(struct dll_module) - sizeof(struct dll_sect))

static const char sym_errid[] = { "symbol" };

/**************************************************************************
 * Procedure dload_symbols
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Reads in symbols and retains ones that might be needed for relocation
 * purposes.
 *********************************************************************** */
/* size of symbol buffer no bigger than target data buffer, to limit stack
 * usage */
#define MY_SYM_BUF_SIZ (BYTE_TO_HOST(IMAGE_PACKET_SIZE)/\
			sizeof(struct doff_syment_t))

static void dload_symbols(struct dload_state *dlthis)
{
	u32 sym_count, siz, dsiz, symbols_left;
	u32 checks;
	struct local_symbol *sp;
	struct dynload_symbol *symp;
	struct dynload_symbol *newsym;
	struct doff_syment_t *my_sym_buf;

	sym_count = dlthis->dfile_hdr.df_no_syms;
	if (sym_count == 0)
		return;

	/*
	 * We keep a local symbol table for all of the symbols in the input.
	 * This table contains only section & value info, as we do not have
	 * to do any name processing for locals.  We reuse this storage
	 * as a temporary for .dllview record construction.
	 * Allocate storage for the whole table.  Add 1 to the section count
	 * in case a trampoline section is auto-generated as well as the
	 * size of the trampoline section name so DLLView doesn't get lost.
	 */

	siz = sym_count * sizeof(struct local_symbol);
	dsiz = DBG_HDR_SIZE +
	    (sizeof(struct dll_sect) * dlthis->allocated_secn_count) +
	    BYTE_TO_HOST_ROUND(dlthis->debug_string_size + 1);
	if (dsiz > siz)
		siz = dsiz;	/* larger of symbols and .dllview temp */
	sp = (struct local_symbol *)dlthis->mysym->dload_allocate(dlthis->mysym,
								  siz);
	if (!sp) {
		DL_ERROR(err_alloc, siz);
		return;
	}
	dlthis->local_symtab = sp;
	/* Read the symbols in the input, store them in the table, and post any
	 * globals to the global symbol table.  In the process, externals
	 become defined from the global symbol table */
	checks = dlthis->verify.dv_sym_tab_checksum;
	symbols_left = sym_count;

	my_sym_buf = kzalloc(sizeof(*my_sym_buf) * MY_SYM_BUF_SIZ, GFP_KERNEL);
	if (!my_sym_buf)
		return;

	do {			/* read all symbols */
		char *sname;
		u32 val;
		s32 delta;
		struct doff_syment_t *input_sym;
		unsigned syms_in_buf;

		input_sym = my_sym_buf;
		syms_in_buf = symbols_left > MY_SYM_BUF_SIZ ?
		    MY_SYM_BUF_SIZ : symbols_left;
		siz = syms_in_buf * sizeof(struct doff_syment_t);
		if (dlthis->strm->read_buffer(dlthis->strm, input_sym, siz) !=
		    siz) {
			DL_ERROR(readstrm, sym_errid);
			goto free_sym_buf;
		}
		if (dlthis->reorder_map)
			dload_reorder(input_sym, siz, dlthis->reorder_map);

		checks += dload_checksum(input_sym, siz);
		do {		/* process symbols in buffer */
			symbols_left -= 1;
			/* attempt to derive the name of this symbol */
			sname = NULL;
			if (input_sym->dn_offset > 0) {
#if BITS_PER_AU <= BITS_PER_BYTE
				if ((u32) input_sym->dn_offset <
				    dlthis->dfile_hdr.df_strtab_size)
					sname = dlthis->str_head +
					    BYTE_TO_HOST(input_sym->dn_offset);
				else
					dload_error(dlthis,
						    "Bad name offset in symbol "
						    " %d", symbols_left);
#else
				sname = unpack_name(dlthis,
						    input_sym->dn_offset);
#endif
			}
			val = input_sym->dn_value;
			delta = 0;
			sp->sclass = input_sym->dn_sclass;
			sp->secnn = input_sym->dn_scnum;
			/* if this is an undefined symbol,
			 * define it (or fail) now */
			if (sp->secnn == DN_UNDEF) {
				/* pointless for static undefined */
				if (input_sym->dn_sclass != DN_EXT)
					goto loop_cont;

				/* try to define symbol from previously
				 * loaded images */
				symp = dlthis->mysym->find_matching_symbol
				    (dlthis->mysym, sname);
				if (!symp) {
					DL_ERROR
					    ("Undefined external symbol %s",
					     sname);
					goto loop_cont;
				}
				val = delta = symp->value;
#ifdef ENABLE_TRAMP_DEBUG
				dload_syms_error(dlthis->mysym,
						 "===> ext sym [%s] at %x",
						 sname, val);
#endif

				goto loop_cont;
			}
			/* symbol defined by this module */
			if (sp->secnn > 0) {
				/* symbol references a section */
				if ((unsigned)sp->secnn <=
				    dlthis->allocated_secn_count) {
					/* section was allocated */
					struct doff_scnhdr_t *srefp =
					    &dlthis->sect_hdrs[sp->secnn - 1];

					if (input_sym->dn_sclass ==
					    DN_STATLAB ||
					    input_sym->dn_sclass == DN_EXTLAB) {
						/* load */
						delta = srefp->ds_vaddr;
					} else {
						/* run */
						delta = srefp->ds_paddr;
					}
					val += delta;
				}
				goto loop_itr;
			}
			/* This symbol is an absolute symbol */
			if (sp->secnn == DN_ABS && ((sp->sclass == DN_EXT) ||
						    (sp->sclass ==
						     DN_EXTLAB))) {
				symp =
				    dlthis->mysym->find_matching_symbol(dlthis->
									mysym,
									sname);
				if (!symp)
					goto loop_itr;
				/* This absolute symbol is already defined. */
				if (symp->value == input_sym->dn_value) {
					/* If symbol values are equal, continue
					 * but don't add to the global symbol
					 * table */
					sp->value = val;
					sp->delta = delta;
					sp += 1;
					input_sym += 1;
					continue;
				} else {
					/* If symbol values are not equal,
					 * return with redefinition error */
					DL_ERROR("Absolute symbol %s is "
						 "defined multiple times with "
						 "different values", sname);
					goto free_sym_buf;
				}
			}
loop_itr:
			/* if this is a global symbol, post it to the
			 * global table */
			if (input_sym->dn_sclass == DN_EXT ||
			    input_sym->dn_sclass == DN_EXTLAB) {
				/* Keep this global symbol for subsequent
				 * modules. Don't complain on error, to allow
				 * symbol API to suppress global symbols */
				if (!sname)
					goto loop_cont;

				newsym = dlthis->mysym->add_to_symbol_table
				    (dlthis->mysym, sname,
				     (unsigned)dlthis->myhandle);
				if (newsym)
					newsym->value = val;

			}	/* global */
loop_cont:
			sp->value = val;
			sp->delta = delta;
			sp += 1;
			input_sym += 1;
		} while ((syms_in_buf -= 1) > 0);	/* process sym in buf */
	} while (symbols_left > 0);	/* read all symbols */
	if (~checks)
		dload_error(dlthis, "Checksum of symbols failed");

free_sym_buf:
	kfree(my_sym_buf);
	return;
}				/* dload_symbols */

/*****************************************************************************
 * Procedure symbol_table_free
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Frees any state used by the symbol table.
 *
 * WARNING:
 *	This routine is not allowed to declare errors!
 **************************************************************************** */
static void symbol_table_free(struct dload_state *dlthis)
{
	if (dlthis->local_symtab) {
		if (dlthis->dload_errcount) {	/* blow off our symbols */
			dlthis->mysym->purge_symbol_table(dlthis->mysym,
							  (unsigned)
							  dlthis->myhandle);
		}
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						dlthis->local_symtab);
	}
}				/* symbol_table_free */

/* .cinit Processing
 *
 * The dynamic loader does .cinit interpretation.  cload_cinit()
 * acts as a special write-to-target function, in that it takes relocated
 * data from the normal data flow, and interprets it as .cinit actions.
 * Because the normal data flow does not  necessarily process the whole
 * .cinit section in one buffer, cload_cinit() must be prepared to
 * interpret the data piecemeal.  A state machine is used for this
 * purpose.
 */

/* The following are only for use by reloc.c and things it calls */
static const struct ldr_section_info cinit_info_init = { cinitname, 0, 0,
	(ldr_addr)-1, 0, DLOAD_BSS, 0
};

/*************************************************************************
 * Procedure cload_cinit
 *
 * Parameters:
 *	ipacket		Pointer to data packet to be loaded
 *
 * Effect:
 *	Interprets the data in the buffer as .cinit data, and performs the
 * appropriate initializations.
 *********************************************************************** */
static void cload_cinit(struct dload_state *dlthis,
			struct image_packet_t *ipacket)
{
#if TDATA_TO_HOST(CINIT_COUNT)*BITS_PER_AU > 16
	s32 init_count, left;
#else
	s16 init_count, left;
#endif
	unsigned char *pktp = ipacket->img_data;
	unsigned char *pktend = pktp + BYTE_TO_HOST_ROUND(ipacket->packet_size);
	int temp;
	ldr_addr atmp;
	struct ldr_section_info cinit_info;

	/*  PROCESS ALL THE INITIALIZATION RECORDS THE BUFFER. */
	while (true) {
		left = pktend - pktp;
		switch (dlthis->cinit_state) {
		case CI_COUNT:	/* count field */
			if (left < TDATA_TO_HOST(CINIT_COUNT))
				goto loopexit;
			temp = dload_unpack(dlthis, (tgt_au_t *) pktp,
					    CINIT_COUNT * TDATA_AU_BITS, 0,
					    ROP_SGN);
			pktp += TDATA_TO_HOST(CINIT_COUNT);
			/* negative signifies BSS table, zero means done */
			if (temp <= 0) {
				dlthis->cinit_state = CI_DONE;
				break;
			}
			dlthis->cinit_count = temp;
			dlthis->cinit_state = CI_ADDRESS;
			break;
#if CINIT_ALIGN < CINIT_ADDRESS
		case CI_PARTADDRESS:
			pktp -= TDATA_TO_HOST(CINIT_ALIGN);
			/* back up pointer into space courtesy of caller */
			*(uint16_t *) pktp = dlthis->cinit_addr;
			/* stuff in saved bits  !! FALL THRU !! */
#endif
		case CI_ADDRESS:	/* Address field for a copy packet */
			if (left < TDATA_TO_HOST(CINIT_ADDRESS)) {
#if CINIT_ALIGN < CINIT_ADDRESS
				if (left == TDATA_TO_HOST(CINIT_ALIGN)) {
					/* address broken into halves */
					dlthis->cinit_addr = *(uint16_t *) pktp;
					/* remember 1st half */
					dlthis->cinit_state = CI_PARTADDRESS;
					left = 0;
				}
#endif
				goto loopexit;
			}
			atmp = dload_unpack(dlthis, (tgt_au_t *) pktp,
					    CINIT_ADDRESS * TDATA_AU_BITS, 0,
					    ROP_UNS);
			pktp += TDATA_TO_HOST(CINIT_ADDRESS);
#if CINIT_PAGE_BITS > 0
			dlthis->cinit_page = atmp &
			    ((1 << CINIT_PAGE_BITS) - 1);
			atmp >>= CINIT_PAGE_BITS;
#else
			dlthis->cinit_page = CINIT_DEFAULT_PAGE;
#endif
			dlthis->cinit_addr = atmp;
			dlthis->cinit_state = CI_COPY;
			break;
		case CI_COPY:	/* copy bits to the target */
			init_count = HOST_TO_TDATA(left);
			if (init_count > dlthis->cinit_count)
				init_count = dlthis->cinit_count;
			if (init_count == 0)
				goto loopexit;	/* get more bits */
			cinit_info = cinit_info_init;
			cinit_info.page = dlthis->cinit_page;
			if (!dlthis->myio->writemem(dlthis->myio, pktp,
						   TDATA_TO_TADDR
						   (dlthis->cinit_addr),
						   &cinit_info,
						   TDATA_TO_HOST(init_count))) {
				dload_error(dlthis, initfail, "write",
					    dlthis->cinit_addr);
			}
			dlthis->cinit_count -= init_count;
			if (dlthis->cinit_count <= 0) {
				dlthis->cinit_state = CI_COUNT;
				init_count = (init_count + CINIT_ALIGN - 1) &
				    -CINIT_ALIGN;
				/* align to next init */
			}
			pktp += TDATA_TO_HOST(init_count);
			dlthis->cinit_addr += init_count;
			break;
		case CI_DONE:	/* no more .cinit to do */
			return;
		}		/* switch (cinit_state) */
	}			/* while */

loopexit:
	if (left > 0) {
		dload_error(dlthis, "%d bytes left over in cinit packet", left);
		dlthis->cinit_state = CI_DONE;	/* left over bytes are bad */
	}
}				/* cload_cinit */

/*	Functions to interface to reloc.c
 *
 * reloc.c is the relocation module borrowed from the linker, with
 * minimal (we hope) changes for our purposes.  cload_sect_data() invokes
 * this module on a section to relocate and load the image data for that
 * section.  The actual read and write actions are supplied by the global
 * routines below.
 */

/************************************************************************
 * Procedure relocate_packet
 *
 * Parameters:
 *	ipacket		Pointer to an image packet to relocate
 *
 * Effect:
 *	Performs the required relocations on the packet.  Returns a checksum
 * of the relocation operations.
 *********************************************************************** */
#define MY_RELOC_BUF_SIZ 8
/* careful! exists at the same time as the image buffer */
static int relocate_packet(struct dload_state *dlthis,
			   struct image_packet_t *ipacket,
			   u32 *checks, bool *tramps_generated)
{
	u32 rnum;
	*tramps_generated = false;

	rnum = ipacket->num_relocs;
	do {			/* all relocs */
		unsigned rinbuf;
		int siz;
		struct reloc_record_t *rp, rrec[MY_RELOC_BUF_SIZ];

		rp = rrec;
		rinbuf = rnum > MY_RELOC_BUF_SIZ ? MY_RELOC_BUF_SIZ : rnum;
		siz = rinbuf * sizeof(struct reloc_record_t);
		if (dlthis->strm->read_buffer(dlthis->strm, rp, siz) != siz) {
			DL_ERROR(readstrm, "relocation");
			return 0;
		}
		/* reorder the bytes if need be */
		if (dlthis->reorder_map)
			dload_reorder(rp, siz, dlthis->reorder_map);

		*checks += dload_checksum(rp, siz);
		do {
			/* perform the relocation operation */
			dload_relocate(dlthis, (tgt_au_t *) ipacket->img_data,
				       rp, tramps_generated, false);
			rp += 1;
			rnum -= 1;
		} while ((rinbuf -= 1) > 0);
	} while (rnum > 0);	/* all relocs */
	/* If trampoline(s) were generated, we need to do an update of the
	 * trampoline copy of the packet since a 2nd phase relo will be done
	 * later. */
	if (*tramps_generated == true) {
		dload_tramp_pkt_udpate(dlthis,
				       (dlthis->image_secn -
					dlthis->ldr_sections),
				       dlthis->image_offset, ipacket);
	}

	return 1;
}				/* dload_read_reloc */

#define IPH_SIZE (sizeof(struct image_packet_t) - sizeof(u32))

/* VERY dangerous */
static const char imagepak[] = { "image packet" };

struct img_buffer {
	struct image_packet_t ipacket;
	u8 bufr[BYTE_TO_HOST(IMAGE_PACKET_SIZE)];
};

/*************************************************************************
 * Procedure dload_data
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Read image data from input file, relocate it, and download it to the
 *	target.
 *********************************************************************** */
static void dload_data(struct dload_state *dlthis)
{
	u16 curr_sect;
	struct doff_scnhdr_t *sptr = dlthis->sect_hdrs;
	struct ldr_section_info *lptr = dlthis->ldr_sections;
	struct img_buffer *ibuf;
	u8 *dest;

	/* Indicates whether CINIT processing has occurred */
	bool cinit_processed = false;

	ibuf = kzalloc(sizeof(*ibuf), GFP_KERNEL);
	if (!ibuf)
		return;

	/* Loop through the sections and load them one at a time.
	 */
	for (curr_sect = 0; curr_sect < dlthis->dfile_hdr.df_no_scns;
	     curr_sect += 1) {
		if (ds_needs_download(sptr)) {
			s32 nip;
			ldr_addr image_offset = 0;
			/* set relocation info for this section */
			if (curr_sect < dlthis->allocated_secn_count)
				dlthis->delta_runaddr = sptr->ds_paddr;
			else {
				lptr = (struct ldr_section_info *)sptr;
				dlthis->delta_runaddr = 0;
			}
			dlthis->image_secn = lptr;
#if BITS_PER_AU > BITS_PER_BYTE
			lptr->name = unpack_name(dlthis, sptr->ds_offset);
#endif
			nip = sptr->ds_nipacks;
			while ((nip -= 1) >= 0) {	/* process packets */

				s32 ipsize;
				u32 checks;
				bool tramp_generated = false;

				/* get the fixed header bits */
				if (dlthis->strm->read_buffer(dlthis->strm,
							      &ibuf->ipacket,
							      IPH_SIZE) !=
				    IPH_SIZE) {
					DL_ERROR(readstrm, imagepak);
					goto free_ibuf;
				}
				/* reorder the header if need be */
				if (dlthis->reorder_map) {
					dload_reorder(&ibuf->ipacket, IPH_SIZE,
						      dlthis->reorder_map);
				}
				/* now read the rest of the packet */
				ipsize =
				    BYTE_TO_HOST(DOFF_ALIGN
						 (ibuf->ipacket.packet_size));
				if (ipsize > BYTE_TO_HOST(IMAGE_PACKET_SIZE)) {
					DL_ERROR("Bad image packet size %d",
						 ipsize);
					goto free_ibuf;
				}
				dest = ibuf->bufr;
				/* End of determination */

				if (dlthis->strm->read_buffer(dlthis->strm,
							      ibuf->bufr,
							      ipsize) !=
				    ipsize) {
					DL_ERROR(readstrm, imagepak);
					goto free_ibuf;
				}
				ibuf->ipacket.img_data = dest;

				/* reorder the bytes if need be */
#if !defined(_BIG_ENDIAN) || (TARGET_AU_BITS > 16)
				if (dlthis->reorder_map) {
					dload_reorder(dest, ipsize,
						      dlthis->reorder_map);
				}
				checks = dload_checksum(dest, ipsize);
#else
				if (dlthis->dfile_hdr.df_byte_reshuffle !=
				    TARGET_ORDER(REORDER_MAP
						 (BYTE_RESHUFFLE_VALUE))) {
					/* put image bytes in big-endian order,
					 * not PC order */
					dload_reorder(dest, ipsize,
						      TARGET_ORDER
						      (dlthis->dfile_hdr.
						       df_byte_reshuffle));
				}
#if TARGET_AU_BITS > 8
				checks = dload_reverse_checksum16(dest, ipsize);
#else
				checks = dload_reverse_checksum(dest, ipsize);
#endif
#endif

				checks += dload_checksum(&ibuf->ipacket,
							 IPH_SIZE);
				/* relocate the image bits as needed */
				if (ibuf->ipacket.num_relocs) {
					dlthis->image_offset = image_offset;
					if (!relocate_packet(dlthis,
							     &ibuf->ipacket,
							     &checks,
							     &tramp_generated))
						goto free_ibuf;	/* error */
				}
				if (~checks)
					DL_ERROR(err_checksum, imagepak);
				/* Only write the result to the target if no
				 * trampoline was generated.  Otherwise it
				 *will be done during trampoline finalize. */

				if (tramp_generated == false) {

					/* stuff the result into target
					 * memory */
					if (dload_check_type(sptr,
						DLOAD_CINIT)) {
						cload_cinit(dlthis,
							    &ibuf->ipacket);
						cinit_processed = true;
					} else {
						/* FIXME */
						if (!dlthis->myio->
						    writemem(dlthis->
							myio,
							ibuf->bufr,
							lptr->
							load_addr +
							image_offset,
							lptr,
							BYTE_TO_HOST
							(ibuf->
							ipacket.
							packet_size))) {
							DL_ERROR
							  ("Write to "
							  FMT_UI32
							  " failed",
							  lptr->
							  load_addr +
							  image_offset);
						}
					}
				}
				image_offset +=
				    BYTE_TO_TADDR(ibuf->ipacket.packet_size);
			}	/* process packets */
			/* if this is a BSS section, we may want to fill it */
			if (!dload_check_type(sptr, DLOAD_BSS))
				goto loop_cont;

			if (!(dlthis->myoptions & DLOAD_INITBSS))
				goto loop_cont;

			if (cinit_processed) {
				/* Don't clear BSS after load-time
				 * initialization */
				DL_ERROR
				    ("Zero-initialization at " FMT_UI32
				     " after " "load-time initialization!",
				     lptr->load_addr);
				goto loop_cont;
			}
			/* fill the .bss area */
			dlthis->myio->fillmem(dlthis->myio,
					      TADDR_TO_HOST(lptr->load_addr),
					      lptr, TADDR_TO_HOST(lptr->size),
					      DLOAD_FILL_BSS);
			goto loop_cont;
		}
		/* if DS_DOWNLOAD_MASK */
		/* If not loading, but BSS, zero initialize */
		if (!dload_check_type(sptr, DLOAD_BSS))
			goto loop_cont;

		if (!(dlthis->myoptions & DLOAD_INITBSS))
			goto loop_cont;

		if (curr_sect >= dlthis->allocated_secn_count)
			lptr = (struct ldr_section_info *)sptr;

		if (cinit_processed) {
			/*Don't clear BSS after load-time initialization */
			DL_ERROR("Zero-initialization at " FMT_UI32
				 " attempted after "
				 "load-time initialization!", lptr->load_addr);
			goto loop_cont;
		}
		/* fill the .bss area */
		dlthis->myio->fillmem(dlthis->myio,
				      TADDR_TO_HOST(lptr->load_addr), lptr,
				      TADDR_TO_HOST(lptr->size),
				      DLOAD_FILL_BSS);
loop_cont:
		sptr += 1;
		lptr += 1;
	}			/* load sections */

	/*  Finalize any trampolines that were created during the load */
	if (dload_tramp_finalize(dlthis) == 0) {
		DL_ERROR("Finalization of auto-trampolines (size = " FMT_UI32
			 ") failed", dlthis->tramp.tramp_sect_next_addr);
	}
free_ibuf:
	kfree(ibuf);
	return;
}				/* dload_data */

/*************************************************************************
 * Procedure dload_reorder
 *
 * Parameters:
 *	data	32-bit aligned pointer to data to be byte-swapped
 *	dsiz	size of the data to be reordered in sizeof() units.
 *	map		32-bit map defining how to reorder the data.  Value
 *			must be REORDER_MAP() of some permutation
 *			of 0x00 01 02 03
 *
 * Effect:
 *	Re-arranges the bytes in each word according to the map specified.
 *
 *********************************************************************** */
/* mask for byte shift count */
#define SHIFT_COUNT_MASK (3 << LOG_BITS_PER_BYTE)

void dload_reorder(void *data, int dsiz, unsigned int map)
{
	register u32 tmp, tmap, datv;
	u32 *dp = (u32 *) data;

	map <<= LOG_BITS_PER_BYTE;	/* align map with SHIFT_COUNT_MASK */
	do {
		tmp = 0;
		datv = *dp;
		tmap = map;
		do {
			tmp |= (datv & BYTE_MASK) << (tmap & SHIFT_COUNT_MASK);
			tmap >>= BITS_PER_BYTE;
		} while (datv >>= BITS_PER_BYTE);
		*dp++ = tmp;
	} while ((dsiz -= sizeof(u32)) > 0);
}				/* dload_reorder */

/*************************************************************************
 * Procedure dload_checksum
 *
 * Parameters:
 *	data	32-bit aligned pointer to data to be checksummed
 *	siz		size of the data to be checksummed in sizeof() units.
 *
 * Effect:
 *	Returns a checksum of the specified block
 *
 *********************************************************************** */
u32 dload_checksum(void *data, unsigned siz)
{
	u32 sum;
	u32 *dp;
	int left;

	sum = 0;
	dp = (u32 *) data;
	for (left = siz; left > 0; left -= sizeof(u32))
		sum += *dp++;
	return sum;
}				/* dload_checksum */

#if HOST_ENDIANNESS
/*************************************************************************
 * Procedure dload_reverse_checksum
 *
 * Parameters:
 *	data	32-bit aligned pointer to data to be checksummed
 *	siz		size of the data to be checksummed in sizeof() units.
 *
 * Effect:
 *	Returns a checksum of the specified block, which is assumed to be bytes
 * in big-endian order.
 *
 * Notes:
 *	In a big-endian host, things like the string table are stored as bytes
 * in host order. But dllcreate always checksums in little-endian order.
 * It is most efficient to just handle the difference a word at a time.
 *
 ********************************************************************** */
u32 dload_reverse_checksum(void *data, unsigned siz)
{
	u32 sum, temp;
	u32 *dp;
	int left;

	sum = 0;
	dp = (u32 *) data;

	for (left = siz; left > 0; left -= sizeof(u32)) {
		temp = *dp++;
		sum += temp << BITS_PER_BYTE * 3;
		sum += temp >> BITS_PER_BYTE * 3;
		sum += (temp >> BITS_PER_BYTE) & (BYTE_MASK << BITS_PER_BYTE);
		sum += (temp & (BYTE_MASK << BITS_PER_BYTE)) << BITS_PER_BYTE;
	}

	return sum;
}				/* dload_reverse_checksum */

#if (TARGET_AU_BITS > 8) && (TARGET_AU_BITS < 32)
u32 dload_reverse_checksum16(void *data, unsigned siz)
{
	uint_fast32_t sum, temp;
	u32 *dp;
	int left;

	sum = 0;
	dp = (u32 *) data;

	for (left = siz; left > 0; left -= sizeof(u32)) {
		temp = *dp++;
		sum += temp << BITS_PER_BYTE * 2;
		sum += temp >> BITS_PER_BYTE * 2;
	}

	return sum;
}				/* dload_reverse_checksum16 */
#endif
#endif

/*************************************************************************
 * Procedure swap_words
 *
 * Parameters:
 *	data	32-bit aligned pointer to data to be swapped
 *	siz	size of the data to be swapped.
 *	bitmap	Bit map of how to swap each 32-bit word; 1 => 2 shorts,
 *		0 => 1 long
 *
 * Effect:
 *	Swaps the specified data according to the specified map
 *
 *********************************************************************** */
static void swap_words(void *data, unsigned siz, unsigned bitmap)
{
	register int i;
#if TARGET_AU_BITS < 16
	register u16 *sp;
#endif
	register u32 *lp;

	siz /= sizeof(u16);

#if TARGET_AU_BITS < 16
	/* pass 1: do all the bytes */
	i = siz;
	sp = (u16 *) data;
	do {
		register u16 tmp;

		tmp = *sp;
		*sp++ = SWAP16BY8(tmp);
	} while ((i -= 1) > 0);
#endif

#if TARGET_AU_BITS < 32
	/* pass 2: fixup the 32-bit words */
	i = siz >> 1;
	lp = (u32 *) data;
	do {
		if ((bitmap & 1) == 0) {
			register u32 tmp;
			tmp = *lp;
			*lp = SWAP32BY16(tmp);
		}
		lp += 1;
		bitmap >>= 1;
	} while ((i -= 1) > 0);
#endif
}				/* swap_words */

/*************************************************************************
 * Procedure copy_tgt_strings
 *
 * Parameters:
 *	dstp		Destination address.  Assumed to be 32-bit aligned
 *	srcp		Source address.  Assumed to be 32-bit aligned
 *	charcount	Number of characters to copy.
 *
 * Effect:
 *	Copies strings from the source (which is in usual .dof file order on
 * the loading processor) to the destination buffer (which should be in proper
 * target addressable unit order).  Makes sure the last string in the
 * buffer is NULL terminated (for safety).
 * Returns the first unused destination address.
 *********************************************************************** */
static char *copy_tgt_strings(void *dstp, void *srcp, unsigned charcount)
{
	register tgt_au_t *src = (tgt_au_t *) srcp;
	register tgt_au_t *dst = (tgt_au_t *) dstp;
	register int cnt = charcount;

	do {
#if TARGET_AU_BITS <= BITS_PER_AU
		/* byte-swapping issues may exist for strings on target */
		*dst++ = *src++;
#else
		*dst++ = *src++;
#endif
	} while ((cnt -= (sizeof(tgt_au_t) * BITS_PER_AU / BITS_PER_BYTE)) > 0);
	/*apply force to make sure that the string table has null terminator */
#if (BITS_PER_AU == BITS_PER_BYTE) && (TARGET_AU_BITS == BITS_PER_BYTE)
	dst[-1] = 0;
#else
	/* little endian */
	dst[-1] &= (1 << (BITS_PER_AU - BITS_PER_BYTE)) - 1;
#endif
	return (char *)dst;
}				/* copy_tgt_strings */

/*************************************************************************
 * Procedure init_module_handle
 *
 * Parameters:
 *	none
 *
 * Effect:
 *	Initializes the module handle we use to enable unloading, and installs
 * the debug information required by the target.
 *
 * Notes:
 * The handle returned from dynamic_load_module needs to encapsulate all the
 * allocations done for the module, and enable them plus the modules symbols to
 * be deallocated.
 *
 *********************************************************************** */
#ifndef _BIG_ENDIAN
static const struct ldr_section_info dllview_info_init = { ".dllview", 0, 0,
	(ldr_addr)-1, DBG_LIST_PAGE, DLOAD_DATA, 0
};
#else
static const struct ldr_section_info dllview_info_init = { ".dllview", 0, 0,
	(ldr_addr)-1, DLOAD_DATA, DBG_LIST_PAGE, 0
};
#endif
static void init_module_handle(struct dload_state *dlthis)
{
	struct my_handle *hndl;
	u16 curr_sect;
	struct ldr_section_info *asecs;
	struct dll_module *dbmod;
	struct dll_sect *dbsec;
	struct dbg_mirror_root *mlist;
	register char *cp;
	struct modules_header mhdr;
	struct ldr_section_info dllview_info;
	struct dynload_symbol *debug_mirror_sym;

	hndl = dlthis->myhandle;
	if (!hndl)
		return;		/* must be errors detected, so forget it */

	/*  Store the section count */
	hndl->secn_count = dlthis->allocated_secn_count;

	/*  If a trampoline section was created, add it in */
	if (dlthis->tramp.tramp_sect_next_addr != 0)
		hndl->secn_count += 1;

	hndl->secn_count = hndl->secn_count << 1;

	hndl->secn_count = dlthis->allocated_secn_count << 1;
#ifndef TARGET_ENDIANNESS
	if (dlthis->big_e_target)
		hndl->secn_count += 1;	/* flag for big-endian */
#endif
	if (dlthis->dload_errcount)
		return;		/* abandon if errors detected */
	/* Locate the symbol that names the header for the CCS debug list
	   of modules. If not found, we just don't generate the debug record.
	   If found, we create our modules list.  We make sure to create the
	   loader_dllview_root even if there is no relocation info to record,
	   just to try to put both symbols in the same symbol table and
	   module. */
	debug_mirror_sym = dlthis->mysym->find_matching_symbol(dlthis->mysym,
							loader_dllview_root);
	if (!debug_mirror_sym) {
		struct dynload_symbol *dlmodsym;
		struct dbg_mirror_root *mlst;

		/* our root symbol is not yet present;
		   check if we have DLModules defined */
		dlmodsym = dlthis->mysym->find_matching_symbol(dlthis->mysym,
							LINKER_MODULES_HEADER);
		if (!dlmodsym)
			return;	/* no DLModules list so no debug info */
		/* if we have DLModules defined, construct our header */
		mlst = (struct dbg_mirror_root *)
		    dlthis->mysym->dload_allocate(dlthis->mysym,
						  sizeof(struct
							 dbg_mirror_root));
		if (!mlst) {
			DL_ERROR(err_alloc, sizeof(struct dbg_mirror_root));
			return;
		}
		mlst->next = NULL;
		mlst->changes = 0;
		mlst->refcount = 0;
		mlst->dbthis = TDATA_TO_TADDR(dlmodsym->value);
		/* add our root symbol */
		debug_mirror_sym = dlthis->mysym->add_to_symbol_table
		    (dlthis->mysym, loader_dllview_root,
		     (unsigned)dlthis->myhandle);
		if (!debug_mirror_sym) {
			/* failed, recover memory */
			dlthis->mysym->dload_deallocate(dlthis->mysym, mlst);
			return;
		}
		debug_mirror_sym->value = (u32) mlst;
	}
	/* First create the DLLview record and stuff it into the buffer.
	   Then write it to the DSP.  Record pertinent locations in our hndl,
	   and add it to the per-processor list of handles with debug info. */
#ifndef DEBUG_HEADER_IN_LOADER
	mlist = (struct dbg_mirror_root *)debug_mirror_sym->value;
	if (!mlist)
		return;
#else
	mlist = (struct dbg_mirror_root *)&debug_list_header;
#endif
	hndl->dm.root = mlist;	/* set pointer to root into our handle */
	if (!dlthis->allocated_secn_count)
		return;		/* no load addresses to be recorded */
	/* reuse temporary symbol storage */
	dbmod = (struct dll_module *)dlthis->local_symtab;
	/* Create the DLLview record in the memory we retain for our handle */
	dbmod->num_sects = dlthis->allocated_secn_count;
	dbmod->timestamp = dlthis->verify.dv_timdat;
	dbmod->version = INIT_VERSION;
	dbmod->verification = VERIFICATION;
	asecs = dlthis->ldr_sections;
	dbsec = dbmod->sects;
	for (curr_sect = dlthis->allocated_secn_count;
	     curr_sect > 0; curr_sect -= 1) {
		dbsec->sect_load_adr = asecs->load_addr;
		dbsec->sect_run_adr = asecs->run_addr;
		dbsec += 1;
		asecs += 1;
	}

	/*  If a trampoline section was created go ahead and add its info */
	if (dlthis->tramp.tramp_sect_next_addr != 0) {
		dbmod->num_sects++;
		dbsec->sect_load_adr = asecs->load_addr;
		dbsec->sect_run_adr = asecs->run_addr;
		dbsec++;
		asecs++;
	}

	/* now cram in the names */
	cp = copy_tgt_strings(dbsec, dlthis->str_head,
			      dlthis->debug_string_size);

	/* If a trampoline section was created, add its name so DLLView
	 * can show the user the section info. */
	if (dlthis->tramp.tramp_sect_next_addr != 0) {
		cp = copy_tgt_strings(cp,
				      dlthis->tramp.final_string_table,
				      strlen(dlthis->tramp.final_string_table) +
				      1);
	}

	/* round off the size of the debug record, and remember same */
	hndl->dm.dbsiz = HOST_TO_TDATA_ROUND(cp - (char *)dbmod);
	*cp = 0;		/* strictly to make our test harness happy */
	dllview_info = dllview_info_init;
	dllview_info.size = TDATA_TO_TADDR(hndl->dm.dbsiz);
	/* Initialize memory context to default heap */
	dllview_info.context = 0;
	hndl->dm.context = 0;
	/* fill in next pointer and size */
	if (mlist->next) {
		dbmod->next_module = TADDR_TO_TDATA(mlist->next->dm.dbthis);
		dbmod->next_module_size = mlist->next->dm.dbsiz;
	} else {
		dbmod->next_module_size = 0;
		dbmod->next_module = 0;
	}
	/* allocate memory for on-DSP DLLview debug record */
	if (!dlthis->myalloc)
		return;
	if (!dlthis->myalloc->dload_allocate(dlthis->myalloc, &dllview_info,
					     HOST_TO_TADDR(sizeof(u32)))) {
		return;
	}
	/* Store load address of .dllview section */
	hndl->dm.dbthis = dllview_info.load_addr;
	/* Store memory context (segid) in which .dllview section
	 * was  allocated */
	hndl->dm.context = dllview_info.context;
	mlist->refcount += 1;
	/* swap bytes in the entire debug record, but not the string table */
	if (TARGET_ENDIANNESS_DIFFERS(TARGET_BIG_ENDIAN)) {
		swap_words(dbmod, (char *)dbsec - (char *)dbmod,
			   DLL_MODULE_BITMAP);
	}
	/* Update the DLLview list on the DSP write new record */
	if (!dlthis->myio->writemem(dlthis->myio, dbmod,
				    dllview_info.load_addr, &dllview_info,
				    TADDR_TO_HOST(dllview_info.size))) {
		return;
	}
	/* write new header */
	mhdr.first_module_size = hndl->dm.dbsiz;
	mhdr.first_module = TADDR_TO_TDATA(dllview_info.load_addr);
	/* swap bytes in the module header, if needed */
	if (TARGET_ENDIANNESS_DIFFERS(TARGET_BIG_ENDIAN)) {
		swap_words(&mhdr, sizeof(struct modules_header) - sizeof(u16),
			   MODULES_HEADER_BITMAP);
	}
	dllview_info = dllview_info_init;
	if (!dlthis->myio->writemem(dlthis->myio, &mhdr, mlist->dbthis,
				    &dllview_info,
				    sizeof(struct modules_header) -
				    sizeof(u16))) {
		return;
	}
	/* Add the module handle to this processor's list
	   of handles with debug info */
	hndl->dm.next = mlist->next;
	if (hndl->dm.next)
		hndl->dm.next->dm.prev = hndl;
	hndl->dm.prev = (struct my_handle *)mlist;
	mlist->next = hndl;	/* insert after root */
}				/* init_module_handle */

/*************************************************************************
 * Procedure dynamic_unload_module
 *
 * Parameters:
 *	mhandle	A module handle from dynamic_load_module
 *	syms	Host-side symbol table and malloc/free functions
 *	alloc	Target-side memory allocation
 *
 * Effect:
 *	The module specified by mhandle is unloaded.  Unloading causes all
 * target memory to be deallocated, all symbols defined by the module to
 * be purged, and any host-side storage used by the dynamic loader for
 * this module to be released.
 *
 * Returns:
 *	Zero for success. On error, the number of errors detected is returned.
 * Individual errors are reported using syms->error_report().
 *********************************************************************** */
int dynamic_unload_module(void *mhandle,
			  struct dynamic_loader_sym *syms,
			  struct dynamic_loader_allocate *alloc,
			  struct dynamic_loader_initialize *init)
{
	s16 curr_sect;
	struct ldr_section_info *asecs;
	struct my_handle *hndl;
	struct dbg_mirror_root *root;
	unsigned errcount = 0;
	struct ldr_section_info dllview_info = dllview_info_init;
	struct modules_header mhdr;

	hndl = (struct my_handle *)mhandle;
	if (!hndl)
		return 0;	/* if handle is null, nothing to do */
	/* Clear out the module symbols
	 * Note that if this is the module that defined MODULES_HEADER
	 (the head of the target debug list)
	 * then this operation will blow away that symbol.
	 It will therefore be impossible for subsequent
	 * operations to add entries to this un-referenceable list. */
	if (!syms)
		return 1;
	syms->purge_symbol_table(syms, (unsigned)hndl);
	/* Deallocate target memory for sections
	 * NOTE: The trampoline section, if created, gets deleted here, too */

	asecs = hndl->secns;
	if (alloc)
		for (curr_sect = (hndl->secn_count >> 1); curr_sect > 0;
		     curr_sect -= 1) {
			asecs->name = NULL;
			alloc->dload_deallocate(alloc, asecs++);
		}
	root = hndl->dm.root;
	if (!root) {
		/* there is a debug list containing this module */
		goto func_end;
	}
	if (!hndl->dm.dbthis) {	/* target-side dllview record exists */
		goto loop_end;
	}
	/* Retrieve memory context in which .dllview was allocated */
	dllview_info.context = hndl->dm.context;
	if (hndl->dm.prev == hndl)
		goto exitunltgt;

	/* target-side dllview record is in list */
	/* dequeue this record from our GPP-side mirror list */
	hndl->dm.prev->dm.next = hndl->dm.next;
	if (hndl->dm.next)
		hndl->dm.next->dm.prev = hndl->dm.prev;
	/* Update next_module of previous entry in target list
	 * We are using mhdr here as a surrogate for either a
	 struct modules_header or a dll_module */
	if (hndl->dm.next) {
		mhdr.first_module = TADDR_TO_TDATA(hndl->dm.next->dm.dbthis);
		mhdr.first_module_size = hndl->dm.next->dm.dbsiz;
	} else {
		mhdr.first_module = 0;
		mhdr.first_module_size = 0;
	}
	if (!init)
		goto exitunltgt;

	if (!init->connect(init)) {
		dload_syms_error(syms, iconnect);
		errcount += 1;
		goto exitunltgt;
	}
	/* swap bytes in the module header, if needed */
	if (TARGET_ENDIANNESS_DIFFERS(hndl->secn_count & 0x1)) {
		swap_words(&mhdr, sizeof(struct modules_header) - sizeof(u16),
			   MODULES_HEADER_BITMAP);
	}
	if (!init->writemem(init, &mhdr, hndl->dm.prev->dm.dbthis,
			    &dllview_info, sizeof(struct modules_header) -
			    sizeof(mhdr.update_flag))) {
		dload_syms_error(syms, dlvwrite);
		errcount += 1;
	}
	/* update change counter */
	root->changes += 1;
	if (!init->writemem(init, &(root->changes),
			    root->dbthis + HOST_TO_TADDR
			    (sizeof(mhdr.first_module) +
			     sizeof(mhdr.first_module_size)),
			    &dllview_info, sizeof(mhdr.update_flag))) {
		dload_syms_error(syms, dlvwrite);
		errcount += 1;
	}
	init->release(init);
exitunltgt:
	/* release target storage */
	dllview_info.size = TDATA_TO_TADDR(hndl->dm.dbsiz);
	dllview_info.load_addr = hndl->dm.dbthis;
	if (alloc)
		alloc->dload_deallocate(alloc, &dllview_info);
	root->refcount -= 1;
	/* target-side dllview record exists */
loop_end:
#ifndef DEBUG_HEADER_IN_LOADER
	if (root->refcount <= 0) {
		/* if all references gone, blow off the header */
		/* our root symbol may be gone due to the Purge above,
		   but if not, do not destroy the root */
		if (syms->find_matching_symbol
		    (syms, loader_dllview_root) == NULL)
			syms->dload_deallocate(syms, root);
	}
#endif
func_end:
	/* there is a debug list containing this module */
	syms->dload_deallocate(syms, mhandle);	/* release our storage */
	return errcount;
}				/* dynamic_unload_module */

#if BITS_PER_AU > BITS_PER_BYTE
/*************************************************************************
 * Procedure unpack_name
 *
 * Parameters:
 *	soffset	Byte offset into the string table
 *
 * Effect:
 *	Returns a pointer to the string specified by the offset supplied, or
 * NULL for error.
 *
 *********************************************************************** */
static char *unpack_name(struct dload_state *dlthis, u32 soffset)
{
	u8 tmp, *src;
	char *dst;

	if (soffset >= dlthis->dfile_hdr.df_strtab_size) {
		dload_error(dlthis, "Bad string table offset " FMT_UI32,
			    soffset);
		return NULL;
	}
	src = (uint_least8_t *) dlthis->str_head +
	    (soffset >> (LOG_BITS_PER_AU - LOG_BITS_PER_BYTE));
	dst = dlthis->str_temp;
	if (soffset & 1)
		*dst++ = *src++;	/* only 1 character in first word */
	do {
		tmp = *src++;
		*dst = (tmp >> BITS_PER_BYTE);
		if (!(*dst++))
			break;
	} while ((*dst++ = tmp & BYTE_MASK));
	dlthis->temp_len = dst - dlthis->str_temp;
	/* squirrel away length including terminating null */
	return dlthis->str_temp;
}				/* unpack_name */
#endif
