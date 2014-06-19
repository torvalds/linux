/*
 * dbdcd.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file contains the implementation of the DSP/BIOS Bridge
 * Configuration Database (DCD).
 *
 * Notes:
 *   The fxn dcd_get_objects can apply a callback fxn to each DCD object
 *   that is located in a specified COFF file.  At the moment,
 *   dcd_auto_register, dcd_auto_unregister, and NLDR module all use
 *   dcd_get_objects.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>

/*  ----------------------------------- Others */
#include <dspbridge/uuidutil.h>

/*  ----------------------------------- This */
#include <dspbridge/dbdcd.h>

/*  ----------------------------------- Global defines. */
#define MAX_INT2CHAR_LENGTH     16	/* Max int2char len of 32 bit int */

/* Name of section containing dependent libraries */
#define DEPLIBSECT		".dspbridge_deplibs"

/* DCD specific structures. */
struct dcd_manager {
	struct cod_manager *cod_mgr;	/* Handle to COD manager object. */
};

/*  Pointer to the registry support key */
static struct list_head reg_key_list;
static DEFINE_SPINLOCK(dbdcd_lock);

/* Global reference variables. */
static u32 refs;
static u32 enum_refs;

/* Helper function prototypes. */
static s32 atoi(char *psz_buf);
static int get_attrs_from_buf(char *psz_buf, u32 ul_buf_size,
				     enum dsp_dcdobjtype obj_type,
				     struct dcd_genericobj *gen_obj);
static void compress_buf(char *psz_buf, u32 ul_buf_size, s32 char_size);
static char dsp_char2_gpp_char(char *word, s32 dsp_char_size);
static int get_dep_lib_info(struct dcd_manager *hdcd_mgr,
				   struct dsp_uuid *uuid_obj,
				   u16 *num_libs,
				   u16 *num_pers_libs,
				   struct dsp_uuid *dep_lib_uuids,
				   bool *prstnt_dep_libs,
				   enum nldr_phase phase);

/*
 *  ======== dcd_uuid_from_string ========
 *  Purpose:
 *      Converts an ANSI string to a dsp_uuid.
 *  Parameters:
 *      sz_uuid:    Pointer to a string that represents a dsp_uuid object.
 *      uuid_obj:      Pointer to a dsp_uuid object.
 *  Returns:
 *      0:        Success.
 *      -EINVAL:  Coversion failed
 *  Requires:
 *      uuid_obj & sz_uuid are non-NULL values.
 *  Ensures:
 *  Details:
 *      We assume the string representation of a UUID has the following format:
 *      "12345678_1234_1234_1234_123456789abc".
 */
static int dcd_uuid_from_string(char *sz_uuid, struct dsp_uuid *uuid_obj)
{
	char c;
	u64 t;
	struct dsp_uuid uuid_tmp;

	/*
	 * sscanf implementation cannot deal with hh format modifier
	 * if the converted value doesn't fit in u32. So, convert the
	 * last six bytes to u64 and memcpy what is needed
	 */
	if (sscanf(sz_uuid, "%8x%c%4hx%c%4hx%c%2hhx%2hhx%c%llx",
	       &uuid_tmp.data1, &c, &uuid_tmp.data2, &c,
	       &uuid_tmp.data3, &c, &uuid_tmp.data4,
	       &uuid_tmp.data5, &c, &t) != 10)
		return -EINVAL;

	t = cpu_to_be64(t);
	memcpy(&uuid_tmp.data6[0], ((char *)&t) + 2, 6);
	*uuid_obj = uuid_tmp;

	return 0;
}

/*
 *  ======== dcd_auto_register ========
 *  Purpose:
 *      Parses the supplied image and resigsters with DCD.
 */
int dcd_auto_register(struct dcd_manager *hdcd_mgr,
			     char *sz_coff_path)
{
	int status = 0;

	if (hdcd_mgr)
		status = dcd_get_objects(hdcd_mgr, sz_coff_path,
					 (dcd_registerfxn) dcd_register_object,
					 (void *)sz_coff_path);
	else
		status = -EFAULT;

	return status;
}

/*
 *  ======== dcd_auto_unregister ========
 *  Purpose:
 *      Parses the supplied DSP image and unresiters from DCD.
 */
int dcd_auto_unregister(struct dcd_manager *hdcd_mgr,
			       char *sz_coff_path)
{
	int status = 0;

	if (hdcd_mgr)
		status = dcd_get_objects(hdcd_mgr, sz_coff_path,
					 (dcd_registerfxn) dcd_register_object,
					 NULL);
	else
		status = -EFAULT;

	return status;
}

/*
 *  ======== dcd_create_manager ========
 *  Purpose:
 *      Creates DCD manager.
 */
int dcd_create_manager(char *sz_zl_dll_name,
			      struct dcd_manager **dcd_mgr)
{
	struct cod_manager *cod_mgr;	/* COD manager handle */
	struct dcd_manager *dcd_mgr_obj = NULL;	/* DCD Manager pointer */
	int status = 0;

	status = cod_create(&cod_mgr, sz_zl_dll_name);
	if (status)
		goto func_end;

