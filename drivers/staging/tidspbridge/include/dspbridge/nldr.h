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
				void *priv_ref, const struct dcd_nodeprops
				*node_props,
				struct nldr_nodeobject **nldr_nodeobj,
				bool *pf_phase_split);

extern int nldr_create(struct nldr_object **nldr,
			      struct dev_object *hdev_obj,
			      const struct nldr_attrs *pattrs);

extern void nldr_delete(struct nldr_object *nldr_obj);

extern int nldr_get_fxn_addr(struct nldr_nodeobject *nldr_node_obj,
				    char *str_fxn, u32 * addr);

extern int nldr_get_rmm_manager(struct nldr_object *nldr,
				       struct rmm_target_obj **rmm_mgr);

extern int nldr_load(struct nldr_nodeobject *nldr_node_obj,
			    enum nldr_phase phase);
extern int nldr_unload(struct nldr_nodeobject *nldr_node_obj,
			      enum nldr_phase phase);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
int nldr_find_addr(struct nldr_nodeobject *nldr_node, u32 sym_addr,
	u32 offset_range, void *offset_output, char *sym_name);
#endif

#endif /* NLDR_ */
