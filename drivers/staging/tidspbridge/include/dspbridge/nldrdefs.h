/*
 * nldrdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Global Dynamic + static/overlay Node loader (NLDR) constants and types.
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

#ifndef NLDRDEFS_
#define NLDRDEFS_

#include <dspbridge/dbdcddef.h>
#include <dspbridge/devdefs.h>

#define NLDR_MAXPATHLENGTH       255
/* NLDR Objects: */
struct nldr_object;
struct nldr_nodeobject;

/*
 *  ======== nldr_loadtype ========
 *  Load types for a node. Must match values in node.h55.
 */
enum nldr_loadtype {
	NLDR_STATICLOAD,	/* Linked in base image, not overlay */
	NLDR_DYNAMICLOAD,	/* Dynamically loaded node */
	NLDR_OVLYLOAD		/* Linked in base image, overlay node */
};

/*
 *  ======== nldr_ovlyfxn ========
 *  Causes code or data to be copied from load address to run address. This
 *  is the "cod_writefxn" that gets passed to the DBLL_Library and is used as
 *  the ZL write function.
 *
 *  Parameters:
 *      priv_ref:       Handle to identify the node.
 *      dsp_run_addr:   Run address of code or data.
 *      dsp_load_addr:  Load address of code or data.
 *      ul_num_bytes:     Number of (GPP) bytes to copy.
 *      mem_space:      RMS_CODE or RMS_DATA.
 *  Returns:
 *      ul_num_bytes:     Success.
 *      0:              Failure.
 *  Requires:
 *  Ensures:
 */
typedef u32(*nldr_ovlyfxn) (void *priv_ref, u32 dsp_run_addr,
			    u32 dsp_load_addr, u32 ul_num_bytes, u32 mem_space);

/*
 *  ======== nldr_writefxn ========
 *  Write memory function. Used for dynamic load writes.
 *  Parameters:
 *      priv_ref:       Handle to identify the node.
 *      dsp_add:        Address of code or data.
 *      pbuf:           Code or data to be written
 *      ul_num_bytes:     Number of (GPP) bytes to write.
 *      mem_space:      DBLL_DATA or DBLL_CODE.
 *  Returns:
 *      ul_num_bytes:     Success.
 *      0:              Failure.
 *  Requires:
 *  Ensures:
 */
typedef u32(*nldr_writefxn) (void *priv_ref,
			     u32 dsp_add, void *pbuf,
			     u32 ul_num_bytes, u32 mem_space);

/*
 *  ======== nldr_attrs ========
 *  Attributes passed to nldr_create function.
 */
struct nldr_attrs {
	nldr_ovlyfxn ovly;
	nldr_writefxn write;
	u16 dsp_word_size;
	u16 dsp_mau_size;
};

/*
 *  ======== nldr_phase ========
 *  Indicates node create, delete, or execute phase function.
 */
enum nldr_phase {
	NLDR_CREATE,
	NLDR_DELETE,
	NLDR_EXECUTE,
	NLDR_NOPHASE
};

/*
 *  Typedefs of loader functions imported from a DLL, or defined in a
 *  function table.
 */

/*
 *  ======== nldr_allocate ========
 *  Allocate resources to manage the loading of a node on the DSP.
 *
 *  Parameters:
 *      nldr_obj:          Handle of loader that will load the node.
 *      priv_ref:       Handle to identify the node.
 *      node_props:     Pointer to a dcd_nodeprops for the node.
 *      nldr_nodeobj:   Location to store node handle on output. This handle
 *                      will be passed to nldr_load/nldr_unload.
 *      pf_phase_split:   pointer to int variable referenced in node.c
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Insufficient memory on GPP.
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_obj.
 *      node_props != NULL.
 *      nldr_nodeobj != NULL.
 *  Ensures:
 *      0:        IsValidNode(*nldr_nodeobj).
 *      error:          *nldr_nodeobj == NULL.
 */
typedef int(*nldr_allocatefxn) (struct nldr_object *nldr_obj,
				       void *priv_ref,
				       const struct dcd_nodeprops
				       * node_props,
				       struct nldr_nodeobject
				       **nldr_nodeobj,
				       bool *pf_phase_split);

