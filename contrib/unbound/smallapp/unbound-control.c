/*
 * checkconf/unbound-control.c - remote control utility for unbound.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * The remote control utility contacts the unbound server over ssl and
 * sends the command, receives the answer, and displays the result
 * from the commandline.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif
#include "util/log.h"
#include "util/config_file.h"
#include "util/locks.h"
#include "util/net_help.h"
#include "util/shm_side/shm_main.h"
#include "daemon/stats.h"
#include "sldns/wire2str.h"
#include "sldns/pkthdr.h"

#ifdef HAVE_SYS_IPC_H
#include "sys/ipc.h"
#endif
#ifdef HAVE_SYS_SHM_H
#include "sys/shm.h"
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

static void usage(void) ATTR_NORETURN;
static void ssl_err(const char* s) ATTR_NORETURN;
static void ssl_path_err(const char* s, const char *path) ATTR_NORETURN;

/** Give unbound-control usage, and exit (1). */
static void
usage(void)
{
	printf("Usage:	local-unbound-control [options] command\n");
	printf("	Remote control utility for unbound server.\n");
	printf("Options:\n");
	printf("  -c file	config file, default is %s\n", CONFIGFILE);
	printf("  -s ip[@port]	server address, if omitted config is used.\n");
	printf("  -q		quiet (don't print anything if it works ok).\n");
	printf("  -h		show this usage help.\n");
	printf("Commands:\n");
	printf("  start				start server; runs unbound(8)\n");
	printf("  stop				stops the server\n");
	printf("  reload			reloads the server\n");
	printf("  				(this flushes data, stats, requestlist)\n");
	printf("  stats				print statistics\n");
	printf("  stats_noreset			peek at statistics\n");
#ifdef HAVE_SHMGET
	printf("  stats_shm			print statistics using shm\n");
#endif
	printf("  status			display status of server\n");
	printf("  verbosity <number>		change logging detail\n");
	printf("  log_reopen			close and open the logfile\n");
	printf("  local_zone <name> <type>	add new local zone\n");
	printf("  local_zone_remove <name>	remove local zone and its contents\n");
	printf("  local_data <RR data...>	add local data, for example\n");
	printf("				local_data www.example.com A 192.0.2.1\n");
	printf("  local_data_remove <name>	remove local RR data from name\n");
	printf("  local_zones, local_zones_remove, local_datas, local_datas_remove\n");
	printf("  				same, but read list from stdin\n");
	printf("  				(one entry per line).\n");
	printf("  dump_cache			print cache to stdout\n");
	printf("  load_cache			load cache from stdin\n");
	printf("  lookup <name>			print nameservers for name\n");
	printf("  flush <name>			flushes common types for name from cache\n");
	printf("  				types:  A, AAAA, MX, PTR, NS,\n");
	printf("					SOA, CNAME, DNAME, SRV, NAPTR\n");
	printf("  flush_type <name> <type>	flush name, type from cache\n");
	printf("  flush_zone <name>		flush everything at or under name\n");
	printf("  				from rr and dnssec caches\n");
	printf("  flush_bogus			flush all bogus data\n");
	printf("  flush_negative		flush all negative data\n");
	printf("  flush_stats 			flush statistics, make zero\n");
	printf("  flush_requestlist 		drop queries that are worked on\n");
	printf("  dump_requestlist		show what is worked on by first thread\n");
	printf("  flush_infra [all | ip] 	remove ping, edns for one IP or all\n");
	printf("  dump_infra			show ping and edns entries\n");
	printf("  set_option opt: val		set option to value, no reload\n");
	printf("  get_option opt		get option value\n");
	printf("  list_stubs			list stub-zones and root hints in use\n");
	printf("  list_forwards			list forward-zones in use\n");
	printf("  list_insecure			list domain-insecure zones\n");
	printf("  list_local_zones		list local-zones in use\n");
	printf("  list_local_data		list local-data RRs in use\n");
	printf("  insecure_add zone 		add domain-insecure zone\n");
	printf("  insecure_remove zone		remove domain-insecure zone\n");
	printf("  forward_add [+i] zone addr..	add forward-zone with servers\n");
	printf("  forward_remove [+i] zone	remove forward zone\n");
	printf("  stub_add [+ip] zone addr..	add stub-zone with servers\n");
	printf("  stub_remove [+i] zone		remove stub zone\n");
	printf("		+i		also do dnssec insecure point\n");
	printf("		+p		set stub to use priming\n");
	printf("  forward [off | addr ...]	without arg show forward setup\n");
	printf("				or off to turn off root forwarding\n");
	printf("				or give list of ip addresses\n");
	printf("  ratelimit_list [+a]		list ratelimited domains\n");
	printf("  ip_ratelimit_list [+a]	list ratelimited ip addresses\n");
	printf("		+a		list all, also not ratelimited\n");
	printf("  list_auth_zones		list auth zones\n");
	printf("  auth_zone_reload zone		reload auth zone from zonefile\n");
	printf("  auth_zone_transfer zone	transfer auth zone from master\n");
	printf("  view_list_local_zones	view	list local-zones in view\n");
	printf("  view_list_local_data	view	list local-data RRs in view\n");
	printf("  view_local_zone view name type  	add local-zone in view\n");
	printf("  view_local_zone_remove view name  	remove local-zone in view\n");
	printf("  view_local_data view RR...		add local-data in view\n");
	printf("  view_local_data_remove view name  	remove local-data in view\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE in source package for details.\n");
	printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
	exit(1);
}

