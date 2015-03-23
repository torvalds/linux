/*
 * hostapd / EAP-SIM database/authenticator gateway
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This is an example implementation of the EAP-SIM/AKA database/authentication
 * gateway interface that is using an external program as an SS7 gateway to
 * GSM/UMTS authentication center (HLR/AuC). hlr_auc_gw is an example
 * implementation of such a gateway program. This eap_sim_db.c takes care of
 * EAP-SIM/AKA pseudonyms and re-auth identities. It can be used with different
 * gateway implementations for HLR/AuC access. Alternatively, it can also be
 * completely replaced if the in-memory database of pseudonyms/re-auth
 * identities is not suitable for some cases.
 */

#include "includes.h"
#include <sys/un.h>

#include "common.h"
#include "crypto/random.h"
#include "eap_common/eap_sim_common.h"
#include "eap_server/eap_sim_db.h"
#include "eloop.h"

struct eap_sim_pseudonym {
	struct eap_sim_pseudonym *next;
	u8 *identity;
	size_t identity_len;
	char *pseudonym;
};

struct eap_sim_db_pending {
	struct eap_sim_db_pending *next;
	u8 imsi[20];
	size_t imsi_len;
	enum { PENDING, SUCCESS, FAILURE } state;
	void *cb_session_ctx;
	struct os_time timestamp;
	int aka;
	union {
		struct {
			u8 kc[EAP_SIM_MAX_CHAL][EAP_SIM_KC_LEN];
			u8 sres[EAP_SIM_MAX_CHAL][EAP_SIM_SRES_LEN];
			u8 rand[EAP_SIM_MAX_CHAL][GSM_RAND_LEN];
			int num_chal;
		} sim;
		struct {
			u8 rand[EAP_AKA_RAND_LEN];
			u8 autn[EAP_AKA_AUTN_LEN];
			u8 ik[EAP_AKA_IK_LEN];
			u8 ck[EAP_AKA_CK_LEN];
			u8 res[EAP_AKA_RES_MAX_LEN];
			size_t res_len;
		} aka;
	} u;
};

struct eap_sim_db_data {
	int sock;
	char *fname;
	char *local_sock;
	void (*get_complete_cb)(void *ctx, void *session_ctx);
	void *ctx;
	struct eap_sim_pseudonym *pseudonyms;
	struct eap_sim_reauth *reauths;
	struct eap_sim_db_pending *pending;
};


static struct eap_sim_db_pending *
eap_sim_db_get_pending(struct eap_sim_db_data *data, const u8 *imsi,
		       size_t imsi_len, int aka)
{
	struct eap_sim_db_pending *entry, *prev = NULL;

	entry = data->pending;
	while (entry) {
		if (entry->aka == aka && entry->imsi_len == imsi_len &&
		    os_memcmp(entry->imsi, imsi, imsi_len) == 0) {
			if (prev)
				prev->next = entry->next;
			else
				data->pending = entry->next;
			break;
		}
		prev = entry;
		entry = entry->next;
	}
	return entry;
}


static void eap_sim_db_add_pending(struct eap_sim_db_data *data,
				   struct eap_sim_db_pending *entry)
{
	entry->next = data->pending;
	data->pending = entry;
}


static void eap_sim_db_sim_resp_auth(struct eap_sim_db_data *data,
				     const char *imsi, char *buf)
{
	char *start, *end, *pos;
	struct eap_sim_db_pending *entry;
	int num_chal;

	/*
	 * SIM-RESP-AUTH <IMSI> Kc(i):SRES(i):RAND(i) ...
	 * SIM-RESP-AUTH <IMSI> FAILURE
	 * (IMSI = ASCII string, Kc/SRES/RAND = hex string)
	 */

	entry = eap_sim_db_get_pending(data, (u8 *) imsi, os_strlen(imsi), 0);
	if (entry == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: No pending entry for the "
			   "received message found");
		return;
	}

	start = buf;
	if (os_strncmp(start, "FAILURE", 7) == 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: External server reported "
			   "failure");
		entry->state = FAILURE;
		eap_sim_db_add_pending(data, entry);
		data->get_complete_cb(data->ctx, entry->cb_session_ctx);
		return;
	}

	num_chal = 0;
	while (num_chal < EAP_SIM_MAX_CHAL) {
		end = os_strchr(start, ' ');
		if (end)
			*end = '\0';

		pos = os_strchr(start, ':');
		if (pos == NULL)
			goto parse_fail;
		*pos = '\0';
		if (hexstr2bin(start, entry->u.sim.kc[num_chal],
			       EAP_SIM_KC_LEN))
			goto parse_fail;

		start = pos + 1;
		pos = os_strchr(start, ':');
		if (pos == NULL)
			goto parse_fail;
		*pos = '\0';
		if (hexstr2bin(start, entry->u.sim.sres[num_chal],
			       EAP_SIM_SRES_LEN))
			goto parse_fail;

		start = pos + 1;
		if (hexstr2bin(start, entry->u.sim.rand[num_chal],
			       GSM_RAND_LEN))
			goto parse_fail;

		num_chal++;
		if (end == NULL)
			break;
		else
			start = end + 1;
	}
	entry->u.sim.num_chal = num_chal;

	entry->state = SUCCESS;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Authentication data parsed "
		   "successfully - callback");
	eap_sim_db_add_pending(data, entry);
	data->get_complete_cb(data->ctx, entry->cb_session_ctx);
	return;

