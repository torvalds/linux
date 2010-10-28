/*
 * node.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Node Manager.
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

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/list.h>
#include <dspbridge/memdefs.h>
#include <dspbridge/proc.h>
#include <dspbridge/strm.h>
#include <dspbridge/sync.h>
#include <dspbridge/ntfy.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cmm.h>
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>
#include <dspbridge/msg.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/dbdcd.h>
#include <dspbridge/disp.h>
#include <dspbridge/rms_sh.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdefs.h>
#include <dspbridge/dspioctl.h>

/*  ----------------------------------- Others */
#include <dspbridge/gb.h>
#include <dspbridge/uuidutil.h>

/*  ----------------------------------- This */
#include <dspbridge/nodepriv.h>
#include <dspbridge/node.h>

/* Static/Dynamic Loader includes */
#include <dspbridge/dbll.h>
#include <dspbridge/nldr.h>

#include <dspbridge/drv.h>
#include <dspbridge/drvdefs.h>
#include <dspbridge/resourcecleanup.h>
#include <_tiomap.h>

#include <dspbridge/dspdeh.h>

#define HOSTPREFIX	  "/host"
#define PIPEPREFIX	  "/dbpipe"

#define MAX_INPUTS(h)  \
		((h)->dcd_props.obj_data.node_obj.ndb_props.num_input_streams)
#define MAX_OUTPUTS(h) \
		((h)->dcd_props.obj_data.node_obj.ndb_props.num_output_streams)

#define NODE_GET_PRIORITY(h) ((h)->prio)
#define NODE_SET_PRIORITY(hnode, prio) ((hnode)->prio = prio)
#define NODE_SET_STATE(hnode, state) ((hnode)->node_state = state)

#define MAXPIPES	100	/* Max # of /pipe connections (CSL limit) */
#define MAXDEVSUFFIXLEN 2	/* Max(Log base 10 of MAXPIPES, MAXSTREAMS) */

#define PIPENAMELEN     (sizeof(PIPEPREFIX) + MAXDEVSUFFIXLEN)
#define HOSTNAMELEN     (sizeof(HOSTPREFIX) + MAXDEVSUFFIXLEN)

#define MAXDEVNAMELEN	32	/* dsp_ndbprops.ac_name size */
#define CREATEPHASE	1
#define EXECUTEPHASE	2
#define DELETEPHASE	3

/* Define default STRM parameters */
/*
 *  TBD: Put in header file, make global DSP_STRMATTRS with defaults,
 *  or make defaults configurable.
 */
#define DEFAULTBUFSIZE		32
#define DEFAULTNBUFS		2
#define DEFAULTSEGID		0
#define DEFAULTALIGNMENT	0
#define DEFAULTTIMEOUT		10000

#define RMSQUERYSERVER		0
#define RMSCONFIGURESERVER	1
#define RMSCREATENODE		2
#define RMSEXECUTENODE		3
#define RMSDELETENODE		4
#define RMSCHANGENODEPRIORITY	5
#define RMSREADMEMORY		6
#define RMSWRITEMEMORY		7
#define RMSCOPY			8
#define MAXTIMEOUT		2000

#define NUMRMSFXNS		9

#define PWR_TIMEOUT		500	/* default PWR timeout in msec */

#define STACKSEGLABEL "L1DSRAM_HEAP"	/* Label for DSP Stack Segment Addr */

/*
 *  ======== node_mgr ========
 */
struct node_mgr {
	struct dev_object *hdev_obj;	/* Device object */
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;
	struct dcd_manager *hdcd_mgr;	/* Proc/Node data manager */
	struct disp_object *disp_obj;	/* Node dispatcher */
	struct lst_list *node_list;	/* List of all allocated nodes */
	u32 num_nodes;		/* Number of nodes in node_list */
	u32 num_created;	/* Number of nodes *created* on DSP */
	struct gb_t_map *pipe_map;	/* Pipe connection bit map */
	struct gb_t_map *pipe_done_map;	/* Pipes that are half free */
	struct gb_t_map *chnl_map;	/* Channel allocation bit map */
	struct gb_t_map *dma_chnl_map;	/* DMA Channel allocation bit map */
	struct gb_t_map *zc_chnl_map;	/* Zero-Copy Channel alloc bit map */
	struct ntfy_object *ntfy_obj;	/* Manages registered notifications */
	struct mutex node_mgr_lock;	/* For critical sections */
	u32 ul_fxn_addrs[NUMRMSFXNS];	/* RMS function addresses */
	struct msg_mgr *msg_mgr_obj;

	/* Processor properties needed by Node Dispatcher */
	u32 ul_num_chnls;	/* Total number of channels */
	u32 ul_chnl_offset;	/* Offset of chnl ids rsvd for RMS */
	u32 ul_chnl_buf_size;	/* Buffer size for data to RMS */
	int proc_family;	/* eg, 5000 */
	int proc_type;		/* eg, 5510 */
	u32 udsp_word_size;	/* Size of DSP word on host bytes */
	u32 udsp_data_mau_size;	/* Size of DSP data MAU */
	u32 udsp_mau_size;	/* Size of MAU */
	s32 min_pri;		/* Minimum runtime priority for node */
	s32 max_pri;		/* Maximum runtime priority for node */

	struct strm_mgr *strm_mgr_obj;	/* STRM manager */

	/* Loader properties */
	struct nldr_object *nldr_obj;	/* Handle to loader */
	struct node_ldr_fxns nldr_fxns;	/* Handle to loader functions */
	bool loader_init;	/* Loader Init function succeeded? */
};

/*
 *  ======== connecttype ========
 */
enum connecttype {
	NOTCONNECTED = 0,
	NODECONNECT,
	HOSTCONNECT,
	DEVICECONNECT,
};

/*
 *  ======== stream_chnl ========
 */
struct stream_chnl {
	enum connecttype type;	/* Type of stream connection */
	u32 dev_id;		/* pipe or channel id */
};

/*
 *  ======== node_object ========
 */
struct node_object {
	struct list_head list_elem;
	struct node_mgr *hnode_mgr;	/* The manager of this node */
	struct proc_object *hprocessor;	/* Back pointer to processor */
	struct dsp_uuid node_uuid;	/* Node's ID */
	s32 prio;		/* Node's current priority */
	u32 utimeout;		/* Timeout for blocking NODE calls */
	u32 heap_size;		/* Heap Size */
	u32 udsp_heap_virt_addr;	/* Heap Size */
	u32 ugpp_heap_virt_addr;	/* Heap Size */
	enum node_type ntype;	/* Type of node: message, task, etc */
	enum node_state node_state;	/* NODE_ALLOCATED, NODE_CREATED, ... */
	u32 num_inputs;		/* Current number of inputs */
	u32 num_outputs;	/* Current number of outputs */
	u32 max_input_index;	/* Current max input stream index */
	u32 max_output_index;	/* Current max output stream index */
	struct stream_chnl *inputs;	/* Node's input streams */
	struct stream_chnl *outputs;	/* Node's output streams */
	struct node_createargs create_args;	/* Args for node create func */
	nodeenv node_env;	/* Environment returned by RMS */
	struct dcd_genericobj dcd_props;	/* Node properties from DCD */
	struct dsp_cbdata *pargs;	/* Optional args to pass to node */
	struct ntfy_object *ntfy_obj;	/* Manages registered notifications */
	char *pstr_dev_name;	/* device name, if device node */
	struct sync_object *sync_done;	/* Synchronize node_terminate */
	s32 exit_status;	/* execute function return status */

	/* Information needed for node_get_attr() */
	void *device_owner;	/* If dev node, task that owns it */
	u32 num_gpp_inputs;	/* Current # of from GPP streams */
	u32 num_gpp_outputs;	/* Current # of to GPP streams */
	/* Current stream connections */
	struct dsp_streamconnect *stream_connect;

	/* Message queue */
	struct msg_queue *msg_queue_obj;

	/* These fields used for SM messaging */
	struct cmm_xlatorobject *xlator;	/* Node's SM addr translator */

	/* Handle to pass to dynamic loader */
	struct nldr_nodeobject *nldr_node_obj;
	bool loaded;		/* Code is (dynamically) loaded */
	bool phase_split;	/* Phases split in many libs or ovly */

};

/* Default buffer attributes */
static struct dsp_bufferattr node_dfltbufattrs = {
	0,			/* cb_struct */
	1,			/* segment_id */
	0,			/* buf_alignment */
};

static void delete_node(struct node_object *hnode,
			struct process_context *pr_ctxt);
static void delete_node_mgr(struct node_mgr *hnode_mgr);
static void fill_stream_connect(struct node_object *node1,
				struct node_object *node2, u32 stream1,
				u32 stream2);
static void fill_stream_def(struct node_object *hnode,
			    struct node_strmdef *pstrm_def,
			    struct dsp_strmattr *pattrs);
static void free_stream(struct node_mgr *hnode_mgr, struct stream_chnl stream);
static int get_fxn_address(struct node_object *hnode, u32 * fxn_addr,
				  u32 phase);
static int get_node_props(struct dcd_manager *hdcd_mgr,
				 struct node_object *hnode,
				 const struct dsp_uuid *node_uuid,
				 struct dcd_genericobj *dcd_prop);
static int get_proc_props(struct node_mgr *hnode_mgr,
				 struct dev_object *hdev_obj);
static int get_rms_fxns(struct node_mgr *hnode_mgr);
static u32 ovly(void *priv_ref, u32 dsp_run_addr, u32 dsp_load_addr,
		u32 ul_num_bytes, u32 mem_space);
static u32 mem_write(void *priv_ref, u32 dsp_add, void *pbuf,
		     u32 ul_num_bytes, u32 mem_space);

static u32 refs;		/* module reference count */

/* Dynamic loader functions. */
static struct node_ldr_fxns nldr_fxns = {
	nldr_allocate,
	nldr_create,
	nldr_delete,
	nldr_exit,
	nldr_get_fxn_addr,
	nldr_init,
	nldr_load,
	nldr_unload,
};

enum node_state node_get_state(void *hnode)
{
	struct node_object *pnode = (struct node_object *)hnode;
	if (!pnode)
		return -1;
	else
		return pnode->node_state;
}

/*
 *  ======== node_allocate ========
 *  Purpose:
 *      Allocate GPP resources to manage a node on the DSP.
 */
int node_allocate(struct proc_object *hprocessor,
			const struct dsp_uuid *node_uuid,
			const struct dsp_cbdata *pargs,
			const struct dsp_nodeattrin *attr_in,
			struct node_res_object **noderes,
			struct process_context *pr_ctxt)
{
	struct node_mgr *hnode_mgr;
	struct dev_object *hdev_obj;
	struct node_object *pnode = NULL;
	enum node_type node_type = NODE_TASK;
	struct node_msgargs *pmsg_args;
	struct node_taskargs *ptask_args;
	u32 num_streams;
	struct bridge_drv_interface *intf_fxns;
	int status = 0;
	struct cmm_object *hcmm_mgr = NULL;	/* Shared memory manager hndl */
	u32 proc_id;
	u32 pul_value;
	u32 dynext_base;
	u32 off_set = 0;
	u32 ul_stack_seg_addr, ul_stack_seg_val;
	u32 ul_gpp_mem_base;
	struct cfg_hostres *host_res;
	struct bridge_dev_context *pbridge_context;
	u32 mapped_addr = 0;
	u32 map_attrs = 0x0;
	struct dsp_processorstate proc_state;

	void *node_res;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hprocessor != NULL);
	DBC_REQUIRE(noderes != NULL);
	DBC_REQUIRE(node_uuid != NULL);

	*noderes = NULL;

	status = proc_get_processor_id(hprocessor, &proc_id);

	if (proc_id != DSP_UNIT)
		goto func_end;

	status = proc_get_dev_object(hprocessor, &hdev_obj);
	if (!status) {
		status = dev_get_node_manager(hdev_obj, &hnode_mgr);
		if (hnode_mgr == NULL)
			status = -EPERM;

	}

	if (status)
		goto func_end;

	status = dev_get_bridge_context(hdev_obj, &pbridge_context);
	if (!pbridge_context) {
		status = -EFAULT;
		goto func_end;
	}

	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in error state then don't attempt
	   to send the message */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}

	/* Assuming that 0 is not a valid function address */
	if (hnode_mgr->ul_fxn_addrs[0] == 0) {
		/* No RMS on target - we currently can't handle this */
		pr_err("%s: Failed, no RMS in base image\n", __func__);
		status = -EPERM;
	} else {
		/* Validate attr_in fields, if non-NULL */
		if (attr_in) {
			/* Check if attr_in->prio is within range */
			if (attr_in->prio < hnode_mgr->min_pri ||
			    attr_in->prio > hnode_mgr->max_pri)
				status = -EDOM;
		}
	}
	/* Allocate node object and fill in */
	if (status)
		goto func_end;

	pnode = kzalloc(sizeof(struct node_object), GFP_KERNEL);
	if (pnode == NULL) {
		status = -ENOMEM;
		goto func_end;
	}
	pnode->hnode_mgr = hnode_mgr;
	/* This critical section protects get_node_props */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	/* Get dsp_ndbprops from node database */
	status = get_node_props(hnode_mgr->hdcd_mgr, pnode, node_uuid,
				&(pnode->dcd_props));
	if (status)
		goto func_cont;

	pnode->node_uuid = *node_uuid;
	pnode->hprocessor = hprocessor;
	pnode->ntype = pnode->dcd_props.obj_data.node_obj.ndb_props.ntype;
	pnode->utimeout = pnode->dcd_props.obj_data.node_obj.ndb_props.utimeout;
	pnode->prio = pnode->dcd_props.obj_data.node_obj.ndb_props.prio;

	/* Currently only C64 DSP builds support Node Dynamic * heaps */
	/* Allocate memory for node heap */
	pnode->create_args.asa.task_arg_obj.heap_size = 0;
	pnode->create_args.asa.task_arg_obj.udsp_heap_addr = 0;
	pnode->create_args.asa.task_arg_obj.udsp_heap_res_addr = 0;
	pnode->create_args.asa.task_arg_obj.ugpp_heap_addr = 0;
	if (!attr_in)
		goto func_cont;

	/* Check if we have a user allocated node heap */
	if (!(attr_in->pgpp_virt_addr))
		goto func_cont;

	/* check for page aligned Heap size */
	if (((attr_in->heap_size) & (PG_SIZE4K - 1))) {
		pr_err("%s: node heap size not aligned to 4K, size = 0x%x \n",
		       __func__, attr_in->heap_size);
		status = -EINVAL;
	} else {
		pnode->create_args.asa.task_arg_obj.heap_size =
		    attr_in->heap_size;
		pnode->create_args.asa.task_arg_obj.ugpp_heap_addr =
		    (u32) attr_in->pgpp_virt_addr;
	}
	if (status)
		goto func_cont;

	map_attrs |= DSP_MAPLITTLEENDIAN;
	map_attrs |= DSP_MAPELEMSIZE32;
	map_attrs |= DSP_MAPVIRTUALADDR;
	status = proc_map(hprocessor, (void *)attr_in->pgpp_virt_addr,
			  pnode->create_args.asa.task_arg_obj.heap_size,
			  NULL, (void **)&mapped_addr, map_attrs,
			  pr_ctxt);
	if (status)
		pr_err("%s: Failed to map memory for Heap: 0x%x\n",
		       __func__, status);
	else
		pnode->create_args.asa.task_arg_obj.udsp_heap_addr =
		    (u32) mapped_addr;

