/*
 * dspchnl.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declares the upper edge channel class library functions required by
 * all Bridge driver / DSP API driver interface tables. These functions are
 * implemented by every class of Bridge channel library.
 *
 * Notes:
 *   The function comment headers reside in dspdefs.h.
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

#ifndef DSPCHNL_
#define DSPCHNL_

extern int bridge_chnl_create(struct chnl_mgr **channel_mgr,
				     struct dev_object *hdev_obj,
				     const struct chnl_mgrattrs
				     *mgr_attrts);

extern int bridge_chnl_destroy(struct chnl_mgr *hchnl_mgr);

extern int bridge_chnl_open(struct chnl_object **chnl,
				   struct chnl_mgr *hchnl_mgr,
				   s8 chnl_mode,
				   u32 ch_id,
				   const struct chnl_attr
				   *pattrs);

extern int bridge_chnl_close(struct chnl_object *chnl_obj);

extern int bridge_chnl_add_io_req(struct chnl_object *chnl_obj,
				      void *host_buf,
				      u32 byte_size, u32 buf_size,
				      u32 dw_dsp_addr, u32 dw_arg);

extern int bridge_chnl_get_ioc(struct chnl_object *chnl_obj,
				   u32 timeout, struct chnl_ioc *chan_ioc);

extern int bridge_chnl_cancel_io(struct chnl_object *chnl_obj);

extern int bridge_chnl_flush_io(struct chnl_object *chnl_obj,
				    u32 timeout);

extern int bridge_chnl_get_info(struct chnl_object *chnl_obj,
				    struct chnl_info *channel_info);

extern int bridge_chnl_get_mgr_info(struct chnl_mgr *hchnl_mgr,
					u32 ch_id, struct chnl_mgrinfo
					*mgr_info);

extern int bridge_chnl_idle(struct chnl_object *chnl_obj,
				   u32 timeout, bool flush_data);

extern int bridge_chnl_register_notify(struct chnl_object *chnl_obj,
					   u32 event_mask,
					   u32 notify_type,
					   struct dsp_notification
					   *hnotification);

#endif /* DSPCHNL_ */
