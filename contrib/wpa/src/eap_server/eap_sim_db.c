/*
 * hostapd / EAP-SIM database/authenticator gateway
 * Copyright (c) 2005-2010, 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
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
#ifdef CONFIG_SQLITE
#include <sqlite3.h>
#endif /* CONFIG_SQLITE */

#include "common.h"
#include "crypto/random.h"
#include "eap_common/eap_sim_common.h"
#include "eap_server/eap_sim_db.h"
#include "eloop.h"

struct eap_sim_pseudonym {
	struct eap_sim_pseudonym *next;
	char *permanent; /* permanent username */
	char *pseudonym; /* pseudonym username */
};

struct eap_sim_db_pending {
	struct eap_sim_db_pending *next;
	char imsi[20];
	enum { PENDING, SUCCESS, FAILURE } state;
	void *cb_session_ctx;
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
	unsigned int eap_sim_db_timeout;
#ifdef CONFIG_SQLITE
	sqlite3 *sqlite_db;
	char db_tmp_identity[100];
	char db_tmp_pseudonym_str[100];
	struct eap_sim_pseudonym db_tmp_pseudonym;
	struct eap_sim_reauth db_tmp_reauth;
#endif /* CONFIG_SQLITE */
};


static void eap_sim_db_del_timeout(void *eloop_ctx, void *user_ctx);
static void eap_sim_db_query_timeout(void *eloop_ctx, void *user_ctx);


#ifdef CONFIG_SQLITE

static int db_table_exists(sqlite3 *db, const char *name)
{
	char cmd[128];
	os_snprintf(cmd, sizeof(cmd), "SELECT 1 FROM %s;", name);
	return sqlite3_exec(db, cmd, NULL, NULL, NULL) == SQLITE_OK;
}


static int db_table_create_pseudonym(sqlite3 *db)
{
	char *err = NULL;
	const char *sql =
		"CREATE TABLE pseudonyms("
		"  permanent CHAR(21) PRIMARY KEY,"
		"  pseudonym CHAR(21) NOT NULL"
		");";

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Adding database table for "
		   "pseudonym information");
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		wpa_printf(MSG_ERROR, "EAP-SIM DB: SQLite error: %s", err);
		sqlite3_free(err);
		return -1;
	}

	return 0;
}


static int db_table_create_reauth(sqlite3 *db)
{
	char *err = NULL;
	const char *sql =
		"CREATE TABLE reauth("
		"  permanent CHAR(21) PRIMARY KEY,"
		"  reauth_id CHAR(21) NOT NULL,"
		"  counter INTEGER,"
		"  mk CHAR(40),"
		"  k_encr CHAR(32),"
		"  k_aut CHAR(64),"
		"  k_re CHAR(64)"
		");";

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Adding database table for "
		   "reauth information");
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		wpa_printf(MSG_ERROR, "EAP-SIM DB: SQLite error: %s", err);
		sqlite3_free(err);
		return -1;
	}

	return 0;
}


