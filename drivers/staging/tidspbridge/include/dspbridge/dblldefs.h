/*
 * dblldefs.h
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

#ifndef DBLLDEFS_
#define DBLLDEFS_

/*
 *  Bit masks for dbl_flags.
 */
#define DBLL_NOLOAD   0x0	/* Don't load symbols, code, or data */
#define DBLL_SYMB     0x1	/* load symbols */
#define DBLL_CODE     0x2	/* load code */
#define DBLL_DATA     0x4	/* load data */
#define DBLL_DYNAMIC  0x8	/* dynamic load */
#define DBLL_BSS      0x20	/* Unitialized section */

#define DBLL_MAXPATHLENGTH       255

/*
 *  ======== DBLL_Target ========
 *
 */
struct dbll_tar_obj;

/*
 *  ======== dbll_flags ========
 *  Specifies whether to load code, data, or symbols
 */
typedef s32 dbll_flags;

/*
 *  ======== DBLL_Library ========
 *
 */
struct dbll_library_obj;

/*
 *  ======== dbll_sect_info ========
 *  For collecting info on overlay sections
 */
struct dbll_sect_info {
	const char *name;	/* name of section */
	u32 sect_run_addr;	/* run address of section */
	u32 sect_load_addr;	/* load address of section */
	u32 size;		/* size of section (target MAUs) */
	dbll_flags type;	/* Code, data, or BSS */
};

/*
 *  ======== dbll_sym_val ========
 *  (Needed for dynamic load library)
 */
struct dbll_sym_val {
	u32 value;
};

/*
 *  ======== dbll_alloc_fxn ========
 *  Allocate memory function.  Allocate or reserve (if reserved == TRUE)
 *  "size" bytes of memory from segment "space" and return the address in
 *  *dsp_address (or starting at *dsp_address if reserve == TRUE). Returns 0 on
 *  success, or an error code on failure.
 */
typedef s32(*dbll_alloc_fxn) (void *hdl, s32 space, u32 size, u32 align,
			      u32 *dsp_address, s32 seg_id, s32 req,
			      bool reserved);

/*
 *  ======== dbll_close_fxn ========
 */
typedef s32(*dbll_f_close_fxn) (void *);

/*
 *  ======== dbll_free_fxn ========
 *  Free memory function.  Free, or unreserve (if reserved == TRUE) "size"
 *  bytes of memory from segment "space"
 */
typedef bool(*dbll_free_fxn) (void *hdl, u32 addr, s32 space, u32 size,
			      bool reserved);

/*
 *  ======== dbll_f_open_fxn ========
 */
typedef void *(*dbll_f_open_fxn) (const char *, const char *);

/*
 *  ======== dbll_log_write_fxn ========
 *  Function to call when writing data from a section, to log the info.
 *  Can be NULL if no logging is required.
 */
typedef int(*dbll_log_write_fxn) (void *handle,
					 struct dbll_sect_info *sect, u32 addr,
					 u32 bytes);

/*
 *  ======== dbll_read_fxn ========
 */
typedef s32(*dbll_read_fxn) (void *, size_t, size_t, void *);

/*
 *  ======== dbll_seek_fxn ========
 */
typedef s32(*dbll_seek_fxn) (void *, long, int);

/*
 *  ======== dbll_sym_lookup ========
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
typedef bool(*dbll_sym_lookup) (void *handle, void *parg, void *rmm_handle,
				const char *name, struct dbll_sym_val **sym);

/*
 *  ======== dbll_tell_fxn ========
 */
typedef s32(*dbll_tell_fxn) (void *);

/*
 *  ======== dbll_write_fxn ========
 *  Write memory function.  Write "n" HOST bytes of memory to segment "mtype"
 *  starting at address "dsp_address" from the buffer "buf".  The buffer is
 *  formatted as an array of words appropriate for the DSP.
 */
typedef s32(*dbll_write_fxn) (void *hdl, u32 dsp_address, void *buf,
			      u32 n, s32 mtype);

/*
 *  ======== dbll_attrs ========
 */