func_cont:
	mutex_unlock(&hnode_mgr->node_mgr_lock);
	if (attr_in != NULL) {
		/* Overrides of NBD properties */
		pnode->utimeout = attr_in->utimeout;
		pnode->prio = attr_in->prio;
	}
	/* Create object to manage notifications */
	if (!status) {
		pnode->ntfy_obj = kmalloc(sizeof(struct ntfy_object),
							GFP_KERNEL);
		if (pnode->ntfy_obj)
			ntfy_init(pnode->ntfy_obj);
		else
			status = -ENOMEM;
	}

	if (!status) {
		node_type = node_get_type(pnode);
		/*  Allocate dsp_streamconnect array for device, task, and
		 *  dais socket nodes. */
		if (node_type != NODE_MESSAGE) {
			num_streams = MAX_INPUTS(pnode) + MAX_OUTPUTS(pnode);
			pnode->stream_connect = kzalloc(num_streams *
					sizeof(struct dsp_streamconnect),
					GFP_KERNEL);
			if (num_streams > 0 && pnode->stream_connect == NULL)
				status = -ENOMEM;

		}
		if (!status && (node_type == NODE_TASK ||
					      node_type == NODE_DAISSOCKET)) {
			/* Allocate arrays for maintainig stream connections */
			pnode->inputs = kzalloc(MAX_INPUTS(pnode) *
					sizeof(struct stream_chnl), GFP_KERNEL);
			pnode->outputs = kzalloc(MAX_OUTPUTS(pnode) *
					sizeof(struct stream_chnl), GFP_KERNEL);
			ptask_args = &(pnode->create_args.asa.task_arg_obj);
			ptask_args->strm_in_def = kzalloc(MAX_INPUTS(pnode) *
						sizeof(struct node_strmdef),
						GFP_KERNEL);
			ptask_args->strm_out_def = kzalloc(MAX_OUTPUTS(pnode) *
						sizeof(struct node_strmdef),
						GFP_KERNEL);
			if ((MAX_INPUTS(pnode) > 0 && (pnode->inputs == NULL ||
						       ptask_args->strm_in_def
						       == NULL))
			    || (MAX_OUTPUTS(pnode) > 0
				&& (pnode->outputs == NULL
				    || ptask_args->strm_out_def == NULL)))
				status = -ENOMEM;
		}
	}
	if (!status && (node_type != NODE_DEVICE)) {
		/* Create an event that will be posted when RMS_EXIT is
		 * received. */
		pnode->sync_done = kzalloc(sizeof(struct sync_object),
								GFP_KERNEL);
		if (pnode->sync_done)
			sync_init_event(pnode->sync_done);
		else
			status = -ENOMEM;

		if (!status) {
			/*Get the shared mem mgr for this nodes dev object */
			status = cmm_get_handle(hprocessor, &hcmm_mgr);
			if (!status) {
				/* Allocate a SM addr translator for this node
				 * w/ deflt attr */
				status = cmm_xlator_create(&pnode->xlator,
							   hcmm_mgr, NULL);
			}
		}
		if (!status) {
			/* Fill in message args */
			if ((pargs != NULL) && (pargs->cb_data > 0)) {
				pmsg_args =
				    &(pnode->create_args.asa.node_msg_args);
				pmsg_args->pdata = kzalloc(pargs->cb_data,
								GFP_KERNEL);
				if (pmsg_args->pdata == NULL) {
					status = -ENOMEM;
				} else {
					pmsg_args->arg_length = pargs->cb_data;
					memcpy(pmsg_args->pdata,
					       pargs->node_data,
					       pargs->cb_data);
				}
			}
		}
	}

	if (!status && node_type != NODE_DEVICE) {
		/* Create a message queue for this node */
		intf_fxns = hnode_mgr->intf_fxns;
		status =
		    (*intf_fxns->pfn_msg_create_queue) (hnode_mgr->msg_mgr_obj,
							&pnode->msg_queue_obj,
							0,
							pnode->create_args.asa.
							node_msg_args.max_msgs,
							pnode);
	}

	if (!status) {
		/* Create object for dynamic loading */

		status = hnode_mgr->nldr_fxns.pfn_allocate(hnode_mgr->nldr_obj,
							   (void *)pnode,
							   &pnode->dcd_props.
							   obj_data.node_obj,
							   &pnode->
							   nldr_node_obj,
							   &pnode->phase_split);
	}

	/* Compare value read from Node Properties and check if it is same as
	 * STACKSEGLABEL, if yes read the Address of STACKSEGLABEL, calculate
	 * GPP Address, Read the value in that address and override the
	 * stack_seg value in task args */
	if (!status &&
	    (char *)pnode->dcd_props.obj_data.node_obj.ndb_props.
	    stack_seg_name != NULL) {
		if (strcmp((char *)
			   pnode->dcd_props.obj_data.node_obj.ndb_props.
			   stack_seg_name, STACKSEGLABEL) == 0) {
			status =
			    hnode_mgr->nldr_fxns.
			    pfn_get_fxn_addr(pnode->nldr_node_obj, "DYNEXT_BEG",
					     &dynext_base);
			if (status)
				pr_err("%s: Failed to get addr for DYNEXT_BEG"
				       " status = 0x%x\n", __func__, status);

			status =
			    hnode_mgr->nldr_fxns.
			    pfn_get_fxn_addr(pnode->nldr_node_obj,
					     "L1DSRAM_HEAP", &pul_value);

			if (status)
				pr_err("%s: Failed to get addr for L1DSRAM_HEAP"
				       " status = 0x%x\n", __func__, status);

			host_res = pbridge_context->resources;
			if (!host_res)
				status = -EPERM;

			if (status) {
				pr_err("%s: Failed to get host resource, status"
				       " = 0x%x\n", __func__, status);
				goto func_end;
			}

			ul_gpp_mem_base = (u32) host_res->dw_mem_base[1];
			off_set = pul_value - dynext_base;
			ul_stack_seg_addr = ul_gpp_mem_base + off_set;
			ul_stack_seg_val = readl(ul_stack_seg_addr);

			dev_dbg(bridge, "%s: StackSegVal = 0x%x, StackSegAddr ="
				" 0x%x\n", __func__, ul_stack_seg_val,
				ul_stack_seg_addr);

			pnode->create_args.asa.task_arg_obj.stack_seg =
			    ul_stack_seg_val;

		}
	}

	if (!status) {
		/* Add the node to the node manager's list of allocated
		 * nodes. */
		lst_init_elem((struct list_head *)pnode);
		NODE_SET_STATE(pnode, NODE_ALLOCATED);

		mutex_lock(&hnode_mgr->node_mgr_lock);

		lst_put_tail(hnode_mgr->node_list, (struct list_head *) pnode);
			++(hnode_mgr->num_nodes);

		/* Exit critical section */
		mutex_unlock(&hnode_mgr->node_mgr_lock);

		/* Preset this to assume phases are split
		 * (for overlay and dll) */
		pnode->phase_split = true;

		/* Notify all clients registered for DSP_NODESTATECHANGE. */
		proc_notify_all_clients(hprocessor, DSP_NODESTATECHANGE);
	} else {
		/* Cleanup */
		if (pnode)
			delete_node(pnode, pr_ctxt);

	}

	if (!status) {
		status = drv_insert_node_res_element(pnode, &node_res, pr_ctxt);
		if (status) {
			delete_node(pnode, pr_ctxt);
			goto func_end;
		}

		*noderes = (struct node_res_object *)node_res;
		drv_proc_node_update_heap_status(node_res, true);
		drv_proc_node_update_status(node_res, true);
	}
	DBC_ENSURE((status && *noderes == NULL) || (!status && *noderes));
func_end:
	dev_dbg(bridge, "%s: hprocessor: %p pNodeId: %p pargs: %p attr_in: %p "
		"node_res: %p status: 0x%x\n", __func__, hprocessor,
		node_uuid, pargs, attr_in, noderes, status);
	return status;
}

/*
 *  ======== node_alloc_msg_buf ========
 *  Purpose:
 *      Allocates buffer for zero copy messaging.
 */
DBAPI node_alloc_msg_buf(struct node_object *hnode, u32 usize,
			 struct dsp_bufferattr *pattr,
			 u8 **pbuffer)
{
	struct node_object *pnode = (struct node_object *)hnode;
	int status = 0;
	bool va_flag = false;
	bool set_info;
	u32 proc_id;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pbuffer != NULL);

	DBC_REQUIRE(usize > 0);

	if (!pnode)
		status = -EFAULT;
	else if (node_get_type(pnode) == NODE_DEVICE)
		status = -EPERM;

	if (status)
		goto func_end;

	if (pattr == NULL)
		pattr = &node_dfltbufattrs;	/* set defaults */

	status = proc_get_processor_id(pnode->hprocessor, &proc_id);
	if (proc_id != DSP_UNIT) {
		DBC_ASSERT(NULL);
		goto func_end;
	}
	/*  If segment ID includes MEM_SETVIRTUALSEGID then pbuffer is a
	 *  virt  address, so set this info in this node's translator
	 *  object for  future ref. If MEM_GETVIRTUALSEGID then retrieve
	 *  virtual address  from node's translator. */
	if ((pattr->segment_id & MEM_SETVIRTUALSEGID) ||
	    (pattr->segment_id & MEM_GETVIRTUALSEGID)) {
		va_flag = true;
		set_info = (pattr->segment_id & MEM_SETVIRTUALSEGID) ?
		    true : false;
		/* Clear mask bits */
		pattr->segment_id &= ~MEM_MASKVIRTUALSEGID;
		/* Set/get this node's translators virtual address base/size */
		status = cmm_xlator_info(pnode->xlator, pbuffer, usize,
					 pattr->segment_id, set_info);
	}
	if (!status && (!va_flag)) {
		if (pattr->segment_id != 1) {
			/* Node supports single SM segment only. */
			status = -EBADR;
		}
		/*  Arbitrary SM buffer alignment not supported for host side
		 *  allocs, but guaranteed for the following alignment
		 *  values. */
		switch (pattr->buf_alignment) {
		case 0:
		case 1:
		case 2:
		case 4:
			break;
		default:
			/* alignment value not suportted */
			status = -EPERM;
			break;
		}
		if (!status) {
			/* allocate physical buffer from seg_id in node's
			 * translator */
			(void)cmm_xlator_alloc_buf(pnode->xlator, pbuffer,
						   usize);
			if (*pbuffer == NULL) {
				pr_err("%s: error - Out of shared memory\n",
				       __func__);
				status = -ENOMEM;
			}
		}
	}
func_end:
	return status;
}

/*
 *  ======== node_change_priority ========
 *  Purpose:
 *      Change the priority of a node in the allocated state, or that is
 *      currently running or paused on the target.
 */
int node_change_priority(struct node_object *hnode, s32 prio)
{
	struct node_object *pnode = (struct node_object *)hnode;
	struct node_mgr *hnode_mgr = NULL;
	enum node_type node_type;
	enum node_state state;
	int status = 0;
	u32 proc_id;

	DBC_REQUIRE(refs > 0);

	if (!hnode || !hnode->hnode_mgr) {
		status = -EFAULT;
	} else {
		hnode_mgr = hnode->hnode_mgr;
		node_type = node_get_type(hnode);
		if (node_type != NODE_TASK && node_type != NODE_DAISSOCKET)
			status = -EPERM;
		else if (prio < hnode_mgr->min_pri || prio > hnode_mgr->max_pri)
			status = -EDOM;
	}
	if (status)
		goto func_end;

	/* Enter critical section */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	state = node_get_state(hnode);
	if (state == NODE_ALLOCATED || state == NODE_PAUSED) {
		NODE_SET_PRIORITY(hnode, prio);
	} else {
		if (state != NODE_RUNNING) {
			status = -EBADR;
			goto func_cont;
		}
		status = proc_get_processor_id(pnode->hprocessor, &proc_id);
		if (proc_id == DSP_UNIT) {
			status =
			    disp_node_change_priority(hnode_mgr->disp_obj,
						      hnode,
						      hnode_mgr->ul_fxn_addrs
						      [RMSCHANGENODEPRIORITY],
						      hnode->node_env, prio);
		}
		if (status >= 0)
			NODE_SET_PRIORITY(hnode, prio);

	}
func_cont:
	/* Leave critical section */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
func_end:
	return status;
}

