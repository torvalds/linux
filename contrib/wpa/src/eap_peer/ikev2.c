/*
 * IKEv2 responder (RFC 4306) for EAP-IKEV2
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/dh_groups.h"
#include "crypto/random.h"
#include "ikev2.h"


void ikev2_responder_deinit(struct ikev2_responder_data *data)
{
	ikev2_free_keys(&data->keys);
	wpabuf_free(data->i_dh_public);
	wpabuf_free(data->r_dh_private);
	os_free(data->IDi);
	os_free(data->IDr);
	os_free(data->shared_secret);
	wpabuf_free(data->i_sign_msg);
	wpabuf_free(data->r_sign_msg);
	os_free(data->key_pad);
}


static int ikev2_derive_keys(struct ikev2_responder_data *data)
{
	u8 *buf, *pos, *pad, skeyseed[IKEV2_MAX_HASH_LEN];
	size_t buf_len, pad_len;
	struct wpabuf *shared;
	const struct ikev2_integ_alg *integ;
	const struct ikev2_prf_alg *prf;
	const struct ikev2_encr_alg *encr;
	int ret;
	const u8 *addr[2];
	size_t len[2];

	/* RFC 4306, Sect. 2.14 */

	integ = ikev2_get_integ(data->proposal.integ);
	prf = ikev2_get_prf(data->proposal.prf);
	encr = ikev2_get_encr(data->proposal.encr);
	if (integ == NULL || prf == NULL || encr == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported proposal");
		return -1;
	}

	shared = dh_derive_shared(data->i_dh_public, data->r_dh_private,
				  data->dh);
	if (shared == NULL)
		return -1;

	/* Construct Ni | Nr | SPIi | SPIr */

	buf_len = data->i_nonce_len + data->r_nonce_len + 2 * IKEV2_SPI_LEN;
	buf = os_malloc(buf_len);
	if (buf == NULL) {
		wpabuf_free(shared);
		return -1;
	}

	pos = buf;
	os_memcpy(pos, data->i_nonce, data->i_nonce_len);
	pos += data->i_nonce_len;
	os_memcpy(pos, data->r_nonce, data->r_nonce_len);
	pos += data->r_nonce_len;
	os_memcpy(pos, data->i_spi, IKEV2_SPI_LEN);
	pos += IKEV2_SPI_LEN;
	os_memcpy(pos, data->r_spi, IKEV2_SPI_LEN);

	/* SKEYSEED = prf(Ni | Nr, g^ir) */
	/* Use zero-padding per RFC 4306, Sect. 2.14 */
	pad_len = data->dh->prime_len - wpabuf_len(shared);
	pad = os_zalloc(pad_len ? pad_len : 1);
	if (pad == NULL) {
		wpabuf_free(shared);
		os_free(buf);
		return -1;
	}

	addr[0] = pad;
	len[0] = pad_len;
	addr[1] = wpabuf_head(shared);
	len[1] = wpabuf_len(shared);
	if (ikev2_prf_hash(prf->id, buf, data->i_nonce_len + data->r_nonce_len,
			   2, addr, len, skeyseed) < 0) {
		wpabuf_free(shared);
		os_free(buf);
		os_free(pad);
		return -1;
	}
	os_free(pad);
	wpabuf_free(shared);

	/* DH parameters are not needed anymore, so free them */
	wpabuf_free(data->i_dh_public);
	data->i_dh_public = NULL;
	wpabuf_free(data->r_dh_private);
	data->r_dh_private = NULL;

	wpa_hexdump_key(MSG_DEBUG, "IKEV2: SKEYSEED",
			skeyseed, prf->hash_len);

	ret = ikev2_derive_sk_keys(prf, integ, encr, skeyseed, buf, buf_len,
				   &data->keys);
	os_free(buf);
	return ret;
}


