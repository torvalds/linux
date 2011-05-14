/*
 * disp.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Node Dispatcher interface. Communicates with Resource Manager Server
 * (RMS) on DSP. Access to RMS is synchronized in NODE.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software;  you can redistribute it and/or modify
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
#include <dspbridge/sync.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdefs.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/chnldefs.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/nodedefs.h>
#include <dspbridge/nodepriv.h>
#include <dspbridge/rms_sh.h>

/*  ----------------------------------- This */
#include <dspbridge/disp.h>

/* Size of a reply from RMS */
#define REPLYSIZE (3 * sizeof(rms_word))

/* Reserved channel offsets for communication with RMS */
#define CHNLTORMSOFFSET       0
#define CHNLFROMRMSOFFSET     1

#define CHNLIOREQS      1

/*
 *  ======== disp_object ========
 */
struct disp_object {
	struct dev_object *dev_obj;	/* Device for this processor */
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;
	struct chnl_mgr *chnl_mgr;	/* Channel manager */
	struct chnl_object *chnl_to_dsp;	/* Chnl for commands to RMS */
	struct chnl_object *chnl_from_dsp;	/* Chnl for replies from RMS */
	u8 *buf;		/* Buffer for commands, replies */
	u32 bufsize;		/* buf size in bytes */
	u32 bufsize_rms;	/* buf size in RMS words */
	u32 char_size;		/* Size of DSP character */
	u32 word_size;		/* Size of DSP word */
	u32 data_mau_size;	/* Size of DSP Data MAU */
};

static u32 refs;

static void delete_disp(struct disp_object *disp_obj);
static int fill_stream_def(rms_word *pdw_buf, u32 *ptotal, u32 offset,
				  struct node_strmdef strm_def, u32 max,
				  u32 chars_in_rms_word);
static int send_message(struct disp_object *disp_obj, u32 timeout,
			       u32 ul_bytes, u32 *pdw_arg);

/*
 *  ======== disp_create ========
 *  Create a NODE Dispatcher object.
 */
int disp_create(struct disp_object **dispatch_obj,
		       struct dev_object *hdev_obj,
		       const struct disp_attr *disp_attrs)
{
	struct disp_object *disp_obj;
	struct bridge_drv_interface *intf_fxns;
	u32 ul_chnl_id;
	struct chnl_attr chnl_attr_obj;
	int status = 0;
	u8 dev_type;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(dispatch_obj != NULL);
	DBC_REQUIRE(disp_attrs != NULL);
	DBC_REQUIRE(hdev_obj != NULL);

	*dispatch_obj = NULL;

	/* Allocate Node Dispatcher object */
	disp_obj = kzalloc(sizeof(struct disp_object), GFP_KERNEL);
	if (disp_obj == NULL)
		status = -ENOMEM;
	else
		disp_obj->dev_obj = hdev_obj;

	/* Get Channel manager and Bridge function interface */
	if (!status) {
		status = dev_get_chnl_mgr(hdev_obj, &(disp_obj->chnl_mgr));
		if (!status) {
			(void)dev_get_intf_fxns(hdev_obj, &intf_fxns);
			disp_obj->intf_fxns = intf_fxns;
		}
	}

	/* check device type and decide if streams or messag'ing is used for
	 * RMS/EDS */
	if (status)
		goto func_cont;

	status = dev_get_dev_type(hdev_obj, &dev_type);

	if (status)
		goto func_cont;

	if (dev_type != DSP_UNIT) {
		status = -EPERM;
		goto func_cont;
	}

	disp_obj->char_size = DSPWORDSIZE;
	disp_obj->word_size = DSPWORDSIZE;
	disp_obj->data_mau_size = DSPWORDSIZE;
	/* Open channels for communicating with the RMS */
	chnl_attr_obj.uio_reqs = CHNLIOREQS;
	chnl_attr_obj.event_obj = NULL;
	ul_chnl_id = disp_attrs->chnl_offset + CHNLTORMSOFFSET;
	status = (*intf_fxns->chnl_open) (&(disp_obj->chnl_to_dsp),
					      disp_obj->chnl_mgr,
					      CHNL_MODETODSP, ul_chnl_id,
					      &chnl_attr_obj);

	if (!status) {
		ul_chnl_id = disp_attrs->chnl_offset + CHNLFROMRMSOFFSET;
		status =
		    (*intf_fxns->chnl_open) (&(disp_obj->chnl_from_dsp),
						 disp_obj->chnl_mgr,
						 CHNL_MODEFROMDSP, ul_chnl_id,
						 &chnl_attr_obj);
	}
	if (!status) {
		/* Allocate buffer for commands, replies */
		disp_obj->bufsize = disp_attrs->chnl_buf_size;
		disp_obj->bufsize_rms = RMS_COMMANDBUFSIZE;
		disp_obj->buf = kzalloc(disp_obj->bufsize, GFP_KERNEL);
		if (disp_obj->buf == NULL)
			status = -ENOMEM;
	}
func_cont:
	if (!status)
		*dispatch_obj = disp_obj;
	else
		delete_disp(disp_obj);

	DBC_ENSURE((status && *dispatch_obj == NULL) ||
				(!status && *dispatch_obj));
	return status;
}