parse_fail:
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Failed to parse response string");
	os_free(entry);
}


static void eap_sim_db_aka_resp_auth(struct eap_sim_db_data *data,
				     const char *imsi, char *buf)
{
	char *start, *end;
	struct eap_sim_db_pending *entry;

	/*
	 * AKA-RESP-AUTH <IMSI> <RAND> <AUTN> <IK> <CK> <RES>
	 * AKA-RESP-AUTH <IMSI> FAILURE
	 * (IMSI = ASCII string, RAND/AUTN/IK/CK/RES = hex string)
	 */

	entry = eap_sim_db_get_pending(data, (u8 *) imsi, os_strlen(imsi), 1);
	if (entry == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: No pending entry for the "
			   "received message found");
		return;
	}

	start = buf;
	if (os_strncmp(start, "FAILURE", 7) == 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: External server reported "
			   "failure");
		entry->state = FAILURE;
		eap_sim_db_add_pending(data, entry);
		data->get_complete_cb(data->ctx, entry->cb_session_ctx);
		return;
	}

	end = os_strchr(start, ' ');
	if (end == NULL)
		goto parse_fail;
	*end = '\0';
	if (hexstr2bin(start, entry->u.aka.rand, EAP_AKA_RAND_LEN))
		goto parse_fail;

	start = end + 1;
	end = os_strchr(start, ' ');
	if (end == NULL)
		goto parse_fail;
	*end = '\0';
	if (hexstr2bin(start, entry->u.aka.autn, EAP_AKA_AUTN_LEN))
		goto parse_fail;

	start = end + 1;
	end = os_strchr(start, ' ');
	if (end == NULL)
		goto parse_fail;
	*end = '\0';
	if (hexstr2bin(start, entry->u.aka.ik, EAP_AKA_IK_LEN))
		goto parse_fail;

	start = end + 1;
	end = os_strchr(start, ' ');
	if (end == NULL)
		goto parse_fail;
	*end = '\0';
	if (hexstr2bin(start, entry->u.aka.ck, EAP_AKA_CK_LEN))
		goto parse_fail;

	start = end + 1;
	end = os_strchr(start, ' ');
	if (end)
		*end = '\0';
	else {
		end = start;
		while (*end)
			end++;
	}
	entry->u.aka.res_len = (end - start) / 2;
	if (entry->u.aka.res_len > EAP_AKA_RES_MAX_LEN) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Too long RES");
		entry->u.aka.res_len = 0;
		goto parse_fail;
	}
	if (hexstr2bin(start, entry->u.aka.res, entry->u.aka.res_len))
		goto parse_fail;

	entry->state = SUCCESS;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Authentication data parsed "
		   "successfully - callback");
	eap_sim_db_add_pending(data, entry);
	data->get_complete_cb(data->ctx, entry->cb_session_ctx);
	return;

parse_fail:
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Failed to parse response string");
	os_free(entry);
}


static void eap_sim_db_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct eap_sim_db_data *data = eloop_ctx;
	char buf[1000], *pos, *cmd, *imsi;
	int res;

	res = recv(sock, buf, sizeof(buf), 0);
	if (res < 0)
		return;
	wpa_hexdump_ascii_key(MSG_MSGDUMP, "EAP-SIM DB: Received from an "
			      "external source", (u8 *) buf, res);
	if (res == 0)
		return;
	if (res >= (int) sizeof(buf))
		res = sizeof(buf) - 1;
	buf[res] = '\0';

	if (data->get_complete_cb == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: No get_complete_cb "
			   "registered");
		return;
	}

	/* <cmd> <IMSI> ... */

	cmd = buf;
	pos = os_strchr(cmd, ' ');
	if (pos == NULL)
		goto parse_fail;
	*pos = '\0';
	imsi = pos + 1;
	pos = os_strchr(imsi, ' ');
	if (pos == NULL)
		goto parse_fail;
	*pos = '\0';
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: External response=%s for IMSI %s",
		   cmd, imsi);

	if (os_strcmp(cmd, "SIM-RESP-AUTH") == 0)
		eap_sim_db_sim_resp_auth(data, imsi, pos + 1);
	else if (os_strcmp(cmd, "AKA-RESP-AUTH") == 0)
		eap_sim_db_aka_resp_auth(data, imsi, pos + 1);
	else
		wpa_printf(MSG_INFO, "EAP-SIM DB: Unknown external response "
			   "'%s'", cmd);
	return;

parse_fail:
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Failed to parse response string");
}


