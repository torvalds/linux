/*
 * dspapi.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Common DSP API functions, also includes the wrapper
 * functions called directly by the DeviceIOControl interface.
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

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/ntfy.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/chnl.h>
#include <dspbridge/dev.h>
#include <dspbridge/drv.h>

#include <dspbridge/proc.h>
#include <dspbridge/strm.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/disp.h>
#include <dspbridge/mgr.h>
#include <dspbridge/node.h>
#include <dspbridge/rmm.h>

/*  ----------------------------------- Others */
#include <dspbridge/msg.h>
#include <dspbridge/cmm.h>
#include <dspbridge/io.h>

/*  ----------------------------------- This */
#include <dspbridge/dspapi.h>
#include <dspbridge/dbdcd.h>

#include <dspbridge/resourcecleanup.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define MAX_TRACEBUFLEN 255
#define MAX_LOADARGS    16
#define MAX_NODES       64
#define MAX_STREAMS     16
#define MAX_BUFS	64

/* Used to get dspbridge ioctl table */
#define DB_GET_IOC_TABLE(cmd)	(DB_GET_MODULE(cmd) >> DB_MODULE_SHIFT)

/* Device IOCtl function pointer */
struct api_cmd {
	u32(*fxn) (union trapped_args *args, void *pr_ctxt);
	u32 index;
};

/*  ----------------------------------- Globals */
static u32 api_c_refs;

/*
 *  Function tables.
 *  The order of these functions MUST be the same as the order of the command
 *  numbers defined in dspapi-ioctl.h  This is how an IOCTL number in user mode
 *  turns into a function call in kernel mode.
 */

/* MGR wrapper functions */
static struct api_cmd mgr_cmd[] = {
	{mgrwrap_enum_node_info},	/* MGR_ENUMNODE_INFO */
	{mgrwrap_enum_proc_info},	/* MGR_ENUMPROC_INFO */
	{mgrwrap_register_object},	/* MGR_REGISTEROBJECT */
	{mgrwrap_unregister_object},	/* MGR_UNREGISTEROBJECT */
	{mgrwrap_wait_for_bridge_events},	/* MGR_WAIT */
	{mgrwrap_get_process_resources_info},	/* MGR_GET_PROC_RES */
};

/* PROC wrapper functions */
static struct api_cmd proc_cmd[] = {
	{procwrap_attach},	/* PROC_ATTACH */
	{procwrap_ctrl},	/* PROC_CTRL */
	{procwrap_detach},	/* PROC_DETACH */
	{procwrap_enum_node_info},	/* PROC_ENUMNODE */
	{procwrap_enum_resources},	/* PROC_ENUMRESOURCES */
	{procwrap_get_state},	/* PROC_GET_STATE */
	{procwrap_get_trace},	/* PROC_GET_TRACE */
	{procwrap_load},	/* PROC_LOAD */
	{procwrap_register_notify},	/* PROC_REGISTERNOTIFY */
	{procwrap_start},	/* PROC_START */
	{procwrap_reserve_memory},	/* PROC_RSVMEM */
	{procwrap_un_reserve_memory},	/* PROC_UNRSVMEM */
	{procwrap_map},		/* PROC_MAPMEM */
	{procwrap_un_map},	/* PROC_UNMAPMEM */
	{procwrap_flush_memory},	/* PROC_FLUSHMEMORY */
	{procwrap_stop},	/* PROC_STOP */
	{procwrap_invalidate_memory},	/* PROC_INVALIDATEMEMORY */
	{procwrap_begin_dma},	/* PROC_BEGINDMA */
	{procwrap_end_dma},	/* PROC_ENDDMA */
};

/* NODE wrapper functions */
static struct api_cmd node_cmd[] = {
	{nodewrap_allocate},	/* NODE_ALLOCATE */
	{nodewrap_alloc_msg_buf},	/* NODE_ALLOCMSGBUF */
	{nodewrap_change_priority},	/* NODE_CHANGEPRIORITY */
	{nodewrap_connect},	/* NODE_CONNECT */
	{nodewrap_create},	/* NODE_CREATE */
	{nodewrap_delete},	/* NODE_DELETE */
	{nodewrap_free_msg_buf},	/* NODE_FREEMSGBUF */
	{nodewrap_get_attr},	/* NODE_GETATTR */
	{nodewrap_get_message},	/* NODE_GETMESSAGE */
	{nodewrap_pause},	/* NODE_PAUSE */
	{nodewrap_put_message},	/* NODE_PUTMESSAGE */
	{nodewrap_register_notify},	/* NODE_REGISTERNOTIFY */
	{nodewrap_run},		/* NODE_RUN */
	{nodewrap_terminate},	/* NODE_TERMINATE */
	{nodewrap_get_uuid_props},	/* NODE_GETUUIDPROPS */
};

/* STRM wrapper functions */
static struct api_cmd strm_cmd[] = {
	{strmwrap_allocate_buffer},	/* STRM_ALLOCATEBUFFER */
	{strmwrap_close},	/* STRM_CLOSE */
	{strmwrap_free_buffer},	/* STRM_FREEBUFFER */
	{strmwrap_get_event_handle},	/* STRM_GETEVENTHANDLE */
	{strmwrap_get_info},	/* STRM_GETINFO */
	{strmwrap_idle},	/* STRM_IDLE */
	{strmwrap_issue},	/* STRM_ISSUE */
	{strmwrap_open},	/* STRM_OPEN */
	{strmwrap_reclaim},	/* STRM_RECLAIM */
	{strmwrap_register_notify},	/* STRM_REGISTERNOTIFY */
	{strmwrap_select},	/* STRM_SELECT */
};

/* CMM wrapper functions */
static struct api_cmd cmm_cmd[] = {
	{cmmwrap_calloc_buf},	/* CMM_ALLOCBUF */
	{cmmwrap_free_buf},	/* CMM_FREEBUF */
	{cmmwrap_get_handle},	/* CMM_GETHANDLE */
	{cmmwrap_get_info},	/* CMM_GETINFO */
};

/* Array used to store ioctl table sizes. It can hold up to 8 entries */
static u8 size_cmd[] = {
	ARRAY_SIZE(mgr_cmd),
	ARRAY_SIZE(proc_cmd),
	ARRAY_SIZE(node_cmd),
	ARRAY_SIZE(strm_cmd),
	ARRAY_SIZE(cmm_cmd),
};

