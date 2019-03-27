/*
 * EAP common peer/server definitions
 * Copyright (c) 2004-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_COMMON_H
#define EAP_COMMON_H

#include "wpabuf.h"

struct erp_tlvs {
	const u8 *keyname;
	const u8 *domain;

	u8 keyname_len;
	u8 domain_len;
};

int eap_hdr_len_valid(const struct wpabuf *msg, size_t min_payload);
const u8 * eap_hdr_validate(int vendor, EapType eap_type,
			    const struct wpabuf *msg, size_t *plen);
struct wpabuf * eap_msg_alloc(int vendor, EapType type, size_t payload_len,
			      u8 code, u8 identifier);
void eap_update_len(struct wpabuf *msg);
u8 eap_get_id(const struct wpabuf *msg);
EapType eap_get_type(const struct wpabuf *msg);
int erp_parse_tlvs(const u8 *pos, const u8 *end, struct erp_tlvs *tlvs,
		   int stop_at_keyname);

#endif /* EAP_COMMON_H */
