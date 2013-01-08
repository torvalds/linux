/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _MEI_HBM_H_
#define _MEI_HBM_H_

void mei_hbm_dispatch(struct mei_device *dev, struct mei_msg_hdr *hdr);

static inline void mei_hbm_hdr(struct mei_msg_hdr *hdr, size_t length)
{
	hdr->host_addr = 0;
	hdr->me_addr = 0;
	hdr->length = length;
	hdr->msg_complete = 1;
	hdr->reserved = 0;
}

void mei_hbm_start_req(struct mei_device *dev);

int mei_hbm_cl_flow_control_req(struct mei_device *dev, struct mei_cl *cl);
int mei_hbm_cl_disconnect_req(struct mei_device *dev, struct mei_cl *cl);
int mei_hbm_cl_connect_req(struct mei_device *dev, struct mei_cl *cl);


#endif /* _MEI_HBM_H_ */

