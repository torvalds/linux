/*
 * dspdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Bridge driver entry point and interface function declarations.
 *
 * Notes:
 *   The DSP API obtains it's function interface to
 *   the Bridge driver via a call to bridge_drv_entry().
 *
 *   Bridge services exported to Bridge drivers are initialized by the
 *   DSP API on behalf of the Bridge driver.
 *
 *   Bridge function DBC Requires and Ensures are also made by the DSP API on
 *   behalf of the Bridge driver, to simplify the Bridge driver code.
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

#ifndef DSPDEFS_
#define DSPDEFS_

#include <dspbridge/brddefs.h>
#include <dspbridge/cfgdefs.h>
#include <dspbridge/chnlpriv.h>
#include <dspbridge/dehdefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/iodefs.h>
#include <dspbridge/msgdefs.h>

/*
 *  Any IOCTLS at or above this value are reserved for standard Bridge driver
 *  interfaces.
 */
#define BRD_RESERVEDIOCTLBASE   0x8000

/* Handle to Bridge driver's private device context. */
struct bridge_dev_context;

/*--------------------------------------------------------------------------- */
/* BRIDGE DRIVER FUNCTION TYPES */
/*--------------------------------------------------------------------------- */

/*
 *  ======== bridge_brd_monitor ========
 *  Purpose:
 *      Bring the board to the BRD_IDLE (monitor) state.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device context.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *  Ensures:
 *      0:        Board is in BRD_IDLE state;
 *      else:           Board state is indeterminate.
 */
typedef int(*fxn_brd_monitor) (struct bridge_dev_context *hDevContext);

/*
 *  ======== fxn_brd_setstate ========
 *  Purpose:
 *      Sets the Bridge driver state
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      ulBrdState:     Board state
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      ulBrdState  <= BRD_LASTSTATE.
 *  Ensures:
 *      ulBrdState  <= BRD_LASTSTATE.
 *  Update the Board state to the specified state.
 */
typedef int(*fxn_brd_setstate) (struct bridge_dev_context
				       * hDevContext, u32 ulBrdState);

/*
 *  ======== bridge_brd_start ========
 *  Purpose:
 *      Bring board to the BRD_RUNNING (start) state.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device context.
 *      dwDSPAddr:      DSP address at which to start execution.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *      Board is in monitor (BRD_IDLE) state.
 *  Ensures:
 *      0:        Board is in BRD_RUNNING state.
 *                      Interrupts to the PC are enabled.
 *      else:           Board state is indeterminate.
 */
typedef int(*fxn_brd_start) (struct bridge_dev_context
				    * hDevContext, u32 dwDSPAddr);

/*
 *  ======== bridge_brd_mem_copy ========
 *  Purpose:
 *  Copy memory from one DSP address to another
 *  Parameters:
 *      dev_context:    Pointer to context handle
 *  ulDspDestAddr:  DSP address to copy to
 *  ulDspSrcAddr:   DSP address to copy from
 *  ul_num_bytes: Number of bytes to copy
 *  ulMemType:  What section of memory to copy to
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      dev_context != NULL
 *  Ensures:
 *      0:        Board is in BRD_RUNNING state.
 *                      Interrupts to the PC are enabled.
 *      else:           Board state is indeterminate.
 */
typedef int(*fxn_brd_memcopy) (struct bridge_dev_context
				      * hDevContext,
				      u32 ulDspDestAddr,
				      u32 ulDspSrcAddr,
				      u32 ul_num_bytes, u32 ulMemType);
/*
 *  ======== bridge_brd_mem_write ========
 *  Purpose:
 *      Write a block of host memory into a DSP address, into a given memory
 *      space.  Unlike bridge_brd_write, this API does reset the DSP
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      dwDSPAddr:      Address on DSP board (Destination).
 *      pHostBuf:       Pointer to host buffer (Source).
 *      ul_num_bytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP to which to transfer.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 */
typedef int(*fxn_brd_memwrite) (struct bridge_dev_context
				       * hDevContext,
				       IN u8 *pHostBuf,
				       u32 dwDSPAddr, u32 ul_num_bytes,
				       u32 ulMemType);

/*
 *  ======== bridge_brd_mem_map ========
 *  Purpose:
 *      Map a MPU memory region to a DSP/IVA memory space
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      ul_mpu_addr:      MPU memory region start address.
 *      ulVirtAddr:     DSP/IVA memory region u8 address.
 *      ul_num_bytes:     Number of bytes to map.
 *      map_attrs:       Mapping attributes (e.g. endianness).
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 */
typedef int(*fxn_brd_memmap) (struct bridge_dev_context
				     * hDevContext, u32 ul_mpu_addr,
				     u32 ulVirtAddr, u32 ul_num_bytes,
				     u32 ulMapAttrs,
				     struct page **mapped_pages);

/*
 *  ======== bridge_brd_mem_un_map ========
 *  Purpose:
 *      UnMap an MPU memory region from DSP/IVA memory space
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      ulVirtAddr:     DSP/IVA memory region u8 address.
 *      ul_num_bytes:     Number of bytes to unmap.
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 */
typedef int(*fxn_brd_memunmap) (struct bridge_dev_context
				       * hDevContext,
				       u32 ulVirtAddr, u32 ul_num_bytes);

