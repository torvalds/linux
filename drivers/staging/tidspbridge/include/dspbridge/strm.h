/*
 * strm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSPBridge Stream Manager.
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

#ifndef STRM_
#define STRM_

#include <dspbridge/dev.h>

#include <dspbridge/strmdefs.h>
#include <dspbridge/proc.h>

/*
 *  ======== strm_allocate_buffer ========
 *  Purpose:
 *      Allocate data buffer(s) for use with a stream.
 *  Parameter:
 *      strmres:     Stream resource info handle returned from strm_open().
 *      usize:          Size (GPP bytes) of the buffer(s).
 *      num_bufs:       Number of buffers to allocate.
 *      ap_buffer:       Array to hold buffer addresses.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream_obj.
 *      -ENOMEM:    Insufficient memory.
 *      -EPERM:      Failure occurred, unable to allocate buffers.
 *      -EINVAL:      usize must be > 0 bytes.
 *  Requires:
 *      strm_init(void) called.
 *      ap_buffer != NULL.
 *  Ensures:
 */
extern int strm_allocate_buffer(struct strm_res_object *strmres,
				       u32 usize,
				       u8 **ap_buffer,
				       u32 num_bufs,
				       struct process_context *pr_ctxt);

/*
 *  ======== strm_close ========
 *  Purpose:
 *      Close a stream opened with strm_open().
 *  Parameter:
 *      strmres:          Stream resource info handle returned from strm_open().
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream_obj.
 *      -EPIPE:   Some data buffers issued to the stream have not
 *                      been reclaimed.
 *      -EPERM:      Failure to close stream.
 *  Requires:
 *      strm_init(void) called.
 *  Ensures:
 */
extern int strm_close(struct strm_res_object *strmres,
			     struct process_context *pr_ctxt);

/*
 *  ======== strm_create ========
 *  Purpose:
 *      Create a STRM manager object. This object holds information about the
 *      device needed to open streams.
 *  Parameters:
 *      strm_man:       Location to store handle to STRM manager object on
 *                      output.
 *      dev_obj:           Device for this processor.
 *  Returns:
 *      0:        Success;
 *      -ENOMEM:    Insufficient memory for requested resources.
 *      -EPERM:      General failure.
 *  Requires:
 *      strm_init(void) called.
 *      strm_man != NULL.
 *      dev_obj != NULL.
 *  Ensures:
 *      0:        Valid *strm_man.
 *      error:          *strm_man == NULL.
 */
extern int strm_create(struct strm_mgr **strm_man,
			      struct dev_object *dev_obj);

/*
 *  ======== strm_delete ========
 *  Purpose:
 *      Delete the STRM Object.
 *  Parameters:
 *      strm_mgr_obj:       Handle to STRM manager object from strm_create.
 *  Returns:
 *  Requires:
 *      strm_init(void) called.
 *      Valid strm_mgr_obj.
 *  Ensures:
 *      strm_mgr_obj is not valid.
 */
extern void strm_delete(struct strm_mgr *strm_mgr_obj);

/*
 *  ======== strm_exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      strm_init(void) successfully called before.
 *  Ensures:
 */
extern void strm_exit(void);

/*
 *  ======== strm_free_buffer ========
 *  Purpose:
 *      Free buffer(s) allocated with strm_allocate_buffer.
 *  Parameter:
 *      strmres:     Stream resource info handle returned from strm_open().
 *      ap_buffer:       Array containing buffer addresses.
 *      num_bufs:       Number of buffers to be freed.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream handle.
 *      -EPERM:      Failure occurred, unable to free buffers.
 *  Requires:
 *      strm_init(void) called.
 *      ap_buffer != NULL.
 *  Ensures:
 */
extern int strm_free_buffer(struct strm_res_object *strmres,
				   u8 **ap_buffer, u32 num_bufs,
				   struct process_context *pr_ctxt);

/*
 *  ======== strm_get_info ========
 *  Purpose:
 *      Get information about a stream. User's dsp_streaminfo is contained
 *      in stream_info struct. stream_info also contains Bridge private info.
 *  Parameters:
 *      stream_obj:         Stream handle returned from strm_open().
 *      stream_info:        Location to store stream info on output.
 *      uSteamInfoSize:     Size of user's dsp_streaminfo structure.
 *  Returns:
 *      0:            Success.
 *      -EFAULT:        Invalid stream_obj.
 *      -EINVAL:          stream_info_size < sizeof(dsp_streaminfo).
 *      -EPERM:          Unable to get stream info.
 *  Requires:
 *      strm_init(void) called.
 *      stream_info != NULL.
 *  Ensures:
 */
extern int strm_get_info(struct strm_object *stream_obj,
				struct stream_info *stream_info,
				u32 stream_info_size);

/*
 *  ======== strm_idle ========
 *  Purpose:
 *      Idle a stream and optionally flush output data buffers.
 *      If this is an output stream and flush_data is TRUE, all data currently
 *      enqueued will be discarded.
 *      If this is an output stream and flush_data is FALSE, this function
 *      will block until all currently buffered data is output, or the timeout
 *      specified has been reached.
 *      After a successful call to strm_idle(), all buffers can immediately
 *      be reclaimed.
 *  Parameters:
 *      stream_obj:     Stream handle returned from strm_open().
 *      flush_data:     If TRUE, discard output buffers.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream_obj.
 *      -ETIME:   A timeout occurred before the stream could be idled.
 *      -EPERM:      Unable to idle stream.
 *  Requires:
 *      strm_init(void) called.
 *  Ensures:
 */