/*
 *  ======== node_connect ========
 *  Purpose:
 *      Connect two nodes on the DSP, or a node on the DSP to the GPP.
 */
int node_connect(struct node_object *node1, u32 stream1,
			struct node_object *node2,
			u32 stream2, struct dsp_strmattr *pattrs,
			struct dsp_cbdata *conn_param)
{
	struct node_mgr *hnode_mgr;
	char *pstr_dev_name = NULL;
	enum node_type node1_type = NODE_TASK;
	enum node_type node2_type = NODE_TASK;
	struct node_strmdef *pstrm_def;
	struct node_strmdef *input = NULL;
	struct node_strmdef *output = NULL;
	struct node_object *dev_node_obj;
	struct node_object *hnode;
	struct stream_chnl *pstream;
	u32 pipe_id = GB_NOBITS;
	u32 chnl_id = GB_NOBITS;
	s8 chnl_mode;
	u32 dw_length;
	int status = 0;
	DBC_REQUIRE(refs > 0);

	if ((node1 != (struct node_object *)DSP_HGPPNODE && !node1) ||
	    (node2 != (struct node_object *)DSP_HGPPNODE && !node2))
		status = -EFAULT;

	if (!status) {
		/* The two nodes must be on the same processor */
		if (node1 != (struct node_object *)DSP_HGPPNODE &&
		    node2 != (struct node_object *)DSP_HGPPNODE &&
		    node1->hnode_mgr != node2->hnode_mgr)
			status = -EPERM;
		/* Cannot connect a node to itself */
		if (node1 == node2)
			status = -EPERM;

	}
	if (!status) {
		/* node_get_type() will return NODE_GPP if hnode =
		 * DSP_HGPPNODE. */
		node1_type = node_get_type(node1);
		node2_type = node_get_type(node2);
		/* Check stream indices ranges */
		if ((node1_type != NODE_GPP && node1_type != NODE_DEVICE &&
		     stream1 >= MAX_OUTPUTS(node1)) || (node2_type != NODE_GPP
							  && node2_type !=
							  NODE_DEVICE
							  && stream2 >=
							  MAX_INPUTS(node2)))
			status = -EINVAL;
	}
	if (!status) {
		/*
		 *  Only the following types of connections are allowed:
		 *      task/dais socket < == > task/dais socket
		 *      task/dais socket < == > device
		 *      task/dais socket < == > GPP
		 *
		 *  ie, no message nodes, and at least one task or dais
		 *  socket node.
		 */
		if (node1_type == NODE_MESSAGE || node2_type == NODE_MESSAGE ||
		    (node1_type != NODE_TASK && node1_type != NODE_DAISSOCKET &&
		     node2_type != NODE_TASK && node2_type != NODE_DAISSOCKET))
			status = -EPERM;
	}
	/*
	 * Check stream mode. Default is STRMMODE_PROCCOPY.
	 */
	if (!status && pattrs) {
		if (pattrs->strm_mode != STRMMODE_PROCCOPY)
			status = -EPERM;	/* illegal stream mode */

	}
	if (status)
		goto func_end;

	if (node1_type != NODE_GPP) {
		hnode_mgr = node1->hnode_mgr;
	} else {
		DBC_ASSERT(node2 != (struct node_object *)DSP_HGPPNODE);
		hnode_mgr = node2->hnode_mgr;
	}
	/* Enter critical section */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	/* Nodes must be in the allocated state */
	if (node1_type != NODE_GPP && node_get_state(node1) != NODE_ALLOCATED)
		status = -EBADR;

	if (node2_type != NODE_GPP && node_get_state(node2) != NODE_ALLOCATED)
		status = -EBADR;

	if (!status) {
		/*  Check that stream indices for task and dais socket nodes
		 *  are not already be used. (Device nodes checked later) */
		if (node1_type == NODE_TASK || node1_type == NODE_DAISSOCKET) {
			output =
			    &(node1->create_args.asa.
			      task_arg_obj.strm_out_def[stream1]);
			if (output->sz_device != NULL)
				status = -EISCONN;

		}
		if (node2_type == NODE_TASK || node2_type == NODE_DAISSOCKET) {
			input =
			    &(node2->create_args.asa.
			      task_arg_obj.strm_in_def[stream2]);
			if (input->sz_device != NULL)
				status = -EISCONN;

		}
	}
	/* Connecting two task nodes? */
	if (!status && ((node1_type == NODE_TASK ||
				       node1_type == NODE_DAISSOCKET)
				      && (node2_type == NODE_TASK
					  || node2_type == NODE_DAISSOCKET))) {
		/* Find available pipe */
		pipe_id = gb_findandset(hnode_mgr->pipe_map);
		if (pipe_id == GB_NOBITS) {
			status = -ECONNREFUSED;
		} else {
			node1->outputs[stream1].type = NODECONNECT;
			node2->inputs[stream2].type = NODECONNECT;
			node1->outputs[stream1].dev_id = pipe_id;
			node2->inputs[stream2].dev_id = pipe_id;
			output->sz_device = kzalloc(PIPENAMELEN + 1,
							GFP_KERNEL);
			input->sz_device = kzalloc(PIPENAMELEN + 1, GFP_KERNEL);
			if (output->sz_device == NULL ||
			    input->sz_device == NULL) {
				/* Undo the connection */
				kfree(output->sz_device);

				kfree(input->sz_device);

				output->sz_device = NULL;
				input->sz_device = NULL;
				gb_clear(hnode_mgr->pipe_map, pipe_id);
				status = -ENOMEM;
			} else {
				/* Copy "/dbpipe<pipId>" name to device names */
				sprintf(output->sz_device, "%s%d",
					PIPEPREFIX, pipe_id);
				strcpy(input->sz_device, output->sz_device);
			}
		}
	}
	/* Connecting task node to host? */
	if (!status && (node1_type == NODE_GPP ||
				      node2_type == NODE_GPP)) {
		if (node1_type == NODE_GPP) {
			chnl_mode = CHNL_MODETODSP;
		} else {
			DBC_ASSERT(node2_type == NODE_GPP);
			chnl_mode = CHNL_MODEFROMDSP;
		}
		/*  Reserve a channel id. We need to put the name "/host<id>"
		 *  in the node's create_args, but the host
		 *  side channel will not be opened until DSPStream_Open is
		 *  called for this node. */
		if (pattrs) {
			if (pattrs->strm_mode == STRMMODE_RDMA) {
				chnl_id =
				    gb_findandset(hnode_mgr->dma_chnl_map);
				/* dma chans are 2nd transport chnl set
				 * ids(e.g. 16-31) */
				(chnl_id != GB_NOBITS) ?
				    (chnl_id =
				     chnl_id +
				     hnode_mgr->ul_num_chnls) : chnl_id;
			} else if (pattrs->strm_mode == STRMMODE_ZEROCOPY) {
				chnl_id = gb_findandset(hnode_mgr->zc_chnl_map);
				/* zero-copy chans are 3nd transport set
				 * (e.g. 32-47) */
				(chnl_id != GB_NOBITS) ? (chnl_id = chnl_id +
							  (2 *
							   hnode_mgr->
							   ul_num_chnls))
				    : chnl_id;
			} else {	/* must be PROCCOPY */
				DBC_ASSERT(pattrs->strm_mode ==
					   STRMMODE_PROCCOPY);
				chnl_id = gb_findandset(hnode_mgr->chnl_map);
				/* e.g. 0-15 */
			}
		} else {
			/* default to PROCCOPY */
			chnl_id = gb_findandset(hnode_mgr->chnl_map);
		}
		if (chnl_id == GB_NOBITS) {
			status = -ECONNREFUSED;
			goto func_cont2;
		}
		pstr_dev_name = kzalloc(HOSTNAMELEN + 1, GFP_KERNEL);
		if (pstr_dev_name != NULL)
			goto func_cont2;

		if (pattrs) {
			if (pattrs->strm_mode == STRMMODE_RDMA) {
				gb_clear(hnode_mgr->dma_chnl_map, chnl_id -
					 hnode_mgr->ul_num_chnls);
			} else if (pattrs->strm_mode == STRMMODE_ZEROCOPY) {
				gb_clear(hnode_mgr->zc_chnl_map, chnl_id -
					 (2 * hnode_mgr->ul_num_chnls));
			} else {
				DBC_ASSERT(pattrs->strm_mode ==
					   STRMMODE_PROCCOPY);
				gb_clear(hnode_mgr->chnl_map, chnl_id);
			}
		} else {
			gb_clear(hnode_mgr->chnl_map, chnl_id);
		}
		status = -ENOMEM;
func_cont2:
		if (!status) {
			if (node1 == (struct node_object *)DSP_HGPPNODE) {
				node2->inputs[stream2].type = HOSTCONNECT;
				node2->inputs[stream2].dev_id = chnl_id;
				input->sz_device = pstr_dev_name;
			} else {
				node1->outputs[stream1].type = HOSTCONNECT;
				node1->outputs[stream1].dev_id = chnl_id;
				output->sz_device = pstr_dev_name;
			}
			sprintf(pstr_dev_name, "%s%d", HOSTPREFIX, chnl_id);
		}
	}
	/* Connecting task node to device node? */
	if (!status && ((node1_type == NODE_DEVICE) ||
				      (node2_type == NODE_DEVICE))) {
		if (node2_type == NODE_DEVICE) {
			/* node1 == > device */
			dev_node_obj = node2;
			hnode = node1;
			pstream = &(node1->outputs[stream1]);
			pstrm_def = output;
		} else {
			/* device == > node2 */
			dev_node_obj = node1;
			hnode = node2;
			pstream = &(node2->inputs[stream2]);
			pstrm_def = input;
		}
		/* Set up create args */
		pstream->type = DEVICECONNECT;
		dw_length = strlen(dev_node_obj->pstr_dev_name);
		if (conn_param != NULL) {
			pstrm_def->sz_device = kzalloc(dw_length + 1 +
							conn_param->cb_data,
							GFP_KERNEL);
		} else {
			pstrm_def->sz_device = kzalloc(dw_length + 1,
							GFP_KERNEL);
		}
		if (pstrm_def->sz_device == NULL) {
			status = -ENOMEM;
		} else {
			/* Copy device name */
			strncpy(pstrm_def->sz_device,
				dev_node_obj->pstr_dev_name, dw_length);
			if (conn_param != NULL) {
				strncat(pstrm_def->sz_device,
					(char *)conn_param->node_data,
					(u32) conn_param->cb_data);
			}
			dev_node_obj->device_owner = hnode;
		}
	}
	if (!status) {
		/* Fill in create args */
		if (node1_type == NODE_TASK || node1_type == NODE_DAISSOCKET) {
			node1->create_args.asa.task_arg_obj.num_outputs++;
			fill_stream_def(node1, output, pattrs);
		}
		if (node2_type == NODE_TASK || node2_type == NODE_DAISSOCKET) {
			node2->create_args.asa.task_arg_obj.num_inputs++;
			fill_stream_def(node2, input, pattrs);
		}
		/* Update node1 and node2 stream_connect */
		if (node1_type != NODE_GPP && node1_type != NODE_DEVICE) {
			node1->num_outputs++;
			if (stream1 > node1->max_output_index)
				node1->max_output_index = stream1;

		}
		if (node2_type != NODE_GPP && node2_type != NODE_DEVICE) {
			node2->num_inputs++;
			if (stream2 > node2->max_input_index)
				node2->max_input_index = stream2;

		}
		fill_stream_connect(node1, node2, stream1, stream2);
	}
	/* end of sync_enter_cs */
	/* Exit critical section */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
func_end:
	dev_dbg(bridge, "%s: node1: %p stream1: %d node2: %p stream2: %d"
		"pattrs: %p status: 0x%x\n", __func__, node1,
		stream1, node2, stream2, pattrs, status);
	return status;
}

/*
 *  ======== node_create ========
 *  Purpose:
 *      Create a node on the DSP by remotely calling the node's create function.
 */
