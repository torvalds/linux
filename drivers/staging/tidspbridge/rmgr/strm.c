/*
 * strm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Stream Manager.
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
#include <dspbridge/sync.h>

/*  ----------------------------------- Bridge Driver */
#include <dspbridge/dspdefs.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/nodepriv.h>

/*  ----------------------------------- Others */
#include <dspbridge/cmm.h>

/*  ----------------------------------- This */
#include <dspbridge/strm.h>

#include <dspbridge/resourcecleanup.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define DEFAULTTIMEOUT      10000
#define DEFAULTNUMBUFS      2

/*
 *  ======== strm_mgr ========
 *  The strm_mgr contains device information needed to open the underlying
 *  channels of a stream.
 */
struct strm_mgr {
	struct dev_object *dev_obj;	/* Device for this processor */
	struct chnl_mgr *chnl_mgr;	/* Channel manager */
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;
};

/*
 *  ======== strm_object ========
 *  This object is allocated in strm_open().
 */
struct strm_object {
	struct strm_mgr *strm_mgr_obj;
	struct chnl_object *chnl_obj;
	u32 dir;		/* DSP_TONODE or DSP_FROMNODE */
	u32 timeout;
	u32 num_bufs;		/* Max # of bufs allowed in stream */
	u32 bufs_in_strm;	/* Current # of bufs in stream */
	u32 bytes;		/* bytes transferred since idled */
	/* STREAM_IDLE, STREAM_READY, ... */
	enum dsp_streamstate strm_state;
	void *user_event;	/* Saved for strm_get_info() */
	enum dsp_strmmode strm_mode;	/* STRMMODE_[PROCCOPY][ZEROCOPY]... */
	u32 dma_chnl_id;	/* DMA chnl id */
	u32 dma_priority;	/* DMA priority:DMAPRI_[LOW][HIGH] */
	u32 segment_id;		/* >0 is SM segment.=0 is local heap */
	u32 buf_alignment;	/* Alignment for stream bufs */
	/* Stream's SM address translator */
	struct cmm_xlatorobject *xlator;
};

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */

/*  ----------------------------------- Function Prototypes */
static int delete_strm(struct strm_object *stream_obj);

/*
 *  ======== strm_allocate_buffer ========
 *  Purpose:
 *      Allocates buffers for a stream.
 */
int strm_allocate_buffer(struct strm_res_object *strmres, u32 usize,
				u8 **ap_buffer, u32 num_bufs,
				struct process_context *pr_ctxt)
{
	int status = 0;
	u32 alloc_cnt = 0;
	u32 i;
	struct strm_object *stream_obj = strmres->stream;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ap_buffer != NULL);

	if (stream_obj) {
		/*
		 * Allocate from segment specified at time of stream open.
		 */
		if (usize == 0)
			status = -EINVAL;

	} else {
		status = -EFAULT;
	}

	if (status)
		goto func_end;

	for (i = 0; i < num_bufs; i++) {
		DBC_ASSERT(stream_obj->xlator != NULL);
		(void)cmm_xlator_alloc_buf(stream_obj->xlator, &ap_buffer[i],
					   usize);
		if (ap_buffer[i] == NULL) {
			status = -ENOMEM;
			alloc_cnt = i;
			break;
		}
	}
	if (status)
		strm_free_buffer(strmres, ap_buffer, alloc_cnt, pr_ctxt);

	if (status)
		goto func_end;

	drv_proc_update_strm_res(num_bufs, strmres);

func_end:
	return status;
}

/*
 *  ======== strm_close ========
 *  Purpose:
 *      Close a stream opened with strm_open().
 */
int strm_close(struct strm_res_object *strmres,
		      struct process_context *pr_ctxt)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_info chnl_info_obj;
	int status = 0;
	struct strm_object *stream_obj = strmres->stream;

	DBC_REQUIRE(refs > 0);

	if (!stream_obj) {
		status = -EFAULT;
	} else {
		/* Have all buffers been reclaimed? If not, return
		 * -EPIPE */
		intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;
		status =
		    (*intf_fxns->chnl_get_info) (stream_obj->chnl_obj,
						     &chnl_info_obj);
		DBC_ASSERT(!status);

		if (chnl_info_obj.cio_cs > 0 || chnl_info_obj.cio_reqs > 0)
			status = -EPIPE;
		else
			status = delete_strm(stream_obj);
	}

	if (status)
		goto func_end;

	idr_remove(pr_ctxt->stream_id, strmres->id);