struct dbll_attrs {
	dbll_alloc_fxn alloc;
	dbll_free_fxn free;
	void *rmm_handle;	/* Handle to pass to alloc, free functions */
	dbll_write_fxn write;
	void *input_params;	/* Handle to pass to write, cinit function */
	bool base_image;
	dbll_log_write_fxn log_write;
	void *log_write_handle;

	/* Symbol matching function and handle to pass to it */
	dbll_sym_lookup sym_lookup;
	void *sym_handle;
	void *sym_arg;

	/*
	 *  These file manipulation functions should be compatible with the
	 *  "C" run time library functions of the same name.
	 */
	 s32 (*fread)(void *ptr, size_t size, size_t count, void *filp);
	 s32 (*fseek)(void *filp, long offset, int origin);
	 s32 (*ftell)(void *filp);
	 s32 (*fclose)(void *filp);
	 void *(*fopen)(const char *path, const char *mode);
};

/*
 *  ======== dbll_close ========
 *  Close library opened with dbll_open.
 *  Parameters:
 *      lib             - Handle returned from dbll_open().
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *  Ensures:
 */
typedef void (*dbll_close_fxn) (struct dbll_library_obj *library);

/*
 *  ======== dbll_create ========
 *  Create a target object, specifying the alloc, free, and write functions.
 *  Parameters:
 *      target_obj         - Location to store target handle on output.
 *      pattrs          - Attributes.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Memory allocation failed.
 *  Requires:
 *      DBL initialized.
 *      pattrs != NULL.
 *      target_obj != NULL;
 *  Ensures:
 *      Success:        *target_obj != NULL.
 *      Failure:        *target_obj == NULL.
 */
typedef int(*dbll_create_fxn) (struct dbll_tar_obj **target_obj,
				      struct dbll_attrs *attrs);

/*
 *  ======== dbll_delete ========
 *  Delete target object and free resources for any loaded libraries.
 *  Parameters:
 *      target          - Handle returned from DBLL_Create().
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *  Ensures:
 */
typedef void (*dbll_delete_fxn) (struct dbll_tar_obj *target);

/*
 *  ======== dbll_exit ========
 *  Discontinue use of DBL module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      refs > 0.
 *  Ensures:
 *      refs >= 0.
 */
typedef void (*dbll_exit_fxn) (void);

/*
 *  ======== dbll_get_addr ========
 *  Get address of name in the specified library.
 *  Parameters:
 *      lib             - Handle returned from dbll_open().
 *      name            - Name of symbol
 *      sym_val         - Location to store symbol address on output.
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Symbol not found.
 *  Requires:
 *      DBL initialized.
 *      Valid library.
 *      name != NULL.
 *      sym_val != NULL.
 *  Ensures:
 */
typedef bool(*dbll_get_addr_fxn) (struct dbll_library_obj *lib, char *name,
				  struct dbll_sym_val **sym_val);

/*
 *  ======== dbll_get_attrs ========
 *  Retrieve the attributes of the target.
 *  Parameters:
 *      target          - Handle returned from DBLL_Create().
 *      pattrs          - Location to store attributes on output.
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      pattrs != NULL.
 *  Ensures:
 */
typedef void (*dbll_get_attrs_fxn) (struct dbll_tar_obj *target,
				    struct dbll_attrs *attrs);

/*
 *  ======== dbll_get_c_addr ========
 *  Get address of "C" name on the specified library.
 *  Parameters:
 *      lib             - Handle returned from dbll_open().
 *      name            - Name of symbol
 *      sym_val         - Location to store symbol address on output.
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Symbol not found.
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      name != NULL.
 *      sym_val != NULL.
 *  Ensures:
 */
typedef bool(*dbll_get_c_addr_fxn) (struct dbll_library_obj *lib, char *name,
				    struct dbll_sym_val **sym_val);

/*
 *  ======== dbll_get_sect ========
 *  Get address and size of a named section.
 *  Parameters:
 *      lib             - Library handle returned from dbll_open().
 *      name            - Name of section.
 *      paddr           - Location to store section address on output.
 *      psize           - Location to store section size on output.
 *  Returns:
 *      0:        Success.
 *      -ENXIO:    Section not found.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      paddr != NULL;
 *      psize != NULL.
 *  Ensures:
 */