int node_create(struct node_object *hnode)
{
	struct node_object *pnode = (struct node_object *)hnode;
	struct node_mgr *hnode_mgr;
	struct bridge_drv_interface *intf_fxns;
	u32 ul_create_fxn;
	enum node_type node_type;
	int status = 0;
	int status1 = 0;
	struct dsp_cbdata cb_data;
	u32 proc_id = 255;
	struct dsp_processorstate proc_state;
	struct proc_object *hprocessor;
#if defined(CONFIG_TIDSPBRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	struct dspbridge_platform_data *pdata =
	    omap_dspbridge_dev->dev.platform_data;
#endif

	DBC_REQUIRE(refs > 0);
	if (!pnode) {
		status = -EFAULT;
		goto func_end;
	}
	hprocessor = hnode->hprocessor;
	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in error state then don't attempt to create
	   new node */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}
	/* create struct dsp_cbdata struct for PWR calls */
	cb_data.cb_data = PWR_TIMEOUT;
	node_type = node_get_type(hnode);
	hnode_mgr = hnode->hnode_mgr;
	intf_fxns = hnode_mgr->intf_fxns;
	/* Get access to node dispatcher */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	/* Check node state */
	if (node_get_state(hnode) != NODE_ALLOCATED)
		status = -EBADR;

	if (!status)
		status = proc_get_processor_id(pnode->hprocessor, &proc_id);

	if (status)
		goto func_cont2;

	if (proc_id != DSP_UNIT)
		goto func_cont2;

	/* Make sure streams are properly connected */
	if ((hnode->num_inputs && hnode->max_input_index >
	     hnode->num_inputs - 1) ||
	    (hnode->num_outputs && hnode->max_output_index >
	     hnode->num_outputs - 1))
		status = -ENOTCONN;

	if (!status) {
		/* If node's create function is not loaded, load it */
		/* Boost the OPP level to max level that DSP can be requested */
#if defined(CONFIG_TIDSPBRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq)
			(*pdata->cpu_set_freq) (pdata->mpu_speed[VDD1_OPP3]);
#endif
		status = hnode_mgr->nldr_fxns.pfn_load(hnode->nldr_node_obj,
						       NLDR_CREATE);
		/* Get address of node's create function */
		if (!status) {
			hnode->loaded = true;
			if (node_type != NODE_DEVICE) {
				status = get_fxn_address(hnode, &ul_create_fxn,
							 CREATEPHASE);
			}
		} else {
			pr_err("%s: failed to load create code: 0x%x\n",
			       __func__, status);
		}
		/* Request the lowest OPP level */
#if defined(CONFIG_TIDSPBRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq)
			(*pdata->cpu_set_freq) (pdata->mpu_speed[VDD1_OPP1]);
#endif
		/* Get address of iAlg functions, if socket node */
		if (!status) {
			if (node_type == NODE_DAISSOCKET) {
				status = hnode_mgr->nldr_fxns.pfn_get_fxn_addr
				    (hnode->nldr_node_obj,
				     hnode->dcd_props.obj_data.node_obj.
				     pstr_i_alg_name,
				     &hnode->create_args.asa.
				     task_arg_obj.ul_dais_arg);
			}
		}
	}
	if (!status) {
		if (node_type != NODE_DEVICE) {
			status = disp_node_create(hnode_mgr->disp_obj, hnode,
						  hnode_mgr->ul_fxn_addrs
						  [RMSCREATENODE],
						  ul_create_fxn,
						  &(hnode->create_args),
						  &(hnode->node_env));
			if (status >= 0) {
				/* Set the message queue id to the node env
				 * pointer */
				intf_fxns = hnode_mgr->intf_fxns;
				(*intf_fxns->pfn_msg_set_queue_id) (hnode->
							msg_queue_obj,
							hnode->node_env);
			}
		}
	}
	/*  Phase II/Overlays: Create, execute, delete phases  possibly in
	 *  different files/sections. */
	if (hnode->loaded && hnode->phase_split) {
		/* If create code was dynamically loaded, we can now unload
		 * it. */
		status1 = hnode_mgr->nldr_fxns.pfn_unload(hnode->nldr_node_obj,
							  NLDR_CREATE);
		hnode->loaded = false;
	}
	if (status1)
		pr_err("%s: Failed to unload create code: 0x%x\n",
		       __func__, status1);
func_cont2:
	/* Update node state and node manager state */
	if (status >= 0) {
		NODE_SET_STATE(hnode, NODE_CREATED);
		hnode_mgr->num_created++;
		goto func_cont;
	}
	if (status != -EBADR) {
		/* Put back in NODE_ALLOCATED state if error occurred */
		NODE_SET_STATE(hnode, NODE_ALLOCATED);
	}
func_cont:
	/* Free access to node dispatcher */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
func_end:
	if (status >= 0) {
		proc_notify_clients(hnode->hprocessor, DSP_NODESTATECHANGE);
		ntfy_notify(hnode->ntfy_obj, DSP_NODESTATECHANGE);
	}

	dev_dbg(bridge, "%s: hnode: %p status: 0x%x\n", __func__,
		hnode, status);
	return status;
}

/*
 *  ======== node_create_mgr ========
 *  Purpose:
 *      Create a NODE Manager object.
 */
int node_create_mgr(struct node_mgr **node_man,
			   struct dev_object *hdev_obj)
{
	u32 i;
	struct node_mgr *node_mgr_obj = NULL;
	struct disp_attr disp_attr_obj;
	char *sz_zl_file = "";
	struct nldr_attrs nldr_attrs_obj;
	int status = 0;
	u8 dev_type;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(node_man != NULL);
	DBC_REQUIRE(hdev_obj != NULL);

	*node_man = NULL;
	/* Allocate Node manager object */
	node_mgr_obj = kzalloc(sizeof(struct node_mgr), GFP_KERNEL);
	if (node_mgr_obj) {
		node_mgr_obj->hdev_obj = hdev_obj;
		node_mgr_obj->node_list = kzalloc(sizeof(struct lst_list),
							GFP_KERNEL);
		node_mgr_obj->pipe_map = gb_create(MAXPIPES);
		node_mgr_obj->pipe_done_map = gb_create(MAXPIPES);
		if (node_mgr_obj->node_list == NULL
		    || node_mgr_obj->pipe_map == NULL
		    || node_mgr_obj->pipe_done_map == NULL) {
			status = -ENOMEM;
		} else {
			INIT_LIST_HEAD(&node_mgr_obj->node_list->head);
			node_mgr_obj->ntfy_obj = kmalloc(
				sizeof(struct ntfy_object), GFP_KERNEL);
			if (node_mgr_obj->ntfy_obj)
				ntfy_init(node_mgr_obj->ntfy_obj);
			else
				status = -ENOMEM;
		}
		node_mgr_obj->num_created = 0;
	} else {
		status = -ENOMEM;
	}
	/* get devNodeType */
	if (!status)
		status = dev_get_dev_type(hdev_obj, &dev_type);

	/* Create the DCD Manager */
	if (!status) {
		status =
		    dcd_create_manager(sz_zl_file, &node_mgr_obj->hdcd_mgr);
		if (!status)
			status = get_proc_props(node_mgr_obj, hdev_obj);

	}
	/* Create NODE Dispatcher */
	if (!status) {
		disp_attr_obj.ul_chnl_offset = node_mgr_obj->ul_chnl_offset;
		disp_attr_obj.ul_chnl_buf_size = node_mgr_obj->ul_chnl_buf_size;
		disp_attr_obj.proc_family = node_mgr_obj->proc_family;
		disp_attr_obj.proc_type = node_mgr_obj->proc_type;
		status =
		    disp_create(&node_mgr_obj->disp_obj, hdev_obj,
				&disp_attr_obj);
	}
	/* Create a STRM Manager */
	if (!status)
		status = strm_create(&node_mgr_obj->strm_mgr_obj, hdev_obj);

	if (!status) {
		dev_get_intf_fxns(hdev_obj, &node_mgr_obj->intf_fxns);
		/* Get msg_ctrl queue manager */
		dev_get_msg_mgr(hdev_obj, &node_mgr_obj->msg_mgr_obj);
		mutex_init(&node_mgr_obj->node_mgr_lock);
		node_mgr_obj->chnl_map = gb_create(node_mgr_obj->ul_num_chnls);
		/* dma chnl map. ul_num_chnls is # per transport */
		node_mgr_obj->dma_chnl_map =
		    gb_create(node_mgr_obj->ul_num_chnls);
		node_mgr_obj->zc_chnl_map =
		    gb_create(node_mgr_obj->ul_num_chnls);
		if ((node_mgr_obj->chnl_map == NULL)
		    || (node_mgr_obj->dma_chnl_map == NULL)
		    || (node_mgr_obj->zc_chnl_map == NULL)) {
			status = -ENOMEM;
		} else {
			/* Block out reserved channels */
			for (i = 0; i < node_mgr_obj->ul_chnl_offset; i++)
				gb_set(node_mgr_obj->chnl_map, i);

			/* Block out channels reserved for RMS */
			gb_set(node_mgr_obj->chnl_map,
			       node_mgr_obj->ul_chnl_offset);
			gb_set(node_mgr_obj->chnl_map,
			       node_mgr_obj->ul_chnl_offset + 1);
		}
	}
	if (!status) {
		/* NO RM Server on the IVA */
		if (dev_type != IVA_UNIT) {
			/* Get addresses of any RMS functions loaded */
			status = get_rms_fxns(node_mgr_obj);
		}
	}

	/* Get loader functions and create loader */
	if (!status)
		node_mgr_obj->nldr_fxns = nldr_fxns;	/* Dyn loader funcs */

	if (!status) {
		nldr_attrs_obj.pfn_ovly = ovly;
		nldr_attrs_obj.pfn_write = mem_write;
		nldr_attrs_obj.us_dsp_word_size = node_mgr_obj->udsp_word_size;
		nldr_attrs_obj.us_dsp_mau_size = node_mgr_obj->udsp_mau_size;
		node_mgr_obj->loader_init = node_mgr_obj->nldr_fxns.pfn_init();
		status =
		    node_mgr_obj->nldr_fxns.pfn_create(&node_mgr_obj->nldr_obj,
						       hdev_obj,
						       &nldr_attrs_obj);
	}
	if (!status)
		*node_man = node_mgr_obj;
	else
		delete_node_mgr(node_mgr_obj);

	DBC_ENSURE((status && *node_man == NULL) || (!status && *node_man));

	return status;
}

/*
 *  ======== node_delete ========
 *  Purpose:
 *      Delete a node on the DSP by remotely calling the node's delete function.
 *      Loads the node's delete function if necessary. Free GPP side resources
 *      after node's delete function returns.
 */
int node_delete(struct node_res_object *noderes,
		       struct process_context *pr_ctxt)
{
	struct node_object *pnode = noderes->hnode;
	struct node_mgr *hnode_mgr;
	struct proc_object *hprocessor;
	struct disp_object *disp_obj;
	u32 ul_delete_fxn;
	enum node_type node_type;
	enum node_state state;
	int status = 0;
	int status1 = 0;
	struct dsp_cbdata cb_data;
	u32 proc_id;
	struct bridge_drv_interface *intf_fxns;

	void *node_res = noderes;

	struct dsp_processorstate proc_state;
	DBC_REQUIRE(refs > 0);

	if (!pnode) {
		status = -EFAULT;
		goto func_end;
	}
	/* create struct dsp_cbdata struct for PWR call */
	cb_data.cb_data = PWR_TIMEOUT;
	hnode_mgr = pnode->hnode_mgr;
	hprocessor = pnode->hprocessor;
	disp_obj = hnode_mgr->disp_obj;
	node_type = node_get_type(pnode);
	intf_fxns = hnode_mgr->intf_fxns;
	/* Enter critical section */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	state = node_get_state(pnode);
	/*  Execute delete phase code for non-device node in all cases
	 *  except when the node was only allocated. Delete phase must be
	 *  executed even if create phase was executed, but failed.
	 *  If the node environment pointer is non-NULL, the delete phase
	 *  code must be  executed. */
	if (!(state == NODE_ALLOCATED && pnode->node_env == (u32) NULL) &&
	    node_type != NODE_DEVICE) {
		status = proc_get_processor_id(pnode->hprocessor, &proc_id);
		if (status)
			goto func_cont1;

		if (proc_id == DSP_UNIT || proc_id == IVA_UNIT) {
			/*  If node has terminated, execute phase code will
			 *  have already been unloaded in node_on_exit(). If the
			 *  node is PAUSED, the execute phase is loaded, and it
			 *  is now ok to unload it. If the node is running, we
			 *  will unload the execute phase only after deleting
			 *  the node. */
			if (state == NODE_PAUSED && pnode->loaded &&
			    pnode->phase_split) {
				/* Ok to unload execute code as long as node
				 * is not * running */
				status1 =
				    hnode_mgr->nldr_fxns.
				    pfn_unload(pnode->nldr_node_obj,
					       NLDR_EXECUTE);
				pnode->loaded = false;
				NODE_SET_STATE(pnode, NODE_DONE);
			}
			/* Load delete phase code if not loaded or if haven't
			 * * unloaded EXECUTE phase */
			if ((!(pnode->loaded) || (state == NODE_RUNNING)) &&
			    pnode->phase_split) {
				status =
				    hnode_mgr->nldr_fxns.
				    pfn_load(pnode->nldr_node_obj, NLDR_DELETE);
				if (!status)
					pnode->loaded = true;
				else
					pr_err("%s: fail - load delete code:"
					       " 0x%x\n", __func__, status);
			}
		}
func_cont1:
		if (!status) {
			/* Unblock a thread trying to terminate the node */
			(void)sync_set_event(pnode->sync_done);
			if (proc_id == DSP_UNIT) {
				/* ul_delete_fxn = address of node's delete
				 * function */
				status = get_fxn_address(pnode, &ul_delete_fxn,
							 DELETEPHASE);
			} else if (proc_id == IVA_UNIT)
				ul_delete_fxn = (u32) pnode->node_env;
			if (!status) {
				status = proc_get_state(hprocessor,
						&proc_state,
						sizeof(struct
						       dsp_processorstate));
				if (proc_state.proc_state != PROC_ERROR) {
					status =
					    disp_node_delete(disp_obj, pnode,
							     hnode_mgr->
							     ul_fxn_addrs
							     [RMSDELETENODE],
							     ul_delete_fxn,
							     pnode->node_env);
				} else
					NODE_SET_STATE(pnode, NODE_DONE);

				/* Unload execute, if not unloaded, and delete
				 * function */
				if (state == NODE_RUNNING &&
				    pnode->phase_split) {
					status1 =
					    hnode_mgr->nldr_fxns.
					    pfn_unload(pnode->nldr_node_obj,
						       NLDR_EXECUTE);
				}
				if (status1)
					pr_err("%s: fail - unload execute code:"
					       " 0x%x\n", __func__, status1);

				status1 =
				    hnode_mgr->nldr_fxns.pfn_unload(pnode->
							    nldr_node_obj,
							    NLDR_DELETE);
				pnode->loaded = false;
				if (status1)
					pr_err("%s: fail - unload delete code: "
					       "0x%x\n", __func__, status1);
			}
		}
	}
	/* Free host side resources even if a failure occurred */
	/* Remove node from hnode_mgr->node_list */
	lst_remove_elem(hnode_mgr->node_list, (struct list_head *)pnode);
	hnode_mgr->num_nodes--;
	/* Decrement count of nodes created on DSP */
	if ((state != NODE_ALLOCATED) || ((state == NODE_ALLOCATED) &&
					  (pnode->node_env != (u32) NULL)))
		hnode_mgr->num_created--;
	/*  Free host-side resources allocated by node_create()
	 *  delete_node() fails if SM buffers not freed by client! */
	drv_proc_node_update_status(node_res, false);
	delete_node(pnode, pr_ctxt);