func_end:
	DBC_ENSURE(status == 0 || status == -EFAULT ||
		   status == -EPIPE || status == -EPERM);

	dev_dbg(bridge, "%s: stream_obj: %p, status 0x%x\n", __func__,
		stream_obj, status);
	return status;
}

/*
 *  ======== strm_create ========
 *  Purpose:
 *      Create a STRM manager object.
 */
int strm_create(struct strm_mgr **strm_man,
		       struct dev_object *dev_obj)
{
	struct strm_mgr *strm_mgr_obj;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(strm_man != NULL);
	DBC_REQUIRE(dev_obj != NULL);

	*strm_man = NULL;
	/* Allocate STRM manager object */
	strm_mgr_obj = kzalloc(sizeof(struct strm_mgr), GFP_KERNEL);
	if (strm_mgr_obj == NULL)
		status = -ENOMEM;
	else
		strm_mgr_obj->dev_obj = dev_obj;

	/* Get Channel manager and Bridge function interface */
	if (!status) {
		status = dev_get_chnl_mgr(dev_obj, &(strm_mgr_obj->chnl_mgr));
		if (!status) {
			(void)dev_get_intf_fxns(dev_obj,
						&(strm_mgr_obj->intf_fxns));
			DBC_ASSERT(strm_mgr_obj->intf_fxns != NULL);
		}
	}

	if (!status)
		*strm_man = strm_mgr_obj;
	else
		kfree(strm_mgr_obj);

	DBC_ENSURE((!status && *strm_man) || (status && *strm_man == NULL));

	return status;
}

/*
 *  ======== strm_delete ========
 *  Purpose:
 *      Delete the STRM Manager Object.
 */
void strm_delete(struct strm_mgr *strm_mgr_obj)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(strm_mgr_obj);

	kfree(strm_mgr_obj);
}

/*
 *  ======== strm_exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 */
void strm_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== strm_free_buffer ========
 *  Purpose:
 *      Frees the buffers allocated for a stream.
 */
int strm_free_buffer(struct strm_res_object *strmres, u8 ** ap_buffer,
			    u32 num_bufs, struct process_context *pr_ctxt)
{
	int status = 0;
	u32 i = 0;
	struct strm_object *stream_obj = strmres->stream;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ap_buffer != NULL);

	if (!stream_obj)
		status = -EFAULT;

	if (!status) {
		for (i = 0; i < num_bufs; i++) {
			DBC_ASSERT(stream_obj->xlator != NULL);
			status =
			    cmm_xlator_free_buf(stream_obj->xlator,
						ap_buffer[i]);
			if (status)
				break;
			ap_buffer[i] = NULL;
		}
	}
	drv_proc_update_strm_res(num_bufs - i, strmres);

	return status;
}

/*
 *  ======== strm_get_info ========
 *  Purpose:
 *      Retrieves information about a stream.
 */