/*
 *  ======== disp_delete ========
 *  Delete the NODE Dispatcher.
 */
void disp_delete(struct disp_object *disp_obj)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(disp_obj);

	delete_disp(disp_obj);
}

/*
 *  ======== disp_exit ========
 *  Discontinue usage of DISP module.
 */
void disp_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== disp_init ========
 *  Initialize the DISP module.
 */
bool disp_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));
	return ret;
}

/*
 *  ======== disp_node_change_priority ========
 *  Change the priority of a node currently running on the target.
 */
int disp_node_change_priority(struct disp_object *disp_obj,
				     struct node_object *hnode,
				     u32 rms_fxn, nodeenv node_env, s32 prio)
{
	u32 dw_arg;
	struct rms_command *rms_cmd;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(disp_obj);
	DBC_REQUIRE(hnode != NULL);

	/* Send message to RMS to change priority */
	rms_cmd = (struct rms_command *)(disp_obj->buf);
	rms_cmd->fxn = (rms_word) (rms_fxn);
	rms_cmd->arg1 = (rms_word) node_env;
	rms_cmd->arg2 = prio;
	status = send_message(disp_obj, node_get_timeout(hnode),
			      sizeof(struct rms_command), &dw_arg);

	return status;
}

/*
 *  ======== disp_node_create ========
 *  Create a node on the DSP by remotely calling the node's create function.
 */
