/*
 * nldr.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge dynamic loader interface.
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

#include <dspbridge/dbdefs.h>
#include <dspbridge/dbdcddef.h>
#include <dspbridge/dev.h>
#include <dspbridge/rmm.h>
#include <dspbridge/nldrdefs.h>

#ifndef NLDR_
#define NLDR_

extern int nldr_allocate(struct nldr_object *nldr_obj,
				void *priv_ref, IN CONST struct dcd_nodeprops
				*node_props,
				OUT struct nldr_nodeobject **phNldrNode,
				IN bool *pf_phase_split);

extern int nldr_create(OUT struct nldr_object **phNldr,
			      struct dev_object *hdev_obj,
			      IN CONST struct nldr_attrs *pattrs);

extern void nldr_delete(struct nldr_object *nldr_obj);
extern void nldr_exit(void);

extern int nldr_get_fxn_addr(struct nldr_nodeobject *nldr_node_obj,
				    char *pstrFxn, u32 * pulAddr);

extern int nldr_get_rmm_manager(struct nldr_object *hNldrObject,
				       OUT struct rmm_target_obj **phRmmMgr);

extern bool nldr_init(void);
extern int nldr_load(struct nldr_nodeobject *nldr_node_obj,
			    enum nldr_phase phase);
extern int nldr_unload(struct nldr_nodeobject *nldr_node_obj,
			      enum nldr_phase phase);
int nldr_find_addr(struct nldr_nodeobject *nldr_node, u32 sym_addr,
	u32 offset_range, void *offset_output, char *sym_name);

#endif /* NLDR_ */
