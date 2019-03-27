/*
 * util/config_file.c - reads and stores the config file for unbound.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * This file contains functions for the config file.
 */

#include "config.h"
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include "util/log.h"
#include "util/configyyrename.h"
#include "util/config_file.h"
#include "configparser.h"
#include "util/net_help.h"
#include "util/data/msgparse.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/fptr_wlist.h"
#include "util/data/dname.h"
#include "util/rtt.h"
#include "services/cache/infra.h"
#include "sldns/wire2str.h"
#include "sldns/parseutil.h"
#ifdef HAVE_GLOB_H
# include <glob.h>
#endif
#ifdef CLIENT_SUBNET
#include "edns-subnet/edns-subnet.h"
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

/** from cfg username, after daemonize setup performed */
uid_t cfg_uid = (uid_t)-1;
/** from cfg username, after daemonize setup performed */
gid_t cfg_gid = (gid_t)-1;
/** for debug allow small timeout values for fast rollovers */
int autr_permit_small_holddown = 0;

/** global config during parsing */
struct config_parser_state* cfg_parser = 0;

/** init ports possible for use */
static void init_outgoing_availports(int* array, int num);

struct config_file* 
config_create(void)
{
	struct config_file* cfg;
	cfg = (struct config_file*)calloc(1, sizeof(struct config_file));
	if(!cfg)
		return NULL;
	/* the defaults if no config is present */
	cfg->verbosity = 1;
	cfg->stat_interval = 0;
	cfg->stat_cumulative = 0;
	cfg->stat_extended = 0;
	cfg->num_threads = 1;
	cfg->port = UNBOUND_DNS_PORT;
	cfg->do_ip4 = 1;
	cfg->do_ip6 = 1;
	cfg->do_udp = 1;
	cfg->do_tcp = 1;
	cfg->tcp_upstream = 0;
	cfg->udp_upstream_without_downstream = 0;
	cfg->tcp_mss = 0;
	cfg->outgoing_tcp_mss = 0;
	cfg->tcp_idle_timeout = 30 * 1000; /* 30s in millisecs */
	cfg->do_tcp_keepalive = 0;
	cfg->tcp_keepalive_timeout = 120 * 1000; /* 120s in millisecs */
	cfg->ssl_service_key = NULL;
	cfg->ssl_service_pem = NULL;
	cfg->ssl_port = UNBOUND_DNS_OVER_TLS_PORT;
	cfg->ssl_upstream = 0;
	cfg->tls_cert_bundle = NULL;
	cfg->tls_win_cert = 0;
	cfg->use_syslog = 1;
	cfg->log_identity = NULL; /* changed later with argv[0] */
	cfg->log_time_ascii = 0;
	cfg->log_queries = 0;
	cfg->log_replies = 0;
	cfg->log_local_actions = 0;
	cfg->log_servfail = 0;
#ifndef USE_WINSOCK
#  ifdef USE_MINI_EVENT
	/* select max 1024 sockets */
	cfg->outgoing_num_ports = 960;
	cfg->num_queries_per_thread = 512;
#  else
	/* libevent can use many sockets */
	cfg->outgoing_num_ports = 4096;
	cfg->num_queries_per_thread = 1024;
#  endif
	cfg->outgoing_num_tcp = 10;
	cfg->incoming_num_tcp = 10;
#else
	cfg->outgoing_num_ports = 48; /* windows is limited in num fds */
	cfg->num_queries_per_thread = 24;
	cfg->outgoing_num_tcp = 2; /* leaves 64-52=12 for: 4if,1stop,thread4 */
	cfg->incoming_num_tcp = 2; 
#endif
	cfg->edns_buffer_size = 4096; /* 4k from rfc recommendation */
	cfg->msg_buffer_size = 65552; /* 64 k + a small margin */
	cfg->msg_cache_size = 4 * 1024 * 1024;
	cfg->msg_cache_slabs = 4;
	cfg->jostle_time = 200;
	cfg->rrset_cache_size = 4 * 1024 * 1024;
	cfg->rrset_cache_slabs = 4;
	cfg->host_ttl = 900;
	cfg->bogus_ttl = 60;
	cfg->min_ttl = 0;
	cfg->max_ttl = 3600 * 24;
	cfg->max_negative_ttl = 3600;
	cfg->prefetch = 0;
	cfg->prefetch_key = 0;
	cfg->infra_cache_slabs = 4;
	cfg->infra_cache_numhosts = 10000;
	cfg->infra_cache_min_rtt = 50;
	cfg->delay_close = 0;
	if(!(cfg->outgoing_avail_ports = (int*)calloc(65536, sizeof(int))))
		goto error_exit;
	init_outgoing_availports(cfg->outgoing_avail_ports, 65536);
	if(!(cfg->username = strdup(UB_USERNAME))) goto error_exit;
#ifdef HAVE_CHROOT
	if(!(cfg->chrootdir = strdup(CHROOT_DIR))) goto error_exit;
#endif
	if(!(cfg->directory = strdup(RUN_DIR))) goto error_exit;
	if(!(cfg->logfile = strdup(""))) goto error_exit;
	if(!(cfg->pidfile = strdup(PIDFILE))) goto error_exit;
	if(!(cfg->target_fetch_policy = strdup("3 2 1 0 0"))) goto error_exit;
	cfg->low_rtt_permil = 0;
	cfg->low_rtt = 45;
	cfg->donotqueryaddrs = NULL;
	cfg->donotquery_localhost = 1;
	cfg->root_hints = NULL;
	cfg->use_systemd = 0;
	cfg->do_daemonize = 1;
	cfg->if_automatic = 0;
	cfg->so_rcvbuf = 0;
	cfg->so_sndbuf = 0;
	cfg->so_reuseport = REUSEPORT_DEFAULT;
	cfg->ip_transparent = 0;
	cfg->ip_freebind = 0;
	cfg->num_ifs = 0;
	cfg->ifs = NULL;
	cfg->num_out_ifs = 0;
	cfg->out_ifs = NULL;
	cfg->stubs = NULL;
	cfg->forwards = NULL;
	cfg->auths = NULL;
#ifdef CLIENT_SUBNET
	cfg->client_subnet = NULL;
	cfg->client_subnet_zone = NULL;
	cfg->client_subnet_opcode = LDNS_EDNS_CLIENT_SUBNET;
	cfg->client_subnet_always_forward = 0;
	cfg->max_client_subnet_ipv4 = 24;
	cfg->max_client_subnet_ipv6 = 56;
#endif
	cfg->views = NULL;
	cfg->acls = NULL;
	cfg->tcp_connection_limits = NULL;
	cfg->harden_short_bufsize = 0;
	cfg->harden_large_queries = 0;
	cfg->harden_glue = 1;
	cfg->harden_dnssec_stripped = 1;
	cfg->harden_below_nxdomain = 1;
	cfg->harden_referral_path = 0;
	cfg->harden_algo_downgrade = 0;
	cfg->use_caps_bits_for_id = 0;
	cfg->caps_whitelist = NULL;
	cfg->private_address = NULL;
	cfg->private_domain = NULL;
	cfg->unwanted_threshold = 0;
	cfg->hide_identity = 0;
	cfg->hide_version = 0;
	cfg->hide_trustanchor = 0;
	cfg->identity = NULL;
	cfg->version = NULL;
	cfg->auto_trust_anchor_file_list = NULL;
	cfg->trust_anchor_file_list = NULL;
	cfg->trust_anchor_list = NULL;
	cfg->trusted_keys_file_list = NULL;
	cfg->trust_anchor_signaling = 1;
	cfg->root_key_sentinel = 1;
	cfg->dlv_anchor_file = NULL;
	cfg->dlv_anchor_list = NULL;
	cfg->domain_insecure = NULL;
	cfg->val_date_override = 0;
	cfg->val_sig_skew_min = 3600; /* at least daylight savings trouble */
	cfg->val_sig_skew_max = 86400; /* at most timezone settings trouble */
	cfg->val_clean_additional = 1;
	cfg->val_log_level = 0;
	cfg->val_log_squelch = 0;
	cfg->val_permissive_mode = 0;
	cfg->aggressive_nsec = 0;
	cfg->ignore_cd = 0;
	cfg->serve_expired = 0;
	cfg->serve_expired_ttl = 0;
	cfg->serve_expired_ttl_reset = 0;
	cfg->add_holddown = 30*24*3600;
	cfg->del_holddown = 30*24*3600;
	cfg->keep_missing = 366*24*3600; /* one year plus a little leeway */
	cfg->permit_small_holddown = 0;
	cfg->key_cache_size = 4 * 1024 * 1024;
	cfg->key_cache_slabs = 4;
	cfg->neg_cache_size = 1 * 1024 * 1024;
	cfg->local_zones = NULL;
	cfg->local_zones_nodefault = NULL;
	cfg->local_zones_disable_default = 0;
	cfg->local_data = NULL;
	cfg->local_zone_overrides = NULL;
	cfg->unblock_lan_zones = 0;
	cfg->insecure_lan_zones = 0;
	cfg->python_script = NULL;
	cfg->remote_control_enable = 0;
	cfg->control_ifs.first = NULL;
	cfg->control_ifs.last = NULL;
	cfg->control_port = UNBOUND_CONTROL_PORT;
	cfg->control_use_cert = 1;
	cfg->minimal_responses = 1;
	cfg->rrset_roundrobin = 0;
	cfg->max_udp_size = 4096;
	if(!(cfg->server_key_file = strdup(RUN_DIR"/unbound_server.key"))) 
		goto error_exit;
	if(!(cfg->server_cert_file = strdup(RUN_DIR"/unbound_server.pem"))) 
		goto error_exit;
	if(!(cfg->control_key_file = strdup(RUN_DIR"/unbound_control.key"))) 
		goto error_exit;
	if(!(cfg->control_cert_file = strdup(RUN_DIR"/unbound_control.pem"))) 
		goto error_exit;

#ifdef CLIENT_SUBNET
	if(!(cfg->module_conf = strdup("subnetcache validator iterator"))) goto error_exit;
#else
	if(!(cfg->module_conf = strdup("validator iterator"))) goto error_exit;
#endif
	if(!(cfg->val_nsec3_key_iterations = 
		strdup("1024 150 2048 500 4096 2500"))) goto error_exit;
#if defined(DNSTAP_SOCKET_PATH)
	if(!(cfg->dnstap_socket_path = strdup(DNSTAP_SOCKET_PATH)))
		goto error_exit;
#endif
	cfg->disable_dnssec_lame_check = 0;
	cfg->ip_ratelimit = 0;
	cfg->ratelimit = 0;
	cfg->ip_ratelimit_slabs = 4;
	cfg->ratelimit_slabs = 4;
	cfg->ip_ratelimit_size = 4*1024*1024;
	cfg->ratelimit_size = 4*1024*1024;
	cfg->ratelimit_for_domain = NULL;
	cfg->ratelimit_below_domain = NULL;
	cfg->ip_ratelimit_factor = 10;
	cfg->ratelimit_factor = 10;
	cfg->qname_minimisation = 1;
	cfg->qname_minimisation_strict = 0;
	cfg->shm_enable = 0;
	cfg->shm_key = 11777;
	cfg->dnscrypt = 0;
	cfg->dnscrypt_port = 0;
	cfg->dnscrypt_provider = NULL;
	cfg->dnscrypt_provider_cert = NULL;
	cfg->dnscrypt_provider_cert_rotated = NULL;
	cfg->dnscrypt_secret_key = NULL;
	cfg->dnscrypt_shared_secret_cache_size = 4*1024*1024;
	cfg->dnscrypt_shared_secret_cache_slabs = 4;
	cfg->dnscrypt_nonce_cache_size = 4*1024*1024;
	cfg->dnscrypt_nonce_cache_slabs = 4;
#ifdef USE_IPSECMOD
	cfg->ipsecmod_enabled = 1;
	cfg->ipsecmod_ignore_bogus = 0;
	cfg->ipsecmod_hook = NULL;
	cfg->ipsecmod_max_ttl = 3600;
	cfg->ipsecmod_whitelist = NULL;
	cfg->ipsecmod_strict = 0;
#endif
#ifdef USE_CACHEDB
	cfg->cachedb_backend = NULL;
	cfg->cachedb_secret = NULL;
#endif
	return cfg;
error_exit:
	config_delete(cfg); 
	return NULL;
}