int strm_get_info(struct strm_object *stream_obj,
			 struct stream_info *stream_info,
			 u32 stream_info_size)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_info chnl_info_obj;
	int status = 0;
	void *virt_base = NULL;	/* NULL if no SM used */

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(stream_info != NULL);
	DBC_REQUIRE(stream_info_size >= sizeof(struct stream_info));

	if (!stream_obj) {
		status = -EFAULT;
	} else {
		if (stream_info_size < sizeof(struct stream_info)) {
			/* size of users info */
			status = -EINVAL;
		}
	}
	if (status)
		goto func_end;

	intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;
	status =
	    (*intf_fxns->chnl_get_info) (stream_obj->chnl_obj,
						  &chnl_info_obj);
	if (status)
		goto func_end;

	if (stream_obj->xlator) {
		/* We have a translator */
		DBC_ASSERT(stream_obj->segment_id > 0);
		cmm_xlator_info(stream_obj->xlator, (u8 **) &virt_base, 0,
				stream_obj->segment_id, false);
	}
	stream_info->segment_id = stream_obj->segment_id;
	stream_info->strm_mode = stream_obj->strm_mode;
	stream_info->virt_base = virt_base;
	stream_info->user_strm->number_bufs_allowed = stream_obj->num_bufs;
	stream_info->user_strm->number_bufs_in_stream = chnl_info_obj.cio_cs +
	    chnl_info_obj.cio_reqs;
	/* # of bytes transferred since last call to DSPStream_Idle() */
	stream_info->user_strm->number_bytes = chnl_info_obj.bytes_tx;
	stream_info->user_strm->sync_object_handle = chnl_info_obj.event_obj;
	/* Determine stream state based on channel state and info */
	if (chnl_info_obj.state & CHNL_STATEEOS) {
		stream_info->user_strm->ss_stream_state = STREAM_DONE;
	} else {
		if (chnl_info_obj.cio_cs > 0)
			stream_info->user_strm->ss_stream_state = STREAM_READY;
		else if (chnl_info_obj.cio_reqs > 0)
			stream_info->user_strm->ss_stream_state =
			    STREAM_PENDING;
		else
			stream_info->user_strm->ss_stream_state = STREAM_IDLE;

	}
func_end:
	return status;
}

/*
 *  ======== strm_idle ========
 *  Purpose:
 *      Idles a particular stream.
 */
int strm_idle(struct strm_object *stream_obj, bool flush_data)
{
	struct bridge_drv_interface *intf_fxns;
	int status = 0;

	DBC_REQUIRE(refs > 0);

	if (!stream_obj) {
		status = -EFAULT;
	} else {
		intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;

		status = (*intf_fxns->chnl_idle) (stream_obj->chnl_obj,
						      stream_obj->timeout,
						      flush_data);
	}

	dev_dbg(bridge, "%s: stream_obj: %p flush_data: 0x%x status: 0x%x\n",
		__func__, stream_obj, flush_data, status);
	return status;
}

/*
 *  ======== strm_init ========
 *  Purpose:
 *      Initialize the STRM module.
 */
bool strm_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== strm_issue ========
 *  Purpose:
 *      Issues a buffer on a stream
 */
int strm_issue(struct strm_object *stream_obj, u8 *pbuf, u32 ul_bytes,
		      u32 ul_buf_size, u32 dw_arg)
{
	struct bridge_drv_interface *intf_fxns;
	int status = 0;
	void *tmp_buf = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pbuf != NULL);

	if (!stream_obj) {
		status = -EFAULT;
	} else {
		intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;

		if (stream_obj->segment_id != 0) {
			tmp_buf = cmm_xlator_translate(stream_obj->xlator,
						       (void *)pbuf,
						       CMM_VA2DSPPA);
			if (tmp_buf == NULL)
				status = -ESRCH;

		}
		if (!status) {
			status = (*intf_fxns->chnl_add_io_req)
			    (stream_obj->chnl_obj, pbuf, ul_bytes, ul_buf_size,
			     (u32) tmp_buf, dw_arg);
		}
		if (status == -EIO)
			status = -ENOSR;
	}

	dev_dbg(bridge, "%s: stream_obj: %p pbuf: %p ul_bytes: 0x%x dw_arg:"
		" 0x%x status: 0x%x\n", __func__, stream_obj, pbuf,
		ul_bytes, dw_arg, status);
	return status;
}

/*
 *  ======== strm_open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task or
 *      XDAIS socket node on the DSP.
 */
int strm_open(struct node_object *hnode, u32 dir, u32 index,
		     struct strm_attr *pattr,
		     struct strm_res_object **strmres,
		     struct process_context *pr_ctxt)
{
	struct strm_mgr *strm_mgr_obj;
	struct bridge_drv_interface *intf_fxns;
	u32 ul_chnl_id;
	struct strm_object *strm_obj = NULL;
	s8 chnl_mode;
	struct chnl_attr chnl_attr_obj;
	int status = 0;
	struct cmm_object *hcmm_mgr = NULL;	/* Shared memory manager hndl */