	/* Create a DCD object. */
	dcd_mgr_obj = kzalloc(sizeof(struct dcd_manager), GFP_KERNEL);
	if (dcd_mgr_obj != NULL) {
		/* Fill out the object. */
		dcd_mgr_obj->cod_mgr = cod_mgr;

		/* Return handle to this DCD interface. */
		*dcd_mgr = dcd_mgr_obj;
	} else {
		status = -ENOMEM;

		/*
		 * If allocation of DcdManager object failed, delete the
		 * COD manager.
		 */
		cod_delete(cod_mgr);
	}

func_end:
	return status;
}

/*
 *  ======== dcd_destroy_manager ========
 *  Purpose:
 *      Frees DCD Manager object.
 */
int dcd_destroy_manager(struct dcd_manager *hdcd_mgr)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	int status = -EFAULT;

	if (hdcd_mgr) {
		/* Delete the COD manager. */
		cod_delete(dcd_mgr_obj->cod_mgr);

		/* Deallocate a DCD manager object. */
		kfree(dcd_mgr_obj);

		status = 0;
	}

	return status;
}

/*
 *  ======== dcd_enumerate_object ========
 *  Purpose:
 *      Enumerates objects in the DCD.
 */
int dcd_enumerate_object(s32 index, enum dsp_dcdobjtype obj_type,
				struct dsp_uuid *uuid_obj)
{
	int status = 0;
	char sz_reg_key[DCD_MAXPATHLENGTH];
	char sz_value[DCD_MAXPATHLENGTH];
	struct dsp_uuid dsp_uuid_obj;
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */
	u32 dw_key_len = 0;
	struct dcd_key_elem *dcd_key;
	int len;

	if ((index != 0) && (enum_refs == 0)) {
		/*
		 * If an enumeration is being performed on an index greater
		 * than zero, then the current enum_refs must have been
		 * incremented to greater than zero.
		 */
		status = -EIDRM;
	} else {
		/*
		 * Pre-determine final key length. It's length of DCD_REGKEY +
		 *  "_\0" + length of sz_obj_type string + terminating NULL.
		 */
		dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;

		/* Create proper REG key; concatenate DCD_REGKEY with
		 * obj_type. */
		strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
		if ((strlen(sz_reg_key) + strlen("_\0")) <
		    DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, "_\0", 2);
		} else {
			status = -EPERM;
		}

		/* This snprintf is guaranteed not to exceed max size of an
		 * integer. */
		status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d",
				  obj_type);

		if (status == -1) {
			status = -EPERM;
		} else {
			status = 0;
			if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
			    DCD_MAXPATHLENGTH) {
				strncat(sz_reg_key, sz_obj_type,
					strlen(sz_obj_type) + 1);
			} else {
				status = -EPERM;
			}
		}

		if (!status) {
			len = strlen(sz_reg_key);
			spin_lock(&dbdcd_lock);
			list_for_each_entry(dcd_key, &reg_key_list, link) {
				if (!strncmp(dcd_key->name, sz_reg_key, len)
						&& !index--) {
					strncpy(sz_value, &dcd_key->name[len],
					       strlen(&dcd_key->name[len]) + 1);
						break;
				}
			}
			spin_unlock(&dbdcd_lock);

			if (&dcd_key->link == &reg_key_list)
				status = -ENODATA;
		}

		if (!status) {
			/* Create UUID value using string retrieved from
			 * registry. */
			status = dcd_uuid_from_string(sz_value, &dsp_uuid_obj);

			if (!status) {
				*uuid_obj = dsp_uuid_obj;

				/* Increment enum_refs to update reference
				 * count. */
				enum_refs++;
			}
		} else if (status == -ENODATA) {
			/* At the end of enumeration. Reset enum_refs. */
			enum_refs = 0;

			/*
			 * TODO: Revisit, this is not an error case but code
			 * expects non-zero value.
			 */
			status = ENODATA;
		} else {
			status = -EPERM;
		}
	}

	return status;
}

/*
 *  ======== dcd_exit ========
 *  Purpose:
 *      Discontinue usage of the DCD module.
 */
void dcd_exit(void)
{
	struct dcd_key_elem *rv, *rv_tmp;

	refs--;
	if (refs == 0) {
		list_for_each_entry_safe(rv, rv_tmp, &reg_key_list, link) {
			list_del(&rv->link);
			kfree(rv->path);
			kfree(rv);
		}
	}

}

/*
 *  ======== dcd_get_dep_libs ========
 */
int dcd_get_dep_libs(struct dcd_manager *hdcd_mgr,
			    struct dsp_uuid *uuid_obj,
			    u16 num_libs, struct dsp_uuid *dep_lib_uuids,
			    bool *prstnt_dep_libs,
			    enum nldr_phase phase)
{
	int status = 0;

	status =
	    get_dep_lib_info(hdcd_mgr, uuid_obj, &num_libs, NULL, dep_lib_uuids,
			     prstnt_dep_libs, phase);

	return status;
}

/*
 *  ======== dcd_get_num_dep_libs ========
 */
int dcd_get_num_dep_libs(struct dcd_manager *hdcd_mgr,
				struct dsp_uuid *uuid_obj,
				u16 *num_libs, u16 *num_pers_libs,
				enum nldr_phase phase)
{
	int status = 0;

	status = get_dep_lib_info(hdcd_mgr, uuid_obj, num_libs, num_pers_libs,
				  NULL, NULL, phase);