static int eap_sim_db_open_socket(struct eap_sim_db_data *data)
{
	struct sockaddr_un addr;
	static int counter = 0;

	if (os_strncmp(data->fname, "unix:", 5) != 0)
		return -1;

	data->sock = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (data->sock < 0) {
		perror("socket(eap_sim_db)");
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_snprintf(addr.sun_path, sizeof(addr.sun_path),
		    "/tmp/eap_sim_db_%d-%d", getpid(), counter++);
	data->local_sock = os_strdup(addr.sun_path);
	if (bind(data->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind(eap_sim_db)");
		close(data->sock);
		data->sock = -1;
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, data->fname + 5, sizeof(addr.sun_path));
	if (connect(data->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect(eap_sim_db)");
		wpa_hexdump_ascii(MSG_INFO, "HLR/AuC GW socket",
				  (u8 *) addr.sun_path,
				  os_strlen(addr.sun_path));
		close(data->sock);
		data->sock = -1;
		return -1;
	}

	eloop_register_read_sock(data->sock, eap_sim_db_receive, data, NULL);

	return 0;
}


static void eap_sim_db_close_socket(struct eap_sim_db_data *data)
{
	if (data->sock >= 0) {
		eloop_unregister_read_sock(data->sock);
		close(data->sock);
		data->sock = -1;
	}
	if (data->local_sock) {
		unlink(data->local_sock);
		os_free(data->local_sock);
		data->local_sock = NULL;
	}
}


/**
 * eap_sim_db_init - Initialize EAP-SIM DB / authentication gateway interface
 * @config: Configuration data (e.g., file name)
 * @get_complete_cb: Callback function for reporting availability of triplets
 * @ctx: Context pointer for get_complete_cb
 * Returns: Pointer to a private data structure or %NULL on failure
 */
void * eap_sim_db_init(const char *config,
		       void (*get_complete_cb)(void *ctx, void *session_ctx),
		       void *ctx)
{
	struct eap_sim_db_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	data->sock = -1;
	data->get_complete_cb = get_complete_cb;
	data->ctx = ctx;
	data->fname = os_strdup(config);
	if (data->fname == NULL)
		goto fail;

	if (os_strncmp(data->fname, "unix:", 5) == 0) {
		if (eap_sim_db_open_socket(data))
			goto fail;
	}

	return data;

fail:
	eap_sim_db_close_socket(data);
	os_free(data->fname);
	os_free(data);
	return NULL;
}


static void eap_sim_db_free_pseudonym(struct eap_sim_pseudonym *p)
{
	os_free(p->identity);
	os_free(p->pseudonym);
	os_free(p);
}


static void eap_sim_db_free_reauth(struct eap_sim_reauth *r)
{
	os_free(r->identity);
	os_free(r->reauth_id);
	os_free(r);
}


/**
 * eap_sim_db_deinit - Deinitialize EAP-SIM DB/authentication gw interface
 * @priv: Private data pointer from eap_sim_db_init()
 */
void eap_sim_db_deinit(void *priv)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_pseudonym *p, *prev;
	struct eap_sim_reauth *r, *prevr;
	struct eap_sim_db_pending *pending, *prev_pending;

	eap_sim_db_close_socket(data);
	os_free(data->fname);

	p = data->pseudonyms;
	while (p) {
		prev = p;
		p = p->next;
		eap_sim_db_free_pseudonym(prev);
	}

	r = data->reauths;
	while (r) {
		prevr = r;
		r = r->next;
		eap_sim_db_free_reauth(prevr);
	}

	pending = data->pending;
	while (pending) {
		prev_pending = pending;
		pending = pending->next;
		os_free(prev_pending);
	}

	os_free(data);
}


static int eap_sim_db_send(struct eap_sim_db_data *data, const char *msg,
			   size_t len)
{
	int _errno = 0;

	if (send(data->sock, msg, len, 0) < 0) {
		_errno = errno;
		perror("send[EAP-SIM DB UNIX]");
	}

	if (_errno == ENOTCONN || _errno == EDESTADDRREQ || _errno == EINVAL ||
	    _errno == ECONNREFUSED) {
		/* Try to reconnect */
		eap_sim_db_close_socket(data);
		if (eap_sim_db_open_socket(data) < 0)
			return -1;
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Reconnected to the "
			   "external server");
		if (send(data->sock, msg, len, 0) < 0) {
			perror("send[EAP-SIM DB UNIX]");
			return -1;
		}
	}

	return 0;
}


static void eap_sim_db_expire_pending(struct eap_sim_db_data *data)
{
	/* TODO: add limit for maximum length for pending list; remove latest
	 * (i.e., last) entry from the list if the limit is reached; could also
	 * use timeout to expire pending entries */
}


/**
 * eap_sim_db_get_gsm_triplets - Get GSM triplets
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: User name identity
 * @identity_len: Length of identity in bytes
 * @max_chal: Maximum number of triplets
 * @_rand: Buffer for RAND values
 * @kc: Buffer for Kc values
 * @sres: Buffer for SRES values
 * @cb_session_ctx: Session callback context for get_complete_cb()
 * Returns: Number of triplets received (has to be less than or equal to
 * max_chal), -1 (EAP_SIM_DB_FAILURE) on error (e.g., user not found), or
 * -2 (EAP_SIM_DB_PENDING) if results are not yet available. In this case, the
 * callback function registered with eap_sim_db_init() will be called once the
 * results become available.
 *
 * In most cases, the user name is '1' | IMSI, i.e., 1 followed by the IMSI in
 * ASCII format.
 *
 * When using an external server for GSM triplets, this function can always
 * start a request and return EAP_SIM_DB_PENDING immediately if authentication
 * triplets are not available. Once the triplets are received, callback
 * function registered with eap_sim_db_init() is called to notify EAP state
 * machine to reprocess the message. This eap_sim_db_get_gsm_triplets()
 * function will then be called again and the newly received triplets will then
 * be given to the caller.
 */
int eap_sim_db_get_gsm_triplets(void *priv, const u8 *identity,
				size_t identity_len, int max_chal,
				u8 *_rand, u8 *kc, u8 *sres,
				void *cb_session_ctx)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_db_pending *entry;
	int len, ret;
	size_t i;
	char msg[40];

	if (identity_len < 2 || identity[0] != EAP_SIM_PERMANENT_PREFIX) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return EAP_SIM_DB_FAILURE;
	}
	identity++;
	identity_len--;
	for (i = 0; i < identity_len; i++) {
		if (identity[i] == '@') {
			identity_len = i;
			break;
		}
	}
	if (identity_len + 1 > sizeof(entry->imsi)) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return EAP_SIM_DB_FAILURE;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: Get GSM triplets for IMSI",
			  identity, identity_len);

	entry = eap_sim_db_get_pending(data, identity, identity_len, 0);
	if (entry) {
		int num_chal;
		if (entry->state == FAILURE) {
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pending entry -> "
				   "failure");
			os_free(entry);
			return EAP_SIM_DB_FAILURE;
		}

		if (entry->state == PENDING) {
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pending entry -> "
				   "still pending");
			eap_sim_db_add_pending(data, entry);
			return EAP_SIM_DB_PENDING;
		}

		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pending entry -> "
			   "%d challenges", entry->u.sim.num_chal);
		num_chal = entry->u.sim.num_chal;
		if (num_chal > max_chal)
			num_chal = max_chal;
		os_memcpy(_rand, entry->u.sim.rand, num_chal * GSM_RAND_LEN);
		os_memcpy(sres, entry->u.sim.sres,
			  num_chal * EAP_SIM_SRES_LEN);
		os_memcpy(kc, entry->u.sim.kc, num_chal * EAP_SIM_KC_LEN);
		os_free(entry);
		return num_chal;
	}

	if (data->sock < 0) {
		if (eap_sim_db_open_socket(data) < 0)
			return EAP_SIM_DB_FAILURE;
	}

	len = os_snprintf(msg, sizeof(msg), "SIM-REQ-AUTH ");
	if (len < 0 || len + identity_len >= sizeof(msg))
		return EAP_SIM_DB_FAILURE;
	os_memcpy(msg + len, identity, identity_len);
	len += identity_len;
	ret = os_snprintf(msg + len, sizeof(msg) - len, " %d", max_chal);
	if (ret < 0 || (size_t) ret >= sizeof(msg) - len)
		return EAP_SIM_DB_FAILURE;
	len += ret;

	wpa_hexdump(MSG_DEBUG, "EAP-SIM DB: requesting SIM authentication "
		    "data for IMSI", identity, identity_len);
	if (eap_sim_db_send(data, msg, len) < 0)
		return EAP_SIM_DB_FAILURE;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return EAP_SIM_DB_FAILURE;

	os_get_time(&entry->timestamp);
	os_memcpy(entry->imsi, identity, identity_len);
	entry->imsi_len = identity_len;
	entry->cb_session_ctx = cb_session_ctx;
	entry->state = PENDING;
	eap_sim_db_add_pending(data, entry);
	eap_sim_db_expire_pending(data);

	return EAP_SIM_DB_PENDING;
}


