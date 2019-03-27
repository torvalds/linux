/*
 * HLR/AuC testing gateway for hostapd EAP-SIM/AKA database/authenticator
 * Copyright (c) 2005-2007, 2012-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This is an example implementation of the EAP-SIM/AKA database/authentication
 * gateway interface to HLR/AuC. It is expected to be replaced with an
 * implementation of SS7 gateway to GSM/UMTS authentication center (HLR/AuC) or
 * a local implementation of SIM triplet and AKA authentication data generator.
 *
 * hostapd will send SIM/AKA authentication queries over a UNIX domain socket
 * to and external program, e.g., this hlr_auc_gw. This interface uses simple
 * text-based format:
 *
 * EAP-SIM / GSM triplet query/response:
 * SIM-REQ-AUTH <IMSI> <max_chal>
 * SIM-RESP-AUTH <IMSI> Kc1:SRES1:RAND1 Kc2:SRES2:RAND2 [Kc3:SRES3:RAND3]
 * SIM-RESP-AUTH <IMSI> FAILURE
 * GSM-AUTH-REQ <IMSI> RAND1:RAND2[:RAND3]
 * GSM-AUTH-RESP <IMSI> Kc1:SRES1:Kc2:SRES2[:Kc3:SRES3]
 * GSM-AUTH-RESP <IMSI> FAILURE
 *
 * EAP-AKA / UMTS query/response:
 * AKA-REQ-AUTH <IMSI>
 * AKA-RESP-AUTH <IMSI> <RAND> <AUTN> <IK> <CK> <RES>
 * AKA-RESP-AUTH <IMSI> FAILURE
 *
 * EAP-AKA / UMTS AUTS (re-synchronization):
 * AKA-AUTS <IMSI> <AUTS> <RAND>
 *
 * IMSI and max_chal are sent as an ASCII string,
 * Kc/SRES/RAND/AUTN/IK/CK/RES/AUTS as hex strings.
 *
 * An example implementation here reads GSM authentication triplets from a
 * text file in IMSI:Kc:SRES:RAND format, IMSI in ASCII, other fields as hex
 * strings. This is used to simulate an HLR/AuC. As such, it is not very useful
 * for real life authentication, but it is useful both as an example
 * implementation and for EAP-SIM/AKA/AKA' testing.
 *
 * For a stronger example design, Milenage and GSM-Milenage algorithms can be
 * used to dynamically generate authenticatipn information for EAP-AKA/AKA' and
 * EAP-SIM, respectively, if Ki is known.
 *
 * SQN generation follows the not time-based Profile 2 described in
 * 3GPP TS 33.102 Annex C.3.2. The length of IND is 5 bits by default, but this
 * can be changed with a command line options if needed.
 */

#include "includes.h"
#include <sys/un.h>
#ifdef CONFIG_SQLITE
#include <sqlite3.h>
#endif /* CONFIG_SQLITE */

#include "common.h"
#include "crypto/milenage.h"
#include "crypto/random.h"

static const char *default_socket_path = "/tmp/hlr_auc_gw.sock";
static const char *socket_path;
static int serv_sock = -1;
static char *milenage_file = NULL;
static int update_milenage = 0;
static int sqn_changes = 0;
static int ind_len = 5;
static int stdout_debug = 1;

/* GSM triplets */
struct gsm_triplet {
	struct gsm_triplet *next;
	char imsi[20];
	u8 kc[8];
	u8 sres[4];
	u8 _rand[16];
};

static struct gsm_triplet *gsm_db = NULL, *gsm_db_pos = NULL;

/* OPc and AMF parameters for Milenage (Example algorithms for AKA). */
struct milenage_parameters {
	struct milenage_parameters *next;
	char imsi[20];
	u8 ki[16];
	u8 opc[16];
	u8 amf[2];
	u8 sqn[6];
	int set;
	size_t res_len;
};

static struct milenage_parameters *milenage_db = NULL;

#define EAP_SIM_MAX_CHAL 3

#define EAP_AKA_RAND_LEN 16
#define EAP_AKA_AUTN_LEN 16
#define EAP_AKA_AUTS_LEN 14
#define EAP_AKA_RES_MIN_LEN 4
#define EAP_AKA_RES_MAX_LEN 16
#define EAP_AKA_IK_LEN 16
#define EAP_AKA_CK_LEN 16


