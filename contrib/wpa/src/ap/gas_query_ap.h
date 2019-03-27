/*
 * Generic advertisement service (GAS) query
 * Copyright (c) 2009, Atheros Communications
 * Copyright (c) 2011-2017, Qualcomm Atheros
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef GAS_QUERY_AP_H
#define GAS_QUERY_AP_H

struct gas_query_ap;

struct gas_query_ap * gas_query_ap_init(struct hostapd_data *hapd,
					void *msg_ctx);
void gas_query_ap_deinit(struct gas_query_ap *gas);
int gas_query_ap_rx(struct gas_query_ap *gas, const u8 *sa, u8 categ,
		    const u8 *data, size_t len, int freq);

/**
 * enum gas_query_ap_result - GAS query result
 */
enum gas_query_ap_result {
	GAS_QUERY_AP_SUCCESS,
	GAS_QUERY_AP_FAILURE,
	GAS_QUERY_AP_TIMEOUT,
	GAS_QUERY_AP_PEER_ERROR,
	GAS_QUERY_AP_INTERNAL_ERROR,
	GAS_QUERY_AP_DELETED_AT_DEINIT
};

int gas_query_ap_req(struct gas_query_ap *gas, const u8 *dst, int freq,
		     struct wpabuf *req,
		     void (*cb)(void *ctx, const u8 *dst, u8 dialog_token,
				enum gas_query_ap_result result,
				const struct wpabuf *adv_proto,
				const struct wpabuf *resp, u16 status_code),
		     void *ctx);
void gas_query_ap_tx_status(struct gas_query_ap *gas, const u8 *dst,
			    const u8 *data, size_t data_len, int ok);

#endif /* GAS_QUERY_AP_H */