/*
 *  ======== bridge_brd_stop ========
 *  Purpose:
 *      Bring board to the BRD_STOPPED state.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device context.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *  Ensures:
 *      0:        Board is in BRD_STOPPED (stop) state;
 *                      Interrupts to the PC are disabled.
 *      else:           Board state is indeterminate.
 */
typedef int(*fxn_brd_stop) (struct bridge_dev_context *hDevContext);

/*
 *  ======== bridge_brd_status ========
 *  Purpose:
 *      Report the current state of the board.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device context.
 *      pdwState:       Ptr to BRD status variable.
 *  Returns:
 *      0:
 *  Requires:
 *      pdwState != NULL;
 *      hDevContext != NULL
 *  Ensures:
 *      *pdwState is one of {BRD_STOPPED, BRD_IDLE, BRD_RUNNING, BRD_UNKNOWN};
 */
typedef int(*fxn_brd_status) (struct bridge_dev_context *hDevContext,
				     int *pdwState);

/*
 *  ======== bridge_brd_read ========
 *  Purpose:
 *      Read a block of DSP memory, from a given memory space, into a host
 *      buffer.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      pHostBuf:       Pointer to host buffer (Destination).
 *      dwDSPAddr:      Address on DSP board (Source).
 *      ul_num_bytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP from which to transfer.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 *  Will not write more than ul_num_bytes bytes into pHostBuf.
 */
typedef int(*fxn_brd_read) (struct bridge_dev_context *hDevContext,
				   OUT u8 *pHostBuf,
				   u32 dwDSPAddr,
				   u32 ul_num_bytes, u32 ulMemType);

/*
 *  ======== bridge_brd_write ========
 *  Purpose:
 *      Write a block of host memory into a DSP address, into a given memory
 *      space.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      dwDSPAddr:      Address on DSP board (Destination).
 *      pHostBuf:       Pointer to host buffer (Source).
 *      ul_num_bytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP to which to transfer.
 *  Returns:
 *      0:        Success.
 *      -ETIMEDOUT:  Timeout occured waiting for a response from hardware.
 *      -EPERM:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 */
typedef int(*fxn_brd_write) (struct bridge_dev_context *hDevContext,
				    IN u8 *pHostBuf,
				    u32 dwDSPAddr,
				    u32 ul_num_bytes, u32 ulMemType);

/*
 *  ======== bridge_chnl_create ========
 *  Purpose:
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given 'Bridge board.
 *  Parameters:
 *      phChnlMgr:      Location to store a channel manager object on output.
 *      hdev_obj:     Handle to a device object.
 *      pMgrAttrs:      Channel manager attributes.
 *      pMgrAttrs->max_channels: Max channels
 *      pMgrAttrs->birq:      Channel's I/O IRQ number.
 *      pMgrAttrs->irq_shared:   TRUE if the IRQ is shareable.
 *      pMgrAttrs->word_size: DSP Word size in equivalent PC bytes..
 *      pMgrAttrs->shm_base:  Base physical address of shared memory, if any.
 *      pMgrAttrs->usm_length: Bytes of shared memory block.
 *  Returns:
 *      0:            Success;
 *      -ENOMEM:        Insufficient memory for requested resources.
 *      -EIO:         Unable to plug ISR for given IRQ.
 *      -EFAULT:    Couldn't map physical address to a virtual one.
 *  Requires:
 *      phChnlMgr != NULL.
 *      pMgrAttrs != NULL
 *      pMgrAttrs field are all valid:
 *          0 < max_channels <= CHNL_MAXCHANNELS.
 *          birq <= 15.
 *          word_size > 0.
 *      hdev_obj != NULL
 *      No channel manager exists for this board.
 *  Ensures:
 */
typedef int(*fxn_chnl_create) (OUT struct chnl_mgr
				      **phChnlMgr,
				      struct dev_object
				      * hdev_obj,
				      IN CONST struct
				      chnl_mgrattrs * pMgrAttrs);

/*
 *  ======== bridge_chnl_destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 *  Parameters:
 *      hchnl_mgr:       Channel manager object.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    hchnl_mgr was invalid.
 *  Requires:
 *  Ensures:
 *      0: Cancels I/O on each open channel. Closes each open channel.
 *          chnl_create may subsequently be called for the same device.
 */
typedef int(*fxn_chnl_destroy) (struct chnl_mgr *hchnl_mgr);
/*
 *  ======== bridge_deh_notify ========
 *  Purpose:
 *      When notified of DSP error, take appropriate action.
 *  Parameters:
 *      hdeh_mgr:        Handle to DEH manager object.
 *      ulEventMask:  Indicate the type of exception
 *      dwErrInfo:     Error information
 *  Returns:
 *
 *  Requires:
 *      hdeh_mgr != NULL;
 *     ulEventMask with a valid exception
 *  Ensures:
 */
typedef void (*fxn_deh_notify) (struct deh_mgr *hdeh_mgr,
				u32 ulEventMask, u32 dwErrInfo);