	void *stream_res;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(strmres != NULL);
	DBC_REQUIRE(pattr != NULL);
	*strmres = NULL;
	if (dir != DSP_TONODE && dir != DSP_FROMNODE) {
		status = -EPERM;
	} else {
		/* Get the channel id from the node (set in node_connect()) */
		status = node_get_channel_id(hnode, dir, index, &ul_chnl_id);
	}
	if (!status)
		status = node_get_strm_mgr(hnode, &strm_mgr_obj);

	if (!status) {
		strm_obj = kzalloc(sizeof(struct strm_object), GFP_KERNEL);
		if (strm_obj == NULL) {
			status = -ENOMEM;
		} else {
			strm_obj->strm_mgr_obj = strm_mgr_obj;
			strm_obj->dir = dir;
			strm_obj->strm_state = STREAM_IDLE;
			strm_obj->user_event = pattr->user_event;
			if (pattr->stream_attr_in != NULL) {
				strm_obj->timeout =
				    pattr->stream_attr_in->timeout;
				strm_obj->num_bufs =
				    pattr->stream_attr_in->num_bufs;
				strm_obj->strm_mode =
				    pattr->stream_attr_in->strm_mode;
				strm_obj->segment_id =
				    pattr->stream_attr_in->segment_id;
				strm_obj->buf_alignment =
				    pattr->stream_attr_in->buf_alignment;
				strm_obj->dma_chnl_id =
				    pattr->stream_attr_in->dma_chnl_id;
				strm_obj->dma_priority =
				    pattr->stream_attr_in->dma_priority;
				chnl_attr_obj.uio_reqs =
				    pattr->stream_attr_in->num_bufs;
			} else {
				strm_obj->timeout = DEFAULTTIMEOUT;
				strm_obj->num_bufs = DEFAULTNUMBUFS;
				strm_obj->strm_mode = STRMMODE_PROCCOPY;
				strm_obj->segment_id = 0;	/* local mem */
				strm_obj->buf_alignment = 0;
				strm_obj->dma_chnl_id = 0;
				strm_obj->dma_priority = 0;
				chnl_attr_obj.uio_reqs = DEFAULTNUMBUFS;
			}
			chnl_attr_obj.reserved1 = NULL;
			/* DMA chnl flush timeout */
			chnl_attr_obj.reserved2 = strm_obj->timeout;
			chnl_attr_obj.event_obj = NULL;
			if (pattr->user_event != NULL)
				chnl_attr_obj.event_obj = pattr->user_event;

		}
	}
	if (status)
		goto func_cont;

	if ((pattr->virt_base == NULL) || !(pattr->virt_size > 0))
		goto func_cont;

	/* No System DMA */
	DBC_ASSERT(strm_obj->strm_mode != STRMMODE_LDMA);
	/* Get the shared mem mgr for this streams dev object */
	status = dev_get_cmm_mgr(strm_mgr_obj->dev_obj, &hcmm_mgr);
	if (!status) {
		/*Allocate a SM addr translator for this strm. */
		status = cmm_xlator_create(&strm_obj->xlator, hcmm_mgr, NULL);
		if (!status) {
			DBC_ASSERT(strm_obj->segment_id > 0);
			/*  Set translators Virt Addr attributes */
			status = cmm_xlator_info(strm_obj->xlator,
						 (u8 **) &pattr->virt_base,
						 pattr->virt_size,
						 strm_obj->segment_id, true);
		}
	}
func_cont:
	if (!status) {
		/* Open channel */
		chnl_mode = (dir == DSP_TONODE) ?
		    CHNL_MODETODSP : CHNL_MODEFROMDSP;
		intf_fxns = strm_mgr_obj->intf_fxns;
		status = (*intf_fxns->chnl_open) (&(strm_obj->chnl_obj),
						      strm_mgr_obj->chnl_mgr,
						      chnl_mode, ul_chnl_id,
						      &chnl_attr_obj);
		if (status) {
			/*
			 * over-ride non-returnable status codes so we return
			 * something documented
			 */
			if (status != -ENOMEM && status !=
			    -EINVAL && status != -EPERM) {
				/*
				 * We got a status that's not return-able.
				 * Assert that we got something we were
				 * expecting (-EFAULT isn't acceptable,
				 * strm_mgr_obj->chnl_mgr better be valid or we
				 * assert here), and then return -EPERM.
				 */
				DBC_ASSERT(status == -ENOSR ||
					   status == -ECHRNG ||
					   status == -EALREADY ||
					   status == -EIO);
				status = -EPERM;
			}
		}
	}
	if (!status) {
		status = drv_proc_insert_strm_res_element(strm_obj,
							&stream_res, pr_ctxt);
		if (status)
			delete_strm(strm_obj);
		else
			*strmres = (struct strm_res_object *)stream_res;
	} else {
		(void)delete_strm(strm_obj);
	}

