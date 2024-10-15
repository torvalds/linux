/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2011-2017, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __U_RMNET_H
#define __U_RMNET_H

#include <linux/usb/composite.h>
#include <linux/usb/cdc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "f_qdss.h"

enum bam_dmux_func_type {
	BAM_DMUX_FUNC_RMNET,
	BAM_DMUX_FUNC_MBIM = 0,
	BAM_DMUX_FUNC_DPL,
	BAM_DMUX_NUM_FUNCS,
};

struct rmnet_ctrl_pkt {
	void	*buf;
	int	len;
	struct list_head	list;
};

struct data_port {
	struct usb_composite_dev	*cdev;
	struct usb_function		*func;
	int				rx_buffer_size;
	struct usb_ep			*in;
	struct usb_ep			*out;
	int				ipa_consumer_ep;
	int				ipa_producer_ep;
	const struct usb_endpoint_descriptor	*in_ep_desc_backup;
	const struct usb_endpoint_descriptor	*out_ep_desc_backup;

};

struct grmnet {
	/* to usb host, aka laptop, windows pc etc. Will
	 * be filled by usb driver of rmnet functionality
	 */
	int (*send_cpkt_response)(void *g, void *buf, size_t len);

	/* to modem, and to be filled by driver implementing
	 * control function
	 */
	int (*send_encap_cmd)(enum qti_port_type qport, void *buf, size_t len);
	void (*notify_modem)(void *g, enum qti_port_type qport, int cbits);

	void (*disconnect)(struct grmnet *g);
	void (*connect)(struct grmnet *g);
};

enum ctrl_client {
	FRMNET_CTRL_CLIENT,
	GPS_CTRL_CLIENT,

	NR_CTRL_CLIENTS
};

enum data_xport_type {
	BAM_DMUX,
	BAM2BAM_IPA,
	NR_XPORT_TYPES
};

int gbam_connect(struct data_port *gr, enum bam_dmux_func_type func);
void gbam_disconnect(struct data_port *gr, enum bam_dmux_func_type func);
void gbam_cleanup(enum bam_dmux_func_type func);
int gbam_setup(enum bam_dmux_func_type func);
int gbam_mbim_connect(struct usb_gadget *g, struct usb_ep *in,
			struct usb_ep *out);
void gbam_mbim_disconnect(void);
int gbam_mbim_setup(void);

int gqti_ctrl_connect(void *gr, enum qti_port_type qport, unsigned int intf,
						enum data_xport_type dxport);
void gqti_ctrl_disconnect(void *gr, enum qti_port_type qport);
int gqti_ctrl_init(void);
void gqti_ctrl_cleanup(void);
#endif /* __U_RMNET_H*/