	/*
	 * Release all Node resources and its context
	 */
	idr_remove(pr_ctxt->node_id, ((struct node_res_object *)node_res)->id);
	kfree(node_res);

	/* Exit critical section */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
	proc_notify_clients(hprocessor, DSP_NODESTATECHANGE);
func_end:
	dev_dbg(bridge, "%s: pnode: %p status 0x%x\n", __func__, pnode, status);
	return status;
}

/*
 *  ======== node_delete_mgr ========
 *  Purpose:
 *      Delete the NODE Manager.
 */
int node_delete_mgr(struct node_mgr *hnode_mgr)
{
	int status = 0;

	DBC_REQUIRE(refs > 0);

	if (hnode_mgr)
		delete_node_mgr(hnode_mgr);
	else
		status = -EFAULT;

	return status;
}

/*
 *  ======== node_enum_nodes ========
 *  Purpose:
 *      Enumerate currently allocated nodes.
 */
int node_enum_nodes(struct node_mgr *hnode_mgr, void **node_tab,
			   u32 node_tab_size, u32 *pu_num_nodes,
			   u32 *pu_allocated)
{
	struct node_object *hnode;
	u32 i;
	int status = 0;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(node_tab != NULL || node_tab_size == 0);
	DBC_REQUIRE(pu_num_nodes != NULL);
	DBC_REQUIRE(pu_allocated != NULL);

	if (!hnode_mgr) {
		status = -EFAULT;
		goto func_end;
	}
	/* Enter critical section */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	if (hnode_mgr->num_nodes > node_tab_size) {
		*pu_allocated = hnode_mgr->num_nodes;
		*pu_num_nodes = 0;
		status = -EINVAL;
	} else {
		hnode = (struct node_object *)lst_first(hnode_mgr->
			node_list);
		for (i = 0; i < hnode_mgr->num_nodes; i++) {
			DBC_ASSERT(hnode);
			node_tab[i] = hnode;
			hnode = (struct node_object *)lst_next
				(hnode_mgr->node_list,
				(struct list_head *)hnode);
		}
		*pu_allocated = *pu_num_nodes = hnode_mgr->num_nodes;
	}
	/* end of sync_enter_cs */
	/* Exit critical section */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
func_end:
	return status;
}

/*
 *  ======== node_exit ========
 *  Purpose:
 *      Discontinue usage of NODE module.
 */
void node_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== node_free_msg_buf ========
 *  Purpose:
 *      Frees the message buffer.
 */
int node_free_msg_buf(struct node_object *hnode, u8 * pbuffer,
			     struct dsp_bufferattr *pattr)
{
	struct node_object *pnode = (struct node_object *)hnode;
	int status = 0;
	u32 proc_id;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pbuffer != NULL);
	DBC_REQUIRE(pnode != NULL);
	DBC_REQUIRE(pnode->xlator != NULL);

	if (!hnode) {
		status = -EFAULT;
		goto func_end;
	}
	status = proc_get_processor_id(pnode->hprocessor, &proc_id);
	if (proc_id == DSP_UNIT) {
		if (!status) {
			if (pattr == NULL) {
				/* set defaults */
				pattr = &node_dfltbufattrs;
			}
			/* Node supports single SM segment only */
			if (pattr->segment_id != 1)
				status = -EBADR;

			/* pbuffer is clients Va. */
			status = cmm_xlator_free_buf(pnode->xlator, pbuffer);
		}
	} else {
		DBC_ASSERT(NULL);	/* BUG */
	}
func_end:
	return status;
}

/*
 *  ======== node_get_attr ========
 *  Purpose:
 *      Copy the current attributes of the specified node into a dsp_nodeattr
 *      structure.
 */
int node_get_attr(struct node_object *hnode,
			 struct dsp_nodeattr *pattr, u32 attr_size)
{
	struct node_mgr *hnode_mgr;
	int status = 0;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pattr != NULL);
	DBC_REQUIRE(attr_size >= sizeof(struct dsp_nodeattr));

	if (!hnode) {
		status = -EFAULT;
	} else {
		hnode_mgr = hnode->hnode_mgr;
		/* Enter hnode_mgr critical section (since we're accessing
		 * data that could be changed by node_change_priority() and
		 * node_connect(). */
		mutex_lock(&hnode_mgr->node_mgr_lock);
		pattr->cb_struct = sizeof(struct dsp_nodeattr);
		/* dsp_nodeattrin */
		pattr->in_node_attr_in.cb_struct =
				 sizeof(struct dsp_nodeattrin);
		pattr->in_node_attr_in.prio = hnode->prio;
		pattr->in_node_attr_in.utimeout = hnode->utimeout;
		pattr->in_node_attr_in.heap_size =
			hnode->create_args.asa.task_arg_obj.heap_size;
		pattr->in_node_attr_in.pgpp_virt_addr = (void *)
			hnode->create_args.asa.task_arg_obj.ugpp_heap_addr;
		pattr->node_attr_inputs = hnode->num_gpp_inputs;
		pattr->node_attr_outputs = hnode->num_gpp_outputs;
		/* dsp_nodeinfo */
		get_node_info(hnode, &(pattr->node_info));
		/* end of sync_enter_cs */
		/* Exit critical section */
		mutex_unlock(&hnode_mgr->node_mgr_lock);
	}
	return status;
}

/*
 *  ======== node_get_channel_id ========
 *  Purpose:
 *      Get the channel index reserved for a stream connection between the
 *      host and a node.
 */
int node_get_channel_id(struct node_object *hnode, u32 dir, u32 index,
			       u32 *chan_id)
{
	enum node_type node_type;
	int status = -EINVAL;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(dir == DSP_TONODE || dir == DSP_FROMNODE);
	DBC_REQUIRE(chan_id != NULL);

	if (!hnode) {
		status = -EFAULT;
		return status;
	}
	node_type = node_get_type(hnode);
	if (node_type != NODE_TASK && node_type != NODE_DAISSOCKET) {
		status = -EPERM;
		return status;
	}
	if (dir == DSP_TONODE) {
		if (index < MAX_INPUTS(hnode)) {
			if (hnode->inputs[index].type == HOSTCONNECT) {
				*chan_id = hnode->inputs[index].dev_id;
				status = 0;
			}
		}
	} else {
		DBC_ASSERT(dir == DSP_FROMNODE);
		if (index < MAX_OUTPUTS(hnode)) {
			if (hnode->outputs[index].type == HOSTCONNECT) {
				*chan_id = hnode->outputs[index].dev_id;
				status = 0;
			}
		}
	}
	return status;
}

/*
 *  ======== node_get_message ========
 *  Purpose:
 *      Retrieve a message from a node on the DSP.
 */
int node_get_message(struct node_object *hnode,
			    struct dsp_msg *message, u32 utimeout)
{
	struct node_mgr *hnode_mgr;
	enum node_type node_type;
	struct bridge_drv_interface *intf_fxns;
	int status = 0;
	void *tmp_buf;
	struct dsp_processorstate proc_state;
	struct proc_object *hprocessor;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(message != NULL);

	if (!hnode) {
		status = -EFAULT;
		goto func_end;
	}
	hprocessor = hnode->hprocessor;
	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in error state then don't attempt to get the
	   message */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}
	hnode_mgr = hnode->hnode_mgr;
	node_type = node_get_type(hnode);
	if (node_type != NODE_MESSAGE && node_type != NODE_TASK &&
	    node_type != NODE_DAISSOCKET) {
		status = -EPERM;
		goto func_end;
	}
	/*  This function will block unless a message is available. Since
	 *  DSPNode_RegisterNotify() allows notification when a message
	 *  is available, the system can be designed so that
	 *  DSPNode_GetMessage() is only called when a message is
	 *  available. */
	intf_fxns = hnode_mgr->intf_fxns;
	status =
	    (*intf_fxns->pfn_msg_get) (hnode->msg_queue_obj, message, utimeout);
	/* Check if message contains SM descriptor */
	if (status || !(message->dw_cmd & DSP_RMSBUFDESC))
		goto func_end;

	/* Translate DSP byte addr to GPP Va. */
	tmp_buf = cmm_xlator_translate(hnode->xlator,
				       (void *)(message->dw_arg1 *
						hnode->hnode_mgr->
						udsp_word_size), CMM_DSPPA2PA);
	if (tmp_buf != NULL) {
		/* now convert this GPP Pa to Va */
		tmp_buf = cmm_xlator_translate(hnode->xlator, tmp_buf,
					       CMM_PA2VA);
		if (tmp_buf != NULL) {
			/* Adjust SM size in msg */
			message->dw_arg1 = (u32) tmp_buf;
			message->dw_arg2 *= hnode->hnode_mgr->udsp_word_size;
		} else {
			status = -ESRCH;
		}
	} else {
		status = -ESRCH;
	}
func_end:
	dev_dbg(bridge, "%s: hnode: %p message: %p utimeout: 0x%x\n", __func__,
		hnode, message, utimeout);
	return status;
}

/*
 *   ======== node_get_nldr_obj ========
 */
int node_get_nldr_obj(struct node_mgr *hnode_mgr,
			     struct nldr_object **nldr_ovlyobj)
{
	int status = 0;
	struct node_mgr *node_mgr_obj = hnode_mgr;
	DBC_REQUIRE(nldr_ovlyobj != NULL);

	if (!hnode_mgr)
		status = -EFAULT;
	else
		*nldr_ovlyobj = node_mgr_obj->nldr_obj;

	DBC_ENSURE(!status || (nldr_ovlyobj != NULL && *nldr_ovlyobj == NULL));
	return status;
}

/*
 *  ======== node_get_strm_mgr ========
 *  Purpose:
 *      Returns the Stream manager.
 */
int node_get_strm_mgr(struct node_object *hnode,
			     struct strm_mgr **strm_man)
{
	int status = 0;

	DBC_REQUIRE(refs > 0);

	if (!hnode)
		status = -EFAULT;
	else
		*strm_man = hnode->hnode_mgr->strm_mgr_obj;

	return status;
}

/*
 *  ======== node_get_load_type ========
 */
enum nldr_loadtype node_get_load_type(struct node_object *hnode)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hnode);
	if (!hnode) {
		dev_dbg(bridge, "%s: Failed. hnode: %p\n", __func__, hnode);
		return -1;
	} else {
		return hnode->dcd_props.obj_data.node_obj.us_load_type;
	}
}

/*
 *  ======== node_get_timeout ========
 *  Purpose:
 *      Returns the timeout value for this node.
 */
u32 node_get_timeout(struct node_object *hnode)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hnode);
	if (!hnode) {
		dev_dbg(bridge, "%s: failed. hnode: %p\n", __func__, hnode);
		return 0;
	} else {
		return hnode->utimeout;
	}
}

/*
 *  ======== node_get_type ========
 *  Purpose:
 *      Returns the node type.
 */
enum node_type node_get_type(struct node_object *hnode)
{
	enum node_type node_type;

	if (hnode == (struct node_object *)DSP_HGPPNODE)
		node_type = NODE_GPP;
	else {
		if (!hnode)
			node_type = -1;
		else
			node_type = hnode->ntype;
	}
	return node_type;
}

/*
 *  ======== node_init ========
 *  Purpose:
 *      Initialize the NODE module.
 */
bool node_init(void)
{
	DBC_REQUIRE(refs >= 0);

	refs++;

	return true;
}

/*
 *  ======== node_on_exit ========
 *  Purpose:
 *      Gets called when RMS_EXIT is received for a node.
 */
void node_on_exit(struct node_object *hnode, s32 node_status)
{
	if (!hnode)
		return;

	/* Set node state to done */
	NODE_SET_STATE(hnode, NODE_DONE);
	hnode->exit_status = node_status;
	if (hnode->loaded && hnode->phase_split) {
		(void)hnode->hnode_mgr->nldr_fxns.pfn_unload(hnode->
							     nldr_node_obj,
							     NLDR_EXECUTE);
		hnode->loaded = false;
	}
	/* Unblock call to node_terminate */
	(void)sync_set_event(hnode->sync_done);
	/* Notify clients */
	proc_notify_clients(hnode->hprocessor, DSP_NODESTATECHANGE);
	ntfy_notify(hnode->ntfy_obj, DSP_NODESTATECHANGE);
}

/*
 *  ======== node_pause ========
 *  Purpose:
 *      Suspend execution of a node currently running on the DSP.
 */