extern int strm_idle(struct strm_object *stream_obj, bool flush_data);

/*
 *  ======== strm_init ========
 *  Purpose:
 *      Initialize the STRM module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
extern bool strm_init(void);

/*
 *  ======== strm_issue ========
 *  Purpose:
 *      Send a buffer of data to a stream.
 *  Parameters:
 *      stream_obj:         Stream handle returned from strm_open().
 *      pbuf:               Pointer to buffer of data to be sent to the stream.
 *      ul_bytes:            Number of bytes of data in the buffer.
 *      ul_buf_size:          Actual buffer size in bytes.
 *      dw_arg:              A user argument that travels with the buffer.
 *  Returns:
 *      0:            Success.
 *      -EFAULT:        Invalid stream_obj.
 *      -ENOSR:    The stream is full.
 *      -EPERM:          Failure occurred, unable to issue buffer.
 *  Requires:
 *      strm_init(void) called.
 *      pbuf != NULL.
 *  Ensures:
 */
extern int strm_issue(struct strm_object *stream_obj, u8 * pbuf,
			     u32 ul_bytes, u32 ul_buf_size, u32 dw_arg);

/*
 *  ======== strm_open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task of
 *      DAIS socket node on the DSP.
 *  Parameters:
 *      hnode:          Node handle returned from node_allocate().
 *      dir:           DSP_TONODE or DSP_FROMNODE.
 *      index:         Stream index.
 *      pattr:          Pointer to structure containing attributes to be
 *                      applied to stream. Cannot be NULL.
 *      strmres:     Location to store stream resuorce info handle on output.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid hnode.
 *      -EPERM: Invalid direction.
 *              hnode is not a task or DAIS socket node.
 *              Unable to open stream.
 *      -EINVAL:     Invalid index.
 *  Requires:
 *      strm_init(void) called.
 *      strmres != NULL.
 *      pattr != NULL.
 *  Ensures:
 *      0:        *strmres is valid.
 *      error:          *strmres == NULL.
 */
extern int strm_open(struct node_object *hnode, u32 dir,
			    u32 index, struct strm_attr *pattr,
			    struct strm_res_object **strmres,
			    struct process_context *pr_ctxt);

/*
 *  ======== strm_reclaim ========
 *  Purpose:
 *      Request a buffer back from a stream.
 *  Parameters:
 *      stream_obj:          Stream handle returned from strm_open().
 *      buf_ptr:        Location to store pointer to reclaimed buffer.
 *      nbytes:         Location where number of bytes of data in the
 *                      buffer will be written.
 *      buff_size:      Location where actual buffer size will be written.
 *      pdw_arg:         Location where user argument that travels with
 *                      the buffer will be written.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream_obj.
 *      -ETIME:   A timeout occurred before a buffer could be
 *                      retrieved.
 *      -EPERM:      Failure occurred, unable to reclaim buffer.
 *  Requires:
 *      strm_init(void) called.
 *      buf_ptr != NULL.
 *      nbytes != NULL.
 *      pdw_arg != NULL.
 *  Ensures:
 */
extern int strm_reclaim(struct strm_object *stream_obj,
			       u8 **buf_ptr, u32 * nbytes,
			       u32 *buff_size, u32 *pdw_arg);

/*
 *  ======== strm_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 *  Parameters:
 *      stream_obj:     Stream handle returned by strm_open().
 *      event_mask:     Mask of types of events to be notified about.
 *      notify_type:    Type of notification to be sent.
 *      hnotification:  Handle to be used for notification.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    Invalid stream_obj.
 *      -ENOMEM:    Insufficient memory on GPP.
 *      -EINVAL:     event_mask is invalid.
 *      -ENOSYS:   Notification type specified by notify_type is not
 *                      supported.
 *  Requires:
 *      strm_init(void) called.
 *      hnotification != NULL.
 *  Ensures:
 */
extern int strm_register_notify(struct strm_object *stream_obj,
				       u32 event_mask, u32 notify_type,
				       struct dsp_notification
				       *hnotification);

/*
 *  ======== strm_select ========
 *  Purpose:
 *      Select a ready stream.
 *  Parameters:
 *      strm_tab:       Array of stream handles returned from strm_open().
 *      strms:          Number of stream handles in array.
 *      pmask:          Location to store mask of ready streams on output.
 *      utimeout:       Timeout value (milliseconds).
 *  Returns:
 *      0:        Success.
 *      -EDOM:     strms out of range.

 *      -EFAULT:    Invalid stream handle in array.
 *      -ETIME:   A timeout occurred before a stream became ready.
 *      -EPERM:      Failure occurred, unable to select a stream.
 *  Requires:
 *      strm_init(void) called.
 *      strm_tab != NULL.
 *      strms > 0.
 *      pmask != NULL.
 *  Ensures:
 *      0:        *pmask != 0 || utimeout == 0.
 *      Error:          *pmask == 0.
 */
extern int strm_select(struct strm_object **strm_tab,
			      u32 strms, u32 *pmask, u32 utimeout);

#endif /* STRM_ */