static struct eap_sim_pseudonym *
eap_sim_db_get_pseudonym(struct eap_sim_db_data *data, const u8 *identity,
			 size_t identity_len)
{
	char *pseudonym;
	size_t len;
	struct eap_sim_pseudonym *p;

	if (identity_len == 0 ||
	    (identity[0] != EAP_SIM_PSEUDONYM_PREFIX &&
	     identity[0] != EAP_AKA_PSEUDONYM_PREFIX))
		return NULL;

	/* Remove possible realm from identity */
	len = 0;
	while (len < identity_len) {
		if (identity[len] == '@')
			break;
		len++;
	}

	pseudonym = os_malloc(len + 1);
	if (pseudonym == NULL)
		return NULL;
	os_memcpy(pseudonym, identity, len);
	pseudonym[len] = '\0';

	p = data->pseudonyms;
	while (p) {
		if (os_strcmp(p->pseudonym, pseudonym) == 0)
			break;
		p = p->next;
	}

	os_free(pseudonym);

	return p;
}


static struct eap_sim_pseudonym *
eap_sim_db_get_pseudonym_id(struct eap_sim_db_data *data, const u8 *identity,
			    size_t identity_len)
{
	struct eap_sim_pseudonym *p;

	if (identity_len == 0 ||
	    (identity[0] != EAP_SIM_PERMANENT_PREFIX &&
	     identity[0] != EAP_AKA_PERMANENT_PREFIX))
		return NULL;

	p = data->pseudonyms;
	while (p) {
		if (identity_len == p->identity_len &&
		    os_memcmp(p->identity, identity, identity_len) == 0)
			break;
		p = p->next;
	}

	return p;
}