#ifdef CONFIG_SQLITE

static sqlite3 *sqlite_db = NULL;
static struct milenage_parameters db_tmp_milenage;


static int db_table_exists(sqlite3 *db, const char *name)
{
	char cmd[128];
	os_snprintf(cmd, sizeof(cmd), "SELECT 1 FROM %s;", name);
	return sqlite3_exec(db, cmd, NULL, NULL, NULL) == SQLITE_OK;
}


static int db_table_create_milenage(sqlite3 *db)
{
	char *err = NULL;
	const char *sql =
		"CREATE TABLE milenage("
		"  imsi INTEGER PRIMARY KEY NOT NULL,"
		"  ki CHAR(32) NOT NULL,"
		"  opc CHAR(32) NOT NULL,"
		"  amf CHAR(4) NOT NULL,"
		"  sqn CHAR(12) NOT NULL,"
		"  res_len INTEGER"
		");";

	printf("Adding database table for milenage information\n");
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		printf("SQLite error: %s\n", err);
		sqlite3_free(err);
		return -1;
	}

	return 0;
}


static sqlite3 * db_open(const char *db_file)
{
	sqlite3 *db;

	if (sqlite3_open(db_file, &db)) {
		printf("Failed to open database %s: %s\n",
		       db_file, sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	if (!db_table_exists(db, "milenage") &&
	    db_table_create_milenage(db) < 0) {
		sqlite3_close(db);
		return NULL;
	}

	return db;
}


static int get_milenage_cb(void *ctx, int argc, char *argv[], char *col[])
{
	struct milenage_parameters *m = ctx;
	int i;

	m->set = 1;

	for (i = 0; i < argc; i++) {
		if (os_strcmp(col[i], "ki") == 0 && argv[i] &&
		    hexstr2bin(argv[i], m->ki, sizeof(m->ki))) {
			printf("Invalid ki value in database\n");
			return -1;
		}

		if (os_strcmp(col[i], "opc") == 0 && argv[i] &&
		    hexstr2bin(argv[i], m->opc, sizeof(m->opc))) {
			printf("Invalid opcvalue in database\n");
			return -1;
		}

		if (os_strcmp(col[i], "amf") == 0 && argv[i] &&
		    hexstr2bin(argv[i], m->amf, sizeof(m->amf))) {
			printf("Invalid amf value in database\n");
			return -1;
		}

		if (os_strcmp(col[i], "sqn") == 0 && argv[i] &&
		    hexstr2bin(argv[i], m->sqn, sizeof(m->sqn))) {
			printf("Invalid sqn value in database\n");
			return -1;
		}

		if (os_strcmp(col[i], "res_len") == 0 && argv[i]) {
			m->res_len = atoi(argv[i]);
		}
	}

	return 0;
}


static struct milenage_parameters * db_get_milenage(const char *imsi_txt)
{
	char cmd[128];
	unsigned long long imsi;

	os_memset(&db_tmp_milenage, 0, sizeof(db_tmp_milenage));
	imsi = atoll(imsi_txt);
	os_snprintf(db_tmp_milenage.imsi, sizeof(db_tmp_milenage.imsi),
		    "%llu", imsi);
	os_snprintf(cmd, sizeof(cmd),
		    "SELECT * FROM milenage WHERE imsi=%llu;", imsi);
	if (sqlite3_exec(sqlite_db, cmd, get_milenage_cb, &db_tmp_milenage,
			 NULL) != SQLITE_OK)
		return NULL;

	if (!db_tmp_milenage.set)
		return NULL;
	return &db_tmp_milenage;
}


static int db_update_milenage_sqn(struct milenage_parameters *m)
{
	char cmd[128], val[13], *pos;

	if (sqlite_db == NULL)
		return 0;

	pos = val;
	pos += wpa_snprintf_hex(pos, sizeof(val), m->sqn, 6);
	*pos = '\0';
	os_snprintf(cmd, sizeof(cmd),
		    "UPDATE milenage SET sqn='%s' WHERE imsi=%s;",
		    val, m->imsi);
	if (sqlite3_exec(sqlite_db, cmd, NULL, NULL, NULL) != SQLITE_OK) {
		printf("Failed to update SQN in database for IMSI %s\n",
		       m->imsi);
		return -1;
	}
	return 0;
}

#endif /* CONFIG_SQLITE */


static int open_socket(const char *path)
{
	struct sockaddr_un addr;
	int s;

	s = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket(PF_UNIX)");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("hlr-auc-gw: bind(PF_UNIX)");
		close(s);
		return -1;
	}

	return s;
}


static int read_gsm_triplets(const char *fname)
{
	FILE *f;
	char buf[200], *pos, *pos2;
	struct gsm_triplet *g = NULL;
	int line, ret = 0;

	if (fname == NULL)
		return -1;

	f = fopen(fname, "r");
	if (f == NULL) {
		printf("Could not open GSM triplet data file '%s'\n", fname);
		return -1;
	}

	line = 0;
	while (fgets(buf, sizeof(buf), f)) {
		line++;

		/* Parse IMSI:Kc:SRES:RAND */
		buf[sizeof(buf) - 1] = '\0';
		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0' && *pos != '\n')
			pos++;
		if (*pos == '\n')
			*pos = '\0';
		pos = buf;
		if (*pos == '\0')
			continue;

		g = os_zalloc(sizeof(*g));
		if (g == NULL) {
			ret = -1;
			break;
		}

		/* IMSI */
		pos2 = NULL;
		pos = str_token(buf, ":", &pos2);
		if (!pos || os_strlen(pos) >= sizeof(g->imsi)) {
			printf("%s:%d - Invalid IMSI\n", fname, line);
			ret = -1;
			break;
		}
		os_strlcpy(g->imsi, pos, sizeof(g->imsi));

		/* Kc */
		pos = str_token(buf, ":", &pos2);
		if (!pos || os_strlen(pos) != 16 || hexstr2bin(pos, g->kc, 8)) {
			printf("%s:%d - Invalid Kc\n", fname, line);
			ret = -1;
			break;
		}

		/* SRES */
		pos = str_token(buf, ":", &pos2);
		if (!pos || os_strlen(pos) != 8 ||
		    hexstr2bin(pos, g->sres, 4)) {
			printf("%s:%d - Invalid SRES\n", fname, line);
			ret = -1;
			break;
		}

		/* RAND */
		pos = str_token(buf, ":", &pos2);
		if (!pos || os_strlen(pos) != 32 ||
		    hexstr2bin(pos, g->_rand, 16)) {
			printf("%s:%d - Invalid RAND\n", fname, line);
			ret = -1;
			break;
		}

		g->next = gsm_db;
		gsm_db = g;
		g = NULL;
	}
	os_free(g);

	fclose(f);

	return ret;
}


static struct gsm_triplet * get_gsm_triplet(const char *imsi)
{
	struct gsm_triplet *g = gsm_db_pos;

	while (g) {
		if (strcmp(g->imsi, imsi) == 0) {
			gsm_db_pos = g->next;
			return g;
		}
		g = g->next;
	}

	g = gsm_db;
	while (g && g != gsm_db_pos) {
		if (strcmp(g->imsi, imsi) == 0) {
			gsm_db_pos = g->next;
			return g;
		}
		g = g->next;
	}

	return NULL;
}


static int read_milenage(const char *fname)
{
	FILE *f;
	char buf[200], *pos, *pos2;
	struct milenage_parameters *m = NULL;
	int line, ret = 0;

	if (fname == NULL)
		return -1;

	f = fopen(fname, "r");
	if (f == NULL) {
		printf("Could not open Milenage data file '%s'\n", fname);
		return -1;
	}

	line = 0;
	while (fgets(buf, sizeof(buf), f)) {
		line++;

		/* Parse IMSI Ki OPc AMF SQN [RES_len] */
		buf[sizeof(buf) - 1] = '\0';
		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0' && *pos != '\n')
			pos++;
		if (*pos == '\n')
			*pos = '\0';
		pos = buf;
		if (*pos == '\0')
			continue;

		m = os_zalloc(sizeof(*m));
		if (m == NULL) {
			ret = -1;
			break;
		}

		/* IMSI */
		pos2 = NULL;
		pos = str_token(buf, " ", &pos2);
		if (!pos || os_strlen(pos) >= sizeof(m->imsi)) {
			printf("%s:%d - Invalid IMSI\n", fname, line);
			ret = -1;
			break;
		}
		os_strlcpy(m->imsi, pos, sizeof(m->imsi));

		/* Ki */
		pos = str_token(buf, " ", &pos2);
		if (!pos || os_strlen(pos) != 32 ||
		    hexstr2bin(pos, m->ki, 16)) {
			printf("%s:%d - Invalid Ki\n", fname, line);
			ret = -1;
			break;
		}

		/* OPc */
		pos = str_token(buf, " ", &pos2);
		if (!pos || os_strlen(pos) != 32 ||
		    hexstr2bin(pos, m->opc, 16)) {
			printf("%s:%d - Invalid OPc\n", fname, line);
			ret = -1;
			break;
		}

		/* AMF */
		pos = str_token(buf, " ", &pos2);
		if (!pos || os_strlen(pos) != 4 || hexstr2bin(pos, m->amf, 2)) {
			printf("%s:%d - Invalid AMF\n", fname, line);
			ret = -1;
			break;
		}

		/* SQN */
		pos = str_token(buf, " ", &pos2);
		if (!pos || os_strlen(pos) != 12 ||
		    hexstr2bin(pos, m->sqn, 6)) {
			printf("%s:%d - Invalid SEQ\n", fname, line);
			ret = -1;
			break;
		}

		pos = str_token(buf, " ", &pos2);
		if (pos) {
			m->res_len = atoi(pos);
			if (m->res_len &&
			    (m->res_len < EAP_AKA_RES_MIN_LEN ||
			     m->res_len > EAP_AKA_RES_MAX_LEN)) {
				printf("%s:%d - Invalid RES_len\n",
				       fname, line);
				ret = -1;
				break;
			}
		}

		m->next = milenage_db;
		milenage_db = m;
		m = NULL;
	}
	os_free(m);

	fclose(f);

	return ret;
}


static void update_milenage_file(const char *fname)
{
	FILE *f, *f2;
	char name[500], buf[500], *pos;
	char *end = buf + sizeof(buf);
	struct milenage_parameters *m;
	size_t imsi_len;

	f = fopen(fname, "r");
	if (f == NULL) {
		printf("Could not open Milenage data file '%s'\n", fname);
		return;
	}

	snprintf(name, sizeof(name), "%s.new", fname);
	f2 = fopen(name, "w");
	if (f2 == NULL) {
		printf("Could not write Milenage data file '%s'\n", name);
		fclose(f);
		return;
	}

	while (fgets(buf, sizeof(buf), f)) {
		/* IMSI Ki OPc AMF SQN */
		buf[sizeof(buf) - 1] = '\0';

		pos = strchr(buf, ' ');
		if (buf[0] == '#' || pos == NULL || pos - buf >= 20)
			goto no_update;

		imsi_len = pos - buf;

		for (m = milenage_db; m; m = m->next) {
			if (strncmp(buf, m->imsi, imsi_len) == 0 &&
			    m->imsi[imsi_len] == '\0')
				break;
		}

		if (!m)
			goto no_update;

		pos = buf;
		pos += snprintf(pos, end - pos, "%s ", m->imsi);
		pos += wpa_snprintf_hex(pos, end - pos, m->ki, 16);
		*pos++ = ' ';
		pos += wpa_snprintf_hex(pos, end - pos, m->opc, 16);
		*pos++ = ' ';
		pos += wpa_snprintf_hex(pos, end - pos, m->amf, 2);
		*pos++ = ' ';
		pos += wpa_snprintf_hex(pos, end - pos, m->sqn, 6);
		*pos++ = '\n';

	no_update:
		fprintf(f2, "%s", buf);
	}

	fclose(f2);
	fclose(f);

	snprintf(name, sizeof(name), "%s.bak", fname);
	if (rename(fname, name) < 0) {
		perror("rename");
		return;
	}

	snprintf(name, sizeof(name), "%s.new", fname);
	if (rename(name, fname) < 0) {
		perror("rename");
		return;
	}

}


static struct milenage_parameters * get_milenage(const char *imsi)
{
	struct milenage_parameters *m = milenage_db;

	while (m) {
		if (strcmp(m->imsi, imsi) == 0)
			break;
		m = m->next;
	}

#ifdef CONFIG_SQLITE
	if (!m)
		m = db_get_milenage(imsi);
#endif /* CONFIG_SQLITE */

	return m;
}


static int sim_req_auth(char *imsi, char *resp, size_t resp_len)
{
	int count, max_chal, ret;
	char *pos;
	char *rpos, *rend;
	struct milenage_parameters *m;
	struct gsm_triplet *g;

	resp[0] = '\0';

	pos = strchr(imsi, ' ');
	if (pos) {
		*pos++ = '\0';
		max_chal = atoi(pos);
		if (max_chal < 1 || max_chal > EAP_SIM_MAX_CHAL)
			max_chal = EAP_SIM_MAX_CHAL;
	} else
		max_chal = EAP_SIM_MAX_CHAL;

	rend = resp + resp_len;
	rpos = resp;
	ret = snprintf(rpos, rend - rpos, "SIM-RESP-AUTH %s", imsi);
	if (ret < 0 || ret >= rend - rpos)
		return -1;
	rpos += ret;

	m = get_milenage(imsi);
	if (m) {
		u8 _rand[16], sres[4], kc[8];
		for (count = 0; count < max_chal; count++) {
			if (random_get_bytes(_rand, 16) < 0)
				return -1;
			gsm_milenage(m->opc, m->ki, _rand, sres, kc);
			*rpos++ = ' ';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, kc, 8);
			*rpos++ = ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, sres, 4);
			*rpos++ = ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, _rand, 16);
		}
		*rpos = '\0';
		return 0;
	}

	count = 0;
	while (count < max_chal && (g = get_gsm_triplet(imsi))) {
		if (strcmp(g->imsi, imsi) != 0)
			continue;

		if (rpos < rend)
			*rpos++ = ' ';
		rpos += wpa_snprintf_hex(rpos, rend - rpos, g->kc, 8);
		if (rpos < rend)
			*rpos++ = ':';
		rpos += wpa_snprintf_hex(rpos, rend - rpos, g->sres, 4);
		if (rpos < rend)
			*rpos++ = ':';
		rpos += wpa_snprintf_hex(rpos, rend - rpos, g->_rand, 16);
		count++;
	}

	if (count == 0) {
		printf("No GSM triplets found for %s\n", imsi);
		ret = snprintf(rpos, rend - rpos, " FAILURE");
		if (ret < 0 || ret >= rend - rpos)
			return -1;
		rpos += ret;
	}

	return 0;
}


