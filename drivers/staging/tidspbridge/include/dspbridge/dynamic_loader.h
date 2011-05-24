/*
 * dynamic_loader.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _DYNAMIC_LOADER_H_
#define _DYNAMIC_LOADER_H_
#include <linux/kernel.h>
#include <linux/types.h>

/*
 * Dynamic Loader
 *
 * The function of the dynamic loader is to load a "module" containing
 * instructions for a "target" processor into that processor.  In the process
 * it assigns memory for the module, resolves symbol references made by the
 * module, and remembers symbols defined by the module.
 *
 * The dynamic loader is parameterized for a particular system by 4 classes
 * that supply the module and system specific functions it requires
 */
	/* The read functions for the module image to be loaded */
struct dynamic_loader_stream;

	/* This class defines "host" symbol and support functions */
struct dynamic_loader_sym;

	/* This class defines the allocator for "target" memory */
struct dynamic_loader_allocate;

	/* This class defines the copy-into-target-memory functions */
struct dynamic_loader_initialize;

/*
 * Option flags to modify the behavior of module loading
 */
#define DLOAD_INITBSS 0x1	/* initialize BSS sections to zero */

/*****************************************************************************
 * Procedure dynamic_load_module
 *
 * Parameters:
 *  module  The input stream that supplies the module image
 *  syms    Host-side symbol table and malloc/free functions
 *  alloc   Target-side memory allocation
 *  init    Target-side memory initialization, or NULL for symbol read only
 *  options Option flags DLOAD_*
 *  mhandle A module handle for use with Dynamic_Unload
 *
 * Effect:
 *  The module image is read using *module.  Target storage for the new image is
 * obtained from *alloc.  Symbols defined and referenced by the module are
 * managed using *syms.  The image is then relocated and references resolved
 * as necessary, and the resulting executable bits are placed into target memory
 * using *init.
 *
 * Returns:
 *  On a successful load, a module handle is placed in *mhandle, and zero is
 * returned.  On error, the number of errors detected is returned.  Individual
 * errors are reported during the load process using syms->error_report().
 **************************************************************************** */
extern int dynamic_load_module(
				      /* the source for the module image */
				      struct dynamic_loader_stream *module,
				      /* host support for symbols and storage */
				      struct dynamic_loader_sym *syms,
				      /* the target memory allocator */
				      struct dynamic_loader_allocate *alloc,
				      /* the target memory initializer */
				      struct dynamic_loader_initialize *init,
				      unsigned options,	/* option flags */
				      /* the returned module handle */
				      void **mhandle);

/*****************************************************************************
 * Procedure dynamic_open_module
 *
 * Parameters:
 *  module  The input stream that supplies the module image
 *  syms    Host-side symbol table and malloc/free functions
 *  alloc   Target-side memory allocation
 *  init    Target-side memory initialization, or NULL for symbol read only
 *  options Option flags DLOAD_*
 *  mhandle A module handle for use with Dynamic_Unload
 *
 * Effect:
 *  The module image is read using *module.  Target storage for the new image is
 * obtained from *alloc.  Symbols defined and referenced by the module are
 * managed using *syms.  The image is then relocated and references resolved
 * as necessary, and the resulting executable bits are placed into target memory
 * using *init.
 *
 * Returns:
 *  On a successful load, a module handle is placed in *mhandle, and zero is
 * returned.  On error, the number of errors detected is returned.  Individual
 * errors are reported during the load process using syms->error_report().
 **************************************************************************** */
extern int dynamic_open_module(
				      /* the source for the module image */
				      struct dynamic_loader_stream *module,
				      /* host support for symbols and storage */
				      struct dynamic_loader_sym *syms,
				      /* the target memory allocator */
				      struct dynamic_loader_allocate *alloc,
				      /* the target memory initializer */
				      struct dynamic_loader_initialize *init,
				      unsigned options,	/* option flags */
				      /* the returned module handle */
				      void **mhandle);

/*****************************************************************************
 * Procedure dynamic_unload_module
 *
 * Parameters:
 *  mhandle A module handle from dynamic_load_module
 *  syms    Host-side symbol table and malloc/free functions
 *  alloc   Target-side memory allocation
 *
 * Effect:
 *  The module specified by mhandle is unloaded.  Unloading causes all
 * target memory to be deallocated, all symbols defined by the module to
 * be purged, and any host-side storage used by the dynamic loader for
 * this module to be released.
 *
 * Returns:
 *  Zero for success. On error, the number of errors detected is returned.
 * Individual errors are reported using syms->error_report().
 **************************************************************************** */
extern int dynamic_unload_module(void *mhandle,	/* the module
							 * handle */
				 /* host support for symbols and
				  * storage */
				 struct dynamic_loader_sym *syms,
				 /* the target memory allocator */
				 struct dynamic_loader_allocate *alloc,
				 /* the target memory initializer */
				 struct dynamic_loader_initialize *init);

/*****************************************************************************
 *****************************************************************************
 * A class used by the dynamic loader for input of the module image
 *****************************************************************************
 **************************************************************************** */
