/*
 * cod.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This module implements DSP code management for the DSP/BIOS Bridge
 * environment. It is mostly a thin wrapper.
 *
 * This module provides an interface for loading both static and
 * dynamic code objects onto DSP systems.
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

#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Platform Manager */
/* Include appropriate loader header file */
#include <dspbridge/dbll.h>

/*  ----------------------------------- This */
#include <dspbridge/cod.h>

/*
 *  ======== cod_manager ========
 */
struct cod_manager {
	struct dbll_tar_obj *target;
	struct dbll_library_obj *base_lib;
	bool loaded;		/* Base library loaded? */
	u32 entry;
	struct dbll_fxns fxns;
	struct dbll_attrs attrs;
	char sz_zl_file[COD_MAXPATHLENGTH];
};

/*
 *  ======== cod_libraryobj ========
 */
struct cod_libraryobj {
	struct dbll_library_obj *dbll_lib;
	struct cod_manager *cod_mgr;
};

static struct dbll_fxns ldr_fxns = {
	(dbll_close_fxn) dbll_close,
	(dbll_create_fxn) dbll_create,
	(dbll_delete_fxn) dbll_delete,
	(dbll_exit_fxn) dbll_exit,
	(dbll_get_attrs_fxn) dbll_get_attrs,
	(dbll_get_addr_fxn) dbll_get_addr,
	(dbll_get_c_addr_fxn) dbll_get_c_addr,
	(dbll_get_sect_fxn) dbll_get_sect,
	(dbll_init_fxn) dbll_init,
	(dbll_load_fxn) dbll_load,
	(dbll_open_fxn) dbll_open,
	(dbll_read_sect_fxn) dbll_read_sect,
	(dbll_unload_fxn) dbll_unload,
};

static bool no_op(void);

/*
 * File operations (originally were under kfile.c)
 */
static s32 cod_f_close(struct file *filp)
{
	/* Check for valid handle */
	if (!filp)
		return -EFAULT;

	filp_close(filp, NULL);

	/* we can't use 0 here */
	return 0;
}

static struct file *cod_f_open(const char *psz_file_name, const char *sz_mode)
{
	mm_segment_t fs;
	struct file *filp;

	fs = get_fs();
	set_fs(get_ds());

	/* ignore given mode and open file as read-only */
	filp = filp_open(psz_file_name, O_RDONLY, 0);

	if (IS_ERR(filp))
		filp = NULL;

	set_fs(fs);

	return filp;
}

static s32 cod_f_read(void __user *pbuffer, s32 size, s32 count,
		      struct file *filp)
{
	/* check for valid file handle */
	if (!filp)
		return -EFAULT;

	if ((size > 0) && (count > 0) && pbuffer) {
		u32 dw_bytes_read;
		mm_segment_t fs;

		/* read from file */
		fs = get_fs();
		set_fs(get_ds());
		dw_bytes_read = filp->f_op->read(filp, pbuffer, size * count,
						 &(filp->f_pos));
		set_fs(fs);

		if (!dw_bytes_read)
			return -EBADF;

		return dw_bytes_read / size;
	}

	return -EINVAL;
}

static s32 cod_f_seek(struct file *filp, s32 offset, s32 origin)
{
	loff_t dw_cur_pos;

	/* check for valid file handle */
	if (!filp)
		return -EFAULT;

	/* based on the origin flag, move the internal pointer */
	dw_cur_pos = filp->f_op->llseek(filp, offset, origin);

	if ((s32) dw_cur_pos < 0)
		return -EPERM;

	/* we can't use 0 here */
	return 0;
}

static s32 cod_f_tell(struct file *filp)
{
	loff_t dw_cur_pos;

	if (!filp)
		return -EFAULT;

	/* Get current position */
	dw_cur_pos = filp->f_op->llseek(filp, 0, SEEK_CUR);

	if ((s32) dw_cur_pos < 0)
		return -EPERM;

	return dw_cur_pos;
}

/*
 *  ======== cod_close ========
 */
void cod_close(struct cod_libraryobj *lib)
{
	struct cod_manager *hmgr;

	hmgr = lib->cod_mgr;
	hmgr->fxns.close_fxn(lib->dbll_lib);

	kfree(lib);
}

/*
 *  ======== cod_create ========
 *  Purpose:
 *      Create an object to manage code on a DSP system.
 *      This object can be used to load an initial program image with
 *      arguments that can later be expanded with
 *      dynamically loaded object files.
 *
 */
