/*
 * dbldefs.h
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

#ifndef DBLDEFS_
#define DBLDEFS_

/*
 *  Bit masks for dbl_flags.
 */
#define DBL_NOLOAD   0x0	/* Don't load symbols, code, or data */
#define DBL_SYMB     0x1	/* load symbols */
#define DBL_CODE     0x2	/* load code */
#define DBL_DATA     0x4	/* load data */
#define DBL_DYNAMIC  0x8	/* dynamic load */
#define DBL_BSS      0x20	/* Unitialized section */

#define DBL_MAXPATHLENGTH       255

/*
 *  ======== dbl_flags ========
 *  Specifies whether to load code, data, or symbols
 */
typedef s32 dbl_flags;

/*
 *  ======== dbl_sect_info ========
 *  For collecting info on overlay sections
 */
struct dbl_sect_info {
	const char *name;	/* name of section */
	u32 sect_run_addr;	/* run address of section */
	u32 sect_load_addr;	/* load address of section */
	u32 size;		/* size of section (target MAUs) */
	dbl_flags type;		/* Code, data, or BSS */
};

/*
 *  ======== dbl_symbol ========
 *  (Needed for dynamic load library)
 */
struct dbl_symbol {
	u32 value;
};

/*
 *  ======== dbl_alloc_fxn ========
 *  Allocate memory function.  Allocate or reserve (if reserved == TRUE)
 *  "size" bytes of memory from segment "space" and return the address in
 *  *dsp_address (or starting at *dsp_address if reserve == TRUE). Returns 0 on
 *  success, or an error code on failure.
 */
typedef s32(*dbl_alloc_fxn) (void *hdl, s32 space, u32 size, u32 align,
			     u32 *dsp_address, s32 seg_id, s32 req,
			     bool reserved);

/*
 *  ======== dbl_free_fxn ========
 *  Free memory function.  Free, or unreserve (if reserved == TRUE) "size"
 *  bytes of memory from segment "space"
 */
typedef bool(*dbl_free_fxn) (void *hdl, u32 addr, s32 space, u32 size,
			     bool reserved);

/*
 *  ======== dbl_log_write_fxn ========
 *  Function to call when writing data from a section, to log the info.
 *  Can be NULL if no logging is required.
 */
typedef int(*dbl_log_write_fxn) (void *handle,
					struct dbl_sect_info *sect, u32 addr,
					u32 bytes);

/*
 *  ======== dbl_sym_lookup ========
 *  Symbol lookup function - Find the symbol name and return its value.
 *
 *  Parameters:
 *      handle          - Opaque handle
 *      parg            - Opaque argument.
 *      name            - Name of symbol to lookup.
 *      sym             - Location to store address of symbol structure.
 *
 *  Returns:
 *      TRUE:           Success (symbol was found).
 *      FALSE:          Failed to find symbol.
 */
typedef bool(*dbl_sym_lookup) (void *handle, void *parg, void *rmm_handle,
			       const char *name, struct dbl_symbol ** sym);

/*
 *  ======== dbl_write_fxn ========
 *  Write memory function.  Write "n" HOST bytes of memory to segment "mtype"
 *  starting at address "dsp_address" from the buffer "buf".  The buffer is
 *  formatted as an array of words appropriate for the DSP.
 */
typedef s32(*dbl_write_fxn) (void *hdl, u32 dsp_address, void *buf,
			     u32 n, s32 mtype);

/*
 *  ======== dbl_attrs ========
 */
struct dbl_attrs {
	dbl_alloc_fxn alloc;
	dbl_free_fxn free;
	void *rmm_handle;	/* Handle to pass to alloc, free functions */
	dbl_write_fxn write;
	void *input_params;	/* Handle to pass to write, cinit function */

	dbl_log_write_fxn log_write;
	void *log_write_handle;

	/* Symbol matching function and handle to pass to it */
	dbl_sym_lookup sym_lookup;
	void *sym_handle;
	void *sym_arg;

	/*
	 *  These file manipulation functions should be compatible with the
	 *  "C" run time library functions of the same name.
	 */
	 s32(*fread) (void *, size_t, size_t, void *);
	 s32(*fseek) (void *, long, int);
	 s32(*ftell) (void *);
	 s32(*fclose) (void *);
	void *(*fopen) (const char *, const char *);
};

#endif /* DBLDEFS_ */