	return status;
}

/*
 *  ======== dcd_get_object_def ========
 *  Purpose:
 *      Retrieves the properties of a node or processor based on the UUID and
 *      object type.
 */
int dcd_get_object_def(struct dcd_manager *hdcd_mgr,
			      struct dsp_uuid *obj_uuid,
			      enum dsp_dcdobjtype obj_type,
			      struct dcd_genericobj *obj_def)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;	/* ptr to DCD mgr */
	struct cod_libraryobj *lib = NULL;
	int status = 0;
	int len;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	u32 dw_buf_size;	/* Used by REG functions */
	char sz_reg_key[DCD_MAXPATHLENGTH];
	char *sz_uuid;		/*[MAXUUIDLEN]; */
	char *tmp;
	struct dcd_key_elem *dcd_key = NULL;
	char sz_sect_name[MAXUUIDLEN + 2];	/* ".[UUID]\0" */
	char *psz_coff_buf;
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */

	sz_uuid = kzalloc(MAXUUIDLEN, GFP_KERNEL);
	if (!sz_uuid) {
		status = -ENOMEM;
		goto func_end;
	}

	if (!hdcd_mgr) {
		status = -EFAULT;
		goto func_end;
	}

	/* Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);

	if ((strlen(sz_reg_key) + strlen("_\0")) < DCD_MAXPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else
		status = -EPERM;

	status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d", obj_type);
	if (status == -1) {
		status = -EPERM;
	} else {
		status = 0;

		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = -EPERM;
		}

		/* Create UUID value to set in registry. */
		snprintf(sz_uuid, MAXUUIDLEN, "%pUL", obj_uuid);

		if ((strlen(sz_reg_key) + MAXUUIDLEN) < DCD_MAXPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = -EPERM;

		/* Retrieve paths from the registry based on struct dsp_uuid */
		dw_buf_size = DCD_MAXPATHLENGTH;
	}
	if (!status) {
		spin_lock(&dbdcd_lock);
		list_for_each_entry(dcd_key, &reg_key_list, link) {
			if (!strncmp(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1))
				break;
		}
		spin_unlock(&dbdcd_lock);
		if (&dcd_key->link == &reg_key_list) {
			status = -ENOKEY;
			goto func_end;
		}
	}


	/* Open COFF file. */
	status = cod_open(dcd_mgr_obj->cod_mgr, dcd_key->path,
							COD_NOLOAD, &lib);
	if (status) {
		status = -EACCES;
		goto func_end;
	}

	/* Ensure sz_uuid + 1 is not greater than sizeof sz_sect_name. */
	len = strlen(sz_uuid);
	if (len + 1 > sizeof(sz_sect_name)) {
		status = -EPERM;
		goto func_end;
	}

	/* Create section name based on node UUID. A period is
	 * pre-pended to the UUID string to form the section name.
	 * I.e. ".24BC8D90_BB45_11d4_B756_006008BDB66F" */

	len -= 4;	/* uuid has 4 delimiters '-' */
	tmp = sz_uuid;

	strncpy(sz_sect_name, ".", 2);
	do {
		char *uuid = strsep(&tmp, "-");

		if (!uuid)
			break;
		len -= strlen(uuid);
		strncat(sz_sect_name, uuid, strlen(uuid) + 1);
	} while (len && strncat(sz_sect_name, "_", 2));

	/* Get section information. */
	status = cod_get_section(lib, sz_sect_name, &ul_addr, &ul_len);
	if (status) {
		status = -EACCES;
		goto func_end;
	}

	/* Allocate zeroed buffer. */
	psz_coff_buf = kzalloc(ul_len + 4, GFP_KERNEL);
	if (psz_coff_buf == NULL) {
		status = -ENOMEM;
		goto func_end;
	}
#ifdef _DB_TIOMAP
	if (strstr(dcd_key->path, "iva") == NULL) {
		/* Locate section by objectID and read its content. */
		status =
		    cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
	} else {
		status =
		    cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
		dev_dbg(bridge, "%s: Skipped Byte swap for IVA!!\n", __func__);
	}
#else
	status = cod_read_section(lib, sz_sect_name, psz_coff_buf, ul_len);
#endif
	if (!status) {
		/* Compress DSP buffer to conform to PC format. */
		if (strstr(dcd_key->path, "iva") == NULL) {
			compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);
		} else {
			compress_buf(psz_coff_buf, ul_len, 1);
			dev_dbg(bridge, "%s: Compressing IVA COFF buffer by 1 "
				"for IVA!!\n", __func__);
		}

		/* Parse the content of the COFF buffer. */
		status =
		    get_attrs_from_buf(psz_coff_buf, ul_len, obj_type, obj_def);
		if (status)
			status = -EACCES;
	} else {
		status = -EACCES;
	}

	/* Free the previously allocated dynamic buffer. */
	kfree(psz_coff_buf);
func_end:
	if (lib)
		cod_close(lib);

	kfree(sz_uuid);

	return status;
}

/*
 *  ======== dcd_get_objects ========
 */