#ifdef HAVE_SHMGET
/** what to put on statistics lines between var and value, ": " or "=" */
#define SQ "="
/** if true, inhibits a lot of =0 lines from the stats output */
static const int inhibit_zero = 1;
/** divide sum of timers to get average */
static void
timeval_divide(struct timeval* avg, const struct timeval* sum, long long d)
{
#ifndef S_SPLINT_S
	size_t leftover;
	if(d == 0) {
		avg->tv_sec = 0;
		avg->tv_usec = 0;
		return;
	}
	avg->tv_sec = sum->tv_sec / d;
	avg->tv_usec = sum->tv_usec / d;
	/* handle fraction from seconds divide */
	leftover = sum->tv_sec - avg->tv_sec*d;
	avg->tv_usec += (leftover*1000000)/d;
#endif
}

/** print unsigned long stats value */
#define PR_UL_NM(str, var) printf("%s."str SQ"%lu\n", nm, (unsigned long)(var));
#define PR_UL(str, var) printf(str SQ"%lu\n", (unsigned long)(var));
#define PR_UL_SUB(str, nm, var) printf(str".%s"SQ"%lu\n", nm, (unsigned long)(var));
#define PR_TIMEVAL(str, var) printf(str SQ ARG_LL "d.%6.6d\n", \
	(long long)var.tv_sec, (int)var.tv_usec);
#define PR_STATSTIME(str, var) printf(str SQ ARG_LL "d.%6.6d\n", \
	(long long)var ## _sec, (int)var ## _usec);
#define PR_LL(str, var) printf(str SQ ARG_LL"d\n", (long long)(var));