int disp_node_create(struct disp_object *disp_obj,
			    struct node_object *hnode, u32 rms_fxn,
			    u32 ul_create_fxn,
			    const struct node_createargs *pargs,
			    nodeenv *node_env)
{
	struct node_msgargs node_msg_args;
	struct node_taskargs task_arg_obj;
	struct rms_command *rms_cmd;
	struct rms_msg_args *pmsg_args;
	struct rms_more_task_args *more_task_args;
	enum node_type node_type;
	u32 dw_length;
	rms_word *pdw_buf = NULL;
	u32 ul_bytes;
	u32 i;
	u32 total;
	u32 chars_in_rms_word;
	s32 task_args_offset;
	s32 sio_in_def_offset;
	s32 sio_out_def_offset;
	s32 sio_defs_offset;
	s32 args_offset = -1;
	s32 offset;
	struct node_strmdef strm_def;
	u32 max;
	int status = 0;
	struct dsp_nodeinfo node_info;
	u8 dev_type;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(disp_obj);
	DBC_REQUIRE(hnode != NULL);
	DBC_REQUIRE(node_get_type(hnode) != NODE_DEVICE);
	DBC_REQUIRE(node_env != NULL);

	status = dev_get_dev_type(disp_obj->dev_obj, &dev_type);

	if (status)
		goto func_end;

	if (dev_type != DSP_UNIT) {
		dev_dbg(bridge, "%s: unknown device type = 0x%x\n",
			__func__, dev_type);
		goto func_end;
	}
	DBC_REQUIRE(pargs != NULL);
	node_type = node_get_type(hnode);
	node_msg_args = pargs->asa.node_msg_args;
	max = disp_obj->bufsize_rms;	/*Max # of RMS words that can be sent */
	DBC_ASSERT(max == RMS_COMMANDBUFSIZE);
	chars_in_rms_word = sizeof(rms_word) / disp_obj->char_size;
	/* Number of RMS words needed to hold arg data */
	dw_length =
	    (node_msg_args.arg_length + chars_in_rms_word -
	     1) / chars_in_rms_word;
	/* Make sure msg args and command fit in buffer */
	total = sizeof(struct rms_command) / sizeof(rms_word) +
	    sizeof(struct rms_msg_args)
	    / sizeof(rms_word) - 1 + dw_length;
	if (total >= max) {
		status = -EPERM;
		dev_dbg(bridge, "%s: Message args too large for buffer! size "
			"= %d, max = %d\n", __func__, total, max);
	}
	/*
	 *  Fill in buffer to send to RMS.
	 *  The buffer will have the following  format:
	 *
	 *  RMS command:
	 *      Address of RMS_CreateNode()
	 *      Address of node's create function
	 *      dummy argument
	 *      node type
	 *
	 *  Message Args:
	 *      max number of messages
	 *      segid for message buffer allocation
	 *      notification type to use when message is received
	 *      length of message arg data
	 *      message args data
	 *
	 *  Task Args (if task or socket node):
	 *      priority
	 *      stack size
	 *      system stack size
	 *      stack segment
	 *      misc
	 *      number of input streams
	 *      pSTRMInDef[] - offsets of STRM definitions for input streams
	 *      number of output streams
	 *      pSTRMOutDef[] - offsets of STRM definitions for output
	 *      streams
	 *      STRMInDef[] - array of STRM definitions for input streams
	 *      STRMOutDef[] - array of STRM definitions for output streams
	 *
	 *  Socket Args (if DAIS socket node):
	 *
	 */
	if (!status) {
		total = 0;	/* Total number of words in buffer so far */
		pdw_buf = (rms_word *) disp_obj->buf;
		rms_cmd = (struct rms_command *)pdw_buf;
		rms_cmd->fxn = (rms_word) (rms_fxn);
		rms_cmd->arg1 = (rms_word) (ul_create_fxn);
		if (node_get_load_type(hnode) == NLDR_DYNAMICLOAD) {
			/* Flush ICACHE on Load */
			rms_cmd->arg2 = 1;	/* dummy argument */
		} else {
			/* Do not flush ICACHE */
			rms_cmd->arg2 = 0;	/* dummy argument */
		}
		rms_cmd->data = node_get_type(hnode);
		/*
		 *  args_offset is the offset of the data field in struct
		 *  rms_command structure. We need this to calculate stream
		 *  definition offsets.
		 */
		args_offset = 3;
		total += sizeof(struct rms_command) / sizeof(rms_word);
		/* Message args */
		pmsg_args = (struct rms_msg_args *)(pdw_buf + total);
		pmsg_args->max_msgs = node_msg_args.max_msgs;
		pmsg_args->segid = node_msg_args.seg_id;
		pmsg_args->notify_type = node_msg_args.notify_type;
		pmsg_args->arg_length = node_msg_args.arg_length;
		total += sizeof(struct rms_msg_args) / sizeof(rms_word) - 1;
		memcpy(pdw_buf + total, node_msg_args.pdata,
		       node_msg_args.arg_length);
		total += dw_length;
	}
	if (status)
		goto func_end;

	/* If node is a task node, copy task create arguments into  buffer */
	if (node_type == NODE_TASK || node_type == NODE_DAISSOCKET) {
		task_arg_obj = pargs->asa.task_arg_obj;
		task_args_offset = total;
		total += sizeof(struct rms_more_task_args) / sizeof(rms_word) +
		    1 + task_arg_obj.num_inputs + task_arg_obj.num_outputs;
		/* Copy task arguments */
		if (total < max) {
			total = task_args_offset;
			more_task_args = (struct rms_more_task_args *)(pdw_buf +
								       total);
			/*
			 * Get some important info about the node. Note that we
			 * don't just reach into the hnode struct because
			 * that would break the node object's abstraction.
			 */
			get_node_info(hnode, &node_info);
			more_task_args->priority = node_info.execution_priority;
			more_task_args->stack_size = task_arg_obj.stack_size;
			more_task_args->sysstack_size =
			    task_arg_obj.sys_stack_size;
			more_task_args->stack_seg = task_arg_obj.stack_seg;
			more_task_args->heap_addr = task_arg_obj.dsp_heap_addr;
			more_task_args->heap_size = task_arg_obj.heap_size;
			more_task_args->misc = task_arg_obj.dais_arg;
			more_task_args->num_input_streams =
			    task_arg_obj.num_inputs;
			total +=
			    sizeof(struct rms_more_task_args) /
			    sizeof(rms_word);
			dev_dbg(bridge, "%s: dsp_heap_addr %x, heap_size %x\n",
				__func__, task_arg_obj.dsp_heap_addr,
				task_arg_obj.heap_size);
			/* Keep track of pSIOInDef[] and pSIOOutDef[]
			 * positions in the buffer, since this needs to be
			 * filled in later. */
			sio_in_def_offset = total;
			total += task_arg_obj.num_inputs;
			pdw_buf[total++] = task_arg_obj.num_outputs;
			sio_out_def_offset = total;
			total += task_arg_obj.num_outputs;
			sio_defs_offset = total;
			/* Fill SIO defs and offsets */
			offset = sio_defs_offset;
			for (i = 0; i < task_arg_obj.num_inputs; i++) {
				if (status)
					break;

				pdw_buf[sio_in_def_offset + i] =
				    (offset - args_offset)
				    * (sizeof(rms_word) / DSPWORDSIZE);
				strm_def = task_arg_obj.strm_in_def[i];
				status =
				    fill_stream_def(pdw_buf, &total, offset,
						    strm_def, max,
						    chars_in_rms_word);
				offset = total;
			}
			for (i = 0; (i < task_arg_obj.num_outputs) &&
			     (!status); i++) {
				pdw_buf[sio_out_def_offset + i] =
				    (offset - args_offset)
				    * (sizeof(rms_word) / DSPWORDSIZE);
				strm_def = task_arg_obj.strm_out_def[i];
				status =
				    fill_stream_def(pdw_buf, &total, offset,
						    strm_def, max,
						    chars_in_rms_word);
				offset = total;
			}
		} else {
			/* Args won't fit */
			status = -EPERM;
		}
	}
	if (!status) {
		ul_bytes = total * sizeof(rms_word);
		DBC_ASSERT(ul_bytes < (RMS_COMMANDBUFSIZE * sizeof(rms_word)));
		status = send_message(disp_obj, node_get_timeout(hnode),
				      ul_bytes, node_env);
	}
func_end:
	return status;
}

