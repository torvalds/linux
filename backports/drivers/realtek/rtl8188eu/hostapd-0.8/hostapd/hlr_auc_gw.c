/*
 * HLR/AuC testing gateway for hostapd EAP-SIM/AKA database/authenticator
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
 * The example implementation here reads GSM authentication triplets from a
 * text file in IMSI:Kc:SRES:RAND format, IMSI in ASCII, other fields as hex
 * strings. This is used to simulate an HLR/AuC. As such, it is not very useful
 * for real life authentication, but it is useful both as an example
 * implementation and for EAP-SIM testing.
 */

#include "includes.h"
#include <sys/un.h>

#include "common.h"
#include "crypto/milenage.h"
#include "crypto/random.h"

static const char *default_socket_path = "/tmp/hlr_auc_gw.sock";
static const char *socket_path;
static int serv_sock = -1;

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
};

static struct milenage_parameters *milenage_db = NULL;

#define EAP_SIM_MAX_CHAL 3

#define EAP_AKA_RAND_LEN 16
#define EAP_AKA_AUTN_LEN 16
#define EAP_AKA_AUTS_LEN 14
#define EAP_AKA_RES_MAX_LEN 16
#define EAP_AKA_IK_LEN 16
#define EAP_AKA_CK_LEN 16


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
		perror("bind(PF_UNIX)");
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
		printf("Could not open GSM tripler data file '%s'\n", fname);
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
		pos2 = strchr(pos, ':');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid IMSI (%s)\n",
			       fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) >= sizeof(g->imsi)) {
			printf("%s:%d - Too long IMSI (%s)\n",
			       fname, line, pos);
			ret = -1;
			break;
		}
		os_strlcpy(g->imsi, pos, sizeof(g->imsi));
		pos = pos2 + 1;

		/* Kc */
		pos2 = strchr(pos, ':');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid Kc (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) != 16 || hexstr2bin(pos, g->kc, 8)) {
			printf("%s:%d - Invalid Kc (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		/* SRES */
		pos2 = strchr(pos, ':');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid SRES (%s)\n", fname, line,
			       pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) != 8 || hexstr2bin(pos, g->sres, 4)) {
			printf("%s:%d - Invalid SRES (%s)\n", fname, line,
			       pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		/* RAND */
		pos2 = strchr(pos, ':');
		if (pos2)
			*pos2 = '\0';
		if (strlen(pos) != 32 || hexstr2bin(pos, g->_rand, 16)) {
			printf("%s:%d - Invalid RAND (%s)\n", fname, line,
			       pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		g->next = gsm_db;
		gsm_db = g;
		g = NULL;
	}
	free(g);

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

		/* Parse IMSI Ki OPc AMF SQN */
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
		pos2 = strchr(pos, ' ');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid IMSI (%s)\n",
			       fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) >= sizeof(m->imsi)) {
			printf("%s:%d - Too long IMSI (%s)\n",
			       fname, line, pos);
			ret = -1;
			break;
		}
		os_strlcpy(m->imsi, pos, sizeof(m->imsi));
		pos = pos2 + 1;

		/* Ki */
		pos2 = strchr(pos, ' ');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid Ki (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) != 32 || hexstr2bin(pos, m->ki, 16)) {
			printf("%s:%d - Invalid Ki (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		/* OPc */
		pos2 = strchr(pos, ' ');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid OPc (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) != 32 || hexstr2bin(pos, m->opc, 16)) {
			printf("%s:%d - Invalid OPc (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		/* AMF */
		pos2 = strchr(pos, ' ');
		if (pos2 == NULL) {
			printf("%s:%d - Invalid AMF (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		*pos2 = '\0';
		if (strlen(pos) != 4 || hexstr2bin(pos, m->amf, 2)) {
			printf("%s:%d - Invalid AMF (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		/* SQN */
		pos2 = strchr(pos, ' ');
		if (pos2)
			*pos2 = '\0';
		if (strlen(pos) != 12 || hexstr2bin(pos, m->sqn, 6)) {
			printf("%s:%d - Invalid SEQ (%s)\n", fname, line, pos);
			ret = -1;
			break;
		}
		pos = pos2 + 1;

		m->next = milenage_db;
		milenage_db = m;
		m = NULL;
	}
	free(m);

	fclose(f);

	return ret;
}


static struct milenage_parameters * get_milenage(const char *imsi)
{
	struct milenage_parameters *m = milenage_db;

	while (m) {
		if (strcmp(m->imsi, imsi) == 0)
			break;
		m = m->next;
	}

	return m;
}


static void sim_req_auth(int s, struct sockaddr_un *from, socklen_t fromlen,
			 char *imsi)
{
	int count, max_chal, ret;
	char *pos;
	char reply[1000], *rpos, *rend;
	struct milenage_parameters *m;
	struct gsm_triplet *g;

	reply[0] = '\0';

	pos = strchr(imsi, ' ');
	if (pos) {
		*pos++ = '\0';
		max_chal = atoi(pos);
		if (max_chal < 1 || max_chal < EAP_SIM_MAX_CHAL)
			max_chal = EAP_SIM_MAX_CHAL;
	} else
		max_chal = EAP_SIM_MAX_CHAL;

	rend = &reply[sizeof(reply)];
	rpos = reply;
	ret = snprintf(rpos, rend - rpos, "SIM-RESP-AUTH %s", imsi);
	if (ret < 0 || ret >= rend - rpos)
		return;
	rpos += ret;

	m = get_milenage(imsi);
	if (m) {
		u8 _rand[16], sres[4], kc[8];
		for (count = 0; count < max_chal; count++) {
			if (random_get_bytes(_rand, 16) < 0)
				return;
			gsm_milenage(m->opc, m->ki, _rand, sres, kc);
			*rpos++ = ' ';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, kc, 8);
			*rpos++ = ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, sres, 4);
			*rpos++ = ':';
			rpos += wpa_snprintf_hex(rpos, rend - rpos, _rand, 16);
		}
		*rpos = '\0';
		goto send;
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
			return;
		rpos += ret;
	}

send:
	printf("Send: %s\n", reply);
	if (sendto(s, reply, rpos - reply, 0,
		   (struct sockaddr *) from, fromlen) < 0)
		perror("send");
}


static void aka_req_auth(int s, struct sockaddr_un *from, socklen_t fromlen,
			 char *imsi)
{
	/* AKA-RESP-AUTH <IMSI> <RAND> <AUTN> <IK> <CK> <RES> */
	char reply[1000], *pos, *end;
	u8 _rand[EAP_AKA_RAND_LEN];
	u8 autn[EAP_AKA_AUTN_LEN];
	u8 ik[EAP_AKA_IK_LEN];
	u8 ck[EAP_AKA_CK_LEN];
	u8 res[EAP_AKA_RES_MAX_LEN];
	size_t res_len;
	int ret;
	struct milenage_parameters *m;

	m = get_milenage(imsi);
	if (m) {
		if (random_get_bytes(_rand, EAP_AKA_RAND_LEN) < 0)
			return;
		res_len = EAP_AKA_RES_MAX_LEN;
		inc_byte_array(m->sqn, 6);
		printf("AKA: Milenage with SQN=%02x%02x%02x%02x%02x%02x\n",
		       m->sqn[0], m->sqn[1], m->sqn[2],
		       m->sqn[3], m->sqn[4], m->sqn[5]);
		milenage_generate(m->opc, m->amf, m->ki, m->sqn, _rand,
				  autn, ik, ck, res, &res_len);
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
		return;
#endif /* AKA_USE_FIXED_TEST_VALUES */
	}

	pos = reply;
	end = &reply[sizeof(reply)];
	ret = snprintf(pos, end - pos, "AKA-RESP-AUTH %s ", imsi);
	if (ret < 0 || ret >= end - pos)
		return;
	pos += ret;
	pos += wpa_snprintf_hex(pos, end - pos, _rand, EAP_AKA_RAND_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, autn, EAP_AKA_AUTN_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, ik, EAP_AKA_IK_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, ck, EAP_AKA_CK_LEN);
	*pos++ = ' ';
	pos += wpa_snprintf_hex(pos, end - pos, res, res_len);

	printf("Send: %s\n", reply);

	if (sendto(s, reply, pos - reply, 0, (struct sockaddr *) from,
		   fromlen) < 0)
		perror("send");
}


static void aka_auts(int s, struct sockaddr_un *from, socklen_t fromlen,
		     char *imsi)
{
	char *auts, *__rand;
	u8 _auts[EAP_AKA_AUTS_LEN], _rand[EAP_AKA_RAND_LEN], sqn[6];
	struct milenage_parameters *m;

	/* AKA-AUTS <IMSI> <AUTS> <RAND> */

	auts = strchr(imsi, ' ');
	if (auts == NULL)
		return;
	*auts++ = '\0';

	__rand = strchr(auts, ' ');
	if (__rand == NULL)
		return;
	*__rand++ = '\0';

	printf("AKA-AUTS: IMSI=%s AUTS=%s RAND=%s\n", imsi, auts, __rand);
	if (hexstr2bin(auts, _auts, EAP_AKA_AUTS_LEN) ||
	    hexstr2bin(__rand, _rand, EAP_AKA_RAND_LEN)) {
		printf("Could not parse AUTS/RAND\n");
		return;
	}

	m = get_milenage(imsi);
	if (m == NULL) {
		printf("Unknown IMSI: %s\n", imsi);
		return;
	}

	if (milenage_auts(m->opc, m->ki, _rand, _auts, sqn)) {
		printf("AKA-AUTS: Incorrect MAC-S\n");
	} else {
		memcpy(m->sqn, sqn, 6);
		printf("AKA-AUTS: Re-synchronized: "
		       "SQN=%02x%02x%02x%02x%02x%02x\n",
		       sqn[0], sqn[1], sqn[2], sqn[3], sqn[4], sqn[5]);
	}
}


static int process(int s)
{
	char buf[1000];
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

	if (strncmp(buf, "SIM-REQ-AUTH ", 13) == 0)
		sim_req_auth(s, &from, fromlen, buf + 13);
	else if (strncmp(buf, "AKA-REQ-AUTH ", 13) == 0)
		aka_req_auth(s, &from, fromlen, buf + 13);
	else if (strncmp(buf, "AKA-AUTS ", 9) == 0)
		aka_auts(s, &from, fromlen, buf + 9);
	else
		printf("Unknown request: %s\n", buf);

	return 0;
}


static void cleanup(void)
{
	struct gsm_triplet *g, *gprev;
	struct milenage_parameters *m, *prev;

	g = gsm_db;
	while (g) {
		gprev = g;
		g = g->next;
		free(gprev);
	}

	m = milenage_db;
	while (m) {
		prev = m;
		m = m->next;
		free(prev);
	}

	close(serv_sock);
	unlink(socket_path);
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
	       "Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>\n"
	       "\n"
	       "usage:\n"
	       "hlr_auc_gw [-h] [-s<socket path>] [-g<triplet file>] "
	       "[-m<milenage file>]\n"
	       "\n"
	       "options:\n"
	       "  -h = show this usage help\n"
	       "  -s<socket path> = path for UNIX domain socket\n"
	       "                    (default: %s)\n"
	       "  -g<triplet file> = path for GSM authentication triplets\n"
	       "  -m<milenage file> = path for Milenage keys\n",
	       default_socket_path);
}


int main(int argc, char *argv[])
{
	int c;
	char *milenage_file = NULL;
	char *gsm_triplet_file = NULL;

	socket_path = default_socket_path;

	for (;;) {
		c = getopt(argc, argv, "g:hm:s:");
		if (c < 0)
			break;
		switch (c) {
		case 'g':
			gsm_triplet_file = optarg;
			break;
		case 'h':
			usage();
			return 0;
		case 'm':
			milenage_file = optarg;
			break;
		case 's':
			socket_path = optarg;
			break;
		default:
			usage();
			return -1;
		}
	}

	if (gsm_triplet_file && read_gsm_triplets(gsm_triplet_file) < 0)
		return -1;

	if (milenage_file && read_milenage(milenage_file) < 0)
		return -1;

	serv_sock = open_socket(socket_path);
	if (serv_sock < 0)
		return -1;

	printf("Listening for requests on %s\n", socket_path);

	atexit(cleanup);
	signal(SIGTERM, handle_term);
	signal(SIGINT, handle_term);

	for (;;)
		process(serv_sock);

	return 0;
}