/** print stat block */
static void pr_stats(const char* nm, struct ub_stats_info* s)
{
	struct timeval sumwait, avg;
	PR_UL_NM("num.queries", s->svr.num_queries);
	PR_UL_NM("num.queries_ip_ratelimited", 
		s->svr.num_queries_ip_ratelimited);
	PR_UL_NM("num.cachehits",
		s->svr.num_queries - s->svr.num_queries_missed_cache);
	PR_UL_NM("num.cachemiss", s->svr.num_queries_missed_cache);
	PR_UL_NM("num.prefetch", s->svr.num_queries_prefetch);
	PR_UL_NM("num.zero_ttl", s->svr.zero_ttl_responses);
	PR_UL_NM("num.recursivereplies", s->mesh_replies_sent);
#ifdef USE_DNSCRYPT
    PR_UL_NM("num.dnscrypt.crypted", s->svr.num_query_dnscrypt_crypted);
    PR_UL_NM("num.dnscrypt.cert", s->svr.num_query_dnscrypt_cert);
    PR_UL_NM("num.dnscrypt.cleartext", s->svr.num_query_dnscrypt_cleartext);
    PR_UL_NM("num.dnscrypt.malformed",
             s->svr.num_query_dnscrypt_crypted_malformed);
#endif /* USE_DNSCRYPT */
	printf("%s.requestlist.avg"SQ"%g\n", nm,
		(s->svr.num_queries_missed_cache+s->svr.num_queries_prefetch)?
			(double)s->svr.sum_query_list_size/
			(double)(s->svr.num_queries_missed_cache+
			s->svr.num_queries_prefetch) : 0.0);
	PR_UL_NM("requestlist.max", s->svr.max_query_list_size);
	PR_UL_NM("requestlist.overwritten", s->mesh_jostled);
	PR_UL_NM("requestlist.exceeded", s->mesh_dropped);
	PR_UL_NM("requestlist.current.all", s->mesh_num_states);
	PR_UL_NM("requestlist.current.user", s->mesh_num_reply_states);
#ifndef S_SPLINT_S
	sumwait.tv_sec = s->mesh_replies_sum_wait_sec;
	sumwait.tv_usec = s->mesh_replies_sum_wait_usec;
#endif
	timeval_divide(&avg, &sumwait, s->mesh_replies_sent);
	printf("%s.", nm);
	PR_TIMEVAL("recursion.time.avg", avg);
	printf("%s.recursion.time.median"SQ"%g\n", nm, s->mesh_time_median);
	PR_UL_NM("tcpusage", s->svr.tcp_accept_usage);
}

/** print uptime */
static void print_uptime(struct ub_shm_stat_info* shm_stat)
{
	PR_STATSTIME("time.now", shm_stat->time.now);
	PR_STATSTIME("time.up", shm_stat->time.up);
	PR_STATSTIME("time.elapsed", shm_stat->time.elapsed);
}

/** print memory usage */
static void print_mem(struct ub_shm_stat_info* shm_stat)
{
	PR_LL("mem.cache.rrset", shm_stat->mem.rrset);
	PR_LL("mem.cache.message", shm_stat->mem.msg);
	PR_LL("mem.mod.iterator", shm_stat->mem.iter);
	PR_LL("mem.mod.validator", shm_stat->mem.val);
	PR_LL("mem.mod.respip", shm_stat->mem.respip);
#ifdef CLIENT_SUBNET
	PR_LL("mem.mod.subnet", shm_stat->mem.subnet);
#endif
#ifdef USE_IPSECMOD
	PR_LL("mem.mod.ipsecmod", shm_stat->mem.ipsecmod);
#endif
#ifdef USE_DNSCRYPT
	PR_LL("mem.cache.dnscrypt_shared_secret",
		shm_stat->mem.dnscrypt_shared_secret);
	PR_LL("mem.cache.dnscrypt_nonce",
		shm_stat->mem.dnscrypt_nonce);
#endif
}

/** print histogram */
static void print_hist(struct ub_stats_info* s)
{
	struct timehist* hist;
	size_t i;
	hist = timehist_setup();
	if(!hist)
		fatal_exit("out of memory");
	timehist_import(hist, s->svr.hist, NUM_BUCKETS_HIST);
	for(i=0; i<hist->num; i++) {
		printf("histogram.%6.6d.%6.6d.to.%6.6d.%6.6d=%lu\n",
			(int)hist->buckets[i].lower.tv_sec,
			(int)hist->buckets[i].lower.tv_usec,
			(int)hist->buckets[i].upper.tv_sec,
			(int)hist->buckets[i].upper.tv_usec,
			(unsigned long)hist->buckets[i].count);
	}
	timehist_delete(hist);
}