static sqlite3 * db_open(const char *db_file)
{
	sqlite3 *db;

	if (sqlite3_open(db_file, &db)) {
		wpa_printf(MSG_ERROR, "EAP-SIM DB: Failed to open database "
			   "%s: %s", db_file, sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	if (!db_table_exists(db, "pseudonyms") &&
	    db_table_create_pseudonym(db) < 0) {
		sqlite3_close(db);
		return NULL;
	}

	if (!db_table_exists(db, "reauth") &&
	    db_table_create_reauth(db) < 0) {
		sqlite3_close(db);
		return NULL;
	}

	return db;
}


static int valid_db_string(const char *str)
{
	const char *pos = str;
	while (*pos) {
		if ((*pos < '0' || *pos > '9') &&
		    (*pos < 'a' || *pos > 'f'))
			return 0;
		pos++;
	}
	return 1;
}


static int db_add_pseudonym(struct eap_sim_db_data *data,
			    const char *permanent, char *pseudonym)
{
	char cmd[128];
	char *err = NULL;

	if (!valid_db_string(permanent) || !valid_db_string(pseudonym)) {
		os_free(pseudonym);
		return -1;
	}

	os_snprintf(cmd, sizeof(cmd), "INSERT OR REPLACE INTO pseudonyms "
		    "(permanent, pseudonym) VALUES ('%s', '%s');",
		    permanent, pseudonym);
	os_free(pseudonym);
	if (sqlite3_exec(data->sqlite_db, cmd, NULL, NULL, &err) != SQLITE_OK)
	{
		wpa_printf(MSG_ERROR, "EAP-SIM DB: SQLite error: %s", err);
		sqlite3_free(err);
		return -1;
	}

	return 0;
}


static int get_pseudonym_cb(void *ctx, int argc, char *argv[], char *col[])
{
	struct eap_sim_db_data *data = ctx;
	int i;

	for (i = 0; i < argc; i++) {
		if (os_strcmp(col[i], "permanent") == 0 && argv[i]) {
			os_strlcpy(data->db_tmp_identity, argv[i],
				   sizeof(data->db_tmp_identity));
		}
	}

	return 0;
}


static char *
db_get_pseudonym(struct eap_sim_db_data *data, const char *pseudonym)
{
	char cmd[128];

	if (!valid_db_string(pseudonym))
		return NULL;
	os_memset(&data->db_tmp_identity, 0, sizeof(data->db_tmp_identity));
	os_snprintf(cmd, sizeof(cmd),
		    "SELECT permanent FROM pseudonyms WHERE pseudonym='%s';",
		    pseudonym);
	if (sqlite3_exec(data->sqlite_db, cmd, get_pseudonym_cb, data, NULL) !=
	    SQLITE_OK)
		return NULL;
	if (data->db_tmp_identity[0] == '\0')
		return NULL;
	return data->db_tmp_identity;
}


static int db_add_reauth(struct eap_sim_db_data *data, const char *permanent,
			 char *reauth_id, u16 counter, const u8 *mk,
			 const u8 *k_encr, const u8 *k_aut, const u8 *k_re)
{
	char cmd[2000], *pos, *end;
	char *err = NULL;

	if (!valid_db_string(permanent) || !valid_db_string(reauth_id)) {
		os_free(reauth_id);
		return -1;
	}

	pos = cmd;
	end = pos + sizeof(cmd);
	pos += os_snprintf(pos, end - pos, "INSERT OR REPLACE INTO reauth "
			   "(permanent, reauth_id, counter%s%s%s%s) "
			   "VALUES ('%s', '%s', %u",
			   mk ? ", mk" : "",
			   k_encr ? ", k_encr" : "",
			   k_aut ? ", k_aut" : "",
			   k_re ? ", k_re" : "",
			   permanent, reauth_id, counter);
	os_free(reauth_id);

	if (mk) {
		pos += os_snprintf(pos, end - pos, ", '");
		pos += wpa_snprintf_hex(pos, end - pos, mk, EAP_SIM_MK_LEN);
		pos += os_snprintf(pos, end - pos, "'");
	}

	if (k_encr) {
		pos += os_snprintf(pos, end - pos, ", '");
		pos += wpa_snprintf_hex(pos, end - pos, k_encr,
					EAP_SIM_K_ENCR_LEN);
		pos += os_snprintf(pos, end - pos, "'");
	}

	if (k_aut) {
		pos += os_snprintf(pos, end - pos, ", '");
		pos += wpa_snprintf_hex(pos, end - pos, k_aut,
					EAP_AKA_PRIME_K_AUT_LEN);
		pos += os_snprintf(pos, end - pos, "'");
	}

	if (k_re) {
		pos += os_snprintf(pos, end - pos, ", '");
		pos += wpa_snprintf_hex(pos, end - pos, k_re,
					EAP_AKA_PRIME_K_RE_LEN);
		pos += os_snprintf(pos, end - pos, "'");
	}

	os_snprintf(pos, end - pos, ");");

	if (sqlite3_exec(data->sqlite_db, cmd, NULL, NULL, &err) != SQLITE_OK)
	{
		wpa_printf(MSG_ERROR, "EAP-SIM DB: SQLite error: %s", err);
		sqlite3_free(err);
		return -1;
	}

	return 0;
}


static int get_reauth_cb(void *ctx, int argc, char *argv[], char *col[])
{
	struct eap_sim_db_data *data = ctx;
	int i;
	struct eap_sim_reauth *reauth = &data->db_tmp_reauth;

	for (i = 0; i < argc; i++) {
		if (os_strcmp(col[i], "permanent") == 0 && argv[i]) {
			os_strlcpy(data->db_tmp_identity, argv[i],
				   sizeof(data->db_tmp_identity));
			reauth->permanent = data->db_tmp_identity;
		} else if (os_strcmp(col[i], "counter") == 0 && argv[i]) {
			reauth->counter = atoi(argv[i]);
		} else if (os_strcmp(col[i], "mk") == 0 && argv[i]) {
			hexstr2bin(argv[i], reauth->mk, sizeof(reauth->mk));
		} else if (os_strcmp(col[i], "k_encr") == 0 && argv[i]) {
			hexstr2bin(argv[i], reauth->k_encr,
				   sizeof(reauth->k_encr));
		} else if (os_strcmp(col[i], "k_aut") == 0 && argv[i]) {
			hexstr2bin(argv[i], reauth->k_aut,
				   sizeof(reauth->k_aut));
		} else if (os_strcmp(col[i], "k_re") == 0 && argv[i]) {
			hexstr2bin(argv[i], reauth->k_re,
				   sizeof(reauth->k_re));
		}
	}

	return 0;
}


static struct eap_sim_reauth *
db_get_reauth(struct eap_sim_db_data *data, const char *reauth_id)
{
	char cmd[256];

	if (!valid_db_string(reauth_id))
		return NULL;
	os_memset(&data->db_tmp_reauth, 0, sizeof(data->db_tmp_reauth));
	os_strlcpy(data->db_tmp_pseudonym_str, reauth_id,
		   sizeof(data->db_tmp_pseudonym_str));
	data->db_tmp_reauth.reauth_id = data->db_tmp_pseudonym_str;
	os_snprintf(cmd, sizeof(cmd),
		    "SELECT * FROM reauth WHERE reauth_id='%s';", reauth_id);
	if (sqlite3_exec(data->sqlite_db, cmd, get_reauth_cb, data, NULL) !=
	    SQLITE_OK)
		return NULL;
	if (data->db_tmp_reauth.permanent == NULL)
		return NULL;
	return &data->db_tmp_reauth;
}


static void db_remove_reauth(struct eap_sim_db_data *data,
			     struct eap_sim_reauth *reauth)
{
	char cmd[256];

	if (!valid_db_string(reauth->permanent))
		return;
	os_snprintf(cmd, sizeof(cmd),
		    "DELETE FROM reauth WHERE permanent='%s';",
		    reauth->permanent);
	sqlite3_exec(data->sqlite_db, cmd, NULL, NULL, NULL);
}

#endif /* CONFIG_SQLITE */


static struct eap_sim_db_pending *
eap_sim_db_get_pending(struct eap_sim_db_data *data, const char *imsi, int aka)
{
	struct eap_sim_db_pending *entry, *prev = NULL;

	entry = data->pending;
	while (entry) {
		if (entry->aka == aka && os_strcmp(entry->imsi, imsi) == 0) {
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


static void eap_sim_db_free_pending(struct eap_sim_db_data *data,
				    struct eap_sim_db_pending *entry)
{
	eloop_cancel_timeout(eap_sim_db_query_timeout, data, entry);
	eloop_cancel_timeout(eap_sim_db_del_timeout, data, entry);
	os_free(entry);
}


static void eap_sim_db_del_pending(struct eap_sim_db_data *data,
				   struct eap_sim_db_pending *entry)
{
	struct eap_sim_db_pending **pp = &data->pending;

	while (*pp != NULL) {
		if (*pp == entry) {
			*pp = entry->next;
			eap_sim_db_free_pending(data, entry);
			return;
		}
		pp = &(*pp)->next;
	}
}


static void eap_sim_db_del_timeout(void *eloop_ctx, void *user_ctx)
{
	struct eap_sim_db_data *data = eloop_ctx;
	struct eap_sim_db_pending *entry = user_ctx;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Delete query timeout for %p", entry);
	eap_sim_db_del_pending(data, entry);
}


static void eap_sim_db_query_timeout(void *eloop_ctx, void *user_ctx)
{
	struct eap_sim_db_data *data = eloop_ctx;
	struct eap_sim_db_pending *entry = user_ctx;

	/*
	 * Report failure and allow some time for EAP server to process it
	 * before deleting the query.
	 */
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Query timeout for %p", entry);
	entry->state = FAILURE;
	data->get_complete_cb(data->ctx, entry->cb_session_ctx);
	eloop_register_timeout(1, 0, eap_sim_db_del_timeout, data, entry);
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

	entry = eap_sim_db_get_pending(data, imsi, 0);
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
	eap_sim_db_free_pending(data, entry);
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

	entry = eap_sim_db_get_pending(data, imsi, 1);
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
	eap_sim_db_free_pending(data, entry);
}


static void eap_sim_db_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct eap_sim_db_data *data = eloop_ctx;
	char buf[1000], *pos, *cmd, *imsi;
	int res;

	res = recv(sock, buf, sizeof(buf) - 1, 0);
	if (res < 0)
		return;
	buf[res] = '\0';
	wpa_hexdump_ascii_key(MSG_MSGDUMP, "EAP-SIM DB: Received from an "
			      "external source", (u8 *) buf, res);
	if (res == 0)
		return;

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
		wpa_printf(MSG_INFO, "socket(eap_sim_db): %s", strerror(errno));
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_snprintf(addr.sun_path, sizeof(addr.sun_path),
		    "/tmp/eap_sim_db_%d-%d", getpid(), counter++);
	os_free(data->local_sock);
	data->local_sock = os_strdup(addr.sun_path);
	if (data->local_sock == NULL) {
		close(data->sock);
		data->sock = -1;
		return -1;
	}
	if (bind(data->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_INFO, "bind(eap_sim_db): %s", strerror(errno));
		close(data->sock);
		data->sock = -1;
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, data->fname + 5, sizeof(addr.sun_path));
	if (connect(data->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		wpa_printf(MSG_INFO, "connect(eap_sim_db): %s",
			   strerror(errno));
		wpa_hexdump_ascii(MSG_INFO, "HLR/AuC GW socket",
				  (u8 *) addr.sun_path,
				  os_strlen(addr.sun_path));
		close(data->sock);
		data->sock = -1;
		unlink(data->local_sock);
		os_free(data->local_sock);
		data->local_sock = NULL;
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
 * @db_timeout: Database lookup timeout
 * @get_complete_cb: Callback function for reporting availability of triplets
 * @ctx: Context pointer for get_complete_cb
 * Returns: Pointer to a private data structure or %NULL on failure
 */
struct eap_sim_db_data *
eap_sim_db_init(const char *config, unsigned int db_timeout,
		void (*get_complete_cb)(void *ctx, void *session_ctx),
		void *ctx)
{
	struct eap_sim_db_data *data;
	char *pos;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	data->sock = -1;
	data->get_complete_cb = get_complete_cb;
	data->ctx = ctx;
	data->eap_sim_db_timeout = db_timeout;
	data->fname = os_strdup(config);
	if (data->fname == NULL)
		goto fail;
	pos = os_strstr(data->fname, " db=");
	if (pos) {
		*pos = '\0';
#ifdef CONFIG_SQLITE
		pos += 4;
		data->sqlite_db = db_open(pos);
		if (data->sqlite_db == NULL)
			goto fail;
#endif /* CONFIG_SQLITE */
	}

	if (os_strncmp(data->fname, "unix:", 5) == 0) {
		if (eap_sim_db_open_socket(data)) {
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: External database "
				   "connection not available - will retry "
				   "later");
		}
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
	os_free(p->permanent);
	os_free(p->pseudonym);
	os_free(p);
}


static void eap_sim_db_free_reauth(struct eap_sim_reauth *r)
{
	os_free(r->permanent);
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

#ifdef CONFIG_SQLITE
	if (data->sqlite_db) {
		sqlite3_close(data->sqlite_db);
		data->sqlite_db = NULL;
	}
#endif /* CONFIG_SQLITE */

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
		eap_sim_db_free_pending(data, prev_pending);
	}

	os_free(data);
}


static int eap_sim_db_send(struct eap_sim_db_data *data, const char *msg,
			   size_t len)
{
	int _errno = 0;

	if (send(data->sock, msg, len, 0) < 0) {
		_errno = errno;
		wpa_printf(MSG_INFO, "send[EAP-SIM DB UNIX]: %s",
			   strerror(errno));
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
			wpa_printf(MSG_INFO, "send[EAP-SIM DB UNIX]: %s",
				   strerror(errno));
			return -1;
		}
	}

	return 0;
}


static void eap_sim_db_expire_pending(struct eap_sim_db_data *data,
				      struct eap_sim_db_pending *entry)
{
	eloop_register_timeout(data->eap_sim_db_timeout, 0,
			       eap_sim_db_query_timeout, data, entry);
}


/**
 * eap_sim_db_get_gsm_triplets - Get GSM triplets
 * @data: Private data pointer from eap_sim_db_init()
 * @username: Permanent username (prefix | IMSI)
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
 * When using an external server for GSM triplets, this function can always
 * start a request and return EAP_SIM_DB_PENDING immediately if authentication
 * triplets are not available. Once the triplets are received, callback
 * function registered with eap_sim_db_init() is called to notify EAP state
 * machine to reprocess the message. This eap_sim_db_get_gsm_triplets()
 * function will then be called again and the newly received triplets will then
 * be given to the caller.
 */
int eap_sim_db_get_gsm_triplets(struct eap_sim_db_data *data,
				const char *username, int max_chal,
				u8 *_rand, u8 *kc, u8 *sres,
				void *cb_session_ctx)
{
	struct eap_sim_db_pending *entry;
	int len, ret;
	char msg[40];
	const char *imsi;
	size_t imsi_len;

	if (username == NULL || username[0] != EAP_SIM_PERMANENT_PREFIX ||
	    username[1] == '\0' || os_strlen(username) > sizeof(entry->imsi)) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: unexpected username '%s'",
			   username);
		return EAP_SIM_DB_FAILURE;
	}
	imsi = username + 1;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Get GSM triplets for IMSI '%s'",
		   imsi);

	entry = eap_sim_db_get_pending(data, imsi, 0);
	if (entry) {
		int num_chal;
		if (entry->state == FAILURE) {
			wpa_printf(MSG_DEBUG, "EAP-SIM DB: Pending entry -> "
				   "failure");
			eap_sim_db_free_pending(data, entry);
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
		eap_sim_db_free_pending(data, entry);
		return num_chal;
	}

	if (data->sock < 0) {
		if (eap_sim_db_open_socket(data) < 0)
			return EAP_SIM_DB_FAILURE;
	}

	imsi_len = os_strlen(imsi);
	len = os_snprintf(msg, sizeof(msg), "SIM-REQ-AUTH ");
	if (os_snprintf_error(sizeof(msg), len) ||
	    len + imsi_len >= sizeof(msg))
		return EAP_SIM_DB_FAILURE;
	os_memcpy(msg + len, imsi, imsi_len);
	len += imsi_len;
	ret = os_snprintf(msg + len, sizeof(msg) - len, " %d", max_chal);
	if (os_snprintf_error(sizeof(msg) - len, ret))
		return EAP_SIM_DB_FAILURE;
	len += ret;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: requesting SIM authentication "
		   "data for IMSI '%s'", imsi);
	if (eap_sim_db_send(data, msg, len) < 0)
		return EAP_SIM_DB_FAILURE;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return EAP_SIM_DB_FAILURE;

	os_strlcpy(entry->imsi, imsi, sizeof(entry->imsi));
	entry->cb_session_ctx = cb_session_ctx;
	entry->state = PENDING;
	eap_sim_db_add_pending(data, entry);
	eap_sim_db_expire_pending(data, entry);
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Added query %p", entry);

	return EAP_SIM_DB_PENDING;
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
	wpa_snprintf_hex(pos, end - pos, buf, sizeof(buf));
	
	return id;
}


/**
 * eap_sim_db_get_next_pseudonym - EAP-SIM DB: Get next pseudonym
 * @data: Private data pointer from eap_sim_db_init()
 * @method: EAP method (SIM/AKA/AKA')
 * Returns: Next pseudonym (allocated string) or %NULL on failure
 *
 * This function is used to generate a pseudonym for EAP-SIM. The returned
 * pseudonym is not added to database at this point; it will need to be added
 * with eap_sim_db_add_pseudonym() once the authentication has been completed
 * successfully. Caller is responsible for freeing the returned buffer.
 */
char * eap_sim_db_get_next_pseudonym(struct eap_sim_db_data *data,
				     enum eap_sim_db_method method)
{
	char prefix = EAP_SIM_REAUTH_ID_PREFIX;

	switch (method) {
	case EAP_SIM_DB_SIM:
		prefix = EAP_SIM_PSEUDONYM_PREFIX;
		break;
	case EAP_SIM_DB_AKA:
		prefix = EAP_AKA_PSEUDONYM_PREFIX;
		break;
	case EAP_SIM_DB_AKA_PRIME:
		prefix = EAP_AKA_PRIME_PSEUDONYM_PREFIX;
		break;
	}

	return eap_sim_db_get_next(data, prefix);
}


/**
 * eap_sim_db_get_next_reauth_id - EAP-SIM DB: Get next reauth_id
 * @data: Private data pointer from eap_sim_db_init()
 * @method: EAP method (SIM/AKA/AKA')
 * Returns: Next reauth_id (allocated string) or %NULL on failure
 *
 * This function is used to generate a fast re-authentication identity for
 * EAP-SIM. The returned reauth_id is not added to database at this point; it
 * will need to be added with eap_sim_db_add_reauth() once the authentication
 * has been completed successfully. Caller is responsible for freeing the
 * returned buffer.
 */
char * eap_sim_db_get_next_reauth_id(struct eap_sim_db_data *data,
				     enum eap_sim_db_method method)
{
	char prefix = EAP_SIM_REAUTH_ID_PREFIX;

	switch (method) {
	case EAP_SIM_DB_SIM:
		prefix = EAP_SIM_REAUTH_ID_PREFIX;
		break;
	case EAP_SIM_DB_AKA:
		prefix = EAP_AKA_REAUTH_ID_PREFIX;
		break;
	case EAP_SIM_DB_AKA_PRIME:
		prefix = EAP_AKA_PRIME_REAUTH_ID_PREFIX;
		break;
	}

	return eap_sim_db_get_next(data, prefix);
}


/**
 * eap_sim_db_add_pseudonym - EAP-SIM DB: Add new pseudonym
 * @data: Private data pointer from eap_sim_db_init()
 * @permanent: Permanent username
 * @pseudonym: Pseudonym for this user. This needs to be an allocated buffer,
 * e.g., return value from eap_sim_db_get_next_pseudonym(). Caller must not
 * free it.
 * Returns: 0 on success, -1 on failure
 *
 * This function adds a new pseudonym for EAP-SIM user. EAP-SIM DB is
 * responsible of freeing pseudonym buffer once it is not needed anymore.
 */
int eap_sim_db_add_pseudonym(struct eap_sim_db_data *data,
			     const char *permanent, char *pseudonym)
{
	struct eap_sim_pseudonym *p;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Add pseudonym '%s' for permanent "
		   "username '%s'", pseudonym, permanent);

	/* TODO: could store last two pseudonyms */
#ifdef CONFIG_SQLITE
	if (data->sqlite_db)
		return db_add_pseudonym(data, permanent, pseudonym);
#endif /* CONFIG_SQLITE */
	for (p = data->pseudonyms; p; p = p->next) {
		if (os_strcmp(permanent, p->permanent) == 0)
			break;
	}
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
	p->permanent = os_strdup(permanent);
	if (p->permanent == NULL) {
		os_free(p);
		os_free(pseudonym);
		return -1;
	}
	p->pseudonym = pseudonym;
	data->pseudonyms = p;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Added new pseudonym entry");
	return 0;
}


static struct eap_sim_reauth *
eap_sim_db_add_reauth_data(struct eap_sim_db_data *data,
			   const char *permanent,
			   char *reauth_id, u16 counter)
{
	struct eap_sim_reauth *r;

	for (r = data->reauths; r; r = r->next) {
		if (os_strcmp(r->permanent, permanent) == 0)
			break;
	}

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
		r->permanent = os_strdup(permanent);
		if (r->permanent == NULL) {
			os_free(r);
			os_free(reauth_id);
			return NULL;
		}
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
 * @permanent: Permanent username
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
int eap_sim_db_add_reauth(struct eap_sim_db_data *data, const char *permanent,
			  char *reauth_id, u16 counter, const u8 *mk)
{
	struct eap_sim_reauth *r;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Add reauth_id '%s' for permanent "
		   "identity '%s'", reauth_id, permanent);

#ifdef CONFIG_SQLITE
	if (data->sqlite_db)
		return db_add_reauth(data, permanent, reauth_id, counter, mk,
				     NULL, NULL, NULL);
#endif /* CONFIG_SQLITE */
	r = eap_sim_db_add_reauth_data(data, permanent, reauth_id, counter);
	if (r == NULL)
		return -1;

	os_memcpy(r->mk, mk, EAP_SIM_MK_LEN);

	return 0;
}


#ifdef EAP_SERVER_AKA_PRIME
/**
 * eap_sim_db_add_reauth_prime - EAP-AKA' DB: Add new re-authentication entry
 * @data: Private data pointer from eap_sim_db_init()
 * @permanent: Permanent username
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
int eap_sim_db_add_reauth_prime(struct eap_sim_db_data *data,
				const char *permanent, char *reauth_id,
				u16 counter, const u8 *k_encr,
				const u8 *k_aut, const u8 *k_re)
{
	struct eap_sim_reauth *r;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Add reauth_id '%s' for permanent "
		   "identity '%s'", reauth_id, permanent);

#ifdef CONFIG_SQLITE
	if (data->sqlite_db)
		return db_add_reauth(data, permanent, reauth_id, counter, NULL,
				     k_encr, k_aut, k_re);
#endif /* CONFIG_SQLITE */
	r = eap_sim_db_add_reauth_data(data, permanent, reauth_id, counter);
	if (r == NULL)
		return -1;

	os_memcpy(r->k_encr, k_encr, EAP_SIM_K_ENCR_LEN);
	os_memcpy(r->k_aut, k_aut, EAP_AKA_PRIME_K_AUT_LEN);
	os_memcpy(r->k_re, k_re, EAP_AKA_PRIME_K_RE_LEN);

	return 0;
}
#endif /* EAP_SERVER_AKA_PRIME */


