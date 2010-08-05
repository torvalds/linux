/*
 * dbll.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 *  DSP/BIOS Bridge Dynamic load library module interface. Function header
 *  comments are in the file dblldefs.h.
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

#ifndef DBLL_
#define DBLL_

#include <dspbridge/dbdefs.h>
#include <dspbridge/dblldefs.h>

extern bool symbols_reloaded;

extern void dbll_close(struct dbll_library_obj *zl_lib);
extern int dbll_create(struct dbll_tar_obj **target_obj,
			      struct dbll_attrs *pattrs);
extern void dbll_delete(struct dbll_tar_obj *target);
extern void dbll_exit(void);
extern bool dbll_get_addr(struct dbll_library_obj *zl_lib, char *name,
			  struct dbll_sym_val **sym_val);
extern void dbll_get_attrs(struct dbll_tar_obj *target,
			   struct dbll_attrs *pattrs);
extern bool dbll_get_c_addr(struct dbll_library_obj *zl_lib, char *name,
			    struct dbll_sym_val **sym_val);
extern int dbll_get_sect(struct dbll_library_obj *lib, char *name,
				u32 *paddr, u32 *psize);
extern bool dbll_init(void);
extern int dbll_load(struct dbll_library_obj *lib,
			    dbll_flags flags,
			    struct dbll_attrs *attrs, u32 * entry);
extern int dbll_load_sect(struct dbll_library_obj *zl_lib,
				 char *sec_name, struct dbll_attrs *attrs);
extern int dbll_open(struct dbll_tar_obj *target, char *file,
			    dbll_flags flags,
		       struct dbll_library_obj **lib_obj);
extern int dbll_read_sect(struct dbll_library_obj *lib,
				 char *name, char *buf, u32 size);
extern void dbll_set_attrs(struct dbll_tar_obj *target,
			   struct dbll_attrs *pattrs);
extern void dbll_unload(struct dbll_library_obj *lib, struct dbll_attrs *attrs);
extern int dbll_unload_sect(struct dbll_library_obj *lib,
				   char *sect_name, struct dbll_attrs *attrs);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
bool dbll_find_dsp_symbol(struct dbll_library_obj *zl_lib, u32 address,
		u32 offset_range, u32 *sym_addr_output, char *name_output);
#endif

#endif /* DBLL_ */