static int ikev2_parse_transform(struct ikev2_proposal_data *prop,
				 const u8 *pos, const u8 *end)
{
	int transform_len;
	const struct ikev2_transform *t;
	u16 transform_id;
	const u8 *tend;

	if (end - pos < (int) sizeof(*t)) {
		wpa_printf(MSG_INFO, "IKEV2: Too short transform");
		return -1;
	}

	t = (const struct ikev2_transform *) pos;
	transform_len = WPA_GET_BE16(t->transform_length);
	if (transform_len < (int) sizeof(*t) || transform_len > end - pos) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid transform length %d",
			   transform_len);
		return -1;
	}
	tend = pos + transform_len;

	transform_id = WPA_GET_BE16(t->transform_id);

	wpa_printf(MSG_DEBUG, "IKEV2:   Transform:");
	wpa_printf(MSG_DEBUG, "IKEV2:     Type: %d  Transform Length: %d  "
		   "Transform Type: %d  Transform ID: %d",
		   t->type, transform_len, t->transform_type, transform_id);

	if (t->type != 0 && t->type != 3) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected Transform type");
		return -1;
	}

	pos = (const u8 *) (t + 1);
	if (pos < tend) {
		wpa_hexdump(MSG_DEBUG, "IKEV2:     Transform Attributes",
			    pos, tend - pos);
	}

	switch (t->transform_type) {
	case IKEV2_TRANSFORM_ENCR:
		if (ikev2_get_encr(transform_id)) {
			if (transform_id == ENCR_AES_CBC) {
				if (tend - pos != 4) {
					wpa_printf(MSG_DEBUG, "IKEV2: No "
						   "Transform Attr for AES");
					break;
				}
				if (WPA_GET_BE16(pos) != 0x800e) {
					wpa_printf(MSG_DEBUG, "IKEV2: Not a "
						   "Key Size attribute for "
						   "AES");
					break;
				}
				if (WPA_GET_BE16(pos + 2) != 128) {
					wpa_printf(MSG_DEBUG, "IKEV2: "
						   "Unsupported AES key size "
						   "%d bits",
						   WPA_GET_BE16(pos + 2));
					break;
				}
			}
			prop->encr = transform_id;
		}
		break;
	case IKEV2_TRANSFORM_PRF:
		if (ikev2_get_prf(transform_id))
			prop->prf = transform_id;
		break;
	case IKEV2_TRANSFORM_INTEG:
		if (ikev2_get_integ(transform_id))
			prop->integ = transform_id;
		break;
	case IKEV2_TRANSFORM_DH:
		if (dh_groups_get(transform_id))
			prop->dh = transform_id;
		break;
	}

	return transform_len;
}


static int ikev2_parse_proposal(struct ikev2_proposal_data *prop,
				const u8 *pos, const u8 *end)
{
	const u8 *pend, *ppos;
	int proposal_len, i;
	const struct ikev2_proposal *p;

	if (end - pos < (int) sizeof(*p)) {
		wpa_printf(MSG_INFO, "IKEV2: Too short proposal");
		return -1;
	}

	/* FIX: AND processing if multiple proposals use the same # */

	p = (const struct ikev2_proposal *) pos;
	proposal_len = WPA_GET_BE16(p->proposal_length);
	if (proposal_len < (int) sizeof(*p) || proposal_len > end - pos) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid proposal length %d",
			   proposal_len);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "IKEV2: SAi1 Proposal # %d",
		   p->proposal_num);
	wpa_printf(MSG_DEBUG, "IKEV2:   Type: %d  Proposal Length: %d "
		   " Protocol ID: %d",
		   p->type, proposal_len, p->protocol_id);
	wpa_printf(MSG_DEBUG, "IKEV2:   SPI Size: %d  Transforms: %d",
		   p->spi_size, p->num_transforms);

	if (p->type != 0 && p->type != 2) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected Proposal type");
		return -1;
	}

	if (p->protocol_id != IKEV2_PROTOCOL_IKE) {
		wpa_printf(MSG_DEBUG, "IKEV2: Unexpected Protocol ID "
			   "(only IKE allowed for EAP-IKEv2)");
		return -1;
	}

	if (p->proposal_num != prop->proposal_num) {
		if (p->proposal_num == prop->proposal_num + 1)
			prop->proposal_num = p->proposal_num;
		else {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Proposal #");
			return -1;
		}
	}

	ppos = (const u8 *) (p + 1);
	pend = pos + proposal_len;
	if (p->spi_size > pend - ppos) {
		wpa_printf(MSG_INFO, "IKEV2: Not enough room for SPI "
			   "in proposal");
		return -1;
	}
	if (p->spi_size) {
		wpa_hexdump(MSG_DEBUG, "IKEV2:    SPI",
			    ppos, p->spi_size);
		ppos += p->spi_size;
	}

	/*
	 * For initial IKE_SA negotiation, SPI Size MUST be zero; for
	 * subsequent negotiations, it must be 8 for IKE. We only support
	 * initial case for now.
	 */
	if (p->spi_size != 0) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected SPI Size");
		return -1;
	}

	if (p->num_transforms == 0) {
		wpa_printf(MSG_INFO, "IKEV2: At least one transform required");
		return -1;
	}

	for (i = 0; i < (int) p->num_transforms; i++) {
		int tlen = ikev2_parse_transform(prop, ppos, pend);
		if (tlen < 0)
			return -1;
		ppos += tlen;
	}

	if (ppos != pend) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected data after "
			   "transforms");
		return -1;
	}

	return proposal_len;
}