struct config_file* config_create_forlib(void)
{
	struct config_file* cfg = config_create();
	if(!cfg) return NULL;
	/* modifications for library use, less verbose, less memory */
	free(cfg->chrootdir);
	cfg->chrootdir = NULL;
	cfg->verbosity = 0;
	cfg->outgoing_num_ports = 16; /* in library use, this is 'reasonable'
		and probably within the ulimit(maxfds) of the user */
	cfg->outgoing_num_tcp = 2;
	cfg->msg_cache_size = 1024*1024;
	cfg->msg_cache_slabs = 1;
	cfg->rrset_cache_size = 1024*1024;
	cfg->rrset_cache_slabs = 1;
	cfg->infra_cache_slabs = 1;
	cfg->use_syslog = 0;
	cfg->key_cache_size = 1024*1024;
	cfg->key_cache_slabs = 1;
	cfg->neg_cache_size = 100 * 1024;
	cfg->donotquery_localhost = 0; /* allow, so that you can ask a
		forward nameserver running on localhost */
	cfg->val_log_level = 2; /* to fill why_bogus with */
	cfg->val_log_squelch = 1;
	cfg->minimal_responses = 0;
	return cfg;
}

/** check that the value passed is >= 0 */
#define IS_NUMBER_OR_ZERO \
	if(atoi(val) == 0 && strcmp(val, "0") != 0) return 0
/** check that the value passed is > 0 */
#define IS_NONZERO_NUMBER \
	if(atoi(val) == 0) return 0
/** check that the value passed is not 0 and a power of 2 */
#define IS_POW2_NUMBER \
	if(atoi(val) == 0 || !is_pow2((size_t)atoi(val))) return 0
/** check that the value passed is yes or no */
#define IS_YES_OR_NO \
	if(strcmp(val, "yes") != 0 && strcmp(val, "no") != 0) return 0
/** put integer_or_zero into variable */
#define S_NUMBER_OR_ZERO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_NUMBER_OR_ZERO; cfg->var = atoi(val); }
/** put integer_nonzero into variable */
#define S_NUMBER_NONZERO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_NONZERO_NUMBER; cfg->var = atoi(val); }
/** put integer_or_zero into unsigned */
#define S_UNSIGNED_OR_ZERO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_NUMBER_OR_ZERO; cfg->var = (unsigned)atoi(val); }
/** put integer_or_zero into size_t */
#define S_SIZET_OR_ZERO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_NUMBER_OR_ZERO; cfg->var = (size_t)atoi(val); }
/** put integer_nonzero into size_t */
#define S_SIZET_NONZERO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_NONZERO_NUMBER; cfg->var = (size_t)atoi(val); }
/** put yesno into variable */
#define S_YNO(str, var) if(strcmp(opt, str) == 0) \
	{ IS_YES_OR_NO; cfg->var = (strcmp(val, "yes") == 0); }
/** put memsize into variable */
#define S_MEMSIZE(str, var) if(strcmp(opt, str)==0) \
	{ return cfg_parse_memsize(val, &cfg->var); }
/** put pow2 number into variable */
#define S_POW2(str, var) if(strcmp(opt, str)==0) \
	{ IS_POW2_NUMBER; cfg->var = (size_t)atoi(val); }
/** put string into variable */
#define S_STR(str, var) if(strcmp(opt, str)==0) \
	{ free(cfg->var); return (cfg->var = strdup(val)) != NULL; }
/** put string into strlist */
#define S_STRLIST(str, var) if(strcmp(opt, str)==0) \
	{ return cfg_strlist_insert(&cfg->var, strdup(val)); }
/** put string into strlist if not present yet*/
#define S_STRLIST_UNIQ(str, var) if(strcmp(opt, str)==0) \
	{ if(cfg_strlist_find(cfg->var, val)) { return 0;} \
	  return cfg_strlist_insert(&cfg->var, strdup(val)); }
/** append string to strlist */
#define S_STRLIST_APPEND(str, var) if(strcmp(opt, str)==0) \
	{ return cfg_strlist_append(&cfg->var, strdup(val)); }