/*
 *  ======== bridge_chnl_open ========
 *  Purpose:
 *      Open a new half-duplex channel to the DSP board.
 *  Parameters:
 *      phChnl:         Location to store a channel object handle.
 *      hchnl_mgr:	Handle to channel manager, as returned by
 *      		CHNL_GetMgr().
 *      chnl_mode:          One of {CHNL_MODETODSP, CHNL_MODEFROMDSP} specifies
 *                      direction of data transfer.
 *      uChnlId:        If CHNL_PICKFREE is specified, the channel manager will
 *                      select a free channel id (default);
 *                      otherwise this field specifies the id of the channel.
 *      pattrs:         Channel attributes.  Attribute fields are as follows:
 *      pattrs->uio_reqs: Specifies the maximum number of I/O requests which can
 *                      be pending at any given time. All request packets are
 *                      preallocated when the channel is opened.
 *      pattrs->event_obj: This field allows the user to supply an auto reset
 *                      event object for channel I/O completion notifications.
 *                      It is the responsibility of the user to destroy this
 *                      object AFTER closing the channel.
 *                      This channel event object can be retrieved using
 *                      CHNL_GetEventHandle().
 *      pattrs->hReserved: The kernel mode handle of this event object.
 *
 *  Returns:
 *      0:                Success.
 *      -EFAULT:            hchnl_mgr is invalid.
 *      -ENOMEM:            Insufficient memory for requested resources.
 *      -EINVAL:        Invalid number of IOReqs.
 *      -ENOSR:    No free channels available.
 *      -ECHRNG:       Channel ID is out of range.
 *      -EALREADY:        Channel is in use.
 *      -EIO:         No free IO request packets available for
 *                              queuing.
 *  Requires:
 *      phChnl != NULL.
 *      pattrs != NULL.
 *      pattrs->event_obj is a valid event handle.
 *      pattrs->hReserved is the kernel mode handle for pattrs->event_obj.
 *  Ensures:
 *      0:                *phChnl is a valid channel.
 *      else:                   *phChnl is set to NULL if (phChnl != NULL);
 */
typedef int(*fxn_chnl_open) (OUT struct chnl_object
				    **phChnl,
				    struct chnl_mgr *hchnl_mgr,
				    s8 chnl_mode,
				    u32 uChnlId,
				    CONST IN OPTIONAL struct
				    chnl_attr * pattrs);

/*
 *  ======== bridge_chnl_close ========
 *  Purpose:
 *      Ensures all pending I/O on this channel is cancelled, discards all
 *      queued I/O completion notifications, then frees the resources allocated
 *      for this channel, and makes the corresponding logical channel id
 *      available for subsequent use.
 *  Parameters:
 *      chnl_obj:          Handle to a channel object.
 *  Returns:
 *      0:        Success;
 *      -EFAULT:    Invalid chnl_obj.
 *  Requires:
 *      No thread must be blocked on this channel's I/O completion event.
 *  Ensures:
 *      0:        chnl_obj is no longer valid.
 */
typedef int(*fxn_chnl_close) (struct chnl_object *chnl_obj);

/*
 *  ======== bridge_chnl_add_io_req ========
 *  Purpose:
 *      Enqueue an I/O request for data transfer on a channel to the DSP.
 *      The direction (mode) is specified in the channel object. Note the DSP
 *      address is specified for channels opened in direct I/O mode.
 *  Parameters:
 *      chnl_obj:          Channel object handle.
 *      pHostBuf:       Host buffer address source.
 *      byte_size:	Number of PC bytes to transfer. A zero value indicates
 *                      that this buffer is the last in the output channel.
 *                      A zero value is invalid for an input channel.
 *!     buf_size:       Actual buffer size in host bytes.
 *      dw_dsp_addr:      DSP address for transfer.  (Currently ignored).
 *      dw_arg:          A user argument that travels with the buffer.
 *  Returns:
 *      0:        Success;
 *      -EFAULT: Invalid chnl_obj or pHostBuf.
 *      -EPERM:   User cannot mark EOS on an input channel.
 *      -ECANCELED: I/O has been cancelled on this channel.  No further
 *                      I/O is allowed.
 *      -EPIPE:     End of stream was already marked on a previous
 *                      IORequest on this channel.  No further I/O is expected.
 *      -EINVAL: Buffer submitted to this output channel is larger than
 *                      the size of the physical shared memory output window.
 *  Requires:
 *  Ensures:
 *      0: The buffer will be transferred if the channel is ready;
 *          otherwise, will be queued for transfer when the channel becomes
 *          ready.  In any case, notifications of I/O completion are
 *          asynchronous.
 *          If byte_size is 0 for an output channel, subsequent CHNL_AddIOReq's
 *          on this channel will fail with error code -EPIPE.  The
 *          corresponding IOC for this I/O request will have its status flag
 *          set to CHNL_IOCSTATEOS.
 */
typedef int(*fxn_chnl_addioreq) (struct chnl_object
					* chnl_obj,
					void *pHostBuf,
					u32 byte_size,
					u32 buf_size,
					OPTIONAL u32 dw_dsp_addr, u32 dw_arg);