static inline void _cp_fm_usr(void *to, const void __user *from,
			      int *err, unsigned long bytes)
{
	if (*err)
		return;

	if (unlikely(!from)) {
		*err = -EFAULT;
		return;
	}

	if (unlikely(copy_from_user(to, from, bytes)))
		*err = -EFAULT;
}

#define CP_FM_USR(to, from, err, n)				\
	_cp_fm_usr(to, from, &(err), (n) * sizeof(*(to)))

static inline void _cp_to_usr(void __user *to, const void *from,
			      int *err, unsigned long bytes)
{
	if (*err)
		return;

	if (unlikely(!to)) {
		*err = -EFAULT;
		return;
	}

	if (unlikely(copy_to_user(to, from, bytes)))
		*err = -EFAULT;
}

#define CP_TO_USR(to, from, err, n)				\
	_cp_to_usr(to, from, &(err), (n) * sizeof(*(from)))

/*
 *  ======== api_call_dev_ioctl ========
 *  Purpose:
 *      Call the (wrapper) function for the corresponding API IOCTL.
 */
inline int api_call_dev_ioctl(u32 cmd, union trapped_args *args,
				      u32 *result, void *pr_ctxt)
{
	u32(*ioctl_cmd) (union trapped_args *args, void *pr_ctxt) = NULL;
	int i;

	if (_IOC_TYPE(cmd) != DB) {
		pr_err("%s: Incompatible dspbridge ioctl number\n", __func__);
		goto err;
	}

	if (DB_GET_IOC_TABLE(cmd) > ARRAY_SIZE(size_cmd)) {
		pr_err("%s: undefined ioctl module\n", __func__);
		goto err;
	}

	/* Check the size of the required cmd table */
	i = DB_GET_IOC(cmd);
	if (i > size_cmd[DB_GET_IOC_TABLE(cmd)]) {
		pr_err("%s: requested ioctl %d out of bounds for table %d\n",
		       __func__, i, DB_GET_IOC_TABLE(cmd));
		goto err;
	}

	switch (DB_GET_MODULE(cmd)) {
	case DB_MGR:
		ioctl_cmd = mgr_cmd[i].fxn;
		break;
	case DB_PROC:
		ioctl_cmd = proc_cmd[i].fxn;
		break;
	case DB_NODE:
		ioctl_cmd = node_cmd[i].fxn;
		break;
	case DB_STRM:
		ioctl_cmd = strm_cmd[i].fxn;
		break;
	case DB_CMM:
		ioctl_cmd = cmm_cmd[i].fxn;
		break;
	}

	if (!ioctl_cmd) {
		pr_err("%s: requested ioctl not defined\n", __func__);
		goto err;
	} else {
		*result = (*ioctl_cmd) (args, pr_ctxt);
	}

	return 0;

err:
	return -EINVAL;
}

/*
 *  ======== api_exit ========
 */
void api_exit(void)
{
	api_c_refs--;

	if (api_c_refs == 0)
		mgr_exit();
}

/*
 *  ======== api_init ========
 *  Purpose:
 *      Module initialization used by Bridge API.
 */
bool api_init(void)
{
	bool ret = true;

	if (api_c_refs == 0)
		ret = mgr_init();

	if (ret)
		api_c_refs++;

	return ret;
}

/*
 *  ======== api_init_complete2 ========
 *  Purpose:
 *      Perform any required bridge initialization which cannot
 *      be performed in api_init() or dev_start_device() due
 *      to the fact that some services are not yet
 *      completely initialized.
 *  Parameters:
 *  Returns:
 *      0:	Allow this device to load
 *      -EPERM:      Failure.
 *  Requires:
 *      Bridge API initialized.
 *  Ensures:
 */
int api_init_complete2(void)
{
	int status = 0;
	struct cfg_devnode *dev_node;
	struct dev_object *hdev_obj;
	struct drv_data *drv_datap;
	u8 dev_type;

	/*  Walk the list of DevObjects, get each devnode, and attempting to
	 *  autostart the board. Note that this requires COF loading, which
	 *  requires KFILE. */
	for (hdev_obj = dev_get_first(); hdev_obj != NULL;
	     hdev_obj = dev_get_next(hdev_obj)) {
		if (dev_get_dev_node(hdev_obj, &dev_node))
			continue;

		if (dev_get_dev_type(hdev_obj, &dev_type))
			continue;

		if ((dev_type == DSP_UNIT) || (dev_type == IVA_UNIT)) {
			drv_datap = dev_get_drvdata(bridge);

			if (drv_datap && drv_datap->base_img)
				proc_auto_start(dev_node, hdev_obj);
		}
	}

	return status;
}

/* TODO: Remove deprecated and not implemented ioctl wrappers */

/*
 * ======== mgrwrap_enum_node_info ========
 */
u32 mgrwrap_enum_node_info(union trapped_args *args, void *pr_ctxt)
{
	u8 *pndb_props;
	u32 num_nodes;
	int status = 0;
	u32 size = args->args_mgr_enumnode_info.ndb_props_size;

	if (size < sizeof(struct dsp_ndbprops))
		return -EINVAL;

	pndb_props = kmalloc(size, GFP_KERNEL);
	if (pndb_props == NULL)
		status = -ENOMEM;

	if (!status) {
		status =
		    mgr_enum_node_info(args->args_mgr_enumnode_info.node_id,
				       (struct dsp_ndbprops *)pndb_props, size,
				       &num_nodes);
	}
	CP_TO_USR(args->args_mgr_enumnode_info.ndb_props, pndb_props, status,
		  size);
	CP_TO_USR(args->args_mgr_enumnode_info.num_nodes, &num_nodes, status,
		  1);
	kfree(pndb_props);

	return status;
}

/*
 * ======== mgrwrap_enum_proc_info ========
 */
