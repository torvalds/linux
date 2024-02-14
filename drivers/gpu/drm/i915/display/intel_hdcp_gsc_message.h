/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_HDCP_GSC_MESSAGE_H__
#define __INTEL_HDCP_GSC_MESSAGE_H__

#include <linux/types.h>

struct device;
struct drm_i915_private;
struct hdcp_port_data;
struct hdcp2_ake_init;
struct hdcp2_ake_send_cert;
struct hdcp2_ake_no_stored_km;
struct hdcp2_ake_send_hprime;
struct hdcp2_ake_send_pairing_info;
struct hdcp2_lc_init;
struct hdcp2_lc_send_lprime;
struct hdcp2_ske_send_eks;
struct hdcp2_rep_send_receiverid_list;
struct hdcp2_rep_send_ack;
struct hdcp2_rep_stream_ready;

ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len);
bool intel_hdcp_gsc_check_status(struct drm_i915_private *i915);
int
intel_hdcp_gsc_initiate_session(struct device *dev, struct hdcp_port_data *data,
				struct hdcp2_ake_init *ake_data);
int
intel_hdcp_gsc_verify_receiver_cert_prepare_km(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_ake_send_cert *rx_cert,
					       bool *km_stored,
					       struct hdcp2_ake_no_stored_km
					       *ek_pub_km,
					       size_t *msg_sz);
int
intel_hdcp_gsc_verify_hprime(struct device *dev, struct hdcp_port_data *data,
			     struct hdcp2_ake_send_hprime *rx_hprime);
int
intel_hdcp_gsc_store_pairing_info(struct device *dev, struct hdcp_port_data *data,
				  struct hdcp2_ake_send_pairing_info *pairing_info);
int
intel_hdcp_gsc_initiate_locality_check(struct device *dev,
				       struct hdcp_port_data *data,
				       struct hdcp2_lc_init *lc_init_data);
int
intel_hdcp_gsc_verify_lprime(struct device *dev, struct hdcp_port_data *data,
			     struct hdcp2_lc_send_lprime *rx_lprime);
int intel_hdcp_gsc_get_session_key(struct device *dev,
				   struct hdcp_port_data *data,
				   struct hdcp2_ske_send_eks *ske_data);
int
intel_hdcp_gsc_repeater_check_flow_prepare_ack(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_rep_send_receiverid_list
					       *rep_topology,
					       struct hdcp2_rep_send_ack
					       *rep_send_ack);
int intel_hdcp_gsc_verify_mprime(struct device *dev,
				 struct hdcp_port_data *data,
				 struct hdcp2_rep_stream_ready *stream_ready);
int intel_hdcp_gsc_enable_authentication(struct device *dev,
					 struct hdcp_port_data *data);
int
intel_hdcp_gsc_close_session(struct device *dev, struct hdcp_port_data *data);

#endif /* __INTEL_HDCP_GSC_MESSAGE_H__ */