/*
 *  ======== bridge_chnl_get_ioc ========
 *  Purpose:
 *      Dequeue an I/O completion record, which contains information about the
 *      completed I/O request.
 *  Parameters:
 *      chnl_obj:          Channel object handle.
 *      dwTimeOut:      A value of CHNL_IOCNOWAIT will simply dequeue the
 *                      first available IOC.
 *      pIOC:           On output, contains host buffer address, bytes
 *                      transferred, and status of I/O completion.
 *      pIOC->status:   See chnldefs.h.
 *  Returns:
 *      0:        Success.
 *      -EFAULT: Invalid chnl_obj or pIOC.
 *      -EREMOTEIO:   CHNL_IOCNOWAIT was specified as the dwTimeOut parameter
 *                      yet no I/O completions were queued.
 *  Requires:
 *      dwTimeOut == CHNL_IOCNOWAIT.
 *  Ensures:
 *      0: if there are any remaining IOC's queued before this call
 *          returns, the channel event object will be left in a signalled
 *          state.
 */
typedef int(*fxn_chnl_getioc) (struct chnl_object *chnl_obj,
				      u32 dwTimeOut,
				      OUT struct chnl_ioc *pIOC);

/*
 *  ======== bridge_chnl_cancel_io ========
 *  Purpose:
 *      Return all I/O requests to the client which have not yet been
 *      transferred.  The channel's I/O completion object is
 *      signalled, and all the I/O requests are queued as IOC's, with the
 *      status field set to CHNL_IOCSTATCANCEL.
 *      This call is typically used in abort situations, and is a prelude to
 *      chnl_close();
 *  Parameters:
 *      chnl_obj:          Channel object handle.
 *  Returns:
 *      0:        Success;
 *      -EFAULT:    Invalid chnl_obj.
 *  Requires:
 *  Ensures:
 *      Subsequent I/O requests to this channel will not be accepted.
 */
typedef int(*fxn_chnl_cancelio) (struct chnl_object *chnl_obj);

/*
 *  ======== bridge_chnl_flush_io ========
 *  Purpose:
 *      For an output stream (to the DSP), indicates if any IO requests are in
 *      the output request queue.  For input streams (from the DSP), will
 *      cancel all pending IO requests.
 *  Parameters:
 *      chnl_obj:              Channel object handle.
 *      dwTimeOut:          Timeout value for flush operation.
 *  Returns:
 *      0:            Success;
 *      S_CHNLIOREQUEST:    Returned if any IORequests are in the output queue.
 *      -EFAULT:        Invalid chnl_obj.
 *  Requires:
 *  Ensures:
 *      0:            No I/O requests will be pending on this channel.
 */
typedef int(*fxn_chnl_flushio) (struct chnl_object *chnl_obj,
				       u32 dwTimeOut);

/*
 *  ======== bridge_chnl_get_info ========
 *  Purpose:
 *      Retrieve information related to a channel.
 *  Parameters:
 *      chnl_obj:          Handle to a valid channel object, or NULL.
 *      pInfo:          Location to store channel info.
 *  Returns:
 *      0:        Success;
 *      -EFAULT: Invalid chnl_obj or pInfo.
 *  Requires:
 *  Ensures:
 *      0:        pInfo points to a filled in chnl_info struct,
 *                      if (pInfo != NULL).
 */
typedef int(*fxn_chnl_getinfo) (struct chnl_object *chnl_obj,
				       OUT struct chnl_info *pChnlInfo);

/*
 *  ======== bridge_chnl_get_mgr_info ========
 *  Purpose:
 *      Retrieve information related to the channel manager.
 *  Parameters:
 *      hchnl_mgr:           Handle to a valid channel manager, or NULL.
 *      uChnlID:            Channel ID.
 *      pMgrInfo:           Location to store channel manager info.
 *  Returns:
 *      0:            Success;
 *      -EFAULT: Invalid hchnl_mgr or pMgrInfo.
 *      -ECHRNG:   Invalid channel ID.
 *  Requires:
 *  Ensures:
 *      0:            pMgrInfo points to a filled in chnl_mgrinfo
 *                          struct, if (pMgrInfo != NULL).
 */
typedef int(*fxn_chnl_getmgrinfo) (struct chnl_mgr
					  * hchnl_mgr,
					  u32 uChnlID,
					  OUT struct chnl_mgrinfo *pMgrInfo);

/*
 *  ======== bridge_chnl_idle ========
 *  Purpose:
 *      Idle a channel. If this is an input channel, or if this is an output
 *      channel and fFlush is TRUE, all currently enqueued buffers will be
 *      dequeued (data discarded for output channel).
 *      If this is an output channel and fFlush is FALSE, this function
 *      will block until all currently buffered data is output, or the timeout
 *      specified has been reached.
 *
 *  Parameters:
 *      chnl_obj:          Channel object handle.
 *      dwTimeOut:      If output channel and fFlush is FALSE, timeout value
 *                      to wait for buffers to be output. (Not used for
 *                      input channel).
 *      fFlush:         If output channel and fFlush is TRUE, discard any
 *                      currently buffered data. If FALSE, wait for currently
 *                      buffered data to be output, or timeout, whichever
 *                      occurs first. fFlush is ignored for input channel.
 *  Returns:
 *      0:            Success;
 *      -EFAULT:        Invalid chnl_obj.
 *      -ETIMEDOUT: Timeout occured before channel could be idled.
 *  Requires:
 *  Ensures:
 */