u32 mgrwrap_enum_proc_info(union trapped_args *args, void *pr_ctxt)
{
	u8 *processor_info;
	u8 num_procs;
	int status = 0;
	u32 size = args->args_mgr_enumproc_info.processor_info_size;

	if (size < sizeof(struct dsp_processorinfo))
		return -EINVAL;

	processor_info = kmalloc(size, GFP_KERNEL);
	if (processor_info == NULL)
		status = -ENOMEM;

	if (!status) {
		status =
		    mgr_enum_processor_info(args->args_mgr_enumproc_info.
					    processor_id,
					    (struct dsp_processorinfo *)
					    processor_info, size, &num_procs);
	}
	CP_TO_USR(args->args_mgr_enumproc_info.processor_info, processor_info,
		  status, size);
	CP_TO_USR(args->args_mgr_enumproc_info.num_procs, &num_procs,
		  status, 1);
	kfree(processor_info);

	return status;
}

#define WRAP_MAP2CALLER(x) x
/*
 * ======== mgrwrap_register_object ========
 */
u32 mgrwrap_register_object(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct dsp_uuid uuid_obj;
	u32 path_size = 0;
	char *psz_path_name = NULL;
	int status = 0;

	CP_FM_USR(&uuid_obj, args->args_mgr_registerobject.uuid_obj, status, 1);
	if (status)
		goto func_end;
	path_size = strlen_user((char *)
				args->args_mgr_registerobject.sz_path_name);
	if (!path_size) {
		status = -EINVAL;
		goto func_end;
	}

	psz_path_name = kmalloc(path_size, GFP_KERNEL);
	if (!psz_path_name) {
		status = -ENOMEM;
		goto func_end;
	}
	ret = strncpy_from_user(psz_path_name,
				(char *)args->args_mgr_registerobject.
				sz_path_name, path_size);
	if (!ret) {
		status = -EFAULT;
		goto func_end;
	}

	if (args->args_mgr_registerobject.obj_type >= DSP_DCDMAXOBJTYPE) {
		status = -EINVAL;
		goto func_end;
	}

	status = dcd_register_object(&uuid_obj,
				     args->args_mgr_registerobject.obj_type,
				     (char *)psz_path_name);
func_end:
	kfree(psz_path_name);
	return status;
}

/*
 * ======== mgrwrap_unregister_object ========
 */
u32 mgrwrap_unregister_object(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_uuid uuid_obj;

	CP_FM_USR(&uuid_obj, args->args_mgr_registerobject.uuid_obj, status, 1);
	if (status)
		goto func_end;

	status = dcd_unregister_object(&uuid_obj,
				       args->args_mgr_unregisterobject.
				       obj_type);
func_end:
	return status;

}

/*
 * ======== mgrwrap_wait_for_bridge_events ========
 */
u32 mgrwrap_wait_for_bridge_events(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_notification *anotifications[MAX_EVENTS];
	struct dsp_notification notifications[MAX_EVENTS];
	u32 index, i;
	u32 count = args->args_mgr_wait.count;

	if (count > MAX_EVENTS)
		status = -EINVAL;

	/* get the array of pointers to user structures */
	CP_FM_USR(anotifications, args->args_mgr_wait.anotifications,
		  status, count);
	/* get the events */
	for (i = 0; i < count; i++) {
		CP_FM_USR(&notifications[i], anotifications[i], status, 1);
		if (status || !notifications[i].handle) {
			status = -EINVAL;
			break;
		}
		/* set the array of pointers to kernel structures */
		anotifications[i] = &notifications[i];
	}
	if (!status) {
		status = mgr_wait_for_bridge_events(anotifications, count,
							 &index,
							 args->args_mgr_wait.
							 timeout);
	}
	CP_TO_USR(args->args_mgr_wait.index, &index, status, 1);
	return status;
}

/*
 * ======== MGRWRAP_GetProcessResourceInfo ========
 */
u32 __deprecated mgrwrap_get_process_resources_info(union trapped_args *args,
						    void *pr_ctxt)
{
	pr_err("%s: deprecated dspbridge ioctl\n", __func__);
	return 0;
}

/*
 * ======== procwrap_attach ========
 */
u32 procwrap_attach(union trapped_args *args, void *pr_ctxt)
{
	void *processor;
	int status = 0;
	struct dsp_processorattrin proc_attr_in, *attr_in = NULL;

	/* Optional argument */
	if (args->args_proc_attach.attr_in) {
		CP_FM_USR(&proc_attr_in, args->args_proc_attach.attr_in, status,
			  1);
		if (!status)
			attr_in = &proc_attr_in;
		else
			goto func_end;

	}
	status = proc_attach(args->args_proc_attach.processor_id, attr_in,
			     &processor, pr_ctxt);
	CP_TO_USR(args->args_proc_attach.ph_processor, &processor, status, 1);
func_end:
	return status;
}

/*
 * ======== procwrap_ctrl ========
 */
u32 procwrap_ctrl(union trapped_args *args, void *pr_ctxt)
{
	u32 cb_data_size, __user * psize = (u32 __user *)
	    args->args_proc_ctrl.args;
	u8 *pargs = NULL;
	int status = 0;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (psize) {
		if (get_user(cb_data_size, psize)) {
			status = -EPERM;
			goto func_end;
		}
		cb_data_size += sizeof(u32);
		pargs = kmalloc(cb_data_size, GFP_KERNEL);
		if (pargs == NULL) {
			status = -ENOMEM;
			goto func_end;
		}

		CP_FM_USR(pargs, args->args_proc_ctrl.args, status,
			  cb_data_size);
	}
	if (!status) {
		status = proc_ctrl(hprocessor,
				   args->args_proc_ctrl.cmd,
				   (struct dsp_cbdata *)pargs);
	}

	/* CP_TO_USR(args->args_proc_ctrl.args, pargs, status, 1); */
	kfree(pargs);
func_end:
	return status;
}

/*
 * ======== procwrap_detach ========
 */
u32 __deprecated procwrap_detach(union trapped_args *args, void *pr_ctxt)
{
	/* proc_detach called at bridge_release only */
	pr_err("%s: deprecated dspbridge ioctl\n", __func__);
	return 0;
}

/*
 * ======== procwrap_enum_node_info ========
 */
