/*
 * EAP server/peer: EAP-EKE shared routines
 * Copyright (c) 2011-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_EKE_COMMON_H
#define EAP_EKE_COMMON_H

/* EKE Exchange */
#define EAP_EKE_ID 1
#define EAP_EKE_COMMIT 2
#define EAP_EKE_CONFIRM 3
#define EAP_EKE_FAILURE 4

/* Diffie-Hellman Group Registry */
#define EAP_EKE_DHGROUP_EKE_2 1
#define EAP_EKE_DHGROUP_EKE_5 2
#define EAP_EKE_DHGROUP_EKE_14 3 /* mandatory to implement */
#define EAP_EKE_DHGROUP_EKE_15 4
#define EAP_EKE_DHGROUP_EKE_16 5

/* Encryption Algorithm Registry */
#define EAP_EKE_ENCR_AES128_CBC 1 /* mandatory to implement */

/* Pseudo Random Function Registry */
#define EAP_EKE_PRF_HMAC_SHA1 1 /* mandatory to implement */
#define EAP_EKE_PRF_HMAC_SHA2_256 2

/* Keyed Message Digest (MAC) Registry */
#define EAP_EKE_MAC_HMAC_SHA1 1 /* mandatory to implement */
#define EAP_EKE_MAC_HMAC_SHA2_256 2

/* Identity Type Registry */
#define EAP_EKE_ID_OPAQUE 1
#define EAP_EKE_ID_NAI 2
#define EAP_EKE_ID_IPv4 3
#define EAP_EKE_ID_IPv6 4
#define EAP_EKE_ID_FQDN 5
#define EAP_EKE_ID_DN 6

/* Failure-Code */
#define EAP_EKE_FAIL_NO_ERROR 1
#define EAP_EKE_FAIL_PROTO_ERROR 2
#define EAP_EKE_FAIL_PASSWD_NOT_FOUND 3
#define EAP_EKE_FAIL_AUTHENTICATION_FAIL 4
#define EAP_EKE_FAIL_AUTHORIZATION_FAIL 5
#define EAP_EKE_FAIL_NO_PROPOSAL_CHOSEN 6
#define EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR 0xffffffff

#define EAP_EKE_MAX_DH_LEN 512
#define EAP_EKE_MAX_HASH_LEN 32
#define EAP_EKE_MAX_KEY_LEN 16
#define EAP_EKE_MAX_KE_LEN 16
#define EAP_EKE_MAX_KI_LEN 32
#define EAP_EKE_MAX_KA_LEN 32
#define EAP_EKE_MAX_NONCE_LEN 16

struct eap_eke_session {
	/* Selected proposal */
	u8 dhgroup;
	u8 encr;
	u8 prf;
	u8 mac;

	u8 shared_secret[EAP_EKE_MAX_HASH_LEN];
	u8 ke[EAP_EKE_MAX_KE_LEN];
	u8 ki[EAP_EKE_MAX_KI_LEN];
	u8 ka[EAP_EKE_MAX_KA_LEN];

	int prf_len;
	int nonce_len;
	int auth_len;
	int dhcomp_len;
	int pnonce_len;
	int pnonce_ps_len;
};

int eap_eke_session_init(struct eap_eke_session *sess, u8 dhgroup, u8 encr,
			 u8 prf, u8 mac);
void eap_eke_session_clean(struct eap_eke_session *sess);
int eap_eke_dh_init(u8 group, u8 *ret_priv, u8 *ret_pub);
int eap_eke_derive_key(struct eap_eke_session *sess,
		       const u8 *password, size_t password_len,
		       const u8 *id_s, size_t id_s_len, const u8 *id_p,
		       size_t id_p_len, u8 *key);
int eap_eke_dhcomp(struct eap_eke_session *sess, const u8 *key, const u8 *dhpub,
		   u8 *ret_dhcomp);
int eap_eke_shared_secret(struct eap_eke_session *sess, const u8 *key,
			  const u8 *dhpriv, const u8 *peer_dhcomp);
int eap_eke_derive_ke_ki(struct eap_eke_session *sess,
			 const u8 *id_s, size_t id_s_len,
			 const u8 *id_p, size_t id_p_len);
int eap_eke_derive_ka(struct eap_eke_session *sess,
		      const u8 *id_s, size_t id_s_len,
		      const u8 *id_p, size_t id_p_len,
		      const u8 *nonce_p, const u8 *nonce_s);
int eap_eke_derive_msk(struct eap_eke_session *sess,
		       const u8 *id_s, size_t id_s_len,
		       const u8 *id_p, size_t id_p_len,
		       const u8 *nonce_p, const u8 *nonce_s,
		       u8 *msk, u8 *emsk);
int eap_eke_prot(struct eap_eke_session *sess,
		 const u8 *data, size_t data_len,
		 u8 *prot, size_t *prot_len);
int eap_eke_decrypt_prot(struct eap_eke_session *sess,
			 const u8 *prot, size_t prot_len,
			 u8 *data, size_t *data_len);
int eap_eke_auth(struct eap_eke_session *sess, const char *label,
		 const struct wpabuf *msgs, u8 *auth);

#endif /* EAP_EKE_COMMON_H */