typedef int(*fxn_chnl_idle) (struct chnl_object *chnl_obj,
				    u32 dwTimeOut, bool fFlush);

/*
 *  ======== bridge_chnl_register_notify ========
 *  Purpose:
 *      Register for notification of events on a channel.
 *  Parameters:
 *      chnl_obj:          Channel object handle.
 *      event_mask:     Type of events to be notified about: IO completion
 *                      (DSP_STREAMIOCOMPLETION) or end of stream
 *                      (DSP_STREAMDONE).
 *      notify_type:    DSP_SIGNALEVENT.
 *      hnotification:  Handle of a dsp_notification object.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Insufficient memory.
 *      -EINVAL:     event_mask is 0 and hnotification was not
 *                      previously registered.
 *      -EFAULT:    NULL hnotification, hnotification event name
 *                      too long, or hnotification event name NULL.
 *  Requires:
 *      Valid chnl_obj.
 *      hnotification != NULL.
 *      (event_mask & ~(DSP_STREAMIOCOMPLETION | DSP_STREAMDONE)) == 0.
 *      notify_type == DSP_SIGNALEVENT.
 *  Ensures:
 */
typedef int(*fxn_chnl_registernotify)
 (struct chnl_object *chnl_obj,
  u32 event_mask, u32 notify_type, struct dsp_notification *hnotification);

/*
 *  ======== bridge_dev_create ========
 *  Purpose:
 *      Complete creation of the device object for this board.
 *  Parameters:
 *      phDevContext:   Ptr to location to store a Bridge device context.
 *      hdev_obj:     Handle to a Device Object, created and managed by DSP API.
 *      pConfig:        Ptr to configuration parameters provided by the
 *                      Configuration Manager during device loading.
 *      pDspConfig:     DSP resources, as specified in the registry key for this
 *                      device.
 *  Returns:
 *      0:            Success.
 *      -ENOMEM:        Unable to allocate memory for device context.
 *  Requires:
 *      phDevContext != NULL;
 *      hdev_obj != NULL;
 *      pConfig != NULL;
 *      pDspConfig != NULL;
 *      Fields in pConfig and pDspConfig contain valid values.
 *  Ensures:
 *      0:        All Bridge driver specific DSP resource and other
 *                      board context has been allocated.
 *      -ENOMEM:    Bridge failed to allocate resources.
 *                      Any acquired resources have been freed.  The DSP API
 *                      will not call bridge_dev_destroy() if
 *                      bridge_dev_create() fails.
 *  Details:
 *      Called during the CONFIGMG's Device_Init phase. Based on host and
 *      DSP configuration information, create a board context, a handle to
 *      which is passed into other Bridge BRD and CHNL functions.  The
 *      board context contains state information for the device. Since the
 *      addresses of all IN pointer parameters may be invalid when this
 *      function returns, they must not be stored into the device context
 *      structure.
 */
typedef int(*fxn_dev_create) (OUT struct bridge_dev_context
				     **phDevContext,
				     struct dev_object
				     * hdev_obj,
				     IN struct cfg_hostres
				     * pConfig);

/*
 *  ======== bridge_dev_ctrl ========
 *  Purpose:
 *      Bridge driver specific interface.
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device info.
 *      dw_cmd:          Bridge driver defined command code.
 *      pargs:          Pointer to an arbitrary argument structure.
 *  Returns:
 *      0 or -EPERM. Actual command error codes should be passed back in
 *      the pargs structure, and are defined by the Bridge driver implementor.
 *  Requires:
 *      All calls are currently assumed to be synchronous.  There are no
 *      IOCTL completion routines provided.
 *  Ensures:
 */
typedef int(*fxn_dev_ctrl) (struct bridge_dev_context *hDevContext,
				   u32 dw_cmd, IN OUT void *pargs);

/*
 *  ======== bridge_dev_destroy ========
 *  Purpose:
 *      Deallocate Bridge device extension structures and all other resources
 *      acquired by the Bridge driver.
 *      No calls to other Bridge driver functions may subsequently
 *      occur, except for bridge_dev_create().
 *  Parameters:
 *      hDevContext:    Handle to Bridge driver defined device information.
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Failed to release a resource previously acquired.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 *      0: Device context is freed.
 */
typedef int(*fxn_dev_destroy) (struct bridge_dev_context *hDevContext);

/*
 *  ======== bridge_deh_create ========
 *  Purpose:
 *      Create an object that manages DSP exceptions from the GPP.
 *  Parameters:
 *      phDehMgr:       Location to store DEH manager on output.
 *      hdev_obj:     Handle to DEV object.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Memory allocation failure.
 *      -EPERM:      Creation failed.
 *  Requires:
 *      hdev_obj != NULL;
 *      phDehMgr != NULL;
 *  Ensures:
 */
typedef int(*fxn_deh_create) (OUT struct deh_mgr
				     **phDehMgr, struct dev_object *hdev_obj);