int dcd_get_objects(struct dcd_manager *hdcd_mgr,
			   char *sz_coff_path, dcd_registerfxn register_fxn,
			   void *handle)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	int status = 0;
	char *psz_coff_buf;
	char *psz_cur;
	struct cod_libraryobj *lib = NULL;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	char seps[] = ":, ";
	char *token = NULL;
	struct dsp_uuid dsp_uuid_obj;
	s32 object_type;

	if (!hdcd_mgr) {
		status = -EFAULT;
		goto func_end;
	}

	/* Open DSP coff file, don't load symbols. */
	status = cod_open(dcd_mgr_obj->cod_mgr, sz_coff_path, COD_NOLOAD, &lib);
	if (status) {
		status = -EACCES;
		goto func_cont;
	}

	/* Get DCD_RESIGER_SECTION section information. */
	status = cod_get_section(lib, DCD_REGISTER_SECTION, &ul_addr, &ul_len);
	if (status || !(ul_len > 0)) {
		status = -EACCES;
		goto func_cont;
	}

	/* Allocate zeroed buffer. */
	psz_coff_buf = kzalloc(ul_len + 4, GFP_KERNEL);
	if (psz_coff_buf == NULL) {
		status = -ENOMEM;
		goto func_cont;
	}
#ifdef _DB_TIOMAP
	if (strstr(sz_coff_path, "iva") == NULL) {
		/* Locate section by objectID and read its content. */
		status = cod_read_section(lib, DCD_REGISTER_SECTION,
					  psz_coff_buf, ul_len);
	} else {
		dev_dbg(bridge, "%s: Skipped Byte swap for IVA!!\n", __func__);
		status = cod_read_section(lib, DCD_REGISTER_SECTION,
					  psz_coff_buf, ul_len);
	}
#else
	status =
	    cod_read_section(lib, DCD_REGISTER_SECTION, psz_coff_buf, ul_len);
#endif
	if (!status) {
		/* Compress DSP buffer to conform to PC format. */
		if (strstr(sz_coff_path, "iva") == NULL) {
			compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);
		} else {
			compress_buf(psz_coff_buf, ul_len, 1);
			dev_dbg(bridge, "%s: Compress COFF buffer with 1 word "
				"for IVA!!\n", __func__);
		}

		/* Read from buffer and register object in buffer. */
		psz_cur = psz_coff_buf;
		while ((token = strsep(&psz_cur, seps)) && *token != '\0') {
			/*  Retrieve UUID string. */
			status = dcd_uuid_from_string(token, &dsp_uuid_obj);

			if (!status) {
				/*  Retrieve object type */
				token = strsep(&psz_cur, seps);

				/*  Retrieve object type */
				object_type = atoi(token);

				/*
				*  Apply register_fxn to the found DCD object.
				*  Possible actions include:
				*
				*  1) Register found DCD object.
				*  2) Unregister found DCD object
				*     (when handle == NULL)
				*  3) Add overlay node.
				*/
				status =
				    register_fxn(&dsp_uuid_obj, object_type,
						 handle);
			}
			if (status) {
				/* if error occurs, break from while loop. */
				break;
			}
		}
	} else {
		status = -EACCES;
	}

	/* Free the previously allocated dynamic buffer. */
	kfree(psz_coff_buf);
func_cont:
	if (lib)
		cod_close(lib);

func_end:
	return status;
}

/*
 *  ======== dcd_get_library_name ========
 *  Purpose:
 *      Retrieves the library name for the given UUID.
 *
 */
int dcd_get_library_name(struct dcd_manager *hdcd_mgr,
				struct dsp_uuid *uuid_obj,
				char *str_lib_name,
				u32 *buff_size,
				enum nldr_phase phase, bool *phase_split)
{
	char sz_reg_key[DCD_MAXPATHLENGTH];
	char sz_uuid[MAXUUIDLEN];
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */
	int status = 0;
	struct dcd_key_elem *dcd_key = NULL;

	dev_dbg(bridge, "%s: hdcd_mgr %p, uuid_obj %p, str_lib_name %p,"
		" buff_size %p\n", __func__, hdcd_mgr, uuid_obj, str_lib_name,
		buff_size);

	/*
	 *  Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL.
	 */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
	if ((strlen(sz_reg_key) + strlen("_\0")) < DCD_MAXPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else
		status = -EPERM;

	switch (phase) {
	case NLDR_CREATE:
		/* create phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDCREATELIBTYPE);
		break;
	case NLDR_EXECUTE:
		/* execute phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDEXECUTELIBTYPE);
		break;
	case NLDR_DELETE:
		/* delete phase type */
		sprintf(sz_obj_type, "%d", DSP_DCDDELETELIBTYPE);
		break;
	case NLDR_NOPHASE:
		/* known to be a dependent library */
		sprintf(sz_obj_type, "%d", DSP_DCDLIBRARYTYPE);
		break;
	default:
		status = -EINVAL;
	}
	if (!status) {
		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = -EPERM;
		}
		/* Create UUID value to find match in registry. */
		snprintf(sz_uuid, MAXUUIDLEN, "%pUL", uuid_obj);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < DCD_MAXPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = -EPERM;
	}
	if (!status) {
		spin_lock(&dbdcd_lock);
		list_for_each_entry(dcd_key, &reg_key_list, link) {
			/*  See if the name matches. */
			if (!strncmp(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1))
				break;
		}
		spin_unlock(&dbdcd_lock);
	}

	if (&dcd_key->link == &reg_key_list)
		status = -ENOKEY;