static int ikev2_process_sai1(struct ikev2_responder_data *data,
			      const u8 *sai1, size_t sai1_len)
{
	struct ikev2_proposal_data prop;
	const u8 *pos, *end;
	int found = 0;

	/* Security Association Payloads: <Proposals> */

	if (sai1 == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: SAi1 not received");
		return -1;
	}

	os_memset(&prop, 0, sizeof(prop));
	prop.proposal_num = 1;

	pos = sai1;
	end = sai1 + sai1_len;

	while (pos < end) {
		int plen;

		prop.integ = -1;
		prop.prf = -1;
		prop.encr = -1;
		prop.dh = -1;
		plen = ikev2_parse_proposal(&prop, pos, end);
		if (plen < 0)
			return -1;

		if (!found && prop.integ != -1 && prop.prf != -1 &&
		    prop.encr != -1 && prop.dh != -1) {
			os_memcpy(&data->proposal, &prop, sizeof(prop));
			data->dh = dh_groups_get(prop.dh);
			found = 1;
		}

		pos += plen;
	}

	if (pos != end) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected data after proposals");
		return -1;
	}

	if (!found) {
		wpa_printf(MSG_INFO, "IKEV2: No acceptable proposal found");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "IKEV2: Accepted proposal #%d: ENCR:%d PRF:%d "
		   "INTEG:%d D-H:%d", data->proposal.proposal_num,
		   data->proposal.encr, data->proposal.prf,
		   data->proposal.integ, data->proposal.dh);

	return 0;
}


static int ikev2_process_kei(struct ikev2_responder_data *data,
			     const u8 *kei, size_t kei_len)
{
	u16 group;

	/*
	 * Key Exchange Payload:
	 * DH Group # (16 bits)
	 * RESERVED (16 bits)
	 * Key Exchange Data (Diffie-Hellman public value)
	 */

	if (kei == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: KEi not received");
		return -1;
	}

	if (kei_len < 4 + 96) {
		wpa_printf(MSG_INFO, "IKEV2: Too short Key Exchange Payload");
		return -1;
	}

	group = WPA_GET_BE16(kei);
	wpa_printf(MSG_DEBUG, "IKEV2: KEi DH Group #%u", group);

	if (group != data->proposal.dh) {
		wpa_printf(MSG_DEBUG, "IKEV2: KEi DH Group #%u does not match "
			   "with the selected proposal (%u)",
			   group, data->proposal.dh);
		/* Reject message with Notify payload of type
		 * INVALID_KE_PAYLOAD (RFC 4306, Sect. 3.4) */
		data->error_type = INVALID_KE_PAYLOAD;
		data->state = NOTIFY;
		return -1;
	}

	if (data->dh == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported DH group");
		return -1;
	}

	/* RFC 4306, Section 3.4:
	 * The length of DH public value MUST be equal to the length of the
	 * prime modulus.
	 */
	if (kei_len - 4 != data->dh->prime_len) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid DH public value length "
			   "%ld (expected %ld)",
			   (long) (kei_len - 4), (long) data->dh->prime_len);
		return -1;
	}

	wpabuf_free(data->i_dh_public);
	data->i_dh_public = wpabuf_alloc(kei_len - 4);
	if (data->i_dh_public == NULL)
		return -1;
	wpabuf_put_data(data->i_dh_public, kei + 4, kei_len - 4);

	wpa_hexdump_buf(MSG_DEBUG, "IKEV2: KEi Diffie-Hellman Public Value",
			data->i_dh_public);
	
	return 0;
}


static int ikev2_process_ni(struct ikev2_responder_data *data,
			    const u8 *ni, size_t ni_len)
{
	if (ni == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Ni not received");
		return -1;
	}

	if (ni_len < IKEV2_NONCE_MIN_LEN || ni_len > IKEV2_NONCE_MAX_LEN) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid Ni length %ld",
		           (long) ni_len);
		return -1;
	}

	data->i_nonce_len = ni_len;
	os_memcpy(data->i_nonce, ni, ni_len);
	wpa_hexdump(MSG_MSGDUMP, "IKEV2: Ni",
		    data->i_nonce, data->i_nonce_len);

	return 0;
}


static int ikev2_process_sa_init(struct ikev2_responder_data *data,
				 const struct ikev2_hdr *hdr,
				 struct ikev2_payloads *pl)
{
	if (ikev2_process_sai1(data, pl->sa, pl->sa_len) < 0 ||
	    ikev2_process_kei(data, pl->ke, pl->ke_len) < 0 ||
	    ikev2_process_ni(data, pl->nonce, pl->nonce_len) < 0)
		return -1;

	os_memcpy(data->i_spi, hdr->i_spi, IKEV2_SPI_LEN);

	return 0;
}


static int ikev2_process_idi(struct ikev2_responder_data *data,
			     const u8 *idi, size_t idi_len)
{
	u8 id_type;

	if (idi == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No IDi received");
		return -1;
	}

	if (idi_len < 4) {
		wpa_printf(MSG_INFO, "IKEV2: Too short IDi payload");
		return -1;
	}

	id_type = idi[0];
	idi += 4;
	idi_len -= 4;

	wpa_printf(MSG_DEBUG, "IKEV2: IDi ID Type %d", id_type);
	wpa_hexdump_ascii(MSG_DEBUG, "IKEV2: IDi", idi, idi_len);
	os_free(data->IDi);
	data->IDi = os_memdup(idi, idi_len);
	if (data->IDi == NULL)
		return -1;
	data->IDi_len = idi_len;
	data->IDi_type = id_type;

	return 0;
}