int node_pause(struct node_object *hnode)
{
	struct node_object *pnode = (struct node_object *)hnode;
	enum node_type node_type;
	enum node_state state;
	struct node_mgr *hnode_mgr;
	int status = 0;
	u32 proc_id;
	struct dsp_processorstate proc_state;
	struct proc_object *hprocessor;

	DBC_REQUIRE(refs > 0);

	if (!hnode) {
		status = -EFAULT;
	} else {
		node_type = node_get_type(hnode);
		if (node_type != NODE_TASK && node_type != NODE_DAISSOCKET)
			status = -EPERM;
	}
	if (status)
		goto func_end;

	status = proc_get_processor_id(pnode->hprocessor, &proc_id);

	if (proc_id == IVA_UNIT)
		status = -ENOSYS;

	if (!status) {
		hnode_mgr = hnode->hnode_mgr;

		/* Enter critical section */
		mutex_lock(&hnode_mgr->node_mgr_lock);
		state = node_get_state(hnode);
		/* Check node state */
		if (state != NODE_RUNNING)
			status = -EBADR;

		if (status)
			goto func_cont;
		hprocessor = hnode->hprocessor;
		status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
		if (status)
			goto func_cont;
		/* If processor is in error state then don't attempt
		   to send the message */
		if (proc_state.proc_state == PROC_ERROR) {
			status = -EPERM;
			goto func_cont;
		}

		status = disp_node_change_priority(hnode_mgr->disp_obj, hnode,
			hnode_mgr->ul_fxn_addrs[RMSCHANGENODEPRIORITY],
			hnode->node_env, NODE_SUSPENDEDPRI);

		/* Update state */
		if (status >= 0)
			NODE_SET_STATE(hnode, NODE_PAUSED);

func_cont:
		/* End of sync_enter_cs */
		/* Leave critical section */
		mutex_unlock(&hnode_mgr->node_mgr_lock);
		if (status >= 0) {
			proc_notify_clients(hnode->hprocessor,
					    DSP_NODESTATECHANGE);
			ntfy_notify(hnode->ntfy_obj, DSP_NODESTATECHANGE);
		}
	}
func_end:
	dev_dbg(bridge, "%s: hnode: %p status 0x%x\n", __func__, hnode, status);
	return status;
}

/*
 *  ======== node_put_message ========
 *  Purpose:
 *      Send a message to a message node, task node, or XDAIS socket node. This
 *      function will block until the message stream can accommodate the
 *      message, or a timeout occurs.
 */
int node_put_message(struct node_object *hnode,
			    const struct dsp_msg *pmsg, u32 utimeout)
{
	struct node_mgr *hnode_mgr = NULL;
	enum node_type node_type;
	struct bridge_drv_interface *intf_fxns;
	enum node_state state;
	int status = 0;
	void *tmp_buf;
	struct dsp_msg new_msg;
	struct dsp_processorstate proc_state;
	struct proc_object *hprocessor;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pmsg != NULL);

	if (!hnode) {
		status = -EFAULT;
		goto func_end;
	}
	hprocessor = hnode->hprocessor;
	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in bad state then don't attempt sending the
	   message */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}
	hnode_mgr = hnode->hnode_mgr;
	node_type = node_get_type(hnode);
	if (node_type != NODE_MESSAGE && node_type != NODE_TASK &&
	    node_type != NODE_DAISSOCKET)
		status = -EPERM;

	if (!status) {
		/*  Check node state. Can't send messages to a node after
		 *  we've sent the RMS_EXIT command. There is still the
		 *  possibility that node_terminate can be called after we've
		 *  checked the state. Could add another SYNC object to
		 *  prevent this (can't use node_mgr_lock, since we don't
		 *  want to block other NODE functions). However, the node may
		 *  still exit on its own, before this message is sent. */
		mutex_lock(&hnode_mgr->node_mgr_lock);
		state = node_get_state(hnode);
		if (state == NODE_TERMINATING || state == NODE_DONE)
			status = -EBADR;

		/* end of sync_enter_cs */
		mutex_unlock(&hnode_mgr->node_mgr_lock);
	}
	if (status)
		goto func_end;

	/* assign pmsg values to new msg */
	new_msg = *pmsg;
	/* Now, check if message contains a SM buffer descriptor */
	if (pmsg->dw_cmd & DSP_RMSBUFDESC) {
		/* Translate GPP Va to DSP physical buf Ptr. */
		tmp_buf = cmm_xlator_translate(hnode->xlator,
					       (void *)new_msg.dw_arg1,
					       CMM_VA2DSPPA);
		if (tmp_buf != NULL) {
			/* got translation, convert to MAUs in msg */
			if (hnode->hnode_mgr->udsp_word_size != 0) {
				new_msg.dw_arg1 =
				    (u32) tmp_buf /
				    hnode->hnode_mgr->udsp_word_size;
				/* MAUs */
				new_msg.dw_arg2 /= hnode->hnode_mgr->
				    udsp_word_size;
			} else {
				pr_err("%s: udsp_word_size is zero!\n",
				       __func__);
				status = -EPERM;	/* bad DSPWordSize */
			}
		} else {	/* failed to translate buffer address */
			status = -ESRCH;
		}
	}
	if (!status) {
		intf_fxns = hnode_mgr->intf_fxns;
		status = (*intf_fxns->pfn_msg_put) (hnode->msg_queue_obj,
						    &new_msg, utimeout);
	}
func_end:
	dev_dbg(bridge, "%s: hnode: %p pmsg: %p utimeout: 0x%x, "
		"status 0x%x\n", __func__, hnode, pmsg, utimeout, status);
	return status;
}

/*
 *  ======== node_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this node.
 */
int node_register_notify(struct node_object *hnode, u32 event_mask,
				u32 notify_type,
				struct dsp_notification *hnotification)
{
	struct bridge_drv_interface *intf_fxns;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hnotification != NULL);

	if (!hnode) {
		status = -EFAULT;
	} else {
		/* Check if event mask is a valid node related event */
		if (event_mask & ~(DSP_NODESTATECHANGE | DSP_NODEMESSAGEREADY))
			status = -EINVAL;

		/* Check if notify type is valid */
		if (notify_type != DSP_SIGNALEVENT)
			status = -EINVAL;

		/* Only one Notification can be registered at a
		 * time - Limitation */
		if (event_mask == (DSP_NODESTATECHANGE | DSP_NODEMESSAGEREADY))
			status = -EINVAL;
	}
	if (!status) {
		if (event_mask == DSP_NODESTATECHANGE) {
			status = ntfy_register(hnode->ntfy_obj, hnotification,
					       event_mask & DSP_NODESTATECHANGE,
					       notify_type);
		} else {
			/* Send Message part of event mask to msg_ctrl */
			intf_fxns = hnode->hnode_mgr->intf_fxns;
			status = (*intf_fxns->pfn_msg_register_notify)
			    (hnode->msg_queue_obj,
			     event_mask & DSP_NODEMESSAGEREADY, notify_type,
			     hnotification);
		}

	}
	dev_dbg(bridge, "%s: hnode: %p event_mask: 0x%x notify_type: 0x%x "
		"hnotification: %p status 0x%x\n", __func__, hnode,
		event_mask, notify_type, hnotification, status);
	return status;
}

/*
 *  ======== node_run ========
 *  Purpose:
 *      Start execution of a node's execute phase, or resume execution of a node
 *      that has been suspended (via NODE_NodePause()) on the DSP. Load the
 *      node's execute function if necessary.
 */
int node_run(struct node_object *hnode)
{
	struct node_object *pnode = (struct node_object *)hnode;
	struct node_mgr *hnode_mgr;
	enum node_type node_type;
	enum node_state state;
	u32 ul_execute_fxn;
	u32 ul_fxn_addr;
	int status = 0;
	u32 proc_id;
	struct bridge_drv_interface *intf_fxns;
	struct dsp_processorstate proc_state;
	struct proc_object *hprocessor;

	DBC_REQUIRE(refs > 0);

	if (!hnode) {
		status = -EFAULT;
		goto func_end;
	}
	hprocessor = hnode->hprocessor;
	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in error state then don't attempt to run the node */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}
	node_type = node_get_type(hnode);
	if (node_type == NODE_DEVICE)
		status = -EPERM;
	if (status)
		goto func_end;

	hnode_mgr = hnode->hnode_mgr;
	if (!hnode_mgr) {
		status = -EFAULT;
		goto func_end;
	}
	intf_fxns = hnode_mgr->intf_fxns;
	/* Enter critical section */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	state = node_get_state(hnode);
	if (state != NODE_CREATED && state != NODE_PAUSED)
		status = -EBADR;

	if (!status)
		status = proc_get_processor_id(pnode->hprocessor, &proc_id);

	if (status)
		goto func_cont1;

	if ((proc_id != DSP_UNIT) && (proc_id != IVA_UNIT))
		goto func_cont1;

	if (state == NODE_CREATED) {
		/* If node's execute function is not loaded, load it */
		if (!(hnode->loaded) && hnode->phase_split) {
			status =
			    hnode_mgr->nldr_fxns.pfn_load(hnode->nldr_node_obj,
							  NLDR_EXECUTE);
			if (!status) {
				hnode->loaded = true;
			} else {
				pr_err("%s: fail - load execute code: 0x%x\n",
				       __func__, status);
			}
		}
		if (!status) {
			/* Get address of node's execute function */
			if (proc_id == IVA_UNIT)
				ul_execute_fxn = (u32) hnode->node_env;
			else {
				status = get_fxn_address(hnode, &ul_execute_fxn,
							 EXECUTEPHASE);
			}
		}
		if (!status) {
			ul_fxn_addr = hnode_mgr->ul_fxn_addrs[RMSEXECUTENODE];
			status =
			    disp_node_run(hnode_mgr->disp_obj, hnode,
					  ul_fxn_addr, ul_execute_fxn,
					  hnode->node_env);
		}
	} else if (state == NODE_PAUSED) {
		ul_fxn_addr = hnode_mgr->ul_fxn_addrs[RMSCHANGENODEPRIORITY];
		status = disp_node_change_priority(hnode_mgr->disp_obj, hnode,
						   ul_fxn_addr, hnode->node_env,
						   NODE_GET_PRIORITY(hnode));
	} else {
		/* We should never get here */
		DBC_ASSERT(false);
	}
func_cont1:
	/* Update node state. */
	if (status >= 0)
		NODE_SET_STATE(hnode, NODE_RUNNING);
	else			/* Set state back to previous value */
		NODE_SET_STATE(hnode, state);
	/*End of sync_enter_cs */
	/* Exit critical section */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
	if (status >= 0) {
		proc_notify_clients(hnode->hprocessor, DSP_NODESTATECHANGE);
		ntfy_notify(hnode->ntfy_obj, DSP_NODESTATECHANGE);
	}
func_end:
	dev_dbg(bridge, "%s: hnode: %p status 0x%x\n", __func__, hnode, status);
	return status;
}

/*
 *  ======== node_terminate ========
 *  Purpose:
 *      Signal a node running on the DSP that it should exit its execute phase
 *      function.
 */
int node_terminate(struct node_object *hnode, int *pstatus)
{
	struct node_object *pnode = (struct node_object *)hnode;
	struct node_mgr *hnode_mgr = NULL;
	enum node_type node_type;
	struct bridge_drv_interface *intf_fxns;
	enum node_state state;
	struct dsp_msg msg, killmsg;
	int status = 0;
	u32 proc_id, kill_time_out;
	struct deh_mgr *hdeh_mgr;
	struct dsp_processorstate proc_state;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pstatus != NULL);

	if (!hnode || !hnode->hnode_mgr) {
		status = -EFAULT;
		goto func_end;
	}
	if (pnode->hprocessor == NULL) {
		status = -EFAULT;
		goto func_end;
	}
	status = proc_get_processor_id(pnode->hprocessor, &proc_id);

	if (!status) {
		hnode_mgr = hnode->hnode_mgr;
		node_type = node_get_type(hnode);
		if (node_type != NODE_TASK && node_type != NODE_DAISSOCKET)
			status = -EPERM;
	}
	if (!status) {
		/* Check node state */
		mutex_lock(&hnode_mgr->node_mgr_lock);
		state = node_get_state(hnode);
		if (state != NODE_RUNNING) {
			status = -EBADR;
			/* Set the exit status if node terminated on
			 * its own. */
			if (state == NODE_DONE)
				*pstatus = hnode->exit_status;

		} else {
			NODE_SET_STATE(hnode, NODE_TERMINATING);
		}
		/* end of sync_enter_cs */
		mutex_unlock(&hnode_mgr->node_mgr_lock);
	}
	if (!status) {
		/*
		 *  Send exit message. Do not change state to NODE_DONE
		 *  here. That will be done in callback.
		 */
		status = proc_get_state(pnode->hprocessor, &proc_state,
					sizeof(struct dsp_processorstate));
		if (status)
			goto func_cont;
		/* If processor is in error state then don't attempt to send
		 * A kill task command */
		if (proc_state.proc_state == PROC_ERROR) {
			status = -EPERM;
			goto func_cont;
		}

		msg.dw_cmd = RMS_EXIT;
		msg.dw_arg1 = hnode->node_env;
		killmsg.dw_cmd = RMS_KILLTASK;
		killmsg.dw_arg1 = hnode->node_env;
		intf_fxns = hnode_mgr->intf_fxns;

		if (hnode->utimeout > MAXTIMEOUT)
			kill_time_out = MAXTIMEOUT;
		else
			kill_time_out = (hnode->utimeout) * 2;

		status = (*intf_fxns->pfn_msg_put) (hnode->msg_queue_obj, &msg,
						    hnode->utimeout);
		if (status)
			goto func_cont;

		/*
		 * Wait on synchronization object that will be
		 * posted in the callback on receiving RMS_EXIT
		 * message, or by node_delete. Check for valid hnode,
		 * in case posted by node_delete().
		 */
		status = sync_wait_on_event(hnode->sync_done,
					    kill_time_out / 2);
		if (status != ETIME)
			goto func_cont;

		status = (*intf_fxns->pfn_msg_put)(hnode->msg_queue_obj,
						&killmsg, hnode->utimeout);
		if (status)
			goto func_cont;
		status = sync_wait_on_event(hnode->sync_done,
					     kill_time_out / 2);
		if (status) {
			/*
			 * Here it goes the part of the simulation of
			 * the DSP exception.
			 */
			dev_get_deh_mgr(hnode_mgr->hdev_obj, &hdeh_mgr);
			if (!hdeh_mgr)
				goto func_cont;

			bridge_deh_notify(hdeh_mgr, DSP_SYSERROR, DSP_EXCEPTIONABORT);
		}
	}