u32 procwrap_enum_node_info(union trapped_args *args, void *pr_ctxt)
{
	int status;
	void *node_tab[MAX_NODES];
	u32 num_nodes;
	u32 alloc_cnt;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (!args->args_proc_enumnode_info.node_tab_size)
		return -EINVAL;

	status = proc_enum_nodes(hprocessor,
				 node_tab,
				 args->args_proc_enumnode_info.node_tab_size,
				 &num_nodes, &alloc_cnt);
	CP_TO_USR(args->args_proc_enumnode_info.node_tab, node_tab, status,
		  num_nodes);
	CP_TO_USR(args->args_proc_enumnode_info.num_nodes, &num_nodes,
		  status, 1);
	CP_TO_USR(args->args_proc_enumnode_info.allocated, &alloc_cnt,
		  status, 1);
	return status;
}

u32 procwrap_end_dma(union trapped_args *args, void *pr_ctxt)
{
	int status;

	if (args->args_proc_dma.dir >= DMA_NONE)
		return -EINVAL;

	status = proc_end_dma(pr_ctxt,
				   args->args_proc_dma.mpu_addr,
				   args->args_proc_dma.size,
				   args->args_proc_dma.dir);
	return status;
}

u32 procwrap_begin_dma(union trapped_args *args, void *pr_ctxt)
{
	int status;

	if (args->args_proc_dma.dir >= DMA_NONE)
		return -EINVAL;

	status = proc_begin_dma(pr_ctxt,
				   args->args_proc_dma.mpu_addr,
				   args->args_proc_dma.size,
				   args->args_proc_dma.dir);
	return status;
}

/*
 * ======== procwrap_flush_memory ========
 */
u32 procwrap_flush_memory(union trapped_args *args, void *pr_ctxt)
{
	int status;

	if (args->args_proc_flushmemory.flags >
	    PROC_WRITEBACK_INVALIDATE_MEM)
		return -EINVAL;

	status = proc_flush_memory(pr_ctxt,
				   args->args_proc_flushmemory.mpu_addr,
				   args->args_proc_flushmemory.size,
				   args->args_proc_flushmemory.flags);
	return status;
}

/*
 * ======== procwrap_invalidate_memory ========
 */
u32 procwrap_invalidate_memory(union trapped_args *args, void *pr_ctxt)
{
	int status;

	status =
	    proc_invalidate_memory(pr_ctxt,
				   args->args_proc_invalidatememory.mpu_addr,
				   args->args_proc_invalidatememory.size);
	return status;
}

/*
 * ======== procwrap_enum_resources ========
 */
u32 procwrap_enum_resources(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_resourceinfo resource_info;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (args->args_proc_enumresources.resource_info_size <
	    sizeof(struct dsp_resourceinfo))
		return -EINVAL;

	status =
	    proc_get_resource_info(hprocessor,
				   args->args_proc_enumresources.resource_type,
				   &resource_info,
				   args->args_proc_enumresources.
				   resource_info_size);

	CP_TO_USR(args->args_proc_enumresources.resource_info, &resource_info,
		  status, 1);

	return status;

}

/*
 * ======== procwrap_get_state ========
 */
u32 procwrap_get_state(union trapped_args *args, void *pr_ctxt)
{
	int status;
	struct dsp_processorstate proc_state;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (args->args_proc_getstate.state_info_size <
	    sizeof(struct dsp_processorstate))
		return -EINVAL;

	status = proc_get_state(hprocessor, &proc_state,
			   args->args_proc_getstate.state_info_size);
	CP_TO_USR(args->args_proc_getstate.proc_state_obj, &proc_state, status,
		  1);
	return status;

}

/*
 * ======== procwrap_get_trace ========
 */
u32 procwrap_get_trace(union trapped_args *args, void *pr_ctxt)
{
	int status;
	u8 *pbuf;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (args->args_proc_gettrace.max_size > MAX_TRACEBUFLEN)
		return -EINVAL;

	pbuf = kzalloc(args->args_proc_gettrace.max_size, GFP_KERNEL);
	if (pbuf != NULL) {
		status = proc_get_trace(hprocessor, pbuf,
					args->args_proc_gettrace.max_size);
	} else {
		status = -ENOMEM;
	}
	CP_TO_USR(args->args_proc_gettrace.buf, pbuf, status,
		  args->args_proc_gettrace.max_size);
	kfree(pbuf);

	return status;
}

/*
 * ======== procwrap_load ========
 */
u32 procwrap_load(union trapped_args *args, void *pr_ctxt)
{
	s32 i, len;
	int status = 0;
	char *temp;
	s32 count = args->args_proc_load.argc_index;
	u8 **argv = NULL, **envp = NULL;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (count <= 0 || count > MAX_LOADARGS) {
		status = -EINVAL;
		goto func_cont;
	}

	argv = kmalloc(count * sizeof(u8 *), GFP_KERNEL);
	if (!argv) {
		status = -ENOMEM;
		goto func_cont;
	}

	CP_FM_USR(argv, args->args_proc_load.user_args, status, count);
	if (status) {
		kfree(argv);
		argv = NULL;
		goto func_cont;
	}

	for (i = 0; i < count; i++) {
		if (argv[i]) {
			/* User space pointer to argument */
			temp = (char *)argv[i];
			/* len is increased by 1 to accommodate NULL */
			len = strlen_user((char *)temp) + 1;
			/* Kernel space pointer to argument */
			argv[i] = kmalloc(len, GFP_KERNEL);
			if (argv[i]) {
				CP_FM_USR(argv[i], temp, status, len);
				if (status) {
					kfree(argv[i]);
					argv[i] = NULL;
					goto func_cont;
				}
			} else {
				status = -ENOMEM;
				goto func_cont;
			}
		}
	}
	/* TODO: validate this */
	if (args->args_proc_load.user_envp) {
		/* number of elements in the envp array including NULL */
		count = 0;
		do {
			if (get_user(temp,
				     args->args_proc_load.user_envp + count)) {
				status = -EFAULT;
				goto func_cont;
			}
			count++;
		} while (temp);
		envp = kmalloc(count * sizeof(u8 *), GFP_KERNEL);
		if (!envp) {
			status = -ENOMEM;
			goto func_cont;
		}

		CP_FM_USR(envp, args->args_proc_load.user_envp, status, count);
		if (status) {
			kfree(envp);
			envp = NULL;
			goto func_cont;
		}
		for (i = 0; envp[i]; i++) {
			/* User space pointer to argument */
			temp = (char *)envp[i];
			/* len is increased by 1 to accommodate NULL */
			len = strlen_user((char *)temp) + 1;
			/* Kernel space pointer to argument */
			envp[i] = kmalloc(len, GFP_KERNEL);
			if (envp[i]) {
				CP_FM_USR(envp[i], temp, status, len);
				if (status) {
					kfree(envp[i]);
					envp[i] = NULL;
					goto func_cont;
				}
			} else {
				status = -ENOMEM;
				goto func_cont;
			}
		}
	}

	if (!status) {
		status = proc_load(hprocessor,
				   args->args_proc_load.argc_index,
				   (const char **)argv, (const char **)envp);
	}
func_cont:
	if (envp) {
		i = 0;
		while (envp[i])
			kfree(envp[i++]);

		kfree(envp);
	}

	if (argv) {
		count = args->args_proc_load.argc_index;
		for (i = 0; (i < count) && argv[i]; i++)
			kfree(argv[i]);

		kfree(argv);
	}

	return status;
}

