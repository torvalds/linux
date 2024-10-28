/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2014,2016,2020, The Linux Foundation. All rights reserved.
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

#ifndef __U_DATA_IPA_H
#define __U_DATA_IPA_H

#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/cdev.h>
#include <linux/ipa_usb.h>
#include <linux/usb_bam.h>

#include "u_rmnet.h"

enum ipa_func_type {
	USB_IPA_FUNC_ECM,
	USB_IPA_FUNC_MBIM,
	USB_IPA_FUNC_RMNET,
	USB_IPA_FUNC_RNDIS,
	USB_IPA_FUNC_DPL,
	USB_IPA_NUM_FUNCS,
};

/* Max Number of IPA data ports supported */
#define IPA_N_PORTS USB_IPA_NUM_FUNCS

struct ipa_function_bind_info {
	struct usb_string *string_defs;
	int data_str_idx;
	struct usb_interface_descriptor *data_desc;
	struct usb_endpoint_descriptor *fs_in_desc;
	struct usb_endpoint_descriptor *fs_out_desc;
	struct usb_endpoint_descriptor *fs_notify_desc;
	struct usb_endpoint_descriptor *hs_in_desc;
	struct usb_endpoint_descriptor *hs_out_desc;
	struct usb_endpoint_descriptor *hs_notify_desc;
	struct usb_endpoint_descriptor *ss_in_desc;
	struct usb_endpoint_descriptor *ss_out_desc;
	struct usb_endpoint_descriptor *ss_notify_desc;

	struct usb_descriptor_header **fs_desc_hdr;
	struct usb_descriptor_header **hs_desc_hdr;
	struct usb_descriptor_header **ss_desc_hdr;
};

/* for configfs support */
#define MAX_INST_NAME_LEN      40

struct f_rndis_qc_opts {
	struct usb_function_instance	func_inst;
	struct f_rndis_qc		*rndis;
	u32				vendor_id;
	const char			*manufacturer;
	struct net_device		*net;
	int				refcnt;
};

struct f_rmnet_opts {
	struct usb_function_instance func_inst;
	struct f_rmnet *dev;
	int refcnt;
};

#ifdef CONFIG_USB_F_QCRNDIS
void ipa_data_port_select(enum ipa_func_type func);
void ipa_data_disconnect(struct data_port *gp, enum ipa_func_type func);
int ipa_data_connect(struct data_port *gp, enum ipa_func_type func,
			u8 src_connection_idx, u8 dst_connection_idx);
int ipa_data_setup(enum ipa_func_type func);
void ipa_data_free(enum ipa_func_type func);

void ipa_data_flush_workqueue(void);
void ipa_data_resume(struct data_port *gp, enum ipa_func_type func,
		bool remote_wakeup_enabled);
void ipa_data_suspend(struct data_port *gp, enum ipa_func_type func,
		bool remote_wakeup_enabled);

void ipa_data_set_ul_max_xfer_size(u32 ul_max_xfer_size);

void ipa_data_set_dl_max_xfer_size(u32 dl_max_transfer_size);

void ipa_data_set_ul_max_pkt_num(u8 ul_max_packets_number);

void ipa_data_start_rx_tx(enum ipa_func_type func);

void ipa_data_start_rndis_ipa(enum ipa_func_type func);

void ipa_data_stop_rndis_ipa(enum ipa_func_type func);
#else
static inline void ipa_data_port_select(enum ipa_func_type func)
{
}
static inline void ipa_data_disconnect(struct data_port *gp,
				enum ipa_func_type func)
{
}
static inline int ipa_data_connect(struct data_port *gp,
			enum ipa_func_type func, u8 src_connection_idx,
			u8 dst_connection_idx)
{
	return 0;
}
static inline int ipa_data_setup(enum ipa_func_type func)
{
	return 0;
}
static inline void ipa_data_free(enum ipa_func_type func)
{
}
void ipa_data_flush_workqueue(void)
{
}
static inline void ipa_data_resume(struct data_port *gp,
		enum ipa_func_type func, bool remote_wakeup_enabled)
{
}
static inline void ipa_data_suspend(struct data_port *gp,
		enum ipa_func_type func, bool remote_wakeup_enabled)
{
}
#endif /* CONFIG_USB_F_QCRNDIS */

#ifdef CONFIG_USB_F_QCRNDIS
void *rndis_qc_get_ipa_priv(void);
void *rndis_qc_get_ipa_rx_cb(void);
bool rndis_qc_get_skip_ep_config(void);
void *rndis_qc_get_ipa_tx_cb(void);
void rndis_ipa_reset_trigger(void);
#else
static inline void *rndis_qc_get_ipa_priv(void)
{
	return NULL;
}
static inline void *rndis_qc_get_ipa_rx_cb(void)
{
	return NULL;
}
static inline bool rndis_qc_get_skip_ep_config(void)
{
	return true;
}
static inline void *rndis_qc_get_ipa_tx_cb(void)
{
	return NULL;
}
static inline void rndis_ipa_reset_trigger(void)
{
}
#endif /* CONFIG_USB_F_QCRNDIS */

#if IS_ENABLED(CONFIG_USB_CONFIGFS_RMNET_BAM)
void gqti_ctrl_update_ipa_pipes(void *gr, enum qti_port_type qport,
				u32 ipa_prod, u32 ipa_cons);
#else
static inline void gqti_ctrl_update_ipa_pipes(void *gr,
				enum qti_port_type qport,
				u32 ipa_prod, u32 ipa_cons)
{
}
#endif /* CONFIG_USB_CONFIGFS_RMNET_BAM */
#endif