	/* If can't find, phases might be registered as generic LIBRARYTYPE */
	if (status && phase != NLDR_NOPHASE) {
		if (phase_split)
			*phase_split = false;

		strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
		if ((strlen(sz_reg_key) + strlen("_\0")) <
		    DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, "_\0", 2);
		} else {
			status = -EPERM;
		}
		sprintf(sz_obj_type, "%d", DSP_DCDLIBRARYTYPE);
		if ((strlen(sz_reg_key) + strlen(sz_obj_type))
		    < DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else {
			status = -EPERM;
		}
		snprintf(sz_uuid, MAXUUIDLEN, "%pUL", uuid_obj);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < DCD_MAXPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = -EPERM;

		spin_lock(&dbdcd_lock);
		list_for_each_entry(dcd_key, &reg_key_list, link) {
			/*  See if the name matches. */
			if (!strncmp(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1))
				break;
		}
		spin_unlock(&dbdcd_lock);

		status = (&dcd_key->link != &reg_key_list) ?
						0 : -ENOKEY;
	}

	if (!status)
		memcpy(str_lib_name, dcd_key->path, strlen(dcd_key->path) + 1);
	return status;
}

/*
 *  ======== dcd_init ========
 *  Purpose:
 *      Initialize the DCD module.
 */
bool dcd_init(void)
{
	bool ret = true;

	if (refs == 0)
		INIT_LIST_HEAD(&reg_key_list);

	if (ret)
		refs++;

	return ret;
}

/*
 *  ======== dcd_register_object ========
 *  Purpose:
 *      Registers a node or a processor with the DCD.
 *      If psz_path_name == NULL, unregister the specified DCD object.
 */
int dcd_register_object(struct dsp_uuid *uuid_obj,
			       enum dsp_dcdobjtype obj_type,
			       char *psz_path_name)
{
	int status = 0;
	char sz_reg_key[DCD_MAXPATHLENGTH];
	char sz_uuid[MAXUUIDLEN + 1];
	u32 dw_path_size = 0;
	u32 dw_key_len;		/* Len of REG key. */
	char sz_obj_type[MAX_INT2CHAR_LENGTH];	/* str. rep. of obj_type. */
	struct dcd_key_elem *dcd_key = NULL;

	dev_dbg(bridge, "%s: object UUID %p, obj_type %d, szPathName %s\n",
		__func__, uuid_obj, obj_type, psz_path_name);

	/*
	 * Pre-determine final key length. It's length of DCD_REGKEY +
	 *  "_\0" + length of sz_obj_type string + terminating NULL.
	 */
	dw_key_len = strlen(DCD_REGKEY) + 1 + sizeof(sz_obj_type) + 1;

	/* Create proper REG key; concatenate DCD_REGKEY with obj_type. */
	strncpy(sz_reg_key, DCD_REGKEY, strlen(DCD_REGKEY) + 1);
	if ((strlen(sz_reg_key) + strlen("_\0")) < DCD_MAXPATHLENGTH)
		strncat(sz_reg_key, "_\0", 2);
	else {
		status = -EPERM;
		goto func_end;
	}

	status = snprintf(sz_obj_type, MAX_INT2CHAR_LENGTH, "%d", obj_type);
	if (status == -1) {
		status = -EPERM;
	} else {
		status = 0;
		if ((strlen(sz_reg_key) + strlen(sz_obj_type)) <
		    DCD_MAXPATHLENGTH) {
			strncat(sz_reg_key, sz_obj_type,
				strlen(sz_obj_type) + 1);
		} else
			status = -EPERM;

		/* Create UUID value to set in registry. */
		snprintf(sz_uuid, MAXUUIDLEN, "%pUL", uuid_obj);
		if ((strlen(sz_reg_key) + MAXUUIDLEN) < DCD_MAXPATHLENGTH)
			strncat(sz_reg_key, sz_uuid, MAXUUIDLEN);
		else
			status = -EPERM;
	}

	if (status)
		goto func_end;

	/*
	 * If psz_path_name != NULL, perform registration, otherwise,
	 * perform unregistration.
	 */

	if (psz_path_name) {
		dw_path_size = strlen(psz_path_name) + 1;
		spin_lock(&dbdcd_lock);
		list_for_each_entry(dcd_key, &reg_key_list, link) {
			/*  See if the name matches. */
			if (!strncmp(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1))
				break;
		}
		spin_unlock(&dbdcd_lock);
		if (&dcd_key->link == &reg_key_list) {
			/*
			 * Add new reg value (UUID+obj_type)
			 * with COFF path info
			 */

			dcd_key = kmalloc(sizeof(struct dcd_key_elem),
								GFP_KERNEL);
			if (!dcd_key) {
				status = -ENOMEM;
				goto func_end;
			}

			dcd_key->path = kmalloc(dw_path_size, GFP_KERNEL);

			if (!dcd_key->path) {
				kfree(dcd_key);
				status = -ENOMEM;
				goto func_end;
			}

			strncpy(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1);
			strncpy(dcd_key->path, psz_path_name ,
						dw_path_size);
			spin_lock(&dbdcd_lock);
			list_add_tail(&dcd_key->link, &reg_key_list);
			spin_unlock(&dbdcd_lock);
		} else {
			/*  Make sure the new data is the same. */
			if (strncmp(dcd_key->path, psz_path_name,
							dw_path_size)) {
				/*  The caller needs a different data size! */
				kfree(dcd_key->path);
				dcd_key->path = kmalloc(dw_path_size,
								GFP_KERNEL);
				if (dcd_key->path == NULL) {
					status = -ENOMEM;
					goto func_end;
				}
			}

			/*  We have a match!  Copy out the data. */
			memcpy(dcd_key->path, psz_path_name, dw_path_size);
		}
		dev_dbg(bridge, "%s: psz_path_name=%s, dw_path_size=%d\n",
			__func__, psz_path_name, dw_path_size);
	} else {
		/* Deregister an existing object */
		spin_lock(&dbdcd_lock);
		list_for_each_entry(dcd_key, &reg_key_list, link) {
			if (!strncmp(dcd_key->name, sz_reg_key,
						strlen(sz_reg_key) + 1)) {
				list_del(&dcd_key->link);
				kfree(dcd_key->path);
				kfree(dcd_key);
				break;
			}
		}
		spin_unlock(&dbdcd_lock);
		if (&dcd_key->link == &reg_key_list)
			status = -EPERM;
	}

	if (!status) {
		/*
		 *  Because the node database has been updated through a
		 *  successful object registration/de-registration operation,
		 *  we need to reset the object enumeration counter to allow
		 *  current enumerations to reflect this update in the node
		 *  database.
		 */
		enum_refs = 0;
	}
func_end:
	return status;
}