static int ikev2_process_cert(struct ikev2_responder_data *data,
			      const u8 *cert, size_t cert_len)
{
	u8 cert_encoding;

	if (cert == NULL) {
		if (data->peer_auth == PEER_AUTH_CERT) {
			wpa_printf(MSG_INFO, "IKEV2: No Certificate received");
			return -1;
		}
		return 0;
	}

	if (cert_len < 1) {
		wpa_printf(MSG_INFO, "IKEV2: No Cert Encoding field");
		return -1;
	}

	cert_encoding = cert[0];
	cert++;
	cert_len--;

	wpa_printf(MSG_DEBUG, "IKEV2: Cert Encoding %d", cert_encoding);
	wpa_hexdump(MSG_MSGDUMP, "IKEV2: Certificate Data", cert, cert_len);

	/* TODO: validate certificate */

	return 0;
}


static int ikev2_process_auth_cert(struct ikev2_responder_data *data,
				   u8 method, const u8 *auth, size_t auth_len)
{
	if (method != AUTH_RSA_SIGN) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported authentication "
			   "method %d", method);
		return -1;
	}

	/* TODO: validate AUTH */
	return 0;
}


static int ikev2_process_auth_secret(struct ikev2_responder_data *data,
				     u8 method, const u8 *auth,
				     size_t auth_len)
{
	u8 auth_data[IKEV2_MAX_HASH_LEN];
	const struct ikev2_prf_alg *prf;

	if (method != AUTH_SHARED_KEY_MIC) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported authentication "
			   "method %d", method);
		return -1;
	}

	/* msg | Nr | prf(SK_pi,IDi') */
	if (ikev2_derive_auth_data(data->proposal.prf, data->i_sign_msg,
				   data->IDi, data->IDi_len, data->IDi_type,
				   &data->keys, 1, data->shared_secret,
				   data->shared_secret_len,
				   data->r_nonce, data->r_nonce_len,
				   data->key_pad, data->key_pad_len,
				   auth_data) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Could not derive AUTH data");
		return -1;
	}

	wpabuf_free(data->i_sign_msg);
	data->i_sign_msg = NULL;

	prf = ikev2_get_prf(data->proposal.prf);
	if (prf == NULL)
		return -1;

	if (auth_len != prf->hash_len ||
	    os_memcmp_const(auth, auth_data, auth_len) != 0) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid Authentication Data");
		wpa_hexdump(MSG_DEBUG, "IKEV2: Received Authentication Data",
			    auth, auth_len);
		wpa_hexdump(MSG_DEBUG, "IKEV2: Expected Authentication Data",
			    auth_data, prf->hash_len);
		data->error_type = AUTHENTICATION_FAILED;
		data->state = NOTIFY;
		return -1;
	}

	wpa_printf(MSG_DEBUG, "IKEV2: Server authenticated successfully "
		   "using shared keys");

	return 0;
}


static int ikev2_process_auth(struct ikev2_responder_data *data,
			      const u8 *auth, size_t auth_len)
{
	u8 auth_method;

	if (auth == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No Authentication Payload");
		return -1;
	}

	if (auth_len < 4) {
		wpa_printf(MSG_INFO, "IKEV2: Too short Authentication "
			   "Payload");
		return -1;
	}

	auth_method = auth[0];
	auth += 4;
	auth_len -= 4;

	wpa_printf(MSG_DEBUG, "IKEV2: Auth Method %d", auth_method);
	wpa_hexdump(MSG_MSGDUMP, "IKEV2: Authentication Data", auth, auth_len);

	switch (data->peer_auth) {
	case PEER_AUTH_CERT:
		return ikev2_process_auth_cert(data, auth_method, auth,
					       auth_len);
	case PEER_AUTH_SECRET:
		return ikev2_process_auth_secret(data, auth_method, auth,
						 auth_len);
	}

	return -1;
}


static int ikev2_process_sa_auth_decrypted(struct ikev2_responder_data *data,
					   u8 next_payload,
					   u8 *payload, size_t payload_len)
{
	struct ikev2_payloads pl;

	wpa_printf(MSG_DEBUG, "IKEV2: Processing decrypted payloads");

	if (ikev2_parse_payloads(&pl, next_payload, payload, payload +
				 payload_len) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Failed to parse decrypted "
			   "payloads");
		return -1;
	}

	if (ikev2_process_idi(data, pl.idi, pl.idi_len) < 0 ||
	    ikev2_process_cert(data, pl.cert, pl.cert_len) < 0 ||
	    ikev2_process_auth(data, pl.auth, pl.auth_len) < 0)
		return -1;

	return 0;
}