/** print extended */
static void print_extended(struct ub_stats_info* s)
{
	int i;
	char nm[16];

	/* TYPE */
	for(i=0; i<UB_STATS_QTYPE_NUM; i++) {
		if(inhibit_zero && s->svr.qtype[i] == 0)
			continue;
		sldns_wire2str_type_buf((uint16_t)i, nm, sizeof(nm));
		PR_UL_SUB("num.query.type", nm, s->svr.qtype[i]);
	}
	if(!inhibit_zero || s->svr.qtype_big) {
		PR_UL("num.query.type.other", s->svr.qtype_big);
	}

	/* CLASS */
	for(i=0; i<UB_STATS_QCLASS_NUM; i++) {
		if(inhibit_zero && s->svr.qclass[i] == 0)
			continue;
		sldns_wire2str_class_buf((uint16_t)i, nm, sizeof(nm));
		PR_UL_SUB("num.query.class", nm, s->svr.qclass[i]);
	}
	if(!inhibit_zero || s->svr.qclass_big) {
		PR_UL("num.query.class.other", s->svr.qclass_big);
	}

	/* OPCODE */
	for(i=0; i<UB_STATS_OPCODE_NUM; i++) {
		if(inhibit_zero && s->svr.qopcode[i] == 0)
			continue;
		sldns_wire2str_opcode_buf(i, nm, sizeof(nm));
		PR_UL_SUB("num.query.opcode", nm, s->svr.qopcode[i]);
	}

	/* transport */
	PR_UL("num.query.tcp", s->svr.qtcp);
	PR_UL("num.query.tcpout", s->svr.qtcp_outgoing);
	PR_UL("num.query.tls", s->svr.qtls);
	PR_UL("num.query.ipv6", s->svr.qipv6);

	/* flags */
	PR_UL("num.query.flags.QR", s->svr.qbit_QR);
	PR_UL("num.query.flags.AA", s->svr.qbit_AA);
	PR_UL("num.query.flags.TC", s->svr.qbit_TC);
	PR_UL("num.query.flags.RD", s->svr.qbit_RD);
	PR_UL("num.query.flags.RA", s->svr.qbit_RA);
	PR_UL("num.query.flags.Z", s->svr.qbit_Z);
	PR_UL("num.query.flags.AD", s->svr.qbit_AD);
	PR_UL("num.query.flags.CD", s->svr.qbit_CD);
	PR_UL("num.query.edns.present", s->svr.qEDNS);
	PR_UL("num.query.edns.DO", s->svr.qEDNS_DO);

	/* RCODE */
	for(i=0; i<UB_STATS_RCODE_NUM; i++) {
		/* Always include RCODEs 0-5 */
		if(inhibit_zero && i > LDNS_RCODE_REFUSED && s->svr.ans_rcode[i] == 0)
			continue;
		sldns_wire2str_rcode_buf(i, nm, sizeof(nm));
		PR_UL_SUB("num.answer.rcode", nm, s->svr.ans_rcode[i]);
	}
	if(!inhibit_zero || s->svr.ans_rcode_nodata) {
		PR_UL("num.answer.rcode.nodata", s->svr.ans_rcode_nodata);
	}
	/* iteration */
	PR_UL("num.query.ratelimited", s->svr.queries_ratelimited);
	/* validation */
	PR_UL("num.answer.secure", s->svr.ans_secure);
	PR_UL("num.answer.bogus", s->svr.ans_bogus);
	PR_UL("num.rrset.bogus", s->svr.rrset_bogus);
	PR_UL("num.query.aggressive.NOERROR", s->svr.num_neg_cache_noerror);
	PR_UL("num.query.aggressive.NXDOMAIN", s->svr.num_neg_cache_nxdomain);
	/* threat detection */
	PR_UL("unwanted.queries", s->svr.unwanted_queries);
	PR_UL("unwanted.replies", s->svr.unwanted_replies);
	/* cache counts */
	PR_UL("msg.cache.count", s->svr.msg_cache_count);
	PR_UL("rrset.cache.count", s->svr.rrset_cache_count);
	PR_UL("infra.cache.count", s->svr.infra_cache_count);
	PR_UL("key.cache.count", s->svr.key_cache_count);
#ifdef USE_DNSCRYPT
	PR_UL("dnscrypt_shared_secret.cache.count",
			 s->svr.shared_secret_cache_count);
	PR_UL("num.query.dnscrypt.shared_secret.cachemiss",
			 s->svr.num_query_dnscrypt_secret_missed_cache);
	PR_UL("dnscrypt_nonce.cache.count", s->svr.nonce_cache_count);
	PR_UL("num.query.dnscrypt.replay",
			 s->svr.num_query_dnscrypt_replay);
#endif /* USE_DNSCRYPT */
	PR_UL("num.query.authzone.up", s->svr.num_query_authzone_up);
	PR_UL("num.query.authzone.down", s->svr.num_query_authzone_down);
#ifdef CLIENT_SUBNET
	PR_UL("num.query.subnet", s->svr.num_query_subnet);
	PR_UL("num.query.subnet_cache", s->svr.num_query_subnet_cache);
#endif
}