/*
 *  ======== dcd_unregister_object ========
 *  Call DCD_Register object with psz_path_name set to NULL to
 *  perform actual object de-registration.
 */
int dcd_unregister_object(struct dsp_uuid *uuid_obj,
				 enum dsp_dcdobjtype obj_type)
{
	int status = 0;

	/*
	 *  When dcd_register_object is called with NULL as pathname,
	 *  it indicates an unregister object operation.
	 */
	status = dcd_register_object(uuid_obj, obj_type, NULL);

	return status;
}

/*
 **********************************************************************
 * DCD Helper Functions
 **********************************************************************
 */

/*
 *  ======== atoi ========
 *  Purpose:
 *      This function converts strings in decimal or hex format to integers.
 */
static s32 atoi(char *psz_buf)
{
	char *pch = psz_buf;
	s32 base = 0;

	while (isspace(*pch))
		pch++;

	if (*pch == '-' || *pch == '+') {
		base = 10;
		pch++;
	} else if (*pch && tolower(pch[strlen(pch) - 1]) == 'h') {
		base = 16;
	}

	return simple_strtoul(pch, NULL, base);
}

/*
 *  ======== get_attrs_from_buf ========
 *  Purpose:
 *      Parse the content of a buffer filled with DSP-side data and
 *      retrieve an object's attributes from it. IMPORTANT: Assume the
 *      buffer has been converted from DSP format to GPP format.
 */
static int get_attrs_from_buf(char *psz_buf, u32 ul_buf_size,
				     enum dsp_dcdobjtype obj_type,
				     struct dcd_genericobj *gen_obj)
{
	int status = 0;
	char seps[] = ", ";
	char *psz_cur;
	char *token;
	s32 token_len = 0;
	u32 i = 0;
#ifdef _DB_TIOMAP
	s32 entry_id;
#endif

	switch (obj_type) {
	case DSP_DCDNODETYPE:
		/*
		 * Parse COFF sect buffer to retrieve individual tokens used
		 * to fill in object attrs.
		 */
		psz_cur = psz_buf;
		token = strsep(&psz_cur, seps);

		/* u32 cb_struct */
		gen_obj->obj_data.node_obj.ndb_props.cb_struct =
		    (u32) atoi(token);
		token = strsep(&psz_cur, seps);

		/* dsp_uuid ui_node_id */
		status = dcd_uuid_from_string(token,
					      &gen_obj->obj_data.node_obj.
					      ndb_props.ui_node_id);
		if (status)
			break;

		token = strsep(&psz_cur, seps);

		/* ac_name */
		token_len = strlen(token);
		if (token_len > DSP_MAXNAMELEN - 1)
			token_len = DSP_MAXNAMELEN - 1;

		strncpy(gen_obj->obj_data.node_obj.ndb_props.ac_name,
			token, token_len);
		gen_obj->obj_data.node_obj.ndb_props.ac_name[token_len] = '\0';
		token = strsep(&psz_cur, seps);
		/* u32 ntype */
		gen_obj->obj_data.node_obj.ndb_props.ntype = atoi(token);
		token = strsep(&psz_cur, seps);
		/* u32 cache_on_gpp */
		gen_obj->obj_data.node_obj.ndb_props.cache_on_gpp = atoi(token);
		token = strsep(&psz_cur, seps);
		/* dsp_resourcereqmts dsp_resource_reqmts */
		gen_obj->obj_data.node_obj.ndb_props.dsp_resource_reqmts.
		    cb_struct = (u32) atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.static_data_size = atoi(token);
		token = strsep(&psz_cur, seps);
		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.global_data_size = atoi(token);
		token = strsep(&psz_cur, seps);
		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.program_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);
		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.wc_execution_time = atoi(token);
		token = strsep(&psz_cur, seps);
		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.wc_period = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.wc_deadline = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.avg_exection_time = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.node_obj.ndb_props.
		    dsp_resource_reqmts.minimum_period = atoi(token);
		token = strsep(&psz_cur, seps);