int cod_create(struct cod_manager **mgr, char *str_zl_file)
{
	struct cod_manager *mgr_new;
	struct dbll_attrs zl_attrs;
	int status = 0;

	/* assume failure */
	*mgr = NULL;

	mgr_new = kzalloc(sizeof(struct cod_manager), GFP_KERNEL);
	if (mgr_new == NULL)
		return -ENOMEM;

	/* Set up loader functions */
	mgr_new->fxns = ldr_fxns;

	/* initialize the ZL module */
	mgr_new->fxns.init_fxn();

	zl_attrs.alloc = (dbll_alloc_fxn) no_op;
	zl_attrs.free = (dbll_free_fxn) no_op;
	zl_attrs.fread = (dbll_read_fxn) cod_f_read;
	zl_attrs.fseek = (dbll_seek_fxn) cod_f_seek;
	zl_attrs.ftell = (dbll_tell_fxn) cod_f_tell;
	zl_attrs.fclose = (dbll_f_close_fxn) cod_f_close;
	zl_attrs.fopen = (dbll_f_open_fxn) cod_f_open;
	zl_attrs.sym_lookup = NULL;
	zl_attrs.base_image = true;
	zl_attrs.log_write = NULL;
	zl_attrs.log_write_handle = NULL;
	zl_attrs.write = NULL;
	zl_attrs.rmm_handle = NULL;
	zl_attrs.input_params = NULL;
	zl_attrs.sym_handle = NULL;
	zl_attrs.sym_arg = NULL;

	mgr_new->attrs = zl_attrs;

	status = mgr_new->fxns.create_fxn(&mgr_new->target, &zl_attrs);

	if (status) {
		cod_delete(mgr_new);
		return -ESPIPE;
	}

	/* return the new manager */
	*mgr = mgr_new;

	return 0;
}

/*
 *  ======== cod_delete ========
 *  Purpose:
 *      Delete a code manager object.
 */
void cod_delete(struct cod_manager *cod_mgr_obj)
{
	if (cod_mgr_obj->base_lib) {
		if (cod_mgr_obj->loaded)
			cod_mgr_obj->fxns.unload_fxn(cod_mgr_obj->base_lib,
							&cod_mgr_obj->attrs);

		cod_mgr_obj->fxns.close_fxn(cod_mgr_obj->base_lib);
	}
	if (cod_mgr_obj->target) {
		cod_mgr_obj->fxns.delete_fxn(cod_mgr_obj->target);
		cod_mgr_obj->fxns.exit_fxn();
	}
	kfree(cod_mgr_obj);
}

/*
 *  ======== cod_get_base_lib ========
 *  Purpose:
 *      Get handle to the base image DBL library.
 */
int cod_get_base_lib(struct cod_manager *cod_mgr_obj,
			    struct dbll_library_obj **plib)
{
	int status = 0;

	*plib = (struct dbll_library_obj *)cod_mgr_obj->base_lib;

	return status;
}

/*
 *  ======== cod_get_base_name ========
 */
int cod_get_base_name(struct cod_manager *cod_mgr_obj, char *sz_name,
			     u32 usize)
{
	int status = 0;

	if (usize <= COD_MAXPATHLENGTH)
		strncpy(sz_name, cod_mgr_obj->sz_zl_file, usize);
	else
		status = -EPERM;

	return status;
}

/*
 *  ======== cod_get_entry ========
 *  Purpose:
 *      Retrieve the entry point of a loaded DSP program image
 *
 */
int cod_get_entry(struct cod_manager *cod_mgr_obj, u32 *entry_pt)
{
	*entry_pt = cod_mgr_obj->entry;

	return 0;
}

/*
 *  ======== cod_get_loader ========
 *  Purpose:
 *      Get handle to the DBLL loader.
 */
int cod_get_loader(struct cod_manager *cod_mgr_obj,
			  struct dbll_tar_obj **loader)
{
	int status = 0;

	*loader = (struct dbll_tar_obj *)cod_mgr_obj->target;

	return status;
}

/*
 *  ======== cod_get_section ========
 *  Purpose:
 *      Retrieve the starting address and length of a section in the COFF file
 *      given the section name.
 */
int cod_get_section(struct cod_libraryobj *lib, char *str_sect,
			   u32 *addr, u32 *len)
{
	struct cod_manager *cod_mgr_obj;
	int status = 0;

	*addr = 0;
	*len = 0;
	if (lib != NULL) {
		cod_mgr_obj = lib->cod_mgr;
		status = cod_mgr_obj->fxns.get_sect_fxn(lib->dbll_lib, str_sect,
							addr, len);
	} else {
		status = -ESPIPE;
	}

	return status;
}

/*
 *  ======== cod_get_sym_value ========
 *  Purpose:
 *      Retrieve the value for the specified symbol. The symbol is first
 *      searched for literally and then, if not found, searched for as a
 *      C symbol.
 *
 */
int cod_get_sym_value(struct cod_manager *cod_mgr_obj, char *str_sym,
			     u32 *pul_value)
{
	struct dbll_sym_val *dbll_sym;

	dev_dbg(bridge, "%s: cod_mgr_obj: %p str_sym: %s pul_value: %p\n",
		__func__, cod_mgr_obj, str_sym, pul_value);
	if (cod_mgr_obj->base_lib) {
		if (!cod_mgr_obj->fxns.
		    get_addr_fxn(cod_mgr_obj->base_lib, str_sym, &dbll_sym)) {
			if (!cod_mgr_obj->fxns.
			    get_c_addr_fxn(cod_mgr_obj->base_lib, str_sym,
						&dbll_sym))
				return -ESPIPE;
		}
	} else {
		return -ESPIPE;
	}

	*pul_value = dbll_sym->value;

	return 0;
}