/*
 *  ======== bridge_deh_destroy ========
 *  Purpose:
 *      Destroy the DEH object.
 *  Parameters:
 *      hdeh_mgr:        Handle to DEH manager object.
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Destroy failed.
 *  Requires:
 *      hdeh_mgr != NULL;
 *  Ensures:
 */
typedef int(*fxn_deh_destroy) (struct deh_mgr *hdeh_mgr);

/*
 *  ======== bridge_deh_register_notify ========
 *  Purpose:
 *      Register for DEH event notification.
 *  Parameters:
 *      hdeh_mgr:        Handle to DEH manager object.
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Destroy failed.
 *  Requires:
 *      hdeh_mgr != NULL;
 *  Ensures:
 */
typedef int(*fxn_deh_registernotify)
 (struct deh_mgr *hdeh_mgr,
  u32 event_mask, u32 notify_type, struct dsp_notification *hnotification);

/*
 *  ======== bridge_deh_get_info ========
 *  Purpose:
 *      Get DSP exception info.
 *  Parameters:
 *      phDehMgr:       Location to store DEH manager on output.
 *      pErrInfo:       Ptr to error info structure.
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Creation failed.
 *  Requires:
 *      phDehMgr != NULL;
 *      pErrorInfo != NULL;
 *  Ensures:
 */
typedef int(*fxn_deh_getinfo) (struct deh_mgr *phDehMgr,
				      struct dsp_errorinfo *pErrInfo);

/*
 *  ======== bridge_io_create ========
 *  Purpose:
 *      Create an object that manages I/O between CHNL and msg_ctrl.
 *  Parameters:
 *      phIOMgr:        Location to store IO manager on output.
 *      hchnl_mgr:       Handle to channel manager.
 *      hmsg_mgr:        Handle to message manager.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Memory allocation failure.
 *      -EPERM:      Creation failed.
 *  Requires:
 *      hdev_obj != NULL;
 *      Channel manager already created;
 *      Message manager already created;
 *      pMgrAttrs != NULL;
 *      phIOMgr != NULL;
 *  Ensures:
 */
typedef int(*fxn_io_create) (OUT struct io_mgr **phIOMgr,
				    struct dev_object *hdev_obj,
				    IN CONST struct io_attrs *pMgrAttrs);

/*
 *  ======== bridge_io_destroy ========
 *  Purpose:
 *      Destroy object created in bridge_io_create.
 *  Parameters:
 *      hio_mgr:         IO Manager.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Memory allocation failure.
 *      -EPERM:      Creation failed.
 *  Requires:
 *      Valid hio_mgr;
 *  Ensures:
 */
typedef int(*fxn_io_destroy) (struct io_mgr *hio_mgr);

/*
 *  ======== bridge_io_on_loaded ========
 *  Purpose:
 *      Called whenever a program is loaded to update internal data. For
 *      example, if shared memory is used, this function would update the
 *      shared memory location and address.
 *  Parameters:
 *      hio_mgr:     IO Manager.
 *  Returns:
 *      0:    Success.
 *      -EPERM:  Internal failure occurred.
 *  Requires:
 *      Valid hio_mgr;
 *  Ensures:
 */
typedef int(*fxn_io_onloaded) (struct io_mgr *hio_mgr);

/*
 *  ======== fxn_io_getprocload ========
 *  Purpose:
 *      Called to get the Processor's current and predicted load
 *  Parameters:
 *      hio_mgr:     IO Manager.
 *      pProcLoadStat   Processor Load statistics
 *  Returns:
 *      0:    Success.
 *      -EPERM:  Internal failure occurred.
 *  Requires:
 *      Valid hio_mgr;
 *  Ensures:
 */
typedef int(*fxn_io_getprocload) (struct io_mgr *hio_mgr,
					 struct dsp_procloadstat *
					 pProcLoadStat);

/*
 *  ======== bridge_msg_create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 *  Parameters:
 *      phMsgMgr:           Location to store msg_ctrl manager on output.
 *      hdev_obj:         Handle to a device object.
 *      msgCallback:        Called whenever an RMS_EXIT message is received.
 *  Returns:
 *      0:            Success.
 *      -ENOMEM:        Insufficient memory.
 *  Requires:
 *      phMsgMgr != NULL.
 *      msgCallback != NULL.
 *      hdev_obj != NULL.
 *  Ensures:
 */
typedef int(*fxn_msg_create)
 (OUT struct msg_mgr **phMsgMgr,
  struct dev_object *hdev_obj, msg_onexit msgCallback);

/*
 *  ======== bridge_msg_create_queue ========
 *  Purpose:
 *      Create a msg_ctrl queue for sending or receiving messages from a Message
 *      node on the DSP.
 *  Parameters:
 *      hmsg_mgr:            msg_ctrl queue manager handle returned from
 *                          bridge_msg_create.
 *      phMsgQueue:         Location to store msg_ctrl queue on output.
 *      msgq_id:	    Identifier for messages (node environment pointer).
 *      max_msgs:           Max number of simultaneous messages for the node.
 *      h:                  Handle passed to hmsg_mgr->msgCallback().
 *  Returns:
 *      0:            Success.
 *      -ENOMEM:        Insufficient memory.
 *  Requires:
 *      phMsgQueue != NULL.
 *      h != NULL.
 *      max_msgs > 0.
 *  Ensures:
 *      phMsgQueue !=NULL <==> 0.
 */
