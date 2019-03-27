/*
 * EAP peer method: EAP-pwd (RFC 5931)
 * Copyright (c) 2010, Dan Harkins <dharkins@lounge.org>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"
#include "crypto/ms_funcs.h"
#include "crypto/crypto.h"
#include "eap_peer/eap_i.h"
#include "eap_common/eap_pwd_common.h"


struct eap_pwd_data {
	enum {
		PWD_ID_Req, PWD_Commit_Req, PWD_Confirm_Req,
		SUCCESS_ON_FRAG_COMPLETION, SUCCESS, FAILURE
	} state;
	u8 *id_peer;
	size_t id_peer_len;
	u8 *id_server;
	size_t id_server_len;
	u8 *password;
	size_t password_len;
	int password_hash;
	u16 group_num;
	u8 prep;
	u8 token[4];
	EAP_PWD_group *grp;

	struct wpabuf *inbuf;
	size_t in_frag_pos;
	struct wpabuf *outbuf;
	size_t out_frag_pos;
	size_t mtu;

	struct crypto_bignum *k;
	struct crypto_bignum *private_value;
	struct crypto_bignum *server_scalar;
	struct crypto_bignum *my_scalar;
	struct crypto_ec_point *my_element;
	struct crypto_ec_point *server_element;

	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 session_id[1 + SHA256_MAC_LEN];
};


#ifndef CONFIG_NO_STDOUT_DEBUG
static const char * eap_pwd_state_txt(int state)
{
	switch (state) {
        case PWD_ID_Req:
		return "PWD-ID-Req";
        case PWD_Commit_Req:
		return "PWD-Commit-Req";
        case PWD_Confirm_Req:
		return "PWD-Confirm-Req";
	case SUCCESS_ON_FRAG_COMPLETION:
		return "SUCCESS_ON_FRAG_COMPLETION";
        case SUCCESS:
		return "SUCCESS";
        case FAILURE:
		return "FAILURE";
        default:
		return "PWD-UNK";
	}
}
#endif  /* CONFIG_NO_STDOUT_DEBUG */


static void eap_pwd_state(struct eap_pwd_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-PWD: %s -> %s",
		   eap_pwd_state_txt(data->state), eap_pwd_state_txt(state));
	data->state = state;
}


static void * eap_pwd_init(struct eap_sm *sm)
{
	struct eap_pwd_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;
	int fragment_size;
	int pwhash;

	password = eap_get_config_password2(sm, &password_len, &pwhash);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: No password configured!");
		return NULL;
	}

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: No identity configured!");
		return NULL;
	}

	if ((data = os_zalloc(sizeof(*data))) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation data fail");
		return NULL;
	}

	if ((data->id_peer = os_malloc(identity_len)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation id fail");
		os_free(data);
		return NULL;
	}

	os_memcpy(data->id_peer, identity, identity_len);
	data->id_peer_len = identity_len;

	if ((data->password = os_malloc(password_len)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation psk fail");
		bin_clear_free(data->id_peer, data->id_peer_len);
		os_free(data);
		return NULL;
	}
	os_memcpy(data->password, password, password_len);
	data->password_len = password_len;
	data->password_hash = pwhash;

	data->out_frag_pos = data->in_frag_pos = 0;
	data->inbuf = data->outbuf = NULL;
	fragment_size = eap_get_config_fragment_size(sm);
	if (fragment_size <= 0)
		data->mtu = 1020; /* default from RFC 5931 */
	else
		data->mtu = fragment_size;

	data->state = PWD_ID_Req;

	return data;
}


static void eap_pwd_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;

	crypto_bignum_deinit(data->private_value, 1);
	crypto_bignum_deinit(data->server_scalar, 1);
	crypto_bignum_deinit(data->my_scalar, 1);
	crypto_bignum_deinit(data->k, 1);
	crypto_ec_point_deinit(data->my_element, 1);
	crypto_ec_point_deinit(data->server_element, 1);
	bin_clear_free(data->id_peer, data->id_peer_len);
	bin_clear_free(data->id_server, data->id_server_len);
	bin_clear_free(data->password, data->password_len);
	if (data->grp) {
		crypto_ec_deinit(data->grp->group);
		crypto_ec_point_deinit(data->grp->pwe, 1);
		os_free(data->grp);
	}
	wpabuf_free(data->inbuf);
	wpabuf_free(data->outbuf);
	bin_clear_free(data, sizeof(*data));
}


