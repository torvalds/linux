/*
 * getsection.c
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

#include <dspbridge/getsection.h>
#include "header.h"

/*
 * Error strings
 */
static const char readstrm[] = { "Error reading %s from input stream" };
static const char seek[] = { "Set file position to %d failed" };
static const char isiz[] = { "Bad image packet size %d" };
static const char err_checksum[] = { "Checksum failed on %s" };

static const char err_reloc[] = { "dload_get_section unable to read"
	    "sections containing relocation entries"
};

#if BITS_PER_AU > BITS_PER_BYTE
static const char err_alloc[] = { "Syms->dload_allocate( %d ) failed" };
static const char stbl[] = { "Bad string table offset " FMT_UI32 };
#endif

/************************************************************** */
/********************* SUPPORT FUNCTIONS ********************** */
/************************************************************** */

#if BITS_PER_AU > BITS_PER_BYTE
/**************************************************************************
 * Procedure unpack_sec_name
 *
 * Parameters:
 *  dlthis		Handle from dload_module_open for this module
 *	soffset	    Byte offset into the string table
 *  dst         Place to store the expanded string
 *
 * Effect:
 *	Stores a string from the string table into the destination, expanding
 * it in the process.  Returns a pointer just past the end of the stored
 * string on success, or NULL on failure.
 *
 ************************************************************************ */
static char *unpack_sec_name(struct dload_state *dlthis, u32 soffset, char *dst)
{
	u8 tmp, *src;

	if (soffset >= dlthis->dfile_hdr.df_scn_name_size) {
		dload_error(dlthis, stbl, soffset);
		return NULL;
	}
	src = (u8 *) dlthis->str_head +
	    (soffset >> (LOG_BITS_PER_AU - LOG_BITS_PER_BYTE));
	if (soffset & 1)
		*dst++ = *src++;	/* only 1 character in first word */
	do {
		tmp = *src++;
		*dst = (tmp >> BITS_PER_BYTE)
		    if (!(*dst++))
			break;
	} while ((*dst++ = tmp & BYTE_MASK));

	return dst;
}

/**************************************************************************
 * Procedure expand_sec_names
 *
 * Parameters:
 *  dlthis		Handle from dload_module_open for this module
 *
 * Effect:
 *    Allocates a buffer, unpacks and copies strings from string table into it.
 * Stores a pointer to the buffer into a state variable.
 ************************************************************************* */
static void expand_sec_names(struct dload_state *dlthis)
{
	char *xstrings, *curr, *next;
	u32 xsize;
	u16 sec;
	struct ldr_section_info *shp;
	/* assume worst-case size requirement */
	xsize = dlthis->dfile_hdr.df_max_str_len * dlthis->dfile_hdr.df_no_scns;
	xstrings = (char *)dlthis->mysym->dload_allocate(dlthis->mysym, xsize);
	if (xstrings == NULL) {
		dload_error(dlthis, err_alloc, xsize);
		return;
	}
	dlthis->xstrings = xstrings;
	/* For each sec, copy and expand its name */
	curr = xstrings;
	for (sec = 0; sec < dlthis->dfile_hdr.df_no_scns; sec++) {
		shp = (struct ldr_section_info *)&dlthis->sect_hdrs[sec];
		next = unpack_sec_name(dlthis, *(u32 *) &shp->name, curr);
		if (next == NULL)
			break;	/* error */
		shp->name = curr;
		curr = next;
	}
}

#endif

/************************************************************** */
/********************* EXPORTED FUNCTIONS ********************* */
/************************************************************** */

/**************************************************************************
 * Procedure dload_module_open
 *
 * Parameters:
 *	module	The input stream that supplies the module image
 *	syms	Host-side malloc/free and error reporting functions.
 *			Other methods are unused.
 *
 * Effect:
 *	Reads header information from a dynamic loader module using the
    specified
 * stream object, and returns a handle for the module information.  This
 * handle may be used in subsequent query calls to obtain information
 * contained in the module.
 *
 * Returns:
 *	NULL if an error is encountered, otherwise a module handle for use
 * in subsequent operations.
 ************************************************************************* */
void *dload_module_open(struct dynamic_loader_stream *module,
				    struct dynamic_loader_sym *syms)
{
	struct dload_state *dlthis;	/* internal state for this call */
	unsigned *dp, sz;
	u32 sec_start;
#if BITS_PER_AU <= BITS_PER_BYTE
	u16 sec;
#endif