int config_set_option(struct config_file* cfg, const char* opt,
	const char* val)
{
	char buf[64];
	if(!opt) return 0;
	if(opt[strlen(opt)-1] != ':' && strlen(opt)+2<sizeof(buf)) {
		snprintf(buf, sizeof(buf), "%s:", opt);
		opt = buf;
	}
	S_NUMBER_OR_ZERO("verbosity:", verbosity)
	else if(strcmp(opt, "statistics-interval:") == 0) {
		if(strcmp(val, "0") == 0 || strcmp(val, "") == 0)
			cfg->stat_interval = 0;
		else if(atoi(val) == 0)
			return 0;
		else cfg->stat_interval = atoi(val);
	} else if(strcmp(opt, "num_threads:") == 0) {
		/* not supported, library must have 1 thread in bgworker */
		return 0;
	} else if(strcmp(opt, "outgoing-port-permit:") == 0) {
		return cfg_mark_ports(val, 1, 
			cfg->outgoing_avail_ports, 65536);
	} else if(strcmp(opt, "outgoing-port-avoid:") == 0) {
		return cfg_mark_ports(val, 0, 
			cfg->outgoing_avail_ports, 65536);
	} else if(strcmp(opt, "local-zone:") == 0) {
		return cfg_parse_local_zone(cfg, val);
	} else if(strcmp(opt, "val-override-date:") == 0) {
		if(strcmp(val, "") == 0 || strcmp(val, "0") == 0) {
			cfg->val_date_override = 0;
		} else if(strlen(val) == 14) {
			cfg->val_date_override = cfg_convert_timeval(val);
			return cfg->val_date_override != 0;
		} else {
			if(atoi(val) == 0) return 0;
			cfg->val_date_override = (uint32_t)atoi(val);
		}
	} else if(strcmp(opt, "local-data-ptr:") == 0) { 
		char* ptr = cfg_ptr_reverse((char*)opt);
		return cfg_strlist_insert(&cfg->local_data, ptr);
	} else if(strcmp(opt, "logfile:") == 0) {
		cfg->use_syslog = 0;
		free(cfg->logfile);
		return (cfg->logfile = strdup(val)) != NULL;
	}
	else if(strcmp(opt, "log-time-ascii:") == 0)
	{ IS_YES_OR_NO; cfg->log_time_ascii = (strcmp(val, "yes") == 0);
	  log_set_time_asc(cfg->log_time_ascii); }
	else S_SIZET_NONZERO("max-udp-size:", max_udp_size)
	else S_YNO("use-syslog:", use_syslog)
	else S_STR("log-identity:", log_identity)
	else S_YNO("extended-statistics:", stat_extended)
	else S_YNO("statistics-cumulative:", stat_cumulative)
	else S_YNO("shm-enable:", shm_enable)
	else S_NUMBER_OR_ZERO("shm-key:", shm_key)
	else S_YNO("do-ip4:", do_ip4)
	else S_YNO("do-ip6:", do_ip6)
	else S_YNO("do-udp:", do_udp)
	else S_YNO("do-tcp:", do_tcp)
	else S_YNO("tcp-upstream:", tcp_upstream)
	else S_YNO("udp-upstream-without-downstream:",
		udp_upstream_without_downstream)
	else S_NUMBER_NONZERO("tcp-mss:", tcp_mss)
	else S_NUMBER_NONZERO("outgoing-tcp-mss:", outgoing_tcp_mss)
	else S_NUMBER_NONZERO("tcp-idle-timeout:", tcp_idle_timeout)
	else S_YNO("edns-tcp-keepalive:", do_tcp_keepalive)
	else S_NUMBER_NONZERO("edns-tcp-keepalive-timeout:", tcp_keepalive_timeout)
	else S_YNO("ssl-upstream:", ssl_upstream)
	else S_STR("ssl-service-key:", ssl_service_key)
	else S_STR("ssl-service-pem:", ssl_service_pem)
	else S_NUMBER_NONZERO("ssl-port:", ssl_port)
	else S_STR("tls-cert-bundle:", tls_cert_bundle)
	else S_YNO("tls-win-cert:", tls_win_cert)
	else S_STRLIST("additional-tls-port:", tls_additional_port)
	else S_STRLIST("tls-additional-ports:", tls_additional_port)
	else S_STRLIST("tls-additional-port:", tls_additional_port)
	else S_YNO("interface-automatic:", if_automatic)
	else S_YNO("use-systemd:", use_systemd)
	else S_YNO("do-daemonize:", do_daemonize)
	else S_NUMBER_NONZERO("port:", port)
	else S_NUMBER_NONZERO("outgoing-range:", outgoing_num_ports)
	else S_SIZET_OR_ZERO("outgoing-num-tcp:", outgoing_num_tcp)
	else S_SIZET_OR_ZERO("incoming-num-tcp:", incoming_num_tcp)
	else S_SIZET_NONZERO("edns-buffer-size:", edns_buffer_size)
	else S_SIZET_NONZERO("msg-buffer-size:", msg_buffer_size)
	else S_MEMSIZE("msg-cache-size:", msg_cache_size)
	else S_POW2("msg-cache-slabs:", msg_cache_slabs)
	else S_SIZET_NONZERO("num-queries-per-thread:",num_queries_per_thread)
	else S_SIZET_OR_ZERO("jostle-timeout:", jostle_time)
	else S_MEMSIZE("so-rcvbuf:", so_rcvbuf)
	else S_MEMSIZE("so-sndbuf:", so_sndbuf)
	else S_YNO("so-reuseport:", so_reuseport)
	else S_YNO("ip-transparent:", ip_transparent)
	else S_YNO("ip-freebind:", ip_freebind)
	else S_MEMSIZE("rrset-cache-size:", rrset_cache_size)
	else S_POW2("rrset-cache-slabs:", rrset_cache_slabs)
	else S_YNO("prefetch:", prefetch)
	else S_YNO("prefetch-key:", prefetch_key)
	else if(strcmp(opt, "cache-max-ttl:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->max_ttl = atoi(val); MAX_TTL=(time_t)cfg->max_ttl;}
	else if(strcmp(opt, "cache-max-negative-ttl:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->max_negative_ttl = atoi(val); MAX_NEG_TTL=(time_t)cfg->max_negative_ttl;}
	else if(strcmp(opt, "cache-min-ttl:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->min_ttl = atoi(val); MIN_TTL=(time_t)cfg->min_ttl;}
	else if(strcmp(opt, "infra-cache-min-rtt:") == 0) {
	    IS_NUMBER_OR_ZERO; cfg->infra_cache_min_rtt = atoi(val);
	    RTT_MIN_TIMEOUT=cfg->infra_cache_min_rtt;
	}
	else S_NUMBER_OR_ZERO("infra-host-ttl:", host_ttl)
	else S_POW2("infra-cache-slabs:", infra_cache_slabs)
	else S_SIZET_NONZERO("infra-cache-numhosts:", infra_cache_numhosts)
	else S_NUMBER_OR_ZERO("delay-close:", delay_close)
	else S_STR("chroot:", chrootdir)
	else S_STR("username:", username)
	else S_STR("directory:", directory)
	else S_STR("pidfile:", pidfile)
	else S_YNO("hide-identity:", hide_identity)
	else S_YNO("hide-version:", hide_version)
	else S_YNO("hide-trustanchor:", hide_trustanchor)
	else S_STR("identity:", identity)
	else S_STR("version:", version)
	else S_STRLIST("root-hints:", root_hints)
	else S_STR("target-fetch-policy:", target_fetch_policy)
	else S_YNO("harden-glue:", harden_glue)
	else S_YNO("harden-short-bufsize:", harden_short_bufsize)
	else S_YNO("harden-large-queries:", harden_large_queries)
	else S_YNO("harden-dnssec-stripped:", harden_dnssec_stripped)
	else S_YNO("harden-below-nxdomain:", harden_below_nxdomain)
	else S_YNO("harden-referral-path:", harden_referral_path)
	else S_YNO("harden-algo-downgrade:", harden_algo_downgrade)
	else S_YNO("use-caps-for-id:", use_caps_bits_for_id)
	else S_STRLIST("caps-whitelist:", caps_whitelist)
	else S_SIZET_OR_ZERO("unwanted-reply-threshold:", unwanted_threshold)
	else S_STRLIST("private-address:", private_address)
	else S_STRLIST("private-domain:", private_domain)
	else S_YNO("do-not-query-localhost:", donotquery_localhost)
	else S_STRLIST("do-not-query-address:", donotqueryaddrs)
	else S_STRLIST("auto-trust-anchor-file:", auto_trust_anchor_file_list)
	else S_STRLIST("trust-anchor-file:", trust_anchor_file_list)
	else S_STRLIST("trust-anchor:", trust_anchor_list)
	else S_STRLIST("trusted-keys-file:", trusted_keys_file_list)
	else S_YNO("trust-anchor-signaling:", trust_anchor_signaling)
	else S_YNO("root-key-sentinel:", root_key_sentinel)
	else S_STR("dlv-anchor-file:", dlv_anchor_file)
	else S_STRLIST("dlv-anchor:", dlv_anchor_list)
	else S_STRLIST("domain-insecure:", domain_insecure)
	else S_NUMBER_OR_ZERO("val-bogus-ttl:", bogus_ttl)
	else S_YNO("val-clean-additional:", val_clean_additional)
	else S_NUMBER_OR_ZERO("val-log-level:", val_log_level)
	else S_YNO("val-log-squelch:", val_log_squelch)
	else S_YNO("log-queries:", log_queries)
	else S_YNO("log-replies:", log_replies)
	else S_YNO("log-local-actions:", log_local_actions)
	else S_YNO("log-servfail:", log_servfail)
	else S_YNO("val-permissive-mode:", val_permissive_mode)
	else S_YNO("aggressive-nsec:", aggressive_nsec)
	else S_YNO("ignore-cd-flag:", ignore_cd)
	else S_YNO("serve-expired:", serve_expired)
	else if(strcmp(opt, "serve_expired_ttl:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->serve_expired_ttl = atoi(val); SERVE_EXPIRED_TTL=(time_t)cfg->serve_expired_ttl;}
	else S_YNO("serve-expired-ttl-reset:", serve_expired_ttl_reset)
	else S_STR("val-nsec3-keysize-iterations:", val_nsec3_key_iterations)
	else S_UNSIGNED_OR_ZERO("add-holddown:", add_holddown)
	else S_UNSIGNED_OR_ZERO("del-holddown:", del_holddown)
	else S_UNSIGNED_OR_ZERO("keep-missing:", keep_missing)
	else if(strcmp(opt, "permit-small-holddown:") == 0)
	{ IS_YES_OR_NO; cfg->permit_small_holddown = (strcmp(val, "yes") == 0);
	  autr_permit_small_holddown = cfg->permit_small_holddown; }
	else S_MEMSIZE("key-cache-size:", key_cache_size)
	else S_POW2("key-cache-slabs:", key_cache_slabs)
	else S_MEMSIZE("neg-cache-size:", neg_cache_size)
	else S_YNO("minimal-responses:", minimal_responses)
	else S_YNO("rrset-roundrobin:", rrset_roundrobin)
	else S_STRLIST("local-data:", local_data)
	else S_YNO("unblock-lan-zones:", unblock_lan_zones)
	else S_YNO("insecure-lan-zones:", insecure_lan_zones)
	else S_YNO("control-enable:", remote_control_enable)
	else S_STRLIST_APPEND("control-interface:", control_ifs)
	else S_NUMBER_NONZERO("control-port:", control_port)
	else S_STR("server-key-file:", server_key_file)
	else S_STR("server-cert-file:", server_cert_file)
	else S_STR("control-key-file:", control_key_file)
	else S_STR("control-cert-file:", control_cert_file)
	else S_STR("module-config:", module_conf)
	else S_STR("python-script:", python_script)
	else S_YNO("disable-dnssec-lame-check:", disable_dnssec_lame_check)
#ifdef CLIENT_SUBNET
	/* Can't set max subnet prefix here, since that value is used when
	 * generating the address tree. */
	/* No client-subnet-always-forward here, module registration depends on
	 * this option. */
#endif
#ifdef USE_DNSTAP
	else S_YNO("dnstap-enable:", dnstap)
	else S_STR("dnstap-socket-path:", dnstap_socket_path)
	else S_YNO("dnstap-send-identity:", dnstap_send_identity)
	else S_YNO("dnstap-send-version:", dnstap_send_version)
	else S_STR("dnstap-identity:", dnstap_identity)
	else S_STR("dnstap-version:", dnstap_version)
	else S_YNO("dnstap-log-resolver-query-messages:",
		dnstap_log_resolver_query_messages)
	else S_YNO("dnstap-log-resolver-response-messages:",
		dnstap_log_resolver_response_messages)
	else S_YNO("dnstap-log-client-query-messages:",
		dnstap_log_client_query_messages)
	else S_YNO("dnstap-log-client-response-messages:",
		dnstap_log_client_response_messages)
	else S_YNO("dnstap-log-forwarder-query-messages:",
		dnstap_log_forwarder_query_messages)
	else S_YNO("dnstap-log-forwarder-response-messages:",
		dnstap_log_forwarder_response_messages)
#endif
#ifdef USE_DNSCRYPT
	else S_YNO("dnscrypt-enable:", dnscrypt)
	else S_NUMBER_NONZERO("dnscrypt-port:", dnscrypt_port)
	else S_STR("dnscrypt-provider:", dnscrypt_provider)
	else S_STRLIST_UNIQ("dnscrypt-provider-cert:", dnscrypt_provider_cert)
	else S_STRLIST("dnscrypt-provider-cert-rotated:", dnscrypt_provider_cert_rotated)
	else S_STRLIST_UNIQ("dnscrypt-secret-key:", dnscrypt_secret_key)
	else S_MEMSIZE("dnscrypt-shared-secret-cache-size:",
		dnscrypt_shared_secret_cache_size)
	else S_POW2("dnscrypt-shared-secret-cache-slabs:",
		dnscrypt_shared_secret_cache_slabs)
	else S_MEMSIZE("dnscrypt-nonce-cache-size:",
		dnscrypt_nonce_cache_size)
	else S_POW2("dnscrypt-nonce-cache-slabs:",
		dnscrypt_nonce_cache_slabs)
#endif
	else if(strcmp(opt, "ip-ratelimit:") == 0) {
	    IS_NUMBER_OR_ZERO; cfg->ip_ratelimit = atoi(val);
	    infra_ip_ratelimit=cfg->ip_ratelimit;
	}
	else if(strcmp(opt, "ratelimit:") == 0) {
	    IS_NUMBER_OR_ZERO; cfg->ratelimit = atoi(val);
	    infra_dp_ratelimit=cfg->ratelimit;
	}
	else S_MEMSIZE("ip-ratelimit-size:", ip_ratelimit_size)
	else S_MEMSIZE("ratelimit-size:", ratelimit_size)
	else S_POW2("ip-ratelimit-slabs:", ip_ratelimit_slabs)
	else S_POW2("ratelimit-slabs:", ratelimit_slabs)
	else S_NUMBER_OR_ZERO("ip-ratelimit-factor:", ip_ratelimit_factor)
	else S_NUMBER_OR_ZERO("ratelimit-factor:", ratelimit_factor)
	else S_NUMBER_OR_ZERO("low-rtt:", low_rtt)
	else S_NUMBER_OR_ZERO("low-rtt-pct:", low_rtt_permil)
	else S_NUMBER_OR_ZERO("low-rtt-permil:", low_rtt_permil)
	else S_YNO("qname-minimisation:", qname_minimisation)
	else S_YNO("qname-minimisation-strict:", qname_minimisation_strict)
#ifdef USE_IPSECMOD
	else S_YNO("ipsecmod-enabled:", ipsecmod_enabled)
	else S_YNO("ipsecmod-ignore-bogus:", ipsecmod_ignore_bogus)
	else if(strcmp(opt, "ipsecmod-max-ttl:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->ipsecmod_max_ttl = atoi(val); }
	else S_YNO("ipsecmod-strict:", ipsecmod_strict)
#endif
	else if(strcmp(opt, "define-tag:") ==0) {
		return config_add_tag(cfg, val);
	/* val_sig_skew_min and max are copied into val_env during init,
	 * so this does not update val_env with set_option */
	} else if(strcmp(opt, "val-sig-skew-min:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->val_sig_skew_min = (int32_t)atoi(val); }
	else if(strcmp(opt, "val-sig-skew-max:") == 0)
	{ IS_NUMBER_OR_ZERO; cfg->val_sig_skew_max = (int32_t)atoi(val); }
	else if (strcmp(opt, "outgoing-interface:") == 0) {
		char* d = strdup(val);
		char** oi = 
		(char**)reallocarray(NULL, (size_t)cfg->num_out_ifs+1, sizeof(char*));
		if(!d || !oi) { free(d); free(oi); return -1; }
		if(cfg->out_ifs && cfg->num_out_ifs) {
			memmove(oi, cfg->out_ifs, cfg->num_out_ifs*sizeof(char*));
			free(cfg->out_ifs);
		}
		oi[cfg->num_out_ifs++] = d;
		cfg->out_ifs = oi;
	} else {
		/* unknown or unsupported (from the set_option interface):
		 * interface, outgoing-interface, access-control,
		 * stub-zone, name, stub-addr, stub-host, stub-prime
		 * forward-first, stub-first, forward-ssl-upstream,
		 * stub-ssl-upstream, forward-zone, auth-zone
		 * name, forward-addr, forward-host,
		 * ratelimit-for-domain, ratelimit-below-domain,
		 * local-zone-tag, access-control-view,
		 * send-client-subnet, client-subnet-always-forward,
		 * max-client-subnet-ipv4, max-client-subnet-ipv6, ipsecmod_hook,
		 * ipsecmod_whitelist. */
		return 0;
	}
	return 1;
}

void config_print_func(char* line, void* arg)
{
	FILE* f = (FILE*)arg;
	(void)fprintf(f, "%s\n", line);
}

/** collate func arg */
struct config_collate_arg {
	/** list of result items */
	struct config_strlist_head list;
	/** if a malloc error occurred, 0 is OK */
	int status;
};

void config_collate_func(char* line, void* arg)
{
	struct config_collate_arg* m = (struct config_collate_arg*)arg;
	if(m->status)
		return;
	if(!cfg_strlist_append(&m->list, strdup(line)))
		m->status = 1;
}

int config_get_option_list(struct config_file* cfg, const char* opt,
	struct config_strlist** list)
{
	struct config_collate_arg m;
	memset(&m, 0, sizeof(m));
	*list = NULL;
	if(!config_get_option(cfg, opt, config_collate_func, &m))
		return 1;
	if(m.status) {
		config_delstrlist(m.list.first);
		return 2;
	}
	*list = m.list.first;
	return 0;
}

int
config_get_option_collate(struct config_file* cfg, const char* opt, char** str)
{
	struct config_strlist* list = NULL;
	int r;
	*str = NULL;
	if((r = config_get_option_list(cfg, opt, &list)) != 0)
		return r;
	*str = config_collate_cat(list);
	config_delstrlist(list);
	if(!*str) return 2;
	return 0;
}

char*
config_collate_cat(struct config_strlist* list)
{
	size_t total = 0, left;
	struct config_strlist* s;
	char *r, *w;
	if(!list) /* no elements */
		return strdup("");
	if(list->next == NULL) /* one element , no newline at end. */
		return strdup(list->str);
	/* count total length */
	for(s=list; s; s=s->next)
		total += strlen(s->str) + 1; /* len + newline */
	left = total+1; /* one extra for nul at end */
	r = malloc(left); 
	if(!r)
		return NULL;
	w = r;
	for(s=list; s; s=s->next) {
		size_t this = strlen(s->str);
		if(this+2 > left) { /* sanity check */
			free(r);
			return NULL;
		}
		snprintf(w, left, "%s\n", s->str);
		this = strlen(w);
		w += this;
		left -= this;
	}
	return r;
}

/** compare and print decimal option */
#define O_DEC(opt, str, var) if(strcmp(opt, str)==0) \
	{snprintf(buf, len, "%d", (int)cfg->var); \
	func(buf, arg);}
/** compare and print unsigned option */
#define O_UNS(opt, str, var) if(strcmp(opt, str)==0) \
	{snprintf(buf, len, "%u", (unsigned)cfg->var); \
	func(buf, arg);}
/** compare and print yesno option */
#define O_YNO(opt, str, var) if(strcmp(opt, str)==0) \
	{func(cfg->var?"yes":"no", arg);}
/** compare and print string option */
#define O_STR(opt, str, var) if(strcmp(opt, str)==0) \
	{func(cfg->var?cfg->var:"", arg);}
/** compare and print array option */
#define O_IFC(opt, str, num, arr) if(strcmp(opt, str)==0) \
	{int i; for(i=0; i<cfg->num; i++) func(cfg->arr[i], arg);}
/** compare and print memorysize option */
#define O_MEM(opt, str, var) if(strcmp(opt, str)==0) { \
	if(cfg->var > 1024*1024*1024) {	\
	  size_t f=cfg->var/(size_t)1000000, b=cfg->var%(size_t)1000000; \
	  snprintf(buf, len, "%u%6.6u", (unsigned)f, (unsigned)b); \
	} else snprintf(buf, len, "%u", (unsigned)cfg->var); \
	func(buf, arg);}
/** compare and print list option */
#define O_LST(opt, name, lst) if(strcmp(opt, name)==0) { \
	struct config_strlist* p = cfg->lst; \
	for(p = cfg->lst; p; p = p->next) \
		func(p->str, arg); \
	}
/** compare and print list option */
#define O_LS2(opt, name, lst) if(strcmp(opt, name)==0) { \
	struct config_str2list* p = cfg->lst; \
	for(p = cfg->lst; p; p = p->next) { \
		snprintf(buf, len, "%s %s", p->str, p->str2); \
		func(buf, arg); \
	} \
	}
/** compare and print list option */
#define O_LS3(opt, name, lst) if(strcmp(opt, name)==0) { \
	struct config_str3list* p = cfg->lst; \
	for(p = cfg->lst; p; p = p->next) { \
		snprintf(buf, len, "%s %s %s", p->str, p->str2, p->str3); \
		func(buf, arg); \
	} \
	}
/** compare and print taglist option */
#define O_LTG(opt, name, lst) if(strcmp(opt, name)==0) { \
	char* tmpstr = NULL; \
	struct config_strbytelist *p = cfg->lst; \
	for(p = cfg->lst; p; p = p->next) {\
		tmpstr = config_taglist2str(cfg, p->str2, p->str2len); \
		if(tmpstr) {\
			snprintf(buf, len, "%s %s", p->str, tmpstr); \
			func(buf, arg); \
			free(tmpstr); \
		} \
	} \
	}

int
config_get_option(struct config_file* cfg, const char* opt, 
	void (*func)(char*,void*), void* arg)
{
	char buf[1024], nopt[64];
	size_t len = sizeof(buf);
	if(!opt) return 0;
	if(opt && opt[strlen(opt)-1] == ':' && strlen(opt)<sizeof(nopt)) {
		memmove(nopt, opt, strlen(opt));
		nopt[strlen(opt)-1] = 0;
		opt = nopt;
	}
	fptr_ok(fptr_whitelist_print_func(func));
	O_DEC(opt, "verbosity", verbosity)
	else O_DEC(opt, "statistics-interval", stat_interval)
	else O_YNO(opt, "statistics-cumulative", stat_cumulative)
	else O_YNO(opt, "extended-statistics", stat_extended)
	else O_YNO(opt, "shm-enable", shm_enable)
	else O_DEC(opt, "shm-key", shm_key)
	else O_YNO(opt, "use-syslog", use_syslog)
	else O_STR(opt, "log-identity", log_identity)
	else O_YNO(opt, "log-time-ascii", log_time_ascii)
	else O_DEC(opt, "num-threads", num_threads)
	else O_IFC(opt, "interface", num_ifs, ifs)
	else O_IFC(opt, "outgoing-interface", num_out_ifs, out_ifs)
	else O_YNO(opt, "interface-automatic", if_automatic)
	else O_DEC(opt, "port", port)
	else O_DEC(opt, "outgoing-range", outgoing_num_ports)
	else O_DEC(opt, "outgoing-num-tcp", outgoing_num_tcp)
	else O_DEC(opt, "incoming-num-tcp", incoming_num_tcp)
	else O_DEC(opt, "edns-buffer-size", edns_buffer_size)
	else O_DEC(opt, "msg-buffer-size", msg_buffer_size)
	else O_MEM(opt, "msg-cache-size", msg_cache_size)
	else O_DEC(opt, "msg-cache-slabs", msg_cache_slabs)
	else O_DEC(opt, "num-queries-per-thread", num_queries_per_thread)
	else O_UNS(opt, "jostle-timeout", jostle_time)
	else O_MEM(opt, "so-rcvbuf", so_rcvbuf)
	else O_MEM(opt, "so-sndbuf", so_sndbuf)
	else O_YNO(opt, "so-reuseport", so_reuseport)
	else O_YNO(opt, "ip-transparent", ip_transparent)
	else O_YNO(opt, "ip-freebind", ip_freebind)
	else O_MEM(opt, "rrset-cache-size", rrset_cache_size)
	else O_DEC(opt, "rrset-cache-slabs", rrset_cache_slabs)
	else O_YNO(opt, "prefetch-key", prefetch_key)
	else O_YNO(opt, "prefetch", prefetch)
	else O_DEC(opt, "cache-max-ttl", max_ttl)
	else O_DEC(opt, "cache-max-negative-ttl", max_negative_ttl)
	else O_DEC(opt, "cache-min-ttl", min_ttl)
	else O_DEC(opt, "infra-host-ttl", host_ttl)
	else O_DEC(opt, "infra-cache-slabs", infra_cache_slabs)
	else O_DEC(opt, "infra-cache-min-rtt", infra_cache_min_rtt)
	else O_MEM(opt, "infra-cache-numhosts", infra_cache_numhosts)
	else O_UNS(opt, "delay-close", delay_close)
	else O_YNO(opt, "do-ip4", do_ip4)
	else O_YNO(opt, "do-ip6", do_ip6)
	else O_YNO(opt, "do-udp", do_udp)
	else O_YNO(opt, "do-tcp", do_tcp)
	else O_YNO(opt, "tcp-upstream", tcp_upstream)
	else O_YNO(opt, "udp-upstream-without-downstream", udp_upstream_without_downstream)
	else O_DEC(opt, "tcp-mss", tcp_mss)
	else O_DEC(opt, "outgoing-tcp-mss", outgoing_tcp_mss)
	else O_DEC(opt, "tcp-idle-timeout", tcp_idle_timeout)
	else O_YNO(opt, "edns-tcp-keepalive", do_tcp_keepalive)
	else O_DEC(opt, "edns-tcp-keepalive-timeout", tcp_keepalive_timeout)
	else O_YNO(opt, "ssl-upstream", ssl_upstream)
	else O_STR(opt, "ssl-service-key", ssl_service_key)
	else O_STR(opt, "ssl-service-pem", ssl_service_pem)
	else O_DEC(opt, "ssl-port", ssl_port)
	else O_STR(opt, "tls-cert-bundle", tls_cert_bundle)
	else O_YNO(opt, "tls-win-cert", tls_win_cert)
	else O_LST(opt, "tls-additional-port", tls_additional_port)
	else O_YNO(opt, "use-systemd", use_systemd)
	else O_YNO(opt, "do-daemonize", do_daemonize)
	else O_STR(opt, "chroot", chrootdir)
	else O_STR(opt, "username", username)
	else O_STR(opt, "directory", directory)
	else O_STR(opt, "logfile", logfile)
	else O_YNO(opt, "log-queries", log_queries)
	else O_YNO(opt, "log-replies", log_replies)
	else O_YNO(opt, "log-local-actions", log_local_actions)
	else O_YNO(opt, "log-servfail", log_servfail)
	else O_STR(opt, "pidfile", pidfile)
	else O_YNO(opt, "hide-identity", hide_identity)
	else O_YNO(opt, "hide-version", hide_version)
	else O_YNO(opt, "hide-trustanchor", hide_trustanchor)
	else O_STR(opt, "identity", identity)
	else O_STR(opt, "version", version)
	else O_STR(opt, "target-fetch-policy", target_fetch_policy)
	else O_YNO(opt, "harden-short-bufsize", harden_short_bufsize)
	else O_YNO(opt, "harden-large-queries", harden_large_queries)
	else O_YNO(opt, "harden-glue", harden_glue)
	else O_YNO(opt, "harden-dnssec-stripped", harden_dnssec_stripped)
	else O_YNO(opt, "harden-below-nxdomain", harden_below_nxdomain)
	else O_YNO(opt, "harden-referral-path", harden_referral_path)
	else O_YNO(opt, "harden-algo-downgrade", harden_algo_downgrade)
	else O_YNO(opt, "use-caps-for-id", use_caps_bits_for_id)
	else O_LST(opt, "caps-whitelist", caps_whitelist)
	else O_DEC(opt, "unwanted-reply-threshold", unwanted_threshold)
	else O_YNO(opt, "do-not-query-localhost", donotquery_localhost)
	else O_STR(opt, "module-config", module_conf)
	else O_STR(opt, "dlv-anchor-file", dlv_anchor_file)
	else O_DEC(opt, "val-bogus-ttl", bogus_ttl)
	else O_YNO(opt, "val-clean-additional", val_clean_additional)
	else O_DEC(opt, "val-log-level", val_log_level)
	else O_YNO(opt, "val-permissive-mode", val_permissive_mode)
	else O_YNO(opt, "aggressive-nsec", aggressive_nsec)
	else O_YNO(opt, "ignore-cd-flag", ignore_cd)
	else O_YNO(opt, "serve-expired", serve_expired)
	else O_DEC(opt, "serve-expired-ttl", serve_expired_ttl)
	else O_YNO(opt, "serve-expired-ttl-reset", serve_expired_ttl_reset)
	else O_STR(opt, "val-nsec3-keysize-iterations",val_nsec3_key_iterations)
	else O_UNS(opt, "add-holddown", add_holddown)
	else O_UNS(opt, "del-holddown", del_holddown)
	else O_UNS(opt, "keep-missing", keep_missing)
	else O_YNO(opt, "permit-small-holddown", permit_small_holddown)
	else O_MEM(opt, "key-cache-size", key_cache_size)
	else O_DEC(opt, "key-cache-slabs", key_cache_slabs)
	else O_MEM(opt, "neg-cache-size", neg_cache_size)
	else O_YNO(opt, "control-enable", remote_control_enable)
	else O_DEC(opt, "control-port", control_port)
	else O_STR(opt, "server-key-file", server_key_file)
	else O_STR(opt, "server-cert-file", server_cert_file)
	else O_STR(opt, "control-key-file", control_key_file)
	else O_STR(opt, "control-cert-file", control_cert_file)
	else O_LST(opt, "root-hints", root_hints)
	else O_LS2(opt, "access-control", acls)
	else O_LS2(opt, "tcp-connection-limit", tcp_connection_limits)
	else O_LST(opt, "do-not-query-address", donotqueryaddrs)
	else O_LST(opt, "private-address", private_address)
	else O_LST(opt, "private-domain", private_domain)
	else O_LST(opt, "auto-trust-anchor-file", auto_trust_anchor_file_list)
	else O_LST(opt, "trust-anchor-file", trust_anchor_file_list)
	else O_LST(opt, "trust-anchor", trust_anchor_list)
	else O_LST(opt, "trusted-keys-file", trusted_keys_file_list)
	else O_YNO(opt, "trust-anchor-signaling", trust_anchor_signaling)
	else O_YNO(opt, "root-key-sentinel", root_key_sentinel)
	else O_LST(opt, "dlv-anchor", dlv_anchor_list)
	else O_LST(opt, "control-interface", control_ifs.first)
	else O_LST(opt, "domain-insecure", domain_insecure)
	else O_UNS(opt, "val-override-date", val_date_override)
	else O_YNO(opt, "minimal-responses", minimal_responses)
	else O_YNO(opt, "rrset-roundrobin", rrset_roundrobin)
#ifdef CLIENT_SUBNET
	else O_LST(opt, "send-client-subnet", client_subnet)
	else O_LST(opt, "client-subnet-zone", client_subnet_zone)
	else O_DEC(opt, "max-client-subnet-ipv4", max_client_subnet_ipv4)
	else O_DEC(opt, "max-client-subnet-ipv6", max_client_subnet_ipv6)
	else O_YNO(opt, "client-subnet-always-forward:",
		client_subnet_always_forward)
#endif
#ifdef USE_DNSTAP
	else O_YNO(opt, "dnstap-enable", dnstap)
	else O_STR(opt, "dnstap-socket-path", dnstap_socket_path)
	else O_YNO(opt, "dnstap-send-identity", dnstap_send_identity)
	else O_YNO(opt, "dnstap-send-version", dnstap_send_version)
	else O_STR(opt, "dnstap-identity", dnstap_identity)
	else O_STR(opt, "dnstap-version", dnstap_version)
	else O_YNO(opt, "dnstap-log-resolver-query-messages",
		dnstap_log_resolver_query_messages)
	else O_YNO(opt, "dnstap-log-resolver-response-messages",
		dnstap_log_resolver_response_messages)
	else O_YNO(opt, "dnstap-log-client-query-messages",
		dnstap_log_client_query_messages)
	else O_YNO(opt, "dnstap-log-client-response-messages",
		dnstap_log_client_response_messages)
	else O_YNO(opt, "dnstap-log-forwarder-query-messages",
		dnstap_log_forwarder_query_messages)
	else O_YNO(opt, "dnstap-log-forwarder-response-messages",
		dnstap_log_forwarder_response_messages)
#endif
#ifdef USE_DNSCRYPT
	else O_YNO(opt, "dnscrypt-enable", dnscrypt)
	else O_DEC(opt, "dnscrypt-port", dnscrypt_port)
	else O_STR(opt, "dnscrypt-provider", dnscrypt_provider)
	else O_LST(opt, "dnscrypt-provider-cert", dnscrypt_provider_cert)
	else O_LST(opt, "dnscrypt-provider-cert-rotated", dnscrypt_provider_cert_rotated)
	else O_LST(opt, "dnscrypt-secret-key", dnscrypt_secret_key)
	else O_MEM(opt, "dnscrypt-shared-secret-cache-size",
		dnscrypt_shared_secret_cache_size)
	else O_DEC(opt, "dnscrypt-shared-secret-cache-slabs",
		dnscrypt_shared_secret_cache_slabs)
	else O_MEM(opt, "dnscrypt-nonce-cache-size",
		dnscrypt_nonce_cache_size)
	else O_DEC(opt, "dnscrypt-nonce-cache-slabs",
		dnscrypt_nonce_cache_slabs)
#endif
	else O_YNO(opt, "unblock-lan-zones", unblock_lan_zones)
	else O_YNO(opt, "insecure-lan-zones", insecure_lan_zones)
	else O_DEC(opt, "max-udp-size", max_udp_size)
	else O_STR(opt, "python-script", python_script)
	else O_YNO(opt, "disable-dnssec-lame-check", disable_dnssec_lame_check)
	else O_DEC(opt, "ip-ratelimit", ip_ratelimit)
	else O_DEC(opt, "ratelimit", ratelimit)
	else O_MEM(opt, "ip-ratelimit-size", ip_ratelimit_size)
	else O_MEM(opt, "ratelimit-size", ratelimit_size)
	else O_DEC(opt, "ip-ratelimit-slabs", ip_ratelimit_slabs)
	else O_DEC(opt, "ratelimit-slabs", ratelimit_slabs)
	else O_LS2(opt, "ratelimit-for-domain", ratelimit_for_domain)
	else O_LS2(opt, "ratelimit-below-domain", ratelimit_below_domain)
	else O_DEC(opt, "ip-ratelimit-factor", ip_ratelimit_factor)
	else O_DEC(opt, "ratelimit-factor", ratelimit_factor)
	else O_DEC(opt, "low-rtt", low_rtt)
	else O_DEC(opt, "low-rtt-pct", low_rtt_permil)
	else O_DEC(opt, "low-rtt-permil", low_rtt_permil)
	else O_DEC(opt, "val-sig-skew-min", val_sig_skew_min)
	else O_DEC(opt, "val-sig-skew-max", val_sig_skew_max)
	else O_YNO(opt, "qname-minimisation", qname_minimisation)
	else O_YNO(opt, "qname-minimisation-strict", qname_minimisation_strict)
	else O_IFC(opt, "define-tag", num_tags, tagname)
	else O_LTG(opt, "local-zone-tag", local_zone_tags)
	else O_LTG(opt, "access-control-tag", acl_tags)
	else O_LTG(opt, "response-ip-tag", respip_tags)
	else O_LS3(opt, "local-zone-override", local_zone_overrides)
	else O_LS3(opt, "access-control-tag-action", acl_tag_actions)
	else O_LS3(opt, "access-control-tag-data", acl_tag_datas)
	else O_LS2(opt, "access-control-view", acl_view)
#ifdef USE_IPSECMOD
	else O_YNO(opt, "ipsecmod-enabled", ipsecmod_enabled)
	else O_YNO(opt, "ipsecmod-ignore-bogus", ipsecmod_ignore_bogus)
	else O_STR(opt, "ipsecmod-hook", ipsecmod_hook)
	else O_DEC(opt, "ipsecmod-max-ttl", ipsecmod_max_ttl)
	else O_LST(opt, "ipsecmod-whitelist", ipsecmod_whitelist)
	else O_YNO(opt, "ipsecmod-strict", ipsecmod_strict)
#endif
#ifdef USE_CACHEDB
	else O_STR(opt, "backend", cachedb_backend)
	else O_STR(opt, "secret-seed", cachedb_secret)
#endif
	/* not here:
	 * outgoing-permit, outgoing-avoid - have list of ports
	 * local-zone - zones and nodefault variables
	 * local-data - see below
	 * local-data-ptr - converted to local-data entries
	 * stub-zone, name, stub-addr, stub-host, stub-prime
	 * forward-zone, name, forward-addr, forward-host
	 */
	else return 0;
	return 1;
}

/** initialize the global cfg_parser object */
static void
create_cfg_parser(struct config_file* cfg, char* filename, const char* chroot)
{
	static struct config_parser_state st;
	cfg_parser = &st;
	cfg_parser->filename = filename;
	cfg_parser->line = 1;
	cfg_parser->errors = 0;
	cfg_parser->cfg = cfg;
	cfg_parser->chroot = chroot;
	init_cfg_parse();
}

int 
config_read(struct config_file* cfg, const char* filename, const char* chroot)
{
	FILE *in;
	char *fname = (char*)filename;
#ifdef HAVE_GLOB
	glob_t g;
	size_t i;
	int r, flags;
#endif
	if(!fname)
		return 1;

	/* check for wildcards */
#ifdef HAVE_GLOB
	if(!(!strchr(fname, '*') && !strchr(fname, '?') && !strchr(fname, '[') &&
		!strchr(fname, '{') && !strchr(fname, '~'))) {
		verbose(VERB_QUERY, "wildcard found, processing %s", fname);
		flags = 0
#ifdef GLOB_ERR
			| GLOB_ERR
#endif
#ifdef GLOB_NOSORT
			| GLOB_NOSORT
#endif
#ifdef GLOB_BRACE
			| GLOB_BRACE
#endif
#ifdef GLOB_TILDE
			| GLOB_TILDE
#endif
		;
		memset(&g, 0, sizeof(g));
		r = glob(fname, flags, NULL, &g);
		if(r) {
			/* some error */
			globfree(&g);
			if(r == GLOB_NOMATCH) {
				verbose(VERB_QUERY, "include: "
				"no matches for %s", fname);
				return 1; 
			} else if(r == GLOB_NOSPACE) {
				log_err("include: %s: "
					"fnametern out of memory", fname);
			} else if(r == GLOB_ABORTED) {
				log_err("wildcard include: %s: expansion "
					"aborted (%s)", fname, strerror(errno));
			} else {
				log_err("wildcard include: %s: expansion "
					"failed (%s)", fname, strerror(errno));
			}
			/* ignore globs that yield no files */
			return 1;
		}
		/* process files found, if any */
		for(i=0; i<(size_t)g.gl_pathc; i++) {
			if(!config_read(cfg, g.gl_pathv[i], chroot)) {
				log_err("error reading wildcard "
					"include: %s", g.gl_pathv[i]);
				globfree(&g);
				return 0;
			}
		}
		globfree(&g);
		return 1;
	}
#endif /* HAVE_GLOB */

	in = fopen(fname, "r");
	if(!in) {
		log_err("Could not open %s: %s", fname, strerror(errno));
		return 0;
	}
	create_cfg_parser(cfg, fname, chroot);
	ub_c_in = in;
	ub_c_parse();
	fclose(in);

	if(!cfg->dnscrypt) cfg->dnscrypt_port = 0;

	if(cfg_parser->errors != 0) {
		fprintf(stderr, "read %s failed: %d errors in configuration file\n",
			fname, cfg_parser->errors);
		errno=EINVAL;
		return 0;
	}

	return 1;
}

struct config_stub* cfg_stub_find(struct config_stub*** pp, const char* nm)
{
	struct config_stub* p = *(*pp);
	while(p) {
		if(strcmp(p->name, nm) == 0)
			return p;
		(*pp) = &p->next;
		p = p->next;
	}
	return NULL;
}

void
config_delstrlist(struct config_strlist* p)
{
	struct config_strlist *np;
	while(p) {
		np = p->next;
		free(p->str);
		free(p);
		p = np;
	}
}

void
config_deldblstrlist(struct config_str2list* p)
{
	struct config_str2list *np;
	while(p) {
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
}

void
config_deltrplstrlist(struct config_str3list* p)
{
	struct config_str3list *np;
	while(p) {
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p->str3);
		free(p);
		p = np;
	}
}

void
config_delauth(struct config_auth* p)
{
	if(!p) return;
	free(p->name);
	config_delstrlist(p->masters);
	config_delstrlist(p->urls);
	config_delstrlist(p->allow_notify);
	free(p->zonefile);
	free(p);
}

void
config_delauths(struct config_auth* p)
{
	struct config_auth* np;
	while(p) {
		np = p->next;
		config_delauth(p);
		p = np;
	}
}

void
config_delstub(struct config_stub* p)
{
	if(!p) return;
	free(p->name);
	config_delstrlist(p->hosts);
	config_delstrlist(p->addrs);
	free(p);
}

void
config_delstubs(struct config_stub* p)
{
	struct config_stub* np;
	while(p) {
		np = p->next;
		config_delstub(p);
		p = np;
	}
}

void
config_delview(struct config_view* p)
{
	if(!p) return;
	free(p->name);
	config_deldblstrlist(p->local_zones);
	config_delstrlist(p->local_zones_nodefault);
	config_delstrlist(p->local_data);
	free(p);
}

void
config_delviews(struct config_view* p)
{
	struct config_view* np;
	while(p) {
		np = p->next;
		config_delview(p);
		p = np;
	}
}
/** delete string array */
static void
config_del_strarray(char** array, int num)
{
	int i;
	if(!array)
		return;
	for(i=0; i<num; i++) {
		free(array[i]);
	}
	free(array);
}

void
config_del_strbytelist(struct config_strbytelist* p)
{
	struct config_strbytelist* np;
	while(p) {
		np = p->next;
		free(p->str);
		free(p->str2);
		free(p);
		p = np;
	}
}

void 
config_delete(struct config_file* cfg)
{
	if(!cfg) return;
	free(cfg->username);
	free(cfg->chrootdir);
	free(cfg->directory);
	free(cfg->logfile);
	free(cfg->pidfile);
	free(cfg->target_fetch_policy);
	free(cfg->ssl_service_key);
	free(cfg->ssl_service_pem);
	free(cfg->tls_cert_bundle);
	config_delstrlist(cfg->tls_additional_port);
	free(cfg->log_identity);
	config_del_strarray(cfg->ifs, cfg->num_ifs);
	config_del_strarray(cfg->out_ifs, cfg->num_out_ifs);
	config_delstubs(cfg->stubs);
	config_delstubs(cfg->forwards);
	config_delauths(cfg->auths);
	config_delviews(cfg->views);
	config_delstrlist(cfg->donotqueryaddrs);
	config_delstrlist(cfg->root_hints);
#ifdef CLIENT_SUBNET
	config_delstrlist(cfg->client_subnet);
	config_delstrlist(cfg->client_subnet_zone);
#endif
	free(cfg->identity);
	free(cfg->version);
	free(cfg->module_conf);
	free(cfg->outgoing_avail_ports);
	free(cfg->python_script);
	config_delstrlist(cfg->caps_whitelist);
	config_delstrlist(cfg->private_address);
	config_delstrlist(cfg->private_domain);
	config_delstrlist(cfg->auto_trust_anchor_file_list);
	config_delstrlist(cfg->trust_anchor_file_list);
	config_delstrlist(cfg->trusted_keys_file_list);
	config_delstrlist(cfg->trust_anchor_list);
	config_delstrlist(cfg->domain_insecure);
	free(cfg->dlv_anchor_file);
	config_delstrlist(cfg->dlv_anchor_list);
	config_deldblstrlist(cfg->acls);
	config_deldblstrlist(cfg->tcp_connection_limits);
	free(cfg->val_nsec3_key_iterations);
	config_deldblstrlist(cfg->local_zones);
	config_delstrlist(cfg->local_zones_nodefault);
	config_delstrlist(cfg->local_data);
	config_deltrplstrlist(cfg->local_zone_overrides);
	config_del_strarray(cfg->tagname, cfg->num_tags);
	config_del_strbytelist(cfg->local_zone_tags);
	config_del_strbytelist(cfg->acl_tags);
	config_del_strbytelist(cfg->respip_tags);
	config_deltrplstrlist(cfg->acl_tag_actions);
	config_deltrplstrlist(cfg->acl_tag_datas);
	config_delstrlist(cfg->control_ifs.first);
	free(cfg->server_key_file);
	free(cfg->server_cert_file);
	free(cfg->control_key_file);
	free(cfg->control_cert_file);
	free(cfg->dns64_prefix);
	config_delstrlist(cfg->dns64_ignore_aaaa);
	free(cfg->dnstap_socket_path);
	free(cfg->dnstap_identity);
	free(cfg->dnstap_version);
	config_deldblstrlist(cfg->ratelimit_for_domain);
	config_deldblstrlist(cfg->ratelimit_below_domain);
#ifdef USE_IPSECMOD
	free(cfg->ipsecmod_hook);
	config_delstrlist(cfg->ipsecmod_whitelist);
#endif
#ifdef USE_CACHEDB
	free(cfg->cachedb_backend);
	free(cfg->cachedb_secret);
#endif
	free(cfg);
}

static void 
init_outgoing_availports(int* a, int num)
{
	/* generated with make iana_update */
	const int iana_assigned[] = {
#include "util/iana_ports.inc"
		-1 }; /* end marker to put behind trailing comma */

	int i;
	/* do not use <1024, that could be trouble with the system, privs */
	for(i=1024; i<num; i++) {
		a[i] = i;
	}
	/* create empty spot at 49152 to keep ephemeral ports available 
	 * to other programs */
	for(i=49152; i<49152+256; i++)
		a[i] = 0;
	/* pick out all the IANA assigned ports */
	for(i=0; iana_assigned[i]!=-1; i++) {
		if(iana_assigned[i] < num)
			a[iana_assigned[i]] = 0;
	}
}

int 
cfg_mark_ports(const char* str, int allow, int* avail, int num)
{
	char* mid = strchr(str, '-');
	if(!mid) {
		int port = atoi(str);
		if(port == 0 && strcmp(str, "0") != 0) {
			log_err("cannot parse port number '%s'", str);
			return 0;
		}
		if(port < num)
			avail[port] = (allow?port:0);
	} else {
		int i, low, high = atoi(mid+1);
		char buf[16];
		if(high == 0 && strcmp(mid+1, "0") != 0) {
			log_err("cannot parse port number '%s'", mid+1);
			return 0;
		}
		if( (int)(mid-str)+1 >= (int)sizeof(buf) ) {
			log_err("cannot parse port number '%s'", str);
			return 0;
		}
		if(mid > str)
			memcpy(buf, str, (size_t)(mid-str));
		buf[mid-str] = 0;
		low = atoi(buf);
		if(low == 0 && strcmp(buf, "0") != 0) {
			log_err("cannot parse port number '%s'", buf);
			return 0;
		}
		for(i=low; i<=high; i++) {
			if(i < num)
				avail[i] = (allow?i:0);
		}
		return 1;
	}
	return 1;
}

int 
cfg_scan_ports(int* avail, int num)
{
	int i;
	int count = 0;
	for(i=0; i<num; i++) {
		if(avail[i])
			count++;
	}
	return count;
}

int cfg_condense_ports(struct config_file* cfg, int** avail)
{
	int num = cfg_scan_ports(cfg->outgoing_avail_ports, 65536);
	int i, at = 0;
	*avail = NULL;
	if(num == 0)
		return 0;
	*avail = (int*)reallocarray(NULL, (size_t)num, sizeof(int));
	if(!*avail)
		return 0;
	for(i=0; i<65536; i++) {
		if(cfg->outgoing_avail_ports[i])
			(*avail)[at++] = cfg->outgoing_avail_ports[i];
	}
	log_assert(at == num);
	return num;
}

/** print error with file and line number */
static void ub_c_error_va_list(const char *fmt, va_list args)
{
	cfg_parser->errors++;
	fprintf(stderr, "%s:%d: error: ", cfg_parser->filename,
	cfg_parser->line);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

/** print error with file and line number */
void ub_c_error_msg(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	ub_c_error_va_list(fmt, args);
	va_end(args);
}

void ub_c_error(const char *str)
{
	cfg_parser->errors++;
	fprintf(stderr, "%s:%d: error: %s\n", cfg_parser->filename,
		cfg_parser->line, str);
}

int ub_c_wrap(void)
{
	return 1;
}

int cfg_strlist_append(struct config_strlist_head* list, char* item)
{
	struct config_strlist *s;
	if(!item || !list) {
		free(item);
		return 0;
	}
	s = (struct config_strlist*)calloc(1, sizeof(struct config_strlist));
	if(!s) {
		free(item);
		return 0;
	}
	s->str = item;
	s->next = NULL;
	if(list->last)
		list->last->next = s;
	else
		list->first = s;
	list->last = s;
	return 1;
}

int 
cfg_region_strlist_insert(struct regional* region,
	struct config_strlist** head, char* item)
{
	struct config_strlist *s;
	if(!item || !head)
		return 0;
	s = (struct config_strlist*)regional_alloc_zero(region,
		sizeof(struct config_strlist));
	if(!s)
		return 0;
	s->str = item;
	s->next = *head;
	*head = s;
	return 1;
}

struct config_strlist*
cfg_strlist_find(struct config_strlist* head, const char *item)
{
	struct config_strlist *s = head;
	if(!head){
		return NULL;
	}
	while(s) {
		if(strcmp(s->str, item) == 0) {
			return s;
		}
		s = s->next;
	}
	return NULL;
}

int 
cfg_strlist_insert(struct config_strlist** head, char* item)
{
	struct config_strlist *s;
	if(!item || !head) {
		free(item);
		return 0;
	}
	s = (struct config_strlist*)calloc(1, sizeof(struct config_strlist));
	if(!s) {
		free(item);
		return 0;
	}
	s->str = item;
	s->next = *head;
	*head = s;
	return 1;
}

int 
cfg_str2list_insert(struct config_str2list** head, char* item, char* i2)
{
	struct config_str2list *s;
	if(!item || !i2 || !head) {
		free(item);
		free(i2);
		return 0;
	}
	s = (struct config_str2list*)calloc(1, sizeof(struct config_str2list));
	if(!s) {
		free(item);
		free(i2);
		return 0;
	}
	s->str = item;
	s->str2 = i2;
	s->next = *head;
	*head = s;
	return 1;
}

int 
cfg_str3list_insert(struct config_str3list** head, char* item, char* i2,
	char* i3)
{
	struct config_str3list *s;
	if(!item || !i2 || !i3 || !head)
		return 0;
	s = (struct config_str3list*)calloc(1, sizeof(struct config_str3list));
	if(!s)
		return 0;
	s->str = item;
	s->str2 = i2;
	s->str3 = i3;
	s->next = *head;
	*head = s;
	return 1;
}

int
cfg_strbytelist_insert(struct config_strbytelist** head, char* item,
	uint8_t* i2, size_t i2len)
{
	struct config_strbytelist* s;
	if(!item || !i2 || !head)
		return 0;
	s = (struct config_strbytelist*)calloc(1, sizeof(*s));
	if(!s)
		return 0;
	s->str = item;
	s->str2 = i2;
	s->str2len = i2len;
	s->next = *head;
	*head = s;
	return 1;
}

time_t 
cfg_convert_timeval(const char* str)
{
	time_t t;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	if(strlen(str) < 14)
		return 0;
	if(sscanf(str, "%4d%2d%2d%2d%2d%2d", &tm.tm_year, &tm.tm_mon, 
		&tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return 0;
	tm.tm_year -= 1900;
	tm.tm_mon--;
	/* Check values */
	if (tm.tm_year < 70)	return 0;
	if (tm.tm_mon < 0 || tm.tm_mon > 11)	return 0;
	if (tm.tm_mday < 1 || tm.tm_mday > 31) 	return 0;
	if (tm.tm_hour < 0 || tm.tm_hour > 23)	return 0;
	if (tm.tm_min < 0 || tm.tm_min > 59)	return 0;
	if (tm.tm_sec < 0 || tm.tm_sec > 59)	return 0;
	/* call ldns conversion function */
	t = sldns_mktime_from_utc(&tm);
	return t;
}

int 
cfg_count_numbers(const char* s)
{
	/* format ::= (sp num)+ sp  */
	/* num ::= [-](0-9)+        */
	/* sp ::= (space|tab)*      */
	int num = 0;
	while(*s) {
		while(*s && isspace((unsigned char)*s))
			s++;
		if(!*s) /* end of string */
			break;
		if(*s == '-')
			s++;
		if(!*s) /* only - not allowed */
			return 0;
		if(!isdigit((unsigned char)*s)) /* bad character */
			return 0;
		while(*s && isdigit((unsigned char)*s))
			s++;
		num++;
	}
	return num;
}

/** all digit number */
static int isalldigit(const char* str, size_t l)
{
	size_t i;
	for(i=0; i<l; i++)
		if(!isdigit((unsigned char)str[i]))
			return 0;
	return 1;
}

int 
cfg_parse_memsize(const char* str, size_t* res)
{
	size_t len;
	size_t mult = 1;
	if(!str || (len=(size_t)strlen(str)) == 0) {
		log_err("not a size: '%s'", str);
		return 0;
	}
	if(isalldigit(str, len)) {
		*res = (size_t)atol(str);
		return 1;
	}
	/* check appended num */
	while(len>0 && str[len-1]==' ')
		len--;
	if(len > 1 && str[len-1] == 'b') 
		len--;
	else if(len > 1 && str[len-1] == 'B') 
		len--;
	
	if(len > 1 && tolower((unsigned char)str[len-1]) == 'g')
		mult = 1024*1024*1024;
	else if(len > 1 && tolower((unsigned char)str[len-1]) == 'm')
		mult = 1024*1024;
	else if(len > 1 && tolower((unsigned char)str[len-1]) == 'k')
		mult = 1024;
	else if(len > 0 && isdigit((unsigned char)str[len-1]))
		mult = 1;
	else {
		log_err("unknown size specifier: '%s'", str);
		return 0;
	}
	while(len>1 && str[len-2]==' ')
		len--;

	if(!isalldigit(str, len-1)) {
		log_err("unknown size specifier: '%s'", str);
		return 0;
	}
	*res = ((size_t)atol(str)) * mult;
	return 1;
}

int
find_tag_id(struct config_file* cfg, const char* tag)
{
	int i;
	for(i=0; i<cfg->num_tags; i++) {
		if(strcmp(cfg->tagname[i], tag) == 0)
			return i;
	}
	return -1;
}

int
config_add_tag(struct config_file* cfg, const char* tag)
{
	char** newarray;
	char* newtag;
	if(find_tag_id(cfg, tag) != -1)
		return 1; /* nothing to do */
	newarray = (char**)malloc(sizeof(char*)*(cfg->num_tags+1));
	if(!newarray)
		return 0;
	newtag = strdup(tag);
	if(!newtag) {
		free(newarray);
		return 0;
	}
	if(cfg->tagname) {
		memcpy(newarray, cfg->tagname, sizeof(char*)*cfg->num_tags);
		free(cfg->tagname);
	}
	newarray[cfg->num_tags++] = newtag;
	cfg->tagname = newarray;
	return 1;
}

/** set a bit in a bit array */
static void
cfg_set_bit(uint8_t* bitlist, size_t len, int id)
{
	int pos = id/8;
	log_assert((size_t)pos < len);
	(void)len;
	bitlist[pos] |= 1<<(id%8);
}

uint8_t* config_parse_taglist(struct config_file* cfg, char* str,
        size_t* listlen)
{
	uint8_t* taglist = NULL;
	size_t len = 0;
	char* p, *s;

	/* allocate */
	if(cfg->num_tags == 0) {
		log_err("parse taglist, but no tags defined");
		return 0;
	}
	len = (size_t)(cfg->num_tags+7)/8;
	taglist = calloc(1, len);
	if(!taglist) {
		log_err("out of memory");
		return 0;
	}
	
	/* parse */
	s = str;
	while((p=strsep(&s, " \t\n")) != NULL) {
		if(*p) {
			int id = find_tag_id(cfg, p);
			/* set this bit in the bitlist */
			if(id == -1) {
				log_err("unknown tag: %s", p);
				free(taglist);
				return 0;
			}
			cfg_set_bit(taglist, len, id);
		}
	}

	*listlen = len;
	return taglist;
}

char* config_taglist2str(struct config_file* cfg, uint8_t* taglist,
        size_t taglen)
{
	char buf[10240];
	size_t i, j, len = 0;
	buf[0] = 0;
	for(i=0; i<taglen; i++) {
		if(taglist[i] == 0)
			continue;
		for(j=0; j<8; j++) {
			if((taglist[i] & (1<<j)) != 0) {
				size_t id = i*8 + j;
				snprintf(buf+len, sizeof(buf)-len, "%s%s",
					(len==0?"":" "), cfg->tagname[id]);
				len += strlen(buf+len);
			}
		}
	}
	return strdup(buf);
}

int taglist_intersect(uint8_t* list1, size_t list1len, uint8_t* list2,
	size_t list2len)
{
	size_t i;
	if(!list1 || !list2)
		return 0;
	for(i=0; i<list1len && i<list2len; i++) {
		if((list1[i] & list2[i]) != 0)
			return 1;
	}
	return 0;
}

void 
config_apply(struct config_file* config)
{
	MAX_TTL = (time_t)config->max_ttl;
	MIN_TTL = (time_t)config->min_ttl;
	SERVE_EXPIRED_TTL = (time_t)config->serve_expired_ttl;
	MAX_NEG_TTL = (time_t)config->max_negative_ttl;
	RTT_MIN_TIMEOUT = config->infra_cache_min_rtt;
	EDNS_ADVERTISED_SIZE = (uint16_t)config->edns_buffer_size;
	MINIMAL_RESPONSES = config->minimal_responses;
	RRSET_ROUNDROBIN = config->rrset_roundrobin;
	log_set_time_asc(config->log_time_ascii);
	autr_permit_small_holddown = config->permit_small_holddown;
}

void config_lookup_uid(struct config_file* cfg)
{
#ifdef HAVE_GETPWNAM
	/* translate username into uid and gid */
	if(cfg->username && cfg->username[0]) {
		struct passwd *pwd;
		if((pwd = getpwnam(cfg->username)) != NULL) {
			cfg_uid = pwd->pw_uid;
			cfg_gid = pwd->pw_gid;
		}
	}
#else
	(void)cfg;
#endif
}

/** 
 * Calculate string length of full pathname in original filesys
 * @param fname: the path name to convert.
 * 	Must not be null or empty.
 * @param cfg: config struct for chroot and chdir (if set).
 * @param use_chdir: if false, only chroot is applied.
 * @return length of string.
 *	remember to allocate one more for 0 at end in mallocs.
 */
static size_t
strlen_after_chroot(const char* fname, struct config_file* cfg, int use_chdir)
{
	size_t len = 0;
	int slashit = 0;
	if(cfg->chrootdir && cfg->chrootdir[0] && 
		strncmp(cfg->chrootdir, fname, strlen(cfg->chrootdir)) == 0) {
		/* already full pathname, return it */
		return strlen(fname);
	}
	/* chroot */
	if(cfg->chrootdir && cfg->chrootdir[0]) {
		/* start with chrootdir */
		len += strlen(cfg->chrootdir);
		slashit = 1;
	}
	/* chdir */
#ifdef UB_ON_WINDOWS
	if(fname[0] != 0 && fname[1] == ':') {
		/* full path, no chdir */
	} else
#endif
	if(fname[0] == '/' || !use_chdir) {
		/* full path, no chdir */
	} else if(cfg->directory && cfg->directory[0]) {
		/* prepend chdir */
		if(slashit && cfg->directory[0] != '/')
			len++;
		if(cfg->chrootdir && cfg->chrootdir[0] && 
			strncmp(cfg->chrootdir, cfg->directory, 
			strlen(cfg->chrootdir)) == 0)
			len += strlen(cfg->directory)-strlen(cfg->chrootdir);
		else	len += strlen(cfg->directory);
		slashit = 1;
	}
	/* fname */
	if(slashit && fname[0] != '/')
		len++;
	len += strlen(fname);
	return len;
}

char*
fname_after_chroot(const char* fname, struct config_file* cfg, int use_chdir)
{
	size_t len = strlen_after_chroot(fname, cfg, use_chdir)+1;
	int slashit = 0;
	char* buf = (char*)malloc(len);
	if(!buf)
		return NULL;
	buf[0] = 0;
	/* is fname already in chroot ? */
	if(cfg->chrootdir && cfg->chrootdir[0] && 
		strncmp(cfg->chrootdir, fname, strlen(cfg->chrootdir)) == 0) {
		/* already full pathname, return it */
		(void)strlcpy(buf, fname, len);
		buf[len-1] = 0;
		return buf;
	}
	/* chroot */
	if(cfg->chrootdir && cfg->chrootdir[0]) {
		/* start with chrootdir */
		(void)strlcpy(buf, cfg->chrootdir, len);
		slashit = 1;
	}
#ifdef UB_ON_WINDOWS
	if(fname[0] != 0 && fname[1] == ':') {
		/* full path, no chdir */
	} else
#endif
	/* chdir */
	if(fname[0] == '/' || !use_chdir) {
		/* full path, no chdir */
	} else if(cfg->directory && cfg->directory[0]) {
		/* prepend chdir */
		if(slashit && cfg->directory[0] != '/')
			(void)strlcat(buf, "/", len);
		/* is the directory already in the chroot? */
		if(cfg->chrootdir && cfg->chrootdir[0] && 
			strncmp(cfg->chrootdir, cfg->directory, 
			strlen(cfg->chrootdir)) == 0)
			(void)strlcat(buf, cfg->directory+strlen(cfg->chrootdir), 
				   len);
		else (void)strlcat(buf, cfg->directory, len);
		slashit = 1;
	}
	/* fname */
	if(slashit && fname[0] != '/')
		(void)strlcat(buf, "/", len);
	(void)strlcat(buf, fname, len);
	buf[len-1] = 0;
	return buf;
}

/** return next space character in string */
static char* next_space_pos(const char* str)
{
	char* sp = strchr(str, ' ');
	char* tab = strchr(str, '\t');
	if(!tab && !sp)
		return NULL;
	if(!sp) return tab;
	if(!tab) return sp;
	return (sp<tab)?sp:tab;
}

/** return last space character in string */
static char* last_space_pos(const char* str)
{
	char* sp = strrchr(str, ' ');
	char* tab = strrchr(str, '\t');
	if(!tab && !sp)
		return NULL;
	if(!sp) return tab;
	if(!tab) return sp;
	return (sp>tab)?sp:tab;
}

int 
cfg_parse_local_zone(struct config_file* cfg, const char* val)
{
	const char *type, *name_end, *name;
	char buf[256];

	/* parse it as: [zone_name] [between stuff] [zone_type] */
	name = val;
	while(*name && isspace((unsigned char)*name))
		name++;
	if(!*name) {
		log_err("syntax error: too short: %s", val);
		return 0;
	}
	name_end = next_space_pos(name);
	if(!name_end || !*name_end) {
		log_err("syntax error: expected zone type: %s", val);
		return 0;
	}
	if (name_end - name > 255) {
		log_err("syntax error: bad zone name: %s", val);
		return 0;
	}
	(void)strlcpy(buf, name, sizeof(buf));
	buf[name_end-name] = '\0';

	type = last_space_pos(name_end);
	while(type && *type && isspace((unsigned char)*type))
		type++;
	if(!type || !*type) {
		log_err("syntax error: expected zone type: %s", val);
		return 0;
	}

	if(strcmp(type, "nodefault")==0) {
		return cfg_strlist_insert(&cfg->local_zones_nodefault, 
			strdup(name));
	} else {
		return cfg_str2list_insert(&cfg->local_zones, strdup(buf),
			strdup(type));
	}
}

char* cfg_ptr_reverse(char* str)
{
	char* ip, *ip_end;
	char* name;
	char* result;
	char buf[1024];
	struct sockaddr_storage addr;
	socklen_t addrlen;

	/* parse it as: [IP] [between stuff] [name] */
	ip = str;
	while(*ip && isspace((unsigned char)*ip))
		ip++;
	if(!*ip) {
		log_err("syntax error: too short: %s", str);
		return NULL;
	}
	ip_end = next_space_pos(ip);
	if(!ip_end || !*ip_end) {
		log_err("syntax error: expected name: %s", str);
		return NULL;
	}

	name = last_space_pos(ip_end);
	if(!name || !*name) {
		log_err("syntax error: expected name: %s", str);
		return NULL;
	}

	sscanf(ip, "%100s", buf);
	buf[sizeof(buf)-1]=0;

	if(!ipstrtoaddr(buf, UNBOUND_DNS_PORT, &addr, &addrlen)) {
		log_err("syntax error: cannot parse address: %s", str);
		return NULL;
	}

	/* reverse IPv4:
	 * ddd.ddd.ddd.ddd.in-addr-arpa.
	 * IPv6: (h.){32}.ip6.arpa.  */

	if(addr_is_ip6(&addr, addrlen)) {
		uint8_t ad[16];
		const char* hex = "0123456789abcdef";
		char *p = buf;
		int i;
		memmove(ad, &((struct sockaddr_in6*)&addr)->sin6_addr, 
			sizeof(ad));
		for(i=15; i>=0; i--) {
			uint8_t b = ad[i];
			*p++ = hex[ (b&0x0f) ];
			*p++ = '.';
			*p++ = hex[ (b&0xf0) >> 4 ];
			*p++ = '.';
		}
		snprintf(buf+16*4, sizeof(buf)-16*4, "ip6.arpa. ");
	} else {
		uint8_t ad[4];
		memmove(ad, &((struct sockaddr_in*)&addr)->sin_addr, 
			sizeof(ad));
		snprintf(buf, sizeof(buf), "%u.%u.%u.%u.in-addr.arpa. ",
			(unsigned)ad[3], (unsigned)ad[2],
			(unsigned)ad[1], (unsigned)ad[0]);
	}

	/* printed the reverse address, now the between goop and name on end */
	while(*ip_end && isspace((unsigned char)*ip_end))
		ip_end++;
	if(name>ip_end) {
		snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "%.*s", 
			(int)(name-ip_end), ip_end);
	}
	snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), " PTR %s", name);

	result = strdup(buf);
	if(!result) {
		log_err("out of memory parsing %s", str);
		return NULL;
	}
	return result;
}

#ifdef UB_ON_WINDOWS
char*
w_lookup_reg_str(const char* key, const char* name)
{
	HKEY hk = NULL;
	DWORD type = 0;
	BYTE buf[1024];
	DWORD len = (DWORD)sizeof(buf);
	LONG ret;
	char* result = NULL;
	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hk);
	if(ret == ERROR_FILE_NOT_FOUND)
		return NULL; /* key does not exist */
	else if(ret != ERROR_SUCCESS) {
		log_err("RegOpenKeyEx failed");
		return NULL;
	}
	ret = RegQueryValueEx(hk, (LPCTSTR)name, 0, &type, buf, &len);
	if(RegCloseKey(hk))
		log_err("RegCloseKey");
	if(ret == ERROR_FILE_NOT_FOUND)
		return NULL; /* name does not exist */
	else if(ret != ERROR_SUCCESS) {
		log_err("RegQueryValueEx failed");
		return NULL;
	}
	if(type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ) {
		buf[sizeof(buf)-1] = 0;
		buf[sizeof(buf)-2] = 0; /* for multi_sz */
		result = strdup((char*)buf);
		if(!result) log_err("out of memory");
	}
	return result;
}

void w_config_adjust_directory(struct config_file* cfg)
{
	if(cfg->directory && cfg->directory[0]) {
		TCHAR dirbuf[2*MAX_PATH+4];
		if(strcmp(cfg->directory, "%EXECUTABLE%") == 0) {
			/* get executable path, and if that contains
			 * directories, snip off the filename part */
			dirbuf[0] = 0;
			if(!GetModuleFileName(NULL, dirbuf, MAX_PATH))
				log_err("could not GetModuleFileName");
			if(strrchr(dirbuf, '\\')) {
				(strrchr(dirbuf, '\\'))[0] = 0;
			} else log_err("GetModuleFileName had no path");
			if(dirbuf[0]) {
				/* adjust directory for later lookups to work*/
				free(cfg->directory);
				cfg->directory = memdup(dirbuf, strlen(dirbuf)+1);
			}
		}
	}
}
#endif /* UB_ON_WINDOWS */

void errinf(struct module_qstate* qstate, const char* str)
{
	struct config_strlist* p;
	if((qstate->env->cfg->val_log_level < 2 && !qstate->env->cfg->log_servfail) || !str)
		return;
	p = (struct config_strlist*)regional_alloc(qstate->region, sizeof(*p));
	if(!p) {
		log_err("malloc failure in validator-error-info string");
		return;
	}
	p->next = NULL;
	p->str = regional_strdup(qstate->region, str);
	if(!p->str) {
		log_err("malloc failure in validator-error-info string");
		return;
	}
	/* add at end */
	if(qstate->errinf) {
		struct config_strlist* q = qstate->errinf;
		while(q->next) 
			q = q->next;
		q->next = p;
	} else	qstate->errinf = p;
}

void errinf_origin(struct module_qstate* qstate, struct sock_list *origin)
{
	struct sock_list* p;
	if(qstate->env->cfg->val_log_level < 2 && !qstate->env->cfg->log_servfail)
		return;
	for(p=origin; p; p=p->next) {
		char buf[256];
		if(p == origin)
			snprintf(buf, sizeof(buf), "from ");
		else	snprintf(buf, sizeof(buf), "and ");
		if(p->len == 0)
			snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), 
				"cache");
		else 
			addr_to_str(&p->addr, p->len, buf+strlen(buf),
				sizeof(buf)-strlen(buf));
		errinf(qstate, buf);
	}
}

char* errinf_to_str_bogus(struct module_qstate* qstate)
{
	char buf[20480];
	char* p = buf;
	size_t left = sizeof(buf);
	struct config_strlist* s;
	char dname[LDNS_MAX_DOMAINLEN+1];
	char t[16], c[16];
	sldns_wire2str_type_buf(qstate->qinfo.qtype, t, sizeof(t));
	sldns_wire2str_class_buf(qstate->qinfo.qclass, c, sizeof(c));
	dname_str(qstate->qinfo.qname, dname);
	snprintf(p, left, "validation failure <%s %s %s>:", dname, t, c);
	left -= strlen(p); p += strlen(p);
	if(!qstate->errinf)
		snprintf(p, left, " misc failure");
	else for(s=qstate->errinf; s; s=s->next) {
		snprintf(p, left, " %s", s->str);
		left -= strlen(p); p += strlen(p);
	}
	p = strdup(buf);
	if(!p)
		log_err("malloc failure in errinf_to_str");
	return p;
}

char* errinf_to_str_servfail(struct module_qstate* qstate)
{
	char buf[20480];
	char* p = buf;
	size_t left = sizeof(buf);
	struct config_strlist* s;
	char dname[LDNS_MAX_DOMAINLEN+1];
	char t[16], c[16];
	sldns_wire2str_type_buf(qstate->qinfo.qtype, t, sizeof(t));
	sldns_wire2str_class_buf(qstate->qinfo.qclass, c, sizeof(c));
	dname_str(qstate->qinfo.qname, dname);
	snprintf(p, left, "SERVFAIL <%s %s %s>:", dname, t, c);
	left -= strlen(p); p += strlen(p);
	if(!qstate->errinf)
		snprintf(p, left, " misc failure");
	else for(s=qstate->errinf; s; s=s->next) {
		snprintf(p, left, " %s", s->str);
		left -= strlen(p); p += strlen(p);
	}
	p = strdup(buf);
	if(!p)
		log_err("malloc failure in errinf_to_str");
	return p;
}

void errinf_rrset(struct module_qstate* qstate, struct ub_packed_rrset_key *rr)
{
	char buf[1024];
	char dname[LDNS_MAX_DOMAINLEN+1];
	char t[16], c[16];
	if((qstate->env->cfg->val_log_level < 2 && !qstate->env->cfg->log_servfail) || !rr)
		return;
	sldns_wire2str_type_buf(ntohs(rr->rk.type), t, sizeof(t));
	sldns_wire2str_class_buf(ntohs(rr->rk.rrset_class), c, sizeof(c));
	dname_str(rr->rk.dname, dname);
	snprintf(buf, sizeof(buf), "for <%s %s %s>", dname, t, c);
	errinf(qstate, buf);
}

void errinf_dname(struct module_qstate* qstate, const char* str, uint8_t* dname)
{
	char b[1024];
	char buf[LDNS_MAX_DOMAINLEN+1];
	if((qstate->env->cfg->val_log_level < 2 && !qstate->env->cfg->log_servfail) || !str || !dname)
		return;
	dname_str(dname, buf);
	snprintf(b, sizeof(b), "%s %s", str, buf);
	errinf(qstate, b);
}

int options_remote_is_address(struct config_file* cfg)
{
	if(!cfg->remote_control_enable) return 0;
	if(!cfg->control_ifs.first) return 1;
	if(!cfg->control_ifs.first->str) return 1;
	if(cfg->control_ifs.first->str[0] == 0) return 1;
	return (cfg->control_ifs.first->str[0] != '/');
}