static int gsm_auth_req(char *imsi, char *resp, size_t resp_len)
{
	int count, ret;
	char *pos, *rpos, *rend;
	struct milenage_parameters *m;

	resp[0] = '\0';

	pos = os_strchr(imsi, ' ');
	if (!pos)
		return -1;
	*pos++ = '\0';

	rend = resp + resp_len;
	rpos = resp;
	ret = os_snprintf(rpos, rend - rpos, "GSM-AUTH-RESP %s", imsi);
	if (os_snprintf_error(rend - rpos, ret))
		return -1;
	rpos += ret;

	m = get_milenage(imsi);
	if (m) {
		u8 _rand[16], sres[4], kc[8];
		for (count = 0; count < EAP_SIM_MAX_CHAL; count++) {
			if (hexstr2bin(pos, _rand, 16) != 0)
				return -1;
			gsm_milenage(m->opc, m->ki, _rand, sres, kc);
			*rpos++ = count == 0 ? ' ' : ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, kc, 8);
			*rpos++ = ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, sres, 4);
			pos += 16 * 2;
			if (*pos != ':')
				break;
			pos++;
		}
		*rpos = '\0';
		return 0;
	}

	printf("No GSM triplets found for %s\n", imsi);
	ret = os_snprintf(rpos, rend - rpos, " FAILURE");
	if (os_snprintf_error(rend - rpos, ret))
		return -1;
	rpos += ret;

	return 0;
}


