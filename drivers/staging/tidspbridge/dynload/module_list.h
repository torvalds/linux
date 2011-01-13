/*
 * dspbridge/mpu_driver/src/dynload/module_list.h
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

/*
 * This C header file gives the layout of the data structure created by the
 * dynamic loader to describe the set of modules loaded into the DSP.
 *
 * Linked List Structure:
 * ----------------------
 * The data structure defined here is a singly-linked list.  The list
 * represents the set of modules which are currently loaded in the DSP memory.
 * The first entry in the list is a header record which contains a flag
 * representing the state of the list.  The rest of the entries in the list
 * are module records.
 *
 * Global symbol  _DLModules designates the first record in the list (i.e. the
 * header record).  This symbol must be defined in any program that wishes to
 * use DLLview plug-in.
 *
 * String Representation:
 * ----------------------
 * The string names of the module and its sections are stored in a block of
 * memory which follows the module record itself.  The strings are ordered:
 * module name first, followed by section names in order from the first
 * section to the last.  String names are tightly packed arrays of 8-bit
 * characters (two characters per 16-bit word on the C55x).  Strings are
 * zero-byte-terminated.
 *
 * Creating and updating the list:
 * -------------------------------
 * Upon loading a new module into the DSP memory the dynamic loader inserts a
 * new module record as the first module record in the list.  The fields of
 * this module record are initialized to reflect the properties of the module.
 * The dynamic loader does NOT increment the flag/counter in the list's header
 * record.
 *
 * Upon unloading a module from the DSP memory the dynamic loader removes the
 * module's record from this list.  The dynamic loader also increments the
 * flag/counter in the list's header record to indicate that the list has been
 * changed.
 */

#ifndef _MODULE_LIST_H_
#define _MODULE_LIST_H_

#include <linux/types.h>

/* Global pointer to the modules_header structure */
#define MODULES_HEADER "_DLModules"
#define MODULES_HEADER_NO_UNDERSCORE "DLModules"

/* Initial version number */
#define INIT_VERSION 1

/* Verification number -- to be recorded in each module record */
#define VERIFICATION 0x79

/* forward declarations */
struct dll_module;
struct dll_sect;

/* the first entry in the list is the modules_header record;
 * its address is contained in the global _DLModules pointer */
struct modules_header {

	/*
	 * Address of the first dll_module record in the list or NULL.
	 * Note: for C55x this is a word address (C55x data is
	 * word-addressable)
	 */
	u32 first_module;

	/* Combined storage size (in target addressable units) of the
	 * dll_module record which follows this header record, or zero
	 * if the list is empty.  This size includes the module's string table.
	 * Note: for C55x the unit is a 16-bit word */
	u16 first_module_size;

	/* Counter is incremented whenever a module record is removed from
	 * the list */
	u16 update_flag;

};

/* for each 32-bits in above structure, a bitmap, LSB first, whose bits are:
 * 0 => a 32-bit value, 1 => 2 16-bit values */
/* swapping bitmap for type modules_header */
#define MODULES_HEADER_BITMAP 0x2

/* information recorded about each section in a module */
struct dll_sect {

	/* Load-time address of the section.
	 * Note: for C55x this is a byte address for program sections, and
	 * a word address for data sections.  C55x program memory is
	 * byte-addressable, while data memory is word-addressable. */
	u32 sect_load_adr;

	/* Run-time address of the section.
	 * Note 1: for C55x this is a byte address for program sections, and
	 * a word address for data sections.
	 * Note 2: for C55x two most significant bits of this field indicate
	 * the section type: '00' for a code section, '11' for a data section
	 * (C55 addresses are really only 24-bits wide). */
	u32 sect_run_adr;

};

/* the rest of the entries in the list are module records */
struct dll_module {

	/* Address of the next dll_module record in the list, or 0 if this is
	 * the last record in the list.
	 * Note: for C55x this is a word address (C55x data is
	 * word-addressable) */
	u32 next_module;

	/* Combined storage size (in target addressable units) of the
	 * dll_module record which follows this one, or zero if this is the
	 * last record in the list.  This size includes the module's string
	 * table.
	 * Note: for C55x the unit is a 16-bit word. */
	u16 next_module_size;

	/* version number of the tooling; set to INIT_VERSION for Phase 1 */
	u16 version;

	/* the verification word; set to VERIFICATION */
	u16 verification;

	/* Number of sections in the sects array */
	u16 num_sects;

	/* Module's "unique" id; copy of the timestamp from the host
	 * COFF file */
	u32 timestamp;

	/* Array of num_sects elements of the module's section records */
	struct dll_sect sects[1];
};

/* for each 32 bits in above structure, a bitmap, LSB first, whose bits are:
 * 0 => a 32-bit value, 1 => 2 16-bit values */
#define DLL_MODULE_BITMAP 0x6	/* swapping bitmap for type dll_module */

#endif /* _MODULE_LIST_H_ */