/*
 * ======== procwrap_map ========
 */
u32 procwrap_map(union trapped_args *args, void *pr_ctxt)
{
	int status;
	void *map_addr;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if (!args->args_proc_mapmem.size)
		return -EINVAL;

	status = proc_map(args->args_proc_mapmem.processor,
			  args->args_proc_mapmem.mpu_addr,
			  args->args_proc_mapmem.size,
			  args->args_proc_mapmem.req_addr, &map_addr,
			  args->args_proc_mapmem.map_attr, pr_ctxt);
	if (!status) {
		if (put_user(map_addr, args->args_proc_mapmem.map_addr)) {
			status = -EINVAL;
			proc_un_map(hprocessor, map_addr, pr_ctxt);
		}

	}
	return status;
}

/*
 * ======== procwrap_register_notify ========
 */
u32 procwrap_register_notify(union trapped_args *args, void *pr_ctxt)
{
	int status;
	struct dsp_notification notification;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	/* Initialize the notification data structure */
	notification.name = NULL;
	notification.handle = NULL;

	status = proc_register_notify(hprocessor,
				 args->args_proc_register_notify.event_mask,
				 args->args_proc_register_notify.notify_type,
				 &notification);
	CP_TO_USR(args->args_proc_register_notify.notification, &notification,
		  status, 1);
	return status;
}

/*
 * ======== procwrap_reserve_memory ========
 */
u32 procwrap_reserve_memory(union trapped_args *args, void *pr_ctxt)
{
	int status;
	void *prsv_addr;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	if ((args->args_proc_rsvmem.size <= 0) ||
	    (args->args_proc_rsvmem.size & (PG_SIZE4K - 1)) != 0)
		return -EINVAL;

	status = proc_reserve_memory(hprocessor,
				     args->args_proc_rsvmem.size, &prsv_addr,
				     pr_ctxt);
	if (!status) {
		if (put_user(prsv_addr, args->args_proc_rsvmem.rsv_addr)) {
			status = -EINVAL;
			proc_un_reserve_memory(args->args_proc_rsvmem.
					       processor, prsv_addr, pr_ctxt);
		}
	}
	return status;
}

/*
 * ======== procwrap_start ========
 */
u32 procwrap_start(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;

	ret = proc_start(((struct process_context *)pr_ctxt)->processor);
	return ret;
}

/*
 * ======== procwrap_un_map ========
 */
u32 procwrap_un_map(union trapped_args *args, void *pr_ctxt)
{
	int status;

	status = proc_un_map(((struct process_context *)pr_ctxt)->processor,
			     args->args_proc_unmapmem.map_addr, pr_ctxt);
	return status;
}

/*
 * ======== procwrap_un_reserve_memory ========
 */
u32 procwrap_un_reserve_memory(union trapped_args *args, void *pr_ctxt)
{
	int status;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	status = proc_un_reserve_memory(hprocessor,
					args->args_proc_unrsvmem.rsv_addr,
					pr_ctxt);
	return status;
}

/*
 * ======== procwrap_stop ========
 */
u32 procwrap_stop(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;

	ret = proc_stop(((struct process_context *)pr_ctxt)->processor);

	return ret;
}

/*
 * ======== find_handle =========
 */
inline void find_node_handle(struct node_res_object **noderes,
				void *pr_ctxt, void *hnode)
{
	rcu_read_lock();
	*noderes = idr_find(((struct process_context *)pr_ctxt)->node_id,
								(int)hnode - 1);
	rcu_read_unlock();
	return;
}


/*
 * ======== nodewrap_allocate ========
 */
u32 nodewrap_allocate(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_uuid node_uuid;
	u32 cb_data_size = 0;
	u32 __user *psize = (u32 __user *) args->args_node_allocate.args;
	u8 *pargs = NULL;
	struct dsp_nodeattrin proc_attr_in, *attr_in = NULL;
	struct node_res_object *node_res;
	int nodeid;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	/* Optional argument */
	if (psize) {
		if (get_user(cb_data_size, psize))
			status = -EPERM;

		cb_data_size += sizeof(u32);
		if (!status) {
			pargs = kmalloc(cb_data_size, GFP_KERNEL);
			if (pargs == NULL)
				status = -ENOMEM;

		}
		CP_FM_USR(pargs, args->args_node_allocate.args, status,
			  cb_data_size);
	}
	CP_FM_USR(&node_uuid, args->args_node_allocate.node_id_ptr, status, 1);
	if (status)
		goto func_cont;
	/* Optional argument */
	if (args->args_node_allocate.attr_in) {
		CP_FM_USR(&proc_attr_in, args->args_node_allocate.attr_in,
			  status, 1);
		if (!status)
			attr_in = &proc_attr_in;
		else
			status = -ENOMEM;

	}
	if (!status) {
		status = node_allocate(hprocessor,
				       &node_uuid, (struct dsp_cbdata *)pargs,
				       attr_in, &node_res, pr_ctxt);
	}
	if (!status) {
		nodeid = node_res->id + 1;
		CP_TO_USR(args->args_node_allocate.node, &nodeid,
			status, 1);
		if (status) {
			status = -EFAULT;
			node_delete(node_res, pr_ctxt);
		}
	}
func_cont:
	kfree(pargs);

	return status;
}

/*
 *  ======== nodewrap_alloc_msg_buf ========
 */