static void inc_sqn(u8 *sqn)
{
	u64 val, seq, ind;

	/*
	 * SQN = SEQ | IND = SEQ1 | SEQ2 | IND
	 *
	 * The mechanism used here is not time-based, so SEQ2 is void and
	 * SQN = SEQ1 | IND. The length of IND is ind_len bits and the length
	 * of SEQ1 is 48 - ind_len bits.
	 */

	/* Increment both SEQ and IND by one */
	val = ((u64) WPA_GET_BE32(sqn) << 16) | ((u64) WPA_GET_BE16(sqn + 4));
	seq = (val >> ind_len) + 1;
	ind = (val + 1) & ((1 << ind_len) - 1);
	val = (seq << ind_len) | ind;
	WPA_PUT_BE32(sqn, val >> 16);
	WPA_PUT_BE16(sqn + 4, val & 0xffff);
}


static int aka_req_auth(char *imsi, char *resp, size_t resp_len)
{
	/* AKA-RESP-AUTH <IMSI> <RAND> <AUTN> <IK> <CK> <RES> */
	char *pos, *end;
	u8 _rand[EAP_AKA_RAND_LEN];
	u8 autn[EAP_AKA_AUTN_LEN];
	u8 ik[EAP_AKA_IK_LEN];
	u8 ck[EAP_AKA_CK_LEN];
	u8 res[EAP_AKA_RES_MAX_LEN];
	size_t res_len;
	int ret;
	struct milenage_parameters *m;
	int failed = 0;

	m = get_milenage(imsi);
	if (m) {
		if (random_get_bytes(_rand, EAP_AKA_RAND_LEN) < 0)
			return -1;
		res_len = EAP_AKA_RES_MAX_LEN;
		inc_sqn(m->sqn);
#ifdef CONFIG_SQLITE
		db_update_milenage_sqn(m);
#endif /* CONFIG_SQLITE */
		sqn_changes = 1;
		if (stdout_debug) {
			printf("AKA: Milenage with SQN=%02x%02x%02x%02x%02x%02x\n",
			       m->sqn[0], m->sqn[1], m->sqn[2],
			       m->sqn[3], m->sqn[4], m->sqn[5]);
		}
		milenage_generate(m->opc, m->amf, m->ki, m->sqn, _rand,
				  autn, ik, ck, res, &res_len);
		if (m->res_len >= EAP_AKA_RES_MIN_LEN &&
		    m->res_len <= EAP_AKA_RES_MAX_LEN &&
		    m->res_len < res_len)
			res_len = m->res_len;
	} else {
		printf("Unknown IMSI: %s\n", imsi);
#ifdef AKA_USE_FIXED_TEST_VALUES
		printf("Using fixed test values for AKA\n");
		memset(_rand, '0', EAP_AKA_RAND_LEN);
		memset(autn, '1', EAP_AKA_AUTN_LEN);
		memset(ik, '3', EAP_AKA_IK_LEN);
		memset(ck, '4', EAP_AKA_CK_LEN);
		memset(res, '2', EAP_AKA_RES_MAX_LEN);
		res_len = EAP_AKA_RES_MAX_LEN;
#else /* AKA_USE_FIXED_TEST_VALUES */
		failed = 1;
#endif /* AKA_USE_FIXED_TEST_VALUES */
	}

	pos = resp;
	end = resp + resp_len;
	ret = snprintf(pos, end - pos, "AKA-RESP-AUTH %s ", imsi);
	if (ret < 0 || ret >= end - pos)
		return -1;
	pos += ret;
	if (failed) {
		ret = snprintf(pos, end - pos, "FAILURE");
		if (ret < 0 || ret >= end - pos)
			return -1;
		pos += ret;
		return 0;
	}
	pos += wpa_snprintf_hex(pos, end - pos, _rand, EAP_AKA_RAND_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, autn, EAP_AKA_AUTN_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, ik, EAP_AKA_IK_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, ck, EAP_AKA_CK_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, res, res_len);

	return 0;
}


