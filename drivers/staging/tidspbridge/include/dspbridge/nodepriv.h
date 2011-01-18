/*
 * nodepriv.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Private node header shared by NODE and DISP.
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

#ifndef NODEPRIV_
#define NODEPRIV_

#include <dspbridge/strmdefs.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/nldrdefs.h>

/* DSP address of node environment structure */
typedef u32 nodeenv;

/*
 *  Node create structures
 */

/* Message node */
struct node_msgargs {
	u32 max_msgs;		/* Max # of simultaneous messages for node */
	u32 seg_id;		/* Segment for allocating message buffers */
	u32 notify_type;	/* Notify type (SEM_post, SWI_post, etc.) */
	u32 arg_length;		/* Length in 32-bit words of arg data block */
	u8 *pdata;		/* Argument data for node */
};

struct node_strmdef {
	u32 buf_size;		/* Size of buffers for SIO stream */
	u32 num_bufs;		/* max # of buffers in SIO stream at once */
	u32 seg_id;		/* Memory segment id to allocate buffers */
	u32 timeout;		/* Timeout for blocking SIO calls */
	u32 buf_alignment;	/* Buffer alignment */
	char *sz_device;	/* Device name for stream */
};

/* Task node */
struct node_taskargs {
	struct node_msgargs node_msg_args;
	s32 prio;
	u32 stack_size;
	u32 sys_stack_size;
	u32 stack_seg;
	u32 dsp_heap_res_addr;	/* DSP virtual heap address */
	u32 dsp_heap_addr;	/* DSP virtual heap address */
	u32 heap_size;		/* Heap size */
	u32 gpp_heap_addr;	/* GPP virtual heap address */
	u32 profile_id;		/* Profile ID */
	u32 num_inputs;
	u32 num_outputs;
	u32 dais_arg;	/* Address of iAlg object */
	struct node_strmdef *strm_in_def;
	struct node_strmdef *strm_out_def;
};

/*
 *  ======== node_createargs ========
 */
struct node_createargs {
	union {
		struct node_msgargs node_msg_args;
		struct node_taskargs task_arg_obj;
	} asa;
};

/*
 *  ======== node_get_channel_id ========
 *  Purpose:
 *      Get the channel index reserved for a stream connection between the
 *      host and a node. This index is reserved when node_connect() is called
 *      to connect the node with the host. This index should be passed to
 *      the CHNL_Open function when the stream is actually opened.
 *  Parameters:
 *      hnode:          Node object allocated from node_allocate().
 *      dir:           Input (DSP_TONODE) or output (DSP_FROMNODE).
 *      index:         Stream index.
 *      chan_id:        Location to store channel index.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hnode.
 *      -EPERM:  Not a task or DAIS socket node.
 *      -EINVAL:     The node's stream corresponding to index and dir
 *                      is not a stream to or from the host.
 *  Requires:
 *      node_init(void) called.
 *      Valid dir.
 *      chan_id != NULL.
 *  Ensures:
 */
extern int node_get_channel_id(struct node_object *hnode,
				      u32 dir, u32 index, u32 *chan_id);

/*
 *  ======== node_get_strm_mgr ========
 *  Purpose:
 *      Get the STRM manager for a node.
 *  Parameters:
 *      hnode:          Node allocated with node_allocate().
 *      strm_man:       Location to store STRM manager on output.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hnode.
 *  Requires:
 *      strm_man != NULL.
 *  Ensures:
 */
extern int node_get_strm_mgr(struct node_object *hnode,
				    struct strm_mgr **strm_man);

/*
 *  ======== node_get_timeout ========
 *  Purpose:
 *      Get the timeout value of a node.
 *  Parameters:
 *      hnode:      Node allocated with node_allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node's timeout value.
 *  Requires:
 *      Valid hnode.
 *  Ensures:
 */
extern u32 node_get_timeout(struct node_object *hnode);

/*
 *  ======== node_get_type ========
 *  Purpose:
 *      Get the type (device, message, task, or XDAIS socket) of a node.
 *  Parameters:
 *      hnode:      Node allocated with node_allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node type:  NODE_DEVICE, NODE_TASK, NODE_XDAIS, or NODE_GPP.
 *  Requires:
 *      Valid hnode.
 *  Ensures:
 */
extern enum node_type node_get_type(struct node_object *hnode);

/*
 *  ======== get_node_info ========
 *  Purpose:
 *      Get node information without holding semaphore.
 *  Parameters:
 *      hnode:      Node allocated with node_allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node info:  priority, device owner, no. of streams, execution state
 *                  NDB properties.
 *  Requires:
 *      Valid hnode.
 *  Ensures:
 */
extern void get_node_info(struct node_object *hnode,
			  struct dsp_nodeinfo *node_info);

/*
 *  ======== node_get_load_type ========
 *  Purpose:
 *      Get the load type (dynamic, overlay, static) of a node.
 *  Parameters:
 *      hnode:      Node allocated with node_allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node type:  NLDR_DYNAMICLOAD, NLDR_OVLYLOAD, NLDR_STATICLOAD
 *  Requires:
 *      Valid hnode.
 *  Ensures:
 */
extern enum nldr_loadtype node_get_load_type(struct node_object *hnode);

#endif /* NODEPRIV_ */