/**
 * eap_sim_db_get_permanent - EAP-SIM DB: Get permanent identity
 * @data: Private data pointer from eap_sim_db_init()
 * @pseudonym: Pseudonym username
 * Returns: Pointer to permanent username or %NULL if not found
 */
const char *
eap_sim_db_get_permanent(struct eap_sim_db_data *data, const char *pseudonym)
{
	struct eap_sim_pseudonym *p;

#ifdef CONFIG_SQLITE
	if (data->sqlite_db)
		return db_get_pseudonym(data, pseudonym);
#endif /* CONFIG_SQLITE */

	p = data->pseudonyms;
	while (p) {
		if (os_strcmp(p->pseudonym, pseudonym) == 0)
			return p->permanent;
		p = p->next;
	}

	return NULL;
}


/**
 * eap_sim_db_get_reauth_entry - EAP-SIM DB: Get re-authentication entry
 * @data: Private data pointer from eap_sim_db_init()
 * @reauth_id: Fast re-authentication username
 * Returns: Pointer to the re-auth entry, or %NULL if not found
 */
struct eap_sim_reauth *
eap_sim_db_get_reauth_entry(struct eap_sim_db_data *data,
			    const char *reauth_id)
{
	struct eap_sim_reauth *r;

#ifdef CONFIG_SQLITE
	if (data->sqlite_db)
		return db_get_reauth(data, reauth_id);
#endif /* CONFIG_SQLITE */

	r = data->reauths;
	while (r) {
		if (os_strcmp(r->reauth_id, reauth_id) == 0)
			break;
		r = r->next;
	}

	return r;
}


