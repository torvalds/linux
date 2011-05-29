/*
 * cod.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Code management module for DSPs. This module provides an interface
 * interface for loading both static and dynamic code objects onto DSP
 * systems.
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

#ifndef COD_
#define COD_

#include <dspbridge/dblldefs.h>

#define COD_MAXPATHLENGTH       255
#define COD_TRACEBEG            "SYS_PUTCBEG"
#define COD_TRACEEND            "SYS_PUTCEND"
#define COD_TRACECURPOS	"BRIDGE_SYS_PUTC_current"

#define COD_NOLOAD              DBLL_NOLOAD
#define COD_SYMB                DBLL_SYMB

/* COD code manager handle */
struct cod_manager;

/* COD library handle */
struct cod_libraryobj;

/*
 *  Function prototypes for writing memory to a DSP system, allocating
 *  and freeing DSP memory.
 */
typedef u32(*cod_writefxn) (void *priv_ref, u32 dsp_add,
			    void *pbuf, u32 ul_num_bytes, u32 mem_space);

/*
 *  ======== cod_close ========
 *  Purpose:
 *      Close a library opened with cod_open().
 *  Parameters:
 *      lib             - Library handle returned by cod_open().
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid lib.
 *  Ensures:
 *
 */
extern void cod_close(struct cod_libraryobj *lib);

/*
 *  ======== cod_create ========
 *  Purpose:
 *      Create an object to manage code on a DSP system. This object can be
 *      used to load an initial program image with arguments that can later
 *      be expanded with dynamically loaded object files.
 *      Symbol table information is managed by this object and can be retrieved
 *      using the cod_get_sym_value() function.
 *  Parameters:
 *      manager:        created manager object
 *      str_zl_file:    ZL DLL filename, of length < COD_MAXPATHLENGTH.
 *  Returns:
 *      0:                Success.
 *      -ESPIPE:   ZL_Create failed.
 *      -ENOSYS:           attrs was not NULL.  We don't yet support
 *                              non default values of attrs.
 *  Requires:
 *      COD module initialized.
 *      str_zl_file != NULL
 *  Ensures:
 */
extern int cod_create(struct cod_manager **mgr,
			     char *str_zl_file);

/*
 *  ======== cod_delete ========
 *  Purpose:
 *      Delete a code manager object.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *  Ensures:
 */
extern void cod_delete(struct cod_manager *cod_mgr_obj);

/*
 *  ======== cod_exit ========
 *  Purpose:
 *      Discontinue usage of the COD module.
 *  Parameters:
 *      None.
 *  Returns:
 *      None.
 *  Requires:
 *      COD initialized.
 *  Ensures:
 *      Resources acquired in cod_init(void) are freed.
 */
extern void cod_exit(void);

/*
 *  ======== cod_get_base_lib ========
 *  Purpose:
 *      Get handle to the base image DBL library.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      plib:       location to store library handle on output.
 *  Returns:
 *      0:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      plib != NULL.
 *  Ensures:
 */
extern int cod_get_base_lib(struct cod_manager *cod_mgr_obj,
				   struct dbll_library_obj **plib);

/*
 *  ======== cod_get_base_name ========
 *  Purpose:
 *      Get the name of the base image DBL library.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      sz_name:    location to store library name on output.
 *      usize:       size of name buffer.
 *  Returns:
 *      0:    Success.
 *      -EPERM:  Buffer too small.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      sz_name != NULL.
 *  Ensures:
 */
extern int cod_get_base_name(struct cod_manager *cod_mgr_obj,
				    char *sz_name, u32 usize);

/*
 *  ======== cod_get_entry ========
 *  Purpose:
 *      Retrieve the entry point of a loaded DSP program image
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      entry_pt:   pointer to location for entry point
 *  Returns:
 *      0:       Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      entry_pt != NULL.
 *  Ensures:
 */
extern int cod_get_entry(struct cod_manager *cod_mgr_obj,
				u32 *entry_pt);

/*
 *  ======== cod_get_loader ========
 *  Purpose:
 *      Get handle to the DBL loader.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      loader:     location to store loader handle on output.
 *  Returns:
 *      0:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      loader != NULL.
 *  Ensures:
 */
extern int cod_get_loader(struct cod_manager *cod_mgr_obj,
				 struct dbll_tar_obj **loader);

/*
 *  ======== cod_get_section ========
 *  Purpose:
 *      Retrieve the starting address and length of a section in the COFF file
 *      given the section name.
 *  Parameters:
 *      lib         Library handle returned from cod_open().
 *      str_sect:   name of the section, with or without leading "."
 *      addr:       Location to store address.
 *      len:        Location to store length.
 *  Returns:
 *      0:                Success
 *      -ESPIPE:  Symbols could not be found or have not been loaded onto
 *                the board.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      str_sect != NULL;
 *      addr != NULL;
 *      len != NULL;
 *  Ensures:
 *      0:  *addr and *len contain the address and length of the
 *                 section.
 *      else:  *addr == 0 and *len == 0;
 *
 */