/** print statistics out of memory structures */
static void do_stats_shm(struct config_file* cfg, struct ub_stats_info* stats,
	struct ub_shm_stat_info* shm_stat)
{
	int i;
	char nm[32];
	for(i=0; i<cfg->num_threads; i++) {
		snprintf(nm, sizeof(nm), "thread%d", i);
		pr_stats(nm, &stats[i+1]);
	}
	pr_stats("total", &stats[0]);
	print_uptime(shm_stat);
	if(cfg->stat_extended) {
		print_mem(shm_stat);
		print_hist(stats);
		print_extended(stats);
	}
}
#endif /* HAVE_SHMGET */

/** print statistics from shm memory segment */
static void print_stats_shm(const char* cfgfile)
{
#ifdef HAVE_SHMGET
	struct config_file* cfg;
	struct ub_stats_info* stats;
	struct ub_shm_stat_info* shm_stat;
	int id_ctl, id_arr;
	/* read config */
	if(!(cfg = config_create()))
		fatal_exit("out of memory");
	if(!config_read(cfg, cfgfile, NULL))
		fatal_exit("could not read config file");
	/* get shm segments */
	id_ctl = shmget(cfg->shm_key, sizeof(int), SHM_R|SHM_W);
	if(id_ctl == -1) {
		fatal_exit("shmget(%d): %s", cfg->shm_key, strerror(errno));
	}
	id_arr = shmget(cfg->shm_key+1, sizeof(int), SHM_R|SHM_W);
	if(id_arr == -1) {
		fatal_exit("shmget(%d): %s", cfg->shm_key+1, strerror(errno));
	}
	shm_stat = (struct ub_shm_stat_info*)shmat(id_ctl, NULL, 0);
	if(shm_stat == (void*)-1) {
		fatal_exit("shmat(%d): %s", id_ctl, strerror(errno));
	}
	stats = (struct ub_stats_info*)shmat(id_arr, NULL, 0);
	if(stats == (void*)-1) {
		fatal_exit("shmat(%d): %s", id_arr, strerror(errno));
	}

	/* print the stats */
	do_stats_shm(cfg, stats, shm_stat);

	/* shutdown */
	shmdt(shm_stat);
	shmdt(stats);
	config_delete(cfg);
#else
	(void)cfgfile;
#endif /* HAVE_SHMGET */
}

/** exit with ssl error */
static void ssl_err(const char* s)
{
	fprintf(stderr, "error: %s\n", s);
	ERR_print_errors_fp(stderr);
	exit(1);
}

/** exit with ssl error related to a file path */
static void ssl_path_err(const char* s, const char *path)
{
	unsigned long err;
	err = ERR_peek_error();
	if (ERR_GET_LIB(err) == ERR_LIB_SYS &&
		(ERR_GET_FUNC(err) == SYS_F_FOPEN ||
		 ERR_GET_FUNC(err) == SYS_F_FREAD) ) {
		fprintf(stderr, "error: %s\n%s: %s\n",
			s, path, ERR_reason_error_string(err));
		exit(1);
	} else {
		ssl_err(s);
	}
}