	/* ensure we return a documented error code */
	DBC_ENSURE((!status && strm_obj) ||
		   (*strmres == NULL && (status == -EFAULT ||
					status == -EPERM
					|| status == -EINVAL)));

	dev_dbg(bridge, "%s: hnode: %p dir: 0x%x index: 0x%x pattr: %p "
		"strmres: %p status: 0x%x\n", __func__,
		hnode, dir, index, pattr, strmres, status);
	return status;
}

/*
 *  ======== strm_reclaim ========
 *  Purpose:
 *      Relcaims a buffer from a stream.
 */
int strm_reclaim(struct strm_object *stream_obj, u8 ** buf_ptr,
			u32 *nbytes, u32 *buff_size, u32 *pdw_arg)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_ioc chnl_ioc_obj;
	int status = 0;
	void *tmp_buf = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(buf_ptr != NULL);
	DBC_REQUIRE(nbytes != NULL);
	DBC_REQUIRE(pdw_arg != NULL);

	if (!stream_obj) {
		status = -EFAULT;
		goto func_end;
	}
	intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;

	status =
	    (*intf_fxns->chnl_get_ioc) (stream_obj->chnl_obj,
					    stream_obj->timeout,
					    &chnl_ioc_obj);
	if (!status) {
		*nbytes = chnl_ioc_obj.byte_size;
		if (buff_size)
			*buff_size = chnl_ioc_obj.buf_size;

		*pdw_arg = chnl_ioc_obj.arg;
		if (!CHNL_IS_IO_COMPLETE(chnl_ioc_obj)) {
			if (CHNL_IS_TIMED_OUT(chnl_ioc_obj)) {
				status = -ETIME;
			} else {
				/* Allow reclaims after idle to succeed */
				if (!CHNL_IS_IO_CANCELLED(chnl_ioc_obj))
					status = -EPERM;

			}
		}
		/* Translate zerocopy buffer if channel not canceled. */
		if (!status
		    && (!CHNL_IS_IO_CANCELLED(chnl_ioc_obj))
		    && (stream_obj->strm_mode == STRMMODE_ZEROCOPY)) {
			/*
			 *  This is a zero-copy channel so chnl_ioc_obj.buf
			 *  contains the DSP address of SM. We need to
			 *  translate it to a virtual address for the user
			 *  thread to access.
			 *  Note: Could add CMM_DSPPA2VA to CMM in the future.
			 */
			tmp_buf = cmm_xlator_translate(stream_obj->xlator,
						       chnl_ioc_obj.buf,
						       CMM_DSPPA2PA);
			if (tmp_buf != NULL) {
				/* now convert this GPP Pa to Va */
				tmp_buf = cmm_xlator_translate(stream_obj->
							       xlator,
							       tmp_buf,
							       CMM_PA2VA);
			}
			if (tmp_buf == NULL)
				status = -ESRCH;

			chnl_ioc_obj.buf = tmp_buf;
		}
		*buf_ptr = chnl_ioc_obj.buf;
	}
func_end:
	/* ensure we return a documented return code */
	DBC_ENSURE(!status || status == -EFAULT ||
		   status == -ETIME || status == -ESRCH ||
		   status == -EPERM);

	dev_dbg(bridge, "%s: stream_obj: %p buf_ptr: %p nbytes: %p "
		"pdw_arg: %p status 0x%x\n", __func__, stream_obj,
		buf_ptr, nbytes, pdw_arg, status);
	return status;
}