static struct eap_sim_reauth *
eap_sim_db_get_reauth(struct eap_sim_db_data *data, const u8 *identity,
		      size_t identity_len)
{
	char *reauth_id;
	size_t len;
	struct eap_sim_reauth *r;

	if (identity_len == 0 ||
	    (identity[0] != EAP_SIM_REAUTH_ID_PREFIX &&
	     identity[0] != EAP_AKA_REAUTH_ID_PREFIX))
		return NULL;

	/* Remove possible realm from identity */
	len = 0;
	while (len < identity_len) {
		if (identity[len] == '@')
			break;
		len++;
	}

	reauth_id = os_malloc(len + 1);
	if (reauth_id == NULL)
		return NULL;
	os_memcpy(reauth_id, identity, len);
	reauth_id[len] = '\0';

	r = data->reauths;
	while (r) {
		if (os_strcmp(r->reauth_id, reauth_id) == 0)
			break;
		r = r->next;
	}

	os_free(reauth_id);

	return r;
}


static struct eap_sim_reauth *
eap_sim_db_get_reauth_id(struct eap_sim_db_data *data, const u8 *identity,
			 size_t identity_len)
{
	struct eap_sim_pseudonym *p;
	struct eap_sim_reauth *r;

	if (identity_len == 0)
		return NULL;

	p = eap_sim_db_get_pseudonym(data, identity, identity_len);
	if (p == NULL)
		p = eap_sim_db_get_pseudonym_id(data, identity, identity_len);
	if (p) {
		identity = p->identity;
		identity_len = p->identity_len;
	}

	r = data->reauths;
	while (r) {
		if (identity_len == r->identity_len &&
		    os_memcmp(r->identity, identity, identity_len) == 0)
			break;
		r = r->next;
	}

	return r;
}


/**
 * eap_sim_db_identity_known - Verify whether the given identity is known
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: User name identity
 * @identity_len: Length of identity in bytes 
 * Returns: 0 if the user is found or -1 on failure
 *
 * In most cases, the user name is ['0','1'] | IMSI, i.e., 1 followed by the
 * IMSI in ASCII format, ['2','3'] | pseudonym, or ['4','5'] | reauth_id.
 */
int eap_sim_db_identity_known(void *priv, const u8 *identity,
			      size_t identity_len)
{
	struct eap_sim_db_data *data = priv;

	if (identity == NULL || identity_len < 2)
		return -1;

	if (identity[0] == EAP_SIM_PSEUDONYM_PREFIX ||
	    identity[0] == EAP_AKA_PSEUDONYM_PREFIX) {
		struct eap_sim_pseudonym *p =
			eap_sim_db_get_pseudonym(data, identity, identity_len);
		return p ? 0 : -1;
	}

	if (identity[0] == EAP_SIM_REAUTH_ID_PREFIX ||
	    identity[0] == EAP_AKA_REAUTH_ID_PREFIX) {
		struct eap_sim_reauth *r =
			eap_sim_db_get_reauth(data, identity, identity_len);
		return r ? 0 : -1;
	}

	if (identity[0] != EAP_SIM_PERMANENT_PREFIX &&
	    identity[0] != EAP_AKA_PERMANENT_PREFIX) {
		/* Unknown identity prefix */
		return -1;
	}

	/* TODO: Should consider asking HLR/AuC gateway whether this permanent
	 * identity is known. If it is, EAP-SIM/AKA can skip identity request.
	 * In case of EAP-AKA, this would reduce number of needed round-trips.
	 * Ideally, this would be done with one wait, i.e., just request
	 * authentication data and store it for the next use. This would then
	 * need to use similar pending-request functionality as the normal
	 * request for authentication data at later phase.
	 */
	return -1;
}


static char * eap_sim_db_get_next(struct eap_sim_db_data *data, char prefix)
{
	char *id, *pos, *end;
	u8 buf[10];

	if (random_get_bytes(buf, sizeof(buf)))
		return NULL;
	id = os_malloc(sizeof(buf) * 2 + 2);
	if (id == NULL)
		return NULL;

	pos = id;
	end = id + sizeof(buf) * 2 + 2;
	*pos++ = prefix;
	pos += wpa_snprintf_hex(pos, end - pos, buf, sizeof(buf));
	
	return id;
}


/**
 * eap_sim_db_get_next_pseudonym - EAP-SIM DB: Get next pseudonym
 * @priv: Private data pointer from eap_sim_db_init()
 * @aka: Using EAP-AKA instead of EAP-SIM
 * Returns: Next pseudonym (allocated string) or %NULL on failure
 *
 * This function is used to generate a pseudonym for EAP-SIM. The returned
 * pseudonym is not added to database at this point; it will need to be added
 * with eap_sim_db_add_pseudonym() once the authentication has been completed
 * successfully. Caller is responsible for freeing the returned buffer.
 */
char * eap_sim_db_get_next_pseudonym(void *priv, int aka)
{
	struct eap_sim_db_data *data = priv;
	return eap_sim_db_get_next(data, aka ? EAP_AKA_PSEUDONYM_PREFIX :
				   EAP_SIM_PSEUDONYM_PREFIX);
}