		/* s32 prio */
		gen_obj->obj_data.node_obj.ndb_props.prio = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 stack_size */
		gen_obj->obj_data.node_obj.ndb_props.stack_size = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 sys_stack_size */
		gen_obj->obj_data.node_obj.ndb_props.sys_stack_size =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 stack_seg */
		gen_obj->obj_data.node_obj.ndb_props.stack_seg = atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 message_depth */
		gen_obj->obj_data.node_obj.ndb_props.message_depth =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 num_input_streams */
		gen_obj->obj_data.node_obj.ndb_props.num_input_streams =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 num_output_streams */
		gen_obj->obj_data.node_obj.ndb_props.num_output_streams =
		    atoi(token);
		token = strsep(&psz_cur, seps);

		/* u32 timeout */
		gen_obj->obj_data.node_obj.ndb_props.timeout = atoi(token);
		token = strsep(&psz_cur, seps);

		/* char *str_create_phase_fxn */
		token_len = strlen(token);
		gen_obj->obj_data.node_obj.str_create_phase_fxn =
					kzalloc(token_len + 1, GFP_KERNEL);
		strncpy(gen_obj->obj_data.node_obj.str_create_phase_fxn,
			token, token_len);
		gen_obj->obj_data.node_obj.str_create_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* char *str_execute_phase_fxn */
		token_len = strlen(token);
		gen_obj->obj_data.node_obj.str_execute_phase_fxn =
					kzalloc(token_len + 1, GFP_KERNEL);
		strncpy(gen_obj->obj_data.node_obj.str_execute_phase_fxn,
			token, token_len);
		gen_obj->obj_data.node_obj.str_execute_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* char *str_delete_phase_fxn */
		token_len = strlen(token);
		gen_obj->obj_data.node_obj.str_delete_phase_fxn =
					kzalloc(token_len + 1, GFP_KERNEL);
		strncpy(gen_obj->obj_data.node_obj.str_delete_phase_fxn,
			token, token_len);
		gen_obj->obj_data.node_obj.str_delete_phase_fxn[token_len] =
		    '\0';
		token = strsep(&psz_cur, seps);

		/* Segment id for message buffers */
		gen_obj->obj_data.node_obj.msg_segid = atoi(token);
		token = strsep(&psz_cur, seps);

		/* Message notification type */
		gen_obj->obj_data.node_obj.msg_notify_type = atoi(token);
		token = strsep(&psz_cur, seps);

		/* char *str_i_alg_name */
		if (token) {
			token_len = strlen(token);
			gen_obj->obj_data.node_obj.str_i_alg_name =
					kzalloc(token_len + 1, GFP_KERNEL);
			strncpy(gen_obj->obj_data.node_obj.str_i_alg_name,
				token, token_len);
			gen_obj->obj_data.node_obj.str_i_alg_name[token_len] =
			    '\0';
			token = strsep(&psz_cur, seps);
		}