static u8 * eap_pwd_getkey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_MSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_pwd_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *id;

	if (data->state != SUCCESS)
		return NULL;

	id = os_memdup(data->session_id, 1 + SHA256_MAC_LEN);
	if (id == NULL)
		return NULL;

	*len = 1 + SHA256_MAC_LEN;

	return id;
}


static void
eap_pwd_perform_id_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
			    struct eap_method_ret *ret,
			    const struct wpabuf *reqData,
			    const u8 *payload, size_t payload_len)
{
	struct eap_pwd_id *id;

	if (data->state != PWD_ID_Req) {
		ret->ignore = TRUE;
		eap_pwd_state(data, FAILURE);
		return;
	}

	if (payload_len < sizeof(struct eap_pwd_id)) {
		ret->ignore = TRUE;
		eap_pwd_state(data, FAILURE);
		return;
	}

	id = (struct eap_pwd_id *) payload;
	data->group_num = be_to_host16(id->group_num);
	wpa_printf(MSG_DEBUG,
		   "EAP-PWD: Server EAP-pwd-ID proposal: group=%u random=%u prf=%u prep=%u",
		   data->group_num, id->random_function, id->prf, id->prep);
	if ((id->random_function != EAP_PWD_DEFAULT_RAND_FUNC) ||
	    (id->prf != EAP_PWD_DEFAULT_PRF)) {
		ret->ignore = TRUE;
		eap_pwd_state(data, FAILURE);
		return;
	}

	if (id->prep != EAP_PWD_PREP_NONE &&
	    id->prep != EAP_PWD_PREP_MS &&
	    id->prep != EAP_PWD_PREP_SSHA1 &&
	    id->prep != EAP_PWD_PREP_SSHA256 &&
	    id->prep != EAP_PWD_PREP_SSHA512) {
		wpa_printf(MSG_DEBUG,
			   "EAP-PWD: Unsupported password pre-processing technique (Prep=%u)",
			   id->prep);
		eap_pwd_state(data, FAILURE);
		return;
	}

	if (id->prep == EAP_PWD_PREP_NONE && data->password_hash) {
		wpa_printf(MSG_DEBUG,
			   "EAP-PWD: Unhashed password not available");
		eap_pwd_state(data, FAILURE);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-PWD (peer): using group %d",
		   data->group_num);

	data->prep = id->prep;
	os_memcpy(data->token, id->token, sizeof(id->token));

	if (data->id_server || data->grp) {
		wpa_printf(MSG_INFO, "EAP-pwd: data was already allocated");
		eap_pwd_state(data, FAILURE);
		return;
	}

	data->id_server = os_malloc(payload_len - sizeof(struct eap_pwd_id));
	if (data->id_server == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation id fail");
		eap_pwd_state(data, FAILURE);
		return;
	}
	data->id_server_len = payload_len - sizeof(struct eap_pwd_id);
	os_memcpy(data->id_server, id->identity, data->id_server_len);
	wpa_hexdump_ascii(MSG_INFO, "EAP-PWD (peer): server sent id of",
			  data->id_server, data->id_server_len);

	data->grp = get_eap_pwd_group(data->group_num);
	if (data->grp == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: failed to allocate memory for "
			   "group");
		eap_pwd_state(data, FAILURE);
		return;
	}

	data->outbuf = wpabuf_alloc(sizeof(struct eap_pwd_id) +
				    data->id_peer_len);
	if (data->outbuf == NULL) {
		eap_pwd_state(data, FAILURE);
		return;
	}
	wpabuf_put_be16(data->outbuf, data->group_num);
	wpabuf_put_u8(data->outbuf, EAP_PWD_DEFAULT_RAND_FUNC);
	wpabuf_put_u8(data->outbuf, EAP_PWD_DEFAULT_PRF);
	wpabuf_put_data(data->outbuf, id->token, sizeof(id->token));
	wpabuf_put_u8(data->outbuf, id->prep);
	wpabuf_put_data(data->outbuf, data->id_peer, data->id_peer_len);

	eap_pwd_state(data, PWD_Commit_Req);
}