/** setup SSL context */
static SSL_CTX*
setup_ctx(struct config_file* cfg)
{
	char* s_cert=NULL, *c_key=NULL, *c_cert=NULL;
	SSL_CTX* ctx;

	if(!(options_remote_is_address(cfg) && cfg->control_use_cert))
		return NULL;
	s_cert = fname_after_chroot(cfg->server_cert_file, cfg, 1);
	c_key = fname_after_chroot(cfg->control_key_file, cfg, 1);
	c_cert = fname_after_chroot(cfg->control_cert_file, cfg, 1);
	if(!s_cert || !c_key || !c_cert)
		fatal_exit("out of memory");
	ctx = SSL_CTX_new(SSLv23_client_method());
	if(!ctx)
		ssl_err("could not allocate SSL_CTX pointer");
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2)
		ssl_err("could not set SSL_OP_NO_SSLv2");
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3)
		ssl_err("could not set SSL_OP_NO_SSLv3");
	if(!SSL_CTX_use_certificate_chain_file(ctx,c_cert))
		ssl_path_err("Error setting up SSL_CTX client cert", c_cert);
	if (!SSL_CTX_use_PrivateKey_file(ctx,c_key,SSL_FILETYPE_PEM))
		ssl_path_err("Error setting up SSL_CTX client key", c_key);
	if (!SSL_CTX_check_private_key(ctx))
		ssl_err("Error setting up SSL_CTX client key");
	if (SSL_CTX_load_verify_locations(ctx, s_cert, NULL) != 1)
		ssl_path_err("Error setting up SSL_CTX verify, server cert",
			     s_cert);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

	free(s_cert);
	free(c_key);
	free(c_cert);
	return ctx;
}

/** contact the server with TCP connect */
static int
contact_server(const char* svr, struct config_file* cfg, int statuscmd)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int addrfamily = 0, proto = IPPROTO_TCP;
	int fd, useport = 1;
	/* use svr or the first config entry */
	if(!svr) {
		if(cfg->control_ifs.first) {
			svr = cfg->control_ifs.first->str;
		} else if(cfg->do_ip4) {
			svr = "127.0.0.1";
		} else {
			svr = "::1";
		}
		/* config 0 addr (everything), means ask localhost */
		if(strcmp(svr, "0.0.0.0") == 0)
			svr = "127.0.0.1";
		else if(strcmp(svr, "::0") == 0 ||
			strcmp(svr, "0::0") == 0 ||
			strcmp(svr, "0::") == 0 ||
			strcmp(svr, "::") == 0)
			svr = "::1";
	}
	if(strchr(svr, '@')) {
		if(!extstrtoaddr(svr, &addr, &addrlen))
			fatal_exit("could not parse IP@port: %s", svr);
#ifdef HAVE_SYS_UN_H
	} else if(svr[0] == '/') {
		struct sockaddr_un* usock = (struct sockaddr_un *) &addr;
		usock->sun_family = AF_LOCAL;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
		usock->sun_len = (unsigned)sizeof(usock);
#endif
		(void)strlcpy(usock->sun_path, svr, sizeof(usock->sun_path));
		addrlen = (socklen_t)sizeof(struct sockaddr_un);
		addrfamily = AF_LOCAL;
		useport = 0;
		proto = 0;
#endif
	} else {
		if(!ipstrtoaddr(svr, cfg->control_port, &addr, &addrlen))
			fatal_exit("could not parse IP: %s", svr);
	}

	if(addrfamily == 0)
		addrfamily = addr_is_ip6(&addr, addrlen)?PF_INET6:PF_INET;
	fd = socket(addrfamily, SOCK_STREAM, proto);
	if(fd == -1) {
#ifndef USE_WINSOCK
		fatal_exit("socket: %s", strerror(errno));
#else
		fatal_exit("socket: %s", wsa_strerror(WSAGetLastError()));
#endif
	}
	if(connect(fd, (struct sockaddr*)&addr, addrlen) < 0) {
#ifndef USE_WINSOCK
		int err = errno;
		if(!useport) log_err("connect: %s for %s", strerror(err), svr);
		else log_err_addr("connect", strerror(err), &addr, addrlen);
		if(err == ECONNREFUSED && statuscmd) {
			printf("unbound is stopped\n");
			exit(3);
		}
#else
		int wsaerr = WSAGetLastError();
		if(!useport) log_err("connect: %s for %s", wsa_strerror(wsaerr), svr);
		else log_err_addr("connect", wsa_strerror(wsaerr), &addr, addrlen);
		if(wsaerr == WSAECONNREFUSED && statuscmd) {
			printf("unbound is stopped\n");
			exit(3);
		}
#endif
		exit(1);
	}
	return fd;
}