/**
 * eap_sim_db_remove_reauth - EAP-SIM DB: Remove re-authentication entry
 * @data: Private data pointer from eap_sim_db_init()
 * @reauth: Pointer to re-authentication entry from
 * eap_sim_db_get_reauth_entry()
 */
void eap_sim_db_remove_reauth(struct eap_sim_db_data *data,
			      struct eap_sim_reauth *reauth)
{
	struct eap_sim_reauth *r, *prev = NULL;
#ifdef CONFIG_SQLITE
	if (data->sqlite_db) {
		db_remove_reauth(data, reauth);
		return;
	}
#endif /* CONFIG_SQLITE */
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
 * @data: Private data pointer from eap_sim_db_init()
 * @username: Permanent username (prefix | IMSI)
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
 * When using an external server for AKA authentication, this function can
 * always start a request and return EAP_SIM_DB_PENDING immediately if
 * authentication triplets are not available. Once the authentication data are
 * received, callback function registered with eap_sim_db_init() is called to
 * notify EAP state machine to reprocess the message. This
 * eap_sim_db_get_aka_auth() function will then be called again and the newly
 * received triplets will then be given to the caller.
 */
int eap_sim_db_get_aka_auth(struct eap_sim_db_data *data, const char *username,
			    u8 *_rand, u8 *autn, u8 *ik, u8 *ck,
			    u8 *res, size_t *res_len, void *cb_session_ctx)
{
	struct eap_sim_db_pending *entry;
	int len;
	char msg[40];
	const char *imsi;
	size_t imsi_len;

	if (username == NULL ||
	    (username[0] != EAP_AKA_PERMANENT_PREFIX &&
	     username[0] != EAP_AKA_PRIME_PERMANENT_PREFIX) ||
	    username[1] == '\0' || os_strlen(username) > sizeof(entry->imsi)) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: unexpected username '%s'",
			   username);
		return EAP_SIM_DB_FAILURE;
	}
	imsi = username + 1;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Get AKA auth for IMSI '%s'",
		   imsi);

	entry = eap_sim_db_get_pending(data, imsi, 1);
	if (entry) {
		if (entry->state == FAILURE) {
			eap_sim_db_free_pending(data, entry);
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
		eap_sim_db_free_pending(data, entry);
		return 0;
	}

	if (data->sock < 0) {
		if (eap_sim_db_open_socket(data) < 0)
			return EAP_SIM_DB_FAILURE;
	}

	imsi_len = os_strlen(imsi);
	len = os_snprintf(msg, sizeof(msg), "AKA-REQ-AUTH ");
	if (os_snprintf_error(sizeof(msg), len) ||
	    len + imsi_len >= sizeof(msg))
		return EAP_SIM_DB_FAILURE;
	os_memcpy(msg + len, imsi, imsi_len);
	len += imsi_len;

	wpa_printf(MSG_DEBUG, "EAP-SIM DB: requesting AKA authentication "
		    "data for IMSI '%s'", imsi);
	if (eap_sim_db_send(data, msg, len) < 0)
		return EAP_SIM_DB_FAILURE;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return EAP_SIM_DB_FAILURE;

	entry->aka = 1;
	os_strlcpy(entry->imsi, imsi, sizeof(entry->imsi));
	entry->cb_session_ctx = cb_session_ctx;
	entry->state = PENDING;
	eap_sim_db_add_pending(data, entry);
	eap_sim_db_expire_pending(data, entry);
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Added query %p", entry);

	return EAP_SIM_DB_PENDING;
}