static int aka_auts(char *imsi, char *resp, size_t resp_len)
{
	char *auts, *__rand;
	u8 _auts[EAP_AKA_AUTS_LEN], _rand[EAP_AKA_RAND_LEN], sqn[6];
	struct milenage_parameters *m;

	resp[0] = '\0';

	/* AKA-AUTS <IMSI> <AUTS> <RAND> */

	auts = strchr(imsi, ' ');
	if (auts == NULL)
		return -1;
	*auts++ = '\0';

	__rand = strchr(auts, ' ');
	if (__rand == NULL)
		return -1;
	*__rand++ = '\0';

	if (stdout_debug) {
		printf("AKA-AUTS: IMSI=%s AUTS=%s RAND=%s\n",
		       imsi, auts, __rand);
	}
	if (hexstr2bin(auts, _auts, EAP_AKA_AUTS_LEN) ||
	    hexstr2bin(__rand, _rand, EAP_AKA_RAND_LEN)) {
		printf("Could not parse AUTS/RAND\n");
		return -1;
	}

	m = get_milenage(imsi);
	if (m == NULL) {
		printf("Unknown IMSI: %s\n", imsi);
		return -1;
	}

	if (milenage_auts(m->opc, m->ki, _rand, _auts, sqn)) {
		printf("AKA-AUTS: Incorrect MAC-S\n");
	} else {
		memcpy(m->sqn, sqn, 6);
		if (stdout_debug) {
			printf("AKA-AUTS: Re-synchronized: "
			       "SQN=%02x%02x%02x%02x%02x%02x\n",
			       sqn[0], sqn[1], sqn[2], sqn[3], sqn[4], sqn[5]);
		}
#ifdef CONFIG_SQLITE
		db_update_milenage_sqn(m);
#endif /* CONFIG_SQLITE */
		sqn_changes = 1;
	}

	return 0;
}