/*
 *  ======== strm_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 */
int strm_register_notify(struct strm_object *stream_obj, u32 event_mask,
				u32 notify_type, struct dsp_notification
				* hnotification)
{
	struct bridge_drv_interface *intf_fxns;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hnotification != NULL);

	if (!stream_obj) {
		status = -EFAULT;
	} else if ((event_mask & ~((DSP_STREAMIOCOMPLETION) |
				   DSP_STREAMDONE)) != 0) {
		status = -EINVAL;
	} else {
		if (notify_type != DSP_SIGNALEVENT)
			status = -ENOSYS;

	}
	if (!status) {
		intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;

		status =
		    (*intf_fxns->chnl_register_notify) (stream_obj->
							    chnl_obj,
							    event_mask,
							    notify_type,
							    hnotification);
	}
	/* ensure we return a documented return code */
	DBC_ENSURE(!status || status == -EFAULT ||
		   status == -ETIME || status == -ESRCH ||
		   status == -ENOSYS || status == -EPERM);
	return status;
}

/*
 *  ======== strm_select ========
 *  Purpose:
 *      Selects a ready stream.
 */
int strm_select(struct strm_object **strm_tab, u32 strms,
		       u32 *pmask, u32 utimeout)
{
	u32 index;
	struct chnl_info chnl_info_obj;
	struct bridge_drv_interface *intf_fxns;
	struct sync_object **sync_events = NULL;
	u32 i;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(strm_tab != NULL);
	DBC_REQUIRE(pmask != NULL);
	DBC_REQUIRE(strms > 0);

	*pmask = 0;
	for (i = 0; i < strms; i++) {
		if (!strm_tab[i]) {
			status = -EFAULT;
			break;
		}
	}
	if (status)
		goto func_end;

	/* Determine which channels have IO ready */
	for (i = 0; i < strms; i++) {
		intf_fxns = strm_tab[i]->strm_mgr_obj->intf_fxns;
		status = (*intf_fxns->chnl_get_info) (strm_tab[i]->chnl_obj,
							  &chnl_info_obj);
		if (status) {
			break;
		} else {
			if (chnl_info_obj.cio_cs > 0)
				*pmask |= (1 << i);

		}
	}
	if (!status && utimeout > 0 && *pmask == 0) {
		/* Non-zero timeout */
		sync_events = kmalloc(strms * sizeof(struct sync_object *),
								GFP_KERNEL);

		if (sync_events == NULL) {
			status = -ENOMEM;
		} else {
			for (i = 0; i < strms; i++) {
				intf_fxns =
				    strm_tab[i]->strm_mgr_obj->intf_fxns;
				status = (*intf_fxns->chnl_get_info)
				    (strm_tab[i]->chnl_obj, &chnl_info_obj);
				if (status)
					break;
				else
					sync_events[i] =
					    chnl_info_obj.sync_event;

			}
		}
		if (!status) {
			status =
			    sync_wait_on_multiple_events(sync_events, strms,
							 utimeout, &index);
			if (!status) {
				/* Since we waited on the event, we have to
				 * reset it */
				sync_set_event(sync_events[index]);
				*pmask = 1 << index;
			}
		}
	}
func_end:
	kfree(sync_events);

	DBC_ENSURE((!status && (*pmask != 0 || utimeout == 0)) ||
		   (status && *pmask == 0));

	return status;
}

/*
 *  ======== delete_strm ========
 *  Purpose:
 *      Frees the resources allocated for a stream.
 */
static int delete_strm(struct strm_object *stream_obj)
{
	struct bridge_drv_interface *intf_fxns;
	int status = 0;

	if (stream_obj) {
		if (stream_obj->chnl_obj) {
			intf_fxns = stream_obj->strm_mgr_obj->intf_fxns;
			/* Channel close can fail only if the channel handle
			 * is invalid. */
			status = (*intf_fxns->chnl_close)
					(stream_obj->chnl_obj);
		}
		/* Free all SM address translator resources */
		kfree(stream_obj->xlator);
		kfree(stream_obj);
	} else {
		status = -EFAULT;
	}
	return status;
}
