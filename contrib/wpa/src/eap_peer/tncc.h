/*
 * EAP-TNC - TNCC (IF-IMC and IF-TNCCS)
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TNCC_H
#define TNCC_H

struct tncc_data;

struct tncc_data * tncc_init(void);
void tncc_deinit(struct tncc_data *tncc);
void tncc_init_connection(struct tncc_data *tncc);
size_t tncc_total_send_len(struct tncc_data *tncc);
u8 * tncc_copy_send_buf(struct tncc_data *tncc, u8 *pos);
char * tncc_if_tnccs_start(struct tncc_data *tncc);
char * tncc_if_tnccs_end(void);

enum tncc_process_res {
	TNCCS_PROCESS_ERROR = -1,
	TNCCS_PROCESS_OK_NO_RECOMMENDATION = 0,
	TNCCS_RECOMMENDATION_ERROR,
	TNCCS_RECOMMENDATION_ALLOW,
	TNCCS_RECOMMENDATION_NONE,
	TNCCS_RECOMMENDATION_ISOLATE
};

enum tncc_process_res tncc_process_if_tnccs(struct tncc_data *tncc,
					    const u8 *msg, size_t len);

struct wpabuf * tncc_process_soh_request(int ver, const u8 *data, size_t len);

#endif /* TNCC_H */