struct dynamic_loader_stream {
/* public: */
    /*************************************************************************
     * read_buffer
     *
     * PARAMETERS :
     *  buffer  Pointer to the buffer to fill
     *  bufsiz  Amount of data desired in sizeof() units
     *
     * EFFECT :
     *  Reads the specified amount of data from the module input stream
     * into the specified buffer.  Returns the amount of data read in sizeof()
     * units (which if less than the specification, represents an error).
     *
     * NOTES:
     *  In release 1 increments the file position by the number of bytes read
     *
     ************************************************************************ */
	int (*read_buffer) (struct dynamic_loader_stream *thisptr,
			    void *buffer, unsigned bufsiz);

    /*************************************************************************
     * set_file_posn (release 1 only)
     *
     * PARAMETERS :
     *  posn  Desired file position relative to start of file in sizeof() units.
     *
     * EFFECT :
     *  Adjusts the internal state of the stream object so that the next
     * read_buffer call will begin to read at the specified offset from
     * the beginning of the input module.  Returns 0 for success, non-zero
     * for failure.
     *
     ************************************************************************ */
	int (*set_file_posn) (struct dynamic_loader_stream *thisptr,
			      /* to be eliminated in release 2 */
			      unsigned int posn);

};

/*****************************************************************************
 *****************************************************************************
 * A class used by the dynamic loader for symbol table support and
 * miscellaneous host-side functions
 *****************************************************************************
 **************************************************************************** */

typedef u32 ldr_addr;

/*
 * the structure of a symbol known to the dynamic loader
 */
struct dynload_symbol {
	ldr_addr value;
};

struct dynamic_loader_sym {
/* public: */
    /*************************************************************************
     * find_matching_symbol
     *
     * PARAMETERS :
     *  name    The name of the desired symbol
     *
     * EFFECT :
     *  Locates a symbol matching the name specified.  A pointer to the
     * symbol is returned if it exists; 0 is returned if no such symbol is
     * found.
     *
     ************************************************************************ */
	struct dynload_symbol *(*find_matching_symbol)
	 (struct dynamic_loader_sym *thisptr, const char *name);

    /*************************************************************************
     * add_to_symbol_table
     *
     * PARAMETERS :
     *  nname       Pointer to the name of the new symbol
     *  moduleid    An opaque module id assigned by the dynamic loader
     *
     * EFFECT :
     *  The new symbol is added to the table.  A pointer to the symbol is
     * returned, or NULL is returned for failure.
     *
     * NOTES:
     *  It is permissible for this function to return NULL; the effect is that
     * the named symbol will not be available to resolve references in
     * subsequent loads.  Returning NULL will not cause the current load
     * to fail.
     ************************************************************************ */
	struct dynload_symbol *(*add_to_symbol_table)
	 (struct dynamic_loader_sym *
	  thisptr, const char *nname, unsigned moduleid);

    /*************************************************************************
     * purge_symbol_table
     *
     * PARAMETERS :
     *  moduleid    An opaque module id assigned by the dynamic loader
     *
     * EFFECT :
     *  Each symbol in the symbol table whose moduleid matches the argument
     * is removed from the table.
     ************************************************************************ */
	void (*purge_symbol_table) (struct dynamic_loader_sym *thisptr,
				    unsigned moduleid);

    /*************************************************************************
     * dload_allocate
     *
     * PARAMETERS :
     *  memsiz  size of desired memory in sizeof() units
     *
     * EFFECT :
     *  Returns a pointer to some "host" memory for use by the dynamic
     * loader, or NULL for failure.
     * This function is serves as a replaceable form of "malloc" to
     * allow the user to configure the memory usage of the dynamic loader.
     ************************************************************************ */
	void *(*dload_allocate) (struct dynamic_loader_sym *thisptr,
				 unsigned memsiz);

    /*************************************************************************
     * dload_deallocate
     *
     * PARAMETERS :
     *  memptr  pointer to previously allocated memory
     *
     * EFFECT :
     *  Releases the previously allocated "host" memory.
     ************************************************************************ */
	void (*dload_deallocate) (struct dynamic_loader_sym *thisptr,
				  void *memptr);

    /*************************************************************************
     * error_report
     *
     * PARAMETERS :
     *  errstr  pointer to an error string
     *  args    additional arguments
     *
     * EFFECT :
     *  This function provides an error reporting interface for the dynamic
     * loader.  The error string and arguments are designed as for the
     * library function vprintf.
     ************************************************************************ */
	void (*error_report) (struct dynamic_loader_sym *thisptr,
			      const char *errstr, va_list args);

};				/* class dynamic_loader_sym */

/*****************************************************************************
 *****************************************************************************
 * A class used by the dynamic loader to allocate and deallocate target memory.
 *****************************************************************************
 **************************************************************************** */

struct ldr_section_info {
	/* Name of the memory section assigned at build time */
	const char *name;
	ldr_addr run_addr;	/* execution address of the section */
	ldr_addr load_addr;	/* load address of the section */
	ldr_addr size;		/* size of the section in addressable units */
#ifndef _BIG_ENDIAN
	u16 page;		/* memory page or view */
	u16 type;		/* one of the section types below */
#else
	u16 type;		/* one of the section types below */
	u16 page;		/* memory page or view */
#endif
	/* a context field for use by dynamic_loader_allocate;
	 *   ignored but maintained by the dynamic loader */
	u32 context;
};