	/* Check that mandatory arguments are present */
	if (!module || !syms) {
		if (syms != NULL)
			dload_syms_error(syms, "Required parameter is NULL");

		return NULL;
	}

	dlthis = (struct dload_state *)
	    syms->dload_allocate(syms, sizeof(struct dload_state));
	if (!dlthis) {
		/* not enough storage */
		dload_syms_error(syms, "Can't allocate module info");
		return NULL;
	}

	/* clear our internal state */
	dp = (unsigned *)dlthis;
	for (sz = sizeof(struct dload_state) / sizeof(unsigned);
	     sz > 0; sz -= 1)
		*dp++ = 0;

	dlthis->strm = module;
	dlthis->mysym = syms;

	/* read in the doff image and store in our state variable */
	dload_headers(dlthis);

	if (!dlthis->dload_errcount)
		dload_strings(dlthis, true);

	/* skip ahead past the unread portion of the string table */
	sec_start = sizeof(struct doff_filehdr_t) +
	    sizeof(struct doff_verify_rec_t) +
	    BYTE_TO_HOST(DOFF_ALIGN(dlthis->dfile_hdr.df_strtab_size));

	if (dlthis->strm->set_file_posn(dlthis->strm, sec_start) != 0) {
		dload_error(dlthis, seek, sec_start);
		return NULL;
	}

	if (!dlthis->dload_errcount)
		dload_sections(dlthis);

	if (dlthis->dload_errcount) {
		dload_module_close(dlthis);	/* errors, blow off our state */
		dlthis = NULL;
		return NULL;
	}
#if BITS_PER_AU > BITS_PER_BYTE
	/* Expand all section names from the string table into the */
	/* state variable, and convert section names from a relative */
	/* string table offset to a pointers to the expanded string. */
	expand_sec_names(dlthis);
#else
	/* Convert section names from a relative string table offset */
	/* to a pointer into the string table. */
	for (sec = 0; sec < dlthis->dfile_hdr.df_no_scns; sec++) {
		struct ldr_section_info *shp =
		    (struct ldr_section_info *)&dlthis->sect_hdrs[sec];
		shp->name = dlthis->str_head + *(u32 *) &shp->name;
	}
#endif

	return dlthis;
}

/***************************************************************************
 * Procedure dload_get_section_info
 *
 * Parameters:
 *  minfo		Handle from dload_module_open for this module
 *	section_name	Pointer to the string name of the section desired
 *	section_info	Address of a section info structure pointer to be
 *			initialized
 *
 * Effect:
 *	Finds the specified section in the module information, and initializes
 * the provided struct ldr_section_info pointer.
 *
 * Returns:
 *	true for success, false for section not found
 ************************************************************************* */
int dload_get_section_info(void *minfo, const char *section_name,
			   const struct ldr_section_info **const section_info)
{
	struct dload_state *dlthis;
	struct ldr_section_info *shp;
	u16 sec;

	dlthis = (struct dload_state *)minfo;
	if (!dlthis)
		return false;

	for (sec = 0; sec < dlthis->dfile_hdr.df_no_scns; sec++) {
		shp = (struct ldr_section_info *)&dlthis->sect_hdrs[sec];
		if (strcmp(section_name, shp->name) == 0) {
			*section_info = shp;
			return true;
		}
	}

	return false;
}

#define IPH_SIZE (sizeof(struct image_packet_t) - sizeof(u32))

/**************************************************************************
 * Procedure dload_get_section
 *
 * Parameters:
 *  minfo		Handle from dload_module_open for this module
 *	section_info	Pointer to a section info structure for the desired
 *			section
 *	section_data	Buffer to contain the section initialized data
 *
 * Effect:
 *	Copies the initialized data for the specified section into the
 * supplied buffer.
 *
 * Returns:
 *	true for success, false for section not found
 ************************************************************************* */