u32 nodewrap_alloc_msg_buf(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_bufferattr *pattr = NULL;
	struct dsp_bufferattr attr;
	u8 *pbuffer = NULL;
	struct node_res_object *node_res;

	find_node_handle(&node_res,  pr_ctxt,
				args->args_node_allocmsgbuf.node);

	if (!node_res)
		return -EFAULT;

	if (!args->args_node_allocmsgbuf.size)
		return -EINVAL;

	if (args->args_node_allocmsgbuf.attr) {	/* Optional argument */
		CP_FM_USR(&attr, args->args_node_allocmsgbuf.attr, status, 1);
		if (!status)
			pattr = &attr;

	}
	/* argument */
	CP_FM_USR(&pbuffer, args->args_node_allocmsgbuf.buffer, status, 1);
	if (!status) {
		status = node_alloc_msg_buf(node_res->node,
					    args->args_node_allocmsgbuf.size,
					    pattr, &pbuffer);
	}
	CP_TO_USR(args->args_node_allocmsgbuf.buffer, &pbuffer, status, 1);
	return status;
}

/*
 * ======== nodewrap_change_priority ========
 */
u32 nodewrap_change_priority(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt,
				args->args_node_changepriority.node);

	if (!node_res)
		return -EFAULT;

	ret = node_change_priority(node_res->node,
				   args->args_node_changepriority.prio);

	return ret;
}

/*
 * ======== nodewrap_connect ========
 */
u32 nodewrap_connect(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_strmattr attrs;
	struct dsp_strmattr *pattrs = NULL;
	u32 cb_data_size;
	u32 __user *psize = (u32 __user *) args->args_node_connect.conn_param;
	u8 *pargs = NULL;
	struct node_res_object *node_res1, *node_res2;
	struct node_object *node1 = NULL, *node2 = NULL;

	if ((int)args->args_node_connect.node != DSP_HGPPNODE) {
		find_node_handle(&node_res1, pr_ctxt,
				args->args_node_connect.node);
		if (node_res1)
			node1 = node_res1->node;
	} else {
		node1 = args->args_node_connect.node;
	}

	if ((int)args->args_node_connect.other_node != DSP_HGPPNODE) {
		find_node_handle(&node_res2, pr_ctxt,
				args->args_node_connect.other_node);
		if (node_res2)
			node2 = node_res2->node;
	} else {
		node2 = args->args_node_connect.other_node;
	}

	if (!node1 || !node2)
		return -EFAULT;

	/* Optional argument */
	if (psize) {
		if (get_user(cb_data_size, psize))
			status = -EPERM;

		cb_data_size += sizeof(u32);
		if (!status) {
			pargs = kmalloc(cb_data_size, GFP_KERNEL);
			if (pargs == NULL) {
				status = -ENOMEM;
				goto func_cont;
			}

		}
		CP_FM_USR(pargs, args->args_node_connect.conn_param, status,
			  cb_data_size);
		if (status)
			goto func_cont;
	}
	if (args->args_node_connect.attrs) {	/* Optional argument */
		CP_FM_USR(&attrs, args->args_node_connect.attrs, status, 1);
		if (!status)
			pattrs = &attrs;

	}
	if (!status) {
		status = node_connect(node1,
				      args->args_node_connect.stream_id,
				      node2,
				      args->args_node_connect.other_stream,
				      pattrs, (struct dsp_cbdata *)pargs);
	}
func_cont:
	kfree(pargs);

	return status;
}

/*
 * ======== nodewrap_create ========
 */
u32 nodewrap_create(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_create.node);

	if (!node_res)
		return -EFAULT;

	ret = node_create(node_res->node);

	return ret;
}

/*
 * ======== nodewrap_delete ========
 */
u32 nodewrap_delete(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_delete.node);

	if (!node_res)
		return -EFAULT;

	ret = node_delete(node_res, pr_ctxt);

	return ret;
}

/*
 *  ======== nodewrap_free_msg_buf ========
 */
u32 nodewrap_free_msg_buf(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_bufferattr *pattr = NULL;
	struct dsp_bufferattr attr;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_freemsgbuf.node);

	if (!node_res)
		return -EFAULT;

	if (args->args_node_freemsgbuf.attr) {	/* Optional argument */
		CP_FM_USR(&attr, args->args_node_freemsgbuf.attr, status, 1);
		if (!status)
			pattr = &attr;

	}

	if (!args->args_node_freemsgbuf.buffer)
		return -EFAULT;

	if (!status) {
		status = node_free_msg_buf(node_res->node,
					   args->args_node_freemsgbuf.buffer,
					   pattr);
	}

	return status;
}

/*
 * ======== nodewrap_get_attr ========
 */
u32 nodewrap_get_attr(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_nodeattr attr;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_getattr.node);

	if (!node_res)
		return -EFAULT;

	status = node_get_attr(node_res->node, &attr,
			       args->args_node_getattr.attr_size);
	CP_TO_USR(args->args_node_getattr.attr, &attr, status, 1);

	return status;
}

/*
 * ======== nodewrap_get_message ========
 */
u32 nodewrap_get_message(union trapped_args *args, void *pr_ctxt)
{
	int status;
	struct dsp_msg msg;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_getmessage.node);

	if (!node_res)
		return -EFAULT;

	status = node_get_message(node_res->node, &msg,
				  args->args_node_getmessage.timeout);

	CP_TO_USR(args->args_node_getmessage.message, &msg, status, 1);

	return status;
}

/*
 * ======== nodewrap_pause ========
 */
u32 nodewrap_pause(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_pause.node);

	if (!node_res)
		return -EFAULT;

	ret = node_pause(node_res->node);

	return ret;
}

/*
 * ======== nodewrap_put_message ========
 */
u32 nodewrap_put_message(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_msg msg;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_putmessage.node);

	if (!node_res)
		return -EFAULT;

	CP_FM_USR(&msg, args->args_node_putmessage.message, status, 1);

	if (!status) {
		status =
		    node_put_message(node_res->node, &msg,
				     args->args_node_putmessage.timeout);
	}

	return status;
}

/*
 * ======== nodewrap_register_notify ========
 */