static void
eap_pwd_perform_commit_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
				struct eap_method_ret *ret,
				const struct wpabuf *reqData,
				const u8 *payload, size_t payload_len)
{
	struct crypto_ec_point *K = NULL, *point = NULL;
	struct crypto_bignum *mask = NULL, *cofactor = NULL;
	const u8 *ptr = payload;
	u8 *scalar = NULL, *element = NULL;
	size_t prime_len, order_len;
	const u8 *password;
	size_t password_len;
	u8 pwhashhash[16];
	const u8 *salt_pwd[2];
	size_t salt_pwd_len[2], exp_len;
	u8 salt_len, salthashpwd[64]; /* 64 = SHA512_DIGEST_LENGTH */
	int res;

	if (data->state != PWD_Commit_Req) {
		ret->ignore = TRUE;
		goto fin;
	}

	if (!data->grp) {
		wpa_printf(MSG_DEBUG,
			   "EAP-PWD (client): uninitialized EAP-pwd group");
		ret->ignore = TRUE;
		goto fin;
	}

	prime_len = crypto_ec_prime_len(data->grp->group);
	order_len = crypto_ec_order_len(data->grp->group);

	switch (data->prep) {
	case EAP_PWD_PREP_MS:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd commit request, password prep is MS");
#ifdef CONFIG_FIPS
		wpa_printf(MSG_ERROR,
			   "EAP-PWD (peer): MS password hash not supported in FIPS mode");
		eap_pwd_state(data, FAILURE);
		return;
#else /* CONFIG_FIPS */
		if (payload_len != 2 * prime_len + order_len) {
			wpa_printf(MSG_INFO,
				   "EAP-pwd: Unexpected Commit payload length %u (expected %u)",
				   (unsigned int) payload_len,
				   (unsigned int) (2 * prime_len + order_len));
			goto fin;
		}
		if (data->password_hash) {
			res = hash_nt_password_hash(data->password, pwhashhash);
		} else {
			u8 pwhash[16];

			res = nt_password_hash(data->password,
					       data->password_len, pwhash);
			if (res == 0)
				res = hash_nt_password_hash(pwhash, pwhashhash);
			os_memset(pwhash, 0, sizeof(pwhash));
		}

		if (res) {
			eap_pwd_state(data, FAILURE);
			return;
		}

		password = pwhashhash;
		password_len = sizeof(pwhashhash);
#endif /* CONFIG_FIPS */
		break;
	case EAP_PWD_PREP_SSHA1:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd commit request, password prep is salted sha1");
		if (payload_len < 1 || *ptr == 0) {
			wpa_printf(MSG_DEBUG, "EAP-pwd: Invalid Salt-len");
			goto fin;
		}
		salt_len = *ptr++;
		exp_len = 1 + salt_len + 2 * prime_len + order_len;
		if (payload_len != exp_len) {
			wpa_printf(MSG_INFO,
				   "EAP-pwd: Unexpected Commit payload length %u (expected %u)",
				   (unsigned int) payload_len,
				   (unsigned int) exp_len);
			goto fin;
		}

		/* salted-password = Hash(password | salt) */
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Unsalted password",
				data->password, data->password_len);
		wpa_hexdump(MSG_DEBUG, "EAP-pwd: Salt", ptr, salt_len);
		salt_pwd[0] = data->password;
		salt_pwd[1] = ptr;
		salt_pwd_len[0] = data->password_len;
		salt_pwd_len[1] = salt_len;
		if (sha1_vector(2, salt_pwd, salt_pwd_len, salthashpwd) < 0)
			goto fin;

		wpa_printf(MSG_DEBUG,
			   "EAP-pwd: sha1 hashed %d byte salt with password",
			   (int) salt_len);
		ptr += salt_len;
		password = salthashpwd;
		password_len = SHA1_MAC_LEN;
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Salted password",
				password, password_len);
		break;
	case EAP_PWD_PREP_SSHA256:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd commit request, password prep is salted sha256");
		if (payload_len < 1 || *ptr == 0) {
			wpa_printf(MSG_DEBUG, "EAP-pwd: Invalid Salt-len");
			goto fin;
		}
		salt_len = *ptr++;
		exp_len = 1 + salt_len + 2 * prime_len + order_len;
		if (payload_len != exp_len) {
			wpa_printf(MSG_INFO,
				   "EAP-pwd: Unexpected Commit payload length %u (expected %u)",
				   (unsigned int) payload_len,
				   (unsigned int) exp_len);
			goto fin;
		}

		/* salted-password = Hash(password | salt) */
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Unsalted password",
				data->password, data->password_len);
		wpa_hexdump(MSG_DEBUG, "EAP-pwd: Salt", ptr, salt_len);
		salt_pwd[0] = data->password;
		salt_pwd[1] = ptr;
		salt_pwd_len[0] = data->password_len;
		salt_pwd_len[1] = salt_len;
		if (sha256_vector(2, salt_pwd, salt_pwd_len, salthashpwd) < 0)
			goto fin;

		ptr += salt_len;
		password = salthashpwd;
		password_len = SHA256_MAC_LEN;
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Salted password",
				password, password_len);
		break;
