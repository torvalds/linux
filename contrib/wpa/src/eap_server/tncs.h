/*
 * EAP-TNC - TNCS (IF-IMV, IF-TNCCS, and IF-TNCCS-SOH)
 * Copyright (c) 2007-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TNCS_H
#define TNCS_H

struct tncs_data;

struct tncs_data * tncs_init(void);
void tncs_deinit(struct tncs_data *tncs);
void tncs_init_connection(struct tncs_data *tncs);
size_t tncs_total_send_len(struct tncs_data *tncs);
u8 * tncs_copy_send_buf(struct tncs_data *tncs, u8 *pos);
char * tncs_if_tnccs_start(struct tncs_data *tncs);
char * tncs_if_tnccs_end(void);

enum tncs_process_res {
	TNCCS_PROCESS_ERROR = -1,
	TNCCS_PROCESS_OK_NO_RECOMMENDATION = 0,
	TNCCS_RECOMMENDATION_ERROR,
	TNCCS_RECOMMENDATION_ALLOW,
	TNCCS_RECOMMENDATION_NONE,
	TNCCS_RECOMMENDATION_ISOLATE,
	TNCCS_RECOMMENDATION_NO_ACCESS,
	TNCCS_RECOMMENDATION_NO_RECOMMENDATION
};

enum tncs_process_res tncs_process_if_tnccs(struct tncs_data *tncs,
					    const u8 *msg, size_t len);

int tncs_global_init(void);
void tncs_global_deinit(void);

struct wpabuf * tncs_build_soh_request(void);
struct wpabuf * tncs_process_soh(const u8 *soh_tlv, size_t soh_tlv_len,
				 int *failure);

#endif /* TNCS_H */