/** setup SSL on the connection */
static SSL*
setup_ssl(SSL_CTX* ctx, int fd)
{
	SSL* ssl;
	X509* x;
	int r;

	if(!ctx) return NULL;
	ssl = SSL_new(ctx);
	if(!ssl)
		ssl_err("could not SSL_new");
	SSL_set_connect_state(ssl);
	(void)SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd))
		ssl_err("could not SSL_set_fd");
	while(1) {
		ERR_clear_error();
		if( (r=SSL_do_handshake(ssl)) == 1)
			break;
		r = SSL_get_error(ssl, r);
		if(r != SSL_ERROR_WANT_READ && r != SSL_ERROR_WANT_WRITE)
			ssl_err("SSL handshake failed");
		/* wants to be called again */
	}

	/* check authenticity of server */
	if(SSL_get_verify_result(ssl) != X509_V_OK)
		ssl_err("SSL verification failed");
	x = SSL_get_peer_certificate(ssl);
	if(!x)
		ssl_err("Server presented no peer certificate");
	X509_free(x);

	return ssl;
}

/** read from ssl or fd, fatalexit on error, 0 EOF, 1 success */
static int
remote_read(SSL* ssl, int fd, char* buf, size_t len)
{
	if(ssl) {
		int r;
		ERR_clear_error();
		if((r = SSL_read(ssl, buf, (int)len-1)) <= 0) {
			if(SSL_get_error(ssl, r) == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				return 0;
			}
			ssl_err("could not SSL_read");
		}
		buf[r] = 0;
	} else {
		ssize_t rr = recv(fd, buf, len-1, 0);
		if(rr <= 0) {
			if(rr == 0) {
				/* EOF */
				return 0;
			}
#ifndef USE_WINSOCK
			fatal_exit("could not recv: %s", strerror(errno));
#else
			fatal_exit("could not recv: %s", wsa_strerror(WSAGetLastError()));
#endif
		}
		buf[rr] = 0;
	}
	return 1;
}

/** write to ssl or fd, fatalexit on error */
static void
remote_write(SSL* ssl, int fd, const char* buf, size_t len)
{
	if(ssl) {
		if(SSL_write(ssl, buf, (int)len) <= 0)
			ssl_err("could not SSL_write");
	} else {
		if(send(fd, buf, len, 0) < (ssize_t)len) {
#ifndef USE_WINSOCK
			fatal_exit("could not send: %s", strerror(errno));
#else
			fatal_exit("could not send: %s", wsa_strerror(WSAGetLastError()));
#endif
		}
	}
}

/** send stdin to server */
static void
send_file(SSL* ssl, int fd, FILE* in, char* buf, size_t sz)
{
	while(fgets(buf, (int)sz, in)) {
		remote_write(ssl, fd, buf, strlen(buf));
	}
}

/** send end-of-file marker to server */
static void
send_eof(SSL* ssl, int fd)
{
	char e[] = {0x04, 0x0a};
	remote_write(ssl, fd, e, sizeof(e));
}

/** send command and display result */
static int
go_cmd(SSL* ssl, int fd, int quiet, int argc, char* argv[])
{
	char pre[10];
	const char* space=" ";
	const char* newline="\n";
	int was_error = 0, first_line = 1;
	int i;
	char buf[1024];
	snprintf(pre, sizeof(pre), "UBCT%d ", UNBOUND_CONTROL_VERSION);
	remote_write(ssl, fd, pre, strlen(pre));
	for(i=0; i<argc; i++) {
		remote_write(ssl, fd, space, strlen(space));
		remote_write(ssl, fd, argv[i], strlen(argv[i]));
	}
	remote_write(ssl, fd, newline, strlen(newline));

	if(argc == 1 && strcmp(argv[0], "load_cache") == 0) {
		send_file(ssl, fd, stdin, buf, sizeof(buf));
	}
	else if(argc == 1 && (strcmp(argv[0], "local_zones") == 0 ||
		strcmp(argv[0], "local_zones_remove") == 0 ||
		strcmp(argv[0], "local_datas") == 0 ||
		strcmp(argv[0], "local_datas_remove") == 0)) {
		send_file(ssl, fd, stdin, buf, sizeof(buf));
		send_eof(ssl, fd);
	}

	while(1) {
		if(remote_read(ssl, fd, buf, sizeof(buf)) == 0) {
			break; /* EOF */
		}
		if(first_line && strncmp(buf, "error", 5) == 0) {
			printf("%s", buf);
			was_error = 1;
		} else if (!quiet)
			printf("%s", buf);

		first_line = 0;
	}
	return was_error;
}