int dload_get_section(void *minfo,
		      const struct ldr_section_info *section_info,
		      void *section_data)
{
	struct dload_state *dlthis;
	u32 pos;
	struct doff_scnhdr_t *sptr = NULL;
	s32 nip;
	struct image_packet_t ipacket;
	s32 ipsize;
	u32 checks;
	s8 *dest = (s8 *) section_data;

	dlthis = (struct dload_state *)minfo;
	if (!dlthis)
		return false;
	sptr = (struct doff_scnhdr_t *)section_info;
	if (sptr == NULL)
		return false;

	/* skip ahead to the start of the first packet */
	pos = BYTE_TO_HOST(DOFF_ALIGN((u32) sptr->ds_first_pkt_offset));
	if (dlthis->strm->set_file_posn(dlthis->strm, pos) != 0) {
		dload_error(dlthis, seek, pos);
		return false;
	}

	nip = sptr->ds_nipacks;
	while ((nip -= 1) >= 0) {	/* for each packet */
		/* get the fixed header bits */
		if (dlthis->strm->read_buffer(dlthis->strm, &ipacket,
					      IPH_SIZE) != IPH_SIZE) {
			dload_error(dlthis, readstrm, "image packet");
			return false;
		}
		/* reorder the header if need be */
		if (dlthis->reorder_map)
			dload_reorder(&ipacket, IPH_SIZE, dlthis->reorder_map);

		/* Now read the packet image bits. Note: round the size up to
		 * the next multiple of 4 bytes; this is what checksum
		 * routines want. */
		ipsize = BYTE_TO_HOST(DOFF_ALIGN(ipacket.packet_size));
		if (ipsize > BYTE_TO_HOST(IMAGE_PACKET_SIZE)) {
			dload_error(dlthis, isiz, ipsize);
			return false;
		}
		if (dlthis->strm->read_buffer
		    (dlthis->strm, dest, ipsize) != ipsize) {
			dload_error(dlthis, readstrm, "image packet");
			return false;
		}
		/* reorder the bytes if need be */
#if !defined(_BIG_ENDIAN) || (TARGET_AU_BITS > 16)
		if (dlthis->reorder_map)
			dload_reorder(dest, ipsize, dlthis->reorder_map);

		checks = dload_checksum(dest, ipsize);
#else
		if (dlthis->dfile_hdr.df_byte_reshuffle !=
		    TARGET_ORDER(REORDER_MAP(BYTE_RESHUFFLE_VALUE))) {
			/* put image bytes in big-endian order, not PC order */
			dload_reorder(dest, ipsize,
				      TARGET_ORDER(dlthis->
						dfile_hdr.df_byte_reshuffle));
		}
#if TARGET_AU_BITS > 8
		checks = dload_reverse_checksum16(dest, ipsize);
#else
		checks = dload_reverse_checksum(dest, ipsize);
#endif
#endif
		checks += dload_checksum(&ipacket, IPH_SIZE);

		/* NYI: unable to handle relocation entries here.  Reloc
		 * entries referring to fields that span the packet boundaries
		 * may result in packets of sizes that are not multiple of
		 * 4 bytes. Our checksum implementation works on 32-bit words
		 * only. */
		if (ipacket.num_relocs != 0) {
			dload_error(dlthis, err_reloc, ipsize);
			return false;
		}

		if (~checks) {
			dload_error(dlthis, err_checksum, "image packet");
			return false;
		}

		/*Advance destination ptr by the size of the just-read packet */
		dest += ipsize;
	}

	return true;
}

/***************************************************************************
 * Procedure dload_module_close
 *
 * Parameters:
 *  minfo		Handle from dload_module_open for this module
 *
 * Effect:
 *	Releases any storage associated with the module handle.  On return,
 * the module handle is invalid.
 *
 * Returns:
 *	Zero for success. On error, the number of errors detected is returned.
 * Individual errors are reported using syms->error_report(), where syms was
 * an argument to dload_module_open
 ************************************************************************* */
void dload_module_close(void *minfo)
{
	struct dload_state *dlthis;

	dlthis = (struct dload_state *)minfo;
	if (!dlthis)
		return;

	if (dlthis->str_head)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						dlthis->str_head);

	if (dlthis->sect_hdrs)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						dlthis->sect_hdrs);

#if BITS_PER_AU > BITS_PER_BYTE
	if (dlthis->xstrings)
		dlthis->mysym->dload_deallocate(dlthis->mysym,
						dlthis->xstrings);

#endif

	dlthis->mysym->dload_deallocate(dlthis->mysym, dlthis);
}