/*
 *  ======== nldr_create ========
 *  Create a loader object. This object handles the loading and unloading of
 *  create, delete, and execute phase functions of nodes on the DSP target.
 *
 *  Parameters:
 *      nldr:           Location to store loader handle on output.
 *      hdev_obj:     Device for this processor.
 *      pattrs:         Loader attributes.
 *  Returns:
 *      0:        Success;
 *      -ENOMEM:    Insufficient memory for requested resources.
 *  Requires:
 *      nldr_init(void) called.
 *      nldr != NULL.
 *      hdev_obj != NULL.
 *	pattrs != NULL.
 *  Ensures:
 *      0:        Valid *nldr.
 *      error:          *nldr == NULL.
 */
typedef int(*nldr_createfxn) (struct nldr_object **nldr,
				     struct dev_object *hdev_obj,
				     const struct nldr_attrs *pattrs);

/*
 *  ======== nldr_delete ========
 *  Delete the NLDR loader.
 *
 *  Parameters:
 *      nldr_obj:          Node manager object.
 *  Returns:
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_obj.
 *  Ensures:
 *	nldr_obj invalid
 */
typedef void (*nldr_deletefxn) (struct nldr_object *nldr_obj);

/*
 *  ======== nldr_exit ========
 *  Discontinue usage of NLDR module.
 *
 *  Parameters:
 *  Returns:
 *  Requires:
 *      nldr_init(void) successfully called before.
 *  Ensures:
 *      Any resources acquired in nldr_init(void) will be freed when last NLDR
 *      client calls nldr_exit(void).
 */
typedef void (*nldr_exitfxn) (void);

/*
 *  ======== NLDR_Free ========
 *  Free resources allocated in nldr_allocate.
 *
 *  Parameters:
 *      nldr_node_obj:      Handle returned from nldr_allocate().
 *  Returns:
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_node_obj.
 *  Ensures:
 */
typedef void (*nldr_freefxn) (struct nldr_nodeobject *nldr_node_obj);

/*
 *  ======== nldr_get_fxn_addr ========
 *  Get address of create, delete, or execute phase function of a node on
 *  the DSP.
 *
 *  Parameters:
 *      nldr_node_obj:      Handle returned from nldr_allocate().
 *      str_fxn:        Name of function.
 *      addr:           Location to store function address.
 *  Returns:
 *      0:        Success.
 *      -ESPIPE:    Address of function not found.
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_node_obj.
 *      addr != NULL;
 *      str_fxn != NULL;
 *  Ensures:
 */
typedef int(*nldr_getfxnaddrfxn) (struct nldr_nodeobject
					 * nldr_node_obj,
					 char *str_fxn, u32 * addr);

/*
 *  ======== nldr_init ========
 *  Initialize the NLDR module.
 *
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
typedef bool(*nldr_initfxn) (void);

/*
 *  ======== nldr_load ========
 *  Load create, delete, or execute phase function of a node on the DSP.
 *
 *  Parameters:
 *      nldr_node_obj:      Handle returned from nldr_allocate().
 *      phase:          Type of function to load (create, delete, or execute).
 *  Returns:
 *      0:                Success.
 *      -ENOMEM:            Insufficient memory on GPP.
 *      -ENXIO:     Can't overlay phase because overlay memory
 *                              is already in use.
 *      -EILSEQ:           Failure in dynamic loader library.
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_node_obj.
 *  Ensures:
 */
typedef int(*nldr_loadfxn) (struct nldr_nodeobject *nldr_node_obj,
				   enum nldr_phase phase);

/*
 *  ======== nldr_unload ========
 *  Unload create, delete, or execute phase function of a node on the DSP.
 *
 *  Parameters:
 *      nldr_node_obj:      Handle returned from nldr_allocate().
 *      phase:          Node function to unload (create, delete, or execute).
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Insufficient memory on GPP.
 *  Requires:
 *      nldr_init(void) called.
 *      Valid nldr_node_obj.
 *  Ensures:
 */
typedef int(*nldr_unloadfxn) (struct nldr_nodeobject *nldr_node_obj,
				     enum nldr_phase phase);

/*
 *  ======== node_ldr_fxns ========
 */
struct node_ldr_fxns {
	nldr_allocatefxn allocate;
	nldr_createfxn create;
	nldr_deletefxn delete;
	nldr_exitfxn exit;
	nldr_getfxnaddrfxn get_fxn_addr;
	nldr_initfxn init;
	nldr_loadfxn load;
	nldr_unloadfxn unload;
};

#endif /* NLDRDEFS_ */
