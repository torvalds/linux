/*
 * disp.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Node Dispatcher.
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

#ifndef DISP_
#define DISP_

#include <dspbridge/dbdefs.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/nodepriv.h>

struct disp_object;

/* Node Dispatcher attributes */
struct disp_attr {
	u32 chnl_offset;	/* Offset of channel ids reserved for RMS */
	/* Size of buffer for sending data to RMS */
	u32 chnl_buf_size;
	int proc_family;	/* eg, 5000 */
	int proc_type;		/* eg, 5510 */
	void *reserved1;	/* Reserved for future use. */
	u32 reserved2;		/* Reserved for future use. */
};


/*
 *  ======== disp_create ========
 *  Create a NODE Dispatcher object. This object handles the creation,
 *  deletion, and execution of nodes on the DSP target, through communication
 *  with the Resource Manager Server running on the target. Each NODE
 *  Manager object should have exactly one NODE Dispatcher.
 *
 *  Parameters:
 *      dispatch_obj:   Location to store node dispatcher object on output.
 *      hdev_obj:     Device for this processor.
 *      disp_attrs:     Node dispatcher attributes.
 *  Returns:
 *      0:                Success;
 *      -ENOMEM:            Insufficient memory for requested resources.
 *      -EPERM:              Unable to create dispatcher.
 *  Requires:
 *      disp_init(void) called.
 *      disp_attrs != NULL.
 *      hdev_obj != NULL.
 *      dispatch_obj != NULL.
 *  Ensures:
 *      0:        IS_VALID(*dispatch_obj).
 *      error:          *dispatch_obj == NULL.
 */
extern int disp_create(struct disp_object **dispatch_obj,
			      struct dev_object *hdev_obj,
			      const struct disp_attr *disp_attrs);

/*
 *  ======== disp_delete ========
 *  Delete the NODE Dispatcher.
 *
 *  Parameters:
 *      disp_obj:  Node Dispatcher object.
 *  Returns:
 *  Requires:
 *      disp_init(void) called.
 *      Valid disp_obj.
 *  Ensures:
 *      disp_obj is invalid.
 */
extern void disp_delete(struct disp_object *disp_obj);

/*
 *  ======== disp_exit ========
 *  Discontinue usage of DISP module.
 *
 *  Parameters:
 *  Returns:
 *  Requires:
 *      disp_init(void) previously called.
 *  Ensures:
 *      Any resources acquired in disp_init(void) will be freed when last DISP
 *      client calls disp_exit(void).
 */
extern void disp_exit(void);

/*
 *  ======== disp_init ========
 *  Initialize the DISP module.
 *
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
extern bool disp_init(void);

/*
 *  ======== disp_node_change_priority ========
 *  Change the priority of a node currently running on the target.
 *
 *  Parameters:
 *      disp_obj:            Node Dispatcher object.
 *      hnode:                  Node object representing a node currently
 *                              allocated or running on the DSP.
 *      ulFxnAddress:           Address of RMS function for changing priority.
 *      node_env:                Address of node's environment structure.
 *      prio:              New priority level to set node's priority to.
 *  Returns:
 *      0:                Success.
 *      -ETIME:           A timeout occurred before the DSP responded.
 *  Requires:
 *      disp_init(void) called.
 *      Valid disp_obj.
 *      hnode != NULL.
 *  Ensures:
 */
extern int disp_node_change_priority(struct disp_object
					    *disp_obj,
					    struct node_object *hnode,
					    u32 rms_fxn,
					    nodeenv node_env, s32 prio);

/*
 *  ======== disp_node_create ========
 *  Create a node on the DSP by remotely calling the node's create function.
 *
 *  Parameters:
 *      disp_obj:    Node Dispatcher object.
 *      hnode:          Node handle obtained from node_allocate().
 *      ul_fxn_addr:      Address or RMS create node function.
 *      ul_create_fxn:    Address of node's create function.
 *      pargs:          Arguments to pass to RMS node create function.
 *      node_env:       Location to store node environment pointer on
 *                      output.
 *  Returns:
 *      0:        Success.
 *      -ETIME:   A timeout occurred before the DSP responded.
 *      -EPERM:      A failure occurred, unable to create node.
 *  Requires:
 *      disp_init(void) called.
 *      Valid disp_obj.
 *      pargs != NULL.
 *      hnode != NULL.
 *      node_env != NULL.
 *      node_get_type(hnode) != NODE_DEVICE.
 *  Ensures:
 */
extern int disp_node_create(struct disp_object *disp_obj,
				   struct node_object *hnode,
				   u32 rms_fxn,
				   u32 ul_create_fxn,
				   const struct node_createargs
				   *pargs, nodeenv *node_env);

/*
 *  ======== disp_node_delete ========
 *  Delete a node on the DSP by remotely calling the node's delete function.
 *
 *  Parameters:
 *      disp_obj:    Node Dispatcher object.
 *      hnode:          Node object representing a node currently
 *                      loaded on the DSP.
 *      ul_fxn_addr:      Address or RMS delete node function.
 *      ul_delete_fxn:    Address of node's delete function.
 *      node_env:        Address of node's environment structure.
 *  Returns:
 *      0:        Success.
 *      -ETIME:   A timeout occurred before the DSP responded.
 *  Requires:
 *      disp_init(void) called.
 *      Valid disp_obj.
 *      hnode != NULL.
 *  Ensures:
 */
extern int disp_node_delete(struct disp_object *disp_obj,
				   struct node_object *hnode,
				   u32 rms_fxn,
				   u32 ul_delete_fxn, nodeenv node_env);

/*
 *  ======== disp_node_run ========
 *  Start execution of a node's execute phase, or resume execution of a node
 *  that has been suspended (via DISP_NodePause()) on the DSP.
 *
 *  Parameters:
 *      disp_obj:    Node Dispatcher object.
 *      hnode:          Node object representing a node to be executed
 *                      on the DSP.
 *      ul_fxn_addr:      Address or RMS node execute function.
 *      ul_execute_fxn:   Address of node's execute function.
 *      node_env:        Address of node's environment structure.
 *  Returns:
 *      0:        Success.
 *      -ETIME:   A timeout occurred before the DSP responded.
 *  Requires:
 *      disp_init(void) called.
 *      Valid disp_obj.
 *      hnode != NULL.
 *  Ensures:
 */
extern int disp_node_run(struct disp_object *disp_obj,
				struct node_object *hnode,
				u32 rms_fxn,
				u32 ul_execute_fxn, nodeenv node_env);

#endif /* DISP_ */