#ifdef CONFIG_SHA512
	case EAP_PWD_PREP_SSHA512:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd commit request, password prep is salted sha512");
		if (payload_len < 1 || *ptr == 0) {
			wpa_printf(MSG_DEBUG, "EAP-pwd: Invalid Salt-len");
			goto fin;
		}
		salt_len = *ptr++;
		exp_len = 1 + salt_len + 2 * prime_len + order_len;
		if (payload_len != exp_len) {
			wpa_printf(MSG_INFO,
				   "EAP-pwd: Unexpected Commit payload length %u (expected %u)",
				   (unsigned int) payload_len,
				   (unsigned int) exp_len);
			goto fin;
		}

		/* salted-password = Hash(password | salt) */
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Unsalted password",
				data->password, data->password_len);
		wpa_hexdump(MSG_DEBUG, "EAP-pwd: Salt", ptr, salt_len);
		salt_pwd[0] = data->password;
		salt_pwd[1] = ptr;
		salt_pwd_len[0] = data->password_len;
		salt_pwd_len[1] = salt_len;
		if (sha512_vector(2, salt_pwd, salt_pwd_len, salthashpwd) < 0)
			goto fin;

		ptr += salt_len;
		password = salthashpwd;
		password_len = SHA512_MAC_LEN;
		wpa_hexdump_key(MSG_DEBUG, "EAP-pwd: Salted password",
				password, password_len);
		break;
#endif /* CONFIG_SHA512 */
	case EAP_PWD_PREP_NONE:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd commit request, password prep is NONE");
		if (data->password_hash) {
			wpa_printf(MSG_DEBUG,
				   "EAP-PWD: Unhashed password not available");
			eap_pwd_state(data, FAILURE);
			return;
		}
		if (payload_len != 2 * prime_len + order_len) {
			wpa_printf(MSG_INFO,
				   "EAP-pwd: Unexpected Commit payload length %u (expected %u)",
				   (unsigned int) payload_len,
				   (unsigned int) (2 * prime_len + order_len));
			goto fin;
		}
		password = data->password;
		password_len = data->password_len;
		break;
	default:
		wpa_printf(MSG_DEBUG,
			   "EAP-pwd: Unsupported password pre-processing technique (Prep=%u)",
			   data->prep);
		eap_pwd_state(data, FAILURE);
		return;
	}

	/* compute PWE */
	res = compute_password_element(data->grp, data->group_num,
				       password, password_len,
				       data->id_server, data->id_server_len,
				       data->id_peer, data->id_peer_len,
				       data->token);
	os_memset(pwhashhash, 0, sizeof(pwhashhash));
	os_memset(salthashpwd, 0, sizeof(salthashpwd));
	if (res) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to compute PWE");
		eap_pwd_state(data, FAILURE);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-PWD (peer): computed %d bit PWE...",
		   (int) crypto_ec_prime_len_bits(data->grp->group));

	data->private_value = crypto_bignum_init();
	data->my_element = crypto_ec_point_init(data->grp->group);
	cofactor = crypto_bignum_init();
	data->my_scalar = crypto_bignum_init();
	mask = crypto_bignum_init();
	if (!data->private_value || !data->my_element || !cofactor ||
	    !data->my_scalar || !mask) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): scalar allocation fail");
		goto fin;
	}

	if (crypto_ec_cofactor(data->grp->group, cofactor) < 0) {
		wpa_printf(MSG_INFO, "EAP-pwd (peer): unable to get cofactor "
			   "for curve");
		goto fin;
	}

	if (crypto_bignum_rand(data->private_value,
			       crypto_ec_get_order(data->grp->group)) < 0 ||
	    crypto_bignum_rand(mask,
			       crypto_ec_get_order(data->grp->group)) < 0 ||
	    crypto_bignum_add(data->private_value, mask,
			      data->my_scalar) < 0 ||
	    crypto_bignum_mod(data->my_scalar,
			      crypto_ec_get_order(data->grp->group),
			      data->my_scalar) < 0) {
		wpa_printf(MSG_INFO,
			   "EAP-pwd (peer): unable to get randomness");
		goto fin;
	}

	if (crypto_ec_point_mul(data->grp->group, data->grp->pwe, mask,
				data->my_element) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): element allocation "
			   "fail");
		eap_pwd_state(data, FAILURE);
		goto fin;
	}

	if (crypto_ec_point_invert(data->grp->group, data->my_element) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): element inversion fail");
		goto fin;
	}

	/* process the request */
	data->k = crypto_bignum_init();
	K = crypto_ec_point_init(data->grp->group);
	point = crypto_ec_point_init(data->grp->group);
	if (!data->k || !K || !point) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): peer data allocation "
			   "fail");
		goto fin;
	}

	/* element, x then y, followed by scalar */
	data->server_element = crypto_ec_point_from_bin(data->grp->group, ptr);
	if (!data->server_element) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): setting peer element "
			   "fail");
		goto fin;
	}
	ptr += prime_len * 2;
	data->server_scalar = crypto_bignum_init_set(ptr, order_len);
	if (!data->server_scalar) {
		wpa_printf(MSG_INFO,
			   "EAP-PWD (peer): setting peer scalar fail");
		goto fin;
	}

	/* check to ensure server's element is not in a small sub-group */
	if (!crypto_bignum_is_one(cofactor)) {
		if (crypto_ec_point_mul(data->grp->group, data->server_element,
					cofactor, point) < 0) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): cannot multiply "
				   "server element by order!\n");
			goto fin;
		}
		if (crypto_ec_point_is_at_infinity(data->grp->group, point)) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): server element "
				   "is at infinity!\n");
			goto fin;
		}
	}

	/* compute the shared key, k */
	if (crypto_ec_point_mul(data->grp->group, data->grp->pwe,
				data->server_scalar, K) < 0 ||
	    crypto_ec_point_add(data->grp->group, K, data->server_element,
				K) < 0 ||
	    crypto_ec_point_mul(data->grp->group, K, data->private_value,
				K) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): computing shared key "
			   "fail");
		goto fin;
	}

	/* ensure that the shared key isn't in a small sub-group */
	if (!crypto_bignum_is_one(cofactor)) {
		if (crypto_ec_point_mul(data->grp->group, K, cofactor, K) < 0) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): cannot multiply "
				   "shared key point by order");
			goto fin;
		}
	}

	/*
	 * This check is strictly speaking just for the case above where
	 * co-factor > 1 but it was suggested that even though this is probably
	 * never going to happen it is a simple and safe check "just to be
	 * sure" so let's be safe.
	 */
	if (crypto_ec_point_is_at_infinity(data->grp->group, K)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): shared key point is at "
			   "infinity!\n");
		goto fin;
	}

	if (crypto_ec_point_x(data->grp->group, K, data->k) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to extract "
			   "shared secret from point");
		goto fin;
	}

	/* now do the response */
	scalar = os_zalloc(order_len);
	element = os_zalloc(prime_len * 2);
	if (!scalar || !element) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): data allocation fail");
		goto fin;
	}

	/*
	 * bignums occupy as little memory as possible so one that is
	 * sufficiently smaller than the prime or order might need pre-pending
	 * with zeros.
	 */
	crypto_bignum_to_bin(data->my_scalar, scalar, order_len, order_len);
	if (crypto_ec_point_to_bin(data->grp->group, data->my_element, element,
				   element + prime_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): point assignment fail");
		goto fin;
	}

	data->outbuf = wpabuf_alloc(order_len + 2 * prime_len);
	if (data->outbuf == NULL)
		goto fin;

	/* we send the element as (x,y) follwed by the scalar */
	wpabuf_put_data(data->outbuf, element, 2 * prime_len);
	wpabuf_put_data(data->outbuf, scalar, order_len);