typedef int(*fxn_msg_createqueue)
 (struct msg_mgr *hmsg_mgr,
  OUT struct msg_queue **phMsgQueue, u32 msgq_id, u32 max_msgs, void *h);

/*
 *  ======== bridge_msg_delete ========
 *  Purpose:
 *      Delete a msg_ctrl manager allocated in bridge_msg_create().
 *  Parameters:
 *      hmsg_mgr:    Handle returned from bridge_msg_create().
 *  Returns:
 *  Requires:
 *      Valid hmsg_mgr.
 *  Ensures:
 */
typedef void (*fxn_msg_delete) (struct msg_mgr *hmsg_mgr);

/*
 *  ======== bridge_msg_delete_queue ========
 *  Purpose:
 *      Delete a msg_ctrl queue allocated in bridge_msg_create_queue.
 *  Parameters:
 *      msg_queue_obj:  Handle to msg_ctrl queue returned from
 *                  bridge_msg_create_queue.
 *  Returns:
 *  Requires:
 *      Valid msg_queue_obj.
 *  Ensures:
 */
typedef void (*fxn_msg_deletequeue) (struct msg_queue *msg_queue_obj);

/*
 *  ======== bridge_msg_get ========
 *  Purpose:
 *      Get a message from a msg_ctrl queue.
 *  Parameters:
 *      msg_queue_obj:     Handle to msg_ctrl queue returned from
 *                     bridge_msg_create_queue.
 *      pmsg:          Location to copy message into.
 *      utimeout:      Timeout to wait for a message.
 *  Returns:
 *      0:       Success.
 *      -ETIME:  Timeout occurred.
 *      -EPERM:     No frames available for message (max_msgs too
 *                     small).
 *  Requires:
 *      Valid msg_queue_obj.
 *      pmsg != NULL.
 *  Ensures:
 */
typedef int(*fxn_msg_get) (struct msg_queue *msg_queue_obj,
				  struct dsp_msg *pmsg, u32 utimeout);

/*
 *  ======== bridge_msg_put ========
 *  Purpose:
 *      Put a message onto a msg_ctrl queue.
 *  Parameters:
 *      msg_queue_obj:      Handle to msg_ctrl queue returned from
 *                      bridge_msg_create_queue.
 *      pmsg:           Pointer to message.
 *      utimeout:       Timeout to wait for a message.
 *  Returns:
 *      0:        Success.
 *      -ETIME:   Timeout occurred.
 *      -EPERM:      No frames available for message (max_msgs too
 *                      small).
 *  Requires:
 *      Valid msg_queue_obj.
 *      pmsg != NULL.
 *  Ensures:
 */
typedef int(*fxn_msg_put) (struct msg_queue *msg_queue_obj,
				  IN CONST struct dsp_msg *pmsg, u32 utimeout);

/*
 *  ======== bridge_msg_register_notify ========
 *  Purpose:
 *      Register notification for when a message is ready.
 *  Parameters:
 *      msg_queue_obj:      Handle to msg_ctrl queue returned from
 *                      bridge_msg_create_queue.
 *      event_mask:     Type of events to be notified about: Must be
 *                      DSP_NODEMESSAGEREADY, or 0 to unregister.
 *      notify_type:    DSP_SIGNALEVENT.
 *      hnotification:  Handle of notification object.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Insufficient memory.
 *  Requires:
 *      Valid msg_queue_obj.
 *      hnotification != NULL.
 *      notify_type == DSP_SIGNALEVENT.
 *      event_mask == DSP_NODEMESSAGEREADY || event_mask == 0.
 *  Ensures:
 */
typedef int(*fxn_msg_registernotify)
 (struct msg_queue *msg_queue_obj,
  u32 event_mask, u32 notify_type, struct dsp_notification *hnotification);

/*
 *  ======== bridge_msg_set_queue_id ========
 *  Purpose:
 *      Set message queue id to node environment. Allows bridge_msg_create_queue
 *      to be called in node_allocate, before the node environment is known.
 *  Parameters:
 *      msg_queue_obj:  Handle to msg_ctrl queue returned from
 *                  bridge_msg_create_queue.
 *      msgq_id:       Node environment pointer.
 *  Returns:
 *  Requires:
 *      Valid msg_queue_obj.
 *      msgq_id != 0.
 *  Ensures:
 */
typedef void (*fxn_msg_setqueueid) (struct msg_queue *msg_queue_obj,
				    u32 msgq_id);

/*
 *  Bridge Driver interface function table.
 *
 *  The information in this table is filled in by the specific Bridge driver,
 *  and copied into the DSP API's own space.  If any interface
 *  function field is set to a value of NULL, then the DSP API will
 *  consider that function not implemented, and return the error code
 *  -ENOSYS when a Bridge driver client attempts to call that function.
 *
 *  This function table contains DSP API version numbers, which are used by the
 *  Bridge driver loader to help ensure backwards compatility between older
 *  Bridge drivers and newer DSP API.  These must be set to
 *  BRD_API_MAJOR_VERSION and BRD_API_MINOR_VERSION, respectively.
 *
 *  A Bridge driver need not export a CHNL interface.  In this case, *all* of
 *  the bridge_chnl_* entries must be set to NULL.
 */