/*
 *  ======== disp_node_delete ========
 *  purpose:
 *      Delete a node on the DSP by remotely calling the node's delete function.
 *
 */
int disp_node_delete(struct disp_object *disp_obj,
			    struct node_object *hnode, u32 rms_fxn,
			    u32 ul_delete_fxn, nodeenv node_env)
{
	u32 dw_arg;
	struct rms_command *rms_cmd;
	int status = 0;
	u8 dev_type;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(disp_obj);
	DBC_REQUIRE(hnode != NULL);

	status = dev_get_dev_type(disp_obj->dev_obj, &dev_type);

	if (!status) {

		if (dev_type == DSP_UNIT) {

			/*
			 *  Fill in buffer to send to RMS
			 */
			rms_cmd = (struct rms_command *)disp_obj->buf;
			rms_cmd->fxn = (rms_word) (rms_fxn);
			rms_cmd->arg1 = (rms_word) node_env;
			rms_cmd->arg2 = (rms_word) (ul_delete_fxn);
			rms_cmd->data = node_get_type(hnode);

			status = send_message(disp_obj, node_get_timeout(hnode),
					      sizeof(struct rms_command),
					      &dw_arg);
		}
	}
	return status;
}

/*
 *  ======== disp_node_run ========
 *  purpose:
 *      Start execution of a node's execute phase, or resume execution of a node
 *      that has been suspended (via DISP_NodePause()) on the DSP.
 */