/**
 * eap_sim_db_get_next_reauth_id - EAP-SIM DB: Get next reauth_id
 * @priv: Private data pointer from eap_sim_db_init()
 * @aka: Using EAP-AKA instead of EAP-SIM
 * Returns: Next reauth_id (allocated string) or %NULL on failure
 *
 * This function is used to generate a fast re-authentication identity for
 * EAP-SIM. The returned reauth_id is not added to database at this point; it
 * will need to be added with eap_sim_db_add_reauth() once the authentication
 * has been completed successfully. Caller is responsible for freeing the
 * returned buffer.
 */
char * eap_sim_db_get_next_reauth_id(void *priv, int aka)
{
	struct eap_sim_db_data *data = priv;
	return eap_sim_db_get_next(data, aka ? EAP_AKA_REAUTH_ID_PREFIX :
				   EAP_SIM_REAUTH_ID_PREFIX);
}


/**
 * eap_sim_db_add_pseudonym - EAP-SIM DB: Add new pseudonym
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: Identity of the user (may be permanent identity or pseudonym)
 * @identity_len: Length of identity
 * @pseudonym: Pseudonym for this user. This needs to be an allocated buffer,
 * e.g., return value from eap_sim_db_get_next_pseudonym(). Caller must not
 * free it.
 * Returns: 0 on success, -1 on failure
 *
 * This function adds a new pseudonym for EAP-SIM user. EAP-SIM DB is
 * responsible of freeing pseudonym buffer once it is not needed anymore.
 */
int eap_sim_db_add_pseudonym(void *priv, const u8 *identity,
			     size_t identity_len, char *pseudonym)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_pseudonym *p;
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: Add pseudonym for identity",
			  identity, identity_len);
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pseudonym: %s", pseudonym);

	/* TODO: could store last two pseudonyms */
	p = eap_sim_db_get_pseudonym(data, identity, identity_len);
	if (p == NULL)
		p = eap_sim_db_get_pseudonym_id(data, identity, identity_len);

	if (p) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Replacing previous "
			   "pseudonym: %s", p->pseudonym);
		os_free(p->pseudonym);
		p->pseudonym = pseudonym;
		return 0;
	}

	p = os_zalloc(sizeof(*p));
	if (p == NULL) {
		os_free(pseudonym);
		return -1;
	}

	p->next = data->pseudonyms;
	p->identity = os_malloc(identity_len);
	if (p->identity == NULL) {
		os_free(p);
		os_free(pseudonym);
		return -1;
	}
	os_memcpy(p->identity, identity, identity_len);
	p->identity_len = identity_len;
	p->pseudonym = pseudonym;
	data->pseudonyms = p;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Added new pseudonym entry");
	return 0;
}


static struct eap_sim_reauth *
eap_sim_db_add_reauth_data(struct eap_sim_db_data *data, const u8 *identity,
			   size_t identity_len, char *reauth_id, u16 counter)
{
	struct eap_sim_reauth *r;

	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: Add reauth_id for identity",
			  identity, identity_len);
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: reauth_id: %s", reauth_id);

	r = eap_sim_db_get_reauth(data, identity, identity_len);
	if (r == NULL)
		r = eap_sim_db_get_reauth_id(data, identity, identity_len);

	if (r) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Replacing previous "
			   "reauth_id: %s", r->reauth_id);
		os_free(r->reauth_id);
		r->reauth_id = reauth_id;
	} else {
		r = os_zalloc(sizeof(*r));
		if (r == NULL) {
			os_free(reauth_id);
			return NULL;
		}

		r->next = data->reauths;
		r->identity = os_malloc(identity_len);
		if (r->identity == NULL) {
			os_free(r);
			os_free(reauth_id);
			return NULL;
		}
		os_memcpy(r->identity, identity, identity_len);
		r->identity_len = identity_len;
		r->reauth_id = reauth_id;
		data->reauths = r;
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Added new reauth entry");
	}

	r->counter = counter;

	return r;
}


/**
 * eap_sim_db_add_reauth - EAP-SIM DB: Add new re-authentication entry
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: Identity of the user (may be permanent identity or pseudonym)
 * @identity_len: Length of identity
 * @reauth_id: reauth_id for this user. This needs to be an allocated buffer,
 * e.g., return value from eap_sim_db_get_next_reauth_id(). Caller must not
 * free it.
 * @counter: AT_COUNTER value for fast re-authentication
 * @mk: 16-byte MK from the previous full authentication or %NULL
 * Returns: 0 on success, -1 on failure
 *
 * This function adds a new re-authentication entry for an EAP-SIM user.
 * EAP-SIM DB is responsible of freeing reauth_id buffer once it is not needed
 * anymore.
 */
int eap_sim_db_add_reauth(void *priv, const u8 *identity,
			  size_t identity_len, char *reauth_id, u16 counter,
			  const u8 *mk)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_reauth *r;

	r = eap_sim_db_add_reauth_data(data, identity, identity_len, reauth_id,
				       counter);
	if (r == NULL)
		return -1;

	os_memcpy(r->mk, mk, EAP_SIM_MK_LEN);
	r->aka_prime = 0;

	return 0;
}