static int process_cmd(char *cmd, char *resp, size_t resp_len)
{
	if (os_strncmp(cmd, "SIM-REQ-AUTH ", 13) == 0)
		return sim_req_auth(cmd + 13, resp, resp_len);

	if (os_strncmp(cmd, "GSM-AUTH-REQ ", 13) == 0)
		return gsm_auth_req(cmd + 13, resp, resp_len);

	if (os_strncmp(cmd, "AKA-REQ-AUTH ", 13) == 0)
		return aka_req_auth(cmd + 13, resp, resp_len);

	if (os_strncmp(cmd, "AKA-AUTS ", 9) == 0)
		return aka_auts(cmd + 9, resp, resp_len);

	printf("Unknown request: %s\n", cmd);
	return -1;
}


static int process(int s)
{
	char buf[1000], resp[1000];
	struct sockaddr_un from;
	socklen_t fromlen;
	ssize_t res;

	fromlen = sizeof(from);
	res = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *) &from,
		       &fromlen);
	if (res < 0) {
		perror("recvfrom");
		return -1;
	}

	if (res == 0)
		return 0;

	if ((size_t) res >= sizeof(buf))
		res = sizeof(buf) - 1;
	buf[res] = '\0';

	printf("Received: %s\n", buf);

	if (process_cmd(buf, resp, sizeof(resp)) < 0) {
		printf("Failed to process request\n");
		return -1;
	}

	if (resp[0] == '\0') {
		printf("No response\n");
		return 0;
	}

	printf("Send: %s\n", resp);

	if (sendto(s, resp, os_strlen(resp), 0, (struct sockaddr *) &from,
		   fromlen) < 0)
		perror("send");

	return 0;
}