u32 nodewrap_register_notify(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_notification notification;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt,
			args->args_node_registernotify.node);

	if (!node_res)
		return -EFAULT;

	/* Initialize the notification data structure */
	notification.name = NULL;
	notification.handle = NULL;

	if (!args->args_proc_register_notify.event_mask)
		CP_FM_USR(&notification,
			  args->args_proc_register_notify.notification,
			  status, 1);

	status = node_register_notify(node_res->node,
				      args->args_node_registernotify.event_mask,
				      args->args_node_registernotify.
				      notify_type, &notification);
	CP_TO_USR(args->args_node_registernotify.notification, &notification,
		  status, 1);
	return status;
}

/*
 * ======== nodewrap_run ========
 */
u32 nodewrap_run(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_run.node);

	if (!node_res)
		return -EFAULT;

	ret = node_run(node_res->node);

	return ret;
}

/*
 * ======== nodewrap_terminate ========
 */
u32 nodewrap_terminate(union trapped_args *args, void *pr_ctxt)
{
	int status;
	int tempstatus;
	struct node_res_object *node_res;

	find_node_handle(&node_res, pr_ctxt, args->args_node_terminate.node);

	if (!node_res)
		return -EFAULT;

	status = node_terminate(node_res->node, &tempstatus);

	CP_TO_USR(args->args_node_terminate.status, &tempstatus, status, 1);

	return status;
}

/*
 * ======== nodewrap_get_uuid_props ========
 */
u32 nodewrap_get_uuid_props(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_uuid node_uuid;
	struct dsp_ndbprops *pnode_props = NULL;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	CP_FM_USR(&node_uuid, args->args_node_getuuidprops.node_id_ptr, status,
		  1);
	if (status)
		goto func_cont;
	pnode_props = kmalloc(sizeof(struct dsp_ndbprops), GFP_KERNEL);
	if (pnode_props != NULL) {
		status =
		    node_get_uuid_props(hprocessor, &node_uuid, pnode_props);
		CP_TO_USR(args->args_node_getuuidprops.node_props, pnode_props,
			  status, 1);
	} else
		status = -ENOMEM;
func_cont:
	kfree(pnode_props);
	return status;
}

/*
 * ======== find_strm_handle =========
 */
inline void find_strm_handle(struct strm_res_object **strmres,
				void *pr_ctxt, void *hstream)
{
	rcu_read_lock();
	*strmres = idr_find(((struct process_context *)pr_ctxt)->stream_id,
							(int)hstream - 1);
	rcu_read_unlock();
	return;
}

/*
 * ======== strmwrap_allocate_buffer ========
 */
u32 strmwrap_allocate_buffer(union trapped_args *args, void *pr_ctxt)
{
	int status;
	u8 **ap_buffer = NULL;
	u32 num_bufs = args->args_strm_allocatebuffer.num_bufs;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt,
		args->args_strm_allocatebuffer.stream);

	if (!strm_res)
		return -EFAULT;

	if (num_bufs > MAX_BUFS)
		return -EINVAL;

	ap_buffer = kmalloc((num_bufs * sizeof(u8 *)), GFP_KERNEL);
	if (ap_buffer == NULL)
		return -ENOMEM;

	status = strm_allocate_buffer(strm_res,
				      args->args_strm_allocatebuffer.size,
				      ap_buffer, num_bufs, pr_ctxt);
	if (!status) {
		CP_TO_USR(args->args_strm_allocatebuffer.ap_buffer, ap_buffer,
			  status, num_bufs);
		if (status) {
			status = -EFAULT;
			strm_free_buffer(strm_res,
					 ap_buffer, num_bufs, pr_ctxt);
		}
	}
	kfree(ap_buffer);

	return status;
}

/*
 * ======== strmwrap_close ========
 */
u32 strmwrap_close(union trapped_args *args, void *pr_ctxt)
{
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt, args->args_strm_close.stream);

	if (!strm_res)
		return -EFAULT;

	return strm_close(strm_res, pr_ctxt);
}

/*
 * ======== strmwrap_free_buffer ========
 */
u32 strmwrap_free_buffer(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	u8 **ap_buffer = NULL;
	u32 num_bufs = args->args_strm_freebuffer.num_bufs;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt,
			args->args_strm_freebuffer.stream);

	if (!strm_res)
		return -EFAULT;

	if (num_bufs > MAX_BUFS)
		return -EINVAL;

	ap_buffer = kmalloc_array(num_bufs, sizeof(u8 *), GFP_KERNEL);
	if (ap_buffer == NULL)
		return -ENOMEM;

	CP_FM_USR(ap_buffer, args->args_strm_freebuffer.ap_buffer, status,
		  num_bufs);

	if (!status)
		status = strm_free_buffer(strm_res,
					  ap_buffer, num_bufs, pr_ctxt);

	CP_TO_USR(args->args_strm_freebuffer.ap_buffer, ap_buffer, status,
		  num_bufs);
	kfree(ap_buffer);

	return status;
}

/*
 * ======== strmwrap_get_event_handle ========
 */
u32 __deprecated strmwrap_get_event_handle(union trapped_args *args,
					   void *pr_ctxt)
{
	pr_err("%s: deprecated dspbridge ioctl\n", __func__);
	return -ENOSYS;
}

/*
 * ======== strmwrap_get_info ========
 */
u32 strmwrap_get_info(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct stream_info strm_info;
	struct dsp_streaminfo user;
	struct dsp_streaminfo *temp;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt,
			args->args_strm_getinfo.stream);

	if (!strm_res)
		return -EFAULT;

	CP_FM_USR(&strm_info, args->args_strm_getinfo.stream_info, status, 1);
	temp = strm_info.user_strm;

	strm_info.user_strm = &user;

	if (!status) {
		status = strm_get_info(strm_res->stream,
				       &strm_info,
				       args->args_strm_getinfo.
				       stream_info_size);
	}
	CP_TO_USR(temp, strm_info.user_strm, status, 1);
	strm_info.user_strm = temp;
	CP_TO_USR(args->args_strm_getinfo.stream_info, &strm_info, status, 1);
	return status;
}

/*
 * ======== strmwrap_idle ========
 */
u32 strmwrap_idle(union trapped_args *args, void *pr_ctxt)
{
	u32 ret;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt, args->args_strm_idle.stream);

	if (!strm_res)
		return -EFAULT;

	ret = strm_idle(strm_res->stream, args->args_strm_idle.flush_flag);

	return ret;
}