static int ikev2_process_sa_auth(struct ikev2_responder_data *data,
				 const struct ikev2_hdr *hdr,
				 struct ikev2_payloads *pl)
{
	u8 *decrypted;
	size_t decrypted_len;
	int ret;

	decrypted = ikev2_decrypt_payload(data->proposal.encr,
					  data->proposal.integ,
					  &data->keys, 1, hdr, pl->encrypted,
					  pl->encrypted_len, &decrypted_len);
	if (decrypted == NULL)
		return -1;

	ret = ikev2_process_sa_auth_decrypted(data, pl->encr_next_payload,
					      decrypted, decrypted_len);
	os_free(decrypted);

	return ret;
}


static int ikev2_validate_rx_state(struct ikev2_responder_data *data,
				   u8 exchange_type, u32 message_id)
{
	switch (data->state) {
	case SA_INIT:
		/* Expect to receive IKE_SA_INIT: HDR, SAi1, KEi, Ni */
		if (exchange_type != IKE_SA_INIT) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Exchange Type "
				   "%u in SA_INIT state", exchange_type);
			return -1;
		}
		if (message_id != 0) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Message ID %u "
				   "in SA_INIT state", message_id);
			return -1;
		}
		break;
	case SA_AUTH:
		/* Expect to receive IKE_SA_AUTH:
		 * HDR, SK {IDi, [CERT,] [CERTREQ,] [IDr,]
		 *	AUTH, SAi2, TSi, TSr}
		 */
		if (exchange_type != IKE_SA_AUTH) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Exchange Type "
				   "%u in SA_AUTH state", exchange_type);
			return -1;
		}
		if (message_id != 1) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Message ID %u "
				   "in SA_AUTH state", message_id);
			return -1;
		}
		break;
	case CHILD_SA:
		if (exchange_type != CREATE_CHILD_SA) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Exchange Type "
				   "%u in CHILD_SA state", exchange_type);
			return -1;
		}
		if (message_id != 2) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected Message ID %u "
				   "in CHILD_SA state", message_id);
			return -1;
		}
		break;
	case NOTIFY:
	case IKEV2_DONE:
	case IKEV2_FAILED:
		return -1;
	}

	return 0;
}


int ikev2_responder_process(struct ikev2_responder_data *data,
			    const struct wpabuf *buf)
{
	const struct ikev2_hdr *hdr;
	u32 length, message_id;
	const u8 *pos, *end;
	struct ikev2_payloads pl;

	wpa_printf(MSG_MSGDUMP, "IKEV2: Received message (len %lu)",
		   (unsigned long) wpabuf_len(buf));

	if (wpabuf_len(buf) < sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "IKEV2: Too short frame to include HDR");
		return -1;
	}

	data->error_type = 0;
	hdr = (const struct ikev2_hdr *) wpabuf_head(buf);
	end = wpabuf_head_u8(buf) + wpabuf_len(buf);
	message_id = WPA_GET_BE32(hdr->message_id);
	length = WPA_GET_BE32(hdr->length);

	wpa_hexdump(MSG_DEBUG, "IKEV2:   IKE_SA Initiator's SPI",
		    hdr->i_spi, IKEV2_SPI_LEN);
	wpa_hexdump(MSG_DEBUG, "IKEV2:   IKE_SA Responder's SPI",
		    hdr->r_spi, IKEV2_SPI_LEN);
	wpa_printf(MSG_DEBUG, "IKEV2:   Next Payload: %u  Version: 0x%x  "
		   "Exchange Type: %u",
		   hdr->next_payload, hdr->version, hdr->exchange_type);
	wpa_printf(MSG_DEBUG, "IKEV2:   Message ID: %u  Length: %u",
		   message_id, length);

	if (hdr->version != IKEV2_VERSION) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported HDR version 0x%x "
			   "(expected 0x%x)", hdr->version, IKEV2_VERSION);
		return -1;
	}

	if (length != wpabuf_len(buf)) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid length (HDR: %lu != "
			   "RX: %lu)", (unsigned long) length,
			   (unsigned long) wpabuf_len(buf));
		return -1;
	}

	if (ikev2_validate_rx_state(data, hdr->exchange_type, message_id) < 0)
		return -1;

	if ((hdr->flags & (IKEV2_HDR_INITIATOR | IKEV2_HDR_RESPONSE)) !=
	    IKEV2_HDR_INITIATOR) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected Flags value 0x%x",
			   hdr->flags);
		return -1;
	}

	if (data->state != SA_INIT) {
		if (os_memcmp(data->i_spi, hdr->i_spi, IKEV2_SPI_LEN) != 0) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected IKE_SA "
				   "Initiator's SPI");
			return -1;
		}
		if (os_memcmp(data->r_spi, hdr->r_spi, IKEV2_SPI_LEN) != 0) {
			wpa_printf(MSG_INFO, "IKEV2: Unexpected IKE_SA "
				   "Responder's SPI");
			return -1;
		}
	}

	pos = (const u8 *) (hdr + 1);
	if (ikev2_parse_payloads(&pl, hdr->next_payload, pos, end) < 0)
		return -1;

	if (data->state == SA_INIT) {
		data->last_msg = LAST_MSG_SA_INIT;
		if (ikev2_process_sa_init(data, hdr, &pl) < 0) {
			if (data->state == NOTIFY)
				return 0;
			return -1;
		}
		wpabuf_free(data->i_sign_msg);
		data->i_sign_msg = wpabuf_dup(buf);
	}

	if (data->state == SA_AUTH) {
		data->last_msg = LAST_MSG_SA_AUTH;
		if (ikev2_process_sa_auth(data, hdr, &pl) < 0) {
			if (data->state == NOTIFY)
				return 0;
			return -1;
		}
	}

	return 0;
}