/* use this macro to extract type of section from ldr_section_info.type field */
#define DLOAD_SECTION_TYPE(typeinfo) (typeinfo & 0xF)

/* type of section to be allocated */
#define DLOAD_TEXT 0
#define DLOAD_DATA 1
#define DLOAD_BSS 2
	/* internal use only, run-time cinit will be of type DLOAD_DATA */
#define DLOAD_CINIT 3

struct dynamic_loader_allocate {
/* public: */

    /*************************************************************************
    * Function allocate
    *
    * Parameters:
    *   info        A pointer to an information block for the section
    *   align       The alignment of the storage in target AUs
    *
    * Effect:
    *   Allocates target memory for the specified section and fills in the
    * load_addr and run_addr fields of the section info structure. Returns TRUE
    * for success, FALSE for failure.
    *
    * Notes:
    *   Frequently load_addr and run_addr are the same, but if they are not
    * load_addr is used with dynamic_loader_initialize, and run_addr is
    * used for almost all relocations.  This function should always initialize
    * both fields.
    ************************************************************************ */
	int (*dload_allocate) (struct dynamic_loader_allocate *thisptr,
			       struct ldr_section_info *info, unsigned align);

    /*************************************************************************
    * Function deallocate
    *
    * Parameters:
    *   info        A pointer to an information block for the section
    *
    * Effect:
    *   Releases the target memory previously allocated.
    *
    * Notes:
    * The content of the info->name field is undefined on call to this function.
    ************************************************************************ */
	void (*dload_deallocate) (struct dynamic_loader_allocate *thisptr,
				  struct ldr_section_info *info);

};				/* class dynamic_loader_allocate */

/*****************************************************************************
 *****************************************************************************
 * A class used by the dynamic loader to load data into a target.  This class
 * provides the interface-specific functions needed to load data.
 *****************************************************************************
 **************************************************************************** */

struct dynamic_loader_initialize {
/* public: */
    /*************************************************************************
    * Function connect
    *
    * Parameters:
    *   none
    *
    * Effect:
    *   Connect to the initialization interface. Returns TRUE for success,
    * FALSE for failure.
    *
    * Notes:
    *   This function is called prior to use of any other functions in
    * this interface.
    ************************************************************************ */
	int (*connect) (struct dynamic_loader_initialize *thisptr);

    /*************************************************************************
    * Function readmem
    *
    * Parameters:
    *   bufr        Pointer to a word-aligned buffer for the result
    *   locn        Target address of first data element
    *   info        Section info for the section in which the address resides
    *   bytsiz      Size of the data to be read in sizeof() units
    *
    * Effect:
    *   Fills the specified buffer with data from the target.  Returns TRUE for
    * success, FALSE for failure.
    ************************************************************************ */
	int (*readmem) (struct dynamic_loader_initialize *thisptr,
			void *bufr,
			ldr_addr locn,
			struct ldr_section_info *info, unsigned bytsiz);

    /*************************************************************************
    * Function writemem
    *
    * Parameters:
    *   bufr        Pointer to a word-aligned buffer of data
    *   locn        Target address of first data element to be written
    *   info        Section info for the section in which the address resides
    *   bytsiz      Size of the data to be written in sizeof() units
    *
    * Effect:
    *   Writes the specified buffer to the target.  Returns TRUE for success,
    * FALSE for failure.
    ************************************************************************ */
	int (*writemem) (struct dynamic_loader_initialize *thisptr,
			 void *bufr,
			 ldr_addr locn,
			 struct ldr_section_info *info, unsigned bytsiz);

    /*************************************************************************
    * Function fillmem
    *
    * Parameters:
    *   locn        Target address of first data element to be written
    *   info        Section info for the section in which the address resides
    *   bytsiz      Size of the data to be written in sizeof() units
    *   val         Value to be written in each byte
    * Effect:
    *   Fills the specified area of target memory.  Returns TRUE for success,
    * FALSE for failure.
    ************************************************************************ */
	int (*fillmem) (struct dynamic_loader_initialize *thisptr,
			ldr_addr locn, struct ldr_section_info *info,
			unsigned bytsiz, unsigned val);

    /*************************************************************************
    * Function execute
    *
    * Parameters:
    *   start       Starting address
    *
    * Effect:
    *   The target code at the specified starting address is executed.
    *
    * Notes:
    *   This function is called at the end of the dynamic load process
    * if the input module has specified a starting address.
    ************************************************************************ */
	int (*execute) (struct dynamic_loader_initialize *thisptr,
			ldr_addr start);

    /*************************************************************************
    * Function release
    *
    * Parameters:
    *   none
    *
    * Effect:
    *   Releases the connection to the load interface.
    *
    * Notes:
    *   This function is called at the end of the dynamic load process.
    ************************************************************************ */
	void (*release) (struct dynamic_loader_initialize *thisptr);

};				/* class dynamic_loader_initialize */

#endif /* _DYNAMIC_LOADER_H_ */
