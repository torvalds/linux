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

struct mei_device;
struct mei_msg_hdr;
struct mei_cl;

/**
 * enum mei_hbm_state - host bus message protocol state
 *
 * @MEI_HBM_IDLE : protocol not started
 * @MEI_HBM_START : start request message was sent
 * @MEI_HBM_ENUM_CLIENTS : enumeration request was sent
 * @MEI_HBM_CLIENT_PROPERTIES : acquiring clients properties
 */
enum mei_hbm_state {
	MEI_HBM_IDLE = 0,
	MEI_HBM_START,
	MEI_HBM_ENUM_CLIENTS,
	MEI_HBM_CLIENT_PROPERTIES,
	MEI_HBM_STARTED,
	MEI_HBM_STOP,
};

void mei_hbm_dispatch(struct mei_device *dev, struct mei_msg_hdr *hdr);

static inline void mei_hbm_hdr(struct mei_msg_hdr *hdr, size_t length)
{
	hdr->host_addr = 0;
	hdr->me_addr = 0;
	hdr->length = length;
	hdr->msg_complete = 1;
	hdr->reserved = 0;
}

int mei_hbm_start_req(struct mei_device *dev);
int mei_hbm_start_wait(struct mei_device *dev);
int mei_hbm_cl_flow_control_req(struct mei_device *dev, struct mei_cl *cl);
int mei_hbm_cl_disconnect_req(struct mei_device *dev, struct mei_cl *cl);
int mei_hbm_cl_connect_req(struct mei_device *dev, struct mei_cl *cl);
bool mei_hbm_version_is_supported(struct mei_device *dev);

#endif /* _MEI_HBM_H_ */