int disp_node_run(struct disp_object *disp_obj,
			 struct node_object *hnode, u32 rms_fxn,
			 u32 ul_execute_fxn, nodeenv node_env)
{
	u32 dw_arg;
	struct rms_command *rms_cmd;
	int status = 0;
	u8 dev_type;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(disp_obj);
	DBC_REQUIRE(hnode != NULL);

	status = dev_get_dev_type(disp_obj->dev_obj, &dev_type);

	if (!status) {

		if (dev_type == DSP_UNIT) {

			/*
			 *  Fill in buffer to send to RMS.
			 */
			rms_cmd = (struct rms_command *)disp_obj->buf;
			rms_cmd->fxn = (rms_word) (rms_fxn);
			rms_cmd->arg1 = (rms_word) node_env;
			rms_cmd->arg2 = (rms_word) (ul_execute_fxn);
			rms_cmd->data = node_get_type(hnode);

			status = send_message(disp_obj, node_get_timeout(hnode),
					      sizeof(struct rms_command),
					      &dw_arg);
		}
	}

	return status;
}

/*
 *  ======== delete_disp ========
 *  purpose:
 *      Frees the resources allocated for the dispatcher.
 */
static void delete_disp(struct disp_object *disp_obj)
{
	int status = 0;
	struct bridge_drv_interface *intf_fxns;

	if (disp_obj) {
		intf_fxns = disp_obj->intf_fxns;

		/* Free Node Dispatcher resources */
		if (disp_obj->chnl_from_dsp) {
			/* Channel close can fail only if the channel handle
			 * is invalid. */
			status = (*intf_fxns->chnl_close)
			    (disp_obj->chnl_from_dsp);
			if (status) {
				dev_dbg(bridge, "%s: Failed to close channel "
					"from RMS: 0x%x\n", __func__, status);
			}
		}
		if (disp_obj->chnl_to_dsp) {
			status =
			    (*intf_fxns->chnl_close) (disp_obj->
							  chnl_to_dsp);
			if (status) {
				dev_dbg(bridge, "%s: Failed to close channel to"
					" RMS: 0x%x\n", __func__, status);
			}
		}
		kfree(disp_obj->buf);

		kfree(disp_obj);
	}
}

/*
 *  ======== fill_stream_def ========
 *  purpose:
 *      Fills stream definitions.
 */