static void ikev2_build_hdr(struct ikev2_responder_data *data,
			    struct wpabuf *msg, u8 exchange_type,
			    u8 next_payload, u32 message_id)
{
	struct ikev2_hdr *hdr;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding HDR");

	/* HDR - RFC 4306, Sect. 3.1 */
	hdr = wpabuf_put(msg, sizeof(*hdr));
	os_memcpy(hdr->i_spi, data->i_spi, IKEV2_SPI_LEN);
	os_memcpy(hdr->r_spi, data->r_spi, IKEV2_SPI_LEN);
	hdr->next_payload = next_payload;
	hdr->version = IKEV2_VERSION;
	hdr->exchange_type = exchange_type;
	hdr->flags = IKEV2_HDR_RESPONSE;
	WPA_PUT_BE32(hdr->message_id, message_id);
}


static int ikev2_build_sar1(struct ikev2_responder_data *data,
			    struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;
	struct ikev2_proposal *p;
	struct ikev2_transform *t;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding SAr1 payload");

	/* SAr1 - RFC 4306, Sect. 2.7 and 3.3 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;

	p = wpabuf_put(msg, sizeof(*p));
	p->proposal_num = data->proposal.proposal_num;
	p->protocol_id = IKEV2_PROTOCOL_IKE;
	p->num_transforms = 4;

	t = wpabuf_put(msg, sizeof(*t));
	t->type = 3;
	t->transform_type = IKEV2_TRANSFORM_ENCR;
	WPA_PUT_BE16(t->transform_id, data->proposal.encr);
	if (data->proposal.encr == ENCR_AES_CBC) {
		/* Transform Attribute: Key Len = 128 bits */
		wpabuf_put_be16(msg, 0x800e); /* AF=1, AttrType=14 */
		wpabuf_put_be16(msg, 128); /* 128-bit key */
	}
	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) t;
	WPA_PUT_BE16(t->transform_length, plen);

	t = wpabuf_put(msg, sizeof(*t));
	t->type = 3;
	WPA_PUT_BE16(t->transform_length, sizeof(*t));
	t->transform_type = IKEV2_TRANSFORM_PRF;
	WPA_PUT_BE16(t->transform_id, data->proposal.prf);

	t = wpabuf_put(msg, sizeof(*t));
	t->type = 3;
	WPA_PUT_BE16(t->transform_length, sizeof(*t));
	t->transform_type = IKEV2_TRANSFORM_INTEG;
	WPA_PUT_BE16(t->transform_id, data->proposal.integ);

	t = wpabuf_put(msg, sizeof(*t));
	WPA_PUT_BE16(t->transform_length, sizeof(*t));
	t->transform_type = IKEV2_TRANSFORM_DH;
	WPA_PUT_BE16(t->transform_id, data->proposal.dh);

	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) p;
	WPA_PUT_BE16(p->proposal_length, plen);

	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);

	return 0;
}


static int ikev2_build_ker(struct ikev2_responder_data *data,
			   struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;
	struct wpabuf *pv;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding KEr payload");

	pv = dh_init(data->dh, &data->r_dh_private);
	if (pv == NULL) {
		wpa_printf(MSG_DEBUG, "IKEV2: Failed to initialize DH");
		return -1;
	}

	/* KEr - RFC 4306, Sect. 3.4 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;

	wpabuf_put_be16(msg, data->proposal.dh); /* DH Group # */
	wpabuf_put(msg, 2); /* RESERVED */
	/*
	 * RFC 4306, Sect. 3.4: possible zero padding for public value to
	 * match the length of the prime.
	 */
	wpabuf_put(msg, data->dh->prime_len - wpabuf_len(pv));
	wpabuf_put_buf(msg, pv);
	wpabuf_free(pv);

	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);
	return 0;
}