#ifdef EAP_SERVER_AKA_PRIME
/**
 * eap_sim_db_add_reauth_prime - EAP-AKA' DB: Add new re-authentication entry
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: Identity of the user (may be permanent identity or pseudonym)
 * @identity_len: Length of identity
 * @reauth_id: reauth_id for this user. This needs to be an allocated buffer,
 * e.g., return value from eap_sim_db_get_next_reauth_id(). Caller must not
 * free it.
 * @counter: AT_COUNTER value for fast re-authentication
 * @k_encr: K_encr from the previous full authentication
 * @k_aut: K_aut from the previous full authentication
 * @k_re: 32-byte K_re from the previous full authentication
 * Returns: 0 on success, -1 on failure
 *
 * This function adds a new re-authentication entry for an EAP-AKA' user.
 * EAP-SIM DB is responsible of freeing reauth_id buffer once it is not needed
 * anymore.
 */
int eap_sim_db_add_reauth_prime(void *priv, const u8 *identity,
				size_t identity_len, char *reauth_id,
				u16 counter, const u8 *k_encr, const u8 *k_aut,
				const u8 *k_re)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_reauth *r;

	r = eap_sim_db_add_reauth_data(data, identity, identity_len, reauth_id,
				       counter);
	if (r == NULL)
		return -1;

	r->aka_prime = 1;
	os_memcpy(r->k_encr, k_encr, EAP_SIM_K_ENCR_LEN);
	os_memcpy(r->k_aut, k_aut, EAP_AKA_PRIME_K_AUT_LEN);
	os_memcpy(r->k_re, k_re, EAP_AKA_PRIME_K_RE_LEN);

	return 0;
}
#endif /* EAP_SERVER_AKA_PRIME */


/**
 * eap_sim_db_get_permanent - EAP-SIM DB: Get permanent identity
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: Identity of the user (may be permanent identity or pseudonym)
 * @identity_len: Length of identity
 * @len: Buffer for length of the returned permanent identity
 * Returns: Pointer to the permanent identity, or %NULL if not found
 */
const u8 * eap_sim_db_get_permanent(void *priv, const u8 *identity,
				    size_t identity_len, size_t *len)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_pseudonym *p;

	if (identity == NULL)
		return NULL;

	p = eap_sim_db_get_pseudonym(data, identity, identity_len);
	if (p == NULL)
		p = eap_sim_db_get_pseudonym_id(data, identity, identity_len);
	if (p == NULL)
		return NULL;

	*len = p->identity_len;
	return p->identity;
}


/**
 * eap_sim_db_get_reauth_entry - EAP-SIM DB: Get re-authentication entry
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: Identity of the user (may be permanent identity, pseudonym, or
 * reauth_id)
 * @identity_len: Length of identity
 * Returns: Pointer to the re-auth entry, or %NULL if not found
 */
struct eap_sim_reauth *
eap_sim_db_get_reauth_entry(void *priv, const u8 *identity,
			    size_t identity_len)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_reauth *r;

	if (identity == NULL)
		return NULL;
	r = eap_sim_db_get_reauth(data, identity, identity_len);
	if (r == NULL)
		r = eap_sim_db_get_reauth_id(data, identity, identity_len);
	return r;
}


/**
 * eap_sim_db_remove_reauth - EAP-SIM DB: Remove re-authentication entry
 * @priv: Private data pointer from eap_sim_db_init()
 * @reauth: Pointer to re-authentication entry from
 * eap_sim_db_get_reauth_entry()
 */
void eap_sim_db_remove_reauth(void *priv, struct eap_sim_reauth *reauth)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_reauth *r, *prev = NULL;
	r = data->reauths;
	while (r) {
		if (r == reauth) {
			if (prev)
				prev->next = r->next;
			else
				data->reauths = r->next;
			eap_sim_db_free_reauth(r);
			return;
		}
		prev = r;
		r = r->next;
	}
}


/**
 * eap_sim_db_get_aka_auth - Get AKA authentication values
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: User name identity
 * @identity_len: Length of identity in bytes
 * @_rand: Buffer for RAND value
 * @autn: Buffer for AUTN value
 * @ik: Buffer for IK value
 * @ck: Buffer for CK value
 * @res: Buffer for RES value
 * @res_len: Buffer for RES length
 * @cb_session_ctx: Session callback context for get_complete_cb()
 * Returns: 0 on success, -1 (EAP_SIM_DB_FAILURE) on error (e.g., user not
 * found), or -2 (EAP_SIM_DB_PENDING) if results are not yet available. In this
 * case, the callback function registered with eap_sim_db_init() will be
 * called once the results become available.
 *
 * In most cases, the user name is '0' | IMSI, i.e., 0 followed by the IMSI in
 * ASCII format.
 *
 * When using an external server for AKA authentication, this function can
 * always start a request and return EAP_SIM_DB_PENDING immediately if
 * authentication triplets are not available. Once the authentication data are
 * received, callback function registered with eap_sim_db_init() is called to
 * notify EAP state machine to reprocess the message. This
 * eap_sim_db_get_aka_auth() function will then be called again and the newly
 * received triplets will then be given to the caller.
 */