static int fill_stream_def(rms_word *pdw_buf, u32 *ptotal, u32 offset,
				  struct node_strmdef strm_def, u32 max,
				  u32 chars_in_rms_word)
{
	struct rms_strm_def *strm_def_obj;
	u32 total = *ptotal;
	u32 name_len;
	u32 dw_length;
	int status = 0;

	if (total + sizeof(struct rms_strm_def) / sizeof(rms_word) >= max) {
		status = -EPERM;
	} else {
		strm_def_obj = (struct rms_strm_def *)(pdw_buf + total);
		strm_def_obj->bufsize = strm_def.buf_size;
		strm_def_obj->nbufs = strm_def.num_bufs;
		strm_def_obj->segid = strm_def.seg_id;
		strm_def_obj->align = strm_def.buf_alignment;
		strm_def_obj->timeout = strm_def.timeout;
	}

	if (!status) {
		/*
		 *  Since we haven't added the device name yet, subtract
		 *  1 from total.
		 */
		total += sizeof(struct rms_strm_def) / sizeof(rms_word) - 1;
		DBC_REQUIRE(strm_def.sz_device);
		dw_length = strlen(strm_def.sz_device) + 1;

		/* Number of RMS_WORDS needed to hold device name */
		name_len =
		    (dw_length + chars_in_rms_word - 1) / chars_in_rms_word;

		if (total + name_len >= max) {
			status = -EPERM;
		} else {
			/*
			 *  Zero out last word, since the device name may not
			 *  extend to completely fill this word.
			 */
			pdw_buf[total + name_len - 1] = 0;
			/** TODO USE SERVICES * */
			memcpy(pdw_buf + total, strm_def.sz_device, dw_length);
			total += name_len;
			*ptotal = total;
		}
	}

	return status;
}

/*
 *  ======== send_message ======
 *  Send command message to RMS, get reply from RMS.
 */
static int send_message(struct disp_object *disp_obj, u32 timeout,
			       u32 ul_bytes, u32 *pdw_arg)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_object *chnl_obj;
	u32 dw_arg = 0;
	u8 *pbuf;
	struct chnl_ioc chnl_ioc_obj;
	int status = 0;

	DBC_REQUIRE(pdw_arg != NULL);

	*pdw_arg = (u32) NULL;
	intf_fxns = disp_obj->intf_fxns;
	chnl_obj = disp_obj->chnl_to_dsp;
	pbuf = disp_obj->buf;

	/* Send the command */
	status = (*intf_fxns->chnl_add_io_req) (chnl_obj, pbuf, ul_bytes, 0,
						    0L, dw_arg);
	if (status)
		goto func_end;

	status =
	    (*intf_fxns->chnl_get_ioc) (chnl_obj, timeout, &chnl_ioc_obj);
	if (!status) {
		if (!CHNL_IS_IO_COMPLETE(chnl_ioc_obj)) {
			if (CHNL_IS_TIMED_OUT(chnl_ioc_obj))
				status = -ETIME;
			else
				status = -EPERM;
		}
	}
	/* Get the reply */
	if (status)
		goto func_end;

	chnl_obj = disp_obj->chnl_from_dsp;
	ul_bytes = REPLYSIZE;
	status = (*intf_fxns->chnl_add_io_req) (chnl_obj, pbuf, ul_bytes,
						    0, 0L, dw_arg);
	if (status)
		goto func_end;

	status =
	    (*intf_fxns->chnl_get_ioc) (chnl_obj, timeout, &chnl_ioc_obj);
	if (!status) {
		if (CHNL_IS_TIMED_OUT(chnl_ioc_obj)) {
			status = -ETIME;
		} else if (chnl_ioc_obj.byte_size < ul_bytes) {
			/* Did not get all of the reply from the RMS */
			status = -EPERM;
		} else {
			if (CHNL_IS_IO_COMPLETE(chnl_ioc_obj)) {
				DBC_ASSERT(chnl_ioc_obj.buf == pbuf);
				if (*((int *)chnl_ioc_obj.buf) < 0) {
					/* Translate DSP's to kernel error */
					status = -EREMOTEIO;
					dev_dbg(bridge, "%s: DSP-side failed:"
						" DSP errcode = 0x%x, Kernel "
						"errcode = %d\n", __func__,
						*(int *)pbuf, status);
				}
				*pdw_arg =
				    (((rms_word *) (chnl_ioc_obj.buf))[1]);
			} else {
				status = -EPERM;
			}
		}
	}
func_end:
	return status;
}