static int ikev2_build_nr(struct ikev2_responder_data *data,
			  struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding Nr payload");

	/* Nr - RFC 4306, Sect. 3.9 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;
	wpabuf_put_data(msg, data->r_nonce, data->r_nonce_len);
	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);
	return 0;
}


static int ikev2_build_idr(struct ikev2_responder_data *data,
			   struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding IDr payload");

	if (data->IDr == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No IDr available");
		return -1;
	}

	/* IDr - RFC 4306, Sect. 3.5 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;
	wpabuf_put_u8(msg, ID_KEY_ID);
	wpabuf_put(msg, 3); /* RESERVED */
	wpabuf_put_data(msg, data->IDr, data->IDr_len);
	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);
	return 0;
}


static int ikev2_build_auth(struct ikev2_responder_data *data,
			    struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;
	const struct ikev2_prf_alg *prf;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding AUTH payload");

	prf = ikev2_get_prf(data->proposal.prf);
	if (prf == NULL)
		return -1;

	/* Authentication - RFC 4306, Sect. 3.8 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;
	wpabuf_put_u8(msg, AUTH_SHARED_KEY_MIC);
	wpabuf_put(msg, 3); /* RESERVED */

	/* msg | Ni | prf(SK_pr,IDr') */
	if (ikev2_derive_auth_data(data->proposal.prf, data->r_sign_msg,
				   data->IDr, data->IDr_len, ID_KEY_ID,
				   &data->keys, 0, data->shared_secret,
				   data->shared_secret_len,
				   data->i_nonce, data->i_nonce_len,
				   data->key_pad, data->key_pad_len,
				   wpabuf_put(msg, prf->hash_len)) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Could not derive AUTH data");
		return -1;
	}
	wpabuf_free(data->r_sign_msg);
	data->r_sign_msg = NULL;

	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);
	return 0;
}


static int ikev2_build_notification(struct ikev2_responder_data *data,
				    struct wpabuf *msg, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding Notification payload");

	if (data->error_type == 0) {
		wpa_printf(MSG_INFO, "IKEV2: No Notify Message Type "
			   "available");
		return -1;
	}

	/* Notify - RFC 4306, Sect. 3.10 */
	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;
	wpabuf_put_u8(msg, 0); /* Protocol ID: no existing SA */
	wpabuf_put_u8(msg, 0); /* SPI Size */
	wpabuf_put_be16(msg, data->error_type);

	switch (data->error_type) {
	case INVALID_KE_PAYLOAD:
		if (data->proposal.dh == -1) {
			wpa_printf(MSG_INFO, "IKEV2: No DH Group selected for "
				   "INVALID_KE_PAYLOAD notifications");
			return -1;
		}
		wpabuf_put_be16(msg, data->proposal.dh);
		wpa_printf(MSG_DEBUG, "IKEV2: INVALID_KE_PAYLOAD - request "
			   "DH Group #%d", data->proposal.dh);
		break;
	case AUTHENTICATION_FAILED:
		/* no associated data */
		break;
	default:
		wpa_printf(MSG_INFO, "IKEV2: Unsupported Notify Message Type "
			   "%d", data->error_type);
		return -1;
	}

	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);
	return 0;
}


static struct wpabuf * ikev2_build_sa_init(struct ikev2_responder_data *data)
{
	struct wpabuf *msg;

	/* build IKE_SA_INIT: HDR, SAr1, KEr, Nr, [CERTREQ], [SK{IDr}] */

	if (os_get_random(data->r_spi, IKEV2_SPI_LEN))
		return NULL;
	wpa_hexdump(MSG_DEBUG, "IKEV2: IKE_SA Responder's SPI",
		    data->r_spi, IKEV2_SPI_LEN);

	data->r_nonce_len = IKEV2_NONCE_MIN_LEN;
	if (random_get_bytes(data->r_nonce, data->r_nonce_len))
		return NULL;
	wpa_hexdump(MSG_DEBUG, "IKEV2: Nr", data->r_nonce, data->r_nonce_len);

	msg = wpabuf_alloc(sizeof(struct ikev2_hdr) + data->IDr_len + 1500);
	if (msg == NULL)
		return NULL;

	ikev2_build_hdr(data, msg, IKE_SA_INIT, IKEV2_PAYLOAD_SA, 0);
	if (ikev2_build_sar1(data, msg, IKEV2_PAYLOAD_KEY_EXCHANGE) ||
	    ikev2_build_ker(data, msg, IKEV2_PAYLOAD_NONCE) ||
	    ikev2_build_nr(data, msg, data->peer_auth == PEER_AUTH_SECRET ?
			   IKEV2_PAYLOAD_ENCRYPTED :
			   IKEV2_PAYLOAD_NO_NEXT_PAYLOAD)) {
		wpabuf_free(msg);
		return NULL;
	}

	if (ikev2_derive_keys(data)) {
		wpabuf_free(msg);
		return NULL;
	}