struct bridge_drv_interface {
	u32 brd_api_major_version;	/* Set to BRD_API_MAJOR_VERSION. */
	u32 brd_api_minor_version;	/* Set to BRD_API_MINOR_VERSION. */
	fxn_dev_create pfn_dev_create;	/* Create device context */
	fxn_dev_destroy pfn_dev_destroy;	/* Destroy device context */
	fxn_dev_ctrl pfn_dev_cntrl;	/* Optional vendor interface */
	fxn_brd_monitor pfn_brd_monitor;	/* Load and/or start monitor */
	fxn_brd_start pfn_brd_start;	/* Start DSP program. */
	fxn_brd_stop pfn_brd_stop;	/* Stop/reset board. */
	fxn_brd_status pfn_brd_status;	/* Get current board status. */
	fxn_brd_read pfn_brd_read;	/* Read board memory */
	fxn_brd_write pfn_brd_write;	/* Write board memory. */
	fxn_brd_setstate pfn_brd_set_state;	/* Sets the Board State */
	fxn_brd_memcopy pfn_brd_mem_copy;	/* Copies DSP Memory */
	fxn_brd_memwrite pfn_brd_mem_write;	/* Write DSP Memory w/o halt */
	fxn_brd_memmap pfn_brd_mem_map;	/* Maps MPU mem to DSP mem */
	fxn_brd_memunmap pfn_brd_mem_un_map;	/* Unmaps MPU mem to DSP mem */
	fxn_chnl_create pfn_chnl_create;	/* Create channel manager. */
	fxn_chnl_destroy pfn_chnl_destroy;	/* Destroy channel manager. */
	fxn_chnl_open pfn_chnl_open;	/* Create a new channel. */
	fxn_chnl_close pfn_chnl_close;	/* Close a channel. */
	fxn_chnl_addioreq pfn_chnl_add_io_req;	/* Req I/O on a channel. */
	fxn_chnl_getioc pfn_chnl_get_ioc;	/* Wait for I/O completion. */
	fxn_chnl_cancelio pfn_chnl_cancel_io;	/* Cancl I/O on a channel. */
	fxn_chnl_flushio pfn_chnl_flush_io;	/* Flush I/O. */
	fxn_chnl_getinfo pfn_chnl_get_info;	/* Get channel specific info */
	/* Get channel manager info. */
	fxn_chnl_getmgrinfo pfn_chnl_get_mgr_info;
	fxn_chnl_idle pfn_chnl_idle;	/* Idle the channel */
	/* Register for notif. */
	fxn_chnl_registernotify pfn_chnl_register_notify;
	fxn_deh_create pfn_deh_create;	/* Create DEH manager */
	fxn_deh_destroy pfn_deh_destroy;	/* Destroy DEH manager */
	fxn_deh_notify pfn_deh_notify;	/* Notify of DSP error */
	/* register for deh notif. */
	fxn_deh_registernotify pfn_deh_register_notify;
	fxn_deh_getinfo pfn_deh_get_info;	/* register for deh notif. */
	fxn_io_create pfn_io_create;	/* Create IO manager */
	fxn_io_destroy pfn_io_destroy;	/* Destroy IO manager */
	fxn_io_onloaded pfn_io_on_loaded;	/* Notify of program loaded */
	/* Get Processor's current and predicted load */
	fxn_io_getprocload pfn_io_get_proc_load;
	fxn_msg_create pfn_msg_create;	/* Create message manager */
	/* Create message queue */
	fxn_msg_createqueue pfn_msg_create_queue;
	fxn_msg_delete pfn_msg_delete;	/* Delete message manager */
	/* Delete message queue */
	fxn_msg_deletequeue pfn_msg_delete_queue;
	fxn_msg_get pfn_msg_get;	/* Get a message */
	fxn_msg_put pfn_msg_put;	/* Send a message */
	/* Register for notif. */
	fxn_msg_registernotify pfn_msg_register_notify;
	/* Set message queue id */
	fxn_msg_setqueueid pfn_msg_set_queue_id;
};

/*
 *  ======== bridge_drv_entry ========
 *  Purpose:
 *      Registers Bridge driver functions with the DSP API. Called only once
 *      by the DSP API.  The caller will first check DSP API version
 *      compatibility, and then copy the interface functions into its own
 *      memory space.
 *  Parameters:
 *      ppDrvInterface  Pointer to a location to receive a pointer to the
 *                      Bridge driver interface.
 *  Returns:
 *  Requires:
 *      The code segment this function resides in must expect to be discarded
 *      after completion.
 *  Ensures:
 *      ppDrvInterface pointer initialized to Bridge driver's function
 *      interface. No system resources are acquired by this function.
 *  Details:
 *      Called during the Device_Init phase.
 */
void bridge_drv_entry(OUT struct bridge_drv_interface **ppDrvInterface,
		   IN CONST char *driver_file_name);

#endif /* DSPDEFS_ */