fin:
	os_free(scalar);
	os_free(element);
	crypto_bignum_deinit(mask, 1);
	crypto_bignum_deinit(cofactor, 1);
	crypto_ec_point_deinit(K, 1);
	crypto_ec_point_deinit(point, 1);
	if (data->outbuf == NULL)
		eap_pwd_state(data, FAILURE);
	else
		eap_pwd_state(data, PWD_Confirm_Req);
}


static void
eap_pwd_perform_confirm_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
				 struct eap_method_ret *ret,
				 const struct wpabuf *reqData,
				 const u8 *payload, size_t payload_len)
{
	struct crypto_hash *hash = NULL;
	u32 cs;
	u16 grp;
	u8 conf[SHA256_MAC_LEN], *cruft = NULL, *ptr;
	size_t prime_len = 0, order_len = 0;

	if (data->state != PWD_Confirm_Req) {
		ret->ignore = TRUE;
		goto fin;
	}

	if (payload_len != SHA256_MAC_LEN) {
		wpa_printf(MSG_INFO,
			   "EAP-pwd: Unexpected Confirm payload length %u (expected %u)",
			   (unsigned int) payload_len, SHA256_MAC_LEN);
		goto fin;
	}

	prime_len = crypto_ec_prime_len(data->grp->group);
	order_len = crypto_ec_order_len(data->grp->group);

	/*
	 * first build up the ciphersuite which is group | random_function |
	 *	prf
	 */
	grp = htons(data->group_num);
	ptr = (u8 *) &cs;
	os_memcpy(ptr, &grp, sizeof(u16));
	ptr += sizeof(u16);
	*ptr = EAP_PWD_DEFAULT_RAND_FUNC;
	ptr += sizeof(u8);
	*ptr = EAP_PWD_DEFAULT_PRF;

	/* each component of the point will be at most as big as the prime */
	cruft = os_malloc(prime_len * 2);
	if (!cruft) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm allocation "
			   "fail");
		goto fin;
	}

	/*
	 * server's commit is H(k | server_element | server_scalar |
	 *			peer_element | peer_scalar | ciphersuite)
	 */
	hash = eap_pwd_h_init();
	if (hash == NULL)
		goto fin;

	/*
	 * zero the memory each time because this is mod prime math and some
	 * value may start with a few zeros and the previous one did not.
	 */
	crypto_bignum_to_bin(data->k, cruft, prime_len, prime_len);
	eap_pwd_h_update(hash, cruft, prime_len);

	/* server element: x, y */
	if (crypto_ec_point_to_bin(data->grp->group, data->server_element,
				   cruft, cruft + prime_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}
	eap_pwd_h_update(hash, cruft, prime_len * 2);

	/* server scalar */
	crypto_bignum_to_bin(data->server_scalar, cruft, order_len, order_len);
	eap_pwd_h_update(hash, cruft, order_len);

	/* my element: x, y */
	if (crypto_ec_point_to_bin(data->grp->group, data->my_element, cruft,
				   cruft + prime_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}
	eap_pwd_h_update(hash, cruft, prime_len * 2);

	/* my scalar */
	crypto_bignum_to_bin(data->my_scalar, cruft, order_len, order_len);
	eap_pwd_h_update(hash, cruft, order_len);

	/* the ciphersuite */
	eap_pwd_h_update(hash, (u8 *) &cs, sizeof(u32));

	/* random function fin */
	eap_pwd_h_final(hash, conf);
	hash = NULL;

	ptr = (u8 *) payload;
	if (os_memcmp_const(conf, ptr, SHA256_MAC_LEN)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm did not verify");
		goto fin;
	}

	wpa_printf(MSG_DEBUG, "EAP-pwd (peer): confirm verified");

	/*
	 * compute confirm:
	 *  H(k | peer_element | peer_scalar | server_element | server_scalar |
	 *    ciphersuite)
	 */
	hash = eap_pwd_h_init();
	if (hash == NULL)
		goto fin;

	/* k */
	crypto_bignum_to_bin(data->k, cruft, prime_len, prime_len);
	eap_pwd_h_update(hash, cruft, prime_len);

	/* my element */
	if (crypto_ec_point_to_bin(data->grp->group, data->my_element, cruft,
				   cruft + prime_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm point "
			   "assignment fail");
		goto fin;
	}
	eap_pwd_h_update(hash, cruft, prime_len * 2);

	/* my scalar */
	crypto_bignum_to_bin(data->my_scalar, cruft, order_len, order_len);
	eap_pwd_h_update(hash, cruft, order_len);

	/* server element: x, y */
	if (crypto_ec_point_to_bin(data->grp->group, data->server_element,
				   cruft, cruft + prime_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm point "
			   "assignment fail");
		goto fin;
	}
	eap_pwd_h_update(hash, cruft, prime_len * 2);

	/* server scalar */
	crypto_bignum_to_bin(data->server_scalar, cruft, order_len, order_len);
	eap_pwd_h_update(hash, cruft, order_len);

	/* the ciphersuite */
	eap_pwd_h_update(hash, (u8 *) &cs, sizeof(u32));

	/* all done */
	eap_pwd_h_final(hash, conf);
	hash = NULL;

	if (compute_keys(data->grp, data->k,
			 data->my_scalar, data->server_scalar, conf, ptr,
			 &cs, data->msk, data->emsk, data->session_id) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to compute MSK | "
			   "EMSK");
		goto fin;
	}

	data->outbuf = wpabuf_alloc(SHA256_MAC_LEN);
	if (data->outbuf == NULL)
		goto fin;

	wpabuf_put_data(data->outbuf, conf, SHA256_MAC_LEN);

fin:
	bin_clear_free(cruft, prime_len * 2);
	if (data->outbuf == NULL) {
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		eap_pwd_state(data, FAILURE);
	} else {
		eap_pwd_state(data, SUCCESS_ON_FRAG_COMPLETION);
	}

	/* clean allocated memory */
	if (hash)
		eap_pwd_h_final(hash, conf);
}