	if (data->peer_auth == PEER_AUTH_CERT) {
		/* TODO: CERTREQ with SHA-1 hashes of Subject Public Key Info
		 * for trust agents */
	}

	if (data->peer_auth == PEER_AUTH_SECRET) {
		struct wpabuf *plain = wpabuf_alloc(data->IDr_len + 1000);
		if (plain == NULL) {
			wpabuf_free(msg);
			return NULL;
		}
		if (ikev2_build_idr(data, plain,
				    IKEV2_PAYLOAD_NO_NEXT_PAYLOAD) ||
		    ikev2_build_encrypted(data->proposal.encr,
					  data->proposal.integ,
					  &data->keys, 0, msg, plain,
					  IKEV2_PAYLOAD_IDr)) {
			wpabuf_free(plain);
			wpabuf_free(msg);
			return NULL;
		}
		wpabuf_free(plain);
	}

	ikev2_update_hdr(msg);

	wpa_hexdump_buf(MSG_MSGDUMP, "IKEV2: Sending message (SA_INIT)", msg);

	data->state = SA_AUTH;

	wpabuf_free(data->r_sign_msg);
	data->r_sign_msg = wpabuf_dup(msg);

	return msg;
}


static struct wpabuf * ikev2_build_sa_auth(struct ikev2_responder_data *data)
{
	struct wpabuf *msg, *plain;

	/* build IKE_SA_AUTH: HDR, SK {IDr, [CERT,] AUTH} */

	msg = wpabuf_alloc(sizeof(struct ikev2_hdr) + data->IDr_len + 1000);
	if (msg == NULL)
		return NULL;
	ikev2_build_hdr(data, msg, IKE_SA_AUTH, IKEV2_PAYLOAD_ENCRYPTED, 1);

	plain = wpabuf_alloc(data->IDr_len + 1000);
	if (plain == NULL) {
		wpabuf_free(msg);
		return NULL;
	}

	if (ikev2_build_idr(data, plain, IKEV2_PAYLOAD_AUTHENTICATION) ||
	    ikev2_build_auth(data, plain, IKEV2_PAYLOAD_NO_NEXT_PAYLOAD) ||
	    ikev2_build_encrypted(data->proposal.encr, data->proposal.integ,
				  &data->keys, 0, msg, plain,
				  IKEV2_PAYLOAD_IDr)) {
		wpabuf_free(plain);
		wpabuf_free(msg);
		return NULL;
	}
	wpabuf_free(plain);

	wpa_hexdump_buf(MSG_MSGDUMP, "IKEV2: Sending message (SA_AUTH)", msg);

	data->state = IKEV2_DONE;

	return msg;
}


static struct wpabuf * ikev2_build_notify(struct ikev2_responder_data *data)
{
	struct wpabuf *msg;

	msg = wpabuf_alloc(sizeof(struct ikev2_hdr) + 1000);
	if (msg == NULL)
		return NULL;
	if (data->last_msg == LAST_MSG_SA_AUTH) {
		/* HDR, SK{N} */
		struct wpabuf *plain = wpabuf_alloc(100);
		if (plain == NULL) {
			wpabuf_free(msg);
			return NULL;
		}
		ikev2_build_hdr(data, msg, IKE_SA_AUTH,
				IKEV2_PAYLOAD_ENCRYPTED, 1);
		if (ikev2_build_notification(data, plain,
					     IKEV2_PAYLOAD_NO_NEXT_PAYLOAD) ||
		    ikev2_build_encrypted(data->proposal.encr,
					  data->proposal.integ,
					  &data->keys, 0, msg, plain,
					  IKEV2_PAYLOAD_NOTIFICATION)) {
			wpabuf_free(plain);
			wpabuf_free(msg);
			return NULL;
		}
		wpabuf_free(plain);
		data->state = IKEV2_FAILED;
	} else {
		/* HDR, N */
		ikev2_build_hdr(data, msg, IKE_SA_INIT,
				IKEV2_PAYLOAD_NOTIFICATION, 0);
		if (ikev2_build_notification(data, msg,
					     IKEV2_PAYLOAD_NO_NEXT_PAYLOAD)) {
			wpabuf_free(msg);
			return NULL;
		}
		data->state = SA_INIT;
	}

	ikev2_update_hdr(msg);

	wpa_hexdump_buf(MSG_MSGDUMP, "IKEV2: Sending message (Notification)",
			msg);

	return msg;
}


struct wpabuf * ikev2_responder_build(struct ikev2_responder_data *data)
{
	switch (data->state) {
	case SA_INIT:
		return ikev2_build_sa_init(data);
	case SA_AUTH:
		return ikev2_build_sa_auth(data);
	case CHILD_SA:
		return NULL;
	case NOTIFY:
		return ikev2_build_notify(data);
	case IKEV2_DONE:
	case IKEV2_FAILED:
		return NULL;
	}
	return NULL;
}