/*
 *  ======== cod_load_base ========
 *  Purpose:
 *      Load the initial program image, optionally with command-line arguments,
 *      on the DSP system managed by the supplied handle. The program to be
 *      loaded must be the first element of the args array and must be a fully
 *      qualified pathname.
 *  Details:
 *      if num_argc doesn't match the number of arguments in the args array, the
 *      args array is searched for a NULL terminating entry, and argc is
 *      recalculated to reflect this.  In this way, we can support NULL
 *      terminating args arrays, if num_argc is very large.
 */
int cod_load_base(struct cod_manager *cod_mgr_obj, u32 num_argc, char *args[],
			 cod_writefxn pfn_write, void *arb, char *envp[])
{
	dbll_flags flags;
	struct dbll_attrs save_attrs;
	struct dbll_attrs new_attrs;
	int status;
	u32 i;

	/*
	 *  Make sure every argv[] stated in argc has a value, or change argc to
	 *  reflect true number in NULL terminated argv array.
	 */
	for (i = 0; i < num_argc; i++) {
		if (args[i] == NULL) {
			num_argc = i;
			break;
		}
	}

	/* set the write function for this operation */
	cod_mgr_obj->fxns.get_attrs_fxn(cod_mgr_obj->target, &save_attrs);

	new_attrs = save_attrs;
	new_attrs.write = (dbll_write_fxn) pfn_write;
	new_attrs.input_params = arb;
	new_attrs.alloc = (dbll_alloc_fxn) no_op;
	new_attrs.free = (dbll_free_fxn) no_op;
	new_attrs.log_write = NULL;
	new_attrs.log_write_handle = NULL;

	/* Load the image */
	flags = DBLL_CODE | DBLL_DATA | DBLL_SYMB;
	status = cod_mgr_obj->fxns.load_fxn(cod_mgr_obj->base_lib, flags,
					    &new_attrs,
					    &cod_mgr_obj->entry);
	if (status)
		cod_mgr_obj->fxns.close_fxn(cod_mgr_obj->base_lib);

	if (!status)
		cod_mgr_obj->loaded = true;
	else
		cod_mgr_obj->base_lib = NULL;

	return status;
}

/*
 *  ======== cod_open ========
 *      Open library for reading sections.
 */
int cod_open(struct cod_manager *hmgr, char *sz_coff_path,
		    u32 flags, struct cod_libraryobj **lib_obj)
{
	int status = 0;
	struct cod_libraryobj *lib = NULL;

	*lib_obj = NULL;

	lib = kzalloc(sizeof(struct cod_libraryobj), GFP_KERNEL);
	if (lib == NULL)
		status = -ENOMEM;

	if (!status) {
		lib->cod_mgr = hmgr;
		status = hmgr->fxns.open_fxn(hmgr->target, sz_coff_path, flags,
					     &lib->dbll_lib);
		if (!status)
			*lib_obj = lib;
	}

	if (status)
		pr_err("%s: error status 0x%x, sz_coff_path: %s flags: 0x%x\n",
		       __func__, status, sz_coff_path, flags);
	return status;
}

/*
 *  ======== cod_open_base ========
 *  Purpose:
 *      Open base image for reading sections.
 */
int cod_open_base(struct cod_manager *hmgr, char *sz_coff_path,
			 dbll_flags flags)
{
	int status = 0;
	struct dbll_library_obj *lib;

	/* if we previously opened a base image, close it now */
	if (hmgr->base_lib) {
		if (hmgr->loaded) {
			hmgr->fxns.unload_fxn(hmgr->base_lib, &hmgr->attrs);
			hmgr->loaded = false;
		}
		hmgr->fxns.close_fxn(hmgr->base_lib);
		hmgr->base_lib = NULL;
	}
	status = hmgr->fxns.open_fxn(hmgr->target, sz_coff_path, flags, &lib);
	if (!status) {
		/* hang onto the library for subsequent sym table usage */
		hmgr->base_lib = lib;
		strncpy(hmgr->sz_zl_file, sz_coff_path, COD_MAXPATHLENGTH - 1);
		hmgr->sz_zl_file[COD_MAXPATHLENGTH - 1] = '\0';
	}

	if (status)
		pr_err("%s: error status 0x%x sz_coff_path: %s\n", __func__,
		       status, sz_coff_path);
	return status;
}

/*
 *  ======== cod_read_section ========
 *  Purpose:
 *      Retrieve the content of a code section given the section name.
 */
int cod_read_section(struct cod_libraryobj *lib, char *str_sect,
			    char *str_content, u32 content_size)
{
	int status = 0;

	if (lib != NULL)
		status =
		    lib->cod_mgr->fxns.read_sect_fxn(lib->dbll_lib, str_sect,
						     str_content, content_size);
	else
		status = -ESPIPE;

	return status;
}

/*
 *  ======== no_op ========
 *  Purpose:
 *      No Operation.
 *
 */
static bool no_op(void)
{
	return true;
}