/*
 * ======== strmwrap_issue ========
 */
u32 strmwrap_issue(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt, args->args_strm_issue.stream);

	if (!strm_res)
		return -EFAULT;

	if (!args->args_strm_issue.buffer)
		return -EFAULT;

	/* No need of doing CP_FM_USR for the user buffer (pbuffer)
	   as this is done in Bridge internal function bridge_chnl_add_io_req
	   in chnl_sm.c */
	status = strm_issue(strm_res->stream,
			    args->args_strm_issue.buffer,
			    args->args_strm_issue.bytes,
			    args->args_strm_issue.buf_size,
			    args->args_strm_issue.arg);

	return status;
}

/*
 * ======== strmwrap_open ========
 */
u32 strmwrap_open(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct strm_attr attr;
	struct strm_res_object *strm_res_obj;
	struct dsp_streamattrin strm_attr_in;
	struct node_res_object *node_res;
	int strmid;

	find_node_handle(&node_res, pr_ctxt, args->args_strm_open.node);

	if (!node_res)
		return -EFAULT;

	CP_FM_USR(&attr, args->args_strm_open.attr_in, status, 1);

	if (attr.stream_attr_in != NULL) {	/* Optional argument */
		CP_FM_USR(&strm_attr_in, attr.stream_attr_in, status, 1);
		if (!status) {
			attr.stream_attr_in = &strm_attr_in;
			if (attr.stream_attr_in->strm_mode == STRMMODE_LDMA)
				return -ENOSYS;
		}

	}
	status = strm_open(node_res->node,
			   args->args_strm_open.direction,
			   args->args_strm_open.index, &attr, &strm_res_obj,
			   pr_ctxt);
	if (!status) {
		strmid = strm_res_obj->id + 1;
		CP_TO_USR(args->args_strm_open.stream, &strmid, status, 1);
	}
	return status;
}

/*
 * ======== strmwrap_reclaim ========
 */
u32 strmwrap_reclaim(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	u8 *buf_ptr;
	u32 ul_bytes;
	u32 dw_arg;
	u32 ul_buf_size;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt, args->args_strm_reclaim.stream);

	if (!strm_res)
		return -EFAULT;

	status = strm_reclaim(strm_res->stream, &buf_ptr,
			      &ul_bytes, &ul_buf_size, &dw_arg);
	CP_TO_USR(args->args_strm_reclaim.buf_ptr, &buf_ptr, status, 1);
	CP_TO_USR(args->args_strm_reclaim.bytes, &ul_bytes, status, 1);
	CP_TO_USR(args->args_strm_reclaim.arg, &dw_arg, status, 1);

	if (args->args_strm_reclaim.buf_size_ptr != NULL) {
		CP_TO_USR(args->args_strm_reclaim.buf_size_ptr, &ul_buf_size,
			  status, 1);
	}

	return status;
}

/*
 * ======== strmwrap_register_notify ========
 */
u32 strmwrap_register_notify(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct dsp_notification notification;
	struct strm_res_object *strm_res;

	find_strm_handle(&strm_res, pr_ctxt,
			args->args_strm_registernotify.stream);

	if (!strm_res)
		return -EFAULT;

	/* Initialize the notification data structure */
	notification.name = NULL;
	notification.handle = NULL;

	status = strm_register_notify(strm_res->stream,
				      args->args_strm_registernotify.event_mask,
				      args->args_strm_registernotify.
				      notify_type, &notification);
	CP_TO_USR(args->args_strm_registernotify.notification, &notification,
		  status, 1);

	return status;
}

/*
 * ======== strmwrap_select ========
 */
u32 strmwrap_select(union trapped_args *args, void *pr_ctxt)
{
	u32 mask;
	struct strm_object *strm_tab[MAX_STREAMS];
	int status = 0;
	struct strm_res_object *strm_res;
	int *ids[MAX_STREAMS];
	int i;

	if (args->args_strm_select.strm_num > MAX_STREAMS)
		return -EINVAL;

	CP_FM_USR(ids, args->args_strm_select.stream_tab, status,
		args->args_strm_select.strm_num);

	if (status)
		return status;

	for (i = 0; i < args->args_strm_select.strm_num; i++) {
		find_strm_handle(&strm_res, pr_ctxt, ids[i]);

		if (!strm_res)
			return -EFAULT;

		strm_tab[i] = strm_res->stream;
	}

	if (!status) {
		status = strm_select(strm_tab, args->args_strm_select.strm_num,
				     &mask, args->args_strm_select.timeout);
	}
	CP_TO_USR(args->args_strm_select.mask, &mask, status, 1);
	return status;
}

/* CMM */

/*
 * ======== cmmwrap_calloc_buf ========
 */
u32 __deprecated cmmwrap_calloc_buf(union trapped_args *args, void *pr_ctxt)
{
	/* This operation is done in kernel */
	pr_err("%s: deprecated dspbridge ioctl\n", __func__);
	return -ENOSYS;
}

/*
 * ======== cmmwrap_free_buf ========
 */
u32 __deprecated cmmwrap_free_buf(union trapped_args *args, void *pr_ctxt)
{
	/* This operation is done in kernel */
	pr_err("%s: deprecated dspbridge ioctl\n", __func__);
	return -ENOSYS;
}

/*
 * ======== cmmwrap_get_handle ========
 */
u32 cmmwrap_get_handle(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct cmm_object *hcmm_mgr;
	void *hprocessor = ((struct process_context *)pr_ctxt)->processor;

	status = cmm_get_handle(hprocessor, &hcmm_mgr);

	CP_TO_USR(args->args_cmm_gethandle.cmm_mgr, &hcmm_mgr, status, 1);

	return status;
}

/*
 * ======== cmmwrap_get_info ========
 */
u32 cmmwrap_get_info(union trapped_args *args, void *pr_ctxt)
{
	int status = 0;
	struct cmm_info cmm_info_obj;

	status = cmm_get_info(args->args_cmm_getinfo.cmm_mgr, &cmm_info_obj);

	CP_TO_USR(args->args_cmm_getinfo.cmm_info_obj, &cmm_info_obj, status,
		  1);

	return status;
}