static void cleanup(void)
{
	struct gsm_triplet *g, *gprev;
	struct milenage_parameters *m, *prev;

	if (update_milenage && milenage_file && sqn_changes)
		update_milenage_file(milenage_file);

	g = gsm_db;
	while (g) {
		gprev = g;
		g = g->next;
		os_free(gprev);
	}

	m = milenage_db;
	while (m) {
		prev = m;
		m = m->next;
		os_free(prev);
	}

	if (serv_sock >= 0)
		close(serv_sock);
	if (socket_path)
		unlink(socket_path);

#ifdef CONFIG_SQLITE
	if (sqlite_db) {
		sqlite3_close(sqlite_db);
		sqlite_db = NULL;
	}
#endif /* CONFIG_SQLITE */
}


static void handle_term(int sig)
{
	printf("Signal %d - terminate\n", sig);
	exit(0);
}


static void usage(void)
{
	printf("HLR/AuC testing gateway for hostapd EAP-SIM/AKA "
	       "database/authenticator\n"
	       "Copyright (c) 2005-2017, Jouni Malinen <j@w1.fi>\n"
	       "\n"
	       "usage:\n"
	       "hlr_auc_gw [-hu] [-s<socket path>] [-g<triplet file>] "
	       "[-m<milenage file>] \\\n"
	       "        [-D<DB file>] [-i<IND len in bits>] [command]\n"
	       "\n"
	       "options:\n"
	       "  -h = show this usage help\n"
	       "  -u = update SQN in Milenage file on exit\n"
	       "  -s<socket path> = path for UNIX domain socket\n"
	       "                    (default: %s)\n"
	       "  -g<triplet file> = path for GSM authentication triplets\n"
	       "  -m<milenage file> = path for Milenage keys\n"
	       "  -D<DB file> = path to SQLite database\n"
	       "  -i<IND len in bits> = IND length for SQN (default: 5)\n"
	       "\n"
	       "If the optional command argument, like "
	       "\"AKA-REQ-AUTH <IMSI>\" is used, a single\n"
	       "command is processed with response sent to stdout. Otherwise, "
	       "hlr_auc_gw opens\n"
	       "a control interface and processes commands sent through it "
	       "(e.g., by EAP server\n"
	       "in hostapd).\n",
	       default_socket_path);
}