		/* Load type (static, dynamic, or overlay) */
		if (token) {
			gen_obj->obj_data.node_obj.load_type = atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Dynamic load data requirements */
		if (token) {
			gen_obj->obj_data.node_obj.data_mem_seg_mask =
			    atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Dynamic load code requirements */
		if (token) {
			gen_obj->obj_data.node_obj.code_mem_seg_mask =
			    atoi(token);
			token = strsep(&psz_cur, seps);
		}

		/* Extract node profiles into node properties */
		if (token) {

			gen_obj->obj_data.node_obj.ndb_props.count_profiles =
			    atoi(token);
			for (i = 0;
			     i <
			     gen_obj->obj_data.node_obj.
			     ndb_props.count_profiles; i++) {
				token = strsep(&psz_cur, seps);
				if (token) {
					/* Heap Size for the node */
					gen_obj->obj_data.node_obj.
					    ndb_props.node_profiles[i].
					    heap_size = atoi(token);
				}
			}
		}
		token = strsep(&psz_cur, seps);
		if (token) {
			gen_obj->obj_data.node_obj.ndb_props.stack_seg_name =
			    (u32) (token);
		}

		break;

	case DSP_DCDPROCESSORTYPE:
		/*
		 * Parse COFF sect buffer to retrieve individual tokens used
		 * to fill in object attrs.
		 */
		psz_cur = psz_buf;
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.cb_struct = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.processor_family = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.processor_type = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.clock_rate = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.internal_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.external_mem_size = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.processor_id = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.ty_running_rtos = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.node_min_priority = atoi(token);
		token = strsep(&psz_cur, seps);

		gen_obj->obj_data.proc_info.node_max_priority = atoi(token);

#ifdef _DB_TIOMAP
		/* Proc object may contain additional(extended) attributes. */
		/* attr must match proc.hxx */
		for (entry_id = 0; entry_id < 7; entry_id++) {
			token = strsep(&psz_cur, seps);
			gen_obj->obj_data.ext_proc_obj.ty_tlb[entry_id].
			    gpp_phys = atoi(token);

			token = strsep(&psz_cur, seps);
			gen_obj->obj_data.ext_proc_obj.ty_tlb[entry_id].
			    dsp_virt = atoi(token);
		}
#endif

		break;

	default:
		status = -EPERM;
		break;
	}

	return status;
}

/*
 *  ======== CompressBuffer ========
 *  Purpose:
 *      Compress the DSP buffer, if necessary, to conform to PC format.
 */
static void compress_buf(char *psz_buf, u32 ul_buf_size, s32 char_size)
{
	char *p;
	char ch;
	char *q;

	p = psz_buf;
	if (p == NULL)
		return;

	for (q = psz_buf; q < (psz_buf + ul_buf_size);) {
		ch = dsp_char2_gpp_char(q, char_size);
		if (ch == '\\') {
			q += char_size;
			ch = dsp_char2_gpp_char(q, char_size);
			switch (ch) {
			case 't':
				*p = '\t';
				break;

			case 'n':
				*p = '\n';
				break;

			case 'r':
				*p = '\r';
				break;

			case '0':
				*p = '\0';
				break;

			default:
				*p = ch;
				break;
			}
		} else {
			*p = ch;
		}
		p++;
		q += char_size;
	}

	/* NULL out remainder of buffer. */
	while (p < q)
		*p++ = '\0';
}

/*
 *  ======== dsp_char2_gpp_char ========
 *  Purpose:
 *      Convert DSP char to host GPP char in a portable manner
 */
static char dsp_char2_gpp_char(char *word, s32 dsp_char_size)
{
	char ch = '\0';
	char *ch_src;
	s32 i;

	for (ch_src = word, i = dsp_char_size; i > 0; i--)
		ch |= *ch_src++;

	return ch;
}

/*
 *  ======== get_dep_lib_info ========
 */
static int get_dep_lib_info(struct dcd_manager *hdcd_mgr,
				   struct dsp_uuid *uuid_obj,
				   u16 *num_libs,
				   u16 *num_pers_libs,
				   struct dsp_uuid *dep_lib_uuids,
				   bool *prstnt_dep_libs,
				   enum nldr_phase phase)
{
	struct dcd_manager *dcd_mgr_obj = hdcd_mgr;
	char *psz_coff_buf = NULL;
	char *psz_cur;
	char *psz_file_name = NULL;
	struct cod_libraryobj *lib = NULL;
	u32 ul_addr = 0;	/* Used by cod_get_section */
	u32 ul_len = 0;		/* Used by cod_get_section */
	u32 dw_data_size = COD_MAXPATHLENGTH;
	char seps[] = ", ";
	char *token = NULL;
	bool get_uuids = (dep_lib_uuids != NULL);
	u16 dep_libs = 0;
	int status = 0;

	/*  Initialize to 0 dependent libraries, if only counting number of
	 *  dependent libraries */
	if (!get_uuids) {
		*num_libs = 0;
		*num_pers_libs = 0;
	}

	/* Allocate a buffer for file name */
	psz_file_name = kzalloc(dw_data_size, GFP_KERNEL);
	if (psz_file_name == NULL) {
		status = -ENOMEM;
	} else {
		/* Get the name of the library */
		status = dcd_get_library_name(hdcd_mgr, uuid_obj, psz_file_name,
					      &dw_data_size, phase, NULL);
	}

	/* Open the library */
	if (!status) {
		status = cod_open(dcd_mgr_obj->cod_mgr, psz_file_name,
				  COD_NOLOAD, &lib);
	}
	if (!status) {
		/* Get dependent library section information. */
		status = cod_get_section(lib, DEPLIBSECT, &ul_addr, &ul_len);

		if (status) {
			/* Ok, no dependent libraries */
			ul_len = 0;
			status = 0;
		}
	}

	if (status || !(ul_len > 0))
		goto func_cont;

	/* Allocate zeroed buffer. */
	psz_coff_buf = kzalloc(ul_len + 4, GFP_KERNEL);
	if (psz_coff_buf == NULL)
		status = -ENOMEM;

	/* Read section contents. */
	status = cod_read_section(lib, DEPLIBSECT, psz_coff_buf, ul_len);
	if (status)
		goto func_cont;

	/* Compress and format DSP buffer to conform to PC format. */
	compress_buf(psz_coff_buf, ul_len, DSPWORDSIZE);

	/* Read from buffer */
	psz_cur = psz_coff_buf;
	while ((token = strsep(&psz_cur, seps)) && *token != '\0') {
		if (get_uuids) {
			if (dep_libs >= *num_libs) {
				/* Gone beyond the limit */
				break;
			} else {
				/* Retrieve UUID string. */
				status = dcd_uuid_from_string(token,
							      &(dep_lib_uuids
								[dep_libs]));
				if (status)
					break;

				/* Is this library persistent? */
				token = strsep(&psz_cur, seps);
				prstnt_dep_libs[dep_libs] = atoi(token);
				dep_libs++;
			}
		} else {
			/* Advanc to next token */
			token = strsep(&psz_cur, seps);
			if (atoi(token))
				(*num_pers_libs)++;

			/* Just counting number of dependent libraries */
			(*num_libs)++;
		}
	}
func_cont:
	if (lib)
		cod_close(lib);

	/* Free previously allocated dynamic buffers. */
	kfree(psz_file_name);

	kfree(psz_coff_buf);

	return status;
}