func_cont:
	if (!status) {
		/* Enter CS before getting exit status, in case node was
		 * deleted. */
		mutex_lock(&hnode_mgr->node_mgr_lock);
		/* Make sure node wasn't deleted while we blocked */
		if (!hnode) {
			status = -EPERM;
		} else {
			*pstatus = hnode->exit_status;
			dev_dbg(bridge, "%s: hnode: %p env 0x%x status 0x%x\n",
				__func__, hnode, hnode->node_env, status);
		}
		mutex_unlock(&hnode_mgr->node_mgr_lock);
	}			/*End of sync_enter_cs */
func_end:
	return status;
}

/*
 *  ======== delete_node ========
 *  Purpose:
 *      Free GPP resources allocated in node_allocate() or node_connect().
 */
static void delete_node(struct node_object *hnode,
			struct process_context *pr_ctxt)
{
	struct node_mgr *hnode_mgr;
	struct bridge_drv_interface *intf_fxns;
	u32 i;
	enum node_type node_type;
	struct stream_chnl stream;
	struct node_msgargs node_msg_args;
	struct node_taskargs task_arg_obj;

	int status;
	if (!hnode)
		goto func_end;
	hnode_mgr = hnode->hnode_mgr;
	if (!hnode_mgr)
		goto func_end;

	node_type = node_get_type(hnode);
	if (node_type != NODE_DEVICE) {
		node_msg_args = hnode->create_args.asa.node_msg_args;
		kfree(node_msg_args.pdata);

		/* Free msg_ctrl queue */
		if (hnode->msg_queue_obj) {
			intf_fxns = hnode_mgr->intf_fxns;
			(*intf_fxns->pfn_msg_delete_queue) (hnode->
							    msg_queue_obj);
			hnode->msg_queue_obj = NULL;
		}

		kfree(hnode->sync_done);

		/* Free all stream info */
		if (hnode->inputs) {
			for (i = 0; i < MAX_INPUTS(hnode); i++) {
				stream = hnode->inputs[i];
				free_stream(hnode_mgr, stream);
			}
			kfree(hnode->inputs);
			hnode->inputs = NULL;
		}
		if (hnode->outputs) {
			for (i = 0; i < MAX_OUTPUTS(hnode); i++) {
				stream = hnode->outputs[i];
				free_stream(hnode_mgr, stream);
			}
			kfree(hnode->outputs);
			hnode->outputs = NULL;
		}
		task_arg_obj = hnode->create_args.asa.task_arg_obj;
		if (task_arg_obj.strm_in_def) {
			for (i = 0; i < MAX_INPUTS(hnode); i++) {
				kfree(task_arg_obj.strm_in_def[i].sz_device);
				task_arg_obj.strm_in_def[i].sz_device = NULL;
			}
			kfree(task_arg_obj.strm_in_def);
			task_arg_obj.strm_in_def = NULL;
		}
		if (task_arg_obj.strm_out_def) {
			for (i = 0; i < MAX_OUTPUTS(hnode); i++) {
				kfree(task_arg_obj.strm_out_def[i].sz_device);
				task_arg_obj.strm_out_def[i].sz_device = NULL;
			}
			kfree(task_arg_obj.strm_out_def);
			task_arg_obj.strm_out_def = NULL;
		}
		if (task_arg_obj.udsp_heap_res_addr) {
			status = proc_un_map(hnode->hprocessor, (void *)
					     task_arg_obj.udsp_heap_addr,
					     pr_ctxt);
		}
	}
	if (node_type != NODE_MESSAGE) {
		kfree(hnode->stream_connect);
		hnode->stream_connect = NULL;
	}
	kfree(hnode->pstr_dev_name);
	hnode->pstr_dev_name = NULL;

	if (hnode->ntfy_obj) {
		ntfy_delete(hnode->ntfy_obj);
		kfree(hnode->ntfy_obj);
		hnode->ntfy_obj = NULL;
	}

	/* These were allocated in dcd_get_object_def (via node_allocate) */
	kfree(hnode->dcd_props.obj_data.node_obj.pstr_create_phase_fxn);
	hnode->dcd_props.obj_data.node_obj.pstr_create_phase_fxn = NULL;

	kfree(hnode->dcd_props.obj_data.node_obj.pstr_execute_phase_fxn);
	hnode->dcd_props.obj_data.node_obj.pstr_execute_phase_fxn = NULL;

	kfree(hnode->dcd_props.obj_data.node_obj.pstr_delete_phase_fxn);
	hnode->dcd_props.obj_data.node_obj.pstr_delete_phase_fxn = NULL;

	kfree(hnode->dcd_props.obj_data.node_obj.pstr_i_alg_name);
	hnode->dcd_props.obj_data.node_obj.pstr_i_alg_name = NULL;

	/* Free all SM address translator resources */
	kfree(hnode->xlator);
	kfree(hnode->nldr_node_obj);
	hnode->nldr_node_obj = NULL;
	hnode->hnode_mgr = NULL;
	kfree(hnode);
	hnode = NULL;
func_end:
	return;
}

/*
 *  ======== delete_node_mgr ========
 *  Purpose:
 *      Frees the node manager.
 */
static void delete_node_mgr(struct node_mgr *hnode_mgr)
{
	struct node_object *hnode;

	if (hnode_mgr) {
		/* Free resources */
		if (hnode_mgr->hdcd_mgr)
			dcd_destroy_manager(hnode_mgr->hdcd_mgr);

		/* Remove any elements remaining in lists */
		if (hnode_mgr->node_list) {
			while ((hnode = (struct node_object *)
				lst_get_head(hnode_mgr->node_list)))
				delete_node(hnode, NULL);

			DBC_ASSERT(LST_IS_EMPTY(hnode_mgr->node_list));
			kfree(hnode_mgr->node_list);
		}
		mutex_destroy(&hnode_mgr->node_mgr_lock);
		if (hnode_mgr->ntfy_obj) {
			ntfy_delete(hnode_mgr->ntfy_obj);
			kfree(hnode_mgr->ntfy_obj);
		}

		if (hnode_mgr->pipe_map)
			gb_delete(hnode_mgr->pipe_map);

		if (hnode_mgr->pipe_done_map)
			gb_delete(hnode_mgr->pipe_done_map);

		if (hnode_mgr->chnl_map)
			gb_delete(hnode_mgr->chnl_map);

		if (hnode_mgr->dma_chnl_map)
			gb_delete(hnode_mgr->dma_chnl_map);

		if (hnode_mgr->zc_chnl_map)
			gb_delete(hnode_mgr->zc_chnl_map);

		if (hnode_mgr->disp_obj)
			disp_delete(hnode_mgr->disp_obj);

		if (hnode_mgr->strm_mgr_obj)
			strm_delete(hnode_mgr->strm_mgr_obj);

		/* Delete the loader */
		if (hnode_mgr->nldr_obj)
			hnode_mgr->nldr_fxns.pfn_delete(hnode_mgr->nldr_obj);

		if (hnode_mgr->loader_init)
			hnode_mgr->nldr_fxns.pfn_exit();

		kfree(hnode_mgr);
	}
}

/*
 *  ======== fill_stream_connect ========
 *  Purpose:
 *      Fills stream information.
 */
static void fill_stream_connect(struct node_object *node1,
				struct node_object *node2,
				u32 stream1, u32 stream2)
{
	u32 strm_index;
	struct dsp_streamconnect *strm1 = NULL;
	struct dsp_streamconnect *strm2 = NULL;
	enum node_type node1_type = NODE_TASK;
	enum node_type node2_type = NODE_TASK;

	node1_type = node_get_type(node1);
	node2_type = node_get_type(node2);
	if (node1 != (struct node_object *)DSP_HGPPNODE) {

		if (node1_type != NODE_DEVICE) {
			strm_index = node1->num_inputs +
			    node1->num_outputs - 1;
			strm1 = &(node1->stream_connect[strm_index]);
			strm1->cb_struct = sizeof(struct dsp_streamconnect);
			strm1->this_node_stream_index = stream1;
		}

		if (node2 != (struct node_object *)DSP_HGPPNODE) {
			/* NODE == > NODE */
			if (node1_type != NODE_DEVICE) {
				strm1->connected_node = node2;
				strm1->ui_connected_node_id = node2->node_uuid;
				strm1->connected_node_stream_index = stream2;
				strm1->connect_type = CONNECTTYPE_NODEOUTPUT;
			}
			if (node2_type != NODE_DEVICE) {
				strm_index = node2->num_inputs +
				    node2->num_outputs - 1;
				strm2 = &(node2->stream_connect[strm_index]);
				strm2->cb_struct =
				    sizeof(struct dsp_streamconnect);
				strm2->this_node_stream_index = stream2;
				strm2->connected_node = node1;
				strm2->ui_connected_node_id = node1->node_uuid;
				strm2->connected_node_stream_index = stream1;
				strm2->connect_type = CONNECTTYPE_NODEINPUT;
			}
		} else if (node1_type != NODE_DEVICE)
			strm1->connect_type = CONNECTTYPE_GPPOUTPUT;
	} else {
		/* GPP == > NODE */
		DBC_ASSERT(node2 != (struct node_object *)DSP_HGPPNODE);
		strm_index = node2->num_inputs + node2->num_outputs - 1;
		strm2 = &(node2->stream_connect[strm_index]);
		strm2->cb_struct = sizeof(struct dsp_streamconnect);
		strm2->this_node_stream_index = stream2;
		strm2->connect_type = CONNECTTYPE_GPPINPUT;
	}
}

/*
 *  ======== fill_stream_def ========
 *  Purpose:
 *      Fills Stream attributes.
 */
static void fill_stream_def(struct node_object *hnode,
			    struct node_strmdef *pstrm_def,
			    struct dsp_strmattr *pattrs)
{
	struct node_mgr *hnode_mgr = hnode->hnode_mgr;

	if (pattrs != NULL) {
		pstrm_def->num_bufs = pattrs->num_bufs;
		pstrm_def->buf_size =
		    pattrs->buf_size / hnode_mgr->udsp_data_mau_size;
		pstrm_def->seg_id = pattrs->seg_id;
		pstrm_def->buf_alignment = pattrs->buf_alignment;
		pstrm_def->utimeout = pattrs->utimeout;
	} else {
		pstrm_def->num_bufs = DEFAULTNBUFS;
		pstrm_def->buf_size =
		    DEFAULTBUFSIZE / hnode_mgr->udsp_data_mau_size;
		pstrm_def->seg_id = DEFAULTSEGID;
		pstrm_def->buf_alignment = DEFAULTALIGNMENT;
		pstrm_def->utimeout = DEFAULTTIMEOUT;
	}
}

/*
 *  ======== free_stream ========
 *  Purpose:
 *      Updates the channel mask and frees the pipe id.
 */
static void free_stream(struct node_mgr *hnode_mgr, struct stream_chnl stream)
{
	/* Free up the pipe id unless other node has not yet been deleted. */
	if (stream.type == NODECONNECT) {
		if (gb_test(hnode_mgr->pipe_done_map, stream.dev_id)) {
			/* The other node has already been deleted */
			gb_clear(hnode_mgr->pipe_done_map, stream.dev_id);
			gb_clear(hnode_mgr->pipe_map, stream.dev_id);
		} else {
			/* The other node has not been deleted yet */
			gb_set(hnode_mgr->pipe_done_map, stream.dev_id);
		}
	} else if (stream.type == HOSTCONNECT) {
		if (stream.dev_id < hnode_mgr->ul_num_chnls) {
			gb_clear(hnode_mgr->chnl_map, stream.dev_id);
		} else if (stream.dev_id < (2 * hnode_mgr->ul_num_chnls)) {
			/* dsp-dma */
			gb_clear(hnode_mgr->dma_chnl_map, stream.dev_id -
				 (1 * hnode_mgr->ul_num_chnls));
		} else if (stream.dev_id < (3 * hnode_mgr->ul_num_chnls)) {
			/* zero-copy */
			gb_clear(hnode_mgr->zc_chnl_map, stream.dev_id -
				 (2 * hnode_mgr->ul_num_chnls));
		}
	}
}

/*
 *  ======== get_fxn_address ========
 *  Purpose:
 *      Retrieves the address for create, execute or delete phase for a node.
 */
static int get_fxn_address(struct node_object *hnode, u32 * fxn_addr,
				  u32 phase)
{
	char *pstr_fxn_name = NULL;
	struct node_mgr *hnode_mgr = hnode->hnode_mgr;
	int status = 0;
	DBC_REQUIRE(node_get_type(hnode) == NODE_TASK ||
		    node_get_type(hnode) == NODE_DAISSOCKET ||
		    node_get_type(hnode) == NODE_MESSAGE);

	switch (phase) {
	case CREATEPHASE:
		pstr_fxn_name =
		    hnode->dcd_props.obj_data.node_obj.pstr_create_phase_fxn;
		break;
	case EXECUTEPHASE:
		pstr_fxn_name =
		    hnode->dcd_props.obj_data.node_obj.pstr_execute_phase_fxn;
		break;
	case DELETEPHASE:
		pstr_fxn_name =
		    hnode->dcd_props.obj_data.node_obj.pstr_delete_phase_fxn;
		break;
	default:
		/* Should never get here */
		DBC_ASSERT(false);
		break;
	}

	status =
	    hnode_mgr->nldr_fxns.pfn_get_fxn_addr(hnode->nldr_node_obj,
						  pstr_fxn_name, fxn_addr);

	return status;
}