extern int cod_get_section(struct cod_libraryobj *lib,
				  char *str_sect,
				  u32 *addr, u32 *len);

/*
 *  ======== cod_get_sym_value ========
 *  Purpose:
 *      Retrieve the value for the specified symbol. The symbol is first
 *      searched for literally and then, if not found, searched for as a
 *      C symbol.
 *  Parameters:
 *      lib:        library handle returned from cod_open().
 *      pstrSymbol: name of the symbol
 *      value:      value of the symbol
 *  Returns:
 *      0:                Success.
 *      -ESPIPE:  Symbols could not be found or have not been loaded onto
 *                the board.
 *  Requires:
 *      COD module initialized.
 *      Valid cod_mgr_obj.
 *      str_sym != NULL.
 *      pul_value != NULL.
 *  Ensures:
 */
extern int cod_get_sym_value(struct cod_manager *cod_mgr_obj,
				    char *str_sym, u32 * pul_value);

/*
 *  ======== cod_init ========
 *  Purpose:
 *      Initialize the COD module's private state.
 *  Parameters:
 *      None.
 *  Returns:
 *      TRUE if initialized; FALSE if error occurred.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public COD functions.
 */
extern bool cod_init(void);

/*
 *  ======== cod_load_base ========
 *  Purpose:
 *      Load the initial program image, optionally with command-line arguments,
 *      on the DSP system managed by the supplied handle. The program to be
 *      loaded must be the first element of the args array and must be a fully
 *      qualified pathname.
 *  Parameters:
 *      hmgr:       manager to load the code with
 *      num_argc:   number of arguments in the args array
 *      args:       array of strings for arguments to DSP program
 *      write_fxn:   board-specific function to write data to DSP system
 *      arb:       arbitrary pointer to be passed as first arg to write_fxn
 *      envp:       array of environment strings for DSP exec.
 *  Returns:
 *      0:                   Success.
 *      -EBADF:       Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      num_argc > 0.
 *      args != NULL.
 *      args[0] != NULL.
 *      pfn_write != NULL.
 *  Ensures:
 */
extern int cod_load_base(struct cod_manager *cod_mgr_obj,
				u32 num_argc, char *args[],
				cod_writefxn pfn_write, void *arb,
				char *envp[]);

/*
 *  ======== cod_open ========
 *  Purpose:
 *      Open a library for reading sections. Does not load or set the base.
 *  Parameters:
 *      hmgr:           manager to load the code with
 *      sz_coff_path:   Coff file to open.
 *      flags:          COD_NOLOAD (don't load symbols) or COD_SYMB (load
 *                      symbols).
 *      lib_obj:        Handle returned that can be used in calls to cod_close
 *                      and cod_get_section.
 *  Returns:
 *      S_OK:                   Success.
 *      -EBADF:       Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      flags == COD_NOLOAD || flags == COD_SYMB.
 *      sz_coff_path != NULL.
 *  Ensures:
 */
extern int cod_open(struct cod_manager *hmgr,
			   char *sz_coff_path,
			   u32 flags, struct cod_libraryobj **lib_obj);

/*
 *  ======== cod_open_base ========
 *  Purpose:
 *      Open base image for reading sections. Does not load the base.
 *  Parameters:
 *      hmgr:           manager to load the code with
 *      sz_coff_path:   Coff file to open.
 *      flags:          Specifies whether to load symbols.
 *  Returns:
 *      0:            Success.
 *      -EBADF:   Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      sz_coff_path != NULL.
 *  Ensures:
 */
extern int cod_open_base(struct cod_manager *hmgr, char *sz_coff_path,
				dbll_flags flags);

/*
 *  ======== cod_read_section ========
 *  Purpose:
 *      Retrieve the content of a code section given the section name.
 *  Parameters:
 *      cod_mgr_obj    - manager in which to search for the symbol
 *      str_sect    - name of the section, with or without leading "."
 *      str_content - buffer to store content of the section.
 *  Returns:
 *      0: on success, error code on failure
 *      -ESPIPE:  Symbols have not been loaded onto the board.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      str_sect != NULL;
 *      str_content != NULL;
 *  Ensures:
 *      0:  *str_content stores the content of the named section.
 */
extern int cod_read_section(struct cod_libraryobj *lib,
				   char *str_sect,
				   char *str_content, u32 content_size);

#endif /* COD_ */