/** go ahead and read config, contact server and perform command and display */
static int
go(const char* cfgfile, char* svr, int quiet, int argc, char* argv[])
{
	struct config_file* cfg;
	int fd, ret;
	SSL_CTX* ctx;
	SSL* ssl;

	/* read config */
	if(!(cfg = config_create()))
		fatal_exit("out of memory");
	if(!config_read(cfg, cfgfile, NULL))
		fatal_exit("could not read config file");
	if(!cfg->remote_control_enable)
		log_warn("control-enable is 'no' in the config file.");
#ifdef UB_ON_WINDOWS
	w_config_adjust_directory(cfg);
#endif
	ctx = setup_ctx(cfg);

	/* contact server */
	fd = contact_server(svr, cfg, argc>0&&strcmp(argv[0],"status")==0);
	ssl = setup_ssl(ctx, fd);

	/* send command */
	ret = go_cmd(ssl, fd, quiet, argc, argv);

	if(ssl) SSL_free(ssl);
#ifndef USE_WINSOCK
	close(fd);
#else
	closesocket(fd);
#endif
	if(ctx) SSL_CTX_free(ctx);
	config_delete(cfg);
	return ret;
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for unbound-control */
int main(int argc, char* argv[])
{
	int c, ret;
	int quiet = 0;
	const char* cfgfile = CONFIGFILE;
	char* svr = NULL;
#ifdef USE_WINSOCK
	int r;
	WSADATA wsa_data;
#endif
#ifdef USE_THREAD_DEBUG
	/* stop the file output from unbound-control, overwrites the servers */
	extern int check_locking_order;
	check_locking_order = 0;
#endif /* USE_THREAD_DEBUG */
	log_ident_set("unbound-control");
	log_init(NULL, 0, NULL);
	checklock_start();
#ifdef USE_WINSOCK
	/* use registry config file in preference to compiletime location */
	if(!(cfgfile=w_lookup_reg_str("Software\\Unbound", "ConfigFile")))
		cfgfile = CONFIGFILE;
#endif
	/* parse the options */
	while( (c=getopt(argc, argv, "c:s:qh")) != -1) {
		switch(c) {
		case 'c':
			cfgfile = optarg;
			break;
		case 's':
			svr = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case '?':
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if(argc == 0)
		usage();
	if(argc >= 1 && strcmp(argv[0], "start")==0) {
		if(execlp("unbound", "unbound", "-c", cfgfile, 
			(char*)NULL) < 0) {
			fatal_exit("could not exec unbound: %s",
				strerror(errno));
		}
	}
	if(argc >= 1 && strcmp(argv[0], "stats_shm")==0) {
		print_stats_shm(cfgfile);
		return 0;
	}

#ifdef USE_WINSOCK
	if((r = WSAStartup(MAKEWORD(2,2), &wsa_data)) != 0)
		fatal_exit("WSAStartup failed: %s", wsa_strerror(r));
#endif

#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
	OpenSSL_add_all_algorithms();
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#else
	(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif

	if(!RAND_status()) {
		/* try to seed it */
		unsigned char buf[256];
		unsigned int seed=(unsigned)time(NULL) ^ (unsigned)getpid();
		unsigned int v = seed;
		size_t i;
		for(i=0; i<256/sizeof(v); i++) {
			memmove(buf+i*sizeof(v), &v, sizeof(v));
			v = v*seed + (unsigned int)i;
		}
		RAND_seed(buf, 256);
		log_warn("no entropy, seeding openssl PRNG with time\n");
	}

	ret = go(cfgfile, svr, quiet, argc, argv);

#ifdef USE_WINSOCK
	WSACleanup();
#endif
	checklock_stop();
	return ret;
}