int main(int argc, char *argv[])
{
	int c;
	char *gsm_triplet_file = NULL;
	char *sqlite_db_file = NULL;
	int ret = 0;

	if (os_program_init())
		return -1;

	socket_path = default_socket_path;

	for (;;) {
		c = getopt(argc, argv, "D:g:hi:m:s:u");
		if (c < 0)
			break;
		switch (c) {
		case 'D':
#ifdef CONFIG_SQLITE
			sqlite_db_file = optarg;
			break;
#else /* CONFIG_SQLITE */
			printf("No SQLite support included in the build\n");
			return -1;
#endif /* CONFIG_SQLITE */
		case 'g':
			gsm_triplet_file = optarg;
			break;
		case 'h':
			usage();
			return 0;
		case 'i':
			ind_len = atoi(optarg);
			if (ind_len < 0 || ind_len > 32) {
				printf("Invalid IND length\n");
				return -1;
			}
			break;
		case 'm':
			milenage_file = optarg;
			break;
		case 's':
			socket_path = optarg;
			break;
		case 'u':
			update_milenage = 1;
			break;
		default:
			usage();
			return -1;
		}
	}

	if (!gsm_triplet_file && !milenage_file && !sqlite_db_file) {
		usage();
		return -1;
	}

#ifdef CONFIG_SQLITE
	if (sqlite_db_file && (sqlite_db = db_open(sqlite_db_file)) == NULL)
		return -1;
#endif /* CONFIG_SQLITE */

	if (gsm_triplet_file && read_gsm_triplets(gsm_triplet_file) < 0)
		return -1;

	if (milenage_file && read_milenage(milenage_file) < 0)
		return -1;

	if (optind == argc) {
		serv_sock = open_socket(socket_path);
		if (serv_sock < 0)
			return -1;

		printf("Listening for requests on %s\n", socket_path);

		atexit(cleanup);
		signal(SIGTERM, handle_term);
		signal(SIGINT, handle_term);

		for (;;)
			process(serv_sock);
	} else {
		char buf[1000];
		socket_path = NULL;
		stdout_debug = 0;
		if (process_cmd(argv[optind], buf, sizeof(buf)) < 0) {
			printf("FAIL\n");
			ret = -1;
		} else {
			printf("%s\n", buf);
		}
		cleanup();
	}

#ifdef CONFIG_SQLITE
	if (sqlite_db) {
		sqlite3_close(sqlite_db);
		sqlite_db = NULL;
	}
#endif /* CONFIG_SQLITE */

	os_program_deinit();

	return ret;
}