/**
 * eap_sim_db_resynchronize - Resynchronize AKA AUTN
 * @data: Private data pointer from eap_sim_db_init()
 * @username: Permanent username
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
int eap_sim_db_resynchronize(struct eap_sim_db_data *data,
			     const char *username,
			     const u8 *auts, const u8 *_rand)
{
	const char *imsi;
	size_t imsi_len;

	if (username == NULL ||
	    (username[0] != EAP_AKA_PERMANENT_PREFIX &&
	     username[0] != EAP_AKA_PRIME_PERMANENT_PREFIX) ||
	    username[1] == '\0' || os_strlen(username) > 20) {
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: unexpected username '%s'",
			   username);
		return -1;
	}
	imsi = username + 1;
	wpa_printf(MSG_DEBUG, "EAP-SIM DB: Get AKA auth for IMSI '%s'",
		   imsi);

	if (data->sock >= 0) {
		char msg[100];
		int len, ret;

		imsi_len = os_strlen(imsi);
		len = os_snprintf(msg, sizeof(msg), "AKA-AUTS ");
		if (os_snprintf_error(sizeof(msg), len) ||
		    len + imsi_len >= sizeof(msg))
			return -1;
		os_memcpy(msg + len, imsi, imsi_len);
		len += imsi_len;

		ret = os_snprintf(msg + len, sizeof(msg) - len, " ");
		if (os_snprintf_error(sizeof(msg) - len, ret))
			return -1;
		len += ret;
		len += wpa_snprintf_hex(msg + len, sizeof(msg) - len,
					auts, EAP_AKA_AUTS_LEN);
		ret = os_snprintf(msg + len, sizeof(msg) - len, " ");
		if (os_snprintf_error(sizeof(msg) - len, ret))
			return -1;
		len += ret;
		len += wpa_snprintf_hex(msg + len, sizeof(msg) - len,
					_rand, EAP_AKA_RAND_LEN);
		wpa_printf(MSG_DEBUG, "EAP-SIM DB: reporting AKA AUTS for "
			   "IMSI '%s'", imsi);
		if (eap_sim_db_send(data, msg, len) < 0)
			return -1;
	}

	return 0;
}


/**
 * sim_get_username - Extract username from SIM identity
 * @identity: Identity
 * @identity_len: Identity length
 * Returns: Allocated buffer with the username part of the identity
 *
 * Caller is responsible for freeing the returned buffer with os_free().
 */
char * sim_get_username(const u8 *identity, size_t identity_len)
{
	size_t pos;

	if (identity == NULL)
		return NULL;

	for (pos = 0; pos < identity_len; pos++) {
		if (identity[pos] == '@' || identity[pos] == '\0')
			break;
	}

	return dup_binstr(identity, pos);
}