/*
 *  ======== get_node_info ========
 *  Purpose:
 *      Retrieves the node information.
 */
void get_node_info(struct node_object *hnode, struct dsp_nodeinfo *node_info)
{
	u32 i;

	DBC_REQUIRE(hnode);
	DBC_REQUIRE(node_info != NULL);

	node_info->cb_struct = sizeof(struct dsp_nodeinfo);
	node_info->nb_node_database_props =
	    hnode->dcd_props.obj_data.node_obj.ndb_props;
	node_info->execution_priority = hnode->prio;
	node_info->device_owner = hnode->device_owner;
	node_info->number_streams = hnode->num_inputs + hnode->num_outputs;
	node_info->node_env = hnode->node_env;

	node_info->ns_execution_state = node_get_state(hnode);

	/* Copy stream connect data */
	for (i = 0; i < hnode->num_inputs + hnode->num_outputs; i++)
		node_info->sc_stream_connection[i] = hnode->stream_connect[i];

}

/*
 *  ======== get_node_props ========
 *  Purpose:
 *      Retrieve node properties.
 */
static int get_node_props(struct dcd_manager *hdcd_mgr,
				 struct node_object *hnode,
				 const struct dsp_uuid *node_uuid,
				 struct dcd_genericobj *dcd_prop)
{
	u32 len;
	struct node_msgargs *pmsg_args;
	struct node_taskargs *task_arg_obj;
	enum node_type node_type = NODE_TASK;
	struct dsp_ndbprops *pndb_props =
	    &(dcd_prop->obj_data.node_obj.ndb_props);
	int status = 0;
	char sz_uuid[MAXUUIDLEN];

	status = dcd_get_object_def(hdcd_mgr, (struct dsp_uuid *)node_uuid,
				    DSP_DCDNODETYPE, dcd_prop);

	if (!status) {
		hnode->ntype = node_type = pndb_props->ntype;

		/* Create UUID value to set in registry. */
		uuid_uuid_to_string((struct dsp_uuid *)node_uuid, sz_uuid,
				    MAXUUIDLEN);
		dev_dbg(bridge, "(node) UUID: %s\n", sz_uuid);

		/* Fill in message args that come from NDB */
		if (node_type != NODE_DEVICE) {
			pmsg_args = &(hnode->create_args.asa.node_msg_args);
			pmsg_args->seg_id =
			    dcd_prop->obj_data.node_obj.msg_segid;
			pmsg_args->notify_type =
			    dcd_prop->obj_data.node_obj.msg_notify_type;
			pmsg_args->max_msgs = pndb_props->message_depth;
			dev_dbg(bridge, "(node) Max Number of Messages: 0x%x\n",
				pmsg_args->max_msgs);
		} else {
			/* Copy device name */
			DBC_REQUIRE(pndb_props->ac_name);
			len = strlen(pndb_props->ac_name);
			DBC_ASSERT(len < MAXDEVNAMELEN);
			hnode->pstr_dev_name = kzalloc(len + 1, GFP_KERNEL);
			if (hnode->pstr_dev_name == NULL) {
				status = -ENOMEM;
			} else {
				strncpy(hnode->pstr_dev_name,
					pndb_props->ac_name, len);
			}
		}
	}
	if (!status) {
		/* Fill in create args that come from NDB */
		if (node_type == NODE_TASK || node_type == NODE_DAISSOCKET) {
			task_arg_obj = &(hnode->create_args.asa.task_arg_obj);
			task_arg_obj->prio = pndb_props->prio;
			task_arg_obj->stack_size = pndb_props->stack_size;
			task_arg_obj->sys_stack_size =
			    pndb_props->sys_stack_size;
			task_arg_obj->stack_seg = pndb_props->stack_seg;
			dev_dbg(bridge, "(node) Priority: 0x%x Stack Size: "
				"0x%x words System Stack Size: 0x%x words "
				"Stack Segment: 0x%x profile count : 0x%x\n",
				task_arg_obj->prio, task_arg_obj->stack_size,
				task_arg_obj->sys_stack_size,
				task_arg_obj->stack_seg,
				pndb_props->count_profiles);
		}
	}

	return status;
}

/*
 *  ======== get_proc_props ========
 *  Purpose:
 *      Retrieve the processor properties.
 */
static int get_proc_props(struct node_mgr *hnode_mgr,
				 struct dev_object *hdev_obj)
{
	struct cfg_hostres *host_res;
	struct bridge_dev_context *pbridge_context;
	int status = 0;

	status = dev_get_bridge_context(hdev_obj, &pbridge_context);
	if (!pbridge_context)
		status = -EFAULT;

	if (!status) {
		host_res = pbridge_context->resources;
		if (!host_res)
			return -EPERM;
		hnode_mgr->ul_chnl_offset = host_res->dw_chnl_offset;
		hnode_mgr->ul_chnl_buf_size = host_res->dw_chnl_buf_size;
		hnode_mgr->ul_num_chnls = host_res->dw_num_chnls;

		/*
		 *  PROC will add an API to get dsp_processorinfo.
		 *  Fill in default values for now.
		 */
		/* TODO -- Instead of hard coding, take from registry */
		hnode_mgr->proc_family = 6000;
		hnode_mgr->proc_type = 6410;
		hnode_mgr->min_pri = DSP_NODE_MIN_PRIORITY;
		hnode_mgr->max_pri = DSP_NODE_MAX_PRIORITY;
		hnode_mgr->udsp_word_size = DSPWORDSIZE;
		hnode_mgr->udsp_data_mau_size = DSPWORDSIZE;
		hnode_mgr->udsp_mau_size = 1;

	}
	return status;
}

/*
 *  ======== node_get_uuid_props ========
 *  Purpose:
 *      Fetch Node UUID properties from DCD/DOF file.
 */
int node_get_uuid_props(void *hprocessor,
			       const struct dsp_uuid *node_uuid,
			       struct dsp_ndbprops *node_props)
{
	struct node_mgr *hnode_mgr = NULL;
	struct dev_object *hdev_obj;
	int status = 0;
	struct dcd_nodeprops dcd_node_props;
	struct dsp_processorstate proc_state;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hprocessor != NULL);
	DBC_REQUIRE(node_uuid != NULL);

	if (hprocessor == NULL || node_uuid == NULL) {
		status = -EFAULT;
		goto func_end;
	}
	status = proc_get_state(hprocessor, &proc_state,
				sizeof(struct dsp_processorstate));
	if (status)
		goto func_end;
	/* If processor is in error state then don't attempt
	   to send the message */
	if (proc_state.proc_state == PROC_ERROR) {
		status = -EPERM;
		goto func_end;
	}

	status = proc_get_dev_object(hprocessor, &hdev_obj);
	if (hdev_obj) {
		status = dev_get_node_manager(hdev_obj, &hnode_mgr);
		if (hnode_mgr == NULL) {
			status = -EFAULT;
			goto func_end;
		}
	}

	/*
	 * Enter the critical section. This is needed because
	 * dcd_get_object_def will ultimately end up calling dbll_open/close,
	 * which needs to be protected in order to not corrupt the zlib manager
	 * (COD).
	 */
	mutex_lock(&hnode_mgr->node_mgr_lock);

	dcd_node_props.pstr_create_phase_fxn = NULL;
	dcd_node_props.pstr_execute_phase_fxn = NULL;
	dcd_node_props.pstr_delete_phase_fxn = NULL;
	dcd_node_props.pstr_i_alg_name = NULL;

	status = dcd_get_object_def(hnode_mgr->hdcd_mgr,
		(struct dsp_uuid *)node_uuid, DSP_DCDNODETYPE,
		(struct dcd_genericobj *)&dcd_node_props);

	if (!status) {
		*node_props = dcd_node_props.ndb_props;
		kfree(dcd_node_props.pstr_create_phase_fxn);

		kfree(dcd_node_props.pstr_execute_phase_fxn);

		kfree(dcd_node_props.pstr_delete_phase_fxn);

		kfree(dcd_node_props.pstr_i_alg_name);
	}
	/*  Leave the critical section, we're done. */
	mutex_unlock(&hnode_mgr->node_mgr_lock);
func_end:
	return status;
}

/*
 *  ======== get_rms_fxns ========
 *  Purpose:
 *      Retrieve the RMS functions.
 */
static int get_rms_fxns(struct node_mgr *hnode_mgr)
{
	s32 i;
	struct dev_object *dev_obj = hnode_mgr->hdev_obj;
	int status = 0;

	static char *psz_fxns[NUMRMSFXNS] = {
		"RMS_queryServer",	/* RMSQUERYSERVER */
		"RMS_configureServer",	/* RMSCONFIGURESERVER */
		"RMS_createNode",	/* RMSCREATENODE */
		"RMS_executeNode",	/* RMSEXECUTENODE */
		"RMS_deleteNode",	/* RMSDELETENODE */
		"RMS_changeNodePriority",	/* RMSCHANGENODEPRIORITY */
		"RMS_readMemory",	/* RMSREADMEMORY */
		"RMS_writeMemory",	/* RMSWRITEMEMORY */
		"RMS_copy",	/* RMSCOPY */
	};

	for (i = 0; i < NUMRMSFXNS; i++) {
		status = dev_get_symbol(dev_obj, psz_fxns[i],
					&(hnode_mgr->ul_fxn_addrs[i]));
		if (status) {
			if (status == -ESPIPE) {
				/*
				 *  May be loaded dynamically (in the future),
				 *  but return an error for now.
				 */
				dev_dbg(bridge, "%s: RMS function: %s currently"
					" not loaded\n", __func__, psz_fxns[i]);
			} else {
				dev_dbg(bridge, "%s: Symbol not found: %s "
					"status = 0x%x\n", __func__,
					psz_fxns[i], status);
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== ovly ========
 *  Purpose:
 *      Called during overlay.Sends command to RMS to copy a block of data.
 */
static u32 ovly(void *priv_ref, u32 dsp_run_addr, u32 dsp_load_addr,
		u32 ul_num_bytes, u32 mem_space)
{
	struct node_object *hnode = (struct node_object *)priv_ref;
	struct node_mgr *hnode_mgr;
	u32 ul_bytes = 0;
	u32 ul_size;
	u32 ul_timeout;
	int status = 0;
	struct bridge_dev_context *hbridge_context;
	/* Function interface to Bridge driver*/
	struct bridge_drv_interface *intf_fxns;

	DBC_REQUIRE(hnode);

	hnode_mgr = hnode->hnode_mgr;

	ul_size = ul_num_bytes / hnode_mgr->udsp_word_size;
	ul_timeout = hnode->utimeout;

	/* Call new MemCopy function */
	intf_fxns = hnode_mgr->intf_fxns;
	status = dev_get_bridge_context(hnode_mgr->hdev_obj, &hbridge_context);
	if (!status) {
		status =
		    (*intf_fxns->pfn_brd_mem_copy) (hbridge_context,
						dsp_run_addr, dsp_load_addr,
						ul_num_bytes, (u32) mem_space);
		if (!status)
			ul_bytes = ul_num_bytes;
		else
			pr_debug("%s: failed to copy brd memory, status 0x%x\n",
				 __func__, status);
	} else {
		pr_debug("%s: failed to get Bridge context, status 0x%x\n",
			 __func__, status);
	}

	return ul_bytes;
}

/*
 *  ======== mem_write ========
 */
static u32 mem_write(void *priv_ref, u32 dsp_add, void *pbuf,
		     u32 ul_num_bytes, u32 mem_space)
{
	struct node_object *hnode = (struct node_object *)priv_ref;
	struct node_mgr *hnode_mgr;
	u16 mem_sect_type;
	u32 ul_timeout;
	int status = 0;
	struct bridge_dev_context *hbridge_context;
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;

	DBC_REQUIRE(hnode);
	DBC_REQUIRE(mem_space & DBLL_CODE || mem_space & DBLL_DATA);

	hnode_mgr = hnode->hnode_mgr;

	ul_timeout = hnode->utimeout;
	mem_sect_type = (mem_space & DBLL_CODE) ? RMS_CODE : RMS_DATA;

	/* Call new MemWrite function */
	intf_fxns = hnode_mgr->intf_fxns;
	status = dev_get_bridge_context(hnode_mgr->hdev_obj, &hbridge_context);
	status = (*intf_fxns->pfn_brd_mem_write) (hbridge_context, pbuf,
					dsp_add, ul_num_bytes, mem_sect_type);

	return ul_num_bytes;
}

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
/*
 *  ======== node_find_addr ========
 */
int node_find_addr(struct node_mgr *node_mgr, u32 sym_addr,
		u32 offset_range, void *sym_addr_output, char *sym_name)
{
	struct node_object *node_obj;
	int status = -ENOENT;
	u32 n;

	pr_debug("%s(0x%x, 0x%x, 0x%x, 0x%x,  %s)\n", __func__,
			(unsigned int) node_mgr,
			sym_addr, offset_range,
			(unsigned int) sym_addr_output, sym_name);

	node_obj = (struct node_object *)(node_mgr->node_list->head.next);

	for (n = 0; n < node_mgr->num_nodes; n++) {
		status = nldr_find_addr(node_obj->nldr_node_obj, sym_addr,
			offset_range, sym_addr_output, sym_name);

		if (!status)
			break;

		node_obj = (struct node_object *) (node_obj->list_elem.next);
	}

	return status;
}
#endif