int eap_sim_db_get_aka_auth(void *priv, const u8 *identity,
			    size_t identity_len, u8 *_rand, u8 *autn, u8 *ik,
			    u8 *ck, u8 *res, size_t *res_len,
			    void *cb_session_ctx)
{
	struct eap_sim_db_data *data = priv;
	struct eap_sim_db_pending *entry;
	int len;
	size_t i;
	char msg[40];

	if (identity_len < 2 || identity == NULL ||
	    identity[0] != EAP_AKA_PERMANENT_PREFIX) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return EAP_SIM_DB_FAILURE;
	}
	identity++;
	identity_len--;
	for (i = 0; i < identity_len; i++) {
		if (identity[i] == '@') {
			identity_len = i;
			break;
		}
	}
	if (identity_len + 1 > sizeof(entry->imsi)) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return EAP_SIM_DB_FAILURE;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: Get AKA auth for IMSI",
			  identity, identity_len);

	entry = eap_sim_db_get_pending(data, identity, identity_len, 1);
	if (entry) {
		if (entry->state == FAILURE) {
			os_free(entry);
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: Failure");
			return EAP_SIM_DB_FAILURE;
		}

		if (entry->state == PENDING) {
			eap_sim_db_add_pending(data, entry);
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pending");
			return EAP_SIM_DB_PENDING;
		}

		wpa_printf(MSG_DEBUG, "EAP-SIM DB: Returning successfully "
			   "received authentication data");
		os_memcpy(_rand, entry->u.aka.rand, EAP_AKA_RAND_LEN);
		os_memcpy(autn, entry->u.aka.autn, EAP_AKA_AUTN_LEN);
		os_memcpy(ik, entry->u.aka.ik, EAP_AKA_IK_LEN);
		os_memcpy(ck, entry->u.aka.ck, EAP_AKA_CK_LEN);
		os_memcpy(res, entry->u.aka.res, EAP_AKA_RES_MAX_LEN);
		*res_len = entry->u.aka.res_len;
		os_free(entry);
		return 0;
	}

	if (data->sock < 0) {
		if (eap_sim_db_open_socket(data) < 0)
			return EAP_SIM_DB_FAILURE;
	}

	len = os_snprintf(msg, sizeof(msg), "AKA-REQ-AUTH ");
	if (len < 0 || len + identity_len >= sizeof(msg))
		return EAP_SIM_DB_FAILURE;
	os_memcpy(msg + len, identity, identity_len);
	len += identity_len;

	wpa_hexdump(MSG_DEBUG, "EAP-SIM DB: requesting AKA authentication "
		    "data for IMSI", identity, identity_len);
	if (eap_sim_db_send(data, msg, len) < 0)
		return EAP_SIM_DB_FAILURE;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return EAP_SIM_DB_FAILURE;

	os_get_time(&entry->timestamp);
	entry->aka = 1;
	os_memcpy(entry->imsi, identity, identity_len);
	entry->imsi_len = identity_len;
	entry->cb_session_ctx = cb_session_ctx;
	entry->state = PENDING;
	eap_sim_db_add_pending(data, entry);
	eap_sim_db_expire_pending(data);

	return EAP_SIM_DB_PENDING;
}


/**
 * eap_sim_db_resynchronize - Resynchronize AKA AUTN
 * @priv: Private data pointer from eap_sim_db_init()
 * @identity: User name identity
 * @identity_len: Length of identity in bytes
 * @auts: AUTS value from the peer
 * @_rand: RAND value used in the rejected message
 * Returns: 0 on success, -1 on failure
 *
 * This function is called when the peer reports synchronization failure in the
 * AUTN value by sending AUTS. The AUTS and RAND values should be sent to
 * HLR/AuC to allow it to resynchronize with the peer. After this,
 * eap_sim_db_get_aka_auth() will be called again to to fetch updated
 * RAND/AUTN values for the next challenge.
 */
int eap_sim_db_resynchronize(void *priv, const u8 *identity,
			     size_t identity_len, const u8 *auts,
			     const u8 *_rand)
{
	struct eap_sim_db_data *data = priv;
	size_t i;

	if (identity_len < 2 || identity == NULL ||
	    identity[0] != EAP_AKA_PERMANENT_PREFIX) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return -1;
	}
	identity++;
	identity_len--;
	for (i = 0; i < identity_len; i++) {
		if (identity[i] == '@') {
			identity_len = i;
			break;
		}
	}
	if (identity_len > 20) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM DB: unexpected identity",
				  identity, identity_len);
		return -1;
	}

	if (data->sock >= 0) {
		char msg[100];
		int len, ret;

		len = os_snprintf(msg, sizeof(msg), "AKA-AUTS ");
		if (len < 0 || len + identity_len >= sizeof(msg))
			return -1;
		os_memcpy(msg + len, identity, identity_len);
		len += identity_len;

		ret = os_snprintf(msg + len, sizeof(msg) - len, " ");
		if (ret < 0 || (size_t) ret >= sizeof(msg) - len)
			return -1;
		len += ret;
		len += wpa_snprintf_hex(msg + len, sizeof(msg) - len,
					auts, EAP_AKA_AUTS_LEN);
		ret = os_snprintf(msg + len, sizeof(msg) - len, " ");
		if (ret < 0 || (size_t) ret >= sizeof(msg) - len)
			return -1;
		len += ret;
		len += wpa_snprintf_hex(msg + len, sizeof(msg) - len,
					_rand, EAP_AKA_RAND_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-SIM DB: reporting AKA AUTS for "
			    "IMSI", identity, identity_len);
		if (eap_sim_db_send(data, msg, len) < 0)
			return -1;
	}

	return 0;
}