static struct wpabuf *
eap_pwd_process(struct eap_sm *sm, void *priv, struct eap_method_ret *ret,
		const struct wpabuf *reqData)
{
	struct eap_pwd_data *data = priv;
	struct wpabuf *resp = NULL;
	const u8 *pos, *buf;
	size_t len;
	u16 tot_len = 0;
	u8 lm_exch;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PWD, reqData, &len);
	if ((pos == NULL) || (len < 1)) {
		wpa_printf(MSG_DEBUG, "EAP-pwd: Got a frame but pos is %s and "
			   "len is %d",
			   pos == NULL ? "NULL" : "not NULL", (int) len);
		ret->ignore = TRUE;
		return NULL;
	}

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = FALSE;

	lm_exch = *pos;
	pos++;                  /* skip over the bits and the exch */
	len--;

	/*
	 * we're fragmenting so send out the next fragment
	 */
	if (data->out_frag_pos) {
		/*
		 * this should be an ACK
		 */
		if (len)
			wpa_printf(MSG_INFO, "Bad Response! Fragmenting but "
				   "not an ACK");

		wpa_printf(MSG_DEBUG, "EAP-pwd: Got an ACK for a fragment");
		/*
		 * check if there are going to be more fragments
		 */
		len = wpabuf_len(data->outbuf) - data->out_frag_pos;
		if ((len + EAP_PWD_HDR_SIZE) > data->mtu) {
			len = data->mtu - EAP_PWD_HDR_SIZE;
			EAP_PWD_SET_MORE_BIT(lm_exch);
		}
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
				     EAP_PWD_HDR_SIZE + len,
				     EAP_CODE_RESPONSE, eap_get_id(reqData));
		if (resp == NULL) {
			wpa_printf(MSG_INFO, "Unable to allocate memory for "
				   "next fragment!");
			return NULL;
		}
		wpabuf_put_u8(resp, lm_exch);
		buf = wpabuf_head_u8(data->outbuf);
		wpabuf_put_data(resp, buf + data->out_frag_pos, len);
		data->out_frag_pos += len;
		/*
		 * this is the last fragment so get rid of the out buffer
		 */
		if (data->out_frag_pos >= wpabuf_len(data->outbuf)) {
			wpabuf_free(data->outbuf);
			data->outbuf = NULL;
			data->out_frag_pos = 0;
		}
		wpa_printf(MSG_DEBUG, "EAP-pwd: Send %s fragment of %d bytes",
			   data->out_frag_pos == 0 ? "last" : "next",
			   (int) len);
		if (data->state == SUCCESS_ON_FRAG_COMPLETION) {
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_UNCOND_SUCC;
			eap_pwd_state(data, SUCCESS);
		}
		return resp;
	}

	/*
	 * see if this is a fragment that needs buffering
	 *
	 * if it's the first fragment there'll be a length field
	 */
	if (EAP_PWD_GET_LENGTH_BIT(lm_exch)) {
		if (len < 2) {
			wpa_printf(MSG_DEBUG,
				   "EAP-pwd: Frame too short to contain Total-Length field");
			ret->ignore = TRUE;
			return NULL;
		}
		tot_len = WPA_GET_BE16(pos);
		wpa_printf(MSG_DEBUG, "EAP-pwd: Incoming fragments whose "
			   "total length = %d", tot_len);
		if (tot_len > 15000)
			return NULL;
		if (data->inbuf) {
			wpa_printf(MSG_DEBUG,
				   "EAP-pwd: Unexpected new fragment start when previous fragment is still in use");
			ret->ignore = TRUE;
			return NULL;
		}
		data->inbuf = wpabuf_alloc(tot_len);
		if (data->inbuf == NULL) {
			wpa_printf(MSG_INFO, "Out of memory to buffer "
				   "fragments!");
			return NULL;
		}
		data->in_frag_pos = 0;
		pos += sizeof(u16);
		len -= sizeof(u16);
	}
	/*
	 * buffer and ACK the fragment
	 */
	if (EAP_PWD_GET_MORE_BIT(lm_exch) || data->in_frag_pos) {
		data->in_frag_pos += len;
		if (data->in_frag_pos > wpabuf_size(data->inbuf)) {
			wpa_printf(MSG_INFO, "EAP-pwd: Buffer overflow attack "
				   "detected (%d vs. %d)!",
				   (int) data->in_frag_pos,
				   (int) wpabuf_len(data->inbuf));
			wpabuf_free(data->inbuf);
			data->inbuf = NULL;
			data->in_frag_pos = 0;
			return NULL;
		}
		wpabuf_put_data(data->inbuf, pos, len);
	}
	if (EAP_PWD_GET_MORE_BIT(lm_exch)) {
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
				     EAP_PWD_HDR_SIZE,
				     EAP_CODE_RESPONSE, eap_get_id(reqData));
		if (resp != NULL)
			wpabuf_put_u8(resp, (EAP_PWD_GET_EXCHANGE(lm_exch)));
		wpa_printf(MSG_DEBUG, "EAP-pwd: ACKing a %d byte fragment",
			   (int) len);
		return resp;
	}
	/*
	 * we're buffering and this is the last fragment
	 */
	if (data->in_frag_pos) {
		wpa_printf(MSG_DEBUG, "EAP-pwd: Last fragment, %d bytes",
			   (int) len);
		pos = wpabuf_head_u8(data->inbuf);
		len = data->in_frag_pos;
	}
	wpa_printf(MSG_DEBUG, "EAP-pwd: processing frame: exch %d, len %d",
		   EAP_PWD_GET_EXCHANGE(lm_exch), (int) len);

	switch (EAP_PWD_GET_EXCHANGE(lm_exch)) {
	case EAP_PWD_OPCODE_ID_EXCH:
		eap_pwd_perform_id_exchange(sm, data, ret, reqData,
					    pos, len);
		break;
	case EAP_PWD_OPCODE_COMMIT_EXCH:
		eap_pwd_perform_commit_exchange(sm, data, ret, reqData,
						pos, len);
		break;
	case EAP_PWD_OPCODE_CONFIRM_EXCH:
		eap_pwd_perform_confirm_exchange(sm, data, ret, reqData,
						 pos, len);
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-pwd: Ignoring message with unknown "
			   "opcode %d", lm_exch);
		break;
	}
	/*
	 * if we buffered the just processed input now's the time to free it
	 */
	if (data->in_frag_pos) {
		wpabuf_free(data->inbuf);
		data->inbuf = NULL;
		data->in_frag_pos = 0;
	}

	if (data->outbuf == NULL) {
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;        /* generic failure */
	}

	/*
	 * we have output! Do we need to fragment it?
	 */
	lm_exch = EAP_PWD_GET_EXCHANGE(lm_exch);
	len = wpabuf_len(data->outbuf);
	if ((len + EAP_PWD_HDR_SIZE) > data->mtu) {
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD, data->mtu,
				     EAP_CODE_RESPONSE, eap_get_id(reqData));
		/*
		 * if so it's the first so include a length field
		 */
		EAP_PWD_SET_LENGTH_BIT(lm_exch);
		EAP_PWD_SET_MORE_BIT(lm_exch);
		tot_len = len;
		/*
		 * keep the packet at the MTU
		 */
		len = data->mtu - EAP_PWD_HDR_SIZE - sizeof(u16);
		wpa_printf(MSG_DEBUG, "EAP-pwd: Fragmenting output, total "
			   "length = %d", tot_len);
	} else {
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
				     EAP_PWD_HDR_SIZE + len,
				     EAP_CODE_RESPONSE, eap_get_id(reqData));
	}
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, lm_exch);
	if (EAP_PWD_GET_LENGTH_BIT(lm_exch)) {
		wpabuf_put_be16(resp, tot_len);
		data->out_frag_pos += len;
	}
	buf = wpabuf_head_u8(data->outbuf);
	wpabuf_put_data(resp, buf, len);
	/*
	 * if we're not fragmenting then there's no need to carry this around
	 */
	if (data->out_frag_pos == 0) {
		wpabuf_free(data->outbuf);
		data->outbuf = NULL;
		data->out_frag_pos = 0;
		if (data->state == SUCCESS_ON_FRAG_COMPLETION) {
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_UNCOND_SUCC;
			eap_pwd_state(data, SUCCESS);
		}
	}

	return resp;
}


static Boolean eap_pwd_key_available(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_pwd_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	if ((key = os_malloc(EAP_EMSK_LEN)) == NULL)
		return NULL;

	os_memcpy(key, data->emsk, EAP_EMSK_LEN);
	*len = EAP_EMSK_LEN;

	return key;
}


int eap_peer_pwd_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_PWD, "PWD");
	if (eap == NULL)
		return -1;

	eap->init = eap_pwd_init;
	eap->deinit = eap_pwd_deinit;
	eap->process = eap_pwd_process;
	eap->isKeyAvailable = eap_pwd_key_available;
	eap->getKey = eap_pwd_getkey;
	eap->getSessionId = eap_pwd_get_session_id;
	eap->get_emsk = eap_pwd_get_emsk;

	return eap_peer_method_register(eap);
}