typedef int(*dbll_get_sect_fxn) (struct dbll_library_obj *lib,
					char *name, u32 *addr, u32 *size);

/*
 *  ======== dbll_init ========
 *  Initialize DBL module.
 *  Parameters:
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Failure.
 *  Requires:
 *      refs >= 0.
 *  Ensures:
 *      Success:        refs > 0.
 *      Failure:        refs >= 0.
 */
typedef bool(*dbll_init_fxn) (void);

/*
 *  ======== dbll_load ========
 *  Load library onto the target.
 *
 *  Parameters:
 *      lib             - Library handle returned from dbll_open().
 *      flags           - Load code, data and/or symbols.
 *      attrs           - May contain alloc, free, and write function.
 *      entry_pt        - Location to store program entry on output.
 *  Returns:
 *      0:        Success.
 *      -EBADF:     File read failed.
 *      -EILSEQ:   Failure in dynamic loader library.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      entry != NULL.
 *  Ensures:
 */
typedef int(*dbll_load_fxn) (struct dbll_library_obj *lib,
				    dbll_flags flags,
				    struct dbll_attrs *attrs, u32 *entry);
/*
 *  ======== dbll_open ========
 *  dbll_open() returns a library handle that can be used to load/unload
 *  the symbols/code/data via dbll_load()/dbll_unload().
 *  Parameters:
 *      target          - Handle returned from dbll_create().
 *      file            - Name of file to open.
 *      flags           - If flags & DBLL_SYMB, load symbols.
 *      lib_obj         - Location to store library handle on output.
 *  Returns:
 *      0:            Success.
 *      -ENOMEM:        Memory allocation failure.
 *      -EBADF:         File open/read failure.
 *                      Unable to determine target type.
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      file != NULL.
 *      lib_obj != NULL.
 *      dbll_attrs fopen function non-NULL.
 *  Ensures:
 *      Success:        Valid *lib_obj.
 *      Failure:        *lib_obj == NULL.
 */
typedef int(*dbll_open_fxn) (struct dbll_tar_obj *target, char *file,
				    dbll_flags flags,
				    struct dbll_library_obj **lib_obj);

/*
 *  ======== dbll_read_sect ========
 *  Read COFF section into a character buffer.
 *  Parameters:
 *      lib             - Library handle returned from dbll_open().
 *      name            - Name of section.
 *      pbuf            - Buffer to write section contents into.
 *      size            - Buffer size
 *  Returns:
 *      0:        Success.
 *      -ENXIO:    Named section does not exists.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      pbuf != NULL.
 *      size != 0.
 *  Ensures:
 */
typedef int(*dbll_read_sect_fxn) (struct dbll_library_obj *lib,
					 char *name, char *content,
					 u32 cont_size);
/*
 *  ======== dbll_unload ========
 *  Unload library loaded with dbll_load().
 *  Parameters:
 *      lib             - Handle returned from dbll_open().
 *      attrs           - Contains free() function and handle to pass to it.
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *  Ensures:
 */
typedef void (*dbll_unload_fxn) (struct dbll_library_obj *library,
				 struct dbll_attrs *attrs);
struct dbll_fxns {
	dbll_close_fxn close_fxn;
	dbll_create_fxn create_fxn;
	dbll_delete_fxn delete_fxn;
	dbll_exit_fxn exit_fxn;
	dbll_get_attrs_fxn get_attrs_fxn;
	dbll_get_addr_fxn get_addr_fxn;
	dbll_get_c_addr_fxn get_c_addr_fxn;
	dbll_get_sect_fxn get_sect_fxn;
	dbll_init_fxn init_fxn;
	dbll_load_fxn load_fxn;
	dbll_open_fxn open_fxn;
	dbll_read_sect_fxn read_sect_fxn;
	dbll_unload_fxn unload_fxn;
};

#endif /* DBLDEFS_ */
